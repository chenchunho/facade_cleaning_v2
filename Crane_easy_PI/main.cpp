// ============================================================================
// Crane_easy_PI — 獨立的簡易吊車主程式
//
// 跑在獨立的 Raspberry Pi (192.168.5.26) 上，控制一套跟 main crane 不同的
// 簡易吊車硬體（單路上下、一顆張力感測）。與 washrobot / main crane 邏輯
// 完全解耦，只透過 Web Backend 介接到同一個 Browser GUI。
//
// TCP command server @ :5003 (line-based, 多 client)
//
// Command set (精簡版):
//   up   <on|off>   # 拉繩 ON/OFF (press-and-hold 模式)
//   down <on|off>   # 釋放繩 ON/OFF
//   stop            # 兩路繼電器立即 OFF
//   status          # 查當前重量 + on/off 狀態
//   ping            # watchdog 心跳
//
// Reply format: OK [data]\n  /  ERR <reason>\n  /  EVT <type> <data>\n
//
// 防呆（三層）:
//   (a) Server watchdog  — motion_active 且超過 WATCHDOG_TIMEOUT_MS 沒收到
//                          任何 inbound → 自動 all_off + EVT
//   (b) 重量門檻          — UP 過程中，weight 持續低於 WEIGHT_UP_STOP_KG
//                          WEIGHT_SUSTAIN_MS 以上 → 自動 all_off + EVT
//   (c) 前端 press-and-hold + 每 500ms heartbeat（見 web_backend/public/app.js）
// ============================================================================

#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>

#include "TCP_client.h"
#include "TCP_server.h"
#include "ZS_DIO_R_RLY.h"
#include "DY_500_weight_sensor.h"

//=========== config ===========

static constexpr int    CMD_PORT            = 5003;
static const char* const GW_21_IP           = "192.168.1.21";  // relay gateway
static const char* const GW_22_IP           = "192.168.1.22";  // weight sensor gateway
static constexpr int    GW_PORT             = 4001;
static constexpr int    RELAY_SLAVE         = 1;   // 原碼寫 16 但舊驅動 buildRelayCommand 硬寫 0x01 → 實機 slave 為 1
static constexpr int    DY500_SLAVE         = 3;
static constexpr int    PIN_UP              = 8;   // relay channel: pull rope (retract)
static constexpr int    PIN_DOWN            = 7;   // relay channel: release rope (pay out)

static constexpr int    WATCHDOG_TIMEOUT_MS = 2000;
static constexpr int    WATCHDOG_POLL_MS    = 250;
// Weight loop runs as fast as Modbus RTT allows (no artificial sleep).
// WEIGHT_YIELD_MS is just a tiny CPU-yield between reads.
static constexpr int    WEIGHT_YIELD_MS     = 1;
static constexpr int    WEIGHT_AVG_N        = 10;          // 10-sample avg for GUI display only
static constexpr int    WEIGHT_SUSTAIN_MS   = 0;           // 0 = trigger on first sample below threshold (trade noise-tolerance for speed)
// If DY500 read fails continuously for this long while motion_active → auto stop + EVT.
static constexpr int    READ_FAIL_STOP_MS   = 500;
// Default UP-stop threshold (user can override via `set_up_stop_kg` command).
// Sign convention: load on rope reads negative; more negative = higher tension.
static constexpr float  DEFAULT_UP_STOP_KG  = -20.0f;

//=========== drivers ===========

static TCP_client           cli_21;
static TCP_client           cli_22;
static ZS_DIO_R_RLY         relay;
static DY_500_weight_sensor dy;
static TCP_server           cmd_server;

//=========== state ===========

static std::atomic<bool>    running(true);
static std::atomic<bool>    up_on(false);
static std::atomic<bool>    down_on(false);
static std::atomic<int64_t> last_inbound_ms(0);
static std::atomic<float>   g_weight(0.0f);
static std::atomic<float>   g_up_stop_kg(DEFAULT_UP_STOP_KG);   // runtime-configurable via `set_up_stop_kg`
static std::atomic<bool>    g_weight_valid(false);              // false = DY500 讀取失敗中

//=========== utility ===========

static int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static void touch_heartbeat() { last_inbound_ms.store(now_ms()); }

static bool motion_active() { return up_on.load() || down_on.load(); }

static void all_off() {
    // Skip redundant Modbus writes when a relay is already OFF — each skipped
    // write saves ~30ms RTT, which matters for safety-triggered stops.
    if (up_on.exchange(false))   relay.controlRelay(PIN_UP,   false);
    if (down_on.exchange(false)) relay.controlRelay(PIN_DOWN, false);
}

static void evt(const std::string& line) {
    std::string s = "EVT " + line + "\n";
    cmd_server.broadcast(s.c_str(), (int)s.size());
}

//=========== background threads ===========

// Weight monitor.
//   Reads DY-500 as fast as Modbus RTT allows（~20-50ms/cycle on LAN）；
//   用 raw 單次讀值做 UP 安全檢查（最低延遲），另累積 WEIGHT_AVG_N 筆平均寫進 g_weight 供 GUI 顯示。
//   (a) UP 安全：raw weight < g_up_stop_kg 累積 WEIGHT_SUSTAIN_MS → all_off + EVT weight_limit
//   (b) 讀失敗：連續失敗 READ_FAIL_STOP_MS 且 motion_active → all_off + EVT weight_read_fail
//   累積時間用 steady_clock 實測（不假設固定間隔）以正確計算 sustain 閾值。
static void weight_loop() {
    double sum = 0.0;
    int    cnt     = 0;
    int    over_ms = 0;
    int    fail_ms = 0;

    auto t_last = std::chrono::steady_clock::now();

    while (running.load()) {
        float w = 0.0f;
        bool  read_fail = (bool)dy.get_weight_float(w);

        auto t_now = std::chrono::steady_clock::now();
        int  dt_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t_now - t_last).count();
        if (dt_ms < 1) dt_ms = 1;
        t_last = t_now;

        if (read_fail) {
            fail_ms += dt_ms;
            if (fail_ms >= READ_FAIL_STOP_MS) {
                g_weight_valid.store(false);
                if (motion_active()) {
                    all_off();
                    std::ostringstream oss;
                    oss << "weight_read_fail duration_ms=" << fail_ms;
                    evt(oss.str());
                }
                fail_ms = 0;  // throttle
            }
            over_ms = 0;
        } else {
            fail_ms = 0;
            g_weight_valid.store(true);

            // UP safety — raw w for minimum latency
            if (up_on.load() && w < g_up_stop_kg.load()) {
                over_ms += dt_ms;
                if (over_ms >= WEIGHT_SUSTAIN_MS) {
                    all_off();
                    std::ostringstream oss;
                    oss << "weight_limit direction=up weight=" << w
                        << " threshold=" << g_up_stop_kg.load();
                    evt(oss.str());
                    over_ms = 0;
                }
            } else {
                over_ms = 0;
            }

            // Update avg for display
            sum += w;
            if (++cnt >= WEIGHT_AVG_N) {
                g_weight.store(static_cast<float>(sum / cnt));
                sum = 0.0;
                cnt = 0;
            }
        }

        // Minimal yield — real rate-limit is the Modbus RTT inside get_weight_float.
        std::this_thread::sleep_for(std::chrono::milliseconds(WEIGHT_YIELD_MS));
    }
}

// Server watchdog — any inbound packet counts as heartbeat. If motion_active
// and no inbound for > WATCHDOG_TIMEOUT_MS → assume disconnect → auto stop.
static void watchdog_loop() {
    last_inbound_ms.store(now_ms());
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(WATCHDOG_POLL_MS));
        if (!running.load()) break;
        if (!motion_active()) continue;

        int64_t elapsed = now_ms() - last_inbound_ms.load();
        if (elapsed > WATCHDOG_TIMEOUT_MS) {
            all_off();
            evt("watchdog_timeout state=aborted");
        }
    }
}

//=========== commands ===========

static std::string cmd_up(bool on) {
    if (on) {
        if (!g_weight_valid.load()) return "ERR weight_read_fail\n";
        if (g_weight.load() < g_up_stop_kg.load()) {
            std::ostringstream oss;
            oss << "ERR weight_limit weight=" << g_weight.load()
                << " threshold=" << g_up_stop_kg.load() << "\n";
            return oss.str();
        }
        if (down_on.load()) {
            relay.controlRelay(PIN_DOWN, false);
            down_on.store(false);
        }
        if (relay.controlRelay(PIN_UP, true)) return "ERR relay_up_on_fail\n";
        up_on.store(true);
    } else {
        relay.controlRelay(PIN_UP, false);
        up_on.store(false);
    }
    return "OK\n";
}

static std::string cmd_down(bool on) {
    if (on) {
        if (!g_weight_valid.load()) return "ERR weight_read_fail\n";
        if (up_on.load()) {
            relay.controlRelay(PIN_UP, false);
            up_on.store(false);
        }
        if (relay.controlRelay(PIN_DOWN, true)) return "ERR relay_down_on_fail\n";
        down_on.store(true);
    } else {
        relay.controlRelay(PIN_DOWN, false);
        down_on.store(false);
    }
    return "OK\n";
}

static std::string cmd_set_up_stop_kg(float v) {
    g_up_stop_kg.store(v);
    std::ostringstream oss;
    oss << "OK up_stop_kg=" << v << "\n";
    return oss.str();
}

static std::string cmd_stop() {
    all_off();
    return "OK\n";
}

static std::string cmd_status() {
    std::ostringstream oss;
    oss << "OK weight=" << g_weight.load()
        << " up="            << (up_on.load()   ? 1 : 0)
        << " down="          << (down_on.load() ? 1 : 0)
        << " up_stop_kg="    << g_up_stop_kg.load()
        << " weight_valid="  << (g_weight_valid.load() ? 1 : 0)
        << "\n";
    return oss.str();
}

static std::string cmd_ping() { return "OK pong\n"; }

static std::string dispatch(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd; iss >> cmd;

    if (cmd == "up") {
        std::string s; iss >> s;
        if (s == "on")  return cmd_up(true);
        if (s == "off") return cmd_up(false);
        return "ERR usage:up_<on|off>\n";
    }
    if (cmd == "down") {
        std::string s; iss >> s;
        if (s == "on")  return cmd_down(true);
        if (s == "off") return cmd_down(false);
        return "ERR usage:down_<on|off>\n";
    }
    if (cmd == "stop")   return cmd_stop();
    if (cmd == "status") return cmd_status();
    if (cmd == "ping")   return cmd_ping();
    if (cmd == "set_up_stop_kg") {
        float v;
        if (!(iss >> v)) return "ERR usage:set_up_stop_kg_<kg>\n";
        return cmd_set_up_stop_kg(v);
    }
    return "ERR unknown_cmd\n";
}

//=========== TCP receive callback ===========

static void on_receive(socket_t sock, const char* data, int len) {
    touch_heartbeat();  // any inbound == heartbeat
    thread_local std::string rx_buf;
    rx_buf.append(data, len);

    size_t pos;
    while ((pos = rx_buf.find('\n')) != std::string::npos) {
        std::string line = rx_buf.substr(0, pos);
        rx_buf.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        const std::string reply = dispatch(line);
        cmd_server.sendToClient(sock, reply.c_str(), (int)reply.size());
    }
}

//=========== main ===========

int main() {
    std::cout << "[Crane_easy_PI] starting...\n";

    if (!cli_21.connectToServer(GW_21_IP, GW_PORT))
        std::cerr << "[WARN] gateway " << GW_21_IP << ":" << GW_PORT << " not reachable\n";
    else
        std::cout << "[OK] gateway " << GW_21_IP << "\n";

    if (!cli_22.connectToServer(GW_22_IP, GW_PORT))
        std::cerr << "[WARN] gateway " << GW_22_IP << ":" << GW_PORT << " not reachable\n";
    else
        std::cout << "[OK] gateway " << GW_22_IP << "\n";

    relay.init(cli_21, RELAY_SLAVE, 16, false);   // total_relay=16, debug=false
    dy.init(cli_22, DY500_SLAVE, false);

    // Safe startup: both relays off
    relay.controlRelay(PIN_UP,   false);
    relay.controlRelay(PIN_DOWN, false);

    std::thread t_weight(weight_loop);
    std::thread t_watchdog(watchdog_loop);

    cmd_server.setReceiveCallback(on_receive);
    if (!cmd_server.start(CMD_PORT, false)) {
        std::cerr << "[FATAL] TCP :" << CMD_PORT << " fail\n";
        running = false;
        t_weight.join();
        t_watchdog.join();
        return 1;
    }
    std::cout << "[OK] easy crane server :" << CMD_PORT << " (type 'exit' to stop)\n";

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "exit" || line == "quit") break;
        if (line == "status") std::cout << cmd_status();
    }

    std::cout << "[SHUTDOWN] stopping...\n";
    running = false;
    all_off();
    cmd_server.stop();
    t_weight.join();
    t_watchdog.join();
    return 0;
}
