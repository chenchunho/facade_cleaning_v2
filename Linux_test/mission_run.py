#!/usr/bin/env python3
# mission_run.py — 10 round trips (top <-> bottom) with per-trip attitude statistics.
#
# 任務（per user 2026-09-01）：地面↔頂端 10 趟來回，全程本體平衡 roll ±1°。
#
# 為什麼要有這支：先前每個條件只跑一趟，而 2026-09-01 實測顯示**趟與趟之間的變異
# 與被比較的效果一樣大**（30Hz 在無相關程式改動下由 11% 跳到 33%）。20 次橫越
# 本身就是統計樣本 —— 與其再做 n=1 調參，不如直接跑任務並取得分佈。
#
# 安全：每一趟都並行監看，任一條觸發即送 stop 並中止整個任務（保留現場不復位）：
#   - |roll| 超過 ROLL_TRIP
#   - 左右位移差超過 DIFF_TRIP（低於韌體的 length_diff_max_cm）
#   - 張力讀取失效（tension_valid=0）→ 過載保護會靜默失效，必須停
#   - 該趟回傳非 OK
#
# usage: mission_run.py [trips] [top_cm] [bottom_cm] [diff_trip] [roll_trip]

import re, socket, sys, threading, time

CRANE = ("127.0.0.1", 5002)
WROBOT = ("192.168.5.26", 5001)

TRIPS = int(sys.argv[1]) if len(sys.argv) > 1 else 10
# 🔴 [2026-09-03 per user] 座標語意由「繩長」改成「**離底高度**」，與 cycle_test.py 一致。
#   SD76 於當日傍晚在**玻璃最底端（離地最近）**重新歸零 → length_left 0=底、往上為負。
#   實測頂端 length_left = -231 ⇒ 玻璃面高度 231 cm。
# 🔴🔴 改這支特別重要：它是**唯一會自己決定方向**的腳本，而 09-01 就因為方向寫反
#   被使用者在執行前攔下（當時機器在頂端卻要再往上收 229cm）。座標一換而邏輯沒跟上，
#   同一個錯會以「看起來完全合理」的樣子重演。
TOP = int(sys.argv[2]) if len(sys.argv) > 2 else 231     # 頂端高度
BOTTOM = int(sys.argv[3]) if len(sys.argv) > 3 else 0    # 底端高度


def height(length_left):
    """繩長讀值 → 離底高度（cm）。繩越長 → 位置越低 → 高度越小。"""
    return None if length_left is None else -length_left
DIFF_TRIP = float(sys.argv[4]) if len(sys.argv) > 4 else 8.0
ROLL_TRIP = float(sys.argv[5]) if len(sys.argv) > 5 else 5.0
# 🔴 [2026-09-03] 座標改成「離底高度」之後，單程距離是 TOP - BOTTOM（頂在上、值較大）。
# 改漏這一行的症狀是抬頭印出「單程 -231cm / -4.6 m」—— 負距離。
# 它不影響任何動作（方向由 plan_leg 決定），但**會讓人懷疑方向是不是也反了**，
# 而在一支剛改過座標的腳本上，那個懷疑代價很高。
SPAN = TOP - BOTTOM

abort_reason = []


def ask(addr, cmd, timeout=5):
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


def leg(verb, cm, label):
    """One traverse with parallel monitoring. Returns (result, samples)."""
    pre = ask(CRANE, "status", 5)
    bl, br = field(pre, "length_left"), field(pre, "length_right")
    stop_evt = threading.Event()
    samples = []

    def mon():
        while not stop_evt.is_set():
            try:
                cs = ask(CRANE, "status", 3)
                ws = ask(WROBOT, "status", 3)
                L, R = field(cs, "length_left"), field(cs, "length_right")
                tv = field(cs, "tension_valid")
                roll = field(ws, "roll")
                if L is None or R is None:
                    time.sleep(0.3); continue
                diff = abs(abs(L - bl) - abs(R - br))
                samples.append((round(time.time() - t0, 1), L, R, diff, roll))
                if tv is not None and tv < 1:
                    abort_reason.append(f"{label}: tension_valid=0（過載保護會靜默失效）"); break
                if diff > DIFF_TRIP:
                    abort_reason.append(f"{label}: 左右差 {diff:.0f}cm > {DIFF_TRIP:.0f}"); break
                if roll is not None and abs(roll) > ROLL_TRIP:
                    abort_reason.append(f"{label}: roll {roll:.2f}° > {ROLL_TRIP:.1f}"); break
            except Exception as e:
                samples.append((round(time.time() - t0, 1), None, None, None, None))
            time.sleep(0.3)
        if abort_reason:
            try:
                ask(CRANE, "stop", 5)
            except Exception:
                pass

    t0 = time.time()
    th = threading.Thread(target=mon, daemon=True); th.start()
    s = socket.create_connection(CRANE, timeout=10); s.settimeout(180)
    s.sendall(f"{verb} {cm}\n".encode())
    result, buf = None, b""
    try:
        while result is None:
            d = s.recv(4096)
            if not d: break
            buf += d
            for line in buf.decode(errors="replace").splitlines():
                if line.startswith("OK") or line.startswith("ERR"):
                    result = line; break
    except socket.timeout:
        result = "TIMEOUT"
    s.close()
    stop_evt.set(); th.join(timeout=6)
    return result, samples, time.time() - t0


def stats(samples):
    rolls = [s[4] for s in samples if s[4] is not None]
    diffs = [s[3] for s in samples if s[3] is not None]
    if not rolls:
        return None
    out = [r for r in rolls if abs(r) > 1.0]
    return dict(n=len(rolls), mx=max(abs(r) for r in rolls),
                avg=sum(abs(r) for r in rolls) / len(rolls),
                out=len(out), outpct=100.0 * len(out) / len(rolls),
                mdiff=max(diffs) if diffs else 0)


print(f"任務：{TRIPS} 趟來回  頂 {TOP}cm ↔ 底 {BOTTOM}cm（離底高度）  單程 {SPAN}cm  "
      f"共 {TRIPS*2} 次橫越 / {TRIPS*2*SPAN/100:.1f} m")
st = ask(CRANE, "status", 5)
print(f"起始 高度={height(field(st,'length_left')):.0f}cm "
      f"(繩長 L={field(st,'length_left'):.0f} R={field(st,'length_right'):.0f})  "
      f"motion_hz={field(st,'motion_hz'):.0f}  "
      f"balance_source={re.search(r'balance_source=(\w+)', st).group(1)}  "
      f"length_diff_max={field(st,'length_diff_max_cm'):.0f}")
print(f"中止門檻：|roll|>{ROLL_TRIP}° 或 左右差>{DIFF_TRIP}cm 或 tension_valid=0\n")
print(f"{'趟':>3} {'方向':>4} {'秒':>6} {'n':>4} {'avg':>6} {'max':>6} {'出帶%':>7} {'Δmax':>5}  結果")

# 🔴 [2026-09-01] **方向由實測位置推導，不再假設起點。**
# 原本每趟固定「先 retract 後 pay_out」，而機器當時在頂端（L=0）——
# retract 是往上收繩，等於從頂端再往上拉 229cm，會把機器拉進吊機/屋頂結構。
# 使用者在執行前攔下。教訓：**會把機器帶出已定義區間的指令必須被拒絕，
# 而不是靠腳本作者記得順序。**
TOL = 5   # cm，端點容許

def plan_leg(cur):
    """由當前高度推導下一段。回傳 (verb, cm, 目標, 標籤) 或 None（無法判定）。

    🔴 新語意下的方向：
        在頂端（cur ≈ TOP，高度大） → 往下 → pay_out（放繩），高度減少
        在底端（cur ≈ BOTTOM，高度小） → 往上 → retract（收繩），高度增加
    移動量一律用「與目標端點的距離」算，不用固定常數 —— 端點值改了它自動跟上。
    """
    if cur >= TOP - TOL:
        return ("pay_out", cur - BOTTOM, BOTTOM, "下")
    if cur <= BOTTOM + TOL:
        return ("retract", TOP - cur, TOP, "上")
    return None          # 不在任一端點 —— 不猜，交給人

def check_envelope(verb, cm, cur):
    """指令執行後的預期高度必須落在 [BOTTOM, TOP] 內。

    pay_out 讓高度**減少**、retract 讓高度**增加** —— 這兩個符號跟舊版相反，
    是本次座標變更中最容易改漏的一處。
    """
    end = cur - cm if verb == "pay_out" else cur + cm
    if cm <= 0:
        return f"cm={cm} 非正值"
    if end < BOTTOM - TOL or end > TOP + TOL:
        return f"預期終點 {end} cm 超出區間 [{BOTTOM}, {TOP}]"
    return None

allr, allout, alln = [], 0, 0
for t in range(1, TRIPS + 1):
    for _half in (0, 1):
        st_ = ask(CRANE, "status", 5)
        cur = height(field(st_, "length_left"))
        pl = plan_leg(cur)
        if pl is None:
            print(f"\n🔴 中止：目前高度 {cur:.0f} cm 不在任一端點附近"
                  f"（頂 {TOP}±{TOL} / 底 {BOTTOM}±{TOL}），無法判定方向。")
            print("   不猜方向 —— 請先手動移到端點。")
            sys.exit(1)
        verb, cm, tgt, label = pl
        bad = check_envelope(verb, cm, cur)
        if bad:
            print(f"\n🔴 拒絕執行：{verb} {cm}（{bad}）")
            sys.exit(1)
        res, smp, dur = leg(verb, cm, f"第{t}趟{label}")
        s_ = stats(smp)
        if s_:
            allr.append(s_["mx"]); allout += s_["out"]; alln += s_["n"]
            print(f"{t:>3} {label:>4} {dur:>6.1f} {s_['n']:>4} {s_['avg']:>6.2f} "
                  f"{s_['mx']:>6.2f} {s_['outpct']:>6.0f}% {s_['mdiff']:>5.0f}  {res[:40]}")
        else:
            print(f"{t:>3} {label:>4} {dur:>6.1f}    - 無取樣  {res[:40]}")
        if abort_reason or not res.startswith("OK"):
            print(f"\n🔴 中止：{abort_reason[0] if abort_reason else res}")
            print("   現場保留，未自動復位。")
            fin = ask(CRANE, "status", 5)
            print(f"   高度={height(field(fin,'length_left')):.0f}cm "
                  f"(繩長 L={field(fin,'length_left'):.0f} R={field(fin,'length_right'):.0f}) "
                  f"tension_valid={field(fin,'tension_valid'):.0f}")
            sys.exit(1)

print(f"\n=== 任務總計 ===")
print(f"  {TRIPS} 趟來回 / {TRIPS*2} 次橫越 全部完成")
print(f"  取樣合計 {alln} 筆   超出 ±1°: {allout} ({100.0*allout/alln:.1f}%)")
print(f"  各趟最大 |roll| 的最大值 {max(allr):.2f}°   平均 {sum(allr)/len(allr):.2f}°")
