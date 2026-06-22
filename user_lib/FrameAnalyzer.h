#pragma once
// FrameAnalyzer — thin C++ wrapper around Python obstacle_combine.py.
//
// Design (2026-06-04, per Sadie): keep detector as Python subprocess rather
// than porting OpenCV to C++. step_down is ~30s scale, 200-500ms subprocess
// overhead is acceptable. Python crash doesn't kill washrobot. Easier to
// iterate on detector algorithm.
//
// Usage:
//     FrameAnalyzer fa;
//     auto r = fa.analyze("/tmp/cam3_before.jpg", "/tmp/cam3_after.jpg",
//                         "/tmp/cam4_before.jpg", "/tmp/cam4_after.jpg");
//     if (r.ok && r.action == FrameAnalyzer::Action::Over) {
//         step_cm_.store(r.step_cm);
//     }

#include <string>

class FrameAnalyzer {
public:
    enum class Action {
        Proceed,        // 沒障礙物，照預設 step 走
        Short,          // 障礙物近、停在前面
        Over,           // 跨過去
        OverPartial,    // 跨不完全、走 MAX、會壓 obstacle 後段
        Block,          // 無解、不要動
        Error,          // detector 出錯
    };

    struct Result {
        bool         ok = false;          // false → detector subprocess 失敗 / parse 失敗
        Action       action = Action::Error;
        double       step_cm = 0.0;       // 建議走多少 cm
        std::string  reason;              // human-readable
        std::string  raw_json;            // 完整 JSON output（debug 用）
        std::string  err_msg;             // !ok 時的錯誤訊息
    };

    // Run obstacle_combine.py on four frame paths and parse the combined
    // decision. Returns Result with ok=true on success.
    Result analyze(const std::string& cam3_before,
                   const std::string& cam3_after,
                   const std::string& cam4_before,
                   const std::string& cam4_after);

    // Stringify Action for log / EVT
    static const char* action_name(Action a);

    // Configurable path to obstacle_combine.py — defaults to Pi production path.
    // Override via setenv("OBSTACLE_COMBINE_PY", "/path/to/combine.py") before
    // first analyze() call, or set via set_combine_path().
    void set_combine_path(const std::string& p) { combine_path_ = p; }
    const std::string& combine_path() const { return combine_path_; }

private:
    // [2026-06-04] User's actual setup has python files in /home/nexuni/projects/
    // (not the source tree path). Override via env OBSTACLE_COMBINE_PY if needed.
    std::string combine_path_ = "/home/nexuni/projects/obstacle_combine.py";

    static Action parse_action(const std::string& s);
    // Extract a value from JSON string by key (regex-based, no external lib).
    // Returns true if found, false otherwise. Strips outer quotes if string value.
    static bool extract_json_value(const std::string& json, const std::string& key,
                                   std::string& out);
};
