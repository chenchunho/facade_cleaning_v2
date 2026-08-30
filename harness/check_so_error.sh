#!/usr/bin/env bash
# 針對性檢查：非阻塞 connect 有沒有把「沒人聽的埠」判成成功。
#
# 🔴 為什麼需要獨立一支：這條（預期差異 ⑥ SO_ERROR）的差異**只出現在 log**，
#    不在匯流排位元組、也不在指令回覆 —— compare.sh 的兩個判準都看不到它。
#    但「看不到」不等於「不重要」：修正前實測**吊機關著卻印了 20 次
#    `reconnect success`**，那是本專案「訊息說謊」那一族裡最貴的一種。
#
# 做法：把 CRANE 端點指到一個**沒有人監聽**的埠，跑一次，斷言 log 裡
#       `reconnect success` 出現 0 次。
#
# ⚠️ 這是**斷言而不是比對** —— 它不需要基準版本，直接驗「不該發生的事沒發生」。
#    負控制：把斷言改成 >0 應該要紅（否則這支什麼都沒驗）。
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${1:?用法: check_so_error.sh <binary>}"
OUT="${2:-$HERE/../tmp/so_error}"

rm -rf "$OUT"; mkdir -p "$OUT"
# 15099 刻意不起任何服務 —— 這正是要測的情境。
FCV_EP_CRANE_PORT=15099 REPLY_TIMEOUT=20 "$HERE/run_trace.sh" "$BIN" \
  "$HERE/cmds/readonly.txt" "$OUT" >/dev/null 2>&1

# ⚠ 用 cat 併檔再數 —— grep -c 對多個檔會逐檔輸出「檔名:數字」，
#   而 `|| echo 0` 在 grep 找不到（離開碼 1）時會**再多印一個 0**。
#   2026-08-30 兩個都踩到，變成 "0\n0" 然後算術炸掉。
n_ok=$(cat "$OUT/trace.raw" "$OUT/stdout.log" 2>/dev/null | grep -c "reconnect success" || true)
n_fail=$(cat "$OUT/trace.raw" "$OUT/stdout.log" 2>/dev/null | grep -c "reconnect failed" || true)
n_ok=${n_ok:-0}; n_fail=${n_fail:-0}

echo "沒人監聽的埠 15099：reconnect success $n_ok 次／reconnect failed $n_fail 次"
if [[ "$n_fail" -eq 0 ]]; then
  echo "🔴 連一次 reconnect failed 都沒有 —— **這輪根本沒去連**，斷言無效。"
  echo "   （零筆資料的通過不是通過。）"
  exit 2
fi
if [[ "$n_ok" -gt 0 ]]; then
  echo "🔴 沒人監聽卻印了 $n_ok 次「reconnect success」—— SO_ERROR 驗證沒生效"
  exit 1
fi
echo "✅ 沒人監聽時 0 次假成功（SO_ERROR 驗證生效），且確實嘗試連了 $n_fail 次"
