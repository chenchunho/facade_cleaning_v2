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

} // namespace user_lib_log

#define ULOG_IMPL(level, tag, ...) do {                                     \
    std::fprintf(stderr, "[%s] [%s] [%s] ",                                 \
                 ::user_lib_log::now_ts().c_str(),                          \
                 (level),                                                   \
                 std::string(tag).c_str());                                 \
    std::fprintf(stderr, __VA_ARGS__);                                      \
    std::fputc('\n', stderr);                                               \
} while (0)

#define LOG_ERR(tag, ...)  do { if (debug_mode) ULOG_IMPL("ERR", tag, __VA_ARGS__); } while (0)
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
