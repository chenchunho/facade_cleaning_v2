// 指令層的實作 —— 見 dispatcher.h 的說明。
// [2026-08-30] 由 facade_cleaning_v2/main.cpp 原樣搬出：
//   dispatch() 內容逐字不變，只把「全域 robot」改成參數。
//   FAST_CMDS 一併搬過來，並包成 is_fast() 讓 main.cpp 不必知道它的型別。

#include "dispatcher.h"

#include <sstream>
#include <string>
#include <unordered_set>

#include "WASH_ROBOT.h"

namespace command {

std::string dispatch(WashRobot& robot, const std::string& line) {
    std::istringstream iss(line);
    std::string cmd; iss >> cmd;

    // 🔴🔴 [2026-08-31] 交替步伐 / 跨障礙已停用 —— 硬體沒有分側真空（泵與閥各一顆
    // 繼電器控 4 顆），這些路徑會在 group_seal_ok_ 確認錨定側吸牢之後，關掉**唯一**
    // 那顆閥 → 四顆一起失去真空，而機器正吊在玻璃上。詳見 do_step_down_ 進場守衛。
    //
    // 📌 擋在**分派層**（而不是只靠 do_step_*_ 的守衛）的理由：cmd_step_* 會先
    //    set_state_(State::Running) 才呼叫 do_step_*_，深層守衛回 ERR 會讓機器停在
    //    State::Error，**把「指令被拒絕」偽裝成「動作失敗」**，要 reset 才能繼續，
    //    而且會害人去查根本不存在的硬體故障。這裡擋掉則狀態機完全不被碰。
    //    do_step_*_ / do_cross_obstacle_ 的守衛保留為第二道（涵蓋 run_script 直接呼叫）。
    // ⚠️ 精確比對：*_sync 版本走 do_step_sync_，是安全的，不可以被這裡攔到。
    if (cmd == "step_down" || cmd == "step_up" ||
        cmd == "step_down_with_sweep"      || cmd == "step_up_with_sweep" ||
        cmd == "step_down_sweep_after_feet"|| cmd == "step_up_sweep_after_feet" ||
        cmd == "step_down_sweep_ba"        || cmd == "step_up_sweep_ba" ||
        cmd == "cross_obstacle_down"       || cmd == "cross_obstacle_up") {
        // 上行/下行的建議指令要跟著命令本身走。注意 cross_obstacle_up 的前綴不是
        // "step_up"，用 find("_up") 才涵蓋得到（2026-08-31 第一版就漏了這個）。
        const bool is_up = (cmd.find("_up") != std::string::npos);
        return "ERR alt_gait_disabled_single_valve — use " +
               std::string(is_up ? "step_up_sync" : "step_down_sync") +
               " (see work_log 2026-08-31)\n";
    }

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
    // [2026-07-13 per user] 跨障礙物 — stand legs off wall to 2×preset, cross, realign back.
    if (cmd == "cross_obstacle_down") {
        int cm = 0; iss >> cm;
        return robot.cmd_cross_obstacle_down(cm);
    }
    if (cmd == "cross_obstacle_up") {
        int cm = 0; iss >> cm;
        return robot.cmd_cross_obstacle_up(cm);
    }
    // [2026-07-22 per user] 同步步伐 — 4 顆吸盤同時放開/縮回、吊機兩側同步放繩、
    // IMU 差動微調、4 顆一起重新伸出。純移動，不含清洗。見 WASH_ROBOT.cpp do_step_sync_。
    if (cmd == "step_down_sync") {
        int cm = 0; iss >> cm;
        return robot.cmd_step_down_sync(cm);
    }
    if (cmd == "step_up_sync") {
        int cm = 0; iss >> cm;
        return robot.cmd_step_up_sync(cm);
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
    if (cmd == "realign")        return robot.cmd_realign();   // [v2 2026-07-08] feet-only sealed retract to preset
    // [2026-08-27 per user] 單獨重取 IMU 水平基準（不跑完整 init）。刻意允許在
    // state==Error 時執行——基準沒校好本身就會把系統打進 Error。
    if (cmd == "imu_zero")       return robot.cmd_imu_zero();
    // [2026-08-27 per user] IMU 傾斜保護開關。IMU 立起來後尤拉角卡 gimbal lock、
    // 讀值會亂跳，誤報會把系統打進 Error 擋住所有操作；在改用加速度計算之前，
    // 讓操作者能手動關掉誤報。⚠ 關閉等於沒有傾斜保護。
    if (cmd == "imu_guard") {
        std::string s; iss >> s;
        if (iss.fail()) return "ERR usage:imu_guard_<on|off>\n";
        if (s == "on")  return robot.cmd_imu_guard(true);
        if (s == "off") return robot.cmd_imu_guard(false);
        return "ERR expected_on_or_off\n";
    }
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
    if (cmd == "wheels_attached") return "ERR removed_in_v2\n";   // [v2] DM2J wheels retired

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
    // [2026-08-26 per user] 乾式清洗（bench 測試）— DEPLOY + 滾筒 + 上滑台 + PARK，
    // 不噴水、不移動機器人。內部直接跑同步步伐的清洗段 do_step_sync_rail_sweep_。
    if (cmd == "arm_clean_sweep_dry") return robot.cmd_arm_clean_sweep_dry();
    if (cmd == "arm_attached") {
        std::string s; iss >> s;
        if (iss.fail()) return "ERR usage:arm_attached_<on|off>\n";
        if (s == "on")  return robot.cmd_arm_attached(true);
        if (s == "off") return robot.cmd_arm_attached(false);
        return "ERR expected_on_or_off\n";
    }

    // [v2 2026-07-07] Camera obstacle detection + balance calibration retired.
    if (cmd == "obstacle_detect")          return "ERR removed_in_v2\n";
    if (cmd == "obstacle_check")           return "ERR removed_in_v2\n";
    if (cmd == "run_avoid")                return "ERR removed_in_v2\n";
    if (cmd == "obstacle_response")        return "ERR removed_in_v2\n";
    if (cmd == "balance_calibrate_start")  return "ERR removed_in_v2\n";
    if (cmd == "balance_calibrate_record") return "ERR removed_in_v2\n";
    if (cmd == "balance_calibrate_abort")  return "ERR removed_in_v2\n";
    if (cmd == "balance_calibrate_status") return "ERR removed_in_v2\n";

    // [2026-07-20] D435i depth-camera continuous obstacle-avoid walk (v2 —
    // new commands, distinct from the retired run_avoid/obstacle_response
    // above since the reply semantics differ: no action/step_cm suggestion,
    // pure candidate geometry + user-chosen next step_cm).
    if (cmd == "run_depth_avoid") return robot.cmd_run_depth_avoid();
    if (cmd == "depth_avoid_continue") {
        int cm = 0;
        iss >> cm;
        if (iss.fail()) return "ERR usage:depth_avoid_continue_<cm>\n";
        return robot.cmd_depth_avoid_continue(cm);
    }
    if (cmd == "depth_avoid_stop") return robot.cmd_depth_avoid_stop();

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
        // [2026-08-31] 預設 "alt" → "sync"。alt(交替步伐)已停用：硬體沒有分側真空
        // (泵與閥各一顆繼電器控 4 顆)，交替步伐會在確認錨定側吸牢後關掉唯一那顆閥。
        // 舊預設等於「run 10」這種最自然的下法就會走到危險路徑。
        std::string gait = "sync";      // [2026-07-23] "alt" (交替，已停用) | "sync" (同步)
        iss >> n;
        if (iss.fail()) return "ERR usage:run_<steps>_[cm]_[down|up]_[alt|sync]\n";
        iss >> cm;                      // optional 2nd arg (default 0 = use step_cm_)
        iss >> direction;               // optional 3rd arg (default "down")
        iss >> gait;                    // optional 4th arg (default "alt")
        return robot.cmd_run(n, cm, direction, gait);
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
        if (p == std::string::npos) return "ERR usage:run_script_[up|down]_[alt|sync]_<csv>\n";
        csv = csv.substr(p);
        // [2026-07-14 per user] optional leading direction token (up/down); default
        // down. CSV tokens are numeric so this is unambiguous. Backward-compat:
        // "run_script <csv>" (no direction) still runs down.
        bool up = false;
        if      (csv.rfind("up ", 0)   == 0) { up = true;  csv = csv.substr(3); }
        else if (csv.rfind("down ", 0) == 0) { up = false; csv = csv.substr(5); }
        // [2026-07-23 per user] optional leading gait token (alt/sync), same
        // prefix-strip pattern as direction above.
        // [2026-08-31] 預設由 "alt" 改為 "sync" —— alt 已停用（單閥、無分側真空，
        // 見 do_step_down_ 進場守衛）。原本的「default alt (backward-compat)」等於
        // 讓沒帶 gait token 的舊腳本一律走到危險路徑。
        std::string gait = "sync";
        if      (csv.rfind("alt ", 0)  == 0) { gait = "alt";  csv = csv.substr(4); }
        else if (csv.rfind("sync ", 0) == 0) { gait = "sync"; csv = csv.substr(5); }
        size_t q = csv.find_first_not_of(" \t");
        if (q == std::string::npos) return "ERR usage:run_script_[up|down]_[alt|sync]_<csv>\n";
        csv = csv.substr(q);
        return robot.cmd_run_script(csv, up, gait);
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
        if (iss.fail() || name.empty()) return "ERR usage:run_saved_<name>_[up|down]_[alt|sync]\n";
        std::string dir; iss >> dir;   // [2026-07-14] optional direction, default down
        std::string gait; iss >> gait; // [2026-07-23] optional gait, default alt
        if (gait.empty()) gait = "alt";
        return robot.cmd_run_saved(name, dir == "up", gait);
    }
    // [2026-07-09] Follower (2nd-moving leg) leveling mode: imu (IMU fine-level)
    // or meter (方案B meter-sync, original method). Reported in status follower_mode=.
    if (cmd == "set_follower_mode") {
        std::string mode; iss >> mode;
        if (iss.fail() || mode.empty()) return "ERR usage:set_follower_mode_<imu|meter>\n";
        return robot.cmd_set_follower_mode(mode);
    }
    // [2026-07-09] Which foot leads the first step: left | right. Reported in
    // status first_step=. Multi-step runs alternate from this seed.
    if (cmd == "set_first_step") {
        std::string side; iss >> side;
        if (iss.fail() || side.empty()) return "ERR usage:set_first_step_<left|right>\n";
        return robot.cmd_set_first_step(side);
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
    // [2026-08-26] QX-DO24 4-ch PWM output (cli_22_ slave 6), web panel 用。
    //   pwm set <ch1-4> <hz> <control> <duty%>   暫存寫入（斷電還原）
    //   pwm save                                 ⚠ 寫 flash 存成開機預設
    //   pwm status                               讀回 4 通道
    // 占空比 5~10% 與頻率鎖 50Hz 由 QX_DO24 driver 強制，這裡只解析參數。
    if (cmd == "pwm") {
        std::string sub; iss >> sub;
        if (iss.fail() || sub.empty()) return "ERR usage:pwm_<set|save|status>\n";
        if (sub == "status") return robot.cmd_pwm_status();
        if (sub == "save")   return robot.cmd_pwm_save();
        if (sub == "set") {
            int ch = 0, hz = 0, control = 0; double duty = 0;
            iss >> ch >> hz >> control >> duty;
            if (iss.fail()) return "ERR usage:pwm_set_<ch1-4>_<hz>_<control>_<duty%>\n";
            return robot.cmd_pwm_set(ch, hz, control, duty);
        }
        return "ERR usage:pwm_<set|save|status>\n";
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
        // [2026-08-28] 原本寫死 1..4，與應用層的 CUP_SLAVE_FIRST..LAST（5-8）
        // 沒有交集 → 這個指令在 08-27 改號後**不可能成功**。改吃同一組常數。
        if (iss.fail() || s < WashRobot::CUP_SLAVE_FIRST || s > WashRobot::CUP_SLAVE_LAST)
            return "ERR usage:zdt_pusher_<" + std::to_string(WashRobot::CUP_SLAVE_FIRST)
                 + ".." + std::to_string(WashRobot::CUP_SLAVE_LAST) + ">_<extend|retract>\n";
        return robot.cmd_zdt_pusher(s, a);
    }
    if (cmd == "zdt_zero") {
        std::string g; iss >> g;
        if (iss.fail()) return "ERR usage:zdt_zero_<right|left|all>\n";
        return robot.cmd_zdt_zero(g);
    }
    // [v2 2026-07-07] DM2J feet-wheel rails + 3-valve roll cal retired.
    if (cmd == "move")           return "ERR removed_in_v2\n";
    if (cmd == "wheels")         return "ERR removed_in_v2\n";
    if (cmd == "dm2j_group")     return "ERR removed_in_v2\n";
    if (cmd == "dm2j_zero")      return "ERR removed_in_v2\n";
    if (cmd == "tilt_mode")      return "ERR removed_in_v2\n";
    if (cmd == "confirm_balance") return "ERR removed_in_v2\n";
    if (cmd == "zdt_disable") {
        int s = 0; iss >> s;
        if (iss.fail() || s < WashRobot::CUP_SLAVE_FIRST || s > WashRobot::CUP_SLAVE_LAST)
            return "ERR usage:zdt_disable_<" + std::to_string(WashRobot::CUP_SLAVE_FIRST)
                 + ".." + std::to_string(WashRobot::CUP_SLAVE_LAST) + ">\n";
        return robot.cmd_zdt_disable(s);
    }
    if (cmd == "zdt_enable") {
        int s = 0; iss >> s;
        if (iss.fail() || s < WashRobot::CUP_SLAVE_FIRST || s > WashRobot::CUP_SLAVE_LAST)
            return "ERR usage:zdt_enable_<" + std::to_string(WashRobot::CUP_SLAVE_FIRST)
                 + ".." + std::to_string(WashRobot::CUP_SLAVE_LAST) + ">\n";
        return robot.cmd_zdt_enable(s);
    }
    if (cmd == "zdt_release_stall") return robot.cmd_zdt_release_stall();
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

bool is_fast(const std::string& verb) {
    return FAST_CMDS.count(verb) != 0;
}

}  // namespace command
