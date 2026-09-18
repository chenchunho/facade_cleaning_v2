#!/usr/bin/env python3
"""SE3 變頻器參數 傾印 / 複製(Modbus RTU over USR-TCP232 透傳網關;位址 = P 編號,2026-09-17 實查)。

  python3 vfd_se3_clone.py dump  <ip> <out.txt>            # 讀 P.0..P.999 全部存檔(格式同 config/vfd/se3_*.txt)
  python3 vfd_se3_clone.py write <ip> <src.txt> [--apply]  # 把檔案裡的值寫進另一台;預設 dry-run 只列差異

寫入規則:
  · 只寫「目標機目前值 ≠ 檔案值」的參數(冪等,可重跑)
  · 一律跳過 P.36(站號)、P.996~P.999(命令暫存器)—— 站號要自己決定,命令不是設定
  · 寫入回 Modbus 例外(唯讀/監視暫存器)只記錄不中止;寫完立刻回讀驗證
  · 🔴 目標機必須停機、且要在同一台網關/鮑率下(115200 8N2,slave 1);寫完建議斷電重開再 dump 一次比對
"""
import socket, struct, sys, time, re

SLAVE = 1
SKIP = {36, 996, 997, 998, 999}

def crc16(b):
    c = 0xFFFF
    for x in b:
        c ^= x
        for _ in range(8): c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c

class SE3:
    def __init__(self, ip, port=4001):
        self.s = socket.create_connection((ip, port), 5); self.s.settimeout(0.5)
    def _txn(self, tx, tries=2):
        for _ in range(tries):
            try:
                self.s.sendall(tx + struct.pack("<H", crc16(tx))); time.sleep(0.06); rx = self.s.recv(512)
            except socket.timeout: rx = b""
            if rx and rx[0] == SLAVE:
                if rx[1] & 0x80: return "EXC%02X" % rx[2]
                return rx
            time.sleep(0.08)
        return None
    def read(self, reg, n=1):
        rx = self._txn(bytes([SLAVE, 3]) + struct.pack(">HH", reg, n))
        if isinstance(rx, (bytes, bytearray)) and len(rx) >= 3 + rx[2] + 2:
            return list(struct.unpack(">%dH" % (rx[2] // 2), rx[3:3 + rx[2]]))
        return rx
    def write(self, reg, val):
        rx = self._txn(bytes([SLAVE, 6]) + struct.pack(">HH", reg, val))
        return rx if isinstance(rx, str) else (rx is not None)

def dump_all(dev):
    out = {}
    for base in range(0, 1000, 20):
        v = dev.read(base, 20)
        if isinstance(v, list):
            for i, x in enumerate(v): out[base + i] = x
        elif isinstance(v, str):
            for r in range(base, base + 20):
                w = dev.read(r, 1)
                if isinstance(w, list): out[r] = w[0]
    return out

def load(path):
    vals = {}
    for line in open(path, encoding="utf-8"):
        m = re.match(r"\s*P\.(\d+)\s*=\s*(\d+)", line)
        if m: vals[int(m.group(1))] = int(m.group(2))
    return vals

def main():
    if len(sys.argv) < 4: print(__doc__); sys.exit(2)
    cmd, ip, path = sys.argv[1], sys.argv[2], sys.argv[3]
    dev = SE3(ip)
    if cmd == "dump":
        vals = dump_all(dev)
        with open(path, "w", encoding="utf-8") as f:
            f.write("# SE3 全參數傾印 %s (%s, slave %d)\n" % (time.strftime("%Y-%m-%d %H:%M"), ip, SLAVE))
            for k in sorted(vals): f.write("P.%d = %d\n" % (k, vals[k]))
        print("dumped %d params → %s" % (len(vals), path)); return
    if cmd == "write":
        apply = "--apply" in sys.argv
        src = load(path); cur = dump_all(dev)
        todo = [(k, cur.get(k), v) for k, v in sorted(src.items()) if k not in SKIP and cur.get(k) != v]
        print("目標 %s:%d 個參數與檔案不同%s" % (ip, len(todo), "" if apply else "(dry-run,加 --apply 才寫)"))
        for k, c, v in todo: print("  P.%-4d %6s → %d" % (k, c, v))
        if not apply: return
        ok = bad = ro = 0
        for k, c, v in todo:
            r = dev.write(k, v)
            if r is True:
                back = dev.read(k, 1)
                if isinstance(back, list) and back[0] == v: ok += 1
                else: bad += 1; print("  🔴 P.%d 寫入後回讀 %s ≠ %d" % (k, back, v))
            else:
                ro += 1; print("  ⚪ P.%d 拒絕(%s)— 唯讀/監視暫存器,略過" % (k, r))
            time.sleep(0.05)
        print("done: 寫入成功 %d、回讀不符 %d、被拒 %d" % (ok, bad, ro)); return
    print(__doc__); sys.exit(2)

if __name__ == "__main__": main()
