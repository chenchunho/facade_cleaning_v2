#!/usr/bin/env bash
# cleaning_arm / motor_api 的**唯一**建置路徑。
# 🔴 [2026-09-07] 抬頭原寫「Fallback／VS MSBuild 走 cleaning_arm.vcxproj 是主路徑」——
#    vcxproj 當日已整組刪除（VS 不再使用），這支從 fallback 變成主路徑。
# 📌 刻意**不搬進 `scripts/build/`**：那會變成第二份副本，而它本來就在這裡且是對的。
#    `scripts/build/README.md` 指到這裡。
#   -I../user_lib  ← damiao.h / SerialPort.h 已搬到 user_lib (2026-05-20h)
#   -std=c++17     ← std::make_unique 需要 C++14+,選 17 保險
#   -pthread       ← damiao TCP server 用 std::thread
g++ -std=c++17 -I../user_lib *.cpp -o motor_api -pthread
