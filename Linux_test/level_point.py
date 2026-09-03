#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Measure the "level point" (the L-R meter difference at which roll ~= 0) at the
current height, with the balance loop disabled so it does not fight the probe.

One deliberate 2 cm single-side move, then restore. Everything is read back --
commanded centimetres are known to overshoot by ~1 cm (work_log 2026-09-01).
"""
import socket, sys, time, re

HOST, PORT = "127.0.0.1", 5002
ABORT_ROLL   = 3.0    # deg, instantaneous
ABORT_TDIFF  = 40.0   # kg, left-right tension difference
MOVE_CM      = 2

sock = socket.create_connection((HOST, PORT), timeout=10)
sock.settimeout(30)
buf = b""

def send(cmd, settle=0.0):
    global buf
    sock.sendall((cmd + "\n").encode())
    deadline = time.time() + 25
    while b"\n" not in buf and time.time() < deadline:
        try:
            chunk = sock.recv(8192)
        except socket.timeout:
            break
        if not chunk:
            break
        buf += chunk
    line, _, buf = buf.partition(b"\n")
    if settle:
        time.sleep(settle)
    return line.decode(errors="replace").strip()

def status():
    s = send("status")
    d = {}
    for m in re.finditer(r"(\w+)=(-?[\d.]+|ERR)", s):
        d[m.group(1)] = m.group(2)
    return d

def read_n(label, n=3, gap=2.0):
    """Sample n times; report spread so noise is visible rather than assumed away."""
    rows = []
    for i in range(n):
        d = status()
        try:
            L = float(d["length_left"]); R = float(d["length_right"])
            roll = float(d["imu_roll"]); tl = float(d["tension_left"]); tr = float(d["tension_right"])
        except (KeyError, ValueError):
            print("  !! status 解析失敗: %s" % d); sys.exit(2)
        rows.append((L, R, roll, tl, tr))
        print("  %-10s L=%-7.1f R=%-7.1f L-R=%-6.1f roll=%+6.2f  T %5.1f/%5.1f (diff %5.1f)"
              % (label if i == 0 else "", L, R, L - R, roll, tl, tr, tl - tr))
        if abs(roll) > ABORT_ROLL:
            print("🔴 中止：|roll| %.2f > %.1f" % (abs(roll), ABORT_ROLL)); sys.exit(3)
        if abs(tl - tr) > ABORT_TDIFF:
            print("🔴 中止：張力差 %.1f > %.1f" % (abs(tl - tr), ABORT_TDIFF)); sys.exit(3)
        if i < n - 1:
            time.sleep(gap)
    avgd = sum(r[0] - r[1] for r in rows) / len(rows)
    avgr = sum(r[2] for r in rows) / len(rows)
    print("  → 平均 L-R=%.2f  roll=%+.3f   (roll 全距 %.2f)"
          % (avgd, avgr, max(r[2] for r in rows) - min(r[2] for r in rows)))
    return avgd, avgr

def wait_idle(timeout=120):
    t0 = time.time()
    while time.time() - t0 < timeout:
        d = status()
        if d.get("motion_active") == "0":
            return True
        time.sleep(1.0)
    return False

print("=== 水平點量測（當前高度，貼牆）===")
print("步驟 0：平衡迴路仍開啟時的基準")
read_n("開啟", 3)

print("\n步驟 1：關閉平衡迴路")
print("  %s" % send("set_balance_enabled off", settle=3.0))
d0, r0 = read_n("關閉後", 3)

print("\n步驟 2：右側收繩 %d cm（L-R 應增加，roll 應下降）" % MOVE_CM)
print("  %s" % send("retract_right %d" % MOVE_CM))
if not wait_idle():
    print("🔴 中止：運動未在時限內結束"); sys.exit(4)
time.sleep(3.0)
d1, r1 = read_n("移動後", 3)

dd = d1 - d0
dr = r1 - r0
print("\n=== 結果 ===")
print("  實際 L-R 變化 : %+.2f cm   (指令 %d cm)" % (dd, MOVE_CM))
print("  實際 roll 變化: %+.3f deg" % dr)
if abs(dd) < 0.5:
    print("  ⚠️ L-R 幾乎沒變，無法求斜率 —— 不計算水平點")
else:
    slope = dr / dd
    print("  斜率          : %+.3f deg/cm   （自由懸吊既有值 ≈ -1.1）" % slope)
    if abs(slope) < 1e-6:
        print("  ⚠️ 斜率為零，無法反推水平點")
        # Still restore below.
    else:
        lp = d1 - r1 / slope
        print("  ⇒ 本高度水平點 L-R = %+.2f cm" % lp)

# Restore by returning the SAME rope we moved. Using the other side would also
# null out L-R, but it would leave both ropes shorter -- i.e. a different height
# and a different geometry, which is the very variable being probed.
print("\n步驟 3：復原（右側放回 %.0f cm，回到原始繩長）" % abs(round(dd)))
n = int(abs(round(dd)))
if n >= 1:
    print("  %s" % send(("pay_out_right %d" % n) if dd > 0 else ("retract_right %d" % n)))
    wait_idle()
    time.sleep(3.0)
read_n("復原後", 3)

print("\n步驟 4：重新開啟平衡迴路")
print("  %s" % send("set_balance_enabled on", settle=2.0))
read_n("完成", 2)
sock.close()
