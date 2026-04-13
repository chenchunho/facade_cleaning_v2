# Work Log

## 2026-04-13

### 已完成（規格增補，`motion_flow.md` 本次主要異動）

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
