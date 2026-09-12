# ARCHITECTURE.md — facade_cleaning_v2 軟體架構

> 📌 **這是持續維護的權威架構文件**(取代 2026-08-13 快照的 `ONBOARDING.md`)。
> 程式碼與 git log 若與此不符,以程式碼為準,並回頭更新本檔。
> 首版:2026-09-12,基線 commit `bbbc425`。日常進度在 `.claude/work_log.md`。

---

## 1. 系統總覽

高空外牆清洗機器人 v2,吊掛式(鋼索由吊機升降),機身以四顆真空吸盤貼附玻璃、手臂
(滾筒/刮刀)配合上滑台掃動清潔。**四個獨立程序 + 一支編排腳本**,以 TCP 行協定互連。

```
                 ┌──────── Web GUI  web_backend/server.js (Node) ────────┐
                 │  :8080 v1(public/,停機)   :8081 v2(public_v2/,含安全閘門)   │
                 │  純 TCP↔WebSocket 橋接,不含業務邏輯                            │
                 └──────┬───────────────────┬───────────────────┬─────────┘
                        │                   │                   │
   ┌────────────────────▼──┐   ┌────────────▼──────────┐   ┌────▼────────────────┐
   │ 本體 Body             │   │ 吊機 Crane             │   │ 手臂 Arm             │
   │ facade_cleaning_v2/   │◄──┤ Crane_control_PI/      │   │ cleaning_arm/        │
   │ app/WASH_ROBOT (class)│TCP│ main.cpp (程序式單體)   │   │ motor_api (class)    │
   │ command/dispatcher    │   │ TCP :5002              │   │ Damiao M1/M2 (CAN)   │
   │ TCP :5001             │   │ 吊機 Pi user@.25        │   │ TCP :9527            │
   │ 本體 Pi nexuni@.26     │   └────────────┬───────────┘   │ (跑在本體 Pi)         │
   └──────────┬────────────┘                │               └──────────────────────┘
              │ Modbus-TCP / serial(user_lib/ 驅動,經 transport/)
   ┌──────────▼────────────────────────────▼─────────────────────────────────────┐
   │ 本體側 USR 閘道:.20(ZDT 推桿/PQW 繼電/DM2J 滑台) .21(PWM) .22(JC100 壓力/XKC/DM2J:14)│
   │ 吊機側 USR 閘道:.30(SE3 左變頻) .31(SE3 右) .34(SD76 計米) .32(ZS-DIO 水閥/X518 張力) │
   │ 序列:WT901 IMU(本體 /dev/ttyUSB0)、Damiao 馬達(手臂 /dev/ttyACM0)                 │
   └───────────────────────────────────────────────────────────────────────────────┘

   編排/上機測試:Linux_test/cycle_test.py(full / arm / mission 模式,走 TCP 打三程序)
   離線測試:    harness/(fake_bus / fake_serial 假匯流排 + 回放比對)
   相機(停用):  frame_capture/(Python 管線)+ user_lib/FrameAnalyzer(本體內樁)
```

### 1.1 程序 / 主機 / 埠

| 程序 | 主機 | 埠 | 進入點 | 啟動方式 |
|---|---|---|---|---|
| 本體 | nexuni@192.168.5.26 (WiFi) / 192.168.1.100 (隧道) | 5001 | `facade_cleaning_v2/main.cpp` | `~/run/start_body.sh <tag>`(FIFO) |
| 吊機 | user@192.168.5.25 / 192.168.1.10 | 5002 | `Crane_control_PI/main.cpp` | `~/run/start_crane.sh <tag> <home_ground>`(FIFO) |
| 手臂 | 本體 Pi | 9527 | `cleaning_arm/main.cpp` | `cd cleaning_arm && ./motor_api`(FIFO) |
| Web | 吊機 Pi | 8080/8081 | `web_backend/server.js` | `~/run/start_web.sh <tag>` |

🔴 **啟動用 FIFO**:各程序 stdin 接 `~/run/<tag>_in` FIFO(`sleep infinity` 撐開),本機 console
只認 `exit/quit/status`,**其餘指令一律走 TCP**。本體 `exit` 會跑 `cmd_shutdown` → **關全部繼電器**。
tmux 未安裝,`wr.sh` 不可用,用 `scripts/bench/launch.sh` 或 start_*.sh。

### 1.2 通訊協定

行協定,回覆 `OK [data]` / `ERR <reason>` / `EVT <type> <data>`(非同步事件,GUI 靠前綴分流)。
指令前綴:本體小寫 `arm_deploy_f 8 RIGHT`;手臂原生大寫 `DEPLOY_F 8 RIGHT`(本體代轉時補送
`M1 ENABLE`/`M2 ENABLE`)。

### 1.3 端點覆寫(隧道 ↔ WiFi)

`common/endpoints.h`:任何連線可用 `FCV_EP_<NAME>_HOST` / `_PORT` 環境變數在**執行期**改主機
(例 `FCV_EP_CRANE_HOST=192.168.5.25`)。
📌 現況(2026-09-12):**暫全走 WiFi**(測試用,測完改回隧道 192.168.1)—— 本體
`FCV_EP_CRANE_HOST=192.168.5.25`、web `WROBOT_IP=192.168.5.26`。**但 start_*.sh 把隧道 IP 寫死**,
切換要手改 → 見 §8 調整建議。

---

## 2. 本體(Body)— `WashRobot`

**規模**:`WASH_ROBOT.h` 3091 行 + `WASH_ROBOT.cpp` 5587 + `wash_robot_commands.cpp` + `dispatcher.cpp` 554;
**102 個 `cmd_` 方法、~115 條指令**。一個 class 扛全部。`main.cpp`(149 行)只做 TCP 伺服 + 快/慢
路徑分派(`is_fast` 同步、其餘丟 worker 執行緒),乾淨。

### 2.1 職責拆解

| 子系統 | 職責 | 驅動 / 連線 | 背景執行緒 | `.cpp` 區段 |
|---|---|---|---|---|
| 生命週期 | init / 連線 / settings.json / shutdown | cli_20/21/22 三條 USR | — | init |
| **真空/推桿(腳)** | 伸縮、封合狀態機(Phase1/2、Step A–F)、補伸 | `zdt_[9]`、`meter_[9]`(JC100)、`pqw_` 閥 | — | pusher/vacuum(最大塊) |
| 手臂代轉 | DEPLOY_F/park/retract → :9527 | `arm_cli_` | — | cleaning arm |
| 滑台掃 | DM2J:14 清潔掃動 / jog / zero | `dm2j_[4]`(rail) | rail_jog_mon、rail_sweep(detached) | cleaning sweep |
| 吊機協調 | watchdog / estop 旁路 / IMU 推送 | `crane_cli_` ×3(cmd/estop/imu) | crane_wd、crane_keepalive | crane |
| IMU | 讀取/監看/推給吊機平衡 | `imu_`(WT901) | imu_mon、imu_push | IMU |
| 水 | 進水閥 deadman、幫浦 A/B 輪替、刷、水位 | `pqw_`、XKC | water_inlet_watchdog、pump_rotate | — |
| 風扇/PWM | | `pwm_`(QX_DO24) | — | — |
| 編排/步態 | step / run / script / cross_obstacle | (全部) | — | — |
| 設定 | 執行期 tune | settings.json | — | — |

### 2.2 關鍵行為

- **真空封合**:extend → 等真空(至少一顆 ≤ VAC_OK)→ 清潔 → retract。吸不到**不中止**
  (現場條件),跳過掃動續行。`vacuum_wait_release_` 容忍 JC100 讀取失敗(cup7 感測器間歇)。
- **ZDT 推桿**:3000 pulse = 1 cm;extend 30000(10 cm)、retract 300(0.1 cm,避撞硬限位)。
  🔴 **24V blip 會打亂驅動器位置計數器** → `zdt_home feet`(自探方向退硬限位再 set_zero)
  或 `zdt_power off` 手推 + `zdt_zero`。`zdt_disable` 只是排除群組,**不失能**。
- **手臂代轉**:本體 `arm_deploy_f <nm> <slot>` → 補 ENABLE → 送手臂 `DEPLOY_F`。
  🔴 target 由**呼叫端顯式傳值**(cycle_test 的 `ARM_TARGET_NM`),手臂 default 只是備援。

### 2.3 已知問題

- 🔴 **v1 遺留死碼**:指令面含大量 v1 走行機器人殘留(`wheels`/`cross_obstacle_*`/
  `step_*_sweep_ba`/`run_avoid`/`run_depth_avoid`/`attach`/`detach`/sync-alt 步態),v2 吊掛式多半用不到。
- `crane_cli_` 三條連線分散;`FrameAnalyzer` 相機樁未整合(`obstacle_detect` 旗標預設 OFF)。

---

## 3. 吊機(Crane)— `Crane_control_PI/main.cpp`

**規模**:5697 行,**純程序式單體**(全部 static 全域 + 函式,無 class)。

### 3.1 裝置 / 連線

| 連線 | 裝置 |
|---|---|
| cli_A (.30) | SE3 左變頻 slave1(+ 中繩 CLV900 slave3 **未裝**) |
| cli_B (.31) | SE3 右變頻 slave2 |
| cli_M (.34) | SD76 計米 左1 / 右2(+ 中4 **未裝**) |
| cli_C (.32) | X518 DSZL 張力 左CH2 / 右CH1(直連 :502) |
| cli_W (.32) | ZS-DIO 進水閥(獨佔) |
| cli_D | **已退役**(2026-09-01) |

**5 條背景執行緒**:watchdog(motion 逾時)、hold(按住式 + 張力安全)、meter(計米快取
250/100 ms)、water_dog(進水閥 deadman 300 s)、vfd_keepalive(SE3 1 s)。

### 3.2 職責拆解

| 子系統 | 行 | 內容 |
|---|---|---|
| 硬體設定 / 全域 | 168–766 | IP、slave、常數、全域狀態 |
| **平衡 ×2** | 767–1170 | 計米差回授(舊)+ **IMU roll 驅動(現行預設)**;IMU 過期(>750 ms)自動退回計米 |
| 低階 | 1189 | VFD run/stop、L/R 並行 |
| 安全 | 1771–1919 | watchdog、張力(過載 / 差值 / 上限 / 收繩停止) |
| hold 模式 | 1920 | 按住式 up/down(GUI) |
| **pay_out / retract** | 2463 | 主運動:雙繩 + 中繩管線 |
| roll_correct / align_lengths | 3298–3560 | 差動修 roll、對齊長邊 |
| 計米校正 | 4501 | SD76 SCAL/DP EEPROM |
| dispatcher | 4892 | ~60 指令 |

### 3.3 座標慣例(🔴 易錯)

`length_left` 為 SD76 讀值。`height = -length_left`(**地歸零**:`home_ground_cm=0`,底端 0、往上為負);
`zero_meters ground` 在地面歸零,`set_home_ground 0`。頂端讀約 −260 = 牆高 260 cm。
**home_ground_cm 等執行期參數重啟即失**,`start_crane.sh` 用 `crcmd.py` 重打,
**預設 HOME_GROUND=256 是舊頂零慣例,地歸零要傳 0**。牆高存 `~/run/wall_height.json`(web)。

### 3.4 已知問題

- 🔴 執行期參數(home_ground / motion_hz / balance_source…)**不持久**。
- 未裝硬體(中繩 CLV900、meter_middle)與退役 cli_D 的碼仍在;`set_*` 調參指令 ~30 條。

---

## 4. 手臂(Arm)— `cleaning_arm` / `DamiaoAPI`

**規模**:`main_api.cpp` 3891 + `main_api.h` 749;`main.cpp` 102(薄)。三元件中**結構最好**。

### 4.1 結構

| 層 | 內容 |
|---|---|
| 設定 | `damiao.cfg`(port /dev/ttyACM0、baud 921600、:9527、M1 DM10010L 0x01、M2 DM4340_48V 0x02)**改了免重編** |
| 馬達槽 | `m1_`(大,抬升/下壓 θ)、`m2_`(小,工具 L/R 旋轉;槽位 RIGHT 滾筒 0.8558 / LEFT 刮刀 0.1913) |
| 低階 slot | enable/disable/set_zero/hold/move_to/go_home(park 剖面)/lr_calibrate/calibrate_arm/lr_move_to_slot |
| **feedback_loop** | M1 控制迴路:重力前饋 `20.87·sin(θ−3.317)`、積分箝制、hold、**過速安全煞車**(vel > 0.4 → kd=5) |
| TCP | server_loop / client_thread / dispatch |
| 高階序列 | INIT、STARTUP(自動:M1 回零 + M2 滾筒)、**DEPLOY_F(力控,現行)**、DEPLOY(距離式,舊)、PARK、STATUS |
| 壓牆原語 | `prepare_touch_slot_`、`press_probe_`、`press_hold_step_` |

**指令面 7 條**:`DEPLOY_F <nm> <slot> [θmin] [θmax]` / `DEPLOY` / `INIT` / `PARK` / `STATUS` / `M1 <sub>` / `M2 <sub>`。

### 4.2 幾何與力控

牆距 mm = `490·sin(θ − 0.38) + 121`(三點擬合,`ARM_LENGTH_MM`=490、`VERTICAL_OFFSET_RAD`=0.38)。
θ 上限 1.10 → 最大 reach ~444 mm;正常玻璃 216–253 mm(θ 0.58–0.65)。

**DEPLOY_F 流程(2026-09-11 定案)**:退到起點 0.40 → 粗掃 0.030 rad/步 到輕觸 2 Nm →
(守衛 A:走到 θ_max 仍沒觸 = `no_wall`)→ 粗壓到 `COARSE_NM` → 割線收斂到 `TARGET_NM`
(需要 θ 超上限 = `cannot reach`)。**現行 TARGET 8 / COARSE 6 / TOUCH 2 Nm**。

🔴 **橫桿(crossbar)問題定案 — 不偵測,低力刷過**:M1 訊號(接觸 θ、低負載剛度)實測皆分不出
橫桿與玻璃(θ 0.55–0.60 vs 0.58–0.59 重疊;剛度皆 9.8,工具柔度主導;長滾筒偏心壓桿會
pivot)。已移除細掃/th_min/剛度守衛。8 Nm 雙工具 + 滑台 0–100–0 掃實測**零 24V blip**;
15 Nm 壓桿堵轉過流就是 blip 根因。

### 4.3 已知問題

- 死檔 `PalletizerController.h`(352 行,0 引用);死常數 `DEPLOY_F_FINE_*`/`STIFF_*`/`THETA_MIN`。
- 舊 `DEPLOY`(距離式)只剩 cycle_test 對「本體舊 binary」的 fallback。
- `kp_eff` 快取:`iters=0` 時為舊值;M1 從遠處 retract 會觸發過速煞車(pos 仍收到 0,無害)。
- 力控參數是 `constexpr`,現場調力要重編。

---

## 5. 週邊

| 目錄 | 角色 |
|---|---|
| `web_backend/` | GUI 橋接。**v2 (public_v2) 有安全閘門**(手臂武裝互鎖 + 頁面級「危險操作」閘門,不靠 window.confirm;PARK/STOP/急停/收回/繼電器/按住式不鎖)。v1 停機。⚠️ 本機目錄 `web_backend/`,**Pi 上佈署為 `.../web/`**(start_web.sh 寫死) |
| `Linux_test/` | `cycle_test.py`(full/arm/mission;`FCV_WROBOT_HOST`/`FCV_CRANE_HOST`/`FCV_TOP_CM`/`FCV_ARM_NM`;高度帶 `FCV_SKIP_BANDS` opt-in 已放棄)+ 各上機腳本 |
| `user_lib/` | 17 支裝置驅動(ZDT、JC100、SD76、SE3/MH300/CLV900、DSZL、PQW、ZS-DIO、QX_DO24、DM2J、WT901、XKC、DY500、FrameAnalyzer…) |
| `transport/` | TCP_client / TCP_server / Serial_port |
| `common/` | endpoints.h(端點覆寫)、log_utils、profile |
| `harness/` | 假匯流排回放測試(不上機驗驅動/流程) |
| `frame_capture/` | 相機障礙偵測 Python 管線(**停用**,橫桿未來備案) |
| `doc/` | 各硬體裝置手冊摘要 |
| `scripts/` | build(`build_body.sh`/`build_crane.sh`)、bench(launch)、`crcmd.py`(吊機 TCP 客戶端) |

---

## 6. 建置 / 部署

- 本體:`scripts/build/build_body.sh` → `~/run/facade_drv.out`(16 obj)→ cp 成 `facade_cleaning_v2.out` → FIFO 重啟。
- 吊機:`scripts/build/build_crane.sh` → `~/run/crane_control_PI.out`(= `crane_drv.out`)。
- 手臂:`cleaning_arm/compile.sh`(`g++ -std=c++17 -I../user_lib *.cpp -o motor_api -pthread`,**Pi 上 aarch64 編**)。
- 流程:本機改 → scp 到 Pi `~/projects/facade_cleaning_v2/` → Pi 上 build → 停舊(FIFO `exit`)→ 起新。
- 🔴 開機**不會自啟**控制程式(無 systemd/cron),斷電後要手動 start_*.sh;SD76 絕對位置與吊機
  機械煞車讓機器斷電不掉。

---

## 7. 安全 / 踩坑(架構層級)

| 坑 | 說明 |
|---|---|
| **24V blip** | 繼電器/幫浦/ZDT/手臂馬達同一 24V bus;高力堵轉過流即 blip → 關繼電器、亂 ZDT 計數器。已用降力(8 Nm)避開;根因(PSU 邊際?)未查 |
| `pgrep -f` 自我匹配 | 監看指令含程式名會匹配到自己 shell,誤判「還在跑」。用 `ps -eo args | grep "[x]name"` 或精確 pid |
| crane ssh 輸出截斷 | `user@.25` 的複合指令偶發回空,要拆開驗證 |
| `ARM_TARGET_NM` override | 降力只改手臂 default 無效,cycle_test 顯式傳值才算 |
| home_ground 重啟歸 0 | 地歸零約定要 `start_crane.sh <tag> 0`,預設 256 是頂零 |
| JC100 cup5/7/8 | 隨位置間歇讀不到(現場幾何/連接),firmware 容忍,實體有吸 |
| 相機 (.112/.113) | admin 空密碼,34567/8899/8000 **不可 port-forward**;USR 閘道不送 rup/rfp |

---

## 8. 調整建議(針對性瘦身,不做大拆解)

**決策(2026-09-12,jim)**:本體/吊機**暫不做完整模組拆解**(單人開發、上機 bring-up 中、時序
bug 風險高);改做**低風險針對性瘦身**,每刀有 baseline `bbbc425` 可回。

| 優先 | 元件 | 動作 | 效益 |
|---|---|---|---|
| 🔴 | 本體 | 砍 v1 死碼(wheels/cross_obstacle/步態…,對照 `.claude/reference/v1_v2_feature_map.md`) | 指令面 115 大減 |
| 🔴 | 吊機 | `crane_settings.json` 持久化執行期參數 | 解重啟忘設坑 |
| 🔴 | 共通 | 集中網路設定(隧道/WiFi 一鍵切,不改腳本) | 解手改 IP |
| ✅ | 手臂 | 刪 `PalletizerController.h` + 死常數;退役舊 DEPLOY;力控參數搬進 `damiao.cfg` | 免重編調力 |
| ✅ | 吊機 | gate/移除未裝中繩 + 退役 cli_D;`set_*`×30 收斂成 set/get_param | 噪音減 |
| ✅ | 文件 | ONBOARDING 抽踩坑後歸檔、更新引用;frame_capture + FrameAnalyzer 一起定去留 | — |
| 🟡 | 本體 | header 3091 行瘦身;crane 三連線合併(僅在反覆出事時) | — |

**何時再考慮完整拆解**:機器能穩定清潔後、或多人開發、或某模組成為反覆 bug 來源。

📌 **完整分層拆解的藍圖已存在**:`.claude/plans/refactor_plan.md`(2026-08-29,⏸ 暫緩)—— 6 層架構 +
黃金軌跡等價證明,且**已部分落地**(`command/dispatcher.h`、`mechanism/rope_axis.h`、`common/profile.h`、
`common/endpoints.h` 為已抽出的樁)。本檔是 **as-is**,該檔是 **to-be**;重啟時以該檔為準。

---

## 附錄:檔案地圖

```
facade_cleaning_v2/
├── ARCHITECTURE.md        ← 本檔(權威)
├── README.md / ONBOARDING.md(過時快照,待歸檔)/ CLAUDE.md
├── facade_cleaning_v2/main.cpp    本體進入點
├── app/WASH_ROBOT.{h,cpp}, wash_robot_commands.cpp   本體 class
├── command/dispatcher.{h,cpp}     本體指令分派
├── Crane_control_PI/main.cpp      吊機(單體)
├── cleaning_arm/{main.cpp,main_api.{h,cpp},damiao.cfg,damiao_config.h,compile.sh}
├── web_backend/{server.js,public/,public_v2/}
├── Linux_test/cycle_test.py …     編排/上機測試
├── user_lib/                       裝置驅動
├── transport/ common/ config/ mechanism/
├── harness/ frame_capture/ doc/ scripts/
└── .claude/                        Claude 工作區 / 專案權威文件(2026-09-12 整理)
    ├── ARCHITECTURE.md  motion_flow.md  runbook.md  per_program_cautions.md  (權威)
    ├── work_log.md(進度+待辦總表)  changelog.md(變更日誌)
    ├── reference/   設計/決策/參考(洗窗機器人設計彙整、comms_interface_for_pcb、
    │                v1_v2_feature_map、v2_app_redesign_plan=07-07 決定紀錄)
    ├── plans/       進行中/等拍板/暫緩(cycle_test_with_fan、wired_switch_and_loop_test、
    │                refactor_plan=⏸ 暫緩的分層重構藍圖)
    ├── archive/     完成/前提反轉/過時/退休(各舊 plan、mailbox、gen_deploy_pdf.py)
    ├── handoff/     AI-2 交接
    └── summaries/   各裝置 Modbus/協定摘要
```
