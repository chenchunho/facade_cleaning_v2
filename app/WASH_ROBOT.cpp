#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "WASH_ROBOT.h"
#include "endpoints.h"

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
    , imu_guard_enabled_(true)   // [2026-08-27] 預設開啟；只有操作者明確關閉才會停用
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
    settings_.pusher_rpm_disable_slow        .store(PUSHER_RPM_DISABLE_SLOW);
    settings_.disable_phase_current_limit_ma .store(DISABLE_PHASE_CURRENT_LIMIT_MA);
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
    // [v2 2026-07-08] Two RS485 gateways remain: .20 hosts the ZDT pushers
    // (1-4, moved here from the retired .21 bus — .20 was freed when the DM2J
    // feet/wheel rails were removed); .22 hosts JC100 / PQW / arm-rail / XKC /
    // DY500. The v1 .21 gateway (cli_21_) is physically gone.
    // [2026-08-27 per user] PQW relay 搬到 .20（cli_22_ → cli_20_）。現況：
    //   .20 (cli_20_): ZDT 1-4, PQW 12
    //   .22 (cli_22_): JC100 1-4, QX PWM 6, DY500 10-11, XKC 13, DM2J arm-rail 14
    // 兩條 bus 的 slave ID 各自唯一，無衝突。
    // ⚠ 注意：檔案裡還有數十處註解沿用舊配置在描述 bus 競爭（例如「cli_22_ bus
    // 有 JC100/PQW 競爭」）。PQW 已不在 cli_22_，那些敘述關於 PQW 的部分已過時；
    // JC100 的部分仍然成立。真正的競爭關係以本段為準。
    // [2026-08-29] 端點先解析成區域變數再用：連線與訊息必須引用**同一個值**。
    // 原本訊息印的是編譯期常數，而連線走的是解析後的端點 —— 一旦有 override，
    // 「連 X 失敗」會指著一個根本沒被連過的位址。本專案最常踩的就是這個形狀。
    const std::string ep_usr20 = ep::host("USR20", IP_485_1);
    const int         pt_usr20 = ep::port("USR20", PORT_485);
    if (!cli_20_.connectToServer(ep_usr20, pt_usr20)) {
        std::cerr << "[WashRobot] connect " << ep_usr20 << ":" << pt_usr20 << " fail\n"; return true;
    }
    const std::string ep_usr22 = ep::host("USR22", IP_485_3);
    const int         pt_usr22 = ep::port("USR22", PORT_485);
    if (!cli_22_.connectToServer(ep_usr22, pt_usr22)) {
        std::cerr << "[WashRobot] connect " << ep_usr22 << ":" << pt_usr22 << " fail\n"; return true;
    }
    std::cout << "[OK] USR .20 (ZDT) / .22 (sensors+PQW) connected\n";

    // [TEST MODE 2026-04-21] driver debug=true by default for on-site troubleshooting.
    // Revert to `false` default when main crane is online.
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

    // [v2] DM2J feet/wheel rails removed. Only the arm-cleaning slide rail
    // (DM2J_ARM slave 14) remains.
    //
    // [2026-08-28 per user] cli_22_ → cli_20_：上滑台實體接在 192.168.1.20，
    // 程式卻一直對 .22 發指令，所以每一次掃動都是
    //     [DBG] PR_move_cm_nowait 17.000 cm -> 170000 pulses
    //     [ERR] writeMulti no response          ← 發到沒有這顆裝置的 gateway
    // 重試 3 次全滅，然後流程照樣印「rail sweep done」（fire-and-forget 不看結果）。
    // .20 上目前只有 ZDT 推桿 5~8 與 PQW 12，slave 14 是空的，不撞號。
    //
    // ⚠ 副作用：上滑台從此跟 ZDT 推桿共用同一條 bus，而 rail sweep 是背景執行緒、
    //   與主執行緒的伸腳並行。TCP_client::socket_mtx 保證幀不交錯（不會壞封包），
    //   且 arm_sweep_fire_nowait_ 是 fire-and-forget、arm_monitor_during_sweep_
    //   已短路成純 sleep（不讀 status），所以佔用很短，只是時序略慢。
    //   注意 pusher_two_stage_retract_ 持有的是 zdt_bus_mtx_，DM2J 不拿那把鎖 ——
    //   兩者靠 socket_mtx 序列化，安全但不互斥。
    if (D_(DM2J_ARM).init(cli_20_, DM2J_ARM, dbg)) {
        std::cerr << "[FATAL] DM2J arm rail (slave " << DM2J_ARM << " @ cli_20_) init fail\n";
        return true;
    }
    // [2026-08-28] 機構標定必須緊接在 init 之後、任何移動之前 —— 漏掉這兩行，
    // 每個 cm 指令就會走 7.7 倍並一路撞到行程底，而且不會有任何錯誤訊息。
    D_(DM2J_ARM).set_lead_cm_per_rev(ARM_RAIL_LEAD_CM_PER_REV);
    D_(DM2J_ARM).set_travel_limit_cm(0.0, ARM_RAIL_TRAVEL_MAX_CM);
    std::cout << "[OK] DM2J arm rail (slave " << DM2J_ARM << " @ cli_20_)"
              << " lead=" << ARM_RAIL_LEAD_CM_PER_REV << " cm/rev"
              << " travel<=" << ARM_RAIL_TRAVEL_MAX_CM << " cm\n";

    // ZDT slave 5..8 on cli_20_ ([v2] 4 cups: right{5,7} / left{6,8}，2026-08-28 修正)
    // [2026-08-27 per user] slave 1-4 → 5-8，見 WASH_ROBOT.h CUP_SLAVE_FIRST。
    for (int i = CUP_SLAVE_FIRST; i <= CUP_SLAVE_LAST; ++i) {
        if (Z_(i).init(cli_20_, i, dbg)) {
            std::cerr << "[FATAL] ZDT slave " << i << " init fail\n"; return true;
        }
    }
    std::cout << "[OK] ZDT " << CUP_SLAVE_FIRST << "~" << CUP_SLAVE_LAST << "\n";

    // JC-100 slave 5..8 ([v2] one vacuum-pressure sensor per cup)
    // 與 ZDT 同號（推桿 slave N 末端的吸盤 = 真空表 slave N），分屬 .20/.22 兩條 bus，不衝突。
    for (int i = CUP_SLAVE_FIRST; i <= CUP_SLAVE_LAST; ++i) {
        if (M_(i).init(cli_22_, i, dbg)) {
            std::cerr << "[FATAL] JC-100 slave " << i << " init fail\n"; return true;
        }
    }
    std::cout << "[OK] JC-100 " << CUP_SLAVE_FIRST << "~" << CUP_SLAVE_LAST << "\n";

    // PQW 8CH relay
    // [2026-08-27 per user] cli_22_ → cli_20_（relay 搬到 .20，見 init() 開頭說明）。
    // .20 上原本只有 ZDT 1-4，PQW 是 slave 12，不撞號。
    if (pqw_.init(cli_20_, PQW_SLAVE, PQW_TOTAL_CH, dbg)) {
        std::cerr << "[FATAL] PQW slave " << PQW_SLAVE << " init fail (cli_20_ / .20)\n"; return true;
    }
    std::cout << "[OK] PQW slave " << PQW_SLAVE << " @ cli_20_ (.20)\n";

    // XKC-Y25 water level sensor (slave 13, same bus as PQW/JC100/DY500)
    // Mode B init does no probe — first read in cmd_arm_clean_sweep will catch
    // physical absence (and PausedOnError per design).
    lvl_.init(cli_22_, XKC_SLAVE, dbg);
    std::cout << "[OK] XKC water level slave " << XKC_SLAVE << " (sensor presence not probed)\n";

    // QX-DO24 PWM output (slave 6, same bus). Mode B init does no probe, so a
    // missing module is only discovered on the first pwm command — that's fine
    // here because nothing in the automatic gait depends on it (web panel only).
    // PWM_ENABLED 為 false 時連 init 都不做：Mode B init 只是記下 client+ID 不發包，
    // 但不 init 就能保證任何漏掉 gate 的呼叫路徑也發不出東西到那個 slave 號上。
    // （2026-08-27 曾因 slave 撞 JC100 而停用；2026-08-28 模組改 slave 9 後解除，
    //   沿革見 WASH_ROBOT.h 的 PWM_ENABLED 註解。）
    if (PWM_ENABLED) {
        pwm_.init(cli_22_, PWM_SLAVE, dbg);
        std::cout << "[OK] QX-DO24 PWM slave " << PWM_SLAVE << " (presence not probed)\n";
    } else {
        std::cout << "[--] QX-DO24 PWM DISABLED (PWM_ENABLED=false) — slave "
                  << PWM_SLAVE << " 不會收到任何封包（見 WASH_ROBOT.h PWM_ENABLED 註解）\n";
    }

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
    for (int s = CUP_SLAVE_FIRST; s <= CUP_SLAVE_LAST; ++s)
        last_seal_pulse_[s - 1].store(preset_extend_pulse_for_slave_(s));
    last_feet_max_over_cm_.store(0.0);
    cached_weight_kg_[0].store(-1.0);
    cached_weight_kg_[1].store(-1.0);
    weight_comm_ok_[0].store(false);
    weight_comm_ok_[1].store(false);

    // Crane (lazy — don't fail boot if crane is down)
    const std::string ep_crane = ep::host("CRANE", CRANE_IP);
    const int         pt_crane = ep::port("CRANE", CRANE_PORT);
    if (crane_connect_if_needed_())
        std::cerr << "[WARN] crane " << ep_crane << ":" << pt_crane << " not yet reachable\n";
    else
        std::cout << "[OK] crane " << ep_crane << ":" << pt_crane << "\n";

    // [2026-06-03] Arm (lazy — same pattern as crane). Required to bootstrap
    // TCP_client.reconnectLoop() background thread — startMonitor() only fires
    // after the first connectToServer() call. Before this explicit init, arm_cmd_
    // would never have a live connection (we removed manual connectToServer()
    // from arm_cmd_ — it now relies entirely on the background thread).
    // [2026-07-23 per user] Arm isn't physically mounted yet — mute the
    // reconnect-loop's "reconnecting/reconnect success" spam for this
    // connection specifically (still reconnects normally underneath, this
    // only quiets the log). Remove once the arm is actually installed and its
    // connection health is worth watching again.
    arm_cli_.set_quiet_reconnect_log(true);
    const std::string ep_arm = ep::host("ARM", ARM_IP);
    const int         pt_arm = ep::port("ARM", ARM_PORT);
    if (!arm_cli_.connectToServer(ep_arm, pt_arm))
        std::cerr << "[WARN] arm " << ep_arm << ":" << pt_arm << " not yet reachable\n";
    else
        std::cout << "[OK] arm " << ep_arm << ":" << pt_arm << "\n";

    // [2026-07-20] Depth camera (D435i) obstacle-detection service — same
    // lazy-connect pattern as arm_cli_ (don't fail boot if the python service
    // isn't running yet; depth_cam_cmd_ relies on the background reconnect).
    // [2026-08-27 per user] 靜音重連 log（同 arm_cli_ 的處置）。bench 觀察到
    // 每 500ms 一組 "reconnecting → reconnect success" 無限洗版，把其他訊息
    // 全部沖掉。TCP_client::available() 是回 -1（r==0，對方送 FIN）才觸發重連，
    // 也就是 depth_cam_service.py 那端會主動關閉連線，不是本地誤判。
    // 這條連線目前也沒有任何用途——depth_cam_cmd_ 只被 cmd_run_depth_avoid 使用，
    // 而該功能的 GUI 已於 2026-08-26 全數移除。保留連線本身（不刪 connectToServer）
    // 是為了將來要用 depth avoid 時不必再改；只是不再吵。
    depth_cli_.set_quiet_reconnect_log(true);
    const std::string ep_depth = ep::host("DEPTHCAM", DEPTH_CAM_IP);
    const int         pt_depth = ep::port("DEPTHCAM", DEPTH_CAM_PORT);
    if (!depth_cli_.connectToServer(ep_depth, pt_depth))
        std::cerr << "[WARN] depth_cam " << ep_depth << ":" << pt_depth << " not yet reachable\n";
    else
        std::cout << "[OK] depth_cam " << ep_depth << ":" << pt_depth << "\n";

    // IMU (Serial_port::init returns true = success, unlike project convention)
    const std::string ep_imu = ep::path("IMU", IMU_PORT);
    if (!imu_serial_.init(ep_imu, IMU_BAUD)) {
        std::cerr << "[FATAL] IMU serial " << ep_imu << " open fail\n"; return true;
    }
    imu_.init(&imu_serial_, dbg);   // [TEST MODE] default dbg=true; WR_DRIVER_DEBUG=0 disables
    sleep_ms_(500);
    if (imu_.read_error.load())
        std::cerr << "[WARN] IMU read error on startup\n";
    else
        std::cout << "[OK] IMU " << IMU_PORT
                  << " roll=" << imu_.z << " pitch=" << imu_.x << "\n";

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

    // Safe startup: ensure all relays off ([v2] channels)
    //pqw_.controlRelay(CH_BRUSH,       false);
    //pqw_.controlRelay(CH_WATER_PUMP,  false);
    //pqw_.controlRelay(CH_PUMP,        false);
    //pqw_.controlRelay(CH_VALVE_RIGHT, false);
    //pqw_.controlRelay(CH_VALVE_LEFT,  false);

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
    return !crane_cli_.connectToServer(ep::host("CRANE", CRANE_IP), ep::port("CRANE", CRANE_PORT));
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
        if (got_reply) {
            // [2026-07-14] Log non-OK replies — callers only check rfind("OK",0)
            // and discard the actual reason (e.g. ERR motion_busy / tension_... /
            // meter_..._lost), making failures like PAUSE-ON-ERROR loops
            // undiagnosable from the log. This is the one place that sees every
            // reply regardless of call site.
            if (reply_line.rfind("OK", 0) != 0)
                std::cout << "[crane_cmd] '" << line << "' -> " << reply_line << "\n";
            return reply_line;
        }
        // recv loop exhausted without a non-EVT reply → consider this attempt
        // failed. Loop iteration ends → for loop tries attempt 1 (force reconnect).
    }
    std::cout << "[crane_cmd] '" << line << "' FAILED after 2 attempts (no reply)\n";
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

// [2026-07-20] Mirrors arm_cmd_ exactly — see that function's comments for
// the reconnect/retry rationale. BEFORE/AFTER are idempotent reads (no motion
// side effect), but kept the same "no retry on recv timeout" shape as arm_cmd_
// for consistency rather than introducing a different retry policy here.
std::string WashRobot::depth_cam_cmd_(const std::string& line, int timeout_sec) {
    std::lock_guard<std::mutex> lk(depth_mtx_);

    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto conn_deadline = std::chrono::steady_clock::now()
                                 + std::chrono::milliseconds(1500);
        while (!depth_cli_.isConnected()
               && std::chrono::steady_clock::now() < conn_deadline) {
            sleep_ms_(100);
        }
        if (!depth_cli_.isConnected()) {
            std::cout << "[depth_cam_cmd] '" << line
                      << "' not connected attempt=" << attempt
                      << " (waiting for background reconnect)\n";
            if (attempt == 1) return "";
            continue;
        }

        std::string tx = line;
        if (tx.empty() || tx.back() != '\n') tx.push_back('\n');
        if (!depth_cli_.sendData(tx.c_str(), (int)tx.size(), 1000)) {
            std::cout << "[depth_cam_cmd] '" << line
                      << "' send fail attempt=" << attempt << "\n";
            continue;
        }

        std::string rx;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
        char buf[512];
        while (std::chrono::steady_clock::now() < deadline) {
            int n = depth_cli_.receiveData(buf, sizeof(buf), 500);
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
        std::cout << "[depth_cam_cmd] '" << line
                  << "' recv timeout attempt=" << attempt << "\n";
        return "";
    }
    return "";
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

// [2026-07-20] D435i depth-camera continuous obstacle-avoid walk (v2).
//
// NOTE on why this does NOT use do_step_down_'s after_feet_rail_hook /
// before_feet_rail_hook / during_body_rail_hook / after_body_rail_hook
// parameters (unlike v1's cmd_run_avoid, which piggybacked on the DM2J
// rail's ~80%/100%-of-motion timing): v2's do_step_down_ has no DM2J rail
// at all (crane rope pay-out per side instead) and explicitly casts all
// four hook params to (void) — they are dead parameters in the current
// engine, not wired to anything. Re-wiring precise mid-motion timing into
// the crane-based engine (which blocks on crane_cmd_ until each side's
// pay_out completes, no natural "80% progress" callback) is a bigger,
// riskier change to a safety-critical function than this feature calls
// for. Instead: BEFORE is captured immediately before do_step_down_(),
// AFTER immediately after it returns OK — using the WHOLE step's real
// displacement as the parallax source. Trade-off: a large user-chosen
// step_cm (up to STEP_CM_MAX) moves the camera further than v1's ~20%-of-
// step probe did, so a sill could shift further out of the D435i's near-
// field FOV between BEFORE/AFTER on a big step. Accepted for now — see
// project memory (depth camera bench notes) for the FOV-vs-shift tension;
// revisit with a real mid-motion hook if big steps prove unreliable.
std::string WashRobot::cmd_run_depth_avoid() {
    State cur = state_.load();
    std::cout << "[depth_avoid] start (current state: " << state_name(cur) << ")\n";
    evt_("depth_avoid_start state=" + std::string(state_name(cur)));
    set_state_(State::Running);
    abort_flag = false;

    int next_step_cm = DEPTH_AVOID_FIRST_STEP_CM;   // fixed first step per user 2026-07-20 spec
    // [2026-07-23 per user] Auto cross-obstacle trigger: when an AFTER capture
    // sees candidates>0, the NEXT iteration automatically uses
    // do_cross_obstacle_ (one side stands off to 2×preset to clear the
    // protrusion) instead of the plain do_step_down_ — no confirm prompt,
    // fully automatic. Single-shot: consumed at the top of the loop below and
    // NOT re-armed by a step that was itself an auto-cross (see the re-arm
    // check further down) — a wide obstacle needing 2+ crosses is left to the
    // user to handle manually via continue, not auto-repeated.
    bool next_is_cross = false;
    int iter = 0;
    while (!abort_flag.load()) {
        iter++;
        // Consume the trigger now — this iteration's gait is locked in before
        // BEFORE/AFTER even run, so the flag can't be re-armed by this same
        // iteration's own detection result.
        const bool run_as_cross = next_is_cross;
        next_is_cross = false;
        std::cout << "[depth_avoid] iter " << iter << " step_cm=" << next_step_cm
                  << (run_as_cross ? " (AUTO cross_obstacle)" : "") << "\n";

        std::string br = depth_cam_cmd_("BEFORE");
        if (br.rfind("OK", 0) != 0) {
            std::cerr << "[depth_avoid] BEFORE capture failed: '" << br << "'\n";
            evt_("depth_avoid_detector_fail before_capture_failed:" + br);
            break;
        }

        step_cm_.store(next_step_cm);
        std::string sr;
        if (run_as_cross) {
            evt_("depth_avoid_cross_obstacle_step step_cm=" + std::to_string(next_step_cm));
            sr = do_cross_obstacle_(/*up=*/false);
        } else {
            // [2026-07-28 per user] 窗框避障走法改成跟 do_step_sync_ 一樣兩邊同動
            // （原本是 do_step_down_ 交替 inchworm 走法）。cross-obstacle 分支
            // （run_as_cross）維持不變，仍走 do_cross_obstacle_，只有一般 step
            // 這條改掉。
            evt_("depth_avoid_step_sync step_cm=" + std::to_string(next_step_cm));
            sr = do_step_sync_(/*up=*/false);
        }
        if (sr.rfind("OK", 0) != 0) {
            std::cerr << "[depth_avoid] " << (run_as_cross ? "cross_obstacle" : "step_down")
                      << " failed: " << sr;
            evt_("depth_avoid_step_fail " + sr);
            break;
        }

        // Longer timeout than the default 10s — the very first AFTER after a
        // step needs to wait out the machine settling + optical flow/plane
        // fit/connected-components work, not just a quick status round-trip.
        std::string ar = depth_cam_cmd_("AFTER", 15);
        if (ar.rfind("OK", 0) != 0) {
            std::cerr << "[depth_avoid] AFTER capture failed: '" << ar << "'\n";
            evt_("depth_avoid_detector_fail after_capture_failed:" + ar);
            break;
        }

        // Parse "OK candidates=<N> max_height_cm=<X.X> max_protrusion_cm=<Y.Y>
        // min_distance_cm=<Z.Z>" (find+substr+stod, matching this file's
        // existing reply-parsing style).
        int candidates = 0;
        double max_height_cm = 0.0, max_protrusion_cm = 0.0, min_distance_cm = 0.0;
        {
            auto cpos = ar.find("candidates=");
            auto hpos = ar.find("max_height_cm=");
            auto ppos = ar.find("max_protrusion_cm=");
            auto dpos = ar.find("min_distance_cm=");
            try { if (cpos != std::string::npos) candidates      = std::stoi(ar.substr(cpos + 11)); } catch (...) {}
            try { if (hpos != std::string::npos) max_height_cm     = std::stod(ar.substr(hpos + 14)); } catch (...) {}
            try { if (ppos != std::string::npos) max_protrusion_cm = std::stod(ar.substr(ppos + 18)); } catch (...) {}
            try { if (dpos != std::string::npos) min_distance_cm   = std::stod(ar.substr(dpos + 16)); } catch (...) {}
        }
        depth_last_candidates_.store(candidates);
        depth_last_max_height_cm_.store(max_height_cm);
        depth_last_max_protrusion_cm_.store(max_protrusion_cm);
        depth_last_min_distance_cm_.store(min_distance_cm);
        const bool big_obstacle = max_height_cm > DEPTH_BIG_OBSTACLE_HEIGHT_CM;

        // [2026-07-23 per user] Arm the auto cross-obstacle trigger for the
        // NEXT iteration when THIS AFTER saw any candidate at all (not gated
        // on big_obstacle — user wants candidates>0 to be the bar). Single-
        // shot: only re-arm if this step was a normal step; a step that was
        // itself the automatic cross doesn't re-trigger even if candidates
        // are still >0 afterward (wide obstacle needing 2+ crosses — left to
        // the user to drive manually via continue).
        if (!run_as_cross && candidates > 0) {
            next_is_cross = true;
            std::cout << "[depth_avoid] candidates=" << candidates
                      << " — next step will AUTO use cross_obstacle gait\n";
        }

        // [2026-07-21] Slant range -> along-travel remaining clearance (see
        // DEPTH_CAM_STANDOFF_CM/DEPTH_CAM_LEAD_OFFSET_CM comment in the
        // header for the geometry). min_distance_cm < standoff shouldn't
        // happen physically (camera can't see a point closer than its own
        // perpendicular height off the wall) — treat as 0 clearance rather
        // than NaN from sqrt of a negative.
        // [2026-07-22] No longer gated on candidates>0 — depth_cam_service.py
        // now reports min_distance_cm from the nearest blob overall (sill-
        // shaped candidate OR not), so a real nearby object still yields a
        // real remaining_travel_cm even on a step where nothing was
        // sill-shaped enough to become an official candidate.
        double remaining_travel_cm = 0.0;
        if (min_distance_cm > DEPTH_CAM_STANDOFF_CM) {
            const double horizontal_cm = std::sqrt(min_distance_cm * min_distance_cm
                                                     - DEPTH_CAM_STANDOFF_CM * DEPTH_CAM_STANDOFF_CM);
            remaining_travel_cm = horizontal_cm - DEPTH_CAM_LEAD_OFFSET_CM;
        }
        depth_last_remaining_travel_cm_.store(remaining_travel_cm);

        // [2026-07-22 per user, guard fixed 2026-07-23] Cross-obstacle step
        // suggestion — when remaining clearance is too tight for another
        // normal small step, suggest one bigger step that clears the whole
        // obstacle instead of creeping up to it: near-edge distance + the
        // obstacle's own along-travel thickness (max_height_cm; 0 if no real
        // candidate, which just makes this a smaller, still-reasonable
        // suggestion) + a full sucker diameter (next placement needs full
        // contact past the far edge, not straddling it) + a small safety
        // margin. Clamped to [STEP_CM_MIN, STEP_CM_MAX]. Suggestion only,
        // fills the GUI's default next-step field — the 2026-07-20 "user
        // decides every time" design is unchanged, this is just a better
        // starting number to edit from.
        //
        // [bug fixed 2026-07-23] Originally gated on `remaining_travel_cm >
        // 0.0` too, which excluded negative remaining (already at/past the
        // obstacle — the MOST urgent case) from the cross suggestion
        // entirely, silently falling back to the plain step_cm_ default.
        // The cross_cm formula handles negative remaining fine (sucker
        // diameter + margin dominate), so the >0.0 guard was wrong — dropped.
        // [2026-07-28 per user] Re-added a `candidates > 0` gate (removed by
        // 2026-07-22's "don't gate remaining_travel_cm on candidates>0"
        // change, which is UNRELATED and stays as-is — remaining_travel_cm
        // itself still reflects the nearest blob regardless of candidates).
        // This gate is only on the SUGGESTION formula below: user observed
        // candidates=0 (no real sill-shaped obstacle) still producing a
        // suggested_step_cm clamped down to STEP_CM_MIN — confusing since the
        // GUI shows "no candidate" yet suggests a tiny step. Now the
        // conservative cross-style suggestion only kicks in when there's an
        // actual candidate; candidates=0 always suggests the plain step_cm_
        // default regardless of how close the nearest (non-candidate) blob is.
        double suggested_step_cm = step_cm_.load();
        if (candidates > 0 && remaining_travel_cm < DEPTH_AVOID_LOW_CLEARANCE_CM) {
            const double cross_cm = remaining_travel_cm + max_height_cm
                                     + DEPTH_AVOID_SUCKER_DIAMETER_CM + DEPTH_AVOID_CROSS_MARGIN_CM;
            suggested_step_cm = std::min(std::max(cross_cm, (double)STEP_CM_MIN), (double)STEP_CM_MAX);
        }

        std::ostringstream evt_msg;
        evt_msg << "depth_obstacle_result iter=" << iter
                << " candidates=" << candidates
                << " max_height_cm=" << std::fixed << std::setprecision(1) << max_height_cm
                << " max_protrusion_cm=" << max_protrusion_cm
                << " min_distance_cm=" << min_distance_cm
                << " remaining_travel_cm=" << remaining_travel_cm
                << " big_obstacle=" << (big_obstacle ? "yes" : "no")
                << " default_step_cm=" << suggested_step_cm
                << " next_step_gait=" << (next_is_cross ? "cross" : "normal");
        evt_(evt_msg.str());
        std::cout << "[depth_avoid] " << evt_msg.str() << "\n";

        // Wait for cmd_depth_avoid_continue(cm) / cmd_depth_avoid_stop() —
        // reuses obstacle_ask_pending_ / obstacle_user_response_ (same
        // atomics v1's run_avoid used; generic pending-flag + response-int,
        // no run_avoid-specific semantics baked into them).
        obstacle_user_response_.store(-1);
        obstacle_ask_pending_.store(true);
        std::cout << "[depth_avoid] waiting for user response (continue/stop/emergency_stop)\n";
        int waited_ms = 0;
        const int timeout_ms = OBSTACLE_ASK_TIMEOUT_S * 1000;
        while (obstacle_ask_pending_.load() && !abort_flag.load() && waited_ms < timeout_ms) {
            sleep_ms_(100);
            waited_ms += 100;
        }
        obstacle_ask_pending_.store(false);

        if (abort_flag.load()) {
            std::cout << "[depth_avoid] aborted by user (emergency_stop)\n";
            break;
        }
        if (waited_ms >= timeout_ms) {
            std::cerr << "[depth_avoid] user response timeout " << OBSTACLE_ASK_TIMEOUT_S << "s — abort\n";
            evt_("depth_avoid_user_timeout");
            break;
        }
        if (obstacle_user_response_.load() == 0) {
            std::cout << "[depth_avoid] user stopped — end loop\n";
            evt_("depth_avoid_user_cancel");
            break;
        }

        next_step_cm = depth_avoid_next_step_cm_.load();
        set_state_(State::Running);   // do_step_down_ / realign may have flipped state between iterations
    }

    obstacle_ask_pending_.store(false);
    if (state_.load() == State::Running) set_state_(cur);
    std::cout << "[depth_avoid] done after " << iter << " iter(s), state restored to "
              << state_name(cur) << "\n";
    evt_("depth_avoid_done iter=" + std::to_string(iter));
    return "OK depth_avoid_done\n";
}

// GUI: user typed/kept a cm value and pressed Continue.
std::string WashRobot::cmd_depth_avoid_continue(int cm) {
    if (!obstacle_ask_pending_.load()) return "ERR no_depth_avoid_pending\n";
    if (cm < STEP_CM_MIN || cm > STEP_CM_MAX) {
        std::ostringstream oss;
        oss << "ERR step_cm_out_of_range " << cm
            << " (allowed " << STEP_CM_MIN << ".." << STEP_CM_MAX << ")\n";
        return oss.str();
    }
    depth_avoid_next_step_cm_.store(cm);
    obstacle_user_response_.store(1);
    obstacle_ask_pending_.store(false);
    std::cout << "[depth_avoid] user CONTINUE step_cm=" << cm << "\n";
    return "OK continuing\n";
}

// GUI: user pressed Stop.
std::string WashRobot::cmd_depth_avoid_stop() {
    if (!obstacle_ask_pending_.load()) return "ERR no_depth_avoid_pending\n";
    obstacle_user_response_.store(0);
    obstacle_ask_pending_.store(false);
    std::cout << "[depth_avoid] user STOP\n";
    return "OK stopping\n";
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
    oss << " pusher_rpm_disable_slow="        << settings_.pusher_rpm_disable_slow.load()        << ":" << PUSHER_RPM_DISABLE_SLOW;
    oss << " disable_phase_current_limit_ma=" << settings_.disable_phase_current_limit_ma.load() << ":" << DISABLE_PHASE_CURRENT_LIMIT_MA;
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
    else if (key == "pusher_rpm_disable_slow")        bad = apply_to_atomic_<int>   (settings_.pusher_rpm_disable_slow,        value, 10,    200);
    else if (key == "disable_phase_current_limit_ma") bad = apply_to_atomic_<int>   (settings_.disable_phase_current_limit_ma, value, 500,   3000);
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
// [2026-08-29] 這裡原本有 21 個「與 static constexpr 同名」的 #define，
// 把常數名稱在本行之後重新定義成 settings_.xxx.load()。其中包含安全互鎖
// （ROPE_WEIGHT_LIMIT_* 繩重上限、DISABLE_PHASE_CURRENT_LIMIT_MA 撞障礙物電流保險）。
//
// 🔴 問題不在它會壞，而在**同一個識別字在檔案前後半是兩個不同的東西**：
//    本行之前拿到編譯期預設值，之後拿到操作者可調的現值。從 WASH_ROBOT.h
//    讀到 `static constexpr double ... = 40.0;` 完全看不出這件事。
//    當時沒有 bug——前半的用法正好都合理需要預設值（初始化 settings、印
//    「現值:預設值」）——但任何人在本行之前新增引用，會**靜默拿到預設值而非現值**，
//    而安全門檻讀到預設值是錯的那個方向。編譯器不會警告、執行期沒有訊號。
//
// 改法：巨集全部移除，要現值的地方明寫 (settings_.xxx.load())。
// 之後常數名稱**在任何位置都只有一個意思**（編譯期預設值），歧義消失。
// 📌 預處理輸出逐位元不變——寫下去的正是巨集原本展開的內容，已用
//    harness/prove_noop.sh 驗證。
//
// ⚠️ PUSHER_EXTEND_BODY_PULSE_SHORT 的巨集一處都沒被用到：那個 setting 可設定、
//    會出現在 status、會存檔，但沒有任何程式碼讀它（v1 body 推桿殘留，v2 已無）。
//    操作者改了會看到值變了，機器完全不理。刻意不動——移除可設定的 key 會改變
//    指令介面＝功能改變，不是整理。已入待辦。

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
    f << "pusher_rpm_disable_slow        " << settings_.pusher_rpm_disable_slow.load()        << "\n";
    f << "disable_phase_current_limit_ma " << settings_.disable_phase_current_limit_ma.load() << "\n";
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
bool WashRobot::arm_sweep_fire_nowait_(double target_cm, int rpm, int acc, int dec, int est_ms) {
    // [2026-08-28] 回傳值原本被整個丟掉，於是上滑台三次寫入全滅時流程照樣印
    // 「rail sweep done」——bench 上 DM2J:14 掛在錯的 gateway 時，log 看起來
    // 一切正常，實際上滑台一動也沒動。現在追蹤有沒有任何一次成功。
    bool any_ok = false;
    for (int i = 0; i < ARM_SWEEP_FIRE_RETRIES; ++i) {
        if (!D_(DM2J_ARM).PR_move_cm_nowait(0, 1, rpm, target_cm, acc, dec)) any_ok = true;
        if (i < ARM_SWEEP_FIRE_RETRIES - 1) sleep_ms_(ARM_SWEEP_FIRE_SPACING_MS);
    }
    if (!any_ok) {
        std::cerr << "[arm_sweep] DM2J:" << DM2J_ARM << " " << ARM_SWEEP_FIRE_RETRIES
                  << " 次寫入全部失敗 — 上滑台沒有移動（目標 " << target_cm << " cm）\n";
        evt_("arm_sweep_rail_no_response cm=" + std::to_string((int)target_cm));
        // 寫入都沒進去就不必等 est_ms 的行程時間 —— 沒有東西在動。
        // 省下的時間在「上滑台整個不通」時很可觀（每步兩次掃動 × est_ms）。
        return false;
    }
    // [2026-05-28] Replace plain sleep with monitor loop (Option A: DM2J:14
    // alarm bit + Option C: damiao M2 tau spike). Sets arm_sweep_obstacle_pending_
    // on detection → main thread try_or_pause_ external-pause picks it up.
    arm_monitor_during_sweep_(est_ms);
    return true;
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
void WashRobot::arm_monitor_during_sweep_(int est_ms) {
    // arm_attached_=off → no tau to read; fall back to plain sleep so caller
    // semantics unchanged. (DM2J:14 still moves; could monitor alarm but no
    // tau reference.)
    if (!arm_attached_.load()) {
        sleep_ms_(est_ms);
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
    sleep_ms_(est_ms);
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
    while (elapsed < est_ms) {
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
            // 🔴 [2026-08-28] 這一發的回傳值原本被丟掉，而上面那段註解自己寫著
            //    "Critical to stop the slide IMMEDIATELY" —— 一個關鍵停止指令
            //    失敗時完全靜默。DM2J 的 void 一族已改為回傳 bool（false = OK），
            //    這裡是第一個真的去看它的呼叫端。
            //    ⚠ 停不下來時**不能只是記錄**：滑台會繼續跑到目標位置（~3-4s），
            //    而障礙就在路徑上。發 EVT 讓上層與 GUI 知道「停止沒送成功」。
            if (D_(DM2J_ARM).speed_move_stop()) {   // 0x6002 = 0x0040 (PR motion halt)
                std::cerr << "[arm_sweep] 🔴 speed_move_stop 送出失敗 — 滑台可能仍在移動\n";
                evt_("arm_sweep_stop_failed");
            }
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
                              << "ms (early exit, saved ~" << (est_ms - elapsed) << "ms)\n";
                    break;
                }
            }
        }

        // [2026-05-28] Skip tau-based trigger in last DECEL_MASK_MS — slide
        // deceleration induces M1 tau spike that mimics obstacle. Trade-off:
        // obstacles in last ~16cm of slide travel not detected.
        // Diagnostic log still prints in decel mask so user can see what M1/M2
        // are doing during decel.
        const bool in_decel_mask = (elapsed > est_ms - ARM_SWEEP_DECEL_MASK_MS);

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
              << " delta=" << delta << " rad (tol=" << (settings_.arm_deploy_pos_tol_rad.load()) << ")\n";
    if (delta > (settings_.arm_deploy_pos_tol_rad.load())) {
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
//
// [2026-07-27 per user] Body below RETIRED (kept as #if 0 reference, not
// deleted) — replaced with a simplified version that matches
// do_step_sync_rail_sweep_'s concrete sequence exactly (INIT every time,
// single DEPLOY LEFT/RIGHT per round, no water, no DEPLOY verify/retry, no
// obstacle-pause handling, no RAII cleanup guard). See the real
// do_arm_clean_sweep_() definition AFTER the #endif below. To restore this
// old, more robust version: flip which one is #if 0'd.

// [2026-07-27 per user] Simplified do_arm_clean_sweep_ — matches
// do_step_sync_rail_sweep_'s concrete sequence exactly, per round:
//   arm_cmd_("INIT") → DEPLOY LEFT → CH_BRUSH on → sleep 2.5s →
//   arm_sweep_fire_nowait_(ARM_SWEEP_CM) → CH_BRUSH off → DEPLOY RIGHT →
//   sleep 2.5s → arm_sweep_fire_nowait_(0.0) → PARK
// Deliberately no water (not plumbed in), no verify_arm_deploy_/
// verify_arm_m2_at_slot_ retry, no obstacle-pause handling, no RAII cleanup
// guard — all of that lived in the retired version above (#if 0) and is
// kept there for reference/restoration, not deleted.
// Same entry guards as before (arm_attached_ / arm_sweep_skip_rest_of_run_)
// since those are basic on/off switches, not part of the robustness being
// simplified away here.
std::string WashRobot::do_arm_clean_sweep_(int wall_mm, int rounds) {
    if (!arm_attached_.load()) {
        std::cout << "[arm_clean_sweep] SKIPPED (arm_attached=off)\n";
        return "OK skipped_arm_off\n";
    }
    if (arm_sweep_skip_rest_of_run_.load()) {
        std::cout << "[arm_clean_sweep] SKIPPED (arm_sweep_skip_rest_of_run_=true from prior obstacle)\n";
        return "OK skipped_arm_obstacle\n";
    }

    for (int r = 0; r < rounds; ++r) {
        if (check_abort_()) return "ERR aborted\n";
        std::cout << "[arm_clean_sweep] round " << (r + 1) << "/" << rounds << " start\n";

        const bool init_ok = (arm_cmd_("INIT", 60).rfind("OK", 0) == 0);
        arm_calibrated_.store(init_ok);
        bool deployed = false;
        if (!init_ok) {
            std::cerr << "[arm_clean_sweep] arm INIT failed — rail sweep only, no brush\n";
        } else {
            // [2026-08-26 per user] 滾筒側 LEFT → RIGHT（工具頭實體對調，見
            // do_step_sync_rail_sweep_ 的同批說明）。這段序列跟那邊是複製關係，
            // 兩處必須一起改，否則手動 CLEAN SWEEP 跟步伐內建清洗會用相反的工具頭。
            std::ostringstream oss_brush;
            oss_brush << "DEPLOY " << wall_mm << " RIGHT";   // RIGHT = 滾筒側
            deployed = (arm_cmd_(oss_brush.str(), 30).rfind("OK", 0) == 0);
            if (deployed) {
                pqw_.controlRelay(CH_BRUSH, true);
                sleep_ms_(2500);
            } else {
                std::cerr << "[arm_clean_sweep] arm deploy RIGHT (brush) failed — rail sweep only, no brush\n";
            }
        }

        // [2026-07-27 per user] Pass DM2J_ARM_STEP_SWEEP_* explicitly instead of
        // arm_sweep_fire_nowait_'s ARM_SWEEP_* defaults — align rail speed/wait
        // with do_step_sync_rail_sweep_ (RPM 1000→300, EST_MS 3900→1000; ACC/DEC
        // already matched at 100/100).
        arm_sweep_fire_nowait_((double)ARM_SWEEP_CM,
                               DM2J_ARM_STEP_SWEEP_RPM, DM2J_ARM_STEP_SWEEP_ACC, DM2J_ARM_STEP_SWEEP_DEC,
                               DM2J_ARM_STEP_SWEEP_EST_MS);
        if (check_abort_()) {
            if (deployed) {
                pqw_.controlRelay(CH_BRUSH, false);
                arm_cmd_("PARK", 30);
            }
            return "ERR aborted\n";
        }

        if (deployed) {
            pqw_.controlRelay(CH_BRUSH, false);
            // [2026-08-26 per user] 刮刀側 RIGHT → LEFT（同上）
            std::ostringstream oss_squeegee;
            oss_squeegee << "DEPLOY " << wall_mm << " LEFT";   // LEFT = 刮刀側
            if (arm_cmd_(oss_squeegee.str(), 30).rfind("OK", 0) != 0) {
                std::cerr << "[arm_clean_sweep] arm deploy LEFT (squeegee) failed — continuing rail only\n";
            } else {
                sleep_ms_(2500);
            }
        }

        arm_sweep_fire_nowait_(0.0,
                               DM2J_ARM_STEP_SWEEP_RPM, DM2J_ARM_STEP_SWEEP_ACC, DM2J_ARM_STEP_SWEEP_DEC,
                               DM2J_ARM_STEP_SWEEP_EST_MS);

        if (deployed) {
            arm_cmd_("PARK", 30);
        }
        std::cout << "[arm_clean_sweep] round " << (r + 1) << "/" << rounds << " done\n";
    }

    std::cout << "[arm_clean_sweep] all rounds done\n";
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

    // ---------- Phase C: 連續 LOOP（RIGHT 滾筒 → LEFT 刮刀）until keep_going=false ----------
    // [2026-08-26 per user] 工具頭實體左右對調：滾筒 LEFT→RIGHT、刮刀 RIGHT→LEFT。
    // 每個 sub-round 內部不檢查 keep_going（避免半 round 停在牆上）。
    // round 之間（滾筒段跟刮刀段之間、刮刀段結束之後）才檢查。
    // [2026-05-28] Bidirectional sweep (single sweep per sub-round):
    //   RIGHT (roller) : slide 0 → ARM_SWEEP_CM  (wet)
    //   LEFT  (scraper): slide ARM_SWEEP_CM → 0  (wipe, returns to 0)
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
                // [2026-08-27 per user] 水泵先拿掉（同 sweep_with_tool，理由見那邊註解）
                //if (pqw_set_relay_verified_(CH_WATER_PUMP, true)) {
                //    std::cerr << "[arm_clean_sweep_cont] pqw ON water_pump FAIL\n";
                //    return false;
                //}
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
                  << " — RIGHT(滾筒+水) 0→" << ARM_SWEEP_CM
                  << " → RIGHT " << ARM_SWEEP_CM << "→0"
                  << " → LEFT(刮刀乾) 0→" << ARM_SWEEP_CM
                  << " → LEFT " << ARM_SWEEP_CM << "→0\n";
        // [2026-06-05] 每 round 4 個 sub-stroke：滾筒濕拖 ×2 + 刮刀乾掃 ×2。
        // 同 slot+water 連續切換時 skip_deploy=true 省 DEPLOY 時間。
        // 1: RIGHT 0→80 (滾筒往右，首次 DEPLOY)
        if (!sweep_with_tool("RIGHT", true,  "roller-1",   (double)ARM_SWEEP_CM, false)) {
            std::cerr << "[arm_clean_sweep_cont] LEFT-1 round " << round_cnt << " failed — abort loop\n";
            return "ERR sweep_left_fail\n";
        }
        // 2: RIGHT 80→0 (滾筒往左，skip_deploy 同 slot+water)
        if (!sweep_with_tool("RIGHT", true,  "roller-2",   0.0,                  true)) {
            std::cerr << "[arm_clean_sweep_cont] LEFT-2 round " << round_cnt << " failed — abort loop\n";
            return "ERR sweep_left_fail\n";
        }
        // 3: LEFT 0→80 (換刮刀往右，DEPLOY + 關水/刷)
        if (!sweep_with_tool("LEFT",  false, "scraper-1",  (double)ARM_SWEEP_CM, false)) {
            std::cerr << "[arm_clean_sweep_cont] RIGHT-1 round " << round_cnt << " failed — abort loop\n";
            return "ERR sweep_right_fail\n";
        }
        // 4: LEFT 80→0 (刮刀往左，skip_deploy 同 slot+water)
        if (!sweep_with_tool("LEFT",  false, "scraper-2",  0.0,                  true)) {
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

// Read max rope tension (kg) — primary via crane DSZL-107, fallback to
// washrobot-end DY-500 cache.
//
// 1. Primary: crane_cmd_("tension") returns "OK left=<kg> right=<kg>" (DSZL-107
//    cached by crane's hold_loop atomic; ~1ms server processing + TCP RTT).
//    Returns max(left, right) per Q1=(a) decision 2026-05-07.
// 2. Fallback (if crane offline / parse fail): washrobot-end DY-500 cache
//    (slave 10/11 — only present if installed; in current builds these are
//    offline, returning -1).
// [2026-08-04 per user] Removed 3rd fallback (easy crane weight via
// crane_shim) — Crane_easy_PI hardware decommissioned, crane_shim retired
// alongside it. See read_easy_weight_kg_ removal in the same change.
// Returns WEIGHT_NO_DATA_KG if both remaining tiers fail.
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

    return WEIGHT_NO_DATA_KG;
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
            if (!crane_cli_estop_.connectToServer(ep::host("CRANE", CRANE_IP), ep::port("CRANE", CRANE_PORT)))
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
            return (settings_.rope_weight_limit_attached.load());
        case State::Idle:
        case State::Ready:
        case State::ReturningHome:
        case State::Error:
        default:
            return (settings_.rope_weight_limit_hanging.load());
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
                        crane_cli_estop_.connectToServer(ep::host("CRANE", CRANE_IP), ep::port("CRANE", CRANE_PORT));
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

// [2026-08-27 per user] 由加速度計算傾斜角（見 WASH_ROBOT.h 的完整說明）。
bool WashRobot::imu_tilt_from_accel_(double& roll_deg, double& pitch_deg) const {
    const double ax = imu_.ax, ay = imu_.ay, az = imu_.az;
    const double amag = std::sqrt(ax * ax + ay * ay + az * az);

    // 靜止時加速度模長應該 ≈1g。低於 0.5 表示模組根本沒送加速度封包（bench 上
    // ax/ay/az 恆為 0，n_accel 計數不動），或資料異常。
    // ⚠ 這裡一定要回報失敗，不能讓 atan2(0,0)=0 被當成「完美水平」——那會讓
    // 傾斜保護在毫無資料的情況下永遠不觸發，比誤報危險得多。
    if (amag < 0.5) return true;

    constexpr double RAD2DEG = 57.29577951308232;   // 180/π
    roll_deg  = std::atan2(ay, ax) * RAD2DEG;                          // 左右傾斜（主要）
    pitch_deg = std::atan2(az, std::sqrt(ax * ax + ay * ay)) * RAD2DEG; // 前後傾斜（輔助）
    return false;
}

bool WashRobot::imu_take_baseline_() {
    double sum_roll = 0.0, sum_pitch = 0.0;
    int n = 0;
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(IMU_BASELINE_SEC);
    while (std::chrono::steady_clock::now() < end) {
        if (!imu_.read_error.load()) {
            // 2026-08-26: IMU 改垂直地面放後 pitch(舊 imu_.y) 卡 gimbal lock（~±90°），
            // 實測 roll 改讀 yaw(imu_.z) 才會隨左右傾斜穩定變化，pitch 改讀舊 roll 軸(imu_.x) 當輔助監控。
            // [2026-08-27 per user] IMU 換成另一顆、改回水平安裝，基準也跟著改回
            // 尤拉角 roll(imu_.x) / pitch(imu_.y)，與 imu_monitor_loop_ / status
            // 的定義保持一致。三者必須用同一套定義，否則相減出來的偏差沒有意義
            // （本日早先一度出現 baseline 存尤拉角、monitor 用加速度的不一致）。
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

void WashRobot::imu_monitor_loop_() {
    const int SAMPLE_MS   = 100;
    const int AVG_SAMPLES = 10;
    const int SUSTAIN_MS  = 500;

    std::deque<double> window;
    int  over_ask_ms  = 0;
    int  over_stop_ms = 0;
    bool ask_sent     = false;
    // [2026-08-27] 「IMU 未輸出加速度」只警告一次的旗標。用 loop-local 變數而非
    // static：一旦加速度恢復輸出就重置，之後若再中斷仍會重新提醒一次。
    bool accel_missing_warned = false;

    while (imu_mon_running_.load()) {
        sleep_ms_(SAMPLE_MS);
        if (!imu_mon_running_.load()) break;

        if (imu_.read_error.load()) {
            over_ask_ms = over_stop_ms = 0;
            continue;
        }

        // [2026-08-27 per user] 傾斜保護被關閉時完全跳過判斷（見 WASH_ROBOT.h
        // imu_guard_enabled_ 的說明）。歸零累積計時，避免關閉期間累積的時間在
        // 重新開啟的瞬間立刻觸發 emergency。
        if (!imu_guard_enabled_.load()) {
            over_ask_ms = over_stop_ms = 0;
            continue;
        }

        // [2026-08-27 per user] IMU 換成另一顆、改回水平安裝，因此改回使用內建
        // 尤拉角 roll(imu_.x) / pitch(imu_.y)——即 2026-08-26 改垂直之前的設計。
        //
        // 為什麼水平安裝下尤拉角才是最佳選擇：
        //   1. 沒有 gimbal lock（pitch 遠離 ±90°），這是垂直安裝時唯一的致命問題
        //   2. WT901 的 roll/pitch 本身就是相對重力算的，且經過陀螺儀融合，
        //      動態下比純加速度換算更穩（純加速度會被機器移動的加速度污染）
        //   3. 只有 yaw(imu_.z) 吃磁力計會漂——而我們不用 yaw
        //
        // imu_tilt_from_accel_() 保留，但用途改為「安裝方位健全性檢查」（見下）：
        // 水平安裝時重力應幾乎全落在 Z 軸，az≈±1。若哪天 IMU 又被改成立起來，
        // 這個檢查會主動警告，而不是讓尤拉角悄悄回到 gimbal lock 的壞狀態。
        {
            const double amag = std::sqrt(imu_.ax * imu_.ax + imu_.ay * imu_.ay
                                        + imu_.az * imu_.az);
            if (amag >= 0.5 && std::abs(imu_.az) < 0.70 && !accel_missing_warned) {
                accel_missing_warned = true;   // 借用同一個 once 旗標，避免洗版
                std::cerr << "[imu_monitor] ⚠ IMU 疑似不是水平安裝：|az|="
                          << std::abs(imu_.az) << " (<0.70)，重力主分量不在 Z 軸。\n"
                             "               水平安裝時 az 應接近 ±1。若 IMU 被改成"
                             "立起來，尤拉角會卡 gimbal lock、roll/pitch 不可信，\n"
                             "               需改用加速度推導（imu_tilt_from_accel_ "
                             "已備妥，只需改軸並接回 monitor）。\n";
            }
        }

        double roll  = imu_.x - imu_roll0_;
        double pitch = imu_.y - imu_pitch0_;
        // 扣掉水平基準（imu_zero 取的），用途是吸收 IMU 安裝的固定偏差。
        double deg = std::max(std::abs(roll), std::abs(pitch));

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
        if (avg >= (settings_.imu_ask_deg.load()) && avg < IMU_EMERGENCY_DEG) {
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
            if (avg < (settings_.imu_ask_deg.load()) - IMU_HYSTERESIS_DEG) {
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
    const int    PRINT_EVERY_N_POLLS = 2000 / poll_ms;   // [2026-07-15 per user] ~2s move ticker

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
            // [2026-07-15 per user] 300ms → ~2s ticker (was drowning the log during
            // long moves across 4 slaves). PRINT_EVERY_N_POLLS derived from poll_ms
            // so the interval stays ~2s even if poll_ms is retuned later.
            if (poll_count % PRINT_EVERY_N_POLLS == 0) {
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
    // [2026-07-15] zdt_bus_mtx_ — see declaration comment (WASH_ROBOT.h).
    std::lock_guard<std::mutex> zdt_lk(zdt_bus_mtx_);

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
    // Modbus spec, broadcasts get no response. [2026-08-29] The driver used to
    // report that missing reply as an error; it now returns false (success) when
    // the send succeeds, so the return value is finally meaningful. Still not
    // checked here on purpose: a broadcast cannot confirm the slaves acted on it,
    // so real error detection stays with the poll loop below.
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
    const int    PRINT_EVERY_N_POLLS = 2000 / poll_ms;   // [2026-07-15 per user] ~2s move ticker (was 300ms)

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

            // Diagnostic: track peak phase current + log live current every ~2s
            // (was every ~300ms — drowned the log during long moves across 4
            // slaves). Mirrors the disable_seal Step D current log so the
            // web RETRACT path (pusher_move_many_) also shows the current curve.
            if (st.phase_current > peak_I[i]) peak_I[i] = st.phase_current;
            if (poll_count % PRINT_EVERY_N_POLLS == 0) {
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

// ⚠ 函式名的 "two_stage" 是歷史遺留 —— 2026-07-31 起已經是「破真空輔助的單段
//   直收」，破真空取代了原本的慢撕階段。沒有第二段。
//
// [2026-08-28] 補上 BREAK_VACUUM_PRE_ON_REST_MS（關真空閥 -> 開破真空閥之間的
// 強制靜置）+ 兩個 controlRelay 的回傳值檢查。在此之前 ON/OFF 都是裸寫、回傳值
// 丟掉，所以「CH ON」那行 log 不論成敗都照印 —— 破真空實際上從沒 fire 過也看不
// 出來。bench 指紋見 WASH_ROBOT.h 的 BREAK_VACUUM_PRE_ON_REST_MS 註解。
//
// [2026-07-31 per user] Rewritten to mirror Linux_test menu 31
// (test_break_vacuum_leg) exactly, generalized to N slaves at once and
// CH16(bench) -> CH_BREAK_VACUUM(2026-08-27 per user 起為 CH6；曾短暫是 14):
// CH_BREAK_VACUUM actively
// charges air into the cups to force the seal open, so the old two-stage
// slow-peel-then-fast retract is no longer needed — every slave now goes
// straight to PUSHER_RETRACT_PULSE at PUSHER_RPM_RETRACT_FULL, with the valve
// open through BREAK_VACUUM_PRE_RETRACT_MS before the move fires and staying
// open until BREAK_VACUUM_TOTAL_ON_MS total has elapsed since it turned on
// (the pull itself helps peel the seal while air is still charging in — same
// bench-proven timing, not gated on pressure). RAII guard closes the valve on
// every exit path (bench lesson: an early-return without closing it left the
// valve charging indefinitely — "very dangerous").
// [pre-2026-07-31 history, kept for context]
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

    // [2026-07-15] zdt_bus_mtx_ — see declaration comment (WASH_ROBOT.h).
    std::lock_guard<std::mutex> zdt_lk(zdt_bus_mtx_);

    // [2026-05-29] DM2J motion active gate — see dm2j_pair_move_abs_ for rationale.
    dm2j_motion_active_.store(true);
    struct ClearMotionFlag {
        std::atomic<bool>* flag;
        ~ClearMotionFlag() { flag->store(false); }
    } _clr{&dm2j_motion_active_};

    // Pre-clear stall flags (same rationale as pusher_move_many_): a lingering
    // flag makes ZDT firmware silently reject the pos_mode write.
    for (int s : slaves) Z_(s).release_stall_flag();

    // [2026-07-31 per user] Break-vacuum-assisted single-stage retract — mirrors
    // Linux_test menu 31 exactly. RAII guard closes CH_BREAK_VACUUM on every
    // exit path (bench lesson: an early-return without closing it left the
    // valve charging indefinitely).
    struct BreakVacuumGuard {
        WashRobot* self;
        bool armed;
        BreakVacuumGuard(WashRobot* s) : self(s), armed(false) {}
        ~BreakVacuumGuard() {
            if (armed) {
                std::cerr << "[2stage_retract] SAFETY closing CH" << CH_BREAK_VACUUM << " on exit\n";
                // [2026-08-28] 解構子裡不能拋，但至少要讓失敗被看見 —— 這是最後
                // 一道關閥保險，它再失敗就真的沒人關了（閥持續充氣 -> 下次伸腳吸不住）。
                if (self->pqw_.controlRelay(CH_BREAK_VACUUM, false)) {
                    std::cerr << "[2stage_retract] ⚠ SAFETY close CH" << CH_BREAK_VACUUM
                              << " ALSO FAILED — 閥可能仍在充氣，請人工確認\n";
                }
            }
        }
    } bv_guard(this);

    // [2026-08-28] 強制靜置後才碰破真空閥 —— 呼叫端剛關過真空閥（vacuum_valve_
    // "feet" false），間隔太近的話這顆 CH 不會實際動作。完整理由與 bench 指紋見
    // WASH_ROBOT.h 的 BREAK_VACUUM_PRE_ON_REST_MS。
    // 放在這裡而不是各呼叫端：pusher_two_stage_retract_ 有 16 個呼叫點，全都是
    // 「關閥 -> 收腳」的序列，逐一補會漏。代價是每次收腳固定 +300ms。
    sleep_ms_(BREAK_VACUUM_PRE_ON_REST_MS);

    // ⚠ 一定要檢查回傳值。原本這行是裸寫 + 丟掉回傳值（註解寫 log-only on failure，
    // 但根本沒有 log failure 的碼），於是上面那行 "CH ON" 在寫入之前就印了 ——
    // 不論成敗都照印，log 完全無法用來判斷破真空到底有沒有作用。比照 Linux_test
    // menu 31/33 改成檢查 TCP-level 回傳值。
    // 刻意不用 pqw_set_relay_verified_（readback 版）：它成功時要等 200ms 才回來，
    // 會把「ON -> 80ms -> 收腳」這個 bench 調出來的時序推成 200ms 才開始收，
    // 驗證機制不該順帶改掉動作時序。
    std::cout << "[2stage_retract] CH" << CH_BREAK_VACUUM << " ON (break-vacuum charge)\n";
    const bool bv_on_fail = pqw_.controlRelay(CH_BREAK_VACUUM, true);
    bv_guard.armed = true;   // from here on, ANY return path closes the valve automatically
    if (bv_on_fail) {
        // 不中止：收腳仍要進行（腳留在伸出狀態更危險）。但要讓操作者知道這一次
        // 是「沒有破真空輔助的硬撕」，對應症狀就是收腳電流飆高 / STALL。
        std::cerr << "[2stage_retract] CH" << CH_BREAK_VACUUM
                  << " ON FAILED (TCP-level) — 破真空沒作用，本次收腳為硬撕，"
                     "預期電流偏高甚至 STALL\n";
        evt_("break_vacuum_on_fail ch=" + std::to_string(CH_BREAK_VACUUM));
    }
    const auto bv_on_at = std::chrono::steady_clock::now();

    sleep_ms_(BREAK_VACUUM_PRE_RETRACT_MS);

    // Direct retract to PUSHER_RETRACT_PULSE for every slave (no slow-peel stage —
    // the break-vacuum charge does that job now). Single sync-trigger fires all.
    for (int s : slaves) {
        if (Z_(s).motion_control_pos_mode_nowait(0, PUSHER_ACC_RETRACT,
                PUSHER_RPM_RETRACT_FULL, PUSHER_RETRACT_PULSE,
                /*abs*/1, /*sync*/1, /*retry*/1)) {
            std::cout << "[2stage_retract ZDT:" << s << "] pos_mode_nowait FAIL\n";
            return true;
        }
    }
    Z_(slaves.front()).trigger_sync_move();

    // CH_BREAK_VACUUM stays on until BREAK_VACUUM_TOTAL_ON_MS has elapsed since
    // it turned on (retract is already running concurrently by now — this wait
    // just times how much longer the valve itself needs to stay open).
    {
        const auto held_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - bv_on_at).count();
        if (held_ms < BREAK_VACUUM_TOTAL_ON_MS)
            sleep_ms_((int)(BREAK_VACUUM_TOTAL_ON_MS - held_ms));
    }
    // [2026-08-28] 同樣要檢查回傳值，而且這邊比 ON 更關鍵：關不掉代表破真空閥
    // 一直在充氣，之後伸腳要吸附時吸不住（正壓灌進吸盤）。
    // ⚠ 失敗時「不」解除 bv_guard.armed —— 讓 RAII guard 在函式結束時再關一次，
    //   多關一次是無害的冪等操作，漏關則是實質危險。
    std::cout << "[2stage_retract] CH" << CH_BREAK_VACUUM << " OFF\n";
    if (pqw_.controlRelay(CH_BREAK_VACUUM, false)) {
        std::cerr << "[2stage_retract] CH" << CH_BREAK_VACUUM
                  << " OFF FAILED (TCP-level) — 閥可能仍在充氣，交給 RAII guard 再關一次\n";
        evt_("break_vacuum_off_fail ch=" + std::to_string(CH_BREAK_VACUUM));
        // armed 維持 true，guard 的解構子會再送一次 OFF
    } else {
        bv_guard.armed = false;   // closed deliberately above — guard is now a harmless no-op
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
    // Modbus spec, broadcasts get no response. [2026-08-29] The driver used to
    // report that missing reply as an error; it now returns false (success) when
    // the send succeeds, so the return value is finally meaningful. Still not
    // checked here on purpose: a broadcast cannot confirm the slaves acted on it,
    // so real error detection stays with the poll loop below.
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
                                                   bool* any_obstacle_out,
                                                   bool stop_on_first_seal,
                                                   int max_iters,
                                                   const std::vector<int>* stop_group_ids) {
    if (any_obstacle_out) *any_obstacle_out = false;   // default-clear so caller doesn't need to pre-init
    if (slaves.empty()) return false;
    if (slaves.size() != target_pulses.size()) {
        std::cout << "[disable_seal] size mismatch slaves=" << slaves.size()
                  << " targets=" << target_pulses.size() << "\n";
        return true;
    }
    // [2026-07-15] zdt_bus_mtx_ — see declaration comment (WASH_ROBOT.h). Held
    // for the whole function since it does many sequential Modbus round-trips.
    std::lock_guard<std::mutex> zdt_lk(zdt_bus_mtx_);

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

    // [2026-07-23 per user] Group-aware stop_on_first_seal support. Freezes
    // slave i in place (re-enable EN, lock final_pulse at current position,
    // fresh-read rescue, else weak_seal) — the EXACT same finalization the
    // MAX_ITERS wrap-up below already does for any leftover !done[i] slave.
    // Factored out so a slave can be frozen mid-loop (its group already has a
    // real seal via a sibling) without waiting for the whole function to end,
    // while every existing `if (done[i]) continue/skip` check elsewhere in
    // this function already treats a frozen slave exactly like a resolved one.
    auto freeze_and_finalize = [&](int i) {
        if (done[i]) return;
        Z_(slaves[i]).motion_control_driver_EN(true);
        sleep_ms_(80);
        if (Z_(slaves[i]).get_system_status() == false) {
            final_pulse[i] = deg_to_pulse(Z_(slaves[i]).status.real_pos);
        }
        sleep_ms_(200);
        int fresh_p = read_pressure_(slaves[i]);
        int errf    = M_(slaves[i]).error_flag;
        if (!errf && fresh_p <= (settings_.vacuum_seal_deep_kpa.load())) {
            done[i] = true;
            std::cout << "[disable_seal:" << slaves[i]
                      << "] RESCUED (group-frozen) — pulse=" << final_pulse[i]
                      << " fresh_p=" << fresh_p << "kPa <= " << (settings_.vacuum_seal_deep_kpa.load())
                      << " → SEAL not weak_seal\n";
        } else {
            weak_seal[i] = true;
            done[i] = true;
            std::cout << "[disable_seal:" << slaves[i]
                      << "] group already sealed via sibling — freezing here, pulse="
                      << final_pulse[i] << " fresh_p=" << fresh_p << "kPa"
                      << (errf ? " (READ_ERR — stale)" : "") << "\n";
        }
    };
    // Which stop-domain slave i belongs to: caller-supplied per-slave group id,
    // or a single implicit group (0) covering everyone — matches the
    // long-standing "any one seal stops the whole call" behavior when no
    // stop_group_ids is given (every pre-2026-07-23 caller).
    auto stop_group_of = [&](int i) -> int {
        return stop_group_ids ? (*stop_group_ids)[i] : 0;
    };
    auto group_has_real_seal = [&](int gid) -> bool {
        for (int i = 0; i < N; ++i) {
            if (stop_group_of(i) == gid && done[i] && !weak_seal[i] && !obstacle[i]) return true;
        }
        return false;
    };
    // Freeze any not-done slave whose group already has a real seal (via a
    // sibling). Returns true if EVERY slave is now done (either genuinely
    // sealed or just frozen) — i.e. nothing left to push, caller should stop.
    auto apply_stop_on_first_seal = [&]() -> bool {
        for (int i = 0; i < N; ++i) {
            if (done[i]) continue;
            if (group_has_real_seal(stop_group_of(i))) freeze_and_finalize(i);
        }
        for (int i = 0; i < N; ++i) if (!done[i]) return false;
        return true;
    };

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
        if (phase1_peak_I[i] >= (settings_.disable_phase_current_limit_ma.load())) {
            endpoint_stalled[i] = true;
            std::cout << "[disable_seal:" << slaves[i] << "] Phase 1 already at wall"
                      << " (peakI=" << phase1_peak_I[i] << "mA >= "
                      << (settings_.disable_phase_current_limit_ma.load())
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
    // [2026-07-14] max_iters override (0 = use DISABLE_RETRY_MAX_ITERS). feet_topup_
    // passes a small cap so the 2nd-cup top-up gives up fast → shorter switch gap.
    const int MAX_ITERS = (max_iters > 0) ? max_iters : (settings_.disable_retry_max_iters.load());
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
            if (p_ok && p <= (settings_.vacuum_seal_deep_kpa.load())) {
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
        // [2026-07-08 per user] step feet: stop as soon as >=1 cup TRULY sealed
        // (not weak_seal / obstacle) — don't keep pushing the rest of the group.
        // The not-done cups fall through to the wrap-up below (re-enable EN + lock
        // position), and cycle_group_'s vacuum_check proceeds on the sealed cup.
        // Obstacle handling is unaffected: early_abort_obstacle breaks the loop
        // before this point and sets any_obstacle_out for the rescue path.
        if (stop_on_first_seal) {
            if (apply_stop_on_first_seal()) {
                std::cout << "[disable_seal] stop_on_first_seal — every stop-group satisfied,"
                             " skip pushing remaining\n";
                break;
            }
        }
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
                    /*fwd*/0, acc, (settings_.pusher_rpm_disable_slow.load()),
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
                if (st.phase_current > (settings_.disable_phase_current_limit_ma.load())) {
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
                if (p <= (settings_.vacuum_seal_deep_kpa.load())) {
                    Z_(slaves[i]).motion_control_driver_EN(true);
                    sleep_ms_(50);
                    if (Z_(slaves[i]).get_system_status() == false) {
                        final_pulse[i] = deg_to_pulse(Z_(slaves[i]).status.real_pos);
                    }
                    done[i] = true;
                    std::cout << "[disable_seal:" << slaves[i] << "] SEALED iter=" << iter
                              << " wait=" << wait_e << "ms p=" << p
                              << "kPa pulse=" << final_pulse[i] << "\n";
                    // [2026-07-14 per user, 2026-07-23 group-aware revision]
                    // Keep polling the REST of this tick's not-done slaves
                    // (don't break the per-slave for-loop here) — with
                    // per-group stop domains, another group's cup may still
                    // need this exact tick's read. The group-aware decision
                    // of whether to stop the outer WAIT_SEAL wait entirely
                    // happens once, after this for-loop, below.
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
                    bool slow_plateau = (wait_e - last_improve_ms[i] >= (settings_.vacuum_plateau_ms.load()));
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
                            if (!errf && fresh_p <= (settings_.vacuum_seal_deep_kpa.load())) {
                                done[i] = true;
                                std::cout << "[disable_seal:" << slaves[i] << "] iter " << iter
                                          << " RESCUED at wall — fresh_p=" << fresh_p
                                          << "kPa <= " << (settings_.vacuum_seal_deep_kpa.load())
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
            // [2026-07-14 per user, 2026-07-23 group-aware revision]
            // stop_on_first_seal: a group with a real seal stops waiting for
            // its OWN remaining member (frozen in place by
            // apply_stop_on_first_seal, same finalization as the MAX_ITERS
            // wrap-up) — but only exits THIS WHILE LOOP once EVERY group is
            // satisfied, so a still-unsatisfied group's cup keeps getting the
            // full WAIT_SEAL window it needs. cycle_group_/do_step_sync_
            // proceed once their own per-side bar (>=1 sealed) is met either
            // way; a still-unsealed non-frozen cup falls through to feet_topup
            // (alt gait) or the retry-skip logic (sync gait) afterward.
            if (stop_on_first_seal) {
                if (apply_stop_on_first_seal()) {
                    std::cout << "[disable_seal] stop_on_first_seal — every stop-group satisfied"
                                 " mid-WAIT_SEAL, stop waiting (prompt proceed)\n";
                    break;   // exit WAIT_SEAL immediately → wrap-up + next-iter-top break
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
            if (!errf && fresh_p <= (settings_.vacuum_seal_deep_kpa.load())) {
                done[i] = true;
                std::cout << "[disable_seal:" << slaves[i]
                          << "] RESCUED MAX_ITERS — pulse=" << final_pulse[i]
                          << " fresh_p=" << fresh_p << "kPa <= "
                          << (settings_.vacuum_seal_deep_kpa.load())
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
bool WashRobot::smart_extend_subset_(const std::string& group, const std::vector<int>& slaves,
                                      bool stop_on_first_seal,
                                      const std::vector<int>* stop_group_ids) {
    if (slaves.empty()) return false;
    if (group != "right" && group != "left" && group != "all" && group != "feet") {
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
        // [v2] all groups are feet cups (1-4) — cap target to guard snowball.
        extend_pulses[i] = feet_target_capped_(s);
    }

    const int extend_rpm = PUSHER_RPM;
    const int extend_acc = PUSHER_ACC;

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

    // disable_seal handles Phase 1 fast → Phase 2 iter loop internally.
    // any_obstacle_out passed but ignored — smart_extend (manual GUI path)
    // does NOT trigger obstacle rescue; obstacle still gets logged inside
    // disable_seal via "OBSTACLE" line + EVT obstacle_detected.
    bool any_obstacle = false;
    if (pusher_extend_with_disable_seal_(slaves, extend_pulses, extend_rpm, extend_acc, &any_obstacle,
                                          stop_on_first_seal, /*max_iters=*/0, stop_group_ids)) {
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

// [v2] groups are the two vertical sides: right{1,2} / left{3,4}.
//   "all" (and legacy alias "feet") = all four cups.
//   body/center groups retired.
std::vector<int> WashRobot::group_slaves_(const std::string& group) const {
    std::vector<int> all;
    // Slave numbers below are the CURRENT ones (2026-08-27: 1-4 → 5-8).
    //
    // ⚠⚠ [2026-08-28 user 指出] "right"/"left" 這兩組**跟實體不符**。
    // ✅ [2026-08-28] 分側已修正：right={5,7}、left={6,8}（實體排列見 WASH_ROBOT.h
    //    的 ZDT_RF1 註解）。在此之前 right 是 {5,6}＝「一邊各拿一顆」，
    //    所以任何分側判準都算不準；那段警告已隨常數修正一併移除。
    if (group == "right")       all = {ZDT_RF1, ZDT_RF2};                     // {5,7} 右上/右下
    else if (group == "left")   all = {ZDT_LF1, ZDT_LF2};                     // {6,8} 左上/左下
    else if (group == "all" || group == "feet")
                                all = {ZDT_RF1, ZDT_RF2, ZDT_LF1, ZDT_LF2};   // {5,7,6,8}
    if (disabled_zdt_slaves_.empty()) return all;
    std::vector<int> out;
    for (int s : all)
        if (!disabled_zdt_slaves_.count(s)) out.push_back(s);
    return out;
}

int WashRobot::group_valve_ch_(const std::string& group) {
    if (group == "right") return CH_VALVE_RIGHT;
    if (group == "left")  return CH_VALVE_LEFT;
    return -1;
}

// Per-slave preset extend pulse. [v2] 只剩 4 顆吸盤：上面那對 / 下面那對。
// 📌 這裡吃的是 RF1/LF1（上）與 RF2/LF2（下），所以 2026-08-28 修正左右歸屬時
//    這段一行都不用改 —— 結構本來就對，錯的只有常數的值。
int WashRobot::preset_extend_pulse_for_slave_(int slave) const {
    if (slave == ZDT_RF1 || slave == ZDT_LF1) return (settings_.pusher_extend_feet_pulse.load());          // upper = 5,6
    if (slave == ZDT_RF2 || slave == ZDT_LF2) return (settings_.pusher_extend_feet_pulse_lower.load());    // lower = 7,8
    return PUSHER_EXTEND_PULSE;   // fallback
}

// Convert cm overextension to ZDT pulses.
// 🔴 [2026-08-28] 這裡原本對 feet 用 `20000/7 = 2857`，並在註解宣告 3000 是
//    「多 5%、靜默算錯」。**實機拿尺量的結果相反：3000 才對**（47994 脈衝 = 16cm，
//    另有四條獨立證據，見 WASH_ROBOT.h 的 CUP_PULSE_PER_CM）。
//    `20000 = 7cm` 很可能是量在 v1 的 body 推桿上，08-27 重構時被錯誤套用到 feet。
//    📌 **「更正」本身也需要被驗證。**
// v2 只剩 4 顆吸盤推桿，兩個分支同值，保留 fallback 只為語意明確。
int WashRobot::cm_to_pulses_for_slave_(int slave, double cm) {
    if (slave >= CUP_SLAVE_FIRST && slave <= CUP_SLAVE_LAST)
        return (int)(cm * CUP_PULSE_PER_CM);
    return (int)(cm * CUP_PULSE_PER_CM);   // v2 已無 body 推桿，正常不會走到
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
        const int preset = preset_extend_pulse_for_slave_(s);   // upper=17100, lower=18000
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
    if (group == "all" || group == "feet") {
        bool err = false;
        err |= pqw_set_relay_verified_(CH_VALVE_RIGHT, on);
        // [2026-08-27 per user] 左右現在是同一顆閥（CH_VALVE_LEFT == CH_VALVE_RIGHT），
        // 對同一個 channel 再寫一次只是多一趟 Modbus 來回（含 verify 讀回），跳過。
        // 寫成條件式而非直接刪掉：若之後改回兩顆獨立閥，改常數即可自動恢復雙寫。
        if (CH_VALVE_LEFT != CH_VALVE_RIGHT)
            err |= pqw_set_relay_verified_(CH_VALVE_LEFT,  on);
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

// "sealed enough" 判準。
//
// [2026-07-08 per user] 原規則：每一側 ≥1 顆吸住就算該側錨定夠。
// [2026-08-28 per user] 現規則：**4 顆裡總共有 SEAL_MIN_CUPS_TOTAL(=2) 顆吸住
//   就算 OK**，不再分側。
//
// 為什麼改：ZDT_RF*/LF* 那組左右歸屬跟實體不符（見 WASH_ROBOT.h 的警告），
// 「分側」算出來的答案本來就不對。bench log 已經出現誤觸發——5、6 沒吸到被
// 判成「右側整側全裸」而觸發後退，但實體上 5、6 分屬兩側、另外兩顆還吸著，
// 依實體規則本來應該直接放行。
//
// ⚠ 已知取捨（不是疏漏）：本規則擋不住「吸住的 2 顆剛好在同一側」的情況
//   （例如 5、7 都吸住、6、8 都掉），那時另一側整個懸空但仍會回 true。
//   左右歸屬確認後應改回分側判準。
//
// 回傳 true = 夠吸；out_unsealed 仍然只列「本 group 裡」沒吸到的杯子，
// 供呼叫端做重試 / top-up 的目標清單與 log 使用（這部分語意沒變）。
//
// 實作上只掃一次 4 顆再導出兩個答案 —— vacuum_check_ 每顆要 3 取樣、
// 顆間隔 50ms，分兩次掃 group 會多花一倍 bus 時間。
bool WashRobot::group_seal_ok_(const std::string& group, std::vector<int>& out_unsealed) {
    const std::vector<int> all_unsealed = vacuum_check_("all");
    const std::vector<int> all_slaves   = group_slaves_("all");

    // 本 group 的未吸清單（呼叫端拿去重試/補吸/印 log）
    out_unsealed.clear();
    for (int s : group_slaves_(group))
        if (std::find(all_unsealed.begin(), all_unsealed.end(), s) != all_unsealed.end())
            out_unsealed.push_back(s);

    const int sealed_total = (int)all_slaves.size() - (int)all_unsealed.size();
    return sealed_total >= SEAL_MIN_CUPS_TOTAL;
}

// [2026-07-08 per user] Best-effort top-up of a side's still-unsealed cup(s)
// after >=1 already sealed (see header). Reuses pusher_extend_with_disable_seal_
// so obstacle / wall / weak-seal detection is fully preserved; valve stays ON
// (caller-owned) and is never toggled here → the already-sealed cup on the same
// side keeps holding. On obstacle we do NOT rescue (would drop the sealed cup) —
// just log and leave the cup for the next step's cycle_group_. Fully NON-FATAL.
void WashRobot::feet_topup_unsealed_(const std::string& group) {
    if (check_abort_()) return;
    // Which cup(s) on this side still aren't sealed?
    auto unsealed = vacuum_check_(group);
    if (unsealed.empty()) {
        std::cout << "[topup] " << group << " already fully sealed — skip\n";
        return;
    }
    std::vector<int> targets(unsealed.size(), 0);
    for (size_t i = 0; i < unsealed.size(); ++i)
        targets[i] = feet_target_capped_(unsealed[i]);

    std::cout << "[topup] " << group << " re-sealing unsealed cup(s)={";
    for (size_t i = 0; i < unsealed.size(); ++i) { if (i) std::cout << ","; std::cout << unsealed[i]; }
    std::cout << "} (valve stays ON, best-effort, no rescue)\n";
    evt_("topup_start group=" + group);

    // stop_on_first_seal=false → try to seal every remaining cup this pass.
    // Full obstacle/wall detection runs inside; we capture obstacle only to log
    // it (no rescue reaction — see header). [2026-07-14] max_iters=2: best-effort
    // top-up gives up after 2 pushes so the group-switch gap stays short — an
    // unsealed cup just retries next step (side still anchored by the sealed one).
    bool obstacle = false;
    if (pusher_extend_with_disable_seal_(unsealed, targets, PUSHER_RPM, PUSHER_ACC,
                                         &obstacle, /*stop_on_first_seal=*/false, /*max_iters=*/2)) {
        std::cout << "[topup] " << group << " extend hard-fail — leave unsealed, proceed\n";
    }
    if (obstacle) {
        std::cout << "[topup] " << group << " OBSTACLE during top-up — cup left unsealed"
                     " (no rescue; next step's cycle handles it)\n";
        evt_("topup_obstacle group=" + group);
    }

    auto still = vacuum_check_(group);
    if (still.empty()) {
        std::cout << "[topup] " << group << " now fully sealed\n";
        evt_("topup_done group=" + group + " sealed=all");
    } else {
        std::string m = "topup_done group=" + group + " still_unsealed=";
        for (size_t i = 0; i < still.size(); ++i) { if (i) m += ","; m += std::to_string(still[i]); }
        std::cout << "[topup] " << m << " (proceeding on the sealed cup)\n";
        evt_(m);
    }
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
    for (int s = CUP_SLAVE_FIRST; s <= CUP_SLAVE_LAST; ++s) {   // [v2] 4 cups
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
    for (int s = CUP_SLAVE_FIRST; s <= CUP_SLAVE_LAST; ++s) {
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
    if (current_group == "right") {          // [v2] other side = left
        other = {ZDT_LF1, ZDT_LF2};
    } else if (current_group == "left") {    // other side = right
        other = {ZDT_RF1, ZDT_RF2};
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
            if (!imu_.read_error.load()) { sum += imu_.z; ++n; }
            if (k < SAMPLES - 1) sleep_ms_(GAP_MS);
        }
        return (n > 0 ? sum / n : imu_.z) - imu_roll0_;
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
    const double end_roll = imu_.read_error.load() ? 0.0 : (imu_.z - imu_roll0_);
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

    auto read_roll_avg = [this]() -> double {
        constexpr int SAMPLES = 6;
        constexpr int GAP_MS  = 50;   // ~300ms window, same as follower_imu_level_
        double sum = 0.0; int n = 0;
        for (int k = 0; k < SAMPLES; ++k) {
            if (!imu_.read_error.load()) { sum += imu_.z; ++n; }
            if (k < SAMPLES - 1) sleep_ms_(GAP_MS);
        }
        return (n > 0 ? sum / n : imu_.z) - imu_roll0_;
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
    const double end_roll = imu_.read_error.load() ? 0.0 : (imu_.z - imu_roll0_);
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
                                      std::function<void()> during_body_rail_hook,
                                      std::function<void()> after_body_rail_hook,
                                      bool right_first) {
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
    (void)during_body_rail_hook; (void)after_body_rail_hook;
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
    std::string r = do_step_down_(false, {}, {}, {}, {}, first_step_right_.load());
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
// The anchor valve is NEVER toggled here, so the anchored cups keep holding
// through the 2× stand-off (shared per-side valve stays ON).
std::string WashRobot::do_cross_obstacle_(bool up) {
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
                      : do_step_down_(false, {}, {}, {}, {}, right_first);
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
                   : do_step_down_(false, {}, {}, {}, {}, right_first);
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
    oss << " duty_min=" << pwm_.dutyMinPct()
        << " duty_max=" << pwm_.dutyMaxPct()
        << " freq_lock=" << pwm_.freqMinHz() << "\n";
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
