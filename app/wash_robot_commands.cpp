// WashRobot 的指令與流程實作。
//
// [2026-08-30] 重構階段 5：由 app/WASH_ROBOT.cpp 依**語意分界**切出。
// 分界點是原檔既有的 `//=========== commands ===========`（原第 5214 行）——
// 不是任意切一刀：前半是子系統（init / utility / crane / arm / IMU / pusher-vacuum），
// 後半是指令與流程。
//
// 🔴 拆之前確認過兩件跨檔案會斷的東西，兩者的使用點**全在前半**，故留在原檔：
//      匿名 namespace 的 apply_to_atomic_（原 :1183-1207）
//      file-static WEIGHT_NO_DATA_KG（原 :2542）
//
// ⚠️ 這是**純搬動**：函式內容逐字不變、順序不變。
//    前言（includes 與平台守衛）整段原樣複製 —— 🔴 第一次只挑 #include 行，
//    把 #ifdef _WIN32 守衛丟掉了，結果在 Linux 上去 include windows.h。
//    **抽「看起來相關的行」而不是「整個區塊」是典型的搬移事故。**

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "WASH_ROBOT.h"
#include "endpoints.h"
#include "profile.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <cmath>
#include <cstdlib>   // std::getenv (driver debug env var)
#include <algorithm> // std::max for weight reading
#include <future>    // std::async for clean_sweep Phase A/B parallelism
#include <limits>    // std::numeric_limits
#include <thread>    // std::thread for run_avoid frame-capture probe (2026-06-04)

//=========== commands ===========

// Public wrapper: dispatch to impl, then broadcast EVT so ALL connected
// clients see the final status (not just the one that sent the command).
// Success → "EVT init_complete status=ok"
// Failure → "EVT init_complete status=fail reason=<reason>"
std::string WashRobot::cmd_init() {
    std::string result = cmd_init_impl_();
    if (result.rfind("OK", 0) == 0) {
        evt_("init_complete status=ok");
    } else {
        // result format: "ERR <reason>\n" — strip prefix + trailing newline
        std::string reason = result;
        if (reason.rfind("ERR ", 0) == 0) reason.erase(0, 4);
        if (!reason.empty() && reason.back() == '\n') reason.pop_back();
        evt_("init_complete status=fail reason=" + reason);
    }
    return result;
}

std::string WashRobot::cmd_init_impl_() {
    State cur = state_.load();
    if (cur != State::Idle && cur != State::Ready && cur != State::Error)
        return state_violation_(cur);

    std::lock_guard<std::mutex> lk(motion_mtx_);
    abort_flag = false;
    pause_flag = false;

    std::cout << "[init] PQW relays → pump ON, valves/water OFF\n";
    // Wrapped in try_or_pause_ so a transient PQW comm fail drops into
    // PausedOnError (allowing 繼續/略過 from GUI) instead of returning ERR
    // and trapping the user (re-running init would hit the same fail).
    if (try_or_pause_([this]() { return pqw_.controlRelay(CH_PUMP, true); },
                      "init_pump_on")) return "ERR aborted\n";
    if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_RIGHT, false); },
                      "init_valve_right_off")) return "ERR aborted\n";
    if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_LEFT, false); },
                      "init_valve_left_off")) return "ERR aborted\n";
    if (try_or_pause_([this]() { return pqw_.controlRelay(CH_BRUSH, false); },
                      "init_brush_off")) return "ERR aborted\n";
    if (try_or_pause_([this]() { return pqw_.controlRelay(CH_WATER_PUMP, false); },
                      "init_water_pump_off")) return "ERR aborted\n";
    // [2026-06-05] water_inlet → crane PQW (.34 slave 12 CH4)
    if (try_or_pause_([this]() { return set_water_inlet_(false); },
                      "init_water_inlet_off")) return "ERR aborted\n";

    // [v2] No DM2J wheels or feet rails — vertical motion is crane-rope driven.
    // (v1 wheel-retract + feet-rail-home block removed here.)

    // Release stall + enable all ZDT drivers before sending any motion command.
    // ZDT firmware returns Modbus exception 0x03 (illegal data value) for pos_mode
    // calls when the drive is disabled or has a latched stall flag. WASH_ROBOT::init()
    // connects TCP but does not enable, and cmd_shutdown / cmd_emergency_stop disable,
    // so cmd_init must re-enable here. Matches Linux_test menu 3 sequence.
    std::cout << "[init] ZDT 1-4 → release_stall + driver enable"
              << (disabled_zdt_slaves_.empty() ? "" : " (disabled slaves skipped)")
              << "\n";
    for (int s = CUP_SLAVE_FIRST; s <= CUP_SLAVE_LAST; ++s) {
        if (disabled_zdt_slaves_.count(s)) {
            std::cout << "[init] ZDT " << s << " disabled — skip\n";
            continue;
        }
        Z_(s).release_stall_flag();
        if (try_or_pause_([this, s]() { return Z_(s).motion_control_driver_EN(true); },
                          std::string("init_zdt_enable_slave_") + std::to_string(s)))
            return "ERR aborted\n";
    }
    sleep_ms_(200);   // let drives settle into enabled state

    // [v2] init does NOT extend any pusher — all 4 cups stay retracted at 0.
    // cmd_attach opens the valves then extends via smart_extend_subset_ (the
    // disable_seal 「一點一點補伸」 pipeline). Just clear any stale stall flags.
    for (int s = CUP_SLAVE_FIRST; s <= CUP_SLAVE_LAST; ++s)
        if (!disabled_zdt_slaves_.count(s)) Z_(s).release_stall_flag();

    // 🔴 [2026-08-28] 注意這寫的是 0x0021「把當前位置設為零點」，**不是 homing**
    //    （homing 是 0x0020，會去找原點感測器）。也就是說 **零點 = init 執行當下
    //    滑台碰巧所在的位置**，不是機械原點。
    //    ⚠ 後果：`ARM_RAIL_TRAVEL_MAX_CM` 那個行程守衛守的是**指令座標**，
    //    如果 init 時滑台停在外面，座標原點就跟著偏移，守衛擋不住實際超程。
    //    → 開機前應確認滑台已收到最左端；真正的解法是啟用 homing（見待辦）。
    std::cout << "[init] DM2J arm (slave " << DM2J_ARM << ") → set current as zero"
              << "（⚠ 零點=當前位置，非機械原點）\n";
    if (D_(DM2J_ARM).home_set_current_pos_zero())
        std::cerr << "[init] ⚠ DM2J arm set-zero 送出失敗 — 之後的絕對位置指令基準不可信\n";

    std::cout << "[init] IMU → take baseline\n";
    if (imu_take_baseline_()) return "ERR imu_baseline_fail\n";

    // [2026-05-28] damiao arm INIT (M1 calibrate + M2 lr_calibrate + set_zero).
    // Moved here from per-sweep so that calibration happens ONCE at system init
    // with operator present, bounded the obstacle-corrupts-zero risk to a single
    // controlled moment. Subsequent sweeps use ensure_arm_ready_() to just
    // ENABLE the motors (re-enable after PARK) without re-calibrating.
    //
    // Failure mode (per user decision 2026-05-28): warn + continue. arm_calibrated_
    // stays false → next cmd_arm_clean_sweep / cmd_run-with-sweep would error
    // ("arm not calibrated") rather than damage the mechanism with a bad zero.
    // Operator can re-run cmd_init after fixing the arm to retry.
    //
    // arm_attached_=off: skip + flag=true (bench-mode without arm; sweep already
    // skips via its own arm_attached_=off early-return).
    if (!arm_attached_.load()) {
        std::cout << "[init] arm_attached=off — skip damiao arm INIT, mark calibrated\n";
        arm_calibrated_.store(true);
    } else {
        std::cout << "[init] damiao arm → INIT (M1 calibrate + M2 lr_calibrate, ~10s)\n";
        std::string r = arm_cmd_("INIT", 60);
        if (r.rfind("OK", 0) == 0) {
            arm_calibrated_.store(true);
            std::cout << "[init] arm INIT OK → arm_calibrated_=true\n";
        } else {
            arm_calibrated_.store(false);
            std::cerr << "[init] arm INIT FAILED (" << r
                      << ") — continuing init but sweep paths will refuse until "
                         "arm fixed and cmd_init re-run\n";
            evt_("arm_init_failed " + r);
        }
    }

    std::cout << "[init] done → Ready\n";
    set_state_(State::Ready);
    return "OK init_done\n";
}

// [2026-06-03] RAII guard: sets step_in_progress_=true on construction, clears
// on destruction. Ensures the flag is reset across all return paths (success,
// ERR, exception). cmd_status reads this flag to suppress JC100 fresh-read
// during step ops — prevents GUI poll from hammering cli_22_ while step body/
// feet pre_cycle uses PQW/JC100/DM2J:14 on same bus (bus contention caused
// PQW verify failures + DM2J:14 timeouts in 2026-06-03 bench).
// [2026-06-08] Moved up from line ~6710 (post-cmd_attach) to allow cmd_attach
// to also use this guard — was hammering cli_22_ throughout attach.
struct StepInProgressGuard {
    std::atomic<bool>& flag;
    StepInProgressGuard(std::atomic<bool>& f) : flag(f) { flag.store(true); }
    ~StepInProgressGuard() { flag.store(false); }
};

std::string WashRobot::cmd_attach() {
    State cur = state_.load();
    if (cur != State::Ready) return state_violation_(cur);

    // unique_lock instead of lock_guard — needed to manually unlock before
    // mid-attach do_feet_realign_ (which acquires motion_mtx_ itself; same-thread
    // re-lock on std::mutex = deadlock). Same pattern as do_step_down_/up_.
    std::unique_lock<std::mutex> lk(motion_mtx_);
    abort_flag = false;
    // [2026-06-08] Suppress cmd_status fresh-read during attach (mirrors
    // step_down/up). Without this, GUI 1Hz poll hammered all 9 JC100 on cli_22_
    // throughout the 10-30s attach flow, contending with disable_seal's own
    // WAIT_SEAL polling → connection-level timeouts on JC100:5/8 (slaves still
    // active in WAIT_SEAL) → WAIT_SEAL silently took 55s real-time instead of 5s.
    StepInProgressGuard _sip_guard{step_in_progress_};
    // [2026-05-29] Reset arm sweep obstacle/skip flags — attach = start fresh.
    arm_sweep_obstacle_pending_.store(false);
    arm_sweep_skip_rest_of_run_.store(false);

    // [v2] attach: hanging → four cups sealed. The machine hangs on the crane
    //   rope here (no side needs to anchor), so BOTH sides extend TOGETHER:
    //   open both valves, then one smart_extend_subset_ over all 4 cups (uses
    //   pusher_move_many_ = simultaneous extend, disable_seal 「一點一點補伸」).
    //   No body/center groups, no mid-attach feet realign.

    // 1. Open BOTH valves (right CH1 + left CH3).
    std::cout << "[attach] open RIGHT valve CH" << CH_VALVE_RIGHT
              << " + LEFT valve CH" << CH_VALVE_LEFT << "\n";
    if (try_or_pause_([this]() { return pqw_set_relay_verified_(CH_VALVE_RIGHT, true); },
                      "attach_valve_right_on")) return "ERR aborted\n";
    if (try_or_pause_([this]() { return pqw_set_relay_verified_(CH_VALVE_LEFT, true); },
                      "attach_valve_left_on")) return "ERR aborted\n";

    // 2. Extend all four cups together (left + right simultaneously).
    {
        std::vector<int> all_slaves;
        for (int s : {ZDT_RF1, ZDT_RF2, ZDT_LF1, ZDT_LF2}) {
            if (!disabled_zdt_slaves_.count(s)) all_slaves.push_back(s);
        }
        if (!all_slaves.empty()) {
            std::cout << "[attach] all-cups disable_seal — extend + wait vacuum (L+R together)\n";
            if (try_or_pause_([this, &all_slaves]() { return smart_extend_subset_("all", all_slaves); },
                              "attach_all_disable_seal_wait"))
                return "ERR aborted\n";
        }
    }

    // 3. Per-cup vacuum check；未密封的 cup 用 disable_seal 機制重伸。
    //    與 step_up/down 共用 smart_extend_subset_ —— 主動推進 + 即時監看真空 +
    //    intended_target 追蹤，取代舊 fine_tune（靜態 preset 基準，cup 已過頭時三輪空推）。
    //    只重伸「未密封」的 cup：disable_seal Phase 1 會快伸到 preset−1cm，
    //    若把已密封的 cup 一起傳進去會被 Phase 1 縮回而破真空。
    auto initial_fails = vacuum_check_("all");
    if (!initial_fails.empty()) {
        std::cout << "[attach] cups not sealed after extend:";
        for (int s : initial_fails) std::cout << " " << s;
        std::cout << " → smart_extend (disable_seal)\n";

        // Re-extend each unsealed cup grouped by its own side.
        std::vector<int> right_fails, left_fails;
        for (int s : initial_fails) {
            if (s == ZDT_RF1 || s == ZDT_RF2)      right_fails.push_back(s);
            else if (s == ZDT_LF1 || s == ZDT_LF2) left_fails.push_back(s);
        }

        if (!right_fails.empty() &&
            try_or_pause_([this, &right_fails]() { return smart_extend_subset_("right", right_fails); },
                          "attach_right_smart_extend"))
            return "ERR aborted\n";
        if (!left_fails.empty() &&
            try_or_pause_([this, &left_fails]() { return smart_extend_subset_("left", left_fails); },
                          "attach_left_smart_extend"))
            return "ERR aborted\n";

        auto remaining = vacuum_check_("all");
        if (!remaining.empty()) {
            std::cout << "[attach] WARN cups still unsealed after smart_extend:";
            for (int s : remaining) std::cout << " " << s;
            std::cout << " (proceeding to Attached anyway)\n";
            evt_("attach_partial_seal count=" + std::to_string((int)remaining.size()));
            // [2026-08-28] 也記進回傳字串。原本部分密封只走 console + EVT，
            // 而**回覆仍是乾淨的 "OK attached"** —— 只看回傳值的呼叫端
            // （腳本、run 序列）看不出有幾顆沒吸住。
            // 慣例照同檔的 cmd_zdt_release_stall（"OK released ok=3 fail=1"）。
            attach_partial_seal_ = (int)remaining.size();
        } else {
            attach_partial_seal_ = 0;
            std::cout << "[attach] all cups sealed after smart_extend\n";
        }
    } else {
        std::cout << "[attach] all 4 cups sealed on first check\n";
    }

    // 4. Pay out crane rope to transfer body weight from the rope onto the
    //    suction cups, leaving a light residual rope tension. Target tension
    //    = crane's `g_retract_tension_stop_kg` (web「收繩軟停張力」, same knob
    //    step_up/step_down retract uses for its soft tension stop). Falls
    //    back to ATTACH_PAYOUT_TARGET_KG if the crane status read fails.
    //
    //    SAFETY GATE — re-check vacuum first. If ANY cup is still unsealed
    //    (fine_tune couldn't bring it up), DO NOT pay out: paying out transfers
    //    weight to the cups; if some cups aren't holding, the load piles onto
    //    the rest → overload → pop off → machine falls. Skip safely; the rope
    //    keeps bearing the weight (= attach-entry state, always safe).
    //
    //    Non-fatal: any error path (unsealed gate / tension sensor offline /
    //    crane detached / pay_out reply non-OK) → skip + EVT, proceed to
    //    Attached anyway (cups are sealed — that's the load-bearing part).
    //
    // [2026-08-27 per user] 整段停用——attach 結尾不再放繩。
    // 效果：機體重量一直留在吊繩上，吸盤只負責貼牆定位、不承重。
    // 這與 v2 的同步步伐更一致：do_step_sync_ 本來就是「4 顆全放開、完全靠鋼索
    // 承重」的設計，原本 attach 把重量交給吸盤、下一步 step 又立刻放開吸盤讓重量
    // 彈回繩上，那次轉移是多餘的。
    // 副作用：吸盤不再預先承重，因此 attach 之後不會有「吸盤是否撐得住整機重量」
    // 的實測驗證——原本 pay_out 前的 vacuum_check_ 安全閘也隨之失效。若日後要恢復
    // 承重式 attach，把下面的 #if 0 改回 #if 1 即可（內含完整的未密封安全閘邏輯）。

    std::cout << "[attach] done → Attached (rope keeps bearing weight; no pay_out)\n";
    set_state_(State::Attached);
    if (attach_partial_seal_ > 0) {
        // 🔴 仍然回 OK：這個狀態是**安全的**（鋼索繼續承重，放繩有 SAFETY GATE
        //    擋著），所以不該讓呼叫端當成錯誤而中止。但要讓它**看得見**。
        return "OK attached partial_seal=" + std::to_string(attach_partial_seal_) + "\n";
    }
    return "OK attached\n";
}

std::string WashRobot::cmd_detach() {
    State cur = state_.load();
    if (cur != State::Attached) return state_violation_(cur);

    std::lock_guard<std::mutex> lk(motion_mtx_);
    std::cout << "[detach] close valves CH" << CH_VALVE_RIGHT << "/"
              << CH_VALVE_LEFT << " → Ready\n";
    pqw_.controlRelay(CH_VALVE_RIGHT, false);
    pqw_.controlRelay(CH_VALVE_LEFT,  false);
    set_state_(State::Ready);
    return "OK detached\n";
}

// Internal sweep — caller must already hold motion_mtx_
std::string WashRobot::do_arm_sweep_() {
    // Start cleaning: brush motor + water pump + water inlet valve.
    // PQW controlRelay return ignored — cleanup at end of function MUST run
    // unconditionally (water flowing without arm motion = bigger problem than
    // ignoring return). LED + water flow = real verification.
    // [2026-07-24 per user] 手臂已實機裝上，CH_BRUSH 恢復開刷；水閥/水泵尚未接管路，繼續維持關閉
    //pqw_.controlRelay(CH_WATER_INLET, true);
    //pqw_.controlRelay(CH_WATER_PUMP,  true);
    pqw_.controlRelay(CH_BRUSH,       true);

    // Sweep: centre → right → centre (home). Each DM2J motion wrapped in
    // try_or_pause_ so a stall / comm fail on arm drops into PausedOnError for
    // operator manual intervention rather than aborting the whole sweep.
    // (2026-05-25: 移除 -ARM_SWEEP_CM 段,改單向 +CM→0)
    std::string err;
    if (try_or_pause_([this]() { return D_(DM2J_ARM).PR_move_cm(0, 1, ARM_SWEEP_RPM,  ARM_SWEEP_CM, ARM_SWEEP_ACC, ARM_SWEEP_DEC); },
                      "arm_sweep_right"))
        err = "ERR aborted\n";
    else if (check_abort_())
        err = "ERR aborted\n";
    // [2026-05-25] 拿掉 -ARM_SWEEP_CM 段(連同它後面的 check_abort_)。改回雙向取消註解即可。
    //else if (try_or_pause_([this]() { return D_(DM2J_ARM).PR_move_cm(0, 1, ARM_SWEEP_RPM, -ARM_SWEEP_CM, ARM_SWEEP_ACC, ARM_SWEEP_DEC); },
    //                       "arm_sweep_left"))
    //    err = "ERR aborted\n";
    //else if (check_abort_())
    //    err = "ERR aborted\n";
    else if (try_or_pause_([this]() { return D_(DM2J_ARM).PR_move_cm(0, 1, ARM_SWEEP_RPM, 0.0, ARM_SWEEP_ACC, ARM_SWEEP_DEC); },
                           "arm_sweep_home"))
        err = "ERR aborted\n";

    // Stop cleaning regardless of outcome
    pqw_.controlRelay(CH_BRUSH,       false);
    pqw_.controlRelay(CH_WATER_PUMP,  false);
    set_water_inlet_(false);   // [2026-06-05] → crane PQW (.34 slave 12 CH4)

    return err.empty() ? "OK arm_sweep_done\n" : err;
}

// Public: acquires motion_mtx_ then delegates to do_arm_sweep_
std::string WashRobot::cmd_arm_sweep() {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    std::lock_guard<std::mutex> lk(motion_mtx_);

    // [2026-08-31] 補上進入點的 abort 清除，與姊妹函式 cmd_arm_clean_sweep_dry 一致
    // （cmd_side_measured 的同型修正是 28dfa30，2026-08-31 實機驗證＝檢查表 ⑨a）。
    //
    // ⚠️ **嚴重性更正（2026-08-31 實測）**：ONBOARDING §1 / runbook §A2「⑨b」宣稱
    //    「任何一次 stop / emergency_stop 之後 arm_sweep 會**永久**回 ERR aborted，
    //     只能重開主程式才能恢復」——**實測不成立**：
    //      · 稽核全部 4 個設 abort_flag=true 的位置：cmd_emergency_stop 與
    //        imu_monitor_loop_ **都同時 set_state_(State::Error)**；
    //        cmd_shutdown / stop() 是收工路徑。
    //      · cmd_reset **會清掉 abort_flag**，所以 Error → reset 就恢復了。
    //      · 而且本函式開頭的 State::Error 檢查會**先**攔下來，abort_flag 這條走不到。
    //    → 目前**沒有已知可達路徑**會讓 abort_flag 停在 true 而狀態不是 Error。
    //
    // 📌 那為什麼還是加？**防的是未來新增一條「設 abort_flag 但不進 Error」的路徑**——
    //    姊妹函式都有這一行，只有這裡沒有，本身就是不一致；補上的成本是一行。
    // 📌 位置比照姊妹函式：**取得 motion_mtx_ 之後**才清 —— 放在鎖之前的話，一個被拒絕的
    //    重疊呼叫會清掉另一條執行緒正在進行中的 abort。
    abort_flag = false;

    return do_arm_sweep_();
}

// [2026-08-26 per user] 乾式清洗（bench 測試用）——見 WASH_ROBOT.h 的說明。
// 完整 DEPLOY + 滾筒 + 上滑台 + PARK，不噴水、不移動機器人。
std::string WashRobot::cmd_arm_clean_sweep_dry() {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    std::lock_guard<std::mutex> lk(motion_mtx_);

    abort_flag = false;   // 比照 do_step_sync_：進入點清掉上一次殘留的 abort

    // do_step_sync_rail_sweep_ 不自己做 INIT（在 do_step_sync_ 裡是跟吊機放繩並行
    // 跑完後把結果傳進來），所以這裡補上。INIT 失敗不擋——該函式會退化成純上滑台
    // 掃動、不開刷，跟正式流程的降級行為一致。
    std::cout << "[arm_dry_sweep] INIT (arm re-calibrate)\n";
    const bool init_ok = (arm_cmd_("INIT", 60).rfind("OK", 0) == 0);
    arm_calibrated_.store(init_ok);
    if (!init_ok)
        std::cerr << "[arm_dry_sweep] arm INIT failed — rail sweep only, no brush\n";

    // force_enable=true：這支是拿來測上滑台裝好沒有的工具，不受
    // STEP_SYNC_ARM_CLEAN_ENABLED（正式步伐的清潔開關）影響。
    do_step_sync_rail_sweep_("arm_dry_sweep", init_ok, /*force_enable=*/true);

    if (check_abort_()) return "ERR aborted\n";
    return init_ok ? "OK arm_clean_sweep_dry_done\n"
                   : "OK arm_clean_sweep_dry_done_rail_only\n";
}

// [方案B 2026-07-08] Read both crane meters (cm). See header.
bool WashRobot::read_crane_meters_(double& len_left, double& len_right) {
    const std::string st = crane_cmd_("status", 3);
    // [2026-07-14] Sanity bound — a corrupted/garbage SD76 read (e.g. stale
    // Modbus frame) can still parse as a valid double via std::stod. Without
    // a range check, a garbage self_len flips crane_abs_target_cmd_'s delta
    // sign (target - self_len), sending the rope the WRONG DIRECTION — the
    // per-move cap there only bounds magnitude, not sign. Reject anything
    // outside physically plausible rope length as a parse failure so the
    // caller falls back to fixed_step() (direction locked at step start,
    // independent of this read).
    constexpr double CRANE_METER_SANITY_MAX_CM = 20000.0;  // 200m — generous, tune to max real deployment
    auto parse = [&st](const std::string& key, double& out) -> bool {
        auto p = st.find(key);
        if (p == std::string::npos) return true;
        const std::string v = st.substr(p + key.size());
        if (v.compare(0, 3, "ERR") == 0) return true;
        try {
            double val = std::stod(v);
            if (std::fabs(val) > CRANE_METER_SANITY_MAX_CM) return true;
            out = val;
            return false;
        } catch (...) { return true; }
    };
    return parse("length_left=", len_left) || parse("length_right=", len_right);
}

// [方案B 2026-07-08] Move one step side to the pre-computed common absolute
// target length. See header. Direction is derived from the signed delta, so the
// same routine serves both step_down (pay_out to lengthen) and step_up (retract
// to shorten); dir_word is only used for the fixed-step fallback.
std::string WashRobot::crane_abs_target_cmd_(const std::string& move_group,
                                             const std::string& dir_word,
                                             int step, bool target_valid,
                                             double target_len, int& out_timeout,
                                             double& out_mv_cm) {
    // out_mv_cm is a plain magnitude (cm), direction-independent — same meaning
    // whether this call is a step_down pay_out or a step_up retract. Callers
    // use it as the vacuum-retry backup budget so a retry never retreats past
    // where this side stood before THIS call moved it.
    auto fixed_step = [&]() -> std::string {
        out_timeout = crane_motion_timeout_sec_(step);
        out_mv_cm   = (double)step;
        return dir_word + " " + std::to_string(step);
    };
    // No valid pre-step target (crane read failed at step start) → blind fixed step.
    if (!target_valid) return fixed_step();

    // Read THIS side's own meter fresh; the target itself was locked at step start.
    double len_left = 0, len_right = 0;
    if (read_crane_meters_(len_left, len_right)) {
        std::cout << "[step_level] " << move_group
                  << " meter read fail — fixed step " << step << "cm fallback\n";
        evt_("step_level_meter_read_fail " + move_group);
        return fixed_step();
    }
    const double self_len = (move_group == "left") ? len_left : len_right;
    const double delta  = target_len - self_len;   // >0: need longer → pay_out; <0: retract
    const double adelta = std::fabs(delta);

    if (adelta < LEVEL_MATCH_TOL_CM) {
        std::cout << "[step_level] " << move_group << " already at target (self=" << self_len
                  << " target=" << target_len << " delta=" << delta << "cm) — no crane move\n";
        out_timeout = 0;
        out_mv_cm   = 0.0;
        return "";
    }
    // Each side moves at most ~one step: the lagging side needs exactly `step`,
    // the leading side gives way (needs ≤step). Clamp to step+margin as a backstop
    // for a pathological gap; the remainder self-corrects next step (clamp, not
    // reject — rejecting was the old bug that let tilt accumulate). Never exceed
    // the LEVEL_MAX_DELTA_CM hard ceiling.
    double move_cm = adelta;
    const double cap = std::min((double)(step + LEVEL_MOVE_MARGIN_CM), (double)LEVEL_MAX_DELTA_CM);
    if (move_cm > cap) {
        std::cout << "[step_level] " << move_group << " delta=" << delta
                  << "cm exceeds per-move cap " << cap << "cm — clamping (remainder next step)\n";
        evt_("step_level_delta_capped " + move_group + " delta=" + std::to_string((int)delta));
        move_cm = cap;
    }
    const int         mv   = (int)std::lround(move_cm);
    const std::string word = (delta > 0) ? ("pay_out_" + move_group)
                                         : ("retract_" + move_group);
    std::cout << "[step_level] " << move_group << " → target: self=" << self_len
              << " target=" << target_len << " delta=" << delta << "cm → " << word << " " << mv << "\n";
    out_timeout = crane_motion_timeout_sec_(mv);
    out_mv_cm   = (double)mv;
    return word + " " + std::to_string(mv);
}

// [策略1 2026-07-09] IMU fine-level the follower side to the datum. See header.
// [2026-08-28] IMU 健康判定 —— 取代原本「瞬間讀一次 imu_.read_error」的寫法。
//
// WT901BC_TTL 的 read_error 是**逐封包**旗標：checksum 錯一包就 true，下一包
// 正常就清回 false（WT901BC_TTL.cpp:47/75/95），另外連續 500ms 沒有有效封包也會
// 設 true。裝置以 115200 持續串流，偶發一包壞掉很正常 —— 在某個瞬間抓到 true
// 完全不能代表「IMU 不能用」。
//
// bench 症狀（2026-08-28）：`[step_sync_imu] IMU read_error mid-pass — stop`
// —— 整步的水平校正被一個瞬時旗標取消，同一步吊機回報左右差 2cm 就這樣留著
// 累積到下一步。
//
// 這裡改成掃一個視窗（預設 6 × 50ms = 300ms，跟 read_roll_avg 的取樣窗一致）：
// 只要期間有**任何一次**讀到正常就當作可用，全部都壞才算真的不可用。
bool WashRobot::imu_persistently_bad_(int samples, int gap_ms) {
    if (samples < 1) samples = 1;
    for (int k = 0; k < samples; ++k) {
        if (!imu_.read_error.load()) return false;   // 有一次好的就夠了
        if (k < samples - 1) sleep_ms_(gap_ms);
    }
    return true;
}

void WashRobot::follower_imu_level_(const std::string& move_group) {
    // Runtime mode: meter (原本方法, 方案B only) skips the IMU trim entirely; the
    // coarse measured descent already ran in pre_cycle.
    if (!follower_use_imu_.load()) return;
    constexpr double DEG2RAD = 0.017453292519943295;

    // IMU must be healthy — else fall back to the coarse measured descent alone.
    // [2026-08-28] 同 do_sync_imu_roll_correct_ 的修正：交替走法這條路徑有一模一樣
    // 的瞬時旗標問題，一併改用視窗判定。
    if (imu_persistently_bad_()) {
        std::cout << "[imu_level] " << move_group << " skip — IMU 持續讀不到 (coarse meter only)\n";
        evt_("imu_level_skip_imu_error " + move_group);
        return;
    }
    // Let the just-released corner stop swinging before trusting roll.
    sleep_ms_(FOLLOWER_IMU_SETTLE_MS);

    // [2026-07-14 per user] Roll read = short-window AVERAGE, not a single snapshot.
    // A single instantaneous read can catch a residual-swing peak → wrong trim
    // magnitude → extra pass. Averaging ~300ms of the continuously-updated IMU
    // rejects that swing → trim closer to correct → higher one-pass hit rate.
    // Skips samples flagged read_error; falls back to a raw read if all bad.
    auto read_roll_avg = [this]() -> double {
        constexpr int SAMPLES = 6;
        constexpr int GAP_MS  = 50;   // ~300ms window
        double sum = 0.0; int n = 0;
        for (int k = 0; k < SAMPLES; ++k) {
            if (!imu_.read_error.load()) { sum += imu_.x; ++n; }
            if (k < SAMPLES - 1) sleep_ms_(GAP_MS);
        }
        return (n > 0 ? sum / n : imu_.x) - imu_roll0_;
    };

    for (int pass = 0; pass < FOLLOWER_IMU_MAX_PASSES; ++pass) {
        if (check_abort_()) return;
        if (imu_persistently_bad_()) {
            std::cout << "[imu_level] " << move_group << " IMU 持續讀不到 mid-trim — stop (non-fatal)\n";
            evt_("imu_level_abort_imu_error " + move_group);
            return;
        }
        const double roll = read_roll_avg();
        // Panic: too tilted to trim safely — leave to the §10 L2 guard / operator.
        if (std::fabs(roll) > BAL_CAL_ROLL_PANIC_DEG) {
            std::cout << "[imu_level] " << move_group << " ROLL PANIC " << roll << "° — stop trim (non-fatal)\n";
            evt_("imu_level_roll_panic " + move_group + " roll=" + std::to_string(roll));
            return;
        }
        // Level enough.
        if (std::fabs(roll) < FOLLOWER_ROLL_TOL_DEG) {
            std::cout << "[imu_level] " << move_group << " level after " << pass
                      << " pass — roll=" << roll << "°\n";
            evt_("imu_level_ok " + move_group + " roll=" + std::to_string(roll));
            return;
        }
        // Estimate the correcting distance from geometry: span·tan(roll). Exact span
        // is not critical — we re-read roll and iterate, so convergence is on the
        // true roll (immune to long-rope meter scale error), span only sets pace.
        double cm = FOLLOWER_SPAN_CM * std::tan(std::fabs(roll) * DEG2RAD);
        if (cm < 1.0) cm = 1.0;
        if (cm > (double)FOLLOWER_IMU_MAX_TRIM_CM) cm = (double)FOLLOWER_IMU_MAX_TRIM_CM;
        const int mv = (int)std::lround(cm);
        // Direction (roll>0 ⟹ right low / left high, per bal_cal sign convention):
        //   follower left  high → lower it  → pay_out_left;  low → raise → retract_left
        //   follower right low  → raise it  → retract_right; high → lower → pay_out_right
        std::string word;
        if (move_group == "left") word = (roll > 0) ? ("pay_out_left")  : ("retract_left");
        else                      word = (roll > 0) ? ("retract_right") : ("pay_out_right");
        const std::string cs = word + " " + std::to_string(mv);
        const int to = crane_motion_timeout_sec_(mv);
        std::cout << "[imu_level] " << move_group << " pass " << pass << " roll=" << roll
                  << "° → " << cs << " (span-est " << FOLLOWER_SPAN_CM << "cm)\n";
        // Tension-safe measured move (NOT raw on): keeps meter-death/tension guards.
        if (crane_cmd_(cs, to).rfind("OK", 0) != 0) {
            std::cout << "[imu_level] " << move_group << " crane move fail — stop trim (non-fatal)\n";
            evt_("imu_level_crane_fail " + move_group);
            return;
        }
        sleep_ms_(FOLLOWER_IMU_SETTLE_MS);   // settle before re-reading roll
    }
    const double end_roll = imu_.read_error.load() ? 0.0 : (imu_.x - imu_roll0_);
    std::cout << "[imu_level] " << move_group << " NOT converged in " << FOLLOWER_IMU_MAX_PASSES
              << " passes — roll=" << end_roll << "° — proceed on coarse (non-fatal)\n";
    evt_("imu_level_no_converge " + move_group + " roll=" + std::to_string(end_roll));
}

// [2026-07-22 per user] Differential IMU roll correction for do_step_sync_.
// do_step_sync_ releases ALL 4 cups together (no resealed datum side the way
// the alternating gait always has one), so this drives the crane-side
// "roll_correct <delta_cm>" differential primitive (+delta = 左放右收, i.e.
// pay_out left + retract right in ONE call) instead of follower_imu_level_'s
// single-side nudge. Same sign convention as follower_imu_level_ (roll>0 =
// left high/right low → +delta levels it, matching "左放右收").
// Geometry estimate halved vs follower_imu_level_'s span·tan(roll): here BOTH
// ropes move oppositely in one differential move, so half the single-side
// distance produces roughly the same leveling effect for the same span.
// Non-fatal throughout — any failure or non-convergence just leaves residual
// tilt for the next step rather than aborting the whole gait; feet still need
// to go back out either way.
void WashRobot::do_sync_imu_roll_correct_() {
    constexpr double DEG2RAD = 0.017453292519943295;

    // [2026-08-28] 改用視窗判定，不再瞬間取樣一次就放棄（見 imu_persistently_bad_）。
    if (imu_persistently_bad_()) {
        std::cout << "[step_sync_imu] skip — IMU 持續讀不到（300ms 內每次取樣都是 "
                     "read_error），本步不做校正\n";
        evt_("step_sync_imu_skip_error");
        return;
    }
    sleep_ms_(FOLLOWER_IMU_SETTLE_MS);   // let the just-released machine stop swinging

    // 🔴 [2026-09-01] 軸向修正 imu_.z -> imu_.x —— **實測證據，不是推論**：
    //   status 實讀 ax=-0.00 ay=0.03 az=1.00  → 重力全在 Z 軸 = IMU **水平安裝**
    //                raw_x=1.87 raw_y=0.12 raw_z=-151.05
    //   水平安裝下 imu_.x=尤拉滾轉、imu_.z=磁力計航向角（與傾斜無關）。
    // WASH_ROBOT.cpp:2781 那句「實測 roll 改讀 yaw 才會隨左右傾斜穩定變化」
    // 是 **2026-08-26 垂直安裝時代**的觀察；08-27 改回水平時 cmd_status /
    // imu_monitor_loop_ / imu_take_baseline_ 都改成 x/y 了，**只有本函式與
    // follower_imu_level_ 和 init 的那行 print 沒改**。
    // 🔴🔴 實際後果不是「修正方向錯」而是「從來沒修正過」：讀到 raw_z=-151 →
    //   |−151| > BAL_CAL_ROLL_PANIC_DEG(15) 恆為真 → 每次都走 ROLL PANIC 分支
    //   直接 return（non-fatal，只印一行）。而 do_sync_imu_roll_correct_ 在
    //   do_step_sync_（v2 正式走法）的活路徑上。
    auto read_roll_avg = [this]() -> double {
        constexpr int SAMPLES = 6;
        constexpr int GAP_MS  = 50;   // ~300ms window, same as follower_imu_level_
        double sum = 0.0; int n = 0;
        for (int k = 0; k < SAMPLES; ++k) {
            if (!imu_.read_error.load()) { sum += imu_.x; ++n; }
            if (k < SAMPLES - 1) sleep_ms_(GAP_MS);
        }
        return (n > 0 ? sum / n : imu_.x) - imu_roll0_;
    };

    for (int pass = 0; pass < FOLLOWER_IMU_MAX_PASSES; ++pass) {
        if (check_abort_()) return;
        // [2026-08-28] 同上：瞬時 read_error 不足以判定 IMU 壞掉。原本這行讓整段
        // 校正被一包壞封包取消（bench 2026-08-28 實際發生過）。
        if (imu_persistently_bad_()) {
            std::cout << "[step_sync_imu] IMU 持續讀不到 mid-pass — stop (non-fatal)\n";
            evt_("step_sync_imu_abort_error");
            return;
        }
        const double roll = read_roll_avg();
        if (std::fabs(roll) > BAL_CAL_ROLL_PANIC_DEG) {
            std::cout << "[step_sync_imu] ROLL PANIC " << roll << "° — stop trim, leave to operator\n";
            evt_("step_sync_imu_roll_panic roll=" + std::to_string(roll));
            return;
        }
        if (std::fabs(roll) < FOLLOWER_ROLL_TOL_DEG) {
            std::cout << "[step_sync_imu] level after " << pass << " pass(es) — roll=" << roll << "°\n";
            evt_("step_sync_imu_ok roll=" + std::to_string(roll));
            return;
        }
        double cm = 0.5 * FOLLOWER_SPAN_CM * std::tan(std::fabs(roll) * DEG2RAD);
        if (cm < 1.0) cm = 1.0;
        if (cm > (double)FOLLOWER_IMU_MAX_TRIM_CM) cm = (double)FOLLOWER_IMU_MAX_TRIM_CM;
        const int delta_cm = (int)std::lround(roll > 0 ? cm : -cm);
        std::ostringstream oss;
        oss << "roll_correct " << delta_cm;
        const std::string cs = oss.str();
        const int to = crane_motion_timeout_sec_(std::abs(delta_cm));
        std::cout << "[step_sync_imu] pass " << pass << " roll=" << roll
                  << "° → " << cs << " (span-est " << FOLLOWER_SPAN_CM << "cm, halved for differential)\n";
        if (crane_cmd_(cs, to).rfind("OK", 0) != 0) {
            std::cout << "[step_sync_imu] roll_correct fail — stop trim (non-fatal)\n";
            evt_("step_sync_imu_crane_fail");
            return;
        }
        sleep_ms_(FOLLOWER_IMU_SETTLE_MS);
    }
    const double end_roll = imu_.read_error.load() ? 0.0 : (imu_.x - imu_roll0_);
    std::cout << "[step_sync_imu] NOT converged in " << FOLLOWER_IMU_MAX_PASSES
              << " passes — roll=" << end_roll << "° — proceed anyway (non-fatal)\n";
    evt_("step_sync_imu_no_converge roll=" + std::to_string(end_roll));
}

// Internal one-step engine. No state guard (caller manages State::Running transition).
// Acquires motion_mtx_. Returns "OK step_done\n" / "ERR <reason>\n".
//
// Uses DM2J absolute positioning:
//   feet target = absolute +STEP_CM  → auto-absorbs any body_residual_cm_ at start
//   body target = absolute 0
//   retries use relative ±VACUUM_BACKUP_CM backup
//
// Rail coord: feet forward = rail +, body forward = rail - (shared rail axis).
std::string WashRobot::do_step_down_(bool skip_cleaning_sweep,
                                      std::function<void()> after_feet_rail_hook,
                                      std::function<void()> before_feet_rail_hook,
                                      bool right_first) {
    // 🔴🔴 [2026-08-31] 停用：本函式假設「每側有獨立的真空閥」，而硬體沒有。
    // 真空幫浦一顆繼電器控 4 顆吸盤，三口二位閥也是一顆繼電器控 4 顆（per user）。
    // CH_VALVE_LEFT == CH_VALVE_RIGHT == 1，group_valve_ch_() 兩個 group 都回 1。
    // → pre_cycle 先用 group_seal_ok_(anchor_group) 確認錨定側吸牢，
    //   下一行 pqw_set_relay_verified_(valve_ch, false) 關的卻是**唯一那顆閥**
    //   → 剛驗證過的錨定側跟著失去真空，而機器正吊在玻璃上。
    //   relay 寫入會 verify 成功、log 一切正常。
    // 而且現行操作模式根本沒有「交替」：移動＝吊機收放繩，行進間靠風扇（QX_DO24 PWM
    // 5-10%）把本體壓在玻璃上，吸盤只在定點當錨 —— 那正是 do_step_sync_ 的流程。
    // 📌 保留原碼未動，等改寫成單閥架構（或正式移除）時再處理。走 *_sync 版本。
    return "ERR alt_gait_disabled_single_valve (use *_sync; see work_log 2026-08-31)\n";

    // [v2 2026-07-07] Descend one step. No DM2J rail — each side's crane rope
    // pays out `step_cm` while the OTHER side's 2 cups anchor the machine.
    // Robustness (per user 2026-07-07): each side runs through the SAME v1
    // `cycle_group_` retry engine — extend (disable_seal 一點一點補伸) → verify
    // → 沒吸牢就退一點到新牆點 backup → retry (VACUUM_RETRY_MAX) → obstacle
    // rescue → PausedOnError. ONLY the movement is swapped DM2J rail → crane
    // rope: pre_cycle's main descent AND backup's "fresh wall spot" retreat both
    // use crane pay_out on that side. Arm cleaning sweep deferred (arm 未裝); the
    // v1 sweep-timing rail hooks are unused in v2.
    (void)after_feet_rail_hook; (void)before_feet_rail_hook;
    std::lock_guard<std::mutex> lk(motion_mtx_);
    abort_flag     = false;
    motion_active_ = true;

    const int step = step_cm_.load();
    std::cout << "[step_down] v2 begin, step=" << step << "cm ("
              << (right_first ? "right then left" : "left then right") << ")\n";

    // [2026-07-15 per user] Top-up runs in the background so the OTHER side can
    // start moving immediately instead of waiting on it (see feet_topup_unsealed_
    // call below). Joined (blocking) right before end-of-step realign, which
    // touches all 4 slaves and must not race a still-running top-up.
    std::future<void> topup_fut_right, topup_fut_left;

    // Run one side's descent through cycle_group_. `anchor_group` must stay
    // sealed (bears the machine); `move_group` releases, descends `step`, and
    // reseals with the full retry/backup engine. crane_word = pay_out_right|left.
    auto run_side = [&](const std::string& move_group, const std::string& anchor_group,
                        const std::vector<int>& move_slaves, int valve_ch,
                        const std::string& crane_word, const std::string& backup_word,
                        bool tgt_valid, double tgt_len, bool imu_level) -> std::string {
        // [2026-07-15 per user] Actual forward move (cm) this side just made in
        // pre_cycle's crane move — the vacuum-retry backup budget below must use
        // THIS (a magnitude, direction-independent), not the flat `step`, or a
        // leading side that only needed part of a step could retreat past where
        // it started. Defaults to `step` (safe fallback if pre_cycle never runs
        // a crane move, e.g. early abort before reaching it).
        double fwd_mv_cm = (double)step;

        // pre_cycle (once): verify anchor sealed, release moving side off the
        // wall, then crane moves this side to the common absolute target (方案B),
        // and (follower only) IMU fine-levels to the datum before re-extending.
        // Leaves cups retracted + valve OFF for cycle_group_'s first extend.
        auto pre_cycle = [this, move_group, anchor_group, move_slaves, valve_ch, crane_word, step, tgt_valid, tgt_len, imu_level, &fwd_mv_cm]() -> std::string {
            if (try_or_pause_([this]() { return ensure_all_zdt_stall_clear_(); },
                              "step_down_" + move_group + "_pre_stall_clear")) return "aborted";
            if (try_or_pause_([this, anchor_group, move_group]() -> bool {
                // [2026-07-08 per user] Anchor is "holding enough" if >=1 of its
                // 2 cups sealed. Refuse to release the moving side only when the
                // WHOLE anchor side is off.
                std::vector<int> fails;
                if (group_seal_ok_(anchor_group, fails)) {
                    if (!fails.empty()) {
                        std::string wmsg = "step_down_anchor_partial " + anchor_group + " unsealed=";
                        for (size_t i = 0; i < fails.size(); ++i) { if (i) wmsg += ","; wmsg += std::to_string(fails[i]); }
                        std::cout << "[safety] " << wmsg << " — anchor >=1 sealed, allow release of " << move_group << "\n";
                        evt_(wmsg);
                    }
                    return false;
                }
                std::string msg = "step_down_anchor_unsealed " + anchor_group + "=";
                for (size_t i = 0; i < fails.size(); ++i) { if (i) msg += ","; msg += std::to_string(fails[i]); }
                std::cout << "[safety] " << msg << " (whole side off) — REFUSE to release " << move_group << "\n";
                evt_(msg);
                return true;
            }, "step_down_" + move_group + "_anchor_check")) return "aborted";
            if (try_or_pause_([this, valve_ch]() { return pqw_set_relay_verified_(valve_ch, false); },
                              "step_down_" + move_group + "_valve_off")) return "aborted";
            if (try_or_pause_([this, move_slaves]() { return vacuum_wait_release_(move_slaves, VACUUM_RELEASE_WAIT_MS); },
                              "step_down_" + move_group + "_vacuum_release")) return "aborted";
            clear_other_group_stalls_(move_group);
            if (try_or_pause_([this, move_slaves]() { return pusher_two_stage_retract_(move_slaves); },
                              "step_down_" + move_group + "_pusher_retract")) return "aborted";
            {
                // Both sides move to the common absolute target locked at step
                // start (方案B): lagging side moves exactly `step`, leading side
                // gives way (≤step) → no side ever swings 2×step to catch up.
                int to = 0;
                const std::string cs = crane_abs_target_cmd_(move_group, crane_word, step, tgt_valid, tgt_len, to, fwd_mv_cm);
                if (!cs.empty()) {
                    std::cout << "[step_down] crane " << cs << "\n";
                    if (try_or_pause_([this, cs, to]() { return crane_cmd_(cs, to).rfind("OK", 0) != 0; },
                                      "step_down_" + move_group + "_crane_move")) return "aborted";
                }
            }
            // [策略1] Follower only: IMU fine-level to the datum (first side already
            // resealed) before re-extending cups — long-rope meter may be off, roll
            // is ground truth. Non-fatal / best-effort.
            if (imu_level) follower_imu_level_(move_group);
            return "";
        };
        // Vacuum-retry / rescue backup: cycle_group_ has already released +
        // retracted this side; retreat it back TOWARD the original (pre-step)
        // position by moving the crane the OPPOSITE way of the step (backup_word,
        // = retract_* for step_down) so it reseals on a fresh wall spot BEHIND the
        // failed one. Bounded to the original position — this side never retreats
        // past where it started this step (cumulative retreat <= fwd_mv_cm, the
        // ACTUAL forward move pre_cycle just made — NOT the flat `step`; a
        // leading side that only needed part of a step must not retreat past
        // where it stood before this step). The final hop is clamped to land
        // exactly on the origin. dry_run: feasibility (room left?) only. v1
        // enforced the same bound via the rail's [0,step] range; v2 has no rail
        // so we track it explicitly here.
        double cumulative_backup_cm = 0.0;   // this side's total retreat so far (vacuum + rescue share it)
        auto backup_cm = [this, move_group, backup_word, &fwd_mv_cm, &cumulative_backup_cm]
                         (double cm, const char* tag, bool dry_run) -> std::string {
            const double remaining = fwd_mv_cm - cumulative_backup_cm;
            if (remaining <= 0.5) {
                if (!dry_run)
                    std::cout << "  [retry " << move_group << tag << "] retreated "
                              << cumulative_backup_cm << "/" << fwd_mv_cm
                              << "cm — back at original position, no more backup room\n";
                return std::string(move_group) + "_backup_at_origin" + tag;
            }
            const double mv_cm = (cm < remaining) ? cm : remaining;   // clamp final hop onto origin
            if (dry_run) return "";
            const int mv = (int)std::lround(mv_cm);
            if (mv <= 0) return "";
            std::ostringstream oss; oss << backup_word << " " << mv;
            const std::string cs = oss.str();
            const int to = crane_motion_timeout_sec_(mv);
            std::cout << "  [retry " << move_group << tag << "] crane " << cs
                      << " (retreat toward origin " << (cumulative_backup_cm + mv_cm)
                      << "/" << fwd_mv_cm << "cm)\n";
            if (try_or_pause_([this, cs, to]() { return crane_cmd_(cs, to).rfind("OK", 0) != 0; },
                              std::string("step_down_") + move_group + "_backup" + tag)) return "aborted";
            cumulative_backup_cm += mv_cm;
            return "";
        };
        // VACUUM_BACKUP_CM is a settings_.<...>.load() macro (non-static member →
        // needs `this`); resolve it here (run_side's [&] holds this) so the inner
        // lambdas capture plain doubles and don't touch settings_ without `this`.
        const double vac_backup_cm    = (settings_.vacuum_backup_cm.load());
        const double rescue_backup_cm = OBSTACLE_RESCUE_BACKUP_CM;
        auto backup = [backup_cm, vac_backup_cm]   (bool dry_run) { return backup_cm(vac_backup_cm,    "",        dry_run); };
        auto rescue = [backup_cm, rescue_backup_cm](bool dry_run) { return backup_cm(rescue_backup_cm, "_rescue", dry_run); };

        int rc = 0, sc = 0;
        std::string cerr = cycle_group_(move_group, pre_cycle, backup, rescue, rc, sc);
        // [2026-07-08 per user] On success (>=1 cup sealed via stop_on_first_seal),
        // best-effort top-up this side's remaining cup(s) before it becomes the
        // next side's anchor. Non-fatal, valve stays ON, full obstacle/wall
        // detection preserved. Skip if aborting.
        // [2026-07-15 per user] Launched in the background (not awaited here) so
        // the caller can move to the OTHER side immediately; zdt_bus_mtx_ (in
        // pusher_extend_with_disable_seal_) keeps it from colliding with that
        // side's own pusher moves on the shared cli_20_ bus. Joined before
        // end-of-step realign below.
        if (cerr.empty() && !check_abort_()) {
            std::future<void> fut = std::async(std::launch::async,
                [this, move_group]() { feet_topup_unsealed_(move_group); });
            if (move_group == "right") topup_fut_right = std::move(fut);
            else                       topup_fut_left  = std::move(fut);
        }
        return cerr;
    };

    // Pre-flight: clear stall flags + confirm all four cups sealed.
    if (try_or_pause_([this]() { return ensure_all_zdt_stall_clear_(); },
                      "step_down_pre_stall_clear")) { motion_active_ = false; return "ERR aborted\n"; }
    if (try_or_pause_([this]() -> bool {
        // [2026-07-08 per user] Start gate: require EACH side to have >=1 cup
        // sealed (was: all 4). Preserves "every side has an anchor" (machine
        // can't drop) while allowing a side to run one-cup-only. Refuse only if
        // a WHOLE side is off.
        std::vector<int> rfail, lfail;
        const bool right_ok = group_seal_ok_("right", rfail);
        const bool left_ok  = group_seal_ok_("left",  lfail);
        if (right_ok && left_ok) {
            if (!rfail.empty() || !lfail.empty()) {
                std::string wmsg = "step_down_pre_partial unsealed=";
                for (int s : rfail) wmsg += std::to_string(s) + ",";
                for (int s : lfail) wmsg += std::to_string(s) + ",";
                std::cout << "[safety] " << wmsg << " — each side >=1 sealed, allow start\n";
                evt_(wmsg);
            }
            return false;
        }
        std::string msg = "step_down_pre_side_off";
        if (!right_ok) msg += " right=ALL";
        if (!left_ok)  msg += " left=ALL";
        std::cout << "[safety] " << msg << " — a whole side is off, REFUSE to start step\n";
        evt_(msg);
        return true;
    }, "step_down_pre_all_sealed")) { motion_active_ = false; return "ERR aborted\n"; }

    // [方案B 2026-07-08 per user] Pre-step: confirm BOTH meters now, lock a common
    // absolute target so a failed reseal last step can't make this step over-travel.
    // Descend one step from the LAGGING (shorter rope) side: target = min(L,R)+step.
    // → lagging side moves exactly step; leading side gives way (≤step). Both end
    // level. Crane read fail → tgt_valid=false → each side falls back to fixed step.
    double tgt_len = 0.0; bool tgt_valid = false;
    {
        double L = 0, R = 0;
        if (!read_crane_meters_(L, R)) {
            tgt_len = std::min(L, R) + (double)step;
            tgt_valid = true;
            std::cout << "[step_down] pre-step meters L=" << L << " R=" << R
                      << " gap=" << std::fabs(L - R) << "cm → target_len=" << tgt_len << "cm\n";
        } else {
            std::cout << "[step_down] pre-step meter read fail — both sides fixed step " << step << "cm\n";
            evt_("step_down_prestep_meter_fail");
        }
    }

    // Right half (left anchors), then left half (right anchors). Both target the
    // same tgt_len; crane_word = descent (pay_out) is only the fixed-step fallback
    // direction. backup_word = retreat toward origin (retract).
    // First side = datum (方案B meter, imu_level=false); second side = follower
    // (IMU fine-levels to it, imu_level=true). right_first alternates per step for
    // multi-step runs; single step = right first.
    auto run_right = [&](bool follower) {
        return run_side("right", "left", {ZDT_RF1, ZDT_RF2}, CH_VALVE_RIGHT, "pay_out_right", "retract_right", tgt_valid, tgt_len, /*imu_level=*/follower);
    };
    auto run_left = [&](bool follower) {
        return run_side("left", "right", {ZDT_LF1, ZDT_LF2}, CH_VALVE_LEFT, "pay_out_left", "retract_left", tgt_valid, tgt_len, /*imu_level=*/follower);
    };
    std::string err;
    err = right_first ? run_right(/*follower=*/false) : run_left(/*follower=*/false);
    if (!err.empty()) { motion_active_ = false; return "ERR " + err + "\n"; }
    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    err = right_first ? run_left(/*follower=*/true) : run_right(/*follower=*/true);
    if (!err.empty()) { motion_active_ = false; return "ERR " + err + "\n"; }
    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    // Both sides descended the same step_cm → nominally level.
    // [2026-07-15] Join any still-running background top-up before realign
    // touches all 4 slaves — must not race a topup still pushing one of them.
    if (topup_fut_right.valid()) topup_fut_right.get();
    if (topup_fut_left.valid())  topup_fut_left.get();

    // End-of-step realign: each side reseals a bit further out over steps (drift
    // snowball). Pull all 4 cups back to preset while SEALED (no rope) — but only
    // here, where both sides are anchored (mid-step one side is released, so realign
    // can't run then). Threshold-gated: only moves once drift passes
    // REALIGN_THRESHOLD_CM/MEAN, so most step-ends are a cheap no-op. NON-FATAL:
    // realign keeps cups sealed, so a failure leaves the machine anchored — log it,
    // the descent already succeeded, drift retries next step-end.
    {
        std::string rerr = do_feet_realign_(/*apply_threshold=*/true, /*caller_holds_lock=*/true);
        if (!rerr.empty()) {
            std::cout << "[step_down] end-of-step realign FAIL (non-fatal): " << rerr;
            evt_("step_realign_fail step_down " + rerr);
        }
    }
    // TODO v2: optional IMU roll + left/right meter-length tolerance check here.

    // [2026-07-24 per user] Arm now physically installed — re-enable the
    // cleaning sweep chained after this step. motion_active_ stays true through
    // the sweep itself (do_arm_clean_sweep_ hammers cli_22_ too — see
    // cmd_arm_clean_sweep's comment on why pressure_poll_loop_ must skip JC100
    // reads during it) and is only cleared after it returns.
    if (!skip_cleaning_sweep) {
        std::cout << "[step_down] start cleaning sweep (wall_mm=" << (settings_.arm_clean_wall_mm.load())
                  << " rounds=" << ARM_CLEAN_ROUNDS << ")\n";
        std::string clean_reply = do_arm_clean_sweep_((settings_.arm_clean_wall_mm.load()), ARM_CLEAN_ROUNDS);
        motion_active_ = false;
        if (clean_reply.rfind("OK", 0) != 0) {
            std::cout << "[step_down] cleaning sweep FAIL: " << clean_reply;
            return clean_reply;
        }
    } else {
        motion_active_ = false;
    }
    return "OK step_done\n";
}


// StepInProgressGuard 已上移至 cmd_attach 上方（2026-06-08 修 compile error）。
// 同一個定義被 cmd_attach / cmd_step_down / cmd_step_up 共用。

std::string WashRobot::cmd_step_down(int cm) {
    StepInProgressGuard _sip_guard{step_in_progress_};
    State cur = state_.load();
    // DISABLE STATUS CHECK
    //if (cur != State::Attached) return state_violation_(cur);
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > (settings_.step_cm_max.load())) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << (settings_.step_cm_max.load()) << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[step_down] start → Running (step=" << step_cm_.load() << " cm)\n";
    set_state_(State::Running);

    // Single step → lead with the chosen first-step side.
    std::string r = do_step_down_(false, {}, {}, first_step_right_.load());
    if (r.rfind("OK", 0) != 0) {
        std::cout << "[step_down] FAIL: " << r;
        set_state_(State::Error);
        return r;
    }
    std::cout << "[step_down] " << r;
    set_state_(State::Attached);

    // [2026-05-22] End-of-step realign DISABLED (per user). The mid-step
    // in-window feet realign + the step's own body cycle already keep the cups
    // aligned, so drift at end-of-step is ~0 and this full realign is a no-op
    // pass. Commented (not deleted) for easy revert if bench shows drift
    // accumulating across steps.
    // // E: realign trigger — if cup drift exceeds REALIGN_THRESHOLD_CM,
    // // retract all 9 cups back to preset.
    // {
    //     std::string realign_err = do_feet_realign_(/*force=*/false);
    //     if (!realign_err.empty()) {
    //         std::cout << "[step_down] realign FAIL (non-fatal): " << realign_err;
    //     }
    // }
    // [arm rope protect TEMP 2026-05-21] end-of-step PARK — realign Phase 5 pays
    // out small amount to restore tension, leaving arm at CENTER. Net rope
    // motion ≈ 0 (Phase 1 retract + Phase 5 pay_out cancel) so PARK path safe.
    ensure_arm_parked_after_rope_("step_down_end_realign");
    return r;
}

// step_down + 連續 cleaning sweep（並行）。對稱 cmd_step_up_with_sweep。
// 主 thread 跑 step_down（skip 末段 sweep），背景 thread 連續跑 LEFT+RIGHT 輪洗到
// step_down 結束。step 結束後等當前輪跑完才返回。
std::string WashRobot::cmd_step_down_with_sweep(int cm) {
    StepInProgressGuard _sip_guard{step_in_progress_};
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > (settings_.step_cm_max.load())) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << (settings_.step_cm_max.load()) << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[step_down+sweep] start → Running (step=" << step_cm_.load() << " cm)\n";
    set_state_(State::Running);
    // [2026-05-29] Reset arm sweep obstacle/skip flags — single-step = fresh scope.
    arm_sweep_obstacle_pending_.store(false);
    arm_sweep_skip_rest_of_run_.store(false);

    std::atomic<bool> sweep_keep_going{true};

    auto fut_sweep = std::async(std::launch::async, [this, &sweep_keep_going]() -> std::string {
        return do_arm_clean_sweep_continuous_((settings_.arm_clean_wall_mm.load()), sweep_keep_going);
    });

    struct SweepJoin {
        std::atomic<bool>& flag;
        std::future<std::string>& f;
        ~SweepJoin() {
            flag.store(false);
            if (f.valid()) f.wait();
        }
    } _sweep_guard{sweep_keep_going, fut_sweep};

    std::string r = do_step_down_(/*skip_cleaning_sweep=*/true);

    sweep_keep_going.store(false);
    std::cout << "[step_down+sweep] step_down done, waiting for current sweep round to finish...\n";
    std::string sweep_r = fut_sweep.get();
    std::cout << "[step_down+sweep] sweep result: " << sweep_r;
    // [2026-05-29] Post-sweep obstacle handler — slide stuck mid-sweep, ask user
    if (handle_post_sweep_obstacle_("step_down_with_sweep")) {
        set_state_(State::Error);
        return "ERR aborted_arm_obstacle\n";
    }

    if (r.rfind("OK", 0) != 0) {
        std::cout << "[step_down+sweep] FAIL: " << r;
        set_state_(State::Error);
        return r;
    }
    std::cout << "[step_down+sweep] " << r;
    set_state_(State::Attached);

    if (sweep_r.rfind("OK", 0) != 0) {
        std::cout << "[step_down+sweep] sweep FAIL (non-fatal): " << sweep_r;
    }

    // [2026-05-22] End-of-step realign DISABLED (per user — see cmd_step_down).
    // // E: realign trigger（同 cmd_step_down）
    // {
    //     std::string realign_err = do_feet_realign_(/*force=*/false);
    //     if (!realign_err.empty()) {
    //         std::cout << "[step_down+sweep] realign FAIL (non-fatal): " << realign_err;
    //     }
    // }
    // [arm rope protect TEMP 2026-05-21] end-of-step PARK
    ensure_arm_parked_after_rope_("step_down_with_sweep_end_realign");
    return r;
}

// Mirror of do_step_down_ for ascending one step.
//   Phase A (feet): rail 0 → +STEP_CM  (feet climbs up, body anchored on wall)
//   Phase B (body): rail +STEP_CM → 0  (body climbs up to feet level)
// DM2J rail still goes 0 → +step → 0, only the feet/body order is swapped.
// Crane (2026-05-19a): NO pre-climb pay_out. Phase B moves the rail first — the
// body climbing UP toward the crane slacks the rope on its own — then the crane
// retracts exactly that slack (= rail_delta, the actual body climb). Net retract
// per step = body climb. Feet phase has no crane motion (body anchored).
std::string WashRobot::do_step_up_(bool skip_cleaning_sweep,
                                    std::function<void()> after_feet_rail_hook,
                                    std::function<void()> before_feet_rail_hook,
                                    bool right_first) {
    // 🔴🔴 [2026-08-31] 停用：本函式假設「每側有獨立的真空閥」，而硬體沒有。
    // 真空幫浦一顆繼電器控 4 顆吸盤，三口二位閥也是一顆繼電器控 4 顆（per user）。
    // CH_VALVE_LEFT == CH_VALVE_RIGHT == 1，group_valve_ch_() 兩個 group 都回 1。
    // → pre_cycle 先用 group_seal_ok_(anchor_group) 確認錨定側吸牢，
    //   下一行 pqw_set_relay_verified_(valve_ch, false) 關的卻是**唯一那顆閥**
    //   → 剛驗證過的錨定側跟著失去真空，而機器正吊在玻璃上。
    //   relay 寫入會 verify 成功、log 一切正常。
    // 而且現行操作模式根本沒有「交替」：移動＝吊機收放繩，行進間靠風扇（QX_DO24 PWM
    // 5-10%）把本體壓在玻璃上，吸盤只在定點當錨 —— 那正是 do_step_sync_ 的流程。
    // 📌 保留原碼未動，等改寫成單閥架構（或正式移除）時再處理。走 *_sync 版本。
    return "ERR alt_gait_disabled_single_valve (use *_sync; see work_log 2026-08-31)\n";

    // [v2 2026-07-07] Ascend one step — mirror of do_step_down_ with the crane
    // RETRACTING each side's rope by `step_cm` instead of paying out. Same
    // cycle_group_ retry/backup engine (吸不好重吸); only the movement is crane
    // rope (retract). Right side first, left second; non-moving side anchors.
    (void)after_feet_rail_hook; (void)before_feet_rail_hook;
    std::lock_guard<std::mutex> lk(motion_mtx_);
    abort_flag     = false;
    motion_active_ = true;

    const int step = step_cm_.load();
    std::cout << "[step_up] v2 begin, step=" << step << "cm ("
              << (right_first ? "right then left" : "left then right") << ")\n";

    // [2026-07-15 per user] Top-up runs in the background so the OTHER side can
    // start moving immediately instead of waiting on it — see do_step_down_ for
    // full rationale. Joined before end-of-step realign below.
    std::future<void> topup_fut_right, topup_fut_left;

    auto run_side = [&](const std::string& move_group, const std::string& anchor_group,
                        const std::vector<int>& move_slaves, int valve_ch,
                        const std::string& crane_word, const std::string& backup_word,
                        bool tgt_valid, double tgt_len, bool imu_level) -> std::string {
        // [2026-07-15 per user] See do_step_down_ for full rationale — the
        // vacuum-retry backup budget must use the ACTUAL forward move (a
        // magnitude), not the flat `step`, or it can retreat past this side's
        // pre-step position.
        double fwd_mv_cm = (double)step;

        auto pre_cycle = [this, move_group, anchor_group, move_slaves, valve_ch, crane_word, step, tgt_valid, tgt_len, imu_level, &fwd_mv_cm]() -> std::string {
            if (try_or_pause_([this]() { return ensure_all_zdt_stall_clear_(); },
                              "step_up_" + move_group + "_pre_stall_clear")) return "aborted";
            if (try_or_pause_([this, anchor_group, move_group]() -> bool {
                // [2026-07-08 per user] Anchor is "holding enough" if >=1 of its
                // 2 cups sealed. Refuse only when the WHOLE anchor side is off.
                std::vector<int> fails;
                if (group_seal_ok_(anchor_group, fails)) {
                    if (!fails.empty()) {
                        std::string wmsg = "step_up_anchor_partial " + anchor_group + " unsealed=";
                        for (size_t i = 0; i < fails.size(); ++i) { if (i) wmsg += ","; wmsg += std::to_string(fails[i]); }
                        std::cout << "[safety] " << wmsg << " — anchor >=1 sealed, allow release of " << move_group << "\n";
                        evt_(wmsg);
                    }
                    return false;
                }
                std::string msg = "step_up_anchor_unsealed " + anchor_group + "=";
                for (size_t i = 0; i < fails.size(); ++i) { if (i) msg += ","; msg += std::to_string(fails[i]); }
                std::cout << "[safety] " << msg << " (whole side off) — REFUSE to release " << move_group << "\n";
                evt_(msg);
                return true;
            }, "step_up_" + move_group + "_anchor_check")) return "aborted";
            if (try_or_pause_([this, valve_ch]() { return pqw_set_relay_verified_(valve_ch, false); },
                              "step_up_" + move_group + "_valve_off")) return "aborted";
            if (try_or_pause_([this, move_slaves]() { return vacuum_wait_release_(move_slaves, VACUUM_RELEASE_WAIT_MS); },
                              "step_up_" + move_group + "_vacuum_release")) return "aborted";
            clear_other_group_stalls_(move_group);
            if (try_or_pause_([this, move_slaves]() { return pusher_two_stage_retract_(move_slaves); },
                              "step_up_" + move_group + "_pusher_retract")) return "aborted";
            {
                // Both sides move to the common absolute target locked at step
                // start (方案B): lagging side moves exactly `step`, leading side
                // gives way (≤step) → no side ever swings 2×step to catch up.
                int to = 0;
                const std::string cs = crane_abs_target_cmd_(move_group, crane_word, step, tgt_valid, tgt_len, to, fwd_mv_cm);
                if (!cs.empty()) {
                    std::cout << "[step_up] crane " << cs << "\n";
                    if (try_or_pause_([this, cs, to]() { return crane_cmd_(cs, to).rfind("OK", 0) != 0; },
                                      "step_up_" + move_group + "_crane_move")) return "aborted";
                }
            }
            // [策略1] Follower only: IMU fine-level to the datum before re-extending.
            if (imu_level) follower_imu_level_(move_group);
            return "";
        };
        // Retreat back TOWARD the original (pre-step) position by moving the crane
        // the OPPOSITE way of the step. For step_up the step retracts (ascends), so
        // backup_word = pay_out_* → the failed side is lowered back down to a fresh
        // wall spot below the failed one (per user 2026-07-08). Bounded to the
        // original position: cumulative retreat <= fwd_mv_cm (the ACTUAL forward
        // move pre_cycle just made, not the flat `step` — see do_step_down_),
        // final hop clamped onto it.
        double cumulative_backup_cm = 0.0;   // this side's total retreat so far (vacuum + rescue share it)
        auto backup_cm = [this, move_group, backup_word, &fwd_mv_cm, &cumulative_backup_cm]
                         (double cm, const char* tag, bool dry_run) -> std::string {
            const double remaining = fwd_mv_cm - cumulative_backup_cm;
            if (remaining <= 0.5) {
                if (!dry_run)
                    std::cout << "  [retry " << move_group << tag << "] retreated "
                              << cumulative_backup_cm << "/" << fwd_mv_cm
                              << "cm — back at original position, no more backup room\n";
                return std::string(move_group) + "_backup_at_origin" + tag;
            }
            const double mv_cm = (cm < remaining) ? cm : remaining;   // clamp final hop onto origin
            if (dry_run) return "";
            const int mv = (int)std::lround(mv_cm);
            if (mv <= 0) return "";
            std::ostringstream oss; oss << backup_word << " " << mv;
            const std::string cs = oss.str();
            const int to = crane_motion_timeout_sec_(mv);
            std::cout << "  [retry " << move_group << tag << "] crane " << cs
                      << " (retreat toward origin " << (cumulative_backup_cm + mv_cm)
                      << "/" << fwd_mv_cm << "cm)\n";
            if (try_or_pause_([this, cs, to]() { return crane_cmd_(cs, to).rfind("OK", 0) != 0; },
                              std::string("step_up_") + move_group + "_backup" + tag)) return "aborted";
            cumulative_backup_cm += mv_cm;
            return "";
        };
        // VACUUM_BACKUP_CM is a settings_.<...>.load() macro (non-static member →
        // needs `this`); resolve it here (run_side's [&] holds this) so the inner
        // lambdas capture plain doubles and don't touch settings_ without `this`.
        const double vac_backup_cm    = (settings_.vacuum_backup_cm.load());
        const double rescue_backup_cm = OBSTACLE_RESCUE_BACKUP_CM;
        auto backup = [backup_cm, vac_backup_cm]   (bool dry_run) { return backup_cm(vac_backup_cm,    "",        dry_run); };
        auto rescue = [backup_cm, rescue_backup_cm](bool dry_run) { return backup_cm(rescue_backup_cm, "_rescue", dry_run); };

        int rc = 0, sc = 0;
        std::string cerr = cycle_group_(move_group, pre_cycle, backup, rescue, rc, sc);
        // [2026-07-08 per user] Best-effort top-up this side's remaining cup(s)
        // after >=1 sealed — see do_step_down_ for rationale. Non-fatal.
        // [2026-07-15 per user] Launched in the background — see do_step_down_
        // for full rationale (zdt_bus_mtx_ serializes it against the other
        // side's pusher moves; joined before end-of-step realign).
        if (cerr.empty() && !check_abort_()) {
            std::future<void> fut = std::async(std::launch::async,
                [this, move_group]() { feet_topup_unsealed_(move_group); });
            if (move_group == "right") topup_fut_right = std::move(fut);
            else                       topup_fut_left  = std::move(fut);
        }
        return cerr;
    };

    if (try_or_pause_([this]() { return ensure_all_zdt_stall_clear_(); },
                      "step_up_pre_stall_clear")) { motion_active_ = false; return "ERR aborted\n"; }
    if (try_or_pause_([this]() -> bool {
        // [2026-07-08 per user] Start gate: require EACH side to have >=1 cup
        // sealed (was: all 4). See do_step_down_ for rationale. Refuse only if a
        // WHOLE side is off.
        std::vector<int> rfail, lfail;
        const bool right_ok = group_seal_ok_("right", rfail);
        const bool left_ok  = group_seal_ok_("left",  lfail);
        if (right_ok && left_ok) {
            if (!rfail.empty() || !lfail.empty()) {
                std::string wmsg = "step_up_pre_partial unsealed=";
                for (int s : rfail) wmsg += std::to_string(s) + ",";
                for (int s : lfail) wmsg += std::to_string(s) + ",";
                std::cout << "[safety] " << wmsg << " — each side >=1 sealed, allow start\n";
                evt_(wmsg);
            }
            return false;
        }
        std::string msg = "step_up_pre_side_off";
        if (!right_ok) msg += " right=ALL";
        if (!left_ok)  msg += " left=ALL";
        std::cout << "[safety] " << msg << " — a whole side is off, REFUSE to start step\n";
        evt_(msg);
        return true;
    }, "step_up_pre_all_sealed")) { motion_active_ = false; return "ERR aborted\n"; }

    // [方案B 2026-07-08 per user] Pre-step: confirm BOTH meters now, lock a common
    // absolute target so a failed reseal last step can't make this step over-travel.
    // Ascend one step from the LAGGING (longer rope = retracted less) side:
    // target = max(L,R)-step. → lagging side retracts exactly step; leading side
    // gives way (≤step). Both end level. Crane read fail → fixed step fallback.
    double tgt_len = 0.0; bool tgt_valid = false;
    {
        double L = 0, R = 0;
        if (!read_crane_meters_(L, R)) {
            tgt_len = std::max(L, R) - (double)step;
            tgt_valid = true;
            std::cout << "[step_up] pre-step meters L=" << L << " R=" << R
                      << " gap=" << std::fabs(L - R) << "cm → target_len=" << tgt_len << "cm\n";
        } else {
            std::cout << "[step_up] pre-step meter read fail — both sides fixed step " << step << "cm\n";
            evt_("step_up_prestep_meter_fail");
        }
    }

    // crane_word = ascent (retract) is only the fixed-step fallback direction;
    // backup_word = retreat toward origin (pay_out down). Both sides target tgt_len.
    // First side = datum (方案B meter, imu_level=false); second side = follower
    // (IMU fine-levels to it). right_first alternates per step for multi-step runs.
    auto run_right = [&](bool follower) {
        return run_side("right", "left", {ZDT_RF1, ZDT_RF2}, CH_VALVE_RIGHT, "retract_right", "pay_out_right", tgt_valid, tgt_len, /*imu_level=*/follower);
    };
    auto run_left = [&](bool follower) {
        return run_side("left", "right", {ZDT_LF1, ZDT_LF2}, CH_VALVE_LEFT, "retract_left", "pay_out_left", tgt_valid, tgt_len, /*imu_level=*/follower);
    };
    std::string err;
    err = right_first ? run_right(/*follower=*/false) : run_left(/*follower=*/false);
    if (!err.empty()) { motion_active_ = false; return "ERR " + err + "\n"; }
    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    err = right_first ? run_left(/*follower=*/true) : run_right(/*follower=*/true);
    if (!err.empty()) { motion_active_ = false; return "ERR " + err + "\n"; }
    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    // [2026-07-15] Join any still-running background top-up before realign
    // touches all 4 slaves — see do_step_down_ for rationale.
    if (topup_fut_right.valid()) topup_fut_right.get();
    if (topup_fut_left.valid())  topup_fut_left.get();

    // End-of-step realign (both sides anchored) — pull all 4 cups back to preset
    // while SEALED, threshold-gated, no rope. NON-FATAL. See do_step_down_ for the
    // full rationale (same drift-snowball correction).
    {
        std::string rerr = do_feet_realign_(/*apply_threshold=*/true, /*caller_holds_lock=*/true);
        if (!rerr.empty()) {
            std::cout << "[step_up] end-of-step realign FAIL (non-fatal): " << rerr;
            evt_("step_realign_fail step_up " + rerr);
        }
    }

    // [2026-07-24 per user] Arm now physically installed — same wiring as
    // do_step_down_ (see its comment for why motion_active_ stays true through
    // the sweep call).
    if (!skip_cleaning_sweep) {
        std::cout << "[step_up] start cleaning sweep (wall_mm=" << (settings_.arm_clean_wall_mm.load())
                  << " rounds=" << ARM_CLEAN_ROUNDS << ")\n";
        std::string clean_reply = do_arm_clean_sweep_((settings_.arm_clean_wall_mm.load()), ARM_CLEAN_ROUNDS);
        motion_active_ = false;
        if (clean_reply.rfind("OK", 0) != 0) {
            std::cout << "[step_up] cleaning sweep FAIL: " << clean_reply;
            return clean_reply;
        }
    } else {
        motion_active_ = false;
    }
    return "OK step_up_done\n";
}

// [2026-07-13 per user] 跨障礙物 (cross-obstacle) — a step that stands the body
// OFF the wall to 2×preset leg length to clear a protruding obstacle (window
// frame etc.), crosses, then both sides realign back to normal preset.
//   up=false → descend (crane pay_out) ; up=true → ascend (crane retract)
// Uses step_cm_ (same as step). BOTH sides cross (two phases). Reseal keeps the
// SAME robustness as a normal step: cycle_group_ retry/backup + stop_on_first_seal
// (one cup sealed = proceed). Only difference from step:
//   Phase 1 (first side A): release+retract A → the still-sealed ANCHOR side B
//     extends to 2×preset (pushes body off the wall) → crane move A → A reseals at
//     2×preset (cycle_group_ with feet_target_override=2×).
//   Phase 2 (second side B): release+retract B (A already anchors at 2×) → crane
//     move B → B reseals at 2×preset. (No anchor re-extend — A already at 2×.)
//   Phase 3: do_feet_realign_(force) → all 4 cups retract to normal preset while
//     SEALED, leveled.
// 🔴🔴 [2026-08-31 更正] 這裡原本寫著：
//   「The anchor valve is NEVER toggled here, so the anchored cups keep holding
//     through the 2x stand-off (shared per-side valve stays ON).」
//   **那句是錯的,而且它正好寫在唯一會出事的那行上方。** 原文用了 "shared" ——
//   作者知道閥是共用的,卻推出相反的結論。pre_cycle 關的是「移動側」的 valve_ch,
//   而因為閥共用,那**就是**錨定側的閥 → 四顆一起失去真空。
//   本函式又是把身體撐離牆面到 2x 腳長的構型,最不穩定的時候發生。
// 🔴 現行操作模式下也不需要它：跨障礙就是一般移動 —— 4 顆輪子(有避震器)貼住玻璃、
//   開風扇、吊機放繩滑過去,沒有專屬動作(per user 2026-08-31)。
std::string WashRobot::do_cross_obstacle_(bool up) {
    // 🔴🔴 [2026-08-31] 停用：本函式假設「每側有獨立的真空閥」，而硬體沒有。
    // 真空幫浦一顆繼電器控 4 顆吸盤，三口二位閥也是一顆繼電器控 4 顆（per user）。
    // CH_VALVE_LEFT == CH_VALVE_RIGHT == 1，group_valve_ch_() 兩個 group 都回 1。
    // → pre_cycle 先用 group_seal_ok_(anchor_group) 確認錨定側吸牢，
    //   下一行 pqw_set_relay_verified_(valve_ch, false) 關的卻是**唯一那顆閥**
    //   → 剛驗證過的錨定側跟著失去真空，而機器正吊在玻璃上。
    //   relay 寫入會 verify 成功、log 一切正常。
    // 而且現行操作模式根本沒有「交替」：移動＝吊機收放繩，行進間靠風扇（QX_DO24 PWM
    // 5-10%）把本體壓在玻璃上，吸盤只在定點當錨 —— 那正是 do_step_sync_ 的流程。
    // 📌 保留原碼未動，等改寫成單閥架構（或正式移除）時再處理。走 *_sync 版本。
    return "ERR alt_gait_disabled_single_valve (use *_sync; see work_log 2026-08-31)\n";

    std::lock_guard<std::mutex> lk(motion_mtx_);
    abort_flag     = false;
    motion_active_ = true;

    const int step = step_cm_.load();
    const char* tag = up ? "cross_up" : "cross_down";
    const bool right_first = first_step_right_.load();
    std::cout << "[" << tag << "] v2 begin, step=" << step << "cm ("
              << (right_first ? "right then left" : "left then right")
              << "), stand-off 2×preset\n";

    // 2×preset per-slave target for the reseal (bypasses the snowball cap so the
    // cup can reach the stood-off wall).
    auto tgt2x = [this](int s) { return 2 * preset_extend_pulse_for_slave_(s); };

    auto run_side = [&](const std::string& move_group, const std::string& anchor_group,
                        const std::vector<int>& move_slaves, const std::vector<int>& anchor_slaves,
                        int valve_ch, const std::string& crane_word, const std::string& backup_word,
                        bool tgt_valid, double tgt_len, bool extend_anchor) -> std::string {
        // [2026-07-15 per user] See do_step_down_ for full rationale — the
        // vacuum-retry backup budget must use the ACTUAL forward move (a
        // magnitude), not the flat `step`.
        double fwd_mv_cm = (double)step;

        auto pre_cycle = [this, tag, move_group, anchor_group, move_slaves, anchor_slaves,
                          valve_ch, crane_word, step, tgt_valid, tgt_len, extend_anchor, tgt2x, &fwd_mv_cm]() -> std::string {
            if (try_or_pause_([this]() { return ensure_all_zdt_stall_clear_(); },
                              std::string(tag) + "_" + move_group + "_pre_stall_clear")) return "aborted";
            // Anchor must have >=1 cup sealed before releasing the moving side.
            if (try_or_pause_([this, anchor_group, move_group, tag]() -> bool {
                std::vector<int> fails;
                if (group_seal_ok_(anchor_group, fails)) {
                    if (!fails.empty()) {
                        std::string wmsg = std::string(tag) + "_anchor_partial " + anchor_group + " unsealed=";
                        for (size_t i = 0; i < fails.size(); ++i) { if (i) wmsg += ","; wmsg += std::to_string(fails[i]); }
                        std::cout << "[safety] " << wmsg << " — anchor >=1 sealed, allow release of " << move_group << "\n";
                        evt_(wmsg);
                    }
                    return false;
                }
                std::string msg = std::string(tag) + "_anchor_unsealed " + anchor_group + "=";
                for (size_t i = 0; i < fails.size(); ++i) { if (i) msg += ","; msg += std::to_string(fails[i]); }
                std::cout << "[safety] " << msg << " (whole side off) — REFUSE to release " << move_group << "\n";
                evt_(msg);
                return true;
            }, std::string(tag) + "_" + move_group + "_anchor_check")) return "aborted";
            if (try_or_pause_([this, valve_ch]() { return pqw_set_relay_verified_(valve_ch, false); },
                              std::string(tag) + "_" + move_group + "_valve_off")) return "aborted";
            if (try_or_pause_([this, move_slaves]() { return vacuum_wait_release_(move_slaves, VACUUM_RELEASE_WAIT_MS); },
                              std::string(tag) + "_" + move_group + "_vacuum_release")) return "aborted";
            clear_other_group_stalls_(move_group);
            // [Phase 1 only] Extend the still-sealed ANCHOR side to 2×preset to stand
            // the body off the wall (clear the obstacle). Valve stays ON — NEVER
            // toggled — so the anchor cups keep holding. [2026-07-14 per user] Run it
            // CONCURRENTLY with the moving side's two-stage retract to save time WITHOUT
            // losing the slow-peel: fire the anchor extend NON-BLOCKING with sync=0
            // (executes immediately, does NOT use the shared broadcast sync-trigger, so
            // it can't collide with the retract's sync triggers), then run the moving
            // side's FULL two-stage retract, then join the anchor extend.
            if (extend_anchor) {
                if (try_or_pause_([this, anchor_slaves, tgt2x]() -> bool {
                    std::cout << "[cross_obstacle] anchor stand-off (concurrent w/ retract): extend {";
                    for (size_t i = 0; i < anchor_slaves.size(); ++i) { if (i) std::cout << ","; std::cout << anchor_slaves[i]; }
                    std::cout << "} → 2×preset (sealed, valve untouched)\n";
                    bool bad = false;
                    for (int s : anchor_slaves) {
                        Z_(s).release_stall_flag();
                        // sync=0 → immediate execution, no trigger; moves concurrently
                        // with the two-stage retract fired below.
                        bad = Z_(s).motion_control_pos_mode_nowait(0, PUSHER_ACC, PUSHER_RPM, tgt2x(s),
                                                                   /*abs*/1, /*sync*/0, /*retry*/1) || bad;
                    }
                    return bad;
                }, std::string(tag) + "_anchor_standoff_2x_fire")) return "aborted";
            }
            // Moving side two-stage retract (slow-peel preserved) — overlaps the anchor
            // extend fired above.
            if (try_or_pause_([this, move_slaves]() { return pusher_two_stage_retract_(move_slaves); },
                              std::string(tag) + "_" + move_group + "_pusher_retract")) return "aborted";
            if (extend_anchor) {
                // Join the concurrent anchor extend (usually already done by now).
                if (try_or_pause_([this, anchor_slaves]() -> bool {
                    int stalled = -1;
                    if (zdt_wait_motion_done_many_(anchor_slaves, 8000, /*defer_stall=*/false, &stalled)) {
                        std::cout << "[cross_obstacle] anchor stand-off join FAIL"
                                  << (stalled >= 0 ? (" (stall slave " + std::to_string(stalled) + ")")
                                                   : std::string(" (timeout)")) << "\n";
                        return true;
                    }
                    return false;
                }, std::string(tag) + "_anchor_standoff_2x_join")) return "aborted";
            }
            {
                // Crane move to the common absolute target (方案B leveling).
                int to = 0;
                const std::string cs = crane_abs_target_cmd_(move_group, crane_word, step, tgt_valid, tgt_len, to, fwd_mv_cm);
                if (!cs.empty()) {
                    std::cout << "[" << tag << "] crane " << cs << "\n";
                    if (try_or_pause_([this, cs, to]() { return crane_cmd_(cs, to).rfind("OK", 0) != 0; },
                                      std::string(tag) + "_" + move_group + "_crane_move")) return "aborted";
                }
            }
            return "";
        };
        // Retreat toward origin on retry — bounded by the ACTUAL forward move
        // (fwd_mv_cm), not the flat `step` (see do_step_down_ for rationale).
        double cumulative_backup_cm = 0.0;
        auto backup_cm = [this, tag, move_group, backup_word, &fwd_mv_cm, &cumulative_backup_cm]
                         (double cm, const char* btag, bool dry_run) -> std::string {
            const double remaining = fwd_mv_cm - cumulative_backup_cm;
            if (remaining <= 0.5) {
                if (!dry_run)
                    std::cout << "  [retry " << move_group << btag << "] retreated "
                              << cumulative_backup_cm << "/" << fwd_mv_cm << "cm — at origin\n";
                return std::string(move_group) + "_backup_at_origin" + btag;
            }
            const double mv_cm = (cm < remaining) ? cm : remaining;
            if (dry_run) return "";
            const int mv = (int)std::lround(mv_cm);
            if (mv <= 0) return "";
            std::ostringstream oss; oss << backup_word << " " << mv;
            const std::string cs = oss.str();
            const int to = crane_motion_timeout_sec_(mv);
            std::cout << "  [retry " << move_group << btag << "] crane " << cs << "\n";
            if (try_or_pause_([this, cs, to]() { return crane_cmd_(cs, to).rfind("OK", 0) != 0; },
                              std::string(tag) + "_" + move_group + "_backup" + btag)) return "aborted";
            cumulative_backup_cm += mv_cm;
            return "";
        };
        const double vac_backup_cm    = (settings_.vacuum_backup_cm.load());
        const double rescue_backup_cm = OBSTACLE_RESCUE_BACKUP_CM;
        auto backup = [backup_cm, vac_backup_cm]   (bool dry_run) { return backup_cm(vac_backup_cm,    "",        dry_run); };
        auto rescue = [backup_cm, rescue_backup_cm](bool dry_run) { return backup_cm(rescue_backup_cm, "_rescue", dry_run); };

        int rc = 0, sc = 0;
        // Reseal at 2×preset (feet_target_override) — same robustness as step
        // (stop_on_first_seal is applied inside cycle_group_'s extend).
        return cycle_group_(move_group, pre_cycle, backup, rescue, rc, sc, tgt2x);
    };

    // Pre-flight: stall clear + each side >=1 sealed (same gate as step).
    if (try_or_pause_([this]() { return ensure_all_zdt_stall_clear_(); },
                      std::string(tag) + "_pre_stall_clear")) { motion_active_ = false; return "ERR aborted\n"; }
    if (try_or_pause_([this, tag]() -> bool {
        std::vector<int> rfail, lfail;
        const bool right_ok = group_seal_ok_("right", rfail);
        const bool left_ok  = group_seal_ok_("left",  lfail);
        if (right_ok && left_ok) return false;
        std::string msg = std::string(tag) + "_pre_side_off";
        if (!right_ok) msg += " right=ALL";
        if (!left_ok)  msg += " left=ALL";
        std::cout << "[safety] " << msg << " — a whole side is off, REFUSE to start\n";
        evt_(msg);
        return true;
    }, std::string(tag) + "_pre_all_sealed")) { motion_active_ = false; return "ERR aborted\n"; }

    // Common absolute target (方案B): down → min(L,R)+step, up → max(L,R)-step.
    double tgt_len = 0.0; bool tgt_valid = false;
    {
        double L = 0, R = 0;
        if (!read_crane_meters_(L, R)) {
            tgt_len = up ? (std::max(L, R) - (double)step) : (std::min(L, R) + (double)step);
            tgt_valid = true;
            std::cout << "[" << tag << "] pre-step meters L=" << L << " R=" << R
                      << " → target_len=" << tgt_len << "cm\n";
        } else {
            std::cout << "[" << tag << "] pre-step meter read fail — both sides fixed step\n";
            evt_(std::string(tag) + "_prestep_meter_fail");
        }
    }

    const std::string cw_r = up ? "retract_right" : "pay_out_right";
    const std::string bw_r = up ? "pay_out_right" : "retract_right";
    const std::string cw_l = up ? "retract_left"  : "pay_out_left";
    const std::string bw_l = up ? "pay_out_left"  : "retract_left";

    // Phase 1: first side crosses; its anchor (the other side) stands off to 2×.
    // Phase 2: second side crosses (anchor already at 2× → extend_anchor=false).
    auto run_right = [&](bool extend_anchor) {
        return run_side("right", "left", {ZDT_RF1, ZDT_RF2}, {ZDT_LF1, ZDT_LF2},
                        CH_VALVE_RIGHT, cw_r, bw_r, tgt_valid, tgt_len, extend_anchor);
    };
    auto run_left = [&](bool extend_anchor) {
        return run_side("left", "right", {ZDT_LF1, ZDT_LF2}, {ZDT_RF1, ZDT_RF2},
                        CH_VALVE_LEFT, cw_l, bw_l, tgt_valid, tgt_len, extend_anchor);
    };

    std::string err;
    err = right_first ? run_right(/*extend_anchor=*/true) : run_left(/*extend_anchor=*/true);
    if (!err.empty()) { motion_active_ = false; return "ERR " + err + "\n"; }
    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    err = right_first ? run_left(/*extend_anchor=*/false) : run_right(/*extend_anchor=*/false);
    if (!err.empty()) { motion_active_ = false; return "ERR " + err + "\n"; }
    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    // Phase 3: both sides sealed at 2× — realign FORCED (not threshold-gated) to
    // retract all 4 cups back to normal preset while SEALED, leveled.
    {
        std::string rerr = do_feet_realign_(/*apply_threshold=*/false, /*caller_holds_lock=*/true);
        if (!rerr.empty()) {
            std::cout << "[" << tag << "] final realign FAIL (non-fatal): " << rerr;
            evt_(std::string(tag) + "_realign_fail " + rerr);
        }
    }

    motion_active_ = false;
    return up ? "OK cross_obstacle_up_done\n" : "OK cross_obstacle_down_done\n";
}

// [2026-07-23 per user] Small 上滑台 (DM2J:14) rail-only sweep — runs
// CONCURRENTLY with do_step_sync_'s feet-extend (launched via std::async the
// moment feet extend starts, "一伸出腳就開始清潔"), not after. Same slot in
// the sequence the full arm-clean-sweep pipeline would occupy (see
// v2_app_redesign_plan.md §5.6).
// [2026-07-24 per user] 手臂已實機裝上 — 補回 arm_cmd_ DEPLOY + CH_BRUSH，跟
// rail 掃動同一段時間執行；PARK + 關刷收尾。水閥/水泵還沒接管路，不開（維持
// 註解狀態，跟 do_arm_sweep_ 一致）。DEPLOY 失敗（no reply / obstacle）非致命
// — 跳過開刷，照舊只做純 rail 掃動，不擋這一步。
// [2026-07-24 per user] 改成 LEFT/RIGHT 各 deploy 一次、各配一段滑台掃動：
//   DEPLOY LEFT → DM2J 到 -10cm → DEPLOY RIGHT (M1 先收回再重伸到 RIGHT slot，
//   cmd_deploy_sequence 本身的 Step1/2/3) → DM2J 回 0 → PARK。
// Uses arm_sweep_fire_nowait_ (PR_move_cm_nowait, write-only) instead of the
// blocking PR_move_cm do_arm_sweep_ uses — required now that this runs
// concurrently with feet-extend's own JC100 pressure polling on the SAME
// cli_22_ bus (blocking PR_move_cm internally status-polls cli_22_ too,
// which is exactly the contention arm_sweep_fire_nowait_ was built in
// 2026-05-26 to avoid — see that function's header comment). CH_BRUSH lives
// on the same cli_22_ PQW device but is a single FC05 write (like the rail's
// own writes), not a poll loop, so it doesn't reintroduce that contention.
// arm_sweep_fire_nowait_ internally waits out DM2J_ARM_STEP_SWEEP_EST_MS
// (via arm_monitor_during_sweep_) before returning, so by the time it returns
// the rail should already be at the target — safe to fire DEPLOY RIGHT right
// after the first arm_sweep_fire_nowait_ call.
// Non-fatal throughout: arm_sweep_fire_nowait_ itself never reports failure
// (fire-and-forget, retries cover a dropped Modbus write) — a rail issue
// here was never going to block the step even before this rewrite.
void WashRobot::do_step_sync_rail_sweep_(const char* tag, bool init_ok, bool force_enable) {
    // [2026-08-27 per user] 上滑台未裝好 → 整段清潔跳過（見 WASH_ROBOT.h 的
    // STEP_SYNC_ARM_CLEAN_ENABLED 說明）。這裡 early-return 而不是讓它跑下去，
    // 是因為 DM2J:14 沒回應時 arm_sweep_fire_nowait_ 會白等 EST_MS、每步多噴一
    // 串 writeMulti no response。force_enable 讓 bench 乾掃測試指令仍能跑。
    if (!STEP_SYNC_ARM_CLEAN_ENABLED && !force_enable) {
        std::cout << "[" << tag << "] arm clean DISABLED (上滑台未裝, "
                     "STEP_SYNC_ARM_CLEAN_ENABLED=false) — 跳過 rail sweep + DEPLOY + brush\n";
        return;
    }

    // [2026-07-24 per user] DEPLOY 前每次都真的重新 arm_cmd_("INIT")（M1/M2 全
    // 校正），不是 ensure_arm_ready_()。這覆蓋掉 2026-05-28 那次「INIT 只在系統
    // init 做一次、sweep 只用 ensure_arm_ready_() 重新 ENABLE」的設計——per user
    // 明確要求每次清洗前都重新校正，即使較慢（~10s）、中途撞到東西的話原點風險
    // 也拉高。
    // [2026-07-24 per user] INIT 本身已經搬到 do_step_sync_ 裡跟吊機放繩同時跑
    // （見該函式 fut_arm_init），這裡只收現成的結果，不再自己呼叫 arm_cmd_("INIT")。
    // INIT 失敗就跳過整段 DEPLOY，只做純 rail 掃動，不擋這一步。
    bool deployed = false;
    if (!init_ok) {
        std::cerr << "[" << tag << "] arm INIT failed — rail sweep only, no brush\n";
    } else {
        // [2026-08-26 per user] 滾筒側 LEFT → RIGHT、刮刀側 RIGHT → LEFT。
        // 工具頭實體左右對調了（同一批改動也把 main_api.h / WASH_ROBOT.h 的
        // TOOL_EXT_LEFT/RIGHT 幾何常數對調），滾筒現在裝在 RIGHT。
        // 流程順序「先濕刷、後乾刮」不變，變的只是各自對應哪一側工具頭；
        // 因此 CH_BRUSH 仍是第一段 ON、第二段 OFF，只有 DEPLOY 的方位字串換邊。
        // 變數改用語意命名（brush / squeegee）而非方位，之後再換邊就不會又看錯。
        std::ostringstream oss_brush;
        oss_brush << "DEPLOY " << DM2J_ARM_STEP_SWEEP_WALL_MM << " RIGHT";   // RIGHT = 滾筒側
        // [2026-08-28] 保留回覆內容 —— 原本只印 "failed"，查不出是逾時、M2 轉不到位
        // 還是 motor_api 根本沒回。
        // [2026-08-28 per user] 滾筒 (CH_BRUSH) 的開關窗口就是「DEPLOY RIGHT 到
        // DEPLOY LEFT」：這裡在送 DEPLOY RIGHT **之前**開（deploy 過程中就要轉，
        // 不是等回 OK 才開，避免手臂先壓上玻璃才起轉），一路轉過靜置 + 第一趟
        // 滑台掃動，直到下面換刮刀側、送 DEPLOY LEFT 之前才關。
        // DEPLOY RIGHT 失敗時**不**提早關 —— 關閉點只有 DEPLOY LEFT 那一個
        // （abort 收尾另有無條件關閉，見下）。
        //
        // [2026-08-28] 檢查回傳值並留 log（PQW driver 沿用本專案慣例 true=失敗）。
        // 先前這裡是裸呼叫，寫入失敗或打錯通道時 log 一個字都沒有 —— 2026-08-28
        // 「滾筒不轉」查了半天才發現是通道號錯（15 應為 5），就是因為沒有這行。
        // ⚠ PQW 韌體的 echo 檢查不可靠（只驗長度不驗內容），所以這裡回 OK 也不等於
        //   繼電器真的切了；它只能抓「完全沒回應／回覆太短」這類硬失敗。
        if (pqw_.controlRelay(CH_BRUSH, true))
            std::cerr << "[" << tag << "] 滾筒 relay CH" << CH_BRUSH
                      << " ON 寫入失敗（PQW 無回應）—— 滾筒可能不會轉\n";

        const std::string rep_brush = arm_cmd_(oss_brush.str(), 30);
        deployed = (rep_brush.rfind("OK", 0) == 0);
        if (deployed) {
            // [2026-07-24 per user] 滾筒 deploy 後靜置再讓 DM2J 滑台開始移動，給滾筒
            // 轉起來/貼牆穩定的時間，避免滑台一動滾筒還沒轉穩。
            // [2026-08-28 per user] 2500 → DM2J_ARM_DEPLOY_SETTLE_MS(3500)，並抽成常數
            // （原本兩處各自寫死，容易改一處漏一處）。
            sleep_ms_(DM2J_ARM_DEPLOY_SETTLE_MS);
        } else {
            // [2026-08-28 per user] 這裡刻意不關滾筒：開關窗口定義為 DEPLOY RIGHT
            // 到 DEPLOY LEFT，DEPLOY RIGHT 失敗不改變關閉點（下面換刮刀側時才關）。
            std::cerr << "[" << tag << "] arm deploy RIGHT (brush) failed — rail sweep continues"
                      << " (motor_api 回覆: " << rep_brush << ")\n";
        }
    }

    const bool rail_ok_out = arm_sweep_fire_nowait_(DM2J_ARM_STEP_SWEEP_CM,
                           DM2J_ARM_STEP_SWEEP_RPM, DM2J_ARM_STEP_SWEEP_ACC, DM2J_ARM_STEP_SWEEP_DEC,
                           DM2J_ARM_STEP_SWEEP_EST_MS);
    if (check_abort_()) {
        // [2026-08-28] 同下方 PARK 的理由：abort 收尾也改吃 init_ok。
        // CH_BRUSH 無條件關 —— 沒開過時關它是 no-op，開著沒關才是問題。
        pqw_.controlRelay(CH_BRUSH, false);
        if (init_ok) arm_cmd_("PARK", 30);
        return;
    }

    // [2026-08-28 per user] 換邊條件由 deployed 改為 init_ok。
    // user 回報「從頭到尾都是 DEPLOY RIGHT 沒換」：DEPLOY RIGHT 失敗時 deployed=false，
    // 這整段（含 DEPLOY LEFT）就被跳過，手臂停在原位、滑台空掃兩趟。
    // 但第一段失敗不代表第二段也會失敗，而且刮刀那一趟本來就是獨立的清洗動作——
    // 第一趟沒刷成，第二趟至少該刮。init_ok 才是「手臂通不通」的正確判準。
    if (init_ok) {
        // [2026-07-27 per user] 換成刮刀側的同時關掉滾筒馬達——刮刀是乾刮，
        // 滾筒(濕刷+CH_BRUSH)沒道理繼續轉。跟原本收尾才關（PARK 前）分開，提早到
        // 切換的當下就關，不要等到整段結束。
        // [2026-08-26 per user] 刮刀側 RIGHT → LEFT（見上方對調說明）。
        // [2026-08-28 per user] 這是滾筒唯一的正常關閉點（abort 收尾另有無條件關）：
        // 開在 DEPLOY RIGHT 之前、關在 DEPLOY LEFT 之前，中間全程轉。
        // [2026-08-28] 同 ON 那側：檢查回傳值。關不掉比開不起來嚴重（滾筒會一直
        // 轉到下一次有人關它為止），所以這裡用 ERR 等級的字眼。
        if (pqw_.controlRelay(CH_BRUSH, false))
            std::cerr << "[" << tag << "] ⚠ 滾筒 relay CH" << CH_BRUSH
                      << " OFF 寫入失敗（PQW 無回應）—— 滾筒可能仍在轉，請手動關\n";
        std::ostringstream oss_squeegee;
        oss_squeegee << "DEPLOY " << DM2J_ARM_STEP_SWEEP_WALL_MM << " LEFT";   // LEFT = 刮刀側
        if (arm_cmd_(oss_squeegee.str(), 30).rfind("OK", 0) != 0) {
            std::cerr << "[" << tag << "] arm deploy LEFT (squeegee) failed — continuing rail only\n";
        } else {
            // [2026-07-24 per user] 每次 deploy 後都先靜置再讓滑台移動。
            // [2026-08-28 per user] 同上，改吃 DM2J_ARM_DEPLOY_SETTLE_MS。
            sleep_ms_(DM2J_ARM_DEPLOY_SETTLE_MS);
        }
    }

    const bool rail_ok_back = arm_sweep_fire_nowait_(0.0,
                           DM2J_ARM_STEP_SWEEP_RPM, DM2J_ARM_STEP_SWEEP_ACC, DM2J_ARM_STEP_SWEEP_DEC,
                           DM2J_ARM_STEP_SWEEP_EST_MS);

    // [2026-08-28] PARK 條件由 deployed 改為 init_ok —— 這是安全收尾。
    // 若 DEPLOY 實際讓手臂動了、只是回覆逾時被判失敗，舊寫法就永遠不 PARK，
    // 手臂留在伸出狀態跟著機器繼續走步伐（刮玻璃／被扯壞）。PARK 對已經 PARK
    // 的手臂是冪等 no-op，多送一次無害；漏送才是實質危險。
    // 仍以 init_ok 為條件：手臂整個不通時 arm_cmd_ 會白等 30s timeout。
    if (init_ok) {
        arm_cmd_("PARK", 30);
    }
    // [2026-08-28] 訊息必須反映實際發生的事。原本無條件印 "rail sweep done"，
    // 於是上滑台三次寫入全滅（DM2J 掛在錯的 gateway）時 log 仍看起來一切正常。
    if (rail_ok_out || rail_ok_back) {
        std::cout << "[" << tag << "] rail sweep done (RIGHT/brush deploy -> " << DM2J_ARM_STEP_SWEEP_CM
                  << " -> LEFT/squeegee deploy -> 0 -> PARK)\n";
    } else {
        std::cerr << "[" << tag << "] ⚠ rail sweep 沒有實際發生 — DM2J:" << DM2J_ARM
                  << " 兩段掃動的寫入都失敗，上滑台完全沒動（手臂 DEPLOY/PARK 仍已執行）\n";
    }
}

// [2026-07-22 per user] Synchronized step gait — deliberately DIFFERENT safety
// shape from every other repeatable gait in this file (do_step_down_/up_,
// do_cross_obstacle_): those always keep >=1 side's cups sealed to the wall
// as a fall-arrest anchor throughout the move. Here ALL 4 cups release
// together and the crane rope is the SOLE support for the whole rope-move
// window — confirmed explicitly with the user (2026-07-21/22) as intentional
// for this gait, trading that anchor for a faster/simpler move (both ropes
// move together instead of the alternating measure-and-match inchworm walk).
//
// Sequence: release vacuum + 2-stage-retract all 4 cups together (mirrors
// cmd_return_home) → crane moves BOTH ropes simultaneously via bare
// pay_out/retract (crane-side dual_vfd_sync_start; NOT the per-side
// pay_out_left/pay_out_right do_step_down_/up_ use) → do_sync_imu_roll_correct_()
// → vacuum on + extend all 4 cups together (mirrors cmd_attach's "hanging on
// crane rope, no side needs to anchor" precedent) → do_step_sync_rail_sweep_()
// (2026-07-23: small DM2J:14 rail-only sweep, no arm/brush — see that
// function's header comment).
std::string WashRobot::do_step_sync_(bool up) {
    std::lock_guard<std::mutex> lk(motion_mtx_);
    abort_flag     = false;
    motion_active_ = true;
    const int cm = step_cm_.load();
    const char* tag = up ? "step_up_sync" : "step_down_sync";

    // [2026-08-28] 所有提早返回都經過這裡，所以順手把 PWM 占空比寫回 5%（=停止）。
    // 否則步伐在「收腳 -> 吊機移動」中間 abort/失敗時，輸出會停在 7% 一直開著，
    // 而接下來的動作（手動伸腳、救援收繩）不會經過步驟 3.5 那個關閉點。
    // 還沒開啟就走到這裡時寫 5% 是無害的 no-op（5% 本來就是停止值）。
    auto fail = [this, tag](const std::string& msg) -> std::string {
        if (pwm_set_duty_only_(PWM_STEP_OFF_DUTY_PCT, "step_abort_off"))
            std::cerr << "[" << tag << "] PWM 5% 寫入失敗（abort 收尾）— 輸出可能仍開著" << "\n";
        motion_active_ = false;
        return msg;
    };

    std::cout << "[" << tag << "] start cm=" << cm << "\n";
    evt_(std::string(tag) + "_start cm=" + std::to_string(cm));

    std::vector<int> all_slaves = group_slaves_("all");

    // 1. Release vacuum both channels, wait all 4 release, two-stage retract
    //    all 4 together (mirrors cmd_return_home's return-home teardown).
    // [2026-07-23 per user] Same feel as the "vacuum feet on/off" manual
    // button — one grouped call instead of two separately-paused try_or_pause_
    // calls. Both channels here don't need an anchor side, so there's nothing
    // to preserve by pausing/reporting them independently.
    if (try_or_pause_([this]() { return vacuum_valve_("feet", false); },
                       std::string(tag) + "_valve_off")) return fail("ERR aborted\n");
    if (check_abort_()) return fail("ERR aborted\n");

    if (try_or_pause_([this, &all_slaves]() { return vacuum_wait_release_(all_slaves, RETURN_VACUUM_RELEASE_MS); },
                       std::string(tag) + "_vacuum_release")) return fail("ERR aborted\n");
    if (check_abort_()) return fail("ERR aborted\n");

    // [2026-08-28 per user] PWM 占空比寫 7%。位置：4 顆都已經**解真空並確認釋放**
    // 之後、ZDT **收腳之前**。（沿革：最初放在解真空之前 → 08-28 per user 改到收腳
    // 之後 → 同日 per user 再往前挪到收腳之前，也就是現在這裡。腳已經不吸在牆上，
    // 但推桿還沒開始縮。）
    // 從這裡一路開著，涵蓋收腳 + 整段吊機移動 + IMU 校平，到定位後、伸腳之前才在
    // 步驟 3.5 寫回 5%（=關掉）。
    // 只寫占空比，頻率與控制字不動（理由見 pwm_set_duty_only_ 的註解）。
    // 失敗不擋步伐：沒開起來不會讓機器進入危險狀態。真正有安全意義的是「該關沒關」
    // —— 那一側在步驟 3.5 用 try_or_pause_ 擋住，不讓它帶著輸出去伸腳。
    if (pwm_set_duty_only_(PWM_STEP_MOVE_DUTY_PCT, "step_move_on"))
        std::cerr << "[" << tag << "] PWM " << PWM_STEP_MOVE_DUTY_PCT
                  << "% 寫入失敗 — 步伐照常繼續\n";

    // [2026-08-28 per user] 寫完 7% 靜置 PWM_STEP_ON_SETTLE_MS 再讓機構動。跟著
    // 寫入點一起搬過來 —— 這段等待的用意是「輸出起來/負載轉穩之前不要動」，所以
    // 貼著寫入走，現在擋在收腳之前（吊機移動更在其後，仍然等滿）。
    // 寫入失敗時也照等 —— 失敗可能只是回覆掉了、輸出其實已經起來，這 300ms 對
    // 步伐總時間無足輕重，不值得為它多分一條路徑。
    sleep_ms_(PWM_STEP_ON_SETTLE_MS);
    if (check_abort_()) return fail("ERR aborted\n");

    if (try_or_pause_([this, &all_slaves]() { return pusher_two_stage_retract_(all_slaves); },
                       std::string(tag) + "_pusher_retract")) return fail("ERR aborted\n");
    if (check_abort_()) return fail("ERR aborted\n");

    // [2026-07-24 per user] Launch arm INIT (M1/M2 full re-calibration, ~10s)
    // CONCURRENTLY with the crane rope move below — INIT only talks to
    // motor_api over localhost :9527 (M1/M2 joints), it doesn't touch the
    // crane/ZDT/PQW buses at all, so there's no resource conflict running it
    // while the rope pays out/retracts. Hides the ~10s INIT latency inside
    // the rope-move window instead of paying it serially later inside
    // do_step_sync_rail_sweep_. Result (+ arm_calibrated_ update) is joined
    // right before do_step_sync_rail_sweep_ is launched at step 5; RAII guard
    // below still waits it out on any earlier abort/fail return so INIT is
    // never left running unjoined in the background.
    // [2026-08-27 per user] 上滑台未裝 → 連 INIT 都不做（見 WASH_ROBOT.h 的
    // STEP_SYNC_ARM_CLEAN_ENABLED）。只關掃動不關 INIT 的話，每步仍會花 ~10s 讓
    // M1/M2 全校正、手臂實際動作，卻完全不清洗——所以兩處一起 gate。
    // 仍然建立一個 ready future 回傳 false，讓下面 fut_arm_init.get() /
    // ArmInitJoin 的結構完全不用改。
    std::future<bool> fut_arm_init = STEP_SYNC_ARM_CLEAN_ENABLED
        ? std::async(std::launch::async, [this, tag]() -> bool {
              const bool ok = (arm_cmd_("INIT", 60).rfind("OK", 0) == 0);
              arm_calibrated_.store(ok);
              if (!ok) std::cerr << "[" << tag << "] arm INIT failed (ran parallel w/ crane move)\n";
              return ok;
          })
        : std::async(std::launch::deferred, []() -> bool { return false; });
    struct ArmInitJoin {
        std::future<bool>& f;
        ~ArmInitJoin() { if (f.valid()) f.wait(); }
    } _arm_init_join{fut_arm_init};

    // 2. Crane moves BOTH ropes simultaneously by cm — bare pay_out/retract
    //    (crane-side dual_vfd_sync_start), not the per-side alternating
    //    pay_out_left/right sequence do_step_down_/up_ issues.
    {
        std::ostringstream oss;
        oss << (up ? "retract " : "pay_out ") << cm;
        const std::string cs = oss.str();
        const int to = crane_motion_timeout_sec_(cm);
        std::cout << "[" << tag << "] crane " << cs << "\n";
        if (try_or_pause_([this, cs, to]() { return crane_cmd_(cs, to).rfind("OK", 0) != 0; },
                           std::string(tag) + "_crane_move"))
            return fail("ERR aborted\n");
    }
    if (check_abort_()) return fail("ERR aborted\n");

    // [2026-08-28 per user] 吊機放/收繩結束後靜置 CRANE_MOVE_SETTLE_MS。
    // crane_cmd_ 回 OK 只代表吊機端的停止指令送完了，機體還在晃、鋼索還在彈；
    // 緊接著就讀 IMU / 動推桿並不理想。放在這裡（不是塞進 do_sync_imu_roll_correct_）
    // 是因為它屬於「吊機動作剛結束」的收尾，就算之後 IMU 校正被跳過也該等。
    sleep_ms_(CRANE_MOVE_SETTLE_MS);
    if (check_abort_()) return fail("ERR aborted\n");

    // 3. IMU differential roll correction — non-fatal, residual tilt just
    //    carries to next step if it can't converge.
    // [2026-08-04 per user] 恢復——2026-07-24 那次暫時註解掉是做實驗用，這次
    // 使用者確認實驗結束、要恢復正常水平微調。
    do_sync_imu_roll_correct_();
    if (check_abort_()) return fail("ERR aborted\n");

    // 3.5 [2026-08-28 per user] 到定位了 —— 伸腳「之前」先把 PWM 占空比寫回 5%
    //     (=停止)。per user：確定關掉之後才可以開始抽真空 + 伸出推桿，所以這裡用
    //     try_or_pause_ 而不是像步驟 1 開啟時那樣忽略失敗 —— 寫不進去就停下來讓
    //     使用者處理，不要在輸出還開著的狀態下伸腳。位置刻意排在步驟 4（抽真空）
    //     之前而不是跟它並排：順序本身就是這個需求的重點。
    if (try_or_pause_([this]() { return pwm_set_duty_only_(PWM_STEP_OFF_DUTY_PCT, "step_move_off"); },
                       std::string(tag) + "_pwm_off")) return fail("ERR aborted\n");
    if (check_abort_()) return fail("ERR aborted\n");

    // 4. Vacuum back on, both channels (same grouped call as step 1).
    if (try_or_pause_([this]() { return vacuum_valve_("feet", true); },
                       std::string(tag) + "_valve_on")) return fail("ERR aborted\n");
    if (check_abort_()) return fail("ERR aborted\n");

    // 5. Extend all 4 cups in ONE simultaneous call (one trigger_sync_move,
    //    matching cmd_attach's "hanging on crane rope, no anchor" all-4-
    //    together precedent — per user 2026-07-23, both foot groups must
    //    extend together, splitting into two sequential per-side calls is
    //    not acceptable), but each SIDE still stops pushing independently the
    //    moment it has >=1 real seal (the v2 "每側 >=1 顆即算好" bar,
    //    group_seal_ok_) instead of continuing to drive an already-anchored
    //    side's second cup deeper toward WEAK_SEAL/a wall hit for no safety
    //    benefit. This needs `pusher_extend_with_disable_seal_`'s new
    //    stop_group_ids (2026-07-23) — a plain stop_on_first_seal=true over
    //    all 4 at once would incorrectly let the FIRST cup sealing ANYWHERE
    //    (either side) stop the whole call.
    // [2026-07-23 per user] "一伸出腳就開始清潔" — launch the rail sweep the
    // moment feet-extend starts, not after. Runs on a background thread
    // concurrently with the extend below; joined (RAII, any exit path) once
    // this function returns. Uses arm_sweep_fire_nowait_ internally (see
    // do_step_sync_rail_sweep_ header comment) specifically so it doesn't
    // contend with this same extend's JC100 polling on cli_22_.
    // [2026-07-24 per user] arm INIT already ran in parallel with the crane
    // move (see fut_arm_init above) — join it here (should already be done
    // by now: crane move + IMU pass + vacuum-on all happened since it was
    // launched) and hand the result straight to do_step_sync_rail_sweep_
    // instead of it calling arm_cmd_("INIT") itself.
    const bool arm_init_ok = fut_arm_init.get();
    std::future<void> fut_rail_sweep = std::async(std::launch::async,
        [this, tag, arm_init_ok]() { do_step_sync_rail_sweep_(tag, arm_init_ok); });
    struct RailSweepJoin {
        std::future<void>& f;
        ~RailSweepJoin() { if (f.valid()) f.wait(); }
    } _rail_sweep_join{fut_rail_sweep};

    {
        std::vector<int> stop_group_ids(all_slaves.size(), 0);
        for (size_t i = 0; i < all_slaves.size(); ++i) {
            const int s = all_slaves[i];
            stop_group_ids[i] = (s == ZDT_RF1 || s == ZDT_RF2) ? 0 : 1;   // 0=right, 1=left
        }
        if (try_or_pause_([this, &all_slaves, &stop_group_ids]() {
                return smart_extend_subset_("all", all_slaves, /*stop_on_first_seal=*/true, &stop_group_ids);
            }, std::string(tag) + "_extend_all")) return fail("ERR aborted\n");
    }
    if (check_abort_()) return fail("ERR aborted\n");

    // Per-cup vacuum re-check + retry any unsealed cup, same fallback cmd_attach uses.
    auto fails = vacuum_check_("all");
    if (!fails.empty()) {
        std::cout << "[" << tag << "] cups not sealed after extend:";
        for (int s : fails) std::cout << " " << s;
        std::cout << " — checking per-side whether retry is needed\n";

        // [2026-08-28 per user] ⚠ 重試之前必須先等清洗掃動整段結束（含手臂 PARK）。
        //
        // rail sweep 是在 step 5 用背景執行緒發動的，跟第一次伸腳「刻意並行」
        // （2026-07-23 per user「一伸出腳就開始清潔」）。但它的 wait() 原本只在
        // 函式最尾端，也就是說**重試是在手臂還 DEPLOY 貼著牆、上滑台還在移動的
        // 時候跑的**。重試會再次推動推桿改變機體姿態 —— 手臂這時還壓在玻璃上，
        // 可能被刮傷或扯壞。bench 上 user 就是看到這個才提出來的。
        //
        // 只在「重試」這條路徑等，不動正常路徑：伸腳一次就成功時 fails 為空，
        // 完全不會走到這裡，並行度照舊。異常路徑本來就慢，多等這一下換掉一個
        // 實體碰撞風險是划算的。
        // 尾端那個 fut_rail_sweep.wait() 保留不動 —— future 已 ready 時它是
        // no-op，而它仍要負責覆蓋「沒有進重試」的正常路徑。
        // [2026-08-28 per user] 原地補伸已停用（見下方 #if 0），這裡仍然等掃動結束
        // 才往下走：接下來若判定停住不動，手臂應該已經 PARK 好、上滑台也停了，
        // 不要在手臂還貼著玻璃的狀態下把錯誤丟出去讓操作者去動機器。
        // 等待總長不變 —— 函式尾端的 RAII join 本來就會等同樣久。
        if (fut_rail_sweep.valid()) {
            std::cout << "[" << tag << "] 等待清洗掃動結束（含手臂 PARK）\n";
            fut_rail_sweep.wait();
            std::cout << "[" << tag << "] 掃動已結束\n";
        }

        std::vector<int> right_fails, left_fails;
        for (int s : fails) {
            if (s == ZDT_RF1 || s == ZDT_RF2)      right_fails.push_back(s);
            else if (s == ZDT_LF1 || s == ZDT_LF2) left_fails.push_back(s);
        }

        // [2026-07-23 per user] Per-side early-stop on retry: a side that
        // already sealed >=1 cup (partial failure — some, not all, of its
        // slaves are in *_fails) already satisfies the v2 "每側 >=1 顆即算好"
        // convention (group_seal_ok_ / project_v2_step_seal_per_side), so
        // there's no safety reason to keep pushing its remaining cup — it
        // just stays retracted/unsealed. Only retry a side that came back
        // COMPLETELY bare (0 of its cups sealed) — that side genuinely needs
        // the attempt. Compares against group_slaves_ instead of a hardcoded
        // 2 so a disabled_zdt_slaves_ entry doesn't misclassify a 1-cup side.
        const size_t right_total = group_slaves_("right").size();
        const size_t left_total  = group_slaves_("left").size();
        const bool right_fully_bare = !right_fails.empty() && right_fails.size() >= right_total;
        const bool left_fully_bare  = !left_fails.empty()  && left_fails.size()  >= left_total;
        if (!right_fails.empty() && !right_fully_bare) {
            std::cout << "[" << tag << "] right already has >=1 sealed — skip retry, leaving unsealed:";
            for (int s : right_fails) std::cout << " " << s;
            std::cout << "\n";
        }
        if (!left_fails.empty() && !left_fully_bare) {
            std::cout << "[" << tag << "] left already has >=1 sealed — skip retry, leaving unsealed:";
            for (int s : left_fails) std::cout << " " << s;
            std::cout << "\n";
        }

        // [2026-08-28 per user] 原地補伸停用 —— 「這種已經掃完的就不用再補伸了」。
        //
        // 第一次伸出時 disable_seal 已經把該側推到底（bench log：iter 2 打到
        // `WALL I=1242mA ... endpoint, not obstacle`，或提早判定
        // `at wall + vacuum can't seal — weak_seal early`）。那已經是「推到牆、
        // 真空還是吸不起來」的結論，再 smart_extend_subset_ 一次只是拿更大的電流
        // 把同一顆杯子往玻璃上再頂一輪（log 實測第二輪推到 1242/1503mA），
        // 對密封沒有幫助，只是多壓一次玻璃與推桿。
        //
        // 這也跟 2026-08-28 那次「後退重吸機制停用、沒吸好就停住不動」
        // （見本檔上方 #if 0 與 changelog [2026-08-28k]）方向一致：v2 現在的態度是
        // 吸不好就交給人判斷，不再自動補救。停用後，未密封的情況直接落到下面的
        // group_seal_ok_ 判斷 —— 整側全裸就停住不動回 ERR，每側至少 1 顆就照舊繼續。
        if (right_fully_bare || left_fully_bare) {
            std::cout << "[" << tag << "] 整側全裸但原地補伸已停用 —— 不再補推，"
                         "直接進密封判定\n";
        }

        // [2026-07-23 fix] Per-side ≥1 sealed is the v2 convention (2026-07-08
        // per user, same rule do_step_down_/up_'s cycle_group_ and pre-flight
        // gates use via group_seal_ok_): only a WHOLE side unsealed is a hard
        // stop. Requiring all 4 here (as this originally did) was stricter
        // than that convention and threw a "1 sealed per side" partial seal
        // into State::Error when it should have proceeded.
        std::vector<int> right_unsealed, left_unsealed;
        bool right_ok = group_seal_ok_("right", right_unsealed);
        bool left_ok  = group_seal_ok_("left",  left_unsealed);
        if (!right_ok || !left_ok) {
            // [2026-08-28 per user] 後退重吸機制停用 —— 整組沒吸好就「停住不動」。
            //
            // 原本的行為（保留在下面 #if 0 內，隨時可以打開）是：整側沒吸住就全縮
            // 回、吊機往回退 STEP_SYNC_BACKOFF_CM、重新抽真空伸出、再檢查，一路退到
            // 起點為止。per user 現在不要這個自動補救 —— 沒吸好就停在原地等人判斷，
            // 不要讓機器在沒有任何一側吸住的狀態下再去驅動吊機。
            //
            // 停住的方式是回 ERR：呼叫端 (cmd_step_down_sync / cmd_step_up_sync /
            // 自動循環迴圈) 看到非 OK 就會把狀態切到 State::Error 並停止後續步伐。
            // 這條路徑走的是 fail()，所以 PWM 占空比也會一併寫回 5%（停止）。
            // 此刻推桿是伸出但未吸住的狀態，機器靠鋼索吊著，不做任何額外動作。
            {
                const std::string bad = (!right_ok && !left_ok) ? "both"
                                      : (!right_ok ? "right" : "left");
                std::cout << "[" << tag << "] " << bad << " unsealed after in-place retry"
                          << " — 停住不動（後退重吸已停用），unsealed:";
                for (int sl : right_unsealed) std::cout << " " << sl;
                for (int sl : left_unsealed)  std::cout << " " << sl;
                std::cout << "\n";
                evt_(std::string(tag) + "_side_unsealed side=" + bad);
                return fail("ERR side_unsealed\n");
            }

        }
        const size_t partial_count = right_unsealed.size() + left_unsealed.size();
        if (partial_count > 0) {
            std::cout << "[" << tag << "] partial seal (each side has >=1) — proceeding:";
            for (int s : right_unsealed) std::cout << " " << s;
            for (int s : left_unsealed)  std::cout << " " << s;
            std::cout << "\n";
            evt_(std::string(tag) + "_partial_seal count=" + std::to_string((int)partial_count));
        } else {
            std::cout << "[" << tag << "] all cups sealed after retry\n";
        }
    }

    // [2026-07-23] Rail sweep launched back at step 5, running concurrently
    // with feet-extend since then. Explicit wait() HERE (not just relying on
    // _rail_sweep_join's destructor, which would only fire once this whole
    // function returns — AFTER "done" already printed/replied) so the step
    // genuinely doesn't report done until the sweep is also finished, not just
    // feet-extend. _rail_sweep_join's destructor still covers every early
    // "return fail(...)" path above this point.
    fut_rail_sweep.wait();
    std::cout << "[" << tag << "] done\n";
    evt_(std::string(tag) + "_done cm=" + std::to_string(cm));
    motion_active_ = false;
    return up ? "OK step_up_sync_done\n" : "OK step_down_sync_done\n";
}


// === E. Realign sequence ===
// Periodically re-zero cup extension drift by retracting all 9 cups
// simultaneously while keeping vacuum sealed. Crane retracts a few cm first
// to share weight via rope so cup vacuum can pull machine toward wall without
// being overloaded.
//
// Trigger: any feet/body cup last_seal_pulse_ exceeds preset by REALIGN_THRESHOLD_CM.
// Returns "" on success or "not needed"; "ERR ..." on failure.
//
// Caller (cmd_step_down/up after success) decides whether to act on ERR.
// [v2 2026-07-08] Feet-only realign — pull all 4 feet cups back to preset while
// they stay SEALED (per user: 只有左右腳、全部一起往內縮、不解真空、不放繩不收繩).
// Over steps each cup reseals a little further out (drift snowball); realign
// retracts each pusher back to preset, which pulls the machine body toward the
// wall. The cup never releases — so the "all 4 cups anchored" invariant holds and
// NO crane rope motion is needed. last_seal_pulse_ is re-read afterwards so the
// next step's extend starts from preset again.
//
// Reuses v1's proven Phase 2 motion (Stage 0 preload-relief jog → Stage A slow
// break-adhesion retract 1/3 → Stage B fast retract 2/3, all relative-mode sync
// moves), restricted to the 4 feet slaves. v1's crane assist (Phase 1/5) and body
// retract/rebuild (Phase A/B) are gone — v2 has no body group and realign moves
// no rope. apply_threshold=false skips the drift threshold gate (manual / forced
// realign, e.g. cross-obstacle Phase 3). The seal pre-check ALWAYS runs regardless
// [2026-07-13 per user]: require EACH side >=1 cup sealed (refuse only if a whole
// side is off) — sealed cups pull the body, a surviving unsealed cup just resets to
// preset in air. `in_window` is ignored (no body window).
std::string WashRobot::do_feet_realign_(bool apply_threshold, bool caller_holds_lock) {
    const std::vector<int> feet = {ZDT_RF1, ZDT_RF2, ZDT_LF1, ZDT_LF2};
    constexpr double REAL_POS_MIN_DEG = -10.0;    // tolerate small negatives at zero
    constexpr double REAL_POS_MAX_DEG = 6000.0;   // SMC LEYG25 20cm = 6000° = corrupt-frame guard
    constexpr double FEET_PULSE_PER_CM = CUP_PULSE_PER_CM;   // [2026-08-28] 原為 20000/7=2857，實測應為 3000（見 WASH_ROBOT.h）
    auto feet_skip = [this](int s) -> bool { return disabled_zdt_slaves_.count(s) > 0; };

    // caller_holds_lock (end-of-step auto-call): do_step_*_ already holds motion_mtx_
    // and owns motion_active_ — re-locking here is a same-thread deadlock, and the
    // step will clear motion_active_ itself. Standalone (cmd_realign): lock + own it.
    std::unique_lock<std::mutex> lk(motion_mtx_, std::defer_lock);
    if (!caller_holds_lock) {
        lk.lock();
        abort_flag     = false;
        motion_active_ = true;
    }

    // Threshold gate (end-of-step auto-call): compute drift from last_seal_pulse_
    // (in-memory, set by the last cycle_group_ extend — no ZDT reads needed) and
    // skip the whole realign unless a single cup drifts past REALIGN_THRESHOLD_CM
    // OR the mean drift passes REALIGN_THRESHOLD_MEAN_CM. This self-throttles: the
    // step checks every time but realign only actually moves once drift builds up.
    // Manual trigger (apply_threshold=false) proceeds on any nonzero drift.
    if (apply_threshold) {
        double max_abs = 0.0, sum_abs = 0.0; int n = 0;
        for (int s : feet) {
            if (feet_skip(s)) continue;
            const double d_cm = std::fabs((last_seal_pulse_[s - 1].load()
                                           - preset_extend_pulse_for_slave_(s)) / FEET_PULSE_PER_CM);
            if (d_cm > max_abs) max_abs = d_cm;
            sum_abs += d_cm; ++n;
        }
        const double mean_abs = (n > 0) ? sum_abs / n : 0.0;
        if (max_abs <= (settings_.realign_threshold_cm.load()) && mean_abs <= (settings_.realign_threshold_mean_cm.load())) {
            std::cout << "[realign] threshold not met (max=" << max_abs << "cm<=" << (settings_.realign_threshold_cm.load())
                      << ", mean=" << mean_abs << "cm<=" << (settings_.realign_threshold_mean_cm.load()) << ") — skip\n";
            if (!caller_holds_lock) motion_active_ = false;
            return "";   // no motion
        }
        std::cout << "[realign] threshold met (max=" << max_abs << "cm, mean=" << mean_abs << "cm) — running\n";
    }

    // SAFETY: realign retracts SEALED cups (vacuum holds cup to wall; the pusher
    // retracting pulls the BODY toward the wall). [2026-07-13 per user] RELAXED
    // from "ALL 4 sealed" to "EACH side has >=1 cup sealed". Rationale: with
    // stop_on_first_seal each side commonly runs one-cup-only, so demanding all 4
    // made the end-of-step realign a near-permanent no-op (log: refuse_unsealed=1).
    // Sealed cups still do the leveling; a surviving unsealed cup just repositions
    // to preset in air (harmless, and resets an over-extended weak cup for the next
    // step). Refuse ONLY if a WHOLE side is off — that side would have no anchor to
    // pull the body against during the retract.
    {
        std::vector<int> rfail, lfail;
        const bool right_ok = group_seal_ok_("right", rfail);
        const bool left_ok  = group_seal_ok_("left",  lfail);
        if (!right_ok || !left_ok) {
            std::string msg = "realign_refuse_side_off";
            if (!right_ok) msg += " right=ALL";
            if (!left_ok)  msg += " left=ALL";
            std::cout << "[realign] " << msg << " — a whole side is off, need >=1 sealed per side\n";
            evt_(msg);
            if (!caller_holds_lock) motion_active_ = false;
            return "ERR " + msg + "\n";
        }
        // Each side has >=1 sealed → proceed. Surface any surviving unsealed cup(s).
        std::vector<int> unsealed = rfail;
        unsealed.insert(unsealed.end(), lfail.begin(), lfail.end());
        unsealed.erase(std::remove_if(unsealed.begin(), unsealed.end(), feet_skip), unsealed.end());
        if (!unsealed.empty()) {
            std::string wmsg = "realign_partial_seal unsealed=";
            for (size_t i = 0; i < unsealed.size(); ++i) { if (i) wmsg += ","; wmsg += std::to_string(unsealed[i]); }
            std::cout << "[realign] " << wmsg << " — each side >=1 sealed, proceed\n";
            evt_(wmsg);
        }
    }

    // Clear any latched stall flags — firmware silently rejects pos_mode while set.
    if (ensure_all_zdt_stall_clear_())
        std::cout << "[realign] stall clear failed (continuing)\n";

    // Phase 0: read live positions → sync last_seal_pulse_ for accurate delta.
    std::cout << "[realign] v2 phase 0: read live feet positions\n";
    for (int s : feet) {
        if (feet_skip(s)) continue;
        if (Z_(s).get_system_status()) {
            std::cout << "[realign] phase 0 slave " << s << " status read fail — keep last_seal_pulse_="
                      << last_seal_pulse_[s - 1].load() << "\n";
            continue;
        }
        const double rp = Z_(s).status.real_pos;
        if (rp < REAL_POS_MIN_DEG || rp > REAL_POS_MAX_DEG) {
            std::cout << "[realign] phase 0 slave " << s << " real_pos=" << rp
                      << "° OUT OF RANGE — corrupt frame, keep prior\n";
            evt_("realign_phase0_bad_pos slave=" + std::to_string(s));
            continue;
        }
        last_seal_pulse_[s - 1].store((int)(rp * 10.0));
    }

    // Per-slave delta (over-extension vs preset). >0 = retract, <0 = extend, 0 = skip.
    std::vector<int> deltas(9, 0);
    for (int s : feet) {
        if (feet_skip(s)) continue;
        const int preset = preset_extend_pulse_for_slave_(s);
        deltas[s - 1] = last_seal_pulse_[s - 1].load() - preset;
        const double drift_cm = deltas[s - 1] / (20000.0 / 7.0);
        std::cout << "[realign] slave " << s << " preset=" << preset
                  << " actual=" << last_seal_pulse_[s - 1].load()
                  << " delta=" << deltas[s - 1] << " drift_cm=" << drift_cm << "\n";
    }

    // [2026-07-14 per user] Single-stage realign: retract each cup to preset in ONE
    // gentle move (RPM_FULL=70) — no Stage 0 jog, no 1/3-slow/2/3-fast split. Each
    // stage was a fixed ~450-600ms sync-move+wait, so the old 3-stage path spent most
    // of its time in overhead for a ~1cm correction; and its Stage A ran at 100rpm,
    // which was popping the vacuum seal (realign_post_unsealed). One 70rpm move is
    // both faster (overhead-wise) and gentler (seal-safe). Large drift just takes a
    // little longer at 70 — rare and acceptable.
    // Build per-slave retract / extend commands.
    struct PhaseCmd { int slave, dir, stageA_mag, stageA_rpm, stageA_acc, stageB_mag, stageB_rpm, stageB_acc; };
    std::vector<PhaseCmd> cmds;
    for (int s : feet) {
        if (feet_skip(s)) continue;
        const int delta = deltas[s - 1];
        if (delta == 0) continue;
        PhaseCmd c{};
        c.slave = s;
        if (delta > 0) {            // RETRACT — single gentle stage (Stage B unused)
            c.dir = 1;
            c.stageA_mag = delta;
            c.stageA_rpm = REALIGN_RETRACT_RPM_FULL; c.stageA_acc = REALIGN_RETRACT_ACC_FULL;
            c.stageB_mag = 0;
        } else {                    // EXTEND single-stage very slow: push short cup into wall
            c.dir = 0;
            c.stageA_mag = -delta;
            c.stageA_rpm = REALIGN_EXTEND_RPM;       c.stageA_acc = REALIGN_EXTEND_ACC;
            c.stageB_mag = 0;
        }
        cmds.push_back(c);
    }

    if (cmds.empty()) {
        if (!caller_holds_lock) motion_active_ = false;
        std::cout << "[realign] all feet already at preset — nothing to do\n";
        return "";   // success, no drift
    }

    bool any_stalled = false, pos_send_failed = false;
    int  stalled_slave = -1, pos_send_fail_slave = -1;

    // Send + sync-trigger + parallel-wait one stage. On stall: freeze all still-
    // moving slaves and flag. On pos_send fail: flag and bail via standard ERR path.
    auto run_stage = [&](int stage_num, bool is_stageA) {
        if (any_stalled || pos_send_failed) return;
        std::vector<int> moving;
        for (auto& c : cmds) {
            const int mag = is_stageA ? c.stageA_mag : c.stageB_mag;
            if (mag <= 0) continue;
            const int rpm = is_stageA ? c.stageA_rpm : c.stageB_rpm;
            const int acc = is_stageA ? c.stageA_acc : c.stageB_acc;
            std::cout << "[realign] stage " << stage_num << " slave " << c.slave
                      << (c.dir ? " RETRACT" : " EXTEND") << " mag=" << mag << " rpm=" << rpm << "\n";
            if (Z_(c.slave).motion_control_pos_mode_nowait(c.dir, acc, rpm, mag,
                                                           /*mode=relative*/0, /*sync*/1, /*retry*/1)) {
                pos_send_failed = true; pos_send_fail_slave = c.slave;
                std::cout << "[realign] stage " << stage_num << " slave " << c.slave << " pos_mode_send FAIL\n";
                return;
            }
            moving.push_back(c.slave);
        }
        if (moving.empty()) return;
        Z_(moving.front()).trigger_sync_move();
        int stalled_id = -1;
        if (zdt_wait_motion_done_many_(moving, 15000, /*defer_stall=*/false, &stalled_id)) {
            any_stalled = true; stalled_slave = stalled_id;
            std::cout << "[realign] stage " << stage_num << " slave " << stalled_id
                      << " STALL — e-stop other moving slaves\n";
            for (int s : moving) if (s != stalled_id) Z_(s).emergency_stop(false);
            sleep_ms_(100);
        }
    };

    // [2026-07-14 per user] Stage 0 preload-relief jog REMOVED — v2 feet don't bear
    // weight so the elastic preload it relieved is negligible; the single 70rpm
    // retract below breaks adhesion gently on its own. (Old jog cost ~500ms/realign.)

    // Single-stage retract (Stage A carries the full move; Stage B mag=0 → no-op).
    run_stage(1, /*is_stageA=*/true);
    run_stage(2, /*is_stageA=*/false);   // all stageB_mag==0 now → skips; kept for structure

    if (pos_send_failed) {
        for (int s : feet) Z_(s).release_stall_flag();
        if (!caller_holds_lock) motion_active_ = false;
        evt_("realign_fail pos_mode_send slave=" + std::to_string(pos_send_fail_slave));
        return "ERR realign_pos_send_fail slave=" + std::to_string(pos_send_fail_slave) + "\n";
    }

    // Re-read positions → last_seal_pulse_ now ≈ preset (new baseline). Reset the
    // feet over-extension tracker so the next step extends from preset.
    for (int s : feet) {
        if (feet_skip(s)) continue;
        if (Z_(s).get_system_status()) continue;
        const double rp = Z_(s).status.real_pos;
        if (rp < REAL_POS_MIN_DEG || rp > REAL_POS_MAX_DEG) {
            evt_("realign_phase25_bad_pos slave=" + std::to_string(s));
            continue;
        }
        last_seal_pulse_[s - 1].store((int)(rp * 10.0));
    }
    last_feet_max_over_cm_.store(0.0);

    if (any_stalled) {
        for (int s : feet) Z_(s).release_stall_flag();
        // A stall leaves cups frozen off-preset — but they NEVER released (valves
        // stayed ON), so all 4 are still sealed and the machine stays anchored;
        // only the drift correction failed. Standalone (manual): force PausedOnError
        // so the operator inspects. In-step (caller_holds_lock): NON-FATAL — return
        // the error and let do_step_*_ log it and finish the step (the descent/ascent
        // itself succeeded; drift just retries next step-end). Matches v1's non-fatal
        // in-window realign handling.
        if (!caller_holds_lock) {
            motion_active_ = false;
            {
                std::lock_guard<std::mutex> slk(state_mtx_);
                if (state_.load() != State::PausedOnError) state_before_pause_ = state_.load();
            }
            set_state_(State::PausedOnError);
            std::cout << "[realign] PAUSED ON ERROR — slave " << stalled_slave
                      << " stalled. Awaiting cmd_continue / emergency_stop.\n";
        } else {
            std::cout << "[realign] slave " << stalled_slave
                      << " stalled (in-step, non-fatal — cups still sealed)\n";
        }
        evt_("realign_stall slave=" + std::to_string(stalled_slave));
        return "ERR realign_motion_fail slave=" + std::to_string(stalled_slave) + "\n";
    }

    // Post-check: cups should still be sealed (they never released). Warn if not —
    // realign doesn't reseal (no extend), so a lost seal is for the operator / IMU.
    {
        auto fails = vacuum_check_("all");
        fails.erase(std::remove_if(fails.begin(), fails.end(), feet_skip), fails.end());
        if (!fails.empty()) {
            std::string msg = "realign_post_unsealed=";
            for (size_t i = 0; i < fails.size(); ++i) { if (i) msg += ","; msg += std::to_string(fails[i]); }
            std::cout << "[realign] WARN " << msg << " (seal lost during retract)\n";
            evt_(msg);
        }
    }

    for (int s : feet) Z_(s).release_stall_flag();
    if (!caller_holds_lock) motion_active_ = false;
    evt_("realign_done");
    std::cout << "[realign] v2 done (feet-only, sealed retract to preset)\n";
    return "";
}

std::string WashRobot::cmd_step_up(int cm) {
    StepInProgressGuard _sip_guard{step_in_progress_};
    State cur = state_.load();
    // DISABLE STATUS CHECK
    // if (cur != State::Attached) return state_violation_(cur);
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > (settings_.step_cm_max.load())) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << (settings_.step_cm_max.load()) << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[step_up] start → Running (step=" << step_cm_.load() << " cm)\n";
    set_state_(State::Running);

    // Single step → lead with the chosen first-step side.
    std::string r = do_step_up_(false, {}, {}, first_step_right_.load());
    if (r.rfind("OK", 0) != 0) {
        std::cout << "[step_up] FAIL: " << r;
        set_state_(State::Error);
        return r;
    }
    std::cout << "[step_up] " << r;
    set_state_(State::Attached);

    // [2026-05-22] End-of-step realign DISABLED (per user — see cmd_step_down).
    // // E: realign trigger (same as step_down — force=false 內部過 threshold)
    // {
    //     std::string realign_err = do_feet_realign_(/*force=*/false);
    //     if (!realign_err.empty()) {
    //         std::cout << "[step_up] realign FAIL (non-fatal): " << realign_err;
    //     }
    // }
    // [arm rope protect TEMP 2026-05-21] end-of-step PARK (same reasoning as step_down)
    ensure_arm_parked_after_rope_("step_up_end_realign");
    return r;
}

// [2026-07-13 per user] 跨障礙物 down/up — mirror of cmd_step_down/up but runs
// do_cross_obstacle_ (2×preset stand-off cross). Same cm handling + state flow.
std::string WashRobot::cmd_cross_obstacle_down(int cm) {
    StepInProgressGuard _sip_guard{step_in_progress_};
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > (settings_.step_cm_max.load())) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << (settings_.step_cm_max.load()) << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[cross_down] start → Running (step=" << step_cm_.load() << " cm)\n";
    set_state_(State::Running);

    std::string r = do_cross_obstacle_(/*up=*/false);
    if (r.rfind("OK", 0) != 0) {
        std::cout << "[cross_down] FAIL: " << r;
        set_state_(State::Error);
        return r;
    }
    std::cout << "[cross_down] " << r;
    set_state_(State::Attached);
    ensure_arm_parked_after_rope_("cross_down_end");
    return r;
}

std::string WashRobot::cmd_cross_obstacle_up(int cm) {
    StepInProgressGuard _sip_guard{step_in_progress_};
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > (settings_.step_cm_max.load())) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << (settings_.step_cm_max.load()) << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[cross_up] start → Running (step=" << step_cm_.load() << " cm)\n";
    set_state_(State::Running);

    std::string r = do_cross_obstacle_(/*up=*/true);
    if (r.rfind("OK", 0) != 0) {
        std::cout << "[cross_up] FAIL: " << r;
        set_state_(State::Error);
        return r;
    }
    std::cout << "[cross_up] " << r;
    set_state_(State::Attached);
    ensure_arm_parked_after_rope_("cross_up_end");
    return r;
}

// [2026-07-22 per user] Synchronized step — see do_step_sync_ for the gait
// itself and its explicit zero-anchor-during-move safety note. Wrapper
// mirrors cmd_step_down/cmd_cross_obstacle_down exactly (state guard, cm
// range validate, do_*_ call, arm-park-after-rope).
std::string WashRobot::cmd_step_down_sync(int cm) {
    StepInProgressGuard _sip_guard{step_in_progress_};
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > (settings_.step_cm_max.load())) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << (settings_.step_cm_max.load()) << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[step_down_sync] start → Running (step=" << step_cm_.load() << " cm)\n";
    set_state_(State::Running);

    std::string r = do_step_sync_(/*up=*/false);
    if (r.rfind("OK", 0) != 0) {
        std::cout << "[step_down_sync] FAIL: " << r;
        set_state_(State::Error);
        return r;
    }
    std::cout << "[step_down_sync] " << r;
    set_state_(State::Attached);
    ensure_arm_parked_after_rope_("step_down_sync_end");
    return r;
}

std::string WashRobot::cmd_step_up_sync(int cm) {
    StepInProgressGuard _sip_guard{step_in_progress_};
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > (settings_.step_cm_max.load())) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << (settings_.step_cm_max.load()) << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[step_up_sync] start → Running (step=" << step_cm_.load() << " cm)\n";
    set_state_(State::Running);

    std::string r = do_step_sync_(/*up=*/true);
    if (r.rfind("OK", 0) != 0) {
        std::cout << "[step_up_sync] FAIL: " << r;
        set_state_(State::Error);
        return r;
    }
    std::cout << "[step_up_sync] " << r;
    set_state_(State::Attached);
    ensure_arm_parked_after_rope_("step_up_sync_end");
    return r;
}

// step_up + 連續 cleaning sweep（並行）。
// 主 thread 跑 step_up（不跑末段 sweep），背景 thread 連續跑 LEFT+RIGHT 輪洗到
// step_up 結束。step 結束後等當前輪跑完才返回。
// 設計細節見 .claude/changelog.md 2026-05-22。
std::string WashRobot::cmd_step_up_with_sweep(int cm) {
    StepInProgressGuard _sip_guard{step_in_progress_};
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > (settings_.step_cm_max.load())) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << (settings_.step_cm_max.load()) << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[step_up+sweep] start → Running (step=" << step_cm_.load() << " cm)\n";
    set_state_(State::Running);
    // [2026-05-29] Reset arm sweep obstacle/skip flags — single-step = fresh scope.
    arm_sweep_obstacle_pending_.store(false);
    arm_sweep_skip_rest_of_run_.store(false);

    // 背景 sweep 控制 flag
    std::atomic<bool> sweep_keep_going{true};

    // Launch 背景 sweep
    auto fut_sweep = std::async(std::launch::async, [this, &sweep_keep_going]() -> std::string {
        return do_arm_clean_sweep_continuous_((settings_.arm_clean_wall_mm.load()), sweep_keep_going);
    });

    // RAII guard：任何 return 路徑都保證 sweep 收尾。先設 keep_going=false，再 wait。
    struct SweepJoin {
        std::atomic<bool>& flag;
        std::future<std::string>& f;
        ~SweepJoin() {
            flag.store(false);
            if (f.valid()) f.wait();
        }
    } _sweep_guard{sweep_keep_going, fut_sweep};

    // 主 thread 跑 step_up，skip 末段 sweep（背景 thread 負責）
    std::string r = do_step_up_(/*skip_cleaning_sweep=*/true);

    // step_up 跑完，通知背景 sweep 停止（跑完當前完整 LEFT+RIGHT round 才結束）
    sweep_keep_going.store(false);
    std::cout << "[step_up+sweep] step_up done, waiting for current sweep round to finish...\n";
    std::string sweep_r = fut_sweep.get();   // 等背景完成、consumes future（guard 變 no-op）
    std::cout << "[step_up+sweep] sweep result: " << sweep_r;
    if (handle_post_sweep_obstacle_("step_up_with_sweep")) {
        set_state_(State::Error);
        return "ERR aborted_arm_obstacle\n";
    }

    if (r.rfind("OK", 0) != 0) {
        std::cout << "[step_up+sweep] FAIL: " << r;
        set_state_(State::Error);
        return r;
    }
    std::cout << "[step_up+sweep] " << r;
    set_state_(State::Attached);

    if (sweep_r.rfind("OK", 0) != 0) {
        std::cout << "[step_up+sweep] sweep FAIL (non-fatal): " << sweep_r;
    }

    // [2026-05-22] End-of-step realign DISABLED (per user — see cmd_step_down).
    // // E: realign trigger（同 cmd_step_up）
    // {
    //     std::string realign_err = do_feet_realign_(/*force=*/false);
    //     if (!realign_err.empty()) {
    //         std::cout << "[step_up+sweep] realign FAIL (non-fatal): " << realign_err;
    //     }
    // }
    // [arm rope protect TEMP 2026-05-21] end-of-step PARK
    ensure_arm_parked_after_rope_("step_up_with_sweep_end_realign");
    return r;
}

// step_up + 1 round cleaning sweep。sweep 在 feet rail DM2J 走完那刻 launch。
// 2026-05-25 改用 do_arm_clean_sweep_continuous_ + max_rounds=1：silent error
// policy 不會卡 try_or_pause_ 跟 arm_mtx_。
std::string WashRobot::cmd_step_up_sweep_after_feet(int cm) {
    StepInProgressGuard _sip_guard{step_in_progress_};
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > (settings_.step_cm_max.load())) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << (settings_.step_cm_max.load()) << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[step_up+sweep_af] start → Running (step=" << step_cm_.load() << " cm)\n";
    set_state_(State::Running);
    // [2026-05-29] Reset arm sweep obstacle/skip flags — single-step = fresh scope.
    arm_sweep_obstacle_pending_.store(false);
    arm_sweep_skip_rest_of_run_.store(false);

    // continuous helper 用 keep_going + max_rounds。max_rounds=1 → 跑完 1 round
    // 即使 keep_going 還是 true 也會自動退出。但仍要 keep_going 以便 step 結束
    // 時可以「提前通知」（雖然這場景下不重要因為已限 1 round）。
    std::atomic<bool>        sweep_keep_going{true};
    std::future<std::string> fut_sweep;

    struct SweepJoin {
        std::atomic<bool>& flag;
        std::future<std::string>& f;
        ~SweepJoin() {
            flag.store(false);
            if (f.valid()) f.wait();
        }
    } _sweep_guard{sweep_keep_going, fut_sweep};

    // hook：feet rail DM2J 走完那刻被呼叫一次 → launch 1 round 背景 sweep
    auto after_feet_hook = [this, &sweep_keep_going, &fut_sweep]() {
        std::cout << "[step_up+sweep_af] feet rail done → launching 1-round sweep (continuous, max_rounds=1)\n";
        fut_sweep = std::async(std::launch::async, [this, &sweep_keep_going]() -> std::string {
            return do_arm_clean_sweep_continuous_((settings_.arm_clean_wall_mm.load()), sweep_keep_going, /*max_rounds=*/1);
        });
    };

    std::string r = do_step_up_(/*skip_cleaning_sweep=*/true, after_feet_hook);

    sweep_keep_going.store(false);
    std::string sweep_r = "OK sweep_not_launched\n";
    if (fut_sweep.valid()) {
        std::cout << "[step_up+sweep_af] step_up done, waiting for sweep round to finish...\n";
        sweep_r = fut_sweep.get();
        std::cout << "[step_up+sweep_af] sweep result: " << sweep_r;
        if (handle_post_sweep_obstacle_("step_up_sweep_after_feet")) {
            set_state_(State::Error);
            return "ERR aborted_arm_obstacle\n";
        }
    } else {
        std::cout << "[step_up+sweep_af] sweep never launched (feet phase failed before hook)\n";
    }

    if (r.rfind("OK", 0) != 0) {
        std::cout << "[step_up+sweep_af] FAIL: " << r;
        set_state_(State::Error);
        return r;
    }
    std::cout << "[step_up+sweep_af] " << r;
    set_state_(State::Attached);

    if (sweep_r.rfind("OK", 0) != 0) {
        std::cout << "[step_up+sweep_af] sweep FAIL (non-fatal): " << sweep_r;
    }

    // [arm rope protect TEMP 2026-05-21] end-of-step PARK
    ensure_arm_parked_after_rope_("step_up_sweep_after_feet_end");
    return r;
}

// step_down + 1 round cleaning sweep。sweep 在 Phase B feet rail 回到 0 那刻 launch。
// 2026-05-25 同 step_up 版改用 do_arm_clean_sweep_continuous_ + max_rounds=1。
std::string WashRobot::cmd_step_down_sweep_after_feet(int cm) {
    StepInProgressGuard _sip_guard{step_in_progress_};
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > (settings_.step_cm_max.load())) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << (settings_.step_cm_max.load()) << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[step_down+sweep_af] start → Running (step=" << step_cm_.load() << " cm)\n";
    set_state_(State::Running);
    // [2026-05-29] Reset arm sweep obstacle/skip flags — single-step = fresh scope.
    arm_sweep_obstacle_pending_.store(false);
    arm_sweep_skip_rest_of_run_.store(false);

    std::atomic<bool>        sweep_keep_going{true};
    std::future<std::string> fut_sweep;

    struct SweepJoin {
        std::atomic<bool>& flag;
        std::future<std::string>& f;
        ~SweepJoin() {
            flag.store(false);
            if (f.valid()) f.wait();
        }
    } _sweep_guard{sweep_keep_going, fut_sweep};

    auto after_feet_hook = [this, &sweep_keep_going, &fut_sweep]() {
        std::cout << "[step_down+sweep_af] feet rail home → launching 1-round sweep (continuous, max_rounds=1)\n";
        fut_sweep = std::async(std::launch::async, [this, &sweep_keep_going]() -> std::string {
            return do_arm_clean_sweep_continuous_((settings_.arm_clean_wall_mm.load()), sweep_keep_going, /*max_rounds=*/1);
        });
    };

    std::string r = do_step_down_(/*skip_cleaning_sweep=*/true, after_feet_hook);

    sweep_keep_going.store(false);
    std::string sweep_r = "OK sweep_not_launched\n";
    if (fut_sweep.valid()) {
        std::cout << "[step_down+sweep_af] step_down done, waiting for sweep round to finish...\n";
        sweep_r = fut_sweep.get();
        std::cout << "[step_down+sweep_af] sweep result: " << sweep_r;
        if (handle_post_sweep_obstacle_("step_down_sweep_after_feet")) {
            set_state_(State::Error);
            return "ERR aborted_arm_obstacle\n";
        }
    } else {
        std::cout << "[step_down+sweep_af] sweep never launched (feet phase failed before hook)\n";
    }

    if (r.rfind("OK", 0) != 0) {
        std::cout << "[step_down+sweep_af] FAIL: " << r;
        set_state_(State::Error);
        return r;
    }
    std::cout << "[step_down+sweep_af] " << r;
    set_state_(State::Attached);

    if (sweep_r.rfind("OK", 0) != 0) {
        std::cout << "[step_down+sweep_af] sweep FAIL (non-fatal): " << sweep_r;
    }

    // [arm rope protect TEMP 2026-05-21] end-of-step PARK
    ensure_arm_parked_after_rope_("step_down_sweep_after_feet_end");
    return r;
}

// ============================================================
// step_up + 移動前後各 1 round sweep (2026-05-27)
// Flow:
//   1) launch sweep round 1 at start (parallel with valve break + pusher retract)
//   2) before_feet_rail_hook joins round 1 (wait until done)
//   3) feet rail DM2J move
//   4) after_feet_rail_hook launches sweep round 2 (parallel with body + crane)
//   5) wait for round 2 at end of step
// 兩 round 都用 do_arm_clean_sweep_continuous_(max_rounds=1) silent error policy.
// ============================================================
std::string WashRobot::cmd_step_up_sweep_before_after(int cm) {
    StepInProgressGuard _sip_guard{step_in_progress_};
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > (settings_.step_cm_max.load())) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << (settings_.step_cm_max.load()) << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[step_up+sweep_ba] start → Running (step=" << step_cm_.load() << " cm)\n";
    set_state_(State::Running);
    // [2026-05-29] Reset arm sweep obstacle/skip flags — single-step = fresh scope.
    arm_sweep_obstacle_pending_.store(false);
    arm_sweep_skip_rest_of_run_.store(false);

    std::atomic<bool>        sweep_keep_going{true};
    std::future<std::string> fut_sweep;

    struct SweepJoin {
        std::atomic<bool>& flag;
        std::future<std::string>& f;
        ~SweepJoin() {
            flag.store(false);
            if (f.valid()) f.wait();
        }
    } _sweep_guard{sweep_keep_going, fut_sweep};

    // round helper（共用 launch 邏輯 + reset flag）
    auto launch_round = [this, &sweep_keep_going]() {
        sweep_keep_going.store(true);
        return std::async(std::launch::async, [this, &sweep_keep_going]() -> std::string {
            return do_arm_clean_sweep_continuous_((settings_.arm_clean_wall_mm.load()), sweep_keep_going, /*max_rounds=*/1);
        });
    };

    // ROUND 1: 整 step 開頭 launch,跟 valve break / pusher retract 並行
    std::cout << "[step_up+sweep_ba] launching sweep round 1 (pre-feet, parallel)\n";
    fut_sweep = launch_round();

    auto before_feet_hook = [this, &fut_sweep]() {
        if (fut_sweep.valid()) {
            std::cout << "[step_up+sweep_ba] joining sweep round 1 before feet rail move...\n";
            std::string r1 = fut_sweep.get();
            std::cout << "[step_up+sweep_ba] round 1 result: " << r1;
            handle_post_sweep_obstacle_("step_up_sweep_before_after_round1");
        }
    };

    auto after_feet_hook = [this, &launch_round, &fut_sweep]() {
        std::cout << "[step_up+sweep_ba] feet rail done → launching sweep round 2 (post-feet, parallel)\n";
        fut_sweep = launch_round();
    };

    std::string r = do_step_up_(/*skip_cleaning_sweep=*/true, after_feet_hook, before_feet_hook);

    // wait round 2 at end
    sweep_keep_going.store(false);
    std::string sweep_r = "OK sweep_not_launched\n";
    if (fut_sweep.valid()) {
        std::cout << "[step_up+sweep_ba] step done, waiting for sweep round 2 to finish...\n";
        sweep_r = fut_sweep.get();
        std::cout << "[step_up+sweep_ba] round 2 result: " << sweep_r;
        if (handle_post_sweep_obstacle_("step_up_sweep_before_after")) {
            set_state_(State::Error);
            return "ERR aborted_arm_obstacle\n";
        }
    }

    if (r.rfind("OK", 0) != 0) {
        std::cout << "[step_up+sweep_ba] FAIL: " << r;
        set_state_(State::Error);
        return r;
    }
    std::cout << "[step_up+sweep_ba] " << r;
    set_state_(State::Attached);

    if (sweep_r.rfind("OK", 0) != 0) {
        std::cout << "[step_up+sweep_ba] sweep round 2 FAIL (non-fatal): " << sweep_r;
    }

    ensure_arm_parked_after_rope_("step_up_sweep_before_after_end");
    return r;
}

// 對稱於 cmd_step_up_sweep_before_after。
std::string WashRobot::cmd_step_down_sweep_before_after(int cm) {
    StepInProgressGuard _sip_guard{step_in_progress_};
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > (settings_.step_cm_max.load())) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << (settings_.step_cm_max.load()) << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[step_down+sweep_ba] start → Running (step=" << step_cm_.load() << " cm)\n";
    set_state_(State::Running);
    // [2026-05-29] Reset arm sweep obstacle/skip flags — single-step = fresh scope.
    arm_sweep_obstacle_pending_.store(false);
    arm_sweep_skip_rest_of_run_.store(false);

    std::atomic<bool>        sweep_keep_going{true};
    std::future<std::string> fut_sweep;

    struct SweepJoin {
        std::atomic<bool>& flag;
        std::future<std::string>& f;
        ~SweepJoin() {
            flag.store(false);
            if (f.valid()) f.wait();
        }
    } _sweep_guard{sweep_keep_going, fut_sweep};

    auto launch_round = [this, &sweep_keep_going]() {
        sweep_keep_going.store(true);
        return std::async(std::launch::async, [this, &sweep_keep_going]() -> std::string {
            return do_arm_clean_sweep_continuous_((settings_.arm_clean_wall_mm.load()), sweep_keep_going, /*max_rounds=*/1);
        });
    };

    std::cout << "[step_down+sweep_ba] launching sweep round 1 (pre-feet, parallel)\n";
    fut_sweep = launch_round();

    auto before_feet_hook = [this, &fut_sweep]() {
        if (fut_sweep.valid()) {
            std::cout << "[step_down+sweep_ba] joining sweep round 1 before feet rail move...\n";
            std::string r1 = fut_sweep.get();
            std::cout << "[step_down+sweep_ba] round 1 result: " << r1;
            handle_post_sweep_obstacle_("step_down_sweep_before_after_round1");
        }
    };

    auto after_feet_hook = [this, &launch_round, &fut_sweep]() {
        std::cout << "[step_down+sweep_ba] feet rail home → launching sweep round 2 (post-feet, parallel)\n";
        fut_sweep = launch_round();
    };

    std::string r = do_step_down_(/*skip_cleaning_sweep=*/true, after_feet_hook, before_feet_hook);

    sweep_keep_going.store(false);
    std::string sweep_r = "OK sweep_not_launched\n";
    if (fut_sweep.valid()) {
        std::cout << "[step_down+sweep_ba] step done, waiting for sweep round 2 to finish...\n";
        sweep_r = fut_sweep.get();
        std::cout << "[step_down+sweep_ba] round 2 result: " << sweep_r;
        if (handle_post_sweep_obstacle_("step_down_sweep_before_after")) {
            set_state_(State::Error);
            return "ERR aborted_arm_obstacle\n";
        }
    }

    if (r.rfind("OK", 0) != 0) {
        std::cout << "[step_down+sweep_ba] FAIL: " << r;
        set_state_(State::Error);
        return r;
    }
    std::cout << "[step_down+sweep_ba] " << r;
    set_state_(State::Attached);

    if (sweep_r.rfind("OK", 0) != 0) {
        std::cout << "[step_down+sweep_ba] sweep round 2 FAIL (non-fatal): " << sweep_r;
    }

    ensure_arm_parked_after_rope_("step_down_sweep_before_after_end");
    return r;
}

std::string WashRobot::cmd_run(int steps, int cm, const std::string& direction, const std::string& gait) {
    if (steps <= 0) return "ERR invalid_steps\n";
    // [2026-07-23 per user] gait selects which per-step engine the loop below
    // calls: "alt" (default, existing behavior) = do_step_down_/do_step_up_
    // alternating inchworm walk; "sync" = do_step_sync_ (all 4 cups release/
    // reseal together, crane ropes move simultaneously — see do_step_sync_'s
    // header comment for the explicit zero-anchor-during-move safety note,
    // which applies to every iteration of a sync-gait run just as it does to
    // a single step_down_sync/step_up_sync call).
    const bool use_sync = (gait == "sync");
    if (!use_sync && gait != "alt") return "ERR gait_must_be_alt|sync\n";
    // [2026-08-31] alt 已停用 —— 在這裡就擋掉，不要跑到第一步才從 do_step_*_ 回錯。
    // 理由見 do_step_down_ 進場的守衛（單閥、無分側真空）。
    if (!use_sync) return "ERR alt_gait_disabled_single_valve (use gait=sync)\n";
    // [v2 2026-07-08] Arm-sweep pipeline STRIPPED. The cleaning arm isn't installed
    // in v2 and do_step_down_/up_ ignore the sweep hooks ((void)-cast), so the v1
    // continuous-sweep pipeline was dead code that also launched threads trying to
    // reach the (absent) arm service. This is now a plain step loop over the v2
    // do_step_down_/up_ (crane rope + 4 cups + cycle_group_ retry + end-of-step
    // realign). The full v1 pipeline is preserved verbatim under #if 0 as
    // _retired_cmd_run_v1_sweep_ below — see .claude/v2_app_redesign_plan.md
    // §"arm sweep 重新接回" for how to wire it back once the arm returns.
    //
    // Direction: down / up. The v1 down_sweep_af / up_sweep_af variants are still
    // accepted (GUI back-compat) but run as plain steps — sweep deferred.
    const bool is_down = (direction == "down" || direction == "down_sweep_af");
    const bool is_up   = (direction == "up"   || direction == "up_sweep_af");
    if (!is_down && !is_up)
        return "ERR direction_must_be_down|up\n";
    if (direction == "down_sweep_af" || direction == "up_sweep_af")
        std::cout << "[run] " << direction
                  << " — sweep deferred (arm not installed in v2), running plain step\n";

    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > (settings_.step_cm_max.load())) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << (settings_.step_cm_max.load()) << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[run] " << steps << " steps " << (is_up ? "up" : "down")
              << " × " << step_cm_.load() << " cm (" << (use_sync ? "sync" : "alt") << " gait)\n";
    set_state_(State::Running);

    motion_active_ = true;
    for (int i = 1; i <= steps; ++i) {
        if (abort_flag.load()) {
            motion_active_ = false;
            set_state_(State::Error);
            return "ERR aborted\n";
        }
        std::ostringstream oss; oss << "step " << i << "/" << steps << " " << (is_up ? "up" : "down")
                                     << " gait=" << (use_sync ? "sync" : "alt");
        evt_(oss.str());

        std::string r;
        if (use_sync) {
            r = do_step_sync_(is_up);
        } else {
            // [2026-07-09 per user] Alternate the leading side each step, seeded by the
            // chosen first-step side: step 1 = first_step_right_, then flips each step.
            const bool right_first = ((i % 2 == 1) == first_step_right_.load());
            r = is_up ? do_step_up_(false, {}, {}, right_first)
                      : do_step_down_(false, {}, {}, right_first);
        }
        if (r.rfind("OK", 0) != 0) {
            motion_active_ = false;
            set_state_(State::Error);
            return r;
        }
        // If IMU flagged a balance issue, pause and wait for confirm_balance.
        if (imu_ask_pending_.load()) pause_flag = true;
        if (check_abort_()) {
            motion_active_ = false;
            set_state_(State::Error);
            return "ERR aborted\n";
        }
    }

    motion_active_ = false;
    set_state_(State::Attached);
    return "OK run_done\n";
}


// ====================================================================
// [2026-06-05] Scripted run — CSV of per-step cm values, fixed
// down_sweep_af direction. See .claude/scripted_run_plan.md.
//
// Carved from cmd_run's is_down_sweep path so per-step behavior is byte-
// identical. Only difference: each iter passes steps[i] instead of 0 to
// cmd_step_down_sweep_after_feet, and EVT prefix is "script step ..." so
// the GUI can distinguish from regular run.
// ====================================================================

bool WashRobot::parse_script_csv_(const std::string& csv,
                                  std::vector<ScriptStep>& out,
                                  std::string& err) {
    out.clear();
    err.clear();
    if (csv.empty()) { err = "csv_empty"; return false; }

    // Token grammar: <int>[n]['*'<count>]
    //   <int>   — required step cm
    //   'n'     — optional, marks this step as no-sweep (transit only)
    //   '*<N>'  — optional repeat shorthand
    // Default (no 'n') = sweep step. Chosen because 99% of steps are sweep
    // and pre-2026-06-05 saved scripts have no 'n' suffix — those keep
    // meaning identical after this change.
    std::stringstream ss(csv);
    std::string token;
    int token_idx = 0;
    while (std::getline(ss, token, ',')) {
        ++token_idx;
        // strip whitespace (explicit set — avoids locale-dependent std::isspace
        // and an extra <cctype> include).
        token.erase(std::remove_if(token.begin(), token.end(),
                                   [](char c){ return c == ' ' || c == '\t'
                                                   || c == '\r' || c == '\n'; }),
                    token.end());
        if (token.empty()) continue;   // tolerate trailing/empty commas

        // Peel off optional '*<count>' from the right end first so the cm
        // part can be inspected for the trailing 'n' flag without ambiguity.
        std::string head = token;
        int count = 1;
        auto star = token.find('*');
        if (star != std::string::npos) {
            head = token.substr(0, star);
            std::string cnt_str = token.substr(star + 1);
            try {
                count = std::stoi(cnt_str);
            } catch (...) {
                err = "invalid_repeat_pos=" + std::to_string(token_idx) + "_val=" + token;
                out.clear();
                return false;
            }
        }

        // Now peel optional flags from the right of `head`, order-independent:
        //   'n' = no-sweep (transit) ; 'x' = 跨障礙物 (cross-obstacle) [2026-07-13].
        // e.g. "30nx" == "30xn". cross overrides sweep at execution time.
        bool sweep = true;
        bool cross = false;
        while (!head.empty()) {
            const char f = head.back();
            if      (f == 'n' || f == 'N') { sweep = false; head.pop_back(); }
            else if (f == 'x' || f == 'X') { cross = true;  head.pop_back(); }
            else break;
        }

        if (head.empty()) {
            err = "missing_cm_pos=" + std::to_string(token_idx) + "_val=" + token;
            out.clear();
            return false;
        }
        int cm = 0;
        try {
            cm = std::stoi(head);
        } catch (...) {
            err = "invalid_token_pos=" + std::to_string(token_idx) + "_val=" + token;
            out.clear();
            return false;
        }

        if (cm < STEP_CM_MIN || cm > (settings_.step_cm_max.load())) {
            err = "step_cm_out_of_range pos=" + std::to_string(token_idx)
                + " cm=" + std::to_string(cm)
                + " (allowed " + std::to_string(STEP_CM_MIN) + ".."
                + std::to_string((settings_.step_cm_max.load())) + ")";
            out.clear();
            return false;
        }
        if (count < 1 || count > SCRIPT_REPEAT_MAX) {
            err = "repeat_count_out_of_range pos=" + std::to_string(token_idx)
                + " count=" + std::to_string(count);
            out.clear();
            return false;
        }
        for (int k = 0; k < count; ++k) {
            out.push_back({cm, sweep, cross});
            if ((int)out.size() > SCRIPT_TOTAL_STEP_MAX) {
                err = "total_steps_exceed_max=" + std::to_string(SCRIPT_TOTAL_STEP_MAX);
                out.clear();
                return false;
            }
        }
    }

    if (out.empty()) { err = "csv_no_valid_tokens"; return false; }
    return true;
}

bool WashRobot::validate_script_name_(const std::string& name) {
    if (name.empty() || (int)name.size() > SCRIPT_NAME_MAX_LEN) return false;
    for (char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z')
                     || (c >= 'a' && c <= 'z')
                     || (c >= '0' && c <= '9')
                     || (c == '_' || c == '-');
        if (!ok) return false;
    }
    return true;
}

std::string WashRobot::cmd_run_script(const std::string& csv, bool up, const std::string& gait) {
    std::vector<ScriptStep> steps;
    std::string perr;
    if (!parse_script_csv_(csv, steps, perr)) return "ERR " + perr + "\n";
    if (steps.empty())                        return "ERR empty_script\n";
    const char* dir = up ? "up" : "down";   // [2026-07-14 per user] script direction
    // [2026-07-23 per user] gait selects the per-step engine for non-cross
    // steps — same "alt"/"sync" meaning as cmd_run. Cross steps always use
    // do_cross_obstacle_ (no sync variant exists for cross-obstacle).
    const bool use_sync = (gait == "sync");
    if (!use_sync && gait != "alt") return "ERR gait_must_be_alt|sync\n";
    // [2026-08-31] alt 已停用 —— 在這裡就擋掉，不要跑到第一步才從 do_step_*_ 回錯。
    // 理由見 do_step_down_ 進場的守衛（單閥、無分側真空）。
    if (!use_sync) return "ERR alt_gait_disabled_single_valve (use gait=sync)\n";

    // [v2 2026-07-08] Arm-sweep pipeline STRIPPED (same as cmd_run — arm not
    // installed, do_step_down_ hooks void'd). Plain per-step loop; every scripted
    // step descends (down, as v1's script did). Each step's `sweep` flag is parsed
    // and reported but has no effect for now (all run as plain descents). The v1
    // sweep pipeline is preserved under #if 0 as _retired_cmd_run_script_v1_sweep_
    // — see plan §"arm sweep 重新接回".
    int total_cm = 0, n_sweep = 0, n_transit = 0, n_cross = 0;
    for (const auto& s : steps) {
        total_cm += s.cm;
        if (s.cross)      ++n_cross;      // cross overrides sweep/transit
        else if (s.sweep) ++n_sweep;
        else              ++n_transit;
    }
    const int N = (int)steps.size();
    std::cout << "[run_script] dir=" << dir << " gait=" << (use_sync ? "sync" : "alt") << " "
              << N << " steps (" << n_sweep << " sweep + "
              << n_transit << " transit + " << n_cross << " cross; sweep deferred in v2), total "
              << total_cm << " cm\n";
    {
        std::ostringstream oss;
        oss << "script_start dir=" << dir << " gait=" << (use_sync ? "sync" : "alt")
            << " total_steps=" << N << " total_cm=" << total_cm
            << " sweep=" << n_sweep << " transit=" << n_transit << " cross=" << n_cross;
        evt_(oss.str());
    }
    set_state_(State::Running);

    motion_active_ = true;

    // [2026-08-28 per user] 第一步「走」之前先清洗一次起始位置。
    //
    // 為什麼需要：清洗是綁在步伐尾段的（do_step_sync_ 步驟 5 伸腳時才並行發動
    // do_step_sync_rail_sweep_），所以每一步洗的都是「移動後」那一格。沒有這段
    // pre-sweep 的話，機器出發前所在的那一格永遠不會被洗到。
    //
    // 這等同 v1 舊 pipeline 的 "iter 1 — launching pre-feet sweep"（現保留在
    // _retired_cmd_run_script_v1_sweep_ 的 #if 0 內），v2 重寫時沒跟著復活。
    //
    // 行為刻意跟步伐內建的那段一致：同一個 do_step_sync_rail_sweep_、同樣受
    // STEP_SYNC_ARM_CLEAN_ENABLED 管、失敗一律非致命（清洗從來不擋步伐）。
    // 差別只在 INIT 這裡是同步跑（沒有吊機移動可以並行掩蓋那 ~10s）。
    if (STEP_SYNC_ARM_CLEAN_ENABLED) {
        std::lock_guard<std::mutex> lk(motion_mtx_);
        std::cout << "[run_script] pre-step clean sweep — 先洗起始位置再開始走\n";
        evt_("script_pre_sweep_start");
        const bool pre_init_ok = (arm_cmd_("INIT", 60).rfind("OK", 0) == 0);
        arm_calibrated_.store(pre_init_ok);
        if (!pre_init_ok)
            std::cerr << "[run_script] pre-step arm INIT failed — rail sweep only, no brush\n";
        do_step_sync_rail_sweep_("run_script_pre", pre_init_ok);
        evt_("script_pre_sweep_done");
    } else {
        std::cout << "[run_script] pre-step clean sweep 跳過"
                     "（STEP_SYNC_ARM_CLEAN_ENABLED=false）\n";
    }
    if (check_abort_()) { motion_active_ = false; set_state_(State::Error); return "ERR aborted\n"; }

    for (int i = 1; i <= N; ++i) {
        if (abort_flag.load()) { motion_active_ = false; set_state_(State::Error); return "ERR aborted\n"; }
        const int  cm_i    = steps[i - 1].cm;
        const bool sweep_i = steps[i - 1].sweep;
        const bool cross_i = steps[i - 1].cross;
        const char* mode_i = cross_i ? "cross" : (sweep_i ? "sweep" : "transit");
        {
            std::ostringstream oss;
            oss << "script step " << i << "/" << N << " cm=" << cm_i << " mode=" << mode_i;
            evt_(oss.str());
        }
        step_cm_.store(cm_i);
        StepInProgressGuard _sip_guard{step_in_progress_};

        // [2026-07-13 per user] cross step → 跨障礙物; else plain step. Direction
        // (up/down) is per-run (dir), applied to both cross and plain steps.
        // [2026-07-23 per user] gait only affects the plain-step branch (sync
        // has no cross-obstacle variant); sync also has no left/right leading
        // side (both move together), so right_first only applies to alt.
        std::string r;
        if (cross_i) {
            r = do_cross_obstacle_(up);
        } else if (use_sync) {
            r = do_step_sync_(up);
        } else {
            // [2026-07-09 per user] Alternate leading side per step, seeded by the
            // chosen first-step side: step 1 = first_step_right_, then flips each step.
            const bool right_first = ((i % 2 == 1) == first_step_right_.load());
            r = up ? do_step_up_(false, {}, {}, right_first)
                   : do_step_down_(false, {}, {}, right_first);
        }
        if (r.rfind("OK", 0) != 0) {
            {
                std::ostringstream oss;
                oss << "script_complete status=fail step=" << i << "/" << N
                    << " mode=" << mode_i << " reason=" << r;
                evt_(oss.str());
            }
            motion_active_ = false; set_state_(State::Error); return r;
        }
        if (imu_ask_pending_.load()) pause_flag = true;
        if (check_abort_()) { motion_active_ = false; set_state_(State::Error); return "ERR aborted\n"; }
    }

    motion_active_ = false;
    set_state_(State::Attached);
    {
        std::ostringstream oss; oss << "script_complete status=ok total=" << N;
        evt_(oss.str());
    }
    return "OK script_done\n";
}


// ---------- Saved-script management ----------

std::string WashRobot::cmd_save_script(const std::string& name,
                                       const std::string& csv) {
    if (!validate_script_name_(name)) {
        return "ERR name_invalid (allowed [A-Za-z0-9_-]{1,"
             + std::to_string(SCRIPT_NAME_MAX_LEN) + "})\n";
    }
    // Parse for syntax / range validation. Discard result — only the original
    // CSV string is persisted (preserves user's `*` / `n` shorthand on reload).
    std::vector<ScriptStep> tmp;
    std::string perr;
    if (!parse_script_csv_(csv, tmp, perr)) return "ERR " + perr + "\n";

    {
        std::lock_guard<std::mutex> lk(saved_scripts_mtx_);
        saved_scripts_[name] = csv;
        if (save_saved_scripts_to_disk_()) {
            // Roll back in-memory change on disk failure so map ≡ disk.
            saved_scripts_.erase(name);
            return "ERR script_save_failed\n";
        }
    }
    return "OK saved name=" + name + " csv=" + csv + "\n";
}

std::string WashRobot::cmd_list_scripts() {
    std::lock_guard<std::mutex> lk(saved_scripts_mtx_);
    std::ostringstream oss;
    oss << "OK scripts=[";
    bool first = true;
    for (const auto& kv : saved_scripts_) {
        if (!first) oss << ",";
        oss << kv.first;
        first = false;
    }
    oss << "]\n";
    return oss.str();
}

std::string WashRobot::cmd_load_script(const std::string& name) {
    if (!validate_script_name_(name)) return "ERR name_invalid\n";
    std::lock_guard<std::mutex> lk(saved_scripts_mtx_);
    auto it = saved_scripts_.find(name);
    if (it == saved_scripts_.end()) return "ERR not_found name=" + name + "\n";
    return "OK csv=" + it->second + "\n";
}

std::string WashRobot::cmd_delete_script(const std::string& name) {
    if (!validate_script_name_(name)) return "ERR name_invalid\n";
    std::lock_guard<std::mutex> lk(saved_scripts_mtx_);
    auto it = saved_scripts_.find(name);
    if (it == saved_scripts_.end()) return "ERR not_found name=" + name + "\n";

    std::string backup = it->second;
    saved_scripts_.erase(it);
    if (save_saved_scripts_to_disk_()) {
        // Roll back in-memory change on disk failure so map ≡ disk.
        saved_scripts_[name] = backup;
        return "ERR script_delete_failed\n";
    }
    return "OK deleted name=" + name + "\n";
}

std::string WashRobot::cmd_run_saved(const std::string& name, bool up, const std::string& gait) {
    std::string csv;
    {
        std::lock_guard<std::mutex> lk(saved_scripts_mtx_);
        auto it = saved_scripts_.find(name);
        if (it == saved_scripts_.end()) return "ERR not_found name=" + name + "\n";
        csv = it->second;   // copy under lock so cmd_run_script can run unlocked
    }
    return cmd_run_script(csv, up, gait);
}

bool WashRobot::load_saved_scripts_from_disk_(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cout << "[scripts] " << path << " not found — no saved scripts\n";
        return false;
    }
    std::lock_guard<std::mutex> lk(saved_scripts_mtx_);
    saved_scripts_.clear();
    std::string line;
    int loaded = 0, skipped = 0;
    while (std::getline(f, line)) {
        // strip comments after '#'
        auto h = line.find('#');
        if (h != std::string::npos) line.resize(h);
        // tokenize: "<name> <csv>" — CSV may itself contain commas but no
        // whitespace (parse_script_csv_ strips spaces anyway).
        std::istringstream iss(line);
        std::string name, csv;
        if (!(iss >> name >> csv)) continue;
        if (!validate_script_name_(name)) { ++skipped; continue; }
        // Validate CSV by parsing — drop if malformed (avoid loading garbage).
        std::vector<ScriptStep> tmp;
        std::string perr;
        if (!parse_script_csv_(csv, tmp, perr)) {
            std::cerr << "[scripts] load skipped name=" << name
                      << " reason=" << perr << "\n";
            ++skipped;
            continue;
        }
        saved_scripts_[name] = csv;
        ++loaded;
    }
    std::cout << "[scripts] loaded " << loaded << " script(s)";
    if (skipped) std::cout << ", skipped " << skipped;
    std::cout << " from " << path << "\n";
    return false;
}

bool WashRobot::save_saved_scripts_to_disk_(const std::string& path) {
    // Caller holds saved_scripts_mtx_ (cmd_save_script / cmd_delete_script).
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "[scripts] save failed — cannot open " << path << "\n";
        return true;
    }
    f << "# washrobot saved scripts — generated by cmd_save_script / cmd_delete_script\n";
    f << "# Each line: <name> <csv>. Comments after '#'.\n";
    for (const auto& kv : saved_scripts_) {
        f << kv.first << " " << kv.second << "\n";
    }
    return false;
}

// [2026-08-27 per user] IMU 傾斜保護開關（見 WASH_ROBOT.h imu_guard_enabled_）。
// 不做 state 檢查：關閉保護的時機正好是「已經被誤報打進 Error」的時候。
std::string WashRobot::cmd_imu_guard(bool on) {
    imu_guard_enabled_.store(on);
    std::cout << "[imu_guard] tilt protection " << (on ? "ENABLED" : "DISABLED")
              << (on ? "" : "  ⚠ 沒有傾斜保護，僅限機器在地面、未吊掛時使用")
              << "\n";
    return on ? "OK imu_guard=on\n"
              : "OK imu_guard=off WARNING_no_tilt_protection\n";
}

// [2026-08-27 per user] 單獨重取 IMU 水平基準（見 WASH_ROBOT.h 的說明）。
std::string WashRobot::cmd_imu_zero() {
    // 刻意不檢查 state：基準沒校好時 imu_monitor_loop_ 會把機器判成 45°+ 傾斜
    // 並 set_state_(Error)，若這裡再要求 state 正常就永遠校正不了。
    if (imu_.read_error.load()) return "ERR imu_read_error\n";

    // [2026-08-27] IMU 改回水平安裝 → before/after 用尤拉角，與基準定義一致。
    const double before_roll  = imu_.x - imu_roll0_;
    const double before_pitch = imu_.y - imu_pitch0_;

    std::cout << "[imu_zero] taking baseline for " << IMU_BASELINE_SEC
              << "s — 機器必須靜止，且保持在你要當作「水平」的姿態\n";
    if (imu_take_baseline_()) return "ERR imu_baseline_fail\n";

    const double after_roll  = imu_.x - imu_roll0_;
    const double after_pitch = imu_.y - imu_pitch0_;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2)
        << "OK imu_zero roll0=" << imu_roll0_ << " pitch0=" << imu_pitch0_
        << " before(roll=" << before_roll << " pitch=" << before_pitch << ")"
        << " after(roll="  << after_roll  << " pitch=" << after_pitch  << ")";
    // 不自動清 Error：Error 也可能是別的原因造成的，這裡自作主張清掉會掩蓋問題。
    if (state_.load() == State::Error)
        oss << " NOTE:state_still_Error_press_reset";
    oss << "\n";
    std::cout << "[imu_zero] " << oss.str();
    return oss.str();
}

std::string WashRobot::cmd_emergency_stop() {
    abort_flag    = true;
    pause_flag    = false;
    motion_active_ = false;
    for (int s = CUP_SLAVE_FIRST; s <= CUP_SLAVE_LAST; ++s) Z_(s).emergency_stop(false);
    crane_cmd_("stop", 2);   // Crane_control_PI uses 'stop' (no 'emergency_stop' alias)
    // [2026-05-28] Invalidate arm calibration: emergency_stop may have left arm
    // in an unknown state (mid-motion abort). Next cmd_init must re-INIT.
    if (arm_calibrated_.exchange(false)) {
        std::cout << "[emergency_stop] arm_calibrated_ → false (re-INIT required)\n";
    }
    // [2026-06-09] Force-close water inlet — emergency_stop may have killed a
    // sweep mid-water-fill, leaving the valve armed open. set_water_inlet_ has
    // its own retry; ignore failure here (watchdog will retry later).
    if (water_inlet_open_ts_ms_.load() != 0) {
        std::cout << "[emergency_stop] water_inlet was open → force close\n";
        set_water_inlet_(false);
    }
    set_state_(State::Error);
    return "OK emergency_stopped\n";
}

std::string WashRobot::cmd_shutdown() {
    abort_flag    = true;
    pause_flag    = false;
    motion_active_ = false;
    for (int s = CUP_SLAVE_FIRST; s <= CUP_SLAVE_LAST; ++s) Z_(s).emergency_stop(false);
    pqw_.controlRelay(CH_BRUSH,        false);
    pqw_.controlRelay(CH_WATER_PUMP,   false);
    set_water_inlet_(false);   // [2026-06-05] → crane PQW (.34 slave 12 CH4)
    pqw_.controlRelay(CH_VALVE_RIGHT,  false);
    pqw_.controlRelay(CH_VALVE_LEFT,   false);
    pqw_.controlRelay(CH_PUMP,         false);
    return "OK shutdown\n";
}

// [2026-05-29] Background pressure poll DISABLED. Body retained as no-op
// for backward compatibility (pressure_poll_running_ never set true).
// Replaced by:
//   - read_pressure_() — piggyback updates cache from natural reads
//   - cmd_status() — fresh-reads on demand when motion idle
void WashRobot::pressure_poll_loop_() {
    // intentionally empty — see above
}

// [2026-05-29] Wrapper around M_(slave).read_pressure() that piggyback-updates
// cached_pressure_[]. Use in motion paths so GUI sees fresh values without
// background thread polling cli_22_ bus.
int WashRobot::read_pressure_(int slave) {
    int p = M_(slave).read_pressure();
    // [2026-08-28] 這是運動路徑的更新點；cmd_status 的 fresh-read 是另一個。
    // 🔴 **兩處都要維護 pressure_stale_**，只改一邊的話 `p_err` 欄位自己就會說謊
    //    —— 那正好是它要解決的問題。（本專案「同一件事寫在兩處」已出過三次事。）
    if (M_(slave).error_flag == 0) {
        cached_pressure_[slave - 1].store(p);
        pressure_stale_[slave - 1].store(false);
    } else {
        pressure_stale_[slave - 1].store(true);
    }
    return p;
}

std::string WashRobot::cmd_status() {
    // [2026-05-29] Refresh-on-demand: if not in motion, do a one-shot fresh
    // read of all 9 JC100 + update cache. During motion, return cache
    // (motion paths piggyback updates via read_pressure_()).
    // [2026-06-02] Rate-limit fresh-read to ≤1Hz. GUI polls status at 2Hz
    // (500ms) and each fresh-read hits cli_22_ 9 times → bus saturation +
    // JC100 TIMEOUT flood (observed during attach idle gaps). Cap to 1
    // fresh-read/sec so JC100 load is decoupled from GUI poll frequency.
    // Cache reads remain cheap → GUI display still updates at poll rate,
    // just with at-most 1-sec-stale pressure values.
    // [2026-06-03] Skip fresh-read while a step cmd is in progress, in addition
    // to motion_active_. motion_active_ toggles off between step phases (e.g.,
    // mid-realign), letting GUI poll fire JC100 reads on cli_22_ — which
    // contends with step_down body_pre_cycle PQW/JC100/DM2J:14 ops on the same
    // bus. step_in_progress_ stays true for the ENTIRE step duration.
    if (!motion_active_.load() && !step_in_progress_.load()) {
        const int64_t now = now_ms_();
        const int64_t prev = last_status_fresh_read_ms_.load();
        if (now - prev >= 1000) {
            last_status_fresh_read_ms_.store(now);   // mark first to absorb concurrent callers
            // [2026-06-06] 30ms inter-slave gap — avoid burst of 9 consecutive
            // Modbus reads which saturates cli_22_ (gateway buffer overflow →
            // JC100 timeouts cascade across slaves). 9 × 30ms = 270ms total gap
            // is well under 1Hz rate-limit budget.
            bool first = true;
            for (int s = CUP_SLAVE_FIRST; s <= CUP_SLAVE_LAST; ++s) {
                if (disabled_zdt_slaves_.count(s)) continue;
                if (!first) sleep_ms_(30);
                first = false;
                int p = M_(s).read_pressure();
                if (M_(s).error_flag == 0) {
                    cached_pressure_[s - 1].store(p);
                    pressure_stale_[s - 1].store(false);
                } else {
                    // 讀失敗 → 保留舊值（維持既有行為），但記下這個值不新鮮。
                    pressure_stale_[s - 1].store(true);
                }
            }
        }
    }
    std::ostringstream oss;
    oss << "OK state=" << state_name(state_.load());
    oss << " crane_attached=" << (crane_attached_.load() ? "on" : "off");
    oss << " arm_attached="   << (arm_attached_.load()   ? "on" : "off");
    oss << " obstacle_detect=" << (obstacle_detect_enabled_.load() ? "on" : "off");
    oss << " follower_mode="  << (follower_use_imu_.load() ? "imu" : "meter");
    oss << " first_step="     << (first_step_right_.load() ? "right" : "left");
    oss << std::fixed << std::setprecision(1);
    for (int s = CUP_SLAVE_FIRST; s <= CUP_SLAVE_LAST; ++s)
        oss << " p" << s << "=" << cached_pressure_[s - 1].load();
    // [2026-08-28] 附加欄位：哪幾顆的壓力值是「上次讀取失敗後沿用的舊值」。
    // 🔴 沒有這個的話，`p5=0` 看起來就只是「沒吸住」，而它也可能是整條 .22 bus
    //    不通時回傳的快取 —— 2026-08-28d 的 changelog 記過有人（我）就這樣誤判過。
    //    刻意用獨立欄位而非改 p<N>= 的格式，避免打壞既有的解析。
    //    ⚠️ 沒有 p_err 欄位 ≠ 數值正確，只代表「最後一次讀取有成功」。
    {
        std::string stale;
        for (int s = CUP_SLAVE_FIRST; s <= CUP_SLAVE_LAST; ++s)
            if (pressure_stale_[s - 1].load()) {
                if (!stale.empty()) stale += ",";
                stale += std::to_string(s);
            }
        if (!stale.empty()) oss << " p_err=" << stale;
    }
    if (!imu_.read_error.load()) {
        // [2026-08-27 per user] IMU 改回水平安裝 → roll/pitch 用內建尤拉角
        // （同 imu_monitor_loop_ / imu_take_baseline_，三者定義必須一致）。
        oss << std::setprecision(2)
            << " roll=" << (imu_.x - imu_roll0_)
            << " pitch=" << (imu_.y - imu_pitch0_);
        // [2026-08-27 per user] IMU 改成立起來安裝（左側朝下）之後，尤拉角在
        // 垂直姿態下會撞 gimbal lock，而目前拿來當 roll 的 imu_.z 是 yaw——靠
        // 磁力計算出來的，會被馬達/鐵件磁場帶著漂（bench 觀察到同一姿態下
        // 7.83 → -71.43）。加速度三軸直接反映重力方向，不吃磁力計，是判斷
        // 實際安裝方位與重寫傾斜公式的可靠依據，所以一併輸出。
        // raw_* 是未扣基準的原始尤拉角，用來對照 gimbal lock 發生在哪一軸。
        oss << " ax=" << imu_.ax << " ay=" << imu_.ay << " az=" << imu_.az
            << " raw_x=" << imu_.x << " raw_y=" << imu_.y << " raw_z=" << imu_.z
            // [2026-08-27] 收到的封包類型計數——用來確認 IMU 到底有沒有送加速度
            // (0x51)。bench 觀察到 ax/ay/az 恆為 0 而角度有值，推測只送 0x53；
            // 這兩個計數可以直接證實，不必開 debug 看 hex dump。
            << " n_accel=" << imu_.n_accel_pkt.load()
            << " n_angle=" << imu_.n_angle_pkt.load();
    }
    // [2026-08-27] 傾斜保護狀態一定要出現在 status——關閉狀態若不可見，
    // 操作者可能在毫無保護的情況下把機器吊起來。
    oss << " imu_guard=" << (imu_guard_enabled_.load() ? "on" : "OFF_NO_PROTECTION");
    oss << "\n";
    return oss.str();
}

// [2026-07-09] Switch the follower (second-moving) side's leveling mode. See header.
std::string WashRobot::cmd_set_follower_mode(const std::string& mode) {
    if      (mode == "imu")   follower_use_imu_.store(true);
    else if (mode == "meter") follower_use_imu_.store(false);
    else return "ERR usage:set_follower_mode_<imu|meter>\n";
    std::cout << "[follower] second-leg leveling mode = " << mode << "\n";
    evt_("follower_mode " + mode);
    return "OK follower_mode=" + mode + "\n";
}

// [2026-07-09] Choose which foot leads the first step. See header.
std::string WashRobot::cmd_set_first_step(const std::string& side) {
    if      (side == "right") first_step_right_.store(true);
    else if (side == "left")  first_step_right_.store(false);
    else return "ERR usage:set_first_step_<left|right>\n";
    std::cout << "[gait] first step leads with " << side << " foot\n";
    evt_("first_step " + side);
    return "OK first_step=" + side + "\n";
}

std::string WashRobot::cmd_vacuum(const std::string& group, bool on) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    if (vacuum_valve_(group, on)) return "ERR vacuum_valve_fail\n";
    return "OK\n";
}

// Manual control of dp0105 vacuum pump motor (PQW CH1). Init/shutdown manage
// it automatically; this command lets the user toggle it from GUI for bench
// debug. Note: turning OFF mid-flow will starve all 9 cups → vacuum fail.
std::string WashRobot::cmd_pump(bool on) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    if (pqw_.controlRelay(CH_PUMP, on)) return "ERR pump_fail\n";
    return "OK\n";
}

// Manual relay controls for cleaning subsystem (used to manually shut down
// brush / water_pump / water_inlet when auto cleanup leaves them on, or when
// bench testing clean_sweep components individually).
std::string WashRobot::cmd_brush(bool on) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    if (pqw_.controlRelay(CH_BRUSH, on)) return "ERR brush_fail\n";
    return "OK\n";
}

// [2026-09-01 per user] 繼電器現況回讀。見 WASH_ROBOT.h 的宣告說明。
// 輸出所有 PQW_TOTAL_CH 個通道，並在已知用途的通道後面附上名稱，方便對照接線表。
std::string WashRobot::cmd_relay_status() {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);

    const std::vector<bool> st = pqw_.readAllStatus();
    if ((int)st.size() < PQW_TOTAL_CH) return "ERR relay_read_fail\n";

    std::ostringstream oss;
    oss << "OK";
    for (int ch = 1; ch <= PQW_TOTAL_CH; ++ch) {
        oss << " ch" << ch << "=" << (st[ch - 1] ? 1 : 0);
    }
    // 已知用途一併印出，讓這一行自己就能對照，不必去翻表。
    oss << " | names ch" << CH_VALVE_RIGHT  << "=valve"
        <<          " ch" << CH_PUMP_A       << "=pumpA"
        <<          " ch" << CH_PUMP_B       << "=pumpB(未啟用)"
        <<          " ch" << CH_BRUSH        << "=brush"
        <<          " ch" << CH_BREAK_VACUUM << "=正壓閥"
        <<          " ch" << CH_WATER_PUMP   << "=water_pump(未接管路)";
    oss << "\n";
    return oss.str();
}

// [2026-09-01 per user] 通用單通道繼電器控制（bring-up / 接線盤點用）。
//
// 🔴 狀態限制只放 Idle / Ready，理由見 WASH_ROBOT.h 的宣告說明：
// CH1 是唯一一顆真空閥、CH6 是破真空閥，吸盤可能承重時開放 raw 控制
// 等於提供一條讓機器脫落的捷徑。這不是保守，是這兩個通道的實際後果。
std::string WashRobot::cmd_relay(int ch, bool on) {
    if (ch < 1 || ch > PQW_TOTAL_CH) {
        std::ostringstream oss;
        oss << "ERR ch_out_of_range " << ch << " (1.." << PQW_TOTAL_CH << ")\n";
        return oss.str();
    }
    State cur = state_.load();
    if (cur != State::Idle && cur != State::Ready) return state_violation_(cur);

    std::cout << "[relay] CH" << ch << " → " << (on ? "ON" : "OFF") << "\n";
    if (pqw_.controlRelay(ch, on)) return "ERR relay_fail\n";

    // 立刻回讀該通道 —— 「送出成功」不等於「繼電器真的動了」。2026-07/08 的
    // CH_BRUSH 誤號事件（打到沒接東西的繼電器、log 完全看不出來）就是因為
    // 呼叫端沒有回讀也沒檢查回傳值。
    const std::vector<bool> st = pqw_.readAllStatus();
    if ((int)st.size() < ch) return "ERR relay_set_but_readback_fail\n";
    std::ostringstream oss;
    oss << "OK ch" << ch << "=" << (st[ch - 1] ? 1 : 0) << "\n";
    return oss.str();
}

std::string WashRobot::cmd_water_pump(bool on) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    if (pqw_.controlRelay(CH_WATER_PUMP, on)) return "ERR water_pump_fail\n";
    return "OK\n";
}

//=========== QX-DO24 PWM output (cli_22_ slave 6) ===========

// [2026-08-28 per user] 步伐用的占空比切換 —— 只寫「占空比」一個暫存器。
//
// 跟面板 (cmd_pwm_set) 走同一條 driver 路徑（QX_DO24::setPWM_Duty，driver 是
// true=成功 的反向慣例，見 QX_DO24.h 頂端 banner），差別只在面板連寫
// Freq -> Duty -> Control 三個，這裡刻意只寫中間那個：per user「只需寫入占空位
// 就好，其他不動」。頻率鎖 50Hz 與控制字 65535 都維持面板/保存值，這條路徑不去
// 動它們 —— 也因此 5~10% 的「停止/全速」對應只有在頻率已經是 50Hz 時才成立。
//
// 回傳沿用本檔慣例：true = 失敗、false = 成功。
// PWM_ENABLED=false 時視為成功並跳過：模組整個沒被驅動 = 本來就沒在輸出，沒有
// 東西需要關；讓它回失敗只會把步伐卡在一個不存在的裝置上。
bool WashRobot::pwm_set_duty_only_(double duty_pct, const char* why) {
    if (!PWM_ENABLED) {
        std::cout << "[pwm_step] skip (" << why << " duty=" << duty_pct
                  << "%) — PWM_ENABLED=false" << "\n";
        return false;
    }
    // [2026-08-28 per user] 重試 PWM_STEP_WRITE_TRIES 次。bench 上看過同一步裡
    // 開啟寫入 timeout、關閉寫入卻成功 —— 是偶發掉包不是接線問題，單次就放棄
    // 太脆弱。每次之間隔 PWM_STEP_WRITE_RETRY_MS，讓 gateway 的半雙工 RS485
    // 有時間把上一筆（可能遲到的）回覆吐完，下一次 atomic transaction 的 drain
    // 才不會又撞上它。
    for (int attempt = 1; attempt <= PWM_STEP_WRITE_TRIES; ++attempt) {
        if (pwm_.setPWM_Duty(PWM_STEP_CH - 1, duty_pct)) {          // driver API 是 0-based
            std::cout << "[pwm_step] " << why << " -> ch" << PWM_STEP_CH << " duty="
                      << std::fixed << std::setprecision(1) << duty_pct << "% OK"
                      << (attempt > 1 ? " (第 " + std::to_string(attempt) + " 次才成功)" : "")
                      << "\n";
            return false;
        }
        if (attempt < PWM_STEP_WRITE_TRIES) {
            std::cout << "[pwm_step] " << why << " 第 " << attempt << "/"
                      << PWM_STEP_WRITE_TRIES << " 次寫入失敗，" << PWM_STEP_WRITE_RETRY_MS
                      << "ms 後重試\n";
            sleep_ms_(PWM_STEP_WRITE_RETRY_MS);
        }
    }
    std::cerr << "[pwm_step] " << why << " -> ch" << PWM_STEP_CH << " duty="
              << std::fixed << std::setprecision(1) << duty_pct << "% FAILED ("
              << PWM_STEP_WRITE_TRIES << " 次全滅)\n";
    return true;
}

//
// ⚠ QX_DO24 uses the INVERTED return convention (true = success) — see the
//   banner at the top of QX_DO24.h. The checks below follow the driver.
//
// Write order is deliberately Freq -> Duty -> Control (same reasoning as
// QX_DO24::setChannel): duty is only meaningful once the frequency is right,
// and control goes last so the load never sees an intermediate state.
std::string WashRobot::cmd_pwm_set(int ch, int hz, int control, double duty_pct) {
    // [2026-08-28] 無條件先印一行「指令進來了」。bench 上遇到 GUI 送出 pwm set
    // 後 washrobot 完全沒回、console 也一個字都沒有，當時無法分辨是
    //   (a) 指令沒送到 washrobot（backend / binary 版本問題）
    //   (b) 送到了但在某個 early-return 就折返（driver 從沒被呼叫，所以沒有 log）
    //   (c) 送到了、driver 也跑了，只是回覆慢或被淹沒
    // 這行讓三者立刻分得開：沒印 = (a)；印了但沒有後續 driver log = (b)。
    std::cout << "[pwm_set] ch=" << ch << " hz=" << hz << " ctrl=" << control
              << " duty=" << duty_pct << " (enabled=" << (PWM_ENABLED ? "1" : "0")
              << " slave=" << PWM_SLAVE << ")\n";

    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    // PWM 停用時在這裡也擋一道，不是只靠 init 不做 —— driver 的 client 指標
    // 若被別的路徑設起來，寫入就會落到那個 slave 號的裝置上（2026-08-27 撞號
    // 事件正是這個風險的實例）。沿革見 WASH_ROBOT.h 的 PWM_ENABLED 註解。
    if (!PWM_ENABLED) {
        std::cout << "[pwm_set] REJECTED — PWM_ENABLED=false\n";
        return "ERR pwm_disabled\n";
    }
    if (ch < 1 || ch > 4)                 return "ERR pwm_channel_must_be_1_to_4\n";
    if (control < 0 || control > 65535)   return "ERR pwm_control_out_of_range\n";

    const int dch = ch - 1;                       // driver API is 0-based

    // 三個寫入各自留痕。driver 自己的 LOG_ERR 只在 debug_mode 開時才印，而且
    // 「沒回應」那條路徑以前完全靜默（2026-08-28b 才補上）——這裡無條件印，
    // 確保 console 一定看得到是卡在哪一步。
    // [2026-08-28] 錯誤訊息要分辨「參數超出安全範圍」與「通訊失敗」。
    // 先前三個分支都只有一句寫死的訊息，實測送**合法的 hz=50** 也會收到
    // 「頻率被鎖在 50Hz」—— 真因其實是模組沒回話。呼叫端只拿得到一個 bool，
    // 所以現在改讀 driver 的 last_fail()。
    // （同型問題 2026-08-28b 在 Linux_test menu 34 修過，當時漏了主程式這一處。）
    auto fail_reason = [this]() -> std::string {
        return std::string(pwm_.last_fail_str());
    };
    if (!pwm_.setPWM_Freq(dch, hz)) {
        std::cout << "[pwm_set] setPWM_Freq FAILED — " << fail_reason() << "\n";
        if (pwm_.last_fail() == QX_DO24::Fail::OutOfRange)
            return "ERR pwm_freq_rejected_locked_" + std::to_string(pwm_.freqMinHz()) + "hz\n";
        return "ERR pwm_freq_write_failed_" + fail_reason() + "\n";
    }
    if (!pwm_.setPWM_Duty(dch, duty_pct)) {
        std::cout << "[pwm_set] setPWM_Duty FAILED — " << fail_reason() << "\n";
        if (pwm_.last_fail() == QX_DO24::Fail::OutOfRange)
            return "ERR pwm_duty_rejected_must_be_" + std::to_string((int)pwm_.dutyMinPct())
                 + "_to_" + std::to_string((int)pwm_.dutyMaxPct() ) + "_pct\n";
        return "ERR pwm_duty_write_failed_" + fail_reason() + "\n";
    }
    if (!pwm_.setPWM_Control(dch, (uint16_t)control)) {
        std::cout << "[pwm_set] setPWM_Control FAILED — " << fail_reason() << "\n";
        return "ERR pwm_control_write_failed_" + fail_reason() + "\n";
    }

    // 🔴 回讀驗證。寫入回 true 只代表「模組收下了這一幀」，不代表暫存器真的是那個值。
    // 而這個裝置的失敗模式特別惡劣：**寫入失敗時輸出不會歸零，模組保持前一個值繼續輸出**
    // —— 也就是通訊斷掉時螺旋槳不會停，會維持轉速（左右螺旋槳共用 CH1）。
    // 所以「有沒有真的生效」必須用讀的確認，不能相信寫入的回傳值。
    {
        double   rb_duty = 0;
        uint32_t rb_freq = 0;
        uint16_t rb_ctrl = 0;
        const bool rb_ok = pwm_.getPWM_Duty(dch, rb_duty)
                        && pwm_.getPWM_Freq(dch, rb_freq)
                        && pwm_.getPWM_Control(dch, rb_ctrl);
        if (rb_ok) {
            // duty 容差 0.6%：模組存的是整數百分比，而呼叫端可以傳小數（例如 5.9）。
            const bool match = ((int)rb_freq == hz)
                            && (std::fabs(rb_duty - duty_pct) <= 0.6)
                            && ((int)rb_ctrl == control);
            if (!match) {
                std::cout << "[pwm_set] ⚠ 回讀不符：要求 hz=" << hz << " duty=" << duty_pct
                          << " ctrl=" << control << "，讀回 hz=" << rb_freq
                          << " duty=" << rb_duty << " ctrl=" << rb_ctrl << "\n";
                evt_("pwm_readback_mismatch ch=" + std::to_string(ch));
                return "ERR pwm_readback_mismatch\n";
            }
            std::cout << "[pwm_set] 回讀確認 ch" << ch << " duty=" << rb_duty
                      << " hz=" << rb_freq << " ctrl=" << rb_ctrl << "\n";
        } else {
            // 讀不到就不能宣稱成功 —— 這正是「寫入回 OK 但實際沒生效」會走的路徑。
            std::cout << "[pwm_set] ⚠ 回讀失敗（" << fail_reason() << "）— 無法確認是否生效\n";
            evt_("pwm_readback_unavailable ch=" + std::to_string(ch));
            return "ERR pwm_readback_failed_" + fail_reason() + "\n";
        }
    }
    std::cout << "[pwm_set] OK — 三個寫入都成功\n";

    // [2026-08-28] 回覆帶上實際寫進去的值，不要只回裸 "OK"。
    // 面板的行分派器認得 "ERR pwm_*"，但裸 OK 不符合它的任何 regex ——
    // 於是寫入成功時畫面完全靜止，使用者無從分辨「送出成功」和「被吞掉」。
    // 帶 pwm_set 前綴讓前端可以認，順便把生效的參數回顯出來。
    std::ostringstream ok;
    ok << "OK pwm_set ch=" << ch << " hz=" << hz
       << " ctrl=" << control << " duty=" << std::fixed << std::setprecision(1)
       << duty_pct << "\n";
    return ok.str();
}

// ⚠ Writes flash. Deliberately NOT called by any automatic flow — only the
// web panel's explicit 「保存參數」 button reaches here. See the warning block
// on QX_DO24::saveOutputAsDefault(): ~1-2k write-cycle life, the module accepts
// only ONE such write per power-up, and saving while control=65535 makes the
// module start driving the motor the instant it is powered on.
std::string WashRobot::cmd_pwm_save() {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    // PWM 停用時在這裡也擋一道，不是只靠 init 不做 —— driver 的 client 指標
    // 若被別的路徑設起來，寫入就會落到那個 slave 號的裝置上（2026-08-27 撞號
    // 事件正是這個風險的實例）。沿革見 WASH_ROBOT.h 的 PWM_ENABLED 註解。
    if (!PWM_ENABLED) return "ERR pwm_disabled\n";
    if (!pwm_.saveOutputAsDefault()) return "ERR pwm_save_fail\n";
    return "OK pwm_saved\n";   // 同上：讓面板認得出成功，不要靜默
}

// Reports all 4 channels for the panel. freq_ok=0 flags "frequency is not the
// locked value", which invalidates the 5~10% duty mapping — the module reverts
// to 1000Hz on every power cycle, so this is the normal state after a reboot.
std::string WashRobot::cmd_pwm_status() {
    // PWM 停用時在這裡也擋一道，不是只靠 init 不做 —— driver 的 client 指標
    // 若被別的路徑設起來，寫入就會落到那個 slave 號的裝置上（2026-08-27 撞號
    // 事件正是這個風險的實例）。沿革見 WASH_ROBOT.h 的 PWM_ENABLED 註解。
    if (!PWM_ENABLED) return "ERR pwm_disabled\n";
    std::ostringstream oss;
    oss << "OK";
    for (int ch = 1; ch <= 4; ++ch) {
        double   duty = 0; uint32_t freq = 0; uint16_t ctrl = 0;
        bool ok = pwm_.getPWM_Duty(ch - 1, duty)
               && pwm_.getPWM_Freq(ch - 1, freq)
               && pwm_.getPWM_Control(ch - 1, ctrl);
        oss << " ch" << ch << "=";
        if (!ok) { oss << "ERR"; continue; }
        oss << duty << "," << freq << "," << ctrl
            << "," << (((int)freq == pwm_.freqMinHz()) ? 1 : 0);
    }
    // [2026-08-31] 補 active_ch —— 上面把四個通道平鋪出來，但**只有 PWM_STEP_CH 是活的**
    // （per user 2026-08-31：左右兩顆風扇共用 CH1；CH2/3/4 沒接東西、不管）。
    // 🔴 為什麼值得多印這一欄：CH3 的模組端殘留值是 duty=11%、ctrl=65535（持續輸出），
    //    **超出本 driver 的 [duty_min, duty_max] = [5,10]**；CH4 是 50% / 1000Hz / ctrl=0。
    //    這兩個值**不可能是本軟體寫的**（setPWM_Duty 會拒絕範圍外的值，且 pwm_set_duty_only_
    //    只寫 PWM_STEP_CH），是廠商工具或改 slave 號（6→9）之前留下的模組端狀態。
    //    2026-08-31 有人（Claude）因為看到 ch3=11 超出上限而以為是缺陷，追了一輪才確認無害。
    //    ⚠️ 只**新增**欄位、不動既有欄位格式，避免打壞任何既有的解析。
    oss << " duty_min=" << pwm_.dutyMinPct()
        << " duty_max=" << pwm_.dutyMaxPct()
        << " freq_lock=" << pwm_.freqMinHz()
        << " active_ch=" << PWM_STEP_CH
        << " (ch2-4 unused: module-side residue, not written by this software)\n";
    return oss.str();
}

// [2026-06-05] Water inlet moved to crane PQW (192.168.1.34 slave 12 CH4).
// Internal helper — sends crane_cmd_("water_inlet on/off"). All washrobot
// callers (init / sweep flows / cmd_water_inlet / shutdown) route here.
// Bypasses state guard so motion-active paths can use it.
//
// [2026-06-09] Hardened against crane comm glitches:
//   1. Retry 3 times (500ms gap) on crane_cmd_ failure — short bus blips
//      shouldn't leave the valve stuck open. close (false) MUST succeed or
//      the watchdog (set_water_inlet_watchdog_loop_) eventually force-closes.
//   2. On successful open, stamp water_inlet_open_ts_ms_ so the watchdog
//      can detect overlong open windows (>WATER_INLET_OPEN_MAX_MS) and
//      auto-close.
//   3. On successful close, reset water_inlet_open_ts_ms_ to 0 (disarmed).
bool WashRobot::set_water_inlet_(bool on) {
    constexpr int RETRY_MAX    = 3;
    constexpr int RETRY_GAP_MS = 500;
    bool err = true;
    for (int attempt = 0; attempt < RETRY_MAX; ++attempt) {
        std::string reply = crane_cmd_(on ? "water_inlet on" : "water_inlet off");
        if (reply.rfind("OK", 0) == 0) { err = false; break; }
        std::cerr << "[water_inlet] " << (on ? "on" : "off")
                  << " attempt " << (attempt + 1) << "/" << RETRY_MAX
                  << " failed: " << reply;
        if (attempt < RETRY_MAX - 1) sleep_ms_(RETRY_GAP_MS);
    }
    if (err) {
        std::cerr << "[water_inlet] " << (on ? "OPEN" : "CLOSE")
                  << " gave up after " << RETRY_MAX << " attempts — valve state UNKNOWN\n";
        return true;
    }
    // Successful op — update watchdog tracker.
    if (on) {
        water_inlet_open_ts_ms_.store(now_ms_());
    } else {
        water_inlet_open_ts_ms_.store(0);   // disarmed
    }
    return false;
}

// [2026-06-09] Background watchdog: if water_inlet has been open for longer
// than WATER_INLET_OPEN_MAX_MS, force a close. Catches:
//   - detached refill threads killed mid-sleep (process exit / crash)
//   - sweep flows that opened valve then hit unhandled exception
//   - GUI user forgot to press OFF
// Stops on stop_ flag. Polls every 10s (cheap — only acts when overdue).
void WashRobot::water_inlet_watchdog_loop_() {
    while (water_inlet_watchdog_running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (!water_inlet_watchdog_running_.load()) break;
        const int64_t ts = water_inlet_open_ts_ms_.load();
        if (ts == 0) continue;   // disarmed (valve closed)
        const int64_t now = now_ms_();
        const int64_t open_ms = now - ts;
        if (open_ms <= WATER_INLET_OPEN_MAX_MS) continue;
        std::cerr << "[water_inlet_watchdog] valve open for " << (open_ms / 1000)
                  << "s > " << (WATER_INLET_OPEN_MAX_MS / 1000)
                  << "s — FORCE CLOSE\n";
        evt_("water_inlet_watchdog_force_close open_sec=" + std::to_string(open_ms / 1000));
        // set_water_inlet_(false) already has retry + resets ts_ms_ on success.
        // If it fails after retries, ts_ms_ stays armed → next tick will try again.
        set_water_inlet_(false);
    }
}

std::string WashRobot::cmd_water_inlet(bool on) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    if (set_water_inlet_(on)) return "ERR water_inlet_fail\n";
    return "OK\n";
}

// [2026-06-06] One-shot XKC-Y25 water level read for GUI manual refresh.
// Retry 3 次（對齊 do_arm_clean_sweep_ Phase A pattern）避免 cli_22_ bus
// 偶發 hiccup。回 "OK water_full=<0|1> rssi=<N>\n" 或 "ERR xkc_unreachable\n"。
std::string WashRobot::cmd_water_level() {
    uint16_t out = 0, rssi = 0;
    bool ok = false;
    for (int i = 0; i < 3; ++i) {
        if (!lvl_.read_state(out, rssi)) { ok = true; break; }
        if (i < 2) sleep_ms_(100);
    }
    if (!ok) return "ERR xkc_unreachable\n";
    std::ostringstream oss;
    oss << "OK water_full=" << out << " rssi=" << rssi << "\n";
    return oss.str();
}

std::string WashRobot::cmd_pusher(const std::string& group, const std::string& pos) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);

    // On emergency_stop abort: clear abort_flag and restore pre-cmd state.
    // Manual pusher ops don't leave mechanical state uncertain enough to
    // require Error-state clearance (unlike mid-step_down abort).
    auto on_abort = [this, cur]() -> std::string {
        abort_flag = false;
        set_state_(cur);
        return "ERR aborted\n";
    };

    if (pos == "retract") {
        // Pre-clear firmware motion queue on relevant slaves (disable_seal extend
        // may leave a residual "extend to preset+2cm" target — without clearing,
        // pusher_move_many_ below briefly resumes the old target → motor moves
        // forward before retracting). emergency_stop clears the queue.
        {
            auto pre_slaves = group_slaves_(group);
            for (int s : pre_slaves) Z_(s).emergency_stop(false);
            if (!pre_slaves.empty()) sleep_ms_(50);
        }

        // Release vacuum valve(s) before retracting — prevents ZDT stall
        // from cups still adhered to wall when valve hasn't been released.
        const std::string valve_ctx = "manual_pusher_" + group + "_valve_off";
        if (group == "all" || group == "feet") {
            if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_RIGHT, false); }, valve_ctx + "_right")) return on_abort();
            if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_LEFT,  false); }, valve_ctx + "_left"))  return on_abort();
        } else {
            const int valve_ch = group_valve_ch_(group);
            if (valve_ch >= 0) {
                if (try_or_pause_([this, valve_ch]() { return pqw_.controlRelay(valve_ch, false); }, valve_ctx)) return on_abort();
            }
        }
        // Poll until cups release (up to VACUUM_RELEASE_WAIT_MS), then retract.
        {
            const auto rel_slaves = group_slaves_(group);  // handles "all" too
            if (!rel_slaves.empty()) {
                if (try_or_pause_([this, &rel_slaves]() { return vacuum_wait_release_(rel_slaves, VACUUM_RELEASE_WAIT_MS); },
                                  "manual_pusher_" + group + "_vacuum_release")) return on_abort();
            }
        }

        // Two-stage retract: half → wait → full. [v2] "all"/"feet" = 4 cups {1,2,3,4}.
        auto slaves = group_slaves_(group);
        if (slaves.empty()) return "ERR unknown_group\n";
        const std::string ctx = "manual_pusher_" + group + "_retract";
        if (try_or_pause_([this, &slaves]() { return pusher_two_stage_retract_(slaves); }, ctx)) return on_abort();
        return "OK\n";
    }
    if (pos == "extend") {
        // Manual extend mirrors auto-cycle extend logic: per-slave start pulses
        // from last_seal_pulse_ + B compensation, vacuum early-stop, fine_tune
        // with obstacle detection. Caller (user) must ensure valve state is set.
        if (group == "all" || group == "feet") {
            auto all_g = group_slaves_(group);
            if (try_or_pause_([this, &all_g]() { return smart_extend_subset_("all", all_g); }, "manual_pusher_all_extend")) return on_abort();
            return "OK\n";
        }
        auto slaves = group_slaves_(group);
        if (slaves.empty()) return "ERR unknown_group\n";
        const std::string ctx = "manual_pusher_" + group + "_extend";
        if (try_or_pause_([this, group, &slaves]() { return smart_extend_subset_(group, slaves); }, ctx)) return on_abort();
        return "OK\n";
    }
    return "ERR expected_extend_or_retract\n";
}

// Manual single-slave ZDT extend / retract. Picks per-slave extend pulse based
// on which group the slave belongs to (feet=8cm / body upper=9.8cm /
// body lower=9.3cm / center=10cm). Retract always goes to 0 with full RPM.
// Acquires motion_mtx_; not allowed in Error / Running / Balancing states.
std::string WashRobot::cmd_zdt_pusher(int slave, const std::string& action) {
    // [2026-08-27] 吸盤 slave 改為 CUP_SLAVE_FIRST..LAST（5-8）；沿用舊的 1-4
    // 驗證會讓 GUI 的單支推桿控制／停用全部回 ERR invalid_slave。
    if (slave < CUP_SLAVE_FIRST || slave > CUP_SLAVE_LAST) return "ERR invalid_slave\n";
    if (disabled_zdt_slaves_.count(slave)) return "ERR slave_disabled\n";

    State cur = state_.load();
    if (cur == State::Error || cur == State::Running || cur == State::Balancing
        || cur == State::ReturningHome || cur == State::WaitingConfirm
        || cur == State::PausedOnError)
        return state_violation_(cur);

    std::lock_guard<std::mutex> lk(motion_mtx_);
    abort_flag = false;

    Z_(slave).release_stall_flag();   // pre-clear in case latched

    if (action == "extend") {
        // Use smart_extend_subset_ to align with auto step_down/run extend:
        // vacuum early-stop, fine_tune for obstacle / unsealed cup, last_seal_pulse_
        // persistence. Per-slave preset pulse + body delta picked inside helper.
        const std::string slave_group = (slave == ZDT_RF1 || slave == ZDT_RF2) ? "right"
                                                                               : "left";
        std::vector<int> single = {slave};
        std::cout << "[zdt_pusher] slave " << slave << " smart_extend group=" << slave_group << "\n";
        if (smart_extend_subset_(slave_group, single))
            return "ERR pusher_move_fail\n";
        return "OK\n";
    }
    if (action == "retract") {
        std::cout << "[zdt_pusher] slave " << slave << " retract → 0 (two-stage)\n";
        // Pre-clear any residual motion command in firmware queue (disable_seal
        // extend may leave a "extend to preset+2cm" target queued — without
        // this, the next pos_mode briefly resumes the old target → motor moves
        // forward briefly before retracting). emergency_stop clears the queue.
        Z_(slave).emergency_stop(false);
        sleep_ms_(50);
        // Pipelined two-stage retract, consistent with all group retract paths
        // — if this cup was sealed, single-stage fast retract risks ZDT stall
        // on lingering cup adhesion.
        std::vector<int> single = {slave};
        if (pusher_two_stage_retract_(single))
            return "ERR pusher_move_fail\n";
        return "OK\n";
    }
    return "ERR expected_extend_or_retract\n";
}

// Set current ZDT position as new zero for the given group (ZDT manual 3.1.3,
// Reg 0x000A). Caveat: should typically be called when pushers are physically
// at retracted hard-stop, otherwise subsequent abs-0 moves won't return to the
// real bottom. Group "all" hits feet+body+center (8+1=9 slaves).
std::string WashRobot::cmd_zdt_disable(int slave) {
    // [2026-08-27] 吸盤 slave 改為 CUP_SLAVE_FIRST..LAST（5-8）；沿用舊的 1-4
    // 驗證會讓 GUI 的單支推桿控制／停用全部回 ERR invalid_slave。
    if (slave < CUP_SLAVE_FIRST || slave > CUP_SLAVE_LAST) return "ERR invalid_slave\n";
    disabled_zdt_slaves_.insert(slave);
    std::cout << "[zdt_disable] slave " << slave << " excluded from group ops\n";
    return "OK\n";
}

std::string WashRobot::cmd_zdt_enable(int slave) {
    // [2026-08-27] 吸盤 slave 改為 CUP_SLAVE_FIRST..LAST（5-8）；沿用舊的 1-4
    // 驗證會讓 GUI 的單支推桿控制／停用全部回 ERR invalid_slave。
    if (slave < CUP_SLAVE_FIRST || slave > CUP_SLAVE_LAST) return "ERR invalid_slave\n";
    disabled_zdt_slaves_.erase(slave);
    std::cout << "[zdt_enable] slave " << slave << " re-included in group ops\n";
    return "OK\n";
}

// Manual operator intervention: release latched stall flag on all 9 ZDT slaves.
// Does NOT acquire motion_mtx_ → safe to call concurrently with step_down/run.
// Disabled slaves are skipped. Comm failures are counted but don't fail the cmd
// (operator can re-issue).
std::string WashRobot::cmd_zdt_release_stall() {
    int ok = 0, fail = 0, skipped = 0;
    for (int s = CUP_SLAVE_FIRST; s <= CUP_SLAVE_LAST; ++s) {
        if (disabled_zdt_slaves_.count(s)) { ++skipped; continue; }
        if (Z_(s).release_stall_flag()) ++fail; else ++ok;
    }
    std::cout << "[zdt_release_stall] ok=" << ok << " fail=" << fail
              << " skipped=" << skipped << "\n";
    std::ostringstream oss;
    oss << "OK released ok=" << ok << " fail=" << fail;
    if (skipped) oss << " skipped=" << skipped;
    oss << "\n";
    return oss.str();
}

std::string WashRobot::cmd_zdt_zero(const std::string& group) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);

    auto slaves = group_slaves_(group);
    if (slaves.empty()) return "ERR unknown_group\n";

    for (int s : slaves) {
        if (Z_(s).set_zero())
            return "ERR zdt_zero_fail slave=" + std::to_string(s) + "\n";
    }
    return "OK\n";
}

std::string WashRobot::cmd_return_home(int descent_cm) {
    if (descent_cm <= 0) return "ERR invalid_descent\n";
    State cur = state_.load();
    if (cur != State::Attached && cur != State::Paused && cur != State::Error)
        return state_violation_(cur);
    set_state_(State::ReturningHome);

    std::lock_guard<std::mutex> lk(motion_mtx_);
    abort_flag     = false;
    motion_active_ = true;

    auto fail = [this](const std::string& msg) -> std::string {
        motion_active_ = false;
        set_state_(State::Error);
        return msg;
    };

    // [v2 2026-07-07] No DM2J rail homing (feet/wheel rails removed).

    // 1. Water system off (brush / pump / inlet valve)
    pqw_.controlRelay(CH_BRUSH,       false);
    pqw_.controlRelay(CH_WATER_PUMP,  false);
    set_water_inlet_(false);   // [2026-06-05] → crane PQW (.34 slave 12 CH4)

    // 2. Break suction on both foot groups (right/left)
    pqw_.controlRelay(CH_VALVE_RIGHT, false);
    pqw_.controlRelay(CH_VALVE_LEFT,  false);

    // 3. Wait for all 4 cups to release. Poll-based — proceeds the moment
    //    pressures rise above DETACH_THRESHOLD_KPA, up to RETURN_VACUUM_RELEASE_MS.
    //    Wrapped in try_or_pause_: timeout drops into PausedOnError so operator
    //    can investigate (continue=re-poll / skip=force retract / stop=Error).
    {
        std::vector<int> all_cups = {ZDT_RF1, ZDT_RF2, ZDT_LF1, ZDT_LF2};
        if (try_or_pause_([this, &all_cups]() { return vacuum_wait_release_(all_cups, RETURN_VACUUM_RELEASE_MS); },
                          "return_home_vacuum_release"))
            return fail("ERR aborted\n");
    }
    if (check_abort_()) return fail("ERR aborted\n");

    // 4. Retract all 4 foot pushers — TWO-STAGE (half → wait → full).
    {
        std::vector<int> all_cups = {ZDT_RF1, ZDT_RF2, ZDT_LF1, ZDT_LF2};
        if (try_or_pause_([this, &all_cups]() { return pusher_two_stage_retract_(all_cups); },
                          "return_home_pusher_retract"))
            return fail("ERR aborted\n");
    }
    if (check_abort_()) return fail("ERR aborted\n");

    // 7. Vacuum pump off
    pqw_.controlRelay(CH_PUMP, false);

    // [arm rope protect TEMP 2026-05-21 — DISABLED 2026-05-22] stow arm before long pay_out
    //if (ensure_arm_center_for_rope_("return_home_pre_pay_out"))
    //    return fail("ERR arm_stow_failed\n");

    // 8-9. Crane pays out descent_cm to lower robot to ground.
    //      Long operation — use 300s timeout.
    std::ostringstream oss;
    oss << "pay_out " << descent_cm;
    std::string reply = crane_cmd_(oss.str(), 300);
    if (reply.rfind("OK", 0) != 0) return fail("ERR crane_pay_out_fail\n");

    motion_active_ = false;
    set_state_(State::Idle);
    return "OK return_home_done\n";
}

std::string WashRobot::cmd_reset() {
    State cur = state_.load();
    if (cur != State::Error) return state_violation_(cur);
    abort_flag       = false;
    pause_flag       = false;
    motion_active_   = false;
    imu_ask_pending_ = false;
    set_state_(State::Idle);
    return "OK reset\n";
}

// Soft recovery from Error: verify all 9 cups still sealed, then jump back
// to Attached without re-running init/attach (which would release vacuum and
// drop the robot if currently on the wall).
//
// Workflow:
//   step_down 失敗 → state Error → 使用者現場處理沒吸的 cup
//   → cmd_recover → 跑 vacuum_check_("all")
//     ├─ 全 9 顆都 sealed → state = Attached、回 OK，可繼續 step_down
//     └─ 有顆沒吸 → 回 ERR recover_vacuum_fail slaves=...，state 留在 Error
//
// 與 cmd_reset 的差別：reset 把 state 退到 Idle，要重跑 init / attach（會破真空）。
// recover 假設 cups 已經貼著、只跳過驗證直接到 Attached，不動任何硬體。
std::string WashRobot::cmd_recover() {
    State cur = state_.load();
    if (cur != State::Error) return state_violation_(cur);

    std::lock_guard<std::mutex> lk(motion_mtx_);
    std::cout << "[recover] verify vacuum on all 4 cups\n";
    // [2026-06-02] Re-enabled vacuum_check_. recover() bypass without verification
    // was unsafe — user could jump Error→Attached with cups not actually sealed,
    // and next step would release the "anchor" group onto nothing → shock load.
    // Escape if sensor lies: user must physically inspect + fix cup, OR cmd_reset
    // (Idle→init/attach, will break vacuum).
    auto fails = vacuum_check_("all");
    if (!fails.empty()) {
        std::ostringstream oss;
        oss << "ERR recover_vacuum_fail slaves=";
        for (size_t i = 0; i < fails.size(); ++i) {
            if (i) oss << ",";
            oss << fails[i];
        }
        oss << "\n";
        std::cout << "[recover] FAIL: " << oss.str();
        return oss.str();
    }
    std::cout << "[recover] all 4 sealed → Attached\n";
    abort_flag       = false;
    pause_flag       = false;
    motion_active_   = false;
    imu_ask_pending_ = false;
    set_state_(State::Attached);
    return "OK recovered\n";
}

// Manual realign trigger — also checks threshold (REALIGN_THRESHOLD_CM=1.0cm)
// like the auto trigger, so user pressing the button when drift is small results
// in a clear "skipped" message rather than running unnecessary motion.
// Allowed only when state ∈ {Attached, Paused, PausedOnError} (cups on wall).
// [v2 2026-07-08] Manual realign trigger — pull all 4 feet cups back to preset
// while sealed (see do_feet_realign_). No crane rope, no reseal, no alternating
// groups (only left+right feet exist now). force=false → keeps the seal pre-check.
std::string WashRobot::cmd_realign() {
    std::cout << "[realign] manual trigger (v2 feet-only, sealed retract to preset)\n";
    // Manual: run on any drift (no threshold gate), stand-alone (we own the lock).
    std::string err = do_feet_realign_(/*apply_threshold=*/false, /*caller_holds_lock=*/false);
    if (!err.empty()) {
        std::cout << "[realign] manual trigger FAIL: " << err;
        return err;   // already "ERR ...\n"
    }
    return "OK realign_done\n";
}

std::string WashRobot::cmd_ping() {
    return "OK pong\n";
}

std::string WashRobot::cmd_pause() {
    State cur = state_.load();
    if (cur != State::Running && cur != State::Balancing)
        return state_violation_(cur);
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        state_before_pause_ = cur;
    }
    pause_flag = true;
    set_state_(State::Paused);
    return "OK paused\n";
}

std::string WashRobot::cmd_resume() {
    State cur = state_.load();
    if (cur != State::Paused) return state_violation_(cur);
    State prev;
    { std::lock_guard<std::mutex> lk(state_mtx_); prev = state_before_pause_; }
    pause_flag = false;
    set_state_(prev);
    return "OK resumed\n";
}

//=========== PauseOnError ===========

// Block on PausedOnError state until user resolves via cmd_continue / cmd_skip
// or emergency_stop sets abort_flag. Returns the chosen PauseAction.
WashRobot::PauseAction WashRobot::await_user_intervention_(const std::string& context) {
    State prev = state_.load();
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        // Bug fix 2026-05-06: don't overwrite state_before_pause_ when prev is
        // already PausedOnError. This happens when realign Phase 2 stall set
        // PausedOnError directly (2026-05-06a), then a subsequent try_or_pause_
        // catches another error and calls this function — without the guard,
        // state_before_pause_ would be overwritten to PausedOnError, and
        // cmd_continue/cmd_skip would set state back to PausedOnError → infinite
        // loop, retry/skip buttons appear non-responsive. Keep the original
        // pre-pause state (set when first entering PausedOnError).
        if (prev != State::PausedOnError) {
            state_before_pause_ = prev;
        } else {
            std::cout << "[PAUSE-ON-ERROR] nested pause detected (state already "
                         "PausedOnError) — keep original state_before_pause_\n";
        }
    }
    pause_action_.store((int)PauseAction::None);
    set_state_(State::PausedOnError);
    evt_("error_pause context=" + context);
    std::cout << "[PAUSE-ON-ERROR] " << context
              << " — awaiting cmd_continue (retry) / cmd_skip / emergency_stop\n";

    while (state_.load() == State::PausedOnError && !abort_flag.load()) {
        sleep_ms_(POLL_INTERVAL_MS);
    }

    if (abort_flag.load()) {
        std::cout << "[PAUSE-ON-ERROR] aborted by emergency_stop\n";
        return PauseAction::Abort;
    }

    PauseAction a = (PauseAction)pause_action_.load();
    std::cout << "[PAUSE-ON-ERROR] resumed: "
              << (a == PauseAction::Retry ? "RETRY" :
                  a == PauseAction::Skip  ? "SKIP"  : "ABORT") << "\n";
    return a;
}

// User pressed 「繼續(重試)」 — retry the failed op.
std::string WashRobot::cmd_continue() {
    State cur = state_.load();
    if (cur != State::PausedOnError) return state_violation_(cur);
    pause_action_.store((int)PauseAction::Retry);
    State prev;
    { std::lock_guard<std::mutex> lk(state_mtx_); prev = state_before_pause_; }
    set_state_(prev);
    return "OK continue (retry)\n";
}

// User pressed 「略過此步」 — assume manual fix succeeded, treat as success.
std::string WashRobot::cmd_skip() {
    State cur = state_.load();
    if (cur != State::PausedOnError) return state_violation_(cur);
    pause_action_.store((int)PauseAction::Skip);
    State prev;
    { std::lock_guard<std::mutex> lk(state_mtx_); prev = state_before_pause_; }
    set_state_(prev);
    return "OK skip\n";
}

// Toggle whether washrobot drives the crane. Default ON.
//   on=true  → commands sent normally, watchdog active (timeout = abort)
//   on=false → crane_cmd_ becomes no-op, watchdog skips ping/timeout entirely
// Use case: bench testing without crane, or crane offline/manual mode.
std::string WashRobot::cmd_crane_attached(bool on) {
    bool prev = crane_attached_.exchange(on);
    if (prev != on) {
        if (on) {
            // Reset last-ok timestamp so we get a grace period after re-enable
            // (otherwise a stale timer from a long-detached period would
            // immediately trip the watchdog).
            crane_last_ok_ms_ = now_ms_();
            std::cout << "[crane] attached ON — commands resume, watchdog active\n";
            evt_("crane_attached on");
        } else {
            std::cout << "[crane] attached OFF — commands skipped, watchdog suspended\n";
            evt_("crane_attached off");
        }
    }
    return on ? "OK crane_attached=on\n" : "OK crane_attached=off\n";
}

// Toggle whether DM2J wheels (slave 2, 4) are physically present.
//   on=true  → init() retracts wheels + cmd_wheels / cmd_dm2j_group("wheels") active
//   on=false → all wheel ops become no-ops; bench without wheels can run cleanly

