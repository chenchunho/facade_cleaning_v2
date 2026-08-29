#!/usr/bin/env bash
# 負控制：故意改壞一個常數，確認 compare.sh 會變紅。
#
# 🔴 這一步不能省，而且必須在信任任何綠燈**之前**跑。
#
#    2026-08-29 的實例：`Linux_test/fake_slaves/test_qx_do24.cpp` 的斷言寫反了
#    （`if (init(...)) FATAL`，而 QX_DO24::init() 是 true=成功），
#    **一個斷言都沒跑到卻回報通過** —— 那支測試從寫出來到當天從沒真正跑起來過。
#    沒有紅過的綠燈不算綠燈。
#
# 用法：  ./harness/negative_control.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
WORK="$REPO/tmp/harness-neg"

rm -rf "$WORK"; mkdir -p "$WORK/good/src" "$WORK/bad/src"
git -C "$REPO" archive HEAD | tar -x -C "$WORK/good/src"
git -C "$REPO" archive HEAD | tar -x -C "$WORK/bad/src"

# 改壞的方式刻意選「一個數字」，因為那正是重構最可能不小心弄錯的東西
# （搬動時抄錯一位、兩份常數分岔）。改 slave ID 會讓匯流排上的位元組直接不同。
BAD="$WORK/bad/src/app/WASH_ROBOT.h"
sed -i 's/static constexpr int ZDT_RF1 = 5, ZDT_RF2 = 7;/static constexpr int ZDT_RF1 = 5, ZDT_RF2 = 8;/' "$BAD"
grep -q 'ZDT_RF2 = 8;' "$BAD" || { echo "🔴 注入失敗 —— 常數的字面可能已經改過，負控制本身要先修"; exit 3; }
echo "[neg] 已注入缺陷：ZDT_RF2 7 → 8"

for name in good bad; do
  "$HERE/build.sh"     "$WORK/$name/src" "$WORK/$name/build" >/dev/null
  "$HERE/run_trace.sh" "$WORK/$name/build/facade_cleaning_v2.out" \
                       "$HERE/cmds/smoke.txt" "$WORK/$name/run" >/dev/null
  python3 "$HERE/normalize.py" "$WORK/$name/run/trace.raw" > "$WORK/$name/trace.norm"
done

echo
if diff -q "$WORK/good/trace.norm" "$WORK/bad/trace.norm" >/dev/null; then
  echo "🔴🔴 負控制失敗：注入了缺陷，diff 卻是綠的。"
  echo "     這套 harness 現在什麼都證明不了 —— 綠燈不可信，先修工具再繼續。"
  echo "     最可能的原因：指令腳本沒有碰到被改壞的那條路徑（覆蓋率不足）。"
  exit 1
fi
echo "✅ 負控制通過：注入缺陷後 diff 會紅，工具確實在量東西。"
diff "$WORK/good/trace.norm" "$WORK/bad/trace.norm" | head -20
