#!/usr/bin/env python3
"""fake_serial — 開一個 PTY 當假序列埠，讓 IMU 那條路徑開得起來。

    python3 fake_serial.py --path-file /tmp/imu_pty

主程式的 `WashRobot::init()` 在 IMU 開埠失敗時直接 `return true`（FATAL），
所以沒有這個東西，整支程式在 harness 裡連起來都起不來。

🔴 **送「固定筆數」的有效幀，然後停止。**

   2026-08-30 第一版是**什麼都不送** —— 那對確定性是對的，但 `init` 會
   `ERR imu_baseline_fail`（IMU 基準線取不到樣本），狀態機停在 Idle，
   於是 `attach` 一律 `ERR state_violation`，**整條步態路徑連碰都碰不到**。

   改成送固定筆數之後仍然是確定性的：**筆數由參數決定，不由時鐘決定**。
   兩個版本各自讀到同樣的 N 筆，`WT901` 的 LOG_HEX 也就出現同樣次數。
   ⚠️ 反過來說：**絕不可以「持續按時間送」** —— 那樣兩次執行讀到的筆數必然
      不同，而那個差異看起來會跟重構搬壞一模一樣。

⚠️ 代價：送完之後 IMU 就沒有新資料了。長時間執行時 `imu_persistently_bad_`
   那條視窗判定會觸發 —— 那是已知且刻意的，不是缺陷。
"""
import argparse
import os
import pty
import termios
import tty
import signal
import threading
import sys
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--path-file', required=True,
                    help='把 PTY 從端路徑寫到這個檔，給 shell 讀')
    ap.add_argument('--delay', type=float, default=3.0,
                    help='等幾秒才開始寫（讓主程式先開好埠）。'
                         '總筆數固定，所以延遲不影響確定性 —— 只影響「何時」寫。')
    ap.add_argument('--frames', type=int, default=400,
                    help='送幾筆 WT901 角度幀後停止（固定筆數＝確定性）')
    a = ap.parse_args()

    master, slave = pty.openpty()
    name = os.ttyname(slave)

    # 🔴 PTY 預設是 **canonical 模式**：資料會一直緩衝到出現換行才交給讀取端。
    #    WT901 的幀是二進位、沒有換行 —— 不設 raw 的話讀取端**一個位元組都收不到**，
    #    症狀是「[WT901] Thread started」之後一片安靜，而 init 回 imu_baseline_fail。
    #    （2026-08-30 實際踩到，查了半天才想到不是資料的問題是終端機模式的問題。）
    #    順便關掉 ECHO，免得寫進去的資料被回送、被自己讀回來。
    tty.setraw(master)
    tty.setraw(slave)

    with open(a.path_file, 'w') as f:
        f.write(name)

    print(f'[fake_serial] pty = {name} frames={a.frames}', file=sys.stderr, flush=True)

    # WT901 角度封包（0x55 0x53）：11 bytes，checksum = 前 10 bytes 之和 & 0xFF。
    # roll/pitch/yaw 全 0（水平）—— 值不重要，重要的是**每次都一樣**。
    body = bytes([0x55, 0x53] + [0x00] * 8)
    frame = body + bytes([sum(body) & 0xFF])

    # 🔴 延遲再寫：主程式的 Serial_port::init 在寫入之前就開好埠，
    #    否則資料可能在開埠前就被丟掉（實測：預先寫入的 400 筆一筆都沒被讀到）。
    #    延遲不影響確定性 —— **總筆數是固定的**，只是「何時」寫。
    #    PTY 緩衝有限（約 4KB），所以分批寫，每批之間讓讀取端有機會排空。
    def _feed():
        time.sleep(a.delay)
        CH = 64                       # 每批 64 筆 = 704 bytes，遠小於 PTY 緩衝
        left = a.frames
        while left > 0:
            n = min(CH, left)
            try:
                os.write(master, frame * n)
            except OSError:
                return
            left -= n
            time.sleep(0.05)
    threading.Thread(target=_feed, daemon=True).start()

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
