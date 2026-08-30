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
start_bus() {  # $1=port $2=proto [$3=額外參數]
  python3 "$HERE/fake_bus.py" --port "$1" --proto "$2" ${3:-} 2>>"$OUT/fakes.log" &
  PIDS+=($!)
}
start_bus 15020 rtu '--dm2j 14'         # USR .20 — ZDT 5-8 / PQW 12 / DM2J 14
start_bus 15022 rtu '--jc100 5,6,7,8'   # USR .22 — JC100 5-8 / QX 9 / DY500 10,11 / XKC 13
start_bus 15030 rtu     # USR_A   — SE3 left / CLV900
start_bus 15031 rtu     # USR_B   — SE3 right
start_bus 15034 rtu     # USR_M   — SD76 1,2,4 / PQW 12
start_bus 15032 tcp     # X518    — DSZL left（原生 MBAP）
start_bus 15033 tcp     # X518    — DSZL right

# ── 假的吊機 / 手臂（行導向文字協定）──────────────────────────────────────
# 沒有它們，crane_cmd_ / arm_cmd_ 會對連不上的位址無界重試，而關閉序列裡就有
# 這些呼叫 → 程式被砍掉時跑到哪裡由時序決定，軌跡尾端就不確定。
for pn in "15002 crane" "15527 arm" "15530 depthcam"; do
  set -- $pn
  python3 "$HERE/fake_text_server.py" --port "$1" --name "$2" 2>>"$OUT/fakes.log" &
  PIDS+=($!)
done

# ── 假序列埠（IMU）──────────────────────────────────────────────────────────
# 主程式在 IMU 開埠失敗時直接 FATAL，所以沒有這個東西整支起不來。
PTYF="$OUT/imu_pty"
rm -f "$PTYF"
python3 "$HERE/fake_serial.py" --path-file "$PTYF" 2>>"$OUT/fakes.log" &
PIDS+=($!)
for _ in $(seq 1 100); do [[ -s "$PTYF" ]] && break; sleep 0.05; done
[[ -s "$PTYF" ]] || { echo "ERROR: 假序列埠沒起來" >&2; exit 1; }
export FCV_EP_IMU_HOST="$(cat "$PTYF")"

# 🔴 等埠真的開了才往下走。用 sleep 猜等待時間是這個 harness 最容易出現
#    「偶爾紅一次」的來源，而間歇性失敗會讓人開始不信任綠燈。
for p in 15020 15022 15030 15031 15032 15033 15034 15002 15527 15530; do
  for _ in $(seq 1 100); do
    (exec 3<>/dev/tcp/127.0.0.1/$p) 2>/dev/null && break
    sleep 0.05
  done
done

# ── 主程式 ─────────────────────────────────────────────────────────────────
# 🔴 LOG_HEX 有**兩道**開關：driver 的 debug_mode，以及 USER_LIB_HEX_LOG=1
#    （log_utils.h:69，預設關）。少了這個，軌跡裡一個 hex dump 都沒有，
#    normalize.py 解析出零筆，而**空對空的 diff 會回報「相同」** ——
#    2026-08-29 第一次做確定性驗證時就是這樣拿到假綠燈的。
#    normalize.py 對空輸入回 exit 2 就是為了擋這件事，呼叫端不要把 stderr 丟掉。
export USER_LIB_HEX_LOG=1
export WR_DRIVER_DEBUG=1

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
: >"$OUT/replies.txt"; : >"$OUT/events.txt"
exec 3<>/dev/tcp/127.0.0.1/5001
while IFS= read -r line; do
  [[ -z "$line" || "$line" == \#* ]] && continue
  printf '%s\n' "$line" >&3
  # 每條指令收一行回覆；逾時就記下來而不是靜靜跳過 ——
  # 「沒有回覆」跟「回覆相同」在 diff 上長得一樣，必須區分。
  # 🔴 這條連線是**多工**的：除了指令回覆，主程式還會主動推 EVT 事件
  #    （runbook §C3）。「一條指令一行回覆」的假設是錯的 —— 2026-08-29 第一次
  #    跑就把 `EVT weak_seal slave=5` 當成 zdt_pusher 的回覆收走，從那筆起
  #    每一條都錯位一格。**錯位後的 diff 看起來會像「到處都不一樣」。**
  #    做法：EVT 另外收進 events.txt，繼續讀到真正的回覆為止。
  # 逾時要夠長：運動類指令的內部逾時是 15s，設短了會把「還在跑」誤記成逾時。
  reply=""
  while IFS= read -r -t "${REPLY_TIMEOUT:-20}" reply <&3; do
    case "$reply" in
      EVT*) printf '%s\t%s\n' "$line" "$reply" >>"$OUT/events.txt" ;;
      *)    break ;;
    esac
    reply=""
  done
  if [[ -n "$reply" ]]; then
    printf '%s\t%s\n' "$line" "$reply" >>"$OUT/replies.txt"
  else
    printf '%s\t<<TIMEOUT>>\n' "$line" >>"$OUT/replies.txt"
    # 🔴 逾時之後必須排空，否則遲到的那筆回覆會被**下一條指令**收走 ——
    #    從那一刻起每一筆都錯位一格，而錯位後的 diff 看起來會像「到處都不一樣」。
    #    2026-08-29 第一次跑就踩到：4 筆逾時之後整份 replies 只剩 7 行。
    while IFS= read -r -t 1 _drain <&3; do :; done
  fi
done <"$CMDS"

# 🔴 收尾必須是確定性的：送 exit 讓程式自己走完關閉流程，等它真的結束。
#    直接 kill 的話，「砍掉的那一刻程式跑到哪」由時序決定 —— 兩次執行的軌跡
#    尾端就會不同，而那個差異看起來完全像行為差異。
printf 'exit\n' >&3 || true
exec 3<&- 3>&-
for _ in $(seq 1 "${EXIT_WAIT_TICKS:-400}"); do kill -0 "$APP" 2>/dev/null || break; sleep 0.1; done
if kill -0 "$APP" 2>/dev/null; then
  echo "[run_trace] ⚠️ 程式在等待時間內沒有自行結束，強制終止 —— **這一輪的軌跡尾端不可信**" >&2
  echo "TRUNCATED" > "$OUT/truncated"
fi

echo "[run_trace] trace.raw $(wc -l <"$OUT/trace.raw") 行 / replies $(wc -l <"$OUT/replies.txt") / events $(wc -l <"$OUT/events.txt")"
