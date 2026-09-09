#ifndef FCV_COMMON_ENDPOINTS_H
#define FCV_COMMON_ENDPOINTS_H

// Runtime endpoint override — the hook that lets the whole program be pointed
// at fake slaves on 127.0.0.1 without touching a single hardcoded address.
//
// [2026-08-29] Added for the refactor's equivalence harness (see
// .claude/refactor_plan.md §5). The refactor's success criterion is "same
// bytes on the bus as main-final", which requires running both builds against
// the same fake devices — and that in turn requires the addresses to be
// injectable. This header is the entire mechanism.
//
// 🔴 DESIGN RULE: with no environment variable set, behaviour must be
// bit-identical to the hardcoded constants. The equivalence test is worthless
// if the thing that enables it perturbs what it measures. Hence:
//   - the compiled-in constant stays exactly where it is (with its history
//     comments, which are load-bearing) and is passed in as the fallback;
//   - nothing is printed unless an override is actually active;
//   - a malformed port falls back to the default AND says so, rather than
//     silently connecting somewhere unintended.
//
// Usage (call site keeps the original constant as the documented default):
//   cli_20_.connectToServer(ep::host("USR20", IP_485_1), ep::port("USR20", PORT_485))
//
// Environment variables: FCV_EP_<NAME>_HOST / FCV_EP_<NAME>_PORT
//   FCV_EP_USR20_HOST=127.0.0.1 FCV_EP_USR20_PORT=15020 ./facade_cleaning_v2.out

#include <cstdio>
#include <cstdlib>
#include <string>

namespace ep {

namespace detail {

inline const char* lookup(const char* name, const char* suffix) {
	std::string var = std::string("FCV_EP_") + name + suffix;
	const char* v = std::getenv(var.c_str());
	return (v && *v) ? v : nullptr;
}

// One line per active override, on stderr, at resolution time. Deliberately
// silent when nothing is overridden — see the design rule above.
inline void announce(const char* name, const char* what, const std::string& value) {
	std::fprintf(stderr, "[endpoints] override %s %s = %s\n", name, what, value.c_str());
}

}  // namespace detail

// [2026-09-08] 「有沒有設覆蓋」必須問這個，**不要拿解析結果去跟編譯期常數比對**。
// 🔴 踩過的坑：resolve_crane_ip_() 原本寫 `if (ep::host(...) != CRANE_IP)` 當作
//    「有覆蓋」的判準 —— 當使用者把覆蓋值設成**剛好等於**那個常數時（例如要強制走
//    WiFi，而 CRANE_IP 本身就是 WiFi 位址），它與「完全沒設」無法區分，於是掉進
//    自動探測分支、選了有線。結果是「強制走 WiFi」變成唯一做不到的事，
//    而且 log 還照樣印出 `[endpoints] override ... = 192.168.5.25`，看起來像生效了。
inline bool has_host_override(const char* name) {
	return detail::lookup(name, "_HOST") != nullptr;
}

// Resolve a host. `fallback` is the compiled-in constant.
inline std::string host(const char* name, const char* fallback) {
	if (const char* v = detail::lookup(name, "_HOST")) {
		std::string s(v);
		detail::announce(name, "host", s);
		return s;
	}
	return std::string(fallback);
}

// Resolve a port. A malformed value is refused (not silently coerced to 0 by
// atoi, which would connect to a wrong place while looking like it worked —
// the exact failure shape this project keeps getting bitten by).
inline int port(const char* name, int fallback) {
	const char* v = detail::lookup(name, "_PORT");
	if (!v) return fallback;

	char* end = nullptr;
	long n = std::strtol(v, &end, 10);
	if (end == v || *end != '\0' || n <= 0 || n > 65535) {
		std::fprintf(stderr,
		             "[endpoints] %s port override \"%s\" is not a valid port — "
		             "using compiled-in %d\n", name, v, fallback);
		return fallback;
	}
	detail::announce(name, "port", std::to_string(n));
	return (int)n;
}

// 裝置路徑（序列埠）。與 host() 同一套環境變數規則，分開命名只是為了讓呼叫端
// 讀起來不像在講網路位址 —— IMU 走的是 /dev/ttyUSB0，不是 IP。
inline std::string path(const char* name, const char* fallback) {
	return host(name, fallback);
}

}  // namespace ep

#endif  // FCV_COMMON_ENDPOINTS_H
