#pragma once
// ==========================================================
//  DamiaoAPI -- dual-motor init + background TCP command server
//
//  Minimal usage:
//      DamiaoAPI api;
//      if (!api.init("COM10", 921600u,
//              {damiao::DM10010L,   0x01, 0x11},   // M1
//              {damiao::DM4340_48V, 0x02, 0x22}))  // M2
//          return 1;
//      api.start();
//      api.stop();
//
//  TCP commands (line-terminated with \n or \r\n):
//      All commands require M1 or M2 prefix (space-separated).
//
//      M1 / M2 (both):
//        ENABLE                              -> OK
//        DISABLE                             -> OK
//        ZERO                                -> OK
//        STATUS                              -> pos=X vel=Y tau=Z hold=0/1 moving=0/1
//        MIT <kp> <kd> <q> <dq> <tau>       -> OK
//        MODE <1-7>                          -> OK / FAIL
//        PARAM <reg_id>                      -> <value>
//        HOME                                -> OK
//        HOLD                                -> OK
//        UNHOLD                              -> OK
//        MOVETO <rad> [speed_rad_s]          -> OK target=X speed=Y
//        MOVING                              -> 0 / 1
//
//      M1 only (大馬達):
//        SETWALL <mm>                        -> OK  (0 = no limit)
//        APPROACH <clearance_mm> [speed_rad_s] -> OK clearance=X speed=Y
//        TOUCHWALL <wall_mm> <LEFT|CENTER|RIGHT> [clearance_mm>=0] [speed] -> OK wall=X slot=Y clearance=Z speed=W [warn=SETWALL_MAY_LIMIT]
//        CALIBRATE                           -> OK  (push negative to stop, set zero)
//
//      M2 only (小馬達左右軸):
//        LR_CALIBRATE [LEFT|RIGHT]           -> OK  (default: LEFT)
//        LR_SLOT <LEFT|CENTER|RIGHT> [spd]   -> OK slot=X speed=Y
//
//      SYS (無 M1/M2 前綴):
//        INIT                                              -> OK
//        DEPLOY <wall_mm> <LEFT|CENTER|RIGHT> [clr_mm] [spd]  -> OK wall=X slot=Y clearance=Z [warn=SETWALL_MAY_LIMIT]
//        PARK                                              -> OK
// ==========================================================

#include "damiao.h"

#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

// ---- cross-platform socket alias ----------------------------------------
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef SOCKET socket_t;
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <arpa/inet.h>
#  include <unistd.h>
   typedef int socket_t;
#  define INVALID_SOCKET (-1)
#  define SOCKET_ERROR   (-1)
   inline int closesocket(int s) { return ::close(s); }
#endif
// -------------------------------------------------------------------------

class DamiaoAPI
{
public:
    using CommandHandler = std::function<std::string(const std::string& cmd_line)>;

    // ---- motor configuration (for init) -------------------------------------
    struct MotorConfig {
        damiao::DM_Motor_Type type;
        uint32_t slave_id;
        uint32_t master_id;
    };

    DamiaoAPI()  = default;
    ~DamiaoAPI() { stop(); }

    // ------------------------------------------------------------------
    bool init(const char*   port,
#ifdef _WIN32
              uint32_t      baud,
#else
              speed_t       baud,
#endif
              MotorConfig   m1,
              MotorConfig   m2,
              int           tcp_port = 9527);

    void start();
    void stop();

    void registerCommand(const std::string& key, CommandHandler handler);

    // ---- arm geometry constants (M1 / large motor) --------------------------
    static constexpr float ARM_LENGTH_MM       = 320.0f;
    static constexpr float PASSIVE_EXT_MM      = 86.46f;
    static constexpr float VERTICAL_OFFSET_RAD = 0.38f;

    // M2-slot-specific tool extension beyond the passive joint (mm)
    static constexpr float TOOL_EXT_LEFT_MM   = 148.09f;
    static constexpr float TOOL_EXT_CENTER_MM = 160.00f;
    static constexpr float TOOL_EXT_RIGHT_MM  = 134.07f;

    // ---- M2 calibrate velocity buffer ---------------------------------------
    static constexpr float LR_VEL_BUFFER_K  = 0.05f;   // back-off += K * vel_at_first_resist

    // ---- M2 / small motor constants (左右軸) --------------------------------
    static constexpr float ZERO_OFFSET = 0.8f;   // M2 only: calibration back-off / lr slot offset

    // ---- M1 gravity feedforward (0 = disabled) ------------------------------
    static constexpr float ARM_MASS_KG = 0.0f;    // effective arm mass; fill in after measurement

    // ---- direct motor control (C++ API, default targets M2) -----------------
    void  enable();
    void  disable();
    void  set_zero();
    bool  switch_mode(damiao::Control_Mode mode);
    void  control_mit(float kp, float kd, float q, float dq, float tau);
    void  go_home();
    void  hold_position();
    void  release_hold();
    bool  is_holding() const;

    // ---- M2 trajectory (左右軸) ---------------------------------------------
    void  lr_calibrate(bool seek_left = true);
    bool  lr_move_to_slot(int slot, float speed_rad_s = 0.4f);   // 2026-06-06: returns true on converge, false on timeout

    // ---- M1 trajectory (大馬達臂) -------------------------------------------
    bool  calibrate_arm();                                          // push negative to stop, set zero; false = stop not found
    void  set_wall_distance(float mm);                              // 0 = no limit
    bool  approach_wall(float clearance_mm, float speed_rad_s = 0.3f);
    // touch_wall: move M1 so slot-specific tool tip is at clearance_mm from wall.
    // m2_slot: -1=LEFT  0=CENTER  +1=RIGHT
    // If SETWALL is active with a smaller wall_dist, move_to_slot may silently
    // clamp theta_target; call 'M1 SETWALL 0' first to disable the safety limit.
    bool  touch_wall(float wall_dist_mm, int m2_slot,
                     float clearance_mm = 0.0f, float speed_rad_s = 0.3f);

    // ---- shared trajectory --------------------------------------------------
    void  move_to(float target_rad, float speed_rad_s = 0.3f);     // smooth move (default: M2)
    bool  is_moving() const;

    float get_position() const;
    float get_velocity() const;
    float get_torque()   const;

    // ---- access underlying objects ------------------------------------------
    damiao::Motor&         m1_motor() { assert(m1_.motor); return *m1_.motor; }
    damiao::Motor&         m2_motor() { assert(m2_.motor); return *m2_.motor; }
    damiao::Motor_Control& ctrl()     { assert(dm_); return *dm_; }

private:
    // ---- per-motor state ----------------------------------------------------
    struct MotorSlot {
        enum class SlotId { M1, M2 } id = SlotId::M2;
        std::string name;
        std::unique_ptr<damiao::Motor> motor;
        float lower_bound { 0.0f };    // move_to lower clamp: M1=0.0, M2=-ZERO_OFFSET
        float upper_bound { 1e9f };    // hard upper clamp: M1=1.2 rad, M2=unconstrained

        float hold_kp { 5.0f };       // per-slot MIT hold/move gain
        float hold_kd { 1.0f };

        std::atomic<bool> enabled  { false };
        std::atomic<bool> hold_en  { false };
        std::atomic<bool> move_act { false };

        float hold_pos    { 0.0f };
        float move_target { 0.0f };
        float move_cur    { 0.0f };
        float move_speed  { 0.3f };
        float move_tau_ff { 0.0f };   // tau captured at hold→move transition; gravity proxy
        float hold_tau_ff { 0.0f };   // gravity proxy for hold; set at HOLD or move→hold
        float wall_dist   { 0.0f };   // M1: SETWALL value; M2: always 0

        float hold_ki          { 0.0f };   // integral gain; 0 = disabled
        float hold_err_integral{ 0.0f };   // integrator state (protected by motor_mutex_)
        static constexpr float HOLD_I_MAX = 2.0f;   // anti-windup clamp (N·m)
    };

    // ---- private slot operations --------------------------------------------
    void        enable_slot(MotorSlot& s);
    void        disable_slot(MotorSlot& s);
    void        set_zero_slot(MotorSlot& s);
    void        go_home_slot(MotorSlot& s);
    void        hold_slot(MotorSlot& s);
    void        release_hold_slot(MotorSlot& s);
    void        move_to_slot(MotorSlot& s, float target_rad, float speed_rad_s);
    bool        approach_wall_slot(MotorSlot& s, float clearance_mm, float speed_rad_s);
    bool        touch_wall_slot(MotorSlot& s, float wall_dist_mm, int m2_slot,
                                float clearance_mm, float speed_rad_s);
    void        lr_calibrate_slot(MotorSlot& s, bool seek_left);
    bool        lr_move_to_slot_impl(MotorSlot& s, int slot, float speed_rad_s);   // 2026-06-06: true=converged
    bool        calibrate_arm_slot(MotorSlot& s);

    // ---- compound sequences (blocking -- run on client_thread) --------------
    std::string cmd_init_sequence();
    std::string cmd_deploy_sequence(const std::string& params);
    std::string cmd_park_sequence();
    std::string cmd_status_sequence();

    // ---- move-completion poll helper (true=done; false=timeout) -------------
    static bool wait_for_move(MotorSlot& s, int timeout_ms = 15000);

    // ---- TCP internals ------------------------------------------------------
    void        server_loop();
    void        client_thread(socket_t client_sock);
    void        feedback_loop();
    std::string dispatch(const std::string& line);
    std::string dispatch_motor(MotorSlot& s, const std::string& cmd_line);

    // ---- underlying objects -------------------------------------------------
    std::shared_ptr<SerialPort>            serial_;
    std::unique_ptr<damiao::Motor_Control> dm_;

    // ---- motor slots --------------------------------------------------------
    MotorSlot m1_;
    MotorSlot m2_;

    // ---- TCP ----------------------------------------------------------------
    int               tcp_port_    = 9527;
    socket_t          listen_sock_ = INVALID_SOCKET;
    std::atomic<bool> running_     { false };
    std::thread       server_thread_;
    std::thread       feedback_thread_;

    // ---- custom command table -----------------------------------------------
    std::unordered_map<std::string, CommandHandler> cmd_map_;

    // ---- serial port lock (shared by both motors) ---------------------------
    mutable std::mutex motor_mutex_;

#ifdef _WIN32
    bool wsa_ok_ = false;
#endif
};
