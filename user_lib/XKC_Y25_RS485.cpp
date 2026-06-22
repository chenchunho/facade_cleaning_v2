#include "XKC_Y25_RS485.h"
#include "log_utils.h"

#include <chrono>
#include <thread>

// MODBUS register map for XKC-Y25-RS485 V1.6.
//
// IMPORTANT: the manual is internally inconsistent — §2.1 lists ADR @ 0x0003 and
// BAUD @ 0x0004 (those are READ-current-value registers), but §1.6 / §1.8 show
// that the SET operations use DIFFERENT registers (the operation registers are
// +1 from the read registers). Bench-verified 2026-05-20.
//
//   reg     read                     write (directed)             write (broadcast)
//   0x0001  OutPut (0/1)             —                            —
//   0x0002  RSSI                     —                            —
//   0x0003  current ADR (R/O)        — (writes here are ignored)  —
//   0x0004  current baud (R/O)       SET ADR (value = new_addr)   factory reset (value=0x02)
//   0x0005  —                        SET BAUD (value = baud code) —
namespace XKC_REG {
    constexpr uint16_t OUTPUT       = 0x0001;  // 0 = no liquid, 1 = liquid (R)
    constexpr uint16_t RSSI         = 0x0002;  // signal strength (R)
    constexpr uint16_t SET_ADDR     = 0x0004;  // directed write: set new ADR (§1.6)
    constexpr uint16_t SET_BAUD     = 0x0005;  // directed write: set new baud code (§1.8)
}

//=========== init ===========

XKC_Y25_RS485::XKC_Y25_RS485()
    : error_flag(0)
    , _slaveID(1)
    , debug_mode(false)
    , client(nullptr)
    , _isExternalClient(false)
    , _last_output(0)
    , _last_rssi(0)
{
    _log_tag = "XKC:?";
}

XKC_Y25_RS485::~XKC_Y25_RS485() {
    if (!_isExternalClient && client != nullptr) {
        delete client;
        client = nullptr;
    }
}

bool XKC_Y25_RS485::init(TCP_client& extClient, int ID, bool debug) {
    _slaveID = ID;
    debug_mode = debug;
    client = &extClient;
    _isExternalClient = true;
    _log_tag = "XKC:" + std::to_string(ID);
    return false;
}

bool XKC_Y25_RS485::init(const std::string& ip, int port, int ID, bool debug) {
    _slaveID = ID;
    debug_mode = debug;
    _isExternalClient = false;
    _log_tag = "XKC:" + std::to_string(ID);
    if (client) delete client;
    client = new TCP_client();
    return !client->connectToServer(ip, port, debug);
}

//=========== utility: Modbus send/recv ===========

bool XKC_Y25_RS485::sendRecv(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx, int expected_rx_len) {
    if (!client || !client->isConnected()) {
        error_flag = 1;
        return true;
    }

    LOG_HEX(_log_tag, "TX", tx.data(), (int)tx.size());

    if (!client->sendData((const char*)tx.data(), (int)tx.size(), 500)) {
        error_flag = 1;
        LOG_ERR(_log_tag, "send fail");
        return true;
    }

    char buf[64];
    int len = client->receiveData(buf, sizeof(buf), 1000);
    if (len < expected_rx_len) {
        error_flag = 1;
        LOG_ERR(_log_tag, "rx short: got %d bytes, expected %d", len, expected_rx_len);
        return true;
    }

    LOG_HEX(_log_tag, "RX", buf, len);

    // CRC check (Modbus RTU: low byte first)
    uint16_t cCrc = modbusCRC((uint8_t*)buf, len - 2);
    uint16_t rCrc = (uint8_t)buf[len - 2] | ((uint8_t)buf[len - 1] << 8);
    if (cCrc != rCrc) {
        error_flag = 1;
        LOG_ERR(_log_tag, "CRC mismatch: calc=0x%04X recv=0x%04X", cCrc, rCrc);
        return true;
    }

    rx.assign((uint8_t*)buf, (uint8_t*)buf + len);
    error_flag = 0;
    return false;
}

//=========== read: state (OutPut + RSSI in one frame) ===========

bool XKC_Y25_RS485::read_state(uint16_t& output, uint16_t& rssi) {
    // Read 2 registers starting at 0x0001 (OutPut, then RSSI).
    // Frame: [slave 03 reg_hi reg_lo cnt_hi cnt_lo crc_lo crc_hi]
    std::vector<uint8_t> tx = {
        (uint8_t)_slaveID, 0x03,
        (uint8_t)(XKC_REG::OUTPUT >> 8), (uint8_t)(XKC_REG::OUTPUT & 0xFF),
        0x00, 0x02,
        0, 0
    };
    uint16_t crc = modbusCRC(tx.data(), 6);
    tx[6] = crc & 0xFF;
    tx[7] = crc >> 8;

    // Expected RX = slave + fn + bc(=04) + 4 data bytes + 2 CRC = 9 bytes
    std::vector<uint8_t> rx;
    if (sendRecv(tx, rx, 9)) {
        // Comms error — return last cached values, signal failure.
        output = _last_output;
        rssi   = _last_rssi;
        LOG_WRN(_log_tag, "comm error, returning cached output=%u rssi=%u", _last_output, _last_rssi);
        return true;
    }

    // rx layout: [slave 03 04 out_hi out_lo rssi_hi rssi_lo crc_lo crc_hi]
    output = ((uint16_t)rx[3] << 8) | rx[4];
    rssi   = ((uint16_t)rx[5] << 8) | rx[6];

    _last_output = output;
    _last_rssi   = rssi;

    LOG_DBG(_log_tag, "output=%u rssi=%u", output, rssi);

    // Pacing — sensor response time is ~500 ms per spec; back-to-back polls
    // faster than this can return stale RSSI. Caller can poll faster than the
    // sensor's internal cadence but should expect repeated values.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return false;
}

bool XKC_Y25_RS485::has_liquid() {
    uint16_t out = 0, rssi = 0;
    if (read_state(out, rssi)) {
        // On error: return cached output as boolean (0 = false at boot).
        return _last_output != 0;
    }
    return out != 0;
}

//=========== config: address / baud rate ===========

bool XKC_Y25_RS485::set_address(uint8_t new_addr) {
    if (new_addr < 1 || new_addr > 254) {
        LOG_ERR(_log_tag, "set_address: invalid addr %u (must be 1..254)", new_addr);
        return true;
    }
    // Per manual §1.6: directed write (current slave ID) to reg 0x0004 = new_addr.
    // (Reg 0x0003 holds the CURRENT addr value but writing it is ignored — the
    // operation reg is +1 from the read reg. See XKC_REG comment.)
    // Sensor reply is non-standard (manual §1.7 shows a 7-byte frame instead of
    // the standard 8-byte 0x06 echo); we don't try to validate it. Verification
    // is via LED flash + re-reading at the new address. Mirrors set_baud_rate.
    std::vector<uint8_t> tx = {
        (uint8_t)_slaveID, 0x06,
        (uint8_t)(XKC_REG::SET_ADDR >> 8), (uint8_t)(XKC_REG::SET_ADDR & 0xFF),
        0x00, new_addr,
        0, 0
    };
    uint16_t crc = modbusCRC(tx.data(), 6);
    tx[6] = crc & 0xFF;
    tx[7] = crc >> 8;

    if (!client || !client->isConnected()) {
        error_flag = 1;
        return true;
    }
    LOG_HEX(_log_tag, "TX (set addr)", tx.data(), (int)tx.size());
    if (!client->sendData((const char*)tx.data(), (int)tx.size(), 500)) {
        error_flag = 1;
        LOG_ERR(_log_tag, "set_address send fail");
        return true;
    }
    // Best-effort drain of any reply (non-standard format per manual §1.7); not validated.
    char drain[16];
    client->receiveData(drain, sizeof(drain), 300);

    LOG_INF(_log_tag, "set_address: %u -> %u (verify via LED flash + re-read at new ID)", _slaveID, new_addr);
    error_flag = 0;
    return false;
}

bool XKC_Y25_RS485::set_baud_rate(uint8_t code) {
    // Per manual §1.8: directed write to reg 0x0005 (NOT 0x0004 — that's the
    // read-current-baud reg; broadcast writing 0x0004 with value 0x02 is the
    // factory reset trigger). Manual §1.9 valid codes: 0x05..0x0F (some marked
    // reserved; we don't enforce here). Sensor "no return" — LED flashes only.
    std::vector<uint8_t> tx = {
        (uint8_t)_slaveID, 0x06,
        (uint8_t)(XKC_REG::SET_BAUD >> 8), (uint8_t)(XKC_REG::SET_BAUD & 0xFF),
        0x00, code,
        0, 0
    };
    uint16_t crc = modbusCRC(tx.data(), 6);
    tx[6] = crc & 0xFF;
    tx[7] = crc >> 8;

    if (!client || !client->isConnected()) {
        error_flag = 1;
        return true;
    }
    LOG_HEX(_log_tag, "TX (set baud)", tx.data(), (int)tx.size());
    if (!client->sendData((const char*)tx.data(), (int)tx.size(), 500)) {
        error_flag = 1;
        LOG_ERR(_log_tag, "set_baud_rate send fail");
        return true;
    }

    LOG_INF(_log_tag, "set_baud_rate code=0x%02X (no reply expected; verify via LED + reconnect at new baud)", code);
    error_flag = 0;
    return false;
}

//=========== utility: CRC16 (Modbus RTU) ===========

uint16_t XKC_Y25_RS485::modbusCRC(const uint8_t* data, int len) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (int j = 8; j != 0; j--) {
            if ((crc & 0x0001) != 0) { crc >>= 1; crc ^= 0xA001; }
            else                       crc >>= 1;
        }
    }
    return crc;
}
