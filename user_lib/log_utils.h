#ifndef USER_LIB_LOG_UTILS_H
#define USER_LIB_LOG_UTILS_H

// ============================================================================
// Unified log format for user_lib drivers
//
// Format:  [HH:MM:SS.mmm] [LEVEL] [DEVICE:ID] <message>
// Levels:  ERR / WRN / INF / DBG
//
// ALL levels are gated by a boolean named `debug_mode` visible at the call
// site. When debug_mode is false the driver is completely silent; callers
// already receive errors via the bool return convention (true = error).
// Turn debug_mode on to observe internal behaviour at any severity.
//
// LOG_HEX (TX/RX hex dumps) has an additional secondary gate:
//   - default: OFF (hex dumps suppressed even when debug_mode is on)
//   - enable via env var USER_LIB_HEX_LOG=1 (re-read lazily on first call)
//   - rationale: hex dumps flood stdout when many devices run concurrently
//                (25+ drivers × every Modbus frame), drowning the useful
//                decoded messages (status, completion, errors). Kept as
//                opt-in for low-level wire-level debugging.
//
// Usage (inside a driver method):
//   LOG_ERR(_log_tag, "PPR read failed");
//   LOG_INF(_log_tag, "target %.3f cm -> %d pulses", pos_cm, pulses);
//   LOG_DBG(_log_tag, "status=0x%08X", st);
//   LOG_HEX(_log_tag, "TX", buf, len);        // opt-in via env var
//
// Driver class must expose:
//   std::string _log_tag;   // e.g. "ZDT:3", "DM2J:1", "TCP"
//   bool        debug_mode; // master switch for ALL log output
//
// Output goes to stderr (not stdout), one line per call.
// No file output, no async queue, no thread-safety lock (line interleaving
// possible but acceptable for current use).
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <string>
#include <atomic>   // LOG_ERR 的每呼叫點限流計數器（driver 由多條背景執行緒呼叫）
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace user_lib_log {

// timestamp "HH:MM:SS.mmm"
inline std::string now_ts() {
    using namespace std::chrono;
    auto t  = system_clock::now();
    auto ms = duration_cast<milliseconds>(t.time_since_epoch()) % 1000;
    std::time_t tt = system_clock::to_time_t(t);
    std::tm bt;
#ifdef _WIN32
    localtime_s(&bt, &tt);
#else
    localtime_r(&tt, &bt);
#endif
    std::ostringstream oss;
    oss << std::put_time(&bt, "%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// Secondary gate for LOG_HEX — default OFF, opt-in via env USER_LIB_HEX_LOG=1.
// Read once lazily; subsequent calls return cached value (env changes at runtime
// not observed — fine for our startup-only config model).
inline bool hex_log_enabled() {
    static bool v = []{
        const char* e = std::getenv("USER_LIB_HEX_LOG");
        return (e && e[0] == '1');
    }();
    return v;
}


// Monotonic ms clock for LOG_ERR's rate limiter. steady_clock so a wall-clock
// adjustment (NTP step) can't make the window misbehave.
inline int64_t now_ms_mono() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace user_lib_log

#define ULOG_IMPL(level, tag, ...) do {                                     \
    std::fprintf(stderr, "[%s] [%s] [%s] ",                                 \
                 ::user_lib_log::now_ts().c_str(),                          \
                 (level),                                                   \
                 std::string(tag).c_str());                                 \
    std::fprintf(stderr, __VA_ARGS__);                                      \
    std::fputc('\n', stderr);                                               \
} while (0)

/* [2026-08-31] LOG_ERR 不再受 debug_mode 管 —— 改為「無條件 + 每呼叫點限流」。
 *
 * 為什麼改：driver 的回覆驗證（CRC / byteCount / 幀長 / slave 不符 / FC 不符）
 * 五條拒絕路徑全部走 LOG_ERR，而 LOG_ERR 被 debug_mode 蓋住、預設關閉
 * → **驗證的整個用意是「把問題變可見」，診斷訊息本身卻藏在預設關閉的旗標後面。**
 * 2026-08-31 吊機兩側計米器同時 length=ERR，log 裡一個字都沒有，查不出是五條裡的哪一條。
 *
 * 限流而不是全開：計米器 250ms 輪詢，持續失敗就是每秒 4 行；137 個呼叫點全開會洗版。
 * 規則沿用本專案既有慣例（meter_read_robust 的 reject_count<3 + 60s 重置）：
 * 每個呼叫點在 LOG_ERR_WINDOW_MS 內印前 LOG_ERR_BURST 次，第 BURST+1 次印一行抑制通知，
 * 之後靜音到窗口結束。debug_mode 為 true 時不限流（要看全部就開 driver debug）。
 *
 * static 放在 do-block 內：每個巨集展開點是各自獨立的區塊作用域，所以計數器是
 * **per 呼叫點**而不是全域共用。atomic 是因為 driver 由多條背景執行緒呼叫。
 */
#define LOG_ERR_WINDOW_MS 60000
#define LOG_ERR_BURST     3
#define LOG_ERR(tag, ...)  do {                                             \
    static std::atomic<int64_t>  _le_t0{0};                                 \
    static std::atomic<unsigned> _le_n{0};                                  \
    const int64_t _le_now = ::user_lib_log::now_ms_mono();                  \
    if (_le_now - _le_t0.load(std::memory_order_relaxed) > LOG_ERR_WINDOW_MS) { \
        _le_t0.store(_le_now, std::memory_order_relaxed);                   \
        _le_n.store(0, std::memory_order_relaxed);                          \
    }                                                                       \
    const unsigned _le_k = _le_n.fetch_add(1, std::memory_order_relaxed) + 1; \
    if (debug_mode || _le_k <= LOG_ERR_BURST) {                             \
        ULOG_IMPL("ERR", tag, __VA_ARGS__);                                 \
    } else if (_le_k == LOG_ERR_BURST + 1) {                                \
        ULOG_IMPL("ERR", tag, "%s", "(同一處錯誤重複發生，本輪已抑制；開 driver debug 看全部)"); \
    }                                                                       \
} while (0)
#define LOG_WRN(tag, ...)  do { if (debug_mode) ULOG_IMPL("WRN", tag, __VA_ARGS__); } while (0)
#define LOG_INF(tag, ...)  do { if (debug_mode) ULOG_IMPL("INF", tag, __VA_ARGS__); } while (0)
#define LOG_DBG(tag, ...)  do { if (debug_mode) ULOG_IMPL("DBG", tag, __VA_ARGS__); } while (0)

// hex dump helper. Gated by BOTH `debug_mode` (driver-level master switch) AND
// ::user_lib_log::hex_log_enabled() (env USER_LIB_HEX_LOG=1, default OFF).
// `note` is a short string such as "TX" / "RX".
// 🔴 [2026-08-29] 改為「先組完整行、再一次寫出」。
//    原本是每個位元組一次 fprintf（一行 hex ＝ 幾十次呼叫），而 driver 的
//    log 來自多條背景執行緒 —— 兩條訊息會**互相插進對方中間**，產生像
//        RX 05 03 00 16
//        TX 01 00 0C 05 01 D4 00 0D 00 00 5D 4E
//    這種既不是 TX 也不是 RX 的殘骸。這不只影響工具：**真機上的 log 一直
//    是這樣被打亂的**，而追 bug 時看到半行 hex 會直接把人帶往錯誤方向。
//    一次 fwrite 不保證原子，但把「幾十次呼叫」壓成一次，交錯窗口小到
//    實務上消失。輸出內容與原本逐字相同。
//
// ⚠️ 兩個巨集衛生陷阱，都是這次現踩的：
//    ① **不能用 `_line.data()`** —— 巨集參數就叫 `data`，展開後會變成
//       `_line.buf()` 之類（呼叫端傳什麼變數名就變成什麼）。改用 `c_str()`。
//    ② **巨集內不能放 `//` 註解** —— 它會連同行尾的 `\` 一起被吃掉，巨集就
//       在那一行斷掉，錯誤訊息會指向下面幾十行外的地方。說明只能寫在巨集外。
#define LOG_HEX(tag, note, data, len) do {                                  \
    if (debug_mode && ::user_lib_log::hex_log_enabled()) {                  \
        std::string _l = "[" + ::user_lib_log::now_ts() + "] [DBG] ["       \
                       + std::string(tag) + "] " + (note) + " ";            \
        char _b[4];                                                         \
        for (int _i = 0; _i < (int)(len); ++_i) {                           \
            std::snprintf(_b, sizeof(_b), "%02X", (unsigned char)(data)[_i]); \
            _l += _b; _l += ' ';                                            \
        }                                                                   \
        _l += '\n';                                                         \
        std::fwrite(_l.c_str(), 1, _l.size(), stderr);                      \
    }                                                                       \
} while (0)

#endif // USER_LIB_LOG_UTILS_H
