#!/usr/bin/env python3
"""fake_serial — 開一個 PTY 當假序列埠，讓 IMU 那條路徑開得起來。

    python3 fake_serial.py --path-file /tmp/imu_pty

主程式的 `WashRobot::init()` 在 IMU 開埠失敗時直接 `return true`（FATAL），
所以沒有這個東西，整支程式在 harness 裡連起來都起不來。

🔴 **刻意什麼資料都不送。**
   `WT901BC_TTL` 是背景執行緒連續讀。假裝置若按時間送資料，兩次執行讀到的
   筆數必然不同（排程與啟動時序的差異），而那個差異**看起來會跟重構搬壞
   一模一樣** —— 等價比對就毀了。不送資料 = IMU 對兩個版本都是「接著但沒訊號」
   的同一個狀態，完全確定性。

⚠️ 代價要講清楚：**IMU 相關的程式路徑因此沒有被涵蓋**（讀值、校平、
   `imu_persistently_bad_` 的視窗判定）。要測那些得另外設計一份「送固定筆數
   後停止」的腳本，並接受它只在該腳本內確定性。
"""
import argparse
import os
import pty
import signal
import sys
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--path-file', required=True,
                    help='把 PTY 從端路徑寫到這個檔，給 shell 讀')
    a = ap.parse_args()

    master, slave = pty.openpty()
    name = os.ttyname(slave)

    with open(a.path_file, 'w') as f:
        f.write(name)

    print(f'[fake_serial] pty = {name}', file=sys.stderr, flush=True)

    # master 必須持續開著，否則從端會收到 EOF/HUP，driver 那邊看起來像斷線。
    # 不寫入任何資料 —— 見檔頭的理由。
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        pass
    finally:
        os.close(master)
        os.close(slave)


if __name__ == '__main__':
    main()
