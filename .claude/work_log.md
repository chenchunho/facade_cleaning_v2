# Work Log

## 2026-04-15 — user_lib 統一 log 格式

> **規範權威：** `CLAUDE.md` 的「Log 格式規範」節 + `user_lib/log_utils.h` 檔頭 comment
> **協作者必讀：** 本項決策影響所有 user_lib 驅動；新增/修改驅動時一律用 LOG_* 巨集，禁用 printf/cout/cerr

### 已完成
- **新增 `user_lib/log_utils.h`** — 4 個 LOG_* 巨集 + LOG_HEX
  - 格式：`[HH:MM:SS.mmm] [LEVEL] [DEVICE:ID] <message>`
  - Levels：ERR / WRN / INF / DBG（+ LOG_HEX for hex dumps）
  - **所有 level 統一由 `debug_mode` 成員控制**（關掉完全靜默，錯誤已透過 bool return 通知呼叫端）
  - 輸出到 stderr，不落檔、不加鎖（輕量 A 方案）
- **`CLAUDE.md` 新增「Log 格式規範」節** 在 Coding Style 之後
- **14 個驅動全改造完成：**
  - 改名統一：`debugEnabled` / `_debug` / `debugMode` / `debugPrint` → `debug_mode`
  - 每個 class 新增 `std::string _log_tag` 成員，init 裡設為 `"PREFIX:<ID>"`
  - 所有 `printf` / `std::cout` / `std::cerr` 換成 LOG_* 巨集
  - 分級：ERR（連線斷/CRC/timeout/fault）、WRN（retry/echo mismatch）、INF（init 成功/狀態轉換）、DBG（輪詢/值讀取）、HEX（所有 TX/RX 幀）
  - TCP_client 特別處理：刪除自有 `printLog()` + `getCurrentTimestamp()`（log_utils.h 已提供時戳）
  - Prefix 表：DM2J / ZDT / JC100 / DY500 / PQW / SD76 / CLV900 / QX / DIHOOL / ZSDIO / WT901 / SER / TCP / TCPSVR
- **驗證：**
  - Grep `printf|std::cout|std::cerr` 於 `user_lib/*.cpp` → 0 筆
  - Grep `debugEnabled|_debug|debugPrint|debugMode` → 0 筆（全改為 `debug_mode`）
  - 15 個 .h 檔皆有 `_log_tag` 成員

### 備註
- DIHOOL_control / QX_DO24 用 inverted convention（true=success）— 維持不動，只改 log
- 業務邏輯 / public API / 回傳值一律不動，純 log 輸出格式改造
- 以子 agent 批次完成 13 檔，主線驗證無遺漏

---

## 2026-04-15 — Web Backend 搬家決策（.100 → .101）+ 失聯模式規範

> **規範權威：** `.claude/motion_flow.md` §8（網路拓撲 + 失聯模式 + 緊急收繩）+ `CLAUDE.md` 分散式通訊段
> **協作者必讀：** 部署位置、失聯模式 UI、緊急收繩按鈕以 motion_flow §8 為準；web_backend 實作者請依此規範

### 決策
- **Web Backend 部署位置：washrobot RPi (.100) → crane RPi (.101)**
- **理由：** washrobot 是高風險側（控制機體吸附/下移），若 GUI 與它同台，washrobot 掛掉 = 失去所有遠端控制能力（機體懸吊半空無法救援）。搬到 crane 側後，即便 washrobot 完全失聯，操作員仍能透過 GUI → crane 手動收繩回收機體
- **程式碼影響：** 零 — `server.js` 原本就用環境變數 / 預設 IP 連（非 localhost），搬家只影響部署位置

### 規格文件已更新
- `.claude/motion_flow.md` §8：
  - 網路拓撲圖改為 Web Backend 跑在 .101:8080
  - 新增「失聯模式 UI 行為」表：4 種 (washrobot × crane) 連線狀態對應的 UI 模式
  - 新增「緊急收繩按鈕」規範：
    - 互動方式 **按住持續收（press-and-hold）**
    - mousedown → `retract_left on` + `retract_right on`
    - mouseup/touchend/mouseleave → 對應 off + 補 `stop` 保險
    - 設計理由：防誤觸，按著才動放開就停
    - 獨立大紅按鈕區塊，即使 washrobot 失聯也能用
- `CLAUDE.md` 分散式通訊段同步：Web Backend 位置 + 救援設計註記

### 待完成（留給實作者）
- 🔴 前端 `public/app.js` + `index.html` 實作失聯模式 UI + 緊急收繩按鈕（本次僅流程/架構規格，不動程式碼）
- 🔴 crane RPi (.101) 部署：裝 Node.js 20.x、複製 `web_backend/`、建立 systemd unit
- 🔴 停用 .100 上原本的 web_backend 服務
- 🔴 `EVT state_changed` / `EVT watchdog_timeout` 與失聯 banner 的聯動測試

### 備註（協作分工）
- 本人角色：流程架構負責人，本次僅更新 .md 規格（motion_flow + CLAUDE）
- 後端 / 前端 / 部署實作交給其他協作者，依 motion_flow.md §8 實作

---

## 2026-04-14 — 階段性 commit（規格定稿 + CLV900 驅動）

### 已完成
- **Commit `d7e4132`** — `feat+docs: add CLV900 inverter driver + finalize architecture spec`
- 8 files changed, +833/-25
- 包含：CLV900 驅動（.h/.cpp + 摘要）、motion_flow.md / CLAUDE.md 架構同步、deploy_and_test.pdf Phase 2 + Gates 7-11、gen_deploy_pdf.py、本 work_log
- **未 push**（本地 ahead of origin/main by 1）
- 原計畫「一次性 commit」改為分階段：本次鎖住規格 + CLV900，DSZL / Crane 重寫 / IMU 後續再提

### 待完成（不變）
- 🟡 DSZL-107 Modbus 暫存器表（唯一卡點硬體文件）
- 🟡 水箱溢流處理 + 攝影機型號/RTSP URL
- 🔴 寫 `user_lib/DSZL_107.{h,cpp}` → 重寫 `Crane_control_PI/main.cpp`
- 🔴 washrobot main.cpp 加 IMU baseline + balance_ask + crane watchdog + confirm_balance
- 🔴 Web GUI：鋼索歸零 + 平衡校正 modal + 4 路攝影機 grid
- 🔴 Fathom-X 100m 實機拔插測試
- 🔴 實機測試 + 參數回填

---

## 2026-04-14 — 架構細化（攝影機 / 水系統 / 剎車 / Fathom-X）

### 已完成（規格文件同步）

1. **PoE 攝影機 × 4 整合方案確認** — 方案 A（Node.js + ffmpeg → HLS → Browser `<video>` grid）
   - 4 角落監看（左上/左下/右上/右下），清洗時即時觀看
   - motion_flow.md §8 新增「攝影機整合」節 + Port 554/8081 分配
   - 待提供：攝影機型號 + RTSP URL 格式

2. **水系統流向確認**
   - CH7 = **水箱進水球閥**（不是出水），連接 頂樓水源 → CH7 → 10L 水箱 → CH6 泵浦 → 機械臂噴頭 → 牆面 → 地面
   - CLAUDE.md 架構圖、motion_flow.md §2/§3/§4 全部同步更新
   - Phase 4-C 邏輯不變（清洗時 CH7/CH6 同時 ON 補水+出水），只是 CH7 意義變了
   - 新 Open Q10：水箱溢流處理（浮球閥 / 溢流孔 / 軟體控制）待使用者確認

3. **絞盤機械規格補齊**
   - 鋼索直徑 **6 mm**
   - 斷電行為：**自動剎車**（電磁剎車失電夾持）
   - motion_flow.md §2 新增「絞盤機械規格」節
   - §5「斷電即脫離設計原則」補註：機器人脫牆後由剎車懸吊（不會墜落也不會繼續下滑）

4. **DM2J 步進剎車** — 失電後**鎖死**（確認）
5. **DY-500 重量感測器** — 硬體有問題，確認暫不啟用（規格保留）
6. **機械手臂 USB→CAN** — 確認本版不整合，保留未來擴充

7. **Open Questions 同步**
   - Q8 中間絞盤變頻器 → ✅ 已解決（CLV900 驅動完成）
   - 新增 Q10 水箱溢流處理
   - 新增 Q11 Fathom-X 100m 拔插/長穩度實測（未做）

### 架構決策（不做）

- **時間同步** 不做（watchdog 即時控制不需要；事後日誌分析誤差可接受）
- **UPS / 雷擊 / 漏電保護** 不做
- **Web GUI 認證** 不做
- **日誌** 先存本地（SD 卡）

### 待完成（更新）

- 🟡 DSZL-107 Modbus 暫存器表（唯一卡點硬體文件）
- 🟡 水箱溢流處理方式（Open Q10）
- 🟡 攝影機型號 + RTSP URL 格式
- 🔴 寫 `user_lib/DSZL_107.{h,cpp}`
- 🔴 重寫 `Crane_control_PI/main.cpp`（ZS_DIO + SD76×3 + DSZL×2 + CLV900 + watchdog）
- 🔴 washrobot main.cpp 加 IMU baseline + balance_ask EVT + crane watchdog + confirm_balance
- 🔴 Web GUI：鋼索歸零按鈕 + 平衡校正詢問 modal + **4 路攝影機 grid**（Node.js ffmpeg+HLS）
- 🔴 Fathom-X 100m 拔插測試（實機 Gate 項目）
- 🔴 實機測試 + 參數回填 + 一次性 commit

---

## 2026-04-13 (Session 3) — CLV900 變頻器驅動

### 已完成

1. **解鎖中間絞盤變頻器**：使用者確認型號為 **clvdrives 900-0007M1**，PDF `doc/900系列通用变频器说明书-V900.24.01.16.C版本（6按键）.pdf`
2. **PDF 摘要** `.claude/summaries/CLV900_INVERTER_MODBUS_SUMMARY.md`
   - Modbus-RTU 透過 USR 透傳（FC 僅 0x03 / 0x06，無多寫）
   - 控制 reg `0x0001`（freq -10000~+10000）/ `0x0002`（命令 1~7）/ `0x0003~5`（DO/AO）
   - F-group 位址公式 `0xF000 + group*0x100 + index`（F0-00=0xF000、F9-40=0xF928）
   - U-group 位址 `0x1000+`，U0-00 運行狀態、U0-03 運行頻率（0.1Hz）等完整對照
   - F7 通訊參數：F7-00 slave / F7-01 baud / F7-02 格式 / F7-03 timeout
   - 啟用 Modbus 控制：F0-00=2、F0-01=8（一次性，需面板或本驅動 `configureModbusControl()`）
3. **驅動實作** `user_lib/CLV900_inverter.{h,cpp}`
   - Mode A / Mode B init（與其他驅動一致）
   - **Control**：runForward / runReverse / jogForward / jogReverse / stopDecel / stopFree / resetFault
   - **Frequency**：setFreqRaw（int16）/ setFreqPercent / setFreqHz（含 max_hz）
   - **Monitor reads**：runStatus / faultCode / setFreq / runFreq / speedRpm / outputVoltage / outputCurrent / busVoltage / outputTorque（signed）/ igbtTemp
   - **Generic param**：writeParam(reg, val) / readParam(reg, val) + 靜態 fxAddr / uxAddr 地址計算
   - **Convenience**：configureModbusControl()
   - 風格：false=success、tab 縮排、`//=== section ===` 區塊註解、CRC16 polynomial 0xA001、寫入 echo 驗證 + 異常 0x86 檢查
4. **OCR 雙重驗證**（PyMuPDF @ 2.5x → 視覺比對 p49/50/54/55/56/57）
   - 6 頁全對：FC 0x03/0x06、12 暫存器上限、0x01~0x05 控制 reg、F7-00~03、U0-00~U0-26 全部位址 + 單位
   - 額外發現 **F7-19 MODBUS 數據通訊格式**（0=標準/1=非標準，驅動假設 0）+ F7-20 兼容旗標
   - 已補進 `CLV900_inverter.h` 上機前清單 + 摘要文件 F7 表 + 注意事項
   - 暫存圖檔 `.claude/tmp_clv900/` 已清除

### 待完成（更新）

- 🟡 **僅剩** DSZL-107 Modbus 暫存器表
- 🔴 寫 `user_lib/DSZL_107.{h,cpp}`
- 🔴 重寫 `Crane_control_PI/main.cpp`（ZS_DIO + SD76×3 + DSZL-107×2 + **CLV900** + watchdog + 新指令）
- 🔴 washrobot main.cpp 加 IMU baseline + balance_ask EVT + 對 Crane watchdog + confirm_balance handler
- 🔴 Web GUI 加「鋼索歸零」按鈕 + 「平衡校正詢問」modal
- 🔴 deploy_and_test.pdf Gate 7~11 實機驗證後微調
- 🔴 實機測試 + 參數回填 + 一次性 commit

### 上機前一次性設定（透過 CLV900 面板，建議）

- F0-00 = 2（運行命令 = 通訊）
- F0-01 = 8（頻率設定 = 通訊）
- F7-00 = 7（slave ID 對齊 motion_flow §2）
- F7-01 = 對齊 RS485_crane 線上其他裝置 baud
- F7-02 = 對齊資料格式
- F7-03 = 1.0~5.0（通訊超時，配合 RPi watchdog 邏輯）

---

## 2026-04-13 (Session 2) — 文件同步

### 已完成

1. **CLAUDE.md 架構圖同步至 motion_flow.md §2 權威版**
   - **RS485_crane** 擴充至 slave 1~7：
     - Slave 1 ZS_DIO 8CH（左右絞盤繼電器，**不經變頻器**）
     - Slave 2/3 SD76 左/右鋼索計米（既有）
     - Slave 4 SD76 #3 中間管線計米（**新**）
     - Slave 5/6 DSZL_107 左/右鋼索張力（**新**，TBD 暫存器表）
     - Slave 7 變頻器 中間絞盤馬達控制（**新**，TBD 型號）
   - **PQW 8CH** 補齊 CH5~7：CH5 刷洗滾筒馬達 / CH6 水箱泵浦 / CH7 出水球閥 / CH8 保留
   - **驅動表重整**：
     - SD76_length_meters、ZS_DIO_R_RLY 從「備用」移至「使用中」
     - 新增「待實作」分類：DSZL_107、中間絞盤變頻器
     - TCP_server 從「未使用」移至「使用中」（washrobot/crane 都已用）
     - WT901BC_TTL 描述加上「Roll+Pitch 平衡監控」
     - PQW 描述加上「+ 刷洗/水泵/水閥」

2. **deploy_and_test.pdf Phase 2 新增**
   - 標頭日期 2026-04-12 → 2026-04-13
   - 新增「Phase 2 測試（硬體擴充後執行）」整頁，含前提清單 + Crane Phase 2 編譯指令
   - **Gate 7 — Watchdog 雙向心跳**：閒置 vs 動作中行為差異、TCP RST 不等 timeout、自動 reconnect
   - **Gate 8 — Phase 1 鋼索人工歸零**：未歸零 ERR cables_not_zeroed、Web GUI 按鈕、SD76 #1/#2/#3 同步歸零
   - **Gate 9 — 中間絞盤同步 (C 案)**：MIDDLE_WINCH_RATIO_K、計米回授驅動 stop
   - **Gate 10 — DSZL-107 鋼索張力監控**：MIN/MAX/DIFF 三種告警、自動 pause vs 手動 log
   - **Gate 11 — IMU 平衡監控**：3s baseline、step 完成後才 ask、>45° 立停、Phase 5 Roll 校正收斂判定
   - 緊急操作備忘新增 Watchdog / IMU / 張力 三條 + 變頻器斷電
   - 重生 `deploy_and_test.pdf` 成功

### 待完成（不變）

- 🟡 等使用者補 DSZL-107 Modbus 暫存器表
- 🟡 等使用者補中間絞盤變頻器型號 + 暫存器表
- 🔴 寫 `user_lib/DSZL_107.{h,cpp}` + 變頻器驅動
- 🔴 重寫 `Crane_control_PI/main.cpp`（ZS_DIO + SD76×3 + DSZL-107×2 + 變頻器 + watchdog + 新指令）
- 🔴 washrobot main.cpp 加入姿態儀 baseline + balance_ask EVT + 對 Crane watchdog + `confirm_balance` handler
- 🔴 Web GUI 加「鋼索歸零」按鈕 + 「平衡校正詢問」modal
- 🔴 deploy_and_test.pdf Gate 7~11 進一步補強（待實作完成後實機驗證再修）
- 🔴 實機測試 + 參數回填 + 一次性 commit

---

## 2026-04-13 — 規格增補（`motion_flow.md` 主要異動）

### 已完成

1. **吊機端硬體擴充**
   - 新增 RS485_crane (192.168.1.30) 配置表：slave 4 (SD76 #3 中間管線計米)、slave 5/6 (DSZL-107 × 2 左右張力)、slave 7 (中間絞盤變頻器)
   - 中間絞盤同步採 C 案：中間放繩 cm = 左右放繩 cm × `MIDDLE_WINCH_RATIO_K` (預設 1.00)
   - 左右鋼索絞盤仍由 ZS_DIO_R_RLY CH1~4 控制，**不經變頻器**
   - 張力門檻暫定：`TENSION_MIN_KG=0.5`、`TENSION_MAX_KG=3.0`、`TENSION_DIFF_MAX_PCT=30`
   - DSZL-107 + 變頻器 Modbus 暫存器表待補（Open Q7、Q8）

2. **姿態儀（WT901BC）平衡監控**
   - 坐標系確定：**X 垂直於牆面（法線朝外）/ Y 水平沿牆 / Z 朝上**
   - 監控 Roll + Pitch，**不監控 Yaw**（貼牆不自轉 + 磁力計有漂移）
   - 平衡指標：`balance_deg = max(|Δroll|, |Δpitch|)`，相對 Phase 2 Init 取 3 秒平均的 baseline
   - 兩級門檻 + 使用者詢問：
     - `> 15°` → 當前 step 完成後暫停 → EVT `balance_ask` → 使用者 Yes 進 Phase 5 / No 忽略續行
     - `> 45°` → 不詢問，立即停機 + crane stop + 人工處理
   - 共通規則：1s 滑動平均 + 持續 500ms 超標才觸發
   - Phase 5 重寫：**僅校正 Roll**（吊機左右鋼索差動），Pitch 不自動校正；校正收斂判定 `|Δroll| < 1°`，最多重試 5 次
   - 新參數：`IMU_ASK_DEG / IMU_STOP_DEG / IMU_BASELINE_SEC / IMU_HYSTERESIS_DEG / ROLL_CORRECT_CM_PER_DEG / ROLL_CORRECT_RETRY_MAX`

3. **鋼索人工歸零**
   - Phase 1 新增步驟 3：起始位置校準後，Web GUI 按「鋼索歸零」→ Crane 端 SD76 #1/#2/#3 同時歸零
   - 未歸零禁止進 Phase 2
   - 新 TCP 指令：`zero_meters`（Crane）

4. **Watchdog 雙向心跳**
   - Washrobot 每 500ms 主動 ping Crane；Crane 回 pong
   - Crane watchdog thread：`last_ping_ts` 逾 2000ms → 動作中立即停（繼電器 OFF + 變頻器 stop） + EVT `watchdog_timeout`；閒置中僅 log
   - TCP socket disconnect 同等處理（不等 timeout）
   - Washrobot 端對 Crane 亦做 watchdog，自動模式下失聯 → pause + EVT
   - 新參數：`HEARTBEAT_INTERVAL_MS=500`、`WATCHDOG_TIMEOUT_MS=2000`
   - 新指令：`ping` / `pong`、`confirm_balance <yes|no>`、`middle_set <rpm> <dir>`、`roll_correct <delta_cm>`
   - 新 EVT 類別：`balance_ask / watchdog_timeout / tension_alarm / imu_stop`

### 待完成

- 🟡 等使用者補 DSZL-107 Modbus 暫存器表
- 🟡 等使用者補中間絞盤變頻器型號 + 暫存器表
- 🔴 寫 `user_lib/DSZL_107.{h,cpp}` + 變頻器驅動
- 🔴 重寫 `Crane_control_PI/main.cpp`（ZS_DIO + SD76×3 + DSZL-107×2 + 變頻器 + watchdog + 新指令）
- 🔴 washrobot main.cpp 加入姿態儀 baseline + balance_ask EVT + 對 Crane watchdog + `confirm_balance` handler
- 🔴 Web GUI 加「鋼索歸零」按鈕 + 「平衡校正詢問」modal
- 🔴 同步更新 `washrobot_new_PI/CLAUDE.md` 架構圖（反映 RS485_crane slave 4~7）
- 🔴 deploy_and_test.pdf Gate 項目增補（張力測試 / 平衡校正測試 / watchdog 測試）
- 🔴 實機測試 + 參數回填 + 一次性 commit

---

## 2026-04-12

### 已完成

1. **動作流程規格書** `.claude/motion_flow.md` — 垂直爬牆尺蠖式清洗流程
   - Phase 1~5：部署 / Init / Attach / 下移循環 / 姿態校正模式
   - 硬體對照：DM2J×5（RS485_1）、ZDT×9（RS485_2）、JC-100×9 + PQW×1（RS485_3）
   - 真空 3 分區：CH2 腳組、CH3 身體組、CH4 中心（獨立，用於姿態校正）
   - 失敗處理：真空重試 5 次、TCP 自動重連、人工急停
   - 可調參數列表（STEP_CM / VACUUM_THRESHOLD / RPM / …）

2. **分散式通訊架構**
   - 吊機 RPi @ 192.168.1.101，TCP server :5002
   - 洗窗 RPi @ 192.168.1.100，TCP server :5001
   - Web Backend（Node.js）跑在洗窗 RPi :8080，橋接 WebSocket ↔ 兩台 TCP
   - 協定：行為單位文字指令，`OK\n` / `ERR\n` / `EVT\n`

3. **CLAUDE.md 更新**
   - PQW 接線圖改為 3 分區（腳/身體/中心）
   - 新增吊機子系統說明（USR :30、ZS_DIO 8CH、SD76×2）
   - 新增「分散式系統通訊」章節

4. **Crane_control_PI/main.cpp 重寫**
   - TCP_server :5002 多 client
   - 指令集：pay_out / retract / pay_out_*_on|off / retract_*_on|off / stop / status
   - pay_out/retract 依 SD76 計米器回授，左右獨立停
   - 2 分鐘 timeout、abort_flag 緊急停、thread_local 行緩衝
   - ZS_DIO slave 1 (8CH)：CH1=左收 CH2=右收 CH3=左放 CH4=右放
   - SD76 slave 2 左繩 / slave 3 右繩

5. **Web Backend（Node.js）** `web_backend/`
   - `server.js`：Express 靜態 + `ws` WebSocket + 兩條 TCP bridge（washrobot :5001、crane :5002）
   - 行緩衝 + 自動重連（3 秒）+ 連線狀態廣播
   - Browser 協定：`{target, cmd}` 送；`{src, line}` 或 `{src:'status', washrobot, crane}` 收

6. **Web GUI** `web_backend/public/`
   - `index.html / style.css / app.js`（vanilla JS + WebSocket，無框架）
   - 面板：auto cycle、vacuum 分區、pusher 分區、DM2J manual move、crane、raw command、log
   - 即時連線指示燈 + 分色 log（TX/OK/ERR/EVT/SYS）

7. **washrobot_new_PI/main.cpp 重寫**
   - 修正硬體對照：DM2J ×5（.20）/ ZDT ×9（.21）/ JC-100 ×9 + PQW slave 12 8CH（.22）
   - 新增 TCP_server :5001、指令集（init/attach/detach/step_down/run/pause/resume/stop/vacuum/pusher/move/arm_sweep/tilt_mode/status）
   - 自動下移時當 TCP client 連吊機 :5002 下 `pay_out <cm>` 同步放繩
   - cycle_group template：valve OFF → pusher retract → displace → pusher extend → valve ON → vacuum verify，失敗自動重試 5 次
   - 真空閥值 -50 kPa（最差值當門檻）
   - vcxproj 加入 TCP_server.cpp/h

### 待完成

- **參數實測微調**：TOTAL_DISTANCE_CM / ARM_SWEEP_CM / ARM_SWEEP_RPM / PUSHER_EXTEND_PULSE 需實測
- 17 個舊未 commit + 本次重寫 + web_backend 新增 + 3 筆 warning 修正 尚未 commit

### 編譯驗證（2026-04-12）

於 user@192.168.2.19（RPi 5 / aarch64 / g++ 14.2.0）遠端編譯雙 binary：
- `crane` 89 KB ARM aarch64 ELF，**零 warning / 零 error**
- `washrobot` 162 KB ARM aarch64 ELF，**零 warning / 零 error**

修掉的 3 筆 warning（都在既有驅動、非本次重寫引入）：
- `user_lib/DM2J_RS570.cpp:723,734` — debug printf 迴圈 `int i` → `size_t i`
- `user_lib/JC_100_METER.h` — 成員宣告順序調整
- `user_lib/JC_100_METER.cpp:22` — 建構子 initializer list 順序對齊

### 產物

- `deploy_and_test.pdf` — 部署步驟 + 6 關冒煙測試（Gate 0~6）+ 緊急操作備忘
- 產生器 `.claude/gen_deploy_pdf.py`（fpdf2 + msjh.ttc / msjhbd.ttc）

---

## 2026-04-11 (Session 3)

### 已完成

1. **DM2J_RS570 驅動修正** — 根據摘要比對結果修正
   - **`read_status`**: 從讀取 1 register (16-bit) 改為 2 registers (32-bit)，支援 Bit16 HOME_DONE
   - **`print_status`**: HOME_DONE 判斷從 `0x0040` 修正為 `0x10000`（Bit16）
   - **PR block 寫入**: `speed_move` 和 `PR_move_set` 從 6 registers 補到 8 registers（+6 dwell=0, +7 special=0）
   - **呼叫端修正**: `PR_move_cm`, `PR_move_cm_trigger_all` 內 status 變數改 uint32_t
   - **Linux_test/main.cpp**: status 變數改 uint32_t + 修正回傳值判斷（原本 true=error 時進入 print）
   - **新增函式**:
     - `motor_enable()` — 寫 0x1801=0x1111
     - `motor_disable()` — 寫 0x1801=0x2233
     - `save_params()` — 寫 0x1801=0x2222
     - `read_error_code()` — 讀 0x2203（overcurrent/overvoltage/stall 等）
     - `read_save_status()` — 讀 0x1901（0x5555=成功, 0xAAAA=失敗）

2. **ZDT_motor_control 修正**
   - `motion_control_pos_mode` + `motion_control_pos_mode_nowait`: 新增 mode 範圍驗證（僅接受 0 或 1）
   - `speed_mode` / `pos_mode` / `pos_mode_nowait`: acc 參數加註解 `0=direct start no ramp`

3. **SD76_length_meters 新增功能**
   - `readStatus()` — 讀取狀態暫存器 0x0000（工作模式 + 警報狀態）
   - `readUpperInteger()` / `readLowerInteger()` — 讀取 int32 整數值（0x0021-0x0024），替代 BCD 解碼
   - `pauseMeter()` — 暫停計米（寫 0x0004）
   - `resumeMeter()` — 恢復計米（寫 0x0008）

4. **PQW_IO_16O_RLY / ZS_DIO_R_RLY 介面統一** — 讓兩個繼電器模組可互換，上層只需改 include + 型別宣告
   - **PQW**: `controlAll()` 回傳值從 `void` 改為 `bool`（false=success），加入 echo 驗證
   - **ZS_DIO**: 新增 `readAllStatus()` 回傳 `vector<bool>`，內部包裝 `readCoils(1, relay_count)`
   - **統一介面**: `init`, `controlRelay`, `controlAll`, `readAllStatus`, `close` 簽名完全一致
   - 各自特殊功能保留不動（ZS_DIO 的 controlGroup/readCoils/readDiscreteInputs，PQW 的 checkAllStatus）

### 待完成
- 17 個修改檔 + 2 個新檔尚未 commit（含 Session 2 ZS_DIO 重寫 + Session 3 全部修正）

---

## 2026-04-11 (Session 2)

### 已完成

0. **全部摘要 PDF 圖片 OCR 驗證** — 用 PyMuPDF 轉圖片後以視覺方式逐份比對 PDF 原文
   - **SD76** — 修正 TIA1/TIA2 為唯讀；補充 0x0000 高字節工作模式；明確 AL1/AL2 需用 FC 0x10；補充錯誤碼
   - **PQW** — 補充模式值 2=聯動(保留)、6=手動(保留)；補充 Input Register (0x04) 地址映射；補充看門狗公式；展開波特率對應值
   - **DM2J** — 修正 HOME_DONE Bit6→Bit16；修正錯誤碼（移除不存在的 0x09，補充 0x80/0x100/0x200）；補充 PR block offset +6/+7；補充命令字 0x1122/0x2222；補充保存狀態字 0x1901
   - **ZDT** — 修正 Microstep 暫存器 0x0084→0x00B4；修正 mode 欄位移除不存在的 0x02；補充 acc=0 直接啟動行為；補充掉電標誌 3.5.3；補充回零參數 3.3.6
   - **JC-100** — 核心正確無需修正
   - **ZS_DIO** — 先前已驗證正確

1. **ZS_DIO_R_RLY 驅動重寫** — 比對 `doc/ZS_DIO数字量输入输出系列使用手册(RS485&RS232版).pdf`
   - PDF 因中文編碼無法用 pdftotext 擷取，改用 PyMuPDF 轉圖片後以視覺方式閱讀
   - **已修正：** init 簽名新增 `int ID` 參數，與其他驅動統一（原本硬寫 slave_id=0x01）
   - **已修正：** `buildRelayCommand()` 改名 `buildWriteRegCmd()`，使用 `slave_id` 成員
   - **已修正：** 控制函式移除硬等 delay，改用 `sendAndReceive()` 等待 echo 回傳
   - **已修正：** 控制函式回傳 `bool`（false=成功），加入 echo 驗證 + 重試 3 次機制
   - **新增功能：**
     - `controlGroup()` — 按位批量控制（0x06, reg 0x0035~0x0037）
     - `readCoils()` — 讀取繼電器輸出狀態（0x01）
     - `readDiscreteInputs()` — 讀取離散輸入狀態（0x02）
     - `readGroupState()` — 讀取群組狀態 bitmask（0x04, reg 0x0030~0x0032）
     - `verifyEcho()` — echo 比對 + Modbus exception 檢查
     - `printHex()` — debug hex dump
   - **確認正確：** `controlAll()` 使用 reg 0x0034（批量控制，值 0=全關 1=全開），非站號暫存器
   - **確認正確：** `controlRelay()` 使用 0x06 寫 reg 0x0000~0x002F，值 0/1
   - **確認正確：** CRC16 Modbus（0xFFFF 初始值，0xA001 多項式）
   - 呼叫端更新：`Crane_control_PI/main.cpp` + `main - 複製.cpp` 的 init 加入 ID=1 參數
   - **已建立摘要：** `.claude/summaries/ZS_DIO_MODBUS_SUMMARY.md`

2. **CLAUDE.md 電源架構修正** — 移除 LED 電源項目（AC 220V 直接供電與 LED 無關）

3. **全域規範新增** — `agent_ai/CLAUDE.md` 通用規則加入「暫存檔案必須放在 agent_ai/ 內」

### 保持寄存器對應表（0x06）
| 暫存器 | 用途 |
|---|---|
| 0x0000~0x002F | 個別通道控制（5 種模式） |
| 0x0030 | 通訊檢測時間 |
| 0x0031 | 輸入口狀態上傳控制 |
| 0x0032 | RS485 站號（1~255） |
| 0x0033 | 鮑率設定（0=4800...7=115200） |
| 0x0034 | 批量控制（0=全關, 1=全開） |
| 0x0035~0x0037 | 按位群組控制（1~16/17~32/33~48） |
| 0x003D | 奇偶校驗 |
| 0x0096~0x00C6 | 各通道工作模式設置 |

---

## 2026-04-11

### 已完成
- **CLAUDE.md 架構大更新** — 根據最新硬體配置重寫整份架構文件
  - 新增系統主體架構圖（Fathom-X → PoE Switch → RPi5 + 3 組 RS485）
  - RS485_1 (192.168.1.20): DM2J_RS570 × 5（左腳/左輪/右腳/右輪/上滑台）
  - RS485_2 (192.168.1.21): ZDT × 9（驅動 SMC LEYG25 推桿，分左腳/左身體/右腳/右身體/中心）
  - RS485_3 (192.168.1.22): JC_100 × 9 (Slave 1~9) + DY_500 × 2 (Slave 10~11) + PQW 8CH (Slave 12)
  - 新增吸盤控制邏輯（dp0105 + VT307 分區控制）
  - 新增電源架構（A/B/C 組 + 5V 變壓器）
  - 新增 4 支 PoE 攝影機（左上/左下/右上/右下）
  - USB→CAN 機械手臂標記為未來擴充
  - SD76、ZS_DIO_R_RLY 標記為備用
  - 驅動列表重新分類（使用中/備用/未使用）
  - 更新 Repository Structure 說明
- **remote_hosts 架構建立** — 新增 `remote_hosts/` 資料夾 + CLAUDE.md 規範
- **192.168.2.19 (raspberry5-MQTT)** — SSH key 設定 + APT full-upgrade 完成
- **上層 CLAUDE.md 建立** — `agent_ai/CLAUDE.md` 全域規範，`projects/CLAUDE.md` 精簡化

---

## 2026-04-10 (Session 3)

### 已完成
7. **SD76_length_meters 回傳值修正** — 比對 `doc/計米器/SD76-C/` 兩份 docx
   - Modbus 協議：register 地址、FC 0x03/0x06、CRC16、BCD 解碼全部正確
   - **已修正：** 回傳值規範 — `sendModbus`, `readRegister`, `readUpperDisplayValue`, `readUpperLowerDisplayValue`, `writeSingleRegister` 全部改為 `false=success`
   - `resetAll()` 直接 return `writeSingleRegister()`，自動傳遞
   - 呼叫端（Crane_control_PI）未判斷回傳值，不需修改
   - **注意：** `decodeSignedBCD6` 從 byte[0] bit7 取負號，文件未記載此用法，可能是廠商慣例
   - **已建立摘要：** `.claude/summaries/SD76_MODBUS_SUMMARY.md`

8. **PQW_IO_16O_RLY 程式碼審查** — 比對 `doc/数字量输出系列/` PDF
   - Modbus 協議：FC 0x05 單線圈、FC 0x06 全開關(0x0085)、FC 0x01 讀線圈狀態，全部正確
   - 線圈地址、ON/OFF 值(0xFF00/0x0000)、CRC16 全部正確
   - **已修正：** 回傳值規範 — `init` (兩個 overload), `controlRelay` 改為 `false=success`
   - **已修正 Bug：** constructor 未初始化 `client=nullptr`，解構函式無條件存取未初始化指標 → 加 null check
   - **已修正 Bug：** `init` Mode A 使用 `client->connectToServer` 但未先 `new TCP_client()` → 加入配置
   - **已修正：** 加入 `owns_client` flag + destructor 中 `delete client`（與其他驅動一致）
   - 呼叫端 `washrobot_new_PI/main.cpp` L165: `if (!relay.init(...))` → `if (relay.init(...))`
   - **已建立摘要：** `.claude/summaries/PQW_IO_MODBUS_SUMMARY.md`

### 全部驅動審查完成
- ✓ ZDT_motor_control (Session 1 + Session 3 補漏)
- ✓ DM2J_RS570 (Session 2)
- ✓ JC_100_METER (Session 2)
- ✓ DY_500_weight_sensor (Session 2)
- ✓ SD76_length_meters (Session 3)
- ✓ PQW_IO_16O_RLY (Session 3)

9. **ZDT_motor_control 補漏修正** — Session 1 只改了 speed_mode 和 pos_mode_nowait，其餘 13 個函式漏改
   - **已修正：** init (2 overloads), set_zero, calibrate_encoder, reset_motor, motion_control_driver_EN, get_system_status, wait_until_pos_reached, release_stall_flag, emergency_stop, factory_reset, trigger_home, abort_home, trigger_sync_move, set_home_zero_position
   - **已修正：** motion_control_pos_mode 內 wait_until_pos_reached 呼叫翻轉
   - 呼叫端：washrobot_new_PI/main.cpp 7x m*.init, Linux_test/main.cpp 2x m1.init + 1x get_system_status

### Git
- commit 1: `e980602` — 全部驅動修正 + 摘要 + 清理 jc100 png + .gitignore 更新
- commit 2: `7c5fd29` — ZDT 補漏修正 (已 push)

---

## 2026-04-10 (Session 2)

### 已完成
1. **建立 Claude 工作日誌規範** — 全域 `CLAUDE.md` 新增規範，每個專案下 `.claude/` 資料夾
2. **搬移舊摘要** — `doc/ZDT/ZDT_MODBUS_SUMMARY.md` → `.claude/summaries/`
3. **DM2J_RS570 程式碼審查** — 比對 `doc/DM2J-RS.V1.pdf`
   - Modbus 協議：所有暫存器、指令、CRC 全部正確
   - **已修正：** 回傳值規範 — 所有 bool 函式改為 `false=success`（影響 init, read_*, PR_move_cm*, writeSingle, writeMulti, sendRecv）
   - **已修正：** 呼叫端同步更新（washrobot_new_PI/main.cpp, Linux_test/main.cpp）
   - **已修正：** `set_jog_dec` header 註解 0x01E8 → 0x01E7（與 acc 共用 Pr6.03）
   - **已修正：** `writeSingle`/`writeMulti` 變數命名（`int err = sendRecv` → 直接 return / `bool err`）
   - **已修正：** 移除 `read_status` 多餘 100ms delay
   - **已修正：** 移除 `read_pulse_per_rev` 接收後多餘 200ms delay
   - **未改：** `PR_move_cm_nowait`/`PR_move_cm_set` 硬編碼 PPR=10000（效能考量保留）
4. **DM2J-RS Modbus 摘要** — 建立 `.claude/summaries/DM2J_RS_MODBUS_SUMMARY.md`
5. **JC_100_METER 程式碼審查** — 比對 `doc/真空氣壓表/JC-100-RS485产品说明书.pdf`
   - Modbus 協議：全部 13 個暫存器正確，function codes 正確，CRC 正確
   - **已修正：** 回傳值規範 — `init`, `send_command` 改為 `false=success`
   - 內部 `get_*` 條件判斷全部同步翻轉
   - 呼叫端 `washrobot_new_PI/main.cpp` 7 個 meter init 同步翻轉
   - 程式碼品質良好，無其他 Modbus 問題

6. **DY_500_weight_sensor 程式碼審查** — 比對 `doc/DY500說明書V1.5中性.pdf`
   - Modbus 協議：全部 31 個 LONG 暫存器 + FLOAT 地址正確，do_clear 正確
   - **已修正：** 回傳值規範 — init, modbus_read/write, read_reg_long, get_weight_*, get_decimal_point, do_clear, read_all_parm 全部改為 `false=success`
   - **已修正：** `read_all_parm` 聚合邏輯 `ok &= ...` → `err |= ...`
   - **發現並修復 Crane bug：** `Crane_control_PI/main.cpp` 的 `get_weight_float` 呼叫端已經用 `false=success` 寫法，原本驅動回傳相反導致重量讀取邏輯反轉，修正後自動正確

### 待完成
- 其他 user_lib 驅動尚未審查：
  - `SD76_length_meters` → doc: `doc/計米器/SD76-C/`
  - `PQW_IO_16O_RLY` → doc: `doc/数字量输出系列(RS485-RS232-TTL通信接口)等4个文件/`
- 所有修改尚未 commit 到 git

---

## 2026-04-10

### 已完成
1. **Git pull** — 從 GitHub `chenchunho/washrobot_new_PI` 拉取最新，解決 `windows_test/windows_test.vcxproj` 衝突（使用 remote 版本）
2. **CLAUDE.md 更新** — 擴充專案文件：驅動細節、腳位對應、吊車演算法、並行模型、指令表
3. **ZDT_motor_control 程式碼審查** — 比對 `doc/ZDT/ZDT閉環步進馬達MODBUS協議.pdf`
   - Modbus 協議：所有指令正確（暫存器、資料格式、CRC）
   - **已修正：** `motion_control_speed_mode` 和 `motion_control_pos_mode_nowait` 回傳值與規範相反（false=success）
   - **已修正：** 變數遮蔽問題（`auto resp = readEcho(500)` → 改為直接 `readEcho(500)`）
4. **PDF 摘要建立** — ZDT Modbus 協議摘要（見 `.claude/summaries/ZDT_MODBUS_SUMMARY.md`）

### 待完成
- 其他 user_lib 驅動尚未審查：
  - `DM2J_RS570`（文件待確認）
  - `JC_100_METER` → doc: `doc/真空氣壓表/JC-100-RS485产品说明书.pdf`
  - `DY_500_weight_sensor` → doc: `doc/DY500說明書V1.5中性.pdf`
  - `SD76_length_meters` → doc: `doc/計米器/SD76-C/`
  - `PQW_IO_16O_RLY` → doc: `doc/数字量输出系列(RS485-RS232-TTL通信接口)等4个文件/`
- 上次修改尚未 commit 到 git
