#!/bin/bash
# Build crane from fix/driver-crc (refactored layout) into ~/projects/facade_cleaning_v2（原始碼）／~/run（產物）
set -u
# [2026-09-09] 搬家：原始碼 ~/projects/facade_cleaning_v2，執行期產物 ~/run。
# 舊路徑 ~/bringup 已不存在。
cd ~/projects/facade_cleaning_v2 || exit 1
OUT="$HOME/run"
mkdir -p "$OUT"
g++ -std=c++17 -O2 -Icommon -Iconfig -Imechanism -Itransport -Iuser_lib \
  -o "$OUT/crane_drv.out" \
  Crane_control_PI/main.cpp \
  transport/TCP_client.cpp transport/TCP_server.cpp \
  user_lib/CLV900_inverter.cpp user_lib/DSZL_107.cpp \
  user_lib/ZS_DIO_R_RLY.cpp user_lib/MH300_inverter.cpp user_lib/SD76_length_meters.cpp \
  user_lib/SE3_inverter.cpp \
  -lpthread || { echo "BUILD FAILED"; exit 2; }
ls -la --time-style=long-iso "$OUT/crane_drv.out"; md5sum "$OUT/crane_drv.out"
