# 修改日誌

每次 Claude Code 修改檔案後，在此記錄異動內容。

格式：
```
## [日期] [修改者]
### 修改檔案
- `路徑/檔案.cpp` — 說明
### 原因
...
```

---

<!-- 日誌從下方開始 -->

## 2026-04-15i — Claude Code — Phase 4 真空失敗後退 5cm 重試機制

### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - 加常數 `VACUUM_BACKUP_CM = 5.0`
  - 加成員 `rail_pos_cm_`（atomic）、`body_residual_cm_`（atomic）、`actual_feet_cm_`
  - 重寫 `cycle_group_` template 簽名：`(group, pre_cycle, backup, out_retry_count)` — pre_cycle 只跑一次做 crane + DM2J 大位移，backup 每次重試前做 5cm 小反向位移
- `user_lib/WASH_ROBOT.cpp` —
  - Constructor 初始化新成員
  - 重寫 `do_step_down_()`：腳組 / 身體組各用 pre_cycle + backup lambda 分離
    - 腳組 pre_cycle：閥關 + 推桿縮 + crane pay_out + DM2J abs `+STEP_CM` + crane retract
    - 腳組 backup：DM2J relative `-5cm`（rail 反向）
    - 身體組 pre_cycle：中心閥關 + 中心推桿縮 + 身體推桿縮 + DM2J abs `0` + 中心推桿伸 + 中心閥開
    - 身體組 backup：DM2J relative `+5cm`（rail 反向）
  - 絕對定位自動吸收 `body_residual_cm_`（身體上步沒回到原點的量）
  - 身體組重試次數 → `body_residual_cm_ = 5 × retries`，下次腳組絕對定位自動補償
  - `cmd_status` 加 `rail=<cm>` 與 `body_residual=<cm>` 輸出

### 原因
依 spec 與使用者 2026-04-15 討論實作真空失敗後退 5cm 機制：
- 使用 DM2J 絕對定位（mode=1）讓 body residual 自動被下次 feet 吸收，無需手動補償運算
- 腳組與身體組共用滑桿，backup 方向相反（腳組 -5、身體組 +5），都是「往對方前進的方向退」
- 重試時不重複 crane 動作（pre_cycle 只跑一次），避免累積 rope 長度
- 範例：身體組重試 1 次（只退 25cm 到 +5）→ 下次腳組絕對目標 +30，DM2J 自動只走 +25（從 +5 到 +30），不會超過行程
- body_residual/rail_pos 只做 diagnostic，不影響控制邏輯

編譯通過（aarch64, g++ -std=c++14），Phase 4 穩定性機制完成。

---

## 2026-04-15h — Claude Code — 狀態機 + reset/ping/pause/resume + 命名同步

### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 `enum class State`（Idle/Ready/Attached/Running/WaitingConfirm/Paused/Balancing/ReturningHome/Error）、`state_`/`state_before_pause_`/`state_before_wait_`/`state_mtx_` 成員、`set_state_`/`state_name`/`state_violation_`/`do_step_down_` helper；`cmd_stop` 改名 `cmd_emergency_stop`；加 `cmd_reset`/`cmd_ping`/`cmd_pause`/`cmd_resume`；`IMU_STOP_DEG` 改名 `IMU_EMERGENCY_DEG`
- `user_lib/WASH_ROBOT.cpp` —
  - Constructor 初始化 state 成員
  - 所有 cmd_* 加 state guard（state_violation_ 回 `ERR state_violation current=<state>`）
  - `cmd_init`/`cmd_attach`/`cmd_detach`/`cmd_step_down`/`cmd_run` 成功後 set_state 轉移；失敗轉 Error
  - `do_step_down_()` 抽出（無 state guard），`cmd_step_down` / `cmd_run` 共用
  - `cmd_emergency_stop` 改名 + 加 `crane_cmd_("emergency_stop")` + 轉 Error
  - `cmd_status` 輸出加 `state=<name>` 與 `roll=/pitch=`
  - `cmd_confirm_balance` 加 WaitingConfirm → Balancing → prev 狀態轉移
  - `cmd_return_home` 加 Attached/Paused/Error 前置檢查，用 fail lambda 統一轉 Error；成功轉 Idle
  - 新增 `cmd_reset` / `cmd_ping` / `cmd_pause` / `cmd_resume`
  - 手動指令（`cmd_vacuum`/`cmd_pusher`/`cmd_move`/`cmd_arm_sweep`/`cmd_tilt_mode`）在 Error 狀態下擋掉
  - `imu_monitor_loop_`：`IMU_STOP_DEG` → `IMU_EMERGENCY_DEG`、EVT `imu_stop` → `imu_emergency`、觸發時 set_state(Error)、ASK 觸發時轉 WaitingConfirm（存 `state_before_wait_`），hysteresis 清除時還原
- `washrobot_new_PI/main.cpp` — dispatch `stop` 改 `emergency_stop`，加 `reset`/`ping`；`pause`/`resume` 改呼叫 `cmd_pause`/`cmd_resume`；header 註解同步
- `.claude/motion_flow.md` — `stop` → `emergency_stop`、`imu_stop` → `imu_emergency`、`IMU_STOP_DEG` → `IMU_EMERGENCY_DEG` 三處同步

### 原因
依使用者要求做 3 件事：
1. 把容易跟 `pause` 搞混的 `stop` 改成語意明確的 `emergency_stop`（與 `ZDT_motor_control::emergency_stop()` 既有命名一致）
2. 補齊 spec §生命週期狀態 定義的 9 個狀態與轉移，指令違反狀態回 `ERR state_violation`
3. 補齊 spec 協定第 589-590 行規定的 `reset` / `ping`，讓 web/crane 能反向心跳

手動指令在 Error 下擋掉：避免操作者在未確認現場安全前操作硬體。允許在 Error 下的指令：`status`/`ping`/`reset`/`return_home`/`emergency_stop`。

---

## 2026-04-15g — Claude Code — Phase 6 Return Home 實作

### 修改檔案
- `user_lib/WASH_ROBOT.h` — 宣告 `cmd_return_home(int descent_cm)`
- `user_lib/WASH_ROBOT.cpp` — 實作 Phase 6 流程（arm 回零 → 關水 → 破真空 → 等 5s → 驗證脫附 → 收推桿 → 關泵 → crane pay_out）
- `washrobot_new_PI/main.cpp` — dispatch 加 `return_home <descent_cm>` 指令

### 原因
依 motion_flow.md §Phase 6 規格實作召回回地面流程。`descent_cm` 由 caller 傳入（washrobot 未追蹤 home_ground_cm / current_down_cm，暫由 GUI/operator 計算），crane 端 300s timeout。脫附驗證失敗時直接停機回報，不繼續放繩。

---

## 2026-04-15f — Claude Code — Crane Step 2（Watchdog 執行緒 + EVT）

### 修改檔案
- `Crane_control_PI/main.cpp` — 新增 watchdog 機制：
  - 新 atomics：`last_ping_ms` / `motion_active` / `watchdog_fired` / `watchdog_stop`
  - 常數：`WATCHDOG_TIMEOUT_MS=2000` / `HEARTBEAT_CHECK_MS=250`
  - `touch_heartbeat()`：任何 inbound 資料都刷新 last_ping_ms；若之前 fired 則發 `EVT watchdog_recovered`
  - `watchdog_loop()` thread：每 250 ms 檢查，超時且有 client 連接時：動作中 → abort + allMotionOff + `EVT watchdog_timeout state=aborted`；閒置 → `EVT watchdog_timeout state=idle`
  - `MotionScope` RAII guard 在 `motion_rope` + `cmd_roll_correct` 頭尾設定/清 `motion_active`
  - `on_receive` 開頭呼叫 `touch_heartbeat()`
  - `main()` 啟動 thread + shutdown 時 join
  - `broadcast_evt()` helper

### 原因
spec §5：washrobot ↔ crane 通訊 > 2s 無心跳視同斷線，動作中需立即停機保護機器人；閒置中僅 log。此 commit 實作 crane 端被動 watchdog（washrobot 端主動 ping 已存在）。

### EVT 格式
```
EVT watchdog_timeout state=aborted   # 動作中觸發，已停機
EVT watchdog_timeout state=idle      # 閒置中觸發
EVT watchdog_recovered               # 下次 ping 回來
```

---

## 2026-04-15e — Claude Code — Crane 重寫 Step 1（SD76 #3 + CLV900 + 新指令 handlers）

### 修改檔案
- `Crane_control_PI/main.cpp` — 整體重寫：
  - 新硬體：`meter_middle` (SD76 slave 4, 中間管線計米) + `inverter` (CLV900 slave 7, 中間絞盤變頻器)
  - `motion_rope()`（原 `cmd_pay_out`）加入中間絞盤同步：起動 CLV900 forward/reverse + `MIDDLE_WINCH_HZ` (placeholder 20 Hz)，middle 完成條件 = `|Δ| >= cm × MIDDLE_WINCH_RATIO_K`
  - 新指令：`ping` / `zero_meters <ground|top>` / `home_status` / `roll_correct <delta_cm>` / `middle_set <rpm> <pay|retract|stop>`
  - `zero_meters top` 讀 `|SD76 左|` 存 `home_ground_cm`（atomic）再三顆計米歸零
  - `roll_correct`：正值 = 左放右收，中間絞盤不動
  - `cmd_stop` / `allMotionOff` 同時關繼電器 + CLV900.stopDecel
- `Crane_control_PI/Crane_control_PI.vcxproj` — 加入 `CLV900_inverter.{cpp,h}` 編譯清單；修正 `AdditionalIncludeDirectories` 從別人電腦硬寫死的 `C:\Users\Administrator\source\repos\...` 改為相對路徑 `..\user_lib`

### 原因
washrobot 端已呼叫 `pay_out`/`retract`/`roll_correct`/`zero_meters`/`home_status`/`ping` 等新協定，crane 端需對應實作，否則分散式無法上機。本 commit 為 Step 1（核心功能），未含 watchdog 執行緒（Step 2）與張力 monitor（Step 3，等 DSZL-107 驅動）。

### 待確認 / TODO
- `MIDDLE_WINCH_HZ` = 20 Hz 為 placeholder，實機測轉速後調整
- `middle_set` 的 rpm→Hz 換算假設 4 極 50 Hz → 1500 rpm，需實機驗證馬達規格
- CLV900 direction convention（forward=pay / reverse=retract）wiring-dependent，實機若反向則翻轉

---

## 2026-04-15d — Claude Code — cmd_init 補 CH5/CH6/CH7 關閉

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_init()` 補上 CH_BRUSH/CH_WATER_PUMP/CH_WATER_INLET 三個 relay 明確設為 OFF（對應 motion_flow.md Phase 2 步驟 5-7）

### 原因
硬體開機 `init()` 的 safe startup 雖已關閉這三路，但 `cmd_init()` 重送時（如崩潰後 re-init）未顯式關閉，造成狀態不確定。

---

## 2026-04-15c — Claude Code — arm_sweep 加清洗動作

### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 CH5/CH6/CH7 relay 常數：`CH_BRUSH=5`, `CH_WATER_PUMP=6`, `CH_WATER_INLET=7`
- `user_lib/WASH_ROBOT.cpp` — `do_arm_sweep_()` 加入清洗 relay 控制：掃臂前開 CH7→CH6→CH5，掃臂後（無論成功失敗）關 CH5→CH6→CH7；`cmd_shutdown()` + `init()` safe startup 補上這三個 relay 的關閉

## 2026-04-15b — Claude Code — Phase 5 吸盤狀態切換補完

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_phase5_roll_correct_()` 補上 Phase 5 spec 規定的吸盤切換：
  - 前置：CH2(feet) OFF + CH3(body) OFF + CH4(center) ON，驗證中心真空OK才繼續
  - 每次差動前再確認中心真空（若中心脫落立即中止）
  - 校正完成（或失敗）後：CH3 ON → 等 VACUUM_SETTLE_MS → CH2 ON 恢復全吸附

### 原因
舊版直接叫吊機差動，未先切換吸盤狀態，導致 8 顆腳+身體全吸住的情況下強拉鋼索，機器人無法旋轉校正。

## 2026-04-15 — Claude Code — IMU bug fix + Linux_test IMU 測試

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — 修正兩個 bug：
  1. `Serial_port::init()` 回傳 `true=success`（非專案慣例），WASH_ROBOT 的判斷缺少 `!` → 把成功當 fatal error；加上 `!` 修正
  2. `cmd_step_down()` 持有 `motion_mtx_` 後呼叫 `cmd_arm_sweep()`（也持有同一 mutex）→ deadlock；改呼叫不加鎖的 `do_arm_sweep_()`
- `user_lib/WASH_ROBOT.h` — 新增 private `do_arm_sweep_()` 宣告
- `Linux_test/main.cpp` — 改寫為選單式測試程式：選項 1 = IMU 連續讀取（Roll/Pitch/Yaw/Pressure/Alt），選項 2 = DM2J 快速移動測試

### 原因
- Serial_port 使用標準慣例（true=success），不同於專案慣例；需加 `!` 才能正確判斷失敗
- mutex deadlock 修正（同 session 發現）

## 2026-04-14d — Claude Code — OOP 重構：WashRobot class

### 修改檔案
- `user_lib/WASH_ROBOT.h` — 全新 WashRobot class 宣告（覆蓋舊版）：所有硬體成員（TCP_client×4, DM2J×5, ZDT×9, JC×9, PQW, IMU）、背景執行緒（crane watchdog + IMU monitor）、狀態 atomics、evt_cb callback、完整 cmd_*() public 方法、private helpers、cycle_group_ template
- `user_lib/WASH_ROBOT.cpp` — 全新實作（覆蓋舊版）：從舊 main.cpp 搬移所有 static function 成 class method；body_displace 改用 dm2j_wait_done_ 取代舊 sleep_ms_(4000)
- `washrobot_new_PI/main.cpp` — 縮減為薄包裝層（~120 行）：TCP server + dispatch → robot.cmd_*()；main() 注入 evt_cb，呼叫 robot.init()，shutdown 呼叫 robot.stop()
- `washrobot_new_PI/washrobot_new_PI.vcxproj` — 新增 Serial_port.cpp/.h、WASH_ROBOT.cpp/.h、WT901BC_TTL.cpp/.h

### 原因
OOP 重構：business logic 移入 WashRobot class，main.cpp 純 I/O 層，職責清晰。

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
