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
#   ③b 清潔動作      —— [2026-09-03 per user] 壓上(滾筒) → 滑台 0→100→0 → 收手臂。
#      推桿仍伸出、風扇仍關。手臂走本體的 arm_deploy_f/arm_park 代轉。
#      🔴 [2026-09-04 per user] 壓上改用 **DEPLOY_F（力控）**：目標壓力 15 N·m 是被控量，
#         不再由 ARM_WALL_MM 開迴路推。判準由「事後讀姿態」改成「讀 DEPLOY_F 的回覆」。
#         四種回覆三種行為：OK 正常掃動 / WARN 掃動照做並計數 /
#         no_wall 跳過掃動並計數續行 / obstacle 中止。詳見下方 ③b 處。
#      實測每步約 +14 秒（10 週期 × 5 步 ≈ 多 12 分鐘）；DEPLOY_F 比 DEPLOY 慢約 7 秒
#         （暖啟動 15s vs 8s），10 週期 50 步再多約 6 分鐘。
#      🔴 若 ③a 判定「一顆都沒吸到」，本步**跳過**（無附著時掃動只會讓機體擺盪），
#         且整輪不中止 —— 改為計數並在總結報出。理由見 ③a 處的註解。
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
# 🔴 [2026-09-09] 位址改為可由環境變數覆蓋，**預設維持 WiFi 不變**。
#
# 為什麼不直接寫死 192.168.1.100：切到有線是 per user 已拍板的方向，但**隧道的
# 電氣干擾還沒處置**（VFD 一運轉掉 90 趴封包）。現在寫死，等於讓任何人在電氣修好
# 之前跑這支腳本就直接撞牆，而失敗的樣子會像是機構問題。
#
# 📌 這也是 2026-09-08 `m1`/`m2` 的教訓：位址要**能被表達**，不要靠改常數 ——
#    當時「強制走 WiFi」正好是那個機制唯一表達不出來的意圖。
#
#   FCV_WROBOT_HOST=192.168.1.100 python3 cycle_test.py ...   # 走有線
#   （不設）                                                # 走 WiFi，與今天相同
WROBOT_HOST = os.environ.get("FCV_WROBOT_HOST", "192.168.5.26")
WROBOT_PORT = int(os.environ.get("FCV_WROBOT_PORT", "5001"))
WROBOT = (WROBOT_HOST, WROBOT_PORT)

CYCLES    = int(sys.argv[1]) if len(sys.argv) > 1 else 1
STEPS     = int(sys.argv[2]) if len(sys.argv) > 2 else 5
STEP_CM   = int(sys.argv[3]) if len(sys.argv) > 3 else 40
VAC_OK_KPA = -50        # [2026-09-03 per user] 密封判準：至少一顆到此值
VAC_WAIT_S = 10.0       # 等真空建立的上限秒數（超過即視為完全沒附著）
# [2026-09-09] 可由環境變數覆蓋，**預設維持 09-03 的 100**。
#
# ⚠️ 沿革（留著是因為這是一次「假說被證偽」的完整紀錄，不要當成雜訊刪掉）：
#   本日一度由 100 降到 50，理由是我把每步的 `停後roll +3~4°` 歸因於「滑台橫走把機體推歪」。
#   🔴 **那個歸因是錯的**，被兩件事同時證偽：
#     ① per user 指出：**滑台橫走時吸盤是吸著的** —— 機體錨定在玻璃上，滑台移動不會讓它擺。
#        而 `停後roll` 是在下降 40cm **之後**量的，那時推桿已收回、機體懸空 ⇒ 量的是自由懸吊姿態。
#     ② 實驗：`RAIL_CM` 減半（滑台耗時 6.3s→4.4s，確實變快），左右差 中位/p90/max
#        由 4/9/10 → 3/8/**11**，**沒改善、最大值更糟**；`停後roll` 仍 +3°。
#   ✅ 真因是 `fine_adjust_level_diff_cm` 被設成 0（＝「水平就是繩長齊平」），
#      而本機水平時 L−R ≈ 6。**每次計米器歸零後都必須重量這個值**（見吊機 main.cpp:398 的註解）。
#   📌 通則：把一個現象歸因給某個步驟之前，先確認**沒有那個步驟時它不出現**。
#
#   FCV_RAIL_CM=50 python3 cycle_test.py ...   # 要縮短滑台行程用這個
RAIL_CM   = int(os.environ.get("FCV_RAIL_CM", "100"))
ARM_SLOT  = 'RIGHT'     # 固定滾筒（per user）。LEFT=刮刀 / CENTER
ARM_WALL_MM = 520       # DEPLOY 的假設牆距（僅在退回舊路徑時使用，見 ARM_TARGET_NM）
# [2026-09-04 per user] 目標壓力 15 N·m，現場目視定案。DEPLOY_F 用它，不用 ARM_WALL_MM。
ARM_TARGET_NM = 15.0
# 自動偵測：本體若還是舊 binary（沒有 arm_deploy_f 代轉）會回 `ERR unknown_cmd`，
# 第一次遇到就整輪退回 arm_deploy 舊路徑並大聲說一次。不必手動切旗標。
ARM_FORCE_MODE = [True]
# 🔴 [2026-09-03] 09-03 實測 tau 一律 14.0~14.4，而 09-02 同動作是 11.1~11.8（高 25%），
#    且**重複性完美**（θ 散布 0.0012 rad、tau 0.10 Nm）⇒ 是固定的幾何偏移，不是機構鬆動。
#    已排除：牆面（per user 相同）、ZDT 位置、滑台零點、M1 零點（09-03 重新校正過）。
#    最可能是機體到玻璃的實際距離與 ARM_WALL_MM 不符 —— 一次 `DEPLOY 505` 就能驗。
# ✅ [2026-09-04] **驗過了，假說成立**：同高度 520/505/490 → tau 15.29/12.75/10.60
#    （0.156 N·m/mm）。⇒ 那 25% 是幾何不是磨損，且牆距隨高度變。
#    📌 但結論不是「把 ARM_WALL_MM 調小」—— per user 現場目視後把 **15 N·m 定為目標壓力**，
#    所以本高度的 520 剛好是對的。真正的修法是 DEPLOY_F（壓力被控），見上方 ARM_TARGET_NM。
#    ⚠️ ARM_WALL_MM 現在只在退回舊路徑時才用得到。
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
# ⚠️ ~~roll 門檻維持瞬時判定，刻意不比照辦理~~ —— 🔴 [2026-09-09] **這句已被它自己下方的
#    程式碼推翻**：09-04 就加了 `ROLL_PERSIST = 3`，roll 早就不是瞬時判定了。
#    這是「結論改了、由它產生的敘述沒跟著改」，與 2026-09-08 記的那三條假待辦同型。
#    保留原文只為留下沿革，**判準以下方 ROLL_PERSIST 為準**。
DIFF_PERSIST = 3          # 連續幾筆超標才中止（取樣間隔約 0.3s → 約 1 秒持續）
# [2026-09-04] roll 的持續筆數。取 3 與 DIFF_PERSIST 一致（約 1 秒）：
#   實測尖峰是**單筆**的（−6.65 之後下一筆就回到 −3.35），1 秒足以區分
#   「擺過去又回來」與「真的歪著不動」。要更保守就調大，但別調回 1 ——
#   那等於退回瞬時判定。
ROLL_PERSIST = 3
# [2026-09-04 per user]「這個修正之後也要做在腳本裡面，救得回來就繼續腳本，不行才停住」
ROLL_RECOVER_OK   = 3.0   # 修正後 |roll| 低於此值視為救回來（ROLL_TRIP 的一半）
ROLL_RECOVER_TRY  = 3     # roll_correct 最多迭代幾次
ROLL_RECOVER_MAXD = 4     # 單次 roll_correct 的 delta 上限（cm），避免一次動太多
# [2026-09-09] 判定「這次修正有沒有效」的最小改善量（度）。
# 小於它就視為沒改善 —— 避免因量測雜訊而以為還在進步、一直動繩。
# 0.3° 的依據：本日靜止連測 3 次的重複性優於 0.02°，量測雜訊遠小於它；
# 而一次 1cm 的修正實測會改變 3~4°，所以 0.3 不會誤殺一次有效的修正。
ROLL_RECOVER_MIN_GAIN = 0.3

# 🔴 [2026-09-03 per user 第二次重新定義] 座標語意由「繩長」改成「**離底高度**」。
#
# 沿革（兩次歸零都在同一天，不要弄混）：
#   ① 早上：SD76 在**玻璃最高點**歸零 → length_left 0=頂、223=底（繩長語意）
#   ② 傍晚：SD76 改在**玻璃最底端（離地最近）**歸零 → 0=底，往上為**負**
#      實測頂端 length_left = **-231** ⇒ 玻璃面高度 231 cm
#
# 🔴🔴 ② 之後若沿用舊程式碼會出事：`L=0` 在新零點是**底端**，而舊的起點檢查
#      `abs(L0 - TOP) > TOL`（TOP=0）會把它讀成「在頂端」→ 通過 → 接著往下放繩 200cm，
#      而區間守衛比的是 BOTTOM=223 也擋不住。**從最底端再往下 200cm。**
#      這與 mission_run.py 那次「方向寫反、被使用者在執行前攔下」是同一類錯誤。
#
# 因此本檔一律以 height() 換算後的「離底高度」運算，**不直接用 length_left**：
#   height = -length_left     （繩越長 → 位置越低 → 高度越小）
#   底端 height = 0 ／ 頂端 height = TOP
# 下行仍是 pay_out（繩變長、height 減少）；回程仍是 retract（繩變短、height 增加）。
# 📌 SD76 量的本來就是繩長，這個換算刻意只做在腳本裡 —— 不去動韌體回報的物理量。
# 🔴 [2026-09-09] TOP 改為可由環境變數覆蓋，**預設維持 231 不變**。
#
# 為什麼不直接改成新測值：跨距**每次來回都會漂**（本日實測 223→226→227→225→223，
# 單趟來回約 2cm，對應 09-01 記的「單側收繩固定過衝約 1cm」）。
# 寫死任何一個數字都只是把過期時間往後推一次，而 231 這個值帶著 09-03 的量測脈絡，
# 改掉會讓下次有人以為它從來不存在。
#
#   FCV_TOP_CM=223 python3 cycle_test.py 10 5 40
#
# ⚠️ 開跑前實測一次再帶進來 —— 起點檢查是 abs(L0-TOP)>TOL(5)，漂超過 5cm 就會拒絕啟動。
# 🔴 [2026-09-09 第三次重新定義] 改回**頂端歸零**（＝ runbook 的生產標準流程）。
#
# 沿革（三次，不要弄混；每次都改變 length_left 的符號意義）：
#   ① 09-03 早上：玻璃最高點歸零 → 0=頂、+223=底（繩長語意）
#   ② 09-03 傍晚：玻璃最底端歸零 → 0=底、往上為**負**（本檔原本的 height=-L 為此而寫）
#   ③ 09-09 傍晚：`zero_meters ground`（玻璃最底點）→ 升頂 → `zero_meters top`
#      ⇒ **回到 ①**：0=頂、+256=底，且 `home_ground_cm=256` 被寫進吊機
#
# 🔴🔴 ② 的 `height = -length_left` 套到 ③ 會整個反號。**但這不是靠小心避免的**——
#      本檔改成從吊機的 `home_ground_cm` 自行判定，並把判定結果印出來讓人看見：
#        home_ground_cm > 0  ⇒ 頂端歸零，height = home_ground_cm - L
#        home_ground_cm == 0 ⇒ 底端歸零，height = -L                （② 的舊行為，逐位元不變）
#      `zero_meters top` 是唯一會寫 home_ground_cm 的指令，所以它 >0 就代表頂端歸零過。
#
# ⚠️ `home_ground_cm` **不持久化**，吊機重啟即回 0 ⇒ 會靜默退回 ② 的慣例。
#    擋這件事的是既有的起點檢查 `abs(L0-TOP)>TOL`：慣例錯的話 L0 會差整整一個跨距，
#    必定拒絕啟動。**失效模式是「拒跑」，不是「跑錯方向」。**
#    可用 FCV_ZERO_AT=top|bottom 明確指定，蓋過自動判定。
#
#   FCV_TOP_CM=223 python3 cycle_test.py 10 5 40      # 仍可手動指定跨距
#
# ⚠️ 跨距**每次來回都會漂**（09-03 實測 223→226→227→225→223，單趟約 2cm），
#    所以自動判定時 TOP 取 home_ground_cm 的當下值，不寫死。
def _detect_zero_convention():
    """回傳 (zero_at, home_ground_cm)。純讀取，不改變機器狀態。"""
    env = os.environ.get("FCV_ZERO_AT", "").strip().lower()
    hg = 0
    try:
        v = field(ask(CRANE, "status", 10), "home_ground_cm")
        if v is not None:
            hg = int(v)
    except Exception:
        hg = 0
    if env in ("top", "bottom"):
        return env, hg
    return ("top" if hg > 0 else "bottom"), hg


# 🔴 這三個由 resolve_zero_convention() 在**起點檢查之前**填實值。
#    不能在此處呼叫 —— _detect_zero_convention 依賴 ask()/field()，兩者定義在本行之後，
#    模組載入時呼叫會 NameError。height() 是惰性的（呼叫時才讀這些全域），所以定義順序無妨。
ZERO_AT, HOME_GROUND_CM, TOP = "bottom", 0, None
BOTTOM = 0


def resolve_zero_convention():
    """填 ZERO_AT / HOME_GROUND_CM / TOP。必須在任何 height() 呼叫之前執行。"""
    global ZERO_AT, HOME_GROUND_CM, TOP
    ZERO_AT, HOME_GROUND_CM = _detect_zero_convention()
    if ZERO_AT == "top" and HOME_GROUND_CM <= 0:
        print("🔴 FCV_ZERO_AT=top 但吊機 home_ground_cm=%d —— 沒有跨距可用，"
              "請先跑 `zero_meters top` 或改用 FCV_ZERO_AT=bottom。" % HOME_GROUND_CM)
        sys.exit(1)
    TOP = int(os.environ.get("FCV_TOP_CM",
                             str(HOME_GROUND_CM if ZERO_AT == "top" else 231)))
    print("座標慣例：**%s 歸零**（home_ground_cm=%d）⇒ height = %s；跨距 TOP=%d cm"
          % ("頂端" if ZERO_AT == "top" else "底端", HOME_GROUND_CM,
             "home_ground_cm - length_left" if ZERO_AT == "top" else "-length_left", TOP))


def height(length_left):
    """繩長讀值 → 離底高度（cm）。讀不到就回 None，交給呼叫端 bail。"""
    if length_left is None:
        return None
    return (HOME_GROUND_CM - length_left) if ZERO_AT == "top" else -length_left
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
# [2026-09-03 per user] 行進速率統計 —— 目的是推估「上下走一公尺要多久」。
# 分三個口徑，因為它們回答的是不同問題（見 _rate_summary 的說明）。
# [2026-09-03 per user] 真空吸不到的步數 —— 不中止，但一定要看得見（見下方 ③a）。
no_seal_steps = [0]
# [2026-09-04] ③b 改用 DEPLOY_F 之後的兩個新計數：
#   no_wall  = 牆比預期遠／沒玻璃／出邊界 —— **跳過掃動、續行**（與「吸不到就繼續走」同性質）
#   warn     = 有壓上但沒收斂到目標壓力 —— 掃動照做，只是資料品質要標記
no_wall_steps = [0]
press_warn_steps = [0]
# [2026-09-04] 橫桿步數。與 no_wall 分開記：兩者都是現場條件，但成因不同
#   （no_wall=牆太遠/沒玻璃；obstacle=有東西比玻璃更近），混在一起會看不出牆的形狀。
obstacle_steps = [0]
timing = {"down_cm": 0.0, "down_move_s": 0.0, "down_step_s": 0.0, "down_steps": 0,
          "up_cm": 0.0, "up_s": 0.0, "up_runs": 0,
          "ext_s": 0.0, "vac_s": 0.0, "rail_s": 0.0, "ret_s": 0.0}
all_nearmiss = []
all_roll_nearmiss = []
roll_fixed = [0]      # roll 自動修正成功的次數


def ask(addr, cmd, timeout, prefixes=("OK", "ERR")):
    """送一行指令，讀到指定前綴的行為止。回傳該行（或 TIMEOUT/EXC:...）。

    🔴 [2026-09-03] `prefixes` 是後加的：**不是每個指令都回 OK/ERR 開頭的行**。
    `arm_status` 回的是手臂原始狀態字串 `[M1] pos=... | [M2] pos=...`，
    用預設前綴會**永遠等不到、直到逾時**，而症狀長得像「手臂沒回應」——
    整合清潔動作那天就是這樣誤判了兩輪，實際上手臂一直是好的。
    """
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
                if any(line.startswith(p) for p in prefixes):
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
    """風扇占空比。寫入失敗時自動走 `pwm restart` 復原並重試一次。

    🔴 [2026-09-03] QX-DO24（.22 slave 9）**寫入方向間歇失效**：09-02 的 cycle10_0902c
    與 09-03 的 10 週期測試都死在這裡，訊息逐字相同
    （`ERR pwm_freq_write_failed_no_reply_timeout`）。當場實測：
      - FC 0x10 長幀 3 次全逾時 → FC 0x06 短幀退路自動接手 → **也全逾時**
      - 但 `pwm status`（讀）正常，`pwm restart`（reg 0xFF00 短幀）回 `acked=1`
    ⇒ 不是模組全滅，是**寫入路徑失效而重啟這條短幀仍活著**。
    既然復原手段已被證實有效，就不該讓一次 40 分鐘的耐久測試死在一個能自動救回的已知故障上。
    ⚠️ 但**每次觸發都會印出來**——這是間歇故障的唯一計數來源，靜默重試等於把證據吃掉。
    """
    cmd = "pwm set 1 50 65535 %d" % pct
    r = ask(WROBOT, cmd, 15)
    if r.startswith("OK"): return r
    print("   ⚠ 風扇寫入失敗（%s）→ 試 pwm restart 後重試" % r.strip())
    rr = ask(WROBOT, "pwm restart", 20)
    print("   ⚠ pwm restart: %s" % rr.strip())
    if not rr.startswith("OK"):
        return r          # 連重啟都進不去 → 交回呼叫端中止，不再盲試
    time.sleep(1.0)
    r2 = ask(WROBOT, cmd, 15)
    print("   ⚠ 重試結果: %s" % r2.strip())
    return r2


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
    # 🔴 [2026-09-04] roll 也改成「連續 N 筆」，與左右差同一套邏輯。
    #   09-03 就記過這條待辦：「roll 中止門檻是瞬時的，左右差卻要連續 3 筆
    #   —— 同一個教訓只套用了一半」，當日 223cm 收回全程平均 1.33°，
    #   卻被最後一筆停止擺盪 −7.04° 中止。
    #   09-04 兩段上升（110cm / 118cm）實測再次量到瞬間尖峰 **−6.65° 與 −5.41°**，
    #   兩次都隨即回穩（−3.35 / −2.13）⇒ **機體在繩索移動中本來就會瞬間擺過 6°，
    #   那是固有行為不是姿態失控。** 一筆就中止會把正常的擺盪判成故障。
    roll_streak, roll_nearmiss = [0], [0]

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
                if roll is not None:
                    if abs(roll) > ROLL_TRIP:
                        roll_streak[0] += 1
                        if roll_streak[0] >= ROLL_PERSIST:
                            abort_reason.append(
                                "%s: roll 連續 %d 筆超過 %.1f°（最後 %.2f°）—— 持續傾斜，非擺盪"
                                % (label, roll_streak[0], ROLL_TRIP, roll)); break
                    else:
                        if roll_streak[0] > 0:
                            roll_nearmiss[0] += 1   # 超標過但自行回復 ＝ 擺盪，不是失控
                        roll_streak[0] = 0
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
                  mdiff=maxdiff[0], nearmiss=nearmiss[0],
                  roll_nearmiss=roll_nearmiss[0])
    return res, st, dur


def roll_recover(tag):
    """roll 持續超標 → 自動水平修正。回 True 表示救回來、可以續行。

    🔴 [2026-09-04 per user]「救得回來就繼續腳本，不行才停住」。
    當日實測：步2 因 roll 連續超標中止，事後量到 L=-147 / R=-153（差 6cm）、roll -7.07°；
    **一道 `roll_correct -2` 就把 L/R 拉平到 -150/-150、roll 回到 -1.4°**。
    ⇒ 這類傾斜是**繩長差造成的、可修的**，不該讓整場停下來。

    🔴 **「繩長已經齊平卻仍然歪」是另一回事** —— 那不是繩長造成的，再動繩只會更糟，
       直接回 False 交回呼叫端中止。這是這支函式最重要的一條分支。

    ⚠️ 中止時 `emergency()` 送過 `emergency_stop`，本體會停在 `state=error`，
       修完必須 `reset` 才能繼續，否則後續指令全部會被狀態閘門擋掉。
    📌 `roll_correct` 的符號：`+delta = 左放右收`、`-delta = 左收右放`（吊機原始碼註解）。
       所以 L-R > 0（左邊比右邊低）要用**負**的 delta。
    📌 delta 與實際位移不是 1:1 —— 當日送 -2 實際兩側各動 3cm。所以用**迭代量到為止**，
       不去猜那個增益。
    """
    prev_roll = None      # [2026-09-09] 上一輪的 roll，用來判斷「這次修正有沒有效」
    for k in range(ROLL_RECOVER_TRY):
        # 🔴 [2026-09-04] 狀態讀取要**重試**，不能一次失敗就放棄。
        #   首次實跑就踩到：`roll_correct -3` **實際上成功了**（事後量到 L=-191/R=-190、
        #   roll 由 -6.94° 變 +1.37°），但緊接著的狀態讀取失敗，程式就放棄並中止整場 ——
        #   **修好了卻回報修不好**。一次瞬時讀取失敗不該否定一個已經生效的修正。
        # 📌 失敗時把**原始回應**印出來：上一版只印「讀不到」，等於把診斷資訊丟掉，
        #   下次還是只能猜。
        L = R = roll = None
        for attempt in range(3):
            cs = ask(CRANE, "status", 15)
            ws = ask(WROBOT, "status", 15)
            L, R = field(cs, "length_left"), field(cs, "length_right")
            roll = field(ws, "raw_x")
            if L is not None and R is not None and roll is not None:
                break
            print("   ⚠ [%s] 狀態讀取第 %d 次失敗 —— crane:%s / wrobot:%s"
                  % (tag, attempt + 1, cs[:40], ws[:40]))
            time.sleep(1.5)
        if L is None or R is None or roll is None:
            print("   🔴 [%s] 連續 3 次讀不到繩長/roll —— 放棄修正" % tag)
            return False
        if abs(roll) <= ROLL_RECOVER_OK:
            print("   ✅ [%s] roll 已回到 %+.2f°（門檻 %.1f），第 %d 次後達成"
                  % (tag, roll, ROLL_RECOVER_OK, k))
            return True
        d = L - R
        # ═══ [2026-09-09] 停止條件與修正量都改掉，原因見下 ═══
        #
        # 🔴 舊版有兩處共用同一個錯誤前提：「水平 ＝ 繩長齊平」。
        #    ① 停止條件 `if abs(d) < 1.0: return False`（「繩長已齊平卻仍歪 ⇒ 不是繩長造成的」）
        #    ② 修正量 `delta = |L-R| / 2` ⇒ **整個修正的目標就是把繩長修到齊平**
        #
        # 🔴 而本機的水平點**不在齊平**。2026-09-09 五個獨立資料點：
        #      頂端齊平 +3.75° → roll_correct 1（差 4cm）→ −0.51°
        #      高度 53 齊平 +3.73° → roll_correct 1（差 3cm）→ +0.63°
        #      上行過程（**平衡迴路自己動的，無人下指令**）齊平三段 +3.25/+3.36/+2.89°，
        #      一有 2cm 差就掉到 +0.75°
        #    ⇒ 舊版會把繩長往齊平修（＝往最歪的方向），再於最歪處宣告「不是繩長問題」中止。
        #      2026-09-09 的 10 週期就是這樣掛在週期 1 步 4（8.04° → 修成 3.89° 後放棄）。
        #
        # ⚠️ **舊守衛的原意是對的**（防無止境動繩），錯的是拿「齊平」當代理指標。
        #    新版改成量**真正在乎的東西**：這次修正有沒有讓 |roll| 變小。
        #    好處是**不需要知道水平點在哪** —— 那個點可能隨高度／載重變，我們沒有模型。
        #
        # 📌 方向用**實測**不用推導：2026-09-09 兩次都是 roll>0 時 `roll_correct +1` 讓它變小。
        #    `roll_correct` 的正負號定義在待辦表上尚未結案（記載 −0.85°/cm，本日實測 −4.26°/cm，
        #    差 5 倍），所以**刻意不從符號約定推、也不用增益去算步長**。
        #    一次固定 1cm；若方向猜錯，下一輪的「沒改善」會立刻停下來，最多多動 1cm。
        if prev_roll is not None and abs(roll) >= abs(prev_roll) - ROLL_RECOVER_MIN_GAIN:
            print("   🔴 [%s] 上一次修正沒有改善 |roll|（%+.2f° → %+.2f°，L-R=%.0f）"
                  "—— 不再動繩，交回中止" % (tag, prev_roll, roll, d))
            return False
        delta = 1 if roll > 0 else -1
        print("   ⚙ [%s] 第 %d 次：L=%.0f R=%.0f（差 %+.0f）roll %+.2f° → roll_correct %d"
              % (tag, k + 1, L, R, d, roll, delta))
        r = ask(CRANE, "roll_correct %d" % delta, 120, prefixes=("OK", "ERR"))
        if not r.startswith("OK"):
            print("   ⚠ [%s] roll_correct 失敗：%s" % (tag, r[:80]))
            return False
        # 🔴 [2026-09-09] 一定要在這裡記下「送出修正前的 roll」——
        #    上面的「有沒有改善」判斷全靠它。漏了這行，prev_roll 永遠是 None、
        #    停止條件永遠不會觸發，而**症狀是「看起來正常，只是從來不會停」**。
        prev_roll = roll
        time.sleep(2.5)
    cs = ask(CRANE, "status", 10); ws = ask(WROBOT, "status", 10)
    roll = field(ws, "raw_x")
    ok = roll is not None and abs(roll) <= ROLL_RECOVER_OK
    print("   %s [%s] %d 次修正後 roll=%s" % ("✅" if ok else "🔴", tag, ROLL_RECOVER_TRY,
                                              ("%+.2f°" % roll) if roll is not None else "讀不到"))
    return ok


def clear_error(tag):
    """emergency_stop 之後把本體由 error 拉回 idle。回 True 表示可以續行。"""
    st = ask(WROBOT, "status", 10)
    if "state=error" not in st:
        return True
    r = ask(WROBOT, "reset", 20)
    st = ask(WROBOT, "status", 10)
    ok = "state=error" not in st
    print("   %s [%s] 清除 error：reset -> %s（現在 %s）"
          % ("✅" if ok else "🔴", tag, r[:30],
             (re.search(r"state=\w+", st).group(0) if re.search(r"state=\w+", st) else "?")))
    return ok


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
        print("  左右差超標後自行回復（未達連續 %d 筆）: %d 次 —— 平衡迴路在工作，不是故障"
              % (DIFF_PERSIST, sum(all_nearmiss)))
    if roll_fixed[0]:
        print("  🔧 roll 自動修正並續行: %d 次 —— 繩長差造成的傾斜，已就地拉平" % roll_fixed[0])
    if all_roll_nearmiss:
        print("  roll 超標後自行回復（未達連續 %d 筆）: %d 次 —— 擺盪，不是姿態失控"
              % (ROLL_PERSIST, sum(all_roll_nearmiss)))
    if obstacle_steps[0]:
        tot = timing["down_steps"] or 1
        print("\n⚠ 疑似橫桿的步數：%d / %d（%.0f%%）—— 手臂在比任何玻璃都近的位置就接觸，"
              "該步清潔動作被跳過。這是**牆面結構**，不是故障。"
              % (obstacle_steps[0], tot, 100.0 * obstacle_steps[0] / tot))
    if no_wall_steps[0]:
        tot = timing["down_steps"] or 1
        print("\n🔴 找不到牆的步數：%d / %d（%.0f%%）—— 手臂伸到上限仍未接觸，"
              "該步的清潔動作被跳過。這是**現場幾何**（牆比預期遠／沒玻璃／出邊界），不是故障。"
              % (no_wall_steps[0], tot, 100.0 * no_wall_steps[0] / tot))
    if press_warn_steps[0]:
        print("🟡 壓力未收斂到目標的步數：%d —— 有壓上、掃動照做，但沒到 %.1f N·m，"
              "這些步的清潔力道與其他步不可比。" % (press_warn_steps[0], ARM_TARGET_NM))
    if no_seal_steps[0]:
        tot = timing["down_steps"] or 1
        print("\n🔴 真空未建立的步數：%d / %d（%.0f%%）—— 這些步**沒有模擬到附著**，"
              "滑台掃動也被跳過，不可拿來當吸附系統的證據。"
              % (no_seal_steps[0], tot, 100.0 * no_seal_steps[0] / tot))
    _rate_summary()


def _rate_summary():
    """行進速率 —— 三個口徑分開，因為它們回答的是不同問題。

    ① 下行純移動：只算 `pay_out` 本身。可跟上行比「機構+VFD 的移動能力」，
       但**仍含每步的加減速**（5 步 × 40cm，不是一次連續 200cm）。
    ② 下行含開銷：整步的牆鐘時間（伸出+真空+滑台+收回+移動+讀值）。
       **這才是實際作業速率** —— 規劃一面牆要多久要用這個數字。
    ③ 上行回程：一次連續移動，沒有分段加減速 ⇒ **不可與 ① 直接相比**。
    """
    t = timing
    if not t["down_steps"] and not t["up_runs"]:
        return
    print("\n=== 行進速率（推估上下一公尺）===")
    def line(label, cm, sec, note=""):
        if cm <= 0 or sec <= 0: return
        print("  %-12s %7.0f cm / %6.1f s  →  %5.2f cm/s   每公尺 %5.1f s%s"
              % (label, cm, sec, cm / sec, 100.0 * sec / cm, note))
    line("下行純移動", t["down_cm"], t["down_move_s"], "   （含每步加減速）")
    line("下行含開銷", t["down_cm"], t["down_step_s"], "   ← 實際作業速率")
    line("上行回程",   t["up_cm"],   t["up_s"],        "   （一次連續，不可與純移動直接比）")
    n = t["down_steps"]
    if n:
        print("  單步平均開銷: 伸出 %.1f / 真空 %.1f / 滑台 %.1f / 收回 %.1f / 移動 %.1f"
              " / 其他 %.1f s  （整步 %.1f s）"
              % (t["ext_s"]/n, t["vac_s"]/n, t["rail_s"]/n, t["ret_s"]/n, t["down_move_s"]/n,
                 (t["down_step_s"] - t["ext_s"] - t["vac_s"] - t["rail_s"]
                  - t["ret_s"] - t["down_move_s"]) / n,
                 t["down_step_s"]/n))
    if t["down_step_s"] > 0 and t["up_s"] > 0:
        per_m = 100.0 * (t["down_step_s"] / t["down_cm"] + t["up_s"] / t["up_cm"])
        print("  ⇒ **一趟來回每公尺約 %.1f s**（下行含開銷 + 上行回程）" % per_m)
    print("  ⚠️ 下行是 %d 步 × %dcm 的分段移動、上行是一次連續移動 —— 兩者的 cm/s 不同源。"
          % (STEPS, STEP_CM))


def bail(msg):
    print("\n🔴 中止：%s" % msg)
    print("   現場保留，未自動復位。")
    _diff_summary()
    cleanup()
    fin = ask(CRANE, "status", 10)
    print("   高度=%s cm（繩長 L=%s R=%s） tension_valid=%s"
          % (height(field(fin, "length_left")), field(fin, "length_left"),
             field(fin, "length_right"), field(fin, "tension_valid")))
    sys.exit(1)


def cleanup():
    """🔴 一定要跑：收手臂 + 風扇關 + motion_hz 寫回下行速度。

    中途中止若把 motion_hz 留在 50，下一個人下 pay_out 就是 50Hz 下行 —— 超出使用者定的上限。

    🔴 [2026-09-03 per user] **補上 arm_park。** ③b 整合清潔動作之後，任何在
    「壓上之後、收手臂之前」的中止都會**讓手臂留在壓著玻璃的狀態**（實測 6~14 Nm 持續頂著），
    而 bail() 只印「現場保留，未自動復位」—— 那句話對吊機與推桿是刻意的（保留現場好查），
    但對手臂不成立：**頂著玻璃不是「保留現場」，是持續施力**，而達妙馬達長時間受力會觸發
    過熱/過流鎖存（09-03 的 `switchControlMode failed` 就是那樣來的，只能斷電解除）。
    ⚠️ 手臂收回**不會**破壞現場證據：θ 與 tau 在中止當下已經被記錄，收回只是卸力。
    ⚠️ 放在最前面：先卸力再處理其他，因為其他兩件都不緊急。
    """
    print("   [收尾] 手臂 arm_park : %s" % ask(WROBOT, "arm_park", 90)[:40])
    print("   [收尾] 風扇 %d%% / motion_hz→%d : %s / %s"
          % (FAN_OFF, DOWN_HZ, fan(FAN_OFF), ask(CRANE, "set_motion_hz %d" % DOWN_HZ, 15)))


print("週期測試：%d 週期 × (下行 %d 步 × %dcm = %dcm @%dHz) + 拉回 @%dHz"
      % (CYCLES, STEPS, STEP_CM, STEPS * STEP_CM, DOWN_HZ, UP_HZ))
print("中止門檻：|roll|>%.1f°（連續 %d 筆，可自動修正） / 左右差>%.0fcm 連續 %d 筆 / tension_valid=0 / 任一指令非 OK\n"
      % (ROLL_TRIP, ROLL_PERSIST, DIFF_TRIP, DIFF_PERSIST))

resolve_zero_convention()          # 🔴 必須在第一次 height() 之前
st0 = ask(CRANE, "status", 10)
L0 = height(field(st0, "length_left"))
if L0 is None or abs(L0 - TOP) > TOL:
    print("🔴 起點不在頂端（高度=%s cm，需 %d±%d）——不猜，請先手動移到頂端。" % (L0, TOP, TOL))
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
        print("%3s %6s %6s %6s %6s %28s %7s %7s %6s %5s %7s"
              % ("步", "伸出s", "真空s", "清潔s", "收回s", "四顆壓力 kPa", "移動s", "roll均", "出帶%", "Δmax", "停後roll"))
        for i in range(1, STEPS + 1):
            t_step0 = time.time()
            cur = height(field(ask(CRANE, "status", 10), "length_left"))
            if cur is None:
                bail("讀不到吊機位置")
            end = cur - STEP_CM          # 下行 = 高度減少
            if end < BOTTOM - TOL:
                bail("預期終點 %d cm 低於底端，超出區間 [%d, %d]" % (end, BOTTOM, TOP))

            r = fan(FAN_OFF)                                   # ①
            if not r.startswith("OK"): bail("風扇關閉失敗：%s" % r)
            r = ask(WROBOT, "vacuum feet on", 20)              # ②
            if not r.startswith("OK"): bail("開真空閥失敗：%s" % r)

            t = time.time()                                     # ③
            r = ask(WROBOT, "pusher all extend_raw", 90)
            t_ext = time.time() - t
            if not r.startswith("OK"): bail("extend_raw 失敗：%s" % r)
            # ③a [2026-09-03 per user] 等真空建立，**至少一顆** <= VAC_OK_KPA 就通過。
            # 背景：09-03 首輪四顆全程都在 0 附近，我一度判成「推桿沒碰到玻璃」——
            # per user 更正：**有碰到，是時間不夠**。原本 extend 一回來就立刻讀壓力，
            # 讀到的是還沒抽起來的瞬間值。
            # 為什麼是「一顆」而不是「四顆」：檔頭 ③ 的理由仍然成立 —— 玻璃有縫時某些
            # 吸盤本來就吸不住，那是現場條件不是故障。但「一顆都沒有」代表這一步完全
            # 沒有附著，那個必須看得見，否則整輪會在無附著的狀態下悄悄跑完（首輪就是）。
            t = time.time()
            pr = None
            while time.time() - t < VAC_WAIT_S:
                ps = ask(WROBOT, "status", 15)
                pr = [field(ps, "p%d" % n) for n in (5, 6, 7, 8)]
                if any(p is not None and p <= VAC_OK_KPA for p in pr): break
                time.sleep(0.3)
            t_vac = time.time() - t
            n_seal = sum(1 for p in (pr or []) if p is not None and p <= VAC_OK_KPA)
            # 🔴 [2026-09-03 per user] 吸不到**不中止**，記錄後跳過本步的滑台掃動、續行。
            #
            # 為什麼可以放行：這個測試**本來就不靠吸盤承重**（見檔頭 ③ 與「無防墜錨點」那段）
            # —— 全程由鋼索承重，第 ③ 步不提供任何附著作用。所以吸不到不改變安全性，
            # 只代表「這一步沒有模擬到附著」。09-03 現場實測也確認過：7% 與 8% 風扇推力
            # 各試一輪都吸不上，而繼續走完全沒有風險。
            #
            # 🔴 但**必須看得見**，否則整輪可能全程零附著卻長得像成功 —— 09-03 早上就出過
            #    一次那種假象（讀太早，四顆全 0 卻照跑）。所以：逐步印、計數、總結再報一次。
            # 🔴 跳過 ③b 滑台掃動的理由：沒有附著時機體只掛在繩上，橫向移動滑台會讓它擺盪，
            #    而那個擺盪不屬於被測項目，只會污染 roll 統計。
            skip_rail = (n_seal == 0)
            if skip_rail:
                no_seal_steps[0] += 1
                print("   ⚠ 真空未建立（%.1fs 內無一顆到 %d kPa：%s）—— 本步跳過滑台掃動、直接收腳續行"
                      % (t_vac, VAC_OK_KPA, "/".join("%s" % p for p in (pr or []))))

            # ③b [2026-09-03 per user] 上滑台 0→50→0。
            # 位置刻意放在 ③ 與 ④ 之間：推桿仍在 10cm（機體有支撐）、風扇仍關（①的順序
            # 是安全需求，不可為了掃動提前開）。手臂維持 PARK —— 本步只動滑台，不壓玻璃。
            # ③b [2026-09-03 per user] 由「滑台空跑」改成**完整清潔動作**：
            #     壓上(arm_deploy) → 滑台 0→RAIL_CM→0 → 收手臂(arm_park)
            #
            # 🔴 走本體的 arm_deploy_f/arm_park 代轉，**不直接連 9527** ——
            #    ⚠️ [2026-09-04 更正] 舊註解寫的理由「motor_api 在本體的 127.0.0.1:9527，
            #    吊機連不到」**是錯的**：它綁的是 `0.0.0.0:9527`，吊機上的 web GUI 正是
            #    靠這條直連。真正的理由只剩後半句 —— 代轉是既有的編排路徑
            #    （cmd_arm_deploy_f → arm_cmd_），不該另闢第二條。那個理由仍然成立。
            #
            # 🔴🔴 [2026-09-04 per user] 判準由「讀姿態」改成「讀 DEPLOY_F 的回覆」。
            #    舊判準（0.55 < θ < 0.75 且 tau > 8.0）存在的唯一原因，是 `arm_deploy`
            #    壓玻璃時本來就回 ERR、回覆沒有資訊，只好回頭去推姿態。
            #    DEPLOY_F 的回覆**本身就是結論**，而且比事後讀姿態更可信：
            #      - 事後 `arm_status` 會凍結（馬達失能就不再送 CAN 幀，快取吐舊值）
            #      - `sleep(1.5)` 讀在鬆弛中途（09-04 實測壓上後 1 秒內掉 1.46~2.15 N·m）
            #    ⇒ 這兩個坑一起消失，不是換個數字而已。
            #
            # 🔴 四種回覆、三種行為（per user 2026-09-04）：
            #      OK        壓到目標壓力            → 正常掃動
            #      WARN      有壓上但沒到目標        → 掃動照做，計數（資料品質標記）
            #      no_wall   牆太遠/沒玻璃/出邊界    → **跳過掃動、計數、續行**
            #      obstacle  有東西擋著              → **中止**
            #    no_wall 與 obstacle 的分野，跟真空那邊「吸不到就繼續走」是同一個道理：
            #    前者是現場條件（09-03 高度 229 的 DEPLOY 520 只壓出 6.01 N·m 就是這個），
            #    後者不是 —— 頂著障礙物再橫走滑台會弄壞東西。
            #    📌 舊判準在高度 229 會 bail()，而症狀會長得像手臂故障。這正是要修掉的。
            #
            # 🔴 無附著時整段跳過（連同壓上）：機體只掛在繩上時把手臂壓上玻璃並橫走滑台，
            #    等於用一台懸空的機器去推牆，姿態會被推歪而那不屬於被測項目。
            t = time.time()                                     # ③b
            t_rail = 0.0
            skip_press = False
            if not skip_rail:
                if ARM_FORCE_MODE[0]:
                    # ⚠️ prefixes 一定要含 WARN：未收斂時前綴是 WARN 不是 OK/ERR，
                    #    漏了它會被當成非同步事件、等到逾時，症狀長得像「手臂沒回應」。
                    r = ask(WROBOT, "arm_deploy_f %.1f %s" % (ARM_TARGET_NM, ARM_SLOT),
                            150, prefixes=("OK", "ERR", "WARN"))
                    if r.startswith("ERR unknown_cmd"):
                        # 本體還是舊 binary。整輪退回舊路徑，並大聲說一次。
                        ARM_FORCE_MODE[0] = False
                        print("   ⚠️ 本體無 arm_deploy_f（舊 binary）→ 本輪起改用 "
                              "arm_deploy %d + 姿態判準。壓力不再是被控量，"
                              "各高度的清潔力道會隨牆距浮動。" % ARM_WALL_MM)
                    elif r.startswith("OK"):
                        pass                                   # 壓到目標，照常掃動
                    elif r.startswith("WARN"):
                        press_warn_steps[0] += 1
                        print("   🟡 壓力未收斂到 %.1f N·m，掃動照做：%s"
                              % (ARM_TARGET_NM, r[:90]))
                    elif "no_wall" in r:
                        no_wall_steps[0] += 1
                        skip_press = True
                        print("   ⚠️ 找不到牆（手臂伸到上限仍未接觸）—— 本步跳過清潔動作、"
                              "續行：%s" % r[:90])
                    elif "obstacle" in r:
                        # 🔴🔴 [2026-09-04 per user 現場確認] **由「中止」改為「跳過、續行」。**
                        #   原本當成異常，是因為我把 obstacle 想成「不該出現的東西」。
                        #   實測那是**橫桿** —— 這面牆的固定結構，一趟下行必然會遇到幾根。
                        #   那跟「有些玻璃面有縫隙、吸盤本來就吸不住」是**完全同一類的現場條件**，
                        #   而那一條 per user 明講過是「設計要求，不是妥協」。
                        #   照舊邏輯，十週期遇到第一根橫桿就整場中止。
                        #   ⚠️ 「不能頂著橫桿橫走滑台」這個顧慮仍然成立 ——
                        #      正確處置是**不要掃**，不是**停止整場測試**。
                        obstacle_steps[0] += 1
                        skip_press = True
                        print("   ⚠ 疑似橫桿（接觸點比任何玻璃都近）—— 本步跳過清潔動作、"
                              "續行：%s" % r[:90])
                    else:
                        bail("arm_deploy_f 失敗：%s" % r[:120])

                if not ARM_FORCE_MODE[0]:
                    # 舊路徑（本體尚未安裝 arm_deploy_f 代轉時）。判準與 09-03 相同。
                    r = ask(WROBOT, "arm_deploy %d %s" % (ARM_WALL_MM, ARM_SLOT), 90)
                    time.sleep(1.5)
                    # arm_status 回的是 `[M1] ... | [M2] ...`，不是 OK 開頭 —— 見 ask() 的說明
                    ast_ = ask(WROBOT, "arm_status", 20, prefixes=("[M1]",))
                    th  = field(ast_, "pos")
                    tau = field(ast_, "tau")
                    if th is None or tau is None:
                        bail("arm_status 讀不到姿態：%s" % ast_[:80])
                    if not (0.55 < th < 0.75 and tau > 8.0):
                        bail("手臂沒有壓上：θ=%.4f tau=%+.2f（arm_deploy 回 %s）"
                             % (th, tau, r[:40]))

            if not skip_rail and not skip_press:
                r = ask(WROBOT, "rail %d" % RAIL_CM, 60)
                if not r.startswith("OK"): bail("rail %d 失敗：%s" % (RAIL_CM, r))
                r = ask(WROBOT, "rail 0", 60)
                if not r.startswith("OK"): bail("rail 0 復位失敗：%s" % r)
            if not skip_rail:
                # 🔴 no_wall 也要收：手臂已經伸出去了（尋觸走到上限），
                #    跳過的只是掃動，不是收回。留在外面會一路撞到下一步。
                r = ask(WROBOT, "arm_park", 90)
                if not r.startswith("OK"): bail("arm_park 失敗：%s" % r)
                t_rail = time.time() - t

            t = time.time()                                     # ④
            r = ask(WROBOT, "pusher all retract", 90)
            t_ret = time.time() - t
            if not r.startswith("OK"): bail("retract 失敗：%s" % r)

            r = fan(FAN_ON)                                     # ⑤
            if not r.startswith("OK"): bail("風扇開啟失敗：%s" % r)
            time.sleep(1.0)                                     # ⑥

            res, stt, dur = monitored_crane_move("pay_out", STEP_CM,        # ⑦
                                                 "週期%d步%d" % (cyc, i), 180)
            if abort_reason:
                # 🔴 [2026-09-04 per user] roll 造成的中止先試著自動修正，救得回來就續行。
                #   其他中止原因（tension_valid=0 / 左右差持續擴大）**不自動修正** ——
                #   那兩者不是「歪了」而是「保護失效」或「持續發散」，性質不同。
                if "roll 連續" in abort_reason[0]:
                    print("   ⚠ %s" % abort_reason[0])
                    tag = "週期%d步%d" % (cyc, i)
                    if roll_recover(tag) and clear_error(tag):
                        roll_fixed[0] += 1
                        abort_reason[:] = []
                        # 這一步的移動被 stop 打斷、沒走完，補完剩下的距離。
                        cur2 = height(field(ask(CRANE, "status", 10), "length_left"))
                        if cur2 is None: bail("修正後讀不到吊機位置")
                        rest = int(round(cur2 - end))
                        if rest > TOL:
                            print("   ↩ 補完本步剩餘 %d cm" % rest)
                            res, stt2, dur2 = monitored_crane_move(
                                "pay_out", rest, tag + "補", 180)
                            dur += dur2
                            if abort_reason:
                                bail(abort_reason[0] + "（修正後補完再次中止 —— 不再重試）")
                            if not res.startswith("OK"):
                                bail("補完 pay_out 失敗：%s" % res)
                        else:
                            print("   ↩ 剩餘 %d cm 在容差內，不補" % rest)
                            # 🔴 [2026-09-04] 這行是實作 bug 的修正：修正成功但**不需要補完**時，
                            #   `res` 仍留著被中止的那個 `ERR aborted`，掉到下面
                            #   `if not res.startswith("OK")` 就會中止 —— 修好了卻還是停。
                            #   首次實跑就踩到（剩餘 -1cm）。補完那條路徑有重新賦值 res，
                            #   只有這條沒有。
                            res = "OK roll_recovered_no_topup"

                    else:
                        bail(abort_reason[0] + "（自動修正失敗）")
                else:
                    bail(abort_reason[0])
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

            t_step = time.time() - t_step0
            timing["down_cm"]    += STEP_CM
            timing["down_move_s"] += dur
            timing["down_step_s"] += t_step
            timing["down_steps"]  += 1
            timing["ext_s"] += t_ext; timing["vac_s"] += t_vac
            timing["rail_s"] += t_rail; timing["ret_s"] += t_ret

            pstr = "/".join("%.0f" % (p if p is not None else 0) for p in pr)
            if stt:
                all_diff.append(("週期%d步%d" % (cyc, i), stt["mdiff"]))
                all_nearmiss.append(stt["nearmiss"])
                all_roll_nearmiss.append(stt.get("roll_nearmiss", 0))
            print("%3d %6.1f %6.1f %6.1f %6.1f %28s %7.1f %7.2f %6.0f%% %5.0f %7s"
                  % (i, t_ext, t_vac, t_rail, t_ret, pstr, dur,
                     stt["avg"] if stt else -1, stt["outpct"] if stt else -1,
                     stt["mdiff"] if stt else -1,
                     ("%+.2f" % ra) if ra is not None else "-"))

        # ⑨ 拉回頂端
        cur = height(field(ask(CRANE, "status", 10), "length_left"))
        if cur is None: bail("讀不到吊機位置（回程前）")
        if TOP - cur < 1: bail("回程距離異常：高度=%.0f cm（已在頂端附近）" % cur)
        r = fan(FAN_OFF)
        if not r.startswith("OK"): bail("回程前關風扇失敗：%s" % r)
        r = ask(CRANE, "set_motion_hz %d" % UP_HZ, 15)
        if not r.startswith("OK"): bail("設定 %dHz 失敗：%s" % (UP_HZ, r))
        res, stt, dur = monitored_crane_move("retract", int(TOP - cur),
                                             "週期%d回程" % cyc, 300)
        ask(CRANE, "set_motion_hz %d" % DOWN_HZ, 15)   # 立刻寫回，不等收尾
        if abort_reason: bail(abort_reason[0])
        if not res.startswith("OK"): bail("回程 retract 失敗：%s" % res)
        timing["up_cm"] += int(TOP - cur)
        timing["up_s"]  += dur
        timing["up_runs"] += 1
        fin = height(field(ask(CRANE, "status", 10), "length_left"))
        if stt:
            all_diff.append(("週期%d回程" % cyc, stt["mdiff"]))
            all_nearmiss.append(stt["nearmiss"])
            all_roll_nearmiss.append(stt.get("roll_nearmiss", 0))
        print("  回程 %.0fcm @%dHz  %.1fs  roll均 %.2f  出帶 %.0f%%  Δmax %.0f  → 高度 %.0f\n"
              % (TOP - cur, UP_HZ, dur, stt["avg"] if stt else -1,
                 stt["outpct"] if stt else -1, stt["mdiff"] if stt else -1,
                 fin if fin is not None else -1))
finally:
    cleanup()

print("=== %d 個週期全部完成 ===" % CYCLES)
_diff_summary()
