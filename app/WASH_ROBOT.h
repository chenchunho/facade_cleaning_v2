#ifndef WASH_ROBOT_H
#define WASH_ROBOT_H

#include <iostream>
#include <string>
#include <vector>
#include <deque>
#include <set>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <cstdint>

#include "TCP_client.h"
#include "DM2J_RS570.h"
#include "ZDT_motor_control.h"
#include "FrameAnalyzer.h"
#include "JC_100_METER.h"
#include "PQW_IO_16O_RLY.h"
#include "QX_DO24.h"
#include "DY_500_weight_sensor.h"
#include "XKC_Y25_RS485.h"
#include "Serial_port.h"
#include "WT901BC_TTL.h"

// ============================================================================
//  WashRobot — vertical window-washing robot controller
//
//  Owns all hardware drivers, background threads, and motion logic.
//  main.cpp owns only the TCP command server and dispatch layer.
//
//  Return convention (matches project-wide rule): false = success, true = error.
//
//  cmd_*() methods return "OK ...\n" or "ERR ...\n" strings for the TCP client.
// ============================================================================
class WashRobot {
public:
    WashRobot();
    ~WashRobot();

    // EVT broadcast callback — injected by main.cpp.
    // Called from background threads; must be thread-safe (TCP_server::broadcast is).
    std::function<void(const std::string&)> evt_cb;

    // Publicly writable by dispatch (pause/resume/stop commands)
    std::atomic<bool> abort_flag;
    std::atomic<bool> pause_flag;

    //=========== init ===========

    // Connect to all hardware, init drivers, start background threads.
    // false = success, true = fatal error.
    bool init();

    // Stop background threads and join them. Call before destroying or on process exit.
    void stop();

    //=========== commands ===========

    std::string cmd_init();
    std::string cmd_attach();
    std::string cmd_detach();
    std::string cmd_step_down(int cm = 0);   // cm = 0 → use current step_cm_; cm > 0 → validate 5..60, override
    std::string cmd_step_up(int cm = 0);     // mirror of step_down — feet phase first, body phase second; for ascending
    // [2026-07-13 per user] 跨障礙物 — step that stands legs off the wall to 2×preset
    // to clear a protruding obstacle, crosses, then realigns back to normal length.
    std::string cmd_cross_obstacle_down(int cm = 0);
    std::string cmd_cross_obstacle_up(int cm = 0);
    // [2026-07-22 per user] 同步步伐 — 跟 cmd_step_down/up 的交替 inchworm 走法不同：
    // 4 顆吸盤同時放開＋縮回、吊機兩側同步放/收繩（crane 端既有 pay_out/retract 雙繩同步）、
    // IMU 差動微調水平、4 顆一起重新伸出吸附。純移動，不含清洗。
    // ⚠ 安全性質跟其他重複執行的步伐不同：放繩期間 4 顆全放開，完全靠吊機繩索承重，
    // 沒有任何吸盤錨定在牆上（其他步伐永遠保持至少一側黏牆防墜）。使用者已確認此設計。
    std::string cmd_step_down_sync(int cm = 0);
    std::string cmd_step_up_sync(int cm = 0);
    std::string cmd_step_up_with_sweep(int cm = 0);   // step_up + continuous cleaning sweep in parallel (2026-05-22)
    std::string cmd_step_down_with_sweep(int cm = 0); // step_down + continuous cleaning sweep in parallel (2026-05-22)
    std::string cmd_step_up_sweep_after_feet(int cm = 0);   // step_up + 1 round sweep launched after feet rail DM2J done (2026-05-22)
    std::string cmd_step_down_sweep_after_feet(int cm = 0); // step_down + 1 round sweep launched after feet rail DM2J done (2026-05-25)
    // step_*_sweep_before_after (2026-05-27): 1 round sweep BEFORE feet rail
    // DM2J move (joined before rail moves) + 1 round AFTER feet rail move (joined
    // before step returns). 兩 round 都是 max_rounds=1。pre-round 跟「破真空 +
    // 收推桿」並行,post-round 跟「body extend + crane」並行。
    std::string cmd_step_up_sweep_before_after(int cm = 0);
    std::string cmd_step_down_sweep_before_after(int cm = 0);
    // [2026-07-23 per user] gait = "alt" (預設，向下相容既有呼叫) | "sync"。
    // alt  → do_step_down_/do_step_up_ 交替走法（每步换边、cycle_group_ retry）
    // sync → do_step_sync_ 同步走法（4 顆同時放開/放繩/重伸，見 do_step_sync_ 註解）
    std::string cmd_run(int steps, int cm = 0, const std::string& direction = "down",
                        const std::string& gait = "alt");   // direction = "down" | "up"

    // [2026-06-05] Scripted run — CSV of per-step cm values, fixed down_sweep_af
    // direction. Mirrors cmd_run's is_down_sweep path exactly; each step calls
    // cmd_step_down_sweep_after_feet(cm) with the per-iter cm. CSV supports `*`
    // repeat shorthand (e.g. "30*5,20*3" = 5×30 + 3×20). See scripted_run_plan.md.
    // [2026-07-23 per user] gait = "alt" (預設，向下相容) | "sync" — same meaning
    // as cmd_run's gait param, applied to every non-cross step in the script.
    // cross steps always use do_cross_obstacle_ regardless of gait (no sync
    // variant of cross-obstacle exists).
    std::string cmd_run_script(const std::string& csv, bool up = false, const std::string& gait = "alt");
    // Named-script management (persisted to ./scripts.json key=value format).
    std::string cmd_save_script(const std::string& name, const std::string& csv);
    std::string cmd_list_scripts();
    std::string cmd_load_script(const std::string& name);
    std::string cmd_delete_script(const std::string& name);
    std::string cmd_run_saved(const std::string& name, bool up = false, const std::string& gait = "alt");   // up=false → down (default); gait see cmd_run_script

    std::string cmd_arm_sweep();  // public: acquires motion_mtx_
    std::string cmd_tilt_mode(bool on);

    // [2026-08-27 per user] 單獨重取 IMU 水平基準，不跑完整 init。
    // 背景：imu_take_baseline_() 原本只有 cmd_init_impl_() 一個呼叫點，想校正
    // IMU 就得連帶做推桿歸零 / 手臂 INIT 等一整串硬體動作。而基準沒校好時
    // imu_monitor_loop_ 會把靜止的機器判成 45°+ 傾斜 → set_state_(Error) →
    // 幾乎所有指令被 state_violation_ 擋掉，形成「要校正卻先被擋住」的死結。
    // 本指令刻意**不做 state 檢查**，就是為了能從那個狀態自救。
    std::string cmd_imu_zero();

    // [2026-08-27 per user] 開關 IMU 傾斜保護（見 imu_guard_enabled_ 的說明）。
    // 同樣不檢查 state——要關掉保護的時機，正好就是已經被誤報打進 Error 的時候。
    std::string cmd_imu_guard(bool on);
    std::string cmd_emergency_stop();
    std::string cmd_shutdown();
    std::string cmd_status();
    std::string cmd_vacuum(const std::string& group, bool on);
    std::string cmd_pump(bool on);                       // dp0105 vacuum pump (PQW CH1)
    std::string cmd_brush(bool on);                      // arm roller brush motor (PQW CH5)

    // QX-DO24 PWM (cli_22_ slave 6). Web panel "PWM 控制" 用。
    //   cmd_pwm_set : 暫存寫入 —— 寫 0x00~0x0F，斷電即還原，可無限次改
    //   cmd_pwm_save: 保存參數 —— 寫 flash 0x10，⚠ 壽命 1~2 千次、每次上電
    //                 只能一次，且若當下 control=65535 會變成「插電就輸出」
    //   cmd_pwm_status: 讀回 4 通道現況給面板顯示
    // 占空比只收 5.0~10.0（5%=停止 / 10%=全速），頻率鎖 50Hz —— 兩個限制都
    // 由 QX_DO24 driver 強制，這層只做參數解析與回覆字串。
    std::string cmd_pwm_set(int ch, int hz, int control, double duty_pct);
    std::string cmd_pwm_save();
    std::string cmd_pwm_status();
    std::string cmd_water_pump(bool on);                 // water tank pump (PQW CH6)
    std::string cmd_water_inlet(bool on);                // water inlet ball valve [2026-06-05 控制權移到 crane PQW (.34 slave 12 CH4)]
    std::string cmd_water_level();                       // XKC-Y25 一次性讀取水位 (2026-06-06)
    std::string cmd_pusher(const std::string& group, const std::string& pos);
    std::string cmd_zdt_pusher(int slave, const std::string& action);   // single-slave manual extend/retract (slave 1..9)
    std::string cmd_zdt_zero(const std::string& group);   // "feet"|"body"|"center"|"all" — set current ZDT pos as new zero (manual 3.1.3)
    std::string cmd_zdt_disable(int slave);  // exclude slave 1..9 from all group ZDT ops (e.g. not yet installed)
    std::string cmd_zdt_enable(int slave);   // re-include previously disabled slave
    std::string cmd_zdt_release_stall();     // release stall flags on all 9 ZDT slaves (operator manual intervention; safe during motion)
    std::string cmd_move(const std::string& motor, double cm);
    std::string cmd_wheels(const std::string& action);   // "retract" = abs 0, "lower" = abs -6
    std::string cmd_dm2j_group(const std::string& group, double cm);   // "feet" | "wheels", abs cm
    std::string cmd_dm2j_zero(const std::string& group);                // "feet" | "wheels" | "arm" — set current pos as new zero
    std::string cmd_confirm_balance(const std::string& ans);
    std::string cmd_return_home(int descent_cm);
    std::string cmd_reset();
    std::string cmd_recover();   // Error → Attached after verifying all 9 cups still sealed
    std::string cmd_realign();   // manual trigger of feet realign (force=true; runs regardless of drift threshold)
    std::string cmd_ping();
    std::string cmd_pause();
    std::string cmd_resume();
    std::string cmd_continue();   // resume from PausedOnError = retry the failed op
    std::string cmd_skip();       // resume from PausedOnError = skip (assume manual fix)
    std::string cmd_crane_attached(bool on);   // toggle whether washrobot drives the crane
    // toggle whether DM2J wheels (slave 2, 4) are present. OFF = init() skips
    // wheel retract + cmd_wheels / cmd_dm2j_group("wheels") become no-op (bench
    // without wheels won't trigger Modbus timeouts → PausedOnError).
    std::string cmd_wheels_attached(bool on);

    // ---- cleaning arm (damiao motors via separate motor_api service, TCP 9527) ----
    std::string cmd_arm_init();                                      // INIT — enable + tool-head calibration
    std::string cmd_arm_deploy(int wall_mm, const std::string& slot); // DEPLOY <mm> <LEFT|CENTER|RIGHT>
    std::string cmd_arm_park();                                       // PARK — return + disable
    std::string cmd_arm_status();                                     // STATUS — relay arm state line
    std::string cmd_arm_attached(bool on);                            // toggle whether washrobot drives the cleaning arm
    // Cleaning routine — water + brush ON, DEPLOY arm to wall_mm, run `rounds`
    // sweeps where each sweep = 上滑台(DM2J_ARM) right → arm M2 LR_SLOT RIGHT
    // → 上滑台 left → M2 LR_SLOT LEFT → 上滑台 center → M2 LR_SLOT CENTER.
    // RAII guarantees water/brush OFF + arm PARK on every exit path.
    std::string cmd_arm_clean_sweep(int wall_mm, int rounds);

    // [2026-08-26 per user] 乾式清洗 — bench 測試用：完整的 DEPLOY + 滾筒 + 上滑台
    // + PARK 動作，但**不噴水、也不移動機器人**。
    // 直接復用 do_step_sync_rail_sweep_()——那正是同步步伐內建的清洗段，本來就沒有
    // 任何水路動作（水閥/水泵尚未接管路）。刻意不另寫一份邏輯：測到的就是實際會跑
    // 的那段，不會出現「測試版跟正式版行為不同」的問題。
    // 對照其他入口：cmd_arm_clean_sweep 會噴水；cmd_arm_sweep 不 DEPLOY（手臂不貼牆、
    // 滾筒空轉）；步伐的 ⇅ 同步 會真的走一步。
    // 牆距沿用 DM2J_ARM_STEP_SWEEP_WALL_MM（與步伐內建清洗完全一致），不開放參數。
    std::string cmd_arm_clean_sweep_dry();   // public: acquires motion_mtx_

    //=========== camera obstacle detection ===========

    // [2026-06-01] Toggle camera-based window-frame/sill obstacle detection.
    // Default OFF (testing-friendly — no impact to existing step_down flow).
    // ON: future FrameAnalyzer will run pre-step check, may override step_cm.
    // Reply format aligned with cmd_arm_attached / cmd_crane_attached so GUI
    // can use the same regex.
    std::string cmd_obstacle_detect(bool on);

    // [2026-06-04] Single-shot obstacle check: run obstacle_combine.py on the
    // four currently-cached frame paths (/tmp/cam{3,4}_{before,after}.jpg).
    // Caller is responsible for capturing those frames beforehand (typically
    // via bench_capture_motion.sh or the upcoming cmd_run_avoid loop).
    // Returns: "OK action=<a> step_cm=<n> reason=<...>" or "ERR <reason>".
    std::string cmd_obstacle_check();

    // [2026-06-04] RUN with obstacle avoidance.
    // Loop:
    //   1. Snap current frames as "before" (cp /tmp/cam{3,4}_latest.jpg)
    //   2. Crane probe: retract 1cm + sleep 1s + pay_out 1cm — generates
    //      camera position offset needed for motion-parallax detection
    //   3. Snap current frames as "after"
    //   4. Run obstacle_combine.py (via FrameAnalyzer)
    //   5. Broadcast EVT obstacle_ask, set obstacle_ask_pending_=true
    //   6. Wait for cmd_obstacle_response (or emergency_stop / timeout)
    //   7. If confirmed: do_step_down_(step_cm from detector)
    //      If cancelled / blocked: break loop
    //   8. Repeat
    std::string cmd_run_avoid();

    // GUI sends 1=confirm, 0=cancel. Releases the run_avoid wait loop.
    std::string cmd_obstacle_response(int v);

    // run_avoid synchronization (set by run_avoid loop, cleared by response/abort)
    std::atomic<bool> obstacle_ask_pending_{false};
    std::atomic<int>  obstacle_user_response_{-1};  // -1=pending, 0=cancel, 1=confirm
    static constexpr int OBSTACLE_ASK_TIMEOUT_S = 300;  // 5 min before auto-abort

    // [2026-07-20] D435i depth-camera continuous obstacle-avoid walk (v2 —
    // BEFORE/AFTER captures bracket the whole step call, not step-internal
    // hooks, so they work the same regardless of which gait engine runs
    // inside). [2026-07-28 per user] normal steps now call do_step_sync_
    // (both sides move together) instead of do_step_down_'s alternating
    // inchworm gait — see cmd_run_depth_avoid's step loop; the auto
    // cross-obstacle branch is unchanged (still do_cross_obstacle_).
    // Per Sadie's design (2026-07-20):
    //   - Every step (including the first, fixed at DEPTH_AVOID_FIRST_STEP_CM)
    //     captures before/after via the hooks during ITS OWN tail motion, then
    //     that step's result is shown to the user before deciding the NEXT step
    //     (act-then-review, simpler than v1's decide-before-acting bootstrap).
    //   - No automatic step_cm suggestion — user decides every time, optionally
    //     typing a custom cm instead of the current default step_cm_.
    //   - candidates whose height_cm > DEPTH_BIG_OBSTACLE_HEIGHT_CM additionally
    //     get a photo shown (served via /snap/depth, see depth_cam_service.py).
    // Loop: step -> depth_cam AFTER result -> EVT depth_obstacle_result ->
    //       wait cmd_depth_avoid_continue(cm) / cmd_depth_avoid_stop() -> repeat.
    std::string cmd_run_depth_avoid();
    // GUI: user typed/kept a cm value and pressed Continue. Validates
    // STEP_CM_MIN..STEP_CM_MAX same as cmd_step_down.
    std::string cmd_depth_avoid_continue(int cm);
    // GUI: user pressed Stop — end the loop after this point.
    std::string cmd_depth_avoid_stop();

    // Reuses obstacle_ask_pending_ / obstacle_user_response_ above for the
    // wait — response value doesn't carry the cm (continue always ships one
    // via depth_avoid_next_step_cm_), so 1=continue(see next_step_cm_), 0=stop.
    std::atomic<int> depth_avoid_next_step_cm_{0};
    std::atomic<int> depth_last_candidates_{0};
    std::atomic<double> depth_last_max_height_cm_{0.0};
    std::atomic<double> depth_last_max_protrusion_cm_{0.0};
    // [2026-07-21] Raw slant range (camera optical axis -> closest point of
    // the closest candidate) from depth_cam_service.py, plus the along-
    // travel remaining-clearance figure derived from it — see
    // cmd_run_depth_avoid's min_distance_cm parsing for the trig.
    std::atomic<double> depth_last_min_distance_cm_{0.0};
    std::atomic<double> depth_last_remaining_travel_cm_{0.0};
    static constexpr int    DEPTH_AVOID_FIRST_STEP_CM     = 5;    // fixed first step (per user 2026-07-20) — no prior frame data before this
    static constexpr double DEPTH_BIG_OBSTACLE_HEIGHT_CM  = 10.0; // candidate height_cm above this -> attach photo, per user spec
    // [2026-07-21] Camera mount geometry — used to convert
    // depth_cam_service.py's raw slant-range reading into "how much further
    // can the robot travel before its leading edge reaches the obstacle",
    // along the direction of travel:
    //   horizontal_cm   = sqrt(min_distance_cm^2 - DEPTH_CAM_STANDOFF_CM^2)
    //   remaining_cm    = horizontal_cm - DEPTH_CAM_LEAD_OFFSET_CM
    // (right-triangle: camera sits DEPTH_CAM_STANDOFF_CM perpendicular off
    // the wall, tilted down/forward; horizontal_cm is the projection of the
    // slant range onto the wall along the tilt/travel direction.)
    //
    // [2026-07-21] Original measurement: standoff 50cm + tilt ~35° ->
    // predicted slant range ~61cm, matched a real bench reading of 62.3cm —
    // confirmed the STANDOFF/tilt geometry (the d/H relationship) was right.
    //
    // [2026-07-23] LEAD_OFFSET_CM was still off by ~2x: with a real
    // candidate at center_distance_m=61.5cm (see depth_reflection_bench.py's
    // center_distance_m — the earlier near_m-based reading's off-axis-pixel
    // bug was already fixed by then), the formula gave remaining_travel_cm
    // =19.8cm with the old LEAD_OFFSET_CM=16, but the user's own on-site
    // tape measurement of camera-to-actual-sucker-leading-edge was only
    // 3-4cm at that point — solving backward (35.8 - remaining ≈ offset)
    // pointed at ~32cm, not 16. User re-measured and confirmed: the correct
    // leading-edge offset is 32cm (the original 16cm likely measured to
    // some other reference point, not the true sucker leading edge). Same
    // re-measurement also updated the standoff itself, 50cm -> 56cm — both
    // constants below are the corrected 2026-07-23 measurements.
    static constexpr double DEPTH_CAM_STANDOFF_CM    = 56.0; // camera height above wall, perpendicular
    static constexpr double DEPTH_CAM_LEAD_OFFSET_CM = 32.0; // robot leading edge (actual sucker front) is this much CLOSER to the wall-ahead than the camera mount

    // [2026-07-22] Cross-obstacle step suggestion, per user spec: when the
    // normal remaining clearance is too tight to keep taking small steps
    // (< DEPTH_AVOID_LOW_CLEARANCE_CM), suggest one bigger step that clears
    // the WHOLE obstacle instead — near edge to obstacle + the obstacle's
    // own thickness along the travel direction (candidate height_cm, "how
    // much the sill occupies along the path") + a full sucker diameter (so
    // the NEXT sucker placement lands with full contact area past the far
    // edge, not straddling it) + a small safety margin. Clamped to
    // STEP_CM_MAX — never suggest more than the robot can physically step.
    // Suggestion only (fills the GUI's default next-step-cm field) — per
    // the 2026-07-20 "no automatic step_cm suggestion" design still in
    // force, the user can always type a different value before Continue.
    static constexpr double DEPTH_AVOID_LOW_CLEARANCE_CM = 20.0; // remaining_travel_cm below this -> suggest crossing instead of another small step
    static constexpr double DEPTH_AVOID_SUCKER_DIAMETER_CM = 20.0; // 吸盤直徑
    static constexpr double DEPTH_AVOID_CROSS_MARGIN_CM = 5.0;     // extra safety buffer on top of near+thickness+sucker

    // [2026-06-04] Step shortfall tracking for vacuum_retry compensation.
    // do_step_down_ writes after Phase A complete; cmd_run_avoid reads to add
    // missed cm to next step's planned distance.
    std::atomic<double> last_step_planned_cm_{0.0};
    std::atomic<double> last_step_achieved_cm_{0.0};

    // [2026-06-04] First-step bootstrap probe — body 2cm out + return, captures
    // before/after frames so iter 1 of run_avoid has detector input.
    // Uses same patterns as step_down (two-stage retract, disable_seal extend).
    // Feet stay sealed throughout (machine doesn't fall).
    std::string do_obstacle_probe_(std::function<void()> cap_before,
                                   std::function<void()> cap_after,
                                   int probe_cm = 2);

    //=========== balance calibration ===========

    // [2026-06-02] Static-balance calibration sequence:
    //   Phase 1: preload — crane sync retract until soft-stop tension hit (uses
    //            crane's current g_retract_tension_stop_kg, same knob as realign).
    //            20s timeout → abort.
    //   Phase 2: body release — body valve OFF + ZDT(5,6,7,8) retract to 0.
    //   Phase 3: feet+center release — feet+center valve OFF + ZDT(1,2,3,4,9)
    //            retract to 0. After: robot fully hanging on ropes.
    //   Phase 4: auto balance loop — read IMU roll, pulse crane up_left / up_right
    //            briefly until |roll| < BAL_CAL_ROLL_TOL_DEG. Watchdog aborts
    //            on tension < BAL_CAL_TENSION_MIN_KG or |roll| > BAL_CAL_ROLL_PANIC_DEG.
    //   Phase 5: (separate cmd) user reviews result + presses RECORD → write
    //            SD76_L - SD76_R into static_roll_offset_cm setting.
    //
    // ⚠ DOES NOT auto re-attach. User must manually cmd_attach when ready.
    // ⚠ Phase 6 (use offset in crane balance) NOT implemented — value is recorded
    //   but does NOT take effect until crane integration done.
    //
    // State transitions: Attached → Calibrating → Idle (success, cups off) or
    //                                          → PausedOnError (abort/watchdog).
    std::string cmd_balance_calibrate_start();
    std::string cmd_balance_calibrate_record();
    std::string cmd_balance_calibrate_abort();
    std::string cmd_balance_calibrate_status();   // GUI poll: phase + readings

    //=========== runtime settings (wall-tune) ===========

    // [2026-05-29] Runtime-tunable wall-tune settings — see Settings struct.
    // get: dump all keys + current + default values (one per line, space-sep).
    // set: requires state==Idle (otherwise ERR busy). Validates key + value.
    // save: persist current values to settings.json (working dir).
    std::string cmd_get_settings();
    std::string cmd_set_setting(const std::string& key, const std::string& value);
    // [2026-07-09] Switch the follower (second-moving) side's leveling mode:
    //   "imu"   → second leg IMU fine-levels to the datum (策略1)
    //   "meter" → second leg meter-syncs only (方案B, original method)
    std::string cmd_set_follower_mode(const std::string& mode);
    // [2026-07-09] Choose which foot leads the first step: "left" | "right".
    std::string cmd_set_first_step(const std::string& side);
    std::string cmd_save_settings();
    // Public init-time wrapper: load settings.json if present (overrides defaults).
    // Called by main.cpp before robot.init(). Returns true on file I/O error
    // (file absence is NOT an error — silent fallback to defaults).
    bool        load_settings_at_boot(const std::string& path = "settings.json");

    //=========== state ===========

    enum class State {
        Idle,            // post-init, awaiting cmd_init (Phase 2)
        Ready,           // Phase 2 done, awaiting attach
        Attached,        // Phase 3 done, 9 cups holding
        Running,         // Phase 4 step_down / run in progress
        WaitingConfirm,  // balance_ask fired, awaiting confirm_balance
        Paused,          // user-paused during Running / Balancing
        PausedOnError,   // auto-flow op failed; awaiting cmd_continue (retry) / cmd_skip / cmd_emergency_stop
        Balancing,       // Phase 5 roll correction running
        ReturningHome,   // Phase 6 return_home running
        Calibrating,     // [2026-06-02] balance calibration (Phase 1-4) running; ends Idle (cups off) on success
        Error            // hard fault — only status / ping / reset / return_home allowed
    };

    enum class PauseAction { None = 0, Retry = 1, Skip = 2, Abort = 3 };

    State get_state() const { return state_.load(); }
    static const char* state_name(State s);

    // ---- 吸盤/推桿 slave 編號（public：指令分派器也要用）--------------------
    // [2026-08-27 per user] 4 顆吸盤的 slave ID 由 1-4 改為 5-8。
    // ZDT 推桿與 JC100 真空表共用同一組編號（推桿 slave N 末端的吸盤 = 真空表
    // slave N），兩者分別掛在 .20 / .22 兩條 bus 上，所以同號不衝突。
    // 這兩個常數是唯一的真實來源 —— 所有遍歷吸盤的迴圈都吃它們。
    // 🔴 [2026-08-28] 從 private 移到 public：`facade_cleaning_v2/main.cpp` 的
    //    分派器拿不到它們，只好自己寫死 `1..4`，於是 08-27 改號之後
    //    `zdt_pusher` / `zdt_disable` / `zdt_enable` **三個指令全部不可能成功**
    //    （分派器只收 1-4、應用層只收 5-8，兩個範圍沒有交集）。
    //    應用層那三處的註解甚至預言了這個後果，卻沒發現分派器有同一個檢查。
    //    **同一個範圍寫在兩個地方 = 遲早分岔；改成共用常數才是修法。**
    // ---- 推桿機構標定（2026-08-28 實機拿尺量）--------------------------------
    // 🔴 **3000 脈衝 = 1 cm**。這個值推翻了程式先前的「更正」——
    //    `cm_to_pulses_for_slave_` 自 2026-08-27 起對 feet 用 `20000/7 = 2857`，
    //    並在註解宣告 3000 是「多 5%、靜默算錯」。**實測顯示相反。**
    //
    // 五條互相獨立的證據全部指向 3000：
    //   1. 尺量：`zdt_pusher 5 extend` 走到 47994 脈衝，量得 **16 cm** → 2999.6
    //   2. 角度回讀：47994 脈衝 / 4800.85° = 10.00 脈衝/度
    //   3. 收回：300 脈衝 / 29.77° = 10.08 脈衝/度（同上）
    //   4. 本檔既有註解「SMC LEYG25 20cm = 6000°」→ 300°/cm × 10 脈衝/度 = 3000
    //   5. 致動器規格 200mm：3000 → 60000 脈衝 = 20cm ✅；
    //      2857 → 60000 脈衝 = 21cm ❌ **超出實體行程**
    //
    // 📌 `20000 = 7cm` 很可能是量在 **v1 的 body 推桿**上，2026-08-27 重構時
    //    被錯誤套用到 feet。**「更正」本身也需要被驗證。**
    // 🔴 這個常數是唯一真實來源：`cm_to_pulses_for_slave_` 與
    //    `do_feet_realign_` 都吃它，不要再各自寫死。
    static constexpr double CUP_PULSE_PER_CM = 3000.0;

    static constexpr int CUP_SLAVE_FIRST = 5;
    static constexpr int CUP_SLAVE_LAST  = 8;

private:
    //=========== constants ===========

    // [v2 2026-07-08] Two RS485 gateways only: .20 (ZDT pushers 1-4) + .22
    // (JC100/PQW/arm-rail/XKC/DY500). v1's .21 (IP_485_2) bus retired — ZDT
    // moved to .20 (freed by removing DM2J feet/wheel rails).
    static constexpr const char* IP_485_1   = "192.168.1.20";
    static constexpr const char* IP_485_3   = "192.168.1.22";
    static constexpr int         PORT_485   = 4001;

    // CRANE_IP: 2026-05-08 set to 192.168.5.26 for current bench network.
    // History: was "192.168.1.101" (formal Crane_control_PI deploy IP), then
    // test-mode "192.168.5.26" / "127.0.0.1" (easy crane shim) earlier rounds —
    // see changelog 2026-04-21e / 2026-04-24ao / 2026-05-07. Tension query goes
    // via crane_cmd_("tension"). Restore to 192.168.1.101 for production deploy.
    // [2026-08-03 per user] crane Pi 實際在 .27，不是 .26 — 這很可能就是那次
    // "reconnect failed + crane 端完全沒收到任何連線" 的真正原因（一直敲錯 IP 的門）。
    // [2026-08-26 per user] bench crane Pi 換到 .17。
    static constexpr const char* CRANE_IP   = "192.168.5.17";   // [v2 2026-07-08] bench crane RPi (was 192.168.1.10); 2026-08-03: .26→.27; 2026-08-26: .27→.17
    static constexpr int         CRANE_PORT = 5002;

    // Cleaning arm — standalone damiao motor service on the same Pi.
    // TCP commands: INIT / DEPLOY <wall_mm> <LEFT|CENTER|RIGHT> / PARK / STATUS /
    // M1|M2 ENABLE|DISABLE|HOLD|UNHOLD|ZERO. See cleaning_arm/main_api.h.
    static constexpr const char* ARM_IP   = "127.0.0.1";
    static constexpr int         ARM_PORT = 9527;

    // Depth-camera obstacle detection — standalone D435i service on the same
    // Pi. TCP commands: BEFORE / AFTER / PING. See frame_capture/depth_cam_service.py.
    static constexpr const char* DEPTH_CAM_IP   = "127.0.0.1";
    static constexpr int         DEPTH_CAM_PORT = 9530;

    // PQW relay channels (slave 12, now 16CH physically — CH_BRUSH moved to CH15)
    static constexpr int PQW_SLAVE       = 12;
    static constexpr int PQW_TOTAL_CH    = 16;  // 2026-07-24: 8→16 so readAllStatus()/pqw_set_relay_verified_ actually covers CH15
    // [v2 2026-07-07] 4-cup rewiring on the same PQW @ .22 slave 12:
    //   CH1 = right-foot valve (cups slave 1,2)
    //   CH2 = vacuum pump dp0105 (was CH1 in v1)   ← moved
    //   CH3 = left-foot valve  (cups slave 3,4)
    // v1's 3-zone feet/body/center scheme is retired (no body cups, no center cup).
    // [2026-08-27 per user] 真空閥不再分左右——實體只剩一顆閥接在 CH1，同時控制
    // 全部 4 顆吸盤。CH_VALVE_LEFT 改為指向同一個 channel（保留這個名字，讓
    // 二十幾處既有呼叫點不必全部改寫；其中絕大多數本來就是左右同時設同一個值）。
    // 若之後又改回兩顆獨立閥，只要把 CH_VALVE_LEFT 改回 3，所有邏輯自動還原。
    //
    // ⚠ 安全影響：「只放開單側」在硬體上已經不可能。
    // do_step_down_ / do_step_up_ / do_cross_obstacle_ 的核心前提是「一側解真空、
    // 另一側維持吸附當防墜錨點」（見 run_side 的 CH_VALVE_RIGHT / CH_VALVE_LEFT
    // 參數）——現在開 CH1 會讓 4 顆一起失去真空，那個前提直接破掉。
    // 這些路徑的 GUI 入口已於 2026-08-26 移除，但後端指令仍在，
    // **不要用 raw command 呼叫 step_down / step_up / cross_obstacle_***。
    // v2 正式走法 do_step_sync_ 本來就是 4 顆同放同吸（只呼叫 vacuum_valve_("feet")），
    // 不受這個變更影響。
    static constexpr int CH_VALVE_RIGHT  = 1;  // VT307 全部吸盤（原右腳專用，現為唯一一顆閥）
    static constexpr int CH_PUMP         = 2;  // dp0105 vacuum pump (always ON while running)
    static constexpr int CH_VALVE_LEFT   = CH_VALVE_RIGHT;  // 2026-08-27: 3 → 同 CH1（單閥）
    static constexpr int CH_BRUSH        = 15; // arm roller brush motor (2026-07-24 per user: 5→15, arm now physically installed)
    // [2026-08-27 per user] 水泵 CH6 → CH14，讓位給破真空閥（user 指定破真空接 CH6）。
    // ⚠ 這個讓位是強制的，不是整理：清洗流程的滾筒段會主動
    // pqw_set_relay_verified_(CH_WATER_PUMP, true)（見 sweep_with_tool 的 water_on
    // 分支）。若水泵仍指向 CH6，清洗時就會打開破真空閥 → 4 顆吸盤同時失去真空
    // → 機器在貼牆狀態下脫落。CH14 是破真空原本用的號，剛好空出。
    // 水泵實體尚未接管路（見 do_arm_sweep_ 內被註解掉的 CH_WATER_PUMP 呼叫），
    // 因此改號目前不影響實際動作；接管路時務必接到 CH14。
    static constexpr int CH_WATER_PUMP   = 14; // water tank pump (spray) (2026-08-27: 6→14)
    // [2026-07-31 per user] Break-vacuum valve — air-charge into the cups to
    // actively force the seal open, replacing the old two-stage slow-peel
    // retract. ON = charge air, OFF = closed. Mirrors Linux_test menu 31
    // (test_break_vacuum_leg) exactly.
    // Channel 沿革：CH16（bench 期）→ CH14（production）→ CH6（2026-08-27 per user）。
    // 搬到 CH6 時水泵已從 CH6 讓位到 CH14（見上方 CH_WATER_PUMP 的說明）——兩者
    // 絕不可同號，否則清洗時開水泵等於開破真空。
    static constexpr int CH_BREAK_VACUUM = 6;   // 2026-08-27: 14→6 per user
    // [2026-06-05] CH_WATER_INLET 移除 — 進水球閥控制權搬到 crane 端 PQW
    // (192.168.1.34 slave 12 CH4)，washrobot 不再直接控制。所有原本走
    // pqw_.controlRelay(CH_WATER_INLET, x) / pqw_set_relay_verified_(CH_WATER_INLET, x)
    // 的地方改成 set_water_inlet_(x)，內部送 crane_cmd_("water_inlet on/off")。
    // cli_22_ 上的 PQW CH7 物理腳位空著不接線。
    // Old: static constexpr int CH_WATER_INLET  = 7;

    // XKC-Y25-RS485 water level sensor (shares cli_22_ with PQW / JC100 / DY500)
    // Non-contact capacitive sensor, output is binary (0=no liquid, 1=liquid detected).
    // Used by cmd_arm_clean_sweep Phase A — refill until output==1, hard fail on
    // sensor offline (no fallback per 2026-05-20 design decision).
    static constexpr int XKC_SLAVE              = 13;
    static constexpr int WATER_FILL_TIMEOUT_MS  = 180000;  // 180s — 2026-06-03 拉長，實機 60s 不夠水填滿（log 顯示需要 ~80s+）
    static constexpr int WATER_POLL_INTERVAL_MS = 200;     // poll output reg every 200 ms while filling

    // QX-DO24 四路 PWM 輸出模組（2026-08-26 新增，共用 cli_22_）
    //
    // QX-DO24 四路 PWM 輸出模組（cli_22_ 上）
    //
    // [2026-08-27] 曾因 slave 撞號整組停用：PWM_SLAVE=6 當初選 6 的前提是
    //   「cli_22_ 上 JC100 只用 slave 1~4，6 是空的」，而同日把吸盤編號 1-4 改成
    //   5-8 之後，slave 6 同時變成右腳下吸盤的真空表和這顆 PWM 模組。bench log 的
    //       [ERR] [QX:6] device rejected FC 0x10: err 0x7C (未定義錯誤碼)
    //   就是撞號的產物（0x7C 不是合法 Modbus exception code，標準只到 0x0B ——
    //   那是 JC100 的回覆被 PWM driver 撿走後亂解出來的位元組）。
    //   撞號不只是通訊雜訊：FC 0x10 是 write-multiple-registers，發給 slave 6 的
    //   寫入會真的落到 JC100 slave 6 的組態暫存器上，而 JC100 的壓力值正是步伐中
    //   「這一側還吸得夠牢、可以放另一側」的判準 —— 弄壞它是掉落風險。
    //
    // [2026-08-28 per user] 解除停用：user 已用 USB-485 直連把模組的 slave ID
    //   改成 9，撞號的前提消失。cli_22_ 目前佔用情形 ——
    //       5,6,7,8 JC100 ／ 10,11 DY500 ／ 13 XKC ／ 14 DM2J
    //       （12 PQW 已於 2026-08-27 搬到 cli_20_）
    //   → 9 確實是空的，兩邊都不撞。
    //   波特率也不再是問題：per user，這條 bus 上所有裝置都是 115200（原本註記的
    //   「其他裝置 9600、必須把模組改回 9600」已不適用，CLAUDE.md 那段待更新）。
    //
    // ⚠ 這個開關只管「軟體要不要送封包」。模組的 RS485 線若還插在 USB-485 轉換器
    //   上（廠商工具那條路），沒接到 USR gateway 的 A/B 端子，即使 PWM_ENABLED=true
    //   也一樣每個指令 timeout —— Mode B init 不發包，所以連線階段不會報錯，
    //   要到第一個指令才看得出來。
    static constexpr bool PWM_ENABLED = true;
    static constexpr int  PWM_SLAVE   = 9;   // 2026-08-28 per user: 6→9（模組端已改）

    // ZDT pusher slave IDs — [v2] 只剩 4 顆吸盤推桿。
    // 🔴 [2026-08-28 per user 確認] 實體排列（**由上往下看**）：
    //
    //          右      左
    //     上    5       6
    //     下    7       8
    //
    //   → 右側 = {5, 7}（上、下）；左側 = {6, 8}（上、下）
    // v1 body{5,6,7,8} + center{9} cups retired (2026-07-07).
    // [2026-08-27 per user] 4 顆吸盤的 slave ID 由 1-4 改為 5-8。
    // ZDT 推桿與 JC100 真空表共用同一組編號（推桿 slave N 末端的吸盤 = 真空表
    // slave N），兩者分別掛在 .20 / .22 兩條 bus 上，所以同號不衝突。
    // 這兩個常數是唯一的真實來源——所有遍歷吸盤的迴圈都改吃它們，日後再調整
    // 編號只要改這裡，不必再全檔搜 "1..4"（原本散在 11 處寫死的迴圈裡）。
    // 相關陣列（zdt_[9] / meter_[9] / cached_pressure_[9] / last_seal_pulse_[9]）
    // 都是 9 格、index = slave-1，5-8 對應 index 4-7，仍在範圍內。
    // 📌 [2026-08-28] 這兩個常數已移到 public 區（本檔上方）—— 指令分派器
    //    （facade_cleaning_v2/main.cpp）也要用它們驗證 slave 範圍，放在 private
    //    會逼它自己寫死 1..4，而那正是 zdt_pusher / zdt_disable / zdt_enable
    //    在 08-27 改號後全部失效的原因。

    // ✅ [2026-08-28] 左右歸屬已修正。原本是 RF={5,6} / LF={7,8}，那把「兩顆在上面的」
    //    當成了右側 —— 於是每一個「分側」判準（尤其是交替步伐的「錨定側是否還吸著」）
    //    實際上都在看「一邊各一顆」，等於沒有保護。
    //
    // 📌 **程式的結構原本就是對的，錯的只有這四個數字。** RF1/LF1 是「上面那對」、
    //    RF2/LF2 是「下面那對」（見 preset_extend_pulse_for_slave_），改成下面這組之後
    //    分側、上下伸出長度、吊機繩對應（pay_out_right/left）、錨定檢查
    //    **31 處使用點全部自動跟著正確**，不需要逐一修改。
    //
    // 🔴 **尚未在實機驗證**：修正依據是 2026-08-28 使用者口頭確認的實體排列，
    //    程式尚未在機器上跑過交替步伐。**第一次跑 do_step_down_/do_step_up_ 要有人在旁邊**，
    //    並先確認「放開哪一側時，另一側的兩顆確實還吸著」與觀察到的一致。
    static constexpr int ZDT_RF1 = 5, ZDT_RF2 = 7;  // 右：上 5 / 下 7
    static constexpr int ZDT_LF1 = 6, ZDT_LF2 = 8;  // 左：上 6 / 下 8

    // [2026-08-28 per user]「4 顆裡有 2 顆吸住就算 OK」——取代原本的「每側各 ≥1」。
    // 改這個的直接原因：上面那組左右歸屬是錯的，任何「分側」判準都算不準，
    // 而 bench log 已經出現誤觸發（5、6 沒吸到被判成「右側全裸」→ 白白後退，
    // 但實體上 5、6 分屬兩側、另兩顆還吸著，本來應該放行）。
    // ⚠ 已知取捨：這條規則擋不住「吸住的 2 顆剛好在同一側」的情況（例如 5、7），
    //   那時另一側整個懸空但仍會被判成 OK。等左右歸屬確認後應改回分側判準。
    static constexpr int SEAL_MIN_CUPS_TOTAL = 2;

    // DM2J rail/arm slave IDs
    // 2026-05-26: 上滑台從 cli_20_ slave 5 搬到 cli_22_ slave 14，目的是讓
    // arm sweep (cli_22_) 跟 feet rail (cli_20_) 真正並行不撞 bus。
    static constexpr int DM2J_LEFT_FOOT   = 1;    // cli_20_
    static constexpr int DM2J_LEFT_WHEEL  = 2;    // cli_20_
    static constexpr int DM2J_RIGHT_FOOT  = 3;    // cli_20_
    static constexpr int DM2J_RIGHT_WHEEL = 4;    // cli_20_
    // 上滑台（手臂清洗滑軌）。2026-08-28 per user 確認實體接在 192.168.1.20，
    // 掛回 cli_20_（在此之前程式對 cli_22_ 發指令，每次掃動都是 writeMulti no
    // response）。slave 號維持 14 —— .20 上只有 ZDT 5~8 與 PQW 12，不撞號。
    // 沿革：cli_20_ slave 5（~2026-05-26）→ cli_22_ slave 14 → cli_20_ slave 14。
    // ---- 上滑台機構標定（2026-08-28 實機量測）--------------------------------
    // 🔴 這是皮帶軸，不是螺桿。程式在此之前一路假設「1 圈 = 1 cm」，於是每一個
    //    cm 指令都走了 7.7 倍 —— `ARM_SWEEP_CM = 17` 實際會下 131 cm 的行程指令，
    //    而滑台總行程只有 50 cm，也就是每一次掃動都是一路撞到底。
    //    完全隱形：驅動器只數脈衝、回報永遠是漂亮的整數；`do_arm_sweep_()` 成功
    //    路徑一個字都不印；而上滑台在 2026-08-28 之前掛在錯的 gateway、三天沒動。
    //
    // 量測方法（三點 + 一次預測性驗證，皆為實機拿尺量）：
    //    指令 1.0 → 7 cm ／ 2.0 → 15 cm ／ 5.0 → 38 cm
    //    最小平方：實際 = 7.731 × 指令 − 0.615   （殘差全在 ±0.15 cm 內）
    //    截距 −0.6 cm = 皮帶自硬限位起步的鬆弛量（backlash）
    //    驗證：反推「物理 20 cm」→ 指令 2.666 → 實際量到 20 cm ✅
    // 📌 想把導程釘得更精確，最準的是數皮帶輪齒數 × 齒距（整數，無讀尺誤差）。
    static constexpr double ARM_RAIL_LEAD_CM_PER_REV = 7.731;

    // 滑台總行程（實機目測 50 cm）。留 2 cm 餘裕給鬆弛量與量測誤差。
    // 🔴 這個上限的價值不在「限制」，在於**超範圍會被明確拒絕並記錄** ——
    //    2026-08-28 之前，下一個超出行程兩倍的指令，三邊都沒有任何抗議。
    //
    // ⚠️ **但它守的是「指令座標」，不是「物理位置」。** `cmd_init()` 用
    //    `home_set_current_pos_zero()`（0x0021）把**當下位置**設為零點，那不是
    //    homing（0x0020 才是去找原點感測器）。所以座標原點 = init 當下滑台所在之處。
    //
    // 🔴 [2026-08-29 per user] **這台沒有原點感測器** —— 本註解原本寫「真正的解法是
    //    啟用驅動器的 homing」，那是錯的：`home_start()`（0x0020）**沒有東西可以觸發**。
    //    實機佐證：`0x1003` 讀到 `HOME_DONE=0`（從未回零過）；`0x600A=0x0002` 的
    //    `sensor_type=1` 只是驅動器裡的設定值，不代表實體裝了感測器。
    //    ⚠️ 而 homing 速度 `0x600F=200 rpm` ＝ **25.8 cm/s**、`0x6015` overrun=0，
    //    在 50cm 行程上送 0x0020 等於賭方向全速撞 —— **不要呼叫 home_start()**。
    //
    //    **實際的保護是作業流程**：這台有斷電煞車，所以**斷電前一律先把滑台移回 0 點**，
    //    位置被煞車保持住 → 開機時滑台必在左端硬限位，`0x0021` 設當前為零是**正確做法**。
    //    ✅ 方向已實測（2026-08-29，PR move +1cm @30rpm）：**正方向 = 往右，0 點 = 左端。**
    //
    // 🟡 **殘餘風險（流程保證，不是機制保證）**：異常斷電／停電來不及回 0 時，
    //    下次 init 會把當時的位置當成零點，**整個座標系偏移且不會有任何訊息**。
    static constexpr double ARM_RAIL_TRAVEL_MAX_CM   = 48.0;

    static constexpr int DM2J_ARM         = 14;   // cli_20_ (2026-08-28 per user)

    // Pusher motion
    static constexpr int PUSHER_EXTEND_PULSE       = 30000;    // center / fallback ~10 cm (對齊 body，2026-04-24)
    // [2026-08-27 per user] 8.1 cm → 12.0 cm（24300 → 36000 pulse）。
    // （同日先訂 14.0 cm，隨即改為 12.0 cm；下方行程計算已依 12.0 更新。）
    //
    // ⚠ 行程餘裕縮小，改動前務必理解：
    //   SMC LEYG25 行程            20.0 cm
    //   preset                     12.0 cm
    //   disable_seal 有效補伸       +5.0 cm  ← 見 DISABLE_RETRY_MAX_ITERS 註解：
    //                                        5 iters × INCR 3000 先 binding，
    //                                        cap DISABLE_RETRY_MAX_OVEREXTEND
    //                                        (24000/+8cm) 迴圈吃不到
    //   最大總伸長                 17.0 cm
    //   剩餘餘裕                    3.0 cm   （preset 8.1cm 時是 6.9 cm）
    //
    // 吸不住而走完重試迴圈時，推桿會停在距機械底 3 cm 處。若之後要再加大 preset，
    // 必須同時降低 DISABLE_RETRY_MAX_ITERS（每少一次 iter 就多 1 cm 餘裕）——
    // preset 15 cm 就會把餘裕吃到 0。
    // FEET_TARGET_OVER_CAP_CM(5.0) 允許 target 到 preset+5=17cm，與上述上限一致，
    // 不需另外調整。
    // ⚠ 另注意：do_cross_obstacle_ 的 2×preset = 24 cm **已超出 20 cm 行程**。
    // 該功能的 GUI 入口已於 2026-08-26 移除，後端仍在——不要用 raw command 呼叫
    // cross_obstacle_*，否則推桿會直接撞底。
    static constexpr int PUSHER_EXTEND_FEET_PULSE       = 36000;  // feet upper (slave 5,6) 12.0 cm ([2026-08-28] 原註解寫 5,7 —— 那是「右側」不是「上面」) (2026-08-27: 24300→36000 per user；2026-07-27: 統一兩顆都 8.1cm；慣例 3000 pulse=1cm)
    static constexpr int PUSHER_EXTEND_FEET_PULSE_LOWER = 36000;  // feet lower (slave 7,8) 12.0 cm ([2026-08-28] 原註解寫 6,8 —— 那是「左側」不是「下面」) (2026-08-27: 24300→36000 per user，與 upper 保持一致)
    static constexpr int PUSHER_EXTEND_BODY_PULSE       = 34000;  // body upper (slave 5,6) ~11.3 cm (2026-05-28: 30000→36000 +6000=+2cm; 2026-05-28i: 36000→33000 -3000=-1cm，bench 顯示 36000+over 害 Phase 1 fast 700rpm 撞 wall peakI 1500mA+；2026-05-29: 33000→34000 +1000=+0.8cm，邊際提速 iter loop 收斂)
    static constexpr int PUSHER_EXTEND_BODY_PULSE_SHORT = 35400;  // body lower (slave 7,8) ~11.8 cm (2026-05-28: 29400→32400 +3000=+1cm；2026-05-28h: 32400→35400 +3000=+1cm，bench log body lower wall at 42798、SHORT 仍不夠導致 iter 0 plateau,加深一輪)
    static constexpr int PUSHER_RETRACT_PULSE      = 300;   // 收腳目標 (2026-07-14: 0→300 ≈0.1cm)。高速收到 0=機械原點會撞 hardstop「叩」一聲；停在原點前 0.1cm 避免撞擊。<FAKE-DONE 容差 50°(500pulse)、300pulse=30° 仍算收好
    static constexpr int PUSHER_RPM           = 900;     // extend 用（feet）(2026-07-14: 700→1200 激進提速；2026-07-23 per user: 1200→900 調降)
    // [2026-07-31 per user] pusher_two_stage_retract_ 改成比照 Linux_test 功能31
    // 的破真空輔助單段直收（CH_BREAK_VACUUM 主動破壞真空 + 直接快收，不再需要
    // 慢慢剝離）。PUSHER_RPM_RETRACT / RETRACT_SLOW_PEEL_CM / PUSHER_STAGE1_DELAY_MS
    // 這三個「第一段慢脫壁」專用的常數不再被 retract 邏輯使用（保留常數定義本身，
    // 因為 RETRACT_SLOW_PEEL_CM 還有 runtime settings_ 可調路徑，懶得順便拆）。
    static constexpr int PUSHER_RPM_RETRACT      = 150;     // [已不用於 retract] 原兩段式第一段（慢脫壁）
    static constexpr int PUSHER_RPM_RETRACT_FULL = 500;     // 破真空輔助單段直收速度 (2026-07-31 per user: 900→1000 比照 bench 初版 → bench 上又測過 900→700→500，同步拉回正式程式)
    static constexpr double RETRACT_SLOW_PEEL_CM = 1.0;     // [已不用於 retract] 原兩段式第一段慢脫壁距離
    // [2026-07-31 per user] 破真空閥時序，比照 Linux_test 功能31 bench 驗證值。
    static constexpr int BREAK_VACUUM_PRE_RETRACT_MS = 80;   // CH_BREAK_VACUUM ON -> 收腳指令送出
    static constexpr int BREAK_VACUUM_TOTAL_ON_MS    = 500;  // CH_BREAK_VACUUM ON -> OFF（收腳指令在這段時間內送出）

    // [2026-08-28] 真空閥 OFF -> 破真空閥 ON 之間的強制靜置。
    //
    // ⚠ 這不是保守值，是 PQW 模組的實際行為：兩個繼電器寫入間隔一旦壓到接近零，
    //   第二個就不會實際動作（TCP/Modbus 層仍然回成功，所以完全無聲）。
    //   Linux_test 兩個 menu 早就踩過並各自解決了：
    //     - menu 31（單腳）：ZDT-prep 執行緒（release_stall_flag + 100ms + enable
    //       + 200ms）跟 valve-off 並行跑，白撿到 ~300ms，所以一直是好的
    //     - menu 33（4 腳）：沒有那個並行執行緒，第一次測就發現破真空從沒 fire 過，
    //       補上明寫的 300ms 才正常（見該 menu 的註解）
    //   主程式走的是 menu 33 那條路（沒有並行 prep），卻沒補這個 gap。
    //
    // bench 指紋（2026-08-27 log，同一顆 slave 7 前後對比）：
    //     第一次  STALL at 900ms  peakI=3061mA   ← 破真空沒 fire，硬撕到卡死
    //     RETRY   done  at 450ms  peakI=3mA      ← PAUSE 等按鍵 = 超大 gap，正常
    //   差三個數量級，兩次唯一的差別就是中間隔了多久。
    static constexpr int BREAK_VACUUM_PRE_ON_REST_MS = 300;
    static constexpr int PUSHER_RPM_BODY_EXTEND = 700;   // body 組 extend 速度（與其他組同速）
    static constexpr int PUSHER_ACC           = 255;     // acc 用（feet / center extend，max）
    static constexpr int PUSHER_ACC_RETRACT   = 255;     // retract 用（所有組，高 acc 快速收回）
    static constexpr int PUSHER_ACC_BODY_EXTEND = 255;   // body 組 extend acc（與其他組同步）
    static constexpr int PUSHER_SETTLE_MS     = 100;     // 1500 → 300 → 100 (2026-05-29): 機構震盪幾百毫秒就停,DM2J rail 跟 cup 不同軸不受影響;extend 後另有 VACUUM_SETTLE_MS=2000 兜底

    // [2026-05-29] 2-stage retract delay-based (no continuous status polling).
    // bench measured: 1 motor rev ≈ 3.08 cm pusher linear motion (combined gear+lead).
    // 1 pulse = 0.1° (encoder spec); 30 rpm observed ~1942 pulses/sec ≈ 1.54 cm/s.
    // Stage 1 delay = (slow_peel_cm / cm_per_sec) × safety_factor → ms.
    // After delay, sync-fire stage 2 (high speed) — motor switches target from
    // stage1_endpoint to 0 from wherever it is. Cup adhesion breaks within first
    // few mm of motion, so safety_factor × peel time guarantees breakage even
    // with ramp-up/down overhead + buffer for over-extended cup.
    static constexpr double PUSHER_CM_PER_REV         = 3.08;   // bench-measured
    static constexpr double PUSHER_RETRACT_CM_PER_SEC =
        (double)PUSHER_RPM_RETRACT * PUSHER_CM_PER_REV / 60.0;  // = 1.54 cm/s @ 30 rpm
    // Safety factor for stage 1 delay. Higher = more conservative (longer wait,
    // gives over-extended cup more time to peel before stage 2 hits).
    // 2026-05-29: 2.0 → 3.0 per user request, "拉長一點".
    // 2026-07-14: 3.0 → 1.0 激進提速（腳不撐重，脫壁延遲=脫壁時間本身、不加保險）。
    static constexpr double PUSHER_STAGE1_SAFETY_FACTOR = 1.0;
    static constexpr int    PUSHER_STAGE1_DELAY_MS    =
        (int)((RETRACT_SLOW_PEEL_CM / PUSHER_RETRACT_CM_PER_SEC) *
              PUSHER_STAGE1_SAFETY_FACTOR * 1000.0);
        // = (2.0 / 1.54) × 3.0 × 1000 ≈ 3896 ms

    // Step parameters
    static constexpr int STEP_CM_DEFAULT  = 30;   // initial value of step_cm_ (settable via cmd_set_step_cm)
    static constexpr int STEP_CM_MIN      = 5;
    static constexpr int STEP_CM_MAX      = 80;
    static constexpr int STEP_MARGIN_CM   = 10;   // crane extra slack before feet move (2026-05-27: 15→10 提速)
    // [2026-07-27 per user] do_step_sync_ backoff-retry: if a whole side is
    // still unsealed after the in-place per-side retry, retreat the crane
    // toward the pre-step position in this many cm per attempt (full 4-cup
    // retract → crane back → re-extend → recheck), until sealed or fully
    // backed off to the original position (see do_step_sync_ for the loop).
    static constexpr int STEP_SYNC_BACKOFF_CM = 10;
    static constexpr int TOTAL_DISTANCE_CM = 30;  // TODO: set actual building height

    // Crane watchdog
    static constexpr int HEARTBEAT_INTERVAL_MS = 500;
    // 2026-05-07: reverted from test-mode 60000 → 2000. Main crane (Crane_control_PI)
    // motion ops respond fast (relay toggles + meter polling, no open-loop sleep),
    // so 2s timeout works. The 60s value was for crane_shim.py with open-loop timed
    // pay_out/retract that could hold crane_mtx_ for 15s+.
    static constexpr int WATCHDOG_TIMEOUT_MS   = 2000;

    // [2026-06-09] Water inlet leak-prevention watchdog.
    // If valve open continuously > this, force a close. Catches:
    //   - detached refill threads killed mid-sleep (process exit)
    //   - sweep flows that opened valve then hit unhandled exception
    //   - GUI user forgot to press OFF
    // Normal sweep refill typically takes 60-120s; 5min cap leaves generous
    // headroom for slow tank fills + 5s post-full delay + comm retries.
    static constexpr int64_t WATER_INLET_OPEN_MAX_MS = 5 * 60 * 1000;   // 300 sec

    // IMU
    static constexpr const char* IMU_PORT           = "/dev/ttyUSB0";  // TODO confirm
    static constexpr int         IMU_BAUD           = 115200;
    static constexpr double      IMU_ASK_DEG        = 45.0;  // 2026-07-09 per user: 15→45 (== emergency). ASK branch needs avg∈[ASK,EMERGENCY); with ASK==EMERGENCY it never fires → no intermediate pause-to-ask, only the 45° emergency stop stays active.
    static constexpr double      IMU_EMERGENCY_DEG  = 45.0;
    static constexpr int         IMU_BASELINE_SEC   = 3;
    static constexpr double      IMU_HYSTERESIS_DEG = 1.0;
    static constexpr double      ROLL_CORRECT_CM_PER_DEG  = 1.0;
    static constexpr int         ROLL_CORRECT_RETRY_MAX   = 5;

    // DM2J motion
    static constexpr int DM2J_RPM      = 200;
    static constexpr int DM2J_RPM_FEET = 400;   // feet rail (slave 1,3) — faster than wheels/arm
    static constexpr int DM2J_ACC      = 500;
    static constexpr int DM2J_DEC      = 500;

    // Arm sweep (上滑台 / DM2J slave 14 @ cli_22_ since 2026-05-26)
    // NOTE: DM2J ACC/DEC unit is ms/1000rpm (Leadshine convention) — LOWER = faster ramp.
    static constexpr int ARM_SWEEP_CM  = 17;   // sweep cm (2026-08-26 per user: -8→17; 2026-07-27 per user: -6→-8; 2026-07-24 per user: -10→-6; 2026-05-21: 30→40→45→50→55; 2026-05-25: 55→60→100→80; 2026-05-26: 80→100; 2026-05-28: 100→80; 2026-06-06: 80→90→100→90→85; 2026-06-11: 85→60→55; 2026-07-23 per user: 55→-10 — 手臂還沒裝，「所有上滑台移動」統一改小行程+同一方向，涵蓋 do_arm_sweep_ / do_arm_clean_sweep_ / do_arm_clean_sweep_continuous_ 共用此常數)
    // 🔴 [2026-08-28 傍晚 per user] 1000 → 250，與 DM2J_ARM_STEP_SWEEP_RPM 統一。
    //    原本的 1000 是 2026-05-27 為 55cm 行程挑的，而且是在「1cm/rev」的錯誤認知下挑的
    //    —— 當時以為 1000rpm = 16.7 cm/s，用實測導程 7.731 換算實際是 **128.8 cm/s**。
    //    2026-08-28 實機已發生失步（使用者回報並手動調回）。
    //    250 rpm 實際線速度 = 32.2 cm/s；若仍失步，這裡還要再往下調。
    static constexpr int ARM_SWEEP_RPM = 250;   // [2026-08-28 per user] 1000→250，與步伐用值統一（原註解沿革：2026-05-26 bench menu28: 2300; 2026-05-27: 2300→2000→1000 因仍觀察失步）
    static constexpr int ARM_SWEEP_ACC = 100;    // start ramp (ms/1000rpm) — 2026-05-26: 100→200; 2026-05-27: 200→100 配合 RPM 1000
    static constexpr int ARM_SWEEP_DEC = 100;    // stop ramp (ms/1000rpm) — 2026-05-26: 100→200; 2026-05-27: 200→100 配合 RPM 1000

    // [2026-07-23 per user] do_step_sync_ 專用的小行程滑台掃動（0→-10cm→0 來回一次）
    // — 跟上面 ARM_SWEEP_* 是完全不同的動作/參數組，行程短很多（10cm vs 55cm）所以
    // 另外設一組。手臂本身（damiao M1/M2）跟刷滾筒（CH5）都不碰，純粹只動 DM2J:14。
    // ⚠ RPM/ACC/DEC 是初次上機前的保守猜測值，還沒實機驗證，上機後依實際手感調整。
    // [2026-08-27 per user] 上滑台（DM2J:14）還沒裝好——bench log 每次 step 都是
    // 一連串 `[ERR] [DM2J:14] writeMulti no response`。整段「手臂清潔」先停掉：
    //   false → do_step_sync_ 不再並行跑 arm INIT，do_step_sync_rail_sweep_ 直接
    //           early-return，步伐本身（放繩 / IMU / 吸盤）完全不受影響。
    //   true  → 恢復原本流程，不需要改其它地方。
    // 用一個 gate 而不是註解掉呼叫，是因為清潔段散在兩處（INIT 在 do_step_sync_、
    // 掃動在 do_step_sync_rail_sweep_），分開註解容易只關一半——那會變成手臂每步
    // 花 10s 校正卻不清洗。cmd_arm_clean_sweep_dry()（乾掃測試指令）刻意不受此
    // gate 管，因為那支就是拿來測上滑台裝好沒有的工具。
    // [2026-08-28 per user] 上滑台已接上 → 改回 true，恢復步伐內建清潔
    // （INIT 並行 + DEPLOY RIGHT/滾筒 + CH_BRUSH + 滑台掃動 + DEPLOY LEFT/刮刀 + PARK）。
    static constexpr bool STEP_SYNC_ARM_CLEAN_ENABLED = true;

    // ⚠ 這個常數只給 do_step_sync_rail_sweep_（同步步伐內建的上滑台掃動）用；
    //   do_arm_sweep_ / do_arm_clean_sweep_ / continuous 用的是上面的 ARM_SWEEP_CM。
    //   2026-08-26 把 -8→17 時漏改這裡，導致 step_up_sync 實際還在走 -8（log
    //   顯示 PR_move_cm_nowait -8.000）。2026-08-27 per user 補上，兩個常數現在
    //   都是 17，方向同為正 → 0 → 17 → 0。日後改行程請同時改這兩處。
    static constexpr double DM2J_ARM_STEP_SWEEP_CM  = 17.0;  // 0 → 此值 → 0，一次來回 (2026-08-27 per user: -8→17，補上 08-26 漏改; 2026-07-27 per user: -6→-8; 2026-07-24 per user: -10→-6→-4→-6)
    static constexpr int    DM2J_ARM_STEP_SWEEP_RPM = 250;    // 2026-07-27 per user: 300→250 稍微放慢；300→600→500→400→300 (2026-07-24 per user)
    static constexpr int    DM2J_ARM_STEP_SWEEP_ACC = 100;
    static constexpr int    DM2J_ARM_STEP_SWEEP_DEC = 100;
    // [2026-07-23 per user] 估計單趟 (10cm @ 600rpm, 1cm/rev 螺桿換算同
    // ARM_SWEEP_EST_MS 的公式) 走完所需時間：加速斜坡 ~60ms(0.3cm) + 巡航
    // ~940ms(9.4cm) + 減速斜坡 ~60ms(0.3cm) + fire retry/緩衝 ~150ms ≈ 1.5s。
    // [2026-07-24 per user] 行程 -10→-4、RPM 500→400→300 後重算：
    // 巡航距離 4−0.6(加減速)=3.4cm，300rpm=5cm/s → 巡航 ~680ms +
    // 加速 60ms + 減速 60ms + 緩衝 150ms ≈ 950ms → 抓 1000ms。
    //
    // ⚠⚠ [2026-08-28] 1000 → 4500：**2026-08-26 把行程 -8→17 時漏改這裡**
    //   （跟 DM2J_ARM_STEP_SWEEP_CM 同一批漏的）。1000ms 是為 4cm 行程算的，
    //   拿去等 17cm 只夠走到約 4cm。
    //   用上面同一條公式對 17cm @ 250rpm 重算：
    //       1cm/rev 螺桿，250rpm = 250/60 = 4.167 cm/s
    //       加速斜坡 ~60ms(≈0.15cm) + 減速斜坡 ~60ms
    //       巡航 (17 − 0.3) / 4.167 = 4.008 s
    //       fire retry/緩衝 ~150ms
    //       → 合計 ≈ 4.28 s，取 4500ms 留餘裕
    //   影響：這個值太短時，滑台還在走就進到下一步。手臂 DEPLOY 失敗時最明顯 ——
    //   `if (deployed)` 整段被跳過，0→17 與 17→0 之間只剩這個等待，滑台會在
    //   ~4cm 處被反向指令叫回頭，實際只掃了四分之一。DEPLOY 正常時是靠
    //   DEPLOY 本身 + DM2J_ARM_DEPLOY_SETTLE_MS 「碰巧」補足時間，不是設計保證。
    //   ⚠ 改 DM2J_ARM_STEP_SWEEP_CM 或 _RPM 時，必須用上面的公式重算這個值。
    // ⚠ 純計算估計值，未實機驗證；跟 ARM_SWEEP_EST_MS 分開設，避免沿用 55cm
    // 那組估計值讓同步步伐的滑台掃動平白多等好幾秒（沒有實際意義的等待）。
    // 🔴 [2026-08-28 晚] 這個 4500 是用「1cm/rev 螺桿」算的，**前提是錯的**（實測 7.731 cm/rev 皮帶軸）。
    //    正確值：250rpm × 7.731 = 32.2 cm/s → 17cm 只需 528ms，加斜坡與緩衝約 650ms。
    //    **刻意暫不調小**：估太長只是多等（安全），估太短會把移動打斷（危險），
    //    而換算修正本身還沒在 bench 實跑過。等新換算驗證通過再一起重調。
    static constexpr int    DM2J_ARM_STEP_SWEEP_EST_MS = 4500;   // 2026-08-28: 1000→4500（17cm 行程重算）

    // [2026-08-28 per user] DEPLOY 完成後、讓上滑台開始移動前的靜置時間。
    // 給滾筒轉起來 / 工具頭貼牆穩定的時間，避免滑台一動工具還沒到位。
    // 原本是兩處寫死的 sleep_ms_(2500)（2026-07-24 per user 2000→2500），
    // 抽成常數以免又發生「改了一處漏另一處」。per user 再加長：2500 → 3500。
    static constexpr int    DM2J_ARM_DEPLOY_SETTLE_MS = 3500;
    // [2026-07-24 per user] LEFT/RIGHT deploy wall_mm for this sync-step sweep
    // — separate from ARM_CLEAN_WALL_MM(330) used by the continuous sweep engine.
    static constexpr int    DM2J_ARM_STEP_SWEEP_WALL_MM = 400;   // 2026-08-28 per user: 380→400；2026-07-27 per user: 360→380

    // 2026-05-26: Fire-and-forget sweep (avoid cli_22_ contention from PR_move_cm's
    // status poll fighting JC100 pressure reads during disable_seal). PR_move_cm_nowait
    // skips the poll loop — just writes PR target + trigger. Retry multiple fires
    // for redundancy (single Modbus write can be dropped under heavy bus load;
    // re-firing same target is idempotent — driver re-loads PRx slot + re-triggers).
    static constexpr int ARM_SWEEP_FIRE_RETRIES    = 3;
    static constexpr int ARM_SWEEP_FIRE_SPACING_MS = 50;
    // Estimated motion time (acc + cruise + dec + safety) for one segment of
    // ARM_SWEEP_CM at ARM_SWEEP_RPM. Used in place of PR status-poll wait.
    // Calc (2026-06-11, 55 cm @ 1000 rpm × 1 cm/rev):
    //   accel ramp (100ms @ 100ms/1000rpm) =   0.83 cm /  0.1 s
    //   cruise   (53.34 / 16.67 cm/s)      =  53.34 cm /  3.20 s
    //   decel ramp (100ms)                 =   0.83 cm /  0.1 s
    //   3× fire retry × 50ms spacing       =   —      /  0.10~0.15 s
    //   buffer                             =   —      /  0.35 s
    //   total                              =  55.0 cm /  ~3.9 s
    // History: 5500ms (100cm@2300rpm) → 5700 (85cm@1000rpm) → 4200 (60cm@1000rpm) → 3900 (55cm@1000rpm, 2026-06-11)
    // If arm hasn't reached target before next fire, next fire overrides (arm jumps
    // to new target mid-motion). Tune up if bench shows arm not reaching extremes.
    // [2026-07-23] NOT re-derived for the new |ARM_SWEEP_CM|=10cm (was 55cm) —
    // left at 3900ms deliberately: an over-long estimate just means the monitor
    // waits longer than the (much shorter) real 10cm move actually needs, which
    // is safe (no correctness impact), just not time-optimal. Tighten this if
    // the extra wait is annoying on the bench.
    // 🔴 同上：3900 依「1cm/rev」而來。實際 1000rpm × 7.731 = 128.8 cm/s → 17cm 僅 132ms。
    //    暫不調小，理由同 DM2J_ARM_STEP_SWEEP_EST_MS。
    static constexpr int ARM_SWEEP_EST_MS          = 3900;

    // [2026-05-28] Sweep obstacle monitor (Option A + C).
    // A: DM2J:14 (slide motor) alarm bit — slide stalls / over-current → status & 0x0001
    // C: damiao M1 + M2 tau spike vs baseline
    //   - M1 (大臂): PD-holds TOUCHWALL, lateral push on tool reflects via arm lever → tau spike
    //   - M2 (工具頭): holds slot angle, sensitive to twisting forces
    //   Either exceeding threshold for N consecutive polls → obstacle
    // Poll loop replaces the plain sleep in arm_sweep_fire_nowait_ (~5.5s).
    static constexpr int   ARM_SWEEP_MONITOR_POLL_MS         = 200;
    // [2026-05-28] M1 跟 M2 用不同參數（實機觀察 M2 在 sweep 期間因摩擦力自然漂 ~0.5 Nm，
    // M1 noise 小、擋住才 spike，所以 M1 更敏感、M2 更保守）：
    // M1 spike+sustained 雙重判定（區分「真擋（突發 spike → sustained）」vs「自然漂移（漸進無 spike）」）
    // INSTANT > SPIKE > SUSTAINED 三層 threshold：
    //   d > INSTANT (0.7) → 立刻觸發 (200ms 反應，無 confirm)
    //   d > SPIKE (0.4)   → armed，等 d > SUSTAINED (0.2) 連續 CNT (2) 次 → 觸發 (400ms+)
    static constexpr float ARM_SWEEP_M1_INSTANT_THRESHOLD_NM = 1.0f;   // 巨大 spike → 立刻觸發 (2026-05-29: 0.7→1.0,避免 ZDT 推牆反作用力 0.78 假警報)
    static constexpr float ARM_SWEEP_M1_SPIKE_THRESHOLD_NM   = 0.4f;   // 中等 spike → armed
    static constexpr float ARM_SWEEP_M1_SUSTAINED_NM         = 0.2f;   // armed 後 d 持續超過這個 N 次 → 觸發
    // [2026-06-03] 2→3：實機觀察 false positive 由 DM2J:14 slide decel transient
    // 引起的短暫 spike 2 ticks 即可 confirm。3 ticks (600ms sustained) 過濾掉這類
    // 機械 transient — 真 obstacle 阻力通常 sustained 遠超 600ms，影響可忽略。
    static constexpr int   ARM_SWEEP_M1_TAU_CONFIRM_CNT      = 3;
    static constexpr float ARM_SWEEP_M1_RATE_THRESHOLD_NM    = 0.2f;   // 2026-05-28ad: 每 poll d 變化 > 這 → 視為突發 spike (drift 每 poll 變化 ~0.097/step)
    // [2026-05-28] 末段 mask：slide 減速時機構慣性會對 M1 產生 ~0.5 Nm spike
    // （比真擋的 spike 還大）。跳過 sweep 末段最後 1 秒避免 false positive。
    // Trade-off：末段 slide 剩約 ~1000ms × cruise_speed = ~16 cm 範圍內的真擋抓不到。
    // 🔴🔴 [2026-08-28] **這個遮罩從來沒有生效過。**
    //    它錨定在 est_ms 的結尾：`in_decel_mask = (elapsed > est_ms - MASK)`。
    //    而用實測導程（7.731 cm/圈）重算，17cm @ 250rpm 的真實運動只有 **553 ms**，
    //    est_ms 卻是 4500 —— 遮罩窗口在 3500~4500ms，真正的減速在 528~553ms，
    //    **兩者完全沒有交集**。減速尖峰一直是在監看全開的情況下發生的。
    //    ⚠️ 而 changelog 顯示他們為假警報吃過不少苦（ARM_SWEEP_M2_* 最後被「實質
    //    disable」，註解寫「無論怎麼調 threshold 都會跟 light block 訊號重疊」）——
    //    **其中一道保護一直是壞的，而沒有人知道。**
    //    📌 下方註解的「~1000ms × cruise_speed = ~16 cm」也是用錯誤的 1cm/rev
    //    前提算的；用實測導程 1000ms 是 **32 cm**，而整個行程只有 17cm。
    //
    // 🔴 **調整 est_ms 之前必讀**：兩者是耦合的。
    //    est_ms ≤ MASK 時 `est_ms - MASK ≤ 0` → `elapsed > 負數` 恆為真
    //    → **整趟障礙偵測全程關閉，而且不會有任何訊息。**
    //    | est_ms | 遮罩起點 | 監看窗口（運動 553ms） |
    //    | 4500（現值） | 3500ms | 完整覆蓋 |
    //    | 1500 | 500ms  | 最後 53ms 未監看 |
    //    | ≤1000 | ≤0    | 🔴 全程關閉 |
    static constexpr int   ARM_SWEEP_DECEL_MASK_MS           = 1000;
    // M2 (工具頭) — 2026-05-28ai 改為「實質 disable」：
    // 實機觀察 M2 drift 在 step+sweep 並行模式下可達 0.7 Nm + rate 0.18，無論怎麼調
    // threshold 都會跟 light block 訊號重疊。乾脆把 SPIKE 拉超高，M2 path 永不觸發。
    // 偵測完全靠 M1 path (SPIKE 0.4 + RATE 0.2) + INSTANT (0.7) + DM2J:14 alarm。
    // Trade-off: 失去 M1 完全沒反應、只 M2 有反應的 light block。手動 emergency_stop fallback。
    static constexpr float ARM_SWEEP_M2_SPIKE_THRESHOLD_NM   = 100.0f; // 實質 disable: 永不過此門檻
    static constexpr float ARM_SWEEP_M2_SUSTAINED_NM         = 0.2f;   // (未用)
    static constexpr int   ARM_SWEEP_M2_TAU_CONFIRM_CNT      = 2;
    static constexpr float ARM_SWEEP_M2_RATE_THRESHOLD_NM    = 100.0f; // 實質 disable

    // Cleaning sweep at the end of each step_up / step_down (2026-05-21 per user)
    static constexpr int ARM_CLEAN_WALL_MM = 400;  // DEPLOY wall distance (fixed); 2026-08-28 per user: 380→400；2026-07-27 per user: 360→380；2026-07-24: 330→360 per user，手臂已實機裝上；2026-06-02: 350→330 試「上貼下不貼」是不是 M1 過度外擺造成；2026-05-27: 300→350 拉大讓 M1 往前推更多（靠刮刀座彈性吸收過壓）
    static constexpr int ARM_CLEAN_ROUNDS  = 1;    // wet+dry rounds per step

    // Vacuum
    static constexpr int VACUUM_RETRY_MAX     = 5;
    // JC-100 read_pressure() returns raw int in kPa unit on this hardware
    // (despite driver comment saying 0.1 kPa — actual readings show kPa scale,
    // see 2026-04-27u). below = attached / above = detached.
    static constexpr int VACUUM_THRESHOLD_KPA   = -40;   // kPa — verified-sealed threshold (vacuum_check_) (2026-06-05: -50 → -40 Phase 1 speedup F1.3 Step A，配合 VACUUM_SEAL_DEEP_KPA 降到 -45)
    static constexpr int VACUUM_EARLY_STOP_KPA  = -45;   // kPa — early-stop threshold near verified-sealed (was -30, too lenient → early-stopped at marginal seal)
    static constexpr int DETACH_THRESHOLD_KPA   = -10;   // kPa
    static constexpr int VACUUM_SETTLE_MS     = 1500;   // 2026-05-22: 2000 → 1500
    static constexpr int VACUUM_RELEASE_WAIT_MS = 700;   // wait after valve OFF before pusher retract (cup adhesion + line vent) (2026-06-01: 3000→1500; 2026-07-14: 1500→700 激進提速。⚠ 這是氣動洩壓時間、物理下限，若 bench 看到 cup 帶負壓「啵」彈開或瞬間 stall 就是砍過頭，往回加)
    static constexpr int POLL_INTERVAL_MS     = 50;
    static constexpr double VACUUM_BACKUP_CM  = 10.0;  // rail backup on each vacuum retry (2026-05-29: 5→10，weak_seal 後找新位置 5cm 不夠遠，常吸到同一個漏氣點)

    // Left/right rope level-match (方案1, 2026-07-08). The follower side (left)
    // moves its rope until its SD76 meter matches the master (right) side's meter
    // instead of walking a fixed `step` → re-levels every step so per-side error
    // can't accumulate. crane status length_left/length_right are in cm.
    static constexpr double LEVEL_MATCH_TOL_CM = 0.5;  // follower already-level deadband (skip crane move if |delta| below this)
    // Safety cap on a single level-match move = max allowed left/right meter diff.
    // A re-seal (backup) can legitimately leave one side up to ~step longer, so the
    // follower must be allowed a large correction (NOT clamped to ~step); it walks
    // the full |delta| to re-level — same as v1's absolute DM2J rail target auto-
    // absorbing a rail backup on the next step. Only |delta| > this (meters wildly
    // apart) is clamped + warned, with the remainder corrected over later steps.
    static constexpr int    LEVEL_MAX_DELTA_CM = 60;   // 2026-07-15 per user: 兩邊計米差 / 單次移動上限 60cm (hard ceiling)
    // [方案B 2026-07-08] Per-side per-step move ceiling = step + this margin.
    // With the common-absolute-target gait each side moves at most ~one step
    // (lagging side moves exactly step; leading side gives way and moves ≤step),
    // so this only bites on a pathological gap — remainder corrects next step.
    // Kills the old failure where a follower caught up 2×step in one swing.
    static constexpr int    LEVEL_MOVE_MARGIN_CM = 5;
    // [策略1 2026-07-09] Follower-side IMU fine-leveling — after the follower's
    // coarse measured descent (方案B), iterate small tension-safe measured moves
    // (span·tan(roll) estimate) until |roll-baseline| < tol. Uses the already-
    // resealed first side as the true-level datum → robust to long-rope meter
    // scale error / rope stretch (converges on real roll, not meter cm). See §12.
    // Reuses BAL_CAL_ROLL_PANIC_DEG (15°) as the abort-and-leave-to-guard ceiling.
    // Enable is the RUNTIME flag follower_use_imu_ (set_follower_mode imu|meter),
    // not a compile constant — so imu/meter can be compared on the bench.
    static constexpr double FOLLOWER_ROLL_TOL_DEG    = 2.0;    // |roll-baseline| below this = level enough (2026-08-04 per user: 1.0→2.0; 2026-07-23 per user: 0.5→1.0 — small tilts were triggering trim passes that then oscillated sign each pass; only correct bigger tilts now). Shared by BOTH follower_imu_level_ (step_down/up per-side leveling) AND do_sync_imu_roll_correct_ (step_sync differential correction) — this raises the threshold for both.
    static constexpr int    FOLLOWER_IMU_MAX_PASSES  = 3;      // cap trim iterations per step
    static constexpr int    FOLLOWER_IMU_MAX_TRIM_CM = 15;     // per-pass measured-move ceiling (backstop vs bad span/roll)
    static constexpr int    FOLLOWER_IMU_SETTLE_MS   = 800;    // let the released corner stop swinging before reading roll (2026-07-14: 1200→800 提速；多 pass 兜底)
    static constexpr double FOLLOWER_SPAN_CM         = 100.0;  // L/R cup-column horizontal span — PLACEHOLDER, bench-cal (only affects convergence speed, not final level)
    // step_up body backup (2026-05-19): pay out (backup_cm + this margin) before
    // the rail descends the body, then retract backup_cm back with
    // crane_retract_safe_ (weight-threshold stop). The extra margin gives the
    // rail-move generous slack; the monitored retract re-tensions by feedback.
    static constexpr int    BACKUP_PAYOUT_MARGIN_CM = 5;

    // Obstacle rescue (2026-05-15h): if ZDT stalls before reaching this fraction
    // of the commanded pulse → treat as "hit obstacle", not endpoint. Trigger
    // rescue: rail backup OBSTACLE_RESCUE_BACKUP_CM (vs the smaller 5cm vacuum
    // retry backup) + re-extend. Up to OBSTACLE_RESCUE_MAX rescues per cycle_group_
    // attempt before falling through to PausedOnError.
    //
    // Per-group decision: any one cup early-stalling triggers whole-group rescue
    // (kinematically the cups move together; partial rescue creates uneven state).
    static constexpr double STALL_ENDPOINT_RATIO     = 0.80;
    static constexpr int    OBSTACLE_RESCUE_MAX      = 2;
    static constexpr double OBSTACLE_RESCUE_BACKUP_CM = 10.0;
    // Extra dwell after vacuum_wait_release_ in the rescue path, before the
    // two-stage retract — lets residual cup-to-wall adhesion fully peel off
    // (pressure sensor "released" ≠ cup physically detached). 2026-05-18.
    static constexpr int    RESCUE_VACUUM_SETTLE_MS   = 1000;

    // ZDT 堵轉保護檢測電流 (Clog_Ma, Reg 13 in §3.7.6 batch).
    // 2026-05-19 (per user): the firmware-write of Clog_Ma is DISABLED — the
    // clog_guard blocks in cycle_group_ / smart_extend_subset_ are #if 0'd out.
    // Obstacle detection is now purely software (DISABLE_PHASE_CURRENT_LIMIT_MA
    // path A). These two constants are therefore currently UNUSED; kept so the
    // #if 0 blocks still compile if re-enabled.
    static constexpr int CLOG_MA_GENTLE = 800;    // 0.8 A — during extend (was 1000mA 2026-05-15h, lowered after bench showed "推了好幾下才賭轉")
    static constexpr int CLOG_MA_NORMAL = 3000;   // 3 A — user-set default, restored after seal

    // Fine-tune extend (vacuum-feedback): after group broadcast extend, per-cup
    // adjustment loop to push unsealed cups slightly more until vacuum sealed.
    static constexpr int FINE_TUNE_MAX_ITERS         = 3;     // up to N rounds of per-cup adjustment (3000 pulse/iter × 3 ≤ MAX_OVEREXTEND)
    static constexpr int FINE_TUNE_INCREMENT_PULSE   = 3000;  // per round, extend unsealed cup +3000 pulses (~1 cm)
    static constexpr int FINE_TUNE_MAX_OVEREXTEND    = 9000;  // hard cap: never exceed base+9000 (~3 cm beyond preset)
    static constexpr int FINE_TUNE_SETTLE_MS         = 2000;  // wait after each round for vacuum to build

    static constexpr int RETURN_VACUUM_RELEASE_MS = 5000;  // wait after valves off before retracting pushers (return_home only)

    // Disable-seal extend (2026-05-05) — 利用 ZDT disable 後 SMC LEYG25 可倒推
    // 的特性，讓 cup 自己被真空拉到牆面後再 enable 鎖位置，避免「motor 比 cup
    // 慢/快」的同步問題。
    static constexpr int    VACUUM_CONTACT_KPA           = -3;   // 觸發 disable — 任何接觸跡象就早停，讓真空自己拉 cup（不要等 -10，馬達會繼續硬推 1.5cm 拉壞 body cups）
    // 2026-06-05: -60 → -45 Phase 1 speedup F1.3 Step A — 物理上 -45 kPa × 30cm²
    // cup = ~14kg 撐力/cup × 4顆 = 56kg，遠超機體 30-40kg 重量。不需要等到 -60。
    // 跟 VACUUM_EARLY_STOP_KPA -45 對齊（motor 早停的點 = iter 視為成功的點）。
    // 配套：VACUUM_THRESHOLD_KPA 也 -50 → -40 給 5kPa margin。
    static constexpr int    VACUUM_SEAL_DEEP_KPA         = -45;  // 觸發 re-enable（密封充分）(2026-06-05: -60 → -45 Phase 1 speedup)
    static constexpr int    VACUUM_DEEPEN_TIMEOUT_MS     = 5000; // 等真空建立的時限（上限）
    // WAIT_SEAL 趨勢提早結束：cup 真空若停滯逾 VACUUM_PLATEAU_MS（沒再變深超過
    // EPSILON）→ 判定本 iter 吸不到、停止等它（不傻等滿 timeout）。仍在變深的 cup
    // 會一直重置停滯計時、保有完整 grace。讓真的吸不到的位置快點走到 weak_seal/L2。
    static constexpr int    VACUUM_PLATEAU_MS            = 2000; // 真空停滯逾此時間 → 本 iter 放棄等待 (2026-05-28: 1500→2000；2026-05-29: 2000→1800 提速；2026-05-29: 1800→2000 退回,1800ms 害「慢開機」cup 被誤判 weak_seal)
    static constexpr int    VACUUM_PROGRESS_EPSILON_KPA  = 3;    // 真空「有變深」的最小判定量（濾 JC-100 雜訊）
    // [2026-05-28] No-contact fast-skip: cup that never broke -5 kPa within
    // 500ms is essentially at atmospheric pressure → almost certainly not in
    // contact with wall. Skip plateau-timer accumulation, judge plateau immediately.
    // Safe threshold: sealing cups typically reach -30 to -60 kPa within 100-200ms;
    // a cup still at 0~-4 kPa after 500ms has no realistic chance to seal this iter.
    // Saves up to (VACUUM_PLATEAU_MS - 500) = 1000ms per no-contact iter.
    static constexpr int    VACUUM_NO_CONTACT_FAST_MS    = 1000;  // 2026-05-28ag: 1000→2000，cup 5 在 1000ms 還只到 -5、需更多時間; 2026-06-01: 2000 → 1000，peakI < 400mA fast-skip (DISABLE_LOW_CONTACT_PEAK_MA) 已先擋掉大部分 no-contact 情境，剩下「有接觸但真空慢」的 cup 1000ms 仍有機會 seal，沒到 -1kPa 就 fast-skip 合理
    static constexpr int    VACUUM_NO_CONTACT_KPA        = -1;   // 2026-05-28ag: -5→-1，best_p 到 -5 也算「有接觸」不該被 fast-skip。only true 大氣壓 (p>=0) 才算無接觸
    static constexpr int    DISABLE_RETRY_INCR_PULSE     = 3000; // 弱密封時每 iter 補伸 1.0 cm (2026-05-22: 2400→3000 per user；慣例 3000 pulse=1.0cm)
    static constexpr int    DISABLE_RETRY_MAX_OVEREXTEND = 24000; // 上限 +8.0 cm (2026-07-08: 15000→24000) — v2 機構容易讓腳歪掉，某幾隻要伸更長才勾到牆；配 1.0cm 步進(INCR 3000) → 8 次 push 都能到 cap；總伸長 ~phase1(5cm)+8cm ≈ 13cm，仍遠低於 SMC LEYG25 20cm 行程
    static constexpr int    DISABLE_RETRY_MAX_ITERS      = 5;    // iter 上限 (2026-07-08: 8→5 per user 改回原本)。注意：5 次 push × 3000 = +15000 先 binding，cap 24000(+8cm) 迴圈吃不到；有效補伸上限 ~+5cm。settings 可調 (1~20)

    // [2026-06-05] Snowball protection (A+B+C):
    //   A — WEAK_SEAL 不 record_seal_pulse_，避免污染 last_seal_pulse_
    //   B — feet_max_overextend_cm_() 對外回傳值 cap → 防 body target 暴衝
    //   C — feet target = min(last_seal_pulse_[feet], preset + FEET_TARGET_OVER_CAP)
    //
    // Cap 值取自物理約束：
    //   body cup 物理 max ≈ 60000 pulses (200mm)
    //   body preset = 34000 / 35400
    //   iter loop 還會在 target 之上推 +12000 pulses (4 iter × INCR 3000)
    //   → feet_over cap = (60000 - 34000 - 12000) / 3000 ≈ 4.67 cm，取 4.5 留餘裕
    //   feet 同理 (60000 - 17100 - 12000) / 2857 ≈ 10.8 cm，取 5.0 保守（v2 preset 縮短後餘裕更大）
    //
    // 牆距超過 cap 時的後果：cup 在 free air、no contact → iter loop 內 fast-skip
    // → MAX_ITERS 後 WEAK_SEAL。A 會接手不去污染 last_seal，下一輪重新從 preset 起算。
    // realign trigger (drift > 1.5cm) 仍是 cup 真正回 preset 的途徑。
    static constexpr double FEET_TARGET_OVER_CAP_CM   = 5.0;
    static constexpr double FEET_MAX_OVER_CAP_CM     = 4.5;
    static constexpr int    DISABLE_PHASE_CURRENT_LIMIT_MA = 1200;  // 撞障礙物保險：2A→1.2A (2026-05-06 cup 變形)→0.9A (2026-05-15)→0.8A (2026-05-18 純電流判定)→0.9A (2026-05-18)→1.2A (2026-05-19 user 調高，減少正常壓牆建真空時的誤判)
    // [2026-05-29] peakI-based fast skip in WAIT_SEAL: cup whose push peak
    // current never exceeded this threshold clearly didn't contact anything
    // (in free air). Skip WAIT_SEAL polling for it — straight to next iter.
    // Bench observed:
    //   - SEAL slaves peakI 600~900mA (slave 1=643, slave 3=847)
    //   - NO-CONTACT peakI 100~400mA (slave 2 iter0=135, slave 4 iter0=310)
    //   - BORDERLINE (approaching wall) peakI 500~800mA
    //   - WALL endpoint peakI > 1200mA (triggers DISABLE_PHASE_CURRENT_LIMIT)
    // 400 sits in the gap. Saves up to VACUUM_PLATEAU_MS per no-contact iter
    // (in find-wall sequences where slave needs 3-4 iter to reach wall).
    static constexpr int    DISABLE_LOW_CONTACT_PEAK_MA    = 400;
    // path A 電流超標時的位置 gate：cup 卡死位置若 >= preset − 此值 → 判定為「壓牆 endpoint」
    // 而非 obstacle（壓牆 jam 和障礙物 jam 電流都會飆，靠位置區分：到位才飆=牆，半路就飆=障礙物）
    static constexpr double OBSTACLE_ENDPOINT_GATE_CM    = 1.5;
    // 跨 iter 退步判定：cup 卡死位置若比「曾到過的最深位置」短超過此 margin → 判 obstacle
    // （蓋過位置 gate）。牆不會內縮，卡在比上輪淺處 = 新障礙物。margin 用來濾機械變異/雜訊
    static constexpr double OBSTACLE_REGRESS_MARGIN_CM   = 0.3;
    // DISABLE_POS_ERROR_LIMIT_DEG: 2026-05-18 起 obstacle 路徑 A 拿掉 pos_error
    // AND 條件、改純電流判定 → 此常數目前未使用。保留供未來若要恢復 pos_err gate。
    static constexpr double DISABLE_POS_ERROR_LIMIT_DEG  = 5.0;     // (currently unused)
    static constexpr int    PUSHER_RPM_DISABLE_SLOW      = 50;   // Phase 2 慢速 RPM
    static constexpr int    PHASE1_BUFFER_PULSES         = 3000; // 4500→3000 (2026-05-18): Phase 1 快伸到 preset-1.0cm（原 1.5cm）。把 0.5cm 從慢 phase 搬到快 phase 加速伸腳。配 INCR 3000 → iter 0 剛好推到 preset、iter 1 = preset+1cm
    static constexpr int    DISABLE_PRE_DISABLE_DELAY_MS = 100;  // push 完到 disable EN 之間的緩衝（讓 cup 在馬達 holding 下接觸牆面）(2026-05-28: 200→100，實機觀察 stable 訊息瞬間印，200ms 過保守)

    // Rope weight (DY_500 × 2 on cli_22_ slaves 10/11) — safety guard for crane retract
    // Topology assumption: 2 ropes (left/right), each sensor on one rope. Each
    // sensor in normal hang reads ~MACHINE_WEIGHT_KG / 2 = ~67 kg. Adjust
    // *_PER_SENSOR_* if redundant on single rope.
    static constexpr int    DY_SLAVE_LEFT  = 10;
    static constexpr int    DY_SLAVE_RIGHT = 11;
    static constexpr double MACHINE_WEIGHT_KG = 135.0;
    // State-aware threshold: when cups sealed (Attached/Running/Paused/Balancing/PausedOnError),
    // rope shouldn't bear much → low threshold to detect crane fighting cups.
    // When hanging (Idle/Ready/Error/ReturningHome), rope carries full weight → higher threshold.
    static constexpr double ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_ATTACHED = 40.0;  // 2026-05-19: 50→40 per user
    static constexpr double ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_HANGING  = 80.0;  // 2026-05-19: 90→80 per user
    static constexpr int    WEIGHT_MONITOR_POLL_MS = 100;     // active monitor poll interval during crane retract

    // Attach finish — once all cups are sealed, pay out crane rope so the body
    // weight transfers from the rope onto the suction cups, leaving only a light
    // residual rope tension as a safety margin.
    // Target tension follows the crane's web-configured `g_retract_tension_stop_kg`
    // (網頁「收繩軟停張力」, same knob step_up/step_down retract uses for its soft
    // tension stop) — one knob covers both. The constant below is a FALLBACK only,
    // used when the crane status read / parse fails.
    static constexpr double ATTACH_PAYOUT_TARGET_KG  = 10.0;  // fallback target (kg) — runtime value comes from crane status
    static constexpr int    ATTACH_PAYOUT_MAX_CM     = 50;    // safety cap — abort pay_out if tension never reaches target
    static constexpr int    ATTACH_PAYOUT_SETTLE_MS  = 300;   // dwell after each 1cm pay_out for tension to settle

    // [2026-06-02] Balance calibration constants — see cmd_balance_calibrate_* doc.
    static constexpr int    BAL_CAL_PRELOAD_TIMEOUT_MS   = 20000;   // Phase 1 timeout (20s; user spec)
    static constexpr int    BAL_CAL_PRELOAD_RETRACT_CM   = 30;      // max retract per attempt during preload
    static constexpr int    BAL_CAL_FREE_HANG_SETTLE_MS  = 3000;    // wait after all cups off for swing to die
    // [2026-06-02 v8] Cal-specific vacuum release wait — replaces the previous blind
    // sleep_ms_(VACUUM_RELEASE_WAIT_MS) in bal_cal_release_body_ / _feet_center_.
    // Calls vacuum_wait_release_ which polls JC100 pressure until p >= DETACH_THRESHOLD_KPA
    // OR this timeout. Bench observed cup retract stalled when blind sleep ended too soon
    // and cups were still sucking. Cal isn't time-pressured — give a generous budget.
    static constexpr int    BAL_CAL_VACUUM_RELEASE_TIMEOUT_MS = 2000;
    // [2026-06-02 v12] Temporary crane up_stop_total_kg raise during Phase 4.
    // Default 50kg trips because cal cup release puts full robot weight on ropes
    // (~50-60kg expected). 100kg leaves comfortable margin while still catching
    // catastrophic overload. Restored to original via RAII when balance loop exits.
    static constexpr double BAL_CAL_UP_STOP_TOTAL_KG = 100.0;
    // [2026-06-02 v2] Proportional pulse width — 越接近 tolerance pulse 越短，
    // 避免 ping-pong overshoot。bench 觀察 (Sadie) 拉左繩短時間就讓 roll 動
    // 3.65°，原 300ms 對 0.5° 收斂太粗。改成 3 段比例 + tolerance 0.5°→1.0°。
    static constexpr int    BAL_CAL_PULSE_FAR_MS         = 300;     // [deprecated 2026-06-02 v7] kept for ABI compat, no longer used
    static constexpr int    BAL_CAL_PULSE_MID_MS         = 150;     // [deprecated 2026-06-02 v7]
    static constexpr int    BAL_CAL_PULSE_NEAR_MS        = 80;      // [deprecated 2026-06-02 v7]
    static constexpr int    BAL_CAL_SETTLE_MS            = 2000;    // wait after motor off for IMU/rope to settle
    static constexpr int    BAL_CAL_MAX_ITER             = 6;       // [v7] outer iter cap (continuous mode: 1-3 typical, allow up to 6)
    // [2026-06-02 v7] Continuous-motor design (per user, replaces pulse approach).
    // Inner poll loop monitors IMU while motor runs continuously:
    static constexpr int    BAL_CAL_INNER_POLL_MS        = 50;      // IMU re-read interval during motor-on phase
    static constexpr int    BAL_CAL_INNER_MAX_MS         = 8000;    // hard cap: motor must turn off within 8s per outer iter
    static constexpr int    BAL_CAL_INNER_STALE_LIMIT    = 60;      // 60 × 50ms = 3s of unchanged imu_.x → emergency stop (2026-06-02 v11: 20→60，1s 在 outer 0 起步、鋼索 slack 還沒拉緊時誤觸；3s 對真正 IMU 凍結還是夠快)
    static constexpr int    BAL_CAL_TOTAL_TIMEOUT_S      = 60;      // total Phase 4 timeout (cumulative across outer iters)
    static constexpr double BAL_CAL_OVERSHOOT_DEG        = 0.1;     // sign-flip overshoot detection: |roll| > this AND sign changed → stop
    static constexpr double BAL_CAL_ROLL_TOL_DEG         = 1.0;     // converged when |roll - baseline| < this (2026-06-02: 0.5→1.0 務實值)
    // Dual-threshold START gating (2026-06-02 v6, per user 反饋):
    //   too small (< MIN) → 機體已平衡，校正無意義 → reject "已平衡"
    //   too large (> MAX) → 太歪，preload/release 階段風險高 → reject "太危險"
    //   中間 → 放行（這才是校正的合理使用區間）
    static constexpr double BAL_CAL_START_ROLL_MIN_DEG   = 0.5;     // pre-check (cmd start): |roll| < this → already balanced, reject
    static constexpr double BAL_CAL_START_ROLL_MAX_DEG   = 15.0;    // pre-check (cmd start): |roll| > this → too tilted, reject
    static constexpr double BAL_CAL_ROLL_PANIC_DEG       = 15.0;    // watchdog during Phase 4: roll > this → abort (留寬給 balance loop 的暫態擺盪)
    static constexpr double BAL_CAL_TENSION_MIN_KG       = 10.0;    // watchdog: any side < this → abort. (2026-06-02: 30→10 — 30 對不平衡機體誤觸發；正常不平衡 R 側可低至 20-25kg；真斷繩會掉到 0-3kg；10kg 仍 catch 真故障)

    // Realign sequence (E) — periodic feet/body cup re-zero when fine_tune drift accumulates.
    // 2026-05-22: 從單一 max 門檻換成 hybrid（max OR mean），避免單顆 cup outlier
    // 過於頻繁觸發 realign。
    //   - 單顆 cup 漂超過 REALIGN_THRESHOLD_CM (1.0cm) → 觸發（safety net for outlier）
    //   - 全部 cup 平均漂超過 REALIGN_THRESHOLD_MEAN_CM (1.0cm) → 觸發（累積式判斷）
    // 2026-06-05: Phase 1 加速 — 提前 trigger 讓每次 realign 工作量小、body cup
    // 不再撞 endpoint → disable_seal iter 大減（連鎖效益）。前提是 2026-06-01h
    // fix 讓 Stage 0 stall non-fatal、realign 整體更穩。
    static constexpr double REALIGN_THRESHOLD_CM            = 1.0;   // single-cup max trigger (2026-07-08: 1.5 → 1.0 per user) (2026-06-05: 3.0 → 1.5 Phase 1 speedup) (2026-05-22: 1.5 → 3.0)
    static constexpr double REALIGN_THRESHOLD_MEAN_CM       = 1.0;   // mean of |drift| across cups → trigger (2026-06-05: 2.0 → 1.0 Phase 1 speedup) (2026-05-22: 1.0 → 1.5; 2026-05-28: 1.5 → 2.0)
    // Realign crane assist target = the per-sensor weight limit (rope_weight_
    // limit_per_sensor_kg_, 2026-05-19 per user — was a fixed 2kg). Not a
    // constant here because the limit is state-dependent.
    static constexpr int    REALIGN_CRANE_ASSIST_MAX_CM     = 10;    // safety upper bound on crane retract during realign
    // Two-stage retract pattern (matches cycle_group_ body retract):
    //   Stage A: retract delta/3 at SLOW rpm — break cup adhesion to wall
    //   Stage B: retract remaining 2*delta/3 at FULL rpm — finish quickly once unstuck
    static constexpr int    REALIGN_RETRACT_RPM             = 100;   // Stage A: retract while sealed (break adhesion) (2026-07-14: 50→100; v2 腳不撐重、torque spike 風險降；限速主因是拉太快扯破真空脫落，100 仍受控)
    static constexpr int    REALIGN_RETRACT_ACC             = 200;
    static constexpr int    REALIGN_RETRACT_RPM_FULL        = 90;    // realign 單段收速度 (2026-07-14: 60→120→70；120 實機扯破真空脫封 realign_post_unsealed，降回 70。2026-07-15 per user: 70→90 試探，介於已知安全值 70 跟已知會脫封的 120 之間 — 上機台要盯 realign_post_unsealed 有沒有再出現)
    static constexpr int    REALIGN_RETRACT_ACC_FULL        = 150;   // (2026-07-14: 50→150，無載 torque spike 風險降)
    static constexpr int    REALIGN_EXTEND_RPM              = 60;    // extend short cups to preset (2026-07-14: 20→60，v2 無載、原「load builds gradually」不再適用)
    static constexpr int    REALIGN_EXTEND_ACC              = 200;
    // [2026-06-01] Phase 2 stage 0 "preload jog" — before Stage A retract, give
    // each retract slave a tiny outward push (~0.1cm) to relieve elastic mechanism
    // preload caused by machine weight deforming the cup-pusher chain during long
    // extension. Without this jog, Stage A retract has to fight adhesion + vacuum
    // + elastic preload → peakI spikes to 3000mA+ → STALL (observed repeatedly in
    // in_window realign where Phase 1 crane assist is skipped).
    // (Threshold to lower realign trigger from max=3.0 → smaller is PENDING — try
    // jog first, observe stall rate, then decide if threshold needs to drop.)
    static constexpr int    REALIGN_JOG_PULSES              = 300;   // ~0.1 cm outward jog
    static constexpr int    REALIGN_JOG_RPM                 = 30;    // gentle between extend(20) and retract(50)
    static constexpr int    REALIGN_JOG_ACC                 = 150;   // slow ramp, avoid extra peakI
    // Pre-equalize: when body-cup extension exceeds feet-cup extension by this, extend feet first
    // before synchronous retract. Avoids tilt-induced over-current on upper cups during retract.
    static constexpr double REALIGN_EQUALIZE_THRESHOLD_CM   = 3.0;

    //=========== hardware ===========

    TCP_client cli_20_, cli_22_;   // [v2] .20 = ZDT pushers 1-4, .22 = JC100/PQW/arm-rail/XKC/DY500 (.21/cli_21_ retired)
    TCP_client crane_cli_;
    // Cleaning arm — separate TCP connection to local motor_api service (127.0.0.1:9527)
    TCP_client arm_cli_;
    std::mutex arm_mtx_;
    // Depth-camera (D435i) obstacle-detection service — separate TCP connection
    // to local depth_cam_service.py (127.0.0.1:9530). Same lazy-connect +
    // background-reconnect pattern as arm_cli_.
    TCP_client depth_cli_;
    std::mutex depth_mtx_;
    // Dedicated 2nd connection for emergency stop sent from weight-monitor thread
    // during in-flight retract. Bypasses crane_mtx_ to avoid deadlock with the
    // main thread holding it for the long-running retract reply wait.
    // Shim is multi-connection (per-conn thread); both connections work in parallel.
    TCP_client crane_cli_estop_;
    std::mutex crane_estop_mtx_;

    DM2J_RS570        dm2j_[5];   // index 0..3 → slave 1..4 (cli_20_); index 4 → slave 14 arm rail (cli_22_, via D_() special case)
    // Serializes DM2J motion on cli_20_ only (slaves 1,2,3,4 = feet + wheels).
    // 2026-05-26: arm rail (slave 14) moved to cli_22_ — no longer needs this
    // mtx because cli_20_ and cli_22_ are physically separate gateways/buses.
    // 2026-05-22 history: when arm was on cli_20_ slave 5, background sweep
    // competed with main thread feet rail for cli_20_ TCP socket → frame
    // timeout → PausedOnError. Mtx fixed it. Now arm sweep uses cli_22_
    // (shared with JC100/PQW/XKC/DY500) and its own TCP_client::socket_mtx_
    // handles serialization within cli_22_.
    std::mutex        dm2j_motion_mtx_;
    ZDT_motor_control zdt_[9];   // index 0..8 → slave 1..9
    JC_100_METER      meter_[9]; // index 0..8 → slave 1..9
    PQW_IO_16O_RLY    pqw_;
    QX_DO24           pwm_;      // 4-ch PWM output, cli_22_ slave 6
    DY_500_weight_sensor weight_[2];  // index 0 = slave 10 (left rope), 1 = slave 11 (right rope)
    XKC_Y25_RS485     lvl_;            // water tank level sensor (slave 13 on cli_22_)

    Serial_port  imu_serial_;
    WT901BC_TTL  imu_;

    // Tracks the per-driver debug flag passed at init() so high-frequency poll
    // loops (e.g. zdt_wait_motion_done_) can temporarily silence hex dumps and
    // restore to the correct value. Set from WR_DRIVER_DEBUG env var.
    bool driver_dbg_ = false;

    //=========== state ===========

    std::atomic<bool>    motion_active_;
    // [2026-06-03] Step-level flag: true for entire duration of cmd_step_*, even
    // across motion_active_ toggles between phases. cmd_status reads it to skip
    // JC100 fresh-read (avoids GUI poll hammering cli_22_ while step body/feet
    // pre_cycle uses PQW/JC100/DM2J:14 on same bus). RAII guard StepInProgressGuard
    // sets+clears atomically across all return paths in step cmd entrypoints.
    std::atomic<bool>    step_in_progress_{false};
    std::mutex           motion_mtx_;
    // [2026-07-15] Serializes ZDT pusher bus ops (pusher_move_many_ /
    // pusher_two_stage_retract_ / pusher_extend_with_disable_seal_) across
    // threads. Needed once feet_topup_unsealed_ started running in the
    // background (see run_side in do_step_down_/do_step_up_) — without this,
    // the background topup and the main thread's other-side pusher calls could
    // both be mid-Modbus-transaction on cli_20_ (shared by all 4 ZDT slaves) at
    // the same time, with no guarantee a thread's receiveData() gets the reply
    // meant for its own request (TCP_client::socket_mtx only protects a single
    // send/recv call, not a whole request-response transaction).
    std::mutex           zdt_bus_mtx_;

    std::mutex           crane_mtx_;
    std::atomic<bool>    crane_wd_running_;
    std::atomic<int64_t> crane_last_ok_ms_;
    std::thread          crane_wd_thread_;

    // Crane keepalive (2026-05-15): periodically ping crane during washrobot-side
    // long ops (pusher extend, DM2J rail moves, etc.) so the crane_watchdog
    // doesn't false-abort. Only pings when motion_active_ is true to avoid
    // spamming the bus during idle.
    std::atomic<bool>    crane_keepalive_running_;
    std::thread          crane_keepalive_thread_;
    void                 crane_keepalive_loop_();

    // [2026-06-09] Water-inlet leak-prevention watchdog. set_water_inlet_(true)
    // stamps water_inlet_open_ts_ms_; set_water_inlet_(false) zeros it. Background
    // loop polls every 10s and if (now - ts) > WATER_INLET_OPEN_MAX_MS, force-closes.
    // Catches detached-thread death, GUI forget-OFF, unhandled exceptions in sweep.
    std::atomic<int64_t> water_inlet_open_ts_ms_{0};    // 0 = closed/disarmed
    std::atomic<bool>    water_inlet_watchdog_running_{false};
    std::thread          water_inlet_watchdog_thread_;
    void                 water_inlet_watchdog_loop_();

    // JC-100 pressure cache. Updated by:
    //   1. Motion paths via read_pressure_() — piggyback during normal reads
    //   2. cmd_status() — fresh read of all 9 when motion idle (refresh button)
    // [2026-05-29] Background poll thread REMOVED — was source of bus contention.
    std::atomic<int>     cached_pressure_[9];  // index s-1 = slave s

    // [2026-08-28] 每顆壓力計「最後一次讀取是否失敗」。
    // 🔴 為什麼需要：`cmd_status` 只在 `error_flag == 0` 時更新 cached_pressure_，
    //    失敗就沿用舊值 —— 而輸出的 `p5=..` **看不出那是新鮮值還是 timeout 後的快取**。
    //    2026-08-28d 的 changelog 記過同一件事：`p5=0 p6=1...` 被讀成「沒吸所以是 0」，
    //    其實是 `comm error, return last pressure`，當時整條 .22 bus 已經不通。
    //    「會被騙的只有看 status 的人」—— 現在讓 status 自己說出來。
    // 📌 刻意不改 `p<N>=` 欄位的格式（GUI 在解析它），改為附加獨立欄位 `p_err=`。
    std::atomic<bool>    pressure_stale_[9] {};   // index s-1；true = 最後一次讀取失敗
    // [2026-06-02] Rate-limit cmd_status() JC100 fresh-read to ≤1Hz regardless
    // of how fast GUI polls. GUI status poll is 500ms (2Hz) but each fresh-read
    // = 9 JC100 reads on cli_22_ (shared bus). 18 reads/sec saturates cli_22_
    // (PQW, XKC, DM2J:14 also on it) → JC100 TIMEOUT flood during attach idle
    // gaps. Cap to 1 fresh-read/sec; GUI still gets cache updates at poll rate.
    std::atomic<int64_t> last_status_fresh_read_ms_{0};
    std::atomic<bool>    pressure_poll_running_;  // kept for backward compat (always false)
    std::thread          pressure_poll_thread_;   // kept for backward compat (never started)
    void                 pressure_poll_loop_();   // kept (no longer called); body becomes no-op
    // Wrapper around M_(slave).read_pressure() that piggyback-updates cache.
    // Use this in motion paths so GUI sees fresh values without background poll.
    int                  read_pressure_(int slave);

    // Cached weight sensor readings (kg). Updated by pressure_poll_loop_ side-channel.
    std::atomic<double>  cached_weight_kg_[2];  // index 0 = slave 10, 1 = slave 11
    std::atomic<bool>    weight_comm_ok_[2];    // last read succeeded?
    // Set to true after init-time probe succeeds. False means sensor not
    // physically present (current test mode without real crane) → skip
    // polling to avoid driver consecutive-error log spam.
    std::atomic<bool>    weight_present_[2];

    // === Cup extension persistence (D) + feet→body delta (B) ===
    // last_seal_pulse_[s-1] = the ZDT extend pulse where slave s last sealed
    // successfully. Initialized to per-slave preset; updated by fine_tune /
    // cycle_group_ on success. Used as base for next extend (auto-tracks
    // cumulative wall-distance drift across steps).
    std::atomic<int>     last_seal_pulse_[9];
    // After feet phase succeeds, max((last_seal_pulse_ - feet_preset)/3000) cm.
    // Body phase extend adds this Δ to its own target so body cups still reach
    // wall when feet over-extension pushed machine away.
    std::atomic<double>  last_feet_max_over_cm_;

    double               imu_roll0_;
    double               imu_pitch0_;
    std::atomic<bool>    imu_ask_pending_;
    std::atomic<bool>    imu_mon_running_;
    std::thread          imu_mon_thread_;

    std::atomic<State>   state_;
    State                state_before_pause_;  // remembered on cmd_pause, restored on cmd_resume
    State                state_before_wait_;   // remembered on balance_ask, restored on confirm_balance / hysteresis clear
    std::mutex           state_mtx_;           // serializes non-atomic prev-state fields

    // Rail / vacuum-retry tracking (diagnostic — actual control uses DM2J absolute positioning)
    std::atomic<double>  rail_pos_cm_;         // current absolute rail position (feet +, body -)
    std::atomic<double>  body_residual_cm_;    // previous body under-retract (auto-absorbed by next feet phase via absolute target)
    double               actual_feet_cm_;      // last feet-phase actual DM2J move (used by body phase logging)

    // ZDT slaves excluded from all group operations (group_slaves_ filters these).
    // Set via cmd_zdt_disable before init for hardware not yet installed.
    std::set<int>          disabled_zdt_slaves_;

    // Per-step rail travel (cm). Settable per cmd_step_down / cmd_run call.
    // Default STEP_CM_DEFAULT (30); valid range STEP_CM_MIN..STEP_CM_MAX (5..80).
    std::atomic<int>     step_cm_;

    // [2026-06-01] Camera-based obstacle detection toggle. Default OFF — does
    // NOT touch existing step_down flow until explicitly enabled. When ON,
    // future FrameAnalyzer integration (camera_obstacle_plan.md Phase 5) will
    // pre-check for window frames / sills before each step_down and may
    // override step_cm to step over obstacles.
    // Toggle via cmd_obstacle_detect; status in cmd_status output.
    // Cameras: 192.168.1.112 / 113 (bottom-mounted, downward-facing).
    // Detector: D:/洗窗戶機器人/window_detect/detect_server.py (UDP :5040,
    //           YOLOv8 + Hailo NPU, class=window_frame).
    std::atomic<bool>    obstacle_detect_enabled_;

    // [2026-07-09] Follower (second-moving) side leveling mode, runtime-switchable
    // via `set_follower_mode imu|meter`:
    //   false (meter) = 原本方法：第二腳走方案B 計米共同目標，不做 IMU 微調
    //   true  (imu)   = 第二腳粗走(方案B)後再 follower_imu_level_ 依 IMU 精對平
    // Default imu (2026-07-14 per user; ⚠ imu path needs FOLLOWER_SPAN_CM bench-cal
    // for good convergence speed — switch to meter via set_follower_mode if unstable).
    std::atomic<bool>    follower_use_imu_{true};

    // [2026-07-09] Which foot leads the FIRST step, runtime-switchable via
    // `set_first_step left|right`. true = right leads step 1 (then alternates
    // right→left→right…); false = left leads step 1 (left→right→left…). Applies to
    // multi-step run/run_script (alternation seed) AND single step_down/step_up.
    // Default true (right first) = prior behaviour.
    std::atomic<bool>    first_step_right_{true};

    // [2026-06-02] Balance calibration state. See cmd_balance_calibrate_*.
    //   running_: true between cmd_balance_calibrate_start and either record
    //             or abort. While true, state_ is also Calibrating.
    //   abort_requested_: set by cmd_balance_calibrate_abort, polled by the
    //             calibration thread between phases / inside sleeps.
    //   phase_:   string for GUI display (preload / releasing_body / ... /
    //             awaiting_record / done / aborted_<reason>).
    //   await_record_: true after Phase 4 converged, awaiting cmd_balance_
    //             calibrate_record. Robot is hanging on ropes during this.
    //   last_offset_cm_: result of last completed calibration (Phase 5 record).
    //             Also persisted to settings.static_roll_offset_cm.
    std::atomic<bool>    balance_cal_running_{false};
    std::atomic<bool>    balance_cal_abort_requested_{false};
    std::atomic<bool>    balance_cal_await_record_{false};
    std::atomic<double>  balance_cal_last_offset_cm_{0.0};
    std::mutex           balance_cal_phase_mtx_;
    std::string          balance_cal_phase_;   // protected by mtx

    // =====================================================================
    // [2026-05-29] Runtime-tunable wall-tune settings (L1 + L2).
    //
    // These shadow the corresponding `static constexpr` defaults; consumers
    // read `settings_.NAME.load()` instead of the constexpr name directly.
    // Initialized from constexpr defaults in the constructor (so a fresh
    // build behaves identically to before this change).
    //
    // Loaded from `settings.json` on startup if present; overrides defaults.
    // Set via cmd_set_setting / cmd_save_settings (only when state==Idle,
    // enforced by cmd_set_setting).
    //
    // Naming: lowercased version of the constexpr (e.g.
    // PUSHER_EXTEND_FEET_PULSE → settings_.pusher_extend_feet_pulse).
    // =====================================================================
    struct Settings {
        // ---- L1 (high-frequency wall-tune) ----
        std::atomic<int>    arm_clean_wall_mm;
        std::atomic<int>    pusher_extend_feet_pulse;
        std::atomic<int>    pusher_extend_feet_pulse_lower;
        std::atomic<int>    pusher_extend_body_pulse;
        std::atomic<int>    pusher_extend_body_pulse_short;
        std::atomic<int>    vacuum_seal_deep_kpa;
        std::atomic<double> realign_threshold_cm;
        std::atomic<double> realign_threshold_mean_cm;
        // ---- L2 (medium-frequency wall-tune) ----
        std::atomic<double> rope_weight_limit_attached;
        std::atomic<double> rope_weight_limit_hanging;
        std::atomic<int>    step_cm_default;
        std::atomic<int>    step_cm_max;
        std::atomic<int>    vacuum_plateau_ms;
        std::atomic<double> vacuum_backup_cm;
        std::atomic<double> retract_slow_peel_cm;
        std::atomic<int>    disable_retry_max_iters;
        std::atomic<int>    pusher_rpm_disable_slow;         // [2026-07-14] Tier2 重吸補伸速度 live-tune
        std::atomic<int>    disable_phase_current_limit_ma;  // [2026-07-14] 障礙偵測相電流門檻 live-tune
        std::atomic<int>    step_margin_cm;
        std::atomic<double> imu_ask_deg;
        std::atomic<double> arm_deploy_pos_tol_rad;
        // [2026-06-02] Result of last balance calibration (Phase 5 record).
        // L_length - R_length when robot was hanging level on ropes during calibration.
        // Used to compensate for left/right weight imbalance in future motions —
        // BUT crane integration (Phase 6) NOT done yet, value is recorded but
        // does NOT take effect on motion until then.
        std::atomic<double> static_roll_offset_cm;
    } settings_;

    // Settings persistence — see Settings struct above. Returns true on file
    // I/O error (no file = silent fall-through to defaults).
    bool        load_settings_file_(const std::string& path);
    bool        save_settings_file_(const std::string& path) const;
    // Public cmd handlers (in public section below) wrap these.

    // PauseOnError mechanism — set by cmd_continue / cmd_skip while state is
    // PausedOnError. await_user_intervention_ blocks until non-None.
    std::atomic<int>     pause_action_;

    // GUI-toggleable: whether washrobot should send commands to the crane.
    // Default true (normal operation). When false: crane_cmd_ becomes a no-op
    // returning "OK skipped"; crane_watchdog_loop_ skips the ping + abort path.
    // Use case: bench testing without crane present, or when crane is in
    // manual-only mode.
    std::atomic<bool>    crane_attached_;

    // [2026-08-27 per user] IMU 傾斜保護開關（預設 on）。
    // 背景：IMU 改成立起來安裝後，pitch 卡在 -90° 附近進入 gimbal lock，此時
    // roll/yaw 在數學上無法分離、會任意跳動（bench 實測同一靜止姿態下 roll 從
    // 7.83 跳到 -71.29）。拿這種資料做 45° 緊急停判斷只會不斷誤觸發，把系統
    // 打進 Error 而讓所有指令被 state_violation_ 擋住——保護反而成了阻礙。
    // 這個旗標讓操作者在 IMU 修好前先關掉誤報，繼續測試其他子系統。
    // ⚠ 關閉 = 完全沒有傾斜保護。只適合機器在地面上、未吊掛的 bench 情境。
    // 根治要等 IMU 改用加速度計算傾斜（見 cmd_imu_guard 的說明）。
    std::atomic<bool>    imu_guard_enabled_;
    // Toggle for cleaning-arm service (defaults true at construction). When off,
    // arm_cmd_ becomes a no-op returning "OK skipped" — bench-mode safe.
    std::atomic<bool>    arm_attached_;
    // [2026-05-28] arm calibration state: tracks whether damiao M1+M2 INIT has
    // been successfully run in this session. Set true by cmd_init_impl_ on
    // successful arm INIT (or skip when arm_attached_=off). Set false by
    // emergency_stop. Sweep uses this to decide whether to send INIT
    // (calibrate) or just ENABLE (re-enable after PARK disabled motors).
    // Default false: every process restart requires explicit cmd_init.
    std::atomic<bool>    arm_calibrated_;
    // [2026-05-28] Background sweep obstacle → pause main thread on next try_or_pause_.
    // Set by do_arm_clean_sweep_continuous_ when verify_arm_deploy_ reports obstacle.
    // Cleared by try_or_pause_ after user_intervention resumes (Retry / Skip / Abort).
    // arm_sweep_skip_rest_of_run_: set by user Skip choice — sweep launchers in
    // cmd_run / cmd_step_*_with_sweep check this and bypass sweep for remaining iters.
    // Cleared at start of cmd_run / explicit cmd. detail is the slot that failed.
    std::atomic<bool>    arm_sweep_obstacle_pending_;
    std::atomic<bool>    arm_sweep_skip_rest_of_run_;
    std::mutex           arm_sweep_obstacle_mtx_;
    std::string          arm_sweep_obstacle_detail_;

    // [2026-06-03] true while do_arm_clean_sweep_ / _continuous_ has launched
    // motion (slide + arm). Set after the early skip checks (arm_attached_ /
    // skip_rest_of_run) pass, cleared in cleanup RAII guard. cycle_group_'s
    // rescue path (any_obstacle=true) waits for this to clear before doing
    // rail backup motion, so arm sweep + step rescue don't race on cli_22_
    // bus or latch stall flags on idle ZDT slaves.
    std::atomic<bool>    arm_sweep_active_{false};
    // Max wait for sweep to finish before rescue forces ahead.
    static constexpr int RESCUE_WAIT_SWEEP_MAX_MS = 15000;
    // [2026-06-06] Guard: only one end_refill detached thread at a time.
    // Without this, multiple consecutive sweeps stack up parallel refill threads,
    // each polling XKC every 200ms → 2+ thread × 200ms = 8+ reads/s on XKC,
    // races on water_inlet open/close (one closes while another still waiting).
    std::atomic<bool>    end_refill_active_{false};

    // [2026-05-29] Gate for arm_monitor_during_sweep_: when feet rail / pushers
    // are actively moving, the mechanical coupling shifts arm tau baselines
    // (body weight redistribution → M1 lever arm sees a few-tenths-Nm tau
    // change). Without this gate, the monitor reads it as a real obstacle
    // (M1 spike) and fires false-positive PAUSE-ON-ERROR. Bench 2026-05-29
    // log showed M1 d=0.488 exactly when DM2J:1 + DM2J:3 (feet rail) started
    // retracting from 5cm to 0cm during a step_up + sweep pipeline run.
    //
    // Set true around any DM2J motion that mechanically affects the arm
    // (feet rail dm2j_pair_move_*, pusher push/retract). False elsewhere.
    // Monitor sees true → freeze detection counters, don't tick;
    // monitor sees true→false transition → re-baseline M1/M2 from current
    // values so post-motion baseline is correct.
    std::atomic<bool>    dm2j_motion_active_{false};
    // Toggle for DM2J wheels (slaves 2, 4). Defaults true. When off, init()
    // skips wheel retract and cmd_wheels / cmd_dm2j_group("wheels") become
    // no-op — bench without wheels won't trip Modbus timeouts.
    std::atomic<bool>    wheels_attached_;

    // ============================================================
    // [arm rope protect TEMP 2026-05-21] — guard cleaning arm from being hit by
    // the rope / swinging pole during rope motion. Before any pay_out → stow arm
    // against wall (DEPLOY 250 CENTER); after any retract → PARK (motors off,
    // arm at home). state-tracked so loops of 1cm pay_outs / 5cm retracts don't
    // toggle arm repeatedly.
    //
    // To DISABLE wholesale: flip ARM_ROPE_PROTECTION to false. All `ensure_arm_*`
    // helpers early-return → behavior identical to pre-change.
    // To REMOVE permanently: grep for "arm rope protect TEMP" — every call site
    // is tagged for batch deletion.
    // ============================================================
    static constexpr bool ARM_ROPE_PROTECTION       = true;
    static constexpr int  ARM_ROPE_PROTECT_WALL_MM  = 400;   // 2026-08-28 per user: 380→400，跟 ARM_CLEAN_WALL_MM 統一；2026-07-27 per user: 360→380；2026-07-24: 250→360 per user；2026-05-22: 300→250 per user
    enum class ArmStowState { Unknown, Center, Parked };
    // [2026-08-28] cmd_attach 的部分密封顆數，用來讓回傳字串帶出這個資訊
    // （原本只走 console + EVT，回覆是乾淨的 "OK attached"）。
    int attach_partial_seal_ = 0;

    std::atomic<ArmStowState> arm_stow_state_{ArmStowState::Unknown};

    // [arm rope protect TEMP 2026-05-21] — obstacle detection after DEPLOY.
    // motor_api's touch_wall_slot doesn't check wait_for_move return value, so
    // M1 stalling at an obstacle silently returns OK. After DEPLOY succeeds we
    // query STATUS and compare actual M1 angle vs expected θ for wall_mm CENTER.
    // Mirror motor_api constants here for the expected θ computation:
    //   total_ext = ARM_M1_PASSIVE_EXT_MM + ARM_M2_TOOL_CENTER_MM
    //   usable    = ARM_ROPE_PROTECT_WALL_MM - total_ext
    //   expected  = ARM_M1_VERTICAL_OFF_RAD + asin(usable / ARM_M1_LENGTH_MM)
    // If these change in motor_api (main_api.h), update mirrors here too.
    static constexpr float ARM_M1_LENGTH_MM        = 320.0f;
    static constexpr float ARM_M1_PASSIVE_EXT_MM   = 86.46f;
    static constexpr float ARM_M1_VERTICAL_OFF_RAD = 0.38f;
    static constexpr float ARM_M2_TOOL_CENTER_MM   = 160.00f;
    // [2026-08-18 per user] LEFT/RIGHT SWAPPED to match the physical tool heads
    // being swapped left-for-right. Was LEFT=148.09 / RIGHT=134.07. Kept in sync
    // with cleaning_arm/main_api.h TOOL_EXT_LEFT_MM / TOOL_EXT_RIGHT_MM.
    static constexpr float ARM_M2_TOOL_LEFT_MM     = 134.07f;
    static constexpr float ARM_M2_TOOL_RIGHT_MM    = 148.09f;
    static constexpr float ARM_DEPLOY_POS_TOL_RAD  = 0.15f;   // ~8.6° / ~48mm (2026-05-22: 0.10 → 0.15, motor PD variance ~0.10 rad 自然 jitter 會誤判)

    // Set by crane_cmd_ when an EVT tension_alarm / tension_total_limit line is
    // drained from the crane TCP buffer (instead of an actual reply). watchdog
    // thread checks this each tick — if set, transitions state to PausedOnError
    // (per Q3 design 2026-05-07: crane safety alarms = manual operator review).
    std::atomic<bool>    crane_alarm_pending_;
    std::mutex           crane_alarm_mtx_;
    std::string          crane_alarm_kind_;       // "tension_alarm" / "tension_total_limit"
    std::string          crane_alarm_detail_;     // raw EVT line for context

    //=========== utility ===========

    // Maps Modbus slave ID → internal dm2j_[] index.
    // slave 1..4 (feet + wheels @ cli_20_) → index 0..3 (direct)
    // slave 14  (arm rail @ cli_22_)       → index 4   (special case, 2026-05-26)
    DM2J_RS570&        D_(int slave) {
        if (slave == DM2J_ARM) return dm2j_[4];
        return dm2j_[slave - 1];
    }
    ZDT_motor_control& Z_(int slave) { return zdt_[slave - 1]; }
    JC_100_METER&      M_(int slave) { return meter_[slave - 1]; }

    static int64_t now_ms_();
    static void    sleep_ms_(int ms);
    void           evt_(const std::string& msg);
    bool           dm2j_wait_done_(int slave, int timeout_ms = 20000);

    // Synchronized pair-motion helper for mechanically-coupled DM2J slaves.
    // Used by feet rails {1, 3} (PR1) and can be reused for wheels {2, 4} (PR2).
    // Broadcast trigger → simultaneous start; parallel status poll → both waited
    // together (no sequential blocking). Logs before/after positions + travel.
    // Bystanders' PR[pr_num] must be safe (rpm=0, set in cmd_init) so broadcast
    // doesn't drive them.
    bool           dm2j_pair_move_abs_(int slave_a, int slave_b, int pr_num,
                                        double target_cm, int timeout_ms = 20000);
    bool           dm2j_pair_poll_done_(int slave_a, int slave_b, int timeout_ms);
    // [2026-06-12] Wheels-only verify+retry helper：trigger 兩輪 (loose sync) →
    // wait_done → read_position 驗證 → 任一輪 fail 就 retry。避免「只有一邊動」。
    // 跟其他 dm2j_* helper 一致：return true = error, false = success。
    bool           dm2j_wheels_move_verified_(double target_cm);
    // Robust position read: retries until 2 consecutive reads agree within 1cm
    // tolerance. Catches occasional Modbus frame corruption (bench saw read 610
    // when actual was 5). Returns true on error (couldn't get consistent reads).
    bool           dm2j_read_pos_robust_(int slave, double& out_cm,
                                          int max_attempts = 5, double agree_cm = 1.0);
    bool           check_abort_();

    // Block until user resolves the error pause via cmd_continue / cmd_skip /
    // emergency_stop. Returns the user's chosen action.
    PauseAction    await_user_intervention_(const std::string& context);

    // Wrap any "bool fn() returning true=error" call so that on failure the
    // flow pauses (PausedOnError state), then retries / skips / aborts based
    // on user input. Returns true if user chose Abort, false otherwise
    // (success on first try OR after retries OR explicit Skip).
    template <typename Fn>
    bool try_or_pause_(Fn fn, const std::string& context) {
        while (true) {
            // [2026-06-01] Honor emergency_stop mid-flow: bail before invoking
            // fn() so body_pre_cycle / feet_pre_cycle stop running new motion
            // ops once cmd_emergency_stop sets abort_flag. Without this check
            // the entire pre_cycle (rail move + crane retract + realign) ran
            // to completion after E-stop and only the post-pre_cycle
            // check_abort_() in cycle_group_ finally noticed.
            if (abort_flag.load()) return true;

            // [2026-05-28] External pause: background arm sweep set obstacle
            // flag → pause main thread now, await user. Decoupled from fn()
            // failure (fn is a body op, sweep obstacle is on arm). 3 outcomes:
            //   Retry = "ack obstacle cleared, continue normal flow (next step's
            //            sweep launch will naturally re-attempt)"
            //   Skip  = "skip sweep for rest of this run; continue step body"
            //   Abort = "stop run"
            if (arm_sweep_obstacle_pending_.load()) {
                std::string detail;
                {
                    std::lock_guard<std::mutex> lk(arm_sweep_obstacle_mtx_);
                    detail = arm_sweep_obstacle_detail_;
                }
                PauseAction a = await_user_intervention_("arm_sweep_obstacle " + detail);
                arm_sweep_obstacle_pending_.store(false);   // ack regardless of choice
                if (a == PauseAction::Abort) return true;
                // [2026-05-29] Retry / Skip: slide was stopped mid-sweep by
                // signal_obstacle() at interrupt position. Send it back to 0
                // so the next sweep launch starts from home, matching the
                // Retry/Skip path in handle_post_sweep_obstacle_(). Without
                // this, the next sweep starts from the obstacle position.
                std::cout << "[arm_sweep_obstacle] "
                          << (a == PauseAction::Retry ? "Retry" : "Skip")
                          << " → sending slide back to 0\n";
                arm_sweep_fire_nowait_(0.0);
                arm_sweep_obstacle_pending_.store(false);   // return-to-0 may re-trigger
                if (a == PauseAction::Skip) {
                    arm_sweep_skip_rest_of_run_.store(true);
                    std::cout << "[arm_sweep_obstacle] Skip → arm_sweep_skip_rest_of_run_=true\n";
                }
                // Retry / Skip → fall through to fn()
            }
            if (!fn()) return false;             // success
            PauseAction action = await_user_intervention_(context);
            if (action == PauseAction::Abort) return true;
            if (action == PauseAction::Skip)  return false;
            // Retry: loop and call fn() again
        }
    }

    void        set_state_(State s);   // atomic + EVT state_changed
    std::string state_violation_(State cur) const;
    // internal: no state guard, caller handles transition; skip_cleaning_sweep=true 給 cmd_step_down_with_sweep 用。
    // after_feet_rail_hook：非空時，在 Phase B feet rail 回到 0 那刻呼叫一次
    // （給 cmd_step_down_sweep_after_feet 用來 launch 背景 sweep）。
    // before_feet_rail_hook：非空時，在 feet phase 的 rail DM2J move 觸發之前
    // 呼叫一次（給 cmd_step_down_sweep_before_after 用來 join pre-feet sweep round）。
    // 2026-05-27 加入。
    //
    // [2026-06-04] run_avoid probe hooks (Phase A body rail DM2J 移動相關):
    // during_body_rail_hook: DM2J move 進行中、約 80% 完成時，被 background thread
    //   呼叫一次。給 cmd_run_avoid 用來拍 "before" frame（rail 接近 step_cm 處）。
    // after_body_rail_hook: DM2J move 完成、rail 已到 step_cm 後呼叫一次。
    //   給 cmd_run_avoid 用來拍 "after" frame（rail 在 step_cm = 下一步起點）。
    // 兩個 hook 構成 motion parallax 的 before/after pair，給下一輪 detector 用。
    // right_first (2026-07-09): which side leads this step. true = right side is the
    // datum (方案B meter) + left is the IMU-leveled follower; false = swapped. Multi-
    // step runs alternate it each step; single step = true (right first).
    std::string do_step_down_(bool skip_cleaning_sweep = false,
                              std::function<void()> after_feet_rail_hook = {},
                              std::function<void()> before_feet_rail_hook = {},
                              std::function<void()> during_body_rail_hook = {},
                              std::function<void()> after_body_rail_hook = {},
                              bool right_first = true);
    // mirror of do_step_down_; skip_cleaning_sweep=true 給 cmd_step_up_with_sweep 用（sweep 由背景 thread 接手）。
    // after_feet_rail_hook：非空時，在 feet phase 的 rail DM2J move 完成那刻呼叫一次
    // （給 cmd_step_up_sweep_after_feet 用來 launch 背景 sweep）。
    // before_feet_rail_hook：對稱於 do_step_down_，在 feet rail PR_trigger 前呼叫
    // （給 cmd_step_up_sweep_before_after 用來 join pre-feet sweep round）。
    std::string do_step_up_(bool skip_cleaning_sweep = false,
                            std::function<void()> after_feet_rail_hook = {},
                            std::function<void()> before_feet_rail_hook = {},
                            bool right_first = true);   // see do_step_down_ right_first note

    // [2026-07-13 per user] 跨障礙物 cross-obstacle engine. up=false descend / up=true
    // ascend. Both sides cross; anchor side stands off to 2×preset to clear the
    // obstacle; reseals at 2×preset (cycle_group_ + feet_target_override); final
    // do_feet_realign_(force) retracts all 4 back to normal preset. See .cpp.
    std::string do_cross_obstacle_(bool up);

    // [2026-07-22 per user] Synchronized step engine backing cmd_step_down_sync/
    // cmd_step_up_sync. up=false descend (crane pay_out) / up=true ascend (crane
    // retract). Sequence: release vacuum + retract all 4 cups together → crane
    // moves BOTH ropes simultaneously by step_cm_ (bare pay_out/retract, crane-
    // side dual_vfd_sync_start — NOT the per-side alternating pay_out_left/right
    // do_step_down_/up_ use) → do_sync_imu_roll_correct_() → vacuum on + extend
    // all 4 cups together → do_step_sync_rail_sweep_() (2026-07-23: small DM2J:14
    // rail-only sweep, no arm/brush). See .cpp for the explicit zero-anchor-
    // during-move safety note.
    std::string do_step_sync_(bool up);

    // [2026-07-23 per user] Small 上滑台 (DM2J:14 only, no arm/motor_api, no
    // brush roller) sweep tacked onto the end of do_step_sync_ — same slot in
    // the sequence the full arm-clean-sweep pipeline would occupy (see
    // v2_app_redesign_plan.md §5.6), stripped down to just the rail since the
    // arm still isn't installed. 0 → DM2J_ARM_STEP_SWEEP_CM (-10cm) → 0, one
    // round trip, sequential/blocking (runs after cups are already re-sealed,
    // so no cli_22_ contention with JC100/PQW ops earlier in the same step).
    // Non-fatal: any failure/abort here is logged and the step still reports
    // OK — rail sweep isn't safety-critical the way vacuum/crane moves are.
    // init_ok: result of arm_cmd_("INIT") already run in parallel with the
    // crane rope move by the caller (do_step_sync_) — see fut_arm_init there.
    // force_enable=true 繞過 STEP_SYNC_ARM_CLEAN_ENABLED gate。只有
    // cmd_arm_clean_sweep_dry()（bench 乾掃測試）該傳 true——那支指令的用途就是
    // 手動測上滑台/手臂，被 gate 攔掉就沒東西可測了。正式步伐流程一律用預設 false。
    void do_step_sync_rail_sweep_(const char* tag, bool init_ok, bool force_enable = false);

    // [v2] Feet-only realign: retract all 4 feet cups back to preset while they
    // stay SEALED (valves ON → vacuum pulls the machine toward the wall; cups
    // never release, so the "4 cups anchored" invariant holds). No crane rope.
    //   apply_threshold : true  → only run when drift exceeds REALIGN_THRESHOLD_CM
    //                             (single) or REALIGN_THRESHOLD_MEAN_CM (mean);
    //                             used by the end-of-step auto-call so realign only
    //                             fires once drift accumulates. Skip returns "".
    //                     false → run on any nonzero drift (manual cmd_realign).
    //   caller_holds_lock: true → caller (do_step_*_) already holds motion_mtx_ and
    //                             owns motion_active_ — don't re-lock / don't flip
    //                             the flag (same-thread deadlock guard).
    // Returns "" on success / skipped; "ERR ..." on failure (caller decides).
    std::string do_feet_realign_(bool apply_threshold = false, bool caller_holds_lock = false);

    // [方案B 2026-07-08] Read both crane meters (length_left/right, cm). Returns
    // true on failure (crane detached / meter invalid / parse fail), false + both
    // filled on success. Inverse convention (true = error).
    bool read_crane_meters_(double& len_left, double& len_right);

    // [方案B 2026-07-08] Build the crane command to move one step side to a
    // pre-computed common ABSOLUTE target length (locked at step start from both
    // meters), so neither side ever travels more than ~one step catching up after
    // a failed reseal (replaces 方案1 master-fixed/follower-match, which let the
    // follower swing up to 2×step). Reads this side's own meter fresh, moves the
    // signed delta toward target_len (pay_out if longer needed / retract if
    // shorter). Per-move clamped to step+LEVEL_MOVE_MARGIN_CM (remainder next
    // step). !target_valid or meter read fail → fixed `dir_word <step>` fallback.
    // Returns "" when already at target (within LEVEL_MATCH_TOL_CM). out_timeout set.
    // [2026-07-15 per user] out_mv_cm = the actual cm this call is moving this
    // side (0 if already at target; step if the fixed-step fallback fired).
    // Callers must use THIS (not the flat `step`) as the retry/backup retreat
    // budget — otherwise a leading side that only needed to move e.g. 20cm of
    // a 35cm step could retreat the full 35cm during vacuum retries and end up
    // BELOW where it started before this step.
    std::string crane_abs_target_cmd_(const std::string& move_group,
                                      const std::string& dir_word,
                                      int step, bool target_valid,
                                      double target_len, int& out_timeout,
                                      double& out_mv_cm);

    // [策略1 2026-07-09] Fine-level the follower (second-moving) side to the datum
    // (first side already resealed) using IMU roll, after its coarse measured
    // descent and BEFORE re-extending its cups. Iterates small tension-safe
    // measured moves until |roll-baseline| < FOLLOWER_ROLL_TOL_DEG. No raw-on
    // (keeps cmd_side_measured's tension/meter-death safety). Non-fatal & best-
    // effort: skipped if IMU unhealthy / disabled; logs+EVTs outcome, never blocks
    // the step. move_group = follower side ("left" in the current gait). See §12.
    void follower_imu_level_(const std::string& move_group);

    // [2026-07-22] Differential IMU roll correction for do_step_sync_ — unlike
    // follower_imu_level_ (nudges ONE side against an already-resealed datum
    // side), do_step_sync_ has no datum side (all 4 cups released together), so
    // this drives crane's "roll_correct <delta_cm>" differential primitive
    // instead (both ropes move oppositely in one call). Non-fatal: any failure
    // or non-convergence just leaves residual tilt for the next step, same
    // philosophy as follower_imu_level_.
    void do_sync_imu_roll_correct_();

    // [2026-06-02] Orchestrate balance calibration Phase 1-4. Runs synchronously
    // in caller's thread (typically cmd_balance_calibrate_start's TCP handler
    // thread). Polls balance_cal_abort_requested_ between phases. Returns "" on
    // convergence (await record), "ERR <reason>" on timeout / watchdog / abort.
    std::string do_balance_calibrate_();
    // Helpers for individual phases (so the GUI-friendly EVT emit is clean):
    std::string bal_cal_preload_();          // Phase 1
    std::string bal_cal_release_body_();     // Phase 2
    std::string bal_cal_release_feet_center_(); // Phase 3
    std::string bal_cal_balance_loop_();     // Phase 4
    // Read tension from crane status reply. Returns true on parse error.
    bool        bal_cal_read_tensions_(double& l_kg, double& r_kg);
    // Read SD76 lengths from crane status reply. Returns true on parse error.
    bool        bal_cal_read_lengths_(double& l_cm, double& r_cm);
    // Set phase string (mutex-protected) + broadcast EVT for GUI.
    void        bal_cal_set_phase_(const std::string& phase);

    // Helper: max over-extension (cm) across feet slaves vs preset. 0 if all at preset.
    // [2026-06-05] Return value is CAPPED at FEET_MAX_OVER_CAP_CM (snowball protection
    // fix B) to prevent body target = preset + feet_over × 3000 from exceeding the
    // body pusher's physical reach (~60000 pulses).
    double      feet_max_overextend_cm_() const;
    // Helper: convert cm overextension to ZDT pulses for the given slave's group.
    static int  cm_to_pulses_for_slave_(int slave, double cm);
    // [2026-06-05] Snowball protection (fix C) — get capped feet target for a slave.
    // Returns min(last_seal_pulse_[slave-1], preset + FEET_TARGET_OVER_CAP_CM cm).
    // Called by cycle_group_ feet branch + smart_extend_subset_ feet branch.
    int         feet_target_capped_(int slave) const;

    // Update last_seal_pulse_[s-1] with confirmed seal pulse (called by cycle_group_/fine_tune)
    void        record_seal_pulse_(int slave, int pulse);
    // Reset last_seal_pulse_ for a group back to preset (called by realign)
    void        reset_seal_pulse_group_(const std::string& group);
    // Get the preset extend pulse for slave (per-slave for body 7,8 SHORT)
    int         preset_extend_pulse_for_slave_(int slave) const;

    //=========== crane ===========

    bool        crane_connect_if_needed_();
    std::string crane_cmd_(const std::string& line, int timeout_sec = 60);    // 30 → 60 (2026-05-11): give fine_adjust 30s budget on top of main motion

    //=========== cleaning arm ===========

    // [REMOVED 2026-06-03] arm_connect_if_needed_() — TCP_client.reconnectLoop()
    // owns socket lifecycle; manual reconnect raced with background thread,
    // causing motor_api to see 3 simultaneous source-port connections + ~30s
    // recovery (bench 2026-06-03).
    std::string arm_cmd_(const std::string& line, int timeout_sec = 30);
    // Mirrors arm_cmd_ exactly (lazy connect + background reconnect via
    // TCP_client, 2-attempt retry, no retry on recv timeout). Longer default
    // timeout than arm_cmd_ — AFTER runs optical flow + plane fit + connected
    // components, can take longer than a simple motor status round-trip.
    std::string depth_cam_cmd_(const std::string& line, int timeout_sec = 10);

    // [arm rope protect TEMP 2026-05-21] — gated by ARM_ROPE_PROTECTION.
    // Both return true on error, false on success / no-op.
    // ctx string is just for log clarity ("body_pre_pay_out" etc.).
    bool ensure_arm_center_for_rope_(const std::string& ctx);
    bool ensure_arm_parked_after_rope_(const std::string& ctx);
    // [2026-05-28] Ensure damiao arm is ready for DEPLOY without re-calibrating.
    // Replaces the per-sweep arm_cmd_("INIT", 60) — INIT now runs only in
    // cmd_init_impl_. Behavior:
    //   - arm_attached_=off → return false (no-op success; sweep already skips)
    //   - arm_calibrated_=false → return true (error; operator must run cmd_init)
    //   - else → send "M1 ENABLE" + "M2 ENABLE" (re-enable motors after PARK
    //     disabled them, keep existing zero calibration intact)
    // Returns true on error (convention).
    bool ensure_arm_ready_();
    // [2026-05-28] Replace plain sleep_ms_(ARM_SWEEP_EST_MS) in arm_sweep_fire_nowait_
    // with a poll loop that watches for obstacles:
    //   A: DM2J:14 status alarm bit (slide motor stall / over-current)
    //   C: damiao M2 tau spike vs baseline captured at entry
    // On detection: set arm_sweep_obstacle_pending_ + detail + EVT, then return
    // early. Main thread's try_or_pause_ external-pause check picks it up next op.
    // est_ms (2026-07-23 per user, default ARM_SWEEP_EST_MS): the plain-sleep
    // fallback duration and polling-loop total duration scale to this instead
    // of the hardcoded 55cm/1000rpm estimate — do_step_sync_rail_sweep_ passes
    // its own much-shorter DM2J_ARM_STEP_SWEEP_EST_MS for its 10cm move.
    void arm_monitor_during_sweep_(int est_ms = ARM_SWEEP_EST_MS);
    // [2026-05-29] Post-sweep obstacle handler — for continuous sweep mode.
    // Background sweep can only set arm_sweep_obstacle_pending_ flag + stop slide
    // (can't safely call await_user_intervention_ from non-main thread). Main
    // thread explicitly handles after joining sweep future.
    //   - flag set → await_user_intervention_ for user decision
    //   - Retry / Skip: send slide back to 0 (so next sweep starts from home)
    //   - Abort: return true (don't move slide; caller propagates ERR)
    //   - flag clear → no-op, return false
    // Returns true on Abort, false otherwise.
    bool handle_post_sweep_obstacle_(const std::string& context);
    // [arm rope protect TEMP 2026-05-21] verify M1 actually reached expected θ.
    // slot = "LEFT" / "CENTER" / "RIGHT", wall_mm matches arm_cmd_ DEPLOY arg.
    // Returns true on obstacle / STATUS parse fail; false on OK or skip
    // (ARM_ROPE_PROTECTION off / arm_attached off).
    bool verify_arm_deploy_(const std::string& slot, int wall_mm);
    // [2026-06-06] M2 slot verify (independent of M1 angle verify). After DEPLOY,
    // reads motor_api STATUS and compares M2 pos against expected slot angle
    // (LEFT=-0.7 / RIGHT=+0.7 / CENTER=0.0 rad). Returns true if M2 NOT at slot
    // (|pos-target| > ARM_M2_SLOT_TOL_RAD). Used by do_arm_clean_sweep_* to retry
    // DEPLOY when motor_api's lr_move_to_slot times out short of target without
    // reporting failure (observed pattern: M2 at -0.58, target +0.7, lr_move_to_slot
    // prints "Done" before reaching).
    bool verify_arm_m2_at_slot_(const std::string& slot);
    static constexpr float ARM_M2_SLOT_TOL_RAD = 0.30f;
    static constexpr int   ARM_M2_VERIFY_RETRIES = 4;     // total attempts = 1 + retries
                                                          // 2026-06-09h: 2→4 (5 total)。M2 馬達進水
                                                          // intermittent fail，bench 需要多 retry 才能 settle
    void        crane_watchdog_loop_();
    void        handle_crane_evt_(const std::string& line);   // dispatches EVT lines drained from RPC channel

    // Read max rope tension (kg) with crane DSZL-107 as primary source.
    // 1. Primary: crane_cmd_("tension"), parse "left=<kg> right=<kg>" → return max
    // 2. Fallback: washrobot-end DY-500 cache (slave 10/11, if installed)
    // [2026-08-04 per user] 3rd fallback (easy crane weight via crane_shim)
    // removed — Crane_easy_PI hardware decommissioned.
    // Returns kg; WEIGHT_NO_DATA_KG if all sources fail.
    double      read_rope_weight_max_kg_();

    // Read max rope weight (kg) via the dedicated estop channel — bypasses
    // crane_mtx_, so it works WHILE a retract holds that mutex (the normal
    // read_rope_weight_max_kg_ would block). Used by crane_retract_safe_'s
    // active monitor. Returns kg; -1 on comm/parse fail / detached.
    double      read_rope_weight_estop_();
    // Returns the per-sensor weight limit appropriate for current state.
    double      rope_weight_limit_per_sensor_kg_() const;
    // Wraps `crane_cmd_("retract <cm>")` with weight-based safety.
    //   1. Pre-check: if already > limit / sensor offline → refuse with ERR
    //   2. Active monitor: watcher polls weight via the estop channel every
    //      WEIGHT_MONITOR_POLL_MS; on overweight → send "stop", return OK
    //      (early stop treated as the retract having reached its goal).
    // Returns same string format as crane_cmd_ ("OK ..." / "ERR ...").
    // [2026-06-05] Dynamic timeout helper for crane pay_out / retract calls.
    // Empirical: crane at 30Hz base ≈ 10 cm/sec, fine_adjust typically 1-3s,
    // overhead 1-2s. Formula: ceil(cm/10) + 5s buffer.
    //   cm=5  → 6s, cm=10 → 6s, cm=20 → 7s, cm=41 → 10s, cm=80 → 13s
    // Tighter than default 60s → real hangs detected faster. If a slow case
    // hits (rope swaying delays fine_adjust), user RETRY usually resolves it.
    static int crane_motion_timeout_sec_(int cm) {
        if (cm <= 0) return 5;
        return (cm + 9) / 10 + 5;   // ceil(cm/10) + 5
    }

    // timeout_sec=0 → auto-pick via crane_motion_timeout_sec_(cm).
    std::string crane_retract_safe_(int cm, int timeout_sec = 0);

    // Incremental pay_out until BOTH rope tensions drop to <= target_kg or
    // max_cm hit. Sends 1cm pay_out at a time, polls per-side DSZL-107 tension
    // ("OK left=<kg> right=<kg>"), repeats. Used at end of cmd_attach to shift
    // body weight onto the cups once all are sealed.
    std::string crane_pay_out_to_weight_(double target_kg, int max_cm);

    // [2026-06-02] Per-side retract until BOTH sides hit target_kg. Mirror of
    // crane_pay_out_to_weight_ but in retract direction, AND uses single-side
    // up_left/up_right hold cmds instead of symmetric retract — necessary for
    // heavily imbalanced robots where crane's `retract` cmd + max(L,R) soft-stop
    // would let one side hit target while the other stays slack forever.
    //
    // Loop: read L/R → if either < target, pulse that side's up cmd briefly →
    // sleep settle → re-read → repeat. Returns "" on success, "ERR ..." on
    // overweight (safety_max) / max_iter exhausted / sensor offline.
    std::string crane_retract_to_weight_(double target_kg, double safety_max_kg,
                                          int max_iter,
                                          int pulse_ms = 300, int settle_ms = 500);

    //=========== IMU ===========

    bool        imu_take_baseline_();

    // [2026-08-27] ⚠ 目前未被呼叫，保留備用。
    // 當日 IMU 一度改成立起來安裝（X 軸朝上）而尤拉角卡 gimbal lock，曾用這個
    // 函式取代尤拉角；後來 user 換了另一顆並改回水平安裝，monitor / baseline /
    // status 三處都已改回內建尤拉角（水平安裝下尤拉角無死角、且有陀螺儀融合，
    // 動態表現優於純加速度換算）。
    // 保留原因：若日後 IMU 又必須非水平安裝，這裡的推導與安全處理可直接沿用，
    // 只需依實際方位改軸並接回 imu_monitor_loop_。imu_monitor_loop_ 內已有
    // |az| < 0.70 的安裝方位檢查會主動警告該情況。
    //
    // 由加速度計算傾斜角（以下推導對應「X 軸朝上、Y 軸朝左」的方位）。
    // IMU 安裝方位（user 提供）：X 軸朝上、Y 軸朝左（Z 軸依右手座標朝後）。
    // 靜止水平時重力全部落在 X 軸 → ax≈±1、ay≈0、az≈0；機器左右傾斜時重力
    // 會分一部分到 Y 軸，故 roll = atan2(ay, ax)。
    // 為什麼不用尤拉角：IMU 立起來後 raw_y ≈ -90°（從平躺繞 Y 軸轉 90°），
    // 尤拉角在此進入 gimbal lock，roll 與 yaw 數學上無法分離、會互相耦合亂跳
    // （bench 實測同一靜止姿態 raw_z 從 7.83 跳到 -71.29）。重力方向不受此
    // 影響，也不依賴磁力計，是這個安裝方位下唯一可靠的傾斜來源。
    // 回傳 true = 失敗（沒有可用的加速度資料），false = 成功（專案慣例）。
    bool        imu_tilt_from_accel_(double& roll_deg, double& pitch_deg) const;
    std::string do_phase5_roll_correct_();
    void        imu_monitor_loop_();

    //=========== arm ===========

    std::string do_arm_sweep_();  // internal: caller must hold motion_mtx_
    // Internal cleaning sweep — caller must hold motion_mtx_ (used by
    // cmd_arm_clean_sweep and by do_step_up_ / do_step_down_ end-of-step).
    std::string do_arm_clean_sweep_(int wall_mm, int rounds);
    // Continuous cleaning sweep — runs LEFT/RIGHT rounds in a loop until
    // keep_going flips to false OR max_rounds is reached (2026-05-22 / 2026-05-25).
    // Caller does NOT need motion_mtx_; helper uses arm_cli_ + cli_22_ independently
    // of main motion thread's cli_20_/cli_21_ ops.
    //   keep_going  — atomic flag, set false to stop after current round
    //   max_rounds  — 0 (default) = unlimited (rely on keep_going only)；
    //                  N>0       = exit after N rounds even if keep_going stays true
    //                              (用於 _sweep_after_feet 場景：固定 1 round)
    std::string do_arm_clean_sweep_continuous_(int wall_mm,
                                                std::atomic<bool>& keep_going,
                                                int max_rounds = 0);
    // Fire-and-forget arm rail move. Uses PR_move_cm_nowait (no status poll
    // → contention-immune on cli_22_). Re-fires ARM_SWEEP_FIRE_RETRIES times
    // (50ms spacing) for redundancy against lost Modbus writes, then sleeps
    // ARM_SWEEP_EST_MS to let the arm physically reach target before next fire.
    // rpm/acc/dec/est_ms (2026-07-23 per user, default ARM_SWEEP_* — every
    // existing caller unaffected): lets do_step_sync_rail_sweep_ fire at its
    // own DM2J_ARM_STEP_SWEEP_* speed/estimate instead of the 55cm/1000rpm
    // tuning this function was originally built around.
    //
    // [2026-08-28] 回傳 true = 至少有一次寫入成功（滑台真的在動）；
    //              false = ARM_SWEEP_FIRE_RETRIES 次全滅，滑台完全沒動，
    //                      且已跳過 est_ms 等待（沒東西在動就不用等）。
    // 原本回傳 void、把 PR_move_cm_nowait 的結果整個丟掉，於是 DM2J:14 掛在錯的
    // gateway 時，log 仍照印「rail sweep done」——看起來一切正常。
    // 呼叫端可以忽略回傳值（語意與舊版相同），要精確回報時再接。
    bool arm_sweep_fire_nowait_(double target_cm,
                                 int rpm = ARM_SWEEP_RPM, int acc = ARM_SWEEP_ACC, int dec = ARM_SWEEP_DEC,
                                 int est_ms = ARM_SWEEP_EST_MS);

    //=========== pusher / vacuum ===========

    // defer_stall_release: when true, a stall during this move is NOT treated as
    // failure — flag is left set and we return success. Caller (cycle_group_ /
    // fine_tune extend) releases after vacuum confirms seal. Use case: cup hits
    // wall during extend; keeping motor in stall state holds cup pressed against
    // wall while vacuum builds.
    bool             pusher_move_(int slave, int pulse, int rpm = PUSHER_RPM, int acc = PUSHER_ACC, bool defer_stall_release = false);
    bool             pusher_move_many_(const std::vector<int>& slaves, int pulse, int rpm = PUSHER_RPM, int acc = PUSHER_ACC, bool defer_stall_release = false);
    // Pipelined two-stage retract: stage 1 slow-peels RETRACT_SLOW_PEEL_CM off
    // the wall (sync start), then each slave — the moment it finishes stage 1 —
    // immediately fires stage 2 (fast retract to 0) without waiting for siblings.
    // Returns true (error) on stall / timeout. Replaces the old pusher_move_many_
    // ×2 retract pattern at every call site.
    bool             pusher_two_stage_retract_(const std::vector<int>& slaves);

    // Group extend with concurrent vacuum monitoring. As cup pressure crosses
    // VACUUM_EARLY_STOP_KPA mid-motion, immediately emergency_stop that slave's
    // ZDT to prevent over-compression once cup has sealed against wall. Stall
    // detected during motion is treated as deferred (flag left set, success).
    // Caller releases stall flags after vacuum check finishes.
    // Returns false on success (all slaves resolved: sealed-stopped / stalled-at-wall
    // / reached target naturally), true on timeout or comm send fail.
    bool             pusher_extend_with_vacuum_stop_(const std::vector<int>& slaves,
                                                       const std::vector<int>& pulses,
                                                       int rpm = PUSHER_RPM, int acc = PUSHER_ACC);

    // Disable-seal extend: brief push → motor disable → passive vacuum wait, iterated.
    // Avoids continuous slow-push (which over-stresses cup + reaction-loads other group).
    //   Phase 1: fast extend per-slave to (target - PHASE1_BUFFER_PULSES) at fast_rpm
    //   Phase 2: iterative loop, up to DISABLE_RETRY_MAX_ITERS:
    //     A) re-enable not-done slaves
    //     B) pre-push vacuum check — if already ≤ SEAL_DEEP, mark DONE
    //     C) send +DISABLE_RETRY_INCR_PULSE relative push at PUSHER_RPM_DISABLE_SLOW
    //        (skip slaves whose cumulative push hit DISABLE_RETRY_MAX_OVEREXTEND → weak_seal)
    //     D) sync trigger + wait motion done; on phase_current/pos_error spike → obstacle,
    //        on stall_flag → defer + lock; on settle (real_speed≈0) → continue
    //     E) emergency_stop + disable not-done slaves
    //     F) wait up to VACUUM_DEEPEN_TIMEOUT_MS; poll vacuum, mark DONE as cups seal
    //   Cleanup: any remaining not-done → weak_seal, force re-enable + record pulse
    //
    // Records final position into last_seal_pulse_ for each slave.
    // Returns true on hard fail (Phase 1 pos_mode send rejected), false otherwise.
    // Caller must hold motion_mtx_ and pre-open valve.
    //
    // any_obstacle_out (optional, 2026-05-15h): if non-null, set to true when ANY
    // slave's internal obstacle[] flag was set during the seal cycle (pos_error
    // + phase_current both above limits during push). Used by cycle_group_ to
    // trigger obstacle rescue (bigger backup, doesn't consume vacuum retry).
    //
    // stop_on_first_seal (2026-07-08 per user): when true, the iter push-loop
    // exits as soon as AT LEAST ONE cup in `slaves` TRULY sealed (not weak /
    // obstacle) — remaining un-sealed cups are NOT pushed further; the wrap-up
    // re-enables their EN + locks position. Used by step_down/up feet extend so
    // a side proceeds on the first sealed cup. Default false = seal all cups
    // (attach / manual / probe keep the original "push every cup" behaviour).
    // max_iters (2026-07-14): Phase 2 iter-loop cap override. 0 = use
    // DISABLE_RETRY_MAX_ITERS (default). feet_topup_ passes a small value (2) so
    // the best-effort 2nd-cup top-up gives up quickly → shorter group-switch gap.
    // stop_group_ids (2026-07-23 per user): optional, same size as `slaves`.
    // When null (default — every existing caller), stop_on_first_seal behaves
    // exactly as before: ANY slave in `slaves` sealing stops the WHOLE call.
    // When provided, each slave's group id partitions `slaves` into
    // independent stop domains — a group only stops pushing/polling ITS OWN
    // members once ANY member of THAT group truly seals; other groups keep
    // going until they independently satisfy their own bar (or MAX_ITERS).
    // Lets one single simultaneous 4-slave call (one trigger_sync_move) give
    // do_step_sync_ per-side early-stop without splitting into two sequential
    // calls — see do_step_sync_ for the caller that needs this.
    bool             pusher_extend_with_disable_seal_(const std::vector<int>& slaves,
                                                       const std::vector<int>& target_pulses,
                                                       int fast_rpm = PUSHER_RPM,
                                                       int acc = PUSHER_ACC,
                                                       bool* any_obstacle_out = nullptr,
                                                       bool stop_on_first_seal = false,
                                                       int max_iters = 0,
                                                       const std::vector<int>* stop_group_ids = nullptr);

    // Smart extend on a subset of slaves in a given group. Mirrors cycle_group_'s
    // extend section: per-slave start_pulses (from last_seal_pulse_ + body delta),
    // vacuum-aware early stop, fine_tune补伸 with obstacle detection, record seal
    // pulse on success.
    // Caller must hold motion_mtx_ and pre-set valve to desired state.
    //   group  : "feet" / "body" / "center"
    //   slaves : subset of group_slaves_(group); for full group pass group_slaves_(group)
    // Returns true on hard fail (extend send / fine_tune size mismatch), false otherwise.
    // stop_on_first_seal (2026-07-23 per user, default false = existing
    // cmd_attach behavior "try to seal every cup"): true makes the underlying
    // pusher_extend_with_disable_seal_ stop pushing a slave's group the moment
    // any ONE member of that group seals.
    // stop_group_ids (2026-07-23 per user, optional): same size as `slaves`,
    // partitions them into independent stop domains — pass this when calling
    // with a mixed multi-side slave list (e.g. all 4 in one simultaneous call)
    // so each side stops independently instead of the whole call stopping on
    // the first cup sealing ANYWHERE. Omit (nullptr) when `slaves` is already
    // a single side/group — see pusher_extend_with_disable_seal_ for detail.
    bool             smart_extend_subset_(const std::string& group, const std::vector<int>& slaves,
                                           bool stop_on_first_seal = false,
                                           const std::vector<int>* stop_group_ids = nullptr);

    // After group broadcast extend, monitor vacuum per-cup and incrementally
    // extend unsealed cups (up to base + FINE_TUNE_MAX_OVEREXTEND). Returns
    // the final list of cups still failing vacuum (empty = all sealed).
    // Best-effort — never fails the cycle, falls through to existing retry path.
    std::vector<int> fine_tune_extend_per_slave_(const std::vector<int>& slaves,
                                                  const std::vector<int>& start_pulses,
                                                  const std::string& group);
    bool             zdt_wait_motion_done_(int slave, int timeout_ms = 15000, bool defer_stall_release = false);
    // Parallel-poll variant: waits for all slaves to reach stable (speed=0+pos stable)
    // or timeout. Returns true (error) on timeout or stall (when !defer_stall_release).
    // Mirrors pusher_move_many_'s inline poll loop — slaves doing broadcast sync motion
    // resolve near-simultaneously, so parallel poll = max(slave time) instead of
    // sum(slave time). Used by disable_seal Phase 1 + realign phase 2 (2026-05-28).
    // stalled_slave_out (optional): if non-null and the function returns true due to
    // a stall, written with the slave id that triggered the stall (for diagnostic /
    // evt_ messages). Untouched on timeout or success.
    bool             zdt_wait_motion_done_many_(const std::vector<int>& slaves, int timeout_ms = 15000, bool defer_stall_release = false, int* stalled_slave_out = nullptr, std::vector<uint16_t>* peakI_out = nullptr);
    // Returns slaves for the group, minus any in disabled_zdt_slaves_.
    std::vector<int>        group_slaves_(const std::string& group) const;
    static int              group_valve_ch_(const std::string& group);
    bool             vacuum_valve_(const std::string& group, bool on);
    // Set PQW relay (1-based ch) and verify via FC01 readback. Up to 3 retries
    // (50ms apart) if state mismatch. Guards against USR gateway silently dropping
    // FC05 when RS485 bus busy. Returns false on success (or if verify-impossible),
    // true only on TCP send failure.
    bool             pqw_set_relay_verified_(int ch, bool on);

    // [2026-06-05] Water inlet ball valve moved to crane side (PQW on
    // 192.168.1.34 slave 12 CH4). All washrobot-side callers go through
    // this helper, which sends "water_inlet on|off" via crane_cmd_.
    // Returns false on success, true on error (TCP fail / crane refused).
    // Used by cmd_water_inlet (GUI), init cleanup, sweep flows, shutdown.
    // Bypasses state guard (used by motion-active paths).
    bool             set_water_inlet_(bool on);

    std::vector<int> vacuum_check_(const std::string& group);
    // [2026-07-08 per user] Per-side "sealed enough" test used by step_down/up.
    // Returns true if AT LEAST ONE cup in `group` reached VACUUM_THRESHOLD_KPA
    // (was: all cups). Fills out_unsealed with the cups that did NOT seal (for
    // logging/telemetry). A side has 2 cups → true unless BOTH failed.
    bool             group_seal_ok_(const std::string& group, std::vector<int>& out_unsealed);
    // [2026-07-08 per user] Best-effort synchronous "top-up": after a side has
    // sealed >=1 cup (stop_on_first_seal), re-run the SAME disable_seal pipeline
    // on JUST that side's still-unsealed cup(s) to try to recover 2-cups-per-side.
    // Preserves ALL obstacle / wall / weak-seal DETECTION (reuses the extend
    // helper), but does NO rescue and NEVER toggles the valve or retracts — the
    // already-sealed cup keeps holding (shared per-side valve stays ON). NON-FATAL:
    // any outcome (seal / weak / obstacle / hard-fail) just proceeds; the next
    // step's cycle_group_ retries the cup from scratch with full rescue. Caller
    // must hold motion_mtx_ and have the group's valve already ON.
    void             feet_topup_unsealed_(const std::string& group);
    // Poll JC-100 every 200ms until all listed slaves' pressure rises above
    // DETACH_THRESHOLD_KPA (-10 kPa) OR timeout. Returns false on success
    // (all released), true on timeout (any slave still attached or comms fail).
    // Used between "valve OFF" and "pusher retract" steps to guarantee cups
    // have actually released before pulling pushers (ZDT stalls otherwise).
    bool             vacuum_wait_release_(const std::vector<int>& slaves, int timeout_ms);

    // Pre-retract safety check: ensure the OTHER (still-holding) group has no
    // latched stall_flag before we release vacuum + retract this group. A stalled
    // motor on the holding group means firmware will reject future motion cmds
    // (e.g. when that group eventually retracts in the next phase). Reads stall
    // status, releases any latched flags, verifies clear. Idempotent — no-op if
    // no stall was set. false=clear, true=persistent stall after release attempt.
    bool             ensure_group_stall_clear_(const std::string& group);
    // Clear stall_flag on all 9 ZDT slaves (skipping disabled). Used at start of
    // step_down/step_up pre_cycle to catch any latched stall from previous extend
    // (defer mode) — otherwise next pos_mode is silently rejected by firmware.
    // Returns false on success, true if persistent stall remains after release.
    bool             ensure_all_zdt_stall_clear_();

    // After releasing one group's vacuum and BEFORE retracting that group's
    // pushers, check the OTHER group (still load-bearing) for latched stall flags
    // and clear them. Vacuum release / mechanism shift may load other-group cups
    // asymmetrically; latched stall would silently reject their next pos_mode →
    // mechanical damage when other-group cup ends up dragged. group ∈ {feet, body}.
    bool             clear_other_group_stalls_(const std::string& current_group);

    // Core motion cycle — template body must be visible at call site (defined below)
    template <typename PreCycle, typename Backup, typename RescueBackup>
    std::string cycle_group_(const std::string& group,
                             PreCycle     pre_cycle,
                             Backup       backup,
                             RescueBackup rescue_backup,
                             int&         out_retry_count,
                             int&         out_rescue_count,
                             // [2026-07-13] optional per-slave feet extend-target
                             // override. Empty (default) → normal feet_target_capped_
                             // behaviour (do_step_*_ unaffected). Used by
                             // do_cross_obstacle_ to reseal at 2×preset. Ignored for
                             // the "body" group.
                             std::function<int(int)> feet_target_override = {});

    // Internal impl — the real init logic. Public cmd_init() wraps this so it
    // can broadcast an EVT init_complete regardless of success/failure.
    std::string cmd_init_impl_();

    //=========== scripted_run (2026-06-05) ===========
    //
    // CSV format: comma-separated tokens. Each token =
    //     <int>[n]['*'<count>]
    // - <int> = step cm (range STEP_CM_MIN..STEP_CM_MAX)
    // - optional 'n' suffix → no-sweep step (transit only; calls
    //   do_step_down_(skip_cleaning_sweep=true) directly, skipping Phase C
    //   cleaning sweep). Default (no suffix) = sweep step
    //   (cmd_step_down_sweep_after_feet). 99% of steps are sweep, so default
    //   keeps the common case shortest and preserves backward-compat with
    //   pre-2026-06-05 saved scripts that knew nothing of the flag.
    // - optional "*<count>" = repeat shorthand.
    // Examples:
    //     "30,20,50"        → 3 sweep steps
    //     "30n,30,30"       → 1 transit + 2 sweep (e.g. skip 30cm then clean)
    //     "30n*3,30*5"      → 3 transit + 5 sweep
    //     "30,30n*2,30"     → sweep, transit, transit, sweep
    // - optional "x" suffix = 跨障礙物 (cross-obstacle) step (do_cross_obstacle_ down,
    //   2×preset stand-off). Overrides sweep (cross has no arm sweep). [2026-07-13]
    //     "30,30x,30"       → sweep, cross-obstacle, sweep
    //     "30x*2"           → 2 cross-obstacle steps
    // Persistence: ./scripts.json — same flat key=value format as settings.json
    // (key = script name, value = original CSV string). Loaded once at startup.
    static constexpr int SCRIPT_TOTAL_STEP_MAX = 1000;   // soft cap on expanded step count
    static constexpr int SCRIPT_REPEAT_MAX     = 1000;   // soft cap on a single `*N` multiplier
    static constexpr int SCRIPT_NAME_MAX_LEN   = 32;     // [A-Za-z0-9_-]{1,32}

    // One step of a scripted run: cm + per-step sweep flag + cross-obstacle flag.
    // sweep=true  → cmd_step_down_sweep_after_feet(cm)
    // sweep=false → do_step_down_(skip_cleaning_sweep=true) — pure down, no
    //               arm sweep at all (transit only).
    // cross=true  → do_cross_obstacle_(down) — 2×preset stand-off cross ("x" suffix;
    //               overrides sweep — cross has no arm sweep). [2026-07-13 per user]
    struct ScriptStep { int cm; bool sweep; bool cross; };

    std::map<std::string, std::string> saved_scripts_;   // name → CSV
    std::mutex                         saved_scripts_mtx_;

    // Parse CSV into a flat vector of ScriptStep. Returns false on failure
    // with err filled in. Empty input → false with err="csv_empty".
    bool        parse_script_csv_(const std::string& csv,
                                  std::vector<ScriptStep>& out,
                                  std::string& err);
    // Validate script name (alnum + underscore + dash, 1..SCRIPT_NAME_MAX_LEN).
    bool        validate_script_name_(const std::string& name);
    // Disk persistence — same key=value format as settings.json.
    // Returns false on success, true on I/O / parse error (matches project conv).
    bool        load_saved_scripts_from_disk_(const std::string& path = "scripts.json");
    bool        save_saved_scripts_to_disk_  (const std::string& path = "scripts.json");
};

// ---- cycle_group_ template definition ----
// Phase 4 vacuum attach cycle with backup-on-retry + obstacle rescue.
//
//   pre_cycle       : () -> string         called ONCE before first attempt
//   backup          : (bool dry_run) -> string  vacuum retry — small (5cm) rail backup
//                                               dry_run=true: feasibility check ONLY
//                                               dry_run=false: perform actual backup
//   rescue_backup   : (bool dry_run) -> string  obstacle rescue — bigger (10cm) rail
//                                               backup, used when extend hit obstacle
//                                               BEFORE vacuum could be evaluated
//   out_retry_count : count of vacuum retries used (= attempt index of success)
//   out_rescue_count: count of obstacle rescues used (across all attempts)
//
// Flow:
//   (Clog_Ma firmware guard DISABLED 2026-05-19 — no longer touches the ZDT
//     driver 賭轉電流; obstacle detection is now pure software phase-current
//     judgment. See the #if 0'd block below.)
//   pre_cycle()
//   for vacuum_attempt = 0 .. VACUUM_RETRY_MAX:
//     if vacuum_attempt > 0:
//       backup(true) feasibility → valve OFF → retract → backup(false)  (5cm)
//     rescue_loop (up to OBSTACLE_RESCUE_MAX times):
//       extend pushers (disable_seal, reports any_obstacle)
//       if any_obstacle:
//         if rescue used up → return "obstacle_rescue_exceeded <group>"
//         else: rescue_backup(true) feasibility → valve OFF → retract →
//               rescue_backup(false) (10cm) → loop back to re-extend
//       else: break out of rescue_loop
//     valve ON + verify vacuum (already done inside disable_seal)
//     if OK: out_retry_count = vacuum_attempt; return ""
//   return "vacuum_retry_exceeded <group>"
//
template <typename PreCycle, typename Backup, typename RescueBackup>
std::string WashRobot::cycle_group_(const std::string& group,
                                    PreCycle     pre_cycle,
                                    Backup       backup,
                                    RescueBackup rescue_backup,
                                    int&         out_retry_count,
                                    int&         out_rescue_count,
                                    std::function<int(int)> feet_target_override) {
    const int  valve_ch = group_valve_ch_(group);
    const auto slaves   = group_slaves_(group);
    out_retry_count   = 0;
    out_rescue_count  = 0;

    // Clog_Ma firmware-write DISABLED (2026-05-19, per user): no longer
    // lower/restore the ZDT firmware 賭轉電流 around a cycle. Obstacle
    // detection now relies purely on the SOFTWARE phase-current judgment
    // (DISABLE_PHASE_CURRENT_LIMIT_MA path A in pusher_extend_with_disable_
    // seal_). Firmware Clog_Ma stays at whatever the operator set on the
    // drivers (3A default). Block kept under #if 0 for easy re-enable.
#if 0
    // RAII: lower Clog_Ma on entry, restore on ALL exit paths (return / abort /
    // exception). User invariant: every cycle_group_ entry MUST leave Clog at
    // NORMAL — never let PausedOnError persist with motor in GENTLE state.
    //
    // Implementation: a lambda captures `this` (privileged access to private
    // Z_() helper) + slaves and does the work. A std::function-based scope-
    // exit holder runs the restore lambda from its destructor — std::function
    // doesn't need private access of its own. We can't use a local class with
    // direct Z_() calls because C++ local classes don't inherit the enclosing
    // class's friend-like access (lambdas do).
    for (int sl : slaves) {
        if (Z_(sl).set_clog_ma(CLOG_MA_GENTLE, /*store=*/false)) {
            std::cout << "[clog_guard] slave " << sl
                      << " set GENTLE (" << CLOG_MA_GENTLE << "mA) FAIL — proceeding\n";
        }
    }
    std::cout << "[clog_guard] group enter — Clog_Ma -> " << CLOG_MA_GENTLE
              << "mA (GENTLE) on " << slaves.size() << " slave(s)\n";

    auto clog_restore_fn = [this, slaves]() {
        for (int sl : slaves) {
            if (Z_(sl).set_clog_ma(CLOG_MA_NORMAL, /*store=*/false)) {
                std::cout << "[clog_guard] slave " << sl
                          << " restore NORMAL (" << CLOG_MA_NORMAL << "mA) FAIL — manual check\n";
            }
        }
        std::cout << "[clog_guard] group exit — Clog_Ma -> " << CLOG_MA_NORMAL
                  << "mA (NORMAL) on " << slaves.size() << " slave(s)\n";
    };
    struct ScopeExit {
        std::function<void()> fn;
        ~ScopeExit() { if (fn) fn(); }
    } clog_guard{ clog_restore_fn };
#endif

    // 1. Pre-cycle (once): crane + DM2J large move
    {
        std::string perr = pre_cycle();
        if (!perr.empty()) return perr;
    }
    if (check_abort_()) return "aborted";

    for (int attempt = 0; attempt <= VACUUM_RETRY_MAX; ++attempt) {
        if (attempt > 0) {
            // [step 1/3] Feasibility check BEFORE doing any cleanup. If backup
            // can't proceed (rail would exceed [0, step_cm] safe range), abort
            // retries early so we don't release vacuum / retract pushers for
            // nothing — leave system in current attached state.
            std::string check_err = backup(true);
            if (!check_err.empty()) return check_err;

            // [step 2/3] Cleanup: release valve + retract pushers (rail can't move
            // backward while mechanism is locked to wall).
            // Wrapped in try_or_pause_ — on op fail, pause for user manual fix
            // then retry / skip / abort (vs. previous immediate Error state).
            if (try_or_pause_([this, valve_ch]() { return pqw_.controlRelay(valve_ch, false); },
                              "cycle_" + group + "_valve_off_retry")) return "aborted";
            // Poll-based wait — proceeds the moment all cups release, up to
            // VACUUM_RELEASE_WAIT_MS. On timeout drops into PausedOnError.
            if (try_or_pause_([this, &slaves]() { return vacuum_wait_release_(slaves, VACUUM_RELEASE_WAIT_MS); },
                              "cycle_" + group + "_vacuum_release_retry")) return "aborted";

            // Other-group stall sweep: 真空釋放後 cup 解離過程可能讓對側組 latch
            // stall flag — 預先清掉，避免接下來 retract 動作被 firmware 拒收。
            clear_other_group_stalls_(group);

            // Pipelined two-stage retract (all groups): slow-peel off the wall
            // then fast retract to 0. Avoids ZDT stall when cup adhesion lingers
            // after valve OFF.
            if (try_or_pause_([this, &slaves]() { return pusher_two_stage_retract_(slaves); },
                              "cycle_" + group + "_pusher_retract_retry")) return "aborted";

            // Re-anchor encoder zero at the physical retracted position.
            // Stall during extend causes gravity-induced position drift; calling
            // set_zero() here prevents accumulation across retries.
            for (int s : slaves) {
                if (Z_(s).set_zero())
                    std::cout << "[cycle_" << group << "] set_zero slave " << s << " fail (non-fatal)\n";
            }
            if (check_abort_()) return "aborted";

            // [step 3/3] Actual backup motion (crane pay_out + DM2J reverse move).
            std::string berr = backup(false);
            if (!berr.empty()) return berr;
            if (check_abort_()) return "aborted";
        }

        // Valve ON BEFORE extend (pre-engage vacuum — cup pulls air as pusher
        // contacts wall → instant seal). Aligned with Linux_test menu 7 and
        // memory project_vacuum_seal_patterns.md.
        // Body group uses lower RPM/ACC: heavier load → higher stall risk on upper two pushers.
        const int extend_rpm   = (group == "body") ? PUSHER_RPM_BODY_EXTEND : PUSHER_RPM;
        const int extend_acc   = (group == "body") ? PUSHER_ACC_BODY_EXTEND : PUSHER_ACC;
        // Per-slave extend pulses:
        //   feet : base = last_seal_pulse_ (learned seal position, persists)
        //   body : base = preset + feet_over delta  (2026-05-18 fix B1, TRIAL)
        // B1 fix: body target used to be last_seal_pulse_body + feet_over, which
        // double-counted feet_over (last_seal_pulse_body already absorbed prior
        // steps' feet_over via record_seal_pulse_) → body target snowballed.
        // Now body base = stable preset, feet_over applied once per step.
        // TRIAL — revert to `last_seal_pulse_[s-1]` base + old body if-block if
        // bench shows excess iter-loop work. See changelog 2026-05-18g.
        std::vector<int> extend_pulses(slaves.size(), 0);
        for (size_t i = 0; i < slaves.size(); ++i) {
            const int s = slaves[i];
            int target;
            if (group == "body") {
                const double over_cm = last_feet_max_over_cm_.load();
                target = preset_extend_pulse_for_slave_(s)
                       + ((over_cm > 0) ? cm_to_pulses_for_slave_(s, over_cm) : 0);
            } else if (feet_target_override) {
                // [2026-07-13] cross-obstacle: explicit target (e.g. 2×preset),
                // bypassing the snowball cap so the cup can reach the stood-off wall.
                target = feet_target_override(s);
            } else {
                // [2026-06-05] Snowball protection (fix C): cap feet target.
                target = feet_target_capped_(s);
            }
            extend_pulses[i] = target;
        }
        // valve_on with FC01 readback verify + retry (USR gateway sometimes drops
        // FC05 when bus busy from prior command).
        if (try_or_pause_([this, valve_ch]() { return pqw_set_relay_verified_(valve_ch, true); },
                          "cycle_" + group + "_valve_on")) return "aborted";

        // Disable-seal extend with obstacle rescue loop. disable_seal reports
        // any_obstacle when one or more cups hit an obstacle during push (pos_err
        // + phase_current both elevated). When detected, retreat rail by
        // OBSTACLE_RESCUE_BACKUP_CM (vs the 5cm vacuum_retry backup) and re-extend
        // — this is a "free" position change that doesn't count toward
        // VACUUM_RETRY_MAX. Up to OBSTACLE_RESCUE_MAX rescues per vacuum attempt;
        // exceeded → fall through to PausedOnError so operator can clear obstacle.
        bool any_obstacle = false;
        int  rescue_in_attempt = 0;
        bool extend_ok = false;
        while (true) {
            any_obstacle = false;
            if (try_or_pause_([this, &slaves, extend_pulses, extend_rpm, extend_acc, &any_obstacle]() {
                                  // [2026-07-08 per user] step feet: stop extending the
                                  // group as soon as >=1 cup seals (stop_on_first_seal).
                                  return pusher_extend_with_disable_seal_(slaves, extend_pulses, extend_rpm, extend_acc, &any_obstacle, /*stop_on_first_seal=*/true);
                              },
                              "cycle_" + group + "_pusher_extend")) return "aborted";

            if (!any_obstacle) { extend_ok = true; break; }

            // Obstacle hit — try rescue (rail backup 10cm + valve off + retract).
            if (rescue_in_attempt >= OBSTACLE_RESCUE_MAX) {
                std::cout << "[cycle_" << group << "] obstacle rescue exhausted ("
                          << rescue_in_attempt << "/" << OBSTACLE_RESCUE_MAX
                          << ") — PausedOnError for operator\n";
                evt_("obstacle_rescue_exhausted group=" + group +
                     " rescues=" + std::to_string(rescue_in_attempt));
                return "obstacle_rescue_exceeded " + group;
            }
            std::cout << "[cycle_" << group << "] obstacle detected — rescue "
                      << (rescue_in_attempt + 1) << "/" << OBSTACLE_RESCUE_MAX
                      << " (rail backup " << OBSTACLE_RESCUE_BACKUP_CM << "cm + re-extend)\n";
            evt_("obstacle_rescue group=" + group +
                 " rescue=" + std::to_string(rescue_in_attempt + 1) +
                 "/" + std::to_string(OBSTACLE_RESCUE_MAX));

            // [2026-06-03] If a parallel arm sweep is running (cmd_step_*_with_
            // sweep launched do_arm_clean_sweep_continuous_ in background), wait
            // for it to finish before starting rescue motion. Rescue uses
            // cli_22_ (PQW valve, JC100) + DM2J 1+3 (feet rail); sweep uses
            // cli_22_ (DM2J:14, PQW pump/brush) — concurrent operation observed
            // to latch stall flags on idle ZDT slaves + amplify FC01 stale-frame
            // races. Capped at RESCUE_WAIT_SWEEP_MAX_MS to avoid blocking
            // forever if sweep gets stuck.
            if (arm_sweep_active_.load()) {
                std::cout << "[cycle_" << group
                          << "] rescue: waiting for parallel arm sweep to finish "
                             "(up to " << (RESCUE_WAIT_SWEEP_MAX_MS / 1000) << "s)\n";
                int waited_ms = 0;
                while (arm_sweep_active_.load() && waited_ms < RESCUE_WAIT_SWEEP_MAX_MS) {
                    sleep_ms_(100);
                    waited_ms += 100;
                }
                if (arm_sweep_active_.load()) {
                    std::cout << "[cycle_" << group
                              << "] rescue: sweep still active after "
                              << (RESCUE_WAIT_SWEEP_MAX_MS / 1000)
                              << "s — proceeding anyway\n";
                    evt_("rescue_sweep_wait_timeout group=" + group);
                } else {
                    std::cout << "[cycle_" << group
                              << "] rescue: sweep finished after "
                              << waited_ms << "ms — proceeding\n";
                }
            }

            // Feasibility check first.
            std::string rcheck = rescue_backup(true);
            if (!rcheck.empty()) {
                std::cout << "[cycle_" << group << "] obstacle rescue blocked: " << rcheck << "\n";
                return rcheck;
            }
            // Cleanup before backup motion: valve off, wait release, retract pushers
            // (same sequence as vacuum retry — rail can't move with cups stuck).
            if (try_or_pause_([this, valve_ch]() { return pqw_.controlRelay(valve_ch, false); },
                              "cycle_" + group + "_rescue_valve_off")) return "aborted";
            if (try_or_pause_([this, &slaves]() { return vacuum_wait_release_(slaves, VACUUM_RELEASE_WAIT_MS); },
                              "cycle_" + group + "_rescue_vacuum_release")) return "aborted";
            // Extra settle after vacuum_wait_release_ reports "released": the
            // pressure sensor crossing DETACH_THRESHOLD_KPA doesn't guarantee
            // the cup has physically peeled off the wall — residual adhesion can
            // linger. Without this dwell the retract motor pulls against a still-
            // stuck cup → ZDT stall. (2026-05-18 per user.)
            sleep_ms_(RESCUE_VACUUM_SETTLE_MS);
            clear_other_group_stalls_(group);
            if (try_or_pause_([this, &slaves]() { return pusher_two_stage_retract_(slaves); },
                              "cycle_" + group + "_rescue_retract")) return "aborted";
            for (int s : slaves) {
                if (Z_(s).set_zero())
                    std::cout << "[cycle_" << group << "] rescue set_zero slave " << s << " fail (non-fatal)\n";
            }
            if (check_abort_()) return "aborted";
            // Actual rescue backup motion (10cm rail retreat).
            std::string rerr = rescue_backup(false);
            if (!rerr.empty()) return rerr;
            if (check_abort_()) return "aborted";

            // Re-open valve before next extend attempt (cycle_group_ entry pattern).
            if (try_or_pause_([this, valve_ch]() { return pqw_set_relay_verified_(valve_ch, true); },
                              "cycle_" + group + "_rescue_valve_on")) return "aborted";

            rescue_in_attempt++;
            out_rescue_count++;
            // loop continues — re-extend with same target_pulses
        }
        if (!extend_ok) return "obstacle_rescue_exceeded " + group;   // defensive

        // Final vacuum check — disable-seal already records last_seal_pulse_, but
        // may have weak_seal cups that need fine_tune as fallback safety net.
        auto fails = vacuum_check_(group);

        // Release any deferred stall flags from extend.
        for (int s : slaves) Z_(s).release_stall_flag();

        // [2026-07-08 per user] Proceed if AT LEAST ONE cup in this group sealed
        // (was: fails.empty() = all cups). slaves = this side's 2 cups; retry
        // only when EVERY cup failed. Surface the weak cup(s) when proceeding on
        // a partial seal so telemetry/operator knows this side is one-cup-only.
        if (fails.size() < slaves.size()) {
            if (!fails.empty()) {
                std::string wmsg = "vacuum_partial_ok " + group + " sealed="
                    + std::to_string(slaves.size() - fails.size()) + "/"
                    + std::to_string(slaves.size()) + " unsealed=";
                for (size_t i = 0; i < fails.size(); ++i) { if (i) wmsg += ","; wmsg += std::to_string(fails[i]); }
                std::cout << "[cycle_" << group << "] " << wmsg << " — proceed (>=1 sealed)\n";
                evt_(wmsg);
            }
            out_retry_count = attempt;
            return "";
        }
        std::string msg = "vacuum_fail_all " + group + " attempt=" + std::to_string(attempt) + " slaves=";
        for (size_t i = 0; i < fails.size(); ++i) {
            if (i) msg += ",";
            msg += std::to_string(fails[i]);
        }
        evt_(msg);
    }
    return "vacuum_retry_exceeded " + group;
}

#endif // WASH_ROBOT_H
