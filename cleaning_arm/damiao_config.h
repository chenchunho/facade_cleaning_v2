#pragma once
// ==========================================================
//  DamiaoConfig -- runtime config loader (INI format)
//
//  File format example (damiao.cfg):
//
//    # comment
//    port=/dev/ttyACM0
//    baud=921600
//    tcp_port=9527
//
//    [m1]
//    type=DM10010L
//    slave_id=0x01
//    master_id=0x11
//
//    [m2]
//    type=DM4340_48V
//    slave_id=0x02
//    master_id=0x22
//
//  Keys are case-insensitive; values trimmed of leading/trailing whitespace.
//  slave_id / master_id accept decimal or 0x hex.
//  Unknown keys print a warning and are skipped.
// ==========================================================

#include "damiao.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

#ifndef _WIN32
#  include <termios.h>
#endif

// ============================================================
//  DamiaoConfig
// ============================================================
struct DamiaoConfig {
    std::string port;               // "" → caller supplies platform default
    uint32_t    baud     = 921600u;
    int         tcp_port = 9527;

    struct MotorEntry {
        damiao::DM_Motor_Type type      = damiao::DM10010L;
        uint32_t              slave_id  = 0x01;
        uint32_t              master_id = 0x11;
    } m1, m2;
};

// ============================================================
//  motor_type_from_str()  -- string → DM_Motor_Type enum
// ============================================================
static inline bool motor_type_from_str(const std::string& s, damiao::DM_Motor_Type& out)
{
    static const struct { const char* name; damiao::DM_Motor_Type type; } kMap[] = {
        {"DM4310",     damiao::DM4310    },
        {"DM4310_48V", damiao::DM4310_48V},
        {"DM4340",     damiao::DM4340    },
        {"DM4340_48V", damiao::DM4340_48V},
        {"DM6006",     damiao::DM6006    },
        {"DM6248P",    damiao::DM6248P   },
        {"DM8006",     damiao::DM8006    },
        {"DM8009",     damiao::DM8009    },
        {"DM10010L",   damiao::DM10010L  },
        {"DM10010",    damiao::DM10010   },
        {"DMH3510",    damiao::DMH3510   },
        {"DMH6215",    damiao::DMH6215   },
        {"DMG6220",    damiao::DMG6220   },
        {"DMJH11",     damiao::DMJH11    },
    };
    std::string upper = s;
    std::transform(upper.begin(), upper.end(), upper.begin(),
        [](unsigned char c) { return static_cast<char>(::toupper(c)); });
    for (auto& e : kMap)
        if (upper == e.name) { out = e.type; return true; }
    return false;
}

// ============================================================
//  baud_to_speed_t()  -- Linux only: uint32_t → speed_t
// ============================================================
#ifndef _WIN32
static inline bool baud_to_speed_t(uint32_t baud, speed_t& out)
{
    static const struct { uint32_t baud; speed_t spd; } kMap[] = {
        {9600,   B9600  }, {19200,  B19200 }, {38400,  B38400 },
        {57600,  B57600 }, {115200, B115200}, {230400, B230400},
        {460800, B460800}, {921600, B921600},
    };
    for (auto& e : kMap)
        if (e.baud == baud) { out = e.spd; return true; }
    return false;
}
#endif

// ============================================================
//  load_config()
//  Returns true on success; err contains message on failure.
// ============================================================
static inline bool load_config(const std::string& path,
                                DamiaoConfig& cfg, std::string& err)
{
    std::ifstream f(path);
    if (!f.is_open()) { err = "cannot open: " + path; return false; }

    auto trim = [](const std::string& s) -> std::string {
        auto b = s.find_first_not_of(" \t\r\n");
        auto e = s.find_last_not_of(" \t\r\n");
        return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
    };
    auto to_lower = [](std::string s) -> std::string {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(::tolower(c)); });
        return s;
    };

    DamiaoConfig::MotorEntry* cur_motor = nullptr;
    int lineno = 0;
    std::string line;

    while (std::getline(f, line)) {
        ++lineno;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        // Section header [m1] / [m2]
        if (line.front() == '[') {
            auto close = line.find(']');
            if (close == std::string::npos) {
                err = "malformed section at line " + std::to_string(lineno);
                return false;
            }
            std::string sec = to_lower(trim(line.substr(1, close - 1)));
            if      (sec == "m1") cur_motor = &cfg.m1;
            else if (sec == "m2") cur_motor = &cfg.m2;
            else {
                err = "unknown section [" + sec + "] at line " + std::to_string(lineno);
                return false;
            }
            continue;
        }

        // key=value
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = to_lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        if (val.empty()) continue;

        auto parse_u32 = [&](uint32_t& out) {
            out = static_cast<uint32_t>(std::stoul(val, nullptr, 0));
        };

        if (cur_motor == nullptr) {
            // global keys
            if      (key == "port")     cfg.port     = val;
            else if (key == "baud")     parse_u32(cfg.baud);
            else if (key == "tcp_port") cfg.tcp_port = std::stoi(val);
            else std::cerr << "[config] warning: unknown key '" << key
                           << "' at line " << lineno << "\n";
        } else {
            // motor keys
            if (key == "type") {
                if (!motor_type_from_str(val, cur_motor->type)) {
                    err = "unknown motor type '" + val + "' at line " + std::to_string(lineno);
                    return false;
                }
            } else if (key == "slave_id")  parse_u32(cur_motor->slave_id);
            else if   (key == "master_id") parse_u32(cur_motor->master_id);
            else std::cerr << "[config] warning: unknown motor key '" << key
                           << "' at line " << lineno << "\n";
        }
    }
    return true;
}
