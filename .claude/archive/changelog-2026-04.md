# changelog — 2026-04(全文,原封)

> 📌 2026-09-12 由 `changelog.md` **輪替**搬入,內容一字不少(append-only 未破,只是分檔)。
> 主檔 `changelog.md` 保留當月全文 + 全部條目索引。本檔 8 條,新的在上。

## [2026-04-14c] Claude Code — IMU 整合（WT901BC baseline + 監控 + Phase 5）

### 修改檔案
- `washrobot_new_PI/main.cpp`

### 變更內容
1. 新增 includes：`Serial_port.h`、`WT901BC_TTL.h`、`<deque>`、`<iomanip>`
2. 新增 IMU 常數：`IMU_PORT / IMU_BAUD / IMU_ASK_DEG(15°) / IMU_STOP_DEG(45°) / IMU_BASELINE_SEC(3) / IMU_HYSTERESIS_DEG(1°) / ROLL_CORRECT_CM_PER_DEG / ROLL_CORRECT_RETRY_MAX`
3. 新增 globals：`imu_serial / imu / imu_roll0 / imu_pitch0 / imu_ask_pending / imu_mon_running / imu_mon_thread`
4. 新增 `imu_take_baseline()`：取 3 秒 100ms 取樣平均，存 roll0/pitch0；`do_init()` 最後呼叫
5. 新增 `do_phase5_roll_correct()`：送 `roll_correct <delta_cm>` 給 crane，最多重試 5 次，收斂條件 `|Δroll| < 1°`
6. 新增 `imu_monitor_loop()`：100ms 取樣，10 樣本 1s 滑動平均，持續 500ms 超標才觸發；`>45°` → abort + crane stop；`>15°` → imu_ask_pending + EVT balance_ask
7. `do_run()` 每步後檢查 `imu_ask_pending`，若 true 則 `pause_flag=true` 等 confirm_balance
8. 新增 `confirm_balance <yes|no>` 指令：yes → Phase 5 → 清 pending/pause；no → 直接清
9. `main()` 加 IMU serial init + imu.init + monitor thread 啟動；shutdown 時 stop + join

### 原因
- 依據 motion_flow.md §5 及 work_log 2026-04-12 確認規格實作

---

## [2026-04-14b] Claude Code — Crane watchdog

### 修改檔案
- `washrobot_new_PI/main.cpp`

### 變更內容
1. 新增常數 `HEARTBEAT_INTERVAL_MS = 500`、`WATCHDOG_TIMEOUT_MS = 2000`
2. 新增 globals：`motion_active`、`crane_last_ok_ms`、`crane_wd_running`、`crane_wd_thread`
3. 新增 `now_ms()` utility
4. `crane_cmd` 加 `timeout_sec` 參數（預設 30s，ping 用 2s），並在 OK 回應時更新 `crane_last_ok_ms`；舊的 180s hardcode 移除
5. 新增 `crane_watchdog_loop()`：每 500ms 送 `ping`，若 `crane_last_ok_ms` 超過 2000ms 未更新 → 動作中則 `abort_flag=true` + EVT，閒置中只推 EVT
6. `do_step_down` / `do_run` 頭尾設定 `motion_active`；`do_stop` / `do_shutdown` 清除
7. `main()` 啟動 watchdog thread；shutdown 時 `join()`

### 原因
- 通訊斷線時機器人必須立即停機，是最底層安全保障

---

## [2026-04-14] Claude Code — Phase 4-A margin 修正 + DM2J 同步等待

### 修改檔案
- `washrobot_new_PI/main.cpp`

### 變更內容
1. 新增常數 `STEP_MARGIN_CM = 15`
2. 新增 helper `dm2j_wait_done(slave, timeout_ms)` — poll 單顆 DM2J slave 的 CMD_DONE + PATH_DONE，取代舊的固定 `sleep_ms(4000)`
3. 重寫 `do_step_down()` 內的 `feet_displace` lambda：
   - 修正順序：crane 先 `pay_out (STEP_CM + STEP_MARGIN_CM)` 等 OK，再啟動 DM2J，DM2J 完成後 crane `retract STEP_MARGIN_CM`
   - 改用 `PR_trigger_sync`（廣播觸發一次）+ 分別 `dm2j_wait_done` 等左右腳，取代舊的 `PR_trigger_sync` + 固定等待

### 原因
- 舊版只放剛好 STEP_CM 的繩，鋼索可能仍有張力，阻礙 DM2J 腳下移
- 舊版先發 DM2J 指令才放繩，順序錯誤
- 舊版用 `sleep_ms(4000)` 等待，無法感知實際到位與否

## [2026-04-13d] Claude Code — testSingleLegWash 重構 + adjustLegPos 修正

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`
  - `adjustLegPos()` 頂部加上 `if (leg.sensor == nullptr)` 保護，無感測器時直接回傳 0（避免 nullptr 解參考）
  - `testSingleLegWash()` 全面重構：
    1. 在 init 後建立 `Leg` struct，將本地裝置指標填入（含 `sensor = has_sensor ? &sensor : nullptr`）
    2. 初始化完成後立即伸腳（初始吸附狀態）
    3. **FORWARD PHASE**：循環起頭才解真空 → 縮腳 → 前進 `step_cm` → 啟動真空 + 伸腳 → `adjustLegPos(leg)`，回傳 `adj` 計算 `actual_step = step_cm + adj`
    4. **BACKWARD PHASE**：不解真空（valve 保持 ON）→ 僅縮腳 → 後退 `actual_step` → 伸腳 → `adjustLegPos(leg)` 確認吸附
    5. 迴圈末尾真空保持 ON，下一次前進才釋放

### 原因
用戶需求：前進吸附後用 adjustLegPos 確認，根據退後補償量計算實際步長；往回滑動時持續吸著牆壁，下次前進時才解真空。

---

## [2026-04-13c] Claude Code — bugfix

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`
  - `testSingleLegWash()` 中 `rly.init(tcp, cfg.relay_slave, false)` 改為 `rly.init(tcp, cfg.relay_slave)`
  - 原因：第三參數對應 `total_relay`（預設 16），傳 `false`（=0）會讓 relay_count=0，之後 parseReadResponse 存取空 vector 造成 segfault
- `user_lib/PQW_IO_16O_RLY.cpp`
  - 解構子從 `if (client)` 改為 `if (client && owns_client)`
  - 原因：外部 client 模式（owns_client=false）不應呼叫 client->close()，否則共用 TCP 連線會被提早關閉

### 原因
test leg wash 執行時出現 segfault，根因為 PQW init 的 total_relay 參數被錯誤傳入 false=0。

---

## [2026-04-13b] Claude Code

### 修改檔案
- `washrobot_new_PI/main.cpp`
  - 移除啟動時自動執行的 `robot.init()` / `robot.doInit()`，改為指令
  - 新增指令 `init`（呼叫 robot.init()，設定 robot_initialized = true）
  - 新增指令 `doinit`（需先 init）
  - 所有需要 robot 初始化的指令加上 `robot_initialized` 檢查，未 init 時印 "Run 'init' first."
  - `test leg wash` 不需要 init 即可直接執行

### 原因
測試單隻腳時，主機器人的 init() 會嘗試連所有 8 個裝置，timeout 累積造成 hang。

---

## [2026-04-13] Claude Code

### 修改檔案
- `user_lib/WASH_ROBOT.h`
  - 新增 `SingleLegTestConfig` struct（網路連線、slave ID、繼電器通道、動作參數全部可設定）
  - 新增 public 方法宣告 `testSingleLegWash(cfg, cycles, step_cm)`
- `user_lib/WASH_ROBOT.cpp`
  - 新增 `testSingleLegWash()` 實作：建立獨立 TCP 連線、初始化單獨裝置（ZDT/DM2J/JC100/PQW）、執行來回洗窗迴圈、每次到位後讀壓力並報告
- `washrobot_new_PI/main.cpp`
  - 在 main() while 迴圈前加入 `SingleLegTestConfig testCfg` 設定區塊（有 ★ 標記，換腳改這裡）
  - 新增指令 `test leg wash <cycles> <step_cm>`

### 原因
用戶需要可獨立設定 slave ID / IP 的單腳來回洗窗測試 function。

---

## [2026-04-12] Claude Code

### 修改檔案
- `user_lib/WASH_ROBOT.h`
  - 新增 `RobotConfig` namespace（config 區塊，陣列形式，8 腳 slave ID、繼電器通道、動作參數）
  - `Leg` struct 新增欄位 `vacuum_motor_ch`（每腳抽真空馬達繼電器通道，0 = 不控制）
  - `m[7]`、`meter[7]` 擴充為 `m[8]`、`meter[8]`
  - 新增 `LegGroup body_group`（上部分 4 腳）、`LegGroup foot_group`（下部分 4 腳）
  - 舊常數重命名為 `COMPAT_RELAY_*` 避免與 `RobotConfig::RELAY_VACUUM_MOTOR[]` 衝突
  - 新增 public 方法 `void startCleaningAll(int step_cm)`
- `user_lib/WASH_ROBOT.cpp`
  - `initDevices()` 改用 `RobotConfig` 陣列初始化全部 8 個 ZDT 馬達 + JC100 感測器（非致命失敗）
  - `setupGroups()` 新增 body_group / foot_group 設定，滑桿 axes[0]/axes[1] 改用 RobotConfig slave ID
  - `enableGroup()` / `disableGroup()` 加入 `vacuum_motor_ch` 控制（含 0 值保護）
  - `moveSync()` 更新為左右滑桿（axes[0] + axes[1]）同步
  - `adjustLegPos()` 使用 `RobotConfig::PRESSURE_THRESHOLD`、`ADJUST_BACK_CM`、邊界常數；修正舊 `sleep(0.5)` 為 `Sleep(500)`
  - 新增 `startCleaningAll(int step_cm)` 實作（Phase A: body 移 / Phase B: foot 移，迴圈含位移補償）
- `washrobot_new_PI/main.cpp`
  - 新增指令 `start cleaning all with step <cm>`，解析整數後呼叫 `robot.startCleaningAll(step)`

### 原因
用戶說明 8 腳架構（body_group 上部 / foot_group 下部），每腳有獨立真空馬達與繼電器通道，需要彈性 config 與主清洗流程。

