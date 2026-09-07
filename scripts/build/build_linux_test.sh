#!/bin/bash
# Linux_test —— bench 互動式硬體測試工具
#
# 🔴 [2026-09-07] 本檔的檔案清單**逐項取自被刪除的 `Linux_test/Linux_test.vcxproj`**
#    （`git show <刪除前的commit>:Linux_test/Linux_test.vcxproj` 可取回原檔）。
#    刪 vcxproj 之前先把它搬過來，否則這個專案會變成**沒有任何建置定義** ——
#    那正是「移除一份定義卻沒有替代品」，跟今天清掉的病同型。
#
# ✅ [2026-09-07] **已在本體 Pi 上實建通過**（產出 671,520 bytes 的 `linux_test.out`）。
#    include 比 vcxproj 原宣告的多帶了 `..\config` 與 `..\mechanism`
#    —— vcxproj 那兩個路徑本來就漏了，本體/吊機也踩過同一個坑。
set -u
cd "$(dirname "$0")/../../Linux_test" || exit 1
INC="-I../common -I../config -I../mechanism -I../transport -I../user_lib"
g++ -std=c++17 -O2 $INC -o linux_test.out \
  main.cpp \
  ../transport/Serial_port.cpp ../transport/TCP_client.cpp ../transport/TCP_server.cpp \
  ../user_lib/DM2J_RS570.cpp ../user_lib/DY_500_weight_sensor.cpp ../user_lib/JC_100_METER.cpp \
  ../user_lib/PQW_IO_16O_RLY.cpp ../user_lib/SD76_length_meters.cpp ../user_lib/WT901BC_TTL.cpp \
  ../user_lib/XKC_Y25_RS485.cpp ../user_lib/ZDT_motor_control.cpp ../user_lib/ZS_DIO_R_RLY.cpp \
  ../user_lib/SE3_inverter.cpp ../user_lib/MH300_inverter.cpp ../user_lib/QX_DO24.cpp \
  -lpthread || { echo "BUILD FAILED"; exit 2; }
ls -la --time-style=long-iso linux_test.out; md5sum linux_test.out
