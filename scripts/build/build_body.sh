#!/bin/bash
# Build washrobot from fix/driver-crc (refactored layout) into ~/projects/facade_cleaning_v2（原始碼）／~/run（產物）
set -u
# [2026-09-09] 搬家：原始碼 ~/projects/facade_cleaning_v2，執行期產物 ~/run。
# 舊路徑 ~/bringup 已不存在。
cd ~/projects/facade_cleaning_v2 || exit 1
OUT="$HOME/run"
mkdir -p "$OUT"
rm -rf "$OUT/obj_drv" && mkdir -p "$OUT/obj_drv"
INC="-Iapp -Icommand -Icommon -Iconfig -Imechanism -Itransport -Iuser_lib"
# [2026-09-16] TU 清單只寫一次:期望的 .o 數從清單算,不再寫死(拿掉 DY_500 那次 15/16 就被寫死的 16 擋下)。
SRCS="facade_cleaning_v2/main.cpp app/WASH_ROBOT.cpp app/wash_robot_commands.cpp \
  command/dispatcher.cpp \
  transport/Serial_port.cpp transport/TCP_client.cpp transport/TCP_server.cpp \
  user_lib/DM2J_RS570.cpp user_lib/FrameAnalyzer.cpp \
  user_lib/JC_100_METER.cpp user_lib/ZS_DIO_R_RLY.cpp user_lib/QX_DO24.cpp \
  user_lib/WT901BC_TTL.cpp user_lib/XKC_Y25_RS485.cpp user_lib/ZDT_motor_control.cpp"
want=$(echo $SRCS | wc -w)
printf "%s\n" $SRCS \
| xargs -P4 -I{} sh -c "g++ -std=c++17 -O2 $INC -c \"\$1\" -o $OUT/obj_drv/\$(basename \"\$1\" .cpp).o 2>&1" _ {}
n=$(ls "$OUT"/obj_drv/*.o 2>/dev/null | wc -l)
echo "=== objs: $n / $want ==="
[ "$n" = "$want" ] || { echo "COMPILE FAILED"; exit 2; }
g++ -o "$OUT/facade_drv.out" "$OUT"/obj_drv/*.o -lpthread || { echo "LINK FAILED"; exit 3; }
ls -la --time-style=long-iso "$OUT/facade_drv.out"; md5sum "$OUT/facade_drv.out"
