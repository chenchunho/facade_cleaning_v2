# MH300 遷移計畫 — 左右吊機變頻器 SE3-210 → Delta VFD-MH300

> 規範權威：本檔。硬體規格 + register map 見 memory `project_new_crane_vfd_mh300`
> + 手冊 `D:\洗窗戶機器人\電控設備資料\DELTA_IA-MDS_MH300_UM_TC_20260505.pdf`。
> Driver 已寫：`user_lib/MH300_inverter.{h,cpp}`（2026-07-03，API 對齊 SE3）。

## 🔴 現況（2026-09-08 逐項打開原始碼複驗，**不是照計畫書推斷**）

> 📌 **這份計畫書一度比實作落後三個 Phase**，而落後的方向是「**把已完成的說成待做**」——
> 於是唯一真正的風險（Phase 3）被埋在三個假待辦中間，看起來只是四項裡的一項。
> 📌 **同型踩坑**：與 09-07「VS 早就不是建置路徑，三份文件都還說它是」是同一類。
> **文件描述的待辦狀態本身就要定期實測。**

| Phase | 計畫書原本 | **實測（2026-09-08）** | 證據 |
|---|---|---|---|
| 1 build 清單 | 待做 | ✅ **已完成** | `scripts/build/build_crane.sh:10-11` 兩支 driver 都在清單內 |
| 2 型別 swap | 待做 | ✅ **已完成** | `main.cpp:127` `#define CRANE_VFD_IS_SE3 1` ＋ `using CraneVFD = SE3_inverter/MH300_inverter` 的 `#if` 切換；全檔用 `CraneVFD&`，fault 輪詢差異已抽成 `vfd_poll_fault_()`／`vfd_status_is_fault_()` 兩個 `#if` 分支 |
| 4 識別碼改名 | 待做 | ✅ **已完成** | `main.cpp` 中 `se3_left=` / `se3_right=` / `dev_se3_` wire token **命中 0 次**；`web_backend/public/` 與 `public_v2/` 同樣 0 殘留 |
| **3 邏輯差異** | 待做 | 🔴 **仍是唯一主體，但內容要改寫**（見該節） | — |

🎯 **換硬體現在只要翻 `main.cpp:127` 那一個巨集。** 這個遷移的剩餘工作量比計畫書看起來小得多，
**但剩下的那一塊是唯一會咬人的那塊。**

---

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
| ~~`Crane_control_PI.vcxproj`~~ → **`scripts/build/build_crane.sh`** | 編譯清單（vcxproj 已於 2026-09-07 刪除） | — |
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

### Phase 1 — build 接線　✅ **已完成**（2026-09-08 複驗）
- ~~`Crane_control_PI.vcxproj`~~ → **`scripts/build/build_crane.sh`** 的編譯清單，**兩支 driver 都已在內**
  （`:10` `MH300_inverter.cpp`／`:11` `SE3_inverter.cpp`）⇒ 巨集翻過去即可，不需再動 build。

### Phase 2 — main.cpp 型別 swap（機械）　✅ **已完成**（2026-09-08 複驗）

實作方式比計畫書原本設想的更好——**不是把 SE3 改成 MH300，而是做成編譯期可切換**：

```cpp
// main.cpp:127
#define CRANE_VFD_IS_SE3 1
#if CRANE_VFD_IS_SE3
  #include "SE3_inverter.h"
  using CraneVFD = SE3_inverter;
#else
  #include "MH300_inverter.h"
  using CraneVFD = MH300_inverter;
#endif
```

- 全檔 helper 簽名已是 `CraneVFD&`，instance 為 `vfd_left` / `vfd_right`
- 兩者 fault 輪詢語意不同的部分已抽成 `vfd_poll_fault_()` / `vfd_status_is_fault_()` 的 `#if` 分支
- ⇒ **切換硬體 ＝ 把 `1` 改成 `0` 重編**，不需要改任何 callsite

### Phase 3 — 🔴 邏輯差異修正（重點，非機械替換）

1. **急停 recovery** —— 🔴 **2026-09-08 改寫：原本的描述已不正確，但問題沒有消失，只是換了位置。**

   **原文（保留備查）**：「`stopDecel` 清不掉 B.B → 急停後馬達被 base-block 卡死。
   → 把這些 callsite 改成 `clearAlarm()`。→ 影響 callsite：L951-952、L985、L2429、L3069-3070、L3214-3215。」

   **實測推翻兩點：**
   - ❌ **那六個行號全是分層重構前的，現在對不上。** 現行的「`emergencyStop()` → `stopDecel()` 清 MRS」
     慣用法在 `main.cpp` 的位置是 **L1417-1418／L1457-1458／L3163-3164／L4020-4021／
     L4104-4105（L 側 escalate）／L4122-4123（R 側 escalate）／L4190-4191**。
   - ❌ **「馬達被 base-block 卡死」在正常路徑上不會發生** —— driver 早就補過了：

     ```cpp
     // MH300_inverter.cpp:233
     void MH300_inverter::releaseBaseBlockIfNeeded_() {
         if (!base_blocked_) return;
         writeParam(REG_AUX_CMD, 0x0000);   // clear B.B / EF bits on 0x2002
         base_blocked_ = false;
     }
     bool MH300_inverter::runForward() { releaseBaseBlockIfNeeded_(); return writeParam(REG_CMD, CMD_RUN_FWD); }
     bool MH300_inverter::runReverse() { releaseBaseBlockIfNeeded_(); return writeParam(REG_CMD, CMD_RUN_REV); }
     bool MH300_inverter::emergencyStop() { base_blocked_ = true; return writeParam(REG_AUX_CMD, AUX_BB); }
     ```
     ⇒ `emergencyStop()` 記下旗標，**下一個 run 命令會先清 0x2002**，`clearAlarm()` 也會清（`:282`）。
     **SE3 語意已在 driver 層還原，app 端那六處 callsite 因此不必改。**

   🔴🔴 **但真正的洞在這裡，而且比原本描述的更隱蔽：`base_blocked_` 是 process-local 的軟體旗標。**

   | 情境 | 硬體 0x2002 B.B | 軟體 `base_blocked_` | 結果 |
   |---|---|---|---|
   | 急停 → 下一個 run（同一次執行） | set | `true` | ✅ run 前自動清掉，正常 |
   | **急停 → 程式重啟 → run** | **仍 set**（寫在變頻器裡） | **`false`**（新物件） | 🔴 **`releaseBaseBlockIfNeeded_()` 直接 return，B.B 永遠不清** |

   **重啟包含：`exit` 正常收尾、crash、Pi 重開機。** 只要在急停之後發生，
   下一次開機的第一個 run 命令就會**寫進 0x2000 但輸出仍被 B.B 切斷**——
   ⚠️ **馬達不轉、Modbus 寫入回報成功、沒有 fault code、沒有任何錯誤訊息。**

   🔴 **`init()` 補不了這個洞**（`MH300_inverter.cpp` init Mode B 段實測）：
   它只在 **讀到 error code 才** `clearAlarm()`，而 **base-block 是輔助命令位元、不是 alarm**
   ⇒ 冷啟動時那條 `if` 不會成立。

   ✅ **修法（換硬體前必做，一行等級）**：`init()` 的 Mode B probe 成功之後，
   **無條件** `writeParam(REG_AUX_CMD, 0x0000)`（或直接 `clearAlarm()`），
   讓冷啟動不可能繼承上一次執行留下的硬體 B.B。
   代價：每次 init 多一筆 Modbus 寫入。**這比「馬達靜默不轉」便宜太多。**

   ### ✅ Phase 3-a 已實作（2026-09-09，`changelog [2026-09-09m1]`）

   `MH300_inverter.cpp` Mode B `init()` 已補上無條件清 `0x2002`。兩個實作細節：
   - 走過既有 `clearAlarm()` 分支時不重複寫（`clearAlarm()` 結尾本來就會把 `0x2002` 寫回 0）
   - 🎯 **寫入失敗時把 `base_blocked_` 設為 `true`**（不是 `false`），讓既有的
     `releaseBaseBlockIfNeeded_()` 在第一個 run 命令時再清一次。
     📌 **旗標初值要選「會多做一次事」的那一邊** —— 設 `false` 等於把同一個靜默失效再造一次。

   **Mode A 不動**：它只做 TCP connect、沒有 probe，沒有時點可掛；吊機走的是 Mode B。

   🔴 **狀態：已實作 + `g++ -fsyntax-only` 通過，但未部署、未實機驗證** ——
   現役仍是 SE3（`main.cpp:127` `CRANE_VFD_IS_SE3 1`），這條路徑目前不會被執行。
   **真正的驗收要等換上 MH300：急停 → 重啟程式 → 下 run，馬達應該要轉。**

   🔎 **順帶查證 SE3（現役）沒有同型的洞**：`cu_mode_set_` 初值 `false` 的語意是「要去寫」＝安全方向；
   且 SE3 的 MRS 與 run 同在一個命令字，寫 run 就會清掉，根本不需要旗標。
   ⇒ **本洞是 MH300 獨有**，差別在於 **MH300 把 B.B 放在跟 run 不同的暫存器**。

   📌 **這一條為什麼值得記**：driver 補了 SE3 語意 parity，補得對，
   但**補在軟體旗標上 ＝ 把一個硬體狀態鏡射成行程狀態**，於是缺陷從「一直會發生」
   變成「**只在跨行程邊界發生**」——更難遇到，也更難查。
   **凡是用軟體旗標追蹤硬體狀態的地方，都要問一句「行程重啟之後呢」。**

2. **keepalive（先保留）**
   `se3_keepalive_loop` 在 MH300 照跑能用（只是 OPT-prevention 多餘）。**本階段不動**。fault 偵測 + `g_*_fault` atomics 維持原狀。
   → 待 bench 驗證 MH300 穩定後，另開任務簡化（把 fault 輪詢併進 motion/hold loop、砍掉整條 thread）。

3. **fault code 顯示**
   `format_se3_fault_codes` 假設 4-deep history；MH300 只有 0x2100 error+warn 一格（driver 已對應 f1=error/f2=warn/f3=f4=0）。顯示字串調整、對照表換成 MH300 代號（ocA/Sto/Sd1…，待手冊補齊）。

### Phase 4 — GUI/status 識別碼改名 `se3_*` → `vfd_*`　✅ **已完成**（2026-09-08 複驗）

**複驗方式**：`grep` `se3_left=` / `se3_right=` / `dev_se3_` ——
`Crane_control_PI/main.cpp` **命中 0 次**，`web_backend/public/` 與 `public_v2/` 同樣 **0 殘留**。
現行 wire token 已是 `vfd_left=` / `dev_vfd_left`。

<details><summary>原始清單（保留備查，已全數完成）</summary>

Wire contract（crane ↔ GUI）要一起改，否則對不上：
- crane `make_device_state_line()` L604-605：EVT token `se3_left=`/`se3_right=` → `vfd_left=`/`vfd_right=`
- crane cmd_status：`dev_se3_left`/`dev_se3_right` → `dev_vfd_left`/`dev_vfd_right`
- （內部 atomic `g_dev_se3_*`/`g_se3_*_fault` 改名為選配、不影響 wire）
- `web_backend/public/app.js`：L289/295/302/308 的 `se3_left`/`se3_right` key + label「左/右繩變頻器」+ L327 的 `dev_se3_*` 比對字串 + motion_full/diff/lr 陣列
- `web_backend/public/index.html`：所有 `data-required="se3_left,se3_right"`（L511/589-627）
- gw 標籤「USR_A 閘道(SE3 左)」可順手改 VFD（選配）

</details>

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
