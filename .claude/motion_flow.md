# washrobot_new_PI 動作流程規格書

> 本文件為 `washrobot_new_PI/main.cpp` 主程式重寫的依據。
> 建立日期：2026-04-12
> 狀態：規格確認中 / 程式尚未重寫

---

## 1. 系統定位

垂直玻璃牆面清洗機器人，由頂樓吊機用鋼索吊掛，由上往下**尺蠖式爬行**清洗。

- 爬行方向：**從頂樓往下**
- 附著方式：9 顆真空吸盤（透過 dp0105 泵浦 + VT307 電磁閥 + ZDT 推桿）
- 水平清洗：DM2J 上滑台（slave 5）帶動機械手臂左右來回
- 重力承載：頂樓吊機鋼索（主動放繩配合下移）
- 姿態監控：WT901BC 九軸姿態儀（USB→TTL）

---

## 2. 硬體對照速查

### RS485_1 @ 192.168.1.20 — DM2J_RS570 步進 × 5

| Slave | 用途 | 走路循環 |
|---|---|---|
| 1 | 左腳（linear rail: 腳 ↔ 身體相對位置）| ✓ |
| 2 | 左輪（裝於靠牆面）| Phase 1: 放輪爬牆；Phase 2: 收輪 |
| 3 | 右腳 | ✓ |
| 4 | 右輪（裝於靠牆面）| Phase 1: 放輪爬牆；Phase 2: 收輪 |
| 5 | 上滑台（機械手臂平台，左右清洗動作）| ✓ |

### RS485_2 @ 192.168.1.21 — ZDT 閉環步進 × 9（SMC LEYG25 推桿）

| Slave | 用途 | 吸盤所屬組 |
|---|---|---|
| 1, 2 | 左腳推桿 × 2 | 腳組 |
| 3, 4 | 左身體推桿 × 2 | 身體組 |
| 5, 6 | 右腳推桿 × 2 | 腳組 |
| 7, 8 | 右身體推桿 × 2 | 身體組 |
| 9 | 中心推桿 × 1 | 中心組（獨立）|

推桿姿態定義：
- **收合位置 = 0 cm**（吸盤離開牆面）
- **貼牆位置 = 10 cm**（吸盤接觸牆面，可真空吸附）

### RS485_3 @ 192.168.1.22 — 感測 / 繼電器

| Slave | 裝置 | 用途 |
|---|---|---|
| 1~9 | JC_100_METER × 9 | 9 顆吸盤的真空壓力回授 |
| 10, 11 | DY_500_weight_sensor × 2 | 鋼索重量感測（washrobot 端總重）|
| 12 | PQW_IO_16O_RLY（8CH）| 真空控制繼電器 |

### RS485_crane @ 192.168.1.30 — 吊機端（Crane RPi 192.168.1.101）

| Slave | 裝置 | 用途 |
|---|---|---|
| 1 | ZS_DIO_R_RLY（8CH）| 左右絞盤繼電器（CH1 左收 / CH2 右收 / CH3 左放 / CH4 右放）|
| 2 | SD76 #1 | 左鋼索計米 |
| 3 | SD76 #2 | 右鋼索計米 |
| 4 | SD76 #3 | 中間管線（水管+電線）計米 |
| 5 | DSZL-107 #1 | 左鋼索張力感測（TBD Modbus 暫存器表）|
| 6 | DSZL-107 #2 | 右鋼索張力感測（TBD Modbus 暫存器表）|
| 7 | CLV900 變頻器 | 中間絞盤馬達控制（已實作 2026-04-13）|

### 絞盤機械規格

- 鋼索直徑：**6 mm**
- 斷電行為：**自動剎車**（電磁剎車，失電即夾持）
- 影響：全斷電時機器人脫離牆面（吸盤鬆）但仍由剎車懸吊，需人工解鎖剎車或恢復電源才能回收

### 本體水系統流向

```
頂樓水源 ──(水管，經中間管線)──▶ CH7 進水球閥 ──▶ 10L 水箱 ──▶ CH6 泵浦 ──▶ 機械臂噴頭 ──▶ 牆面 ──▶ 自然流下至地面
```
- 水箱溢流處理（浮球/溢流孔）待確認 → Open Q10
- 清洗時 CH7 持續 ON 補水、CH6 持續 ON 出水

---

## 3. PQW 8CH 繼電器接線

```
[B 組電源 DC 24V]
      │
      ▼
┌──────────────────────────────────────────────────┐
│ PQW 繼電器 (slave 12 @ 192.168.1.22)              │
│                                                  │
│  CH1 ──▶ dp0105 真空泵浦                          │
│            (Init 開啟，全程 ON)                    │
│                                                  │
│  CH2 ──▶ VT307 電磁閥 ─ 腳組 (4 cups)              │
│            └─ 左腳 ×2 (ZDT 1,2 末端)               │
│            └─ 右腳 ×2 (ZDT 5,6 末端)               │
│                                                  │
│  CH3 ──▶ VT307 電磁閥 ─ 身體組 (4 cups)            │
│            └─ 左身體 ×2 (ZDT 3,4 末端)             │
│            └─ 右身體 ×2 (ZDT 7,8 末端)             │
│                                                  │
│  CH4 ──▶ VT307 電磁閥 ─ 中心 (1 cup)               │
│            └─ 中心 ×1 (ZDT 9 末端) — 獨立控制      │
│                                                  │
│  CH5 ──▶ 手臂刷洗滾筒馬達                          │
│            (裝於上滑台機械臂上，清洗時旋轉)         │
│                                                  │
│  CH6 ──▶ 水箱泵浦                                 │
│                                                  │
│  CH7 ──▶ 水箱進水球閥（頂樓水壓 → 水箱補水）         │
│                                                  │
│  CH8 ─── 保留                                    │
└──────────────────────────────────────────────────┘

[電磁閥邏輯]
  CH = OFF → VT307 不通 → 吸盤對大氣（無吸力）
  CH = ON  → VT307 通   → 吸盤接負壓（吸附）
```

---

## 4. 完整動作流程

### Phase 1 — 部署（人工 + 吊機操控）

```
1. 頂樓吊機放下 2 條鋼索 + 1 條中間管線（水管+電線）到地面
2. 地面人員把機器人本體接上鋼索 / 電源 / 網路 / 水管
3. 【地面自檢】Web GUI 按「連線自檢」
   - 連線 4 路 Modbus-TCP（.20 / .21 / .22 / .30）全數 OK
   - 驅動 init：DM2J×5 / ZDT×9 / JC-100×9 / PQW / ZS_DIO / SD76×3
   - （DSZL-107 / 變頻器 驅動完成後一併納入）
   - 任一失敗 → 回報，禁止繼續
4. 【第 1 次歸零：地面起點】
   - Web GUI 按「鋼索歸零（起點）」→ 送 `zero_meters ground`
   - Crane 把 SD76 #1 / #2 / #3 三計米器同時歸零
5. 機器人放下左右輪（DM2J slave 2, 4）
6. 吊機收繩，把機器人拉至頂樓玻璃位置，4 輪貼近大樓玻璃面
7. 【第 2 次歸零：洗窗起點】
   - Web GUI 按「鋼索歸零（到位）」→ 送 `zero_meters top`
   - Crane 讀當前 |SD76| 存成 `home_ground_cm`（= 從頂樓放回地面所需的繩長）
   - 再把 SD76 × 3 歸零（後續 Phase 4 下移距離從玻璃頂開始計）
   - 未執行 `zero_meters top` 禁止進入 Phase 2
```

### Phase 2 — Init 指令（精簡：連線 + 驅動 init 已在 Phase 1 地面自檢完成）

```
1. CH1 = ON  （dp0105 泵浦啟動）
2. CH2 = OFF
3. CH3 = OFF
4. CH4 = OFF  （全部吸盤無吸力）
5. CH5 = OFF  （刷子停）
6. CH6 = OFF  （水泵停）
7. CH7 = OFF  （進水閥關）
8. DM2J slave 2, 4 → 0（收回左右輪）
   輪子裝於靠牆面；Phase 1 展開讓吊機把機器人沿玻璃拉上樓。
   進 Phase 2 要先收輪，ZDT 推桿才能把吸盤伸出到玻璃面。
   前置假設：Phase 1 展開前輪子已 zero 於「完全收回」位置。
9. ZDT 1~8 → 10 cm（腳 + 身體推桿都伸出預備吸附）
   ZDT 9（中心）暫不伸出
10. 上滑台（DM2J slave 5）回零
11. WT901BC 姿態儀 baseline 採樣 3 秒 → 得 roll0 / pitch0
12. 輸出「Init 完成，等待貼牆」
```

### Phase 3 — 吸附啟動（人工貼牆後下指令）

```
1. 中心 ZDT 9 → 10 cm
2. CH2 = ON
3. CH3 = ON
4. CH4 = ON
5. 等 VACUUM_SETTLE_MS（預設 2000 ms）讓電磁閥通氣、9 顆吸盤壓力穩定
6. 檢查 JC-100 (1~9) 全部達閥值 → 回報 OK
   任一失敗 → 回報人工，不進入 Phase 4
```

### Phase 4 — 下移洗牆循環（自動）

**參數（固定）**
- 單步步長：`STEP_CM = 30`（或 40，固定值）
- 總下移距離：`TOTAL_DISTANCE_CM = ?`（需填）
- 最大步數：`MAX_STEPS = TOTAL_DISTANCE_CM / STEP_CM`

**A. 腳組下移（身體固定為支撐）**
```
1. CH2 = OFF（腳組鬆）
2. ZDT 1,2,3,4 → 0（腳推桿收回）
3. **吊機先放繩 STEP_CM + STEP_MARGIN_CM**（多放 margin 確保鋼索鬆出）
   → 送 crane `pay_out (STEP_CM + STEP_MARGIN_CM)` → 等 `OK`
4. DM2J slave 1 (左腳) 與 slave 3 (右腳) 同步下移 STEP_CM
   → 等兩腳都回報到位
5. **吊機收繩 STEP_MARGIN_CM**（回收餘量，恢復正常張力）
   → 送 crane `retract STEP_MARGIN_CM` → 等 `OK`
6. ZDT 1,2,3,4 → 10 cm（腳推桿伸出貼牆）
7. CH2 = ON
8. 檢查 JC-100 (1,2,3,4) 真空度
   不足：retry_count++; 回 step 1
   成功 5 次仍失敗 → 停機回報人工
```

**B. 身體組下移（腳固定為支撐）**
```
1. CH3 = OFF + CH4 = OFF（身體 + 中心鬆）
2. ZDT 5,6,7,8,9 → 0（身體 + 中心推桿收回）
3. DM2J slave 1, 3 同步（反向）→ 身體相對腳下移 STEP_CM
4. ZDT 5,6,7,8,9 → 10 cm
5. CH3 = ON，CH4 = ON
6. 檢查 JC-100 (5,6,7,8,9) 真空度
   不足：retry_count++; 回 step 1
   成功 5 次仍失敗 → 停機回報人工
```

**C. 清洗（一次 A+B 後執行）— 水 + 刷**
```
1. CH7 = ON（水箱進水閥開，頂樓水壓補水到本體水箱）
2. CH6 = ON（水箱泵浦啟動，把水箱的水打到機械臂噴頭）
3. CH5 = ON（手臂刷洗滾筒旋轉）
4. 上滑台 (DM2J slave 5) 由左端 → 右端（速度/行程參數可調）
5. 上滑台 右端 → 左端
6. CH5 = OFF（刷子停）
7. CH6 = OFF（泵浦停）
8. CH7 = OFF（進水閥關，停補水）
9. 完成一次清洗循環
```

**D. 循環**
```
- 若 step_counter < MAX_STEPS → 回到 A
- 若達 MAX_STEPS → 輸出完成訊息，停機
- 任意時刻人工按停止鍵 → 安全停機（當前動作完成後停）
```

### Phase 5 — 平衡校正模式（Roll 軸，吊機左右鋼索差動）

**觸發條件：** `balance_deg > IMU_ASK_DEG` (15°) 且使用者回覆 Yes 同意執行。

**校正對象：僅 Roll**（左右歪）。Pitch（前後傾）吊機動不了，不做自動校正，超標時也一併交給使用者在詢問階段決定要不要進 Phase 5；若使用者選 No 則忽略繼續。

```
前置：當前 step 已完成，機器人在穩定姿態。

1. CH2 = OFF, CH3 = OFF, CH4 = ON
   （僅中心吸盤吸住，8 顆腳+身體鬆開，留出校正自由度）
2. 讀 WT901BC 取得當下 roll
3. Δroll = roll - roll0
   Δroll > 0 → 左側高 / 右側低 → 左鋼索放 / 右鋼索收
   Δroll < 0 → 相反
4. 吊機執行差動：
   - 單次微調量：|Δroll| × ROLL_CORRECT_CM_PER_DEG（預設 1.0 cm/度，實測調整）
   - 一邊放、一邊收，或單邊放另一邊不動（依實際鋼索張力決定）
5. 停 500 ms 讓姿態穩定，重讀 roll
6. 若 |Δroll| < IMU_HYSTERESIS_DEG (1°) → 校正完成
   否則回 step 3，最多 ROLL_CORRECT_RETRY_MAX (5) 次
7. 超過重試上限仍失敗 → 停機回報人工
8. 完成後依序：CH3 = ON → CH2 = ON（恢復全吸附）→ 繼續 Phase 4
```

**注意：** 校正過程中中心吸盤必須持續吸住（唯一支撐點），任何時候 JC-100 (slave 9) 真空度不足 → 立即停機。

### Phase 6 — 召回回地面（Return Home）

**觸發：** 使用者在 `attached` / `paused` / `error` 狀態發 `return_home`。
**前置：** 若當下為 `running` / `balancing`，需先 pause（系統自動轉 paused 後再執行）。

```
1. 上滑台 (DM2J slave 5) 回零
2. 關閉水系統
   CH5 OFF（刷子停）、CH6 OFF（水泵停）、CH7 OFF（進水閥關）
3. 破真空（吸盤脫附）
   CH2 OFF、CH3 OFF、CH4 OFF
4. 等 RETURN_VACUUM_RELEASE_MS（預設 5000 ms）讓 9 顆吸盤完全脫離玻璃
5. 檢查 JC-100 (1~9) 全數讀值回升至近大氣壓 → 確認已脫附
   任一仍低於閥值（還黏住）→ 停機回報人工，不繼續放繩
6. 收回推桿 ZDT 1~9 → 0 cm
7. 關閉泵浦 CH1 OFF
8. 計算放繩量：descent_cm = home_ground_cm - current_down_cm
9. 吊機一次放繩 descent_cm
   → 送 crane `pay_out <descent_cm>`
   → 中間絞盤變頻器同步（× MIDDLE_WINCH_RATIO_K）
10. 全程監控（違規 → 立即 crane stop + 停機回報人工）
    - 姿態 balance_deg > 45° → 停
    - 張力異常（低/高/左右差）→ 停
    - watchdog 失聯 → 停
11. 到位（SD76 累積 = descent_cm）→ 回報「已回地面」
    → state 轉為 `idle`（需從頭重新部署才能再作業）
```

**設計重點：**
- **關真空 → 等 5 秒 → 收推桿** 的順序不可變更：真空未洩時硬收推桿會讓 LEYG25 被負壓拉壞
- **水系統最先關**：避免召回中還在噴水
- **脫附失敗停機**：保護機體，不做「靠鋼索硬拉脫離」
- **召回後 state = idle**（不是 attached）：機器人已不在牆上，需從 Phase 1 重部署

---

### 機器人生命週期狀態（Washrobot 端維護）

```
disconnected    TCP 或任一 Modbus 斷線
idle            啟動後 / 尚未自檢
ready           地面自檢 + Phase 1 歸零 + Phase 2 Init 完成，等 attach
attached        Phase 3 完成，9 顆吸盤全吸住，可下移
running         Phase 4 執行中（不論 manual 單步或 auto 連續）
waiting_confirm 收到 balance_ask，等使用者 confirm_balance
paused          pause 指令 或 感測器異常自動暫停
balancing       Phase 5 Roll 校正執行中
returning_home  召回回地面中
error           硬故障，僅允許 status / reset
```

**狀態轉移規則（擇要）：**
```
idle   ── (地面自檢+zero_ground+放輪+拉上樓+zero_top+init OK) ──▶ ready
ready  ── (attach) ──▶ attached
attached ── (step_down / run N) ──▶ running
running ── (N 步完成) ──▶ attached
running ── (balance_deg>IMU_ASK_DEG, step 結束) ──▶ waiting_confirm
waiting_confirm ── (confirm yes) ──▶ balancing
waiting_confirm ── (confirm no)  ──▶ running（繼續剩餘步數）
balancing ── (收斂 or 超重試) ──▶ attached（收斂後自動接回剩餘 run）
任何狀態 ── (pause) ──▶ paused（記錄 prev_state）
paused ── (resume) ──▶ prev_state（若 prev=running 繼續剩餘步數）
任何狀態 ── (return_home) ──▶ returning_home
任何狀態 ── (嚴重錯誤) ──▶ error
```

**每次狀態轉移推播：** `EVT state_changed <old> <new>`

**status 指令回傳包含 `state=<current_state>`**

### 操作模式（manual vs auto）

**不另設 mode flag**，由指令直接表達：

| 指令 | 模式 | 行為 |
|---|---|---|
| `step_down` | manual | 執行 1 次 A+B+C → 自動回到 `attached` 等下次指令 |
| `run <N>` | auto | 連續執行 N 次，期間狀態維持 `running`，完成後回 `attached` |

**pause / resume：**
- `pause` 任何時候可發，`state` 轉為 `paused` 並記錄 `prev_state`
- `resume` 回到 `prev_state` 從暫停點繼續：
  - prev = `running` → 繼續剩餘步數
  - prev = `balancing` → 繼續校正重試
  - prev = `returning_home` → 繼續召回
- `emergency_stop` 無論何時直接中止，一律轉 `error`（需 `reset` 才能恢復）

**指令在各狀態的可用性（部分）：**

| state | attach | step_down/run | pause | resume | confirm_balance | return_home | reset |
|---|---|---|---|---|---|---|---|
| idle | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ |
| ready | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ |
| attached | ✗ | ✓ | ✗ | ✗ | ✗ | ✓ | ✓ |
| running | ✗ | ✗ | ✓ | ✗ | ✗ | ✗ | ✗ |
| waiting_confirm | ✗ | ✗ | ✓ | ✗ | ✓ | ✗ | ✗ |
| paused | ✗ | ✗ | ✗ | ✓ | ✗ | ✓ | ✗ |
| balancing | ✗ | ✗ | ✓ | ✗ | ✗ | ✗ | ✗ |
| returning_home | ✗ | ✗ | ✓ | ✗ | ✗ | ✗ | ✗ |
| error | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ |

未列的指令（`status`、`ping`、`vacuum`、`move`、`pusher`、`tilt_mode`）通常不受狀態限制（除 running/balancing 互斥外）。

---

## 5. 失敗處理

**實體安全層（不經 MCU）：**
- **E-stop 實體按鈕** 由頂樓吊機人員判斷並手動觸發，**硬斷絞盤電源迴路**（不經 RPi / Modbus / 軟體）
- 軟體端看到絞盤突然失電會透過 watchdog / 張力歸零等間接觀察到，但**不負責觸發 E-stop**

**斷電即脫離設計原則：**
- 本系統**不做吸盤被動保持**（不加止回閥 / 真空罐 / UPS）
- 任何斷電情況下：dp0105 泵浦失壓 → 9 顆吸盤同時鬆開 → 機器人從牆面脫離 → 由吊機鋼索吊著
- **吊機絞盤斷電時自動剎車**（電磁剎車失電夾持）→ 機器人脫離牆面後停留在當前高度懸吊，不會墜落也不會繼續下滑
- 頂樓人員發現異常時，**按 E-stop 斷電** → 機器人自動脫離牆面（由剎車懸吊） → 恢復電源或手動解鎖剎車後，再由吊機將其拉回頂樓
- 此設計犧牲「斷電時保持牆面位置」以換取「異常時可靠脫離 + 回收」
- 軟體層不需要處理「斷電後機器人還吸在牆上」的恢復情境
- **注意：** Modbus-TCP 斷線時 DM2J/ZDT 驅動卡本地會繼續執行既有指令到完成（軟體無法中止），最終仍靠頂樓 E-stop 這層保底


| 失敗類型 | 處理 |
|---|---|
| 真空吸附不足 | 重試 5 次 → 超過即停機回報人工 |
| DM2J / ZDT 驅動回報錯誤 | 立即停機，回報 error code |
| TCP 斷線（washrobot ↔ crane）| **動作中** → crane 端立即停繼電器 + 變頻器 stop + EVT；**閒置中** → log 並重連 |
| Ping 超時（> WATCHDOG_TIMEOUT_MS）動作中 | 視同 TCP 斷線，立即停 |
| Ping 超時 閒置中 | log EVT，不動作 |
| Modbus-TCP 斷線（USR-TCP232-304）| TCP_client 自動重連（監控執行緒）|
| 人工緊急停止 | 完成當前 ZDT/DM2J 動作後停於安全姿態 |
| 鋼索張力過低（< TENSION_MIN_KG）| 疑似鬆弛 / 斷裂 → 立即 crane stop + washrobot pause |
| 鋼索張力過高（> TENSION_MAX_KG）| 疑似卡住 / 超載 → 立即 crane stop + 回報人工 |
| 左右張力差 > TENSION_DIFF_MAX_PCT | 不平衡 → pause + warning |
| DSZL-107（crane 端）vs DY-500（washrobot 端）同側差 > `TENSION_CROSS_DIFF_KG` | 疑似鋼索中段卡住 / 感測異常 → pause + warning，持續超標 → stop **（暫不啟用，`ENABLE_CROSS_TENSION_CHECK=false`）** |
| 姿態平衡偏移 `balance_deg > IMU_ASK_DEG` (15°) | **當前 step 完成後暫停** → EVT 推播詢問「是否執行平衡校正」→ 使用者 yes 進 Phase 5 / no 忽略繼續 |
| 姿態平衡偏移 `balance_deg > IMU_EMERGENCY_DEG` (45°) | 不詢問，立即停機 + crane emergency_stop + 回報人工 |

**平衡指標定義：**
```
balance_deg = max( |roll - roll0|, |pitch - pitch0| )
```
- `roll0 / pitch0` 為 Phase 2 Init 時取 3 秒平均得到的 baseline
- Roll = 左右歪（繞牆面法線 X 軸），Pitch = 前後傾（繞水平沿牆 Y 軸）
- Yaw 不監控（貼牆狀態不會自轉 + 磁力計有漂移）
- 坐標系：**X 垂直於牆面（法線朝外）/ Y 水平沿牆 / Z 朝上**

**共通判定規則：** 所有感測器門檻判定採 1 秒滑動平均 + 持續 500 ms 超標才觸發（避免瞬時雜訊誤觸發）。

---

## 6. 可調參數（寫成 const 變數置於 main.cpp 頂部）

| 參數 | 預設 | 說明 |
|---|---|---|
| `PUSHER_EXTEND_CM` | 10 | ZDT 推桿貼牆位置 |
| `STEP_CM` | 30 | 單步下移距離 |
| `STEP_MARGIN_CM` | 15 | 吊機先放繩的額外 margin，確保 DM2J 腳下移時鋼索有足夠鬆量；DM2J 完成後吊機再收回這段 |
| `TOTAL_DISTANCE_CM` | TBD | 總下移距離（一次洗到底）|
| `VACUUM_RETRY_MAX` | 5 | 吸附重試上限 |
| `VACUUM_THRESHOLD_KPA` | -50 | 真空度成功閥值（實測最佳 -70，最差 -50，取最差值當門檻）|
| `VACUUM_SETTLE_MS` | 2000 | 閥開後等電磁閥通氣 + 吸盤壓力穩定的時間（Phase 3 吸附、Phase 4 cycle_group）|
| `DM2J_RPM` | 700 | 腳移動速度 |
| `ZDT_SPEED` | 1000 | 推桿速度（RPM）|
| `ARM_SWEEP_RANGE_CM` | TBD | 上滑台清洗行程 |
| `ARM_SWEEP_RPM` | TBD | 上滑台速度 |
| `TENSION_MIN_KG` | 0.5 | 鋼索張力下限（低於 → 疑似鬆弛/斷裂）|
| `TENSION_MAX_KG` | 3.0 | 鋼索張力上限（高於 → 疑似卡住/超載）暫定，實測調整 |
| `TENSION_DIFF_MAX_PCT` | 30 | 左右張力差容忍百分比（DSZL-107 左 vs 右）|
| `TENSION_CROSS_DIFF_KG` | 1.0 | DSZL-107 vs DY-500 同側張力差容忍（暫定，實測調整）|
| `ENABLE_CROSS_TENSION_CHECK` | false | 是否啟用雙點張力冗餘比對（DY-500 硬體問題排除後改 true）|
| `MIDDLE_WINCH_RATIO_K` | 1.00 | 中間絞盤同步補償係數（中間放繩 cm = 左右放繩 cm × k）|
| `MIDDLE_WINCH_RPM` | TBD | 中間絞盤變頻器放繩基礎轉速 |
| `IMU_ASK_DEG` | 15.0 | 平衡偏移詢問門檻 → 當前 step 完成後暫停並詢問使用者是否執行 Phase 5 |
| `IMU_EMERGENCY_DEG` | 45.0 | 平衡偏移立即停機門檻（不詢問，直接 crane emergency_stop + 人工處理）|
| `IMU_BASELINE_SEC` | 3 | Phase 2 Init 時取 baseline 的平均秒數 |
| `IMU_HYSTERESIS_DEG` | 1.0 | 詢問解除遲滯（回到 ASK - 1° 以下才清 flag），亦作為 Phase 5 校正收斂判定 |
| `ROLL_CORRECT_CM_PER_DEG` | 1.0 | Phase 5 單次校正量換算（每度偏差對應鋼索差動 cm），實測調整 |
| `ROLL_CORRECT_RETRY_MAX` | 5 | Phase 5 校正重試上限 |
| `RETURN_VACUUM_RELEASE_MS` | 5000 | Phase 6 召回時關閥後等待吸盤完全脫附的時間 |
| `HEARTBEAT_INTERVAL_MS` | 500 | Washrobot → Crane 主動 ping 間隔（Web Backend → 兩端亦同）|
| `WATCHDOG_TIMEOUT_MS` | 2000 | Crane / Washrobot 端 watchdog 判定失聯門檻（約 4 次 ping 漏失）|

---

## 7. Open Questions（未解決，影響 code）

1. **吊機同步機制** — 已確認 2026-04-12 / 更新 2026-04-13
   - **吊機硬體介面：** ZS_DIO_R_RLY 繼電器
     - CH1 = 左收繩 / CH2 = 右收繩 / CH3 = 左放繩 / CH4 = 右放繩
   - **新增感測：** SD76 #3（中間管線計米）、DSZL-107 × 2（左右張力）、變頻器 × 1（中間絞盤馬達）
   - **同步策略（C 案）：** 左右絞盤用繼電器 + SD76 位置回授；中間絞盤（變頻器）同步放繩，中間 cm = 左右 cm × `MIDDLE_WINCH_RATIO_K`
   - **注意：** 現有 `Crane_control_PI/main.cpp` 用 `CRANE_DOWN_PIN=7, CRANE_UP_PIN=8`（舊單絞盤版本），需配合重寫
   - **通訊架構：** 見下方「系統通訊架構」章節

2. **真空度閥值** — JC-100 讀到多少 kPa 算吸附成功？（單位 0.1 kPa）

3. **總下移距離** — 大樓一層樓 / 整棟？實際數字？

4. **上滑台清洗行程 / 速度** — 起點、終點、RPM？

5. **Phase 1 自動化** — 4 輪貼牆是人工還是吊機程式自動？DM2J 輪子（slave 2,4）在 Phase 1 要怎麼驅動？
   - 清洗動作細節已確認 2026-04-13：CH5 刷洗滾筒 / CH6 水泵 / CH7 水箱進水球閥（確認於 2026-04-14）
   - 機械手臂（USB→CAN）本版**不整合**
   - 部署流程已重構（2026-04-13）：地面自檢 + 兩次歸零（地面 ground → 頂樓 top，自動存 `home_ground_cm` 供未來召回回地面使用）

6. **重量感測器 DY-500** — 已確認 2026-04-13
   - 硬體已安裝於 washrobot 端左右鋼索連接點
   - **目前硬體有問題，暫不啟用**（`ENABLE_CROSS_TENSION_CHECK=false`）
   - 規格保留：未來硬體修復後啟用「雙點張力冗餘比對」（crane 端 DSZL-107 vs washrobot 端 DY-500）
   - 閾值 `TENSION_CROSS_DIFF_KG` 暫定 1.0 kg，待實測調整

7. **DSZL-107 Modbus 暫存器表** — 文件待補（張力讀取單位、暫存器位址、slave ID 設定方式）→ 影響 `user_lib/DSZL_107.{h,cpp}` 新驅動實作

8. **中間絞盤變頻器** — ✅ 已解決（2026-04-13）：確認型號 clvdrives 900-0007M1，驅動 `user_lib/CLV900_inverter.{h,cpp}` 完成，暫存器表見 `.claude/summaries/CLV900_INVERTER_MODBUS_SUMMARY.md`；`MIDDLE_WINCH_RPM` 實測調整

9. **姿態儀 baseline 校正** — Phase 2 Init 完成時取 N 秒平均當 `roll0/pitch0/yaw0`，後續比較相對偏移。N 建議 3 秒（150 sample @ 50 Hz），實測調整

10. **水箱溢流處理**（2026-04-14 新增）— 10L 水箱若 CH7 持續補水是否會溢流？需使用者確認：
    - (a) 水箱有**浮球閥**（機械式，滿自動關）→ 軟體無需管，CH7 清洗時常開即可
    - (b) 水箱有**溢流孔**接外排 → 軟體也無需管
    - (c) 都沒有 → 需軟體控制 CH7 間歇開關（依耗水速率）
    - 影響 Phase 4-C 與 Phase 2 Init 的 CH7 策略

11. **Fathom-X Tether 100m 網路驗證**（2026-04-14 新增）— 長距離 2-wire 雙絞線尚未實測：
    - 拔插恢復測試（TCP 重連、watchdog 觸發）
    - 長時間穩定度（掉包率、延遲）
    - 納入 `deploy_and_test.pdf` Gate 項目

---

## 8. 系統通訊架構

### 網路拓撲

```
[Browser (Web GUI, Vue/React)]
              │ WebSocket
              ▼
[Node.js Web Backend]  ← 跑在 crane RPi @ 192.168.1.101:8080
       │                  （刻意放在救援側：washrobot 在半空中掛掉時
       │                    GUI 仍可控 crane 手動收繩回收機體）
       │ TCP text
       ├──────────────────────────┐
       ▼                          ▼
[Washrobot C++ TCP server]   [Crane C++ TCP server]
  192.168.1.100:5001           192.168.1.101:5002 (localhost)
       ▲
       │ TCP text（僅自動模式下 washrobot 連 crane 放繩）
       ▼
[Crane :5002]
```

> **部署位置決策（2026-04-15）：** Web Backend 從原本 .100 搬到 .101。
> 理由：washrobot RPi 是控制機體吸附與下移的主角，風險高（卡死、過載、網路異常都可能）；
> 若 GUI 與 washrobot 同台，washrobot 掛掉 = 失去所有遠端控制能力（機體懸吊半空）。
> 搬到 crane 側後，即便 washrobot 完全失聯，操作員仍能透過 GUI → crane 手動收繩救援。

### Port 分配

| Port | 用途 |
|---|---|
| 4001 | USR-TCP232-304 Modbus-TCP（已佔用，三組 .20/.21/.22）|
| 5001 | Washrobot C++ TCP command server |
| 5002 | Crane C++ TCP command server |
| 8080 | Node.js Web backend（HTTP 靜態網頁 + WebSocket）|
| 554  | PoE 攝影機 × 4 RTSP（埠隨廠牌，Node.js ffmpeg 消費）|
| 8081 | Node.js 轉出 HLS/WebRTC 影像串流給 Browser（四路 grid）|

### 元件職責

**Web Backend（Node.js，跑在 crane RPi @ 192.168.1.101）**
- 提供靜態網頁資源（HTML/CSS/JS）
- 維持兩條 TCP client 連線（對 washrobot 192.168.1.100:5001 + crane localhost:5002）
- WebSocket server 接多個瀏覽器 client
- 雙向轉發：Browser ws 訊息 → 對應裝置 TCP；裝置 TCP 訊息 → broadcast 給所有 browser
- 管理連線狀態與自動重連
- 透過 `{src:"status", washrobot:bool, crane:bool}` 廣播兩側連線狀態，供前端判定進入失聯模式

**Washrobot C++（TCP server :5001）**
- 接受多 client 同時連線
- 解析單行文字指令 → 執行動作
- 自動模式下，同時主動連 Crane :5002 發放繩指令
- 狀態變化（壓力、位置、錯誤）以 `EVT` 主動推播

**Crane C++（TCP server :5002）**
- 接受多 client 同時連線（washrobot + GUI backend 同時）
- 接收放繩 / 收繩指令，配合 SD76 計米器自動停
- DY-500 重量即時推播

### 攝影機整合（方案 A — Node.js + ffmpeg）

```
[PoE Cam × 4] ── RTSP ──▶ [Node.js Backend: ffmpeg 轉 HLS] ──▶ [Browser: <video> × 4 grid]
  左上 / 左下 / 右上 / 右下
```

- **目的：** 清洗時監看 4 角落畫面（吸附狀態、刷洗覆蓋、異物撞擊等）
- **實作重點：**
  - Node.js 為每路 camera 啟動 ffmpeg 子進程，讀 RTSP → 輸出 HLS `.m3u8` + TS segments 到 `/public/cam<N>/`
  - Browser 透過 `<video>` + hls.js 播放 4 路
  - 生命週期：Backend 啟動時拉起 4 個 ffmpeg；camera 斷線由 ffmpeg retry loop 處理
  - 延遲：HLS 典型 2~5 秒（可接受，清洗不需即時控制）
- **待提供：** 攝影機型號 + RTSP URL 格式（`rtsp://user:pass@192.168.1.xx:554/...`）

### 指令協定（簡單文字，行為單位，`\n` 結尾）

#### Crane 接受指令
```
pay_out <cm>                # 雙繩同步放 N cm + 中間管線同步（× MIDDLE_WINCH_RATIO_K）
retract <cm>                # 雙繩同步收
pay_out_left  <on|off>      # 手動：左放繩
pay_out_right <on|off>      # 手動：右放繩
retract_left  <on|off>      # 手動：左收繩
retract_right <on|off>      # 手動：右收繩
middle_set <rpm> <dir>      # 手動：中間絞盤變頻器（dir = pay|retract|stop）
zero_meters ground          # 地面起點歸零（SD76 #1/#2/#3 同時 = 0）
zero_meters top             # 頂樓到位歸零（歸零前先讀 |SD76| 存為 home_ground_cm）
home_status                 # 回傳 home_ground_cm / 當前 SD76 / 尚需回地面繩長
roll_correct <delta_cm>     # Phase 5 專用：單次左右鋼索差動量（正值=左放右收）
emergency_stop              # 所有繩動作立刻停（繼電器 OFF + 變頻器 stop）
ping                        # watchdog 心跳 → 回 pong
status                      # 查狀態
```

#### Washrobot 接受指令
```
init                        # Phase 2
attach                      # Phase 3（全吸）
step_down                   # 單步（A 腳下移 + B 身體下移 + C 清洗一次）
run <steps>                 # 連續 N 步自動下移
pause / resume / emergency_stop
vacuum <group> <on|off>     # group = feet / body / center / all
move <motor> <cm>           # motor = left_foot / right_foot / arm
pusher <group> <cm>         # group = feet / body / center
arm_sweep                   # 清洗一次
tilt_mode <on|off>          # Phase 5 平衡校正模式（僅 Roll）
confirm_balance <yes|no>    # 回應 balance_ask EVT（Yes → 執行 Phase 5 / No → 忽略繼續）
return_home                 # 召回到地面（attached / paused → 鬆吸盤 + 放繩 home_ground_cm - current_down）
reset                       # 清 error 回 idle（需人工確認現場安全）
ping                        # watchdog 心跳 → 回 pong
status                      # 查狀態（含 state / step_counter / home_ground_cm 等）
```

#### 主動事件（Washrobot → client）
```
EVT state_changed <old> <new>            # 每次生命週期狀態轉移
EVT balance_ask roll=<deg> pitch=<deg>   # balance_deg > IMU_ASK_DEG，等待 confirm_balance
EVT watchdog_timeout <peer>              # peer = crane / web
EVT vacuum_fail <group> retry=<n>
EVT tension_alarm <kind> left=<kg> right=<kg>   # kind = low / high / diff
EVT imu_emergency balance=<deg>          # balance_deg > IMU_EMERGENCY_DEG，已強制停機
```

#### 回應格式

| 格式 | 用途 | 例 |
|---|---|---|
| `OK\n` | 指令成功（無資料）| `OK` |
| `OK <data>\n` | 指令成功（附回傳）| `OK weight=42.3 length=300` |
| `ERR <msg>\n` | 指令失敗 | `ERR vacuum_insufficient feet` |
| `EVT <type> <data>\n` | 主動事件推播（非回應）| `EVT vacuum_fail feet retry=3` |

### 失聯模式（Degraded Mode）UI 行為

前端依 `status` broadcast 的兩個 bool 分成四種模式：

| washrobot | crane | 模式 | UI 表現 |
|---|---|---|---|
| ✅ | ✅ | 正常 | 全區塊可用 |
| ✅ | ❌ | Crane 失聯 | Crane 區塊灰掉鎖住 + 紅 banner「CRANE 失聯，禁止啟動自動下移」；washrobot 仍可用但**阻擋 `run` / `step_down` 等會下移的指令** |
| ❌ | ✅ | **Washrobot 失聯（救援模式）** | washrobot 區塊整塊灰掉鎖住 + 橘 banner「⚠ WASHROBOT 失聯，僅 crane 可控 — 請確認機體是否懸吊在外並手動收繩回收」；**緊急收繩區塊保持可用** |
| ❌ | ❌ | 全斷 | 紅 banner「全線失聯，請檢查網路」，所有控制鎖住 |

切換進入失聯模式時，前端要**自動送一次 `stop`** 給仍存活的那一側（避免殘留動作繼續跑）。

### 緊急收繩按鈕（救援模式專用）

GUI 在 crane 區塊提供獨立的「🆘 緊急收繩」分區，**即使 washrobot 失聯也能用**（它只依賴 crane 連線）：

- **互動方式：按住持續收（press-and-hold）**
  - `mousedown` / `touchstart` → 同時送 `retract_left on` + `retract_right on`
  - `mouseup` / `touchend` / `mouseleave` → 同時送 `retract_left off` + `retract_right off`
  - 另補送一次 `stop` 做保險（避免瀏覽器崩潰時繼電器卡在 ON）
  - 設計理由：防誤觸（點一下就收會危險）；按著才動、放開就停是最安全的人因邏輯
- **視覺：** 大紅按鈕、獨立區塊、文字「按住收繩」；按下中顯示秒數計時
- **不走 SD76 自動停：** 緊急模式下不信任自動邏輯，完全由操作員眼睛判定何時放開
- **張力保護仍在：** Crane C++ 端的 `tension_alarm` safety monitor 不受 GUI 模式影響，超張力照樣強制停 + 回 EVT

**失聯模式下，下列 crane 手動指令也保持可用**（操作員或許需要調整姿態才能收）：
- `pay_out_left/right <on|off>`、`retract_left/right <on|off>`、`middle_set`、`roll_correct`、`stop`

**不建議 / 鎖住的 crane 指令**（失聯模式下）：
- `pay_out <cm>` / `retract <cm>` — 自動量化版本預設走 SD76 閉環，救援時不見得能信任；如要用需雙重確認 modal

---

## 9. 下一步

1. 使用者確認本規格
2. 解決 Open Questions（至少吊機同步 + 真空閥值）
3. **取得 DSZL-107 + 中間絞盤變頻器文件** → 實作 `user_lib/DSZL_107.{h,cpp}` 與變頻器驅動
4. 重寫 `Crane_control_PI/main.cpp`：
   - 移除舊 `CRANE_DOWN_PIN/CRANE_UP_PIN` GPIO 邏輯
   - 改為 ZS_DIO_R_RLY CH1~4 + SD76 × 3 + DSZL-107 × 2 + 中間絞盤變頻器
   - 實作 C 案同步放繩（左右繼電器 + 中間變頻器 × `MIDDLE_WINCH_RATIO_K`）
   - 張力 safety monitor 執行緒（1s 滑動平均 + 500ms 持續判定）
   - **Watchdog 執行緒**：追蹤 last_ping_ts，> `WATCHDOG_TIMEOUT_MS` 且動作中 → 立即停
   - 新指令 handler：`zero_meters ground|top`、`home_status`、`middle_set`、`roll_correct`、`ping`
   - 執行期狀態：`home_ground_cm`（頂樓到地面所需放繩量，Phase 1 時自動紀錄）
5. 重寫 `washrobot_new_PI/main.cpp`：
   - 修正 `m1~m7` 宣告（應該是 `m1~m9`）
   - 修正 IP 分配（感測器從 cli_21 改到 cli_22）
   - 修正 PQW slave ID（16 → 12）
   - 用 CH1~4 取代 CH11~14
   - 實作 Phase 2~6 指令（含 Phase 6 召回流程）
   - 姿態儀 baseline 校正 + 平衡指標 + `balance_ask` EVT 機制
   - Watchdog：對 Crane 主動 ping（500ms）、2000ms 未回應 → pause + EVT
   - 新指令 handler：`confirm_balance`、`ping`
