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
    // Init 2 re485 tcp connection
    if (!initConnections()) return false;
    // Init all device
    if (!initDevices())     return false;
    setupGroups();
    return true;
}

bool WashRobot::initConnections() {
    if (!cli_20.connectToServer("10.0.0.42", 4001)) {
        std::cerr << "[WashRobot] Failed to connect 192.168.1.20:4001" << std::endl;
        return false;
    }
    /*if (!cli_21.connectToServer("192.168.1.21", 4001)) {
        std::cerr << "[WashRobot] Failed to connect 192.168.1.21:4001" << std::endl;
        return false;
    }*/
    return true;
}

bool WashRobot::initDevices() {
    // 步進馬達 drv[0]~drv[3] — slave 1~4
    for (int i = 0; i < 1; i++) {
        //if (!drv[i].init(cli_20, i + 1, false)) {
        if (!drv[i].init(cli_20, 2, false)) {
            std::cerr << "[WashRobot] Failed to init DM2J drv[" << i
                      << "] slave " << (i + 1) << std::endl;
            return false;
        }
    }

    // 無刷馬達 m[0]~m[6] — slave 2~8
    for (int i = 0; i < 1; i++) {
        //if (!m[i].init(cli_21, i + 2, false)) {
        if (!m[i].init(cli_20, 7, false)) {
            std::cerr << "[WashRobot] Failed to init ZDT m[" << i
                      << "] slave " << (i + 2) << std::endl;
            return false;
        }
    }

    // 壓力感測器 meter[0]~meter[6] — slave 9~15
    for (int i = 0; i < 1; i++) {
      //if (!meter[i].init(cli_21, i + 9, false)) {
        if (!meter[i].init(cli_20, 9, false)) {
            std::cerr << "[WashRobot] Failed to init JC100 meter[" << i
                      << "] slave " << (i + 9) << std::endl;
            return false;
        }
    }

    // 繼電器
    if (!relay.init(cli_20, 1)) {
        std::cerr << "[WashRobot] Failed to init relay (slave 1)" << std::endl;
        return false;
    }
    //if (!relay_2.init(cli_21, 16)) {
    //    std::cerr << "[WashRobot] Failed to init relay_2 (slave 16)" << std::endl;
    //    return false;
    //}

    return true;
}

// ============================================================
//  setupGroups  ─ 定義腳組與線性軸設定
//
//  右側 (right_group): m[0](m1), m[1](m2)
//    m[0](m1): 無中間位置，直接歸零
//    m[1](m2): 先縮回至 72000 pulse，等待後歸零
//
//  左側 (left_group): m[5](m6), m[6](m7)
//    兩腳皆縮回至 72000 pulse，等待後歸零
//
//  中間 (center_group): m[2](m3), m[3](m4), m[4](m5)
//    三腳皆縮回至 15000 pulse，等待後歸零
//
//  注意：RELAY_VALVE_LEFT/RIGHT 命名依原始接線，
//        right_group 使用 RELAY_VALVE_LEFT（原始行為保留）
// ============================================================

void WashRobot::setupGroups() {

    // ── 右側腳組（線性軸 drv[0]）────────────────────────────
    right_group.cfg = { &relay, RELAY_VALVE_LEFT, 4000 };
    right_group.legs = {
        //  motor    sensor     axis      relay    valve_ch           en_rpm  en_pulse  ret_rpm  ret_pulse  zero_rpm
        { &m[0], &meter[0], &drv[0], &relay, 2,       1000,   144000,       0,        0,     1000 },  // m1: 直接歸零
    //  { &m[1], &meter[1], &drv[0], &relay, RELAY_VALVE_LEFT,       1000,   144000,     500,    72000,     1000 },  // m2: 先縮回
    };

    // ── 左側腳組（線性軸 drv[1]）────────────────────────────
    left_group.cfg = { &relay, RELAY_VALVE_RIGHT, 4000 };
    left_group.legs = {
    //  { &m[5], &meter[5], &drv[1], &relay, RELAY_VALVE_RIGHT,      1000,   144000,     500,    72000,     1000 },  // m6
    //  { &m[6], &meter[6], &drv[1], &relay, RELAY_VALVE_RIGHT,      1000,   144000,     500,    72000,     1000 },  // m7
    };

    // ── 中間腳組（線性軸 drv[2]）────────────────────────────
    center_group.cfg = { &relay, RELAY_VALVE_CENTER, 2000 };
    center_group.legs = {
    //  { &m[2], &meter[2], &drv[2], &relay, RELAY_VALVE_CENTER,      300,    30000,      80,    15000,      300 },  // m3
    //  { &m[3], &meter[3], &drv[2], &relay, RELAY_VALVE_CENTER,      300,    30000,      80,    15000,      300 },  // m4
    //  { &m[4], &meter[4], &drv[2], &relay, RELAY_VALVE_CENTER,      300,    30000,      80,    15000,      300 },  // m5
    };

    // ── 線性軸 ────────────────────────────────────────────────
    for (int i = 0; i < 1; i++)
        axes[i] = { &drv[i], i + 1, -38.0, 38.0, 700 };
}

// ============================================================
//  LegGroup 操作
// ============================================================

void WashRobot::enableGroup(LegGroup& g) {
    g.cfg.relay->controlRelay(g.cfg.valve_channel, true);
    for (auto& leg : g.legs)
        leg.motor->motion_control_pos_mode(
            0, 255, leg.enable_rpm, leg.enable_pulses, 1, 0, 1);
}

void WashRobot::disableGroup(LegGroup& g) {
    g.cfg.relay->controlRelay(g.cfg.valve_channel, false);

    // Phase 1：有中間位置的腳縮回，無中間位置的腳直接歸零
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
    enableLeft();
    enableRight();
    enableCenter();
}

void WashRobot::disableAll() {
    disableLeft();
    disableRight();
    disableCenter();
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
    // drv[1](drv_2) + drv[2](drv_3) 同步移動
    if (!checkAxis(axes[1], rpm, cm)) return;

    axes[1].drv->PR_move_cm_set(1, 1, rpm, cm, 100, 100);
    axes[2].drv->PR_move_cm_set(1, 1, rpm, cm, 100, 100);
    axes[1].drv->PR_trigger_sync(1);
}

// ============================================================
//  高階流程
// ============================================================

void WashRobot::doInit() {
    // 關閉所有 valve，啟動真空馬達
    relay.controlRelay(RELAY_VALVE_LEFT,   false);
    relay.controlRelay(RELAY_VALVE_RIGHT,  false);
    relay.controlRelay(RELAY_VALVE_CENTER, false);
    relay.controlRelay(RELAY_VACUUM_MOTOR, true);

    // 伸出所有腳（valve 保持關閉，待確認接觸後再 enable）
    extendGroupMotors(right_group);
    extendGroupMotors(left_group);
    extendGroupMotors(center_group);

    std::cout << "[WashRobot] Initialization done." << std::endl;
}

void WashRobot::doShutdown() {
    // 開啟所有 valve（釋放吸附），關閉真空馬達
    relay.controlRelay(RELAY_VALVE_LEFT,   true);
    relay.controlRelay(RELAY_VALVE_RIGHT,  true);
    relay.controlRelay(RELAY_VALVE_CENTER, true);
    relay.controlRelay(RELAY_VACUUM_MOTOR, false);

    std::cout << "[WashRobot] Shutdown." << std::endl;
}

// ============================================================
//  壓力讀取
// ============================================================

int WashRobot::readPressure(int leg_index) {
    if (leg_index < 0 || leg_index > 6) {
        std::cerr << "[ERROR] leg_index must be 0~6" << std::endl;
        return -1;
    }
    return meter[leg_index].read_pressure();
}

void WashRobot::adjustLegPos(Leg& leg) {
  relay.controlRelay(3, true); //RELAY_VACUUM_MOTOR
  leg.relay->controlRelay(leg.valve_ch, true);

  // 讀取起始位置
  double original_pos = 0;
  if (leg.axis->read_position_cm(original_pos)) {
    std::cout << "[INFO] Start position: " << original_pos << " cm" << std::endl;
  }

  bool is_fail = true;
  while (is_fail) {
    // 確認壓力計
    int val = leg.sensor->read_pressure();
    double pressure = val;
    std::cout << "[INFO] Pressure now: " << std::fixed << std::setprecision(1) << std::setw(6) << pressure << " kPa" << std::endl;

    if (pressure >= -50) {
      std::cout << "[WARN] The suction cup isn't sticking properly, try next location." << std::endl;
    }
    else {
      // 吸附成功
      is_fail = false;
      break;
    }

    // 解真空
    leg.relay->controlRelay(leg.valve_ch, false);

    // 縮腳
    leg.motor->motion_control_pos_mode(0, 255, leg.retract_rpm, leg.retract_pulses, 1, 0, 1);
    Sleep(2000);
    leg.motor->motion_control_pos_mode(0, 255, leg.zero_rpm, 0, 1, 0, 1);

    // 後退五公分
    double cm = 0;
    int rpm = 500;
    if (!leg.axis->read_position_cm(cm)) {
      std::cerr << "[ERROR] Failed to read position." << std::endl;
      continue;
    }
    std::cout << "[INFO] Position: " << cm << " cm" << std::endl;

    cm -= 5;
    if (cm < -60.0 || cm > 60.0) {
      std::cerr << "[ERROR] Position " << cm << " cm OUT OF RANGE (-38 ~ 38)." << std::endl;
    }
    else {
      leg.axis->PR_move_cm(0, 1, rpm, cm, 100, 100);
      std::cout << "[INFO] Move: RPM=" << rpm << " CM=" << cm << std::endl;
    }

    // 腳下去重吸
    leg.relay->controlRelay(leg.valve_ch, true);
    leg.motor->motion_control_pos_mode(0, 255, leg.enable_rpm, leg.enable_pulses, 1, 0, 1);

    // 不delay收不到值
    sleep(1);
  }

  // 讀取最終位置
  double final_pos = 0;
  if (leg.axis->read_position_cm(final_pos)) {
    std::cout << "[INFO] Final position: " << final_pos << " cm" << std::endl;
  }

  std::cout << "Total move: " << final_pos - original_pos << " cm" << std::endl;
  relay.controlRelay(RELAY_VACUUM_MOTOR, false);
  leg.relay->controlRelay(leg.valve_ch, false);
}

void WashRobot::moveRight() {
  adjustLegPos(right_group.legs[0]);
}

// ============================================================
//  startWash  ─ 線性軸來回洗窗，每次停點確認吸附
// ============================================================

void WashRobot::processWash(Leg& leg, int cycles, int rpm) {

  for (int i = 0; i < cycles; i++) {
    std::cout << "[Wash] Cycle " << (i + 1) << "/" << cycles << std::endl;

    // ── 讀目前位置 ──────────────────────────────────────────
    double cm = 0;
    if (!leg.axis->read_position_cm(cm)) {
      std::cerr << "[ERROR] Failed to read position, abort." << std::endl;
      return;
    }

    // 解真空
    leg.relay->controlRelay(leg.valve_ch, false);

    // 縮腳
    leg.motor->motion_control_pos_mode(0, 255, leg.retract_rpm, leg.retract_pulses, 1, 0, 1);
    Sleep(2000);
    leg.motor->motion_control_pos_mode(0, 255, leg.zero_rpm, 0, 1, 0, 1);

    // ── 往前 30 cm ─────────────────────────────────────────
    double fwd = cm + 30.0;
    if (fwd < -60.0 || fwd > 60.0) {
      std::cerr << "[ERROR] Forward target " << fwd << " cm out of range, abort." << std::endl;
      return;
    }
    std::cout << "[Wash] Move forward -> " << fwd << " cm" << std::endl;
    leg.axis->PR_move_cm(0, 1, rpm, fwd, 100, 100);
    sleep(3);  // 等待移動完成

    // 腳下去重吸
    leg.relay->controlRelay(leg.valve_ch, true);
    leg.motor->motion_control_pos_mode(0, 255, leg.enable_rpm, leg.enable_pulses, 1, 0, 1);


    // ── 確認吸附，必要時自動調整 ───────────────────────────
    adjustLegPos(leg);

    // ── 讀目前位置（adjustLegPos 可能已移動軸） ────────────
    if (!leg.axis->read_position_cm(cm)) {
      std::cerr << "[ERROR] Failed to read position, abort." << std::endl;
      return;
    }

    // ── 往後 30 cm ─────────────────────────────────────────
    double bwd = cm - 30.0;
    if (bwd < -60.0 || bwd > 60.0) {
      std::cerr << "[ERROR] Backward target " << bwd << " cm out of range, abort." << std::endl;
      return;
    }
    std::cout << "[Wash] Move backward -> " << bwd << " cm" << std::endl;
    leg.axis->PR_move_cm(0, 1, rpm, bwd, 100, 100);
    sleep(3);

    // ── 確認吸附，必要時自動調整 ───────────────────────────
    adjustLegPos(leg);
  }

  std::cout << "[Wash] Done." << std::endl;
}

void WashRobot::startWash(int cycles, int rpm) {
  std::cout << "Start wash, " << cycles << " cycles @ " << rpm << " RPM." << std::endl;
  std::cout << "Press enter to continue..." << std::endl;
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  processWash(right_group.legs[0], cycles, rpm);
}

