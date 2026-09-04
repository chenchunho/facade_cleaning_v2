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
# [2026-09-04 per user] 支援多工具：逗號分隔，一輪內依序各跑一次完整壓上+掃動+收回。
# 例：RIGHT,LEFT = 先滾筒 0->100->0，再換刮刀 0->100->0。
TOOLS   = [t.strip().upper() for t in (sys.argv[3] if len(sys.argv) > 3 else "RIGHT").split(",") if t.strip()]
SLOT    = TOOLS[0]
RAIL_CM = int(sys.argv[4]) if len(sys.argv) > 4 else 100
MODE    = sys.argv[5] if len(sys.argv) > 5 else "force"   # force=DEPLOY_F / wall=DEPLOY

TH_LO, TH_HI, TAU_MIN = 0.55, 0.75, 8.0

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
    a = _seg_kv(s, "M1")
    b = _seg_kv(s, "M2")
    if not a or "pos" not in a:
        return None
    d = {"th": a["pos"], "vel": a.get("vel", 0.0), "tau": a.get("tau", 0.0), "en": a.get("en")}
    if b and "pos" in b:
        d["m2"] = b["pos"]; d["m2tau"] = b.get("tau"); d["m2en"] = b.get("en")
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
    print("=== arm_cycle: %d 輪 x 工具 %s | DEPLOY_F %.1f N·m | rail 0->%d->0 ==="
          % (ROUNDS, "+".join(TOOLS), TARGET, RAIL_CM))
    print("    力控：壓力是被控量，判準讀 DEPLOY_F 的回覆（OK/WARN/no_wall/obstacle）\n")
else:
    print("=== arm_cycle: %d 輪 x 工具 %s | DEPLOY %d（開迴路）| rail 0->%d->0 ==="
          % (ROUNDS, "+".join(TOOLS), int(TARGET), RAIL_CM))
    print("    判準 %.2f < th < %.2f 且 tau > %.1f；姿態輪詢到穩定才採用\n" % (TH_LO, TH_HI, TAU_MIN))
rows = []
for r in range(1, ROUNDS + 1):
    t_round = time.time()
    print("--- 第 %d/%d 輪 ---" % (r, ROUNDS))
    c0 = cups()
    for slot in TOOLS:
        t = time.time()
        if MODE == "force":
            rep = ask("arm_deploy_f %.1f %s" % (TARGET, slot), 150)
            t_dep = time.time() - t
            if rep.startswith("ERR unknown_cmd"):
                bail("本體無 arm_deploy_f（舊 binary）—— 要跑開迴路請傳第五個參數 wall")
            if "obstacle" in rep:
                bail("[%s] 疑似障礙物：%s" % (slot, rep[:110]))
            if "no_wall" in rep:
                bail("[%s] 找不到牆：%s" % (slot, rep[:110]))
            if not rep.startswith(("OK", "WARN")):
                bail("[%s] arm_deploy_f 失敗：%s" % (slot, rep[:110]))
            # 🔴 [2026-09-04] 力控模式**不再輪詢到穩定**，改單次讀取。
            #   motor_api 內部已經等過鬆弛（迭代 1500ms + 收尾）並回報穩定後的 tau，
            #   這裡再輪詢到穩定是**把同一件事量第二次**，實測固定多花 2.00 秒
            #   （3 次輪詢 x 1s），而且全部發生在手臂已經停住之後。
            #   交叉比對的目的（驗證「回覆說的」與「獨立量到的」一致）單次讀取就達成。
            d = arm()
            if d is None:
                bail("[%s] arm_status 讀不到姿態" % slot)
            rep_tau = None
            m = re.search(r"\btau=(-?[\d.]+)", rep)
            if m: rep_tau = float(m.group(1))
            flag = ""
            if rep_tau is not None and abs(rep_tau - d["tau"]) > 1.0:
                flag = "  ⚠回覆tau=%+.2f 與複量差 %.2f" % (rep_tau, abs(rep_tau - d["tau"]))
            print("  [%-5s] 壓上 %5.1fs  th=%+.4f tau=%+.2f  M2=%+.4f%s"
                  % (slot, t_dep, d["th"], d["tau"], d.get("m2", float("nan")), flag))
            if rep.startswith("WARN"):
                print("      🟡 未收斂到 %.1f N·m（掃動照做）" % TARGET)
        else:
            rep = ask("arm_deploy %d %s" % (int(TARGET), slot), 90)
            t_dep = time.time() - t
            d = settle()
            if d is None:
                bail("[%s] arm_status 讀不到姿態" % slot)
            print("  [%-5s] 壓上 %5.1fs  th=%+.4f tau=%+.2f  M2=%+.4f  (%s)"
                  % (slot, t_dep, d["th"], d["tau"], d.get("m2", float("nan")), rep[:40]))
            if not (TH_LO < d["th"] < TH_HI and d["tau"] > TAU_MIN):
                bail("[%s] 手臂沒有壓上：th=%.4f tau=%+.2f" % (slot, d["th"], d["tau"]))

        m2_press = d.get("m2")
        t = time.time()
        x = ask("rail %d" % RAIL_CM, 60)
        t_out = time.time() - t
        if not x.startswith("OK"):
            bail("[%s] rail %d 失敗：%s" % (slot, RAIL_CM, x))
        d_out = arm() or {}
        t = time.time()
        x = ask("rail 0", 60)
        t_back = time.time() - t
        if not x.startswith("OK"):
            bail("[%s] rail 0 復位失敗：%s" % (slot, x))
        d_back = arm() or {}
        m2_out  = d_out.get("m2")
        m2_back = d_back.get("m2")
        # 🔴 淨扭轉 = 掃完回到 rail 0 時，M2 相對壓上當下偏了多少。
        #   09-03 記過「刮刀每次收在 -1.32~-1.34、已越過下界 -1.05」——那是被作業本身
        #   扭出去的，不是有人手轉。所以這欄要逐輪盯著，特別是刮刀。
        net = (m2_back - m2_press) if (m2_back is not None and m2_press is not None) else float("nan")
        print("           滑台 %4.1f / %4.1fs  @%dcm: th=%+.4f M2=%+.4f | 回0: th=%+.4f M2=%+.4f  淨扭轉=%+.4f (%.1f°)"
              % (t_out, t_back, RAIL_CM, d_out.get("th", float("nan")), m2_out if m2_out is not None else float("nan"),
                 d_back.get("th", float("nan")), m2_back if m2_back is not None else float("nan"),
                 net, net * 57.2958))
        t = time.time()
        x = ask("arm_park", 90)
        t_park = time.time() - t
        if not x.startswith("OK"):
            bail("[%s] arm_park 失敗：%s" % (slot, x))
        rows.append((r, slot, t_dep, d["th"], d["tau"], m2_press, t_out, t_back,
                     d_out.get("th"), m2_out, m2_back, net, t_park))
    c1 = cups()
    print("           全輪 %5.1fs   吸盤 %s -> %s\n" % (time.time() - t_round, c0, c1))

print("=== 總結（%d 輪 x %d 工具）===" % (ROUNDS, len(TOOLS)))
print("輪 工具   壓上s   th_壓上   tau_壓上   M2_壓上   出s  回s   th@%-3d   M2@%-3d   M2_回0   淨扭轉  收回s"
      % (RAIL_CM, RAIL_CM))
def _f(v):
    return float("nan") if v is None else v
for x in rows:
    print("%2d %-5s %5.1f  %+.4f  %+7.2f  %+.4f  %4.1f %4.1f  %+.4f  %+.4f  %+.4f  %+.4f  %5.1f"
          % (x[0], x[1], x[2], x[3], x[4], _f(x[5]), x[6], x[7],
             _f(x[8]), _f(x[9]), _f(x[10]), _f(x[11]), x[12]))
for slot in TOOLS:
    sub = [x for x in rows if x[1] == slot]
    if len(sub) < 1: continue
    taus = [x[4] for x in sub]; ths = [x[3] for x in sub]; nets = [x[11] for x in sub]
    print("\n[%s] n=%d  tau %.2f~%.2f (%.2f)  th %.4f~%.4f (%.4f)  淨扭轉 %+.4f~%+.4f rad (%.1f~%.1f°)"
          % (slot, len(sub), min(taus), max(taus), max(taus)-min(taus),
             min(ths), max(ths), max(ths)-min(ths),
             min(nets), max(nets), min(nets)*57.2958, max(nets)*57.2958))
sock.close()
