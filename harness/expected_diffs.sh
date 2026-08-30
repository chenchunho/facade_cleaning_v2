#!/usr/bin/env bash
# 逐條驗證：runbook §A2 塊三那 9 條預期差異，harness 抓不抓得到？
#
# 用法：  ./harness/expected_diffs.sh [cmds.txt]
#
# 🔴 做法是**逐條反轉**，不是拿中間基準比。
#    中間基準沒有 harness 的儀器（端點注入、log 原子性），要一個個 cherry-pick 進去；
#    而且它回答的是「那個 commit 當時長什麼樣」。
#    逐條反轉回答的是更該問的問題：**現有指令腳本能不能偵測到這條差異？**
#
#    抓到 → 這條在保護範圍內
#    抓不到 → **覆蓋缺口**，而且是可行動的：那條路徑沒被腳本走到，
#             日後重構若在那裡搬壞，diff 依然是綠的
#
# ⚠️ 這支不是在測程式對不對，是在**測這套驗證工具的偵測能力**。
#    它是 negative_control.sh 的系統化版本 —— 一條缺陷一個對照。
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
CMDS="${1:-$HERE/cmds/smoke.txt}"
WORK="$REPO/tmp/expdiff"

# 每一條：名稱 | 檔案 | 原字串 | 反轉成
# 只列「單一常數就能反轉」的那些；其餘需要改程式碼，見腳本末尾。
CASES=(
"①上滑台7.731換算|app/WASH_ROBOT.h|static constexpr double ARM_RAIL_LEAD_CM_PER_REV = 7.731;|static constexpr double ARM_RAIL_LEAD_CM_PER_REV = 1.0;"
"②ARM_SWEEP_RPM|app/WASH_ROBOT.h|static constexpr int ARM_SWEEP_RPM = 250;|static constexpr int ARM_SWEEP_RPM = 1000;"
"③吸盤左右歸屬|app/WASH_ROBOT.h|static constexpr int ZDT_RF1 = 5, ZDT_RF2 = 7;|static constexpr int ZDT_RF1 = 5, ZDT_RF2 = 6;"
"④推桿CUP_PULSE_PER_CM|app/WASH_ROBOT.h|static constexpr double CUP_PULSE_PER_CM = 3000.0;|static constexpr double CUP_PULSE_PER_CM = 2857.0;"
)

rm -rf "$WORK"; mkdir -p "$WORK"

# 基準：目前的 HEAD（工作區）
echo "=== 建基準（HEAD）"
mkdir -p "$WORK/ref/src"
git -C "$REPO" archive HEAD | tar -x -C "$WORK/ref/src"
"$HERE/build.sh"     "$WORK/ref/src" "$WORK/ref/build" >/dev/null 2>&1 || { echo "🔴 基準建置失敗"; exit 1; }
"$HERE/run_trace.sh" "$WORK/ref/build/facade_cleaning_v2.out" "$CMDS" "$WORK/ref/run" >/dev/null 2>&1
python3 "$HERE/normalize.py" "$WORK/ref/run/trace.raw" > "$WORK/ref.norm" 2>/dev/null
python3 "$HERE/normalize_replies.py" "$WORK/ref/run/replies.txt" > "$WORK/ref.rep" 2>/dev/null
REFN=$(grep -c '^[TR]X' "$WORK/ref.norm" || true)
echo "    基準軌跡 $REFN 筆"
if [[ "$REFN" -eq 0 ]]; then
  echo "🔴 基準軌跡是空的 —— 後面每一條的「抓到/抓不到」都不可信。先修這個。"
  exit 2
fi

PASS=0; GAP=0; IDX=0
for c in "${CASES[@]}"; do
  IFS='|' read -r name file old new <<<"$c"
  # ⚠ 不要用 tr -d 刪中文編號 —— tr 按**位元組**刪，會把多位元組字元切壞
  #    （2026-08-30 實測目錄名變成「上�台7.731換算」）。用序號當目錄名。
  d="$WORK/case$((IDX+1))"; IDX=$((IDX+1))
  mkdir -p "$d/src"
  git -C "$REPO" archive HEAD | tar -x -C "$d/src"
  if ! grep -qF "$old" "$d/src/$file"; then
    echo "🔴 $name：找不到要反轉的字面 —— **這條對照本身已經過期**，先修它"
    GAP=$((GAP+1)); continue
  fi
  python3 - "$d/src/$file" "$old" "$new" <<'PY'
import sys
p, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(p, encoding='utf-8').read()
open(p, 'w', encoding='utf-8').write(s.replace(old, new, 1))
PY
  "$HERE/build.sh"     "$d/src" "$d/build" >/dev/null 2>&1 || { echo "🔴 $name：建置失敗"; GAP=$((GAP+1)); continue; }
  "$HERE/run_trace.sh" "$d/build/facade_cleaning_v2.out" "$CMDS" "$d/run" >/dev/null 2>&1
  python3 "$HERE/normalize.py" "$d/run/trace.raw" > "$d.norm" 2>/dev/null
  n=$(grep -c '^[TR]X' "$d.norm" || true)

  if [[ "$n" -eq 0 ]]; then
    echo "🔴 $name：反轉版軌跡是空的 —— 這條測不了（不是覆蓋問題，是這輪跑壞了）"
    GAP=$((GAP+1)); continue
  fi
  python3 "$HERE/normalize_replies.py" "$d/run/replies.txt" > "$d.rep" 2>/dev/null
  if diff -q "$WORK/ref.norm" "$d.norm" >/dev/null && \
     diff -q "$WORK/ref.rep" "$d.rep" >/dev/null; then
    echo "⚠️  $name：**抓不到** —— 反轉了卻沒有任何差異（軌跡 $n 筆）"
    echo "        ＝ 覆蓋缺口。這條路徑沒被腳本走到，日後在那裡搬壞 diff 也是綠的。"
    GAP=$((GAP+1))
  else
    dn=$(diff "$WORK/ref.norm" "$d.norm" | grep -c '^[<>]' || true)
    dr=$(diff "$WORK/ref.rep" "$d.rep" | grep -c '^[<>]' || true)
    echo "✅ $name：抓到（位元組 $dn 行差異／回覆 $dr 行差異）"
    PASS=$((PASS+1))
  fi
done

echo
echo "===== 結果：抓到 $PASS ／ 覆蓋缺口 $GAP （共 ${#CASES[@]} 條）"
echo
echo "🔴 未納入本腳本的預期差異（需要改程式碼而非單一常數，或結構上測不到）："
echo "   ⑤ 9 支 driver 的回覆驗證 —— **結構上這裡永遠測不到**：它只在收到"
echo "      **壞幀**時才有行為，而 harness 的假從站永遠送好幀。"
echo "      那條要靠 Linux_test/fake_slaves/（專門送壞幀），兩套工具在此互補。"
echo "   ⑥ SO_ERROR（重連判定）—— 需要一個「有人監聽但不回應」的端點才走得到"
echo "   ⑦ zdt_pusher 範圍分岔 —— 已在 compare.sh vs main-final 驗到（唯一驗到的一條）"
echo "   ⑧ DM2J void→bool、⑨ abort_flag/張力警示 —— 需要程式碼層反轉"
