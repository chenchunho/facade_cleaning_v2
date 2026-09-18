#!/usr/bin/env python3
# [2026-09-17 per user「工具重心對 M1 有影響嗎 — 先吸附、量測看看」] Bench tool: does the M2 tool slot
# (roller / squeegee) change the gravity moment M1 sees?  Body attached (cups sealed), arm in free air.
# Runs on the crane Pi (LAN to the body .26):  scp scripts/m1_tool_gravity.py user@192.168.5.25:~/run/
#   python3 m1_tool_gravity.py attach       # vacuum feet on → pusher all extend_raw → wait cups (pump must be on: `pump on`)
#   python3 m1_tool_gravity.py measure      # static: RIGHT → LEFT → RIGHT at θ 0.30/0.40/0.50 (friction-limited, ±2 N·m — inconclusive)
#   python3 m1_tool_gravity.py sweep [trips]# bidirectional slow sweep 0.25↔0.52 @0.12 rad/s, G = -(T_out+T_in)/2 (friction cancels) ← use this
# Result 2026-09-17 (3 trips each, RIGHT repeated): squeegee G is 0.3–0.9 N·m lower than roller at every θ bin
# (mean ≈ −0.5 N·m, RIGHT repeat spread ±0.2–0.5) ⇒ same tau target presses ~0.5 N·m (≈10 % of 5 N·m) lighter with the squeegee.
# Friction band f = 0.7–1.9 N·m (matches 09-02). See .claude/work_log.md 2026-09-17.
import socket, sys, time, statistics

BODY = ("192.168.5.26", 5001)
ARM  = ("192.168.5.26", 9527)
THETAS = [0.30, 0.40, 0.50]
TOUCH_NM = 2.0          # DEPLOY_F_TOUCH_NM — above this in "free air" means we hit something → abort that θ

def ask(addr, cmd, timeout=10):
    s = socket.create_connection(addr, timeout=timeout)
    s.sendall((cmd + "\n").encode())
    buf = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            ch = s.recv(4096)
        except socket.timeout:
            break
        if not ch: break
        buf += ch
        if buf.endswith(b"\n") and (addr != ARM or cmd != "STATUS" or b"[M2]" in buf): break
    s.close()
    return buf.decode("utf-8", "replace").strip()

def fields(line):
    o = {}
    for tok in line.replace("|", " ").split():
        if "=" in tok:
            k, v = tok.split("=", 1); o[k] = v
    return o

def m1():
    r = ask(ARM, "STATUS", 5)
    seg = r.split("[M2]")[0]
    f = fields(seg)
    tool = fields(r.split("[M2]")[-1]).get("tool", "?") if "[M2]" in r else "?"
    return float(f.get("pos", "nan")), float(f.get("tau", "nan")), int(f.get("moving", "1")), tool

def wait_m1(target, tmo=20):
    t0 = time.time()
    while time.time() - t0 < tmo:
        pos, tau, mv, _ = m1()
        if tau > TOUCH_NM and target > 0.05 and pos > 0.15:   # past the hard-stop kick-off; free air tau is negative here
            return ("TOUCH", pos, tau)
        if mv == 0 and abs(pos - target) < 0.03:
            return ("OK", pos, tau)
        time.sleep(0.3)
    return ("TIMEOUT", pos, tau)

def attach():
    print("pump →", ask(BODY, "pump on", 15))   # cups never seal with the pump off (first run today)
    print("vacuum feet on →", ask(BODY, "vacuum feet on", 20))
    t = time.time()
    print("pusher all extend_raw →", ask(BODY, "pusher all extend_raw", 90), "(%.1fs)" % (time.time() - t))
    t = time.time(); pr = None
    while time.time() - t < 25:
        f = fields(ask(BODY, "status", 10))
        pr = [float(f.get("p%d" % n, "nan")) for n in (5, 6, 7, 8)]
        if sum(1 for p in pr if p <= -50) >= 4: break
        time.sleep(0.5)
    print("cups p5..p8 = %s after %.1fs, sealed=%d" % (pr, time.time() - t, sum(1 for p in pr if p <= -50)))

def measure():
    rows = []
    for tool in ("RIGHT", "LEFT", "RIGHT"):
        pos, tau, mv, cur = m1()
        if pos > 0.15:
            print("M1 → 0 first:", ask(ARM, "M1 MOVETO 0", 10)); print("  ", wait_m1(0.0))
        r = ask(BODY, "arm_slot " + tool, 40)
        print("arm_slot %s → %s" % (tool, r))
        if not r.startswith("OK"):
            print("abort: slot switch failed"); break
        time.sleep(2.0)                          # let M2 hold torque build (09-16 slip lesson)
        _, _, _, cur = m1()
        for th in THETAS:
            r = ask(ARM, "M1 MOVETO %.2f" % th, 10)
            st, pos, tau = wait_m1(th)
            if st != "OK":
                print("  θ=%.2f %s pos=%.4f tau=%.3f → skip" % (th, st, pos, tau))
                if st == "TOUCH":
                    ask(ARM, "M1 MOVETO 0", 10); wait_m1(0.0)
                    break
                continue
            time.sleep(2.0)                      # settle (relaxation ~1 s)
            ps, ts = [], []
            for _ in range(5):
                p, t_, _, _ = m1(); ps.append(p); ts.append(t_); time.sleep(0.5)
            rows.append((tool, cur, th, statistics.median(ps), statistics.median(ts), max(ts) - min(ts)))
            print("  %-5s tool=%-8s θcmd=%.2f pos=%.4f tau=%.3f (spread %.3f)" % rows[-1])
        print("M1 → 0:", ask(ARM, "M1 MOVETO 0", 10), wait_m1(0.0))
    print("\n=== summary (median tau at same θ; ΔG = LEFT − RIGHT) ===")
    for th in THETAS:
        R = [r for r in rows if r[0] == "RIGHT" and r[2] == th]
        L = [r for r in rows if r[0] == "LEFT" and r[2] == th]
        if R and L:
            rt = statistics.mean(r[4] for r in R); lt = L[0][4]
            print("θ=%.2f  RIGHT tau=%s  LEFT tau=%.3f  Δ=%.3f N·m  (RIGHT repeat spread %.3f)" %
                  (th, "/".join("%.3f" % r[4] for r in R), lt, lt - rt, (max(r[4] for r in R) - min(r[4] for r in R)) if len(R) > 1 else 0.0))
    print("final:", m1())


LO, HI, SPD = 0.25, 0.52, 0.12
BIN = 0.03
TOUCH_NM = 2.0

def sweep_once(target):
    ask(ARM, "M1 MOVETO %.2f %.2f" % (target, SPD), 10)
    samples = []
    t0 = time.time()
    while time.time() - t0 < 12:
        r = ask(ARM, "M1 STATUS", 3)
        f = fields(r)
        try:
            pos, vel, tau, mv = float(f["pos"]), float(f["vel"]), float(f["tau"]), int(f["moving"])
        except (KeyError, ValueError):
            continue
        samples.append((pos, vel, tau))
        if tau > TOUCH_NM and pos > 0.2:
            ask(ARM, "M1 MOVETO %.2f 0.3" % LO, 10); wait_m1(LO)
            return samples, "TOUCH pos=%.3f tau=%.2f" % (pos, tau)
        if mv == 0 and abs(pos - target) < 0.02: break
    return samples, "ok"

def run_tool(tool, trips):
    pos, tau, mv, cur = m1()
    if pos > 0.15:
        ask(ARM, "M1 MOVETO 0 0.3", 10); wait_m1(0.0)
    r = ask(BODY, "arm_slot " + tool, 40)
    print("arm_slot %s → %s" % (tool, r))
    if not r.startswith("OK"): return None
    time.sleep(2.0)
    _, _, _, cur = m1()
    ask(ARM, "M1 MOVETO %.2f 0.3" % LO, 10); print("  to LO:", wait_m1(LO)); time.sleep(1.0)
    out, inn = [], []
    for k in range(trips):
        s, st = sweep_once(HI); out += s; print("  trip %d out: %d samples %s" % (k + 1, len(s), st))
        if st != "ok": break
        time.sleep(0.8)
        s, st = sweep_once(LO); inn += s; print("  trip %d in : %d samples %s" % (k + 1, len(s), st))
        time.sleep(0.8)
    ask(ARM, "M1 MOVETO 0 0.3", 10); wait_m1(0.0)
    # bin by position, only truly sliding samples (|vel| > 0.05, per 09-02: slower is stick-slip)
    def binned(ss, sign):
        d = {}
        for p, v, t in ss:
            if sign * v > 0.05:
                d.setdefault(round((p - LO) / BIN), []).append(t)
        return {k: statistics.median(v) for k, v in d.items() if len(v) >= 3}
    return cur, binned(out, +1), binned(inn, -1)

def sweep():
    trips = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    res = {}
    for tool in ("RIGHT", "LEFT", "RIGHT2"):
        r = run_tool(tool.rstrip("2"), trips)
        if r: res[tool] = r
    print("\n=== G(θ) = -(T_out + T_in)/2 per bin  [N·m]   f = (T_out - T_in)/2 ===")
    keys = sorted(set(k for r in res.values() for k in set(r[1]) & set(r[2])))
    print("θ      " + "".join("%-22s" % ("%s(%s)" % (t, r[0])) for t, r in res.items()) + "ΔG LEFT−RIGHT")
    for k in keys:
        th = LO + k * BIN
        line = "%.3f  " % th; g = {}
        for t, (cur, o, i) in res.items():
            if k in o and k in i:
                g[t] = -(o[k] + i[k]) / 2; fr = (o[k] - i[k]) / 2
                line += "G=%6.2f f=%4.2f       " % (g[t], fr)
            else:
                line += "%-22s" % "-"
        if "LEFT" in g and "RIGHT" in g:
            line += "%+.2f" % (g["LEFT"] - g["RIGHT"])
            if "RIGHT2" in g: line += "  (RIGHT repeat %+.2f)" % (g["RIGHT2"] - g["RIGHT"])
        print(line)
    print("final:", m1())


if __name__ == "__main__":
    if sys.argv[1] == "sweep": sweep()
    else: {"attach": attach, "measure": measure}[sys.argv[1]]()
