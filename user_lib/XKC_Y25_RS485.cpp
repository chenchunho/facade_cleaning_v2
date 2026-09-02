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

    // 🔴 [2026-09-01] 原子交易，取代原本的 sendData + receiveData 裸對。
    //
    // 為什麼非改不可：本感測器（slave 13）與 JC-100 5~8、QX-DO24 9、DY-500 10/11
    // 共用 `.22` 這條匯流排。裸對之間 TCP_client 的 socket_mtx 是放開的 → 別條
    // 執行緒可以把自己的交易插進來，兩邊的回覆在核心緩衝區裡錯位；而且一旦有
    // 一筆回覆遲到落在下一次的 recv 窗口內，就會**永久落後一筆**，只有重連救得回來
    //（2026-09-01 於 `.20` 實測過同一個機制，見 TCP_client::drainRx 的說明）。
    //
    // 附帶效益：納入 TCP_client 的「連續 10 次接收逾時 → 主動斷線」守衛
    //（只掛在原子交易 API 上）。該守衛的計數每條連線共用但成功一次就歸零，所以
    // 同一條 bus 上還有別的裝置在正常交易時，單一裝置故障不會把整條 bus 扯斷。
    // ⚠️ 這一點對本驅動特別相關：水箱水位感測器**不探測、缺件時首次讀取才會發現**
    //（見 app/WASH_ROBOT.cpp 的 init），也就是它本來就可能長期讀不到。
    //
    // 逾時沿用原值（send 500 / recv 1000），不趁機改時序。
    char buf[64];
    const int len = client->sendAndReceive((const char*)tx.data(), (int)tx.size(),
                                           buf, sizeof(buf), 500, 1000);
    if (len <= 0) {
        error_flag = 1;
        LOG_ERR(_log_tag, "send/recv fail");
        return true;
    }
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

    // 🔴 [2026-09-01] 回覆的 slave id 檢查 —— 由當日新增的 fake-slave 測試
    // （`test_stage2 xkc wrongslave`）當場抓出來的**既有缺陷**：本驅動原本只驗
    // 長度與 CRC，而**寫給別的 slave 的回覆帶著完全合法的 CRC**，所以會被當成
    // 自己的收下。這正是 2026-08-28 的 driver 稽核在 DM2J 修掉的同一類問題，
    // 當時漏了這一支。
    // 為什麼在這台特別要緊：本感測器（slave 13）與 JC-100 5~8、QX-DO24 9、
    // DY-500 10/11 共用 `.22`，收到鄰居的回覆是這條匯流排上真實會發生的事。
    // sendRecv() 只有 read_state() 一個呼叫端，沒有廣播路徑，所以這裡驗得起。
    if ((uint8_t)buf[0] != (uint8_t)_slaveID) {
        error_flag = 1;
        LOG_ERR(_log_tag, "reply slave %d != %d — frame dropped",
                (int)(uint8_t)buf[0], (int)_slaveID);
        return true;
    }

    // 同理，唯一的呼叫端發的是 FC 0x03；沒有這道檢查時，例外回覆（0x83）會被
    // 當成暫存器資料解析，「讀取被拒絕」就變成一個看起來正常的數值。
    if ((uint8_t)buf[1] != 0x03) {
        error_flag = 1;
        LOG_ERR(_log_tag, "reply FC 0x%02X (expected 0x03) — frame dropped", (int)(uint8_t)buf[1]);
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
    // [2026-09-01] 同樣走原子交易（見 sendRecv 的說明）：回覆雖然不驗證，
    // 但送出與接收之間若被別的裝置插入，殘留的位元組一樣會害下一筆交易失步。
    // 逾時沿用原值（send 500 / recv 300）。
    // Best-effort drain of any reply (non-standard format per manual §1.7); not validated.
    char drain[16];
    // `== 0` 才算失敗：sendAndReceive 以 0 表示送出失敗、-1 表示無回覆／斷線，
    // 而這一站本來就允許「沒有回覆」（原本用的是不看回傳值的 receiveData）。
    if (client->sendAndReceive((const char*)tx.data(), (int)tx.size(),
                               drain, sizeof(drain), 500, 300) == 0) {
        error_flag = 1;
        LOG_ERR(_log_tag, "set_address send fail");
        return true;
    }

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
    // ⚠️ [2026-09-01] 這一站**刻意保留裸 sendData**，不改用 sendAndReceive：
    // 感測器對這個指令不回覆（手冊 §1.8，只閃 LED），改成原子交易只會白等一個
    // recv 逾時，而且會把「沒有回覆」誤記成接收逾時、去推動 TCP_client 的斷線守衛。
    // 純送出不配對接收，本來就不會造成失步。
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
