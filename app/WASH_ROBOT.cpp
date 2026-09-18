#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
// [2026-08-31] resolve_crane_ip_ 的有界可達性探測所需（非阻塞 connect + select）
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cerrno>
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
    // [2026-09-10] warn=2000 沿用舊常數當**觀測**門檻（它至少是有來歷的數字）；
    // abort=0 = 關閉，見宣告處的三步落地說明。
    , crane_wd_warn_ms_(WATCHDOG_TIMEOUT_MS)
    , crane_wd_abort_ms_(0)
    , crane_idle_ms_max_(0)
    , crane_idle_ms_max_motion_(0)
    , crane_wd_warned_(false)
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
    // 🆕 [2026-09-03 per user] `.21` = QX-DO24 專用匯流排。
    // 🔴 **連不上只警告、不中止**，與 `.20`/`.22` 不同：
    //   那兩條上面是推桿、繼電器、壓力計 —— 沒有它們機器不能動、也不能安全停下。
    //   而風扇是附屬裝置（本來就有 PWM_ENABLED 開關可以整組關掉）。
    //   新裝一個網關沒接好就讓整台起不來，是把可用性賠給一個附屬功能。
    //   ⚠️ 代價是「`.21` 沒接」與「`.21` 接了但不通」在啟動時看起來一樣（都只有一行警告），
    //     所以這行訊息要明確說出它的後果：風扇指令會失敗。
    const std::string ep_usr21 = ep::host("USR21", IP_485_2);
    const int         pt_usr21 = ep::port("USR21", PORT_485);
    const bool usr21_ok = cli_21_.connectToServer(ep_usr21, pt_usr21);
    if (!usr21_ok) {
        std::cout << "[WRN] USR .21 (" << ep_usr21 << ":" << pt_usr21
                  << ") 連線失敗 —— 風扇（QX-DO24）指令將全部失敗，其餘功能不受影響\n";
    }

    std::cout << "[OK] USR .20 (ZDT/PQW/rail) / .22 (JC100/XKC) connected"
              << (usr21_ok ? " / .21 (PWM) connected\n" : " / .21 (PWM) NOT connected\n");

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
    // [2026-09-17 per user「指定位置 −3 cm 不會動」] Driver-level guard must match the app-level
    // window (−travel..+travel): the 09-17 negative-rail change only widened cmd_rail_move, so the
    // driver still rejected every negative target ("PR_move_cm_nowait -3.000 cm REJECTED").
    D_(DM2J_ARM).set_travel_limit_cm(-rail_travel, rail_travel);
    // ⚠️ [2026-08-31] 「presence not probed」不是隨手加的免責聲明，是**事實陳述**：
    //    本 driver 的 init() 不做任何匯流排交易（只綁 client、設 slave 號），所以這行 [OK]
    //    只證明「軟體物件建好了」，**不證明裝置在線**。裝置實體拔掉一樣會印 [OK]。
    //    📌 用詞比照同一段裡本來就誠實的 XKC / QX-DO24 兩行。
    //    🔴 這個坑 2026-08-31 由 PQW 實證：模組被實體移除，init 照印 [OK]，下游
    //       set_water_inlet_() 全部靜默失敗（PQW 當日已補 FC01 存在性探測）。
    //    🟡 **未替本行的裝置加探測是刻意的決定**：本函式的失敗路徑是 [FATAL] + 中止整個
    //       init，替 DM2J/ZDT×4/JC-100×4 共 9 個裝置加探測 = 9 個新的開機失敗點，
    //       一次匯流排抖動就開不了機。要加的話得比照 PQW 帶重試，是獨立的決定。
    std::cout << "[OK] DM2J arm rail (slave " << DM2J_ARM << " @ cli_20_)"
              << " lead=" << rail_lead << " cm/rev"
              << " travel=±" << rail_travel << " cm"
              << " (presence not probed)\n";

    // ZDT slave 5..8 on cli_20_ ([v2] 4 cups: right{5,7} / left{6,8}，2026-08-28 修正)
    // [2026-08-27 per user] slave 1-4 → 5-8，見 WASH_ROBOT.h CUP_SLAVE_FIRST。
    for (int i = CUP_SLAVE_FIRST; i <= CUP_SLAVE_LAST; ++i) {
        if (Z_(i).init(cli_20_, i, dbg)) {
            std::cerr << "[FATAL] ZDT slave " << i << " init fail\n"; return true;
        }
    }
    // presence not probed —— 理由同上方 DM2J 那段的說明。
    std::cout << "[OK] ZDT " << CUP_SLAVE_FIRST << "~" << CUP_SLAVE_LAST
              << " (presence not probed)\n";

    // JC-100 slave 5..8 ([v2] one vacuum-pressure sensor per cup)
    // 與 ZDT 同號（推桿 slave N 末端的吸盤 = 真空表 slave N），分屬 .20/.22 兩條 bus，不衝突。
    for (int i = CUP_SLAVE_FIRST; i <= CUP_SLAVE_LAST; ++i) {
        if (M_(i).init(cli_22_, i, dbg)) {
            std::cerr << "[FATAL] JC-100 slave " << i << " init fail\n"; return true;
        }
    }
    // presence not probed —— 理由同上方 DM2J 那段的說明。
    std::cout << "[OK] JC-100 " << CUP_SLAVE_FIRST << "~" << CUP_SLAVE_LAST
              << " (presence not probed)\n";

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
    lvl_high_.init(cli_22_, XKC_HIGH_SLAVE, dbg);
    std::cout << "[OK] XKC high-water slave " << XKC_HIGH_SLAVE << " (sensor presence not probed)\n";

    // QX-DO24 PWM output (slave 6, same bus). Mode B init does no probe, so a
    // missing module is only discovered on the first pwm command — that's fine
    // here because nothing in the automatic gait depends on it (web panel only).
    // PWM_ENABLED 為 false 時連 init 都不做：Mode B init 只是記下 client+ID 不發包，
    // 但不 init 就能保證任何漏掉 gate 的呼叫路徑也發不出東西到那個 slave 號上。
    // （2026-08-27 曾因 slave 撞 JC100 而停用；2026-08-28 模組改 slave 9 後解除，
    //   沿革見 WASH_ROBOT.h 的 PWM_ENABLED 註解。）
    if (PWM_ENABLED) {
        // [2026-09-03] cli_22_ → cli_21_：改掛獨立匯流排，見 WASH_ROBOT.h IP_485_2 的說明。
        // Slave ID **維持 9**：改位址要寫 reg 0x20，而手冊明載那是「實時保存、斷電記憶」
        // 的暫存器且「請勿頻繁寫入」。獨立匯流排上沒有撞號問題 ⇒ 沒有理由花一次 flash
        // 寫入去改一個不影響任何事的數字。
        pwm_.init(cli_21_, PWM_SLAVE, dbg);
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
    //
    // 🔴 [2026-09-03 per user] **移除 init 繫結，driver 與 tier-2 程式碼路徑保留。**
    //   原本這裡呼叫 weight_[i].init(cli_22_, …) 把兩個從未安裝的裝置綁在感測匯流排上。
    //   Mode B init 本身不發包，所以移除不改變任何流量 —— 但它讓「`.22` 上有什麼」
    //   這件事在原始碼上與實體一致（先前讀 init() 會以為那條上面掛了 7 個裝置，實際 5 個）。
    //   要復用時把兩行 init 加回來、並在此處做一次性探測設定 weight_present_ 即可。
    weight_present_[0].store(false);
    weight_present_[1].store(false);
    std::cout << "[--] DY-500 slaves 10/11 not installed — driver 保留但未繫結任何匯流排\n";

    // Init last_seal_pulse_ to per-slave preset; will be updated by fine_tune on success.
    for (int s = CUP_SLAVE_FIRST; s <= CUP_SLAVE_LAST; ++s)
        last_seal_pulse_[s - 1].store(preset_extend_pulse_for_slave_(s));
    last_feet_max_over_cm_.store(0.0);
    cached_weight_kg_[0].store(-1.0);
    cached_weight_kg_[1].store(-1.0);
    weight_comm_ok_[0].store(false);
    weight_comm_ok_[1].store(false);

    // [2026-08-31] 先決定走有線還是 WiFi（見 resolve_crane_ip_），再做 lazy connect。
    // 這一行讓「eth 串接後要回頭改 CRANE_IP」那條伏筆自動生效，不必記得回來改常數。
    resolve_crane_ip_();

    // Crane (lazy — don't fail boot if crane is down)
    const std::string ep_crane = crane_endpoint_ip_();
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
        // 🔴 [2026-09-01] 軸向修正（證據見 wash_robot_commands.cpp 的
        // do_sync_imu_roll_correct_）：這行是 2026-08-26 垂直安裝時代的映射，
        // 08-27 改回水平安裝後漏改。今天早上的啟動 log 就是證據：
        // 印出 roll=-150.32 pitch=0.922852 —— -150 是 yaw，0.92 才是真正的 roll。
        std::cout << "[OK] IMU " << ep_imu
                  << " roll=" << imu_.x << " pitch=" << imu_.y << "\n";

    // Start background threads
    imu_mon_running_ = true;
    imu_mon_thread_  = std::thread(&WashRobot::imu_monitor_loop_, this);
    imu_push_running_ = true;
    imu_push_thread_  = std::thread(&WashRobot::imu_push_loop_, this);
    std::cout << "[OK] IMU roll push -> crane started (" << IMU_PUSH_PERIOD_MS << "ms)\n";
    std::cout << "[OK] IMU monitor started\n";

    // [2026-09-09] Warm up the estop bypass socket at startup.
    //
    // Why: crane_stop_estop_() is on two emergency paths, and connectToServer()
    // is a BLOCKING connect with no timeout — doing it there would stall the
    // emergency for the OS SYN timeout (~2 min) in exactly the case that matters,
    // an unreachable crane. Connecting once here hands the socket to TCP_client's
    // reconnectLoop (500ms retry), so by the time an emergency arrives it is
    // either already up, or known-down and crane_stop_estop_() can fail fast.
    //
    // Until today this socket was never established at runtime at all: its only
    // user was crane_retract_safe_, which has zero call sites (its weight-stop
    // job moved to the crane as g_retract_tension_stop_kg). The dead function
    // took the live bypass channel down with it.
    //
    // Quiet reconnect: crane_cli_ already reports crane-offline loudly; a second
    // socket to the same host would just double an already-noisy log (45s of
    // crane downtime = 107 log pairs, seen on 2026-08-31).
    // 🔴 [2026-09-09] **刻意不靜音**。原本這裡是 set_quiet_reconnect_log(true)，
    //    但 TCP_client.cpp 的註解早就寫過判準：「arm 是已知永遠不會接上＝純噪音；
    //    而**吊機離線是該被看見的事件**，靜音會把該看的也藏掉。」
    //    急停旁路是安全通道，比主連線更該被看見 —— 同日就因為它靜音，
    //    吊機重啟後這條沒接回來、壞了 40 分鐘完全沒有徵兆。
    //    洗版由 TCP_client 內建的節流處理（頭 3 次逐次印，之後每 30 秒一行摘要，
    //    恢復時一定印），不需要整條靜音。
    if (crane_cli_estop_.connectToServer(crane_endpoint_ip_(), ep::port("CRANE", CRANE_PORT)))
        std::cout << "[OK] crane estop bypass channel connected\n";
    else
        std::cout << "[WARN] crane estop bypass channel not up yet (retrying in background)\n";

    // [2026-09-09] 同理預熱 roll 推送通道 —— 它現在同時是鏈路探針，
    // 迴圈內已不再 connect（見 imu_push_loop_）。
    crane_cli_imu_.set_quiet_reconnect_log(true);
    if (crane_cli_imu_.connectToServer(crane_endpoint_ip_(), ep::port("CRANE", CRANE_PORT)))
        std::cout << "[OK] crane IMU push channel connected\n";
    else
        std::cout << "[WARN] crane IMU push channel not up yet (retrying in background)\n";

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

    // [2026-09-10 per user] 真空幫浦 A/B 輪替 loop。累計目前這顆的 ON 時間，達
    // g_pump_rotate_ms_ 就 make-before-break 換到另一顆（併聯管路，真空不斷）。
    // 計時由 init 武裝（pump_active_since_ms_）；此處只起執行緒。
    pump_rotate_running_.store(true);
    pump_rotate_thread_ = std::thread(&WashRobot::pump_rotate_loop_, this);
    std::cout << "[OK] pump A/B rotation started (每 "
              << (PUMP_ROTATE_MS_DEFAULT / 60000) << " 分輪替，0=停用)\n";

    // [2026-09-11] 上滑台 JOG 監看(deadman + 行程守衛)。只在 jog 中作用。
    rail_jog_mon_running_.store(true);
    rail_jog_mon_thread_ = std::thread(&WashRobot::rail_jog_monitor_loop_, this);
    std::cout << "[OK] rail JOG monitor started (deadman "
              << RAIL_JOG_DEADMAN_MS << "ms)\n";

    // [DISABLED 2026-05-15] crane_keepalive_loop_ thread no longer started.
    // Reason: 14t added it to prevent watchdog false-aborts during long
    // washrobot-side ops, but 14v further analysis showed the underlying bug
    // is zombie TCP socket on crane_cli_ (isConnected=true but dead).
    // New design: no continuous ping. Each crane_cmd_ self-heals on fail.
    // See crane_watchdog_loop_ header comment for rationale.
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

    // [2026-09-14 plan §4 階段 1] First real presence probe. Every [OK] line above
    // that says "presence not probed" is honest: nothing before this point has
    // exchanged a byte with ZDT/DM2J/JC-100/XKC/QX. A failure here is NOT fatal
    // (the operator sees dev_*=0 in status and decides) — see run_selfcheck_().
    {
        std::string detail;
        const bool all_ok = run_selfcheck_(detail);
        std::cout << (all_ok ? "[OK] selfcheck" : "[WARN] selfcheck") << detail << "\n";
    }

    // 🔴 [2026-09-16 per user] **開機盤點:一律「全關全收」,收得回才是 Idle,收不回就 Error。**
    //
    // 起因:改成 systemd 之後 `systemctl restart` 送 SIGTERM —— 程式直接被殺,
    // **不跑 cmd_shutdown,繼電器維持原狀**(幫浦還開、吸盤閥還開、腳還伸著)。
    // 新程序若直接宣稱 Idle,就是「機器貼在牆上,程式卻以為自己什麼都沒做」。
    //
    // per user 拍板(先是 C 案「只承認不動作」,同日改為本案):
    //   開機偵測到**吸附中**或**壓力讀不到** → 直接跑 `emergency_detach_()`(與急停同一支):
    //     關滾刷 → 關水泵 → 收手臂 → 風扇停 → 吸盤閥關 → 等鬆開 → 兩段收腳 → 關幫浦 A/B
    //   · 九步全成功 → **Idle**(那時 Idle 名副其實:沒真空、沒伸出、繼電器全關)
    //   · 任一步失敗 → **留在 Error**,`estop=partial` 指出是哪一步 ⇒ 「收不回」這件事看得見
    // ⚠️ **這是有代價的選擇**:開機與「機器正吊在牆上」會同時發生(停電復電、跳電、重啟服務),
    //    本案等於把上電定義成「鬆手」。per user 2026-09-16 明示採用。
    // 📌 同步執行(不開背景執行緒):此時 TCP server 還沒起來,沒有指令會跟它搶,
    //    而且「收完才開始接指令」正是這個設計要的順序。約 10 s。
    {
        int sealed = 0, readable = 0;
        std::ostringstream pk;
        for (int s = CUP_SLAVE_FIRST; s <= CUP_SLAVE_LAST; ++s) {
            const int p = read_pressure_(s);
            const bool ok = (M_(s).error_flag == 0);
            if (ok) { ++readable; if (p <= VACUUM_THRESHOLD_KPA) ++sealed; }
            pk << (s == CUP_SLAVE_FIRST ? "" : "/") << (ok ? std::to_string(p) : std::string("ERR"));
        }
        if (sealed > 0 || readable == 0) {
            std::cerr << "[WARN] 開機盤點:" << (sealed > 0
                        ? ("**吸附中**(" + std::to_string(sealed) + "/" + std::to_string(readable) + " 顆 ≤ "
                           + std::to_string(VACUUM_THRESHOLD_KPA) + " kPa)")
                        : std::string("四顆壓力**全部讀不到**"))
                      << "(壓力 " << pk.str() << ")—— 上一個程序沒有正常收尾,"
                         "現在執行全關全收(同急停的收回程序,約 10 s)…\n";
            // Error 是 emergency_detach_ 成功時 CAS 的來源狀態;先設好它,收完才會翻成 Idle。
            set_state_(State::Error);
            estop_detach_state_.store(1);
            emergency_detach_active_.store(true);
            emergency_detach_();          // 同步;內部會把 estop_detach_state_ 設成 done/partial
            if (state_.load() == State::Idle) {
                std::cout << "[OK] 開機盤點:全關全收完成 → state=Idle\n";
            } else {
                std::cerr << "[WARN] 開機盤點:**收不回**(estop=partial,見上面逐步 log)→ 留在 Error。"
                             "人工處理後 `reset`,或用 `return_home`。\n";
            }
        } else {
            // 沒吸著:把上一個程序可能留下的繼電器狀態清乾淨,Idle 才名副其實。
            pqw_.controlRelay(CH_BRUSH,        false);
            pqw_.controlRelay(CH_WATER_PUMP,   false);
            pqw_.controlRelay(CH_VALVE_RIGHT,  false);
            if (CH_VALVE_LEFT != CH_VALVE_RIGHT) pqw_.controlRelay(CH_VALVE_LEFT, false);
            pqw_.controlRelay(CH_BREAK_VACUUM, false);
            pqw_.controlRelay(CH_PUMP_A,       false);
            pqw_.controlRelay(CH_PUMP_B,       false);
            pump_active_since_ms_.store(0);
            std::cout << "[OK] 開機盤點:未吸附(壓力 " << pk.str()
                      << ") → 繼電器全關、state=Idle\n";
        }
    }

    return false;
}

void WashRobot::stop() {
    abort_flag    = true;
    motion_active_ = false;
    imu_mon_running_ = false;
    if (imu_mon_thread_.joinable()) imu_mon_thread_.join();
    imu_push_running_ = false;
    if (imu_push_thread_.joinable()) imu_push_thread_.join();
    imu_.stop();
    crane_wd_running_ = false;
    if (crane_wd_thread_.joinable()) crane_wd_thread_.join();
    // [2026-06-09] Stop water-inlet watchdog. Last-chance force close (one
    // attempt only — process is shutting down, no point retrying long).
    water_inlet_watchdog_running_.store(false);
    if (water_inlet_watchdog_thread_.joinable()) water_inlet_watchdog_thread_.join();
    if (water_inlet_open_ts_ms_.load() != 0) {
        std::cerr << "[water_inlet] stop(): valve still armed open — sending final close\n";
        set_water_inlet_(false);
    }

    // [2026-09-10] Stop pump-rotation loop. Turning the process off leaves the
    // relay module's last state latched, so a mid-swap exit could strand BOTH
    // A and B on (harmless — parallel manifold) or the wrong one on. Force both
    // off here so the next boot starts from a known "all pumps off" state; init
    // re-opens A. (One best-effort attempt each — process is shutting down.)
    pump_rotate_running_.store(false);
    if (pump_rotate_thread_.joinable()) pump_rotate_thread_.join();
    pump_active_since_ms_.store(0);
    pqw_.controlRelay(CH_PUMP_A, false);
    pqw_.controlRelay(CH_PUMP_B, false);

    // [2026-09-11] Stop rail JOG monitor + last-chance jog_stop (滑台若還在 jog,
    // 關機時務必停下,別讓它撞硬限位)。
    rail_jog_mon_running_.store(false);
    if (rail_jog_mon_thread_.joinable()) rail_jog_mon_thread_.join();
    if (rail_jog_dir_.load() != 0) {
        rail_jog_dir_.store(0);
        D_(DM2J_ARM).jog_stop();
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

bool WashRobot::check_abort_() {
    while (pause_flag.load() && !abort_flag.load()) sleep_ms_(POLL_INTERVAL_MS);
    return abort_flag.load();
}

const char* WashRobot::state_name(State s) {
    switch (s) {
        case State::Idle:      return "idle";
        case State::Ready:     return "ready";
        case State::Attached:  return "attached";
        case State::Running:   return "running";
        case State::Paused:    return "paused";
        case State::Error:     return "error";
        default:               return "unknown";
    }
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

// [2026-08-31] 有界的 TCP 可達性探測：非阻塞 connect + select，逾時即放棄。
// ⚠️ 刻意不用 TCP_client::connectToServer —— 它是**無逾時的 blocking connect**，
//    對不存在的主機會卡滿 TCP SYN timeout（實測約兩分鐘）。用它來「試試看有線通不通」
//    會讓沒串 eth 的現況下每次開機先卡兩分鐘，比不做還糟。
bool WashRobot::tcp_reachable_(const std::string& ip, int port, int timeout_ms) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port   = htons((uint16_t)port);
    if (::inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) { ::close(fd); return false; }

    bool ok = false;
    if (::connect(fd, (sockaddr*)&a, sizeof(a)) == 0) {
        ok = true;                       // 立即連上（同網段常見）
    } else if (errno == EINPROGRESS) {
        fd_set wf; FD_ZERO(&wf); FD_SET(fd, &wf);
        timeval tv{ timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        if (::select(fd + 1, nullptr, &wf, nullptr, &tv) > 0) {
            // 🔴 select 說可寫**不等於**連上 —— 必須查 SO_ERROR。
            //    這正是 2026-08-28 修過的那個缺陷（連到沒人聽的埠也判定成功）。
            int err = 0; socklen_t len = sizeof(err);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) ok = true;
        }
    }
    ::close(fd);
    return ok;
}

// [2026-08-31] 開機決定 crane 走有線還是 WiFi。只在 init 呼叫一次。
void WashRobot::resolve_crane_ip_() {
    const std::string wifi = CRANE_IP;
    const std::string eth  = CRANE_IP_ETH;
    const int         port = ep::port("CRANE", CRANE_PORT);

    // 🔴 環境變數覆蓋存在時完全不探測 —— common/endpoints.h 的設計規則是
    //    「沒設環境變數時行為必須位元等價」，等價性測試靠它。有覆蓋就照用。
    // [2026-09-08] 判準改為「環境變數有沒有設」，不是「解析值等不等於常數」。
    // 舊寫法在覆蓋值 == CRANE_IP 時無法與「沒設」區分（見 endpoints.h 的說明）。
    if (ep::has_host_override("CRANE")) {
        crane_ip_resolved_ = ep::host("CRANE", CRANE_IP);
        std::cout << "[crane] 位址由環境變數覆蓋 = " << crane_ip_resolved_
                  << "（不做有線探測）\n";
        return;
    }

    if (tcp_reachable_(eth, port, CRANE_ETH_PROBE_MS)) {
        crane_ip_resolved_ = eth;
        std::cout << "[crane] 走**有線** " << eth << ":" << port
                  << "（探測通過）— WiFi " << wifi << " 未使用\n";
    } else {
        crane_ip_resolved_ = wifi;
        std::cout << "[crane] 有線 " << eth << " 探測不通（" << CRANE_ETH_PROBE_MS
                  << "ms）→ 走 WiFi " << wifi << ":" << port << "\n";
    }
}

// [2026-09-08] 連往吊機的**單一位址來源**（宣告處有完整理由）。
// crane_ip_resolved_ 為空表示 init 還沒跑到 resolve（或被略過）→ 退回原常數，
// 行為與 2026-08-31 之前完全相同。
// 執行緒安全：crane_ip_resolved_ 只在 init 的 resolve_crane_ip_() 寫入一次，
// 而 IMU 推送執行緒與 estop 路徑都在 init 完成之後才啟動 ⇒ 其後全為唯讀。
std::string WashRobot::crane_endpoint_ip_() const {
    return crane_ip_resolved_.empty()
         ? ep::host("CRANE", CRANE_IP) : crane_ip_resolved_;
}

bool WashRobot::crane_connect_if_needed_() {
    if (crane_cli_.isConnected()) return false;
    return !crane_cli_.connectToServer(crane_endpoint_ip_(), ep::port("CRANE", CRANE_PORT));
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
    // uses existing TCP connection (or fresh connect if not connected). If the
    // SEND fails (nothing transmitted), force-close the socket and reconnect
    // on the second attempt. 🔴 [2026-09-16] A receive timeout does NOT retry
    // any more — see the comment at the bottom of the loop. This handles "zombie socket":
    // isConnected()=true but actually dead (e.g. NAT entry evicted, peer kernel
    // restart didn't send RST). Without this, the only fix was program restart
    // or operator manually toggling crane_attached.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt == 1) {
            // Force fresh socket: close current then reconnect.
            std::cout << "[crane_cmd] '" << line << "' attempt 1 failed — force reconnect\n";
            crane_cli_.close();
            crane_rx_buf_.clear();   // bytes from the dead socket mean nothing on the new one
        }
        if (crane_connect_if_needed_()) {
            if (attempt == 1) {
                std::cout << "[crane_cmd] '" << line << "' reconnect failed\n";
                return "";
            }
            continue;   // try fresh reconnect on next attempt
        }

        // [2026-09-14] Anything queued on the socket BEFORE we send belongs to
        // the past: EVT broadcasts (dispatch them) or a late reply to a call
        // that already timed out (drop it — it must not become THIS reply).
        // Gate on available() (MSG_PEEK|MSG_DONTWAIT) so an idle socket costs
        // nothing; 🔴 receiveData(…, 0) is NOT non-blocking — SO_RCVTIMEO=0
        // means "wait forever" — hence the peek first and a 10 ms read after.
        // 🔴 This must run BEFORE sendData(): TCP_client::sendData() silently
        //    discards up to 4096 queued bytes (a Modbus-gateway habit). On this
        //    line-oriented channel that (a) throws away EVT broadcasts — a
        //    queued `EVT tension_alarm` would vanish — and (b) when more than
        //    4096 bytes are queued (motion_progress flood) the cut lands
        //    mid-line and the TAIL becomes the next "reply". Reading everything
        //    into the line buffer first leaves sendData() nothing to cut.
        {
            char pre[512];
            for (int guard = 0; guard < 2048 && crane_cli_.available() > 0; ++guard) {
                const int n = crane_cli_.receiveData(pre, sizeof(pre), 10);
                if (n <= 0) break;
                crane_rx_buf_.append(pre, n);
            }
            crane_rx_consume_pending_(line);
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
        // [2026-09-14] `rx` is now the persistent crane_rx_buf_ (see header):
        // a partial line left after the reply is kept for the next call.
        std::string& rx = crane_rx_buf_;
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
        // 🔴🔴 [2026-09-16] Receive timeout → give up. NEVER fall through to
        // attempt 1 here. The command already LEFT the socket; the crane may
        // have executed it, may be executing it, or may have died half-way.
        // Resending it after a reconnect re-runs a MOTION command with no
        // idea what happened in between. That is exactly what happened today:
        // `goto 0` was in flight when the user cut crane power for
        // maintenance; 105 s later the body timed out, the crane had just
        // been powered back on, the retry reconnected and resent `goto 0`,
        // and the crane started paying out rope with a person next to it
        // (body log 18:46:22 "'goto 0' attempt 1 failed — force reconnect").
        // Only a *send* failure (nothing transmitted) is allowed to retry —
        // same rule arm_cmd_ has had since 2026-06-03 for DEPLOY / PARK.
        // Zombie-socket self-healing is kept by closing the socket so the
        // NEXT call reconnects fresh; it just never resends THIS line.
        std::cout << "[crane_cmd] '" << line << "' no reply in " << timeout_sec
                  << "s — NOT retrying (already sent; a resend after reconnect would re-run it). "
                     "Socket closed; next call reconnects.\n";
        crane_cli_.close();
        crane_rx_buf_.clear();
        return "";
    }
    std::cout << "[crane_cmd] '" << line << "' FAILED after 2 attempts (send failed twice)\n";
    return "";   // both attempts failed to even transmit
}

// [2026-09-14] See crane_rx_buf_ in the header. Caller holds crane_mtx_.
void WashRobot::crane_rx_consume_pending_(const std::string& ctx) {
    size_t pos;
    while ((pos = crane_rx_buf_.find('\n')) != std::string::npos) {
        std::string one = crane_rx_buf_.substr(0, pos);
        crane_rx_buf_.erase(0, pos + 1);
        if (!one.empty() && one.back() == '\r') one.pop_back();
        if (one.empty()) continue;
        if (one.rfind("EVT ", 0) == 0) { handle_crane_evt_(one); continue; }
        // A reply nobody is waiting for = the answer to an earlier call that
        // gave up. Refresh the link timestamp (the crane clearly answered) but
        // never hand it to the command about to be sent.
        if (one.rfind("OK", 0) == 0) crane_last_ok_ms_ = now_ms_();
        std::cout << "[crane_cmd] stale reply dropped before '" << ctx << "': "
                  << one.substr(0, 80) << "\n";
    }
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
    return arm_cmd_locked_(line, timeout_sec);
}

// [2026-09-16] Non-blocking arm_ready refresh for cmd_status. try_lock: a DEPLOY
// or sweep holding arm_mtx_ can last many seconds and a 2 Hz status poll must
// never queue behind it — skipping a refresh is harmless, blocking is not.
void WashRobot::arm_status_refresh_() {
    if (!arm_attached_.load() || !arm_cli_.isConnected()) return;
    const int64_t now = now_ms_();
    if (now - last_arm_status_ms_.load() < ARM_STATUS_REFRESH_MS) return;
    std::unique_lock<std::mutex> lk(arm_mtx_, std::try_to_lock);
    if (!lk.owns_lock()) return;
    last_arm_status_ms_.store(now);
    arm_cmd_locked_("STATUS", 2);   // reply parsed by note_arm_status_ inside
}

std::string WashRobot::arm_cmd_locked_(const std::string& line, int timeout_sec) {
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
                    if (line == "STATUS") note_arm_status_(one);   // [2026-09-14] feeds arm_ready
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
        // [2026-09-15] INIT 成功後立刻刷新 arm_ready 的來源。
        // 🔴 `arm_ready` 只由 `arm_cmd_("STATUS")` 的回覆餵(note_arm_status_),而 INIT 自己
        //    不送 STATUS ⇒ 按完 INIT 後 status 仍是 `arm_ready=0`,要等下一次有人送 arm_status
        //    才翻成 1。GUI 前置 ⑤ 讀的正是這個欄位,症狀就是「按了 INIT 沒反應」。
        arm_cmd_("STATUS", 3);
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

    // 🔴 [2026-09-03] 先送 ENABLE —— **PARK 會停用兩顆馬達**，而停用狀態下 DEPLOY
    //   會被 motor_api 以 `ERR motor not enabled; send ENABLE first` 擋掉。
    //   於是「arm_park 之後 arm_deploy」這個很自然的順序會**靜默失敗**：
    //   指令送得出去、log 印得出 [arm] DEPLOY、回傳是 ERR，但手臂一動也不動。
    //   09-03 把清潔動作整合進 cycle_test 時就是踩到這個 —— 而 cyc.py 一直能跑，
    //   只因為它在每個週期開頭自己補了 `M1 ENABLE; M2 ENABLE`（見該檔註解「PARK 會停用馬達」）。
    //   ⇒ 那個 workaround 說明缺陷在編排層，不在呼叫端。修在這裡，所有呼叫端一次受益。
    // ⚠️ 不檢查 ENABLE 的回傳：已經 enabled 時再 enable 是無害的冪等操作，
    //   而真正的失敗會在下一行的 DEPLOY 顯現（那裡才有姿態驗證）。
    arm_cmd_("M1 ENABLE", 5);
    arm_cmd_("M2 ENABLE", 5);

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

// [2026-09-04 per user] 力控貼合的代轉。與 cmd_arm_deploy 的差別只有一個：
// 送 DEPLOY_F <target_nm> 而不是 DEPLOY <wall_mm>，其餘（ENABLE 前置、stow 狀態）相同。
//
// 🔴 **刻意不做 verify_arm_deploy_**：那支是拿「wall_mm 推算出的預期 θ」去比對實際 θ，
//   而 DEPLOY_F 的整個前提就是不預設 θ 在哪。障礙物判斷已經在 motor_api 側用
//   theta_min/theta_max 兩個守衛做掉了，回傳字串本身就分得出 no_wall / obstacle。
//   在這裡再套一次舊的幾何檢查等於把剛拆掉的假設又裝回來。
// ⚠️ 回傳保留 motor_api 的原字串（OK tau=... / ERR ... no_wall / obstacle），
//   呼叫端要判讀就看它，不要只看 OK/ERR 前綴。
std::string WashRobot::cmd_arm_deploy_f(double target_nm, const std::string& slot, int dist_mm) {
    if (target_nm <= 0.0) return "ERR invalid_target_nm\n";
    if (target_nm > ARM_FORCE_MAX_NM) return "ERR target_nm_exceeds_max_7\n";   // [2026-09-17 per user] 防呆
    std::string s = slot;
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    if (s != "LEFT" && s != "CENTER" && s != "RIGHT")
        return "ERR invalid_slot (LEFT|CENTER|RIGHT)\n";
    std::ostringstream oss;
    oss << "DEPLOY_F " << target_nm << " " << s;
    // [2026-09-17 per user] Manual 手臂卡「壓上距離」: 壓力/距離雙上限,先到先停。
    // 距離(機身到玻璃,mm)用 motor_api 的實測幾何 `mm = 490·sin(θ−0.38)+121` 反推成 θ_max
    // (motor_api main_api.h 2026-09-02 三點擬合;M1 上限 θ=1.10 ⇒ 最遠 444 mm),
    // 送 `DEPLOY_F nm slot θ_min θ_max hold_at_max`。0 = 不限距離(舊行為)。
    if (dist_mm > 0) {
        const double sin_arg = (dist_mm - 121.0) / 490.0;
        if (sin_arg <= 0.0 || sin_arg >= 1.0) return "ERR dist_mm_range (122..444)\n";
        double th_max = std::asin(sin_arg) + 0.38;
        // 🔴 Beyond the M1 hard limit the "limit" would be no limit at all — reject,
        //    don't clamp (first test: 500 mm clamped to 1.10 and did a full press).
        if (th_max > 1.10) return "ERR dist_mm_range (122..444)\n";
        if (th_max <= 0.570) return "ERR dist_mm_too_close (θ_max ≤ θ_min 0.570)\n";
        oss << " 0.570 " << std::fixed << std::setprecision(4) << th_max << " hold_at_max";
    }

    // 與 cmd_arm_deploy 同一個理由：PARK 會停用馬達，不先 ENABLE 就會靜默失敗。
    arm_cmd_("M1 ENABLE", 5);
    arm_cmd_("M2 ENABLE", 5);

    std::cout << "[arm] " << oss.str() << "\n";
    // 逾時放大到 120s：DEPLOY_F 有尋觸與收斂迭代，09-04 首次冷啟動實測 44 秒
    // （暖啟動後較短）。用 DEPLOY 的 30s 會在尋觸中途就逾時。
    std::string r = arm_cmd_(oss.str(), 120);
    if (r.rfind("OK", 0) == 0)
        arm_stow_state_.store(s == "CENTER" ? ArmStowState::Center : ArmStowState::Unknown);
    return r + "\n";
}

std::string WashRobot::cmd_arm_park() {
    std::cout << "[arm] PARK\n";
    std::string r = arm_cmd_("PARK", 30);
    // [arm rope protect TEMP 2026-05-21]
    if (r.rfind("OK", 0) == 0) arm_stow_state_.store(ArmStowState::Parked);
    return r + "\n";
}

// [2026-09-11 per user] 收手臂但**不失能**(收回 M1、馬達保持通電 holding)。
// 為什麼要有:PARK 會失能,而 per user「失能只在校正位置時,其他狀態都不該失能」——
// 失能時手臂會因無保持力而亂跑/漂。清潔流程每組合之間收手臂用這個,不用 PARK。
// 走 M1 MOVETO 0(非同步),送出後等 M1 回到 ~0(moving=0)才回,好讓呼叫端知道收妥。
// [2026-09-17 per user] Tool-slot passthrough. After the squeegee pass the script
// puts M2 back on the roller (the standby / default tool) so a mission never
// ends with the squeegee out; motor_api's LR_SLOT already refuses while M1 is
// still on the glass, so no extra guard here.
// [2026-09-17 per user] Manual 手臂卡「即時修改力道 → 套用」:已壓在牆上時只跑收斂,不重新尋觸。
std::string WashRobot::cmd_arm_force(double target_nm) {
    if (target_nm <= 0.0) return "ERR invalid_target_nm\n";
    if (target_nm > ARM_FORCE_MAX_NM) return "ERR target_nm_exceeds_max_7\n";
    std::ostringstream oss; oss << "M1 SETFORCE " << target_nm;
    std::cout << "[arm] " << oss.str() << "\n";
    std::string r = arm_cmd_(oss.str(), 30);
    if (r.empty()) return "ERR arm_no_reply\n";
    if (r.back() != '\n') r.push_back('\n');
    return r;
}

std::string WashRobot::cmd_arm_slot(const std::string& slot) {
    if (slot != "LEFT" && slot != "CENTER" && slot != "RIGHT") return "ERR usage:arm_slot_<LEFT|CENTER|RIGHT>\n";
    std::cout << "[arm] SLOT " << slot << "\n";
    std::string r = arm_cmd_("M2 LR_SLOT " + slot, 30);
    if (r.empty()) return "ERR arm_no_reply\n";
    if (r.back() != '\n') r.push_back('\n');
    return r;
}

std::string WashRobot::cmd_arm_retract() {
    std::cout << "[arm] RETRACT (M1->0, keep enabled)\n";
    std::string r = arm_cmd_("M1 MOVETO 0", 30);
    if (r.rfind("OK", 0) != 0) return r + "\n";     // MOVETO 送出即失敗
    std::this_thread::sleep_for(std::chrono::milliseconds(400));   // 讓移動起動,避免一進來就讀到 moving=0
    for (int i = 0; i < 40; ++i) {                   // 上限 ~8s
        const std::string s = arm_cmd_("M1 STATUS", 3);
        double pos = 1.0; int moving = 1;
        const auto pp = s.find("pos=");
        if (pp != std::string::npos) pos = std::strtod(s.c_str() + pp + 4, nullptr);
        const auto mp = s.find("moving=");
        if (mp != std::string::npos) moving = (int)std::strtol(s.c_str() + mp + 7, nullptr, 10);
        if (moving == 0 && std::fabs(pos) < 0.06) {
            arm_stow_state_.store(ArmStowState::Unknown);   // 收回但未 park(仍通電)
            std::ostringstream oss; oss << "OK arm_retract pos=" << pos << "\n";
            return oss.str();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return "OK arm_retract (逾時等 M1 回零,可能未完全收回)\n";
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
    // [2026-08-31] 一併把「真實運動時間」算給 monitor 當減速遮罩的錨點。
    // 起點取目前座標；讀不到就退回 0（保守：距離估得較長→遮罩較晚，寧可晚遮不要早遮，
    // 早遮等於在還在巡航時就關掉偵測）。
    double from_cm = 0.0;
    if (D_(DM2J_ARM).read_position_cm(from_cm)) from_cm = 0.0;   // false = 成功（本專案慣例）
    const int motion_ms = arm_rail_motion_ms_(target_cm, from_cm, rpm, acc, dec);
    if (motion_ms > 0)
        std::cout << "[arm_sweep] 推算運動時間 " << motion_ms << "ms（"
                  << from_cm << "→" << target_cm << "cm @" << rpm << "rpm）"
                  << " — 減速遮罩錨點；est_ms=" << est_ms << "ms 僅為監看逾時\n";
    arm_monitor_during_sweep_(est_ms, motion_ms);
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
void WashRobot::arm_monitor_during_sweep_(int est_ms, int motion_ms) {
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
        // 🔴 [2026-08-31] 遮罩改錨定「真實運動時間」，不再錨定 est_ms。
        //
        // 舊寫法 `elapsed > est_ms - MASK` 有兩個問題，而且互相掩蓋：
        //  ① **est_ms 是「監看多久」不是「動多久」**。它被刻意設得很長
        //     （3900/4500ms），註解說「估太長只是多等一下，safe、no correctness
        //     impact」——**那句是錯的**：遮罩錨在 est_ms 結尾，估太長就把遮罩
        //     整個推到真實運動之外。17cm @250rpm 實際只要約 578ms（實測 553ms），
        //     而遮罩窗口是 3500~4500ms → **真實減速期完全沒有遮罩**，
        //     而迴圈又會在 motion complete 時 early-exit，根本走不到窗口。
        //     ⇒ **這道保護從加進來就沒生效過，而且不會有任何訊息。**
        //  ② est_ms ≤ MASK 時 `est_ms - MASK ≤ 0` → 條件恆為真 →
        //     **整趟偵測全程關閉**，同樣靜默。所以「把 est_ms 調小」這個
        //     直覺修法會直接關掉偵測。
        //
        // 兩者的耦合來自「同一個數字被當成兩種語意」。分開之後：
        //   est_ms    = 監看逾時（可以估得寬鬆，估太長只是多等）
        //   motion_ms = 真實運動時間（遮罩錨點，要盡量接近事實）
        //
        // ⚠️ motion_ms 是**推算值**（arm_rail_motion_ms_，用實測導程 7.731）。
        //    bench 有機會時應拿 log 的 "motion complete at t=Xms" 回頭校驗。
        // ⚠️ **本次修改未經實機驗證**（需要實際掃動才能觀察 tau 行為）。
        //    motion_ms <= 0 時退回舊行為，行為與修改前完全相同。
        const int  mask_anchor    = (motion_ms > 0) ? motion_ms : est_ms;
        const bool in_decel_mask  = (elapsed > mask_anchor - ARM_SWEEP_DECEL_MASK_MS);

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
    // [2026-09-16] Dead half removed: the θ-vs-wall_mm check below the early return
    // had been unreachable since 2026-06-06 and was superseded by DEPLOY_F's own
    // no_wall / obstacle / cannot_reach replies (2026-09-11). git has the old body.

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

// [2026-09-17 per user] "Stow" = M1 back to 0, motors STAY ENABLED. The arm is
// ready (roller slot, enabled) from power-up, an emergency only pulls it back to
// that state, and nothing in operation may de-energise it — a de-energised arm
// swings freely while the machine hangs in the air. The only place PARK
// (home + disable) is still legitimate is calibration, via the manual
// `arm_park` command. Every former operational PARK below now goes through
// cmd_arm_retract().
bool WashRobot::ensure_arm_parked_after_rope_(const std::string& ctx) {
    if (!ARM_ROPE_PROTECTION) return false;
    std::cout << "[arm_protect] " << ctx << " — RETRACT (M1 → 0, keep enabled)\n";
    if (cmd_arm_retract().rfind("OK", 0) != 0) {
        std::cerr << "[arm_protect] retract failed — non-fatal, arm may still be deployed\n";
        return true;   // log only, don't block flow
    }
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
            // 🔴 [2026-09-02 per user] 滾筒繼電器改到 DEPLOY **之前** 打開。
            //   原本是「先把滾筒壓上玻璃、再讓它開始轉」——靜止的滾筒頂著玻璃才起轉，
            //   對滾筒與玻璃都不好，而且清洗段的頭幾公分等於乾磨。
            //   本段與 do_step_sync_rail_sweep_ 是複製關係，**該處 08-28 就已改對**，
            //   這一份被落下（又一次「兩份只改一份」）。政策一併對齊那邊：
            //   開關窗口 = 「DEPLOY RIGHT 之前開 → DEPLOY LEFT 之前關」，
            //   **DEPLOY RIGHT 失敗不提早關**，關閉點的閘改用 init_ok（見下方兩處）。
            pqw_.controlRelay(CH_BRUSH, true);
            deployed = (arm_cmd_(oss_brush.str(), 30).rfind("OK", 0) == 0);
            if (deployed) {
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
            // 🔴 [2026-09-02] CH_BRUSH 無條件關（對齊 do_step_sync_rail_sweep_ 的
            //   08-28 政策：「沒開過時關它是 no-op，開著沒關才是問題」）。
            //   原本 gate 在 deployed —— DEPLOY RIGHT 失敗時滾筒會一直轉沒人關。
            pqw_.controlRelay(CH_BRUSH, false);
            if (init_ok) cmd_arm_retract();   // [2026-09-17] was PARK — keep enabled
            return "ERR aborted\n";
        }

        // 🔴 [2026-09-02] 換邊條件 deployed → init_ok（對齊 08-28 的同型修正：
        //   per user 當時回報「從頭到尾都是 DEPLOY RIGHT 沒換」，成因就是
        //   DEPLOY RIGHT 失敗 ⇒ deployed=false ⇒ 整段含 DEPLOY LEFT 被跳過，
        //   手臂停在原位、滑台空掃兩趟，而且滾筒沒人關）。
        if (init_ok) {
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
            cmd_arm_retract();   // [2026-09-17] was PARK — keep enabled
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
            set_water_inlet_(false);   // [2026-06-05] → crane water valve (2026-09-10: ZS-DIO 4CH @ .32 slave 1, was .34 PQW slave 12)
        });
        // [2026-05-29] PARK timeout 30s → 10s (fast fail when motor_api 沒回覆,
        // 避免 cleanup 卡 30s × 2 attempts = 60s)。
        std::string r = arm_cmd_("M1 MOVETO 0", 10);   // [2026-09-17] was PARK — keep enabled
        if (r.rfind("OK", 0) == 0) {
            arm_stow_state_.store(ArmStowState::Unknown);
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
            if (set_water_inlet_(true)) {   // [2026-06-05] → crane water valve (2026-09-10: ZS-DIO 4CH @ .32 slave 1, was .34 PQW slave 12)
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
            // 🔴 [2026-09-02 per user] 滾筒繼電器改到 DEPLOY **之前** 打開（原本在
            //   verify 之後）。理由同 do_arm_clean_sweep_：靜止的滾筒頂著玻璃才起轉。
            //   ⚠️ 開了就必須在下面每一條失敗出口關掉（!deploy_ok / verify 障礙）。
            //   🟡 乾式那輪（!water_on）仍是明確關閉滾筒——那個 gate 是否合理，
            //      per user 表示之後再處理，本次不動。
            if (water_on) {
                if (pqw_set_relay_verified_(CH_BRUSH, true)) {
                    std::cerr << "[arm_clean_sweep_cont] pqw ON brush FAIL\n";
                    return false;
                }
            }
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
                    // [2026-09-17] was PARK(釋放 M2 + settle);改成收 M1 不失能 —— 失能的手臂在空中會亂跑
                    arm_cmd_("M1 MOVETO 0", 10);
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
                if (water_on) pqw_.controlRelay(CH_BRUSH, false);   // [2026-09-02] 別讓滾筒空轉
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
                if (water_on) pqw_.controlRelay(CH_BRUSH, false);   // [2026-09-02] 別讓滾筒空轉
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
                // [2026-09-02] 原本的 brush ON 已移到 DEPLOY 之前（見上），此處不再開。
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
// tension_retract_stop) into atomic flag for watchdog to escalate to PausedOnError.
// [2026-09-09] Split out of handle_crane_evt_ so a background reader can record
// alarms without paying the full handler's cost.
//
// The full handler ends in evt_() -> TCP_server::broadcast(), which holds
// clients_mtx across a BLOCKING send() per client (SEND_FLAGS is MSG_NOSIGNAL,
// never MSG_DONTWAIT; the client sockets are never made non-blocking and carry
// no SO_SNDTIMEO). Today that back-pressure lands on crane_cmd_, which has
// second-scale timeouts and tolerates it. It must never land on the 4Hz roll
// push: that path has a hard 750ms cliff (IMU_ROLL_STALE_MS), so one slow
// browser client would stall the pusher and silently drop the crane's balance
// back to src=meter — our own GUI causing the exact failure the tunnel caused
// on 2026-09-08.
//
// So: record-only here (atomics + one short mutex), no printing, no broadcast.
// Safe to call from any thread.
//
// Returns true if a tension_retract_stop was suppressed by balance calibration,
// so the full handler can log that without re-testing the same condition.
bool WashRobot::record_crane_evt_(const std::string& line) {
    bool suppressed = false;
    if (line.find("tension_alarm") != std::string::npos ||
        line.find("tension_retract_stop") != std::string::npos) {
        // [2026-06-02 v7, per Sadie bench] Suppress tension_retract_stop during
        // balance calibration. During cal (especially after Phase 2/3 cup
        // release) all robot weight transfers to ropes, easily pushing
        // total tension >50kg even at normal load. Letting this fire causes
        // crane_watchdog to escalate PausedOnError repeatedly mid-cal,
        // which corrupts the post-cal state machine. tension_alarm (per-side
        // peak) still fires — only the total-sum gate is suppressed.
        if (balance_cal_running_.load() &&
            line.find("tension_retract_stop") != std::string::npos) {
            suppressed = true;
        } else {
            std::lock_guard<std::mutex> lk(crane_alarm_mtx_);
            if (line.find("tension_retract_stop") != std::string::npos)
                crane_alarm_kind_ = "tension_retract_stop";
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
    return suppressed;
}

// Full EVT handling: record, print, re-broadcast to the GUI.
// 🔴 Stays on the crane_cmd_ path ONLY — see record_crane_evt_ for why the
// broadcast must not reach the roll-push thread.
void WashRobot::handle_crane_evt_(const std::string& line) {
    std::cout << "[crane_evt] " << line << "\n";
    if (record_crane_evt_(line))
        std::cout << "[crane_evt] suppressed (balance cal in progress): "
                  << line << "\n";
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

// [2026-09-09] Send "stop" to the crane over the dedicated estop channel.
//
// Why this exists as a helper: emergency paths MUST NOT go through crane_cmd_,
// whose very first act is an unconditional `std::lock_guard lk(crane_mtx_)`.
// A motion command holds that mutex for the whole motion (return_home's pay_out
// budget is 300s), so an emergency stop sent on the main channel does not fail
// — it *waits*, which is worse. crane_cli_estop_ is a separate socket with its
// own mutex precisely so a stop can overtake an in-flight motion.
//
// Returns true only when the crane acknowledged with OK. Callers in emergency
// paths must surface a false — a stop that never reached the crane means the
// ropes are still moving while the washrobot believes it has stopped.
//
// NOTE: this does not consult crane_attached_. Detached mode is a bench
// convenience; an emergency stop should always try to reach real hardware.
//
// Bounded by design: worst case is send 500ms + recv 1000ms. It never calls
// connectToServer() (blocking connect, no timeout); the socket is warmed at
// init and maintained by TCP_client's 500ms reconnectLoop.
// [2026-09-09] 丟棄急停通道上排隊的 EVT 廣播。**兩個理由，第二個比較嚴重：**
//
//  ① `crane_stop_estop_` 送完 stop 只讀一次就判斷，會先讀到排隊中的 EVT
//     → 開頭不是 "OK" → 回報「CRANE STOP NOT ACKED — ropes may still be moving」，
//     **即使 stop 其實成功了**。急停之後最不需要的就是一個假警報。
//
//  ② 吊機的 `broadcast_evt()` 對**所有**連線送，而這條 client 從來不讀
//     ⇒ 接收緩衝區只漲不消。滿了之後吊機那端的 `send()` 會**阻塞**
//       （`SEND_FLAGS` 只有 `MSG_NOSIGNAL`、沒有 `SO_SNDTIMEO`、socket 也不是非阻塞），
//       而 `TCP_server::broadcast()` 是**持著 `clients_mtx`** 在送
//       ⇒ 整條 EVT 廣播路徑卡死，連帶拖住其他 client。
//     📌 所以 watchdog 會週期性呼叫本函式，讓佇列常態是空的。
//
// ⚠️ `receiveData` 的 timeout 走 `SO_RCVTIMEO`，**傳 0 在 Linux 是「永不逾時」＝永久阻塞**，
//    不是「立刻返回」。這裡一律傳正值。
int WashRobot::estop_drain_locked_(int budget_ms) {
    int dropped = 0;
    char buf[512];
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(budget_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const int n = crane_cli_estop_.receiveData(buf, sizeof(buf), 20);
        if (n <= 0) break;   // 0 = 佇列空了；-1 = 斷線，交給 reconnectLoop
        dropped += n;
    }
    return dropped;
}

bool WashRobot::crane_stop_estop_() {
    std::lock_guard<std::mutex> elk(crane_estop_mtx_);
    // 🔴 Deliberately NO connect attempt here. connectToServer() blocks with no
    // timeout, and an unreachable crane is precisely the emergency case — a
    // ~2 min stall on this thread would be far worse than a fast, loud failure.
    // The socket is opened at init and kept alive by TCP_client's reconnectLoop.
    if (!crane_cli_estop_.isConnected()) {
        // 🔴 [2026-09-09] 改為**有界自救**。原本是「完全不 connect、直接失敗」，
        //    理由是 connectToServer() 是沒有逾時的阻塞 connect（~127s），而
        //    「對端不可達」正是急停情境 —— **那個理由成立，但結論下錯了**：
        //    正解不是「永不連」，是「連，但有界」。
        //    同日實測：吊機重啟後這條通道再也沒接回來，於是這個分支變成
        //    「永遠走這裡、永遠失敗」，急停等於沒有旁路。
        //    connectWithTimeout 走 reconnectLoop 同一份 nb_connect（非阻塞 +
        //    select 逾時 + SO_ERROR），最壞情況就是 ESTOP_CONNECT_MS。
        std::cout << "[crane_stop_estop] channel down — bounded reconnect ("
                  << ESTOP_CONNECT_MS << "ms)\n";
        if (!crane_cli_estop_.connectWithTimeout(crane_endpoint_ip_(),
                                                 ep::port("CRANE", CRANE_PORT),
                                                 ESTOP_CONNECT_MS)) {
            std::cout << "[crane_stop_estop] WARN: reconnect failed — stop NOT sent\n";
            return false;
        }
    }
    // 🔴 先排空再送 —— ACK 不能排在一堆 EVT 廣播後面（見 estop_drain_locked_）。
    //    常態下 watchdog 已清空，這裡是 no-op。
    const int dropped = estop_drain_locked_(ESTOP_DRAIN_MS);
    if (dropped > 0)
        std::cout << "[crane_stop_estop] drained " << dropped
                  << " queued bytes before sending\n";

    const char* tx = "stop\n";
    if (!crane_cli_estop_.sendData(tx, 5, 500)) {
        std::cout << "[crane_stop_estop] WARN: send failed — stop NOT sent\n";
        return false;
    }

    // 🔴 到這裡 stop **已經在線上了**。以下只是「確認」。
    //    「沒送出去」與「送了但沒收到確認」對現場的意義完全不同
    //    （前者繩子一定還在動，後者多半已經停了），所以訊息刻意分開寫 ——
    //    急停之後看 log 的人要靠這個差別決定下一步。
    std::string rx;
    char buf[256];
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(ESTOP_ACK_MS);
    while (std::chrono::steady_clock::now() < deadline) {
        const int n = crane_cli_estop_.receiveData(buf, sizeof(buf), 100);
        if (n < 0) {
            std::cout << "[crane_stop_estop] WARN: link dropped waiting ACK "
                      << "— stop WAS sent\n";
            return false;
        }
        if (n == 0) continue;                 // 逾時，還沒到
        rx.append(buf, (size_t)n);
        size_t pos;
        while ((pos = rx.find('\n')) != std::string::npos) {
            std::string one = rx.substr(0, pos);
            rx.erase(0, pos + 1);
            if (!one.empty() && one.back() == '\r') one.pop_back();
            if (one.empty()) continue;
            if (one.rfind("EVT", 0) == 0) continue;   // 廣播，不是我的回覆
            return one.rfind("OK", 0) == 0;           // 第一個非 EVT 行就是答案
        }
    }
    std::cout << "[crane_stop_estop] WARN: no ACK within " << ESTOP_ACK_MS
              << "ms — stop WAS sent (command is on the wire, ACK not seen)\n";
    return false;
}

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

double WashRobot::rope_weight_limit_per_sensor_kg_() const {
    // State-aware: cups holding → low limit; hanging on rope → high limit.
    State s = state_.load();
    // [2026-09-16] 狀態收斂後 ReturningHome 併進 Running,但它的重量上限屬於「吊著」那組
    // ⇒ 改用 returning_home_ 旗標判斷,行為與收斂前逐位元相同。
    if (returning_home_.load()) return (settings_.rope_weight_limit_hanging.load());
    switch (s) {
        case State::Attached:
        case State::Running:
        case State::Paused:
            return (settings_.rope_weight_limit_attached.load());
        case State::Idle:
        case State::Ready:
        case State::Error:
        default:
            return (settings_.rope_weight_limit_hanging.load());
    }
}

// 🔴 [2026-09-10] 上面原本有三行「Per-side retract until both L/R tension >=
// target_kg … Used by bal_cal_preload_」—— **那是 crane_retract_to_weight_ 的說明，
// 不是這支的**。該函式的本體在某次改動中被刪掉，宣告留在 WASH_ROBOT.h:2309
// （只有宣告、無定義、無呼叫點），註解則留在原地黏到了下一個函式頭上。
// 已移除，避免下一個人照它去理解 watchdog。
// 📌 通則：刪函式本體時，它的檔頭註解會靜默地變成下一個函式的檔頭註解。
//
// crane watchdog：每 HEARTBEAT_INTERVAL_MS 醒來一次，做三件事——
//   ① 週期性排空急停通道（防死鎖，見下方註解）
//   ② 把 handle_crane_evt_ 收到的張力警報升級為 PausedOnError
//   ③ 🆕 [2026-09-10] 鏈路逾時比較（觀測 → 中止，兩段門檻）
//
// 🔴 **③ 是補回來的。** 原本的「吊機逾時未回應就中止動作」比較
// （`now - crane_last_ok_ms_ > WATCHDOG_TIMEOUT_MS`）在 4d1409c(06-22) 之前就消失了，
// 而 `crane_last_ok_ms_` 的三個寫入點、`WATCHDOG_TIMEOUT_MS`、以及三段
// 「為了不讓 2s watchdog 誤觸發」的解釋註解**全部留在原地** ⇒ 讀這段碼的人會
// 以為保護存在。舊實作見 `6abd8c6:user_lib/WASH_ROBOT.cpp:391`。
// 📌 **通則：判斷保護在不在，要找「誰讀這個值」，不是「誰寫這個值」。**
//     寫入點、常數、餵食執行緒、解釋註解可以全部健在，而比較那一行不存在。
//
// ⚠️ **今天預設仍不會中止任何運動**（`crane_wd_abort_ms_ = 0`）——
//    門檻要先量再訂，理由見 WASH_ROBOT.h 的宣告處。
// 完整分析見 .claude/handoff/ai2-watchdog-handoff.md。
void WashRobot::crane_watchdog_loop_() {
    while (crane_wd_running_.load()) {
        sleep_ms_(HEARTBEAT_INTERVAL_MS);
        if (!crane_wd_running_.load()) break;
        if (!crane_attached_.load()) continue;

        // 🔴 [2026-09-09] 週期性排空急停通道。**這不是整潔，是防死鎖**：
        //    吊機對所有連線廣播 EVT，而這條 client 平常從來不讀 ⇒ 接收緩衝區只漲不消，
        //    滿了之後吊機的 send() 會阻塞，而 TCP_server::broadcast() 是持著 clients_mtx
        //    在送 ⇒ 整條 EVT 路徑卡死（理由詳見 estop_drain_locked_）。
        //    順帶讓急停路徑的排空常態是 no-op。
        //    ⚠️ try_lock：急停正在進行時直接跳過，絕不排在它後面。
        {
            std::unique_lock<std::mutex> elk(crane_estop_mtx_, std::try_to_lock);
            if (elk.owns_lock() && crane_cli_estop_.isConnected()) {
                const int dropped = estop_drain_locked_(50);
                if (dropped > 0)
                    std::cout << "[crane_watchdog] estop channel drained "
                              << dropped << " bytes\n";
            }
        }

        // Crane safety alarm (set by handle_crane_evt_ when EVT tension_alarm
        // / tension_retract_stop drained from any crane_cmd_'s recv stream).
        // Per Q3=(a) 2026-05-07 design: escalate to PausedOnError so operator
        // must inspect before next motion.
        if (crane_alarm_pending_.exchange(false)) {
            std::string kind, detail;
            {
                std::lock_guard<std::mutex> lk(crane_alarm_mtx_);
                kind   = crane_alarm_kind_;
                detail = crane_alarm_detail_;
            }
            // [2026-09-17 per user] Only pause when there is something to pause.
            // A crane tension alarm while the body is Idle/Ready (e.g. the operator
            // pulling with Manual ▲ tripped up_stop_total_kg — the crane already
            // did hold_all_off) used to drop the body into Paused(error), and from
            // there crane_goto is refused ⇒ "按放到地面不會動" with no obvious cause
            // until someone finds `reset`. With no flow running the alarm is
            // information, not a state change: log + EVT, state untouched.
            const State cur_st = state_.load();
            const bool flowing = (cur_st == State::Running) || step_in_progress_.load();
            if (!flowing) {
                std::cout << "[crane_watchdog] CRANE ALARM " << kind
                          << " while " << state_name(cur_st) << " (no flow) — logged only. Detail: " << detail << "\n";
                evt_("crane_alarm kind=" + kind + " state=" + state_name(cur_st) + " action=none");
                continue;
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
                if (!(state_.load() == State::Paused
                      && pause_reason_.load() == (int)PauseReason::Error))
                    state_before_pause_ = state_.load();
            }
            pause_reason_.store((int)PauseReason::Error);
            set_state_(State::Paused);
        }

        // ③ [2026-09-10] 鏈路逾時比較 —— 補回 4d1409c(06-22) 之前存在的那一段。
        //    per user 拍板：「本體發現吊機異常是要終止」。
        //    ⚠️ 走到「終止」之前先走「觀測」，理由見 WASH_ROBOT.h 宣告處。
        {
            const int64_t last_ok = crane_last_ok_ms_.load();
            if (last_ok == 0) {
                // 從未與吊機成功往來過（開機到第一道指令之間）。此時 idle 沒有意義
                // —— 若拿 now-0 去比，開機當下就會立刻逾時。照 imu_roll_fresh() 的
                // 慣例：st==0 一律視為「還沒有資料」而不是「資料很舊」。
            } else {
                const int64_t idle_ms = now_ms_() - last_ok;

                // 峰值先記，且**不受任何門檻影響** —— 這是第②步要用的數字，
                // 門檻設多少都不該改變我們量到什麼。
                const bool in_motion = motion_active_.load();
                int64_t prev_max = crane_idle_ms_max_.load();
                while (idle_ms > prev_max &&
                       !crane_idle_ms_max_.compare_exchange_weak(prev_max, idle_ms)) {}
                if (in_motion) {
                    int64_t prev_mm = crane_idle_ms_max_motion_.load();
                    while (idle_ms > prev_mm &&
                           !crane_idle_ms_max_motion_.compare_exchange_weak(prev_mm, idle_ms)) {}
                }

                const int warn_ms  = crane_wd_warn_ms_.load();
                const int abort_ms = crane_wd_abort_ms_.load();

                if (warn_ms > 0 && idle_ms > warn_ms) {
                    if (!crane_wd_warned_.exchange(true)) {
                        std::cout << "[crane_watchdog] link idle " << idle_ms
                                  << "ms > warn " << warn_ms << "ms"
                                  << " (motion_active=" << (in_motion ? 1 : 0)
                                  << ", abort_ms=" << abort_ms << ")\n";
                        evt_("crane_watchdog_idle idle_ms=" + std::to_string(idle_ms) +
                             " motion=" + (in_motion ? "1" : "0"));
                    }
                } else {
                    crane_wd_warned_.store(false);   // 回到門檻內 → 下次再逾時會重報
                }

                // 🔴 只有在 abort_ms 被明確設成 >0 之後，這裡才會動 abort_flag。
                //    預設 0 ⇒ 這一段今天是純觀測，不會中止任何運動。
                if (abort_ms > 0 && idle_ms > abort_ms && in_motion) {
                    if (!abort_flag) {
                        std::cerr << "[crane_watchdog] LINK TIMEOUT " << idle_ms
                                  << "ms > abort " << abort_ms
                                  << "ms — aborting motion\n";
                        evt_("crane_watchdog_timeout idle_ms=" + std::to_string(idle_ms) +
                             " abort_ms=" + std::to_string(abort_ms));
                        abort_flag = true;
                    }
                }
            }
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

// [2026-09-01] 把 IMU roll 推給吊機，供其 IMU 驅動平衡迴路使用。
//
// 為什麼需要：吊機的平衡控制器原本以「計米器左右差」為誤差，而該訊號有兩道
// 天花板都大於 per user 的 ±1° 規格 —— 1cm 量化，以及鋼索彈性遲滯
// （2026-09-01 實測：同樣繩長 168/169 出現 roll +0.21° 與 +1.90°，差 1.69°）。
// IMU 量的是真實姿態，繞過兩者。控制律不變，只換誤差來源。
//
// 設計要點：
//  🔴 獨立執行緒 —— 不可以放進 imu_monitor_loop_（那是 45° 傾斜緊急停止的偵測
//     迴圈，網路一卡就會延遲保護）。
//  🔴 第三條連線 crane_cli_imu_ —— 不搶 crane_mtx_，因為 do_step_sync_ 期間
//     主連線正阻塞在 pay_out_* 的回覆等待上（同 crane_cli_estop_ 的理由）。
//  🔴 **一律推送，不只在移動中推**。原計畫寫「非移動中不推」，實作時改成一律推：
//     這樣資料永遠是新鮮的，動作一開始就能用；而吊機端的過期退回機制
//     （IMU_ROLL_STALE_MS）就只在**真正的故障**（本體掛掉／網路斷）時才觸發
//     —— 那才是它該有的語意。成本是每 250ms 一行短指令，可忽略。
//  🔴 失敗完全靜默且不重試 —— 這是「錦上添花」的資料流，不可以拖慢或吵到
//     任何東西。吊機端資料過期就自動退回計米器路徑，本身就是安全的降級。
void WashRobot::imu_push_loop_() {
    std::string rxbuf;   // [2026-09-09] 跨輪保存的行緩衝，只有本執行緒碰得到
    while (imu_push_running_.load()) {
        sleep_ms_(IMU_PUSH_PERIOD_MS);
        if (!imu_push_running_.load()) break;
        if (!crane_attached_.load())   continue;
        if (imu_.read_error.load())    continue;   // 讀不到就不推，讓吊機端自然過期

        const double roll = imu_.x - imu_roll0_;
        std::ostringstream oss;
        oss << "set_imu_roll " << std::fixed << std::setprecision(2) << roll << "\n";
        const std::string line = oss.str();

        std::lock_guard<std::mutex> lk(crane_imu_mtx_);
        // 🔴 [2026-09-09] 這裡刻意不再 connect。connectToServer() 是**沒有逾時的
        //    阻塞 connect()**（TCP_client.cpp:99）—— 對端不可達時整輪卡 ~127 秒
        //    （OS SYN timeout）。以前只是「推送變慢」，現在這條迴圈同時是鏈路探針，
        //    卡住等於**在最該報警的情境下瞎掉**。socket 改由 init 預熱、
        //    TCP_client 的 reconnectLoop（500ms）維持。
        if (!crane_cli_imu_.isConnected()) continue;   // 靜默重試於下一輪
        if (!crane_cli_imu_.sendData(line.c_str(), (int)line.size(), 200)) continue;

        // 讀掉回覆避免堆積在接收佇列 —— 2026-09-01 吊機端的 DSZL 失步
        // （Recv-Q 卡 13 bytes、txid 對不上）就是沒把回覆讀乾淨造成的。
        char buf[128];
        const int n = crane_cli_imu_.receiveData(buf, sizeof(buf), 100);
        if (n <= 0) continue;
        crane_peer_last_rx_ms_.store(now_ms_());   // 收到任何位元組＝鏈路活著

        // 🔴 [2026-09-09] 行緩衝是**必要項不是保險**：receiveData 是單次 recv 且
        //    `recv(sock, buf, bufSize - 1, ...)` ⇒ 128 的 buf 單次最多 127 bytes，
        //    而吊機的 `EVT device_state` 一行遠超過 ⇒ **必定被切成兩次以上**。
        //    不做緩衝的話 find("tension_alarm") 對半行回 npos ⇒ 警報被靜默吃掉，
        //    正好是這次要補的那個洞的同一種失敗形狀。
        //    用區域變數（跨迴圈保存）而不是成員：只有這條執行緒碰得到。
        rxbuf.append(buf, (size_t)n);
        // 防呆：對端若吐出無換行的垃圾，不要無上限長大。
        if (rxbuf.size() > 8192) rxbuf.clear();
        size_t nl;
        while ((nl = rxbuf.find('\n')) != std::string::npos) {
            std::string one = rxbuf.substr(0, nl);
            rxbuf.erase(0, nl + 1);
            if (!one.empty() && one.back() == '\r') one.pop_back();
            // 🔴 只走 record（不印、不轉播）—— 理由見 record_crane_evt_ 的說明。
            //    也刻意不用 stod 之類會丟例外的東西：這條迴圈一停，roll 推送就
            //    永久停止且沒人會重啟，750ms 後吊機平衡靜默降級。
            if (one.rfind("EVT", 0) == 0) record_crane_evt_(one);
        }
    }
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
                // [2026-09-09] Was crane_cmd_("stop", 2) with the result dropped.
                // Two defects in one line: (a) crane_cmd_ blocks on crane_mtx_,
                // which a motion command can hold for its whole duration — and a
                // 45 deg tilt is most likely *while the crane is moving*, i.e.
                // exactly when the mutex is held; (b) the result was ignored, so a
                // stop that never reached the crane looked identical to one that
                // did. Go through the estop channel (no crane_mtx_) and surface a
                // failure: the washrobot has aborted, but the ropes have not.
                if (!crane_stop_estop_()) {
                    std::cerr << "[imu_monitor] 🔴 CRANE STOP NOT ACKED — ropes may still be moving\n";
                    evt_("imu_emergency crane_stop_failed");
                }
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
                    // [2026-09-16] WaitingConfirm 併進 Paused(reason=balance_ask)。
                    // 沒有使用者出口(confirm_balance 在 v2 已移除),由下方 roll 回穩時自己還原。
                    if (cur == State::Running || cur == State::Attached) {
                        state_before_wait_ = cur;
                        pause_reason_.store((int)PauseReason::BalanceAsk);
                        set_state_(State::Paused);
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
                    if (state_.load() == State::Paused
                        && pause_reason_.load() == (int)PauseReason::BalanceAsk)
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
    if (rpm <= 0) rpm = pusher_rpm_extend_();
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
    if (rpm <= 0) rpm = pusher_rpm_extend_();
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

    // 🔴 [2026-09-15 per user] **已經在目標位置的從站不下命令。**
    //
    // 目標是絕對位置(mode=1,手冊 3.2.11 Reg 0x00FD 的 mode 欄,已對照確認),照理說
    // 「命令到你已經在的地方」應該完全不動;實機不是這樣:09-15 吸附狀態下重按
    // 「伸 raw 10cm」,三支實時位置 2998.7/3018.9/3088°(目標 30000 脈衝 ≈ 3000°),
    // 殘差只有 10~90 脈衝,但吸盤吸在玻璃上、機體被真空拉住 ⇒ 連這點殘差都走不掉,
    // 韌體 150 ms 判堵轉、峰值 3.0~3.2 A,整組回失敗 → PAUSE-ON-ERROR。
    // ⇒ 殘差在容差內就當作已到位,連命令都不送(不送就不會有堵轉)。
    // ⚠️ 讀不到位置時**照送**:維持既有行為,寧可多送一次也不要因為讀取失敗就不動作。
    std::vector<int> todo;
    todo.reserve(slaves.size());
    for (int s : slaves) {
        if (Z_(s).get_system_status()) { todo.push_back(s); continue; }   // 讀失敗 → 照送
        const double cur_pulse = Z_(s).status.real_pos * PUSHER_PULSE_PER_DEG;
        if (std::fabs(cur_pulse - (double)pulse) <= PUSHER_AT_TARGET_TOL_PULSE) {
            std::cout << "[pusher_move_many ZDT:" << s << "] 已在目標 ("
                      << (long)std::lround(cur_pulse) << " vs " << pulse
                      << " 脈衝, 容差 " << (int)PUSHER_AT_TARGET_TOL_PULSE << ") — 不下命令\n";
            continue;
        }
        todo.push_back(s);
    }
    if (todo.empty()) {
        std::cout << "[pusher_move_many] 全部已在目標 — 整組不動作\n";
        return false;
    }

    // sync=1 pattern requires the _nowait variant: enqueue each slave's PR block
    // without internal wait, then broadcast trigger_sync_move, then poll per slave.
    for (int s : todo) {
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
    Z_(todo.front()).trigger_sync_move();   // [2026-09-15] todo 非空(上面已 return)

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

    std::vector<int>      stable(todo.size(), 0);
    std::vector<double>   prev_pos(todo.size(), 1e9);
    std::vector<bool>     done(todo.size(), false);
    std::vector<uint16_t> peak_I(todo.size(), 0);   // peak phase_current per slave
    int n_done  = 0;
    int elapsed = 0;
    int poll_count = 0;

    if (driver_dbg_) for (int s : todo) Z_(s).set_debug(false);

    while (n_done < (int)todo.size() && elapsed < timeout_ms) {
        sleep_ms_(poll_ms);
        elapsed += poll_ms;
        poll_count++;

        for (size_t i = 0; i < todo.size(); ++i) {
            if (done[i]) continue;
            const int s = todo[i];

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

    if (driver_dbg_) for (int s : todo) Z_(s).set_debug(true);

    if (n_done < (int)todo.size()) {
        std::cout << "[wait_many] TIMEOUT after " << timeout_ms << "ms, "
                  << n_done << "/" << todo.size() << " resolved\n";
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
// straight to PUSHER_RETRACT_PULSE at PUSHER_RPM_RETRACT_FULL.
// 🔴 [2026-09-15 per user, 當日定案] 時序:關真空閥 → 靜置 PRE_ON_REST(100) → CH6 ON →
//    立刻送四顆同步收(PRE_MOVE=0,不等到位)→ HOLD_MOVE(300) → CH6 OFF。
//    **正壓包住整個收的動作。** 沿革見 WASH_ROBOT.h 該常數區;舊常數保留定義但不再使用。
// RAII guard closes the valve on every exit path (bench lesson: an early-return
// without closing it left the valve charging indefinitely — "very dangerous").
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
// equivalent because at the old stage-1 rpm (150) the cup moves >1cm/sec — adhesion
// [2026-09-09] 該常數鏈已刪除（見 WASH_ROBOT.h）。本段是 pre-2026-07-31 的歷史說明、
// 刻意保留，但符號已不存在 —— 不要照它去 grep。
// breaks well before delay elapses. If cup over-extended (still in motion at
// end of delay), stage 2 just updates target to 0 + speed jumps to RETRACT_FULL —
// motor smoothly accelerates from current intermediate position. Driver accepts
// new pos_mode_nowait mid-motion (same primitive used in old per-slave path).
// Slaves already past stage 1 endpoint skip stage 1 entirely (absolute stage 1
// target would extend them back toward wall).
// Returns true (error) on any stall during stage 2 wait, or timeout.
bool WashRobot::pusher_two_stage_retract_(const std::vector<int>& slaves, int rpm) {
    if (slaves.empty()) return false;
    if (rpm <= 0) rpm = pusher_rpm_retract_();   // 0/未給 = 共用執行期值(set_pusher_rpm)

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
    if (BREAK_VACUUM_PRE_ON_REST_MS > 0) sleep_ms_(BREAK_VACUUM_PRE_ON_REST_MS);

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
    // [2026-09-15 per user, 當日三修] CH6 ON → (PRE_MOVE=0 → 不等)立刻送四顆同步收
    //   → HOLD_MOVE(500) → CH6 OFF。收腳**送出後不等到位**就往下走(到位交給後面的輪詢),
    //   所以正壓從頭到尾包住整個收的動作。
    if (BREAK_VACUUM_PRE_MOVE_MS > 0) sleep_ms_(BREAK_VACUUM_PRE_MOVE_MS);
    // Direct retract to PUSHER_RETRACT_PULSE for every slave (no slow-peel stage —
    // the break-vacuum charge does that job now). Single sync-trigger fires all.
    for (int s : slaves) {
        if (Z_(s).motion_control_pos_mode_nowait(0, PUSHER_ACC_RETRACT,
                rpm, PUSHER_RETRACT_PULSE,
                /*abs*/1, /*sync*/1, /*retry*/1)) {
            std::cout << "[2stage_retract ZDT:" << s << "] pos_mode_nowait FAIL\n";
            return true;   // RAII guard 會關 CH6
        }
    }
    Z_(slaves.front()).trigger_sync_move();
    // 收腳已經在跑,閥再開 HOLD_MOVE 才關。
    sleep_ms_(BREAK_VACUUM_HOLD_MOVE_MS);
    std::cout << "[2stage_retract] CH" << CH_BREAK_VACUUM << " OFF (charge "
              << (BREAK_VACUUM_PRE_MOVE_MS + BREAK_VACUUM_HOLD_MOVE_MS) << "ms total)\n";
    // ⚠ 關不掉比開不起來更危險:閥持續充氣 → 下次伸腳吸不住。失敗時**不**解除 armed,
    //   讓 RAII guard 在函式結束時再關一次(冪等,多關無害)。
    if (pqw_.controlRelay(CH_BREAK_VACUUM, false)) {
        std::cerr << "[2stage_retract] CH" << CH_BREAK_VACUUM
                  << " OFF FAILED (TCP-level) — 閥可能仍在充氣,交給 RAII guard 再關一次\n";
        evt_("break_vacuum_off_fail ch=" + std::to_string(CH_BREAK_VACUUM));
    } else {
        bv_guard.armed = false;
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
    // [2026-09-17 per user] FAKE-DONE has a second cause besides a stall: the
    // synchronised start never reached the motors (the .20 bus dropped the
    // broadcast — 11:36 today the PQW readback on the same bus returned size=0 at
    // that very instant) and wait_many read a stale "not moving" as done. All four
    // sat at ~1160° with ~500 mA (a stall shows 2–3 A) and emergency_detach gave
    // up with the feet half out. Re-issue the retract ONCE for the slaves that are
    // off target: absolute target 0 is idempotent, the cups are already released,
    // and a real stall will simply stall again and be reported as such.
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::vector<int> off;
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
                          << "° (expected ≈0, tol=±" << RETRACT_VERIFY_TOL_DEG << "°)"
                          << (attempt == 0 ? " — re-issuing retract once (sync-start may have been lost)\n"
                                           : " — still off target after retry, fail\n");
                off.push_back(s);
            }
        }
        if (off.empty()) break;
        if (attempt == 1) return true;
        if (pusher_move_many_(off, 0, rpm, PUSHER_ACC_RETRACT)) {
            std::cout << "[2stage_retract] retry retract FAILED (stall/timeout)\n";
            return true;
        }
    }

    sleep_ms_(PUSHER_SETTLE_MS);
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
    if (fast_rpm <= 0) fast_rpm = pusher_rpm_extend_();
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

    const int extend_rpm = pusher_rpm_extend_();
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
        if (st.empty() || (int)st.size() <= ch - 1) {
            // [2026-09-07] 這條分支原本一行都不印 —— 而它正是「回讀整條掛掉」的路徑。
            // 對照組：下面「重試三次放棄」那條**有**印。於是最該留下證據的情況反而無聲，
            // 唯一徵兆是 cmd_pump 回覆字串裡少了 `ch2=` 欄位；而實查三個呼叫端
            // （console v2 / cycle_test.py / 8080 GUI）**沒有一個看那個字串**，
            // 它們一律各自去讀 relay_status ⇒ **降級狀態等於零證據**。
            // 🔴 語意刻意不變：best-effort、權威留給下游 vacuum check。這裡只補證據。
            std::cout << "[pqw_relay] ch=" << ch << " set " << (on ? "ON" : "OFF")
                      << " readback unavailable (size=" << st.size()
                      << ") — proceeding unverified, downstream check is the authority\n";
            return false;  // can't verify, proceed
        }
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
// [2026-08-28 per user] 曾改為：4 顆裡總共有 SEAL_MIN_CUPS_TOTAL(=2) 顆吸住就算 OK，不再分側。
// ✅ [2026-08-31] **已改回分側判準**（每側各 >= SEAL_MIN_CUPS_PER_SIDE=1）——
//   這正是下方 08-28 註解自己寫下的退場條件「左右歸屬確認後應改回分側判準」，
//   而該條件已達成：實機逐組推吸盤驗證 right={5,7}／left={6,8} 四顆全對。
//
// 為什麼改：ZDT_RF*/LF* 那組左右歸屬跟實體不符（見 WASH_ROBOT.h 的警告），
// 「分側」算出來的答案本來就不對。bench log 已經出現誤觸發——5、6 沒吸到被
// 判成「右側整側全裸」而觸發後退，但實體上 5、6 分屬兩側、另外兩顆還吸著，
// 依實體規則本來應該直接放行。
//
// ⚠ 該規則的已知取捨（不是疏漏）：擋不住「吸住的 2 顆剛好在同一側」的情況
//   （例如 5、7 都吸住、6、8 都掉），那時另一側整個懸空但仍會回 true。
//   ✅ 這個洞正是 2026-08-31 改回分側判準所修掉的。
//
// 回傳 true = 夠吸；out_unsealed 仍然只列「本 group 裡」沒吸到的杯子，
// 供呼叫端做重試 / top-up 的目標清單與 log 使用（這部分語意沒變）。
//
// 實作上只掃一次 4 顆再導出兩個答案 —— vacuum_check_ 每顆要 3 取樣、
// 顆間隔 50ms，分兩次掃 group 會多花一倍 bus 時間。
bool WashRobot::group_seal_ok_(const std::string& group, std::vector<int>& out_unsealed) {
    const std::vector<int> all_unsealed = vacuum_check_("all");

    // 本 group 的未吸清單（呼叫端拿去重試/補吸/印 log）
    const std::vector<int> grp = group_slaves_(group);
    out_unsealed.clear();
    for (int s : grp)
        if (std::find(all_unsealed.begin(), all_unsealed.end(), s) != all_unsealed.end())
            out_unsealed.push_back(s);

    // 單側 group（"right" / "left"）：這一側自己要有 >= SEAL_MIN_CUPS_PER_SIDE 顆吸住。
    // 🔴 [2026-08-31] 這是本函式的行為改變。舊版回傳的是
    //        sealed_total(全部四顆) >= SEAL_MIN_CUPS_TOTAL
    //    ——**完全沒有用到 group 參數**，所以 group_seal_ok_("right") 與
    //    group_seal_ok_("left") 永遠回傳相同的值，而 do_step_sync_ 的
    //    `if (!right_ok || !left_ok)` 因此退化成一個全域的「四顆裡有 >=2 顆」檢查。
    //    後果：右上+右下都吸住、左邊兩顆全空 → sealed_total==2 → 兩個旗標都 true
    //    → 步伐照跑，而整個左側是脫離的。
    auto side_ok = [&](const std::string& g) {
        const std::vector<int> ss = group_slaves_(g);
        // 整側都被 zdt_disable 停用時 ss 為空：沒有杯子可以要求，視為不阻擋，
        // 否則停用一側等於讓步伐永遠無法進行（而停用是使用者的明確意圖）。
        if (ss.empty()) return true;
        int sealed = 0;
        for (int s : ss)
            if (std::find(all_unsealed.begin(), all_unsealed.end(), s) == all_unsealed.end())
                ++sealed;
        return sealed >= SEAL_MIN_CUPS_PER_SIDE;
    };

    if (group == "all" || group == "feet")
        return side_ok("right") && side_ok("left");   // 兩側各自都要達標
    return side_ok(group);
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
    if (pusher_extend_with_disable_seal_(unsealed, targets, pusher_rpm_extend_(), PUSHER_ACC,
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

// Poll JC-100 every 200ms until all listed slaves' pressure rises above
// DETACH_THRESHOLD_KPA (-10 kPa) OR timeout. Returns false on success (all
// released), true on timeout. Used between valve-OFF and pusher retract.
bool WashRobot::vacuum_wait_release_(const std::vector<int>& slaves, int timeout_ms) {
    constexpr int POLL_MS = 300;   // 2026-05-29: 200→300,JC100 timeout 之間隔開,給 bus 喘息
    if (slaves.empty()) return false;   // nothing to check = trivial success

    // [2026-09-11 per user] Tolerate JC100 read failures (cup 7 sensor/wiring
    // was intermittently failing at ~57%). A comm timeout means we CANNOT
    // CONFIRM this cup's pressure — it does NOT mean the cup is still gripping.
    // Blocking the whole feet-retract (→ PAUSE-ON-ERROR → full aborts) on an
    // unreadable sensor is too fragile: the physical vacuum DID release (p≈0
    // whenever cup 7 read back), and CH6 break-vacuum (the NEXT stage of the
    // two-stage retract) forcibly releases regardless. So:
    //   - GOOD read, p >= DETACH     → released (confirmed)
    //   - GOOD read, p <  DETACH     → still gripping (confirmed) → keep waiting
    //   - READ FAIL (error_flag != 0)→ unconfirmed → do NOT block; proceed
    // Only a cup CONFIRMED still-gripping (good read below threshold) keeps us
    // waiting / counts as stuck. Unconfirmed cups are logged (never silent).
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        bool any_confirmed_gripping = false;
        std::vector<int> unconfirmed;
        for (int s : slaves) {
            int p = read_pressure_(s);
            if (M_(s).error_flag != 0) { unconfirmed.push_back(s); continue; }
            if (p < DETACH_THRESHOLD_KPA) { any_confirmed_gripping = true; break; }
        }
        if (!any_confirmed_gripping) {
            if (unconfirmed.empty()) {
                std::cout << "[vacuum_release] all released after " << elapsed << "ms\n";
            } else {
                std::cout << "[vacuum_release] proceeding after " << elapsed
                          << "ms; UNCONFIRMED (read-fail) slaves:";
                for (int s : unconfirmed) std::cout << " " << s;
                std::cout << " — CH6 break-vacuum will release regardless\n";
            }
            return false;
        }
        sleep_ms_(POLL_MS);
        elapsed += POLL_MS;
    }

    // Timeout — only cups CONFIRMED still-gripping (good read below threshold)
    // count as stuck. Read-fail cups are NOT stuck (see rationale above); if the
    // only problem were unreadable sensors we'd have returned false already.
    std::vector<int> stuck;
    for (int s : slaves) {
        int p = read_pressure_(s);
        if (M_(s).error_flag == 0 && p < DETACH_THRESHOLD_KPA) stuck.push_back(s);
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
