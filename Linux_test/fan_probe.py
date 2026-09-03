#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""QX-DO24 風扇寫入故障的分辨測試（2026-09-03 per user）。

問題：09-02 記載「失敗在主站→模組方向，長幀進不去」，但 09-03 觀察到
「所有關閉指令都回報失敗，風扇卻確實停了」⇒ 可能相反：寫入有送達、回覆遺失。

做法：開/關各 10 次，每次寫入後用 `pwm status`（即時讀模組暫存器）回讀 duty。
  寫入回報 OK  + duty 相符  → 正常
  寫入回報 ERR + duty **相符** → 寫入有進去、只是回覆遺失   ← 待驗證的假設
  寫入回報 ERR + duty 未變     → 寫入真的沒進去（維持原記載）
不靠 ACK 判定，也不靠目視 —— 目視是獨立的第三方對照。
"""
import re, socket, sys, time
sys.stdout.reconfigure(line_buffering=True)

HOST, PORT = "127.0.0.1", 5001
ON, OFF = 7, 5
N = 10

def ask(cmd, wait=25):
    s = socket.create_connection((HOST, PORT), timeout=10); s.settimeout(wait)
    s.sendall((cmd + "\n").encode()); buf = b""
    try:
        while True:
            d = s.recv(4096)
            if not d: break
            buf += d
            for line in buf.decode(errors="replace").splitlines():
                if line.startswith(("OK", "ERR")):
                    s.close(); return line.strip()
    except socket.timeout:
        pass
    s.close()
    return "(no reply)"

HZ, CTRL = 50, 65535

def regs():
    """回讀 ch1 四個欄位。格式：OK ch1=<duty>,<hz>,<ctrl>,<en> ...

    ⚠️ 分辨力的界線（2026-09-03）：hz 與 ctrl **每次都寫同一個值**（50 / 65535），
    所以「control 寫入失敗」時暫存器本來就已經是對的，回讀分不出「寫進去了」還是
    「沒寫但值剛好對」。**四欄比對不能證明 control 寫入成功**——它能回答的是另一個
    更有用的問題：那次失敗有沒有留下功能後果（duty 對不對、模組還在不在該有的狀態）。
    """
    r = ask("pwm status", 25)
    m = re.search(r"ch1=(\d+),(\d+),(\d+),(\d+)", r)
    if not m: return None, r
    return tuple(int(x) for x in m.groups()), r

print("=== QX-DO24 風扇寫入分辨測試：開/關各 %d 次 ===" % N)
d0, _ = regs()
print("起始 (duty,hz,ctrl,en) = %s   (ON=%d / OFF=%d，hz 固定 %d、ctrl 固定 %d)\n"
      % (d0, ON, OFF, HZ, CTRL))
print(" # 動作 目標  寫入回報                                      回讀 duty,hz,ctrl,en   判定")
tally = {}
for i in range(1, N + 1):
    for label, target in (("開", ON), ("關", OFF)):
        w = ask("pwm set 1 50 65535 %d" % target, 25)
        time.sleep(0.8)
        g, raw = regs()
        d = g[0] if g else None
        ok_w = w.startswith("OK")
        unv  = "unverified" in w
        # 🔴 關鍵那一格是「unverified 但實際沒生效」—— 09-03 的折衷（回讀逾時改回
        # OK...unverified 而非 ERR）**只有在那格是 0 的時候才成立**。不是 0 就要改回去。
        if g is None:
            verdict = "回讀失敗(無法判定)"
        else:
            bad = []
            if g[0] != target: bad.append("duty")
            if g[1] != HZ:     bad.append("hz")
            if g[2] != CTRL:   bad.append("ctrl")
            if not bad:
                # 四欄全中 = 模組處於該有的狀態，不論那道指令回報了什麼。
                verdict = ("unverified 但狀態正確" if unv else
                           ("正常" if ok_w else "ERR 但狀態正確"))
            else:
                verdict = ("🔴 unverified 但狀態不符(%s)" if unv else
                           ("🔴 回報OK 但狀態不符(%s)" if ok_w else
                            "ERR 且狀態不符(%s)")) % "+".join(bad)
        tally[verdict] = tally.get(verdict, 0) + 1
        print("%2d %s  %2d   %-44s %-18s  %s"
              % (i, label, target, w[:44], str(g), verdict))
        time.sleep(1.2)

print("\n=== 統計 ===")
for k, v in tally.items():
    if v: print("  %-24s %d" % (k, v))
print("\n收尾：確保風扇關閉")
for k in range(1, 7):
    wz = ask("pwm set 1 50 65535 %d" % OFF, 25)
    time.sleep(1.0)
    g1, _ = regs()
    print("  嘗試 %d: %-44s 回讀=%s" % (k, wz[:44], g1))
    if g1 and g1[0] == OFF:
        print("✅ 已確認 duty=%d（關閉）" % OFF); break
    time.sleep(1.5)
else:
    print("🔴 六次都無法確認關閉狀態 —— 請目視螺旋槳")
