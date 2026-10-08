# Work Log

## 🔴 待辦總表(單一權威;**2026-09-23 清帳重建**)

> 📌 **這張表是本專案唯一的彙整待辦清單,只列仍開放的。** 舊表(2026-08-27 起、168 列 + 附錄)**原封歸檔**在
> `.claude/archive/todo-table-2026-08-27_to_09-23.md`;下方「舊#N」指該檔表格第 N 列,證據與量測值在那裡。
> 清帳方法:逐列對**現行原始碼**(`3e60c7d` + 09-23 未 commit 改動)與 09-10 之後的日誌判定,不靠記憶。
> 分組依「**要什麼才能動**」:A 離線程式 / B 要上機 / C 要你拍板 / D 暫停。🔴 = 安全或會靜默失效。
> 🆕 **9/19 official 戶外實測報告**:草稿 `.claude/reports/2026-09-19_official_field_test.md`;正式版 v1(PDF+HTML)在共用雲端硬碟 `給老闆/20260919_正式吊機戶外實測報告.*`。

### A. 離線可做(程式/文件)
| | 項目 | 出處 |
|---|---|---|
| 🟡 | **`length_diff_max_cm` 餘裕**(10-07 查明「13~16 cm 不觸發」**不是 bug**:比的是本次動作相對位移差,10-01 26 趟最大 9 cm):10-06 起下行可到 50 Hz、實測已 9/10 + 計米器抖動 ⇒ 可能誤停。待拍板:門檻 → 12~13 或下行維持 30 Hz;零風險可先做 `length_diff_peak` 印出 | 10-01、10-07 |

### B. 要上機
| | 項目 | 出處 |
|---|---|---|
| 🟡 | **部署 `[2026-10-08c]` 中繩放繩多放 % + 連動方式**(離線完成、harness 287 全過):crane → official、GUI → 兩台(bump 版號);實測 ① 多放 10 % 放繩時中繩 Hz ×1.1、收繩不加 ② 拔中計米器 → 長度跟隨擋鋼索 + 紅字、改只照速度後 ▲▼ 照常 ③ 兩項重啟後還在 | 10-08 |
| 🔴 | **抗風 / 低樓層貼牆的現場量測**(9/19 實測兩大問題的前置):① 吊點離牆水平距離 d、吸盤面到掛點深度 ② 測試牆面朝向 ③ 風扇全速推力 ④ 拉 9/19 log:橫移/失敗時的 roll、張力、風扇占空比、每次吸附的 `p5..p8`(吸到哪幾顆、是否同側、高度)⇒ 算「各高度 × 風速」可作業範圍(⚪ 10-01 per user:**9/19 報告 v2 不做**,DJI 空拍不再補看;報告維持 v1) | 報告 3.3/3.5 |
| 🔴 | **official 收尾**(🆕 **10-08 吊機半邊已部署最新 main**:crane binary `9f50192f`、新 `fcv-crane.service`(xargs 回放、StartLimit 在 [Unit])+ `crcmd.py`、GUI `v3-2026.10.08-1957` + `server.js`(+ `cam.conf` `CAM_BASE`)、`cycle_test.py`;啟動全 `[OK]`、`pay_out_max_hz=50`;本體同一台已是最新):剩 🔴 **本體切 official**(鋼索還在測試吊機,per user 先不改;切時刪 `crane-ip.conf` + `endpoints.conf.official-disabled` → `endpoints.conf`)、**手臂部署**(裝回後 `start fcv-arm` + `arm_attached on` + `deploy.sh arm`)、抽驗:hold 租約(新 GUI 續約)、`body_link_age_ms` ≤300、`set_setting` 重啟後還在、`crane_idle_ms`、計米器標紅、goto 軟停文字、`ntp_probe.sh` | 09-21/23/30、10-08 |
| 🟡 | **WiFi 橋 RF**:9/19 實測全程連線正常、IMU 平衡經 WiFi 表現良好;但 RSSI −70、Tx PER 83% 仍偏弱(前一晚曾斷 285 s)。主路由改 20 MHz、拉近/對準;長期 5 GHz 或有線 | 09-19、報告 2.2 |
| 🔴 | **`.22` 匯流排實體層沒修**(終端電阻/線長/接地未查);風扇開 15× 錯誤;official 也偶發 JC100:5 TIMEOUT;**JC-100 slave 8 換表頭**(單日 67 錯)。👁 10-01 觀察:風扇不轉時近乎乾淨;風扇轉時 `.20`/`.21`/`.22` **三條同時**出錯;**對照測試**:停 0 錯 / 7% ~7 / 8% ~15 每 30 s(劑量相關、兩輪重現),8% 時繼電器回讀約半數失敗 ⇒ 見 10-01 日誌;**10-02 重測**:停 0~3 / 7% 20~21 / 8% 12~14 ⇒ 轉就錯第三次重現,但劑量關係沒重現(見 10-02) | 舊#26/#27、09-15、10-01 |
| 🔴 | **official 計米器捲尺校正**(9/19 實測確認左右需重校;同指令左 +8 cm 時右 +18 cm)、中錶 DP=0→2(面板)、漂移累積 | 09-19、報告 3.4 |
| 🟡 | MH300 中繩:official `.32` 站號/baud 未測;程式仍寫 CLV900 slave 3 @ USR_A;MH300 必驗清單(方向/電流 scale/run bit/fault code);`0x2002` 開機無條件清已寫、未上機 | 舊#21/#76 |
| 🟡 | official 兩台 Pi 無 NTP(先跑 `ntp_probe.sh`) | 09-21 |
| 🟡 | 真機驗 09-16 中止五步(`[web] STOP ⑤` + 四顆回大氣)。✅ 吸附中 `crane_goto` → `ERR cups_attached` **10-01 真機已驗**。⏸ 10-07 per user「暫時不修、保持待辦」:環境已備好(吊機 `fcv-web-v3` drop-in `no-arm.conf` = `FCV_NO_ARM=1`、本體 `fcv-arm` 停 + `arm_attached off`);**卡在起點須在頂端**(當時計米器 4/3 = 底部)。測法:機器頂端貼玻璃 → Mission 1/1/5 cm、fan `move:5`、rail off → 推桿伸出時按中止(無手臂時吸住只有幾秒)→ 看 ①②③(④)⑤ 順序、⑤ 在 proc 結束後、p5..p8 回大氣、橫幅「停止中 → 已脫離牆面」、吊機沒動 | 09-16、10-01、10-07 |
| 🔴 | **手臂 M2 滾筒位(RIGHT)到不了**(10-08 M2 修好後):停 0.797→0.790→0.783、目標 0.8558、頂住 tau≈3.4 Nm,LEFT 0.2 正常;user 手轉後致能 M2 在 **1.6161**(未說明是什麼位置)⇒ 下次:失能 + `M2 MIT 0 0 0 0 0` 刷新、逐位置重量(滾筒/刮刀/正向停點),判斷零點偏移或機構卡住,必要時更新 `M2_SLOT_*` | 10-08 |
| 🟡 | **手臂裝回後**(10-08 已裝回:`fcv-arm` 跑、`arm_attached on`、`dev_arm=1`;測試吊機 `no-arm.conf` 仍在):`deploy.sh arm`(確認 binary 是否最新)、刪 `no-arm.conf`;**繼電器 CH4 噴水泵 / CH5 滾刷**在新 ZS 驅動下開關回讀(10-01 沒測)。手臂:`DEPLOY_F` WARN 分支從未在硬體觸發、`theta_max=1.10` 無上界實測、四個樞軸→玻璃幾何常數未實體量、臂長 490 vs 編碼器尺度 1.53× 無法分辨(量角器量一次)、M1 passive 根因(08-17 起)、**M2 掃動被摩擦帶走 26~28°** | 舊#105/#106、附錄 手臂/滑台群 |
| 🟡 | `status` 的 `p_err=` 與 attach 的 `partial_seal=` 兩條「讓失敗看得見」路徑從沒被執行到(非缺陷,只是未觸發)。👁 10-01:`attach` 四顆全吸 → 未走到 `partial_seal`;風扇轉時 slave 8 讀不到 → `p_err=` 應會出現,下次跑風扇時抓一次 status 即可驗 | 舊#69、10-01 |
| 🟢 | SE3 `readFaultCode`:**10-01 測試機重驗** `vfd_fault left/right` ×3 → 成功 4/6,失敗是 `0x1008` 偶發 comm fail(不再是「連續」);兩側履歷 4 筆皆 `160/OPT`(推定為程式重啟時 keepalive 中斷,當天重啟多次)。剩:偶發失敗要不要加重試、OPT 是否只在重啟時累加 | 舊#100/#141、10-01 |
| 🟢 | 右腳推桿阻力 1.4~1.6× 左腳(假說:右側離牆較近);四顆推桿接觸力不均 2.1×。👁 10-01 小樣本(每支 4 次、400 rpm、頂端)**未重現**,左略高 —— 要結論需整趟多步(下次跑 full 時統計 `wait_many` 行) | 舊#25/#107、10-01 |

### C. 要你拍板(09-23 已逐項對碼;1/2/3/4/6/9 照建議維持現狀,不再列)
| | 項目 | 說明 |
|---|---|---|
| 🔴 | **抗風對策方向**:9/19 平均 7 m/s、陣風 16 m/s 下,拉上 2–3 層就左右橫移、風扇壓上也壓不住;1–3 樓吸不上或只吸 1–2 顆。分析(報告 3.3):主因推定為**長繩回復剛性低(W/L)+ 風**,受風面三個:機器、6 mm 鋼索、**黃色臍帶**(影片中被吹成大弧)。現場試過**地面導引繩:效果不大**,且越高側向分量越小、高樓層拉不到(user 09-23)。候選:頂樓端側向約束/導軌、作業風速上限、臍帶收束、加大貼牆推力。**先等 B 組量測** | 報告 3.1b/3.3 |
| 🔴 | **清潔部件 × 垂直窗框**:刀具垂直 + 滑台橫掃,遇垂直直料會卡住(程式只處理水平橫桿跳過帶)。方向 A 行程限單一分格 / B 遇直料抬起跨越 / C 刀具水平靠吊機上下刮 / D 刀具可轉向。**先收集目標建築分格寬、直料間距與凸出深度** | 報告 3.3b |

### D. ⏸ 暫停 / 長期(有意擱置,別當成漏掉)
- **隧道(Fathom-X)**:VFD 運轉掉 90% 封包的電氣問題、`IMU_ROLL_STALE_MS=750` 對隧道太緊、隧道閒置 1% 丟包 —— per user 09-21「暫時用 WiFi 之後會再改」,**切回隧道前要先讀舊#1/#2**。
- ZS-DIO 通訊斷線全關(保持暫存器 `0x0030`,N×0.1 s 後全關):per user 09-23 **先不做**(吊機端 300 s deadman 已有)。
- **重心偏左**:per user 09-09 定調「捲筒本來就這樣,計米器 + IMU 修正就是為了補償它」⇒ 不配重/不改吊點;門檻 09-17 已併成單側 `retract_tension_stop_kg`。
- v3 新架構硬體設計 27 項(**與 C 組「清潔部件 × 垂直窗框」相關**:最小玻璃分割、吸盤中心距、刮刀延伸、ESC 電壓版本、RCD、螺旋槳防護、LRS 容量、滑台極限開關…)—— 原列見歸檔檔舊#142~#168;⏸ 項 per user 09-01 暫緩。
- `package-lock.json` 從 Pi 的 node_modules 產並進版控;牆距剖面 GUI(θ→mm);跨障礙步幅公式實證;一般窗戶窗框辨識(相機路線已移除,僅留紀錄)。

### 清帳對照(2026-09-23)
- **已解(對原始碼確認)**:本體 watchdog 死碼(09-10 補回,`WASH_ROBOT.cpp:2885`)舊#4/`crane_cmd_` 行緩衝(`crane_rx_buf_`)舊#5/overrun pass 套 `level_diff` 舊#12/
  `level_diff` 程序(09-15 `level_auto` 取代)舊#10/MH300 `0x2002` 開機清(已寫)舊#21 程式部分/balance 預設 `Imu`(`main.cpp:995`;official 看到 `meter` 是 GUI 手動切的)舊#32/#36/
  **兩個死碼函式**(09-16 `44bcb13` 已刪)舊#18/**幫浦 B 組**(09-10 A/B 輪替)舊#46/**DSZL 歸零自動存 flash**(`cmd_zero_tension`)舊#101/**`group_seal_ok_` 每側各 ≥1**(08-31 已改,`SEAL_MIN_CUPS_PER_SIDE=1`)舊#57/
  **手臂 INIT 停滾筒**(09-16)/**吊機端補水閥 deadman**(`g_water_open_max_ms` 300 s,09-10)/張力門檻持久化(09-17 `crane_settings.txt`)/
  DSZL scale 持久化(09-18 env)/Web GUI 開機自啟(09-16 systemd)/tmux 啟動腳本(systemd 取代)舊#63/`FCV_EP_CRANE_HOST`(official 用 drop-in)舊#22/
  `abort_flag`、SD76 CRC、`MSG_NOSIGNAL`、null-client、ZDT 廣播回傳等 08 月已修各列(舊表已標 ✅)。
- **作廢(前提消失)**:console v2 confirm 舊#23(v2 已退役)/Pi 上 `web_ver2` 舊#139/緊急收繩無張力保護舊#64(救援收繩 09-15 移除)/
  2026-07 批次未編譯舊#89(之後全部建過)/v1 `#if 0` 舊#92/06-02 實機觀察清單、scripted run/snowball 驗證舊#96/#97(v1 流程)/
  crane placeholder 常數舊#98(official 已逐台實測)/左右張力差 12.4 kg 舊#140(刻度已校)/follower alt 步態舊#88(GUI 已只走 sync)/
  Manual 手臂「靠上/離開」(09-17 手臂卡重做為壓上/收回取代)/`motion_flow.md` §2 v1 表(刻意凍結)/深度相機各列(09-01 整套移除)。
- **09-23 本次做掉**:風扇順序改回原版(`cycle_test.py`,未上機)。
- **09-30 A 組清完**(per user「其他的全部都做」,未上機):hold 租約 + `hold_renew`(舊#19)/吊機本體鏈路年齡 `body_link_age_ms` 只觀測(舊#3 選項 2)/hold 中擋 motion/pay_out 30 Hz 上限/本體閘道重連自動 selfcheck(舊#78)/rail 送出重試/ZDT `pulse<0`/`vacuum left|right` 拒絕/5 個死 setting 移除/設定自動持久化(本體 `settings.json`、吊機 `crane_settings.txt` +15 鍵)/本體 watchdog 閒置改看 IMU 回覆(舊#16)/include guard(舊#138)/過期註解/`set_imu_roll` log 節流/`QX_DO24::init` 對齊(舊#70)。
- **09-30 不做**:滑台橫走時平衡迴路不跑(舊#8)—— per user「吸盤有做動,還需要嗎」:滑台只在吸附後掃(`n_seal==0` 跳過),吸附中繩子不該被平衡拉 ⇒ 不是缺陷。
- **10-01 拍板**:ZDT 原生歸位 `trigger_home(0x02)` **不採用、結案**(per user)——讀回零參數(停本體,`FC04 0x0022`;FC03 回空、0x001C exception)四顆皆出廠預設:方向 0/CW(推定=伸出)、30 rpm、逾時 10 s、碰撞 300 rpm/800 mA/60 ms ⇒ 直接觸發會往牆伸、把牆當零點;要用須對 4 顆改寫參數且換件即丟。保留手刻 `zdt_home`(先探測方向、09-11 實證)。細節 `summaries/ZDT_MODBUS_SUMMARY.md` §3.3.5/3.3.6。
- **10-01 拍板**:張力方向遲滯(靜止 下停 −5 / 上停 +6 kg)**門檻不改**(per user)——遲滯相對餘裕很小(收繩軟停 75 vs 上升單側 ~50-55、單側上限 100 vs 傾斜時 ~65),且門檻管的是動態峰值;唯一放大處是單側傾斜時左右差(單側 5 cm 達 36 kg / 門檻 50)⇒ 另一個不做大幅單側移動的理由。
- **10-01 拍板**:單側過衝 +1 cm(四方向固定、與距離無關)**不補償**(per user A)——單側移動只用在姿態修正,修完會再量再修、過衝被下一輪吸收;日後要精準定位再改「提前停」(`side_measured` 停止點)。
- **09-30 拍板**:`DEPLOY_F` 工作力道 **3**(per user)→ 手臂 header `DEPLOY_F_TARGET_NM` 5→3,與腳本/GUI 一致(已知代價:09-15 量到掃動中 tau 掉到 1.2~1.9,若再現改調這裡)。
- **09-30 查證早已改好**:`WASH_ROBOT.h` 的 `.22 = arm-rail`(09-03 已重寫)、DSZL MBAP 規格段(`motion_flow.md` 已寫原生 Modbus TCP)。

---


## 2026-10-08:正式吊機上線(倉庫 WiFi)—— 唯讀初查

- 倉庫路由器 ARP:**`official-crane` = `192.168.5.15`**(WiFi S5,MAC `2C-CF-67-12-3F-47`);同網段機器人只有它與 `raspberry-cran` .31,**測試本體 .26 不在**。本機 `~/.ssh/config` claudeuser 清單加 `.15`。
- SSH:key 與預設金鑰都 `Permission denied`(official 從沒裝 key;09-19 用密碼,**密碼出處沒記、Claude 密碼簿也沒有**)⇒ 請 user `ssh-copy-id -i ~/.ssh/claudeuser.pub nexuni@192.168.5.15`。
- 不登入可看的(`status` :5002、GUI :8080):GUI **`v3-2026.09.18-2326`**、吊機 binary 為 09-19 版(無 `body_link_age_ms`/`pay_out_max_hz`)。
  設備:`dev_gw_a=1` 但 **`dev_vfd_left=0`**、**`dev_gw_b=0`/`dev_vfd_right=0`**、**`dev_gw_c=0`(X518)/張力 ERR**、計米 左=1(讀 0、`zeroed=0`)右=0 中=0、`dev_gw_m=1`、`dev_gw_w=1`;`imu_roll_age_ms=-1`(本體沒連)。設定:牆高 257、hold 10、`balance_imu_kp_ratio=0.2`。
  ⇒ 推定動力/設備未全上電或有線網段問題,待 user 確認;部署最新 main 仍是總表 B 🔴 大項(先給計畫)。
- ✅ user 裝好 key(`nexuni@192.168.5.15` 金鑰登入 OK)。user:「吊機箱有電、有整理過電箱 ⇒ 逐項檢查」。
  - Pi:eth0 `192.168.1.10` 1000M carrier=1、wlan0 `.5.15`;🔴 **時鐘停在 09-19**(開機 35 min,`date` = 2026-09-19 03:26)⇒ 無 NTP/RTC;🔴 `sudo` 要密碼(部署需 sudoers 一行)。服務 fcv-crane/fcv-web-v3 active,binary md5 `b755b403`(09-19 版)。
  - **網路層**:`192.168.1.0/24` 全段掃描**只有 `.30`/`.34`/`.35`**;**`.31`(SE3 右網關)、`.32`(計米右/中+MH300 網關)、`.33`(X518 張力)、`.1`(主路由)、`.100`(本體)全不在**;log `仍未連上 … 離線 2127s`(= 開機起從沒連上)⇒ 疑同一台交換器/上聯線/電源(5 台同時缺)。
  - **`.30` 通但 SE3 左不回**:`init Mode B probe failed after 3 attempts — device not on bus (slave 1)` ⇒ 左繩停用、keepalive ok=0 fail=0(沒初始化就不打)。網關 `.30` 設定 115200 8N2 = 文件 ⇒ 問題在 RS485 線 / SE3 電源 / SE3 P.32·P.36。
  - `.34` SD76 左 OK(讀 0、`readScale: DP read failed` 一次);`.35` ZS-DIO OK(9600 8N1 = 文件)。開機回放 `set_home_ground 0`/`set_motion_hz 30`/`set_roll_correct_hz 30`/`set_wall_height 257`。
  - user 處理後重掃:**`.31`/`.32`/`.33` 回到 eth0**(X518 MAC `00:08:dc:11:11:21` 與測試機那顆相同 = WIZnet 預設,兩網段不衝突)。log `.31 reconnect success`,但 status `dev_gw_b/dev_gw_c/dev_vfd_*` 仍 0 —— **device 旗標/SE3 init 只在開機做,晚到的設備要重啟 fcv-crane 才會初始化**(sudo 要密碼 ⇒ user 或 reboot)。主路由 `.1` 仍不在。
- **本體恢復(WiFi `.26`)**:per user「正式吊機還在維修」⇒ **本體維持接測試吊機 `.31`**(`crane-ip.conf` 不動);測試吊機 `body_link_age_ms=145`、`imu_roll_fresh=1`。
  ⚠️ 本體 :5001 同時有 **official GUI(來源 `192.168.1.10`,經 QWRT 橋 MAT,MAC `be:6a:80:e1:7c:30`)** 連著 ⇒ 正式吊機 GUI 能操作本體,但本體的吊機指令會送測試吊機 —— **official 修好、切換前不要從 `.15:8080` 操作本體**。
  本體重開後 `fcv-arm` 又在重啟迴圈(enabled、手臂未裝)⇒ 照 10-02/10-07 `stop` + `arm_attached off`(未 disable)。
- **official 單獨除錯 —— USR 層**(per user「先看 USR 有沒有起來」):`.30/.31/.32/.34/.35` 全部 ping ✅、TCP 4001 ✅、序列埠設定 = 文件 ✅(.30/.31 115200 8N2、.32/.34 115200 8N1、.35 9600 8N1)。
  - **X518 `.33`**:ping ✅ 但 502 **Connection refused**(RST)×3;吊機端沒有任何 socket 到 .33 ⇒ X518 唯一連線槽被**舊連線卡住**(03:30:44 連上 → 03:32:57 斷,正值 user 接線、`.31/.34/.35` 同時段都有斷線重連)⇒ **X518 要斷電重開**。
  - `.32`:03:30:15 reconnect success 後無錯,但 init 時 `USR_M2 connect failed — gw2 meters disabled` ⇒ 右/中計米、MH300 停用到重啟。
  - ⇒ 設備晚到/卡住都要 **重啟 fcv-crane**;sudo 要密碼 ⇒ 請 user 放 deploy.sh 檔頭那行 sudoers(只放行 restart 兩支服務)或自己重開。左 SE3(.30 後)不回應仍待查。
- **user 重開 official Pi** → `.31` 右 SE3 ✅、`.32` 右/中計米 ✅(-5144/-5489)、左計米 ✅;**X518 又被卡**(全埠 RST、吊機無 socket)⇒ 重開前那條舊連線沒送 FIN(推定非正常關機;無持久 journal 無法證實)。
  **device 旗標只在 `main()` 啟動時設**(`main.cpp:5990` vfd_right、`:6106` dsz_left),最新程式也一樣 ⇒ 🟡 待辦:網關重連時自動重新初始化該設備。
- **X518 手冊查證**(`doc/張力sensor/x518…v1.1.pdf` 15 頁,摘要已補 `summaries/DSZL_107_X518_MODBUS_SUMMARY.md` 新節):網路參數只有 IP/port/協定/遠端 IP·port/機碼,**沒有連線數/逾時/keepalive** ⇒ 異常斷線只能斷電復原;RS485 可同時讀(替代路徑)。
  🟡 根治候選(硬體,待討論):① ZS-DIO 空通道接 X518 電源,吊機偵測「ping 通但 502 拒」自動斷電重開 ② 改走 X518 RS485 → USR 網關。
- ✅ **X518 修復 + 假設驗證**:user 斷電 X518 → 03:52:58 吊機連上 → 監看自動 `sudo -n systemctl restart fcv-crane` → **舊程式正常關閉、X518 有放、新程式 init `[OK] DSZL-107 right/left`**,`tension_valid=1`(左 2.15 / 右 1.63 kg,空載)。⇒ **只有異常斷線會卡,正常 restart 不會**。
  重啟後 device:`vfd_right=1`(keepalive 50/30 s ok)、`meter_middle=1`(-5489)、`dsz=1/1`、`gw_*` 全 1;🔴 **`vfd_left=0`**(.30 後 SE3 `device not on bus`,同前)、🔴 **`meter_right=0`**(.32 slave 2 `init Mode B probe failed`,同 bus 的 slave 1 中計米 OK,Pi 重開那次右計米卻 OK ⇒ **時好時壞,疑 RS485 接觸/終端**,沒另外探測以免干擾吊機輪詢)。
  ⚠️ **左計米讀 0、device scale `0.142857`**(文件 official 左右 `SCAL=000200`;中計米 0.5)⇒ 左錶疑被重設/換過,待查。
- 🔎 **RS485 匯流排逐台掃描**(per user「USR 重接線打亂,一個一個確認」;停 fcv-crane → 各網關 slave 1–16 只送 FC03 → 啟回;腳本在 scratchpad,識別讀 SE3 `0x1001`、SD76 `0x0000`/`0x0001-2`、ZS `0x0032`、Delta `0x2101`):
  - `.31` slave 1 = SE3 右 ✅;`.35` slave 1 = ZS-DIO(`0x0032`=1)✅;`.32` slave 1 = SD76 中 ✅,**slave 2(右計米)無回應**、**MH300 slave 1–16 都沒有**。
  - 🔴 **`.30` ↔ `.34` 對調**:`.34`(8N1)slave 1 回應與 `.31` 的 SE3 **完全相同**(`0x0000`=8、`0x1001`=128、`0x0001`=5000)⇒ **左繩 SE3 接在 `.34`**;`.30`(8N2)slave 1 回**亂碼**(`01 20 00 b8 f4`…)⇒ 8N1 設備(推定左計米器)接在 8N2 網關,連續位元組被判框架錯誤。吊機因此把 SE3 當左計米器(讀 0、scale `0.142857`)。
  - ⚠️ 錯接風險:SD76 驅動每次開機 `resumeMeter` 寫 `0x0000=8` ⇒ 寫到左 SE3 **P.0**(現值本來就 8,兩台 SE3 相同 ⇒ 未改值);但「計米器歸零/暫停」會寫 3/4 進 P.0 ⇒ **對調修好前 official 不要按計米器歸零**。
  - 建議:實體把 `.30`/`.34` 兩條 RS485 換回(與 drop-in/網關序列設定一致),換完重掃 + restart。
- 🔎 **逐顆 USR 確認**(user 一次只留一顆上電;fcv-crane 全程停):`…a5:2c`(.34 8N1)= 左 SE3、`…a5:54`(.31 8N2)= 右 SE3 ✅、`…a5:23`(.30 8N2)= 左計米(亂碼)、`…a5:33`(.32)= 中計米 s1 ✅ + 右計米 s2 **0/20 無回應**(站號 1–32 全掃)、`…a5:40`(.35 9600)= ZS-DIO ✅。
  ✅ **per user「修改 USR、不動線路」⇒ 兩顆網關 IP 互換 + 改停止位元**(`usr_reconf.py`:先比對 MAC、備份兩頁到 `~/run/usr_backup_*.json`、原值帶回只改 IP/stop、`manage.cgi?reset=1&rup=0&rfp=0`):
  `…a5:2c` → **`.30` 8N2**、`…a5:23` → **`.34` 8N1**(經暫時位址)。🔴 **踩坑**:`network.shtml` 有兩個 `staticip`(`var staticip = 1` 與 JS `staticip=document.ip.staticip.value`),取到後者 ⇒ 送出非數字 ⇒ **設備當 0 = DHCP**,`a5:23` 跑到主路由發的 `.12`(用 MAC 掃回來);已改成只取第一個字面值 + 固定送 `staticip=1`。
  重掃:5 顆全部固定 IP、設定對、設備在對的位置。`fcv-crane` 啟動:**`vfd_left=1`(今天第一次)**、`vfd_right=1`、`meter_left=1`(-50)、`meter_middle=1`(0)、`dsz=1/1`(X518 經 stop/start 正常重連,第二次驗證)、`gw_*`/`pqw_water` 全 1;🔴 **`meter_right=0`**(`.32` slave 2 `not on bus`;user 稱面板站號 1/2 OK)。
  ⚠️ `.35` 網關的 gw 設成 `192.168.0.1`(其他是 `.1.1`),同網段通訊不受影響,沒動。
  - 右計米:user 轉右計米繩 → 左/中/右讀值都不變(⇒ `.32` slave 1 確為中計米、右計米沒在通訊);user 現場處理後「再偵測」:停 crane → `.32` slave 2 **20/20**、站號 1–32 = {1, 2} → 啟 crane `[OK] SD76 right USR_M2 slave 2`,`length_right=127`。
  - ✅ **official 設備全到齊**:`vfd_left/right=1`、`meter_left/right/middle=1`(-50/127/0)、`dsz=1/1`、`pqw_water=1`、6 網關 1。user 接著開 GUI `http://192.168.5.15:8080` 測功能(⚠️ 只測吊機側:本體仍接測試吊機;GUI/binary 仍 09-18/19 舊版)。
  - ⚠️ 左計米 device scale `0.005`、中 `0.5`(差 100 倍,疑小數點設定)⇒ 併入「official 計米器捲尺校正」待辦。
- ✅ **本體 eth0 ↔ official eth0(WiFi 橋)**:本體→`.1.10` 0/20 掉包、avg 3.0 ms,對方 MAC `2c:cf:67:12:3f:46`(official 本機)、`:5002` 讀到牆高 257/中計米 ⇒ 確為 official;反向 0/10、2.4 ms(本體 MAC 經橋 MAT = `5c:23:bc:00:33:19`);official GUI 已 ESTAB 到本體 :5001。
  per user **「鋼索還在測試吊機這邊,先不改」** ⇒ 本體**維持接測試吊機 `.31`**(`crane-ip.conf` 不動)。日後切 official:刪 `crane-ip.conf` + `endpoints.conf.official-disabled` → `endpoints.conf`(`192.168.1.10`,走這條橋)+ restart fcv-body。
- 🔐 **official sudo**:user 開了**全開 NOPASSWD**(`/etc/sudoers.d/010_nexuni-nopasswd`)並指示 **「留著不移除」** ⇒ 收尾不要提醒關閉(memory `project_fcv_official_crane_nopasswd`);`deploy.sh` 檔頭已更新。
- ✅ **official 部署最新 main(吊機半邊)**(per user「OK」):
  ① `deploy.sh` official 位址可覆蓋(`FCV_CRANE`/`FCV_BODY`,倉庫用 `nexuni@192.168.5.15`)② 新 `fcv-crane.service`(repo 版換 `/home/nexuni`、`User=nexuni`;舊檔 `.bak-<MMDD-HHMM>`)+ 新 `crcmd.py`(舊 `.bak`),`systemd-analyze verify` 無警告
  ③ crane `deploy.sh crane` → md5 `9f50192f…`(舊 `crane_control_PI.out.prev-1008-1954`),啟動**全部 `[OK]`**、`pay_out_max_hz=50`、dev 全 1、計米 -50/146/0 與部署前一致、回放 home_ground/wall 257 OK
  ④ GUI `v3-2026.10.08-1957` + `server.js`(與舊版同模組);新 drop-in **`fcv-web-v3.service.d/cam.conf` `CAM_BASE=http://192.168.5.26:8091`**(預設會指 `.1.100`,倉庫 WiFi 瀏覽器連不到;repo 副本 `scripts/systemd/official/…/cam.conf`)⑤ `cycle_test.py` 三方 md5 一致。
  🔴 **右張力 -99.846054 kg 固定值、零雜訊**(部署前 ~1.6;左 2.35 正常跳動);DSZL 驅動與 main.cpp 張力碼自 09-19 未改 ⇒ **推定 X518 CH1 輸入飽和(右感測器線鬆/斷)**,時間上與 user 修計米器重疊,待 user 確認。
  ⚪ log 小 bug:`[OK] USR_W (ZS-DIO water) @ 192.168.1.32` 印的是預設值,實際連 override 的 `.35`。
  ✅ 右張力:user「張力我測好了,是對的」⇒ 回到 左 2.41 / 右 2.44 kg(-99.85 是修計米器期間的暫態)。
- 🆕 **新 USR(中繩 MH300 專用)**(per user 新增):`192.168.1.x` 掃不到、出廠 `192.168.0.7` 也沒有 → Pi 上沒 tcpdump,改 **Python AF_PACKET 嗅探** `d4:ad:20:*` + UDP 1500/1901 廣播搜尋 ⇒ **MAC `d4:ad:20:a2:a5:2a`、`10.0.0.147`**(固定、gw `10.0.0.1`、115200 8N1;以前用過別網段)。
  暫掛 `10.0.0.250` 讀設定 → per user「OK」改成 **`192.168.1.36` 固定、gw `.1.1`**(`usr_reconf.py` 加 gw 參數;備份 `~/run/usr_backup_a52a_*.json`)。暫掛 IP 已移除。
  USR 校驗代碼:**0=NONE 1=EVEN 2=ODD**(偶是 1);資料位元/停止位元填數字本身。
  per user「用 B」⇒ 背景掃序列格式找 MH300(RTU 22 組 + ASCII 8 組 × slave 1–16,讀 `0x2101`;user:「MH300 跟 CLV900 一樣的東西」= 程式 CLV900 那個中繩位置)。
  - 第一輪 MH300 **未上電**(user 後補)⇒ 作廢;有電後 115200 8N2/8N1 無回應;全掃 RTU 20 組無回應 → user 中途「改通訊模式」⇒ 重掃;user「**115200 N81 我確定**」⇒ 網關固定 115200 8N1:**RTU slave 1–247、ASCII 1–32 全部無回應**。
  - 🔴 USR `.36` status 頁:**序列埠 TX 2520 / RX 0 bytes**(= 本次探測送出量)⇒ MH300 **一個位元組都沒回** ⇒ 不是站號/格式,是**接線(A/B 反、端子錯)或參數未生效(需斷電重開)**。待 user 現場:斷電重開 MH300、A/B 對調、確認 SG+/SG− 端子、`09-04` 為 RTU(非 ASCII)、`09-00` 站號。網關停在 115200 8N1。
  - 🔴 我的探測漏洞:一直只讀 Delta `0x2101`、有回才讀 `0x1000` ⇒ CLV900 若對不支援位址**不回**就會被漏掉;改讀 CLV900 合法位址 `0x1000`/`0xF700`/`0x0001`(per user 看 CLV900 doc):站號 1–247、**站號 3(user 確認)30 次** ⇒ 仍 0 回應(TX 4736 / RX 0)。
  - 🔴 **手冊查到根因(推定)**:`900-0007M1` = **900M 迷你型**,p.17 迷你型接線圖註「**`*S+/S-` 是非標配功能,需依外加 485 擴展卡**」⇒ 沒卡就不會有任何回應;程式裡 CLV900 一直 `[SKIP] hardware not installed`,以前推定從沒真的通訊過。已補 `summaries/CLV900_INVERTER_MODBUS_SUMMARY.md`。待 user 確認機身銘牌與有無 485 擴展卡(若實體其實是台達 MH300,則它有內建 RS-485,要再查接線)。
  - user:「擴展卡有」;再測仍 0 回應(TX 6952 / RX 0)⇒ 建議交叉測試(變頻器借接 `.32` 或新 USR 借接計米器線)隔離是 USR 端還是變頻器端;user 未回,轉去計米器。
- 🔎 **計米器修好後三顆參數比對**(停 crane 讀 → 啟回;`agent_ai/.tmp/fcv_mh300/sd76cfg.py`):SCAL 000200 ×3 ✅、DP 2/2 ×3 ✅(之前左 scale 0.005 的差異已消失)、mode 0 ×3、PRE 0、AL1 5 ×3;
  🔴 **左計米器站號變成 3**(程式 `FCV_METER_LEFT_SLAVE=1` ⇒ 重啟後 `SD76 left init failed`);⚠️ AL2 左 5 / 右中 10(程式不用警報);⚠️ 右計米對「讀 1 個暫存器」固定回 2 個暫存器的 0、首筆偶爾亂碼(完整讀 + 重試正常,驅動讀得到)。待 user 選:面板改回 1(推薦)或 drop-in 改 3。
  - ✅ user 左計米站號改回 1 → 重啟 crane:`meter_left/right/middle=1`、scale 0.5 ×3。三顆設定唯一差異 **AL2 左 5 / 右中 10**(程式不用)。
- ✅ **中繩 CLV900 通了**(user 現場處理後,做了什麼待問):`.36` 115200 8N1 **slave 3** 30/30;U0-00=3(停機)、U0-01=0(無故障)、**F7-00=3、F7-01=4(115200)、F7-02=3(8N1)**、F7-03=0;讀 `0x0001` 回 exc 02(寫專用,正常)。⚠️ **slave 172(0xAC)也回同樣內容**(同一台應兩個位址;匯流排上別用 172)。USR `.36` RX 284 bytes。
  🟡 待辦:程式支援中繩 CLV900 —— 現在 `[SKIP] CLV900 hardware not installed`、寫死 `USR_A` slave 3 ⇒ 改 config-driven(網關 `.36`、slave、啟用旗標),先給計畫。
- ✅ **中繩程式支援(第一 + 第二階段)**(per user「OK 照這樣做,第二階段直接做完」;GUI/fake/harness/changelog 補漏/NTP 探測**交給 AI-2**,`main.cpp` 由本 session):
  - **啟用**:`FCV_CLV900_ENABLE=1` + `FCV_EP_USR_MW_HOST` + `FCV_CLV900_SLAVE`(預設 0 = 舊行為);開機探測 + 讀 F0-00/F0-01 只警告不寫;keepalive 每秒讀 U0-00/01/03(新故障 → 停手動中繩 + `EVT middle_fault`,**不自動 reset**);status +`middle_state/fault/run_hz/hold/auto/ratio/dir_invert/comm_fail`、`dev_gw_mw`。
  - **手動**:`middle_hold <pay|retract> <on|off>`(併入 hold 租約 1.5 s / `any_hold_active` / `hold_all_off`;`ERR motion_active`)、`middle_stop`;`middle_set` 也改走 `middle_run`(方向反轉一致)。
  - **自動跟隨**:`set_middle_auto`/`set_middle_dir_invert`/`set_middle_ratio`(持久化)。motion_rope 與**兩側同步 ▲▼ hold** 都用 **P 追蹤** K×左右平均位移(KP 1 Hz/cm、下限 2 Hz),中止:`middle_lag`(放繩落後/收繩超前 >15 cm = 中繩會吊住機器,無中繩張力計)、`middle_wrong_dir`(反向 ≥5 cm)、`middle_fault`;中止歸類安全中止(緊急停 + EVT、不做 fine_adjust)。auto 開但中繩不可用 ⇒ motion `ERR middle_unavailable`、hold on 也拒。🔴 修既有漏洞:motion_rope **逾時/length_diff 中止時中繩不會停** ⇒ 迴圈出口一律停中繩。
  - 本機 `g++ -std=c++17 -fsyntax-only` 0 錯;**official 已部署**(md5 `f385ccf9`,drop-in `fcv-crane.service.d/middle.conf`,repo 副本同名):`[OK] CLV900 middle winch USR_MW 192.168.1.36 slave 3 (state=3 fault=0)`、status 新欄位齊。
  - 🔴 **開機警告抓到**:`F0-01=0`(頻率源=面板,F0-16 預置 **50.0 Hz**)、**F0-04/F0-05 加減速 20.0 s** ⇒ 照現設定一轉就往 50 Hz、停機要 20 s ⇒ **先不轉**;建議 F0-01=8、F0-04/05=2.0 s(面板或 FC06 寫,待 user 選)。單位:頻率 0.1 Hz、時間 0.1 s(F0-09 / F8-03 = 50.0 Hz)。
  - ✅ per user「通訊寫」:停 crane → 確認 U0-00=3 → 備份(`~/run/clv900_params_before_*.json`:F0-01=0、F0-04=200、F0-05=200)→ FC06 寫 **F0-01=8、F0-04=20、F0-05=20**,echo + 讀回一致 → 啟 crane,開機警告消失。⚠️ 手冊沒寫通訊寫入是否進 EEPROM ⇒ 下次斷電重開要再讀一次。
  - ✅ **轉動測試**(`middle_hold pay` 約 1 s @10 Hz,脫離鋼索):state 3→**1(正轉)**、run_hz **10.0**;off 後 ~1 s 回 3/0.0;fault 0。中計米 3 不變(捲筒沒掛繩)。🟡 待 user 目視確認「pay = 放繩方向」,反了就 `set_middle_dir_invert 1`;自動跟隨要等中繩掛上、中計米跟著動再開。
  - user 要求再轉一次放繩方向:同上(state 1、10.0 Hz、放開 ~1 s 停)。🟡 待 user 回報實際轉向。
- ✅ **AI-2 交付**(另一 session,本 session 驗收):GUI Manual `#mid-card`(狀態/運轉 Hz/中計米/故障/hold/讀失敗、速度、▼放 ▲收 按住 `.btn.mhold` 共用 hold 續約、停止)+ Setting「中繩 · CLV900」(自動跟隨 confirm、方向反轉、比例)+ EVT 處理;fake_robot 全指令與租約;harness **267 全過**(+36,本 session 重跑確認)、check_console 過。
  AI-2 自加:吸附中鎖中繩按住鈕、`dev_clv900=0` 時「停止」仍可按(唯一出口)。**GUI `v3-2026.10.08-2133` 已部署 official**。
  - **NTP(AI-2 唯讀)**:official timesyncd 開但從未同步;**兩條 default route,eth0(→ 192.168.1.1,倉庫裡沒有對外)metric 100 贏過 wlan0 600** ⇒ 公網流量走錯;綁 wlan0 時 NTP/DNS 都通。RTC 讀 1970(無電池)。chrony 未裝。
    建議(未執行,待 user):倉庫讓 wlan0 優先(`nmcli … ipv4.route-metric 50` 或 eth0 `never-default`);現場無公網:裝 Pi5 RTC 電池 + 對時後 `hwclock -w`,可選 chrony(吊機當 local stratum、本體指吊機)。
- ✅ **中繩速度快選**(per user「跟吊機一樣給 10 20 30 40 50」):`#mid-hzq`(`data-mhzq`,反白照 status、dev=0 停用)+ harness 3 項 ⇒ **270 全過**(第一輪 1 項失敗 = 已知的手臂工具 seg 時序不穩,重跑全過)。GUI `v3-2026.10.08-2148` **部署 official + 測試吊機**。
- ✅ **中繩連動勾選**(per user「manual 新增連動打勾,吊機同收同放跟著轉」「速度用當前選中的速度為主」):`set_middle_link`(不存檔)= 兩側同步 ▲▼ 帶中繩(基準 middle_hz + 計米 P 補償 + lag 中止),不影響自動動作;GUI 勾選框 + 標頭提示;harness 278 全過;crane + GUI `v3-2026.10.08-2205` 已部署(official 驗 `OK middle_link=1/0`)。⚠️ 「當前選中的速度」我解讀為**中繩卡選的速度**(若 user 指鋼索那組要再改)。
- ✅ **連動停止不同步**(per user「連動不會同時停止」):log 顯示 `cmd_hold off` 與 `[middle] hold follow stop` 同一週期 ⇒ 軟體沒延遲;主因是**減速斜坡**:SE3 讀值 **P.21=0(0.01 s)、P.7=0.10 s、P.8=0.50 s @ P.20=60 Hz**(兩台一致),CLV900 F0-04/05 = 2.0 s @50 Hz ⇒ 25 Hz 停車 SE3 ~0.2 s vs 中繩 ~1.0 s。
  修:① `cmd_hold` combined OFF 直接停跟隨中的中繩(先於 SE3,省 hold_loop ≤200 ms)② CLV900 **F0-04/F0-05 2.0→0.5 s**(停 crane、備份 `clv900_params_before_*`、FC06、讀回)。實轉 10 Hz:啟動 <0.25 s、放開 <0.4 s 停、fault 0。⚠️ 掛繩後減速回灌可能 Err06 → 調回 ~1.0 s。crane 已部署 official。
- ✅ `.21` 風扇網關(per user「應該修復了」):本體 21:16:35 reconnect success、`dev_qx=1`、`pwm status` 正常(duty_max 9、ch1 5%)。本體 state 已回 idle(user 按 RESET)。
- 🔎 **本體 Error 原因**(per user「本體 ERROR 為什麼」):本體開機 20:23 盤點正常 Idle;**`emergency_detach` 跑了 2 輪(~20:34、~21:22)**⇒ 依設計進 Error。觸發來源本體 log 沒記、兩台吊機 log 都無 estop ⇒ 推定 GUI「🔴 緊急脫離」被按(待 user 確認)。兩輪皆 PARTIAL:`arm_retract` 失敗(手臂服務重啟迴圈)、`fan_stop` 失敗(**`.21` PWM 網關開機起就連不上**);推桿收回/破真空/幫浦關都 OK。現況 p5–p8≈0 ⇒ RECOVER(要 4 顆都吸著)必失敗,**應按 RESET**。
  - GUI 錯誤橫幅 RECOVER 說明寫「重驗 9 顆吸盤」(v1 殘留)→ 改 4 顆(含 2 處註解),已部署。
- 🔎 **手臂(user:手臂裝回可測了,有錯誤)**:`motor_api` 每 ~3 s 重啟 —— `M1 init OK`,**M2 `switchControlMode FINAL failed … power cycle may be needed`** ⇒ `DamiaoAPI init failed` exit 1。同條 CAN 上 M1 正常 ⇒ 轉接器/匯流排 OK,問題在 M2:電源(48V)/ M1→M2 CAN 接頭 / 故障鎖存(斷電才解,09-03 踩過)/ 換過馬達(ID 非 0x02/0x22)。`damiao.cfg` 08-27 起沒改。之前一段是 M1、M2 都不回(手臂未接)。待 user 處理;是否先停 `fcv-arm` 重啟迴圈待 user 決定。
  - 🔴 踩坑:ssh 遠端指令 `cat > f && … & …` 的 `&` 讓整串進背景 ⇒ stdin 變 /dev/null ⇒ 檔案 0 bytes;要先 `scp` 再 `setsid nohup`。掃描腳本 `agent_ai/.tmp/fcv_mh300/hunt.py`(Pi `~/run/mh300_hunt.py`)。
- 🔎 **本體 Error 第 3 次**(per user「看一下本體 ERROR 為什麼」):**22:13:51 `emergency_stop`**(時間由測試吊機 `[dispatch] cmd='stop' t=21325089` 換算;前一輪 `t=18224054` = 21:22:10 對得上)。當時本體→吊機 `set_imu_roll` 每 5 s 照常 ⇒ **不是 GUI「吊機失聯自動急停」**,是 STOP 按鈕(哪個 GUI 無 log 可分)。detach 這輪只剩 **`arm_retract` 失敗**(`arm_slot RIGHT: ERR arm_no_reply`),`fan_stop` 已 OK(.21 修好)⇒ PARTIAL ⇒ 依設計留 Error;22:18 查時已 idle(有人按了 RESET)。
- 🔎 **手臂(user:手臂修好了)**:`fcv-arm` 仍重啟迴圈(NRestarts 459),M2 照樣 `switchControlMode FINAL failed`。停服務做**唯讀 CAN 掃描**(`agent_ai/.tmp/fcv_arm/dm_scan.py`,Pi `~/run/dm_scan.py`:對 ID 1~0x7F 送 0x7FF 讀參數 RID 7/8/10 + 0xCC 刷新):**只有 M1 回**(ESC_ID 1、MST_ID 0x11、模式 1=MIT、err=0);**M2 在任何 ID 都零回應**,也沒有 canId 0x000(= 出廠預設的新馬達)⇒ 不是 ID 改掉,是 M2 沒上電 / CAN 沒接到 / M2 本身。服務已重新 start;M2 一有回應,下次自動重啟就會 init 成功,不需我動作。
- ✅ **M2 修好**(user):`fcv-arm` 22:41:24 自動起來 ——`M2 init OK (slave=0x2 master=0x22)` → STARTUP(M1 回零、M2 到 RIGHT 滾筒)→ `INIT: OK`。沒有停服務重掃(會打斷已使能的手臂),改用本體 `arm_status`:M1 `pos≈0 en=1 init_done=1`、M2 `pos=0.862 en=1 tool=roller`,兩顆 `err=0x1`(達妙狀態碼 1 = 已使能,非故障);本體 `dev_arm=1 arm_ready=1 state=idle`。測試吊機 GUI 仍掛 `no-arm.conf`(`FCV_NO_ARM=1`)。
- ⏸ user:「手臂先做維修,電腦都先關機」⇒ 機器全關,以下離線做、**未部署**。
- ✅ **中繩連動:放繩多放 % + 連動方式開關**(per user「左右放繩時中繩長度也要跟上,甚至更多,在連動鋼索這邊可以設定」「這個功能要可以開關,在中間計米器失效的時候做獨立控制」)。詳 changelog `[2026-10-08c]`:
  - `set_middle_pay_extra <0..100>`(持久化):**只放繩**時中繩追 k×(1+%)、Hz 前饋 ×(1+%);收繩不加;`middle_lag` 仍以 k×平均判。
  - `set_middle_link_track <0|1>`(持久化,預設 1):0 = **只照速度**,▲▼ 跟隨不讀中計米器 ⇒ 中計米器壞時鋼索不再被連動擋死(🔴 無落後保護)。`ERR middle_unavailable` 帶原因。
  - GUI 中繩卡加兩組控制 + 紅字出口提示;harness **287 全過**(+9);吊機本機完整編譯連結 OK、零警告。
  - ⚠️ 解讀待 user 確認:①「更多」= 放繩多放 %(收繩不加)②「獨立控制」= 只照速度的連動(取消連動則中繩完全靠自己的 ▼▲)。
  - 部署待開機:crane(official)+ GUI 兩台(bump 版號)。
- 🔎 **手臂(user:本體開機,手臂看一下什麼問題)**:開機 STARTUP/INIT 都 OK(M1/M2 init OK、`dev_arm=1 arm_ready=1`)。問題在 **M2 轉滾筒位(RIGHT)到不了**:`lr_move_to_slot FAIL pos≈0.797→0.790→0.783 target=0.8558`(誤差 0.053~0.072 rad ≈ 3~4°,逐次變差),停住時 M2 tau ≈ 3.4 Nm(像頂到東西);LEFT 0.2 每次都到位。目標 0.8558 = 09-10 手轉量的絕對值(`lr_stop_valid=false` ⇒ 用 `M2_SLOT_RIGHT_RAD`),M2 修過後零點/機構若有變就會失準。
  - user:「M2 先失能,我手轉看看」「M1 也失能」⇒ 經 TCP 9527 送 `M2 DISABLE`、`M1 DISABLE`,回讀兩顆 `en=0 hold=0`,3 s 後仍 0。📌 寫 `~/run/arm.in` fifo **沒有效果**(motor_api 不讀 stdin),要走 TCP 9527。
  - user:「M1 M2 致能」⇒ `M1 ENABLE`/`M2 ENABLE`(`enable_slot` 以當下位置為 hold_pos,不跳動)→ 兩顆 `en=1 hold=1`:**M1 −0.012、M2 1.6161**(tool=between)。🔴 **失能期間 STATUS 的 pos 是凍結的舊值**(失能前 M2 0.8803,致能後才讀到真值 1.6161)⇒ 手轉量位置要在失能狀態送 `M2 MIT 0 0 0 0 0` 刷新回授再讀(`SET_HALF_RANGE` 註解記載的做法),這次手轉的過程值沒量到。

## 2026-10-07:測試吊機 DHCP 換 IP(.25 → .31)→ 本體連不到吊機(IMU 推送斷)

- 兩台開機後 `.25` 不通。倉庫路由器 ARP(`drayops.py 5.1 arp`):`raspberry-cran` = **192.168.5.31**(MAC `DC-A6-32-75-8A-AA`,WiFi S5)、`washrobot` 仍 .26。
  SSH ECDSA 主機金鑰與 known_hosts 的 .25 **逐字相同** ⇒ 同一台。路由器 bindmac 只綁了 NVR/打卡鐘/LED,**兩台機器人都是 DHCP**。
  ⚠️ `.31` 不在 `~/.ssh/config` 的 claudeuser 清單 ⇒ 要 `-i ~/.ssh/claudeuser`。
- 症狀(user「IMU 有問題」):IMU 本身正常(`dev_imu=1`、n_angle 遞增);本體 `FCV_EP_CRANE_HOST=192.168.5.25` 寫死 ⇒ `[WARN] crane IMU push channel not up yet`、`crane_peer_age_ms=-1`;三條本體↔吊機連線(主/estop/IMU)全斷 ⇒ 吊機平衡退回計米器、`crane_goto` 失敗。
- **per user 選 B(臨時)**:本體加 drop-in **`~/.config/systemd/user/fcv-body.service.d/crane-ip.conf`**(`FCV_EP_CRANE_HOST=192.168.5.31`),unit 本體不動 → daemon-reload + restart → `[OK] crane 192.168.5.31:5002`、estop/IMU channel connected;吊機 `body_link_age_ms=239`、`imu_roll_fresh=1`。
  (未採 A:路由器 bindmac 把吊機固定 .25。)
- ✅ **部署 duty_max 9**(per user「1 8 開始」):`deploy.sh` 測試機吊機位址改可覆蓋 **`FCV_CRANE=user@192.168.5.31`**(預設仍 .25);本機 `~/.ssh/config` claudeuser 清單加 `.31`。
  `deploy.sh body` → `[OK] crane 192.168.5.31:5002`、selfcheck 全過、未吸附;真機 `pwm set 1 50 65535 10` → `ERR pwm_duty_rejected_must_be_5_to_9_pct`、`pwm status` `duty_max=9`、ch1=5。`deploy.sh script` 兩台 md5 一致。
  ⚠️ 補漏:`server.js` Mission `fan=` 仍收 `:10` → 改 5~9(harness 216 過;**未部署**,要 `deploy.sh server` 重啟 fcv-web-v3)。⚠️ `pwm set` 參數序是 `<ch> <hz> <control> <duty>`(不是 duty 在前)。
- ✅ **離線:`length_diff_max_cm` 查明**(`Crane_control_PI/main.cpp:3335-3344`):`|(L−L0)−(R−R0)| > 10`(整數 cm、每 20 ms、無去抖),L0/R0=動作起點;**只在 `motion_rope`**(pay_out/retract/goto/Mission);hold(GUI ▲▼)、side_measured、fine_adjust **不檢查**;與 `level_diff` 無關。
  10-01 重算(`agent_ai/.tmp/1001speed/*.json`):起點 L−R +5~+7、絕對 13~16、**相對最大 9** ⇒ 照設計不觸發。待辦改列「餘裕」(見總表 A)。
- 🧹 總表 A「cycle_test 上行 50 Hz」刪除:`cycle_test.py:307` 早已 `DOWN_HZ, UP_HZ = 30, 30`。
- ✅ `server.js` 已部署(`FCV_CRANE=… deploy.sh server`);吊機 **`/etc/systemd/system/fcv-web-v3.service.d/no-arm.conf`**(`FCV_NO_ARM=1`,🔴 **手臂裝回後刪除** + daemon-reload + restart);本體 `fcv-arm` stop、`arm_attached off`。
- 🆕 **Dashboard 要加兩支攝影機畫面**(per user,優先於 8)。查證:
  - 攝影機在**本體那段有線網**:本體 `eth0 .1.100` ping/RTSP `.112` OK(子碼流 h264 **800×448 25 fps**);吊機 `.1.10` 那段**完全看不到**(只有 .30~.34)⇒ 串流要由本體出,瀏覽器直連本體 `.26`(經吊機轉 = WiFi 流量兩倍)。
  - 🔴 **攝影機 2 `.113` 失聯**:本體/吊機兩段 ping sweep 都沒有、XM 廣播搜尋(UDP 34569、msgid 1530)只回 `.112` ⇒ 推定沒電/沒接線;✅ **user 處理後重掃:`.113` 回來了**(同 MAC/SN、仍在本體段),兩支 RTSP 子碼流皆 h264 800×448 25 fps。本體段另有一台未知 `.250`(MAC `58:23:bc:00:33:1a`)。
  - 本體 ffmpeg 子碼流 → MJPEG 實測(10 s):8 fps q7 = CPU 7.4%、3.2 Mbps;**5 fps q9 = CPU 5.5%、1.6 Mbps**/支。⚠️ 本體 WiFi 同時承載控制 + IMU 推送,上線後要量 `body_link_age_ms` / `imu_roll_age_ms`。
- ✅ **攝影機上線**(per user「可以 開始做」;細節 changelog `[2026-10-07a]`):本體 **`fcv-cam`**(`scripts/cam_mjpeg.py` :8091,user service、enabled)+ GUI Dashboard 第一張「攝影機」卡(`v3-2026.10.07-2042`)+ `server.js` `{src:'cam'}`(已部署)+ `deploy.sh cam`。
  驗證:harness 229 過(新 `[cam]` 13 項);真機 `.jpg` 兩支 800×448;開兩路 30 s:ffmpeg 各 ~5%、WiFi 1.46+0.98 Mbps、`body_link_age_ms` avg 127→144 / max 253→256 ⇒ **無明顯劣化**。🟡 待 user 用瀏覽器實看(Ctrl+F5)。
  ✅ **OSD 移除**(per user「兩隻 OSD 幫我移除」):原因是鐘沒對時(停 09-14 / 09-10)、兩支標題都 `CAM01`。XM 34567 `AVEnc.VideoWidget` 時間/標題的 `EncodeBlend`+`PreviewBlend` → false,兩支 Ret=100、讀回 false、快照確認無字。
    工具由 git 歷史救回擴充成 **`scripts/xm_ipcam.py`**(`get`/`osd on|off`/`time`,在本體跑);**原設定備份**(per user 問)`config/ipcam/videowidget_{112,113}_before_osd_off_20261007.json`(本體 `~/run/cam_osd_backup_*` 另有一份)。GUI 註解同步 `v3-2026.10.07-2106`。
  📌 決策:**推翻 09-01「攝影機不列入本版架構」**(CLAUDE.md / HARDWARE.md 已改);畫面**不經吊機轉送**(省一倍 WiFi)。
  ✅ changelog 漏記的 10-02(`0f61823`)、10-06(`c069c84`)已由 AI-2 補上(`[2026-10-02a]`、`[2026-10-06a]`,索引 165)。
- ✅ **攝影機優化**(per user「速度有點慢、關閉不該啟用的功能、串流再優化」;細節 changelog `[2026-10-07c]`、`summaries/IPCAM_XIONGMAI_SUMMARY.md`):
  - 查因:攝影機本身 20~25 fps,**慢的是我們 MJPEG 5 fps**。
  - 攝影機端關掉:音訊、移動/人形偵測、雲端 P2P、推播、線上自動升級、每週自動重開、錄影;子碼流 GOP 2→1 s;慢快門 2→0。改前整份備份 `config/ipcam/features_*`;重開機後全數保留。⚠️ 重開後兩支 **20.0 fps**(重開前 25,原因未明、已排除慢快門)。
  - 串流改 **H.264 直通**(`scripts/cam_relay.py` 取代 `cam_mjpeg.py`,本體舊檔已刪):fMP4 一張一段、常駐 + GOP 快取;GUI `<video>`+MSE,MJPEG 退為備援 + 格式選單(`v3-2026.10.07-2126`)。
  - 驗證:harness 231 過;**真 Chrome 無頭(CDP,`harness/cam_browser_check.js`)**:兩路 H.264 20 fps、播放端落後 ~0.4 s、掉格 ~1%(第一版追趕門檻 0.3 s 害 1.1 倍速 + 掉格 30%,已改遲滯);本體 ffmpeg 各 0.2% CPU、WiFi 0.34+0.27 Mbps;兩路開著鏈路年齡 avg 128/max 248(=基準)。🟡 待 user 實際瀏覽器看(Ctrl+F5)。
- 🔴 **待辦/伏筆**:① 吊機 IP 若再變(或回 .25)要改/刪 `crane-ip.conf` ② `deploy.sh`(`CRANE=user@192.168.5.25`)與 `deploy_web.sh`(`PI` 預設 .25)、GUI 網址都還指 .25 ⇒ 部署要覆蓋或改用 .31(`PI=user@192.168.5.31`)③ 上 official 時:drop-in 依檔名排序,`endpoints.conf`(還原後)排在 `crane-ip.conf` 之後會蓋過它,但仍建議一併刪除。

## 2026-10-03:從上層搬入(agent_ai 上層日誌拆檔時的核對;上層原有、這裡沒有的)
- ⚠️ **踩坑(09-01 續十二)X518/DSZL 張力刻度校正順序不可省**:空載 `zero_tension` → 掛已知重量 → 算 scale → **移除後必須回 0**(負向對照)。當天第一次跳過歸零、拿「碰巧讀到 0」當基準,方向與大小全錯,是靠移除後停在 raw 117 才抓到。official 計米器/張力計重校時照這個順序。
- 📌 **實測依據(09-04)力控貼合 `DEPLOY_F` 耐久基準**:滾筒 50 輪(吸盤連續 22 分鐘零衰減)+ 雙工具 10 輪,全部零中止(當時目標 15 N·m;09-30 起工作力道改 3)。手臂裝回後可當回歸對照。
- 🟡 **不在現行待辦總表、需確認去向**:① 急停必須能切 57.6 V 動力(09-13 待辦;`HARDWARE.md` 問題 #12,安全項)② 09-04 後端「作業佔用」旗標(⏸ 待拍板,未見結案)③ `plans/mission_manual_isolation.md` 檔頭仍寫「未實作」,但 09-17 已部署 → 檔頭過期。
- ✅(10-03 已改)~~🟡 **`CLAUDE.md` 待修**~~:L12「以 motion_flow.md §2 為準」已過期(現況權威是 `HARDWARE.md`/v2 規格);兩台共用一顆 binary 靠 drop-in 區分、本體 drop-in 目前停用(上 official 前要改回)、「測試移動單次 ≤5 cm」都還沒寫進 CLAUDE.md。
- ⚠️ 10-02 引用的 `agent_ai/.tmp/npp1700/spec.pdf` 在不備份的 `.tmp/`,建議搬進 `doc/`。


## 2026-10-02:測試機開機 + 風扇 × 匯流排錯誤重測

- 開機檢查:吊機服務正常、本體連線 ~200 ms。per user 重設地面/最高歸零:牆高 **248**(原 244)、計米器 −248/−248(**機器在頂端,per user 正確**)、張力 51/48 kg、roll −0.26°。設定檔只有 `hold_hz 30`。
- 本體重開後 `fcv-arm` 又起來(enabled)⇒ 為對齊 10-01 條件已再 `stop` + `arm_attached off`(手臂仍在維修)。
- 👁 **觀察:風扇 × 匯流排錯誤重測**(同 10-01 方法:本體 idle、未吸附、掛繩頂端;每段 30 s,每 0.5 s 打 `relay_status`/`pwm status`/`water_level`)
  | 段 | 停 | 7% | 停 | 7% | 停 | 8% | 停 | 8% | 停 |
  |---|---|---|---|---|---|---|---|---|---|
  | 驅動層錯誤/30 s | 3 | **21** | 2 | **20** | 1 | **14** | 1 | **12** | 0 |
  - 7% 分布:`.21` QX 4/8、`.22` XKC 13/14 共 6~7、JC-100 3~8、`.20` ZS-DIO 2;8%:QX 4、XKC 5~6、ZS-DIO 3~4、**JC-100 0**。
  - 應用層指令失敗:7% 3+2、8% 5+4(多為 `relay_status`)。
  - 🔌 **條件差異:本次 USR 串列轉 TCP 閘道已改 5V 隔離電源**(per user,本體開機前換;log 確認 11:43 開機後閘道無斷線重連)。
  - 對照 10-01(停 0 / 7% ~7 / 8% ~15):**風扇轉 ⇒ 三條匯流排同時出錯、停即歸零 → 第三次重現**;但今天 **7% 反而比 8% 多**,10-01 的「隨轉速上升」今天沒重現 ⇒ 「劑量相關」不成立,只能說「轉就出錯、量級 10~20/30 s」。
  - ⇒ 隔離電源**未見改善**(7% 甚至較多);但電源與日期兩個變因同時變,待同日換回原電源 A/B 一次才能歸因。若原電源也 ~20 ⇒ 電源非路徑,改查 RS-485 線屏蔽/終端/接地/與電調線並行。
  - ✅ **同日 A/B:換回原電源重測**(12:09–12:14;本體因換電源重開,同條件:idle、未吸附、頂端、`fcv-arm` 停):
    | 段 | 停 | 7% | 停 | 7% | 停 | 8% | 停 | 8% | 停 |
    |---|---|---|---|---|---|---|---|---|---|
    | 原電源 | 1 | **15** | 2 | **17** | 1 | **23** | 0 | **16** | 2 |
    | 5V 隔離 | 3 | 21 | 2 | 20 | 1 | 14 | 1 | 12 | 0 |
    ⇒ **兩種電源同一量級(風扇轉 12~23 / 30 s、停 0~3)⇒ USR 閘道電源不是干擾路徑**。分布同樣是 `.20` ZS-DIO + `.21` QX + `.22` XKC/JC-100 一起出現。下一步查 RS-485 線(屏蔽、終端、與電調電源線並行)與接地。
    ⚠️ 驅動層 log 有「同一處錯誤重複發生,本輪已抑制」⇒ 計數是**下限**(QX 每段都剛好 4,疑似被抑制封頂)。
    📌 測試腳本修正:逾時後等 1.5 s 吃掉遲到回覆再繼續(上一輪的錯位問題),本輪回覆已對齊。
  - ⚠️ 測試腳本缺陷:逾時(3 s)後遲到的回覆讓後續「指令 ↔ 回覆」錯位一格(log 中 `pwm set` 拿到前一個 `pwm status` 的回覆);**風扇實際有照計畫切換**(錯位回覆中的 `ch1=7/8` 與段落吻合),驅動層計數取自本體 log 不受影響,應用層失敗數只供參考。結束風扇 5% 已回讀確認。

- 🟡 **風扇 `duty_max` 10 → 9**(10-06 per user;依據 10-02 電源實測:2×NPP 50 A,雙顆 10% = 49 A 頂限流、9% = 45 A)
  - 本體:`WASH_ROBOT.h` 新增 `PWM_DUTY_MAX_PCT = 9.0`,init 後 `pwm_.setDutyLimits(min, 9)`(driver 預設仍 10 = 馬達規格);超出回 `ERR pwm_duty_rejected_must_be_5_to_9_pct`。
  - GUI:Manual/Mission 風扇輸入 max 9、`setFanDuty` 5~9、Mission 驗證 `fan pct 5~9`、狀態字 ≥9 顯示「上限」;`cycle_test.py` 起跑前擋 `fan pct > 9`;fake_robot duty_max=9;harness +1 項(216 全過)。
  - 部署:✅ GUI `v3-2026.10.06-1147`;⏳ **本體 + `cycle_test.py` 待本體開機**(`deploy.sh body` + `deploy.sh script`,後者要兩台都在)。加第 3 顆 NPP 後改回 10。

- ✅ **(10-02 改、10-06 部署)「上下行照使用者選」第二步**(per user「兩件事都做」):① `PAY_OUT_MAX_HZ` 30 → **50**(goto / Mission / side_measured 下行也照 motion_hz;`set_pay_out_max_hz` 仍可壓低,不持久化)② `set_hold_hz` / `set_motion_hz` 上限 120 → **50**(`ROPE_USER_HZ_MAX`,馬達額定)③ GUI Manual hz 輸入 max 50。⚠️ Mission 實際仍 30/30:`cycle_test.py` 自己設 `DOWN_HZ, UP_HZ = 30, 30`(介面不能選)。
  - ✅ 10-06 真機:`pay_out_max_hz=50`;`set_hold_hz 60` / `set_motion_hz 51` → `ERR hz_out_of_range (1..50)`;`set_hold_hz 50` OK。GUI `v3-2026.10.06-1025`。
(per user「GUI manual 吊機更改速度上下不一致 → 上下行照使用者選的去跑」;`main.cpp`,已部署測試機)
  - 原因:09-30 `dir_hz()` 對所有 pay_out 路徑套 `pay_out_max_hz`=30,hold ▼ 也在內;GUI 沒標示 ⇒ 看起來上下不一致。
  - 改:hold 路徑(`vfdStartRopeHold`、`dual_vfd_hold_start`、hold-sync 平衡 base 與結束 reset)改用 `g_vfd_hold_hz` 不夾;**goto / Mission / side_measured / 手動 pay_out(motion_hz)仍夾 30**(我採的範圍,user 要全放再改)。
  - ✅ 真機:hold_hz 50 → 下 50;30 → 上下 30;40 → 上下 40(HOLD-TRACE `hz=`)。
  - ⚠️ `set_hold_hz` 上限仍是 `VFD_MAX_HZ`=120,現在下行也照跑(馬達額定 50)⇒ 要不要收到 50 待拍板。設定檔現 `set_hold_hz 40` + `set_balance_imu_kp 0.1`。
  - 👁 log 另見:17:53 一趟 **40 Hz 上行 IMU err 最大 8.57°**(已是 kp 0.1)⇒ 40 Hz 上行擺盪可能仍在(10-01 兩次 + 這次),待錄製確認。

- 👁 **觀察:離牆上下拉 IMU 平衡擺盪 → `balance_imu_kp` 0.2 → 0.1**(per user「離牆上下拉 IMU 控得不好」;30 Hz 手動 hold,底 ↔ 頂 ~215 cm)
  - log(17:2x 手動多趟):上行 roll 每 2~3 tick 換號、幅度 ±0.6 → ±2.5 → **−5.25°**,trim 飽和 ±15 Hz;下行較小。增益 0.2×30 Hz = **6 Hz/度**(差速 20%/度),tick 250 ms + `roll_age` 150~250 ms(最大 464)⇒ 延遲 ~0.3~0.5 s ≈ 擺盪週期(1.5~2 s)的 1/4~1/3 ⇒ 純 P 控制在延遲下變成推擺盪(推論)。離牆 = 無牆面摩擦阻尼。
  - user 提「離牆可能前傾」:靜止 pitch ~3°(移動中 1~7.6°)、yaw 扭轉 ±4°;前傾×扭轉耦合假 roll ≈ 3°×sin4° ≈ **0.2°**;上行**前 7 s yaw 幾乎不動 roll 已 ±1.6° 擺** ⇒ 前傾**非主因**。
  - 錄製(本體 roll/pitch/yaw + 吊機 L/R,每 0.2 s;`/tmp/rec.py` 跑在吊機):
    | | kp 0.2 | **kp 0.1** |
    |---|---|---|
    | 上行 max\|roll\| / RMS | 4.78° / 1.95°(前半 1.91 → 後半 4.78,發散) | **1.63° / 0.59°**(後半 ≤0.60,收斂) |
    | 下行 max\|roll\| / RMS | 2.93° / 1.06° | 2.03°(到底前單次慢偏)/ **0.59°** |
    | 平均 roll | −0.11 / −0.12 | −0.11 / +0.17(無偏移代價) |
  - ⇒ **吊機現為 0.1 且已持久化**(`crane_settings.txt`:`set_hold_hz 30` + `set_balance_imu_kp 0.1`);改回 `set_balance_imu_kp 0.2`。各只 1 趟,待:貼牆作業是否修正不足、40/50 Hz 上行重測(10-01 40 Hz 擺盪疑同機制)、多趟後再決定是否改程式預設。長期:加 D 項(角速度阻尼)。
  - 👁 附帶:兩趟計米器 L−R 每 0.2 s 在 1 ↔ 7 cm 跳(實體不可能)⇒ 計米器讀值抖動,影響 meter 平衡來源;未查。

- 👁 **觀察:風扇(螺旋槳)電流實測**(per user 實測,10-02;duty = `pwm set` 的 %,5% = 停)
  | duty | 單顆 | 雙顆 | 雙/單 |
  |---|---|---|---|
  | 6% | 0.7 A | 1.64 A | 2.3 |
  | 7% | 3.8 A | 8.5 A | 2.2 |
  | 8% | 11.7 A | 23 A | 2.0 |
  | 9% | 24.7 A | 45 A | 1.8 |
  | 10% | 41.3 A | **49 A** | **1.2** |
  - 6~9% 雙顆約為單顆 ×2;**10% 雙顆只有 49 A(預期 ~82 A)** ⇒ 雙顆 9→10% 只多 4 A。未查原因(候選:電源限流 —— NPP-1700-48 ×2 並聯 57.6V,單顆額定約 29.5 A、合計約 59 A;或量測點/量程)。量測點(DC 輸入或其他)與當時電壓未記。
  - 對照今天 EMI 測試(雙顆):7%(8.5 A)與 8%(23 A)錯誤數同一量級(15~23 / 30 s)⇒ 匯流排錯誤**未隨電流比例增加**。
  - 參考:Mission 用 7%(FAN_ON);程式 `duty_max=10`。
  - 推算(per user「雙顆電流好像不夠」):50 Hz 下 5~10% = 1~2 ms ⇒ **10% = 全油門**;單顆全速 41.3 A ×2 ≈ 83 A ≈ 4.8 kW @57.6V > 電源 2×1700 W ≈ 3.4 kW(~59 A)⇒ **雙顆全速電源供不起**,實際上限約在 9% 附近(45 A ≈ 2.6 kW)。
    ⇒ 平常 7%(8.5 A)不受影響;但 C 組抗風「加大貼牆推力」的上限是**電源**,規格「~7 kg/軸」(全速)目前雙顆達不到。
  - 雙顆 10% 實測電壓 **56 V**(per user;僅降 1.6 V ≈ 3%,49 A 下相當於 ~33 mΩ,線路壓降即可解釋)⇒ **不像硬限流**(CC 限流時電壓應明顯下掉);功率 56×49 ≈ 2.7 kW 也低於推算上限 3.4 kW。
    ⚠️ 上限 3.4 kW / 29.5 A 是推算,**NPP 銘牌輸出電流未核對**;若銘牌 ≈ 25 A,則 49 A(各 ~24.5)仍是電源到頂。
  - ✅ **銘牌:NPP-1700-48 輸出上限 25 A**(per user)⇒ 並聯 **~50 A**;雙顆 10% 實測 49 A = **電源到頂**(兩顆均分良好才到得了 49;56 V = 剛進限流的起點)。
    ⇒ 雙顆可用上限 ≈ **9%**(45 A,90%);全油門需 ~83 A,電源只夠六成;要全推力需 ≥ 4 顆 25 A。
    ⇒ 待拍板:`duty_max` 10 → 9(避免長時間頂限流 → 電壓下掉、ESC/電源保護跳脫、貼牆中推力消失)。
  - 「調電壓能不能多拿功率」(per user 看說明書電流曲線):**現場 AC 220 V**(已滿額、無降額空間)、**ESC 標示 14S 100A** ⇒ 上限 14×4.2 = **58.8 V**,現 57.6 V 只剩 1.2 V;調到頂功率也只 +2% ⇒ **調電壓這條路不可行**。剩:加電源(全速 ~83 A ⇒ 25 A ×4,需確認 NPP 並聯台數上限與 220 V 迴路 ~7 kW)或 `duty_max` 限 9。
    📄 規格書(明緯 NPP-1700-SPEC 2025-04-11,存 `agent_ai/.tmp/npp1700/spec.pdf`):PSU 模式 48 型 DC 57.6 V、可調 42~80 V、額定 25 A(可調 12.5~25)、額定 1680 W;**輸出 ≤67.2 V 可給 100% 電流,>67.2 V 定功率**(per user 讀曲線,1680/25 = 67.2)⇒ 現 57.6×25 = 1440 W 只用到 86%。電源端確實可升壓增功率,**但卡在 14S ESC 58.8 V**(+2%);67.2 V 也只有 3.36 kW < 全速需求 ~4.8 kW。規格書**未列並聯/均流功能**(現場兩顆並聯可達 49 A,但非原廠保證;擴到 4 顆前宜詢原廠)。
  - 📄 **推力估算**(per user 問「單邊 8 kg 可能嗎」;T-Motor 官方 P60 KV170 + 22×6.6CF @48V:85% 23.2 A/1114 W/6.58 kg、**100% 34 A/1632 W/8.41 kg**;最大電流 38 A、「最大持續功率 412 W」;https://uav-cn.tmotor.com/2019/P_1108/305.html)
    - HARDWARE.md 的「~7 kg/軸」偏保守。依功率內插(推力 ∝ P^0.64):**8 kg ≈ 1.5 kW/顆**(兩顆 ~3.0 kW)。
    - 現電源(2×25 A ≈ 2.8 kW):雙顆 9% ≈ 1.27 kW/顆 → **~7.1 kg/邊**;10%(49 A,到頂)≈ 1.37 kW → **~7.5 kg/邊** ⇒ **8 kg 差 ~7%**;加第 3 顆 NPP(75 A)即足。
    - ⚠️ 57.6 V 單顆 10% 實測 41.3 A **已超馬達最大電流 38 A**;8 kg 長時間 vs「持續功率 412 W」含義待確認。
    - 皆為推算,**待實測推力**(單/雙顆 9%、10%,拉力計)。
  - 📊 **電壓 × 電流 × 推力估測表(雙顆,每顆馬達)**(推力 = 官方 48 V 數據依功率 log-log 內插;* 57.6 V 為設定值未實測)
    | duty | 電壓 | 預估需要總電流 | 總電流(實測) | 每顆電流 | 每顆功率 | 估算推力/邊 | 合計 | 備註 |
    |---|---|---|---|---|---|---|---|---|
    | 6% | 57.6* | 1.64(=實測) | 1.64 | 0.82 | 47 W | ~0.8 kg | ~1.7 | 往下外推 |
    | 7% | 57.6* | 8.5(=實測) | 8.5 | 4.25 | 245 W | ~2.4 kg | ~4.8 | 作業值;往下外推 |
    | 8% | 57.6* | 23(=實測) | 23 | 11.5 | 662 W | ~4.7 kg | ~9.3 | |
    | 9% | 57.6* | 45(=實測) | 45 | 22.5 | 1296 W | ~7.3 kg | ~14.5 | 未到頂,供應 100% |
    | 10% | 56(實測) | **75~83**(推估) | 49 | 24.5 | 1372 W | ~7.5 kg | ~15 | 電源到頂 |
    - 單顆(參考):9% 24.7 A/1423 W/~7.7 kg;10% 41.3 A/2379 W/~10.7 kg(外推、超 38 A)。
    - **10% 需求兩種推法**:單顆×2 = 82.6 A(上限);套 9% 的雙/單比 0.91 ⇒ ~75 A(兩顆同開電壓被拉低、每顆少吃;10% 時可能少更多)⇒ **未加電源前無法實測確定**。
    - 修正先前說法:**9% 電源完全供得上**(45 < 50 A),只有 10% 不足。
    - 電源規劃:2 顆 50 A ❌ / **3 顆 75 A 🟡(10% 在範圍下緣)** / 4 顆 100 A ✅。但**目標單邊 8 kg ≈ 1.5 kW/顆 ≈ 27 A ⇒ 雙顆 ~54 A,落在 9~10% 之間 ⇒ 3 顆足夠且有餘裕**;4 顆只在要雙顆長時間全油門時才需要,而全油門每顆 ~41 A 已超馬達 38 A,本就不建議。
    ⚠️ 未量:ESC 減速回灌把匯流排推高的瞬間電壓(電源供應器吸收不了;57.6 離 58.8 只 1.2 V)。
  - (原待查,①已解)① NPP 銘牌輸出電流 ② 雙顆 10% 時兩顆 NPP 各自輸出電流(是否均分)③ 56 V / 49 A 的量測點(電源端子 or ESC 輸入;總電流 or 單路)④ 兩槳氣流互擾或 ESC 限流。

- ✅ **GUI 修正:本體斷線時「拉到頂端」卡 5 分鐘**(per user 同意後修;`index.html` `v3-2026.10.02-1647`,已部署測試機)
  - 起因:12:05 換電源本體重開,user 按「拉到頂端」無反應。`crane_goto` 走本體代轉;server.js 對斷線 bridge 回 `{src:'error', line:'washrobot_not_connected'}`,但前端回覆比對只看 `src===target` ⇒ 佇列那道 `crane_goto`(逾時 300 s)不會結束,按鈕卡「移動中…」、再按跳「已經有一次 crane_goto 在進行中」。今天吊機無張力保護事件(本體 log 的 `tension_alarm kind=diff` 是 10-01 的)。
  - 修法:新增 `failInflight(target, why)`;① `src:'error'` + `<t>_not_connected` → 該 target 在途指令以 ERR 結束 ② `status` 連線狀態 up→down → 在途指令以 `ERR <t>_disconnected` 結束(washrobot/crane/arm 皆適用)。
  - 離線 harness 215 項:改後 11 輪中 4 輪有 1~2 項失敗,都在「手臂卡工具 seg」(等非同步 [M2] 行的時序);改前 4 輪全過。⚠️ 與修正的關聯未證實(記錄沒印出來)。
  - ✅ **真機驗證**(本體 `fcv-body` stop 16:47 → user Ctrl+F5 → 按「拉到頂端」):立即 🔴 `ERR washrobot_not_connected`、再按仍立即回、不再跳「進行中」(per user「正確」)。本體已重啟(body_link ~160 ms),機器未動(−66/−67)。
  - 案例 ②(送出後才斷線)只有離線,真機未測。

## 2026-10-01:測試機部署 + 吊機端上機驗證(本體離線)

- 測試機 `raspberry-cran` 開機 → 部署 `6f910ff` 吊機/server/GUI(`v3-2026.10.01-1526`)。驗證 7 項全過(表見 `changelog [2026-10-01a]`):租約斷線 1563 ms 放掉、`hold_active`、放繩 30 Hz、設定持久化、續約撐得住。
- 🔴 **抓到並修好**:19 行設定回放讓 `restart fcv-crane` 82 s 逼近 90 s 啟動逾時 → `crcmd.py` 收到回覆即走 + unit 一次送 → 21 s。測試機 unit 已換;**official unit 還沒換**(已加進 B 組)。commit `9a04e3e`。
- 斷線測試手法:Pi 端臨時 nft 表擋 laptop→:8080(使用者走 WiFi 無法拔線)。
- per user:**9/19 報告 v2 不做**(DJI 空拍素材不補看),報告定稿 v1。
- SE3 故障碼唯讀重驗:4/6 成功、履歷全 `160/OPT`(見 B 組該列,降 🟢)。
- **下午:本體開機 → 測試吊機 + 本體(兩台都走 WiFi `192.168.5.x`)**
  - 🔴 **本體還掛著 9/19 official 的 drop-in**(`~/.config/systemd/user/fcv-body.service.d/endpoints.conf` → 吊機 `.1.10`)⇒ 連不到測試吊機。
    已改名 **`endpoints.conf.official-disabled`**(unit 本身預設就是 `.5.25`)。**🔴 下次上 official 要改回來**(已寫進 B 組部署列)。
  - 部署本體 + `cycle_test.py`(兩台)。⚠️ `deploy.sh body` 只送 `app/`+`command/`,09-30 改的 `user_lib`/`transport` 沒送 → **Pi 上編譯失敗**;手動補送後成功。
    已修 `deploy.sh`:body/crane 一併同步 `user_lib/ transport/ common/`(+ crane `mechanism/`、body `main.cpp`),未 commit。
  - **驗證(全過)**:`vacuum left/right` → `ERR vacuum_group_not_independent`、`vacuum body` → `unknown_vacuum_group`(繼電器沒動)/5 個死 setting 從 `get_settings` 消失/
    `set_setting imu_ask_deg 12.5` → 重啟後仍 12.5/本體 `crane_idle_ms` ~140、峰值 260(不再無限長)、吊機 `body_link_age_ms` ~100/
    `set_imu_roll` log 每 5 s 一行(+19)/**`.21` 閘道斷電再上電 → 4.5 s 重連 → `[gw] .21 reconnected — re-running selfcheck` → `dev_qx=1`,免重啟**(09-19 的情境)。
  - ⚠️ **新發現(待拍板)**:設定自動持久化是**整組寫入**(本體 17 鍵、吊機 19 行)⇒ 檔案一旦存在,**之後改程式預設值不會生效**(檔案蓋回舊值)。
    本體測完已移除 `settings.json`(原本就沒有,檔案在 `/tmp/settings.json.test-1001`);吊機 19 行仍在。修法候選:只存與預設不同的鍵。
  - 未做:**手臂部署**(per user 先不測;重啟會 M1 回零)、**風扇順序**(per user 先不測)。
  - **手臂拆下維修**(per user):本體 `arm_attached off`、`fcv-arm` stop(⚠️ 仍 enabled、arm_attached 不持久 ⇒ 重開會恢復)。M1 原本一直 `switchControlMode FINAL failed`。
  - **`cycle_test.py` 加 `no_arm=1` / `FCV_NO_ARM=1`**(per user B 案):跳 arm_ready 檢查、每步與補清的清潔、暫停/收尾的 arm_retract,起跑送 `arm_attached off` 且 init 後不切回。
    fake_robot 先驗(指令序列無任何 arm_* 除 `arm_attached off`)→ 部署兩台。
  - ✅ **風扇順序真機通過**(`full 1 1 10 rail=off final_clean=0 no_arm=1`,FCV_DRY=1,FCV_TOP_CM=244):`pwm duty=5`(回讀✓)→ 伸腳 10 cm → 吸 3 顆(p5 −2 / p6 −46 / p7 −43 / p8 −53,每側 ≥1 ✓)→ 收腳(CH6 正壓)→ `duty=7`(回讀✓)→ 下放 10 cm → `duty=5` → 拉回頂端。roll 均 0.22°、L/R 差 1 cm、整步 14.9 s。log `~/run/logs/fan_order_1001.log`(吊機)。
    觀察:**p5 這趟沒吸上**(單次,待多趟看);`.21` QX CRC mismatch ×2(重試後寫入+回讀都成功)。
    ⚠️ ssh 直接跑 full 要自帶 `FCV_TOP_CM=<牆高>`(GUI Mission 由 server.js 帶);沒帶會落到 231。
  - 🔴 **本體繼電器 = ZS-DIO**(per user 09-23 記得是 ZS,今天實證):停本體 30 s,`.20` slave 12 FC03 → `0x0043`=**0**(PQW 站號暫存器,若是 PQW 應為 12)、`0x0032`=**12**(ZS-DIO 站號)⇒ **本體也是用 PQW 驅動在開 ZS-DIO**(同吊機 09-10 那顆)。能用是因 FC05 線圈位址重疊、回讀也對得上。換 `ZS_DIO_R_RLY` 牽涉吸盤閥/破真空閥等安全通道 ⇒ 待討論(已進 B 組)。
  - ✅ **9/16 吸附鎖真機驗證**:Ready 下手動 `vacuum feet on` + `pusher all extend_raw` → 四顆 −67~−70 → `crane_goto 240` → `ERR cups_attached sealed=4 unreadable=0`,吊機**沒收到** goto(L/R 維持 −244/−245)。
  - `attach` 4 顆第一次就全吸(p5 −62)⇒ 上一趟 p5 沒吸是**偶發**;因此 `partial_seal=` 路徑仍未被觸發(要真的有吸不上才走得到)。`detach`、收腳、關閥關幫浦正常。
  - ✅ **單側過衝實測**(機器掛繩、未吸附、頂端):`pay_out_left 5`/`retract_left 5` → 計米器 ±**6**;`pay_out_right 3`/`retract_right 3` → ±**4** ⇒ **四個方向固定 +1 cm,與距離無關**(回報 `moved=` 是停止瞬間值,之後滑行 1 cm)。
    ⚠️ **單側 5 cm ⇒ roll −6.38°**(超過 Mission 6° 中止門檻;單側張力 29.5/65.6 kg),單側 3 cm ⇒ +3.0°。
  - ✅ **下降 20 cm 張力掉 = 方向遲滯**(per user 放行 20 cm):靜止總張力 下降前 97.7 → `pay_out 20` 後 **92.6(−5.1)** → `retract 20` 後 **98.7(+6.1)**;移動中 下 min 79.1 / 上 max 119.4。
    兩次停止 roll 都在 ±1° ⇒ 不是姿態;推定滑輪/捲筒摩擦,**往下停偏輕、往上停偏重,差 ~6 kg**。雙側也過衝:放 20 → 21。腳本 `/tmp/ten20.py`(取樣 0.15 s,另開連線)。
  - 👁 **觀察:匯流排錯誤與風扇的時間關係**(今天 15:56 起的本體 log,只記現象不下結論)
    - 風扇**未轉**時:JC-100 5~8 各 3~7 次單筆 TIMEOUT(皆「連續 1 次」),其他裝置無錯。
    - 風扇轉的 5 s 內(16:30:12 duty 7 → 16:30:17 duty 5):**`.22` JC-100 5/6/7/8 TIMEOUT + slave 8 `ADDR_MISMATCH`(收 `A8`,應 `08`,單一位元)/CRC 連 3 次 → fast-fail;`.20` PQW `reply slave 44 != 12`;`.21` QX CRC ×2**。風扇停即止。
    - 即:錯誤出現在**三條匯流排同時**,且只在風扇轉時。與舊紀錄「風扇開 15× 錯誤」一致;是否為電調 EMI / 共地,未查。JC-100 slave 8 仍是錯最多的一顆。
    - 同一時段 slave 8 讀不到 ⇒ 那時 status 應會出 `p_err=`(當下沒抓 status,未驗)。
  - ✅ **本體繼電器換 `ZS_DIO_R_RLY`**(per user「換,照建議做」):
    ① 先補強 ZS 驅動(檔頭自己寫「回 production 前先補回覆驗證」):收發改 `TCP_client::sendAndReceive` 原子交易(`.20` 與 ZDT/DM2J 多執行緒共用)、FC01/02/04 回覆驗站號/FC/長度/CRC。吊機那顆一併受惠。
    ② 本體成員型別換掉,**名稱 `pqw_`/`PQW_*` 刻意保留**(同吊機 09-10);`build_body.sh` PQW→ZS。⚠️ 行為差異:`controlRelay` 現在會驗 FC06 echo、3 次失敗回 true(PQW 永遠回 false)⇒ `try_or_pause_` 呼叫端可能會停。
    ③ 真機:`[OK] relay ZS-DIO slave 12 8CH`、selfcheck 全過;CH1/2/3/6/7/8 逐一 on→回讀→off→回讀全對(**CH4 噴水泵 / CH5 滾刷沒測**:手臂拆下、管線/線可能外露);`init`→`attach`(4 顆一次吸)→`detach`→`pusher all retract`(CH6 破真空 300 ms)正常,ZSDIO 錯誤 0 筆。
    📌 PQW 驅動註解的「韌體 echo 不標準 05→00」= ZS 在回 FC05。本體已無程式用 PQW 驅動。
    📌 **`detach` 只關閥不收腳**(原設計);要收腳另送 `pusher all retract`。
  - ✅ **設定持久化改「只存與預設不同的值」**(per user OK):本體 `save_settings_file_`、吊機 `persist_crane_settings` 逐鍵比對編譯預設,全預設 ⇒ 刪檔(沒檔 = 預設)。
    真機:吊機部署後回放舊 19 行 → 全是預設 → 檔案自動消失;`set_balance_kp 1.5` → 檔只剩那行、重啟仍 1.5 → 改回 1.0 → 檔消失;本體 `imu_ask_deg 12.5` → 1 行 → 改回 45 → 檔消失。兩台現況:**無設定檔(全預設)**。
  - 👁 **觀察:推桿左右阻力**(今天 log `[wait_many ZDT:N] done at`,伸出 n=4 / 收回 n=3 每支,含風扇測試、attach ×2、手動伸):
    伸出 時間/峰值 右上 1575/421、右下 1462/587、左上 1500/664、左下 1688/523(右均 1519 ms/504 mA,左均 1594/594);收回 左下 759 mA 最高、其餘 ~500。
    ⇒ 9/2 的「右腳 1.4~1.6× 時間、+20% 電流」**今天未重現**(左略高)。條件不同:樣本少(9/2 每支 50)、推桿 rpm 900→400、頂端單一位置;每支各有一次低電流(未觸牆)。
  - 👁 **觀察:風扇 × 匯流排錯誤的對照與劑量測試**(本體 idle、未吸附、掛繩;風扇 CH1;每段 30 s,同時每 0.5 s 打 `relay_status`(.20)/`pwm status`(.21)/`water_level`(.22),JC-100 輪詢照常)
    | 風扇 | 段 1 | 段 2 | 平均驅動層錯誤/30 s | 應用層指令失敗 |
    |---|---|---|---|---|
    | 5%(停) | 0/0/0(×3 段)+ 最後一段 1 | | **0** | 0 |
    | 7% | 5 | 9 | **7** | 1(`relay_status`)|
    | 8% | 16 | 14 | **15** | **7**(全是 `relay_status`,約讀取次數一半)|
    - 分布:7% 以 `.21` QX、`.22` XKC 為主,`.20` ZS-DIO 偶發;8% 起 `.20` ZS-DIO 每段 4、`.22` JC-100 6/7/8 也出現。風扇停 → 立即歸零。
    - ⇒ 風扇開關兩輪完全重現、錯誤隨轉速上升、三條匯流排同時受影響。電調通電但馬達不轉(5%)= 0 錯。
    - ⚠️ 8% 時繼電器**回讀**約半數讀不到;`pqw_set_relay_verified_` 讀不到時是 best-effort 放行 ⇒ 那時的「寫後回讀確認」等於失效(寫入本身有 FC06 echo 比對)。
    - 未查:干擾路徑(電調/馬達輻射、共地、電源)。
  - 👁 **觀察:30/40/50 Hz 全程上下行速度**(per user;10↔234 cm,`retract 224`/`pay_out 224`,每 0.15 s 取樣,等速段取 40~200 cm 線性回歸;起點是 GUI `goto 0` 後先 `retract 10`)
    | Hz | 上行等速 | 下行等速 | 上行 224 cm 用時 | 總張力 上/下 | \|roll\|max 上/下 | \|L−R\|max |
    |---|---|---|---|---|---|---|
    | 30 | 15.7 cm/s | 16.5 | 17.2 s | 98~120 / 83~99 kg | 1.94° / 2.04° | 4~5 |
    | 40 | 21.5 | 16.6 | 14.5 s | 88~118 / 82~100 | **7.00°** / 2.49° | 6~7 |
    | 50 | 25.5 | 16.6 | 11.8 s | 94~124 / 80~101 | 2.66° / 1.62° | 5~6 |
    - 上行 ≈ 0.52 cm/s/Hz(50 Hz 略低於線性);**下行三者皆 ~16.5 ⇒ `PAY_OUT_MAX_HZ=30` 真機生效**。
    - ⚠️ **40 Hz 上行左右擺盪**:L−R 在 −7~+7 cm 來回、roll +2.5 / −4.4° 交替、週期 ~1.5–2 s(最大 7°);30 Hz 無、50 Hz 只 2 筆 >2°。像平衡迴路與擺盪在 40 Hz 耦合,**單次**,未重跑。
    - 上行總張力峰值 118~124 kg 三者相近;下行最低 ~80(與方向遲滯一致)。每趟淨下沉 ~1 cm(10→9→8)。結束 `motion_hz` 30、停 8 cm。資料 `/tmp/speed.json`(吊機)。
  - 👁 **觀察:下行 40/50 Hz**(per user「下行 我想試試 40 50」;為此加 `set_pay_out_max_hz <1..50>` 執行期覆寫,**不持久化、重啟回 30**,測完已改回 30)
    | 下行 | 等速 | 總張力 min~max | \|roll\|max | \|L−R\|max | 終點(目標 15) |
    |---|---|---|---|---|---|
    | 30 Hz(前一輪) | 16.5 cm/s | 83~99 kg | 2.04° | 5 | — |
    | 40 Hz | 22.2 | 75~104 | 3.76° | 9 | 14 |
    | 50 Hz | 27.7 | **70**~107 | **6.26°** | **10** | 14 |
    - 下行 ≈ 0.55 cm/s/Hz;速度越快張力谷越低(83→75→70)。**50 Hz:L−R 達 10 cm(=`length_diff_max_cm` 10,未觸發但零餘裕)、roll 6.26° > Mission 6°**;40 Hz 尚可。
    - 對照 30 Hz 上行 ×2:15.7/15.8 cm/s、roll ≤1.6°(穩)。⇒「下行最多 30 Hz」的規則有實測支撐。結束停 14 cm、`motion_hz` 30、`pay_out_max_hz` 30。資料 `/tmp/speed_down.json`(吊機)。
  - 👁 **觀察:30/40/50 Hz 上下行各再跑一次**(底 ↔ 頂 ~234 cm;合計 16 趟,原始資料 `agent_ai/.tmp/1001speed/*.json`)
    | 方向 | Hz | m/s | 50 m 用時(等速換算)| 最大 \|roll\| | roll RMS |
    |---|---|---|---|---|---|
    | 上 | 30 | 0.157 | 5′18″ | 2.75° | 0.4–0.5° |
    | 上 | 40 | 0.214 | 3′54″ | 7.00° | **2.6–2.7°(兩次都擺盪 ⇒ 重現)** |
    | 上 | 50 | 0.254 | 3′17″ | 3.94° | 1.0–1.2° |
    | 下 | 30 | 0.166 | 5′01″ | 2.49° | 0.4–1.3° |
    | 下 | 40 | 0.223 | 3′44″ | 4.53° | 0.7–2.0° |
    | 下 | 50 | 0.275 | 3′02″ | 6.26°(起步單筆)| 0.85–0.9° |
    - roll 平均偏移 16 趟皆 −0.26~+0.35°(上行略左低、下行略右低);差別在擺動幅度。**30 Hz 最穩**。方向:+ = 右低、− = 左低(由單側測試確認)。
    - ⚠️ 50 m 一次跑完會超過 `MOTION_TIMEOUT_MS`=120 s ⇒ 要分段或放寬。
    - ⚠️ **左右差到 13~16 cm 都沒觸發 `length_diff_max_cm`=10**(起點帶 5~7 cm 偏差;偏差來自 GUI 單側 ▲▼ 後 `goto 0`)⇒ 門檻比的是什麼待查(見 A 組)。
    - 結束:機器停在底部 ~−1 cm,**左計米器 +5(在 0 點下方)/ 右 −4,左右差 9 cm**,下行張力谷 66 kg ⇒ 下次動前先拉高 + 對齊。PDF `agent_ai/.tmp/1001speed/吊機速度測試_20261001.pdf`(只含第一輪 10 趟)。
  - 現場:per user 機器已掛繩貼牆、設好最高最低 → 檢查 OK:地面歸零 16:18:50、牆高 **244**(原 231)、目前離地 ~35 cm、L/R 差 1 cm、張力 46+50 ≈ 96 kg、本體未吸附、roll −0.73°。
- **收尾**:全部 push(`ff4fb32`,本地 = origin/main)。測試機現況:新版吊機/GUI 在跑、`hold_hz` 10、`balance_kp` 1.0、無殘留 nft 表與暫存腳本。下一步要本體開機(驗本體半邊)或上 official(記得換 unit)。

## 2026-09-30:待辦 A 組一次清(離線,commit `505a422`,未上機)

- **收尾**:C 組 `DEPLOY_F` 力道 per user 定 **3**(`6f910ff`,手臂 header 5→3);**10-01 push 到 GitHub**(`625174b..6f910ff`,含先前累積共 27 個 commit,本地與 origin/main 一致)。C 組只剩抗風、清潔部件 × 直料兩項,都等現場資料。

per user「滑台橫走時平衡迴路不跑 可以吸盤有做動 還需要嗎,其他的全部都做」。細節 `changelog [2026-09-30a]`。

- **#3 不做**:滑台只在吸附後掃,吸附中不該動繩 ⇒ per user 判斷成立。
- **兩個 🔴**:#2 用 **hold 租約**解(1.5 s 沒 `hold_renew` 吊機自己停);#1 照 09-09 定的選項 2 做**只觀測**的 `body_link_age_ms` + EVT,全域 watchdog 不動(門檻要先量)。
- 🟡/🟢 其餘 13 項全做(清單見總表清帳對照)。兩個語意決定寫在程式註解裡:pay_out 30 Hz 夾**基準頻率不夾平衡修正**;吊機持久化**不存模式開關與 motion_hz**。
- **驗證**:本機 x86 兩支 binary 編譯連結 ✅;吊機 binary 跑 fake_bus 實測租約/續約/`hold_active`/30 Hz/鏈路年齡/設定檔全部 ✅;GUI harness 212 → **215 條**(新增 3 條 hold 租約),最終版連跑 7 次:6 次全綠、1 次紅在手臂卡工具顯示(開機工具「轉換中」,本次未動該段 ⇒ 既有時序不穩,未處理)。續約第一版走前端佇列,harness 量到按住中租約過期 → 改成 ws `{holdRenew:1}` 由 server.js 代送、回覆後端吃掉。**只有編譯、沒跑過的**:本體閘道重連 selfcheck、rail 重試、`settings.json` 自動存 ⇒ 列入 B 組上機驗證。
- ⚠️ 踩坑:`pgrep -f "fake_bus.py --port 150"` 又比對到自己的 shell(exit 144)—— `pgrep -f` 跟 `pkill -f` 同一個坑,改 `pgrep -x python3` + 讀 `/proc/<pid>/cmdline` 篩。

## 2026-09-23:待辦總表清帳重建 + work_log 壓縮 + 風扇順序改回原版(離線)

per user「先做 A,兩個死碼函式清除,手臂 INIT 結束位置改成滾筒,4 顆有 2 顆吸住改每側各 ≥1,風扇順序改回原版」。

- **四件指定改動,查證後三件早已完成**(舊總表沒跟上):兩個死碼函式 09-16 `44bcb13` 已刪(只剩一段孤兒註解,這次清掉 `WASH_ROBOT.h`)、
  手臂 INIT 09-16 已改停滾筒、`group_seal_ok_` 08-31 已改每側各 ≥1。**真正要改的只有風扇**:`cycle_test.py` 步內 + 最低點補清兩處
  改回「① 關風扇 → ② 開閥 → ③ 伸腳」與「④ 收腳 → ⑤ 開風扇」(09-15「先吸附再關/先開再收」實測更吵、無改善)。py_compile 綠,**未上機**。
- **待辦總表清帳**:舊表 168 列 + 附錄逐列對原始碼判定 → **舊表原封歸檔** `.claude/archive/todo-table-2026-08-27_to_09-23.md`,
  新表只列仍開放的,依「要什麼才能動」分 A 離線/B 上機/C 拍板/D 暫停,附清帳對照(已解/作廢各附理由)。依 CLAUDE.md 移到**檔首**。
  順帶確認:吊機端補水閥 deadman 09-10 就有(300 s 強制關);official 顯示 `balance_source=meter` 是 GUI 手動切的,不是預設壞了。
- **work_log 壓縮** 1,365 → ~660 行:09-16~09-18 壓成「歷史摘要 #16」;#2 的「本段仍開放」改指標。
- **9/19 戶外實測報告**(`.claude/reports/2026-09-19_official_field_test.md`,草稿 v2):user 口述 + 竹北 ERA5 風速(平均 7、陣風 16 m/s)+ Claude 分析。三議題進總表:抗風/低樓層貼牆(B 量測 + C 方向)、清潔部件 × 垂直窗框(C)、計米器校正(B 升 🔴);WiFi 實測正常 → RF 降 🟡。
- **9/19 實測正式報告 v1 已交付**(per user「放進給老闆資料夾 PDF+html」,讀者老闆 + 團隊):
  `G:\共用雲端硬碟\Facade_Cleaning_Robot_Report\給老闆\20260919_正式吊機戶外實測報告.{pdf,html}`(8 頁 A4;HTML 單檔圖片內嵌)。
  結構:一頁摘要(七項燈號 + 結論 + 三項待決策)→ 概要 / 通訊平衡 / 抗風(風速表、影片截圖對照、吊點幾何圖、W/L 與三受風面計算、導引繩隨高度失效)/
  低樓層吸附 / 清潔部件 × 直料(A–D)/ 計米器 / 下一步 → 附錄(假設值、資料來源、未納入)。產生器與圖片在 `agent_ai/projects/facade_cleaning_v2/.claude/scratch/0919report/`(`build.py`,Chrome headless 印 PDF)。
  素材:手機照片 25 + 影片 32(每支抽 6 格);**DJI 空拍 6 支 + 8 張 Drive 尚未下載、未看**;9/15「倉庫完整刷洗影片」不屬本次。
  🔴 **user 更正**:地面導引繩「效果不大、高一點就拉不到」—— 我先前依靜態畫面寫「明顯穩定」是誤判,報告/待辦已改。
  ⚠️ 踩坑:① Google Drive 串流模式下檔案**讀出來全是 0**(robocopy 照樣「成功」複製出空殼)→ 要先設「可離線使用」,且複製前驗中段/尾段非 0;
  ② PowerShell 5 讀無 BOM 的 UTF-8 `.ps1` 中文路徑變亂碼 → 加 BOM;③ WSL `/mnt/facade_report`(fstab drvfs 掛 G:)失效,改走 Windows PowerShell;
  ④ WSL 無 ffmpeg → `pip install --user imageio-ffmpeg pillow-heif`;整支解碼 4K HEVC 抽格太慢 → 改 `-ss` 跳點抓單格(20 s/支);⑤ `pkill -f` 又把自己的 shell 殺掉(exit 144)→ 用 PID。
- ⚠️ 歷史摘要已 **16 段**(超過 10 段提醒門檻),最舊的 #3~#7(2026-04/05)可考慮移到 archive。

## 2026-09-21:程式/文件離線日 —— deploy.sh 認 official、GUI 畫 meter_suspect、goto 張力軟停講清楚、rail 拒絕帶原因、NTP 探測腳本

per user「24V 治本已經解決,隧道延遲暫時用 WiFi 之後會再改,程式/文件(離線可做)開始」。機器全部離線,**全部未在真機驗證**,下次上機先 `FCV_TARGET=official ./scripts/deploy.sh prep-official`。

### 已完成(commit `3e60c7d`)
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
- ✅ 09-23 已壓縮(見 09-23 條)。

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

### 歷史摘要 #16(2026-09-16 ~ 2026-09-18)— systemd 自啟、狀態機 11→6、`crane_cmd_` 不重送、手臂原則、共用值持久化、**official-crane 從零上線 + 一顆 binary 兩台共用**

> 2026-09-23 壓縮。原文 ~430 行 → 本段;逐版明細在 `changelog.md`(`[2026-09-16*]`~`[2026-09-18a]`)、指令在 git(`44bcb13`/`2087a2e`/`a0ed84f`)。
> 只留**決策(含否決)/伏筆/踩坑**;仍開放的待辦已全數搬進檔首總表,這裡不重列。

**決策(per user)**
- **五支程式 systemd 開機自啟**(吊機 system unit;本體 user unit + `loginctl enable-linger`,因本體 sudo 在使用者那本)。v1 GUI 退役,v3 接手 `:8080`。
- **本體狀態機 11 → 6**(`Idle/Ready/Attached/Running/Paused/Error` + 正交的 `pause_reason`、`flow`);`returning_home_` 用 RAII 清(20+ return 點)。
- **開機盤點**:`init()` 讀四顆壓力,吸附中或全讀不到 ⇒ 跑 `emergency_detach_()`;全成功才 `Idle`,否則留 `Error`。⚠️ 代價已知:上電＝鬆手。
- **中止 ≠ 急停**:「停止作業」收尾自動收腳 + 關幫浦(機器純吊繩);「緊急脫離」紅色隔開進 Error。**要原位續跑用「暫停」**(保留吸附)。
- 本體 `crane_goto` 加**吸附硬守衛**(fresh read,讀不到也擋;`force` 旁路留 EVT);▲▼ 直連吊機擋不到 → GUI 是唯一防線(09-19 改「只有密封才鎖」)。
- **手臂原則**:上電就緒＝滾筒槽 + 使能,整個上電期間**不失能**(除非校正);急停只拉回滾筒;INIT 待命位 CENTER → **RIGHT(滾筒)**;STARTUP 到槽後等 2 s 建力再 INIT;
  壓上力道防呆 **≤ 7 N·m**(本體 + 手臂兩層);距離上限**超範圍要拒絕、不是夾到上限**。刮刀做完回滾筒。
- **Manual 與 Mission 共用同一份機器狀態**,Mission 起跑強制安全項(hold_guard/level_auto/crane_attached/zdt_power)。**否決**「Mission 自己一份參數」。
  推桿 RPM、五個張力門檻**持久化**(`~/run/{body,crane}_settings.txt`,程式 `set_*` 時寫,unit `ExecStartPost` 回放);hold_guard/level_auto **刻意不持久化**。
- `up_stop_total_kg` 併入單側 `retract_tension_stop_kg`(自動收繩軟停 + ▲ 手拉停)。吊機警報在沒流程跑時**不再把 Idle 拖進 paused**。
- 第二顆水位計(高水位,`.22` slave 14):到高水位本體自動關閥;水閥兩段時序(偵測到水 20 s 開洗、180 s 關)。
- **兩套環境**:測試 `raspberry-cran` / 正式 `official-crane`,**程式共用一份**(否決維護兩套);official 佈線刻意不同 ⇒ 裝置↔匯流排↔站號 **env 驅動**,預設＝測試機(逐位元不變),
  official 靠 systemd drop-in(副本 `scripts/systemd/official/`,裝置表 `config/official-crane_485.md`)。本體↔吊機隧道改 WiFi AP(QWRT)。

**🔴 事故**:09-16 傍晚吊機斷電維修、上電後**自己放繩**(−195 → −147)——本體 `crane_cmd_("goto 0")` recv timeout 後「強制重連 + 重送同一條」。
已修:recv timeout 直接放棄,只有「一個 byte 都沒送出」才重試。通則:**通道自癒只能重連,不能重送**;斷電維修前先確認本體沒卡在等吊機回覆(最乾淨:本體一起斷電)。
同型掃描全通道只有這一條。

**踩坑(新增到 pitfalls 的已標)**
- systemd:stdin 預設 `/dev/null` ⇒ console 讀到 EOF 退出 → `exec 3<> fifo`;unit 檔裡 `%s` 是 specifier;`fcv-body` 停止走 SIGTERM 不送 `exit`(會關繼電器)。
- `deploy.sh` 自己的四個坑:awk 空欄字串比較、反引號被執行、只看 md5 會把編譯失敗當成功、arm 只同步兩個檔。`~/run/build_*.sh` 是 Pi 副本不跟 repo、`.o` 數寫死。
- 同一個限制兩層(rail 指令層/驅動層,pitfalls §2.6)。急停收腳「同時到位、位置沒變、電流很低」＝同步廣播掉包不是堵轉 → 重送一次。
- GUI:`hidden` 被 class display 蓋掉;`.grp overflow:hidden` + nowrap 把按鈕擠出卡片;手臂 STATUS `[M1]`/`[M2]` 是兩則訊息;驗收腳本每節要自己重設狀態;fake `Lock` 重入死鎖 → RLock。
- 第二顆 XKC:出廠 9600 在 115200 匯流排隱形,改位址後要隔 ≥1 s 才寫鮑率。
- **official 建置**:USR `port.cgi` 少帶欄位回 200 但不生效、要 `manage.cgi?reset=1`;SE3 改序列參數要斷電重開(先亂碼再靜音)、`P.79` 3→2→目標;
  ZS-DIO 被改成 9600(掃出來的);X518 只允許一條 TCP;**official 這顆 X518 方向相反(scale 正值)**;`set_dsz_scale` 不持久 → env;
  SD76 與 ZS-DIO 不可共線(ZS-DIO 獨立 `.35`);psudo 用 `repr()` 把換行變字面 `\n`、sudo 快取時密碼被當指令印出(→ `-k`)。

**伏筆 / 基準值**
- M2 hold 扭矩在滾筒待命位 2 分鐘內爬到 **≈1.6 N·m 飽和**＝今天的基準,>2.5 才是傳動異常訊號。
- 工具重心對 M1:刮刀那側重力矩比滾筒小 ~0.5 N·m(~10%),量得到但不值得分工具補(真要補:刮刀目標 +0.5)。
- SE3 參數:Modbus 位址 = P 編號;兩台只有 P.2 下限頻率真的不同;`scripts/vfd_se3_clone.py` + `config/vfd/` dump 可複製到新機。
- 右計米器跨斷電跳 ~15 cm;吊機 Pi 曾兩次上電沒自己起來(之後未再現)。
- 舊日誌/交接單裡 `Linux_test/cycle_test.py` 字樣刻意不改(歷史事實);現在在 `scripts/cycle_test.py`。

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

#### 本段仍開放的待辦
- ➡️ 2026-09-23 清帳時已全數併入檔首總表(或判定已解/作廢,見總表「清帳對照」)。原文在 git `3e60c7d` 之前的本檔。

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
