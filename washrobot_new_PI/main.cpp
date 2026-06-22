// ============================================================================
// washrobot_new_PI — TCP command server + dispatch
//
// All hardware, motion logic, and background threads live in WashRobot (WASH_ROBOT.h/.cpp).
// This file owns only the TCP server and routes incoming commands to robot.cmd_*().
//
// Command server @ :5001 (line-based, multi-client):
//   init / attach / detach / step_down [cm] / step_up [cm] / run <n> [cm] [down|up]
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
    if (cmd == "step_down") {
        int cm = 0; iss >> cm;          // optional; 0 = use current step_cm_
        return robot.cmd_step_down(cm);
    }
    if (cmd == "step_down_with_sweep") {
        int cm = 0; iss >> cm;
        return robot.cmd_step_down_with_sweep(cm);
    }
    if (cmd == "step_up") {
        int cm = 0; iss >> cm;          // optional; 0 = use current step_cm_
        return robot.cmd_step_up(cm);
    }
    if (cmd == "step_up_with_sweep") {
        int cm = 0; iss >> cm;
        return robot.cmd_step_up_with_sweep(cm);
    }
    if (cmd == "step_up_sweep_after_feet") {
        int cm = 0; iss >> cm;
        return robot.cmd_step_up_sweep_after_feet(cm);
    }
    if (cmd == "step_down_sweep_after_feet") {
        int cm = 0; iss >> cm;
        return robot.cmd_step_down_sweep_after_feet(cm);
    }
    if (cmd == "step_up_sweep_ba") {
        int cm = 0; iss >> cm;
        return robot.cmd_step_up_sweep_before_after(cm);
    }
    if (cmd == "step_down_sweep_ba") {
        int cm = 0; iss >> cm;
        return robot.cmd_step_down_sweep_before_after(cm);
    }
    if (cmd == "arm_sweep")      return robot.cmd_arm_sweep();
    if (cmd == "shutdown")       return robot.cmd_shutdown();
    if (cmd == "status")         return robot.cmd_status();
    if (cmd == "emergency_stop") return robot.cmd_emergency_stop();
    if (cmd == "reset")          return robot.cmd_reset();
    if (cmd == "recover")        return robot.cmd_recover();
    if (cmd == "realign")        return robot.cmd_realign();
    if (cmd == "ping")           return robot.cmd_ping();
    if (cmd == "pause")          return robot.cmd_pause();
    if (cmd == "resume")         return robot.cmd_resume();
    if (cmd == "continue")       return robot.cmd_continue();
    if (cmd == "skip")           return robot.cmd_skip();

    if (cmd == "crane_attached") {
        std::string s; iss >> s;
        if (iss.fail()) return "ERR usage:crane_attached_<on|off>\n";
        if (s == "on")  return robot.cmd_crane_attached(true);
        if (s == "off") return robot.cmd_crane_attached(false);
        return "ERR expected_on_or_off\n";
    }
    if (cmd == "wheels_attached") {
        std::string s; iss >> s;
        if (iss.fail()) return "ERR usage:wheels_attached_<on|off>\n";
        if (s == "on")  return robot.cmd_wheels_attached(true);
        if (s == "off") return robot.cmd_wheels_attached(false);
        return "ERR expected_on_or_off\n";
    }

    // ---- cleaning arm (damiao motors via motor_api on 127.0.0.1:9527) ----
    if (cmd == "arm_init")   return robot.cmd_arm_init();
    if (cmd == "arm_park")   return robot.cmd_arm_park();
    if (cmd == "arm_status") return robot.cmd_arm_status();
    if (cmd == "arm_deploy") {
        int wall_mm = 0;
        std::string slot;
        iss >> wall_mm >> slot;
        if (iss.fail()) return "ERR usage:arm_deploy_<wall_mm>_<LEFT|CENTER|RIGHT>\n";
        return robot.cmd_arm_deploy(wall_mm, slot);
    }
    if (cmd == "arm_clean_sweep") {
        int wall_mm = 0;
        int rounds = 1;
        iss >> wall_mm >> rounds;
        if (iss.fail() || wall_mm <= 0) return "ERR usage:arm_clean_sweep_<wall_mm>_<rounds>\n";
        return robot.cmd_arm_clean_sweep(wall_mm, rounds);
    }
    if (cmd == "arm_attached") {
        std::string s; iss >> s;
        if (iss.fail()) return "ERR usage:arm_attached_<on|off>\n";
        if (s == "on")  return robot.cmd_arm_attached(true);
        if (s == "off") return robot.cmd_arm_attached(false);
        return "ERR expected_on_or_off\n";
    }

    // [2026-06-01] Camera obstacle detection toggle (default OFF, testing-only)
    if (cmd == "obstacle_detect") {
        std::string s; iss >> s;
        if (iss.fail()) return "ERR usage:obstacle_detect_<on|off>\n";
        if (s == "on")  return robot.cmd_obstacle_detect(true);
        if (s == "off") return robot.cmd_obstacle_detect(false);
        return "ERR expected_on_or_off\n";
    }
    // [2026-06-04] Single-shot obstacle detector test (Step 1).
    // Reads pre-captured /tmp/cam{3,4}_{before,after}.jpg, runs detector,
    // returns combined decision.
    if (cmd == "obstacle_check") return robot.cmd_obstacle_check();
    // [2026-06-04] RUN with obstacle avoidance (Step 2).
    // Loop: probe → detect → ask user → step_down with confirmed step.
    if (cmd == "run_avoid") return robot.cmd_run_avoid();
    if (cmd == "obstacle_response") {
        int v; iss >> v;
        if (iss.fail()) return "ERR usage:obstacle_response_<0|1>\n";
        return robot.cmd_obstacle_response(v);
    }

    // [2026-06-02] Balance calibration (3 cmd: start/record/abort + status)
    if (cmd == "balance_calibrate_start")  return robot.cmd_balance_calibrate_start();
    if (cmd == "balance_calibrate_record") return robot.cmd_balance_calibrate_record();
    if (cmd == "balance_calibrate_abort")  return robot.cmd_balance_calibrate_abort();
    if (cmd == "balance_calibrate_status") return robot.cmd_balance_calibrate_status();

    // [2026-05-29] Runtime settings (wall-tune) — see WashRobot::Settings struct.
    if (cmd == "get_settings") {
        return robot.cmd_get_settings();
    }
    if (cmd == "set_setting") {
        std::string key, value;
        iss >> key >> value;
        if (iss.fail() || key.empty() || value.empty())
            return "ERR usage:set_setting_<key>_<value>\n";
        return robot.cmd_set_setting(key, value);
    }
    if (cmd == "save_settings") {
        return robot.cmd_save_settings();
    }

    if (cmd == "run") {
        int n = 0, cm = 0;
        std::string direction = "down";
        iss >> n;
        if (iss.fail()) return "ERR usage:run_<steps>_[cm]_[down|up]\n";
        iss >> cm;                      // optional 2nd arg (default 0 = use step_cm_)
        iss >> direction;               // optional 3rd arg (default "down")
        return robot.cmd_run(n, cm, direction);
    }
    // [2026-06-05] Scripted run — CSV of per-step cm. Fixed down_sweep_af.
    // CSV grammar: <int> | <int>*<count>, comma-separated. e.g. "30*5,20*3".
    // CSV may contain spaces (parse_script_csv_ strips them per-token); read
    // rest-of-line so whitespace inside CSV doesn't get eaten by `iss >>`.
    if (cmd == "run_script") {
        std::string csv;
        std::getline(iss, csv);
        // Trim leading space left by `iss >> cmd`.
        size_t p = csv.find_first_not_of(" \t");
        if (p == std::string::npos) return "ERR usage:run_script_<csv>\n";
        csv = csv.substr(p);
        return robot.cmd_run_script(csv);
    }
    if (cmd == "save_script") {
        std::string name;
        iss >> name;
        if (iss.fail() || name.empty()) return "ERR usage:save_script_<name>_<csv>\n";
        std::string csv;
        std::getline(iss, csv);
        size_t p = csv.find_first_not_of(" \t");
        if (p == std::string::npos) return "ERR usage:save_script_<name>_<csv>\n";
        csv = csv.substr(p);
        return robot.cmd_save_script(name, csv);
    }
    if (cmd == "list_scripts") return robot.cmd_list_scripts();
    if (cmd == "load_script") {
        std::string name; iss >> name;
        if (iss.fail() || name.empty()) return "ERR usage:load_script_<name>\n";
        return robot.cmd_load_script(name);
    }
    if (cmd == "delete_script") {
        std::string name; iss >> name;
        if (iss.fail() || name.empty()) return "ERR usage:delete_script_<name>\n";
        return robot.cmd_delete_script(name);
    }
    if (cmd == "run_saved") {
        std::string name; iss >> name;
        if (iss.fail() || name.empty()) return "ERR usage:run_saved_<name>\n";
        return robot.cmd_run_saved(name);
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
    if (cmd == "pump") {
        std::string s; iss >> s;
        if (iss.fail()) return "ERR usage:pump_<on|off>\n";
        if (s == "on")  return robot.cmd_pump(true);
        if (s == "off") return robot.cmd_pump(false);
        return "ERR expected_on_or_off\n";
    }
    if (cmd == "brush") {
        std::string s; iss >> s;
        if (iss.fail()) return "ERR usage:brush_<on|off>\n";
        if (s == "on")  return robot.cmd_brush(true);
        if (s == "off") return robot.cmd_brush(false);
        return "ERR expected_on_or_off\n";
    }
    if (cmd == "water_pump") {
        std::string s; iss >> s;
        if (iss.fail()) return "ERR usage:water_pump_<on|off>\n";
        if (s == "on")  return robot.cmd_water_pump(true);
        if (s == "off") return robot.cmd_water_pump(false);
        return "ERR expected_on_or_off\n";
    }
    if (cmd == "water_inlet") {
        std::string s; iss >> s;
        if (iss.fail()) return "ERR usage:water_inlet_<on|off>\n";
        if (s == "on")  return robot.cmd_water_inlet(true);
        if (s == "off") return robot.cmd_water_inlet(false);
        return "ERR expected_on_or_off\n";
    }
    if (cmd == "water_level")    return robot.cmd_water_level();
    if (cmd == "pusher") {
        std::string g, p; iss >> g >> p;
        if (iss.fail()) return "ERR usage:pusher_<group>_<extend|retract>\n";
        return robot.cmd_pusher(g, p);
    }
    if (cmd == "zdt_pusher") {
        int s = 0; std::string a;
        iss >> s >> a;
        if (iss.fail() || s < 1 || s > 9) return "ERR usage:zdt_pusher_<1..9>_<extend|retract>\n";
        return robot.cmd_zdt_pusher(s, a);
    }
    if (cmd == "zdt_zero") {
        std::string g; iss >> g;
        if (iss.fail()) return "ERR usage:zdt_zero_<feet|body|center|all>\n";
        return robot.cmd_zdt_zero(g);
    }
    if (cmd == "move") {
        std::string m; double cm = 0;
        iss >> m >> cm;
        if (iss.fail()) return "ERR usage:move_<motor>_<cm>\n";
        return robot.cmd_move(m, cm);
    }
    if (cmd == "wheels") {
        std::string a; iss >> a;
        if (iss.fail()) return "ERR usage:wheels_<retract|lower>\n";
        return robot.cmd_wheels(a);
    }
    if (cmd == "dm2j_group") {
        std::string g; double cm = 0;
        iss >> g >> cm;
        if (iss.fail()) return "ERR usage:dm2j_group_<feet|wheels>_<cm>\n";
        return robot.cmd_dm2j_group(g, cm);
    }
    if (cmd == "zdt_disable") {
        int s = 0; iss >> s;
        if (iss.fail() || s < 1 || s > 9) return "ERR usage:zdt_disable_<1..9>\n";
        return robot.cmd_zdt_disable(s);
    }
    if (cmd == "zdt_enable") {
        int s = 0; iss >> s;
        if (iss.fail() || s < 1 || s > 9) return "ERR usage:zdt_enable_<1..9>\n";
        return robot.cmd_zdt_enable(s);
    }
    if (cmd == "zdt_release_stall") return robot.cmd_zdt_release_stall();
    if (cmd == "dm2j_zero") {
        std::string g; iss >> g;
        if (iss.fail()) return "ERR usage:dm2j_zero_<feet|wheels|arm>\n";
        return robot.cmd_dm2j_zero(g);
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
//
// Commands split into two categories to avoid the receive thread getting stuck
// when a long-running motion command (step_down / run / etc.) blocks on
// await_user_intervention_ — that previously starved the same TCP connection
// of its ability to deliver continue/skip/emergency_stop, deadlocking the GUI.
//
//   FAST: state-mutation / atomic / quick I/O — run synchronously on receive
//         thread. Reply goes back immediately on the same socket call.
//   SLOW: anything that may take motion_mtx_ or block on user intervention —
//         spawn detached thread to run dispatch + sendToClient. Receive thread
//         returns immediately → next FAST command (continue, skip, stop) can
//         interrupt or unblock the in-flight slow op.
//
// SLOW ops naturally serialize on motion_mtx_ inside WashRobot, so spawning
// per-call is safe (no out-of-order race). sendToClient is just send(2) —
// POSIX guarantees thread-safety for the same fd.

static const std::unordered_set<std::string> FAST_CMDS = {
    "ping", "status", "pause", "resume",
    "continue", "skip", "emergency_stop", "reset",
    "zdt_release_stall"
};

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

        if (FAST_CMDS.count(verb)) {
            // Fast path: synchronous on receive thread
            const std::string reply = dispatch(line);
            cmd_server.sendToClient(sock, reply.c_str(), (int)reply.size());
        } else {
            // Slow path: detach to worker so receive thread stays responsive
            std::thread([sock, line]() {
                const std::string reply = dispatch(line);
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

    std::cout << "HI~ [washrobot_new_PI] starting...\n";

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
