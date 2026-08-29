#!/usr/bin/env bash
# 跑一次主程式、對著假匯流排、灌一份指令腳本，收下軌跡。
#
# 用法：  ./harness/run_trace.sh <binary> <cmds.txt> <輸出目錄>
#
# 產出兩份東西，對應 harness 的兩個判準（見 README）：
#   trace.raw     driver 的 LOG_HEX（stderr）→ normalize.py 之後分裝置比對
#   replies.txt   TCP 文字回覆              → 完全確定性，直接比對
set -euo pipefail

BIN="${1:?用法: run_trace.sh <binary> <cmds.txt> <輸出目錄>}"
CMDS="${2:?用法: run_trace.sh <binary> <cmds.txt> <輸出目錄>}"
OUT="${3:?用法: run_trace.sh <binary> <cmds.txt> <輸出目錄>}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mkdir -p "$OUT"
PIDS=()
cleanup() {
  for p in "${PIDS[@]:-}"; do kill "$p" 2>/dev/null || true; done
  wait 2>/dev/null || true
}
trap cleanup EXIT

# ── 假匯流排 ────────────────────────────────────────────────────────────────
# 每條實體 bus 一台，port 用 15000+ 避開真實的 4001/502。
start_bus() {  # $1=port $2=proto
  python3 "$HERE/fake_bus.py" --port "$1" --proto "$2" 2>>"$OUT/fakes.log" &
  PIDS+=($!)
}
start_bus 15020 rtu     # USR .20 — ZDT 5-8 / PQW 12 / DM2J 14
start_bus 15022 rtu     # USR .22 — JC100 5-8 / QX 9 / DY500 10,11 / XKC 13
start_bus 15030 rtu     # USR_A   — SE3 left / CLV900
start_bus 15031 rtu     # USR_B   — SE3 right
start_bus 15034 rtu     # USR_M   — SD76 1,2,4 / PQW 12
start_bus 15032 tcp     # X518    — DSZL left（原生 MBAP）
start_bus 15033 tcp     # X518    — DSZL right

# 🔴 等埠真的開了才往下走。用 sleep 猜等待時間是這個 harness 最容易出現
#    「偶爾紅一次」的來源，而間歇性失敗會讓人開始不信任綠燈。
for p in 15020 15022 15030 15031 15032 15033 15034; do
  for _ in $(seq 1 100); do
    (exec 3<>/dev/tcp/127.0.0.1/$p) 2>/dev/null && break
    sleep 0.05
  done
done

# ── 主程式 ─────────────────────────────────────────────────────────────────
export FCV_EP_USR20_HOST=127.0.0.1  FCV_EP_USR20_PORT=15020
export FCV_EP_USR22_HOST=127.0.0.1  FCV_EP_USR22_PORT=15022
export FCV_EP_CRANE_HOST=127.0.0.1  FCV_EP_CRANE_PORT=15002
export FCV_EP_ARM_HOST=127.0.0.1    FCV_EP_ARM_PORT=15527
export FCV_EP_DEPTHCAM_HOST=127.0.0.1 FCV_EP_DEPTHCAM_PORT=15530

"$BIN" >"$OUT/stdout.log" 2>"$OUT/trace.raw" &
APP=$!
PIDS+=($APP)

# 等主程式的指令埠（:5001）起來
for _ in $(seq 1 200); do
  (exec 3<>/dev/tcp/127.0.0.1/5001) 2>/dev/null && break
  sleep 0.05
done

# ── 灌指令 ─────────────────────────────────────────────────────────────────
: >"$OUT/replies.txt"
exec 3<>/dev/tcp/127.0.0.1/5001
while IFS= read -r line; do
  [[ -z "$line" || "$line" == \#* ]] && continue
  printf '%s\n' "$line" >&3
  # 每條指令收一行回覆；逾時就記下來而不是靜靜跳過 ——
  # 「沒有回覆」跟「回覆相同」在 diff 上長得一樣，必須區分。
  if IFS= read -r -t 5 reply <&3; then
    printf '%s\t%s\n' "$line" "$reply" >>"$OUT/replies.txt"
  else
    printf '%s\t<<TIMEOUT>>\n' "$line" >>"$OUT/replies.txt"
  fi
done <"$CMDS"
exec 3<&- 3>&-

echo "[run_trace] $OUT/trace.raw ($(wc -l <"$OUT/trace.raw") 行) / $OUT/replies.txt"
