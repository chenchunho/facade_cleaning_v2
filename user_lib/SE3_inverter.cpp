#include "SE3_inverter.h"
#include "log_utils.h"
#include <string.h>
#include <math.h>
#include <algorithm>
#include <thread>
#include <chrono>

//=========== init ===========

SE3_inverter::SE3_inverter()
    : client(nullptr),
      owns(false),
      deviceID(1),
      debug_mode(false)
{
    _log_tag = "SE3:?";
}

SE3_inverter::~SE3_inverter()
{
    if (owns && client) delete client;
}

bool SE3_inverter::init(const std::string& ip, int port, int id, bool debug)
{
    client     = new TCP_client();
    owns       = true;
    deviceID   = id;
    debug_mode = debug;
    _log_tag   = "SE3:" + std::to_string(id);
    if (!client->connectToServer(ip, port)) {
        LOG_ERR(_log_tag, "connect failed %s:%d", ip.c_str(), port);
        return true;
    }
    return false;
}

bool SE3_inverter::init(TCP_client& extClient, int id, bool debug)
{
    client     = &extClient;
    owns       = false;
    deviceID   = id;
    debug_mode = debug;
    _log_tag   = "SE3:" + std::to_string(id);

    // Modbus probe: read status word (H1001) to verify the inverter actually
    // responds on the RS485 bus behind the shared TCP gateway. Without this
    // probe, TCP connect to the USR-TCP232 gateway returning OK (gateway
    // alive) is mistaken for "SE3 alive" — keepalive readStatusWord later
    // returns garbage / fault bits, looking like a real fault on a missing
    // device. See SD76 init Mode B for the same pattern.
    //
    // Retry + clearAlarm fallback (2026-05-15): SE3 may be in OPT alarm at
    // startup (residual from previous bench / unclean shutdown). OPT in some
    // firmware states blocks reads or causes timeout. Without retry, init fails
    // → caller sets g_dev_se3_*=false → keepalive skips this side → OPT never
    // auto-clears → SE3 permanently stuck. clearAlarm (H1101=0x9696) is the
    // standard recovery; do it after first probe fail and re-try. clearAlarm
    // has internal 200ms sleep so SE3 firmware settles before re-probe.
    constexpr int MAX_PROBE_ATTEMPTS = 3;
    for (int i = 0; i < MAX_PROBE_ATTEMPTS; ++i) {
        uint16_t st = 0;
        if (!readParam(0x1001, st)) {
            // Probe OK. If FAULT bit is set, opportunistic clearAlarm so the
            // caller doesn't start with SE3 in alarm state (would reject the
            // very first run cmd otherwise).
            if (st & 0x80) {
                LOG_WRN(_log_tag, "init probe OK but FAULT bit set (status=0x%04X) — clearAlarm", st);
                clearAlarm();
            }
            return false;   // success
        }
        LOG_WRN(_log_tag, "init probe attempt %d/%d failed (slave %d)", i + 1, MAX_PROBE_ATTEMPTS, id);
        if (i < MAX_PROBE_ATTEMPTS - 1) {
            // Try clearing OPT in case that's blocking the probe response.
            // clearAlarm internally sleeps 200ms so SE3 has time to settle
            // before we re-probe on next iteration.
            clearAlarm();
        }
    }
    LOG_ERR(_log_tag, "init Mode B probe failed after %d attempts — device not on bus (slave %d)",
            MAX_PROBE_ATTEMPTS, id);
    return true;
}

//=========== utility: CRC16 (Modbus standard) ===========

uint16_t SE3_inverter::crc16(const uint8_t* buf, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
    }
    return crc;
}

//=========== utility: send/recv ===========

bool SE3_inverter::sendModbus(const uint8_t* req, int reqLen,
                              uint8_t* resp, int& respLen)
{
    LOG_HEX(_log_tag, "TX", req, reqLen);

    // Atomic: TCP_client holds its mutex from drain → send → recv so a
    // concurrent caller on the same gateway (e.g. meter_loop reading SD76 on
    // shared cli_A) can't interleave between our send and recv and corrupt
    // the reply.
    //
    // Timeouts (2026-05-14, shortened recv 300→150): SE3 typical Modbus reply
    // is 10-50ms — 150ms is still 3-15x headroom. Cuts worst-case writeParam
    // fail latency from ~500ms to ~350ms, and with 8 retry attempts in
    // reliable_*_one the total wall time for a stubborn-fail cycle drops
    // from ~4.8s to ~2.4s. Tradeoff: SE3 doing internal heavy work (post-
    // clearAlarm reset transition, ramp boundary) may exceed 150ms once in
    // a while — caller's retry loop catches that.
    int got = client->sendAndReceive((const char*)req, reqLen,
                                     (char*)resp, 256,
                                     200, 150);
    if (got <= 0) {
        respLen = 0;
        return true;
    }
    respLen = got;

    LOG_HEX(_log_tag, "RX", resp, respLen);
    return false;
}

//=========== generic param write (FC 0x06 single register) ===========

bool SE3_inverter::writeParam(uint16_t reg, uint16_t value)
{
    uint8_t req[8];
    req[0] = (uint8_t)deviceID;
    req[1] = 0x06;
    req[2] = (uint8_t)(reg >> 8);
    req[3] = (uint8_t)(reg & 0xFF);
    req[4] = (uint8_t)(value >> 8);
    req[5] = (uint8_t)(value & 0xFF);
    uint16_t c = crc16(req, 6);
    req[6] = (uint8_t)(c & 0xFF);
    req[7] = (uint8_t)(c >> 8);

    uint8_t resp[256];
    int     respLen = 0;
    if (sendModbus(req, 8, resp, respLen)) {
        LOG_ERR(_log_tag, "writeParam reg=0x%04X val=0x%04X comm fail", reg, value);
        return true;
    }
    // Echo: 8-byte same as request (slave + 0x06 + reg + val + crc)
    if (respLen < 8 || resp[0] != deviceID || resp[1] != 0x06) {
        LOG_ERR(_log_tag, "writeParam reg=0x%04X bad reply len=%d", reg, respLen);
        return true;
    }
    return false;
}

//=========== generic param read (FC 0x03 single register) ===========

bool SE3_inverter::readParam(uint16_t reg, uint16_t& value)
{
    uint8_t req[8];
    req[0] = (uint8_t)deviceID;
    req[1] = 0x03;
    req[2] = (uint8_t)(reg >> 8);
    req[3] = (uint8_t)(reg & 0xFF);
    req[4] = 0x00;
    req[5] = 0x01;   // 1 register
    uint16_t c = crc16(req, 6);
    req[6] = (uint8_t)(c & 0xFF);
    req[7] = (uint8_t)(c >> 8);

    uint8_t resp[256];
    int     respLen = 0;
    if (sendModbus(req, 8, resp, respLen)) {
        LOG_ERR(_log_tag, "readParam reg=0x%04X comm fail", reg);
        return true;
    }
    // Reply: slave + 0x03 + bytecount(2) + hi + lo + crc(2) = 7 bytes
    if (respLen < 7 || resp[0] != deviceID || resp[1] != 0x03 || resp[2] != 0x02) {
        LOG_ERR(_log_tag, "readParam reg=0x%04X bad reply len=%d", reg, respLen);
        return true;
    }
    value = ((uint16_t)resp[3] << 8) | resp[4];
    return false;
}

//=========== control (run command via Modbus reg 0x1001) ===========
// V1.03 manual §7-3 「例一．通訊寫操作模式為 CU（通訊）模式」 says step 1 is to
// write H1000 = 0 (set Modbus operation mode to CU/通訊). Even when panel P.79=3
// puts SE3 in CU mode, the Modbus side may need its own explicit H1000 = 0 write
// before run commands at H1001 will be accepted. SL-INV likely does this in
// background. We do it lazily on first run command.
//
// Bit layout @ H1001 (write):
//   b0 reserved, b1 STF, b2 STR, b3 RL, b4 RM, b5 RH, b6 RT, b7 MRS.
// Stop = clear all bits (0x0000) → motor decel per P.7 ramp.
//
// Required SE3 panel setup for Modbus run command to work:
//   P.33 (07-00) = 0     protocol = Modbus
//   P.154 (07-07) = 3    format = 8N2 RTU (or 4=8E1 / 5=8O1, match USR parity)
//   P.36 (07-01) = <id>  station, NOT 0
//   P.79 (00-16) = 3     operation mode = 通訊
//   P.35 (00-19) = 0     comm-mode command source = communication

bool SE3_inverter::ensureCuMode_()
{
    // Cached: only write H1000 = 0 once per power-up. SE3 firmware rejects
    // H1000 writes while motor is running (Modbus exception, disturbs bus
    // state and corrupts subsequent writes). After first success, the latch
    // stays set inside SE3 until power cycle — no need to re-write.
    if (cu_mode_set_) return false;

    // Manual H1000 mapping: 0 = 通訊模式 (CU). Best-effort: if write fails
    // (e.g., comm timeout), log warning but don't latch — try again next time.
    if (writeParam(0x1000, 0x0000)) {
        LOG_WRN(_log_tag, "ensureCuMode write H1000=0 fail (proceeding anyway)");
        return true;
    }
    // SE3 firmware needs ~50-150ms after the H1000=0 ack to actually transition
    // its internal state machine into CU mode. Without this settle, an immediate
    // H1001 (run cmd) write that follows can be silently rejected — observed on
    // bench 2026-05-08 when both left/right SE3 are first-touched in dual
    // operation (Linux_test menu 25). Single-side path tends to hide this
    // because user prompt typing already covers the settle window.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    cu_mode_set_ = true;
    return false;
}

// runForward / runReverse: ensure CU mode latched (lazy first-time write).
// stopDecel / emergencyStop: skip ensureCuMode_ entirely. By the time stop is
// called, motor is running → H1000 write would be rejected by firmware. Stop
// only needs H1001 = 0, which works regardless of CU mode latch state.
//
// All four commands go through run_h1001_with_watchdog_, which detects N
// consecutive comm failures (e.g., SE3 power-cycled mid-session and lost CU
// mode) and auto-recovers by re-writing H1000 = 0 + retrying the original cmd.

bool SE3_inverter::run_h1001_with_watchdog_(uint16_t value, const char* ctx) {
    bool err = writeParam(0x1001, value);
    if (!err) {
        comm_fail_count_ = 0;
        return false;
    }

    comm_fail_count_++;
    LOG_WRN(_log_tag, "run %s fail (count=%d/%d)",
            ctx, comm_fail_count_, RUN_FAIL_THRESHOLD);

    if (comm_fail_count_ < RUN_FAIL_THRESHOLD) {
        return true;   // not yet at threshold, let caller decide / next call retries
    }

    // Threshold reached — assume SE3 lost CU mode (power cycle / reset / latched
    // some bad state). Re-claim CU and retry once.
    LOG_INF(_log_tag, "watchdog: re-claiming CU mode after %d fails",
            comm_fail_count_);
    cu_mode_set_ = false;
    comm_fail_count_ = 0;

    if (ensureCuMode_()) {
        LOG_ERR(_log_tag, "watchdog: ensureCuMode_ also failed, giving up");
        return true;
    }

    if (writeParam(0x1001, value)) {
        LOG_ERR(_log_tag, "watchdog: retry %s after CU re-claim also failed", ctx);
        return true;
    }

    LOG_INF(_log_tag, "watchdog: %s succeeded after CU re-claim", ctx);
    comm_fail_count_ = 0;
    return false;
}

bool SE3_inverter::runForward() {
    ensureCuMode_();
    return run_h1001_with_watchdog_(0x0002, "fwd");
}
bool SE3_inverter::runReverse() {
    ensureCuMode_();
    return run_h1001_with_watchdog_(0x0004, "rev");
}
bool SE3_inverter::stopDecel() {
    return run_h1001_with_watchdog_(0x0000, "stop");
}
bool SE3_inverter::emergencyStop() {
    return run_h1001_with_watchdog_(0x0080, "emergency");
}

//=========== CU mode management ===========

void SE3_inverter::invalidateCuModeCache()
{
    LOG_INF(_log_tag, "invalidateCuModeCache: forcing re-claim on next run cmd");
    cu_mode_set_     = false;
    comm_fail_count_ = 0;
}

//=========== alarm management ===========

// Write magic H9696 to H1101 to trigger SE3 internal reset (clears OPT and
// all other alarms). Per V1.03 manual H1101 table this is equivalent to
// panel 00-02=2 / P.997=1.
//
// After reset, SE3 internal state machine restarts — CU mode latch resets,
// so we flip cu_mode_set_ = false to force re-claim on next run command.
bool SE3_inverter::clearAlarm()
{
    if (writeParam(0x1101, 0x9696)) {
        LOG_ERR(_log_tag, "clearAlarm: write H1101=0x9696 failed");
        return true;
    }
    LOG_INF(_log_tag, "clearAlarm: H1101=0x9696 written (inverter reset), waiting 200ms");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    cu_mode_set_      = false;   // force re-claim CU mode on next run cmd
    comm_fail_count_  = 0;       // reset watchdog counter
    return false;
}

//=========== frequency setpoint via Modbus reg 0x1002 (RAM write) ===========

bool SE3_inverter::setFreqHz(double hz, double max_hz)
{
    if (hz < 0.0) hz = 0.0;
    if (hz > max_hz) hz = max_hz;
    uint16_t raw = (uint16_t)std::lround(hz * 100.0);   // 0.01 Hz units
    return setFreqRaw(raw);
}

bool SE3_inverter::setFreqRaw(uint16_t value_001hz)
{
    return writeParam(0x1002, value_001hz);
}

//=========== monitor reads ===========

// FIXED 2026-05-07: monitor addresses were wrong (H100A/B/C are line-speed /
// tension / torque setpoints, NOT output Hz/V/A). Correct addresses per V1.03
// manual §7-3 監視類: H1003 輸出頻率, H1004 輸出電流, H1005 輸出電壓.

bool SE3_inverter::readOutputFreqHz(double& out_hz)
{
    uint16_t v = 0;
    if (readParam(0x1003, v)) return true;
    out_hz = v / 100.0;   // H1003 unit: 0.01 Hz
    return false;
}

bool SE3_inverter::readOutputCurrentA(double& out_amp)
{
    uint16_t v = 0;
    if (readParam(0x1004, v)) return true;
    out_amp = v / 100.0;   // H1004 unit: 0.01 A (manual: "2 位小數")
    return false;
}

bool SE3_inverter::readOutputVoltageV(double& out_volt)
{
    uint16_t v = 0;
    if (readParam(0x1005, v)) return true;
    out_volt = v / 100.0;   // H1005 unit: 0.01 V (manual: "2 位小數")
    return false;
}

bool SE3_inverter::readStatusWord(uint16_t& out)
{
    return readParam(0x1001, out);
}

// Read fault history packed as two 16-bit registers (4 fault codes total).
// H1007 = latest 2 codes, H1008 = previous 2 codes. Each byte holds one code
// per manual §異常代碼 table (160=OPT, 144=OHT, 16-19=OC1-0, 32-35=OV1-0 etc).
// Byte ordering (high vs low = which is more recent) is bench-verified by
// observation since manual is ambiguous; out_f1 is high byte of H1007 by
// convention.
bool SE3_inverter::readFaultCode(uint8_t& out_f1, uint8_t& out_f2,
                                 uint8_t& out_f3, uint8_t& out_f4)
{
    uint16_t v1 = 0, v2 = 0;
    if (readParam(0x1007, v1)) return true;
    if (readParam(0x1008, v2)) return true;
    out_f1 = (uint8_t)((v1 >> 8) & 0xFF);
    out_f2 = (uint8_t)( v1       & 0xFF);
    out_f3 = (uint8_t)((v2 >> 8) & 0xFF);
    out_f4 = (uint8_t)( v2       & 0xFF);
    return false;
}
