#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#define Sleep(ms) usleep((ms) * 1000)
#endif

#include "WASH_ROBOT.h"
#include <iostream>

// ============================================================
//  Constructor / Destructor
// ============================================================

WashRobot::WashRobot() {}
WashRobot::~WashRobot() {}

// ============================================================
//  init
// ============================================================

bool WashRobot::init() {
    if (!initConnections()) return false;
    if (!initDevices())     return false;
    setupGroups();
    return true;
}

bool WashRobot::initConnections() {
    if (!cli_20.connectToServer("10.0.0.42", 4001)) {
        std::cerr << "[WashRobot] Failed to connect 10.0.0.42:4001" << std::endl;
        return false;
    }
    /*if (!cli_21.connectToServer("192.168.1.21", 4001)) {
        std::cerr << "[WashRobot] Failed to connect 192.168.1.21:4001" << std::endl;
        return false;
    }*/
    return true;
}

bool WashRobot::initDevices() {

    // ── 步進馬達：左右滑桿 ───────────────────────────────────
    if (drv[0].init(cli_20, RobotConfig::SLIDER_LEFT_SLAVE, false)) {
        std::cerr << "[WashRobot] Failed to init left slider (slave "
                  << RobotConfig::SLIDER_LEFT_SLAVE << ")" << std::endl;
        return false;
    }
    if (drv[1].init(cli_20, RobotConfig::SLIDER_RIGHT_SLAVE, false)) {
        std::cerr << "[WashRobot] Failed to init right slider (slave "
                  << RobotConfig::SLIDER_RIGHT_SLAVE << ")" << std::endl;
        return false;
    }

    // ── 無刷馬達 m[0]~m[7]（SMC 推桿，8 腳）───────────────
    //    初始化失敗為非致命，繼續初始化其他裝置
    for (int i = 0; i < 8; i++) {
        if (!m[i].init(cli_20, RobotConfig::ZDT_SLAVE[i], false)) {
            std::cerr << "[WashRobot] Failed to init ZDT m[" << i
                      << "] slave " << RobotConfig::ZDT_SLAVE[i] << std::endl;
        }
    }

    // ── 壓力感測器 meter[0]~meter[7]（8 腳）────────────────
    //    初始化失敗為非致命
    for (int i = 0; i < 8; i++) {
        if (meter[i].init(cli_20, RobotConfig::JC100_SLAVE[i], false)) {
            std::cerr << "[WashRobot] Failed to init JC100 meter[" << i
                      << "] slave " << RobotConfig::JC100_SLAVE[i] << std::endl;
        }
    }

    // ── 繼電器 ───────────────────────────────────────────────
    if (relay.init(cli_20, 1)) {
        std::cerr << "[WashRobot] Failed to init relay (slave 1)" << std::endl;
        return false;
    }

    return true;
}

// ============================================================
//  setupGroups  ─ 定義腳組與線性軸設定
//
//  欄位順序（Leg 初始化）：
//    motor, sensor, axis, relay,
//    valve_ch, vacuum_motor_ch,
//    enable_rpm, enable_pulses,
//    retract_rpm, retract_pulses, zero_rpm
// ============================================================

void WashRobot::setupGroups() {

    // ── 左右滑桿軸 ───────────────────────────────────────────
    axes[0] = { &drv[0], RobotConfig::SLIDER_LEFT_SLAVE,
                RobotConfig::SLIDER_MIN_CM, RobotConfig::SLIDER_MAX_CM,
                RobotConfig::SLIDER_RPM };
    axes[1] = { &drv[1], RobotConfig::SLIDER_RIGHT_SLAVE,
                RobotConfig::SLIDER_MIN_CM, RobotConfig::SLIDER_MAX_CM,
                RobotConfig::SLIDER_RPM };

    // ── body_group（上部分 4 腳）─────────────────────────────
    //  legs[0,1] = 上-左-1, 上-左-2  ─ 共用左滑桿 drv[0]
    //  legs[2,3] = 上-右-1, 上-右-2  ─ 共用右滑桿 drv[1]
    body_group.cfg = { &relay, 0, 2000 };
    body_group.legs = {
        { &m[0], &meter[0], &drv[0], &relay, RobotConfig::RELAY_VALVE[0], RobotConfig::RELAY_VACUUM_MOTOR[0], 1000, 144000, 500, 72000, 1000 },
        { &m[1], &meter[1], &drv[0], &relay, RobotConfig::RELAY_VALVE[1], RobotConfig::RELAY_VACUUM_MOTOR[1], 1000, 144000, 500, 72000, 1000 },
        { &m[2], &meter[2], &drv[1], &relay, RobotConfig::RELAY_VALVE[2], RobotConfig::RELAY_VACUUM_MOTOR[2], 1000, 144000, 500, 72000, 1000 },
        { &m[3], &meter[3], &drv[1], &relay, RobotConfig::RELAY_VALVE[3], RobotConfig::RELAY_VACUUM_MOTOR[3], 1000, 144000, 500, 72000, 1000 },
    };

    // ── foot_group（下部分 4 腳）─────────────────────────────
    //  legs[0,1] = 下-左-1, 下-左-2  ─ 共用左滑桿 drv[0]
    //  legs[2,3] = 下-右-1, 下-右-2  ─ 共用右滑桿 drv[1]
    foot_group.cfg = { &relay, 0, 2000 };
    foot_group.legs = {
        { &m[4], &meter[4], &drv[0], &relay, RobotConfig::RELAY_VALVE[4], RobotConfig::RELAY_VACUUM_MOTOR[4], 1000, 144000, 500, 72000, 1000 },
        { &m[5], &meter[5], &drv[0], &relay, RobotConfig::RELAY_VALVE[5], RobotConfig::RELAY_VACUUM_MOTOR[5], 1000, 144000, 500, 72000, 1000 },
        { &m[6], &meter[6], &drv[1], &relay, RobotConfig::RELAY_VALVE[6], RobotConfig::RELAY_VACUUM_MOTOR[6], 1000, 144000, 500, 72000, 1000 },
        { &m[7], &meter[7], &drv[1], &relay, RobotConfig::RELAY_VALVE[7], RobotConfig::RELAY_VACUUM_MOTOR[7], 1000, 144000, 500, 72000, 1000 },
    };

    // ── 舊測試腳組（保留相容）────────────────────────────────
    //    vacuum_motor_ch = 0 表示不控制個別真空馬達（使用舊的共用馬達邏輯）
    right_group.cfg = { &relay, COMPAT_RELAY_VALVE_LEFT, 4000 };
    right_group.legs = {
        { &m[0], &meter[0], &drv[0], &relay, 2, 0, 1000, 144000, 0, 0, 1000 },
    };

    left_group.cfg  = { &relay, COMPAT_RELAY_VALVE_RIGHT, 4000 };
    left_group.legs = {};

    center_group.cfg  = { &relay, COMPAT_RELAY_VALVE_CENTER, 2000 };
    center_group.legs = {};
}

// ============================================================
//  LegGroup 操作
// ============================================================

void WashRobot::enableGroup(LegGroup& g) {
    for (auto& leg : g.legs) {
        if (leg.vacuum_motor_ch != 0)
            leg.relay->controlRelay(leg.vacuum_motor_ch, true);  // 啟動真空馬達
        leg.relay->controlRelay(leg.valve_ch, true);              // 開真空閥
        leg.motor->motion_control_pos_mode(
            0, 255, leg.enable_rpm, leg.enable_pulses, 1, 0, 1);
    }
}

void WashRobot::disableGroup(LegGroup& g) {
    // 停止吸附
    for (auto& leg : g.legs) {
        leg.relay->controlRelay(leg.valve_ch, false);              // 關真空閥
        if (leg.vacuum_motor_ch != 0)
            leg.relay->controlRelay(leg.vacuum_motor_ch, false);  // 停真空馬達
    }

    // Phase 1：縮回（有中間位置的腳先縮回，無的直接歸零）
    for (auto& leg : g.legs) {
        if (leg.retract_pulses != 0)
            leg.motor->motion_control_pos_mode(
                0, 255, leg.retract_rpm, leg.retract_pulses, 1, 0, 1);
        else
            leg.motor->motion_control_pos_mode(
                0, 255, leg.zero_rpm, 0, 1, 0, 1);
    }

    Sleep(g.cfg.settle_ms);

    // Phase 2：有中間位置的腳歸零
    for (auto& leg : g.legs) {
        if (leg.retract_pulses != 0)
            leg.motor->motion_control_pos_mode(
                0, 255, leg.zero_rpm, 0, 1, 0, 1);
    }
}

// 只伸出腳馬達，不動 valve（供 doInit 使用）
void WashRobot::extendGroupMotors(LegGroup& g) {
    for (auto& leg : g.legs)
        leg.motor->motion_control_pos_mode(
            0, 255, leg.enable_rpm, leg.enable_pulses, 1, 0, 1);
}

void WashRobot::enableLeft  () { enableGroup(left_group);   }
void WashRobot::disableLeft () { disableGroup(left_group);  }
void WashRobot::enableRight () { enableGroup(right_group);  }
void WashRobot::disableRight() { disableGroup(right_group); }
void WashRobot::enableCenter() { enableGroup(center_group); }
void WashRobot::disableCenter(){ disableGroup(center_group);}

void WashRobot::enableAll() {
    enableLeft(); enableRight(); enableCenter();
}

void WashRobot::disableAll() {
    disableLeft(); disableRight(); disableCenter();
}

// ============================================================
//  線性軸移動
// ============================================================

bool WashRobot::checkAxis(const LinearAxis& axis, int rpm, double cm) {
    if (cm < axis.min_cm || cm > axis.max_cm) {
        std::cerr << "[ERROR] Position " << cm << " cm out of range ("
                  << axis.min_cm << " to " << axis.max_cm << ")" << std::endl;
        return false;
    }
    if (rpm < 0 || rpm > axis.max_rpm) {
        std::cerr << "[ERROR] Speed " << rpm << " RPM out of range (0 to "
                  << axis.max_rpm << ")" << std::endl;
        return false;
    }
    return true;
}

void WashRobot::move(int axis_id, int rpm, double cm) {
    if (axis_id < 1 || axis_id > 4) {
        std::cerr << "[ERROR] axis_id must be 1~4" << std::endl;
        return;
    }
    LinearAxis& ax = axes[axis_id - 1];
    if (!checkAxis(ax, rpm, cm)) return;
    ax.drv->PR_move_cm(0, 1, rpm, cm, 100, 100);
    std::cout << "[LOG] Axis " << axis_id << " -> " << cm
              << " cm @ " << rpm << " RPM" << std::endl;
}

void WashRobot::moveSync(int rpm, double cm) {
    // 左滑桿(axes[0]) + 右滑桿(axes[1]) 同步移動
    if (!checkAxis(axes[0], rpm, cm)) return;
    axes[0].drv->PR_move_cm_set(1, 1, rpm, cm, 100, 100);
    axes[1].drv->PR_move_cm_set(1, 1, rpm, cm, 100, 100);
    axes[0].drv->PR_trigger_sync(1);
}

// ============================================================
//  高階流程
// ============================================================

void WashRobot::doInit() {
    // 舊共用繼電器：關閉所有 valve、啟動共用真空馬達
    relay.controlRelay(COMPAT_RELAY_VALVE_LEFT,   false);
    relay.controlRelay(COMPAT_RELAY_VALVE_RIGHT,  false);
    relay.controlRelay(COMPAT_RELAY_VALVE_CENTER, false);
    relay.controlRelay(COMPAT_RELAY_VACUUM_MOTOR, true);

    extendGroupMotors(right_group);
    extendGroupMotors(left_group);
    extendGroupMotors(center_group);

    std::cout << "[WashRobot] Initialization done." << std::endl;
}

void WashRobot::doShutdown() {
    relay.controlRelay(COMPAT_RELAY_VALVE_LEFT,   true);
    relay.controlRelay(COMPAT_RELAY_VALVE_RIGHT,  true);
    relay.controlRelay(COMPAT_RELAY_VALVE_CENTER, true);
    relay.controlRelay(COMPAT_RELAY_VACUUM_MOTOR, false);

    std::cout << "[WashRobot] Shutdown." << std::endl;
}

// ============================================================
//  壓力讀取
// ============================================================

int WashRobot::readPressure(int leg_index) {
    if (leg_index < 0 || leg_index > 7) {
        std::cerr << "[ERROR] leg_index must be 0~7" << std::endl;
        return -1;
    }
    return meter[leg_index].read_pressure();
}

// ============================================================
//  adjustLegPos  ─ 確認吸附，失敗則後退重試
//  回傳：最終位置 - 起始位置（cm，int）
// ============================================================

int WashRobot::adjustLegPos(Leg& leg) {
    if (leg.sensor == nullptr) {
        std::cout << "[INFO] No pressure sensor configured, skipping suction adjust." << std::endl;
        return 0;
    }

    double original_pos = 0;
    if (!leg.axis->read_position_cm(original_pos)) {
        std::cout << "[INFO] Start position: " << original_pos << " cm" << std::endl;
    }

    bool is_fail = true;
    while (is_fail) {
        int val = leg.sensor->read_pressure();
        double pressure = val;
        std::cout << "[INFO] Pressure: " << std::fixed << std::setprecision(1)
                  << std::setw(6) << pressure << " (x0.1 kPa)" << std::endl;

        if (pressure < RobotConfig::PRESSURE_THRESHOLD) {
            // 壓力夠負，吸附成功
            is_fail = false;
            break;
        }

        std::cout << "[WARN] Suction cup not attached, backing up "
                  << RobotConfig::ADJUST_BACK_CM << " cm." << std::endl;

        // 解真空
        leg.relay->controlRelay(leg.valve_ch, false);
        //if (leg.vacuum_motor_ch != 0)
        //    leg.relay->controlRelay(leg.vacuum_motor_ch, false);

        // 縮腳
        leg.motor->motion_control_pos_mode(0, 255, leg.retract_rpm, leg.retract_pulses, 1, 0, 1);
        Sleep(2000);
        leg.motor->motion_control_pos_mode(0, 255, leg.zero_rpm, 0, 1, 0, 1);

        // 後退
        double cm = 0;
        if (leg.axis->read_position_cm(cm)) {
            std::cerr << "[ERROR] Failed to read position." << std::endl;
            continue;
        }
        cm -= RobotConfig::ADJUST_BACK_CM;
        if (cm < RobotConfig::SLIDER_MIN_CM || cm > RobotConfig::SLIDER_MAX_CM) {
            std::cerr << "[ERROR] Adjusted position " << cm << " cm out of range, abort." << std::endl;
            break;
        }
        leg.axis->PR_move_cm(0, 1, 500, cm, 100, 100);
        Sleep(500);

        // 重新吸附
        if (leg.vacuum_motor_ch != 0)
            leg.relay->controlRelay(leg.vacuum_motor_ch, true);
        leg.relay->controlRelay(leg.valve_ch, true);
        leg.motor->motion_control_pos_mode(0, 255, leg.enable_rpm, leg.enable_pulses, 1, 0, 1);
        Sleep(1000);
    }

    double final_pos = 0;
    leg.axis->read_position_cm(final_pos);
    int total_adj = (int)(final_pos - original_pos);
    std::cout << "[INFO] Adjust total: " << total_adj << " cm" << std::endl;
    return total_adj;
}

// ============================================================
//  moveRight / processWash / startWash  （舊測試流程，保留）
// ============================================================

void WashRobot::moveRight() {
    adjustLegPos(right_group.legs[0]);
}

// ============================================================
//  startCleaningAll  ─ 主清洗流程（一路往下）
//
//  step_cm: 每次移動公分數（正值）
//
//  流程（重複直到 Ctrl+C）：
//    Phase A: body_group 縮推桿+停真空 → 滑桿往正 → body_group 開真空+伸推桿 → 確認吸附
//    Phase B: foot_group 縮推桿+停真空 → 滑桿往負 → foot_group 開真空+伸推桿 → 確認吸附
// ============================================================

void WashRobot::startCleaningAll(int step_cm) {
    int slider_pos_cm = 0;

    std::cout << "[CleanAll] Starting. Step=" << step_cm
              << " cm. Press Ctrl+C to stop." << std::endl;

    while (true) {

        // ── Phase A: body_group（上部分）往前 ───────────────────
        std::cout << "[CleanAll] Phase A ──────────────────────────────" << std::endl;

        // 1. body_group 縮推桿 + 停真空
        disableGroup(body_group);

        // 2. 兩支滑桿往正移動
        slider_pos_cm += step_cm;
        std::cout << "[CleanAll] Sliders -> +" << slider_pos_cm << " cm" << std::endl;
        moveSync(RobotConfig::SLIDER_RPM, (double)slider_pos_cm);
        Sleep(3000);  // 等待滑桿到位

        // 3. body_group 開真空 + 伸推桿
        enableGroup(body_group);
        Sleep(1000);  // 等待吸附建立

        // 4. 逐腳確認吸附，累計位移補償
        for (auto& leg : body_group.legs) {
            int adj = adjustLegPos(leg);
            slider_pos_cm += adj;
        }

        // ── Phase B: foot_group（下部分）往前 ───────────────────
        std::cout << "[CleanAll] Phase B ──────────────────────────────" << std::endl;

        // 5. foot_group 縮推桿 + 停真空
        disableGroup(foot_group);

        // 6. 兩支滑桿往負移動
        slider_pos_cm -= step_cm;
        std::cout << "[CleanAll] Sliders -> " << slider_pos_cm << " cm" << std::endl;
        moveSync(RobotConfig::SLIDER_RPM, (double)slider_pos_cm);
        Sleep(3000);

        // 7. foot_group 開真空 + 伸推桿
        enableGroup(foot_group);
        Sleep(1000);

        // 8. 逐腳確認吸附
        for (auto& leg : foot_group.legs) {
            int adj = adjustLegPos(leg);
            slider_pos_cm += adj;
        }
    }
}

// ============================================================
//  testSingleLegWash  ─ 單腳來回洗窗測試
//
//  建立獨立連線與裝置，不依賴主機器人的 cli_20/cli_21。
//  在 main.cpp 定義 SingleLegTestConfig 並修改其中數值即可換腳測試。
// ============================================================

void WashRobot::testSingleLegWash(const SingleLegTestConfig& cfg, int cycles, int step_cm) {

    // ── 建立獨立 TCP 連線 ─────────────────────────────────────
    TCP_client tcp;
    if (!tcp.connectToServer(cfg.tcp_ip, cfg.tcp_port)) {
        std::cerr << "[TestLeg] Failed to connect "
                  << cfg.tcp_ip << ":" << cfg.tcp_port << std::endl;
        return;
    }
    std::cout << "[TestLeg] Connected to "
              << cfg.tcp_ip << ":" << cfg.tcp_port << std::endl;

    // ── 初始化裝置 ────────────────────────────────────────────
    ZDT_motor_control motor;
    if (!motor.init(tcp, cfg.zdt_slave, false)) {
        std::cerr << "[TestLeg] Failed to init ZDT slave " << cfg.zdt_slave << std::endl;
        return;
    }

    DM2J_RS570 axis;
    if (axis.init(tcp, cfg.dm2j_slave, false)) {
        std::cerr << "[TestLeg] Failed to init DM2J slave " << cfg.dm2j_slave << std::endl;
        return;
    }

    PQW_IO_16O_RLY rly;
    if (rly.init(tcp, cfg.relay_slave)) {   // total_relay 用預設 16，不傳 false
        std::cerr << "[TestLeg] Failed to init relay slave " << cfg.relay_slave << std::endl;
        return;
    }

    JC_100_METER sensor;
    bool has_sensor = (cfg.jc100_slave != 0);
    if (has_sensor && sensor.init(tcp, cfg.jc100_slave, false)) {
        std::cerr << "[TestLeg] Failed to init JC100 slave " << cfg.jc100_slave
                  << ", continuing without pressure check." << std::endl;
        has_sensor = false;
    }

    // ── 建立 Leg struct（供 adjustLegPos 使用）────────────────
    Leg leg;
    leg.motor           = &motor;
    leg.sensor          = has_sensor ? &sensor : nullptr;
    leg.axis            = &axis;
    leg.relay           = &rly;
    leg.valve_ch        = cfg.valve_ch;
    leg.vacuum_motor_ch = cfg.vacuum_motor_ch;
    leg.enable_rpm      = cfg.enable_rpm;
    leg.enable_pulses   = cfg.enable_pulses;
    leg.retract_rpm     = cfg.retract_rpm;
    leg.retract_pulses  = cfg.retract_pulses;
    leg.zero_rpm        = cfg.zero_rpm;

    // ── 初始：啟動真空馬達 + 開真空閥 + 伸腳 ─────────────────
    if (cfg.vacuum_motor_ch != 0)
        rly.controlRelay(cfg.vacuum_motor_ch, true);
    rly.controlRelay(cfg.valve_ch, true);
    motor.motion_control_pos_mode(0, 255, cfg.enable_rpm, cfg.enable_pulses, 1, 0, 1);
    Sleep(2000);

    std::cout << "[TestLeg] Start. Cycles=" << cycles
              << "  Step=" << step_cm << " cm" << std::endl;

    // actual_step 記錄本次前進後實際停留的位移（含 adjustLegPos 補償）
    int actual_step = step_cm;

    for (int i = 0; i < cycles; i++) {
        std::cout << "[TestLeg] ── Cycle " << (i + 1) << "/" << cycles
                  << " ───────────────────────────" << std::endl;

        // ======================================================
        //  FORWARD PHASE: 解真空 → 縮腳 → 前進 → 吸附 → adjustLegPos
        //  （上一次 backward 或初始狀態下，valve 已開；此時才釋放）
        // ======================================================

        double cm = 0;
        if (axis.read_position_cm(cm)) {
            std::cerr << "[TestLeg] Failed to read position, abort." << std::endl;
            break;
        }
        std::cout << "[TestLeg] Fwd start pos: " << cm << " cm" << std::endl;

        // 解真空
        rly.controlRelay(cfg.valve_ch, false);
        //if (cfg.vacuum_motor_ch != 0)
        //    rly.controlRelay(cfg.vacuum_motor_ch, false);

        // 縮腳
        motor.motion_control_pos_mode(0, 255, cfg.retract_rpm, cfg.retract_pulses, 1, 0, 1);
        Sleep(2000);
        motor.motion_control_pos_mode(0, 255, cfg.zero_rpm, 0, 1, 0, 1);

        // 往前 step_cm
        double fwd = cm + step_cm;
        if (fwd < cfg.axis_min_cm || fwd > cfg.axis_max_cm) {
            std::cerr << "[TestLeg] Forward target " << fwd
                      << " cm out of range [" << cfg.axis_min_cm
                      << ", " << cfg.axis_max_cm << "], abort." << std::endl;
            break;
        }
        std::cout << "[TestLeg] Move forward -> " << fwd << " cm" << std::endl;
        axis.PR_move_cm(0, 1, cfg.axis_rpm, fwd, 100, 100);
        Sleep(3000);

        // 開真空 + 伸腳
        //if (cfg.vacuum_motor_ch != 0)
        //    rly.controlRelay(cfg.vacuum_motor_ch, true);
        rly.controlRelay(cfg.valve_ch, true);
        motor.motion_control_pos_mode(0, 255, cfg.enable_rpm, cfg.enable_pulses, 1, 0, 1);
        Sleep(1000);

        // 確認吸附，計算實際步長（adj ≤ 0 表示退後補償）
        int adj = adjustLegPos(leg);
        actual_step = step_cm + adj;
        std::cout << "[TestLeg] Fwd actual_step=" << actual_step
                  << " cm (step=" << step_cm << ", adj=" << adj << ")" << std::endl;

        // ======================================================
        //  BACKWARD PHASE: 真空保持 ON → 縮腳 → 後退 actual_step
        //                  → 伸腳 → adjustLegPos（確認吸附）
        //  注意：valve 全程維持開啟，下一次迴圈開頭才解真空
        // ======================================================

        if (axis.read_position_cm(cm)) {
            std::cerr << "[TestLeg] Failed to read position, abort." << std::endl;
            break;
        }
        std::cout << "[TestLeg] Bwd start pos: " << cm << " cm" << std::endl;


        // 往後 actual_step
        double bwd = cm - actual_step;
        if (bwd < cfg.axis_min_cm || bwd > cfg.axis_max_cm) {
            std::cerr << "[TestLeg] Backward target " << bwd
                      << " cm out of range, abort." << std::endl;
            break;
        }
        std::cout << "[TestLeg] Move backward <- " << bwd << " cm" << std::endl;
        axis.PR_move_cm(0, 1, cfg.axis_rpm, bwd, 100, 100);
        Sleep(3000);

        // valve 保持 ON → 下一次迴圈 FORWARD PHASE 開頭才解真空
    }

    // ── 結束：關真空閥 + 停真空馬達 ──────────────────────────
    rly.controlRelay(cfg.valve_ch, false);
    if (cfg.vacuum_motor_ch != 0)
        rly.controlRelay(cfg.vacuum_motor_ch, false);

    std::cout << "[TestLeg] Done." << std::endl;
}
