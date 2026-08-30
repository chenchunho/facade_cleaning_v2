#ifndef FCV_COMMON_PROFILE_H
#define FCV_COMMON_PROFILE_H

// 設定檔載入 —— 極簡的 `key = value`，不使用任何外部相依
// （CLAUDE.md：no external dependencies beyond POSIX/WinSock and the C++ stdlib）。
//
// [2026-08-30] 重構階段 4（見 .claude/refactor_plan.md §7.1）。
//
// 🔴 設計規則（與 common/endpoints.h 同一條）：
//    **沒有設定檔時，行為必須與編譯進去的常數逐位元相同。**
//    設定檔不存在、讀不到、鍵不存在 → 一律回退到 fallback，且**一個字都不印**。
//    只有真的覆寫了才印一行。用來鬆綁常數的機制，自己不能擾動被量的對象。
//
// 🔴 分兩份 profile，因為它們的**變更理由不同**：
//      axis_profile    機構標定 —— 導程、行程上下限、方向。跟著**機器**走。
//      device_profile  設備協定 —— slave、通道、端點、位元組序。跟著**設備型號**走。
//    合成一份的話，換一顆同型馬達就會把機構標定一起重置 ——
//    而那正是 `ARM_RAIL_LEAD_CM_PER_REV = 7.731` 那個缺陷的形狀
//    （程式假設 1.0，每個 cm 指令實際走 7.7 倍，四個月沒有人被告知）。
//
// 🔴 **安全互鎖不externalize**（12 個，清單見 refactor_plan.md §7.1.1）：
//    `CH6`/`CH14` 同號會讓機器在貼牆狀態下脫落；`IMU_EMERGENCY_DEG`、
//    `ROPE_WEIGHT_LIMIT_*`、`DISABLE_PHASE_CURRENT_LIMIT_MA` 同理。
//    **一個可以被人改錯的數字不是保護。** 它們留在程式碼裡當斷言。
//
// 格式（刻意極簡）：
//    # 註解
//    ARM_RAIL_LEAD_CM_PER_REV = 7.731
//    ARM_RAIL_LEAD_CM_PER_REV.provenance = 實測 2026-08-28；皮帶軸；最小平方…
//
// 🔴 `.provenance` 不是裝飾：標定值沒有來源就沒有人敢改它，
//    也沒有人知道它什麼時候該重量。覆寫生效時會連同 provenance 一起印出來 ——
//    **看不到來源的覆寫，本身就是一個警訊。**

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace profile {

namespace detail {

inline std::map<std::string, std::string>& table(const char* which) {
    static std::map<std::string, std::map<std::string, std::string>> all;
    auto it = all.find(which);
    if (it != all.end()) return it->second;

    std::map<std::string, std::string> kv;
    // 路徑可用環境變數覆寫，讓 harness 能指向測試用的設定檔而不動樹裡那份。
    std::string var = std::string("FCV_PROFILE_") + which;
    const char* p = std::getenv(var.c_str());
    std::string path = p && *p ? p : (std::string("config/") + which + ".txt");

    std::ifstream f(path);
    if (f) {
        std::string line;
        while (std::getline(f, line)) {
            const size_t h = line.find('#');
            if (h != std::string::npos) line.erase(h);
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            auto trim = [](std::string& s) {
                const char* ws = " \t\r\n";
                const size_t a = s.find_first_not_of(ws);
                const size_t b = s.find_last_not_of(ws);
                s = (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
            };
            trim(k); trim(v);
            if (!k.empty() && !v.empty()) kv[k] = v;
        }
    }
    // 檔案不存在**不是錯誤** —— 那是預設狀態，全部走編譯進去的常數。
    all[which] = kv;
    return all[which];
}

inline void announce(const char* which, const std::string& key, const std::string& val) {
    auto& kv = table(which);
    auto  pv = kv.find(key + ".provenance");
    std::fprintf(stderr, "[profile] %s: %s = %s%s%s\n", which, key.c_str(), val.c_str(),
                 pv == kv.end() ? "  ⚠ 無 provenance" : "  (",
                 pv == kv.end() ? "" : (pv->second + ")").c_str());
}

}  // namespace detail

inline double num(const char* which, const char* key, double fallback) {
    auto& kv = detail::table(which);
    auto  it = kv.find(key);
    if (it == kv.end()) return fallback;
    try {
        const double v = std::stod(it->second);
        detail::announce(which, key, it->second);
        return v;
    } catch (...) {
        // 🔴 格式錯誤要拒絕並明說，不要靜默變成 0 —— 那會讓機器用一個
        //    看起來像成功的錯誤值動起來。
        std::fprintf(stderr, "[profile] %s: %s = \"%s\" 不是數字 — 用編譯進去的 %g\n",
                     which, key, it->second.c_str(), fallback);
        return fallback;
    }
}

}  // namespace profile

#endif  // FCV_COMMON_PROFILE_H
