#!/usr/bin/env python3
# retract_seg.py — one monitored retract segment.
#
# Issues `retract <cm>` on the crane and, IN PARALLEL, samples crane
# length_left/right + washrobot IMU roll. Trips `stop` early if the two ropes
# diverge or the body tilts — both thresholds sit BELOW the firmware's own
# length_diff_max_cm=15 so we intervene before it aborts mid-motion.
#
# Rationale (2026-09-01): a 10cm test retract showed the LEFT VFD silently
# failing to start (delta_L=0 while delta_R=-10) with sync_start reporting
# err=0. The length-diff balance controller cannot correct that case — it trims
# frequency, and trimming a motor that never started does nothing.
#
# usage: retract_seg.py <retract|pay_out> <cm> [diff_trip_cm] [roll_trip_deg]

import socket, sys, time, threading, re

CRANE = ("127.0.0.1", 5002)
WROBOT = ("192.168.5.26", 5001)

VERB = sys.argv[1] if len(sys.argv) > 1 else "retract"
assert VERB in ("retract", "pay_out"), "verb must be retract|pay_out"
cm = int(sys.argv[2]) if len(sys.argv) > 2 else 30
DIFF_TRIP = float(sys.argv[3]) if len(sys.argv) > 3 else 4.0    # < firmware length_diff_max_cm
ROLL_TRIP = float(sys.argv[4]) if len(sys.argv) > 4 else 6.0

stop_evt = threading.Event()
tripped = []
samples = []


def ask(addr, cmd, timeout=5):
    """One request/response. Skips EVT lines, returns the first OK/ERR line."""
    s = socket.create_connection(addr, timeout=timeout)
    s.settimeout(timeout)
    s.sendall((cmd + "\n").encode())
    buf = b""
    try:
        while True:
            d = s.recv(4096)
            if not d:
                break
            buf += d
            for line in buf.decode(errors="replace").splitlines():
                if line.startswith("OK") or line.startswith("ERR"):
                    s.close()
                    return line
    except socket.timeout:
        pass
    s.close()
    return buf.decode(errors="replace").strip()


def field(line, key, cast=float):
    m = re.search(rf"\b{key}=(-?[\d.]+)", line)
    return cast(m.group(1)) if m else None


def monitor(base_l, base_r):
    while not stop_evt.is_set():
        try:
            cs = ask(CRANE, "status", 3)
            ws = ask(WROBOT, "status", 3)
            L, R = field(cs, "length_left"), field(cs, "length_right")
            src = re.search(r"balance_source=(\w+)", cs)
            tl, tr = field(cs, "tension_left"), field(cs, "tension_right")
            roll = field(ws, "roll")
            if L is None or R is None:
                time.sleep(0.4)
                continue
            pl, pr = abs(L - base_l), abs(R - base_r)       # per-side progress
            diff = abs(pl - pr)
            samples.append((round(time.time() - T0, 1), L, R, pl, pr, diff, roll, tl, tr))
            if diff > DIFF_TRIP:
                tripped.append(f"位移差 {diff:.0f}cm > {DIFF_TRIP:.0f}cm (L走{pl:.0f} R走{pr:.0f})")
                break
            if roll is not None and abs(roll) > ROLL_TRIP:
                tripped.append(f"roll {roll:.2f}° > {ROLL_TRIP:.1f}°")
                break
        except Exception as e:
            samples.append((round(time.time() - T0, 1), None, None, None, None, None, None, None, str(e)[:30]))
        time.sleep(0.4)
    if tripped:
        try:
            print(f"\n🔴 自動中止觸發: {tripped[0]}  → 送出 stop")
            print("   stop 回應:", ask(CRANE, "stop", 5))
        except Exception as e:
            print("   🔴 stop 送出失敗:", e)


pre = ask(CRANE, "status", 5)
base_l, base_r = field(pre, "length_left"), field(pre, "length_right")
print(f"起點 L={base_l:.0f} R={base_r:.0f}  {VERB} {cm}cm  "
      f"(中止門檻: 位移差>{DIFF_TRIP:.0f}cm 或 |roll|>{ROLL_TRIP:.1f}°)")

T0 = time.time()
th = threading.Thread(target=monitor, args=(base_l, base_r), daemon=True)
th.start()

s = socket.create_connection(CRANE, timeout=10)
s.settimeout(150)
s.sendall(f"{VERB} {cm}\n".encode())
result, buf = None, b""
try:
    while result is None:
        d = s.recv(4096)
        if not d:
            break
        buf += d
        for line in buf.decode(errors="replace").splitlines():
            if line.startswith("OK") or line.startswith("ERR"):
                result = line
                break
except socket.timeout:
    result = "TIMEOUT(讀取)"
s.close()
elapsed = time.time() - T0
stop_evt.set()
th.join(timeout=6)

print(f"\n耗時 {elapsed:.1f}s   結果: {result}")
print(f"\n{'t(s)':>6} {'L':>5} {'R':>5} {'L走':>5} {'R走':>5} {'差':>4} {'roll':>7} {'tenL':>7} {'tenR':>7}")
for r in samples:
    t, L, R, pl, pr, df, ro, tl, tr = r
    if L is None:
        print(f"{t:>6} {'讀取失敗':>10} {tr}")
        continue
    print(f"{t:>6} {L:>5.0f} {R:>5.0f} {pl:>5.0f} {pr:>5.0f} {df:>4.0f} "
          f"{(f'{ro:.2f}' if ro is not None else '—'):>7} "
          f"{(f'{tl:.2f}' if tl is not None else 'ERR'):>7} "
          f"{(f'{tr:.2f}' if tr is not None else 'ERR'):>7}")

# [2026-09-01] 任務驗收統計：10 趟來回要證明的是「全程本體平衡」，
# 光看「跑完了沒事」不算證據。這裡輸出可判定的量。
rolls = [r[6] for r in samples if r[6] is not None]
diffs = [r[5] for r in samples if r[5] is not None]
if rolls:
    out = [r for r in rolls if abs(r) > 1.0]
    print(f"\n=== 統計 ===")
    print(f"  取樣 {len(rolls)} 筆")
    print(f"  |roll| 最大 {max(abs(r) for r in rolls):.2f}°   平均 {sum(abs(r) for r in rolls)/len(rolls):.2f}°")
    print(f"  超出 ±1° 的取樣: {len(out)}/{len(rolls)} ({100*len(out)/len(rolls):.0f}%)"
          + (f"   最大 {max(abs(r) for r in out):.2f}°" if out else ""))
    print(f"  左右差最大 {max(diffs):.0f}cm")

post = ask(CRANE, "status", 5)
pl_, pr_ = field(post, "length_left"), field(post, "length_right")
print(f"\n終點 L={pl_:.0f} R={pr_:.0f}   實走 L={abs(pl_-base_l):.0f} R={abs(pr_-base_r):.0f} (指令 {cm})")
if tripped:
    print(f"🔴 本段被自動中止: {tripped[0]}")
