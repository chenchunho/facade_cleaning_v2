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
    // [2026-08-30 重構階段 4] 機構標定改由 axis_profile 提供，編譯進去的常數是
    // fallback。設定檔不存在 → 行為逐位元不變（見 common/profile.h 的設計規則）。
    // 🔴 注入與訊息必須用**同一個變數** —— 不然「印的值」與「實際生效的值」會分岔，
    //    而那正是本專案最常踩的形狀（`[WARN] crane 192.168.5.17` 指著一個
    //    根本沒被連過的位址）。
    const double rail_lead   = profile::num("axis_profile", "ARM_RAIL_LEAD_CM_PER_REV",
                                            ARM_RAIL_LEAD_CM_PER_REV);
    const double rail_travel = profile::num("axis_profile", "ARM_RAIL_TRAVEL_MAX_CM",
                                            ARM_RAIL_TRAVEL_MAX_CM);
    D_(DM2J_ARM).set_lead_cm_per_rev(rail_lead);
    D_(DM2J_ARM).set_travel_limit_cm(0.0, rail_travel);
    std::cout << "[OK] DM2J arm rail (slave " << DM2J_ARM << " @ cli_20_)"
              << " lead=" << rail_lead << " cm/rev"
              << " travel<=" << rail_travel << " cm\n";

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
        std::cout << "[OK] IMU " << ep_imu
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
    else if (key == "step_cm_max")                    bad = apply_to_atomic_<int>   (settings_.step_cm_max,                    value, 5,     STEP_CM_MAX);   // [2026-08-31] 原本寫死 100 —— 與 STEP_CM_MAX 脫鉤，改常數改不動這裡（開機載入 settings.json 也走這條）
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


// [2026-08-30 重構階段 5] 以下的「commands」與其後的流程已切到
// app/wash_robot_commands.cpp。分界是本檔原有的分節，不是任意切一刀。
