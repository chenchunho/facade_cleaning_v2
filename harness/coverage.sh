#!/usr/bin/env bash
# 用「回 OK 的比例」量覆蓋率，而不是「名字有沒有出現在腳本裡」。
#
# 🔴 兩者差很多：2026-08-29 第一次量到「55%」是後者 —— 名字對了但參數個數不足，
#    那些指令全部回 ERR，等於一行程式都沒跑到。**名字在腳本裡不代表路徑被走過。**
set -euo pipefail
R="${1:?用法: coverage.sh <replies.txt>}"
tot=$(grep -c . "$R" || true)
ok=$(grep -cP '\tOK' "$R" || true)
err=$(grep -cP '\tERR' "$R" || true)
to=$(grep -c '<<TIMEOUT>>' "$R" || true)
echo "指令 $tot：OK $ok / ERR $err / TIMEOUT $to"
[[ "$err" -gt 0 ]] && { echo "--- 回 ERR 的（可能是參數寫錯，不是程式問題）---"; grep -P '\tERR' "$R" | cut -c1-100; }
echo "→ 實際走到的比例 $(( ok * 100 / (tot>0?tot:1) ))%"
