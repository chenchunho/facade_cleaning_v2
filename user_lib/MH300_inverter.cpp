#include "MH300_inverter.h"
#include "log_utils.h"
#include <string.h>
#include <math.h>
#include <algorithm>
#include <thread>
#include <chrono>

// ---------------------------------------------------------------------------
// Register addresses (Delta ACMD Modbus list — see header for full bit maps).
// ---------------------------------------------------------------------------
namespace {
constexpr uint16_t REG_CMD        = 0x2000;  // run / direction command word
constexpr uint16_t REG_FREQ_SET   = 0x2001;  // frequency setpoint, 0.01 Hz
constexpr uint16_t REG_AUX_CMD    = 0x2002;  // EF(b0) / RESET(b1) / B.B(b2)
constexpr uint16_t REG_ERR_CODE   = 0x2100;  // error(b7~0) + warning(b15~8)
constexpr uint16_t REG_STATUS     = 0x2101;  // drive status word
constexpr uint16_t REG_OUT_FREQ   = 0x2102;  // output frequency, 0.01 Hz
constexpr uint16_t REG_OUT_CURR   = 0x2104;  // output current
constexpr uint16_t REG_OUT_VOLT   = 0x2106;  // output voltage
constexpr uint16_t REG_P_00_20    = 0x0014;  // freq command source
constexpr uint16_t REG_P_00_21    = 0x0015;  // run command source

// Command-word bit patterns for REG_CMD (0x2000).
constexpr uint16_t CMD_STOP    = 0x0001;  // bit1~0 = 01
constexpr uint16_t CMD_RUN_FWD = 0x0012;  // RUN(10b) | FWD(01b<<4)
constexpr uint16_t CMD_RUN_REV = 0x0022;  // RUN(10b) | REV(10b<<4)

// Aux-command bits for REG_AUX_CMD (0x2002).
constexpr uint16_t AUX_RESET = 0x0002;  // bit1 — clear fault/alarm
constexpr uint16_t AUX_BB    = 0x0004;  // bit2 — base block (output cutoff)

// ⚠ SCALE — bench-verify against keypad, then adjust HERE only.
// Manual annotations are inconsistent (2104H "XXX.X A" vs 2103H "XX.XX A").
// Frequency is unambiguous (0.01 Hz) and hard-coded /100 below.
constexpr double OUTPUT_CURRENT_SCALE = 0.1;   // register unit -> Amps
constexpr double OUTPUT_VOLTAGE_SCALE = 0.1;   // register unit -> Volts
} // namespace

//=========== init ===========

MH300_inverter::MH300_inverter()
    : client(nullptr),
      owns(false),
      deviceID(1),
      debug_mode(false)
{
    _log_tag = "MH300:?";
}

MH300_inverter::~MH300_inverter()
{
    if (owns && client) delete client;
}

bool MH300_inverter::init(const std::string& ip, int port, int id, bool debug)
{
    client     = new TCP_client();
    owns       = true;
    deviceID   = id;
    debug_mode = debug;
    _log_tag   = "MH300:" + std::to_string(id)
               + (_log_side.empty() ? "" : "@" + _log_side);   // [2026-08-31] 側別
    if (!client->connectToServer(ip, port)) {
        LOG_ERR(_log_tag, "connect failed %s:%d", ip.c_str(), port);
        return true;
    }
    return false;
}

bool MH300_inverter::init(TCP_client& extClient, int id, bool debug)
{
    client     = &extClient;
    owns       = false;
    deviceID   = id;
    debug_mode = debug;
    _log_tag   = "MH300:" + std::to_string(id)
               + (_log_side.empty() ? "" : "@" + _log_side);   // [2026-08-31] 側別

    // Modbus probe: read the status word to verify the drive actually answers
    // on the RS485 bus behind the shared TCP gateway. A TCP connect to the
    // USR-TCP232 gateway succeeding only proves the gateway is alive, not the
    // drive. Same pattern as SE3 / SD76 Mode B. Unlike SE3, MH300 has no OPT
    // latch that blocks reads, so a plain retry (no clearAlarm dance) suffices.
    constexpr int MAX_PROBE_ATTEMPTS = 3;
    for (int i = 0; i < MAX_PROBE_ATTEMPTS; ++i) {
        uint16_t st = 0;
        if (!readParam(REG_STATUS, st)) {
            uint16_t err = 0;
            if (!readParam(REG_ERR_CODE, err) && (err & 0x00FF)) {
                LOG_WRN(_log_tag, "init probe OK but error code=0x%02X set — clearAlarm",
                        (unsigned)(err & 0x00FF));
                clearAlarm();
            }
            return false;   // success
        }
        LOG_WRN(_log_tag, "init probe attempt %d/%d failed (slave %d)",
                i + 1, MAX_PROBE_ATTEMPTS, id);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LOG_ERR(_log_tag, "init Mode B probe failed after %d attempts — device not on bus (slave %d)",
            MAX_PROBE_ATTEMPTS, id);
    return true;
}

//=========== utility: CRC16 (Modbus standard) ===========

uint16_t MH300_inverter::crc16(const uint8_t* buf, int len)
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

bool MH300_inverter::sendModbus(const uint8_t* req, int reqLen,
                                uint8_t* resp, int& respLen)
{
    // [2026-08-29] Null-client guard: the constructor leaves `client` as nullptr
    // and only init() sets it, so a call on an un-init'd (or failed-init)
    // instance dereferences nullptr and takes the whole process down.
    // Application layers already gate these calls, but that is the caller
    // remembering to be careful — the driver must not be a landmine.
    if (!client) { respLen = 0; return true; }
    LOG_HEX(_log_tag, "TX", req, reqLen);

    // Atomic drain->send->recv inside TCP_client's mutex so a concurrent caller
    // on the same gateway (e.g. the other winch SE3/MH300, or SD76 meter poll)
    // can't interleave and corrupt the reply. Timeouts mirror SE3 (200ms
    // connect-drain / 150ms recv): MH300 typical reply is 10-50ms.
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

bool MH300_inverter::writeParam(uint16_t reg, uint16_t value)
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
    // Echo: 8-byte copy of the request (slave + 0x06 + reg + val + crc).
    // A Modbus exception reply is 5 bytes: slave + (0x06|0x80=0x86) + code + crc,
    // so surface the exception code — it says WHY the drive rejected the write.
    // [2026-08-28] CRC added by the driver audit (same shape as SE3_inverter).
    // Echo frame = slave+fc+addr(2)+val(2)+crc(2); without this a bit-flipped
    // echo that left slave and FC intact was accepted as a successful write.
    if (respLen < 8 || resp[0] != deviceID || resp[1] != 0x06
        || crc16(resp, 6) != (uint16_t)(resp[6] | (resp[7] << 8))) {
        if (respLen >= 3 && resp[1] == 0x86) {
            LOG_ERR(_log_tag, "writeParam reg=0x%04X REJECTED — Modbus exception 0x%02X "
                              "(01=illegal func, 02=illegal addr, 03=illegal value, "
                              "04=dev fail, 06=busy)", reg, resp[2]);
        } else {
            LOG_ERR(_log_tag, "writeParam reg=0x%04X bad reply len=%d", reg, respLen);
        }
        LOG_HEX(_log_tag, "writeParam RX", resp, respLen);
        return true;
    }
    return false;
}

//=========== generic param read (FC 0x03 single register) ===========

bool MH300_inverter::readParam(uint16_t reg, uint16_t& value)
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
    // Reply: slave + 0x03 + bytecount(2) + hi + lo + crc(2) = 7 bytes.
    // [2026-08-28] CRC added — see writeParam above. Read frame =
    // slave+fc+bc(=2)+data(2)+crc(2).
    if (respLen < 7 || resp[0] != deviceID || resp[1] != 0x03 || resp[2] != 0x02
        || crc16(resp, 5) != (uint16_t)(resp[5] | (resp[6] << 8))) {
        LOG_ERR(_log_tag, "readParam reg=0x%04X bad reply len=%d", reg, respLen);
        return true;
    }
    value = ((uint16_t)resp[3] << 8) | resp[4];
    return false;
}

//=========== control (run command via reg 0x2000, aux via 0x2002) ===========
// MH300 needs no CU-mode latch: with 00-21=2 (run source = RS-485), writing the
// command word to 0x2000 runs the motor directly. Direction is encoded in the
// same word (bit5~4), so a single write sets run + direction atomically.

// Release a base-block left by a prior emergencyStop() before running, so the
// run command actually engages. No-op (skipped) when no emergency cutoff is
// pending, so normal starts pay no extra Modbus write. Best-effort: a failed
// clear doesn't abort the run — the caller's retry will clear again next time.
void MH300_inverter::releaseBaseBlockIfNeeded_() {
    if (!base_blocked_) return;
    writeParam(REG_AUX_CMD, 0x0000);   // clear B.B / EF bits on 0x2002
    base_blocked_ = false;
}

bool MH300_inverter::runForward() {
    releaseBaseBlockIfNeeded_();
    return writeParam(REG_CMD, CMD_RUN_FWD);
}
bool MH300_inverter::runReverse() {
    releaseBaseBlockIfNeeded_();
    return writeParam(REG_CMD, CMD_RUN_REV);
}
bool MH300_inverter::stopDecel() {
    return writeParam(REG_CMD, CMD_STOP);
}
bool MH300_inverter::emergencyStop() {
    // Base block: immediate output cutoff (motor coasts), independent of the
    // run word. Sets base_blocked_ so the next runForward/runReverse clears
    // 0x2002 first (SE3 MRS parity). clearAlarm() also releases it.
    base_blocked_ = true;
    return writeParam(REG_AUX_CMD, AUX_BB);
}

//=========== SE3-compat no-op ===========

void MH300_inverter::invalidateCuModeCache()
{
    // MH300 has no CU-mode latch. Kept for SE3->MH300 source compatibility.
    LOG_DBG(_log_tag, "invalidateCuModeCache: no-op on MH300 (no CU latch)");
}

//=========== alarm management ===========

bool MH300_inverter::clearAlarm()
{
    // Pulse RESET (0x2002 bit1) then clear the aux word back to 0 so the EF/BB
    // bits are left inactive. 200ms settle lets the drive finish the reset.
    if (writeParam(REG_AUX_CMD, AUX_RESET)) {
        LOG_ERR(_log_tag, "clearAlarm: write RESET (0x2002=0x0002) failed");
        return true;
    }
    LOG_INF(_log_tag, "clearAlarm: RESET pulsed, waiting 200ms");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (writeParam(REG_AUX_CMD, 0x0000)) {
        LOG_WRN(_log_tag, "clearAlarm: clearing aux word back to 0 failed (proceeding)");
        return true;
    }
    base_blocked_ = false;   // 0x2002 cleared → any pending base-block released
    return false;
}

//=========== commissioning helper (writes EEPROM — call once) ===========

bool MH300_inverter::configureModbusControl()
{
    // 00-20 = 1 (freq source RS-485), 00-21 = 2 (run source RS-485).
    if (writeParam(REG_P_00_20, 0x0001)) {
        LOG_ERR(_log_tag, "configureModbusControl: write 00-20=1 failed");
        return true;
    }
    if (writeParam(REG_P_00_21, 0x0002)) {
        LOG_ERR(_log_tag, "configureModbusControl: write 00-21=2 failed");
        return true;
    }
    LOG_INF(_log_tag, "configureModbusControl: 00-20=1 / 00-21=2 set (RS-485 command source)");
    return false;
}

//=========== frequency setpoint via reg 0x2001 (RAM write) ===========

bool MH300_inverter::setFreqHz(double hz, double max_hz)
{
    if (hz < 0.0) hz = 0.0;
    if (hz > max_hz) hz = max_hz;
    uint16_t raw = (uint16_t)std::lround(hz * 100.0);   // 0.01 Hz units
    return setFreqRaw(raw);
}

bool MH300_inverter::setFreqRaw(uint16_t value_001hz)
{
    return writeParam(REG_FREQ_SET, value_001hz);
}

//=========== monitor reads ===========

bool MH300_inverter::readOutputFreqHz(double& out_hz)
{
    uint16_t v = 0;
    if (readParam(REG_OUT_FREQ, v)) return true;
    out_hz = v / 100.0;   // 0.01 Hz — trusted
    return false;
}

bool MH300_inverter::readOutputCurrentA(double& out_amp)
{
    uint16_t v = 0;
    if (readParam(REG_OUT_CURR, v)) return true;
    out_amp = v * OUTPUT_CURRENT_SCALE;   // ⚠ scale bench-verify
    return false;
}

bool MH300_inverter::readOutputVoltageV(double& out_volt)
{
    uint16_t v = 0;
    if (readParam(REG_OUT_VOLT, v)) return true;
    out_volt = v * OUTPUT_VOLTAGE_SCALE;  // ⚠ scale bench-verify
    return false;
}

bool MH300_inverter::readStatusWord(uint16_t& out)
{
    return readParam(REG_STATUS, out);
}

bool MH300_inverter::readErrorCode(uint16_t& out_code)
{
    return readParam(REG_ERR_CODE, out_code);
}

// SE3-API-compat: MH300 keeps a single live error+warning word (0x2100), not a
// 4-deep fault history. Map error->f1, warning->f2, f3/f4=0.
bool MH300_inverter::readFaultCode(uint8_t& out_f1, uint8_t& out_f2,
                                   uint8_t& out_f3, uint8_t& out_f4)
{
    uint16_t v = 0;
    if (readParam(REG_ERR_CODE, v)) return true;
    out_f1 = (uint8_t)( v       & 0xFF);   // error code
    out_f2 = (uint8_t)((v >> 8) & 0xFF);   // warning code
    out_f3 = 0;
    out_f4 = 0;
    return false;
}
