// ============================================================================
// facade_cleaning_v2 — TCP command server + dispatch
//
// All hardware, motion logic, and background threads live in WashRobot (WASH_ROBOT.h/.cpp).
// This file owns only the TCP server and routes incoming commands to robot.cmd_*().
//
// Command server @ :5001 (line-based, multi-client):
//   init / attach / detach / step_down [cm] / step_up [cm]
//   run <n> [cm] [down|up] [alt|sync]   (2026-07-23: 4th arg 選走法，預設 alt=交替，sync=同步)
//   run_script [up|down] [alt|sync] <csv>   / run_saved <name> [up|down] [alt|sync]
//     (2026-07-23: gait 同 run；cross 步驟一律走 do_cross_obstacle_，不受 gait 影響)
//   step_down_sync [cm] / step_up_sync [cm]   (2026-07-22: 4 顆吸盤同時放開/放繩/重伸，非交替步伐)
//   pause / resume / continue / skip / emergency_stop / reset / recover / realign / ping
//   vacuum <group> <on|off>   pump <on|off>   pusher <group> <extend|retract>
//   zdt_pusher <1..9> <extend|retract>     (single-slave manual, GUI 單支按鈕)
//   zdt_zero <feet|body|center|all>   (set current ZDT pos as zero, manual 3.1.3)
//   zdt_release_stall                 (release stall flags on all 9 slaves; safe during motion)
//   move <motor> <cm>         wheels <retract|lower>
//   dm2j_group <feet|wheels> <cm>     dm2j_zero <feet|wheels|arm>
//   arm_sweep / tilt_mode <on|off>
//   confirm_balance <yes|no>  return_home <descent_cm>
//   status / shutdown
//
// Reply format: OK [data]\n  /  ERR <reason>\n  /  EVT <type> <data>\n
// ============================================================================

#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <cstdlib>   // setenv for CLI-flag → env-var bridging

#ifndef _WIN32
#include <signal.h>
#endif

#include "TCP_server.h"
#include "WASH_ROBOT.h"
#include "dispatcher.h"

static constexpr int CMD_PORT = 5001;

static WashRobot   robot;
static TCP_server  cmd_server;

// ============ Dispatcher ============

static void on_receive(socket_t sock, const char* data, int len) {
    thread_local std::string rx_buf;
    rx_buf.append(data, len);

    size_t pos;
    while ((pos = rx_buf.find('\n')) != std::string::npos) {
        std::string line = rx_buf.substr(0, pos);
        rx_buf.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // First whitespace-separated token = command verb (used for fast/slow lookup)
        std::string verb;
        {
            std::istringstream iss(line);
            iss >> verb;
        }

        if (command::is_fast(verb)) {
            // Fast path: synchronous on receive thread
            const std::string reply = command::dispatch(robot, line);
            cmd_server.sendToClient(sock, reply.c_str(), (int)reply.size());
        } else {
            // Slow path: detach to worker so receive thread stays responsive
            std::thread([sock, line]() {
                const std::string reply = command::dispatch(robot, line);
                cmd_server.sendToClient(sock, reply.c_str(), (int)reply.size());
            }).detach();
        }
    }
}

// ============ Main ============

int main(int argc, char** argv) {
#ifndef _WIN32
    // Ignore SIGPIPE so send() on a dead peer returns -1/EPIPE instead of killing the process.
    // user_lib TCP_client/TCP_server use send(..., 0) without MSG_NOSIGNAL; any peer drop
    // (web backend, crane, RS485 gateway) would otherwise terminate this process.
    signal(SIGPIPE, SIG_IGN);
#endif

    // Parse CLI flags. Convert recognized flags into env vars so WashRobot::init()
    // (which reads env) picks them up uniformly. Program Arguments is the most
    // reliable way to inject config via VS's remote-debug launch vs the flakier
    // "Environment" property field.
    //
    //   --no-debug   → WR_DRIVER_DEBUG=0 (silence driver hex dumps, avoids
    //                  saturating VS's SSH stdout pipe during remote debug)
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-debug") {
#ifndef _WIN32
            setenv("WR_DRIVER_DEBUG", "0", 1);
#else
            _putenv_s("WR_DRIVER_DEBUG", "0");
#endif
            std::cout << "[CLI] --no-debug → WR_DRIVER_DEBUG=0\n";
        } else {
            std::cerr << "[CLI] unknown flag ignored: " << a << "\n";
        }
    }

    std::cout << "HI~ [facade_cleaning_v2] starting...\n";

    // Wire EVT broadcast before calling init (background threads may fire events during init)
    robot.evt_cb = [](const std::string& line) {
        cmd_server.broadcast(line.c_str(), (int)line.size());
    };

    // [2026-05-29] Load runtime wall-tune settings (overrides constexpr defaults).
    // Missing file is silent fallback to defaults — first run before any tune.
    robot.load_settings_at_boot("settings.json");

    // 初始化所有tcp連線和設備
    if (robot.init()) {
        std::cerr << "[FATAL] WashRobot init failed\n";
        return 1;
    }

    // Command TCP server(接收來自web的指令並執行)
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
