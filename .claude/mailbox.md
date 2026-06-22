# 協作信箱 (Mailbox)

> 跨分工邊界的需求、通知、問題。
> 每位協作者的 Claude 開 session 時應掃一眼此檔。
> 處理完的條目移到「已處理」段，保留作為決策紀錄。
>
> **阻塞程度標記：** 🔴 急迫（阻塞工作）/ 🟡 非急迫（可等）/ 🟢 參考通知
>
> **格式：** `- **[日期] 來源角色:** 描述（含用途/上下文 + 阻塞程度）`

## 待處理

### → 架構（Jim）

- **[2026-05-14] Sadie:** 🟡 **SE3_inverter `sendModbus` recv timeout 縮短 300→150ms（[跨界: user_lib]，bench 驗證中）** — bench 觀察 `[dual_se3_concurrent] asymmetric cmd dispatch L=1228ms R=7ms`（L 端 cli_A jitter 觸發 driver watchdog reclaim + reliable_*_one 8x retry loop）。每次 writeParam fail 等 300ms recv timeout、累積 ~5 attempts × (300+100) = 2s。SE3 typical Modbus reply 10-50ms，150ms 仍 3-15x headroom；極端情況（post-clearAlarm reset 期間、ramp boundary）超 150ms 由 caller retry 補。**review 重點**：worst case writeParam fail 從 500ms 降到 350ms；8 retries 上限 wall time 從 ~4.8s → ~2.4s；trade-off 偏響應速度。

- **[2026-05-14] Sadie:** 🔴 **SD76_length_meters::readRegister 沒驗 CRC、沒驗 byteCount → corrupted Modbus reply silently propagate（bench 已踩到、application 層暫時加 sanity filter 接住）** — `user_lib/SD76_length_meters.cpp` `readRegister()` 只檢查 `resp[1] == 0x03`（FC byte）後就 `memcpy` data，**沒驗 CRC、沒檢查 byteCount 是否 == count×2**。bench 2026-05-14 觀察到 RS485 偶發 bit-flip 造成的 garbled frame 通過 FC check、driver 回 success、上層拿到隨機 garbage（e.g. balance err=224cm / -214cm / 266cm 連飆，實際繩長差才 30cm 級別）。connecting frame 觸發 balance 拉滿 ±5Hz trim → 馬達瞬間左右差 5Hz → 機械應力 → SE3 OC/OL fault 連環 trigger（30s 內 18 次 clearAlarm）。**Application 層 (Crane_control_PI/main.cpp meter_loop) 已加 sanity filter 擋 > 30cm jump**，但 driver 應該從根本驗 CRC。建議 SE3 / DSZL / JC100 / CLV900 / DM2J / ZDT / etc 全部 driver 都掃一輪，sendModbus 之後讀 reply 時驗 CRC 是基本功。

- **[2026-05-14] Sadie:** 🟢 **SE3_inverter 加 `readFaultCode()` 純 additive method（[跨界: user_lib]，driver 端保留、應用層撤回自動呼叫）** — 新增 `readFaultCode(f1, f2, f3, f4)` 讀 H1007 + H1008。**注意：bench 驗到 0x1007/0x1008 register 每次回 READ_FAIL（連續 ~10 次都失敗）**。可能 (a) 不是 SE3-210 的 fault code reg、(b) 那邊只能在 motor 停止時讀、(c) `.claude/summaries/SE3_INVERTER_MODBUS_SUMMARY.md` PDF text dump 抓錯。應用層已從 keepalive 撤回（會拖長 tick 把另一邊 cli 的 SE3 也踢進 OPT），改成 raw command `se3_fault left|right` 讓 bench on-demand 試。**driver 方法本身留著、未撤**。**待 Jim review + bench 驗證 0x1007/0x1008 正確地址。**

- **[2026-05-14] Sadie:** 🟡 **CLV900_inverter 缺 null-client 防護 → 跳過 init 時 segfault** — `Crane_control_PI` 因中間管線硬體未裝（2026-05-14e changelog），CLV900 的 `init()` 被註解掉 → `client = nullptr`。隨後 `allMotionOff() → inverter.stopDecel() → writeParam → sendModbus → client->sendAndReceive` null-deref，啟動直接 segfault。**已在 `Crane_control_PI/main.cpp` 端用 `if (g_dev_clv900.load()) inverter.stopDecel();` 守起來解 blocking**，但 driver 本身應該防一手。建議 `CLV900_inverter::sendModbus` 開頭加 `if (!client) return true;`（或 writeParam/readParam 都加），這樣未 init 的 driver 對外永遠回 error code，呼叫端不需個別 guard。SD76 / SE3 / DSZL_107 driver 應該也檢查一下有沒有同樣問題（裝置未 init 時呼叫 read/write 會不會 null-deref）。

- **[2026-05-13] Sadie:** 🟢 **SE3 driver 加 `invalidateCuModeCache()` 純 additive method（[跨界: user_lib]，bench 驗證中）** — bench 觀察 cold start fine_adjust 連按 3 次第三次才動。分析：cu_mode_set_ cached as true 但 SE3 firmware 內部 CU mode 失同步；Modbus run cmd ACK 但 motor 不engagement；driver watchdog 只在 protocol fail 時計數，silent reject 不計 → 永遠不會自動 re-claim。新 `invalidateCuModeCache()` 讓應用層能主動 drop cached flag → 下次 run cmd 自動 ensureCuMode_ 重新寫 H1000=0。比 clearAlarm 輕（不觸發 H1101=H9696 / 不重置 inverter）。`cmd_align_lengths` 進場呼叫一次。詳見 changelog `2026-05-13k`。

- **[2026-05-13] Sadie:** 🟢 **SE3 driver 加 `clearAlarm()` 純 additive method（[跨界: user_lib]，純 additive，bench 已驗證）** — bench 觀察 SE3 在通訊中斷後進 OPT 異警狀態，光重連 TCP 沒清，馬達一直不動。手冊 §H1101 對照表明文 H1101=H9696 = 變頻器復位（= 00-02=2 / P.997=1）。新增 `clearAlarm()` public method：`writeParam(0x1101, 0x9696)` + sleep 200ms + reset `cu_mode_set_=false` + `comm_fail_count_=0`。應用層 `se3_keepalive_loop` 偵測 readStatusWord b7 (fault) 自動呼叫 clearAlarm，5s per-side cooldown 防止持續斷線時狂送。詳見 changelog `2026-05-13d`。**review 重點**：H1101=0x9696 是手冊明文 magic、200ms sleep 慣例（DSZL save_params 用 150ms）；reset 後 SE3 內部狀態完全回到 power-on 等價，所以 cu_mode_set_ 必須跟著 reset 才不會 stale。

- **[2026-05-09] Sadie:** 🟢 **SD76 通訊模式 mode latch — DP 寫被 firmware 吃掉（changelog 2026-05-09j）** — 上面那條 setEffectiveScale 自動挑 DP 的方案 bench 試發現 DP 寫沒進去（同類 SE3 H1000 / P.79 mode latch 行為）。User 自述 SD76 面板側操作要「00-16=非3 → 改 → 00-16=3」才能改參數，反過來 Modbus 在通訊模式下也鎖部分 config reg（DP 確認被鎖；SCAL 寫得進去）。已 revert driver auto-DP feature → setEffectiveScale 改成 preserve current DP，超出範圍清楚回 ERR + 提示面板操作步驟。`cmd_read_meter_scale` 多吐 raw_scal + raw_dp 給 bench diagnose。詳見 memory `project_sd76_panel_mode_latch.md`。**未來方向**：找 SD76 對應 SE3 H1000 的 unlock magic（如果有的話），driver 才能完全自動化 DP 改變；目前只能 panel-only。

- **[2026-05-09] Sadie:** 🟢 **SD76 SCAL bench 驗證後修正：SCAL 是除數不是乘數（見 changelog 2026-05-09h）** — 上面那條 SCAL/DP API 加完後 bench 試，發現 SD76-C 實際把 SCAL 當 K-factor / divisor 用（手冊說 "Counter Multiplier" 但行為相反）。已調整 driver `getEffectiveScale` / `setEffectiveScale` 內部公式：對外 API 還是 multiplier 語意（M = display/pulses），driver 內部換算成 1/K 寫進裝置；**setEffectiveScale 同時自動挑最高 DP 給最大精度（write_dp=true）— 後續 2026-05-09j 已 revert 此 feature，見上條**。

- **[2026-05-09] Sadie:** 🟡 **SD76_length_meters 加 SCAL/DP 校正 API（[跨界: user_lib]，待 review + bench 驗證）** — 之前計米器校正用軟體 scale + 本地檔案，Sadie 想要校正存進裝置 EEPROM、跟著裝置走（Pi 重灌不丟、移機不丟）。SD76 本來就有 SCAL（counter multiplier @ 0x0014-0x0015，6-BCD）+ DP（decimal point @ 0x0020）兩個 R/W 暫存器，driver 之前沒暴露而已。

  **新增 public API（純 additive，不動既有）：**
  - `bool readScale(uint32_t& scal_value, uint8_t& dp_upper)` — 讀 SCAL + DP（upper-display 那一格）
  - `bool writeScale(uint32_t scal_value, uint8_t dp_upper, bool write_dp = false)` — 寫 SCAL（FC 0x10），預設 preserve DP；write_dp=true 時走 read-modify-write 保留 lower-display DP byte
  - `bool getEffectiveScale(double& multiplier)` — 算 SCAL × 10^(-DP)
  - `bool setEffectiveScale(double multiplier)` — 用裝置當前 DP 反算 SCAL，超出 999999 回 ERR
  - `bool scaleByRatio(double ratio)` — read-modify-write，把當前 effective × ratio 寫回（cal_set 用）

  **新增 private helper：**
  - `writeMultipleRegisters(addr, count, data, dataLen)` — FC 0x10
  - `encodeBCD6(value, out[4])` — 鏡像 `decodeSignedBCD6` 反向，no sign byte（SCAL 永遠正）

  **review 重點：**
  - **公式假設未驗證**：以為 `display = pulse × SCAL × 10^(-DP)`，依手冊 + 常見 SD76 系列廠商設計猜的；bench 第一次寫會驗證（步驟見 changelog 2026-05-09f）。萬一不是，第一次 cal_set 會把 SD76 顯示弄歪，從面板恢復原值。
  - **是否需要 save_params**：DSZL 那種 `0xA20=40` 的明確 save 命令 SD76 手冊沒提；目前假設 FC 0x10 寫 SCAL/DP 直接落 EEPROM。bench 寫完 power-cycle 確認，若沒持久化要照 DSZL 模式加 save 補丁。
  - **DP 範圍**：`writeScale` 限 [0,5]，`getEffectiveScale` 限 [0,6]（防 overflow）；實機可能不到 5，過大值該怎麼處理？目前直接回 true（error）。
  - encodeBCD6 `out[0] = 0x00` 是把 sign byte 留 0；確認 SD76 對 SCAL 不檢查 sign bit（讀時 decoder 也只讀 raw[1..3]）

  詳見 changelog `2026-05-09f`。應用層 `Crane_control_PI/main.cpp` 已配合改寫（拆掉軟體 scale 層、cal_zero/cal_set 改用 driver、cmd_status 改吐裝置端 scale），bench 驗證流程也寫在 changelog 裡。

- **[2026-05-08] Sadie:** 🟢 **TCP_client 加 SO_KEEPALIVE + TCP_KEEPIDLE/INTVL/CNT（跨界改動，待 review）** — 加匿名 namespace `apply_keepalive(sock_fd)` helper，從 `connectToServer` 跟 `reconnectLoop` 兩處內部呼叫。Linux 設 SO_KEEPALIVE + TCP_KEEPIDLE=10s + TCP_KEEPINTVL=3s + TCP_KEEPCNT=3（dead connection 偵測 ~19s vs default ~2hr）；Windows 帶 SO_KEEPALIVE 系統預設。motivation：USR-TCP232 / NAT 中介裝置 silent disconnection 時 send 不會立刻 RST，要靠 keepalive 探包才能讓 monitor thread 發現重連。詳見 changelog `2026-05-08hh`。Review 重點：keepalive timing 是否太激進、Windows path 是否需要 WSAIoctl SIO_KEEPALIVE_VALS。

- **[2026-05-08] Sadie:** 🟢 **DSZL_107 加 `save_params()` public method（純 additive，純跨界，待 review）** — bench 確認 X518 zero / unit / 其他參數寫指令只動 RAM，每次 X518 power-cycle 會丟設定（使用者反映「每次重開要重新校零」）。手冊明文要求所有參數修改要 follow-up 寫 `0xA20 = 40` 才會持久化到 flash，driver 之前缺這支援。本次新增單一 public method `bool save_params()`（內部寫 `0xA20=40` + Sleep 150ms 等 X518 寫 flash 完成），不動既有任何 API 簽名 / 行為。`Crane_control_PI/main.cpp` 已配合：init `set_unit_kg()` + `cmd_zero_tension` 各分支都 follow-up 呼叫 `save_params()`。詳見 changelog `2026-05-08y`。請 review API + Sleep 時間（150ms 是憑手冊「>=100ms」加保險，可調）+ 是否需要在 driver 內部對 do_zero_ch1/2/all 自動 invoke save（目前是 caller 顯式呼叫，避免連續校零浪費 flash wear）。

- **[2026-05-08] Sadie:** 🟡 **DSZL-107 driver 已熱修走路 B — PR `[跨界: user_lib]` 待 Jim review（原本三選一決策請求）** —

  **進度更新（2026-05-08 同日）：** 因 crane 啟動 FATAL 阻塞 SE3/SD76/CLV900 後續測試，已照路 B（推薦選項）自行熱修。詳見 changelog `2026-05-08f`。

  **熱修內容：**
  - `user_lib/DSZL_107.h`：private member 加 `uint16_t txid_`；移除 `CRC16` 宣告；header 範例 IP/port/slave 更新為 `192.168.1.32:502 / slave 1`
  - `user_lib/DSZL_107.cpp`：`modbus_read` / `modbus_write_long` 內部框架從 RTU+CRC16 → Modbus TCP MBAP（7-byte header + PDU）；reply 重新封裝成 `[unit][fc][bc][data]` 的 RTU-like layout 讓 `parse_long(&buf[3], …)` 等 caller 不用動；加 exception code 解碼+log
  - `Crane_control_PI/main.cpp`：應用層配合 — `USR_C_IP/USR_D_IP` → `DSZL_LEFT_IP/DSZL_RIGHT_IP`、新增 `DSZL_PORT=502`、connect 用新 port、header comment 改寫
  - **Public API 完全沒動**：`init / get_tension_long / get_tension_kg / get_both_long / do_zero_* / set_unit / setScale / getScale` 簽名與行為都不變

  **review 重點請看：**
  - MBAP frame 包裝是否正確（txid 計數、length=6 for read / =11 for write multiple、proto=0、unit byte）
  - reply 重封裝（`memcpy(rx, buf+6, 3+bc)` + `rxLen = 3+bc`）是否真的相容於 caller 端的 buf[3..]+len 檢查
  - 是否該保留 RTU 路徑（為了路 C 兩支援）— Sadie 直接拿掉 RTU 是因為架構圖也跟著改了，但你如果想保留兩種 framing，再加 init mode flag 也行
  - 規範文件未動（CLAUDE.md 架構圖、motion_flow.md 對 .32/.33 還寫 USR gateway）— 待你 review driver 後一起更新

  **手冊細節（bench 實測 + PDF 抽取彙整）：**
  - X518 是 **2 通道（不是 8）**
  - 出廠 IP `192.168.1.120`（bench 收到那兩台是 .100）/ port `502` / mode register `0x644` 預設 1=Modbus TCP
  - IP 編碼 `IPH = oct1*1000 + oct2`（默認 192168）/ `IPL = oct3*1000 + oct4`（默認 1120）
  - 暫存器位址（CH1=0x0A00 / zero=0x0A20 / unit=0x0614 / slave=0x064C）跟 driver 一致
  - **`0xA20` 是多功能命令暫存器：寫 1/2/7=zero CH1/CH2/all、寫 40 (decimal) = SAVE 參數**。手冊明寫所有參數修改 / 校零 / 校準後**都要 follow up 寫 40 才會持久化** — driver 目前 `do_zero_ch1/2/all` **還沒做 follow-up save**，是已知缺口（短期 bench 用 Linux_test menu 24 的 `S` 命令補；長期建議 driver 內部加可選 `bool persist=false` 參數）
  - 廠商 0755-2890-9121（深圳，德森特）

  **bench 工具 / 實機狀態（2026-05-08 收尾）：**
  - 兩台 X518 都已配置 + power-cycle + 在新 IP `.32` / `.33` 上 ping/Modbus TCP 通
  - DSZL-107 #1：bench 確認 cell 接 CH1（CH2 浮空），4-5 kg 拉 → raw 動 ~400 counts，方向反（拉 = raw 下降）。scale ≈ 0.01125 待實機重校
  - DSZL-107 #2：未校
  - `Linux_test/main.cpp` menu 24（2026-05-08 新增）— C++ 互動式 Modbus TCP :502 工具，含 `r/l/p/R/W/S/u/z/Z/A`，可校零 / dump 全參數 / raw 讀寫 / SAVE
  - python 工具 `Linux_test/x518_probe.py` / `x518_portscan.py` / `x518_wide_scan.py` 仍保留

  詳見 `memory/project_x518_architecture_mismatch.md` + changelog `2026-05-08a/d/e/f` + work_log `2026-05-08`。
  bench 上的 X518 採集板**自帶 Ethernet 口**直接接 switch（不是 RS485 版本），出廠原生 **Modbus TCP（port 502，標準 MBAP header，無 CRC）**。但 `user_lib/DSZL_107.cpp` 是寫 **Modbus RTU + USR-TCP232 透傳**框架（CRC16、slave ID byte），跟 CLAUDE.md 拓樸表（USR_C/.32、USR_D/.33 各掛一個 X518）對齊。實機跟規範對不上。

  **🔴 升級理由（2026-05-08 同日 bench 後）：** 兩台 X518 IP 已設成 `.32` / `.33` 並插上 switch，`Crane_control_PI` 啟動立刻：
  ```
  [OK] USR_A @ 192.168.1.30:4001
  [OK] USR_B @ 192.168.1.31:4001
  [FATAL] connect USR_C 192.168.1.32:4001 failed (DSZL left)
  ```
  X518 真的在 .32 但講 :502 不是 :4001 → driver framing 不換不行。Crane 整支起不來。

  **三條路請選一：**
  - **路 A**（driver 不動）：把 X518 接到 RS485 介面，前面加 USR-TCP232，IP 設 .32/.33 — 配線多一層、要再買 USR
  - **路 B**（推薦）：改 DSZL_107 driver 走 Modbus TCP — 拿掉 CRC16、加 MBAP header、port 502、X518 直插 switch 即可
  - **路 C**：driver 加 init mode flag，兩種都支援

  **手冊確認（bench 實測 + PDF 抽取）：**
  - X518 是 **2 通道（不是 8）**
  - 出廠 IP `192.168.1.120`（bench 收到那兩台是 .100）/ port `502` / mode register `0x644` 預設 1=Modbus TCP
  - IP 編碼 `IPH = oct1*1000 + oct2`（默認 192168）/ `IPL = oct3*1000 + oct4`（默認 1120）
  - 暫存器位址（CH1=0x0A00 / zero=0x0A20 / unit=0x0614 / slave=0x064C）跟 driver 寫的一致 — 改 framing 就好，business logic 不用動
  - **`0xA20` 是多功能命令暫存器：寫 1/2/7=zero CH1/CH2/all、寫 40 (decimal) = SAVE 參數**。手冊明寫所有參數修改 / 校零 / 校準後**都要 follow up 寫 40 才會持久化** — driver 目前的 `do_zero_ch1/2/all` 沒 follow save，重啟後會 lose
  - 廠商 0755-2890-9121（深圳，德森特）

  **bench 工具：**
  - `Linux_test/x518_probe.py` / `x518_portscan.py` / `x518_wide_scan.py`（python，Modbus TCP 直戳）
  - `Linux_test` menu 24（2026-05-08 新增）— C++ 互動式 Modbus TCP :502 工具，含 `r/l/p/R/W/S/u/z/Z/A`，可校零 / dump 全參數 / raw 讀寫 / SAVE。**bench 已用此工具把兩台 X518 IP 改成 .32/.33 + unit 切到 kg + SAVE**

  **當下狀態（2026-05-08 收尾）：**
  - 兩台 X518 都已配置 + power-cycle + 在新 IP `.32` / `.33` 上 ping/Modbus TCP 通
  - DSZL-107 #1：bench 確認 cell 接 CH1（CH2 浮空），4-5 kg 拉 → raw 動 ~400 counts，方向反（拉 = raw 下降）。scale ≈ 0.01125 待實機重校
  - DSZL-107 #2：未校
  - Crane 啟動 FATAL 詳見 work_log 2026-05-08 條目

  詳見 `memory/project_x518_architecture_mismatch.md` + changelog `2026-05-08a/d/e`。

- **[2026-04-22] Sadie:** 🟡 `user_lib/TCP_client.cpp:175` 和 `TCP_server.cpp:175` 的 `send(sock, buf, len, 0)` 沒帶 `MSG_NOSIGNAL`，Linux 下對已關閉對端寫入會送 SIGPIPE 把 process 殺掉。現場曾踩到：washrobot 跑到一半對端斷，shell 顯示 `Broken pipe` 後 process 死。已在三個 main.cpp（washrobot / Crane_control_PI / Crane_easy_PI）加 `signal(SIGPIPE, SIG_IGN)` 先擋住，但長期建議 user_lib 的 send 統一改用 `MSG_NOSIGNAL`（Linux），Windows 用 `#ifdef` 守衛。這樣 Linux_test 或未來新 main.cpp 即使忘記加 signal ignore 也不會中招。跨界改動標 `[跨界: user_lib]`，不阻塞我目前工作。

- **[2026-04-30] Sadie:** 🟡 `user_lib/ZDT_motor_control.cpp:506` 的 `trigger_sync_move()` 是 Modbus 廣播（slave addr 0x00）— 依協定廣播**不會**有回應，但實作 `return resp.empty();` 把「沒回應」當成 error → **永遠回傳 true，即使廣播成功**。導致呼叫端如果 check return 會誤判全部失敗。現場症狀：body extend 實際成功（馬達真的有動）但 log 一直印「trigger_sync_move FAIL」。我先在 `WashRobot.cpp` 把回傳值忽略並加 TODO 註解，但根本應該在 driver 改成「廣播 send 成功就 return false」（或加參數 `expect_response=false`）。跨界改動標 `[跨界: user_lib]`，不阻塞我目前工作。

- **[2026-05-07] Sadie:** 🟡 **新增 `user_lib/SE3_inverter.{h,cpp}` 跨界 review 請求** — 應用戶要求新增士林 SE3 系列變頻器 driver，取代 ZS_DIO_R_RLY 8CH 繼電器，作為吊機左右鋼索的 winch 馬達控制（2 台 SE3，各 1 台對應左/右繩）。
  **資料來源：** `D:\洗窗戶機器人\電控設備資料\變頻器SE3系列操作手冊 V1.03.pdf`（PDF 中文 GBK 字型轉文字殘缺，但 Modbus chapter 5.8 + 暫存器表可解）
  **核心暫存器：** `0x1101` 控制（bit0 stop / bit1 STF / bit2 STR / bit7 MRS）/ `0x1002` 頻率 RAM（0.01 Hz 單位）/ `0x100A` 輸出頻率 / `0x100B` 電流 / `0x100C` 電壓
  **Function code：** FC03 read / FC06 write single
  **API 模式：** 仿 `CLV900_inverter` — `init(client&, slave)` / `runForward/Reverse/stopDecel/emergencyStop` / `setFreqHz` / `readOutputFreqHz` 等
  **已加：** Crane_control_PI.vcxproj 編譯項；ZS_DIO_R_RLY 從 vcxproj 移除（class 保留供未來使用）；CLAUDE.md 架構圖 + driver 表更新
  **未做 / 待硬體驗證：**
    - USR2 IP（暫定 .31，待硬體確認）
    - SE3 keypad 預先設定（站號 1/2、波特率、Modbus 控制源、watchdog timeout）
    - 方向約定（pay_out=forward 是猜的，實機測會知道）
    - 實機 Modbus 暫存器位址驗證
  詳見 changelog `2026-05-07b`。請 review API + 確認 register 解讀正確。

- **[2026-05-06] Sadie:** 🟡 **新增 `user_lib/DSZL_107.{h,cpp}` 跨界 review 請求** — 應用戶要求先補完吊機鋼索張力 driver（slave 5 左 / slave 6 右）。實際 Modbus 對話對象是 X518 雙通道採集板（讀 DSZL_107 load cell 類比訊號），但 class 名取 DSZL_107 對齊 CLAUDE.md 角色名稱。
  **資料來源：** `D:\洗窗戶機器人\電控設備資料\張力感測器\x518多通道数据采集器操作手册v1.1.pdf`（PDF 中文 GBK 字型部分轉文字殘缺，但 Modbus 暫存器 + 範例 frame 可解）
  **核心暫存器：** 0x0A00 area（FC03 讀 4 reg = 2 long = CH1+CH2 raw）/ 0x0A20（FC10 寫校零）/ 0x0614（單位選擇 1=t 2=kg…）/ 0x064C（slave ID）/ 0x0636（baud）
  **已加：** Crane_control_PI.vcxproj 編譯項；CLAUDE.md 從「待實作」搬到「使用中」
  **未做：** 應用層串接（main.cpp 還沒加 DSZL_107 實例）/ scale factor 實機校正 / byte order 驗證（PDF 提到有 BE 跟 word-swap 兩種，預設用 BE）
  詳見 changelog `2026-05-06m`。請 review API + 是否符合 user_lib 規範（命名、log_utils、return convention）。

### → washrobot 實機

_（目前無）_

### → crane 實機

_（目前無）_

### → 前端

_（目前無）_

## 已處理

_（目前無）_
