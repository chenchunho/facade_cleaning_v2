#pragma once
#include <stdint.h>
#include <string>
#include "TCP_client.h"

// ============================================================================
// MH300_inverter — Delta VFD-MH300 series VFD driver
//
// Reference doc: DELTA_IA-MDS_MH300_UM_TC_20260505.pdf
//   (Appendix A "Modbus 通訊", printed p.1042-1044 ACMD address list;
//    ch.9 comm params 09-XX; group 00-20/00-21 command source; 07-XX DC brake)
//
// Project use: replaces SE3_inverter (Shihlin SE3-210). 2 units control the
//   left / right rope winch. Motors unchanged; only the inverter + braking
//   resistor (BR300W070-S 300W/70Ohm) change. See memory
//   project_new_crane_vfd_mh300 for the migration rationale.
//
// ★ API-COMPATIBLE with SE3_inverter: same public method names/signatures so
//   Crane_control_PI/main.cpp can swap `SE3_inverter` -> `MH300_inverter` with
//   the control logic unchanged. Two SE3-specific methods are kept as
//   documented no-ops for source compatibility (invalidateCuModeCache).
//
// Protocol: Modbus-RTU over RS485, via USR-TCP232 transparent gateway (same
//   crane control bus as SE3 used — USR_A .30, slaves 1/2). Function codes:
//   0x03 (read), 0x06 (write single). CRC16 standard Modbus (init 0xFFFF,
//   poly 0xA001, LSB-first).
//
// Why Delta over SE3: MH300 has NO CU-mode latch and NO OPT silent-latch, so
//   the ensureCuMode_ / keepalive machinery SE3 needed is gone. Run commands
//   write the control register directly.
//
// ---------------------------------------------------------------------------
// Required commissioning setup (keypad, once per drive — CONFIRMED from manual)
//   09-00 = <id>   Modbus station address 1..254 (NOT 0)          [addr 0x0900]
//   09-01 = 9.6    baud in kbps (4.8/9.6/19.2/38.4/57.6/115.2)    [addr 0x0901]
//                  ⚠ value is the kbps number itself, NOT an index code.
//   09-04 = 12     data format 8N1 (RTU)  (12=8N1 RTU ... 17=8O2) [addr 0x0904]
//                  ⚠ format lives at 09-04, NOT 09-02. 12 matches the crane bus
//                    (SD76 locked 8N1). Other RTU: 13=8N2,14=8E1,15=8O1.
//   00-20 = 1      frequency command source = RS-485 (Modbus)     [addr 0x0014]
//                  ⚠ NOT 4 — index 4 is Pulse input.
//   00-21 = 2      run command source = RS-485 (Modbus)           [addr 0x0015]
//                  ⚠ NOT 3 — index 3 is CANopen.
//   09-09          comm timeout detect (ms), 0=off — optional
//
//   00-20 / 00-21 can also be set over Modbus once via configureModbusControl()
//   (writes 0x0014=1, 0x0015=2). Those writes persist to EEPROM — call ONCE at
//   commissioning, never every boot (flash wear). Baud/format (09-xx) must be
//   keypad-set: changing them over Modbus would drop the very link in use.
//
// Braking (heavy-load winch — braking resistor BR300W070-S):
//   07-00  brake chopper activation level (VDC)   [addr 0x0700]
//   07-01  DC brake current level (%)             [addr 0x0701]
//   07-02  DC brake time at start (s)             [addr 0x0702]
//   07-03  DC brake time at stop (s)              [addr 0x0703]
//   07-04  DC brake start frequency at stop (Hz)  [addr 0x0704]
//   01-12/01-13  accel / decel time 1 (s)         [addr 0x010C / 0x010D]
//
// ---------------------------------------------------------------------------
// Modbus register map (Delta ACMD list — CONFIRMED from manual App. A):
//
//   Control (write FC 0x06):
//     0x2000  Run / direction command word
//               bit1~0: 00=no func, 01=STOP, 10=RUN, 11=JOG+RUN
//               bit5~4: 00=no func, 01=FWD, 10=REV, 11=toggle dir
//               -> RUN FORWARD = 0x0012 (RUN=10b | FWD=01b<<4)
//               -> RUN REVERSE = 0x0022 (RUN=10b | REV=10b<<4)
//               -> STOP        = 0x0001 (decel per 01-13)
//     0x2001  Frequency setpoint, unit 0.01 Hz  (5000 = 50.00 Hz)
//     0x2002  Auxiliary command bits
//               bit0 = EF     (external fault trigger)
//               bit1 = RESET  (clear fault / alarm)
//               bit2 = B.B    (base block — immediate output cutoff / coast)
//               -> EMERGENCY STOP (coast) = 0x0004
//               -> FAULT RESET            = 0x0002  (then clear back to 0x0000)
//     0x2003  PID reference, bit15~0 = -10000..+10000 (-100.00..+100.00%)
//
//   Monitor (read FC 0x03):
//     0x2100  Error code (bit7~0) + warning code (bit15~8). 0 = no fault.
//     0x2101  Drive status word:
//               bit1~0: RUN/STOP status
//               bit2  : JOG
//               bit4~3: FWD/REV direction
//               bit8  : master freq via comm ; bit9~11 misc ; bit15~13 misc
//     0x2102  Output frequency, unit 0.01 Hz
//     0x2104  Output current  (see OUTPUT_CURRENT_SCALE — ⚠ verify)
//     0x2105  DC bus voltage  (0.1 V)
//     0x2106  Output voltage  (see OUTPUT_VOLTAGE_SCALE — ⚠ verify)
//
// ⚠ SCALE CAVEAT: the manual PDF text-extraction lost table alignment; the
//   current/voltage decimal places are annotated inconsistently (2104H shown
//   "XXX.X A" = 0.1A, but neighbouring 2103H "XX.XX A" = 0.01A). The scale
//   divisors live as named constants at the top of MH300_inverter.cpp — bench-
//   verify each against the keypad reading and adjust there in one place.
//   Frequency (0.01 Hz) is unambiguous and trusted.
// ============================================================================

class MH300_inverter {
public:
    // 🔴 [2026-08-31] 側別標記 —— 讓 log 分得出左右。
    //
    // 問題：`_log_tag` 只由 slave id 組成（"MH300:1"），而本專案**左右兩顆的 slave 都是 1**
    // （各自獨佔一條 USR gateway，所以號碼不必錯開）。結果 driver 層印出來的
    // `[MH300:1] ... comm fail` **完全分不出是哪一顆**。
    // 2026-08-31 `LOG_ERR` 脫離 debug_mode 之後這些訊息才看得見，卻卡在這裡——
    // **看得見、但一半的診斷價值被標籤吃掉**（當晚查 VFD 寫入失敗時就卡在這個點）。
    //
    // 呼叫端在 init 之後呼叫一次即可，例如 set_log_side("L") → tag 變成 "MH300:1@L"。
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

    MH300_inverter();
    ~MH300_inverter();

    //=========== init ===========

    // Mode A: own the TCP connection (ip:port).
    bool init(const std::string& ip, int port, int id, bool debug = false);
    // Mode B: share an external TCP_client (recommended when multiple devices
    // sit behind the same USR-TCP232 gateway). Probes the drive on the bus.
    bool init(TCP_client& extClient, int id, bool debug = false);

    //=========== control (write reg 0x2000 / 0x2002) ===========

    bool runForward();        // 0x2000 = 0x0012 (RUN + FWD)
    bool runReverse();        // 0x2000 = 0x0022 (RUN + REV)
    bool stopDecel();         // 0x2000 = 0x0001 (STOP, decel per 01-13)
    bool emergencyStop();     // 0x2002 bit2 (B.B) — immediate output cutoff

    //=========== frequency setpoint (write reg 0x2001) ===========

    // hz clamped to [0, max_hz]. Direction is chosen by runForward/runReverse,
    // not by sign — negative hz is clamped to 0.
    bool setFreqHz(double hz, double max_hz = 50.0);
    // Raw: write value (0.01 Hz units) to 0x2001 directly.
    bool setFreqRaw(uint16_t value_001hz);

    //=========== monitor reads (read 0x21xx) ===========

    bool readOutputFreqHz(double& out_hz);     // 0x2102 / 100
    bool readOutputCurrentA(double& out_amp);  // 0x2104 * OUTPUT_CURRENT_SCALE
    bool readOutputVoltageV(double& out_volt); // 0x2106 * OUTPUT_VOLTAGE_SCALE
    bool readStatusWord(uint16_t& out);        // 0x2101 (drive status word)

    // Error / warning code from 0x2100. low byte = error code (0 = no fault),
    // high byte = warning code. Convenience wrapper over readParam(0x2100).
    bool readErrorCode(uint16_t& out_code);

    // SE3-API-compat fault read. MH300 has a single live error+warn word (no
    // 4-deep history like SE3), so: out_f1 = error code, out_f2 = warning code,
    // out_f3 = out_f4 = 0. Returns true on Modbus error.
    bool readFaultCode(uint8_t& out_f1, uint8_t& out_f2,
                       uint8_t& out_f3, uint8_t& out_f4);

    //=========== alarm management ===========

    // Clear fault/alarm: pulse 0x2002 bit1 (RESET) then clear the word back to
    // 0x0000. 200ms settle between. Unlike SE3 there is no CU-mode latch to
    // re-claim afterwards. Returns true on Modbus error.
    bool clearAlarm();

    //=========== SE3-compat no-ops ===========

    // SE3 needed this to force re-claiming CU mode. MH300 has no CU-mode latch,
    // so this is a no-op kept only so a SE3->MH300 swap in the app compiles.
    void invalidateCuModeCache();

    //=========== commissioning helper (optional, writes EEPROM) ===========

    // Set command sources to RS-485/Modbus over the wire: 00-20=1, 00-21=2.
    // Persists to EEPROM — call ONCE at commissioning, not per boot. Baud/format
    // (09-xx) are intentionally NOT touched here (changing them would drop the
    // link). Returns true on Modbus error.
    bool configureModbusControl();

    //=========== generic param read/write ===========

    bool writeParam(uint16_t reg, uint16_t value);   // FC 0x06
    bool readParam(uint16_t reg, uint16_t& value);   // FC 0x03 single

    //=========== utility ===========

    int getSlaveID() const { return deviceID; }

private:
    bool     sendModbus(const uint8_t* req, int reqLen, uint8_t* resp, int& respLen);
    uint16_t crc16(const uint8_t* buf, int len);
    void     releaseBaseBlockIfNeeded_();   // clear 0x2002 B.B before a run cmd

    // Tracks whether emergencyStop() left base-block (0x2002 bit2) set. Unlike
    // SE3's MRS (which shared the run register and was cleared by the next run
    // write), MH300 B.B lives on 0x2002 while run lives on 0x2000 — so a run
    // command alone would NOT release the emergency cutoff. runForward/
    // runReverse clear 0x2002 first when this flag is set, restoring the SE3
    // "next run command resumes after emergency stop" invariant that the crane
    // app relies on. Cleared by run commands and clearAlarm().
    bool        base_blocked_ = false;

    TCP_client* client;
    bool        owns;
    int         deviceID;
    bool        debug_mode;
    std::string _log_tag;
    std::string _log_side;   // [2026-08-31] "L"/"R"，由 set_log_side 設定；init 會併進 _log_tag
};
