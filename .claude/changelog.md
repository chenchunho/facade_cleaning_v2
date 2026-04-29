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

## 2026-04-29a — Claude Code — body 推桿伸長 10 cm → 9.5 cm（PUSHER_EXTEND_BODY_PULSE 30000 → 28500）

### 修改檔案
- `user_lib/WASH_ROBOT.h:152`：`PUSHER_EXTEND_BODY_PULSE` 30000 → **28500**（~10 cm → ~9.5 cm）

### 換算
body 校準：30000 pulses ≈ 10 cm → 3000 pulses/cm。9.5 × 3000 = **28500 pulses**。

### 影響範圍
所有 body 推桿伸長動作會用新值（少 0.5 cm）：
- `cmd_init_impl_` body extend
- `cmd_pusher body extend`
- `cmd_pusher all extend`（body 那一段）
- `do_step_down_` body 階段（cycle_group_ template 內 PUSHER_EXTEND_BODY_PULSE）
- staged extend 的 half_pulse（body）會變 14250

### 為什麼
使用者要求縮短 body 伸長量到 9.5 cm（從 10 cm）。

## 2026-04-28x — Claude Code — PUSHER_RPM_RETRACT 250 → 100

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`PUSHER_RPM_RETRACT` 250 → **100**

### 原因
04-28w 才剛改 250（extend 500 的半速），現場實測仍太硬。再降到 100 RPM（extend 的 1/5）。

### 速度對照（最新）
| 動作 | RPM |
|---|---|
| extend | 500 |
| retract（含 half / full / 所有 group / 手動） | **100** |

### 預期副作用
retract 時間再次拉長（500/100 = 5 倍 vs 原版 1000 RPM）。step_down 體感再慢一些。

## 2026-04-28w — Claude Code — ZDT retract 速度獨立慢（PUSHER_RPM_RETRACT = 250）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 新增 `PUSHER_RPM_RETRACT = 250`（extend `PUSHER_RPM = 500` 的半速）
  - `pusher_move_` / `pusher_move_many_` signature 加 `int rpm = PUSHER_RPM` 參數（default 不破壞 extend 呼叫端）
  - `cycle_group_` retry 路徑兩個 retract（half + full）改傳 `PUSHER_RPM_RETRACT`
- `user_lib/WASH_ROBOT.cpp`：
  - `pusher_move_` / `pusher_move_many_` impl 用參數 rpm 取代寫死的 PUSHER_RPM
  - 所有 retract 呼叫加 `PUSHER_RPM_RETRACT` 第三參數：
    - body_pre_cycle center half + full
    - body_pre_cycle body half + full
    - feet_pre_cycle feet half + full
    - cmd_pusher manual feet/body/center retract（half + full）
    - cmd_pusher manual all retract（feet/body/center 各 half + full = 6 個）
    - cmd_return_home pusher retract

### 速度對照
| 動作 | RPM |
|---|---|
| extend（任何 group / phase）| **500**（PUSHER_RPM）|
| retract（任何 group / phase / half / full）| **250**（PUSHER_RPM_RETRACT）|

### 沒影響
- DM2J 動作（PR_move_cm 等）速度走 DM2J_RPM=200，不變
- ZDT 廠商工具讀回的速度顯示不變（這個是 RPM 設定，不是 max_speed）

### 預期副作用
- 所有 retract 動作時間加倍
- step_down 每步多花約 30~40 秒（feet/body 各 2 個 retract phase × 2 倍時間）

### 待驗
- [ ] 跑 step_down → 觀察 retract 段是否明顯比 extend 慢
- [ ] 觀察是否減少 ZDT stall 發生率

## 2026-04-28v — Claude Code — 兩段式 retract 擴及所有 ZDT（含 center / 手動）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `body_pre_cycle` center pusher：one-shot 0 → two-stage（half=PUSHER_EXTEND_PULSE/2 → 1s → 0）
  - `cmd_pusher("retract")` for feet / body / center：全部改 two-stage
  - `cmd_pusher("all", "retract")`：拆成 feet / body / center 三段式（各自 half pulse）
- `user_lib/WASH_ROBOT.h` `cycle_group_` template retry：拿掉 `if (group == "feet" || group == "body")` 條件，所有 group（含 center）都走 two-stage

### 行為對照
| 觸發點 | 改前 | 改後 |
|---|---|---|
| body_pre_cycle 收 center | one-shot 0 | half=15000 → 1s → 0 |
| body_pre_cycle 收 body 4 顆 | (04-28u 已 two-stage) | 不變 |
| feet_pre_cycle 收 feet 4 顆 | (04-28u 已 two-stage) | 不變 |
| cycle_group_ retry 收 center / fallback | one-shot | two-stage |
| GUI feet RETRACT | one-shot | two-stage |
| GUI body RETRACT | one-shot | two-stage |
| GUI center RETRACT | one-shot | two-stage |
| GUI all RETRACT | one-shot 0 全部 | feet two-stage → body two-stage → center two-stage（依序）|

### 結論
**所有「ZDT 收推桿到 0」的路徑都用兩段式**。沒有例外。

## 2026-04-28u — Claude Code — vacuum wait 4s + ZDT 半速 + feet/body 兩段式縮

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `VACUUM_RELEASE_WAIT_MS` 2000 → **4000 ms**
  - 新增 `RETRACT_HALF_WAIT_MS = 1000`（兩段式縮的中間等待）
  - `PUSHER_RPM` 1000 → **500**（伸縮速度半速）
  - `cycle_group_` template retry 路徑：feet/body 群組改兩段式縮（half → wait → full）；center / 其他群組維持 one-shot
- `user_lib/WASH_ROBOT.cpp`：
  - `body_pre_cycle`：body 4 顆 ZDT 改兩段式（center 維持 one-shot）
  - `feet_pre_cycle`：feet 4 顆 ZDT 改兩段式

### 行為
| 步驟 | 改前 | 改後 |
|---|---|---|
| valve OFF 後等多久 | 2 秒 | **4 秒** |
| ZDT 速度 | 1000 RPM | **500 RPM**（半速）|
| body group 收推桿 | one-shot 0 | half (15000) → 等 1s → 0 |
| feet group 收推桿 | one-shot 0 | half (11500) → 等 1s → 0 |
| center 收推桿 | one-shot 0 | one-shot 0（不變）|
| 手動 cmd_pusher | one-shot 0 | one-shot 0（不變，使用者自己控制）|

### 為什麼
解真空後 cup 邊緣黏吸需要時間鬆開、加上推桿原速太急會在 cup 沒鬆時 stall。兩段式 + 半速 + 等更久三招同時治。

### 預期副作用
單次 step_down 體感變慢：
- valve OFF 等：+2s × 2 phases = +4s
- 兩段式：+1s × 2 phases = +2s
- 速度半 → 推桿動作時間 ×2（每段 ~6s 變 ~12s，body+feet 6 段 → +約 36s）
- 累計每步 step_down 增加 ~40 秒左右

如果太慢之後可以調回。

## 2026-04-28t — Claude Code — Linux_test 腳組 extend 對齊 7 cm → 8 cm

### 修改檔案
- `Linux_test/main.cpp`：
  - `PUSHER_EXTEND_FEET_PULSE` 常數 20000 → 23000
  - 註解 `feet pushers reach ~7cm at 20000 pulses` → `~8cm at 23000 pulses`
  - 所有 cout `extend feet pushers ~7 cm` → `~8 cm`（共 8 處）

### 對齊
跟 04-28s 主程式的 WASH_ROBOT.h 數值同步。Linux_test menu 7/8/11/12 等用到腳組伸出的選項都跟著走 8 cm。

### 沒動的
- `cm_per_degree` 估算函式 L610（用 `7.0 / (20000 × 360 / 51200) ≈ 0.04978`）：那是校準點，數值差很小，留著不影響顯示

## 2026-04-28s — Claude Code — 腳組推桿 extend 7 cm → 8 cm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`PUSHER_EXTEND_FEET_PULSE` 從 20000 → 23000（依 ~2857 pulses/cm 校準，8 cm ≈ 22857 取整）

### 影響
所有觸發 feet 推桿伸出的流程都自動跟著走：
- `cmd_init_impl_` 初始化伸 feet
- `do_step_down_` `cycle_group_("feet", ...)` 各 attempt 的伸出
- `cmd_pusher feet extend`（GUI 手動）
- `cmd_pusher all extend`（GUI manual all）

body 組的 `PUSHER_EXTEND_BODY_PULSE = 30000`（~10 cm）不變。

## 2026-04-28r — Claude Code — vacuum release wait 1s → 2s（新增 VACUUM_RELEASE_WAIT_MS 常數）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新增常數 `VACUUM_RELEASE_WAIT_MS = 2000`，cycle_group_ template 內 `sleep_ms_(1000)` 換用此常數
- `user_lib/WASH_ROBOT.cpp`：`body_pre_cycle` 和 `feet_pre_cycle` 內 `sleep_ms_(1000)` 換用 `VACUUM_RELEASE_WAIT_MS`

### 統一三處
| 位置 | 改前 | 改後 |
|---|---|---|
| `cycle_group_` retry 路徑 | 1000 ms | **VACUUM_RELEASE_WAIT_MS = 2000** |
| `body_pre_cycle` | 1000 ms | 同上 |
| `feet_pre_cycle` | 1000 ms | 同上 |

### 為什麼加長
1 秒對某些 cup 不夠 — 真空管路洩壓 + cup 邊緣黏吸完全鬆開需要時間。短了會在 cup 還吸住時拔推桿 → ZDT 卡（stall）。集中常數方便日後微調。

### 沒動的
- `RETURN_VACUUM_RELEASE_MS = 5000`（return_home 緊急流程用，更保守）

## 2026-04-28q — Claude Code — Linux_test 新增 menu 19：讀全部 9 顆 ZDT 位置

### 修改檔案
- `Linux_test/main.cpp`：
  - 新增 `test_zdt_positions()` — 連 .21、init 9 顆、loop 印表格（slave / pos(deg) / cm(est) / enabled / pos_reached / stall / group label）
  - menu 列加 `19  ZDT positions   — read all 9 ZDT pushers (deg + cm estimate)`
  - main loop 加 `else if (line == "19") test_zdt_positions();`
  - 按 Enter 重讀，q 回 menu

### cm 估算
依據 `WASH_ROBOT.h` 的 pusher 校正常數 + ZDT 預設 51200 ppr：
- feet (slave 1-4): `cm/deg ≈ 0.04978`（20000 pulses ≈ 7 cm）
- body (slave 5-8) + center (9): `cm/deg ≈ 0.04741`（30000 pulses ≈ 10 cm）

注意：若使用者改過 ZDT microstepping（不是預設 51200 ppr），cm 估算就不準，但 deg 仍然是真值（直接從 encoder 讀的）。

### 為什麼

bench 測試需要快速確認 9 顆推桿目前伸到哪、是否同步、是否有 stall flag — 之前每次只能 menu 3 / 6 一顆一顆查。新 menu 19 一次印 9 顆 + group label，方便對照物理位置。

### 待驗
- [ ] 9 顆全 retract 時跑 menu 19 → cm 估算應該全部接近 0
- [ ] 點 GUI `pusher feet extend` → 跑 menu 19 → feet (1-4) cm ≈ 7
- [ ] 點 `pusher body extend` → body (5-8) cm ≈ 10
- [ ] 故意送很大的 target 看 stall_flag 是否亮 Y

## 2026-04-28p — Claude Code — crane-link-badge 改反映 crane_attached（不是 web→crane TCP）

### 修改檔案
- `web_backend/public/index.html`：badge label 從「connection 狀態」改成「crane 驅動狀態」
- `web_backend/public/app.js`：
  - `setDot` 還原成原本只切 `.ok` class（不再 mirror 到 badge）
  - `crane_attached=on/off` parser 同時更新 `#crane-link-badge` 文字 + 顏色 class
- 樣式 `.link-badge` 不變（仍用 04-28o 的綠/紅樣式）

### 行為更正
| crane_attached 值 | badge 顯示 | class |
|---|---|---|
| `on` | `🟢 ATTACHED (washrobot 驅動)` | `link-ok`（綠底）|
| `off` | `⚪ DETACHED (skip)` | `link-down`（紅底脈動）|
| 未知（沒收到 status）| `? unknown` | 預設灰底 |

### 為什麼改
之前 04-28o 把 badge 接 setDot（dotC），反映 web→crane TCP 連線。但使用者要的是 washrobot 的 `crane_attached_` flag（決定 step_down 等流程是否送命令到 crane）。這兩個是不同層的訊息，前者由 web_backend 維護、後者由 washrobot 內部 atomic flag。

### 部署
只動前端兩檔（HTML 文字、JS 邏輯），scp + Ctrl+Shift+R reload。

## 2026-04-28o — Claude Code — GUI crane panel 加連線狀態 badge（鏡像 #dot-crane）

### 修改檔案
- `web_backend/public/index.html`：crane panel 加新 row「connection 狀態」 + `<span id="crane-link-badge">`
- `web_backend/public/app.js`：`setDot` 改為「除了切 dot class 外，crane dot 同步更新 badge 文字 + 顏色 class」
- `web_backend/public/style.css`：新增 `.link-badge` / `.link-ok` / `.link-down` 樣式（已連線綠 / 失聯紅 + pulse 動畫）

### 行為
- web_backend → crane TCP 通訊狀態變化時，header 的小綠點與 crane panel 的 badge 同步切換
- 連線：`🟢 已連線`（綠底）
- 失聯：`🔴 失聯`（紅底脈動）
- WS 斷線：badge 維持上次狀態，直到重連刷新

### 注意
這個 badge 反映的是 **web_backend ↔ crane TCP 連線**（同 #dot-crane），不是 washrobot ↔ crane 的健康度。如果之後要區分（例如 watchdog timeout 但 web 仍可連），要再加一個 badge。

### 部署
只動前端三檔，scp + Ctrl+Shift+R reload 生效。

## 2026-04-28n — Claude Code — 新增 `crane_attached` toggle（GUI 切換 washrobot 是否驅動 crane）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：宣告 `cmd_crane_attached(bool on)` + member `std::atomic<bool> crane_attached_`（預設 true）
- `user_lib/WASH_ROBOT.cpp`：
  - constructor 加 `crane_attached_(true)`
  - `crane_cmd_` 進入時若 `!crane_attached_` 直接回 `OK skipped`，不送 TCP、不抓 mutex
  - `crane_watchdog_loop_` 進每輪先檢查 `crane_attached_`，false 就 continue（不 ping、不檢 timeout、不 abort）
  - 新 `cmd_crane_attached(on)`：切換 atomic、ON 時 reset `crane_last_ok_ms_` 給 grace period、廣播 EVT
  - `cmd_status` 回應加 `crane_attached=on/off` 欄位
- `washrobot_new_PI/main.cpp`：dispatch 加 `crane_attached <on|off>`
- `web_backend/public/index.html`：crane panel 上方加一排 `attached ON / OFF` 按鈕 + 狀態文字 + hint 說明
- `web_backend/public/app.js`：parse 任何含 `crane_attached=on/off` 的 line → 同步 `#crane-attached-status` 文字

### 行為
| 狀態 | crane_cmd_（內部呼叫）| watchdog | 用途 |
|---|---|---|---|
| **ON**（預設）| 真送 TCP，timeout 等回應 | ping 跑、timeout 期間 motion → abort | 真實 crane 連線 |
| **OFF** | 直接回 `OK skipped` | 整輪 skip（不 ping、不 abort）| bench 測試 / crane 離線 |

step_down body_pre_cycle / feet_backup / phase5 / return_home 內 crane_cmd_ 全部會自動受影響。GUI 直接 raw command 送 `pay_out 30` 還是會 forwarded 給 crane（不經 WashRobot）。

### 部署
- 後端：rebuild + deploy washrobot 主程式
- 前端：scp index.html / app.js 到 .5.26 + Ctrl+Shift+R reload

### 待驗
- [ ] OFF：點 step_down → log 看到 `[crane_cmd] '...' SKIPPED`、watchdog 沒動、流程繼續
- [ ] ON：cmd_status 顯示 `crane_attached=on`、watchdog 正常 ping crane
- [ ] OFF → ON 切換：crane_last_ok_ms_ 重置，不會立刻 trigger timeout abort
- [ ] GUI 狀態文字隨 cmd_status / EVT 切換顯示

## 2026-04-28m — Claude Code — vacuum_check_ 廣播每顆 sensor worst 讀數到 GUI（即時更新真空 panel）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `vacuum_check_`：每顆 sensor 確定 worst（最弱 sample）後，呼叫 `evt_("vac_sample pN=value")`，廣播到所有 WS client

### 行為
GUI 的 `parseVacuumValues()` 已經 parse 任何含 `pN=value` 的 line（cmd_status / EVT 都吃），現在 vacuum_check_ 期間也會 emit。
- step_down body / feet / cmd_attach 觸發 vacuum_check_("body"/"feet"/"all") 時，cell 5/6/7/8（body）或 1/2/3/4（feet）或全 9 顆即時 colored update
- 通訊全失敗那顆 sensor 不 emit（panel cell 保留前次數字）

### 沒動的
- cmd_status 一次性回 9 顆值的格式不變
- 前端不用改（parser 已通用）

### 待驗
- [ ] 跑 step_down body 階段 → 看 GUI cell 5-8 在 vacuum_check_ 時更新顏色 + 數字
- [ ] cmd_attach 之後 vacuum_check_("all") → 9 顆都 update
- [ ] 一顆 sensor 全 comm fail → 那顆 cell 不變、其他更新

## 2026-04-28l — Claude Code — 撤掉 3 處 TEST MODE 安全機制 skip（attach 真空驗證 / IMU emergency / crane watchdog）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - **`cmd_attach`** 末段：解註解 `vacuum_check_("all")` — attach 完之後驗證 9 顆 cup 真的吸住，任一未吸 → 回 `ERR attach_vacuum_fail slaves=...`，state 不轉到 Attached
  - **`imu_monitor_loop_`** EMERGENCY 路徑：解註解 4 行 abort 邏輯（`abort_flag = true`、`motion_active_ = false`、`crane_cmd_("emergency_stop")`、`set_state_(State::Error)`）— 連續 SUSTAIN_MS 內傾斜 ≥ `IMU_EMERGENCY_DEG (45°)` → 真的進緊急停止
  - **`crane_watchdog_loop_`**：解註解 `abort_flag = true` — 動作中 crane 失聯 (`elapsed > WATCHDOG_TIMEOUT_MS = 60s`) → 設 abort_flag

### 為什麼解
- attach vacuum：threshold 已對齊 kPa 單位 + multi-sample + comm-retry 補強，誤觸機率低
- IMU emergency：機體已上機，傾斜 45° 不停就是真有問題
- crane watchdog：要連 crane 才有意義，連線後 60s 沒回應 = 真的斷了

### 影響
- attach 失敗會直接回 ERR、state 維持 Ready（之前永遠回 OK）
- IMU 偵測 45° 連續 SUSTAIN_MS → state = Error，整個流程立刻停
- crane 60s 沒回 OK + 動作進行中 → abort_flag → 下個 check_abort_ 點停下

### 仍保留 TEST MODE 的設定
- `WATCHDOG_TIMEOUT_MS = 60000`（仍 60s，主 crane 連上要回 2000）
- `CRANE_IP = "192.168.5.26"`（shim 在那台，主 crane 上自己 Pi 後改 .1.101）
- driver `debug=true`（`WR_DRIVER_DEBUG=0` 可暫時關）

### 待驗
- [ ] attach 真的吸住 → state 轉 Attached
- [ ] attach 故意一顆吸不上 → 回 `ERR attach_vacuum_fail slaves=N`，state 留在 Ready
- [ ] IMU 把機體傾斜 45° 連續 N 秒 → state 進 Error、log 印 `imu_emergency balance_deg=...`
- [ ] step_down 中斷 crane 連線 → 60 秒後 abort，下個 check_abort_ 觸發中止

## 2026-04-28k — Claude Code — step_down 重啟清洗自動化（撤掉 TEST MODE skip）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_` 尾段：
  - 撤掉 `[TEST MODE 2026-04-24]` 的 arm_sweep skip 註解
  - 重新啟用 `do_arm_sweep_()` 呼叫
  - sweep 失敗 → 印 log + 直接 propagate 失敗（state 進 Error）

### 行為
每跑完一個 step_down（body 階段 + feet 階段都成功）後，自動執行 arm_sweep：
1. 開水閥 + 水泵 + 刷子
2. arm 從 0 → +30 cm（右）
3. arm → -30 cm（左）
4. arm → 0 cm（回中）
5. 關水/刷子

arm_sweep 內 3 段 DM2J 動作各自有 `try_or_pause_` 包過（04-28j 改的）→ arm 卡住會進 PausedOnError 讓使用者按繼續/略過，不是直接整個 step_down 中斷。

### 連帶
- `cmd_run` 跑 N 次也會每次 step 後跑 sweep
- 整個 step 時間延長：原本 body+feet ~30~60 秒 → 加上 sweep（3 段 × 30 cm @ 200 rpm ≈ 27 秒 + 水/刷子 ramp）約 60~90 秒

### 待驗
- [ ] 跑單次 step_down → body OK → feet OK → 看到 `[step_down] start wash sweep`
- [ ] arm 三段移動正確（右-左-中）+ 水閥 / 水泵 / 刷子有開
- [ ] sweep 結束 → 水/刷子全關 → return OK step_done
- [ ] 故意讓 arm 卡（手擋住）→ try_or_pause_ 進 PausedOnError，可繼續/略過

## 2026-04-28j — Claude Code — PauseOnError 擴到 cmd_init / cmd_attach / do_arm_sweep_

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `cmd_init_impl_`：所有原本 `if (op) return "ERR ...";` 的硬體呼叫包進 `try_or_pause_`
    - 7 個 PQW relay（pump on / 6 個 valves+water off）
    - 2 顆 DM2J wheel retract（PR_move_cm）
    - DM2J 腳組 rail home（dm2j_pair_move_abs_）
    - ZDT enable loop 的 `motion_control_driver_EN(true)`
  - `cmd_attach`：3 個 valve ON 包進 try_or_pause_
  - `do_arm_sweep_`：3 個 DM2J ARM PR_move_cm 包進 try_or_pause_（cleanup 的 brush/water_pump/water_inlet OFF 維持原樣不包，因為 cleanup 必須無條件跑）

### 不包的地方（風險評估）
- `do_phase5_roll_correct_` — 機體只靠中心吸盤撐，pause 時間不可控 → 風險高，不包
- `cmd_return_home` — 緊急下降流程，pause 機體留懸空 → 風險高，不包

### 行為對照
| 流程 | 改前（fail 行為）| 改後 |
|---|---|---|
| init 中 PQW 偶發失敗 | 直接 return ERR，state 不變、再跑 init 又撞 | PausedOnError → 使用者 GUI 按繼續 → 重試 |
| attach valve ON 失敗 | 直接 ERR，state 退回 Ready | PausedOnError → 重試 / 略過 |
| arm_sweep DM2J 失敗 | ERR，但 brush/water 有清乾淨 | PausedOnError → 重試 / 略過（cleanup 仍跑）|

### 待驗
- [ ] 拔網線製造 init PQW 失敗 → state 進 paused_on_error，GUI 紅底脈動顯示 context
- [ ] 接回網線、按「繼續」→ 重試該 op、流程接著跑
- [ ] 按「略過此步」→ 假裝該 op OK、流程繼續（init 完成）
- [ ] arm 卡住 stall → PausedOnError；按「繼續」清 stall 後重做

## 2026-04-28i — Claude Code — vacuum_check_ comm-error 顯式處理 + 跨 slave delay

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `vacuum_check_`：
  - 每個 sample 內部加 `COMM_RETRY_MAX=3` 次 retry，間隔 50 ms
  - 每次 read_pressure() 後檢查 `M_(s).error_flag`，error 視為該 sample 失敗（不採用回傳的 cached 值）
  - 全 3 sample 都 comm fail → 印 log + 視為 detached（fail）
  - 部分 sample comm fail → log 提示，採用其餘 good samples 的最弱
  - 換 slave 之間加 50 ms delay 降低 gateway buffer 殘留風險

### 原因
之前 `read_pressure()` 在 Modbus 失敗時 silent return cached `_last_pressure`，呼叫端拿到的是「舊讀數冒充新讀數」。使用者觀察到 vacuum_check_ 一直讀到 -23（其實是某次成功讀取留下的 cache），Linux_test 同顆 sensor 卻讀 -68。

新版透過 `error_flag` 判斷該 sample 是否 fresh，stale 就 retry，retry 仍失敗就丟棄。即使 driver 行為沒改，呼叫端也能不被 cache 騙。

### 風險 / 副作用
- 4 顆 cup × 3 sample × 最壞情況 3 retry × 50 ms = 1.8 秒（但只在 comm 真的有問題時才達到）
- 正常情況一次過：4 × 3 × 50 ms 採樣 + 3 × 50 ms slave gap ≈ 750 ms（比舊版多 150 ms）

### 待驗
- [ ] 製造 gateway 阻塞（cmd_status + step_down 同時跑）→ vacuum_check_ 不會被 -23 cache 騙
- [ ] 真的 comm 全失敗時看到「ALL 3 samples comm-failed — treat as detached」log + 該 slave 列為 fail
- [ ] 正常情況沒有 comm fail log，速度跟之前差不多

## 2026-04-28h — Claude Code — GUI vacuum panel 新增 pump (CH1) 開關 + 後端 `pump <on|off>` 指令

### 修改檔案
- `user_lib/WASH_ROBOT.h`：宣告 `cmd_pump(bool on)`（在 `cmd_vacuum` 旁邊）
- `user_lib/WASH_ROBOT.cpp`：實作 `cmd_pump` — `pqw_.controlRelay(CH_PUMP, on)`，Error 狀態拒絕
- `washrobot_new_PI/main.cpp`：dispatch 加 `pump <on|off>` + header 註解同步
- `web_backend/public/index.html`：在 `manual — vacuum` panel **最上面**加一排「pump (CH1) ON / OFF」按鈕

### 為什麼

使用者要求在網頁 vacuum panel 新增馬達開關（即 dp0105 vacuum pump，CH1）。原本 pump 只能由 `cmd_init_impl_` 自動開、`cmd_shutdown` 自動關，沒有手動 toggle 的入口，不利 bench debug。

### 注意

- `cmd_init_impl_` 一進來會把 pump 自動打開，所以正常流程不需手動操作
- 跑流程中（init / attach / step_down / run）手動關 pump 會讓 9 顆吸盤瞬間漏氣 → 真空 fail，不要這樣做
- 沒加 hint 提醒（按鈕語意明確、跟其他 valve 排在同 panel）

## 2026-04-28g — Claude Code — 把所有 `pusher_move_*` 呼叫點都包進 `try_or_pause_`（一致性）

### 為什麼

之前 PausedOnError + continue/skip 按鈕只在 `do_step_down_` / `cycle_group_` 路徑生效。其他指令（init / attach / 手動 pusher / return_home）裡的 pusher 動作**沒包**，stall 時只回 `ERR ...\n`，state 不變，按鈕不會 enable，使用者卡住只能用 raw command 重試或 reset。本次補齊一致性。

### 改動

`user_lib/WASH_ROBOT.cpp` 新增 9 個 try_or_pause_ wrap 點（context 對應）：

| 函式 | 動作 | context |
|---|---|---|
| `cmd_init_impl_` | feet pusher extend | `init_feet_pusher_extend` |
| `cmd_init_impl_` | body pusher extend | `init_body_pusher_extend` |
| `cmd_attach` | center pusher extend | `attach_center_pusher_extend` |
| `cmd_pusher` (retract) | group retract | `manual_pusher_<group>_retract` |
| `cmd_pusher` (all extend) | feet | `manual_pusher_all_feet_extend` |
| `cmd_pusher` (all extend) | body | `manual_pusher_all_body_extend` |
| `cmd_pusher` (all extend) | center | `manual_pusher_all_center_extend` |
| `cmd_pusher` (group extend) | feet/body/center | `manual_pusher_<group>_extend` |
| `cmd_return_home` | all 9 retract | `return_home_pusher_retract` |

加上原有 4 個 `do_step_down_` + 2 個 `cycle_group_` template 內部的 pusher wrap，**共 13 處 pusher 呼叫全部走 try_or_pause_**。

### 正確性檢查

1. **狀態保留**：`await_user_intervention_` 會把進入時的 state 存到 `state_before_pause_`，user 按 continue 後恢復。例如 init 從 Idle 開始 → stall → PausedOnError → continue → 回 Idle → 重試 → 成功 → cmd_init_impl_ 結尾 set_state_ Ready
2. **Skip 語意**：user 按 skip 表示「我手動修好了，當作這步成功」，try_or_pause_ 回 false，後續流程繼續（譬如 init 跳過 pusher extend 直接做 IMU baseline）
3. **Abort 路徑**：try_or_pause_ 回 true 只發生在 emergency_stop，那時 cmd_emergency_stop 已經把 state 設成 Error，所以 cmd 直接 `return "ERR aborted\n"` 即可（state 已是 Error）。`cmd_return_home` 用 `fail()` lambda 多套一層 `set_state_(Error)`，是冗余但無害
4. **多次點擊**：第二次點擊同 cmd_pusher 進到 worker thread，會在 `motion_mtx_` 上排隊等第一次解套；continue/skip 是 FAST 不卡 mutex
5. **lambda capture**：所有 `&slaves` / `&feet_group` 等 by-ref 都在同 scope（try_or_pause_ 同步呼叫），無 dangling 風險

### 待驗

- [ ] 在 init 時故意讓某顆 ZDT stall → 觀察「⚠ ERROR 暫停中」label 顯示 + context 顯示 `init_feet_pusher_extend` 之類
- [ ] 同上但 stall 在 attach 階段 → context 應為 `attach_center_pusher_extend`
- [ ] 手動 pusher feet extend → ZDT stall → context 為 `manual_pusher_feet_extend`
- [ ] return_home 階段 stall → context 為 `return_home_pusher_retract`，按 continue 重試 / skip 略過 / emergency_stop 後狀態為 Error

## 2026-04-28f — Claude Code — 修 PausedOnError 解套死鎖（前端 regex + 後端 fast/slow 分流）

### Fix A：前端 regex 漏抓 `EVT state_changed`
`web_backend/public/app.js:174-182`：原本只用 `\bstate=(\S+)` 抓 state，但 `EVT state_changed running paused_on_error` 不含 `state=` 字面 → `washrobotState` 永遠停在 `'unknown'` → continue/skip 按鈕永遠 disabled。

加 fallback regex `EVT\s+state_changed\s+\S+\s+(\S+)` 抓 state_changed 的第二個 token（new state）。

### Fix B：dispatch 在 receive thread 內同步阻塞 → 死鎖
`washrobot_new_PI/main.cpp on_receive`：原本所有指令 inline 跑在 receive thread，long-running 指令（step_down / run）卡進 `await_user_intervention_` 後，**同條 TCP 連線後續送來的 continue / skip / emergency_stop / ping / status 全在 buffer 排隊**，因為 receive thread 沒空回去處理 → 整個 GUI 死鎖。

改成 fast/slow 兩條路：
- **FAST**（synchronous on receive thread）：`ping` / `status` / `pause` / `resume` / `continue` / `skip` / `emergency_stop` / `reset`
- **SLOW**（detached thread per call）：其他全部

SLOW 指令在 worker thread 跑 `dispatch()` + `sendToClient()`，receive thread 立刻回去 polling 下一筆 → 卡 PausedOnError 時可以**從同一條連線送 continue/skip 解套**。

SLOW 指令彼此會在 `motion_mtx_` 自然 serialize（先 push 先處理），不會 race。`sendToClient` 是 raw `send(2)`，POSIX 保證對同 fd thread-safe，<200 byte 的 ASCII 訊息也不會 interleave。

### 修改檔案
- `web_backend/public/app.js` — regex 加 state_changed 的 fallback
- `washrobot_new_PI/main.cpp` — `#include <thread>` + `#include <unordered_set>`、新 `FAST_CMDS` 集合、`on_receive` 拆 fast/slow 兩條路

### 部署
- `app.js` → scp 到 .5.26 backend `public/`，瀏覽器 Ctrl+Shift+R
- `main.cpp` → 重新編譯 + 部署 washrobot 主程式

### 待驗
- [ ] 模擬 PausedOnError（step_down 故意讓 ZDT stall）→ 按 status 應立刻回 OK，狀態框顯示「⚠ ERROR 暫停中」+ context、continue/skip 按鈕變可按
- [ ] 直接按 continue/skip 不需先按 status → state_changed event 抓到 paused_on_error 即啟動
- [ ] PausedOnError 期間多次按 ping 都能回 OK pong（receive thread 沒被卡）
- [ ] PausedOnError 期間按 pusher / vacuum 等 SLOW 指令會在 worker thread 等 motion_mtx_，不影響 fast 指令處理

### 後續可考慮
- 若 SLOW 指令的回覆順序需要保證一致，加一個 worker queue + 單一 dispatcher thread；目前每個 SLOW 指令一條 detached thread，靠 motion_mtx_ 自然排序，dev/test 夠用

## 2026-04-28e — Claude Code — feet_pre_cycle valve OFF → ZDT retract 等待從 300 ms → 1000 ms

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `feet_pre_cycle` lambda：valve OFF 後 sleep 從 300 ms 改 1000 ms，對齊 body_pre_cycle 與 cycle_group_ template 的 1000 ms 標準

### 原因
使用者要求：解真空後必須等 1 秒才能縮回 ZDT 推桿。掃過所有 valve OFF + ZDT retract 的點：
- `cycle_group_` template ✓ 1000 ms
- `body_pre_cycle` ✓ 1000 ms
- **`feet_pre_cycle` ✗ 只有 300 ms** ← 本次修正
- 其他 valve OFF 點後面沒接 ZDT retract（init 後接 extend；phase5 / tilt_mode / shutdown 都沒 retract；return_home 有 5000 ms `RETURN_VACUUM_RELEASE_MS`）

### 物理意義
真空放掉到吸盤實際脫離牆面有滯後（VT307 電磁閥切換 ~50 ms + 真空管路洩壓 + 吸盤接觸點仍黏附）。短於 1 秒收推桿，cup 還在吸狀態下被機械強行拔下，會卡 ZDT (stall) + 拉壞 cup。

## 2026-04-28d — Claude Code — GUI 新增 vacuum readings panel（9 顆 JC-100 顯示 + 顏色標示）

### 修改檔案
- `web_backend/public/index.html`：在 `manual — vacuum`（valve 控制）和 `manual — pusher` 之間插入新 panel `manual — vacuum readings`，9 顆吸盤分三組（feet 1-4 / body 5-8 / center 9）+ refresh 按鈕
- `web_backend/public/app.js`：
  - `parseVacuumValues(line)` — 從任何含 `pN=value` 的 line 抓出讀數，更新 `#vac-N` 內容 + 顏色 class
  - `onWashrobotLine` 結尾呼叫 `parseVacuumValues(line)` 自動同步
  - `btn-refresh-vacuum` onclick → 送 `status` 觸發後端回 9 顆讀數
- `web_backend/public/style.css`：新增 `.vac-cell` 基本樣式 + 三種狀態 class（`.vac-strong` 綠、`.vac-weak` 黃、`.vac-none` 紅）

### 行為
- 點 refresh → 送 status → 後端回 `OK state=... rail=... p1=N p2=N ... p9=N ...` → 各 cell 自動更新
- step_down / 任何流程的 status 回應也會被 parseVacuumValues 抓到 → 同時更新顯示
- 顏色判定：
  - `≤ -50 kPa` → 綠 `.vac-strong`（attached）
  - `-50 < p ≤ -10` → 黃 `.vac-weak`（partial seal）
  - `> -10 kPa` → 紅 `.vac-none`（detached / no contact）

### 部署
只動前端三檔，scp 到 .5.26 web_backend/public/，瀏覽器 Ctrl+Shift+R reload 生效。

### 待驗
- [ ] 開頁面後所有 cell 顯示 `pN = ?` 灰色（沒讀過）
- [ ] 按 refresh → 9 個 cell 跳出實際數值 + 對應顏色
- [ ] 跑 step_down 過程中 cell 顏色隨吸盤狀態變化（valve OFF 後變紅 / 重吸後變綠）

## 2026-04-28c — Claude Code — GUI 繼續/略過按鈕加視覺狀態指示

### 修改檔案
- `web_backend/public/index.html`：error pause row 重構為 status 框 + 兩顆 disabled-by-default 按鈕（id `btn-continue` / `btn-skip` / `error-pause-status` / `error-pause-label` / `error-pause-context`）
- `web_backend/public/app.js`：
  - 新 module-level state `washrobotState` / `lastPauseContext`
  - `onWashrobotLine` 新增 regex 抓 `state=X`（適用 EVT state_changed + cmd_status reply）
  - 新增監聽 `EVT error_pause context=...` 抓暫停 context
  - 新 helper `updateErrorPauseUI()`：依 state 切按鈕 disabled、status 框 active class、文字標示
- `web_backend/public/style.css`：新增 `.error-pause-row` / `.error-pause-status` / `.error-action:disabled` 規則 + `pulse-err` 動畫

### 行為對照
| state | 按鈕 | status 框 |
|---|---|---|
| running / attached / etc. | disabled、半透明 | 灰底虛線「state=X (非 paused_on_error，按鈕 disabled)」|
| **paused_on_error** | **enabled、可按** | 紅底脈動「⚠ ERROR 暫停中: <context>」|

### 原因
之前兩顆按鈕永遠可見可按，使用者搞不清楚什麼時候有用。Backend `cmd_continue` / `cmd_skip` 都會檢查 state 是否為 PausedOnError，非的話直接回 ERR。視覺 disable 把這個語意提前到 GUI 層，避免誤按 + 看不懂為什麼 ERR。

### 部署
只動前端三檔，scp 到 .5.26 web_backend/public/，瀏覽器 Ctrl+Shift+R reload 就生效。

### 待驗
- [ ] 啟動時 status 框顯示 `state=unknown (...)`，按一次 status 後變正確
- [ ] step_down 故意製造 fail 進 PausedOnError → status 框紅底脈動 + 按鈕亮起
- [ ] 按「繼續」→ 重試動作 → 過了的話 status 框回灰、按鈕 disabled
- [ ] 按「略過此步」→ state 回 Running → 按鈕 disabled

## 2026-04-28b — Claude Code — GUI + 後端新增 `zdt_zero <group>`（ZDT 當前位置歸零）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新增 `cmd_zdt_zero(const std::string& group)` 宣告（在 `cmd_pusher` 旁）
- `user_lib/WASH_ROBOT.cpp`：實作 `cmd_zdt_zero` — 呼叫 `group_slaves_(group)` 拿 slave list（"feet"/"body"/"center"/"all"），對每顆 `Z_(s).set_zero()`（ZDT 手冊 3.1.3，Reg `0x000A` ← `0x0001`），任一失敗回 `ERR zdt_zero_fail slave=N`。Error 狀態拒絕，其他允許
- `washrobot_new_PI/main.cpp`：dispatch 新增 `zdt_zero <group>` 分支 + header 註解同步
- `web_backend/public/index.html`：在 `manual — pusher` panel 底下加一個 row，4 顆按鈕（feet / body / center / all），下方 hint 提醒「正確時機：pushers 完全縮回到底時按」

### 為什麼

使用者要求：依 ZDT 手冊在網頁加當前位置歸零功能。`Z_(s).set_zero()` 已存在但沒透出到 GUI。

### Caveat（GUI hint 也寫了）

- ✅ 推桿在物理底（完全縮回）時 zero → 之後 abs 0 = 真實底
- ❌ 半伸/全伸時 zero → 之後 retract → 0 不會回到真實底，可能 over-extend 撞死或漏縮

### 待驗
- [ ] 點 zero feet → DM2J/ZDT log 看到 4 顆 (slave 1,2,3,4) set_zero 成功
- [ ] zero 後送 `pusher feet extend` → 看實際移動的相對位置是否如預期
- [ ] zero all 9 顆同時下指令是否會 timing 衝突（`group_slaves_("all")` 是循序，不該有問題）

## 2026-04-28a — Claude Code — ZDT stall_flag 升級為 fail（保留 release_stall_flag）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `zdt_wait_motion_done_`：stall_flag 路徑從「印 log + clear flag + return false (假裝成功)」改為「印 log + clear flag + return true (fail)」
- function header comment 同步更新

### 原因
之前堵轉被當成「正常完成」往上回，呼叫端不知道有問題 → 流程繼續 → 後面 vacuum 一定吸不上 → 5 次 retry exhausted (~50 秒) 才認失敗，等使用者已經沒辦法即時介入。

實例：使用者 04-27 測試時某一支 ZDT 原點偏移、伸太長撞牆 stall 在 22°（target 是 2000°），但程式視為完工繼續往下，後面才在 vacuum 階段崩。

### 新行為
1. ZDT drive 偵測到 stall → set stall_flag
2. `zdt_wait_motion_done_` 看到 stall_flag → 清 flag（讓下次 motion command 能下）→ return true
3. caller `pusher_move_many_` 拿到 fail → 透過 `try_or_pause_` 進 PausedOnError
4. EVT 廣播 `error_pause context=cycle_<group>_pusher_extend / pusher_retract`
5. 使用者現場處理（重設原點、清障）→ 按 GUI「繼續」→ 重做這個 pusher group → 再 stall 又 pause
6. 不行就按「略過此步」或 emergency_stop

### release_stall_flag 維持
- 必須清才能讓下次重試能成功下指令；ZDT firmware 對 latched stall 後續 pos_mode write 會拒絕

### 風險
- 若 ZDT 在「正常推到底」自然 stall（例如貼到牆面瞬間 drive 過電流保護），會誤判 fail
- 目前觀察 ZDT 正常完成都走「速度穩定 / 位置不變」收尾、不走 stall 路徑，誤觸機率低
- 如果現場後續發現誤判，再升級成「stall + 偏離 target 才 fail」（C 方案）

### 待驗
- [ ] 故意製造 stall（推桿頭頂東西）→ log 看到 `STALL ... — release flag + fail` → state 變 paused_on_error
- [ ] 按「繼續」重試，這次正常推完 → 流程繼續
- [ ] 按「略過此步」→ 假裝成功、流程繼續

## 2026-04-27w — Claude Code — dm2j_pair_move_abs_ 加 skip-if-at-target 優化

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `dm2j_pair_move_abs_`：讀完 before 位置後比對 target，兩顆都在 ±0.5 mm 內視為已到位，直接 return false（成功）

### 原因
現場 log 看到 feet 階段呼叫 `pair DM2J 1+3` 從 (1=0, 3=0) 移到 target=0：
```
[pair DM2J 1+3] before: 1=0 3=0 cm → target 0 cm
[pair DM2J 1+3] after:  1=0 (Δ0) 3=0 (Δ0) cm
```
這個 no-op 還是跑完整套：read positions × 2 → PR_move_cm_set × 2 → PR_trigger_sync 廣播 → dm2j_pair_poll_done_ poll → read positions × 2，浪費 ~2 秒。

加 EPSILON_CM = 0.05 (0.5 mm) 容忍度，兩顆都已在 target 就 skip。

### 影響
- step_down feet 階段如果 rail 已經在 0 → skip rail 動作
- step_down body 階段如果 step_cm 比目前 rail 位置近（不太常見）→ skip
- backup retry 動作如果 target 跟現位置一樣 → skip
- 都不影響 vacuum / pusher / valve 流程，只跳過 rail 動作本身

### 你提到的「身體走 0 公分時腳不需要移動」更深的可能性
如果你希望「**body 階段沒實際動到 rail（包括 skip 也算）→ 整個 feet phase 都跳過**（連 valve/pusher 都不做）」，這個現在還沒做。本次只跳 rail 動作。要做進階的 skip 我再加。

### 待驗
- [ ] feet phase rail=0 / target=0 時 log 看到 `already at target 0 cm — skip`，總時間少 ~2 秒
- [ ] body phase 正常情境（rail 0 → +30）仍跑完整 motion，沒被誤跳

## 2026-04-27v — Claude Code — vacuum_check_ 改 multi-sample 取最弱（過濾真空 ripple / glitch）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `vacuum_check_`：每顆 sensor 連讀 3 次（間隔 50 ms）取最弱（最不負）那次跟 threshold 比

### 原因
現場觀察 attached 時 reading 多半 -68，但偶爾跳到 -35。可能來源：
1. dp0105 真空泵 PWM 運轉造成 cup 內壓力 ripple
2. JC-100 driver 在 Modbus glitch 時 return cached `_last_pressure`（可能是稍早 transient 值）
3. cmd_status / cycle_group_ 連讀多顆 sensor，gateway buffer 對齊偶發異常

單一 sample 抖到 -35 就 > -50 threshold → vacuum_check_ 誤判 fail → step_down 進 retry → 連續抖兩三次就 vacuum_retry_exceeded。Multi-sample 取最弱，3 次都 > -50 才認失敗，過濾掉 ripple 與 single-shot glitch。

### 行為
- 每顆 sensor 取 3 個 sample、間隔 50 ms（一顆共 ~150 ms）
- 取「最不負」那個 reading 跟 `VACUUM_THRESHOLD_KPA = -50` 比
- 例如三次讀 [-68, -35, -70] → worst = -35 → -35 > -50 = fail
- 例如三次讀 [-68, -35, -68]（中間一次 ripple）→ worst = -35 → 仍 fail 嗎？

> 注意：取最弱仍會被 single-shot glitch 觸發。如果你想更寬鬆改用「median」或「2/3 majority OK」更好。但保守起見先用「最弱」測試現場，若仍偶發誤判再升級。

### 時間成本
- body / feet（4 顆）：~600 ms
- center（1 顆）：~150 ms
- "all"（9 顆）：~1.35 s

可接受（vacuum_check_ 在 settle 2 秒之後跑，多 0.6 秒不影響整體節奏）。

### 待驗
- [ ] step_down 不再因為單次 reading 抖到 -35 而誤判 vacuum_retry
- [ ] cmd_status 顯示的 p 值穩定度可從 multi-sample 結果評估

## 2026-04-27u — Claude Code — 真空 threshold 單位對齊現場 JC-100（0.1 kPa → kPa）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `VACUUM_THRESHOLD_X10 = -500`（0.1 kPa）→ `VACUUM_THRESHOLD_KPA = -50`（kPa）
  - `DETACH_THRESHOLD_X10 = -100`（0.1 kPa）→ `DETACH_THRESHOLD_KPA = -10`（kPa）
- `user_lib/WASH_ROBOT.cpp`：4 處引用全部 rename（vacuum_check_、phase5 中心吸盤檢查、return_home 脫離檢查）

### 原因
現場 JC-100 attach 後 cmd_status 顯示 p1~p8 ≈ -68~-70（強吸力 ≈ -68 kPa），p9 ≈ -1（無吸）。對照原本 driver 假設「raw value × 0.1 = kPa」，那這些 readings 應該是 -680~-700（在 0.1 kPa 單位下對應 -68 kPa）才對。實際讀回來是 -68 表示**硬體已經是 kPa 單位**（可能 JC-100 set_pressure_unit 設成 kPa 或本來就是）。

舊 `VACUUM_THRESHOLD_X10 = -500` 在「0.1 kPa 單位下 -50 kPa」是對的，但 reading 是 kPa 單位 → -68 永遠 > -500 → vacuum_check_ 永遠回 fail，就算實際吸得很死（-68 kPa）也算失敗 → step_down 一直 retry → vacuum_retry_exceeded。

新 `-50 kPa` 對齊後，-68 < -50 → 視為 attached ✓。

### 待驗
- [ ] 重新跑 step_down，body / feet 階段不再因為門檻錯誤而 retry exhausted
- [ ] cmd_status 顯示的 p 值符合預期：attached cup -50 ~ -80 kPa、未 attached -1 ~ -5 kPa
- [ ] phase5 平衡校正（cmd_confirm_balance yes）的中心吸盤檢查也用對齊後的門檻
- [ ] return_home 的脫離檢查（p > -10 kPa = 脫離）也對齊

## 2026-04-27t — Claude Code — [跨界: user_lib] PauseOnError 機制（auto 流程 fail → 暫停 → 使用者重試/略過）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - State enum 加 `PausedOnError`
  - 新 enum `PauseAction { None, Retry, Skip, Abort }`
  - 新 member `std::atomic<int> pause_action_`
  - 新指令 `cmd_continue()` / `cmd_skip()`
  - 新私有 helper `await_user_intervention_(ctx)` — block 直到 cmd_continue / cmd_skip / emergency_stop
  - 新 inline template `try_or_pause_(fn, ctx)` — 包裝任何 `bool fn() returning true=error` 的呼叫
  - `cycle_group_` template 內所有 `controlRelay` / `pusher_move_many_` 都改用 `try_or_pause_` 包
- `user_lib/WASH_ROBOT.cpp`：
  - constructor 初始化 `pause_action_(None)`
  - `state_name` 加 `paused_on_error`
  - 實作 `await_user_intervention_` / `cmd_continue` / `cmd_skip`
  - `do_step_down_` 內 4 個 lambda（body_pre_cycle、body_backup、feet_pre_cycle、feet_backup）所有 hardware op 都改用 `try_or_pause_`：PQW controlRelay / pusher_move_/_many_ / crane_cmd_ / dm2j_pair_move_abs_
- `washrobot_new_PI/main.cpp`：dispatch 加 `continue` / `skip`，header 註解同步
- `web_backend/public/index.html`：auto cycle panel 多一 row 加兩顆按鈕「繼續(重試)」/「略過此步」（用 data-tgt/data-cmd 走既有 dispatch）

### 行為
1. step_down / run 自動流程，任一 hardware op 失敗 → 進 `PausedOnError` 狀態（之前是直接進 Error）
2. EVT 廣播 `error_pause context=<具體哪一步>`，console 印 `[PAUSE-ON-ERROR] ...`
3. 使用者三選項：
   - **`continue`（繼續）** → 重試剛才那步
   - **`skip`（略過）** → 假設使用者已手動處理完該步、繼續往下
   - **`emergency_stop`** → 設 abort_flag → await 跳出回 Abort → 流程 propagate `aborted` → 進 Error
4. 重試的 op 還是失敗會再 pause 一次，無限循環直到 skip / abort

### 已知限制
- PausedOnError 期間 `do_step_down_` 仍持有 `motion_mtx_`，所以 GUI 的 manual 指令（vacuum / pusher / move / dm2j_group）會 block 在 mutex 上等。**使用者預期是物理現場手動調整**（撥 valve、推/縮推桿等）；要從 GUI 操作得先 emergency_stop 跳出 PausedOnError
- 未涵蓋的 fail 點：`cmd_arm_sweep`、`cmd_init_impl_` 內部、`cmd_attach`、`cmd_return_home` 還沒 wrap（這些不是 step_down/run 流程的常見痛點）

### 待驗
- [ ] 故意讓某 op 失敗（拔網線等）→ state 變 paused_on_error、EVT 收到 error_pause context=...
- [ ] 修復後按 GUI「繼續(重試)」→ 同一 op 重做、流程繼續
- [ ] 按「略過此步」→ 假裝該 op 成功、流程繼續
- [ ] 按 emergency_stop → 流程結束、state 進 Error
- [ ] 多次重試（一直失敗）能正常重複 pause/retry

## 2026-04-27s — Claude Code — [跨界: user_lib] PQW controlRelay 拆掉 readback 驗證

### 修改檔案
- `user_lib/PQW_IO_16O_RLY.cpp` `controlRelay()`：
  - 移除「再送 read-back 命令 + 解析狀態 + 比對」三步驟
  - 保留 send relay command（送失敗仍 return true）+ 讀 echo（純 log，不解析）
  - 永遠 return false on success

### 原因
PQW 韌體 echo 格式非標準（work_log 04-23 已記錄："TX `... 05 ...` echoed as RX `... 00 ...`"），導致 `parseReadResponse` 解出錯誤狀態 → `controlRelay` 對 OFF 操作隨機回 true（誤判失敗）。今天現場 step_down body 階段第一個 `controlRelay(CH_VALVE_BODY, false)` 就被誤判 → 進 Error 狀態 → reset 也救不回（init/attach 重跑會撞同樣 bug） → 必須重啟程式。

### 規範邊界
PQW driver 屬 user_lib 範圍，本 PR 標 `[跨界: user_lib]`。實質是把已知壞掉的驗證機制拆掉、靠物理 LED + JC-100 + 真空驗證（cycle_group_ 內既有的）作為真實狀態確認。

### 影響
- ✅ 所有 `pqw_.controlRelay(...)` 呼叫不再因 readback bug 誤判
- ⚠️ 失去「真實電氣狀態驗證」能力 — 但本來這個驗證就是壞的
- ⚠️ 真實 PQW 沒收到 / 沒響應的狀況偵測不到 → 改靠後續 vacuum_check_ / 觀察 LED 補強

### 待驗
- [ ] 重新跑 init → attach → step_down，body_valve_off / center_valve_off 不再無故 fail
- [ ] valve 真的有切換（聽 VT307 click + 看 LED + 觀察 JC-100 變化）

## 2026-04-27r — Claude Code — Linux_test menu 18 — XKC-Y25-RS485 sensor 配置工具

### 修改檔案
- `Linux_test/main.cpp`：
  - 新增 `test_xkc_y25()` 函式（menu 18）
  - print_menu 加上「18 XKC water sensor」一行
  - main dispatch 加 `else if (line == "18") test_xkc_y25();`

### 功能
- 連線到既有 RS485 gateway（預設 .22:4001）+ slave ID（預設 13）
- 子選單動作：
  - `r` 連續讀（200ms 一次，Enter 中止）
  - `s` 單次讀（output + RSSI）
  - `i` 改 slave ID（呼叫 `set_address`，需 'yes' 二次確認；改完退出 menu 讓使用者重連）
  - `f` 出廠還原（手動發 broadcast `FF 06 00 04 00 02 5C 14`，需 'reset' 二次確認；sensor 重置為 slave=1 / 9600）
  - `q` 退出

### 不開放的功能
- **改 baud rate** — RS485 bus 上 JC-100 / PQW / DY-500 共用，改 sensor baud 會拆掉整條 bus。需要時請手動操作 gateway 設定後再用 hex frame 改 sensor。

### 待驗
- [ ] 連續讀：把手指或杯水靠近 sensor 探頭 → output 0→1 切換、RSSI 跨越 4100 門檻
- [ ] 單次讀：印 `有水/無水` 中文標示
- [ ] 改 slave ID：執行後實機 LED 閃爍（依手冊 §1.7 描述），退出 menu 後用新 ID 能讀到
- [ ] 出廠還原：執行後 sensor LED 閃 2 下，重新進 menu 用 slave=1 能讀到

## 2026-04-27q — Claude Code — Linux_test menu 13 mode 4 加入 sensor 驅動補水

### 修改檔案
- `Linux_test/main.cpp`：
  - include 新增 `XKC_Y25_RS485.h`
  - menu 13（water tank）prompt 描述更新（mode 2 標註 timed / mode 4 標註 sensor 補水→閒置→刷洗）
  - mode 4 補水階段重寫：
    - 進入時 prompt 一個 `水位 sensor slave ID [13]:`
    - 共用既有 `cli` 初始化 `XKC_Y25_RS485 lvl`
    - 開球閥 → 每 200 ms 讀 `read_state(output, rssi)` → output==1 視為水位滿 → 關球閥、印 `[DONE] 水位達滿 — output=N RSSI=N`
    - 補水秒數 input 改用為「sensor 等待 timeout 上限」，避免 sensor 故障害淹水
    - sensor init 失敗 → 警告 + fallback 回原本 timed 行為（不阻塞使用者）
    - 中途按 Enter 中止支援（沿用 `water_wait_or_abort` 的 stdin 非阻塞 polling 模式）
  - 階段 2（閒置）+ 階段 3（刷洗）邏輯不變

### 行為
- 正常情境：sensor 偵測到水位 → 主動關閥（不會超量）
- sensor 異常：read_state 失敗時印 `read err` 但不放棄；連續無 output=1 直到 timeout 才認失敗
- sensor 完全無響應（init 失敗）：自動退回 timed mode，不會卡死

### 待驗
- [ ] 接好 XKC sensor、注水到貼片高度 → output 切 0→1、補水自動停止
- [ ] sensor slave ID 預設 13（實際接線確定後改 default）
- [ ] timeout 路徑：sensor 一直回 output=0（例如貼錯位置）→ 60s 後自動關閥、不淹水
- [ ] Enter 中止：補水中按 Enter → 立刻關閥 + 印當下 RSSI

## 2026-04-27p — Claude Code — [跨界: user_lib] 新增 XKC_Y25_RS485 水位感測器驅動

### 修改檔案
- 新增 `user_lib/XKC_Y25_RS485.h`
- 新增 `user_lib/XKC_Y25_RS485.cpp`

### 規範邊界備註
依 CLAUDE.md「新增 class（新硬體驅動）→ 架構方負責，協作者提供硬體文件即可」原則上是 Jim 範圍。Sadie 直接要求新增，本 PR 標 `[跨界: user_lib]` 等 Jim review。文件來源：`D:\洗窗戶機器人\電控設備資料\水位sensor\XKC-Y25-RS485 INFO-CN-V16.pdf`。

### 規格摘要
- 非接觸式電容式水位感測器（外貼，可穿透 ≤20 mm 非金屬容器壁）
- Modbus-RTU over RS-485，預設 9600 8N1，slave 1
- 24V DC（可訂 12V），耗電 5 mA，響應時間 500 ms
- 寄存器：
  - `0x0001` OutPut（0=無水 / 1=有水）
  - `0x0002` RSSI 信號強度（< 3900 無水 / > 4100 有水 / 之間保持）
  - `0x0003` slave 地址（1~254）
  - `0x0004` 波特率代碼

### Public API
- `init(ip, port, ID, debug)` / `init(extClient, ID, debug)` — 兩種模式對齊現有 driver
- `read_state(uint16_t& output, uint16_t& rssi)` — 一個 frame 拿到狀態 + 信號（func 0x03 連讀 2 reg）
- `has_liquid()` — bool 包裝
- `last_output()` / `last_rssi()` — 不發 Modbus、回上次成功讀的值
- `set_address(new_addr)` / `set_baud_rate(code)` — 設定（caller 自己負責 re-init）

### 約定遵循
- 回傳值 false=success / true=error（CLAUDE.md coding style）
- log 全部走 `log_utils.h` 的 `LOG_ERR/WRN/INF/DBG/HEX` 巨集
- 預設 `debug_mode=false`（靜默）；錯誤透過 bool return 通知，log 純除錯觀察
- `_log_tag = "XKC:<id>"` per project log format
- class 內部以區塊註解分群（init / read / config / utility）

### 文件不一致說明
原廠手冊範例 frame 與寄存器表對不齊（例如 set address 範例寫到 reg 0x0004 但 table 說 0x0003 才是地址）。實作以 register table 為準。實機若行為不符要回頭驗證。

### 待驗
- [ ] 接線：棕 VCC / 藍 GND / 黃 RS485-B / RS485-A（依 PDF p.10 接腳定義）— **注意 PDF p.14 尺寸圖標的腳位顏色與 p.10 不同（黃 = Out）**，實機接線需以 RS485 版本 PDF p.10 為準
- [ ] read_state 在有水 / 無水時 OutPut 切換
- [ ] RSSI 讀數合理（接近水時應上升超過 4100）
- [ ] 共用 TCP_client 模式整合到 USR-TCP232-304 RS485_3 (.22) 是否可行（看 gateway slave 衝突）

## 2026-04-27o — Claude Code — GUI log panel 寬螢幕固定為右側 sidebar

### 修改檔案
- `web_backend/public/style.css`：尾端新增 `@media (min-width: 1200px)` block

### 行為
- **螢幕 ≥ 1200px**：log panel 變成固定右側 sidebar（width 480px，從 header 下方延伸到視窗底部），控制 panel 區域 `padding-right: 512px` 騰出空間
- **螢幕 < 1200px**：原本排版不變（log 在最下方 full-width，max-height 42vh）

### 原因
使用者要求操控時可以同時看到 log 即時輸出，避免按按鈕後要捲頁去看回應。寬螢幕固定 sidebar 是最直覺的做法，純 CSS 改動，不破壞 HTML，也不影響窄螢幕使用者。

### 效能考量
無新增 GPU 重的 op（沒有 backdrop-filter、沒有動畫），對 Pi Chromium 友好。

### 待驗
- [ ] 寬螢幕（>= 1200px）：log panel 固定在右側永遠可見，控制 panel 在左側自動 reflow
- [ ] 窄螢幕（< 1200px）：排版回到原本，log 在最下方
- [ ] 滾動 main 區時 log 不會跟著滾、log 自己內部 scrollbar 正常運作
- [ ] log 文字夠新時會自動 scroll 到底（既有行為不變）

## 2026-04-27n — Claude Code — retry backup guard 移到 cleanup 之前（dry-run 預檢）

### 修改檔案
- `user_lib/WASH_ROBOT.h` `cycle_group_` template：
  - `Backup` lambda 簽名改為 `(bool dry_run) -> std::string`
  - retry 流程改為 3 步驟：
    1. `backup(true)` 預檢可行性，不可行就直接 return（不動 valve / pusher）
    2. valve OFF + pusher retract（cleanup）
    3. `backup(false)` 實際反向移動
- `user_lib/WASH_ROBOT.cpp`：
  - `body_backup` lambda 簽名 `[this](bool dry_run)`，target < 0 → return error；dry_run=true 且可行 → return ""（無副作用）
  - `feet_backup` 同樣處理，門檻是 `target > step_cm_`

### 原因
原本 guard 寫在 backup() 內部，但 cycle_group_ 在 backup() 之前已經做完 valve_off + pusher_retract。意思是：guard 觸發時系統已經半解除（吸盤鬆掉、推桿縮回），但 rail 沒退到位 → 留下不一致狀態。
應有的順序是：先檢查能不能退 → 不能退就放棄（保持貼牆狀態）→ 能退才動 valve/pusher → 退 rail。

### 行為差異
- 之前：guard 觸發 → 系統處於「valve 關、pusher 縮、rail 在 +step_cm」的半解除狀態 + 回 ERR
- 現在：guard 觸發 → 系統保持「valve 開、pusher 伸、rail 在 +step_cm（仍貼牆）」狀態 + 回 ERR

### 待驗
- [ ] step_cm=10 連續真空失敗到 attempt 3：dry_run 觸發 `body_backup_no_space`，**valve 維持 ON、pusher 維持 extend**（log 不應出現 valve_off / pusher_retract 訊息）
- [ ] 正常 retry 流程：valve_off / pusher_retract / 退 rail 三步都跑

## 2026-04-27m — Claude Code — DM2J group set-zero 功能（mirror Linux_test menu 15）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：宣告 `cmd_dm2j_zero(group)`
- `user_lib/WASH_ROBOT.cpp`：實作 `cmd_dm2j_zero`
  - feet → slaves 1, 3 / wheels → 2, 4 / arm → 5
  - 對 group 內每顆 slave 呼叫 `home_set_current_pos_zero()`（寫 0x6002 = 0x0021）
  - 讀前 / 讀後位置寫到 log，回 reply 包含每顆 slave 歸零後位置
  - feet 歸零時順手 `rail_pos_cm_.store(0.0)` 同步軟體紀錄
  - Error 狀態拒絕，其他狀態都允許
- `washrobot_new_PI/main.cpp`：dispatch `dm2j_zero <feet|wheels|arm>`，header 註解同步
- `web_backend/public/index.html`：`manual — DM2J group sync` panel 同 row 加 `set zero (current = 0)` 按鈕
- `web_backend/public/app.js`：`btn-dm2j-zero` handler — 讀 group selector → `confirm()` 二次確認 → 送 `dm2j_zero <group>`

### 行為
- 共用 group selector：選 feet/wheels/arm → 按 set zero → 瀏覽器跳 confirm → 確定送指令
- 單一動作對 group 所有 slave 一起歸零（feet 1+3、wheels 2+4、arm 5）
- 歸零後讀回位置寫進 log + reply 字串

### 待驗
- [ ] feet 歸零：兩腳當下位置 → 0；rail_pos_cm_ 內部追蹤同步重置
- [ ] wheels 歸零：兩輪當下位置 → 0
- [ ] arm 歸零：手臂當下位置 → 0
- [ ] confirm 對話框取消 → 不送指令

## 2026-04-27l — Claude Code — step_down retry backup 加範圍守衛（避免退出本步行程超撞限位）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_`：
  - `body_backup`：在送 DM2J 動作前先算 `target = rail - 5`，若 `target < 0` 直接 `return "body_backup_no_space"` 不再 retry
  - `feet_backup`：先算 `target = rail + 5`，若 `target > step_cm_` 直接 `return "feet_backup_no_space"` 不再 retry

### 原因
原 backup 沒檢查範圍，連續 retry 會把 rail 推出本步應有區間 [0, step_cm]。例如 step_cm=10 時 body 第 3 次 retry 就會把 rail 推到 -5（低於原點）→ 硬撞機構限位 / drive 計數器 vs 物理位置不一致。Option A：把 retry 限定在「本步起終點之間」[0, step_cm]，超出視為「沒空間了，認失敗」。

### 行為
- step_cm=30：body retry 最多到 0（5 次都還在範圍內）；feet retry 最多到 +30
- step_cm=10：body retry 最多 2 次到 0（3rd 退到 -5 → 攔下）；feet 同樣最多 2 次到 +10
- step_cm=5：body / feet 最多 retry 1 次（5cm backup 1 次就到 0 / +5 邊界，2nd 直接攔下）

### 訊息
失敗時 cycle_group_ 把 backup() 的 error 字串往上回傳，最終看到：
- `ERR body_backup_no_space\n`
- `ERR feet_backup_no_space\n`

### 待驗
- [ ] step_cm=10 故意連續真空失敗 → 第 3 次 retry 應該看到 `target -5 cm < 0 — no more backup space, abort retries` 然後 `ERR body_backup_no_space`
- [ ] step_cm=30 正常情況 → retry 5 次都不會撞 guard

## 2026-04-27k — Claude Code — GUI manual 區改群組同動（mirror Linux_test menu 16）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：宣告 `cmd_dm2j_group(group, cm)`
- `user_lib/WASH_ROBOT.cpp`：實作 `cmd_dm2j_group`
  - `feet` (slave 1, 3) → 走 `dm2j_pair_move_abs_(1, 3, 1, cm)` PR1 廣播同動（剛性耦合用真同步）
  - `wheels` (slave 2, 4) → `PR_move_cm_nowait × 2 + dm2j_wait_done_ × 2`（非剛性，與 `cmd_wheels` 同 pattern）
  - Error 狀態拒絕，其他狀態都允許
- `washrobot_new_PI/main.cpp`：dispatch 新增 `dm2j_group <feet|wheels> <cm>`，header 註解同步
- `web_backend/public/index.html`：`manual — DM2J move (cm, absolute)` panel 換成 `manual — DM2J group sync`，下拉選 feet / wheels / arm
- `web_backend/public/app.js`：`btn-move` handler 改成 `btn-dm2j-group`，feet/wheels 送 `dm2j_group`、arm 沿用 `move arm`

### 行為
- GUI 上選 group + 輸入 cm + 按 move
  - feet → broadcast PR1 真同步（兩腳同 frame 啟動）
  - wheels → 平行 nowait（兩輪一個 Modbus frame 內前後 trigger）
  - arm → 走舊 `cmd_move arm` 單顆動作
- 舊 `cmd_move` 後端**保留**（raw command 還能用 `move left_foot 30` 之類）

### 待驗
- [ ] feet 移到 abs 30 → 兩腳同步啟動 + 完成
- [ ] wheels 移到 abs -7 → 兩輪同時下降
- [ ] arm 移到 abs 0 → 手臂歸位

## 2026-04-27j — Claude Code — step_down 真空驗證 + retry 機制復活（撤 TEST MODE skip）

### 修改檔案
- `user_lib/WASH_ROBOT.h` `cycle_group_` template：
  - 移除 `[TEST MODE 2026-04-24] 真空驗證暫時跳過` 那段 short-circuit (`out_retry_count = 0; return "";`)
  - 解註 `vacuum_check_(group)` + 失敗 EVT 廣播 + 進入 outer for-loop 下一個 attempt 的 retry 流程

### 行為
- step_down body / feet 階段，extend + valve ON + settle 後讀 JC-100 真空感測，任一吸盤未達 `VACUUM_THRESHOLD_X10 (-50 kPa)` → fail
- fail 時：發 `EVT vacuum_fail <group> attempt=N slaves=A,B,...`，回到 for-loop 下一輪 → 關 valve、收推桿、跑 `backup()`（rail 後退 5 cm）、再 extend + valve + verify
- 最多 retry `VACUUM_RETRY_MAX (5)` 次；都失敗才回 `vacuum_retry_exceeded <group>`

### 待驗
- [ ] 故意讓某顆吸盤吸不到（例如吸盤外貼膠帶）→ EVT vacuum_fail 觸發 + rail 後退 5 cm + 重吸
- [ ] 5 次都吸不上 → step_down 回 `ERR vacuum_retry_exceeded body` / `feet`，狀態進 Error
- [ ] 正常情況：第 1 attempt 就 OK，retry_count = 0

## 2026-04-27i — Claude Code — step_cm 改為 runtime 可調（GUI input → step_down/run 按鈕讀值帶參送出）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `STEP_CM` 常數拆成 `STEP_CM_DEFAULT=30 / STEP_CM_MIN=5 / STEP_CM_MAX=60`
  - 新增 member `std::atomic<int> step_cm_`，constructor 預設 30
  - `cmd_step_down()` 改 `cmd_step_down(int cm = 0)`，`cmd_run(int steps)` 改 `cmd_run(int steps, int cm = 0)`，cm=0 = 用當下 step_cm_
- `user_lib/WASH_ROBOT.cpp`：
  - constructor 加 `step_cm_(STEP_CM_DEFAULT)`
  - `cmd_step_down(cm)` 和 `cmd_run(steps, cm)`：cm > 0 時 validate 5..60 再 store；超範圍回 ERR step_cm_out_of_range
  - `do_step_down_` 內 3 處 STEP_CM 改用 `step_cm_.load()`
- `washrobot_new_PI/main.cpp`：
  - dispatch `step_down [cm]` / `run <n> [cm]` 都接受可選 cm 參數
  - header 註解同步更新
- `web_backend/public/index.html`：auto cycle panel 加 `step cm` input (5-60，預設 30)，`step_down` button 從 `data-cmd` 改 `id="btn-step-down"` 走 JS handler
- `web_backend/public/app.js`：新增 `readStepCm()` helper、`btn-step-down` handler、`btn-run` 改成同時讀 run-steps + step-cm 送出 `run <n> <cm>`

### 行為
- 使用者每次按 step_down / run，**先讀 input 的 step cm 值**，連同指令一起送
- Backend 收到 cm 時先 validate 5..60 範圍，OK 才存 atomic + 執行；超範圍回 ERR、不執行動作
- cm 不送（純 `step_down` 或 `run <n>`）時用當下 step_cm_（預設 30）
- 兼容向後：raw command 沒帶 cm 也能跑

### 待驗
- [ ] GUI 改 step cm = 15 → 按 step_down → log 顯示 `start → Running (step=15 cm)` + 實際 rail 走 15 cm
- [ ] GUI 改 step cm = 60 → run 5 → 5 步 × 60 cm 都正確
- [ ] 改 step cm = 4（超下限）→ 客戶端攔截或 backend 回 ERR step_cm_out_of_range
- [ ] 改 step cm = 70（超上限）同上

## 2026-04-27h — Claude Code — `dm2j_pair_poll_done_` read_status 加 retry（解 RS485 gateway 單次掉包誤判 fail）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `dm2j_pair_poll_done_`：
  - 新增 lambda `read_status_retry` 包裝 `D_(slave).read_status()` — 最多 3 次嘗試、每次間隔 50 ms
  - slave_a / slave_b 的 read_status 都改用 retry 包裝

### 原因
現場 step_down 出現 `slave 3 comms error at 5200ms` → `feet_rail_home_fail`，但使用者確認**硬體實際移動到位**（5 顆 EEPROM 都寫過、物理位置正確）。表示 RS485-over-TCP gateway 在動作中偶有單次 Modbus frame 掉包（已知問題，work_log 記錄過 "TCP buffer 殘留干擾"），但**單次失敗不該整個動作 fail**。Retry 後若真的死掉才認 fail。

### 同步啟動保證
PR 廣播觸發 (`PR_trigger_sync`) 在 `dm2j_pair_move_abs_` 內，本 PR **完全不動**那段。retry 只加在「等待完成」的 polling，不影響兩腳同瞬間啟動。

### 待驗
- [ ] step_down 重跑：feet phase rail → 0 cm 不再因為單次 read_status 掉包 fail
- [ ] 真的物理 fault / 通訊全斷 時仍能在 3 次 retry 後正確 fail（不會無限等）

## 2026-04-27g — Claude Code — cmd_init 加入腳組 rail 歸零（DM2J slave 1, 3 → abs 0）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_init_impl_()`：在 wheel retract（slave 2, 4 → 0）之後、ZDT enable 之前，新增腳組 rail 歸零動作

### 做法
- `dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, 0.0)` — broadcast PR1 同瞬間啟動，bystander 2/4/5 已在 `WashRobot::init()` L92-94 設 PR1 rpm=0 safe
- 完成後 `rail_pos_cm_.store(0.0)` 同步軟體紀錄
- 失敗回 `ERR feet_rail_home_fail`

### 為什麼用 sync pair
左右腳 rail 機械剛性耦合（do_step_down 也是因為這個用 broadcast）— 一般循序 `PR_move_cm` 兩顆會有 ms 級時序差，剛性連桿可能受傷。

### 待驗
- [ ] 現場測試：點 init → 兩顆 foot rail 同步歸 0、無聲音/震動異常
- [ ] 若 foot rail 啟動位置已是 0，廣播觸發是否會因 0 距離立刻完成（預期：是，pair_move 內部位置讀取 + log 會看到 Δ=0）

## 2026-04-27f — Claude Code — [跨界: user_lib] DM2J driver 內部 timeout 10s → 20s

### 修改檔案
- `user_lib/DM2J_RS570.cpp`：
  - `PR_move_cm()` 內 hard-coded `timeout_ms` 10000 → 20000
  - `PR_move_cm_trigger_all()` 內 hard-coded `timeout_ms` 10000 → 20000

### 原因
延續 04-27e — 為了支援 60 cm @ 200 rpm（18 秒）動作，driver 自己的內部 timeout 也要對齊。否則 `cmd_move` / `cmd_arm_sweep` / `cmd_init_impl_` 收輪等走 `PR_move_cm` 的單顆動作仍會撞 10 秒 timeout。

### 規範邊界
屬 user_lib 內部改動，**不改 public API 簽名**（const value 變更）。依 CLAUDE.md「不改 public API 簽名的內部改動 → 協作者可以修，但 PR 必須標 [跨界: user_lib]」。

## 2026-04-27e — Claude Code — DM2J 動作 helper timeout 加大（撐 60 cm @ 200 rpm = 18 秒）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `dm2j_wait_done_` default timeout 10000 → 20000 ms
  - `dm2j_pair_move_abs_` default timeout 15000 → 20000 ms

### 計算依據
60 cm × 10000 PPR = 600,000 pulses；200 rpm × 10000 PPR / 60 = 33,333 pulses/sec；移動時間 600,000 / 33,333 ≈ 18 秒；加速/減速段約 0.2 秒；合計 ≈ 18.2 秒。加 10% safety margin 取 20 秒。

### 已知未處理（待跨界 user_lib）
~~`user_lib/DM2J_RS570.cpp` 內 `PR_move_cm()` 自己有寫死的 `const int timeout_ms = 10000`。~~ 已在 04-27f 一併處理。

## 2026-04-27d — Claude Code — DM2J_RPM 100 → 200 還原（EEPROM 電流參數已存好）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`DM2J_RPM` 100 → 200

### 原因
使用者用廠商工具把所有 DM2J 驅動器的電流/微步進等參數調好並寫進 EEPROM (`0x1801 = 0x2211`)，斷電重啟驗證過參數保留，不再有失步問題。可以還原到 200 rpm 正常速度，避免 step_down 撞到 `dm2j_pair_move_abs_` 預設 15 秒 timeout（100 rpm 下 30 cm 需 18 秒，會 fail 回 `body_rail_forward_fail`）。

### 後續
- ACC/DEC 維持 500（避免加速段失步的保險不必撤）
- step_down body phase 30 cm @ 200 rpm = 9 秒，回到 timeout 安全範圍

## 2026-04-27c — Claude Code — DM2J_RPM 200 → 100（電流不足下穩態失步驗證）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`DM2J_RPM` 200 → 100

### 原因
ACC=500 後 slave 2/3/4 仍有「卡住的聲音」（穩態失步聲，非加速段問題）。診斷指向驅動電流被前面 0x2233 bug 出廠還原打回 default（~1A），無法支撐 200 rpm 的負載。降速到 100 rpm 看能不能用 default 電流跑得動以縮小範圍。

### 補充情報
使用者目前 slave 1 能動是因為剛用廠商工具調好參數，但是 RAM 暫態，未存 EEPROM (`0x1801 = 0x2211`)。下次斷電會回出廠。

### 待驗
- [ ] 100 rpm 下 slave 2/3/4 能不能動
- [ ] 不論結果，都要用廠商工具把所有 5 顆 DM2J 的電流 + 微步進等參數**存進 EEPROM**（`0x1801 = 0x2211`），否則斷電就回出廠
- [ ] EEPROM 存好後，可考慮把 RPM 還原回 200

## 2026-04-27b — Claude Code — DM2J ACC/DEC 100 → 500（解 slave 2/3/4 加速失步 stall）

### 修改檔案
- `user_lib/WASH_ROBOT.h`:
  - `DM2J_ACC` 100 → 500
  - `DM2J_DEC` 100 → 500

### 原因
slave 2, 3, 4 用 200 rpm 跑時物理卡住（廠商 JOG 工具同樣馬達能動）。`DM2J_ACC=100` 在 DM2J 手冊單位「ms / 1000 rpm」下，200 rpm 加速時間僅 20 ms，太陡造成失步 stall。改成 500 → 200 rpm 加速時間 100 ms，類似廠商 JOG 工具的溫和曲線。對整段移動時間幾乎無影響（30 cm 約 9 秒，加速段 100 ms 可忽略）。

### 影響範圍
全域改動，影響所有 DM2J PR 動作：腳組同動 (`dm2j_pair_move_abs_`)、輪組 (`cmd_init_impl_` 收輪 / `cmd_wheels` / `cmd_move`)、手臂 (`cmd_arm_sweep`)、bystander-safe init (rpm=0 → 不影響)。

### 待驗
- [ ] 現場：slave 2, 3, 4 在 ACC=500 下能正常動作不再 stall
- [ ] 若仍 stall，下一輪試 1000；同時檢查驅動器電流參數是否被前面 0x2233 bug 出廠還原導致電流不足

## 2026-04-27a — Claude Code — Linux_test 拆掉 dm2j_manual_enable/disable 本地 helper（誤植 0x2233 = 出廠還原）

### 修改檔案
- `Linux_test/main.cpp`:
  - 刪除 L97-136：comment block + `_dm2j_crc16` + `dm2j_write_0x1801` + `dm2j_manual_enable` + `dm2j_manual_disable`
  - menu 群組同步：`dm2j_manual_enable(cli, s)` → `drv[s].motor_enable()`
  - menu 群組 cleanup：`dm2j_manual_disable(cli, s)` → `drv[s].motor_disable()`
  - menu 17 emergency cleanup：
    - `dm2j_manual_enable(cli20, …)` → `dm2j_L.motor_enable()` / `dm2j_R.motor_enable()`
    - `for (s=1..5) dm2j_manual_disable(cli20, s)` → 直接對 5 個 driver instance 各呼叫 `motor_disable()`（slave 1=`dm2j_L`、2=`bL`、3=`dm2j_R`、4=`bR`、5=`arm`，都是上面已經 init 好的）

### 為什麼修

Sadie 觀察「每次重新上電後，用程式發送控制 DM2J 的指令都會有問題，參數都要重新調一遍」。

挖出主嫌：`dm2j_manual_disable` 寫 `0x1801 = 0x2233`，註解寫「DISABLE」但對照 DM2J V1.0 原廠手冊 + `.claude/summaries/DM2J_RS_MODBUS_SUMMARY.md:185`，**0x2233 是「所有參數恢復出廠值」不是 disable**。

兩個 callsite（menu 群組結尾 cleanup + menu 17 emergency cleanup）每次跑都會把全部 5 顆 DM2J 出廠還原一次，且這個指令通常會持久化到 EEPROM → 重新上電 EEPROM 仍是出廠值 → Sadie 必須再手動把 PPR / micro-step / enable mode 等重灌。完美符合症狀。

### 為什麼選砍 helper（不選改 helper 內部值）

- `user_lib/DM2J_RS570::motor_enable() / motor_disable()` 04-24ay 已正確實作（`0x000F = 1 / 0`）
- helper 是「未實作前的暫時 workaround」，現在 user_lib 已落實，留著只會誤導下一個讀 code 的人
- 連 helper 用的 `_dm2j_crc16` 也只給 `dm2j_write_0x1801` 用，可以一起清掉
- 一次拆乾淨 ≈ 40 行死碼

### 側面影響

- `washrobot_new_PI` 主程式不受影響（從沒用過這 helper，主程式只透過 `user_lib/WASH_ROBOT.cpp` 間接呼叫，而 user_lib 寫的是正確的 `0x000F`）
- 只有 Linux_test 本身會受惠

### 待 Sadie 跟進
- [ ] 受傷的 DM2J 驅動器需重新調參數 + 寫 EEPROM（正確存檔指令是 `0x1801 ← 0x2211`，不是 0x2222 也不是 0x2233）
- [ ] 部署修好的 Linux_test 到現場 Pi
- [ ] 後續若還在掉參數，看別的地方有沒有別人也踩到 0x2233（grep 全 repo 已確認沒有，只有此處）

## 2026-04-24be — Claude Code — [跨界: user_lib] 拿掉 WashRobot::init 啟動時放輪組 (-7cm) 那段

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `WashRobot::init()`：
  - 移除啟動時對 slave 2, 4（左右輪）的 `PR_move_cm_nowait(-7.0)` + `dm2j_wait_done_` 整段
  - 留一個 `[REMOVED 2026-04-24]` 註解指向 `wheels lower` TCP 指令作為替代手動觸發方式

### 原因
使用者要求拿掉。啟動時不再自動放輪 — 要用時改呼叫 `wheels lower` 指令（header 已有 `cmd_wheels(action)` 對應）。

### 影響
- 啟動速度變快（省掉 wheels 移動時間 + 潛在 DM2J timeout/FAULT 風險）
- 啟動後輪子位置**未定義**（在上一個程式結束/斷電前的位置）
- 若需放輪，用 web GUI 的放輪按鈕或 TCP `wheels lower` 指令

## 2026-04-24bd — Claude Code — [跨界: user_lib] do_step_down_ 順序交換：body 先 feet 後（對齊 Linux_test menu 7）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_` 整段重寫：
  - **順序交換**：Phase A = body（原本 feet），Phase B = feet（原本 body）
  - **絕對目標**：Phase A rail → +STEP_CM，Phase B rail → 0
  - **Crane 搬到 body phase**：pay_out (STEP+margin) → rail 前進 → retract margin，配合 body 下降
  - **Retry 方向反轉**：body_backup → rail -VACUUM_BACKUP_CM（body 退回）；feet_backup → rail +VACUUM_BACKUP_CM（feet 退回）
  - **Center 推桿 / CH_VALVE_CENTER 處理搬到 body phase**（因為 body 現在先動，center 跟 body 同組）
  - **移除 step-compensation 狀態**：`rail_at_start` / `residual_start` / `body_residual_cm_.store()` / `actual_feet_cm_` 全部拿掉 — 用絕對目標就夠了，不需跨 step carryover

### cmd_run 受惠
`cmd_run(int steps)` 內部呼叫 `do_step_down_()` 迴圈 N 次，自動跟著新邏輯跑，不用改。

### 行為變化
- 原本：feet 先下降 → body 追上 → wash sweep（跳過）
- 新版：body 先下降 → feet 追上 → wash sweep（跳過）
- Rail 0 → +STEP_CM → 0 每個 step 循環
- Crane pay_out/retract 發生在 body phase（第一個下降的）

### 未動
- `cycle_group_` template（H 檔）— 共用 retry + vacuum check 架構不變
- `cmd_attach` / `cmd_detach` — 不動
- Phase C wash sweep — 仍是 TEST MODE skip

## 2026-04-24bc — Claude Code — Linux_test menu 7 step 順序交換：body 先 feet 後（都用絕對路徑）

### 修改檔案
- `Linux_test/main.cpp` `test_full_step` 迴圈內的 step body 整段重寫：
  - **順序交換**：Phase A = body（原本是 feet），Phase B = feet（原本是 body）
  - **絕對目標**：Phase A rail → `+step_cm`（身長，body 下降），Phase B rail → `0`（收回，feet 跟上）
  - 改用 `dm2j_pair_rail_move_abs_sync()` 直接送絕對 target（原本用 `dm2j_pair_rail_move_sync` 送相對 delta）
  - 移除 step-compensation 計算（`offset`、`phase_a_target`、`phase_b_target`、`feet_delta`）
  - Retry backup 方向符合新流程：body retry `-rail_backup_cm` / feet retry `+rail_backup_cm`，max cumulative 都 = `step_cm`
  - `rail_baseline` 變數保留（step loop 前讀一次，純診斷用）

### 原因
使用者要求：rail 下走一律 `+step_cm`（身長），再回到 0，都用絕對路徑；body 先 feet 後。移除原本的 step compensation 邏輯以符合「簡單絕對」流程。

### 行為變化
- **每個 step 開頭**：body 組先釋放/收推桿/rail +step → 吸住
- **每個 step 結尾**：feet 組釋放/收推桿/rail 0 → 吸住
- Rail 總是 0 → +step → 0 循環
- 若 Phase B retry 剩下 offset，下一 step 不會自動補償（絕對目標 +step 和 0 固定）

### Menu 11/12 未動
只改 menu 7 (`test_full_step`)。menu 11 (`test_full_step_no_rail_verify`) 和 menu 12 (`test_full_step_report`) 順序不變，因為使用者只指定 menu 7。

## 2026-04-24bb — Claude Code — WASH_ROBOT DM2J 速度全面降到 200 rpm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `DM2J_RPM` 700 → 200（影響所有腳組/輪組 rail 動作、`cmd_move`、`cmd_wheels`、啟動下輪、`dm2j_pair_move_abs_`）
  - `ARM_SWEEP_RPM` 500 → 200（影響 `cmd_arm_sweep`）

### 保持不變
- `WashRobot::init()` L92-94 bystander-safe `PR_move_set(1, 1, 0, 0, …)` 的 `rpm=0` 維持不動，這是廣播同動時叫輪/手臂 no-op 的保險值

### 原因
使用者要求把主程式所有 DM2J 動作都降速到 200 rpm。

### 待驗
- [ ] 現場觀察：降速後動作是否仍在合理時間內完成（注意 `dm2j_wait_done_` 預設 timeout 10 s，長距離 rail 移動可能會逼近上限）
- [ ] 若出現 timeout，需考慮提高 `dm2j_wait_done_` 的 timeout 或針對某些動作個別覆蓋 rpm

## 2026-04-24ba — Claude Code — Linux_test DM2J_RPM 500 → 200（menu 7/11/12 共用）

### 修改檔案
- `Linux_test/main.cpp`:
  - `DM2J_RPM` 常數從 500 → 200
  - 註解裡提到 500 同步改成 200

### 原因
使用者要求 menu 7 的 DM2J 軌道移動速度改 200 RPM（更慢、更穩定）。`DM2J_RPM` 是全域常數 menu 7 / 11 / 12 共用，都會受影響（它們都是 full-step 測試變體）。

Menu 2 不受影響（直接用數字 500）。主程式 WASH_ROBOT.h 的 `DM2J_RPM=700` 也不變。

## 2026-04-24az — Claude Code — menu 2 加 mode 選項 (PR abs / PR rel / JOG) + 位置回讀驗證

### 修改檔案
- `Linux_test/main.cpp` `test_dm2j` (menu 2)：
  - 加 mode 選擇：`a=PR absolute` (原行為) / `r=PR relative` (mode=2) / `j=JOG forward 2s`
  - JOG 路徑用 `set_jog_speed(200)` + `jog_forward()` + 2s sleep + `jog_stop()`
  - 尾端加位置回讀 + `Δ` 顯示：驗證 drive 報完成時馬達實際有沒有動

### 原因
使用者用廠商工具「使能 + 正向轉」（= JOG）馬達會動，但 menu 2 PR absolute 報 CMD_DONE 但物理沒動；歸 0 後還是不動。要確認：
1. 我們的程式呼叫 JOG 能不能動（跟廠商工具同路徑）
2. PR relative (mode=2) 能不能動
3. 動完位置有沒有真的到 target

### 三個可能結論
- **JOG 動、PR 不動** → 確認 PR 子系統 drive-side 有配置問題，需對比廠商工具 Pr 參數
- **JOG 也不動** → 我們程式通訊層有問題（基本排除，因為其他 DM2J 都 OK）
- **位置 Δ 跟 target 差很多** → drive fake completion 確認，需深入 PR 配置

## 2026-04-24ay — Claude Code — [跨界: user_lib] DM2J 實作 motor_enable/disable/save_params/reset_alarm + menu 2 加 pre-step

### 修改檔案
- `user_lib/DM2J_RS570.h`:
  - 4 個函式的註解改成**真實指令**（對照手冊 §5.3.2/5.3.3）：
    - `motor_enable()` → `0x000F (Pr0.07) = 1` 軟體強制使能（原舊註解 `0x1801=0x1111` 錯誤）
    - `motor_disable()` → `0x000F = 0`
    - `save_params()` → `0x1801 = 0x2211`（原 `0x2222` 錯；那是參數初始化不是儲存）
  - 新增 `reset_alarm()` 宣告（`0x1801 = 0x1111` 清當前警報）
- `user_lib/DM2J_RS570.cpp`:
  - 實作 `motor_enable()` / `motor_disable()` / `save_params()` / `reset_alarm()`（之前只宣告沒實作，呼叫會 link 錯）
  - 每個加 `LOG_INF` 標示動作
- `Linux_test/main.cpp` `test_dm2j` (menu 2)：
  - `PR_move_cm` 之前加 `reset_alarm()` + 50ms + `motor_enable()` + 100ms 作為 pre-step
  - 若使用者現場測「廠商工具會動、menu 2 不會動」，補這兩行驗證是不是缺了這塊

### 原因
使用者實測 Linux_test menu 2 有時候馬達不動（status 顯示 ENABLE+RUN 但物理沒移動），但用廠商工具（MotionStudio / JMC tool）同樣參數能動。硬體層沒問題，差別在廠商工具每次連線時可能順便清警報 + 強制使能。補齊這兩個 pre-step 重試。

若有效：
- 下一步可以評估要不要把 `reset_alarm()` / `motor_enable()` 搬進 `DM2J_RS570::init()` 或 WASH_ROBOT `init()` 預設 per-slave 做一次
- 或每個 cmd_* 呼叫前做一次（保守但通訊流量增加）

### 驗證方式
跑 menu 2 → slave 4 → target 7cm。
- 若現在能動 → 確認「缺 reset_alarm + motor_enable」是主因
- 若還不動 → 問題在更深層（驅動器 Pr 參數）、或 broadcast 層面

## 2026-04-24ax — Claude Code — GUI 新增收輪/放輪按鈕 + 新 `wheels <retract|lower>` 指令

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新增 `cmd_wheels(const std::string& action)` 宣告
- `user_lib/WASH_ROBOT.cpp`：實作 `cmd_wheels` — `retract` = abs 0 cm / `lower` = abs -7 cm，兩輪 `PR_move_cm_nowait` ×2 + 依序 `dm2j_wait_done_`，Error 狀態拒絕、其他狀態允許
- `washrobot_new_PI/main.cpp`：dispatch 新增 `wheels <retract|lower>` 分支、header 註解同步更新
- `web_backend/public/index.html`：在 `manual — pusher` 下方新增 `manual — wheels` panel，含兩按鈕 `wheels retract` / `wheels lower`

### 原因
使用者要求 GUI 可手動下/收輪組。命名與行為 mirror 啟動時的 init() 動作（平行移動、不用 broadcast sync，避免需要設 slave 1, 3 bystander-safe）。

### 待驗
- [ ] 現場測試：GUI 點「放輪 (abs -7)」時兩輪同步下降、無碰撞
- [ ] GUI 點「收輪 (abs 0)」時兩輪回到 0 位置，且不會撞到其他結構
- [ ] 回報狀態正常（OK / ERR）顯示於 log

## 2026-04-24aw — Claude Code — 啟動時自動下輪（slave 2, 4 → abs -7 cm，平行）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：`WashRobot::init()` 尾端（relays off 之後、`return false;` 之前）加入啟動時自動下輪動作

### 做法
- `D_(DM2J_LEFT_WHEEL).PR_move_cm_nowait(0, 1, DM2J_RPM, -7.0, ...)` + `D_(DM2J_RIGHT_WHEEL).PR_move_cm_nowait(...)` 兩個連續 trigger（無 wait），接著 `dm2j_wait_done_` 各等一次完成
- 兩輪在一個 Modbus frame 內前後觸發，實際是並行移動，不是 broadcast sync → 不需要處理 slave 1, 3 的 PR0 bystander-safe
- 任一 trigger 或 wait 失敗即 `return true` 讓 `main()` 判定為 FATAL

### 原因
啟動時需要先把輪組放下（-7 cm 絕對位置）才能讓車體著地進入後續流程。放在 `init()` 尾端而非 `cmd_init_impl_()`，因為這是**開機動作不是使用者 init 指令的一部分**，且此時 TCP server 還沒開（`main()` 在 init 之後才啟 server），不會被 web 指令干擾。

### 待驗
- [ ] 現場測試：機器啟動後兩輪是否同時放下到 -7 cm，動作平順無碰撞
- [ ] DM2J slave 2, 4 啟動前的絕對座標零點假設是否正確（若未歸零，-7 cm 絕對位置沒有意義 → 日後可能需要在此動作前加 `home_set_current_pos_zero()`）

## 2026-04-24av — Claude Code — [跨界: user_lib] DM2J 對同步組 helper（修 1 診斷 log + 平行 poll）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：宣告 `dm2j_pair_move_abs_(slave_a, slave_b, pr_num, target_cm, timeout_ms=15000)` + `dm2j_pair_poll_done_(slave_a, slave_b, timeout_ms)` 兩個 private helper
- `user_lib/WASH_ROBOT.cpp`：
  - 實作 `dm2j_pair_poll_done_`：每 iter 同時 poll 兩顆 slave（不序列），任一 fault 或兩者都 done 就返回，有 FAULT/timeout 則 return true
  - 實作 `dm2j_pair_move_abs_`：讀兩顆位置 → 寫 PR block → broadcast trigger → parallel poll → 再讀位置；log 印 `[pair DM2J 1+3] before: 1=X 3=Y → target Z` / `after: 1=X' (ΔdX) 3=Y' (ΔdY) cm`
  - 4 處 DM2J rail move 全改成呼叫 helper：
    - feet_pre_cycle: rail +STEP_CM
    - body_pre_cycle: rail → 0
    - feet_backup: rail -VACUUM_BACKUP_CM (retry)
    - body_backup: rail +VACUUM_BACKUP_CM (retry)
  - 移除舊的兩次 `dm2j_wait_done_` 序列呼叫（它只看一顆完成才看下一顆，掩蓋了平行狀態）

### 原因
使用者實機跑 `run 2` 發現 DM2J slave 1 跟 3 沒同時移動（第二個 step 某顆先完成）。從 log 分析序列 poll 模式看不到真實差異（先 poll slave 1 到 done → 才 poll slave 3，可能 slave 3 早就 done 只是我們晚到）。

新 helper 做兩件事：
1. **Parallel poll**：同一個 iter 裡同時讀兩顆 status，真實反映同步狀況
2. **位置診斷 log**：before/after/Δ 印出來，可以直接看到實際 travel 有沒有差異

Broadcast trigger 保持（廣播 slave=0 在 Modbus 同一幀 = 真硬體同步起步；byspassers 2/4/5 PR1=rpm=0 不會動）。

### 之後排查流程
下次跑 log 看 `[pair DM2J 1+3]` 行：
- Δ 兩邊差很大 → slave 1/3 starting pos 有 drift（累積誤差）
- 某邊 Δ 接近 0 → 那顆可能沒收到 broadcast（USR-TCP232 廣播不穩）或本來就在 target 附近
- FAIL → fault 或 timeout

## 2026-04-24au — Claude Code — [跨界: user_lib] zdt_wait_motion_done_ 偵測到 stall 時清 stall_flag

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `zdt_wait_motion_done_`：stall_flag 分支加 `Z_(slave).release_stall_flag()` 呼叫，然後才 `return false`

### 原因
實機跑 `cmd_run` 第二個 step 的 feet retract 時，slave 3/4 stall（slave 3 卡在 477°、slave 4 卡在 22.65°）。原 helper 把 stall 當「停了」直接 return false 繼續下一步，但 drive 的 stall_flag 沒清掉 — 接下來 cycle_group_ 的 feet extend `motion_control_pos_mode_nowait` 可能被 drive 拒絕 / 忽略，造成 pusher_move_many_ 等第一個 slave 15s timeout 才回錯，表面看像「卡住」。

改成偵測到 stall 時主動呼叫 `release_stall_flag()`，下個 pos_mode 指令 drive 能正常接受。行為仍 return false 讓流程繼續（避免一次 stall 就中斷整個 step）。

### 觀察
Slave 3/4 重複 stall 代表這兩顆推桿有機械阻力（可能：推桿末端卡牆面、桿本身磨損、氣管拉扯）。現場應檢查機構；若持續 stall 可調 `PUSHER_ACC` 降加速度減少堵轉機率。

## 2026-04-24at — Claude Code — [跨界: user_lib][TEST MODE] cycle_group_ 真空驗證跳過（step_down / run 用）

### 修改檔案
- `user_lib/WASH_ROBOT.h` — `cycle_group_` template 在 extend + settle 完成後，直接 `out_retry_count = 0; return "";`（視為貼附成功）。原本的 `vacuum_check_ / evt_(vacuum_fail) / retry` 邏輯整段 `/* ... */` 註解保留，REVERT 時刪這兩行、解開註解即可

### 原因
Sadie bench 測試 `run` / `step_down` 時不想被真空門檻卡住 — 吸盤可能未貼牆或環境條件達不到 -50 kPa。跳過驗證讓 step_down 流程可以跑通，專心驗證 crane + DM2J + ZDT 時序。

### 效果
- `do_step_down_` 兩個 `cycle_group_("feet" / "body")` 呼叫都不再驗真空
- 不再有 `vacuum_fail` / `vacuum_retry_exceeded` 錯誤
- Retry loop 不會執行（第 1 次就回 OK）
- backup 機制（DM2J 反向 5cm）不再觸發

### 恢復方式
刪除 `[TEST MODE]` 標記的 `out_retry_count = 0; return "";` 兩行，移除 `/* ... */` 註解包圍。同標記 grep 可找：
```
grep -n "TEST MODE 2026-04-24" user_lib/WASH_ROBOT.h
```

## 2026-04-24as — Claude Code — [跨界: user_lib][TEST MODE] cmd_attach vacuum check 再度註解掉

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_attach`：再度把 `vacuum_check_("all")` 註解掉（上一版 ar 剛恢復），bench 測試直接通過進 Attached

### 原因
使用者要求：attach 真空檢查先註解掉直接通過。Bench 沒貼牆時 attach 永遠失敗擋住後續測試流程；step_down 的 cycle_group_ 內部 vacuum check + retry 保持啟用（那裡才是驗證 seal 品質的地方）。

### 保留
- `cycle_group_` vacuum check 仍**啟用**（Phase A/B 每次伸推桿後都驗證真空，失敗才觸發 retry + crane 放繩）

## 2026-04-24ar — Claude Code — [跨界: user_lib] step_down 恢復 vacuum 檢查 + retry 加 crane pay_out

### 修改檔案
- `user_lib/WASH_ROBOT.h` `cycle_group_` template：解除 TEST MODE 註解，恢復 `vacuum_check_(group)` + retry 迴圈（fails 非空則發 `vacuum_fail` EVT、進 retry；連 VACUUM_RETRY_MAX 次都失敗回 `vacuum_retry_exceeded`）
- `user_lib/WASH_ROBOT.cpp` `cmd_attach`：解除 TEST MODE 註解，恢復 `vacuum_check_("all")` 檢查；任一 cup 失敗回 `ERR attach_vacuum_fail slaves=...`
- `user_lib/WASH_ROBOT.cpp` `feet_backup`：retry rail 後退前加 `crane pay_out <VACUUM_BACKUP_CM>` 放繩，給 rope slack 讓 rail 能自由後退
- `user_lib/WASH_ROBOT.cpp` `body_backup`：同樣加 `crane pay_out <VACUUM_BACKUP_CM>` 在 rail 後退前

### 原因
使用者要求：恢復 step_down 的真空檢查重吸機制 + crane 搭配放繩。

**Retry 流程（新）：**
1. vacuum_check_ 偵測 fail slaves
2. 進 retry：關 valve → 300ms → 收推桿
3. backup lambda：**crane pay_out** VACUUM_BACKUP_CM → rail -5/+5cm 後退
4. valve ON → 伸推桿 → settle
5. 再次 vacuum_check_
6. 最多 VACUUM_RETRY_MAX=5 次，都失敗就 `vacuum_retry_exceeded`

**crane pay_out 時機**：rail backup 之前，跟 Phase A pre_cycle 的「crane pay_out → DM2J 下降 → crane retract」一致 — 先放繩給 slack，再動軌道。

### 仍保留的 TEST MODE
- crane_watchdog_loop_ abort 關閉（意外斷線不 abort）
- imu_monitor_loop_ emergency abort 關閉（bench 傾斜不中斷）
- do_step_down_ Phase C wash sweep 註解掉（DM2J:5 FAULT 未排除）

## 2026-04-24aq — Claude Code — [跨界: user_lib] do_step_down_ DM2J 改 broadcast sync + init 加 bystanders safe PR1

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`:
  - `WashRobot::init()` DM2J 初始化後加 3 行：wheel left/right + arm 的 PR1 設 `rpm=0`（safe no-op）
  - `do_step_down_()` 4 處 DM2J 觸發改用 `PR_trigger_sync(1)` 廣播：
    1. `feet_pre_cycle` rails → +STEP_CM
    2. `feet_backup` rails → current - backup
    3. `body_pre_cycle` rails → 0
    4. `body_backup` rails → current + backup
  - 每處拿掉多餘的 `PR_trigger` 第二次呼叫（廣播只要一次）

### 原因
Sadie 指出腳組 slaves 1,3 機構剛性連接，washrobot 原用個別 `PR_trigger` 兩次有 5~20ms 的 TCP 序列化 skew，足以扭壞機構。改用 `PR_trigger_sync(1)` 廣播讓兩顆在同一個 Modbus frame 同時啟動（<1ms skew）。

廣播會對所有 slave 起作用 → 前置必須把 wheels(2,4) / arm(5) 的 PR1 設成 rpm=0 safe state，broadcast 到它們時執行 rpm=0 = 不動作。wheels 只用 PR0（cmd_init retract），arm 只用 PR0（arm_sweep），PR1 在 bystander 上是一次性設定終身有效。

### 對齊 Linux_test menu 7
Menu 7 早就用 `dm2j_pair_rail_move_sync` + `dm2j_set_safe_pr` 同樣 pattern，這次把 washrobot 生產碼也對齊，修 4 項差異裡最嚴重的 #1。

### 規範邊界
`user_lib/WASH_ROBOT.cpp` 屬 Jim 範圍，標 `[跨界: user_lib]`。純內部改動（public API 不變）。

## 2026-04-24ap — Claude Code — [跨界: user_lib] feet_pre_cycle 恢復 crane pay_out / retract 呼叫

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_` `feet_pre_cycle`：
  - 解除 `crane_cmd_("pay_out <STEP_CM + STEP_MARGIN_CM>")` 的 TEST MODE 註解
  - 解除 `crane_cmd_("retract <STEP_MARGIN_CM>")` 的 TEST MODE 註解
  - 兩處加 `[step_down] crane pay_out/retract <N>` stdout log

### 原因
Changelog ao 將 CRANE_IP 改 127.0.0.1 後 crane_shim 本地可連。使用者指出「step_down 的吊機沒有連動」— 因為我之前為 bench 測試把 crane 呼叫註解掉了。現在 crane 可用，恢復呼叫讓腳組 rail 移動前後跟 crane pay_out/retract 同步。

### 仍保留的 TEST MODE 註解（bench 測試安全）
- `crane_watchdog_loop_` abort 仍關閉（ap 後 crane healthy 時不會觸發；意外斷線時不會誤 abort）
- IMU emergency abort 仍關閉（bench 傾斜不中斷）
- Vacuum check（cmd_attach + cycle_group_）仍關閉（沒貼牆）
- Phase C wash sweep 仍關閉（DM2J:5 FAULT 未排除）

## 2026-04-24ao — Claude Code — [跨界: user_lib] CRANE_IP 改 127.0.0.1（shim 移到 .5.19 與 washrobot 同機）

### 修改檔案
- `user_lib/WASH_ROBOT.h:104` `CRANE_IP`：`"192.168.5.26" → "127.0.0.1"`
- `.claude/easy_crane_test_mode.md §9a` — 撤除清單更新

### 原因
Sadie 回報 step_down 跑完但 easy crane 沒動。診斷：
- 現狀：washrobot + shim 都在 .5.19，實體 easy crane 在 .5.26
- 原本 CRANE_IP 設 `192.168.5.26` → washrobot 想連 shim 卻指到 easy crane 本體（:5002 沒人 listen）
- `crane_cmd_("pay_out 45")` connect 失敗回空字串 → step_down 回 `ERR crane_pay_out_fail` 進 Error

### 新配置
- washrobot → `127.0.0.1:5002`（shim 在同機）
- shim 啟動：`python3 crane_shim.py --easy-host 192.168.5.26`（連實體 easy crane）
- easy crane → 實體 .5.26:5003

本機 loopback 最穩不經網路層。主 crane 部署時仍改回 `192.168.1.101`。

## 2026-04-24an — Claude Code — [跨界: user_lib] body_pre_cycle 補「body valve OFF 先於 retract」（對齊 Linux_test menu 7）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_` `body_pre_cycle`：
  - Retract center/body 推桿之前同時關 `CH_VALVE_BODY` 和 `CH_VALVE_CENTER`（之前只關了 CENTER）
  - 然後 sleep 300ms 讓真空釋放
  - 才開始 retract

### 原因
使用者指出：ZDT 推桿 retract 之前應該先解真空（對齊 Linux_test menu 7）。

Phase A 的 feet_pre_cycle 已經有這步（`CH_VALVE_FEET OFF → 300ms → retract`），但 Phase B 的 body_pre_cycle 只關了 CH_VALVE_CENTER，**沒關 CH_VALVE_BODY**。body valve 從 attach 或 Phase A extend 之後一直 ON，retract 時吸盤還抓著牆：
- 輕則推桿拉扯 seal 損壞
- 重則推桿 stall 收不回來

補上 `CH_VALVE_BODY OFF` 讓狀況對稱到 Linux_test menu 7 pattern。

### 其他 retract 點檢查
- `feet_pre_cycle`：有做 ✓
- `body_pre_cycle` center retract：有做 ✓（CH_VALVE_CENTER OFF）
- `body_pre_cycle` body retract：**之前漏了，本次補上** ✓
- `cycle_group_` retry 內的 retract：有做 ✓（`controlRelay(valve_ch, false)` + `sleep_ms(300)`）

## 2026-04-24am — Claude Code — [跨界: user_lib][TEST MODE] do_step_down_ 註解掉 Phase C 清洗流程

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_` 最後段：
  - 註解掉 `do_arm_sweep_()` 呼叫、return 檢查
  - 保留 `motion_active_ = false`
  - 加 log `[step_down] wash sweep skipped (TEST MODE)`
  - 直接 return "OK step_done\n"

### 原因
- DM2J:5 (arm/上滑台) 在 FAULT 狀態，`PR_move_cm` 立刻 return fault → `ERR arm_right_fail`
- Bench 測試焦點在走路（feet/body/rail），清洗流程（水泵/進水閥/刷子 + arm 左右掃）不是驗證目標
- `do_arm_sweep_` 本身不動（REVERT 後還能用）；只是 cmd_step_down 暫時不呼叫它

### 上牆前要做
1. 處理 DM2J:5 FAULT（讀 error code 0x2203 確認是不是鎖軸 0x80，清 alarm、手動轉軸測機械）
2. 解註解 Phase C

## 2026-04-24al — Claude Code — [跨界: user_lib] cycle_group_ 改 valve ON 在 extend 之前（對齊 Linux_test menu 7）

### 修改檔案
- `user_lib/WASH_ROBOT.h` `cycle_group_` template：把 `pqw_.controlRelay(valve_ch, true)` 從 extend 後移到 extend 前

### 原因
使用者要求：放 ZDT 腳推桿**之前**應該先開抽真空，對齊 Linux_test menu 7 的「pre-engage valve」pattern。

原順序（錯）：extend → valve ON
- 推桿貼牆時吸盤內還是大氣壓
- 閥打開後要從大氣壓抽到真空，seal 效能打折扣

新順序（對）：valve ON → extend
- 推桿還沒碰牆時吸盤已經被抽空
- 接觸牆瞬間吸住，真空 seal 最佳化
- 權威：memory `project_vacuum_seal_patterns.md`「valve-before-extend」

### 未做
Memory 同時提到 staged extend（half → full 兩段），Linux_test menu 7 用 `zdt_group_extend_staged`。這次沒改（WASH_ROBOT 還是一次伸到目標），使用者沒明確要求。上牆測試若 seal 效能仍不足再追加。

## 2026-04-24ak — Claude Code — [跨界: user_lib] ZDT 加 set_debug() + zdt_wait_motion_done_ 暫停 hex dump

### 修改檔案
- `user_lib/ZDT_motor_control.h` — 公開 inline `void set_debug(bool v)` 讓上層 runtime 切 debug_mode
- `user_lib/WASH_ROBOT.h` — 新增 private member `bool driver_dbg_`（記住 init 時 WR_DRIVER_DEBUG 的值）
- `user_lib/WASH_ROBOT.cpp`:
  - init() 裡 `driver_dbg_ = dbg` 記下狀態
  - `zdt_wait_motion_done_` 進入時 `Z_(slave).set_debug(false)` 關掉 ZDT hex dump；正常結束 / timeout 兩條 return 前都 restore `set_debug(true)`

### 原因
Sadie 回報：step_down 時 web GUI log 被 `[DBG] [ZDT:9] RX get_status ...` 洗屏 — zdt_wait_motion_done_ 每 150ms 呼叫 get_system_status，啟用 debug 時每次 TX/RX 都印 hex dump（~7 行/秒）。只要關掉 poll loop 的 hex 即可，ad-hoc motion 命令的 hex dump 保留（方便 debug trigger / stall）。

### 效果
- poll loop 完全靜默（不再 get_status hex 洗屏）
- 單一 motion 命令（`motion_control_pos_mode` 等）的 TX/RX 仍然印（debug 價值保留）
- PQW / DM2J / JC-100 等其他 driver 的 hex 不受影響
- WR_DRIVER_DEBUG=0 仍然完全關閉所有 debug（driver_dbg_=false，set_debug 呼叫被 skip）

## 2026-04-24aj — Claude Code — [跨界: user_lib][TEST MODE] 關掉 crane watchdog 和 IMU 緊急 abort（bench 測試）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`:
  - `crane_watchdog_loop_` (line 263): `abort_flag = true` 註解掉；改成只發 `crane_watchdog timeout (TEST MODE — abort suppressed)` EVT 不 abort
  - `imu_monitor_loop_` (line 377): IMU > 45° 持續 500ms 的 `abort_flag = true` + `motion_active_ = false` + `crane_cmd_(emergency_stop)` + `set_state_(Error)` 整段註解掉；改成只發 EVT 不 abort

### 原因
step_down Phase B 途中（center 推桿 extend + CH4 valve ON 之後）突然 `ERR aborted`。身體推桿沒 extend。追源頭發現背景執行緒 `crane_watchdog_loop_`：ping crane 沒回應 → 60s timeout → 看到 `motion_active_==true` 就 `abort_flag=true`。Bench 沒接 crane，從程式啟動起 ping 就不會 OK，累積到 step_down 剛好過 60s → 觸發。

同時預防 IMU 緊急 abort — bench 上機身可能隨便傾斜，> 45° 也會 abort。

兩個 abort source 都註解掉，TEST MODE marker 保留 `[TEST MODE 2026-04-24]`，正式部署前回復。

### 同時保留的 EVT
- `crane_watchdog timeout (TEST MODE — abort suppressed)` — 每次 watchdog 仍會發，讓操作員知道 crane 沒上線
- `imu_emergency balance_deg=X (TEST MODE — abort suppressed)` — 仍會發，不中斷運動

## 2026-04-24ai — Claude Code — [跨界: user_lib] cmd_init 結束廣播 EVT init_complete

### 修改檔案
- `user_lib/WASH_ROBOT.h`:
  - 私有區加 `std::string cmd_init_impl_();` 宣告
- `user_lib/WASH_ROBOT.cpp`:
  - 把原本的 `cmd_init()` 改名 `cmd_init_impl_()`（實際 init 邏輯）
  - 新增薄 `cmd_init()` 包裝層：呼叫 `cmd_init_impl_()` 拿到 result → 依成功/失敗廣播 EVT：
    - Success：`EVT init_complete status=ok`
    - Failure：`EVT init_complete status=fail reason=<reason>`
  - 原樣回傳 result 給 caller

### 原因
Sadie 要求：init 完成不論成功失敗都要有狀態 log 廣播到 web。原本 `cmd_init` 只**回傳**字串給發命令的 client（Web 看得到 `[washrobot] OK init_done`），但其他同時連線的 client 看不到。改成用 `evt_()` 廣播 EVT，讓所有 GUI client 都收到。
用 wrapper pattern 避免動到 13 個 return 分支各自加 EVT，單點集中處理。

### GUI 端會收到
```
[washrobot] EVT state_changed idle ready
[washrobot] EVT init_complete status=ok               ← 新增
[washrobot] OK init_done

# 或失敗時：
[washrobot] EVT init_complete status=fail reason=pump_on_fail
[washrobot] ERR pump_on_fail
```

## 2026-04-24ah — Claude Code — [跨界: user_lib] cmd_attach / cmd_detach / cmd_step_down 加 progress std::cout

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`
  - `cmd_attach`: 加 `[attach] center pusher extend / open valves / settle Xms / done → Attached`
  - `cmd_detach`: 加 `[detach] close valves CH2/3/4 → Ready`
  - `cmd_step_down`: 加 `[step_down] start → Running / done / FAIL: <reason>`

### 原因
Web GUI 按 attach 後 stdout 最後一條是 PQW 第三個 relay RX echo，接著 2000ms settle + state change + TCP reply 都不走 stdout，看起來像「卡住」但實際上 cmd_attach 已經成功返回 `OK attached`。加 [attach] done 讓 stdout 有明確完成訊息，避免誤判。

## 2026-04-24ag — Claude Code — [跨界: user_lib][TEST MODE] do_step_down_ 對齊 Linux_test menu 7 pattern

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_`:
  - **feet_pre_cycle / body_pre_cycle**：`PR_trigger_sync(1)`（廣播）改成 `PR_trigger(1)` 對左右腳軌道個別觸發 — 避免誤觸發 slaves 2/4/5（輪子 + 上滑台）的 stale PR1 資料
  - **feet_pre_cycle crane_cmd_("pay_out ...") + crane_cmd_("retract ...")**：註解掉，bench 測試沒接 crane 否則每次 30s timeout
  - **feet_backup / body_backup**：mode=0 relative 改 mode=1 absolute（mode=0 在此 firmware 是「PR 未配置」不動）— 目標 = `rail_pos_cm + backup_signed`
- `user_lib/WASH_ROBOT.h` `cycle_group_` template：註解掉 `vacuum_check_(group)` + retry 迴圈，`pusher_move_many_` 成功後直接 `return ""`（跟 cmd_attach 註解掉 vacuum 對齊）

### 原因
現場實測 step_down log 顯示 Phase A 收完 feet 推桿後（`[wait ZDT:1/2] done`）就沒進展 — 下一步 `crane_cmd_("pay_out ...")` 在 bench 沒 crane 會等 30s timeout 才回錯，使用者看起來像卡住。

加上之前發現的三個子 bug（broadcast 會誤觸發無關 slaves、mode=0 無效、bench vacuum 不可能通過），一次把 do_step_down_ 改成對齊 Linux_test menu 7 可跑的 pattern。

所有註解都加 `[TEST MODE 2026-04-24]` marker，正式上牆部署前 revert。

### 剩下跟 Linux_test menu 7 的差異（未改）
- Linux_test menu 7 用 `zdt_group_extend_staged`（half → full 兩段），WASH_ROBOT 還是一次伸到目標
- Linux_test 「valve ON before extend」預抽真空（memory vacuum_seal_patterns）；WASH_ROBOT cycle_group_ 是 extend → valve ON（順序反）

這兩點對 bench 測試沒差（沒真空可驗證），上牆 seal 效能可能打折扣，日後真實測試再評估。

## 2026-04-24af — Claude Code — [跨界: user_lib][TEST MODE] cmd_attach 註解掉 vacuum check

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_attach`：註解掉 `vacuum_check_("all")` + `if (!fails.empty()) return "ERR ..."` 整段，讓 state 直接進 Attached

### 原因
現場 bench 測試沒真的貼牆 → 吸盤無法 seal → JC100 都讀到 ≈-0.2 kPa（閾值 -50 kPa）→ vacuum_check_ 全 fail → cmd_attach 回 `ERR attach_vacuum_fail slaves=1..9` → state 沒進 Attached → 接著按 step_down 被 `cur != Attached` 擋下 → 看起來「沒反應」。

使用者要求先註解掉讓下一步能測試。加 `[TEST MODE 2026-04-24]` marker，正式部署到牆上前記得 REVERT。

### 下一個可能的雷
`WASH_ROBOT.h::cycle_group_` template（do_step_down_ 用）line 328 也有 `vacuum_check_(group)`：若真空還是沒 seal，step_down 會 retry 5 次（VACUUM_RETRY_MAX）後回 `vacuum_retry_exceeded feet/body`，整個 cmd_step_down 返錯。若 bench 測試需要 step_down 也能通，那裡也要註解掉。

## 2026-04-24ae — Claude Code — [跨界: user_lib] **CRITICAL** ZDT get_system_status return 值反了

### 修改檔案
- `user_lib/ZDT_motor_control.cpp`:
  - `get_system_status()` line 182：`return true` → `return false`（成功要回 false 符合專案慣例）
  - `wait_until_pos_reached()` line 188-189：`if (get_system_status() && status.pos_reached) return true;` → `if (!get_system_status() && status.pos_reached) return false;`（配合上面的 convention 修正）

### 原因（**影響所有 ZDT 用 status polling 的地方**）
實機 log 顯示 slave 3 `get_system_status` TX/RX bytes 完全正常（37 bytes response, 可正確解析），但 WASH_ROBOT 的 `zdt_wait_motion_done_` 判為 100 次連續 comms fail。

挖到 driver 源碼發現 bug：`get_system_status` **所有 return 路徑都是 `return true`** — send 失敗、response 太短、**以及成功解析完** — 全部回 true。按專案慣例 `false=success, true=error`，所有呼叫端做 `if (get_system_status()) ...error...` 都把成功當失敗。

Log 證據：RX `03 04 20 23 09 5C C2 00 66 C3 06 ...` 正好 37 bytes，byte count 0x20=32，格式完美；但我的 helper 100% 失敗。只能是 return 值反了。

### 連帶修正
`wait_until_pos_reached` 內部用 `get_system_status() && status.pos_reached` — 之前靠 get_system_status 回 true=成功才進 if，表面上是「同錯抵消」能 work；現在修成 convention 後要翻成 `!get_system_status() && status.pos_reached`。

### 影響範圍
修完後以下呼叫端自動變對（原本都默默踩雷）：
- `WASH_ROBOT::zdt_wait_motion_done_`（我的 helper）
- `Linux_test/main.cpp` menu 3 `test_zdt` line 321
- `Linux_test/main.cpp` menu 7 `zdt_group_move_sync` line 855
- `Linux_test/main.cpp` menu 16 `test_dm2j_group_sync` 若有用到
- `ZDT_motor_control::wait_until_pos_reached` 內部（已同時修）

原本 Linux_test menu 7 「能 work」可能是靠其他 fallback（如 POS_DELTA_DEG 穩定偵測繞過）或者實際也有悄悄 bug 只是沒被注意到。

**「CRITICAL」**：這 bug 擋住所有 ZDT motion 完成偵測；修完 init / attach / step / run 任何有 ZDT 等待的指令都會受惠。

## 2026-04-24ad — Claude Code — 選項 17 cleanup 改個別 relay 關閉（修 CH1 pump 沒關）

### 修改檔案
- `Linux_test/main.cpp` `test_cleanup()` — PQW 清理區塊改用 for 迴圈 `controlRelay(ch, false)` 關 CH1..CH8，`controlAll(false)` 降級為最後 belt-and-suspenders

### 原因
Sadie 回報選項 17 跑完 CH1 pump 沒關。根因：`controlAll` 用 PQW 廠商特殊的 0x0085 register，此韌體版本 echo 長度檢查通過但實際不作動（跟 2026-04-21 debug PQW 時看到的現象一致）。改成逐通道 `controlRelay` function 0x05 標準寫入，每顆 relay 都有實際 TX → 一定送出命令。

## 2026-04-24ac — Claude Code — Linux_test 新增選項 17「Emergency cleanup」

### 修改檔案
- `Linux_test/main.cpp`:
  - 新增 `test_cleanup()`（~100 行）— 獨立 cleanup 工具，跟選項 7 尾段 cleanup 行為一致但可單跑
  - 選單加第 17 項

### 流程
1. 三個 gateway IP 輸入（預設 .20/.21/.22），各自 quick_tcp_probe 判定
2. PQW（若 .22 通）：CH2/3/4 OFF → 300ms settle → `controlAll(false)` 關 pump + CH5-8
3. ZDT（若 .21 通）：9 顆 release_stall + enable → 廣播 retract 到 0 → disable 全部
4. DM2J（若 .20 通）：bystanders(2,4,5) 設 safe PR1 → 1,3 enable → 廣播返回 0 → 1..5 全 disable
5. 各 gateway 單獨失敗不中止整個 cleanup（skip 該層 + [WARN]）

### 原因
Sadie 要一個現場 panic button：測試中途掛掉 / 按錯順序 / 現場要撤走，一鍵把機器回 parked state（繼電器 OFF / 推桿收光 / 軌道歸 0 / 馬達 disable）。單獨跑比要 rerun 選項 7 全套快很多，也避免選項 7 前半段誤動到卡在奇怪狀態的機構。

## 2026-04-24ab — Claude Code — [跨界: user_lib] zdt_wait_motion_done_ 對齊 Linux_test pattern（sleep 先於 poll + 無 comms fail 限制）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `zdt_wait_motion_done_`：
  - sleep 移到迴圈頂端（從 poll 後 → poll 前）：broadcast trigger 後給 150ms 暖身窗讓 TCP gateway buffer 對齊穩定，再開始讀 status
  - 移除 `COMMS_FAIL_MAX` 上限：只要在 15s 全域 timeout 內都持續 retry，不因為連續失敗就放棄
  - 失敗時累計 total_fails，success / timeout log 印出總失敗次數供診斷

### 原因
上輪 (aa) 雖然把容忍度從 3 → 10 次，實機 log 顯示「comms fail x10 at 1350ms」— 10 次連續失敗都不恢復，motion 實際成功但判定 fail。

對比 Linux_test `zdt_group_move_sync` 迴圈結構：**先 sleep 再 poll**。broadcast trigger 後立即讀會撞到 TCP buffer 對齊 race window；先睡 150ms 讓 gateway 處理完 broadcast 後的 buffer 狀態，第一次讀才有機會對。

也取消連續失敗限制 — 物理 motion 完成前反覆讀失敗是正常的 frame alignment 問題，持續 retry 總會成功；只在 15s 全域 timeout 後才放棄，那才算真的死了。

### 預期 log
```
[init] feet pushers (ZDT 3,4,1,2) → extend 20000 pulses (~7 cm)
[wait ZDT:3] recovered after 3 comms fail(s) at 600ms
[wait ZDT:3] done at 900ms, pos=2250° (total comms fails=3)
[wait ZDT:4] done at 750ms, pos=2250° (total comms fails=2)
...
```

若還是 timeout，log 末尾的 `total comms fails=N` 讓我們知道是「真的沒動」還是「通訊整段爛」。

## 2026-04-24aa — Claude Code — [跨界: user_lib] zdt_wait_motion_done_ comms fail 容忍 3→10 次 + 恢復 log

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `zdt_wait_motion_done_`：
  - `COMMS_FAIL_MAX` 從 3 改 10（= 10 × 150ms = 1.5s 容忍度）
  - 成功讀取時若之前有失敗，印 `recovered after X comms fail(s) at Yms`

### 原因
實機 init 踩雷：
```
[init] feet pushers (ZDT 3,4,1,2) → extend 20000 pulses (~7 cm)
[wait ZDT:3] comms fail x3 at 300ms — giving up
```

但推桿物理上**有伸出來**。這是 memory `project_zdt_firmware_quirks.md` #3 典型症狀：trigger_sync_move 廣播後 TCP gateway buffer 對齊會亂一陣，幾次 read 後自動穩定。原本 3 次容忍太嚴，motion 才 300ms 就放棄（推桿動作總時間 ~500ms 都還沒結束）。

改 10 次 = 1.5s 容忍窗，motion 物理完成後如果讀還是失敗才真的放棄（那才是硬體/TCP 斷線）。加「recovered after N comms fail(s)」log 讓現場可看到 frame 對齊何時恢復。

## 2026-04-24z — Claude Code — [跨界: user_lib] zdt_wait_motion_done_ 加診斷 log + comms fail 容忍

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `zdt_wait_motion_done_`：
  - 每種退出印一行 `std::cout`：`done at Xms, pos=Y°` / `stall_flag set at Xms, pos=Y°` / `comms fail x3 at Xms — giving up` / `TIMEOUT after Yms, last pos=Z°, speed=W rpm`
  - `get_system_status()` 失敗不再立刻 return true；改容忍 2 次暫時錯誤（累積 3 次才放棄），期間 sleep + continue 下個 poll

### 原因
現場 init 卡在 feet pushers extend 後沒 log 可 debug。加進度輸出後可快速定位：每顆 slave 停止 / 失敗時間、位置、速度。Comms fail 容忍對應 memory `project_zdt_firmware_quirks.md` #3 frame alignment issue — TCP-gateway 連續指令後殘留 buffer 可能讓單次 read 失敗，retry 即正常。不容忍的話單次 hiccup 會讓整個 extend 誤 abort。

## 2026-04-24y — Claude Code — [跨界: user_lib] center ZDT 伸出距離改 ~10cm 對齊 body

### 修改檔案
- `user_lib/WASH_ROBOT.h:137` — `PUSHER_EXTEND_PULSE 144000 → 30000`（約 20cm → 10cm）

### 原因
Sadie 回報：中心推桿（ZDT slave 9）原本用全行程 20cm 伸出，應該跟 body 組一樣 10cm。三處用到的地方（`cmd_attach` / `do_step_down_` 中心 re-extend / `cmd_pusher` 中心或 fallback 路徑）共用同一個常數，改值後全部一致。

PUSHER_EXTEND_BODY_PULSE 維持 30000，兩者數值相等但保留分開常數，未來若兩者要分開調整只改對應常數即可。

## 2026-04-24x — Claude Code — [跨界: user_lib] 推桿等待改 speed/pos 穩定偵測（繞過 pos_reached 不可靠）

### 修改檔案
- `user_lib/WASH_ROBOT.h`: 宣告 private `zdt_wait_motion_done_(slave, timeout_ms=15000)`
- `user_lib/WASH_ROBOT.cpp`:
  - 新增 `zdt_wait_motion_done_` 實作：每 150ms `get_system_status()`，判「stall_flag 已 set」或「`|real_speed| ≤ 20 RPM` 連續 3 次」或「`|Δreal_pos| ≤ 0.15°` 連續 3 次」就算完成；15s timeout fail
  - `pusher_move_`：原本 `motion_control_pos_mode(sync=0)`（內建 wait_until_pos_reached）+ 再 `wait_until_pos_reached` 一次 → 改 `motion_control_pos_mode_nowait(sync=0)` + `zdt_wait_motion_done_`
  - `pusher_move_many_`：原本迴圈呼叫 `wait_until_pos_reached` → 改呼叫 `zdt_wait_motion_done_`

### 原因
用戶按 attach 後 log 顯示 `[WRN] [ZDT:9] Waiting for moving timeout` 但**現場確認 center pusher 物理有正常伸出去** → 典型 ZDT firmware quirk #1（memory `project_zdt_firmware_quirks.md`）：馬達實際已停但 `pos_reached` bit 不 set。cmd_attach 拿到 timeout 回 `ERR center_extend_fail` 給 web GUI（stdout 看不到）。Linux_test 早就用 speed/pos 穩定偵測繞過這個雷（menu 7 的 `zdt_group_move_sync`），主程式補上同 pattern。

穩定偵測三個 fallback（跟 memory 記錄完全對齊）：
1. `stall_flag`=1 → 算已停（雖然堵轉）
2. `|real_speed| ≤ 20 RPM` 連續 3 次 poll (~450ms) → 低速 = 停
3. `|Δreal_pos| ≤ 0.15°` 連續 3 次 → 幾乎沒動 = 停

驅動原 `wait_until_pos_reached` 不動（還有別人在用）；WASH_ROBOT 自己 wrap。

`[跨界: user_lib]` — WASH_ROBOT.{h,cpp} 內部實作 + 新增 private helper，沒動 public API。

### 影響
- cmd_init extend feet/body → 不會再假 timeout
- cmd_attach center extend → 不會再假 timeout
- cycle_group_ template（Phase A/B 推桿 extend / retract）→ 不會再假 timeout
- cmd_pusher 手動推桿 → 同上

## 2026-04-24w — Claude Code — [跨界: user_lib] cmd_init 加 progress std::cout

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_init` 每個關鍵步驟前加 `std::cout`：
  - `[init] PQW relays → pump ON, valves/water OFF`
  - `[init] DM2J wheels (slave 2, 4) → retract to 0`
  - `[init] ZDT 1-9 → release_stall + driver enable`
  - `[init] feet pushers (ZDT ...) → extend <N> pulses (~7 cm)`
  - `[init] body pushers (ZDT ...) → extend <N> pulses (~10 cm)`
  - `[init] DM2J arm (slave N) → set current as zero`
  - `[init] IMU → take baseline`
  - `[init] done → Ready`

### 原因
LOG_HEX 預設關掉後，ZDT release_stall / driver_EN / pos_mode 沒有其他 decoded log，使用者看不到 init 在跑什麼，以為程式卡住。`std::cout` 走正常 stdout 不受 log_utils 的 debug_mode / hex gate 控制，一定會印。格式「[init] ...」跟既有 `[OK] ...` / `[SETUP] ...` 風格對齊。

`[跨界: user_lib]` 但純加 log，不影響行為。

## 2026-04-24v — Claude Code — [跨界: user_lib] LOG_HEX 預設關（TX/RX hex dump 不再洗版）

### 修改檔案
- `user_lib/log_utils.h`:
  - 加 `#include <cstdlib>`（getenv 用）
  - 加 `namespace user_lib_log::hex_log_enabled()` — lazy-init 從 env `USER_LIB_HEX_LOG` 讀取，預設 false
  - `LOG_HEX` macro 多加一道 gate：`if (debug_mode && hex_log_enabled())`，兩個同時 true 才印
  - 文件頂部註解補說明 hex dump 預設 off、如何打開

### 原因
用戶要求把「tx、rx 那種 log」不要顯示。driver LOG_HEX 印出 Modbus frame hex dump，25+ 個 device 併發時會把 stdout 洗掉，decoded status / PR completion 這些有用訊息被沖走。其他 LOG_DBG/INF/WRN/ERR（例如 `status=0x00000072 [ENABLE] [CMD_DONE]` / `PR motion completed`）不受影響，仍由 `debug_mode` 控制。

### 如何重新打開（做低階診斷時）
Linux: `USER_LIB_HEX_LOG=1 ./washrobot_new_PI`
Windows: `set USER_LIB_HEX_LOG=1 & washrobot_new_PI.exe`

Lazy 讀取：env 只在第一次 LOG_HEX 被呼叫時讀一次，之後都用 cached value。啟動前設定即可，runtime 改 env 無效。

`[跨界: user_lib]` 但純加法（新 gate + 註解），沒動現有 LOG_ERR/WRN/INF/DBG 行為。

## 2026-04-24u — Claude Code — [跨界: user_lib] pusher_move_many_ 改用 _nowait 配 sync=1 pattern

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `pusher_move_many_`: 內部迴圈呼叫從 `motion_control_pos_mode(..., sync=1, ...)` 改成 `motion_control_pos_mode_nowait(..., sync=1, ...)`

### 原因
修完 ZDT enable 後 (s) init 能送出 Pos Mode，log 顯示：
```
[ZDT:3] TX Pos Mode ... sync=01 ...
[ZDT:3] RX Pos Mode 03 10 00 FD 00 05 90 18       ← echo 正常
[ZDT:3] TX get_status ...
[WRN] [ZDT:3] Waiting for moving timeout          ← 卡住
```

原因：ZDT firmware 對 `sync=1` 的 pos mode 指令**只 queue 不執行**，要等 `trigger_sync_move()` 廣播才同時啟動所有 queued slaves。但 `motion_control_pos_mode`（無 nowait 後綴）**內建 wait_until_pos_reached**：
1. slave N queue 成功（sync=1 → 不動）
2. 同函式內立即 poll pos_reached → 永遠不 set（馬達還沒動）
3. timeout → return true

結果：迴圈第一個 slave 就 timeout 退出，`trigger_sync_move()` 都沒送就失敗。

Memory `project_zdt_firmware_quirks.md` #4 有記錄正確 pattern。`motion_control_pos_mode_nowait` 送完 echo 檢查就 return 不 wait，配合迴圈外單次 broadcast + 迴圈外 per-slave `wait_until_pos_reached` 才對。單 slave 版 `pusher_move_`（sync=0 立即執行）不改。

### 驗證順序
按 init → DM2J 輪子回零 → ZDT 1-9 enable → ZDT 1-8 同步 extend (feet 7cm / body 10cm) → DM2J 5 set zero → IMU baseline → Ready。

## 2026-04-24t — Claude Code — main.cpp 加 `--no-debug` CLI flag（VS 可靠注入）

### 修改檔案
- `washrobot_new_PI/main.cpp`:
  - `main()` 加 `argc, argv` 參數
  - `#include <cstdlib>`（setenv / _putenv_s）
  - 解析 argv：`--no-debug` → 把 `WR_DRIVER_DEBUG=0` 注入環境（WashRobot::init() 的 env var 讀取邏輯接手）
  - 印 `[CLI] --no-debug → WR_DRIVER_DEBUG=0` 確認
  - 未知 flag 印 warning 不中止

### 原因
VS Linux remote debug 的 Environment 欄位不見得可靠，Program Arguments 最穩 — argv 保證 pass 進 binary。Sadie 之前嘗試把 `--no-debug` 放 Linker AdditionalOptions 是錯位置（給 ld，不進 argv）。現在改用 CLI flag：
- VS 專案 Properties → **Debugging → Program Arguments** 填 `--no-debug`
- 啟動 log 確認：`[CLI] --no-debug → WR_DRIVER_DEBUG=0` + `[OK] driver debug = OFF`
- 之前放錯的 linker AdditionalOptions `--no-debug` 要拿掉

## 2026-04-24s — Claude Code — [跨界: user_lib] cmd_init 補 ZDT release_stall + enable（修 exception 0x03）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_init`：DM2J 輪子回零後、ZDT 推桿 extend 前，加 loop 對 ZDT slave 1-9 做 `release_stall_flag()` + `motion_control_driver_EN(true)`；enable 失敗 return `ERR zdt_enable_fail slave=N`；sleep 200ms 給 drive 進穩定狀態。

### 原因
現場測試按「init」後 DM2J 輪子歸零成功（Bug 1 修完後驗證 status decode 正確），接著 ZDT slave 3（左腳第一顆推桿）送 `motion_control_pos_mode` 收到 Modbus exception：`03 90 03 AD C1`（function 0x10 + error bit，exception code 3 "Illegal data value"）。對照 Linux_test menu 3 (`test_zdt`) 發現它每次先做 `release_stall_flag()` + `motion_control_driver_EN(true)` + 200ms 才送 pos_mode。WASH_ROBOT::init() 只連 TCP 不 enable，加上 `cmd_shutdown` / `cmd_emergency_stop` 會 disable ZDT，所以 cmd_init 必須自己負責重新 enable。

`[跨界: user_lib]`：改 WASH_ROBOT.cpp `cmd_init` 內部實作，API 簽名不變。

### 驗證
實機上次 init log 卡在 ZDT:3 retry 兩次都 exception。修完後 ZDT 1-9 都會先 enable，pos_mode 應該能正常執行 7cm/10cm 推桿 extend。

### 下一步
按 init 看能不能走到「extend feet 7cm」→「extend body 10cm」→「slave 5 set zero」→「imu baseline」→ state Ready。若仍失敗再看 log。Bug 2 / Bug 3 繼續排程。

## 2026-04-24r — Claude Code — [跨界: user_lib] 主程式推桿深度依 group 拆分 (feet 7cm / body 10cm)

### 修改檔案
- `user_lib/WASH_ROBOT.h`
  - 新增常數 `PUSHER_EXTEND_FEET_PULSE = 20000`（~7 cm，實測值對齊 Linux_test）
  - 新增常數 `PUSHER_EXTEND_BODY_PULSE = 30000`（~10 cm）
  - 保留 `PUSHER_EXTEND_PULSE = 144000`（full stroke，給 center 和 fallback 用）
  - `cycle_group_` template extend 區塊：依 group ("feet"/"body") 選對應 pulse，其他 fallback 到全行程
- `user_lib/WASH_ROBOT.cpp`
  - `cmd_init`：不再把 8 顆一起伸 full stroke；改成 feet(1,2,3,4) 伸 7cm、body(5,6,7,8) 伸 10cm 兩次分開呼叫
  - `cmd_pusher`：`extend` 依 group 選對應 pulse；`pusher all extend` 拆成 feet+body+center 三次分開呼叫（各自 depth）；`retract` 邏輯不變
  - `cmd_attach`（center extend）/ `do_step_down_` 的 center re-extend — 保留 PUSHER_EXTEND_PULSE（用戶未指定 center 深度）

### 原因
用戶要求主程式走路時腳組推桿全伸 7cm、身體組全伸 10cm（跟 Linux_test 實測對齊，避免吸盤壓力過強或推桿負載過大）。原本所有 `PUSHER_EXTEND_PULSE` 一視同仁 = 144000 pulse (~20cm) 壓到底，對真空 seal / 推桿 / 機構剛性都不友善。實作對齊 Linux_test menu 7/8 pattern。

**`[跨界: user_lib]`**：改動 WASH_ROBOT.{h,cpp} public 常數 + cmd_init / cmd_pusher 實作。API 簽名未改（constants 新增、沒刪舊的），但 runtime 行為對 web GUI 的 "init" / "pusher all extend" / "pusher feet extend" 按鈕結果有**直接影響**（推桿不再伸到底）。

### 下一步
繼續修 Bug 2 (`PR_trigger_sync` broadcast safety on slaves 2/4/5) 或 Bug 3 (`feet_backup`/`body_backup` mode=0)。

## 2026-04-24q — Claude Code — [跨界: user_lib] DM2J read_status 改讀 1 register + HOME_DONE mask

### 修改檔案
- `user_lib/DM2J_RS570.cpp`
  - `read_status()`：Modbus 請求從「read 2 registers」改「read 1 register」（`tx[5] = 0x01`），回應長度檢查從 9 改 7，組裝從 `(hi<<16)|lo` 改 `rx[3]<<8 | rx[4]`。16-bit 值落在 uint32_t 低 16 位
  - `print_status()`：HOME_DONE mask 從 `0x10000` → `0x0040`（bit 6，對照手冊 §5.3.2）
  - 函式頂部加詳細註解說明修改原因
- `Linux_test/main.cpp` menu 14 (`test_dm2j_monitor`)：HOME_DONE mask 從 `0x10000` → `0x0040`

### 原因
**`[跨界: user_lib]` 改動**。參考：`.claude/summaries/DM2J_RS_MODBUS_SUMMARY.md` 「Known Driver Bugs」#1 + `memory/project_dm2j_firmware_truth.md`。

DM2J-RS V1.0 手冊 §5.3.2 明確寫 0x1003 是單一 16-bit register，bit 0-6 為 FAULT/ENABLE/RUN/(unused)/CMD_DONE/PATH_DONE/HOME_DONE。但 driver 之前讀 2 個 register 組成 32-bit、把真實 bits 推到高 16 位，再用 0x0010/0x0020 低 16 位查 → 永遠抓不到完工 → 所有 `PR_move_cm` 內部 poll + 所有 `dm2j_wait_done_` 呼叫 timeout。實測 menu 7 先靠位置穩定偵測 workaround；WASH_ROBOT 的 `do_step_down_` 目前還 100% 跑不起來就是這個 bug。

改完後連帶受益：
- `DM2J_RS570::PR_move_cm` / `PR_move_cm_trigger_all` 內部 poll → 自動變對
- `WASH_ROBOT::dm2j_wait_done_`（line 163）→ 自動變對，主程式 do_step_down_ 具備跑起來的前提
- `Linux_test` menu 16 (`test_dm2j_group_sync`) 的 status 查詢 → 自動變對
- `windows_test` 呼叫 `print_status` → 自動變對

`dm2j_pair_poll_done`（Linux_test menu 7 用的位置穩定偵測）**不受影響**也**無需改動** — 它刻意不查 bit，留著當雙保險。

### 下一步
繼續修 Bug 2 (`PR_trigger_sync` broadcast safety) 或 Bug 3 (`feet_backup`/`body_backup` mode=0)。

## 2026-04-24p — Claude Code — 驅動 debug 改 env var WR_DRIVER_DEBUG 控制 [跨界: user_lib]

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `init()`:
  - 加 `#include <cstdlib>` (std::getenv)
  - 開頭讀 env var `WR_DRIVER_DEBUG`，預設 `true`（對齊 [TEST MODE]）；若值為 `"0"` 則 `false`
  - 所有 driver init（DM2J×5 / ZDT×9 / JC-100×9 / PQW / IMU）的 debug 參數從寫死 `true` 改為 `dbg`
  - 印 `[OK] driver debug = ON|OFF (override via WR_DRIVER_DEBUG=0|1)` 讓使用者一眼看當下模式

### 原因
Sadie 回報：VS remote debug washrobot 會出現 "Broken pipe"（Pi process 沒死，是 VS↔gdbserver stdout pipe 被 25 顆裝置的 Modbus hex dump 淹爆）。直接在 Pi 跑 binary 沒這問題。

修法：保留 TEST MODE 預設 debug=true（現場 troubleshoot 完整 log），但加 env var 讓 VS debug 時透過 Properties → Debugging → Environment 設 `WR_DRIVER_DEBUG=0` 關掉 hex dump。主 crane 上線要撤 TEST MODE 時只改這裡的預設 `true → false`，env var 機制留著。

### 使用
- **VS remote debug**：專案 Properties → Debugging → Environment 加 `WR_DRIVER_DEBUG=0`
- **現場直接跑**：`./washrobot_new_PI` 預設 debug=true
- **強制關**：`WR_DRIVER_DEBUG=0 ./washrobot_new_PI`

### 規範邊界
`user_lib/WASH_ROBOT.cpp` 屬 Jim 範圍，標 `[跨界: user_lib]`。原來的 `true` 寫死本身就是 Sadie 04-21c 加的 TEST MODE，同性質改動。

## 2026-04-24o — Claude Code — 選項 7 rail move 改廣播同步觸發（1+3 硬體同步）

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 `dm2j_pair_rail_move_sync()` / `dm2j_pair_rail_move_abs_sync()` — 先 set PR on 左右腳（slave 1,3），再 `PR_trigger_sync(pr_num)` 廣播觸發。零 TCP 序列化 skew，硬體級同步。
  - 新增 `dm2j_set_safe_pr(slaves, pr_num)` helper — 把指定 slaves 的 PR<pr_num> 設 rpm=0（safe no-op）
  - `test_full_step()` 開頭 init 輪組（slave 2,4）+ 上滑台（slave 5）當 bystanders，用 `dm2j_set_safe_pr` 把它們 PR1 設 rpm=0。廣播時這三顆執行 rpm=0 → 不動作
  - `test_full_step()` 內 4 個 rail move 呼叫都換成 `_sync` 版本（retry back-off / phase A / phase B / cleanup return-to-0）
  - 舊 `dm2j_pair_rail_move()` / `_abs()` 保留不動，仍被 `test_full_step_report()`（選項 12）使用（沒 bystander 設置，繼續用個別 trigger 避免誤觸發）

### 原因
Sadie 要求：腳組 slave 1,3 機構剛性連接必須同步移動（ms-skew 會扭壞）。原實作用個別 `PR_trigger`，兩顆相差 5-20ms。改成 `PR_trigger_sync` 廣播需要保護其他非目標 slave：用 PR1 劃分 + bystanders PR1 set rpm=0，確保輪組 + arm 不會被廣播誤觸發。

## 2026-04-24n — Claude Code — 步伐補償寫入 motion_flow 規範 (§4.E)

### 修改檔案
- `.claude/motion_flow.md` — Phase 4 下新增 §4.E「步伐補償規則」：
  - Invariant：每 step 結束 rail 回 `rail_baseline`
  - 動態 target 公式：`phase_a_target = STEP_CM - offset`、`phase_b_target = baseline - rail_after_A`
  - 動態 retry skip：每 retry 先 check cumulative < phase_target 才執行，否則跳過
  - 兩個數值範例（Phase B retry compensation、retry skip）
  - 關鍵性質四點 + 呼叫端 `rail_baseline` 記錄時機
- `memory/project_step_compensation.md` — 依 motion_flow §4.E 更新，加 retry skip 細節跟數值範例、強調「以後所有 inchworm 流程皆套此邏輯」

### 原因
用戶要求「以後統一用這種方式走路」。之前 memory 只描述基本邏輯，現在把完整規範（含 retry skip）寫進 motion_flow 當**權威文件**給所有協作者（Jim、Sadie、未來協作者、所有 Claude session），不只留在我的 personal memory。日後 `WASH_ROBOT.cpp` 主程式實作 Phase 4 時直接依 motion_flow §4.E 走。

## 2026-04-24m — Claude Code — menu 7 retry 改為動態 skip（取代 pre-clamp）

### 修改檔案
- `Linux_test/main.cpp` `test_full_step()`:
  - 移除 pre-clamp `rail_backup_cm = step_cm / retry_cnt`，改成只印 WARN
  - `retry_grip_rail` 簽名加 `max_total_backoff_cm` 參數
  - retry 迴圈每輪先 check `cumulative_backoff + per_backoff > max_total_backoff_cm`，超過就印 `[RETRY n] skipped` + return false
  - retry log 加上 `cum X/Y` 顯示累積進度
  - Feet 呼叫端傳 `phase_a_target`（這 step 腳組實際要走的距離）
  - Body 呼叫端傳 `std::fabs(phase_b_target)`（這 step 身體要走的距離）

### 原因
使用者要求：用戶設定 `rail_backup=2cm × retry=3` 但 `step=4cm` 時，不要預先改小 rail_backup，改成動態執行 retry 1（-2cm）+ retry 2（-2cm）到達原位後，retry 3 不執行。這樣 retry 幅度可以維持使用者意圖，超過 phase 自己走的範圍時才停。Limit 用 `phase_a_target / |phase_b_target|` 而非 step_cm，配合步伐補償語意：每輪 retry 最多只能回到這 phase 自己的起點，不會 dig 到上一 step 的累積 offset。

## 2026-04-24l — Claude Code — WebSocket 層 heartbeat + tab visibility 強制重連

### 修改檔案
- `web_backend/server.js` —
  - 新常數 `WS_PING_INTERVAL_MS = 30000`
  - `wss.on('connection')` 裡加 `ws.isAlive = true` + `on('pong')` 更新
  - 新 `setInterval` 每 30s 對每個 ws client 送 `ws.ping()`，上輪沒 pong 的 client `terminate()`
- `web_backend/public/app.js` —
  - 新 `_onVisibilityChange()` handler：tab 變 visible 時若 ws 是 CLOSED/CLOSING 就立刻 `connectWs()` 不等 2s timer
  - `connectWs()` 內加 `document.addEventListener('visibilitychange', ...)` 註冊

### 原因
Sadie 回報：web + washrobot 放著 idle 一陣子後無法從 web 控制。診斷：三層連線只有「browser ↔ backend WS」沒有 heartbeat 保護（backend ↔ 3 個 TCP bridge 已有 setKeepAlive + 10s ping）。browser tab 背景化時 setInterval 被 throttle / 凍結，WS 長期閒置可能被 NAT/middlebox 偷殺。加 WS 層 ping/pong + tab visibility 回前景立即重連。

## 2026-04-24k — Claude Code — menu 7 步伐補償邏輯（rail 閉回 baseline）

### 修改檔案
- `Linux_test/main.cpp` `test_full_step()`：
  - 初始化 (step loop 之前)：加 `rail_baseline_L/R` 讀取 + log
  - Step 開頭：讀 `rail_cur`、算 `offset = rail_cur - rail_baseline`、算 `phase_a_target = step_cm - offset`，log 印出 offset 跟 target；若 target ≤ 0 直接 abort
  - Phase A 軌道 `rail +step_cm` 改成 `rail +phase_a_target`
  - Phase A 結束後讀 `rail_after_A`，`phase_b_target = rail_baseline - rail_after_A`（關到 baseline，不是只反 feet_delta）
  - Phase B 軌道從 `-feet_delta` 改成 `phase_b_target`
  - 保留 feet_delta 的 log 作為觀察用
- `memory/project_step_compensation.md` — 新增，紀錄「步伐補償」專案用語對應此邏輯（含 Step 1/2/3 數值範例）
- `memory/MEMORY.md` — 加 index

### 原因
用戶要求：Phase B retry 若導致 body 沒完全追上（rail 留下 offset），下一 step 的 Phase A 要減小 `step_cm - offset`，Phase B 要額外走把 offset 關回 baseline。Invariant：rail 每步結束一定回到 baseline。用戶定義這整套為「步伐補償」，未來聽到這詞直接套這邏輯，不再重新討論。對比上一版 j（Phase B = -feet_delta 只 match feet）差別：j 讓 rail offset 持續累積，k 每步關回 0。

## 2026-04-24j — Claude Code — menu 7 Phase B 用實際 feet delta 而非固定 step_cm

### 修改檔案
- `Linux_test/main.cpp` `test_full_step()` step loop：
  - Phase A 「收腳推桿」之後加一步 `read_position_cm(L/R)` 記 `rail_before_{L,R}`
  - Phase A verify + retry 結束之後加 `read_position_cm(L/R)` 記 `rail_after_{L,R}`
  - 計算 `feet_delta = avg(L, R)`，log 印出 L/R 各自差值
  - Phase B 的 rail 後退距離從固定 `-step_cm` 改成 `-feet_delta`

### 原因
Phase A 的 retry_grip_rail 會讓腳組軌道倒退（-rail_backup_cm 每次），導致實際前進距離 < step_cm。原本 Phase B 固定走 `-step_cm` 會讓身體走超過腳組實際進度，下一 step 的 baseline 就不對。改讀實際位置差取 L/R 平均，Phase B 走同樣絕對量反向，確保一個 step 結束時鐵軌回到 Phase A 起始位置（即腳+身體都淨前進同樣距離）。

## 2026-04-24i — Claude Code — 把 DM2J 手冊真相記錄起來（summary / work_log / memory）

### 修改檔案
- `.claude/summaries/DM2J_RS_MODBUS_SUMMARY.md` — 依 `doc/DM2J-RS.V1.pdf` V1.0 實機手冊**整篇重寫**：
  - Status register 0x1003 改回單 16-bit（bit 0-6），移除「Bit16=HOME_DONE」誤解
  - 0x1801 控制字表全改對：0x1111=復位當前報警、0x2211=save EEPROM、0x2233=恢復出廠值 等
  - 新加「Enable 機制」章節：DI1 NC auto-enable + Pr0.07 (0x000F) 軟體強制 enable
  - PR Mode 欄位加警告：實際 `1=absolute, 2=relative`，舊 driver 註解顛倒；`mode=0` 是「路徑未配置」不會動
  - 文末新加「Known Driver Bugs」清單列出 user_lib/DM2J_RS570 待修的四類 bug
- `.claude/work_log.md` — 頂部加 2026-04-24 session 條目：發現緣起、核心真相、過去 log 重解讀、user_lib 待修項、workaround 說明、影響範圍
- `memory/project_dm2j_firmware_truth.md` — 新增 memory 條目（對齊 ZDT firmware quirks 那個的格式）
- `memory/MEMORY.md` — 加 index 條目

### 原因
Sadie 要求在動手修 user_lib 前先把發現記錄起來，確保其他協作者 session 能直接看到權威文件（summary + work_log），未來我的 Claude session 也會從 memory 直接看到 → 不會再花一輪 debug 撞同一個雷。程式碼本身未動。

## 2026-04-24h — Claude Code — 選項 7 retry 軌道退縮距離改使用者可調 + 位置下限保護

### 修改檔案
- `Linux_test/main.cpp` `test_full_step()`:
  - 新增 prompt `"Retry rail back-off cm (DM2J retract per retry) [5]: "`
  - 移除 `static constexpr double RAIL_BACKUP_CM`，改為 local `double rail_backup_cm = 5.0`
  - 加 clamp：`retry_cnt × rail_backup_cm ≤ step_cm`（否則軌道會退縮超過 step 前原本位置）超過會 `[WARN]` 印出並降到 `step_cm / retry_cnt`
  - body 退縮的 live call（行 1284 `+RAIL_BACKUP_CM` → `+rail_backup_cm`）改用新變數
  - feet retry 的註解（行 1248-1250）也改用 `rail_backup_cm`，未來 uncomment 可用
  - lambda `retry_grip_rail` 透過 `[&]` capture 取用新變數

### 原因
Sadie 要求：
1. 錯誤重試時 DM2J 軌道縮回距離可調（預設 5cm）
2. 總縮回量（`N × backup`）不能小於原本位置 → 加 clamp

現場不同吸盤位置 / 牆面條件需要調大/調小 back-off；clamp 避免軌道退過 step 起點造成機構異常。

## 2026-04-24g — Claude Code — DM2J 完工判斷改位置穩定偵測 + menu 7 cleanup 歸 0

### 修改檔案
- `Linux_test/main.cpp`：
  - 頂部加 `#include <cmath>`（std::fabs 用）
  - 新增 `dm2j_pair_poll_done(left, right, left_target, right_target)` — 位置穩定偵測 helper：每 150ms 讀兩顆位置，連續 3 次 `|Δpos| < 0.01cm` 且 `|pos - target| < 0.5cm` 就算完成，15s timeout
  - 新增 `dm2j_pair_rail_move_abs(left, right, pr_num, target_cm)` — 兩顆同時走絕對目標（cleanup 歸 0 用）
  - `dm2j_pair_rail_move()` 改用 `dm2j_pair_poll_done` 做完工判斷（原本查 `CMD_DONE/PATH_DONE` bit 在這 firmware 永遠讀不到 → timeout）
  - `test_full_step()` cleanup 在收完推桿之後、關 PQW 之前加一步 `dm2j_pair_rail_move_abs(..., 0.0)` 讓 rail 回絕對 0 位置；失敗只印 WARN 不中止

### 原因
實測 DM2J rail 確實用 mode=1 絕對模式走到目標，但 status register 一直回 `0x00320000`。Driver 依 summary 猜的 `CMD_DONE=bit4 / PATH_DONE=bit5` 查 LOW word，實際 firmware 的 bit 位置不同 → 永遠找不到完成指示 → timeout `[ABORT]`。改用位置穩定偵測（跟 ZDT firmware quirks memory 驗證過的 pattern 一樣），不依賴 bit layout 推論，穩定性優先。使用者另要求結束後 rail 歸 0，用新的 `move_abs` helper 實作。

## 2026-04-24f — Claude Code — Linux_test 加本地 DM2J motor_enable/disable helper（避開 user_lib 未實作）

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 `_dm2j_crc16()` Modbus CRC16 helper
  - 新增 `dm2j_write_0x1801(cli, slave, value)` — 寫 0x1801 register 的 generic helper
  - 新增 `dm2j_manual_enable()` / `dm2j_manual_disable()` — 寫 0x1111 / 0x2233
  - `test_dm2j_group_sync()` 改用這些本地 helper，不呼叫 `drv.motor_enable()`（避免 link 錯誤）

### 原因
`DM2J_RS570::motor_enable()` / `motor_disable()` 在 header 宣告但 `.cpp` 沒實作 → link 報 `undefined reference`。這是 user_lib 範圍（Jim）的事，正式要補進 `DM2J_RS570.cpp`。為了今天能測，Linux_test 自己送 Modbus function 0x06 frame 寫 0x1801 register。日後 Jim 補完可以移除本地 helper。

### 需要 mailbox
建議往 `.claude/mailbox.md` 發訊息給 Jim：DM2J motor_enable/disable 實作缺失。

## 2026-04-24e — Claude Code — Linux_test 新增選項 16「DM2J group sync move」

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 `test_dm2j_group_sync()`：硬體同步移動 {腳組 1,3} 或 {輪組 2,4}
    - 啟動時初始化所有 5 顆 DM2J 的 PR1、PR2 為 rpm=0 safe state
    - 使用者選 f/w 群組 → 目標 cm → RPM
    - 目標 slave `motor_enable` + 刷新對應 PR slot（feet 用 PR1、wheels 用 PR2）
    - 廣播 `PR_trigger_sync(pr_slot)` — 非目標 slave 執行 rpm=0 PR → 不動
    - wait 兩顆 target 到位（10s timeout）
    - 動完後把該 PR slot 重置回 safe，避免下次廣播殘留
    - 退出時 reset 所有 PR + disable 所有馬達
  - 選單加第 16 項

### 原因
Sadie 回報 slave 1,3 跟 2,4 機構剛性連接，必須硬體同步（ms 級差距會扭壞機構）。個別 `PR_trigger` 有 TCP round-trip 延遲不夠同步。用 `PR_trigger_sync` 廣播 + PR slot 劃分（PR1 for 1,3 / PR2 for 2,4）+ safe-state 初始化，非目標 slave 執行 rpm=0 不動作，實現真硬體同步。

## 2026-04-24d — Claude Code — menu 7 cleanup 最後加 controlAll(false)

### 修改檔案
- `Linux_test/main.cpp` — `test_full_step()` cleanup 尾段：
  - 舊：`controlRelay(PQW_CH_PUMP, false)` 只關 CH1
  - 新：`controlAll(false)` 一次關 CH1~8（包含 CH5 刷子 / CH6 水泵 / CH7 水閥 / CH8 保留）

### 原因
使用者要求結束後所有繼電器都關掉。原本只關 pump，CH5-8 如果之前被其他流程意外打開會留在 ON 狀態。現在用 controlAll 一次關完，不論哪個 channel 有沒有被用到都保證結束狀態乾淨。Valves（CH2/3/4）仍然在 controlAll 之前個別關並 settle 300ms，維持「先放真空再收推桿」的安全順序。

## 2026-04-24c — Claude Code — dm2j_pair_rail_move 改用 mode=1 絕對模式

### 修改檔案
- `Linux_test/main.cpp` — `dm2j_pair_rail_move()`：
  - 動作前先 `read_position_cm()` 讀左右當前位置，讀失敗就 abort
  - 計算 `target = current + cm`（保留呼叫端「相對位移」語意）
  - `PR_move_cm_set` mode 從 `0` 改 `1`（絕對），改送絕對 target
  - log 印出「left X → Y cm / right X → Y cm」方便對照

### 原因
實機測試發現改個別觸發後 status 仍停在 `0x00320000` timeout。比對 menu 2 發現差異：menu 2 用 mode=1（絕對）**會動**，menu 7 原本 mode=0（summary 寫是「相對」）**不動**。推測此 DM2J firmware 的 mode=0 是「PR 未配置 / 跳過」不是「相對」，所以 drive 收到 trigger 但不進入運動狀態、ENABLE 維持 0。改用已驗證的絕對模式 + 讀當前位置計算 target，可靠性優先。代價：每次 rail move 多 ~20ms 讀位置，影響可忽略。

## 2026-04-24b — Claude Code — menu 7 cleanup 改成完整歸位（對齊 menu 8）

### 修改檔案
- `Linux_test/main.cpp` — `test_full_step()` 結尾 cleanup 流程：
  - 舊：只 disable ZDT driver，valves/pump 保留
  - 新：依序 (1) 關 CH2/3/4 所有 valve → settle 300ms (2) 收 feet 推桿 到 0 (3) 收 body 推桿 到 0 (4) pump OFF (5) disable ZDT driver

### 原因
使用者要求 menu 7 結束後所有設備歸位（對齊 menu 8 行為）。舊版為「機器人可能還吸在牆上」的安全考量保留 valves + pump；但既然 menu 7 現在已有 initial attach（機器人從非吸附狀態開始），結束時也應該回到非吸附狀態，流程才閉環。DM2J 沒 motor_disable 實作（header 宣告未落地），仍保持通電不動。

## 2026-04-24a — Claude Code — dm2j_pair_rail_move 改個別觸發 + menu 7 檢查 return

### 修改檔案
- `Linux_test/main.cpp` — `dm2j_pair_rail_move()`（line 918）從 broadcast 改個別觸發：
  - 移除 `PR_move_cm_trigger_all`（內部走 `writeSingle_sync` slave 0 broadcast）
  - 改用 `left.PR_trigger(pr_num)` + `right.PR_trigger(pr_num)` 兩次個別 writeSingle
  - 新 poll 迴圈同時監視兩顆（up to 10s），任一 fault 或 timeout 即 return true
- `test_full_step()` 三個 rail 呼叫點都檢查 return value：
  - Phase A 腳組軌道前進：fail → cerr `[ABORT] feet rail move failed` → break step loop
  - Phase B 身體軌道後退：fail → cerr `[ABORT] body rail move failed` → break
  - `retry_grip_rail` 內部 rail backup：fail → cerr `[ABORT RETRY] rail backup failed` → return false（放棄整個 retry，因為下一次 retry 位置不會變）

### 原因
實機測試發現 menu 2（個別 `PR_trigger` to slave 2/3/5）可動，menu 7（`PR_trigger_sync` broadcast slave 0）不動，DM2J status 停在 ENABLE=0 重複讀取到 timeout。原因：USR-TCP232-304 gateway 對 Modbus broadcast 不可靠（跟 memory 裡 ZDT firmware quirk 類似），加上 broadcast 也會觸發 slave 2/4/5（輪子 + 上滑台）造成誤動作 — 不是正確工具。改個別觸發後左右起步差 ~5-20ms（DM2J_RPM=500, 10mm pitch → ~0.4-1mm 位置差），犧牲少許同步精度換正確性 + 安全性。呼叫端從「忽略 return」改「fail 就 abort step」避免矽默失敗繼續跑下面推桿流程造成誤判。

## 2026-04-24 — Claude Code — menu 7 加 initial attach + user gate

### 修改檔案
- `Linux_test/main.cpp` — `test_full_step()` 新增 initial attach 區塊（抄 menu 8 的 pattern）：
  - Pre-flight 訊息從「must already be attached」改為「will perform initial attach」
  - Pump ON 後先伸 feet 7cm + body 10cm staged（valves 全關），讀 JC-100 1..8 報告（預期 ≈0）
  - User gate：Enter 確認後開 CH2+CH3，settle 2000ms 再讀一次
  - Step 迴圈加 `!user_aborted` 條件，使用者在 gate 按 q 就跳過 step 進 cleanup
  - Center 組（ZDT slave 9 + PQW CH4）依使用者指示**整組跳過**，menu 7 initial 不開第三口

### 原因
Menu 7 原本假設「機器人已吸附在牆上」不做 initial attach，但現場測試需要從牆邊完全釋放狀態開始執行。對齊 menu 8 做法：先伸推桿看吸盤定位、閥關、讀壓力驗證 cup 無貼合、Enter 後才開閥入 step 迴圈。

## 2026-04-23zzzz — Claude Code — menu 7 改 staged 7/10cm extend

### 修改檔案
- `Linux_test/main.cpp` — `test_full_step()` 三處 extend 全部改用 `zdt_group_extend_staged()`：
  - Phase A Feet：`PUSHER_EXTEND_PULSE` (144000 全行程) → `PUSHER_EXTEND_FEET_PULSE` (20000 ~7cm) staged
  - Phase B Body：`PUSHER_EXTEND_PULSE` → `PUSHER_EXTEND_BODY_PULSE` (30000 ~10cm) staged
  - `retry_grip_rail` 內 re-extend：依 `zdt_group.front() <= 4` 判斷 feet/body 選 target，統一改 staged（抄 menu 8 retry_grip 的 pattern）
- Menu 顯示字串 + section header + 函式標題同步改成「8 pushers staged 7/10cm」

### 原因
使用者要求 menu 7 與 menu 8/11/12 對齊：腳組只伸 ~7cm、身體組 ~10cm（不再全行程 200mm），且分兩階段 (half → full) 避免一次伸到底對吸盤 seal 不利。helper `zdt_group_extend_staged()` + 兩個 pulse 常數早就存在，只是 menu 7 沒用到。

## 2026-04-23zzz — Claude Code — Linux_test 新增 menu 15「DM2J zero position」

### 修改檔案
- `Linux_test/main.cpp` — 新增 `test_dm2j_zero()`；menu 顯示 + dispatch 加 15

### 原因
Debug 過程需要把 DM2J 目前位置當零點重置（呼叫 `home_set_current_pos_zero()`，寫 0x6002 = 0x0021）。會改寫座標原點屬於破壞性操作，加一層 `type 'yes' to confirm` 防手滑；動作前後各讀一次位置顯示給使用者對照，200ms 等驅動器 latch 新零點。

## 2026-04-23zz — Claude Code — Linux_test 新增 menu 14「DM2J live monitor」

### 修改檔案
- `Linux_test/main.cpp` — 新增 `test_dm2j_monitor()`；menu 顯示 + dispatch 加 14

### 原因
DM2J 5 顆 slave 都不動，status bit 顯示 `[ENABLE]` 未 set，使用者需要在調面板 P00.03 / 量 48V 動力電源時能即時觀察位置 + status bits 是否變化（ENABLE / RUN / FAULT / HOME_DONE）。新選項純監視、不動馬達；驅動 init 時刻意 `debug=false` 避免 TX/RX hex dump 洗版，讓一行 live display 清楚可讀。每 200ms 更新。

## 2026-04-23z — Claude Code — Linux_test 新增 menu 13「Water tank」

### 修改檔案
- `Linux_test/main.cpp` — 新增 `test_water_tank()` + helper `water_wait_or_abort()`；menu 顯示 + dispatch 加 13；頂部 Windows include 區加 `<conio.h>` 供 `_kbhit()/_getch()` 用（abort 檢查跨平台）

### 原因
使用者要測試水箱（PQW CH5 刷子 / CH6 水泵 / CH7 進水閥）。現有 menu 5 雖能操作 PQW 但範圍是 8 channel 易誤觸，且無計時腳本 — 進水閥忘了關會淹水。新選項限定 CH5/6/7、腳本模式有秒數上限（valve ≤120s / wash ≤300s / gap ≤60s）+ Enter 中止 + 離開自動全關。包含 4 子模式：手動 toggle / 補水 / 刷洗 / 完整循環（補水→閒置→刷洗）。

## 2026-04-23y — Claude Code — Session 總結歸檔

### 修改檔案
- `.claude/work_log.md` — 頂部加 2026-04-23 Session 總結條目：Linux_test 12 option 現況、slave ID 映射、TEST MODE 清單、硬體校準值、9 個 debug 發現、未解問題、未 commit 狀態、規範邊界
- `memory/project_zdt_firmware_quirks.md` — 新增：ZDT 韌體 4 個 quirk + workaround（pos_reached / broadcast echo / frame 對齊 / sync pattern）
- `memory/project_vacuum_seal_patterns.md` — 新增：valve-before-extend + staged extend 兩個 pattern + 實測參數
- `memory/MEMORY.md` — 加兩個 index 條目

### 原因
Sadie 要清空 session 前整理。Session 內 changelog 已有 ~45 筆細節條目，但 work_log 只有幾筆、memory 未更新。補上：(a) session-level 軌跡總結進 work_log (b) durable 技術洞見進 memory。

## 2026-04-23x — Claude Code — 加選項 12「Full step with rail, report only」

### 修改檔案
- `Linux_test/main.cpp` — 新增 `test_full_step_report()`（選項 12）：結構同選項 7（有 DM2J rail），但：
  - 拿掉 `retry_grip_rail` lambda + verify 呼叫
  - 每個 phase 結束用 `vacuum_report()` 只印壓力，不做 threshold 判斷
  - 包含初始貼附階段（閥關 → 伸桿 → 使用者等 Enter 開閥）跟完整 cleanup（關閥 / 推桿歸 0 / 泵浦 OFF）
  - 用 staged extend（兩階段 → 1 秒 → 全伸）
  - 選單加第 12 項；main 分派加

### 新選項矩陣（rail vs vacuum handling）
| 選項 | rail | 真空 |
|---|---|---|
| 7  | ✅   | verify + retry + ABORT |
| 8  | ❌   | report only |
| 11 | ❌   | verify + retry + FAIL-continue |
| 12 | ✅   | report only |

### 原因
Sadie 要有滑桿但不驗證的版本（選項 12），用於現場觀察壓力變化、diagnose 建立真空的能力。完整四象限測試工具到齊。

## 2026-04-23w — Claude Code — 新增 frame_capture.py（RTSP → /tmp/cam_latest.jpg）

### 修改檔案
- `frame_capture/frame_capture.py` — 新增（~140 行）
- `frame_capture/README.md` — 新增（啟動、CLI flag、驗證步驟、systemd service、故障排除）

### 腳本重點
- OpenCV + FFmpeg 持續解碼 RTSP 子碼流（避免每次 query 重新 handshake）
- `os.replace()` Linux atomic rename，避免 detect_server 讀到半寫入檔
- 斷線自動重連（預設 2s 間隔）
- FPS 限流（預設 10fps），避免過度寫檔
- `OPENCV_FFMPEG_CAPTURE_OPTIONS=rtsp_transport;tcp` RTSP over TCP 較穩
- `CAP_PROP_BUFFERSIZE=1` 只保留最新 frame 降延遲
- SIGINT/SIGTERM 清理退出
- 每 10s stderr 印一次 fps + latest_age 健康統計

### 原因
Sadie 要開工寫攝影機避障，第一步 sidecar decoder。相機 RTSP URL 已由 Sadie 提供（Xiongmai H264DVR @ 192.168.1.10，子碼流），預設值直接帶進程式。程式先寫，實機 ffmpeg 驗證 + 跑腳本由 Sadie 之後做。

### 下一步
- Sadie 實機驗 ffmpeg + 跑 frame_capture.py
- 我接著寫 C++ `FrameAnalyzer` class（UDP to :5040 + JSON parse + decide）

## 2026-04-23v — Claude Code — 新增 camera_obstacle_plan.md（攝影機窗框避障規劃）

### 修改檔案
- `.claude/camera_obstacle_plan.md` — 新增
  - 系統架構（frame_capture.py / detect_server / washrobot）
  - 參數表分 5 類：攝影機 / 網路協定 / 決策邏輯 / server 內建 / frame_capture
  - Detect server v2 回應格式（多了 width_cm/height_cm/distance_cm/near_edge_cm）
  - Null 處理表 + 5 狀態 client 收發狀態機（ok/empty/error/no_reply/server_down）
  - C++ FrameAnalyzer decide_from_result() 決策邏輯範例
  - 分階段 roadmap + 規範邊界備註

### 原因
Sadie 提供：相機資訊（Xiongmai H264DVR RTSP + 帳密）、假設貼牆距離 15cm、detect_server v2 回應格式、client 收發建議（2s timeout × 3 連續失敗 → server_down）。先存檔規劃 + 參數，程式碼下一步才動工。

## 2026-04-23u — Claude Code — 兩階段 extend 修 short-circuit bug

### 修改檔案
- `Linux_test/main.cpp` `zdt_group_extend_staged()` — 原本 stage 1 `zdt_group_move_sync` 回 true 就 early return，導致 stage 2 沒跑，馬達停在半程。改成不管 stage 1 結果都跑 stage 2，err 合併回傳

### 原因
Sadie 回報：加了兩階段後實際只伸 3.5 cm（一半）。根因：stage 1 timeout（已知 ZDT pos_reached 不穩）觸發 early return。必須讓 stage 2 永遠執行。

## 2026-04-23t — Claude Code — 選項 8/11 推桿 extend 改兩階段

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 helper `zdt_group_extend_staged(drvs, slaves, full_target, delay_ms=1000)`：先 extend 到 half_target → 等 1s → extend 到 full_target
  - 選項 8 / 11 所有 extend 相關呼叫（初始貼附 / step phase / retry grip）全部改用 staged 版本：
    - feet extend to 20000 → 10000 → (1s) → 20000
    - body extend to 30000 → 15000 → (1s) → 30000
  - Retract 仍然單階段（直接到 0）

### 原因
Sadie 要 ZDT 伸長時分兩階段（先一半、等 1 秒、再另一半）減少吸盤接觸衝擊、給真空閥建立負壓時間 seal。

## 2026-04-23s — Claude Code — 推桿 extend 常數現場實測校正

### 修改檔案
- `Linux_test/main.cpp` — `PUSHER_EXTEND_FEET_PULSE` / `PUSHER_EXTEND_BODY_PULSE` 改直接硬編實測值：
  - `PUSHER_EXTEND_FEET_PULSE = 20000`（實測 ~7 cm）
  - `PUSHER_EXTEND_BODY_PULSE = 30000`（實測 ~10 cm）
  - 拿掉 `PUSHER_PULSES_PER_CM = 7200` 推算（當初從 144000 pulses = 20 cm 反推錯了）
- print 字樣從 "7 cm" → "~7 cm" 提醒這是近似

### 原因
Sadie 實測：原本推算 7 cm = 50400 pulses / 10 cm = 72000 pulses，結果超過 15 cm。實際腳組 20000 pulses / 身體組 30000 pulses 才對。直接硬編實測值比反推更可靠。

## 2026-04-23r — Claude Code — 選項 8/11 推桿改部分伸（腳 7cm / 身 10cm）

### 修改檔案
- `Linux_test/main.cpp` —
  - 加 `PUSHER_PULSES_PER_CM = 7200`（144000 pulses / 20cm）
  - 加 `PUSHER_EXTEND_FEET_PULSE = 50400`（7 cm）
  - 加 `PUSHER_EXTEND_BODY_PULSE = 72000`（10 cm）
  - 選項 8 所有 `zdt_group_move_sync(.. feet_slaves, PUSHER_EXTEND_PULSE)` → `PUSHER_EXTEND_FEET_PULSE`
  - 選項 8 所有 body_slaves 同理 → `PUSHER_EXTEND_BODY_PULSE`
  - 選項 11 同樣改法（包括 retry_grip lambda 裡依 zdt_group front() ≤4 判定 feet/body，選對 extend target）
  - 選項 7（有滑桿的完整版）不動

### 原因
Sadie 要腳組推桿伸 7cm、身體組推桿伸 10cm，不要 full stroke 20cm。避免推桿整根 extend 時機器被過度推離牆面。

## 2026-04-23q — Claude Code — 選項 8/11 initial attach 拆成「閥關伸桿 → 等操作員 → 開閥驗證」

### 修改檔案
- `Linux_test/main.cpp` `test_full_step_no_rail()` (選項 8) 和 `test_full_step_no_rail_verify()` (選項 11) 初始貼附流程：
  - **階段 1：** 不開閥，直接 extend 所有 8 支推桿（讓操作員目視確認貼附位置）
  - **印當前壓力**（閥關所以應該 ≈ 0）
  - **User gate：** `Press Enter to OPEN valves, 'q' to abort`
  - **階段 2（若沒 abort）：** 開 CH2+CH3 → settle 2s → 再印壓力（選項 8 只 report，選項 11 驗證 + retry）
  - User 若 abort → 跳過整個 step 迴圈，直接進入 cleanup

### 原因
Sadie 要求：初始貼附時先不要開三口二位電磁閥（CH2/CH3），給操作員時間目視確認吸盤貼牆位置是否正確。確認後操作員手動觸發開閥，才真正開始抽真空。避免「閥一下開了馬上錯位吸破真空」的情境。

## 2026-04-23p — Claude Code — zdt_group_move_sync 加位置不變 fallback

### 修改檔案
- `Linux_test/main.cpp` `zdt_group_move_sync()` settle 偵測：
  - `STOP_RPM` 5 → 20（寬鬆容許殘值）
  - 新增 `POS_DELTA_DEG = 0.15`：追蹤每 slave 前次 real_pos，Δpos ≤ 0.15° 視為靜止
  - 完成條件改 `stopped_by_speed || stopped_by_pos`（任一符合即可）
  - Debug 印 `rpm=X Δpos=Y°` 看是哪個 signal 觸發

### 原因
Sadie 回報 ZDT 物理上已停但 poll loop 卻總 timeout。根因：ZDT firmware 在靜止時 `real_speed` 可能回非 0 殘值（5-20 RPM 噪訊），速度 threshold 5 太嚴。改加位置不變 fallback — 就算 real_speed 讀值不乾淨，只要 real_pos 在 450ms 內沒變化就算到位。

## 2026-04-23o — Claude Code — 選項 11 對齊選項 8：完整 cleanup + retry 失敗繼續不中止

### 修改檔案
- `Linux_test/main.cpp` `test_full_step_no_rail_verify()`：
  - 四處 retry 失敗訊息（initial feet / initial body / step feet / step body）從 `[ABORT] ... Stopping.` 改 `[FAIL] ... continuing anyway` — 不 break、不 return，繼續下個 phase / step
  - 結尾 cleanup 從「只 disable ZDT」擴增成跟選項 8 一樣完整流程：關 CH2/CH3/CH4 → retract feet → retract body → CH1 pump OFF → disable ZDT

### 原因
Sadie 要選項 11 跟選項 8 結構對齊（initial attach 已經有、現在補上 cleanup），且 retry 超限時只印 [FAIL] 繼續，不要像之前那樣整個停止。適合全步驟跑完後看哪幾輪成功哪幾輪失敗的 diagnostic 用途。

## 2026-04-23n — Claude Code — 選項 8 完整 cleanup（關閥 + 推桿歸 0 + 泵浦 off）

### 修改檔案
- `Linux_test/main.cpp` `test_full_step_no_rail()` 結尾 cleanup：
  - 依序：關 CH2/CH3/CH4 三閥 → 等 300ms → 腳組 1,2,3,4 retract → 身體組 5,6,7,8 retract → 泵浦 CH1 OFF → disable ZDT 驅動
  - 順序重要：先關閥讓吸盤釋壓，避免帶著真空撕開吸盤

### 原因
Sadie 要選項 8 走完全部步數後完整清場：所有 relay 關、所有推桿回 0。原本只 disable ZDT driver，閥跟推桿位置都留在最後狀態。



### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 `test_full_step_no_rail_verify()`：結構類似選項 8，但每階段（初始貼附 / Phase A / Phase B）做 `vacuum_verify` 對 threshold，失敗 trigger `retry_grip`（release valve → retract → valve ON → re-extend → settle → re-verify）最多 N 次，exhaust 後 ABORT
  - retry_grip 是局部 lambda，只需 group 名 + slaves + valve channel 三參（沒有 center valve 參）
  - 選單加第 11 項；option 8 選單字樣更新為 "report, no verify" 區別
  - main 分派加分支

### 原因
Sadie 要「像選項 8 但帶 vacuum 判斷 + 重吸」。選項 8 保持純 diagnostic 模式（只印壓力、繼續），選項 11 是嚴格模式（驗證不過就整組 retry，超限就 ABORT）。兩個同時存在，用途分明。

## 2026-04-23l — Claude Code — 選項 8 加 initial attach phase

### 修改檔案
- `Linux_test/main.cpp` `test_full_step_no_rail()` — 在 step 迴圈前加初始貼附段：
  - 開兩個閥（CH2 feet + CH3 body）
  - 腳組 4 支 extend（ZDT 1,2,3,4）
  - 身體組 4 支 extend（ZDT 5,6,7,8）
  - Settle 2s
  - 讀 JC-100 1-8 報告壓力
- 更新 PRE-FLIGHT 提示：不再要求 operator 先貼好，測試會自己做初始貼附

### 原因
Sadie 要選項 8 最一開始先把所有推桿伸出吸住（初始貼附），之後再進入腳組 → 身體組的循環。原本流程假設 operator 手動貼好，對測試不方便；現在可以把機器放到牆上就按 Enter，程式自己貼。

## 2026-04-23k — Claude Code — 選項 7/8 拿掉中心推桿（ZDT 9 + CH4）

### 修改檔案
- `Linux_test/main.cpp` — `test_full_step()` 跟 `test_full_step_no_rail()` 的 Phase B：
  - `body_center_slaves = {5,6,7,8,9}` → `body_slaves = {5,6,7,8}`
  - 拿掉 `pqw.controlRelay(PQW_CH_VALVE_CENTER, ...)` 呼叫
  - Phase B 名稱從 "Body + Center" → "Body"
  - 所有 "body+center" 字樣改 "body"
  - retry_grip_rail 第 5 參 extra_valve_ch 改 0（無第二個閥）
  - JC-100 讀取範圍從 5,6,7,8,9 → 5,6,7,8

### 原因
Sadie 要求。中心推桿（ZDT 9）+ 中心吸盤閥（PQW CH4）暫時不用，現階段先驗證腳組 + 身體組。(User 說 "8,9"，但選項 9 是 SD76 不動 ZDT，所以實際改的是 7 跟 8。)

## 2026-04-23j — Claude Code — 選項 8 改成只印壓力不判門檻（diagnostic mode）

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 `vacuum_report()` helper：讀 JC-100 一群印壓力值，不做門檻判斷，無回傳
  - `test_full_step_no_rail()` Phase A / Phase B 的 `vacuum_verify + retry_grip + ABORT` 三件套改成單一 `vacuum_report()` 呼叫
  - 壓力無論如何繼續下一步

### 原因
Sadie 要選項 8 在此階段當純診斷工具 — 現場觀察壓力變化、不要被門檻卡住中斷流程。選項 7 保持完整門檻 + retry + ABORT 行為。`vacuum_verify()` 跟 `retry_grip` 都保留給選項 7 使用。

## 2026-04-23i — Claude Code — zdt_group_move_sync 加 per-slave retry + 失敗 skip 不 abort

### 修改檔案
- `Linux_test/main.cpp` `zdt_group_move_sync()` 重構：
  - enable 和 pos_queue 各自最多 3 次 retry，每次 back-off 120ms
  - 某個 slave 整組 retry 失敗 → 只 skip 那顆，不中斷整組
  - 收集 `queued`（成功佇列的）跟 `skipped`（放棄的）
  - Poll loop 只追蹤 `queued`
  - 全部 queued 成功到位且有 skipped → 回 error 讓 caller 知道
  - Timeout 時同時列 stuck + skipped
  - 加 `#include <functional>` 給 std::function

### 原因
Sadie 回報 option 8 retry 時連續 `[ERR] ZDT slave 1 enable fail` / `slave 3 enable fail`。根因：ZDT driver 連續送 Modbus 命令時 TCP buffer 有殘留 echo 造成下一命令 echo 讀錯 → 某個 slave 偶爾 enable 回非預期 bytes → 回 true。原邏輯一失敗就整組 abort 太嚴格，且失敗 slave 下次 retry 可能就好。改加 per-slave 3 次 retry + back-off + 放棄某顆時繼續其他 slave。

## 2026-04-23h — Claude Code — option 6 (test_zdt_group) 也補上速度回零 fallback

### 修改檔案
- `Linux_test/main.cpp` `test_zdt_group()` Phase 2 poll loop — 加入跟 `zdt_group_move_sync` 一樣的速度 fallback：`|real_speed| ≤ 5 RPM` 連續 3 次 poll 且超過 500ms → 視為完成

### 原因
Sadie 跑選項 6 slaves 5,6,7,8,9：馬達實際都到 `real_pos≈3600°` (=144000 pulses = 10 轉) 但 `pos_reached` bit 沒被 firmware set → 15s timeout 全部 aborted。之前只在選項 7/8 用的 `zdt_group_move_sync` 有這個 fallback，選項 6 的 poll loop 漏掉。

## 2026-04-23g — Claude Code — zdt_group_move_sync 移除 trigger_sync_move 誤警告

### 修改檔案
- `Linux_test/main.cpp` — `zdt_group_move_sync()` 不再檢查 `trigger_sync_move()` 回傳值

### 原因
ZDT driver 的 `trigger_sync_move()` 送 Modbus broadcast (slave 0x00)，依規範**不會有回應**。driver 看 readEcho 空就回 true (=專案慣例的 error)。我的 code 原本檢查回傳值印 `[WARN] trigger_sync_move reported send error`，但這實際上是 broadcast 正常行為，不是錯誤。Sadie 回報 retry 時 WARN 重複出現誤導 debug 方向。

## 2026-04-23f — Claude Code — Linux_test 選項 7/8 改為「先開真空再伸推桿」

### 修改檔案
- `Linux_test/main.cpp` — 選項 7 (Phase A feet / Phase B body+center)、選項 8 (同 2 phase)、兩個 retry grip lambda 都改順序：
  - **舊：** retract → (rail) → extend → valve ON → settle
  - **新：** retract → (rail) → **valve ON → extend** → settle
- print 訊息改稱 "pre-engage valve" 跟 "extend into pre-vacuumed cups"

### 原因
Sadie 指出抽真空應該在推桿伸出前開始 — 吸盤碰牆瞬間已有負壓，seal 立刻形成，減少邊緣漏氣機率。原先是貼牆後才開閥，空氣有時間從邊緣跑進去。

### 注意
WASH_ROBOT.cpp `cmd_attach()` 目前仍是「extend → valve ON」舊順序（line 516-519），之後 Sadie 實機驗證新順序確實更穩，可以同步改 WASH_ROBOT（跨界 user_lib 需要 PR review）。

## 2026-04-23e — Claude Code — ZDT group 加速度回零 fallback，避免 pos_reached bit 沒 set 的無限 poll

### 修改檔案
- `Linux_test/main.cpp` — `zdt_group_move_sync()` 偵測完成條件擴增：
  - (a) `pos_reached` bit set（原本唯一條件）
  - (b) `stall_flag` set（原本已有）
  - (c) **新**：`|real_speed| <= 5 RPM` 連續 3 次 poll（~450ms）且超過 MIN_WAIT 500ms → 視為實際停止，印 INFO 標註 pos_reached bit 未 set
  - timeout 10s → 6s（fallback 生效時本就該更早結束）

### 原因
Sadie 回報選項 6/7/8 的 ZDT 動作：推桿明明已經伸/縮到定位（物理、耳朵聽馬達停了），但程式一直 `get_system_status()` 直到 timeout。根因：ZDT 某些韌體 / 位置容差下，馬達到位後 `pos_reached` bit 不會被 set，只有速度歸零。加速度 fallback 讓程式能看到「馬達停了就走」而不是死等那個 bit。

## 2026-04-23d — Claude Code — Linux_test 加選項 9 (SD76 計米器) + 10 (ZS_DIO 捲揚機)

### 修改檔案
- `Linux_test/main.cpp` —
  - include `SD76_length_meters.h` + `ZS_DIO_R_RLY.h`
  - 新增 `test_sd76()`（選項 9）：提示 gateway IP (.30) + slave (2/3/4)，持續 live-read `readUpperInteger()` + `readStatus()`，互動指令 r/p/s（reset / pause / resume）/ q
  - 新增 `test_zsdio()`（選項 10）：提示 gateway IP + slave + total_relay，互動 `on N / off N / r N / a / o / q`。註解標示 main crane (.30) 跟 easy crane (.21) 的 channel 對應
  - 選單加兩項；main 分派加兩分支

### 原因
Sadie 要單獨測試計米器（SD76）和捲揚機繼電器（ZS_DIO）硬體是否正常。兩者都掛在吊車側，SD76 只有 main crane 有（slave 2,3,4），ZS_DIO 兩端都有但 channel 用法不同。測試工具涵蓋兩種配置。

## 2026-04-23c — Claude Code — 更新 ZDT slave ID 映射 + 選項 7 retry 改 rail-backup [跨界: user_lib]

### 修改檔案
- `user_lib/WASH_ROBOT.h:119-123` — ZDT slave ID 常數更新（跨界）：
  - `ZDT_LF1, LF2: 1,2 → 3,4`（左腳）
  - `ZDT_LB1, LB2: 3,4 → 6,8`（左身體）
  - `ZDT_RF1, RF2: 5,6 → 1,2`（右腳）
  - `ZDT_RB1, RB2: 7,8 → 5,7`（右身體）
  - `ZDT_C: 9`（中心，不變）
- `Linux_test/main.cpp` —
  - 選項 7/8 的 `feet_slaves` 改 `{1,2,3,4}`、`body_slaves` 改 `{5,6,7,8}`、`body_center_slaves` 改 `{5,6,7,8,9}`
  - print 字串從 "ZDT 1,2,5,6" → "ZDT 1,2,3,4"、"ZDT 3,4,7,8,9" → "ZDT 5,6,7,8,9"
  - **選項 7 retry 策略改 rail-backup（Strategy B）**：新增 `retry_grip_rail()` lambda + `RAIL_BACKUP_CM=5.0` 常數。Feet retry → rail -5cm；Body retry → rail +5cm。每次 retry 累積（3 次 retry = ±15cm）。跟 WASH_ROBOT `feet_backup`/`body_backup` 一致
  - 選項 8 保持原 `retry_grip`（純 pusher back-off，因無滑桿）
- `CLAUDE.md` 架構圖 — ZDT slave 對應表更新；PQW CH2/CH3 備註的 slave 清單從 `1,2,5,6 / 3,4,7,8` 改 `1,2,3,4 / 5,6,7,8`

### 原因
Sadie 確認實機接線：feet 左 3,4 / 右 1,2、body 左 6,8 / 右 5,7。原本 WASH_ROBOT.h 的常數跟實機對不上。修掉後 do_step_down_ / do_attach 等函式自然用新 ID（因為都用 `ZDT_LF1` 等常數）。另外選項 7 retry 改 rail-backup 更貼近 WASH_ROBOT 原生行為，能嘗試不同貼附位置。

### 規範邊界備註
`WASH_ROBOT.h` 屬 Jim 範圍，標 `[跨界: user_lib]`。Sadie 已確認 slave ID 就是這個新映射。

## 2026-04-23b — Claude Code — Linux_test 加選項 8「無滑桿的步伐測試」

### 修改檔案
- `Linux_test/main.cpp` — 新增 `test_full_step_no_rail()`：選項 7 的子集，去掉 DM2J rail 移動，只跑「腳組 detach → retract → extend → attach → 驗真空」+「身+中心同流程」+ retry grip。選單加第 8 項

### 原因
Sadie 要一個純 push/vacuum 循環測試，不動滑桿。適用場景：
- DM2J 還沒好
- 在平台上測（rail 會撞東西）
- 單純驗證推桿同步 + 真空重吸邏輯

## 2026-04-23 — Claude Code — Linux_test 加選項 7「完整步伐測試」

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增常數：slave 映射（feet 1,2,5,6 / body 3,4,7,8 / center 9 / DM2J rail 1,3 / PQW slave 12）、`PUSHER_BACKOFF_PULSE`（16mm back-off 用於重吸）、`VACUUM_SETTLE_MS`(2s) / `VACUUM_RELEASE_MS`(300ms)
  - 新增三個 helper：
    - `zdt_group_move_sync()` — queue N 個 ZDT 用 sync=1 + broadcast trigger + poll 到 pos_reached（沿用 option 6 的硬體同步模式）
    - `vacuum_verify()` — 讀 JC-100 一批，印每顆壓力 + 判斷是否都 ≤ threshold
    - `dm2j_pair_rail_move()` — 左右滑桿 queue + `PR_move_cm_trigger_all` 廣播 + 分別確認 done
  - 新增 `test_full_step()`：IP × 3（.20/.21/.22）+ 步距 + 步數 + 壓力門檻 + retry 數，循環跑：
    - Phase A（腳組）：釋放 CH2 → 腳推桿 retract → 滑桿 +cm → 腳推桿 extend → CH2 ON → 讀 JC-100 1/2/5/6 → 失敗觸發 retry_grip（back-off + re-extend + re-engage）
    - Phase B（身體+中心）：釋放 CH3+CH4 → 身+中推桿 retract → 滑桿 -cm → extend → ON → JC-100 3/4/7/8/9 → retry
  - 選單加第 7 項

### 原因
Sadie 要一個整合測試把 8 支推桿（腳 4 + 身 4）+ 滑桿（DM2J）+ 真空壓力驗證 + retry 全部串起來跑，不經 washrobot 主程式。相當於 `do_step_down_` 精簡成 Linux_test。預設小步距（10cm）+ 可調 retry，方便現場調參。



### 修改檔案
- `Linux_test/main.cpp` `test_zdt_group()`：`send_pos` lambda 把 `motion_control_pos_mode` 換成 `motion_control_pos_mode_nowait`

### 原因
2026-04-22i 的 patch 把 sync 改成 1（暫存模式），但觀察到「都不會動」。根因：`user_lib/ZDT_motor_control.cpp:361-369` 的 `motion_control_pos_mode` 內部在送完指令後會 blocking 呼叫 `wait_until_pos_reached()` 等到位。sync=1 下 ZDT 不啟動馬達（等廣播觸發才動），所以 `wait_until_pos_reached()` 永遠等不到 pos_reached → 每顆 slave 都 timeout → 被標 `aborted/send_fail` → 到 Phase 1b 時已無可 trigger 的 slave → 整組不動。

`motion_control_pos_mode_nowait` 是現成的 user_lib API（相同簽名、不 wait），送完即返回，剛好適合 queue-then-broadcast 的模式。換成 nowait 後 Phase 1 能正確 queue，Phase 1b broadcast 才會真的觸發 8 顆同步啟動。

### 規範權威
無（測試工具行為；未改 user_lib）。

## 2026-04-22i — Claude Code — 選項 6 改用 ZDT 硬體同步觸發

### 修改檔案
- `Linux_test/main.cpp` `test_zdt_group()`：
  - `send_pos()` lambda 加 `sync_flag` 參數
  - Phase 1 把 `pos_mode` 的 sync 從 `0`（立即）改為 `1`（暫存不執行）
  - 新增 Phase 1b：對任一已 queue 成功的 slave 呼叫 `trigger_sync_move()`（廣播 slave 0x00, Reg 0x00FF），所有暫存指令的 ZDT 同時執行
  - Stall recovery 的 resend 仍用 `sync=0`（立即），因為此時其他顆早已在動或到位，不該等廣播

### 原因
Sadie 回報選項 6 的推桿「一支接一支伸出」，不是真同步。根因：RS485 bus 是半雙工序列化，原本 `sync=0` 指令一送到就立刻執行，導致第 1 顆比第 8 顆早 ~300ms 動作。ZDT 本身支援多軸硬體同步（Reg 0x00FF 廣播觸發），用這機制後 8 顆會同一瞬間啟動。

### 規範權威
無（測試工具行為；ZDT API 本來就有，未改 user_lib）。

## 2026-04-22h — Claude Code — Linux_test 新增選項 6「ZDT multi-pusher group」

### 修改檔案
- `Linux_test/main.cpp`
  - 頂部新增 `#include <vector>` `#include <set>`
  - 新增 `test_zdt_group()`：對 slave 1~9 下達統一 target pulse，支援 skip list（預設 `9`，因主體只裝 8 支推桿）
  - 流程：Phase 1 逐顆 `release_stall_flag` + `motion_control_driver_EN(true)` + `motion_control_pos_mode`；Phase 2 統一 poll 每 200ms 讀所有未完成 slave 的 status
  - Per-slave 堵轉自動 recovery（延用選項 3 的邏輯：`emergency_stop` → `release_stall_flag` → re-enable → re-send，最多 3 次）
  - 單顆失敗不阻斷其他顆；結束印 SUMMARY 列每顆狀態（reached / aborted + 原因）
  - 主選單新增選項 `6`，dispatcher 加 `else if (line == "6")`

### 原因
Sadie 需求：主體目前只裝 slave 1~8 SMC 推桿（slave 9 中心未裝），想單元測試「8 支同時動作」驗證機械同步性與堵轉處理。選項 3 僅能單顆測，選項 6 填補批次驗證需求，不改 user_lib、不改 WASH_ROBOT 自動流程。

### 規範權威
無（測試工具行為，不動規範）。

## 2026-04-22g — Claude Code — 修 Linux SIGPIPE 讓主程式被殺

### 修改檔案
- `washrobot_new_PI/main.cpp` — `main()` 最前面加 `signal(SIGPIPE, SIG_IGN)`（`#ifndef _WIN32` 守衛）
- `Crane_control_PI/main.cpp` — 同上
- `Crane_easy_PI/main.cpp` — 同上
- `.claude/mailbox.md` — 留訊息給 Jim，請評估 user_lib `TCP_client::sendData` / `TCP_server::sendToClient` 改用 `MSG_NOSIGNAL`

### 原因
Sadie 回報 washrobot 跑到一半印 `Broken pipe` 被 shell 殺掉。根因：`TCP_client.cpp:175` / `TCP_server.cpp:175` 都用 `send(sock, buf, len, 0)` 不帶 `MSG_NOSIGNAL`，Linux 下對已關閉對端的 socket 寫入會送 SIGPIPE，預設處置 terminate → 整個 process 死。任何一端斷（web 後端、crane、RS485 gateway、Linux_test 退出）都會踩到。

`signal(SIGPIPE, SIG_IGN)` 在 main 最前面一次設定即可 process-wide 生效，之後 `send()` 對死 socket 會回 -1 + `errno=EPIPE`，交由呼叫端 return false 處理，不會殺 process。Windows 不受影響（Winsock 沒 SIGPIPE 概念）。

長期建議由 Jim 把 user_lib 的 send 改用 `MSG_NOSIGNAL`（更完整、Linux_test 等也受益），已留 mailbox。

### 規範權威
無（build/runtime 修復，不動規範或 API）。

## 2026-04-22f — Claude Code — 修 `WashRobot::IMU_BASELINE_SEC` undefined reference

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — 頂部新增類外定義 `constexpr int WashRobot::IMU_BASELINE_SEC;`

### 原因
連結錯誤 `undefined reference to WashRobot::IMU_BASELINE_SEC`。根因：C++14 下 `static constexpr` 類別成員**只是宣告**不是定義，一旦被 ODR-use（如 `WASH_ROBOT.cpp:266` 傳進 `std::chrono::seconds(const Rep&)`，參數是 const reference）linker 就要找類外定義。最小修復為加一行類外定義；長期應升 C++17（`static constexpr` 成員自動 inline variable 則無此坑）。

### 規範權威
無（build 層修復，不動規範或 API）。

## 2026-04-22e — Claude Code — Linux_test ZDT 測試加堵轉自動排除

### 修改檔案
- `Linux_test/main.cpp` — `test_zdt()` 把原本的 `drv.wait_until_pos_reached(10000, 200)` 換成自寫 poll loop：
  - 每 200ms 讀一次 `get_system_status()`
  - `pos_reached=1` → 成功 break
  - `stall_flag=1` → 印 `[STALL] at real_pos=... attempt N/3`，執行 recovery 序列（`emergency_stop(true)` → `release_stall_flag` → `motion_control_driver_EN(true)` → 重送 `motion_control_pos_mode`），最多重試 3 次
  - 10 秒無 stall 也沒到位 → `[WARN] attempt timeout`
  - 成功時印 `[OK] reached target (stall retries=N)` 讓使用者看到有沒有觸發過 recovery

### 原因
Sadie 測 slave 2 遇到 `pos_reached=0 stall=1` final 狀態，`wait_until_pos_reached` 只會報 timeout 不會告訴你是 stall，也不會嘗試排除。現在 Linux_test 能在偵測到堵轉時自動 release + 重試，用於排除偶發堵轉；連續 3 次 stall 時會 ABORT 並印最後位置，方便判斷是「穩定機械卡」還是「偶發 encoder 雜訊」。

### 規範權威
無（測試工具行為，不動 motion_flow 規範；user_lib API 未改）。

## 2026-04-22d — Claude Code — PQW all ON/OFF 從 [OK] 改 [SENT]（更誠實）

### 修改檔案
- `Linux_test/main.cpp` —
  - `test_pqw()` 裡 `all ON` / `all OFF` 改印 `[SENT] ... echo-len OK, content NOT verified → check LEDs physically`。因為 `controlAll` 只檢查 echo 長度 ≥ 8，內容不驗，garbage 也算 success
  - 在 PQW 測試開頭加 note 提醒：Modbus echo check 在有些 PQW 韌體不穩，實體 LED 驗證才是可靠

### 原因
Sadie 回報：按 `all ON` 看到 `[OK]`，但實體 relay 沒動。根因：driver 的 `controlAll` 邏輯太寬（echo.size() >= 8 就當成功），而不是驗證 echo 內容是否跟 TX 對應。Linux_test 不改 user_lib 邏輯，只改訊息誠實化：現在顯示 `[SENT]` 表示「送出去了，但不能保證真的開」，叫使用者看 LED。

## 2026-04-22c — Claude Code — Linux_test 避免 readback 誤報誤判（PQW / ZDT）

### 修改檔案
- `Linux_test/main.cpp` —
  - `test_pqw()` `controlRelay` / `controlAll(true)` / `controlAll(false)` 的錯誤改 `[WARN] readback mismatch — check LED physically`，不再報 ERR 中斷
  - 三處都採 optimistic 更新本地 state（信任寫入，忽略 readback 解析失敗）
  - `test_zdt()` `wait_until_pos_reached` timeout 改 `[WARN]` 並提示實體檢查位置
  - `motion_control_pos_mode` send 失敗仍是 ERR（這是真的送不出去，不是 readback）

### 原因
Sadie 實測 PQW `on 1` 時 TX 有發出、RX 回非標準 Modbus 框架，`parseReadResponse` 解析失敗→ 全回 false → `states[0] != true` → driver return error。實際上 relay 可能已經物理吸合。改 ERR→WARN 讓現場操作員憑實體 LED / 咔聲判定，而不是被假錯誤打斷。

## 2026-04-22b — Claude Code — Linux_test 加 TCP quick-probe 避免 2 分鐘 connect 卡死

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 `quick_tcp_probe(ip, port, timeout_ms=2000)`：用 non-blocking connect + select 做 2 秒快速可達性檢查（跨平台 Linux/Windows）
  - 4 個 TCP 測試（DM2J / ZDT / JC-100 / PQW）在 `TCP_client::connectToServer` 之前先 probe，不可達直接 2 秒內回錯誤（原本要 ~130 秒 Linux SYN retry 耗盡才會回）

### 原因
Sadie 回報無法連線的等待太久。`TCP_client::connectToServer` 用 blocking connect 沒設 timeout，Linux 預設要跑完所有 SYN 重試。不動 user_lib（Jim 範圍），只在 Linux_test 層加 pre-probe 處理。

## 2026-04-22 — Claude Code — Linux_test 重構：5 個單物件測試 + menu 迴圈

### 修改檔案
- `Linux_test/main.cpp` — 整個重寫結構：
  - 拿掉多-slave 的 `test_dm2j_scan()` + `test_zdt_pusher()`（violates "single object per test" 原則）
  - 保留 5 個獨立測試：IMU / DM2J / ZDT / JC-100 / PQW，每個自問 IP / slave / 參數
  - `main()` 改 menu loop（原本測完就結束；現在測完回選單、直到輸入 `q` 才退出）
  - 每個測試函式都清理資源後 `return`（ZDT/PQW 自動 disable / all_off，TCP 自動 close）

### 原因
Sadie 要求：每個物件都能輸入 slave id + ip 單獨測，且測完回主視窗選下一個。原設計單次執行單次測試後即退出，切換設備要重跑執行檔很煩。移除 scan/cycle 這兩個不是「單物件」的選項也讓介面更一致。

## 2026-04-21s — Claude Code — Linux_test ZDT 測試結束自動 disable 馬達

### 修改檔案
- `Linux_test/main.cpp` —
  - `test_zdt_single()` (option 5) 結束前加 `motion_control_driver_EN(false)` 自動關使能
  - `test_zdt_pusher()` (option 4) 'q' 退出前 loop slave 1..9 全部 disable

### 原因
Sadie 回報 ZDT 測試完馬達「關不掉」— 原流程只 enable 不 disable，馬達保持通電卡位置 + 持續發熱。現在測完自動 disable。跟 PQW 的 exit-all-off 同策略。

## 2026-04-21r — Claude Code — Linux_test PQW 改明確 on/off 語法（不再 toggle）

### 修改檔案
- `Linux_test/main.cpp` — `test_pqw()` 互動 loop 改成：`on N` 開指定通道、`off N` 關、`a` 全開、`o` 全關、`s` 看 state、`q` 退出。原 toggle 模式拿掉（避免跟硬體實際狀態不同步造成誤解）

### 原因
Sadie 要明確「開 relay」的動作而不是 toggle。改成 verb 明確的語法後意圖清楚，重複按同指令也不會誤翻狀態。

## 2026-04-21q — Claude Code — Linux_test 選項 6 簡化（拿掉 scan，直接問 slave）

### 修改檔案
- `Linux_test/main.cpp` — `test_jc100()` 移除 scan 1..9 階段，改成線性流程：`IP → Slave ID → live-read` 跟選項 2/5 統一風格
- 選單 line 改成 `JC-100 pressure — slave + live read`

### 原因
Sadie 要所有測試都可直接輸 slave id 跑，不要中間 scan 干擾。選項 7 PQW 已經是線性流程沒動。

## 2026-04-21p — Claude Code — Linux_test 加選項 6/7（JC-100 + PQW）

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 `test_jc100()`：掃 slave 1~9 印 pressure，然後可選一顆 live-read（每 200ms 刷新，按 Enter 停）
  - 新增 `test_pqw()`：連 gateway → init PQW 8CH → 互動 toggle（1~8 切換 / 'a' 全開 / 'o' 全關 / 's' 看 state / 'q' 退出），退出時自動 all off
  - 內含 washrobot 的 CH1~8 對照表（泵浦/腳閥/身閥/中心閥/刷洗/水泵/水閥/保留）
  - 選單加選項 6、7

### 原因
Sadie 要在 Linux_test 裡測試 JC-100 真空表跟 PQW 繼電器，讓現場可以不經 washrobot 單獨驗這兩個硬體。PQW 測試退出時自動 all_off 避免泵浦 / 閥留啟動狀態。

## 2026-04-21o — Claude Code — Linux_test 加選項 5「ZDT single move」（簡單版）

### 修改檔案
- `Linux_test/main.cpp` — 新增 `test_zdt_single()`：提示 gateway IP / slave / target pulse，release_stall + enable + motion_control_pos_mode + wait_until_pos_reached + 印最終狀態。選單加第 5 項

### 原因
Sadie 覺得選項 4（scan+cycle）太複雜，要一個像 option 2 (test_dm2j) 那樣簡單線性的版本：給 slave + 目標 pulse 就 go。保留 option 4 給掃描需求。

## 2026-04-21n — Claude Code — Linux_test 加 ZDT SMC 推桿測試（選項 4）

### 修改檔案
- `Linux_test/main.cpp` —
  - 加 SMC 推桿常數（與 `WASH_ROBOT.h PUSHER_*` 對齊：EXTEND=144000, RETRACT=0, RPM=1000, ACC=255）
  - 新增 `test_zdt_pusher()`：連 .21 gateway → 掃 slave 1~9 印每顆 `enabled / pos_reached / stall / home_ok` → 互動式選 slave → `release_stall_flag` + `motion_control_driver_EN(true)` + 伸 → 等 `wait_until_pos_reached` → 縮 → 等 → 報告 → 回到選單
  - 支援 `e`（只 enable）/ `d`（只 disable）/ `q`（退出）
  - main 選單加第 4 項

### 原因
Sadie 想現場測 SMC 推桿（ZDT 驅動卡 × 9）是否活著 + 動得了。比單一 slave 方便，掃完直接選要 cycle 哪顆。含 enable 保險（DM2J 那邊的 ENABLE 問題同樣可能發生在 ZDT）。

## 2026-04-21m — Claude Code — Linux_test main.cpp：debug=true + 加 DM2J slave scan 選項

### 修改檔案
- `Linux_test/main.cpp` —
  - `test_imu()` IMU init debug `false → true`
  - `test_dm2j()` DM2J init debug `false → true`（現場 diagnostic 用途）
  - 新增 `test_dm2j_scan()` — 掃 slave 1~10，每個呼叫 `read_pulse_per_rev()`，回報 OK/no_response/init_fail
  - main 選單加第 3 項「DM2J slave scan」

### 原因
Sadie 跑 test_dm2j 報 `PPR read failed` — slave ID 不對或硬體狀態不明。加 scan 功能讓現場可一鍵掃出哪些 slave 活著、PPR 多少；debug=true 印 Modbus TX/RX hex 幫助判斷是哪一段沒回應。

## 2026-04-21l — Claude Code — Linux_test.vcxproj 同樣修法（log_utils + 相對路徑 + pthread）

### 修改檔案
- `Linux_test/Linux_test.vcxproj` —
  - `<ClInclude>` 群組加 `..\user_lib\log_utils.h`
  - `AdditionalIncludeDirectories` 從 `D:\washrobot_new_PI\user_lib` 改相對 `..\user_lib`
  - 加 unconditional Link 依賴 `pthread`

### 原因
Sadie 跑 Linux_test build 時報 `log_utils.h: No such file or directory`，跟 washrobot_new_PI 是同樣三個老問題（log_utils.h 漏列 + 絕對路徑 + pthread）。一併修好。

## 2026-04-21k — Claude Code — 兩個 vcxproj 加 pthread linker dependency

### 修改檔案
- `washrobot_new_PI/washrobot_new_PI.vcxproj` — 加 unconditional ItemDefinitionGroup 帶 `<Link><LibraryDependencies>pthread</LibraryDependencies></Link>`
- `Crane_easy_PI/Crane_easy_PI.vcxproj` — 同上

### 原因
Sadie build 時 link 階段報 `undefined reference to pthread_create`。std::thread 在 Linux g++ 下需要 `-lpthread`，VS Linux project 要透過 `<LibraryDependencies>pthread</LibraryDependencies>` 指定（MSBuild 翻譯為 `-lpthread`）。兩個 vcxproj 都漏了；加 unconditional ItemDefinitionGroup 讓所有 config 都吃到。

## 2026-04-21j — Claude Code — washrobot_new_PI.vcxproj 修 log_utils.h 遺漏 + 絕對路徑改相對

### 修改檔案
- `washrobot_new_PI/washrobot_new_PI.vcxproj` —
  - `<ClInclude>` 群組加 `..\user_lib\log_utils.h`（原本漏了，Crane_easy_PI 有）
  - `AdditionalIncludeDirectories` 從硬編 `D:\washrobot_new_PI\user_lib` 改成相對 `..\user_lib`（跟 Crane_easy_PI 對齊）

### 原因
Sadie build washrobot_new_PI 時報 `log_utils.h: No such file or directory`。根因：VS Linux project 靠 `<ClInclude>` 列表決定 sync 哪些 header 到 Pi，washrobot vcxproj 當初沒把 log_utils.h 列進去 → VS 不 sync → g++ 找不到。Crane_easy_PI 當初有加。順便把同檔的硬編 Windows 絕對路徑改相對，避免換 drive letter / 換機器壞掉。

## 2026-04-21i — Claude Code — web_backend reconnect exponential-spawn bug fix（OOM 元兇）

### 修改檔案
- `web_backend/server.js` — `makeBridge()` 重構：reconnect 去重（`state.reconnectTimer` flag）、進 connect() 先 destroy 舊 socket、`error` 事件只 log 不觸發 reconnect（讓 `close` 獨占驅動）
- `.claude/work_log.md` — 頂部 2026-04-21i 條目

### 原因
Sadie 回報 backend 被 `Killed`（OOM），ss 顯示 fd 87787 + 大量 SYN-SENT。根因是 Node socket 失敗會同時 fire error+close 兩事件，原碼兩個 handler 各自 schedule 重連 → 每次失敗 reconnect 數量翻倍 → 10 分鐘內 socket 指數爆炸。修去重 + 清理 + 事件單一驅動。

## 2026-04-21h — Claude Code — easy_crane_test_mode.md §5 補 shim 不聽 EVT 限制

### 修改檔案
- `.claude/easy_crane_test_mode.md` §5 功能落差表 — 加一行「shim 監聽 easy EVT ❌」，說明 easy 自我保護觸發時繩物理會停但 shim 回假 OK、累積位置誤差；現場發現狀態不一致時查 shim stderr + easy log

### 原因
Sadie 問收繩時碰到 weight_limit 或 DY500 read_fail 會不會停。確認：物理層 easy 會 all_off 停，但 shim 是開環睡覺不訂閱 EVT，會繼續回 OK 給 washrobot。現階段不改 shim（retract 15cm 太短不易觸發），只補文件提醒。

## 2026-04-21g — Claude Code — easy crane 按鈕語意重構（HOLD×2 + AUTO 單鍵）

### 修改檔案
- `web_backend/public/index.html` — 拿掉「模式」切換 row；AUTO 按鈕移到獨立 row（🤖 AUTO 拉到上限）；hint 更新
- `web_backend/public/app.js` — 廢除 `easyAutoMode` 雙模式切換，改成三顆按鈕獨立語意：UP/DOWN 純 hold、AUTO click toggle。server state sync 改單向（僅在 server 清零時重置 local，避免剛按下的 race window）；`releaseAllEasyHolds()` 擴充包含 AUTO 重置

### 原因
Sadie 要更直覺的按鈕語意：AUTO = 一鍵拉到重量門檻（靠 server-side weight_loop 自動停）、UP/DOWN = 純按住。原 HOLD/AUTO mode toggle 混淆。後端不動（cmd_up / weight_loop 既有行為已足夠），純前端重構。

## 2026-04-21f — Claude Code — GUI 效能調整（拿掉 backdrop-filter + aurora blob）

### 修改檔案
- `web_backend/public/style.css` — 全面精簡，拿掉 `backdrop-filter`（原每 panel / header / banner / modal 都有 blur）、aurora drift 動畫、banner pulse、按鈕 hover box-shadow glow、log text-shadow；保留靜態 bg gradient、dot 脈動（改 opacity fade）、panel 頂部漸層線、gradient header、input focus border
- `.claude/work_log.md` — 頂部 2026-04-21f 條目

### 原因
Sadie 回報 Pi Chromium 渲染原 04-21b 主題有點卡。主因是 `backdrop-filter: blur(16px)` 用在每個 panel，加上兩顆 aurora 漂移 blob（`blur(90px)` + 無限動畫）GPU 吃很兇。拆掉最重的幾項，保 aesthetic 核心（霓虹色、gradient 標題、脈動 dot、漸層 bg）。CSS 行數 380 → ~290。

## 2026-04-21e — Claude Code — WASH_ROBOT CRANE_IP 改 .5.26（shim/easy 共 Pi）[跨界: user_lib]

### 修改檔案
- `user_lib/WASH_ROBOT.h:100` — `CRANE_IP: "192.168.1.101" → "192.168.5.26"` + `[TEST MODE 2026-04-21]` 4 行註解
- `.claude/easy_crane_test_mode.md §9a` — 撤除清單加 CRANE_IP 第一行；其他行號因前面加註解而位移 +5
- `.claude/work_log.md` — 頂部 2026-04-21e 條目

### 原因
Sadie 測試配置：crane_shim + Crane_easy_PI + web_backend 全在 .5.26 同一台 Pi，washrobot 將進 .5.x 網段。原硬編 .101 連不到。加 TEST MODE 註解，主 crane 到位時 revert。

## 2026-04-21d — Claude Code — web_backend TCP keepalive + 10s ping（easy crane 閒置掉線修復）

### 修改檔案
- `web_backend/server.js` — `makeBridge()` 加 `sock.setKeepAlive(true, 30000)` + 每 10s 對每個 bridge 送 `ping\n`；新常數 `BRIDGE_PING_MS = 10000`
- `.claude/work_log.md` — 頂部 2026-04-21d 條目

### 原因
Sadie 回報 GUI 閒置一陣子後 easy crane 轉紅。根因：easy crane 跨網段（.5.x），中間 NAT 閒置 15~60min 殺 TCP session，backend 沒啟 keepalive 沒察覺。Browser 50ms status poll 在 tab 背景化時被 setInterval throttle 無法保連。兩層修：OS 層 `setKeepAlive` + backend 自己 10s `ping`。副作用：log 每 10s 多 3 行 OK 回應，若吵下一輪處理。

## 2026-04-21c — Claude Code — 測試模式程式改動：watchdog 60s + 全驅動 debug [跨界: user_lib]

### 修改檔案
- `user_lib/WASH_ROBOT.h:142` — `WATCHDOG_TIMEOUT_MS: 2000 → 60000`（+ 6 行 [TEST MODE] 註解）
- `user_lib/WASH_ROBOT.cpp:58,66,74,81` — DM2J / ZDT / JC-100 / PQW init debug `false → true`（+ 區塊 [TEST MODE] 註解）
- `user_lib/WASH_ROBOT.cpp:96` — IMU init debug `false → true`（+ 單行 [TEST MODE] 註解）
- `Crane_easy_PI/main.cpp:318,319` — relay / dy500 init debug `false → true`（+ [TEST MODE] 註解）
- `.claude/easy_crane_test_mode.md` §9 — 新增「撤除測試模式 ⚠️ 必看清單」
- `.claude/work_log.md` — 頂部 2026-04-21c 條目

### 原因
下午上機前 code review 發現 `WATCHDOG_TIMEOUT_MS=2000` 會讓 shim `pay_out 45cm @ 3cm/s = 15s` 期間 crane_mtx_ 鎖死 → `motion_active_=true` 時自動 abort。Sadie 決策：調 60000ms + 所有驅動 debug=true 方便 on-site troubleshoot。所有改動旁都有 `[TEST MODE 2026-04-21]` 註解，grep 可快速找到撤除點。主 crane 到位時照 §9 清單還原。

## 2026-04-21b — Claude Code — Web GUI 主題重做（深空極光 glassmorphism）

### 修改檔案
- `web_backend/public/style.css` — 全面重寫，加設計 token（深空紫藍底 + 霓虹青紫粉 accent）、aurora bg blobs（body ::before/::after 漂移動畫）、glass panel（backdrop-filter blur + 紫 border + 頂部漸層線）、脈動 status dot、gradient 標題、cyan-glow input focus、發光 log 顏色、pulse banner、modal glass
- `web_backend/public/index.html` — `<head>` 加 Google Fonts 3 行（Inter + JetBrains Mono，display=swap，Pi 離線自動 fallback system-ui/Consolas）
- `.claude/work_log.md` — 頂部 2026-04-21b 條目

### 原因
Sadie 要「夢幻 + 科技感」，從 A/B/C 三方向選了 A（深空 glass）。保留所有 class/id hook，app.js 零改動。待上機驗證：backdrop-filter 在 Pi 瀏覽器的渲染效能、blur 過重可能要降、Google Fonts 離線 fallback。

## 2026-04-21 — Claude Code — crane_shim 測試模式（簡易吊車 + washrobot 自動下洗）

### 修改檔案
- `crane_shim/crane_shim.py` — 新增（Python stdlib，TCP server :5002 偽裝 Crane_control_PI，翻譯 pay_out/retract → easy crane :5003 timed on/off；motion_lock + abort flag + easy watchdog keepalive）
- `crane_shim/README.md` — 新增（啟動、CLI flag、速率校正、故障排除）
- `.claude/easy_crane_test_mode.md` — 新增（測試模式規範：指令對照、安全守則、可用流程、checklist）
- `.claude/runbook.md` — §A 加「1-alt 測試模式」啟動分支
- `.claude/work_log.md` — 頂部 2026-04-21 條目

### 原因
Sadie 想在主吊車（Crane_control_PI + DSZL_107 + 中間絞盤變頻器）到位前，用簡易吊車（Crane_easy_PI @ .5.26:5003）跑 washrobot 的 `run` / `step_down` 自動循環做短距離受控測試。兩協定不相容（距離型 vs 時間型），用 shim 層轉譯最不侵入。Phase 5/6 / home_status / roll_correct 在 shim 內明確拒絕以擋 GUI 自動按鈕。速率 rate_down/rate_up 目前 3.0 cm/s placeholder，上機實測後校正。

## 2026-04-20k — Claude Code — easy crane 第三輪提速（SUSTAIN=0 + all_off 最佳化）

### 修改檔案
- `Crane_easy_PI/main.cpp` — `WEIGHT_SUSTAIN_MS: 50 → 0`；`all_off()` 用 `atomic::exchange` 跳過已關閉的繼電器寫入
- `.claude/work_log.md` — 頂部 2026-04-20k

### 原因
Sadie 要求再快。j 輪降到 ~50-80ms，k 輪再砍 30-50ms：第 1 個 bad reading 就觸發（移除 sustain）+ 省一次冗餘 relay OFF Modbus 寫入。剩下的延遲全在 Modbus 物理層。

## 2026-04-20j — Claude Code — easy crane weight_loop 二輪提速

### 修改檔案
- `Crane_easy_PI/main.cpp` —
  - 常數：`WEIGHT_POLL_MS = 50` → 移除；新增 `WEIGHT_YIELD_MS = 1`（僅 CPU yield）
  - `WEIGHT_SUSTAIN_MS: 100 → 50`
  - `weight_loop` 用 `std::chrono::steady_clock` 實測時間累計 over_ms / fail_ms（原本假設固定間隔不準）
- `.claude/work_log.md` — 頂部 2026-04-20j 條目

### 原因
i 輪優化把停機從 ~800ms 降到 ~150ms，Sadie 回報仍不夠快。主因 50ms sleep 佔一半循環時間。移除 sleep 並用實測時間累計後預估 ~50-80ms 停機。

## 2026-04-20i — Claude Code — Easy crane 停機延遲優化 + 門檻 input live

### 修改檔案
- `Crane_easy_PI/main.cpp` — `weight_loop` 安全檢查改用 raw `w` 不用平均 `g_weight`（節省 ~500ms 平均延遲）；`WEIGHT_SUSTAIN_MS` 300→100；結構調整把安全檢查移進 read OK 分支
- `web_backend/public/index.html` — 移除「set」按鈕，改成純 input（中間文字「（目前: X kg）」顯示 server 值）
- `web_backend/public/app.js` — input `input` event 直接送 `set_up_stop_kg`，150ms debounce 防中間狀態
- `.claude/work_log.md` — 頂部新增 2026-04-20i 條目

### 原因
Sadie 實測發現停機太慢（~800ms 最差），來不及避免打到門檻。根因是 safety 用了 10 樣本平均 + 300ms sustain。改用 raw 讀值 + 2 樣本 sustain 降到 ~150ms。順便把 set 按鈕去掉改為 live input 以減少操作步驟。

## 2026-04-20h — Claude Code — Easy crane AUTO / HOLD 雙模式

### 修改檔案
- `web_backend/public/index.html` — easy crane panel 新增「模式」row + AUTO toggle 按鈕；hint 列點加 2 條解釋兩模式
- `web_backend/public/app.js` — 整段 easy crane 重寫：
  - 移除 bindHold 依賴 + 500ms ping heartbeat（與 50ms status poll 重複）
  - 新增 `easyAutoMode` / `easyUpActive` / `easyDownActive` 狀態
  - `easyStartUp/StopUp/StartDown/StopDown` + `updateEasyButtonLabels`
  - AUTO 按鈕 handler（關閉時自動停所有動作）
  - UP/DOWN onPress/onRelease mode-aware：HOLD 按住才動、AUTO 點擊 toggle
  - `onEasyCraneLine` 新增 server state sync（解析 `up=` / `down=` 校正客戶端）
- `web_backend/public/style.css` — `.btn-auto` + `.btn-auto.active` 樣式（橙色、box-shadow）
- `.claude/work_log.md` — 頂部新增 2026-04-20h 條目

### 原因
Sadie 要求加 auto 模式：點擊 UP/DOWN 持續動作，再點一次停，遇門檻/watchdog/讀失敗仍自動停。設計關鍵：50ms status poll 本身就是 heartbeat，因此 AUTO 模式不需要額外心跳；客戶端狀態用 status 回傳的 `up=` / `down=` 持續校正，避免任何顯示與 server 實際狀態不一致。

## 2026-04-20g — Claude Code — Easy crane 可調 UP 門檻 + DY500 讀取失敗防呆

### 修改檔案
- `Crane_easy_PI/main.cpp` —
  - `WEIGHT_UP_STOP_KG` (constexpr) → `g_up_stop_kg` (atomic<float>)，預設 `DEFAULT_UP_STOP_KG = -20.0f`
  - 新增 `g_weight_valid` (atomic<bool>)、常數 `READ_FAIL_STOP_MS = 500`
  - `weight_loop` 雙檢：(a) 讀失敗累計超 500ms 且動作中 → all_off + EVT `weight_read_fail`；(b) UP 且 weight < g_up_stop_kg 持續 300ms → all_off + EVT `weight_limit`
  - `cmd_up` / `cmd_down` pre-flight：`!g_weight_valid` 直接 `ERR weight_read_fail`；`cmd_up` 同時檢查門檻
  - 新 `cmd_set_up_stop_kg` + dispatch 新增 `set_up_stop_kg <kg>`
  - `cmd_status` 回傳加 `up_stop_kg` + `weight_valid`
- `web_backend/public/index.html` — easy crane panel 新「收繩停止門檻」input + set 按鈕 + 目前值顯示；hint 列點從 3 條擴到 4 條
- `web_backend/public/app.js` — `onEasyCraneLine` 解析 `up_stop_kg=`、新增 EVT `weight_read_fail` 觸發 `releaseAllEasyHolds()`；`btn-easy-set-stop` 送 `set_up_stop_kg <v>`
- `.claude/runbook.md` — C2b easy crane 指令集加 `set_up_stop_kg`、防呆從 3 層更新為 4 層
- `.claude/work_log.md` — 頂部新增 2026-04-20g 條目

### 原因
Sadie 要求：(a) 網頁 input 可設門檻（避免 hard-code 重編）；(b) DY500 讀不到重量時強制停機且拒絕新指令。設計上 UP 門檻 runtime 可設 + pre-flight 檢查 + 持續監測三重保險。

## 2026-04-20f — Claude Code — 新增 Crane_easy_PI 子專案（獨立簡易吊車）

### 修改檔案
- `Crane_easy_PI/main.cpp` — **新檔**：獨立吊車 TCP server (:5003)，指令集 `up/down <on|off> / stop / status / ping`，含 weight monitor thread + watchdog thread + weight-limit safety
- `Crane_easy_PI/Crane_easy_PI.vcxproj` — **新檔**：MSBuild 專案檔（GUID `{909DCE76-3882-475C-8853-EB344B428FF6}`），引用 user_lib 的 TCP_client/server、ZS_DIO_R_RLY、DY_500_weight_sensor
- `washrobot_new_PI.sln` — 加入新專案 + 8 configuration mappings
- `crane_control_PI_easy_ver/` — **刪除整個資料夾**（user_lib/ 副本重複、main.cpp 被重寫的版本取代）
- `web_backend/server.js` — 加第 3 條 bridge `easy_crane`（env `EASY_CRANE_IP` 預設 `192.168.5.26:5003`），3-state status broadcast
- `web_backend/public/index.html` — 頂部第 3 顆 dot、新 panel「easy crane」（重量顯示 + 拉繩/釋放繩 press-and-hold + refresh/STOP）
- `web_backend/public/app.js` — 新 `bindHold()` 通用 helper（emergency retract 按鈕也改走 helper），`easy_crane` press-and-hold + 500ms ping heartbeat，自動 2 秒 poll `status` 更新重量顯示，EVT `watchdog_timeout` / `weight_limit` 收到時本地釋放按鈕狀態
- `web_backend/public/style.css` — `.panel-easy_crane` + `.btn-hold` + `.btn-hold.active` 樣式
- `.claude/runbook.md` — 啟動順序加第 3 步 Pi、GUI 按鈕表加 easy crane、C2b 指令表 + 三層防呆說明、緊急處置表新增 2 條
- `.claude/work_log.md` — 頂部新增 2026-04-20f 條目

### 原因
Sadie 丟來獨立的簡易吊車程式碼（交互式 terminal 版），要求整理成主 workspace 的子專案、加到 Web GUI、並做網路失聯防呆（防止 UP 方向繩縮到最上卡壞）。採取三層防呆設計：server watchdog（2s 無 inbound 自動停）+ 重量門檻（`WEIGHT_UP_STOP_KG` placeholder `-20kg`）+ 前端 press-and-hold 500ms heartbeat。

## 2026-04-20e — Claude Code — CLAUDE.md 交接指引加 runbook [跨界: CLAUDE.md]

### 修改檔案
- `CLAUDE.md` — 「給 Claude CLI 的交接指引」節新增 item 3 指向 `.claude/runbook.md`，原 item 3 順延為 4

### 原因
runbook 定義「怎麼用系統」，對新接手 session / 協作者而言與 work_log / motion_flow 同等重要，加進 onboarding 清單。規範文件屬 Jim 範圍，標 `[跨界: CLAUDE.md]`。

## 2026-04-20d — Claude Code — 新增 runbook.md

### 修改檔案
- `.claude/runbook.md` — 新增：啟動順序（crane → washrobot → browser）/ Web GUI 按鈕對應 / washrobot + crane raw command 指令集 / EVT 主動事件 / 典型流程表（Phase 1~6）/ 緊急處置表 / 失聯模式四態對照

### 原因
Sadie 要求整理一份「從冷開機到操控」的操作文件，涵蓋啟動順序、按鈕 → 指令對應、典型流程、緊急處置。放進 `.claude/` 讓協作者都看得到。規範權威仍以 motion_flow.md 為準，runbook 只說「怎麼用」不重複定義。

## 2026-04-20c — Claude Code — Web 前端五件套

### 修改檔案
- `web_backend/public/app.js` — 整檔重寫：mode 切換、auto-stop 邏輯、home_status pending resolver、balance_ask EVT 解析、press-and-hold 緊急收繩、2 個 modal
- `web_backend/public/index.html` — 新增 `#banner` + 各 panel 加 `panel-washrobot` / `panel-crane` / `panel-emergency` class、2 個 modal、鋼索歸零按鈕、召回按鈕、reset 按鈕；修正 STOP (robot) 送 `emergency_stop`
- `web_backend/public/style.css` — append banner / panel-disabled / primary button / emergency panel+button / modal 樣式
- `.claude/work_log.md` — 頂部新增 2026-04-20c 條目

### 原因
motion_flow §8 已規範失聯模式 UI + 緊急收繩按住邏輯，但 `app.js` 舊版只有基本命令送出、`index.html` 無對應元素。本次實作 5 件套：失聯模式灰化 + banner、緊急收繩 press-and-hold、Phase 6 召回兩步驟流程（home_status → remaining → return_home）、鋼索歸零、balance_ask EVT 彈窗。

未動 `server.js`（既有 bridge 已足夠）。

## 2026-04-20b — Claude Code — Phase 2 補收輪 + 程式碼同步 [跨界: motion_flow]

### 修改檔案
- `.claude/motion_flow.md` —
  - §2 RS485_1 表 slave 2/4：「Phase 1 only」→「Phase 1 放輪爬牆；Phase 2 收輪」
  - §4 Phase 2 新增 step 8「DM2J slave 2, 4 → 0（收輪）」+ 說明機械原理 + 前置假設，原 step 8~11 順延
- `user_lib/WASH_ROBOT.cpp cmd_init()` — 在繼電器 OFF 後、`pusher_move_many_` 前插入左右輪 retract（`PR_move_cm(0, 1, DM2J_RPM, 0.0, DM2J_ACC, DM2J_DEC)`）
- `.claude/work_log.md` — 頂部新增 2026-04-20b 條目

### 原因
輪子裝於靠牆面，Phase 1 展開供爬牆；Phase 2 推桿伸出前必須先收輪，否則吸盤碰不到玻璃。放在 cmd_init() 統一觸發：單一 entry point + 防呆。Phase 6 召回不放輪（輪子無地面緩衝作用）。

## 2026-04-20 — Claude Code — Phase 3 補 VACUUM_SETTLE_MS 規格 [跨界: motion_flow]

### 修改檔案
- `.claude/motion_flow.md` —
  - §4 Phase 3 新增 step 5「等 VACUUM_SETTLE_MS（預設 2000 ms）讓壓力穩定」，原 step 5 順延為 6
  - §6 可調參數表新增 `VACUUM_SETTLE_MS = 2000 ms`（列於 VACUUM_THRESHOLD_KPA 之後）
- `.claude/work_log.md` — 頂部新增 2026-04-20 條目

### 原因
`cmd_attach()` 實際已有 `sleep_ms_(VACUUM_SETTLE_MS)` 在 CH4 ON 後、讀 JC-100 前；規格漏列。補進規格讓 spec/code 對齊。程式碼無需變動。

## 2026-04-17b — Claude Code — 角色表釐清 Jim/Sadie 分工

### 修改檔案
- `CLAUDE.md` — 角色表從 5 列改 4 列：「規範 + 裝置驅動 = Jim」「應用層實作 = Sadie」「前端 = Sadie」「測試 / 工具 = Sadie」。`介面契約` 節完全不動（保留抽象代名詞「架構方」/「協作方」）
- `.claude/work_log.md` — 2026-04-17 條目的「Jim 一度以為」→「Sadie 一度以為」（該條目紀錄的是本次對話，user 是 Sadie）

### 原因
Sadie 本次 session 自我介紹並釐清分工：Jim 負責規範文件 + user_lib/ 裝置驅動；Sadie 負責應用層（WASH_ROBOT 編排、main.cpp、web_backend、測試工具）。原 5 列角色表把 user_lib/ 完整掛在 Jim 名下、其他 4 列為空，與實況不符。

## 2026-04-17 — Claude Code — work_log 備忘 TCP 拓撲釐清

### 修改檔案
- `.claude/work_log.md` — 頂部新增 2026-04-17 條目，記錄「washrobot↔crane TCP 方向現狀已符合 Jim 的救援設計，無需更動」

### 原因
Jim 對話中一度以為 washrobot 是 crane 連線的 server 端，想翻轉；翻代碼確認現狀 crane 已是 server、washrobot 是 client，救援路徑已健全。純備忘，避免未來協作者/session 再誤會。


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
