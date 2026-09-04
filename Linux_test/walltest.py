#!/usr/bin/env python3
# One DEPLOY at a given wall_mm, then poll arm_status until M1 stops moving.
# Verdict is read from the pose (theta/tau), not from the DEPLOY return value:
# pressing against glass legitimately answers ERR.
import socket, sys, time, re

HOST, PORT = "127.0.0.1", 5001

def ask(sock, f, cmd, timeout, prefixes=("OK", "ERR", "WARN", "[M1]")):
    # 🔴 [2026-09-04] WARN 一定要在裡面：DEPLOY_F 未收斂時前綴是 WARN 而不是 OK/ERR。
    #   漏了它，回覆會被當成非同步事件，指令永遠等不到 resolve → 卡到逾時，
    #   症狀長得像「手臂沒回應」，而真正的 WARN 那行已經滾過去了。
    #   （由 facade web gui session 在自己的 transport 上發現同型 bug 後提醒。）
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
    a = _seg_kv(status, "M1")
    b = _seg_kv(status, "M2")
    if not a or "pos" not in a:
        return None
    d = {"m1_pos": a["pos"], "m1_vel": a.get("vel", 0.0), "m1_tau": a.get("tau", 0.0)}
    if b and "pos" in b:
        d.update({"m2_pos": b["pos"], "m2_vel": b.get("vel", 0.0), "m2_tau": b.get("tau", 0.0)})
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

def _seg_kv(status, tag):
    """把 `[M1] k=v k=v ... | [M2] ...` 切段後，段內用**通用 k=v** 掃描。
    🔴 [2026-09-04] 原本是「把整串欄位依序寫死」的正則：
        r"\[M1\] pos=(...) vel=(...) tau=(...)"
    那要求 pos/vel/tau **相鄰且同序**。當天上游在 err= 後面加了 en= 剛好沒炸到，
    但**只要有人把新欄位插在中間，整條比對就會失敗**，而症狀是「解析不到姿態」——
    看起來像通訊問題，除錯的人會去查網路，不會想到是對方多加了一個欄位。
    📌 由 facade web gui session 在自己的前端踩到同一形狀後提醒。
    **新增欄位本來是相容的改動，是「依序寫死的解析」把它變成不相容。**
    """
    i = status.find("[%s]" % tag)
    if i < 0:
        return None
    seg = status[i:]
    j = seg.find("|")            # 只取到下一段之前，避免抓到另一顆馬達的欄位
    if j > 0:
        seg = seg[:j]
    # ⚠️ 只收「純數值」的欄位。err=0x1 這種會**整條不匹配而被略過**，
    #    不會吃到前面那個 "0" 給出一個看起來合理的錯值。使能狀態一律看 en=。
    out = {}
    for k, v in re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=(-?[0-9.]+)(?![0-9A-Za-z.])", seg):
        out[k] = float(v)
    return out or None


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
