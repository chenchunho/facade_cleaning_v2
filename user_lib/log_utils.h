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
// Usage (inside a driver method):
//   LOG_ERR(_log_tag, "PPR read failed");
//   LOG_INF(_log_tag, "target %.3f cm -> %d pulses", pos_cm, pulses);
//   LOG_DBG(_log_tag, "status=0x%08X", st);
//   LOG_HEX(_log_tag, "TX", buf, len);
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

// hex dump helper (DBG-gated). `note` is a short string such as "TX" / "RX".
#define LOG_HEX(tag, note, data, len) do {                                  \
    if (debug_mode) {                                                       \
        std::fprintf(stderr, "[%s] [DBG] [%s] %s ",                         \
                     ::user_lib_log::now_ts().c_str(),                      \
                     std::string(tag).c_str(),                              \
                     (note));                                               \
        for (int _i = 0; _i < (int)(len); ++_i)                             \
            std::fprintf(stderr, "%02X ", (unsigned char)(data)[_i]);       \
        std::fputc('\n', stderr);                                           \
    }                                                                       \
} while (0)

#endif // USER_LIB_LOG_UTILS_H
