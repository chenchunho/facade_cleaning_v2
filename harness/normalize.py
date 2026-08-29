#!/usr/bin/env python3
"""把一份原始軌跡正規化成可比對的形式。

輸入：主程式的 stderr（`debug_mode` 打開時的 LOG_HEX 輸出）
輸出：分裝置的位元組序列

    python3 normalize.py raw.log > normalized.txt

🔴 為什麼**不能**直接 diff 整份軌跡
   主程式有多條背景執行緒（`crane_wd_thread_` / `crane_keepalive_thread_` /
   `imu_mon_thread_` / 吊機的 `meter_loop` / `hold_loop` / `vfd_keepalive_loop`），
   它們各自對 stderr 寫入。**全域行序是排程器決定的**，同一支程式跑兩次就會不同。
   直接 diff 會得到滿江紅，而且每一條紅都是假的 ——
   📌 這正是本專案「政策反轉時舊斷言會把正確報成故障」的同一種錯誤：
      **判準必須配合被測對象的性質，否則你會去查一個沒壞的東西。**

✅ 正確的判準：**分裝置比對**
   同一個 `[DEV:ID]` 上的交易來自同一個邏輯呼叫者，順序是確定的。
   跨裝置的交錯不比對 —— 那不是程式的行為，是排程的行為。

⚠ 這個取捨的代價要講清楚：如果重構改變了「先跟哪個裝置說話」的**順序**，
   分裝置比對抓不到。那一半靠 harness 的第二個判準（TCP 文字回覆，見 README）
   來守 —— 那個是完全確定性的，而且是程式的功能輸出。
"""
import argparse
import collections
import re
import sys

# [14:32:01.123] [DBG] [DM2J:3] TX 03 03 00 5F 00 02 35 86
# 🔴 用 search 不用 match：driver 的 log 行**會被應用層的輸出接在前面**。
#    2026-08-30 實例：
#      [water_inlet] off attempt 2/3 failed: [00:19:31.424] [DBG] [XKC:13] TX 0D 03 ...
#    LOG_HEX 本身已是原子的，但應用層的 std::cerr << a << b << c 不是 ——
#    兩者仍會在行首交錯。用 ^ 錨定會把這種行**靜默丟掉**，而丟掉的那一筆
#    在 diff 上看起來就是「這一側少了一次交易」＝假的行為差異。
LINE = re.compile(
    r'\[\d{2}:\d{2}:\d{2}\.\d{3}\]\s+'      # 時間戳 —— 丟掉
    r'\[(?P<lvl>[A-Z]+)\]\s+'
    r'\[(?P<dev>[^\]]+)\]\s+'
    r'(?P<rest>.*)$'
)

# 只有 hex dump 進比對：TX/RX 是程式與外界的實際契約。
# 其他 log 文字會因為註解、訊息措辭改動而變，那些不是行為。
# 尾端空白可有可無：LOG_HEX 每個 byte 印 "%02X "，所以真實輸出**有**尾空白，
# 但任何經過 strip 的中間處理都會把它吃掉。要求尾空白會讓工具在
# 「資料其實好好的」時候解析出零筆 —— 而零筆會讓 diff 變成空對空的假綠燈。
HEXDUMP = re.compile(r'^(?P<note>.*?)\s((?:[0-9A-F]{2}(?:\s|$))+)\s*$')


def normalize(fp, keep_notes: bool):
    per_dev = collections.OrderedDict()
    dropped = []
    for raw in fp:
        m = LINE.search(raw.rstrip('\n'))
        if not m:
            # 完全不含 driver log 樣式的行（應用層訊息）本來就不該進比對。
            # 但「看起來像 hex dump 卻沒被解析」必須被算出來 —— 靜默丟棄
            # 會讓「這一側少一次交易」看起來像行為差異。
            if re.search(r'\b(?:[0-9A-F]{2} ){3,}', raw):
                dropped.append(raw.rstrip('\n')[:100])
            continue
        dev, rest = m.group('dev'), m.group('rest')
        h = HEXDUMP.match(rest)
        if not h:
            continue
        note, hexbytes = h.group('note').strip(), h.group(2).strip()

        # 只留方向（TX / RX），丟掉 note 裡的自由文字 —— 那是給人看的訊息，
        # 改個措辭不代表行為變了。加 --keep-notes 可保留，用來追查差異來源。
        if keep_notes:
            key = note
        else:
            key = 'TX' if note.startswith('TX') else ('RX' if note.startswith('RX') else '??')

        per_dev.setdefault(dev, []).append(f'{key} {hexbytes}')
    if dropped:
        print(f'[normalize] 🔴 {len(dropped)} 行看起來像 hex dump 卻沒解析成功 —— '
              f'這些會在 diff 上偽裝成行為差異：', file=sys.stderr)
        for d in dropped[:5]:
            print(f'  {d}', file=sys.stderr)
    return per_dev


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('logfile', nargs='?', help='原始軌跡；省略則讀 stdin')
    ap.add_argument('--keep-notes', action='store_true',
                    help='保留 hex dump 的說明文字（追查差異來源時用）')
    a = ap.parse_args()

    fp = open(a.logfile, encoding='utf-8', errors='replace') if a.logfile else sys.stdin
    per_dev = normalize(fp, a.keep_notes)

    if not per_dev:
        # 🔴 空輸入要當成錯誤，不能安靜地輸出空檔然後被 diff 判成「兩邊相同」。
        #    本專案踩過同型的坑：明文掃描器路徑解析錯 → 讀零個檔卻回報
        #    「未發現明文秘密」。**零筆資料的「通過」不是通過。**
        print('[normalize] 沒有解析到任何 hex dump —— '
              'debug_mode 有打開嗎？軌跡是不是空的？', file=sys.stderr)
        sys.exit(2)

    for dev in sorted(per_dev):
        print(f'##### {dev}  ({len(per_dev[dev])} 筆)')
        for line in per_dev[dev]:
            print(line)


if __name__ == '__main__':
    main()
