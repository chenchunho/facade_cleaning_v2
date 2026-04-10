#ifndef WASH_REBOT_H
#define WASH_REBOT_H

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#define Sleep(ms) usleep((ms) * 1000)
#endif

#include "TCP_client.h"
#include "ZDT_motor_control.h"
#include "JC_100_METER.h"
#include "DM2J_RS570.h"
#include "PQW_IO_16O_RLY.h"

#include <vector>
#include <string>
#include <limits>
#include <iomanip>

// ============================================================
//  Leg  ─ 單腳：馬達 + 壓力感測器 + 動作參數
//
//  縮回分兩段：
//    Phase 1：移動至中間位置 (retract_pulses != 0 才執行)
//    Phase 2：等待 settle_ms 後歸零
// ============================================================
struct Leg {
    ZDT_motor_control* motor;
    JC_100_METER*      sensor;    // 無感測器時設為 nullptr
    DM2J_RS570*        axis;      // 該腳對應的線性軸（同側兩腳共用）
    PQW_IO_16O_RLY*    relay;     // 該腳對應的繼電器模組
    int                valve_ch;  // 氣閥繼電器通道

    // 吸附（enable）參數
    int enable_rpm;
    int enable_pulses;

    // 縮回 Phase 1：中間位置（retract_pulses == 0 則略過，直接歸零）
    int retract_rpm;
    int retract_pulses;

    // 縮回 Phase 2 / 直接歸零速度
    int zero_rpm;
};

// ============================================================
//  LegGroupConfig  ─ 腳組共用設定
// ============================================================
struct LegGroupConfig {
    PQW_IO_16O_RLY* relay;
    int             valve_channel;
    int             settle_ms;   // 兩段縮回之間的等待時間 (ms)
};

// ============================================================
//  LegGroup  ─ 一組腳（共用同一個 vacuum valve）
// ============================================================
struct LegGroup {
    std::vector<Leg> legs;
    LegGroupConfig   cfg;
};

// ============================================================
//  LinearAxis  ─ 線性步進軸（DM2J）
// ============================================================
struct LinearAxis {
    DM2J_RS570* drv;
    int         id;
    double      min_cm;
    double      max_cm;
    int         max_rpm;
};

// ============================================================
//  WashRobot  ─ 洗窗機器人主控制類別
//
//  網路佈局：
//    cli_20 (192.168.1.20:4001) — DM2J 步進馬達 slave 1~4
//    cli_21 (192.168.1.21:4001) — ZDT 無刷馬達 slave 2~8
//                               — JC100 壓力感測器 slave 9~15
//                               — PQW 繼電器 slave 1, 16
//
//  腳組對應：
//    right_group  ─ m[0](m1), m[1](m2)        slave 2, 3
//    left_group   ─ m[5](m6), m[6](m7)        slave 7, 8
//    center_group ─ m[2](m3), m[3](m4), m[4](m5) slave 4, 5, 6
// ============================================================
class WashRobot {
public:
    WashRobot();
    ~WashRobot();

    // 初始化所有連線與裝置
    bool init();

    // ── 腳組操作 ──────────────────────────────────────────
    // 開閥 + 伸出腳
    void enableGroup (LegGroup& g);
    // 關閥 + 縮回腳（兩段式）
    void disableGroup(LegGroup& g);

    void enableLeft  ();  void disableLeft  ();
    void enableRight ();  void disableRight ();
    void enableCenter();  void disableCenter();
    void enableAll   ();  void disableAll   ();
    void adjustLegPos(Leg& leg);
    void processWash(Leg& leg, int cycles, int rpm);
    void startWash(int cycles, int rpm);
    void moveRight();

    // ── 線性軸移動 ─────────────────────────────────────────
    // axis_id: 1~4 對應 drv[0]~drv[3]（slave 1~4）
    void move    (int axis_id, int rpm, double cm);
    void moveSync(int rpm, double cm);  // drv[1]+drv[2] 同步（drv_2, drv_3）

    // ── 高階流程 ───────────────────────────────────────────
    void doInit    ();   // 初始化：關閥、開真空馬達、伸出所有腳
    void doShutdown();   // 關機：開閥放壓、關真空馬達

    // ── 壓力讀取 ───────────────────────────────────────────
    // leg_index: 0~6 對應 meter[0]~meter[6]
    int readPressure(int leg_index);

private:
    // ── TCP 連線 ───────────────────────────────────────────
    TCP_client cli_20;
    TCP_client cli_21;

    // ── 無刷馬達 m[0]~m[6]，slave ID 2~8 ─────────────────
    ZDT_motor_control m[7];

    // ── 壓力感測器 meter[0]~meter[6]，slave ID 9~15 ───────
    JC_100_METER meter[7];

    // ── 步進馬達 drv[0]~drv[3]，slave ID 1~4 ──────────────
    DM2J_RS570 drv[4];

    // ── 繼電器 ─────────────────────────────────────────────
    PQW_IO_16O_RLY relay;    // cli_21, slave 1
    PQW_IO_16O_RLY relay_2;  // cli_21, slave 16

    // ── 腳組 ───────────────────────────────────────────────
    LegGroup right_group;
    LegGroup left_group;
    LegGroup center_group;

    // ── 線性軸 ─────────────────────────────────────────────
    LinearAxis axes[4];

    // ── 繼電器通道定義（依原始接線） ──────────────────────
    // 注意：valve_left/right 命名與腳組左右相反，保留原始定義
    static const int RELAY_VACUUM_MOTOR  = 11;
    static const int RELAY_VALVE_LEFT    = 12;
    static const int RELAY_VALVE_CENTER  = 13;
    static const int RELAY_VALVE_RIGHT   = 14;

    // ── 內部輔助 ───────────────────────────────────────────
    bool initConnections();
    bool initDevices();
    void setupGroups();
    bool checkAxis(const LinearAxis& axis, int rpm, double cm);

    // 只伸出腳，不動 valve（供 doInit 使用）
    void extendGroupMotors(LegGroup& g);
};

#endif // WASH_REBOT_H
