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
//  ★ Robot Hardware Config — 修改此區域以對應實際接線 ★
//
//  腳位索引（陣列下標）：
//    [0]=上-左-1  [1]=上-左-2  [2]=上-右-1  [3]=上-右-2
//    [4]=下-左-1  [5]=下-左-2  [6]=下-右-1  [7]=下-右-2
// ============================================================
namespace RobotConfig {

    // ── ZDT 無刷馬達 Slave ID（SMC 推桿，8 腳）─────────────
    constexpr int ZDT_SLAVE[8] = {
        2, 3,    // 上-左-1, 上-左-2
        4, 5,    // 上-右-1, 上-右-2
        6, 7,    // 下-左-1, 下-左-2
        8, 9     // 下-右-1, 下-右-2
    };

    // ── JC_100 壓力感測器 Slave ID（8 腳）──────────────────
    constexpr int JC100_SLAVE[8] = {
        10, 11,  // 上-左-1, 上-左-2
        12, 13,  // 上-右-1, 上-右-2
        14, 15,  // 下-左-1, 下-左-2
        16, 17   // 下-右-1, 下-右-2
    };

    // ── DM2J 步進馬達 Slave ID（左/右滑桿）─────────────────
    constexpr int SLIDER_LEFT_SLAVE  = 1;
    constexpr int SLIDER_RIGHT_SLAVE = 2;

    // ── 繼電器：抽真空馬達通道（每腳獨立，0 = 未使用）──────
    constexpr int RELAY_VACUUM_MOTOR[8] = {
        1, 2,    // 上-左-1, 上-左-2
        3, 4,    // 上-右-1, 上-右-2
        5, 6,    // 下-左-1, 下-左-2
        7, 8     // 下-右-1, 下-右-2
    };

    // ── 繼電器：真空閥通道（每腳獨立）──────────────────────
    constexpr int RELAY_VALVE[8] = {
        9,  10,  // 上-左-1, 上-左-2
        11, 12,  // 上-右-1, 上-右-2
        13, 14,  // 下-左-1, 下-左-2
        15, 16   // 下-右-1, 下-右-2
    };

    // ── 動作參數 ─────────────────────────────────────────────
    constexpr int    PRESSURE_THRESHOLD = -50;   // 吸附閾值 (x0.1 kPa)，< 此值視為吸好
    constexpr int    ADJUST_BACK_CM     = 5;     // 吸附失敗後退距離 (cm)
    constexpr double SLIDER_MAX_CM      = 38.0;  // 滑桿行程上限 (cm)
    constexpr double SLIDER_MIN_CM      = -38.0; // 滑桿行程下限 (cm)
    constexpr int    SLIDER_RPM         = 300;   // 滑桿移動速度 (rpm)

}  // namespace RobotConfig

// ============================================================
//  Leg  ─ 單腳：馬達 + 壓力感測器 + 動作參數
//
//  縮回分兩段：
//    Phase 1：移動至中間位置 (retract_pulses != 0 才執行)
//    Phase 2：等待 settle_ms 後歸零
// ============================================================
struct Leg {
    ZDT_motor_control* motor;
    JC_100_METER*      sensor;
    DM2J_RS570*        axis;
    PQW_IO_16O_RLY*    relay;
    int                valve_ch;         // 真空閥繼電器通道
    int                vacuum_motor_ch;  // 抽真空馬達繼電器通道（0 = 不控制）

    int enable_rpm;
    int enable_pulses;
    int retract_rpm;
    int retract_pulses;
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
//  LegGroup  ─ 一組腳
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
//  SingleLegTestConfig  ─ 單腳測試設定
//
//  ★ 在 main.cpp 裡修改這個 struct 的值以對應實際接線 ★
// ============================================================
struct SingleLegTestConfig {
    // ── 網路連線 ──────────────────────────────────────────
    std::string tcp_ip   = "10.0.0.42";
    int         tcp_port = 4001;

    // ── Slave ID ──────────────────────────────────────────
    int zdt_slave        = 7;   // ZDT 無刷馬達（SMC 推桿）
    int jc100_slave      = 9;   // JC100 壓力感測器（0 = 不使用）
    int dm2j_slave       = 2;   // DM2J 步進馬達（線性軸）
    int relay_slave      = 1;   // PQW 繼電器

    // ── 繼電器通道 ────────────────────────────────────────
    int valve_ch         = 2;   // 真空閥通道
    int vacuum_motor_ch  = 0;   // 抽真空馬達通道（0 = 不控制）

    // ── 推桿動作參數 ──────────────────────────────────────
    int enable_rpm       = 1000;
    int enable_pulses    = 144000;
    int retract_rpm      = 500;
    int retract_pulses   = 72000;
    int zero_rpm         = 1000;

    // ── 線性軸參數 ────────────────────────────────────────
    int    axis_rpm      = 300;
    double axis_min_cm   = -38.0;
    double axis_max_cm   =  38.0;

    // ── 吸附判斷 ──────────────────────────────────────────
    int pressure_threshold = -50;  // < 此值視為吸好 (x0.1 kPa)
    int adjust_back_cm     = 5;    // 吸附失敗後退距離 (cm)
};

// ============================================================
//  WashRobot  ─ 洗窗機器人主控制類別
//
//  主清洗腳組：
//    body_group  ─ 上部分 4 腳（m[0]~m[3]）
//    foot_group  ─ 下部分 4 腳（m[4]~m[7]）
//
//  滑桿：
//    axes[0] = 左滑桿 (drv[0], SLIDER_LEFT_SLAVE)
//    axes[1] = 右滑桿 (drv[1], SLIDER_RIGHT_SLAVE)
// ============================================================
class WashRobot {
public:
    WashRobot();
    ~WashRobot();

    bool init();

    // ── 腳組操作 ──────────────────────────────────────────
    void enableGroup (LegGroup& g);
    void disableGroup(LegGroup& g);

    void enableLeft  ();  void disableLeft  ();
    void enableRight ();  void disableRight ();
    void enableCenter();  void disableCenter();
    void enableAll   ();  void disableAll   ();
    int  adjustLegPos(Leg& leg);
    void moveRight();

    // ── 線性軸移動 ─────────────────────────────────────────
    // axis_id: 1~4 對應 drv[0]~drv[3]
    void move    (int axis_id, int rpm, double cm);
    void moveSync(int rpm, double cm);  // 左右滑桿同步

    // ── 高階流程 ───────────────────────────────────────────
    void doInit    ();
    void doShutdown();

    // ── 主清洗流程 ─────────────────────────────────────────
    // step_cm: 每次滑桿移動公分數
    void startCleaningAll(int step_cm);

    // ── 單腳測試：來回洗窗 ─────────────────────────────────
    // cfg    : 單腳硬體設定（在 main.cpp 定義並修改）
    // cycles : 來回次數
    // step_cm: 每次前進公分數
    void testSingleLegWash(const SingleLegTestConfig& cfg, int cycles, int step_cm);

    // ── 壓力讀取 ───────────────────────────────────────────
    // leg_index: 0~7 對應 meter[0]~meter[7]
    int readPressure(int leg_index);

private:
    TCP_client cli_20;
    TCP_client cli_21;

    ZDT_motor_control  m[8];      // slave ID 由 RobotConfig::ZDT_SLAVE 決定
    JC_100_METER       meter[8];  // slave ID 由 RobotConfig::JC100_SLAVE 決定
    DM2J_RS570         drv[4];    // drv[0]=左滑桿, drv[1]=右滑桿

    PQW_IO_16O_RLY relay;
    PQW_IO_16O_RLY relay_2;

    // ── 主清洗腳組 ─────────────────────────────────────────
    LegGroup body_group;   // 上部分 4 腳
    LegGroup foot_group;   // 下部分 4 腳

    // ── 舊測試腳組（保留供 enable/disable 指令使用）────────
    LegGroup right_group;
    LegGroup left_group;
    LegGroup center_group;

    LinearAxis axes[4];    // axes[0]=左滑桿, axes[1]=右滑桿

    // 舊繼電器通道（保留供舊指令相容）
    static const int COMPAT_RELAY_VACUUM_MOTOR  = 11;
    static const int COMPAT_RELAY_VALVE_LEFT    = 12;
    static const int COMPAT_RELAY_VALVE_CENTER  = 13;
    static const int COMPAT_RELAY_VALVE_RIGHT   = 14;

    bool initConnections();
    bool initDevices();
    void setupGroups();
    bool checkAxis(const LinearAxis& axis, int rpm, double cm);
    void extendGroupMotors(LegGroup& g);
};

#endif // WASH_REBOT_H
