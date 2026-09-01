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

// [2026-09-01] 重新同步的最大丟棄筆數。落後一筆是實測到的情況，
// 3 給足餘裕又不會在真的斷線時空轉太久（每次最多 400ms）。
static constexpr int DSZL_RESYNC_MAX = 3;

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
    _log_tag   = "DSZL:" + std::to_string(ID)
               + (_log_side.empty() ? "" : "@" + _log_side);   // [2026-08-31] 側別
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
    // [2026-08-29] Null-client guard: the constructor leaves `client` as nullptr
    // and only init() sets it, so a call on an un-init'd (or failed-init)
    // instance dereferences nullptr and takes the whole process down.
    // Application layers already gate these calls, but that is the caller
    // remembering to be careful — the driver must not be a landmine.
    if (!client) return true;
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

    // 🔴🔴 [2026-09-01] sendData + receiveData → **sendAndReceive**。
    //
    // 分開兩步呼叫**沒有送出前的排空（drain）**，也不是原子交易。後果是：
    // 只要有一筆回覆遲到（或多來一筆），緩衝區就永遠落後一筆 —— 之後每次
    // receiveData 讀到的都是**上一筆交易的回覆** → txid/slave 對不上 → 全數被拒
    // → **永久失步，只能重連才能恢復**。
    //
    // 實證（2026-09-01 一天內四次）：
    //   [ERR] [DSZL:1] stale/foreign reply txid=3510 want=3511   ← 正好差一筆
    //   ss 顯示 socket 仍 ESTAB 但 Recv-Q 卡著 13 bytes
    //   （＝一筆完整的 MBAP FC03 回覆：7 頭 + fc + bc + 4 資料）
    //
    // 對照：SE3_inverter::sendModbus 走的是 sendAndReceive，所以它的故障是
    // **間歇且會自癒**的；DSZL 走這條沒有 drain 的路徑，故障是**永久**的。
    // ⚠️ 這個差異也一度誤導了診斷 —— 查了 sendAndReceive 有 drain 就以為
    //    DSZL 也有，實際上兩支 driver 走不同路徑。
    //
    // TCP_client::sendAndReceive 在同一把 socket_mtx 內完成 drain → send → recv，
    // 排空的是「先前放棄/失敗的交易留下的位元組」，正是本缺陷需要的。
    LOG_HEX(_log_tag, "TX read", req, (int)sizeof(req));

    // 🔴🔴 [2026-09-01 第二次修正] **txid 不符時要重新同步，不能只是放棄。**
    //
    // 第一次修正（改用 sendAndReceive 取得送出前排空）**不夠**，實測仍卡死：
    //   [ERR] [DSZL:1] stale/foreign reply txid=1861 want=1862   ← 仍正好差一筆
    //
    // 為什麼排空救不了：一旦錯開一筆，上一筆的回覆**恰好在新交易的 recv 窗口內**
    // 抵達（不是躺在緩衝區裡等著被排空）——drain 發生在送出前，那時它還沒到。
    // 於是每一筆都讀到上一筆的答案，**自我延續，永遠追不回來**。
    //
    // 正確做法：**丟掉不符的回覆、繼續讀（不重送）**。因為我們只落後一筆，
    // 下一次讀取就會拿到正確的回覆 —— 一筆交易內即完成重新同步。
    // ⚠️ 刻意**不重送請求**：重送只會讓佇列裡再多一筆答案，把落後變成落後兩筆。
    char buf[256];
    int n = 0;
    bool synced = false;
    for (int attempt = 0; attempt <= DSZL_RESYNC_MAX; ++attempt) {
        n = (attempt == 0)
            ? client->sendAndReceive((const char*)req, (int)sizeof(req),
                                     buf, sizeof(buf), 100, 400)
            : client->receiveData(buf, sizeof(buf), 400);   // 只讀，不重送
        if (n < 9) return true;        // need at least MBAP(7) + fc + bc
        const uint16_t rx = ((uint16_t)(uint8_t)buf[0] << 8) | (uint8_t)buf[1];
        if (rx == txid_) { synced = true; break; }
        LOG_ERR(_log_tag, "丟棄遲到回覆 txid=%u want=%u（重新同步 %d/%d）",
                rx, txid_, attempt + 1, DSZL_RESYNC_MAX);
    }
    if (!synced) return true;

    LOG_HEX(_log_tag, "RX read", buf, n);

    // [2026-08-28] Reply validation — same class of defect fixed in
    // SD76_length_meters::readRegister the same day, found by the driver audit.
    //
    // Before this, the only checks were `n >= 9` and the fc byte. `bc` came
    // straight off the wire and drove the memcpy below; `n < 9 + bc` bounded it
    // against the RECEIVE buffer (256) but never against the CALLER's, which is
    // uint8_t buf[64] at both call sites. A reply claiming bc=247 therefore
    // wrote 250 bytes into 64 — stack corruption on the tension-sensor path
    // that hold_loop's safety monitor depends on.
    //
    // Modbus TCP carries no CRC (TCP checksums the frame), so the RTU fix does
    // not transfer directly; the equivalents here are the MBAP transaction id
    // and unit id, plus a byte-count that must match what we asked for.

    // Transaction id echo. The 400ms receive timeout means a late reply to an
    // earlier, abandoned transaction can still be sitting in the socket when
    // the next request goes out — without this it would be read as the answer
    // to the current one.
    // [2026-09-01] txid 已由上方的重新同步迴圈保證相符，此處不再需要檢查。
    // Protocol id must be 0 for Modbus.
    if (buf[2] != 0 || buf[3] != 0) return true;
    // Unit id must be the slave we addressed.
    if ((uint8_t)buf[6] != slaveID) {
        LOG_ERR(_log_tag, "reply unit %d != slave %d", (int)(uint8_t)buf[6], (int)slaveID);
        return true;
    }

    uint8_t fc = (uint8_t)buf[7];
    if (fc == (0x03 | 0x80)) {
        LOG_ERR(_log_tag, "Modbus exception code=0x%02X at addr=0x%04X",
                (uint8_t)buf[8], addr);
        return true;
    }
    if (fc != 0x03) return true;

    // Byte count must match the quantity requested. This is what bounds the
    // memcpy: callers ask for 2 or 4 registers, so payload is 7 or 11 bytes.
    const int bc = (uint8_t)buf[8];
    if (bc != quantity * 2) {
        LOG_ERR(_log_tag, "byteCount %d != requested %d", bc, quantity * 2);
        return true;
    }
    if (n < 9 + bc) {
        LOG_ERR(_log_tag, "frame truncated: %d < %d", n, 9 + bc);
        return true;
    }

    // Strip MBAP header except unit byte: caller sees [unit][fc][bc][data...]
    const int payload = 3 + bc;
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
    if (!client) return true;
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

    // [2026-09-01] 同 modbus_read：改用 sendAndReceive（原子 + 送出前排空）。
    // 讀寫共用同一條連線，**任一邊失步都會污染另一邊** —— 只修讀不修寫，
    // 一次失敗的寫入照樣能讓後續所有讀取永久錯開。
    LOG_HEX(_log_tag, "TX write_long", req, (int)sizeof(req));

    char buf[32];
    int n = client->sendAndReceive((const char*)req, (int)sizeof(req),
                                   buf, sizeof(buf), 100, 300);
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
