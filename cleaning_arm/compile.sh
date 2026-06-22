#!/usr/bin/env bash
# Fallback / Linux 直編路徑（VS MSBuild 走 cleaning_arm.vcxproj 是主路徑）。
#   -I../user_lib  ← damiao.h / SerialPort.h 已搬到 user_lib (2026-05-20h)
#   -std=c++17     ← std::make_unique 需要 C++14+,選 17 保險
#   -pthread       ← damiao TCP server 用 std::thread
g++ -std=c++17 -I../user_lib *.cpp -o motor_api -pthread
