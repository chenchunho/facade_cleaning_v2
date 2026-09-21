# Work Log

## 2026-09-21:程式/文件離線日 —— deploy.sh 認 official、GUI 畫 meter_suspect、goto 張力軟停講清楚、rail 拒絕帶原因、NTP 探測腳本

per user「24V 治本已經解決,隧道延遲暫時用 WiFi 之後會再改,程式/文件(離線可做)開始」。機器全部離線,**全部未在真機驗證**,下次上機先 `FCV_TARGET=official ./scripts/deploy.sh prep-official`。

### 已完成(未 commit,停在可檢視狀態)
- **`scripts/deploy.sh` 認 official**:`FCV_TARGET=official`(吊機 `nexuni@192.168.1.10`、本體 `nexuni@192.168.1.100`),`web` 目標的 `PI` 跟著切;
  sudo 一律 `sudo -n`(沒 NOPASSWD 就明確失敗);新增 `prep-official` 一次性檢查(ssh key ×2 + `sudo -n`)。
  🔴 **前置要 user 做**:`ssh-copy-id` 兩台 + official 吊機放一行 sudoers(只放行 restart 兩支服務,寫在檔頭)。
- **GUI 畫 `meter_suspect`**:Manual 吊機卡 L/R 數字被拒中標紅 + tooltip;Dashboard facts 多一列「⚠ 計米器 左 讀值被拒中(可能過期)」(平常隱藏)。
  ⚠️ 踩坑:`.wf` 是 flex 會蓋掉 `hidden`,harness「every [hidden] is display:none」當場抓到 → 補 `.wf[hidden]{display:none}`。
  fake_robot 加 `meter_suspect=` 欄 + harness-only `fake_meter_suspect [LRM]`;gui_v3_check +3 條。
- **`crane_goto` 張力軟停講清楚**(09-15 帶過來的):吊機 `cmd_goto` 在 `motion_rope` 回 `OK tension_reached` 時加 `stopped=tension_stop short_by=N retract_tension_stop_kg=K`
  (**保留 OK 前綴**——所有消費端把非 OK 當失敗,而且動作本身是安全完成;「改 WARN 前綴」被否決);GUI `gotoResultText` 畫「⚠️ 張力先到,停在 X(目標 Y,差 N;上限 K kg)」、log 走 ⚠️。
  fake_robot 加 `fake_goto_short <cm>`;harness +1 條。
- **rail 驅動層拒絕帶原因**(pitfalls §2.6 的 🟡):`DM2J_RS570` 累加式 `last_error()`(`travel_limit target=… range=[…]` / `modbus_write_failed`),
  `cmd_rail_move` 回 `ERR rail_move_command_failed reason=…`。bool 契約不變。
- **NTP**:寫 `scripts/systemd/official/ntp_probe.sh`(只探測:主路由答不答 NTP / 出不出公網 / 兩台時差),三個答案決定裝法;裝置專網沒 apt,不預設 chrony。
- 驗證:`gui_v3_check` **212/212**、check_console 全過、`main.cpp`/`DM2J_RS570.cpp`/`wash_robot_commands.cpp` `-fsyntax-only` 綠、deploy.sh `bash -n` + 錯誤路徑。
- 待辦表清理:24V 治本 ✅、隧道延遲 → 由 WiFi 橋取代(之後再改)。

### 待完成
- 🟡 下次上機:`prep-official` → 用 `FCV_TARGET=official ./scripts/deploy.sh crane/body/web` 把這批部署上去並驗:計米器標紅、goto 軟停文字、`rail -300` 回 reason、`ntp_probe.sh`。
- 🟡 `work_log.md` 1,400 行,超過壓縮門檻 —— 下次文書日壓 09-16~09-19。

## 2026-09-19:official 開機競態修正 + 計米器「永久凍結」修正(resync)+ 平衡不吃可疑計米

### 已完成
- **開機競態**:斷電重開後 `fcv-crane` 在開機 43 s 就起來,交換器/USR 網關還沒好 → 全部裝置被停用直到手動重啟。
  修法:drop-in 加 `ExecStartPre=/home/nexuni/run/wait_devices.sh`(等 `.30:4001` 與 `.34:4001`,上限 180 s,逾時放行)。
  ⚠️ 踩坑:腳本用 `#!/bin/sh` 但 `/dev/tcp` 是 **bash 專有** → dash 下永遠失敗、白等 180 s 讓 service 卡 `activating`。改 `#!/bin/bash` 後 0.005 s 放行。
  ✅ 09-21 副本已進 `scripts/systemd/official/wait_devices.sh`。
- **計米器永久凍結**(`main.cpp` `meter_read_robust`):30 cm/poll 的防雜訊規則沒有回頭路 —— 真實值一旦跟快取差 >30 cm,之後每次讀都被當「sustained corruption」,
  快取凍在舊值直到 `zero_meters`/重啟(official log:`prev=1616 v1=v2=3387` 持續 10 分鐘以上,GUI 顯示 1616/2496 實際 3387/5021)。
  official 觸發條件:3 顆錶串讀、網關慢 → 一輪 ≈0.5 s;平衡把左推到 62.5 Hz → 每輪 37 cm → 被拒 → 凍結 → err 越滾越大 → 推更高 Hz(自我強化,err 到 -103 cm 才被按停)。
  修法:`MeterResync` 追蹤「被拒但彼此連貫(相鄰差 ≤30 cm)」的讀值,連續 ≥3 筆且 ≥2 s → **RESYNC** 接受(log `[meter] X RESYNC cache a → b`)。
  真雜訊(05-14 實測「0」約 1 s)仍被拒;若假值持續 >2 s 最多跟錯一個視窗、真值回來再跟回 —— 有界錯而非無界錯。
- **平衡守衛**:新增 `g_length_*_suspect`(被拒中=快取過期),`apply_balance_trim` 見到 suspect 就 `reset_to_base()` 不修正(2 s 一則 `[BAL] ⚠`)。
  `status` 新欄 `meter_suspect=` (L/R/M 組合)、`home_status` 新欄 `suspect=`。GUI 尚未顯示。
- 已部署 official(`crane_control_PI.out` md5 `b755b403…`,舊檔留 `.bak-0918`),restart 後 L=3284 R=4890 為真實值。
  ⚠️ 踩坑:`cp` 覆蓋執行中的 binary 會 **Text file busy**(靜默失敗過一次),要 `cp 到 .new` 再 `mv -f`。

- **GUI「本體有連線但不更新」**:根因是 WiFi 橋 RF 極差(RSSI −70、**Tx PER 83%**、本體→吊機 30–40% 掉包 + 大量重複包;01:23–01:28 斷 285 s)。
  本體→web 的 TCP 卡在指數退避(Send-Q 10 KB、backoff 11、rto 120 s)—— socket「連著」但每個 status 都進重傳佇列。
  兩層處置:① `web_backend/server.js` makeBridge 加 **dead-peer 看門狗**(`BRIDGE_DEAD_MS`=30 s 沒收到任何資料就 destroy → 新連線新 RTO),已部署 official,status 0.14 s 回;
  ② QWRT 改 **HT20** + 關 Block-Ack/AMPDU(`HT_AutoBA=0 HT_BADecline=1`,寫進 rc.local):掉包 6.7%→3.3%、重複包 291→107/30 發。
  🔴 **RF 沒改善前不要跑任務**(控制通道走這條)。要 user 做:主路由 `facade_cleaning_2.4G` 改 20 MHz、拉近/對準;長期換 5 GHz 或有線。

- **GUI 吸附鎖改「只有密封才鎖」**(per user):`cupsState().attached = sealed > 0`,讀不到的那顆不再鎖 ▲▼/拉到…(official `.22` 偶發 JC100 TIMEOUT 就鎖死全部)。
  提示文字仍列讀不到顆數;本體 `crane_goto` 硬守衛沒動(自動流程仍把讀不到當吸附)。版號 `v3-2026.09.19-1548`,已部署 official。

- **09-21 程式整理**(per user,機器離線):Pi 上才有的東西鏡射進 repo —— `scripts/systemd/official/`(三個 drop-in + `wait_devices.sh`)、
  `config/qwrt/`(hotplug + rc.local + 重建 README);`fcv-crane.service` `StartLimit*` 搬回 `[Unit]`;systemd README 加 official 安裝段;
  changelog `[2026-09-19a]`(150 條)、pitfalls §2.7/§2.8 + 兩條方法論;上層索引已同步。`gui_v3_check` **208/208**、`main.cpp` 語法綠、`server.js` `node --check` 綠。**commit `d844553`**(main)。

### 觀察(待實測定案)
- 🟡 **左右繩同指令出繩量差很多**(平衡未介入的早段:左 +8 cm 時右 +18 cm;之後每段右都多)。可能右 SD76 計數模式(1x/2x)或滾輪不同 —— 這批只 clone 了 SCAL,面板其他參數沒對。**要捲尺實測**(各放 100 cm → `cal_zero`/`cal_set`)。
- 🟡 **中錶 DP=0**(左右 DP=2):`raw_scal=200 raw_dp=0` → 解析度 1 m。要從面板改:`00-16≠3 → DP=2 → 00-16=3`。
- 本體上線檢查:吊機網段掃到新主機 **`192.168.1.122`**(MAC `98:28:a6` Compal,ping 0.4 ms、22/80 全關)—— 疑為 QWRT 的 apcli0 以 WAN 身分向主路由租到的位址(OpenWrt wan zone 預設擋 input);本體 `192.168.1.100` 仍不可達。
- official 掛了約 105 kg(張力左 51.7 / 右 53.9 kg)。

- **本體↔吊機 WiFi 橋接通了(重開機驗證)**:QWRT `apcli0` 進 `br-lan`,MTK 驅動 MAT 讓有線端穿過;AP 固定 `192.168.1.250`(+`192.168.100.1` fallback)、DHCP 關。
  🔴 `wireless.sta.network='lan'` 不會自動 addif → hotplug + rc.local 兩層保險。09-18「3-address 帶不動」是沒實測的誤判。
  詳見 `config/official-crane_485.md` 第七節。
- QWRT **5 口全改 LAN**(WAN VLAN 刪除),重開機驗證持久。本體 `washrobot 192.168.1.100` 從吊機 ping 通、本體→吊機 `:5002` OK。
  ⚠️ `swconfig link:` 回報不可信(本體 0.5 ms 通但顯示 link down)。吊機→本體 RTT 7–50 ms,偶爾 ~1 s。
- **GUI 沒偵測到本體 / 本體連不到吊機** —— 兩邊 unit 檔都還寫著 bench WiFi 位址:
  本體 `fcv-body` `FCV_EP_CRANE_HOST=192.168.5.25`(有 env 就不探測有線)、吊機 `fcv-web-v3` `WROBOT_IP=192.168.5.26`。
  各加一個 drop-in 覆蓋(本體→`192.168.1.10`、web→`192.168.1.100`),restart 後 `crane_peer_fresh=1`、estop 通道通、web `[washrobot] connected`。
  本體 `.21`(QX-DO24 螺旋槳 PWM 網關)一開始 ARP 無回應 → user 上電後 ping 通、序列設定正確(115200 8N1);**裝置旗標只在 init 設一次**,restart fcv-body 後 `dev_gw21=1 dev_qx=1`、`pwm status` OK(ch3 由 ERR 變 11,ch4 50/1000 殘留照舊)。

### 待完成
- 🟡 計米器捲尺校正、右/中錶面板參數(計數模式、DP)比對。

## 2026-09-18:**正式吊機 `official-crane` 從零建置上線**(Pi5 → 可控吊機)+ 一顆 binary 兩台共用(config-driven)

> 一整天都在把**正式環境**建起來。結論:SE3/計米器/張力/水閥全部到位、GUI 可控、開機自啟,
> 且**測試機(raspberry-cran)與正式機共用同一份原始碼**,差異全靠每台一份 systemd drop-in 的 env。

### 決策(per user)
- 兩套環境並存:**測試 `raspberry-cran` / 正式 `official-crane`**,程式**共用一份**(否決維護兩套)。
- official 的 485 佈線**刻意與測試機不同**(見下表),所以把「裝置↔匯流排↔站號」做成**設定檔驅動**。
- 本體↔吊機的 Fathom-X 隧道改用 **WiFi AP(QWRT)** 取代;吊機側上層路由發 `facade_cleaning_2.4G`。
- 密碼/帳號:official Pi 維持 `nexuni/123`(不走公網);AP `root/password`。

### official-crane 主機
- hostname `nexuni` → **`official-crane`**(含 `/etc/hosts` 127.0.1.1)。
- **eth0 改靜態 `192.168.1.10/24`、gw `192.168.1.1`**(原為 DHCP 恰好租到 .10,不可靠);wlan0 `192.168.5.11` 對外。
- 關桌面:default target → `multi-user.target`、停 lightdm;**移除 chromium/firefox/LXDE/xorg 全家(釋出 ~944 MB)**;APT 升級 20 項。
- 裝 **Node.js 20.19 + npm**(唯一缺的;g++14/make/git/python3.13/pyserial 本來就有)。
- 部署吊機程式與 GUI:原始碼 tar → `~/projects/facade_cleaning_v2`、`build_crane.sh` 編譯、
  systemd unit 由 `scripts/systemd/` 改寫(`/home/user`→`/home/nexuni`、`User=nexuni`)、
  **`fcv-crane` / `fcv-web-v3` 皆 enable(開機自啟)**;GUI `http://192.168.5.11:8080`。

### official 裝置位址表(**與測試機不同**,已全部實測)
| 網關/裝置 | IP:Port | slave | 序列 |
|---|---|---|---|
| SE3 左繩 | `.30:4001` | 1 | 115200 **8N2** |
| SE3 右繩 | `.31:4001` | 1 | 115200 8N2 |
| SD76 中 / 右 / **MH300** | `.32:4001` | 中1 / 右2 / MH300 🟡未測 | 115200 **8N1** |
| X518 張力(原生 TCP) | `.33:502` | 1(CH1右/CH2左) | — |
| SD76 左 | `.34:4001` | 1 | 115200 8N1 |
| **ZS-DIO 水閥(CH1)** | `.35:4001` | 1 | **9600** 8N1 |
(測試機是:SD76 三顆全在 .34(左1/右2/中4,中未裝)、ZS-DIO 在 .32、水閥 CH4。)

### 程式改動:config-driven(預設值 = 測試機,測試機零改動)
- `main.cpp` 新增第二條計米器匯流排 `cli_M2` + `FCV_EP_USR_M2_HOST`(沒設就不連)。
- 每顆 SD76 的 client 與站號吃 env:`FCV_METER_<LEFT|RIGHT|MIDDLE>_GW`(`M`/`M2`)、`_SLAVE`、`FCV_METER_MIDDLE_ENABLE`。
  **維持「一條匯流排一個 client」**(不給每顆錶各開連線 → 避開 USR 透明網關廣播造成的 frame 污染舊雷)。
- `CH_WATER_INLET` → `FCV_WATER_INLET_CH`(預設 4、official=**1**)。
- `g_dsz_left/right_scale` 初值 → `FCV_DSZL_SCALE_LEFT/RIGHT`(**張力校正因此可持久化**,見下)。
- GUI:水閥通道**不再寫死 CH4**,改從後端 `water_status` 的 `wch<N>=water_inlet` 動態建卡片 ⇒ 兩台共用同一份 GUI(`v3-2026.09.18-2326`)。

### 校正(全部從測試機搬過來 / 現場兩點校)
- **SD76 SCAL**:三顆寫成測試機的值(有效低 3 bytes = `000200`;開頭 byte 是各錶狀態旗標、讀取時捨棄)。
- **SE3 全參數 clone**:`scripts/vfd_se3_clone.py`(SKIP 加上計數器 P.292/296/298/755/757/769/771)。
  左寫入 25、右 23,**回讀 0 不符**;**斷電重開後複驗只剩 14 個唯讀監視暫存器不同** ⇒ 設定確實存住。
- **X518 張力兩點校正**:空載 `zero_tension all` → 掛 2kg → `set_dsz_scale`。
  🔴 **official 這顆 X518 方向與測試機相反**(施力 raw 上升),scale 是**正值** 左 `0.0075758` / 右 `0.008547`
  (測試機是負值)。校完左 1.99 / 右 1.97。

### 🔴 踩到的坑(都會再犯的那種)
1. **USR 網關改 baud**:`port.cgi` 必須**帶齊所有欄位**、且要接 `manage.cgi?reset=1` 重啟才落地。
   少帶欄位 → 回 200 但值沒變(我第一次就這樣被騙)。
2. **SD76 與 ZS-DIO 不能同一條 RS485**(程式註解記過三次:共線時 SD76 回垃圾或消失)。
   official 原本把兩者放 .34 ⇒ 必須拆開,最後 ZS-DIO 獨立到 `.35`。
3. **SE3 改序列參數要斷電重開才套用**。症狀序列很典型:先是 `0xFE` 亂碼(舊 baud 還在講)→ 改了值沒重開 → **完全靜音**。重開後一次就通。
4. **ZS-DIO 的 baud 被人改過**:出廠 38400、實際在 **9600**。115200/38400 都試過全靜音,最後用「逐一改網關 baud + 重啟 + 探測」的掃描找到。
   (`0x0032`=站號、`0x0033`=鮑率碼(7=115200)、`0x003D`=校驗,**改完斷電才生效**。)
5. **X518 只允許 1 條 TCP 連線**:`fcv-crane` 佔著時我另開連線讀 → 回 `Connection refused` 或半截亂碼。要讀原始 counts 得停服務或從 kg/scale 反推。
6. **`set_dsz_scale` 與 SD76 站號改動都不持久**:每次重啟就回編譯預設/舊值 ⇒ 最後全部用 drop-in env 固化才停止 whack-a-mole。
7. **X518 IP 改法**:IP 存在參數暫存器,`IPH 0x063E = a*1000+b`、`IPL 0x0640 = c*1000+d`(只吃 FC03/FC16,32 位),寫完 `0x0A20=40` 存 flash,**重上電生效**。把它從 `.32` 搬到 `.33` 就是這樣做的。

### 本體↔吊機 WiFi 橋接(QWRT AP)— **未完成**
- AP 是 MediaTek 型 OpenWrt(`QWRT`,SSID `QWRT-2.4G`),管理 `192.168.100.1` root/password,LuCI 在 :80。
- 已設好 client:`wireless.sta`(mode `sta` → `apcli0`)、ssid `facade_cleaning_2.4G`、encryption `none`、network `lan`;
  **apcli0 已成功關聯**(BSSID `16:49:BC:47:1D:88`,與吊機主路由 `.1` 的 MAC 同源)、`br-lan = ra0 + apcli0 + eth0.1`。
- 🔴 **但本體 `192.168.1.100` 仍不通** —— WiFi client 是 **3-address**,只能代表自己,
  後面裝置的 MAC 過不去 ⇒ 要 **WDS/4addr(兩端都開)** 或 relayd,否則透明橋接不成立。
- AP 管理 IP 還在 `192.168.100.1`(與吊機網段不同),**從吊機網段管不到**;要改成 `192.168.1.250` 之類才方便。

### 待完成
- ✅ 09-19 本體↔吊機橋接通了(不是 WDS/relayd,是 MTK MAT)、AP 固定 `192.168.1.250` —— 見 09-19 條。
- 🟡 **MH300 中繩絞盤**(`.32`)站號/baud 未測;程式仍寫 CLV900 slave 3 @ USR_A,與 official 佈線不符。
- 🟡 official 跑任務前要**地面歸零**(計米器現值非零)、`wall_height` 尚未設(goto 目前被擋,安全)。
- 🟡 `hold_guard` 曾被關過,操作前確認開啟。
- 🟡 `config/official-crane_485.md` 橋接節已補(第七節);**MH300 節仍待補**。


## 2026-09-17 下午:手臂原則落地、急停收腳重送、距離上限/即時力道、張力門檻合併、警報不再拖 Idle 進 paused、rail 負值、第二顆水位計

### 決策(per user)
- **手臂原則**:上電就緒＝滾筒槽+使能;急停只把手臂拉回滾筒;整個上電期間**不失能,除非校正**(「免得機器在空中手臂亂跑」)。
- 手臂 Manual 卡只留:壓上/收回、力道、距離上限;壓上後即時顯示力與估測牆距,可改力道「套用」;**防呆 ≤ 7 N·m**。
- 刮刀做完回滾筒(預設工具)。Mission 加「任務結束回頂端」勾(`return_top=0|1`)。Dashboard 加 Mission 小卡(右下、一般尺寸)。
- `up_stop_total_kg`(▲ 手拉總和上限)**併入** `retract_tension_stop_kg`(單側,自動收繩軟停 + ▲ 手拉停)。GUI 張力卡**不要**即時值,只要左右繩差門檻可設。
- Manual 上滑台可設負值(−130..130,0=歸零時位置)。Mission 前置 ③ 推桿歸零、手臂(資訊)兩列移除;最上面狀態卡移除;Manual 本體開關機移除。
- 第二顆 XKC 水位計(高水位,slave 14):**高水位到就關閥**;低水位語意不變(有水才可噴)。

### 本體 / 手臂 / 吊機(皆已部署,真機驗過)
- **急停收腳 FAKE-DONE 重送一次**:11:36 急停時四支 ZDT 同時「到位」卻停在 ~1160°、電流 ~500 mA(堵轉是 2–3 A)
  ⇒ 同步啟動廣播被 .20 匯流排掉包(同瞬間 PQW 回讀 size=0),`estop=partial` 留 Error、腳半伸。現在對沒到位的重送絕對 0 一次。
- **失能路徑全清**:`missionStop` ② `arm_park` → `arm_retract`;本體 6 處作業路徑 PARK → 收回保持使能(`ensure_arm_parked_after_rope_`、
  arm_clean_sweep abort/round end、cleanup、M2 verify retry);`emergency_stop` 不再 `arm_calibrated_=false`;`emergency_detach_` 收臂後 `arm_slot RIGHT`。
  PARK 只剩 Manual `arm_park`(校正用)。
- 新指令:`arm_slot LEFT|CENTER|RIGHT`(腳本刮刀後回滾筒);`arm_deploy_f <nm> <slot> [dist_mm]`(122..444,θ_max 由 `490·sin(θ−0.38)+121` 反推,
  距離先到回 `OK stopped=distance`);`arm_force <nm>`(手臂 `M1 SETFORCE`,Step 6/7 抽成 `force_converge_()` 共用,已壓上時只收斂不重尋觸);
  手臂 STATUS `[M1]` 加 `wall_mm=`;**7 N·m 上限**本體+手臂兩層(`DEPLOY_F_MAX_NM`/`ARM_FORCE_MAX_NM`)。
  🔴 踩坑:距離上限第一版把 >444 **夾成 1.10 而不是拒絕**,測 500 時手臂真的壓了一次牆(未吸附、吊著,壓到 2.1 N·m 收斂無事)。改拒絕。
- **吊機警報不再把 Idle 拖進 paused**:▲ 手拉觸發 `tension_total_limit`(137 > 130)→ 本體 `paused(error)` → `crane_goto` 全拒 ⇒「按放到地面不會動」。
  改:沒有流程在跑(非 Running 且 step_in_progress=0)只記錄 + EVT,狀態不動;`reset` 也接受 paused(error)。
- **`up_stop_total_kg` 移除**:hold_loop 改 `max(L,R) >= retract_tension_stop_kg` → `EVT tension_retract_stop left= right= threshold=`(本體 handler 同步改名);
  status/持久化/setter 全拿掉,Pi 上 `crane_settings.txt` 舊行手動刪。
- `rail`/`rail_sweep`/jog 守衛 −130..130(`ARM_RAIL_TRAVEL_MIN_CM`)。
- **第二顆水位計**:出廠 9600/位址 1 在 115200 匯流排隱形 → 用網關 `.22` web(`port.cgi` 改鮑率 + `manage.cgi?reset=1` 重啟,admin 預設帳號)暫切 9600,
  對位址 1 寫 0x0004=14、0x0005=0x0D(**位址改完要隔 ≥1 s 再寫鮑率**,第一次太快沒吃到),切回 115200 → 14 回 `output=0 rssi=269 baud=0x0D`。
  本體:`lvl_high_`(slave 14);`water_level` 加 `water_high= rssi_high=`;status `water_low= water_high=`(watchdog 執行緒快取);
  watchdog 閥開時 2 s 一輪,**high=1 → 關閥 + `EVT water_inlet_auto_close reason=high_level`**;已滿送 on 回 `OK skipped water_high=1`。
  腳本:高水位已滿不補;閥由本體關,180 s 計時降為保險。
- 📌 拉到頂端停在 135 ← `retract_tension_stop_kg=75` 軟停(滑台偏一邊右繩 76 kg);`crane_goto` 軟停回 `OK … err=-96` GUI 當成功 → 🟡 待改 `WARN short_by=`。
  建議 75 → 85(user 未拍板)。

### 觀察
- 手臂壓上「小拉起來再靠回」:第一次測試 `vacuum on` 少群組參數、實際**沒吸附**,3 N·m 一壓整台被推晃;正確吸附後(四顆 −64~−69)
  `arm_deploy_f 3 RIGHT` 5.9 s、四顆壓力不動、M1 位置單調、無退回 ⇒ 現象不存在。
- 吊機 Pi 今天**兩次上電沒自己起來**(user 手動重開才起);本體沒事。要看它的電源。
- 維修後計米器 L=−253/R=−251 對不上牆高 231,跑任務前要地面歸零。

### 變頻器設定讀出／複製(user「把設定讀出來 可以複製到另外一台機器」)
- 實查 **Modbus 位址 = P 編號**(P.79→3、P.36→1)。兩台 SE3(.30/.31,slave 1)各 880 個可讀參數全 dump:`config/vfd/se3_{left,right}_2026-09-17.txt`、diff、`README.md`。
  左右只有 **P.2 下限頻率(左 0.06/右 0)** 是真的設定差,其餘 7 個是計數/監視值。
- `scripts/vfd_se3_clone.py dump|write [--apply]`:只寫不同的、跳 P.36/P.996–999、逐項回讀驗證、唯讀被拒只記錄。真機驗 dump + dry-run,**未套用**。手冊 PDF 在 user 的 D 槽。

### 上滑台負值第二道守衛(user「指定位置 −3 cm 不會動」,已部署 18:36)
- 上午只放寬指令層 `cmd_rail_move`(−130..130),**DM2J 驅動層** `set_travel_limit_cm(0, 130)` 沒改 ⇒ 負數全被驅動層擋
  (`PR_move_cm_nowait -3.000 cm REJECTED — outside travel limit [0.00, 130.00]`,GUI 只看到 `ERR rail_move_command_failed`)。
  改 `set_travel_limit_cm(−travel, +travel)`;真機 `rail -3` → pos −2.9996、`rail 0` → 0。備份 `facade_cleaning_v2.out.prev-0917-1832`。
- 🔴 踩坑型:**同一個限制有兩層(指令層/驅動層)**,改一層另一層不會跟著變,而且驅動層拒絕只進 log、指令回覆看不出原因。

### 工具重心對 M1 的影響(per user「先吸附 這個位置量測看看」,已量)
- 吸附:`vacuum feet on` + `pusher all extend_raw` 之後四顆仍 0 kPa ——**幫浦沒開**(腳本起跑才會 `pump on`);`pump on` 8 s 後 −66/−69/−67/−68。
  第一次我照腳本順序把風扇拉到 7%(user 問「為什麼風扇啟動」),之後不再動風扇。
- 單點靜態量(θ 0.30/0.40/0.50 各 5 讀)被靜摩擦吃掉:Δ 1.1/2.2/0.0 無一致趨勢,不能用。M1 從機械止點起步 tau 會衝到 6 N·m,「碰到東西」判定要等 θ>0.15 再看。
- **雙向慢掃**(0.25↔0.52,0.12 rad/s,3 趟,G=−(T_out+T_in)/2,`scripts/m1_tool_gravity.py sweep`):

  | θ | 滾筒 G | 刮刀 G | 滾筒重複 | Δ刮刀−滾筒 |
  |---|---|---|---|---|
  | 0.28 | 1.86 | 1.32 | 1.81 | −0.54 |
  | 0.40 | 3.81 | 3.39 | 4.10 | −0.42 |
  | 0.52 | 6.47 | 5.64 | 6.45 | −0.83 |

  九個 bin **全部負**、平均 ≈ −0.5 N·m(滾筒重複散佈 ±0.2~0.5)⇒ 刮刀那側重力矩小約 0.5 N·m(~10%)。摩擦帶 f=0.7~1.9 與 09-02 一致。
  滾筒實測 G 比模型 `16.09·sin(θ−0.177)` 在 0.52 高約 1 N·m(0.28~0.40 接近)。
- 結論:同一個 tau 目標,刮刀實際壓玻璃的力少 ~0.5 N·m(目標 5、容差 1、高度差已經 ~8%)——**量得到但不值得分工具補重力**;真要補,最簡單是刮刀目標 +0.5。
- 收尾狀態:手臂滾筒、M1=0;本體仍吸附(幫浦開、閥開、腳伸)、風扇 5%。

### GUI 下午(我做,v3-2026.09.17-1752,見 changelog v3n)
- 推桿卡與四張半寬卡對調(推桿墊底)+ 卡內兩欄;手臂卡工具 seg(亮＝STATUS tool=,壓上用亮的那顆)+ 全 stack 重排;張力條「總和」讀值回來(純顯示)。
- 🔴 踩坑:fake `s.lock` 是 Lock,`arm_slot` passthrough 持鎖再進 `arm_dispatch` ⇒ **死鎖整台假機器**,check 掛到 timeout 才看出來;改 RLock。
  另:`pkill -f fake_robot.py` 會先殺掉自己這個 shell(exit 144)——用 `ss -ltnp` 取 pid。

### GUI(AI-2,皆已部署 :8080,最後 v3-2026.09.17-1606)
0959 Mission 重排 → 1011 推桿卡 → 1037 單支被裁根因 → 1058 共用值 → 1123 return_top+閃爍+Dashboard Mission 卡 → 1132 小卡右下 →
1211/1215 手臂卡+err 健康值 → 1339 手臂鈕不被擠+本體開關機移除 → 1406 張力合併+本體區重排 → 1422 rail 負值 → 1448/1510 張力卡撤即時值+Mission 狀態卡移除 →
1549 兩顆水位計。踩坑:`.grp overflow:hidden` + nowrap 把按鈕擠出卡片(推桿卡、手臂卡各一次)。

## 2026-09-17 上午:GUI 重排三輪(AI-2)、推桿契約、**Manual/Mission 共用值模型 + 持久化**(per user 拍板)

### 決策(per user)
- **Manual 與 Mission 共用同一份機器狀態**(真值在本體/吊機;Manual 調什麼 Mission 就用什麼),**Mission 起跑時把安全項強制開啟**。
  否決了我原提的「Mission 自己一份參數 + 從 Manual 帶入」(太複雜)。計畫 `.claude/plans/mission_manual_isolation.md`。
- 推桿 RPM 要在 Mission 可設、預設 400 → 做成**共用執行期值** `set_pusher_rpm`,兩頁互通。
- 「張力門檻也做」→ 五個門檻**持久化**(本來重啟就回編譯預設)。
- 群組「納入/排除」鈕 → 改每支一個勾(驅動本體 `zdt_disable`,真值在本體,status `zdt_skip=`);Mission 前置 ③ 推桿歸零、手臂(資訊)兩列移除。

### 本體 / 吊機 / 腳本(已部署,真機驗過)
- 本體:`zdt_power all on|off`;status `zdt_pwr=1111`(指令狀態非回讀)、`zdt_skip=-|7|5,7`;`set_pusher_rpm <伸> [收]`(50..1000,0=keep,EVT `pusher_rpm`),
  `pusher_rpm=`/`pusher_rpm_retract=` 改為執行期值,所有 rpm<=0 的推桿路徑解析到它(預設參數 `PUSHER_RPM` → 0)。
- 持久化:本體 `~/run/body_settings.txt`(set_pusher_rpm)、吊機 `~/run/crane_settings.txt`(tension_max/diff/retract_stop/length_diff/up_stop_total),
  程式在 `set_*` 時寫一行 `set_* 值`,unit `ExecStartPost` 逐行用 crcmd.py 回放。真機驗:吊機設 77 → 重啟 → 77;本體 420/390 → 重啟 → 420/390。
  hold_guard / level_auto **刻意不持久化**(重啟回 on)。
- 🔴 踩坑:**systemd unit 檔裡 `%s` 是 specifier**(展開成 `/bin/bash`),`printf '%s\n'` 在 ExecStartPost 裡變成 `printf '/bin/bash\n'` ⇒ 回放靜默失敗;
  要嘛 `%%`,要嘛用 `echo`。另:程式在 `set_*` 時會**重寫**設定檔,回放迴圈要先 `L=$(cat …)` 快照再逐行送,不能邊讀邊送。
- 腳本 `cycle_test.py` 起跑:強制 `set_hold_guard on`/`set_level_auto on`/`crane_attached on`/`zdt_power all on`(任一失敗拒跑);
  已吸附(任一顆 ≤ −40)或 `arm_ready≠1` 拒跑;log 頭印**共用值快照**(zdt_skip/RPM/五門檻/level_auto/k/hold_guard);`FCV_PREFLIGHT_ONLY=1` 只檢查不起跑(真機驗過)。
  新 `sfield()`(`field()` 只吃數字,`zdt_skip=-`/`arm_ready=1` 要字串版)。
- fake_robot 鏡像:zdt_power all、zdt_pwr、zdt_skip(隨 enable/disable)、set_pusher_rpm。

### GUI(AI-2,皆已部署 :8080)
- 0959 Mission 重排(側欄 Dashboard 在前、參數五列、右欄 存檔/開始⇄停止/緊急脫離、四卡移除、腳本輸出+每步時間搬 Dashboard;server.js 加 `missionSave` 存檔不起跑)。
- 1011 推桿卡五點(RPM 顯示現值、當前位置歸零/自動歸零、失能/使能列、勾選框)。
- 1037 單支按不到根因:11 欄 grid 一列 >700 px、卡 300–450 px、`.grp overflow:hidden` 裁掉右邊 ⇒ 推桿卡吃滿整列 + 每支一列可折行;參數五列骨架統一;前置剩 ①②③④。
- 交接單 `ai2-shared-values.md`(Manual RPM 改送 set_pusher_rpm、Mission 共用值唯讀行、拒跑醒目)進行中。

### 待完成
- 🟡 AI-2 共用值三件 → 併 commit
- 🟡 本體 `set_setting` 22 鍵與吊機 balance/hz 家族**未持久化**(同一機制可擴,等需要)

## 2026-09-17 早:開機檢查、SOFTWARE.md 對齊、`Linux_test/` 退役 → `scripts/cycle_test.py`

- **開機檢查**:吊機這次**第一次上電就自己起來**(up 1 min:fcv-crane/fcv-web-v3 active、ExecStartPost 補回牆高 244/motion_hz 30、
  :8080 → 200、三條橋接 connected);本體 **up 15 h 沒斷過**(Idle、未吸附、arm_ready=1、M2 hold 1.58 N·m 同昨晚基準)。
  📌 昨天吊機「第一次上電沒起」沒有再現,先觀察。機器吊在 L −211 / R −204。
- **`SOFTWARE.md` 對齊 09-16 清樹**(commit `0d9c7e4`):§1 拓樸(v1/v2 退役、v3 :8080、systemd)、目錄表、樹狀圖;
  檔頭註明**行數類數字未重驗**(刪了 19k 行後多半過期)。CLAUDE.md 的樹昨晚已隨手改,兩份現在一致。
- **`Linux_test/` 退役(per user)**:`cycle_test.py` → **`scripts/cycle_test.py`**(與 `crcmd.py` 同性質:兩台 Pi 都有一份的執行期腳本;
  不另開 `mission/`,因為計畫是編排搬回 C++、它會降為對照/耐久工具)。改動:`server.js` MISSION_PY 預設、`deploy.sh script` 目標、
  兩台 Pi `mv` + 刪目錄、fcv-web-v3 重啟(log `mission script = …/scripts/cycle_test.py`)、三方 md5 5687eb8c 一致;CLAUDE.md/SOFTWARE.md 同步。
  → AI-2:`index.html:3638` 等效指令顯示字串與兩處註解的舊路徑。
- 🔮 舊日誌/交接單/計畫裡的 `Linux_test/cycle_test.py` 字樣**刻意不改**(歷史事實);找檔案以 CLAUDE.md 樹為準。

## 2026-09-16 夜:M2 一次性滑槽 + 待命位改滾筒(per user)+ M2 hold 扭矩爬升(新觀察,無基準)

- **事件**:死碼版手臂重啟時 user 看到「M2 動作異常」。log 對照三次重啟:前兩次 STARTUP→滾筒後 M1 校正期間 M2 站住(0.847/0.841);
  這次 STARTUP 到滾筒 0.8398,M1 校正那幾秒**沒有 M2 指令**,INIT 開始時 M2 已在 **0.2367 且還在動**(vel −0.06、tau +0.94)
  ⇒ M2 hold 不住被拖回中心。與 09-10「M2 間歇 passive/弱出力」同族。**不是死碼刪除造成**(手臂只拿掉三個零呼叫 accessor;`.prev-0916-deadcode` 在 Pi 上)。
- **手動驗證**(per user):`M2 LR_SLOT RIGHT` → 0.8448 / `LEFT` → 0.1974,各盯 30 s 位置不動;再重啟一次 **沒有再滑**(M2 校正期間 0.8417)⇒ 一次性。
- 📌 **決策(per user)**:**INIT 待命位 CENTER → RIGHT(滾筒)**。`main_api.cpp` INIT 尾段 `lr_move_to_slot_impl(m2_, 1)`;已部署重啟,
  `tool=roller arm_ready=1`。理由:每趟都從滾筒開始,少一次 RIGHT→CENTER→RIGHT;且中心 hold 的扭矩爬得最兇。
  ⚠️ GUI 若有「INIT 後應為 center」的假設要跟著改(AI-2 的 tool 顯示是讀 `tool=`,不受影響)。
- 🔬 **新觀察:M2 hold 時 tau 單調爬升、位置一格不動** —— 中心 −0.55 → −0.90/30 s、−0.95 → −1.66/數分鐘;滾筒 +0.54 → +0.90/30 s;刮刀 30 s 內 −0.29~−0.40 較平。
  歷史 log **沒有 M2 週期性 tau 紀錄**(只有 M1 刷屏),所以**沒有基準**判斷這是本來就有還是今天才有。已在滾筒待命位量 4 分鐘看會不會飽和(結果見下一條)。
  ✅ **4 分鐘實測(滾筒待命位)**:重啟到位後 0.54 → 2 分鐘內爬到 **≈1.6 N·m 就飽和**,之後 3.5 分鐘全在 1.55~1.59,位置 0.8417 不動。
  ⇒ 不是失控,是 hold 迴路慢慢把力補到一個**約 1.6 N·m 的靜態負載**(皮帶預載或刀頭重心偏置);中心那次 −1.66 同量級。**1.6 是今天的基準**,
  之後 >2.5 才是傳動出問題的訊號。
  🔎 回頭解釋那次滑槽:滑的時候 tau 只有 +0.94、離 1.6 還遠 ⇒ **STARTUP 剛到滾筒、hold 力還沒建立,M1 校正撞止點的震動就把它拖走了**。
  🟡 建議防呆(未做,等 user):STARTUP 到槽後等 1~2 s 讓 hold 建力,再做 M1 校正(`main_api.cpp` 一行 sleep)。
- 🗑 per user 刪除 repo 根目錄 `deploy_and_test.pdf`(v1 部署說明)與 `dm2j_manual_utf8.txt`(手冊文字擷取,權威在 summaries + doc/上滑台 PDF);CLAUDE.md 檔案表已標 ⚰️。
- ✅ **防呆已加(per user)**:`startup_then_init()` 在 STARTUP 到滾筒槽後**等 `STARTUP_TO_INIT_DWELL_MS`=2000 ms** 讓 hold 建力,再做 M1 校正。
  已部署重啟:log `M2 hold 建力等待 2000 ms 後再 INIT…` → `INIT: OK`,M2 停 0.8417。
- 🗑 **第二輪清檔(per user「看看還有沒有不要的」)**,全部已被取代、git `842e774` 之前仍有:
  `Crane_control_PI/readme/` + `facade_cleaning_v2/readme/`(VS Linux 專案模板的 GIF 教學,2.8 MB,VS 09-07 就不用了)、
  `frame_capture/`(相機路線 09-01 作廢)、`scripts/cams.sh`(起相機)、`scripts/wr.sh`/`crane.sh`(tmux launcher,兩台 Pi 沒裝 tmux、
  今天起 systemd 取代)、`scripts/bench/`(搬家前腳本副本)、`harness/__pycache__/*.pyc`(誤入版控)。
  CLAUDE.md 目錄樹/檔案表、runbook §A0(加 systemd 說明)/§0(標 ⚰️)已改。**保留**:`scripts/link_probe.sh`(隧道 A/B 量測工具)、
  `config/axis_profile.txt`(本體在用)、`mechanism/rope_axis.h`(吊機在用)、`web_backend/public/`(v2 舊 GUI,:8080 還在服務,要不要退役等 user)。
- 今晚合計:107 檔、−19,326 / +309 行。
- 🗑 **v1 GUI 退役 + v3 接手 :8080(per user)**:吊機 `fcv-web.service`(v1,:8080)`disable --now` + 刪 unit;`web_backend/public/`(index.html+app.js = **v1**,
  unit 描述誤寫 v2)自樹移除;Pi 上 `web/public`、`web/public_v2` 殘留目錄清掉。`fcv-web-v3.service` HTTP_PORT 8081→**8080**(已重啟,200)。
  `server.js` 預設 PUBLIC_DIR → public_v3;`deploy.sh`/`deploy_web.sh`(PI_PORT 8080)/`check_console.js`(預設查 v3)/systemd README/CLAUDE.md 架構圖與埠表/runbook §A0 同步。
  📌 **現在 GUI 唯一位址:http://192.168.5.25:8080**(harness 的假機器仍在本機 :8081,不衝突)。
  → AI-2 已完成並部署 **v3-2026.09.16-1948**(md5 56edc1b0 三方一致):「v2」連結拿掉;check_console 四項全綠。
  🔎 AI-2 澄清:那 4 個「寫到不存在元素」只有 `flow-init-rd` 是真死寫入(④ init 09-15 離開前置);`flow-zdt-rd`/`flow-arm-rd` 是
  `'flow-'+k+'-rd'` **動態拼的 id,checker 字面比對看不見**(它自己寫明的盲區:字串串接組的 id)—— 加 `flowRd(k)` helper 統一拼法後不再誤報;
  `rd-balen` 只是 HTML 註解裡的字樣。📌 **checker 的 ❌ 要先分「真死寫入」還是「動態 id 誤報」,不能照單全刪。**

## 2026-09-16 晚:同型 bug 掃描、work_log 壓縮、死碼移除(本體 −1,100 行、Linux_test 整批)、build 清單去寫死

### ① 「收不到回覆就重送」同型掃描 —— 全通道只有 `crane_cmd_` 一條
| 通道 | 重送什麼 | 判定 |
|---|---|---|
| `arm_cmd_` | 只在 send 失敗(0 byte 出去)重送;recv timeout 不重送 | ✅ 06-03 已有規則 |
| `TCP_client` 重連 | 不重放任何緩衝 | ✅ |
| 吊機 VFD `reliable_start/stop`、`dual_vfd_sync_retry` | 寫頻率/方向暫存器=設狀態 | ✅ 冪等 |
| DM2J `PR_move` re-trigger ×3 | 只在驅動器**從沒進 busy**才重觸發;呼叫端全絕對模式 | ✅ 冪等+驗證 |
| ZDT | 驅動層無重試;`pusher_move_many_` 讀失敗照送=絕對位置;zdt_home 相對探步單發 | ✅ |
| 達妙 CAN `max_retries 20` | 單發後輪詢回覆 | ✅ |
| cycle_test `ask()`/server.js/v3 GUI | 單發無重試 | ✅ |
| 吊機 hold ▲▼ | deadman 在吊機端;watchdog 斷鏈 → `hold_all_off + allMotionOff` | ✅ |
⇒ 單點,已修;但與 `arm_cmd_` 同族一條修過一條沒修,pitfalls 已記通則。

### ② work_log 壓縮:2,145 → 1,022 行
09-10 ~ 09-15 的 ~1,225 行壓成「歷史摘要 #2」(100 行,只留待辦/決策/伏筆/踩坑,段尾集中「本段仍開放」);09-16 四條全文;待辦總表與 #8–#15 不動;`# Work Log` 標題歸位檔首。壓縮前副本在 scratchpad(不進樹)。

### ③ 死碼移除(全部 Pi 上 aarch64 建過)
- **本體**(`app/`,−952 行):宣告無定義 12(`bal_cal_*`×7、`do_balance_calibrate_`、`do_obstacle_probe_`、`do_phase5_roll_correct_`、
  `crane_retract_to_weight_`、`dm2j_wheels_move_verified_`);有定義零呼叫 13(`crane_keepalive_loop_`、`crane_retract_safe_`+`read_rope_weight_estop_`、
  `crane_pay_out_to_weight_`、`dm2j_pair_move_abs_`、`dm2j_read_pos_robust_`、`ensure_arm_center_for_rope_`、`ensure_group_stall_clear_`、
  `feet_max_overextend_cm_`、`fine_tune_extend_per_slave_`、`pressure_poll_loop_`、`pusher_extend_with_vacuum_stop_`、`reset_seal_pulse_group_`);
  `verify_arm_deploy_` 早退之後的死半段(06-06 起不可達,09-11 被 DEPLOY_F 自己的 obstacle/no_wall 取代);
  75 個孤兒常數/成員(`BAL_CAL_*`、`FINE_TUNE_*`、`REALIGN_*`、舊 `BREAK_VACUUM_*`、`DM2J_*_WHEEL/FOOT`、`MACHINE_WEIGHT_KG`…);
  keepalive/pressure_poll 的執行緒成員與建構子初始化。**保留** `imu_tilt_from_accel_`(註解明寫 gimbal lock 備援)。
  🔴 「重心校正/窗框避障是暫時 stub 要留」講的是 **dispatcher 的 cmd_**(GUI 面板),這次刪的是 private 宣告與常數,重做時 git 有。
- **`DY_500` 驅動**:本體 `weight_[2]` 從沒用、吊機沒 include ⇒ `user_lib/DY_500_weight_sensor.{h,cpp}` 移除,三份 build 清單同步。
- **`Linux_test/` 整批移除(per user)**,只留 `cycle_test.py`(server.js 起跑的就是它;deploy.sh `script` 目標也指它)。移除:bench 互動 console `main.cpp`(270 KB)、
  `fake_slaves/`、~25 支一次性探針、`build_linux_test.sh`。CLAUDE.md / bench README / harness README / build README 已改。
- **吊機** `vfdStartRopeMotion`(零呼叫 wrapper);**手臂** `m1_motor()/m2_motor()/ctrl()` 三個零呼叫 accessor。
- 部署狀態:本體 ✅ 建+換+重啟;吊機 ✅ 建+換+重啟(user「重啟 OK」);**手臂只建未重啟**(`motor_api` 已是新檔、`.prev-0916-deadcode` 備份,下次重啟即生效)。

### 踩坑
- 🔴 `~/run/build_*.sh` 是 Pi 上的副本,repo 改了 TU 清單它不跟 ⇒ `deploy.sh` 現在每次先 scp 這支腳本再建。
- 🔴 `build_body.sh` 把 `.o` 期望數寫死 16 ⇒ 拿掉一個 TU 就 15/16 FAILED。改成從清單算(`want=$(echo $SRCS | wc -w)`)。
- 另:`arm_ready` 快取不再永遠 0(`status` 每 5 s try_lock 問一次手臂)。

## 🔴🔴 2026-09-16 傍晚:**吊機斷電上電後自己動了** —— `crane_cmd_` 收不到回覆會重送運動指令(已修)

### 事故
- user 要「機器放下來維修」→ 我送本體 `crane_goto 0`(243 cm 下行)。下行到 L≈−195 時 user 為維修**斷吊機電**。
- 本體 `crane_cmd_("goto 0", 105 s)` 卡著等回覆。user 把吊機**上電**→ 本體 TCP 重連成功 → 幾秒後 105 s timeout 到期 →
  `crane_cmd_` 的第二次嘗試「強制重連 + **重送同一條指令**」→ `goto 0` 第二次送出 → **吊機自己開始放繩**(−195 → −147),
  user 喊「為什麼吊機又動」並第二次斷電。本體 log 18:46:22 `'goto 0' attempt 1 failed — force reconnect` 是鐵證。
- ⚠️ 人在機器旁邊。這是本次上機以來最嚴重的一次。

### 根因
- `crane_cmd_` 的 2-attempt 自癒重連(2026-05-15,治「殭屍 socket」)把 **recv timeout 也當成可重試**:
  指令**已經離開 socket**,吊機可能做了、做到一半、或中途斷電,本體完全不知道,卻在重連後原樣再送一次。
  對 `status` 無所謂,對 `goto/pay_out/retract` 就是「斷線期間發生什麼都不管,回來再跑一次」。
- `arm_cmd_` 早在 2026-06-03 就有「**recv timeout 不重送**(DEPLOY/PARK 會重跑)」的規則;`crane_cmd_` 從來沒有。
  ⇒ 同一類錯誤在兩條通道上一條修過一條沒修,**踩坑索引也沒把它列成通則**。

### 已修(本體 md5 96cd3bf3,已部署重啟)
- recv timeout → **直接回空字串放棄、不進第二次嘗試**;只有「`sendData` 失敗(一個 byte 都沒出去)」才允許重試。
- 殭屍 socket 自癒保留:timeout 時 `close()` + 清 rx 緩衝,**下一條**指令自己重連;只是永遠不重送**這一條**。
- 本體重啟同時把舊程序裡卡著的那條 `goto 0` 清掉。吊機上電後盯 30 s:L=−147/R=−167 不動。

### 通則(進 pitfalls)
- 🔴 **任何「收不到回覆就重送」的邏輯,對非冪等指令都是重跑**。通道自癒只能重連,不能重送;要重送必須由呼叫端在知道現場狀態後決定。
- 🔴 **斷電維修前,先確認本體沒有卡在等吊機回覆的指令**(status `flow=`/log 最後一條 `[crane_goto] → …` 有沒有配對的回覆);
  有的話先 `stop` 或重啟本體。最乾淨:**維修前把本體也一起斷電**。

### 附帶
- `arm_ready` 快取修正(同日稍早):本體 `status` 每 5 s 用 try_lock 順手向手臂要一次 STATUS,手臂自行 INIT 後不再永遠 0。
- 右計米器跨斷電跳 ~15 cm(−244 → −259,IMU 平);level_auto 已學進去,但 `goto` 的悲觀端判斷會受影響,跑 FULL 前先最高歸零。
- 吊機 Pi 第一次上電**沒跟著起**(本體 up 10 min 時它 ARP INCOMPLETE),user 手動重開才起。之後再發生要看它的電源。

## 2026-09-16 中止/急停分家:本體 crane_goto 加吸附守衛 + 中止收尾自動收腳

> user:「中止跟急停容易被誤解,而且中止還吸附在牆上容易被誤操作」→ 三件一起做(user 核可「照這樣動手 三要」)。

### 已完成
- 🔴 **本體 `crane_goto` 加吸附硬守衛**(`app/wash_robot_commands.cpp`)。
  查證的洞:這支原本只擋 `Idle/Ready` 以外的狀態 + `step_in_progress_`,**完全沒有查吸盤**,
  而「中止」之後本體正好停在 `ready`、腳還吸在牆上 ⇒ GUI 的三顆「拉到…」/ Mission ⤒ / 前置 ⑦
  (全部都走本體 `crane_goto`)會被接受,吊機就把還吸著的機器往上扯。
  - 新 helper `cups_sealed_now_(int* out_unreadable)` —— **fresh read,不吃 `cached_pressure_`**
    (快取可能是幾分鐘前某次運動順手留下的,這裡誤判的代價是扯機器)。
  - `sealed > 0 || unreadable > 0` ⇒ `ERR cups_attached sealed=N unreadable=M …` + `EVT crane_goto_blocked`。
    **讀不到也擋** —— 讀不到 ≠ 沒吸住。
  - 救援旁路 `crane_goto <cm> force`,放行但一定留 `EVT crane_goto_forced`。dispatcher 解析第二個 token,
    壞參數回 `ERR usage:crane_goto_<height_cm>_[force]`。
  - ⚠️ **沒有涵蓋** Manual 的 ▲▼ 拉繩/放繩與緊急收繩 —— 那組直連吊機 :5002,吊機不知道吸盤狀態。
    這一層交給 GUI(見交接單),屬於已知缺口。
- 🔴 **`server.js` missionStop 由四步變五步**:④ SIGKILL 之後、**python 行程真的 close 時**
  再送 `pusher all retract` + `pump off`(`missionDetachFeet()`,旗標 `mission.detachAfterExit`)。
  - 為什麼等 proc close:`pusher all retract` 會跟腳本自己的 cleanup 搶同一條 Modbus 匯流排。
  - 為什麼補 `pump off`:SIGKILL 收場時腳本的 cleanup 沒跑到。
  - **語意變更**:中止完成後機器**純吊在繩上,不再吸附**。要「停下來看一眼、原位續跑」請用**暫停**
    (那條路徑刻意保留吸附,不受本次影響)。
- `harness/fake_robot.py` 鏡像新契約,並實測三條路徑:吸附中拒絕(`sealed=4`)/ `force` 放行 / 收腳後放行。
- 交接單 `.claude/handoff/ai2-stop-vs-estop.md`(已 SendMessage 通知 AI-2):用詞改
  **「停止作業」/「緊急脫離」**(後者紅色、隔開、二次確認)、停止後常駐橫幅、吸附中把會動吊機的按鈕變灰
  (含後端擋不到的 ▲▼)、旁邊放一顆「收腳」。
- 已部署真機:`deploy.sh body` + `deploy.sh server`(md5 f5345319 / 1371def2),五支服務 active。
  實機驗過放行路徑(`crane_goto 242` → `OK goto already_there`)與壞參數;**拒絕路徑只在 fake_robot 驗過**
  (真機要驗得先吸附一次)。

### 待完成
- 🟡 真機驗證拒絕路徑:先吸附 → `crane_goto` 應回 `ERR cups_attached`(要 user 同意才動硬體)
- 🟡 真機驗證中止五步:GUI 起跑 → 中止 → 看 `[web] STOP ⑤` 與四顆壓力回大氣
- 🟡 AI-2 的 GUI 用詞/灰化/橫幅
- 🔴 自 `e7a05ba` 之後全部未 commit(等 user 說)

## 🆕 2026-09-16（GUI 線／AI-2）：6 狀態對齊、手臂降資訊列、tool= 顯示、「停止作業／緊急脫離」分家、吸附中鎖吊機

**未 commit**（agent-ai-db 統一收）。逐版明細在 `changelog.md` `[2026-09-16v3]` 與其後兩段追加；這裡只留決策／踩坑／伏筆。
最後一版 **`v3-2026.09.16-1610`** 已部署吊機 Pi，`gui_v3_check.js` **142/142**、report 0。

### 拍板（per user，都已落地）
- **前置只剩四項必要**（① 地面歸零 ② 牆高 ③ 水位 ④ 起點）+ 兩條資訊列（推桿歸零、手臂）。手臂服務改成開機即待命
  （STARTUP → 自動 INIT）⇒ 由必要項降級；「一鍵前置」**整列拿掉**——沒有必做且可自動跑的項目還留按鈕，只會讓人以為還有事沒做。
- **「中止／急停」分家**：停止作業（一般色；正常結束，留在原高度純吊）vs 🔴 緊急脫離（紅、隔開、二次確認；全關全收進 Error）。
- **吸附中不可動吊機**：本體對 `crane_goto` 有硬守衛，但 Manual 的 ▲▼ 拉繩/放繩**直連吊機、吊機不知道吸盤狀態** ⇒
  GUI 是那六顆的唯一防線，判據與本體同一條（`p5..p8` 任一 ≤ −40 或讀不到），旁邊放「收腳」一步解鎖。

### 🔴 踩坑
1. **手臂 STATUS 的 `[M1]`／`[M2]` 是兩則訊息**（motor_api 逐段印，橋接 line-buffered），而 `armStatus()` 只解析回覆那一行
   ⇒ **Manual「M2 工具頭」那一列從有它以來就一直是「—」，真機假機器都一樣，沒人發現**——沒人盯的欄位就沒人發現。
   修法：繪圖抽成 `armPaintSeg()`，onmessage 接住單獨飄來的 `[M2]`。📌 同族教訓：**一個從沒顯示過值的欄位，先懷疑解析、不是硬體**。
2. **驗收腳本的狀態殘留**：每一節都以「乾淨假機器」為前提，但前幾節會留下腳伸著、M2 在刮刀槽、進過 SAFE 等狀態，
   後面的節就紅得莫名其妙（今天三次）。對策：每節開頭先把自己需要的狀態**明確重設**，不假設上一節收乾淨。
3. fake 的 `retract` 把 pos 設 300 而壓力跟著 `pos > 0` ⇒ 收腳永遠不放（真機不是這樣）。假機器的「鏡像」也要驗它自己能變回來。

### 📌 伏筆／刻意保留
- `en=0` 時手臂 pos/tool 都是**凍結快取**：兩處顯示都標「（未使能，快取）」且不給綠；`between` 也不給綠——它是「不知道在哪」。
- `paused` **一律連 `pause_reason` 顯示**：三種暫停的出口不同（user→resume／error→continue|skip／balance_ask→**沒有指令**，自動還原）。
- `WR_MOVING` 只剩 `running`；驗收裡有六字串真值表，舊的 `balancing/returning_home/calibrating` 必須是 false。
- 緊急脫離的 `confirmOnce` 在 kiosk 可能被自動接受 ⇒ 它是提示不是閘門；真正的分隔是名字、顏色、間隔。
- `public/app.js` 的 `paused_on_error` 是 v2 殘留，**刻意不改**（v2 已退役、`public_v2/` 已刪）。

### 🟡 待辦（GUI 線，延續 09-15）
- 🟡 Mission「開始」的確認窗要不要拿掉，等 jim。
- 🟡 腳本模式沒有「跳過」通道（`mis-skip` 只在 body 模式顯示）。
- 🟡 張力保護卡整張 `data-safe-keep`，jim 若不要再收窄。


## 2026-09-16 五支程式改 systemd 開機自動啟動、本體狀態機 11→6、開機盤點、水閥時序、手臂自動 INIT

### 已完成
- 🔴 **五支程式改成開機自動啟動**(user:「四支一起上」,加上 v2 web 共五支)。unit 檔副本已收進
  `scripts/systemd/`(含 README 與重灌安裝步驟)—— 在此之前它們**只存在兩台 Pi 的檔案系統裡**,
  Pi 重灌就沒了而且沒有任何地方記得它長什麼樣。
  - 吊機 .25 用 **system unit**(該機有 sudo):`fcv-crane` / `fcv-web`(:8080)/ `fcv-web-v3`(:8081)
  - 本體 .26 用 **user unit + `loginctl enable-linger nexuni`**:`fcv-arm` / `fcv-body`
    (本體 sudo 密碼不在 Claude 的密碼簿裡,在使用者那本)
  - 🔴 **`exec 3<> fifo` 的理由**:三支 C++ 都有 console,console 讀到 **EOF 會讓程式退出**,
    而 systemd 預設把 stdin 接 `/dev/null` ⇒ 開機起來立刻 EOF ⇒ 當場結束。開一個 fifo 並**自己同時
    持有讀端與寫端**就得到「開著但不會 EOF」的 stdin。副作用是好的:外部仍可 `echo <指令> > ~/run/body.in`。
  - 🔴 **`fcv-crane` 的 `ExecStartPost` 是必要的**:吊機三個執行期參數不落地,重啟一律回預設
    (`home_ground_cm`→0 會讓 cycle_test 誤判座標慣例、`wall_height_cm`→0 會讓 `goto` 直接拒絕、
    `motion_hz`→編譯預設),所以等 20 s 後補送一次,牆高從 `~/run/wall_height.json` 讀。
  - 🔴 `fcv-body` 刻意 **`KillSignal=SIGTERM`、不送 console 的 `exit`**:`exit` 會跑 `cmd_shutdown`
    關掉所有繼電器,腳吸著時等於當場失去真空。
- **`scripts/deploy.sh`**(新,開發期更新流程,user 核可):`body|arm|crane|server|script|web|status`,
  每個目標一律五步:同步原始碼 → Pi 上編譯 → **備份現役 binary**(`.prev-MMDD-HHMM`)→ rm+cp 換檔 →
  重啟服務 → 驗證啟動訊息。**先 rm 再 cp** 是因為覆蓋執行中的執行檔會 `Text file busy`。
  - 這支腳本自己踩了四個坑,都已修並就地寫註解:
    · `awk -F= '$2 <= -40'` —— 尾端空白產生空欄,`"" <= "-40"` 是**字串**比較為真 ⇒ 四顆都沒吸也算出 1 顆。
      改 `NF==2 && $2+0 <= -40`。
    · echo 字串裡的反引號**被 shell 執行**了。
    · 只看 md5 判斷編譯成功 ⇒ **編譯失敗照樣印 ✅**(g++ 失敗會留下舊 binary)。改成抓編譯輸出裡的 `error:`。
    · arm 只同步 `main_api.{h,cpp}` ⇒ Pi 上 `main.cpp` 停在舊版,症狀是「本機改好了、Pi 上編不過」。
      改成整個目錄的 `*.cpp *.h` 一起送。
- 🔴 **本體狀態機 11 → 6**(user:「一次收斂到位」)。
  `Idle / Ready / Attached / Running / Paused / Error`,另加 `pause_reason`(none|user|error|balance_ask)
  與 `flow` 兩個正交欄位。被吸收掉的:`PausedOnError`→`Paused+reason=error`、`WaitingConfirm`→同上、
  `Balancing`→`Paused+reason=balance_ask`、`ReturningHome`→旗標 `returning_home_`(繩重上限吃它)、
  `Calibrating`→沒有實際使用。status 新增 `pause_reason=` 與 `flow=`。
  - ⚠️ `returning_home_` 用 **RAII 清除**:`cmd_return_home` 有 20+ 個 return 點,漏清一個會讓「吊著」的
    重量上限在回到 Idle 之後還留著,而那是看不出來的錯。
- 🔴 **開機盤點**(user:「開機進 IDLE 應該是要把全部的東西關掉跟收回,如果沒有就表示東西收不回要進 ERROR」)。
  `init()` 讀四顆壓力:**吸附中或全部讀不到** ⇒ 直接跑 `emergency_detach_()`(與急停同一支,九步,約 10 s)。
  九步全成功 → `Idle`(那時 Idle 名副其實);任一步失敗 → **留在 `Error`**,`estop=partial` 指出卡在哪。
  - ⚠️ 這是**有代價的選擇**:開機與「機器正吊在牆上」會同時發生(停電復電、跳電、重啟服務),
    本案等於把上電定義成「鬆手」。user 明示採用。
  - 同步執行不開背景緒:此時 TCP server 還沒起來,沒有指令會跟它搶,而「收完才開始接指令」正是要的順序。
- **水閥時序改成兩段**(user:「沒水開閥,偵測到水再等 20 S 就開始清洗,等 180 S 完成後關水閥」):
  `WATER_PRE_CLEAN_S=20`(偵測到水 → 等 20 s 就開始清洗,閥留著繼續灌)、
  `WATER_TOPUP_S=180`(偵測到水後 180 s 由**背景計時器**關閥)。`close_inlet_now()` 冪等,收尾一律再關一次
  —— 否則腳本結束後閥還開著,只剩 300 s deadman 兜底。
- **手臂程式一開就自動進 INIT**(user 指示,這樣 GUI Mission 前置的「手臂」項可以拿掉)。
  新公開 `startup_then_init()`(STARTUP 成功才接 INIT;`cmd_init_sequence` 是 private,加 wrapper 而不是放寬存取)。
- **手臂 STATUS `[M2]` 加 `tool=`**(`roller|squeegee|center|between`),GUI 才能顯示「現在是滾筒還是刮刀」。
- 急停已在 GUI 上實測過一次完整流程(任務執行中按下 → 腳本先停 → 九步脫離 → 自動 Error→Idle)。

### 待完成 / 未拍板
- 🟡 手臂 INIT 結束位置要不要改成滾筒(現在是 center)—— 未拍板
- 🟡 `DEPLOY_F_TARGET_NM` 目前檔案上是 **5.0**,但腳本跑 FULL 時明確傳 `arm_deploy_f 3`,
  所以實跑都是 3 N·m。要不要把 header 改回 3.0 —— 未拍板
- 🔴 JC-100 slave 8 一天 67 次讀取錯誤 → 換頭候選
- 🟡 24 V 掉電的根因(換電源模組後症狀消失,根因未查)
- 🟡 `work_log.md` 已約 2000 行,該壓縮了

### 歷史摘要 #2（2026-09-10 ~ 2026-09-15）
> 2026-09-16 壓縮。原文 ~1,225 行 → 本段;逐版明細在 `changelog.md`、指令在 git。只留**待辦／決策（含被否決）／伏筆／踩坑**。
> 例行「裝了、跑通了」一律丟。仍開放的待辦集中在段尾「本段仍開放」;已進待辦總表的不重列。

#### 09-10 幫浦 A/B 輪替、CH4 修正、腳本整併、v2 GUI、watchdog 死碼
- **決策**:真空幫浦 A/B 每 30 分輪替,make-before-break(開 B → 併聯 3 s → 關 A → 8 s 內驗真空劣化 >15 kPa 就切回 A 並停用自動輪替)。
  累計**不跨重開機**;`cmd_pump_swap_` 非同步回 `OK swap_started`,前端等「先見 swapping=1 再見 0」+75 s 上限;
  **後端刻意不消那個微秒競態窗**(消它風險大於收益,前端那套本來就得存在)—— 別當 bug 補。
- **決策**:`CH_WATER_PUMP` 14→**4**(PQW 只有 8 路,14 打不到;≠ CH6 破真空);GUI 送語意 `water_pump` 不送 `relay 4`(通道搬過三次)。
  水位計裝在**最低點** ⇒ `water_full=0`＝空箱,不可開噴水馬達。
- **決策**:六支腳本併成 `cycle_test.py <mode>`(full/crane/arm),第一參數是數字＝full 向後相容;舊檔轉墓碑。
- **決策(被否決)**:v1→v2 GUI 逐項移植**作廢**,v2 自訂;Manual 完全不鎖(flag 留著);JOG 前端移除(web 共用 socket + 隧道延遲,
  deadman 600 ms 頓挫)、**後端 `rail_jog` 保留**。通則:**按住式控制不適合 web 共用 socket;長動作要機器擁有、非同步回 OK**(`rail_sweep`)。
- **踩坑**:PQW 對不存在的通道**照單全收並編造回讀**(`ch14=1`)⇒ CH_BRUSH 誤改 15 五週沒人發現;`PQW_TOTAL_CH` 16→8 只是「說謊降級為沉默」。
  `zdt_disable` ≠ 失能(只是排除群組);ZDT pulse **無號**,負絕對值會反向衝;ZDT 有原生 homing 從沒用(`trigger_home 0x02`,🔮 待試)。
  `pkill -f` 會殺到自己的 `bash -c`(node/cycle_test/sleep infinity 三次);FIFO console 只認 exit/quit/status,其餘靜默丟(兩台同一段碼,一天踩兩次)。
  `cycle_test.py` 在 WSL「乾跑」＝連真機真動作。跨裝置自動急停判準要 `wasBothUp`。
- **通則(AI-2)**:判斷保護在不在要找**誰讀這個值**、任何程式碼要找**啟用條件**(執行緒啟動點/if 閘門/解除路徑);只宣告沒定義編譯器不吭聲;
  對有物理副作用的遠端指令**先武裝後送**(水閥 `set_water_inlet_`);越界不報錯的裝置會編造回覆;動作對與理由對要分開驗。
- 本體 crane watchdog 是死碼(`crane_last_ok_ms_` 寫 3 讀 0)且 `crane_keepalive_loop_` 自 05-15 就沒啟動 ⇒ **死碼餵死碼**;
  補回 2 s 門檻會誤觸發(沒 ping 餵)⇒ 分三步:先只發 EVT → 實測 idle 分布重選門檻 → 才接 abort(09-14 已補回觀測模式 warn=2000/abort=0)。
- 🔮 伏筆:`vac`+`vacSource()` 是 v2 真空源唯一事實來源;救援收繩走 `cmd_manual`(無保護)刻意不與 `cmd_hold` 合併;
  水閥控制**走本體**不直送吊機(deadman 只在本體;吊機端沒有 ⇒ 待辦「吊機端補水閥 deadman」);`web_backend` 無 lockfile,`package-lock.json` 要**在 Pi 上從現有 node_modules 產**。
- M2 選刀槽值因傳動滑動失效 → 手轉重量:刮刀 +0.1913 / 滾筒 +0.8558;再滑就先修傳動。M2 間歇 passive 電源重置可清。

#### 09-11 力控提速、橫桿問題定案、地面歸零慣例、full 首度上機、24V blip
- **決策**:DEPLOY_F 提速四招(粗壓段 COARSE_NM、RELAX 900、deploy_speed 0.35、**尋觸也改粗步**)deploy 10→5.5 s;
  再砍 settle 會 overshoot,**36 s/週期是實際下限**。`fine_adjust_level_diff`(水平基準 L−R)有用勿移除。
- **決策**:橫桿偵測**放棄**(θ 與剛度都分不出橫桿/玻璃,實測全失敗)→ **降力刷過**(15→8 N·m,細掃/th_min/剛度判據全刪);
  高度帶跳過 `FCV_SKIP_BANDS` 留 opt-in。🔴 踩坑:**降力要改「顯式傳值處」不能只改 default**(cycle_test 傳 15 蓋掉手臂 default,full 又 blip 一次)。
- **決策**:牆面座標慣例＝**地面歸零**(`zero_meters ground` → SD76=0;頂端只讀 |length_left| 存牆高,**不要 `zero_meters top`**),
  `home_ground_cm` 維持 0;牆高存吊機 `~/run/wall_height.json`,①地面/②最高點獨立可單獨重校(只重②一定安全)。
  🔴 `start_crane.sh` 預設 HOME_GROUND=256 會打回頂零,地面歸零要帶 0(09-14 又踩一次)。
- **決策**:「只要機器上電 arm 都要使能」⇒ 收手臂用 `arm_retract`(不失能),cycle_test 已無 `arm_park`;唯一失能時機＝校正位置。
  手臂力控＝「壓到異物就不刷、跳過到下一位置」(cannot_reach/obstacle 不 bail)。
- **決策**:上滑台 DM2J DI1 常閉未接線 ⇒ **馬達永遠使能,`rail_enable off` 不生效**;行程守衛可設定落在 GUI 軟限位(本體無 `set_rail_max`)。
- **踩坑**:full 首度上機 step ~4 手臂 M1 超速+間歇失力中止;進水球閥 `.32` CH4 載重時間歇 Modbus 逾時(非硬故障,init 該步 skip 安全);
  V2 GUI 重啟後手臂「自己靠上」(kiosk 自動接受 confirm)⇒ 武裝互鎖;**腳吸著時送 `exit` 重啟 ⇒ 關繼電器失真空**(重犯)。
- 🔮 牆距可由 θ 換算 `490·sin(θ−0.38)+121` mm(GUI 剖面待辦);ZDT 推桿 rpm 可選參數(50..1000);`RAIL_JOG_DEADMAN_MS` 600→1000 若頓挫。

#### 09-12 文書日:架構文件、瘦身第一刀、24V 根因
- **24V blip 根因(per user)**:全系統只有一顆 LRS-150-24,手臂 M1/M2 也掛在上面;高力堵轉峰值拉垮 24V;**無任何斷路器/保險絲**,唯一保護是 PSU 自身 OLP hiccup
  (解釋 blip 為何自己回來)。治本三選項並行:分離供電/換大 PSU/**加裝過流保護**。⚠️ LRS 手冊 OLP 行為未核對。
- **螺旋槳兩顆 ESC 並接同一路 PWM** ⇒ 結構上無法差動、單邊失效無法偵測;急停必須能切 57.6V 動力。
- `SOFTWARE.md`+`HARDWARE.md` 成對取代 ONBOARDING;`engineering_pitfalls.md` 抽出;瘦身第一刀純刪(PalletizerController、DIHOOL、9 個死常數、12 條死 cmd_ 宣告),
  預處理比對證明無副作用。**本機能 build**(g++ 9.4 syntax/x86 .o/預處理比對,只差 aarch64)—— 「本機不能 build」是假的。
- **三個被推翻的判斷**:`DEPLOY_F_THETA_MIN` 不是死常數;`removed_in_v2` 15 條裡**重心校正 5 + 窗框避障 4 是暫時 stub 要留**;本機可 build。
- **拍板**:8 N·m 對刮刀可接受不拆目標;DSZL-107 已校正、scale 沒地方存 → 併入「吊機參數不持久化」。
- PQW 實體 8 路;真空閥四顆共用一顆 VT307;正壓閥數量待目視。

#### 09-13/14 假機器、三條拍板、計畫書、階段 1、行緩衝、8/6/3 N·m
- **三條拍板(per user)**:① 編排搬回 C++(`mission` 指令族)② SAFE 最小版(風扇直接關)③ 自檢＝GUI 軟閘門(否決硬閘門,擋救援)。
  → `plans/orchestration_to_cpp_plan.md` 已核准;`harness/fake_robot.py` + `gui_offline.sh` 是階段 0。
- **決策**:v2/v3 整合成一個,`public_v3` :8081 唯一主控台;`safe_clear` 無任務回 idle/ready;SAFE 後任務暫停可續;`zeroed=0` 軟提示;手臂加 `PING`;
  AI-2 的 jsdom 驗證收進 `harness/gui_v3_check.js`。
- **階段 1 完成**:`selfcheck` 指令(真探測,不上 2 Hz status)+ `dev_*` 欄位 + `arm_ready`;偏離計畫 §4(init 旗標對 Mode B 裝置永遠 1)已回寫。
- **`crane_cmd_` 行緩衝真因**:`rx` 是區域變數 + `sendData()` 靜默排掉 4 KB ⇒ EVT 洪水切在行中間、尾巴變下一條的回覆(也會吞 `tension_alarm`)。
  修:`crane_rx_buf_` 成員 + 送前先消化佇列。🔴 `receiveData(…,0)` 是永久等,要先 `available()`。本機 hostile_crane 重現 2/6→6/6。
- **力控定案**:8→6→**3 N·m 為工作力度**(per user);真因「iters=0 漂走」＝尋觸最後一步只等 150 ms 就讀瞬時 tau → Step 6 進割線前先等 RELAX 900 ms 重讀。
  `DEPLOY_F_COARSE_NM` 6→4;粗壓門檻 `min(COARSE, target−TOL)`;`FCV_DRY=1` 乾掃旗標。
- **踩坑**:`compile.sh` 就地覆蓋 motor_api 無 `.prev`;`~/run/motor_api` 是舊版、現場從 `cleaning_arm/` 起;`pgrep -c -f` 數到自己;
  Bash 工作目錄會在指令間重設回 `/mnt/agent_ai`(一律絕對路徑);`start_crane.sh <tag> 0`。
- **硬體**:JC-100 slave 7 **換表頭後錯誤歸零**(壞的是表頭);`.21` 網關一次沒跟著上電(selfcheck 第一次抓到真問題);
  **濕式 full 四趟全被 24V 掉電打斷、乾掃三趟全過** ⇒ 濕式負載(水泵+滾刷+真空+推桿保持)疊上去超過 OLP,升為第一優先(09-15 user 換電源模組後消失)。
  blip 時只有 PQW 掉、ZDT 沒重置 ⇒ 24V 幹線有分支。
- 隧道(Fathom-X)當天不通,全走 WiFi;WiFi 偶抖(reconnect failed 自癒)。

#### 09-15 濕式跑通、單程接力、goto、level_auto、Mission 合併、急停改版、收腳時序
- **決策**:清潔改**單程接力**(滾筒 起→對面、刮刀 對面→起);風扇**只在下行移動時開**(風扇一轉 JC-100 RS-485 錯誤就冒;5%＝ESC 停,不是 0);
  full 參數改 key=value(`fan=move|all[:pct]`、`rail=<起>-<迄>|off`);補水進 cycle_test(XKC 單點門檻附近 0/1 亂跳 ⇒ 滿了再灌 20 s)。
- **決策**:`goto <離地cm>` 絕對位置(跨距外拒;**往上信高的、往下信低的**);本體 `crane_goto`;測試性移動單次 ≤5 cm(memory)。
  端點跑掉:① goto 方向保守 ✅ ② 端點靠張力事件 **不做** ③ 落地重歸零 **手動**。
- **決策**:水平基準 **level_auto**(靜止+IMU 新鮮+|roll|≤8° 時 `level_diff=(L−R)+roll/k`,k=0.85,EMA、±0.75 遲滯、夾 ±20)——
  rail=100 從三步就撞 10 cm 變連兩趟乾淨;**rail=100 + fan=move 可當作業設定**。`set_hold_guard on|off`(重啟回 on)。
- **隧道實測失敗**:換電源後不掉包但延遲 avg 330 ms/max 1.8 s、運動中 age 27 s ⇒ **維持 WiFi,隧道不切**(降級「有空再修」)。
- **Mission 合併(per user)**:作業流程頁併入 Mission(名稱保持);啟動先走腳本(server.js spawn),`MISSION_BACKEND='script'|'body'` 常數,body 路徑原封留;
  危險按鈕 tier/60 s 解鎖/手臂互鎖**整套取消**(`dangerGate` 等留成 no-op 呼叫點不動);急停搬進 Mission 控制列且**先停腳本再急停**;
  救援收繩移除(＝關張力保護 + 一般 ▲▼);前置 ④ init 移除、③ 推桿歸零改資訊列(ZDT 磁編碼器+電池跨斷電保留零點)。
- **急停狀態機**:Error 放行 14 個 manual 指令(**Error 擋的是自動流程,不是人的手**);`emergency_detach` 全成功自動 Error→Idle,`estop=` 欄位。
  🔴 踩坑:`M1 STATUS` 沒有 `en=`,要解析整機 STATUS 的 `[M1]` 段(每次急停誤報 partial)。真機首次驗證急停九步。
- **ZDT 兩個真相**:已在目標再下絕對命令 → 殘差走不掉 → 150 ms 判堵轉 STALL 3 A ⇒ `pusher_move_many_` 殘差 ≤300 脈衝就不送;
  **10 脈衝/度(3600/圈)**,不是手冊的 3200。
- **收腳正壓時序**(五組實測):有效的是「收的當下閥開著」,預灌無貢獻。定案:關真空閥 → 100 ms → CH6 ON → 立刻收(不等到位)→ 300 ms → CH6 OFF,
  峰值 3.1 A → 0.5 A、收腳 8 → 3.1 s。🔴🔴 **靜置 0 ms 破真空閥靜默不動**(PQW 兩次寫太近第二次失效;100 ms 是實測下限;收腳峰值 >1.5 A 就懷疑它)。
- **風扇順序改版(先吸附再關風扇、先開風扇再收腳)實測更吵、無改善**,建議改回(等 user)。`cmd_arm_init` 後補送 STATUS 餵 `arm_ready`。
- **GUI 踩坑(AI-2,四個同族「看不出來的那一層」)**:`hidden` 被 class 的 display 蓋掉(驗收要看 computed display);自動收合與人打架(人碰過就永久退場);
  收起連標題一起藏只剩一個入口;腳本模式被本體 `mission=` 蓋掉。伏筆:推桿 RPM 欄只填 placeholder;`gui_v3_check.js` 跑前刪 `wall_height.json`。
- 🔴 JC-100 slave 8 一天 67 錯(其他 ≤18)→ 換表頭候選。

#### 本段仍開放的待辦(未進總表的)
- 🟡 風扇順序改回原版(先關風扇再吸附、先收腳再開風扇)—— 等 user
- 🟡 吊機端補水閥 deadman(繼電器在吊機手上;或用 ZS-DIO `0x0030` 硬體斷線全關)—— 等拍板
- 🟡 手臂 STATUS `tool=` 已做;牆距剖面 GUI(θ→mm)未做
- 🟡 `trigger_home(0x02)` ZDT 原生歸位試用;ZDT 驅動加 pulse<0 守衛
- 🟡 `package-lock.json` 在 Pi 上從現有 node_modules 產並進版控
- 🟡 Mission「開始」確認窗要不要拿掉;腳本模式「跳過」通道;張力保護卡 `data-safe-keep` 範圍
- 🟡 DM2J 驅動層 `PR_move_cm_nowait` 送出失敗重試(有上限、印出來;`not_confirmed` 那條不重試)
- 🟡 Manual 手臂 M1/M2「靠上/離開」+ DEPLOY_F 壓力/距離雙上限(設計定案未實作)
- 🟡 `motion_flow.md` §2 硬體表仍是 v1;瘦身第二刀(`DY_500`、本體 v1 死碼 11 條)
- 🔴 24V 治本三選項 / LRS OLP 手冊核對 / 急停切 57.6V / RCD;開箱拍照補 HARDWARE §3.2C

## 🔴 待辦總表（單一權威，2026-08-27 由 mailbox / ONBOARDING 併入）

> 📌 **這張表是本專案唯一的彙整待辦清單。** 2026-08-27 專案改為單人開發，退休了多人協作
> 的分工機制，原本散在三個地方的未結案項目全部併到這裡：
>
> | 來源 | 開放項目數 | 現況 |
> |---|---|---|
> | `.claude/archive/mailbox.md`（`### → 架構（Jim）`，2026-04-22 ~ 2026-05-14） | **16** | 已退休，檔案改為墓碑 |
> | `ONBOARDING.md` `## ⚠️ 尚未解決 / 待處理事項` | **6** | 該節改為指標，其餘章節不動。⚰️ **2026-09-12 全檔歸檔為 `.claude/archive/ONBOARDING-2026-08-13.md`** —— 本表與下方各處的 `ONBOARDING §N` 出處標註**一律指該歸檔檔**；踩坑/工程方法已抽到 `.claude/reference/engineering_pitfalls.md` |
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
| 🔴🔴 | 🆕 **VFD 一運轉，隧道就掉 90% 封包 —— 電氣問題，不是軟體** —— 2026-09-08 受控實驗（per user 執行，機器未貼牆、繩上 6.4kg 無吊重）：同時量隧道 `192.168.1.10` 與 WiFi `192.168.5.25`，5Hz／420 秒，中間按住 `▲ 拉繩` 一次（hold 窗口由吊機 `HOLD-TRACE` 取得，17.45 秒）。**隧道 9/90 ＝ 90% 丟包；同一秒 WiFi 90/90 ＝ 0%。** 三個獨立證據：① 同兩台機器同時段，WiFi 毫髮無傷 ⇒ 不是主機忙，是隧道專屬 ② roll age **單調惡化** `758 → 2,895 → 5,045 → 7,206ms`（17 秒內）⇒ 持續劣化不是偶發尖峰 ③ 吊機自己對 VFD 的 Modbus 同時段 `keepalive ok=50 fail=0`（走本地閘道 0.11ms）⇒ 毫髮無傷。📌 Fathom-X 是 HomePlug AV，工作頻帶 **2–30 MHz**，VFD 切換雜訊正落在帶內 ＝ 教科書等級的干擾情境。🔴 **後果：隧道目前不能當控制鏈路** —— 它正好在**運動期間**（唯一需要它的時候）掉 90%；整段 hold 平衡全程 `src=meter` ⇒ **IMU 路徑實質死亡**。🔴 **WiFi 在干擾解決之前不能拆。** 🔧 待查全部需 per user 現場資訊：① Fathom-X 板子實體位置（是否貼著變頻器／動力線）② **供電是否與 VFD 控制電路共用（最強候選** —— HomePlug 靠訊噪比工作，電源軌被污染直接殺 SNR）③ 線材是否與動力線同束／平行。**便宜的決定性測試：吊機端 Fathom-X 改用獨立電源（電池）再測一次** —— 清乾淨＝電源耦合；沒改善＝輻射／線間耦合。常規對策：共模扼流圈／鐵氧體磁環、馬達線改遮蔽線 360° 接地、VFD 輸出加 dU/dt 或正弦濾波器、隧道遠離動力線。 | 電氣／機構（非程式） | **病因已證實，待現場處置** ✔ | 2026-09-08 |
| 🔴🔴 | 🆕 **`IMU_ROLL_STALE_MS = 750` 對隧道太緊 —— 但它排在干擾後面，不要先動** —— 即使**閒置**，隧道最大空窗 903ms 就已 > 750ms；**馬達一動 2,867ms**，連放寬到 1,500ms 都破，且超過 watchdog 的 2,000ms。⚠️ **本條當日一度被排成第一順位（「切有線前必須改」），同日就被自己的實驗推翻** —— 先調門檻＝把一條**在運動時 90% 死掉**的鏈路用更寬的門檻蓋起來，蓋住的正好是機器吊在牆上時最需要看見的東西。⚠️ 同時**收回**「每 5.5 秒破一次」那個描述：量細之後空窗間隔散佈在 **0.6~10.6 秒**，**不是週期性**，排除「HomePlug beacon／通道適應是唯一成因」。⇒ 干擾處置完成、隧道能當控制鏈路之後這條才輪得到。選項：放寬門檻／提高推送頻率／改成隧道感知的時效判斷。📌 **現在測不出來** —— WiFi 上 120 秒 0 次超標，這個缺陷在 bench 上完全隱形，是「在不具代表性的環境上驗收」的典型。 | `Crane_control_PI/main.cpp:884` 🆕 **2026-09-09 per user 指示改成 5 tick（1250ms），實測更糟、已回退 750。**三輪 1 週期對照（有線、起跑姿態對齊）：**750 → src=imu 67%／roll_age 149ms／≥10cm 0/6**；**1250 → 20%／829ms／1/6**；**1250 重跑 → 21%／806ms／2/6**。🔴 **放寬門檻 67%，IMU 可用率反而由 67% 掉到 20%**，重跑高度複製 ⇒ **不是隧道隨機變差，是門檻造成的**。📌 **推測機制（未實證）**：採用舊 roll → 用過時姿態下 trim → 機器晃更兇 → VFD 動作更劇烈 → 隧道干擾更嚴重 → 推送更慢 ⇒ **這個門檻會透過控制品質回饋到鏈路品質本身**。🔴🔴 **更正：上面那個因果結論被第四輪推翻。** 回退門檻後 `src=imu` 只有 **8%**（四輪最低）⇒ **同一門檻 750 給出 67% 與 8%，變異比門檻效果大得多**，「放寬造成 IMU 下降」不成立。📌 **我真正犯的錯是把「重現」當「因果確立」**：重現性排除隨機噪音，**不排除共變的第三因**（兩輪之間還隔著運轉、機器狀態、環境）。⇒ 🔴 **通則：A/B 之前先量「A 自己的變異」** —— 只要改門檻前多跑一輪 750，就會發現 67% 不可重現，整段冤枉路可省。✅ 四輪中唯一穩定的對照是**姿態**：750 兩輪 ≥8cm 皆 0/6，1250 兩輪為 3/6、2/6，而 **IMU 可用率沒有跟著走**（輪 4 僅 8% IMU 卻姿態最好之一）⇒ **支持「姿態主要由 `level_diff` 決定、IMU 是次要」**。⚠️ 但 n=2 vs n=2，**同樣不是定論**：維持 750 的理由是「它是原值且無證據支持改動」，不是「已證實較好」。⚠️ 仍**未修**（維持 750）：要真正評估需「固定隧道品質、只改門檻」，**而那在電氣干擾處置前做不到** —— 原排序的理由再次被證實。 | `Crane_control_PI/main.cpp:920` | **維持 750（1250 已試並回退）** ✔ | 2026-09-08 |
| 🔴 | 🆕 **吊機 watchdog 守不到它該守的東西 —— 而且比 09-08 記的更徹底** —— 2026-09-09 由 `AI-2` 逐行診斷、`agent-ai-60` 獨立複驗。✅ **「心跳不分連線來源」成立，且是最強的形式**：`touch_heartbeat()` **全檔單一呼叫點**（`main.cpp:4984`，`on_receive` 第一行、在解析成指令之前），`last_ping_ms` 是**單一全域 atomic**，`:5002` 上 5 條 client（本體 3 + web_backend 2）全部餵同一個 —— 不是「分不清楚」，是**設計上任何 byte 都算數**。🔴🔴 **決定性的一點**：餵它的那條連線**根本不過網路** —— `web_backend` 跑在吊機自己身上（`runbook.md:137`/`:795` 皆 `CRANE_IP=127.0.0.1`；那是刻意設計，為了本體掛掉時還能救援），GUI 輪詢走 loopback ⇒ **這個 watchdog 在結構上不可能偵測任何網路故障**，不只是「被 GUI 蓋住」。另 `server.js:171` 每個 bridge 還無條件每 10 秒送一次 ping。⚠️ **09-08 的敘述要修正：roll 從來就不在它的監看範圍內** —— roll 有自己的獨立時效檢查（`imu_roll_fresh()`／`IMU_ROLL_STALE_MS=750`／`main.cpp:1581` 每 tick 重判），而且**當天它正常運作**：「平衡全程退回 `src=meter`」正是它在動作。📌 **正確說法：watchdog 沒響不是因為 roll 的問題被蓋住，而是 watchdog 從來不看 roll** —— 它問的是「還有沒有人跟我講話」，而因為 localhost，那個答案**永遠是有**。⇒ 真正的缺陷是：**吊機端沒有任何機制在監看「本體那條控制鏈路還活著嗎」**。⚠️ 另附：`main.cpp:1738` 沒有任何 client 時**直接 `continue`**，連 `state=idle` 都不發（又一層「本機行程活著就當沒事」）。🟢 **修法建議（AI-2，我同意）：先做選項 2** —— 只在 `cmd_status` 加分來源的 `peer_age_ms` 欄位 + 超標發 EVT 但**不 abort**，照抄既有 `imu_roll_age_ms` 的形狀。理由：**訂門檻需要「本體鏈路實際斷多久」這個數字，而目前沒有人有**（09-08 量到的 7.2 秒是 roll age，不是鏈路 age）。選項 3（改看最後一筆有效感測資料）不建議，語意會打架。📌 **本條不需要排在干擾後面**，它不涉及調鬆任何門檻。 | `Crane_control_PI/main.cpp` watchdog | **未修；已完成逐行診斷 + 獨立複驗，修法待拍板** ✔ | 2026-09-08 |
| 🔴🔴 | 🆕 **本體端「監看吊機連線」的 watchdog 是死碼 —— 反方向也沒有存活偵測** —— 2026-09-09 由 `AI-2` 發現、`agent-ai-60` 獨立複驗確認。`crane_last_ok_ms_` ＝ **1 宣告 + 1 初始化 + 3 處寫入**（`WASH_ROBOT.cpp:786`／`:2418`／`wash_robot_commands.cpp:4822`）**+ 3 處註解 + 0 處讀取**；`WATCHDOG_TIMEOUT_MS`（`WASH_ROBOT.h:830`，2000）**只出現在自己的定義與兩行註解裡，沒有任何運算式用到**；`crane_watchdog_loop_()`（`:2784-2818`）只把張力警報排空，**沒有任何逾時比較**。🔴 **它曾經存在**：`9f33c6a`（2026-04-15）有完整的 `crane_cmd_("ping")` + `elapsed > WATCHDOG_TIMEOUT_MS → abort_flag`，到 `4d1409c`（06-22 那個把一個月壓成一筆的大 commit）就沒了，中間的移除點無法再細分。⚠️ **而餵它的機構全部留著還在長大**：`crane_keepalive_loop_` 1Hz、`:2418` 的 `motion_progress` 刷新（2026-05 為了「避免 false-abort」特地加的）、三段解釋「為什麼要避免誤觸發」的註解。⇒ **一個沒有消費者的計時器，養著一整套餵食機構，還附三段解釋為什麼要餵它。**🔴 **後果：吊機↔本體這條鏈路目前兩個方向都沒有可運作的存活偵測**，「隧道斷掉時本體會不會自己停下來」**目前沒有答案**（`crane_cmd_` 失敗路徑是否構成替代保護已另派 AI-2 追查；但那是「送指令才會發現」，**不是背景偵測**，hold／等待期間沒有覆蓋）。🔴 **必須拍板：補回來，還是明確刪掉。不能留現狀** —— 現狀最糟，因為它看起來像有保護。📌 **通則：判斷一個保護機制在不在，要找「誰讀這個值」，不是找「誰寫這個值」** —— 寫入點、常數、餵食執行緒、解釋註解可以全部健在，而**比較那一行不存在**；`grep` 到符號會讓人以為它活著。 | `app/WASH_ROBOT.cpp` `crane_watchdog_loop_()`、`WASH_ROBOT.h:830` | **未修；待拍板（補回 or 刪除）** ✔ | 2026-09-09 |
| 🔴🔴 | 🆕 **`crane_cmd_` 的收包路徑沒有行緩衝 —— 會把 `EVT` 廣播的半行殘片當成指令回覆** —— 2026-09-09 實地咬到：`init` 的 `water_inlet off` 三次「失敗」，而三個回覆都是 `EVT motion_progress` 的**尾段**（`ss phase=main_loop…`／`_cm=220 r_cm=219…`／`t_cm=223…`），開頭被切掉。🔴 **真因其實很明確**：該機水路 PQW 不在線（開機 `EVT device_state` 就寫著 `pqw_water=0`），乾淨連線直接問吊機得到 **`ERR pqw_water_offline`**。⇒ **本體因此分不出「裝置不在線」與「通訊亂掉」** —— 明確的診斷被降級成 `valve state UNKNOWN`。📌 **與 `[2026-09-09m5]` 在 `imu_push_loop_` 補的是同一個缺陷**（`recv` 單次最多 127 bytes，而 `EVT device_state` 一行遠超過 ⇒ 必定被切），那裡補了、**這裡沒有**。🟢 修法已知且已在本樹用過三次（`crane_cmd_` 自己的 drain 迴圈、吊機 `on_receive`、`server.js` 的 `state.buf`）。 | `app/WASH_ROBOT.cpp` `crane_cmd_` 收包段 | **未修（已定位，屬 C++ 線）** ✔ | 2026-09-09 |
| ❌ | ~~**滑台橫走 100cm 會把機體推歪約 4°**~~ 🔴 **2026-09-09 當日證偽，per user 指出：滑台橫走時吸盤是吸著的**（機體錨定在玻璃上）。而 `停後roll` 是下降 40cm **之後**量的、推桿已收回、機體懸空 ⇒ 量的是自由懸吊姿態，與滑台無關。**兩組證據**：① `mission_run` 兩輪無滑台無吸盤，傾角照樣累積到 +3.72°；② `RAIL_CM` 100→50 實測，左右差 中位/p90/max 由 4/9/10 → 3/8/**11**（沒改善、最大值更糟），`停後roll` 仍 +3°。⇒ ✅ 正解：**+3~4° 是繩長接近齊平時的自然懸吊姿態**。📌 **我的錯誤形狀：看到「每步都出現」就去找步驟裡的成因，沒先問「沒有那個步驟時它出現嗎」**——而我自己稍早的資料就有答案。原文保留如下 —— **滑台橫走 100cm 會把機體推歪約 4°**，左右差因此摸到韌體硬限** —— 2026-09-09 1 週期實測：開跑前姿態修到 −0.49°，**一個週期後每步 `停後roll` 都是 +3~4°**。手臂全程未動（`arm_attached=off`，`arm_park: OK skipped`）⇒ **就是滑台 0→100→0 本身**。🔴 左右差分布因此惡化：中位 4／**p90 9**／**最大 10**（＝韌體 `length_diff_max_cm`）／≥6cm **33%**，對比 09-02 `0902d` 的 中位 3／p90 4／max 5／≥6cm **0%**。📌 **機制**：平衡迴路要修一個**持續被滑台推出來**的傾角，就得製造更大的左右差 —— 這與 `cycle_test.py` 註解記的「監看的正是控制器用來修正的那個量」一致。🟢 **`m6` 的持續性判定在本輪救了 2 次**（`超標後自行回復 2 次`）—— **沒有它這輪會中止**。🟡 選項：縮小 `RAIL_CM`（09-03 由 50 改 100）／調整滑台速度／接受並靠平衡迴路吃掉。 | `Linux_test/cycle_test.py` `RAIL_CM`；滑台控制 | **待決策** ✔ | 2026-09-09 |
| 🔴🔴 | 🆕 **`roll_recover` 的守衛用「繩長齊平」當代理指標，而這台機器的水平點不在齊平** —— 2026-09-09 10 週期在**週期 1 步 4** 中止。中止本身正確（roll 連續 3 筆 >6°，最後 8.00°，持續傾斜非擺盪），自動修正也動了（`roll_correct 2`：8.04° → 3.89°），但撞到守衛「繩長已齊平卻仍歪 ⇒ 不是繩長造成的」就收手。🔴 **本日五個獨立資料點證明這台機器齊平時就是歪 +2.9~3.8°**：頂端齊平 +3.75°／`roll_correct 1` 後差 4cm → −0.51°；高度 53 齊平 +3.73°／修正後差 3cm → +0.63°；**上行過程（無人下指令、平衡迴路自己動的）齊平三段 +3.25/+3.36/+2.89°，一有 2cm 差就掉到 +0.75°**。⇒ **守衛把繩長往齊平修 ＝ 往最歪的方向修**，然後在最歪處宣告「不是繩長問題」並中止 —— **三個步驟共用同一個錯誤前提**。⚠️ **守衛的原意是對的**（防無止境動繩），錯的是**代理指標**。🟢 **修法建議：改用「這次修正有沒有讓 |roll| 改善」當停止條件** —— 那是可以直接量的，**不需要知道水平點在哪**。 | `Linux_test/cycle_test.py` `roll_recover` | **未修（已定位）** ✔ | 2026-09-09 |
| 🔴🔴 | 🆕 **平衡迴路在繩子不動時完全不跑 —— 而滑台橫走正好是繩子不動的那 6.3 秒** —— `hold_loop()`（`Crane_control_PI/main.cpp:2359`）的 `apply_balance_trim` 被 `if (cur_sync_dir != 0 && prev_sync_dir == cur_sync_dir)` 圈住，`cur_sync_dir` 是 hold 方向（0＝沒有在動）⇒ **滑台 0→100→0 期間一個 balance tick 都不會跑**。🎯 **這是「每步 `停後roll` 都 +3~4°」與「左右差惡化到 p90 9／max 10」的共同根因**：機體被滑台甩歪時沒有任何東西在修，要等下一個 40cm 移動才補一部分，而平衡迴路一動起來就要修一個已經累積好的大傾角 ⇒ 製造更大的左右差。🟡 選項：① 縮小 `RAIL_CM`（09-03 由 50 改 100）減少擾動 ② 讓平衡在靜止時也能修（**行為變更，等於新增一個定位保持模式，要謹慎**）③ 處理重心偏移本身（實體）。 | `Crane_control_PI/main.cpp` `hold_loop`；`cycle_test.py` `RAIL_CM` | **待決策** ✔ | 2026-09-09 |
| ✅ | ~~**靜止時無法做小於 1cm 的姿態修正（計米器量化下限）**~~ —— 🎯 **2026-09-09 已修：新增 `roll_trim_ms <±ms>`**（`changelog [2026-09-09c1]`）。**細度來自時間不是降 Hz** —— per user「**5Hz 幾乎吊繩不會動**」⇒ 10Hz 是實務下限。實測 **~0.3~0.45°/100ms**，比 `roll_correct` 的最小步階（3~4°）**細 10 倍以上**；🎯 **100~150ms 時 `L−R` 讀數完全不變、但 IMU 量得到** ＝ 繞過 1cm 量化的直接證據。安全：走既有 hold 旗標 ⇒ **沿用 `hold_loop` 的張力保護**（刻意不走註明「debug，無張力安全」的 `pay_out_left|right on|off`）；上限 500ms、`motion_active` 時拒絕、一律 `hold_all_off()` 收尾。🔴 **但它的效果只有 IMU 量得到** ⇒ IMU 不在線時不可驗證，呼叫端必須退回 `roll_correct`。**與 per user「IMU 連不上就放棄」一致：粗調永遠可用，細調是加分項。**🟡 **尚未接進任何腳本**（`cycle_test` 的 `roll_recover` 仍用 `roll_correct`）。 | `Crane_control_PI/main.cpp` `cmd_roll_trim_ms` | **已修，待接入腳本** ✔ | 2026-09-09 |
| 🔴🔴 | 🆕 **`level_diff` 必須在每次計米器歸零後重量，而今天早上歸零後我沒有重量** —— `g_fine_adjust_level_diff_cm` 的註解（09-01）明寫「**每次歸零計米器之後都要重量**」與「**任何一次計米器歸零都會靜默地移動這個目標，而且不會有任何徵兆**」。而本日第一件事就是 `zero_meters ground` + `top` ⇒ **舊值必然失效**，我卻花了一整天追由此產生的偏移。✅ 已照註解程序實機量測並設定 **`level_diff = 6`**（三點內插，斜率 −1.10°/cm）。🔴 **這條要進「歸零計米器」的操作程序**：歸零之後**必須**重量水平參考偏移，否則整條平衡鏈的目標是錯的。🔴 **且不持久化** —— 重啟吊機即回 0（本日重啟後實地確認全部掉光）。🎯 **2026-09-09 傍晚已解除**：重新定義工作區基準時**在機器水平的狀態下**歸零 （頂端 per user 手動調平 roll +0.03°／L−R=+6 → 底端 `roll_trim_ms -100` 修到 +0.37°／L−R=+6 → `zero_meters ground`），⇒ **新座標系裡 `L−R=0` 就是水平、`level_diff=0`**，而 0 正是編譯預設 ⇒ **重啟不再靜默弄歪機器**。🔴 **新規則：歸零一定要在機器水平時做**（兩支同時歸零 ⇒ L−R 座標平移當下的 L−R）——這把「必須記得重量」降級為「歸零前先調平」，**後者現場看得見，前者看不見**。⚠️ 保護只對這次歸零有效；日後在傾斜狀態下歸零就沒了。🔴🔴 **2026-09-09 晚間更正：設進去的 6 從來沒有真正生效** —— overrun pass 漏套 `level_diff`，把它夾死在 2cm（見下一列）。本日所有「`level_diff=6` 很好」的觀察，量到的都是 **2cm** 的效果；6 這個值**尚未被施加過**。 | `Crane_control_PI/main.cpp:421`；操作程序 | **值已設，程序待寫入 runbook** ✔ | 2026-09-09 |
| ✅ | ~~**急停旁路通道未連線 + ACK 被 EVT 蓋掉 + 接收緩衝只漲不消**~~ —— 2026-09-09 三段全修：① `crane_stop_estop_()` 改**有界自救**（`connectWithTimeout` 300ms，走 `reconnectLoop` 同一份 `nb_connect`）；② 送前排空、送後**逐行掃跳過 EVT**，並把「沒送出去」與「送了沒收到確認」分開印；③ `crane_watchdog_loop_` 每 500ms 週期性排空（`try_lock`）——**這條是防死鎖**：該 client 不讀 ⇒ 緩衝滿 ⇒ 吊機 `send()` 阻塞，而 `TCP_server::broadcast()` 持著 `clients_mtx` ⇒ 整條 EVT 路徑卡死。④ `status` 新增 `crane_estop_connected` / `crane_estop_down_ms`（先前這條安全通道的死活完全看不見，壞了 40 分鐘是查別的事才發現）。🔬 實測：運動中 `Recv-Q` 連續 10 次取樣全 0；吊機重啟後通道自己接回；`emergency_stop` 實跑吊機收到 `stop`、本體零警告。 | `app/WASH_ROBOT.cpp` `crane_stop_estop_` / `estop_drain_locked_` / `crane_watchdog_loop_`；`transport/TCP_client.{h,cpp}` | **已結案** ✔ | 2026-09-09 |
| 🔴🔴 | 🆕 **`fine_adjust` 的 overrun correction pass 漏套 `level_diff` ⇒ 參數被靜默夾死在 2cm** —— 起點是 per user 現場觀察「**每次往下走到定點，第一次修正後是平的，第二次修正就讓它變歪**」。`motion_fine_adjust_sync` 的收斂目標在「已對齊座標」裡（`curL_adj = curL_init - level_diff`），**收斂迴圈**的 `L_stop_at` 有把偏移加回去 ✅，**overrun pass** 卻用原始讀值比：`errL = finalL - target_cm` ❌ ⇒ `errL` 恆多出 `level_diff`，`level_diff=6` 對上 `FINE_ADJUST_TOLERANCE_CM=2` 時 `|errL|>2` **永遠成立**，每一次繩索運動結束都判定假過衝、把左繩收回到 `L−R≈2`。🔬 **實機證據 `cr_0909d.log` 最後 6 段 6/6 全中**：收斂後 L−R = 5/6/6/5/4/6 → overrun 後一律 **2/1/2/2/2/2**。其中一段左側判定 `stop(at-target)`、本來一步都不用動，仍被驅動了一次（**每次運動都有多餘起停**）。📌 **這推翻了本日「`level_diff=6` 讓 10 週期第一次跑完」的機制解釋**：`level_diff=0` 時 `errL=0`、overrun 不動作 ⇒ 真的收在 0；設 6 時被夾到 2 ⇒ 實際交付的是 **0→2cm**。✅ 修法：`:2738` `errL = (finalL - level_diff) - target_cm`、`:2827` `new_err = (curL - level_diff) - target_cm`（右側兩處不動；`level_diff=0` 時逐位元相同）。✅ **2026-09-09 15:53 已建置部署**：新 md5 `a5b88123`（舊 `1e47a947` 備份於 `crane_control_PI.out.prev-20260909-1552`），只重啟吊機（tag `0909e`）、本體靠 `reconnectLoop` 自行接回有線 192.168.1.10；init 全 OK、VFD keepalive 兩側 `ok=50 fail=0`；執行期值已補回 `motion_hz=30`／`roll_correct_hz=30`／`level_diff=6`。🔬 **驗證運動尚未做**。部署當下基準 `L−R=+2`、`roll=+4.22°` ⇒ 可證偽預測：第一次繩索運動應收斂到 **L−R=6**、roll 落到 **≈0**（4cm × −1.10°/cm）。🔴 **部署後 `level_diff` 必須重新量**，不可沿用 6 —— 建議先設 2（＝現行實際行為）跑對照，再往上試。 | `Crane_control_PI/main.cpp:2738`、`:2827` | **已部署，待一次驗證運動** ✔ | 2026-09-09 |
| ✅ | ~~**`0.85°/cm` 換算的定義／正負號要釐清**~~ 仍未結案，但 🆕 **2026-09-09 取得一個矛盾的實測值**：`roll_correct 1`（1cm）讓 `raw_x` 由 **+3.75° → −0.51°**，即 **−4.26°/cm**，與記載的 **−0.85°/cm** 差 **5 倍**。⇒ **至少有一個的定義是錯的。** ⚠️ 不以本次取代舊值：一個資料點不能推翻另一個，且本次由 3.75° 大偏差起跳、可能不在線性區。📌 **正負號未結案，所以我沒有用那個數字算方向**，而是先下 1cm 最小步實測往哪邊動（與驗下降方向同一套）。📌 **順帶反例**：本次**水平時張力反而更平均**（差 29.2 → 8.7 kg），與「水平與張力平衡互斥」的敘述相反（條件不同，記為反例）。 | — | **待釐清** ✔ | 2026-09-09 |
| 🟡 | 🆕 **`mission_run.py` 的中止語意曾與 `cycle_test.py` 不一致 —— 已修，但只驗證了一半** —— 2026-09-09：`mission_run` **1 筆超標就中止**，而 `cycle_test` 早在 09-04 就改成**連續 3 筆**並驗證過（10d「超標後自行回復 1 次」）。**同一個修正從來沒有被帶到另一支腳本**。✅ 已移植（`changelog [2026-09-09m6]`），門檻值不動（5.0/8.0），`tension_valid=0` 維持瞬時中止。🎯 **diff 那一半已驗證**：10 趟那輪第 7 趟下 `Δmax=9 > 8` 但 3 筆內回復 ⇒ 任務繼續；**改之前會在第 7 趟掛掉**。🔴 **roll 那一半仍未驗證**（全程未達 5.0，nearmiss=0）⇒ 「roll 超標後會不會自己回復」仍無答案。📌 **通則：一個修正「跑過了」不等於「被驗證了」——要看它負責的那條路徑有沒有被走到。**這次剛好有 nearmiss 計數當觀測點；**沒有那兩個數字就會被當成修好了**。⇒ 設計防護時順便設計「它有沒有作用」的觀測點。 | `Linux_test/mission_run.py`／`cycle_test.py` | **已修，roll 半邊待驗證** ✔ | 2026-09-09 |
| 🟡 | 🆕 **計米器漂移會累積，端點容差遲早撐不住** —— 2026-09-09：10 趟（20 次橫越）由 **223 → 226**，累積 **+3cm**（單趟來回約 2cm，與 09-01 記的「單側收繩固定過衝約 1cm」一致）。`TOL=5` 這輪撐得住，**但它會累積** ⇒ 連續跑更多趟或多輪之間不重新歸零時會漂出容差，屆時 `plan_leg` 會回 None、腳本拒跑（守衛正確，但會變成例行阻礙）。⚠️ 另：跨距三次量到 **231（09-03）／221／224**，**`TOP` 不是精確常數**。 | `Linux_test/mission_run.py`／`cycle_test.py` 的 `TOP` | **待決（要不要每輪重新歸零）** ✔ | 2026-09-09 |
| 🔴🔴 | 🆕 **本體端對吊機「零背景偵測」是兩個獨立改動疊出來的，不只 watchdog 死碼** —— 2026-09-09 `AI-2` 追查、`agent-ai-60` 逐條複驗原始碼。① **`crane_keepalive_thread_` 自 2026-05-15 起就沒被啟動**（`WASH_ROBOT.cpp:363-370` 整段註解掉，原文寫著 `New design: no continuous ping. Each crane_cmd_ self-heals on fail.`）。🔴 **這個決定本身沒錯**（是為了解決 zombie TCP socket 造成的 watchdog 誤觸發），**錯的是前提**：「會有 `crane_cmd_`」只在動作流程中成立。② **`crane_watchdog_loop_` 沒有任何背景輸入源** —— `handle_crane_evt_` **全樹單一呼叫點** `WASH_ROBOT.cpp:783`，位在 `crane_cmd_` 的收包 drain 迴圈裡；而吊機的 `broadcast_evt` 是推播給所有 client 的 ⇒ **沒有指令在途時，吊機廣播的 EVT（`tension_alarm`／`tension_total_limit`／`watchdog_timeout`）全部堆在 socket 緩衝區沒人讀**。那條「背景」執行緒＝**每 500ms 醒來檢查一個只有在別人講話時才會變的旗標**，而開機照樣印 `[OK] crane watchdog started`。⇒ 🔴 **與死碼那列合起來：背景偵測整條消失**（同一時期的兩個改動，各自看都合理）。**分界不是「隧道斷多久」，是「本體當下有沒有正在送指令」** —— hold／閒置／等 `PausedOnError`／本體自己在跑推桿走行期間，**一個 `crane_cmd_` 都不會送 ⇒ 零偵測**。🟢 **最低成本修法（AI-2 建議，我同意）：`imu_push_loop_` 已經每 250ms 在讀那條 socket（`WASH_ROBOT.cpp:2941-2946`）、讀完直接丟掉** —— 改成丟給 `handle_crane_evt_` 即可，**不新增任何流量**（刻意避開 05-15 那個 zombie socket 誤觸發問題）。 | `app/WASH_ROBOT.cpp:363-370`／`:783`／`:2941` | **未修；待拍板** ✔ | 2026-09-09 |
| ✅ | ~~**兩條急停路徑的回傳值被直接丟棄，而且走會被 `crane_mtx_` 卡住的主通道**~~ —— **2026-09-09 已修**（per user「依建議處理」），`changelog [2026-09-09m3]`。🔎 **我直接證實了 AI-2 標為「未實測」的那條**：`crane_cmd_` 第 733 行是 `std::lock_guard<std::mutex> lk(crane_mtx_)` **無條件阻塞取鎖** ⇒ 急停走主通道不是失敗，是**等**。🔴🔴 **並查出更根本的一件事：那條旁路整條是死碼** —— `crane_retract_safe_` **有 0 個呼叫點**（三法交叉複核，正對照 `crane_cmd_` 命中 22/10/17）⇒ `crane_cli_estop_` **在執行期從來沒被建立過**⇒ 兩條急停用主通道**不是疏忽，是當時只剩主通道**。📌 成因：`crane_retract_safe_` 的張力保護已正確搬到吊機端（`g_retract_tension_stop_kg`），但**張力監控與旁路通道被綁在同一個函式裡**，前者退場時把後者一起帶走。改法三處：抽出 `crane_stop_estop_()`（三份實作→一份）／兩條急停改走它、失敗發 EVT 不 fallback／🎯 **init 預熱那條 socket** —— 因為 `connectToServer()` 是**無逾時的阻塞 connect**，在 helper 裡現場連會讓不可達的吊機**卡住急停 ~127 秒**，正好是最需要它的情境；改成 init 連一次交給 `reconnectLoop` 維持，**helper 完全不 connect** ⇒ 急停最壞 1.5 秒，有界。✅ 完整建置 **16/16 objs**（刻意不用增量，因為改了 header）。🔴 **未部署、未實機驗證** —— 驗收要看開機的 `[OK] crane estop bypass channel connected`，以及真的製造一次急停。 | `app/WASH_ROBOT.cpp`／`.h`／`app/wash_robot_commands.cpp` | **已修，待部署驗證** ✔ | 2026-09-09 |
| 🔴 | 🆕 **兩個死碼函式要拍板：刪除還是復活** —— `crane_retract_safe_`（0 呼叫點）與 `crane_retract_to_weight_`（`WASH_ROBOT.h:2256` **只有宣告，全樹無定義、無呼叫**）。⚠️ **C++ 只宣告不定義且沒人呼叫不會連結錯誤 ⇒ 編譯器不會告訴你。**`[2026-09-09m3]` 只是讓其中一段實作被 `crane_stop_estop_()` 取代，**沒有動它們的存廢**。📌 **通則：一個函式同時承載兩個關注點時，其中一個過時會把另一個一起帶走，而且沒有徵兆。** | `app/WASH_ROBOT.cpp:2630`／`app/WASH_ROBOT.h:2244`·`:2256` | **待拍板** ✔ | 2026-09-09 |
| 🔴 | 🆕 **hold 期間操作者掉線，繩子不會停 —— 而 2 秒門檻的存在理由正是為了防這件事** —— 吊機 watchdog 的門檻註解寫明 `hold = press-and-hold, must stop fast if GUI disconnects`，**而它做不到**（因為 web_backend 在吊機自己身上走 loopback，見上列）。釋放 hold **完全依賴瀏覽器主動送 off**：v2 有綁 `blur`／`visibilitychange`（切分頁、鎖螢幕有蓋到，比 v1 好），但**瀏覽器當掉／平板沒電／按住當下 WiFi 掉**這三種沒有任何一層會放開繩子。🔎 **複驗**：`web_backend/server.js`（270 行）**`off` 與 `hold` 皆 0 命中**（ugrep／GNU grep／Python 位元組計數三法一致，正對照 `socket` 命中 7 ⇒ 不是 grep 失效）⇒ **它是純透明橋接、根本不認識 hold 語意**，`ws close` 時不會補送 off；`TCP_server` 也沒有 disconnect 回呼；`hold_loop` 只管張力、沒有時間上限。⚠️ **09-09 查到的「閒置時本體→吊機單向丟包 80~100%」讓這條更該優先** —— 那正好是「**按下去的瞬間鏈路是冷的**」。🟢 修法可獨立先做且與 watchdog 怎麼修無關：`server.js` 在 `ws close` 補送 off（走 loopback 一定送得到）。 | `web_backend/server.js`；`Crane_control_PI/main.cpp` hold_loop | **未修；可獨立先做** ✔ | 2026-09-09 |
| 🔴 | 🆕 **`crane_attached_ = off` 會讓本體端所有吊機保護同時且靜默失效** —— `WASH_ROBOT.cpp:728` 在 detached 模式下回**合成的 `"OK skipped"`**，而呼叫端一律用 `rfind("OK",0)` 判成功 ⇒ **每一個檢查都通過**。設計意圖是 bench 測試（吊機沒接時不要整條流程卡住），本身合理；🔴 **風險在於它沒有任何持續提醒**：一旦 bench 關了忘了開回來，**看起來一切正常，而所有吊機側的保護都不存在**。📌 與 `FCV_EP_CRANE_HOST` 那條同族：**「忘記帶／忘記開」而且沒有徵兆的那種**。 | `app/WASH_ROBOT.cpp:728` | **未修（設計如此，待決定要不要加持續警示）** ✔ | 2026-09-09 |
| 🔴🔴 | 🆕 **MH300 Phase 3-a：`init()` 要無條件清 `0x2002`（換硬體前必做，一行等級）** —— 2026-09-08 逐項複驗發現計畫書比實作**落後三個 Phase**（1／2／4 皆已完成，換硬體只要翻 `main.cpp:127` 一個巨集），**而 Phase 3 的描述本身也是錯的**：「急停後被 base-block 卡死」在**正常路徑不會發生**（driver 早已補上 `releaseBaseBlockIfNeeded_()`，`MH300_inverter.cpp:233/240/244/254`，`clearAlarm()` 亦清，`:282`）。🔴🔴 **但洞還在而且更隱蔽：`base_blocked_` 是 process-local 軟體旗標** —— 急停後只要**程式重啟**（`exit` 正常收尾／crash／Pi 重開機），硬體 `0x2002` 仍 set 而新物件旗標為 `false` ⇒ `releaseBaseBlockIfNeeded_()` 直接 return，**B.B 永遠不清**。症狀：馬達不轉、**Modbus 寫入回報成功、無 fault code、無任何錯誤訊息**。🔴 **`init()` 補不了** —— 它只在**讀到 error code 才** `clearAlarm()`，而 base-block 是輔助命令位元、**不是 alarm**。✅ 修法：`init()` Mode B probe 成功後**無條件** `writeParam(REG_AUX_CMD, 0x0000)`，代價是每次 init 多一筆寫入。📌 **通則：凡是用軟體旗標追蹤硬體狀態的地方，都要問一句「行程重啟之後呢」** —— 補在軟體旗標上＝把硬體狀態鏡射成行程狀態，缺陷從「一直會發生」變成「只在跨行程邊界發生」，更難遇到也更難查。 | `user_lib/MH300_inverter.cpp` `init()` | 🟡 **2026-09-09 已實作 + `g++ -fsyntax-only` 通過，但未部署、未實機驗證** —— 現役是 SE3（`main.cpp:127` `CRANE_VFD_IS_SE3 1`），這條路徑不會被執行。**驗收要等換上 MH300：急停 → 重啟程式 → 下 run，馬達要轉。**順帶查證 SE3 無同型洞（`cu_mode_set_` 初值是安全方向；MRS 與 run 同一命令字）。`changelog [2026-09-09m1]` ✔ | 2026-09-08 |
| 🔴 | 🆕 **`FCV_EP_CRANE_HOST=192.168.5.25` 是「必須記得帶」的啟動參數 —— 也就是遲早會被忘記的那種** —— `[2026-09-08m2]` 之後有線自動探測**會成功**，不帶環境變數就會選有線 `192.168.1.10`，而有線在 VFD 運轉時掉 90% 封包。🔴 **失敗的樣子沒有徵兆**：init 全 `[OK]`、埠正常開、閒置一切正常，**只有機器真的動起來才壞**。✅ 已補進 `runbook.md` §A0 第 3 步指令與驗收判準（含兩種開機 log 的原文對照）。🟡 **長期解法待決（非已拍板）**：寫進啟動腳本／把編譯期預設暫時改成 WiFi —— 干擾修好之前都成立。 | `.claude/runbook.md` §A0；啟動腳本（未建） | **已緩解（runbook 已補），長期解法待決** ✔ | 2026-09-08 |
| 🔴 | 🆕 **console v2 拿掉 confirm 的三個副作用按鈕要不要補回（等使用者拍板）** —— per user「操作繼電器按了就執行」，但通用 `[data-cmd]` handler 是共用的，移除後**三個非繼電器按鈕一併失去確認**：`pwm save`（🔴 寫 QX-DO24 flash，**非 `0x00~0x0F` 暫存器實時寫 flash 且壽命僅 1~2 千次** ⇒ 誤觸消耗不可回復的寫入壽命）／`zdt_zero`（重新定義推桿原點，與**仍保留** confirm 的手臂 `INIT` 同類風險）／`rail_cfg_soft_enable`。⚠️ 並產生一處不一致：`rail-zero` 就在隔壁但有**自己** hardcoded 的 confirm ⇒ **它還是會跳窗**。📌 使用者原話範圍只到「操作繼電器」，這三個是被共用 handler 順帶掃到的。🔴 另附：**console v2 尚未經真人瀏覽器實測**，需使用者重新整理 `:8081` 後實按一次確認手感。 | `web_backend/public_v2/index.html` | **等使用者拍板** ✔ | 2026-09-08 |
| ✅ | ~~**連往吊機的位址有 5 處各自解析，探測選有線時會分裂**~~ ＋ ~~**「強制走 WiFi」是唯一表達不出來的意圖**~~ —— **2026-09-08 兩件都已修、已建置部署、已驗證正在跑**（`changelog [2026-09-08m1]`／`[2026-09-08m2]`）。① 抽出 `crane_endpoint_ip_()` 單一位址來源，5 處全走它 —— 🔴 **第 5 處是 `crane_retract_safe_` 的 OVERWEIGHT 分支（超重時實際送 `stop`）**，連到死位址等於**超重保護靜默失效**；而 `connectToServer` 是無逾時 blocking connect（實測卡滿約兩分鐘），**急停旁路存在的意義正是主連線塞住時還能停機**。📌 09-08 之所以沒出事只是因為探測失敗、三條剛好都落在 WiFi（**巧合，不是設計**）。② `ep::has_host_override()` —— 判準由「解析值 ≠ 編譯期常數」改為「環境變數在不在」；舊寫法在覆蓋值**剛好等於**常數時與「沒設」無法區分，而 `CRANE_IP` 本身就是 WiFi 位址 ⇒ 「強制走 WiFi」正好無法表達，且 log 照樣印 `override … = 192.168.5.25` 看起來像生效了。📌 **教訓：「值相同所以沒差」只在讀的那一側成立；在寫的那一側，它讓一個意圖無法表達。** 我先前看過這個分支並判斷成「無害的小瑕疵」，然後就被它咬了。 | `app/WASH_ROBOT.cpp`／`.h`；`common/endpoints.h` | **已修（09-08 建置部署驗證，現役）** ✔ | 2026-09-08 |
| 🟡 | 🆕 **右腳推桿阻力明顯高於左腳，假說是「右側離牆較近」** —— 2026-09-02 晚輪（每顆 50 次伸出）：右上 2025ms/1039mA、右下 2400ms/1089mA vs 左上 1650ms/874mA、左下 1500ms/882mA ⇒ 右腳為左腳的 **1.4~1.6 倍**時間、高約 20% 電流（早上同樣態，使用者當日檢修後**仍在**）。⏸ **per user 推測「右邊重心較重」——張力資料指向相反**（整天 左 52~62kg / 右 37~43kg，比值 1.4× **左邊重**，與續十二「重心偏左」一致），且**推桿水平、鋼索垂直，不同軸**。🆕 **吸附率交叉比對支持幾何解釋**：50 步中「只有右腳吸到」11 次 vs「只有左腳吸到」2 次（**5.5 倍**）⇒ 右側離牆較近 → 右推桿較早接觸玻璃、接觸後仍要推到指令位置 → 時間長電流高，同時右吸盤較易吸到。**一個原因解釋兩組獨立量測**；競爭解釋「左吸盤漏氣」解釋不了電流那一半。🔧 **分辨方法：懸掛時用尺量四個吸盤位置到牆面的間距。** | 機構（非程式） | **未查明** ✔ | 2026-09-02 |
| 🔴🔴 | 🆕 **風扇會干擾 `.22` 匯流排** —— 2026-09-02 兩份 log 一致：風扇開啟期間 `.22` 裝置（JC100 ×4、QX:9）錯誤密度是關閉期間的 **≈15×**（462 行/246 錯 vs 1341 行/48 錯），且風扇開啟後 **3 秒內** 兩顆 JC100 同時進 fast-fail。⚠️ 分母是 log 行數不是交易數，絕對比例偏誤，但效果量與時序都指向同一件事。🔴 **這推翻了同日先寫下的兩個推測**（「左腳接頭間歇接觸」「`.22` 匯流排本身有問題」——風扇關著時 JC100 連讀 10 次全一致）。**兩個候選機制未分辨**：① EMI 耦合進 RS485；② 電源下陷（電源架構 [B] EPP-200-24 同時供應「氣動、感測 I/O 與通訊介面」）。🔧 **分辨方法：量風扇啟動瞬間 [B] 的電壓** —— 掉得明顯＝②（要分電源），否則偏 ①（要屏蔽/接地/走線分離）。**處置完全不同，量了再動。** 🆕 **2026-09-02 晚間新診斷給出病因層級的證據**：135 筆非 timeout 錯誤分類為 `CRC 22 / ADDR_MISMATCH 12 / FUNC_MISMATCH 10 / MODBUS_EXCEPTION 1 / **SHORT_FRAME 0**`，且訊息附帶的位元組顯示是**正確位址被打壞幾個位元**（`0x08`→`0x28` bit5、`0x06`→`0x0E` bit3、`0x03`→`0x13` bit4），後續位元組仍在正確位置 ⇒ **線上位元級損毀（電氣），不是分片、不是錯位**。| 電氣（非程式）；影響 `.22` 全部裝置 | **病因已定性（電氣雜訊），機制未分辨** ✔ | 2026-09-02 |
| 🟡 | 🆕 **QX-DO24（`.22` slave 9，風扇）間歇性寫入失敗** 🆕 **2026-09-03 大幅縮小**：60 次開/關的四欄回讀測試證明**四類錯誤訊息裡只有兩類是真失敗**（`freq`/`duty`，≈7%），`control`/`readback` 兩類**事後狀態都正確**（11/11）＝回報問題。已據此修 `cmd_pwm_set`（回讀相符才降級為 `unverified`）＋ `cycle_test.py` 的 `fan()` 自動復原，當日 **10 週期跑完、零中止、復原只觸發 1 次**。🔴 剩下的 ≈7% 真失敗仍指向 `.22` 電氣問題；**新約束：本次全程風扇停止仍會失敗** ⇒ 風扇是放大因素不是唯一成因。優先度 🔴→🟡（不再擋住測試）。~~原記載~~： —— 2026-09-02 12:44 起寫入 **0/22**（讀取偶爾成功 ⇒ 失敗在「主站→模組」方向，長幀進不去）。逐一排除：網關死連線❌／`_pt` 分片❌（0 與 5 各 0/5）／slave ID❌／整台斷電❌／匯流排本身❌（同線 JC100 完美）／電力接線❌（per user 正常）。🔴 **13:22 換 binary 重啟後突然全好**（讀 8/8、寫 15/15、錯位消失），**per user 期間沒動任何實體** ⇒ 「485 接收端被燒」的推測收回（燒毀不會自癒）。⚠️ **同樣是重啟程式，13:05 沒用、13:22 有用 ⇒ 間歇、隨時可能再犯。**下次再犯時：新加的 FC `0x06` 退路會自動接手並印 `LOG_WRN`，`pwm restart` 是另一條短幀命令 | `user_lib/QX_DO24.cpp`；硬體 | **未查明（現已恢復）** ✔ | 2026-09-02 |
| ✅ | ~~**`cmd_pump` 會謊報成功**~~ —— **2026-09-07 查證：09-03 就修好了，本列從那天起就是過期的。** 修正走 `pqw_set_relay_verified_()`（回讀 + 最多 3 次重試）並額外回報實際通道狀態，見 `changelog [2026-09-03a]`；`~/bringup/facade_cleaning_v2.out`（Sep 4 10:44、`pid 1749` 正在跑的那支）`strings` 查得到新版才有的 `pump_set_but_readback_fail` ⇒ **已部署、已在跑**。🎯 **09-07 實機端對端複驗**：`pump on` → `OK ch2=1`，`relay_status` **4/4** 全 `ch2=1`（09-02 的失敗場景無法重現）；再 on/off 循環 ×3 逐筆一致；收尾還原後 3 次獨立回讀 `ch1..ch16` 全 0，與動手前基線逐欄相同。📌 **本列的教訓不在 bug 而在帳**：修好的當天沒有回頭改這張表，於是它替一個已修的缺陷多站了 5 天崗，還害交接單照抄。**修完要回來翻這一列。** ⚠️ 同批的 `zdt_release_stall` 瞬態（`.20` 重啟後頭幾筆 `ok=2 fail=2`）**仍未查**，已另立一列。 | `app/wash_robot_commands.cpp` `cmd_pump` | **已修（09-03 修、09-04 部署、09-07 實機複驗）** ✔ | 2026-09-02，2026-09-07 結案 |
| ✅ | ~~**`cmd_brush`(CH5) 與 `cmd_water_pump`(CH14) 無回讀**~~ ＋ ~~**`OK pump_set_but_readback_fail` 待拍板**~~ —— 🆕 **2026-09-07 兩件都由 GUI 線在 `changelog [2026-09-07m2]` 結掉**（我開 09-07 這列、當日結案）。① 兩支比照 `cmd_pump` 改走 `pqw_set_relay_verified_()` + 回讀回報。**補一個我當初沒提到的理由：`cmd_brush` 比 `cmd_pump` 更需要 —— 滾筒刷沒有任何下游感測可以推翻它**（幫浦至少還有 `vacuum_check` 當權威），假 OK 就是最終答案。② 🔴 **我當初的診斷位置是錯的，已更正**：我擔心 `OK …readback_fail` 開頭是 `OK` 會騙到 `startswith("OK")` 的呼叫端 —— **實查三個呼叫端沒有任何一個被騙**（我獨立複核過：`cycle_test.py:534-551` 解析的是 `relay_status` 的 `ch<N>=1`、缺欄位還會 `sys.exit(1)`；console v2 `:1589` 是 `/\bch2=1\b/.test(relay)`；8080 GUI 根本不解析 pump 回覆）⇒ best-effort 語意是被尊重的，**要修的不是字串**。真正的缺陷在 `pqw_set_relay_verified_` 的 `st.empty()` 分支**一行 log 都不印**，而同函式「重試三次放棄」那條**有**印 ⇒ **最該留下證據的降級路徑反而無聲**。已補在 helper（valve 等其他呼叫端一起受惠），語意不動。📌 **這列最值得留的一句：問「誰會被這個字串騙」比問「這個字串對不對」更快收斂。** 🔴 **未經硬體驗證**（只到 `-fsyntax-only`），需 VS 重建。 | `app/wash_robot_commands.cpp` `cmd_brush`/`cmd_water_pump`；`app/WASH_ROBOT.cpp` `pqw_set_relay_verified_` | **已修，待建置部署驗證** ✔ | 2026-09-07 開列，同日結案 |
| ⚪ | 🆕 **七支驅動裡只有 `QX_DO24` 有分片防護**（🔴 **2026-09-02 當日證偽為本專案的實際病因**：新診斷上線後 197 筆 `.22` 錯誤裡 **`SHORT_FRAME` 掛零**，實際是位址／功能碼的**位元級損毀**。結構性暴露仍在，但優先度由 🟡 下調 ⚪） —— `.20`/`.22` **都是 115200 8N1、`_pt` 都是 0**，網關依字元間隔打包（115200 下僅約 0.3ms）⇒ 回覆可能被切成兩個 TCP 段。只有 `QX_DO24` 用會累積分片的 `sendAndReceiveQuiet`，其餘六支（ZDT/PQW/DM2J/JC100/XKC/DY500）都是普通 `sendAndReceive`。⚠️ **`QX_DO24.cpp` 原註解宣稱「JC100/SD76/SE3 都是 9600、本模組是全專案唯一 115200」——那半句是錯的，而錯的那半句正是防護沒擴散出去的原因**（已於 2026-09-02 更正）。📌 症狀不會長成「分片」的樣子：截斷的幀在 JC100 表現為 **CRC error** ⇒ 要靠新加的 `SHORT_FRAME(len=N,expect=M)` 才分得出來。🧪 `_pt=5` 已在 `.22` 實測過但**量不到效果**（當時的故障與分片無關），已改回 0 | `user_lib/*.cpp`；`.20`/`.22` 網關 | **未修** ✔ | 2026-09-02 |
| ✅ | ~~**`cycle_test.py` 10 週期一次都沒跑完**~~，而且一輪比一輪早掛** —— 2026-09-01 三輪：`cycle10b` 掛在**週期9**、`cycle10c` 掛在**週期7**、`cycle10d` 掛在**週期6**（前兩輪是左右差 9cm>8，最後一輪是 roll 7.97°>6.0）。單週期兩次都完成。🔴🔴 **左右差分布在兩輪之間明顯惡化、中間沒有相關改動**：10c（36段）中位 2 / p90 5 / 最大 8；10d（35段）中位 **3** / p90 **7** / 最大 **9**；且 **10d 之內也在惡化**（週期1~3 的 Δmax 多為 1~5，週期4~6 出現 7/7/9）。⚠️ 依續七「run-to-run 變異 ≈ 效果量」的教訓**不逕稱機構退化**，但「單輪之內與跨輪之間同時越跑越差」值得當觀察重點。✅ 附帶：「連續 3 筆」修正驗證通過（10d 統計：「超標後自行回復（未達連續 3 筆）: 1 次」＝正確地沒為瞬態中止）。🆕 **2026-09-02 `cycle10_0902` 10/10 全部跑完**（60 段：中位 3 / p90 **5** / 最大 **6**、≥6cm 僅 3%、自行回復 **0** 次）＝昨天那個惡化沒有重演。🔴🔴 **但這輪全程沒有真空源**（`relay_status` 直接回讀 `ch2=pumpA` 為 0），定位是**無真空對照組**，**不可與 10c/10d 直接比對** —— 它只證明「吸盤完全不參與時，同套機構動作 60 段最大差 6cm」。⇒ **吸附/脫離是嫌疑最大的變因，但這是一次觀察不是結論**（另有三個混淆項：靜置一夜、開跑前做過一次乾淨的收回頂端、`level_diff` 由 4 改 5）。**下一步就是開幫浦跑同樣的 10 週期。** 🎯 **2026-09-03 補記：那一輪早就跑過了，結論與當時的嫌疑相反。** 09-02 當天在 `cycle10_0902`（10:01，無真空）之後還有三輪**全部開著真空 A 組**，但只有日誌沒收錄：`0902b`（12:32，21 段，🔴 中止 `extend_raw TIMEOUT`，收尾 `Connection refused`＝本體程式斷線）、`0902c`（12:45，6 段，🔴 中止 `pwm_freq_write_failed_no_reply_timeout`＝**QX-DO24 那條待辦當場發作**）、**`0902d`（13:45，10/10 全部跑完、60 段）**。⇒ **有真空 vs 無真空的完整對照成立**：`0902d` 中位 3 / p90 **4** / 最大 **5**、**≥6cm 0%**，對上無真空的 `0902` 中位 3 / p90 5 / 最大 6、≥6cm 3% —— **有真空不但沒惡化，還略好**。🔴🔴 **「吸附/脫離是左右差最大嫌疑變因」的假說被推翻**；09-01 那三輪（掛在週期 9/7/6、最大 8~9cm）比這兩輪都差，**真正的改善來自別的地方，不是真空**。⚠️ 兩輪中止都是**裝置故障**（推桿逾時、風扇無回應），與左右差無關 —— 中止原因要分「機構/姿態」與「裝置通訊」兩類看，不要混為一談。 | `Linux_test/cycle_test.py`；紀錄在吊機 `~/bringup/cycle_logs/` | **已結案（對照輪已完成）** ✔ | 2026-09-01（續十八）盤點、2026-09-02 更新、**2026-09-03 結案** |
| 🟡 | **跑 `cycle_test` 前必須先確認 `balance_source=imu`** —— 🆕 **2026-09-07 已改編譯預設為 `Imu`，但改動尚未建置部署** ⇒ **在下一次 VS remote build 之前，本列照舊有效、每次重啟仍要手動設**。部署之後本列即可降為只剩 `fine_adjust_level_diff_cm=5`（那一項仍是純執行期值、沒有編譯預設可改）。續八對照組：關掉 IMU 平衡 → 平均 |roll| 0.68°→**2.52°**、出帶 22.7%→**67%** ⇒ **沒設就跑，整輪數據不可比，而且看起來只會像「機構又變差了」**。🔴 **真正的根治仍未做：執行期參數不持久化**（`motion_hz` / `balance_source` / `fine_adjust_level_diff_cm` 三者同病），改預設只是把「重啟後落在哪」挪到對的那一邊。 | 操作流程；`Crane_control_PI/main.cpp:834` | **待部署後複查** ✔ | 2026-09-01（續十八），2026-09-07 更新 |
| 🔴 | 🆕 **單側在起步 1 秒內暴走 5cm（右繩張力掉到 7kg）** —— 2026-09-01 第 2 趟 `pay_out 20`：起步不到 1 秒、**一個 `[BAL]` tick 都還沒跑**（tick=250ms），右側多放 5cm（L+3/R+8），右繩張力一度 **7.07kg**（幾乎鬆繩），`tension_diff` 守衛於 51.0 中止。⚠️ **不是**續七那個「一側沒啟動」—— `sync_start` 啟動驗證通過、兩側都確認在轉。停止後右張力回到 22.6 ⇒ 那個 7 是運動中瞬態，但 5cm 位置差是真的。📌 假說（**未證實，n=1**）：起步瞬間重量分佈偏移 → 右繩卸載 → 開迴路 VFD 輕載下轉更快 → 放更多 → 正回饋。🔴 值得注意的是**平衡迴路在這個時間尺度上根本來不及作用**（250ms tick vs <1s 的事件） | `Crane_control_PI/main.cpp` motion_rope / balance tick | 🆕 **2026-09-09 量到兩次並更正發生率**：`mission_run` 第一趟下降 13cm 即 `roll -5.06°` 中止；10 趟那輪第 7 趟下 `roll 4.49° + Δ9cm`。⇒ **23 次下降起步中 2 次 ≈ 1/10**。🔴 **並更正我自己的統計框架**：先前說「>5° 只佔 0.0%」是**分母錯了** —— 瞬態只持續一瞬，用取樣筆數當分母必然稀釋它，**要用事件數**。📌 **並確認大 roll 與大左右差是同一個事件的兩面**（平衡迴路靠製造左右差修 roll）。🔴 靜置 5 分鐘刻意重現**失敗** ⇒ 「靜摩擦／靠牆被釋放」假說**已排除**。**未查明** ✔ | 2026-09-01（續十六）實測 |
| 🟡 | 🆕 **總張力讀值隨姿態變動超過 10kg** —— 同一台機器：歪 6.75° 時總和 81.9kg、水平時 92.8kg。✅ 這解釋了續十五記的「20cm 下降掉 4.7kg」＝**不是感測器漂移，是總和本身隨傾角變**（該列可結）。🔴 安全意義：`up_stop_total_kg=130` 吃的正是這個總和，而它會隨姿態浮動 10kg 以上 —— 訂門檻時要把這個浮動算進餘裕  | `Crane_control_PI/main.cpp` hold_loop | **已理解，門檻未調** ✔ | 2026-09-01（續十七） |
| 🟡 | 🆕 **單側量測收繩固定過衝約 1cm** —— `retract_right 3` 實走 4、`retract_right 2` 實走 3（同一晚、同一側、連續兩次）。與待辦表「`roll_correct` 致動解析度不足（指令 1cm 實動約 5cm）」同源但量級小得多，推測差別在 `side_measured` 短程走 10Hz 起步（log：`短程 3cm <= approach 8cm → 直接以 10Hz 起步`）。**下小步矯正時要預期多走 1cm** | `Crane_control_PI/main.cpp` `cmd_side_measured` | **未修（已知並可補償）** ✔ | 2026-09-01（續十七） |
| 🟢 | ~~**`balance_source` 的編譯預設是 `meter`，但 production 用的是 `imu`**~~ —— 🆕 **2026-09-07 已改為 `BalanceSource::Imu`**（`changelog [2026-09-07m1]`）。✅ **拍板依據：查證後確認「預設 Meter」不是 IMU 沒接時的安全退路** —— 退路在 `apply_balance_trim` 裡、而且是**每個 tick 重新判定**的（`use_imu = want_imu && imu_roll_fresh()`；`imu_roll_fresh()` 在「從未收到」與「>750ms 過期」兩種情況都回 false，退回的 else 分支與 Meter 路徑**逐位元相同**）⇒ **IMU 缺席時兩個設定跑同一段控制律**，改預設不會讓任何情境變得不安全。📌 原本 `Meter` 的理由是註解自己寫的「與現行逐位元相同」＝導入當天的 opt-in 開關，`git log -L834,834` 確認該行自 09-01 導入後一個字都沒動過。🔴 另一個依據：`LENGTH_DIFF_MAX_CM_DEFAULT` 已為 IMU 世界**永久**由 15 放寬到 10，留著 default=Meter 等於出廠組態是「計米器控制 ＋ 為 IMU 調過的守衛」＝沒有一邊被驗證過的組合。➕ 同時新增**開機大聲宣告**（`balance_source=` 一行 ＋ roll 未到時的 WARN），因為 `imu` 只代表「想要」，實際要看本體有沒有在推 roll。🔴 **尚未建置部署，且未經硬體驗證**（只到 `g++ -fsyntax-only`，含負控制）。 | `Crane_control_PI/main.cpp:834` | **已改，待建置部署驗證** ✔ | 2026-09-01 冷啟動實測，2026-09-07 拍板 |
| 🔴 | **`fine_adjust` 的 `level_diff`：機制已驗證、值已收斂到 5，但 🔴 可能根本不該是常數** —— 機制端對端確認（`pay_out 20` 實測：IMU 迴路自己停在 L−R=4，fine_adjust 算出 diff=0 正確不動作；**舊行為 `0` 會主動把機器拉到 L−R=0**）。斜率取得**三筆一致量測** −1.14／−1.05／−1.16 °/cm ⇒ **≈−1.1°/cm**，確認續十二 `−0.85` 的正負號正確（量值偏小）。反推水平點：`L≈74` 處為 **4.9 / 5.4 / 5.2** ⇒ 執行期已設 **`level_diff=5`**。🔴🔴 **但 `L≈50` 處反推是 ≈3.8** —— 下降 24cm、水平點移約 1.4cm ⇒ **單一常數撐不過 0→229cm 全行程**。（未證實：`L≈50` 那點在傾倒事件前、張力分佈不同，n 也小。）**⇒ 編譯預設維持 0，理由已不是「值不準」而是「它可能是函數不是常數」。** 🎯 **2026-09-09 傍晚結案：是常數。** 重新定義工作區基準後在新座標系兩端各量一次——**玻璃最底點 `L−R=0 → roll +0.26°`、最高點（+256cm）`L−R=0 → roll +0.12°`** ⇒ 跨 256cm 水平點只移 0.14°（≈0.13cm）。📌 舊證據（`L≈50` 處 3.8、`L≈74` 處 5.2）為何相反未解釋，但那取自傾倒事件前、n 小、且在舊零點座標裡；本次條件乾淨（同一次校正、兩端、靜止擺幅 0.00°），採信本次。✅ 且因為是在**水平狀態下歸零**，新座標系裡 `level_diff` 就是 **0** ＝ 編譯預設 ⇒ **不再需要常數化，重啟也不會弄歪。** | `Crane_control_PI/main.cpp` `motion_fine_adjust_sync()` | **已結案：是常數，且新基準下 = 0** ✔ | work_log 2026-09-01（續十四→十七），2026-09-09 結案 |
| ✅ | 🆕 **`0.85°/cm` 這個換算的定義／正負號要回頭釐清** —— 續十二由兩點（繩長差 3cm→+0.92°、8cm→−3.35°）得到 −0.85°/cm，今天多處推理靠它（含 `level_diff=4` 的取值）。但 2026-09-01 `pay_out 20` 實測L−R 由 3→4 時 roll 由 +0.91→+1.05（**+0.14°/cm，正負號相反**）。n=1 不足以推翻它，但足以說明**「繩長差」當時指的是不是 status 的 `length_left - length_right`，需要確認**（是 L−R？R−L？還是指令差而非讀值差？） | 量測定義；影響 `level_diff` 取值 | **2026-09-09 結案** ✔ | 2026-09-01（續十五） |
| 🟡 | 🆕 **20cm 下降造成總張力掉 4.7kg（93.6 → 88.9，−5%）而機器並沒有變輕** —— 疑為滑輪／繩索在不同繩長下的摩擦與遲滯。**刻度校正後 kg 有絕對意義、而且安全門檻直接吃它**（`up_stop_total` 130 / `retract_tension_stop` 75），所以這個 5% 的漂移值得單獨查一次，不要當雜訊放過 | `user_lib/DSZL_107.cpp`；機構 | **未查** ✔ | 2026-09-01（續十五）實測 |
| 🔴 | 🆕 **重心偏左：水平與張力平衡互斥，要決定怎麼處置** —— 2026-09-01 實測：機器歪 −3.35° 時張力 46.9/45.9（1.02×），調到水平 +0.92° 時變成 59.7/34.6（**1.73×**）。**機器要水平，左繩就得承擔約 1.7 倍重量；兩側張力相等的那個狀態，機器是歪的。**三個選項：**配重**（治本，但要往 94 kg 的機器再加重量）／**改吊點**（不加重量，但要動機構且牽涉 `FOLLOWER_SPAN_CM` 幾何）／**接受並讓控制器補償**（純軟體，但 `roll_correct` 的最小可執行步比死區大，迴路結構上收斂不了）。🔴 **在這件事決定之前，調任何控制參數都是在補症狀** —— 對照組已證實姿態誤差是**單向漂移不是擺盪**。📌 這也改寫了三個先前的判斷：①「左右繩長相等」不是目標；② 平衡迴路製造左右差是**幾何要求**不只是暫態；③「下行比上行差」可能同源（放繩時張力低，重心偏移被放大） | 機構決定；影響 `Crane_control_PI/main.cpp` 的平衡與門檻 | 📌 **2026-09-09 框架由 per user 更正：捲筒本來就是這樣，`計米器 + IMU 修正` 就是為補償它而存在的** —— 以下數字是**控制系統要對付的條件**，不是待修的缺陷。🎯 **本日把量級量出來了**：頂端四次靜止量測 `−0.27 → 2.01 → 2.99 → 3.72°`、張力差 `19.0 → 29.5 kg`（**每 10 趟來回約 +1°／+3kg**）；同一時段**頂端 3.72° vs 底端 1.38°（差 2.3°）**，靜置 1.7 分不回復。🔴 **而計米器完全看不到**（繩長讀值始終相等 −225/−225）⇒ **這正是平衡不能只靠計米器、必須有 IMU 的原因**。🆕 原始證據：10 趟來回收工後 **L=−226／R=−226（左右差 0）**，而 `raw_x=2.01°`、張力 **60.5 / 37.4 kg（差 23kg）** ⇒ **繩長相等時機器不水平**。跑之前靜止是 −0.27°，同一天同一台機器。**待決策** ✔ | work_log 2026-09-01（續十二） |
| 🔴 | 🆕 **`fine_adjust` 的收斂目標是「左右讀值相等」，而這台機器水平時左右不相等** —— `motion_fine_adjust_sync()` 用 `diff_init = curL_init − curR_init` 的**絕對差**收斂到 0（容許 `g_fine_adjust_diff_tol_cm` = 1cm），`align_lengths` 更是明寫 `target = max(L, R)`。但 ① 2026-09-01 實測**水平對應的是非零繩長差**（當時約 3cm）；② 兩支 SD76 的零點各自獨立，**絕對差裡還混著一個與繩長無關的固定偏移**（08-31 靜止不動就差 13cm，`length_diff_max_cm` 那條守衛當天就因此改成比「本次動作的相對位移差」——**`fine_adjust` 沒有跟著改**）。⇒ 它收斂到的那個點與「水平」沒有定義好的關係，目前接近水平只是現行零點偏移剛好抵銷；**任何一次計米器歸零都會靜默地移動它**。🔧 修法：給 `fine_adjust` 一個**水平參考偏移**（roll≈0 時的 L−R），收斂到該值而不是 0；預設 0 ＝ 行為與現在逐位元相同，另加 `set_*` 指令讓下次上機一步量到就能校。✅ **2026-09-01（續十四）實作已上線**（`g_fine_adjust_level_diff_cm` + `set_fine_adjust_level_diff` + status 欄位）——**本列原記「未實作」是過期的**，2026-09-02 校正。🔴 **剩兩步**：① **`fine_adjust` 自身的運動驗證仍未做**（09-02 上午那趟 74cm `retract` 收工 `L−R=+5 / raw_x=−0.81°` 落在 ±1° 內，但那是**平衡迴路**的功勞，`fine_adjust` 沒被呼叫到——不可拿來充當本項證據）；② 驗證後要把值寫進**編譯預設**，否則重開機回到 0，而 0 已知是錯的（穩定偏 +3.5°） | `Crane_control_PI/main.cpp` `motion_fine_adjust_sync()`:2340、`align_lengths` | **部分完成**（實作✅／驗證+常數化❌）✔ | 2026-09-01 讀碼發現（源於續十二的重心偏左） |
| 🟡 | 🆕 **刻度校正後，兩個「維持不動」的門檻含意變了** —— per user 2026-09-01「維持」，但校正之後：① `TENSION_MAX_KG_DEFAULT`(100) 是**單側**門檻而整機才 94 kg → **一條繩承擔全部重量也不會觸發**，它現在只擋得到「單繩受力超過整機重量」（卡住／被拉住），擋不到「另一條繩鬆脫」。要擋得到，值須落在 (75, 94) 之間。② `TENSION_DIFF_MAX_KG_DEFAULT`(50) 對上「水平時本來就有的 25 kg 差」→ **正常狀態就用掉一半預算**。📌 兩者都應該在「重心偏左」那列決定之後一起回頭調 —— 現在改等於對著會變的基準調  | `Crane_control_PI/main.cpp:472,475` | **值未動，已在原始碼註解記載** ✔ | 2026-09-01 常數化時發現 |
| 🟡 | 🆕 **`ATTACH_PAYOUT_TARGET_KG`(10 kg) 是校正前的單位** —— 新單位約 21~24 kg。目前不影響行為：使用它的 attach pay_out 整段自 2026-08-27 起是 `#if 0`（per user「attach 結尾不再放繩」）。⚠️ **把那段改回 `#if 1` 之前必須先換算**，否則 fallback 目標比預期低一半以上，pay_out 會一路放到 `ATTACH_PAYOUT_MAX_CM`(50cm) 上限才停，而且沒有任何錯誤訊息  | `app/WASH_ROBOT.h:1149` | **未修（刻意）；已在原始碼加警告** ✔ | 2026-09-01 常數化時發現 |
| ✅ | ~~**裸 send/recv 對還有四支：DM2J / PQW / XKC / DY_500**~~ ✅ **2026-09-01 全部改完**（ZDT 當日稍早已改並實機驗證）。四支都改走 `TCP_client::sendAndReceive()` 原子交易，逾時沿用原值。**本體不再有裸對驅動**（`DIHOOL_control` 除外——全 repo 無呼叫端＝死碼）。刻意保留裸送出的只剩兩處，都是「不配對接收」：ZDT `trigger_sync_move`（廣播無回覆）與 XKC `set_baud_rate`（手冊 §1.8 明載不回覆）。🔴 **順帶抓到 XKC 的既有缺陷**：它原本只驗長度與 CRC，**別的 slave 的回覆帶著合法 CRC 就會被收下**（與 08-28 稽核在 DM2J 修掉的同一類，當時漏了這支）→ 已補 slave id + FC 檢查。驗證：三個建置目標全過；假從站 `test_stage2` 5 驅動 × 5 模式 **25/25 PASS**、`test_dy500` 5/5、`test_dm2j` 全過。⚠️ **尚未上機**：兩支程式都還沒部署重啟  | `user_lib/{DM2J_RS570,PQW_IO_16O_RLY,XKC_Y25_RS485,DY_500_weight_sensor}` | **已修 ✔** | work_log 2026-09-01（續十一）→ 本次結案 |
| ✅ | ~~**張力門檻只在記憶體裡，重開程式會回舊值並直接擋住收繩**~~ ✅ **2026-09-01 已常數化**：`RETRACT_TENSION_STOP_KG_DEFAULT` 50 → **75**（實測單側最大 59.7，留 26% 餘裕）、`UP_STOP_TOTAL_KG_DEFAULT` 70 → **130**（實測總和 94.3，留 38% 餘裕；**舊值 70 已低於整機自重，一按 UP hold 就會立刻 `hold_all_off`**）。兩者相對關係與 08-28 當時一致（130 < 75×2），只是整組換算到校正後的單位。⚠️ **要重啟才生效**  | `Crane_control_PI/main.cpp:488,498` | **已修 ✔** | work_log 2026-09-01（續十二）→ 本次結案 |
| 🟡 | **真空幫浦 B 組（PQW CH3）實體存在但程式從未啟用** —— 2026-09-01 逐一通電實測發現：本體 CH3 被 `WASH_ROBOT.h` 與 `motion_flow.md` 雙雙記成「空通道（原左腳閥）」，實際是**幫浦 B 組**。`init` 只開 `CH_PUMP_A`(CH2) → **真空系統長期只有一半在運轉**。📌 **這很可能與吸盤密封一直要靠 `smart_extend_subset_` 反覆補伸（最多推到 ~16cm）才吸得住有關** —— 在查明之前不要再把那個現象直接歸因於機構或吸盤本身。⏸ **per user 2026-09-01：「幫浦先用 A 組就好，B 之後再規劃」** —— 刻意不啟用，等規劃。🔴 順帶拆掉一顆地雷：原註解寫「要改回雙閥只要把 `CH_VALVE_LEFT` 改回 3」，照做會讓二十幾處閥呼叫去驅動幫浦 B 組 | `app/WASH_ROBOT.h`（`CH_PUMP_B`）、`app/WASH_ROBOT.cpp` init | **記載已更正；啟用待規劃** ✔ | work_log 2026-09-01（續十）實測 |
| ✅ | ~~重連的非阻塞 connect 少了 `getsockopt(SO_ERROR)` → 連到沒人聽的埠也判定成功~~ | `transport/TCP_client.cpp` | **已修（本分支 `-drv4`）** —— 雙向斷言實機驗證：吊機關→假成功 0 次、吊機開→正常連上。⚠️ **原記「`refactor/app-layer` 上仍未修」是錯的**（2026-08-29 複查）：該修正已由 `56bfa5c` cherry-pick 進整理分支，兩條分支都有 | work_log 2026-08-28（實機驗證） |
| ✅ | ~~`init()` 印 `VFD left/right (MH300)` 是**寫死字串**，在 `#if CRANE_VFD_IS_SE3` 之外 → 旗標是 1（實際跑 SE3）卻印 MH300，會把人導去查錯的 driver~~ | `Crane_control_PI/main.cpp:4298,4300,4317,4319` | **已修（`f4e0d02`）**：四處都改吃 `CRANE_VFD_NAME` 巨集，隨 `#if` 一起切換。🔴 **未實機驗證**（改於 14:41，16:30 機器讓出前有過一次重編，但是否涵蓋本檔未經確認——不宣稱編譯狀態） | work_log 2026-08-28（實機驗證）｜2026-08-29 複查原始碼確認 |
| ✅ | ~~上滑台 cm↔pulse 換算錯 7.7 倍（皮帶軸 7.731 cm/圈，程式假設 1）→ 每次掃動下 131cm 指令、滑台只有 50cm，一路撞到底~~ | `user_lib/DM2J_RS570.*`、`app/WASH_ROBOT.*` | **已修（本分支 `-drv5`）**：換算層修正 + 行程守衛，實機量測指令 17→實際 17cm。⚠️ **原記「`refactor/app-layer` 上仍未修」是錯的**（2026-08-29 複查）：`9fa4fe1` 已 cherry-pick 進整理分支，兩條分支都有 | work_log 2026-08-28（實機量測） |
| ✅ | ~~**`refactor/app-layer` 已經不是「純整理、功能等價」了**——上機計畫的前提失效了卻沒有人被告知~~ 📌 **2026-08-29 使用者拍板：直接上 `fix/driver-crc`，不再分兩段。** 理由是要維持分兩段就得另開一條真正只有搬家的分支，代價大於收益。🔴 **代價已明確記錄**：上機若出現非預期行為，**不再能靠「哪一段出現的」來歸因** → 取而代之的是 `runbook.md` §A2 塊三那張「9 條刻意行為改變」清單，上機前先讀一遍。`runbook.md` §A2 已整段翻面（標題、前提、驗收判準——**舊判準「與 baseline 逐字一致」照用會整片報紅，而每一條紅都是設計好的**） | `.claude/runbook.md` §A2 | ✅ **已決並落文件（2026-08-29）** | 2026-08-29 複查帶出，同日拍板 |
| ✅ | **`ARM_SWEEP_DECEL_MASK_MS` 的減速遮罩從來沒有生效過** —— 它錨定在 `est_ms` 結尾，而 est_ms(4500) 比真實運動(553ms)長 8 倍，遮罩窗口(3500~4500ms)與真正的減速(528~553ms)**完全沒有交集**。changelog 顯示他們為假警報吃過苦（M2 path 最後被實質 disable），**其中一道保護一直是壞的而沒人知道** | `app/WASH_ROBOT.h`、`app/WASH_ROBOT.cpp:2504` | **2026-08-31 已修**:遮罩改錨定新的 `motion_ms`(真實運動時間,由實測導程推算)而非 `est_ms`。根因是**同一個數字被當成兩種語意**(監看逾時 vs 動多久)。`motion_ms<=0` 退回舊行為。⚠️ 編譯過但**未實機驗證**(需實際掃動觀察 tau) | work_log 2026-08-28 |
| ✅ | **`*_EST_MS` 與 `ARM_SWEEP_DECEL_MASK_MS` 是耦合的，天真調小會關掉障礙偵測**：`est_ms ≤ MASK` 時 `elapsed > 負數` 恆為真 → 整趟偵測全程關閉且無任何訊息。實測導程重算：17cm @ 250rpm 真實運動 **553ms**，現值 4500/3900 是 7~8 倍餘裕。🔴 **三方取捨（偵測覆蓋率／週期時間／運動被截斷）需使用者決定**，已把算式與對照表寫進常數註解 | `app/WASH_ROBOT.h` | **2026-08-31 已解耦**:遮罩不再錨定 `est_ms`,`est_ms` 純粹是監看逾時 → **調小 est_ms 不再會關掉障礙偵測**。原耦合(`est_ms ≤ MASK` → 條件恆真 → 全程關閉)已消失 | work_log 2026-08-28 |
| 🟡 | **上滑台的「零點」是 `init` 當下的位置，不是機械原點** —— 🔴 **2026-08-29 per user：原本寫的「真解是啟用 homing」是錯的，這台沒有原點感測器**，`home_start()`（`0x0020`）沒有東西可觸發。實際的保護是**斷電煞車＋作業流程（斷電前一律先移回 0 點）**，所以開機時滑台就在左端硬限位 → `0x0021` 設當前為零**是對的做法，不是缺陷**。✅ 實機佐證：`0x1003` 的 **`HOME_DONE=0`**（從未回零）、方向實測 **正方向=往右／0 點=左端**（與 `WASH_ROBOT.h:624` 註解一致）。🟡 **殘餘風險**：異常斷電／停電來不及回 0 時，下次 init 會把當時位置當成零點，**座標系整個偏移且無人被告知** —— 這是流程保證而非機制保證。🔴 **待辦改為：改寫 `WASH_ROBOT.h:622-625` 的註解**（勿再指向 homing），並評估要不要加「開機時提示確認滑台在左端」 | `app/WASH_ROBOT.h:622-625`、`app/WASH_ROBOT.cpp:6912` | **待改註解** ✔ | work_log 2026-08-28（更正）｜2026-08-29 per user 推翻原「真解」 |
| ✅ | ~~推桿 cm↔pulse 用 `20000/7 = 2857`，實測應為 **3000**（5% 系統誤差）~~ | `app/WASH_ROBOT.{h,cpp}` | **已修（`[2026-08-28n]`）**：新增 `CUP_PULSE_PER_CM = 3000.0`，兩處都改吃它。實機 47994 脈衝 = 16cm + 四條交叉驗證 | work_log 2026-08-28（實機量測） |
| 🟡 | `PUSHER_EXTEND_*` 常數的註解標的公分現在是對的（本來就用 3000），但**「12.0 cm」等標示仍未逐一複查**；另 `zdt_pusher extend` 實際走的是 `disable_seal` 尋封序列（可達 47994 脈衝／16cm），**不是預設的 36000** —— 文件與 GUI 說明都沒講 | `app/WASH_ROBOT.h`、runbook | **未修** ✔ | work_log 2026-08-28 |
| ✅ | ~~左右歸屬與實體不符（RF={5,6}/LF={7,8}），**交替步伐因此不可用**~~ | `app/WASH_ROBOT.{h,cpp}` | **已修（`[2026-08-28p]`）**：右={5上,7下}／左={6上,8下}，31 處使用點自動跟著正確。🔴 **尚未實機驗證**，第一次跑交替步伐要有人在旁邊 | work_log 2026-08-28 per user |
| 🟡 | `group_seal_ok_` 的「4 顆有 2 顆吸住就算 OK」是為了繞過「分側判準算不準」而採用的（2026-08-28）。**歸屬修好後那個前提消失** → 是否改回「每側各 ≥1」需使用者決定 | `app/WASH_ROBOT.h` | **待決定** ✔ | work_log 2026-08-28 |
| ✅ | ~~`readRegister()` 不驗 reply CRC、不驗 byteCount → 壞掉的 Modbus reply 被當有效值往上傳（bench 已造成實體損害，詳見下方）~~ | `user_lib/SD76_length_meters.cpp:153-171` | **已修（`1a15588`，driver 稽核那一輪）**：三道依序做完——`byteCount == count*2` → 幀長 `≥ 3+byteCount+2` → CRC 比對，且**先夾 byteCount 再拿它當長度用**（harness 實測 `byteCount=0xFF` 會 segfault）。🔴 **尚未實機驗證**；應用層 `meter_loop` 的 >30cm 跳變 filter 保留不動 | mailbox 2026-05-14｜2026-08-29 複查原始碼確認 |
| ✅ | 🔮 **eth 串接之後要回頭改 `WASH_ROBOT.h` 的 `CRANE_IP`**：目前是 bench 用的 WiFi **`192.168.5.25`**（2026-08-31 由 `.17` 漂過來，當日已改；註解顯示改過四次）。串上 eth 之後**它仍然會走 WiFi**——有線路徑就在旁邊卻沒被用到，而且完全不會有訊息告訴你。機器吊在半空中時控制流量跑在 WiFi 上，是實質風險 | `app/WASH_ROBOT.h:414` | **2026-08-31 已消除(改成不需要記得做)**:新增 `CRANE_IP_ETH` + `resolve_crane_ip_()`,開機**先探測有線(300ms 有界,非阻塞+SO_ERROR)、通了就用,不通退 WiFi**。🔴 刻意不用 `connectToServer` 探測(無逾時 blocking,沒串 eth 時會卡兩分鐘);🔴 刻意不放進 `ep::host`(會破壞等價測試的位元等價規則),覆蓋存在時不探測。三分支實機全驗 | work_log 2026-08-28 per user |
| ✅ | ~~**所有上滑台 RPM 常數都是在錯誤的線速度認知下挑的**~~ ✅ **2026-09-01 結案，本列有兩處記載是錯的**：① 它警告「`ARM_SWEEP_RPM=1000`（129cm/s）幾乎確定過快」，但 `WASH_ROBOT.h:752` **在寫下本列的同一天（08-28）就已 per user 1000→250**，`DM2J_ARM_STEP_SWEEP_RPM` 也早在 07-27 就是 250 → **「現況：未修」是錯的記載**。② 剩餘的「250 要重新評估／ACC-DEC=100 也在錯誤前提下挑的」已被 **08-31 per user 拍板否決**：「**RPM 的搜尋不做，由使用者視情況自行調整**」。📌 **理由留著**：開迴路前提下「找到不失步的 RPM」只對當下負載/摩擦條件成立，負載變了就要重驗 —— 這正是它適合由現場的人視情況調、而非訂一個常數的原因。真正的解是加回授或原點感測器。🔴 **不要再提議「跑 10 趟協議找可用 RPM 上限」——已提出並被否決一次。** 已知數字：目前 250；500 實測累積失步 0.2–0.3mm/橫越，不可用 | `app/WASH_ROBOT.h` | **已結案（記載更正 + 決策否決）** ✔ | work_log 2026-08-28（實機失步）＋08-31（決策）＋09-01（記載更正） |
| 🟡 | **USR 網關 `_pt`（串口打包時間）設為 0＝自動** → 115200 下字元間隔僅約 0.3ms，是「回覆被切成兩個 TCP 段」的結構性根源（`[2026-08-28b]` 的分片問題）。**改成 5ms 可從根本解決**，代價每筆交易 ≤5ms（`status` 讀 4 顆 → +20ms）。⚠️ 影響 bus 上所有裝置，且目前量到的失敗是 `no reply` 不是 `too short` —— **先記錄、之後再改**（per user 2026-08-28）。後台 `http://192.168.1.22/system.shtml`，admin/admin | 網關 `.20` / `.22` | **待改** ✔ | work_log 2026-08-28 |
| ✅ | ~~`web_backend/server.js` 的 **`CRANE_IP` 預設值寫錯**：`192.168.1.101`，吊機實際是 `192.168.1.10`~~ | `web_backend/server.js` config 區 | **已修（`f4e0d02`）**：預設值改 `192.168.1.10`，並在原地留註解說明「這與有線/WiFi 無關，串上 eth 之後照樣會錯」。⚠️ 同區的 `WROBOT_IP = 192.168.1.100` **是對的、刻意不動**（eth 尚未串接，bench 期間用環境變數覆蓋）。🔴 **未在 Pi 上實跑驗證**（且 Pi 上的 `web_ver2` 落後 repo，見下方該列） | work_log 2026-08-28｜2026-08-29 複查原始碼確認 |
| 🟡 | 兩台 Pi 都沒有 `tmux`／`screen` → runbook §A「一鍵啟動」`scripts/crane.sh`／`wr.sh` **在這兩台跑不起來**。替代方案 `~/bringup/run_bg.sh`（FIFO 背景啟動）已放兩台 | `scripts/*.sh`、`.claude/runbook.md` §A | **未修** ✔ | work_log 2026-08-28 |
| 🟡 | 緊急收繩按鈕**沒有張力保護**，跟 `motion_flow.md` §8 的安全性描述相反 | `Crane_control_PI/main.cpp` `hold_loop()`:1786、`cmd_manual()` | **部分處理（`b1234ad`）**：`hold_loop()` 新增 `any_manual_motion()` 分支，緊急收繩期間**補上張力警示與廣播**（此前該路徑張力既不檢查也不回報）。🔴 **刻意不呼叫 `hold_all_off()`** —— §8 明訂緊急模式由操作員眼睛判定，自動停止會擋住救援；規格表已就地更正（`2b16601`）。⚠️ **警示的可信度受限於 DSZL 刻度未校正**（見下方 🔴🔴 那列）：「有出現」值得信，「沒出現」不代表安全。🔴 尚未編譯驗證 | ONBOARDING §6｜2026-08-29 複查原始碼確認 |
| ✅ | ~~`cmd_side_measured()` 進場沒重置 `abort_flag` → 被 stop 過一次後所有 v2 step 指令永久回 `ERR aborted`~~ | `Crane_control_PI/main.cpp` | **已修（`[2026-08-28s]`）**：補上 `abort_flag = false;`，位置與姊妹函式一致（`try_lock` 之後，避免被拒絕的重疊指令清掉他人的 abort）。✅ **2026-08-29 已編譯通過**（吊機 Pi，`crane_control_PI.out.new`）；🔴 仍未實機執行驗證 | ONBOARDING §1 ＋ work_log 2026-07-15 |
| ✅ | ~~**DSZL-107 刻度未校正（量值）**~~ ✅ **2026-09-01 全列結案**（正負號當日稍早結、量值當日稍晚結）。**正負號**：`dszl_sign_test.py` 唯讀探測，左 Δ **−401.6**／右 Δ **−302.6**，兩側同為負、基線 spread 僅 1–2 counts、放開皆回基線（負向對照）→ `right untested but assumed same wiring` 的假設是對的，且現在是量測值。**量值**：使用者提供 **4.16 kg 已知重量** → 右 `-0.0236364`（42.3 counts/kg）／左 `-0.0205816`（48.6 counts/kg），已寫進 `DSZL_SCALE_RIGHT` / `DSZL_SCALE_LEFT` 並重啟驗證；**先前兩側共用 `-0.01` 是錯的**（如當初預判，左右確實需要不同量值）。**整機總重首次量到約 94 kg。** 🔴 **必看的三個殘留**已各自另立一列：① 校正後安全門檻的含意改變（`TENSION_MAX` / `TENSION_DIFF`）；② 重心偏左；③ 左側雜訊是右側 3 倍、且單點校正外推到 30~60kg 的線性度未驗 | `Crane_control_PI/main.cpp`、`user_lib/DSZL_107.cpp` | **已修 ✔**（正負號＋量值）| work_log 2026-05-07 ＋ 2026-08-28（升級）＋ 2026-09-01（結案） |
| ✅ | `tension_safety_check_values` 的註解寫「motion_flow.md §6.5 needs corresponding spec update **(mailbox to Jim)**」—— ⚰️ mailbox 已於 2026-08-27 退休成墓碑檔，**那個待辦丟進了沒人再看的信箱**。規格表該列已於 2026-08-28 就地更正 | `Crane_control_PI/main.cpp`、`.claude/motion_flow.md` | **已更正規格** ✔ | work_log 2026-08-28 |
| ✅ | 安全盤點高優先兩項未做：`cmd_hold` 與 motion 互斥、左右繩長差超標 abort | `Crane_control_PI/main.cpp` | **2026-08-31 兩項都完成並實機驗證**。① `cmd_hold` 補 `try_lock(motion_mtx)` —— 稽核六支驅動 VFD 的指令只有它沒有;**只鎖 `on`、`off` 永遠放行**(不擋停止路徑);`cmd_manual` 不加(刻意的原始旁路)。② 新增左右繩長差硬警報 + `set_length_diff_max_cm`,上下界負向對照已驗。🔴 **第一版寫成絕對差,上機當場打臉**(靜止就差 13cm、門檻 15cm)→ 改為本次動作期間的相對位移差。🟡 門檻 15cm 待確認;🟡 反向(hold 生效期間再啟動 motion)未做 | work_log 2026-05-08 |
| 🟡 | 🆕 **`8320bf3` 新加的兩個「讓失敗看得見」欄位，出現路徑都還沒被執行到**：`status` 的 `p_err=`（只在壓力讀取失敗時附加）與 `cmd_attach` 的 `partial_seal=N`（只在部分密封時附加）。2026-08-29 實機連跑 8 次 `status`（32 筆 JC-100 讀取）**全部成功 → `p_err` 一次都沒出現**＝正確行為，但也代表**這條路徑仍未驗證**。`partial_seal` 需要真的 attach（會動作），未測。📌 **與 `recovered on attempt` 同型**：實作了、編譯了，但沒被執行過的路徑不算驗證過 | `app/WASH_ROBOT.cpp` `cmd_status`／`cmd_attach` | **待驗** ✔ | work_log 2026-08-29（實機） |
| 🟡 | **`QX_DO24::init()` 是 14 支 driver 裡唯一活著的「`true`=成功」異類**（其餘 12 支是 Modbus 風格 `false`=成功；`DIHOOL_control` 亦為 true 但全 repo 無呼叫端＝死碼）。`bool init(...)` 的宣告兩派逐字相同，**從 `.h` 看不出來**。✅ 應用層目前沒踩到（SE3/MH300 呼叫端寫 `if (!init())` 正確；QX 唯一呼叫端 `WASH_ROBOT.cpp:204` 不檢查回傳值），**唯一受害者是那支從未執行過的測試**。→ 是否把 QX_DO24 對齊多數派（語意變更）**待決定** | `user_lib/QX_DO24.cpp:32`、`CLAUDE.md` 介面契約節 | **已記錄待決** ✔ | work_log 2026-08-29（第一次跑 `test_qx_do24` 揭露） |
| ✅ | `trigger_sync_move()` 是 Modbus 廣播（slave 0x00）不會有回應，卻以 `return resp.empty();` 收尾 → 廣播成功也永遠回報失敗 | `user_lib/ZDT_motor_control.cpp:599`（宣告 `.h:63`） | ✅ **已修（2026-08-29）**：送出成功即 `return false`。`readEcho(200)` 保留但降格為**排空**（避免上一筆交易的遲到回覆被下一筆誤讀），結果丟棄；**200ms 刻意不動**——它在步態迴圈裡，縮短是計時改變、要有機器才驗得了。三處呼叫端註解（`app/WASH_ROBOT.cpp` ×2、`Linux_test/main.cpp` ×1）已同步，TODO 已移除。🔴 **未編譯**（本機無 cc1plus、Pi 不可達） | mailbox 2026-04-30｜2026-08-29 修 |
| ✅ | ~~`send(sock, buf, len, 0)` 沒帶 `MSG_NOSIGNAL`，Linux 下對已關閉對端寫入會 SIGPIPE 殺 process~~ | `transport/TCP_client.cpp:53`、`transport/TCP_server.cpp:21`（**檔案已於分層重構搬離 `user_lib/`**） | **已修（`9e1ad1b`，分支 `fix/msg-nosignal` 已併入）**：兩檔各定義 `constexpr int SEND_FLAGS = MSG_NOSIGNAL` 供所有 `send()` 共用。🔴 **合併 main 時 `sendAndReceiveQuiet` 曾帶著 `send(...,0)` 繞過這道防線**（`[2026-08-28j]` 已修）——**新增送出路徑一律用 `SEND_FLAGS`，不要再寫字面 0** | mailbox 2026-04-22｜2026-08-29 複查原始碼確認 |
| ✅ | `CLV900_inverter` 缺 null-client 防護：跳過 `init()` 時 `client == nullptr`，`sendModbus` 直接 null-deref segfault（應用層已用 `g_dev_clv900` 守起來，driver 本身沒守） | `user_lib/CLV900_inverter.cpp:66` | ✅ **已修（2026-08-29）**：`sendModbus` 進場 `if (!client) { LOG_ERR; respLen=0; return true; }`，沿用 `DM2J_RS570::sendRecv` 的既有慣例。🔴 **未編譯**（同上）。⚠️ **但這條只關掉 12 支裡的 1 支**——見下方新增列 | mailbox 2026-05-14｜2026-08-29 修 |
| ✅ | ~~**null-client 守衛：12 支 driver 裡有 8 支的傳輸路徑沒守**~~ ⚠️ **原記「10 支」是錯的（2026-08-29 當日更正）**：那次用 grep pattern `!client\b` 判定，而 `!client->sendData(...)` 也會匹配，於是把 `JC_100_METER:57` 與 `XKC_Y25_RS485:70,180,214` （寫法是 `if (!client \|\| !client->isConnected())`）誤判成沒守，同時把 `DM2J_RS570` 誤判成守好了（它只守 `sendRecv`，六支 `read_*` 與 `recv_frame_` 是裸的）。**逐函式讀原始碼後實際是 8 支。**| `user_lib/`：ZDT(18)／DM2J(7)／PQW(5)／DY_500(3)／DSZL(2)／MH300(1)／SD76(1)／SE3(1)＝**38 處**，外加先前的 CLV900(1) | ✅ **已修（2026-08-29）**：守衛插在各函式進場，回傳值依各自慣例（Modbus 系 `true`=錯／`recv_frame_` 回 `-1`／回 vector 的回 `{}`／`close()` 直接 `return`）。本來就守好的是 `JC_100`／`XKC_Y25`／`QX_DO24`。🔴 **未編譯** | 2026-08-29 修 CLV900 時帶出，同日修完 |
| ✅ | ~~`TCP_client` 缺 `SO_ERROR` 驗證 → 影響 reconnect 的邊界 case~~ ⚠️ **本列與表格第一列是同一件事**（2026-06-09 與 2026-08-28 各記了一次），2026-08-29 合併確認 | `transport/TCP_client.cpp:208,214` | **已修（`56bfa5c`／`ce8ba81`）** — 詳見表格第一列（含雙向斷言實機驗證） | work_log 2026-06-09｜2026-08-29 判為重複列 |
| 🟡 | MH300 實機必驗清單未跑：方向映射、電流 scale、2101H run bit、fault code | `Crane_control_PI/main.cpp`（`VFD_DIR_*` 巨集）、`.claude/archive/mh300_migration_plan.md` | **未修** ✔（註解仍寫 `RE-VERIFY on MH300`） | work_log 2026-07-07 |
| ✅ | ~~**4 個 `.vcxproj.user` 被 git 追蹤**~~ → 🎉 **2026-09-07 徹底結案：整個 VS 檔案組已刪除**（`.sln` + 4 個 `.vcxproj` + 4 個 `.vcxproj.user` + `.vs/` 43 MB，per user「不用 VS 了」）。⚠️ **先前以為它結案了，其實沒有**：`.gitignore` 早就有 `*.vcxproj.user`，但 **gitignore 對已追蹤的檔案無效**，四個檔一直還在版控裡。📌 **「規則加了」≠「規則生效了」** —— 加 ignore 規則時要一併 `git rm --cached`。 | — | **已刪除** | 多處 |
| 🟡 | 沒有 hot re-init：裝置 flag 只在啟動時設一次，硬體中途修好要重開 crane | `Crane_control_PI/main.cpp` | **未修** | work_log 2026-05-08 |
| 🟡 | 沒有任何機制偵測「M2 被重新安裝過」；重裝後若位置落在 ±1.5 rad 內，INIT 會**靜默**移到錯的 CENTER | `cleaning_arm/main_api.cpp:1992-2028` | **未修** | work_log 2026-08-17 |
| 🟡 | `LR_CALIBRATE` 自動雙向尋邊不可靠（假觸發撞牆、或衝很遠都撞不到），目前只能走手動流程 | `cleaning_arm/main_api.cpp` | **未修** | work_log 2026-08-17 |
| 🟡 | 同步步伐（`step_down_sync`/`step_up_sync`）沒有地面淨空 / 障礙檢查，完全信任使用者輸入的 cm | `app/WASH_ROBOT.cpp` `do_step_sync_` | **未修** | work_log 2026-07-22 |
| 🟢 | 規範文件架構圖與程式碼脫節 —— **2026-08-28 已解**：`CLAUDE.md` `## Architecture` 全節由原始碼重建（v2 as-built）。`motion_flow.md` §2 **刻意維持 v1 不動**（它是已凍結的 v1 世代文件，見本檔文件世代表），不是遺漏 | `CLAUDE.md` `## Architecture` | **已修** ✔ | ONBOARDING §5 |
| 🟡 | DSZL-107 熱修走路 B（RTU+CRC16 → Modbus TCP MBAP）的 review 沒做完，且當時說「規範文件未動、待 review 後一起更新」 | `user_lib/DSZL_107.{h,cpp}` | driver **已修** ✔（MBAP 已在 code）／文件 **未修** | mailbox 2026-05-08 |
| 🟡 | SD76 SCAL/DP 校正 API 的公式假設（`display = pulse × SCAL × 10^(-DP)`）、是否需要 save_params、DP 上限行為都還沒 bench 驗證 | `user_lib/SD76_length_meters.cpp` | API **已修** ✔／驗證 **待查** | mailbox 2026-05-09 |
| 🟡 | 新 driver `SE3_inverter` 的 review 與硬體驗證未結案：USR2 IP、SE3 keypad 預設（站號/波特率/控制源/watchdog）、方向約定、暫存器位址 | `user_lib/SE3_inverter.{h,cpp}` | **待查** | mailbox 2026-05-07 |
| 🟡 | 新 driver `DSZL_107` 的 review 未結案：scale factor 實機校正、byte order（BE vs word-swap）驗證 | `user_lib/DSZL_107.{h,cpp}` | 應用層串接 **已修**／校正驗證 **待查** | mailbox 2026-05-06 |
| 🟡 | crane 端偶發 `ERR meter_left_read_fail` + TCP 每 500ms reconnect，根因未知（已排除兩個假設），workaround 是重開 crane 程式 | `Crane_control_PI/main.cpp:1367` `meter_read_robust()` | **待查** | ONBOARDING §3 |
| 🟡 | follower 側 IMU 校平疑似被切到 `meter` 模式導致機體歪斜；`follower_use_imu_==false` 的路徑**完全靜默**，一行 log 都不印 | `app/WASH_ROBOT.cpp:6366`、`WASH_ROBOT.h:881` | **待查**（走法已全面改 sync，但後端 raw command 預設仍是 `alt`，仍走得到） | ONBOARDING §2 |
| 🟡 | 2026-07 那整批改動**從未編譯 / 部署驗證**（🔴 **2026-09-12 更正:「本機無法 build」是錯的**,本機有 g++ 9.4.0 可做語法/預處理驗證,只是產不出 aarch64 部署檔）：TCP_client 殭屍連線修復要驗自癒、WASH_ROBOT 安裝幾何常數、同步步伐、partial-seal 判準、crane 端 `Crane_control_PI` 建議先單獨 build 綠燈；`1829964` 等 commit 仍在本機 main **未 push** | `transport/TCP_client.cpp`、`app/WASH_ROBOT.{h,cpp}`、`Crane_control_PI/main.cpp`、`facade_cleaning_v2/main.cpp`、`web_backend/public/*` | **待查** | work_log 2026-07-07 / 07-15 / 07-21 / 07-22 / 07-23（7 筆合併） |
| 🟡 | 同步步伐的 IMU 差動微調**方向**（sign convention）沒實機驗證過，第一次上機要小角度有人看著 | `app/WASH_ROBOT.cpp` `do_step_sync_` | **待查** | work_log 2026-07-22 |
| 🟡 | 水平校正整合（IMU roll ＋ 左右繩長差 tol）在 v2 step 收尾只留 TODO | `app/WASH_ROBOT.cpp` | **待查** | work_log 2026-07-07 |
| 🟡 | v1 舊 body 用 `#if 0` 包起來當 reference，說好 bench 驗證 v2 綠燈後再硬刪 — 還沒刪 | `app/WASH_ROBOT.cpp` | **待查** | work_log 2026-07-07 |
| 🟡 | Realign Layer 2（Phase 2 in_window 期間 cycle valve OFF/ON）設計討論完但未實作 | `app/WASH_ROBOT.cpp` | **待查** | work_log 2026-06-02 |
| 🟡 | `vacuum_check` 重複跑兩次浪費 30s／attach（提了 α + δ 兩方案，未選） | `app/WASH_ROBOT.cpp` | **待查** | work_log 2026-06-09 |
| 🟡 | `arm_cmd_` INIT recv timeout 真因沒查清楚（看起來是 motor_api 端會卡）；60s 是否要再拉長待決 | `app/WASH_ROBOT.cpp`、`cleaning_arm/main_api.cpp` | **待查** | work_log 2026-06-09 |
| 🟡 | Scripted run / Snowball 防護 A+B+C / Water inlet 防漏三批功能**全部沒實機驗證過** | `app/WASH_ROBOT.{h,cpp}`、`web_backend/public/*` | **待查** | work_log 2026-06-09 |
| 🟡 | 2026-06-02 那批 fix 的實機觀察清單未跑完：`wall_mm=330` 是否平貼、anchor vacuum check 會不會誤報、`cmd_recover` vacuum_check 的使用者處置、BAL `kp=1.0` 是否改善震盪、`cmd_status` 1Hz rate-limit 是否減半 JC100 timeout | `app/WASH_ROBOT.{h,cpp}`、`Crane_control_PI/main.cpp` | **待查** | work_log 2026-06-02 |
| 🟡 | crane 端 placeholder 常數與未驗事項：4 個 gateway IP 對應、SE3 keypad 預設、CLV900 RPM↔Hz 公式（等馬達極數）、`UP_STOP_TOTAL_KG_DEFAULT=50` / `SE3_HOLD_HZ=20` 等 | `Crane_control_PI/main.cpp` | **待查**（拓樸 2026-08-27 又重配過，需重新對照） | work_log 2026-05-07 |
| 🟢 | SD76 通訊模式 mode latch：DP 寫入被 firmware 吃掉（同類 SE3 H1000 / P.79 行為），driver 已 revert auto-DP、改成 preserve current DP。**未來方向**：找 SD76 對應的 unlock magic 才能完全自動化改 DP，目前只能面板操作 | `user_lib/SD76_length_meters.cpp` | **待查** | mailbox 2026-05-09 |
| 🟢 | `SE3_inverter::readFaultCode()` 已加，但 bench 驗到 `0x1007`/`0x1008` 連續 ~10 次都 READ_FAIL — 位址是否正確待驗（三個可能原因見下方） | `user_lib/SE3_inverter.cpp:381` | method **已修** ✔／位址 **待查** | mailbox 2026-05-14 |
| 🟢 | `DSZL_107::do_zero_ch1/2/all()` 目前不會自動 follow-up `save_params()`（刻意設計，避免連續校零磨損 flash），是否要加可選 `persist` 參數待決 | `user_lib/DSZL_107.cpp:304-306` | **待查** ✔ | mailbox 2026-05-08 |
| 🟢 | `arm_sweep_monitor` SUSTAINED 0.2→0.4（防 false positive，代價是可能漏接弱接觸 obstacle）— 待 user 拍板 | `app/WASH_ROBOT.cpp` | **待查** | work_log 2026-06-09 |
| 🔴 | 🆕 **`verify_arm_deploy_` 自 2026-06-06 起是無條件 `return false`**（bench 沒真牆、每次誤報），下面整段是死碼 ⇒ **障礙偵測三個月完全沒在跑**。修好重力模型**還不夠**：DEPLOY 壓玻璃時，命令角（0.969）與實際角（0.675）的 **0.29 rad 落差是壓力來源、不是故障**，拿它跟由 `wall_mm` 反算的命令角比對必然誤判。→ **應改為比對每個 slot 校正過的「預期接觸角」** | `app/WASH_ROBOT.cpp` `verify_arm_deploy_` | **待修（設計改動）** | work_log 2026-09-02（續七） |
| 🟡 | 🆕 **`hold_kp=90` 是舊重力模型下的補償**（當日 34→60→90 一路往上加，為的是抵銷被高估的前饋）。重力修正後這個增益可能過大（震盪與撞擊力風險）→ 需重新掃一次最低可用值 | `cleaning_arm/main_api.cpp` | **待調** | work_log 2026-09-02（續七） |
| 🟡 | 🆕 **重力擬合區間只有 0.42~0.64 rad**（0.65 以上被玻璃擋住，是外推）。新舊兩條擬合線在 0.64 差 **2.17 Nm**——剛性手臂不可能不連續 ⇒ **至少有一邊的量測是錯的**。採信新值（12 點 vs 舊值的 2 點、且舊值靜態量測混入 0.39~1.86 Nm 摩擦），但**高角度段未驗證** | `cleaning_arm/main_api.h` | **待驗** | work_log 2026-09-02（續七） |
| 🟡 | 🆕 **臂長 320→490 是由三點反推的擬合值，不是量出來的**（殘差 ±0.1mm 很漂亮，但那只證明*模型自洽*）。🔴 **無法排除的替代解釋：編碼器角度尺度差 1.53 倍**——兩者對這三點會給出相同預測。→ 需**用量角器獨立量一次實際關節角**才能分辨 | `cleaning_arm/main_api.h`、`app/WASH_ROBOT.h` | **待驗** | work_log 2026-09-02 |
| 🟡 | 🆕 **四顆推桿接觸力道不均**：10cm 時 peakI 589~1264 mA（2.1 倍）＝**機身與玻璃不平行**。手臂的貼合角度因此會隨機身姿態變動，`wall_mm` 的校正值只在當下站位成立 | 機械（非程式） | **待查** | work_log 2026-09-02 |
| 🟢 | PQW CH6 verify fail「gave up after 3 retries」最後沒人 catch，downstream 沒擋住 — 要確認是不是真的有 propagation 問題 | `app/WASH_ROBOT.cpp`、`user_lib/PQW_IO_16O_RLY.cpp` | **待查** | work_log 2026-06-02 |
| 🟢 | `DM2J:14` writeMulti no response（cli_22_ contention 偶發，driver 自己 retry 成功）— 要不要監控連續失敗率 | `user_lib/DM2J_RS570.cpp` | **待查** | work_log 2026-06-02 |
| ✅ | arm M1 `verify_deploy` delta 漸增（RIGHT 從 0.797 漂到 0.910，delta −0.114 / tol 0.150，接近邊緣） | `cleaning_arm/main_api.{h,cpp}` | ✅ **2026-09-02 找到成因**：**不是漂移，是重力前饋高估 30%**（`M1_GRAVITY_K` 20.87，實測 16.09）。手臂停在 `kp·err` 與過大前饋的平衡點 ⇒ **角度越大、下垂越多**，delta 自然隨姿態「漸增」到逼近 tol。12 點雙向慢掃重擬合後，自由平衡下垂由 **0.0695 → 0.0025~0.0060 rad**（12~28 倍），`arm_deploy` 首次回 `OK`。🔴 **但 `verify_arm_deploy_` 仍不能打開**——見下方新增列 | work_log 2026-06-02｜2026-09-02 解 |
| 🟢 | `cmd_recover` force escape（sensor 假報故障時 user 會卡死）— 設計討論完，暫不做，先看誤報率 | `app/WASH_ROBOT.cpp` | **待查** | work_log 2026-06-02 |
| 🟢 | Tool 物理裝歪：若 `ARM_CLEAN_WALL_MM=330` 還是不平貼 → 拆 tool mount 重裝 | 機械（非程式） | **待查** | work_log 2026-06-02 |
| 🟢 | BAL 討論但未落地：機體重心本來偏 L，應追求「兩繩同步收放」而非「等張力」；kp 1.0 不夠可能要加 base offset | `Crane_control_PI/main.cpp` | **待查** | work_log 2026-06-02 |
| 🟢 | 跨越障礙物步幅建議公式（`remaining + max_height + 20 + 5`）只驗過算式邏輯，沒驗過「照這個步幅走真的跨得過去」。跨障礙物按鈕本身仍保留 | `app/WASH_ROBOT.{h,cpp}` | **待查**（深度相機這個**輸入來源**已移除） | work_log 2026-07-22~23 |
| 🟢 | SE3 `sendModbus` recv timeout 300→150ms（worst case writeParam fail 500→350ms、8 retry wall time ~4.8s→~2.4s） | `user_lib/SE3_inverter.cpp:121` | **已修** ✔（已套用，review 請求作廢） | mailbox 2026-05-14 |
| 🟢 | SE3 `invalidateCuModeCache()` 純 additive method（解 cold start `fine_adjust` 連按 3 次第三次才動） | `user_lib/SE3_inverter.cpp:297` | **已修** ✔ | mailbox 2026-05-13 |
| 🟢 | SE3 `clearAlarm()` 純 additive method（H1101=H9696 變頻器復位，解通訊中斷後卡 OPT） | `user_lib/SE3_inverter.cpp:312` | **已修** ✔ | mailbox 2026-05-13 |
| 🟢 | SD76 SCAL 是**除數**不是乘數（手冊寫 "Counter Multiplier" 但行為相反），driver 內部換算成 1/K | `user_lib/SD76_length_meters.cpp` | **已修** ✔ | mailbox 2026-05-09 |
| 🟢 | `TCP_client` 加 `SO_KEEPALIVE` + `TCP_KEEPIDLE=10s`/`INTVL=3s`/`CNT=3`（dead connection 偵測 ~19s vs 預設 ~2hr） | `transport/TCP_client.cpp:24` `apply_keepalive()` | **已修** ✔ | mailbox 2026-05-08 |
| 🟢 | `DSZL_107::save_params()`（寫 `0xA20=40` + 150ms sleep，解 X518 power-cycle 掉設定） | `user_lib/DSZL_107.cpp:314` | **已修** ✔ | mailbox 2026-05-08 |
| 🟢 | `DM2J_RS570` 多處 bug：`read_status` 讀 2 reg 應讀 1、完工檢查查錯 word、`print_status` HOME_DONE mask、`motor_enable/disable/save_params` 只宣告沒實作 | `user_lib/DM2J_RS570.cpp` | **已修** ✔（mask 改 `0x0040`、`0x000F` enable、`0x2211→0x1801` save 都已落地） | work_log 2026-04-24 |
| 🟢 | 清掉 `Linux_test` 的 `dm2j_manual_enable` helper（那段寫 `0x1111` 其實是 reset alarm 不是 enable） | `Linux_test/main.cpp` | **已修** ✔（符號已不存在） | work_log 2026-04-24 |
| 🟢 | GUI 按鈕對應（右/左閥、單側繩、step） | `web_backend/public/*` | **已修**（2026-08-26~27 多輪 GUI 改版已重做） | work_log 2026-07-07 |
| 🟢 | arm 清洗 sweep 因手臂未裝而 deferred | `app/WASH_ROBOT.cpp` | **已修**（2026-07-24 手臂實裝後接回 `do_step_sync_rail_sweep_`） | work_log 2026-07-07 |
| ✅ | `frame_capture/depth_cam_service.py` / `depth_reflection_bench.py` / `depth_cam_test_client.py` 三個檔 git untracked ✅ **2026-09-01 作廢：深度相機整套移除**（C++／service／harness 假端點／3 支 Python 全刪，dispatcher 回 `ERR removed_2026_09`）——本列所述的對象已不存在。 | `frame_capture/` | **已修** ✔（三個檔已進版控） | work_log 2026-07-22~23 |
| ✅ | D435i 深度相機**戶外強光**未測（曾是換相機決策的最大未知數） ✅ **2026-09-01 作廢：深度相機整套移除**（C++／service／harness 假端點／3 支 Python 全刪，dispatcher 回 `ERR removed_2026_09`）——本列所述的對象已不存在。 | `frame_capture/` | 🔴 **作廢理由不成立（2026-08-29 複查）**：移除的是 **GUI**，不是後端。`cmd_run_depth_avoid` / `depth_cam_cmd_` / `DEPTH_CAM_*` 仍在 `app/WASH_ROBOT.{h,cpp}` 活著。實體相機未接故不會跑，**但這是「沒接線」不是「已移除」** | ONBOARDING §4 |
| ✅ | `remaining_travel_cm` 用新常數（`LEAD_OFFSET=32cm`/`STANDOFF=56cm`）後沒重新實機驗證 ✅ **2026-09-01 作廢：深度相機整套移除**（C++／service／harness 假端點／3 支 Python 全刪，dispatcher 回 `ERR removed_2026_09`）——本列所述的對象已不存在。 | `app/WASH_ROBOT.h:296-297` | 🔴🔴 **誤標作廢，實為未驗證的活常數（2026-08-29 複查）**：`DEPTH_CAM_STANDOFF_CM=56.0` 與 `DEPTH_CAM_LEAD_OFFSET_CM=32.0` 都還在，且正是 🔴「`run_depth_avoid` 後端仍會自行改走 cross 步伐」那條待辦所用的算式輸入 → **恢復為未驗證** | work_log 2026-07-22~23 |
| 🟢 | 一般（非鏡面）窗戶場景的窗框辨識沒測過 | `frame_capture/obstacle_detector.py` | 🔴 **作廢理由不成立（2026-08-29 複查）**：`obstacle_detector.py` 仍在版控，`FrameAnalyzer` 仍呼叫 `obstacle_combine.py`。實體相機未接故不會跑 | work_log 2026-07-22~23 |
| ✅ | ~~`scripts/wr.sh` 的 cam1/cam2 window 還註解著，攝影機接回去要取消註解~~ | `scripts/wr.sh:5,50-53,65` | ✅ **已修（2026-08-29）**：兩個 window 本來就已註解，這次修的是**與決策矛盾的註解文字**——原本三處（檔頭用法說明、檢查區、start 區）都寫「暫時／之後接回去時取消註解即可」，而 2026-08-27c 的決策是**永久移除**。已全部改成「永久不接」並註明保留兩段只為記錄它們曾經怎麼啟動、不是待辦。`bash -n` 通過。⚠️ **depth window（`:66`）刻意未動**——那條是既有的獨立待辦且標著「待 user 決定」，不是我可以順手拍板的 | work_log 2026-07-21｜2026-08-29 修 |
| 🟢 | `camera_obstacle_plan.md` 還沒加 motion mode section | `.claude/archive/camera_obstacle_plan.md`（**已於 2026-08-29 複查時發現搬進 `archive/`**） | **已修（作廢）**：該計畫檔已封存，Phase 5 未實作 | work_log 2026-06-02/03 |
| 🟢 | v1 現場未解 5 項：PQW 寫 relay 不成功、DM2J slave ENABLE bit 沒亮、ZDT slave 6 堵轉、推桿距離待細調、FrameAnalyzer C++ 沒寫 | v1 硬體 | **已修（多數作廢）**：v2 已無 DM2J 滑軌/輪組，吸盤 slave 2026-08-27 改 5-8，`user_lib/FrameAnalyzer.cpp` 已存在 | work_log 2026-04-23 |
| ✅ | `run_depth_avoid` 後端仍活著，且偵測到大障礙物時會**自行改走 `cross` 步伐**：`run_depth_avoid` / `depth_avoid_continue` / `depth_avoid_stop` 三個指令仍 dispatch 到真實實作，而同輩的 `obstacle_detect`/`run_avoid`/`obstacle_response` 早已硬關成 `ERR removed_in_v2`。前端已於 2026-08-27c 移除 → **現在完全沒有 UI 提示** | `facade_cleaning_v2/main.cpp:184-189` | **2026-08-31 已處置**。📌 一般步伐**本來就已是 `do_step_sync_`**(2026-07-28 per user 改過),只有 auto-cross 分支走 `do_cross_obstacle_`。已停用該觸發:偵測到障礙改為**停下來說明原因**(`depth_avoid_obstacle_needs_manual`),不再跑到下一輪撞守衛回看不懂的 ERR。原碼保留 | `camera_obstacle_plan.md` 稽核 2026-08-27（changelog 2026-08-26e） |
| ✅ | 🆕 **本體主程式自己也還在探測深度相機**：`init()` 印 `[WARN] depth_cam 127.0.0.1:9530 not yet reachable`（2026-08-29 實機）。既有待辦只記了 `scripts/wr.sh:67` 會**啟動** `depth_cam_service.py`，**漏了主程式端還在連它** —— 攝影機路線 2026-08-27 已永久移除。無害（只是一行 WARN），但**每次啟動都在報一個不存在的東西**，會稀釋真正的 WARN ✅ **2026-09-01 作廢：深度相機整套移除**（C++／service／harness 假端點／3 支 Python 全刪，dispatcher 回 `ERR removed_2026_09`）——本列所述的對象已不存在。 | `app/WASH_ROBOT.cpp`（depth_cam 連線初始化）、`facade_cleaning_v2/main.cpp` | **未修** ✔ | work_log 2026-08-29（實機 init） |
| ✅ | `scripts/wr.sh:67` 仍會啟動 `depth_cam_service.py`（depth window）。changelog `2026-08-26e` 結尾寫「可以把那個 window 註解掉——尚未變更，待 user 決定」，至今未決 ✅ **2026-09-01 作廢：深度相機整套移除**（C++／service／harness 假端點／3 支 Python 全刪，dispatcher 回 `ERR removed_2026_09`）——本列所述的對象已不存在。 | `scripts/wr.sh:67` | **未決** ✔ | `camera_obstacle_plan.md` 稽核 2026-08-27 |
| ✅ | MH300 keypad commissioning 參數表**是唯一副本**（只記在 plan 檔裡，沒有第二份）：站號 `09-00`=1/2、`09-01`=9.6、`09-04`=12（8N1 RTU，與 SD76 共用同一條 bus）、`00-20`=1（頻率來源 RS-485）、`00-21`=2（運轉來源 RS-485）、`07-00~04` DC brake／煞車截波（配 BR300W070-S 制動電阻）、`01-12`/`01-13` 加減速時間——**左右必須對齊，否則不同步停車** | `.claude/summaries/MH300_INVERTER_MODBUS_SUMMARY.md`(新) | **2026-08-31 已解除單點失效**:新建 `.claude/summaries/MH300_INVERTER_MODBUS_SUMMARY.md`,keypad 參數表全數抄入,並補上**與 SE3 的關鍵邏輯差異**(B.B 在 `0x2002` 而非 run 的 `0x2000` → 現況「`stopDecel` 清 MRS」在 MH300 會讓急停後馬達被 base-block 卡死)。📌 遷移步驟仍在 `mh300_migration_plan.md`,但**實體換機需要的參數已不依賴計畫檔存活** | `mh300_migration_plan.md` Phase 0 |
| ✅ | SE3 `P.79` 切換程序與「`P.5` 必為 0」**是唯一副本**，而且 bench 目前**仍在跑 SE3**（`Crane_control_PI/main.cpp:116` `#define CRANE_VFD_IS_SE3 1`），不是已作廢的舊文件：改 `P.79` 前須先停馬達、解除 OPT，再 `P.79=3 → 2 → 6`（防 latch 卡住）；`P.5`（multi-speed）必須保持 0，否則多段速會覆蓋 H1002 頻率命令 | `.claude/summaries/SE3_INVERTER_MODBUS_SUMMARY.md` | **2026-08-31 已解除單點失效**:抄入 `summaries/SE3_INVERTER_MODBUS_SUMMARY.md` 新增的「🔴 面板切換程序(P.79 / P.5)」一節 —— 改 P.79 前須停馬達+解 OPT、**`3 → 2 → 6` 逐步切換防 latch**、**`P.5` 必須保持 0**(>0 時多段速覆蓋 H1002,屬「寫入回報成功但沒作用」)。⚠️ 原始檔 `.claude/archive/se3_mode6_migration_plan.md` **已在 archive/**,正是會被清掉的位置 | `se3_mode6_migration_plan.md` §1.1 |
| ✅ | ~~QX-DO24 PWM（螺旋槳 ESC 控制）目前停用，`PWM_SLAVE=6` 撞 JC100 真空計~~ **已解決** | `app/WASH_ROBOT.cpp:175-192`（`PWM_ENABLED`） | **2026-08-31 複查:本列已過期。** `PWM_SLAVE` 已於 08-28 改為 **9**（模組端同步改號）、`PWM_ENABLED` 現為 **true**，且 08-31 實機確認風扇確實受控（`step_move_on` 寫 7% 會轉、`step_abort_off` 寫回 5% 會停，使用者現場目視確認）。🔴 **左右兩顆風扇共用 CH1**（per user），`PWM_STEP_CH=1` 只寫一個通道是正確的 | changelog 2026-08-27h ＋ 新架構設計 2026-08-27 |
| 🟡 | **`SERIAL_PORT_H` guard 衝突：兩個不同的序列埠實作共用同一個 guard** | `user_lib/SerialPort.h`（322 行，cleaning_arm/damiao 用）與 `transport/Serial_port.h`（本專案用，WASH_ROBOT.h / WT901BC_TTL.h / Linux_test）。目前不爆只因使用者不重疊；**一旦同一編譯單元同時碰到兩者，第二個被 guard 靜默吃掉**，症狀是「class 莫名找不到」、錯誤訊息不指向真因。修正方向：guard 改唯一名稱或 `#pragma once`，動前先確認無別處拿此 guard 名做條件編譯。兩檔開頭皆已標註 | 未修 | 分層重構 2026-08-27 |
| 🟡 | **Pi 上的 `web_ver2` 落後 repo 一個 commit** ⚠️ 原記「分岔 589 行 / 不是增量是另一個程式」是**誤判**，2026-08-28 更正 | Pi `~/projects/web_ver2/`（在**吊機** `raspberry-cran`，不是本體）四個檔全是 repo 內容的複本：`server.js`／`style.css` 與 commit `faf1d3f` **逐位元相同**、`app.js` 與 `a894ae1` 逐位元相同、`index.html` 與 HEAD 的差異**全部**是攝影機面板那一段。**沒有任何人手改過的內容，repo 仍是權威**，只是落後移除攝影機的 commit `e3c8820` | 待部署（🔴 **main 分支的人正在改這兩台，部署前先確認**） | 更正 2026-08-28（原：實機盤查 2026-08-27） |
| ✅ | ~~**張力刻度仍是 placeholder，kg 讀值無絕對意義**~~ ✅ **2026-09-01 結案**（與上方 DSZL 那列同一件事）。沿革保留：08-29 複測仍是 `-0.01`，且當時把 kg 反推的 raw（−2645/−1685）誤讀成空載值 —— 09-01 實測機器在地上繩鬆時 raw 是 **−1.2 / 67.0**（接近零），零點一直是正常的，那組讀值是有載時的。📌 `crane_balance_hold_plan` 重啟前提「張力可信」**現在達成了**，該計畫可重新評估 | 已修 ✔ | 實機讀取 2026-08-27｜2026-08-29 複測｜2026-09-01 校正結案 |
| 🟡 | ~~**左右張力差 12.4 kg，且左側已越過收繩停止門檻**~~ **結論已過期（2026-08-29 複測）**：門檻實際是 `retract_tension_stop_kg=**50**`（08-27 記的 25 與現況不符），左側 26.45 **並未越過**；左右差也由 12.4 縮為 **9.6 kg**。🔴 **但根因沒變**——刻度仍是佔位值（上一列），所以「差 9.6kg」這個數字同樣不可信。**此列降為 🟡：可觀察，不可據以判斷** | `Crane_control_PI/main.cpp`（`retract_tension_stop_kg`）、`user_lib/DSZL_107.cpp` | 未修（根因在上一列） ✔ | 實機讀取 2026-08-27｜2026-08-29 複測更正 |
| 🟡 | **VFD 故障碼顯示是壞的** ⚠️ **2026-08-29 複測：症狀變了，而原本的歸因很可能是錯的** | 08-27 記的是「left 報假警 `f1~f4=160/OPT`／right `ERR read_fail`」；**08-29 兩側都是 `ERR read_fail side=<left\| **2026-08-31 查明:不是讀取壞掉,是內容沒有鑑別力**。直讀兩顆:`H1001=0x0080`(b7 SET)、**`H1007`/`H1008` 四槽全部是 160 OPT(通訊逾時)** —— 每次停程式 keepalive 一停就鎖存 OPT,**真實故障已被例行關機擠出歷史**。📌 `07-10=0`(通訊斷即報警+空轉停車)是**安全正確設定,不要改**。✅ 開機訊息已能分辨 OPT(WRN)與非 OPT(**ERR,可能真故障**)。⚠️ 原註解說位址 unverified 是過期的 | 未修；**歸因待重驗** ✔ | 實機讀取 2026-08-27｜2026-08-29 複測推翻歸因 |

---

### 🟡 2026-09-12 由歷史壓縮併入（原「壓縮保全清單」81 條核對後的真遺漏）

> **來源**:2026-07-22~09-09 詳細日誌壓縮時,81 條未結案項在本總表比對不到。當日逐條複驗:
> **20 條確認已被上表涵蓋或已結案 → 刪**、**2 條前提已消失 → 作廢**、**其餘併入本節**。
> 保全清單已清空刪除。此處刻意**分組壓縮**而非逐條展開 —— 每條都指得到日期與主旨,細節查
> git `1ecc6be` 前版本的 work_log 原行號。
>
> ❌ **作廢 2 條(前提已消失,別再撿回來)**:①「玻璃接觸角只有下半部兩點,上半部若更近 `theta_min`
> 會誤擋」—— `th_min` 守衛 2026-09-11 已隨橫桿定案移除;②「`m1_.hold_ki` 需 anti-windup 才能重新
> 啟用」—— `hold_ki` 已移除。

| 群 | 項目(日期) |
|---|---|
| 🔴 **通訊/匯流排** | **隧道閒置 1.06% 丟包未查**(09-08,與上表「VFD 運轉掉 90%」是**不同現象**);**WiFi 冷路徑單向丟包,powersave 假說未證實**(09-08/09,待裝 `iw` 或關 powersave);**SE3 VFD 寫入間歇失敗**(每個動作恰一筆,09-01 重現;慢速起步只是繞過);**X518 連線數上限未量**;🔴 **`.22` 匯流排本身沒修** —— QX 移 `.21` 是繞過,現只剩 JC100×4 看獨處穩不穩,**實體層(終端電阻/線長/接地)未查**;吊機 `set_imu_roll` dispatch log 4 Hz 洗版待節流 |
| 🔴 **張力/平衡** | 🎯 **DSZL-107 scale 持久化(2026-09-12 per user 定調)** —— **校正已完成、現在讀值是準的**,剩下唯一的事是**校出來的 scale 要存起來**:存**吊機端 setting/config**;若吊機端暫不規劃,**本體端也會用到** ⇒ 走 **Web GUI 改數值**的那個既有功能當介面。**併入下方 🔴「吊機執行期參數不持久化」一起做**(同一件事:motion_hz / balance_source / home_ground / fine_adjust_level_diff_cm / DSZL scale 全部重啟即失);兩側機械耦合受控量測(串音方向不一致);`BALANCE_IMU_KP=2.0`/`deadband=0.5` 未調初值;`pay_out ≤ 30Hz` 方向上限**程式未強制**;`site_profile` 設計(待核准,新架構待辦表 ⏸ 暫緩);DSZL log 補 `@L/@R` 到 `get_tension_kg`;**hold 生效期間再啟動 motion 未擋**(`any_hold_active()`,等拍板);`pay_out_right 1` 回 `moved=2cm` 但計米器變 9 單位 —— **單位換算未驗**(兩筆證據) |
| 🔴 **手臂力控/幾何** | ✅ ~~「15 Nm 對刮刀可能不是對的目標值」~~ **2026-09-12 per user 結案:「8 Nm 對刮刀可以接受」** —— 滾筒與刮刀共用同一個 target 即可,不需為刮刀另訂力道目標。**別再提議為刮刀拆獨立 target**;`DEPLOY_F` WARN 分支**從未在硬體觸發**、GUI 未收斂四態未實證;`theta_max=1.10` **無上界實測**;力控 `TOL` 1.0→0.4 待決;尋觸走到 0.7× 目標才交棒可省一輪(未做);`disable_slot(m1_)` 而 M2 無對應 disable ⇒ **M2 可能仍使能**(未證實);`M1_GRAVITY_MIN_VALID_RAD=0.20` 低角度無補償的獨立影響未驗;`TOOL_EXT_CENTER=160` **唯一未實測,且是 `wall_mm=520` 的錨點**;樞軸→玻璃**四個幾何常數未實體量測**;`PARK_STOP_MARGIN=0.05` 長行程落點失控(落 0.0170);🔴 **M1 passive 根因自 2026-08-17 起未解**(看門狗只能重試);`cmd_status_sequence` disabled slot 刷新 / `cmd_arm_status` 格式對齊;診斷記錄取樣抖動(22.2 ms、偶有 190 ms 空隙)未查;M1 起步踢擊是否因 M2 推更久而改變,未複測;M2 負向有沒有機械停點未知(2 rad 內沒有) |
| 🔴 **滑台/M2/機構** | 🔴 **M2 掃動中被橫向摩擦帶著跑 0.39~0.49 rad(26~28°)** —— 十輪一致＝固有特性,**唯一剩下的機構問題**(09-04 `hold_kp=31` 是否部分緩解需核對);140 端玻璃比 0 端遠 6 mm,行程加長要重評;回到 09-03 量 14.31 的高度重跑三點,把「25% 偏移」與牆距畫上等號;`water_on` 把開滾筒綁著(乾式那輪不轉),per user 之後再處理;11 段移動 10~11 s 且停後 roll 偏大,「其他 3.6 s」未拆開 |
| 🟡 **本體/驅動/流程** | Web GUI 8080/8081 **非 systemd,開機不自啟**(與控制程式同病);`cmd_vacuum(group)` 接受 left/right 但實際全域,應對非 feet/all 回錯;**5 個「設定了沒效果」的 setting**(`PUSHER_EXTEND_BODY_*`/`RETRACT_SLOW_PEEL_CM`/`STEP_CM_DEFAULT`/`STEP_MARGIN_CM`)接上或移除;`DISABLE_POS_ERROR_LIMIT_DEG` 0 處讀(09-02 已補「後果」註解,**刻意不接**);PWM 重試間隔 40 ms vs 對方 120 ms,觀察掉包率後定 `kBackoffMs`;`try_or_pause_` 靜默中止加一行診斷;`mb_scan` 回覆晚一拍(工具缺陷,使用時注意);`DY_500_weight_sensor.cpp:221` `hasError` 設了沒用(錯誤被吞)—— 併入 §瘦身的 DY-500 移除;PQW 模組需更換/檢修,**決定性測試(一支計米器＋PQW)未做**;`WR_DRIVER_DEBUG=ON` 會出 hex dump 且要關要重啟;`probe_dm2j.cpp` 未進版控;`WASH_ROBOT.h:1082` 成員註解 `.22 = arm-rail` 過期;runbook 連線資訊仍寫 `server.js CRANE_IP=.101`(過期);runbook 三條事實更正未寫回、`origin/main` 推不推等拍板;`tmp/` 未處理(per user 最後處理);兩台 Pi 的 `~/merge_check_20260828/` 拋棄式資料夾可刪;feet hook「傳了不會觸發」落差(**刻意保留**);等價比對基線需重取 |
| 🟡 **GUI(AI-2 領域)** | 動作類按鈕未驗;Mission `bal:` roll 說明字串未在畫面出現過;版面 Mission/Manual 最下排卡片 900px 高會被切;8080 `public/index.html` 沒版號欄位;平板版**未經真人觸控實測**;8080 改動只驗到服務出去的內容對、瀏覽器未驗;`.man` 整頁鎖改用 `disabled`(擋不住鍵盤);吸盤 1~2 顆達標要不要警示是判準,**等使用者定義** |

✅ **上表 DSZL-107 那列結案確認(2026-09-12 per user)**:原先我判「③ 線性度子項未結」是**過時的**
—— per user「**DSZL-107 這個好像校正過了,現在應該是準的**」。⇒ **量測面全部結案**(正負號＋量值＋線性度),
不需要再補 30~60 kg 多點量測或 X518 裝置端標定。
🔴 **唯一遺留的是工程面:校出來的 scale 沒地方存**(見上表「張力/平衡」群第一條),
與「吊機執行期參數不持久化」同一件事。

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

---## 🆕 新架構待辦（2026-08-27 設計彙整，與上表的現行程式碼待辦分開，共 27 項）

> 📌 **這一節屬於新一代機器的規格文件 `.claude/reference/洗窗機器人設計彙整.md`（v3，2026-08-27），
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

> ⏸ **2026-09-01 per user：整張表暫緩，包含四條 🔴 安全項。**
> 🔴 **優先度刻意維持 🔴、沒有降級** —— 「暫緩」是「現在不做」，不是「風險降低了」。
> 這四條的共同性質是**「以為已經存在、實際上不存在」的保護**，新機器一旦開始組裝就會立刻生效：
> Pi 當機後螺旋槳停不下來、帶水設備上有 220V AC、22 吋碳纖槳尖速超過 100 m/s、
> 電源 57.6V 已超出 6–12S 版 ESC 上限。
> 📌 **恢復條件**：新架構開始實體製作時，這四條必須在通電前先處理完。


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
| 🔴 | 硬體看門狗 | 485→PWM **斷線維持輸出**，Pi 當機後螺旋槳無法停止 | **⏸ 暫緩（2026-09-01 per user）** — 獨立於 RS485 的硬體電路，逾時直接切斷 ESC 電源 | 設計彙整 §6 已知待解 |
| 🔴 | ESC 電壓版本 | FLAME 100A 有 6–12S 與 6–14S 兩版，電源 57.6V 超過 12S 上限 | **⏸ 暫緩（2026-09-01 per user）** — 確認為 14S 版，或將 NPP 輸出調至 50V 以下 | 設計彙整 §6 已知待解 |
| 🟡 | 螺旋槳成對 | 同向旋轉會產生淨反扭矩，使機體繞鋼索旋轉 | P22×6.6 須 CW/CCW 成對，接線相序相反 | 設計彙整 §6 已知待解 |
| 🟡 | 單邊推力失效 | 一顆 NPP 故障會造成左右推力不平衡 | 兩顆的 DC OK 訊號接入 Pi，任一失效即同步降載 | 設計彙整 §6 已知待解 |
| 🟡 | AC 側壓降 | 兩顆 NPP 加控制電源約 3.7kW，220V 單相約 17A，200m 壓降偏高 | 確認電纜線徑，或改送 380V 三相 | 設計彙整 §6 已知待解 |
| 🔴 | 漏電保護 | 帶水作業，設備上有 220V AC | **⏸ 暫緩（2026-09-01 per user）** — 漏電斷路器（RCD）**為必要，非選配** | 設計彙整 §6 已知待解 |
| 🔴 | 螺旋槳防護 | 22 吋碳纖槳葉尖速度超過 100 m/s | **⏸ 暫緩（2026-09-01 per user）** — 護網或護罩，地面裝機測試時尤其必要 | 設計彙整 §6 已知待解 |
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

## 📦 歷史日誌壓縮(2026-09-12)—— 2026-07-22 ~ 2026-09-09 詳細條目已依 CLAUDE.md 規則壓成歷史摘要 #8–#15

> 原 10,085 行詳細日誌(壓縮前為本檔 1168–11252 行)壓成下方 8 段摘要;只留待辦/決策(含被否決)/伏筆/踩坑,
> 例行維護與逐條指令已丟。**完整原文在 git:`1ecc6be` 及更早版本的 `.claude/work_log.md`。**
> 🔴 待辦零遺漏原則:壓縮前逐條比對「待辦總表」,總表已有者略去;比對不到的 81 條先保全於本區末尾的「壓縮保全清單」。
> ✅ **2026-09-12 當日已核對清空** —— 真遺漏已併入待辦總表的「🟡 2026-09-12 由歷史壓縮併入」一節,保全清單任務結束。

### 歷史摘要 #8（2026-09-08 ~ 2026-09-09）— 隧道干擾定性為電氣問題；watchdog 守不到它該守的；MH300 Phase 3-a；繼電器 confirm 拍板

> **規範權威：** `.claude/changelog.md` `[2026-09-08*]`／`[2026-09-09*]`；`runbook.md` §A0（照 `ps` 逐字啟動）＋ §A4（`main` 快照）；`.claude/mh300_migration_plan.md`（Phase 3 段已改寫）；待辦總表 09-08 新增 7 列 + 09-09 watchdog 5 列。

**決策 / 伏筆**
1. (09-08) 隧道 = Blue Robotics **Fathom-X（HomePlug AV，2–30 MHz）**。受控實驗：VFD 一運轉隧道掉 **90%** 封包（hold 期間 9/90，同一秒 WiFi 90/90 全通），roll age 758→7,206 ms，平衡全程退回 `src=meter` ⇒ **電氣干擾，不是軟體**。決定性測試＝吊機端 Fathom-X 改獨立電源（電池）再量；其餘（板子位置／供電是否與 VFD 共用／線材是否與動力線同束）全需 per user 現場資訊。**per user 拍板：最終走有線，現階段 bench 維持 WiFi**；「eth 串接後改 `CRANE_IP`」那條因此結案。
2. (09-08) **`IMU_ROLL_STALE_MS=750` 排在干擾處置之後，不要先動**：同日稍早寫成「切有線前必改」，被自己的實驗推翻（放寬到 1250 實測更糟、已回退）——放寬門檻＝把運動時 90% 死掉的鏈路蓋起來。WiFi 上 120 s 零超標，這個缺陷在 bench 完全隱形。「每 5.5 秒破一次」已收回（空窗 0.6~10.6 s，非週期）。
3. (09-08) `crane_endpoint_ip_()` 成為吊機位址**單一來源**（5 處含 OVERWEIGHT stop 全收斂）；新增 `ep::has_host_override()`（舊判定 `overridden != CRANE_IP`，覆蓋值等於 `CRANE_IP` 時辨識不出）。🔴 **本體啟動必帶 `FCV_EP_CRANE_HOST=192.168.5.25`**——不帶不報錯，只會安靜走上掉 90% 的有線。同時推翻 runbook／待辦表「`CRANE_IP` 無 env 覆蓋、漂一次重編一次」的過期記載。
4. (09-09) **吊機 watchdog 心跳不分連線來源**：GUI 經 loopback 的 status 輪詢永遠餵心跳，roll 斷 7.2 s（> `WATCHDOG_TIMEOUT_MS_IDLE=2000`）仍不響；本體端 crane watchdog 自 `4d1409c` 起是**死碼**；隧道斷了本體不會自己停（`crane_cmd_` 是自癒不是偵測；hold／閒置零偵測）。AI-2 兩輪診斷併入待辦表 5 列，**等 per user 拍板**：①兩條急停路徑丟棄回傳值＋走會被 `crane_mtx_` 卡住的主通道（IMU 45° 自動急停尤其）②本體零背景偵測→最低成本是把 `imu_push_loop_` 既有 socket 丟給 `handle_crane_evt_`（不新增流量，避開 05-15 zombie socket）③hold 期間操作者掉線繩子不停→`server.js` 在 `ws close` 補送 off ④吊機端 `cmd_status` 加 `peer_age_ms` 先觀測不改判定 ⑤本體死碼 watchdog 補回或明確刪（留現狀最糟）。另 AI-2 標未實測的「`crane_mtx_` 持有期間急停送不出去」要獨立驗證。
5. (09-09) **MH300 Phase 3-a 已實作未部署**（現役仍 SE3）：`init()` 無條件清 `0x2002`；process-local `base_blocked_` 重啟後 B.B 永不清是設計意圖；旗標讀失敗值選 `true`。Phase 3-b／3-c（keepalive 簡化、fault code 對照表）未動。`mh300_migration_plan.md` 曾把 Phase 1/2/4 已完成說成待做，真正的風險（Phase 3 邏輯差異）被埋在假待辦中間。
6. (09-08) 繼電器「延遲」= 前端 confirm 700 ms，不是 bus。**per user 拍板：console v2 繼電器 confirm 全拿掉——不要當 bug 修回**；否決「按鈕時停止問狀態」。`pwm save`／`zdt_zero`／`rail_cfg_soft_enable` 三個 confirm 要不要補回**等拍板**（`pwm save` 寫壽命僅 1~2 千次的 flash）。
7. (09-08) Phase 2.3（本體背景吃吊機 EVT）**素樸版不該上**：`TCP_server::broadcast` 是阻塞 `send`、recv buf 128 必切幀；要做走 Option B `record_crane_evt_`。`imu_push_loop_` 的 connect 無逾時。
8. (09-08) WiFi 冷路徑 80–100% 單向丟包，**powersave 假說未證實**（吊機 Pi 4 + brcmfmac 吃驅動預設）；要不要裝 `iw` 或直接關 powersave 等 per user。隧道閒置 1.06% 丟包未查。
9. (09-08) 四顆推桿空載一致性 **1.03×** ⇒ 致動端排除，「用尺量四個吸盤到牆面間距」價值升高（四顆各量，不只比左右）。JC-100 出現切段／錯位族（每 3~6 分鐘一筆 TIMEOUT）⇒ USR 網關 `_pt` 0→5 ms 的證據基礎變了，**待重評**。吊機 `set_imu_roll` dispatch log 189:1 洗版待節流。
10. (09-08) `zero_meters top` 座標事故：top 0=頂 vs 腳本 0=底；`ground` 不沖 `home_ground_cm`；TOP 漂 ±2 cm。`roll_correct` 1 cm≈3–4° ⇒ `ROLL_RECOVER_OK=3.0`、`roll_recover` 守衛改「有沒有改善」；`hold_loop` 平衡被 `cur_sync_dir` 圈住（滑台掃動期間無平衡）；姿態每 10 趟 +1° 是**捲筒特性（per user，非故障）**；兩段式水平修正首入 ±1°。
11. (09-08) 吊機／本體**不共電**結案。`motor_api` 不自啟，等 per user 確認手臂裝回、周邊淨空（啟動即自動就緒＝可能動作；M2 重裝後 ±1.5 rad 內 INIT 靜默移錯的風險仍在）。GUI 線歸 AI-2（`web_backend/` 全部）。

**⚠ 踩坑 / 教訓**
1. (09-08) `pkill -f "sleep infinity"` 會把主程式一起關掉（它是 FIFO 的常駐 writer）——用 `readlink /proc/<pid>/fd/1` 找對的那個；runbook §A0 從此「照 `ps` 逐字」。
2. (09-08) **三條假待辦**：兩條線並行時各自的「待完成」會互相過期（機器線 20:12 寫、GUI 線 19:19 已做完）⇒ 收工前掃一次同日另一條線；**結論改了要回頭改由它產生的待辦**；文件第四型過期＝把已完成說成待做。
3. (09-08) Pi 無 RTC、`/dev/tty` mtime 不可當證據；分辨「做沒做」看檔案時間戳不是回想。
4. (09-09) 「有沒有保護」要**找誰讀這個值**——watchdog 看起來有、實際守不到；本體 watchdog 看起來有、其實是死碼。
5. (09-08) 4 Hz 逐筆 log（40 分鐘 9,997 行）把真實事件埋掉——洗版本身就是缺陷。

### 歷史摘要 #9（2026-09-04 ~ 2026-09-07）— 目標壓力定為 15 Nm；③b 改讀 DEPLOY_F；M2 hold_kp 7→31；console v2 上線但從未經瀏覽器；driver 手冊落差

> **規範權威：** `.claude/changelog.md` `[2026-09-04*]`／`[2026-09-07m1~m6, g1~g6]`；`doc/` 14 裝置手冊摘要（09-07 新增）；commit `937656c`（09-07 全日合併為單一 commit，per user）；`web_backend/tools/check_console.js`。

**決策 / 伏筆**
1. (09-04) **目標壓力 15 Nm（per user 現場目視定案）**；本高度 `wall_mm=520` 維持不改 496。**目標是維持 tau≈15，不是固定 wall_mm**——牆距隨高度變：同一 `DEPLOY 520 RIGHT` 在高度 229 只壓 6.01，其他高度 14.31。做法未定（查表／SD76 高度算／每高度先探），至少頂／中／底三高度各實測一次。**`DEPLOY 505` 一次就能驗，是當日最該做而沒做的。** 15 Nm 對刮刀可能不對（刮刀在 `length=-129` 壓不到 15）。
2. (09-04) `cycle_test.py` ③b 的 `tau>8.0` **整條換掉，改讀 `DEPLOY_F` 回覆**（OK／WARN／`no_wall` 跳過／`obstacle` 後翻案跳過）；橫桿是牆面結構⇒跳過續行（`obstacle_steps` 分開記）。但 `cycle_test` 本身**還沒實跑過新 ③b**；`DEPLOY_F` 的 WARN 分支從 motor_api 到 GUI **沒有一段被真的走過**。
3. (09-04) `THETA_MIN 0.45→0.565`、`THETA_MAX 0.95→1.10`（實測橫桿 0.5449–0.5510）；`theta_max` 無上界實測（最遠 0.9581）；玻璃接觸角只有下半部兩點。`hold_pos` 推壓（手臂抖 15.8→10.6 s）；力控 20 輪 +0.68 系統偏，`TOL` 1.0→0.4 **待決**（每迭代 +1.5 s）。
4. (09-04) **M2 `hold_kp` 7→31**（1.88 < 2.0 靜摩擦）；3.4° 可否接受待 per user，n=1；M1 要手扳不動需 kp 514>500＝機械煞車題。STARTUP 自動就緒（只 `go_home` 不 CALIBRATE；`ARM_NO_AUTOSTART`）；`LR_CALIBRATE` 會 `set_zero` 不能拿來量停點。
5. (09-04) **本地 console 只認 `exit`/`quit`/`status`**，`set_*` 靜默丟棄要走 TCP；設參數要在人碰 GUI 之前。快送提速不動（移動 25%、滑台 44%）。腳本鎖三缺口：33 秒無鎖 `state=idle`；後端「作業佔用」旗標牽涉安全語意（哪些擋、STOP/PARK 豁免、異常釋放）**等拍板**；STOP/PARK 不鎖。吸盤「至少一顆」判準待決。
6. (09-04→09-07) console v2（8081）：手臂面板、keepalive `pong` 冒充回覆前端用簽章表擋（簽章寫死是代價），`server.js` 根治**留待決定**（動生產 8080）；逾時前端>後端；滿刻度＝門檻是病根；`.man` 鎖改 `disabled`；吸盤 1~2 顆達標要不要警示是判準等使用者定義；運動中 250 ms 輪詢已做。**🔴🔴 真人瀏覽器從上線到現在從未實測**（按鈕、觸控戴手套、陽光可讀性、未收斂顯示、姿態圖符號方向）——本線最大未知。
7. (09-07) 清掉三處「同一件事寫在兩個地方」：`STEP_CM_MAX` 三邊不一致→向後端取值；CH15→CH5；roll 門檻第三處說明字串。`balance_source` 編譯預設 `Meter→Imu`（**仍非持久化**：GUI Setting 表「重啟後遺失」那格不能改成「不會遺失」；binary 部署時 `public_v2/index.html` 四處要同一動作更新，部署前不要先改）；`cmd_brush`/`cmd_water_pump` 補回讀；`OK pump_set_but_readback_fail` 前綴查證後不改。
8. (09-07) driver vs 手冊落差（皆非急件）：`WT901BC` 0x56 氣壓路徑是死碼、不解析 0x54、同步不驗封包類型；**`DSZL_107` 名稱誤導（實為 X518 採集器協議）**；X518 裝置端實物標定（`0x0A1E`/`0x0A20`）沒用——可答 4.16 kg 外推線性度；XKC-Y25 恢復出廠掉回 9600 會從 `.22` 安靜消失。VS 整組移除（`79a2312`，`build_linux_test.sh` 唯一副本）。
9. (09-07) `main` 領先 `origin/main` 16 未 push；`tmp/` per user 最後處理；Web GUI 8080/8081 非 systemd 不自啟；`ROLL_TRIP` 根治靠 `cycle_test` 起跑回報參數 EVT。

**⚠ 踩坑 / 教訓**
1. (09-07) `cmd_pump` 謊報成功 **09-03 就修好了，待辦表過期 5 天**——修完當天要回頭翻那一列（帳的問題不是 bug 的問題）。
2. (09-07) headless 截圖 `load` ≠ 畫完；HTTP 200 + 標題正確 ≠ 畫面對。
3. (09-04) `[M1]` 前綴／WARN 解析 bug；逾時前端比後端短會把還在走的判成失敗。
4. (09-07) 版號沒更新就部署，使用者無法確認是不是新版（8080 `index.html` 沒版號欄位）。
5. (09-04) 我把一支「~3 分、安全」的函式推薦出去時**還沒讀它**——`LR_CALIBRATE` 會 set_zero。

### 歷史摘要 #10（2026-09-02 ~ 2026-09-03）— 幾何／重力模型重擬；上滑台從零到可用；10/10 首次完整；QX 移 `.21`；座標與真空假說翻案

> **規範權威：** `.claude/changelog.md` `[2026-09-02*]`（續一～續二十一）／`[2026-09-03*]`；`cleaning_arm/main_api.{h,cpp}`（`M2_SLOT_*_RAD`、`TOOL_EXT_*`、`hold_kp`）；`summaries/DM2J_RS_MODBUS_SUMMARY.md`（Pr4.02）；`cycle_test.py`；`.claude/summaries/QX_DO24*`。

**決策 / 伏筆**
1. (09-02) **幾何模型錯 1.53×**：`490·sin(θ−0.38)+121`（舊 320 只到 65%）；可能真因是**角度標度 1.53×**，待量角器獨立驗證。重力 K 20.87→16.09（擬合區 0.42~0.64，0.65+ 外推，與舊擬合在 0.64 差 2.17 Nm ⇒ 至少一邊錯）。`TOOL_EXT` 204.32/192.37（動 `ARM_LENGTH` 必一起重量）；`TOOL_EXT_CENTER=160` 是三者中唯一未實測、且是 `wall_mm=520` 的錨點。
2. (09-02) `wall_mm`：**380 才對（08-28 的 400 是錯的）**→530→520；`hold_kp` 34→90 是舊模型下的補償值（可能可調回）；`hold_ki` 移除（需 anti-windup on contact 才能回）；INIT 門檻 1.5→3.0；`CONV_TOL` 0.15→0.02（「摩擦飽和」是容差假象）；`touch_wall` 探測 ±0.3→±0.05（M1 起步頓挫真因）。
3. (09-02) M2 三位置絕對值＋停點相對定位（+0.7204；負向 2 rad 內無停點）；**未解矛盾**：per user 手轉 +0.5316/−1.0115 vs `LR_CALIBRATE` 停點 +0.7204。M2 掃動被摩擦帶著扭轉 0.45–0.49 rad 是固有特性；黏滑真因＝07-27 per user `use_park_profile`（三次修法全被量測推翻）；「M2 摩擦約 2 Nm」是容差假象下量的數字。起步踢擊 18 Nm 未根除（1/4）。
4. (09-02) 上滑台從零到可用：Pr4.02 DI1→8（常開，軟體失能）；舊 `0x2233` 其實是**恢復出廠**；手動歸零；行程 140→130；400 rpm/3000（失步未驗，無回授只能拿尺）；`rail_pos` 非量測；`rail_move` 三缺陷；140 端玻璃遠約 6 mm；缺一支唯讀組態查詢指令；`rail_enable off` 能否手推未驗。
5. (09-02) 滾筒繼電器 DEPLOY 前開（兩複本只改一份）；**`verify_arm_deploy_` 自 06-06 起無條件 `return false`**——障礙偵測三個月沒在跑，要改成比對校正過的接觸角才能移除；`go_home_slot` 速度安全閥（|vel| 2.2 rad/s 無煞車）抽共用；`LR_SLOT` M1 先離玻璃守衛；M1 DIAG 50 Hz 內部記錄。
6. (09-03) **真空假說推翻**（`0902d` 有真空是四輪最好）；座標第二次重定義：0=底、往上負、頂 −231（`check_envelope` 符號）；**吸盤吸著收繩是危險操作（per user 指出）**，現場保留＝機構仍在中止狀態。
7. (09-03) 真空「讀太早」per user 更正：`VAC_OK −50/10s`、至少一顆；壓力欄是快照。QX-DO24 四類失敗分辨（`unverified` 折衷）；**`.22` 故障⇒QX 移 `.21`（per user 新網關）**，問題在 `.22` 側不在模組，08-28「第二主站」假設重評；**`.22` 現只剩 JC100×4，下次長測看它獨處穩不穩**。吸不到＝橫桿（per user）；「吸不到記錄不中止」；推桿分組量到的是**上/下**不是左/右。
8. (09-03) 10/10 首次完整（62.6 s/m；移動 24%）；`level_diff` 是**高度的函數**＋遲滯 ±0.4°；三個執行期參數斷電全回預設（`motion_hz` 50）；backlog 5 說法推翻；DY-500 移除繫結；手臂電源鎖存只能斷電；`STATUS` 凍結值（ENABLE 後才可信）；bail 補 `arm_park`；`pwm restart` 被 `paused_on_error` 擋→拿掉；QX FC06 退路先讀再寫（手冊說保持）；位元級損毀非分片（`SHORT_FRAME 0`）；右側離牆近假說。
9. (09-03) `cycle_test` 三 bug（沒部署／缺 ENABLE／`ask` 前綴）修了但**整合 ③b 未跑完一輪**；本機 `main` fast-forward `74ce8be`，`origin/main` 推不推等拍板；runbook 三條事實更正未寫回。

**⚠ 踩坑 / 教訓**
1. (09-02) **9 次憑假設改參數全被推翻**——先量再改（黏滑三次還原、幾何 1.53×、摩擦模型）。
2. (09-02) 「兩份只改一份」再犯（滾筒繼電器順序）；自己的腳本沒做「abort 出口要無條件關滾筒」。
3. (09-03) `cmd_pump` 謊報成功：自己當天稍早寫進日誌「一律要回讀」，然後自己沒做。
4. (09-03) 3.2 秒就下結論——報的失敗有時只是還沒走完；`listen(fd,5)` 是 accept 佇列不是連線數上限。
5. (09-03) 兩台 WiFi／`tmux` 沒裝／`~/bringup` 不是 git，時間戳與版本要另外驗。

### 歷史摘要 #11（2026-09-01）— 10 趟 ±1° 任務；IMU 驅動平衡；`available()` 跨執行緒改 socket 模式是連線失步真根因；X518 雙通道與張力校正；深度相機連根拔除；待辦表校準

> **規範權威：** `.claude/changelog.md` `[2026-09-01*]`（續一～續十八）；`Crane_control_PI/main.cpp`（`BalanceSource`、`TENSION_MAX/DIFF`、`fine_adjust`）；`transport/TCP_client.cpp`（`available()`、`rx_timeout_streak`）；`Linux_test/dszl_sign_test.py`；`ONBOARDING.md` §8；`.claude/reference/`（site_profile 計畫）；memory `project_v2_crane_meter_read_fail_OPEN`。

**決策 / 伏筆**
1. 任務目標定案：**10 趟 ±1°**（L=229、離地 88）。IMU 驅動平衡（`BalanceSource`；`kp 2.0`／`deadband 0.5`／`stale 750` 都是未調初值，只在 10 Hz、≤40 cm 驗過）；第三條連線一律推；符號 `err=−direction×roll` 曾寫錯放大。**IMU 差動校平自 08-27 起是 no-op**（讀 `imu_.z` yaw）已修。平衡 gate 逐側 `base_hz`；`diff_tol` 拆出；`sync_start` 啟動驗證 500 ms。
2. **連線失步真根因：`available()` 跨執行緒改 socket 模式且不拿鎖**（改 `MSG_PEEK|MSG_DONTWAIT`）；ZDT 交易改 `txn()`；101 ms 指紋。`rx_timeout_streak` 10 主動斷線已上線**但未被觸發過**；DSZL txid 重同步僅實測復原一次。
3. 貼牆「0% 出帶」是 9 筆假象→實為 31.7%；對照組關平衡 67% 單調惡化⇒**機構固定偏差**。**重心偏左三選一（配重／改吊點／接受並補償）是決策題**，先於任何調參（09-04 後修正：懸空重心平）。
4. X518 兩台改**一台雙通道**（`set_channel`；`do_zero_ch1` 坑；單 TCP；`set_unit` 只 RAM 差 9.8×）。張力校正 4.16 kg（`−0.0236364`/`−0.0205816`；順序必要）；單點外推 30~60 kg 線性度未驗；**量到之後值放哪裡（scale 無持久化）要先決定**；「反推」不能結刻度那條待辦。DSZL 正負號結案（🔴🔴→🔴）。
5. 門檻常數化 130/75（`TENSION_MAX` 100 單側>整機 94；`DIFF` 50 用掉一半；等重心決定後一起調）；四支裸 send/recv 對改原子交易；XKC wrongslave 補；`fine_adjust level_diff` 執行期 4→5 **不寫預設**（可能隨繩長變）；右繩 1 秒暴走 5 cm（`tension_diff` 50 不宜放寬）；`length_diff_max_cm=15` 允 11°，應依 0.8°/cm 重訂；`FOLLOWER_SPAN_CM=100` PLACEHOLDER；Part A/B/C（roll_correct 加減速→`FOLLOWER_ROLL_TOL_DEG` 2→1→SPAN 校正）順序不可倒。
6. 繼電器逐顆實測：**CH3 記載為幫浦 B 從沒開過（錯格）**、CH6 正壓閥 500 ms；拆「改回 3」地雷；relay 只 Idle/Ready；重啟繼電器全 OFF。`extend_raw` 玻璃有縫是設計要求（per user）。init 在歪 2.92° 取基準（讀 `raw_x`、用 `imu_zero`）；左右差守衛連續 3 筆而 roll 瞬時；致動解析度 1 cm→5 cm；`mission_run` 方向寫反 per user 攔下。
7. DSZL 兩側 ERR＝TCP 失步（`502 refused` 誤導；讀失敗＝無警報→中止計畫）；`meter_left_read_fail` workaround 重啟仍有效，`[DSZL:1]` 分不出左右（08-31 的 `@L/@R` 沒涵蓋 `get_tension_kg`）；X518 連線數上限未量。
8. `site_profile` 缺口（吊機無 profile；安全互鎖不進 config）計畫待核准；`log_utils.h`→`common/`；**深度相機連根拔除（per user 選 B）**：dispatcher 保留 `ERR removed_2026_09`；feet hook 刻意保留（有真實呼叫端）；ONBOARDING §8 標頭不刪；harness 基線需重取；2D 相機 `obstacle_*` 殘留未處理。**新架構待辦表整張 ⏸ 暫緩（per user）**。
9. 待辦表校準（per user）🔴 3→2：**RPM 常數列記載錯——08-31 已否決「RPM 重新評估」，不要再提**；`balance_source` 編譯預設 `meter` 是陷阱（production 用 `imu`；09-07 改為編譯預設 `Imu`）。全部改動當日尚未 commit。

**⚠ 踩坑 / 教訓**
1. 「貼牆 0% 出帶」是 9 筆樣本的假象——n 小不下結論；對照組要做。
2. `502 connection refused` 誤導成網路問題，實際是 TCP 失步；讀失敗＝無警報＝安全監控形同不存在。
3. 幾何 0.8°/cm、繩距 80 ⇒ IMU 45° 永不先觸發，`length_diff_max` 15 允 11°——門檻要從幾何推，不是抄舊值。
4. `launch.sh` 每次啟動漏一個 `sleep` process。
5. 「已修」在 cherry-pick／改碼後會過期而不出聲——勾掉時寫憑什麼（commit／檔案:行）。

### 歷史摘要 #12（2026-08-31）— `fix/driver-crc` 上機階段 A；交替步伐停用；VFD 寫入間歇失敗吃掉減速；SE3 每次開機 OPT；PQW 拉垮 RS485；`main` 端到端四坑

> **規範權威：** `.claude/changelog.md` `[2026-08-31*]`；`runbook.md` §A2（9 條預期差異、階段 A/B/C/D 檢查表）＋ §A4（`main` 專用建置／`launch.sh` FIFO）；`command/dispatcher.cpp`（10 個 alt 指令攔截）；`transport/TCP_client.cpp`（`reconnectLoop` 限流）；`~/bringup/{se3_fault,pqw_probe,meter_probe,mb_scan}`（唯讀探測工具）。

**決策 / 伏筆**
1. `fix/driver-crc` 上機 16 TU；`CRANE_IP` `.17→.25`；階段 A 無動作檢查通過（lead 7.731、`SO_ERROR` 雙向、`zdt_pusher` 範圍外指令當指紋）；⑨a 放錯階段。**500 rpm 失步 0.2–0.3 mm/橫越＝不可用；per user 決策：RPM 搜尋不做、不要再提議。** `CUP_PULSE_PER_CM=3000` 正確（08-27 的 2857 是改錯）；吸盤歸屬右={5,7} 左={6,8} 實機驗證。
2. **現行操作模式（per user）**：吊機拉到頂樓→風扇壓→吊機收放繩→定點關風扇→推桿 10 cm→滾筒 50 cm→步 <50。⇒ **交替步伐 v1 遺留架構不可用**（單閥；`pre_cycle` 矛盾；`valve_ch` 虛構；`do_cross_obstacle_` 那句「shared valve 絕不會被切」註解寫在唯一會發生的那行上方）。五處實作停用：dispatcher 攔 10 個指令名、`run`/`run_script` 預設 `alt→sync`、進場守衛；8 條路徑實機全擋、狀態機零變動。per user：跨障礙不需替代方案（現行就是一般移動，4 輪有避震）。原碼一行未動，日後正式清理仍在原地；`cmd_vacuum(group)` 接受 left/right 但實際全域。
3. `STEP_CM_MAX` 100→45（`WASH_ROBOT.cpp:1263` 脫鉤；`settings.json` 舊值警告）；`PUSHER_EXTEND` 36000→30000；中斷仍執行；`cup_move` 用 300 非 0；`LOG_ERR` 脫離 `debug_mode` 限流（**`LOG_WRN` 仍被蓋住，待評估**）；driver log 補 `@L/@R`（SE3/DSZL/MH300）。
4. **VFD 寫入間歇失敗會吃掉減速命令**：每個動作指令恰好一筆 SE3 寫入失敗；短程 `cm ≤ CMD_MEASURED_APPROACH_CM(8)` 改直接以 `fine_adjust_hz` 起步（過衝 10 cm→準確）。per user：過衝另一半是 6 mm 鋼索彈性。**SE3 每次開機帶 FAULT bit＝OPT（通訊逾時），良性，`07-10=0` 不要改**（=1 是斷線馬達照轉）；但 H1007/H1008 四槽全 OPT ⇒ 故障歷史無鑑別力，這就是待辦 ⑥「VFD 故障碼」查不清的原因；程式碼註解「addresses unverified」過期（規格書有載）。
5. **PQW 拉垮整條 RS485 維持拔除（per user）**；PQW 設定區被清空已寫回 12/7；`init()` 補探測（3 次重試）；三個裝置同時在線就垮，決定性測試（只接一支計米器＋PQW）未做；鮑率超規格誤判——不拿規格書推翻現場。**待辦：PQW 模組需更換／檢修**，換上後同時複驗兩件事。10 支 `init()` 不探測是刻意的（9 個新開機失敗點，加要比照 PQW 帶重試，由使用者拍板）；init 訊息誠實化（`presence not probed`）。
6. `group_seal_ok_` 分側 `SEAL_MIN_CUPS_PER_SIDE=1`（歸屬修好後「4 顆有 2 顆」的前提消失；realign 三向測試）。`CRANE_IP` 自動選路 `resolve_crane_ip_`（非阻塞 300 ms；不放 `ep::host`）。減速遮罩 `est_ms`/`motion_ms` 兩語意分開（未實機驗證，要拿 `motion complete at t=Xms` 校驗）。`cmd_hold` 補 motion 互斥（只鎖 `on`，`off` 永遠放行；`cmd_manual` 刻意不鎖）；左右繩長差硬警報第一版用絕對差被上機資料打臉（兩支 SD76 零點獨立，改相對位移差），門檻 15 cm 待使用者確認。`crane_cli_` 重連洗版 214→9 行（刻意不用 `quiet_reconnect_log`，吊機離線是要被看見的事件）。
7. ⑨b `cmd_arm_sweep` 補 `abort_flag=false`，**但 ONBOARDING §1／runbook §A2「永久卡死」的嚴重性是誇大的**（`reset` 就恢復），不該再宣稱。ZDT 有編碼器（10 脈衝/度）、DM2J 無回授——排實機驗證先問這個軸有沒有回授。`do_arm_sweep_` 會開滾筒（CH5 實體接線仍待現場確認）。吊機計米器「兩側同時 `length=ERR`」不可重現，兩條錯路已記（新幀驗證擋讀取／`stop`、DEBUG 是變因）。`pay_out_right 1` 回 `moved=2cm` 但計米器變 9 單位，單位換算未驗。**per user：左繩不可再收繩**；SD76 兩支已依指示歸零。
8. `main` 端到端（`~/main_20260831/` 保留，per user）四坑：**吊機 WiFi IP `.17→.25` 而 `CRANE_IP` 是編譯常數**（blocking `connect()` 對不存在主機卡 2 分鐘；runbook「連不到只 WARN」只在對方送 RST 時成立）；`main` 目錄結構與重構分支完全不同（§A4）；**stdin 給 `/dev/null` 程式跑完 init 就自己關**（`while getline`），改 FIFO + `sleep infinity`；`stdbuf -oL` 不能省；兩台沒 `tmux`。`~/.ssh/config` `IdentityFile` 列舉式，金鑰改名 `claudeuser` 後沒列到的位址一律 publickey 拒絕。

**⚠ 踩坑 / 教訓**
1. **驗證清單只該放預期被拒絕的指令**——同一天兩次把會動的指令（`step_down_sync 10`、`up_left on`）放進清單，左繩因此被收了 12 cm。
2. 用固定行數窗口判斷「某段沒有 X」不可靠（兩次：稽核溢進姊妹函式得假 ✅；`sed -n` 少兩行推出錯誤可達路徑）——範圍要由函式邊界界定。
3. 「沒有壞訊息」≠「修好了」——要讓它印出來才算驗到（`@L/@R` 首輪 0 筆證明不了任何事）。
4. 洗版的解法不是靜音，是換成每行都帶新資訊的輸出。
5. `mb_scan` 回覆晚一拍；探測工具要與主程式一起重編；逐一移除二分法比猜快。
6. `[SHUTDOWN]` 印出不等於結束（本體 5 s、吊機 10 s 才退出，看 `ss -ltn`）。

### 歷史摘要 #13（2026-08-29 ~ 2026-08-30）— 待辦總表雙向校準；null-client 掃描；合併 `6523b54`（PWM 重試疊加）；重構計畫階段 0–5 與 harness 預期差異 6/9

> **規範權威：** `.claude/plans/refactor_plan.md`（§5.5 `prove_noop`、§5.6 兩套工具分工）；`runbook.md` §A2（9 條預期差異＝等價規格；`git tag main-final`=`6523b54`）＋ §A3（`cl /Zs` 語法檢查）；`harness/`（`compare.sh`／`prove_noop.sh`／`expected_diffs.sh`／`check_so_error.sh`／`cmds/`）；`common/endpoints.h`／`common/profile.h`＋`config/axis_profile.txt`；`command/dispatcher.{h,cpp}`；`mechanism/`（`RopeAxis`）；`Linux_test/fake_slaves/`。

**決策 / 伏筆**
1. (08-29) **待辦總表自己落後程式碼**：「未修」側 80 列 7 列錯；「已修」側 32 列 8 列錯（三條跨分支警語過期、四條攝影機列「作廢」理由是假的——移除的是 GUI 不是後端、19 列路徑過期）。從此勾掉要寫憑什麼（commit／檔案:行）＋日期。`retract_tension_stop_kg` 實為 50 非 25，該列 🔴→🟡 但根因（scale `-0.01` 佔位）沒變。`vfd_fault` 兩側 `read_fail` 的「MH300 遷移未完成」歸因站不住（跑的就是 SE3）。
2. (08-29) **整理分支已不是純整理**：9 條刻意行為改變。**per user 拍板直接上 `fix/driver-crc`、不分兩段**；§A2 驗收判準翻面為「預期差異表上沒有的差異才是訊號」。上滑台 **homing 這條路放棄（per user：無原點感測器，靠斷電前回 0 + 斷電煞車）**——`WASH_ROBOT.h:622-625`「真正的解法是啟用 homing」是錯的；`HOME_DONE=0`；homing 速度 200 rpm=25.8 cm/s、`overrun=0` 危險。殘餘風險：異常斷電來不及回 0 座標系整個偏移無人被告知。座標正方向＝往右、0＝左端硬限位（PR move 不用 JOG）。
3. (08-29) 合併 `origin/main` `6523b54`（per user 更正：對方**已實機驗證**，commit message「未驗證」過期，行為上他們是權威）。**PWM 重試疊加**（應用層 3×driver 層 3＝9 次+480 ms）git 一字不說 ⇒ `PWM_STEP_WRITE_TRIES` 3→1，重試單獨留 driver 層（40 ms vs 對方驗過的 120 ms，要調的是 `kBackoffMs`）。`CH_BRUSH` 15→5（**兩邊記載互相矛盾都聲稱有實體依據**，以實機跑過的為準，矛盾原地記在 `.h`）；對方 commit message 列了沒有的改動（`UP_STOP_TOTAL_KG` 50→70 是 `0d5f6bc` 改的）。
4. (08-29 交接條目，Sadie-fang) main 的 08-28 批次：同步步伐 PWM 7%/5%（只寫占空比，per user）；清洗恢復；**自動補救全部停用**（後退重吸／原地補伸 `#if 0`，吸不好就停住等人）；`imu_persistently_bad_` 修逐封包 `read_error` gate；`STEP_CM_MAX` 80→100；`run_script_pre` 先洗第一格（按 run 先靜止 30 s 不是當機）。已知未解：`DEPLOY 400` 兩側失敗（LEFT err 0.293、RIGHT `[M1 SAFETY] vel>0.4`）；`ROLL PANIC −150.9°`；`meter_left_read_fail` 兩層靜默＋單次失敗即 invalidate（建議 rate-limited log + 連續 N 次）；script `n` 旗標無作用；`Linux_test` menu 5 channel map 是舊的。
5. (08-29) null-client：`!client\b` pattern 誤判雙向（實際 8 支 38 處＋CLV900）；`trigger_sync_move()` 廣播永遠回失敗已修（`readEcho(200)` 降格為排空，200 ms 刻意不動）；4 個 `.vcxproj.user` 移出版控。**08-29 整串 C++ 一行未編**（本機無 `cc1plus`），`cl /Zs` 只證語法（VS 路徑是 `18\Community`、.bat 必須 CRLF）。`scripts/wr.sh` depth window（`:66`）待 user 決定。
6. (08-29~30) 重構計畫：4→**6 層 + 橫切安全層**（機構層／指令層；吊機一條繩＝三裝置三匯流排，強套 `IAxis` 閉環會沒有家；`cleaning_arm` 不拉進來）。等價基準＝`main-final` + 9 條預期差異。階段 1 刪 3,993 行 `#if 0` 死碼（`cl /EP` 逐位元證明，先查無 `#else`）；`prove_noop.sh`（預處理逐位元；能用它就不動 `compare.sh`）**第一次派上用場救的是我自己**（regex 刪整塊 `#define` 把安全互鎖靜默變成編譯期預設）；21 個同名巨集全部移除；`*_WALL_MM` 改「共同起點 + 可個別覆寫」（先前「三邊吃同一常數」的建議收回）；`DISABLE_POS_ERROR_LIMIT_DEG` 0 處讀（補「後果」不接上）；5 個「設定了沒效果」的 setting 刻意不動（功能改變）。225 常數分類：**12 個安全互鎖不外部化**；機構標定外部化必帶 provenance。
7. (08-30) 階段 2 指令層抽出（`main.cpp` 522→149）；階段 3 `RopeAxis`（`meter_loop` 順序必須左→右）；階段 4 `profile.h`（沒設定檔時逐位元不變）；階段 5 `WASH_ROBOT.cpp` 拆兩 TU。**先上機不再往下重構**（9 條沒有實機證據）。harness 預期差異保護 1/9→**6/9**：④ `CUP_PULSE_PER_CM` 天生難測（seal 深層邊界）；⑤⑧ 錯誤路徑 harness 結構上測不到→靠 `fake_slaves/`（兩套一起才算完整）。`check_so_error.sh` 差異只在 log；`run_trace.sh` 硬設 `FCV_EP_*` 曾靜默蓋掉外部值。假 IMU 真因是 3 秒視窗不是時機；`normalize_replies.py` 遮 `n_angle/n_accel`。
8. (08-30) 抓到真缺陷：`cmd_arm_sweep` 沒重置 `abort_flag`（與 `28dfa30` 同族；`try_or_pause_` 靜默中止值得加診斷）；建置指令缺 `-Imechanism`（只加進 `harness/build.sh` 忘了 runbook）；`CLAUDE.md` 樹狀圖連違三次「新增檔案必須加一列」。

**⚠ 踩坑 / 教訓**
1. **稽核工具的輸出是待查清單不是結論**——一天應驗三次（`!client\b`、hex regex 空對空、對齊空格數錯 4 vs 21）。
2. **零筆資料的相等不是通過**（`test_qx_do24` 斷言寫反、`normalize` 空對空、`rail.txt` DM2J 0 筆綠燈）——`compare.sh` 強制列出覆蓋範圍。
3. 量測儀器必須兩側一致、收尾必須確定性（非原子 `LOG_HEX`、`^` 錨定丟行、無界重試讓誰多活 25 秒看起來像行為差異）。
4. 「寫下來不等於做到」——同一天三次只驗到一端（新增 `common/`、`-Imechanism`、`CLAUDE.md`），擋住的是每次真跑一遍。
5. 「應該不影響」與「證明不影響」差一個負控制；`grep -c` 多檔輸出格式＋`|| echo 0` 多印一個 0。
6. `--help` 會直接啟動程式連上硬體（08-28）；`echo "(無輸出＝通過)"` 是永遠成立的斷言；`false=成功` 慣例在自己的測試程式又踩一次。

### 歷史摘要 #14（2026-08-27 ~ 2026-08-28）— 16 支 driver 回覆驗證稽核；上滑台每 cm 走 7.7 倍；吸盤左右歸屬；PWM 20% 無回應；CRLF 誤判成分岔；上機準備

> **規範權威：** `.claude/changelog.md` `[2026-08-28a]`~`[2026-08-28u]`＋`-drv1~5`；`Linux_test/fake_slaves/fake_rtu.py`（46 情境）；`runbook.md` A2 上機檢查表＋§建置；`CLAUDE.md ## Architecture`（08-28 由原始碼重建）＋兩張索引表（`.claude/` 13 項、根目錄 8 項）；`.claude/summaries/`（8 份＝手冊本身）；`probe_dm2j.cpp`（`~/bringup/`，未進版控）。

**決策 / 伏筆**
1. (08-28) driver 回覆驗證稽核 16 支結案（mailbox 05-14 擱置 3.5 月）：9 支修補、記憶體覆寫類別關閉（SD76／DSZL／DY-500 壞幀 SIGSEGV/SIGBUS）。**三個刻意不動**：PQW 寫入 echo 不驗（曾把機器卡在序列中間無可恢復）；ZDT `readEcho()` 不檢 slave id（廣播）；DM2J `sendRecv()` 不檢、`recv_frame_()` 要檢（同 bus 競爭）。行為改變風險：ZDT 在步態迴圈、PQW「CH6 verify fail 三次」、SD76 `meter_left_read_fail` 都會更常出現＝變可見不是新故障。
2. (08-28) **上滑台每個 cm 指令走 7.731 倍**（皮帶軸 7.731 cm/rev，程式假設 1；截距 −0.6 cm 皮帶鬆弛）——三個沉默疊加（驅動器只數脈衝／`do_arm_sweep_` 成功路徑不印／滑台三天掛錯 gateway）。修法：driver `set_lead_cm_per_rev` + `set_travel_limit_cm(0,48)`，常數數值刻意不逐一乘。**RPM 全在錯誤認知下挑的**（250 rpm 實為 32.2 cm/s、1000 rpm 128.8 cm/s），使用者實測失步；`ACC/DEC` 同（→ 後於 08-31 per user 否決 RPM 重評）。
3. (08-28) **吸盤左右歸屬**：per user 實體排列右={5 上,7 下}、左={6 上,8 下}；原 `RF={5,6}` 讓「錨定側是否還吸著」看的是一邊各一顆＝等於沒有保護；改四個常數 31 處自動正確。**推桿 pulse/cm 差 5%——08-27 的「更正」3000→2857 本身是錯的**，實測 47994 脈衝=16 cm。
4. (08-28) PWM（QX-DO24 slave 9，**左右螺旋槳共用 CH1**，5%=停 10%=全速）：寫入 20% `no reply`（間歇、環境相關，非穩定）；**寫入失敗模組保持前一值繼續輸出＝通訊斷掉螺旋槳不停**；交易層重試 3 次+40 ms（救援路徑未被觸發）、回讀驗證、錯誤訊息分辨真因。`ch3=11%`／`ch4=50/1000` 是模組端殘留（per user 只用 CH1，08-31 結案）。`.22` 實體層（終端電阻／線長／接地）待查（→09-03 QX 移 `.21`）。
5. (08-28) 七個隱形缺陷：`sendAndReceiveQuiet` 繞過 `MSG_NOSIGNAL`（合併乾淨≠語意接得上）；`PR_move_cm_nowait` 寫死 `return false` 讓 main 的掃動全滅偵測接到常數（三個 `void`→`bool`）；重連缺 `getsockopt(SO_ERROR)`（吊機沒開印 20 次 `reconnect success`）；`init()` VFD 型號寫死 MH300；`zdt_pusher` 範圍無交集自 08-27 不可能成功。`init()` 檢查表兩台跑完四件未驗改動通過；`presence not probed` 證明不了模組接上。
6. (08-28) 部署：`~/bringup/` 刻意與 `~/projects/`（VS 遠端建置落點、對方會重建覆蓋）分開；部署路徑五處全錯已修（`bin/ARM64/Debug/*.out`、`web_ver2`）；兩台無 `tmux`/`screen`→FIFO 背景啟動；`exit` 只對兩支 C++ 有效，`motor_api`/`node` 要 TERM。`server.js` `CRANE_IP` 預設 `.101` 是值寫錯（要修）；`WROBOT_IP .1.100` 是正確組態只是線還沒接（**per user 08-28：兩台刻意還沒 eth 串接，不要修**）。`CLAUDE.md` Build System 寫的 `washrobot_new_PI.sln` 不存在；`Platform=ARM` 應為 ARM64。
7. (08-28) **CRLF 誤判成「Pi 上 web_ver2 分岔 589 行」**——實際只落後一個 commit；伏筆：從 WSL `scp` 不會混 CRLF，經 Windows 中轉才會；比對 Pi 與 repo 先 `sed 's/\r$//'`。`web_ver2` 在吊機不在本體。刪四個誤導檔（228 KB，判準「看了會被誤導」）。`#if 0` 死碼引用的 `ZDT_LB1/RB1/C` 已不存在＝無復原價值。**CH6 破真空／CH14 水泵是安全關鍵一對**（水泵指錯＝貼牆時四顆失壓）。
8. (08-27) 吊機唯讀盤查：有線 `.1.10`（文件 `.101` 錯）、帳號 `nexuni`/`user`；四項發現進待辦（部署分岔／張力刻度／張力差／故障碼）；吊機二進位 7/23 版比 repo 舊。

**⚠ 踩坑 / 教訓**
1. **「單位」也要驗**——拿尺量一次勝過一百行「驗證通過」；零點是硬限位的系統，回零位置對失步沒有鑑別力；漂亮的數字（8.0 cm/rev）最容易讓人停止量測。
2. **「更正」也是一種主張**（2857）；「有在檢查」≠「檢查得到」（常數 `return false`）。
3. `diff` 回報整檔每行都不同時先問「為什麼是全部」（CRLF）；一次觀察就下結論（SD76 CRC 誤報是探測程式沒排空緩衝）。
4. `pkill -f` 比中執行它的 SSH 指令自己——判斷程式在不在用 `ss -ltn`／`ps -eo comm`。
5. 邊界類測試值必須落在窗口內（`bc=255` 落窗外回 FAIL 差點判無缺陷，`bc=100` 才 SIGBUS）；CRC 只證訊息沒壞不證是給我的；測試用錯 `init()` overload 五個情境全假通過。
6. `TCP server :5001 fail` 真因是舊實例還握著 socket，不是 `SO_REUSEADDR`；`status` 顯示的壓力值看不出是新鮮值還是 timeout 後的快取。

### 歷史摘要 #15（2026-07-22 ~ 2026-08-17）— depth camera 窗框辨識除錯（距離優先）；`TCP_client` 假連線修；M2 重裝校正手動流程

> **規範權威：** `.claude/changelog.md` 2026-07-21e ~ 2026-07-23f（depth cam）／2026-08-14a~c（M2 `SET_HALF_RANGE`、`lr_calibrated`、`lr_half_range=0.7275`）；`cleaning_arm/main_api.h:225-251`、`main_api.cpp:1992-2028`。⚠️ 07-22~23 這段與既有摘要 #1（07-07~07-23）期間重疊，內容為 #1 未收的除錯細節；depth camera 路線已於 2026-09-01 連根拔除，本段只留「當時的決策與未驗項」供追溯。

**決策 / 伏筆**
1. (07-22~23) 鏡面反光場景（刻意）下逐步追出：service 沒被 `wr.sh` 啟動→`protrusion std` 爆炸（bbox 漏距離門檻）→背景平面把木板拉平（兩階段重擬合仍常無背景點）→**per user 設計轉向：距離為主、凸出量次要**，平面擬合失敗不再等於偵測失敗→只留最寬候選→`min_distance_cm` 用候選自己的距離→`remaining_travel_cm` 高估 10 cm 改用 `center_distance_m`→安裝幾何常數本身錯：`DEPTH_CAM_LEAD_OFFSET_CM` 16→32、`STANDOFF` 50→56（per user 重量）。
2. (07-22~23) 跨障礙步幅建議 `remaining + max_height + 20 + 5`（per user 公式，只改預設值，07-20「使用者每次自己決定」不變）；Camera 頁「拍照」按鈕。**未驗**：新常數後未實機重測、步幅公式未實跑、非鏡面場景未測、三支 `frame_capture/*.py` untracked、全部改動未編譯。
3. (07-22~23) 意外撈到全專案 bug：Linux `available()` 用 `ioctl FIONREAD` 偵測不到對方正常關閉、失敗不設 `connected=false` ⇒ 永不重連（改 `recv(MSG_PEEK)`）——影響所有 `TCP_client` 裝置（**伏筆：09-01 查到的失步真根因就是這個 `available()` 跨執行緒改模式**）。
4. (08-17) **M2 重裝後校正靠人記得**：M1 每次 INIT 無條件重校；M2 `lr_half_range=0.7275` 寫死、`lr_calibrated` 預設 `true`，只有 |pos|>1.5 rad 才強制歸零 ⇒ 重裝後落在 ±1.5 rad 內會**靜默移到錯的 CENTER**。手動流程：DISABLE→手轉正中→`MIT 0 0 0 0 0` 刷新快取→`ZERO`→兩側極限各量→取**較小值** `SET_HALF_RANGE`。`LR_CALIBRATE` 自動尋邊仍不可靠（假觸發撞牆／衝很遠撞不到）。

**⚠ 踩坑 / 教訓**
1. (07-22) 「完全偵測不到」第一層原因是服務根本沒起——先確認行程再查演算法。
2. (07-23) `near_m` 不保證在畫面中央，偏心像素被公式當成往前距離；算出來的距離要拿皮尺對。
3. (08-17) `DISABLE` 不像 `ENABLE` 會補送 MIT frame，`STATUS` 讀到的是轉動前的舊值。

---

### ✅ 壓縮保全清單 —— 2026-09-12 已核對清空

> 81 條逐條比對待辦總表完畢:**20 條已被總表涵蓋或已結案**、**2 條前提已消失作廢**、
> 其餘**併入總表**的「🟡 2026-09-12 由歷史壓縮併入」一節(本檔待辦總表內)。本清單任務結束。

---

### 歷史摘要 #1（2026-07-07 ~ 2026-07-23）— v2 應用層重寫 / crane 三起實機事故 / depth camera 上線

> **規範權威：** `.claude/changelog.md` 2026-07-15b~f、2026-07-21、2026-07-21b、2026-07-22e、2026-07-23；`.claude/motion_flow.md` §4b「同步步伐」；`.claude/reference/v2_app_redesign_plan.md`（應用層重寫）＋ `.claude/archive/mh300_migration_plan.md`（吊機變頻器）；memory `project_v2_mechanical_gait` / `project_new_crane_vfd_mh300`。

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
- 多人協作機制（角色表 ／ `user_lib/*.h` public API 為介面契約、跨界 PR 標 `[跨界: user_lib]` ／ `.claude/archive/mailbox.md` 協作信箱 ／ 開 session 三步驟）—— ⚠ **此機制已於 2026-08-27 整個退休**（改單人開發，mailbox 改為墓碑），歷史條目裡的「等 Jim review」「屬 Sadie 範圍」等分工字樣一律作廢。

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

> **規範權威：** `.claude/motion_flow.md`（初版：Phase 1~5、硬體對照、可調參數表）；`CLAUDE.md`（架構圖、分散式系統通訊章節）；`.claude/summaries/` 下各驅動摘要（ZDT / DM2J / JC-100 / DY500 / SD76 / PQW / ZS_DIO）；`deploy_and_test.pdf` Gate 0~6 ＋ 產生器 `.claude/archive/gen_deploy_pdf.py`。

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
