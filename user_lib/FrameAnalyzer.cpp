#include "FrameAnalyzer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <sstream>
#include <iostream>

// Note: target platform is Linux (Raspberry Pi), so popen is fine.
// Windows version not needed (washrobot_new_PI deploys to ARM64 Linux).

namespace {

// Run a shell command, capture stdout. Returns the entire stdout as string.
// Errors return empty string (caller checks).
// timeout_sec: hard cap on subprocess runtime.
std::string run_subprocess(const std::string& cmd, int timeout_sec) {
    // Prepend timeout(1) to enforce hard cap (POSIX coreutils)
    std::ostringstream wrapped;
    wrapped << "timeout " << timeout_sec << " " << cmd << " 2>&1";

    FILE* pipe = popen(wrapped.str().c_str(), "r");
    if (!pipe) return "";

    std::string out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        out += buf;
    }
    int rc = pclose(pipe);
    // rc == 0 success, 124 timeout, other = subprocess error. Caller may
    // still try to parse partial output.
    (void)rc;
    return out;
}

// Shell-escape a path (single-quote wrap, escape any existing single-quote).
std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

} // namespace

const char* FrameAnalyzer::action_name(Action a) {
    switch (a) {
        case Action::Proceed:     return "proceed";
        case Action::Short:       return "short";
        case Action::Over:        return "over";
        case Action::OverPartial: return "over_partial";
        case Action::Block:       return "block";
        case Action::Error:       return "error";
    }
    return "unknown";
}

FrameAnalyzer::Action FrameAnalyzer::parse_action(const std::string& s) {
    if (s == "proceed")      return Action::Proceed;
    if (s == "short")        return Action::Short;
    if (s == "over")         return Action::Over;
    if (s == "over_partial") return Action::OverPartial;
    if (s == "block")        return Action::Block;
    return Action::Error;
}

// Extract first occurrence of "key": <value> from json.
// Handles string ("..."), number (123, 33.3), and null. Returns string repr.
bool FrameAnalyzer::extract_json_value(const std::string& json,
                                        const std::string& key,
                                        std::string& out) {
    // Regex: "key"\s*:\s*("([^"\\]|\\.)*"|[-+]?\d+\.?\d*([eE][-+]?\d+)?|null|true|false)
    std::string pat = "\"" + key + "\"\\s*:\\s*(\"((?:[^\"\\\\]|\\\\.)*)\"|"
                                   "([-+]?\\d+\\.?\\d*(?:[eE][-+]?\\d+)?)|"
                                   "(null|true|false))";
    std::regex re(pat);
    std::smatch m;
    if (!std::regex_search(json, m, re)) return false;
    // m[2] = string content (no outer quotes), m[3] = number, m[4] = literal
    if (m[2].matched)      out = m[2].str();
    else if (m[3].matched) out = m[3].str();
    else if (m[4].matched) out = m[4].str();
    else                   out = m[1].str();   // fallback
    return true;
}

FrameAnalyzer::Result FrameAnalyzer::analyze(const std::string& cam3_before,
                                              const std::string& cam3_after,
                                              const std::string& cam4_before,
                                              const std::string& cam4_after) {
    Result r;
    // Honor env override of combine path
    if (const char* env = std::getenv("OBSTACLE_COMBINE_PY")) {
        combine_path_ = env;
    }

    std::ostringstream cmd;
    cmd << "python3 " << shell_quote(combine_path_)
        << " --cam3-before " << shell_quote(cam3_before)
        << " --cam3-after "  << shell_quote(cam3_after)
        << " --cam4-before " << shell_quote(cam4_before)
        << " --cam4-after "  << shell_quote(cam4_after);

    std::cout << "[FrameAnalyzer] running: " << cmd.str() << "\n";

    std::string out = run_subprocess(cmd.str(), /*timeout_sec=*/30);
    r.raw_json = out;

    if (out.empty()) {
        r.err_msg = "subprocess_no_output (combine_path=" + combine_path_ + ")";
        return r;
    }

    // Find "combined": {...} block and extract from it
    auto pos = out.find("\"combined\"");
    if (pos == std::string::npos) {
        r.err_msg = "no_combined_in_output";
        // [2026-06-04] dump raw subprocess output for debug — error msg often
        // tells you why combine.py couldn't produce combined section.
        std::cerr << "[FrameAnalyzer] subprocess stdout (no combined):\n"
                  << out.substr(0, 2000) << "\n[/FrameAnalyzer]\n";
        return r;
    }
    std::string combined_block = out.substr(pos);   // from "combined" to end

    std::string action_str, step_str, reason_str;
    if (!extract_json_value(combined_block, "action", action_str)) {
        r.err_msg = "no_action_in_combined";
        return r;
    }
    if (!extract_json_value(combined_block, "step_cm", step_str)) {
        r.err_msg = "no_step_cm_in_combined";
        return r;
    }
    extract_json_value(combined_block, "reason", reason_str);  // optional

    r.action = parse_action(action_str);
    if (r.action == Action::Error) {
        r.err_msg = "unknown_action: " + action_str;
        return r;
    }
    try {
        r.step_cm = std::stod(step_str);
    } catch (...) {
        r.err_msg = "step_cm_not_number: " + step_str;
        return r;
    }
    r.reason = reason_str;
    r.ok = true;
    return r;
}
