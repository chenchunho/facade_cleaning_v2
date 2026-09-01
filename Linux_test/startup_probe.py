#!/usr/bin/env python3
# startup_probe.py — characterise the wall-contact START transient.
#
# 2026-09-01：貼牆＋風扇下行時，起步 0.4 秒內左右差到 5cm、roll 衝到 -4.08°，
# 而同樣條件的行進段全程最大只有 0.69°。推測是靜摩擦：四輪被風扇壓在玻璃上，
# 起步要克服靜摩擦，哪一側先掙脫就先走。
#
# 既有的 retract_seg.py 每筆取樣要查兩台（約 2-3Hz），0.4 秒的瞬態只會被取到一次
# ——看不出峰值與形狀。這支**只查本體 roll**，用單一長連線連續問，取樣率高得多。
#
# 每次：起步前靜置取基線 → 下指令 → 高速取樣 → 停止後再取尾段。
# usage: startup_probe.py <n_reps> <cm> [roll_trip]

import re, socket, sys, threading, time

CRANE = ("127.0.0.1", 5002)
WROBOT = ("192.168.5.26", 5001)
REPS = int(sys.argv[1]) if len(sys.argv) > 1 else 5
CM = int(sys.argv[2]) if len(sys.argv) > 2 else 15
ROLL_TRIP = float(sys.argv[3]) if len(sys.argv) > 3 else 6.0


def ask(addr, cmd, timeout=5):
    s = socket.create_connection(addr, timeout=timeout)
    s.settimeout(timeout)
    s.sendall((cmd + "\n").encode())
    buf = b""
    try:
        while True:
            d = s.recv(4096)
            if not d: break
            buf += d
            for line in buf.decode(errors="replace").splitlines():
                if line.startswith("OK") or line.startswith("ERR"):
                    s.close(); return line
    except socket.timeout:
        pass
    s.close()
    return buf.decode(errors="replace").strip()


def field(line, key):
    m = re.search(rf"\b{key}=(-?[\d.]+)", line)
    return float(m.group(1)) if m else None


print(f"起步瞬態量測：{REPS} 次 × {CM}cm 下行   roll 中止門檻 {ROLL_TRIP}°")
print(f"{'#':>3} {'基線':>7} {'峰值':>7} {'峰值時間':>9} {'ΔL':>4} {'ΔR':>4} {'差':>3}  結果")

peaks = []
for rep in range(1, REPS + 1):
    pre = ask(CRANE, "status", 5)
    bl, br = field(pre, "length_left"), field(pre, "length_right")
    base_roll = field(ask(WROBOT, "status", 5), "roll")

    trace, stop_evt, tripped = [], threading.Event(), []

    def mon():
        # 單一長連線，連續問 —— 比每次新建連線快得多
        s = socket.create_connection(WROBOT, timeout=5); s.settimeout(2)
        buf = ""
        while not stop_evt.is_set():
            try:
                s.sendall(b"status\n")
                buf = ""
                while "\n" not in buf:
                    buf += s.recv(8192).decode(errors="replace")
                line = buf.split("\n")[0]
                r = field(line, "roll")
                if r is not None:
                    trace.append((time.time() - t0, r))
                    if abs(r) > ROLL_TRIP:
                        tripped.append(r); break
            except Exception:
                pass
            time.sleep(0.05)
        s.close()
        if tripped:
            try: ask(CRANE, "stop", 5)
            except Exception: pass

    t0 = time.time()
    th = threading.Thread(target=mon, daemon=True); th.start()
    c = socket.create_connection(CRANE, timeout=10); c.settimeout(120)
    c.sendall(f"pay_out {CM}\n".encode())
    res, rb = None, b""
    try:
        while res is None:
            d = c.recv(4096)
            if not d: break
            rb += d
            for line in rb.decode(errors="replace").splitlines():
                if line.startswith("OK") or line.startswith("ERR"):
                    res = line; break
    except socket.timeout:
        res = "TIMEOUT"
    c.close()
    stop_evt.set(); th.join(timeout=4)

    post = ask(CRANE, "status", 5)
    dl = abs(field(post, "length_left") - bl)
    dr = abs(field(post, "length_right") - br)
    if trace:
        pk_t, pk_r = max(trace, key=lambda x: abs(x[1] - base_roll))
        dev = pk_r - base_roll
        peaks.append(abs(dev))
        print(f"{rep:>3} {base_roll:>7.2f} {pk_r:>7.2f} {pk_t:>8.2f}s "
              f"{dl:>4.0f} {dr:>4.0f} {abs(dl-dr):>3.0f}  {res[:26]}"
              f"  (取樣 {len(trace)} 筆, 偏離 {dev:+.2f}°)")
    else:
        print(f"{rep:>3}  無取樣  {res[:30]}")
    time.sleep(3)          # 讓機器沉澱再做下一次

if peaks:
    print(f"\n=== {len(peaks)} 次起步的峰值偏離 ===")
    print(f"  最小 {min(peaks):.2f}°   最大 {max(peaks):.2f}°   平均 {sum(peaks)/len(peaks):.2f}°")
    big = [p for p in peaks if p > 1.0]
    print(f"  超過 1°: {len(big)}/{len(peaks)}"
          + ("  → 系統性，值得為它改架構" if len(big) >= len(peaks) * 0.6
             else "  → 偶發，不值得為它改架構"))
