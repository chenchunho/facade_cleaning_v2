#include "DSZL_107.h"
#include "log_utils.h"
#ifdef _WIN32
#include <windows.h>
#include <thread>
#else
#include <unistd.h>
#define Sleep(ms) usleep((ms) * 1000)
#endif

#include <cstring>
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>

// X518 register addresses (decoded from x518多通道数据采集器操作手册v1.1.pdf
// + 2026-05-08 bench validation):
//   0x0A00 .. 0x0A03  : channel data, 4 registers = 2 longs (CH1, CH2) — read FC03
//   0x0A20            : multi-purpose command reg (write FC10):
//                         value 1/2 → zero CH1/CH2
//                         value 7   → zero all channels
//                         value 40  → SAVE parameters (any param/zero/cal change
//                                     needs a follow-up save to persist across
//                                     power-cycle; manual is explicit on this)
//   0x0614            : unit selector (1=t 2=kg 3=g 4=kN 5=N 6=lb, default 5)
//   0x063E / 0x0640   : own IPH / IPL (encoding: oct1*1000 + oct2)
//   0x0642 / 0x0644   : own port / mode (1=Modbus TCP, default 1)
//   0x0636            : baudrate index (RS485 only, irrelevant in TCP-direct)
//   0x0638            : data format    (RS485 only, irrelevant in TCP-direct)
//   0x064C            : Modbus slave / unit ID (default 1)
//
// Wire-level: Modbus TCP (MBAP header + PDU, no CRC16). All long writes use
// FC10 (write multiple registers, 2 registers / 4 bytes). Reads use FC03.
//
// 2026-05-08 — switched from RTU+CRC16 (USR-TCP232 transparent gateway path)
// to MBAP+TCP (X518 direct on switch :502). Public API unchanged.

//=========== init ===========

DSZL_107::DSZL_107()
    : client(nullptr),
      debug_mode(false),
      slaveID(1),
      txid_(0),
      scaleToKg(0.01),
      lastValidKg(0.0),
      errorCount(0)
{
    _log_tag = "DSZL:?";
}

DSZL_107::~DSZL_107()
{
}

bool DSZL_107::init(const std::string& ip, int port, int ID, bool debug)
{
    slaveID    = (uint8_t)ID;
    debug_mode = debug;
    _log_tag   = "DSZL:" + std::to_string(ID);
    client     = &ownedClient;
    return !client->connectToServer(ip, port);
}

bool DSZL_107::init(TCP_client& extClient, int ID, bool debug)
{
    this->client     = &extClient;
    this->slaveID    = (uint8_t)ID;
    this->debug_mode = debug;
    _log_tag         = "DSZL:" + std::to_string(ID);
    return false;
}

//=========== control: communication params ===========

void DSZL_107::set_communication_parm(int ID, int baud, int format)
{
    bool valid = true;

    if (ID < 1 || ID > 255) {
        LOG_ERR(_log_tag, "ID out of range: %d", ID);
        valid = false;
    }
    if (baud < 0 || baud > 6) {
        // 0=2400 1=4800 2=9600 3=19200 4=38400 5=57600 6=115200
        LOG_ERR(_log_tag, "Baud index out of range: %d", baud);
        valid = false;
    }
    if (format < 0 || format > 5) {
        // 0=n81 1=n82 2=e81 3=e82 4=o81 5=o82
        LOG_ERR(_log_tag, "Format out of range: %d", format);
        valid = false;
    }
    if (!valid) {
        LOG_ERR(_log_tag, "communication params not applied");
        return;
    }

    modbus_write_long(0x064C, ID);
    modbus_write_long(0x0636, baud);
    modbus_write_long(0x0638, format);

    LOG_INF(_log_tag, "communication params updated: ID=%d Baud=%d Format=%d",
            ID, baud, format);
}

//=========== utility: Modbus TCP Read (FC03 with MBAP) ===========
//
// Frame layout:
//   Request: [txid_hi][txid_lo][0][0][len_hi=0][len_lo=6]
//            [unit][fc=0x03][addr_hi][addr_lo][qty_hi][qty_lo]
//   Reply:   [txid][txid][0][0][len][len][unit][fc][bc][data...]
//
// To preserve the legacy RTU-style buffer layout that callers (parse_long
// at offset 3) expect, we strip the first 6 MBAP bytes and hand back
// rx = [unit][fc][bc][data...]. rxLen reflects this stripped length so
// existing length checks (e.g. get_both_long: len < 11) still work.

bool DSZL_107::modbus_read(uint16_t addr, uint16_t quantity, uint8_t* rx, int& rxLen)
{
    ++txid_;
    uint8_t req[12] = {
        (uint8_t)(txid_ >> 8), (uint8_t)(txid_ & 0xFF),     // txid
        0, 0,                                                // proto = 0
        0, 6,                                                // PDU length = 6
        slaveID,
        0x03,
        (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
        (uint8_t)(quantity >> 8), (uint8_t)(quantity & 0xFF)
    };

    LOG_HEX(_log_tag, "TX read", req, (int)sizeof(req));

    if (!client->sendData((char*)req, (int)sizeof(req), 100))
        return true;

    char buf[256];
    int n = client->receiveData(buf, sizeof(buf), 400);
    if (n < 9) return true;        // need at least MBAP(7) + fc + bc

    LOG_HEX(_log_tag, "RX read", buf, n);

    uint8_t fc = (uint8_t)buf[7];
    if (fc == (0x03 | 0x80)) {
        LOG_ERR(_log_tag, "Modbus exception code=0x%02X at addr=0x%04X",
                (uint8_t)buf[8], addr);
        return true;
    }
    if (fc != 0x03) return true;

    int bc = (uint8_t)buf[8];
    if (n < 9 + bc) return true;

    // Strip MBAP header except unit byte: caller sees [unit][fc][bc][data...]
    int payload = 3 + bc;
    memcpy(rx, buf + 6, payload);
    rxLen = payload;
    return false;
}

//=========== utility: Modbus TCP Write LONG (FC10 with MBAP, 2 registers) ===========
//
// Request: [txid][txid][0][0][len=11][unit][fc=0x10][addr][addr]
//          [qty=0,2][bcount=4][d3][d2][d1][d0]
// Reply:   [txid][txid][0][0][len=6][unit][fc=0x10][addr][addr][qty=0,2]  (12 bytes)

bool DSZL_107::modbus_write_long(uint16_t addr, int32_t value)
{
    ++txid_;
    uint8_t req[17] = {
        (uint8_t)(txid_ >> 8), (uint8_t)(txid_ & 0xFF),     // txid
        0, 0,                                                // proto = 0
        0, 11,                                               // PDU length = 11
        slaveID,
        0x10,
        (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
        0x00, 0x02,                                          // 2 registers
        0x04,                                                // 4 bytes data
        (uint8_t)((value >> 24) & 0xFF),
        (uint8_t)((value >> 16) & 0xFF),
        (uint8_t)((value >> 8)  & 0xFF),
        (uint8_t)( value        & 0xFF)
    };

    LOG_HEX(_log_tag, "TX write_long", req, (int)sizeof(req));

    if (!client->sendData((char*)req, (int)sizeof(req), 100))
        return true;

    char buf[32];
    int n = client->receiveData(buf, sizeof(buf), 300);
    if (n < 12) return true;

    LOG_HEX(_log_tag, "RX write_long", buf, n);

    uint8_t fc = (uint8_t)buf[7];
    if (fc == (0x10 | 0x80)) {
        LOG_ERR(_log_tag, "Modbus exception code=0x%02X at addr=0x%04X",
                (uint8_t)buf[8], addr);
        return true;
    }
    return fc != 0x10;
}

//=========== utility: parse long (Big Endian, d3 d2 d1 d0) ===========

int32_t DSZL_107::parse_long(uint8_t* buf, int index)
{
    uint32_t raw =
        ((uint32_t)buf[index]     << 24) |
        ((uint32_t)buf[index + 1] << 16) |
        ((uint32_t)buf[index + 2] <<  8) |
        ((uint32_t)buf[index + 3]);
    return (int32_t)raw;
}

//=========== read: register long (with retry + delay) ===========

bool DSZL_107::read_reg_long(uint16_t addr, int32_t& out)
{
    for (int retry = 0; retry < 3; retry++)
    {
        uint8_t buf[64];
        int len = 0;

        if (!modbus_read(addr, 2, buf, len))
        {
            // FC03 reply layout: [slave][fc][bytecount][d3 d2 d1 d0 ...]
            out = parse_long(&buf[3], 0);
            Sleep(8);
            return false;
        }
        Sleep(8);
    }
    return true;
}

//=========== read ===========

bool DSZL_107::get_tension_long(int32_t& outValue)
{
    return read_reg_long(0x0A00, outValue);
}

bool DSZL_107::get_both_long(int32_t& ch1, int32_t& ch2)
{
    // Single FC03 read of 4 registers from 0x0A00 covering both CH1 + CH2 longs.
    for (int retry = 0; retry < 3; retry++)
    {
        uint8_t buf[64];
        int len = 0;

        if (!modbus_read(0x0A00, 4, buf, len))
        {
            // FC03 reply: [slave][fc][bytecount=8][d3 d2 d1 d0  d3 d2 d1 d0]
            if (len < 11) return true;
            ch1 = parse_long(&buf[3], 0);
            ch2 = parse_long(&buf[3], 4);
            Sleep(8);
            return false;
        }
        Sleep(8);
    }
    return true;
}

bool DSZL_107::get_tension_kg(double& outKg)
{
    constexpr int ERROR_THRESHOLD = 10;

    int32_t raw = 0;
    if (get_tension_long(raw))
    {
        errorCount++;
        outKg = lastValidKg;
        if (errorCount < ERROR_THRESHOLD)
            return false;   // soft error: caller still gets cached value
        LOG_ERR(_log_tag, "get_tension_kg: consecutive errors reached threshold");
        return true;
    }

    const double kg = (double)raw * scaleToKg;

    // Sanity bound: tension reading should be within plausible range. Adjust
    // based on real load cell capacity once known.
    if (std::isnan(kg) || std::isinf(kg) || kg < -5000.0 || kg > 5000.0) {
        errorCount++;
        outKg = lastValidKg;
        if (errorCount < ERROR_THRESHOLD)
            return false;
        LOG_ERR(_log_tag, "get_tension_kg: insane value %f", kg);
        return true;
    }

    lastValidKg  = kg;
    outKg        = kg;
    errorCount   = 0;
    return false;
}

//=========== control: zero ===========

bool DSZL_107::do_zero_ch1() { return modbus_write_long(0x0A20, 1); }
bool DSZL_107::do_zero_ch2() { return modbus_write_long(0x0A20, 2); }
bool DSZL_107::do_zero_all() { return modbus_write_long(0x0A20, 7); }

//=========== control: persist params to flash ===========

// Write 0xA20 = 40 (decimal) → X518 copies RAM params to flash. Per manual,
// CPU pauses for ~100ms during the copy; we sleep 150ms after the write so
// the next Modbus call doesn't race with X518's busy state. Returns true
// on Modbus error (write itself failed before X518 starts the copy).
bool DSZL_107::save_params()
{
    if (modbus_write_long(0x0A20, 40)) {
        LOG_ERR(_log_tag, "save_params: write 0xA20=40 failed");
        return true;
    }
    LOG_INF(_log_tag, "save_params: RAM → flash committed (sleeping 150ms)");
    Sleep(150);
    return false;
}

//=========== control: unit ===========

bool DSZL_107::read_unit(int& unit)
{
    int32_t v = 0;
    if (read_reg_long(0x0614, v)) return true;
    unit = (int)v;
    return false;
}

bool DSZL_107::set_unit(int unit)
{
    if (unit < 1 || unit > 6) {
        LOG_ERR(_log_tag, "set_unit: invalid unit %d (expected 1..6)", unit);
        return true;
    }
    return modbus_write_long(0x0614, unit);
}

bool DSZL_107::set_unit_kg() { return set_unit(2); }
