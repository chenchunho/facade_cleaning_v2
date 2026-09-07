#!/bin/bash
# Build crane from fix/driver-crc (refactored layout) into ~/bringup
set -u
cd ~/bringup || exit 1
g++ -std=c++17 -O2 -Icommon -Iconfig -Imechanism -Itransport -Iuser_lib \
  -o crane_drv.out \
  Crane_control_PI/main.cpp \
  transport/TCP_client.cpp transport/TCP_server.cpp \
  user_lib/CLV900_inverter.cpp user_lib/DSZL_107.cpp user_lib/DY_500_weight_sensor.cpp \
  user_lib/PQW_IO_16O_RLY.cpp user_lib/MH300_inverter.cpp user_lib/SD76_length_meters.cpp \
  user_lib/SE3_inverter.cpp \
  -lpthread || { echo "BUILD FAILED"; exit 2; }
ls -la --time-style=long-iso crane_drv.out; md5sum crane_drv.out
