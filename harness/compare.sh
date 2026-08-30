#!/usr/bin/env bash
# 兩個 commit 的等價性比對：各建一份、各跑一次、正規化後 diff。
#
# 用法：  ./harness/compare.sh <基準 commit> <受測 commit> [cmds.txt]
#         ./harness/compare.sh main-final HEAD
#
# 🔴 綠燈的意思：**兩個版本對外的位元組與文字回覆相同**。
#    它不代表功能正確，只代表「沒有被搬壞」。功能正確要靠實機（runbook §A2）。
set -euo pipefail

BASE="${1:?用法: compare.sh <基準 commit> <受測 commit> [cmds.txt]}"
CAND="${2:?用法: compare.sh <基準 commit> <受測 commit> [cmds.txt]}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
CMDS="${3:-$HERE/cmds/smoke.txt}"
# TARGET=crane 改比吊機那支二進位（階段 3 用）。
BINNAME="${TARGET:-wr}"
[[ "$BINNAME" == crane ]] && BINNAME=crane_control_PI.out || BINNAME=facade_cleaning_v2.out
WORK="${WORK:-$REPO/tmp/harness}"

rm -rf "$WORK"; mkdir -p "$WORK"

for side in base:"$BASE" cand:"$CAND"; do
  name="${side%%:*}"; rev="${side#*:}"
  echo "=== $name = $rev"
  mkdir -p "$WORK/$name/src"
  git -C "$REPO" archive "$rev" | tar -x -C "$WORK/$name/src"
  "$HERE/build.sh"     "$WORK/$name/src" "$WORK/$name/build"
  "$HERE/run_trace.sh" "$WORK/$name/build/$BINNAME" "$CMDS" "$WORK/$name/run"
  python3 "$HERE/normalize.py"         "$WORK/$name/run/trace.raw"   > "$WORK/$name/trace.norm"
  python3 "$HERE/normalize_replies.py" "$WORK/$name/run/replies.txt" > "$WORK/$name/replies.norm"
done

echo
echo "########## 判準 1：TCP 文字回覆（完全確定性）"
if diff -u "$WORK/base/replies.norm" "$WORK/cand/replies.norm"; then
  echo "✅ 回覆完全相同"
else
  echo "🔴 回覆有差異 —— 見上方"
  RC=1
fi

echo
echo "########## 判準 2：分裝置的位元組序列"
if diff -u "$WORK/base/trace.norm" "$WORK/cand/trace.norm"; then
  echo "✅ 每個裝置的位元組序列完全相同"
else
  echo "🔴 位元組序列有差異 —— 見上方"
  RC=1
fi

# 🔴 綠燈必須附上「實際走到了什麼」。
#    2026-08-30 踩過：寫了一份 rail.txt 想驗上滑台的換算差異，兩個判準都 ✅，
#    但 DM2J:14 **一筆交易都沒有** —— arm_sweep 兩側都回 ERR aborted，
#    上滑台根本沒被驅動。那是零覆蓋造成的假綠燈，而它跟真的等價長得一模一樣。
echo
echo "########## 實際走到的裝置（🔴 綠燈只在這張表涵蓋的範圍內有意義）"
paste <(grep '^#####' "$WORK/base/trace.norm" | sed 's/##### //') \
      <(grep '^#####' "$WORK/cand/trace.norm" | sed 's/##### //') \
  | awk -F'\t' '{printf "  base %-24s cand %-24s\n", $1, $2}'
echo "  指令：$(grep -c . "$WORK/base/run/replies.txt") 條，"\
     "其中回 OK $(grep -cP '\tOK' "$WORK/base/run/replies.txt" || true) 條"\
     "／ERR $(grep -cP '\tERR' "$WORK/base/run/replies.txt" || true) 條"
echo "  ⚠️ 回 ERR 的指令等於沒走到那條路徑 —— 它的 diff 永遠是綠的。"

echo
if [[ "${RC:-0}" == 0 ]]; then
  echo "✅ 等價（就這份指令腳本的覆蓋範圍而言）"
  echo "   ⚠ 覆蓋率是這套驗證最弱的一環：腳本沒碰到的路徑，diff 永遠是綠的。"
else
  echo "🔴 不等價。對照 runbook §A2 塊三那 9 條預期差異："
  echo "   差異 ⊆ 那 9 條 → 預期；出現清單外的 → 搬壞了。"
fi
exit "${RC:-0}"
