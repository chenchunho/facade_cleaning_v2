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
| ✅ | ~~重連的非阻塞 connect 少了 `getsockopt(SO_ERROR)` → 連到沒人聽的埠也判定成功~~ | `transport/TCP_client.cpp` | **已修（本分支 `-drv4`）** —— 雙向斷言實機驗證：吊機關→假成功 0 次、吊機開→正常連上。🔴 **`refactor/app-layer` 上仍未修**（那條分支要保持功能等價） | work_log 2026-08-28（實機驗證） |
| 🟡 | `init()` 印 `VFD left/right (MH300)` 是**寫死字串**，在 `#if CRANE_VFD_IS_SE3` 之外 → 旗標是 1（實際跑 SE3）卻印 MH300，會把人導去查錯的 driver | `Crane_control_PI/main.cpp:4215,4234` | **未修** ✔ | work_log 2026-08-28（實機驗證） |
| ✅ | ~~上滑台 cm↔pulse 換算錯 7.7 倍（皮帶軸 7.731 cm/圈，程式假設 1）→ 每次掃動下 131cm 指令、滑台只有 50cm，一路撞到底~~ | `user_lib/DM2J_RS570.*`、`app/WASH_ROBOT.*` | **已修（本分支 `-drv5`）**：換算層修正 + 行程守衛，實機量測指令 17→實際 17cm。🔴 **`refactor/app-layer` 上仍未修** | work_log 2026-08-28（實機量測） |
| 🟡 | `DM2J_ARM_STEP_SWEEP_EST_MS`(4500) 與 `ARM_SWEEP_EST_MS`(3900) 依「1cm/rev」錯誤前提算出，正確約 650ms／132ms。**刻意暫不調小**（估太長安全、估太短危險），等新換算跑過完整步伐流程再重調 | `app/WASH_ROBOT.h` | **待重調** ✔ | work_log 2026-08-28 |
| 🔴 | **上滑台的「零點」是 `init` 當下的位置，不是機械原點** —— ⚠️ 我 2026-08-28 一度寫成「從未做過設零點」，**那是錯的**：`cmd_init()` 有呼叫 `home_set_current_pos_zero()`，但那是 `0x0021`「把當前位置設為零點」，**不是 homing**（`0x0020` 才是找原點感測器）。後果：`ARM_RAIL_TRAVEL_MAX_CM` 守的是**指令座標**，init 時滑台若停在外面，座標原點跟著偏移，**守衛擋不住實際超程**。真解是啟用 homing（`home_start()` 已實作但無人呼叫） | `app/WASH_ROBOT.cpp:6912`、`user_lib/DM2J_RS570.cpp` | **未修** ✔ | work_log 2026-08-28（更正） |
| ✅ | ~~推桿 cm↔pulse 用 `20000/7 = 2857`，實測應為 **3000**（5% 系統誤差）~~ | `app/WASH_ROBOT.{h,cpp}` | **已修（`[2026-08-28n]`）**：新增 `CUP_PULSE_PER_CM = 3000.0`，兩處都改吃它。實機 47994 脈衝 = 16cm + 四條交叉驗證 | work_log 2026-08-28（實機量測） |
| 🟡 | `PUSHER_EXTEND_*` 常數的註解標的公分現在是對的（本來就用 3000），但**「12.0 cm」等標示仍未逐一複查**；另 `zdt_pusher extend` 實際走的是 `disable_seal` 尋封序列（可達 47994 脈衝／16cm），**不是預設的 36000** —— 文件與 GUI 說明都沒講 | `app/WASH_ROBOT.h`、runbook | **未修** ✔ | work_log 2026-08-28 |
| ✅ | ~~左右歸屬與實體不符（RF={5,6}/LF={7,8}），**交替步伐因此不可用**~~ | `app/WASH_ROBOT.{h,cpp}` | **已修（`[2026-08-28p]`）**：右={5上,7下}／左={6上,8下}，31 處使用點自動跟著正確。🔴 **尚未實機驗證**，第一次跑交替步伐要有人在旁邊 | work_log 2026-08-28 per user |
| 🟡 | `group_seal_ok_` 的「4 顆有 2 顆吸住就算 OK」是為了繞過「分側判準算不準」而採用的（2026-08-28）。**歸屬修好後那個前提消失** → 是否改回「每側各 ≥1」需使用者決定 | `app/WASH_ROBOT.h` | **待決定** ✔ | work_log 2026-08-28 |
| 🔴 | `readRegister()` 不驗 reply CRC、不驗 byteCount → 壞掉的 Modbus reply 被當有效值往上傳（bench 已造成實體損害，詳見下方） | `user_lib/SD76_length_meters.cpp` | **未修** ✔ | mailbox 2026-05-14 |
| 🔴 | 🔮 **eth 串接之後要回頭改 `WASH_ROBOT.h` 的 `CRANE_IP`**：目前是 bench 用的 WiFi `192.168.5.17`（註解顯示已改過三次）。串上 eth 之後**它仍然會走 WiFi**——有線路徑就在旁邊卻沒被用到，而且完全不會有訊息告訴你。機器吊在半空中時控制流量跑在 WiFi 上，是實質風險 | `app/WASH_ROBOT.h:414` | **待處理（等 eth 串接）** | work_log 2026-08-28 per user |
| 🔴 | **所有上滑台 RPM 常數都是在錯誤的線速度認知下挑的**。以「1cm/rev」算時以為 250rpm=4.17cm/s、1000rpm=16.7cm/s；用實測導程 7.731 換算，實際是 **32.2 / 128.8 cm/s**。**2026-08-28 實機已發生失步**（使用者回報，手動調回）。→ `ARM_SWEEP_RPM=1000`（129cm/s）幾乎確定過快，`DM2J_ARM_STEP_SWEEP_RPM=250` 也要重新評估；ACC/DEC=100 同樣是在錯誤前提下挑的 | `app/WASH_ROBOT.h` | **未修** ✔ | work_log 2026-08-28（實機失步） |
| 🟡 | **USR 網關 `_pt`（串口打包時間）設為 0＝自動** → 115200 下字元間隔僅約 0.3ms，是「回覆被切成兩個 TCP 段」的結構性根源（`[2026-08-28b]` 的分片問題）。**改成 5ms 可從根本解決**，代價每筆交易 ≤5ms（`status` 讀 4 顆 → +20ms）。⚠️ 影響 bus 上所有裝置，且目前量到的失敗是 `no reply` 不是 `too short` —— **先記錄、之後再改**（per user 2026-08-28）。後台 `http://192.168.1.22/system.shtml`，admin/admin | 網關 `.20` / `.22` | **待改** ✔ | work_log 2026-08-28 |
| 🔴 | `web_backend/server.js` 的 **`CRANE_IP` 預設值寫錯**：`192.168.1.101`，吊機實際是 `192.168.1.10`（`.101` 沒有任何機器回應）。⚠️ 同一行的 `WROBOT_IP = 192.168.1.100` **是對的、不要動**（見上一列：eth 尚未串接而已）。照預設啟動 GUI 連不到吊機，**畫面不會說是 IP 錯** | `web_backend/server.js:25` | **未修** ✔（08-28 啟動時實測，以環境變數繞過） | work_log 2026-08-28 |
| 🟡 | 兩台 Pi 都沒有 `tmux`／`screen` → runbook §A「一鍵啟動」`scripts/crane.sh`／`wr.sh` **在這兩台跑不起來**。替代方案 `~/bringup/run_bg.sh`（FIFO 背景啟動）已放兩台 | `scripts/*.sh`、`.claude/runbook.md` §A | **未修** ✔ | work_log 2026-08-28 |
| 🔴 | 緊急收繩按鈕實際上**沒有張力保護**，跟 `motion_flow.md` §8 的安全性描述相反 | `Crane_control_PI/main.cpp` `cmd_manual()` | **未修** ✔ | ONBOARDING §6 |
| ✅ | ~~`cmd_side_measured()` 進場沒重置 `abort_flag` → 被 stop 過一次後所有 v2 step 指令永久回 `ERR aborted`~~ | `Crane_control_PI/main.cpp` | **已修（`[2026-08-28s]`）**：補上 `abort_flag = false;`，位置與姊妹函式一致（`try_lock` 之後，避免被拒絕的重疊指令清掉他人的 abort）。⚠️ 尚未編譯 | ONBOARDING §1 ＋ work_log 2026-07-15 |
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
| 🟢 | 規範文件架構圖與程式碼脫節 —— **2026-08-28 已解**：`CLAUDE.md` `## Architecture` 全節由原始碼重建（v2 as-built）。`motion_flow.md` §2 **刻意維持 v1 不動**（它是已凍結的 v1 世代文件，見本檔文件世代表），不是遺漏 | `CLAUDE.md` `## Architecture` | **已修** ✔ | ONBOARDING §5 |
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
| 🟡 | **`SERIAL_PORT_H` guard 衝突：兩個不同的序列埠實作共用同一個 guard** | `user_lib/SerialPort.h`（322 行，cleaning_arm/damiao 用）與 `transport/Serial_port.h`（本專案用，WASH_ROBOT.h / WT901BC_TTL.h / Linux_test）。目前不爆只因使用者不重疊；**一旦同一編譯單元同時碰到兩者，第二個被 guard 靜默吃掉**，症狀是「class 莫名找不到」、錯誤訊息不指向真因。修正方向：guard 改唯一名稱或 `#pragma once`，動前先確認無別處拿此 guard 名做條件編譯。兩檔開頭皆已標註 | 未修 | 分層重構 2026-08-27 |
| 🟡 | **Pi 上的 `web_ver2` 落後 repo 一個 commit** ⚠️ 原記「分岔 589 行 / 不是增量是另一個程式」是**誤判**，2026-08-28 更正 | Pi `~/projects/web_ver2/`（在**吊機** `raspberry-cran`，不是本體）四個檔全是 repo 內容的複本：`server.js`／`style.css` 與 commit `faf1d3f` **逐位元相同**、`app.js` 與 `a894ae1` 逐位元相同、`index.html` 與 HEAD 的差異**全部**是攝影機面板那一段。**沒有任何人手改過的內容，repo 仍是權威**，只是落後移除攝影機的 commit `e3c8820` | 待部署（🔴 **main 分支的人正在改這兩台，部署前先確認**） | 更正 2026-08-28（原：實機盤查 2026-08-27） |
| 🔴 | **張力刻度仍是 placeholder，kg 讀值無意義** | 實機 `status` 讀到 `dsz_left_scale=-0.01 dsz_right_scale=-0.01`，即待辦既有的 DSZL placeholder。當下讀值 `tension_left=27.35 / right=14.98` 是用佔位刻度算出來的。**這是 `crane_balance_hold_plan` 重啟前提「張力可信」仍未達成的實證** | 未修 | 實機讀取 2026-08-27 |
| 🔴 | **左右張力差 12.4 kg，且左側已越過收繩停止門檻** | `retract_tension_stop_kg=25`，左側讀值 27.35 已高於它 → 若刻度正確，收繩指令會立刻觸發張力停止。⚠️ 但因上一列（刻度是佔位值），也可能是假象 —— **兩種可能都不可接受**，要先解決刻度才能判斷 | 未修 | 實機讀取 2026-08-27 |
| 🔴 | **VFD 故障碼顯示是壞的：一邊報假警、一邊讀不到** | `vfd_fault left` → `f1~f4 = 160/OPT`（四格全故障）；`vfd_fault right` → `ERR read_fail`。根因是 `mh300_migration_plan` Phase 3-3 未完成：`format_vfd_fault_codes`（`Crane_control_PI/main.cpp:1529`）仍讀 SE3 的 H1007/H1008、`vfd_fault_name()` 仍是 SE3 代號表。**故障診斷目前不可用** | 未修 | 實機讀取 2026-08-27 |

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

## 2026-08-28（傍晚）— 🔴🔴 上滑台每個 cm 指令走 7.7 倍：三個沉默疊在一起的隱形缺陷

### 起點是一句「移動很多、速度很快」
我一開始往速度查（`ARM_SWEEP_RPM=1000` 確實是步伐用值的 4 倍），
但使用者換到 250 rpm 後說「**一樣超過**」——**與速度無關**，那就只剩換算。

實機拿尺量三點（250 rpm）：指令 1→7cm、2→15cm、5→38cm。
最小平方 **實際 = 7.731 × 指令 − 0.615**（殘差全在 ±0.15cm 內）。
截距 −0.6cm ＝ 皮帶自硬限位起步的鬆弛量。
**預測性驗證**：反推物理 20cm → 指令 2.666 → 量到 20cm ✅（使用者確認是皮帶軸）

🔴 **滑台總行程 50cm，而 `ARM_SWEEP_CM=17` 實際下 131cm 指令 —— 每次掃動都一路撞到底。**

### 三個獨立的沉默，疊起來讓它隱形
1. **驅動器只數脈衝** → 回報永遠是漂亮的 `15.0000`／`17.0000`，跟滑台實際在哪無關
2. **`do_arm_sweep_()` 成功路徑一個字都不印**（同日稍早才發現）
3. **上滑台在 08-28 之前掛在錯的 gateway、三天完全沒動** → 根本沒機會現形

### ⚠️ 我在這輪犯的兩個錯（都已更正）
- **把「回零正確」當成「沒失步」的證據**。物理 0 點是**機械硬限位**，不管中間失步多少
  都會頂回同一位置。📌 **零點是硬限位的系統，回零位置對失步沒有鑑別力。**
- **先入為主猜「8.0 cm/rev（40 齒 GT2）」**，因為那是個漂亮數字。實測 7.731。
  📌 **漂亮的數字最容易讓人停止量測。**

### 修法（三件）
1. **換算層**：driver 加 `set_lead_cm_per_rev()`（預設 1.0 維持舊行為），五個 cm↔pulse
   換算點收斂到兩個 helper；機構參數由應用層在 `init()` 注入，不寫死進通用驅動層
2. **行程守衛**：`set_travel_limit_cm(0, 48)`，三個 cm 移動函式全部檢查，
   超範圍**明確拒絕並記錄**——修正前驅動器／應用層／log 三邊都沒有任何抗議
3. **常數數值刻意不動**：它們本來就宣稱是公分，修好換算後才第一次名副其實。
   🔴 **不逐一乘係數**——該常數散在三處以上，逐一改必定漏（本專案已為此出過三次事）

### 驗證（雙向 + 實機量測）
| 測試 | 結果 |
|---|---|
| `calib 60`（超範圍） | ✅ 拒絕、滑台不動 |
| `calib 17`（範圍內） | ✅ 執行，回報 `16.9997` |
| **拿尺量實際位移** | ✅ **17 cm** |
| `init()` | ✅ `lead=7.731 cm/rev travel<=48 cm` |

📌 `16.9997` 這個**不整齊**的數字反而是可信度來源——證明換算真的經過新的除法，
不是又一個假的漂亮整數。

### 工具
`probe_dm2j.cpp`（不經主程式、只動 DM2J）：`read`／`goto <cm> <rpm>`（原始無標定）／
`calib <cm> <rpm>`（套用標定與守衛）。留在 `~/bringup/` 與 `~/merge_check_20260828/drv/`。
🔴 **尚未進版控**，日後再標定機構時很有用。
## 2026-08-28（收尾·續）— 交接後的無硬體工作：左右歸屬修正

### 機器已交出，這段全部不需要硬體
16:18 起有六個 SSH session 來自 `192.168.5.25`，16:30 對方啟動了
`facade_cleaning_v2.out` —— **交接確認生效，我已停止對該 Pi 的一切操作**。
（16:30 那次編譯只用 CPU、只寫 `~/bringup/`，目的是避免留下「原始碼比二進位新」的陷阱。）

### 🔴 修好了本專案最大的功能封鎖
`WASH_ROBOT.h` 原本明寫「**在修正之前：不要使用交替步伐**」，因為左右歸屬與實體不符。
使用者確認實體排列（由上往下看）：**右 = {5 上, 7 下}、左 = {6 上, 8 下}**。

原本的 `RF={5,6}` 把「兩顆在上面的」當成右側 → 交替步伐的「錨定側是否還吸著」
一直在看「一邊各一顆」，**等於沒有保護**。

📌 **程式結構原本就對，錯的只有四個數字** —— `preset_extend_pulse_for_slave_` 吃的是
RF1/LF1（上）與 RF2/LF2（下），所以改完之後 **31 處使用點全部自動正確，一行都不用改**。
與同日 `zdt_pusher` 那件事剛好互為對照：**那邊因為同一個範圍寫在兩處而分岔，
這邊因為分側只走這四個常數而一改全中。單一真實來源的價值，正反面各看到一次。**

🔴 **尚未實機驗證**（依據是口頭確認的排列）。第一次跑 `do_step_down_`/`do_step_up_`
要有人在旁邊，先確認「放開哪一側時另一側兩顆確實還吸著」與觀察一致。

---

## 2026-08-28（收尾）— 本日總結：合併 main + 七個隱形缺陷 + 兩台 Pi 已讓出

### 機器狀態：兩台 Pi 已完全讓出給 main 分支的人
本體 `5001`/`9527`、吊機 `5002`/`8080` **四個埠全數釋放、無殘留行程**（含 FIFO 的
`sleep` 持有者與 fifo 檔）。🔴 **`~/projects/` 全程未動**（本體 Aug 25 / 吊機 Jul 8），
所有工作都在 `~/bringup/` 與 `~/merge_check_20260828/`。

### 本日抓到並修好的七個缺陷（全部是「壞了但沒有人被告知」那一類）
| # | 缺陷 | 怎麼被發現的 |
|---|---|---|
| 1 | `sendAndReceiveQuiet` 帶 `send(...,0)` 進來，繞過同期的 `MSG_NOSIGNAL` | 合併後逐一檢查「對方新增的碼有沒有繞過我方新防線」 |
| 2 | `PR_move_cm_nowait` 寫死 `return false` → main 的「掃動全滅偵測」接到常數 | 讀 main 修補的實作，追它到底接到什麼 |
| 3 | 重連把失敗的 connect 判成成功（缺 `getsockopt(SO_ERROR)`） | 吊機沒開卻印 20 次 `reconnect success` |
| 4 | 🔴 **上滑台每個 cm 指令走 7.7 倍**（皮帶軸 7.731 cm/圈，程式假設 1） | 使用者一句「移動很多、速度很快」 |
| 5 | PWM 無重試、無回讀，且錯誤訊息張冠李戴 | 停止螺旋槳那一發失敗，而槳繼續轉 |
| 6 | `zdt_pusher`/`disable`/`enable` 自 08-27 起不可能成功（範圍無交集） | 讀分派器與應用層的檢查，發現兩邊寫死不同範圍 |
| 7 | 🔴 **推桿 pulse/cm 差 5%**——08-27 的「更正」本身是錯的 | 拿尺量 47994 脈衝 = 16cm |

### 📌 本日累積的四條通則（已同步到上層踩坑索引）
1. **合併乾淨 ≠ 語意接得上**——對方新增的碼可能繞過我方新加的防線，git 不會提醒
2. **「有在檢查」≠「檢查得到」**——判斷式寫對了，被判斷的值卻是個常數
3. **「單位」也要驗**——拿尺量一次，勝過一百行「驗證通過」的 log
4. **「更正」也是一種主張**——08-27 把 3000 改成 2857 並宣告 3000 是錯的，實測相反

### ⚠️ 我在本日犯的三個錯（都已當場更正並留紀錄）
- 把「回零正確」當成沒失步的證據 —— 零點是**機械硬限位**，會把誤差吃掉，沒有鑑別力
- 先入為主猜導程是 8.0（40 齒 GT2 的漂亮數字）—— 實測 7.731
- 以為 `zdt_pusher extend` 只走預設 36000 —— 它實際跑 `disable_seal` 尋封序列到 47994（16cm），
  我據此給了使用者偏小的淨空需求

### 分支
兩條分支**已同步**（`fix/driver-crc` 含 `refactor/app-layer` 全部內容），
`main` 刻意保持與 `origin/main` 一致。🔴 **兩條都尚未推上遠端。**

### 🔴 下次接手最該先看的三件
1. **RPM／ACC/DEC 全部要重新評估**——250 rpm 實際是 32.2 cm/s（不是當初以為的 4.17），
   而使用者已實測到失步。上滑台原點需重新校正後才能繼續
2. **`.22` bus 實體層**——PWM 寫入間歇 `no reply`；設定、連線、第二主站都已排除，
   剩電氣（終端電阻／線長／接地）。網關設定已記進 `CLAUDE.md`
3. **eth 串接後要改 `WASH_ROBOT.h` 的 `CRANE_IP`**（現寫死 WiFi）——串上有線後
   它仍會走 WiFi，而且不會有任何訊息告訴你

---

## 2026-08-28（晚·續）— 螺旋槳實轉測試 + PWM 重試與回讀驗證

### 螺旋槳實轉（與使用者一步一步對）
50Hz 週期 20ms，duty 就是脈寬 —— **標準 RC 電調協定**：5%=1.0ms（停止/待命）、
6%=1.2ms、10%=2.0ms（全速）。🔴 **左右螺旋槳共用 CH1**（per user）。

| 步驟 | 指令 | 結果 |
|---|---|---|
| 待命 | `pwm set 1 50 65535 5` | ✅ 回讀 `ch1=5,50,65535,1`，不轉 |
| 低速 | `pwm set 1 50 65535 6` | ✅ 回讀 `ch1=6,50,65535,1`，**兩具都轉，使用者確認正常** |
| 停止 | `pwm set 1 50 65535 5` | 🔴 **失敗** |

### 🔴🔴 停止指令失敗 —— 這一下把風險完全暴露
送停止時回 `ERR`，回讀顯示**四個通道全部 `ERR`**（模組整個沒回應），
而**螺旋槳繼續以 6% 轉**。手動重試一次才停下來。

📌 **關鍵性質：寫入失敗不會讓輸出歸零，模組會保持前一個值繼續輸出。**
也就是**通訊斷掉時螺旋槳不會停，會維持轉速**。這與直覺相反，
對 v3「兩具 22 吋螺旋槳貼牆」的架構是實質風險。

### 已實作（`refactor/app-layer`）
1. **交易層重試**（`QX_DO24::sendAndReceive`，3 次、40ms backoff）——
   只重試傳輸層失敗（no reply／too short／CRC），`device rejected` 不重試（模組明確表態）。
   放在 driver 而非各呼叫端：左右螺旋槳共用 CH1，不能指望每個呼叫端都記得重試。
2. **回讀驗證**（`cmd_pwm_set`）——寫完три個暫存器後讀回比對，不符或讀不到都回 ERR + EVT。
   理由：寫入回 true 只代表「模組收下這一幀」，不代表暫存器真的是那個值。
3. **錯誤訊息分辨真因**（`QX_DO24::last_fail()`）——先前送**合法的 hz=50** 也會收到
   「頻率被鎖在 50Hz」，真因其實是沒回話。同型問題 `[2026-08-28b]` 在 `Linux_test`
   修過，當時漏了主程式這一處。

### ⚠️ 驗證結果要誠實說
| 機制 | 狀態 |
|---|---|
| 回讀驗證 | ✅ **已驗證**：15 次寫入全部回讀確認實際暫存器值 |
| 重試 | ⚠️ **已實作但未被觸發**（`recovered on attempt` 計數 0）→ **救援路徑仍未實測** |

🔴 **而且先前那 20% 失敗率這次沒有重現**：同樣指令、同樣間隔、同樣開 debug，
8/10 變成 15/15。所以失敗是**間歇且與環境相關**，不是穩定的 20%。
回頭看失敗時段（15:38），`JC100:8` 唯一一次 timeout 也落在同一時間 →
**同一條 `.22` bus 上多個裝置同時受影響**，比較像 bus 層級干擾而非 QX 模組本身。

### JC-100 同批量測
15 次 `status` × 4 顆 = **60 次真實感測器讀取，0 次 timeout**。
📌 但要注意：`status` 顯示的壓力值**看不出是新鮮值還是 timeout 後的快取**
（`comm error, return last pressure: N`）。所以「status 有回應」不能當成
「感測器讀到了」——本輪是靠數 driver log 的 timeout 次數才算得準。

### 待完成
- 🔴 **實體層檢查**：`.22` bus 間歇性無回應（115200 的 RS485 對終端電阻／線長／接地敏感）。
  這是根因，重試只是止血
- 🔴 **重試救援路徑尚未實測**——要等下次自然出現失敗，或想辦法誘發
- 🟡 `ch3` 存著 `duty=11%`（**超過安全上限 10%**）且 `ctrl=65535` 持續輸出、已啟用。
  使用者確認 ch3 沒接線 → 目前無害，但**日後有人接上 ch3 會立刻以超過全速的訊號輸出**
- 🟡 `ch4` 存著 `duty=50 / freq=1000`，同樣超出安全鎖（`ctrl=0` 所以無輸出）

---

## 2026-08-28（晚）— PWM 實測：模組接上了，但寫入有 20% 打不進去

### 已確認：模組確實接在 gateway 上（先前的未知解除）
`init()` 的 `presence not probed` 永遠證明不了這件事——它不發包。實際問一次就知道：
`pwm status` 有回應，且**寫入會生效**（`ch1` 的 control 由 `65535` 變成我們寫的 `0`）。

🔴 **架構事實（per user）**：**左右螺旋槳共用 CH1**、50Hz、duty 5~10%（5%=停、10%=全速）。
所以任何對 CH1 的寫入都同時影響兩具螺旋槳。

### 量到的數字
| 動作 | 次數 | 結果 |
|---|---|---|
| `pwm set 1 50 0 5`（合法、duty=停止、不轉） | 10 | **成功 8 ／ 失敗 2（20%）** |
| `pwm status`（讀取） | 10 | 整包 10/10 回 OK |

失敗模式由 driver log 定案：**`[ERR] [QX:9] no reply (timeout)`** ——
**不是分片沒收齊，是模組根本沒回**。所以調 `sendAndReceiveQuiet` 的 `quiet_ms`（現為 20ms）
沒有意義，問題不在收包端。

### 🔴 三個獨立的問題
**1. 寫入 20% 無回應。** CH1 是左右螺旋槳共用的那個通道 → 兩成的貼牆指令打不進去。
   QX-DO24 是全專案**唯一的 115200 裝置**，而 RS485 在 115200 下對終端電阻、線長、
   反射特別敏感 —— 我的判斷是先查實體層（終端電阻／線長／gateway 設定），不是改軟體參數。

**2. 錯誤訊息張冠李戴（與 `[2026-08-28b]` 修過的同型，只是漏了這一處）**
```cpp
if (!pwm_.setPWM_Freq(dch, hz))
    return "ERR pwm_freq_rejected_locked_50hz";   // ← 通訊失敗也回這句
```
`setPWM_Freq` 失敗有兩個完全不同的原因（頻率超出鎖定／裝置沒回應），
訊息一律說是頻率鎖。實測送 **合法的 hz=50** 也會收到「頻率被拒」。
📌 `[2026-08-28b]` 才剛在 `Linux_test` menu 34 修掉一模一樣的毛病，
**`cmd_pwm_set` 這一處沒修到**。

**3. 整包 `OK` 掩蓋個別通道讀取失敗**
`pwm status` 十次都回 `OK`，但內容裡 **`ch3` 始終 `ERR`**、**`ch4` 時好時壞**。
呼叫端只看回傳的 `OK` 就會以為四個通道都正常。

### 待完成
- 🔴 實體層檢查（終端電阻／線長／gateway 波特率設定）—— 20% 無回應應該先從這裡查
- 🟡 `cmd_pwm_set` 的錯誤訊息要把「通訊失敗」與「參數超範圍」分開
- 🟡 `ch3` 恆 `ERR`、`ch4` 間歇 `ERR`：是模組通道故障還是同一個通訊問題？
- 🟡 PWM 寫入沒有重試機制；在 20% 失敗率下，貼牆指令應該要重試或把失敗上報 GUI

---

## 2026-08-28（傍晚·續）— 換算修正 cherry-pick 進整理分支；並實測到失步

### 已完成
- **換算修正 cherry-pick 進 `refactor/app-layer`**（`9fa4fe1`，= driver 分支的 `-drv5`）。
  上機用的是這條分支，先前每一步都會把滑台推到底。
  🔴 **破例讓「功能等價」分支多一項改動的理由**：等價性的目的是證明搬家沒搬壞，
  那個證明已完成並留下兩份獨立證據（源碼逐位元 + 實機輸出逐字比對）。
  **為了保住一個已達成目的的證明，而讓上機版本每一步把滑台推到底，取捨已經反過來了。**
  衝突處理成「只取標定」：保留 `void` 簽章、不帶 `recv_frame_`（那兩項留在 driver 分支）。

### 🔴 使用者實測到失步（2026-08-28 傍晚）
使用者回報「應該是剛剛的指令太快、失步了」，並手動調回原點。

**這修正了我先前的結論。** 我一度以為「5 趟記號沒漂 = 沒失步」，後來因為
「零點是硬限位、回零沒有鑑別力」而收回；**現在有實據了**。

📌 **更重要的是它揭露：先前以為的「慢速」根本不慢。**
所有 RPM 常數都是在「1cm/rev」的錯誤認知下挑的：

| 轉速 | 當時以為 | **實際（7.731 cm/rev）** |
|---|---|---|
| 250 rpm（步伐內建） | 4.17 cm/s | **32.2 cm/s** |
| 1000 rpm（`arm_sweep`） | 16.7 cm/s | **128.8 cm/s** |

`ACC/DEC = 100 ms/1000rpm` 同樣是在錯誤前提下挑的 → 已進待辦總表。

### ⚠️ 因此本輪驗證刻意**不跑** `arm_sweep`
重建 `~/bringup/` 之後只驗 `init()` 的標定輸出，**不觸發 1000 rpm（129 cm/s）的移動** ——
在 RPM 重新評估之前再跑一次只是再失步一次。

---

## 2026-08-28（下午·續）— 整套跑起來供 Web GUI 操作（吊機＋本體＋手臂三支）

### 已完成
用 `~/bringup/`（**整理分支**）把整套起起來，`~/projects/` 全程未動：

| 程式 | 機器 | 埠 |
|---|---|---|
| `crane_control_PI.out` | 吊機 `.5.17` | 5002 |
| `node server.js` | 吊機 `.5.17` | 8080（GUI 入口 **http://192.168.5.17:8080**） |
| `facade_cleaning_v2.out` | 本體 `.5.26` | 5001 |
| `motor_api`（手臂） | 本體 `.5.26` | 9527 |

驗證：`ping` → `OK pong`；`status` → `OK state=idle crane_attached=on **arm_attached=on**`；
motor_api 直接問 `STATUS` → `[M1] pos=0.0051 ... [M2] pos=-0.1184 ...`（CAN 兩顆馬達都讀得到）。
**馬達沒有動過。**

### 🔴 啟動時才發現的三件事（都不是我們改壞的）

**1. `web_backend/server.js` 兩個預設 IP 都連不上，必須用環境變數蓋掉**
- `CRANE_IP` 預設 `192.168.1.101` —— **吊機實際是 `.1.10`**（這條 CLAUDE.md 早就記過，
  但沒人改 `server.js`）→ 蓋成 `127.0.0.1`（web 與 crane 同機）
- `WROBOT_IP` 預設 `192.168.1.100` —— ✅ **這個值是對的，不要改**。
  本體 `eth0` 確實是 `.1.100`、`carrier=1`、`operstate=up`。
  🔴 **08-28 per user：兩台目前「刻意」還沒用 eth 串接，之後實際使用時會串。**
  所以雙向 ping 失敗與 `ip neigh ... FAILED` 是**預期結果、不是故障**，
  不要去查 PoE switch 或線路。bench 期間以環境變數蓋成 WiFi `192.168.5.26` 是**暫時 workaround**。
  📌 **兩個預設值性質完全不同**：`CRANE_IP` 是**值寫錯**（要修）；
  `WROBOT_IP` 是**正確的正式組態，只是目前那條線還沒接**（不要修）。
🔴 **照預設值啟動，GUI 會兩邊都連不上**，而畫面上不會說是 IP 寫錯。

**2. 兩台 Pi 都沒有 `tmux`、也沒有 `screen`**
→ runbook §A 的「一鍵啟動」`./scripts/crane.sh start` / `wr.sh start` **在這兩台跑不起來**
（腳本整個建立在 tmux 上）。沒有裝套件（會改動機器），改用 FIFO 背景啟動：
`~/bringup/run_bg.sh <name> <log> <cmd...>`，之後 `echo exit > /tmp/<name>.fifo` 可優雅停止。

**3. ⚠️ 我自己先歸錯因一次（記下來免得再犯）**
重起本體時吃到 `[FATAL] TCP server :5001 fail`，我第一時間寫成「`TCP_server` 沒設
`SO_REUSEADDR`」。**查了原始碼才發現它有設**（`transport/TCP_server.cpp:51`）。
真正原因是**舊實例還活著、還握著 listening socket**（`pkill` 的 TERM 沒有立刻生效），
兩個活著的 listener 本來就不能共用同一個埠（那要 `SO_REUSEPORT`，不是 `SO_REUSEADDR`）。
📌 **正確做法：重起前等 `:5001` 真的沒有行程在聽**，不是等 TIME_WAIT。
（本專案踩坑索引第四條就是「錯誤歸因錯了比沒有訊息更糟」——這次差點自己示範一遍。）

### 收尾（08-28 14:5x）
使用者確認 GUI 操作沒問題後，四支全部停止並複驗：本體 `5001`/`9527`、吊機 `5002`/`8080`
**四個埠皆已釋放、兩台無任何殘留行程**（含 FIFO 的 `sleep` 持有者與 fifo 檔）。
`~/projects/` 全程未動。

🐛 **`exit` 只對兩支 C++ 主程式有效**：`motor_api` 與 `node server.js` 的指令通道不是 stdin，
stdin 送 `exit` 對它們無效，要用 `kill -TERM`（兩者都吃 TERM，不需要 KILL）。
📌 **停止流程要分兩類**，寫成一句「`echo exit > fifo` 就好」會漏掉一半。

### ⚠️ 仍未驗證
- **馬達一次都沒動過**；GUI 上的 DEPLOY / 步伐等按鈕都還沒按過
- ✅ **PWM 實體接線已證實（08-28 下午，唯讀 `pwm status`）**：模組有回應 →
  **QX-DO24 確實接在 gateway 上、slave 9 通**，先前「可能還插在 USB-485 轉換器上」的疑慮排除。
  回覆：`ch1=5,50,65535,1 ch2=5,50,65535,1 ch3=ERR ch4=50,1000,0,0 duty_min=5 duty_max=10 freq_lock=50`
  🟡 兩件值得追：**`ch3=ERR`**（其餘三通道都讀得到）；**`ch4` 存的是 duty=50 / freq=1000**，
  兩者都在 driver 的安全鎖（5~10% / 50Hz）之外——那是模組自己存的組態，不是 driver 寫的，
  但代表**模組出廠/廠商測試殘留值仍在**，日後啟用 ch4 前要先覆蓋
- `depth_cam`（:9530）沒跑 —— 相機路線已永久移除，預期如此

---

## 2026-08-28（下午）— 🎉 `init()` 檢查表兩台都跑完，四件未驗改動全數通過

### 已完成 —— 這條待辦掛了很久：「唯一未驗的是 `init()`」現在清掉了
依 runbook §A2 跑完，**baseline（純 `0d5f6bc`）與整理分支各建一份、兩支輪流跑、逐字比對**。

**吊機**：兩份輸出**逐字完全一致**（28 行）。五個網關全 OK（`.30`/`.31`/`.34`/`.32`/`.33`）、
VFD 左右、SD76 左右、PQW water、DSZL 左右，`init complete — accepting commands`。

**本體**：兩份輸出**只差 IMU 的即時讀值**（`roll=-152.424` vs `-150.804`＝感測器讀數，非程式），
其餘 64 行完全相同。🔴 **四件從沒在實機驗過的改動全部通過**：

| runbook §3 預期 | 實機輸出 | 判定 |
|---|---|---|
| `[OK] DM2J arm rail (slave 14 @ cli_20_)` | 一致 | ✅ **08-28 搬 bus 首次實機驗證通過** |
| `[OK] ZDT 5~8` | 一致 | ✅ 08-27 吸盤改號通過 |
| `[OK] PQW slave 12 @ cli_20_ (.20)` | 一致 | ✅ 08-27 PQW 搬 bus 通過 |
| `[OK] QX-DO24 PWM slave 9 (presence not probed)` | 一致 | ✅ 改號通過（⚠️ 見下） |

⚠️ **`presence not probed` 不等於模組真的接上**：init 不發包，模組若還插在 USB-485 轉換器上
也照樣印 `[OK]`，要到第一個 `pwm set` 才會 timeout。**PWM 的實體接線仍未驗證。**

📌 **搬家等價性其實有兩份獨立證據**：(a) 源碼比對——`WASH_ROBOT.cpp/.h` 相對 `0d5f6bc`
**逐位元相同**，全樹唯一可執行差異是 5 處 `send(..., 0)` → `SEND_FLAGS`，其餘 73 行全是註解；
(b) 本次實機執行輸出一致。

### 🔴 抓到兩個「訊息說謊」（都是 main 既有的，不是合併造成——baseline 輸出一模一樣）

**1. `connectToServer` 把失敗的 connect 判成成功**（已進待辦總表）
實機證據：吊機 `:5002` **確認無人在聽**（`ss -ltn` 查過），本體卻印了 **20 次**
`[INF] [TCP 192.168.5.17:5002] reconnect success`，每 500ms 一次 flapping。

根因在 `transport/TCP_client.cpp:180-220`：非阻塞 connect → `EINPROGRESS` →
`select()` 等可寫 → `if (res > 0) success = true`。
🔴 **失敗的 connect 同樣會讓 socket 變成可寫**，POSIX 要求這裡必須再
`getsockopt(SO_ERROR)` 分辨，那一步沒有 → `connected = true`。

影響：任何以 `connected` 判斷「吊機連線還在」的地方都會被騙。
（指令層本身有抓到：`crane_cmd 'water_inlet off' attempt 1 failed`。）

**2. `init()` 印的 VFD 型號是寫死的**（已進待辦總表）
`CRANE_VFD_IS_SE3 = 1`（實際跑 SE3），但 `main.cpp:4215/4234` 的
`[OK] VFD left (MH300)` 字串在 `#if` **外面**，不管旗標是什麼都印 MH300。

### ⚠️ 這次測試沒有涵蓋到的
- **兩台沒有同時跑**（吊機 server 已關才測本體）→ 本體印
  `[WARN] crane not yet reachable`，且 `[SHUTDOWN]` 時 `water_inlet off` 三次全失敗、
  **收尾訊息是 `valve state UNKNOWN`**。兩台同時跑的整合行為仍未驗
- `arm 127.0.0.1:9527`（motor_api）與 depth_cam 都沒跑
- **只驗 `init()`，沒有讓任何馬達動過**

---

## 2026-08-28（午·續）— driver 分支也合併 main，並修掉一個「修補本身沒生效」的修補

### 已完成
- **`refactor/app-layer`（已含 main 0d5f6bc）合併進本分支**（merge `f33ccfe`）。
  C++ 再次零衝突——本分支動的是 `user_lib/` 的**收包路徑**，main 動的
  `QX_DO24` / `JC_100_METER` 不在其中。衝突只有三份文件。
- **changelog 撞號**：本分支的 `[2026-08-28a]`/`[2026-08-28b]` 與 main 上同日、
  **內容完全不同**的 a/b 相撞 → 本分支兩筆改名 `-drv1`/`-drv2`。
  📌 **字母序號在多分支下必然撞號**，日後跨分支條目建議直接帶分支標記。
- **兩台 Pi 編譯通過**（隔離資料夾 `~/merge_check_20260828/drv/`）：本體、吊機、`Linux_test` 三支。

### 🔴 修掉一個「診斷正確、修法方向也對，但接到的訊號源不存在」的修補
main `[2026-08-28f2]` 發現「上滑台三次寫入全滅、流程照印 rail sweep done」，
把 `arm_sweep_fire_nowait_` 改成檢查回傳值。**但 `PR_move_cm_nowait` 寫死
`return false`（＝成功）**，而它呼叫的 `PR_move_set` / `PR_trigger` 是 `void`——
`writeMulti`/`writeSingle` 的結果在那一層就死了。

→ `any_ok` **恆為 true**，`arm_sweep_rail_no_response` 這個 EVT 與
「⚠ rail sweep 沒有實際發生」的訊息**在任何情況下都不會出現**。
**症狀跟它要修的那個 bug 一模一樣。**

已把三個 `void` 改回傳 `bool` 並逐層傳遞（`-drv3`）。忽略回傳值的既有呼叫端是相容改變。

📌 **新的一條踩坑，與既有的同型但不同面**：「印出來 ≠ 檢查過」「`status=success` 不保證生效」
→ 這次是 **「有在檢查 ≠ 檢查得到」**：判斷式寫對了，被判斷的值卻是個常數。
🔴 **合併時只看「C++ 沒有衝突」會直接放它過去。**

### 待完成
- 🔴 本分支的行為改變仍**不與整理分支混在一起上機**（`~/bringup/` 放的是整理分支）
- 🟡 上滑台寫入失敗現在會真的浮出來 → 上機時步伐日誌會多出以前不存在的失敗訊息，
  那是把問題變可見、不是新故障

---

## 2026-08-28（午）— 合併 main 的 bench 修正批次進整理分支 + 兩台 Pi 實機編譯通過

### 已完成
- **`origin/main` 0d5f6bc（Sadie-fang「fix bug」，16 檔 +1367/-134）已合併進 `refactor/app-layer`**
  （merge commit `5f0bbd4`）。內容是 08-27h ~ 08-28i 的 bench 修正：破真空閥 300ms gap、
  JC-100 fast-fail、`sendAndReceiveQuiet` 分片修復、PWM 改 slave 9 復用、上滑台 DM2J 搬回 `.20`、
  牆距 380→400、滑台掃動等待 1000→4500ms、吊機兩個張力門檻。
- **C++ 自動合併零衝突**，git 正確追蹤 `user_lib/` → `app/` + `transport/` 的兩處 100% rename，
  `user_lib/` 沒有長回重複檔。衝突只有 `CLAUDE.md` 與本檔兩份文件。
- **兩台 Pi 實機編譯通過**（g++ 14.2.0 / Debian）：本體 `facade_cleaning_v2.out`（14 個編譯單元）、
  吊機 `crane_control_PI.out`、`linux_test.out` 三支全 OK。
  🔴 **在隔離資料夾 `~/merge_check_20260828/` 編**，per user：不碰 `~/projects/`（對方的 VS 遠端建置
  落點）也不碰 `~/bringup/`（上機用二進位）。已複驗兩者時間戳未變。**只編譯、沒有執行**
  （runbook：這兩支 `main()` 一開頭就連硬體）。

### 🔴 這次合併真正的風險不是衝突，是「合乾淨了但語意漏一塊」
main 在 `[2026-08-28b]` 新增的 `TCP_client::sendAndReceiveQuiet()` 帶著 `send(..., 0)` 進來，
**繞過了本分支同期做的 `MSG_NOSIGNAL` 修補**（`9e1ad1b`）。兩邊沒碰到同幾行 → git 一句話都不會說。
後果不是「少一個旗標」：對端已關閉時 `send()` 發 SIGPIPE = **終止整個行程**，而這條路徑的唯一
呼叫端 `QX_DO24` 所連結的 `Linux_test` 正好**沒有** `signal(SIGPIPE, SIG_IGN)`。已改為 `SEND_FLAGS`，
四處 `send()` 複驗全部帶旗標。

📌 **通則**：rename 追得對 ≠ 語意接得上。合併後要逐一檢查「對方新增的程式碼有沒有繞過我方新加的防線」，
這件事 git 不會提醒，跟本專案「東西壞了但沒有人被告知」是同一類。

### 順帶修掉的文件失真
- `CLAUDE.md` 的 as-built 匯流排圖是 08-28 由**原始碼**重建的，main 同日改掉兩個 bus 事實
  → 圖立刻過期。已更新（DM2J `.22`→`.20`、QX PWM slave 6→9 且解除停用）。
- main 新增的 `.claude/per_program_cautions.md`（211 行，好文件）§0.2 的 bus 表
  **與它自己那個 commit 的程式改動相反**，已就地更正並補進 `CLAUDE.md` 的 `.claude/` 索引表。

### 待完成
- ✅ **`~/bringup/` 已重建（08-28 12:42/12:43）** —— 兩台的 `.out` md5 皆已改變，
  確認是合併後的版本；`~/projects/` 時間戳全程未變
- ✅ **`fix/driver-crc` 已合併 main**（merge `f33ccfe`，兩台編譯通過），
  並在該分支修掉一個「修補本身沒生效」的問題（見下方待辦與該分支 changelog `-drv3`）
- 🔴 **上機仍是上整理分支**（`~/bringup/` 放的就是它）；driver 分支的行為改變不與這次混在一起
- 🟡 `app/WASH_ROBOT.h:1082` 成員註解仍寫「`.22 = ... arm-rail ...`」，已過期（屬 main 那批的既有債）
- 🟡 兩台 Pi 的 `~/merge_check_20260828/` 是這次的拋棄式編譯資料夾，確認不需要後可刪

---

## 2026-08-28 — 更正「部署分岔」誤判

> **規範權威：** 待辦總表（本檔最上方）該列已同步更正。

### 🐛 踩到的坑（本專案已記過的同型錯誤，第三次）

🔴 **`diff` 回報「整個檔案每一行都不同」時，先問「為什麼是*全部*」，不要直接推論成「另一個程式」。**

08-27 的結論是「Pi 上的 `web_ver2` 與 repo 分岔 589 行、不是增量是另一個程式、讓『repo 是權威』當場失效」。
實際原因是 **Pi 上的檔案是 CRLF、repo 是 LF** —— `diff` 因此判定每一行都不同。去掉 `\r` 之後：

| 檔案 | 表面 | 實際 | 結論 |
|---|---|---|---|
| `server.js` | 330 行全不同 | +77 / −6 | 與 commit `faf1d3f` **逐位元相同** |
| `public/app.js` | 2,087 行全不同 | +160 / −9 | 與 commit `a894ae1` **逐位元相同** |
| `public/style.css` | 1,104 行全不同 | +113 / −3 | 與 commit `faf1d3f` **逐位元相同** |
| `public/index.html` | 908 行全不同 | +60 / −6 | 差異**全部**是攝影機面板那一段 |

**時間軸完全吻合**：Pi 上所有檔案 mtime 都是 `2026-08-27 15:31`（**同一秒＝複製不是編輯**；旁證是目錄裡躺著
git-bash 的 `bash.exe.stackdump`），而移除攝影機的 commit `e3c8820` 是**同日 17:16**。
即：有人 15:31 把當時的 Windows 工作樹 scp 上 Pi，兩小時後才 commit 攝影機移除。

📌 **更正後的事實**：**沒有分岔，repo 仍是權威**，Pi 只是落後一個 commit。
📌 **順帶更正**：`web_ver2` 在**吊機** `raspberry-cran`（`user@192.168.5.17`），不在本體 —— 本體上根本沒有這個目錄。

**這與本檔既有的兩條是同一型**：〈在某一個時間點看一眼就下定論〉、〈一次觀察就下結論（SD76 CRC 誤報）〉。
差別只在這次的觸發器是「工具的輸出被當成結論」而不是「觀察次數不足」。

🔮 **伏筆**：日後從 WSL 這端 `scp`／`rsync` 送原始碼上 Pi（repo 是 LF）就不會再混進 CRLF；
**經 Windows 中轉才會**。要比對 Pi 與 repo 一律先 `sed 's/\r$//'` 正規化再 `diff`。

---

### 📚 全 repo 盤點：把「孤兒檔案」全部登記進 `CLAUDE.md`

**問題**：一份沒被任何索引指到的文件等於不存在。接手的人只能靠現有程式碼反推——
**那正是 DM2J 那次踩雷的方式**。這次把 `.claude/` 與根目錄全部盤完，`CLAUDE.md` 新增兩張索引表，
並各自標明「🔴 新增檔案必須在這裡加一列」。

**`.claude/`（13 項）**：5 個孤兒 —— `crane_balance_hold_plan.md`／`mh300_migration_plan.md`／
`step_speedup_phase1_plan.md`／`mailbox.md`／`gen_deploy_pdf.py`，其中**兩個裝著待辦表標為「唯一副本」的內容**。

**根目錄（8 項孤兒）**：`README.md`（唯一記載 fork 出身）／`ONBOARDING.md`（**52 KB、11 章知識庫**）／
`facade_cleaning_v2.sln`／`deploy_and_test.pdf`／4 個 `.txt` 手冊擷取／`.vs`＋`tmp`（已 gitignore）。

**🐛 盤點抓到的實際錯誤**
- 🔴 **`CLAUDE.md` 的 Build System 寫 `washrobot_new_PI.sln`——repo 裡沒有這個檔**
  （那是 fork 前 v1 的檔名），實際是 `facade_cleaning_v2.sln`。照舊寫法跑會直接失敗。已更正
- 🔴 msbuild 範例寫 `/p:Platform=ARM`，但**只有 `Debug|ARM64` 設了 include 路徑**。已改 ARM64
- ⚠️ **4 個檔（228 KB）是「看了會被誤導」而不只是沒用**：`main_tmp.txt` 描述的是 v1 拓樸
  （`.21` 匯流排、`washrobot_new_PI` 抬頭）；`dm2j_manual.txt`／`dm2j_manual2.txt`／`zdt_modbus.txt`
  編碼壞掉無法閱讀，且都已被 `summaries/` 取代。**是否刪除待使用者決定**

**📌 `.claude/summaries/`（8 份手冊摘要、1,228 行）另立一節**：原始 PDF 不在 repo
（Windows 端 `D:\洗窗戶機器人\電控設備資料\`），所以對只有 repo 的人來說**這 8 份就是手冊本身**。
🔴 既有待辦「VFD 故障碼顯示是壞的」要的 SE3 錯誤碼對照表，**就在
`SE3_INVERTER_MODBUS_SUMMARY.md` 的 `## Error Code Reference (H1007 / H1008)`**。

**🔴 一條通則**：「已歸檔」不等於「內容失效」。`archive/se3_mode6_migration_plan.md` 判定為已作廢，
但 bench 現在跑的還是 SE3，它的 §1.1 是**唯一一張「SE3 故障 ↔ workaround」對照表**。
歸檔時墓碑抬頭務必寫清楚「裡面哪些知識仍然有效」——現有三份墓碑都寫得很好，這條是為了保持。

---

### 📚 `.claude/summaries/`（細節）

**問題**：8 份硬體手冊摘要（1,228 行）**從未出現在任何索引裡**，接手的人不會知道它存在，
只能靠 driver 現有程式碼反推協定——**那正是 DM2J 那次踩雷的方式**。

📌 **原始 PDF 不在 repo 裡**（Windows 端 `D:\洗窗戶機器人\電控設備資料\`），
所以對只有 repo 的人來說，這 8 份摘要**就是手冊本身**。

已在 `CLAUDE.md` 文件架構表加一列 + 新增一節列出每份的重點。
🔴 **順帶發現**：既有待辦「VFD 故障碼顯示是壞的」要的 SE3 錯誤碼對照表，
**就在 `SE3_INVERTER_MODBUS_SUMMARY.md` 的 `## Error Code Reference (H1007 / H1008)`**
——不必再翻 PDF。

---

### 🚀 上機準備完成（等機器空出來）

**🔴 上機的是 `refactor/app-layer`（整理分支），不是 `fix/driver-crc`。**
使用者要求「功能上要跟原本的程式一樣，因為我們只是做整理」——這跟 driver 那批**有衝突**：

| 分支 | vs `main` 的程式碼差異 | 功能等價？ |
|---|---|---|
| `refactor/app-layer` | 檔案搬家（`app/`＋`transport/`）、移除 `windows_test`、`send()` 加 `MSG_NOSIGNAL`（3 處） | ✅ **是**。兩支 `main.cpp` 本來就有 `signal(SIGPIPE, SIG_IGN)`，`send` 兩種寫法都回 `-1/EPIPE`，可觀察行為相同 |
| `fix/driver-crc` | 上面全部 ＋ **9 支 driver 的回覆驗證** | ❌ **否**，是刻意的行為改變 |

📌 **實測確認整理分支的 `user_lib/` 九支 driver 一行都沒動。**
分開上機的理由：第一次上機若行為和現在不同，才分得清是「搬家搬壞」還是「driver 改的」。

**🐛 修掉的實際錯誤：部署路徑五處全錯**
`scripts/crane.sh`／`wr.sh` 與 runbook 寫的 deploy 路徑，今天跑會直接報錯：

```
實際  ~/projects/crane_control_PI/bin/ARM64/Debug/crane_control_PI.out
舊值  ~/Crane_control_PI/bin/ARM/Release/Crane_control_PI
```
少 `projects/`／`ARM` 應為 `ARM64`／`Release` 應為 `Debug`／檔名少 `.out`／
web 目錄寫 `washrobot_web_backend`（**兩台上都不存在**，實際是 `web_ver2`）。
這佈局是 VS 遠端建置產生的（`bin/<Platform>/<Configuration>/<name>.out`），註解已寫明。

**已備妥**：`runbook.md` 新增 **A2 上機檢查表**，兩條建置指令**已在兩台上逐字實跑驗證**
（吊機 10 個編譯單元、本體 14 個平行編），二進位就放在兩台的 `~/bringup/`。
🔴 **`~/bringup/` 刻意與 `~/projects/` 分開**——後者是 VS 遠端建置的落點，
另一位開發者在 `main` 上迭代時會重建覆蓋它（今天 11:29 就重建過一次）。

**🔴 實機盤查到的關鍵事實**：兩台的部署樹只有 `<project>/` 與 `user_lib/`，
**沒有 `transport/`、沒有 `app/`** → **對方是從 `main` 建的，不是我們這條分支**。
所以正式部署等於把另一個世代的程式碼換上去，不只是佔用機器而已。

**唯一真正的未知數**：`init()` 從來沒跑過（那 3 秒的 `--help` 意外不算），
所以本體那張逐項 `[OK]` 硬體檢查表一項都沒驗過。且 `init()` 第一個失敗就 `return`，
而 08-27 才改過吸盤改號（1-4→5-8）與 PQW 搬 bus（.22→.20）兩件事。

---

### ✅ driver 回覆驗證稽核全部收尾（16 支，4 個 commit）

mailbox 2026-05-14 開出、擱置 3.5 個月的行動項，2026-08-28 結案。

| 結果 | 數量 | 是哪些 |
|---|---|---|
| 本次修補 | **9 支** | SD76／DSZL／DY-500／PQW／SE3／ZDT／DM2J／MH300／CLV900 |
| 本來就有驗 | 4 支 | JC-100／XKC／QX-DO24／WT901BC（sum 式） |
| 標記退役 | 1 支 | ZS_DIO（production 已不用，只剩 `Linux_test`） |
| 無通訊／未被引用 | 2 支 | FrameAnalyzer／DIHOOL |

**🔴 記憶體覆寫這個類別已經關閉**：三支（SD76／DSZL／DY-500）都會被壞幀打成
呼叫端堆疊覆寫，實測 SIGSEGV／SIGBUS／SIGBUS。修完之後 repo 裡沒有已知的第四支。

**測試框架已進版控**：`Linux_test/fake_slaves/`。原本兩支一次性腳本合併成通用的
`fake_rtu.py`（除 DSZL 外全部走 RTU over TCP，一支服務 7 支 driver）。
全部驗證 **20 + 8 + 7 + 6 + 5 = 46 個情境通過**，一律先對未修補版證明缺陷存在再修。

**📌 三個決策，記下來免得日後被「順手補完」**
1. **`PQW` 的寫入 echo 路徑刻意不動**——韌體 echo 格式非標準，先前「讀回來比對」
   造成間歇假失敗、**把機器卡在序列中間且無可恢復路徑**（work_log 2026-04-23／04-27）。
   只硬化 FC01 讀取
2. **`ZDT` 的 `readEcho()` 不檢查 slave id**——`trigger_sync_move()` 走廣播 `0x00`
3. **`DM2J` 的 `sendRecv()` 不檢查 slave id**——`writeSingle_sync()` 走廣播；
   但 `recv_frame_()` **要**檢查，因為手臂滑台是 `cli_22_` 上的 slave 14，
   同 bus 還有 JC100 5-8／XKC 13／DY500 10-11，匯流排競爭是有紀錄的症狀

**🐛 稽核過程中我犯的三個錯（都被測試抓出來，值得記）**
1. **測試用錯 `init()` overload**（不 probe 的那個）→ 五個故障情境一個都沒觸發卻全部「通過」
2. **溢位測試值取極端值 `bc=255`** → 落在缺陷窗口之外、回 FAIL，差點判定「無缺陷」。
   窗口是 62~247，換 `bc=100` 才 SIGBUS
3. **`wrongslave`／`badfc` 的幀帶著正確 CRC** → 只補 CRC 抓不到。
   `dm2j` 因此在第一輪還是被判 WRONG，才回頭補上 slave id 與 FC 檢查。
   **CRC 只證明「這則訊息沒壞」，不證明「這則訊息是給我的」**

**🔴 行為改變風險（部署前必讀）**
- **`ZDT` 在步態迴圈裡**，原本被吞掉的壞幀現在是明確失敗 → **步態中途失敗頻率可能上升**。
  第一次上機要有人看著
- **`PQW`** 可能讓既有待辦「CH6 verify fail 三次後沒人 catch」更常出現 → 那條要一起重評
- **`SD76`** 可能讓既有待辦「crane 端偶發 `ERR meter_left_read_fail`」更常出現
- 三者都是**把原本看不見的問題變可見**，不是新故障

🔴 **九支全部尚未部署、尚未上實機驗證。**

---

### 🔒 DSZL_107 同型溢位已修（稽核找出的第二支，而且是活的）

細節見 `changelog.md` `[2026-08-28b]`。DSZL 走 Modbus TCP（MBAP）沒有 CRC，
對應檢查換成 **txid / unit id / byteCount**。強制 `bc == quantity*2` 就完全夾住，
**不必改 API 簽名**。🔴 這條是張力感測路徑，`hold_loop` 安全監控靠它。

**🐛 我第一版測試值挑錯，差點得到「缺陷不存在」的結論**
`bc=255` 修補前**回 FAIL 而非崩潰**——該幀 264 位元組超過 256 收包緩衝，既有的
`n < 9 + bc` 剛好擋住。真正的溢位窗口是 **`bc` 介於 62~247**。補了 `bc=100` 才重現
**SIGBUS，3/3**。
📌 **通則：邊界類驗證的測試值必須落在窗口內，取極端值反而測不到。**
「通過」看起來像「缺陷不存在」，其實是測試值落在窗口之外——**與 08-27 那次
「SD76 CRC 誤報」是鏡像關係**（那次是誤判有問題，這次差點誤判沒問題）。

### 🗑️ 刪除四個會誤導人的檔案（228 KB）

`main_tmp.txt`（v1 拓樸的舊副本）＋ `dm2j_manual.txt`／`dm2j_manual2.txt`／`zdt_modbus.txt`
（編碼壞掉無法閱讀，已被 `summaries/` 取代）。
📌 **判準是「看了會被誤導」而不只是「沒用」。** 保留 `dm2j_manual_utf8.txt` 作原文對照。
四個檔都在版控裡，需要時從 git 歷史取回。

---

### 🔒 SD76 CRC 修補完成 + 全 driver 回覆驗證稽核（分支 `fix/driver-crc`）

修補內容與驗證過程見 `changelog.md` `[2026-08-28a]`，這裡只記結論與待辦。

**🔴 修補前實測到的最嚴重一項**：`byteCount = 0xFF` 的回覆讓行程 **SIGSEGV**——
待辦原本記的是「壞值往上傳」，實際是**堆疊覆寫**（255 位元組 memcpy 進 `uint8_t raw[2]`）。

**🐛 我的測試框架第一版是錯的**：用了不做 probe 的 `init(ip, port, ...)` overload，
導致五個故障情境一個都沒被觸發、卻全部「通過」。→ **驗證通過 ≠ 真的做了事**，
本專案第 N 次。修法是改用 production 真正在用的 `init(TCP_client&, ...)`。

---

### 📋 全 driver 回覆驗證稽核表（16 支，2026-08-28 逐檔讀原始碼）

mailbox 2026-05-14 附帶的行動項，開了 3.5 個月從未執行。**只稽核不修**——
一次改 16 支等於同時動 16 個上層錯誤路徑，出事無法歸因。

| Driver | slave ID | FC | 長度邊界 | 收 CRC | 風險 |
|---|---|---|---|---|---|
| `SD76_length_meters` | ✅ | ✅ | ✅ | ✅ | 🟢 **本次已修** |
| `DSZL_107` | ✅ | ✅ | ✅ | n/a（Modbus TCP，改驗 txid） | 🟢 **2026-08-28 已修**（`bc=100` 實測 SIGBUS 已消失） |
| `DY_500_weight_sensor` | ✅ | ✅ | ✅ | ✅ | 🟢 **已修**（溢位實測 SIGBUS 已消失） |
| `CLV900_inverter` | ✅ | ✅ | ✅ 固定 `0x02` | ✅ | 🟢 **已修**（未安裝，先鋪路） |
| `MH300_inverter` | ✅ | ✅ | ✅ 固定 `0x02` | ✅ | 🟢 **已修**（未啟用，先鋪路） |
| `SE3_inverter` | ✅ | ✅ | ✅ 固定 `0x02` | ✅ | 🟢 **已修**（read + write echo 兩處） |
| `ZDT_motor_control` | ✅ 呼叫端 | ✅ | ✅ vector | ✅ | 🟢 **已修**（收斂在 `readEcho()` 一處） |
| `DM2J_RS570` | ✅ | ✅ | ✅ | ✅ | 🟢 **已修**（新 private `recv_frame_()` + `sendRecv()`） |
| `PQW_IO_16O_RLY` | ✅ | ✅ | ✅ vector | ✅ | 🟢 **已修**（僅 FC01 讀取；寫入 echo 依原決策不動） |
| `ZS_DIO_R_RLY` | ❌ | 部分 | ✅ 有夾 `3+bc+2` | ❌ | ⚰️ **標記退役**（production 已不用，只剩 `Linux_test`） |
| `JC_100_METER` | — | — | — | ✅ | 🟢 |
| `XKC_Y25_RS485` | — | — | — | ✅ | 🟢 |
| `QX_DO24` | — | — | — | ✅ | 🟢 |
| `WT901BC_TTL` | n/a（序列 IMU） | n/a | — | ✅ sum | 🟢 |
| `DIHOOL_control` | ❌ | ❌ | ? | ❌ | ⚪ **未被任何主程式引用**（全 repo grep 無呼叫端） |
| `FrameAnalyzer` | — | — | — | — | ⚪ 無通訊 |

📌 **`.claude/summaries/` 的 8 份手冊摘要證實**這些裝置全用標準 Modbus CRC16
（0xA001 / init 0xFFFF / LSB first），所以補驗 CRC 在協定上一律成立，
而且**每支 driver 都已經有 CRC 函式**（只用在發送端），補驗證不需要新寫演算法。

**🔴 稽核產生的新待辦（依風險排序）**

1. 🔴 **`DSZL_107` 同型溢位，且它是活的**——張力計走 `hold_loop` 安全監控。
   `n < 9 + bc` 只夾了收包長度，沒夾呼叫端的 `buf[64]`
2. 🔴 **`DY_500` 三項全缺**（連 FC 都不檢查）——但硬體未安裝、polling 關閉，**目前打不到**
3. 🟡 **`SE3` 補收 CRC**——bench 現用，值不可信但無記憶體風險
4. 🟡 其餘 6 支補 slave ID / CRC

---

### 📐 全專案架構掃描 → `CLAUDE.md` `## Architecture` 全節重建

**動機**：每個 session 都在重掃同一批檔案。這次一次掃完並落成文件，下次直接讀。

**改了什麼**：`CLAUDE.md` 原本的架構圖是 **v1**（`DM2J×5`／`ZDT×9`／三區真空／`.21` 匯流排），
與現行程式碼差距大到會誤導。整節由原始碼重建，每個數字標出處檔案行號。新增了原本沒有的
**「執行時的行程拓樸」**——它回答「我該 ssh 去哪台」，而這件事原本要靠實機盤查才知道。

📌 **`motion_flow.md` §2 刻意不動**：它是已凍結的 v1 世代文件，維持 v1 是正確的，不是遺漏。
待辦總表該列已據此結案。

### 🐛 掃描中發現的三件事

**1. `WASH_ROBOT.cpp` 有 3,879 行死碼（佔全檔 12,931 行的 30%）**
16 個 `#if 0` 區塊，最大一塊 897 行。🔴 **關鍵**：既有待辦寫「v1 舊 body 用 `#if 0` 包起來當
reference，bench 驗證 v2 綠燈後再硬刪」——但它們**已經無法靠改回 `#if 1` 復活**，
裡面引用的 `ZDT_LB1`／`ZDT_RB1`／`ZDT_C` 在 `WASH_ROBOT.h` 裡**已經不存在**
（預處理器吃掉才沒報錯）。所以「留作參考」只剩閱讀價值、沒有復原價值 —— **刪除的顧慮比想像中小**。

**2. 註解描述舊配置、程式碼本身是對的**（`WASH_ROBOT.h:513` 開頭的「right{1,2}/left{3,4}」、
`:1020` 的「.20 = ZDT pushers 1-4」、`cmd_water_pump` 宣告處的「PQW CH6」實為 CH14）。
📌 **判準：常數定義 > 附近註解。**

**3. 🔴🔴 CH6／CH14 是安全關鍵的一對**：CH6 = 破真空閥、CH14 = 水泵（2026-08-27 對調）。
若水泵仍指向 CH6，清洗時開水泵＝開破真空 → 4 顆吸盤同時失壓 → **貼牆狀態下脫落**。
兩個常數的沿革註解務必保留。

### 🔧 順手更正 `runbook.md` 的過期事實

- 吊機有線 `192.168.1.101` → **`192.168.1.10`**（5 處）
- `ssh pi@` → `ssh nexuni@` / `ssh user@`（4 處，帳號本來就不是 `pi`）
- ⚠️ 一併記下：`web_backend/server.js` 的 `CRANE_IP` 預設值**仍是 `.101`**（過期），
  但現行程式實際走 `app/WASH_ROBOT.h` 的 `CRANE_IP = "192.168.5.17"`（WiFi），所以不影響運轉

---

### ✅ 吊機端編譯驗證通過（`refactor/app-layer` 首次編譯）

在 `user@192.168.5.17:~/verify_20260828/`（**刻意不碰 `~/projects/`**，main 分支的人正在上面工作）：

```
g++ -std=c++17 -O2 -Itransport -Iuser_lib -o crane.out \
    Crane_control_PI/main.cpp transport/{TCP_client,TCP_server}.cpp \
    user_lib/{CLV900_inverter,DSZL_107,DY_500_weight_sensor,PQW_IO_16O_RLY,MH300_inverter,SD76_length_meters,SE3_inverter}.cpp -lpthread
```

- **零錯誤零警告**，49 秒，產出 aarch64 ELF 416 KB
- **意義**：分層重構（`TCP_client.h` `user_lib/` → `transport/`）在吊機端成立；
  順帶驗掉 `MSG_NOSIGNAL` 那個 commit（`transport/TCP_client.cpp`，先前從未編譯過）
- `-Wall -Wextra` 全 10 個編譯單元只有 **4 個警告，且都不是重構造成的**：
  `DSZL_107.h` 成員初始化順序 3 個 `-Wreorder`、`DY_500_weight_sensor.cpp:221` 一個
  `hasError` 設了沒用（🟡 **這是「錯誤被吞掉」的小型案例**，DY500 硬體未安裝所以目前無感）

### ✅ 本體端編譯驗證通過（分層重構最大的一塊）

在 `nexuni@192.168.5.26:~/verify_20260828/`（同樣不碰 `~/projects/`）。14 個編譯單元先各自產 `.o`
（`xargs -P4`，wall 23 秒 / CPU 50 秒）再連結，**零錯誤**，產出 `wr.out` aarch64 ELF 1,035 KB。

- **`app/WASH_ROBOT.{h,cpp}`（15,321 行、佔全專案 37%）從 `user_lib/` 搬到 `app/` 後編得過** ——
  這是分層重構爆炸半徑最大的一步
- `transport/Serial_port.cpp` 首次被編譯（吊機那包沒有它）
- 🔴 **`SERIAL_PORT_H` guard 衝突沒有發生 —— 而且是用正面斷言驗的**，不是靠「沒看到錯誤訊息」：
  `g++ -M` 展開相依樹確認 `WASH_ROBOT.cpp`／`main.cpp`／`WT901BC_TTL.cpp` 三者
  **都只拉到 `transport/Serial_port.h`，沒有任何一個碰到 `user_lib/SerialPort.h`**。
  原因是後者只經 `user_lib/damiao.h` 進來，而 `damiao.h` 只有 `cleaning_arm` 用 ——
  📌 **這條待辦仍然有效**（風險還在，只是這個編譯組合碰不到它），不要因為這次沒爆就結案

`-Wall -Wextra` 共 10 個警告，**沒有一個來自重構**：`WASH_ROBOT` 7 個（成員初始化順序、
兩個 `cmd_step_*` 裡沒用到的 `cur`、一個 `size_t` vs `int` 比較、一個沒用到的 lambda 參數）、
`Serial_port.cpp` 2 個、`DY_500` 1 個。

⚠️ **`Serial_port.cpp` 那 2 個不是缺陷**：`tx_multiplier`／`tx_constant` 只在 `#ifdef _WIN32`
分支裡有意義，Linux 端本來就用不到 —— 查過原始碼才下這個結論，沒有只看警告就當成 bug。

📌 **兩台的 `~/verify_20260828/` 已於同日依使用者指示刪除**（吊機 2.4 MB／本體 5.6 MB，
內容只有原始碼複本與編譯產物）。下次要驗證重新 `rsync` 一份即可，指令見本節上方。

### ⏸ 本體 B3（硬體連線）未做：機器被佔用

`facade_cleaning`（pid 2866）已運轉 1h28m、佔著 5001、握著 `.22` 匯流排三條連線、
並連著吊機 5002；`192.168.5.25` 開了 7 個 SSH session。跑 `init()` 需要獨佔 `.20`/`.22`，
會跟運轉中的系統搶匯流排，因此**只編譯、沒有執行 `wr.out`**。

---

### 🐛 我製造的風險：`--help` 把程式啟動了

`./crane.out --help` —— **這支程式不認 `--help`，等於直接啟動**。它連上 `.30`/`.31`/`.34` 三個 RS485
網關才退出（`.32`/`.33` 因為被正在運轉的 pid 1653 佔著而 connect failed）。
**有幾秒鐘我的行程與運轉中的系統共用同一條 485 匯流排。**

📌 **通則**：在有硬體的機器上跑沒跑過的二進位，先確認它**不會在 main 一開頭就自動連硬體**
（本專案的 `main()` 就是這樣寫的）。要看用法請讀原始碼，不要拿 `--help` 試。
事後已確認：我的行程無殘留，pid 1653 五條硬體連線都在、系統正常。

### ⏸ A3~A5 未做（吊機被佔用）

盤查時 `crane_control_PI.out`（pid 1653）已運轉 1h56m、`node` 同時在跑、5002/8080 皆被佔，
另有兩個從 `192.168.5.25` 進來的 SSH session；**本體 `192.168.5.26` 也連著吊機 5002**
——整套系統正在被人操作。因此**沒有啟動程式、沒有送任何指令、沒有讓機器動**。

---

## 2026-08-28 — 開發環境：本機可以在 commit 前做 C++ 語法檢查

> **規範權威：** 無（環境筆記，非設計決策）。這台開發機的既有工具，不是新增依賴。

專案沒有 CMake/Makefile，改完 C++ 只能等部署到 Pi 才知道編不編得過 —— 這輪就因此
讓使用者踩到一次 `missing terminating " character`（腳本寫入時把 `\n` 寫成了真實
換行，字串跨行）。其實本機裝了 Visual Studio 2022，`cl /Zs`（只做語法檢查、不產出
obj）可以當場驗，**幾秒鐘就有結果**。

```
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /nologo /utf-8 /std:c++17 /EHsc /Zs ^
   /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
   /FIwinsock2.h /FIws2tcpip.h /I user_lib ^
   user_lib\WASH_ROBOT.cpp
```

四個 flag 都是必要的，少一個就會被大量假錯誤淹沒：

| flag | 少了會怎樣 |
|------|-----------|
| `/utf-8` | MSVC 用 cp950 讀檔，中文註解裡的字元被誤解 → 一堆 `C2001 常數中包含新行字元`，看起來像字串壞掉但其實是編碼 |
| `/FIwinsock2.h /FIws2tcpip.h` | `windows.h` 先於 `winsock2.h` 被拉進來 → ~100 個 `sockaddr 重複定義`，在第一個 include 就爆，**根本讀不到你改的碼** |
| `/DNOMINMAX` | `windows.h` 的 `min`/`max` 巨集吃掉 `std::max(...)` → 20+ 個 `C2589 '(' :: 右邊的語彙基元不合法` |

**⚠ 判讀方式：比對 HEAD，不要看絕對錯誤數。** 這是 Linux 目標的專案，MSVC 有些
地方比 GCC 嚴格（例如 `WASH_ROBOT.cpp:7160` 的 `CRANE_METER_SANITY_MAX_CM` 在
lambda 內被 MSVC 要求顯式 capture，GCC/C++17 不需要）。那類錯誤 HEAD 本來就有，
不是你改出來的。做法：

```
git show HEAD:user_lib/WASH_ROBOT.cpp > /tmp/head.cpp
# 兩邊用同一組 flag 編，比對錯誤「集合」而非數量
```

行號會因為新增註解而位移（這輪是 +46 行），所以比對時要看錯誤**種類與相對位置**，
不是行號本身。另外 `find /c "error"` 的數字在撞到 `C1003 錯誤計數超過 100 停止編譯`
時會被截斷成假的相等，別被騙。

---

## 2026-08-27（晚）— 吊機唯讀盤查（未讓機器動）

> **規範權威：** 連線資訊見 `.claude/runbook.md`；分層見 `CLAUDE.md` Repository Structure。

### 連線資訊（實測，與文件不同）
| 機器 | hostname | 有線 | WiFi | 帳號 |
|---|---|---|---|---|
| 洗窗本體 | `washrobot` | `192.168.1.100` | `192.168.5.26` | `nexuni` |
| 吊機 | `raspberry-cran` | **`192.168.1.10`**（文件寫 `.101`，錯） | `192.168.5.17` | `user` |

- 兩台皆 aarch64 / Debian 13 (trixie) / g++ 14.2；**測試環境實體在倉庫（新國街）**
- `app/WASH_ROBOT.h` 的 `CRANE_IP = "192.168.5.17"` → 現行程式本來就走 WiFi 跟吊機講話
- 工程師留的啟動說明路徑少了 `projects/`（實際 `~/projects/crane_control_PI/`、`~/projects/web_ver2/`），
  且 `WROBOT_IP` 寫 `192.168.5.19`，洗窗機實際在 `.26`

### 已完成
- **五個網關全部可達**：`.30`/`.31`（SE3 左右）、`.34`（SD76 計米）、`.32`/`.33`（X518 張力）
- 啟動 `crane_control_PI.out` 讀取後**已停止並清乾淨**，回到進場前狀態（原本沒有任何程式在跑）
- 只送唯讀指令（`ping`/`status`/`tension`/`home_status`/`vfd_fault`），
  **未碰任何會讓機器動的指令**（`up`/`down`/`pay_out`/`retract`/`align_lengths`/`roll_correct`/`zero_*`）
- 讀到：`length_left=-18 length_right=-21`、`tension_left=27.35 right=14.98`、所有 `dev_* = 1`
- 四項發現已進待辦總表（見上方）

### 🐛 本次踩到的兩個坑
- **`pkill -f crane_control_PI.out` 比中了執行它的那條 SSH 指令自己**，把遠端 shell 殺掉 →
  後面的 `rm` 沒跑到，看起來像「清理失敗」。**這正是本檔既有那條 `pgrep -f` 自我比中的同型坑**，
  當時記的是 SSH 轉發，這次換成遠端程式。**判斷程式在不在，用 `ss -ltn` 看埠、或 `ps -eo comm` 比對執行檔名**
- 🔴 **我一度誤報「SD76 讀到 CRC 不符、重現了驅動未驗 CRC 的缺陷」——那是我的探測程式沒排空網關緩衝造成的。**
  重讀 12 次（左右各 6 次）CRC 全部正確、回覆位元組完全一致。
  📌 **驅動沒驗 CRC 仍是真缺陷，但今天沒有重現它**；一次觀察就下結論，跟本專案 OCR 那次是同型錯誤

### 待完成
- 🔴 上述四項（部署分岔／張力刻度／張力差／故障碼），詳見待辦總表
- 🟡 吊機二進位是 7/23 版，比 repo 舊；repo HEAD 的改動未部署過

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

### 歷史摘要 #1（2026-07-07 ~ 2026-07-23）— v2 應用層重寫 / crane 三起實機事故 / depth camera 上線

> **規範權威：** `.claude/changelog.md` 2026-07-15b~f、2026-07-21、2026-07-21b、2026-07-22e、2026-07-23；`.claude/motion_flow.md` §4b「同步步伐」；`.claude/v2_app_redesign_plan.md`（應用層重寫）＋ `.claude/mh300_migration_plan.md`（吊機變頻器）；memory `project_v2_mechanical_gait` / `project_new_crane_vfd_mh300`。

**決策**
- v2 是新硬體 fork：4 吸盤（推桿 slave 右 {1,2} / 左 {3,4}）、真空 **2 區**（左閥/右閥）無中心杯、**無 DM2J**（無滑軌無輪組、無橫向）；垂直位移改成「單側吊機放/收繩 ＋ SD76 計米量測」取代 v1 的滑軌，水平靠 IMU ＋ 左右繩長差。PQW 通道重配為 CH1=右腳閥 / CH2=幫浦 / CH3=左腳閥。
- 步態不變式：**至少一側撐住，絕不 4 杯全放**。
- 專案改名 washrobot_new_PI → facade_cleaning_v2（`a2e0704`）；吊機變頻器 SE3 → MH300（`1829964`）。

**🔮 伏筆（刻意留的，別當垃圾清掉）**
- `do_step_down_`/`do_step_up_` 及 fork 中性化的 v1-only 函式，舊 body 一律用 `#if 0` 包著當 reference，**說好 bench 驗 v2 綠燈後才硬刪**——不是忘了刪。
- 同步步伐 `step_down_sync`/`step_up_sync` 是**唯一**打破「至少一側 ≥1 顆吸盤黏牆」不變式的走法（放繩期間完全靠鋼索承重），這是 user 明確確認過的刻意設計；IMU 差動微調是用 v2 方式重新呼叫 crane 既有的 `roll_correct <delta_cm>`，**沒有**復用綁死 body/center valve 的 v1 `do_phase5_roll_correct_`。
- `cmd_recover` 的 `vacuum_check` 刻意保持嚴格（2026-06-02 決策），2026-07-23 修 `step_down_sync` 判準時特意沒動它。
- 開機第一件事建議先單獨 build `Crane_control_PI`（與 WASH_ROBOT 是獨立編譯單元）拿綠燈，再往下大改 WASH_ROBOT，才隔離得出編譯錯誤來源。

**⚠ 踩坑 / 教訓**
1. **(07-23)** `step_down_sync` 回報 `partial_seal count=2` 卻直接進 `State::Error` — 2026-07-22 新增 `do_step_sync_` 時最終真空判準誤寫成「4 顆全吸」，而 v2 既有慣例是 `group_seal_ok_` 的「**每側 ≥1**」。已改成只有整側全掉才判失敗。
2. **(07-21)** `run_depth_avoid` 秒失敗在 `before_capture_failed`（空訊息）——`scripts/wr.sh` 從沒開過 `depth_cam_service.py` 這個 window（新檔案，沒接進腳本）。症狀長得像程式 bug，其實是服務根本沒起。
3. **(07-21)** Camera 頁「拍照」永遠 offline —— `/snap/depth` 拿的是「上一次 `run_depth_avoid` AFTER 分析結果圖」，服務剛啟動、沒跑過 BEFORE/AFTER 時 buffer 是空的直接回 503。已另開 `/snap/depth_live` 給即時原始畫面。
4. **(07-21)** `unknown_cam` 純粹是 web_backend 沒重啟、還在跑改動前的 `server.js`——不是程式 bug。**先確認服務有起、後端有重啟，再懷疑程式碼。**
5. **(07-21，07-22~23 亦記)** `user_lib/TCP_client.cpp` 殭屍連線：Linux 上 `available()` 用 `ioctl FIONREAD` **偵測不到對端正常關閉**，且 `sendData`/`receiveData`/`sendAndReceive` 失敗時不會把 `connected` 設回 `false` —— 兩者疊加使 `reconnectLoop()` 永遠不觸發，client 卡死在**假的「已連線」**狀態，只能重開主程式。已改用 `recv(MSG_PEEK)` 比照 Windows 分支。**影響所有用 `TCP_client` 的裝置，不只 depth cam。**
6. **(07-15)** 吊機通訊頻繁 PAUSE-ON-ERROR ＋ 快速重試 —— client timeout 太短會強制斷線重送，且 `cmd_side_measured` 沒有 `motion_mtx` 保護會被重複驅動。已補鎖 ＋ 補 log。
7. **(07-15)** 傾斜 49.6° 觸發 IMU 緊急停 —— corrupted meter 讀數（`3.36941e+07`）沒做 sanity check，讓方向判斷整個反過來。已加範圍檢查；`crane_abs_target_cmd_` 的方向/target 公式本身回推驗算過**是對的**，不要再去翻它。
8. **(07-15)** 退繩比原位還低 —— 退繩重試預算用固定 `step`，而不是「這側實際前進量」；已改用 `out_mv_cm`。
9. **(07-15)** VS Connection Manager 遠端主機顯示空白、編譯連不上 bench —— 根因是 `.vcxproj.user` 被 git 追蹤，不同人/不同 bench 網路的 Remote Target 互相覆蓋。

---

### 歷史摘要 #2（2026-06-02 ~ 2026-06-09）— realign 修復、安全/效能改動、camera motion parallax 驗證

> **規範權威：** `.claude/changelog.md` 2026-06-01g/h、2026-06-02a/b/c/m、2026-06-02t、2026-06-05k~o、2026-06-09a~b；`.claude/scripted_run_plan.md`；`.claude/camera_obstacle_plan.md`；`CLAUDE.md` §硬體架構（cli_22_ bus 擁塞）；memory `project_se3_07_10_two_options.md`（cli_22_ stale）。程式端安全註解：`WASH_ROBOT.cpp` 4 處 `[2026-06-02] SAFETY:`（body/feet pre_cycle）、`WASH_ROBOT.h:629`（JC100 1Hz cap）、`WASH_ROBOT.cpp:6196-6212`（realign invariant：Phase 2 stall = PausedOnError 強制人介入，但 in_window 路徑 caller 當 non-fatal log）。

**決策 / 被否決**
- 這期做了三批功能：Scripted run（`cmd_run_script <csv>` ＋ 5 個 saved-script 管理指令、持久化 `./scripts.json`）、Snowball 防護 A+B+C、Water inlet 防漏（retry 3 次 ＋ 5 分鐘 watchdog thread ＋ emergency/stop 兜底）。
- 🔮 釋放某組真空前先 `vacuum_check_` 另一組（anchor check），4 處 pre_cycle 都加；`cmd_recover` 的 `vacuum_check_` 取消註解、失敗回 `ERR recover_vacuum_fail` 並讓 state 留在 Error —— **刻意設計成嚴格**，後續 2026-07-23 特意沒動它。
- ❌ **否決**：DM2J:1,3 feet rail obstacle detect —— ROI 低，user 接受不做。
- ❌ **否決**：用「重啟背景 `pressure_poll_loop_`」解 GUI poll 轟炸 —— 該路徑 2026-05-29 已知有問題，改用 `cmd_status` 的 JC100 fresh-read 1Hz rate-limit。
- ❌ **否決（camera 路線）**：不再調 `obstacle_detector.py` 的 `--cam3/cam4` 單張模式（驗證過走不通，留著當 fallback）、不 retrain NPU model（bench 時間不花這）、不動主程式（user 明確要求）。
- 討論但未落地：BAL 應追求「兩繩同步收放」而非「等張力」（機體重心本來偏 L），kp 1.0 不夠可能要加 base offset。

**⚠ 踩坑 / 教訓**
1. **(06-01h)** realign Stage 0 的 JOG stall 原本是 FATAL，整個 realign 就死在那；改成 NON-FATAL（`emergency_stop` ＋ `release_stall_flag`）後 Stage A 才跑得完。實機 log 佐證：Stage 0 slave 4 peakI=1294mA、slave 2 peakI=2703mA 卻沒卡死，Stage A retract 完整跑完、4 顆 feet 全回 preset（29000~30000 pulse）。
2. **(06-05o)** Snowball 鏈：feet 的 `last_seal` 會自然往外長 → 越伸越多 → body 撞 end-stop。三段修法必須合起來看：A 記錄 seal pulse 時 `if (weak_seal[i]) continue;`、B `feet_max_overextend_cm_()` cap 4.5cm、C 新增 `feet_target_capped_()` cap 在 `preset + 5cm`。cap 後撞不到牆 → 判 WEAK_SEAL → A 不記錄 → realign 拉回 preset，鏈條才閉合。
3. **(06-02)** JC100 timeout 多的根因是 **cli_22_ bus 擁塞**，而 Web GUI 高頻 poll `cmd_status` 會直接轟炸它；加 1Hz fresh-read rate-limit。剩下的 timeout 來自 `disable_seal` 自己讀，不可避免。
4. **(06-02)** `ARM_CLEAN_WALL_MM=350` 時 tool「上貼下不貼」（pitch 偏），改 330 試 —— 若 330 還是不平貼，那就不是軟體問題，是 tool mount 物理裝歪要拆重裝。
5. **(06-03)** 純 OpenCV（不含 motion）做窗框偵測，**三輪全失敗** —— 反射 ＋ 雜物訊號太強。motion parallax 才驗證可行（plank 25→24cm 位移 1cm，conf 0.99、STOP_SHORT 19.1cm）。
6. **(06-03)** NPU model `yolov8s_window_640.hef` 對 bench 木條 **0 detection** —— 模型認的是「真實鋁窗框」，不是木條。要走 ML 路線得重訓。
7. **(06-03)** `frame_capture.py` 必須用 `stream=0` **主碼流** —— 子碼流被 camera 內部 ROI 裁過，畫面不是你以為的那個。
8. **(06-03)** bench 圖路徑含中文，`imread`/`imwrite` 會失敗，必須走 `imread_unicode` / `imwrite_unicode`。另註：既有 LUT 是用**單張木條 top edge y_px** 校的，而 motion 模式抓的是「motion 高的中心 y_px」，兩者未必是同一個點，LUT 不能想當然直接沿用。

---

### 歷史摘要 #3（2026-05-07 ~ 2026-05-08）— crane 端大重構、X518 架構錯位、graceful degradation

> **規範權威：** `CLAUDE.md` §架構圖（4 gateway、USR_C/.32、USR_D/.33 行）；`.claude/changelog.md` 2026-05-06m、05-06p、05-07a/b/c、05-08f、05-08j；`mailbox.md`「2026-05-08 DSZL-107 driver vs X518 三選一」；memory `project_x518_architecture_mismatch.md`、`project_deployment_state_2026_05_07.md`。

**決策 / 伏筆**
- crane 從「test mode ＋ easy crane shim」切回**正式 `Crane_control_PI` ＋ 全套硬體**：移除 ZS_DIO_R_RLY 繼電器改用 SE3-210 變頻器控左右繩；拓樸從 1 條 RS485 bus 改成 **4 個獨立 gateway**（每繩各一 ＋ DSZL 各佔一）；新增 hold-to-pull 指令 ＋ 後台 `hold_loop` 張力安全監看。washrobot 端同步 `CRANE_IP` 回 `192.168.1.101`、`WATCHDOG_TIMEOUT_MS` 60000 → 2000。
- **X518 三選一選了「路 B」**：`DSZL_107.{h,cpp}` 內部 framing 從 Modbus RTU+CRC16 改成 **Modbus TCP MBAP**，**public API 完全不動**，reply 重新封裝成 RTU-like layout 讓 caller 不用改；`USR_C_IP/USR_D_IP` 更名 `DSZL_LEFT_IP/DSZL_RIGHT_IP` ＋ 新增 `DSZL_PORT=502`。
- 🔮 **Graceful degradation 是刻意的架構**：12 個 init fail 全改 `[WARN] continuing` ＋ 12 個 device atomic flag（4 gateway ＋ 8 device），7 個 cmd handler 進場檢查所需 flag、缺則回 `ERR <device>_unavailable`，`cmd_status` 多回 `dev_*`/`gw_*` 欄位並 broadcast `EVT device_state`；GUI 按鈕靠 `data-required` 屬性自動灰化 ＋ 頂部中文 banner。**單一裝置不通時 crane 仍要能起來**，不要「修」成 FATAL。
- 部署測試順序照「9 步驟」走：status → kg 顯示 → 校零 → 個別 raw on/off 確認方向 → hold 按鈕 → 門檻自動停 → `motion_rope` → safety 觸發 → 接 washrobot。**不要直接跳 step 7。**

**⚠ 踩坑 / 教訓**
1. **(05-08)** crane 啟動直接 `[FATAL] connect USR_C 192.168.1.32:4001 failed (DSZL left)` —— bench 把兩台 X518 直插 switch 走 **Modbus TCP :502**，但 `Crane_control_PI/main.cpp` 假設 .32/.33 是 **USR-TCP232 gateway 在 :4001**。這不是程式 bug，是**規範文件（CLAUDE.md / motion_flow.md 架構圖）與實體佈線對不上**——架構圖錯了，程式照著錯的架構圖寫。
2. **(05-08)** DSZL 校零不持久：手冊規定校零後要寫 `0xA20 = 40`（SAVE）才落 EEPROM，driver 的 `do_zero_*` 沒有 follow-up SAVE → **每次 power-cycle 就掉 tare**。短期 workaround 是用 `Linux_test` menu 24 的 `S` 命令手動存一次。
3. **(05-07)** `await_user_intervention_` 巢狀 PausedOnError 會卡死，連 `cmd_continue`/`cmd_skip` 都失效 —— 第二次進入時覆寫了 `state_before_pause_`。已加 guard 不覆寫。

---

### 歷史摘要 #4（2026-04-23 ~ 2026-04-24）— DM2J driver 真相大白 ＋ Linux_test 大擴充與硬體實測

> **規範權威：** `.claude/summaries/DM2J_RS_MODBUS_SUMMARY.md`（2026-04-24 依原廠 `DM2J-RS.V1.pdf` V1.0 整篇重寫）；`.claude/changelog.md` 2026-04-22 / 04-23 約 45 筆條目；`.claude/easy_crane_test_mode.md` §9a（TEST MODE 撤除清單）；`.claude/camera_obstacle_plan.md`。

**決策 / 伏筆**
- 🔮 Menu 7 的 `dm2j_pair_rail_move` 改用**位置穩定偵測**（`dm2j_pair_poll_done`：每 150ms 讀位置、連續 3 次穩定且接近 target 就算完成）而非 status bit —— 跟 ZDT firmware quirks 的處理 pattern 相同，**刻意不依賴 bit layout 推論**。
- 🔮 所有 TEST MODE 改動一律在程式碼標 `[TEST MODE 2026-04-21]` 註解，撤除清單寫在 `easy_crane_test_mode.md §9a`，靠 `grep -rn "TEST MODE"` 找回來。
- ZDT slave ID 實機重新映射後，`WASH_ROBOT.h:119-123`、`CLAUDE.md` 架構圖、`motion_flow.md §4` 三處同步。

**⚠ 踩坑 / 教訓（DM2J 真相，2026-04-24）**
起因：menu 7 跑起來 **rails 物理上明明有走到目標位置**，卻一路回 `[ABORT] rail move timed out`，status register 永遠停在 `0x00320000`。順著這個 bug 讀原廠 V1.0 手冊（`pdftotext -enc UTF-8` 抽繁中文字）才發現**舊 summary 幾乎整篇是錯的**：
1. status register `0x1003` 是**單一 16-bit**，不是跨 `0x1003+0x1004` 的 32-bit。driver 讀 2 個 register 拼成 32-bit 後，完工檢查查的是 LOW word（`& 0x0010`），真值卻在 HIGH word → **所有 `PR_move_cm` 內部 poll 永遠 timeout**。
2. `0x00320000` 其實是 `0x0032` = bits 1+4+5 = **ENABLE + CMD_DONE + PATH_DONE = 動作已完成**；而舊 log 裡的 `0x00010000` 被誤判成 HOME_DONE，實際是 `0x0001` = bit 0 = **FAULT**。**過去所有 status log 的解讀都要重來一次。**
3. HOME_DONE 是 **bit 6（`0x0040`）**，不是 bit 16。
4. `0x1801` 控制字表整張錯：`0x1111` 是「**復位當前報警**」不是 enable、`0x2233` 是「恢復出廠值」不是 disable、存參數是 `0x2211` 不是 `0x2222`；軟體強制 enable 其實是寫 **`0x000F`（Pr0.07）= 1**。⚠ **這個錯之所以長期沒被抓到，是因為 DI1 出廠預設 SRV-ON 且為常閉 (NC)、上電本來就自動 enable** —— 送 `0x1111` 清掉 alarm 後馬達就會動，看起來像「enable 指令成功」，其實是巧合。
5. PR mode 欄位：`mode=0` 被 drive 視為「路徑未配置」→ **馬達完全不動、ENABLE 維持 0**（menu 7 那個 bug 的直接死因）；且舊 driver 註解寫「0=relative / 1=absolute」與手冊**相反**，實際是 `1=absolute / 2=relative`。

**⚠ 踩坑 / 教訓（bench 實測，2026-04-23）**

6. ZDT `pos_reached` bit **不可靠** —— 馬達物理已停但 bit 不 set。加三層 fallback：stall_flag / 速度回零（`|RPM| ≤ 20` 連 3 次）/ 位置不變（`|Δpos| ≤ 0.15°` 連 3 次）。
7. `trigger_sync_move()` 的 "send failure" 是 **Modbus 廣播（slave 0x00）的正常行為**，規範上就沒有 reply，driver 看 readEcho 空回 true 而已 —— 不是錯誤。
8. ZDT enable / pos_mode 偶發失敗 = **RS485-over-TCP gateway 的 frame 對齊問題**。加 per-slave 3 次 retry ＋ back-off ＋ 跳過失敗 slave 不中斷整個群組。
9. Staged extend（先伸一半 → 停 1 秒 → 再伸全段）可避免吸盤接觸衝擊；**stage 2 必須無條件執行**，stage 1 timeout 不能 short-circuit 掉它。
10. **Valve-before-extend 比 extend-before-valve 穩** —— 吸盤碰牆瞬間已經有負壓，立即 seal。
11. PQW relay 模組回應格式異常：TX `0C 05 00 00 FF 00 ...` 卻收到 RX `0C 00 00 00 FE 00 ...`（function code `0x00` 非標準）。懷疑是 gateway 的 Modbus-TCP↔RTU 模式設錯，或 PQW 韌體本身非標準；當時只能把 `Linux_test` 選項 5 的語意改成「`[SENT]` 請自己看 LED」。
12. DM2J slave 3/5 **沒有 ENABLE 位元**，status 只有 HOME_DONE —— 需要 `motor_enable()` 或硬體 dip switch 設 auto-enable。
13. **Modbus RTU over TCP gateway 連續下指令必須留 delay**，否則 TCP buffer 殘留的 echo 會干擾下一次 read。
14. **SMC LEYG25 pusher 的 pulse/cm 不是原推算的 7200/cm**：實測腳組 20000 pulses ≈ 7cm（~2857/cm）、身體組 30000 pulses ≈ 10cm（~3000/cm）；而 144000 pulses 實際**不是** 20cm（可能 >30cm 或已打到實體止動）。
15. ZDT slave ID 與原本假設不符：feet 是左 3,4 / 右 1,2（**不是** 1,2 / 5,6）；body 是左 6,8 / 右 5,7（**不是** 3,4 / 7,8）；center 9 不變。

---

### 歷史摘要 #5（2026-04-20 ~ 2026-04-21）— Crane_easy_PI ＋ crane_shim ＋ Web GUI 一連串改版

> **規範權威：** `.claude/easy_crane_test_mode.md`（測試模式權威，含 §9 撤除清單）；`.claude/motion_flow.md` §4 Phase 2/Phase 3、§6 可調參數表、§8 系統通訊架構（失聯模式 UI ＋ 緊急收繩按鈕 ＋ 指令協定）；`.claude/runbook.md` §A / C2b；`crane_shim/README.md`。

**決策 / 被否決**
- ❌ **crane_shim 的兩條替代路線都被否決**（重要，別再提）：① 加 `CRANE_MOCK` flag 讓 washrobot 跳過 crane —— **違反 motion_flow §8 的失聯安全鎖**；② 改 washrobot 直接講 easy crane 協定 —— **破壞協定權威**。最後選的是「shim 層」：一支跑在 crane Pi 的 Python 程式偽裝成 `Crane_control_PI` 監聽 :5002，把 `pay_out <cm>` 翻譯成 easy 的 `down on → sleep(cm/rate) → down off`，**washrobot / web_backend / Crane_easy_PI 三邊都不用改**。
- 🔮 shim 刻意讓 `ping` **不經 easy**（自己直接回），避免 washrobot 的 2s ping timeout 誤觸 crane_watchdog；`home_status` 回 `ERR shim_no_home_use_manual_easy_crane`、`roll_correct` 回 `ERR shim_no_roll_correct`，**刻意擋掉** Phase 6 自動召回與 Phase 5 平衡校正（測試模式下要手動）。
- ❌ **04-20h 的 HOLD/AUTO「模式切換」設計在 04-21g 被整個廢除**，改成 UP/DOWN 純 press-and-hold ＋ AUTO 獨立單鍵（click 1 = `up on`，click 2 或 `EVT weight_limit` = 停）。不要再回頭提 mode toggle。
- ❌ **04-20h 移除 500ms `ping` heartbeat（理由：與 50ms status poll 重複）是錯的決定**，04-21d 打臉後由 backend 自己每 10s 送 `ping` 補回來（見下方教訓 5）。
- 緊急收繩按鈕定為 **press-and-hold**（mousedown 送 `retract_left/right on`，放開送 off ＋ 補一次 `stop`），設計理由是**防誤觸**；失聯模式切入時自動送 `stop`（crane）或 `emergency_stop`（washrobot）。
- 收輪步驟放在 **Phase 2**（不是 Phase 1 末尾），實作在 `cmd_init()` 推桿伸出之前 —— 理由是單一 entry point ＋ 防呆（Phase 1 忘了收輪也會自動處理）。❌ Phase 6 召回**不需**放輪：輪子在牆面側，落地沒有緩衝作用。
- `Crane_easy_PI` 三層防呆：server watchdog（`motion_active` 且 >2000ms 無 inbound → `all_off`）／重量門檻（UP 過程低於 `up_stop_kg` 持續 SUSTAIN → `all_off`）／前端 press-and-hold ＋ 心跳。後加第 4 層：DY500 連續讀失敗 >500ms → `weight_valid=false`，且 `cmd_up`/`cmd_down` 進場 pre-flight 擋掉。

**⚠ 踩坑 / 教訓**
1. **(04-21i)** web_backend 被 OOM killer 幹掉，`ss -tnp` 看到 fd 衝到 **87787**、滿滿 SYN-SENT。根因：Node.js socket 失敗會**同時** fire `error` **與** `close`，兩個 handler 都呼叫 `onClose` 都 `setTimeout(connect, 3000)` → 第 N 輪排 2^N 個 reconnect。修法：`error` 只 log 不觸發重連，讓 `close`（Node 保證 error 後必跟 close）單獨驅動；加 `state.reconnectTimer` 去重；`connect()` 進場先 `destroy()` 舊 socket 防 fd leak。**引爆條件**是啟動時沒帶 `CRANE_IP`/`EASY_CRANE_IP` env var，兩個 bridge 同時連向不存在的 target。
2. **(04-21g)** easy crane 按鈕狀態 race：原本 `if (serverUp !== easyUpActive)` 的**雙向** state sync，會在「client 剛按下、server 還沒回報」的 race window 把 local state 誤重置成 false。改成**單向** —— 只有 server 清掉時才重置 local，且要連 `easyUpActive` 和 `easyAutoActive` 一起重置（兩者驅動同一個物理 relay）。
3. **(04-21f)** 04-21b 的深空極光主題在 **Pi Chromium 上卡**。根因排序：每個 panel 的 `backdrop-filter: blur(16px) saturate(140%)` 是最大殺手，其次是兩顆 `filter: blur(90px)` 的 aurora blob 無限動畫、banner pulse、按鈕 hover `box-shadow` glow、log 每行 `text-shadow`（debug=true 時 log 量大會累積重繪）。⚠ 04-21b 當時就把「Aurora blob 在 Pi 上會不會過重」列為待驗證項，結果成真 —— **Pi 上不要用 `backdrop-filter`**。
4. **(04-21d)** easy crane 閒置一陣子就掉線（`Crane_easy_PI` 其實還活著）：`makeBridge()` 的 socket 沒開 `setKeepAlive`，而 easy crane 在 `.5.26` 跨網段（`.1.x` ↔ `.5.x`），中間 router/NAT 閒置 15~60 分鐘會偷殺 TCP session。backend 的 `state.connected` 仍是 true，**直到下一次 write 才 RST**。
5. **(04-21d)** 同一條的另一半：backend 原本靠 app.js `setInterval(..., 50ms)` 的 status poll 當隱性心跳 —— 但**瀏覽器 tab 背景化時 setInterval 會被 throttle 到 1s+ 甚至完全停**，瀏覽器關掉更不用說。**後端存活不可以依賴前端活動**，已改成 backend 自己每 10s 對每個 bridge 送 `ping`（`BRIDGE_PING_MS`）。
6. **(04-21c)** 上機前 code review 抓到的 blocker：`WATCHDOG_TIMEOUT_MS = 2000` 太短 —— shim 的 `pay_out 45cm @ 3cm/s = 15s` 會把 `crane_mtx_` 鎖住 15 秒 → watchdog 看 elapsed > 2s 且 `motion_active_` → 自動 `abort_flag = true` → **`step_down` 每次都 mid-motion abort**。測試期暫調 60000ms。副作用預警：同批把所有驅動 debug 打開會噴大量 Modbus hex dump 淹掉 terminal 與 GUI log。
7. **(04-20c)** 原 HTML 的 `STOP (robot)` 按鈕送的是 `stop`，但 **washrobot 根本不支援這個指令**，要送 `emergency_stop`。
8. **(04-20i/j/k)** easy crane 停機延遲追了三輪才壓下來，每一輪的假設都只對一半：① 最初安全檢查吃的是 **10 樣本平均 `g_weight`（落後 500ms）** ＋ `WEIGHT_SUSTAIN_MS=300`，最差 ~800ms → 改用 raw 單次讀值、SUSTAIN 降 100；② 仍不夠快，因為 `WEIGHT_POLL_MS=50ms` 的 **sleep 佔掉一半以上循環時間**，且 sustain 計時原本**假設「固定 50ms 一格」而錯估** → 移除 sleep 改 1ms yield、改用 `steady_clock` 實測累計；③ 最後 `SUSTAIN=0` ＋ `all_off()` 用 `atomic::exchange` 跳過已 OFF 的繼電器寫入。~800ms → **~30-50ms**，剩下的是 Modbus RTT 物理極限（要再快只能動 DY500 driver 的 400ms recv timeout，或拿掉 TCP gateway 改 Pi 直連 USB→RS485）。

**🟡 仍掛著、未進上方待辦總表的項目**
- `Crane_easy_PI` 的 `WEIGHT_UP_STOP_KG = -20.0f` 是 placeholder，說好上機測實際卡住時的張力值再調。
- crane_shim 的 `--rate-down/--rate-up = 3.0 cm/s` 是佔位值，說好上機實測校正（`STEP_MARGIN_CM=15` 只吃得下 ±50% 誤差）。
- TEST MODE 撤除清單：`CRANE_IP` 與 `WATCHDOG_TIMEOUT_MS` 已於 2026-05-07 還原，但**各驅動的 `debug=true` 是否全部還原沒有紀錄**，要 `grep -rn "TEST MODE"` 再確認一次。

---

### 歷史摘要 #6（2026-04-13 ~ 2026-04-17）— 規格定稿、CLV900 驅動、Crane_control_PI 重寫、協作機制

> **規範權威：** `.claude/motion_flow.md`（§2 硬體表／§4 Phase 1~6／§6 可調參數／§8 網路拓撲）；`.claude/summaries/CLV900_INVERTER_MODBUS_SUMMARY.md`；`CLAUDE.md`（架構圖、Log 格式規範、分散式通訊段、多人協作紀律）；`user_lib/log_utils.h` 檔頭；`deploy_and_test.pdf` Gate 7~11。

**決策 / 被否決**
- **Web Backend 從 washrobot (.100) 搬到 crane (.101)** —— 理由：washrobot 是高風險側（控制吸附/下移），GUI 與它同台的話 washrobot 一掛就**失去所有遠端控制能力，機體懸吊半空無法救援**；搬到 crane 側後即使 washrobot 全失聯，操作員仍能透過 GUI → crane 手動收繩回收機體。程式碼影響為零（`server.js` 本來就走環境變數連線）。
- **IMU 只監控 Roll ＋ Pitch，不監控 Yaw** —— 貼牆不自轉，且磁力計會漂移。Phase 5 也**只校正 Roll**（吊機左右鋼索差動），Pitch 不自動校正。兩級門檻：>15° 當前 step 完成後暫停並 `EVT balance_ask` 問使用者；>45° 不問，直接停機 ＋ crane stop ＋ 人工處理。共通規則是 1s 滑動平均 ＋ 持續 500ms 超標才觸發。
- 中間絞盤同步採 **C 案**（中間放繩 cm = 左右放繩 cm × `MIDDLE_WINCH_RATIO_K`，預設 1.00）；左右鋼索絞盤仍由 ZS_DIO_R_RLY CH1~4 控制，**不經變頻器**。
- ❌ **明確不做的四件事（2026-04-14 架構決策）**：時間同步（watchdog 即時控制不需要，事後日誌誤差可接受）、**UPS / 雷擊 / 漏電保護**、Web GUI 認證、日誌集中化（先存本地 SD 卡）。⚠ 其中「漏電保護」已在 2026-08-27 的新架構設計中**被推翻**——新架構是帶水作業且設備上有 220V AC，RCD 列為必要非選配（見上方新架構待辦表）。
- ❌ DY-500 重量感測器硬體有問題，**確認暫不啟用**（規格保留）；❌ 機械手臂 USB→CAN **本版不整合**，保留未來擴充。
- 🔮 安全性事實：絞盤斷電為**自動剎車**（電磁剎車失電夾持），DM2J 步進失電**鎖死** —— 所以「斷電即脫離」原則下，機器人脫牆後是由剎車懸吊，不會墜落也不會繼續下滑。
- 🔮 **Log 格式規範**：`user_lib/log_utils.h` 的 4 個 `LOG_*` 巨集 ＋ `LOG_HEX`，格式 `[HH:MM:SS.mmm] [LEVEL] [DEVICE:ID] <msg>`；**所有 level 統一由 `debug_mode` 成員控制**（關掉完全靜默，錯誤靠 bool return 通知呼叫端）；輸出到 stderr、不落檔、不加鎖（輕量 A 方案）。14 個驅動全改造，禁用 printf/cout/cerr。⚠ **`DIHOOL_control` 與 `QX_DO24` 刻意維持 inverted convention（true=success），與全專案 false=success 相反 —— 這是有意保留的例外，不要「順手改正」。**
- 多人協作機制（角色表 ／ `user_lib/*.h` public API 為介面契約、跨界 PR 標 `[跨界: user_lib]` ／ `.claude/mailbox.md` 協作信箱 ／ 開 session 三步驟）—— ⚠ **此機制已於 2026-08-27 整個退休**（改單人開發，mailbox 改為墓碑），歷史條目裡的「等 Jim review」「屬 Sadie 範圍」等分工字樣一律作廢。

**⚠ 踩坑 / 教訓**
1. **(04-17)** 一度以為現況是「washrobot 當 server、crane 當 client」，想翻轉成 crane server 以利救援 —— 實際翻代碼確認**現況早就是想要的架構**：washrobot `:5001` 給 Web Backend 連、crane `:5002` 同時接 Web Backend 與 washrobot 兩個 client，washrobot 掛掉時 Web Backend → crane 的救援路徑完全不經 washrobot。**結論：一行都不用改。此條純備忘，就是為了避免未來又誤會一次。**
2. **(04-14)** 水系統流向搞反過：CH7 是**水箱進水**球閥，不是出水。正確流向是 頂樓水源 → CH7 → 10L 水箱 → CH6 泵浦 → 機械臂噴頭 → 牆面。Phase 4-C 的「清洗時 CH7/CH6 同時 ON」邏輯不變，但 CH7 的意義變了（補水而非出水）。
3. **(04-13 S3)** CLV900 手冊靠 **OCR 視覺比對**（PyMuPDF @ 2.5x 逐頁看）才抓到純文字抽取漏掉的 **`F7-19` MODBUS 數據通訊格式**（0=標準／1=非標準，driver 假設 0）與 `F7-20` 兼容旗標 —— 表格類內容不能只信 pdftotext。
4. **(04-15)** `Crane_control_PI.vcxproj` 的 include 路徑硬編成 `C:\Users\Administrator\...`，換一台機器就編不起來；已改相對路徑 `..\user_lib`。

**🟡 仍掛著、未進上方待辦總表的項目**
- 🟡 **水箱溢流處理（Open Q10）** —— 浮球閥／溢流孔／軟體控制三選一，從 2026-04-14 開到現在沒結論。
- 🟡 **Fathom-X 100m 拔插 ／ 長時間穩定度實測（Open Q11）** —— 列為實機 Gate 項目，從未執行。
- 🟡 IMU serial port 上機確認（`/dev/ttyUSB0` 當時只是暫定值）。
- 🟡 CLV900 正反轉方向 wiring-dependent，實機若反向需翻轉（`MIDDLE_WINCH_HZ=20` 與 rpm→Hz 換算已併入待辦總表的 crane placeholder 那列）。

---

### 歷史摘要 #7（2026-04-10 ~ 2026-04-12）— user_lib 全驅動審查、初版分散式架構、首次遠端編譯

> **規範權威：** `.claude/motion_flow.md`（初版：Phase 1~5、硬體對照、可調參數表）；`CLAUDE.md`（架構圖、分散式系統通訊章節）；`.claude/summaries/` 下各驅動摘要（ZDT / DM2J / JC-100 / DY500 / SD76 / PQW / ZS_DIO）；`deploy_and_test.pdf` Gate 0~6 ＋ 產生器 `.claude/gen_deploy_pdf.py`。

**決策**
- 分散式架構定案：吊機 RPi `192.168.1.101` TCP server `:5002`、洗窗 RPi `192.168.1.100` TCP server `:5001`、Web Backend（Node.js）橋接 WebSocket ↔ 兩台 TCP；協定為行為單位的文字指令，回 `OK` / `ERR` / `EVT`。
- 真空閥值取 **-50 kPa**（用最差值當門檻）；cycle_group 樣板為 valve OFF → pusher retract → displace → pusher extend → valve ON → vacuum verify，失敗自動重試 5 次。
- **全 `user_lib` 統一 `false = success`**（`PQW` / `ZS_DIO` 介面也統一成 `init`/`controlRelay`/`controlAll`/`readAllStatus`/`close` 同簽名，可互換）。
- 🔮 `PR_move_cm_nowait` / `PR_move_cm_set` 內硬編碼 `PPR=10000` **刻意不改**（效能考量）—— 不是漏改。

**⚠ 踩坑 / 教訓**
1. **回傳值慣例翻轉極容易漏改**：ZDT 第一輪只改了 `motion_control_speed_mode` 與 `pos_mode_nowait`，其餘 **13 個函式全部漏改**（init ×2、set_zero、calibrate_encoder、reset_motor、driver_EN、get_system_status、wait_until_pos_reached、release_stall_flag、emergency_stop、factory_reset、trigger_home、abort_home、trigger_sync_move、set_home_zero_position），隔一個 session 才補齊，連帶呼叫端也要一起翻。批次改慣例時**必須逐檔 grep 收尾**。
2. **DY_500 造成的真 bug**：`Crane_control_PI/main.cpp` 的 `get_weight_float` 呼叫端**已經照 false=success 寫**，但驅動當時回傳相反 —— 整個重量讀取邏輯是反的。翻正驅動後呼叫端自動變對。
3. **PQW 兩個記憶體地雷**：constructor 沒把 `client` 初始化為 `nullptr`，解構函式無條件存取未初始化指標；`init` 的 Mode A 直接用 `client->connectToServer` 卻**沒有先 `new TCP_client()`**。已加 null check ＋ `owns_client` flag ＋ destructor delete。
4. **SD76 `decodeSignedBCD6` 從 byte[0] 的 bit7 取負號，文件完全沒記載這個用法**，推測是廠商慣例 —— 這類「只有程式碼知道、手冊沒寫」的行為要標出來，否則下一個人會以為是 bug 而改掉。
5. **DM2J `set_jog_dec` 的 header 註解位址寫錯**（`0x01E8` 應為 `0x01E7`，與 acc 共用 Pr6.03）。
6. **中文 PDF 抽不出文字**（ZS_DIO、DM2J 等），改用 PyMuPDF 轉圖片後**視覺閱讀**，並回頭 OCR 複驗全部摘要，改掉了 SD76（TIA1/TIA2 唯讀、AL1/AL2 需 FC 0x10）／PQW（模式值、Input Register 映射、看門狗公式）／DM2J／ZDT（Microstep 暫存器 `0x0084` → `0x00B4`、mode 欄位移除不存在的 0x02）四份摘要。⚠ **但這輪 OCR 複驗自己也錯了一項**：把 DM2J 的 HOME_DONE 從 Bit6「修正」成 Bit16，並據此在同一天把 `read_status`（1 register → 2 registers）、`print_status`（`0x0040` → `0x10000`）以及 `motor_enable/disable/save_params` 三個新函式**通通改壞**，直到 2026-04-24 拿原廠 V1.0 手冊重讀才翻案回 Bit6。**教訓：視覺 OCR 複驗不是終點，原廠手冊的版本與出處才是；而且「照著錯的摘要做修正」比不修正更危險。**
7. **ZS_DIO driver 原本 `init` 簽名沒有 ID 參數、硬寫 `slave_id = 0x01`**，且控制函式用硬等 delay 而不是等 echo；重寫後對齊其他驅動（加 ID 參數、`sendAndReceive` 等 echo、回傳 bool ＋ 重試 3 次）。

**🟡 仍掛著、未進上方待辦總表的項目**
- 🟡 2026-04-12 列的參數實測微調清單：`TOTAL_DISTANCE_CM` / `ARM_SWEEP_CM` / `ARM_SWEEP_RPM` / `PUSHER_EXTEND_PULSE`（其中 pusher pulse 已於 2026-04-23 實測，其餘三項沒有後續紀錄）。
