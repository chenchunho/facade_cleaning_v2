#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""座標變更後的乾跑檢查：不動馬達，只驗方向邏輯。

刻意用 exec 從原始檔抽出真正的函式定義來跑 —— 重打一份等於測副本，
而副本正確不代表腳本正確（這正是這次要防的錯誤類型）。
"""
import io, re, socket, sys

def ask(cmd):
    s = socket.create_connection(("127.0.0.1", 5002), timeout=10); s.settimeout(20)
    s.sendall((cmd + "\n").encode()); b = b""
    while True:
        d = s.recv(4096)
        if not d: break
        b += d
        for l in b.decode(errors="replace").splitlines():
            if l.startswith(("OK", "ERR")): s.close(); return l.strip()
    return ""

def grab(path, names):
    """從原始檔取出指定的 def 區塊並 exec —— 測的是本體。"""
    src = io.open(path, encoding="utf-8").read()
    ns = {}
    for n in names:
        m = re.search(r"^def %s\(.*?(?=^\S|\Z)" % n, src, re.S | re.M)
        if not m: raise SystemExit("找不到 def %s in %s" % (n, path))
        exec(compile(m.group(0), path, "exec"), ns)
    # 常數
    for k in ("TOP", "BOTTOM", "TOL", "STEP_CM"):
        mm = re.search(r"^%s\s*=\s*([^\n#]+)" % k, src, re.M)
        if mm:
            try: ns[k] = eval(mm.group(1).strip(), {"sys": sys, "int": int, "len": len})
            except Exception: pass
    mm = re.search(r"^TOP,\s*BOTTOM\s*=\s*(\d+),\s*(\d+)", src, re.M)
    if mm: ns["TOP"], ns["BOTTOM"] = int(mm.group(1)), int(mm.group(2))
    return ns

st = ask("status")
L = float(re.search(r"length_left=(-?[\d.]+)", st).group(1))
R = float(re.search(r"length_right=(-?[\d.]+)", st).group(1))
print("實測 繩長 L=%.0f R=%.0f" % (L, R))

# ---------- cycle_test.py ----------
ct = grab("cycle_test.py", ["height"])
h = ct["height"](L)
TOP, BOTTOM, TOL, STEP = ct["TOP"], ct["BOTTOM"], 5, 40
print("\n=== cycle_test.py ===")
print("  TOP=%d BOTTOM=%d" % (TOP, BOTTOM))
print("  height(%.0f) = %.0f cm" % (L, h))
ok_start = abs(h - TOP) <= TOL
print("  起點檢查 |%.0f - %d| <= %d  → %s" % (h, TOP, TOL, "✅ 通過（在頂端）" if ok_start else "🔴 拒跑"))
end = h - STEP
print("  第 1 步預期終點 = %.0f - %d = %.0f cm" % (h, STEP, end))
print("  區間守衛 end < BOTTOM-TOL(%d) ？ → %s" % (BOTTOM - TOL, "🔴 會中止" if end < BOTTOM - TOL else "✅ 允許"))
print("  5 步後 = %.0f cm（應 >= 0，不得低於底端）" % (h - 5 * STEP))
print("  回程收繩量 = TOP - cur = %d - %.0f = %.0f cm" % (TOP, h - 5 * STEP, TOP - (h - 5 * STEP)))

# ---------- mission_run.py ----------
mr = grab("mission_run.py", ["height", "plan_leg", "check_envelope"])
mr_ns = mr
h2 = mr_ns["height"](L)
print("\n=== mission_run.py ===")
print("  TOP=%d BOTTOM=%d" % (mr_ns["TOP"], mr_ns["BOTTOM"]))
pl = mr_ns["plan_leg"](h2)
if pl is None:
    print("  plan_leg(%.0f) → None（不在端點，拒絕執行）" % h2)
else:
    verb, cm, tgt, label = pl
    print("  plan_leg(%.0f) → %s %d cm，目標 %d（%s）" % (h2, verb, cm, tgt, label))
    bad = mr_ns["check_envelope"](verb, cm, h2)
    print("  check_envelope → %s" % ("🔴 " + bad if bad else "✅ 通過"))
    # 反向那一趟
    h3 = tgt
    pl2 = mr_ns["plan_leg"](h3)
    if pl2:
        v2, c2, t2, l2 = pl2
        print("  到達 %d 後 plan_leg → %s %d cm，目標 %d（%s）" % (h3, v2, c2, t2, l2))
        print("  check_envelope → %s" % ("🔴 " + (mr_ns["check_envelope"](v2, c2, h3) or "")
                                          if mr_ns["check_envelope"](v2, c2, h3) else "✅ 通過"))

print("\n=== 負向對照：假裝在底端 ===")
print("  cycle_test 起點檢查 |0 - %d| <= %d → %s" % (TOP, TOL, "🔴 竟然通過（危險）" if abs(0-TOP)<=TOL else "✅ 正確拒跑"))
pl3 = mr_ns["plan_leg"](0)
print("  mission_run plan_leg(0) → %s" % (str(pl3[:2]) + " （應為往上 retract）" if pl3 else "None"))
