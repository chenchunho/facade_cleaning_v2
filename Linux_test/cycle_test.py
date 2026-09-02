#!/usr/bin/env python3
# cycle_test.py — 機構週期耐久測試（per user 2026-09-01）
#
# 一個週期 = 由頂端向下 5 步 × 40cm（走滿 200cm）+ 50Hz 一口氣拉回頂端。
#
# 每一步（使用者口述順序）：
#   ① 風扇 5%（關）        —— 伸/收推桿期間必須關（關風扇才可抽真空+伸腳，順序本身是安全需求）
#   ② vacuum feet on       —— 開真空閥
#   ③ pusher all extend_raw—— 推出 10cm，**不驗真空度**
#      🔴 為什麼不驗：有些玻璃面有縫隙，吸盤落在縫上本來就吸不住，那是現場條件不是故障。
#         smart_extend_subset_ 會為了找封一路補伸到 ~16cm 並重試 —— 在有縫的面上是徒勞。
#   ④ pusher all retract   —— 已內建「關閥→洩壓→CH6 正壓 500ms→兩段收回」
#   ⑤ 風扇 7%（開）
#   ⑥ delay 1000ms
#   ⑦ crane pay_out 40（30Hz）—— 並行監看
#   ⑧ 靜置 300ms（imu_level 已移除，見下方步驟 ⑧ 的說明）
# 到底後：⑨ 風扇 5% → retract 回頂端（30Hz，見 UP_HZ 的說明）→ motion_hz 寫回 30
#
# 🔴 全程沒有防墜錨點：移動時 4 顆推桿都在 0，靠鋼索承重。這與 do_step_sync_ 同性質，
#    但本測試連密封都不驗 —— 第 ③ 步**不提供任何附著作用**，不能拿來宣稱吸附系統可用。
#
# 🔴 [2026-09-01] **監看與統計一律讀 `raw_x`，不讀 `status` 的 `roll`。**
# `roll` 是**扣掉基準之後**的值（`imu_.x - imu_roll0_`），而 `imu_roll0_` 由 `init` /
# `imu_zero` 在**當下姿態**取樣決定。若 init 時機體本來就歪，那個歪角就被定義成「水平」，
# 之後 roll 一路報 0，中止門檻也跟著偏移同樣的角度 —— 安全門檻不可以建立在會漂的基準上。
# `raw_x` 是 IMU 直接輸出的滾轉角，與基準無關。
#
# usage: cycle_test.py [cycles] [steps_per_cycle] [step_cm] [roll_trip] [diff_trip]

import os, re, socket, sys, threading, time

# 🔴 [2026-09-02] stdout 導向檔案時是**全緩衝**，整輪產出才約 4.7KB
# → 一個 4KB 緩衝區都填不滿，log 會從頭到尾停在 0 bytes，看起來像腳本沒跑。
# 2026-09-02 上午就是這樣：測試明明在跑（pid 在、機器在動），log 卻是空的。
# 這與 runbook §A4 對兩支 C++ 記過的 `stdbuf -oL` 是同一個坑。
# 靠呼叫端記得加 `python3 -u` 不可靠 —— 在這裡設，怎麼叫都有效。
# ⚠️ 中止路徑（bail / cleanup）本來就會在解譯器結束時 flush，所以這不影響「會不會留下紀錄」，
#    影響的是「跑的當下看不看得到」——耐久測試一跑 20 分鐘，這就是全部的可觀測性。
sys.stdout.reconfigure(line_buffering=True)

CRANE  = ("127.0.0.1", 5002)
WROBOT = ("192.168.5.26", 5001)

CYCLES    = int(sys.argv[1]) if len(sys.argv) > 1 else 1
STEPS     = int(sys.argv[2]) if len(sys.argv) > 2 else 5
STEP_CM   = int(sys.argv[3]) if len(sys.argv) > 3 else 40
ROLL_TRIP = float(sys.argv[4]) if len(sys.argv) > 4 else 6.0
DIFF_TRIP = float(sys.argv[5]) if len(sys.argv) > 5 else 8.0

# 🔴 [2026-09-01] 左右差守衛改為「**連續**超標」才中止，不再看瞬時值。
#
# 為什麼：**吊機的 IMU 平衡迴路就是靠製造左右差來修正 roll 的**
# （apply_balance_trim 調的是左右不同的 Hz → 直接產生左右位移差），
# 而本腳本監看的正是同一個量 —— 等於裝了一個會在控制器最用力工作時把它關掉的保護。
#
# 實測分布（36 段）：最小 1 / 中位 2 / p90 5 / 最大 8 cm，>=8cm 只佔 3%。
# 中止那次的瞬時值 9cm，當下 raw_x=4.35° —— 那不是繩子卡住，是控制器在修一個大傾角。
#
# 真正該擋的「一側繩子卡住」，特徵是 Δ **持續擴大且不回頭**；平衡修正則是衝一下就收斂。
# 用**持續性**而非瞬時大小來分辨。門檻本身不動，韌體的 length_diff_max_cm=10 仍是硬底線。
#
# ⚠️ roll 門檻**維持瞬時判定**，刻意不比照辦理：那是機體姿態的安全線，
#    一筆真實的 6° 就該停，沒有「等它持續」的餘裕。
DIFF_PERSIST = 3          # 連續幾筆超標才中止（取樣間隔約 0.3s → 約 1 秒持續）

TOP, BOTTOM = 0, 229
TOL = 5
# 🔴 [2026-09-01 per user] 回程由 50Hz 改 30Hz。
# 原因：50Hz 回程實測左右差瞬間衝到 9cm，而韌體自己的 length_diff_max_cm 是 10
# —— 把腳本門檻放寬沒有意義，韌體會先擋。30Hz 今天多次驗證瞬態差最大 7cm。
# 使用者原本的規格是「上行最多 50Hz」，那是上限不是必須值。
DOWN_HZ, UP_HZ = 30, 30
FAN_ON, FAN_OFF = 7, 5

abort_reason = []

# 🔴 [2026-09-01] 每一段的左右位移差最大值。
# 為什麼要收：10 週期實測在**第 9 趟回程**撞上 9cm 而中止，而韌體自己的
# length_diff_max_cm 是 10 —— 我的門檻 8 與韌體的 10 之間只有 2cm 餘裕。
# 光看「有沒有中止」分不出「9cm 是偶發尖峰」還是「常態就貼著上限」，
# 而這兩者的處置完全相反（前者調門檻、後者要修吊機的啟停時序）。
# 所以每一段都記，跑完出分布。
all_diff = []
all_nearmiss = []


def ask(addr, cmd, timeout):
    """送一行指令，讀到 OK/ERR 為止。回傳該行（或 TIMEOUT/EXC:...）。"""
    try:
        s = socket.create_connection(addr, timeout=10)
        s.settimeout(timeout)
        s.sendall((cmd + "\n").encode())
        buf = b""
        while True:
            d = s.recv(4096)
            if not d:
                break
            buf += d
            for line in buf.decode(errors="replace").splitlines():
                if line.startswith("OK") or line.startswith("ERR"):
                    s.close()
                    return line
        s.close()
        return "EOF"
    except socket.timeout:
        return "TIMEOUT"
    except Exception as e:
        return "EXC:%s" % e


def field(line, key):
    m = re.search(r"\b%s=(-?[\d.]+)" % key, line)
    return float(m.group(1)) if m else None


def fan(pct):
    return ask(WROBOT, "pwm set 1 50 65535 %d" % pct, 15)


def emergency():
    """任一中止條件觸發後的收手。兩邊都送，不管回應。"""
    for a, c in ((WROBOT, "emergency_stop"), (CRANE, "stop")):
        try:
            ask(a, c, 10)
        except Exception:
            pass


def monitored_crane_move(verb, cm, label, timeout):
    """吊機移動 + 並行監看。回傳 (結果行, roll 統計, 秒數)。"""
    pre = ask(CRANE, "status", 10)
    bl, br = field(pre, "length_left"), field(pre, "length_right")
    if bl is None or br is None:
        return "ERR status_unreadable", None, 0.0

    stop_evt = threading.Event()
    rolls, maxdiff = [], [0.0]
    streak, nearmiss = [0], [0]      # 連續超標筆數 / 超標但自行回復的次數

    def mon():
        while not stop_evt.is_set():
            try:
                cs = ask(CRANE, "status", 5)
                ws = ask(WROBOT, "status", 5)
                L, R = field(cs, "length_left"), field(cs, "length_right")
                tv   = field(cs, "tension_valid")
                roll = field(ws, "raw_x")   # 🔴 讀真實傾角，見檔頭說明
                if L is None or R is None:
                    time.sleep(0.3); continue
                diff = abs(abs(L - bl) - abs(R - br))
                maxdiff[0] = max(maxdiff[0], diff)
                if roll is not None:
                    rolls.append(roll)
                if tv is not None and tv < 1:
                    abort_reason.append("%s: tension_valid=0（過載保護會靜默失效）" % label); break
                if diff > DIFF_TRIP:
                    streak[0] += 1
                    if streak[0] >= DIFF_PERSIST:
                        abort_reason.append(
                            "%s: 左右差連續 %d 筆超過 %.0fcm（最後 %.0fcm）—— 持續擴大，非平衡修正"
                            % (label, streak[0], DIFF_TRIP, diff)); break
                else:
                    if streak[0] > 0:
                        nearmiss[0] += 1     # 超標過但自行回復 ＝ 平衡迴路在工作
                    streak[0] = 0
                if roll is not None and abs(roll) > ROLL_TRIP:
                    abort_reason.append("%s: roll %.2f° > %.1f" % (label, roll, ROLL_TRIP)); break
            except Exception:
                pass
            time.sleep(0.3)
        if abort_reason:
            emergency()

    t0 = time.time()
    th = threading.Thread(target=mon, daemon=True); th.start()
    res = ask(CRANE, "%s %d" % (verb, cm), timeout)
    stop_evt.set(); th.join(timeout=6)
    dur = time.time() - t0

    st = None
    if rolls:
        out = [r for r in rolls if abs(r) > 1.0]
        st = dict(n=len(rolls), avg=sum(abs(r) for r in rolls) / len(rolls),
                  mx=max(abs(r) for r in rolls), outpct=100.0 * len(out) / len(rolls),
                  mdiff=maxdiff[0], nearmiss=nearmiss[0])
    return res, st, dur


def _diff_summary():
    """左右位移差的分布 —— 判斷 9cm 是尖峰還是常態貼上限。"""
    if not all_diff:
        return
    vals = sorted(d for _, d in all_diff)
    n = len(vals)
    def pct(p):
        return vals[min(n - 1, int(round((p / 100.0) * (n - 1))))]
    print("\n=== 左右位移差分布（%d 段）===" % n)
    print("  最小 %.0f  中位 %.0f  p90 %.0f  最大 %.0f cm" % (vals[0], pct(50), pct(90), vals[-1]))
    for th in (6, 7, 8, 9, 10):
        c = sum(1 for v in vals if v >= th)
        print("  >=%2dcm: %3d/%d (%.0f%%)%s" % (th, c, n, 100.0 * c / n,
              "   ← 我的中止門檻" if th == int(DIFF_TRIP) else
              "   ← 韌體 length_diff_max_cm" if th == 10 else ""))
    worst = sorted(all_diff, key=lambda x: -x[1])[:5]
    print("  最大的 5 段: " + "  ".join("%s=%.0f" % (k, v) for k, v in worst))
    if all_nearmiss:
        print("  超標後自行回復（未達連續 %d 筆）: %d 次 —— 平衡迴路在工作，不是故障"
              % (DIFF_PERSIST, sum(all_nearmiss)))


def bail(msg):
    print("\n🔴 中止：%s" % msg)
    print("   現場保留，未自動復位。")
    _diff_summary()
    cleanup()
    fin = ask(CRANE, "status", 10)
    print("   L=%s R=%s tension_valid=%s"
          % (field(fin, "length_left"), field(fin, "length_right"), field(fin, "tension_valid")))
    sys.exit(1)


def cleanup():
    """🔴 一定要跑：風扇關 + motion_hz 寫回下行速度。
    中途中止若把 motion_hz 留在 50，下一個人下 pay_out 就是 50Hz 下行 —— 超出使用者定的上限。"""
    print("   [收尾] 風扇 %d%% / motion_hz→%d : %s / %s"
          % (FAN_OFF, DOWN_HZ, fan(FAN_OFF), ask(CRANE, "set_motion_hz %d" % DOWN_HZ, 15)))


print("週期測試：%d 週期 × (下行 %d 步 × %dcm = %dcm @%dHz) + 拉回 @%dHz"
      % (CYCLES, STEPS, STEP_CM, STEPS * STEP_CM, DOWN_HZ, UP_HZ))
print("中止門檻：|roll|>%.1f°（瞬時） / 左右差>%.0fcm 連續 %d 筆 / tension_valid=0 / 任一指令非 OK\n"
      % (ROLL_TRIP, DIFF_TRIP, DIFF_PERSIST))

st0 = ask(CRANE, "status", 10)
L0 = field(st0, "length_left")
if L0 is None or abs(L0 - TOP) > TOL:
    print("🔴 起點不在頂端（L=%s，需 %d±%d）——不猜，請先手動移到頂端。" % (L0, TOP, TOL))
    sys.exit(1)
ws0 = ask(WROBOT, "status", 10)
if "state=ready" not in ws0 and "state=idle" not in ws0:
    print("🔴 本體狀態非 ready/idle：%s" % ws0[:90]); sys.exit(1)
print("起點 L=%.0f  本體 %s\n" % (L0, re.search(r"state=\w+", ws0).group(0)))

# 🔴 [2026-09-02] 真空幫浦在不在，開跑前必須實際回讀。
#
# 為什麼補這一條：2026-09-02 上午整輪 10 週期是在**沒有真空源**的情況下跑完的。
# 開幫浦的是 `init` 這支 TCP 指令（cmd_init 送 controlRelay(CH_PUMP, true)），
# **不是**程式啟動時的驅動 init —— 後者底下那五行 relay 設定是註解掉的。
# 本腳本不送 init，而 `state=idle` 在幫浦沒開時照樣成立 ⇒ 既有的兩道前置檢查
# （起點在頂端 / 本體 ready-idle）**沒有一道碰得到這件事**。
# 續十已經寫過「程式重啟後所有繼電器都是 OFF」，但那是給人看的紀錄，不是給腳本看的守衛。
#
# 📌 判準與「起點不在頂端」同一套：不猜、不自動補送 init（那會在不知情的狀態下
#    啟動幫浦），只擋下來並印出該送什麼。
# 📌 通道編號**從 relay_status 自己的 names 欄推導**，不寫死 2 —— CH3 那次的教訓就是
#    通道對應會變，而寫死的數字不會跟著變。
ALLOW_NO_PUMP = os.environ.get("ALLOW_NO_PUMP") == "1"
rs = ask(WROBOT, "relay_status", 15)
if not rs.startswith("OK"):
    print("🔴 讀不到繼電器狀態：%s" % rs[:90]); sys.exit(1)
_st_part, _, _names_part = rs.partition("|")
_m = re.search(r"ch(\d+)=pumpA", _names_part)
if not _m:
    print("🔴 relay_status 沒有 pumpA 欄位，無法確認真空源：%s" % rs[:120]); sys.exit(1)
_pump_ch = _m.group(1)
_pump_on = re.search(r"\bch%s=1\b" % _pump_ch, _st_part) is not None
if not _pump_on:
    if not ALLOW_NO_PUMP:
        print("🔴 真空幫浦 A 組（ch%s）是 OFF —— 沒有真空源，吸盤全程不會吸住。" % _pump_ch)
        print("   先送 `init` 給本體（它會開幫浦並印 [init] PQW relays → pump ON），或")
        print("   確定要跑無真空的對照組就用 ALLOW_NO_PUMP=1 重跑（這會記進標題列）。")
        sys.exit(1)
    print("⚠️ 【無真空對照組】幫浦 A 組（ch%s）是 OFF，經 ALLOW_NO_PUMP=1 明示放行。" % _pump_ch)
    print("   本輪的壓力欄與吸附行為不可與有真空的輪次比較。\n")
else:
    print("真空幫浦 A 組（ch%s）ON ✅\n" % _pump_ch)

ask(CRANE, "set_motion_hz %d" % DOWN_HZ, 15)

try:
    for cyc in range(1, CYCLES + 1):
        print("═══ 週期 %d/%d ═══" % (cyc, CYCLES))
        print("%3s %6s %6s %28s %7s %7s %6s %5s %7s"
              % ("步", "伸出s", "收回s", "四顆壓力 kPa", "移動s", "roll均", "出帶%", "Δmax", "停後roll"))
        for i in range(1, STEPS + 1):
            cur = field(ask(CRANE, "status", 10), "length_left")
            if cur is None:
                bail("讀不到吊機位置")
            end = cur + STEP_CM
            if end > BOTTOM + TOL:
                bail("預期終點 %d 超出區間 [%d, %d]" % (end, TOP, BOTTOM))

            r = fan(FAN_OFF)                                   # ①
            if not r.startswith("OK"): bail("風扇關閉失敗：%s" % r)
            r = ask(WROBOT, "vacuum feet on", 20)              # ②
            if not r.startswith("OK"): bail("開真空閥失敗：%s" % r)

            t = time.time()                                     # ③
            r = ask(WROBOT, "pusher all extend_raw", 90)
            t_ext = time.time() - t
            if not r.startswith("OK"): bail("extend_raw 失敗：%s" % r)
            ps = ask(WROBOT, "status", 15)
            pr = [field(ps, "p%d" % n) for n in (5, 6, 7, 8)]

            t = time.time()                                     # ④
            r = ask(WROBOT, "pusher all retract", 90)
            t_ret = time.time() - t
            if not r.startswith("OK"): bail("retract 失敗：%s" % r)

            r = fan(FAN_ON)                                     # ⑤
            if not r.startswith("OK"): bail("風扇開啟失敗：%s" % r)
            time.sleep(1.0)                                     # ⑥

            res, stt, dur = monitored_crane_move("pay_out", STEP_CM,        # ⑦
                                                 "週期%d步%d" % (cyc, i), 180)
            if abort_reason: bail(abort_reason[0])
            if not res.startswith("OK"): bail("pay_out 失敗：%s" % res)

            # ⑧ 靜置後量一次「停下來之後的 roll」。
            # 🔴 [2026-09-01] **imu_level 已從週期中移除**（per user）。原因是實測第 5 步
            # 它自己發散：pass0 +2.69° → pass1 -5.29° → pass2 +6.39°，每輪反號且振幅遞增，
            # 最後留下 -6.53° 並讓回程一啟動就撞上中止門檻。根因是靜置 800ms 不足以讓
            # 繩吊機體停止擺盪，讀到的是擺盪相位而非真實傾角。
            # 韌體已加發散守衛（|roll| 沒變小就住手），但那要單獨驗證，不綁在耐久測試裡。
            # 移動中的姿態本來就由吊機端的 IMU 平衡迴路負責（第 1~4 步 roll 均 0.6~1.1°
            # 就是它的成績），步後這支 cm 級粗調不是必要的。
            time.sleep(0.3)
            ra = field(ask(WROBOT, "status", 15), "raw_x")
            rb = ra

            pstr = "/".join("%.0f" % (p if p is not None else 0) for p in pr)
            if stt:
                all_diff.append(("週期%d步%d" % (cyc, i), stt["mdiff"]))
                all_nearmiss.append(stt["nearmiss"])
            print("%3d %6.1f %6.1f %28s %7.1f %7.2f %6.0f%% %5.0f %7s"
                  % (i, t_ext, t_ret, pstr, dur,
                     stt["avg"] if stt else -1, stt["outpct"] if stt else -1,
                     stt["mdiff"] if stt else -1,
                     ("%+.2f" % ra) if ra is not None else "-"))

        # ⑨ 拉回頂端
        cur = field(ask(CRANE, "status", 10), "length_left")
        if cur is None: bail("讀不到吊機位置（回程前）")
        if cur - TOP < 1: bail("回程距離異常：L=%.0f" % cur)
        r = fan(FAN_OFF)
        if not r.startswith("OK"): bail("回程前關風扇失敗：%s" % r)
        r = ask(CRANE, "set_motion_hz %d" % UP_HZ, 15)
        if not r.startswith("OK"): bail("設定 %dHz 失敗：%s" % (UP_HZ, r))
        res, stt, dur = monitored_crane_move("retract", int(cur - TOP),
                                             "週期%d回程" % cyc, 300)
        ask(CRANE, "set_motion_hz %d" % DOWN_HZ, 15)   # 立刻寫回，不等收尾
        if abort_reason: bail(abort_reason[0])
        if not res.startswith("OK"): bail("回程 retract 失敗：%s" % res)
        fin = field(ask(CRANE, "status", 10), "length_left")
        if stt:
            all_diff.append(("週期%d回程" % cyc, stt["mdiff"]))
            all_nearmiss.append(stt["nearmiss"])
        print("  回程 %.0fcm @%dHz  %.1fs  roll均 %.2f  出帶 %.0f%%  Δmax %.0f  → L=%.0f\n"
              % (cur - TOP, UP_HZ, dur, stt["avg"] if stt else -1,
                 stt["outpct"] if stt else -1, stt["mdiff"] if stt else -1,
                 fin if fin is not None else -1))
finally:
    cleanup()

print("=== %d 個週期全部完成 ===" % CYCLES)
_diff_summary()
