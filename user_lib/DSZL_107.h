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
    // 🔴 [2026-08-31] 側別標記 —— 讓 log 分得出左右。
    //
    // 問題：`_log_tag` 只由 slave id 組成（"DSZL:1"），而本專案**左右兩顆的 slave 都是 1**
    // （各自獨佔一條 USR gateway，所以號碼不必錯開）。結果 driver 層印出來的
    // `[DSZL:1] ... comm fail` **完全分不出是哪一顆**。
    // 2026-08-31 `LOG_ERR` 脫離 debug_mode 之後這些訊息才看得見，卻卡在這裡——
    // **看得見、但一半的診斷價值被標籤吃掉**（當晚查 VFD 寫入失敗時就卡在這個點）。
    //
    // 呼叫端在 init 之後呼叫一次即可，例如 set_log_side("L") → tag 變成 "DSZL:1@L"。
    // 📌 **冪等**：重複呼叫只會取代既有的 @suffix，不會累加。
    // ⚠️ **要在 init() 之前呼叫**：init 內部就會印 log（例如 clearAlarm 失敗），
    //    而那些正是最需要知道「是哪一顆」的訊息。側別存在 _log_side，由 init 併進 tag。
    //    init 之後呼叫也有效（會就地改寫 tag），只是 init 期間那幾行會少了標記。
    void set_log_side(const std::string& side) {
        _log_side = side;
        const size_t p = _log_tag.find('@');
        if (p != std::string::npos) _log_tag.erase(p);
        if (!_log_side.empty()) _log_tag += "@" + _log_side;
    }


    DSZL_107();
    ~DSZL_107();

    // 初始化（本類建立 TCP）
    bool init(const std::string& ip, int port, int ID, bool debug = false);

    // 初始化（外部傳入 TCP_client）
    bool init(TCP_client& extClient, int ID, bool debug = false);

    // 設定通訊參數（ID / Baud / Format）— writes to X518 config registers
    void set_communication_parm(int ID, int baud, int format);

    //=========== read ===========

    // 🔴 [2026-09-01] 通道選擇（1 或 2）。**預設 1，不呼叫就完全維持舊行為。**
    //
    // 沿革：原本左右各一台 X518（`.32` 左 / `.33` 右），兩台都只用 CH1，所以驅動
    // 通篇寫死 CH1。2026-09-01 per user **移除一台，剩下的 `.33` 一台接兩個通道：
    // CH1 = 右、CH2 = 左**。兩個 DSZL_107 物件共用同一條 TCP_client，靠本設定分流。
    //
    // 📌 為什麼保留兩個物件而不是改用單物件 + get_both_long()：每個物件各自持有
    //    **獨立的 scale、錯誤計數、last-valid 快取、@L/@R log 標記**，而上層的
    //    read_tensions() / cmd_tension / 歸零 / 張力安全檢查全都建立在左右對稱結構上。
    //    而且左右**幾乎必然需要不同 scale**（2026-09-01 實測兩側張力差 2.4 倍），
    //    那個結構正是要保留的東西。
    //
    // ⚠️ **單點故障**：兩側張力現在來自同一台裝置，它一掛 `tension_valid=0`，
    //    **左右過載保護同時失效**。先前一台壞只影響一側。
    // ⚠️ **X518 只允許一條 TCP 連線**：外部探測工具必須先停吊機程式，否則一律被拒絕
    //    （2026-09-01 踩過：ping 通但 502 拒連，差點誤判成裝置沒起來）。
    void set_channel(int ch) { channel_ = (ch == 2) ? 2 : 1; }
    int  get_channel() const { return channel_; }

    // 讀值暫存器：CH1 = 0x0A00、CH2 = 0x0A02（與 get_both_long 一次讀 4 個暫存器一致）。
    // 原註解寫「the left/right slave only uses CH1」，2026-09-01 起不再成立。
    // CH1 reading as raw int32 from register 0x0A00 （實際讀的通道由 set_channel 決定）。
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
    // 🔴 [2026-09-01] 依 set_channel() 的設定歸零對應通道。**呼叫端一律用這支**，
    // 不要再直接呼叫 do_zero_ch1() —— 那會讓「左側」物件去歸零右側的通道。
    bool do_zero();

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
    std::string _log_side;   // [2026-08-31] "L"/"R"，由 set_log_side 設定；init 會併進 _log_tag

    /***********************
     * 換算 / 容錯狀態
     ***********************/
    uint8_t     channel_ = 1;  // [2026-09-01] 1 或 2，見 set_channel()

    double scaleToKg;          // raw * scaleToKg = kg (default 0.01)
    double lastValidKg;
    int    errorCount;
};
