#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "WASH_ROBOT.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <cstdlib>   // std::getenv (driver debug env var)

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
{}

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

    // DM2J slave 1..5
    for (int i = 1; i <= 5; ++i) {
        if (D_(i).init(cli_20_, i, dbg)) {
            std::cerr << "[FATAL] DM2J slave " << i << " init fail\n"; return true;
        }
    }
    std::cout << "[OK] DM2J 1~5\n";

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

    // Crane (lazy — don't fail boot if crane is down)
    if (crane_connect_if_needed_())
        std::cerr << "[WARN] crane " << CRANE_IP << ":" << CRANE_PORT << " not yet reachable\n";
    else
        std::cout << "[OK] crane " << CRANE_IP << ":" << CRANE_PORT << "\n";

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

    // Safe startup: ensure all relays off
    pqw_.controlRelay(CH_BRUSH,        false);
    pqw_.controlRelay(CH_WATER_PUMP,   false);
    pqw_.controlRelay(CH_WATER_INLET,  false);
    pqw_.controlRelay(CH_PUMP,         false);
    pqw_.controlRelay(CH_VALVE_FEET,   false);
    pqw_.controlRelay(CH_VALVE_BODY,   false);
    pqw_.controlRelay(CH_VALVE_CENTER, false);

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

// Synchronized pair move to same absolute target (cm).
// Broadcast trigger ensures same-moment start. Parallel poll ensures both
// finish before we return. Logs before/after positions + travel for diagnostic.
bool WashRobot::dm2j_pair_move_abs_(int slave_a, int slave_b, int pr_num,
                                      double target_cm, int timeout_ms) {
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

    // Queue targets on both slaves (same PR slot, same absolute target)
    D_(slave_a).PR_move_cm_set(pr_num, 1, DM2J_RPM, target_cm, DM2J_ACC, DM2J_DEC);
    D_(slave_b).PR_move_cm_set(pr_num, 1, DM2J_RPM, target_cm, DM2J_ACC, DM2J_DEC);

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
    if (crane_connect_if_needed_()) return "";

    std::string tx = line;
    if (tx.empty() || tx.back() != '\n') tx.push_back('\n');
    if (!crane_cli_.sendData(tx.c_str(), (int)tx.size(), 1000)) return "";

    std::string rx;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    char buf[512];
    while (std::chrono::steady_clock::now() < deadline) {
        int n = crane_cli_.receiveData(buf, sizeof(buf), 500);
        if (n > 0) {
            rx.append(buf, n);
            size_t pos = rx.find('\n');
            if (pos != std::string::npos) {
                std::string reply = rx.substr(0, pos);
                if (!reply.empty() && reply.back() == '\r') reply.pop_back();
                if (reply.rfind("OK", 0) == 0) crane_last_ok_ms_ = now_ms_();
                return reply;
            }
        } else {
            sleep_ms_(POLL_INTERVAL_MS);
        }
    }
    return "";
}

void WashRobot::crane_watchdog_loop_() {
    crane_last_ok_ms_ = now_ms_();
    while (crane_wd_running_.load()) {
        sleep_ms_(HEARTBEAT_INTERVAL_MS);
        if (!crane_wd_running_.load()) break;

        // Detached mode: skip ping + abort entirely. Watchdog effectively pauses.
        // (Resumed when cmd_crane_attached(true) — handler resets crane_last_ok_ms_.)
        if (!crane_attached_.load()) continue;

        crane_cmd_("ping", 2);

        int64_t elapsed = now_ms_() - crane_last_ok_ms_.load();
        if (elapsed > WATCHDOG_TIMEOUT_MS) {
            if (motion_active_.load()) {
                // Re-enabled 2026-04-28 — crane disconnect during motion is a
                // hard abort condition (machine half-suspended, no safety net).
                abort_flag = true;
                evt_("crane_watchdog timeout — abort_flag set");
            } else {
                evt_("crane_watchdog timeout idle");
            }
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
    if (M_(ZDT_C).read_pressure() > VACUUM_THRESHOLD_KPA)
        return "phase5_center_vacuum_fail";

    // Steps 2-6: iterative roll correction
    std::string err;
    for (int attempt = 1; attempt <= ROLL_CORRECT_RETRY_MAX; ++attempt) {
        double roll = imu_.x - imu_roll0_;
        if (std::abs(roll) < IMU_HYSTERESIS_DEG) { err = ""; break; }

        // Monitor center vacuum on every iteration
        if (M_(ZDT_C).read_pressure() > VACUUM_THRESHOLD_KPA) {
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
                abort_flag    = true;
                motion_active_ = false;
                crane_cmd_("emergency_stop", 2);
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
bool WashRobot::zdt_wait_motion_done_(int slave, int timeout_ms) {
    const int    poll_ms            = 150;
    const int    STABLE_COUNT       = 3;
    const double SPEED_THRESHOLD_RPM = 20.0;
    const double POS_DELTA_DEG       = 0.15;
    int stable_count = 0;
    double prev_pos = 1e9;
    int elapsed = 0;
    int consecutive_fails = 0;
    int total_fails = 0;

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
        if (st.stall_flag) {
            std::cout << "[wait ZDT:" << slave << "] STALL at " << elapsed
                      << "ms, pos=" << st.real_pos << "° — release flag + fail\n";
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
                      << "ms, pos=" << st.real_pos << "° (total comms fails=" << total_fails << ")\n";
            if (driver_dbg_) Z_(slave).set_debug(true);   // restore for subsequent commands
            return false;
        }
    }
    std::cout << "[wait ZDT:" << slave << "] TIMEOUT after " << timeout_ms
              << "ms, last pos=" << prev_pos << "°, speed=" << Z_(slave).status.real_speed
              << " rpm, total comms fails=" << total_fails << "\n";
    if (driver_dbg_) Z_(slave).set_debug(true);   // restore (timeout path too)
    return true;
}

bool WashRobot::pusher_move_(int slave, int pulse, int rpm) {
    // Use _nowait variant + our stability-based wait (ZDT pos_reached bit unreliable).
    if (Z_(slave).motion_control_pos_mode_nowait(0, PUSHER_ACC, rpm, pulse, 1, 0, 1))
        return true;
    return zdt_wait_motion_done_(slave, 15000);
}

bool WashRobot::pusher_move_many_(const std::vector<int>& slaves, int pulse, int rpm) {
    // sync=1 pattern requires the _nowait variant: enqueue each slave's PR block
    // without internal wait, then broadcast trigger_sync_move, then poll per slave.
    // Using motion_control_pos_mode (with internal wait) against sync=1 causes the
    // driver to poll pos_reached before the broadcast ever fires → always timeout.
    // See memory project_zdt_firmware_quirks #4 "sync=1 + broadcast trigger pattern".
    for (int s : slaves) {
        if (Z_(s).motion_control_pos_mode_nowait(0, PUSHER_ACC, rpm, pulse, 1, 1, 1))
            return true;
    }
    if (!slaves.empty()) Z_(slaves.front()).trigger_sync_move();
    // Wait for each slave to physically stop (stability-based, pos_reached bit unreliable).
    for (int s : slaves) {
        if (zdt_wait_motion_done_(s, 15000)) return true;
    }
    sleep_ms_(PUSHER_SETTLE_MS);
    return false;
}

std::vector<int> WashRobot::group_slaves_(const std::string& group) {
    if (group == "feet")   return {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2};
    if (group == "body")   return {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2};
    if (group == "center") return {ZDT_C};
    if (group == "all")    return {ZDT_LF1, ZDT_LF2, ZDT_LB1, ZDT_LB2,
                                   ZDT_RF1, ZDT_RF2, ZDT_RB1, ZDT_RB2, ZDT_C};
    return {};
}

int WashRobot::group_valve_ch_(const std::string& group) {
    if (group == "feet")   return CH_VALVE_FEET;
    if (group == "body")   return CH_VALVE_BODY;
    if (group == "center") return CH_VALVE_CENTER;
    return -1;
}

bool WashRobot::vacuum_valve_(const std::string& group, bool on) {
    if (group == "all") {
        bool err = false;
        err |= pqw_.controlRelay(CH_VALVE_FEET,   on);
        err |= pqw_.controlRelay(CH_VALVE_BODY,   on);
        err |= pqw_.controlRelay(CH_VALVE_CENTER, on);
        return err;
    }
    int ch = group_valve_ch_(group);
    if (ch < 0) return true;
    return pqw_.controlRelay(ch, on);
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
                p = M_(s).read_pressure();
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
    if (try_or_pause_([this]() { return pqw_.controlRelay(CH_WATER_INLET, false); },
                      "init_water_inlet_off")) return "ERR aborted\n";

    // Retract wheels (Phase 1 climb → Phase 2 prep) before pushers extend.
    // Assumes Phase 1 zeroed wheels at retracted position before deploy.
    std::cout << "[init] DM2J wheels (slave 2, 4) → retract to 0\n";
    if (try_or_pause_([this]() { return D_(DM2J_LEFT_WHEEL).PR_move_cm(0, 1, DM2J_RPM, 0.0, DM2J_ACC, DM2J_DEC); },
                      "init_left_wheel_retract")) return "ERR aborted\n";
    if (try_or_pause_([this]() { return D_(DM2J_RIGHT_WHEEL).PR_move_cm(0, 1, DM2J_RPM, 0.0, DM2J_ACC, DM2J_DEC); },
                      "init_right_wheel_retract")) return "ERR aborted\n";

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
    std::cout << "[init] ZDT 1-9 → release_stall + driver enable\n";
    for (int s = 1; s <= 9; ++s) {
        Z_(s).release_stall_flag();
        if (try_or_pause_([this, s]() { return Z_(s).motion_control_driver_EN(true); },
                          std::string("init_zdt_enable_slave_") + std::to_string(s)))
            return "ERR aborted\n";
    }
    sleep_ms_(200);   // let drives settle into enabled state

    // Extend feet (7 cm) and body (10 cm) separately — each group has its own target.
    // Wrapped in try_or_pause_ so a ZDT stall drops into PausedOnError (allowing
    // continue/skip from GUI) instead of returning ERR + leaving state unchanged.
    std::vector<int> feet_group = {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2};
    std::vector<int> body_group = {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2};
    std::cout << "[init] feet pushers (ZDT " << ZDT_LF1 << "," << ZDT_LF2
              << "," << ZDT_RF1 << "," << ZDT_RF2
              << ") → extend " << PUSHER_EXTEND_FEET_PULSE << " pulses (~7 cm)\n";
    if (try_or_pause_([this, &feet_group]() { return pusher_move_many_(feet_group, PUSHER_EXTEND_FEET_PULSE); },
                      "init_feet_pusher_extend")) return "ERR aborted\n";
    std::cout << "[init] body pushers (ZDT " << ZDT_LB1 << "," << ZDT_LB2
              << "," << ZDT_RB1 << "," << ZDT_RB2
              << ") → extend " << PUSHER_EXTEND_BODY_PULSE << " pulses (~10 cm)\n";
    if (try_or_pause_([this, &body_group]() { return pusher_move_many_(body_group, PUSHER_EXTEND_BODY_PULSE); },
                      "init_body_pusher_extend")) return "ERR aborted\n";

    std::cout << "[init] DM2J arm (slave " << DM2J_ARM << ") → set current as zero\n";
    D_(DM2J_ARM).home_set_current_pos_zero();

    std::cout << "[init] IMU → take baseline\n";
    if (imu_take_baseline_()) return "ERR imu_baseline_fail\n";

    std::cout << "[init] done → Ready\n";
    set_state_(State::Ready);
    return "OK init_done\n";
}

std::string WashRobot::cmd_attach() {
    State cur = state_.load();
    if (cur != State::Ready) return state_violation_(cur);

    std::lock_guard<std::mutex> lk(motion_mtx_);
    abort_flag = false;

    std::cout << "[attach] center pusher extend\n";
    // Wrapped in try_or_pause_ — ZDT stall on center extend drops into
    // PausedOnError instead of failing attach outright.
    if (try_or_pause_([this]() { return pusher_move_(ZDT_C, PUSHER_EXTEND_PULSE); },
                      "attach_center_pusher_extend")) return "ERR aborted\n";
    std::cout << "[attach] open valves CH" << CH_VALVE_FEET << "/"
              << CH_VALVE_BODY << "/" << CH_VALVE_CENTER << "\n";
    if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_FEET, true); },
                      "attach_valve_feet_on")) return "ERR aborted\n";
    if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_BODY, true); },
                      "attach_valve_body_on")) return "ERR aborted\n";
    if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_CENTER, true); },
                      "attach_valve_center_on")) return "ERR aborted\n";
    std::cout << "[attach] settle " << VACUUM_SETTLE_MS << "ms\n";
    sleep_ms_(VACUUM_SETTLE_MS);

    // Verify all 9 cups (feet 4 + body 4 + center 1) actually sealed.
    // Re-enabled 2026-04-28 after threshold + multi-sample + comm-retry fixes.
    //auto fails; = vacuum_check_("all");
    //if (!fails.empty()) {
    //    std::string msg = "ERR attach_vacuum_fail slaves=";
    //    for (size_t i = 0; i < fails.size(); ++i) {
     //       if (i) msg += ",";
      //      msg += std::to_string(fails[i]);
       // }
       // msg += "\n";
       // std::cout << "[attach] FAIL: " << msg;
       // return msg;
  //  }
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

    // Sweep: centre → right → left → centre (home). Each DM2J motion wrapped
    // in try_or_pause_ so a stall / comm fail on arm drops into PausedOnError
    // for operator manual intervention rather than aborting the whole sweep.
    std::string err;
    if (try_or_pause_([this]() { return D_(DM2J_ARM).PR_move_cm(0, 1, ARM_SWEEP_RPM,  ARM_SWEEP_CM, DM2J_ACC, DM2J_DEC); },
                      "arm_sweep_right"))
        err = "ERR aborted\n";
    else if (check_abort_())
        err = "ERR aborted\n";
    else if (try_or_pause_([this]() { return D_(DM2J_ARM).PR_move_cm(0, 1, ARM_SWEEP_RPM, -ARM_SWEEP_CM, DM2J_ACC, DM2J_DEC); },
                           "arm_sweep_left"))
        err = "ERR aborted\n";
    else if (check_abort_())
        err = "ERR aborted\n";
    else if (try_or_pause_([this]() { return D_(DM2J_ARM).PR_move_cm(0, 1, ARM_SWEEP_RPM, 0.0, DM2J_ACC, DM2J_DEC); },
                           "arm_sweep_home"))
        err = "ERR aborted\n";

    // Stop cleaning regardless of outcome
    pqw_.controlRelay(CH_BRUSH,       false);
    pqw_.controlRelay(CH_WATER_PUMP,  false);
    pqw_.controlRelay(CH_WATER_INLET, false);

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
std::string WashRobot::do_step_down_() {
    std::lock_guard<std::mutex> lk(motion_mtx_);
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
    auto body_pre_cycle = [this]() -> std::string {
        // Release body + center valves BEFORE retracting pushers (cups would
        // grip wall otherwise, stalling pusher retract).
        if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_BODY, false); },
                          "body_pre_valve_off_body")) return "aborted";
        if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_CENTER, false); },
                          "body_pre_valve_off_center")) return "aborted";
        sleep_ms_(VACUUM_RELEASE_WAIT_MS);

        // Two-stage retract for center pusher (half → wait → full).
        if (try_or_pause_([this]() { return pusher_move_(ZDT_C, PUSHER_EXTEND_PULSE / 2, PUSHER_RPM_RETRACT); },
                          "body_pre_center_retract_half")) return "aborted";
        sleep_ms_(RETRACT_HALF_WAIT_MS);
        if (try_or_pause_([this]() { return pusher_move_(ZDT_C, PUSHER_RETRACT_PULSE, PUSHER_RPM_RETRACT); },
                          "body_pre_center_retract_full")) return "aborted";

        // Body pushers: two-stage retract (half → wait → full) to give cups
        // time to release from wall after valve OFF — avoids ZDT stall when
        // cup adhesion lingers.
        std::vector<int> body_slaves = {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2};
        if (try_or_pause_([this, &body_slaves]() { return pusher_move_many_(body_slaves, PUSHER_EXTEND_BODY_PULSE / 2, PUSHER_RPM_RETRACT); },
                          "body_pre_pusher_retract_half")) return "aborted";
        sleep_ms_(RETRACT_HALF_WAIT_MS);
        if (try_or_pause_([this, &body_slaves]() { return pusher_move_many_(body_slaves, PUSHER_RETRACT_PULSE, PUSHER_RPM_RETRACT); },
                          "body_pre_pusher_retract_full")) return "aborted";

        // Crane pay out (step_cm + margin) for rope slack before body descent.
        const int step = step_cm_.load();
        {
            std::ostringstream oss;
            oss << "pay_out " << (step + STEP_MARGIN_CM);
            const std::string cmd_str = oss.str();
            std::cout << "[step_down] crane " << cmd_str << "\n";
            if (try_or_pause_([this, cmd_str]() { return crane_cmd_(cmd_str).rfind("OK", 0) != 0; },
                              "body_pre_crane_pay_out")) return "aborted";
        }

        // DM2J rails → absolute +step_cm (body descends).
        // Synchronized pair helper: broadcast trigger + parallel poll + position
        // diagnostic log. Bystanders 2/4/5 have PR1=rpm=0 from cmd_init so
        // broadcast is safe.
        if (try_or_pause_([this, step]() { return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, (double)step); },
                          "body_pre_rail_forward")) return "aborted";
        rail_pos_cm_.store((double)step);

        // Crane retract margin to restore normal tension after body descent.
        {
            std::ostringstream oss;
            oss << "retract " << STEP_MARGIN_CM;
            const std::string cmd_str = oss.str();
            std::cout << "[step_down] crane " << cmd_str << "\n";
            if (try_or_pause_([this, cmd_str]() { return crane_cmd_(cmd_str).rfind("OK", 0) != 0; },
                              "body_pre_crane_retract")) return "aborted";
        }

        // Re-extend center + re-engage center valve
        if (try_or_pause_([this]() { return pusher_move_(ZDT_C, PUSHER_EXTEND_PULSE); },
                          "body_pre_center_extend")) return "aborted";
        if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_CENTER, true); },
                          "body_pre_valve_on_center")) return "aborted";
        return "";
    };

    auto body_backup = [this](bool dry_run) -> std::string {
        // Body retry: rail retreats from +STEP toward 0 (body goes back up).
        // Bail out if next backup would exceed the safe range [0, step_cm]:
        // body retry should never push rail below 0 (body would have to climb
        // past the previous step's lock position — exceeds mechanical headroom).
        double target = rail_pos_cm_.load() - VACUUM_BACKUP_CM;
        if (target < 0.0) {
            if (!dry_run)
                std::cout << "  [retry body] target " << target
                          << " cm < 0 — no more backup space, abort retries\n";
            return "body_backup_no_space";
        }
        if (dry_run) return "";   // feasible — no side effects in dry-run

        // Crane pay_out gives rope slack for retreat motion.
        {
            std::ostringstream oss;
            oss << "pay_out " << (int)VACUUM_BACKUP_CM;
            const std::string cmd_str = oss.str();
            std::cout << "  [retry body] crane " << cmd_str << "\n";
            if (try_or_pause_([this, cmd_str]() { return crane_cmd_(cmd_str).rfind("OK", 0) != 0; },
                              "body_backup_crane_pay_out")) return "aborted";
        }
        std::cout << "  [retry body] rail " << rail_pos_cm_.load() << " → " << target << " cm\n";
        if (try_or_pause_([this, target]() { return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, target); },
                          "body_backup_rail")) return "aborted";
        rail_pos_cm_.store(target);
        return "";
    };

    int body_retries = 0;
    std::string err = cycle_group_("body", body_pre_cycle, body_backup, body_retries);
    if (!err.empty()) { motion_active_ = false; return "ERR " + err + "\n"; }

    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    // ---------- B. Feet phase (second, feet catches up to body) ----------
    auto feet_pre_cycle = [this]() -> std::string {
        // Release feet valve BEFORE retracting
        if (try_or_pause_([this]() { return pqw_.controlRelay(CH_VALVE_FEET, false); },
                          "feet_pre_valve_off")) return "aborted";
        sleep_ms_(VACUUM_RELEASE_WAIT_MS);   // wait for vacuum to fully release before retracting ZDT pushers

        // Two-stage feet retract: half → wait → full. Same rationale as body
        // (avoid ZDT stall on lingering cup adhesion).
        std::vector<int> feet_slaves = {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2};
        if (try_or_pause_([this, &feet_slaves]() { return pusher_move_many_(feet_slaves, PUSHER_EXTEND_FEET_PULSE / 2, PUSHER_RPM_RETRACT); },
                          "feet_pre_pusher_retract_half")) return "aborted";
        sleep_ms_(RETRACT_HALF_WAIT_MS);
        if (try_or_pause_([this, &feet_slaves]() { return pusher_move_many_(feet_slaves, PUSHER_RETRACT_PULSE, PUSHER_RPM_RETRACT); },
                          "feet_pre_pusher_retract_full")) return "aborted";

        // DM2J rails → absolute 0 (feet catches up, rail retracts)
        if (try_or_pause_([this]() { return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, 0.0); },
                          "feet_pre_rail_home")) return "aborted";
        rail_pos_cm_.store(0.0);
        return "";
    };

    auto feet_backup = [this](bool dry_run) -> std::string {
        // Feet retry: rail retreats from 0 back toward +STEP (feet goes back up).
        // Bail out if next backup would exceed the safe range [0, step_cm]:
        // feet retry should never push rail beyond where body just was — that
        // would mean feet climbing higher than the previous lock point.
        const double step = (double)step_cm_.load();
        double target = rail_pos_cm_.load() + VACUUM_BACKUP_CM;
        if (target > step) {
            if (!dry_run)
                std::cout << "  [retry feet] target " << target
                          << " cm > step " << step << " — no more backup space, abort retries\n";
            return "feet_backup_no_space";
        }
        if (dry_run) return "";   // feasible — no side effects in dry-run

        {
            std::ostringstream oss;
            oss << "pay_out " << (int)VACUUM_BACKUP_CM;
            const std::string cmd_str = oss.str();
            std::cout << "  [retry feet] crane " << cmd_str << "\n";
            if (try_or_pause_([this, cmd_str]() { return crane_cmd_(cmd_str).rfind("OK", 0) != 0; },
                              "feet_backup_crane_pay_out")) return "aborted";
        }
        std::cout << "  [retry feet] rail " << rail_pos_cm_.load() << " → " << target << " cm\n";
        if (try_or_pause_([this, target]() { return dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, target); },
                          "feet_backup_rail")) return "aborted";
        rail_pos_cm_.store(target);
        return "";
    };

    int feet_retries = 0;
    err = cycle_group_("feet", feet_pre_cycle, feet_backup, feet_retries);
    if (!err.empty()) { motion_active_ = false; return "ERR " + err + "\n"; }

    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    // ---------- C. Wash sweep ----------
    // 每完成一輪 body+feet 下移後，自動跑 arm_sweep 清洗該段玻璃。
    // arm_sweep 內部 3 段 DM2J 動作各自包了 try_or_pause_，arm 卡住會
    // 進 PausedOnError 讓使用者處理（不會直接整個 step_down 中斷）。
    std::cout << "[step_down] start wash sweep (arm_sweep)\n";
    std::string arm_reply = do_arm_sweep_();
    motion_active_ = false;
    if (arm_reply.rfind("OK", 0) != 0) {
        std::cout << "[step_down] wash sweep FAIL: " << arm_reply;
        return arm_reply;
    }

    return "OK step_done\n";
}

std::string WashRobot::cmd_step_down(int cm) {
    State cur = state_.load();
    if (cur != State::Attached) return state_violation_(cur);
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
    return r;
}

std::string WashRobot::cmd_run(int steps, int cm) {
    if (steps <= 0) return "ERR invalid_steps\n";
    State cur = state_.load();
    if (cur != State::Attached) return state_violation_(cur);
    if (cm > 0) {
        if (cm < STEP_CM_MIN || cm > STEP_CM_MAX) {
            std::ostringstream oss;
            oss << "ERR step_cm_out_of_range " << cm
                << " (allowed " << STEP_CM_MIN << ".." << STEP_CM_MAX << ")\n";
            return oss.str();
        }
        step_cm_.store(cm);
    }
    std::cout << "[run] " << steps << " steps × " << step_cm_.load() << " cm\n";
    set_state_(State::Running);

    motion_active_ = true;
    for (int i = 1; i <= steps; ++i) {
        if (abort_flag.load()) {
            motion_active_ = false;
            set_state_(State::Error);
            return "ERR aborted\n";
        }
        std::ostringstream oss; oss << "step " << i << "/" << steps;
        evt_(oss.str());

        std::string r = do_step_down_();
        if (r.rfind("OK", 0) != 0) {
            motion_active_ = false;
            set_state_(State::Error);
            return r;
        }

        // If IMU flagged a balance issue, pause and wait for confirm_balance
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
    crane_cmd_("emergency_stop", 2);
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
    pqw_.controlRelay(CH_WATER_INLET,  false);
    pqw_.controlRelay(CH_VALVE_FEET,   false);
    pqw_.controlRelay(CH_VALVE_BODY,   false);
    pqw_.controlRelay(CH_VALVE_CENTER, false);
    pqw_.controlRelay(CH_PUMP,         false);
    return "OK shutdown\n";
}

std::string WashRobot::cmd_status() {
    std::ostringstream oss;
    oss << "OK state=" << state_name(state_.load());
    oss << " crane_attached=" << (crane_attached_.load() ? "on" : "off");
    oss << std::fixed << std::setprecision(1)
        << " rail=" << rail_pos_cm_.load()
        << " body_residual=" << body_residual_cm_.load();
    for (int s = 1; s <= 9; ++s) {
        int p = M_(s).read_pressure();
        oss << " p" << s << "=" << p;
    }
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

std::string WashRobot::cmd_pusher(const std::string& group, const std::string& pos) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);

    // All pusher motions wrapped in try_or_pause_ — a ZDT stall now drops into
    // PausedOnError so the GUI continue/skip buttons can resolve it (matches
    // step_down behavior). Abort path returns "ERR aborted" + state already
    // moved to Error by emergency_stop.

    if (pos == "retract") {
        // Two-stage retract for all ZDT groups: half → wait → full. "all" is
        // expanded into 3 sub-groups (feet / body / center) each with their
        // own half-pulse since extend depths differ.
        if (group == "all") {
            std::vector<int> feet_g   = {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2};
            std::vector<int> body_g   = {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2};
            std::vector<int> center_g = {ZDT_C};

            if (try_or_pause_([this, &feet_g]() { return pusher_move_many_(feet_g, PUSHER_EXTEND_FEET_PULSE / 2, PUSHER_RPM_RETRACT); },
                              "manual_pusher_all_feet_retract_half")) return "ERR aborted\n";
            sleep_ms_(RETRACT_HALF_WAIT_MS);
            if (try_or_pause_([this, &feet_g]() { return pusher_move_many_(feet_g, PUSHER_RETRACT_PULSE, PUSHER_RPM_RETRACT); },
                              "manual_pusher_all_feet_retract_full")) return "ERR aborted\n";

            if (try_or_pause_([this, &body_g]() { return pusher_move_many_(body_g, PUSHER_EXTEND_BODY_PULSE / 2, PUSHER_RPM_RETRACT); },
                              "manual_pusher_all_body_retract_half")) return "ERR aborted\n";
            sleep_ms_(RETRACT_HALF_WAIT_MS);
            if (try_or_pause_([this, &body_g]() { return pusher_move_many_(body_g, PUSHER_RETRACT_PULSE, PUSHER_RPM_RETRACT); },
                              "manual_pusher_all_body_retract_full")) return "ERR aborted\n";

            if (try_or_pause_([this, &center_g]() { return pusher_move_many_(center_g, PUSHER_EXTEND_PULSE / 2, PUSHER_RPM_RETRACT); },
                              "manual_pusher_all_center_retract_half")) return "ERR aborted\n";
            sleep_ms_(RETRACT_HALF_WAIT_MS);
            if (try_or_pause_([this, &center_g]() { return pusher_move_many_(center_g, PUSHER_RETRACT_PULSE, PUSHER_RPM_RETRACT); },
                              "manual_pusher_all_center_retract_full")) return "ERR aborted\n";
            return "OK\n";
        }

        auto slaves = group_slaves_(group);
        if (slaves.empty()) return "ERR unknown_group\n";
        const int half_pulse = (group == "feet") ? PUSHER_EXTEND_FEET_PULSE / 2
                             : (group == "body") ? PUSHER_EXTEND_BODY_PULSE / 2
                             :                     PUSHER_EXTEND_PULSE / 2;   // center
        const std::string ctx = "manual_pusher_" + group + "_retract";
        if (try_or_pause_([this, &slaves, half_pulse]() { return pusher_move_many_(slaves, half_pulse, PUSHER_RPM_RETRACT); },
                          ctx + "_half")) return "ERR aborted\n";
        sleep_ms_(RETRACT_HALF_WAIT_MS);
        if (try_or_pause_([this, &slaves]() { return pusher_move_many_(slaves, PUSHER_RETRACT_PULSE, PUSHER_RPM_RETRACT); },
                          ctx + "_full")) return "ERR aborted\n";
        return "OK\n";
    }
    if (pos == "extend") {
        // "all" = extend feet + body + center at their respective depths (3 group calls).
        if (group == "all") {
            std::vector<int> feet_group   = {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2};
            std::vector<int> body_group   = {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2};
            std::vector<int> center_group = {ZDT_C};
            if (try_or_pause_([this, &feet_group]() { return pusher_move_many_(feet_group, PUSHER_EXTEND_FEET_PULSE); },
                              "manual_pusher_all_feet_extend")) return "ERR aborted\n";
            if (try_or_pause_([this, &body_group]() { return pusher_move_many_(body_group, PUSHER_EXTEND_BODY_PULSE); },
                              "manual_pusher_all_body_extend")) return "ERR aborted\n";
            if (try_or_pause_([this, &center_group]() { return pusher_move_many_(center_group, PUSHER_EXTEND_PULSE); },
                              "manual_pusher_all_center_extend")) return "ERR aborted\n";
            return "OK\n";
        }
        int pulse = (group == "feet") ? PUSHER_EXTEND_FEET_PULSE
                  : (group == "body") ? PUSHER_EXTEND_BODY_PULSE
                  :                     PUSHER_EXTEND_PULSE;   // center
        auto slaves = group_slaves_(group);
        if (slaves.empty()) return "ERR unknown_group\n";
        const std::string ctx = "manual_pusher_" + group + "_extend";
        if (try_or_pause_([this, &slaves, pulse]() { return pusher_move_many_(slaves, pulse); },
                          ctx)) return "ERR aborted\n";
        return "OK\n";
    }
    return "ERR expected_extend_or_retract\n";
}

// Set current ZDT position as new zero for the given group (ZDT manual 3.1.3,
// Reg 0x000A). Caveat: should typically be called when pushers are physically
// at retracted hard-stop, otherwise subsequent abs-0 moves won't return to the
// real bottom. Group "all" hits feet+body+center (8+1=9 slaves).
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
    if (D_(slave).PR_move_cm(0, 1, DM2J_RPM, cm, DM2J_ACC, DM2J_DEC))
        return "ERR move_fail\n";
    return "OK\n";
}

// Move both wheels (slave 2, 4) to an absolute target in parallel.
// "retract" = abs 0 cm, "lower" = abs -7 cm. Mirrors init()'s startup wheel-lower:
// PR_move_cm_nowait on both triggers within one Modbus frame, then sequential wait.
std::string WashRobot::cmd_wheels(const std::string& action) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);

    double target_cm;
    if      (action == "retract") target_cm = 0.0;
    else if (action == "lower")   target_cm = -7.0;
    else return "ERR expected_retract_or_lower\n";

    if (D_(DM2J_LEFT_WHEEL ).PR_move_cm_nowait(0, 1, DM2J_RPM, target_cm, DM2J_ACC, DM2J_DEC))
        return "ERR left_wheel_trigger_fail\n";
    if (D_(DM2J_RIGHT_WHEEL).PR_move_cm_nowait(0, 1, DM2J_RPM, target_cm, DM2J_ACC, DM2J_DEC))
        return "ERR right_wheel_trigger_fail\n";
    if (dm2j_wait_done_(DM2J_LEFT_WHEEL )) return "ERR left_wheel_timeout\n";
    if (dm2j_wait_done_(DM2J_RIGHT_WHEEL)) return "ERR right_wheel_timeout\n";
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
        if (D_(DM2J_LEFT_WHEEL ).PR_move_cm_nowait(0, 1, DM2J_RPM, cm, DM2J_ACC, DM2J_DEC))
            return "ERR left_wheel_trigger_fail\n";
        if (D_(DM2J_RIGHT_WHEEL).PR_move_cm_nowait(0, 1, DM2J_RPM, cm, DM2J_ACC, DM2J_DEC))
            return "ERR right_wheel_trigger_fail\n";
        if (dm2j_wait_done_(DM2J_LEFT_WHEEL )) return "ERR left_wheel_timeout\n";
        if (dm2j_wait_done_(DM2J_RIGHT_WHEEL)) return "ERR right_wheel_timeout\n";
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
    pqw_.controlRelay(CH_WATER_INLET, false);

    // 3. Break suction on all three groups
    pqw_.controlRelay(CH_VALVE_FEET,   false);
    pqw_.controlRelay(CH_VALVE_BODY,   false);
    pqw_.controlRelay(CH_VALVE_CENTER, false);

    // 4. Wait for cups to fully release
    sleep_ms_(RETURN_VACUUM_RELEASE_MS);
    if (check_abort_()) return fail("ERR aborted\n");

    // 5. Verify all 9 cups detached (pressure risen back near atmosphere).
    //    Retracting pushers while still attached would damage the LEYG25 rods.
    std::vector<int> still_attached;
    for (int s = 1; s <= 9; ++s) {
        int p = M_(s).read_pressure();
        if (p < DETACH_THRESHOLD_KPA) still_attached.push_back(s);
    }
    if (!still_attached.empty()) {
        std::string msg = "ERR detach_fail slaves=";
        for (size_t i = 0; i < still_attached.size(); ++i) {
            if (i) msg += ",";
            msg += std::to_string(still_attached[i]);
        }
        msg += "\n";
        return fail(msg);
    }

    // 6. Retract all 9 pushers
    // Wrapped in try_or_pause_ — stall during return-home retract drops into
    // PausedOnError. Abort path falls through to fail() (sets Error state).
    std::vector<int> all9 = {ZDT_LF1, ZDT_LF2, ZDT_LB1, ZDT_LB2,
                             ZDT_RF1, ZDT_RF2, ZDT_RB1, ZDT_RB2, ZDT_C};
    if (try_or_pause_([this, &all9]() { return pusher_move_many_(all9, PUSHER_RETRACT_PULSE, PUSHER_RPM_RETRACT); },
                      "return_home_pusher_retract"))
        return fail("ERR aborted\n");
    if (check_abort_()) return fail("ERR aborted\n");

    // 7. Vacuum pump off
    pqw_.controlRelay(CH_PUMP, false);

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
        state_before_pause_ = prev;
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
