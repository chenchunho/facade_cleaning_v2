#ifndef WASH_ROBOT_H
#define WASH_ROBOT_H

#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <cstdint>

#include "TCP_client.h"
#include "DM2J_RS570.h"
#include "ZDT_motor_control.h"
#include "JC_100_METER.h"
#include "PQW_IO_16O_RLY.h"
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
    std::string cmd_step_down();
    std::string cmd_run(int steps);
    std::string cmd_arm_sweep();  // public: acquires motion_mtx_
    std::string cmd_tilt_mode(bool on);
    std::string cmd_emergency_stop();
    std::string cmd_shutdown();
    std::string cmd_status();
    std::string cmd_vacuum(const std::string& group, bool on);
    std::string cmd_pusher(const std::string& group, const std::string& pos);
    std::string cmd_move(const std::string& motor, double cm);
    std::string cmd_confirm_balance(const std::string& ans);
    std::string cmd_return_home(int descent_cm);
    std::string cmd_reset();
    std::string cmd_ping();
    std::string cmd_pause();
    std::string cmd_resume();

    //=========== state ===========

    enum class State {
        Idle,            // post-init, awaiting cmd_init (Phase 2)
        Ready,           // Phase 2 done, awaiting attach
        Attached,        // Phase 3 done, 9 cups holding
        Running,         // Phase 4 step_down / run in progress
        WaitingConfirm,  // balance_ask fired, awaiting confirm_balance
        Paused,          // user-paused during Running / Balancing
        Balancing,       // Phase 5 roll correction running
        ReturningHome,   // Phase 6 return_home running
        Error            // hard fault — only status / ping / reset / return_home allowed
    };

    State get_state() const { return state_.load(); }
    static const char* state_name(State s);

private:
    //=========== constants ===========

    static constexpr const char* IP_485_1   = "192.168.1.20";
    static constexpr const char* IP_485_2   = "192.168.1.21";
    static constexpr const char* IP_485_3   = "192.168.1.22";
    static constexpr int         PORT_485   = 4001;

    // [TEST MODE 2026-04-21] CRANE_IP changed from "192.168.1.101" → "192.168.5.26"
    // because crane_shim.py is co-located on the easy crane Pi (.5.26) during testing.
    // REVERT to "192.168.1.101" when main crane (Crane_control_PI) deploys to its own Pi.
    // See .claude/easy_crane_test_mode.md §9a 撤除清單.
    static constexpr const char* CRANE_IP   = "192.168.5.26";
    static constexpr int         CRANE_PORT = 5002;

    // PQW relay channels (slave 12, 8CH)
    static constexpr int PQW_SLAVE       = 12;
    static constexpr int PQW_TOTAL_CH    = 8;
    static constexpr int CH_PUMP         = 1;  // dp0105 vacuum pump (always ON while running)
    static constexpr int CH_VALVE_FEET   = 2;  // VT307 feet suction cups
    static constexpr int CH_VALVE_BODY   = 3;  // VT307 body suction cups
    static constexpr int CH_VALVE_CENTER = 4;  // VT307 center suction cup
    static constexpr int CH_BRUSH        = 5;  // arm roller brush motor
    static constexpr int CH_WATER_PUMP   = 6;  // water tank pump (spray)
    static constexpr int CH_WATER_INLET  = 7;  // water inlet ball valve (tank refill from rooftop)

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
    static constexpr int DM2J_LEFT_FOOT   = 1;
    static constexpr int DM2J_LEFT_WHEEL  = 2;
    static constexpr int DM2J_RIGHT_FOOT  = 3;
    static constexpr int DM2J_RIGHT_WHEEL = 4;
    static constexpr int DM2J_ARM         = 5;

    // Pusher motion
    static constexpr int PUSHER_EXTEND_PULSE  = 144000;
    static constexpr int PUSHER_RETRACT_PULSE = 0;
    static constexpr int PUSHER_RPM           = 1000;
    static constexpr int PUSHER_ACC           = 255;
    static constexpr int PUSHER_SETTLE_MS     = 1500;

    // Step parameters
    static constexpr int STEP_CM          = 30;
    static constexpr int STEP_MARGIN_CM   = 15;   // crane extra slack before feet move
    static constexpr int TOTAL_DISTANCE_CM = 30;  // TODO: set actual building height

    // Crane watchdog
    static constexpr int HEARTBEAT_INTERVAL_MS = 500;
    // [TEST MODE 2026-04-21] bumped 2000 → 60000 because crane_shim.py on :5002 does
    // open-loop timed pay_out/retract (e.g. 45cm @ 3cm/s = 15s) which holds crane_mtx_
    // longer than the normal 2s watchdog. Main crane (Crane_control_PI) is fast enough
    // that 2000ms is correct. REVERT to 2000 when main crane is online.
    // See .claude/easy_crane_test_mode.md §9 撤除清單.
    static constexpr int WATCHDOG_TIMEOUT_MS   = 60000;

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
    static constexpr int DM2J_RPM = 700;
    static constexpr int DM2J_ACC = 100;
    static constexpr int DM2J_DEC = 100;

    // Arm sweep
    static constexpr int ARM_SWEEP_CM  = 30;
    static constexpr int ARM_SWEEP_RPM = 500;

    // Vacuum
    static constexpr int VACUUM_RETRY_MAX     = 5;
    static constexpr int VACUUM_THRESHOLD_X10 = -500;  // 0.1 kPa; -50 kPa — below = attached
    static constexpr int DETACH_THRESHOLD_X10 = -100;  // 0.1 kPa; -10 kPa — above = detached
    static constexpr int VACUUM_SETTLE_MS     = 2000;
    static constexpr int POLL_INTERVAL_MS     = 50;
    static constexpr double VACUUM_BACKUP_CM  = 5.0;   // rail backup on each vacuum retry

    static constexpr int RETURN_VACUUM_RELEASE_MS = 5000;  // wait after valves off before retracting pushers

    //=========== hardware ===========

    TCP_client cli_20_, cli_21_, cli_22_;
    TCP_client crane_cli_;

    DM2J_RS570        dm2j_[5];   // index 0..4 → slave 1..5
    ZDT_motor_control zdt_[9];   // index 0..8 → slave 1..9
    JC_100_METER      meter_[9]; // index 0..8 → slave 1..9
    PQW_IO_16O_RLY    pqw_;

    Serial_port  imu_serial_;
    WT901BC_TTL  imu_;

    //=========== state ===========

    std::atomic<bool>    motion_active_;
    std::mutex           motion_mtx_;

    std::mutex           crane_mtx_;
    std::atomic<bool>    crane_wd_running_;
    std::atomic<int64_t> crane_last_ok_ms_;
    std::thread          crane_wd_thread_;

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

    //=========== utility ===========

    DM2J_RS570&        D_(int slave) { return dm2j_[slave - 1]; }
    ZDT_motor_control& Z_(int slave) { return zdt_[slave - 1]; }
    JC_100_METER&      M_(int slave) { return meter_[slave - 1]; }

    static int64_t now_ms_();
    static void    sleep_ms_(int ms);
    void           evt_(const std::string& msg);
    bool           dm2j_wait_done_(int slave, int timeout_ms = 10000);
    bool           check_abort_();

    void        set_state_(State s);   // atomic + EVT state_changed
    std::string state_violation_(State cur) const;
    std::string do_step_down_();        // internal: no state guard, caller handles transition

    //=========== crane ===========

    bool        crane_connect_if_needed_();
    std::string crane_cmd_(const std::string& line, int timeout_sec = 30);
    void        crane_watchdog_loop_();

    //=========== IMU ===========

    bool        imu_take_baseline_();
    std::string do_phase5_roll_correct_();
    void        imu_monitor_loop_();

    //=========== arm ===========

    std::string do_arm_sweep_();  // internal: caller must hold motion_mtx_

    //=========== pusher / vacuum ===========

    bool             pusher_move_(int slave, int pulse);
    bool             pusher_move_many_(const std::vector<int>& slaves, int pulse);
    static std::vector<int> group_slaves_(const std::string& group);
    static int              group_valve_ch_(const std::string& group);
    bool             vacuum_valve_(const std::string& group, bool on);
    std::vector<int> vacuum_check_(const std::string& group);

    // Core motion cycle — template body must be visible at call site (defined below)
    template <typename PreCycle, typename Backup>
    std::string cycle_group_(const std::string& group,
                             PreCycle  pre_cycle,
                             Backup    backup,
                             int&      out_retry_count);
};

// ---- cycle_group_ template definition ----
// Phase 4 vacuum attach cycle with backup-on-retry.
//
//   pre_cycle : () -> string   called ONCE before first attempt (crane + DM2J large move)
//   backup    : () -> string   called BEFORE each retry (DM2J small 5cm reverse move)
//
// Flow:
//   pre_cycle()                       e.g. feet: crane pay_out + DM2J abs STEP_CM + crane retract
//   for attempt = 0 .. VACUUM_RETRY_MAX:
//     if attempt > 0:
//       release valve + retract pushers + backup()
//     extend pushers + valve ON + verify
//     if OK: out_retry_count = attempt; return ""
//   return "vacuum_retry_exceeded <group>"
//
template <typename PreCycle, typename Backup>
std::string WashRobot::cycle_group_(const std::string& group,
                                    PreCycle  pre_cycle,
                                    Backup    backup,
                                    int&      out_retry_count) {
    const int  valve_ch = group_valve_ch_(group);
    const auto slaves   = group_slaves_(group);

    // 1. Pre-cycle (once): crane + DM2J large move
    {
        std::string perr = pre_cycle();
        if (!perr.empty()) return perr;
    }
    if (check_abort_()) return "aborted";

    for (int attempt = 0; attempt <= VACUUM_RETRY_MAX; ++attempt) {
        if (attempt > 0) {
            // Retry: release valve + retract pushers + small backup
            if (pqw_.controlRelay(valve_ch, false)) return "valve_off_fail";
            sleep_ms_(300);
            if (pusher_move_many_(slaves, PUSHER_RETRACT_PULSE)) return "pusher_retract_fail";
            if (check_abort_()) return "aborted";

            std::string berr = backup();
            if (!berr.empty()) return berr;
            if (check_abort_()) return "aborted";
        }

        // Extend + valve ON + settle + verify
        if (pusher_move_many_(slaves, PUSHER_EXTEND_PULSE)) return "pusher_extend_fail";
        if (pqw_.controlRelay(valve_ch, true))              return "valve_on_fail";
        sleep_ms_(VACUUM_SETTLE_MS);

        auto fails = vacuum_check_(group);
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
