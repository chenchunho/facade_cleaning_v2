// ============================================================================
// washrobot_new_PI — TCP command server + dispatch
//
// All hardware, motion logic, and background threads live in WashRobot (WASH_ROBOT.h/.cpp).
// This file owns only the TCP server and routes incoming commands to robot.cmd_*().
//
// Command server @ :5001 (line-based, multi-client):
//   init / attach / detach / step_down / run <n>
//   pause / resume / emergency_stop / reset / ping
//   vacuum <group> <on|off>   pusher <group> <extend|retract>
//   move <motor> <cm>         arm_sweep / tilt_mode <on|off>
//   confirm_balance <yes|no>  return_home <descent_cm>
//   status / shutdown
//
// Reply format: OK [data]\n  /  ERR <reason>\n  /  EVT <type> <data>\n
// ============================================================================

#include <iostream>
#include <string>
#include <sstream>

#include "TCP_server.h"
#include "WASH_ROBOT.h"

static constexpr int CMD_PORT = 5001;

static WashRobot   robot;
static TCP_server  cmd_server;

// ============ Dispatcher ============

static std::string dispatch(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd; iss >> cmd;

    if (cmd == "init")           return robot.cmd_init();
    if (cmd == "attach")         return robot.cmd_attach();
    if (cmd == "detach")         return robot.cmd_detach();
    if (cmd == "step_down")      return robot.cmd_step_down();
    if (cmd == "arm_sweep")      return robot.cmd_arm_sweep();
    if (cmd == "shutdown")       return robot.cmd_shutdown();
    if (cmd == "status")         return robot.cmd_status();
    if (cmd == "emergency_stop") return robot.cmd_emergency_stop();
    if (cmd == "reset")          return robot.cmd_reset();
    if (cmd == "ping")           return robot.cmd_ping();
    if (cmd == "pause")          return robot.cmd_pause();
    if (cmd == "resume")         return robot.cmd_resume();

    if (cmd == "run") {
        int n = 0; iss >> n;
        if (iss.fail()) return "ERR usage:run_<steps>\n";
        return robot.cmd_run(n);
    }
    if (cmd == "vacuum") {
        std::string g, s; iss >> g >> s;
        if (iss.fail()) return "ERR usage:vacuum_<group>_<on|off>\n";
        bool on;
        if      (s == "on")  on = true;
        else if (s == "off") on = false;
        else return "ERR expected_on_or_off\n";
        return robot.cmd_vacuum(g, on);
    }
    if (cmd == "pusher") {
        std::string g, p; iss >> g >> p;
        if (iss.fail()) return "ERR usage:pusher_<group>_<extend|retract>\n";
        return robot.cmd_pusher(g, p);
    }
    if (cmd == "move") {
        std::string m; double cm = 0;
        iss >> m >> cm;
        if (iss.fail()) return "ERR usage:move_<motor>_<cm>\n";
        return robot.cmd_move(m, cm);
    }
    if (cmd == "tilt_mode") {
        std::string s; iss >> s;
        if (iss.fail()) return "ERR usage:tilt_mode_<on|off>\n";
        if (s == "on")  return robot.cmd_tilt_mode(true);
        if (s == "off") return robot.cmd_tilt_mode(false);
        return "ERR expected_on_or_off\n";
    }
    if (cmd == "confirm_balance") {
        std::string ans; iss >> ans;
        if (iss.fail()) return "ERR usage:confirm_balance_<yes|no>\n";
        return robot.cmd_confirm_balance(ans);
    }
    if (cmd == "return_home") {
        int cm = 0; iss >> cm;
        if (iss.fail() || cm <= 0) return "ERR usage:return_home_<descent_cm>\n";
        return robot.cmd_return_home(cm);
    }

    return "ERR unknown_cmd\n";
}

// ============ TCP receive callback ============

static void on_receive(socket_t sock, const char* data, int len) {
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
    std::cout << "[washrobot_new_PI] starting...\n";

    // Wire EVT broadcast before calling init (background threads may fire events during init)
    robot.evt_cb = [](const std::string& line) {
        cmd_server.broadcast(line.c_str(), (int)line.size());
    };

    if (robot.init()) {
        std::cerr << "[FATAL] WashRobot init failed\n";
        return 1;
    }

    // Command TCP server
    cmd_server.setReceiveCallback(on_receive);
    if (!cmd_server.start(CMD_PORT, false)) {
        std::cerr << "[FATAL] TCP server :" << CMD_PORT << " fail\n";
        return 1;
    }
    std::cout << "[OK] command server :" << CMD_PORT << " (type 'exit' to stop)\n";

    // Local console (status / exit)
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "exit" || line == "quit") break;
        if (line == "status") std::cout << robot.cmd_status();
    }

    std::cout << "[SHUTDOWN] stopping...\n";
    robot.cmd_shutdown();
    robot.stop();
    cmd_server.stop();
    return 0;
}
