# Work Log

## 🔴 待辦總表（單一權威，2026-08-27 由 mailbox / ONBOARDING 併入）

> 📌 **這張表是本專案唯一的彙整待辦清單。** 2026-08-27 專案改為單人開發，退休了多人協作
> 的分工機制，原本散在三個地方的未結案項目全部併到這裡：
>
> | 來源 | 開放項目數 | 現況 |
> |---|---|---|
> | `.claude/mailbox.md`（`### → 架構（Jim）`，2026-04-22 ~ 2026-05-14） | **16** | 已退休，檔案改為墓碑 |
> | `ONBOARDING.md` `## ⚠️ 尚未解決 / 待處理事項` | **6** | 該節改為指標，其餘章節不動 |
> | 本檔各日期條目的「待確認 / 尚未處理 / 待完成」段（2026-04-23 ~ 2026-08-17） | **44** | 原文保留在下方各日期條目中 |
> | **合計** | **66 筆 → 表中 60 列** | **66 筆全部入表，無遺漏** |
>
> **66 筆為什麼是 60 列（沒有任何一筆被丟掉）：**
> - **併 2→1**：ONBOARDING §1 與 work_log 2026-07-15 的 `abort_flag` 是同一件事
> - **併 7→1**：work_log 2026-07-07 / 07-15 / 07-21 / 07-22 / 07-23 的「改動未編譯 / 未部署驗證 / 未 push / remote build 驗證」共 7 筆同性質，合成一列
> - **拆 1→2**：mailbox 2026-05-08 的 DSZL 條目拆成 `save_params()`（已修）與 `do_zero_* 是否自動 save`（待決）兩列
>
> ⚠️ **mailbox 的「已處理」段是空的** — 那 16 筆從 2026-04/05 開出來之後從來沒有正式結案過，
> 最久的已經放了 **4 個月**。表格每一列都帶原始日期就是為了讓這個「債齡」看得見，
> 不要因為併檔就把時間資訊洗掉。
>
> **現況欄定義：**
> - **未修** = 2026-08-27 實際打開原始碼確認過，問題仍在
> - **已修** = 已在原始碼中確認修好、或該功能路線已整個移除而自動作廢
> - **待查** = 需要 bench / 實機 / 部署後才能判定，或條目本身太模糊無法定位
> - ✔ = 這次有實際比對原始碼；無 ✔ 者是靠 changelog / git 狀態推斷
>
> **優先度說明：** 🔴/🟡/🟢 沿用來源的原始標記；ONBOARDING 與 work_log 原本沒有標記的，
> 依「是否為安全性 / 是否會讓系統永久卡死」補上。

### 待辦表

| 優先度 | 項目 | 涉及檔案 | 現況 | 來源與原始日期 |
|---|---|---|---|---|
| 🔴 | `readRegister()` 不驗 reply CRC、不驗 byteCount → 壞掉的 Modbus reply 被當有效值往上傳（bench 已造成實體損害，詳見下方） | `user_lib/SD76_length_meters.cpp` | **未修** ✔ | mailbox 2026-05-14 |
| 🔴 | 緊急收繩按鈕實際上**沒有張力保護**，跟 `motion_flow.md` §8 的安全性描述相反 | `Crane_control_PI/main.cpp` `cmd_manual()` | **未修** ✔ | ONBOARDING §6 |
| 🔴 | `cmd_side_measured()` 進場沒重置 `abort_flag` → 被 stop 過一次後所有 v2 step 指令永久回 `ERR aborted`，只能重開程式 | `Crane_control_PI/main.cpp:2800` | **未修** ✔ | ONBOARDING §1 ＋ work_log 2026-07-15 |
| 🔴 | DSZL-107 scale factor 仍是 placeholder（driver `-0.01` / 廠商說 `0.02`），張力門檻等於沒有基準；#1 只有 bench 手拉估值、#2 完全沒校 | `user_lib/DSZL_107.cpp`、`Crane_control_PI/main.cpp` | **未修** ✔ | work_log 2026-05-07 ＋ 2026-06-02 |
| 🔴 | 安全盤點高優先兩項未做：`cmd_hold` 與 motion 互斥、左右繩長差超標 abort | `Crane_control_PI/main.cpp` | **未修** ✔（原始碼註解仍留 TODO） | work_log 2026-05-08 |
| 🟡 | `trigger_sync_move()` 是 Modbus 廣播（slave 0x00）不會有回應，卻以 `return resp.empty();` 收尾 → 廣播成功也永遠回報失敗 | `user_lib/ZDT_motor_control.cpp:506` | **未修** ✔ | mailbox 2026-04-30 |
| 🟡 | `send(sock, buf, len, 0)` 沒帶 `MSG_NOSIGNAL`，Linux 下對已關閉對端寫入會 SIGPIPE 殺 process（目前靠各 `main.cpp` 的 `signal(SIGPIPE, SIG_IGN)` 擋著） | `user_lib/TCP_client.cpp`（2 處）、`user_lib/TCP_server.cpp`（1 處） | **未修** ✔ | mailbox 2026-04-22 |
| 🟡 | `CLV900_inverter` 缺 null-client 防護：跳過 `init()` 時 `client == nullptr`，`sendModbus` 直接 null-deref segfault（應用層已用 `g_dev_clv900` 守起來，driver 本身沒守） | `user_lib/CLV900_inverter.cpp` | **未修** ✔ | mailbox 2026-05-14 |
| 🟡 | `TCP_client` 缺 `SO_ERROR` 驗證 → 影響 reconnect 的邊界 case | `user_lib/TCP_client.cpp` | **未修** ✔（全檔無 `SO_ERROR`） | work_log 2026-06-09 |
| 🟡 | MH300 實機必驗清單未跑：方向映射、電流 scale、2101H run bit、fault code | `Crane_control_PI/main.cpp`（`VFD_DIR_*` 巨集）、`.claude/mh300_migration_plan.md` | **未修** ✔（註解仍寫 `RE-VERIFY on MH300`） | work_log 2026-07-07 |
| 🟡 | 5 個 `.vcxproj.user` 被 git 追蹤 → 不同 bench 的 Remote Target 互相覆蓋（Connection Manager 顯示空白） | `Crane_control_PI/`、`Linux_test/`、`cleaning_arm/`、`facade_cleaning_v2/`、`windows_test/` | **未修** ✔（仍 tracked，`.gitignore` 未加） | work_log 2026-07-15 |
| 🟡 | 沒有 hot re-init：裝置 flag 只在啟動時設一次，硬體中途修好要重開 crane | `Crane_control_PI/main.cpp` | **未修** | work_log 2026-05-08 |
| 🟡 | 沒有任何機制偵測「M2 被重新安裝過」；重裝後若位置落在 ±1.5 rad 內，INIT 會**靜默**移到錯的 CENTER | `cleaning_arm/main_api.cpp:1992-2028` | **未修** | work_log 2026-08-17 |
| 🟡 | `LR_CALIBRATE` 自動雙向尋邊不可靠（假觸發撞牆、或衝很遠都撞不到），目前只能走手動流程 | `cleaning_arm/main_api.cpp` | **未修** | work_log 2026-08-17 |
| 🟡 | 同步步伐（`step_down_sync`/`step_up_sync`）沒有地面淨空 / 障礙檢查，完全信任使用者輸入的 cm | `user_lib/WASH_ROBOT.cpp` `do_step_sync_` | **未修** | work_log 2026-07-22 |
| 🟡 | 規範文件架構圖與程式碼脫節：`motion_flow.md` §2 仍寫 v1 的「DM2J×5 / ZDT×9 / 三區真空」，crane 拓樸也已再排過 | `.claude/motion_flow.md` §2（`CLAUDE.md` 另有任務同步處理中） | **未修** ✔ | ONBOARDING §5 |
| 🟡 | DSZL-107 熱修走路 B（RTU+CRC16 → Modbus TCP MBAP）的 review 沒做完，且當時說「規範文件未動、待 review 後一起更新」 | `user_lib/DSZL_107.{h,cpp}` | driver **已修** ✔（MBAP 已在 code）／文件 **未修** | mailbox 2026-05-08 |
| 🟡 | SD76 SCAL/DP 校正 API 的公式假設（`display = pulse × SCAL × 10^(-DP)`）、是否需要 save_params、DP 上限行為都還沒 bench 驗證 | `user_lib/SD76_length_meters.cpp` | API **已修** ✔／驗證 **待查** | mailbox 2026-05-09 |
| 🟡 | 新 driver `SE3_inverter` 的 review 與硬體驗證未結案：USR2 IP、SE3 keypad 預設（站號/波特率/控制源/watchdog）、方向約定、暫存器位址 | `user_lib/SE3_inverter.{h,cpp}` | **待查** | mailbox 2026-05-07 |
| 🟡 | 新 driver `DSZL_107` 的 review 未結案：scale factor 實機校正、byte order（BE vs word-swap）驗證 | `user_lib/DSZL_107.{h,cpp}` | 應用層串接 **已修**／校正驗證 **待查** | mailbox 2026-05-06 |
| 🟡 | crane 端偶發 `ERR meter_left_read_fail` + TCP 每 500ms reconnect，根因未知（已排除兩個假設），workaround 是重開 crane 程式 | `Crane_control_PI/main.cpp:1367` `meter_read_robust()` | **待查** | ONBOARDING §3 |
| 🟡 | follower 側 IMU 校平疑似被切到 `meter` 模式導致機體歪斜；`follower_use_imu_==false` 的路徑**完全靜默**，一行 log 都不印 | `user_lib/WASH_ROBOT.cpp:6366`、`WASH_ROBOT.h:881` | **待查**（走法已全面改 sync，但後端 raw command 預設仍是 `alt`，仍走得到） | ONBOARDING §2 |
| 🟡 | 2026-07 那整批改動**從未編譯 / 部署驗證**（本機無法 remote build）：TCP_client 殭屍連線修復要驗自癒、WASH_ROBOT 安裝幾何常數、同步步伐、partial-seal 判準、crane 端 `Crane_control_PI` 建議先單獨 build 綠燈；`1829964` 等 commit 仍在本機 main **未 push** | `user_lib/TCP_client.cpp`、`user_lib/WASH_ROBOT.{h,cpp}`、`Crane_control_PI/main.cpp`、`facade_cleaning_v2/main.cpp`、`web_backend/public/*` | **待查** | work_log 2026-07-07 / 07-15 / 07-21 / 07-22 / 07-23（7 筆合併） |
| 🟡 | 同步步伐的 IMU 差動微調**方向**（sign convention）沒實機驗證過，第一次上機要小角度有人看著 | `user_lib/WASH_ROBOT.cpp` `do_step_sync_` | **待查** | work_log 2026-07-22 |
| 🟡 | 水平校正整合（IMU roll ＋ 左右繩長差 tol）在 v2 step 收尾只留 TODO | `user_lib/WASH_ROBOT.cpp` | **待查** | work_log 2026-07-07 |
| 🟡 | v1 舊 body 用 `#if 0` 包起來當 reference，說好 bench 驗證 v2 綠燈後再硬刪 — 還沒刪 | `user_lib/WASH_ROBOT.cpp` | **待查** | work_log 2026-07-07 |
| 🟡 | Realign Layer 2（Phase 2 in_window 期間 cycle valve OFF/ON）設計討論完但未實作 | `user_lib/WASH_ROBOT.cpp` | **待查** | work_log 2026-06-02 |
| 🟡 | `vacuum_check` 重複跑兩次浪費 30s／attach（提了 α + δ 兩方案，未選） | `user_lib/WASH_ROBOT.cpp` | **待查** | work_log 2026-06-09 |
| 🟡 | `arm_cmd_` INIT recv timeout 真因沒查清楚（看起來是 motor_api 端會卡）；60s 是否要再拉長待決 | `user_lib/WASH_ROBOT.cpp`、`cleaning_arm/main_api.cpp` | **待查** | work_log 2026-06-09 |
| 🟡 | Scripted run / Snowball 防護 A+B+C / Water inlet 防漏三批功能**全部沒實機驗證過** | `user_lib/WASH_ROBOT.{h,cpp}`、`web_backend/public/*` | **待查** | work_log 2026-06-09 |
| 🟡 | 2026-06-02 那批 fix 的實機觀察清單未跑完：`wall_mm=330` 是否平貼、anchor vacuum check 會不會誤報、`cmd_recover` vacuum_check 的使用者處置、BAL `kp=1.0` 是否改善震盪、`cmd_status` 1Hz rate-limit 是否減半 JC100 timeout | `user_lib/WASH_ROBOT.{h,cpp}`、`Crane_control_PI/main.cpp` | **待查** | work_log 2026-06-02 |
| 🟡 | crane 端 placeholder 常數與未驗事項：4 個 gateway IP 對應、SE3 keypad 預設、CLV900 RPM↔Hz 公式（等馬達極數）、`UP_STOP_TOTAL_KG_DEFAULT=50` / `SE3_HOLD_HZ=20` 等 | `Crane_control_PI/main.cpp` | **待查**（拓樸 2026-08-27 又重配過，需重新對照） | work_log 2026-05-07 |
| 🟢 | SD76 通訊模式 mode latch：DP 寫入被 firmware 吃掉（同類 SE3 H1000 / P.79 行為），driver 已 revert auto-DP、改成 preserve current DP。**未來方向**：找 SD76 對應的 unlock magic 才能完全自動化改 DP，目前只能面板操作 | `user_lib/SD76_length_meters.cpp` | **待查** | mailbox 2026-05-09 |
| 🟢 | `SE3_inverter::readFaultCode()` 已加，但 bench 驗到 `0x1007`/`0x1008` 連續 ~10 次都 READ_FAIL — 位址是否正確待驗（三個可能原因見下方） | `user_lib/SE3_inverter.cpp:381` | method **已修** ✔／位址 **待查** | mailbox 2026-05-14 |
| 🟢 | `DSZL_107::do_zero_ch1/2/all()` 目前不會自動 follow-up `save_params()`（刻意設計，避免連續校零磨損 flash），是否要加可選 `persist` 參數待決 | `user_lib/DSZL_107.cpp:304-306` | **待查** ✔ | mailbox 2026-05-08 |
| 🟢 | `arm_sweep_monitor` SUSTAINED 0.2→0.4（防 false positive，代價是可能漏接弱接觸 obstacle）— 待 user 拍板 | `user_lib/WASH_ROBOT.cpp` | **待查** | work_log 2026-06-09 |
| 🟢 | PQW CH6 verify fail「gave up after 3 retries」最後沒人 catch，downstream 沒擋住 — 要確認是不是真的有 propagation 問題 | `user_lib/WASH_ROBOT.cpp`、`user_lib/PQW_IO_16O_RLY.cpp` | **待查** | work_log 2026-06-02 |
| 🟢 | `DM2J:14` writeMulti no response（cli_22_ contention 偶發，driver 自己 retry 成功）— 要不要監控連續失敗率 | `user_lib/DM2J_RS570.cpp` | **待查** | work_log 2026-06-02 |
| 🟢 | arm M1 `verify_deploy` delta 漸增（RIGHT 從 0.797 漂到 0.910，delta −0.114 / tol 0.150，接近邊緣） | `cleaning_arm/main_api.cpp` | **待查** | work_log 2026-06-02 |
| 🟢 | `cmd_recover` force escape（sensor 假報故障時 user 會卡死）— 設計討論完，暫不做，先看誤報率 | `user_lib/WASH_ROBOT.cpp` | **待查** | work_log 2026-06-02 |
| 🟢 | Tool 物理裝歪：若 `ARM_CLEAN_WALL_MM=330` 還是不平貼 → 拆 tool mount 重裝 | 機械（非程式） | **待查** | work_log 2026-06-02 |
| 🟢 | BAL 討論但未落地：機體重心本來偏 L，應追求「兩繩同步收放」而非「等張力」；kp 1.0 不夠可能要加 base offset | `Crane_control_PI/main.cpp` | **待查** | work_log 2026-06-02 |
| 🟢 | 跨越障礙物步幅建議公式（`remaining + max_height + 20 + 5`）只驗過算式邏輯，沒驗過「照這個步幅走真的跨得過去」。跨障礙物按鈕本身仍保留 | `user_lib/WASH_ROBOT.{h,cpp}` | **待查**（深度相機這個**輸入來源**已移除） | work_log 2026-07-22~23 |
| 🟢 | SE3 `sendModbus` recv timeout 300→150ms（worst case writeParam fail 500→350ms、8 retry wall time ~4.8s→~2.4s） | `user_lib/SE3_inverter.cpp:121` | **已修** ✔（已套用，review 請求作廢） | mailbox 2026-05-14 |
| 🟢 | SE3 `invalidateCuModeCache()` 純 additive method（解 cold start `fine_adjust` 連按 3 次第三次才動） | `user_lib/SE3_inverter.cpp:297` | **已修** ✔ | mailbox 2026-05-13 |
| 🟢 | SE3 `clearAlarm()` 純 additive method（H1101=H9696 變頻器復位，解通訊中斷後卡 OPT） | `user_lib/SE3_inverter.cpp:312` | **已修** ✔ | mailbox 2026-05-13 |
| 🟢 | SD76 SCAL 是**除數**不是乘數（手冊寫 "Counter Multiplier" 但行為相反），driver 內部換算成 1/K | `user_lib/SD76_length_meters.cpp` | **已修** ✔ | mailbox 2026-05-09 |
| 🟢 | `TCP_client` 加 `SO_KEEPALIVE` + `TCP_KEEPIDLE=10s`/`INTVL=3s`/`CNT=3`（dead connection 偵測 ~19s vs 預設 ~2hr） | `user_lib/TCP_client.cpp:24` `apply_keepalive()` | **已修** ✔ | mailbox 2026-05-08 |
| 🟢 | `DSZL_107::save_params()`（寫 `0xA20=40` + 150ms sleep，解 X518 power-cycle 掉設定） | `user_lib/DSZL_107.cpp:314` | **已修** ✔ | mailbox 2026-05-08 |
| 🟢 | `DM2J_RS570` 多處 bug：`read_status` 讀 2 reg 應讀 1、完工檢查查錯 word、`print_status` HOME_DONE mask、`motor_enable/disable/save_params` 只宣告沒實作 | `user_lib/DM2J_RS570.cpp` | **已修** ✔（mask 改 `0x0040`、`0x000F` enable、`0x2211→0x1801` save 都已落地） | work_log 2026-04-24 |
| 🟢 | 清掉 `Linux_test` 的 `dm2j_manual_enable` helper（那段寫 `0x1111` 其實是 reset alarm 不是 enable） | `Linux_test/main.cpp` | **已修** ✔（符號已不存在） | work_log 2026-04-24 |
| 🟢 | GUI 按鈕對應（右/左閥、單側繩、step） | `web_backend/public/*` | **已修**（2026-08-26~27 多輪 GUI 改版已重做） | work_log 2026-07-07 |
| 🟢 | arm 清洗 sweep 因手臂未裝而 deferred | `user_lib/WASH_ROBOT.cpp` | **已修**（2026-07-24 手臂實裝後接回 `do_step_sync_rail_sweep_`） | work_log 2026-07-07 |
| 🟢 | `frame_capture/depth_cam_service.py` / `depth_reflection_bench.py` / `depth_cam_test_client.py` 三個檔 git untracked | `frame_capture/` | **已修** ✔（三個檔已進版控） | work_log 2026-07-22~23 |
| 🟢 | D435i 深度相機**戶外強光**未測（曾是換相機決策的最大未知數） | `frame_capture/` | **已修（作廢）**：2026-08-26e 移除深度避障 GUI、2026-08-27c 攝影機介面永久移除 | ONBOARDING §4 |
| 🟢 | `remaining_travel_cm` 用新常數（`LEAD_OFFSET=32cm`/`STANDOFF=56cm`）後沒重新實機驗證 | `user_lib/WASH_ROBOT.h` | **已修（作廢）**：攝影機路線已移除 | work_log 2026-07-22~23 |
| 🟢 | 一般（非鏡面）窗戶場景的窗框辨識沒測過 | `frame_capture/obstacle_detector.py` | **已修（作廢）**：攝影機路線已移除 | work_log 2026-07-22~23 |
| 🟢 | `scripts/wr.sh` 的 cam1/cam2 window 還註解著，攝影機接回去要取消註解 | `scripts/wr.sh` | **已修（作廢）**：攝影機永久不接 | work_log 2026-07-21 |
| 🟢 | `camera_obstacle_plan.md` 還沒加 motion mode section | `.claude/camera_obstacle_plan.md` | **已修（作廢）**：攝影機路線已移除 | work_log 2026-06-02/03 |
| 🟢 | v1 現場未解 5 項：PQW 寫 relay 不成功、DM2J slave ENABLE bit 沒亮、ZDT slave 6 堵轉、推桿距離待細調、FrameAnalyzer C++ 沒寫 | v1 硬體 | **已修（多數作廢）**：v2 已無 DM2J 滑軌/輪組，吸盤 slave 2026-08-27 改 5-8，`user_lib/FrameAnalyzer.cpp` 已存在 | work_log 2026-04-23 |
| 🔴 | `run_depth_avoid` 後端仍活著，且偵測到大障礙物時會**自行改走 `cross` 步伐**：`run_depth_avoid` / `depth_avoid_continue` / `depth_avoid_stop` 三個指令仍 dispatch 到真實實作，而同輩的 `obstacle_detect`/`run_avoid`/`obstacle_response` 早已硬關成 `ERR removed_in_v2`。前端已於 2026-08-27c 移除 → **現在完全沒有 UI 提示** | `facade_cleaning_v2/main.cpp:184-189` | **未修** ✔ | `camera_obstacle_plan.md` 稽核 2026-08-27（changelog 2026-08-26e） |
| 🟡 | `scripts/wr.sh:67` 仍會啟動 `depth_cam_service.py`（depth window）。changelog `2026-08-26e` 結尾寫「可以把那個 window 註解掉——尚未變更，待 user 決定」，至今未決 | `scripts/wr.sh:67` | **未決** ✔ | `camera_obstacle_plan.md` 稽核 2026-08-27 |
| 🔴 | MH300 keypad commissioning 參數表**是唯一副本**（只記在 plan 檔裡，沒有第二份）：站號 `09-00`=1/2、`09-01`=9.6、`09-04`=12（8N1 RTU，與 SD76 共用同一條 bus）、`00-20`=1（頻率來源 RS-485）、`00-21`=2（運轉來源 RS-485）、`07-00~04` DC brake／煞車截波（配 BR300W070-S 制動電阻）、`01-12`/`01-13` 加減速時間——**左右必須對齊，否則不同步停車** | `.claude/mh300_migration_plan.md` Phase 0 | **待用** | `mh300_migration_plan.md` Phase 0 |
| 🔴 | SE3 `P.79` 切換程序與「`P.5` 必為 0」**是唯一副本**，而且 bench 目前**仍在跑 SE3**（`Crane_control_PI/main.cpp:116` `#define CRANE_VFD_IS_SE3 1`），不是已作廢的舊文件：改 `P.79` 前須先停馬達、解除 OPT，再 `P.79=3 → 2 → 6`（防 latch 卡住）；`P.5`（multi-speed）必須保持 0，否則多段速會覆蓋 H1002 頻率命令 | `.claude/se3_mode6_migration_plan.md` §1.1、`Crane_control_PI/main.cpp:116` | **有效** ✔ | `se3_mode6_migration_plan.md` §1.1 |
| 🔴 | QX-DO24 PWM（螺旋槳 ESC 控制）目前停用，`PWM_SLAVE=6` 撞 JC100 真空計。停用註解的理由是「nothing in the automatic gait depends on it (web panel only)」——這對舊架構成立，**對新架構不成立**：新架構設計文件寫明貼附序列的第一步就是「先讓螺旋槳把機體壓穩」。同 bus 的 slave 1-8 已被吸盤佔滿（`Linux_test/main.cpp:891` feet `{1,2,3,4}` / body `{5,6,7,8}`）、10/11 為 DY-500（未安裝），需挑一個空號並對照 `cli_22_` 上所有裝置確認不撞號 | `user_lib/WASH_ROBOT.cpp:175-192`（`PWM_ENABLED`） | **未修（阻塞新架構）** ✔ | changelog 2026-08-27h ＋ 新架構設計 2026-08-27 |

---

### 🔴 詳細（不要只看表格）

**🔴 SD76 `readRegister()` 不驗 CRC（mailbox 2026-05-14，唯一造成過實體損害的一條）**

`readRegister()` 只檢查 `resp[1] == 0x03`（FC byte）就 `memcpy` data，**不驗 CRC、也不檢查
byteCount 是否等於 `count × 2`**。2026-05-14 bench 觀察到 RS485 偶發 bit-flip 造成的 garbled
frame 通過 FC check、driver 回 success、上層拿到隨機 garbage：balance err 連飆 `224cm` /
`-214cm` / `266cm`，而實際繩長差只有 30cm 級別。連鎖反應是——garbage 觸發 balance 把 trim
拉滿 ±5Hz → 兩顆馬達瞬間差 5Hz → 機械應力 → SE3 OC/OL fault 連環觸發，**30 秒內 clearAlarm 18 次**。

應用層（`Crane_control_PI/main.cpp` `meter_loop`）已加 sanity filter 擋 >30cm 的跳變接住症狀，
但那是止血，driver 應該從根本驗 CRC。

📌 **同一條 mailbox 還附帶一個更大的行動項**：建議把 SE3 / DSZL / JC100 / CLV900 / DM2J / ZDT
**全部 driver 掃一輪**，確認 `sendModbus` 之後讀 reply 時都有驗 CRC——這是基本功。這一輪掃描
還沒做。

**🔴 緊急收繩按鈕沒有張力保護（ONBOARDING §6，安全性，與文件相反）**

`motion_flow.md` §8「緊急收繩按鈕」寫著「張力保護仍在：Crane C++ 端的 tension_alarm safety
monitor 不受 GUI 模式影響，超張力照樣強制停」。**實際程式碼相反**：🆘 緊急收繩按鈕送的是
`retract_left/right on`，走 `cmd_manual()`，而 `cmd_manual()` 的原始碼註解自己就寫
「manual = 不受張力感測門檻限制」，全函式沒有任何一處呼叫 `tension_safety_check_values`
（2026-08-27 逐行確認仍是如此）。真正有背景張力監控（`hold_loop()` → 超標 `hold_all_off()`）
保護的是吊機區域一般的「↑/↓ 拉繩」按鈕（`cmd_hold()`），不是緊急收繩按鈕。

兩個方向擇一，**已口頭跟 user 提過但沒得到答覆**：
(a) 這是故意的——緊急狀況不該被軟體張力門檻卡住，那要**改文件**；
(b) 這是真的安全缺口，要把張力檢查**補進 `cmd_manual`**。
下次直接問要走哪條，不用重查程式碼。

⚠️ 注意這條跟表中 🔴「DSZL scale 仍是 placeholder」互相放大：就算補了張力保護，
scale 沒校正的話門檻本身也不可信。

**🔴 `cmd_side_measured` 沒重置 `abort_flag`（ONBOARDING §1 ＋ work_log 2026-07-15）**

症狀：washrobot 跑 script 到一半按 stop，之後不管做什麼都變成「無法連線吊機」，必須重開整支
`Crane_control_PI` 才恢復。根因：`abort_flag` 被 `cmd_stop()` 或 watchdog timeout 設成 `true`
之後，`cmd_side_measured` 進場**沒有**把它重置回 `false`——它的三個兄弟函式
`motion_rope`(2233)/`cmd_roll_correct`(2535)/另一個 MotionScope 函式(2661) 都有
`abort_flag = false;` 這行，唯獨這個漏掉（2026-08-27 確認：那三處分別在 line 2267 / 2599 / 2728，
`cmd_side_measured`（line 2800 起）仍然沒有）。因為 v2 幾乎所有跟吊機的互動都經過
`cmd_side_measured`，迴圈第一行 `if (abort_flag.load()) { ...; break; }` 會讓馬達剛啟動就中止、
回 `ERR aborted`，**永久性直到重開程式**（static 變數重新初始化）。

修法：在 `cmd_side_measured` 拿到 `motion_mtx` / `MotionScope` 之後補一行 `abort_flag = false;`。
該函式 2026-07-14 已補上 `motion_mtx` try_lock 保護，補這行不會有並發風險。

**🔴 DSZL-107 scale factor 仍是 placeholder**

driver 目前是 `-0.01`（符號要翻：bench 觀察拉 = raw 下降），廠商給的是 `0.02`，bench 手拉
4~5kg → raw 動 ~400 counts 推估 ≈ `0.01125 kg/count`。DSZL-107 #1 只有這個 bench 估值、
**#2 完全沒校過**。實機接上鋼索後拉的方向跟 bench 不一樣，一律要重校。要先確認 cell 規格
（50kg / 100kg）配對，再掛標準重物實測。`TENSION_MAX_KG_DEFAULT=100` 這類門檻都要等
scale 驗證後才能收緊（`Crane_control_PI/main.cpp` 註解已寫明）。

**🔴 安全盤點高優先兩項（work_log 2026-05-08）**

2026-05-08 的 graceful degradation 做完後，安全措施盤點裡標 🔴 的兩項沒做，理由是「等你確認
threshold 再實作」：① `cmd_hold` 與 motion 互斥（避免 hold 跟 motion 同時驅動同一顆 VFD）、
② 左右繩長差超標 abort。`cmd_side_measured` 上方的註解至今仍留著
`GUI motion-active state is a TODO (see cmd_hold)`。

---

### 需要保留的細節（🟡/🟢，條列存查）

- **ZDT `trigger_sync_move()`（mailbox 2026-04-30）** — 現場症狀：body extend 實際成功（馬達真的有動）
  但 log 一直印「trigger_sync_move FAIL」。已在 `WashRobot.cpp` 忽略回傳值 + 加 TODO 註解。
  根本修法：廣播 send 成功就 `return false`（本專案慣例 false=成功），或加參數 `expect_response=false`。
- **`MSG_NOSIGNAL`（mailbox 2026-04-22）** — 現場踩過：washrobot 跑到一半對端斷，shell 印
  `Broken pipe` 後 process 直接死。三個 `main.cpp`（washrobot / Crane_control_PI / Crane_easy_PI）
  已加 `signal(SIGPIPE, SIG_IGN)` 擋住。長期要在 `user_lib` 的 send 統一改用 `MSG_NOSIGNAL`（Linux），
  Windows 用 `#ifdef` 守衛——這樣 `Linux_test` 或未來新 `main.cpp` 忘記加 signal ignore 也不會中招。
- **CLV900 null-client（mailbox 2026-05-14）** — 起因：中間管線硬體未裝，`init()` 被註解掉 →
  `client = nullptr` → `allMotionOff() → stopDecel() → writeParam → sendModbus → client->sendAndReceive`
  null-deref，啟動直接 segfault。建議 `sendModbus` 開頭加 `if (!client) return true;`（本專案 true=error），
  讓未 init 的 driver 對外永遠回 error code，呼叫端就不用個別 guard。**SD76 / SE3 / DSZL_107 也要一起檢查**
  有沒有同樣問題。
- **SE3 `readFaultCode` 位址（mailbox 2026-05-14）** — bench 驗到 `0x1007`/`0x1008` 連續 ~10 次
  都 READ_FAIL。三個可能：(a) 不是 SE3-210 的 fault code register、(b) 只能在馬達停止時讀、
  (c) `.claude/summaries/SE3_INVERTER_MODBUS_SUMMARY.md` 的 PDF text dump 抓錯。應用層已從
  keepalive 撤回自動呼叫（會拖長 tick 把另一邊的 SE3 也踢進 OPT），改成 raw command
  `se3_fault left|right` 讓 bench on-demand 試；**driver 方法本身留著、未撤**。
- **SD76 SCAL/DP API review 重點（mailbox 2026-05-09）** — ① 公式假設
  `display = pulse × SCAL × 10^(-DP)` 是依手冊 + 常見廠商設計猜的，第一次 `cal_set` 若猜錯會把
  SD76 顯示弄歪，要從面板恢復；② 是否需要像 DSZL 那樣的明確 save 命令（手冊沒提，目前假設 FC 0x10
  直接落 EEPROM），bench 寫完要 power-cycle 確認；③ `writeScale` DP 限 [0,5]、`getEffectiveScale`
  限 [0,6]，實機可能不到 5，超範圍目前直接回 true(error)；④ `encodeBCD6` 的 `out[0]=0x00` 是把
  sign byte 留 0，要確認 SD76 對 SCAL 不檢查 sign bit。
- **DSZL 路 B 熱修 review 重點（mailbox 2026-05-08）** — ① MBAP frame 包裝是否正確
  （txid 計數、read len=6 / write multiple len=11、proto=0、unit byte）；② reply 重封裝
  （`memcpy(rx, buf+6, 3+bc)` + `rxLen = 3+bc`）是否真的相容 caller 端的 `buf[3..]`+len 檢查；
  ③ 是否該保留 RTU 路徑（雙 framing）——當時直接拿掉是因為架構圖也跟著改了。
  X518 手冊要點：**2 通道（不是 8）**、出廠 IP `192.168.1.120` / port `502` / mode reg `0x644` 預設
  1=Modbus TCP、IP 編碼 `IPH=oct1*1000+oct2` `IPL=oct3*1000+oct4`、暫存器 CH1=`0x0A00` /
  zero=`0x0A20` / unit=`0x0614` / slave=`0x064C`、`0xA20` 是多功能命令暫存器（1/2/7=zero CH1/CH2/all、
  40=SAVE）。廠商 0755-2890-9121（深圳，德森特）。bench 工具：`Linux_test` menu 24（C++ 互動式
  Modbus TCP :502，`r/l/p/R/W/S/u/z/Z/A`）＋ `Linux_test/x518_probe.py` / `x518_portscan.py` /
  `x518_wide_scan.py`。
- **`meter_left_read_fail`（ONBOARDING §3）** — 已排除兩個假設：(a) `TCP_client` 的「Linux 殭屍
  連線偵測失效」——crane 已用最新版重編部署，問題仍在；(b) 2026-05-08 的「Modbus-TCP gateway
  stale buffer」——已用 `sendAndReceive` atomic API 修過，SD76 在修復清單內。**還沒查的方向**：
  `meter_read_robust()` 在 `readUpperInteger` 硬失敗時才設 `g_length_left_valid=false`，但沒查為什麼
  設下去之後不會自己恢復（`meter_loop` 一直在跑，下次讀成功理論上該恢復）。
  **下次遇到，優先收集 crane 程式自己的 console/log，不要只看 washrobot 端收到的回覆。**
- **follower IMU 校平（ONBOARDING §2）** — 判斷方法：如果那次完整 log 裡 follower 移動附近連一行
  `[imu_level]` 都沒出現，就是 `follower_mode` 當時被切到 `meter`。`follower_use_imu_` 預設是
  `true`（`WASH_ROBOT.h:881`），只有 `cmd_set_follower_mode("meter")` 會關掉。
  ⚠️ 2026-08-26 GUI 已移除交替走法、`status` 也不再解析 `follower_mode=`，但**後端 raw command
  的 gait 預設仍是 `alt`**（`main.cpp:195` / `WASH_ROBOT.h:91`），所以這條路徑還走得到。
- **文件脫節（ONBOARDING §5）** — `motion_flow.md` §2 仍是 v1 的「RS485_1 @ .20 DM2J×5 /
  RS485_2 @ .21 ZDT×9 / 三區真空」；crane 端也對不上。而 2026-08-27 bench 又重配過一次硬體
  （gateway 角色對調、吸盤 slave 1-4 → 5-8、繼電器搬 bus），落差只會更大。要更新就直接對照目前
  程式碼常數重寫，不要沿用舊圖。
- **同步步伐的安全前提（work_log 2026-07-22）** — `step_down_sync`/`step_up_sync` 是本專案第一個
  「會讓 4 顆吸盤同時全部放開」的重複走法，放繩期間**完全靠鋼索承重、沒有任何吸盤錨定**。
  這是使用者明確確認過的刻意設計，不是疏漏——但之後要改這塊邏輯的人務必記得：v2 一路以來
  「至少一側 ≥1 顆吸盤黏牆」的不變式在這裡**不成立**。

---

## 🆕 新架構待辦（2026-08-27 設計彙整，與上表的現行程式碼待辦分開，共 27 項）

> 📌 **這一節屬於新一代機器的規格文件 `.claude/洗窗機器人設計彙整.md`（v3，2026-08-27），
> 全部是設計階段的未定案與未解項——不是現行程式碼的 bug。**
>
> 新架構是「沿用既有硬體的改寫」：四輪貼玻璃滾動升降 ＋ 兩具 22 吋螺旋槳提供貼牆推力 ＋
> 四支電動缸 ø200mm 吸盤 ＋ 橫向滑台（滾筒／刮刀）＋ 雙主控（頂樓 Pi ＋ 機上 Pi 5，
> 電力載波乙太網路）＋ 正壓破真空。
>
> **刻意跟上面的待辦總表分開放，避免兩者混淆**：上表每一列都指得到現行原始碼的檔案與行號、
> 現況欄講的是「程式碼現在是什麼樣」；這一節沒有任何一列有對應的程式碼，現況欄講的是
> 「規格還沒決定」。唯一的交界是上表最後一列（QX-DO24 PWM 停用）——那條是現行程式碼的狀態，
> 卻同時擋住新架構貼附序列的第一步。
>
> 內容為原文 `## 5. 待定規格`（10 項）＋ `## 6. 已知待解項目`（14 項）＋
> `### 暫緩項目`（3 項）＝ **27 項全數入表，無遺漏**。
> ⚠️ 交辦時說「已知待解 15 項」，2026-08-27 逐列清點原文只有 **14 項**
> （`LRS-150-24 容量` ~ `20cm 吸盤落點`）。這裡以原文為準，沒有補湊出第 15 項。
>
> 🔴 **其中四項是安全項**：硬體看門狗、漏電保護（RCD）、螺旋槳防護、ESC 電壓版本。
> 這四項的共同性質是——**它們是「以為已經存在、實際上不存在」的保護**，
> 其餘項目沒定案只是規格未收斂，這四項沒做是會出事的：
> Pi 當機後螺旋槳停不下來、帶水設備上有 220V AC、22 吋碳纖槳尖速超過 100 m/s、
> 電源 57.6V 已超出 6–12S 版 ESC 的上限。

### 新架構待辦表

| 優先度 | 項目 | 說明 | 建議 | 來源 |
|---|---|---|---|---|
| 🟢 | 吸盤中心距 | 決定可適應的最小玻璃分割 | — | 設計彙整 §5 待定規格 |
| 🟢 | 刮刀延伸方向 | 刮刀較滾筒長的 220mm，是上下各 110mm 還是全部往下 | — | 設計彙整 §5 待定規格 |
| 🟢 | 皮帶輪節圓直徑 | 計算滑台速度與推力用 | — | 設計彙整 §5 待定規格 |
| 🟢 | 滑台有效行程 | 1m 清洗寬度加刮刀走出的餘裕 | 建議 1.2m 以上 | 設計彙整 §5 待定規格 |
| 🟢 | 極限開關配置 | 須感測滑車本身，非馬達端 | — | 設計彙整 §5 待定規格 |
| 🟢 | 輪子型號 | 未定 | — | 設計彙整 §5 待定規格 |
| 🟢 | 計米器型號 | 未定（頂樓端鋼索 ×2、臍帶 ×1 共 3 具） | — | 設計彙整 §5 待定規格 |
| 🟢 | 空壓機型號 | 未定（機上小型，硬體壓力開關自動補氣、500 kPa 停止） | — | 設計彙整 §5 待定規格 |
| 🟢 | 滾筒馬達額定扭矩 | 需向廠商確認（名揚 MY32GP-3175，24V／296rpm） | — | 設計彙整 §5 待定規格 |
| 🟢 | 減壓閥 | 正壓氣路是否加裝減壓閥 | §3.4 標為「建議加裝」，降至 30～50 kPa | 設計彙整 §5 待定規格 |
| 🟡 | LRS-150-24 容量 | 6.5A 對現有負載偏緊，四軸電動缸同動加空壓機啟動會超過 | 改用 LRS-350-24 以上 | 設計彙整 §6 已知待解 |
| 🔴 | 硬體看門狗 | 485→PWM **斷線維持輸出**，Pi 當機後螺旋槳無法停止 | 獨立於 RS485 的硬體電路，逾時直接切斷 ESC 電源 | 設計彙整 §6 已知待解 |
| 🔴 | ESC 電壓版本 | FLAME 100A 有 6–12S 與 6–14S 兩版，電源 57.6V 超過 12S 上限 | 確認為 14S 版，或將 NPP 輸出調至 50V 以下 | 設計彙整 §6 已知待解 |
| 🟡 | 螺旋槳成對 | 同向旋轉會產生淨反扭矩，使機體繞鋼索旋轉 | P22×6.6 須 CW/CCW 成對，接線相序相反 | 設計彙整 §6 已知待解 |
| 🟡 | 單邊推力失效 | 一顆 NPP 故障會造成左右推力不平衡 | 兩顆的 DC OK 訊號接入 Pi，任一失效即同步降載 | 設計彙整 §6 已知待解 |
| 🟡 | AC 側壓降 | 兩顆 NPP 加控制電源約 3.7kW，220V 單相約 17A，200m 壓降偏高 | 確認電纜線徑，或改送 380V 三相 | 設計彙整 §6 已知待解 |
| 🔴 | 漏電保護 | 帶水作業，設備上有 220V AC | 漏電斷路器（RCD）**為必要，非選配** | 設計彙整 §6 已知待解 |
| 🔴 | 螺旋槳防護 | 22 吋碳纖槳葉尖速度超過 100 m/s | 護網或護罩，地面裝機測試時尤其必要 | 設計彙整 §6 已知待解 |
| 🟡 | 計米器累積誤差 | 滾輪式長距離滑差可能達 1～2%，200m 為 2～4m | 每層樓歸零校正 | 設計彙整 §6 已知待解 |
| 🟡 | 開環滑台失步 | 皮帶跳齒或阻力過大時系統不會知道 | 兩端極限開關，每趟行程歸零 | 設計彙整 §6 已知待解 |
| 🟡 | 幫浦回流 | 隔膜泵停轉時空氣會回流 | 幫浦出口加止回閥 | 設計彙整 §6 已知待解 |
| 🟡 | 正壓倒灌 | 正壓吹氣時可能打進幫浦 | 確認真空閥切換時幫浦口確實封閉，或加止回閥 | 設計彙整 §6 已知待解 |
| 🟡 | 滾筒馬達散熱 | 馬達內藏於滾筒，只能靠外殼傳導 | 確認連續運轉溫升與軸端油封等級 | 設計彙整 §6 已知待解 |
| 🟡 | 20cm 吸盤落點 | 吸盤不可壓到鋁橫料或矽利康膠縫，否則漏氣 | 固定段高須配合玻璃分割高度 | 設計彙整 §6 已知待解 |
| 🟢 | 空壓機與電動缸電流重疊 | 空壓機啟動與電動缸同動時的電流重疊 | **暫緩**；症狀為電動缸偶發失步或抱閘異響，實機測試時可能浮現 | 設計彙整 §6 暫緩項目 |
| 🟢 | 空壓機振動干擾姿態 | 空壓機振動對陀螺儀姿態判斷的干擾 | **暫緩**，實機測試時可能浮現 | 設計彙整 §6 暫緩項目 |
| 🟢 | 儲氣筒壓力未讀 | Pi 未讀取儲氣筒壓力，假設氣壓恆定可用 | **暫緩**，實機測試時可能浮現 | 設計彙整 §6 暫緩項目 |

---

## 2026-08-17 — 新增操作說明：M2（工具頭馬達）重裝後的校正流程

> **規範權威：** `.claude/changelog.md` 2026-08-14a（手動量測 + `SET_HALF_RANGE`）/ 2026-08-14b（`lr_calibrated` flag）/ 2026-08-14c（`lr_half_range` 預設值 0.7275）；程式位置 `cleaning_arm/main_api.h:225-251`、`cleaning_arm/main_api.cpp:1992-2028`。這塊目前沒有獨立規格文件，權威就是原始碼本身 + 上述 changelog 條目。

### 背景

比對舊版（`D:\洗窗戶機器人\cleaning_arm`）跟現行版手臂控制邏輯時，使用者問「馬達重新旋轉安裝過，是不是要重新找 M2 位置」，追出 M1/M2 在這件事上行為不一樣：

- **M1：不用擔心。** `cmd_init_sequence()`（main_api.cpp:1992）每次 INIT 都無條件重跑 `calibrate_arm_slot(m1_)`（往負方向頂機械停點、重設零點），不管怎麼拆裝，每次 INIT 都是現場重新量。
- **M2：有風險，不會自動抓到。** `lr_half_range=0.7275`（main_api.h:241）是寫死常數、`lr_calibrated` 預設 `true`（信任舊值）。INIT 只有在 `|M2 位置| > 1.5 rad`（main_api.cpp:1997-2017，防的是程式當機後回報離譜殘值）才會強制歸零重新校正；只要重裝後讀到的位置還落在 ±1.5 rad 內，INIT 會直接信任舊零點+舊 half_range 移到它以為的「CENTER」——**這不會報錯，是靜默移到錯的位置。**

零點本身是用 CAN 指令 `0xFE`（`set_zero_position`）寫進 damiao 馬達內部，重開機/重啟程式都還在，所以問題不是「零點會不會消失」，是「零點還對不對得上新的實體安裝角度」。

### M2 重裝後的校正步驟（手動流程，目前唯一可靠路徑；`LR_CALIBRATE` 自動雙向尋邊仍不穩定，見上述 changelog）

1. `M2 DISABLE`（卸力，可徒手轉動）
2. 手動把工具頭轉到真實物理正中間
3. `M2 MIT 0 0 0 0 0`（zero kp/kd，刷新 `Get_Position()` 的被動快取——`DISABLE` 不會像 `ENABLE` 一樣補送 MIT frame 刷新，直接 `STATUS` 會讀到轉動前的舊值）
4. 在物理正中間位置下 `M2 ZERO`（設新零點）
5. 手動轉到 LEFT / RIGHT 兩側極限，各自 `M2 MIT 0 0 0 0 0` + `STATUS` 量出 `CENTER→LEFT` 與 `CENTER→RIGHT` 的距離
6. 取兩者**較小值**當 half_range（較大值會讓另一側只剩極小緩衝、太貼近硬停點，風險高）
7. `M2 SET_HALF_RANGE <算出來的較小值>`（同時把 `lr_calibrated` 設為 `true`，往後 INIT 才會信任這組新值）

### 待確認 / 尚未處理

- 沒有機制會自動偵測「M2 被重新安裝過」——上述步驟完全靠人記得去做，重裝後如果忘記走這流程、且重裝後讀到的位置剛好落在 ±1.5 rad 內，系統不會有任何警告。
- `LR_CALIBRATE` 自動兩邊尋邊本身仍不可靠（假觸發撞牆、或衝很大距離都撞不到東西），還沒修，手動流程是目前建議路徑。

## 2026-07-22~23 — 📌 depth camera 窗框辨識：從「完全測不到」到「距離算對」的整趟除錯（下次接手讀這條）

> **規範權威：** `.claude/changelog.md` 2026-07-21e ~ 2026-07-23f 一大串條目（逐筆有檔案+行為說明，這節只整理「現在到哪、還缺什麼」）。

### 這兩天的除錯順序（用真實 bench log 一路追出來的，不是憑空猜參數）

實機 bench 場景：木板架在**鏡子反射**的雜亂工作室前（模擬真實玻璃帷幕的鏡面反光情境，這是刻意選的測試條件，user 的目標本來就是鏡面反光跟一般窗戶都要能處理）。

1. **完全偵測不到（candidates=0，無 log）** → depth_cam_service.py 沒被啟動（scripts/wr.sh 沒接這支服務）
2. **`protrusion std` 爆炸** → bbox 重新取樣漏掉距離門檻，遠處反光雜訊污染近物凸出量統計 → 修
3. **背景平面把木板自己拉平（凸出量≈0）** → 鏡面場景下畫面裡「近距離+有效」的像素幾乎全是木板本身，沒有獨立背景可擬合 → 加兩階段重擬合（`fit_plane_two_pass`），但這個場景仍常常沒有足夠背景點
4. **關鍵設計轉向**：user 明確表態「最主要需要距離（算下一步步伐），凸出量次要」→ 候選物判準整個改成**距離+形狀為主**，背景平面擬合失敗不再等於偵測失敗（退回純距離+形狀判斷）
5. **雜物被誤判成候選物** → 加「只留最寬」過濾（window sill 本來就該是畫面最寬的水平特徵）
6. **距離被不相干的 rejected 小碎片搶走** → `min_distance_cm` 改成有候選物就一定用候選物自己的距離
7. **`remaining_travel_cm` 系統性高估**（user 現場皮尺量測跟算出來的差 10cm）→ 追出 `near_m`（候選框內最近像素）不保證落在畫面正中央，偏心像素被公式誤當成「往前距離」→ 改用 `center_distance_m`（畫面水平中心窄帶內最近距離）
8. **修完中心點問題後，數字還是差很多**（算出 19.8cm，實測 3~4cm）→ 回推發現是安裝幾何常數本身錯了：`DEPTH_CAM_LEAD_OFFSET_CM` 應該是 32cm 不是 16cm（原本可能量到機身中間某個點，不是真正吸盤前緣），`DEPTH_CAM_STANDOFF_CM` 也更新成 56cm（原 50cm）——user 重新量測後確認

### 順便做的兩個功能

- **跨越障礙物步幅建議**：`remaining_travel_cm < DEPTH_AVOID_LOW_CLEARANCE_CM(20cm)` 時，GUI「下一步走幾公分」預設值自動算成 `remaining + max_height + 吸盤直徑20cm + 緩衝5cm`（user 提供公式），夾到 `STEP_CM_MAX`。只改預設值，2026-07-20 原設計「使用者每次自己決定」沒變
- **Camera 頁面「拍照」按鈕**：同時顯示全彩 + 深度圖兩張，独立於 BEFORE/AFTER 分析週期（`/snap/depth_live` + `/snap/depth_live_depth`）

### 意外撈到的獨立 bug（跟窗框辨識無關，但影響全專案）

`user_lib/TCP_client.cpp`：Linux 上 `available()` 用 `ioctl FIONREAD` 偵測不到對方正常關閉連線，`sendData`/`receiveData` 失敗時也不會把 `connected` 設回 false——導致連線斷了以後 `reconnectLoop()` 永遠不會觸發重連，client 卡死在假的「已連線」狀態。已修（改用 `recv(MSG_PEEK)` 比照 Windows 分支），**影響所有用 `TCP_client` 的裝置**，不只 depth cam。

### ⚠ 還沒做完、下次要接的

- **這兩天全部改動都還沒編譯驗證**（本機無法 remote build）：`user_lib/TCP_client.cpp`、`user_lib/WASH_ROBOT.h/.cpp`（含這次更新的安裝幾何常數）都要重新編譯 `facade_cleaning_v2` 才會生效
- **`remaining_travel_cm` 修完新常數（32cm/56cm）後，還沒實機重新驗證過**——上次驗證的 3~4cm 案例是用來反推常數的，還沒有拿修正後的常數重新測一輪、確認算出來的數字跟皮尺一致
- **跨越障礙物步幅建議公式（remaining+height+20+5）本身還沒實機測試過**——只驗證過算式邏輯，沒驗證過「照這個步幅走，機器人是不是真的能安全跨過去」
- **一般（非鏡面）窗戶場景還沒測過**——這兩天全部驗證都是鏡面反光場景，「只留最寬候選物」「距離優先」這些新邏輯在乾淨背景場景下的行為還沒確認過，理論上不會變差但沒有實機證據
- **`frame_capture/depth_cam_service.py`、`depth_reflection_bench.py`、`depth_cam_test_client.py` 三個檔案都還是 git untracked**，這批修改都沒進版控，換人接手前記得先 commit 這幾個檔案

## 2026-07-23 — 修 step_down_sync 真空判準（過嚴，已對齊「每側 ≥1」慣例）

> **規範權威：** `.claude/changelog.md` 2026-07-23 條目。

user 實機測 `step_down_sync` 回報 `partial_seal count=2` 卻直接進了 `State::Error`（log 顯示其實左右各有 1 顆吸住，照 v2 既有「每側 ≥1 算過」慣例不該進 Error）。追出是 2026-07-22 新增 `do_step_sync_` 時，最終真空判準誤寫成「4 顆全吸」而非既有的 `group_seal_ok_`「每側 ≥1」規則——已修正，改成只有整側全掉才判失敗。`recover` 那邊回的 `ERR recover_vacuum_fail` 是另一個獨立、刻意設計成嚴格的既有函式（`cmd_recover`，2026-06-02 決策），沒有動它。

**⚠ 還沒做完：** 這批修改連同 2026-07-22 的同步步伐一起，完全未編譯驗證（本機無法 remote build）。部署後除了原本要測的方向/收斂，還要確認這次修的 partial-seal 判準真的會正常回 OK 而不是進 Error。

## 2026-07-22 — 新增同步步伐（step_down_sync/step_up_sync）

> **規範權威：** `.claude/motion_flow.md` §4b「同步步伐」+ `.claude/changelog.md` 2026-07-22e 條目。

### 這次做了什麼
新增一組跟現有 `step_down`/`step_up`（交替式 inchworm 走法）並行的新走路方式：`step_down_sync <cm>` / `step_up_sync <cm>`。流程是 4 顆吸盤同時解真空縮腳 → 吊機兩側同步放/收繩 → IMU 差動微調水平 → 4 顆同時重新吸附，不含清洗。

**⚠ 重要安全性質變動，下次接手務必先讀：** 這是本專案第一個「重複執行、且會讓 4 顆吸盤同時全部放開」的走法。現有所有其他重複走法（`do_step_down_`/`do_step_up_`/`do_cross_obstacle_`）都保持「至少一側 ≥1 顆吸盤黏牆」當防墜落錨點；這個新走法在吊機放繩期間**完全靠鋼索承重，沒有任何吸盤錨定**。**這是使用者明確確認過的刻意設計**（2026-07-21/22 對話），不是疏漏——但任何人之後要改這塊邏輯，務必記得這個前提已經跟其他所有走法不一樣，不要假設「至少一側黏牆」這個 v2 一路以來的不變式在這裡也成立。

IMU 微調用的是「雙邊差動」（兩側繩子同時反向調），呼叫 crane 端既有的 `roll_correct <delta_cm>` 指令——這條指令本來就存在且是活的（只是 WASH_ROBOT.cpp 這邊呼叫它的 v1 舊 code `do_phase5_roll_correct_` 被 retired 了，這次是重新用 v2 的方式呼叫，沒有復用那段舊 code 本身，因為它綁定了 v2 已經沒有的 body/center valve）。

### ⚠ 還沒做完、下次要接的
- **完全未編譯驗證**（本機無法 remote build）——`user_lib/WASH_ROBOT.h/.cpp`、`facade_cleaning_v2/main.cpp`、`web_backend/public/*` 都是本機未 commit 的修改
- **IMU 差動微調的方向還沒實機驗證**——sign convention 是照抄 `follower_imu_level_`（單邊微調）的既有邏輯推算出來的「兩側同時反向」版本，還沒實際測過方向是否真的抓平，第一次上機建議先在小角度、有人在旁邊看著的情況下測
- **沒有做角度限制的地面淨空檢查**——目前這個新指令跟 Phase 4 一樣完全信任使用者輸入的 cm，沒有額外檢查「這一步放繩期間附近是否有已知障礙物」，跟 depth camera 避障是分開的兩件事，沒有整合

## 2026-07-21 — depth camera 上線除錯 + TCP_client 殭屍連線 bug 修復

> **規範權威：** `.claude/changelog.md` 2026-07-21 / 2026-07-21b 兩筆條目。

### 這次 session 做了什麼

1. **depth_cam_service.py 沒被啟動** — `run_depth_avoid` 按下去秒失敗在 `before_capture_failed`（空訊息），追出 `scripts/wr.sh` 從沒開過 `depth_cam_service.py` 這個 window（新檔案，未接進腳本）。已補上 `depth` window（`scripts/wr.sh`），cam1/cam2 暫時沒實體攝影機先註解掉。
2. **Camera 頁面「拍照」按鈕永遠 offline** — 追出 `/snap/depth` 是「上一次 run_depth_avoid AFTER 分析結果圖」，服務剛啟動、從沒跑過 BEFORE/AFTER 時 buffer 是空的、回 503。已加獨立的 `/snap/depth_live`（即時原始畫面，不需要先跑分析）：`depth_cam_service.py` 新增 endpoint、`server.js` CAMERAS 加 `depth_live`、`app.js` 拍照按鈕改打新 endpoint。
3. **`unknown_cam`** — web_backend 沒重啟，還在跑改動前的 `server.js`（純部署问题，非程式 bug）。
4. **真正的根因（TCP_client 層級 bug）**：即使 depth_cam_service.py 確認活著、`run_depth_avoid` 還是一直 `send fail`，且主程式完全沒印 reconnect 訊息。追到 `user_lib/TCP_client.cpp` 的 `available()` 在 Linux 上用 `ioctl FIONREAD`，偵測不到對方正常關閉連線；`sendData`/`receiveData`/`sendAndReceive` 失敗時也不會把 `connected` 設回 `false`。兩者疊加造成連線斷了以後 `reconnectLoop()` 永遠不會觸發、client 卡死在假的「已連線」狀態，只能重開主程式。**已修**（詳見 changelog 2026-07-21b），影響所有用 `TCP_client` 的裝置連線，不只 depth cam。

### ⚠ 還沒做完、下次要接的

- **TCP_client 這批修改全部未編譯驗證**（本機無法 remote build），部署到 Pi 後要重跑一次 `run_depth_avoid` 確認真的恢復自癒（不需要再手動重開主程式）
- cam1/cam2 目前在 `scripts/wr.sh` 裡是註解掉的狀態，之後攝影機接回去記得取消註解（腳本裡有留位置跟說明）

## 2026-07-15 — 📌 crane 通訊/傾斜/退繩三個實機事故的除錯與修復（下次接手讀這條）

> **規範權威：** `.claude/changelog.md` 2026-07-15b~f 六筆條目（逐筆有檔案+行為說明）；本節只整理「現在進度到哪、還缺什麼」。

### 這次 session 做了什麼（起因都是 user 回報的實機現象）

1. **吊機通訊頻繁 PAUSE-ON-ERROR + 快速重試** → 追出 client timeout 太短會強制斷線重送、且 `cmd_side_measured` 沒有 `motion_mtx` 保護會被重複驅動 → 已補鎖 + 補 log（changelog 07-15b）
2. **傾斜 49.6° 觸發 IMU 緊急停** → 追出 corrupted meter 讀數（3.36941e+07）沒做 sanity check，讓方向判斷反過來 → 已加範圍檢查（changelog 07-15c）；`crane_abs_target_cmd_` 的方向/target 公式本身有回推驗算過，是對的
3. **退繩比原位還低** → 追出退繩重試預算用固定 `step` 而非「這側實際前進量」→ 已改用 `out_mv_cm`（changelog 07-15f）
4. **順手做的功能需求**：feet top-up 改背景執行、換邊不等它（changelog 07-15e）；首頁吊機面板搬到 IMU 下面（changelog 07-15d）

### ⚠ 還沒做完、下次要接的

- **`abort_flag` 沒有在 `cmd_side_measured` 進入時重置**（`Crane_control_PI/main.cpp`）——已經跟 user 討論出修法（補一行 `abort_flag = false;`，仿照 `motion_rope`/`cmd_roll_correct`），**但還沒實際套用**。症狀：吊機被 stop 過一次之後，所有 v2 step 指令永遠秒回 `ERR aborted`，只能重開吊機程式。詳見 changelog 07-15b 的「待辦」段。
- **左/右 follower 移動時 IMU 沒對平** — user 回報過，懷疑是 `follower_use_imu_` 被切到 `meter` 模式（該欄位在 `status` 指令的 `follower_mode=` 顯示）。**還沒確認實際值**，也還沒改任何程式碼。
- **`.vcxproj.user`（`facade_cleaning_v2/`、`Linux_test/`）被 git 追蹤，導致不同人/不同 bench 網路的 Remote Target 互相覆蓋**，這次 user 編譯時遇到 Connection Manager 遠端主機顯示空白就是這個原因。已建議 `git rm --cached` + 加 `.gitignore`，**user 尚未回覆是否要做**。
- **全部改動都還沒編譯驗證**（remote build，本機編不了）——`user_lib/WASH_ROBOT.h/.cpp`、`Crane_control_PI/main.cpp`、`web_backend/public/index.html` 都是本機未 commit 的修改。

## 2026-07-07 — 📌 v2 SESSION HANDOFF（下次接手讀這條）

> **規範權威：** `.claude/v2_app_redesign_plan.md`（應用層重寫）+ `.claude/mh300_migration_plan.md`（吊機變頻器）+ memory `project_v2_mechanical_gait` / `project_new_crane_vfd_mh300`

### v2 目前在哪

v2 是新硬體 fork（機械大改）。這幾個 session 累積：專案改名 → 吊機變頻器換 MH300 → 開始 v2 應用層重寫。**所有程式改動都尚未編譯**（VS Linux 交叉編譯要 remote build，本機編不了）。

### 已完成（git 狀態）

| 項目 | commit | 狀態 |
|---|---|---|
| 專案改名 washrobot_new_PI → facade_cleaning_v2（含 .sln/vcxproj/部署路徑）| `a2e0704` | 已 push |
| MH300 driver + 吊機 SE3→MH300 遷移 + GUI se3_→vfd_ + 2 份計畫文件 | `1829964` | 本機、未 push、**未編譯** |
| v2 重寫**第 1 步**：crane 端單側量測放/收繩 `cmd_side_measured` | 本次 commit | **未編譯** |

### v2 機械架構（user 口述 + 決策，2026-07-07）

- 4 吸盤：推桿 slave **1,2=右腳 / 3,4=左腳**（右腳 上=1/下=2、左腳 上=3/下=4；單顆機構同 v1：ZDT 推桿+真空+JC100）
- 真空 **2 區**（左閥/右閥），無中心杯
- **無 DM2J**（無滑軌、無輪組）、**無橫向**（只垂直上下）
- 垂直位移 = **單側吊機繩放/收 + SD76 計米量測**（取代 v1 的滑軌）
- 水平 = IMU + 左右繩長差
- 步態（向下一步）：4杯吸附水平 → 放左2杯+放左繩+重吸左 → 放右2杯+放右繩+重吸右 → 完成。**不變式：至少一側撐住，絕不4杯全放**
- 清洗手臂仍要但**未裝** → step 收尾 sweep 先註解

### v2 WASH_ROBOT 重構進度（2026-07-07，「從上往下改到好」）

PQW 通道確認：**CH1=右腳閥 / CH2=幫浦(原CH1) / CH3=左腳閥**（@ .22 slave 12）。推桿 slave 右{1,2}/左{3,4}（v1 feet 本來就這樣，不用動）。

WASH_ROBOT 大重構已做（**未編譯**，本機編不了）：
1. ✅ 第 1 步 crane 單側量測指令 `pay_out/retract_left/right <cm>`（commit 845e200）
2. ✅ 常數（.h）：CH_VALVE_RIGHT/PUMP/LEFT；ZDT 只剩 1-4；刪 body/center/CH_VALVE_FEET等
3. ✅ group config：group_slaves_/valve_ch_/preset/vacuum_valve_/stall helper 全改右左；陣列 size 保留 9（只用 1-4）
4. ✅ init() + cmd_init_impl_：拆 DM2J 腳/輪 rail、中心杯、bystander broadcast；ZDT/JC100 迴圈 9→4；保留 arm rail(cli_22 s14)/DY500(present=false)/IMU/threads
5. ✅ cmd_attach：右→左序列，各側 smart_extend 一點一點補伸 + vacuum_check；拆 body/center/realign
6. ✅ do_step_down_/do_step_up_：**重寫成 v2 精簡版**（右先左後、每側 crane pay_out/retract step_cm、補伸重吸、anchor 安全檢查）；v1 舊 body 用 `#if 0` 包起來「retired，待 bench 驗證後刪」
7. 🔄 **task #6 收尾（fork 執行中）**：把剩下引用已移除符號的 v1-only 函式 #if0（do_feet_realign_/obstacle/balance/phase5/tilt/wheels/dm2j_group/realign/cmd_move）或修正（detach/shutdown/emergency/status/pusher/zdt_*/return_home 的閥門與 1-4 範圍）+ main.cpp dispatch，目標整檔可編譯

⚠ **`#if 0` retired 慣例**：do_step_down_/up_（及 fork 中性化的函式）保留 v1 舊 body 在 `#if 0` 內當 reference，**bench 驗證 v2 綠燈後再硬刪**。

### 之後還沒做
- 水平校正整合（IMU roll + 左右繩長差 tol）：step 收尾目前只留 TODO
- GUI 按鈕對應（右/左閥、單側繩、step）
- arm 清洗 sweep（未裝，step 收尾先 deferred）
- **remote build 驗證** washrobot 主程式（改動全未編譯）

### ⚠ 開機第一件事：建議先 remote build 驗證 crane 端

第 1 步 + MH300 遷移都改在 `Crane_control_PI`（跟 WASH_ROBOT 是獨立編譯單元）。往下大改 WASH_ROBOT 前，**先 build `Crane_control_PI` 確認綠燈**，隔離問題。有編譯錯我再修。
另有 MH300 實機必驗清單（方向/電流scale/2101H run bit/fault code）見 `mh300_migration_plan.md`。

### 未 push 提醒

`1829964` + 本次 commit 都在本機 main、**未 push**（因未編譯）。build 綠燈後再決定 push。

---

## 2026-06-09 — 📌 SESSION HANDOFF（v1 舊交接，v2 已由上方取代）

> **規範權威：** `changelog.md` 2026-06-05k~o + 2026-06-09a~b（這一週累積的程式改動）

### 這次 session 做了什麼（按時間序）

**全部後端 + GUI 改動都已寫進 changelog，但都還沒實機驗證**。

#### 1. Scripted Run 功能（2026-06-05k~n、6-05o 部分相關）
- ✅ 新增 `cmd_run_script <csv>` + 5 個 saved-script 管理 cmd（save/load/list/delete/run_saved）
- ✅ CSV 文法：`<int>[n][*<count>]` — `n` 後綴 = transit (不刷洗)、預設 = sweep
- ✅ 持久化到 `./scripts.json` (key=value 格式)
- ✅ pipeline 模式照抄 `cmd_run`（sweep + step overlap，pre-loop sweep / iter k after_hook launch）
- ✅ GUI panel 完整（CSV preview、saved scripts row、進度條、EVT 訂閱）
- 檔案：`WASH_ROBOT.h/.cpp` (~285 行新增) / `main.cpp` (6 dispatch) / `web_backend/public/*`
- ❌ 還沒實機跑過：`30*3` 全 sweep / `30,30n,30` 混合 / `30n*3` 全 transit

#### 2. Snowball 防護 A+B+C（2026-06-05o）
- ✅ **A**：`pusher_extend_with_disable_seal_` 結尾 record_seal_pulse_ 加 `if (weak_seal[i]) continue;`
- ✅ **B**：`feet_max_overextend_cm_()` cap 在 `FEET_MAX_OVER_CAP_CM = 4.5cm`
- ✅ **C**：新增 `feet_target_capped_(slave)` helper，cap 在 `preset + FEET_TARGET_OVER_CAP_CM = 5cm`
- 邏輯：feet last_seal 自然 snowball → cap 後撞不到牆 → WEAK_SEAL → A 不記錄 → realign 拉回 preset
- ❌ 還沒實機驗證：要看 `[snowball]` log 出現頻率、body 還會不會撞 end-stop

#### 3. Water Inlet 防漏（2026-06-09a + 09b）
- ✅ `set_water_inlet_(bool)` 加 retry 3 次 / 500ms gap
- ✅ Background watchdog thread：valve 開超過 `WATER_INLET_OPEN_MAX_MS = 5 min` 強制關 + EVT
- ✅ `cmd_emergency_stop` / `stop()` 兜底 force close
- ✅ GUI panel：狀態燈 (灰/綠 pulse/黃 pending/紅 error) + 計時 + auto-OFF 60s + watchdog toast
- 檔案：`WASH_ROBOT.h/.cpp` 後端 / `web_backend/public/*` 前端
- ❌ 還沒實機驗證：故意拔 crane Ethernet 看 retry log + watchdog 行為

#### 4. 純討論沒實作（user 決定先不動）
- arm_sweep_monitor SUSTAINED 0.2→0.4（避免 false positive）— **待 user 確認後做**
- DM2J:1,3 feet rail obstacle detect — **user 接受我建議不做**（ROI 低）
- arm_cmd_ INIT recv timeout 真因 — 沒查清楚（看起來 motor_api 端會卡）
- TCP_client SO_ERROR 驗證缺失 — **未修**（影響 reconnect 邊界 case）
- vacuum_check 重複跑兩次浪費 30s — **未修**（提了 α + δ 方案）

### 下次開機要做的事（按順序）

1. **build + deploy washrobot_new_PI**
   - VS MSBuild `washrobot_new_PI.vcxproj` (ARM64 Debug)
   - scp 到 192.168.1.100，重啟 washrobot 程式
   - **crane / Linux_test 不用編**（沒動）
   - 瀏覽器 **Ctrl+F5 強制 reload**（GUI panel / JS / CSS 都改了）

2. **快速 sanity check**
   ```
   washrobot console 啟動時應該看到：
     [OK] water-inlet watchdog started (max open 300s)
   ```

3. **實機驗證（按 ROI 序）**
   - **A. Scripted run pipeline**：跑 `run_script 30*3`、對照 `run 3 30 down`，sweep round 數應該都是 4
   - **B. Snowball 防護**：連跑 5 個 step 看 `[snowball]` log 出現頻率、body 還會不會撞 end-stop
   - **C. Water inlet GUI**：按 ON → 60s 自動 OFF；按 ON 後拔 crane Ethernet → 燈轉紅
   - **D. Watchdog**：暫時把 `WATER_INLET_OPEN_MAX_MS` 改 30000 試 watchdog force-close 路徑

### 待 user 決策的事

| 題目 | 我傾向 | 等 user 確認 |
|---|---|---|
| arm_sweep_monitor SUSTAINED 0.2→0.4 | 做 | 看 user 是否同意防 false positive 的犧牲（可能漏接弱接觸 obstacle） |
| arm_cmd_ INIT 60s timeout 是否還要拉長 | 看實機 | 60s 已給足 INIT (~10s)，recv timeout 真因要再查 |
| TCP_client SO_ERROR 驗證 | 該補 | user_lib 邊界，影響所有 client；要值得 review |
| vacuum_check skip if just SEALED (α 方案) | 該做（省 30s/attach）| 但 attach 路徑改動較大 |

### 重要狀態 / 變數

```
WASH_ROBOT.h 新常數：
  FEET_TARGET_OVER_CAP_CM      = 5.0    snowball C
  FEET_MAX_OVER_CAP_CM         = 4.5    snowball B
  WATER_INLET_OPEN_MAX_MS      = 300_000 (5 min)  watchdog

WASH_ROBOT.h 新成員：
  struct ScriptStep { int cm; bool sweep; };
  saved_scripts_ map / saved_scripts_mtx_
  water_inlet_open_ts_ms_ atomic
  water_inlet_watchdog_running_ / _thread_

WASH_ROBOT.cpp 新函式：
  cmd_run_script / cmd_save_script / cmd_list_scripts / cmd_load_script / cmd_delete_script / cmd_run_saved
  parse_script_csv_ / validate_script_name_ / load_saved_scripts_from_disk_ / save_saved_scripts_to_disk_
  feet_target_capped_(int slave)
  water_inlet_watchdog_loop_()

主要新 EVT：
  script_start total_steps=N total_cm=X sweep=A transit=B
  script step K/N cm=Y mode=sweep|transit
  script_complete status=ok|fail [step=K mode=X reason=...]
  water_inlet_watchdog_force_close open_sec=N
  weak_seal slave=N (已存在)
```

### 文件對應

| 主題 | 規範權威 |
|---|---|
| Scripted run 設計 | `.claude/scripted_run_plan.md` |
| Snowball 鏈條分析 | `changelog.md` 2026-06-05o |
| Water inlet 防漏 | `changelog.md` 2026-06-09a + 09b |

### Build 路徑提醒

- washrobot binary 應該在 `bin/ARM64/Debug/washrobot_new_PI.out`
- 部署到 RPi 192.168.1.100 `~/projects/washrobot_new_PI/bin/ARM64/Debug/`
- 進水球閥控制權現在在 crane (.34 slave 12 CH4)，所以 crane 不能掛
- bench 網段是 192.168.5.x（CRANE_IP 暫設 .5.26），production 要還原 .1.101

### 前一個 SESSION HANDOFF（保留以防需要回看）

---

## 2026-06-03 — Camera 障礙物偵測 motion parallax 驗證（舊 handoff，已被 06-09 取代）

> **規範權威：** `camera_obstacle_plan.md` + `changelog.md` 2026-06-02t

### 目前在哪 — Camera 障礙物偵測（窗框避障）

**驗證了什麼：**
- ✅ Motion parallax 概念**確認可行** — bench 工作室雜亂 + 反光玻璃場景下，仍能乾淨偵測 plank
- ✅ `frame_capture/obstacle_detector.py` 加了 `--motion-before/--motion-after` 模式
- ✅ 一對驗證圖：plank @ 25→24cm 1cm shift → detector 抓到 obstacle conf 0.99、STOP_SHORT 19.1cm
- ❌ 純 OpenCV（沒 motion）試三輪都失敗 — 反射 + 雜物太強
- ❌ NPU model (`yolov8s_window_640.hef`) 對木條 0 detection — 模型認的是「真實鋁窗框」不是 bench 木條

### 下次要做的 — **補拍 5 對 motion parallax 校正集**

User 關機前 commitment：開機後拍齊以下 10 張：

```
方法：
  1. 擺 plank 在距離 X 處，拍 cam3 snap → cam3_motion_Xcm_before.jpg
  2. plank 往相機方向滑 1cm，拍 cam3 → cam3_motion_Xcm_after.jpg

距離 5 對：
  X = 10, 15, 25, 35, 45 cm
  
存放：D:/工作/觀賞用/ （bench 圖慣例位置）
  cam3_motion_10cm_before.jpg + cam3_motion_10cm_after.jpg
  cam3_motion_15cm_before.jpg + cam3_motion_15cm_after.jpg
  ... (共 10 張)

跑 (每對一次)：
  cd D:/washrobot_new_PI/frame_capture
  python3 obstacle_detector.py \
      --motion-before D:/工作/觀賞用/cam3_motion_25cm_before.jpg \
      --motion-after  D:/工作/觀賞用/cam3_motion_25cm_after.jpg \
      --motion-cam cam3 --debug-out /tmp/m25 --pretty
```

### 接手後檢查清單

1. **5 對都進來了嗎** — 看 `D:/工作/觀賞用/`
2. **跑 5 對，整理表格：** 預期 feet_distance vs 偵測 feet_distance、誤差
3. **如果誤差大（> 5cm）：** LUT 要校正
   - 目前 LUT 用單張木條校正、沒 motion，可能不準
   - motion 模式抓的是「motion 高的中心 y_px」，可能跟單張 top edge y_px 不同
   - 從 motion 校正集重 fit LUT
4. **如果誤差小（< 3cm）：** motion 模式可用、進下一階段
   - 把 motion 模式整合進 frame_capture (continuous mode)
   - 寫 C++ FrameAnalyzer 接 UDP
   - **跨界 user_lib** — 要 PR 等 Jim review

### 關鍵變數 / 檔案

```
frame_capture/obstacle_detector.py:
  CAM3_LUT (~L86)           — 6 點 (y_px, distance_cm)，可能要重 fit
  CAM4_LUT (~L95)           — 同上，cam4 中間 3 點是 interpolation
  MOTION_MAG_FACTOR 2.5     — flow 高/低分界 (median × factor)
  MOTION_MIN_MAG 1.5px      — 絕對下限 (避免雜訊)
  OBSTACLE_MIN_LINE_LENGTH_PX 180  — line 長度門檻（motion 模式可放寬）

實機 cameras：
  cam3 = 192.168.1.112 (左下)
  cam4 = 192.168.1.113 (右下)
  俯角 54°（從水平算），optical axis 對準 25cm
  
  frame_capture.py 啟動 (要用 stream=0 主碼流，子碼流被 camera 內部 ROI 裁了)：
    --url "rtsp://192.168.1.112:554/user=admin&password=&channel=1&stream=0.sdp?"
    --out /tmp/cam_bottom_l.jpg --cam-id cam3 --http-port 5006

NPU Pi:
  IP: 192.168.5.25 (bench 網段)
  ~/window_detect/detect_server.py  UDP :5040
  Model: yolov8s_window_640.hef
  **目前模型對木條不認** — 後續若要 ML 路線得重訓
```

### 不接手做的事

- ❌ 不調 obstacle_detector.py 原本的 `--cam3/cam4` 單張模式 — 那條路驗證過走不通，留 fallback
- ❌ 不 retrain NPU model — bench 時間先不花這
- ❌ 不動主程式（user 明確說「不動主程式」）— toggle flag 已 ready 但 detector 還沒接 do_step_down_

### Related context

- `changelog.md` 2026-06-02t — motion parallax 詳細變更紀錄
- `camera_obstacle_plan.md` — 規範文件，目前**還沒加 motion mode section**（TODO 補）
- `D:/工作/觀賞用/` — bench 圖位置
- 路徑含中文需用 `imread_unicode` / `imwrite_unicode`（obstacle_detector.py 已處理）

---

## 2026-06-02/03 — Sadie session：realign 修好 + 一連串安全/效能改動

> **規範權威：** changelog `2026-06-01g/h` + `2026-06-02a/b/c/m`、`CLAUDE.md` §硬體架構（cli_22_ bus 擁塞）、memory `project_se3_07_10_two_options.md`（cli_22_ stale）

### 改動（已落 commit pending 重編 deploy）

| Tag | 檔案 | 內容 | 已實機驗 |
|---|---|---|---|
| 2026-06-01g | `WASH_ROBOT.h:814` | `try_or_pause_` loop 開頭加 `if (abort_flag.load()) return true;` — E-stop 中斷後下一個 op 不再執行 | 未 |
| 2026-06-01h | `WASH_ROBOT.cpp` realign `run_jog` | Stage 0 JOG stall 改 NON-FATAL（emergency_stop + release_stall_flag），讓 Stage A 繼續跑 | ✅ 已驗 — log 顯示 Stage 0 高電流 + Stage A 順利完成 + slaves 回 preset |
| 2026-06-02a | `Crane_control_PI/main.cpp:477` | `BALANCE_KP_DEFAULT` 0.6 → 1.0 Hz/cm | 未 |
| 2026-06-02b | `WASH_ROBOT.h:350` | `ARM_CLEAN_WALL_MM` 350 → 330（嘗試修「上貼下不貼」pitch 偏）| 未 |
| 2026-06-02c | `WASH_ROBOT.cpp` 4 處 pre_cycle | step Phase A/B 釋放某 group 真空前先 `vacuum_check_(another_group)`，4 處：do_step_down body+feet pre_cycle、do_step_up feet+body pre_cycle | 未 |
| 2026-06-02c | `WASH_ROBOT.cpp:cmd_recover` | 取消 vacuum_check_ 註解 — recover 失敗回 `ERR recover_vacuum_fail` state 留 Error | 未 |
| 2026-06-02m | `WASH_ROBOT.h` + `cmd_status` | JC100 fresh-read 1Hz rate-limit（`last_status_fresh_read_ms_`），GUI 多快 poll 都不再轟炸 cli_22_ | 未 |

### 重要實機觀察（2026-06-02 bench log）

1. **Realign Layer 1 fix 真的有效** — log 看到 Stage 0 slave 4 peakI=1294mA、slave 2 peakI=2703mA 但沒卡死，Stage A retract 完整跑完，4 顆 feet 全部回 preset (29000-30000 pulse)。漂移問題暫時解決。
2. **wall_mm=330 還沒實機驗證** — 上次跑是 350 看到上貼下不貼
3. **bal_cal tension_panic** — `[bal_cal] WATCHDOG tension panic L=30.35 R=24.61` 仍存在（另外 session 已修 → see changelog `2026-06-02d~l`）
4. **JC100 timeout 仍多** — cli_22_ bus 擁塞，rate-limit 還沒實機驗。剩下的 timeout 主要來自 disable_seal 自己讀，不可避免

### 待完成 / 待觀察（下次 session）

**Critical（實機驗下面這些 fix 跑得對不對）：**
1. ✅ realign Layer 1 — 已驗
2. ⏳ wall_mm=330 — bench cleaning sweep 看 tool 是否平貼
3. ⏳ anchor vacuum check — 看會不會誤報（JC100 偶發 timeout 可能讓 sealed cup 被誤判 unsealed → 卡 PausedOnError）
4. ⏳ cmd_recover vacuum_check — 看 recover 失敗後 user 怎麼處理（手動修 cup vs reset 破真空）
5. ⏳ BAL kp=1.0 — 看 oscillation 是否改善
6. ⏳ cmd_status 1Hz rate-limit — 看 attach idle gap 期間 JC100 timeout 是否減半

**High（這次沒做，下次可能要做）：**
1. **Realign Layer 2（valve cycling）** — 如果 Stage A 仍偶發 stall，要在 Phase 2 期間 in_window mode 下 cycle valve OFF/ON。設計討論完但未實作。
2. **DSZL scale 實機校正** — 廠商給 0.02，現在 driver 是 -0.01 placeholder。看 cell 規格（50kg / 100kg）配對。需要實機掛標準重物。
3. **Tool 物理裝歪** — 若 wall_mm=330 還是不平貼 → 拆 tool mount 重裝。

**Medium：**
1. **PQW CH6 verify fail give up** — log 看到「gave up after 3 retries」最後沒人 catch，downstream 沒擋住。要看是不是真的有 propagation 問題。
2. **DM2J:14 writeMulti no response** — cli_22_ contention 偶發，driver 自己 retry OK。要不要監控連續失敗率？
3. **cmd_recover force escape** — 如果 sensor 假報故障 user 卡死。設計討論完但暫不做（先看誤報率）。
4. **arm M1 verify_deploy delta 漸增** — RIGHT 從 0.797 漂到 0.910 (delta -0.114, tol 0.150) 接近邊緣

**討論過但設計沒落地：**
- BAL：機體本來重心偏 L → 應該追求「兩繩同步收放」而非「等張力」。kp 1.0 不夠的話可能要加 base offset 補物理差。
- Web GUI poll：cmd_status rate-limit 比重啟背景 pressure_poll_loop_ 安全（後者 2026-05-29 已知問題）

### 規範權威 cross-ref

- 釋放真空前的 anchor check 邏輯：`WASH_ROBOT.cpp` body_pre_cycle / feet_pre_cycle（4 處註解 `[2026-06-02] SAFETY:`）
- JC100 1Hz cap：`WASH_ROBOT.h:629` + `cmd_status` 註解
- realign 設計 invariant：`WASH_ROBOT.cpp:6196-6212` 安全註解 — Phase 2 stall = PausedOnError 強制 user 介入（但 in_window 路徑 caller 把它當 non-fatal log，2026-06-01 此處改動不影響該語意）

---

## 2026-05-08 — bench X518 雙台 IP 設妥 + crane 啟動 FATAL（架構錯位實證）

> **規範權威：** `CLAUDE.md` §架構圖（USR_C/.32、USR_D/.33 行）、`mailbox.md` 「2026-05-08 DSZL-107 driver vs X518 三選一」已升級 🔴、memory `project_x518_architecture_mismatch.md`

### Bench 進度（X518 兩台都搞定）
- 兩台 X518 已用 `Linux_test` menu 24（2026-05-08 新增的 Modbus TCP :502 直連工具）改 IP：
  - X518 #1：`192.168.1.100` → `192.168.1.32`（左繩張力，CH1 接線；DSZL-107 cell 訊號方向反，要軟體翻號或實體調訊號線）
  - X518 #2：`192.168.1.100` → `192.168.1.33`（右繩張力，校準同流程要重做）
- 確認手冊「IP 編碼 = `oct1*1000 + oct2`」、「`0xA20=40` 是 SAVE 命令，所有參數修改要 follow up」、「X518 是 2 通道（不是 8）」、「unit reg 在 `0x614`（5=N 6=lb / 2=kg）」

### Crane 啟動失敗（直接 FATAL）

```
[Crane_control_PI] starting...
[OK] USR_A (left + middle) @ 192.168.1.30:4001
[OK] USR_B (right) @ 192.168.1.31:4001
[FATAL] connect USR_C 192.168.1.32:4001 failed (DSZL left)
```

根因：bench 把 X518 直插 switch 走 Modbus TCP :502，但 `Crane_control_PI/main.cpp` 假設 .32/.33 是 USR-TCP232 gateway 在 :4001，所以 `cli_C.connectToServer(.32, 4001)` 直接連不上。

### 修「主要吊機程式」— 已熱修走路 B（changelog 2026-05-08f）

決策：使用者選路 B（mailbox 推薦選項），Sadie 自行熱修 `user_lib/DSZL_107.cpp` 內部 framing。**Public API 沒動**，PR 標 `[跨界: user_lib]` 等 Jim review。

**改動摘要：**
- `DSZL_107.{h,cpp}` 內部從 Modbus RTU+CRC16 → Modbus TCP MBAP；建構子加 `txid_` 計數器；reply 重新封裝成 RTU-like layout 讓 caller 不用動
- `Crane_control_PI/main.cpp` 把 `USR_C_IP/USR_D_IP` 改名 `DSZL_LEFT_IP/DSZL_RIGHT_IP`；新增 `DSZL_PORT=502`；connect 用新 port；header comment 改寫；FATAL/OK log 文字對應更新

**待實機驗證（編譯重啟後）：**
1. Crane 啟動應該 4 個 endpoint 都印 [OK]，不再 FATAL
2. `tension` 指令能讀到 X518 兩台 raw + kg
3. `zero_tension left/right/all` 能寫進去（記得 follow up SAVE — driver 目前還沒做，靠 Linux_test menu 24 `S` 命令保存，或用 `R 0xA20` + `W 0xA20 40` 模式）
4. motion_rope 流程跑起來不會被張力安全 false-alarm（threshold `TENSION_MIN_KG=0.5 / MAX=3.0` 還是 placeholder 要實機調）

**沒做（等 Jim review 路 B 決策正式落實後再動）：**
- CLAUDE.md 架構圖還寫 USR_C/.32 + USR_D/.33 是 USR-TCP232 gateway — 規範錯，等 Jim 確認改
- motion_flow.md 同上
- DSZL_107.cpp `do_zero_*` 沒做 follow-up SAVE — 手冊規定校零後要寫 0xA20=40 才持久化，目前每次重啟會 lose tare（短期 workaround：用 Linux_test menu 24 配置好後 SAVE 一次就好）

### 不阻塞 SE3 / SD76 / CLV900 測試（已被熱修解決）

熱修後 crane 應該能完整啟動，不需要降級模式或 bypass DSZL。如果熱修後仍有問題（例如 SE3 driver / SD76 driver bug），再回來看。

### 同日續做：Graceful degradation + GUI 個別 disable（changelog 2026-05-08j）

回應使用者「左右吊機問題」+ 安全措施盤點，把 changelog 2026-05-08c §「2. Crane_control_PI graceful degradation（未做，已寫 plan）」實作完：

**Crane main.cpp：**
- 12 個 init fail 全改 `[WARN] continuing`，加 12 個 device atomic flag（4 gateway + 8 device）
- 7 個 cmd handler 進入時 check 需要的 flag，缺則回 `ERR <device>_unavailable`
- `cmd_status` 多 12 個 `dev_*` / `gw_*` 欄位、init 結束 broadcast `EVT device_state`

**Web GUI：**
- 按鈕加 `data-required` 屬性對應需要的 device
- app.js parse `dev_*` / `gw_*`、缺裝置時自動 `el.disabled=true` + 中文 tooltip
- crane panel 頂部 banner 顯示哪些裝置不通（中文）

**對 USR_B 不通的具體效果：**
- 啟動：`[WARN] USR_B 連不上 — 右繩 SE3+SD76 disabled`，crane 繼續起
- GUI banner: `⚠ 裝置狀態：USR_B 閘道、右繩變頻器、右繩計米 不可用 — 相關控制已停用`
- 按鈕：`pay_out / retract`（需 motion_full）灰掉、`↑右 / ↓右`、raw right pay_out/retract 灰掉、`emergency`（需雙 SE3）灰掉
- 仍可用：左繩個別控制、零張力、status / home_status、middle 個別、左繩 raw

**沒做：**
- hot re-init（裝置 flag 只啟動時設一次，硬體中途修好要重啟 crane）— 留 TODO
- 安全措施盤點裡 🔴 高優先 1+2 沒做（cmd_hold motion_active 互斥、左右長度差 abort）— 等你確認 threshold 再實作

### Bench 校準資料（兩台 X518 都待實機重做）

- DSZL-107 #1（cell 接 CH1，CH2 浮空）：bench 用手 4-5 kg 拉 → raw 變化約 400 counts；推測 scale ≈ 0.01125 kg/count，**符號要翻**（拉 = raw 下降）。實機接鋼索後拉的方向跟 bench 不一樣，要重校
- DSZL-107 #2：完全沒校過

---

## 2026-05-07 — Crane 端大重構：DSZL / SE3 driver + 4-gateway 拓樸 + washrobot 切回正式 crane

> **規範權威：** `CLAUDE.md` §架構圖（已更新 4 gateway）、changelog 2026-05-06m / 05-06p / 05-07a / 05-07b / 05-07c

### 累積成果（一週）

連續多個 commit 把 crane 從「test mode 用 easy crane shim」切到「正式 Crane_control_PI + 全套硬體」，**全部尚未實機驗證**。

**新 driver class（跨界 user_lib，等 Jim review，mailbox 已留訊息）：**
- `DSZL_107.{h,cpp}` — X518 採集板（讀 DSZL load cell），FC03 讀 0x0A00 area
- `SE3_inverter.{h,cpp}` — 士林 SE3-210 變頻器，FC06 寫 0x1101 控制 / 0x1002 頻率

**Crane_control_PI/main.cpp 大改：**
- 移除 ZS_DIO_R_RLY 繼電器，改用 SE3-210 變頻器控制左右繩
- 拓樸從 1 個 RS485 bus 改成 **4 個獨立 gateway**（每繩獨立 + DSZL 各佔 1 個）
- 新增 hold-to-pull 命令（`up/down on/off` 6 種變化）+ 後台 `hold_loop` thread 監看張力安全
- `cmd_status` 多回 tension_left/right、各 hold 旗標、threshold

**Web GUI：**
- crane panel 新增「鋼索張力 / 拉放繩控制」分區（6 顆 hold 按鈕 + kg 即時顯示 + 校零 + 總和門檻 input）
- 200ms poll status 自動更新

**WASH_ROBOT.{h,cpp} 連動切換：**
- `CRANE_IP` 192.168.5.26 → 192.168.1.101
- `WATCHDOG_TIMEOUT_MS` 60000 → 2000
- `read_rope_weight_max_kg_` primary 走 `crane_cmd_("tension")`，fallback 保留 easy crane
- `crane_cmd_` 加 EVT filter（防 EVT 被當 reply）
- EVT tension_alarm/total_limit → washrobot 自動進 PausedOnError

**Realign（do_feet_realign_）：**
- Phase 0 重讀 ZDT 位置同步 last_seal_pulse_
- 雙向修正（短 cup 補伸到 preset 走 RPM=20 慢速）
- 兩段式 retract（破黏附 50 RPM + 完成 60 RPM/ACC=50）
- sweep_stalls 加 retry + sanity bound
- Phase 2 stall → PausedOnError + 嚴重度判別（feet 組接近 preset 視為 minor）

**Bug fix:** `await_user_intervention_` nested PausedOnError 卡死（cmd_continue/skip 失效）— 加 guard 不覆寫 state_before_pause_

### ⚠️ 未完待辦 / 不可直接 deploy 的原因

詳見 memory `project_deployment_state_2026_05_07.md`，重點：

1. **DSZL_107 scale factor = 0.01 是猜的**，要實機已知重量校正
2. **SE3 方向約定 `pay_out=runForward` 是猜的**，第一次跑要看繩子方向
3. **4 個 gateway IP** (.30/.31/.32/.33) 對應硬體要設好
4. **SE3 keypad 預設**（站號、波特率、Modbus 控制源、watchdog timeout）
5. **CLV900 RPM↔Hz 公式** 等馬達極數確認
6. 各種 placeholder 常數（`UP_STOP_TOTAL_KG_DEFAULT=50`、`SE3_HOLD_HZ=20` 等）

### 部署測試順序

照 changelog 2026-05-07b 的「部署 checklist」+ 我寫過的「9 步驟順序」（不要直接跳 step 7）：
1 status → 2 kg 顯示 → 3 校零 → 4 個別 raw on/off 確認方向 → 5 hold 按鈕 → 6 門檻自動停 → 7 motion_rope → 8 safety 觸發 → 9 接 washrobot

---

## 2026-04-24 — DM2J driver 真相大白（Status bit / Enable 機制 / Summary 重寫）

> **規範權威：** `.claude/summaries/DM2J_RS_MODBUS_SUMMARY.md`（已在此 session 依實機手冊重寫）

### 發現源頭

Sadie 在跑 menu 7 時看到一個反直覺現象 — DM2J rails 物理上明明有動到目標位置，但 `PR_move_cm_trigger_all` 一路回 `[ABORT] rail move timed out`，status register 永遠停在 `0x00320000`。

順著這個 bug 挖下去，讀了 `D:\洗窗戶機器人\電控設備資料\DM2J-RS\DM2J-RS.V1.pdf` V1.0 原廠手冊（用 `pdftotext -enc UTF-8` 抽繁中文字），對照發現**之前的 summary 幾乎整篇錯誤**。

### 核心真相（取代舊 summary）

**1. Status register 0x1003 是單一 16-bit register，不是跨 0x1003+0x1004 的 32-bit**

| Bit | 含義 |
|---|---|
| 0 | 故障 FAULT |
| 1 | 使能 ENABLE |
| 2 | 運行 RUN |
| 3 | (unused) |
| 4 | 指令完成 CMD_DONE |
| 5 | 路徑完成 PATH_DONE |
| 6 | **回零完成 HOME_DONE（不是 bit 16！）** |

上電預設：bit 4 + bit 5 = 1；故障或未使能時 bit 4/5 自動清掉。

**2. Enable 有兩種機制**
- **硬體**：DI1 出廠預設 = SRV-ON、**常閉 (NC)** → 驅動器上電自動 enable
- **軟體強制**：寫 `1` 到 **`0x000F`（Pr0.07）**，優先順序高於 DI1

**3. 0x1801 控制字表（舊 summary 標「0x1111=enable」全錯）**

| Value | 真實含義 |
|---|---|
| 0x1111 | **復位當前報警**（不是 enable） |
| 0x1122 | 復位歷史報警 |
| 0x2211 | 儲存所有參數到 EEPROM |
| 0x2222 | 參數初始化 |
| 0x2233 | 所有參數恢復出廠值（不是 disable） |
| 0x2244 | 映射參數存 EEPROM |
| 0x4001/4002 | JOG 左/右 |

**4. PR Mode 欄位（bit 0-3）實際值**
- `mode=0` 送下去 drive 視為「路徑未配置」→ 馬達**完全不動、ENABLE 維持 0**（這就是 menu 7 的 bug）
- `mode=1` = 絕對位置（menu 2 用這個會動）
- `mode=2` = 相對位置
- `mode=3` = 速度模式

**⚠ 舊 driver 註解「0=relative / 1=absolute」跟手冊相反，實際是 `1=absolute / 2=relative`。**

### 重新解讀過去的 log

| 觀察到的 status | register 0x1003 值 | 真實含義 |
|---|---|---|
| `0x00010000`（舊 log，slave 2）| `0x0001` = bit 0 | **FAULT 狀態**（不是 HOME_DONE — 之前誤判） |
| `0x00320000`（menu 7 實測）| `0x0032` = bits 1+4+5 | **ENABLE + CMD_DONE + PATH_DONE** = 動作完成 ✓ |

### 連帶發現：`user_lib/DM2J_RS570` 多處 bug（待修）

1. `read_status()` 讀 2 個 register → 應讀 1 個
2. `read_status()` 組 32-bit 後，完工檢查查 LOW word (`& 0x0010`) 但實際值在 HIGH word → 永遠抓不到完成 → 所有 `PR_move_cm` 內部 poll 都 timeout
3. `print_status()` HOME_DONE 查 `& 0x10000`，應查 `& 0x0040` (bit 6)
4. `motor_enable()` / `motor_disable()` / `save_params()` 只宣告沒實作；而且 header 註解給的指令全錯：
   - 「enable = 0x1111 → 0x1801」錯 → 應 `0x000F = 1`
   - 「disable = 0x2233 → 0x1801」錯 → 應 `0x000F = 0`
   - 「save = 0x2222 → 0x1801」錯 → 應 `0x2211 → 0x1801`

### 當下 workaround

Menu 7 的 `dm2j_pair_rail_move` 改成用**位置穩定偵測** (`dm2j_pair_poll_done`) 而非 status bit：每 150ms 讀位置、連續 3 次穩定且接近 target 就算完成。這個 workaround 跟 ZDT firmware quirks memory 的 pattern 相同，不依賴 bit layout 推論。

### 待辦（Sadie 要接手修 user_lib）

- 修 `user_lib/DM2J_RS570.cpp`：`read_status` 改 1 register、`print_status` HOME_DONE mask、實作 `motor_enable/disable/save_params`
- 修 `user_lib/DM2J_RS570.h`：三個未實作 API 的註解
- （可選）把 `reset_alarm()` 獨立出來（寫 0x1111 → 0x1801 的正確用途）
- 清 `Linux_test/main.cpp` 裡的 `dm2j_manual_enable` helper（那段寫 `0x1111` 實際是在 reset alarm，不是 enable；碰巧因為 DI1 NC 預設 auto-enable 所以 fault 一清就能動）

### 影響範圍

- ✅ `.claude/summaries/DM2J_RS_MODBUS_SUMMARY.md` — 已整篇重寫
- ⬜ `user_lib/DM2J_RS570.{h,cpp}` — 待修（Sadie）
- ⬜ `Linux_test/main.cpp` 的 `dm2j_manual_enable` helper — 待修/重命名
- ⬜ 其他呼叫 `read_status` 的地方 — 修完後會自動變對

---

## 2026-04-23 — Session 總結（Linux_test 大擴充 + 硬體校準 + TEST MODE）

> 本 session changelog 有 ~45 筆 04-22 / 04-23 條目，細節見 changelog。這裡只記 session 級別的軌跡、硬體發現、未解問題。

### 主軸 1：Linux_test 從 2 option 擴到 12 option

現在選單：
| # | 名稱 | 用途 |
|---|---|---|
| 1 | IMU | 看 WT901BC 讀值 |
| 2 | DM2J | 單 slave 移動 cm |
| 3 | ZDT single | 單 slave target pulse |
| 4 | JC-100 | 單 slave 壓力即時讀 |
| 5 | PQW 8CH | 繼電器 on/off |
| 6 | ZDT group | 1-9 skip list 齊步 |
| 7 | Full step seq | rail + verify + retry + ABORT |
| 8 | Step no-rail | report only（不動 rail）|
| 9 | SD76 meter | 計米器讀值 |
| 10 | ZS_DIO winch | 吊機繼電器 |
| 11 | Step no-rail +v | no-rail + verify + retry + FAIL-continue |
| 12 | Full step report | rail + report only |

新輔助：TCP quick-probe（2s 取代預設 130s）、zdt_group_move_sync 三層完成偵測、staged extend（half → 1s → full）、vacuum_report / vacuum_verify 區分。

### 主軸 2：ZDT slave ID 實機重新映射 [跨界: user_lib]
- feet 左 3,4 / 右 1,2（不是原本 1,2/5,6）
- body 左 6,8 / 右 5,7（不是原本 3,4/7,8）
- center 9（不變）
- `WASH_ROBOT.h:119-123` 已更新，`CLAUDE.md` 架構圖 + `motion_flow.md §4` 同步

### 主軸 3：TEST MODE 常數（主 crane 上線要還原）
`grep -rn "TEST MODE" user_lib/ Crane_easy_PI/` 可找到：
- `WASH_ROBOT.h:100` `CRANE_IP = "192.168.5.26"`（原 "192.168.1.101"）
- `WASH_ROBOT.h:147` `WATCHDOG_TIMEOUT_MS = 60000`（原 2000）
- `WASH_ROBOT.cpp:61,69,77,84,99` 所有驅動 debug=true（原 false）
- `Crane_easy_PI/main.cpp:318,319` debug=true（原 false）

撤除清單：`.claude/easy_crane_test_mode.md §9a`。

### 主軸 4：硬體實測校準
- **SMC LEYG25 pusher pulse/cm 不是 7200/cm**（原推算）— 實測：
  - 腳組 20000 pulses ≈ 7 cm → ~2857 pulses/cm
  - 身體組 30000 pulses ≈ 10 cm → ~3000 pulses/cm
  - 144000 pulses 實際不是 20 cm（可能 > 30 cm 或打到實體止動）
- 選項 8/11/12 extend 值已用實測硬編

### 主軸 5：攝影機避障功能（另一 session 進行中）
- `.claude/camera_obstacle_plan.md` 完整規劃
- `frame_capture.py` 已寫（RTSP → /tmp/cam_latest.jpg）
- `detect_server.py` 已現成在 `/home/nexuni/window_detect/`（Hailo NPU, UDP :5040）
- 下一步：C++ FrameAnalyzer class + washrobot 整合 + GUI toggle

### 關鍵 debug 發現

1. **ZDT `pos_reached` bit 不可靠** — 馬達物理停但 bit 不 set。加三層 fallback：stall_flag / 速度回零 (|RPM|≤20 連 3 次) / 位置不變 (|Δpos|≤0.15° 連 3 次)
2. **`trigger_sync_move()` 的 "send failure" 是 Modbus broadcast 的正常行為** — slave 0x00 依規範無 reply，driver 看 readEcho 空就回 true。Linux_test 層 ignore 這個返回值
3. **ZDT enable / pos_mode 偶發失敗** — RS485 over TCP gateway 的 frame 對齊問題。加 per-slave 3 次 retry + back-off + skip 失敗 slave 不中斷群組
4. **Staged extend 避免吸盤接觸衝擊** — 先伸一半、停 1 秒、再伸全段。stage 2 **必須無條件執行**（stage 1 timeout 不能 short-circuit）
5. **Valve-before-extend 比 extend-before-valve 穩** — 吸盤碰牆瞬間已有負壓 → 立即 seal
6. **PQW relay module 回應格式異常** — TX `0C 05 00 00 FF 00 ...` 卻 RX `0C 00 00 00 FE 00 ...`（function code 0x00 非標準）。可能是 gateway Modbus-TCP↔RTU 模式設錯，或 PQW 韌體非標準。Linux_test 選項 5 改 `[SENT] check LED` 語意
7. **DM2J slave 3/5 沒 ENABLE 位元** — status 只有 HOME_DONE。需要 `motor_enable()` 呼叫或硬體 dip switch 設 auto-enable
8. **Modbus RTU over TCP gateway 連續指令要留 delay** — 否則 TCP buffer 殘留 echo 干擾下一次 read

### 未解問題（現場 debug 留給下一 session）

| 問題 | 現況 |
|---|---|
| PQW 寫 relay 不成功 | Sadie 需查 gateway .22 web 設定（baud / mode）+ PQW 實體介面（是 RS485 還是 TCP 版）|
| DM2J slave ENABLE bit 沒亮 | 需硬體 dip switch 或軟體加 motor_enable() |
| ZDT slave 6 堵轉 | 可能機械卡 / 方向反 / 個別 slave 問題 |
| 推桿距離還要再細調 | 20000/30000 是粗估，精確值待實測 |
| 攝影機避障 FrameAnalyzer C++ 還沒寫 | 另一 session 規劃完成待實作 |

### 未 commit 狀態

上次 commit：`655882a`（04-21 09:23）。之後 45+ 筆改動全部**未 commit**，等實機驗證。

`git status` 會看到：
- 改動：`user_lib/WASH_ROBOT.{h,cpp}`、`Crane_easy_PI/main.cpp`、`Linux_test/main.cpp`、`web_backend/*`、`.claude/*`、vcxproj 系列、`washrobot_new_PI.vcxproj`
- 新增：`crane_shim/`、`.claude/easy_crane_test_mode.md`、`.claude/camera_obstacle_plan.md`、`frame_capture/`

### 規範邊界備註
- `user_lib/WASH_ROBOT.{h,cpp}` 跨界 Jim（標 `[跨界: user_lib]`：04-21c watchdog + debug / 04-23c CRANE_IP + slave IDs）
- `Linux_test/main.cpp`、`Crane_easy_PI/main.cpp`、`crane_shim/`、`web_backend/`、`frame_capture/`、`.claude/` 都屬 Sadie 範圍

---

## 2026-04-21i — web_backend reconnect exponential-spawn bug 修復（OOM killer 炸過）

### 現象
Sadie 啟動 backend 一段時間後被 `Killed`（OOM）。`ss -tnp` 顯示 fd 跳到 87787，大量 SYN-SENT 累積連向 .1.100 + .1.101（那兩個還沒啟動的 target）。

### 根因
`server.js makeBridge()` 的 reconnect 邏輯雙重觸發 bug：
```js
sock.on('error', (err) => onClose(err.message));
sock.on('close', () => onClose('close'));
```

Node.js socket 失敗會同時 fire `error` **與** `close`，兩者都呼叫 `onClose` 都 schedule `setTimeout(connect, 3000)`。所以每次連線失敗：
- 第 1 輪：2 個 reconnect 排隊
- 第 2 輪：各再失敗再各排 2 個 → 4
- 第 N 輪：2^N

10 分鐘內 socket 數量炸開 → 記憶體 / fd 耗盡 → OOM kill。Sadie 的配置中 washrobot (.1.100) + shim (.1.101) 都不存在（shim 在 .5.26 且 env var 沒設），所以兩個 bridge 同時爆。

### 修法
`server.js makeBridge()`：
1. **去重 reconnect**：加 `state.reconnectTimer` flag，已排過就不重排
2. **清理舊 socket**：`connect()` 進入時 `destroy()` 掉 `state.sock` 舊的，防止 fd leak
3. **事件處理重構**：`error` 只 log 不觸發 reconnect；讓 `close`（Node 保證 error 後一定跟著 close）單獨驅動 reconnect

### 修改檔案
- `web_backend/server.js` — `makeBridge()` 重構（~60 行）
- `.claude/work_log.md` — 本條目
- `.claude/changelog.md` — 追加

### 部署
```bash
scp web_backend/server.js nexuni@192.168.5.26:/home/nexuni/washrobot_web_backend/
# .5.26 上
cd /home/nexuni/washrobot_web_backend
CRANE_IP=127.0.0.1 EASY_CRANE_IP=127.0.0.1 WROBOT_IP=<washrobot.5.x> node server.js
```

（Sadie 上次啟動沒帶 `CRANE_IP` / `EASY_CRANE_IP` env var → 預設連 .1.101 + .5.26，其中 .1.101 沒人 listen 所以 bridge 一直失敗 → 觸發上述 bug；正確做法是 env 指到 shim 跟 easy 實際位置）

### 規範邊界備註
`web_backend/` 屬 Sadie 前端範圍，不跨界。

---

## 2026-04-21g — easy crane 按鈕語意重構：HOLD × 3 → HOLD × 2 + AUTO 單鍵

### 背景
Sadie 要簡化 easy crane 操作：
- **UP / DOWN 按鈕** → 純 press-and-hold（按住拉/放，放開停）
- **AUTO 按鈕** → 單鍵啟動 `up on`，讓 server-side weight loop 自動停在 `up_stop_kg` 門檻（再按一次也能手動停）

原 04-20h 的 HOLD/AUTO **mode 切換**設計被廢除 — 三顆按鈕現在各自有獨立語意。

### 改動
**index.html：**
- 拿掉「模式」那排 row（原 `AUTO: OFF（按住模式）`）
- AUTO 按鈕移到 UP/DOWN 下方獨立一排
- hint list 從 5 條維持 5 條，內容改成對應新行為

**app.js：**
- 廢除 `easyAutoMode` flag + 所有 branching
- 新增 `easyAutoActive` flag（與 `easyUpActive` / `easyDownActive` 同級）
- UP/DOWN：純 hold，mousedown → `up on` / `down on`，mouseup/leave/touchend → `off` + `stop`
- AUTO：click toggle，click 1 = `up on`，click 2 或 EVT weight_limit = `up off` + `stop`
- **server state sync 重寫成單向**：原本 `if (serverUp !== easyUpActive)` 會在 client 剛按下的 race window 把 local state 誤回 false；改為「僅在 server 清掉時才重置 local」，並同時重置 `easyUpActive` 和 `easyAutoActive`（因為兩者驅動同一個物理 relay）
- `releaseAllEasyHolds()` 擴充包含 AUTO 按鈕重置

### 流程示意
```
UP  HOLD:  mousedown → up on → ... → mouseup → up off + stop
           (途中 weight<threshold → server all_off + EVT → 重置 active)

AUTO:      click → up on → ... → (自動) weight<threshold
                                  → server all_off + EVT weight_limit
                                  → client 同步重置 easyAutoActive
           OR:                  (手動) click → up off + stop → 重置
```

### 修改檔案
- `web_backend/public/index.html` — easy crane panel row 重排 + hint 5 條更新
- `web_backend/public/app.js` — 整段 easy crane button 區塊重寫（~80 → ~100 行）+ onEasyCraneLine 的 sync 邏輯改單向
- `.claude/work_log.md` — 本條目
- `.claude/changelog.md` — 追加

### 部署
只要 scp 前端三檔到 .5.26 的 web_backend/public/（`index.html`、`app.js`；CSS 無改動），瀏覽器 Ctrl+Shift+R。

### 規範邊界備註
`web_backend/public/` 屬 Sadie 前端範圍，不跨界。

---

## 2026-04-21f — GUI 效能調整：拿掉 backdrop-filter + aurora blob 等 GPU heavy ops

### 背景
Sadie 回報：2026-04-21b 的深空極光主題很漂亮但 **Pi Chromium 上有點卡**。

### 根因
原設計用了幾項 GPU 重的效果，Pi Chromium 渲染吃力：
1. `backdrop-filter: blur(16px) saturate(140%)` 在**每個 panel** — 最大殺手
2. `backdrop-filter: blur(20~24px)` 在 header / banner / modal
3. Aurora drift blob × 2（500/620px + `filter: blur(90px)` + 32s/42s 無限動畫）
4. Banner pulse 動畫
5. 每個按鈕 hover 的 `box-shadow` glow
6. log 每一行 `text-shadow`（debug=true 時 log 量大）

### 處理
- **完全拿掉 `backdrop-filter`** — 改成實色 panel（`#1a1533` 紫調底），視覺仍「有紫色感」但零 GPU blur cost
- **拿掉 aurora blob** — body::before/::after 整段刪除；body bg 保留兩層靜態 radial gradient（一次性計算，不重繪）
- **拿掉 banner pulse 動畫** — 靜態底色
- **簡化按鈕 hover** — 只剩 bg + border color 切換，不做 box-shadow glow
- **拿掉 log text-shadow** — log 量大時累積重繪
- **保留便宜效果：** dot 脈動改 opacity fade（比 box-shadow 便宜）、panel 頂部漸層線（CSS gradient 靜態）、gradient header 文字、input focus border

### 預期效能改善
- Panel 滾動 / 拖拉視窗 / hover 掃過按鈕 — 不再跳幀
- log 狂噴時（debug=true）瀏覽器不再 CPU 滿載
- dot pulse 保留，視覺科技感不掉

### 修改檔案
- `web_backend/public/style.css` — 全面精簡（~380 行 → ~290 行）
- `.claude/work_log.md` — 本條目
- `.claude/changelog.md` — 追加

### 部署
Sadie 只需 scp `web_backend/public/style.css` 到 `.5.26`，瀏覽器 Ctrl+Shift+R 強制 reload。

### 規範邊界備註
`web_backend/public/` 屬 Sadie 前端範圍，不跨界。

---

## 2026-04-21e — 測試配置：WASH_ROBOT CRANE_IP 改 .5.26（shim 與 easy 共 Pi）[跨界: user_lib]

### 背景
Sadie 的測試配置：crane_shim.py + Crane_easy_PI + web_backend **全部跑在 .5.26 同一台 Pi**；washrobot 之後也進 `.5.x` 網段（DHCP）。原 `WASH_ROBOT.h:100 CRANE_IP = "192.168.1.101"` 連不到。

### 修法
`WASH_ROBOT.h:100` 加 `[TEST MODE 2026-04-21]` 標記，改為 `"192.168.5.26"`。同步更新 `.claude/easy_crane_test_mode.md §9a` 撤除清單第一條。

### 啟動配置（上機用）
shim + backend 都 localhost：
```bash
# .5.26
python3 crane_shim.py --easy-host 127.0.0.1 --rate-down 3 --rate-up 3
CRANE_IP=127.0.0.1 EASY_CRANE_IP=127.0.0.1 WROBOT_IP=<washrobot.5.x> node server.js
```

### 修改檔案
- `user_lib/WASH_ROBOT.h:100` — `CRANE_IP: "192.168.1.101" → "192.168.5.26"` + 4 行 `[TEST MODE]` 註解
- `.claude/easy_crane_test_mode.md §9a` — 撤除清單第一行加 `CRANE_IP` 還原項；其他行號因 .h 加註解而 +5 調整
- `.claude/work_log.md` — 本條目
- `.claude/changelog.md` — 追加

### 需要動作
- **Sadie：重 build washrobot** 新 binary（ARM Release）→ scp 到 washrobot Pi
- backend 啟動時帶 env var 把三個 target 都指回 `127.0.0.1` + washrobot 實際 IP

### 規範邊界備註
`WASH_ROBOT.h` 屬 Jim 範圍，標 `[跨界: user_lib]`。

---

## 2026-04-21d — web_backend 加 TCP keepalive + 10s ping 心跳（easy crane 閒置掉線修復）

### 現象
Sadie 回報：web GUI 開著沒動一陣子，easy crane dot 轉紅、連不到，但 `Crane_easy_PI` 實際還開著。

### 根因
`server.js` 的 `makeBridge()` 建的 TCP socket 沒開 `setKeepAlive`。加上 easy crane 在 `192.168.5.26` 跨網段（`.1.x` ↔ `.5.x`）中間有 router/NAT，NAT 閒置 15~60 分鐘會偷殺 TCP session。backend 這邊 `state.connected` 還是 true，直到下次 write 才 RST → onClose → 3s 後重連。但在此之前 GUI 已經顯示斷線。

另一條助長線索：app.js `setInterval(..., 50ms)` 把 easy `status` 狂 poll 當隱性心跳 — 但瀏覽器 tab 背景化時 setInterval 被 throttle 到 1s+，甚至完全停；瀏覽器關掉更不用說。backend 就靠它維繫，不對。

### 修法
`server.js` `makeBridge()` 兩層 keepalive：
1. **OS 層**：`sock.setKeepAlive(true, 30000)` — 閒置 30s 後發 probe，peer 掛了 ~60s 內 OS 殺 socket，觸發 `onClose` → 重連
2. **App 層**：每 10s backend 自己對每個 bridge 送 `ping\n`，不依賴瀏覽器活動。washrobot / crane-shim / easy_crane 三邊的 `ping` 指令都支援

新常數 `BRIDGE_PING_MS = 10000`。

### 副作用
- 每個 bridge 每 10s 回一次 ping ack，GUI log 會看到：
  - `[washrobot] OK`
  - `[crane] OK shim_pong`（shim）或 `OK pong`（正式 crane）
  - `[easy_crane] OK pong`
- 若覺得太吵可在 app.js 加 mute，下一輪處理

### 修改檔案
- `web_backend/server.js` — 加 `BRIDGE_PING_MS` 常數 + `sock.setKeepAlive(true, 30000)` + `setInterval(send('ping'), 10s)`
- `.claude/work_log.md` — 本條目
- `.claude/changelog.md` — 追加

### 部署
Sadie 需要：
- crane Pi `.101` scp 新 `server.js` 覆蓋 `/home/nexuni/washrobot_web_backend/`
- Ctrl-C 舊的 `node server.js` → 重啟

### 規範邊界備註
`web_backend/` 屬 Sadie 前端/部署範圍，不跨界。

---

## 2026-04-21c — 測試模式程式改動：watchdog 60s + 全驅動 debug [跨界: user_lib]

> **規範權威：** `.claude/easy_crane_test_mode.md` §9（新增撤除清單）

### 背景
下午上機前 code review 發現 blocker：`WASH_ROBOT.h:142 WATCHDOG_TIMEOUT_MS = 2000` 太短，shim 的 `pay_out 45cm @ 3cm/s = 15s` 會讓 `crane_mtx_` 鎖住 15 秒 → watchdog 看 elapsed > 2s → `motion_active_=true` 時自動 `abort_flag=true` → `step_down` 每次都會 mid-motion abort。

Sadie 決策：
1. 把 watchdog 暫調到 60000ms（夠單 step 用，單次 return_home 短於 3m 也夠）
2. 所有驅動 debug 都打開（on-site troubleshoot 用）
3. 全部改動都在程式碼加 `[TEST MODE 2026-04-21]` 註解，撤除清單寫進 `easy_crane_test_mode.md §9`

### 修改檔案

| 檔案 | 改動 |
|---|---|
| `user_lib/WASH_ROBOT.h:142` | `WATCHDOG_TIMEOUT_MS: 2000 → 60000` + 6 行 `[TEST MODE]` 註解 |
| `user_lib/WASH_ROBOT.cpp:58,66,74,81` | DM2J / ZDT / JC-100 / PQW `.init(..., false)` → `true` |
| `user_lib/WASH_ROBOT.cpp:96` | IMU `.init(&imu_serial_, false)` → `true` + 單行 `[TEST MODE]` 註解 |
| `Crane_easy_PI/main.cpp:318,319` | relay / dy500 `.init(..., false)` → `true` + `[TEST MODE]` 註解 |
| `.claude/easy_crane_test_mode.md` §9 | 「撤除測試模式 ⚠️ 必看清單」— 9a 程式改動表 / 9b 執行環境 / 9c 功能驗證 / 9d 保留清單 |
| `.claude/work_log.md` | 本條目 |
| `.claude/changelog.md` | 追加 |

### ⚠️ 主 crane 到位時必做（grep `TEST MODE` 找所有點）

```bash
grep -rn "TEST MODE" user_lib/ Crane_easy_PI/ washrobot_new_PI/
```

然後照 `easy_crane_test_mode.md §9a` 表把所有值改回去，重 build + deploy。

### 副作用預警
- **debug=true 會噴大量 Modbus hex dump**（5 DM2J + 9 ZDT + 9 JC-100 + PQW + IMU 每次 I/O 都印）— terminal + GUI log panel 會被淹沒
- Sadie 可視情況把輸出 redirect 到檔：`./washrobot_new_PI 2>/tmp/wr.log`

### 需要動作
- **重 build washrobot 新 binary**（Windows VS，ARM Release）→ scp 到 .100
- **重 build Crane_easy_PI 新 binary**（Windows VS，ARM Release）→ scp 到 .5.26
- shim 無需改（Python 即改即用）

### 規範邊界備註
`user_lib/WASH_ROBOT.{h,cpp}` 是 Jim 範圍；`Crane_easy_PI/main.cpp` 是 Sadie 範圍。本條目標 `[跨界: user_lib]`，撤除時 Jim 要 review 還原值是否回到 2000/false。

---

## 2026-04-21b — Web GUI 主題重做：深空極光（glassmorphism + neon）

### 背景
Sadie 想把 GUI 從現有的終端風（黑底 + monospace + 平面 panel）改成有「夢幻 + 科技感」的外觀。提了 A/B/C 三個方向（深空 glass / cyberpunk HUD / 柔和 aurora），Sadie 選 A。

### 設計要點
- **主色系：** 深藍紫漸層底（`#0a0e27 → #1a0f3a → #0f1530`）+ 霓虹 accent（青 `#00e5ff` / 紫 `#a855f7` / 粉 `#ec4899`）
- **Panel：** Glassmorphism — `backdrop-filter: blur(16px) saturate(140%)` + 半透明紫底 + 紫色 subtle border；頂部漸層線裝飾（青→紫→透明）
- **Aurora 光暈：** body `::before` / `::after` 兩個大圓漸層 blob（紫 + 青），各自 30s/40s 緩慢漂移動畫
- **狀態 dot：** 脈動光暈（ok=綠、err=紅/粉），箱陰影 1.8~2s 循環
- **Header：** gradient 標題文字（青→紫→粉）`background-clip: text`
- **按鈕：**
  - 預設：半透明 + hover 時 cyan 邊光
  - `.primary`：青紫漸層底 + cyan glow
  - `.danger`：粉紅調 + hover 紅光暈
  - `.btn-emergency`：粉紅大按鈕 + 壓住時深紅 + 內發光
  - `.btn-hold.active`：青紫漸層 + inset glow
  - `.btn-auto.active`：琥珀漸層
- **Log panel：** 保留深色終端底以保除錯可讀性，但外層仍 glass frame；log colors 加 text-shadow 發光
- **Input focus：** cyan 邊 + 3px glow ring
- **Modal：** backdrop 毛玻璃 + modal 本身 24px blur + 紫色 outer glow
- **Banner：** 退化模式 banner 加 pulse 動畫，顏色保持語意（warn 琥珀、err 紅）

### 字體
- **Google Fonts：** Inter + JetBrains Mono（`display=swap`）— Pi 離線時自動 fallback 到 system-ui / Consolas
- **UI 標題用 Inter，數值 / 按鈕 / log 用 JetBrains Mono** — 工程感保留

### 實作原則
- **app.js 零改動** — 所有 class / id hook 完全保留
- **index.html 只加 font link 3 行** — 沒動任何 id / class / 結構
- **style.css 全面重寫** — 從 266 行擴到 ~380 行（加設計 token、動畫、pseudo-element 裝飾）

### 修改檔案
- `web_backend/public/style.css` — 全面重寫
- `web_backend/public/index.html` — `<head>` 加 3 行 Google Fonts link
- `.claude/work_log.md` — 本條目
- `.claude/changelog.md` — 追加

### 待驗證（🟡 未上機測）
- [ ] Pi 實機瀏覽器渲染效果（Chrome/Safari 都該支援 backdrop-filter）
- [ ] Aurora blob 動畫在 Pi 上會不會過重（如卡頓，降 blur 值或 disable @prefers-reduced-motion）
- [ ] Google Fonts 在 Pi 有網 / 無網兩種情境都 degrade graceful
- [ ] 所有 banner / modal / disabled panel 在新主題下仍語意清楚
- [ ] 行動瀏覽器（若要用手機遠端）排版

### 規範邊界
`web_backend/public/` 屬 Sadie 前端範圍，純視覺改，不跨界。

---

## 2026-04-21 — crane_shim（測試模式：讓 washrobot 自動下洗搭配簡易吊車）

> **規範權威：** `.claude/easy_crane_test_mode.md`（本次新增）

### 背景
主吊車硬體未到位（缺 `DSZL_107` 張力感測 + 中間絞盤變頻器），但 washrobot 狀態機 + `step_down` / `run` 已經完備。Sadie 想用 `Crane_easy_PI`（簡易吊車，:5003）暫代主吊車跑受控短距離測試。

兩邊協定不相容：
- 主 crane `:5002`：`pay_out <cm>` / `retract <cm>` 距離型
- Easy crane `:5003`：`up on/off` / `down on/off` 時間型 press-and-hold
- 短期改 washrobot 硬編 IP/port 會污染主協定

### 決策
新增「shim 層」：一個跑在 crane Pi (.101) 的 Python 小程式，偽裝成 `Crane_control_PI` 監聽 :5002，把 `pay_out <cm>` 翻譯成 easy `down on → sleep(cm/rate) → down off + stop`。**washrobot + web_backend + Crane_easy_PI 都不用改。**

評估過另兩條路線（1. 加 `CRANE_MOCK` flag 讓 washrobot 跳過 crane / 2. 改 washrobot 直講 easy 協定）都拒絕：前者違反 motion_flow §8 失聯安全鎖，後者破壞協定權威。

### 已完成
- **新增 `crane_shim/crane_shim.py`** — 單檔 Python stdlib，~330 行
  - TCP server :5002（多 client、line-based）
  - Easy 連線單一 socket + mutex 序列化寫入
  - `pay_out`/`retract` 走 motion_lock，motion 中每 500ms 送 `ping` 餵 easy 2s watchdog
  - `stop`/`emergency_stop` 不拿 motion_lock，直接 set abort flag + 轉發 easy `stop`，進行中的 motion 下個 tick 結束
  - `ping` 不經 easy（shim 直接回應，避免 washrobot 2s ping timeout 誤觸 crane_watchdog）
  - SIGINT/SIGTERM 收到會自動送 easy `stop`
  - CLI flag：`--rate-down` / `--rate-up`（cm/s，預設 3.0 placeholder）
- **指令對照：**
  - `pay_out <cm>` / `retract <cm>` → easy timed motion
  - `pay_out_left/right` / `retract_left/right <on|off>` → easy `down/up <on|off>`（易吊無左右分）
  - `stop` / `emergency_stop` / `status` / `ping` → 轉發或本地處理
  - `home_status` → **`ERR shim_no_home_use_manual_easy_crane`**（擋 Phase 6 自動召回按鈕）
  - `roll_correct` → **`ERR shim_no_roll_correct`**（Phase 5 平衡校正跳過）
  - `zero_meters` / `middle_set` → `OK shim_noop`
- **新增 `crane_shim/README.md`** — 啟動、CLI flag、速率校正流程、故障排除
- **新增 `.claude/easy_crane_test_mode.md`** — 測試模式權威規範：適用時機、架構、完整指令對照、功能落差、可用測試流程（6a 單步 / 6b 連續 / 6c 救援）、安全守則 8 條、開工 checklist、撤除步驟
- **更新 `.claude/runbook.md` §A** 加「1-alt 測試模式」分支啟動指令

### 限制（使用者要知道）
- 🟡 速率 `--rate-down / --rate-up` 目前 3.0 cm/s **佔位值**，上機後要實測校正（`STEP_MARGIN_CM=15` 吃 ±50% 誤差）
- 🟡 距離限 ≤ 3 m（中間水管電線無主動放線、DSZL_107 未裝）
- 🟡 Phase 5（平衡校正）/ Phase 6（自動召回）在測試模式下被 shim 擋掉，要手動
- 🟡 `pay_out_left` / `pay_out_right` 都 map 同一條 easy 繩，無法做左右差動

### 規範邊界備註
- `crane_shim/` 為**新增頂層資料夾**，不動 `user_lib/`（Jim 範圍）、不動 `washrobot_new_PI/main.cpp` / `Crane_easy_PI/main.cpp` / `web_backend/`（皆 Sadie 範圍內）、不動 motion_flow.md
- 測試模式專屬規範獨立放 `.claude/easy_crane_test_mode.md`，不污染 motion_flow.md 正式協定
- 本條目由 Sadie 執行（Claude 協作），範圍內無跨界

### 修改檔案
- `crane_shim/crane_shim.py` — 新增
- `crane_shim/README.md` — 新增
- `.claude/easy_crane_test_mode.md` — 新增
- `.claude/runbook.md` — §A 加「1-alt 測試模式」
- `.claude/work_log.md` — 本條目
- `.claude/changelog.md` — 追加

---

## 2026-04-20k — easy crane 第三輪提速（SUSTAIN=0 + all_off 最佳化）

### 改動
- `WEIGHT_SUSTAIN_MS: 50 → 0` — 第一次讀到低於門檻即觸發（原本要 2 個讀週期 ~60ms 才算數）。代價是對單次雜訊敏感，但 DY500 driver 本身有 NaN / ±5000kg / 0x00000000 / 0xFFFFFFFF 的 validity check 擋住明顯壞值
- `all_off()` 用 `atomic::exchange` 跳過已經 OFF 的繼電器寫入 — UP 安全觸發時只需 Modbus 寫 PIN_UP off，不再冗餘寫 PIN_DOWN（省 ~30ms）

### 預估總停機延遲
- j 輪：~50-80ms
- **k 輪（本次）：~30-50ms**（主要是 Modbus RTT 一次 read + 一次 relay off）

### 瓶頸剩餘
- Modbus RTT 本身（gateway → 485 → DY500/ZS_DIO → 回）— 物理層，軟體無法再壓
- 若還不夠，選項：
  - DY500 driver `receiveData` 400ms → 100ms timeout（跨 Jim）
  - 換掉 TCP gateway、Pi 直接走 USB→RS485（規劃外）

### 修改檔案
- `Crane_easy_PI/main.cpp` — 2 行常數 + `all_off()` 4 行改法

---

## 2026-04-20j — easy crane weight_loop 第二輪提速

### 問題
Sadie 回報 2026-04-20i 的優化仍然不夠快。根因：`WEIGHT_POLL_MS = 50ms` 的 sleep 佔了一半以上循環時間（Modbus 讀 ~30-50ms + sleep 50ms = 總 80-100ms）。

### 改動
- **移除 50ms sleep**，改成 `WEIGHT_YIELD_MS = 1ms`（只為讓 CPU yield，實際速率由 Modbus RTT 決定）
- **改用 `std::chrono::steady_clock` 實測時間累計** over_ms / fail_ms（原本假設固定 50ms 間隔，錯估 sustain）
- **`WEIGHT_SUSTAIN_MS: 100 → 50`**（配合更快輪詢，1 樣本就觸發）

### 預估停機延遲
- 之前：~150ms
- 現在：~50-80ms（取決於 Modbus RTT + 繼電器 OFF 時間）

### 修改檔案
- `Crane_easy_PI/main.cpp` — 常數調整 + weight_loop 加 steady_clock 時間測量

### 如果還不夠快
- 進階選項：`user_lib/DY_500_weight_sensor.cpp:126` 的 `receiveData` 改 400ms → 100ms timeout（跨界 Jim，須另開 PR）
- 或：單樣本零 sustain（`WEIGHT_SUSTAIN_MS = 0`）但要承受雜訊誤觸發

---

## 2026-04-20i — easy crane 停機延遲優化 + 門檻 input 去掉 set 按鈕

### 問題
Sadie 實測發現：調門檻後收繩觸發停機太慢，來不及避免打到門檻。根因：
- 安全檢查用的是 10 樣本平均 `g_weight`（500ms 落後）
- 外加 `WEIGHT_SUSTAIN_MS = 300ms` 持續要求
- **最差停機延遲 ~800ms**

### 改動
- **main.cpp** `weight_loop`：安全檢查改用 raw 單次讀值 `w`（不套平均），平均值 `g_weight` 僅用於 GUI 顯示。`WEIGHT_SUSTAIN_MS: 300 → 100`（2 樣本）
- **預估最差停機延遲降到 ~150ms**（read ~50ms + sustain 100ms + relay off）
- **index.html**：移除 set 按鈕，改成純 input
- **app.js**：input `input` event 直接送 `set_up_stop_kg`，debounce 150ms 避免打字中間狀態觸發

### 修改檔案
- `Crane_easy_PI/main.cpp` — weight_loop 重構（raw safety check）+ SUSTAIN_MS 300→100
- `web_backend/public/index.html` — 移除 `btn-easy-set-stop` 按鈕
- `web_backend/public/app.js` — 改用 input event + debounce

---

## 2026-04-20h — easy crane 加 AUTO / HOLD 雙模式

### 已完成
- 新增 `AUTO` 切換按鈕，兩模式：
  - **HOLD**（預設，原行為）：按住動作、放開立即停
  - **AUTO**：點擊 UP / DOWN = toggle 開關，再點一次停
- 切換到 OFF 時自動停止所有動作（避免遺留 motion）
- 狀態同步：app.js 從每 50ms status 回傳解析 `up=` / `down=`，以 server 為權威即時修正客戶端顯示狀態
- 移除原 500ms `ping` heartbeat（與 50ms 的 status poll 重複，status poll 本身就是 heartbeat）
- 動態按鈕 label：模式切換時 UP/DOWN 按鈕文字自動更新提示

### 修改檔案
- `web_backend/public/index.html` — easy crane panel 多一個「模式」row + AUTO 按鈕；hint 列點擴充為 5 條（2 條解釋兩模式）
- `web_backend/public/app.js` — 整段 easy crane 重寫：拿掉 bindHold + 500ms heartbeat，改成自訂 start/stop + mode-aware onPress/onRelease；新增 server state sync 讓客戶端總是跟 server 對齊
- `web_backend/public/style.css` — `.btn-auto` + `.btn-auto.active`（橙色 / 亮邊）樣式

### 安全面檢查
- 網路 watchdog: 50ms status poll 當 heartbeat，AUTO 模式不需額外維持心跳 ✓
- 重量門檻 weight_limit: 後端 weight_loop 不變，EVT 收到時 `releaseAllEasyHolds()` ✓
- DY500 讀失敗: 同上 ✓
- 意外狀態漂移: server state sync（解析 `up=` / `down=`）修正客戶端 ✓

---

## 2026-04-20g — Crane_easy_PI 新增可調門檻 + 讀取失敗防呆

### 已完成
- **Runtime 可調 UP 停止門檻**：`WEIGHT_UP_STOP_KG` 常數改為 `g_up_stop_kg` atomic<float>（預設 `-20.0f`）
- **新指令 `set_up_stop_kg <kg>`**：前端用 input + set 按鈕送，立刻生效（不需重編）
- **`status` 回傳擴充**：加 `up_stop_kg=<值>` + `weight_valid=<0|1>`，前端自動解析更新顯示
- **DY500 讀取失敗防呆（第 3 層）**：
  - 連續讀失敗超過 `READ_FAIL_STOP_MS (500ms)` → `g_weight_valid = false`
  - 若當時 motion_active → `all_off` + EVT `weight_read_fail duration_ms=N`
  - `cmd_up` / `cmd_down` pre-flight：若 `!g_weight_valid` 直接回 `ERR weight_read_fail`
- **前端**：
  - 新增「收繩停止門檻」input row + set 按鈕 + 目前值顯示
  - `onEasyCraneLine` 解析 `up_stop_kg=` 更新顯示、新增 `EVT weight_read_fail` 也觸發 `releaseAllEasyHolds()`
  - hint 列點擴充到 4 條
- runbook.md C2b 節更新成 4 層防呆 + 新指令

### 修改檔案
- `Crane_easy_PI/main.cpp` — atomic + weight_loop 雙重檢查 + 3 個 cmd_* 更新 + 新 cmd_set_up_stop_kg + dispatch 新增
- `web_backend/public/index.html` — 新 row + hint 4 條
- `web_backend/public/app.js` — 解析 up_stop_kg、EVT weight_read_fail trigger、set 按鈕 handler
- `.claude/runbook.md` — C2b 節更新

---

## 2026-04-20f — 新增獨立簡易吊車子專案 Crane_easy_PI

> **規範權威：** 本節（之前沒有；之後若需要移到 motion_flow.md 再搬）

### 背景
Sadie 丟來一份 `crane_control_PI_easy_ver/` 資料夾 — 一隻獨立的簡易吊車 C++ 程式，跟主 crane 是完全不同的硬體（relay slave 16 / DY500 slave 3，兩個不同 gateway 21/22），走互動式 cin/cout。要求：
1. 放進主 workspace 當子專案、整理程式碼
2. Web 加 2 顆按鈕（拉繩 / 釋放繩）
3. 網路防呆（防止繩縮到最上面卡壞）

### 硬體
- 獨立 RPi @ **192.168.5.26**（跟主系統不同網段）
- TCP server @ **:5003**
- Modbus gateways: `192.168.1.21`（relay）+ `192.168.1.22`（DY500 重量感測）— Pi 應有第二片 NIC 在 .1.x 網段
- 繼電器 CH8 = 拉繩（UP）、CH7 = 釋放繩（DOWN）
- DY500 @ slave 3 讀張力（kg，帶負號表示受力）

### 已完成
- **新專案 `Crane_easy_PI/`**（取代原 `crane_control_PI_easy_ver/`）
  - `main.cpp` — 重寫成 TCP server + 後端 watchdog + 重量 loop，對齊主 crane 架構
  - `Crane_easy_PI.vcxproj` — MSBuild 檔（GUID `{909DCE76-...}`）
  - `washrobot_new_PI.sln` — 加入新專案節點 + 8 種配置映射
  - **刪除** 原 `crane_control_PI_easy_ver/` 整個資料夾（user_lib/ 是重複檔）
- **指令集**（精簡）：`up <on|off>` / `down <on|off>` / `stop` / `status` / `ping`
- **三層防呆**：
  1. Server watchdog — `motion_active` 且 > `WATCHDOG_TIMEOUT_MS (2000ms)` 沒 inbound → `all_off` + EVT
  2. 重量門檻 — UP 過程 `weight < WEIGHT_UP_STOP_KG (-20kg placeholder)` 持續 `WEIGHT_SUSTAIN_MS (300ms)` → `all_off` + EVT
  3. 前端 press-and-hold + 500ms `ping` heartbeat（網路掉線 2 秒內必觸發 (1)）
- **Web Backend 三端橋接**：
  - `server.js` — 新增 `easy_crane` bridge（env `EASY_CRANE_IP` 預設 `.5.26`）+ 3-state status broadcast
  - `index.html` — 頂部第 3 顆 dot、新 panel「easy crane」（重量顯示 + 拉繩/釋放繩/stop/refresh）
  - `app.js` — 重構 `bindHold()` 通用 helper（emergency retract 也改走 helper）、`easy_crane` hold + heartbeat、自動每 2 秒 poll `status` 顯示即時重量、EVT `watchdog_timeout` / `weight_limit` 收到時本地解除按鈕狀態
  - `style.css` — `panel-easy_crane` + `btn-hold` 樣式
- `runbook.md` 更新：啟動順序加第 3 步、GUI 按鈕表加 easy crane、C2b 指令表、緊急處置表新增 2 條

### 參數待實測
- 🟡 `WEIGHT_UP_STOP_KG = -20.0f` 先放 placeholder，上機測實際卡住時的張力值再調
- 🟡 Gateway IP `192.168.1.21` / `.22` 跟主系統相同，確認 Pi 第二片 NIC 走這個網段

### 規範邊界備註
本子專案屬 Sadie 範圍（應用層 + 前端 + 部署），不跨界。原 `user_lib/` 驅動完全沒改。

---

## 2026-04-20c — Web 前端五件套（失聯模式 / 緊急收繩 / Phase 6 召回 / 鋼索歸零 / 平衡校正 modal）

> **規範權威：** `.claude/motion_flow.md` §8 系統通訊架構（失聯模式 UI + 緊急收繩按鈕 + 指令協定）

### 已完成
- **失聯模式 UI** — `applyMode()` 依 `status` broadcast 的 washrobot/crane 兩個 bool 切 4 態：
  - 兩邊活 → banner 隱藏、區塊全可用
  - washrobot 失聯 → 橘 banner「救援模式」，所有 `.panel-washrobot` 灰化鎖住；crane + 緊急收繩保留可用
  - crane 失聯 → 紅 banner「禁止下移」，所有 `.panel-crane` 灰化鎖住；washrobot 可用
  - 全斷 → 紅 banner「全線失聯」
  - **切入失聯自動送 stop**：兩邊活 → 單邊掉時，自動送 `stop` 給 crane 或 `emergency_stop` 給 washrobot
- **緊急收繩按鈕** — 獨立紅框 panel，`mousedown/touchstart` → `retract_left on` + `retract_right on`；`mouseup/touchend/mouseleave` → `retract_*_off` + 補送一次 `stop` 保險；按下中顯示秒數計時
- **Phase 6 召回流程** — 「召回回地面」按鈕：先送 `crane home_status` → 解析 `remaining=<N>` → 彈 modal 確認 → 送 `washrobot return_home <N>`；timeout 3s、remaining ≤ 0 拒絕執行
- **鋼索歸零按鈕** — crane panel 新增「鋼索歸零（地面起點）」`zero_meters ground` + 「鋼索歸零（洗窗起點）」`zero_meters top`
- **平衡校正 modal** — 監聽 `EVT balance_ask roll=<x> pitch=<y>` → 彈 modal 顯示兩角度 → Yes/No 對應 `confirm_balance yes|no`
- **Bug 修正** — 原 HTML `STOP (robot)` 按鈕送 `stop`（washrobot 不支援），改為 `emergency_stop`

### 修改檔案
- `web_backend/public/app.js` — 整檔重寫（112 → 225 行）：新增 mode 切換、modal、緊急收繩 press-and-hold、home_status pending resolver、EVT balance_ask 解析
- `web_backend/public/index.html` — 加 `#banner` + 各 panel 加 `panel-washrobot` / `panel-crane` class 供灰化、加 `panel-emergency` + 兩個 modal（balance / return_home）+ 鋼索歸零按鈕 + 召回按鈕 + `reset` 按鈕
- `web_backend/public/style.css` — append banner / panel-disabled / primary / emergency / modal 樣式（~90 行）
- **未動 `server.js`** — 既有 WebSocket ↔ TCP bridge 已足夠；broadcast `status` / `{src, line}` 機制不需變更

### 待完成 / 待測
- 🟡 上機驗證：失聯模式 banner 切換、緊急收繩按住節拍、home_status 回傳解析、balance_ask 彈窗
- 🟡 `.101` 部署（裝 Node.js、搬 web_backend、systemd）
- 🟡 停用 `.100` 舊 web_backend
- 🔴 攝影機 grid（方案 A — ffmpeg + HLS）尚未做

### 規範邊界備註
`web_backend/` 屬 Sadie 的「前端」範圍，本次不跨界。

---

## 2026-04-20b — Phase 2 補「收輪」步驟 + 程式碼同步 [跨界: motion_flow]

> **規範權威：** `.claude/motion_flow.md` §4 Phase 2 + §2 RS485_1 表

### 背景
原 motion_flow §4 Phase 2 只寫推桿伸出到 10 cm，沒有收輪步驟；`WASH_ROBOT.cpp cmd_init()` 也完全沒碰 DM2J slave 2/4。§7 Open Q5 就是在問輪子如何驅動。

Sadie 釐清機械：**輪子裝於靠牆面**，Phase 1 展開供吊機把機器人沿玻璃拉上樓。Phase 2 要先收輪，ZDT 推桿才能把吸盤送到玻璃面。

**決策：** 收輪步驟放在 Phase 2（不是 Phase 1 末尾），實作上放在 `cmd_init()` 推桿伸出之前。理由：
- 單一 entry point — 操作員按 `init` 一鍵搞定
- 防呆 — 就算 Phase 1 忘了收輪也會自動處理

Phase 6 召回確認**不需放輪**（輪子在牆面側，落地無緩衝作用）。

### 已完成
- `motion_flow.md` §2 RS485_1 表 slave 2/4 欄位：「Phase 1 only」→「Phase 1 放輪爬牆；Phase 2 收輪」
- `motion_flow.md` §4 Phase 2 新增 step 8「DM2J slave 2, 4 → 0」，原 step 8~11 順延為 9~12
- `user_lib/WASH_ROBOT.cpp cmd_init()` 在繼電器 OFF 後、`pusher_move_many_` 前插入左右輪 retract（PR_move_cm mode=1 absolute，目標 0 cm）

### 規範邊界備註
motion_flow.md 屬 Jim 範圍，標 `[跨界: motion_flow]`。

---

## 2026-04-20 — Phase 3 補 VACUUM_SETTLE_MS 規格 [跨界: motion_flow]

> **規範權威：** `.claude/motion_flow.md` §4 Phase 3 + §6 可調參數表

### 背景
`WASH_ROBOT.cpp cmd_attach()` 在 CH4 ON 後有 `sleep_ms_(VACUUM_SETTLE_MS)`（2000 ms）再讀 JC-100，讓電磁閥通氣 + 吸盤壓力穩定。原規格 Phase 3 直接從「CH4 ON」跳到「檢查 JC-100」，沒寫這段等待。補規格讓 spec/code 對齊。

### 已完成
- `motion_flow.md` §4 Phase 3 新增 step 5「等 VACUUM_SETTLE_MS 讓壓力穩定」，原 step 5「檢查 JC-100」順延為 step 6
- `motion_flow.md` §6 可調參數表新增 `VACUUM_SETTLE_MS = 2000 ms`

### 規範邊界備註
程式碼未動（`cmd_attach()` 既有行為即為補進規格的內容）。本次純規格同步，標 `[跨界: motion_flow]` 送 Jim review。

---

## 2026-04-17 — 釐清：washrobot↔crane TCP 方向無需更動

> **規範權威：** `.claude/motion_flow.md` §8 網路拓撲 + 元件職責

### 釐清事項
Sadie 一度以為目前是「washrobot 當 server、crane 當 client」，想翻轉為「crane server / washrobot client」以利 washrobot 失聯時 Web Backend 仍能救援。實際翻代碼後確認**現狀已經是使用者想要的架構**：

- washrobot `:5001` TCP server — 給 Web Backend 連（保留）
- crane `:5002` TCP server — 同時接 Web Backend + washrobot 兩個 client
- washrobot 作為 **TCP client** 連 crane `:5002` 下 `pay_out` / `ping`（見 `user_lib/WASH_ROBOT.cpp:198` `crane_connect_if_needed_()`）
- Web Backend 在 .101，連 `.100:5001` + 本機 `:5002`
- washrobot 掛掉時：Web Backend → crane :5002 的救援路徑完全不經 washrobot ✅

**結論：不改任何東西**。motion_flow §8 已寫明拓撲，此條純粹備忘，避免未來又誤會。

### 待完成（不變）
（略，接續下方 2026-04-15 條目）

---

## 2026-04-15 — Crane_control_PI 重寫（Step 1 + Step 2）

### 已完成

1. **Step 1：硬體 + 指令擴充**（`Crane_control_PI/main.cpp`）
   - 新增 SD76 #3（slave 4，中間管線計米）+ CLV900（slave 7，中間絞盤變頻器）
   - `motion_rope()` 整合雙繩 + 中間管線同步：CLV900 forward/reverse @ `MIDDLE_WINCH_HZ=20` (placeholder)，中間完成條件 `|Δ| >= cm × MIDDLE_WINCH_RATIO_K`
   - 新指令：`ping` / `zero_meters <ground|top>` / `home_status` / `roll_correct <delta_cm>` / `middle_set <rpm> <pay|retract|stop>`
   - `zero_meters top` 存 `home_ground_cm`（atomic）供 Phase 6 召回算放繩量
   - `roll_correct`：正值=左放右收，中間絞盤不動
   - `Crane_control_PI.vcxproj` 加 CLV900 + 修 include 路徑 `C:\Users\Administrator\...` → `..\user_lib`

2. **Step 2：Watchdog 執行緒 + EVT**（`Crane_control_PI/main.cpp`）
   - Atomics：`last_ping_ms` / `motion_active` / `watchdog_fired` / `watchdog_stop`
   - `touch_heartbeat()` 在 `on_receive` 頂呼叫；任何 inbound 資料都算心跳
   - `watchdog_loop()`：每 250ms 檢查，超 2000ms 且有 client → 動作中 abort + allMotionOff + EVT；閒置中僅 EVT
   - `MotionScope` RAII guard 在 `motion_rope` / `cmd_roll_correct` 頭尾管理 flag
   - EVT：`watchdog_timeout state=aborted|idle` / `watchdog_recovered`

### 待完成（更新）

- 🟡 `MIDDLE_WINCH_HZ` 實機調校（目前 20 Hz placeholder）
- 🟡 `middle_set` rpm→Hz 換算需驗證馬達極數（目前假設 4 極 50Hz→1500rpm）
- 🟡 CLV900 正反轉方向 wiring-dependent，實機若反向需翻轉
- 🔴 **Step 3：DSZL-107 張力 monitor** — 等 Modbus 暫存器表才能實作 `user_lib/DSZL_107.{h,cpp}`，目前整個跳過
- 🟡 IMU serial port 上機確認（`/dev/ttyUSB0` 暫定）
- 🟡 水箱溢流處理（Open Q10）
- 🟡 攝影機型號 + RTSP URL
- 🔴 Phase 6 `recall` 指令（washrobot 端：吸盤脫附 + 呼叫 `pay_out home_ground_cm - current_down`）
- 🔴 Web GUI 補強
- 🔴 實機測試 + 參數回填

---

## 2026-04-14b — washrobot main.cpp 實作（Phase 4-A / Crane Watchdog / IMU）

### 已完成

1. **Phase 4-A margin 修正**（`washrobot_new_PI/main.cpp`）
   - 新增 `STEP_MARGIN_CM = 15`
   - 修正 `feet_displace` 順序：crane 先放繩（STEP_CM + MARGIN）→ DM2J 動 → crane 收繩（MARGIN）
   - 新增 `dm2j_wait_done()` helper，取代舊的 `sleep_ms(4000)`，改為 poll CMD_DONE + PATH_DONE
   - DM2J 左右腳同步：`PR_trigger_sync`（廣播）+ 分別 poll 等待

2. **Crane watchdog**（`washrobot_new_PI/main.cpp`）
   - 新增常數 `HEARTBEAT_INTERVAL_MS=500`、`WATCHDOG_TIMEOUT_MS=2000`
   - `crane_cmd` 加 `timeout_sec` 參數（預設 30s，ping 用 2s），移除舊 180s hardcode
   - 任何 crane 指令成功時更新 `crane_last_ok_ms`
   - `crane_watchdog_loop()`：每 500ms ping，超時 2000ms → 動作中 abort + EVT，閒置中只推 EVT
   - `motion_active` atomic flag，`do_step_down` / `do_run` 頭尾設定

3. **IMU 整合**（`washrobot_new_PI/main.cpp`）
   - `Serial_port` + `WT901BC_TTL` init（`/dev/ttyUSB0`，待上機確認）
   - `imu_take_baseline()`：Phase 2 init 時取 3 秒平均存 `roll0/pitch0`
   - `imu_monitor_loop()`：100ms 取樣，1s 滑動平均，持續 500ms 超標觸發
     - `> 45°` → abort + crane stop + EVT `imu_stop`
     - `> 15°` → `imu_ask_pending = true` + EVT `balance_ask`
   - `do_phase5_roll_correct()`：送 `roll_correct <cm>` 給 crane，最多重試 5 次，收斂 `|Δroll| < 1°`
   - `confirm_balance <yes|no>` 指令：yes → Phase 5 → 清 pending；no → 直接清
   - `do_run()` 每步後若 `imu_ask_pending` 則 `pause_flag=true` 等回覆

### 待完成（更新）

- 🟡 DSZL-107 Modbus 暫存器表
- 🟡 水箱溢流處理（Open Q10）
- 🟡 攝影機型號 + RTSP URL
- 🟡 IMU serial port 確認（目前暫定 `/dev/ttyUSB0`）
- ✅ CH5/CH6/CH7 繼電器定義 + `arm_sweep` 加入清洗動作
- 🔴 Phase 6 `recall` 指令（吸盤脫附 + crane 放繩回地面）
- 🔴 重寫 `Crane_control_PI/main.cpp`
- 🔴 寫 `user_lib/DSZL_107.{h,cpp}`
- 🔴 Web GUI 補強
- 🔴 實機測試 + 參數回填

---

## 2026-04-16 — 多人協作機制強化

> **規範權威：** `CLAUDE.md`「多人協作紀律」節（含開 session 須知 / 角色定義 / 介面契約）

### 已完成
- **協作者角色定義表** — CLAUDE.md 新增 5 角色表格（架構/washrobot 實機/crane 實機/前端/測試工具），Jim 已填，其他待協作者自行填入後 commit
- **介面契約（彈性版）** — `user_lib/*.h` public API 為契約，原則上架構方改；協作者遇阻塞 bug 可先在自己 branch 熱修，但 PR 必須標 `[跨界: user_lib]` 等架構方 review
- **協作信箱 `.claude/mailbox.md`** — 跨分工邊界的需求/通知/問題；按角色分區 + 阻塞程度標記（🔴🟡🟢）；已處理條目保留作為決策紀錄
- **開 session 須知** — CLAUDE.md 新增流程：(1) 告知角色 (2) 掃 mailbox (3) 讀 work_log
- **全域決策畢業** — `agent_ai/.claude/work_log.md` 移除「架構決策」段（內容已在 CLAUDE.md 規範裡，work_log 保持純進度追蹤）

---

## 2026-04-15 — Commit `6dbbe22` pushed（log 統一 + Web Backend 搬家 + 協作紀律）

### Commit 內容
- `refactor+docs: unify user_lib logging + web backend relocation spec`
- 32 files, +993/-1097（log 統一實際減少行數）
- `d7e4132..6dbbe22  main -> main` — 已 push 到 origin
- 涵蓋本日三項工作（下方分條列出）

---

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
