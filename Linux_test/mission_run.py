#!/usr/bin/env python3
# ⚰️ 2026-09-10 已整併進 cycle_test.py。
#   純吊機來回 + 姿態統計 → `cycle_test.py crane <趟數>`
#   （crane 模式複用 monitored_crane_move:讀 raw_x、座標自動判定,
#    修掉了本檔原有的「座標慣例過期、讀 roll 非 raw_x」兩個 bug。）
#   原始碼在 git 歷史。
import sys
sys.exit("已整併 → 改用:  python3 cycle_test.py crane <趟數>   （列出模式: cycle_test.py modes）")
