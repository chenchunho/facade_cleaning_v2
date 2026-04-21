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

    // DM2J slave 1..5
    for (int i = 1; i <= 5; ++i) {
        if (D_(i).init(cli_20_, i, false)) {
            std::cerr << "[FATAL] DM2J slave " << i << " init fail\n"; return true;
        }
    }
    std::cout << "[OK] DM2J 1~5\n";

    // ZDT slave 1..9
    for (int i = 1; i <= 9; ++i) {
        if (Z_(i).init(cli_21_, i, false)) {
            std::cerr << "[FATAL] ZDT slave " << i << " init fail\n"; return true;
        }
    }
    std::cout << "[OK] ZDT 1~9\n";

    // JC-100 slave 1..9
    for (int i = 1; i <= 9; ++i) {
        if (M_(i).init(cli_22_, i, false)) {
            std::cerr << "[FATAL] JC-100 slave " << i << " init fail\n"; return true;
        }
    }
    std::cout << "[OK] JC-100 1~9\n";

    // PQW 8CH relay
    if (pqw_.init(cli_22_, PQW_SLAVE, PQW_TOTAL_CH, false)) {
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
    imu_.init(&imu_serial_, false);
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

        crane_cmd_("ping", 2);

        int64_t elapsed = now_ms_() - crane_last_ok_ms_.load();
        if (elapsed > WATCHDOG_TIMEOUT_MS) {
            if (motion_active_.load()) {
                abort_flag = true;
                evt_("crane_watchdog timeout");
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
    if (M_(ZDT_C).read_pressure() > VACUUM_THRESHOLD_X10)
        return "phase5_center_vacuum_fail";

    // Steps 2-6: iterative roll correction
    std::string err;
    for (int attempt = 1; attempt <= ROLL_CORRECT_RETRY_MAX; ++attempt) {
        double roll = imu_.x - imu_roll0_;
        if (std::abs(roll) < IMU_HYSTERESIS_DEG) { err = ""; break; }

        // Monitor center vacuum on every iteration
        if (M_(ZDT_C).read_pressure() > VACUUM_THRESHOLD_X10) {
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
                abort_flag    = true;
                motion_active_ = false;
                crane_cmd_("emergency_stop", 2);
                set_state_(State::Error);
                std::ostringstream oss;
                oss << "imu_emergency balance_deg=" << std::fixed << std::setprecision(1) << avg;
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

bool WashRobot::pusher_move_(int slave, int pulse) {
    if (Z_(slave).motion_control_pos_mode(0, PUSHER_ACC, PUSHER_RPM, pulse, 1, 0, 1))
        return true;
    Z_(slave).wait_until_pos_reached(15000, 200);
    return false;
}

bool WashRobot::pusher_move_many_(const std::vector<int>& slaves, int pulse) {
    for (int s : slaves) {
        if (Z_(s).motion_control_pos_mode(0, PUSHER_ACC, PUSHER_RPM, pulse, 1, 1, 1))
            return true;
    }
    if (!slaves.empty()) Z_(slaves.front()).trigger_sync_move();
    for (int s : slaves) Z_(s).wait_until_pos_reached(15000, 200);
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
    std::vector<int> fail;
    for (int s : group_slaves_(group)) {
        int p = M_(s).read_pressure();
        if (p > VACUUM_THRESHOLD_X10) fail.push_back(s);
    }
    return fail;
}

//=========== commands ===========

std::string WashRobot::cmd_init() {
    State cur = state_.load();
    if (cur != State::Idle && cur != State::Ready && cur != State::Error)
        return state_violation_(cur);

    std::lock_guard<std::mutex> lk(motion_mtx_);
    abort_flag = false;
    pause_flag = false;

    if (pqw_.controlRelay(CH_PUMP,         true))  return "ERR pump_on_fail\n";
    if (pqw_.controlRelay(CH_VALVE_FEET,   false)) return "ERR valve_feet_off_fail\n";
    if (pqw_.controlRelay(CH_VALVE_BODY,   false)) return "ERR valve_body_off_fail\n";
    if (pqw_.controlRelay(CH_VALVE_CENTER, false)) return "ERR valve_center_off_fail\n";
    if (pqw_.controlRelay(CH_BRUSH,        false)) return "ERR brush_off_fail\n";
    if (pqw_.controlRelay(CH_WATER_PUMP,   false)) return "ERR water_pump_off_fail\n";
    if (pqw_.controlRelay(CH_WATER_INLET,  false)) return "ERR water_inlet_off_fail\n";

    // Retract wheels (Phase 1 climb → Phase 2 prep) before pushers extend.
    // Assumes Phase 1 zeroed wheels at retracted position before deploy.
    if (D_(DM2J_LEFT_WHEEL ).PR_move_cm(0, 1, DM2J_RPM, 0.0, DM2J_ACC, DM2J_DEC))
        return "ERR left_wheel_retract_fail\n";
    if (D_(DM2J_RIGHT_WHEEL).PR_move_cm(0, 1, DM2J_RPM, 0.0, DM2J_ACC, DM2J_DEC))
        return "ERR right_wheel_retract_fail\n";

    std::vector<int> feet_body = {ZDT_LF1, ZDT_LF2, ZDT_LB1, ZDT_LB2,
                                  ZDT_RF1, ZDT_RF2, ZDT_RB1, ZDT_RB2};
    if (pusher_move_many_(feet_body, PUSHER_EXTEND_PULSE)) return "ERR pusher_extend_fail\n";

    D_(DM2J_ARM).home_set_current_pos_zero();

    if (imu_take_baseline_()) return "ERR imu_baseline_fail\n";

    set_state_(State::Ready);
    return "OK init_done\n";
}

std::string WashRobot::cmd_attach() {
    State cur = state_.load();
    if (cur != State::Ready) return state_violation_(cur);

    std::lock_guard<std::mutex> lk(motion_mtx_);
    abort_flag = false;

    if (pusher_move_(ZDT_C, PUSHER_EXTEND_PULSE)) return "ERR center_extend_fail\n";
    if (pqw_.controlRelay(CH_VALVE_FEET,   true)) return "ERR valve_feet_fail\n";
    if (pqw_.controlRelay(CH_VALVE_BODY,   true)) return "ERR valve_body_fail\n";
    if (pqw_.controlRelay(CH_VALVE_CENTER, true)) return "ERR valve_center_fail\n";
    sleep_ms_(VACUUM_SETTLE_MS);

    auto fails = vacuum_check_("all");
    if (!fails.empty()) {
        std::string msg = "ERR attach_vacuum_fail slaves=";
        for (size_t i = 0; i < fails.size(); ++i) { if (i) msg += ","; msg += std::to_string(fails[i]); }
        msg += "\n";
        return msg;
    }
    set_state_(State::Attached);
    return "OK attached\n";
}

std::string WashRobot::cmd_detach() {
    State cur = state_.load();
    if (cur != State::Attached) return state_violation_(cur);

    std::lock_guard<std::mutex> lk(motion_mtx_);
    pqw_.controlRelay(CH_VALVE_FEET,   false);
    pqw_.controlRelay(CH_VALVE_BODY,   false);
    pqw_.controlRelay(CH_VALVE_CENTER, false);
    set_state_(State::Ready);
    return "OK detached\n";
}

// Internal sweep — caller must already hold motion_mtx_
std::string WashRobot::do_arm_sweep_() {
    // Start cleaning: brush motor + water pump + water inlet valve
    pqw_.controlRelay(CH_WATER_INLET, true);
    pqw_.controlRelay(CH_WATER_PUMP,  true);
    pqw_.controlRelay(CH_BRUSH,       true);

    // Sweep: centre → right → left → centre (home)
    std::string err;
    if (D_(DM2J_ARM).PR_move_cm(0, 1, ARM_SWEEP_RPM,  ARM_SWEEP_CM, DM2J_ACC, DM2J_DEC))
        err = "ERR arm_right_fail\n";
    else if (check_abort_())
        err = "ERR aborted\n";
    else if (D_(DM2J_ARM).PR_move_cm(0, 1, ARM_SWEEP_RPM, -ARM_SWEEP_CM, DM2J_ACC, DM2J_DEC))
        err = "ERR arm_left_fail\n";
    else if (check_abort_())
        err = "ERR aborted\n";
    else if (D_(DM2J_ARM).PR_move_cm(0, 1, ARM_SWEEP_RPM, 0.0, DM2J_ACC, DM2J_DEC))
        err = "ERR arm_home_fail\n";

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

    const double rail_at_start  = rail_pos_cm_.load();
    const double residual_start = body_residual_cm_.load();

    // ---------- A. Feet phase ----------
    auto feet_pre_cycle = [this]() -> std::string {
        // Initial release + retract (prep for crane move)
        if (pqw_.controlRelay(CH_VALVE_FEET, false)) return "feet_valve_off_fail";
        sleep_ms_(300);
        std::vector<int> feet_slaves = {ZDT_LF1, ZDT_LF2, ZDT_RF1, ZDT_RF2};
        if (pusher_move_many_(feet_slaves, PUSHER_RETRACT_PULSE)) return "feet_pusher_retract_fail";

        // Crane pay out (STEP_CM + margin) for rope slack
        {
            std::ostringstream oss;
            oss << "pay_out " << (STEP_CM + STEP_MARGIN_CM);
            if (crane_cmd_(oss.str()).rfind("OK", 0) != 0) return "crane_pay_out_fail";
        }

        // DM2J move rails to absolute +STEP_CM (auto-compensates body residual)
        D_(DM2J_LEFT_FOOT ).PR_move_cm_set(1, 1, DM2J_RPM, (double)STEP_CM, DM2J_ACC, DM2J_DEC);
        D_(DM2J_RIGHT_FOOT).PR_move_cm_set(1, 1, DM2J_RPM, (double)STEP_CM, DM2J_ACC, DM2J_DEC);
        D_(DM2J_LEFT_FOOT ).PR_trigger_sync(1);
        if (dm2j_wait_done_(DM2J_LEFT_FOOT))  return "feet_left_move_fail";
        if (dm2j_wait_done_(DM2J_RIGHT_FOOT)) return "feet_right_move_fail";
        rail_pos_cm_.store((double)STEP_CM);

        // Crane retract margin to restore normal tension
        {
            std::ostringstream oss;
            oss << "retract " << STEP_MARGIN_CM;
            if (crane_cmd_(oss.str()).rfind("OK", 0) != 0) return "crane_retract_fail";
        }
        return "";
    };

    auto feet_backup = [this]() -> std::string {
        // Relative -VACUUM_BACKUP_CM (rail reverses toward body)
        D_(DM2J_LEFT_FOOT ).PR_move_cm_set(1, 0, DM2J_RPM, -VACUUM_BACKUP_CM, DM2J_ACC, DM2J_DEC);
        D_(DM2J_RIGHT_FOOT).PR_move_cm_set(1, 0, DM2J_RPM, -VACUUM_BACKUP_CM, DM2J_ACC, DM2J_DEC);
        D_(DM2J_LEFT_FOOT ).PR_trigger_sync(1);
        if (dm2j_wait_done_(DM2J_LEFT_FOOT))  return "feet_backup_left_fail";
        if (dm2j_wait_done_(DM2J_RIGHT_FOOT)) return "feet_backup_right_fail";
        rail_pos_cm_.store(rail_pos_cm_.load() - VACUUM_BACKUP_CM);
        return "";
    };

    int feet_retries = 0;
    std::string err = cycle_group_("feet", feet_pre_cycle, feet_backup, feet_retries);
    if (!err.empty()) { motion_active_ = false; return "ERR " + err + "\n"; }

    // Feet phase consumed any prior body residual
    body_residual_cm_.store(0.0);
    // Actual DM2J travel in feet phase (for diagnostics / body target)
    actual_feet_cm_ = (double)STEP_CM - rail_at_start - VACUUM_BACKUP_CM * feet_retries;
    (void)residual_start;  // already absorbed via absolute positioning

    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    // ---------- B. Body phase ----------
    auto body_pre_cycle = [this]() -> std::string {
        // Release center briefly so body can slide
        if (pqw_.controlRelay(CH_VALVE_CENTER, false)) return "center_valve_off_fail";
        sleep_ms_(300);
        if (pusher_move_(ZDT_C, PUSHER_RETRACT_PULSE)) return "center_retract_fail";

        // Retract body pushers
        std::vector<int> body_slaves = {ZDT_LB1, ZDT_LB2, ZDT_RB1, ZDT_RB2};
        if (pusher_move_many_(body_slaves, PUSHER_RETRACT_PULSE)) return "body_pusher_retract_fail";

        // DM2J absolute to 0 (body slides down, rail returns to origin)
        D_(DM2J_LEFT_FOOT ).PR_move_cm_set(1, 1, DM2J_RPM, 0.0, DM2J_ACC, DM2J_DEC);
        D_(DM2J_RIGHT_FOOT).PR_move_cm_set(1, 1, DM2J_RPM, 0.0, DM2J_ACC, DM2J_DEC);
        D_(DM2J_LEFT_FOOT ).PR_trigger_sync(1);
        if (dm2j_wait_done_(DM2J_LEFT_FOOT))  return "body_left_move_fail";
        if (dm2j_wait_done_(DM2J_RIGHT_FOOT)) return "body_right_move_fail";
        rail_pos_cm_.store(0.0);

        // Re-extend center + re-attach center valve
        if (pusher_move_(ZDT_C, PUSHER_EXTEND_PULSE)) return "center_extend_fail";
        if (pqw_.controlRelay(CH_VALVE_CENTER, true)) return "center_valve_on_fail";
        return "";
    };

    auto body_backup = [this]() -> std::string {
        // Relative +VACUUM_BACKUP_CM (rail reverses toward feet, body retreats)
        D_(DM2J_LEFT_FOOT ).PR_move_cm_set(1, 0, DM2J_RPM, VACUUM_BACKUP_CM, DM2J_ACC, DM2J_DEC);
        D_(DM2J_RIGHT_FOOT).PR_move_cm_set(1, 0, DM2J_RPM, VACUUM_BACKUP_CM, DM2J_ACC, DM2J_DEC);
        D_(DM2J_LEFT_FOOT ).PR_trigger_sync(1);
        if (dm2j_wait_done_(DM2J_LEFT_FOOT))  return "body_backup_left_fail";
        if (dm2j_wait_done_(DM2J_RIGHT_FOOT)) return "body_backup_right_fail";
        rail_pos_cm_.store(rail_pos_cm_.load() + VACUUM_BACKUP_CM);
        return "";
    };

    int body_retries = 0;
    err = cycle_group_("body", body_pre_cycle, body_backup, body_retries);
    if (!err.empty()) { motion_active_ = false; return "ERR " + err + "\n"; }

    // Body residual = how much body under-retracted (next feet phase absorbs via absolute target)
    body_residual_cm_.store(VACUUM_BACKUP_CM * body_retries);

    if (check_abort_()) { motion_active_ = false; return "ERR aborted\n"; }

    // ---------- C. Wash sweep ----------
    std::string arm_reply = do_arm_sweep_();
    motion_active_ = false;
    if (arm_reply.rfind("OK", 0) != 0) return arm_reply;

    return "OK step_done\n";
}

std::string WashRobot::cmd_step_down() {
    State cur = state_.load();
    if (cur != State::Attached) return state_violation_(cur);
    set_state_(State::Running);

    std::string r = do_step_down_();
    if (r.rfind("OK", 0) != 0) {
        set_state_(State::Error);
        return r;
    }
    set_state_(State::Attached);
    return r;
}

std::string WashRobot::cmd_run(int steps) {
    if (steps <= 0) return "ERR invalid_steps\n";
    State cur = state_.load();
    if (cur != State::Attached) return state_violation_(cur);
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

std::string WashRobot::cmd_pusher(const std::string& group, const std::string& pos) {
    State cur = state_.load();
    if (cur == State::Error) return state_violation_(cur);
    int pulse;
    if      (pos == "extend")  pulse = PUSHER_EXTEND_PULSE;
    else if (pos == "retract") pulse = PUSHER_RETRACT_PULSE;
    else return "ERR expected_extend_or_retract\n";
    auto slaves = group_slaves_(group);
    if (slaves.empty()) return "ERR unknown_group\n";
    if (pusher_move_many_(slaves, pulse)) return "ERR pusher_fail\n";
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
        if (p < DETACH_THRESHOLD_X10) still_attached.push_back(s);
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
    std::vector<int> all9 = {ZDT_LF1, ZDT_LF2, ZDT_LB1, ZDT_LB2,
                             ZDT_RF1, ZDT_RF2, ZDT_RB1, ZDT_RB2, ZDT_C};
    if (pusher_move_many_(all9, PUSHER_RETRACT_PULSE))
        return fail("ERR pusher_retract_fail\n");
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
