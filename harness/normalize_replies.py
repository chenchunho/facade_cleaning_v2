#!/usr/bin/env python3
"""把 replies.txt 裡「時間驅動的診斷計數器」正規化掉。

    python3 normalize_replies.py replies.txt > replies.norm

🔴 為什麼需要這一步
   `status` 會印 `n_angle=` / `n_accel=` —— 那是 IMU 背景執行緒**到目前為止讀到
   幾個封包**。假 IMU 按時間餵資料（必須如此：`imu_take_baseline_` 要在 3 秒的
   視窗內取到樣本，一次爆發送完會落在視窗外，實測 `init` 直接 `imu_baseline_fail`），
   所以這個計數**必然**每次不同 —— 2026-08-30 實測 320 vs 256。

   它跟時間戳是同一類東西：**量的是「跑了多久」，不是「做了什麼」**。
   留著它，每一次比對都會紅，而且紅的地方跟真正的行為差異長得一模一樣。

⚠️ 但排除任何東西都要付代價，代價要寫明：
   **IMU 讀取量本身不再被比對** —— 如果重構讓 IMU 執行緒少讀了一半封包，
   這裡看不出來。那條要靠別的方式守（例如把 `n_angle` 的**數量級**納入判準，
   而不是精確值）。目前接受這個代價，因為替代方案是「每次都紅」＝驗證失效。

📌 排除清單刻意做得很短。每加一個欄位就少一分保護 ——
   加之前先問：**這個欄位量的是「做了什麼」還是「跑了多久」？**
"""
import argparse
import re
import sys

# 時間驅動的診斷計數器 —— 量的是「跑了多久」
TIME_DRIVEN = re.compile(r'\b(n_angle|n_accel)=\d+')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('replies', nargs='?', help='replies.txt；省略則讀 stdin')
    a = ap.parse_args()
    fp = open(a.replies, encoding='utf-8', errors='replace') if a.replies else sys.stdin

    n_lines = 0
    n_masked = 0
    for line in fp:
        out, k = TIME_DRIVEN.subn(lambda m: m.group(0).split('=')[0] + '=<time-driven>', line)
        n_lines += 1
        n_masked += k
        sys.stdout.write(out)

    if n_lines == 0:
        # 🔴 零行要當錯誤 —— 空對空的 diff 會回報「相同」，那是假的通過。
        print('[normalize_replies] 輸入是空的 —— 這個比對無效', file=sys.stderr)
        sys.exit(2)
    print(f'[normalize_replies] {n_lines} 行，遮蔽 {n_masked} 個時間驅動計數器',
          file=sys.stderr)


if __name__ == '__main__':
    main()
