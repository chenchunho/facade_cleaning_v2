#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "WASH_ROBOT.h"

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

// ============================================================
//  Out-of-class definitions for static constexpr members
//  (required under C++14 when ODR-used, e.g. passed by const ref
//  to std::chrono::seconds)
// ============================================================
constexpr int WashRobot::IMU_BASELINE_SEC;

// ============================================================
//  Constructor / Destructor
// ============================================================

WashRobot::WashRobot()
    : abort_flag(false)
    , pause_flag(false)
    , motion_active_(false)
    , crane_wd_running_(false)
    , crane_last_ok_ms_(0)
    , crane_keepalive_running_(false)
    , imu_roll0_(0.0)
    , imu_pitch0_(0.0)
    , imu_ask_pending_(false)
    , imu_mon_running_(false)
    , state_(State::Idle)
    , state_before_pause_(State::Idle)
    , state_before_wait_(State::Idle)
    , rail_pos_cm_(0.0)
    , body_residual_cm_(0.0)
    , actual_feet_cm_(0.0)
    , step_cm_(STEP_CM_DEFAULT)
    , pause_action_((int)PauseAction::None)
    , crane_attached_(true)
    , arm_attached_(true)
    , arm_calibrated_(false)
    , arm_sweep_obstacle_pending_(false)
    , arm_sweep_skip_rest_of_run_(false)
    , wheels_attached_(true)
    , crane_alarm_pending_(false)
    , pressure_poll_running_(false)
    , obstacle_detect_enabled_(false)
{
    for (int i = 0; i < 9; ++i) cached_pressure_[i].store(0);
    // [2026-05-29] Init runtime settings from constexpr defaults. main.cpp
    // calls load_settings_file_("settings.json") right after construction to
    // override these with persisted values if a file exists.
    settings_.arm_clean_wall_mm              .store(ARM_CLEAN_WALL_MM);
    settings_.pusher_extend_feet_pulse       .store(PUSHER_EXTEND_FEET_PULSE);
    settings_.pusher_extend_feet_pulse_lower .store(PUSHER_EXTEND_FEET_PULSE_LOWER);
    settings_.pusher_extend_body_pulse       .store(PUSHER_EXTEND_BODY_PULSE);
    settings_.pusher_extend_body_pulse_short .store(PUSHER_EXTEND_BODY_PULSE_SHORT);
    settings_.vacuum_seal_deep_kpa           .store(VACUUM_SEAL_DEEP_KPA);
    settings_.realign_threshold_cm           .store(REALIGN_THRESHOLD_CM);
    settings_.realign_threshold_mean_cm      .store(REALIGN_THRESHOLD_MEAN_CM);
    settings_.rope_weight_limit_attached     .store(ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_ATTACHED);
    settings_.rope_weight_limit_hanging      .store(ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_HANGING);
    settings_.step_cm_default                .store(STEP_CM_DEFAULT);
    settings_.step_cm_max                    .store(STEP_CM_MAX);
    settings_.vacuum_plateau_ms              .store(VACUUM_PLATEAU_MS);
    settings_.vacuum_backup_cm               .store(VACUUM_BACKUP_CM);
    settings_.retract_slow_peel_cm           .store(RETRACT_SLOW_PEEL_CM);
    settings_.disable_retry_max_iters        .store(DISABLE_RETRY_MAX_ITERS);
    settings_.step_margin_cm                 .store(STEP_MARGIN_CM);
    settings_.imu_ask_deg                    .store(IMU_ASK_DEG);
    settings_.arm_deploy_pos_tol_rad         .store(ARM_DEPLOY_POS_TOL_RAD);
    settings_.static_roll_offset_cm          .store(0.0);   // not calibrated yet

    // [2026-06-05] Load saved scripts from ./scripts.json (no-op if file absent).
    // Same lifecycle as settings.json — read once at construction, persisted on
    // each cmd_save_script / cmd_delete_script. Disk failure here is silent
    // (saved_scripts_ just stays empty).
    load_saved_scripts_from_disk_();
}

WashRobot::~WashRobot() {
    stop();
}

//=========== init ===========

bool WashRobot::init() {
    // TCP connections
    if (!cli_20_.connectToServer(IP_485_1, PORT_485)) {
        std::cerr << "[WashRobot] connect " << IP_485_1 << " fail\n"; return true;
    }
    if (!cli_21_.connectToServer(IP_485_2, PORT_485)) {
        std::cerr << "[WashRobot] connect " << IP_485_2 << " fail\n"; return true;
    }
    if (!cli_22_.connectToServer(IP_485_3, PORT_485)) {
        std::cerr << "[WashRobot] connect " << IP_485_3 << " fail\n"; return true;
    }
    std::cout << "[OK] USR .20 / .21 / .22 connected\n";

    // [TEST MODE 2026-04-21] driver debug=true by default for on-site troubleshooting.
    // Revert to `false` default when main crane is online. See .claude/easy_crane_test_mode.md §9.
    //
    // Override via env var WR_DRIVER_DEBUG=0 (e.g. when remote-debugging via VS,
    // whose stdout pipe saturates under the hex-dump flood from 25 devices × I/O).
    bool dbg = true;
    if (const char* env = std::getenv("WR_DRIVER_DEBUG")) {
        if (env[0] == '0') dbg = false;
    }
    driver_dbg_ = dbg;   // remember for temp toggling in poll loops
    std::cout << "[OK] driver debug = " << (dbg ? "ON" : "OFF")
              << " (override via WR_DRIVER_DEBUG=0|1)\n";

    // DM2J slave 1..4 on cli_20_ (feet + wheels)
    for (int i = 1; i <= 4; ++i) {
        if (D_(i).init(cli_20_, i, dbg)) {
            std::cerr << "[FATAL] DM2J slave " << i << " init fail\n"; return true;
        }
    }
    std::cout << "[OK] DM2J 1~4 (cli_20_)\n";

    // DM2J arm rail (slave 14) on cli_22_ — shares bus with JC100/PQW/XKC/DY500.
    // 2026-05-26 wiring change: moved off cli_20_ so arm sweep can run truly in
    // parallel with feet rail motion (different physical RS485 bus).
    if (D_(DM2J_ARM).init(cli_22_, DM2J_ARM, dbg)) {
        std::cerr << "[FATAL] DM2J arm rail (slave " << DM2J_ARM << " @ cli_22_) init fail\n";
        return true;
    }
    std::cout << "[OK] DM2J arm rail (slave " << DM2J_ARM << " @ cli_22_)\n";

    // [2026-06-12] Re-enabled — 之前註解掉造成「同步 feet 變成輪子動」bug。
    // Bystanders safe PR1 — do_step_down_ uses PR_trigger_sync(1) broadcast so
    // slaves 1,3 start in the same Modbus frame (required: rigid-coupled feet
    // rails, ms-skew damages mechanism). The broadcast also hits wheels (2,4)
    // and arm (5); setting their PR1 to rpm=0 here makes that broadcast a no-op
    // for them. PR1 on bystanders is only touched here (one-shot) — wheel retract
    // in cmd_init and arm_sweep both use PR0, so PR1 stays safe for the session.
    // Mirrors Linux_test menu 7 dm2j_set_safe_pr pattern.
    D_(DM2J_LEFT_WHEEL ).PR_move_set(1, 1 /*absolute*/, 0 /*rpm=0*/, 0, DM2J_ACC, DM2J_DEC);
    D_(DM2J_RIGHT_WHEEL).PR_move_set(1, 1,              0,           0, DM2J_ACC, DM2J_DEC);
    D_(DM2J_ARM        ).PR_move_set(1, 1,              0,           0, DM2J_ACC, DM2J_DEC);
    std::cout << "[OK] DM2J bystanders (2,4,5) PR1 = rpm=0 safe for broadcast\n";

    // ZDT slave 1..9
    for (int i = 1; i <= 9; ++i) {
        if (Z_(i).init(cli_21_, i, dbg)) {
            std::cerr << "[FATAL] ZDT slave " << i << " init fail\n"; return true;
        }
    }
    std::cout << "[OK] ZDT 1~9\n";

    // 2026-05-18: center pusher (ZDT slave 9) intentionally disabled — not
    // controlled in current bench config. Adding to disabled_zdt_slaves_ makes
    // group_slaves_() filter it out of every group op (extend / retract /
    // stall-clear / vacuum-wait). Most center control paths were already
    // commented out (2026-05-04) but this guarantees no group op touches it.
    // Re-enable: remove this line OR runtime `zdt_enable 9`.
    // static_cast → prvalue copy: avoids ODR-use of the static constexpr
    // member (set::insert binds a const int& — would need out-of-class
    // definition in C++14 otherwise → "undefined reference to ZDT_C").
    disabled_zdt_slaves_.insert(static_cast<int>(ZDT_C));
    std::cout << "[OK] ZDT 9 (center) disabled — excluded from all group ops\n";

    // JC-100 slave 1..9
    for (int i = 1; i <= 9; ++i) {
        if (M_(i).init(cli_22_, i, dbg)) {
            std::cerr << "[FATAL] JC-100 slave " << i << " init fail\n"; return true;
        }
    }
    std::cout << "[OK] JC-100 1~9\n";

    // PQW 8CH relay
    if (pqw_.init(cli_22_, PQW_SLAVE, PQW_TOTAL_CH, dbg)) {
        std::cerr << "[FATAL] PQW slave " << PQW_SLAVE << " init fail\n"; return true;
    }
    std::cout << "[OK] PQW slave " << PQW_SLAVE << "\n";

    // XKC-Y25 water level sensor (slave 13, same bus as PQW/JC100/DY500)
    // Mode B init does no probe — first read in cmd_arm_clean_sweep will catch
    // physical absence (and PausedOnError per design).
    lvl_.init(cli_22_, XKC_SLAVE, dbg);
    std::cout << "[OK] XKC water level slave " << XKC_SLAVE << " (sensor presence not probed)\n";

    // DY-500 weight sensors (slaves 10, 11): NOT physically installed on this
    // robot (2026-05-19, per user). Init the driver objects but hard-disable
    // polling — weight_present_=false so the background loop never reads them
    // (no log spam). Rope weight comes from the crane DSZL-107 tension via TCP
    // (read_rope_weight_max_kg_ tier 1); the DY-500 tier is an unused fallback.
    // If they get physically installed later, restore a one-shot probe here to
    // set weight_present_ per sensor.
    weight_[0].init(cli_22_, DY_SLAVE_LEFT,  dbg);
    weight_[1].init(cli_22_, DY_SLAVE_RIGHT, dbg);
    weight_present_[0].store(false);
    weight_present_[1].store(false);
    std::cout << "[--] DY-500 slaves 10/11 not installed — polling disabled\n";

    // Init last_seal_pulse_ to per-slave preset; will be updated by fine_tune on success.
    for (int s = 1; s <= 9; ++s)
        last_seal_pulse_[s - 1].store(preset_extend_pulse_for_slave_(s));
    last_feet_max_over_cm_.store(0.0);
    cached_weight_kg_[0].store(-1.0);
    cached_weight_kg_[1].store(-1.0);
    weight_comm_ok_[0].store(false);
    weight_comm_ok_[1].store(false);

    // Crane (lazy — don't fail boot if crane is down)
    if (crane_connect_if_needed_())
        std::cerr << "[WARN] crane " << CRANE_IP << ":" << CRANE_PORT << " not yet reachable\n";
    else
        std::cout << "[OK] crane " << CRANE_IP << ":" << CRANE_PORT << "\n";

    // [2026-06-03] Arm (lazy — same pattern as crane). Required to bootstrap
    // TCP_client.reconnectLoop() background thread — startMonitor() only fires
    // after the first connectToServer() call. Before this explicit init, arm_cmd_
    // would never have a live connection (we removed manual connectToServer()
    // from arm_cmd_ — it now relies entirely on the background thread).
    if (!arm_cli_.connectToServer(ARM_IP, ARM_PORT))
        std::cerr << "[WARN] arm " << ARM_IP << ":" << ARM_PORT << " not yet reachable\n";
    else
        std::cout << "[OK] arm " << ARM_IP << ":" << ARM_PORT << "\n";

    // IMU (Serial_port::init returns true = success, unlike project convention)
    if (!imu_serial_.init(IMU_PORT, IMU_BAUD)) {
        std::cerr << "[FATAL] IMU serial " << IMU_PORT << " open fail\n"; return true;
    }
    imu_.init(&imu_serial_, dbg);   // [TEST MODE] default dbg=true; WR_DRIVER_DEBUG=0 disables
    sleep_ms_(500);
    if (imu_.read_error.load())
        std::cerr << "[WARN] IMU read error on startup\n";
    else
        std::cout << "[OK] IMU " << IMU_PORT
                  << " roll=" << imu_.x << " pitch=" << imu_.y << "\n";

    // Start background threads
    imu_mon_running_ = true;
    imu_mon_thread_  = std::thread(&WashRobot::imu_monitor_loop_, this);
    std::cout << "[OK] IMU monitor started\n";

    crane_wd_running_ = true;
    crane_wd_thread_  = std::thread(&WashRobot::crane_watchdog_loop_, this);
    std::cout << "[OK] crane watchdog started\n";

    // [2026-06-09] Water-inlet leak watchdog. Polls water_inlet_open_ts_ms_
    // every 10s; if open >WATER_INLET_OPEN_MAX_MS, force-close. Catches dead
    // detached refill threads, GUI forget-OFF, sweep flow exceptions.
    water_inlet_watchdog_running_.store(true);
    water_inlet_watchdog_thread_ = std::thread(&WashRobot::water_inlet_watchdog_loop_, this);
    std::cout << "[OK] water-inlet watchdog started (max open "
              << (WATER_INLET_OPEN_MAX_MS / 1000) << "s)\n";

    // [DISABLED 2026-05-15] crane_keepalive_loop_ thread no longer started.
    // Reason: 14t added it to prevent watchdog false-aborts during long
    // washrobot-side ops, but 14v further analysis showed the underlying bug
    // is zombie TCP socket on crane_cli_ (isConnected=true but dead).
    // New design: no continuous ping. Each crane_cmd_ self-heals on fail.
    // See crane_watchdog_loop_ header comment for rationale.
    // crane_keepalive_running_ = true;
    // crane_keepalive_thread_  = std::thread(&WashRobot::crane_keepalive_loop_, this);
    // std::cout << "[OK] crane keepalive started\n";

    // [2026-05-29] Background pressure_poll_loop_ REMOVED — purely for GUI cache.
    // Now: motion paths piggyback updates via read_pressure_(), and cmd_status
    // does a one-shot fresh read of all 9 JC100 when called during idle. This
    // eliminates background cli_22_ bus traffic that contended with PARK / PQW
    // verify retries and caused the JC100 timeout flood observed 2026-05-29.
    // DY-500 cache (Tier-2 fallback in read_rope_weight_max_kg_) becomes dead
    // code but harmless — sensors aren't installed anyway.
    std::cout << "[OK] pressure poll DISABLED (cmd_status fresh-reads on demand)\n";

    // Safe startup: ensure all relays off
    //pqw_.controlRelay(CH_BRUSH,        false);
    //pqw_.controlRelay(CH_WATER_PUMP,   false);
    //pqw_.controlRelay(CH_WATER_INLET,  false);
    //pqw_.controlRelay(CH_PUMP,         false);
    //pqw_.controlRelay(CH_VALVE_FEET,   false);
    //pqw_.controlRelay(CH_VALVE_BODY,   false);
    //pqw_.controlRelay(CH_VALVE_CENTER, false);

    // [REMOVED 2026-04-24] Startup wheel-lower step removed per user request.
    // Previously slaves 2, 4 were moved to absolute -7 cm here. If you need wheels
    // lowered at boot, use the `wheels lower` TCP command after init, or restore
    // this block.

    return false;
}

void WashRobot::stop() {
    abort_flag    = true;
    motion_active_ = false;
    imu_mon_running_ = false;
    if (imu_mon_thread_.joinable()) imu_mon_thread_.join();
    imu_.stop();
    crane_wd_running_ = false;
    if (crane_wd_thread_.joinable()) crane_wd_thread_.join();
    // crane_keepalive thread disabled in init() — see comment there.
    // crane_keepalive_running_ = false;
    // if (crane_keepalive_thread_.joinable()) crane_keepalive_thread_.join();
    // [2026-05-29] pressure_poll_thread_ no longer started — nothing to join.
    // [2026-06-09] Stop water-inlet watchdog. Last-chance force close (one
    // attempt only — process is shutting down, no point retrying long).
    water_inlet_watchdog_running_.store(false);
    if (water_inlet_watchdog_thread_.joinable()) water_inlet_watchdog_thread_.join();
    if (water_inlet_open_ts_ms_.load() != 0) {
        std::cerr << "[water_inlet] stop(): valve still armed open — sending final close\n";
        set_water_inlet_(false);
    }
}

//=========== utility ===========

int64_t WashRobot::now_ms_() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void WashRobot::sleep_ms_(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void WashRobot::evt_(const std::string& msg) {
    if (!evt_cb) return;
    std::string s = "EVT " + msg;
    if (s.empty() || s.back() != '\n') s.push_back('\n');
    evt_cb(s);
}

bool WashRobot::dm2j_wait_done_(int slave, int timeout_ms) {
    for (int e = 0; e < timeout_ms; e += 100) {
        uint32_t st = 0;
        if (D_(slave).read_status(st)) return true;           // comms error
        if (st & 0x0001)              return true;           // fault
        if ((st & 0x0010) && (st & 0x0020)) return false;   // cmd_done + path_done
        sleep_ms_(100);
    }
    return true; // timeout
}

// Parallel poll — each iteration polls both slaves, exits when both done or
// either faults / times out. Unlike sequential wait (wait-slave-A, then wait-slave-B)
// this gives visibility to both slaves' progress during same motion.
// On fail, prints which slave + reason + error code for post-mortem diagnosis.
//
// read_status retry: RS485-over-TCP gateway occasionally drops a single Modbus
// frame mid-motion (buffer interleave under traffic). One missed read should
// not abort the whole pair motion — retry up to 3 times w/ 50 ms gap before
// giving up. Behaviour unchanged when no comms error occurs.
bool WashRobot::dm2j_pair_poll_done_(int slave_a, int slave_b, int timeout_ms) {
    auto read_status_retry = [this](int slave, uint32_t& s) -> bool {
        for (int r = 0; r < 3; ++r) {
            if (!D_(slave).read_status(s)) return false;   // ok
            if (r < 2) sleep_ms_(50);
        }
        return true;   // really failed after 3 tries
    };

    bool a_done = false, b_done = false;
    for (int e = 0; e < timeout_ms; e += 100) {
        uint32_t sa = 0, sb = 0;
        if (!a_done) {
            if (read_status_retry(slave_a, sa)) {
                std::cout << "  [pair DM2J fail] slave " << slave_a << " comms error (3 retries) at " << e << "ms\n";
                return true;
            }
            if (sa & 0x0001) {
                uint16_t ec = 0;
                D_(slave_a).read_error_code(ec);
                std::cout << "  [pair DM2J fail] slave " << slave_a << " FAULT at " << e
                          << "ms, error_code=0x" << std::hex << ec << std::dec << "\n";
                return true;
            }
            if ((sa & 0x0010) && (sa & 0x0020)) a_done = true;
        }
        if (!b_done) {
            if (read_status_retry(slave_b, sb)) {
                std::cout << "  [pair DM2J fail] slave " << slave_b << " comms error (3 retries) at " << e << "ms\n";
                return true;
            }
            if (sb & 0x0001) {
                uint16_t ec = 0;
                D_(slave_b).read_error_code(ec);
                std::cout << "  [pair DM2J fail] slave " << slave_b << " FAULT at " << e
                          << "ms, error_code=0x" << std::hex << ec << std::dec << "\n";
                return true;
            }
            if ((sb & 0x0010) && (sb & 0x0020)) b_done = true;
        }
        if (a_done && b_done) return false;
        sleep_ms_(100);
    }
    std::cout << "  [pair DM2J fail] TIMEOUT after " << timeout_ms
              << "ms (a_done=" << a_done << " b_done=" << b_done << ")\n";
    return true;   // timeout (one or both still running)
}

// Robust DM2J position read: retries until 2 consecutive reads agree within
// `agree_cm` tolerance. Catches occasional Modbus frame corruption — bench
// 2026-05-15 saw read return 610.x when actual position was 5cm (likely
// stale-buffer or cross-slave contamination on USR-TCP232 gateway shared bus).
//
// Returns true on error (couldn't get consistent reads in max_attempts), false
// on success with out_cm = the agreed value.
bool WashRobot::dm2j_read_pos_robust_(int slave, double& out_cm,
                                       int max_attempts, double agree_cm) {
    // dm2j_motion_mtx_：跟背景 arm sweep 序列化 cli_20_。沒 lock 的話 sweep 的
    // PR_move_cm poll 占用 TCP socket，這裡的 read_position_cm 全 5 次 timeout。
    std::lock_guard<std::mutex> dm2j_lk(dm2j_motion_mtx_);
    double prev = 0;
    bool have_prev = false;
    for (int i = 0; i < max_attempts; ++i) {
        double v = 0;
        if (D_(slave).read_position_cm(v)) {
            std::cout << "  [dm2j_robust] slave " << slave << " attempt " << (i + 1)
                      << "/" << max_attempts << " comm fail\n";
            have_prev = false;
            continue;
        }
        if (have_prev && std::fabs(v - prev) <= agree_cm) {
            out_cm = v;
            return false;   // success
        }
        if (have_prev) {
            std::cout << "  [dm2j_robust] slave " << slave << " attempt " << (i + 1)
                      << " prev=" << prev << " new=" << v
                      << " (diff " << std::fabs(v - prev) << "cm > " << agree_cm
                      << " tol) — retry\n";
        }
        prev = v;
        have_prev = true;
    }
    std::cout << "  [dm2j_robust] slave " << slave
              << " FAILED to get consistent reads in " << max_attempts << " attempts\n";
    return true;
}

// Synchronized pair move to same absolute target (cm).
// Broadcast trigger ensures same-moment start. Parallel poll ensures both
// finish before we return. Logs before/after positions + travel for diagnostic.
bool WashRobot::dm2j_pair_move_abs_(int slave_a, int slave_b, int pr_num,
                                      double target_cm, int timeout_ms) {
    // 2026-05-22 序列化：cli_20_ 上有 slave 1,2,3,4,5，跟背景 arm sweep
    // (slave 5) 共用 TCP socket。沒這 lock → bus contention → PausedOnError。
    std::lock_guard<std::mutex> dm2j_lk(dm2j_motion_mtx_);

    // Read current positions (diagnostic baseline)
    double pa_before = 0, pb_before = 0;
    if (D_(slave_a).read_position_cm(pa_before)) return true;
    if (D_(slave_b).read_position_cm(pb_before)) return true;
    std::cout << "  [pair DM2J " << slave_a << "+" << slave_b
              << "] before: " << slave_a << "=" << pa_before
              << " " << slave_b << "=" << pb_before
              << " cm → target " << target_cm << " cm\n";

    // Skip-if-at-target optimization: when both slaves are already within
    // EPSILON_CM of target, no motion is needed. Avoids ~2 s overhead from
    // PR write + broadcast + poll + read-back when called as a no-op
    // (e.g. feet phase target=0 with rail already at 0 from previous cycle).
    constexpr double EPSILON_CM = 0.05;   // 0.5 mm tolerance
    if (std::fabs(pa_before - target_cm) < EPSILON_CM &&
        std::fabs(pb_before - target_cm) < EPSILON_CM) {
        std::cout << "  [pair DM2J " << slave_a << "+" << slave_b
                  << "] already at target " << target_cm << " cm — skip\n";
        return false;
    }

    // [2026-05-29] DM2J motion active — freeze arm_monitor_during_sweep_'s
    // tau-trigger logic (mechanical coupling on feet rail shifts arm M1/M2
    // baselines). RAII clear on any exit path.
    dm2j_motion_active_.store(true);
    struct ClearMotionFlag {
        std::atomic<bool>* flag;
        ~ClearMotionFlag() { flag->store(false); }
    } _clr{&dm2j_motion_active_};

    // Queue targets on both slaves (same PR slot, same absolute target).
    // Uses DM2J_RPM_FEET (faster than DM2J_RPM) since this function is exclusively
    // called for the feet rail pair (DM2J_LEFT_FOOT + DM2J_RIGHT_FOOT).
    D_(slave_a).PR_move_cm_set(pr_num, 1, DM2J_RPM_FEET, target_cm, DM2J_ACC, DM2J_DEC);
    D_(slave_b).PR_move_cm_set(pr_num, 1, DM2J_RPM_FEET, target_cm, DM2J_ACC, DM2J_DEC);

    // Broadcast trigger — both slaves start at exact same instant.
    // Bystanders on bus (other DM2J) must have PR[pr_num] = rpm=0 (safe no-op).
    D_(slave_a).PR_trigger_sync(pr_num);

    // Parallel poll until both done or fault/timeout
    bool err = dm2j_pair_poll_done_(slave_a, slave_b, timeout_ms);

    // Read final positions + log actual travel
    double pa_after = 0, pb_after = 0;
    D_(slave_a).read_position_cm(pa_after);
    D_(slave_b).read_position_cm(pb_after);
    std::cout << "  [pair DM2J " << slave_a << "+" << slave_b
              << "] after:  " << slave_a << "=" << pa_after
              << " (Δ" << (pa_after - pa_before) << ") "
              << slave_b << "=" << pb_after
              << " (Δ" << (pb_after - pb_before) << ") cm"
              << (err ? " [FAIL]" : "") << "\n";

    return err;
}

bool WashRobot::check_abort_() {
    while (pause_flag.load() && !abort_flag.load()) sleep_ms_(POLL_INTERVAL_MS);
    return abort_flag.load();
}

const char* WashRobot::state_name(State s) {
    switch (s) {
        case State::Idle:           return "idle";
        case State::Ready:          return "ready";
        case State::Attached:       return "attached";
        case State::Running:        return "running";
        case State::WaitingConfirm: return "waiting_confirm";
        case State::Paused:         return "paused";
        case State::PausedOnError:  return "paused_on_error";
        case State::Balancing:      return "balancing";
        case State::ReturningHome:  return "returning_home";
        case State::Calibrating:    return "calibrating";
        case State::Error:          return "error";
    }
    return "unknown";
}

void WashRobot::set_state_(State s) {
    State old = state_.exchange(s);
    if (old == s) return;
    std::ostringstream oss;
    oss << "state_changed " << state_name(old) << " " << state_name(s);
    evt_(oss.str());
}

std::string WashRobot::state_violation_(State cur) const {
    return std::string("ERR state_violation current=") + state_name(cur) + "\n";
}

//=========== crane ===========

bool WashRobot::crane_connect_if_needed_() {
    if (crane_cli_.isConnected()) return false;
    return !crane_cli_.connectToServer(CRANE_IP, CRANE_PORT);
}

std::string WashRobot::crane_cmd_(const std::string& line, int timeout_sec) {
    // Detached mode: don't talk to crane at all. Return a synthetic OK so that
    // callers (step_down body_pre_cycle / feet_backup / phase5 / return_home)
    // continue without aborting. Useful for bench testing when crane isn't
    // connected. Toggle via cmd_crane_attached.
    if (!crane_attached_.load()) {
        std::cout << "[crane_cmd] '" << line << "' SKIPPED (crane_attached=off)\n";
        return "OK skipped";
    }

    std::lock_guard<std::mutex> lk(crane_mtx_);

    // Self-healing reconnect (2026-05-15): try up to 2 attempts. First attempt
    // uses existing TCP connection (or fresh connect if not connected). If it
    // fails (send fails, recv timeout, no OK in reply), force-close the socket
    // and reconnect on the second attempt. This handles "zombie socket":
    // isConnected()=true but actually dead (e.g. NAT entry evicted, peer kernel
    // restart didn't send RST). Without this, the only fix was program restart
    // or operator manually toggling crane_attached.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt == 1) {
            // Force fresh socket: close current then reconnect.
            std::cout << "[crane_cmd] '" << line << "' attempt 1 failed — force reconnect\n";
            crane_cli_.close();
        }
        if (crane_connect_if_needed_()) {
            if (attempt == 1) {
                std::cout << "[crane_cmd] '" << line << "' reconnect failed\n";
                return "";
            }
            continue;   // try fresh reconnect on next attempt
        }

        std::string tx = line;
        if (tx.empty() || tx.back() != '\n') tx.push_back('\n');
        if (!crane_cli_.sendData(tx.c_str(), (int)tx.size(), 1000)) {
            continue;   // send fail → force reconnect on next attempt
        }

        // Drain lines until a non-EVT reply or timeout. EVT lines are broadcast
        // by crane to all connected clients (including this RPC channel) and can
        // arrive interleaved with replies. Filter them, dispatch to alarm handler
        // for safety-critical kinds, then continue waiting for the actual reply.
        std::string rx;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
        char buf[512];
        bool got_reply = false;
        std::string reply_line;
        while (std::chrono::steady_clock::now() < deadline) {
            int n = crane_cli_.receiveData(buf, sizeof(buf), 500);
            if (n > 0) {
                rx.append(buf, n);
                size_t pos;
                while ((pos = rx.find('\n')) != std::string::npos) {
                    std::string one = rx.substr(0, pos);
                    rx.erase(0, pos + 1);
                    if (!one.empty() && one.back() == '\r') one.pop_back();
                    if (one.empty()) continue;

                    if (one.rfind("EVT ", 0) == 0) {
                        handle_crane_evt_(one);
                        continue;   // not the reply we're waiting for
                    }
                    if (one.rfind("OK", 0) == 0) crane_last_ok_ms_ = now_ms_();
                    reply_line = one;
                    got_reply = true;
                    break;
                }
                if (got_reply) break;
            } else {
                sleep_ms_(POLL_INTERVAL_MS);
            }
        }
        if (got_reply) return reply_line;
        // recv loop exhausted without a non-EVT reply → consider this attempt
        // failed. Loop iteration ends → for loop tries attempt 1 (force reconnect).
    }
    return "";   // both attempts failed
}

//=========== cleaning arm ===========
//
// Cleaning arm = separate `motor_api` service on the same Pi, talking to two
// damiao motors (M1 large arm, M2 tool-head slot). Architecture mirrors crane:
// washrobot is a TCP client, arm_cmd_ sends a line and reads the reply.
// Differences vs crane: no EVT broadcasts (arm spec doesn't emit them) → no
// EVT filtering / alarm handler / estop channel / watchdog. Plain line-based
// RPC with self-healing reconnect. arm_attached_ toggle = bench-mode skip.

// [REMOVED 2026-06-03] arm_connect_if_needed_() — replaced by background
// reconnect ownership. TCP_client.reconnectLoop() (500ms tick) handles socket
// lifecycle by itself; manual connectToServer() raced with it and caused
// motor_api to see 3 simultaneous source-port connections + ~30s recovery
// (bench 2026-06-03). See arm_cmd_ below for the wait-for-background pattern.

std::string WashRobot::arm_cmd_(const std::string& line, int timeout_sec) {
    if (!arm_attached_.load()) {
        std::cout << "[arm_cmd] '" << line << "' SKIPPED (arm_attached=off)\n";
        return "OK skipped";
    }

    std::lock_guard<std::mutex> lk(arm_mtx_);

    // [2026-06-03] DON'T manually close()/connectToServer() — TCP_client has
    // its own reconnectLoop (500ms tick) that races with manual reconnect.
    // motor_api 2026-06-03 saw 3 source ports simultaneously, 30s recovery.
    // Trust the background thread to own socket lifecycle. Up to 2 attempts
    // absorbs "send failed because socket just dropped, background reconnected,
    // retry now works" cases. NO retry on recv timeout — could double-send
    // DEPLOY / PARK which would re-trigger motion at motor_api side.
    for (int attempt = 0; attempt < 2; ++attempt) {
        // Wait briefly for background reconnect if currently disconnected.
        // Background tick is 500ms — wait up to 1.5s (3 ticks worth).
        const auto conn_deadline = std::chrono::steady_clock::now()
                                 + std::chrono::milliseconds(1500);
        while (!arm_cli_.isConnected()
               && std::chrono::steady_clock::now() < conn_deadline) {
            sleep_ms_(100);
        }
        if (!arm_cli_.isConnected()) {
            std::cout << "[arm_cmd] '" << line
                      << "' not connected attempt=" << attempt
                      << " (waiting for background reconnect)\n";
            if (attempt == 1) return "";
            continue;
        }

        std::string tx = line;
        if (tx.empty() || tx.back() != '\n') tx.push_back('\n');
        if (!arm_cli_.sendData(tx.c_str(), (int)tx.size(), 1000)) {
            std::cout << "[arm_cmd] '" << line
                      << "' send fail attempt=" << attempt << "\n";
            // Socket dropped — background will detect (available()<0 on next
            // tick) and reconnect. Loop retries with the new socket.
            continue;
        }

        // Read one reply line (arm doesn't emit EVT, so no filtering needed).
        std::string rx;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
        char buf[512];
        while (std::chrono::steady_clock::now() < deadline) {
            int n = arm_cli_.receiveData(buf, sizeof(buf), 500);
            if (n > 0) {
                rx.append(buf, n);
                auto pos = rx.find('\n');
                if (pos != std::string::npos) {
                    std::string one = rx.substr(0, pos);
                    if (!one.empty() && one.back() == '\r') one.pop_back();
                    return one;
                }
            } else {
                sleep_ms_(POLL_INTERVAL_MS);
            }
        }
        // Receive timeout — do NOT retry. The command may have already been
        // sent + executed at motor_api (DEPLOY/PARK trigger motion). Retrying
        // would double-execute. Caller (arm_clean_sweep_cont etc.) handles ERR
        // at its own level by entering PausedOnError.
        std::cout << "[arm_cmd] '" << line
                  << "' recv timeout attempt=" << attempt << "\n";
        return "";
    }
    return "";   // both attempts failed
}

std::string WashRobot::cmd_arm_init() {
    std::cout << "[arm] INIT\n";
    std::string r = arm_cmd_("INIT", 60);
    // [arm rope protect TEMP 2026-05-21] post-INIT motors are enabled but at HOME (0).
    // Treat as Unknown — next pay_out/retract will re-evaluate via ensure_*.
    if (r.rfind("OK", 0) == 0) {
        arm_stow_state_.store(ArmStowState::Unknown);
        // [2026-05-28] Also mark arm_calibrated_=true so sweep can run without
        // requiring a full cmd_init. Useful for re-calibrating arm only (e.g.
        // after recovering from an arm error) without re-running full system init.
        arm_calibrated_.store(true);
        std::cout << "[arm] INIT OK → arm_calibrated_=true\n";
    } else {
        arm_calibrated_.store(false);
        std::cerr << "[arm] INIT failed (" << r << ") → arm_calibrated_=false\n";
    }
    return r + "\n";
}

std::string WashRobot::cmd_arm_deploy(int wall_mm, const std::string& slot) {
    if (wall_mm <= 0) return "ERR invalid_wall_mm\n";
    std::string s = slot;
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    if (s != "LEFT" && s != "CENTER" && s != "RIGHT")
        return "ERR invalid_slot (LEFT|CENTER|RIGHT)\n";
    std::ostringstream oss;
    oss << "DEPLOY " << wall_mm << " " << s;
    std::cout << "[arm] " << oss.str() << "\n";
    std::string r = arm_cmd_(oss.str(), 30);
    if (r.rfind("OK", 0) == 0) {
        // [arm rope protect TEMP 2026-05-21] obstacle detection for GUI DEPLOY.
        // If M1 stopped short of expected θ → return ERR (don't update state).
        // User sees ERR in GUI log, can inspect / clear obstacle / retry manually.
        if (verify_arm_deploy_(s, wall_mm)) {
            return "ERR DEPLOY obstacle (M1 stopped short of expected wall)\n";
        }
        // CENTER deploy stows arm for rope safety; LEFT/RIGHT leave at wall but
        // not "stowed" — mark Unknown so next ensure_arm_center_for_rope_ re-DEPLOYs.
        arm_stow_state_.store(s == "CENTER" ? ArmStowState::Center : ArmStowState::Unknown);
    }
    return r + "\n";
}

std::string WashRobot::cmd_arm_park() {
    std::cout << "[arm] PARK\n";
    std::string r = arm_cmd_("PARK", 30);
    // [arm rope protect TEMP 2026-05-21]
    if (r.rfind("OK", 0) == 0) arm_stow_state_.store(ArmStowState::Parked);
    return r + "\n";
}

std::string WashRobot::cmd_arm_status() {
    return arm_cmd_("STATUS", 3) + "\n";
}

std::string WashRobot::cmd_arm_attached(bool on) {
    bool prev = arm_attached_.exchange(on);
    if (prev != on) {
        std::cout << "[arm] arm_attached = " << (on ? "ON" : "OFF") << "\n";
    }
    // [2026-05-29] Reply format aligned with cmd_crane_attached (on/off) so GUI
    // can use the same regex pattern.
    return on ? std::string("OK arm_attached=on\n")
              : std::string("OK arm_attached=off\n");
}

// [2026-06-01] Toggle camera obstacle detection. Default OFF — testing-only
// flag, does NOT affect step_down flow until FrameAnalyzer integration is
// wired up in do_step_down_ (camera_obstacle_plan.md Phase 5).
//
// Reply mirrors arm_attached / crane_attached format so GUI can reuse regex.
std::string WashRobot::cmd_obstacle_detect(bool on) {
    bool prev = obstacle_detect_enabled_.exchange(on);
    if (prev != on) {
        std::cout << "[obstacle] obstacle_detect = " << (on ? "ON" : "OFF") << "\n";
    }
    return on ? std::string("OK obstacle_detect=on\n")
              : std::string("OK obstacle_detect=off\n");
}

// [2026-06-04] Single-shot obstacle check (Step 1 / FrameAnalyzer wiring test).
// Reads /tmp/cam{3,4}_{before,after}.jpg, runs obstacle_combine.py subprocess,
// parses the combined decision, returns formatted reply.
// Caller must capture the frames beforehand (bench_capture_motion.sh).
std::string WashRobot::cmd_obstacle_check() {
    FrameAnalyzer fa;
    auto r = fa.analyze("/tmp/cam3_before.jpg", "/tmp/cam3_after.jpg",
                        "/tmp/cam4_before.jpg", "/tmp/cam4_after.jpg");
    if (!r.ok) {
        std::ostringstream oss;
        oss << "ERR obstacle_check_fail " << r.err_msg << "\n";
        std::cerr << "[obstacle] " << oss.str();
        return oss.str();
    }
    std::ostringstream oss;
    oss << "OK action=" << FrameAnalyzer::action_name(r.action)
        << " step_cm=" << std::fixed << std::setprecision(1) << r.step_cm
        << " reason=" << r.reason << "\n";
    std::cout << "[obstacle] " << oss.str();
    return oss.str();
}

// [2026-06-04] Bootstrap obstacle probe for run_avoid iter 0.
// Sequence (per Sadie design):
//   1. capture before frames
//   2. release body cups (valve off + two-stage retract — same as step_down Phase A)
//   3. DM2J rail 0 → probe_cm (body slides out, feet still anchored to wall)
//   4. capture after frames (camera moved probe_cm)
//   5. DM2J rail probe_cm → 0 (body returns to original position)
//   6. body re-seal: valve on + disable_seal incremental extend + vacuum_check
// Feet remain sealed throughout — machine doesn't fall.
std::string WashRobot::do_obstacle_probe_(std::function<void()> cap_before,
                                            std::function<void()> cap_after,
                                            int probe_cm) {
    std::unique_lock<std::mutex> lk(motion_mtx_);
    abort_flag = false;
    motion_active_ = true;

    std::vector<int> body_slaves;
    for (int s : {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2}) {
        if (disabled_zdt_slaves_.count(s) == 0) body_slaves.push_back(s);
    }
    if (body_slaves.empty()) {
        std::cerr << "[probe] all body slaves disabled — cannot probe\n";
        motion_active_ = false;
        return "ERR probe_no_body_slaves\n";
    }

    std::cout << "[probe] start (probe_cm=" << probe_cm << ", body_slaves=" << body_slaves.size() << ")\n";
    evt_("obstacle_probe_start probe_cm=" + std::to_string(probe_cm));

    // 1. Capture before frame
    if (cap_before) {
        try { cap_before(); } catch (...) {}
    }

    // 2. body release: stall clear + valve off + vacuum wait + two-stage retract
    if (try_or_pause_([this]() { return ensure_all_zdt_stall_clear_(); },
                      "probe_stall_clear")) {
        motion_active_ = false; return "ERR aborted\n";
    }
    if (try_or_pause_([this]() { return pqw_set_relay_verified_(CH_VALVE_BODY, false); },
                      "probe_valve_off")) {
        motion_active_ = false; return "ERR aborted\n";
    }
    if (try_or_pause_([this, &body_slaves]() {
                          return vacuum_wait_release_(body_slaves, VACUUM_RELEASE_WAIT_MS);
                      }, "probe_vacuum_release")) {
        motion_active_ = false; return "ERR aborted\n";
    }
    clear_other_group_stalls_("body");
    if (try_or_pause_([this, &body_slaves]() { return pusher_two_stage_retract_(body_slaves); },
                      "probe_pusher_retract")) {
        motion_active_ = false; return "ERR aborted\n";
    }

    if (abort_flag.load()) { motion_active_ = false; return "ERR aborted\n"; }

    // 3. DM2J 0 → probe_cm (body slides probe_cm out from feet)
    if (try_or_pause_([this, probe_cm]() {
            return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, (double)probe_cm);
        }, "probe_dm2j_out")) {
        motion_active_ = false; return "ERR aborted\n";
    }
    rail_pos_cm_.store((double)probe_cm);

    // 4. Settle + capture after frame
    sleep_ms_(500);
    if (cap_after) {
        try { cap_after(); } catch (...) {}
    }

    if (abort_flag.load()) { motion_active_ = false; return "ERR aborted\n"; }

    // 5. DM2J probe_cm → 0 (body returns)
    if (try_or_pause_([this]() {
            return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, 0.0);
        }, "probe_dm2j_back")) {
        motion_active_ = false; return "ERR aborted\n";
    }
    rail_pos_cm_.store(0.0);

    if (abort_flag.load()) { motion_active_ = false; return "ERR aborted\n"; }

    // 6. Body re-seal: valve on + disable_seal extend (same as cycle_group_)
    if (try_or_pause_([this]() { return pqw_set_relay_verified_(CH_VALVE_BODY, true); },
                      "probe_valve_on")) {
        motion_active_ = false; return "ERR aborted\n";
    }

    // Targets: each cup goes back to its last_seal_pulse_ (where it was sealed before)
    std::vector<int> targets(body_slaves.size());
    for (size_t i = 0; i < body_slaves.size(); ++i) {
        const int s = body_slaves[i];
        targets[i] = last_seal_pulse_[s - 1].load();
    }
    bool any_obstacle = false;
    if (try_or_pause_([this, &body_slaves, &targets, &any_obstacle]() {
            return pusher_extend_with_disable_seal_(body_slaves, targets,
                PUSHER_RPM_BODY_EXTEND, PUSHER_ACC_BODY_EXTEND, &any_obstacle);
        }, "probe_body_reseal")) {
        motion_active_ = false; return "ERR aborted\n";
    }

    // Verify body sealed
    auto fails = vacuum_check_("body");
    if (!fails.empty()) {
        std::cerr << "[probe] body re-seal vacuum_check fail slaves=";
        for (int s : fails) std::cerr << s << ",";
        std::cerr << "\n";
        motion_active_ = false;
        evt_("obstacle_probe_fail body_unsealed");
        return "ERR probe_body_unsealed\n";
    }

    motion_active_ = false;
    std::cout << "[probe] done (frames captured, body resealed)\n";
    evt_("obstacle_probe_done");
    return "OK probe_done\n";
}

// [2026-06-04] RUN with obstacle avoidance loop. See WASH_ROBOT.h for design.
// Probe uses do_step_down_'s during/after body-rail hooks to capture before/after
// frames during the step's natural DM2J motion (= camera moves ~step_cm by body
// descending). Iteration N's hook produces frames used by iteration N+1's detector.
//
// First iteration: no last-step frames → skip detector, run default step_down to
// bootstrap. Iteration 2 onwards: detector runs with last step's captured frames.
std::string WashRobot::cmd_run_avoid() {
    // [2026-06-04 per Sadie] No state check — bench testing wants to run this
    // from Idle / Ready etc. without going through full init+attach. User is
    // responsible for ensuring cups are sealed / robot is on wall before use.
    State cur = state_.load();
    std::cout << "[run_avoid] start (current state: " << state_name(cur) << ")\n";
    evt_("run_avoid_start state=" + std::string(state_name(cur)));
    set_state_(State::Running);
    abort_flag = false;

    // Hooks: capture cam3+cam4 latest into _before/_after slots during step_down's
    // body rail motion. system() cp is fast (~ms), runs in thread for during_hook,
    // runs in main thread for after_hook.
    auto cap_before = []() {
        std::system("cp /tmp/cam3_latest.jpg /tmp/cam3_before.jpg");
        std::system("cp /tmp/cam4_latest.jpg /tmp/cam4_before.jpg");
        std::cout << "[run_avoid] hook: captured 'before' frames\n";
    };
    auto cap_after = []() {
        std::system("cp /tmp/cam3_latest.jpg /tmp/cam3_after.jpg");
        std::system("cp /tmp/cam4_latest.jpg /tmp/cam4_after.jpg");
        std::cout << "[run_avoid] hook: captured 'after' frames\n";
    };

    // [2026-06-04] iter 0 bootstrap probe — body 2cm out + return to get
    // frame pair for iter 1 detection.
    std::cout << "[run_avoid] iter 0: bootstrap probe (body 2cm out + return)\n";
    evt_("run_avoid_bootstrap_probe");
    std::string probe_r = do_obstacle_probe_(cap_before, cap_after, 2);
    bool have_prev_frames = (probe_r.rfind("OK", 0) == 0);
    if (!have_prev_frames) {
        std::cerr << "[run_avoid] bootstrap probe FAILED: " << probe_r
                  << " — iter 1 will use default step without detector\n";
        evt_("run_avoid_bootstrap_fail " + probe_r);
        // Don't abort — fall through to default-step iter 1 (user can stop manually)
    } else {
        std::cout << "[run_avoid] bootstrap probe OK — iter 1 will detect\n";
    }

    // Shortfall compensation: previous step's missed cm carries to next step.
    double pending_shortfall_cm = 0.0;
    int iter = 0;
    while (!abort_flag.load()) {
        iter++;
        std::cout << "[run_avoid] iter " << iter << " — "
                  << (have_prev_frames ? "detect → confirm → step" : "first step (default, no detector)")
                  << "\n";

        // 1. Detect (only when prev step / bootstrap probe captured frames)
        FrameAnalyzer::Action action = FrameAnalyzer::Action::Proceed;
        double suggested_step_cm = (double)step_cm_.load();   // default = current step_cm
        std::string reason = "no_frames_default";

        if (have_prev_frames) {
            FrameAnalyzer fa;
            auto detection = fa.analyze("/tmp/cam3_before.jpg", "/tmp/cam3_after.jpg",
                                         "/tmp/cam4_before.jpg", "/tmp/cam4_after.jpg");
            if (!detection.ok) {
                std::cerr << "[run_avoid] detector fail: " << detection.err_msg << "\n";
                evt_("run_avoid_detector_fail " + detection.err_msg);
                break;
            }
            action = detection.action;
            suggested_step_cm = detection.step_cm;
            reason = detection.reason;

            // 2. Broadcast EVT obstacle_ask
            std::ostringstream evt_msg;
            evt_msg << "obstacle_ask action=" << FrameAnalyzer::action_name(action)
                    << " step_cm=" << std::fixed << std::setprecision(1) << suggested_step_cm
                    << " iter=" << iter
                    << " reason=" << reason;
            evt_(evt_msg.str());
            std::cout << "[run_avoid] " << evt_msg.str() << "\n";

            // 3. Wait for user response
            obstacle_user_response_.store(-1);
            obstacle_ask_pending_.store(true);
            std::cout << "[run_avoid] waiting for user response (confirm/cancel/emergency_stop)\n";
            int waited_ms = 0;
            const int timeout_ms = OBSTACLE_ASK_TIMEOUT_S * 1000;
            while (obstacle_ask_pending_.load() && !abort_flag.load() && waited_ms < timeout_ms) {
                sleep_ms_(100);
                waited_ms += 100;
            }
            obstacle_ask_pending_.store(false);

            if (abort_flag.load()) {
                std::cout << "[run_avoid] aborted by user (emergency_stop)\n";
                break;
            }
            if (waited_ms >= timeout_ms) {
                std::cerr << "[run_avoid] user response timeout " << OBSTACLE_ASK_TIMEOUT_S << "s — abort\n";
                evt_("run_avoid_user_timeout");
                break;
            }

            int resp = obstacle_user_response_.load();
            if (resp == 0) {
                std::cout << "[run_avoid] user cancelled — stop loop\n";
                evt_("run_avoid_user_cancel");
                break;
            }

            // 4. Block action — refuse to step regardless of user (safety)
            if (action == FrameAnalyzer::Action::Block) {
                std::cout << "[run_avoid] detector said BLOCK — abort loop\n";
                evt_("run_avoid_block " + reason);
                break;
            }
        }

        // 5. Apply shortfall compensation: previous step's missed cm carries over.
        // Detector's suggested step was calculated from after-frame position which
        // may differ from actual machine position by the shortfall amount. Adding
        // shortfall lets machine land where detector planned (preserves safety margin).
        if (pending_shortfall_cm > 0.5) {
            std::cout << "[run_avoid] applying shortfall compensation +"
                      << pending_shortfall_cm << "cm to suggested " << suggested_step_cm << "cm\n";
            evt_("run_avoid_shortfall_add cm=" + std::to_string((int)pending_shortfall_cm));
            suggested_step_cm += pending_shortfall_cm;
            pending_shortfall_cm = 0.0;   // consume
        }

        // 6. Execute step_down with the chosen step_cm + hooks to capture next-iter frames
        // Cast to (int) prvalues to avoid ODR-use of static constexpr members
        // (template<typename T> std::max/min take const T& → would need out-of-class
        // definition for STEP_CM_MIN / STEP_CM_MAX which we don't have).
        const int step_cm = std::max((int)STEP_CM_MIN,
                                       std::min((int)STEP_CM_MAX,
                                                (int)std::lround(suggested_step_cm)));
        step_cm_.store(step_cm);
        std::cout << "[run_avoid] iter " << iter << " → step_down(" << step_cm << "cm)\n";
        evt_("run_avoid_step_down step_cm=" + std::to_string(step_cm));

        // Pass during/after_body_rail_hook to capture frames during Phase A DM2J move.
        // skip_cleaning_sweep=true — run_avoid focuses on obstacle detection only;
        // if user wants sweep too, use the other RUN buttons (step_down_with_sweep).
        std::string sr = do_step_down_(/*skip_cleaning_sweep=*/true,
                                        /*after_feet_rail_hook=*/{},
                                        /*before_feet_rail_hook=*/{},
                                        /*during_body_rail_hook=*/cap_before,
                                        /*after_body_rail_hook=*/cap_after);
        if (sr.rfind("OK", 0) != 0) {
            std::cerr << "[run_avoid] step_down failed: " << sr;
            evt_("run_avoid_step_down_fail " + sr);
            break;
        }
        have_prev_frames = true;   // next iter can use these frames for detection

        // [2026-06-04] Calculate shortfall from last step (read atomics set by do_step_down_)
        const double planned  = last_step_planned_cm_.load();
        const double achieved = last_step_achieved_cm_.load();
        if (planned > 0 && achieved + 0.5 < planned) {
            pending_shortfall_cm = planned - achieved;
            std::cout << "[run_avoid] step shortfall: planned " << planned
                      << " achieved " << achieved << " → carry " << pending_shortfall_cm
                      << "cm to next step\n";
        }

        // Set state back to Running between iterations (do_step_down_ may flip)
        set_state_(State::Running);
    }

    obstacle_ask_pending_.store(false);
    // Restore original state (whatever it was when run_avoid was invoked)
    if (state_.load() == State::Running) set_state_(cur);
    std::cout << "[run_avoid] done after " << iter << " iter(s), state restored to "
              << state_name(cur) << "\n";
    evt_("run_avoid_done iter=" + std::to_string(iter));
    return "OK run_avoid_done\n";
}

std::string WashRobot::cmd_obstacle_response(int v) {
    if (!obstacle_ask_pending_.load()) {
        return "ERR no_obstacle_ask_pending\n";
    }
    if (v != 0 && v != 1) {
        return "ERR usage:obstacle_response_<0|1>\n";
    }
    obstacle_user_response_.store(v);
    obstacle_ask_pending_.store(false);
    std::cout << "[run_avoid] user response = " << (v == 1 ? "CONFIRM" : "CANCEL") << "\n";
    return v == 1 ? std::string("OK confirmed\n") : std::string("OK cancelled\n");
}

// ====================================================================
// [2026-06-02] Balance calibration — see WASH_ROBOT.h for full doc + phase
// sequence. Watchdog → PausedOnError (no auto-recovery). Phase 5 records
// offset but does NOT auto-apply (Phase 6 = crane balance integration deferred).
// ====================================================================

void WashRobot::bal_cal_set_phase_(const std::string& phase) {
    {
        std::lock_guard<std::mutex> lk(balance_cal_phase_mtx_);
        balance_cal_phase_ = phase;
    }
    std::cout << "[bal_cal] phase = " << phase << "\n";
    evt_("balance_cal phase=" + phase);
}

bool WashRobot::bal_cal_read_tensions_(double& l_kg, double& r_kg) {
    std::string st = crane_cmd_("tension", 3);
    // [2026-06-02] crane `tension` cmd reply is "OK left=X right=Y\n"
    // (NOT "tension_left=X" — that prefix is only in `status` cmd reply).
    // Use " left=" / " right=" with leading space to avoid matching e.g.
    // "length_left=" if crane changes the format later.
    auto lp = st.find(" left=");
    auto rp = st.find(" right=");
    if (lp == std::string::npos || rp == std::string::npos) {
        std::cerr << "[bal_cal] tension parse fail, reply='" << st << "'\n";
        return true;
    }
    try {
        l_kg = std::stod(st.substr(lp + 6));   // skip " left="
        r_kg = std::stod(st.substr(rp + 7));   // skip " right="
        return false;
    } catch (...) {
        std::cerr << "[bal_cal] tension stod fail, reply='" << st << "'\n";
        return true;
    }
}

bool WashRobot::bal_cal_read_lengths_(double& l_cm, double& r_cm) {
    std::string st = crane_cmd_("status", 3);
    // crane reply: "OK length_left=X length_right=Y ..."
    auto lp = st.find("length_left=");
    auto rp = st.find("length_right=");
    if (lp == std::string::npos || rp == std::string::npos) return true;
    try {
        l_cm = std::stod(st.substr(lp + 12));
        r_cm = std::stod(st.substr(rp + 13));
        return false;
    } catch (...) { return true; }
}

std::string WashRobot::bal_cal_preload_() {
    bal_cal_set_phase_("preload");

    // [2026-06-02 v3] 改用 per-side retract（crane_retract_to_weight_），不再
    // 用對稱 retract + max() 軟停 — 後者對重心不平衡機體做不到雙側都達標。
    //
    // Target: GUI 上「收繩軟停張力」設定值（從 crane status 讀
    // `retract_tension_stop_kg=N`），跟現有 realign / pay_out_to_weight 共享同
    // 一個 GUI knob。fallback = 15kg if parse fails.
    //
    // Safety max: 40kg（同 rope_weight_limit_per_sensor_attached default）—
    // 任一側超過視為硬警報、abort 進 PausedOnError。

    double target_kg = 15.0;   // fallback
    {
        std::string st = crane_cmd_("status", 3);
        auto pos = st.find("retract_tension_stop_kg=");
        if (pos != std::string::npos) {
            try { target_kg = std::stod(st.substr(pos + 24)); } catch (...) {}
        }
        std::cout << "[bal_cal] preload target_kg=" << target_kg
                  << " (from crane retract_tension_stop_kg)\n";
    }

    // Max iter computed from timeout / per-iter time. pulse 300ms + settle 500ms
    // + 2x crane comm ~100ms ≈ 1 sec/iter → 20s timeout allows ~20 iter.
    const int max_iter = BAL_CAL_PRELOAD_TIMEOUT_MS / 1000;

    std::string r = crane_retract_to_weight_(target_kg,
                                             /*safety_max=*/40.0,
                                             max_iter,
                                             /*pulse_ms=*/300,
                                             /*settle_ms=*/500);
    if (!r.empty() && r.rfind("ERR", 0) == 0) {
        std::cerr << "[bal_cal] preload " << r << "\n";
        return "ERR preload_" + r.substr(4);   // strip "ERR " prefix, prepend "ERR preload_"
    }

    std::cout << "[bal_cal] preload OK\n";
    return "";
}

std::string WashRobot::bal_cal_release_body_() {
    bal_cal_set_phase_("releasing_body");
    if (balance_cal_abort_requested_.load()) return "ERR aborted";

    // [2026-06-02 v9] Use _verified_ version (mirrors step_down at line ~5398).
    // bench (Sadie): step_down's body release works fine, cal fails at same
    // valve. Diff: cal used raw controlRelay (trusts Modbus ACK). After
    // disable_seal's heavy cli_22_ traffic, gateway TCP recv buffer may carry
    // stale frames → write ACK'd but relay doesn't actuate → no vent sound,
    // cup stays sucking. _verified_ reads back PQW state, retries up to 3×.
    if (pqw_set_relay_verified_(CH_VALVE_BODY, false))
        return "ERR body_valve_off_fail";

    // body ZDT slaves: 5, 6, 7, 8 — filter out disabled (mirror do_feet_realign_)
    std::vector<int> body;
    for (int s : {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2}) {
        if (disabled_zdt_slaves_.count(s) == 0) body.push_back(s);
    }
    if (body.empty()) {
        std::cout << "[bal_cal] release_body: all body slaves disabled — skip\n";
        return "";
    }

    // [2026-06-02 v8] Active vacuum-release polling (replaces blind sleep_ms_).
    // bench: blind 1500ms sometimes left cups at -30kPa+ → pusher_two_stage_retract_
    // stalled because cups still sucking. vacuum_wait_release_ polls JC100 until
    // p >= DETACH_THRESHOLD_KPA. NOT wrapped in try_or_pause_ — don't want
    // PausedOnError mid-cal scrambling the state machine; cal handles abort itself.
    if (vacuum_wait_release_(body, BAL_CAL_VACUUM_RELEASE_TIMEOUT_MS)) {
        std::cerr << "[bal_cal] body vacuum_release_timeout — abort\n";
        return "ERR body_vacuum_release_timeout";
    }
    if (balance_cal_abort_requested_.load()) return "ERR aborted";

    if (pusher_two_stage_retract_(body))
        return "ERR body_retract_fail";
    return "";
}

std::string WashRobot::bal_cal_release_feet_center_() {
    bal_cal_set_phase_("releasing_feet_center");
    if (balance_cal_abort_requested_.load()) return "ERR aborted";

    // [2026-06-02 v9] _verified_ version (same rationale as release_body_).
    if (pqw_set_relay_verified_(CH_VALVE_FEET, false))   return "ERR feet_valve_off_fail";
    if (pqw_set_relay_verified_(CH_VALVE_CENTER, false)) return "ERR center_valve_off_fail";

    // feet (1,2,3,4) only — center (ZDT_C=9) 2026-06-02 已物理拔除 (per user),
    // 不放進 list 也不下 retract 命令。若未來重裝再加回 ZDT_C 並走 disabled
    // filter pattern。
    std::vector<int> feet_center;
    for (int s : {ZDT_RF1, ZDT_RF2, ZDT_LF1, ZDT_LF2}) {
        if (disabled_zdt_slaves_.count(s) == 0) feet_center.push_back(s);
    }
    if (feet_center.empty()) {
        std::cout << "[bal_cal] release_feet_center: all slaves disabled — skip\n";
    } else {
        // [2026-06-02 v8] Active vacuum-release polling (see bal_cal_release_body_
        // for full rationale). Same approach: vacuum_wait_release_ on the actual
        // slaves being retracted, no try_or_pause_ wrap, abort on timeout.
        if (vacuum_wait_release_(feet_center, BAL_CAL_VACUUM_RELEASE_TIMEOUT_MS)) {
            std::cerr << "[bal_cal] feet_center vacuum_release_timeout — abort\n";
            return "ERR feet_center_vacuum_release_timeout";
        }
        if (balance_cal_abort_requested_.load()) return "ERR aborted";

        if (pusher_two_stage_retract_(feet_center))
            return "ERR feet_center_retract_fail";
    }

    bal_cal_set_phase_("free_hang_settle");
    sleep_ms_(BAL_CAL_FREE_HANG_SETTLE_MS);
    return "";
}

// [2026-06-02 v7] Continuous-motor design replacing the old pulse-per-iter.
// Per Sadie 反饋: 30+ on/off cycles per cal stresses SE3 inverter ramp logic
// and isn't necessary. Replaced with "motor on, monitor IMU continuously,
// motor off when converged or overshooting".
//
// Each outer iter = ONE on/off cycle. Expected: 1-3 outer iters (initial run
// + 1-2 overshoot corrections). Hard cap at BAL_CAL_MAX_ITER (6) for runaway.
//
// Safety nets in inner poll loop (per user 2026-06-02 v7 spec):
//   - converged                                  → break (OK)
//   - sign-flip (overshoot)                       → break (OK, may flip dir next outer)
//   - imu_.read_error                             → emergency stop
//   - imu_.x frozen >1s (no IMU updates)          → emergency stop
//   - crane_cli_ TCP disconnect                   → emergency stop (best-effort off + SE3 07-10 backup)
//   - |roll| > BAL_CAL_ROLL_PANIC_DEG             → emergency stop
//   - single outer iter motor-on > INNER_MAX_MS   → emergency stop
//   - total cal time > BAL_CAL_TOTAL_TIMEOUT_S    → emergency stop
//   - abort requested                              → ERR aborted
std::string WashRobot::bal_cal_balance_loop_() {
    bal_cal_set_phase_("balancing");

    // [2026-06-02 v12, per Sadie bench] Crane's hold_loop forces hold_all_off()
    // when L+R > g_up_stop_total_kg (default 50kg). After Phase 2/3 cup release,
    // full robot weight sits on ropes — L+R typically 55-65kg, immediately tripping
    // the limit. v7 suppressed the EVT on washrobot side, but didn't stop crane
    // from internally killing the motor. Result: up_left "running" but actually
    // crane silently stopped → no roll change → inner_timeout abort.
    //
    // Workaround: query original, raise to BAL_CAL_UP_STOP_TOTAL_KG (100kg) for
    // duration of Phase 4, restore on any exit (RAII).
    double saved_up_stop_kg = 50.0;
    bool   threshold_raised = false;
    {
        std::string status_reply = crane_cmd_("status", 3);
        const auto p = status_reply.find("up_stop_total_kg=");
        if (p != std::string::npos) {
            try { saved_up_stop_kg = std::stod(status_reply.substr(p + 17)); }
            catch (...) { /* keep fallback */ }
        }
    }
    {
        std::ostringstream oss; oss << "set_up_stop_total_kg " << BAL_CAL_UP_STOP_TOTAL_KG;
        std::string reply = crane_cmd_(oss.str(), 3);
        if (reply.find("OK") != std::string::npos) {
            threshold_raised = true;
            std::cout << "[bal_cal] crane up_stop_total_kg " << saved_up_stop_kg
                      << " → " << BAL_CAL_UP_STOP_TOTAL_KG << " (cal-only)\n";
        } else {
            std::cerr << "[bal_cal] WARN: set_up_stop_total_kg failed reply=" << reply
                      << " — Phase 4 may trip on tension limit\n";
        }
    }
    // RAII: restore original threshold on ANY exit path (converged / fail / abort / throw).
    struct ThresholdRestorer {
        WashRobot* self;
        bool       raised;
        double     original;
        ~ThresholdRestorer() {
            if (raised) {
                std::ostringstream oss; oss << "set_up_stop_total_kg " << original;
                self->crane_cmd_(oss.str(), 3);
                std::cout << "[bal_cal] crane up_stop_total_kg restored → " << original << "\n";
            }
        }
    } _restore{this, threshold_raised, saved_up_stop_kg};

    const int64_t loop_start_ms   = now_ms_();
    const int64_t loop_timeout_ms = (int64_t)BAL_CAL_TOTAL_TIMEOUT_S * 1000LL;

    for (int outer = 0; outer < BAL_CAL_MAX_ITER; ++outer) {
        if (balance_cal_abort_requested_.load()) return "ERR aborted";

        // === Pre-motor checks ===
        const double init_roll = imu_.x - imu_roll0_;

        // Tension watchdog (one-shot before motor on)
        double l_kg = 0, r_kg = 0;
        const bool tension_ok = !bal_cal_read_tensions_(l_kg, r_kg);
        if (tension_ok && (l_kg < BAL_CAL_TENSION_MIN_KG || r_kg < BAL_CAL_TENSION_MIN_KG)) {
            std::cerr << "[bal_cal] WATCHDOG tension panic L=" << l_kg << " R=" << r_kg << "\n";
            return "ERR tension_panic L=" + std::to_string(l_kg) + " R=" + std::to_string(r_kg);
        }
        if (std::abs(init_roll) > BAL_CAL_ROLL_PANIC_DEG) {
            std::cerr << "[bal_cal] WATCHDOG roll panic " << init_roll << "°\n";
            return "ERR roll_panic " + std::to_string(init_roll);
        }

        // EVT broadcast outer state
        {
            std::ostringstream ev;
            ev << "balance_cal_iter " << outer << " roll=" << init_roll
               << " L=" << l_kg << " R=" << r_kg;
            evt_(ev.str());
        }

        // Convergence check (before motor on)
        if (std::abs(init_roll) < BAL_CAL_ROLL_TOL_DEG) {
            std::cout << "[bal_cal] CONVERGED after " << outer << " outer iter, roll="
                      << init_roll << "°\n";
            return "";
        }

        // Direction: roll > 0 → up_right; roll < 0 → up_left (2026-06-02 sign convention)
        const std::string dir = (init_roll > 0) ? "up_right" : "up_left";
        const double      init_sign = (init_roll > 0) ? 1.0 : -1.0;

        // Crane TCP pre-check — don't even turn on motor if comm is dead
        if (!crane_cli_.isConnected()) {
            std::cerr << "[bal_cal] WATCHDOG crane disconnected before motor on\n";
            return "ERR crane_disconnected_pre";
        }

        std::cout << "[bal_cal] outer " << outer << " roll=" << init_roll
                  << "° → " << dir << " (continuous)\n";

        // === Motor ON ===
        crane_cmd_(dir + " on", 3);
        const int64_t motor_on_ms = now_ms_();

        // === Inner poll loop ===
        double  prev_imu_x   = imu_.x;
        int     stale_count  = 0;
        std::string emergency_err;
        std::string exit_reason = "converged";  // or "overshoot" — both OK exits

        while (true) {
            sleep_ms_(BAL_CAL_INNER_POLL_MS);

            // Abort
            if (balance_cal_abort_requested_.load()) {
                emergency_err = "ERR aborted";
                break;
            }
            // Total timeout
            if ((now_ms_() - loop_start_ms) > loop_timeout_ms) {
                emergency_err = "ERR total_timeout";
                break;
            }
            // Single outer iter motor-on cap
            if ((now_ms_() - motor_on_ms) > BAL_CAL_INNER_MAX_MS) {
                emergency_err = "ERR inner_timeout";
                break;
            }
            // IMU read error
            if (imu_.read_error.load()) {
                emergency_err = "ERR imu_read_error";
                break;
            }
            // IMU staleness (value frozen → IMU likely dead/disconnected)
            const double cur_imu_x = imu_.x;
            if (std::abs(cur_imu_x - prev_imu_x) < 1e-6) {
                if (++stale_count > BAL_CAL_INNER_STALE_LIMIT) {
                    emergency_err = "ERR imu_stale";
                    break;
                }
            } else {
                stale_count = 0;
                prev_imu_x  = cur_imu_x;
            }
            // Crane TCP disconnect mid-run
            if (!crane_cli_.isConnected()) {
                emergency_err = "ERR crane_disconnected_mid";
                break;
            }
            // Roll panic
            const double roll = cur_imu_x - imu_roll0_;
            if (std::abs(roll) > BAL_CAL_ROLL_PANIC_DEG) {
                emergency_err = "ERR roll_panic " + std::to_string(roll);
                break;
            }
            // Converged (OK exit)
            if (std::abs(roll) < BAL_CAL_ROLL_TOL_DEG) {
                exit_reason = "converged";
                break;
            }
            // Sign-flip overshoot (OK exit; outer loop may run opposite direction)
            const double cur_sign = (roll > 0) ? 1.0 : -1.0;
            if (cur_sign != init_sign && std::abs(roll) > BAL_CAL_OVERSHOOT_DEG) {
                exit_reason = "overshoot";
                break;
            }
        }

        // === Motor OFF (best effort) ===
        // Send off regardless of emergency — try to stop even if TCP is sketchy.
        // SE3's own 07-10 alarm-on-no-comm backstops if this fails entirely.
        std::string off_reply = crane_cmd_(dir + " off", 3);

        if (!emergency_err.empty()) {
            std::cerr << "[bal_cal] EMERGENCY STOP: " << emergency_err
                      << " (off_reply=" << off_reply << ")\n";
            return emergency_err;
        }

        const double end_roll = imu_.x - imu_roll0_;
        std::cout << "[bal_cal] outer " << outer << " end: " << exit_reason
                  << " roll=" << end_roll << "° (motor was on "
                  << (now_ms_() - motor_on_ms) << "ms)\n";

        // Settle before next outer iter (let IMU/rope stabilize)
        sleep_ms_(BAL_CAL_SETTLE_MS);
    }
    return "ERR no_convergence_after_" + std::to_string(BAL_CAL_MAX_ITER) + "_outer_iter";
}

std::string WashRobot::do_balance_calibrate_() {
    // [2026-06-02 v11] Capture pre-cal state for proper state_before_pause_
    // restoration on fail. Without this, cmd_continue resolves to whatever
    // garbage was last written to state_before_pause_ (often Calibrating itself
    // — infinite ping-pong), making "continue" look frozen.
    const State pre_cal_state = state_.load();

    balance_cal_running_.store(true);
    balance_cal_abort_requested_.store(false);
    balance_cal_await_record_.store(false);
    set_state_(State::Calibrating);

    std::string err;
    err = bal_cal_preload_();
    if (!err.empty()) goto fail;

    err = bal_cal_release_body_();
    if (!err.empty()) goto fail;

    err = bal_cal_release_feet_center_();
    if (!err.empty()) goto fail;

    err = bal_cal_balance_loop_();
    if (!err.empty()) goto fail;

    // Phase 4 converged — await user's RECORD cmd
    bal_cal_set_phase_("awaiting_record");
    balance_cal_await_record_.store(true);
    balance_cal_running_.store(false);
    return "";

fail:
    bal_cal_set_phase_("aborted " + err);
    balance_cal_running_.store(false);
    balance_cal_await_record_.store(false);

    // [2026-06-02 v13] User-initiated abort goes to Idle (clean exit), not
    // PausedOnError. Abort means "user changed mind" not "system error".
    // Phase 2/3 already released cups so Idle reflects physical reality.
    if (balance_cal_abort_requested_.load()) {
        balance_cal_abort_requested_.store(false);
        set_state_(State::Idle);
        std::cerr << "[bal_cal] FAIL (user abort): " << err << " — state→Idle\n";
        return err;
    }

    // [2026-06-02 v11] Set state_before_pause_ to pre_cal_state (typically Attached)
    // so cmd_continue restores correctly. Mirrors crane_watchdog_loop_'s guarded
    // save pattern (line ~2829) — never overwrite if already PausedOnError.
    {
        std::lock_guard<std::mutex> slk(state_mtx_);
        if (state_.load() != State::PausedOnError)
            state_before_pause_ = pre_cal_state;
    }
    set_state_(State::PausedOnError);
    std::cerr << "[bal_cal] FAIL: " << err
              << " — state→PausedOnError, cmd_continue will resume to "
              << state_name(pre_cal_state) << "\n";
    return err;
}

std::string WashRobot::cmd_balance_calibrate_start() {
    // [2026-06-02 v3] state check 拿掉（per user），方便 bench 任何狀態試。
    // 只保留「不能重入」的 guard。
    if (balance_cal_running_.load()) {
        return "ERR already_calibrating\n";
    }

    // [2026-06-02 v6] 安全 pre-checks (per user 反饋, 雙門檻)：
    //
    // 1. crane_attached_ 必須 ON — 沒接 crane 整套流程 no-op，根本不該跑
    if (!crane_attached_.load()) {
        std::cout << "[bal_cal] REJECT cmd_start: crane not attached" << std::endl;
        return "ERR crane_not_attached (calibration needs crane control)\n";
    }
    // 2. IMU roll 雙門檻：
    //    - 太小 (< MIN) → 機體已平衡，校正無意義
    //    - 太大 (> MAX) → 太歪，preload/release 階段風險高
    const double init_roll = imu_.x - imu_roll0_;
    const double abs_roll  = std::abs(init_roll);
    if (abs_roll < BAL_CAL_START_ROLL_MIN_DEG) {
        std::cout << "[bal_cal] REJECT cmd_start: already balanced roll="
                  << init_roll << "° (< " << BAL_CAL_START_ROLL_MIN_DEG << "°)" << std::endl;
        std::ostringstream oss;
        oss << "ERR already_balanced roll=" << init_roll
            << " min=" << BAL_CAL_START_ROLL_MIN_DEG
            << " 機體已平衡，無需校正\n";
        return oss.str();
    }
    if (abs_roll > BAL_CAL_START_ROLL_MAX_DEG) {
        std::cout << "[bal_cal] REJECT cmd_start: too tilted roll="
                  << init_roll << "° (> " << BAL_CAL_START_ROLL_MAX_DEG << "°)" << std::endl;
        std::ostringstream oss;
        oss << "ERR initial_roll_too_high roll=" << init_roll
            << " max=" << BAL_CAL_START_ROLL_MAX_DEG
            << " 太傾斜，請先手動拉回再跑\n";
        return oss.str();
    }
    // 3. 低張力警告 — 不擋下，只 EVT broadcast 讓 user 知道 preload 會比較劇烈
    double init_l = 0, init_r = 0;
    if (!bal_cal_read_tensions_(init_l, init_r)) {
        if (init_l < 5.0 || init_r < 5.0) {
            std::ostringstream w;
            w << "balance_cal_warn preload_aggressive L=" << init_l
              << "kg R=" << init_r << "kg (鋼索鬆，cup 釋放時可能驟降)";
            std::cout << "[bal_cal] " << w.str() << "\n";
            evt_(w.str());
            // 不 return，繼續跑
        }
    }
    std::cout << "[bal_cal] pre-checks OK: roll=" << init_roll
              << "° L=" << init_l << "kg R=" << init_r << "kg\n";

    std::string err = do_balance_calibrate_();
    if (err.empty()) {
        return "OK calibration_done awaiting_record\n";
    }
    return err + "\n";
}

std::string WashRobot::cmd_balance_calibrate_record() {
    if (!balance_cal_await_record_.load()) {
        return "ERR no_calibration_to_record (run cmd_balance_calibrate_start first)\n";
    }
    double l_cm = 0, r_cm = 0;
    if (bal_cal_read_lengths_(l_cm, r_cm)) {
        return "ERR length_read_fail\n";
    }
    double offset = l_cm - r_cm;
    balance_cal_last_offset_cm_.store(offset);
    settings_.static_roll_offset_cm.store(offset);
    balance_cal_await_record_.store(false);
    bal_cal_set_phase_("done");
    set_state_(State::Idle);   // calibration complete, cups off, awaiting user attach

    std::ostringstream oss;
    oss << "OK static_roll_offset_cm=" << offset
        << " (L=" << l_cm << " R=" << r_cm << ")\n";
    std::cout << "[bal_cal] RECORDED " << oss.str();
    evt_("balance_cal_recorded offset=" + std::to_string(offset));
    return oss.str();
}

std::string WashRobot::cmd_balance_calibrate_abort() {
    // [2026-06-02 v13] Handle await_record case synchronously. Old behavior just
    // set abort_requested_ which only mid-loop code listens to; in await_record
    // state, do_balance_calibrate_ already returned, nobody listening → user
    // stuck in Calibrating state with phase=awaiting_record forever.
    if (balance_cal_await_record_.load()) {
        balance_cal_await_record_.store(false);
        balance_cal_abort_requested_.store(false);
        bal_cal_set_phase_("aborted_after_converge");
        set_state_(State::Idle);   // cups off → same as RECORD path's exit state
        std::cout << "[bal_cal] abort during awaiting_record — cleanup, state→Idle\n";
        evt_("balance_cal_aborted");
        return "OK aborted (offset NOT recorded)\n";
    }
    if (balance_cal_running_.load()) {
        // Mid-run: signal flag, loop will detect and bail. fail path handles state
        // (see do_balance_calibrate_'s explicit-abort branch).
        balance_cal_abort_requested_.store(true);
        std::cout << "[bal_cal] abort requested (mid-run, loop will react)\n";
        return "OK abort_requested\n";
    }
    return "ERR no_calibration_in_progress\n";
}

std::string WashRobot::cmd_balance_calibrate_status() {
    std::ostringstream oss;
    std::string phase;
    {
        std::lock_guard<std::mutex> lk(balance_cal_phase_mtx_);
        phase = balance_cal_phase_;
    }
    oss << "OK running=" << (balance_cal_running_.load() ? "1" : "0")
        << " await_record=" << (balance_cal_await_record_.load() ? "1" : "0")
        << " phase=" << (phase.empty() ? "idle" : phase)
        << " last_offset_cm=" << balance_cal_last_offset_cm_.load()
        << "\n";
    return oss.str();
}

// ====================================================================
// [2026-05-29] Runtime settings (wall-tune) — see WASH_ROBOT.h Settings struct.
//
// Simple key=value text protocol:
//   GET → "OK <key>=<current>:<default> ..." (one big space-separated line)
//   SET → cmd_set_setting("key", "value") → "OK <key>=<value>" or ERR
//   SAVE → write all current values to settings.json (working dir)
//
// File format is plain "key value" pairs, one per line. Comments after '#'.
// Chose NOT to use a JSON parser — 19 numeric settings, minimal format ok.
// ====================================================================

namespace {
// Apply setter helper that branches on type (int vs double) without a runtime
// type registry. Each helper returns true if parse failed.
template <typename T>
bool apply_to_atomic_(std::atomic<T>& a, const std::string& value, T min, T max);

template <>
bool apply_to_atomic_<int>(std::atomic<int>& a, const std::string& value, int min, int max) {
    try {
        int v = std::stoi(value);
        if (v < min || v > max) return true;
        a.store(v);
        return false;
    } catch (...) { return true; }
}
template <>
bool apply_to_atomic_<double>(std::atomic<double>& a, const std::string& value, double min, double max) {
    try {
        double v = std::stod(value);
        if (v < min || v > max) return true;
        a.store(v);
        return false;
    } catch (...) { return true; }
}
}  // namespace

std::string WashRobot::cmd_get_settings() {
    std::ostringstream oss;
    oss << "OK";
    // Format: <key>=<current>:<default>
    oss << " arm_clean_wall_mm="              << settings_.arm_clean_wall_mm.load()              << ":" << ARM_CLEAN_WALL_MM;
    oss << " pusher_extend_feet_pulse="       << settings_.pusher_extend_feet_pulse.load()       << ":" << PUSHER_EXTEND_FEET_PULSE;
    oss << " pusher_extend_feet_pulse_lower=" << settings_.pusher_extend_feet_pulse_lower.load() << ":" << PUSHER_EXTEND_FEET_PULSE_LOWER;
    oss << " pusher_extend_body_pulse="       << settings_.pusher_extend_body_pulse.load()       << ":" << PUSHER_EXTEND_BODY_PULSE;
    oss << " pusher_extend_body_pulse_short=" << settings_.pusher_extend_body_pulse_short.load() << ":" << PUSHER_EXTEND_BODY_PULSE_SHORT;
    oss << " vacuum_seal_deep_kpa="           << settings_.vacuum_seal_deep_kpa.load()           << ":" << VACUUM_SEAL_DEEP_KPA;
    oss << std::fixed << std::setprecision(2);
    oss << " realign_threshold_cm="           << settings_.realign_threshold_cm.load()           << ":" << REALIGN_THRESHOLD_CM;
    oss << " realign_threshold_mean_cm="      << settings_.realign_threshold_mean_cm.load()      << ":" << REALIGN_THRESHOLD_MEAN_CM;
    oss << " rope_weight_limit_attached="     << settings_.rope_weight_limit_attached.load()     << ":" << ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_ATTACHED;
    oss << " rope_weight_limit_hanging="      << settings_.rope_weight_limit_hanging.load()      << ":" << ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_HANGING;
    oss.unsetf(std::ios::floatfield);
    oss << " step_cm_default="                << settings_.step_cm_default.load()                << ":" << STEP_CM_DEFAULT;
    oss << " step_cm_max="                    << settings_.step_cm_max.load()                    << ":" << STEP_CM_MAX;
    oss << " vacuum_plateau_ms="              << settings_.vacuum_plateau_ms.load()              << ":" << VACUUM_PLATEAU_MS;
    oss << std::fixed << std::setprecision(2);
    oss << " vacuum_backup_cm="               << settings_.vacuum_backup_cm.load()               << ":" << VACUUM_BACKUP_CM;
    oss << " retract_slow_peel_cm="           << settings_.retract_slow_peel_cm.load()           << ":" << RETRACT_SLOW_PEEL_CM;
    oss.unsetf(std::ios::floatfield);
    oss << " disable_retry_max_iters="        << settings_.disable_retry_max_iters.load()        << ":" << DISABLE_RETRY_MAX_ITERS;
    oss << " step_margin_cm="                 << settings_.step_margin_cm.load()                 << ":" << STEP_MARGIN_CM;
    oss << std::fixed << std::setprecision(2);
    oss << " imu_ask_deg="                    << settings_.imu_ask_deg.load()                    << ":" << IMU_ASK_DEG;
    oss << " arm_deploy_pos_tol_rad="         << settings_.arm_deploy_pos_tol_rad.load()         << ":" << ARM_DEPLOY_POS_TOL_RAD;
    oss << " static_roll_offset_cm="          << settings_.static_roll_offset_cm.load()          << ":0.00";
    oss << "\n";
    return oss.str();
}

std::string WashRobot::cmd_set_setting(const std::string& key, const std::string& value) {
    // Idle-only gate: mid-motion edits could leave consumers reading
    // inconsistent values (e.g. push uses old extend, next iter uses new).
    if (state_.load() != State::Idle) {
        return std::string("ERR settings_edit_requires_Idle current_state=") + state_name(state_.load()) + "\n";
    }
    // (min, max) tuples picked from sane ranges; values way outside reject.
    bool bad = false;
    if      (key == "arm_clean_wall_mm")              bad = apply_to_atomic_<int>   (settings_.arm_clean_wall_mm,              value, 100,   1000);
    else if (key == "pusher_extend_feet_pulse")       bad = apply_to_atomic_<int>   (settings_.pusher_extend_feet_pulse,       value, 10000, 50000);
    else if (key == "pusher_extend_feet_pulse_lower") bad = apply_to_atomic_<int>   (settings_.pusher_extend_feet_pulse_lower, value, 10000, 50000);
    else if (key == "pusher_extend_body_pulse")       bad = apply_to_atomic_<int>   (settings_.pusher_extend_body_pulse,       value, 10000, 50000);
    else if (key == "pusher_extend_body_pulse_short") bad = apply_to_atomic_<int>   (settings_.pusher_extend_body_pulse_short, value, 10000, 50000);
    else if (key == "vacuum_seal_deep_kpa")           bad = apply_to_atomic_<int>   (settings_.vacuum_seal_deep_kpa,           value, -100,  0);
    else if (key == "realign_threshold_cm")           bad = apply_to_atomic_<double>(settings_.realign_threshold_cm,           value, 0.5,   20.0);
    else if (key == "realign_threshold_mean_cm")      bad = apply_to_atomic_<double>(settings_.realign_threshold_mean_cm,      value, 0.5,   20.0);
    else if (key == "rope_weight_limit_attached")     bad = apply_to_atomic_<double>(settings_.rope_weight_limit_attached,     value, 5.0,   200.0);
    else if (key == "rope_weight_limit_hanging")      bad = apply_to_atomic_<double>(settings_.rope_weight_limit_hanging,      value, 5.0,   200.0);
    else if (key == "step_cm_default")                bad = apply_to_atomic_<int>   (settings_.step_cm_default,                value, 5,     60);
    else if (key == "step_cm_max")                    bad = apply_to_atomic_<int>   (settings_.step_cm_max,                    value, 5,     100);
    else if (key == "vacuum_plateau_ms")              bad = apply_to_atomic_<int>   (settings_.vacuum_plateau_ms,              value, 200,   10000);
    else if (key == "vacuum_backup_cm")               bad = apply_to_atomic_<double>(settings_.vacuum_backup_cm,               value, 1.0,   50.0);
    else if (key == "retract_slow_peel_cm")           bad = apply_to_atomic_<double>(settings_.retract_slow_peel_cm,           value, 0.5,   10.0);
    else if (key == "disable_retry_max_iters")        bad = apply_to_atomic_<int>   (settings_.disable_retry_max_iters,        value, 1,     20);
    else if (key == "step_margin_cm")                 bad = apply_to_atomic_<int>   (settings_.step_margin_cm,                 value, 0,     50);
    else if (key == "imu_ask_deg")                    bad = apply_to_atomic_<double>(settings_.imu_ask_deg,                    value, 1.0,   45.0);
    else if (key == "arm_deploy_pos_tol_rad")         bad = apply_to_atomic_<double>(settings_.arm_deploy_pos_tol_rad,         value, 0.01,  1.0);
    else if (key == "static_roll_offset_cm")          bad = apply_to_atomic_<double>(settings_.static_roll_offset_cm,          value, -50.0, 50.0);
    else return "ERR unknown_setting_key " + key + "\n";

    if (bad) return "ERR invalid_value_or_out_of_range key=" + key + " value=" + value + "\n";
    std::cout << "[settings] " << key << " = " << value << "\n";
    return "OK " + key + "=" + value + "\n";
}

std::string WashRobot::cmd_save_settings() {
    if (save_settings_file_("settings.json")) {
        return "ERR settings_save_failed\n";
    }
    return "OK settings_saved settings.json\n";
}

bool WashRobot::load_settings_at_boot(const std::string& path) {
    // Allowed pre-init (state==Idle at construction). cmd_set_setting's Idle
    // gate is satisfied because robot.init() hasn't run yet.
    return load_settings_file_(path);
}

bool WashRobot::load_settings_file_(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cout << "[settings] " << path << " not found — using defaults\n";
        return false;
    }
    std::string line;
    int loaded = 0;
    while (std::getline(f, line)) {
        // strip comments after '#'
        auto h = line.find('#');
        if (h != std::string::npos) line.resize(h);
        // tokenize: "key value"
        std::istringstream iss(line);
        std::string key, value;
        if (!(iss >> key >> value)) continue;
        std::string r = cmd_set_setting(key, value);
        if (r.rfind("OK", 0) == 0) ++loaded;
        else std::cerr << "[settings] load skipped: " << r;
    }
    std::cout << "[settings] loaded " << loaded << " value(s) from " << path << "\n";
    return false;
}

// ====================================================================
// [2026-05-29] Per-translation-unit shadow: redirect old constexpr names to
// live settings_.<name>.load() so existing consumer code reads runtime values
// without per-site edits. These #defines take effect for code AFTER this
// point in WASH_ROBOT.cpp — load_settings_file_/save_settings_file_/
// cmd_get_settings/cmd_set_setting are ABOVE and intentionally see the
// original constexpr defaults (so cmd_get_settings can emit ":<default>").
//
// Only the symbols matching the Settings struct fields are shadowed. Other
// constants in WASH_ROBOT.h (STEP_CM_MIN, IMU_HYSTERESIS_DEG, etc.) remain
// compile-time constexpr.
// ====================================================================
#define ARM_CLEAN_WALL_MM              (settings_.arm_clean_wall_mm.load())
#define PUSHER_EXTEND_FEET_PULSE       (settings_.pusher_extend_feet_pulse.load())
#define PUSHER_EXTEND_FEET_PULSE_LOWER (settings_.pusher_extend_feet_pulse_lower.load())
#define PUSHER_EXTEND_BODY_PULSE       (settings_.pusher_extend_body_pulse.load())
#define PUSHER_EXTEND_BODY_PULSE_SHORT (settings_.pusher_extend_body_pulse_short.load())
#define VACUUM_SEAL_DEEP_KPA           (settings_.vacuum_seal_deep_kpa.load())
#define REALIGN_THRESHOLD_CM           (settings_.realign_threshold_cm.load())
#define REALIGN_THRESHOLD_MEAN_CM      (settings_.realign_threshold_mean_cm.load())
#define ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_ATTACHED (settings_.rope_weight_limit_attached.load())
#define ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_HANGING  (settings_.rope_weight_limit_hanging.load())
#define STEP_CM_DEFAULT                (settings_.step_cm_default.load())
#define STEP_CM_MAX                    (settings_.step_cm_max.load())
#define VACUUM_PLATEAU_MS              (settings_.vacuum_plateau_ms.load())
#define VACUUM_BACKUP_CM               (settings_.vacuum_backup_cm.load())
#define RETRACT_SLOW_PEEL_CM           (settings_.retract_slow_peel_cm.load())
#define DISABLE_RETRY_MAX_ITERS        (settings_.disable_retry_max_iters.load())
#define STEP_MARGIN_CM                 (settings_.step_margin_cm.load())
#define IMU_ASK_DEG                    (settings_.imu_ask_deg.load())
#define ARM_DEPLOY_POS_TOL_RAD         (settings_.arm_deploy_pos_tol_rad.load())

bool WashRobot::save_settings_file_(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "[settings] save failed — cannot open " << path << "\n";
        return true;
    }
    f << "# washrobot runtime settings — generated by cmd_save_settings\n";
    f << "# Each line: <key> <value>. Comments after '#'.\n";
    f << "arm_clean_wall_mm              " << settings_.arm_clean_wall_mm.load()              << "\n";
    f << "pusher_extend_feet_pulse       " << settings_.pusher_extend_feet_pulse.load()       << "\n";
    f << "pusher_extend_feet_pulse_lower " << settings_.pusher_extend_feet_pulse_lower.load() << "\n";
    f << "pusher_extend_body_pulse       " << settings_.pusher_extend_body_pulse.load()       << "\n";
    f << "pusher_extend_body_pulse_short " << settings_.pusher_extend_body_pulse_short.load() << "\n";
    f << "vacuum_seal_deep_kpa           " << settings_.vacuum_seal_deep_kpa.load()           << "\n";
    f << std::fixed << std::setprecision(3);
    f << "realign_threshold_cm           " << settings_.realign_threshold_cm.load()           << "\n";
    f << "realign_threshold_mean_cm      " << settings_.realign_threshold_mean_cm.load()      << "\n";
    f << "rope_weight_limit_attached     " << settings_.rope_weight_limit_attached.load()     << "\n";
    f << "rope_weight_limit_hanging      " << settings_.rope_weight_limit_hanging.load()      << "\n";
    f.unsetf(std::ios::floatfield);
    f << "step_cm_default                " << settings_.step_cm_default.load()                << "\n";
    f << "step_cm_max                    " << settings_.step_cm_max.load()                    << "\n";
    f << "vacuum_plateau_ms              " << settings_.vacuum_plateau_ms.load()              << "\n";
    f << std::fixed << std::setprecision(3);
    f << "vacuum_backup_cm               " << settings_.vacuum_backup_cm.load()               << "\n";
    f << "retract_slow_peel_cm           " << settings_.retract_slow_peel_cm.load()           << "\n";
    f.unsetf(std::ios::floatfield);
    f << "disable_retry_max_iters        " << settings_.disable_retry_max_iters.load()        << "\n";
    f << "step_margin_cm                 " << settings_.step_margin_cm.load()                 << "\n";
    f << std::fixed << std::setprecision(3);
    f << "imu_ask_deg                    " << settings_.imu_ask_deg.load()                    << "\n";
    f << "arm_deploy_pos_tol_rad         " << settings_.arm_deploy_pos_tol_rad.load()         << "\n";
    f << "static_roll_offset_cm          " << settings_.static_roll_offset_cm.load()          << "\n";
    std::cout << "[settings] saved to " << path << "\n";
    return false;
}

// ====================================================================
// [arm rope protect TEMP 2026-05-21] — see WASH_ROBOT.h for design notes.
// To DISABLE: flip ARM_ROPE_PROTECTION → false (both helpers no-op).
// To REMOVE: grep for "arm rope protect TEMP" — delete helpers + all call sites.
// ====================================================================
bool WashRobot::ensure_arm_center_for_rope_(const std::string& ctx) {
    if (!ARM_ROPE_PROTECTION) return false;
    // arm_attached=off → washrobot not driving arm; skip protection entirely
    // (otherwise arm_cmd_ returns "OK skipped" and STATUS parse would fail).
    if (!arm_attached_.load()) return false;
    if (arm_stow_state_.load() == ArmStowState::Center) return false;   // already stowed
    std::cout << "[arm_protect] " << ctx << " — ENABLE + DEPLOY "
              << ARM_ROPE_PROTECT_WALL_MM << " CENTER\n";
    // [2026-05-28] Replaced INIT with ensure_arm_ready_(): INIT now happens
    // only in cmd_init_impl_. If arm not calibrated, ensure_arm_ready_ returns
    // true → pay_out blocked for safety (correct behavior — operator must
    // cmd_init before any motion that requires arm rope protection).
    if (ensure_arm_ready_()) {
        std::cerr << "[arm_protect] arm not ready (calibration missing or ENABLE failed) — pay_out blocked for safety\n";
        return true;
    }
    std::ostringstream oss;
    oss << "DEPLOY " << ARM_ROPE_PROTECT_WALL_MM << " CENTER";
    if (arm_cmd_(oss.str(), 60).rfind("OK", 0) != 0) {
        std::cerr << "[arm_protect] DEPLOY CENTER failed — pay_out blocked for safety\n";
        return true;
    }

    // [arm rope protect TEMP 2026-05-21] obstacle detection — refactored 5/21x
    // into verify_arm_deploy_ helper so cmd_arm_deploy / clean_sweep sub-rounds
    // can reuse the same STATUS-based check across LEFT / CENTER / RIGHT slots.
    if (verify_arm_deploy_("CENTER", ARM_ROPE_PROTECT_WALL_MM)) return true;

    arm_stow_state_.store(ArmStowState::Center);
    return false;
}

// [2026-05-26] Fire-and-forget arm rail sweep.
// 為何不用 PR_move_cm (blocking)：搬到 cli_22_ slave 14 後跟 disable_seal 階段的
// JC100 壓力讀撞 bus → PR_move_cm 內 status poll timeout → sweep 整輪 abort。
// nowait 版只做 PR_move_set + PR_trigger (modbus write only)，沒 poll → 不受
// contention 影響。Re-fire N 次冗餘：單一 write frame 可能因 bus contention 被
// dropped/timeout，多 fire 確保至少一個 land。Re-fire 同一個 target 是 idempotent
// (driver 只是重新 load PRx slot + re-trigger)。
// 最後 sleep ARM_SWEEP_EST_MS 估計 motion 時間，否則下一段 fire 會覆蓋前一段
// target 害 arm 跳到新 target 沒走完前一段。
void WashRobot::arm_sweep_fire_nowait_(double target_cm) {
    for (int i = 0; i < ARM_SWEEP_FIRE_RETRIES; ++i) {
        D_(DM2J_ARM).PR_move_cm_nowait(0, 1, ARM_SWEEP_RPM, target_cm,
                                         ARM_SWEEP_ACC, ARM_SWEEP_DEC);
        if (i < ARM_SWEEP_FIRE_RETRIES - 1) sleep_ms_(ARM_SWEEP_FIRE_SPACING_MS);
    }
    // [2026-05-28] Replace plain sleep with monitor loop (Option A: DM2J:14
    // alarm bit + Option C: damiao M2 tau spike). Sets arm_sweep_obstacle_pending_
    // on detection → main thread try_or_pause_ external-pause picks it up.
    arm_monitor_during_sweep_();
}

// [2026-05-28] Watches for obstacles during slide motion (replaces plain sleep).
// Option A: DM2J:14 (slide motor) status — alarm bit set means motor stalled
//           (something heavy enough to make the slide motor fail).
// Option C: damiao M1 + M2 tau — captured baselines at entry, watch for sustained
//           |tau - baseline| > threshold on EITHER motor.
//   - M1 holds TOUCHWALL via PD; lateral push on tool → reaction force along M1
//     lever arm → tau spike. Primary detector for "something blocks the tool".
//   - M2 holds slot angle; sensitive to twisting forces. Secondary backup.
// All share the same pause channel (arm_sweep_obstacle_pending_); detail string
// distinguishes the source (slide_alarm / m1_tau_spike / m2_tau_spike) for the EVT.
void WashRobot::arm_monitor_during_sweep_() {
    // arm_attached_=off → no tau to read; fall back to plain sleep so caller
    // semantics unchanged. (DM2J:14 still moves; could monitor alarm but no
    // tau reference.)
    if (!arm_attached_.load()) {
        sleep_ms_(ARM_SWEEP_EST_MS);
        return;
    }
    // [2026-06-06] Polling fully disabled — bench testing without arm obstacle
    // detection. The three signal_obstacle() calls below are already commented
    // (M1 INSTANT / M1 SPIKE / M2 SPIKE), so all the tau reading + DM2J:14
    // status reading produces no action — pure comm waste:
    //   - arm_cmd_("STATUS") @ 200ms on localhost TCP :9527 → motor_api → damiao
    //   - D_(DM2J_ARM).read_status() @ 200ms on cli_22_ (contends w/ JC100 polling)
    // Short-circuit to plain sleep. Re-enable by removing the early-return
    // below + uncommenting the 3 signal_obstacle() blocks.
    sleep_ms_(ARM_SWEEP_EST_MS);
    return;

    // Helper: parse tau value from STATUS reply for a given motor tag.
    auto parse_tau = [](const std::string& s, const char* tag, float& out) -> bool {
        auto p = s.find(tag);
        if (p == std::string::npos) return false;
        auto tp = s.find("tau=", p);
        if (tp == std::string::npos) return false;
        try { out = std::stof(s.substr(tp + 4)); return true; }
        catch (...) { return false; }
    };

    // ---- Capture baselines for both M1 and M2 (best effort) ----
    // Status reply format from motor_api STATUS:
    //   "[M1] pos=X vel=Y tau=Z hold=? moving=? | [M2] pos=X vel=Y tau=Z ..."
    float m1_baseline_tau = 0.0f, m2_baseline_tau = 0.0f;
    bool  have_m1 = false, have_m2 = false;
    {
        std::string s = arm_cmd_("STATUS", 1);
        have_m1 = parse_tau(s, "[M1]", m1_baseline_tau);
        have_m2 = parse_tau(s, "[M2]", m2_baseline_tau);
        if (!have_m1 && !have_m2) {
            std::cerr << "[arm_sweep_monitor] baseline tau capture failed (STATUS='"
                      << s << "') — slide alarm check only (Option A active)\n";
        } else {
            std::cout << "[arm_sweep_monitor] baseline M1_tau="
                      << (have_m1 ? std::to_string(m1_baseline_tau) : "N/A")
                      << " M2_tau="
                      << (have_m2 ? std::to_string(m2_baseline_tau) : "N/A")
                      << " (M1 inst=" << ARM_SWEEP_M1_INSTANT_THRESHOLD_NM
                      << " spike=" << ARM_SWEEP_M1_SPIKE_THRESHOLD_NM
                      << " sust=" << ARM_SWEEP_M1_SUSTAINED_NM
                      << " cnt=" << ARM_SWEEP_M1_TAU_CONFIRM_CNT
                      << ", M2 spike=" << ARM_SWEEP_M2_SPIKE_THRESHOLD_NM
                      << " sust=" << ARM_SWEEP_M2_SUSTAINED_NM
                      << " cnt=" << ARM_SWEEP_M2_TAU_CONFIRM_CNT << ")\n";
        }
    }

    int m1_spike_count = 0, m2_spike_count = 0;
    bool m1_armed = false, m2_armed = false;   // [2026-05-28aa/ab] spike+sustained state machine
    // [2026-05-28ad] track previous delta to compute rate of change. Drift has
    // slow gradual rise (~0.014/poll for M2, ~0.097/poll for M1), real block has
    // sudden jump (~0.1+/poll for M2, ~0.4+/poll for M1). Use rate as discriminator.
    float m1_prev_delta = -1.0f, m2_prev_delta = -1.0f;
    int elapsed = 0;
    bool ever_busy = false;   // [2026-05-28] track that path_done has been cleared (motion started)
    // [2026-05-29] DM2J motion gate: freeze tau detection while feet rail /
    // pushers move (mechanical coupling shifts M1/M2 baselines). Track edge
    // so we re-baseline once motion ends.
    bool dm2j_active_prev = dm2j_motion_active_.load();
    while (elapsed < ARM_SWEEP_EST_MS) {
        sleep_ms_(ARM_SWEEP_MONITOR_POLL_MS);
        elapsed += ARM_SWEEP_MONITOR_POLL_MS;

        // Already flagged elsewhere? exit early to avoid duplicate EVT
        if (arm_sweep_obstacle_pending_.load()) break;

        // [2026-05-29] DM2J motion gate (A + B combined):
        //   A. While dm2j_motion_active_ = true → skip tau trigger, hold
        //      counters / armed state / prev_delta. Slide alarm + early-exit
        //      checks below still run (those are independent of arm tau).
        //   B. On true→false transition → re-baseline M1/M2 from current tau
        //      so post-motion drift doesn't carry into next detection window.
        const bool dm2j_active_now = dm2j_motion_active_.load();
        if (!dm2j_active_now && dm2j_active_prev) {
            // Motion just ended → re-baseline
            std::string s = arm_cmd_("STATUS", 1);
            float new_m1 = 0.0f, new_m2 = 0.0f;
            const bool got_m1 = have_m1 && parse_tau(s, "[M1]", new_m1);
            const bool got_m2 = have_m2 && parse_tau(s, "[M2]", new_m2);
            if (got_m1) m1_baseline_tau = new_m1;
            if (got_m2) m2_baseline_tau = new_m2;
            m1_armed = m2_armed = false;
            m1_spike_count = m2_spike_count = 0;
            m1_prev_delta = m2_prev_delta = -1.0f;
            std::cout << "[arm_sweep_monitor] dm2j motion ended → re-baseline"
                      << " M1_tau=" << (got_m1 ? std::to_string(m1_baseline_tau) : "N/A")
                      << " M2_tau=" << (got_m2 ? std::to_string(m2_baseline_tau) : "N/A")
                      << " (counters reset)\n";
        }
        dm2j_active_prev = dm2j_active_now;

        // [2026-05-28] Helper: stop slide + raise obstacle flag together.
        // Critical to stop the slide IMMEDIATELY — PR_move_cm_nowait already
        // fired, slide will continue to target unless we send stop. Without
        // this, monitor breaks out of loop but slide keeps rolling to 0cm
        // (~3-4s more), pause UI fires AFTER slide stops → user sees pause at
        // wrong position.
        auto signal_obstacle = [this](const std::string& detail) {
            D_(DM2J_ARM).speed_move_stop();   // 0x6002 = 0x0040 (PR motion halt)
            {
                std::lock_guard<std::mutex> lk(arm_sweep_obstacle_mtx_);
                arm_sweep_obstacle_detail_ = detail;
            }
            arm_sweep_obstacle_pending_.store(true);
            evt_("arm_sweep_obstacle " + detail);
        };

        // ---- Option A: DM2J:14 slide motor alarm + early motion-done exit ----
        // [2026-05-29] Skip DM2J:14 status read during first 1000ms — slide is
        // in acceleration phase, obstacle probability low. Avoids cli_22_ bus
        // contention with body_pre_cycle vacuum_wait_release_ (JC100 on cli_22_).
        // After 1000ms, read every poll (200ms) as before. Motion complete
        // typically fires at 1400-2400ms, so early-exit unaffected in practice.
        if (elapsed >= 1000) {
            uint32_t st = 0;
            if (!D_(DM2J_ARM).read_status(st)) {
                // Alarm bit
                if (st & 0x0001) {
                    std::cerr << "[arm_sweep_monitor] DM2J:14 alarm bit set (status=0x"
                              << std::hex << st << std::dec << ") — slide stalled\n";
                    signal_obstacle("slide_alarm");
                    break;
                }
                // Motion completion: cmd_done (0x10) + path_done (0x20) both SET,
                // AND we've seen path_done CLEAR earlier (edge detect, filters stale
                // done bit from previous move). Mirrors PR_move_cm Phase 1+2 logic.
                const bool cmd_done  = (st & 0x0010) != 0;
                const bool path_done = (st & 0x0020) != 0;
                if (!path_done) ever_busy = true;
                if (ever_busy && cmd_done && path_done) {
                    std::cout << "[arm_sweep_monitor] motion complete at t=" << elapsed
                              << "ms (early exit, saved ~" << (ARM_SWEEP_EST_MS - elapsed) << "ms)\n";
                    break;
                }
            }
        }

        // [2026-05-28] Skip tau-based trigger in last DECEL_MASK_MS — slide
        // deceleration induces M1 tau spike that mimics obstacle. Trade-off:
        // obstacles in last ~16cm of slide travel not detected.
        // Diagnostic log still prints in decel mask so user can see what M1/M2
        // are doing during decel.
        const bool in_decel_mask = (elapsed > ARM_SWEEP_EST_MS - ARM_SWEEP_DECEL_MASK_MS);

        // ---- Option C: M1 + M2 tau spike (if baselines available) ----
        // [2026-05-29] Skip entirely while DM2J motion active — mechanical
        // coupling moves baselines. Diagnostic still useful so we print a
        // "GATED" marker every N polls so user knows monitor is alive.
        if (dm2j_active_now) {
            if ((elapsed / ARM_SWEEP_MONITOR_POLL_MS) % 5 == 0) {
                std::cout << "[arm_sweep_monitor] t=" << elapsed
                          << "ms DM2J_MOTION_GATE active (tau detection paused)\n";
            }
            continue;   // skip tau-trigger block; outer loop tick continues
        }
        if (have_m1 || have_m2) {
            std::string s = arm_cmd_("STATUS", 1);
            float m1_tau = 0.0f, m2_tau = 0.0f;
            bool got_m1 = have_m1 && parse_tau(s, "[M1]", m1_tau);
            bool got_m2 = have_m2 && parse_tau(s, "[M2]", m2_tau);
            // [2026-06-05] Direction-aware delta: obstacle = motor exerts MORE
            // force = tau magnitude INCREASES (same sign as baseline going further
            // from zero). Opposite direction (motor relaxes / less load) is NOT an
            // obstacle. False positive on 2026-06-05 (baseline -5.6 → spike -5.1,
            // motor relaxed but old fabs() triggered) + earlier false positive
            // example in comment ("M1_tau=-0.05 vs steady -1.3, delta=1.27 → false").
            // Now: count delta only if it goes same direction as baseline's sign.
            auto directional_delta = [](float now, float baseline) -> float {
                float signed_delta = now - baseline;
                if (baseline < 0.0f && signed_delta < 0.0f) return -signed_delta;   // more negative = obstacle
                if (baseline > 0.0f && signed_delta > 0.0f) return  signed_delta;   // more positive = obstacle
                return 0.0f;                                                         // opposite direction = relaxation, ignore
            };
            const float m1_delta = got_m1 ? directional_delta(m1_tau, m1_baseline_tau) : 0.0f;
            const float m2_delta = got_m2 ? directional_delta(m2_tau, m2_baseline_tau) : 0.0f;

            // Diagnostic per-poll log (user tune phase 2026-05-28)
            std::cout << "[arm_sweep_monitor] t=" << elapsed
                      << "ms M1_tau=" << (got_m1 ? std::to_string(m1_tau) : "N/A")
                      << " d=" << (got_m1 ? std::to_string(m1_delta) : "N/A")
                      << " M2_tau=" << (got_m2 ? std::to_string(m2_tau) : "N/A")
                      << " d=" << (got_m2 ? std::to_string(m2_delta) : "N/A")
                      << " (m1armed=" << (m1_armed ? "1" : "0")
                      << " m1cnt=" << m1_spike_count
                      << " m2armed=" << (m2_armed ? "1" : "0")
                      << " m2cnt=" << m2_spike_count
                      << (in_decel_mask ? " DECEL_MASK" : "") << ")\n";

            // Skip trigger logic during decel mask period
            if (in_decel_mask) continue;

            // [2026-05-28aa] Revert to spike+sustained+armed state machine.
            // Gradient filter (28z) blocked initial spike of real blocks (when d
            // jumps from baseline to >0.4 in single poll, prev was still low).
            // M1 check (3 tiers):
            //   INSTANT (d > 0.7): trigger immediately, no confirmation
            //   SPIKE (d > 0.4): armed → wait for sustained
            //   SUSTAINED while armed (d > 0.2): cnt++; cnt >= CONFIRM → trigger
            //   Back below SUSTAINED while armed: dis-arm (was single-poll noise)
            if (got_m1) {
                const float m1_change = (m1_prev_delta >= 0) ? std::fabs(m1_delta - m1_prev_delta) : 0.0f;
                // Tier 1: INSTANT — heavy spike, single poll trigger (rate not required)
                // [2026-05-29] Gate INSTANT to elapsed >= 400ms. The first 1-2 polls
                // after baseline capture can show a huge spurious delta if M1 was
                // still settling into hold-torque from DEPLOY (observed baseline
                // M1_tau=-0.05 vs steady -1.3, delta=1.27 → false INSTANT). The
                // spike-armed-confirm tier (Tier 2) below still catches real
                // obstacles within 400ms via sustained-poll filter.
                if (elapsed >= 400 && m1_delta > ARM_SWEEP_M1_INSTANT_THRESHOLD_NM) {
                    std::cerr << "[arm_sweep_monitor] M1 INSTANT TRIGGER d=" << m1_delta
                              << " > " << ARM_SWEEP_M1_INSTANT_THRESHOLD_NM << " Nm (DISABLED — testing mode)\n";
                    // [2026-06-06] Disabled per user — bench testing scenario doesn't need
                    // arm obstacle detection. Re-enable by uncommenting:
                    // signal_obstacle("m1_tau_instant");
                    // break;
                }
                // Tier 2: SPIKE + RATE → armed (filter gradual drift)
                if (m1_delta > ARM_SWEEP_M1_SPIKE_THRESHOLD_NM
                    && m1_change > ARM_SWEEP_M1_RATE_THRESHOLD_NM) {
                    if (!m1_armed) {
                        m1_armed = true;
                        std::cout << "[arm_sweep_monitor] M1 ARMED by spike d=" << m1_delta
                                  << " (> " << ARM_SWEEP_M1_SPIKE_THRESHOLD_NM << ")"
                                  << " rate=" << m1_change << " (> " << ARM_SWEEP_M1_RATE_THRESHOLD_NM << ")\n";
                    }
                    ++m1_spike_count;
                } else if (m1_armed && m1_delta > ARM_SWEEP_M1_SUSTAINED_NM) {
                    // Sustained elevation after armed spike
                    ++m1_spike_count;
                } else if (m1_armed) {
                    std::cout << "[arm_sweep_monitor] M1 DIS-ARMED (back to baseline d=" << m1_delta << ")\n";
                    m1_armed = false;
                    m1_spike_count = 0;
                }
                if (m1_armed && m1_spike_count >= ARM_SWEEP_M1_TAU_CONFIRM_CNT) {
                    std::cerr << "[arm_sweep_monitor] M1 tau spike CONFIRMED"
                              << " (tau=" << m1_tau << " baseline=" << m1_baseline_tau
                              << " delta=" << m1_delta << " Nm, " << m1_spike_count
                              << " polls after spike-arm) (DISABLED — testing mode)\n";
                    // [2026-06-06] Disabled per user — re-enable:
                    // signal_obstacle("m1_tau_spike");
                    // break;
                    // Reset state so we don't re-print on every subsequent poll.
                    m1_armed = false;
                    m1_spike_count = 0;
                }
                m1_prev_delta = m1_delta;
            }
            // [2026-05-28ab] M2 check (spike+sustained, mirrors M1):
            //   SPIKE (d > 0.5): armed → wait for sustained
            //   SUSTAINED while armed (d > 0.3): cnt++; cnt >= CONFIRM → trigger
            //   Back below SUSTAINED while armed: dis-arm
            // M2 reacts EARLIER than M1 to obstacles (tool head contacts first,
            // M1 PD response lags 1+ poll), so this often triggers before M1 spike.
            if (got_m2) {
                const float m2_change = (m2_prev_delta >= 0) ? std::fabs(m2_delta - m2_prev_delta) : 0.0f;
                // SPIKE + RATE → armed (filter gradual drift)
                if (m2_delta > ARM_SWEEP_M2_SPIKE_THRESHOLD_NM
                    && m2_change > ARM_SWEEP_M2_RATE_THRESHOLD_NM) {
                    if (!m2_armed) {
                        m2_armed = true;
                        std::cout << "[arm_sweep_monitor] M2 ARMED by spike d=" << m2_delta
                                  << " (> " << ARM_SWEEP_M2_SPIKE_THRESHOLD_NM << ")"
                                  << " rate=" << m2_change << " (> " << ARM_SWEEP_M2_RATE_THRESHOLD_NM << ")\n";
                    }
                    ++m2_spike_count;
                } else if (m2_armed && m2_delta > ARM_SWEEP_M2_SUSTAINED_NM) {
                    ++m2_spike_count;
                } else if (m2_armed) {
                    std::cout << "[arm_sweep_monitor] M2 DIS-ARMED (back to baseline d=" << m2_delta << ")\n";
                    m2_armed = false;
                    m2_spike_count = 0;
                }
                if (m2_armed && m2_spike_count >= ARM_SWEEP_M2_TAU_CONFIRM_CNT) {
                    std::cerr << "[arm_sweep_monitor] M2 tau spike CONFIRMED"
                              << " (tau=" << m2_tau << " baseline=" << m2_baseline_tau
                              << " delta=" << m2_delta << " Nm, " << m2_spike_count
                              << " polls after spike-arm) (DISABLED — testing mode)\n";
                    // [2026-06-06] Disabled per user — re-enable:
                    // signal_obstacle("m2_tau_spike");
                    // break;
                    m2_armed = false;
                    m2_spike_count = 0;
                }
                m2_prev_delta = m2_delta;
            }
        }
    }
}

// [2026-05-29] Handle post-sweep obstacle pause for continuous sweep mode.
// Background sweep sets flag + stops slide on obstacle but can't show UI.
// Main thread calls this AFTER fut_sweep.get() to give user the choice.
//   Retry / Skip → also send slide back to 0 (next sweep starts from home)
//   Abort        → return true, caller propagates ERR. Slide stays at obstacle
//                  position so operator can investigate.
bool WashRobot::handle_post_sweep_obstacle_(const std::string& context) {
    if (!arm_sweep_obstacle_pending_.load()) return false;

    std::string detail;
    {
        std::lock_guard<std::mutex> lk(arm_sweep_obstacle_mtx_);
        detail = arm_sweep_obstacle_detail_;
    }
    PauseAction a = await_user_intervention_("arm_sweep_obstacle_" + context + " " + detail);
    arm_sweep_obstacle_pending_.store(false);

    if (a == PauseAction::Abort) {
        std::cout << "[" << context << "] obstacle → Abort, leaving slide at interrupt position\n";
        return true;
    }

    // Retry / Skip: ensure slide returns to 0 before next sweep / step continues.
    // Slide was stopped mid-sweep via speed_move_stop() in signal_obstacle().
    std::cout << "[" << context << "] obstacle resolved ("
              << (a == PauseAction::Retry ? "Retry" : "Skip")
              << ") → sending slide back to 0\n";
    arm_sweep_fire_nowait_(0.0);   // fire + monitor + sleep EST_MS, slide reaches 0
    // Clear flag again — slide return might trigger another spike, ignore here
    arm_sweep_obstacle_pending_.store(false);

    if (a == PauseAction::Skip) {
        arm_sweep_skip_rest_of_run_.store(true);
        std::cout << "[" << context << "] Skip → arm_sweep_skip_rest_of_run_=true\n";
    }
    return false;
}

// [arm rope protect TEMP 2026-05-21] verify M1 reached expected θ after DEPLOY.
// Slot-aware: LEFT / CENTER / RIGHT each have different TOOL_EXT, so expected θ
// for the same wall_mm differs. Mirrors motor_api touch_wall_slot formula.
//
// Skip (return false) cases:
//   - ARM_ROPE_PROTECTION disabled at compile time
//   - arm_attached_ = off (washrobot doesn't drive arm → STATUS would be "OK skipped")
//
// Fail (return true) cases:
//   - STATUS reply has no "[M1] pos=" (motor_api offline / unexpected format)
//   - M1 actual angle < expected - ARM_DEPLOY_POS_TOL_RAD (obstacle blocked)
bool WashRobot::verify_arm_deploy_(const std::string& slot, int wall_mm) {
    if (!ARM_ROPE_PROTECTION) return false;
    if (!arm_attached_.load()) return false;
    // [2026-06-06] Bench testing — disable DEPLOY obstacle verification (M1 angle
    // vs expected wall). Same scenario as arm_sweep_monitor short-circuit
    // (2026-06-06h): no real wall to deploy against, M1 stays at ~0 rad and
    // every DEPLOY would trip a false obstacle. 4 call sites (cmd_arm_deploy,
    // ensure_arm_at_center_for_rope_, do_arm_clean_sweep_, do_arm_clean_sweep_continuous_)
    // all bypass with this single early return. Re-enable by removing this block.
    return false;

    float tool_ext;
    if (slot == "LEFT")       tool_ext = ARM_M2_TOOL_LEFT_MM;
    else if (slot == "RIGHT") tool_ext = ARM_M2_TOOL_RIGHT_MM;
    else                      tool_ext = ARM_M2_TOOL_CENTER_MM;   // CENTER default

    const float total_ext = ARM_M1_PASSIVE_EXT_MM + tool_ext;
    const float usable    = (float)wall_mm - total_ext;
    const float expected_rad = (usable <= 0.0f)
        ? ARM_M1_VERTICAL_OFF_RAD
        : ARM_M1_VERTICAL_OFF_RAD + std::asin(std::min(usable / ARM_M1_LENGTH_MM, 1.0f));

    std::string status_reply = arm_cmd_("STATUS", 3);
    auto p = status_reply.find("[M1] pos=");
    if (p == std::string::npos) {
        std::cerr << "[arm_protect] verify_deploy STATUS parse fail — reply='"
                  << status_reply << "'\n";
        return true;
    }
    float actual_rad = 0.0f;
    try { actual_rad = std::stof(status_reply.substr(p + 9)); }
    catch (...) {
        std::cerr << "[arm_protect] verify_deploy pos parse exception — reply='"
                  << status_reply << "'\n";
        return true;
    }
    const float delta = expected_rad - actual_rad;
    std::cout << "[arm_protect] verify_deploy " << slot << " wall=" << wall_mm
              << " M1 actual=" << std::fixed << std::setprecision(3) << actual_rad
              << " expected=" << expected_rad
              << " delta=" << delta << " rad (tol=" << ARM_DEPLOY_POS_TOL_RAD << ")\n";
    if (delta > ARM_DEPLOY_POS_TOL_RAD) {
        std::cerr << "[arm_protect] DEPLOY " << slot << " hit obstacle — M1 stopped "
                  << (delta * ARM_M1_LENGTH_MM) << " mm short of expected wall\n";
        return true;
    }
    return false;
}

// [2026-06-06] Verify M2 actually rotated to the requested slot. motor_api's
// lr_move_to_slot prints "Done" on a fixed timeout without confirming M2 reached
// the target angle — observed bench pattern: M2 stays at -0.58 (LEFT side) when
// commanded to +0.7 (RIGHT slot), because distance 1.27 rad > what its internal
// settle window allows. Returns true if M2 NOT at slot (caller can retry).
//   slot → expected M2 rad:  LEFT=-0.7,  RIGHT=+0.7,  CENTER=0.0
bool WashRobot::verify_arm_m2_at_slot_(const std::string& slot) {
    if (!arm_attached_.load()) return false;   // can't verify, trust motor_api
    float target_rad = 0.0f;
    // [2026-06-06p] Slot targets reduced from ±0.7 to ±0.5 in motor_api
    // (cleaning_arm) — clearance from mechanical stop 0.1 → 0.3 rad to avoid
    // M2 fault state. Verify here must mirror that change or every DEPLOY
    // would report off-target by 0.2 rad and trigger unnecessary retry.
    if (slot == "LEFT")       target_rad = -0.5f;
    else if (slot == "RIGHT") target_rad =  0.5f;
    else                      target_rad =  0.0f;   // CENTER

    std::string s = arm_cmd_("STATUS", 3);
    auto p = s.find("[M2]");
    if (p == std::string::npos) {
        std::cerr << "[arm_m2_verify] STATUS missing [M2] block — skip verify (reply='"
                  << s << "')\n";
        return false;   // can't verify, don't block flow
    }
    auto pp = s.find("pos=", p);
    if (pp == std::string::npos) {
        std::cerr << "[arm_m2_verify] STATUS [M2] missing 'pos=' — skip verify\n";
        return false;
    }
    float m2_pos = 0.0f;
    try { m2_pos = std::stof(s.substr(pp + 4)); }
    catch (...) {
        std::cerr << "[arm_m2_verify] M2 pos parse exception — skip verify\n";
        return false;
    }
    const float diff = std::fabs(m2_pos - target_rad);
    const bool fail = (diff > ARM_M2_SLOT_TOL_RAD);
    std::cout << "[arm_m2_verify] slot=" << slot
              << " M2_pos=" << std::fixed << std::setprecision(3) << m2_pos
              << " target=" << target_rad
              << " diff=" << diff << " rad (tol=" << ARM_M2_SLOT_TOL_RAD << ")"
              << (fail ? " FAIL" : " OK") << "\n";
    return fail;
}

bool WashRobot::ensure_arm_parked_after_rope_(const std::string& ctx) {
    if (!ARM_ROPE_PROTECTION) return false;
    if (arm_stow_state_.load() == ArmStowState::Parked) return false;   // already parked
    std::cout << "[arm_protect] " << ctx << " — PARK\n";
    if (arm_cmd_("PARK", 30).rfind("OK", 0) != 0) {
        std::cerr << "[arm_protect] PARK failed — non-fatal, arm may still be deployed\n";
        return true;   // log only, don't block flow (retract already done)
    }
    arm_stow_state_.store(ArmStowState::Parked);
    return false;
}

// [2026-05-28] Ensure damiao arm is ready for DEPLOY without re-calibrating.
// Replaces per-sweep INIT calls. INIT now runs only in cmd_init_impl_ (so the
// obstacle-during-INIT-corrupts-zero risk is bounded to system init time when
// operator is present). Sweep paths call this helper, which just re-enables
// the motors (PARK disabled them) using their existing calibrated zero.
bool WashRobot::ensure_arm_ready_() {
    // arm_attached_=off → sweep skips anyway (do_arm_clean_sweep_/_continuous_
    // already have early-return on this); return success to keep contract clean.
    if (!arm_attached_.load()) {
        return false;
    }
    if (!arm_calibrated_.load()) {
        std::cerr << "[arm_ready] arm not calibrated — run cmd_init first (arm INIT now part of system init flow)\n";
        return true;
    }
    // Re-enable both motors. PARK disables motors after each sweep finishes;
    // ENABLE without re-calibrating preserves the zero set at cmd_init.
    if (arm_cmd_("M1 ENABLE", 5).rfind("OK", 0) != 0) {
        std::cerr << "[arm_ready] M1 ENABLE failed\n";
        return true;
    }
    if (arm_cmd_("M2 ENABLE", 5).rfind("OK", 0) != 0) {
        std::cerr << "[arm_ready] M2 ENABLE failed\n";
        return true;
    }
    return false;
}

// ---- Cleaning sweep (sequential: 上滑台 + 水 + 刷 + cleaning arm) ----
// 流程:
//   A. 開水(進水球閥 + 水箱泵浦)+ 開刷洗滾筒
//   B. arm DEPLOY <wall_mm> CENTER(M1 大臂貼牆,M2 工具頭 CENTER)
//   C. rounds × {上滑台 → +ARM_SWEEP_CM、arm M2 LR_SLOT RIGHT
//                 → 0、arm M2 LR_SLOT CENTER}
//      (2026-05-25: 移除中間 -ARM_SWEEP_CM 段,改成單向 +CM → 0)
//   D. (RAII ScopeExit)PARK + 關水 + 關刷,**任何 exit path 都會跑**
//
// 序列(非並行):每段 DM2J 動完才換 arm 動,反之亦然。確保不會跨 thread race。
// RAII cleanup 保證即使 try_or_pause_ abort 中,水也不會繼續流。
std::string WashRobot::cmd_arm_clean_sweep(int wall_mm, int rounds) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    if (wall_mm <= 0)                return "ERR invalid_wall_mm (>0)\n";
    if (rounds  <= 0 || rounds > 20) return "ERR invalid_rounds (1..20)\n";

    std::lock_guard<std::mutex> lk(motion_mtx_);
    abort_flag = false;
    // [2026-05-29] Reset arm sweep obstacle/skip flags — each user-initiated
    // command starts fresh (skip scope = within this command only).
    arm_sweep_obstacle_pending_.store(false);
    arm_sweep_skip_rest_of_run_.store(false);
    // [2026-05-28] Set motion_active_=true so pressure_poll_loop_ skips JC100
    // reads on cli_22_ during sweep (sweep uses cli_22_ heavily — DM2J:14
    // motion + arm STATUS via arm_cli_ + PQW relay + XKC). Otherwise JC100
    // reads race the bus and time out, flooding log with JC100:N TIMEOUT.
    motion_active_ = true;
    std::string r = do_arm_clean_sweep_(wall_mm, rounds);
    motion_active_ = false;
    return r;
}

// Internal cleaning sweep — caller MUST already hold motion_mtx_ (used by
// cmd_arm_clean_sweep and by do_step_up_ / do_step_down_ end-of-step). Does no
// state check / no lock / no abort_flag reset — the caller owns those.
std::string WashRobot::do_arm_clean_sweep_(int wall_mm, int rounds) {
    // [2026-05-27] arm_attached_=off: 整輪 sweep 跳過（含上滑台、水、刷）。
    // arm_cmd_ 本身已會 skip，但 slide motion + water/brush relay 是獨立流程，
    // 需在入口統一短路，否則 arm off 時上滑台還是會掃 + 水會出來。
    if (!arm_attached_.load()) {
        std::cout << "[arm_clean_sweep] SKIPPED (arm_attached=off)\n";
        return "OK skipped_arm_off\n";
    }
    // [2026-05-28] Symmetric with do_arm_clean_sweep_continuous_: honor
    // arm_sweep_skip_rest_of_run_ flag (user chose Skip on a previous obstacle).
    if (arm_sweep_skip_rest_of_run_.load()) {
        std::cout << "[arm_clean_sweep] SKIPPED (arm_sweep_skip_rest_of_run_=true from prior obstacle)\n";
        return "OK skipped_arm_obstacle\n";
    }
    // [2026-06-03] Mark sweep active so cycle_group_ rescue waits for us
    // before doing rail backup motion (avoids bus contention + ZDT stall
    // flag latching). Cleared in cleanup RAII guard below.
    arm_sweep_active_.store(true);
    // RAII cleanup — 無論成功 / abort / exception,離開時一定 PARK + 關水 + 關刷
    auto cleanup = [this]() {
        std::cout << "[arm_clean_sweep] cleanup: PARK + water/brush OFF\n";
        // [2026-05-29] PARK timeout 30s → 10s (motor_api 沒回覆時 cleanup 快速放棄)。
        std::string r = arm_cmd_("PARK", 10);
        // [arm rope protect TEMP 2026-05-21] clean_sweep cleanup leaves arm Parked
        if (r.rfind("OK", 0) == 0) arm_stow_state_.store(ArmStowState::Parked);
        pqw_.controlRelay(CH_BRUSH,       false);
        pqw_.controlRelay(CH_WATER_PUMP,  false);
        set_water_inlet_(false);   // [2026-06-05] → crane PQW (.34 slave 12 CH4)
        // [2026-06-03] Clear active flag — rescue path can now proceed.
        arm_sweep_active_.store(false);
        // [2026-06-06] End-of-sweep background refill — top up water for next
        // sweep cycle while caller continues. Detached thread polls XKC, opens
        // inlet if not full, closes with 5s delay after full (or timeout).
        // Guard against multiple concurrent refill threads (every additional
        // thread doubles XKC poll rate + races on water_inlet open/close).
        if (end_refill_active_.exchange(true)) {
            std::cout << "[arm_clean_sweep_end_refill] another refill thread already"
                         " active — skip spawning\n";
        } else {
        std::thread([this]() {
            uint16_t out = 0, rssi = 0;
            if (lvl_.read_state(out, rssi)) {
                std::cerr << "[arm_clean_sweep_end_refill] XKC unreachable — skip\n";
                end_refill_active_.store(false);
                return;
            }
            if (out == 1) {
                std::cout << "[arm_clean_sweep_end_refill] water already full (rssi="
                          << rssi << ") — skip refill\n";
                end_refill_active_.store(false);
                return;
            }
            std::cout << "[arm_clean_sweep_end_refill] not full (rssi=" << rssi
                      << ") — opening inlet (background)\n";
            if (set_water_inlet_(true)) {
                std::cerr << "[arm_clean_sweep_end_refill] open valve failed\n";
                end_refill_active_.store(false);
                return;
            }
            int elapsed = 0;
            bool full = false;
            int last_log = 0;
            const int poll_ms    = WATER_POLL_INTERVAL_MS;   // ODR fix: copy to local
            const int timeout_ms = WATER_FILL_TIMEOUT_MS;
            while (elapsed < timeout_ms) {
                sleep_ms_(poll_ms);
                elapsed += poll_ms;
                if (!lvl_.read_state(out, rssi) && out == 1) { full = true; break; }
                if (elapsed - last_log >= 30000) {
                    std::cout << "[arm_clean_sweep_end_refill] filling... elapsed="
                              << (elapsed / 1000) << "s rssi=" << rssi << "\n";
                    last_log = elapsed;
                }
            }
            if (full) {
                std::cout << "[arm_clean_sweep_end_refill] water full (rssi=" << rssi
                          << ") — close inlet in 5s\n";
                sleep_ms_(5000);
            } else {
                std::cerr << "[arm_clean_sweep_end_refill] REAL timeout — close now\n";
            }
            set_water_inlet_(false);
            std::cout << "[arm_clean_sweep_end_refill] done\n";
            end_refill_active_.store(false);
        }).detach();
        }
    };
    struct ScopeExit {
        std::function<void()> fn;
        ~ScopeExit() { if (fn) fn(); }
    } guard{cleanup};

    std::cout << "[arm_clean_sweep] start wall_mm=" << wall_mm
              << " rounds=" << rounds << "\n";

    // ---------- Phase A + B in PARALLEL ----------
    // Water refill (cli_22_) and arm INIT (arm_cli_) target physically
    // independent subsystems with separate TCP clients — safe to run concurrently.
    // Wall-time savings ≈ min(Phase A duration, Phase B duration). With a full
    // 60s water timeout + 10s INIT, sequential = 70s, parallel = 60s. Most cases
    // (water already full ~0s, INIT ~10s) save ~10s.
    //
    // Design (per user 2026-05-21):
    //   1) Launch arm INIT in background via std::async
    //   2) Run Phase A water fill in foreground with normal try_or_pause_ semantics
    //   3) After Phase A done, fut_init.get() to wait for arm INIT result
    //   4) If background INIT failed, sync retry via try_or_pause_ (single-threaded
    //      now → standard pause semantics work)
    //   5) Abort path: AsyncJoin guard waits for background thread before return
    //      (may stall up to 60s — accepted trade-off vs orphan thread risk)
    // [2026-05-28] INIT moved to cmd_init_impl_. Sweep now uses ensure_arm_ready_()
    // which only ENABLE the motors (PARK disabled them). Re-using async-launched
    // pattern + future joining for symmetry with previous flow / minimal disruption,
    // but ensure_arm_ready_() is ~50ms (just ENABLE) vs old INIT ~10s.
    auto fut_init = std::async(std::launch::async, [this]() -> bool {
        return ensure_arm_ready_();   // true = error (refused / motor enable fail)
    });
    // RAII guard: ensures the background thread joins on every exit path,
    // including PausedOnError → Abort. valid() check skips if get() already called.
    struct AsyncJoin {
        std::future<bool>& f;
        ~AsyncJoin() { if (f.valid()) f.wait(); }
    } _join_guard{fut_init};

    // ---------- Phase A: water level check + refill (foreground) ----------
    // Closure semantics for try_or_pause_:
    //   return false = "water is full, ready to proceed"
    //   return true  = sensor offline OR refill timeout → PausedOnError (user
    //                  can manually fill + Skip, or fix sensor + Continue retry)
    // Always closes the inlet valve before returning, regardless of path.
    if (try_or_pause_([this]() -> bool {
        uint16_t out = 0, rssi = 0;
        // [2026-06-06] Retry XKC 3 次再放棄 — 對齊 do_arm_clean_sweep_continuous_
        // (2026-05-22 已套用同 pattern)。cli_22_ bus 有 JC100/PQW 競爭，單次 XKC
        // read 偶有 "rx short: got 0 bytes" hiccup；之前一次失敗就 PausedOnError
        // 太敏感（觀察 2026-06-06 同次 sweep 後即時連續觸發）。
        bool xkc_ok = false;
        for (int i = 0; i < 3; ++i) {
            if (!lvl_.read_state(out, rssi)) { xkc_ok = true; break; }
            if (i < 2) {
                std::cerr << "[arm_clean_sweep] XKC read attempt " << (i + 1)
                          << "/3 fail — retry in 100ms\n";
                sleep_ms_(100);
            }
        }
        if (!xkc_ok) {
            std::cerr << "[arm_clean_sweep] XKC water sensor unreachable (3 retries)\n";
            return true;
        }
        if (out == 1) {
            std::cout << "[arm_clean_sweep] water already full (rssi=" << rssi
                      << ") — skip refill\n";
            return false;
        }
        std::cout << "[arm_clean_sweep] water not full (out=" << out
                  << " rssi=" << rssi << ") — opening inlet valve\n";
        if (set_water_inlet_(true)) return true;   // [2026-06-05] → crane PQW

        int elapsed = 0;
        bool full = false;
        int last_log_elapsed = 0;       // [2026-06-03] 進度 log 節流
        while (elapsed < WATER_FILL_TIMEOUT_MS) {
            sleep_ms_(WATER_POLL_INTERVAL_MS);
            elapsed += WATER_POLL_INTERVAL_MS;
            if (abort_flag.load()) break;
            if (!lvl_.read_state(out, rssi) && out == 1) { full = true; break; }
            // [2026-06-03] 每 30 秒印一次進度，方便 bench 觀察填水速度
            if (elapsed - last_log_elapsed >= 30000) {
                std::cout << "[arm_clean_sweep] filling... elapsed=" << (elapsed / 1000)
                          << "s rssi=" << rssi << " (timeout at "
                          << (WATER_FILL_TIMEOUT_MS / 1000) << "s)\n";
                last_log_elapsed = elapsed;
            }
        }
        // [2026-06-05] 水滿時 delay 5s 才關 valve（per user 要求 — 讓水稍微過滿
        // 確保 sensor 穩定）。Spawn detached thread sleep 5s + close，主流程立刻
        // 返回繼續 Phase B/C。RAII ScopeExit 結束時也會 close（idempotent，valve
        // 已關不影響）。timeout / abort 則立刻 close（anti-flood）。
        if (!full) {
            set_water_inlet_(false);   // immediate close on timeout / abort
            std::cerr << "[arm_clean_sweep] water fill timeout / aborted (out=" << out
                      << " rssi=" << rssi << " elapsed=" << elapsed << "ms)\n";
            return true;
        }
        std::cout << "[arm_clean_sweep] water full (rssi=" << rssi
                  << ") — will close inlet in 5s (sweep continues)\n";
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            set_water_inlet_(false);
            std::cout << "[arm_clean_sweep] water_inlet closed (5s delay after full)\n";
        }).detach();
        return false;
    }, "clean_water_fill")) return "ERR aborted\n";

    // ---------- Phase B: collect parallel arm INIT result ----------
    // Background INIT (launched before Phase A) is most likely already done by now
    // (Phase A typically ≥ 10s). fut_init.get() blocks if INIT is still running.
    // Per-clean_sweep INIT guarantees a known starting pose (M1=0 / M2 CENTER /
    // both calibrated, ~10s cost).
    bool init_err = fut_init.get();   // consumes the future → AsyncJoin no-ops
    if (init_err) {
        std::cerr << "[arm_clean_sweep] arm not ready (calibration missing or ENABLE failed) — sync retry\n";
        // Single-threaded retry path: standard try_or_pause_ semantics now safe.
        if (try_or_pause_([this]() { return ensure_arm_ready_(); },
                          "clean_arm_ready")) return "ERR aborted\n";
    }

    // ---------- Phase C: rounds × (LEFT 滾筒+水 → RIGHT 刮刀乾) ----------
    // Per user flow 2026-05-20: each sub-round =
    //   1) arm DEPLOY <wall_mm> <slot>      (M1 retract → M2 LR_SLOT → M1 TOUCHWALL)
    //   2) water pump + brush ON/OFF        (LEFT = ON / RIGHT = OFF)
    //   3) 上滑台 sweep — see target_cm
    // Pump and brush track each other — no point spinning the roller motor when
    // the scraper is engaged. RIGHT round runs dry to wipe residual water.
    //
    // [2026-05-28] Bidirectional sweep (do single sweep per sub-round, no return).
    // Pattern per round:
    //   LEFT  (roller) : slide 0 → ARM_SWEEP_CM  (wet going RIGHT)
    //   RIGHT (scraper): slide ARM_SWEEP_CM → 0  (wipe going LEFT)
    // Eliminates the wasted "return to 0" sweep after each sub-round (was ~7s each).
    // Assumes slide starts at 0 at round entry (true after init / previous round).
    auto sweep_with_tool = [&](const char* m2_slot, bool water_on,
                                const char* tag_prefix, double target_cm,
                                bool skip_deploy = false) -> bool {
        // [2026-06-05] skip_deploy=true: 跳過 DEPLOY + pqw on/off + verify，只 fire
        // slide motion。用於連續 sub-stroke 同 slot+water 切換（例如 LEFT 0→80→0→80
        // 中間兩段）省 ~2× DEPLOY 時間。
        if (!skip_deploy) {
            // 1) Pre-DEPLOY: if transitioning to a DRY round, turn pump+brush OFF
            //    BEFORE DEPLOY initiates M1 retract. Otherwise water sprays into air
            //    and roller spins dry while arm lifts off wall to park position.
            //    For WET rounds we leave the toggle until AFTER wall contact (step 3)
            //    so water only flows when the roller is actually pressed against glass.
            if (!water_on) {
                if (try_or_pause_([this]() {
                                      return pqw_set_relay_verified_(CH_WATER_PUMP, false);
                                  },
                                  std::string("clean_pump_off_pre_") + tag_prefix)) return false;
                if (try_or_pause_([this]() {
                                      return pqw_set_relay_verified_(CH_BRUSH, false);
                                  },
                                  std::string("clean_brush_off_pre_") + tag_prefix)) return false;
                // [2026-06-06] Sleep 500ms before DEPLOY 換刮刀。理由：
                //   (1) pqw_set_relay_verified_ best-effort 可能 verify 假通過
                //       (cli_22_ bus 擁塞 + Modbus TCP stale frame buffer)，
                //       sleep 期間真正的 OFF cmd 也已經到 PQW board、relay 切完。
                //   (2) Pump 馬達 + 水管殘水的物理 inertia ~ 100s ms，sleep 確保
                //       M2 轉到刮刀位置前水流停掉、刮刀不會碰到還在噴的水。
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            // 2) DEPLOY arm to wall at the requested tool slot
            //    (M1 retract→0 → M2 LR_SLOT switch → M1 TOUCHWALL)
            std::ostringstream oss;
            oss << "DEPLOY " << wall_mm << " " << m2_slot;
            const std::string deploy_cmd = oss.str();
            const std::string slot_str(m2_slot);   // for verify_arm_deploy_
            // [arm rope protect TEMP 2026-05-21] DEPLOY + STATUS verify in one
            // try_or_pause_ context: if motor_api says OK but M1 actually stopped
            // short (obstacle), treat as failure → PausedOnError → user clears
            // obstacle + Continue retries the whole DEPLOY+verify.
            if (try_or_pause_([this, deploy_cmd, &slot_str, wall_mm]() -> bool {
                                  // [2026-06-06] DEPLOY + auto-retry on M2 slot mismatch.
                                  // [2026-06-09h] Also retry on arm_cmd_ no_reply (M2 motor 進水
                                  // intermittent fail) — PARK + 500ms settle between attempts.
                                  // motor_api lr_move_to_slot 偶爾因 M2 passive (tau≈0)
                                  // 完全不動 → arm_cmd_ 回非 OK → 之前直接 fail；現在 retry。
                                  const int MAX = ARM_M2_VERIFY_RETRIES + 1;
                                  std::string last_fail;
                                  for (int attempt = 1; attempt <= MAX; ++attempt) {
                                      bool cmd_ok = (arm_cmd_(deploy_cmd, 60).rfind("OK", 0) == 0);
                                      if (cmd_ok) {
                                          if (verify_arm_deploy_(slot_str, wall_mm)) return true;   // M1 obstacle path
                                          sleep_ms_(150);
                                          if (!verify_arm_m2_at_slot_(slot_str)) return false;   // OK
                                          last_fail = "M2 off-target";
                                      } else {
                                          last_fail = "no_reply (M2 motor fail or motor_api busy)";
                                      }
                                      if (attempt < MAX) {
                                          std::cerr << "[arm_m2_verify] DEPLOY " << slot_str
                                                    << " attempt " << attempt << "/" << MAX
                                                    << " — " << last_fail << " — retrying\n";
                                          arm_cmd_("PARK", 10);
                                          sleep_ms_(500);
                                      }
                                  }
                                  std::cerr << "[arm_m2_verify] DEPLOY " << slot_str
                                            << " gave up after " << MAX
                                            << " attempts (last: " << last_fail << ")\n";
                                  evt_("arm_m2_verify_fail slot=" + slot_str + " reason=" + last_fail);
                                  return false;   // best-effort, don't block sweep
                              },
                              std::string("clean_arm_deploy_") + tag_prefix)) return false;

            // 3) Post-DEPLOY: if WET round, turn pump+brush ON now that roller is
            //    pressed against wall (water lands where it should, not in air).
            if (water_on) {
                if (try_or_pause_([this]() {
                                      return pqw_set_relay_verified_(CH_WATER_PUMP, true);
                                  },
                                  std::string("clean_pump_on_post_") + tag_prefix)) return false;
                if (try_or_pause_([this]() {
                                      return pqw_set_relay_verified_(CH_BRUSH, true);
                                  },
                                  std::string("clean_brush_on_post_") + tag_prefix)) return false;
            }
        }   // end !skip_deploy

        // [2026-05-28] Single sweep to target_cm (absolute). Caller decides
        // direction by passing 0 or ARM_SWEEP_CM depending on which slot.
        // Wrapped in retry loop: if monitor detects obstacle mid-sweep, slide
        // stops at intermediate position. Ask user (Retry/Skip/Abort):
        //   - Retry : re-fire sweep to same target → slide continues from
        //             stopped position to original target (absolute PR_move)
        //   - Skip  : abandon this and future sweeps in this run
        //   - Abort : sub-round fails → arm_clean_sweep_ returns ERR
        while (true) {
            arm_sweep_fire_nowait_(target_cm);   // fires + monitors
            if (!arm_sweep_obstacle_pending_.load()) break;   // no obstacle, sweep done

            // Obstacle: pause + ask user (slide already stopped by monitor's signal_obstacle)
            std::string detail;
            { std::lock_guard<std::mutex> lk(arm_sweep_obstacle_mtx_); detail = arm_sweep_obstacle_detail_; }
            PauseAction a = await_user_intervention_("arm_sweep_obstacle " + detail);
            arm_sweep_obstacle_pending_.store(false);
            if (a == PauseAction::Abort) return false;
            if (a == PauseAction::Skip) {
                arm_sweep_skip_rest_of_run_.store(true);
                std::cout << "[arm_clean_sweep] Skip → 立即 PARK + skip remaining sweeps\n";
                // [2026-05-28] Per user: Skip 後直接 PARK，不要繼續任何 sweep ops。
                // ScopeExit cleanup 仍會 fire PARK 一次（idempotent，motor_api 對已 disable 馬達快速 no-op）。
                // 關水/刷也 explicit 做掉，免得等 ScopeExit。
                pqw_.controlRelay(CH_BRUSH,      false);
                pqw_.controlRelay(CH_WATER_PUMP, false);
                set_water_inlet_(false);   // [2026-06-05] → crane PQW (.34 slave 12 CH4)
                arm_cmd_("PARK", 30);
                arm_stow_state_.store(ArmStowState::Parked);
                return true;   // sub-round 視為完成
            }
            // Retry: loop back, re-fire to same target_cm
            std::cout << "[arm_clean_sweep] Retry → re-fire sweep to " << target_cm << " cm\n";
        }
        return true;
    };

    // [2026-05-28] sweep_with_tool now handles obstacle pause INLINE (retry
    // loop with Retry/Skip/Abort). The previous post-sweep check_arm_obstacle_
    // pause helper is obsolete. Outer loop only needs to honor the Skip choice:
    // if user picked Skip during any sub-round, break the round loop so the
    // remaining sub-rounds (and rounds) are skipped.
    for (int r = 0; r < rounds; ++r) {
        std::cout << "[arm_clean_sweep] round " << (r + 1) << "/" << rounds
                  << " — LEFT(滾筒+水) 0→" << ARM_SWEEP_CM
                  << " → LEFT " << ARM_SWEEP_CM << "→0"
                  << " → RIGHT(刮刀乾) 0→" << ARM_SWEEP_CM
                  << " → RIGHT " << ARM_SWEEP_CM << "→0\n";
        if (check_abort_()) return "ERR aborted\n";
        // [2026-06-05] 每 round 4 個 sub-stroke：滾筒濕拖 ×2 + 刮刀乾掃 ×2。
        // 同 slot+water 連續切換時 skip_deploy=true 省 DEPLOY 時間。
        // 1: LEFT 0→80 (滾筒往右，首次 DEPLOY)
        if (!sweep_with_tool("LEFT",  true,  "roller-1",   (double)ARM_SWEEP_CM, false))  return "ERR aborted\n";
        if (arm_sweep_skip_rest_of_run_.load()) {
            std::cout << "[arm_clean_sweep] skip_rest_of_run_=true → break round loop\n";
            break;
        }
        if (check_abort_()) return "ERR aborted\n";
        // 2: LEFT 80→0 (滾筒往左，skip_deploy 同 slot+water)
        if (!sweep_with_tool("LEFT",  true,  "roller-2",   0.0,                  true))   return "ERR aborted\n";
        if (arm_sweep_skip_rest_of_run_.load()) break;
        if (check_abort_()) return "ERR aborted\n";
        // 3: RIGHT 0→80 (換刮刀往右，DEPLOY + 關水/刷)
        if (!sweep_with_tool("RIGHT", false, "scraper-1",  (double)ARM_SWEEP_CM, false)) return "ERR aborted\n";
        if (arm_sweep_skip_rest_of_run_.load()) break;
        if (check_abort_()) return "ERR aborted\n";
        // 4: RIGHT 80→0 (刮刀往左，skip_deploy 同 slot+water)
        if (!sweep_with_tool("RIGHT", false, "scraper-2",  0.0,                  true))  return "ERR aborted\n";
        if (arm_sweep_skip_rest_of_run_.load()) break;
    }

    std::cout << "[arm_clean_sweep] all rounds done\n";
    // Phase D 在 ScopeExit 自動執行(PARK + 關水/刷)
    return "OK arm_clean_sweep_done\n";
}

// ============================================================
// Continuous cleaning sweep — runs LEFT/RIGHT rounds in a loop until
// keep_going flips to false (used by cmd_step_up_with_sweep background
// thread, 2026-05-22). Does NOT take motion_mtx_ — coexists with main
// motion thread (step_up) by using independent devices (arm_cli_, cli_22_
// PQW water/XKC). Bus contention with main thread's cli_22_ reads is
// serialized through TCP_client mutex (latency only, no corruption).
//
// Error policy (per user 2026-05-22): on internal failure (DEPLOY obstacle /
// relay write fail / etc.), log + return ERR + cleanup. Does NOT call
// try_or_pause_ — would race with main thread's state_ / PausedOnError.
// ============================================================
std::string WashRobot::do_arm_clean_sweep_continuous_(int wall_mm,
                                                       std::atomic<bool>& keep_going,
                                                       int max_rounds) {
    // [2026-05-27] arm_attached_=off: 整輪 sweep 跳過（含上滑台、水、刷）。
    // 跟 do_arm_clean_sweep_ 同步：避免 arm off 時背景 thread 還在跑 slide motion。
    if (!arm_attached_.load()) {
        std::cout << "[arm_clean_sweep_cont] SKIPPED (arm_attached=off)\n";
        return "OK skipped_arm_off\n";
    }
    // [2026-05-28] User chose "Skip future sweeps" on a previous obstacle in
    // this run → bypass all subsequent sweeps until cmd_run starts a new run
    // (which clears the flag).
    if (arm_sweep_skip_rest_of_run_.load()) {
        std::cout << "[arm_clean_sweep_cont] SKIPPED (arm_sweep_skip_rest_of_run_=true from prior obstacle)\n";
        return "OK skipped_arm_obstacle\n";
    }
    // [2026-06-03] Mark sweep active so cycle_group_ rescue waits for us
    // before doing rail backup motion (avoids bus contention + ZDT stall
    // flag latching). Cleared in cleanup RAII guard below.
    arm_sweep_active_.store(true);
    // RAII cleanup — 跟 do_arm_clean_sweep_ 一致：PARK + 關水 + 關刷
    auto cleanup = [this]() {
        std::cout << "[arm_clean_sweep_cont] cleanup: PARK + water/brush OFF (parallel)\n";
        // [2026-05-29] PQW OFF 3 個 channel 跟 arm_cmd PARK 並行
        // 不同通道 (cli_22_ PQW vs motor_api TCP) → 真正並行。
        // 省掉 sweep 結束跟 body_pre_cycle vacuum_wait_release_ 之間的 cli_22_
        // 競爭時間段 (cleanup PQW 寫早早結束,不會跟 body 釋放讀同時)。
        auto fut_pqw = std::async(std::launch::async, [this]() {
            pqw_.controlRelay(CH_BRUSH,       false);
            pqw_.controlRelay(CH_WATER_PUMP,  false);
            set_water_inlet_(false);   // [2026-06-05] → crane PQW (.34 slave 12 CH4)
        });
        // [2026-05-29] PARK timeout 30s → 10s (fast fail when motor_api 沒回覆,
        // 避免 cleanup 卡 30s × 2 attempts = 60s)。
        std::string r = arm_cmd_("PARK", 10);
        if (r.rfind("OK", 0) == 0) {
            arm_stow_state_.store(ArmStowState::Parked);
        } else if (!arm_sweep_obstacle_pending_.load()) {
            // PARK 也沒回覆 → 跟 sweep 期間 DEPLOY no_reply 同樣處理：
            // 設 flag 讓 main thread pause + 問 user 要不要收回 slide。
            // 只有在 obstacle_pending_ 還沒被別處設過時才設,避免覆蓋更早的原因。
            {
                std::lock_guard<std::mutex> lk(arm_sweep_obstacle_mtx_);
                arm_sweep_obstacle_detail_ = "arm_park_no_reply";
            }
            arm_sweep_obstacle_pending_.store(true);
            evt_("arm_park_no_reply");
        }
        // Wait for parallel PQW OFF to complete before returning (RAII guarantee).
        fut_pqw.get();
        // [2026-06-03] Clear active flag — rescue path can now proceed.
        arm_sweep_active_.store(false);
        // [2026-06-06] End-of-sweep background refill — same as do_arm_clean_sweep_
        // cleanup. Detached thread polls XKC, opens inlet if not full, closes with
        // 5s delay after full (or timeout immediate close). Other flows continue.
        // Guard against multiple concurrent refill threads (see do_arm_clean_sweep_
        // version for full rationale).
        if (end_refill_active_.exchange(true)) {
            std::cout << "[arm_clean_sweep_cont_end_refill] another refill thread already"
                         " active — skip spawning\n";
        } else {
        std::thread([this]() {
            uint16_t out = 0, rssi = 0;
            if (lvl_.read_state(out, rssi)) {
                std::cerr << "[arm_clean_sweep_cont_end_refill] XKC unreachable — skip\n";
                end_refill_active_.store(false);
                return;
            }
            if (out == 1) {
                std::cout << "[arm_clean_sweep_cont_end_refill] water already full (rssi="
                          << rssi << ") — skip refill\n";
                end_refill_active_.store(false);
                return;
            }
            std::cout << "[arm_clean_sweep_cont_end_refill] not full (rssi=" << rssi
                      << ") — opening inlet (background)\n";
            if (set_water_inlet_(true)) {
                std::cerr << "[arm_clean_sweep_cont_end_refill] open valve failed\n";
                end_refill_active_.store(false);
                return;
            }
            int elapsed = 0;
            bool full = false;
            int last_log = 0;
            const int poll_ms    = WATER_POLL_INTERVAL_MS;   // ODR fix: copy to local
            const int timeout_ms = WATER_FILL_TIMEOUT_MS;
            while (elapsed < timeout_ms) {
                sleep_ms_(poll_ms);
                elapsed += poll_ms;
                if (!lvl_.read_state(out, rssi) && out == 1) { full = true; break; }
                if (elapsed - last_log >= 30000) {
                    std::cout << "[arm_clean_sweep_cont_end_refill] filling... elapsed="
                              << (elapsed / 1000) << "s rssi=" << rssi << "\n";
                    last_log = elapsed;
                }
            }
            if (full) {
                std::cout << "[arm_clean_sweep_cont_end_refill] water full (rssi=" << rssi
                          << ") — close inlet in 5s\n";
                sleep_ms_(5000);
            } else {
                std::cerr << "[arm_clean_sweep_cont_end_refill] REAL timeout — close now\n";
            }
            set_water_inlet_(false);
            std::cout << "[arm_clean_sweep_cont_end_refill] done\n";
            end_refill_active_.store(false);
        }).detach();
        }
    };
    struct ScopeExit {
        std::function<void()> fn;
        ~ScopeExit() { if (fn) fn(); }
    } guard{cleanup};

    std::cout << "[arm_clean_sweep_cont] start wall_mm=" << wall_mm
              << " (continuous mode, keep_going-controlled)\n";

    // ---------- Phase A + B in PARALLEL (same as do_arm_clean_sweep_) ----------
    // [2026-05-28] INIT moved to cmd_init_impl_. Sweep now just ENABLEs the
    // motors via ensure_arm_ready_() (PARK disabled them after previous sweep).
    auto fut_init = std::async(std::launch::async, [this]() -> bool {
        return ensure_arm_ready_();
    });
    struct AsyncJoin {
        std::future<bool>& f;
        ~AsyncJoin() { if (f.valid()) f.wait(); }
    } _join_guard{fut_init};

    // Phase A: water fill (inline, no try_or_pause_ — sweep errors stay quiet)
    {
        uint16_t out = 0, rssi = 0;
        // 2026-05-22: 連續 sweep 平行模式 user 介入機會少。XKC 讀第一次失敗很可能
        // 是 cli_22_ bus 瞬間 contention（同 bus 有 JC100/PQW），retry 3 次再放棄。
        bool xkc_ok = false;
        for (int i = 0; i < 3; ++i) {
            if (!lvl_.read_state(out, rssi)) { xkc_ok = true; break; }
            if (i < 2) {
                std::cerr << "[arm_clean_sweep_cont] XKC read attempt " << (i + 1)
                          << "/3 fail — retry in 100ms\n";
                sleep_ms_(100);
            }
        }
        if (!xkc_ok) {
            std::cerr << "[arm_clean_sweep_cont] XKC sensor unreachable (3 retries) — abort sweep\n";
            return "ERR xkc_offline\n";
        }
        if (out == 1) {
            std::cout << "[arm_clean_sweep_cont] water already full (rssi=" << rssi
                      << ") — skip refill\n";
        } else {
            std::cout << "[arm_clean_sweep_cont] water not full (out=" << out
                      << " rssi=" << rssi << ") — opening inlet valve\n";
            if (set_water_inlet_(true)) {   // [2026-06-05] → crane PQW
                return "ERR water_inlet_open_fail\n";
            }
            int elapsed = 0;
            bool full = false;
            int last_log_elapsed = 0;       // [2026-06-03] 進度 log 節流
            // [2026-06-03] water-fill phase 不檢查 keep_going。
            // 原本有 `if (!keep_going.load()) break;` 但 parent step_down 結束
            // 時 SweepJoin destructor 跟顯式 sweep_keep_going.store(false) 會
            // 在水填到滿前殺掉這個 loop → 印出誤導的「water fill timeout」
            // 訊息（實際只跑了 15 秒，遠不到 180s timeout）。
            // 移掉這個 check 讓水填完才繼續，sweep round 內部還有 keep_going
            // check 可以中斷 → emergency_stop 仍能在 round 階段生效。
            while (elapsed < WATER_FILL_TIMEOUT_MS) {
                sleep_ms_(WATER_POLL_INTERVAL_MS);
                elapsed += WATER_POLL_INTERVAL_MS;
                if (!lvl_.read_state(out, rssi) && out == 1) { full = true; break; }
                // [2026-06-03] 每 30 秒印一次進度，方便 bench 觀察填水速度
                if (elapsed - last_log_elapsed >= 30000) {
                    std::cout << "[arm_clean_sweep_cont] filling... elapsed=" << (elapsed / 1000)
                              << "s rssi=" << rssi << " (timeout at "
                              << (WATER_FILL_TIMEOUT_MS / 1000) << "s)\n";
                    last_log_elapsed = elapsed;
                }
            }
            // [2026-06-05] 水滿 → delay 5s 才關 valve（per user 要求）。Spawn
            // detached thread；主流程立刻 return 繼續 sweep round。timeout / abort
            // 則立刻 close。RAII cleanup 結束時也會 close（idempotent）。
            if (!full) {
                set_water_inlet_(false);   // immediate close on real timeout
                std::cerr << "[arm_clean_sweep_cont] water fill REAL timeout — "
                          << (WATER_FILL_TIMEOUT_MS / 1000) << "s 內水沒填滿 (rssi="
                          << rssi << "), abort sweep\n";
                return "ERR water_fill_timeout\n";
            }
            std::cout << "[arm_clean_sweep_cont] water full (rssi=" << rssi
                      << ") — will close inlet in 5s (sweep continues)\n";
            std::thread([this]() {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                set_water_inlet_(false);
                std::cout << "[arm_clean_sweep_cont] water_inlet closed (5s delay after full)\n";
            }).detach();
        }
    }

    // Phase B: collect parallel arm ready (ENABLE) result
    bool init_err = fut_init.get();
    if (init_err) {
        std::cerr << "[arm_clean_sweep_cont] arm not ready (calibration missing or ENABLE failed) — abort sweep\n";
        return "ERR arm_not_ready\n";
    }

    // ---------- Phase C: 連續 LOOP（LEFT 滾筒 → RIGHT 刮刀）until keep_going=false ----------
    // 每個 sub-round 內部不檢查 keep_going（避免半 round 停在牆上）。
    // round 之間（LEFT 跟 RIGHT 之間、RIGHT 結束之後）才檢查。
    // [2026-05-28] Bidirectional sweep (single sweep per sub-round):
    //   LEFT  (roller) : slide 0 → ARM_SWEEP_CM  (wet going RIGHT)
    //   RIGHT (scraper): slide ARM_SWEEP_CM → 0  (wipe going LEFT, returns to 0)
    // Eliminates the wasted "return to 0" sweep that doubled each sub-round time.
    // Saves ~ARM_SWEEP_EST_MS × 2 (sub-rounds) per round.
    auto sweep_with_tool = [&](const char* m2_slot, bool water_on,
                                const char* tag_prefix, double target_cm,
                                bool skip_deploy = false) -> bool {
        // 2026-05-28: 移動上滑台前先檢查 DM2J:14 alarm。失步 / encoder fault / 過流
        // 等都會 latch 在 0x2203,直到 reset_alarm 才清。fault 時跳過此 round —
        // 不 DEPLOY、不 sweep、不啟刷子,直接 return false 結束本 slot。
        // (alarm 不會自己清,user 必須手動 reset:重新 init、或 Linux_test menu)
        // alarm check 在 skip_deploy 模式也跑（廉價的安全 check）。
        {
            uint32_t st = 0;
            if (!D_(DM2J_ARM).read_status(st) && (st & 0x0001)) {
                std::cerr << "[arm_clean_sweep_cont] DM2J:14 alarm set (status=0x"
                          << std::hex << st << std::dec
                          << ") — skip sweep round (" << m2_slot
                          << "), reset needed to resume\n";
                return false;
            }
            // status read fail(可能 bus contention)→ fall through、試 fire,寧可
            // 嘗試也不要因 transient read miss 永久跳過。
        }
        // [2026-06-05] skip_deploy=true 用於連續 sub-stroke 同 slot+water 切換時
        // 省下 DEPLOY/verify/pqw 切換的開銷。略過下面的 pqw 切換 + DEPLOY + verify，
        // 直接跳到 slide motion。
        if (!skip_deploy) {
            // [2026-06-03] Pre-DEPLOY pqw OFF for dry round — SYNCHRONOUS (was async).
            if (!water_on) {
                if (pqw_set_relay_verified_(CH_WATER_PUMP, false)) {
                    std::cerr << "[arm_clean_sweep_cont] pqw OFF water_pump FAIL\n";
                    return false;
                }
                if (pqw_set_relay_verified_(CH_BRUSH, false)) {
                    std::cerr << "[arm_clean_sweep_cont] pqw OFF brush FAIL\n";
                    return false;
                }
                // [2026-06-06] Sleep 500ms before DEPLOY — let pump motor + water
                // pipe inertia drain, and absorb potential verify phantom-success
                // from cli_22_ stale frame buffer. See do_arm_clean_sweep_ for full
                // rationale.
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            // DEPLOY + verify (+ M2 retry — off-target since 2026-06-06、
            // no_reply since 2026-06-09).
            // 2026-06-09h: 將 no_reply (arm_cmd_ 非 OK，常見於 M2 馬達 lr_move_to_slot
            // FAIL — water damage intermittent) 也納入 retry，PARK + 500ms 重置 M2
            // state，多次嘗試讓 transient M2 fail 自動恢復而不打擾 user。
            std::ostringstream oss;
            oss << "DEPLOY " << wall_mm << " " << m2_slot;
            const std::string deploy_str = oss.str();
            const int MAX_DEPLOY_ATTEMPTS = ARM_M2_VERIFY_RETRIES + 1;
            bool deploy_ok = false;
            std::string last_fail;
            for (int attempt = 1; attempt <= MAX_DEPLOY_ATTEMPTS; ++attempt) {
                bool cmd_ok = (arm_cmd_(deploy_str, 60).rfind("OK", 0) == 0);
                if (cmd_ok) {
                    sleep_ms_(150);   // M2 settle margin
                    if (!verify_arm_m2_at_slot_(m2_slot)) {
                        deploy_ok = true;
                        break;
                    }
                    last_fail = "M2 off-target";
                } else {
                    last_fail = "no_reply (M2 motor fail or motor_api busy)";
                }
                if (attempt < MAX_DEPLOY_ATTEMPTS) {
                    std::cerr << "[arm_m2_verify] DEPLOY " << m2_slot
                              << " attempt " << attempt << "/" << MAX_DEPLOY_ATTEMPTS
                              << " — " << last_fail << " — retrying\n";
                    // PARK 釋放 M2 + 500ms 讓 passive 馬達 settle、re-init
                    arm_cmd_("PARK", 10);
                    sleep_ms_(500);
                } else {
                    std::cerr << "[arm_m2_verify] DEPLOY " << m2_slot
                              << " gave up after " << MAX_DEPLOY_ATTEMPTS
                              << " attempts (last: " << last_fail << ")\n";
                    evt_(std::string("arm_m2_verify_fail slot=") + m2_slot
                         + " reason=" + last_fail);
                }
            }
            if (!deploy_ok) {
                std::cerr << "[arm_clean_sweep_cont] DEPLOY " << m2_slot << " no_reply (timeout or motor_api busy)\n";
                {
                    std::lock_guard<std::mutex> lk(arm_sweep_obstacle_mtx_);
                    arm_sweep_obstacle_detail_ = std::string("arm_deploy_no_reply slot=") + m2_slot;
                }
                arm_sweep_obstacle_pending_.store(true);
                evt_(std::string("arm_deploy_no_reply slot=") + m2_slot);
                return false;
            }
            if (verify_arm_deploy_(m2_slot, wall_mm)) {
                std::cerr << "[arm_clean_sweep_cont] DEPLOY " << m2_slot << " obstacle\n";
                {
                    std::lock_guard<std::mutex> lk(arm_sweep_obstacle_mtx_);
                    arm_sweep_obstacle_detail_ = std::string("slot=") + m2_slot;
                }
                arm_sweep_obstacle_pending_.store(true);
                evt_(std::string("arm_sweep_obstacle slot=") + m2_slot);
                return false;
            }
            // [2026-06-03] Post-DEPLOY pqw ON for wet round — SYNCHRONOUS.
            if (water_on) {
                if (pqw_set_relay_verified_(CH_WATER_PUMP, true)) {
                    std::cerr << "[arm_clean_sweep_cont] pqw ON water_pump FAIL\n";
                    return false;
                }
                if (pqw_set_relay_verified_(CH_BRUSH, true)) {
                    std::cerr << "[arm_clean_sweep_cont] pqw ON brush FAIL\n";
                    return false;
                }
            }
        }   // end !skip_deploy
        // [2026-05-28] Single sweep to target_cm. fire-and-forget pattern unchanged
        // (avoid PR status poll contention with JC100/PQW on cli_22_).
        arm_sweep_fire_nowait_(target_cm);
        return true;
    };

    int round_cnt = 0;
    // 2026-05-25: 加 max_rounds 上限。0=unlimited（沿用 keep_going 控制）。
    // _sweep_after_feet 場景傳 1：sweep 跑完 1 round 自動結束、不等 keep_going。
    //
    // 2026-06-03 BUG FIX: 原本 `keep_going.load() && (max_rounds <= 0 || ...)`
    // 是 AND 條件，把 keep_going 跟 max_rounds 綁在一起 — 跟 2026-05-25 註解
    // 「不等 keep_going」矛盾。實際 bug：step_down 比水灌完快，主 thread 已經
    // 設 keep_going=false，背景 sweep 灌完水進 loop 時 keep_going 已 false →
    // 0 round 直接退出。改成「max_rounds>0 時 ignore keep_going」匹配原始意圖。
    while ((max_rounds > 0 && round_cnt < max_rounds) ||
           (max_rounds <= 0 && keep_going.load())) {
        round_cnt++;
        std::cout << "[arm_clean_sweep_cont] round " << round_cnt
                  << (max_rounds > 0 ? "/" + std::to_string(max_rounds) : std::string(""))
                  << " — LEFT(滾筒+水) 0→" << ARM_SWEEP_CM
                  << " → LEFT " << ARM_SWEEP_CM << "→0"
                  << " → RIGHT(刮刀乾) 0→" << ARM_SWEEP_CM
                  << " → RIGHT " << ARM_SWEEP_CM << "→0\n";
        // [2026-06-05] 每 round 4 個 sub-stroke：滾筒濕拖 ×2 + 刮刀乾掃 ×2。
        // 同 slot+water 連續切換時 skip_deploy=true 省 DEPLOY 時間。
        // 1: LEFT 0→80 (滾筒往右，首次 DEPLOY)
        if (!sweep_with_tool("LEFT",  true,  "roller-1",   (double)ARM_SWEEP_CM, false)) {
            std::cerr << "[arm_clean_sweep_cont] LEFT-1 round " << round_cnt << " failed — abort loop\n";
            return "ERR sweep_left_fail\n";
        }
        // 2: LEFT 80→0 (滾筒往左，skip_deploy 同 slot+water)
        if (!sweep_with_tool("LEFT",  true,  "roller-2",   0.0,                  true)) {
            std::cerr << "[arm_clean_sweep_cont] LEFT-2 round " << round_cnt << " failed — abort loop\n";
            return "ERR sweep_left_fail\n";
        }
        // 3: RIGHT 0→80 (換刮刀往右，DEPLOY + 關水/刷)
        if (!sweep_with_tool("RIGHT", false, "scraper-1",  (double)ARM_SWEEP_CM, false)) {
            std::cerr << "[arm_clean_sweep_cont] RIGHT-1 round " << round_cnt << " failed — abort loop\n";
            return "ERR sweep_right_fail\n";
        }
        // 4: RIGHT 80→0 (刮刀往左，skip_deploy 同 slot+water)
        if (!sweep_with_tool("RIGHT", false, "scraper-2",  0.0,                  true)) {
            std::cerr << "[arm_clean_sweep_cont] RIGHT-2 round " << round_cnt << " failed — abort loop\n";
            return "ERR sweep_right_fail\n";
        }
    }

    std::cout << "[arm_clean_sweep_cont] loop exit (keep_going="
              << (keep_going.load() ? "true" : "false")
              << " max_rounds=" << max_rounds
              << " completed=" << round_cnt << " rounds)\n";
    return "OK arm_clean_sweep_cont_done\n";
}

// Crane EVT line dispatcher. Called from crane_cmd_ when an EVT line is drained
// from the RPC channel. Records safety-critical alarms (tension_alarm /
// tension_total_limit) into atomic flag for watchdog to escalate to PausedOnError.
void WashRobot::handle_crane_evt_(const std::string& line) {
    std::cout << "[crane_evt] " << line << "\n";
    if (line.find("tension_alarm") != std::string::npos ||
        line.find("tension_total_limit") != std::string::npos) {
        // [2026-06-02 v7, per Sadie bench] Suppress tension_total_limit during
        // balance calibration. During cal (especially after Phase 2/3 cup
        // release) all robot weight transfers to ropes, easily pushing
        // total tension >50kg even at normal load. Letting this fire causes
        // crane_watchdog to escalate PausedOnError repeatedly mid-cal,
        // which corrupts the post-cal state machine. tension_alarm (per-side
        // peak) still fires — only the total-sum gate is suppressed.
        if (balance_cal_running_.load() &&
            line.find("tension_total_limit") != std::string::npos) {
            std::cout << "[crane_evt] suppressed (balance cal in progress): "
                      << line << "\n";
        } else {
            std::lock_guard<std::mutex> lk(crane_alarm_mtx_);
            if (line.find("tension_total_limit") != std::string::npos)
                crane_alarm_kind_ = "tension_total_limit";
            else
                crane_alarm_kind_ = "tension_alarm";
            crane_alarm_detail_ = line;
            crane_alarm_pending_.store(true);
        }
    }
    // motion_progress: crane is mid-op (long pay_out / retract / fine_adjust).
    // Refresh watchdog timestamp so 2s WATCHDOG_TIMEOUT_MS doesn't fire while
    // crane is legitimately busy. Without this, only OK replies refresh — and
    // OK only comes after the entire op finishes.
    if (line.find("motion_progress") != std::string::npos) {
        crane_last_ok_ms_ = now_ms_();
    }
    // Re-broadcast to GUI so operator sees the EVT in washrobot's own log channel
    evt_("crane_relay " + line);
}

// Read max rope tension (kg) — primary via crane DSZL-107, fallback to easy crane.
//
// 1. Primary: crane_cmd_("tension") returns "OK left=<kg> right=<kg>" (DSZL-107
//    cached by crane's hold_loop atomic; ~1ms server processing + TCP RTT).
//    Returns max(left, right) per Q1=(a) decision 2026-05-07.
// 2. Fallback (if crane offline / parse fail): washrobot-end DY-500 cache
//    (slave 10/11 — only present if installed; in current builds these are
//    offline, returning -1).
// 3. Final fallback: easy crane weight via shim (kept per Q4=(a) decision —
//    extra safety net in case DSZL-107 fails).
// Returns -1 if all three fail.
// Sentinel for rope/weight read functions: "couldn't read at all" vs "got a
// valid reading (possibly negative — uncalibrated DSZL can read negative as
// a zero-offset artifact, treated as 'low tension')". A real reading never
// approaches -9999 kg. Callers should use `pre <= WEIGHT_NO_DATA_KG` to test
// for "no data" instead of `pre < 0` (which would also reject valid negative
// readings on uncalibrated hardware).
static constexpr double WEIGHT_NO_DATA_KG = -9999.0;

double WashRobot::read_rope_weight_max_kg_() {
    // Parser helper — sets a/b to parsed values or leaves at WEIGHT_NO_DATA_KG
    // if the field is missing / unparseable (e.g. "ERR" in place of number).
    auto parse_lr = [](const std::string& rep, double& a, double& b) {
        a = b = WEIGHT_NO_DATA_KG;
        auto lp = rep.find("left=");
        auto rp = rep.find("right=");
        if (lp != std::string::npos) {
            try { a = std::stod(rep.substr(lp + 5)); } catch (...) {}
        }
        if (rp != std::string::npos) {
            try { b = std::stod(rep.substr(rp + 6)); } catch (...) {}
        }
    };

    // 1. Primary: ask crane via TCP RPC. Negative values are accepted as
    // valid (uncalibrated DSZL); a re-read confirms a transient negative
    // isn't a glitch (per user 2026-05-20). Only fall through to fallbacks
    // when both sides truly fail to parse.
    if (crane_attached_.load()) {
        std::string rep = crane_cmd_("tension", 2);
        if (rep.rfind("OK", 0) == 0) {
            double l, rr;
            parse_lr(rep, l, rr);

            // Re-read confirmation on negative: physically impossible but
            // bench (uncalibrated DSZL zero/scale) reads it. A consistent
            // negative is real (low tension after offset); a transient
            // negative gets overridden by the second read.
            const bool l_neg = (l > WEIGHT_NO_DATA_KG && l < 0);
            const bool r_neg = (rr > WEIGHT_NO_DATA_KG && rr < 0);
            if (l_neg || r_neg) {
                std::cout << "[rope_weight] negative reading L=" << l
                          << " R=" << rr << " — re-reading to confirm\n";
                std::string rep2 = crane_cmd_("tension", 2);
                if (rep2.rfind("OK", 0) == 0) {
                    double l2, r2;
                    parse_lr(rep2, l2, r2);
                    if (l2 > WEIGHT_NO_DATA_KG) l = l2;
                    if (r2 > WEIGHT_NO_DATA_KG) rr = r2;
                    std::cout << "[rope_weight] re-read L=" << l << " R=" << rr
                              << ((l < 0 || rr < 0) ? " (negative confirmed, using as-is)"
                                                    : " (was transient, recovered)") << "\n";
                }
            }

            if (l > WEIGHT_NO_DATA_KG && rr > WEIGHT_NO_DATA_KG) return std::max(l, rr);
            if (l > WEIGHT_NO_DATA_KG) return l;
            if (rr > WEIGHT_NO_DATA_KG) return rr;
            // both unparseable — fall through to next fallback
        }
    }

    // 2. Washrobot-end DY-500 cache (rare — sensors not currently installed)
    double a = weight_comm_ok_[0].load() ? cached_weight_kg_[0].load() : -1.0;
    double b = weight_comm_ok_[1].load() ? cached_weight_kg_[1].load() : -1.0;
    if (a >= 0 || b >= 0) {
        if (a < 0) return b;
        if (b < 0) return a;
        return std::max(a, b);
    }

    // 3. Final fallback — easy crane weight (kept per Q4=(a) decision)
    double easy = read_easy_weight_kg_();
    return (easy >= 0) ? easy : WEIGHT_NO_DATA_KG;   // normalize legacy -1 to sentinel
}

// Query easy crane weight via shim (single sensor: easy crane's DY500 slave 3).
// Uses estop channel to bypass crane_mtx_ — safe to call during in-flight retract.
// Returns kg; -1 on any failure.
double WashRobot::read_easy_weight_kg_() {
    if (!crane_attached_.load()) return -1.0;

    std::string reply;
    {
        std::lock_guard<std::mutex> lk(crane_estop_mtx_);
        if (!crane_cli_estop_.isConnected()) {
            if (!crane_cli_estop_.connectToServer(CRANE_IP, CRANE_PORT))
                return -1.0;
        }
        const char* tx = "status\n";
        if (!crane_cli_estop_.sendData(tx, 7, 500)) return -1.0;
        char buf[512];
        int n = crane_cli_estop_.receiveData(buf, sizeof(buf), 1500);
        if (n <= 0) return -1.0;
        reply.assign(buf, n);
    }

    // Parse "OK weight=<kg> ..." (shim forwards easy's status reply unchanged).
    // weight may be int or float; weight_valid=0 means easy DY500 is failing —
    // treat as -1 (sensor offline).
    auto vpos = reply.find("weight_valid=");
    if (vpos != std::string::npos) {
        // skip past "weight_valid="
        if (reply[vpos + 13] == '0') return -1.0;
    }
    auto wpos = reply.find("weight=");
    if (wpos == std::string::npos) return -1.0;
    try {
        return std::stod(reply.substr(wpos + 7));
    } catch (...) {
        return -1.0;
    }
}

// Read max rope weight (kg) via the dedicated estop TCP channel. Unlike
// read_rope_weight_max_kg_() (whose tier-1 crane_cmd_("tension") grabs
// crane_mtx_), this uses crane_cli_estop_ + crane_estop_mtx_ — so it works
// WHILE a retract holds crane_mtx_ on the main thread. Sends 'tension',
// parses "left=<kg> right=<kg>", returns max. -1 on any failure / detached.
// Used by the crane_retract_safe_ active monitor.
double WashRobot::read_rope_weight_estop_() {
    if (!crane_attached_.load()) return -1.0;

    std::string reply;
    {
        std::lock_guard<std::mutex> lk(crane_estop_mtx_);
        if (!crane_cli_estop_.isConnected()) {
            if (!crane_cli_estop_.connectToServer(CRANE_IP, CRANE_PORT))
                return -1.0;
        }
        const char* tx = "tension\n";
        if (!crane_cli_estop_.sendData(tx, 8, 500)) return -1.0;

        // Drain lines until an OK reply (EVT lines may interleave) or timeout.
        std::string rx;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        char buf[256];
        while (std::chrono::steady_clock::now() < deadline) {
            int n = crane_cli_estop_.receiveData(buf, sizeof(buf), 300);
            if (n <= 0) continue;
            rx.append(buf, n);
            size_t pos;
            while ((pos = rx.find('\n')) != std::string::npos) {
                std::string one = rx.substr(0, pos);
                rx.erase(0, pos + 1);
                if (!one.empty() && one.back() == '\r') one.pop_back();
                if (one.rfind("OK", 0) == 0) { reply = one; break; }
            }
            if (!reply.empty()) break;
        }
    }
    if (reply.empty()) return -1.0;

    double l = -1, r = -1;
    auto lp = reply.find("left=");
    auto rp = reply.find("right=");
    if (lp != std::string::npos) { try { l = std::stod(reply.substr(lp + 5)); } catch (...) {} }
    if (rp != std::string::npos) { try { r = std::stod(reply.substr(rp + 6)); } catch (...) {} }
    if (l >= 0 && r >= 0) return std::max(l, r);
    if (l >= 0) return l;
    if (r >= 0) return r;
    return -1.0;
}

double WashRobot::rope_weight_limit_per_sensor_kg_() const {
    // State-aware: cups holding → low limit; hanging on rope → high limit.
    State s = state_.load();
    switch (s) {
        case State::Attached:
        case State::Running:
        case State::WaitingConfirm:
        case State::Paused:
        case State::PausedOnError:
        case State::Balancing:
            return ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_ATTACHED;
        case State::Idle:
        case State::Ready:
        case State::ReturningHome:
        case State::Error:
        default:
            return ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_HANGING;
    }
}

// Wraps `crane_cmd_("retract <cm>")` with weight-based safety guard:
//   - Pre-check: if any sensor already > limit → refuse with ERR
//   - Active monitor: spawn watcher polling every WEIGHT_MONITOR_POLL_MS; on
//     overweight → send "stop" to crane, then return OK (the early stop is
//     treated as the retract having reached its goal — slack collected).
// On comm fail (sensors offline) → refuse all retracts (safe default).
std::string WashRobot::crane_retract_safe_(int cm, int timeout_sec) {
    if (cm <= 0) return "ERR retract_cm_invalid";

    // [2026-06-05] timeout_sec=0 → 用 cm 算 dynamic timeout（default behavior）。
    // 顯式傳值的 caller 維持原 timeout。
    if (timeout_sec <= 0) timeout_sec = crane_motion_timeout_sec_(cm);

    // Detached mode bypass — no rope tension to worry about
    if (!crane_attached_.load()) {
        std::cout << "[crane_retract_safe] crane_attached=off, skip\n";
        return crane_cmd_("retract " + std::to_string(cm), timeout_sec);
    }

    const double limit = rope_weight_limit_per_sensor_kg_();
    const double pre = read_rope_weight_max_kg_();
    if (pre <= WEIGHT_NO_DATA_KG) {   // truly no reading (not just negative — DSZL uncalibrated)
        std::cout << "[crane_retract_safe] WEIGHT SENSOR OFFLINE — refuse retract for safety\n";
        return "ERR rope_weight_sensor_offline";
    }
    if (pre > limit) {
        std::ostringstream oss;
        oss << "ERR rope_weight_too_high pre=" << pre << "kg limit=" << limit << "kg";
        std::cout << "[crane_retract_safe] " << oss.str() << "\n";
        return oss.str();
    }
    std::cout << "[crane_retract_safe] pre=" << pre << "kg limit=" << limit
              << "kg → start retract " << cm << " cm\n";

    // Active monitor: separate thread polls weight via the dedicated estop TCP
    // channel (read_rope_weight_estop_). It MUST use the estop channel for BOTH
    // the read AND the stop: the main thread holds crane_mtx_ for the whole
    // retract, so read_rope_weight_max_kg_() (tier-1 crane_cmd_) would block on
    // that mutex and the monitor would never get a reading. On breach → send
    // "stop" over the same estop channel.
    std::atomic<bool> monitor_running{true};
    std::atomic<bool> monitor_tripped{false};
    std::atomic<double> monitor_peak_kg{pre};
    std::thread monitor([this, &monitor_running, &monitor_tripped, &monitor_peak_kg, limit]() {
        while (monitor_running.load()) {
            double w = read_rope_weight_estop_();
            if (w >= 0) {
                if (w > monitor_peak_kg.load()) monitor_peak_kg.store(w);
                if (w > limit) {
                    monitor_tripped.store(true);
                    std::cout << "[crane_retract_safe] OVERWEIGHT w=" << w
                              << "kg > limit=" << limit << "kg — sending crane stop via estop channel\n";
                    // Use dedicated estop connection to avoid crane_mtx_ deadlock
                    std::lock_guard<std::mutex> elk(crane_estop_mtx_);
                    if (!crane_cli_estop_.isConnected())
                        crane_cli_estop_.connectToServer(CRANE_IP, CRANE_PORT);
                    if (crane_cli_estop_.isConnected()) {
                        const char* tx = "stop\n";
                        crane_cli_estop_.sendData(tx, 5, 500);
                        char buf[64];
                        crane_cli_estop_.receiveData(buf, sizeof(buf), 1000);   // drain reply
                    } else {
                        std::cout << "[crane_retract_safe] WARN: estop connection unavailable\n";
                    }
                    break;
                }
            }
            sleep_ms_(WEIGHT_MONITOR_POLL_MS);
        }
    });

    // Main retract call (blocks until crane reply or timeout)
    std::string reply = crane_cmd_("retract " + std::to_string(cm), timeout_sec);
    monitor_running.store(false);
    if (monitor.joinable()) monitor.join();

    if (monitor_tripped.load()) {
        // Tension hit the limit mid-retract — crane was stopped via the estop
        // channel. Per user (2026-05-19): treat this as the retract having
        // reached its goal (slack collected, rope taut), NOT an error — return
        // OK so the motion flow continues instead of dropping to PausedOnError.
        // The "rope_weight_tripped" marker stays in the reply for logs / EVT.
        std::ostringstream oss;
        oss << "OK rope_weight_tripped peak=" << monitor_peak_kg.load()
            << "kg limit=" << limit << "kg (stopped early, treated as done)";
        std::cout << "[crane_retract_safe] " << oss.str()
                  << " (crane reply was: " << reply << ")\n";
        // [arm rope protect TEMP 2026-05-21] PARK injection REMOVED 2026-05-21o:
        // user observed that after pay_out the pole's relative position blocks
        // the arm's PARK trajectory. Auto-PARK during intermediate retracts is
        // therefore unsafe. Arm now stays at DEPLOY 250 CENTER throughout
        // body phases — PARK happens only at clean_sweep cleanup (end of
        // step_down/step_up). If clean_sweep PARK is also blocked, remove its
        // arm_cmd_("PARK") in do_arm_clean_sweep_ cleanup too.
        return oss.str();
    }

    std::cout << "[crane_retract_safe] retract " << cm << " cm done, peak weight="
              << monitor_peak_kg.load() << "kg\n";
    // [arm rope protect TEMP 2026-05-21] PARK injection REMOVED 2026-05-21o (see above)
    return reply;
}

// Incremental pay_out: send 1cm-at-a-time, poll per-side rope tension, stop
// when EITHER left OR right tension drops to <= target_kg (body weight
// partially transferred to the cups). Capped at max_cm. Called at end of
// cmd_attach.
// (2026-05-20: changed from "both ≤ target" to "either ≤ target" per bench —
// the "both" condition let one side overshoot way under target by the time the
// heavier side caught up, e.g. waiting for left to drop from 14.91→12 made the
// already-OK right side go from 9.18→1.97kg. Stopping when either side first
// reaches target keeps the rope from going slack.)
std::string WashRobot::crane_pay_out_to_weight_(double target_kg, int max_cm) {
    if (target_kg <= 0 || max_cm <= 0) return "ERR invalid_params";

    if (!crane_attached_.load()) {
        std::cout << "[crane_pay_out_to_weight] crane_attached=off, skip\n";
        return "OK skipped total_cm=0";
    }

    // Read per-side tension from crane DSZL-107: "OK left=<kg> right=<kg>".
    // Returns true ONLY on real read failure (no OK reply / both sides
    // unparseable). Negative values are accepted as valid (uncalibrated DSZL
    // offset — re-read confirms it isn't a transient glitch). Per user
    // 2026-05-20: a consistent negative is a real low-tension reading.
    auto read_lr = [this](double& l, double& r) -> bool {
        auto parse = [](const std::string& rep, double& a, double& b) {
            a = b = WEIGHT_NO_DATA_KG;
            auto lp = rep.find("left=");
            auto rp = rep.find("right=");
            if (lp != std::string::npos) { try { a = std::stod(rep.substr(lp + 5)); } catch (...) {} }
            if (rp != std::string::npos) { try { b = std::stod(rep.substr(rp + 6)); } catch (...) {} }
        };
        std::string rep = crane_cmd_("tension", 2);
        if (rep.rfind("OK", 0) != 0) { l = r = WEIGHT_NO_DATA_KG; return true; }
        parse(rep, l, r);
        // Re-read on negative (transient vs real low tension)
        if ((l > WEIGHT_NO_DATA_KG && l < 0) || (r > WEIGHT_NO_DATA_KG && r < 0)) {
            std::string rep2 = crane_cmd_("tension", 2);
            if (rep2.rfind("OK", 0) == 0) {
                double l2, r2;
                parse(rep2, l2, r2);
                if (l2 > WEIGHT_NO_DATA_KG) l = l2;
                if (r2 > WEIGHT_NO_DATA_KG) r = r2;
            }
        }
        return (l <= WEIGHT_NO_DATA_KG && r <= WEIGHT_NO_DATA_KG);   // fail only if both unparseable
    };

    double l = -1, r = -1;
    if (read_lr(l, r)) {
        std::cout << "[crane_pay_out_to_weight] tension read FAIL — skip (won't pay out blind)\n";
        return "ERR tension_sensor_offline total_cm=0";
    }
    if (l <= target_kg || r <= target_kg) {
        std::ostringstream oss;
        oss << "OK already_at_target left=" << l << " right=" << r << " total_cm=0";
        std::cout << "[crane_pay_out_to_weight] " << oss.str() << "\n";
        return oss.str();
    }

    // [arm rope protect TEMP 2026-05-21 — DISABLED 2026-05-22] stow arm
    // BEFORE pay_out loop starts. user 2026-05-22: 「把所有在收放繩之前 deploy
    // center 都註解掉」。保留 commented 程式碼以便日後恢復。
    //if (ensure_arm_center_for_rope_("crane_pay_out_to_weight")) {
    //    return "ERR arm_stow_failed total_cm=0";
    //}

    int total_cm = 0;
    while (total_cm < max_cm) {
        if (crane_cmd_("pay_out 1").rfind("OK", 0) != 0) {
            std::ostringstream oss;
            oss << "ERR pay_out_step_fail total_cm=" << total_cm;
            return oss.str();
        }
        total_cm += 1;

        sleep_ms_(ATTACH_PAYOUT_SETTLE_MS);

        if (read_lr(l, r)) {
            std::ostringstream oss;
            oss << "ERR tension_sensor_offline_mid_payout total_cm=" << total_cm;
            return oss.str();
        }

        std::cout << "[crane_pay_out_to_weight] step total_cm=" << total_cm
                  << "/" << max_cm << " left=" << l << "kg right=" << r
                  << "kg target=" << target_kg << "kg\n";

        if (l <= target_kg || r <= target_kg) {
            std::ostringstream oss;
            oss << "OK reached left=" << l << " right=" << r << " total_cm=" << total_cm;
            return oss.str();
        }
    }

    std::ostringstream oss;
    oss << "OK max_cm_reached total_cm=" << total_cm << " left=" << l << " right=" << r;
    return oss.str();
}

// [2026-06-02] Per-side retract until both L/R tension >= target_kg. See
// header doc for why this exists vs symmetric `retract` cmd. Used by
// bal_cal_preload_; could be general-purpose.
std::string WashRobot::crane_retract_to_weight_(double target_kg, double safety_max_kg,
                                                  int max_iter,
                                                  int pulse_ms, int settle_ms) {
    if (target_kg <= 0 || max_iter <= 0) return "ERR invalid_params";
    if (safety_max_kg <= target_kg)      return "ERR safety_max_le_target";

    if (!crane_attached_.load()) {
        std::cout << "[retract_to_weight] crane_attached=off, skip\n";
        return "OK skipped";
    }

    // Tension parser shared with crane_pay_out_to_weight_ pattern.
    auto read_lr = [this](double& l, double& r) -> bool {
        auto parse = [](const std::string& rep, double& a, double& b) {
            a = b = WEIGHT_NO_DATA_KG;
            auto lp = rep.find("left=");
            auto rp = rep.find("right=");
            if (lp != std::string::npos) { try { a = std::stod(rep.substr(lp + 5)); } catch (...) {} }
            if (rp != std::string::npos) { try { b = std::stod(rep.substr(rp + 6)); } catch (...) {} }
        };
        std::string rep = crane_cmd_("tension", 2);
        if (rep.rfind("OK", 0) != 0) { l = r = WEIGHT_NO_DATA_KG; return true; }
        parse(rep, l, r);
        return (l <= WEIGHT_NO_DATA_KG && r <= WEIGHT_NO_DATA_KG);
    };

    for (int iter = 0; iter < max_iter; ++iter) {
        double l = -1, r = -1;
        if (read_lr(l, r)) {
            return "ERR tension_sensor_offline iter=" + std::to_string(iter);
        }

        // Safety: either side over limit → abort
        if (l >= safety_max_kg || r >= safety_max_kg) {
            std::ostringstream oss;
            oss << "ERR overweight L=" << l << " R=" << r << " safety_max=" << safety_max_kg;
            std::cerr << "[retract_to_weight] " << oss.str() << "\n";
            return oss.str();
        }

        // Both sides at target → done
        const bool l_ok = (l >= target_kg);
        const bool r_ok = (r >= target_kg);
        if (l_ok && r_ok) {
            std::cout << "[retract_to_weight] DONE iter=" << iter
                      << " L=" << l << "kg R=" << r << "kg target=" << target_kg << "kg\n";
            return "";
        }

        std::cout << "[retract_to_weight] iter " << iter << " L=" << l << "kg R=" << r
                  << "kg target=" << target_kg << "kg → pulse";

        // [2026-06-02 v2] Per user：兩側同時 pulse（並行），避免 sequential
        // 開→停→開→停 的多次 toggle。如果只一側需收，只 pulse 那側。
        const bool need_l = !l_ok;
        const bool need_r = !r_ok;
        if (need_l) { crane_cmd_("up_left on",  2); std::cout << " L"; }
        if (need_r) { crane_cmd_("up_right on", 2); std::cout << " R"; }
        std::cout << " " << pulse_ms << "ms";
        sleep_ms_(pulse_ms);
        if (need_l) crane_cmd_("up_left off",  2);
        if (need_r) crane_cmd_("up_right off", 2);
        std::cout << "\n";

        sleep_ms_(settle_ms);
    }
    return "ERR max_iter_exhausted=" + std::to_string(max_iter);
}

// Crane watchdog — 2026-05-15 redesigned: NO continuous ping. Only drains
// crane_alarm_pending_ flag (set by handle_crane_evt_ from passively-arriving
// EVT lines) and escalates to PausedOnError.
//
// History (2026-04..05): used to ping every 500ms + abort if no OK in 2s.
// Bench found this caused false aborts when crane TCP socket was zombie
// (flag=connected but actually dead — kernel didn't see RST, e.g. from NAT
// table eviction or network blip). User's actual operation (GUI) worked
// because GUI uses a separate TCP socket from web_backend. Washrobot's stale
// socket kept failing pings → watchdog triggered abort even though crane was
// fine. Continuous ping also added TCP traffic and competed for crane_mtx_.
//
// New design: don't ping. Trust crane_cmd_ to fail-fast when actually used,
// and rely on its self-healing reconnect on failure. Crane safety alarms still
// flow via EVT broadcasts (handle_crane_evt_) which arrive on the recv side
// of any crane_cmd_ in progress.
void WashRobot::crane_watchdog_loop_() {
    while (crane_wd_running_.load()) {
        sleep_ms_(HEARTBEAT_INTERVAL_MS);
        if (!crane_wd_running_.load()) break;
        if (!crane_attached_.load()) continue;

        // Crane safety alarm (set by handle_crane_evt_ when EVT tension_alarm
        // / tension_total_limit drained from any crane_cmd_'s recv stream).
        // Per Q3=(a) 2026-05-07 design: escalate to PausedOnError so operator
        // must inspect before next motion.
        if (crane_alarm_pending_.exchange(false)) {
            std::string kind, detail;
            {
                std::lock_guard<std::mutex> lk(crane_alarm_mtx_);
                kind   = crane_alarm_kind_;
                detail = crane_alarm_detail_;
            }
            std::cout << "[crane_watchdog] CRANE ALARM " << kind
                      << " — entering PausedOnError. Detail: " << detail << "\n";
            evt_("crane_alarm_paused kind=" + kind);
            {
                std::lock_guard<std::mutex> slk(state_mtx_);
                // Guard (same as await_user_intervention_): if a try_or_pause_
                // already entered PausedOnError for the same crane failure,
                // state_ is ALREADY PausedOnError — overwriting state_before_pause_
                // with it corrupts the recovery target, so cmd_continue / cmd_skip
                // would just set the state right back to PausedOnError (the
                // skip/retry buttons appear dead). Keep the original pre-pause state.
                if (state_.load() != State::PausedOnError)
                    state_before_pause_ = state_.load();
            }
            set_state_(State::PausedOnError);
        }
    }
}

// Crane keepalive (2026-05-15): background ping during washrobot-side long ops
// so crane_watchdog doesn't false-abort. Without this, sustained washrobot
// motion (ZDT pusher extend 4s+, DM2J rail move 2-3s, no crane comms during
// these) silently lets crane_last_ok_ms_ age past WATCHDOG_TIMEOUT_MS (2s)
// → abort_flag set → motion aborts mid-step.
//
// Logic: poll motion_active_ every PING_PERIOD_MS. When active, send "ping"
// to crane via crane_cmd_; the OK reply refreshes crane_last_ok_ms_ via the
// existing path in crane_cmd_. When idle, just sleep — no need to ping.
//
// Skip if !crane_attached_ (operator disabled crane for solo bench testing)
// or !crane_cli_.isConnected() (TCP not up yet — avoid spammy reconnect
// attempts; the normal reconnect path handles initial connection).
void WashRobot::crane_keepalive_loop_() {
    constexpr int PING_PERIOD_MS = 1000;   // 1Hz keepalive while motion active
    int  consecutive_fail = 0;
    while (crane_keepalive_running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(PING_PERIOD_MS));
        if (!crane_keepalive_running_.load()) break;
        if (!motion_active_.load())  { consecutive_fail = 0; continue; }
        if (!crane_attached_.load()) { consecutive_fail = 0; continue; }
        if (!crane_cli_.isConnected()) {
            std::cout << "[crane_keepalive] TCP not connected — skipping ping\n";
            consecutive_fail = 0;
            continue;
        }
        // Fire-and-check: crane_cmd_ refreshes crane_last_ok_ms_ on OK reply.
        std::string r = crane_cmd_("ping", 2);
        const bool ok = (r.rfind("OK", 0) == 0);
        if (!ok) {
            consecutive_fail++;
            std::cout << "[crane_keepalive] ping FAIL (" << consecutive_fail
                      << ") reply='" << r << "'\n";
        } else if (consecutive_fail > 0) {
            std::cout << "[crane_keepalive] ping recovered after " << consecutive_fail << " fails\n";
            consecutive_fail = 0;
        }
    }
}

//=========== IMU ===========

bool WashRobot::imu_take_baseline_() {
    double sum_roll = 0.0, sum_pitch = 0.0;
    int n = 0;
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(IMU_BASELINE_SEC);
    while (std::chrono::steady_clock::now() < end) {
        if (!imu_.read_error.load()) {
            sum_roll  += imu_.x;
            sum_pitch += imu_.y;
            ++n;
        }
        sleep_ms_(100);
    }
    if (n == 0) return true;
    imu_roll0_  = sum_roll  / n;
    imu_pitch0_ = sum_pitch / n;
    return false;
}

std::string WashRobot::do_phase5_roll_correct_() {
    std::lock_guard<std::mutex> lk(motion_mtx_);

    // Step 1: release feet + body, keep only center attached (sole support point)
    if (pqw_.controlRelay(CH_VALVE_FEET, false)) return "phase5_feet_off_fail";
    if (pqw_.controlRelay(CH_VALVE_BODY, false)) return "phase5_body_off_fail";
    if (pqw_.controlRelay(CH_VALVE_CENTER, true)) return "phase5_center_on_fail";
    sleep_ms_(300);

    // Verify center is holding before touching ropes
    if (read_pressure_(ZDT_C) > VACUUM_THRESHOLD_KPA)
        return "phase5_center_vacuum_fail";

    // Steps 2-6: iterative roll correction
    std::string err;
    for (int attempt = 1; attempt <= ROLL_CORRECT_RETRY_MAX; ++attempt) {
        double roll = imu_.x - imu_roll0_;
        if (std::abs(roll) < IMU_HYSTERESIS_DEG) { err = ""; break; }

        // Monitor center vacuum on every iteration
        if (read_pressure_(ZDT_C) > VACUUM_THRESHOLD_KPA) {
            err = "phase5_center_lost";
            break;
        }

        double delta_cm = std::abs(roll) * ROLL_CORRECT_CM_PER_DEG;
        std::ostringstream oss;
        oss << "roll_correct " << std::fixed << std::setprecision(1)
            << (roll > 0 ? delta_cm : -delta_cm);
        if (crane_cmd_(oss.str()).rfind("OK", 0) != 0) {
            err = "crane_roll_correct_fail";
            break;
        }

        sleep_ms_(500);

        if (attempt == ROLL_CORRECT_RETRY_MAX &&
            std::abs(imu_.x - imu_roll0_) >= IMU_HYSTERESIS_DEG)
            err = "phase5_no_converge";
    }

    // Step 8: restore full attachment regardless of outcome
    // body first (closer to center), then feet
    pqw_.controlRelay(CH_VALVE_BODY, true);
    sleep_ms_(VACUUM_SETTLE_MS);
    pqw_.controlRelay(CH_VALVE_FEET, true);
    sleep_ms_(VACUUM_SETTLE_MS);

    return err;
}

void WashRobot::imu_monitor_loop_() {
    const int SAMPLE_MS   = 100;
    const int AVG_SAMPLES = 10;
    const int SUSTAIN_MS  = 500;

    std::deque<double> window;
    int  over_ask_ms  = 0;
    int  over_stop_ms = 0;
    bool ask_sent     = false;

    while (imu_mon_running_.load()) {
        sleep_ms_(SAMPLE_MS);
        if (!imu_mon_running_.load()) break;

        if (imu_.read_error.load()) {
            over_ask_ms = over_stop_ms = 0;
            continue;
        }

        double roll  = imu_.x - imu_roll0_;
        double pitch = imu_.y - imu_pitch0_;
        double deg   = std::max(std::abs(roll), std::abs(pitch));

        window.push_back(deg);
        if ((int)window.size() > AVG_SAMPLES) window.pop_front();

        double avg = 0.0;
        for (double d : window) avg += d;
        avg /= (double)window.size();

        // --- EMERGENCY threshold ---
        if (avg >= IMU_EMERGENCY_DEG) {
            over_stop_ms += SAMPLE_MS;
            if (over_stop_ms >= SUSTAIN_MS && !abort_flag.load()) {
                // Re-enabled 2026-04-28 — tilt > 45° sustained → emergency stop.
                std::cout << "[imu_monitor] EMERGENCY tilt avg=" << std::fixed
                          << std::setprecision(1) << avg << "° >= "
                          << IMU_EMERGENCY_DEG << "° sustained "
                          << over_stop_ms << "ms — ABORT_FLAG SET\n";
                abort_flag    = true;
                motion_active_ = false;
                crane_cmd_("stop", 2);   // Crane_control_PI uses 'stop' (no 'emergency_stop' alias)
                set_state_(State::Error);
                std::ostringstream oss;
                oss << "imu_emergency balance_deg=" << std::fixed << std::setprecision(1)
                    << avg;
                evt_(oss.str());
            }
        } else {
            over_stop_ms = 0;
        }

        // --- ASK threshold ---
        if (avg >= IMU_ASK_DEG && avg < IMU_EMERGENCY_DEG) {
            over_ask_ms += SAMPLE_MS;
            if (over_ask_ms >= SUSTAIN_MS && !ask_sent) {
                ask_sent        = true;
                imu_ask_pending_ = true;
                {
                    std::lock_guard<std::mutex> lk(state_mtx_);
                    State cur = state_.load();
                    if (cur == State::Running || cur == State::Balancing || cur == State::Attached) {
                        state_before_wait_ = cur;
                        set_state_(State::WaitingConfirm);
                    }
                }
                std::ostringstream oss;
                oss << "balance_ask roll=" << std::fixed << std::setprecision(1) << roll
                    << " pitch=" << pitch;
                evt_(oss.str());
            }
        } else {
            over_ask_ms = 0;
            if (avg < IMU_ASK_DEG - IMU_HYSTERESIS_DEG) {
                if (ask_sent) {
                    std::lock_guard<std::mutex> lk(state_mtx_);
                    if (state_.load() == State::WaitingConfirm)
                        set_state_(state_before_wait_);
                }
                ask_sent        = false;
                imu_ask_pending_ = false;
            }
        }
    }
}

//=========== pusher / vacuum ===========

// Wait for ZDT motor to physically stop (speed + position stability) instead of
// relying on the `pos_reached` status bit, which ZDT firmware sets unreliably
// (memory project_zdt_firmware_quirks #1). Declare done when:
//   - |real_speed| <= 20 RPM for 3 consecutive polls (~450ms), OR
//   - |Δreal_pos| <= 0.15° for 3 consecutive polls
// stall_flag set → release flag + return true (fail), letting the caller
// (try_or_pause_) drop into PausedOnError so the operator can fix.
// Returns false on success, true on stall / comms fail / timeout.
bool WashRobot::zdt_wait_motion_done_(int slave, int timeout_ms, bool defer_stall_release) {
    const int    poll_ms            = 150;
    const int    STABLE_COUNT       = 3;
    const double SPEED_THRESHOLD_RPM = 20.0;
    const double POS_DELTA_DEG       = 0.15;
    int stable_count = 0;
    double prev_pos = 1e9;
    int elapsed = 0;
    int consecutive_fails = 0;
    int total_fails = 0;
    int poll_count = 0;
    uint16_t peak_I = 0;   // peak phase_current during this move

    // Silence ZDT hex dump during the poll loop to avoid flooding the GUI log
    // with dozens of get_status TX/RX pairs per second. Restore user's chosen
    // driver_dbg_ when done (so ad-hoc ZDT commands elsewhere still log).
    if (driver_dbg_) Z_(slave).set_debug(false);
    // NOTE: sleep at top of loop (matches Linux_test zdt_group_move_sync pattern).
    // This gives ~150ms warm-up after trigger_sync_move before first poll, letting
    // TCP-gateway buffer alignment settle (ZDT firmware quirk #3). We never give up
    // on comms fail alone — just keep retrying until the global 15s timeout, then
    // report total fail count for diagnosis.
    while (elapsed < timeout_ms) {
        sleep_ms_(poll_ms);
        elapsed += poll_ms;

        if (Z_(slave).get_system_status()) {
            consecutive_fails++;
            total_fails++;
            continue;   // keep retrying within timeout budget
        }
        if (consecutive_fails > 0) {
            std::cout << "[wait ZDT:" << slave << "] recovered after " << consecutive_fails
                      << " comms fail(s) at " << elapsed << "ms\n";
            consecutive_fails = 0;
        }
        const auto& st = Z_(slave).status;

        // Diagnostic: peak phase current + live current log every ~300ms (every
        // 2 polls). Mirrors disable_seal / pusher_move_many_ current logging.
        poll_count++;
        if (st.phase_current > peak_I) peak_I = st.phase_current;
        if (poll_count % 2 == 0) {
            std::cout << "[wait ZDT:" << slave << "] move I=" << st.phase_current
                      << "mA pos=" << st.real_pos << "°"
                      << " spd=" << st.real_speed << "rpm\n";
        }

        if (st.stall_flag) {
            std::cout << "[wait ZDT:" << slave << "] STALL at " << elapsed
                      << "ms, pos=" << st.real_pos << "° peakI=" << peak_I << "mA";
            if (defer_stall_release) {
                // Cup hit wall during extend — that's the desired endpoint.
                // Leave stall flag set so motor stays clamped against wall while
                // vacuum builds. Caller (cycle_group_/fine_tune extend) releases
                // after vacuum check. Treat as success.
                std::cout << " — DEFER stall release (vacuum check pending)\n";
                if (driver_dbg_) Z_(slave).set_debug(true);
                return false;
            }
            std::cout << " — release flag + fail\n";
            // Clear stall flag so the NEXT motion command (e.g. user retries
            // via cmd_continue) is accepted. Without clearing, ZDT firmware
            // rejects subsequent Modbus pos_mode writes — retry would also
            // hang waiting for a response that never comes.
            Z_(slave).release_stall_flag();
            // Promote stall to a real failure so the caller (pusher_move_many_
            // → cycle_group_ via try_or_pause_) goes to PausedOnError instead
            // of pretending success. Operator can then physically inspect
            // (re-zero ZDT, clear obstruction) and press 繼續/略過.
            if (driver_dbg_) Z_(slave).set_debug(true);   // restore log on fail path
            return true;
        }
        bool speed_ok = std::fabs(st.real_speed) <= SPEED_THRESHOLD_RPM;
        bool pos_ok   = std::fabs(st.real_pos - prev_pos) <= POS_DELTA_DEG;
        prev_pos = st.real_pos;
        if (speed_ok && pos_ok) stable_count++; else stable_count = 0;
        if (stable_count >= STABLE_COUNT) {
            std::cout << "[wait ZDT:" << slave << "] done at " << elapsed
                      << "ms, pos=" << st.real_pos << "° peakI=" << peak_I
                      << "mA (total comms fails=" << total_fails << ")\n";
            if (driver_dbg_) Z_(slave).set_debug(true);   // restore for subsequent commands
            return false;
        }
    }
    std::cout << "[wait ZDT:" << slave << "] TIMEOUT after " << timeout_ms
              << "ms, last pos=" << prev_pos << "°, speed=" << Z_(slave).status.real_speed
              << " rpm, peakI=" << peak_I << "mA, total comms fails=" << total_fails << "\n";
    if (driver_dbg_) Z_(slave).set_debug(true);   // restore (timeout path too)
    return true;
}

bool WashRobot::pusher_move_(int slave, int pulse, int rpm, int acc, bool defer_stall_release) {
    if (Z_(slave).motion_control_pos_mode_nowait(0, acc, rpm, pulse, 1, 0, 1)) {
        std::cout << "[pusher_move ZDT:" << slave << "] pos_mode_nowait FAIL"
                  << " (pulse=" << pulse << " rpm=" << rpm << " acc=" << acc
                  << ") — check driver_EN / stall / alarm\n";
        return true;
    }
    return zdt_wait_motion_done_(slave, 15000, defer_stall_release);
}

// Parallel-poll wait for many ZDT slaves. Extracted from pusher_move_many_'s
// inline loop so disable_seal Phase 1 can reuse it (2026-05-28). Vs. sequential
// per-slave wait: when slaves run sync motion (broadcast trigger), they finish
// near-simultaneously → parallel poll time ≈ max(slave time) instead of sum.
// pusher_move_many_ still has its own inline copy to avoid regression risk;
// can be refactored to call this helper later.
bool WashRobot::zdt_wait_motion_done_many_(const std::vector<int>& slaves, int timeout_ms, bool defer_stall_release, int* stalled_slave_out, std::vector<uint16_t>* peakI_out) {
    if (slaves.empty()) return false;
    const int    poll_ms             = 150;
    const int    STABLE_COUNT_NEED   = 2;   // 2026-05-29: 3 → 2 試提速,省 ~150ms 確認延遲;stage 2 高速 controlled stop 反彈機率低
    const double SPEED_THRESHOLD_RPM = 20.0;
    const double POS_DELTA_DEG       = 0.15;

    std::vector<int>      stable(slaves.size(), 0);
    std::vector<double>   prev_pos(slaves.size(), 1e9);
    std::vector<bool>     done(slaves.size(), false);
    std::vector<uint16_t> peak_I(slaves.size(), 0);

    // [2026-05-29] If caller wants peakI feedback, prep the output vector.
    if (peakI_out) {
        peakI_out->assign(slaves.size(), 0);
    }
    int n_done     = 0;
    int elapsed    = 0;
    int poll_count = 0;

    if (driver_dbg_) for (int s : slaves) Z_(s).set_debug(false);

    while (n_done < (int)slaves.size() && elapsed < timeout_ms) {
        sleep_ms_(poll_ms);
        elapsed += poll_ms;
        poll_count++;

        for (size_t i = 0; i < slaves.size(); ++i) {
            if (done[i]) continue;
            const int s = slaves[i];

            if (Z_(s).get_system_status()) continue;   // comm fail, retry within timeout
            const auto& st = Z_(s).status;

            if (st.phase_current > peak_I[i]) peak_I[i] = st.phase_current;
            if (poll_count % 2 == 0) {
                std::cout << "[wait_many ZDT:" << s << "] move I=" << st.phase_current
                          << "mA pos=" << st.real_pos << "°"
                          << " spd=" << st.real_speed << "rpm\n";
            }

            if (st.stall_flag) {
                std::cout << "[wait_many ZDT:" << s << "] STALL at " << elapsed
                          << "ms, pos=" << st.real_pos << "° peakI=" << peak_I[i] << "mA";
                if (defer_stall_release) {
                    std::cout << " — DEFER stall release\n";
                    done[i] = true;
                    ++n_done;
                    continue;
                }
                std::cout << " — release flag + fail\n";
                Z_(s).release_stall_flag();
                if (stalled_slave_out) *stalled_slave_out = s;
                if (driver_dbg_) for (int s2 : slaves) Z_(s2).set_debug(true);
                return true;
            }

            const bool speed_ok = std::fabs(st.real_speed) <= SPEED_THRESHOLD_RPM;
            const bool pos_ok   = std::fabs(st.real_pos - prev_pos[i]) <= POS_DELTA_DEG;
            prev_pos[i] = st.real_pos;
            if (speed_ok && pos_ok) ++stable[i]; else stable[i] = 0;
            if (stable[i] >= STABLE_COUNT_NEED) {
                std::cout << "[wait_many ZDT:" << s << "] done at " << elapsed
                          << "ms, pos=" << st.real_pos << "° peakI=" << peak_I[i] << "mA\n";
                done[i] = true;
                ++n_done;
            }
        }
    }

    if (driver_dbg_) for (int s : slaves) Z_(s).set_debug(true);

    // [2026-05-29] Copy per-slave peakI to caller's vector if requested.
    if (peakI_out) {
        for (size_t i = 0; i < slaves.size(); ++i) (*peakI_out)[i] = peak_I[i];
    }

    if (n_done < (int)slaves.size()) {
        std::cout << "[wait_many] TIMEOUT after " << timeout_ms << "ms, "
                  << n_done << "/" << slaves.size() << " resolved\n";
        return true;
    }
    return false;
}
// (end zdt_wait_motion_done_many_)

bool WashRobot::pusher_move_many_(const std::vector<int>& slaves, int pulse, int rpm, int acc, bool defer_stall_release) {
    // [2026-05-29] DM2J motion active gate — see dm2j_pair_move_abs_ for rationale.
    // Pushers extending/retracting also shifts body weight → arm M1/M2 tau drift.
    // (Reuses the same flag — both feet rail and pushers share the same gating
    // semantic from arm_monitor_during_sweep_'s perspective.)
    dm2j_motion_active_.store(true);
    struct ClearMotionFlag {
        std::atomic<bool>* flag;
        ~ClearMotionFlag() { flag->store(false); }
    } _clr{&dm2j_motion_active_};

    // Pre-clear stall flags before issuing the motion command. If a previous
    // release_stall_flag() call failed (comm error), the flag may still be set
    // and ZDT firmware will silently reject the pos_mode write → motor never
    // moves but zdt_wait_motion_done_ sees speed=0/pos stable and returns false
    // (false success). Clearing here prevents that phantom success.
    for (int s : slaves) Z_(s).release_stall_flag();

    // sync=1 pattern requires the _nowait variant: enqueue each slave's PR block
    // without internal wait, then broadcast trigger_sync_move, then poll per slave.
    for (int s : slaves) {
        if (Z_(s).motion_control_pos_mode_nowait(0, acc, rpm, pulse, 1, 1, 1)) {
            std::cout << "[pusher_move_many ZDT:" << s << "] pos_mode_nowait FAIL"
                      << " (pulse=" << pulse << " rpm=" << rpm << " acc=" << acc
                      << ") — check driver_EN / stall / alarm\n";
            return true;
        }
    }
    // NOTE: trigger_sync_move() is a Modbus BROADCAST (slave addr 0x00) — per
    // Modbus spec, broadcasts get no response. Driver impl returns true (error)
    // on empty response, which is unreliable for broadcasts. We ignore return
    // value here and trust the send. (TODO: fix in user_lib/ZDT_motor_control.)
    if (!slaves.empty()) Z_(slaves.front()).trigger_sync_move();

    // Parallel poll all slaves in a single loop: one iteration polls every
    // not-yet-done slave, marks the ones that have finished, exits when all
    // resolved. Vs. sequential per-slave wait, this saves (N-1) × ~600ms of
    // confirmation time when slaves finish near-simultaneously (sync trigger).
    const int    timeout_ms          = 15000;
    const int    poll_ms             = 150;
    const int    STABLE_COUNT_NEED   = 3;
    const double SPEED_THRESHOLD_RPM = 20.0;
    const double POS_DELTA_DEG       = 0.15;

    std::vector<int>      stable(slaves.size(), 0);
    std::vector<double>   prev_pos(slaves.size(), 1e9);
    std::vector<bool>     done(slaves.size(), false);
    std::vector<uint16_t> peak_I(slaves.size(), 0);   // peak phase_current per slave
    int n_done  = 0;
    int elapsed = 0;
    int poll_count = 0;

    if (driver_dbg_) for (int s : slaves) Z_(s).set_debug(false);

    while (n_done < (int)slaves.size() && elapsed < timeout_ms) {
        sleep_ms_(poll_ms);
        elapsed += poll_ms;
        poll_count++;

        for (size_t i = 0; i < slaves.size(); ++i) {
            if (done[i]) continue;
            const int s = slaves[i];

            if (Z_(s).get_system_status()) continue;   // comm fail, retry within timeout
            const auto& st = Z_(s).status;

            // Diagnostic: track peak phase current + log live current every ~300ms
            // (every 2 polls). Mirrors the disable_seal Step D current log so the
            // web RETRACT path (pusher_move_many_) also shows the current curve.
            if (st.phase_current > peak_I[i]) peak_I[i] = st.phase_current;
            if (poll_count % 2 == 0) {
                std::cout << "[wait_many ZDT:" << s << "] move I=" << st.phase_current
                          << "mA pos=" << st.real_pos << "°"
                          << " spd=" << st.real_speed << "rpm\n";
            }

            if (st.stall_flag) {
                std::cout << "[wait_many ZDT:" << s << "] STALL at " << elapsed
                          << "ms, pos=" << st.real_pos << "° peakI=" << peak_I[i] << "mA";
                if (defer_stall_release) {
                    std::cout << " — DEFER stall release\n";
                    done[i] = true;
                    ++n_done;
                    continue;
                }
                std::cout << " — release flag + fail\n";
                Z_(s).release_stall_flag();
                if (driver_dbg_) for (int s2 : slaves) Z_(s2).set_debug(true);
                return true;
            }

            const bool speed_ok = std::fabs(st.real_speed) <= SPEED_THRESHOLD_RPM;
            const bool pos_ok   = std::fabs(st.real_pos - prev_pos[i]) <= POS_DELTA_DEG;
            prev_pos[i] = st.real_pos;
            if (speed_ok && pos_ok) ++stable[i]; else stable[i] = 0;
            if (stable[i] >= STABLE_COUNT_NEED) {
                std::cout << "[wait_many ZDT:" << s << "] done at " << elapsed
                          << "ms, pos=" << st.real_pos << "° peakI=" << peak_I[i] << "mA\n";
                done[i] = true;
                ++n_done;
            }
        }
    }

    if (driver_dbg_) for (int s : slaves) Z_(s).set_debug(true);

    if (n_done < (int)slaves.size()) {
        std::cout << "[wait_many] TIMEOUT after " << timeout_ms << "ms, "
                  << n_done << "/" << slaves.size() << " resolved\n";
        return true;
    }
    sleep_ms_(PUSHER_SETTLE_MS);
    return false;
}

// Two-stage retract — delay-based (no continuous status polling during stage 1).
// [2026-05-29 rewrite]
//   Old behavior: polled each slave's status @150ms, fired stage 2 individually
//                 when each finished stage 1 — wall time = max(stage1) + max(stage2)
//                 ≈ 7-10s for body, dominated by slowest slave's stage 1.
//   New behavior: sync-fire stage 1 for all, sleep PUSHER_STAGE1_DELAY_MS (~2.6s)
//                 — no polling — then sync-fire stage 2 for all, wait for done.
//                 Wall time ≈ delay + stage 2 ≈ 4s. Saves ~3-5s per retract.
// Correctness: cup adhesion breaks in the first few mm of motion. Slow-peel
// distance was originally chosen for position-based safety; time-based is
// equivalent because at PUSHER_RPM_RETRACT the cup moves >1cm/sec — adhesion
// breaks well before delay elapses. If cup over-extended (still in motion at
// end of delay), stage 2 just updates target to 0 + speed jumps to RETRACT_FULL —
// motor smoothly accelerates from current intermediate position. Driver accepts
// new pos_mode_nowait mid-motion (same primitive used in old per-slave path).
// Slaves already past stage 1 endpoint skip stage 1 entirely (absolute stage 1
// target would extend them back toward wall).
// Returns true (error) on any stall during stage 2 wait, or timeout.
bool WashRobot::pusher_two_stage_retract_(const std::vector<int>& slaves) {
    if (slaves.empty()) return false;
    const size_t N = slaves.size();

    // [2026-05-29] DM2J motion active gate — see dm2j_pair_move_abs_ for rationale.
    dm2j_motion_active_.store(true);
    struct ClearMotionFlag {
        std::atomic<bool>* flag;
        ~ClearMotionFlag() { flag->store(false); }
    } _clr{&dm2j_motion_active_};

    // Pre-clear stall flags (same rationale as pusher_move_many_): a lingering
    // flag makes ZDT firmware silently reject the pos_mode write.
    for (int s : slaves) Z_(s).release_stall_flag();

    // ---- One-shot position read + classification ----
    // Stage 1 = slow peel to (preset − RETRACT_SLOW_PEEL_CM). A slave whose pusher
    // is ALREADY retracted past that endpoint must NOT be sent there with an
    // absolute move — that would EXTEND it back out toward the wall. Such a
    // slave skips stage 1 and retracts straight to 0 (stage 2). (2026-05-19)
    std::vector<int> stage(N, 1);   // 1 = slow peel, 2 = skip-to-fast
    bool any_stage1 = false;
    for (size_t i = 0; i < N; ++i) {
        const int s = slaves[i];
        const int stage1_target = preset_extend_pulse_for_slave_(s)
                                - cm_to_pulses_for_slave_(s, RETRACT_SLOW_PEEL_CM);

        // Current commanded position (1 pulse = 0.1° encoder).
        // [2026-05-29] Sentinel INT_MIN distinguishes "read failed" from a
        // slightly-negative real_pos (e.g. -0.5° = -5 pulse after retract
        // overshoot noise). Old `-1` sentinel collided with valid -1 pulse
        // reads → mis-classified as "read failed" → ran stage 1 → extended
        // outward (typical bug pattern when calling retract twice with cup
        // already at 0).
        constexpr int READ_FAIL_SENTINEL = std::numeric_limits<int>::min();
        // Hard floor: anything below this is treated as encoder fault — abort
        // rather than risk commanding a forward push toward the wall.
        constexpr int CUR_PULSE_FAULT_FLOOR = -1000;   // -100° ≈ -3cm
        int cur_pulse = READ_FAIL_SENTINEL;
        if (Z_(s).get_system_status() == false)
            cur_pulse = (int)(Z_(s).status.real_pos * 10.0);

        // Encoder fault guard: cur_pulse very negative => something is wrong,
        // don't trust either path. Caller sees error → PausedOnError.
        if (cur_pulse != READ_FAIL_SENTINEL && cur_pulse < CUR_PULSE_FAULT_FLOOR) {
            std::cout << "[2stage_retract ZDT:" << s << "] encoder fault suspect"
                      << " (cur=" << cur_pulse << " < " << CUR_PULSE_FAULT_FLOOR
                      << ") — abort\n";
            return true;
        }

        // Skip stage 1 if cup already at-or-past endpoint. Accept slightly
        // negative cur_pulse (small overshoot noise) as "at hardstop" — skipping
        // stage 1 is safer than running an absolute move that would extend the
        // cup outward to stage1_target.
        if (cur_pulse != READ_FAIL_SENTINEL && cur_pulse <= stage1_target) {
            std::cout << "[2stage_retract ZDT:" << s << "] already inside stage1 endpoint"
                      << " (cur=" << cur_pulse << " <= " << stage1_target
                      << ") — skip stage1, retract direct to 0\n";
            // Queue stage 2 with sync=1 so it fires with the stage-1 batch.
            if (Z_(s).motion_control_pos_mode_nowait(0, PUSHER_ACC_RETRACT,
                    PUSHER_RPM_RETRACT_FULL, PUSHER_RETRACT_PULSE,
                    /*abs*/1, /*sync*/1, /*retry*/1)) {
                std::cout << "[2stage_retract ZDT:" << s << "] stage2-direct pos_mode_nowait FAIL\n";
                return true;
            }
            stage[i] = 2;
        } else {
            if (Z_(s).motion_control_pos_mode_nowait(0, PUSHER_ACC_RETRACT, PUSHER_RPM_RETRACT,
                                                     stage1_target, /*abs*/1, /*sync*/1, /*retry*/1)) {
                std::cout << "[2stage_retract ZDT:" << s << "] stage1 pos_mode_nowait FAIL"
                          << " (target=" << stage1_target << ")\n";
                return true;
            }
            stage[i] = 1;
            any_stage1 = true;
        }
    }
    // Single sync-trigger fires all queued moves (stage 1 + skip-to-stage 2 mixed).
    Z_(slaves.front()).trigger_sync_move();

    // ---- Stage 1 delay (no polling) ----
    // If everyone skipped stage 1, no delay needed — proceed to stage 2 immediately.
    if (any_stage1) {
        std::cout << "[2stage_retract] stage1 delay " << PUSHER_STAGE1_DELAY_MS
                  << "ms (slow peel, no polling)\n";
        sleep_ms_(PUSHER_STAGE1_DELAY_MS);
    }

    // ---- Fire stage 2 for slaves that did stage 1 ----
    std::vector<int> stage2_slaves;
    for (size_t i = 0; i < N; ++i) {
        if (stage[i] != 1) continue;
        const int s = slaves[i];
        if (Z_(s).motion_control_pos_mode_nowait(0, PUSHER_ACC_RETRACT,
                PUSHER_RPM_RETRACT_FULL, PUSHER_RETRACT_PULSE,
                /*abs*/1, /*sync*/1, /*retry*/1)) {
            std::cout << "[2stage_retract ZDT:" << s << "] stage2 pos_mode_nowait FAIL\n";
            return true;
        }
        stage2_slaves.push_back(s);
    }
    if (!stage2_slaves.empty()) {
        Z_(stage2_slaves.front()).trigger_sync_move();
    }

    // ---- Wait for all slaves to reach 0 (single batch wait) ----
    // Uses existing zdt_wait_motion_done_many_ helper. Stall during stage 2 → fail.
    int stalled_id = -1;
    const int stage2_timeout_ms = 10000;
    if (zdt_wait_motion_done_many_(slaves, stage2_timeout_ms,
                                   /*defer_stall=*/false, &stalled_id)) {
        if (stalled_id >= 0) {
            std::cout << "[2stage_retract] STALL slave " << stalled_id
                      << " during stage2 wait\n";
        } else {
            std::cout << "[2stage_retract] TIMEOUT after " << stage2_timeout_ms
                      << "ms waiting for stage2\n";
        }
        return true;
    }

    // [2026-06-02 v10] Anti-FAKE-DONE verification (per Sadie bench 2026-06-02 cal Phase 2).
    // zdt_wait_motion_done_many_ treats "spd≈0 + pos stable" as motion-done. This works
    // when the motor reached its commanded target. But when motor stalls against a
    // load (firmware's stall_flag not yet latched within the polling window), the
    // sensor reading looks identical: speed=0, pos not changing. wait_many reports
    // "done" with the pusher still at preset_extend (~3000°), cup still on wall.
    //
    // Bench: cal Phase 2 released body vacuum but pushers 5/7/8 stalled at ~3000°
    // (high I=2500mA, spd=0, pos unchanged). wait_many said "done at 450ms" — cal
    // then proceeded to Phase 4 with 3 of 4 body cups still mechanically against wall.
    //
    // Verify each slave actually reached target (0 = fully retracted). Tolerance
    // RETRACT_VERIFY_TOL_DEG = 50° (~500 pulse ≈ 0.15cm pusher slack); normal end
    // positions seen in feet retract are < 1° (e.g. 0.4° / -0.1° / 0.07° / 0.09°).
    constexpr double RETRACT_VERIFY_TOL_DEG = 50.0;
    for (int s : slaves) {
        if (Z_(s).get_system_status()) {
            std::cout << "[2stage_retract ZDT:" << s
                      << "] post-wait status read fail — can't verify, fail-safe abort\n";
            return true;
        }
        const double pos = Z_(s).status.real_pos;
        if (std::abs(pos) > RETRACT_VERIFY_TOL_DEG) {
            std::cout << "[2stage_retract ZDT:" << s
                      << "] FAKE-DONE detected: pos=" << pos
                      << "° (expected ≈0, tol=±" << RETRACT_VERIFY_TOL_DEG
                      << "°) — pusher likely stalled, fail\n";
            return true;
        }
    }

    sleep_ms_(PUSHER_SETTLE_MS);
    return false;
}

// Group extend with concurrent vacuum watch. Per-slave wait loop combining ZDT
// status (stall / motion stable) with JC-100 pressure read; whichever fires
// first decides the slave is "done". Vacuum-sealed slaves get emergency_stop
// to halt ZDT mid-motion. Stalls are deferred (cup pressed against wall).
bool WashRobot::pusher_extend_with_vacuum_stop_(const std::vector<int>& slaves,
                                                  const std::vector<int>& pulses,
                                                  int rpm, int acc) {
    // Pre-clear stall flags (matches pusher_move_many_ rationale)
    for (int s : slaves) Z_(s).release_stall_flag();

    // Send motion commands, sync=1 → wait for trigger; each slave uses its own target pulse.
    for (size_t i = 0; i < slaves.size(); ++i) {
        if (Z_(slaves[i]).motion_control_pos_mode_nowait(0, acc, rpm, pulses[i], 1, 1, 1)) {
            std::cout << "[extend group ZDT:" << slaves[i] << "] pos_mode_nowait FAIL"
                      << " (pulse=" << pulses[i] << " rpm=" << rpm << " acc=" << acc
                      << ") — check driver_EN / stall / alarm\n";
            return true;
        }
    }
    // NOTE: trigger_sync_move() is a Modbus BROADCAST (slave addr 0x00) — per
    // Modbus spec, broadcasts get no response. Driver impl returns true (error)
    // on empty response, which is unreliable for broadcasts. We ignore return
    // value here and trust the send. (TODO: fix in user_lib/ZDT_motor_control.)
    if (!slaves.empty()) Z_(slaves.front()).trigger_sync_move();

    const int    timeout_ms          = 15000;
    const int    poll_ms             = 150;
    const int    STABLE_COUNT_NEED   = 3;
    const double SPEED_THRESHOLD_RPM = 20.0;
    const double POS_DELTA_DEG       = 0.15;

    std::vector<int>    stable(slaves.size(), 0);
    std::vector<double> prev_pos(slaves.size(), 1e9);
    std::vector<bool>   done(slaves.size(), false);
    int n_done  = 0;
    int elapsed = 0;

    // Silence ZDT hex dump during the poll loop (matches zdt_wait_motion_done_).
    if (driver_dbg_) for (int s : slaves) Z_(s).set_debug(false);

    while (n_done < (int)slaves.size() && elapsed < timeout_ms) {
        sleep_ms_(poll_ms);
        elapsed += poll_ms;

        for (size_t i = 0; i < slaves.size(); ++i) {
            if (done[i]) continue;
            const int s = slaves[i];

            // 1. Vacuum check FIRST — if sealing, halt motor early to avoid
            //    over-compressing cup. Single-sample (lenient threshold tolerates
            //    noise; motion poll cadence already gives us several reads).
            int p = read_pressure_(s);
            if (M_(s).error_flag == 0 && p <= VACUUM_EARLY_STOP_KPA) {
                // Read current status snapshot for obstacle measurement (best effort)
                double err_deg = 0, cur_ma = 0;
                if (!Z_(s).get_system_status()) {
                    err_deg = Z_(s).status.pos_error;
                    cur_ma  = (double)Z_(s).status.phase_current;
                }
                std::cout << "[extend ZDT:" << s << "] VACUUM SEAL at " << elapsed
                          << "ms, p=" << p << "kPa err=" << err_deg
                          << "° I=" << cur_ma << "mA — emergency_stop early\n";
                Z_(s).emergency_stop(false);   // single-slave halt (sync=false)
                done[i] = true;
                ++n_done;
                continue;
            }

            // 2. ZDT status check
            if (Z_(s).get_system_status()) continue;   // comm fail, retry within timeout
            const auto& st = Z_(s).status;

            // [BENCH MEASURE] log phase_current + pos_error per poll for obstacle
            // threshold tuning. Compare values across:
            //   - normal extend (motor moving freely)
            //   - cup sealed against wall (still under load but at target)
            //   - obstacle stuck (pos_error accumulates, current spikes)
            // Remove or gate behind env var once thresholds are determined.
            std::cout << "[obstacle_meas ZDT:" << s
                      << "] t=" << elapsed
                      << "ms pos=" << st.real_pos
                      << "° err=" << st.pos_error
                      << "° spd=" << st.real_speed
                      << "rpm I=" << st.phase_current
                      << "mA p=" << p << "kPa\n";

            if (st.stall_flag) {
                // Cup hit wall — defer flag release (cycle_group_ releases after
                // vacuum check). Treat as success.
                std::cout << "[extend ZDT:" << s << "] STALL at " << elapsed
                          << "ms, pos=" << st.real_pos << "° err=" << st.pos_error
                          << "° I=" << st.phase_current << "mA — DEFER stall release\n";
                done[i] = true;
                ++n_done;
                continue;
            }

            // 3. Stability check — naturally reached target or held position
            const bool speed_ok = std::fabs(st.real_speed) <= SPEED_THRESHOLD_RPM;
            const bool pos_ok   = std::fabs(st.real_pos - prev_pos[i]) <= POS_DELTA_DEG;
            prev_pos[i] = st.real_pos;
            if (speed_ok && pos_ok) ++stable[i]; else stable[i] = 0;
            if (stable[i] >= STABLE_COUNT_NEED) {
                std::cout << "[extend ZDT:" << s << "] STABLE done at " << elapsed
                          << "ms, pos=" << st.real_pos << "° err=" << st.pos_error
                          << "° I=" << st.phase_current << "mA\n";
                done[i] = true;
                ++n_done;
            }
        }
    }

    if (driver_dbg_) for (int s : slaves) Z_(s).set_debug(true);
    sleep_ms_(PUSHER_SETTLE_MS);

    if (n_done < (int)slaves.size()) {
        std::cout << "[extend group] TIMEOUT after " << timeout_ms << "ms, "
                  << n_done << "/" << slaves.size() << " resolved\n";
        return true;
    }
    return false;
}

// === Disable-seal extend ===
// Two-phase extend with ZDT disable trick to let cup self-position under
// vacuum suction (LEYG25 is back-drivable when motor disabled). Replaces
// vacuum-early-stop logic that suffered from poll-rate vs motion-rate mismatch.
//
// Per-slave state machine:
//   PHASE1_FAST  → motor extends at fast_rpm to (target - PHASE1_BUFFER_PULSES)
//   PHASE2_SLOW  → motor extends at PUSHER_RPM_DISABLE_SLOW toward (target + 2cm cap),
//                  poll: vacuum / phase_current / pos_error / stall
//   WAIT_SEAL    → motor disabled, poll vacuum waiting for SEAL_DEEP
//   RETRY_PUSH   → re-enabled, slow push +0.5cm, then back to WAIT_SEAL
//   DONE         → final position recorded
bool WashRobot::pusher_extend_with_disable_seal_(const std::vector<int>& slaves,
                                                   const std::vector<int>& target_pulses,
                                                   int fast_rpm,
                                                   int acc,
                                                   bool* any_obstacle_out) {
    if (any_obstacle_out) *any_obstacle_out = false;   // default-clear so caller doesn't need to pre-init
    if (slaves.empty()) return false;
    if (slaves.size() != target_pulses.size()) {
        std::cout << "[disable_seal] size mismatch slaves=" << slaves.size()
                  << " targets=" << target_pulses.size() << "\n";
        return true;
    }

    const int N = (int)slaves.size();
    std::vector<bool> done(N, false);
    std::vector<bool> obstacle(N, false);
    std::vector<bool> weak_seal(N, false);
    std::vector<int>  final_pulse(N, 0);
    // max_reached[i]: deepest (largest) pulse position cup i has ever reached
    // (seeded after Phase 1, updated every Step D poll, persists across iters).
    // Used by the path-A obstacle check: the wall can't move closer, so a cup
    // jamming SHORTER than a depth it already cleared = a new obstacle.
    std::vector<int>  max_reached(N, 0);
    // 2026-05-18: endpoint_stalled[i] — set when cup i hits an endpoint stall
    // (progress ≥ STALL_ENDPOINT_RATIO = cup physically against wall, can't
    // advance further). Once set, subsequent iters SKIP pushing this cup (no
    // target increment) — it just stays at its stalled position and waits for
    // vacuum in Step F. Without this, the iter loop kept incrementing target
    // and jamming an already-walled cup → current spike → false OBSTACLE abort.
    std::vector<bool> endpoint_stalled(N, false);

    // First-obstacle-abort flag (2026-05-15h4): when any cup hits obstacle during
    // Step D, set this flag — Step D loop will emergency_stop remaining pushing
    // cups, skip Step D.5/E/F, and break out of the iter for-loop. Rationale:
    // ZDT body/feet groups physically move together, so partial sealing on the
    // not-obstructed cups isn't useful — they'll be released anyway during the
    // outer cycle_group_ rescue (valve off + retract all + rail backup + retry).
    // Aborting early saves the time wasted on cup pushes that will be undone.
    bool early_abort_obstacle = false;

    // Per-slave real_pos snapshot taken BEFORE each iter's Step C push, used by
    // Step D STALL path to compute "push progress" ratio. If stall happens with
    // progress < STALL_ENDPOINT_RATIO (cup didn't move much vs expected) → cup
    // is blocked by something (hard obstacle), trigger early abort. If progress
    // ≥ ratio → cup reached its target then stalled (normal endpoint contact
    // against wall), defer as before. (2026-05-15h5)
    std::vector<double> pre_iter_pos(N, 0.0);
    // intended_target[i]: per-iter absolute target (in encoder pulse frame).
    // Initialized to phase1_targets[i] after Phase 1, then incremented by INCR_PULSE
    // per iter in Step C. Sent as absolute (mode=1) so motor always tries to reach
    // the in-memory target regardless of stall / back-drive during disable wait.
    std::vector<int>  intended_target(N, 0);

    // Pre-clear stall flags
    for (int s : slaves) Z_(s).release_stall_flag();

    // 1 pos_mode pulse = 0.1 deg encoder (bench-verified, see deg→pulse comment)
    auto deg_to_pulse = [](double deg) -> int { return (int)(deg * 10.0); };

    if (driver_dbg_) for (int s : slaves) Z_(s).set_debug(false);

    // ---------- Phase 1: fast extend to (target - PHASE1_BUFFER_PULSES) ----------
    std::vector<int> phase1_targets(N, 0);
    for (int i = 0; i < N; ++i) {
        phase1_targets[i] = std::max(0, target_pulses[i] - PHASE1_BUFFER_PULSES);
    }
    std::cout << "[disable_seal] Phase 1 fast extend, slaves={";
    for (int i = 0; i < N; ++i) { if (i) std::cout << ","; std::cout << slaves[i]; }
    std::cout << "}\n";

    for (int i = 0; i < N; ++i) {
        if (Z_(slaves[i]).motion_control_pos_mode_nowait(0, acc, fast_rpm,
                                                          phase1_targets[i], 1, 1, 1)) {
            std::cout << "[disable_seal] Phase 1 pos_mode FAIL slave=" << slaves[i] << "\n";
            return true;
        }
    }
    Z_(slaves.front()).trigger_sync_move();

    // [2026-05-28] Parallel wait: slaves are broadcast-sync triggered → all
    // move simultaneously, so waiting in parallel = max(slave time) instead of
    // sum. Sequential wait was paying ~1800ms (slowest slave) × N when slaves
    // finish near-simultaneously; parallel cuts to ~1800ms total. Per-slave
    // stall_flag release is now done after the helper returns (helper handles
    // defer_stall internally — leaves flag set when defer=true).
    // [2026-05-29] Capture per-slave Phase 1 peakI to detect "already at wall"
    // cases. If Phase 1 fast extend (700rpm) ran into the wall, peakI spikes
    // (observed 1500-2000mA vs 600-800mA normal travel). Such cups should skip
    // iter 0 push entirely — they're already pressed against wall.
    std::vector<uint16_t> phase1_peak_I(N, 0);
    if (zdt_wait_motion_done_many_(slaves, 10000, /*defer_stall=*/true, nullptr, &phase1_peak_I)) {
        std::cout << "[disable_seal] Phase 1 wait fail (timeout / non-defer stall) — continuing\n";
    }
    for (int s : slaves) Z_(s).release_stall_flag();

    // [2026-05-29] Phase 1 wall detection: cup pushed at 700rpm with peakI past
    // DISABLE_PHASE_CURRENT_LIMIT_MA almost certainly contacted the wall during
    // Phase 1. Mark these as endpoint_stalled — iter 0 below will skip their push
    // (going straight to WAIT_SEAL vacuum check). Saves the ~1s iter 0 slow push
    // for cups already at the wall + reduces cumulative wall-press stress on cup.
    for (size_t i = 0; i < N; ++i) {
        if (phase1_peak_I[i] >= DISABLE_PHASE_CURRENT_LIMIT_MA) {
            endpoint_stalled[i] = true;
            std::cout << "[disable_seal:" << slaves[i] << "] Phase 1 already at wall"
                      << " (peakI=" << phase1_peak_I[i] << "mA >= "
                      << DISABLE_PHASE_CURRENT_LIMIT_MA
                      << ") — skip iter 0 push, wait vacuum only\n";
        }
    }

    // Initialize intended_target to phase1 endpoint — first Step C iter increments
    // to phase1+INCR_PULSE, second iter to phase1+2*INCR_PULSE, etc.
    for (int i = 0; i < N; ++i) intended_target[i] = phase1_targets[i];

    // Seed max_reached with each cup's actual end-of-Phase-1 position so the
    // cross-iter regression check has a baseline from iter 0.
    for (int i = 0; i < N; ++i) {
        if (Z_(slaves[i]).get_system_status() == false)
            max_reached[i] = deg_to_pulse(Z_(slaves[i]).status.real_pos);
    }

    // ---------- Phase 2: iterative push-disable-wait ----------
    // 每個 iter：
    //   Step A: re-enable not-done slaves
    //   Step B: 讀真空 — 若已密封（前次 wait 中達標）→ mark DONE
    //   Step C: 對未 DONE slaves 送 absolute push（intended_target += INCR_PULSE，slow rpm，sync trigger）
    //   Step D: 等所有 push motion done（含 obstacle/stall 偵測）
    //   Step D.5: holding 緩衝 DISABLE_PRE_DISABLE_DELAY_MS — 馬達還出力時讓 cup 與牆面接觸建立
    //   Step E: emergency_stop + disable not-done slaves
    //   Step F: 等真空達 SEAL_DEEP — 期間 poll，達標即 mark DONE
    //   loop back if any not-done remain
    //
    // 每次 push 是「短推 → 緩衝 → disable → 等真空」，避免連續慢推造成 cup 過度擠壓 +
    // 反作用力拉壞另一組 cup。absolute target 累加（不是 relative）— 即使前次 stall
    // 沒推到位 / disable 期間 encoder 飄走，下次 push 會把馬達拉回到設計位置。
    const int MAX_ITERS = DISABLE_RETRY_MAX_ITERS;
    const int INCR_PULSE = DISABLE_RETRY_INCR_PULSE;
    const int wait_seal_ms = VACUUM_DEEPEN_TIMEOUT_MS;

    // +1 iter for initial vacuum check before any push (in case Phase 1 already sealed cup)
    for (int iter = 0; iter <= MAX_ITERS; ++iter) {
        // [2026-05-29] Per-iter peak push current per slave. Used after Step D
        // to fast-skip WAIT_SEAL on slaves whose peakI never crossed
        // DISABLE_LOW_CONTACT_PEAK_MA (cup in free air, no contact).
        std::vector<uint16_t> peak_I_iter(N, 0);

        // Step A: clear stall flags + re-enable not-done slaves (with retry).
        // Defensive: previous iter's Step D timeout / Step E emergency_stop may
        // have latched stall_flag — firmware silently rejects pos_mode if set.
        // motion_control_driver_EN can also fail silently (Modbus comm fail);
        // explicit return-check + retry covers that case (observed 2026-05-06:
        // slaves 6,7,8 iter 2 pos_mode FAIL with no stall — likely EN never
        // engaged after Step E disable).
        for (int i = 0; i < N; ++i) {
            if (done[i]) continue;
            Z_(slaves[i]).release_stall_flag();
            if (Z_(slaves[i]).motion_control_driver_EN(true)) {
                std::cout << "[disable_seal:" << slaves[i] << "] iter " << iter
                          << " Step A EN re-enable FAIL — retry\n";
                sleep_ms_(50);
                if (Z_(slaves[i]).motion_control_driver_EN(true)) {
                    std::cout << "[disable_seal:" << slaves[i] << "] iter " << iter
                              << " Step A EN re-enable FAIL again (continuing)\n";
                }
            }
        }
        sleep_ms_(200);   // longer settle (was 80ms) — firmware needs time after re-enable

        // Step B: read pressure on all not-done; if already sealed, mark DONE
        for (int i = 0; i < N; ++i) {
            if (done[i]) continue;
            int p = read_pressure_(slaves[i]);
            const bool p_ok = (M_(slaves[i]).error_flag == 0);
            if (p_ok && p <= VACUUM_SEAL_DEEP_KPA) {
                if (Z_(slaves[i]).get_system_status() == false) {
                    final_pulse[i] = deg_to_pulse(Z_(slaves[i]).status.real_pos);
                }
                done[i] = true;
                std::cout << "[disable_seal:" << slaves[i] << "] iter " << iter
                          << " SEALED (pre-push check) p=" << p << "kPa pulse="
                          << final_pulse[i] << "\n";
            }
        }

        // Check exit condition: all done OR reached MAX_ITERS
        bool any_left = false;
        for (int i = 0; i < N; ++i) if (!done[i]) { any_left = true; break; }
        if (!any_left) break;
        if (iter >= MAX_ITERS) break;   // 別再 push、跳出讓收尾處理 weak_seal

        // Snapshot real_pos before push so Step D STALL path can compute progress
        // ratio. Failure to read leaves entry as previous value (or 0 on first iter)
        // → progress comparison degrades to "no info, treat as endpoint" via ratio
        // calc (safe fallback to defer behavior).
        for (int i = 0; i < N; ++i) {
            if (done[i]) continue;
            if (Z_(slaves[i]).get_system_status() == false) {
                pre_iter_pos[i] = Z_(slaves[i]).status.real_pos;
            }
        }

        // Step C: increment intended_target by INCR_PULSE and send absolute push.
        //         Skip slaves whose accumulated overshoot (intended_target - phase1)
        //         already hit DISABLE_RETRY_MAX_OVEREXTEND — those are weak_seal.
        std::vector<int> pushing;
        for (int i = 0; i < N; ++i) {
            if (done[i]) continue;
            // 2026-05-18: endpoint-stalled cup is physically against the wall —
            // skip pushing it (no target increment). It stays put; Step F still
            // polls its vacuum each iter. If it never seals, the MAX_ITERS
            // wrap-up marks it weak_seal. Avoids jamming → false OBSTACLE.
            if (endpoint_stalled[i]) {
                std::cout << "[disable_seal:" << slaves[i] << "] iter " << iter
                          << " skip push (endpoint-stalled, at wall) — wait vacuum only\n";
                continue;
            }
            const int accumulated = intended_target[i] - phase1_targets[i];
            if (accumulated >= DISABLE_RETRY_MAX_OVEREXTEND) {
                if (Z_(slaves[i]).get_system_status() == false) {
                    final_pulse[i] = deg_to_pulse(Z_(slaves[i]).status.real_pos);
                }
                weak_seal[i] = true;
                done[i] = true;
                std::cout << "[disable_seal:" << slaves[i] << "] WEAK SEAL cap (+"
                          << accumulated << " pulses past phase1), pulse=" << final_pulse[i] << "\n";
                evt_("weak_seal slave=" + std::to_string(slaves[i]));
                continue;
            }
            // Bump intended target by INCR_PULSE — next iter will advance by another INCR_PULSE.
            intended_target[i] += INCR_PULSE;
            if (Z_(slaves[i]).motion_control_pos_mode_nowait(
                    /*fwd*/0, acc, PUSHER_RPM_DISABLE_SLOW,
                    intended_target[i], /*absolute*/1, /*sync*/1, /*retry*/2)) {
                // pos_mode FAIL — print status diagnostic to identify cause
                // (EN bit / stall_flag / position-error / phase-current).
                std::string status_info = "status_unread";
                if (Z_(slaves[i]).get_system_status() == false) {
                    const auto& st = Z_(slaves[i]).status;
                    std::ostringstream oss;
                    oss << "en=" << st.is_enabled
                        << " stall=" << st.stall_flag
                        << " pos=" << st.real_pos << "°"
                        << " posErr=" << st.pos_error << "°"
                        << " I=" << st.phase_current << "mA";
                    status_info = oss.str();
                }
                std::cout << "[disable_seal:" << slaves[i] << "] iter " << iter
                          << " push pos_mode FAIL — " << status_info << "\n";
                evt_("disable_seal_push_fail slave=" + std::to_string(slaves[i])
                     + " " + status_info);
                weak_seal[i] = true;
                done[i] = true;
                continue;
            }
            pushing.push_back(slaves[i]);
            std::cout << "[disable_seal:" << slaves[i] << "] iter " << iter
                      << " push absolute target=" << intended_target[i]
                      << " (cum +" << (intended_target[i] - phase1_targets[i])
                      << " past phase1)\n";
        }
        if (pushing.empty()) continue;   // all slaves hit cap or send-fail in this iter

        Z_(pushing.front()).trigger_sync_move();

        // Step D: wait for each push to finish (with obstacle / stall detection)
        for (int s : pushing) {
            int idx = -1;
            for (int i = 0; i < N; ++i) if (slaves[i] == s) { idx = i; break; }
            if (idx < 0) continue;

            // First-obstacle-abort: if a previous slave in this iter already hit
            // obstacle, emergency_stop this still-pushing one and skip its wait
            // loop. Don't mark obstacle/done — let post-loop cleanup decide its
            // state. The whole group's about to be released by cycle_group_
            // rescue anyway.
            if (early_abort_obstacle) {
                Z_(s).emergency_stop(false);
                std::cout << "[disable_seal:" << s << "] iter " << iter
                          << " abort-stop (sibling obstacle, skipping wait)\n";
                continue;
            }

            const int wait_max_ms = 5000;
            int wait_e = 0;
            bool finished = false;
            int  poll_count = 0;
            uint16_t peak_I = 0;   // peak phase_current observed during this push
            while (wait_e < wait_max_ms) {
                sleep_ms_(50);
                wait_e += 50;
                if (Z_(s).get_system_status()) continue;
                const auto& st = Z_(s).status;

                // Diagnostic: log live phase current every ~200ms (every 4 polls)
                // so bench can see the current curve during a push — useful for
                // calibrating Clog_Ma / DISABLE_PHASE_CURRENT_LIMIT_MA thresholds.
                poll_count++;
                if (st.phase_current > peak_I) {
                    peak_I = st.phase_current;
                    peak_I_iter[idx] = peak_I;   // expose to post-Step-D fast-skip logic
                }
                {
                    const int cur_pulse = deg_to_pulse(st.real_pos);
                    if (cur_pulse > max_reached[idx]) max_reached[idx] = cur_pulse;
                }
                if (poll_count % 4 == 0) {
                    std::cout << "[disable_seal:" << s << "] iter " << iter
                              << " push I=" << st.phase_current << "mA"
                              << " pos=" << st.real_pos << "°"
                              << " posErr=" << st.pos_error << "°"
                              << " spd=" << st.real_speed << "rpm\n";
                }

                // Obstacle path A: phase current over threshold.
                // A current spike just means the motor jammed — it can't alone
                // tell a real obstacle from a normal cup-pressed-against-wall
                // push. Two discriminators decide:
                //   (1) regressed — jammed SHORTER than a depth this cup already
                //       reached earlier. The wall can't move closer, so a new
                //       blockage is the only explanation = obstacle. Catches
                //       obstacles near full extension that (2) alone would miss.
                //   (2) position gate — jammed far short of preset = obstacle;
                //       jammed near preset (and not regressed) = pressed the
                //       WALL (intended endpoint), defer.
                if (st.phase_current > DISABLE_PHASE_CURRENT_LIMIT_MA) {
                    const uint16_t trig_I = st.phase_current;   // capture before re-read
                    Z_(s).emergency_stop(false);
                    sleep_ms_(30);
                    Z_(s).get_system_status();
                    final_pulse[idx] = deg_to_pulse(Z_(s).status.real_pos);

                    const int regress_margin = cm_to_pulses_for_slave_(s, OBSTACLE_REGRESS_MARGIN_CM);
                    const bool regressed = (final_pulse[idx] < max_reached[idx] - regress_margin);
                    const int preset_pulse  = preset_extend_pulse_for_slave_(s);
                    const int endpoint_gate = preset_pulse
                                            - cm_to_pulses_for_slave_(s, OBSTACLE_ENDPOINT_GATE_CM);
                    const bool near_preset  = (final_pulse[idx] >= endpoint_gate);

                    if (near_preset && !regressed) {
                        // Near full extension, no regression — jammed against
                        // the WALL (intended endpoint). Defer (Step E disables
                        // EN, Step F lets vacuum build), NOT obstacle.
                        Z_(s).release_stall_flag();
                        endpoint_stalled[idx] = true;
                        std::cout << "[disable_seal:" << s << "] iter " << iter
                                  << " WALL I=" << trig_I << "mA peakI=" << peak_I
                                  << "mA pulse=" << final_pulse[idx] << " >= gate "
                                  << endpoint_gate << " maxReached=" << max_reached[idx]
                                  << " — endpoint, not obstacle\n";
                        finished = true;
                        break;
                    }
                    obstacle[idx] = true;
                    done[idx] = true;
                    std::cout << "[disable_seal:" << s << "] iter " << iter
                              << " OBSTACLE I=" << trig_I << "mA peakI=" << peak_I
                              << "mA pulse=" << final_pulse[idx]
                              << " (regressed=" << regressed
                              << " maxReached=" << max_reached[idx]
                              << " gate=" << endpoint_gate << ")\n";
                    evt_("obstacle_detected slave=" + std::to_string(s));
                    finished = true;
                    early_abort_obstacle = true;   // signal sibling-push stop + iter break
                    break;
                }
                // Stall — distinguish endpoint stall (cup hit wall, expected) from
                // early stall (cup blocked mid-push, obstacle).
                //   actual_delta = real_pos - pre_iter_pos  (deg of progress this iter)
                //   expected_delta = INCR_PULSE * 0.1       (1 cmd-pulse = 0.1° encoder)
                //   progress = actual / expected
                //     ≥ STALL_ENDPOINT_RATIO → endpoint stall, defer (existing behavior)
                //     < ratio                → early stall, treat as obstacle + abort
                // (2026-05-15h5 per user spec: STALL+進度<80% 視為 obstacle)
                if (st.stall_flag) {
                    Z_(s).emergency_stop(false);
                    sleep_ms_(30);
                    Z_(s).get_system_status();
                    final_pulse[idx] = deg_to_pulse(Z_(s).status.real_pos);
                    Z_(s).release_stall_flag();

                    const double actual_delta   = std::fabs(Z_(s).status.real_pos - pre_iter_pos[idx]);
                    const double expected_delta = (double)INCR_PULSE * 0.1;   // INCR_PULSE 3000 → 300°
                    const double progress       = (expected_delta > 0.1)
                                                  ? (actual_delta / expected_delta) : 1.0;
                    if (progress < STALL_ENDPOINT_RATIO) {
                        obstacle[idx] = true;
                        done[idx]     = true;
                        std::cout << "[disable_seal:" << s << "] iter " << iter
                                  << " STALL+EARLY actual=" << actual_delta
                                  << "° expected=" << expected_delta
                                  << "° progress=" << progress
                                  << " < " << STALL_ENDPOINT_RATIO
                                  << " peakI=" << peak_I << "mA → OBSTACLE (abort)\n";
                        evt_("obstacle_detected slave=" + std::to_string(s) + " path=stall_early");
                        finished = true;
                        early_abort_obstacle = true;
                        break;
                    }
                    // Endpoint stall — cup is physically against the wall.
                    // Mark so subsequent iters don't push it further (would
                    // just jam → false OBSTACLE). It stays here, waits vacuum.
                    endpoint_stalled[idx] = true;
                    std::cout << "[disable_seal:" << s << "] iter " << iter
                              << " STALL pos=" << Z_(s).status.real_pos
                              << "° (defer endpoint, progress=" << progress
                              << ", peakI=" << peak_I << "mA — will not re-push)\n";
                    finished = true;
                    break;
                }
                // Stable
                if (std::fabs(st.real_speed) < 5.0) {
                    std::cout << "[disable_seal:" << s << "] iter " << iter
                              << " push stable — I=" << st.phase_current << "mA"
                              << " peakI=" << peak_I << "mA"
                              << " pos=" << st.real_pos << "°\n";
                    finished = true;
                    break;
                }
            }
            if (!finished) {
                Z_(s).emergency_stop(false);
                std::cout << "[disable_seal:" << s << "] iter " << iter
                          << " push timeout — peakI=" << peak_I << "mA\n";
            }
        }

        // First-obstacle-abort: any cup hit obstacle in Step D → skip Step D.5/E/F
        // and exit the iter loop. cycle_group_'s rescue will release valve, retract
        // all, rail-backup 10cm, and retry — partial seal on the un-obstructed cups
        // would be undone anyway. Pre-emptive emergency_stop on all not-done cups
        // ensures no cup keeps pushing while rescue runs.
        if (early_abort_obstacle) {
            for (int i = 0; i < N; ++i) {
                if (!done[i]) Z_(slaves[i]).emergency_stop(false);
            }
            std::cout << "[disable_seal] iter " << iter
                      << " obstacle abort — exiting iter loop early (rescue will handle)\n";
            break;
        }

        // Step D.5: holding 緩衝 — push 完馬達還在 holding 出力，給 cup 一點時間
        // 接觸牆面建立初步密封，再切 disable EN。少了這段，馬達一推完立刻斷電 →
        // cup 在剛接觸牆面那刻就失去 holding 力，可能彈離。
        sleep_ms_(DISABLE_PRE_DISABLE_DELAY_MS);

        // Step E: emergency_stop + disable all not-done slaves
        for (int i = 0; i < N; ++i) {
            if (done[i]) continue;
            Z_(slaves[i]).emergency_stop(false);
        }
        sleep_ms_(50);
        for (int i = 0; i < N; ++i) {
            if (done[i]) continue;
            Z_(slaves[i]).motion_control_driver_EN(false);
        }

        // Step F: wait for vacuum to deepen on each not-done slave (poll, mark
        // done as they seal). Per-cup trend early-out: a cup whose vacuum stops
        // deepening for VACUUM_PLATEAU_MS is "not progressing this iter" — stop
        // waiting on it (don't burn the full timeout). A cup still deepening
        // keeps resetting its plateau timer → keeps full grace up to wait_seal_ms.
        // Lets a genuinely-stuck cup reach weak_seal / L2 retry far sooner
        // without false-failing slow-but-OK cups.
        std::cout << "[disable_seal] iter " << iter << " WAIT_SEAL phase ("
                  << wait_seal_ms << "ms timeout)\n";
        int wait_e = 0;
        std::vector<int>  best_p(N, 9999);          // deepest (most-neg) kPa seen this iter
        std::vector<int>  last_improve_ms(N, 0);
        std::vector<bool> plateaued(N, false);      // vacuum stalled this iter
        // [2026-06-06] Per-slave read accounting + last raw value:
        // distinguish "cup not sealing" (real -1kPa reads) from "JC100 stale/error
        // making us think cup not sealing" (lots of error_flag skips, best_p stuck).
        std::vector<int>  read_ok_cnt(N, 0);
        std::vector<int>  read_err_cnt(N, 0);
        std::vector<int>  last_raw_p(N, 9999);      // most recent value returned by read_pressure_

        // [2026-05-29] peakI fast-skip: cup whose push never crossed
        // DISABLE_LOW_CONTACT_PEAK_MA clearly didn't contact anything (free air).
        // Mark plateaued immediately so the WAIT_SEAL loop skips it — no point
        // polling vacuum on a cup that didn't even touch a surface.
        // Logged so user can correlate with peakI from "push stable" line.
        for (int i = 0; i < N; ++i) {
            if (done[i]) continue;
            if (endpoint_stalled[i]) continue;   // endpoint cups already at wall
            if (peak_I_iter[i] > 0 && peak_I_iter[i] < DISABLE_LOW_CONTACT_PEAK_MA) {
                plateaued[i] = true;
                std::cout << "[disable_seal:" << slaves[i] << "] iter " << iter
                          << " peakI=" << peak_I_iter[i] << "mA < "
                          << DISABLE_LOW_CONTACT_PEAK_MA
                          << "mA — no contact evidence, skip WAIT_SEAL\n";
            }
        }
        // If all not-done slaves got fast-skipped, exit WAIT_SEAL immediately.
        {
            bool any_to_poll = false;
            for (int i = 0; i < N; ++i) if (!done[i] && !plateaued[i]) { any_to_poll = true; break; }
            if (!any_to_poll) {
                std::cout << "[disable_seal] iter " << iter
                          << " WAIT_SEAL skipped entirely (all not-done slaves fast-skipped)\n";
                // Skip the polling loop below — fall through to wrap-up logic.
                continue;   // jump to next iter directly
            }
        }

        // [2026-06-06] Poll interval 100→200ms — disable_seal 跟 cmd_status 共用
        // cli_22_ bus，100ms × 4 slaves = 40 reads/s 持續多秒會把 gateway buffer
        // 灌爆 → 連環 JC100 TIMEOUT。200ms × 4 = 20 reads/s 給 PQW / DM2J:14 /
        // cmd_status fresh-read 喘息空間。代價：seal 偵測響應慢 100ms（每 iter
        // 多 100ms × 平均 wait_e≈1s = 整體 disable_seal 多 ~5%）。
        constexpr int WAIT_SEAL_POLL_MS = 200;
        while (wait_e < wait_seal_ms) {
            sleep_ms_(WAIT_SEAL_POLL_MS);
            wait_e += WAIT_SEAL_POLL_MS;
            for (int i = 0; i < N; ++i) {
                if (done[i] || plateaued[i]) continue;
                int p = read_pressure_(slaves[i]);
                last_raw_p[i] = p;
                if (M_(slaves[i]).error_flag != 0) { read_err_cnt[i]++; continue; }
                read_ok_cnt[i]++;
                if (p <= VACUUM_SEAL_DEEP_KPA) {
                    Z_(slaves[i]).motion_control_driver_EN(true);
                    sleep_ms_(50);
                    if (Z_(slaves[i]).get_system_status() == false) {
                        final_pulse[i] = deg_to_pulse(Z_(slaves[i]).status.real_pos);
                    }
                    done[i] = true;
                    std::cout << "[disable_seal:" << slaves[i] << "] SEALED iter=" << iter
                              << " wait=" << wait_e << "ms p=" << p
                              << "kPa pulse=" << final_pulse[i] << "\n";
                    continue;
                }
                // Trend: vacuum deepened ≥ EPSILON below the best-so-far →
                // reset plateau timer; else check two plateau exit conditions:
                //   (a) [2026-05-28] No-contact fast-skip: best_p still ≥ -5kPa
                //       after 500ms → cup almost certainly not in contact, no
                //       need to wait the full VACUUM_PLATEAU_MS.
                //   (b) Standard plateau: stalled past VACUUM_PLATEAU_MS.
                if (p <= best_p[i] - VACUUM_PROGRESS_EPSILON_KPA) {
                    best_p[i]          = p;
                    last_improve_ms[i] = wait_e;
                } else {
                    // [2026-06-08] fast_skip 不適用 endpoint_stalled cup：peakI>1200
                    // 已證實撞牆，no-contact 假設不成立。此 cup 只是 vacuum 抽得慢
                    // （觀察 slave 5/6 慢吸 case：peakI=1225 撞牆但 vacuum 1 秒只到
                    // -1，fast_skip 砍掉誤判 weak；給足 5 秒 slave 6 在 3200ms 就 SEAL）。
                    // slow_plateau 仍適用 — 真的整段 WAIT_SEAL 都沒進步是 hardware
                    // 漏氣，不是抽得慢。
                    bool fast_skip = (!endpoint_stalled[i]
                                       && wait_e >= VACUUM_NO_CONTACT_FAST_MS
                                       && best_p[i] >= VACUUM_NO_CONTACT_KPA);
                    bool slow_plateau = (wait_e - last_improve_ms[i] >= VACUUM_PLATEAU_MS);
                    if (fast_skip || slow_plateau) {
                        plateaued[i] = true;
                        const char* reason = fast_skip ? "no contact" : "no progress";
                        std::cout << "[disable_seal:" << slaves[i] << "] iter " << iter
                                  << " vacuum plateau p=" << p << "kPa best=" << best_p[i]
                                  << "kPa (" << reason << " " << wait_e << "ms"
                                  << " reads ok=" << read_ok_cnt[i]
                                  << " err=" << read_err_cnt[i]
                                  << ") — stop waiting this iter\n";
                        // Endpoint cup (already against the wall) + vacuum plateau
                        // = this wall spot genuinely can't seal (e.g. a seam/gap).
                        // An endpoint cup is NOT pushed again in later iters, so the
                        // verdict cannot change → mark weak_seal now instead of
                        // dragging it through the remaining iters. Frees the iter
                        // loop to finish and hand off to cycle_group_'s L2 retry,
                        // which moves the rail to a fresh wall spot — the real fix.
                        if (endpoint_stalled[i]) {
                            // Re-enable the driver EN (Step E disabled it) + lock
                            // position — same as the SEALED path and the MAX_ITERS
                            // wrap-up. Without this the cup exits disable_seal with
                            // EN OFF; the next pos_mode (cycle_group_ retract retry)
                            // is then silently rejected by ZDT firmware → pos_mode
                            // FAIL. (marking done WITHOUT this also makes the
                            // wrap-up's !done re-enable skip it.)
                            Z_(slaves[i]).motion_control_driver_EN(true);
                            sleep_ms_(80);
                            if (Z_(slaves[i]).get_system_status() == false)
                                final_pulse[i] = deg_to_pulse(Z_(slaves[i]).status.real_pos);
                            // [2026-06-06] Fresh-read rescue before declaring weak_seal:
                            // wait 200ms for vacuum to build, re-read JC100. If now deep,
                            // the polling missed it (cli_22_ stale read / slow JC100
                            // response). Demote weak_seal → SEAL.
                            sleep_ms_(200);
                            int fresh_p = read_pressure_(slaves[i]);
                            int errf    = M_(slaves[i]).error_flag;
                            if (!errf && fresh_p <= VACUUM_SEAL_DEEP_KPA) {
                                done[i] = true;
                                std::cout << "[disable_seal:" << slaves[i] << "] iter " << iter
                                          << " RESCUED at wall — fresh_p=" << fresh_p
                                          << "kPa <= " << VACUUM_SEAL_DEEP_KPA
                                          << " → SEAL not weak_seal (polling missed it,"
                                          " likely cli_22_ stale read)\n";
                            } else {
                                weak_seal[i] = true;
                                done[i]      = true;
                                std::cout << "[disable_seal:" << slaves[i] << "] iter " << iter
                                          << " at wall + vacuum can't seal (p=" << p
                                          << "kPa fresh_p=" << fresh_p
                                          << (errf ? " READ_ERR" : "")
                                          << ") — weak_seal early, skip remaining iters\n";
                                evt_("weak_seal slave=" + std::to_string(slaves[i]));
                            }
                        }
                    }
                }
            }
            bool all_resolved = true;
            for (int i = 0; i < N; ++i) if (!done[i] && !plateaued[i]) { all_resolved = false; break; }
            if (all_resolved) break;
        }
    }

    // 收尾：仍有 not-done → weak_seal、強制 re-enable + 鎖位置
    for (int i = 0; i < N; ++i) {
        if (!done[i]) {
            Z_(slaves[i]).motion_control_driver_EN(true);
            sleep_ms_(80);
            if (Z_(slaves[i]).get_system_status() == false) {
                final_pulse[i] = deg_to_pulse(Z_(slaves[i]).status.real_pos);
            }
            // [2026-06-06] Fresh-read rescue (parallel to endpoint+plateau path).
            // Wait 200ms for vacuum to fully build, re-read. If deep → demote
            // weak_seal to SEAL. Otherwise mark weak_seal as before.
            sleep_ms_(200);
            int fresh_p = read_pressure_(slaves[i]);
            int errf    = M_(slaves[i]).error_flag;
            if (!errf && fresh_p <= VACUUM_SEAL_DEEP_KPA) {
                done[i] = true;
                std::cout << "[disable_seal:" << slaves[i]
                          << "] RESCUED MAX_ITERS — pulse=" << final_pulse[i]
                          << " fresh_p=" << fresh_p << "kPa <= "
                          << VACUUM_SEAL_DEEP_KPA
                          << " → SEAL not weak_seal (polling missed it,"
                          " likely cli_22_ stale read or slow JC100 response)\n";
            } else {
                weak_seal[i] = true;
                done[i] = true;
                std::cout << "[disable_seal:" << slaves[i]
                          << "] WEAK SEAL after MAX_ITERS, pulse=" << final_pulse[i]
                          << " fresh_p=" << fresh_p << "kPa"
                          << (errf ? " (READ_ERR — stale)" : "")
                          << "\n";
                evt_("weak_seal slave=" + std::to_string(slaves[i]));
            }
        }
    }

    if (driver_dbg_) for (int s : slaves) Z_(s).set_debug(true);

    // Record final pulse to last_seal_pulse_
    // [2026-06-05] Snowball protection (fix A): skip WEAK_SEAL slaves so a
    // cup that pushed all the way to physical end-stop without sealing doesn't
    // poison last_seal_pulse_ for the next step's target calculation. Truly
    // sealed cups still update normally. Combined with fix B/C this lets the
    // system retreat to preset after a snowball/cap hit rather than oscillating.
    for (int i = 0; i < N; ++i) {
        if (weak_seal[i]) {
            std::cout << "[snowball] WEAK_SEAL slave " << slaves[i]
                      << " pulse=" << final_pulse[i]
                      << " — NOT recording (keep prior last_seal_pulse_="
                      << last_seal_pulse_[slaves[i] - 1].load() << ")\n";
            continue;
        }
        record_seal_pulse_(slaves[i], final_pulse[i]);
    }

    // Aggregate per-slave obstacle flags into the optional output. Caller
    // (cycle_group_) uses this to decide whether to trigger obstacle rescue
    // (rail backup + re-extend) instead of falling through to vacuum_check.
    if (any_obstacle_out) {
        for (int i = 0; i < N; ++i) {
            if (obstacle[i]) { *any_obstacle_out = true; break; }
        }
    }

    sleep_ms_(PUSHER_SETTLE_MS);
    return false;
}

// Smart extend on a subset of slaves — same disable_seal pipeline as cycle_group_
// extend, usable from manual paths (cmd_pusher / cmd_zdt_pusher) so GUI EXTEND
// buttons match the auto step_down/up flow:
//   Phase 1: fast extend to (target − PHASE1_BUFFER_PULSES = preset − 1.5 cm)
//   Phase 2: iter loop (push +0.5 cm absolute → 200ms holding → disable EN
//            → wait up to 5s for vacuum to deepen → re-enable on seal)
//            up to DISABLE_RETRY_MAX_ITERS / +2.5 cm cap
//   final_pulse recorded into last_seal_pulse_ internally.
bool WashRobot::smart_extend_subset_(const std::string& group, const std::vector<int>& slaves) {
    if (slaves.empty()) return false;
    if (group != "feet" && group != "body" && group != "center") {
        std::cout << "[smart_extend] unknown group=" << group << "\n";
        return true;
    }

    // Build per-slave target pulses.
    //   feet : base = last_seal_pulse_ (learned seal position, persists)
    //   body : base = preset + feet_over delta  (2026-05-18 fix B1, TRIAL)
    // --- B1 fix rationale ---
    // Body target used to be `last_seal_pulse_body + feet_over`. But
    // record_seal_pulse_ stores the delta-adjusted seal position into
    // last_seal_pulse_body, so each step's feet_over got re-added on top of a
    // base that already contained prior feet_over → body target snowballed.
    // Fix: body base = stable preset (NOT drifting last_seal_pulse_), feet_over
    // applied exactly once per step. TRIAL — if bench shows body Phase 1 under-
    // shoots too much (preset far from real wall → excess iter-loop work),
    // REVERT to: `int target = last_seal_pulse_[s-1].load();` + the old body
    // `if` block. See changelog 2026-05-18g.
    std::vector<int> extend_pulses(slaves.size(), 0);
    for (size_t i = 0; i < slaves.size(); ++i) {
        const int s = slaves[i];
        int target;
        if (group == "body") {
            const double over_cm = last_feet_max_over_cm_.load();
            target = preset_extend_pulse_for_slave_(s)
                   + ((over_cm > 0) ? cm_to_pulses_for_slave_(s, over_cm) : 0);
        } else {
            // [2026-06-05] Snowball protection (fix C): cap feet target so
            // last_seal_pulse_ can't grow unbounded across steps.
            target = feet_target_capped_(s);
        }
        extend_pulses[i] = target;
    }

    const int extend_rpm = (group == "body") ? PUSHER_RPM_BODY_EXTEND : PUSHER_RPM;
    const int extend_acc = (group == "body") ? PUSHER_ACC_BODY_EXTEND : PUSHER_ACC;

    std::cout << "[smart_extend] " << group << " slaves={";
    for (size_t i = 0; i < slaves.size(); ++i) { if (i) std::cout << ","; std::cout << slaves[i]; }
    std::cout << "} target_pulses={";
    for (size_t i = 0; i < slaves.size(); ++i) { if (i) std::cout << ","; std::cout << extend_pulses[i]; }
    std::cout << "} (disable_seal mechanism)\n";

    // Clog_Ma firmware-write DISABLED (2026-05-19, per user): smart_extend no
    // longer lowers/restores the ZDT firmware 賭轉電流. Obstacle detection
    // relies purely on the SOFTWARE phase-current judgment inside
    // pusher_extend_with_disable_seal_ (DISABLE_PHASE_CURRENT_LIMIT_MA path A).
    // Firmware Clog_Ma stays at the operator-set driver value (3A default).
    // Block kept under #if 0 for easy re-enable.
#if 0
    // Clog_Ma RAII guard (2026-05-15h, per user: smart_extend also wants
    // gentle Clog during extend, no obstacle rescue logic — manual GUI path
    // expected to operate with operator watching). Setup BEFORE extend so the
    // very first push is already at gentle current; restore on ALL exit paths.
    for (int sl : slaves) {
        if (Z_(sl).set_clog_ma(CLOG_MA_GENTLE, /*store=*/false)) {
            std::cout << "[clog_guard] smart_extend slave " << sl
                      << " set GENTLE (" << CLOG_MA_GENTLE << "mA) FAIL — proceeding\n";
        }
    }
    std::cout << "[clog_guard] smart_extend " << group
              << " enter — Clog_Ma -> " << CLOG_MA_GENTLE << "mA (GENTLE) on "
              << slaves.size() << " slave(s)\n";
    auto clog_restore_fn = [this, slaves]() {
        for (int sl : slaves) {
            if (Z_(sl).set_clog_ma(CLOG_MA_NORMAL, /*store=*/false)) {
                std::cout << "[clog_guard] smart_extend slave " << sl
                          << " restore NORMAL (" << CLOG_MA_NORMAL << "mA) FAIL — manual check\n";
            }
        }
        std::cout << "[clog_guard] smart_extend exit — Clog_Ma -> "
                  << CLOG_MA_NORMAL << "mA (NORMAL) on " << slaves.size() << " slave(s)\n";
    };
    struct ScopeExit {
        std::function<void()> fn;
        ~ScopeExit() { if (fn) fn(); }
    } clog_guard{ clog_restore_fn };
#endif

    // disable_seal handles Phase 1 fast → Phase 2 iter loop internally.
    // any_obstacle_out passed but ignored — smart_extend (manual GUI path)
    // does NOT trigger obstacle rescue; obstacle still gets logged inside
    // disable_seal via "OBSTACLE" line + EVT obstacle_detected.
    bool any_obstacle = false;
    if (pusher_extend_with_disable_seal_(slaves, extend_pulses, extend_rpm, extend_acc, &any_obstacle)) {
        std::cout << "[smart_extend] " << group << " pusher_extend_with_disable_seal_ FAIL\n";
        return true;
    }
    if (any_obstacle) {
        std::cout << "[smart_extend] " << group << " obstacle detected (no rescue in manual path — operator action)\n";
    }

    // Release any deferred stall flags from Phase 1 fast extend
    for (int s : slaves) Z_(s).release_stall_flag();

    return false;
}

std::vector<int> WashRobot::group_slaves_(const std::string& group) const {
    std::vector<int> all;
    if (group == "feet")   all = {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2};
    else if (group == "body")   all = {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2};
    else if (group == "center") all = {ZDT_C};
    else if (group == "all")    all = {ZDT_LF1, ZDT_LF2, ZDT_LB1, ZDT_LB2,
                                       ZDT_RF1, ZDT_RF2, ZDT_RB1, ZDT_RB2, ZDT_C};
    if (disabled_zdt_slaves_.empty()) return all;
    std::vector<int> out;
    for (int s : all)
        if (!disabled_zdt_slaves_.count(s)) out.push_back(s);
    return out;
}

int WashRobot::group_valve_ch_(const std::string& group) {
    if (group == "feet")   return CH_VALVE_FEET;
    if (group == "body")   return CH_VALVE_BODY;
    if (group == "center") return CH_VALVE_CENTER;
    return -1;
}

// Per-slave preset extend pulse (matches cycle_group_ logic for body 7,8 SHORT)
int WashRobot::preset_extend_pulse_for_slave_(int slave) const {
    if (slave == ZDT_RF1 || slave == ZDT_LF1) return PUSHER_EXTEND_FEET_PULSE;          // feet upper 1,3 (~8.0 cm)
    if (slave == ZDT_RF2 || slave == ZDT_LF2) return PUSHER_EXTEND_FEET_PULSE_LOWER;    // feet lower 2,4 (~8.3 cm)
    if (slave == ZDT_RB2 || slave == ZDT_LB2) return PUSHER_EXTEND_BODY_PULSE_SHORT;    // body 7,8 (~9.3 cm)
    if (slave == ZDT_RB1 || slave == ZDT_LB1) return PUSHER_EXTEND_BODY_PULSE;          // body 5,6 (~9.5 cm)
    if (slave == ZDT_C)                       return PUSHER_EXTEND_PULSE;               // center 9 (~10 cm)
    return PUSHER_EXTEND_PULSE;   // fallback
}

// Convert cm overextension to ZDT pulses based on slave's group ratio
//   feet (1-4): 20000 pulses = 7 cm → 2857 pulses/cm
//   body (5-9): 30000 pulses = 10 cm → 3000 pulses/cm
int WashRobot::cm_to_pulses_for_slave_(int slave, double cm) {
    if (slave >= 1 && slave <= 4) return (int)(cm * (20000.0 / 7.0));
    if (slave >= 5 && slave <= 9) return (int)(cm * (30000.0 / 10.0));
    return (int)(cm * 3000.0);
}

// Record successful seal pulse — used by fine_tune & cycle_group_
void WashRobot::record_seal_pulse_(int slave, int pulse) {
    if (slave < 1 || slave > 9) return;
    last_seal_pulse_[slave - 1].store(pulse);
}

// Reset last_seal_pulse_ for given group back to per-slave preset
void WashRobot::reset_seal_pulse_group_(const std::string& group) {
    auto slaves = group_slaves_(group);
    for (int s : slaves)
        last_seal_pulse_[s - 1].store(preset_extend_pulse_for_slave_(s));
}

// Compute max feet over-extension (cm) across feet slaves vs per-slave preset
// [2026-06-05] Snowball protection (fix B): cap return value at
// FEET_MAX_OVER_CAP_CM so body target = preset + over × 3000 stays within
// body cup's physical reach (preset + cap*3000 + iter loop room <= ~60000).
double WashRobot::feet_max_overextend_cm_() const {
    double max_over = 0.0;
    for (int s : {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2}) {
        const int preset = preset_extend_pulse_for_slave_(s);   // upper=29000, lower=29900
        const int last   = last_seal_pulse_[s - 1].load();
        const double over_pulses = last - preset;
        const double over_cm     = over_pulses / (20000.0 / 7.0);   // feet ratio
        if (over_cm > max_over) max_over = over_cm;
    }
    if (max_over > FEET_MAX_OVER_CAP_CM) {
        std::cout << "[snowball] feet_max_overextend_cm uncapped=" << max_over
                  << "cm > cap " << FEET_MAX_OVER_CAP_CM
                  << "cm — clamping (protects body target from overshoot)\n";
        max_over = FEET_MAX_OVER_CAP_CM;
    }
    return max_over;
}

// [2026-06-05] Snowball protection (fix C): cap feet target so feet pusher
// itself can't snowball past physical reach. Without this, last_seal_pulse_
// grows unbounded as cups push further each step to seal a receding wall.
int WashRobot::feet_target_capped_(int slave) const {
    const int last_seal = last_seal_pulse_[slave - 1].load();
    const int preset    = preset_extend_pulse_for_slave_(slave);
    const int cap       = preset + cm_to_pulses_for_slave_(slave, FEET_TARGET_OVER_CAP_CM);
    if (last_seal > cap) {
        std::cout << "[snowball] feet slave " << slave << " last_seal=" << last_seal
                  << " > cap " << cap << " (preset+" << FEET_TARGET_OVER_CAP_CM
                  << "cm) — clamping\n";
        return cap;
    }
    return last_seal;
}

bool WashRobot::vacuum_valve_(const std::string& group, bool on) {
    if (group == "all") {
        bool err = false;
        err |= pqw_set_relay_verified_(CH_VALVE_FEET,   on);
        err |= pqw_set_relay_verified_(CH_VALVE_BODY,   on);
        err |= pqw_set_relay_verified_(CH_VALVE_CENTER, on);
        return err;
    }
    int ch = group_valve_ch_(group);
    if (ch < 0) return true;
    return pqw_set_relay_verified_(ch, on);
}

// Set PQW relay (1-based ch) and verify via FC01 readback. Retries up to 3 times
// (50ms apart) if state mismatch. Guards against USR-TCP232 gateway silently
// dropping FC05 when RS485 bus is busy from a prior command. Returns false on
// success (state confirmed) or when readback unavailable (best-effort).
bool WashRobot::pqw_set_relay_verified_(int ch, bool on) {
    if (pqw_.controlRelay(ch, on)) return true;   // TCP send fail = real error
    for (int vr = 0; vr < 3; ++vr) {
        // [2026-06-03] 50 → 200ms: two reasons.
        // 1. Physical relay actuation ~20-30ms + PQW gateway internal handling
        //    → 50ms was on the edge, readback sometimes caught pre-toggle state.
        // 2. cli_22_ bus has concurrent users (step_down main thread doing
        //    PQW valve + JC100 reads, GUI poll). During the 50ms wait, other
        //    Modbus traffic interleaves on the bus → next FC01 readback may
        //    return a stale frame from another query's reply. 200ms gives bus
        //    enough quiet time to flush stale buffer before our readback.
        // Cost: each successful pqw_set_relay_verified_ +150ms (was ~50ms,
        // now ~200ms). Each sweep round has ~4-6 PQW ops → +0.6-0.9s per round.
        sleep_ms_(200);
        auto st = pqw_.readAllStatus();
        if (st.empty() || (int)st.size() <= ch - 1) return false;  // can't verify, proceed
        if (st[ch - 1] == on) return false;                         // confirmed
        std::cout << "[pqw_relay] ch=" << ch << " set " << (on ? "ON" : "OFF")
                  << " verify fail vr=" << vr << ", retrying\n";
        if (pqw_.controlRelay(ch, on)) return true;
    }
    std::cout << "[pqw_relay] ch=" << ch << " set " << (on ? "ON" : "OFF")
              << " gave up verify after 3 retries (downstream check will catch)\n";
    return false;   // best-effort — let downstream vacuum_wait / vacuum_check fail clearly
}

std::vector<int> WashRobot::vacuum_check_(const std::string& group) {
    // Multi-sample to filter pump-ripple / Modbus glitch. Retry on comm error
    // per-sample (driver silently returns last cached value otherwise — not
    // safe for detection). Take WEAKEST reading across good samples (most
    // positive = worst vacuum case) — only fail if even worst-case beyond
    // threshold. Inter-slave delay avoids gateway buffer overlap that yields
    // garbage parses (history of seeing stuck -23 / -35 readings).
    constexpr int SAMPLES        = 3;
    constexpr int SAMPLE_GAP_MS  = 50;
    constexpr int COMM_RETRY_MAX = 3;
    constexpr int COMM_RETRY_GAP_MS = 50;
    constexpr int SLAVE_GAP_MS   = 50;

    std::vector<int> fail;
    auto slaves = group_slaves_(group);

    for (size_t idx = 0; idx < slaves.size(); ++idx) {
        if (idx > 0) sleep_ms_(SLAVE_GAP_MS);
        const int s = slaves[idx];

        int  worst    = 0;
        bool any_good = false;
        int  comm_fails = 0;

        for (int i = 0; i < SAMPLES; ++i) {
            // Per-sample comm retry — read_pressure() returns cached value on
            // Modbus failure, so we must check error_flag explicitly to know
            // whether the value reflects this read or a stale prior state.
            int  p  = 0;
            bool ok = false;
            for (int r = 0; r < COMM_RETRY_MAX; ++r) {
                p = read_pressure_(s);
                if (M_(s).error_flag == 0) { ok = true; break; }
                sleep_ms_(COMM_RETRY_GAP_MS);
            }

            if (!ok) {
                comm_fails++;
            } else if (!any_good || p > worst) {
                worst = p;
                any_good = true;
            }

            if (i < SAMPLES - 1) sleep_ms_(SAMPLE_GAP_MS);
        }

        if (!any_good) {
            std::cout << "[vacuum_check] slave " << s
                      << " ALL " << SAMPLES << " samples comm-failed — treat as detached\n";
            fail.push_back(s);
            continue;
        }
        if (comm_fails > 0) {
            std::cout << "[vacuum_check] slave " << s
                      << " " << comm_fails << "/" << SAMPLES
                      << " sample(s) had comm fail (used worst of good samples)\n";
        }

        // Broadcast the worst-case sample so GUI vacuum readings panel updates
        // in real time (frontend parses any line containing pN=value).
        {
            std::ostringstream oss;
            oss << "vac_sample p" << s << "=" << worst;
            evt_(oss.str());
        }

        if (worst > VACUUM_THRESHOLD_KPA) fail.push_back(s);
    }
    return fail;
}

// Vacuum-feedback fine-tune: after group broadcast extend, monitor pressure
// per-cup and incrementally push unsealed cups (up to base + MAX_OVEREXTEND).
// Best-effort — never aborts the cycle, just returns final fails for the
// caller's existing retry path to handle.
std::vector<int> WashRobot::fine_tune_extend_per_slave_(const std::vector<int>& slaves,
                                                          const std::vector<int>& start_pulses,
                                                          const std::string& group) {
    if (slaves.size() != start_pulses.size()) {
        std::cout << "[fine_tune] " << group << " size mismatch (slaves=" << slaves.size()
                  << " starts=" << start_pulses.size() << ") — abort\n";
        return slaves;   // pretend all failed; caller will retry
    }
    std::vector<int> current = start_pulses;
    // Per-slave obstacle flag: set when stall observed but vacuum still failed
    // → cup is jammed against an obstruction (not a wall it can seal on).
    // Once set, that slave is skipped for remaining fine_tune iterations to
    // avoid hammering it repeatedly into the obstacle.
    std::vector<bool> obstacle(slaves.size(), false);
    auto idx_of = [&](int s) -> int {
        for (size_t i = 0; i < slaves.size(); ++i) if (slaves[i] == s) return (int)i;
        return -1;
    };

    std::vector<int> last_fails;
    for (int iter = 0; iter < FINE_TUNE_MAX_ITERS; ++iter) {
        last_fails = vacuum_check_(group);
        if (last_fails.empty()) {
            std::cout << "[fine_tune] " << group << " all sealed at iter " << iter << "\n";
            // Record per-slave seal pulse: first-pass seal = sealed at start_pulses
            for (size_t i = 0; i < slaves.size(); ++i)
                record_seal_pulse_(slaves[i], current[i]);
            return {};
        }

        // Cross-check: if any slave in last_fails has stall_flag set from a
        // prior pusher_move_ this iteration, mark it as obstacle (stalled but
        // not sealed = jammed on something other than wall).
        for (int s : last_fails) {
            int idx = idx_of(s);
            if (idx < 0 || obstacle[idx]) continue;
            // status was last refreshed by pusher_move_ → zdt_wait_motion_done_;
            // stall_flag survives because defer mode left it set.
            if (Z_(s).status.stall_flag) {
                std::cout << "[fine_tune] slave " << s
                          << " OBSTACLE detected (stall_flag set + vacuum still fail at "
                          << current[idx] << " pulses) — skip remaining iterations\n";
                evt_("obstacle_detected slave=" + std::to_string(s) + " pulse=" + std::to_string(current[idx]));
                obstacle[idx] = true;
            }
        }

        bool extended_any = false;
        for (int s : last_fails) {
            int idx = idx_of(s);
            if (idx < 0) continue;
            if (obstacle[idx]) continue;   // skip obstacle-flagged slaves
            int new_target = current[idx] + FINE_TUNE_INCREMENT_PULSE;
            const int cap = start_pulses[idx] + FINE_TUNE_MAX_OVEREXTEND;
            if (new_target > cap) {
                std::cout << "[fine_tune] slave " << s << " hit over-extend cap "
                          << current[idx] << " (start=" << start_pulses[idx]
                          << "+max=" << FINE_TUNE_MAX_OVEREXTEND << ") — give up this cup\n";
                continue;
            }
            std::cout << "[fine_tune] iter " << iter << " slave " << s
                      << " unsealed, extend " << current[idx] << " → " << new_target << "\n";
            // defer_stall_release=true: cup pushing into wall is the desired
            // endpoint. Stall = wall reached, treat as success and let the next
            // vacuum_check_ decide. Stall flags cleared by cycle_group_ after
            // fine_tune returns.
            if (pusher_move_(s, new_target, PUSHER_RPM, PUSHER_ACC, /*defer_stall=*/true)) {
                std::cout << "[fine_tune] slave " << s << " pusher_move_ failed at "
                          << current[idx] << " — skip this cup, continue\n";
                continue;
            }
            current[idx] = new_target;
            extended_any = true;
        }

        if (!extended_any) {
            std::cout << "[fine_tune] " << group << " no cup extendable this iter (all hit cap / fail / obstacle) — stop\n";
            break;
        }
        sleep_ms_(FINE_TUNE_SETTLE_MS);
    }

    last_fails = vacuum_check_(group);
    // Record seal pulse for cups that ended up sealed (D persistence — used as
    // base for next step's extend). Cups that didn't seal keep previous value.
    {
        std::set<int> failing(last_fails.begin(), last_fails.end());
        for (size_t i = 0; i < slaves.size(); ++i) {
            int s = slaves[i];
            if (!failing.count(s)) record_seal_pulse_(s, current[i]);
        }
    }
    if (!last_fails.empty()) {
        std::cout << "[fine_tune] " << group << " done, still failing slaves=";
        for (size_t i = 0; i < last_fails.size(); ++i) {
            if (i) std::cout << ",";
            std::cout << last_fails[i];
        }
        std::cout << "\n";
    }
    return last_fails;
}

// Poll JC-100 every 200ms until all listed slaves' pressure rises above
// DETACH_THRESHOLD_KPA (-10 kPa) OR timeout. Returns false on success (all
// released), true on timeout. Used between valve-OFF and pusher retract.
bool WashRobot::vacuum_wait_release_(const std::vector<int>& slaves, int timeout_ms) {
    constexpr int POLL_MS = 300;   // 2026-05-29: 200→300,JC100 timeout 之間隔開,給 bus 喘息
    if (slaves.empty()) return false;   // nothing to check = trivial success

    int elapsed = 0;
    while (elapsed < timeout_ms) {
        bool all_released = true;
        for (int s : slaves) {
            int p = read_pressure_(s);
            // comms fail → treat as still attached (conservative); poll-based
            // approach gives gateway transient hiccups time to recover within
            // the timeout budget.
            const bool released = (M_(s).error_flag == 0) && (p >= DETACH_THRESHOLD_KPA);
            if (!released) { all_released = false; break; }
        }
        if (all_released) {
            std::cout << "[vacuum_release] all released after " << elapsed << "ms\n";
            return false;
        }
        sleep_ms_(POLL_MS);
        elapsed += POLL_MS;
    }

    // Timeout — collect stuck list (re-read once for fresh values), log + EVT
    std::vector<int> stuck;
    for (int s : slaves) {
        int p = read_pressure_(s);
        if (M_(s).error_flag != 0 || p < DETACH_THRESHOLD_KPA) stuck.push_back(s);
    }
    std::ostringstream oss;
    oss << "[vacuum_release] TIMEOUT after " << timeout_ms << "ms, stuck slaves:";
    for (int s : stuck) oss << " " << s;
    std::cout << oss.str() << "\n";

    std::ostringstream evt;
    evt << "vacuum_release_timeout stuck=";
    for (size_t i = 0; i < stuck.size(); ++i) {
        if (i) evt << ",";
        evt << stuck[i];
    }
    evt_(evt.str());
    return true;
}

// Pre-retract safety: scan group for latched stall_flag, release any found,
// verify cleared. Stall on the still-holding group means ZDT firmware will
// reject the next motion cmd on that group (e.g. when it retracts in the
// following phase). Called before "valve OFF + retract" of the OTHER group.
// Idempotent — no-op if all clear. Returns false=clear, true=persistent stall.
bool WashRobot::ensure_group_stall_clear_(const std::string& group) {
    auto slaves = group_slaves_(group);
    if (slaves.empty()) return false;

    // Pass 1: detect stalls
    std::vector<int> stalled;
    for (int s : slaves) {
        if (Z_(s).get_system_status()) continue;   // comm fail, skip — best effort
        if (Z_(s).status.stall_flag) stalled.push_back(s);
    }
    if (stalled.empty()) {
        std::cout << "[stall_check " << group << "] all clear\n";
        return false;
    }

    // Pass 2: release latched flags
    std::cout << "[stall_check " << group << "] STALL on slaves:";
    for (int s : stalled) std::cout << " " << s;
    std::cout << " — releasing\n";
    for (int s : stalled) Z_(s).release_stall_flag();

    // Pass 3: verify cleared (50ms gap for firmware to update status)
    sleep_ms_(50);
    for (int s : stalled) {
        if (Z_(s).get_system_status()) continue;
        if (Z_(s).status.stall_flag) {
            std::cout << "[stall_check " << group << "] slave " << s
                      << " STALL persistent after release\n";
            return true;
        }
    }
    std::cout << "[stall_check " << group << "] cleared\n";
    return false;
}

// Pre-flight stall clear on all 9 ZDT slaves. Defer-stall mode in extend leaves
// stall_flag set on cups that hit wall — without clearing, next pos_mode (e.g.
// retract in next phase) gets silently rejected by firmware → motor won't move
// → cup yanked off wall by valve release → cascade failure.
bool WashRobot::ensure_all_zdt_stall_clear_() {
    int cleared = 0;
    for (int s = 1; s <= 9; ++s) {
        if (disabled_zdt_slaves_.count(s)) continue;
        if (Z_(s).get_system_status()) continue;   // comm fail, best-effort skip
        if (Z_(s).status.stall_flag) {
            std::cout << "[stall_check all] slave " << s
                      << " stall_flag SET (pos=" << Z_(s).status.real_pos << "°) → release\n";
            evt_("pre_cycle_stall_clear slave=" + std::to_string(s));
            Z_(s).release_stall_flag();
            ++cleared;
        }
    }
    if (cleared == 0) {
        std::cout << "[stall_check all] all clear\n";
        return false;
    }
    sleep_ms_(100);   // firmware settle
    int persistent = 0;
    for (int s = 1; s <= 9; ++s) {
        if (disabled_zdt_slaves_.count(s)) continue;
        if (Z_(s).get_system_status()) continue;
        if (Z_(s).status.stall_flag) {
            std::cout << "[stall_check all] slave " << s << " STALL PERSISTENT after release\n";
            ++persistent;
        }
    }
    if (persistent > 0) {
        evt_("stall_persistent count=" + std::to_string(persistent));
        return true;   // → caller's try_or_pause_ → PausedOnError
    }
    std::cout << "[stall_check all] cleared " << cleared << " latched stall flag(s)\n";
    return false;
}

bool WashRobot::clear_other_group_stalls_(const std::string& current_group) {
    std::vector<int> other;
    if (current_group == "body") {
        other = {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2};
    } else if (current_group == "feet") {
        other = {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2};
    } else {
        std::cout << "[other_stall_clear] unknown group=" << current_group << " — skip\n";
        return false;
    }
    int cleared = 0;
    for (int s : other) {
        if (disabled_zdt_slaves_.count(s)) continue;
        if (Z_(s).get_system_status()) {
            std::cout << "[other_stall_clear] " << current_group << " phase: slave "
                      << s << " status read fail (skip)\n";
            continue;
        }
        if (Z_(s).status.stall_flag) {
            std::cout << "[other_stall_clear] " << current_group << " phase: other-group slave "
                      << s << " stall_flag SET (pos=" << Z_(s).status.real_pos
                      << "°) → release\n";
            evt_("other_group_stall_clear current=" + current_group
                 + " slave=" + std::to_string(s));
            Z_(s).release_stall_flag();
            ++cleared;
        }
    }
    if (cleared > 0) {
        sleep_ms_(100);   // firmware settle
        std::cout << "[other_stall_clear] " << current_group << " phase: cleared "
                  << cleared << " latched stall flag(s) on other group\n";
    }
    return false;   // best-effort — never block the cycle
}

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
    if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_FEET, false); },
                      "init_valve_feet_off")) return "ERR aborted\n";
    if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_BODY, false); },
                      "init_valve_body_off")) return "ERR aborted\n";
    if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_CENTER, false); },
                      "init_valve_center_off")) return "ERR aborted\n";
    if (try_or_pause_([this]() { return pqw_.controlRelay(CH_BRUSH, false); },
                      "init_brush_off")) return "ERR aborted\n";
    if (try_or_pause_([this]() { return pqw_.controlRelay(CH_WATER_PUMP, false); },
                      "init_water_pump_off")) return "ERR aborted\n";
    // [2026-06-05] water_inlet → crane PQW (.34 slave 12 CH4)
    if (try_or_pause_([this]() { return set_water_inlet_(false); },
                      "init_water_inlet_off")) return "ERR aborted\n";

    // Retract wheels (Phase 1 climb → Phase 2 prep) before pushers extend.
    // Assumes Phase 1 zeroed wheels at retracted position before deploy.
    // wheels_attached_=off skips the block — bench without wheels stays clean
    // (otherwise PR_move_cm Modbus-times-out on missing slaves 2/4 → PausedOnError).
    if (wheels_attached_.load()) {
        std::cout << "[init] DM2J wheels (slave 2, 4) → retract to 0 (parallel)\n";
        // [2026-06-05] 改成 nowait + wait pattern，跟 cmd_wheels（收輪按鈕）一致：
        // 兩邊 trigger 都下完才開始 wait，總時間 ≈ max(L, R) 而非 L + R。
        // 原本是兩個 try_or_pause_ 各自 blocking PR_move_cm 依序執行。
        if (try_or_pause_([this]() -> bool {
            if (D_(DM2J_LEFT_WHEEL ).PR_move_cm_nowait(0, 1, DM2J_RPM, 0.0, DM2J_ACC, DM2J_DEC)) return true;
            if (D_(DM2J_RIGHT_WHEEL).PR_move_cm_nowait(0, 1, DM2J_RPM, 0.0, DM2J_ACC, DM2J_DEC)) return true;
            if (dm2j_wait_done_(DM2J_LEFT_WHEEL )) return true;
            if (dm2j_wait_done_(DM2J_RIGHT_WHEEL)) return true;
            return false;
        }, "init_wheels_retract")) return "ERR aborted\n";
    } else {
        std::cout << "[init] wheels_attached=off, skip wheel retract\n";
    }

    // Foot rails (DM2J slave 1, 3) → absolute 0. Rigid-coupled mechanically so
    // broadcast-sync via dm2j_pair_move_abs_ on PR1 (bystanders 2/4/5 have PR1
    // rpm=0 safe set in WashRobot::init).
    std::cout << "[init] DM2J feet rails (slave " << DM2J_LEFT_FOOT
              << ", " << DM2J_RIGHT_FOOT << ") → home to abs 0\n";
    if (try_or_pause_([this]() { return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, 0.0); },
                      "init_feet_rail_home")) return "ERR aborted\n";
    rail_pos_cm_.store(0.0);

    // Release stall + enable all ZDT drivers before sending any motion command.
    // ZDT firmware returns Modbus exception 0x03 (illegal data value) for pos_mode
    // calls when the drive is disabled or has a latched stall flag. WASH_ROBOT::init()
    // connects TCP but does not enable, and cmd_shutdown / cmd_emergency_stop disable,
    // so cmd_init must re-enable here. Matches Linux_test menu 3 sequence.
    std::cout << "[init] ZDT 1-9 → release_stall + driver enable"
              << (disabled_zdt_slaves_.empty() ? "" : " (disabled slaves skipped)")
              << "\n";
    for (int s = 1; s <= 9; ++s) {
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

    // Extend pushers in init in 4 staged sub-groups (2026-05-05):
    //   1. feet lower  (F2 = slave 2, 4)
    //   2. feet upper  (F1 = slave 1, 3)
    //   3. body lower  (B2 = slave 7, 8) — 9.3 cm short
    //   4. body upper  (B1 = slave 5, 6) — 9.5 cm long
    // defer_stall=true: cup hitting wall during init is treated as success;
    // cmd_attach handles valves later.
    auto filter_disabled = [&](std::initializer_list<int> raw) {
        std::vector<int> out;
        for (int s : raw) if (!disabled_zdt_slaves_.count(s)) out.push_back(s);
        return out;
    };
    const std::vector<int> feet_lower = filter_disabled({ZDT_RF2, ZDT_LF2});   // slave 2, 4
    const std::vector<int> feet_upper = filter_disabled({ZDT_RF1, ZDT_LF1});   // slave 1, 3
    const std::vector<int> body_short = filter_disabled({ZDT_RB2, ZDT_LB2});   // slave 7, 8
    const std::vector<int> body_long  = filter_disabled({ZDT_RB1, ZDT_LB1});   // slave 5, 6

    // [2026-05-25] feet extend MOVED to cmd_attach (after feet valve open).
    // init 不再伸任何 pusher（feet + body 都留在 0）。attach 開 feet valve 之後
    // 才透過 smart_extend_subset_("feet") 一氣呵成「伸 + 等真空」。
    // 舊版「init 伸 feet」kept commented for easy revert。
    //if (!feet_lower.empty()) {
    //    std::cout << "[init] feet lower pushers (2,4) → extend " << PUSHER_EXTEND_FEET_PULSE_LOWER
    //              << " pulses (~8.3 cm)\n";
    //    if (try_or_pause_([this, &feet_lower]() { return pusher_move_many_(feet_lower, PUSHER_EXTEND_FEET_PULSE_LOWER, PUSHER_RPM, PUSHER_ACC, /*defer_stall=*/true); },
    //                      "init_feet_lower_extend")) return "ERR aborted\n";
    //}
    //if (!feet_upper.empty()) {
    //    std::cout << "[init] feet upper pushers (1,3) → extend " << PUSHER_EXTEND_FEET_PULSE
    //              << " pulses (~8.0 cm)\n";
    //    if (try_or_pause_([this, &feet_upper]() { return pusher_move_many_(feet_upper, PUSHER_EXTEND_FEET_PULSE, PUSHER_RPM, PUSHER_ACC, /*defer_stall=*/true); },
    //                      "init_feet_upper_extend")) return "ERR aborted\n";
    //}
    // [2026-05-22] body extend MOVED to cmd_attach (smart_extend_subset_, disable_seal).
    // Old "init extends body too" kept commented for easy revert.
    //if (!body_short.empty()) {
    //    std::cout << "[init] body lower pushers (7,8) → extend " << PUSHER_EXTEND_BODY_PULSE_SHORT
    //              << " pulses (~9.3 cm)\n";
    //    if (try_or_pause_([this, &body_short]() { return pusher_move_many_(body_short, PUSHER_EXTEND_BODY_PULSE_SHORT, PUSHER_RPM_BODY_EXTEND, PUSHER_ACC_BODY_EXTEND, /*defer_stall=*/true); },
    //                      "init_body_lower_extend")) return "ERR aborted\n";
    //}
    //if (!body_long.empty()) {
    //    std::cout << "[init] body upper pushers (5,6) → extend " << PUSHER_EXTEND_BODY_PULSE
    //              << " pulses (~9.5 cm)\n";
    //    if (try_or_pause_([this, &body_long]() { return pusher_move_many_(body_long, PUSHER_EXTEND_BODY_PULSE, PUSHER_RPM_BODY_EXTEND, PUSHER_ACC_BODY_EXTEND, /*defer_stall=*/true); },
    //                      "init_body_upper_extend")) return "ERR aborted\n";
    //}
    // Release any deferred stall flags from init extends (feet only — body extend disabled above)
    for (int s : feet_lower)  Z_(s).release_stall_flag();
    for (int s : feet_upper)  Z_(s).release_stall_flag();
    //for (int s : body_short)  Z_(s).release_stall_flag();
    //for (int s : body_long)   Z_(s).release_stall_flag();
    (void)body_short; (void)body_long;   // declared above; silence unused-variable warning

    std::cout << "[init] DM2J arm (slave " << DM2J_ARM << ") → set current as zero\n";
    D_(DM2J_ARM).home_set_current_pos_zero();

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

    // [2026-05-22] init now only extends FEET to preset; body (5,6,7,8) stays at 0.
    // attach takes care of:
    //   1) open all valves (feet / body / center)
    //   2) body group extends from 0 -> preset via smart_extend_subset_ (the same
    //      disable_seal pipeline step_up/down use). The feet, already pressed
    //      against the wall from init, build vacuum PASSIVELY during this body
    //      extend (body disable_seal typically takes 10s+, plenty of settle).
    //   3) vacuum_check all; any cup still unsealed gets a smart_extend fine-tune.
    //   4) crane pay_out_to_weight_ transfers body weight onto the cups.
    // [TEMP DISABLED] center pusher (slave 9) is still NOT extended; its valve is
    // opened anyway (existing behavior, no change).

    // [2026-05-25 重排] 新流程：feet valve → feet seal wait → realign → body valve → body extend
    //   (center valve 不開 — per user 2026-05-25 暫不控制 center)
    //   舊流程 "同時開 3 valve + body extend 期間 feet 被動 seal" 改成顯式
    //   "feet 先 seal + realign → 才動 body"。

    // 1. 開 FEET valve（FC01 readback verify 防 USR gateway 丟 FC05）
    std::cout << "[attach] open FEET valve CH" << CH_VALVE_FEET << "\n";
    if (try_or_pause_([this]() { return pqw_set_relay_verified_(CH_VALVE_FEET, true); },
                      "attach_valve_feet_on")) return "ERR aborted\n";

    // 1b. 開腳真空後才伸 + 等真空建立：用 disable_seal pipeline (smart_extend_subset_)。
    //     2026-05-25 起 init 不伸 feet → 進來 feet 在 0；pipeline 內 Phase 1 fast
    //     extend 從 0 → preset-buffer、iter loop 慢推到 preset + WAIT_SEAL 等真空。
    //     一氣呵成「伸 + 等 seal」。
    {
        std::vector<int> feet_slaves;
        for (int s : {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2}) {
            if (!disabled_zdt_slaves_.count(s)) feet_slaves.push_back(s);
        }
        if (!feet_slaves.empty()) {
            std::cout << "[attach] feet disable_seal — wait for feet vacuum to build\n";
            if (try_or_pause_([this, &feet_slaves]() { return smart_extend_subset_("feet", feet_slaves); },
                              "attach_feet_disable_seal_wait"))
                return "ERR aborted\n";
        }
    }

    // 1c. realign once（force=false → 內部 threshold check 自動決定要不要跑）
    //     必須先 unlock motion_mtx_：do_feet_realign_ 內部會自己 lock,
    //     同 thread re-lock std::mutex = deadlock(2026-05-25 user bench 踩到)。
    //     in_window=true：跳 Phase A（body retract，因為此時 body 在 0、
    //     從 0 retract 會撞下限）。Phase B 還是會 open body valve + 伸 body
    //     via disable_seal — 後面的 step 4-5 變成 no-op-ish 但保留當保險。
    lk.unlock();
    {
        std::cout << "[attach] mid-attach realign (force=false, in_window=true)\n";
        std::string realign_err = do_feet_realign_(/*force=*/false, /*in_window=*/true);
        if (!realign_err.empty()) {
            std::cout << "[attach] mid-attach realign FAIL (non-fatal): " << realign_err;
        }
    }
    lk.lock();

    // 2. 開 BODY valve（center valve 不開，per user 2026-05-25）
    std::cout << "[attach] open BODY valve CH" << CH_VALVE_BODY << "\n";
    if (try_or_pause_([this]() { return pqw_set_relay_verified_(CH_VALVE_BODY, true); },
                      "attach_valve_body_on")) return "ERR aborted\n";

    // 3. 身體組從 0 走 disable_seal 伸出 (跟 step_up/down 共用 smart_extend_subset_)。
    {
        std::vector<int> body_slaves;
        for (int s : {ZDT_RB1, ZDT_LB1, ZDT_RB2, ZDT_LB2}) {
            if (!disabled_zdt_slaves_.count(s)) body_slaves.push_back(s);
        }
        if (!body_slaves.empty()) {
            std::cout << "[attach] body disable_seal extend (from 0 -> preset)\n";
            if (try_or_pause_([this, &body_slaves]() { return smart_extend_subset_("body", body_slaves); },
                              "attach_body_disable_seal_extend"))
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
        std::cout << "[attach] cups not sealed after body extend:";
        for (int s : initial_fails) std::cout << " " << s;
        std::cout << " → smart_extend (disable_seal)\n";

        // Split unsealed cups by group (center slave 9 not extended in attach).
        std::vector<int> feet_fails, body_fails;
        for (int s : initial_fails) {
            if (s >= 1 && s <= 4)      feet_fails.push_back(s);
            else if (s >= 5 && s <= 8) body_fails.push_back(s);
        }

        if (!feet_fails.empty() &&
            try_or_pause_([this, &feet_fails]() { return smart_extend_subset_("feet", feet_fails); },
                          "attach_feet_smart_extend"))
            return "ERR aborted\n";
        if (!body_fails.empty() &&
            try_or_pause_([this, &body_fails]() { return smart_extend_subset_("body", body_fails); },
                          "attach_body_smart_extend"))
            return "ERR aborted\n";

        auto remaining = vacuum_check_("all");
        if (!remaining.empty()) {
            std::cout << "[attach] WARN cups still unsealed after smart_extend:";
            for (int s : remaining) std::cout << " " << s;
            std::cout << " (proceeding to Attached anyway)\n";
            evt_("attach_partial_seal count=" + std::to_string((int)remaining.size()));
        } else {
            std::cout << "[attach] all cups sealed after smart_extend\n";
        }
    } else {
        std::cout << "[attach] all 9 cups sealed on first check\n";
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
    {
        auto pre_payout_fails = vacuum_check_("all");
        if (!pre_payout_fails.empty()) {
            std::cout << "[attach] skip pay_out — cups still unsealed:";
            for (int s : pre_payout_fails) std::cout << " " << s;
            std::cout << " (rope must keep bearing weight; would overload "
                         "sealed cups otherwise)\n";
            evt_("attach_payout_skipped_unsealed count="
                 + std::to_string((int)pre_payout_fails.size()));
        } else {
            double target_kg = ATTACH_PAYOUT_TARGET_KG;
            std::string st = crane_cmd_("status", 2);
            auto pos = st.find("retract_tension_stop_kg=");
            if (pos != std::string::npos) {
                try {
                    // 24 = strlen("retract_tension_stop_kg=")
                    double v = std::stod(st.substr(pos + 24));
                    if (v > 0 && v < 500) target_kg = v;
                } catch (...) {}
            }
            std::cout << "[attach] pay out crane to ~" << target_kg
                      << " kg per rope (web 收繩軟停張力, fallback="
                      << ATTACH_PAYOUT_TARGET_KG << ")\n";
            std::string pr = crane_pay_out_to_weight_(target_kg, ATTACH_PAYOUT_MAX_CM);
            std::cout << "[attach] crane pay out: " << pr << "\n";
            if (pr.rfind("OK", 0) != 0) {
                evt_("attach_payout_skipped " + pr);
            }
        }
    }

    std::cout << "[attach] done → Attached\n";
    set_state_(State::Attached);
    return "OK attached\n";
}

std::string WashRobot::cmd_detach() {
    State cur = state_.load();
    if (cur != State::Attached) return state_violation_(cur);

    std::lock_guard<std::mutex> lk(motion_mtx_);
    std::cout << "[detach] close valves CH" << CH_VALVE_FEET << "/"
              << CH_VALVE_BODY << "/" << CH_VALVE_CENTER << " → Ready\n";
    pqw_.controlRelay(CH_VALVE_FEET,   false);
    pqw_.controlRelay(CH_VALVE_BODY,   false);
    pqw_.controlRelay(CH_VALVE_CENTER, false);
    set_state_(State::Ready);
    return "OK detached\n";
}

// Internal sweep — caller must already hold motion_mtx_
std::string WashRobot::do_arm_sweep_() {
    // Start cleaning: brush motor + water pump + water inlet valve.
    // PQW controlRelay return ignored — cleanup at end of function MUST run
    // unconditionally (water flowing without arm motion = bigger problem than
    // ignoring return). LED + water flow = real verification.
    //pqw_.controlRelay(CH_WATER_INLET, true);
    //pqw_.controlRelay(CH_WATER_PUMP,  true);
    //pqw_.controlRelay(CH_BRUSH,       true);

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
    return do_arm_sweep_();
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
                                      std::function<void()> during_body_rail_hook,
                                      std::function<void()> after_body_rail_hook) {
    // unique_lock instead of lock_guard so we can manually release before
    // calling do_feet_realign_ (which acquires motion_mtx_ itself) — same-thread
    // re-lock would deadlock.
    std::unique_lock<std::mutex> lk(motion_mtx_);
    abort_flag    = false;
    motion_active_ = true;

    // New order (2026-04-24): body FIRST, feet SECOND.
    // Both rail moves use ABSOLUTE targets (aligned with Linux_test menu 7):
    //   Phase A (body): rail → +STEP_CM  (body descends, rail extends)
    //   Phase B (feet): rail → 0         (feet catches up, rail retracts)
    // Crane pay_out for descent happens in body phase (first descending group).
    // Step-compensation (body_residual_cm_ / actual_feet_cm_) removed — absolute
    // rail targets handle drift without needing cross-step carryover.

    // ---------- A. Body phase (first, body descends STEP_CM) ----------
    // All hardware ops wrapped in try_or_pause_: on fail, pause for user manual
    // fix, then retry / skip / abort based on cmd_continue / cmd_skip / emergency_stop.
    auto body_pre_cycle = [this, &during_body_rail_hook, &after_body_rail_hook]() -> std::string {
        // Pre-flight: clear ALL 9 ZDT stall flags before any valve / motion ops.
        // Defer-stall mode in extend leaves stall_flag set on cups that hit wall;
        // any latched stall on the about-to-retract group would silently reject
        // its next pos_mode → motor stays put while valve releases → cup ripped
        // off mechanically. Covers feet (load-bearing) AND body (about to retract).
        if (try_or_pause_([this]() { return ensure_all_zdt_stall_clear_(); },
                          "body_pre_stall_clear")) return "aborted";
        // [2026-06-02] SAFETY: verify FEET (anchor) all sealed BEFORE releasing
        // body. State=Attached alone doesn't guarantee physical seal — recover()
        // bypass, slow vacuum leak between steps, JC100 stale-read on TIMEOUT
        // can all leave state out of sync with reality. Releasing body onto
        // unsealed feet → machine drops onto crane rope → shock load + body
        // reseal failure cascade (see 2026-06-01 step 35cm incident).
        if (try_or_pause_([this]() -> bool {
            auto fails = vacuum_check_("feet");
            if (fails.empty()) return false;
            std::string msg = "body_pre_anchor_unsealed feet=";
            for (size_t i = 0; i < fails.size(); ++i) {
                if (i) msg += ",";
                msg += std::to_string(fails[i]);
            }
            std::cout << "[safety] " << msg << " — REFUSE to release body\n";
            evt_(msg);
            return true;
        }, "body_pre_anchor_check")) return "aborted";
        // Release body + center valves BEFORE retracting pushers (cups would
        // grip wall otherwise, stalling pusher retract).
        if (try_or_pause_([this]() { return pqw_set_relay_verified_(CH_VALVE_BODY, false); },
                          "body_pre_valve_off_body")) return "aborted";
        // [TEMP DISABLED 2026-05-04] center pusher (slave 9) skipped during step_down
        //if (try_or_pause_([this]() { return pqw_set_relay_verified_(CH_VALVE_CENTER, false); },
        //                  "body_pre_valve_off_center")) return "aborted";
        // Poll-based wait — proceeds the moment all body cups release,
        // up to VACUUM_RELEASE_WAIT_MS. On timeout drops into PausedOnError.
        {
            // [TEMP DISABLED 2026-05-04] ZDT_C removed from vacuum_wait_release_ list
            std::vector<int> body_center = {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2 /*, ZDT_C*/};
            if (try_or_pause_([this, &body_center]() { return vacuum_wait_release_(body_center, VACUUM_RELEASE_WAIT_MS); },
                              "body_pre_vacuum_release")) return "aborted";
        }

        // Other-group stall sweep: feet 在 body 真空釋放後，可能因機體重新分配
        // 載荷產生 stall flag — 預先清掉，避免 feet 之後 motion 被 ZDT 拒收。
        clear_other_group_stalls_("body");

        // [TEMP DISABLED 2026-05-04] center pusher two-stage retract skipped
        //// Two-stage retract for center pusher (half → wait → full).
        //if (try_or_pause_([this]() { return pusher_move_(ZDT_C, PUSHER_EXTEND_PULSE * 2 / 3, PUSHER_RPM_RETRACT, PUSHER_ACC_RETRACT); },
        //                  "body_pre_center_retract_half")) return "aborted";
        //if (try_or_pause_([this]() { return pusher_move_(ZDT_C, PUSHER_RETRACT_PULSE, PUSHER_RPM_RETRACT_FULL, PUSHER_ACC_RETRACT); },
        //                  "body_pre_center_retract_full")) return "aborted";

        // Body pushers: pipelined two-stage retract — slow-peel off the wall
        // then fast retract to 0 (avoids ZDT stall on lingering cup adhesion).
        std::vector<int> body_slaves = {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2};
        if (try_or_pause_([this, &body_slaves]() { return pusher_two_stage_retract_(body_slaves); },
                          "body_pre_pusher_retract")) return "aborted";

        // 2026-05-15 dynamic crane motion + margin pattern, based on ACTUAL
        // DM2J position (not just rail_pos_cm_ atomic — that can be stale if
        // a previous step failed mid-way without proper rollback).
        //
        // Pattern: pay_out (delta + margin) BEFORE rail move (gives slack so
        // body can descend without rope tension fighting rail motor) → DM2J
        // moves → retract margin (collect overshoot, restore baseline tension).
        // Net crane motion = delta = actual body descent.
        const int    step        = step_cm_.load();
        double dm2j_L = 0, dm2j_R = 0;
        if (try_or_pause_([this, &dm2j_L]() { return dm2j_read_pos_robust_(DM2J_LEFT_FOOT,  dm2j_L); },
                          "body_pre_dm2j_read_left")) return "aborted";
        if (try_or_pause_([this, &dm2j_R]() { return dm2j_read_pos_robust_(DM2J_RIGHT_FOOT, dm2j_R); },
                          "body_pre_dm2j_read_right")) return "aborted";
        const double rail_actual = (dm2j_L + dm2j_R) / 2.0;
        const double rail_target = (double)step;
        const double rail_delta  = rail_target - rail_actual;
        std::cout << "[step_down] DM2J read L=" << dm2j_L << " R=" << dm2j_R
                  << " avg=" << rail_actual << " target=" << rail_target
                  << " delta=" << rail_delta << "cm\n";

        if (rail_delta > 0.5) {
            // pay_out (delta + margin)
            const int pay_cm = (int)std::lround(rail_delta + STEP_MARGIN_CM);
            {
                std::ostringstream oss;
                oss << "pay_out " << pay_cm;
                const std::string cmd_str = oss.str();
                std::cout << "[step_down] crane " << cmd_str
                          << " (delta " << rail_delta << " + margin " << STEP_MARGIN_CM << ")\n";
                // [arm rope protect TEMP 2026-05-21 — DISABLED 2026-05-22]
                //if (try_or_pause_([this]() { return ensure_arm_center_for_rope_("body_pre_crane_pay_out"); },
                //                  "arm_stow_for_body_pay_out")) return "aborted";
                // [2026-06-05] Dynamic timeout from pay_cm (was default 60s).
                const int pay_timeout = crane_motion_timeout_sec_(pay_cm);
                if (try_or_pause_([this, cmd_str, pay_timeout]() { return crane_cmd_(cmd_str, pay_timeout).rfind("OK", 0) != 0; },
                                  "body_pre_crane_pay_out")) return "aborted";
            }

            // DM2J rails → absolute step_cm
            // [2026-06-04] Optional during_body_rail_hook: cmd_run_avoid 在 motion 進行中
            // 約 80% 處拍 "before" frame，DM2J move 結束後 after_body_rail_hook 拍 "after"。
            // 兩 frame 構成下一輪 detector 用的 motion parallax pair。
            // 估時間：DM2J feet rail 6.67 cm/s + 400ms 加減速 → ~step×150+400 ms total。
            // 80% trigger = est_total × 0.8 → rail 接近 step (-2~-4cm)。
            std::thread probe_thread_;
            if (during_body_rail_hook) {
                const int est_total_ms = (int)(rail_delta * 150 + 400);
                const int capture_at_ms = std::max(300, est_total_ms * 80 / 100);
                auto hook_copy = during_body_rail_hook;   // capture by value into thread
                probe_thread_ = std::thread([capture_at_ms, hook_copy]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(capture_at_ms));
                    try { hook_copy(); } catch (...) {}
                });
            }
            if (try_or_pause_([this, rail_target]() { return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, rail_target); },
                              "body_pre_rail_forward")) {
                if (probe_thread_.joinable()) probe_thread_.join();   // ensure thread finished
                return "aborted";
            }
            if (probe_thread_.joinable()) probe_thread_.join();   // wait for capture
            rail_pos_cm_.store(rail_target);
            // [2026-06-04] After-body-rail hook: rail 已到 step_cm = 下一步起點。
            if (after_body_rail_hook) {
                try { after_body_rail_hook(); } catch (...) {}
            }

            // retract margin (collect overshoot)
            {
                std::cout << "[step_down] crane retract " << STEP_MARGIN_CM << " (margin)\n";
                if (try_or_pause_([this]() { return crane_retract_safe_(STEP_MARGIN_CM).rfind("OK", 0) != 0; },
                                  "body_pre_crane_retract_margin")) return "aborted";
            }
        } else {
            std::cout << "[step_down] rail already at target (delta " << rail_delta
                      << "cm <= 0.5) — skip crane + DM2J ops\n";
            rail_pos_cm_.store(rail_target);   // sync atomic with reality anyway
        }

        // [TEMP DISABLED 2026-05-04] center pusher re-extend + valve ON skipped
        //// Re-extend center + re-engage center valve
        //if (try_or_pause_([this]() { return pusher_move_(ZDT_C, PUSHER_EXTEND_PULSE); },
        //                  "body_pre_center_extend")) return "aborted";
        //if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_CENTER, true); },
        //                  "body_pre_valve_on_center")) return "aborted";

        // In-window feet realign (2026-05-22): body is retracted here and
        // cycle_group_ rebuilds it right after — so feet realign runs feet-only
        // (in_window=true), skipping its own Phase A/B body retract+rebuild.
        // Non-fatal: a realign error is logged but does not abort the step
        // (matches the old separate mid-step realign call's non-fatal handling).
        {
            std::string rerr = do_feet_realign_(/*force=*/false, /*in_window=*/true);
            if (!rerr.empty()) {
                std::cout << "[step_down] in-window feet realign FAIL (non-fatal): " << rerr;
                evt_("mid_step_realign_fail step_down " + rerr);
            }
        }
        // realign may have flipped motion_active_ off on an internal error path —
        // restore it: cycle_group_ is about to disable_seal-extend the body and
        // pressure_poll_loop_ must stay parked (stale-frame race, 2026-05-22).
        motion_active_ = true;
        return "";
    };

    // Backup lambda factory (used by both vacuum-retry small backup and
    // obstacle-rescue larger backup). Captures `backup_cm` by value.
    auto body_backup_factory = [this](double backup_cm, const char* tag) {
        return [this, backup_cm, tag](bool dry_run) -> std::string {
            // 2026-05-18: read ACTUAL DM2J position, not rail_pos_cm_ atomic
            // (stale-atomic → crane retract mismatch vs real DM2J move; see
            // body_backup_factory_up comment for the bench scenario).
            double dm2j_L = 0, dm2j_R = 0;
            if (dm2j_read_pos_robust_(DM2J_LEFT_FOOT,  dm2j_L) ||
                dm2j_read_pos_robust_(DM2J_RIGHT_FOOT, dm2j_R)) {
                if (!dry_run)
                    std::cout << "  [retry body" << tag << "] DM2J read fail — abort backup\n";
                return std::string("body_backup_dm2j_read_fail") + tag;
            }
            const double rail_before = (dm2j_L + dm2j_R) / 2.0;
            const double target      = rail_before - backup_cm;
            if (target < 0.0) {
                if (!dry_run)
                    std::cout << "  [retry body" << tag << "] target " << target
                              << " cm < 0 — no more backup space, abort\n";
                return std::string("body_backup_no_space") + tag;
            }
            if (dry_run) return "";
            const double rail_delta = rail_before - target;
            // [2026-05-27] Order: rail backward FIRST → crane retract SECOND.
            // Body retreating UP toward the crane slacks the rope on its own;
            // doing crane retract first would shorten rope while body is still
            // in place → tension spike against the still-anchored feet vacuum /
            // rope mount. Symmetric to step_up body_pre_cycle forward (L4396-
            // 4411): "body climbing UP toward the crane generates rope slack
            // on its own → retract collects it". Also upgraded crane_cmd_
            // ("retract N") → crane_retract_safe_(N) for consistency with
            // body_backup_factory_up re-tension (weight-monitor early stop).
            std::cout << "  [retry body" << tag << "] rail " << rail_before << " → " << target << " cm\n";
            if (try_or_pause_([this, target]() { return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, target); },
                              std::string("body_backup_rail") + tag)) return "aborted";
            rail_pos_cm_.store(target);
            {
                const int retract_cm = (int)std::lround(rail_delta);
                std::cout << "  [retry body" << tag << "] crane retract " << retract_cm
                          << " (tension-monitored re-tension)\n";
                if (try_or_pause_([this, retract_cm]() { return crane_retract_safe_(retract_cm).rfind("OK", 0) != 0; },
                                  std::string("body_backup_crane_retract") + tag)) return "aborted";
            }
            return "";
        };
    };
    auto body_backup        = body_backup_factory(VACUUM_BACKUP_CM,         "");
    auto body_rescue_backup = body_backup_factory(OBSTACLE_RESCUE_BACKUP_CM, "_rescue");

    int body_retries = 0;
    int body_rescues = 0;
    std::string err = cycle_group_("body", body_pre_cycle, body_backup, body_rescue_backup, body_retries, body_rescues);
    if (!err.empty()) { motion_active_ = false; return "ERR " + err + "\n"; }

    // [2026-06-04] Record actual Phase A achievement for run_avoid shortfall comp.
    // rail_pos_cm_ here reflects FINAL position after any vacuum_retry backup.
    // last_step_planned_cm_ = the requested step, last_step_achieved_cm_ = actual.
    // shortfall = planned - achieved (≥ 0); cmd_run_avoid adds this to next step.
    // (step_cm_ atomic read here — body_pre_cycle's local 'step' isn't in scope)
    {
        const double planned  = (double)step_cm_.load();
        const double achieved = rail_pos_cm_.load();
        last_step_planned_cm_.store(planned);
        last_step_achieved_cm_.store(achieved);
        if (achieved + 0.5 < planned) {
            std::cout << "[step_down] Phase A shortfall: planned " << planned
                      << "cm achieved " << achieved << "cm — "
                      << (planned - achieved) << "cm to compensate next step\n";
            evt_("step_shortfall planned=" + std::to_string((int)planned) +
                 " achieved=" + std::to_string((int)achieved));
        }
    }

    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    // No-progress check: body Phase A 應該讓 rail 到達 +step_cm。如果 backup 全
    // 退到 rail≈0，代表 body 沒實際下降，feet phase 不需要動（否則會無謂拉鋼索）。
    if (rail_pos_cm_.load() < 0.5) {
        std::cout << "[step_down] body backed up to origin (rail=" << rail_pos_cm_.load()
                  << " cm) — skip feet phase, no actual descent\n";
        // [arm rope protect TEMP 2026-05-21] no_progress 跳過 cleaning sweep
        // → arm 卡在 DEPLOY CENTER。手動 PARK 一次回 home（此路徑下降淨值=0、
        // 桿子相對位置不變，PARK 軌跡安全）。
        ensure_arm_parked_after_rope_("step_down_no_progress");
        evt_("step_down_no_progress body_rail_at_origin");
        motion_active_ = false;
        return "OK step_down_no_progress\n";
    }

    // [2026-05-22] Mid-step realign MOVED into body_pre_cycle — it now runs
    // in-window (feet-only) right after the body group is retracted, so it no
    // longer redundantly retracts+rebuilds the body. Old standalone call kept
    // commented for easy revert if the in-window approach misbehaves.
    // // Mid-step realign: body 剛吸好、feet 仍吸著上一輪位置 — 全 9 支都吸著，
    // // 對 realign 條件成立。提早 realign 避免 Phase B 又疊一次 drift。
    // lk.unlock();
    // {
    //     std::string realign_err = do_feet_realign_(/*force=*/false);
    //     if (!realign_err.empty()) {
    //         std::cout << "[step_down] mid-step realign FAIL (non-fatal): " << realign_err;
    //         evt_("mid_step_realign_fail step_down phase=A " + realign_err);
    //     }
    // }
    // lk.lock();
    // motion_active_ = true;

    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    // ---------- B. Feet phase (second, feet catches up to body) ----------
    auto feet_pre_cycle = [this, &after_feet_rail_hook, &before_feet_rail_hook]() -> std::string {
        // Pre-flight: clear ALL 9 ZDT stall flags. (See body_pre_cycle comment
        // for rationale — covers both load-bearing body AND about-to-retract feet.)
        if (try_or_pause_([this]() { return ensure_all_zdt_stall_clear_(); },
                          "feet_pre_stall_clear")) return "aborted";
        // [2026-06-02] SAFETY: verify BODY (anchor) all sealed BEFORE releasing
        // feet. Same rationale as body_pre_cycle anchor check.
        if (try_or_pause_([this]() -> bool {
            auto fails = vacuum_check_("body");
            if (fails.empty()) return false;
            std::string msg = "feet_pre_anchor_unsealed body=";
            for (size_t i = 0; i < fails.size(); ++i) {
                if (i) msg += ",";
                msg += std::to_string(fails[i]);
            }
            std::cout << "[safety] " << msg << " — REFUSE to release feet\n";
            evt_(msg);
            return true;
        }, "feet_pre_anchor_check")) return "aborted";
        // Release feet valve BEFORE retracting
        if (try_or_pause_([this]() { return pqw_set_relay_verified_(CH_VALVE_FEET, false); },
                          "feet_pre_valve_off")) return "aborted";
        // Poll-based wait — proceeds the moment all feet cups release, up to
        // VACUUM_RELEASE_WAIT_MS. On timeout drops into PausedOnError so
        // operator can verify cup release before pusher retract.
        std::vector<int> feet_slaves = {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2};
        if (try_or_pause_([this, &feet_slaves]() { return vacuum_wait_release_(feet_slaves, VACUUM_RELEASE_WAIT_MS); },
                          "feet_pre_vacuum_release")) return "aborted";

        // Other-group stall sweep: body 在 feet 真空釋放後可能 latch stall flag。
        clear_other_group_stalls_("feet");

        // Pipelined two-stage feet retract. Same rationale as body
        // (avoid ZDT stall on lingering cup adhesion).
        if (try_or_pause_([this, &feet_slaves]() { return pusher_two_stage_retract_(feet_slaves); },
                          "feet_pre_pusher_retract")) return "aborted";

        // [2026-05-27] before_feet_rail_hook — feet rail PR_trigger 前呼叫一次。
        // 給 cmd_step_down_sweep_before_after 用來 join pre-feet sweep round
        // （等 round 1 跑完才動 feet rail）。其他 cmd 傳空 hook、行為不變。
        if (before_feet_rail_hook) {
            std::cout << "[step_down] pre feet rail move → before_feet_rail_hook\n";
            before_feet_rail_hook();
        }

        // DM2J rails → absolute 0 (feet catches up, rail retracts)
        if (try_or_pause_([this]() { return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, 0.0); },
                          "feet_pre_rail_home")) return "aborted";
        rail_pos_cm_.store(0.0);
        // [2026-05-25] after_feet_rail_hook — feet rail 已回到 0。給
        // cmd_step_down_sweep_after_feet launch 背景 sweep 用。一般 cmd_step_down /
        // cmd_step_down_with_sweep 傳空 hook、行為不變。
        if (after_feet_rail_hook) {
            std::cout << "[step_down] feet rail home → after_feet_rail_hook\n";
            after_feet_rail_hook();
        }
        return "";
    };

    auto feet_backup_factory = [this](double backup_cm, const char* tag) {
        return [this, backup_cm, tag](bool dry_run) -> std::string {
            const double step = (double)step_cm_.load();
            const double rail_before = rail_pos_cm_.load();
            double target = rail_before + backup_cm;
            if (target > step) {
                if (!dry_run)
                    std::cout << "  [retry feet" << tag << "] target " << target
                              << " cm > step " << step << " — no more backup space, abort\n";
                return std::string("feet_backup_no_space") + tag;
            }
            if (dry_run) return "";
            // No crane motion — feet repositioning is body-anchored.
            std::cout << "  [retry feet" << tag << "] rail " << rail_before << " → " << target << " cm\n";
            if (try_or_pause_([this, target]() { return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, target); },
                              std::string("feet_backup_rail") + tag)) return "aborted";
            rail_pos_cm_.store(target);
            return "";
        };
    };
    auto feet_backup        = feet_backup_factory(VACUUM_BACKUP_CM,         "");
    auto feet_rescue_backup = feet_backup_factory(OBSTACLE_RESCUE_BACKUP_CM, "_rescue");

    int feet_retries = 0;
    int feet_rescues = 0;
    err = cycle_group_("feet", feet_pre_cycle, feet_backup, feet_rescue_backup, feet_retries, feet_rescues);
    if (!err.empty()) { motion_active_ = false; return "ERR " + err + "\n"; }

    // B: record feet's max over-extension for next step's body compensation
    last_feet_max_over_cm_.store(feet_max_overextend_cm_());

    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    // ---------- C. Cleaning sweep ----------
    // skip_cleaning_sweep=true 路徑：cmd_step_down_with_sweep 把 sweep 搬到背景
    // thread 連續跑（do_arm_clean_sweep_continuous_），這裡跳過避免重複。
    if (!skip_cleaning_sweep) {
        std::cout << "[step_down] start cleaning sweep (wall_mm=" << ARM_CLEAN_WALL_MM
                  << " rounds=" << ARM_CLEAN_ROUNDS << ")\n";
        std::string clean_reply = do_arm_clean_sweep_(ARM_CLEAN_WALL_MM, ARM_CLEAN_ROUNDS);
        motion_active_ = false;
        if (clean_reply.rfind("OK", 0) != 0) {
            std::cout << "[step_down] cleaning sweep FAIL: " << clean_reply;
            return clean_reply;
        }
    } else {
        motion_active_ = false;
        std::cout << "[step_down] cleaning sweep skipped (cmd_step_down_with_sweep handles it in parallel)\n";
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
        if (cm < STEP_CM_MIN || cm > STEP_CM_MAX) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << STEP_CM_MAX << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[step_down] start → Running (step=" << step_cm_.load() << " cm)\n";
    set_state_(State::Running);

    std::string r = do_step_down_();
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
        if (cm < STEP_CM_MIN || cm > STEP_CM_MAX) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << STEP_CM_MAX << ")\n";
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
        return do_arm_clean_sweep_continuous_(ARM_CLEAN_WALL_MM, sweep_keep_going);
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
                                    std::function<void()> before_feet_rail_hook) {
    // unique_lock — same reason as do_step_down_ (release before mid-step realign).
    std::unique_lock<std::mutex> lk(motion_mtx_);
    abort_flag    = false;
    motion_active_ = true;

    // ---------- A. Feet phase (first, feet ascends STEP_CM) ----------
    auto feet_pre_cycle = [this, &after_feet_rail_hook, &before_feet_rail_hook]() -> std::string {
        // Pre-flight: clear ALL 9 ZDT stall flags before any valve / motion ops.
        if (try_or_pause_([this]() { return ensure_all_zdt_stall_clear_(); },
                          "feet_pre_stall_clear_up")) return "aborted";
        // [2026-06-02] SAFETY: verify BODY (anchor) all sealed BEFORE releasing
        // feet. Same rationale as do_step_down_ pre_cycle anchor checks.
        if (try_or_pause_([this]() -> bool {
            auto fails = vacuum_check_("body");
            if (fails.empty()) return false;
            std::string msg = "feet_pre_anchor_unsealed_up body=";
            for (size_t i = 0; i < fails.size(); ++i) {
                if (i) msg += ",";
                msg += std::to_string(fails[i]);
            }
            std::cout << "[safety] " << msg << " — REFUSE to release feet\n";
            evt_(msg);
            return true;
        }, "feet_pre_anchor_check_up")) return "aborted";
        // Release feet valve BEFORE retracting pushers (cup adhesion lock-out).
        if (try_or_pause_([this]() { return pqw_set_relay_verified_(CH_VALVE_FEET, false); },
                          "feet_pre_valve_off_up")) return "aborted";
        std::vector<int> feet_slaves = {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2};
        if (try_or_pause_([this, &feet_slaves]() { return vacuum_wait_release_(feet_slaves, VACUUM_RELEASE_WAIT_MS); },
                          "feet_pre_vacuum_release_up")) return "aborted";

        // Other-group stall sweep: body 在 feet 真空釋放後可能 latch stall flag。
        clear_other_group_stalls_("feet");

        // Pipelined two-stage feet retract.
        if (try_or_pause_([this, &feet_slaves]() { return pusher_two_stage_retract_(feet_slaves); },
                          "feet_pre_pusher_retract_up")) return "aborted";

        // [2026-05-27] before_feet_rail_hook — feet rail PR_trigger 前呼叫一次。
        // 給 cmd_step_up_sweep_before_after 用來 join pre-feet sweep round。
        if (before_feet_rail_hook) {
            std::cout << "[step_up] pre feet rail move → before_feet_rail_hook\n";
            before_feet_rail_hook();
        }

        // DM2J rails → absolute +step_cm (feet ascends; body anchored, no crane motion).
        const int step = step_cm_.load();
        if (try_or_pause_([this, step]() { return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, (double)step); },
                          "feet_pre_rail_forward_up")) return "aborted";
        rail_pos_cm_.store((double)step);
        // [2026-05-22] after_feet_rail_hook — feet rail 已到 +step（robot frame
        // 在新垂直位置）。cmd_step_up_sweep_after_feet 用這 hook launch 背景
        // cleaning sweep。一般 cmd_step_up / cmd_step_up_with_sweep 傳空 hook。
        if (after_feet_rail_hook) {
            std::cout << "[step_up] feet rail done → after_feet_rail_hook\n";
            after_feet_rail_hook();
        }
        return "";
    };

    auto feet_backup_factory_up = [this](double backup_cm, const char* tag) {
        return [this, backup_cm, tag](bool dry_run) -> std::string {
            const double rail_before = rail_pos_cm_.load();
            const double target = rail_before - backup_cm;
            if (target < 0.0) {
                if (!dry_run)
                    std::cout << "  [retry feet up" << tag << "] target " << target
                              << " cm < 0 — no more backup space, abort\n";
                return std::string("feet_backup_no_space_up") + tag;
            }
            if (dry_run) return "";
            std::cout << "  [retry feet up" << tag << "] rail " << rail_before << " → " << target << " cm\n";
            if (try_or_pause_([this, target]() { return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, target); },
                              std::string("feet_backup_rail_up") + tag)) return "aborted";
            rail_pos_cm_.store(target);
            return "";
        };
    };
    auto feet_backup        = feet_backup_factory_up(VACUUM_BACKUP_CM,         "");
    auto feet_rescue_backup = feet_backup_factory_up(OBSTACLE_RESCUE_BACKUP_CM, "_rescue");

    // [arm rope protect TEMP 2026-05-21 — DISABLED 2026-05-22] step_up 腳組
    // 往上前先 stow arm。user 2026-05-22 把所有 deploy center 註解掉。
    //if (try_or_pause_([this]() { return ensure_arm_center_for_rope_("step_up_pre_feet"); },
    //                  "arm_stow_for_step_up_feet")) { motion_active_ = false; return "ERR aborted\n"; }

    int feet_retries = 0;
    int feet_rescues = 0;
    std::string err = cycle_group_("feet", feet_pre_cycle, feet_backup, feet_rescue_backup, feet_retries, feet_rescues);
    if (!err.empty()) { motion_active_ = false; return "ERR " + err + "\n"; }

    // B: record feet's max over-extension for body phase (this step) and next step compensation
    last_feet_max_over_cm_.store(feet_max_overextend_cm_());

    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    // No-progress check: feet Phase A 應該讓 rail 到達 +step_cm。如果 backup 全
    // 退到 rail≈0，代表 feet 沒實際上爬，body phase 不需要動（否則 crane 會無謂
    // retract step+margin、拉扯機構）。
    if (rail_pos_cm_.load() < 0.5) {
        std::cout << "[step_up] feet backed up to origin (rail=" << rail_pos_cm_.load()
                  << " cm) — skip body phase, no actual ascent\n";
        // [arm rope protect TEMP 2026-05-21] no_progress 跳過 cleaning sweep
        // → arm 卡在 DEPLOY CENTER。手動 PARK 一次回 home（無實際升降、桿子相
        // 對位置不變、PARK 安全）。
        ensure_arm_parked_after_rope_("step_up_no_progress");
        evt_("step_up_no_progress feet_rail_at_origin");
        motion_active_ = false;
        return "OK step_up_no_progress\n";
    }

    // [2026-05-22] Mid-step realign MOVED into body_pre_cycle — it now runs
    // in-window (feet-only) right after the body group is retracted, so it no
    // longer redundantly retracts+rebuilds the body. Old standalone call kept
    // commented for easy revert if the in-window approach misbehaves.
    // // Mid-step realign: feet 剛吸好、body 仍吸著上一輪位置 — 全 9 支都吸著，
    // // 對 realign 條件成立。同 do_step_down_ Phase A 後的 realign 註解。
    // lk.unlock();
    // {
    //     std::string realign_err = do_feet_realign_(/*force=*/false);
    //     if (!realign_err.empty()) {
    //         std::cout << "[step_up] mid-step realign FAIL (non-fatal): " << realign_err;
    //         evt_("mid_step_realign_fail step_up phase=A " + realign_err);
    //     }
    // }
    // lk.lock();
    // motion_active_ = true;

    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    // ---------- B. Body phase (second, body catches up upward to feet) ----------
    auto body_pre_cycle = [this]() -> std::string {
        // Pre-flight: clear ALL 9 ZDT stall flags before any valve / motion ops.
        if (try_or_pause_([this]() { return ensure_all_zdt_stall_clear_(); },
                          "body_pre_stall_clear_up")) return "aborted";
        // [2026-06-02] SAFETY: verify FEET (anchor) all sealed BEFORE releasing
        // body. Same rationale as do_step_down_ pre_cycle anchor checks.
        if (try_or_pause_([this]() -> bool {
            auto fails = vacuum_check_("feet");
            if (fails.empty()) return false;
            std::string msg = "body_pre_anchor_unsealed_up feet=";
            for (size_t i = 0; i < fails.size(); ++i) {
                if (i) msg += ",";
                msg += std::to_string(fails[i]);
            }
            std::cout << "[safety] " << msg << " — REFUSE to release body\n";
            evt_(msg);
            return true;
        }, "body_pre_anchor_check_up")) return "aborted";
        // Release body + center valves BEFORE retracting pushers.
        if (try_or_pause_([this]() { return pqw_set_relay_verified_(CH_VALVE_BODY, false); },
                          "body_pre_valve_off_body_up")) return "aborted";
        // [TEMP DISABLED 2026-05-04] center pusher (slave 9) skipped during step_up
        //if (try_or_pause_([this]() { return pqw_set_relay_verified_(CH_VALVE_CENTER, false); },
        //                  "body_pre_valve_off_center_up")) return "aborted";
        {
            // [TEMP DISABLED 2026-05-04] ZDT_C removed from vacuum_wait_release_ list
            std::vector<int> body_center = {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2 /*, ZDT_C*/};
            if (try_or_pause_([this, &body_center]() { return vacuum_wait_release_(body_center, VACUUM_RELEASE_WAIT_MS); },
                              "body_pre_vacuum_release_up")) return "aborted";
        }

        // Other-group stall sweep: feet 在 body 真空釋放後可能 latch stall flag。
        clear_other_group_stalls_("body");

        // [TEMP DISABLED 2026-05-04] center pusher two-stage retract skipped
        //// Two-stage retract for center pusher.
        //if (try_or_pause_([this]() { return pusher_move_(ZDT_C, PUSHER_EXTEND_PULSE * 2 / 3, PUSHER_RPM_RETRACT, PUSHER_ACC_RETRACT); },
        //                  "body_pre_center_retract_half_up")) return "aborted";
        //if (try_or_pause_([this]() { return pusher_move_(ZDT_C, PUSHER_RETRACT_PULSE, PUSHER_RPM_RETRACT_FULL, PUSHER_ACC_RETRACT); },
        //                  "body_pre_center_retract_full_up")) return "aborted";

        // Body pushers pipelined two-stage retract.
        std::vector<int> body_slaves = {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2};
        if (try_or_pause_([this, &body_slaves]() { return pusher_two_stage_retract_(body_slaves); },
                          "body_pre_pusher_retract_up")) return "aborted";

        // 2026-05-19 dynamic crane motion, based on ACTUAL DM2J position.
        // Pattern for step_up body climb: NO pre-climb pay_out — the body
        // climbing UP toward the crane generates rope slack on its own.
        //   DM2J climbs (rope slacks delta cm) → retract delta (collect slack).
        //   Net retract = delta = actual body climb.
        double dm2j_L = 0, dm2j_R = 0;
        if (try_or_pause_([this, &dm2j_L]() { return dm2j_read_pos_robust_(DM2J_LEFT_FOOT,  dm2j_L); },
                          "body_pre_dm2j_read_left_up")) return "aborted";
        if (try_or_pause_([this, &dm2j_R]() { return dm2j_read_pos_robust_(DM2J_RIGHT_FOOT, dm2j_R); },
                          "body_pre_dm2j_read_right_up")) return "aborted";
        const double rail_actual = (dm2j_L + dm2j_R) / 2.0;
        const double rail_target = 0.0;
        const double rail_delta  = rail_actual - rail_target;   // expected body climb (cm)
        std::cout << "[step_up] DM2J read L=" << dm2j_L << " R=" << dm2j_R
                  << " avg=" << rail_actual << " target=" << rail_target
                  << " delta=" << rail_delta << "cm\n";

        if (rail_delta > 0.5) {
            // DM2J rails → absolute 0 (body climbs mechanically; climbing UP
            // toward the crane slacks the rope delta cm on its own)
            if (try_or_pause_([this, rail_target]() { return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, rail_target); },
                              "body_pre_rail_home_up")) return "aborted";
            rail_pos_cm_.store(rail_target);

            // retract delta — collect the slack the climb created. No pre-climb
            // pay_out needed: the body climbing up generates the slack itself.
            {
                const int retract_cm = (int)std::lround(rail_delta);
                std::cout << "[step_up] crane retract " << retract_cm
                          << " (= body climb delta)\n";
                if (try_or_pause_([this, retract_cm]() { return crane_retract_safe_(retract_cm).rfind("OK", 0) != 0; },
                                  "body_pre_crane_retract_up")) return "aborted";
            }
        } else {
            std::cout << "[step_up] rail already at target (delta " << rail_delta
                      << "cm <= 0.5) — skip crane + DM2J ops\n";
            rail_pos_cm_.store(rail_target);
        }

        // [TEMP DISABLED 2026-05-04] center pusher re-extend + valve ON skipped
        //// Re-extend center + re-engage center valve.
        //if (try_or_pause_([this]() { return pusher_move_(ZDT_C, PUSHER_EXTEND_PULSE); },
        //                  "body_pre_center_extend_up")) return "aborted";
        //if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_CENTER, true); },
        //                  "body_pre_valve_on_center_up")) return "aborted";

        // In-window feet realign (2026-05-22): body is retracted here and
        // cycle_group_ rebuilds it right after — so feet realign runs feet-only
        // (in_window=true), skipping its own Phase A/B body retract+rebuild.
        // Non-fatal: a realign error is logged but does not abort the step
        // (matches the old separate mid-step realign call's non-fatal handling).
        {
            std::string rerr = do_feet_realign_(/*force=*/false, /*in_window=*/true);
            if (!rerr.empty()) {
                std::cout << "[step_up] in-window feet realign FAIL (non-fatal): " << rerr;
                evt_("mid_step_realign_fail step_up " + rerr);
            }
        }
        // realign may have flipped motion_active_ off on an internal error path —
        // restore it: cycle_group_ is about to disable_seal-extend the body and
        // pressure_poll_loop_ must stay parked (stale-frame race, 2026-05-22).
        motion_active_ = true;
        return "";
    };

    auto body_backup_factory_up = [this](double backup_cm, const char* tag) {
        return [this, backup_cm, tag](bool dry_run) -> std::string {
            const double step        = (double)step_cm_.load();
            // 2026-05-18: read ACTUAL DM2J position, not rail_pos_cm_ atomic.
            // Atomic can be stale if a previous step left rail at an
            // intermediate position — then crane pay_out (= rail_delta) would
            // mismatch the real DM2J absolute move (bench: rail at 5cm, target
            // abs 20cm → real move 15cm but crane did 20cm).
            double dm2j_L = 0, dm2j_R = 0;
            if (dm2j_read_pos_robust_(DM2J_LEFT_FOOT,  dm2j_L) ||
                dm2j_read_pos_robust_(DM2J_RIGHT_FOOT, dm2j_R)) {
                if (!dry_run)
                    std::cout << "  [retry body up" << tag << "] DM2J read fail — abort backup\n";
                return std::string("body_backup_dm2j_read_fail_up") + tag;
            }
            const double rail_before = (dm2j_L + dm2j_R) / 2.0;
            const double target      = rail_before + backup_cm;
            if (target > step) {
                if (!dry_run)
                    std::cout << "  [retry body up" << tag << "] target " << target
                              << " cm > step " << step << " — no more backup space, abort\n";
                return std::string("body_backup_no_space_up") + tag;
            }
            if (dry_run) return "";
            const int backup_int = (int)std::lround(target - rail_before);   // = backup_cm
            // Crane (2026-05-19 per user): pay out (backup_cm + margin) BEFORE
            // the rail move so the body descends with generous slack; then after
            // the rail is in position, retract backup_cm back via
            // crane_retract_safe_ — the weight-threshold monitor stops the
            // re-tension early (same as step_up/down retracts), so the final
            // tension is feedback-determined, not a blind fixed-cm pull.
            {
                const int pay_cm = backup_int + BACKUP_PAYOUT_MARGIN_CM;
                std::ostringstream oss;
                oss << "pay_out " << pay_cm;
                const std::string cmd_str = oss.str();
                std::cout << "  [retry body up" << tag << "] crane " << cmd_str
                          << " (backup " << backup_int << " + margin "
                          << BACKUP_PAYOUT_MARGIN_CM << ")\n";
                // [arm rope protect TEMP 2026-05-21 — DISABLED 2026-05-22]
                //if (try_or_pause_([this]() { return ensure_arm_center_for_rope_("body_backup_pay_out_up"); },
                //                  std::string("arm_stow_for_backup_pay_out_up") + tag)) return "aborted";
                // [2026-06-05] Dynamic timeout from pay_cm (was default 60s).
                const int pay_timeout = crane_motion_timeout_sec_(pay_cm);
                if (try_or_pause_([this, cmd_str, pay_timeout]() { return crane_cmd_(cmd_str, pay_timeout).rfind("OK", 0) != 0; },
                                  std::string("body_backup_crane_pay_out_up") + tag)) return "aborted";
            }
            std::cout << "  [retry body up" << tag << "] rail " << rail_before << " → " << target << " cm\n";
            if (try_or_pause_([this, target]() { return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, target); },
                              std::string("body_backup_rail_up") + tag)) return "aborted";
            rail_pos_cm_.store(target);
            // Rail in position — retract the slack back. Capped at backup_cm;
            // crane_retract_safe_'s monitor stops it early on the weight limit.
            std::cout << "  [retry body up" << tag << "] crane retract " << backup_int
                      << " (tension-monitored re-tension)\n";
            if (try_or_pause_([this, backup_int]() { return crane_retract_safe_(backup_int).rfind("OK", 0) != 0; },
                              std::string("body_backup_crane_retract_up") + tag)) return "aborted";
            return "";
        };
    };
    auto body_backup        = body_backup_factory_up(VACUUM_BACKUP_CM,         "");
    auto body_rescue_backup = body_backup_factory_up(OBSTACLE_RESCUE_BACKUP_CM, "_rescue");

    int body_retries = 0;
    int body_rescues = 0;
    err = cycle_group_("body", body_pre_cycle, body_backup, body_rescue_backup, body_retries, body_rescues);
    if (!err.empty()) { motion_active_ = false; return "ERR " + err + "\n"; }

    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    // ---------- C. Cleaning sweep ----------
    // skip_cleaning_sweep=true 路徑：cmd_step_up_with_sweep 把 sweep 搬到背景 thread
    // 連續跑（do_arm_clean_sweep_continuous_），這裡跳過避免重複。
    if (!skip_cleaning_sweep) {
        std::cout << "[step_up] start cleaning sweep (wall_mm=" << ARM_CLEAN_WALL_MM
                  << " rounds=" << ARM_CLEAN_ROUNDS << ")\n";
        std::string clean_reply = do_arm_clean_sweep_(ARM_CLEAN_WALL_MM, ARM_CLEAN_ROUNDS);
        motion_active_ = false;
        if (clean_reply.rfind("OK", 0) != 0) {
            std::cout << "[step_up] cleaning sweep FAIL: " << clean_reply;
            return clean_reply;
        }
    } else {
        motion_active_ = false;
        std::cout << "[step_up] cleaning sweep skipped (cmd_step_up_with_sweep handles it in parallel)\n";
    }

    return "OK step_up_done\n";
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
std::string WashRobot::do_feet_realign_(bool force, bool in_window) {
    // in_window (2026-05-22): "feet-only" mode. Caller is a step's body cycle,
    // running inside body_pre_cycle with the body group ALREADY retracted and
    // motion_mtx_ ALREADY held. realign then does ONLY the feet realignment
    // (Phase 0/2/2.5) and SKIPS Phase 1 (crane assist), Phase A (body retract),
    // Phase B (body rebuild) and Phase 5 (crane restore) — the enclosing step
    // body cycle already retracts + rebuilds the body, so realign doing it too
    // is pure redundant work. force / non-window callers (end-of-step,
    // standalone) pass in_window=false → full behaviour, unchanged.
    //
    // realign_skip: slaves to entirely exclude from realign — both motion AND
    // drift threshold computation. Adds ZDT_C (center) on top of disabled_zdt_slaves_
    // because center is [TEMP DISABLED 2026-05-04] in step_down/up flows; its
    // encoder accumulates without being reset (10cm physical limit but real_pos
    // can show 13cm+) → fake drift would dominate threshold and force unnecessary realign.
    // in_window also excludes body 5-8: they are retracted (Phase 0 would read a
    // near-zero position and inject fake drift) and rebuilt by the step itself.
    auto realign_skip = [this, in_window](int s) -> bool {
        if (disabled_zdt_slaves_.count(s) > 0 || s == ZDT_C) return true;
        if (in_window && s >= 5 && s <= 8) return true;
        return false;
    };

    // [NEW 2026-05-05] Phase 0: read live ZDT real_pos for eligible slaves and
    // update last_seal_pulse_ to match — last_seal_pulse_ from prior cycles
    // can drift from actual position (disable_seal lock, manual jog, etc.),
    // so re-syncing here gives realign accurate delta input.
    //
    // Conversion: ZDT pos_mode pulse ↔ encoder real_pos (deg) → pulses = deg*10
    // (same formula as pusher_extend_with_disable_seal_ deg_to_pulse lambda).
    // On read fail: keep prior last_seal_pulse_ value (don't overwrite with 0).
    // Sanity bounds for ZDT real_pos: SMC LEYG25 physical stroke = 20 cm = 60000
    // pulse = 6000°. Anything outside this range (with tolerance for encoder noise
    // around retract zero, e.g. -0.01° observed on bench 2026-05-06) is corrupt
    // frame data (ZDT firmware sometimes returns garbage despite get_system_status
    // reporting success — frame alignment / broadcast echo race). Keep prev_recorded.
    constexpr double REAL_POS_MIN_DEG = -10.0;   // tolerate small negatives at zero
    constexpr double REAL_POS_MAX_DEG = 6000.0;

    std::cout << "[realign] phase 0: read live ZDT positions\n";
    for (int s = 1; s <= 9; ++s) {
        if (realign_skip(s)) continue;
        if (Z_(s).get_system_status()) {
            std::cout << "[realign] phase 0 slave " << s
                      << " status read fail — keep last_seal_pulse_="
                      << last_seal_pulse_[s - 1].load() << "\n";
            continue;
        }
        const double real_pos_deg = Z_(s).status.real_pos;
        if (real_pos_deg < REAL_POS_MIN_DEG || real_pos_deg > REAL_POS_MAX_DEG) {
            std::cout << "[realign] phase 0 slave " << s
                      << " real_pos=" << real_pos_deg
                      << "° OUT OF RANGE [" << REAL_POS_MIN_DEG << ", " << REAL_POS_MAX_DEG
                      << "] — corrupt frame, keep last_seal_pulse_="
                      << last_seal_pulse_[s - 1].load() << "\n";
            evt_("realign_phase0_bad_pos slave=" + std::to_string(s));
            continue;
        }
        const int actual_pulse = (int)(real_pos_deg * 10.0);
        const int prev_recorded = last_seal_pulse_[s - 1].load();
        last_seal_pulse_[s - 1].store(actual_pulse);
        std::cout << "[realign] phase 0 slave " << s
                  << " real_pos=" << real_pos_deg
                  << "° pulse=" << actual_pulse
                  << " (prev_recorded=" << prev_recorded << ")\n";
    }

    // Compute per-slave delta in pulses (how much each cup is over preset).
    // deltas[i] > 0 means cup over-extended this much, will be retracted.
    // deltas[i] <= 0 means already at preset, skipped in retract phase.
    std::vector<int> deltas(9, 0);
    double max_abs_drift_cm = 0.0;
    double sum_abs_drift_cm = 0.0;
    int    count_included   = 0;
    for (int s = 1; s <= 9; ++s) {
        if (realign_skip(s)) continue;   // skip disabled / ZDT_C from drift compute
        const int preset = preset_extend_pulse_for_slave_(s);
        const int last   = last_seal_pulse_[s - 1].load();   // refreshed in phase 0
        deltas[s - 1] = last - preset;
        const double drift_pulses = (double)(last - preset);
        const double drift_cm = (s >= 1 && s <= 4)
                              ? drift_pulses / (20000.0 / 7.0)
                              : drift_pulses / (30000.0 / 10.0);
        const double abs_drift_cm = std::fabs(drift_cm);
        if (abs_drift_cm > max_abs_drift_cm) max_abs_drift_cm = abs_drift_cm;
        sum_abs_drift_cm += abs_drift_cm;
        count_included++;
        std::cout << "[realign] slave " << s << " preset=" << preset
                  << " actual=" << last << " delta=" << deltas[s - 1]
                  << " drift_cm=" << drift_cm << "\n";
    }
    const double mean_abs_drift_cm = (count_included > 0)
                                   ? (sum_abs_drift_cm / count_included)
                                   : 0.0;

    // Threshold check (2026-05-22 hybrid): trigger 如果
    //   單顆 max 漂超過 REALIGN_THRESHOLD_CM(3.0cm) — outlier safety net
    //   OR 平均漂超過 REALIGN_THRESHOLD_MEAN_CM(1.0cm) — 累積式 trigger
    const bool max_triggered  = max_abs_drift_cm  > REALIGN_THRESHOLD_CM;
    const bool mean_triggered = mean_abs_drift_cm > REALIGN_THRESHOLD_MEAN_CM;
    if (!max_triggered && !mean_triggered) {
        std::cout << "[realign] threshold not met: max=" << max_abs_drift_cm
                  << "cm (limit " << REALIGN_THRESHOLD_CM
                  << ") mean=" << mean_abs_drift_cm
                  << "cm (limit " << REALIGN_THRESHOLD_MEAN_CM << ") — skip\n";
        return "";
    }

    // in_window: caller (step body cycle) already holds motion_mtx_ — don't re-lock.
    std::unique_lock<std::mutex> lk(motion_mtx_, std::defer_lock);
    if (!in_window) lk.lock();
    motion_active_ = true;

    const char* trigger_reason = max_triggered
                               ? (mean_triggered ? "max+mean" : "max")
                               : "mean";
    evt_(std::string("realign_start force=") + (force ? "1" : "0")
         + " trigger=" + trigger_reason
         + " max_abs_drift_cm=" + std::to_string(max_abs_drift_cm)
         + " mean_abs_drift_cm=" + std::to_string(mean_abs_drift_cm));
    std::cout << "[realign] start, force=" << force
              << " trigger=" << trigger_reason
              << " max=" << max_abs_drift_cm << "cm (limit " << REALIGN_THRESHOLD_CM
              << ") mean=" << mean_abs_drift_cm << "cm (limit " << REALIGN_THRESHOLD_MEAN_CM
              << ")\n";

    // Tail cleanup: scan & clear any latched stall flags on all 9 slaves.
    // Called before every return path below — Phase 2 stall release was already
    // done by zdt_wait_motion_done_, but emergency_stop on other slaves can
    // latch their stall flags too, and pos_mode_send fail leaves drives in
    // unknown state. Without this sweep, a latched flag silently rejects the
    // next motion command on subsequent cycles.
    // Retry: each slave gets up to MAX_RETRIES status reads. Single-shot reads
    // can hit (a) get_system_status comm fail (CRC / TCP gateway frame issue)
    // or (b) success-return with garbage real_pos (observed 2026-05-06: slave
    // 5 read pos=1.5e7°, slave 7 read 3.3e6° while stall_flag genuinely SET —
    // the corrupt frame happened to read stall_flag bit as 0, masking the
    // latched flag). Retry until a reading both succeeds and lies in the
    // physical sanity range.
    auto sweep_stalls = [&]() {
        constexpr int SWEEP_MAX_RETRIES = 3;
        constexpr int SWEEP_RETRY_MS    = 30;
        for (int s = 1; s <= 9; ++s) {
            if (realign_skip(s)) continue;
            bool got_valid = false;
            for (int attempt = 0; attempt < SWEEP_MAX_RETRIES; ++attempt) {
                if (Z_(s).get_system_status()) {
                    std::cout << "[realign] post-cleanup slave " << s
                              << " status read FAIL (attempt " << (attempt + 1)
                              << "/" << SWEEP_MAX_RETRIES << ")\n";
                    sleep_ms_(SWEEP_RETRY_MS);
                    continue;
                }
                const double pos = Z_(s).status.real_pos;
                if (pos < REAL_POS_MIN_DEG || pos > REAL_POS_MAX_DEG) {
                    std::cout << "[realign] post-cleanup slave " << s
                              << " bad pos " << pos << "° (corrupt frame, attempt "
                              << (attempt + 1) << "/" << SWEEP_MAX_RETRIES << ")\n";
                    sleep_ms_(SWEEP_RETRY_MS);
                    continue;
                }
                got_valid = true;
                if (Z_(s).status.stall_flag) {
                    std::cout << "[realign] post-cleanup slave " << s
                              << " stall_flag SET (pos=" << pos << "°) → release\n";
                    evt_("realign_post_stall_clear slave=" + std::to_string(s));
                    Z_(s).release_stall_flag();
                }
                break;
            }
            if (!got_valid) {
                std::cout << "[realign] post-cleanup slave " << s
                          << " UNREADABLE after " << SWEEP_MAX_RETRIES
                          << " retries — flag state unknown\n";
                evt_("realign_post_stall_read_fail slave=" + std::to_string(s));
            }
        }
    };

    // Phase 0: pre-flight stall clear — ZDT firmware silently rejects pos_mode
    // commands when stall_flag is latched, which would corrupt subsequent
    // equalize / retract motions. Check all 9 slaves and clear any stale stall.
    {
        int cleared = 0;
        for (int s = 1; s <= 9; ++s) {
            if (realign_skip(s)) continue;
            if (Z_(s).get_system_status()) {
                std::cout << "[realign] pre-check slave " << s
                          << " status read fail (skip clear)\n";
                continue;
            }
            if (Z_(s).status.stall_flag) {
                std::cout << "[realign] pre-check slave " << s
                          << " stall_flag SET (pos=" << Z_(s).status.real_pos << "°) → release\n";
                evt_("realign_pre_stall_clear slave=" + std::to_string(s));
                Z_(s).release_stall_flag();
                ++cleared;
            }
        }
        if (cleared > 0) {
            sleep_ms_(100);   // give firmware time to settle after multiple clears
            std::cout << "[realign] pre-flight cleared " << cleared << " latched stall flag(s)\n";
        }
    }

    // Feet slave list — used by both pre-checks below (and Phase 2 motion skip logic)
    const std::vector<int> phase2_slaves = {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2};   // 1,2,3,4

    // === Pre-check #1 (NEW 2026-05-06): feet vacuum, BEFORE Phase A ===
    // Machine still in original sealed state (8/9 cups attached). Failure here
    // is the safe place to pause — feet not yet committed to supporting body
    // alone (Phase A hasn't retracted body). On unsealed: PausedOnError + await
    // user. Operator may inspect / fix cup, then retry / skip / abort.
    //   continue (retry): re-run vacuum check
    //   skip: proceed with realign at user's risk (DANGEROUS if cup truly unsealed —
    //         Phase A will retract body, leaving 3 feet + crane to hold)
    //   abort: return ERR, machine state UNCHANGED, no recovery needed
    while (true) {
        auto pre_fails = vacuum_check_("all");
        pre_fails.erase(std::remove_if(pre_fails.begin(), pre_fails.end(),
            [&](int s) {
                return realign_skip(s)
                    || s == ZDT_RB1 || s == ZDT_LB1
                    || s == ZDT_RB2 || s == ZDT_LB2;
            }), pre_fails.end());
        if (pre_fails.empty()) {
            std::cout << "[realign] pre-check #1 OK (feet 1,2,3,4 sealed)\n";
            break;
        }
        std::string msg = "realign_pre_A_unsealed=";
        for (size_t i = 0; i < pre_fails.size(); ++i) {
            if (i) msg += ",";
            msg += std::to_string(pre_fails[i]);
        }
        std::cout << "[realign] " << msg << " — pausing for user (machine still safe)\n";
        evt_(msg);

        PauseAction action = await_user_intervention_("realign_pre_A_feet_unsealed");
        if (action == PauseAction::Abort) {
            sweep_stalls();
            motion_active_ = false;
            return "ERR realign_pre_A_aborted\n";
        }
        if (action == PauseAction::Skip) {
            std::cout << "[realign] pre-check #1 SKIP — proceeding (DANGER: cup may be loose)\n";
            evt_("realign_pre_A_user_skip");
            break;
        }
        // Retry: loop back, re-check (operator may have fixed cup manually)
        std::cout << "[realign] pre-check #1 RETRY — re-reading vacuum\n";
    }

    // === Phase 1: crane assist (moved here 2026-05-19 per user — runs BEFORE
    // Phase A so the rope is loaded up before the body cups release; no sudden
    // load transfer when body releases). ===
    // ONE continuous monitored retract via crane_retract_safe_ (2026-05-22 per
    // user — replaces the old crane_retract_to_weight_ 1cm-step loop that pulsed
    // the crane on/off every cm). crane_retract_safe_ sends a single
    // "retract <cm>"; its monitor stops the crane the instant rope tension hits
    // rope_weight_limit_per_sensor_kg_() — exactly realign's threshold — capped
    // at REALIGN_CRANE_ASSIST_MAX_CM. The reply carries no distance, so read
    // rope length before/after: Phase 5 caps its pay_out at crane_assist_actual_cm
    // to keep net rope ≤ 0.
    int crane_assist_actual_cm = 0;
    if (in_window) {
        // Phase 1 SKIPPED — the enclosing step body cycle owns all crane motion.
        // Feet realign never releases a cup (feet stay sealed throughout), so
        // there is no load-transfer event that needs the rope pre-loaded.
        std::cout << "[realign] phase 1 SKIPPED (in_window — step body cycle owns crane)\n";
    } else if (!crane_attached_.load()) {
        std::cout << "[realign] phase 1: crane detached — skip crane assist\n";
    } else {
        auto read_crane_length_left_cm = [this]() -> double {
            std::string st = crane_cmd_("status", 2);
            auto p = st.find("length_left=");
            if (p == std::string::npos) return -1.0;
            std::string val = st.substr(p + 12);   // 12 = strlen("length_left=")
            if (val.compare(0, 3, "ERR") == 0) return -1.0;
            try { return std::stod(val); } catch (...) { return -1.0; }
        };

        const double crane_len_before = read_crane_length_left_cm();
        std::string crane_reply = crane_retract_safe_(REALIGN_CRANE_ASSIST_MAX_CM);
        // "rope_weight_too_high" = rope already at/over the limit → Phase 1 goal
        // (rope pre-loaded before body release) already met; proceed, 0 cm.
        if (crane_reply.rfind("OK", 0) != 0 &&
            crane_reply.find("rope_weight_too_high") == std::string::npos) {
            sweep_stalls();
            motion_active_ = false;
            evt_("realign_fail crane_assist " + crane_reply);
            return "ERR realign_crane_assist_fail " + crane_reply + "\n";
        }
        // Actual retracted cm = rope length drop (retract makes length decrease).
        // Truncate toward 0 so the Phase 5 cap never exceeds what was retracted.
        const double crane_len_after = read_crane_length_left_cm();
        if (crane_len_before >= 0 && crane_len_after >= 0) {
            int d = (int)(crane_len_before - crane_len_after);
            if (d > 0) crane_assist_actual_cm = d;
        }
        std::cout << "[realign] crane assist done (" << crane_reply
                  << "), actual retracted=" << crane_assist_actual_cm << " cm\n";
    }

    // body_all (5,6,7,8) — referenced by Phase A and Phase B.
    const std::vector<int> body_all = {ZDT_RB1, ZDT_LB1, ZDT_RB2, ZDT_LB2};   // 5,6,7,8

    // === Phase A (NEW 2026-05-06): retract WHOLE body group (5,6,7,8) ===
    // Body cups (long 5,6 + short 7,8) accumulate drift from disable_seal locks
    // and repeated stalls. Phase 2 relative-mode retract on body has stalled 8
    // (and earlier 6) — solution is to retract all 4 body fully here, do feet
    // realign in Phase 2, then rebuild whole body via disable_seal in Phase B
    // (same mechanism as cycle_group_). Body valve OFF until Phase B re-engages.
    // in_window: SKIPPED — the enclosing step body cycle already retracted the body.
    if (in_window) {
        std::cout << "[realign] phase A SKIPPED (in_window — body already retracted by step body cycle)\n";
    } else {
        std::cout << "[realign] phase A: retract whole body group (5,6,7,8)\n";

        // A1. body valve OFF — releases all 4 body cups (5,6,7,8)
        if (pqw_set_relay_verified_(CH_VALVE_BODY, false)) {
            sweep_stalls();
            motion_active_ = false;
            evt_("realign_phaseA_valve_off_fail");
            return "ERR realign_phaseA_valve_off_fail\n";
        }
        // A2. Wait 5,6 cups release (vacuum drops to atmospheric)
        if (vacuum_wait_release_(body_all, VACUUM_RELEASE_WAIT_MS)) {
            std::cout << "[realign] phase A: vacuum release timeout on 5,6 (continuing)\n";
            // non-fatal — proceed even if timeout, retract will pull through
        }
        // A3. Pipelined two-stage retract 5,6 (slow-peel off wall → fast retract
        // to 0). Single-stage fast retract risks ZDT stall when cup adhesion lingers.
        if (pusher_two_stage_retract_(body_all)) {
            sweep_stalls();
            motion_active_ = false;
            evt_("realign_phaseA_retract_fail");
            return "ERR realign_phaseA_retract_fail\n";
        }
        // Body valve stays OFF — Phase B will turn it on before extend (matches
        // cycle_group_ pattern: valve ON immediately before pusher_extend_with_disable_seal_).
        std::cout << "[realign] phase A: body group retracted to 0, valve OFF\n";
    }

    // === Pre-check #2 (after Phase 1 crane assist + Phase A): catch crane-induced detach ===
    // Pre-check #1 already verified feet sealed in safe state. This second check
    // is a backup catching the rare case where Phase A retract or crane assist
    // disturbed a feet cup. WARNING: machine is in dangerous state here (body
    // retracted, only feet + crane support). Pause is risky but necessary if
    // detected — alternative is silent failure or auto-recovery (Phase B early)
    // which has its own complications.
    {
        // Stall sweep — release any latched flags (would silently reject pos_mode)
        int swept = 0;
        for (int s : phase2_slaves) {
            if (disabled_zdt_slaves_.count(s)) continue;
            if (Z_(s).get_system_status()) {
                std::cout << "[realign] phase 2 pre-check slave " << s
                          << " status read fail (skip)\n";
                continue;
            }
            if (Z_(s).status.stall_flag) {
                std::cout << "[realign] phase 2 pre-check slave " << s
                          << " stall_flag SET (pos=" << Z_(s).status.real_pos
                          << "°) → release\n";
                evt_("realign_phase2_pre_stall_clear slave=" + std::to_string(s));
                Z_(s).release_stall_flag();
                ++swept;
            }
        }
        if (swept > 0) sleep_ms_(100);
    }
    // Vacuum pre-check on feet (1,2,3,4). Body 5,6,7,8 retracted in Phase A
    // → not part of this check.
    // On unsealed: PausedOnError + await user. User options:
    //   continue (retry) → re-run vacuum check (operator may have fixed cup)
    //   skip            → accept risk, proceed to Phase 2 motion
    //   emergency_stop  → abort whole realign
    while (true) {
        auto pre_fails = vacuum_check_("all");
        pre_fails.erase(std::remove_if(pre_fails.begin(), pre_fails.end(),
            [&](int s) {
                // Only care about feet (1,2,3,4); exclude all body 5,6,7,8 (retracted
                // in Phase A) and disabled / ZDT_C
                return realign_skip(s)
                    || s == ZDT_RB1 || s == ZDT_LB1
                    || s == ZDT_RB2 || s == ZDT_LB2;
            }), pre_fails.end());
        if (pre_fails.empty()) {
            std::cout << "[realign] phase 2 pre-check OK (feet 1,2,3,4 sealed, no stalls)\n";
            break;
        }
        std::string msg = "realign_phase2_pre_vacuum_fail unsealed=";
        for (size_t i = 0; i < pre_fails.size(); ++i) {
            if (i) msg += ",";
            msg += std::to_string(pre_fails[i]);
        }
        std::cout << "[realign] " << msg << " — pausing for user intervention\n";
        evt_(msg);

        PauseAction action = await_user_intervention_("realign_phase2_pre_vacuum_fail");
        if (action == PauseAction::Abort) {
            sweep_stalls();
            motion_active_ = false;
            return "ERR realign_phase2_pre_vacuum_aborted\n";
        }
        if (action == PauseAction::Skip) {
            std::cout << "[realign] phase 2 pre-check SKIP — proceeding despite unsealed (user override)\n";
            evt_("realign_phase2_pre_vacuum_user_skip");
            break;
        }
        // Retry: loop back, re-check vacuum (cups may have been fixed manually)
        std::cout << "[realign] phase 2 pre-check RETRY — re-reading vacuum\n";
    }

    // [REMOVED 2026-05-05] Phase 1.5 within-group pre-equalize 已移除。
    // 原本想先把組內較短 cup 推到 max over_cm 再 retract（避免上下力道不均），
    // 但實測 equalize 把 feet 推前 3 cm 時，會把 body cups 從牆面拉脫、
    // body ZDT 馬達 holding torque 不足 → 連續 stall。副作用比原問題嚴重，移除。
    // 直接走 Phase 2 同步 retract 即可（每 cup 用 relative mode 縮自己的 delta）。

    // Phase 2: relative-mode motion to bring each cup to its preset.
    //   delta > 0  → RETRACT by delta, two-stage:
    //                Stage A: delta/3 at RETRACT_RPM (slow)        — break adhesion
    //                Stage B: remaining 2*delta/3 at RETRACT_RPM_FULL — finish quick
    //   delta < 0  → EXTEND by |delta|, single-stage at EXTEND_RPM (very slow)
    //                — pushing short cups into wall, gradual load on machine
    //   delta == 0 → skip
    //
    // Stage A retract motivates from observed cup-adhesion stall (slave 6
    // 2026-05-05, slave 3 2026-05-06): single-stage retract while sealed
    // stalls at the adhesion break point. Slow 1/3 first lets vacuum/torque
    // peel the cup; once unstuck, 2/3 finish at higher RPM is safe.
    //
    // RELATIVE MODE: was absolute mode pre-2026-05-04 and caused power-trip:
    // ZDT encoder real_pos accumulates over many cycles, so absolute target=preset
    // could be much smaller than current real_pos → motor tries to reverse beyond
    // hardstop → stall current spike → trips B-group 24V supply (everything dies).
    // 2026-05-18: ZDT_C (center, slave 9) commented out — not controlled in
    // current bench config (also in disabled_zdt_slaves_).
    std::vector<int> all_slaves = {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2,
                                   ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2 /*, ZDT_C*/};
    struct PhaseCmd {
        int slave;
        int dir;          // 0=forward(extend), 1=reverse(retract)
        int stageA_mag;   // first stage pulses (slow break-adhesion for retract; full mag for extend)
        int stageA_rpm;
        int stageA_acc;
        int stageB_mag;   // second stage pulses (0 if extend — single-stage)
        int stageB_rpm;
        int stageB_acc;
    };
    std::vector<PhaseCmd> cmds;
    for (int s : all_slaves) {
        if (realign_skip(s)) continue;
        // Skip whole body group (5,6,7,8) — handled by Phase A retract + Phase B disable_seal
        if (s == ZDT_RB1 || s == ZDT_LB1 || s == ZDT_RB2 || s == ZDT_LB2) continue;
        const int delta = deltas[s - 1];
        if (delta == 0) continue;
        PhaseCmd c{};
        c.slave = s;
        if (delta > 0) {            // RETRACT: two-stage
            c.dir         = 1;
            c.stageA_mag  = delta / 3;        // 1/3 slow break-adhesion
            c.stageA_rpm  = REALIGN_RETRACT_RPM;
            c.stageA_acc  = REALIGN_RETRACT_ACC;
            c.stageB_mag  = delta - c.stageA_mag;   // remaining ~2/3 fast finish
            c.stageB_rpm  = REALIGN_RETRACT_RPM_FULL;
            c.stageB_acc  = REALIGN_RETRACT_ACC_FULL;
        } else {                    // EXTEND: single-stage very slow
            c.dir         = 0;
            c.stageA_mag  = -delta;           // full
            c.stageA_rpm  = REALIGN_EXTEND_RPM;
            c.stageA_acc  = REALIGN_EXTEND_ACC;
            c.stageB_mag  = 0;
        }
        cmds.push_back(c);
    }

    // Helper: send + sync-trigger + wait for one stage. On stall: freeze all
    // still-moving slaves, set out flags, return. On pos_send fail: caller
    // sees pos_send_fail flag and bails out via the standard ERR path.
    bool any_stalled = false;
    int  stalled_slave = -1;
    bool pos_send_failed = false;
    int  pos_send_fail_slave = -1;
    auto run_stage = [&](int stage_num, bool is_stageA) {
        if (any_stalled || pos_send_failed) return;
        std::vector<int> moving;
        for (auto& c : cmds) {
            const int mag = is_stageA ? c.stageA_mag : c.stageB_mag;
            if (mag <= 0) continue;
            const int rpm = is_stageA ? c.stageA_rpm : c.stageB_rpm;
            const int acc = is_stageA ? c.stageA_acc : c.stageB_acc;
            std::cout << "[realign] phase 2 stage " << stage_num
                      << " slave " << c.slave
                      << (c.dir ? " RETRACT" : " EXTEND")
                      << " mag=" << mag << " rpm=" << rpm << "\n";
            if (Z_(c.slave).motion_control_pos_mode_nowait(c.dir, acc, rpm, mag,
                                                           /*mode=0=relative*/0, /*sync=1*/1, /*retry=1*/1)) {
                pos_send_failed = true;
                pos_send_fail_slave = c.slave;
                std::cout << "[realign] phase 2 stage " << stage_num
                          << " slave " << c.slave << " pos_mode_send FAIL\n";
                return;
            }
            moving.push_back(c.slave);
        }
        if (moving.empty()) return;
        Z_(moving.front()).trigger_sync_move();
        // [2026-05-28] Parallel wait via zdt_wait_motion_done_many_. Slaves are
        // sync-triggered so they move in parallel — waiting in parallel = max
        // (slave time) instead of sum (slave time). Old serial code paid up to
        // ~3-4s/realign extra.
        int stalled_id = -1;
        if (zdt_wait_motion_done_many_(moving, 15000, /*defer_stall=*/false, &stalled_id)) {
            any_stalled = true;
            stalled_slave = stalled_id;
            std::cout << "[realign] phase 2 stage " << stage_num
                      << " slave " << stalled_id << " STALL — emergency_stop other moving slaves\n";
            for (int s : moving) {
                if (s == stalled_id) continue;
                Z_(s).emergency_stop(false);
            }
            sleep_ms_(100);
            return;
        }
    };

    // [2026-06-01] Stage 0: pre-jog OUTWARD for retract slaves to unload
    // mechanism preload before Stage A. User insight (2026-06-01):
    //   machine weight + cup-pusher extended position → mechanism elastically
    //   deformed in extension direction. Stage A retract has to fight not just
    //   adhesion + vacuum but also the deformed mechanism springing back AGAINST
    //   the retract direction → peakI spikes 3000mA+ → STALL.
    //   A brief outward jog (~0.1 cm) at gentle rpm relieves the preload — cup
    //   moves SLIGHTLY further into wall, but mechanism returns to neutral. Then
    //   Stage A retracts cleanly.
    // Only applies to retract slaves (dir==1); extend slaves skip (no preload to
    // relieve — they're already trying to push deeper).
    auto run_jog = [&]() {
        if (any_stalled || pos_send_failed) return;
        std::vector<int> moving;
        for (auto& c : cmds) {
            if (c.dir != 1) continue;   // only retract slaves
            std::cout << "[realign] phase 2 stage 0 JOG"
                      << " slave " << c.slave
                      << " EXTEND mag=" << REALIGN_JOG_PULSES
                      << " rpm=" << REALIGN_JOG_RPM
                      << " (unload mechanism preload)\n";
            if (Z_(c.slave).motion_control_pos_mode_nowait(
                    /*dir=0=extend*/0, REALIGN_JOG_ACC, REALIGN_JOG_RPM,
                    REALIGN_JOG_PULSES,
                    /*mode=0=relative*/0, /*sync=1*/1, /*retry=1*/1)) {
                pos_send_failed = true;
                pos_send_fail_slave = c.slave;
                std::cout << "[realign] phase 2 stage 0 slave " << c.slave
                          << " pos_mode_send FAIL\n";
                return;
            }
            moving.push_back(c.slave);
        }
        if (moving.empty()) return;
        Z_(moving.front()).trigger_sync_move();
        int stalled_id = -1;
        if (zdt_wait_motion_done_many_(moving, 5000, /*defer_stall=*/false, &stalled_id)) {
            // [2026-06-01] Stage 0 JOG stall NON-FATAL. Sealed cup blocks
            // outward jog (vacuum holds cup to wall + cup itself blocks
            // further push). Cup may also be near physical endpoint at
            // high drift. Either way, preload-relief intent can't apply,
            // but Stage A retract is the actual corrective motion — let
            // it try. Without this every realign with heavily-sealed feet
            // aborts here → drift compounds across steps → body cups
            // physically can't reach wall (observed step 35cm 2026-06-01).
            std::cout << "[realign] phase 2 stage 0 JOG slave " << stalled_id
                      << " STALL (NON-FATAL — proceeding to Stage A)\n";
            // emergency_stop ALL moving slaves (including stalled one) so
            // firmware motion queues are clean before Stage A issues new
            // pos_mode commands.
            for (int s : moving) Z_(s).emergency_stop(false);
            // Release stall flag on the stalled slave — firmware would
            // otherwise reject Stage A's pos_mode command while flag set.
            // Other moving slaves were cleanly e-stopped (no stall flag).
            Z_(stalled_id).release_stall_flag();
            sleep_ms_(100);
            // Do NOT set any_stalled — Stage A still runs.
            return;
        }
    };

    // Stage 0: outward preload-relief jog (retract slaves only)
    run_jog();

    // Stage A: slow break-adhesion (retract 1/3) + extend full (very slow)
    run_stage(1, /*is_stageA=*/true);

    // Stage B: fast retract finish (2/3 remaining). Skipped if Stage A stalled
    // or no retract slaves had any 2/3-remaining motion to do.
    run_stage(2, /*is_stageA=*/false);

    if (pos_send_failed) {
        sweep_stalls();
        motion_active_ = false;
        evt_("realign_fail pos_mode_send slave=" + std::to_string(pos_send_fail_slave));
        return "ERR realign_pos_send_fail slave=" + std::to_string(pos_send_fail_slave) + "\n";
    }

    // Phase 2.5: re-read actual positions for ALL 9 slaves (whether or not they
    // were commanded to move). Phase 0 sync'd at start; positions changed during
    // Phase 2 motion; refresh again so last_seal_pulse_ reflects ground truth
    // before Phase 3 vacuum check / Phase 4 / next cycle.
    std::cout << "[realign] phase 2.5: re-read positions after motion\n";
    for (int s = 1; s <= 9; ++s) {
        if (realign_skip(s)) continue;
        // Skip whole body — at retract position now, will be re-extended in Phase B
        if (s == ZDT_RB1 || s == ZDT_LB1 || s == ZDT_RB2 || s == ZDT_LB2) continue;
        if (Z_(s).get_system_status()) {
            std::cout << "[realign] phase 2.5 slave " << s
                      << " status read fail (keep last_seal_pulse_="
                      << last_seal_pulse_[s - 1].load() << ")\n";
            continue;
        }
        const double real_pos_deg = Z_(s).status.real_pos;
        if (real_pos_deg < REAL_POS_MIN_DEG || real_pos_deg > REAL_POS_MAX_DEG) {
            std::cout << "[realign] phase 2.5 slave " << s
                      << " real_pos=" << real_pos_deg
                      << "° OUT OF RANGE — corrupt frame, keep last_seal_pulse_="
                      << last_seal_pulse_[s - 1].load() << "\n";
            evt_("realign_phase25_bad_pos slave=" + std::to_string(s));
            continue;
        }
        const int actual = (int)(real_pos_deg * 10.0);
        last_seal_pulse_[s - 1].store(actual);
        std::cout << "[realign] phase 2.5 slave " << s
                  << " real_pos=" << real_pos_deg << "° pulse=" << actual << "\n";
    }

    if (any_stalled) {
        sweep_stalls();
        motion_active_ = false;
        // SAFETY: Phase 2 stall = realign aborted with cups frozen at non-preset
        // positions, machine geometry possibly distorted. step_up/step_down
        // running next would release body vacuum on misaligned cups → machine
        // can fall (observed 2026-05-06). Force PausedOnError so caller's
        // "non-fatal log" cannot let next motion command auto-execute.
        // Operator must manually inspect and cmd_continue / emergency_stop.
        {
            std::lock_guard<std::mutex> lk(state_mtx_);
            // Same guard as await_user_intervention_ / crane_watchdog: never
            // overwrite state_before_pause_ if we're already in PausedOnError.
            if (state_.load() != State::PausedOnError)
                state_before_pause_ = state_.load();
        }
        set_state_(State::PausedOnError);
        evt_("realign_stall_paused slave=" + std::to_string(stalled_slave));
        std::cout << "[realign] PAUSED ON ERROR — slave " << stalled_slave
                  << " stalled, state→PausedOnError. Awaiting cmd_continue / emergency_stop.\n";
        return "ERR realign_motion_fail slave=" + std::to_string(stalled_slave) + "\n";
    }

    // in_window early-out: feet realign is done. Phase B (body rebuild),
    // Phase 3 and Phase 5 are SKIPPED — the enclosing step body cycle rebuilds
    // and vacuum-checks the body itself. Phase 4 (reset the feet over-extension
    // tracker) still runs here: the step's body extend right after reads
    // last_feet_max_over_cm_, and feet are now realigned to preset (over = 0).
    // motion_active_ is left untouched — the step owns it.
    if (in_window) {
        auto ffails = vacuum_check_("all");
        ffails.erase(std::remove_if(ffails.begin(), ffails.end(), realign_skip),
                     ffails.end());
        if (!ffails.empty()) {
            sweep_stalls();
            std::string msg = "realign_in_window_feet_unsealed=";
            for (size_t i = 0; i < ffails.size(); ++i) {
                if (i) msg += ",";
                msg += std::to_string(ffails[i]);
            }
            evt_(msg);
            std::cout << "[realign] " << msg << "\n";
            return "ERR " + msg + "\n";
        }
        last_feet_max_over_cm_.store(0.0);
        sweep_stalls();
        evt_("realign_done in_window");
        std::cout << "[realign] done (in_window — feet only)\n";
        return "";
    }

    // === Phase B (NEW 2026-05-06): re-extend whole body group (5,6,7,8) via disable_seal ===
    // Same mechanism as cycle_group_ body extend. Body cups currently at
    // PUSHER_RETRACT_PULSE (set by Phase A); disable_seal Phase 1 fast extends to
    // (per-slave preset − PHASE1_BUFFER), then Phase 2 iter loop pushes incremental
    // + holding + disable + vacuum wait until SEAL_DEEP. final_pulse recorded into
    // last_seal_pulse_ for each body slave (5,6,7,8).
    // Per-slave targets: 5,6 (long) = 28500, 7,8 (short) = 27900 — via preset_extend_pulse_for_slave_.
    {
        // First open body valve (was OFF since Phase A1)
        if (pqw_set_relay_verified_(CH_VALVE_BODY, true)) {
            sweep_stalls();
            motion_active_ = false;
            evt_("realign_phaseB_valve_on_fail");
            return "ERR realign_phaseB_valve_on_fail\n";
        }
        std::cout << "[realign] phase B: re-extend whole body group (5,6,7,8) via disable_seal\n";
        std::vector<int> phaseB_targets;
        phaseB_targets.reserve(body_all.size());
        for (int s : body_all) {
            phaseB_targets.push_back(preset_extend_pulse_for_slave_(s));
        }
        if (pusher_extend_with_disable_seal_(body_all, phaseB_targets,
                                              PUSHER_RPM_BODY_EXTEND, PUSHER_ACC_BODY_EXTEND)) {
            sweep_stalls();
            motion_active_ = false;
            evt_("realign_phaseB_disable_seal_fail");
            return "ERR realign_phaseB_disable_seal_fail\n";
        }
    }

    // Phase 3: verify cups still sealed after motion. Filter out skipped slaves
    // (ZDT_C / disabled) — we never moved them, so we don't care if vacuum_check_
    // flags them as unsealed (e.g., center valve may be off in current step flow).
    auto fails = vacuum_check_("all");
    fails.erase(std::remove_if(fails.begin(), fails.end(), realign_skip), fails.end());
    if (!fails.empty()) {
        sweep_stalls();
        motion_active_ = false;
        std::string msg = "realign_partial_fail unsealed=";
        for (size_t i = 0; i < fails.size(); ++i) {
            if (i) msg += ",";
            msg += std::to_string(fails[i]);
        }
        evt_(msg);
        std::cout << "[realign] " << msg << "\n";
        return "ERR " + msg + "\n";
    }

    // Phase 4: success — last_seal_pulse_ already refreshed in phase 2.5,
    // so no blanket reset to preset. Reset feet over_cm tracker.
    last_feet_max_over_cm_.store(0.0);

    // Phase 5: restore crane tension — pay out incrementally until rope tension
    // drops to the residual target (same mechanism as cmd_attach end via
    // crane_pay_out_to_weight_), capped at what Phase 1 actually retracted so
    // net rope ≤ 0. 2026-05-20 per user: replaces the previous blind
    // "pay back exact crane_assist_actual_cm" which overpaid in practice.
    if (crane_assist_actual_cm > 0) {
        std::cout << "[realign] restore pay_out_to_weight target="
                  << ATTACH_PAYOUT_TARGET_KG << "kg cap="
                  << crane_assist_actual_cm << "cm\n";
        std::string r = crane_pay_out_to_weight_(ATTACH_PAYOUT_TARGET_KG,
                                                  crane_assist_actual_cm);
        if (r.rfind("OK", 0) != 0) {
            // Non-fatal — alignment done, just rope tension off-baseline
            std::cout << "[realign] crane pay_out_to_weight restore failed: " << r << " (non-fatal)\n";
        }
    }

    sweep_stalls();
    motion_active_ = false;
    evt_("realign_done");
    std::cout << "[realign] done — all cups reset to preset\n";
    return "";
}

std::string WashRobot::cmd_step_up(int cm) {
    StepInProgressGuard _sip_guard{step_in_progress_};
    State cur = state_.load();
    // DISABLE STATUS CHECK
    // if (cur != State::Attached) return state_violation_(cur);
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > STEP_CM_MAX) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << STEP_CM_MAX << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[step_up] start → Running (step=" << step_cm_.load() << " cm)\n";
    set_state_(State::Running);

    std::string r = do_step_up_();
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

// step_up + 連續 cleaning sweep（並行）。
// 主 thread 跑 step_up（不跑末段 sweep），背景 thread 連續跑 LEFT+RIGHT 輪洗到
// step_up 結束。step 結束後等當前輪跑完才返回。
// 設計細節見 .claude/changelog.md 2026-05-22。
std::string WashRobot::cmd_step_up_with_sweep(int cm) {
    StepInProgressGuard _sip_guard{step_in_progress_};
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > STEP_CM_MAX) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << STEP_CM_MAX << ")\n";
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
        return do_arm_clean_sweep_continuous_(ARM_CLEAN_WALL_MM, sweep_keep_going);
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
        if (cm < STEP_CM_MIN || cm > STEP_CM_MAX) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << STEP_CM_MAX << ")\n";
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
            return do_arm_clean_sweep_continuous_(ARM_CLEAN_WALL_MM, sweep_keep_going, /*max_rounds=*/1);
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
        if (cm < STEP_CM_MIN || cm > STEP_CM_MAX) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << STEP_CM_MAX << ")\n";
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
            return do_arm_clean_sweep_continuous_(ARM_CLEAN_WALL_MM, sweep_keep_going, /*max_rounds=*/1);
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
        if (cm < STEP_CM_MIN || cm > STEP_CM_MAX) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << STEP_CM_MAX << ")\n";
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
            return do_arm_clean_sweep_continuous_(ARM_CLEAN_WALL_MM, sweep_keep_going, /*max_rounds=*/1);
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
        if (cm < STEP_CM_MIN || cm > STEP_CM_MAX) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << STEP_CM_MAX << ")\n";
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
            return do_arm_clean_sweep_continuous_(ARM_CLEAN_WALL_MM, sweep_keep_going, /*max_rounds=*/1);
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

std::string WashRobot::cmd_run(int steps, int cm, const std::string& direction) {
    if (steps <= 0) return "ERR invalid_steps\n";
    // 2026-05-26: 加 down_sweep_af / up_sweep_af —— 每 step 用
    // cmd_step_down_sweep_after_feet / cmd_step_up_sweep_after_feet（含 sweep）。
    // 2026-05-27: up / down 改成「整 run 期間單一 sweep thread 持續刷洗」（B 方案）
    // —— iter 之間 arm 不 PARK/INIT,真正「邊走邊刷」。對齊 cmd_step_*_with_sweep
    // 行為但 scope 拉到整 run。sweep_af 變體保持原邏輯（per-step,after feet rail）。
    const bool is_down       = (direction == "down");
    const bool is_up         = (direction == "up");
    const bool is_down_sweep = (direction == "down_sweep_af");
    const bool is_up_sweep   = (direction == "up_sweep_af");
    if (!is_down && !is_up && !is_down_sweep && !is_up_sweep)
        return "ERR direction_must_be_down|up|down_sweep_af|up_sweep_af\n";
    // DISABLE STATUS CHECK (per user 2026-05-22: match cmd_step_down/up which
    // also have this disabled; bench convenience — run from any state)
    //State cur = state_.load();
    //if (cur != State::Attached) return state_violation_(cur);
    // [2026-05-28] Clear arm sweep obstacle state at run start. If a previous
    // run had an obstacle and user chose Skip, that decision was for that run
    // only — fresh run starts with arm sweep enabled again.
    arm_sweep_obstacle_pending_.store(false);
    arm_sweep_skip_rest_of_run_.store(false);
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > STEP_CM_MAX) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << STEP_CM_MAX << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[run] " << steps << " steps " << direction
              << " × " << step_cm_.load() << " cm\n";
    set_state_(State::Running);

    // 2026-05-28 pipeline: up / down 行為改成跨 iter 重疊 sweep。
    //   iter 1: ba 模式（pre-feet sweep + 等到 feet rail → post-feet sweep）
    //   iter 2+: af 模式,但 after_feet_hook 內「先等前 iter sweep 完 → 再 launch
    //            本 iter sweep」,**iter step 不等 sweep 就 return** → iter N+1
    //            step pre-feet 跟 iter N sweep 並行。
    //   Sync 點：only after_feet_hook 內(launch 前先 join 前 sweep),確保不雙
    //           launch sweep / 不衝突 arm_cli_。
    //   step 本身不用 arm rail (DM2J:14),所以 pre-feet 並行 OK。
    //   sweep_af / sweep_af 變體保持原 cmd_step_*_sweep_after_feet 邏輯（每 iter
    //   self-contained,不 pipeline）。

    const bool is_pipeline = (is_up || is_down);

    std::atomic<bool>        sweep_keep_going{true};
    std::future<std::string> fut_sweep;

    // RAII：任何 return 路徑（error / abort / exception）都通知 sweep 停 + wait
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
            return do_arm_clean_sweep_continuous_(ARM_CLEAN_WALL_MM, sweep_keep_going, /*max_rounds=*/1);
        });
    };

    // iter 1 ba pattern:在 loop 開始前 launch pre-feet sweep,跟 iter 1 step
    // pre-feet 並行(vacuum break + pusher retract 期間 sweep 在 DEPLOY + 刷)。
    if (is_pipeline) {
        std::cout << "[run+pipe] iter 1 — launching pre-feet sweep (round 1)\n";
        fut_sweep = launch_round();
    }

    motion_active_ = true;
    for (int i = 1; i <= steps; ++i) {
        if (abort_flag.load()) {
            motion_active_ = false;
            set_state_(State::Error);
            return "ERR aborted\n";
        }

        std::ostringstream oss; oss << "step " << i << "/" << steps << " " << direction;
        evt_(oss.str());

        std::string r;
        if (is_down_sweep)      r = cmd_step_down_sweep_after_feet(0);   // cm=0 = use step_cm_
        else if (is_up_sweep)   r = cmd_step_up_sweep_after_feet(0);
        else {
            // up / down pipeline path
            std::function<void()> before_hook;
            std::function<void()> after_hook;

            if (i == 1) {
                // iter 1 ba: pre-round joined before feet rail; post-round launched after.
                before_hook = [this, &fut_sweep]() {
                    if (fut_sweep.valid()) {
                        std::cout << "[run+pipe] iter 1 joining pre-feet sweep before rail move\n";
                        std::string r1 = fut_sweep.get();
                        std::cout << "[run+pipe] iter 1 pre-feet sweep result: " << r1;
                        handle_post_sweep_obstacle_("run_pipe_iter1_pre");
                    }
                };
                after_hook = [&launch_round, &fut_sweep]() {
                    std::cout << "[run+pipe] iter 1 feet rail done → launching post-feet sweep (round 2)\n";
                    fut_sweep = launch_round();
                };
            } else {
                // iter 2+ pipelined af: after_feet_hook 內先 join 前 iter sweep,
                // 再 launch 本 iter sweep。**step 不等 sweep 就 return** → 下 iter
                // pre-feet 跟本 iter sweep 並行。
                after_hook = [this, &launch_round, &fut_sweep, i]() {
                    if (fut_sweep.valid()) {
                        std::cout << "[run+pipe] iter " << i
                                  << " feet rail done — waiting for previous sweep before relaunch\n";
                        std::string prev_r = fut_sweep.get();
                        std::cout << "[run+pipe] previous sweep result: " << prev_r;
                        handle_post_sweep_obstacle_("run_pipe_iter_join");
                    }
                    std::cout << "[run+pipe] iter " << i << " launching sweep\n";
                    fut_sweep = launch_round();
                };
                // before_hook stays empty
            }

            if (is_up) {
                r = do_step_up_(/*skip_cleaning_sweep=*/true, after_hook, before_hook);
            } else {
                r = do_step_down_(/*skip_cleaning_sweep=*/true, after_hook, before_hook);
            }
        }

        if (r.rfind("OK", 0) != 0) {
            motion_active_ = false;
            set_state_(State::Error);
            return r;
        }

        // [2026-05-22] Between-steps realign DISABLED (per user — see cmd_step_down).

        // If IMU flagged a balance issue, pause and wait for confirm_balance
        if (imu_ask_pending_.load()) pause_flag = true;

        if (check_abort_()) {
            motion_active_ = false;
            set_state_(State::Error);
            return "ERR aborted\n";
        }
    }

    // 全 run 結束,等最後一個 sweep 完成（pipeline 最後 iter 的 sweep 還在跑）
    if (is_pipeline && fut_sweep.valid()) {
        sweep_keep_going.store(false);
        std::cout << "[run+pipe] all steps done, waiting for final sweep to finish...\n";
        std::string final_r = fut_sweep.get();
        std::cout << "[run+pipe] final sweep result: " << final_r;
        // [2026-05-29] Final post-sweep obstacle check at end of run
        if (handle_post_sweep_obstacle_("run_pipe_final")) {
            motion_active_ = false;
            set_state_(State::Error);
            return "ERR aborted_arm_obstacle\n";
        }
    }

    motion_active_ = false;
    set_state_(State::Attached);
    // end-of-run PARK（do_arm_clean_sweep_continuous_ 最後 cleanup 已 PARK,
    // 這邊 idempotent safety 收尾,確保 arm_stow_state_ 同步成 Parked）
    ensure_arm_parked_after_rope_("run_end");
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

        // Now peel optional 'n' (no-sweep) flag from the right of `head`.
        bool sweep = true;
        if (!head.empty() && (head.back() == 'n' || head.back() == 'N')) {
            sweep = false;
            head.pop_back();
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

        if (cm < STEP_CM_MIN || cm > STEP_CM_MAX) {
            err = "step_cm_out_of_range pos=" + std::to_string(token_idx)
                + " cm=" + std::to_string(cm)
                + " (allowed " + std::to_string(STEP_CM_MIN) + ".."
                + std::to_string(STEP_CM_MAX) + ")";
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
            out.push_back({cm, sweep});
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

std::string WashRobot::cmd_run_script(const std::string& csv) {
    std::vector<ScriptStep> steps;
    std::string perr;
    if (!parse_script_csv_(csv, steps, perr)) return "ERR " + perr + "\n";
    if (steps.empty())                        return "ERR empty_script\n";

    // Mirror cmd_run init: clear arm-sweep obstacle latches so a previous
    // run's "Skip rest" decision doesn't carry over into this script.
    arm_sweep_obstacle_pending_.store(false);
    arm_sweep_skip_rest_of_run_.store(false);

    int total_cm = 0, n_sweep = 0, n_transit = 0;
    for (const auto& s : steps) {
        total_cm += s.cm;
        if (s.sweep) ++n_sweep; else ++n_transit;
    }
    const int N = (int)steps.size();

    std::cout << "[run_script] " << N << " steps (" << n_sweep << " sweep + "
              << n_transit << " transit), total " << total_cm << " cm\n";
    {
        std::ostringstream oss;
        oss << "script_start total_steps=" << N
            << " total_cm=" << total_cm
            << " sweep=" << n_sweep
            << " transit=" << n_transit;
        evt_(oss.str());
    }
    set_state_(State::Running);

    // ========================================================
    // Pipeline mode — mirrors cmd_run's is_pipeline path so sweep
    // and step overlap exactly like `run` does:
    //
    //   * Pre-loop sweep round launched (ba pattern for iter 1) — only
    //     if step[0] is a sweep step. Joined at iter 1's
    //     before_feet_rail_hook so feet rail doesn't fight arm sweep.
    //   * iter 1 after_feet_rail_hook launches the next sweep round
    //     (if step[0].sweep) — runs through iter 1 feet re-extend +
    //     iter 2 body phase + Phase B release/retract + feet rail.
    //   * iter k (k>=2) after_feet_rail_hook joins the previous sweep
    //     (whoever launched it), then launches a fresh round if
    //     step[k-1].sweep. Transit step => no launch, but still joins.
    //
    // Per-step dispatch:
    //   sweep step   → do_step_down_(skip_cleaning_sweep=true, hooks)
    //                  with hooks managing the sweep round
    //   transit step → do_step_down_(skip_cleaning_sweep=true, hooks)
    //                  same call, but launch part of hook skipped
    //
    // NOTE: a sweep launched by iter k still runs during iter k+1's
    // body phase even if iter k+1 is transit. Joining happens at
    // iter k+1's after_feet_rail_hook — same timing as cmd_run. This
    // means a transit step CAN have residual arm motion in the
    // background (the tail of the previous sweep). Accepted trade-off:
    // matches cmd_run's overlap semantics; physically safe because arm
    // is mounted on the robot body and moves with it during descent.
    // ========================================================

    std::atomic<bool>        sweep_keep_going{true};
    std::future<std::string> fut_sweep;

    // RAII guard — any return path (error / abort / exception) signals
    // sweep to stop + waits for the future. Identical to cmd_run's.
    struct SweepJoin {
        std::atomic<bool>&        flag;
        std::future<std::string>& f;
        ~SweepJoin() {
            flag.store(false);
            if (f.valid()) f.wait();
        }
    } _sweep_guard{sweep_keep_going, fut_sweep};

    auto launch_round = [this, &sweep_keep_going]() {
        sweep_keep_going.store(true);
        return std::async(std::launch::async, [this, &sweep_keep_going]() -> std::string {
            return do_arm_clean_sweep_continuous_(ARM_CLEAN_WALL_MM, sweep_keep_going,
                                                  /*max_rounds=*/1);
        });
    };

    // Pre-loop sweep — only if iter 1 is a sweep step. Gives iter 1 the
    // same Phase A overlap that iter 2+ get from prev iter's after_hook.
    if (steps[0].sweep) {
        std::cout << "[run_script+pipe] iter 1 — launching pre-feet sweep (round 1)\n";
        fut_sweep = launch_round();
    } else {
        std::cout << "[run_script+pipe] iter 1 is transit — skipping pre-feet sweep\n";
    }

    motion_active_ = true;
    for (int i = 1; i <= N; ++i) {
        if (abort_flag.load()) {
            motion_active_ = false;
            set_state_(State::Error);
            return "ERR aborted\n";
        }

        const int  cm_i    = steps[i - 1].cm;
        const bool sweep_i = steps[i - 1].sweep;
        const char* mode_s = sweep_i ? "sweep" : "transit";
        {
            std::ostringstream oss;
            oss << "script step " << i << "/" << N
                << " cm=" << cm_i
                << " mode=" << mode_s;
            evt_(oss.str());
        }
        step_cm_.store(cm_i);

        StepInProgressGuard _sip_guard{step_in_progress_};

        std::function<void()> before_hook;
        std::function<void()> after_hook;

        if (i == 1) {
            // iter 1 ba pattern — only sets a before_hook to join the
            // pre-loop sweep (if one was launched).
            before_hook = [this, &fut_sweep]() {
                if (fut_sweep.valid()) {
                    std::cout << "[run_script+pipe] iter 1 joining pre-feet sweep"
                                 " before rail move\n";
                    std::string r1 = fut_sweep.get();
                    std::cout << "[run_script+pipe] iter 1 pre-feet sweep result: " << r1;
                    handle_post_sweep_obstacle_("run_script_pipe_iter1_pre");
                }
            };
            // iter 1 after_hook launches the next sweep IF this iter is sweep.
            after_hook = [this, &launch_round, &fut_sweep, sweep_i]() {
                if (sweep_i) {
                    std::cout << "[run_script+pipe] iter 1 feet rail done → launching"
                                 " post-feet sweep (round 2)\n";
                    fut_sweep = launch_round();
                } else {
                    std::cout << "[run_script+pipe] iter 1 transit — not launching"
                                 " post-feet sweep\n";
                }
            };
        } else {
            // iter 2+ af pattern — after_hook joins prev sweep (always, defensive),
            // then launches a fresh round IF current step is sweep.
            after_hook = [this, &launch_round, &fut_sweep, i, sweep_i]() {
                if (fut_sweep.valid()) {
                    std::cout << "[run_script+pipe] iter " << i
                              << " feet rail done — waiting for previous sweep"
                                 " before relaunch\n";
                    std::string prev_r = fut_sweep.get();
                    std::cout << "[run_script+pipe] previous sweep result: " << prev_r;
                    handle_post_sweep_obstacle_("run_script_pipe_iter_join");
                }
                if (sweep_i) {
                    std::cout << "[run_script+pipe] iter " << i << " launching sweep\n";
                    fut_sweep = launch_round();
                } else {
                    std::cout << "[run_script+pipe] iter " << i
                              << " transit — not launching sweep\n";
                }
            };
        }

        // Pure step (Phase C suppressed via skip_cleaning_sweep=true) — sweep is
        // entirely managed by the hooks above.
        std::string r = do_step_down_(/*skip_cleaning_sweep=*/true,
                                      after_hook, before_hook);

        if (r.rfind("OK", 0) != 0) {
            {
                std::ostringstream oss;
                oss << "script_complete status=fail step=" << i << "/" << N
                    << " mode=" << mode_s
                    << " reason=" << r;
                evt_(oss.str());
            }
            motion_active_ = false;
            set_state_(State::Error);
            return r;
        }

        // [2026-05-22] Between-steps realign DISABLED (per user — see cmd_step_down).

        // If IMU flagged a balance issue, pause and wait for confirm_balance
        if (imu_ask_pending_.load()) pause_flag = true;

        if (check_abort_()) {
            motion_active_ = false;
            set_state_(State::Error);
            return "ERR aborted\n";
        }
    }

    // After all iters, wait for the final outstanding sweep (the one launched
    // at the last sweep iter's after_feet_rail_hook). Mirrors cmd_run's
    // pipeline end-of-run cleanup.
    if (fut_sweep.valid()) {
        sweep_keep_going.store(false);
        std::cout << "[run_script+pipe] all steps done, waiting for final sweep...\n";
        std::string final_r = fut_sweep.get();
        std::cout << "[run_script+pipe] final sweep result: " << final_r;
        if (handle_post_sweep_obstacle_("run_script_pipe_final")) {
            motion_active_ = false;
            set_state_(State::Error);
            return "ERR aborted_arm_obstacle\n";
        }
    }

    motion_active_ = false;
    set_state_(State::Attached);
    // Idempotent PARK — do_arm_clean_sweep_continuous_ already PARKs at end,
    // this is defensive sync with arm_stow_state_.
    ensure_arm_parked_after_rope_("run_script_end");
    {
        std::ostringstream oss;
        oss << "script_complete status=ok total=" << N;
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

std::string WashRobot::cmd_run_saved(const std::string& name) {
    std::string csv;
    {
        std::lock_guard<std::mutex> lk(saved_scripts_mtx_);
        auto it = saved_scripts_.find(name);
        if (it == saved_scripts_.end()) return "ERR not_found name=" + name + "\n";
        csv = it->second;   // copy under lock so cmd_run_script can run unlocked
    }
    return cmd_run_script(csv);
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

std::string WashRobot::cmd_tilt_mode(bool on) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    std::lock_guard<std::mutex> lk(motion_mtx_);
    if (on) {
        pqw_.controlRelay(CH_VALVE_FEET,   false);
        pqw_.controlRelay(CH_VALVE_BODY,   false);
        pqw_.controlRelay(CH_VALVE_CENTER, true);
    } else {
        pqw_.controlRelay(CH_VALVE_BODY, true);
        sleep_ms_(VACUUM_SETTLE_MS);
        pqw_.controlRelay(CH_VALVE_FEET, true);
    }
    return on ? "OK tilt_on\n" : "OK tilt_off\n";
}

std::string WashRobot::cmd_emergency_stop() {
    abort_flag    = true;
    pause_flag    = false;
    motion_active_ = false;
    for (int s = 1; s <= 9; ++s) Z_(s).emergency_stop(false);
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
    for (int s = 1; s <= 9; ++s) Z_(s).emergency_stop(false);
    pqw_.controlRelay(CH_BRUSH,        false);
    pqw_.controlRelay(CH_WATER_PUMP,   false);
    set_water_inlet_(false);   // [2026-06-05] → crane PQW (.34 slave 12 CH4)
    pqw_.controlRelay(CH_VALVE_FEET,   false);
    pqw_.controlRelay(CH_VALVE_BODY,   false);
    pqw_.controlRelay(CH_VALVE_CENTER, false);
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
    if (M_(slave).error_flag == 0)
        cached_pressure_[slave - 1].store(p);
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
            for (int s = 1; s <= 9; ++s) {
                if (disabled_zdt_slaves_.count(s)) continue;
                if (!first) sleep_ms_(30);
                first = false;
                int p = M_(s).read_pressure();
                if (M_(s).error_flag == 0)
                    cached_pressure_[s - 1].store(p);
            }
        }
    }
    std::ostringstream oss;
    oss << "OK state=" << state_name(state_.load());
    oss << " crane_attached=" << (crane_attached_.load() ? "on" : "off");
    oss << " arm_attached="   << (arm_attached_.load()   ? "on" : "off");
    oss << " obstacle_detect=" << (obstacle_detect_enabled_.load() ? "on" : "off");
    oss << std::fixed << std::setprecision(1)
        << " rail=" << rail_pos_cm_.load()
        << " body_residual=" << body_residual_cm_.load();
    for (int s = 1; s <= 9; ++s)
        oss << " p" << s << "=" << cached_pressure_[s - 1].load();
    if (!imu_.read_error.load()) {
        oss << std::setprecision(2)
            << " roll=" << (imu_.x - imu_roll0_)
            << " pitch=" << (imu_.y - imu_pitch0_);
    }
    oss << "\n";
    return oss.str();
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

std::string WashRobot::cmd_water_pump(bool on) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    if (pqw_.controlRelay(CH_WATER_PUMP, on)) return "ERR water_pump_fail\n";
    return "OK\n";
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
        if (group == "all") {
            if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_FEET,   false); }, valve_ctx + "_feet"))   return on_abort();
            if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_BODY,   false); }, valve_ctx + "_body"))   return on_abort();
            if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_CENTER, false); }, valve_ctx + "_center")) return on_abort();
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

        // Two-stage retract: half → wait → full.
        if (group == "all") {
            std::vector<int> feet_g   = {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2};
            std::vector<int> body_g   = {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2};
            // 2026-05-18: center_g (ZDT_C slave 9) commented out — center
            // pusher not controlled in current bench config.
            // std::vector<int> center_g = {ZDT_C};

            if (try_or_pause_([this, &feet_g]() { return pusher_two_stage_retract_(feet_g); }, "manual_pusher_all_feet_retract")) return on_abort();
            if (try_or_pause_([this, &body_g]() { return pusher_two_stage_retract_(body_g); }, "manual_pusher_all_body_retract")) return on_abort();
            // [2026-05-18 disabled] center pusher retract:
            // if (try_or_pause_([this, &center_g]() { return pusher_two_stage_retract_(center_g); }, "manual_pusher_all_center_retract")) return on_abort();
            return "OK\n";
        }

        auto slaves = group_slaves_(group);
        if (slaves.empty()) return "ERR unknown_group\n";
        const std::string ctx = "manual_pusher_" + group + "_retract";
        // Safety: before retracting feet/body, ensure the OTHER load-bearing
        // group has no latched stall (firmware would reject its next motion cmd).
        // Center is independent — skip the cross-check.
        if (group == "feet" || group == "body") {
            const std::string other = (group == "feet") ? "body" : "feet";
            if (try_or_pause_([this, other]() { return ensure_group_stall_clear_(other); },
                              ctx + "_check_other_stall")) return on_abort();
        }
        if (try_or_pause_([this, &slaves]() { return pusher_two_stage_retract_(slaves); }, ctx)) return on_abort();
        return "OK\n";
    }
    if (pos == "extend") {
        // Manual extend mirrors auto-cycle extend logic: per-slave start pulses
        // from last_seal_pulse_ + B compensation, vacuum early-stop, fine_tune
        // with obstacle detection. Caller (user) must ensure valve state is set.
        if (group == "all") {
            auto feet_g   = group_slaves_("feet");
            auto body_g   = group_slaves_("body");
            auto center_g = group_slaves_("center");
            if (try_or_pause_([this, &feet_g]()   { return smart_extend_subset_("feet",   feet_g); },   "manual_pusher_all_feet_extend"))   return on_abort();
            if (try_or_pause_([this, &body_g]()   { return smart_extend_subset_("body",   body_g); },   "manual_pusher_all_body_extend"))   return on_abort();
            if (try_or_pause_([this, &center_g]() { return smart_extend_subset_("center", center_g); }, "manual_pusher_all_center_extend")) return on_abort();
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
    if (slave < 1 || slave > 9) return "ERR invalid_slave\n";
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
        const std::string slave_group = (slave >= 1 && slave <= 4) ? "feet"
                                       : (slave == ZDT_C)          ? "center"
                                       :                             "body";
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
    if (slave < 1 || slave > 9) return "ERR invalid_slave\n";
    disabled_zdt_slaves_.insert(slave);
    std::cout << "[zdt_disable] slave " << slave << " excluded from group ops\n";
    return "OK\n";
}

std::string WashRobot::cmd_zdt_enable(int slave) {
    if (slave < 1 || slave > 9) return "ERR invalid_slave\n";
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
    for (int s = 1; s <= 9; ++s) {
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

std::string WashRobot::cmd_move(const std::string& motor, double cm) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    int slave;
    if      (motor == "left_foot")  slave = DM2J_LEFT_FOOT;
    else if (motor == "right_foot") slave = DM2J_RIGHT_FOOT;
    else if (motor == "arm")        slave = DM2J_ARM;
    else return "ERR unknown_motor\n";
    // dm2j_motion_mtx_：序列化 cli_20_ bus (feet/wheels)。arm 在 cli_22_ slave 14,
    // 不需要 cli_20_ lock — TCP_client::socket_mtx_ 自己 serialize cli_22_。
    std::unique_lock<std::mutex> dm2j_lk(dm2j_motion_mtx_, std::defer_lock);
    if (motor != "arm") dm2j_lk.lock();
    if (D_(slave).PR_move_cm(0, 1, DM2J_RPM, cm, DM2J_ACC, DM2J_DEC))
        return "ERR move_fail\n";
    return "OK\n";
}

// Move both wheels (slave 2, 4) to an absolute target in parallel.
// "retract" = abs 0 cm, "lower" = abs -6 cm. Mirrors init()'s startup wheel-lower:
// PR_move_cm_nowait on both triggers within one Modbus frame, then sequential wait.
// [2026-06-12] Helper：兩輪同時 move 到 target，加 position verify + retry。
// 避免「只有一邊輪子動」的症狀（DM2J 報 done 但實際馬達 stall / 沒到 target）。
// 流程：trigger 兩邊 (back-to-back loose sync) → wait_done → read_position 驗證
//      → 任一輪 fail 就 retry 那一邊 (最多 MAX_RETRIES 次)。
// 回傳：true = fail (跟其他 dm2j_* helper 一致：true=error), false = success。
bool WashRobot::dm2j_wheels_move_verified_(double target_cm) {
    constexpr double TOL_CM = 0.5;
    constexpr int MAX_RETRIES = 3;
    bool left_ok = false, right_ok = false;

    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        // 1. Trigger（只觸發尚未 ok 的輪子、back-to-back 保持 loose sync）
        bool left_trig = false, right_trig = false;
        if (!left_ok) {
            left_trig = !D_(DM2J_LEFT_WHEEL).PR_move_cm_nowait(
                0, 1, DM2J_RPM, target_cm, DM2J_ACC, DM2J_DEC);
            if (!left_trig)
                std::cerr << "[wheels] LEFT trigger fail attempt " << (attempt+1) << "\n";
        }
        if (!right_ok) {
            right_trig = !D_(DM2J_RIGHT_WHEEL).PR_move_cm_nowait(
                0, 1, DM2J_RPM, target_cm, DM2J_ACC, DM2J_DEC);
            if (!right_trig)
                std::cerr << "[wheels] RIGHT trigger fail attempt " << (attempt+1) << "\n";
        }

        // 2. Wait（馬達已平行移動中、wait 是 sequential 但不影響並行性）
        bool left_done = false, right_done = false;
        if (left_trig)  left_done  = !dm2j_wait_done_(DM2J_LEFT_WHEEL);
        if (right_trig) right_done = !dm2j_wait_done_(DM2J_RIGHT_WHEEL);

        // 3. Verify position
        if (left_done) {
            double actual = 0.0;
            if (D_(DM2J_LEFT_WHEEL).read_position_cm(actual)) {
                std::cerr << "[wheels] LEFT pos read fail attempt "
                          << (attempt+1) << " — accept best-effort\n";
                left_ok = true;
            } else if (std::abs(actual - target_cm) <= TOL_CM) {
                std::cout << "[wheels] LEFT ok at " << actual << " cm\n";
                left_ok = true;
            } else {
                std::cerr << "[wheels] LEFT pos mismatch attempt " << (attempt+1)
                          << ": got " << actual << " want " << target_cm << "\n";
            }
        }
        if (right_done) {
            double actual = 0.0;
            if (D_(DM2J_RIGHT_WHEEL).read_position_cm(actual)) {
                std::cerr << "[wheels] RIGHT pos read fail attempt "
                          << (attempt+1) << " — accept best-effort\n";
                right_ok = true;
            } else if (std::abs(actual - target_cm) <= TOL_CM) {
                std::cout << "[wheels] RIGHT ok at " << actual << " cm\n";
                right_ok = true;
            } else {
                std::cerr << "[wheels] RIGHT pos mismatch attempt " << (attempt+1)
                          << ": got " << actual << " want " << target_cm << "\n";
            }
        }

        if (left_ok && right_ok) {
            if (attempt > 0)
                std::cout << "[wheels] both ok after " << (attempt+1) << " attempts\n";
            return false;   // success
        }
        if (attempt < MAX_RETRIES - 1) sleep_ms_(500);
    }

    std::cerr << "[wheels] FAILED after " << MAX_RETRIES << " retries:";
    if (!left_ok)  std::cerr << " LEFT";
    if (!right_ok) std::cerr << " RIGHT";
    std::cerr << "\n";
    return true;   // error
}

std::string WashRobot::cmd_wheels(const std::string& action) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    if (!wheels_attached_.load()) return "OK skipped (wheels_attached=off)\n";

    double target_cm;
    if      (action == "retract") target_cm = 0.0;
    else if (action == "lower")   target_cm = -6.0;
    else return "ERR expected_retract_or_lower\n";

    // dm2j_motion_mtx_：跟背景 arm sweep 序列化 cli_20_ bus
    std::lock_guard<std::mutex> dm2j_lk(dm2j_motion_mtx_);

    if (dm2j_wheels_move_verified_(target_cm))
        return "ERR wheels_move_failed (see log for which side)\n";
    return "OK\n";
}

// Group sync absolute move (mirrors Linux_test menu 16):
//   "feet"   = slaves 1, 3 — broadcast PR1 sync (rigid-coupled, true simultaneous start)
//   "wheels" = slaves 2, 4 — parallel PR_move_cm_nowait + wait (non-rigid, loose simultaneous)
std::string WashRobot::cmd_dm2j_group(const std::string& group, double cm) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);

    if (group == "feet") {
        if (dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, cm))
            return "ERR feet_move_fail\n";
        return "OK\n";
    }
    if (group == "wheels") {
        if (!wheels_attached_.load()) return "OK skipped (wheels_attached=off)\n";
        // dm2j_motion_mtx_：跟背景 arm sweep 序列化 cli_20_ bus
        std::lock_guard<std::mutex> dm2j_lk(dm2j_motion_mtx_);
        // [2026-06-12] 用 verify+retry helper、避免「只有一邊動」
        if (dm2j_wheels_move_verified_(cm))
            return "ERR wheels_move_failed (see log for which side)\n";
        return "OK\n";
    }
    return "ERR unknown_group (expected feet|wheels)\n";
}

// Set current shaft position as new zero for an entire group.
// Mirrors Linux_test menu 15 but operates on grouped slaves at once.
//   "feet"   → slaves 1, 3
//   "wheels" → slaves 2, 4
//   "arm"    → slave 5
// Writes 0x6002 = 0x0021 (home_set_current_pos_zero) to each slave, then reads
// back to verify. Logs before/after positions per slave.
std::string WashRobot::cmd_dm2j_zero(const std::string& group) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);

    std::vector<int> slaves;
    if      (group == "feet")   slaves = {DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT};
    else if (group == "wheels") slaves = {DM2J_LEFT_WHEEL, DM2J_RIGHT_WHEEL};
    else if (group == "arm")    slaves = {DM2J_ARM};
    else return "ERR unknown_group (expected feet|wheels|arm)\n";

    // dm2j_motion_mtx_：序列化 cli_20_ bus (feet/wheels)。arm 在 cli_22_ slave 14,
    // 不需要 cli_20_ lock — TCP_client 自己 serialize cli_22_。
    std::unique_lock<std::mutex> dm2j_lk(dm2j_motion_mtx_, std::defer_lock);
    if (group != "arm") dm2j_lk.lock();

    std::ostringstream reply;
    reply << "OK zeroed " << group << ":";

    for (int s : slaves) {
        double before = 0;
        if (D_(s).read_position_cm(before)) {
            std::cout << "  [zero " << group << "] slave " << s << " read-before failed\n";
        } else {
            std::cout << "  [zero " << group << "] slave " << s << " before=" << before << " cm\n";
        }

        D_(s).home_set_current_pos_zero();
        sleep_ms_(200);

        double after = 0;
        if (D_(s).read_position_cm(after)) {
            std::cout << "  [zero " << group << "] slave " << s << " read-after failed\n";
            reply << " " << s << "=?";
        } else {
            std::cout << "  [zero " << group << "] slave " << s << " after=" << after << " cm\n";
            reply << " " << s << "=" << after;
        }
    }
    reply << "\n";

    // Reset rail_pos_cm_ tracking when feet zeroed (rail axis just shifted).
    if (group == "feet") rail_pos_cm_.store(0.0);

    return reply.str();
}

std::string WashRobot::cmd_confirm_balance(const std::string& ans) {
    State cur = state_.load();
    if (cur != State::WaitingConfirm) return state_violation_(cur);

    State prev;
    { std::lock_guard<std::mutex> lk(state_mtx_); prev = state_before_wait_; }

    if (ans == "yes") {
        set_state_(State::Balancing);
        std::string err = do_phase5_roll_correct_();
        imu_ask_pending_ = false;
        pause_flag       = false;
        if (!err.empty()) {
            set_state_(State::Error);
            return "ERR " + err + "\n";
        }
        set_state_(prev);   // restore pre-ask state (Running or Attached)
        return "OK phase5_done\n";
    }
    if (ans == "no") {
        imu_ask_pending_ = false;
        pause_flag       = false;
        set_state_(prev);
        return "OK balance_skipped\n";
    }
    return "ERR expected_yes_or_no\n";
}

// Phase 6 — Return home (release cups, retract pushers, crane pays out to ground).
// descent_cm: how far crane should pay out. Caller computes
// (home_ground_cm - current_down_cm) since washrobot does not track cable length.
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

    // 1. Arm rail back to zero
    if (D_(DM2J_ARM).PR_move_cm(0, 1, DM2J_RPM, 0.0, DM2J_ACC, DM2J_DEC))
        return fail("ERR arm_home_fail\n");
    if (check_abort_()) return fail("ERR aborted\n");

    // 2. Water system off (brush / pump / inlet valve)
    pqw_.controlRelay(CH_BRUSH,       false);
    pqw_.controlRelay(CH_WATER_PUMP,  false);
    set_water_inlet_(false);   // [2026-06-05] → crane PQW (.34 slave 12 CH4)

    // 3. Break suction on all three groups
    pqw_.controlRelay(CH_VALVE_FEET,   false);
    pqw_.controlRelay(CH_VALVE_BODY,   false);
    pqw_.controlRelay(CH_VALVE_CENTER, false);

    // 4. Wait for all 9 cups to release. Poll-based — proceeds the moment
    //    pressures rise above DETACH_THRESHOLD_KPA, up to RETURN_VACUUM_RELEASE_MS.
    //    Wrapped in try_or_pause_: timeout drops into PausedOnError so operator
    //    can investigate (continue=re-poll / skip=force retract / stop=Error).
    //    Replaces previous fixed sleep + one-shot manual check.
    {
        // 2026-05-18: ZDT_C (center, slave 9) commented out — not controlled.
        std::vector<int> all9 = {ZDT_LF1, ZDT_LF2, ZDT_LB1, ZDT_LB2,
                                 ZDT_RF1, ZDT_RF2, ZDT_RB1, ZDT_RB2 /*, ZDT_C*/};
        if (try_or_pause_([this, &all9]() { return vacuum_wait_release_(all9, RETURN_VACUUM_RELEASE_MS); },
                          "return_home_vacuum_release"))
            return fail("ERR aborted\n");
    }
    if (check_abort_()) return fail("ERR aborted\n");

    // 6. Retract feet + body pushers — TWO-STAGE (half → wait → full), per group.
    // Single-stage fast retract risks ZDT stall when cup adhesion lingers after
    // valve OFF. feet/body groups have different extend pulses → split per group
    // so each stage-1 target = that group's (extend × 2/3). Matches the two-stage
    // pattern used everywhere else (do_step_*, cmd_pusher, cycle_group_).
    // 2026-05-18: ZDT_C (center, slave 9) commented out — not controlled.
    {
        std::vector<int> feet_g = {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2};
        std::vector<int> body_g = {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2};
        if (try_or_pause_([this, &feet_g]() { return pusher_two_stage_retract_(feet_g); },
                          "return_home_feet_retract"))
            return fail("ERR aborted\n");
        if (try_or_pause_([this, &body_g]() { return pusher_two_stage_retract_(body_g); },
                          "return_home_body_retract"))
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
    std::cout << "[recover] verify vacuum on all 9 cups\n";
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
    std::cout << "[recover] all 9 sealed → Attached\n";
    abort_flag       = false;
    pause_flag       = false;
    motion_active_   = false;
    imu_ask_pending_ = false;
    set_state_(State::Attached);
    return "OK recovered\n";
}

// Manual realign trigger — also checks threshold (REALIGN_THRESHOLD_CM=1.5cm)
// like the auto trigger, so user pressing the button when drift is small results
// in a clear "skipped" message rather than running unnecessary motion.
// Allowed only when state ∈ {Attached, Paused, PausedOnError} (cups on wall).
std::string WashRobot::cmd_realign() {
    State cur = state_.load();
    //if (cur != State::Attached && cur != State::Paused && cur != State::PausedOnError)
    //    return state_violation_(cur);

    // Compute max_over_cm for user-facing message (whether skipped or executed)
    double max_over_cm = 0.0;
    for (int s = 1; s <= 9; ++s) {
        const int preset = preset_extend_pulse_for_slave_(s);
        const int last   = last_seal_pulse_[s - 1].load();
        const double over_pulses = (double)(last - preset);
        const double over_cm = (s >= 1 && s <= 4)
                             ? over_pulses / (20000.0 / 7.0)
                             : over_pulses / (30000.0 / 10.0);
        if (over_cm > max_over_cm) max_over_cm = over_cm;
    }

    // [DISABLED 2026-05-05] threshold check 同 do_feet_realign_，realign 一律跑
    //if (max_over_cm < REALIGN_THRESHOLD_CM) {
    //    std::ostringstream oss;
    //    oss << "OK realign_skipped max_over=" << max_over_cm
    //        << "cm < threshold=" << REALIGN_THRESHOLD_CM << "cm\n";
    //    std::cout << "[realign] manual trigger SKIPPED: " << oss.str();
    //    return oss.str();
    //}

    std::cout << "[realign] manual trigger, max_over=" << max_over_cm << " cm (threshold disabled)\n";
    std::string err = do_feet_realign_(/*force=*/false);
    if (!err.empty()) {
        std::cout << "[realign] manual trigger FAIL: " << err;
        return err;
    }
    // [arm rope protect TEMP 2026-05-21] manual realign 結束後 PARK arm（user 報 5/21y）
    // realign Phase 5 pay_out_to_weight 把 arm 帶到 CENTER，realign 結束應該把它收回。
    // Net rope ≈ 0（Phase 1 retract + Phase 5 pay_out 抵消），PARK 路徑安全。
    ensure_arm_parked_after_rope_("cmd_realign_done");
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
std::string WashRobot::cmd_wheels_attached(bool on) {
    bool prev = wheels_attached_.exchange(on);
    if (prev != on) {
        std::cout << "[wheels] attached " << (on ? "ON — wheel ops active"
                                                : "OFF — wheel ops skipped (bench mode)") << "\n";
        evt_(std::string("wheels_attached ") + (on ? "on" : "off"));
    }
    return on ? "OK wheels_attached=on\n" : "OK wheels_attached=off\n";
}
