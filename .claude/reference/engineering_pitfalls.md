# engineering_pitfalls.md — 驅動層踩坑 / 工程方法

> 📌 **2026-09-12 由 `ONBOARDING.md` §2「硬體驅動 Firmware 踩坑知識」+ §3「工程方法」抽出,
> 外加 §6.8 / §7 / §8.2 / §9 的方法論教訓。** ONBOARDING 原檔已凍結歸檔為
> `.claude/archive/ONBOARDING-2026-08-13.md`(整份保留,**§4 機械差異與 §6 步態演進目前無可替代,
> 要查設計沿革去那裡**)。
>
> **抽出判準**:只收「別處查不到」的。抽出前逐條比對 `summaries/`(15 份裝置暫存器摘要)、
> `per_program_cautions.md`、`motion_flow.md`、`SOFTWARE.md`、`HARDWARE.md` ——
> 已收錄的不重複(DM2J `0x1003`/`0x1111`、SE3 `H1000` latch/`07-10`/DC 煞車 → 見對應 summary)。
>
> ⚠️ **這些多半是 v1 時期建立的知識,但原理與排查方法在 v2 遇到同類症狀仍通用**。
> 檔名行號可能已過時,**與程式碼衝突時以程式碼為準**。

---

## 1. 裝置驅動踩坑(`summaries/` 沒收的)

### 1.1 ZDT 閉環步進(推桿,v1/v2 共用)

- 🔴 **`pos_reached` bit 不可靠** —— 馬達物理已停但 bit 不 set。**不能只靠它判完成**,用三層 fallback:
  `stall_flag=1` 也算停了;`|real_speed| ≤ 20 RPM` 連續 3 次 poll(~450 ms);
  **`|Δreal_pos| ≤ 0.15°` 連續 3 次 poll(最可靠)**。
- 🔴 **`trigger_sync_move()` 回傳值不可信** —— Modbus 廣播(slave `0x00`)依規範無 reply,
  driver 看 `readEcho` 空就回 true(看起來像 error),但廣播**實際已送出**。
  呼叫端要**忽略回傳值**,改靠 poll 判斷 motion 有沒有發生。
- **gateway frame 對齊**:連送 Modbus 時 TCP buffer 殘留 echo 會干擾下一個指令的 readEcho,
  偶發 enable/pos_mode 失敗(**不是硬體壞**)。per-slave 2–3 次 retry + 120 ms back-off;
  群組指令某 slave 失敗應 **skip 該 slave 而非中斷整組**。
- **sync=1 + broadcast trigger**:用 `motion_control_pos_mode_nowait(sync=1)` 排隊,
  再從任一 instance 廣播 `trigger_sync_move()`,所有排隊 slave 同時啟動,避免姿勢不對稱。

### 1.2 SD76-C 計米器

- 🔴 **SCAL(`0x0014`-`0x0015`)手冊標「Counter Multiplier」,實機行為是除數(K-factor)**:
  `display = pulses ÷ (SCAL × 10^(-DP))`。SCAL 加大 → 顯示變慢長。
  Driver 對外 API 維持 multiplier 語意,內部換算 `M_app = 10^DP / SCAL`。
- 🔴 **通訊模式(`00-16=3`)下,部分 config 暫存器(含 `0x0020` DP)的 Modbus 寫入被 firmware 靜默忽略**
  —— **回 OK 但值沒進 EEPROM**,readback 仍是原值。要改 DP 必須從**面板**把 `00-16` 設成非 3、
  改完設回 3。`setEffectiveScale` 因此**不動 DP**,只處理 SCAL(SCAL 可走 Modbus)。
- 📌 換型號 / firmware 升級要重驗這兩條 —— 目前結論只代表這台。
- 📌 **同族 latch 行為**:SE3 的 `H1000` / `P.79` 也是同一類「模式沒切就靜默拒絕」。

### 1.3 Modbus-TCP gateway 共通問題(跨所有 driver)

- 🔴 **Stale buffer**:USR-TCP232 是**透傳**,RS485 上任何 byte 都會進 TCP socket。
  舊 transaction 逾時後遲到的 reply、或共用 bus 上別的裝置的 reply,都可能殘留在 kernel recv buffer
  → 下次 `recv()` 一次收到「stale + new」,driver 只檢查前幾 byte 容易誤判成 `bad reply len=N`。
  **解法**:`TCP_client::sendAndReceive()` 把整個 Modbus transaction(drain→send→recv)
  包在**一個 `lock_guard` 範圍內**,並發 caller 完全排隊。
- 🔴 **`TCP_client` Linux 殭屍連線**(2026-07-23 修):Linux 的 `available()` 用 `ioctl FIONREAD`
  **偵測不到對方正常關閉連線**(graceful close 時 FIONREAD 回 0,跟「沒資料」分不清);
  `sendData`/`receiveData` 失敗時也沒把 `connected` 設回 false ⇒ `reconnectLoop()` 永不觸發,
  卡在假的「已連線」。已修(Linux 分支改用 `recv(MSG_PEEK)`)。
  ⚠️ **這個 bug 影響所有用 `TCP_client` 的裝置**,不只發現它的那個功能。
- 🔴 **Driver init Mode B(共用外部 `TCP_client`)預設不做 probe**:TCP 連到 gateway 永遠會成功
  → 被誤判「device alive」,**實際硬體不存在也抓不到**;後續 polling 每次等滿 recv timeout,
  佔用共用 socket mutex 拖慢同 gateway 上其他裝置。
  **判斷方法:拔掉硬體但啟動還顯示 `[OK]`,就要懷疑這個 pattern。**

### 1.4 Crane RS485 匯流排格式統一 8N1

SD76-C(面板無 UART 設定入口,鎖死 8N1)+ SE3(RTU 無 8N1,只有 8N2/8E1/8O1)**沒有正規共同格式**。
解法:USR gateway 統一設 8N1,靠 **SE3 UART 對收端寬容** —— SE3 送出 8N2(多 1 個 stop bit),
USR 收時把多出的 stop bit 當 inter-frame idle gap,雙向都通。
⚠️ **長時間 stress test 沒做**;若遇隨機 CRC error,考慮把 SE3 與 SD76 分到不同 USR 物理隔離。
📌 2026-09 的 `.22` 匯流排干擾問題屬同一家族(見待辦總表「`.22` 匯流排本身沒修」)。

### 1.5 X518 / DSZL-107 張力採集

- ✅ **架構疑問已解**:X518 是**自帶 Ethernet、原生 Modbus TCP(port 502)**版本。
  現行 `Crane_control_PI` 的 `cli_C` 直連 `192.168.1.33:502`,**不經 USR 透傳**。
  (原 ONBOARDING 記的「driver 寫 RTU 框架、拓樸待確認」已不成立。)
- **物理上只有 2 通道**(CH1+CH2),手冊標題「双通道」;曾誤讀成 8 通道(讀到隔壁暫存器垃圾值)。
- 🔴 **`0xA20` 多功能命令暫存器**:寫 1=zero CH1、2=zero CH2、7=zero all、
  **寫 40(decimal)=保存參數到 flash**。**任何參數修改/校零都要接著寫 40 才會持久化**
  (`save_params()` 已封裝)。
- 出廠 IP `192.168.1.120`、mode register `0x644`(1=Modbus TCP)。
  ⚠️ 手冊中文 PDF 有 cmap 問題,欄位對照**務必用實機 dump 交叉確認**。

### 1.6 真空吸盤 seal 最佳實踐順序(寫吸附/步伐序列一律照這個)

- **Pattern 1 — valve-before-extend**:**先開電磁閥**(真空開始抽)→ **再** extend 推桿到牆面。
  吸盤碰牆瞬間已有負壓,seal 立刻形成。舊做法(extend 完再開閥)會讓空氣先跑進邊緣導致漏氣。
- **Pattern 2 — staged extend(兩階段推桿)**:伸到目標一半 → pause 1000 ms(讓負壓建立)→
  再伸到全目標。🔴 **Stage 2 必須無條件執行**,不能因為 Stage 1 的 `pos_reached` 判斷不穩就 early
  return(否則馬達卡在半程)。

---

## 2. 工程方法 / 反覆出現的 pattern

### 2.1 Obstacle 偵測:電流門檻 + **位置** gate

純電流門檻(`DISABLE_PHASE_CURRENT_LIMIT_MA` 800→900→1200 mA)會把「正常壓牆 jam」跟「真障礙物」
都判成 obstacle(壓牆電流可飆到 1.7 A)。
🎯 **核心洞察:壓牆與障礙物的馬達卡死電流特徵相同,調門檻治不了,差別在卡死的位置。**
修法是加**絕對位置** gate(不是角度誤差):電流超標時看卡死的絕對位置 ——
`final_pulse ≥ preset − OBSTACLE_ENDPOINT_GATE_CM(1.5 cm)` 判定為壓牆 endpoint(defer,不中斷其他 slave);
小於這個 gate 才是真障礙物。
📌 **2026-09-11 橫桿問題是同一課的反例**:那次連「位置」都分不出來(長滾筒偏心 pivot 讓 θ 重疊),
最後放棄偵測改低力刷過 —— **先確認訊號物理上分得出來,再設計判據**。

### 2.2 Motion parallax:反光/雜亂場景的視覺突破

反光玻璃 + 雜亂場景下,純 OpenCV 單張分析與 NPU 預訓練模型(訓練的是真鋁窗框,對木條 0 detection)
**都失敗**;stereo disparity 對鏡面也失效(兩台相機看到同一個反射場景)。
🎯 **解法是 motion parallax**:機體與場景相對位移 1 cm 時,真實牆面物體(距離 ~20 cm)影像移位
10–20 px(**強 parallax**),反射的虛擬場景(虛擬深度 2–5 m)移位 <1 px(**弱 parallax**)。
optical flow magnitude 用 `median × 2.5` 當門檻就能自然分區,**不用標註資料不用訓練**。
📌 **遇到反光/雜亂場景,不要先浪費時間試純 OpenCV 或 NPU 預訓練模型。**

### 2.3 EVT 廣播 / PausedOnError 巢狀(兩個必抄的 guard)

- 🔴 **EVT 誤當 reply**:吊機的 `broadcast()` 會把 `EVT` 行送給所有 client,本體在等 RPC reply 時
  可能先收到一行 `EVT …` 而誤判。修法(`crane_cmd_` 已實作):receive loop 看到開頭 `"EVT "`
  就 `handle_crane_evt_(line)` 後 **`continue` 不 `return`**,繼續等真正的 OK/ERR。
  **任何新寫的 RPC reader 都要照抄這個 filter。**
  ⚠️ 2026-09-09 又咬到同族問題:`crane_cmd_` 收包路徑**沒有行緩衝**,會把 `EVT` 廣播的**半行殘片**
  當成指令回覆(見待辦總表 🔴🔴)。filter 只擋整行,擋不住半行。
- 🔴 **nested await guard**:`await_user_intervention_()` 進入時記 `state_before_pause_`。
  若某 code path 沒走它、**直接手動 `set_state_(PausedOnError)`**,之後真正呼叫
  `await_user_intervention_` 的地方會把 `state_before_pause_` 覆寫成 `PausedOnError`
  → `cmd_continue` 把 state 設回 `PausedOnError`,while loop 永遠跑,retry/skip 看起來壞掉。
  修法:`if (prev != State::PausedOnError) state_before_pause_ = prev;`
  **任何新增的「直接 set PausedOnError」路徑都要保留這個 guard。**

### 2.4 共用匯流排的序列化:鎖要加在**業務邏輯層**

`TCP_client::socket_mtx` 只保護**單次** send/recv,**不保護「送出請求→等對應回覆」整個交易** ——
多執行緒各自送請求各自等回覆,會收到對方的回覆。修法一律是加一把**業務層** mutex 包住整段交易:
- v1 `dm2j_motion_mtx_` 序列化 `cli_20_` 上的 DM2J motion;
- v2 `zdt_bus_mtx_` 序列化 `cli_20_` 上的 4 顆 ZDT(背景 `feet_topup_unsealed_` async vs 主執行緒)。
  ⚠️ **這是新鎖不是重用** —— `dm2j_motion_active_` 只是給 arm sweep 監控用的 atomic 旗標,**不是互斥鎖**。

🔴 **任何新加的「會持 bus > 100 ms」操作都要用這類 mutex 包住整段交易**,背景與主執行緒都要遵守。
⚠️ 同一 thread 對同一個 `std::mutex` **不能重複 lock**(非 reentrant)——
`cmd_attach` 呼叫 `do_feet_realign_` 內部又 lock 同一把鎖曾經死當,
修法是 `std::unique_lock` 手動 unlock 再呼叫、呼叫完再 lock 回來。

### 2.5 重心校正流程(v1 完整實作,v2 尚未搬移)

v1 `balance_calibration` 是 5 階段:preload(拉到目標張力)→ release_body(解真空 + 兩段式縮回)→
release_feet_center → free_hang_settle(3 s)→ balancing(IMU 閉環,連續 motor + 50 ms inner poll)。
三種結束路徑都回 Idle(RECORD 保存 offset / ABORT / 中途 ABORT),**真正系統錯誤才進 PausedOnError**。
入口雙門檻 `|roll| ∈ [0.5°, 15°]`(太平不用校、太歪太危險)。
🔴 **v2 重做時預期走 IMU + 吊機差動**(與 level-match 共用感測器),**不是照抄 v1**
—— v1 靠解開 body 真空讓機體自由懸掛,v2 沒有 body 吸盤分組。

### 2.6 同一個限制有兩層:指令層守衛 + 驅動層守衛(2026-09-17)

上滑台行程守衛在**兩個地方**:`cmd_rail_move` 的 `ARM_RAIL_TRAVEL_MIN/MAX_CM`(指令層,回 `ERR rail_target_out_of_range`)
與 `DM2J_RS570::set_travel_limit_cm()`(驅動層,只 `LOG_ERR … REJECTED`、指令回覆只剩 `ERR rail_move_command_failed`)。
09-17 上午「上滑台可設負值」只放寬指令層,驅動層開機仍設 `[0,130]` ⇒ 負數全被擋,GUI 看不出原因,要到 Pi 上翻 log 才知道。
- 🔴 改任何「範圍」之前先 `grep` 這個範圍的**所有**出現點(指令層 / 驅動層 / GUI 軟限位 / fake),三層一起改。
- 驅動層的拒絕**要能從指令回覆看出來**——現在仍只在 log(🟡 待改:`ERR rail_move_command_failed` 後面帶原因)。

---

## 3. 方法論教訓(吃過虧才學到的)

| 教訓 | 出處 / 應驗 |
|---|---|
| 🔴 **公式邏輯對 ≠ 數字準。常數本身是實體量測值,必須跟現場皮尺交叉驗證** | 深度相機 `LEAD_OFFSET` 量成 16 實際 32 cm、`STANDOFF` 50→56。**已重複應驗多次**:2026-08-28 上滑台導程差 7.7 倍、08-31 推桿 `CUP_PULSE_PER_CM`。⚠️ 現仍有未實測常數:`TOOL_EXT_CENTER=160`(且是 `wall_mm` 錨點)、樞軸→玻璃四個幾何常數 |
| 🔴 **修完「髒值/異常輸入」的 bug 不能只加 sanity check 就結束** —— 要把整條計算鏈(方向公式、上下游目標值、硬體方向對應)**用事故當下的真實數字重新代入驗算**,明確講清楚是「邏輯沒問題、輸入被污染」還是「邏輯也有問題」 | 2026-07-14 SD76 髒值(`3.37e7`)讓方向算反 → IMU emergency abort(49.6° 傾斜)。驗算後確認純輸入污染 |
| 🔴 **反覆出現的同一症狀,值得往下查根因,不要在每個函式各自補檢查** | 「vel/tau 有讀值但 pos 完全凍結」在 `go_home_slot`/`lr_move_to_slot_impl`/`lr_calibrate_slot` **三個函式都遇到** ⇒ 暗示 M2 有間歇性硬體層級問題。**這條至今未解**(待辦總表「M1 passive 根因自 08-17 未解」) |
| **改共用函式要考慮所有呼叫路徑** | `go_home_slot()` 加的 passive-probe 邏輯被 PARK 與 DEPLOY 的 M1 retract **無條件套用**,物理上真的彈了一下 → 撤回 |
| **靜默失敗要變成看得到的 ERR** | `cmd_deploy_sequence()` Step 3 的 `wait_for_move(m1_)` 回傳值被丟掉("best-effort"),不管有沒有到位都回 OK ⇒ 解釋了「距離越跑越遠」。同模式在 `lr_calibrate_slot()` 也發生過(`void` 回傳、三種失敗都靜默 return) |
| **先猜新參數不如換用已驗證可靠的路徑** | M2 PARK 卡 18 秒,第一版假說猜 CAN passive latch,port 偵測邏輯**沒用**;真因是 `go_home_slot()` 的 kp(≈2.5)比 `lr_move_to_slot_impl()`(`MIT_KP=28` + 摩擦前饋)**差 10 倍以上**。修法是讓 PARK 也走已驗證的那條,不是繼續猜 kp/kd |
| 🔴 **移除看似獨立的功能前,一定要全 repo 搜一次相關字串** | 拔 Easy Crane 時才發現牽連到核心的張力 fallback(`read_rope_weight_max_kg_()` 的第三層),**事前沒有任何紀錄提過這個關聯** |
| 🔴 **斷言的對象要是「使用者看到的東西」,不是「我設了什麼」** —— 綠燈證明的是屬性寫進去了,不是它真的生效 | 2026-09-15 v3 GUI 中了兩次:`el.hidden = true` 明明成立,但 CSS 的 `.ebanner{display:flex}` / `.grp > div{display:flex}` 把它蓋掉 ⇒ 元素照樣看得見(症狀:兩條鎖定橫幅在沒鎖時恆亮、前置卡收起後打不開),而驗收只讀 `.hidden` 所以**一路綠燈**。改法:斷言 computed display,並加一條全頁掃描「任何帶 hidden 的元素 computed display 必須真的是 none」。**同族**:`el.disabled` vs `pointer-events:none`、`textContent` 有值 vs 元素在畫面外、Modbus 寫入回成功 vs 繼電器真的動作(見 1.3 與破真空閥 0ms 那條) |
| 🔴🔴 **通道自癒只能重連,不能重送** —— 「收不到回覆就重送」對任何非冪等指令都等於**重跑**:指令已經離開 socket,對方可能做了、做到一半、或中途斷電,本機一無所知 | 2026-09-16 `crane_cmd_` 的殭屍 socket 自癒(2026-05-15)把 recv timeout 也當可重試:`goto 0` 下行中 user 為維修**斷吊機電**,105 s 後 timeout 到、吊機剛上電、本體重連並**原樣重送 `goto 0`**,吊機在人旁邊自己開始放繩。`arm_cmd_` 早在 06-03 就為 DEPLOY/PARK 定了「timeout 不重送」,`crane_cmd_` 從沒套用 ⇒ **同一類錯誤修一條通道時要掃所有通道**。正解:timeout 只 `close()` 讓**下一條**指令重連;要重送必須由呼叫端在知道現場狀態後決定。另一條:**斷電維修前先確認本體沒有卡著等回覆的運動指令**,最乾淨是本體一起斷電 |
| 🔴 **「同時到位、位置沒變、電流很低」= 同步啟動沒送到,不是堵轉** —— 廣播幀沒有回覆、沒有重試,匯流排掉一包就整組不動,而 wait 迴圈把「沒在動」讀成「到位」 | 2026-09-17 急停收腳:四支 ZDT 1.05 s 同時 done、位置 ~1160°、~500 mA(堵轉 2–3 A),同瞬間同匯流排 PQW 回讀 size=0。修:FAKE-DONE 且非堵轉 → 對沒到位的重送絕對目標一次(冪等) |
| 🔴 **超範圍要拒絕,不要夾到上限** —— 夾到上限等於「無限制」,而呼叫端以為有限制 | 2026-09-17 `arm_deploy_f … 500` mm 被夾成 θ 1.10(=不限),測邊界值時手臂真的壓了牆一次。測邊界前先確認回覆是拒絕再送 |
| **systemd unit 檔裡 `%` 是 specifier**(`%s` → `/bin/bash`),`printf '%s\n'` 會靜默變成別的字串 | 2026-09-17 ExecStartPost 回放設定檔靜默失敗;要嘛 `%%`,要嘛用 `echo` |
| **出廠 9600 的 RS485 裝置在 115200 匯流排上是隱形的**,掃位址掃不到 ≠ 沒接 | 2026-09-17 第二顆 XKC:網關 web 暫切 9600 → 改位址/鮑率(兩次寫入要隔 ≥1 s)→ 切回 |
| **機構層級的硬性要求不能為實作方便讓步** | sync 步伐曾想「一側伸完再伸另一側」以套用早停邏輯,被否決 ——「兩邊腳組一定要一起放」。正解是回頭改底層共用函式(`stop_group_ids` 每組獨立早停),而非犧牲同時性 |

---

## 附:ONBOARDING 各章去向

| 原章 | 去向 |
|---|---|
| §1 專案總覽 | → `SOFTWARE.md` §1(現況權威) |
| **§2 驅動踩坑** | **→ 本檔 §1**(未收錄者);已收錄者見 `summaries/` 對應裝置 |
| **§3 工程方法** | **→ 本檔 §2** |
| §4 v2 機械差異(v1↔v2 對照表) | 🔴 **無可替代,留在歸檔原檔**;硬體現況見 `HARDWARE.md` |
| §5 MH300 遷移 | → `summaries/MH300_INVERTER_MODBUS_SUMMARY.md` + 待辦總表 Phase 3 |
| §6 步態引擎演進(6.1–6.7) | 🔴 **無可替代,留在歸檔原檔** —— `motion_flow.md` 仍是 v1 快照(見待辦總表「文件脫節」) |
| §6.8 手臂除錯教訓 | **→ 本檔 §3** |
| §7 吊機通訊 hardening | **→ 本檔 §3**(方法論);三個 bug 本身已修 |
| §8 深度相機 | ⚰️ 整套移除(2026-09-01);**§8.2 的教訓 → 本檔 §3** |
| §9 Easy Crane 退役 | **→ 本檔 §3**(全 repo 搜字串那條) |
| §10 Web GUI 演進 | v1 `public/` 已停機;v2 `public_v2/` 為另一套 → 見 `SOFTWARE.md` §5 |
| §11 保留但未重做的功能 | 🔴 **重要決策,已提升到 `SOFTWARE.md` §2.3**(`removed_in_v2` 裡有幾條是**暫時 stub 不是廢棄決定**) |
