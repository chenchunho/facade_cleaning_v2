#pragma once
#include <string>
#include "TCP_client.h"

/******************************************************
 *  DSZL_107 (X518 multi-channel data acquisition)
 *
 *  This driver class is named DSZL_107 to align with the
 *  architecture role ("左/右鋼索張力感測"), but the device
 *  actually speaking Modbus is the X518 acquisition board
 *  that reads the DSZL_107 load cell via analog input.
 *
 *  Per architecture (CLAUDE.md, post 2026-05-08):
 *    192.168.1.32:502 — X518 #1 直插 switch, slave ID 1, 左鋼索張力 (CH1)
 *    192.168.1.33:502 — X518 #2 直插 switch, slave ID 1, 右鋼索張力 (CH1)
 *
 *  Wire-level: Modbus TCP (MBAP header + PDU, no CRC16). Each X518
 *  carries its own Ethernet stack and listens on :502 directly — no
 *  USR-TCP232-304 RS485 gateway in between. (Pre 2026-05-08 design
 *  assumed RS485 → USR transparent gateway; that path was dropped
 *  after bench commissioning showed bench units are Ethernet-native.)
 *
 *  一、初始化方式
 *  --------------------------------------------------
 *  方式 1：由本類別內部建立 TCP 連線
 *
 *      DSZL_107 ds;
 *      ds.init("192.168.1.32", 502, 1, true);
 *
 *  方式 2：外部已有 TCP_client
 *
 *      TCP_client cli;
 *      cli.connectToServer("192.168.1.32", 502);
 *      DSZL_107 ds;
 *      ds.init(cli, 1, true);
 *
 *
 *  二、基本使用方法
 *  --------------------------------------------------
 *      int32_t raw;
 *      ds.get_tension_long(raw);     // CH1 raw long
 *
 *      double kg;
 *      ds.get_tension_kg(kg);         // CH1 raw * scale (default 0.01)
 *
 *      int32_t ch1, ch2;
 *      ds.get_both_long(ch1, ch2);    // both CH at once
 *
 *
 *  三、校零 / 單位
 *  --------------------------------------------------
 *      ds.do_zero_ch1();              // zero CH1
 *      ds.set_unit_kg();              // unit register -> kg
 *
 *
 *  四、Scale factor
 *  --------------------------------------------------
 *  Default: kg = raw * 0.01  (assumes X518 returns hundredths of kg).
 *  Real scale depends on load cell + X518 calibration. Override with
 *  setScale() once measured against a known weight.
 *
 ******************************************************/

class DSZL_107
{
public:

    DSZL_107();
    ~DSZL_107();

    // 初始化（本類建立 TCP）
    bool init(const std::string& ip, int port, int ID, bool debug = false);

    // 初始化（外部傳入 TCP_client）
    bool init(TCP_client& extClient, int ID, bool debug = false);

    // 設定通訊參數（ID / Baud / Format）— writes to X518 config registers
    void set_communication_parm(int ID, int baud, int format);

    //=========== read ===========

    // CH1 reading as raw int32 from register 0x0A00 (the left/right slave only uses CH1).
    bool get_tension_long(int32_t& outValue);

    // CH1 reading converted to kg via current scale factor (graceful degradation:
    // caches last valid; returns true only after consecutive errors exceed threshold).
    bool get_tension_kg(double& outKg);

    // Both channels at once (single Modbus read of 0x0A00 area, 4 registers).
    bool get_both_long(int32_t& ch1, int32_t& ch2);

    //=========== control ===========

    // Zero a channel (writes 0x0A20 area). Per X518 manual:
    //   value = 1 -> zero CH1
    //   value = 2 -> zero CH2
    //   value = 7 -> zero all channels
    // NOTE: zero only affects RAM. To persist across X518 power-cycle, call
    // save_params() after zeroing.
    bool do_zero_ch1();
    bool do_zero_ch2();
    bool do_zero_all();

    // Persist all parameter changes (zero, unit, IP, etc.) to X518 flash by
    // writing 0xA20 = 40 (decimal). Per manual, X518 CPU pauses ~100ms while
    // copying RAM → flash; driver sleeps 150ms after the write to be safe.
    // Without calling this, X518 forgets all changes on power-cycle.
    bool save_params();

    // Unit register 0x0614:  1=t  2=kg  3=g  4=kN  5=N  6=lb (default 5=N).
    // Like zeroing, set_unit only writes RAM — call save_params() to persist.
    bool read_unit(int& unit);
    bool set_unit(int unit);
    bool set_unit_kg();   // = set_unit(2)

    //=========== utility ===========

    // raw -> kg scale (kg = raw * scaleToKg). Default 0.01.
    void   setScale(double scale) { scaleToKg = scale; }
    double getScale() const       { return scaleToKg; }

    // Last successfully cached kg (used internally for graceful degradation).
    double getLastValidKg() const { return lastValidKg; }
    int    getErrorCount()  const { return errorCount;  }

private:

    /***********************
     * Modbus + Tools
     ***********************/
    bool modbus_read(uint16_t addr, uint16_t quantity, uint8_t* rx, int& rxLen);
    bool modbus_write_long(uint16_t addr, int32_t value);

    int32_t parse_long(uint8_t* buf, int index);

    // Single register-pair long read with retry + small inter-frame delay.
    bool read_reg_long(uint16_t addr, int32_t& out);

    // Modbus TCP transaction id counter — bumped on every request frame.
    uint16_t txid_;

    /***********************
     * 通訊物件
     ***********************/
    TCP_client  ownedClient;  // 由本類建立連線時使用
    TCP_client* client;       // 指向 ownedClient 或外部傳入的物件
    bool        debug_mode;
    uint8_t     slaveID;
    std::string _log_tag;

    /***********************
     * 換算 / 容錯狀態
     ***********************/
    double scaleToKg;          // raw * scaleToKg = kg (default 0.01)
    double lastValidKg;
    int    errorCount;
};
