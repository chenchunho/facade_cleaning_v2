#pragma once
#include <stdint.h>
#include <string>
#include "TCP_client.h"

// ============================================================================
// SE3_inverter — Shihlin Electric SE3 series VFD driver
//
// Reference doc: 變頻器SE3系列操作手冊 V1.03.pdf (chapter 5.8 / 07-XX params)
// Project use: 2 units control left / right rope winch (replaces ZS_DIO_R_RLY
// 2026-05-07).
//
// Protocol: Modbus-RTU over RS485, accessed via USR-TCP232 transparent gateway.
//   Function codes used: 0x03 (read), 0x06 (write single), 0x10 (write multi).
//   CRC16: standard Modbus (init 0xFFFF, poly 0xA001, LSB-first byte order).
//
// Required keypad pre-configuration (verified bench 2026-05-07/08):
//   P.36  (07-01) = <id>     station number 1..99 (NOT 0 = broadcast)
//   P.32  (07-02) = 1        baudrate 9600 (or match USR)
//   P.33  (07-00) = 0        protocol = Modbus
//   P.154 (07-07) = 3        format 8N2 RTU (only RTU option without parity)
//                            ⚠ USR can be set to 8N1 — SE3 UART tolerates
//                              receiving 1 stop bit even when set to expect 2.
//                              Required for sharing bus with SD76 (locked at 8N1).
//   P.51  (07-06) = 1        End char = CR only
//   00-16 (P.79) = 3         operation mode = 通訊 (CU/communication)
//   00-19 (P.35) = 0         comm-mode command source = communication (default)
//
// Comm timeout protection (07-08/09/10, V1.03 manual 2026-05-13):
//   P.52  (07-08) = 2        通訊異常容許次數 (consecutive timeouts before alarm)
//   P.53  (07-09) = 2.0      通訊間隔容許時間 sec (0=disabled, 99999=disabled)
//   P.153 (07-10) = 0        通訊錯誤處理:
//                              0 = 報警並空轉停車 (alarm OPT + coast stop)
//                              1 = 不報警並繼續運行 (no alarm, continue)
//   ⚠ Only TWO values 0/1 — earlier docs incorrectly listed 4 options (decel
//   stop / trip / continue) which exist on other Shihlin VFD models, NOT SE3-210.
//   ⚠ Requires se3_keepalive_loop running in Crane_control_PI (1Hz readStatus
//   during motion/hold) to prevent OPT alarm from triggering during legitimate
//   silent periods (freeze / fine_adjust / hold idle).
//   See changelog 2026-05-13a and Crane_control_PI/main.cpp se3_keepalive_loop.
//
// ★ CRITICAL Modbus run-command quirk (manual §7-3 example #1):
//   Even with P.79 = 3 set from panel, SE3 firmware still REJECTS run-command
//   writes to H1001 unless the Modbus side has explicitly written H1000 = 0
//   (Modbus operation mode = CU/通訊). Panel value 3 and Modbus value 0 both
//   refer to "通訊模式" but use different number codings.
//   → Driver auto-writes H1000 = 0 before every run command via ensureCuMode_().
//   → If you write run command without this prefix, expect timeout (no response).
//   → SL-INV PC software does this in background; raw Modbus tools must too.
//
// ★ CU-mode timing race (bench 2026-05-08):
//   SE3 acks the H1000=0 Modbus write quickly, but its internal state machine
//   takes another ~50-150ms to actually finish transitioning into CU mode. An
//   H1001 (run cmd) write issued in that window is silently rejected. To make
//   first-attempt run reliable, ensureCuMode_() now sleeps 150ms after the
//   H1000 write before returning. Cost: 150ms one-time per power-up (cached).
//
// Modbus register map (verified against V1.03 manual §7-3 通訊命令明細, 2026-05-07):
//
//   Control (write 0x06 / 0x10):
//     0x1001  Run command bits (write)
//             b0 = reserved   (DO NOT set)
//             b1 = STF        (run forward)
//             b2 = STR        (run reverse)
//             b3 = RL         (multi-speed low)
//             b4 = RM         (multi-speed mid)
//             b5 = RH         (multi-speed high)
//             b6 = RT         (2nd parameter set)
//             b7 = MRS        (output cutoff / emergency)
//             Stop = write 0x0000 (clear all bits) → motor decel per P.7
//             ⚠ NOT 0x1101 — 0x1101 is 站號釋放 (station release), expects magic H9696.
//
//     0x1002  Frequency setpoint (RAM, no EEPROM wear)
//             unit: 0.01 Hz   (5000 = 50.00 Hz)
//             prefer this over 0x1009 (EEPROM) for runtime control
//
//     0x1009  Frequency setpoint (EEPROM, persisted) — avoid for runtime
//
//   Monitor (read 0x03):
//     0x1001  Status word (read same address as control, FC distinguishes)
//             b0=running / b1=fwd / b2=rev / b3=freq reached / b4=overload
//             b6=freq detect / b7=fault / b8=undervolt / b9=stall / b10=PLC
//             b11=EO / b14=resetting / b15=tuning
//     0x1003  Output frequency   unit: 0.01 Hz   (manual: 輸出頻率)
//     0x1004  Output current     unit: 0.01 A    (manual: 輸出電流)
//     0x1005  Output voltage     unit: 0.01 V    (manual: 輸出電壓)
//     0x1007  Fault code 1+2     two error codes packed (b0~b7 / b8~b15)
//     0x1008  Fault code 3+4     latest fault history
//     ⚠ Earlier driver used 0x100A/B/C — those are line-speed feedback / line-speed
//       target / tension setpoint, NOT output monitor. Caused 0 reads to look right
//       when motor stopped, but actually wrong registers. Fixed 2026-05-07.
//
// NOTE: Register addresses extracted from PDF text dump; PDF text extraction
// loses table layout. Verify against panel + Modbus poke before relying on
// these in production.
// ============================================================================

class SE3_inverter {
public:
    SE3_inverter();
    ~SE3_inverter();

    //=========== init ===========

    bool init(const std::string& ip, int port, int id, bool debug = false);
    bool init(TCP_client& extClient, int id, bool debug = false);

    //=========== control (write reg 0x1101) ===========

    bool runForward();        // bit1 (STF) = 1
    bool runReverse();        // bit2 (STR) = 1
    bool stopDecel();         // bit0 = 1 (stop, decel per P.7 acc/dec settings)
    bool emergencyStop();     // bit7 (MRS) = 1, output cutoff

    //=========== frequency setpoint (write reg 0x1002, RAM only) ===========

    // hz clamped to [0, max_hz]. Negative not supported here — use runReverse() for direction.
    bool setFreqHz(double hz, double max_hz = 50.0);

    // Raw: writes value (0.01 Hz units) to 0x1002 directly.
    bool setFreqRaw(uint16_t value_001hz);

    //=========== monitor reads (read 0x100x) ===========

    bool readOutputFreqHz(double& out_hz);     // 0x100A / 100
    bool readOutputCurrentA(double& out_amp);  // 0x100B / 100
    bool readOutputVoltageV(double& out_volt); // 0x100C / 100
    bool readStatusWord(uint16_t& out);        // 0x1001

    // Read fault history (H1007 + H1008). Each register packs 2 fault codes
    // (high byte / low byte). out_f1 is most recent. Manual §異常代碼 table
    // example values: 160 = OPT, 16-19 = OC1-OC0, 32-35 = OV1-OV0, 144 = OHT,
    // 64 = EEP, 0 = empty/no fault in that slot.
    // Returns true on Modbus error.
    bool readFaultCode(uint8_t& out_f1, uint8_t& out_f2,
                       uint8_t& out_f3, uint8_t& out_f4);

    //=========== CU mode management ===========

    // Force re-claim CU mode on next run command. Drops the `cu_mode_set_`
    // cached flag so ensureCuMode_() writes H1000=0 again (+ 150ms sleep).
    //
    // Use when application suspects SE3 firmware lost CU mode but Modbus
    // ACK is still returning OK (silent firmware reject — symptom: run cmd
    // returns success at protocol level but motor doesn't engage).
    //
    // Bench observation 2026-05-13: cold-start fine_adjust intermittently
    // fails 2-3x before succeeding (driver watchdog reclaims after 2 fails).
    // Calling this before fine_adjust forces re-claim upfront.
    //
    // Lighter than clearAlarm() (which triggers full inverter reset).
    void invalidateCuModeCache();

    //=========== alarm management ===========

    // Clear OPT / fault alarm by writing magic H9696 to register H1101.
    // Per V1.03 manual §H1101 table: H9696 = 00-02=2 / P.997=1 = 變頻器復位
    // (inverter reset, clears all alarms and resets internal state).
    //
    // Side effects:
    //   - All alarms cleared
    //   - SE3 internal CU mode latch resets → next run cmd needs ensureCuMode_
    //     (driver flips cu_mode_set_ = false to force re-claim)
    //   - 200ms sleep after write to let SE3 firmware complete reset transition
    //
    // Use when readStatusWord shows b7 (fault) set, e.g., after comm timeout
    // triggered OPT alarm. Crane_control_PI's se3_keepalive_loop checks fault
    // bit and calls this with 5s cooldown to avoid spamming during sustained
    // disconnection.
    bool clearAlarm();

    //=========== generic param read/write ===========

    bool writeParam(uint16_t reg, uint16_t value);   // FC 0x06
    bool readParam(uint16_t reg, uint16_t& value);   // FC 0x03 single

    //=========== utility ===========

    int  getSlaveID() const { return deviceID; }

private:
    bool sendModbus(const uint8_t* req, int reqLen, uint8_t* resp, int& respLen);
    uint16_t crc16(const uint8_t* buf, int len);

    // Per V1.03 manual §7-3 example #1: write H1000 = 0 (set Modbus operation
    // mode to CU/通訊) before any run command. Required even when panel P.79
    // already set to 3 — Modbus side has its own mode latch.
    // Lazy + cached: only writes H1000 once per power-up. SE3 firmware rejects
    // H1000 writes while motor is running (returns Modbus exception, disturbs
    // bus), so we MUST avoid re-writing on every command.
    bool ensureCuMode_();
    bool cu_mode_set_ = false;   // sticky: set true on first successful H1000=0 write

    // Watchdog: if N consecutive H1001 writes fail (e.g., SE3 power-cycled
    // mid-session and lost CU mode), reset cu_mode_set_ and re-claim CU mode
    // automatically on next run command. Threshold is small so recovery is
    // fast but not on first failure (allow transient comm errors to retry on
    // next call). Helper run_h1001_with_watchdog_ wraps the H1001 write + retry.
    bool run_h1001_with_watchdog_(uint16_t value, const char* ctx);
    int  comm_fail_count_ = 0;
    static constexpr int RUN_FAIL_THRESHOLD = 2;   // reclaim CU after N consecutive fails

    TCP_client* client;
    bool        owns;
    int         deviceID;
    bool        debug_mode;
    std::string _log_tag;
};
