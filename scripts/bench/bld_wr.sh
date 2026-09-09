#!/bin/bash
# Build washrobot from fix/driver-crc (refactored layout) into ~/bringup
set -u
cd ~/bringup || exit 1
rm -rf obj_drv && mkdir -p obj_drv
INC="-Iapp -Icommand -Icommon -Iconfig -Imechanism -Itransport -Iuser_lib"
printf "%s\n" \
  facade_cleaning_v2/main.cpp app/WASH_ROBOT.cpp app/wash_robot_commands.cpp \
  command/dispatcher.cpp \
  transport/Serial_port.cpp transport/TCP_client.cpp transport/TCP_server.cpp \
  user_lib/DM2J_RS570.cpp user_lib/DY_500_weight_sensor.cpp user_lib/FrameAnalyzer.cpp \
  user_lib/JC_100_METER.cpp user_lib/PQW_IO_16O_RLY.cpp user_lib/QX_DO24.cpp \
  user_lib/WT901BC_TTL.cpp user_lib/XKC_Y25_RS485.cpp user_lib/ZDT_motor_control.cpp \
| xargs -P4 -I{} sh -c "g++ -std=c++17 -O2 $INC -c \"\$1\" -o obj_drv/\$(basename \"\$1\" .cpp).o 2>&1" _ {}
n=$(ls obj_drv/*.o 2>/dev/null | wc -l)
echo "=== objs: $n / 16 ==="
[ "$n" = "16" ] || { echo "COMPILE FAILED"; exit 2; }
g++ -o facade_drv.out obj_drv/*.o -lpthread || { echo "LINK FAILED"; exit 3; }
ls -la --time-style=long-iso facade_drv.out; md5sum facade_drv.out
