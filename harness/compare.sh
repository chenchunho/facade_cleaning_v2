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
WORK="${WORK:-$REPO/tmp/harness}"

rm -rf "$WORK"; mkdir -p "$WORK"

for side in base:"$BASE" cand:"$CAND"; do
  name="${side%%:*}"; rev="${side#*:}"
  echo "=== $name = $rev"
  mkdir -p "$WORK/$name/src"
  git -C "$REPO" archive "$rev" | tar -x -C "$WORK/$name/src"
  "$HERE/build.sh"     "$WORK/$name/src" "$WORK/$name/build"
  "$HERE/run_trace.sh" "$WORK/$name/build/facade_cleaning_v2.out" "$CMDS" "$WORK/$name/run"
  python3 "$HERE/normalize.py" "$WORK/$name/run/trace.raw" > "$WORK/$name/trace.norm"
done

echo
echo "########## 判準 1：TCP 文字回覆（完全確定性）"
if diff -u "$WORK/base/run/replies.txt" "$WORK/cand/run/replies.txt"; then
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

echo
if [[ "${RC:-0}" == 0 ]]; then
  echo "✅ 等價（就這份指令腳本的覆蓋範圍而言）"
  echo "   ⚠ 覆蓋率是這套驗證最弱的一環：腳本沒碰到的路徑，diff 永遠是綠的。"
else
  echo "🔴 不等價。對照 runbook §A2 塊三那 9 條預期差異："
  echo "   差異 ⊆ 那 9 條 → 預期；出現清單外的 → 搬壞了。"
fi
exit "${RC:-0}"
