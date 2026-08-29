#!/usr/bin/env bash
# 證明一次改動對編譯器而言不存在（純刪死碼、純搬動、只改註解）。
#
# 用法：  ./harness/prove_noop.sh <檔案> [基準 commit]
#         ./harness/prove_noop.sh app/WASH_ROBOT.cpp            # 比工作區 vs HEAD
#         ./harness/prove_noop.sh app/WASH_ROBOT.cpp HEAD~1
#
# 🔴 這比 harness/compare.sh 更強，而且更便宜：
#    compare.sh 比的是「跑到的路徑」—— 指令腳本沒碰到的地方，diff 永遠是綠的。
#    這支比的是**整個編譯單元的預處理輸出**，涵蓋每一行，不需要跑任何東西、
#    不需要 g++、不需要機器。
#
#    ⚠ 但它只能證明「編譯器看到的東西相同」。凡是真的改變了語意的搬動
#    （抽函式、換介面、改常數），這支一定會紅 —— 那時候才輪到 compare.sh。
#
# 用 cl /EP：預處理但**不輸出 `#line` 指令**。若用 /E，行號位移會讓每一行都不同。
# 空行要去掉：MSVC 在被吃掉的 `#if 0` 區塊原處輸出空行，行數會差，但那不是內容。
set -euo pipefail

FILE="${1:?用法: prove_noop.sh <檔案> [基準 commit]}"
BASE="${2:-HEAD}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
WIN_REPO='D:\Desktop\agent_ai\projects\facade_cleaning_v2'
WORK="$REPO/tmp/noop"
CMD='/mnt/c/Windows/System32/cmd.exe'

[[ -x "$CMD" ]] || { echo "ERROR: 需要 Windows 端的 MSVC（見 runbook §A3）" >&2; exit 127; }

rm -rf "$WORK"; mkdir -p "$WORK/base"
git -C "$REPO" archive "$BASE" | tar -x -C "$WORK/base"

preprocess() {  # $1=Windows 端的樹根  $2=輸出檔
  "$CMD" /c "${WIN_REPO}\\tmp\\preproc.bat ${FILE//\//\\} $1" 2>/dev/null \
    | tr -d '\r' | grep -v '^[[:space:]]*$' > "$2"
}

# preproc.bat 由 runbook §A3 的 flag 組成；這裡就地產生，免得兩份分岔。
mkdir -p "$REPO/tmp"
printf '%s\r\n' \
  '@echo off' \
  'call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1' \
  'cd /d %2' \
  'cl /nologo /utf-8 /std:c++17 /EHsc /EP /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /FIwinsock2.h /FIws2tcpip.h /I app /I common /I transport /I user_lib %1 2>nul' \
  > "$REPO/tmp/preproc.bat"

preprocess "${WIN_REPO}\\tmp\\noop\\base" "$WORK/base.txt"
preprocess "$WIN_REPO"                    "$WORK/cand.txt"

nb=$(wc -l < "$WORK/base.txt"); nc=$(wc -l < "$WORK/cand.txt")
echo "預處理非空行數: 基準 $nb / 現況 $nc"

# 🔴 零行要當錯誤。空對空的 cmp 會回「相同」，而那是假的通過 ——
#    本專案踩過同型的坑不只一次（明文掃描器讀零個檔卻回報未發現）。
if [[ "$nb" -eq 0 || "$nc" -eq 0 ]]; then
  echo "🔴 預處理輸出是空的 —— 編譯失敗還是路徑錯了？這個比對無效。" >&2
  exit 2
fi

if cmp -s "$WORK/base.txt" "$WORK/cand.txt"; then
  echo "✅ 預處理輸出逐位元相同 —— 這次改動對編譯器而言不存在"
else
  echo "🔴 有差異（前 40 行）："
  diff "$WORK/base.txt" "$WORK/cand.txt" | head -40
  exit 1
fi
