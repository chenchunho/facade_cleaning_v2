// ============================================================================
// Crane_control_PI — Rooftop crane controller
//
// Hardware (see CLAUDE.md / motion_flow.md):
//   RPi @ 192.168.1.101
//   USR-TCP232-304 @ 192.168.1.30 : 4001
//     ZS_DIO_R_RLY  slave 1 (8CH)
//       CH1 = retract_left   (左收繩)
//       CH2 = retract_right  (右收繩)
//       CH3 = pay_out_left   (左放繩)
//       CH4 = pay_out_right  (右放繩)
//     SD76 slave 2 : 左繩計米器
//     SD76 slave 3 : 右繩計米器
//
// Command TCP server @ :5002 (line-based text protocol, multi-client)
//   pay_out <cm>        : both ropes pay out N cm (SD76 feedback)
//   retract <cm>        : both ropes retract N cm
//   pay_out_left  <on|off>
//   pay_out_right <on|off>
//   retract_left  <on|off>
//   retract_right <on|off>
//   stop                : abort any motion, turn off all channels
//   status              : reply with current lengths
//
// Reply format:
//   OK [data]\n  / ERR <reason>\n  / EVT <type> <data>\n
// ============================================================================

#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdlib>

#include "TCP_client.h"
#include "TCP_server.h"
#include "ZS_DIO_R_RLY.h"
#include "SD76_length_meters.h"

// ============ Hardware config ============

static constexpr const char* USR_IP   = "192.168.1.30";
static constexpr int         USR_PORT = 4001;
static constexpr int         CMD_PORT = 5002;

static constexpr int RELAY_SLAVE       = 1;
static constexpr int METER_LEFT_SLAVE  = 2;
static constexpr int METER_RIGHT_SLAVE = 3;

// Relay channel mapping (ZS_DIO CH1~4 per new hardware spec)
static constexpr int CH_RETRACT_LEFT  = 1;
static constexpr int CH_RETRACT_RIGHT = 2;
static constexpr int CH_PAY_OUT_LEFT  = 3;
static constexpr int CH_PAY_OUT_RIGHT = 4;

// Safety
static constexpr int MOTION_TIMEOUT_MS = 120000;  // 2 min hard cap per motion
static constexpr int POLL_INTERVAL_MS  = 50;

// ============ Globals ============

static TCP_client           cli_30;
static ZS_DIO_R_RLY         relay;
static SD76_length_meters   meter_left;
static SD76_length_meters   meter_right;
static TCP_server           cmd_server;

static std::atomic<bool> abort_flag(false);
static std::mutex        motion_mtx;

// ============ Low-level helpers ============

static void allRopeOff() {
    relay.controlRelay(CH_RETRACT_LEFT,  false);
    relay.controlRelay(CH_RETRACT_RIGHT, false);
    relay.controlRelay(CH_PAY_OUT_LEFT,  false);
    relay.controlRelay(CH_PAY_OUT_RIGHT, false);
}

// ============ Command handlers ============

static std::string cmd_pay_out(int cm, bool is_retract) {
    if (cm <= 0) return "ERR invalid_cm\n";

    std::lock_guard<std::mutex> lock(motion_mtx);
    abort_flag = false;

    int32_t base_left = 0, base_right = 0;
    if (meter_left.readUpperInteger(base_left))   return "ERR meter_left_read_fail\n";
    if (meter_right.readUpperInteger(base_right)) return "ERR meter_right_read_fail\n";

    const int ch_left  = is_retract ? CH_RETRACT_LEFT  : CH_PAY_OUT_LEFT;
    const int ch_right = is_retract ? CH_RETRACT_RIGHT : CH_PAY_OUT_RIGHT;

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
        if (!left_done && !meter_left.readUpperInteger(cur)) {
            if (std::abs(cur - base_left) >= cm) {
                relay.controlRelay(ch_left, false);
                left_done = true;
            }
        }
        if (!right_done && !meter_right.readUpperInteger(cur)) {
            if (std::abs(cur - base_right) >= cm) {
                relay.controlRelay(ch_right, false);
                right_done = true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }

    // Safety off both channels regardless of exit path
    relay.controlRelay(ch_left,  false);
    relay.controlRelay(ch_right, false);

    if (!abort_reason.empty()) return "ERR " + abort_reason + "\n";
    return "OK\n";
}

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

static std::string cmd_status() {
    int32_t l = 0, r = 0;
    const bool lok = !meter_left.readUpperInteger(l);
    const bool rok = !meter_right.readUpperInteger(r);

    std::ostringstream oss;
    oss << "OK";
    oss << " length_left="  << (lok ? std::to_string(l) : std::string("ERR"));
    oss << " length_right=" << (rok ? std::to_string(r) : std::string("ERR"));
    oss << "\n";
    return oss.str();
}

static std::string cmd_stop() {
    abort_flag = true;
    allRopeOff();
    return "OK\n";
}

// ============ Dispatcher ============

static std::string dispatch(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "pay_out") {
        int cm = 0; iss >> cm;
        if (iss.fail()) return "ERR usage:pay_out_<cm>\n";
        return cmd_pay_out(cm, false);
    }
    if (cmd == "retract") {
        int cm = 0; iss >> cm;
        if (iss.fail()) return "ERR usage:retract_<cm>\n";
        return cmd_pay_out(cm, true);
    }
    if (cmd == "pay_out_left" || cmd == "pay_out_right" ||
        cmd == "retract_left" || cmd == "retract_right") {
        std::string onoff; iss >> onoff;
        if (iss.fail()) return "ERR usage:<cmd>_<on|off>\n";
        return cmd_manual(cmd, onoff);
    }
    if (cmd == "status") return cmd_status();
    if (cmd == "stop")   return cmd_stop();
    return "ERR unknown_cmd\n";
}

// ============ TCP receive callback (line-buffered per client thread) ============

static void on_receive(socket_t sock, const char* data, int len) {
    // thread_local: each client handler thread has its own accumulator;
    // cleaned up automatically when the thread exits (on disconnect).
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
    std::cout << "[OK] ZS_DIO_R_RLY slave " << RELAY_SLAVE << std::endl;

    if (meter_left.init(cli_30, METER_LEFT_SLAVE, false)) {
        std::cerr << "[FATAL] SD76 left (slave " << METER_LEFT_SLAVE << ") init failed" << std::endl;
        return 1;
    }
    std::cout << "[OK] SD76 left  slave " << METER_LEFT_SLAVE << std::endl;

    if (meter_right.init(cli_30, METER_RIGHT_SLAVE, false)) {
        std::cerr << "[FATAL] SD76 right (slave " << METER_RIGHT_SLAVE << ") init failed" << std::endl;
        return 1;
    }
    std::cout << "[OK] SD76 right slave " << METER_RIGHT_SLAVE << std::endl;

    // Safe startup state
    allRopeOff();

    cmd_server.setReceiveCallback(on_receive);
    if (!cmd_server.start(CMD_PORT, false)) {
        std::cerr << "[FATAL] TCP server start on port " << CMD_PORT << " failed" << std::endl;
        return 1;
    }
    std::cout << "[OK] command server :" << CMD_PORT << " (type 'exit' to stop)" << std::endl;

    // Local console for operator to kill the daemon
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "exit" || line == "quit") break;
        if (line == "status") std::cout << cmd_status();
    }

    std::cout << "[SHUTDOWN] stopping..." << std::endl;
    abort_flag = true;
    cmd_server.stop();
    allRopeOff();
    return 0;
}
