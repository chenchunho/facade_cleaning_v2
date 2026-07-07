# MH300 遷移計畫 — 左右吊機變頻器 SE3-210 → Delta VFD-MH300

> 規範權威：本檔。硬體規格 + register map 見 memory `project_new_crane_vfd_mh300`
> + 手冊 `D:\洗窗戶機器人\電控設備資料\DELTA_IA-MDS_MH300_UM_TC_20260505.pdf`。
> Driver 已寫：`user_lib/MH300_inverter.{h,cpp}`（2026-07-03，API 對齊 SE3）。

## 目標與範圍

把專案裡「控制到變頻器」的部分從 `SE3_inverter` 換成 `MH300_inverter`。

**Scope 決定（2026-07-03，user 拍板）：**
- ✅ **只換左右繩 winch**（`se3_left` / `se3_right` 兩台）
- ❌ **CLV900 中間絞盤不納入**（未安裝、不同絞盤，保留原 driver、之後要裝再議）
- ✅ **keepalive thread 先保留**（MH300 上照跑能用，只是多餘；bench 驗證過再簡化）
- ✅ **GUI/status 識別碼改名** `se3_left`/`se3_right` → `vfd_left`/`vfd_right`

## 現況盤點（受影響清單）

| 位置 | 內容 | 錨點 |
|---|---|---|
| `Crane_control_PI/main.cpp` | 2 instance + ~40 用點 + helper 群 + keepalive thread | 見下 |
| `Crane_control_PI.vcxproj` | build SE3_inverter.cpp/.h | L84/L95 |
| `web_backend/public/{app.js,index.html}` | `se3_left`/`se3_right` 裝置旗標 + 按鈕 data-required + 中文標籤 | app.js:289/295/308, index.html:511/589+ |
| `Linux_test/main.cpp` | SE3 bench 測試 menu | — |
| docs | CLAUDE.md 架構+driver 表、motion_flow、summaries | — |

main.cpp 主要錨點：
- instances：`se3_left` (cli_A slave 1) / `se3_right` (cli_B slave 2) @ L339-340
- helper（吃 `SE3_inverter&`）：`reliable_start_one` L661 / `reliable_stop_one` L694 / `reliable_setfreq_one` L841 / `reliable_run_one` L860 / `dual_se3_concurrent` / `dual_se3_sync_retry` / `se3StartRopeMotion` L1002 / `se3StartRopeHold` L1007 / `apply_hold_one_side` L2967 / `format_se3_fault_codes` L1462 / `robust_read_status` L3037
- 方向約定：`pay_out = runForward, retract = runReverse` @ L1012-1015
- 常數：`SE3_MAX_HZ`=50 L162、`SE3_HOLD_HZ_DEFAULT`=10 L167、`SE3_MOTION_HZ_DEFAULT`=30 L168
- keepalive：`se3_keepalive_loop` L1477、`SE3_KEEPALIVE_INTERVAL_MS` L414
- fault atomics：`g_se3_left_fault`/`g_se3_right_fault` L423-424（motion abort 用 @ L2259）
- status wire tokens：`g_dev_se3_left/right` L584-585 → `make_device_state_line()` 吐 `se3_left=`/`se3_right=` @ L604-605；cmd_status 吐 `dev_se3_left/right`

---

## 分階段執行

### Phase 0 — 硬體 + MH300 commissioning（實機，最先）

兩台 MH300 keypad 設定（值見 memory，手冊確認）：

| 參數 | 值 | 說明 |
|---|---|---|
| 09-00 | 1 / 2 | 站號（對齊 se3_left=1 / se3_right=2）|
| 09-01 | 9.6 | baud kbps（填數值本身）|
| 09-04 | 12 | 8N1 RTU（配 SD76 共 bus 的 8N1）|
| 00-20 | 1 | 頻率來源 RS-485 |
| 00-21 | 2 | 運轉來源 RS-485 |
| 07-00~04 | 依載重調 | DC brake / 煞車截波（配 BR300W070-S 制動電阻）|
| 01-12 / 01-13 | 依現況 | 加/減速時間（左右必須對齊，同步停車）|

- 制動電阻 BR300W070-S 接線；bus 接到原 SE3 的 USR gateway（沿用拓樸）
- 00-20/00-21 也可用 driver `configureModbusControl()` 一次性寫入（寫 EEPROM，只跑一次）

### Phase 1 — build 接線
- `Crane_control_PI.vcxproj` 加 `MH300_inverter.cpp` / `.h`（SE3 檔先留著、全部驗證過再從 vcxproj 移除）

### Phase 2 — main.cpp 型別 swap（機械）
- include `SE3_inverter.h` → `MH300_inverter.h`
- 2 instance + 所有 `SE3_inverter&` helper 簽名 → `MH300_inverter&`
- 常數/atomic/helper 名稱裡的 `se3`/`SE3` 可一併改 `vfd`（見 Phase 4 決定連 wire token 一起改）

### Phase 3 — 🔴 邏輯差異修正（重點，非機械替換）

1. **急停 recovery（必改）**
   現況慣用法「`emergencyStop()` → `stopDecel()` 清 MRS」，SE3 因 MRS 與 run 同 reg（0x1001）成立；MH300 的 **B.B 在 0x2002、run 在 0x2000，`stopDecel` 清不掉 B.B** → 急停後馬達被 base-block 卡死。
   → 把這些 callsite 的「stopDecel 清 MRS」改成 **`clearAlarm()`**（driver 內會寫 0x2002=0 解 B.B）。
   → 影響 callsite（audit 全部）：L951-952、L985、L2429、L3069-3070、L3214-3215。

2. **keepalive（先保留）**
   `se3_keepalive_loop` 在 MH300 照跑能用（只是 OPT-prevention 多餘）。**本階段不動**。fault 偵測 + `g_*_fault` atomics 維持原狀。
   → 待 bench 驗證 MH300 穩定後，另開任務簡化（把 fault 輪詢併進 motion/hold loop、砍掉整條 thread）。

3. **fault code 顯示**
   `format_se3_fault_codes` 假設 4-deep history；MH300 只有 0x2100 error+warn 一格（driver 已對應 f1=error/f2=warn/f3=f4=0）。顯示字串調整、對照表換成 MH300 代號（ocA/Sto/Sd1…，待手冊補齊）。

### Phase 4 — GUI/status 識別碼改名 `se3_*` → `vfd_*`
Wire contract（crane ↔ GUI）要一起改，否則對不上：
- crane `make_device_state_line()` L604-605：EVT token `se3_left=`/`se3_right=` → `vfd_left=`/`vfd_right=`
- crane cmd_status：`dev_se3_left`/`dev_se3_right` → `dev_vfd_left`/`dev_vfd_right`
- （內部 atomic `g_dev_se3_*`/`g_se3_*_fault` 改名為選配、不影響 wire）
- `web_backend/public/app.js`：L289/295/302/308 的 `se3_left`/`se3_right` key + label「左/右繩變頻器」+ L327 的 `dev_se3_*` 比對字串 + motion_full/diff/lr 陣列
- `web_backend/public/index.html`：所有 `data-required="se3_left,se3_right"`（L511/589-627）
- gw 標籤「USR_A 閘道(SE3 左)」可順手改 VFD（選配）

### Phase 5 — bench 驗證（照 SE3 當年 9 步）
1 status → 2 kg 顯示 → 3 校零 → 4 raw pay_out/retract **確認方向**（⚠ 可能要翻 runForward/Reverse）→ 5 hold 按鈕 → 6 門檻自動停 → 7 motion_rope → 8 safety 觸發 → 9 接 washrobot

### Phase 6 — docs + memory 收尾
- CLAUDE.md：架構圖 SE3 行 → MH300、driver 表換列
- motion_flow.md：變頻器相關段落
- work_log.md：新 handoff 條目
- memory `project_new_crane_vfd_mh300`：標「已上線」
- SE3 相關 summary / se3_mode6_migration_plan 標 deprecated（v1 仍用）

---

## 風險 / 未定

- ⚠ **方向約定**：`pay_out=runForward` 是 SE3 的，MH300 換上第一次要實測繩子方向、可能要翻
- ⚠ **電流/電壓 scale**：driver `OUTPUT_CURRENT_SCALE`/`OUTPUT_VOLTAGE_SCALE` 暫 0.1，實機對面板校
- ⚠ **bus 現況**：main.cpp 註解 se3_right 在 cli_B，但 CLAUDE.md 拓樸說 2026-05-15 兩台都搬到 USR_A — 換裝前先確認實際 gateway/slave
- 手冊未 100% 確認：0x2102 vs 頻率命令回讀、0x2106 vs 2107 輸出電壓、09-02 意義、MH300 fault code 代號表
- v1 (`washrobot_new_PI`) 仍用 SE3 — SE3 driver / 相關 doc **不刪、只在 v2 停用**
