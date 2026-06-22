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
    std::string cmd_run(int steps, int cm = 0, const std::string& direction = "down");   // direction = "down" | "up"

    // [2026-06-05] Scripted run — CSV of per-step cm values, fixed down_sweep_af
    // direction. Mirrors cmd_run's is_down_sweep path exactly; each step calls
    // cmd_step_down_sweep_after_feet(cm) with the per-iter cm. CSV supports `*`
    // repeat shorthand (e.g. "30*5,20*3" = 5×30 + 3×20). See scripted_run_plan.md.
    std::string cmd_run_script(const std::string& csv);
    // Named-script management (persisted to ./scripts.json key=value format).
    std::string cmd_save_script(const std::string& name, const std::string& csv);
    std::string cmd_list_scripts();
    std::string cmd_load_script(const std::string& name);
    std::string cmd_delete_script(const std::string& name);
    std::string cmd_run_saved(const std::string& name);

    std::string cmd_arm_sweep();  // public: acquires motion_mtx_
    std::string cmd_tilt_mode(bool on);
    std::string cmd_emergency_stop();
    std::string cmd_shutdown();
    std::string cmd_status();
    std::string cmd_vacuum(const std::string& group, bool on);
    std::string cmd_pump(bool on);                       // dp0105 vacuum pump (PQW CH1)
    std::string cmd_brush(bool on);                      // arm roller brush motor (PQW CH5)
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

private:
    //=========== constants ===========

    static constexpr const char* IP_485_1   = "192.168.1.20";
    static constexpr const char* IP_485_2   = "192.168.1.21";
    static constexpr const char* IP_485_3   = "192.168.1.22";
    static constexpr int         PORT_485   = 4001;

    // CRANE_IP: 2026-05-08 set to 192.168.5.26 for current bench network.
    // History: was "192.168.1.101" (formal Crane_control_PI deploy IP), then
    // test-mode "192.168.5.26" / "127.0.0.1" (easy crane shim) earlier rounds —
    // see changelog 2026-04-21e / 2026-04-24ao / 2026-05-07. Tension query goes
    // via crane_cmd_("tension"). Restore to 192.168.1.101 for production deploy.
    static constexpr const char* CRANE_IP   = "192.168.1.10";
    static constexpr int         CRANE_PORT = 5002;

    // Cleaning arm — standalone damiao motor service on the same Pi.
    // TCP commands: INIT / DEPLOY <wall_mm> <LEFT|CENTER|RIGHT> / PARK / STATUS /
    // M1|M2 ENABLE|DISABLE|HOLD|UNHOLD|ZERO. See cleaning_arm/main_api.h.
    static constexpr const char* ARM_IP   = "127.0.0.1";
    static constexpr int         ARM_PORT = 9527;

    // PQW relay channels (slave 12, 8CH)
    static constexpr int PQW_SLAVE       = 12;
    static constexpr int PQW_TOTAL_CH    = 8;
    static constexpr int CH_PUMP         = 1;  // dp0105 vacuum pump (always ON while running)
    static constexpr int CH_VALVE_FEET   = 2;  // VT307 feet suction cups
    static constexpr int CH_VALVE_BODY   = 3;  // VT307 body suction cups
    static constexpr int CH_VALVE_CENTER = 4;  // VT307 center suction cup
    static constexpr int CH_BRUSH        = 5;  // arm roller brush motor
    static constexpr int CH_WATER_PUMP   = 6;  // water tank pump (spray)
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

    // ZDT pusher slave IDs
    // Slave IDs updated 2026-04-23 per actual wiring on robot
    //   feet:  left = 3,4 / right = 1,2
    //   body:  left = 6,8 / right = 5,7
    //   center = 9
    static constexpr int ZDT_LF1 = 3, ZDT_LF2 = 4;  // left foot
    static constexpr int ZDT_LB1 = 6, ZDT_LB2 = 8;  // left body
    static constexpr int ZDT_RF1 = 1, ZDT_RF2 = 2;  // right foot
    static constexpr int ZDT_RB1 = 5, ZDT_RB2 = 7;  // right body
    static constexpr int ZDT_C   = 9;                // center

    // DM2J rail/arm slave IDs
    // 2026-05-26: 上滑台從 cli_20_ slave 5 搬到 cli_22_ slave 14，目的是讓
    // arm sweep (cli_22_) 跟 feet rail (cli_20_) 真正並行不撞 bus。
    static constexpr int DM2J_LEFT_FOOT   = 1;    // cli_20_
    static constexpr int DM2J_LEFT_WHEEL  = 2;    // cli_20_
    static constexpr int DM2J_RIGHT_FOOT  = 3;    // cli_20_
    static constexpr int DM2J_RIGHT_WHEEL = 4;    // cli_20_
    static constexpr int DM2J_ARM         = 14;   // cli_22_ (was cli_20_ slave 5 pre-2026-05-26)

    // Pusher motion
    static constexpr int PUSHER_EXTEND_PULSE       = 30000;    // center / fallback ~10 cm (對齊 body，2026-04-24)
    static constexpr int PUSHER_EXTEND_FEET_PULSE       = 29000;  // feet upper (slave 1,3) ~9.7 cm (2026-05-28: 26000→29000 +3000=+1cm，bench iter 0 plateau no contact、iter 1+ 才 seal 浪費 ~3-5s/iter)
    static constexpr int PUSHER_EXTEND_FEET_PULSE_LOWER = 29900;  // feet lower (slave 2,4) ~10.0 cm (2026-05-28: 26900→29900 +3000=+1cm 同上)
    static constexpr int PUSHER_EXTEND_BODY_PULSE       = 34000;  // body upper (slave 5,6) ~11.3 cm (2026-05-28: 30000→36000 +6000=+2cm; 2026-05-28i: 36000→33000 -3000=-1cm，bench 顯示 36000+over 害 Phase 1 fast 700rpm 撞 wall peakI 1500mA+；2026-05-29: 33000→34000 +1000=+0.8cm，邊際提速 iter loop 收斂)
    static constexpr int PUSHER_EXTEND_BODY_PULSE_SHORT = 35400;  // body lower (slave 7,8) ~11.8 cm (2026-05-28: 29400→32400 +3000=+1cm；2026-05-28h: 32400→35400 +3000=+1cm，bench log body lower wall at 42798、SHORT 仍不夠導致 iter 0 plateau,加深一輪)
    static constexpr int PUSHER_RETRACT_PULSE      = 0;
    static constexpr int PUSHER_RPM           = 700;     // extend 用（feet / center）
    static constexpr int PUSHER_RPM_RETRACT      = 30;      // two-stage retract 第一段（慢速脫壁）(2026-05-22: 30 → 50 → 30 改回)
    static constexpr int PUSHER_RPM_RETRACT_FULL = 500;     // two-stage retract 第二段（收到 0）— 脫壁後空走可快
    static constexpr double RETRACT_SLOW_PEEL_CM = 2.0;     // two-stage retract 第一段慢速脫壁距離 (2026-05-22: 2.0 → 2.3 → 2.6; 2026-05-27: 2.6 → 2.3; 2026-05-29: 2.3 → 2.0 提速)
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
    static constexpr double PUSHER_STAGE1_SAFETY_FACTOR = 3.0;
    static constexpr int    PUSHER_STAGE1_DELAY_MS    =
        (int)((RETRACT_SLOW_PEEL_CM / PUSHER_RETRACT_CM_PER_SEC) *
              PUSHER_STAGE1_SAFETY_FACTOR * 1000.0);
        // = (2.0 / 1.54) × 3.0 × 1000 ≈ 3896 ms

    // Step parameters
    static constexpr int STEP_CM_DEFAULT  = 30;   // initial value of step_cm_ (settable via cmd_set_step_cm)
    static constexpr int STEP_CM_MIN      = 5;
    static constexpr int STEP_CM_MAX      = 50;
    static constexpr int STEP_MARGIN_CM   = 10;   // crane extra slack before feet move (2026-05-27: 15→10 提速)
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
    static constexpr double      IMU_ASK_DEG        = 15.0;
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
    static constexpr int ARM_SWEEP_CM  = 55;   // sweep cm (2026-05-21: 30→40→45→50→55; 2026-05-25: 55→60→100→80; 2026-05-26: 80→100; 2026-05-28: 100→80; 2026-06-06: 80→90→100→90→85; 2026-06-11: 85→60→55 per user 縮短行程)
    static constexpr int ARM_SWEEP_RPM = 1000;   // top speed (2026-05-26 bench menu28: 2300 + ACC/DEC 200/200 穩定; 2026-05-27: 2300→2000→1000 per user 因仍觀察失步)
    static constexpr int ARM_SWEEP_ACC = 100;    // start ramp (ms/1000rpm) — 2026-05-26: 100→200; 2026-05-27: 200→100 配合 RPM 1000
    static constexpr int ARM_SWEEP_DEC = 100;    // stop ramp (ms/1000rpm) — 2026-05-26: 100→200; 2026-05-27: 200→100 配合 RPM 1000

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
    static constexpr int ARM_CLEAN_WALL_MM = 330;  // DEPLOY wall distance (fixed); 2026-06-02: 350→330 試「上貼下不貼」是不是 M1 過度外擺造成；2026-05-27: 300→350 拉大讓 M1 往前推更多（靠刮刀座彈性吸收過壓）
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
    static constexpr int VACUUM_RELEASE_WAIT_MS = 1500;  // wait after valve OFF before pusher retract (cup adhesion + line vent) (2026-05-22: 4000 → 3000; 2026-06-01: 3000 → 1500，bench 觀察 valve OFF 瞬間 vent 完成，剩下殘留要靠 slow peel 拉開，3000 大部分是浪費)
    static constexpr int POLL_INTERVAL_MS     = 50;
    static constexpr double VACUUM_BACKUP_CM  = 10.0;  // rail backup on each vacuum retry (2026-05-29: 5→10，weak_seal 後找新位置 5cm 不夠遠，常吸到同一個漏氣點)
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
    static constexpr int    DISABLE_RETRY_MAX_OVEREXTEND = 15000; // 上限 +5.0 cm (2026-05-19: 7500→15000) — 配 1.0cm 步進(INCR 3000) → iter 0~4 = +1.0~+5.0cm 五輪都能 push，cap 不再提早截斷；weak_seal 改由 MAX_ITERS 收尾判定
    static constexpr int    DISABLE_RETRY_MAX_ITERS      = 5;    // iter 上限 → iter 0~4 共 5 次 push（cap 拉到 15000 後此值才是真正的 binding limit）

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
    //   feet 同理 (60000 - 29000 - 12000) / 2857 ≈ 6.65 cm，取 5.0 保守
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
    //   - 單顆 cup 漂超過 REALIGN_THRESHOLD_CM (1.5cm) → 觸發（safety net for outlier）
    //   - 全部 cup 平均漂超過 REALIGN_THRESHOLD_MEAN_CM (1.0cm) → 觸發（累積式判斷）
    // 2026-06-05: Phase 1 加速 — 提前 trigger 讓每次 realign 工作量小、body cup
    // 不再撞 endpoint → disable_seal iter 大減（連鎖效益）。前提是 2026-06-01h
    // fix 讓 Stage 0 stall non-fatal、realign 整體更穩。
    static constexpr double REALIGN_THRESHOLD_CM            = 1.5;   // single-cup max trigger (2026-06-05: 3.0 → 1.5 Phase 1 speedup) (2026-05-22: 1.5 → 3.0)
    static constexpr double REALIGN_THRESHOLD_MEAN_CM       = 1.0;   // mean of |drift| across cups → trigger (2026-06-05: 2.0 → 1.0 Phase 1 speedup) (2026-05-22: 1.0 → 1.5; 2026-05-28: 1.5 → 2.0)
    // Realign crane assist target = the per-sensor weight limit (rope_weight_
    // limit_per_sensor_kg_, 2026-05-19 per user — was a fixed 2kg). Not a
    // constant here because the limit is state-dependent.
    static constexpr int    REALIGN_CRANE_ASSIST_MAX_CM     = 10;    // safety upper bound on crane retract during realign
    // Two-stage retract pattern (matches cycle_group_ body retract):
    //   Stage A: retract delta/3 at SLOW rpm — break cup adhesion to wall
    //   Stage B: retract remaining 2*delta/3 at FULL rpm — finish quickly once unstuck
    static constexpr int    REALIGN_RETRACT_RPM             = 50;    // Stage A: slow retract while sealed (break adhesion)
    static constexpr int    REALIGN_RETRACT_ACC             = 200;
    static constexpr int    REALIGN_RETRACT_RPM_FULL        = 60;    // Stage B: 1.2× of Stage A — minimize speed jump (avoid 80 RPM ramp-up torque spike that stalled slave 5 / 2026-05-06)
    static constexpr int    REALIGN_RETRACT_ACC_FULL        = 50;    // Gentle ramp-up — lowers peak torque demand at Stage 2 start
    static constexpr int    REALIGN_EXTEND_RPM              = 20;    // very slow extend for short cups (push cup into wall, machine load builds gradually)
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

    TCP_client cli_20_, cli_21_, cli_22_;
    TCP_client crane_cli_;
    // Cleaning arm — separate TCP connection to local motor_api service (127.0.0.1:9527)
    TCP_client arm_cli_;
    std::mutex arm_mtx_;
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
    // Default STEP_CM_DEFAULT (30); valid range STEP_CM_MIN..STEP_CM_MAX (5..50).
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
    static constexpr int  ARM_ROPE_PROTECT_WALL_MM  = 250;   // 2026-05-22: 300 → 250 per user
    enum class ArmStowState { Unknown, Center, Parked };
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
    static constexpr float ARM_M2_TOOL_LEFT_MM     = 148.09f;
    static constexpr float ARM_M2_TOOL_RIGHT_MM    = 134.07f;
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
    std::string do_step_down_(bool skip_cleaning_sweep = false,
                              std::function<void()> after_feet_rail_hook = {},
                              std::function<void()> before_feet_rail_hook = {},
                              std::function<void()> during_body_rail_hook = {},
                              std::function<void()> after_body_rail_hook = {});
    // mirror of do_step_down_; skip_cleaning_sweep=true 給 cmd_step_up_with_sweep 用（sweep 由背景 thread 接手）。
    // after_feet_rail_hook：非空時，在 feet phase 的 rail DM2J move 完成那刻呼叫一次
    // （給 cmd_step_up_sweep_after_feet 用來 launch 背景 sweep）。
    // before_feet_rail_hook：對稱於 do_step_down_，在 feet rail PR_trigger 前呼叫
    // （給 cmd_step_up_sweep_before_after 用來 join pre-feet sweep round）。
    std::string do_step_up_(bool skip_cleaning_sweep = false,
                            std::function<void()> after_feet_rail_hook = {},
                            std::function<void()> before_feet_rail_hook = {});

    // Realign sequence (E): when last_seal_pulse_ exceeds preset by REALIGN_THRESHOLD_CM
    // (or force=true), synchronously retract all 9 cups back to preset extension
    // while keeping valves ON (cups stay sealed → vacuum pulls machine toward wall).
    // Crane retract incrementally until rope tension reaches the per-sensor
    // weight limit (rope_weight_limit_per_sensor_kg_), capped at
    // REALIGN_CRANE_ASSIST_MAX_CM, to share weight via rope.
    // Returns "" on success / not-needed; "ERR ..." on failure (caller decides).
    std::string do_feet_realign_(bool force = false, bool in_window = false);

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
    void arm_monitor_during_sweep_();
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
    // 2. Fallback: easy crane weight via shim (read_easy_weight_kg_) — kept as
    //    redundancy if DSZL-107 read fails (Q4=(a) decision 2026-05-07)
    // Returns kg; -1 if all sources fail.
    double      read_rope_weight_max_kg_();

    // Query easy crane weight via shim (test mode primary source until real
    // crane arrives). Sends 'status' on estop channel (bypasses crane_mtx_ so
    // it's safe to call during in-flight retract). Parses 'weight=<kg>' field.
    // Returns kg; -1 on comm fail / parse fail / detached.
    double      read_easy_weight_kg_();
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
    // Does NOT return error — sweep cleanup runs regardless.
    void arm_sweep_fire_nowait_(double target_cm);

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
    bool             pusher_extend_with_disable_seal_(const std::vector<int>& slaves,
                                                       const std::vector<int>& target_pulses,
                                                       int fast_rpm = PUSHER_RPM,
                                                       int acc = PUSHER_ACC,
                                                       bool* any_obstacle_out = nullptr);

    // Smart extend on a subset of slaves in a given group. Mirrors cycle_group_'s
    // extend section: per-slave start_pulses (from last_seal_pulse_ + body delta),
    // vacuum-aware early stop, fine_tune补伸 with obstacle detection, record seal
    // pulse on success.
    // Caller must hold motion_mtx_ and pre-set valve to desired state.
    //   group  : "feet" / "body" / "center"
    //   slaves : subset of group_slaves_(group); for full group pass group_slaves_(group)
    // Returns true on hard fail (extend send / fine_tune size mismatch), false otherwise.
    bool             smart_extend_subset_(const std::string& group, const std::vector<int>& slaves);

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
                             int&         out_rescue_count);

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
    // Persistence: ./scripts.json — same flat key=value format as settings.json
    // (key = script name, value = original CSV string). Loaded once at startup.
    static constexpr int SCRIPT_TOTAL_STEP_MAX = 1000;   // soft cap on expanded step count
    static constexpr int SCRIPT_REPEAT_MAX     = 1000;   // soft cap on a single `*N` multiplier
    static constexpr int SCRIPT_NAME_MAX_LEN   = 32;     // [A-Za-z0-9_-]{1,32}

    // One step of a scripted run: cm + per-step sweep flag.
    // sweep=true  → cmd_step_down_sweep_after_feet(cm)
    // sweep=false → do_step_down_(skip_cleaning_sweep=true) — pure down, no
    //               arm sweep at all (transit only).
    struct ScriptStep { int cm; bool sweep; };

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
                                    int&         out_rescue_count) {
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
                                  return pusher_extend_with_disable_seal_(slaves, extend_pulses, extend_rpm, extend_acc, &any_obstacle);
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

        if (fails.empty()) {
            out_retry_count = attempt;
            return "";
        }
        std::string msg = "vacuum_fail " + group + " attempt=" + std::to_string(attempt) + " slaves=";
        for (size_t i = 0; i < fails.size(); ++i) {
            if (i) msg += ",";
            msg += std::to_string(fails[i]);
        }
        evt_(msg);
    }
    return "vacuum_retry_exceeded " + group;
}

#endif // WASH_ROBOT_H
