#!/usr/bin/env bash
# 在本機（x86-64）建一份主程式，供等價性比對用。
#
# 📌 為什麼可以不用 ARM64：等價比對是「同一台機器上的兩個建置互比」。
#    只要兩邊用同一組 flag、同一個編譯器，架構差異不影響結論。
#    這條 harness **不是**在驗證能不能在 Pi 上跑 —— 那是 runbook §A2 的事。
#
# 用法：  ./harness/build.sh <原始碼樹> <輸出目錄>
#         ./harness/build.sh . out/head
#         ./harness/build.sh /tmp/main-final out/main-final
set -euo pipefail

SRC="${1:?用法: build.sh <原始碼樹> <輸出目錄>}"
OUT="${2:?用法: build.sh <原始碼樹> <輸出目錄>}"
JOBS="${JOBS:-4}"

# 優先用本機能裝到的最新 g++ —— Pi 是 14.2，越新越接近。
# 可用 CXX= 覆寫（例如刻意用舊版重現某個問題）。
CXX="${CXX:-}"
if [[ -z "$CXX" ]]; then
  for c in g++-12 g++-11 g++-10 g++; do command -v "$c" >/dev/null && { CXX="$c"; break; }; done
fi
[[ -n "$CXX" ]] || { echo "ERROR: 找不到 g++。sudo apt install -y g++-10" >&2; exit 127; }

SRC="$(cd "$SRC" && pwd)"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

# main-final 的樹是分層前的：WASH_ROBOT 在 user_lib/、沒有 app/ 也沒有 common/。
# 兩種佈局都要能建，否則沒辦法跟 main-final 比。
if [[ -d "$SRC/app" ]]; then
  WR="app/WASH_ROBOT.cpp";  INC="-I$SRC/app -I$SRC/common -I$SRC/transport -I$SRC/user_lib"
  TRANSPORT_DIR="transport"
else
  WR="user_lib/WASH_ROBOT.cpp"; INC="-I$SRC/user_lib"
  TRANSPORT_DIR="user_lib"
fi
[[ -d "$SRC/common" ]] || INC="${INC/-I$SRC\/common /}"

# -funsigned-char：目標機（aarch64）的 char 預設是**無號**，x86-64 是**有號**。
# 這份程式碼有 15 處用裸 char 陣列裝位元組，任何拿 char 做數值比較的地方在
# 兩個平台上會得到不同結果。加這個 flag 讓本機的 char 語意跟 Pi 一致。
#
# 🔴 它補不了另外兩個差異，見 README「本機建置證明不了什麼」：
#    編譯器 g++ 9.3/10.5（本機）vs 14.2（Pi）—— 差 4~5 個大版本
#    標準函式庫 libstdc++ 9/10 vs 14 —— 傳遞性 include 被大量移除
CXXFLAGS="-std=c++17 -O1 -g0 -pthread -funsigned-char $INC"

echo "[build] $SRC -> $OUT  ($($CXX --version | head -1))"

SRCS=(
  "$SRC/facade_cleaning_v2/main.cpp"
  "$SRC/$WR"
  "$SRC/$TRANSPORT_DIR/TCP_client.cpp"
  "$SRC/$TRANSPORT_DIR/TCP_server.cpp"
  "$SRC/$TRANSPORT_DIR/Serial_port.cpp"
)
for d in DM2J_RS570 DY_500_weight_sensor FrameAnalyzer JC_100_METER PQW_IO_16O_RLY \
         QX_DO24 WT901BC_TTL XKC_Y25_RS485 ZDT_motor_control; do
  SRCS+=("$SRC/user_lib/$d.cpp")
done

mkdir -p "$OUT/obj"
printf '%s\n' "${SRCS[@]}" | xargs -P"$JOBS" -I{} \
  sh -c "$CXX"' '"$CXXFLAGS"' -c "$1" -o "'"$OUT"'/obj/$(basename "$1" .cpp).o"' _ {}
"$CXX" -pthread -o "$OUT/facade_cleaning_v2.out" "$OUT"/obj/*.o

echo "[build] OK: $OUT/facade_cleaning_v2.out"
