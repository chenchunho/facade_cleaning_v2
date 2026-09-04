#!/usr/bin/env python3
# arm_cycle.py — 手臂清潔動作單項耐久測試（per user 2026-09-04）
#
# 一輪 = 壓上(滾筒) -> 上滑台 0->RAIL_CM->0 -> 收手臂
#   usage: arm_cycle.py [rounds] [wall_mm] [slot] [rail_cm]
#   預設: 1 520 RIGHT 100
#
# 這是把 cycle_test.py 的步驟 ③b 抽出來單跑，不含步伐/放繩/推桿/風扇。
# 前提：機體已吸附（本腳本不碰真空閥與推桿，也不驗附著——由操作者確認）。
#
# 🔴 判準看姿態不看回傳值：arm_deploy 壓玻璃時本來就回
#    `ERR DEPLOY: M1 touch_wall did not converge`。門檻沿用 09-02 建立、09-03 十輪 x 2 工具
#    確認的那組：0.55 < theta < 0.75 且 tau > 8.0。
#
# 🔴 姿態一律「輪詢到位移歸零 + 連續兩筆一致」才採用，不用固定秒數。
#    2026-09-04 實測 DEPLOY 490 在壓上後 1 秒內還會鬆弛 1.46 Nm（t=0 讀 12.06 -> 10.60），
#    而 cycle_test.py 的 ③b 是 sleep(1.5) 後只讀一次。單次讀取會把值記錯。
#
# 🔴 任何中止都先 arm_park：頂著玻璃不是「保留現場」，是持續施力，
#    達妙馬達長時間受力會觸發過熱/過流鎖存，那只能斷電解除（09-03 踩過）。
import socket, sys, time, re

HOST, PORT = "127.0.0.1", 5001
ROUNDS  = int(sys.argv[1]) if len(sys.argv) > 1 else 1
# 🔴 [2026-09-04] 第二個參數改成**目標壓力 N·m**（力控），不再是 wall_mm。
#   要跑舊的開迴路路徑就傳 `wall` 當第五個參數（值域自己看：15 vs 520 差很多，
#   送錯不會有語法錯誤——DEPLOY_F 的 theta_max 守衛會擋下「把 520 當 N·m」）。
TARGET  = float(sys.argv[2]) if len(sys.argv) > 2 else 15.0
SLOT    = sys.argv[3] if len(sys.argv) > 3 else "RIGHT"   # RIGHT=滾筒 / LEFT=刮刀
RAIL_CM = int(sys.argv[4]) if len(sys.argv) > 4 else 100
MODE    = sys.argv[5] if len(sys.argv) > 5 else "force"   # force=DEPLOY_F / wall=DEPLOY

TH_LO, TH_HI, TAU_MIN = 0.55, 0.75, 8.0

sock = socket.create_connection((HOST, PORT), timeout=10)
f = sock.makefile("r")

def ask(cmd, timeout, prefixes=("OK", "ERR", "WARN")):
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
        if line and line.startswith(prefixes):
            return line
    return "<timeout>"

def arm():
    """arm_status -> dict, or None."""
    s = ask("arm_status", 20, prefixes=("[M1]",))
    m1 = re.search(r"\[M1\] pos=(-?[\d.]+) vel=(-?[\d.]+) tau=(-?[\d.]+)", s)
    m2 = re.search(r"\[M2\] pos=(-?[\d.]+) vel=(-?[\d.]+) tau=(-?[\d.]+)", s)
    if not m1:
        return None
    d = {"th": float(m1.group(1)), "vel": float(m1.group(2)), "tau": float(m1.group(3))}
    if m2:
        d["m2"] = float(m2.group(1)); d["m2tau"] = float(m2.group(3))
    return d

def settle(max_wait=30.0):
    t0, prev, stable, d = time.time(), None, 0, None
    while time.time() - t0 < max_wait:
        d = arm()
        if d is None:
            time.sleep(1.0); continue
        if prev is not None and abs(d["th"] - prev) < 0.0005 and abs(d["vel"]) < 0.01:
            stable += 1
            if stable >= 2:
                return d
        else:
            stable = 0
        prev = d["th"]
        time.sleep(1.0)
    return d

def cups():
    s = ask("status", 15)
    return [int(x) for x in re.findall(r"p[5-8]=(-?\d+)", s)]

def bail(msg):
    print("\n[BAIL] %s" % msg)
    print("[BAIL] 收手臂中（頂著玻璃不是保留現場）...")
    print("[BAIL] arm_park -> %s" % ask("arm_park", 90))
    sock.close()
    sys.exit(1)

if MODE == "force":
    print("=== arm_cycle: %d 輪 | DEPLOY_F %.1f N·m %s | rail 0->%d->0 ==="
          % (ROUNDS, TARGET, SLOT, RAIL_CM))
    print("    力控：壓力是被控量，判準讀 DEPLOY_F 的回覆（OK/WARN/no_wall/obstacle）\n")
else:
    print("=== arm_cycle: %d 輪 | DEPLOY %d %s（開迴路）| rail 0->%d->0 ==="
          % (ROUNDS, int(TARGET), SLOT, RAIL_CM))
    print("    判準 %.2f < th < %.2f 且 tau > %.1f；姿態輪詢到穩定才採用\n" % (TH_LO, TH_HI, TAU_MIN))
rows = []
for r in range(1, ROUNDS + 1):
    t_round = time.time()
    print("--- 第 %d/%d 輪 ---" % (r, ROUNDS))
    c0 = cups()
    t = time.time()
    if MODE == "force":
        # prefixes 含 WARN：未收斂時前綴是 WARN 不是 OK/ERR。
        rep = ask("arm_deploy_f %.1f %s" % (TARGET, SLOT), 150)
        t_dep = time.time() - t
        if rep.startswith("ERR unknown_cmd"):
            bail("本體無 arm_deploy_f（舊 binary）—— 要跑開迴路請傳第五個參數 wall")
        if "obstacle" in rep:
            bail("疑似障礙物：%s" % rep[:110])
        if "no_wall" in rep:
            bail("找不到牆：%s" % rep[:110])
        if not rep.startswith(("OK", "WARN")):
            bail("arm_deploy_f 失敗：%s" % rep[:110])
        # 🔴 判準讀回覆不讀姿態，但仍**另外**量一次姿態當交叉檢查 ——
        #    回覆裡的 tau 是 motor_api 等過鬆弛後量的，這裡再讀一次是為了驗證
        #    「回覆說的」與「事後獨立量到的」一致。不一致就是有東西在回覆之後動了。
        d = settle()
        if d is None:
            bail("arm_status 讀不到姿態")
        rep_tau = None
        m = re.search(r"\btau=(-?[\d.]+)", rep)
        if m: rep_tau = float(m.group(1))
        flag = ""
        if rep_tau is not None and abs(rep_tau - d["tau"]) > 1.0:
            flag = "  ⚠回覆tau=%+.2f 與複量差 %.2f" % (rep_tau, abs(rep_tau - d["tau"]))
        print("  壓上  %5.1fs  th=%+.4f tau=%+.2f  M2=%+.4f   %s%s"
              % (t_dep, d["th"], d["tau"], d.get("m2", float("nan")),
                 rep[:60], flag))
        if rep.startswith("WARN"):
            print("      🟡 未收斂到 %.1f N·m（掃動照做）" % TARGET)
    else:
        rep = ask("arm_deploy %d %s" % (int(TARGET), SLOT), 90)
        t_dep = time.time() - t
        d = settle()
        if d is None:
            bail("arm_status 讀不到姿態")
        print("  壓上  %5.1fs  th=%+.4f tau=%+.2f  M2=%+.4f   (deploy 回 %s)"
              % (t_dep, d["th"], d["tau"], d.get("m2", float("nan")), rep[:46]))
        if not (TH_LO < d["th"] < TH_HI and d["tau"] > TAU_MIN):
            bail("手臂沒有壓上：th=%.4f tau=%+.2f" % (d["th"], d["tau"]))

    t = time.time()
    x = ask("rail %d" % RAIL_CM, 60)
    t_out = time.time() - t
    if not x.startswith("OK"):
        bail("rail %d 失敗：%s" % (RAIL_CM, x))
    d_out = arm() or {}

    t = time.time()
    x = ask("rail 0", 60)
    t_back = time.time() - t
    if not x.startswith("OK"):
        bail("rail 0 復位失敗：%s" % x)
    d_back = arm() or {}
    print("  滑台  %4.1f / %4.1fs  @%dcm: th=%+.4f M2=%+.4f | 回0: th=%+.4f M2=%+.4f"
          % (t_out, t_back, RAIL_CM, d_out.get("th", float("nan")), d_out.get("m2", float("nan")),
             d_back.get("th", float("nan")), d_back.get("m2", float("nan"))))

    t = time.time()
    x = ask("arm_park", 90)
    t_park = time.time() - t
    if not x.startswith("OK"):
        bail("arm_park 失敗：%s" % x)
    c1 = cups()
    dt = time.time() - t_round
    print("  收回  %5.1fs   全輪 %5.1fs   吸盤 %s -> %s\n" % (t_park, dt, c0, c1))
    rows.append((r, t_dep, d["th"], d["tau"], d.get("m2"), t_out, t_back,
                 d_out.get("th"), d_out.get("m2"), d_back.get("m2"), t_park, dt, c1))

print("=== 總結（%d 輪）===" % len(rows))
print("輪  壓上s   th_壓上   tau_壓上   M2_壓上  滑台出s 回s  th@%-3d  M2@%-3d  M2_回0  收回s  全輪s  吸盤"
      % (RAIL_CM, RAIL_CM))
for x in rows:
    print("%2d  %5.1f  %+.4f  %+7.2f  %+.4f   %4.1f %4.1f  %+.4f  %+.4f  %+.4f  %5.1f %5.1f  %s"
          % (x[0], x[1], x[2], x[3], x[4] if x[4] is not None else float("nan"), x[5], x[6],
             x[7] if x[7] is not None else float("nan"), x[8] if x[8] is not None else float("nan"),
             x[9] if x[9] is not None else float("nan"), x[10], x[11], x[12]))
if len(rows) > 1:
    taus = [x[3] for x in rows]; ths = [x[2] for x in rows]; tps = [x[1] for x in rows]
    print("\n散布: tau %.2f~%.2f (%.2f)  th %.4f~%.4f (%.4f)  壓上耗時 %.1f~%.1fs"
          % (min(taus), max(taus), max(taus)-min(taus), min(ths), max(ths), max(ths)-min(ths),
             min(tps), max(tps)))
sock.close()
