#!/bin/bash
set -u
cd ~/bringup || exit 1
rm -f obj/WASH_ROBOT.o obj/wash_robot_commands.o obj/dispatcher.o obj/main.o facade_cleaning_v2.new
printf "%s\n" facade_cleaning_v2/main.cpp app/WASH_ROBOT.cpp app/wash_robot_commands.cpp command/dispatcher.cpp \
  | xargs -P4 -I{} sh -c 'g++ -std=c++17 -O2 -Iapp -Icommand -Icommon -Imechanism -Itransport -Iuser_lib -c "$1" -o "obj/$(basename "$1" .cpp).o"' _ {}
for o in obj/WASH_ROBOT.o obj/wash_robot_commands.o obj/dispatcher.o obj/main.o; do
  [ -f "$o" ] || { echo "MISSING $o — 編譯失敗，不連結"; exit 1; }
done
g++ -o facade_cleaning_v2.new obj/*.o -lpthread || exit 1
ls -la facade_cleaning_v2.new
strings facade_cleaning_v2.new | grep -E "pumpB|relay_status" | head -3
