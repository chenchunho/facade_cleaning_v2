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
