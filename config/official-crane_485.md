# official-crane — 裝置位址 / 485 配置(2026-09-18 全部實測完成)

> 正式吊機 Pi(`official-crane`,Pi5)。**eth0 靜態 `192.168.1.10/24`、gw `192.168.1.1`**(裝置專網);
> wlan0 目前 DOWN(09-18 重開後);GUI 走 `http://192.168.1.10:8080`。
> 🔴 **佈線與測試機 `raspberry-cran` 不同**,但**程式共用同一份原始碼** —— 差異全部由
> `/etc/systemd/system/fcv-crane.service.d/endpoints.conf` 的 env 決定(見第四節)。

## 一、網關 → 裝置分組

| 網關 IP:Port | 掛的裝置 | 序列設定 |
|---|---|---|
| `192.168.1.30:4001` | SE3 變頻 **左繩** | 115200 **8N2** |
| `192.168.1.31:4001` | SE3 變頻 **右繩** | 115200 8N2 |
| `192.168.1.32:4001` | SD76 計米 **中** + **右** + **MH300**(中繩絞盤) | 115200 **8N1** |
| `192.168.1.33:502`  | **X518 張力**(DSZL-107,原生 Modbus-TCP,不經網關) | — |
| `192.168.1.34:4001` | SD76 計米 **左** | 115200 8N1 |
| `192.168.1.35:4001` | **ZS-DIO 繼電器**(進水球閥) | **9600** 8N1 |

> ⚠️ **SD76 與 ZS-DIO 不可同一條 RS485**(共線時 SD76 回垃圾位元組或整個消失,程式註解記過三次)。
> 這就是 ZS-DIO 從 `.34` 獨立到 `.35` 的原因。

## 二、逐裝置(全部 2026-09-18 實測在線)

| # | 裝置 / 功能 | 網關 | Slave | Baud | 格式 | 狀態 |
|---|---|---|---|---|---|---|
| 1 | SE3 變頻 左繩 | `.30:4001` | 1 (P.36) | 115200 (P.32=5) | 8N2 (P.154=3) | 參數已從測試機 clone |
| 2 | SE3 變頻 右繩 | `.31:4001` | 1 | 115200 | 8N2 | 同上 |
| 3 | SD76 計米 **左** | `.34:4001` | **1** | 115200 | 8N1 (UArt=1) | SCAL=000200 |
| 4 | SD76 計米 **右** | `.32:4001` | **2** | 115200 | 8N1 | SCAL=000200 |
| 5 | SD76 計米 **中** | `.32:4001` | **1** | 115200 | 8N1 | official 有裝(測試機未裝) |
| 6 | X518 張力 (CH1=右/CH2=左) | `.33:502` | 1 | — | 原生 TCP | 已兩點校正 |
| 7 | **ZS-DIO 水閥(CH1)** | `.35:4001` | 1 | **9600** | 8N1 | CH1=進水球閥 |
| 8 | MH300 中繩絞盤 | `.32:4001` | 待測 | 待測 | 待測 | 程式仍寫 CLV900 slave3 @ USR_A,與此佈線不符 |

### SE3 通訊參數(兩台相同)
`P.33=0` Modbus · `P.36=1` 站號 · **`P.32=5`→115200** · **`P.154=3`→8N2** · `P.51=1` CR ·
**`P.79=3` 通訊模式(CU)** · `P.35=0` 指令來源=通訊 · `P.52=20`/`P.53=20` 逾時 · `P.153=0` 逾時報警停車(需 keepalive)。
🔴 **改序列參數(P.32/P.154/P.33)後 SE3 必須斷電重開才套用**(否則先亂碼、再完全靜音)。
🔴 改 `P.79` 不要直接跳:`3 → 2(外部) → 目標值`;`P.5` 保持 0。

### SD76 計米器
Baud **115200**(手冊寫 9600/4800,但這批實機跑 115200)· 格式 **8N1**(UArt=1,**無 8N2 選項**)·
面板項 `Adrset`/`BAUSET`/`UArt` · FC03/06/10 · **SCAL(0x0014-15)有效值 = `000200`**(低 3 bytes;
開頭 byte 是各錶狀態旗標、讀取時捨棄)· **改站號要斷電重開**才生效。

### X518 張力(DSZL-107)
- **IP 存在參數暫存器**:`IPH 0x063E = a*1000+b`、`IPL 0x0640 = c*1000+d`;port `0x0642`、模式 `0x0644`(1=Modbus-TCP)、站號 `0x064C`。
  只吃 **FC03 讀 / FC16 寫**(每參數 32 位 = 2 暫存器),寫完 **`0x0A20=40` 存 flash**,**重上電生效**。
  (2026-09-18 就是這樣把它從 `.32` 搬到 `.33`:IPL 1032→1033。)
- **只允許 1 條 TCP 連線** —— `fcv-crane` 佔著時外部再連會被拒或讀到半截。
- 🔴 **official 這顆的方向與測試機相反**(施力時 raw **上升**)⇒ scale 是**正值**:
  **左 `0.0075758` / 右 `0.008547`**(測試機是負值)。校法:空載 `zero_tension all` → 掛 2kg → `set_dsz_scale`。
  校後左 1.99 / 右 1.97。**靠 drop-in env 持久化**(`set_dsz_scale` 本身不持久)。

### ZS-DIO 繼電器
站號 `0x0032`、**鮑率 `0x0033`**(0=4800 1=9600 2=14400 3=19200 4=38400 5=56000 6=57600 **7=115200**)、校驗 `0x003D`;
FC01 讀線圈 / FC06 寫;**改完斷電保存、需重上電生效**。
🔴 **這顆 baud 被人改成 9600**(出廠 38400),115200/38400 都試過全靜音,最後靠「逐一改網關 baud + 重啟 + 探測」掃出來。
**進水球閥在 CH1**(測試機是 CH4)。

## 三、USR 網關改設定的正確方式(踩過坑)

網頁表單是 `GET → port.cgi`,**必須帶齊所有欄位**,而且要接 **`manage.cgi?reset=1` 重啟**才落地:

```
http://<gw>/port.cgi?port=0&br=<baud>&bc=8&parity=0&stop=<1|2>&tlp=4001&trp=8234&tnm=1
   &cmode=0&cnum=4&umode=0&shortc=0&shortct=3&htpch=0&htpot=10&htpcoh=0&rip=192.168.0.201&htphead=&htpurl=
http://<gw>/manage.cgi?reset=1
```
少帶欄位 → 回 **200 但值沒變**(會被騙)。帳密 admin/admin。現值可從 `port.shtml` 的 `var _br/sb/bc/par/_tlp/cmode/_cnum` 讀。

## 四、程式怎麼吃這份配置(一顆 binary 兩台共用)

`main.cpp` 的裝置對應已 **config-driven**,**預設值 = 測試機**(不設任何 env 時行為與改動前完全相同)。
official 靠這份 drop-in(`/etc/systemd/system/fcv-crane.service.d/endpoints.conf`):

```ini
[Service]
Environment=FCV_EP_USR_A_HOST=192.168.1.30      # SE3 左
Environment=FCV_EP_USR_B_HOST=192.168.1.31      # SE3 右
Environment=FCV_EP_DSZL_L_HOST=192.168.1.33     # X518
Environment=FCV_EP_USR_M_HOST=192.168.1.34      # 計米器 gw1
Environment=FCV_EP_USR_M2_HOST=192.168.1.32     # 計米器 gw2(新增;沒設就不連)
Environment=FCV_EP_USR_W_HOST=192.168.1.35      # ZS-DIO
Environment=FCV_METER_LEFT_GW=M                 # 左 → gw1(.34)
Environment=FCV_METER_LEFT_SLAVE=1
Environment=FCV_METER_RIGHT_GW=M2               # 右 → gw2(.32)
Environment=FCV_METER_RIGHT_SLAVE=2
Environment=FCV_METER_MIDDLE_GW=M2              # 中 → gw2(.32)
Environment=FCV_METER_MIDDLE_SLAVE=1
Environment=FCV_METER_MIDDLE_ENABLE=1           # 測試機未裝 → 預設 0
Environment=FCV_WATER_INLET_CH=1                # 進水球閥通道(預設 4)
Environment=FCV_DSZL_SCALE_LEFT=0.0075758       # X518 校正值(正值,方向與測試機相反)
Environment=FCV_DSZL_SCALE_RIGHT=0.008547
```

> **維持「一條匯流排一個 client」**(`cli_M` / `cli_M2`),不給每顆錶各開連線 ——
> 避開 USR 透明網關「對所有連線廣播」造成的 frame 污染(舊雷)。
> GUI 也已 config-driven:水閥卡片從後端 `water_status` 的 `wch<N>=water_inlet` 動態判定通道,不再寫死 CH4。

## 五、服務

| 服務 | 說明 |
|---|---|
| `fcv-crane` | 吊機主程式,TCP `:5002`;**enabled(開機自啟)** |
| `fcv-web-v3` | GUI `:8080`(`http://192.168.1.10:8080`);**enabled**;drop-in `fcv-web-v3.service.d/endpoints.conf` → `WROBOT_IP=192.168.1.100`(unit 檔仍是 bench 的 .26) |
| 本體 `fcv-body`(user service @ washrobot) | drop-in `~/.config/systemd/user/fcv-body.service.d/endpoints.conf` → `FCV_EP_CRANE_HOST=192.168.1.10`(unit 檔寫死 bench WiFi `.25`,有 env 覆蓋時**不做有線探測**,所以必須覆蓋) |

unit 由 `scripts/systemd/` 改寫:路徑 `/home/user` → `/home/nexuni`、`User=nexuni`。
三個 drop-in + `wait_devices.sh` 的副本在 **`scripts/systemd/official/`**(權威版在機器上)。

🔴 **開機競態**:Pi 比 PoE 交換器/USR 網關早 ~40 s 起來,fcv-crane 若先啟動會把所有裝置標成 skipped
(旗標只在 init 設一次)→ `ExecStartPre=/home/nexuni/run/wait_devices.sh` 等 `.30`/`.34` 的 :4001,
上限 180 s。腳本必須是 **bash**(`/dev/tcp` 是 bash 專有;`#!/bin/sh` 會白等滿 180 s)。

## 六、待完成

- 🟡 **兩台 Pi 都沒有 NTP**(裝置專網不出公網):時鐘各漂十幾小時,log 時間對不上。可讓主路由/吊機 Pi 當 NTP 源。

- 🟡 **MH300 中繩絞盤**(`.32`)站號/baud 未測;程式仍寫 CLV900 slave 3 @ USR_A,與 official 佈線不符。
- ✅ **本體↔吊機 WiFi 橋接已通(2026-09-19,重開機驗證過)**:見第七節。
- ✅ 本體 `washrobot` `192.168.1.100`(eth0 靜態)已從吊機 ping 通、本體→吊機 `:5002` 可連(2026-09-19)。
- 🟡 跑任務前需**地面歸零**;`wall_height` 未設(goto 會被擋,安全)。(09-19 戶外實測已跑過,狀態以現場為準)
- 🟡 `hold_guard` 曾被關過,操作前確認開啟。

## 七、本體↔吊機 WiFi 橋(QWRT,2026-09-19 完成)

| 項目 | 值 |
|---|---|
| 機型 / 韌體 | Q-WRT 25.06,MT7628(`ra0`=AP、`apcli0`=client),kernel 4.4 |
| 管理 IP | **`192.168.1.250`**(主)+ `192.168.100.1`(fallback alias `lanfb`);root/password |
| 上連 | `apcli0` → `facade_cleaning_2.4G`(主路由 IMSG2F4T-W `192.168.1.1`,開放無加密) |
| 模式 | **L2 橋接**:`apcli0` 進 `br-lan`,靠 MediaTek 驅動內建 **MAT(MAC 轉譯)** 讓有線端裝置穿過 3-address client 連線 |
| DHCP | QWRT 的 DHCP **關閉**(`dhcp.lan.ignore=1`),LAN 端裝置直接向主路由租 `192.168.1.x` |
| 交換器 | **5 個實體口全部 LAN**(`switch_vlan[0].ports='0 1 2 3 4 6t'`,WAN VLAN 刪除、`wan` proto=none),本體插哪個口都行 |
| 本體 | `washrobot` `192.168.1.100`(eth0 靜態),SSH `nexuni@`;吊機→本體 RTT 7–50 ms、偶爾飆到 ~1 s(2.4G,要留意) |

⚠️ `swconfig … link:` 這台回報的 link 狀態**不可信**(本體明明 ping 得到 0.5 ms,卻只顯示筆電那口 up)。

🔴 **踩坑**:`wireless.sta.network='lan'` 在這個 MTK build **不會**把 `apcli0` 加進 `br-lan`(`brctl show` 只有 ra0/eth0.1),
所以做了兩層保險:`/etc/hotplug.d/net/50-apcli-bridge`(apcli0 出現時 addif)+ `/etc/rc.local`(有上限 60 s 的等待迴圈)。
重開機 log 兩條都會出現 `apcli-bridge: apcli0 added to br-lan`。

驗證方法:筆電插 QWRT LAN 口拔插網路線 → 拿到主路由發的 `192.168.1.x`(實測 `.11`)且能 SSH `192.168.1.10` ⇒ 橋接成立。
AP 自己 ping 吊機 RTT 20–80 ms(2.4G)。

09-18 的「3-address 帶不動」判斷是**沒實測就下的結論**——驅動有 MAT 就能過,別再繞去 WDS/relayd。
