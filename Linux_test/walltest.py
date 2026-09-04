#!/usr/bin/env python3
# One DEPLOY at a given wall_mm, then poll arm_status until M1 stops moving.
# Verdict is read from the pose (theta/tau), not from the DEPLOY return value:
# pressing against glass legitimately answers ERR.
import socket, sys, time, re

HOST, PORT = "127.0.0.1", 5001

def ask(sock, f, cmd, timeout, prefixes=("OK", "ERR", "[M1]")):
    sock.sendall((cmd + "\n").encode())
    deadline = time.time() + timeout
    while time.time() < deadline:
        sock.settimeout(max(0.2, deadline - time.time()))
        try:
            line = f.readline()
        except socket.timeout:
            break
        if not line:
            break
        line = line.strip()
        if not line:
            continue
        if line.startswith(prefixes):
            return line
    return "<timeout>"

def parse(status):
    m1 = re.search(r"\[M1\] pos=(-?[\d.]+) vel=(-?[\d.]+) tau=(-?[\d.]+)", status)
    m2 = re.search(r"\[M2\] pos=(-?[\d.]+) vel=(-?[\d.]+) tau=(-?[\d.]+)", status)
    if not m1:
        return None
    d = {"m1_pos": float(m1.group(1)), "m1_vel": float(m1.group(2)), "m1_tau": float(m1.group(3))}
    if m2:
        d.update({"m2_pos": float(m2.group(1)), "m2_vel": float(m2.group(2)), "m2_tau": float(m2.group(3))})
    return d

def settle(sock, f, label, max_wait=40.0):
    """Poll until |vel| is small and pos stops changing across two reads."""
    t0 = time.time()
    prev = None
    stable = 0
    while time.time() - t0 < max_wait:
        st = ask(sock, f, "arm_status", 15, prefixes=("[M1]",))
        d = parse(st)
        if d is None:
            print(f"  [{label}] status unparsed: {st}")
            time.sleep(1.0)
            continue
        moved = None if prev is None else abs(d["m1_pos"] - prev)
        print(f"  [{label}] t={time.time()-t0:5.1f}s  M1 pos={d['m1_pos']:+.4f} vel={d['m1_vel']:+.4f} "
              f"tau={d['m1_tau']:+.2f}  M2 pos={d.get('m2_pos',float('nan')):+.4f} tau={d.get('m2_tau',float('nan')):+.2f}"
              + ("" if moved is None else f"  d_pos={moved:.4f}"))
        if moved is not None and moved < 0.0005 and abs(d["m1_vel"]) < 0.01:
            stable += 1
            if stable >= 2:
                return d
        else:
            stable = 0
        prev = d["m1_pos"]
        time.sleep(1.0)
    return d

def main():
    wall = sys.argv[1]
    slot = sys.argv[2] if len(sys.argv) > 2 else "RIGHT"
    sock = socket.create_connection((HOST, PORT), timeout=10)
    f = sock.makefile("r")
    print(f"=== DEPLOY {wall} {slot} ===")
    t0 = time.time()
    r = ask(sock, f, f"arm_deploy {wall} {slot}", 90)
    print(f"  deploy reply after {time.time()-t0:.1f}s: {r}   (ERR here is expected when pressing glass)")
    d = settle(sock, f, f"deploy{wall}")
    print(f"  >>> SETTLED {wall}: M1 pos={d['m1_pos']:+.4f} tau={d['m1_tau']:+.2f}")
    sock.close()

main()
