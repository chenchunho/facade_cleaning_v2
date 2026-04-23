// ============================================================================
// Crane_control_PI — Rooftop crane controller
//
// Hardware (see CLAUDE.md / motion_flow.md §2):
//   RPi @ 192.168.1.101
//   USR-TCP232-304 @ 192.168.1.30 : 4001
//     slave 1 : ZS_DIO_R_RLY 8CH
//                 CH1 = retract_left   (左收繩)
//                 CH2 = retract_right  (右收繩)
//                 CH3 = pay_out_left   (左放繩)
//                 CH4 = pay_out_right  (右放繩)
//     slave 2 : SD76 #1  左鋼索計米
//     slave 3 : SD76 #2  右鋼索計米
//     slave 4 : SD76 #3  中間管線計米（水管+電線）
//     slave 7 : CLV900   中間絞盤變頻器
//     slave 5,6 : DSZL-107 × 2 張力感測（Step 3 再接，驅動未完成）
//
// Command TCP server @ :5002 (line-based text protocol, multi-client)
//   pay_out <cm> / retract <cm>       # 雙繩 + 中間管線同步 (× K)
//   pay_out_left|right <on|off>
//   retract_left|right <on|off>
//   middle_set <rpm> <pay|retract|stop>
//   zero_meters <ground|top>
//   home_status
//   roll_correct <delta_cm>           # + = 左放右收
//   stop / status / ping
//
// Reply format: OK [data]\n / ERR <reason>\n / EVT <type> <data>\n
// ============================================================================

#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdlib>
#include <cmath>

#ifndef _WIN32
#include <signal.h>
#endif

#include "TCP_client.h"
#include "TCP_server.h"
#include "ZS_DIO_R_RLY.h"
#include "SD76_length_meters.h"
#include "CLV900_inverter.h"

// ============ Hardware config ============

static constexpr const char* USR_IP   = "192.168.1.30";
static constexpr int         USR_PORT = 4001;
static constexpr int         CMD_PORT = 5002;

static constexpr int RELAY_SLAVE        = 1;
static constexpr int METER_LEFT_SLAVE   = 2;
static constexpr int METER_RIGHT_SLAVE  = 3;
static constexpr int METER_MIDDLE_SLAVE = 4;
static constexpr int INVERTER_SLAVE     = 7;

// Relay channels (ZS_DIO CH1~4)
static constexpr int CH_RETRACT_LEFT  = 1;
static constexpr int CH_RETRACT_RIGHT = 2;
static constexpr int CH_PAY_OUT_LEFT  = 3;
static constexpr int CH_PAY_OUT_RIGHT = 4;

// Motion tunables (motion_flow.md §6)
static constexpr int    MOTION_TIMEOUT_MS    = 120000;
static constexpr int    POLL_INTERVAL_MS     = 50;
static constexpr double MIDDLE_WINCH_RATIO_K = 1.00;
static constexpr double MIDDLE_WINCH_HZ      = 20.0;  // TODO: tune on site (MIDDLE_WINCH_RPM placeholder)
static constexpr double CLV900_MAX_HZ        = 50.0;  // F8-03 default

// Watchdog tunables (motion_flow.md §6)
static constexpr int WATCHDOG_TIMEOUT_MS = 2000;
static constexpr int HEARTBEAT_CHECK_MS  = 250;

// ============ Globals ============

static TCP_client         cli_30;
static ZS_DIO_R_RLY       relay;
static SD76_length_meters meter_left;
static SD76_length_meters meter_right;
static SD76_length_meters meter_middle;
static CLV900_inverter    inverter;
static TCP_server         cmd_server;

static std::atomic<bool> abort_flag(false);
static std::mutex        motion_mtx;

// Phase 1 top-zero snapshot: rope length from top to ground.
// Set by `zero_meters top` (= |SD76 left| just before reset).
static std::atomic<int32_t> home_ground_cm(0);

// Watchdog state
static std::atomic<uint64_t> last_ping_ms(0);     // 0 = no activity yet
static std::atomic<bool>     motion_active(false);
static std::atomic<bool>     watchdog_fired(false);
static std::atomic<bool>     watchdog_stop(false);
static std::thread           watchdog_thread;

// ============ Low-level helpers ============

static void allRelayOff() {
    relay.controlRelay(CH_RETRACT_LEFT,  false);
    relay.controlRelay(CH_RETRACT_RIGHT, false);
    relay.controlRelay(CH_PAY_OUT_LEFT,  false);
    relay.controlRelay(CH_PAY_OUT_RIGHT, false);
}

static void allMotionOff() {
    allRelayOff();
    inverter.stopDecel();
}

// Direction convention (wiring-dependent — flip fwd/rev if inverted on site):
//   pay_out = runForward, retract = runReverse.
static bool middleStart(bool pay_out) {
    if (inverter.setFreqHz(MIDDLE_WINCH_HZ, CLV900_MAX_HZ)) return true;
    return pay_out ? inverter.runForward() : inverter.runReverse();
}

// ============ Watchdog ============

static uint64_t now_ms() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void broadcast_evt(const std::string& s) {
    cmd_server.broadcast(s.c_str(), (int)s.size());
}

// Called on any inbound TCP data — treats any byte as "peer alive".
static void touch_heartbeat() {
    last_ping_ms.store(now_ms());
    if (watchdog_fired.exchange(false)) {
        broadcast_evt("EVT watchdog_recovered\n");
    }
}

static void watchdog_loop() {
    while (!watchdog_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(HEARTBEAT_CHECK_MS));

        if (cmd_server.getConnectedClients().empty()) continue;

        const uint64_t last = last_ping_ms.load();
        if (last == 0) continue;  // no heartbeat yet since startup

        if (now_ms() - last <= WATCHDOG_TIMEOUT_MS) continue;

        // exchange guards against re-firing on every tick
        if (watchdog_fired.exchange(true)) continue;

        if (motion_active.load()) {
            abort_flag = true;
            allMotionOff();
            broadcast_evt("EVT watchdog_timeout state=aborted\n");
        } else {
            broadcast_evt("EVT watchdog_timeout state=idle\n");
        }
    }
}

// RAII guard: marks motion_active=true on construction, false on destruction.
struct MotionScope {
    MotionScope()  { motion_active.store(true);  }
    ~MotionScope() { motion_active.store(false); }
};

// ============ pay_out / retract (both ropes + middle pipeline) ============

static std::string motion_rope(int cm, bool is_retract) {
    if (cm <= 0) return "ERR invalid_cm\n";

    std::lock_guard<std::mutex> lock(motion_mtx);
    MotionScope ms;
    abort_flag = false;

    int32_t base_left = 0, base_right = 0, base_middle = 0;
    if (meter_left.readUpperInteger(base_left))     return "ERR meter_left_read_fail\n";
    if (meter_right.readUpperInteger(base_right))   return "ERR meter_right_read_fail\n";
    if (meter_middle.readUpperInteger(base_middle)) return "ERR meter_middle_read_fail\n";

    const int ch_left  = is_retract ? CH_RETRACT_LEFT  : CH_PAY_OUT_LEFT;
    const int ch_right = is_retract ? CH_RETRACT_RIGHT : CH_PAY_OUT_RIGHT;
    const int middle_target_cm = (int)std::lround(cm * MIDDLE_WINCH_RATIO_K);

    if (relay.controlRelay(ch_left, true))
        return "ERR relay_left_on_fail\n";
    if (relay.controlRelay(ch_right, true)) {
        relay.controlRelay(ch_left, false);
        return "ERR relay_right_on_fail\n";
    }
    if (middleStart(!is_retract)) {
        allMotionOff();
        return "ERR inverter_start_fail\n";
    }

    const auto start = std::chrono::steady_clock::now();
    bool left_done = false, right_done = false, middle_done = false;
    std::string abort_reason;

    while (!(left_done && right_done && middle_done)) {
        if (abort_flag.load()) { abort_reason = "aborted"; break; }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > MOTION_TIMEOUT_MS) { abort_reason = "timeout"; break; }

        int32_t cur = 0;
        if (!left_done && !meter_left.readUpperInteger(cur) &&
            std::abs(cur - base_left) >= cm) {
            relay.controlRelay(ch_left, false);
            left_done = true;
        }
        if (!right_done && !meter_right.readUpperInteger(cur) &&
            std::abs(cur - base_right) >= cm) {
            relay.controlRelay(ch_right, false);
            right_done = true;
        }
        if (!middle_done && !meter_middle.readUpperInteger(cur) &&
            std::abs(cur - base_middle) >= middle_target_cm) {
            inverter.stopDecel();
            middle_done = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }

    allMotionOff();

    if (!abort_reason.empty()) return "ERR " + abort_reason + "\n";
    return "OK\n";
}

// ============ roll_correct: differential (middle winch idle) ============

static std::string cmd_roll_correct(int delta_cm) {
    // +delta = 左放右收 |delta| cm;  -delta = 左收右放
    if (delta_cm == 0) return "OK\n";

    std::lock_guard<std::mutex> lock(motion_mtx);
    MotionScope ms;
    abort_flag = false;

    const int  abs_cm   = std::abs(delta_cm);
    const bool left_pay = (delta_cm > 0);

    int32_t base_left = 0, base_right = 0;
    if (meter_left.readUpperInteger(base_left))   return "ERR meter_left_read_fail\n";
    if (meter_right.readUpperInteger(base_right)) return "ERR meter_right_read_fail\n";

    const int ch_left  = left_pay ? CH_PAY_OUT_LEFT  : CH_RETRACT_LEFT;
    const int ch_right = left_pay ? CH_RETRACT_RIGHT : CH_PAY_OUT_RIGHT;

    if (relay.controlRelay(ch_left, true))
        return "ERR relay_left_on_fail\n";
    if (relay.controlRelay(ch_right, true)) {
        relay.controlRelay(ch_left, false);
        return "ERR relay_right_on_fail\n";
    }

    const auto start = std::chrono::steady_clock::now();
    bool left_done = false, right_done = false;
    std::string abort_reason;

    while (!(left_done && right_done)) {
        if (abort_flag.load()) { abort_reason = "aborted"; break; }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > MOTION_TIMEOUT_MS) { abort_reason = "timeout"; break; }

        int32_t cur = 0;
        if (!left_done && !meter_left.readUpperInteger(cur) &&
            std::abs(cur - base_left) >= abs_cm) {
            relay.controlRelay(ch_left, false);
            left_done = true;
        }
        if (!right_done && !meter_right.readUpperInteger(cur) &&
            std::abs(cur - base_right) >= abs_cm) {
            relay.controlRelay(ch_right, false);
            right_done = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }

    allRelayOff();

    if (!abort_reason.empty()) return "ERR " + abort_reason + "\n";
    return "OK\n";
}

// ============ Other handlers ============

static std::string cmd_manual(const std::string& dir, const std::string& onoff) {
    int ch = -1;
    if      (dir == "pay_out_left")  ch = CH_PAY_OUT_LEFT;
    else if (dir == "pay_out_right") ch = CH_PAY_OUT_RIGHT;
    else if (dir == "retract_left")  ch = CH_RETRACT_LEFT;
    else if (dir == "retract_right") ch = CH_RETRACT_RIGHT;
    else return "ERR unknown_channel\n";

    bool on;
    if      (onoff == "on")  on = true;
    else if (onoff == "off") on = false;
    else return "ERR expected_on_or_off\n";

    if (relay.controlRelay(ch, on)) return "ERR relay_fail\n";
    return "OK\n";
}

static std::string cmd_middle_set(int rpm, const std::string& dir) {
    if (dir == "stop") {
        if (inverter.stopDecel()) return "ERR inverter_stop_fail\n";
        return "OK\n";
    }
    if (rpm <= 0) return "ERR invalid_rpm\n";

    // TODO: proper rpm → Hz needs motor pole count. Interim: assume 4-pole
    // 50 Hz → 1500 rpm synchronous. Recalibrate on site.
    const double hz = rpm * 50.0 / 1500.0;

    if (inverter.setFreqHz(hz, CLV900_MAX_HZ)) return "ERR inverter_freq_fail\n";
    if (dir == "pay") {
        if (inverter.runForward()) return "ERR inverter_run_fail\n";
    } else if (dir == "retract") {
        if (inverter.runReverse()) return "ERR inverter_run_fail\n";
    } else {
        return "ERR expected_pay_retract_stop\n";
    }
    return "OK\n";
}

static std::string cmd_zero_meters(const std::string& mode) {
    if (mode != "ground" && mode != "top") return "ERR expected_ground_or_top\n";

    if (mode == "top") {
        // Store |left SD76| before reset: rope length from top to ground.
        // Left used as reference (left/right symmetric; middle uses K ratio).
        int32_t left_cur = 0;
        if (meter_left.readUpperInteger(left_cur)) return "ERR meter_left_read_fail\n";
        home_ground_cm.store(std::abs(left_cur));
    }

    if (meter_left.resetAll())   return "ERR meter_left_reset_fail\n";
    if (meter_right.resetAll())  return "ERR meter_right_reset_fail\n";
    if (meter_middle.resetAll()) return "ERR meter_middle_reset_fail\n";

    std::ostringstream oss;
    oss << "OK";
    if (mode == "top") oss << " home_ground_cm=" << home_ground_cm.load();
    oss << "\n";
    return oss.str();
}

static std::string cmd_home_status() {
    int32_t l = 0, r = 0, m = 0;
    const bool lok = !meter_left.readUpperInteger(l);
    const bool rok = !meter_right.readUpperInteger(r);
    const bool mok = !meter_middle.readUpperInteger(m);
    const int32_t home = home_ground_cm.load();
    const int32_t remaining = home - (lok ? std::abs(l) : 0);

    std::ostringstream oss;
    oss << "OK home_ground_cm=" << home;
    oss << " left="      << (lok ? std::to_string(l) : std::string("ERR"));
    oss << " right="     << (rok ? std::to_string(r) : std::string("ERR"));
    oss << " middle="    << (mok ? std::to_string(m) : std::string("ERR"));
    oss << " remaining=" << remaining;
    oss << "\n";
    return oss.str();
}

static std::string cmd_status() {
    int32_t l = 0, r = 0, m = 0;
    const bool lok = !meter_left.readUpperInteger(l);
    const bool rok = !meter_right.readUpperInteger(r);
    const bool mok = !meter_middle.readUpperInteger(m);

    std::ostringstream oss;
    oss << "OK";
    oss << " length_left="    << (lok ? std::to_string(l) : std::string("ERR"));
    oss << " length_right="   << (rok ? std::to_string(r) : std::string("ERR"));
    oss << " length_middle="  << (mok ? std::to_string(m) : std::string("ERR"));
    oss << " home_ground_cm=" << home_ground_cm.load();
    oss << "\n";
    return oss.str();
}

static std::string cmd_stop() {
    abort_flag = true;
    allMotionOff();
    return "OK\n";
}

static std::string cmd_ping() {
    return "OK pong\n";
}

// ============ Dispatcher ============

static std::string dispatch(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "pay_out") {
        int cm = 0; iss >> cm;
        if (iss.fail()) return "ERR usage:pay_out_<cm>\n";
        return motion_rope(cm, false);
    }
    if (cmd == "retract") {
        int cm = 0; iss >> cm;
        if (iss.fail()) return "ERR usage:retract_<cm>\n";
        return motion_rope(cm, true);
    }
    if (cmd == "pay_out_left" || cmd == "pay_out_right" ||
        cmd == "retract_left" || cmd == "retract_right") {
        std::string onoff; iss >> onoff;
        if (iss.fail()) return "ERR usage:<cmd>_<on|off>\n";
        return cmd_manual(cmd, onoff);
    }
    if (cmd == "middle_set") {
        int rpm = 0; std::string dir;
        iss >> rpm >> dir;
        if (dir.empty()) return "ERR usage:middle_set_<rpm>_<pay|retract|stop>\n";
        return cmd_middle_set(rpm, dir);
    }
    if (cmd == "zero_meters") {
        std::string mode; iss >> mode;
        if (mode.empty()) return "ERR usage:zero_meters_<ground|top>\n";
        return cmd_zero_meters(mode);
    }
    if (cmd == "home_status") return cmd_home_status();
    if (cmd == "roll_correct") {
        int delta_cm = 0; iss >> delta_cm;
        if (iss.fail()) return "ERR usage:roll_correct_<delta_cm>\n";
        return cmd_roll_correct(delta_cm);
    }
    if (cmd == "status") return cmd_status();
    if (cmd == "stop")   return cmd_stop();
    if (cmd == "ping")   return cmd_ping();
    return "ERR unknown_cmd\n";
}

// ============ TCP receive callback (line-buffered per client thread) ============

static void on_receive(socket_t sock, const char* data, int len) {
    touch_heartbeat();

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

// ============ Main ============

int main() {
#ifndef _WIN32
    // Ignore SIGPIPE so send() on a dead peer returns -1/EPIPE instead of killing the process.
    signal(SIGPIPE, SIG_IGN);
#endif

    std::cout << "[Crane_control_PI] starting..." << std::endl;

    if (!cli_30.connectToServer(USR_IP, USR_PORT)) {
        std::cerr << "[FATAL] connect USR " << USR_IP << ":" << USR_PORT << " failed" << std::endl;
        return 1;
    }
    std::cout << "[OK] USR @ " << USR_IP << ":" << USR_PORT << std::endl;

    if (relay.init(cli_30, RELAY_SLAVE, 8, false)) {
        std::cerr << "[FATAL] ZS_DIO_R_RLY init (slave " << RELAY_SLAVE << ") failed" << std::endl;
        return 1;
    }
    std::cout << "[OK] ZS_DIO_R_RLY   slave " << RELAY_SLAVE << std::endl;

    if (meter_left.init(cli_30, METER_LEFT_SLAVE, false)) {
        std::cerr << "[FATAL] SD76 left (slave " << METER_LEFT_SLAVE << ") init failed" << std::endl;
        return 1;
    }
    std::cout << "[OK] SD76 left      slave " << METER_LEFT_SLAVE << std::endl;

    if (meter_right.init(cli_30, METER_RIGHT_SLAVE, false)) {
        std::cerr << "[FATAL] SD76 right (slave " << METER_RIGHT_SLAVE << ") init failed" << std::endl;
        return 1;
    }
    std::cout << "[OK] SD76 right     slave " << METER_RIGHT_SLAVE << std::endl;

    if (meter_middle.init(cli_30, METER_MIDDLE_SLAVE, false)) {
        std::cerr << "[FATAL] SD76 middle (slave " << METER_MIDDLE_SLAVE << ") init failed" << std::endl;
        return 1;
    }
    std::cout << "[OK] SD76 middle    slave " << METER_MIDDLE_SLAVE << std::endl;

    if (inverter.init(cli_30, INVERTER_SLAVE, false)) {
        std::cerr << "[FATAL] CLV900 (slave " << INVERTER_SLAVE << ") init failed" << std::endl;
        return 1;
    }
    std::cout << "[OK] CLV900         slave " << INVERTER_SLAVE << std::endl;

    // Safe startup state
    allMotionOff();

    cmd_server.setReceiveCallback(on_receive);
    if (!cmd_server.start(CMD_PORT, false)) {
        std::cerr << "[FATAL] TCP server start on port " << CMD_PORT << " failed" << std::endl;
        return 1;
    }
    std::cout << "[OK] command server :" << CMD_PORT << " (type 'exit' to stop)" << std::endl;

    watchdog_thread = std::thread(watchdog_loop);
    std::cout << "[OK] watchdog thread (timeout " << WATCHDOG_TIMEOUT_MS << " ms)" << std::endl;

    // Local console
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "exit" || line == "quit") break;
        if (line == "status") std::cout << cmd_status();
    }

    std::cout << "[SHUTDOWN] stopping..." << std::endl;
    abort_flag = true;
    watchdog_stop = true;
    if (watchdog_thread.joinable()) watchdog_thread.join();
    cmd_server.stop();
    allMotionOff();
    return 0;
}
