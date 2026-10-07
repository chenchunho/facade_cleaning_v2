# SOFTWARE.md — facade_cleaning_v2 軟體架構

> 📌 **持續維護的權威軟體架構文件**,與 `HARDWARE.md`(硬體架構:機構/匯流排/裝置/電源)成對。
> 取代 2026-08-13 快照的 `ONBOARDING.md`（**2026-09-12 已歸檔**為 `archive/ONBOARDING-2026-08-13.md`；
> 其踩坑/工程方法抽到 `reference/engineering_pitfalls.md`）。
> 程式碼與 git log 若與此不符,以程式碼為準,並回頭更新本檔。
> 首版:2026-09-12,基線 commit `bbbc425`。日常進度在 `.claude/work_log.md`。
> ✅ **2026-09-12 全文以腳本對原始碼驗證過**(行數/區段行號/埠/指令數/常數/引用計數共 37 項)。
> 行數與吊機 §3.2 區段行號**逐項命中**;修正了 7 項(指令數、v1 死碼分類、驅動清單、
> `start_*.sh` 未版控、public_v2 單檔、cli_C 註解已修、cmd_ 計數語意)。
> 🔴 **再次改動這些數字前請重跑比對,不要憑印象改。**
> 📌 **2026-09-17 對齊 09-16 清樹**:§1 拓樸(v1/v2 GUI 退役、v3 :8080、systemd 自啟)、§1.1 目錄表(frame_capture/bench/tmux launcher/DY_500 移除)、§8 樹狀圖。
>    行數類數字(server.js 544 行、驅動 16 支…)**未重驗**,09-16 刪了 19k 行後多半已過期。

---

## 1. 系統總覽

高空外牆清洗機器人 v2,吊掛式(鋼索由吊機升降),機身以四顆真空吸盤貼附玻璃、手臂
(滾筒/刮刀)配合上滑台掃動清潔。**四個獨立程序 + 一支編排腳本**,以 TCP 行協定互連。

```
                 ┌──────── Web GUI  web_backend/server.js (Node) ────────┐
                 │  :8080 console v3(public_v3/,唯一主控台;v1/v2 已退役 09-16)      │
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
   │ 硬體層(USR 閘道 6 + 水閥 .32、X518、IMU、Damiao、電源)→ 見 **HARDWARE.md** §2–§5      │
   └───────────────────────────────────────────────────────────────────────────────┘

   編排/上機測試:scripts/cycle_test.py(09-17 由 Linux_test/ 搬來;full / arm / crane 模式,走 TCP 打三程序;server.js Mission 起跑的就是它)
   離線測試:    harness/(fake_robot 假端點 + gui_v3_check / fake_bus 假匯流排 + 回放比對)
   (相機管線 frame_capture/ 2026-09-16 已自樹移除;user_lib/FrameAnalyzer 仍在本體 build 內,為樁)
   🆕 攝影機轉播:scripts/cam_relay.py = 本體 user service fcv-cam,HTTP :8091(2026-10-07;RTSP .112/.113 → H.264 直通 fMP4(預設,<video>+MSE)/ MJPEG(退路),
      有人看才跑 ffmpeg;瀏覽器直連本體,server.js 只送 {src:'cam'} 位置,不轉送畫面)
```

📌 **硬體邊界 = 軟體邊界**(per user 2026-09-13,`HARDWARE.md` §0):吊機與本體兩箱之間**只共用 220V 進線與隧道通訊**,
其餘各自負責 —— 正好對應這裡「兩支獨立程序、各跑自己的 Pi、只靠 TCP 講話」。所以本體 24V blip 時吊機程式與其張力保護**仍在跑**,
只是隧道機身端斷了;設計跨箱互動時要以「TCP 訊息」為唯一通道,不要假設對方箱內的電或訊號。

### 1.1 程序 / 主機 / 埠

| 程序 | 主機 | 埠 | 進入點 | 啟動方式 |
|---|---|---|---|---|
| 本體 | nexuni@192.168.5.26 (WiFi) / 192.168.1.100 (隧道) | 5001 | `facade_cleaning_v2/main.cpp` | `~/run/start_body.sh <tag>`(FIFO) |
| 吊機 | user@192.168.5.25 / 192.168.1.10 | 5002 | `Crane_control_PI/main.cpp` | `~/run/start_crane.sh <tag> <home_ground>`(FIFO) |
| 手臂 | 本體 Pi | 9527 | `cleaning_arm/main.cpp` | `cd cleaning_arm && ./motor_api`(FIFO) |
| Web | 吊機 Pi | 8080/8081 | `web_backend/server.js` | `~/run/start_web.sh <tag>` |

🟢 **2026-09-16 起四支程序都是 systemd 開機自啟**(吊機 `fcv-crane`/`fcv-web-v3` system unit;本體 `fcv-arm`/`fcv-body` user unit + linger),
unit 副本與重灌步驟在 `scripts/systemd/`;更新程式用 `scripts/deploy.sh`。stdin 由 unit 內 `exec 3<> fifo` 撐開(console 讀到 EOF 會退出),
本機 console 只認 `exit/quit/status`,**其餘指令一律走 TCP**。本體 `exit` 會跑 `cmd_shutdown` → **關全部繼電器**;unit 停止用 SIGTERM 不送 exit。

🔴 **`start_body.sh` / `start_crane.sh` / `start_web.sh` 不在 repo 裡**(2026-09-12 確認),只活在兩台
Pi 的 `~/run/`。**啟動參數沒有版控** —— 包含害過人的 `HOME_GROUND` 預設 256(頂零舊慣例,地歸零要
傳 0,見 §3.3)、寫死的隧道 IP、佈署路徑 `.../web/`。Pi 的 SD 卡掛掉這些就沒了 → 見 §8。
📌 2026-09-16 起 systemd unit 已把啟動參數(IP、埠、`PUBLIC_DIR`、吊機 ExecStartPost 補送的 home_ground/motion_hz/wall_height)收進版控副本 `scripts/systemd/`;
`start_*.sh` 從此只是歷史。repo 內 `scripts/` = `deploy.sh`/`deploy_web.sh`/`crcmd.py`/`link_probe.sh`/`build/`/`systemd/`(tmux launcher 與 bench 09-16 移除)。

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

**規模**:`WASH_ROBOT.h` 3091 行 + `WASH_ROBOT.cpp` 5587 + `wash_robot_commands.cpp` + `dispatcher.cpp` 554。
一個 class 扛全部。`main.cpp`(149 行)只做 TCP 伺服 + 快/慢路徑分派(`is_fast` 同步、其餘丟 worker
執行緒),乾淨。

**指令/方法計數(2026-09-12 以腳本實測,勿憑印象改)**:

| 量 | 數 | 說明 |
|---|---|---|
| dispatcher 指令(去重 `cmd == "…"`) | **99** | 其中 **15 條已掏空**成 `ERR removed_in_v2` 一行 → 實際有行為的 **84** |
| `cmd_` header 宣告 | **90** | ✅ 2026-09-12 已刪 12 條死宣告,與定義完全對齊 |
| `cmd_` .cpp 實際定義 | **90** | 11 在 `WASH_ROBOT.cpp` + 79 在 `wash_robot_commands.cpp` |
| 差額 = 死宣告 | **0** | ~~12~~ 已清乾淨 |

`FAST_CMDS` 9 條:`ping/status/pause/resume/continue/skip/emergency_stop/reset/zdt_release_stall`。

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

**v1 遺留分兩類,別混為一談**(2026-09-12 實測釐清,先前本節高估了待砍量):

- ✅ **已掏空(15 條)** —— dispatcher 只剩 `return "ERR removed_in_v2"` 一行,**不是待砍死碼**:
  `wheels` `wheels_attached` `dm2j_group` `dm2j_zero` `tilt_mode` `confirm_balance` `move`
  `obstacle_detect` `obstacle_check` `obstacle_response` `run_avoid` `balance_calibrate_{start,record,abort,status}`。
  ✅ **header 的 12 條死 `cmd_` 宣告已於 2026-09-12 刪除**(宣告 102→90,與定義對齊)。
  ⚠️ **只刪了宣告,dispatcher 的 stub 依下表分類保留** —— 重做時把宣告加回來即可。

  🔴 **這 15 條不是同一種東西 —— 動刀前先分辨**(決策來源:歸檔的 ONBOARDING §11):

  | 類別 | 指令 | 處置 |
  |---|---|---|
  | ⚰️ **真退役,不會回來** | `wheels` `wheels_attached` `dm2j_group` `dm2j_zero` `move` `tilt_mode` | 連同 header 宣告一起刪 |
  | 🔮 **暫時 stub,不是廢棄決定** | `balance_calibrate_{start,record,abort,status}` `confirm_balance`(重心校正)、`obstacle_*` `run_avoid`(窗框避障) | **保留 stub** —— GUI 上「⚖️ 重心校正」「🎥 窗框避障」兩個面板是 **per user 明確要求保留**的,等 backend 重做才會通 |

  **重做時的既定方向**:重心校正走 **IMU + 吊機差動**(與 level-match 共用感測器),**不是照抄 v1**
  「解開 body 真空自由懸掛」那套(v2 沒有 body 吸盤分組);流程沿革見 `reference/engineering_pitfalls.md` §2.5。
  窗框避障原主力是深度相機,但該路線 2026-09-01 已整套移除 → 目前**無方向**(橫桿低力刷過是現行解)。
- 🔴 **仍有實作、真的可砍(v2 吊掛式用不到)**:`attach` / `detach` /
  `cross_obstacle_up` / `cross_obstacle_down` / `run_depth_avoid` / `depth_avoid_stop` /
  `depth_avoid_continue` / `step_up_sweep_ba` / `step_down_sweep_ba` / `step_up_sync` / `step_down_sync`
  (實作集中在 `wash_robot_commands.cpp` 188 / 316 / 2769 / 2795 / 2825 / 2851 附近)。

其他:`crane_cli_` 三條連線分散;`FrameAnalyzer` 相機樁未整合(`obstacle_detect` 已掏空,旗標形同關閉)。

---

## 3. 吊機(Crane)— `Crane_control_PI/main.cpp`

**規模**:5697 行,**純程序式單體**(全部 static 全域 + 函式,無 class)。

### 3.1 裝置 / 連線

| 連線 | 裝置 |
|---|---|
| cli_A (.30) | SE3 左變頻 slave1;**中繩絞盤 MH300**(已裝,捲水管+隧道電纜;程式碼仍為 CLV900 `inverter` slave3,**驅動/命名待對齊**,實際匯流排/slave 待確認) |
| cli_B (.31) | SE3 右變頻 slave2 |
| cli_M (.34) | SD76 計米 左1 / 右2 / **中4**(實體已裝,per user;但 runtime `dev_meter_middle=0`/`length_middle=ERR` **讀不到**,待對) |
| cli_C (.33) | X518 DSZL 張力 左CH2 / 右CH1(直連 `:502`,原生 Modbus-TCP。✅ 2026-09-01 兩台併一台,碼裡註解已正確且留有「不要再新增第二個 IP 常數」警語) |
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
- **中繩絞盤實裝 MH300,程式碼仍用 CLV900 驅動/命名(`inverter`)** —— 需對齊;**中繩計米器實體已裝但 runtime 讀不到**(`dev_meter_middle=0`,查接線/slave/bus);退役 cli_D 的碼仍在;`set_*` 調參指令 ~30 條。

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

- ✅ **2026-09-12 已清**:死檔 `PalletizerController.h`(352 行,0 引用)已刪;死常數 `DEPLOY_F_FINE_*`(4)+ `DEPLOY_F_STIFF_*`(5)共 9 個已刪。
  🔴 **`DEPLOY_F_THETA_MIN` 不是死常數,刪了編不過** —— 它是 `DEPLOY_F <nm> <slot> [θmin] [θmax]` 的
  **θmin 預設值**,且參與 `th_min >= th_max` 參數驗證(`main_api.cpp:3031`)。
  ⚠️ 但它的**行為用途已於 09-11 移除**(th_min 守衛拿掉)⇒ 現在是個**收得進來、驗證得到、卻不影響行為**的
  殘留參數。要清得連同 `DEPLOY_F` 的指令簽名一起改,不是刪常數。
- 舊 `DEPLOY`(距離式)只剩 cycle_test 對「本體舊 binary」的 fallback。
- `kp_eff` 快取:`iters=0` 時為舊值;M1 從遠處 retract 會觸發過速煞車(pos 仍收到 0,無害)。
- 力控參數是 `constexpr`,現場調力要重編。

---

## 5. 週邊

| 目錄 | 角色 |
|---|---|
| `web_backend/` | GUI 橋接(`server.js` 544 行)。🆕 **2026-09-14:v2 與 v3 整合成一個 —— `public_v3/` 是唯一主控台**(v2 退役,git 歷史 `feebc03`)。v3 = v2 全部功能 + 作業流程頁(兩道閘門)+ Mission 純按鈕吃 `EVT mission` + SAFE 橫幅/解除 + Manual 在 SAFE 上鎖;刻意拿掉跑腳本看 stdout 那條路。仍是**單檔 `index.html`(~320 KB)**;安全閘門(手臂武裝互鎖、危險操作 60 s)沿用。v1 `public/` 已停機。⚠️ 本機目錄 `web_backend/`,**Pi 上佈署為 `.../web/`**(start_web.sh 寫死,`PUBLIC_DIR` 要改指 `public_v3`) |
| ~~`Linux_test/`~~ → `scripts/cycle_test.py` | 09-17 目錄退役,只剩的任務腳本搬到 scripts/。`cycle_test.py`(full/arm/mission;`FCV_WROBOT_HOST`/`FCV_CRANE_HOST`/`FCV_TOP_CM`/`FCV_ARM_NM`;高度帶 `FCV_SKIP_BANDS` opt-in 已放棄)+ 各上機腳本 |
| `user_lib/` | **15 支成對驅動**(.cpp+.h)+ 2 個純 header(`SerialPort.h`、`damiao.h`)。本體 8:ZDT、JC100、PQW、QX_DO24、DM2J、WT901、XKC、FrameAnalyzer(樁);吊機 6:SE3、MH300、CLV900、DSZL_107、SD76、ZS-DIO;手臂:damiao.h。**每一支都在 build 清單裡**(`DIHOOL_control` 09-12、`DY_500` 09-16 已移除) |
| `transport/` | TCP_client / TCP_server / Serial_port |
| `common/` | endpoints.h(端點覆寫)、log_utils、profile |
| `harness/` | 離線驗證:`fake_robot.py`(三個假端點,GUI 開發)、`gui_offline.sh`/`gui_v3_check.js`(GUI 回歸)、`fake_bus`/`build.sh`/`prove_noop.sh`(重構等價比對)、`hostile_crane.py` |
| ~~`frame_capture/`~~ | ⚰️ 2026-09-16 移除(相機路線 09-01 作廢;git `44bcb13` 之前仍有) |
| `doc/` | 各硬體裝置**原廠手冊 PDF**(gitignore,49 MB);摘要在 `.claude/summaries/` |
| `scripts/` | `deploy.sh`(開發期部署 7 目標)、`deploy_web.sh`(GUI 版號+三方 md5)、`crcmd.py`(line protocol 客戶端,兩台都能打)、`link_probe.sh`、`build/`(建置腳本權威版)、`systemd/`(4 個 unit 副本) |

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
| **24V blip** | 全系統**唯一一顆 150 W 24V**(LRS-150-24)供繼電器/幫浦/ZDT/抱閘/滾筒**及手臂 M1/M2**(per user 2026-09-12 確認);M1 堵轉峰值把 24V 拉垮 → 關繼電器、亂 ZDT 計數器。**根因已確認**(見 `HARDWARE.md` §5);軟體降力 8 Nm 是緩解,治本要分離供電/加大 PSU |
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

📌 **2026-09-12 以腳本比對原始碼後校準過**(原先高估了 v1 死碼量,見 §2.3)。

| 優先 | 元件 | 動作 | 效益 / 實測量 |
|---|---|---|---|
| ✔ 已做 | 本體 | ~~刪 header 12 條死 `cmd_` 宣告~~ **2026-09-12 完成**(102→90,與定義對齊);dispatcher stub 依 §2.3 分類保留 | — |
| ✔ 已做 | user_lib | ~~刪 `DIHOOL_control.{h,cpp}`~~ **2026-09-12 完成**(0 引用,且**本來就不在任何 build 清單裡**)。🟡 `DY_500` **未動** —— 它在 body+crane 兩份 build 清單內且 `WASH_ROBOT.h` 有 include,要動得改碼+改 build,**需上機 build 驗證** | — |
| ✔ 已做 | 手臂 | ~~刪 `PalletizerController.h`(352 行)+ 死常數~~ **2026-09-12 完成**(`FINE_*` 4 + `STIFF_*` 5 = 9 個;🔴 **`THETA_MIN` 不能刪,仍是 `DEPLOY_F` 的 θmin 預設值**,見 §4.3)。🟡 仍待:退役舊 `DEPLOY`、力控參數搬進 `damiao.cfg` | 免重編調力 |
| 🔴 | 本體 | 砍**仍有實作**的 v1 走行殘留:`attach`/`detach`/`cross_obstacle_*`/`run_depth_avoid`/`depth_avoid_*`/`step_*_sweep_ba`/`step_*_sync`(對照 `.claude/reference/v1_v2_feature_map.md`) | 99 → 約 88 條;**掏空那 15 條已不必再砍** |
| 🔴 | 吊機 | `crane_settings.json` 持久化執行期參數 | 解重啟忘設坑 |
| 🔴 | 共通 | 集中網路設定(隧道/WiFi 一鍵切,不改腳本) | 解手改 IP |
| 🔴 | **佈署** | **`start_*.sh` 收進 repo `scripts/run/`**,Pi 端改成 symlink/複製 | 啟動參數(HOME_GROUND、IP、web 路徑)進版控,見 §1.1 |
| ✅ | 吊機 | 中繩驅動對齊 CLV900(23 處)→ MH300(18 處,實裝);查中繩計米 runtime ERR;退役 cli_D(殘留 3 處);`set_*`×29 收斂成 set/get_param | 噪音減、對齊實機 |
| ✅ | 文件 | ~~ONBOARDING 抽踩坑後歸檔、更新引用~~ **2026-09-12 完成**;frame_capture + FrameAnalyzer 一起定去留 | — |
| 🟡 | web | `public_v2/index.html` 298 KB 單檔拆成 html/css/js | 可讀性;非阻塞 |
| 🟡 | 本體 | header 3091 行瘦身;crane 三連線合併(僅在反覆出事時) | — |

**何時再考慮完整拆解**:機器能穩定清潔後、或多人開發、或某模組成為反覆 bug 來源。

📌 **完整分層拆解的藍圖已存在**:`.claude/plans/refactor_plan.md`(2026-08-29,⏸ 暫緩)—— 6 層架構 +
黃金軌跡等價證明,且**已部分落地**(`command/dispatcher.h`、`mechanism/rope_axis.h`、`common/profile.h`、
`common/endpoints.h` 為已抽出的樁)。本檔是 **as-is**,該檔是 **to-be**;重啟時以該檔為準。

---

## 附錄:檔案地圖

```
facade_cleaning_v2/
├── SOFTWARE.md        ← 本檔(權威)
├── README.md / CLAUDE.md          (ONBOARDING.md 已歸檔至 .claude/archive/)
├── facade_cleaning_v2/main.cpp    本體進入點
├── app/WASH_ROBOT.{h,cpp}, wash_robot_commands.cpp   本體 class
├── command/dispatcher.{h,cpp}     本體指令分派
├── Crane_control_PI/main.cpp      吊機(單體)
├── cleaning_arm/{main.cpp,main_api.{h,cpp},damiao.cfg,damiao_config.h,compile.sh}
├── web_backend/{server.js,public_v3/,tools/check_console.js}   (v1 public/ 09-16 移除)
├── user_lib/                       裝置驅動
├── transport/ common/ config/ mechanism/
├── harness/ doc/(gitignore) scripts/{cycle_test.py,deploy.sh,deploy_web.sh,crcmd.py,link_probe.sh,build/,systemd/}
└── .claude/                        Claude 工作區 / 專案權威文件(2026-09-12 整理)
    ├── SOFTWARE.md  motion_flow.md  runbook.md  per_program_cautions.md  (權威)
    ├── work_log.md(進度+待辦總表)  changelog.md(變更日誌)
    ├── reference/   設計/決策/參考(洗窗機器人設計彙整、comms_interface_for_pcb、
    │                v1_v2_feature_map、v2_app_redesign_plan=07-07 決定紀錄)
    ├── plans/       進行中/等拍板/暫緩(cycle_test_with_fan、wired_switch_and_loop_test、
    │                refactor_plan=⏸ 暫緩的分層重構藍圖)
    ├── archive/     完成/前提反轉/過時/退休(各舊 plan、mailbox、gen_deploy_pdf.py)
    ├── handoff/     AI-2 交接
    └── summaries/   各裝置 Modbus/協定摘要
```
