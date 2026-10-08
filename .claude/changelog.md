# changelog — 變更帳本

> **規則(2026-09-12 修訂,見 CLAUDE.md)**:append-only、不壓縮、按月**輪替**——主檔留當月全文,
> 更早月份全文原封搬 `archive/changelog-YYYY-MM.md`。下方索引列出**全部**條目與所在檔。
> **每條格式(2026-09-12 起瘦身)**:「改了什麼」3–5 行 + commit hash(diff 在 git,不重抄);
> 保留「原因 / 決策(尤其被否決的)/ 驗證結論」。原則 ≤40 行,驗證表可超。
> (原 2026-04 的「# 修改日誌」格式範本已由本說明取代。)

## 索引(全部 168 條,新的在上)

- `[2026-10-08c]` **中繩連動:放繩多放 % + 連動方式開關**(`set_middle_pay_extra`、`set_middle_link_track`,皆持久化):放繩時中繩追 k×(1+%),收繩不加;「只照速度」不讀中計米器 ⇒ 中計米器失效時鋼索不再被連動擋死;harness 287(+9)(**未部署**,機器關機中)→ (本檔)
- `[2026-10-08b]` **中繩 CLV900 程式支援**:config-driven 啟用(`FCV_CLV900_ENABLE`/`FCV_EP_USR_MW_HOST`/`FCV_CLV900_SLAVE`)、`middle_hold` 按住租約、自動跟隨(P 追蹤 + lag/反向/故障中止,含兩側 ▲▼)、GUI 中繩卡 + 速度快選 + Setting 三項;RECOVER 說明 9→4 顆;Manual「連動鋼索 ▲▼」勾選(`set_middle_link`,不存檔)(`v3-2026.10.08-2205`,兩台)→ (本檔)
- `[2026-10-08a]` official 吊機半邊部署最新 main(新 `fcv-crane.service`/`crcmd.py`/GUI `cam.conf`)+ `deploy.sh` official 位址可覆蓋;USR 接線錯置以**改網關 IP**修正(`.30`↔`.34`)→ (本檔)
- `[2026-10-07c]` 攝影機串流優化:**H.264 直通 fMP4 → `<video>`+MSE**(MJPEG 退為備援)、`cam_mjpeg.py`→`cam_relay.py`;攝影機多餘功能全關、GOP 1 s(`v3-2026.10.07-2126`)→ (本檔)
- `[2026-10-07b]` 風扇 `duty_max` 10→9 部署(本體/腳本/GUI/server.js 四處一致);`deploy.sh` 測試機吊機位址可 `FCV_CRANE` 覆蓋(DHCP .25→.31)→ (本檔)
- `[2026-10-07a]` **Dashboard 兩支攝影機**:本體 `fcv-cam`(`scripts/cam_mjpeg.py` :8091,RTSP→MJPEG、有人看才跑)+ GUI 攝影機卡 + `server.js` `{src:'cam'}` + `deploy.sh cam` + 機身 OSD 關閉(`scripts/xm_ipcam.py`)→ (本檔)
- `[2026-10-06a]` 上下行照使用者選的速度跑:`PAY_OUT_MAX_HZ` 30→50、hold ▼ 不再夾、`set_hold_hz`/`set_motion_hz` 上限 120→50 + 10-03 文件校正(`v3-2026.10.06-1025`)→ (本檔)
- `[2026-10-02a]` GUI:本體斷線時在途指令立即以 ERR 結束(`failInflight`,「拉到頂端」不再卡 5 分鐘)+ 風扇 EMI/電源觀察(`v3-2026.10.02-1647`)→ (本檔)
- `[2026-10-01f]` 吊機 `set_pay_out_max_hz <1..50>`(放繩上限執行期覆寫,不持久化);30/40/50 Hz 上下行速度實測 → (本檔)
- `[2026-10-01e]` 設定持久化只存與預設不同的值(本體 settings.json / 吊機 crane_settings.txt;全預設刪檔)→ (本檔)
- `[2026-10-01d]` 本體繼電器換 `ZS_DIO_R_RLY`(實證是 ZS-DIO)+ ZS 驅動補強(原子交易 + 回覆驗證)→ (本檔)
- `[2026-10-01c]` `cycle_test.py` 無手臂模式 `no_arm=1`/`FCV_NO_ARM=1`;風扇順序真機通過 → (本檔)
- `[2026-10-01b]` `deploy.sh` body/crane 補同步 `user_lib/ transport/ common/`(本體部署因只送 app/ 而編譯失敗);本體半邊上機驗證 → (本檔)
- `[2026-10-01a]` 測試機部署 + 上機驗證 hold 租約/30 Hz/hold_active/設定持久化;🔴 修 `crcmd.py` 每條固定等 2 s ⇒ ExecStartPost 19 行回放 82 s 逼近 90 s 啟動逾時 → 21 s → (本檔)
- `[2026-09-30b]` `DEPLOY_F` 工作力道定案 3:手臂 header `DEPLOY_F_TARGET_NM` 5→3,與腳本/GUI 一致 → (本檔)
- `[2026-09-30a]` 待辦 A 組 15 項(#3 滑台平衡經確認不需要):**hold 租約 1.5 s + `hold_renew`**、吊機本體鏈路年齡 `body_link_age_ms`(只觀測)、hold 中擋 motion(`ERR hold_active`)、**pay_out 30 Hz 上限**、本體閘道重連自動 selfcheck、rail 送出重試、ZDT `pulse<0`、`vacuum left/right` 拒絕、5 個死 setting 移除、設定自動持久化(本體 `settings.json` / 吊機 `crane_settings.txt` 擴充)、本體 watchdog 閒置改看 IMU 回覆、include guard、過期註解、`set_imu_roll` log 節流、`QX_DO24::init` 對齊 → (本檔)
- `[2026-09-23a]` `cycle_test.py` 風扇順序改回原版(伸腳前關、收腳後開)+ 刪 `WASH_ROBOT.h` 孤兒註解;待辦總表清帳重建(舊表歸檔)→ (本檔)
- `[2026-09-21a]` 離線日:`deploy.sh` `FCV_TARGET=official` + `prep-official`、GUI 畫 `meter_suspect`、`goto` 張力軟停 `stopped=tension_stop short_by=`(OK 前綴保留)+ GUI ⚠️、`DM2J::last_error()` → rail 拒絕帶原因、`ntp_probe.sh` → (本檔)
- `[2026-09-19a]` **official 上線第二天**:開機競態 `ExecStartPre` 閘門、計米器凍結 RESYNC + 平衡不吃可疑計米、本體↔吊機 WiFi 橋(QWRT)通了 + 兩端 endpoint drop-in、web 橋 dead-peer 看門狗、吸附鎖只認密封(`v3-2026.09.19-1548`)→ (本檔)
- `[2026-09-18a]` **裝置對應 config-driven**:計米器可拆兩條匯流排(`cli_M2`)、slave/水閥通道/張力 scale 全部 env 可覆蓋;GUI 水閥通道改由後端動態判定 ⇒ **測試機與正式機共用同一份原始碼**(`v3-2026.09.18-2326`)→ (本檔)
- `[2026-09-17v3o]` v3：張力條「總和」給一條 bar——刻度＝參考 2×單側上限、中性色、不是門檻（`v3-2026.09.17-1842`）→ (本檔)
- `[2026-09-17v3n]` v3：Manual 本體區推桿卡墊底＋卡內兩欄、手臂卡工具切換（seg 亮＝STATUS tool=）＋全 stack 重排、張力條加「總和」讀值；fake RLock（`v3-2026.09.17-1752`）→ (本檔)
- `[2026-09-17v3m]` v3：張力保護卡第四個門檻「左右繩長差上限（cm）」`set_length_diff_max_cm`（`v3-2026.09.17-1606`）→ (本檔)
- `[2026-09-17v3l]` v3：第二顆水位計（高水位）——Dashboard／前置 ③ 兩顆各一燈、補水改到高水位自動關閥、吃 `EVT water_inlet_auto_close`（`v3-2026.09.17-1549`）→ (本檔)
- `[2026-09-17v3k]` v3（本地，未部署——Pi 斷電維修）：張力卡即時值三格撤掉、左右繩差上限標籤；Mission 狀態列狀態字移除；fake `set_tension_*` 反映到 status（`v3-2026.09.17-1448`）→ (本檔)
- `[2026-09-17v3j]` v3：上滑台可設負值（−130..130）；張力保護卡門檻旁顯示即時值（左/右/上限、左右差/門檻、最大側/收繩上限）（`v3-2026.09.17-1422`）→ (本檔)
- `[2026-09-17v3i]` v3：`up_stop_total_kg` 退場（GUI 12 處）、收繩張力上限標籤講清楚、吃 `EVT tension_retract_stop`；Manual 本體區依操作順序重排（`v3-2026.09.17-1406`）→ (本檔)
- `[2026-09-17v3h]` v3 現場修正：手臂卡結果文字擠掉「收回」（安全）→ 鈕獨立一列；Manual 本體開關機卡移除，本體 state 搬 Mission 狀態列（`v3-2026.09.17-1339`）→ (本檔)
- `[2026-09-17v3g]` v3：Manual 手臂卡重做——力道/距離上限、壓上/收回、目前力+估測牆距、力道→套用（`arm_deploy_f`/`arm_force`/`arm_retract`，≤7 N·m 防呆）（`v3-2026.09.17-1211`）→ (本檔)
- `[2026-09-17v3f]` v3：「任務結束回頂端」勾（return_top）、執行中緊急脫離閃爍、Dashboard Mission 卡（三顆鏡像鈕 + 目前參數）（`v3-2026.09.17-1123`）→ (本檔)
- `[2026-09-17v3e]` v3 共用值：推桿 RPM 改 `set_pusher_rpm` 共用值（指令不帶 rpm）、Mission「共用值」卡（zdt_skip 標黃）、腳本拒跑行醒目（`v3-2026.09.17-1058`）→ (本檔)
- `[2026-09-17v3d]` v3 現場修正：推桿單支區每支一列（原 11 欄 grid 被卡片裁掉 ⇒ 按不到）、參數卡五列對齊、前置移除 推桿歸零／手臂 兩列（`v3-2026.09.17-1037`）→ (本檔)
- `[2026-09-17v3c]` v3：Manual 吸盤推桿卡整理——RPM 顯示預設值、失能/使能（zdt_power）、整組包含勾（zdt_skip）、歸零兩顆改名（`v3-2026.09.17-1011`）→ (本檔)
- `[2026-09-17v3b]` v3：側欄 Dashboard 優先、Mission 照草圖重排（參數卡五列 + 右欄三顆鈕）、四卡移除、兩卡搬 Dashboard、存檔鈕（`v3-2026.09.17-0959`）→ (本檔)
- `[2026-09-17v3]` v3：`cycle_test.py` 路徑跟著搬到 `scripts/`（顯示字串 + 2 處註解，`v3-2026.09.17-0934`）→ (本檔)
- `[2026-09-16v3c]` v3：v1 退役收尾 —— 頂欄「v2」連結拿掉、check_console ② 4 個寫入點清零（`v3-2026.09.16-1948`，:8080）→ (本檔)
- `[2026-09-16v3]` v3：本體狀態機收斂成 6 個 —— `WR_MOVING` 與暫停原因顯示對齊 → (本檔)
- `[2026-09-15v3b]` v3：01 作業流程併入 Mission、啟動改走 server.js 起 cycle_test、危險閘門取消、通訊紀錄移除 → (本檔)
- `[2026-09-15v3]` v3：Manual 張力保護 啟用/關閉鈕（吊機 `set_hold_guard`）+ Mission `fan=`/`rail=` 起跑參數 → (本檔)
- `[2026-09-13v3]` console v3（`public_v3/`）：作業流程頁 + `mission` 指令族（EVT 驅動）+ SAFE + 假機器離線驗收 → (本檔)
- `[2026-09-10a6]` per user：吊機水閥**拿掉確認視窗**，按了就執行 → (本檔)
- `[2026-09-10a5]` `paintRelay` 同型修正 + 確認彈窗不再預支未部署的行為 + 10 處過時註解 → (本檔)
- `[2026-09-10a4]` 上一條的三個修正 —— 降級狀態要有回頭路，而「最顯眼的元件」也會說謊 → (本檔)
- `[2026-09-10a4]` 🔴🔴 吊機那顆繼電器**三個月來被當成錯的型號在驅動** —— ZS-DIO 誤用 PQW driver → (本檔)
- `[2026-09-10a3]` console v2 新增「吊機繼電器 ZS-DIO CH1–4」面板 —— 🔴 送出走本體、回讀走吊機 → (本檔)
- `[2026-09-10 診斷]` 吊機 PQW（水閥繼電器）**電氣上不在匯流排上** —— 兩種鮑率、255 個位址全掃 → (本檔)
- `[2026-09-10a2]` `PQW_TOTAL_CH` 16 → 8 —— 把「偽造的確認」降級成「留下證據的沉默」 → (本檔)
- `[2026-09-10 踩坑]` 🔴 我昨天刪舊樹刪掉了 WEB 的 `node_modules` 目標，而**驗收做在刪除之前** → (本檔)
- `[2026-09-10 實測]` ✅ SD76 計米器的零點**撐得過整台斷電** → (本檔)
- `[2026-09-10a1]` 水閥「先武裝後送」＋ 獨立逾時 —— 開閥送到了但回覆丟了，不再沒人看管 → (本檔)
- `[2026-09-09 實測]` 🔴 PQW 繼電器模組**會替不存在的通道編造回覆** —— `CH_BRUSH=15` 潛伏五週的機制找到了 → (本檔)
- `[2026-09-09 文件更正]` 🔴 PWM 那一路是**貼牆螺旋槳不是散熱風扇** + 兩份手冊摘要的 8N1 記載已過期 → (本檔)
- `[2026-09-09 實測]` 第二輪 10 週期：**漂移趨勢重現了**，但「漂的是什麼」仍未解 → (本檔)
- `[2026-09-09 更正]` 🔴 上一條的「計米器在漂、IMU 是必要項」**沒有排除相反的可能** → (本檔)
- `[2026-09-09 實測]` 🏆 10 週期全數完成，左右差是專案至今最好 —— 但量到 roll 會**隨週期累積漂移** → (本檔)
- `[2026-09-09m7]` 🔴 急停通道：ACK 被排隊的 EVT 蓋掉 + 接收緩衝區只漲不消 → (本檔)
- `[2026-09-09c5]` 🗂️ 兩台 Pi 路徑大搬家：`~/bringup` → `~/projects/facade_cleaning_v2` + `~/run` → (本檔)
- `[2026-09-09c4]` 新基準下的兩趟 1 週期 + 🔴 發現急停旁路通道未連線 → (本檔)
- `[2026-09-09c3]` ✅ 重新定義工作區基準：地面起點＝玻璃最底點，`level_diff` 歸一到 0 → (本檔)
- `[2026-09-09c2]` 🔴 `fine_adjust` 的 overrun pass 漏套 `level_diff` —— `level_diff` 設多少都只到手 2cm → (本檔)
- `[2026-09-09 更正]` 🔴 上一條的因果結論**被第四輪推翻** —— 是我拿「重現」當「因果」 → (本檔)
- `[2026-09-09]` 切 192.168.1 有線鏈路：IMU 掉 1/3，但 `level_diff` 撐住了；放寬 stale 門檻**證實更糟**已回退 → (本檔)
- `[2026-09-09c1]` 吊機四項水平修正：A+B+C 是參數沒設對，只有 D 是新程式碼 → (本檔)
- `[2026-09-09 更正 + 實測]` 🔴 「滑台把機體推歪」是錯的 —— per user 指出，實驗也證偽 → (本檔)
- `[2026-09-09 實測]` 10 週期在週期 1 步 4 中止 —— 挖出一個前提錯誤的自動修正機制 → (本檔)
- `[2026-09-09 實測]` 姿態修正、`init` 卡住的真因、以及 `crane_cmd_` 讀到半行殘片 → (本檔)
- `[2026-09-09 實測]` 第二輪 10 趟 + 姿態累積量測 —— 📌 **這是已知的設計前提，不是新發現的故障** → (本檔)
- `[2026-09-09 實測]` `mission_run.py 10 224` 第一輪：20/20 完成，並驗證了 `m6` 的一半 → (本檔)
- `[2026-09-09m6]` `mission_run.py` 補上持續性判定 —— 移植 `cycle_test.py` 09-04 就做過的決定 → (本檔)
- `[2026-09-09m5]` 鏈路可見度（Option B）＋ 修掉 `imu_push_loop_` 的無逾時 connect ＋ 測試位址可覆蓋 → (本檔)
- `[2026-09-09m4]` `link_probe.sh` 改量「窗口內最長空窗」—— 因為驗收判準是空窗，不是丟包率 → (本檔)
- `[2026-09-09m3]` 🔴🔴 兩條急停不再走會被鎖住的主通道 —— 並讓那條旁路真的存在 → (本檔)
- `[2026-09-09m2]` 🆕 `scripts/link_probe.sh` —— 把 09-08 的受控實驗做成可重跑的腳本 → (本檔)
- `[2026-09-09m1]` 🔴🔴 MH300 冷啟動無條件解除 base-block —— 補掉一個「只在行程重啟時發生、且完全沒有徵兆」的洞 → (本檔)
- `[2026-09-08m2]` 🔴 「強制走 WiFi」是這個機制唯一表達不出來的意圖 —— 覆蓋判準由比對值改為看環境變數 → (本檔)
- `[2026-09-08m1]` 🔗 連往吊機的位址收斂成單一來源 —— 第 5 處若連到死位址，超重保護會靜默失效 → (本檔)
- `[2026-09-08g1]` 🖥 console v2 繼電器：移除確認彈窗 + 消除 700ms 回饋延遲 → (本檔)
- `[2026-09-07m6]` 📚 原廠資料整理進 `doc/`；補上四份缺的硬體摘要（8→14 份） → (本檔)
- `[2026-09-07m5]` ⚰️ Visual Studio 整組移除；補上被它獨佔的建置定義 → (本檔)
- `[2026-09-07g6]` 🔗 Setting 表四處耦合到期改完；並抓到同表另一個方向相反的謊 → (本檔)
- `[2026-09-07m4]` 🔴🔴 VS 早就不是建置路徑了；四個「待建置」改動當天建完、部署、實機驗證 → (本檔)
- `[2026-09-07m3]` 🔴 本體 `status` 新增 `step_in_progress` 欄位 → (本檔)
- `[2026-09-07g5]` ⚡ console v2 動態輪詢：閒置 1s、運動中 250ms（附防堆積） → (本檔)
- `[2026-09-07g4]` 🛠 前端靜默失敗檢查器進版控（`web_backend/tools/check_console.js`） → (本檔)
- `[2026-09-07m2]` 🔴 `cmd_brush` / `cmd_water_pump` 補回讀；回讀整條掛掉的那條路徑補上證據 → (本檔)
- `[2026-09-07g3]` 🖥 console v2 加電腦版／平板版切換（觸控目標 44px） → (本檔)
- `[2026-09-07g2]` 🖥 console v2 首次被渲染出來看（headless，帶真實資料）；輪詢說明改由資料產生 → (本檔)
- `[2026-09-07m1]` 🔴 `balance_source` 編譯預設 Meter → Imu；`cmd_pump` 的謊已在 09-03 修掉且已上機驗證 → (本檔)
- `[2026-09-07g1]` 🔧 步伐上限改成向後端取值；補掉 console v2 第三處寫死的 roll 門檻 → (本檔)
- `[2026-09-04r]` 🎯 `cycle_test` 完整週期跑通（5 步 200cm + 回程）；roll 修正再修兩個實作缺陷 → (本檔)
- `[2026-09-04q]` 🔒 Mission 的兩顆修正鈕納入腳本鎖；並記錄「腳本執行中鎖定」的三個缺口 → (本檔)
- `[2026-09-04p]` 🎯 `cycle_test` 首次跑通；roll 由「瞬時中止」改成「連續判定 + 自動修正」 → (本檔)
- `[2026-09-04n]` 🖥 console v2：姿態改 2D + 自動量程；張力條門檻不再頂在邊緣 → (本檔)
- `[2026-09-04m]` 🖥 console v2：姿態圖由 3D 改回 2D（只有 roll 是被規範的量） → (本檔)
- `[2026-09-04o]` 🎯 兩個守衛拿到實測依據；橫桿讓 `obstacle` 的處置整個翻案 → (本檔)
- `[2026-09-04l]` 🖥 console v2：Manual 張力保護改用 Dashboard 的畫法（並抽成共用函式） → (本檔)
- `[2026-09-04k]` 🖥 console v2：Dashboard 整頁重新規劃（大樓圖／3D 姿態／圓形吸盤／張力門檻標點） → (本檔)
- `[2026-09-04j]` 🖥 console v2：頁首四格收進 Dashboard；roll 補進 Manual 吊機卡 → (本檔)
- `[2026-09-04i]` 🖥 console v2：Setting 也改成卡片版面，四個分頁自此一致 → (本檔)
- `[2026-09-04h]` 🖥 console v2：接上 `en=`，並把 `arm_status` 解析改成對欄位新增免疫 → (本檔)
- `[2026-09-04g]` 🖥 console v2：Dashboard / Mission 改用與 Manual 相同的卡片版面 → (本檔)
- `[2026-09-04f]` 🖥 console v2：推桿分側定位、上方張力條改成「離跳掉還有多遠」、快捷檔位 → (本檔)
- `[2026-09-04e]` 🖥 console v2：戶外亮色、Manual 版面重排、四個「靜默沒作用」的元素 → (本檔)
- `[2026-09-04d]` 🔴 接觸後改推 `hold_pos`：消掉 `DEPLOY_F` 尋觸的加載/卸載振盪 → (本檔)
- `[2026-09-04c]` 🔴 keepalive 的回覆會冒充成指令回覆；手臂面板加代轉路徑 → (本檔)
- `[2026-09-04b]` 🎯 `DEPLOY_F` 力控貼合：壓力變成被控量，`wall_mm` 退場 → (本檔)
- `[2026-09-04a]` 🖥 console v2：手臂面板 + 兩個讓回覆卡到逾時的傳輸層 bug → (本檔)
- `[2026-09-03a]` 📋 09-03 程式改動補登（事後由 git 重建，非當日撰寫） → (本檔)
- `[2026-09-02j]` 🎯 M1 起步頓挫與 M2 定位：兩個被遺留的舊參數 → (本檔)
- `[2026-09-02i]` 滾筒繼電器順序；三個「加速」嘗試全部否決 → (本檔)
- `[2026-09-02h]` 上滑台：手動指令、軟體失能、行程 140cm、速度與斜坡調整 → (本檔)
- `[2026-09-02g]` M2 定位改為停點相對；修掉會毀掉零點的陳舊偏移門檻 → (本檔)
- `[2026-09-02f]` cleaning_arm 重力模型重擬 + go_home 安全閥 + wall_mm 定案 → (本檔)
- `[2026-09-02e]` Claude Code — 馬達自報的錯誤碼一直被丟掉（純觀測，不改控制行為） → (本檔)
- `[2026-09-02d]` Claude Code — `lr_move_to_slot_impl` 的 passive 探針把 M2 踹過頭（短距離移動落點差 0.14 rad） → (本檔)
- `[2026-09-02c]` Claude Code — QX-DO24：FC 0x06 頻率退路 + 模組重啟命令 → (本檔)
- `[2026-09-02b]` Claude Code — JC100 回覆結構檢查：拆開「CRC error」，並修掉例外回覆被當成功的缺陷 → (本檔)
- `[2026-09-02a]` Claude Code — `cycle_test.py` 兩道守衛：輸出可觀測性 + 真空源前置檢查 → (本檔)
- `[2026-09-01b]` Claude Code — `fine_adjust` 水平參考偏移 + 吊機冷啟動驗證 → (本檔)
- `[2026-09-01]` Claude Code — 張力門檻常數化 + 最後四支裸對驅動改原子交易 → (本檔)
- `[2026-08-29]` Claude Code — 上滑台方向確認工具 + homing 註解更正 → archive/changelog-2026-08.md
- `[2026-08-28-drv5 ＝ 2026-08-28k]` Claude Code — 🔴🔴 上滑台每個 cm 指令都走 7.7 倍（實機量測） → archive/changelog-2026-08.md
- `[2026-08-28u]` Claude Code — 掃描「整體回 OK 掩蓋個別失敗」：status 的壓力值 + attach 的部分密封 → archive/changelog-2026-08.md
- `[2026-08-28t]` Claude Code — 緊急收繩：補上「說」，刻意不補「停」 → archive/changelog-2026-08.md
- `[2026-08-28s]` Claude Code — 吊機掃描：`cmd_side_measured` 一被 abort 就永久壞掉 → archive/changelog-2026-08.md
- `[2026-08-28r]` Claude Code — 系統性掃描：DM2J 有 16 個 void 函式吞掉寫入結果（含「停止」） → archive/changelog-2026-08.md
- `[2026-08-28p]` Claude Code — 🔴 左右歸屬修正：解除交替步伐的封鎖 → archive/changelog-2026-08.md
- `[2026-08-28n]` Claude Code — 🔴 推桿 pulse/cm：08-27 的「更正」本身是錯的（實機拿尺量） → archive/changelog-2026-08.md
- `[2026-08-28m]` Claude Code — `zdt_pusher` / `zdt_disable` / `zdt_enable` 三個指令自 08-27 起就不可能成功 → archive/changelog-2026-08.md
- `[2026-08-28k]` Claude Code — 🔴🔴 上滑台每個 cm 指令都走 7.7 倍（實機量測） → archive/changelog-2026-08.md
- `[2026-08-28q]` Claude Code — 重連把失敗的 connect 判成成功（＝ driver 分支 `-drv4`） → archive/changelog-2026-08.md
- `[2026-08-28-drv3]` Claude Code — DM2J 寫入結果一路被丟掉，main 的「掃動全滅偵測」接到的是常數 → archive/changelog-2026-08.md
- `[2026-08-28j]` Claude Code — 合併 `origin/main` 0d5f6bc 進整理分支 → archive/changelog-2026-08.md
- `[2026-08-28-drv1]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28-drv2]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28u]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28t]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28s]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28r]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28q]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28p]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28o]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28n]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28m]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28l]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28k]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28j]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28i]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28i2]` Claude Code  ← 編號補記：與上方 i（滑台 EST_MS）為不同批改動，先後獨立 → archive/changelog-2026-08.md
- `[2026-08-28h]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28g]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28f]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28f2]` Claude Code  ← 編號補記：與上方 f（貼牆距離）為不同批改動，先後獨立 → archive/changelog-2026-08.md
- `[2026-08-28e]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28d]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28c]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28b]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-28a]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-27h]` Claude Code → archive/changelog-2026-08.md
- `[2026-08-27g]` Claude Code → archive/changelog-2026-08.md
- `[2026-04-14c]` Claude Code — IMU 整合（WT901BC baseline + 監控 + Phase 5） → archive/changelog-2026-04.md
- `[2026-04-14b]` Claude Code — Crane watchdog → archive/changelog-2026-04.md
- `[2026-04-14]` Claude Code — Phase 4-A margin 修正 + DM2J 同步等待 → archive/changelog-2026-04.md
- `[2026-04-13d]` Claude Code — testSingleLegWash 重構 + adjustLegPos 修正 → archive/changelog-2026-04.md
- `[2026-04-13c]` Claude Code — bugfix → archive/changelog-2026-04.md
- `[2026-04-13b]` Claude Code → archive/changelog-2026-04.md
- `[2026-04-13]` Claude Code → archive/changelog-2026-04.md
- `[2026-04-12]` Claude Code → archive/changelog-2026-04.md

---

## 當月全文(2026-09,98 條)

## [2026-10-08c] 中繩連動:放繩多放 % + 連動方式(長度跟隨/只照速度)

per user「左右放繩的時候中繩長度也要跟上,甚至更多,在連動鋼索這邊可以設定」「這個功能要可以開關,在中間計米器失效的時候做獨立控制」。
- **放繩多放**(`set_middle_pay_extra <0..100>`,持久化,預設 0;status `middle_pay_extra`):**只在放繩**時中繩追 `k × (1+%) × 左右平均位移`、Hz 前饋 `middle_hz × (1+%)`(上限 50);收繩維持 k × 平均 —— 收得比鋼索多會讓中繩吊住機器。🔴 `middle_lag` 中止**仍以 k × 平均判**(追不上多放的那段 ≠ 拉緊)。兩側 ▲▼ 跟隨與 `set_middle_auto` 的 pay_out 都套用(同一個 `middle_track_gain()`)。
- **連動方式**(`set_middle_link_track <0|1>`,持久化,預設 1 = 原行為;status `middle_link_track`):1 長度跟隨(需中計米器);0 **只照速度** —— ▲▼ 跟隨不讀中計米器,中繩以 `middle_hz × (1+%)` 跟著兩側起停,`cmd_hold` 不再因中計米器擋鋼索(clv900 不在/故障仍擋)。每次按下時鎖定模式。🔴 只照速度**沒有落後保護**(GUI 切換時 confirm 一次講明)。pay_out/retract/goto 的自動跟隨不受此開關影響(要靠中計米器決定停點)。
- `cmd_hold` 拒絕改帶原因:`ERR middle_unavailable <clv900=0|clv900_fault|meter_middle>`;GUI 鋼索 ▲▼ 被擋時放開按鈕並在 log 給出口;連動 + 長度跟隨 + 中計米器讀不到 ⇒ 中繩卡紅字提示「改只照速度/取消連動」;標頭在只照速度時標「(只照速度)」。
- Manual 中繩卡:連動勾選下新增「連動方式」兩鈕 + 「放繩多放」快選 0/5/10/20/30 + 自填。fake 加 `fake_meter_middle` 旋鈕(中計米器與 CLV900 分開)。harness **287 全過**(+9);吊機本機完整編譯連結 OK、`-Wall -Wextra` 零警告。
- **未部署**(機器關機中);部署時 crane + GUI 兩台(GUI 版號要 bump)。

## [2026-10-08b] 中繩(CLV900)程式支援:手動按住 + 自動跟隨

per user「程式支援中繩 OK」「第二階段直接做完」。實體:official 中繩 clvdrives `900-0007M1`(迷你型 + 485 擴展卡)接**新 USR `.36`**、slave 3、115200 8N1。GUI/fake/harness 由另一 session(AI-2)依本 session 給的介面規格實作,本 session 驗收。
- **啟用**(`main.cpp`):預設關 = 舊行為。`FCV_CLV900_ENABLE=1` + `FCV_EP_USR_MW_HOST`(新 `cli_MW`)+ `FCV_CLV900_SLAVE`(預設 3;`FCV_CLV900_GW=A` 可共用 cli_A)。開機探測 U0-00,讀 F0-00/F0-01 **只警告不寫**。keepalive 每秒讀 U0-00/01/03;新故障 → 停手動中繩 + `EVT middle_fault`(**不自動 reset**)。status +`middle_state/fault/run_hz/hold/auto/ratio/dir_invert/comm_fail`、`dev_gw_mw`。official drop-in `fcv-crane.service.d/middle.conf`。
- **手動**:`middle_hold <pay|retract> <on|off>`(併入 1.5 s hold 租約、`any_hold_active`、`hold_all_off` ⇒ 租約到期/張力保護/watchdog/stop 都停中繩;`EVT middle_lease_expired`)、`middle_stop`;所有啟動走 `middle_run`(方向反轉一致)。
- **自動跟隨**(`set_middle_auto`/`set_middle_dir_invert`/`set_middle_ratio`,持久化):motion_rope 與兩側同步 ▲▼ 都 P 追蹤 K × 左右平均位移(1 Hz/cm、下限 2 Hz),原本固定 10 Hz 跑到目標會在放繩時落後而吊住機器(無中繩張力計)。中止 `middle_lag`(放繩落後/收繩超前 >15 cm)、`middle_wrong_dir`(反向 ≥5 cm)、`middle_fault` → 歸類安全中止(緊急停 + `EVT motion_abort`、不做 fine_adjust)。auto 開但中繩不可用 ⇒ `ERR middle_unavailable`(motion 與 hold on 都拒)。
- 🔴 修既有漏洞:motion_rope 以 **timeout / length_diff** 結束時中繩不會停(只在中繩從沒啟用過才沒出事)⇒ 迴圈出口一律停中繩。
- GUI(AI-2):Manual `#mid-card`(狀態、運轉 Hz、中計米、故障、hold、▼放 ▲收 按住、停止;吸附中鎖按住鈕;`dev_clv900=0` 停用但「停止」可按)、**速度快選 10/20/30/40/50**(per user,同鋼索 `data-hzq`,反白照 status)、Setting「中繩 · CLV900」三項。harness 270(+39)。順手修 GUI 錯誤橫幅 RECOVER 說明「重驗 **9** 顆吸盤」→ 4 顆(v1 殘留,本體 `cmd_recover` 本來就驗 4 顆)。GUI `v3-2026.10.08-2148` 部署 official + 測試吊機(測試吊機無 CLV900 → 中繩卡自動停用)。
- **連動勾選**(per user「打勾吊機同收同放跟著轉」「速度用當前選中的速度為主」):`set_middle_link <0|1>`(**runtime、不持久化**,重啟回不勾;`clv900` 不在時拒開)讓**兩側同步 ▲▼** 帶中繩(同 hold_loop 跟隨:基準 = 中繩選中速度 `middle_hz`,中計米跟不上時 P 補償、>15 cm 中止),**不影響 pay_out/retract/goto**(那仍是 `set_middle_auto`)。連動開著而中繩不可用 ⇒ 鋼索 hold on 也 `ERR middle_unavailable`。關掉時若正在跟隨立即停中繩。status +`middle_link`。**兩側同時放開時在 `cmd_hold` 直接停中繩**(不等 hold_loop);CLV900 F0-04/05 改 0.5 s(SE3 P.7/P.8 = 0.10/0.50 s @60 Hz,單位 P.21=0 → 0.01 s)⇒ 連動同時停。GUI Manual 中繩卡勾選框 + 標頭「連動鋼索 ▲▼」(任務鎖定文字優先);fake + harness +9 ⇒ **278 全過**。crane 部署 official、GUI `v3-2026.10.08-2205` 兩台。
- 現場:CLV900 參數以 FC06 寫 F0-01 0→8、F0-04/05 20.0→2.0 s(原值備份 official `~/run/clv900_params_before_*.json`;原設定一轉就往 50 Hz、停機 20 s)。`middle_hold pay` 10 Hz 實轉兩次:正轉、放開 ~1 s 停。待:方向目視確認、中繩掛繩後才開自動跟隨。

## [2026-10-08a] official 吊機半邊部署最新 main + USR 錯置以改 IP 修正

- `deploy.sh`:official 的 `CRANE`/`BODY` 也可用 `FCV_CRANE`/`FCV_BODY` 覆蓋(倉庫裡只能走 WiFi `.5.15`);檔頭註記 official nexuni 已全開 NOPASSWD(per user 保留)。
- official 換新 `fcv-crane.service`(xargs 回放、`StartLimit*` 回 `[Unit]`)+ 新 `crcmd.py`;crane 9f50192f → 中繩版 f385ccf9;GUI + server.js;`cam.conf` drop-in(`CAM_BASE=http://192.168.5.26:8091`)。
- 電箱整理後 RS485 錯接:逐顆 USR 單獨上電 + FC03 掃 slave 1–16 判定 `…a5:2c`=左 SE3、`…a5:23`=左計米(8N1 設備接 8N2 網關回亂碼)⇒ **不動線路,兩顆網關 IP/停止位元互換**(`usr_reconf.py`:比對 MAC、備份、原值帶回、`reset=1&rup=0&rfp=0`)。🔴 坑:`network.shtml` 有兩個 `staticip`(字面值 + JS),取錯 ⇒ 送非數字 ⇒ 設備當 DHCP。
- 其他查證:X518 只收一條 TCP、異常斷線卡死到斷電(手冊無逾時設定);device 旗標只在開機設定(晚到設備要 restart);左計米站號被改成 3(面板改回 1);新 USR `.36` 原 IP 10.0.0.147(AF_PACKET 嗅探 + UDP 廣播找到)。

## [2026-10-07c] 攝影機串流優化(H.264 直通)+ 攝影機多餘功能關閉

per user「兩隻的速度有點慢,都關閉不該啟用的功能,串流可以再優化」。查:攝影機子碼流本身 ~20–25 fps,**慢的是我們 MJPEG 5 fps**。
- **`scripts/cam_relay.py`(取代同日的 `cam_mjpeg.py`)**:每支兩條 ffmpeg 管線 —— `/cam/<id>.mp4` = `-c:v copy` fMP4、`frag_every_frame`(一張一段,muxer 不囤),**常駐(warm)**,新觀看者拿 init + 最近關鍵畫格起的 GOP 快取 ⇒ 開畫面即有;ffmpeg 重啟就結束回應讓頁面重連(新管線時間軸重來,同一個 MediaSource 會卡);`/cam/<id>.mjpg` 保留為備援(有人看才轉)。status 加 `codec` 與 `modes{mp4,mjpeg}`。
- **GUI**:`<video>` + MediaSource(`sequence` 模式),codec 從 init 段的 avcC 讀;追即時邊緣:落後 >1.5 s 跳、>0.6 s 1.1 倍速、<0.4 s 回 1.0(🔴 第一版 0.3 s 門檻 ⇒ Chrome 本身就留 ~0.3 s ⇒ 永遠 1.1 倍、掉格 30%);無資料 6 s / 10 s 內沒資料 ⇒ 重連;無 MSE 或連續失敗 ⇒ MJPEG。格式選單 自動/H.264/MJPEG(localStorage)。點畫面 = 全螢幕。
- **攝影機端**(XM 34567,`scripts/xm_ipcam.py`;改前整份備份 `config/ipcam/features_*_before_cleanup_20261007.json`):音訊(主/子)、移動偵測、人形偵測、雲端 P2P(`secu100.net`,Ret 603 = 重開生效)、推播、線上自動升級、每週自動重開、錄影 → 關;子碼流 GOP 2→1 s;慢快門 2→0(A/B 實驗:不影響 fps)。重開後全數保留;**重開後兩支實測 20.0 fps**(重開前子碼流 25,原因未明,非慢快門)。
- 驗證:harness 231 過(`[cam]` +2:格式選單/無 MSE 退 MJPEG、avcC 解析);**真 Chrome 154 無頭**(`harness/cam_browser_check.js`,CDP):兩路 mp4、20 fps、readyState 4、播放端落後 ~0.4 s、掉格 ~1%;本體 ffmpeg copy 各 0.2% CPU;WiFi 0.34+0.27 Mbps(MJPEG 1.46+0.98);兩路開著 `body_link_age_ms` avg 128 / max 248(基準 127/253)。

## [2026-10-07b] 風扇占空比上限 9% 部署 + 部署腳本可指定吊機位址

per user(10-06「duty_max 9」;依 10-02 實測兩顆 NPP 合計 50 A,雙顆 10% = 49 A 頂限流)。改:本體 `PWM_DUTY_MAX_PCT = 9`(`setDutyLimits`)、`cycle_test.py` fan pct >9 拒跑、GUI 輸入/驗證 5~9,**10-07 補 `server.js` Mission `fan=` 正則**(原仍收 `:10`)。harness:`fake_robot` duty_max 9。
測試吊機 WiFi DHCP 由 .25 漂到 .31 ⇒ `deploy.sh` 測試機 `CRANE="${FCV_CRANE:-user@192.168.5.25}"`(預設不變);本體端走 drop-in `crane-ip.conf`(見 work_log 10-07)。
驗證:真機 `pwm set 1 50 65535 10` → `ERR pwm_duty_rejected_must_be_5_to_9_pct`、`pwm status` `duty_max=9`;harness 229 過。

## [2026-10-07a] Dashboard 兩支攝影機

per user。**推翻 09-01「攝影機不列入本版架構」與 08-27 移除攝影機反向代理**(那時是「以後不用串接攝影機」)。
- 攝影機 `.112`/`.113` 在**本體那段**有線網,吊機看不到 ⇒ 新 `scripts/cam_mjpeg.py`(⚠️ 同日由 `cam_relay.py` 取代,見 10-07c)(Python 標準庫)= 本體 user service `fcv-cam` :8091:`/cam/<id>.mjpg`(multipart)、`/cam/<id>.jpg`、`/cam/status`(CORS *)。子碼流 800×448 → ffmpeg MJPEG 5 fps q9;**有人看才起 ffmpeg**(離開 10 s 後停)、卡住 8 s 重啟、每支最多 4 人、mpjpeg 以 Content-Length 切幀(不掃 FFD9)。`Nice=10`。
- **不經 server.js 轉送**:瀏覽器直連本體(經吊機轉 = 每張畫面走兩次 WiFi);`server.js` 只在 ws 連線送 `{src:'cam', base, ids}`(`CAM_BASE`/`CAM_IDS` 可覆蓋,預設 `http://$WROBOT_IP:8091`)。
- GUI:Dashboard 第一張卡、整列寬、兩路並排(≤520px 上下);**只在 Dashboard 分頁開著 + 視窗可見 + 沒按暫停時拉串流**(本體 WiFi 同時承載吊機連線與 IMU 推送);狀態輪詢 2 s(即時 n fps/畫面停 n s/離線/服務離線);fcv-cam 回報 0 人但頁面以為開著 ⇒ 自動重連。`deploy.sh cam` 安裝/更新 unit。
- 驗證:harness 新 `[cam]` 段 13 項(假 fcv-cam 狀態服務)全過,總 229;真機兩支 jpg/mjpg 皆 800×448;開兩路 30 s:ffmpeg 各 ~5% CPU、WiFi 1.46 + 0.98 Mbps,`body_link_age_ms` avg 127→144 / max 253→256(推送週期 ~250 ms 內,無明顯劣化)。
- **OSD 關掉**(per user「兩隻 OSD 幫我移除」):XM 34567 `AVEnc.VideoWidget` 的時間/標題 `EncodeBlend`+`PreviewBlend` → false(原因:鐘沒對時停在 09-14/09-10、兩支標題都 `CAM01`)。Ret=100、讀回 false、快照確認畫面已無字。工具由 git 歷史救回並擴充為 `scripts/xm_ipcam.py`(`get`/`osd on|off`/`time`);改前整段備份 `config/ipcam/`。

## [2026-10-06a] 上下行照使用者選的速度跑(放繩上限 30→50)+ hz 上限收到 50 + 10-03 文件校正

commit `c069c84`(10-06 提交;程式改動為 10-02 per user「GUI manual 吊機更改速度上下不一致 → 上下行照使用者選的去跑」「兩件事都做」)。
原因:09-30 的 `dir_hz()` 對所有 pay_out 路徑套 `pay_out_max_hz`=30,hold ▼ 也在內;GUI 沒標示 ⇒ 看起來上下不一致。
- `Crane_control_PI/main.cpp`:① hold ▼(`vfdStartRopeHold`、`dual_vfd_hold_start`、hold-sync 平衡 base 與結束 reset)改用 `g_vfd_hold_hz`、不再夾 `pay_out_max_hz`;② `PAY_OUT_MAX_HZ` 30 → 50(goto / Mission / side_measured 下行照 `motion_hz`;`set_pay_out_max_hz` 仍可執行期壓低、不持久化);③ `set_hold_hz` / `set_motion_hz` 上限 `VFD_MAX_HZ`(120)→ `ROPE_USER_HZ_MAX`=50(馬達額定),超出回 `ERR hz_out_of_range (1..50)`。
- `index.html`:Manual hz 輸入 max 50(`v3-2026.10.06-1025`)。
- 10-03 文件校正:`CLAUDE.md` 硬體權威改指 `HARDWARE.md` / v2 規格、新增「動機器前必知」(測試移動 ≤5 cm、兩套環境共用一份程式、本體 official drop-in 停用中);`common/log_utils.h` 檔頭「所有 level 受 `debug_mode` 管」更正為 `LOG_ERR` 無條件輸出(08-31 起);`engineering_pitfalls.md` +1 條(`nc -q` 半關閉吃掉慢速指令 ⇒ 判「壞了」前先在已知正常對象上驗探測方式)。
- 決策:hold 路徑不夾 = `hold_hz` 是操作員明示選擇、人在看;`cycle_test.py` 仍自設 `DOWN_HZ=30`,Mission 實際下行仍 30(要改走腳本,另案)。
- 驗證(測試機):hold 50/30/40 上下一致(HOLD-TRACE `hz=`);`set_hold_hz 60` / `set_motion_hz 51` → ERR,`set_hold_hz 50` OK;status `pay_out_max_hz=50`。10-01 留存數據:`pay_out` 50 Hz → roll 6.26°、L−R 10 cm(後續 `length_diff_max_cm` 餘裕疑慮見 work_log 待辦 A)。
- 同 commit 的 work_log(無程式改動):離牆 IMU 擺盪錄製 —— `balance_imu_kp` 0.2 → 0.1(上行 max|roll| 4.78° → 1.63°,吊機 `crane_settings.txt` 持久化 0.1);風扇推力 / 電流估測表。

## [2026-10-02a] GUI:本體斷線時在途指令立即以 ERR 結束 + 風扇 EMI / 電源觀察(`v3-2026.10.02-1647`)

commit `0f61823`。起因:12:05 換電源本體重開,user 按「拉到頂端」無反應。`crane_goto` 走本體代轉(逾時 300 s);`server.js` 對斷線 bridge 回 `{src:'error', line:'washrobot_not_connected'}`,但前端回覆比對只認 `src===target` ⇒ 佇列那道指令不會結束,按鈕卡「移動中…」、再按跳「已經有一次 crane_goto 在進行中」。
- `index.html`:新增 `failInflight(target, why)`;① `src:'error'` + `<t>_not_connected` ② `status` 連線 up→down(`ERR <t>_disconnected`)都會把該 target 的在途指令以 ERR 結束(washrobot / crane / arm 皆適用)。
- 驗證:真機(本體 `fcv-body` stop → Ctrl+F5 → 按「拉到頂端」)立即 🔴 `ERR washrobot_not_connected`、再按仍立即回、不再跳「進行中」(per user「正確」)。案例 ②(送出後才斷線)只有離線,真機未測。離線 harness 215 項:改後 11 輪中 4 輪有 1~2 項失敗,都在「手臂卡工具 seg」(等非同步 `[M2]` 行的時序),改前 4 輪全過;⚠️ 與本修正的關聯未證實。
- 同 commit 的 work_log(無程式改動):風扇 × 匯流排錯誤 A/B —— 5V 隔離電源 vs 原電源,風扇轉時 12~23 / 30 s、停 0~3 同量級 ⇒ **USR 閘道電源不是干擾路徑**(下一步查 RS-485 線屏蔽 / 終端 / 與電調線並行 / 接地);風扇電流實測(雙顆 9% 45 A,10% 僅 49 A);NPP-1700-48 銘牌 25 A ×2 並聯 ≈ 50 A ⇒ 10% 是**電源到頂**、14S ESC 58.8 V 上限擋住升壓 ⇒ 導出 `duty_max` 10 → 9 的決策(10-06 per user、10-07 部署,見 `[2026-10-07b]`)。

## [2026-10-01f] `set_pay_out_max_hz`(測試用執行期覆寫)+ 上下行速度實測

per user 要試下行 40/50 Hz,而 09-30 的 `PAY_OUT_MAX_HZ=30` 是編譯常數。改:`g_pay_out_max_hz`(預設 30)供 `dir_hz()` 使用;`set_pay_out_max_hz <1..50>`(50 = 馬達額定);status `pay_out_max_hz=` 顯示生效值。**刻意不持久化**:重啟一律回 30,測試值不會悄悄變成規則(也不在 crane_settings.txt)。
實測(測試機,10↔234 cm 全程):上行 30/40/50 Hz = 15.7/21.5/25.5 cm/s;下行(上限 30 時)恆 ~16.5 → 上限生效;覆寫後下行 40/50 Hz = 22.2/27.7 cm/s,50 Hz 時 L−R 10 cm(= 門檻)、roll 6.26°。40 Hz 上行曾出現 ±7 cm / ±4° 週期擺盪(單次)。詳見 work_log 10-01。

## [2026-10-01e] 設定持久化:只存與預設不同的值

per user。09-30 的自動持久化整組寫入 ⇒ 檔一存在,之後改程式預設不生效。改:`WashRobot::save_settings_file_` 與 `persist_crane_settings` 逐鍵比對編譯預設(double 容差 1e-9),只寫不同的;全部預設 ⇒ `std::remove` 刪檔。代價(已接受):刻意設成等於預設的值會跟著日後預設變。
吊機兩個預設寫在宣告處的(`fine_adjust_diff_tol` 1、`level_deg_per_cm` 0.85)以字面值比對並註明。
驗證:測試機兩台部署後實測(見 work_log 10-01)。

## [2026-10-01d] 本體繼電器換 ZS-DIO 驅動 + ZS 驅動補強(真機驗證)

起因:`.20` slave 12 holding `0x0032`=12(ZS 站號)、`0x0043`=0(PQW 站號)⇒ 本體那顆是 ZS-DIO,與吊機 09-10 同坑。per user 換。
- `user_lib/ZS_DIO_R_RLY.{h,cpp}`:`sendAndReceive` 改 `TCP_client::sendAndReceive` 原子交易;`parseBitResponse` 驗 slave/FC/長度/CRC;`readGroupState` 同。公開介面不變。檔頭由「退役、未硬化」改為現況。
- `app/WASH_ROBOT.{h,cpp}`:`pqw_` 型別 PQW_IO_16O_RLY → ZS_DIO_R_RLY(名稱/常數刻意不改);init 訊息改 `relay ZS-DIO`。`scripts/build/build_body.sh` 清單 PQW→ZS。
- 行為差異:`controlRelay` 驗 FC06 echo,3 次失敗回 true(PQW 版永遠 false)。
- 驗證:本機兩支 binary 編譯連結;真機 CH1/2/3/6/7/8 on/off 回讀全對、selfcheck、init→attach→detach→retract(CH6)正常、ZSDIO 錯誤 0。未測 CH4/CH5(手臂拆修)。
- 文件:CLAUDE.md 架構圖/吸盤控制/驅動表、HARDWARE.md §2 更正。

## [2026-10-01c] `cycle_test.py` 無手臂模式 + 風扇順序真機驗證

per user「手臂拆下維修」→ B 案:`no_arm=1`(或 `FCV_NO_ARM=1`)跳過 `arm_ready` 起跑檢查、每步與最低點補清的 `arm_clean_combo`/`arm_slot`、`pause_point` 與 `cleanup` 的 `arm_retract`;起跑送 `arm_attached off`,自動 init 後不切回 on。預設關。
驗證:fake_robot 整趟 rc=0、指令序列除 `arm_attached off` 無任何 arm_*;真機 `full 1 1 10 rail=off final_clean=0 no_arm=1`(乾跑)→ ①`pwm 5`→②③伸腳→吸 3 顆→④收腳→⑤`pwm 7`→下放,**09-23 改回的原版風扇順序在真機確認**。

## [2026-10-01b] `deploy.sh` 補同步驅動層 + 本體半邊上機驗證(測試機,WiFi)

- `scripts/deploy.sh`:`deploy_body` 加送 `user_lib/*` `transport/*` `common/*.h` `facade_cleaning_v2/main.cpp`;`deploy_crane` 加送 `user_lib/*` `transport/*` `common/*.h` `mechanism/*.h`。
  起因:09-30 改了 ZDT/QX/DM2J 與 `Serial_port.h` 的 guard,`deploy body` 只送 `app/`+`command/` → Pi 上編譯失敗(這次是失敗;簽名沒變的話會**靜默**編出新舊混合)。
- 本體部署前發現 drop-in 還指 official(`.1.10`)→ 停用為 `endpoints.conf.official-disabled`,回到 unit 預設 `.5.25`。
- 驗證全過:vacuum 分組錯誤碼、死 setting 移除、`set_setting` 持久化(測完移除 `settings.json`)、`crane_idle_ms` 不再無限長大、`body_link_age_ms` ~100、IMU log 節流、**`.21` 斷電再上電 4.5 s 自動重連 + 自動 selfcheck(`dev_qx=1`,免重啟)**。
- 發現待拍板:持久化整組寫入 ⇒ 檔案存在後程式預設值變更不生效。未做:手臂部署、風扇順序(per user)。

## [2026-10-01a] 測試機(raspberry-cran)部署 6f910ff 吊機/server/GUI + 上機驗證;crcmd 啟動逾時修正

**部署**(`deploy.sh crane/server/web`,GUI `v3-2026.10.01-1526`):吊機相依檔與 repo md5 全一致,只同步 main.cpp。本體離線,本體側未部署。

**🔴 部署當下抓到**:`crane_settings.txt` 由 4 行變 19 行後,`systemctl restart fcv-crane` 量到 **82 s**(`TimeoutStartUSec=90s`)——開機慢一點 systemd 就會判啟動失敗、殺掉吊機。根因 `scripts/crcmd.py` 每條指令固定 `drain(2.0 s)`,且 unit 每行開一個 python。
修:`crcmd.py` 收到第一行非 EVT 回覆即換下一條(上限仍 2 s);`fcv-crane.service` 改 `tr '\n' '\0' | xargs -0 python3 crcmd.py` 一次送完。→ **21 s**。測試機 unit 已換(舊檔 `.prev-1001-*`;舊檔是 09-19 前版,StartLimit 兩行還在 [Service])。⚠️ **official 的 unit 也要換**(下次上 official 時)。

**上機驗證(測試機,繩上無負載)**:
| 項目 | 結果 |
|---|---|
| status `pay_out_max_hz=30`、`body_link_age_ms=-1`(本體不在)、`hold_renew` 閒置回 idle | ✅ |
| 設定持久化:改 `balance_kp 1.9` → 重啟仍 1.9、`motion_hz` 仍 30(驗完改回 1.0) | ✅ |
| 續約撐得住:▼ 左按住 5.5 s 未被租約放掉 | ✅ |
| 關分頁:0.46 s 就收到 off —— 是既有 blur/visibilitychange 保護,**不是租約** | ✅(舊保護) |
| **斷線**:按住 ▲ 左 0.5 s 後在吊機端 nft 丟 laptop→:8080(模擬 WiFi 斷)→ `lease expired (1563 ms)`,切斷到停 1.67 s | ✅ |
| 按住時送 `pay_out 1` → `ERR hold_active` | ✅ |
| `hold_hz 50` 時 ▼ 雙側 → `dual_vfd_hold_start pay_out=1 hz=30`(驗完改回 10) | ✅ |

📌 使用者用 WiFi,拔線測不了 ⇒ 改在 Pi 端用臨時 nft 表(`inet fcvcut`,priority -10,測完整表刪除)擋封包;這招比拔線更可重現,之後要測斷線可沿用。

## [2026-09-30b] `DEPLOY_F` 工作力道定案 3(未上機)

per user「DEPLOY_F 力道 3」(C 組 #5 選 b)。`cleaning_arm/main_api.h` `DEPLOY_F_TARGET_NM` 5.0→3.0;`cycle_test.py` 註解同步。
背景:09-14 定 3(清潔效果)→ 09-15 定 5 但只改了 header,腳本 `FCV_ARM_NM`/GUI `mp-armnm` 仍 3 且都顯式傳值 ⇒ **實跑一直是 3**;header 只影響不帶力道的 `DEPLOY_F`。本次三處一致,行為對 Mission 不變。
已知代價(09-15 實測):3 N·m 掃動中 tau 由 3.86 掉到 1.2~1.9;若現場再出現壓不住,改這裡。驗證:`main_api.cpp` `-fsyntax-only` ✅。

## [2026-09-30a] 待辦 A 組一次清(離線,未上機)

per user「滑台橫走時平衡迴路不跑 可以吸盤有做動 還需要嗎，其他的全部都做」。**未 commit、未上機。**

**#3 不做(per user 判斷成立)**:`cycle_test.py` 滑台只在吸附後掃(`n_seal==0` 直接跳過),吸附中繩子本來就不該被平衡迴路拉;平衡只在繩子動時才有意義。

**吊機 `main.cpp`**
- 🔴 **hold 租約**:`* on` 蓋章,`hold_renew` 續約(只動時間戳,不碰匯流排);`hold_loop` 每圈先查,逾 `HOLD_LEASE_MS=1500` → `hold_all_off` + `EVT hold_lease_expired`。`on` 成功後再蓋一次(啟動慢時不給舊章)。GUI 按住每 0.5 s 送 ws `{holdRenew:1}`,**server.js 代送** `hold_renew` 到吊機中斷通道、回覆在後端吃掉——**不經前端佇列**(第一版走 `send()` 佇列,harness 量到續約排在慢回覆後面、按住中租約過期);GUI 收 EVT 清按住狀態。
- 🔴 **本體鏈路年齡**(#1 的修法選項 2,09-09 診斷時就同意):只算非 loopback 來源的收包 → status `body_link_age_ms`(-1=從未)、>3 s `EVT body_link_stale` / 恢復 `body_link_recovered`。**只觀測、不中止**——沒有實測分布前任何門檻都是猜。全域 watchdog 不動。
- hold 期間 motion 四入口(motion_rope/roll_correct/align_lengths/side_measured)拿到鎖後擋 `ERR hold_active`;`cmd_manual`(緊急收繩)刻意不擋。
- **`PAY_OUT_MAX_HZ=30`**:`dir_hz()` 套在所有下放方向的基準頻率(motion_rope 三段、side_measured、hold 單/雙側、manual、hold 同步重設);平衡修正量不夾(否則回到 09-01 的單邊飽和),最壞一側 = 30 + hz_head。status `pay_out_max_hz=`。設定值本身不夾(motion_hz 兩方向共用)。
- `crane_settings.txt` 加平衡/Hz 調校 15 鍵(hold_hz、fine_adjust_hz、balance_*、imu_*、kick/freeze/roll_finish、level_deg_per_cm…);**刻意不存** motion_hz/roll_correct_hz(ExecStartPost 送、cycle_test 會 50↔30 切)、balance_enabled/source/hold_guard/level_auto(模式開關,重啟回安全預設)、fine_adjust_level_diff(level_auto 學習)。
- `set_imu_roll` dispatch log 每 5 s 一行附累計數;`hold_renew` 不記。

**本體**
- 閘道重連自動 selfcheck(`.20/.21/.22` 上升緣,掛在 water_inlet_watchdog 2 s 迴圈;運動中不跑,zdt 匯流排忙就下輪再試)+ `EVT gw_reconnected` / `selfcheck_auto`。09-19 `.21` 晚上電:TCP 其實會自己重連,卡住的是 `dev_qx` 只在 selfcheck 更新的快取。
- `cmd_rail_move` 送出失敗重試 3 次 / 150 ms(只重試 `modbus_write_failed`;travel 拒絕與 `not_confirmed` 不重試)。
- `cmd_vacuum left|right` → `ERR vacuum_group_not_independent`(一顆閥 CH1 管四顆);未知 group → `ERR unknown_vacuum_group`(原本回 `vacuum_valve_fail` 誤導)。
- 5 個死 setting 移除(`pusher_extend_body_pulse(_short)`/`retract_slow_peel_cm`/`step_cm_default`/`step_margin_cm`,struct/建構/get/set/save 共 20 行 + 4 個常數)。舊 settings.json 帶這幾鍵只會印 `load skipped`。
- `set_setting` 成功即自動存 `settings.json`(載入回放時不存;寫失敗回 `persist=fail`,值仍生效)。
- crane watchdog 閒置 = max(指令往來, IMU 推送回覆)——IMU 在線時閒置不再無限長大;09-30 前量到的峰值不可直接比。keepalive ping 維持停用(IMU 推送就是那個探針)。

**驅動 / 其他**:ZDT `motion_control_pos_mode(_nowait)` 拒絕 `pulse<0`(補數會變 ~4e9 脈衝);`QX_DO24::init` 改 false=成功(唯一呼叫端不看回傳);`SerialPort.h` / `Serial_port.h` guard 各自唯一;`WASH_ROBOT.h` 推桿行程表(12→10 cm)、滑台行程/零點、runbook `.101` 三處過期註解。`.22 = arm-rail`、DSZL MBAP 文件兩條查證**早已改好**。

**驗證**:本機 x86 兩支 binary 完整編譯連結 ✅、手臂 `-fsyntax-only` ✅;吊機 binary 跑在 fake_bus 上實測:不續約 1538 ms 放掉 + EVT ✅、續約 4 s 不放 ✅、hold 中 `pay_out`/`roll_correct` → `ERR hold_active` ✅、`set_hold_hz 50` 時 `down` 實際 30 / `up` 50 ✅、非 loopback 連線 → `body_link_age_ms` 251 → 靜默 3 s 發 stale、恢復發 recovered ✅、`crane_settings.txt` 寫出 19 行不含 motion_hz ✅。GUI harness **215 條**(+3 hold 租約),7 次完整跑 6 綠 1 紅(紅在手臂卡工具顯示的開機時序,與本次無關)。**未驗**:本體閘道重連 selfcheck、rail 重試、settings 自動存(只有編譯)。

## [2026-09-23a] 風扇順序改回原版 + 待辦總表清帳(未上機)

- `scripts/cycle_test.py`:步內與最低點補清兩處改回 **① 關風扇 → ② 開閥 → ③ 伸腳**、**④ 收腳 → ⑤ 開風扇**;檔頭步驟表同步。
  理由(per user):09-15 改的「吸附建立後才關 / 收腳前先開」(伸腳與脫離全程有推力)實測**更吵、吸附無改善**。
- `app/WASH_ROBOT.h`:刪掉描述 `crane_retract_safe_`(09-16 已刪)的孤兒註解——它黏在 `crane_stop_estop_` 宣告上方,讀起來像在說後者。
- per user 同批要求的另三件**查證已完成,未改碼**:兩個死碼函式(09-16 `44bcb13`)、手臂 INIT 停滾筒(09-16)、`group_seal_ok_` 每側各 ≥1(08-31)。
- 文件:`work_log.md` 待辦總表清帳重建,舊表原封 `archive/todo-table-2026-08-27_to_09-23.md`。
驗證:`py_compile` 綠;**未上機**。

## [2026-09-21a] 離線日 —— deploy 認 official、meter_suspect 上 GUI、goto 軟停講清楚、rail 拒絕帶原因(未在真機驗證)

- `scripts/deploy.sh`:`FCV_TARGET=official` 切位址(吊機 `.1.10`/本體 `.1.100`)、`web` 的 `PI` 跟著切、`sudo -n`、新目標 `prep-official`(ssh key + sudo -n 檢查)。
  前置(user):兩台 `ssh-copy-id`、official 吊機 sudoers 只放行 `systemctl restart fcv-crane|fcv-web-v3`。
- `web_backend/public_v3/index.html`:`meter_suspect=L/R/M` → Manual L/R 標紅 + tooltip、Dashboard `d-susp-row`(平常隱藏;`.wf[hidden]{display:none}` 因 flex 蓋 hidden);
  `gotoResultText` 認 `stopped=tension_stop` → 「⚠️ 張力先到…」,log ⚠️(`gotoIsShort`)。
- `Crane_control_PI/main.cpp` `cmd_goto`:`motion_rope` 回 `OK tension_reached` 時追加 `stopped=tension_stop short_by=N retract_tension_stop_kg=K`。
  決策:**保留 OK 前綴**(所有消費端把非 OK 當失敗、動作是安全完成);09-15 提的「回 WARN」否決。
- `user_lib/DM2J_RS570.{h,cpp}`:累加式 `last_error()`(`travel_limit target=… range=[…]` / `modbus_write_failed`),三個 `PR_move_cm*` 進場清空;
  `app/wash_robot_commands.cpp` `cmd_rail_move` → `ERR rail_move_command_failed reason=…`。
- `harness/fake_robot.py`:`meter_suspect=` 欄、`fake_meter_suspect`、`fake_goto_short`;`gui_v3_check.js` +4 條(**212/212**)。
- `scripts/systemd/official/ntp_probe.sh`:只探測(路由器 NTP / 公網 / 兩台時差)。
驗證:harness 212/212、check_console 全過、三個 TU `-fsyntax-only` 綠;**未上真機**。未 commit。

## [2026-09-19a] official 上線第二天 —— 開機閘門、計米器 RESYNC、WiFi 橋、endpoint drop-in、web 看門狗(`v3-2026.09.19-1548`)

9/19 白天使用者帶去戶外實測(本體 eth0 + QWRT WiFi 橋 → official-crane);這條是實測前那一夜的全部修正。

- **開機競態**(Pi 上 + `scripts/systemd/official/wait_devices.sh`):Pi 比交換器/USR 早 ~40 s 起來,fcv-crane 把所有裝置標 skipped 直到人手 restart。
  `ExecStartPre` 等 `.30`/`.34` :4001,上限 180 s。踩坑:`#!/bin/sh` + `/dev/tcp` 永遠失敗、白等 180 s、service 卡 activating → 改 bash。
- **計米器永久凍結**(`main.cpp` `meter_read_robust`):30 cm/poll 防雜訊規則沒有回頭路,真值一旦與快取差 >30 cm 就永遠被拒(`prev=1616 v1=v2=3387` 十幾分鐘)。
  official 觸發:3 顆錶串讀一輪 ≈0.5 s,平衡把左推到 62.5 Hz → 每輪 37 cm → 拒 → 凍 → err 越滾越大 → 推更高(自我強化,−103 cm 才被按停)。
  修:`MeterResync` 連貫被拒 ≥3 筆且 ≥2 s → **RESYNC** 接受;新增 `g_length_*_suspect`,`apply_balance_trim` 見 suspect 就回 base 不修正;
  `status` 加 `meter_suspect=`。真雜訊(05-14「0」約 1 s)仍擋。
- **本體↔吊機 WiFi 橋**(`config/qwrt/`):QWRT `apcli0` 進 `br-lan`,MTK 驅動 MAT 讓有線端穿過(09-18「3-address 帶不動」是沒實測的誤判)。
  AP 固定 `192.168.1.250`(+`.100.1` 備援)、DHCP 關、5 口全 LAN、HT20、關 Block-Ack;hotplug + rc.local 兩層保險;重開機驗證。
  RF 極差(RSSI −70、Tx PER 83%、30–40% 掉包、一次斷 285 s)→ 調完 3%,但主路由 20 MHz/拉近才是解。
- **兩端 endpoint drop-in**(`scripts/systemd/official/`):本體 unit 寫死 `FCV_EP_CRANE_HOST=.5.25`(有 env 就不探測有線)、web unit `WROBOT_IP=.5.26`
  → 各加 drop-in 蓋成 `.1.10`/`.1.100`,GUI 才看得到本體、本體才連得到吊機。
- **web 橋 dead-peer 看門狗**(`server.js` `BRIDGE_DEAD_MS`=30 s):掉包讓本體→web 的 TCP 卡指數退避(Send-Q 10 KB、rto 120 s),socket「連著」但 GUI 不更新;
  30 s 沒收到任何資料就 destroy 重連。
- **GUI 吸附鎖只認密封**(`cupsState().attached = sealed > 0`):`.22` 偶發 JC100 TIMEOUT 一顆讀不到就鎖死 ▲▼;本體 `crane_goto` 硬守衛沒動。
- 雜項:`fcv-crane.service` 的 `StartLimit*` 搬到 `[Unit]`(原在 `[Service]` 被忽略且每次 reload 印 Unknown key);
  本體 `.21`(QX-DO24 螺旋槳網關)上電後需 restart fcv-body 旗標才翻 1;`swconfig link:` 不可信;兩台 Pi 沒 NTP。

🟡 待:計米器捲尺校正(左右同指令出繩量差很多)、中錶 DP=0→2(面板)、GUI 畫 `meter_suspect`、`deploy.sh` 認 official、MH300。
驗證:official 實機全部裝置 `[OK]`、body status 經 GUI 路徑 0.14 s、AP 兩次重開機。測試機未動。未 commit。

## [2026-09-18a] 裝置對應 config-driven —— 一顆 binary 兩台共用(`v3-2026.09.18-2326`)

per user:測試機 `raspberry-cran` 與正式機 `official-crane` 並存,**程式共用一份**(否決維護兩套)。
official 的 485 佈線刻意與測試機不同(計米器拆 `.34`+`.32`、ZS-DIO 獨立 `.35`、水閥在 CH1),
所以把「裝置↔匯流排↔站號」從編譯期常數搬到 **env 覆蓋**,**預設值 = 測試機**(不設 env 時行為逐位元不變)。

- `Crane_control_PI/main.cpp`
  - 新增第二條計米器匯流排 `cli_M2` + `FCV_EP_USR_M2_HOST`(沒設就不連 → 測試機零影響)。
  - 三顆 SD76 的 client 與站號改吃 env:`FCV_METER_<LEFT|RIGHT|MIDDLE>_GW`(`M`/`M2`)、`_SLAVE`、`FCV_METER_MIDDLE_ENABLE`。
    🔴 **維持「一條匯流排一個 client」**,不給每顆錶各開連線 —— 避開 USR 透明網關對所有連線廣播造成的 frame 污染(舊雷)。
  - `CH_WATER_INLET` → `FCV_WATER_INLET_CH`(預設 4、official=1);`g_dsz_*_scale` 初值 → `FCV_DSZL_SCALE_LEFT/RIGHT`
    ⇒ **張力校正終於能持久化**(`set_dsz_scale` 是執行期值,每次重啟就被編譯預設洗掉,當天重複踩了三次)。
- `web_backend/public_v3/index.html`:水閥卡片不再寫死 CH4,改從 `water_status` 的 `wch<N>=water_inlet`
  動態建卡片(`WATER_INLET_CH` + `buildWRelay()`)⇒ GUI 也兩台共用。

official 的 drop-in 見 `config/official-crane_485.md` 第四節。

驗證(official 實機):SE3 左右、SD76 左(.34/1)右(.32/2)中(.32/1) 全部 `[OK] … (resumed)`、length 有值;
`ZS-DIO water USR_W slave 1 **CH1** of 4`;張力 1.99/1.97(scale 正值,重啟後仍在);GUI 200。
測試機不受影響(預設值即原行為),**未在測試機上換 binary**。未 commit。

## [2026-09-17v3o] v3：張力「總和」有條了（`v3-2026.09.17-1842`）

per user「總和的部分還是給他一個 BAR」。`renderTenBars` 的 `info` 列加 `ref`：條的滿刻度＝參考值 `2×tension_max_kg`（兩側都到上限），
**顏色固定中性、標點灰、文字寫「參考」**，永遠不出現「超過上限」——總和那道保護已併掉，不能畫得像還有。
驗證：`gui_v3_check` 208/208（第四條有 tbar、含「參考 200 kg」、無「超過上限」）；三方 md5 一致。未 commit。

## [2026-09-17v3n] v3：推桿卡對調＋兩欄、手臂卡切換工具＋重排、張力「總和」讀值回來（`v3-2026.09.17-1752`）

per user 三句（下午）：「吸盤推桿小卡跟下面 4 張小卡對調位置」「順手重新排列吸盤推桿版面」「手臂小卡新增切換工具功能＋重新排版」「張力保護 總和重量怎麼不見了」。
- Manual 本體區：手臂 → 上滑台 → 水路 → 風扇 → **吸盤推桿（整列，墊底）**。上午排「推桿在最前」是操作順序，但整列卡最高、擺最前把四張半寬卡全推下去。
- 推桿卡內 `.grp > .zdtcard` 改 grid 兩欄（3fr/2fr，<820px 單欄）：左 `.zmain`＝單支四列＋解堵轉；右 `.zside`＝整組 · 共用 RPM · 全部失能／使能 · 歸零（兩顆併一格）。id／data-* 不變。
- 手臂卡：**工具列回來**（上午拿掉的工具槽）——seg 滾筒／刮刀，**亮的＝手臂 STATUS `tool=`**（不是上次叫它去哪），按另一顆送本體 `arm_slot RIGHT|LEFT`；
  壓上中灰＋前端擋（手臂端 `LR_SLOT refused: M1 未離開玻璃`）；**壓上改用亮的那顆當 slot**（原寫死 RIGHT 會把已切到刮刀的 M2 轉回滾筒）；頂欄多「工具 X」tag；
  手臂連上那一刻讀一次 STATUS（seg 才知道亮哪顆，之後仍按需讀）。版面全改 `.ctl.stack`（半寬卡三欄一定擠）。
- 張力條（Manual）第四列「總和 L+R kg」純讀值（無條、無門檻）——總和那道保護 09-17 上午已併入收繩上限，AI-2 連讀值一起拿掉了；讀值本身是「吊著多重」，留。
- `harness/fake_robot.py`：`s.lock` → **RLock**（`wr_dispatch('arm_slot')` 在持鎖下呼叫 `arm_dispatch` ⇒ 死鎖，整台假機器停擺；上午加的 passthrough 今天第一次被測到）；
  `M2 LR_SLOT` 建模（壓上中拒、回 `OK slot=`）；`DEPLOY_F`／`arm_deploy_f` 的 slot 會轉 M2（tool= 跟著變）。

驗證：`gui_v3_check` **208/208**（新增：本體區順序、推桿卡兩欄、手臂 seg 切刮刀→tool=squeegee→Dashboard 同步、壓上帶 LEFT、壓上中擋切換、收回後切回、張力四列三條）；三方 md5 一致。未 commit。

## [2026-09-17v3m] v3：張力保護卡加 `length_diff_max_cm` 設定（`v3-2026.09.17-1606`）

per user：卡上四個門檻 + 開關。吊機 `set_length_diff_max_cm <cm>`（>1 且 ≤200，持久化；status `length_diff_max_cm=`）由 agent-ai-db 部署。
- `#tenset` 第四列「左右繩長差上限（cm）｜計米器 L−R｜超過就中止動作」，`data-min=2 data-max=200`（整數 cm；契約是 >1），回填現值；Setting 表那列改標「持久化 · Manual 可設」，
  註解「沒有設定入口」更正；卡片徽章「重啟就沒了」→「門檻會持久化」（09-17 起吊機 crane_settings.txt 回放）；套用的確認文字同步改。
- `harness/fake_robot.py`（AI-2 標記）：`set_length_diff_max_cm` 存狀態、status 帶出（之前寫死 10）。

驗證：`check_console` 全綠；`gui_v3_check` **203/203**（套用 15 → status 15 → Setting 表與共用值卡跟著；1 cm 擋；還原）；`gui_offline.sh report` 0；三方 md5 一致。未 commit。

## [2026-09-17v3l] v3：高水位計（slave 14）（`v3-2026.09.17-1549`）

契約新增 per user（本體已部署、fake 鏡像）：`water_level` 多回 `water_high=0|1|?` `rssi_high=`；status 多 `water_low=`／`water_high=`（本體快取，閥開著時每 2 s）；
高水位=1 本體自動關閥 + `EVT water_inlet_auto_close reason=high_level`；已滿時 `water_inlet on` 回 `OK skipped water_high=1`。

- `waterLv` 變 `{full(低), high(高), at, err}`；`paintWaterLevel` 解析兩顆，`waterFromStatus()` 每筆 status 把 `water_low/high` 併進來（比指令新）；`?` → null。
- Dashboard 水箱水位：兩顆各一燈（低 有水／空箱、高 滿／未滿；讀不到紅）+ RSSI 低/高；tag：需補水／有水·未滿／滿（自動關閥）／讀不到。
- 前置 ③：門檻仍是「低水位有水」；顯示 `低 有水 · 高 滿`。**補水改成補到高水位**：on → 每 2 s 讀 → `high=1`（本體自動關）→ GUI 補送 off（冪等）→ 完成；
  `OK skipped water_high=1` 直接算完成。09-15 的「低位滿再 +20 s 餘量」拿掉——滿的定義現在是高水位那顆，不再用時間猜。
- `EVT water_inlet_auto_close` → log 💧 一行、高燈亮、閥狀態歸 0、補 pollFast／pollWaterLevel。進水閥（ZS-DIO CH4）說明加「高水位滿了本體自動關 · 已滿時 on 會被跳過」。

驗證：`check_console` 全綠；`gui_v3_check` **200/200**（fake：補水前 低空箱·高未滿 → on → 3 s 低有水 → 8 s 高滿 + EVT → OK；status 快取併入；已滿再補 → skipped；閥說明）；
`gui_offline.sh report` 0；三方 md5 一致（吊機 Pi 上線後直接部署）。未 commit。

## [2026-09-17v3k] v3：張力卡即時值撤回 + Mission 狀態字移除（本地 `v3-2026.09.17-1448` → Pi 上電後 agent-ai-db 推上去蓋成 `v3-2026.09.17-1510`，三方 md5 41802232 一致）

⚠️ 兩台 Pi 斷電維修中，per agent-ai-db 本地改好不部署，上電後由他們統一部署。版號用 `deploy_web.sh --dry-run` 蓋（stamp + syntax gate，不 scp）。

- per user **更正**（上一則轉錯）：v3j 加的三格即時值（左/右/上限、左右差/門檻、最大側/收繩上限）**撤掉**，`#tenset` 回三欄；
  user 要的只有「左右繩差上限 `tension_diff_max_kg` 可設定」——那格 09-04 起就在，標籤改「左右繩差上限（kg）｜左 − 右｜超過就停 · 吊機會持久化」。
  上滑台負值那半（v3j）保留。
- per user：Mission 頁 sticky 列最上面那排狀態字（閒置 · 本體 ready · 週期 · 步 · 高度 · 吸住）移除——Dashboard Mission 小卡已有。
  按鈕（暫停⇄繼續／⤒／STOP／PARK）留著。📌 那六個 id 留在一個 `hidden` 容器裡：misPaint／onMissionEvt 仍寫它們，Dashboard 鏡像從 `mis-state`／`mis-h` 抄字，
  check 也用 `mis-state` 當判準——拆 id 得重寫一圈、收益零。
- `harness/fake_robot.py`（`[2026-09-17 AI-2, for agent-ai-db]` 標記）：`set_tension_max_kg`／`set_tension_diff_max_kg`／`set_retract_tension_stop_kg`
  之前落在 `set_*` 通吃回 OK、status 不動 ⇒ 沒辦法驗「套用 → status 跟著變」；改成存進狀態、status 帶出（預設值＝原本寫死的 80/25/50）。

驗證：`check_console` 全綠；`gui_v3_check` **196/196**（左右繩差 套用 33 → status 33 → 改回預設；狀態字六個 id 都在 hidden 容器、按鈕仍在）；`gui_offline.sh report` 0。未 commit、未部署。
- 追加：agent-ai-db 把 fake 三個張力預設改成真機編譯預設 100/50/75（harness 要跟真機一致）；check 改成讀 fake 開機值再還原，不寫死數字（197/197）。
  ⚠️ 伏筆：`② 最高點設定 refused at ground` 那條 check 今天首次偶發失敗一次（重跑即過）——判準是 `w.__alerts.some(/太小/)`，疑似 alert 時序；再發就要改成 waitFor。

## [2026-09-17v3j] v3：上滑台負值 + 張力保護卡即時值（`v3-2026.09.17-1422`）

per user 兩件（本體守衛 −130..130 已由 agent-ai-db 部署；fake 鏡像）。

- 上滑台：移動到／區間起迄／行程守衛的輸入框 min 改 −130；`RAIL_HARD`／`railGuard` 預設 −130..130（0＝rail_zero 當時的位置，可在行程中間歸零）；
  快捷多一顆「→ −50」。⚠️ 行程守衛記在瀏覽器 localStorage：之前存過 0–130 的瀏覽器會沿用舊守衛，要負值請在卡上把下限改成 −130 再套用。
  Mission 參數的 `rail=<起>-<迄>` 仍是 0~130（那是 cycle_test 的清潔段行程，這次沒動）。
- 張力保護卡 `#tenset` 改四欄「標籤 | 即時值 | 門檻格 | 套用」：單側「左 58.6 / 右 40.6 / 上限 100」、左右差「左右差 18.0 kg / 門檻 50」（`set_tension_diff_max_kg` 本來就可設）、
  收繩「最大側 58.6 kg / 上限 75」；超標標紅、未超標綠。即時值＝status `tension_left − tension_right`。

驗證：`check_console` 全綠；`gui_v3_check` **194/194**（三列即時值格式、輸入框 min、守衛預設、`rail -40` 送出不被擋）；`gui_offline.sh report` 0；三方 md5 一致。未 commit。

## [2026-09-17v3i] v3：up_stop_total_kg 退場 + 收繩上限標籤 + Manual 本體區重排（`v3-2026.09.17-1406`）

契約變更 per user（agent-ai-db 部署吊機＋本體＋fake＋腳本）：`up_stop_total_kg` 移除（`set_up_stop_total_kg` → `ERR unknown_cmd`、status 無此欄）；
▲ 手拉停止改用 `retract_tension_stop_kg`（單側，任一側 ≥ 就 hold_all_off），與自動收繩軟停同一個值；
`EVT tension_total_limit total=… threshold=…` 改名 `EVT tension_retract_stop left=… right=… threshold=…`。

- GUI 12 處 `up_stop_total`／`tension_total_limit` 清掉：Manual 張力保護卡的「總和上限」設定格、Setting 參數表那列（`st-ust`）、門檻回填、
  張力條的「總和」那條（Dashboard 兩條、Manual 三條）、抬頭徽章「先撞到哪一道」少掉總和那道、共用值卡字樣。
- `retract_tension_stop_kg` 標籤改「**收繩張力上限（單側）**：自動收繩軟停 + ▲ 手拉停止 · 任一側 ≥ 就停」（user 之前看不懂「收繩停止」）；
  張力條橘點改名「收繩上限（軟停/手拉停）」；共用值卡寫「收繩上限(軟停+手拉停)」。
- EVT 路由改收 `tension_retract_stop`（舊名 `tension_total_limit` 暫時一併收，新舊吊機交錯時不漏）。
- per user：Manual「洗窗機本體」區依**現場操作順序**重排：① 吸盤推桿（吃滿一列）→ ② 手臂 → ③ 上滑台 → ④ 水路（PQW 繼電器，標題加「水路」）→ 風扇；
  Manual 卡片按鈕統一 min-height 36。📌 交接單的「⑤ 繼電器表與狀態（relay_status、壓力、本體 state）」目前沒有獨立的卡：relay 回讀就在 PQW 卡、
  壓力在 Dashboard 吸盤真空度、本體 state 在 Mission 狀態列 —— 沒有另造一張，要的話再說。

驗證：`check_console` 全綠；`gui_v3_check` **191/191**（status 無 up_stop_total_kg／GUI 無其設定格與列、標籤字樣、張力條 2/3 條、餵 `EVT tension_retract_stop` 進 ws.onmessage → 按住狀態清除 + log、本體區順序）；
`gui_offline.sh report` 0；三方 md5 一致。未 commit。

## [2026-09-17v3h] v3 現場修正：手臂卡「收回」被擠掉（安全）+ 本體開關機卡移除（`v3-2026.09.17-1339`）

- 🔴 **現場 per user**：手臂卡按「壓上」後結果文字一長（「壓上：力 5.71 / 目標 … 牆距 … 迭代 …」），把同一列的「收回」擠出卡片 ⇒ **壓在牆上收不回**。
  根因與早上推桿卡同型：`.ctl` 是 `1fr auto auto`、`.rd` nowrap、`.grp{overflow:hidden}`。改：壓上／收回兩顆鈕自己一列（`.arm-btns`，`flex:none`，永遠可見）、
  結果文字獨立一列 `.arm-result`（`white-space:normal`，可換行）。📌 這是今天第二次「文字把按鈕擠出 overflow:hidden 的卡片」——凡是會變長的結果字串，不要跟按鈕同一列。
- per user：Manual「本體開關機」（init／shutdown）整張移除——systemd 自啟、腳本自己 init 幫浦，shutdown 那顆「按錯就掉下來」沒有留下的理由。
  原本那張卡上的本體 state（含暫停原因，09-16 契約）搬到 Mission 狀態列 `#rd-wrstate`（「本體 暫停中（使用者）」），不丟。

驗證：`check_console` 全綠；`gui_v3_check` **186/186**；`gui_offline.sh report` 0；三方 md5 一致。未 commit。
📌 jsdom 無版面：check 驗的是「鈕在自己那列、flex:none、結果列 white-space normal」這些結構事實；會不會再擠只有真瀏覽器看得到。

## [2026-09-17v3g] v3：Manual「手臂」卡重做（`v3-2026.09.17-1211`）

per user 2026-09-17（交接單 `handoff/ai2-manual-arm-card.md`）：「手臂小卡只會有壓上、收回、壓上距離跟力道」「壓上後顯示目前的力跟估測牆距，可即時改力道按套用」「防呆不超過 7 NM」。
契約（本體 `arm_deploy_f <nm> <slot> [dist_mm]`、`arm_force <nm>`、`arm_retract`、`ERR target_nm_exceeds_max_7`、手臂 STATUS `[M1] wall_mm=`）由 agent-ai-db 部署到真機。

- 卡片只剩四列：力道／距離上限（≤7 N·m；122~444 mm 空白＝不限，先到先停）、壓上／收回（壓上一律 RIGHT）、目前（`tau=` 力 · `wall_mm=` 估測牆距，
  壓上後每 1 s 讀 STATUS、收回後停並清空——閒置時 STATUS 是凍結快取）、力道→套用（`arm_force`，未壓上時灰）。
- 前端防呆 >7 與距離範圍都擋（alert 不送）；`OK stopped=distance` 顯示「⚠️ 距離先到，停在 θ…、力 …」仍算壓上；`WARN` 算壓上（黃）；
  `ERR not_in_contact`／`exceeds_max_7`／`no_wall`／`obstacle` 各有一句人話。
- 拿掉：解鎖列、路徑選擇（STATUS／PARK 固定直連 :9527；壓上／套用／收回走本體）、M1／M2 讀取列、目前工具列（Dashboard 任務卡仍有 `tool=`）、
  工具槽、開迴路 DEPLOY、PARK（Mission 狀態列仍有）、INIT（開機自動）。`armPaintSeg`／互鎖 no-op 殘骸一併刪；`armErr` 接受 fake 的 `err=NONE`。

驗證：`check_console` 全綠；`gui_v3_check` **183/183**（fake：壓上 8 擋、壓上 3 → OK → 壓上中／套用可按／目前列 力+牆距、套用 5 → tau=5.00 wall_mm=230、套用 8 擋、
收回 → 未壓上、未壓上硬送 arm_force → not_in_contact 人話、`3 RIGHT 200` → stopped=distance、距離 100 擋）；`gui_offline.sh report` 0；三方 md5 一致。未 commit。
- 追加（`v3-2026.09.17-1215`）agent-ai-db 實查 motor_api `err_name()`：健康值 `0`／**`0x1`**（達妙 1＝使能中，真機現在就印這個）／`NONE`（fake）；
  故障是具名碼 `8:OVER_VOLT`…`E:OVERLOAD`（GUI 附中文：過壓／欠壓／過流／MOS 過溫／線圈過溫／CAN 斷線／過載）；其他 `0x??` 一律當故障顯示原文。
  09-04 那個「三級（0／具名／未分類）」作廢——`0x1` 之前會被標成「狀態碼，未分類」的黃字，真機每次讀都會看到。184/184。

## [2026-09-17v3f] v3：return_top 勾、緊急脫離閃爍、Dashboard Mission 卡（`v3-2026.09.17-1123`）

per user 2026-09-17（agent-ai-db 轉述三件）。契約（server.js `return_top`、腳本 `return_top=0`）由他們部署。

- 參數設定第一列尾巴加勾「任務結束回頂端」（`#mp-rtop`，預設勾）。不勾 ⇒ `params.return_top=0`、等效指令與本體模式尾巴多 `return_top=0`（勾著不顯示）、摘要／確認視窗標「結束停最低點」；defaults 帶入 `return_top`；存檔一起存。
- 執行中 `.estop` 加 `blink`（1 s 呼吸光暈，`prefers-reduced-motion` 退成靜態光暈），永遠 enabled；二次確認照舊。
- Dashboard 第一張「Mission」卡：開始／停止作業／緊急脫離三顆是 Mission 頁本尊的**鏡像**（`data-mirror` → 轉去 `.click()` 本尊：同一支 handler、同一個確認；disabled／同一格切換／閃爍由 misPaint 從本尊抄），
  右側 週期／步／高度／吸住 + 目前參數：執行中＝server 的 `mission.args`＋`env`（週期×步×cm、門檻、kv、頂、手臂/乾掃、回頂）、閒置＝Mission 表單那組（＝defaults 帶入後）。
  `onMissionMsg` 多存 `mis.args`／`mis.env`。
- 追加（`v3-2026.09.17-1132`）per user：這張卡改成**一般小卡大小、放右下**——排在小卡群最後（張力之後），`grid-column:-2/-1` 釘在最後一欄（欄數隨寬度變也一樣）；三顆鈕一列（44px 高）、下面週期/步/高度與參數。173/173。

驗證：`check_console` 全綠；`gui_v3_check` **172/172**；`gui_offline.sh report` 0；三方 md5 一致。未 commit。

## [2026-09-17v3e] v3 共用值：Manual／Mission 共用同一份機器狀態（`v3-2026.09.17-1058`）

per user 2026-09-17（交接單 `handoff/ai2-shared-values.md`；計畫 `plans/mission_manual_isolation.md`）。模型：真值在本體／吊機，Manual 調什麼 Mission 就用什麼，
起跑時腳本強制安全項；**不做 Mission 自己的一份**。契約（本體 `set_pusher_rpm`、`EVT pusher_rpm`、status 執行期 RPM、settings 檔回放）由 agent-ai-db 部署。

- ① Manual 推桿卡：RPM 收成**兩格共用值**（伸／收），`change` 就送 `set_pusher_rpm <伸> <收>`（另一格送現值＝不改）；OK 後清 `data-user` 讓 status 接手；
  `EVT pusher_rpm` 來了跟著變。**單次指令（伸 raw／尋封伸／收，整組與單支）不再帶 rpm**。單支列各自的 rpm 欄與 `zdtRpm()`／`ZDT_RPM_ENABLED` 退場
  （早上 v3c 那版「每支各自 rpm、指令帶 rpm」同日作廢——共用值只有一份，逐支填五遍沒意義）。
- ② Mission 左欄多一張「共用值」卡（前置 → 共用值 → 參數設定）：推桿含入／跳過（`zdt_skip` 有值整張標黃 +「推桿 7 被 Manual 排除」）、
  RPM 兩格**可改**（同一支 `set_pusher_rpm`，兩頁互通）、吊機五門檻 + level_auto + 張力保護（唯讀，讀吊機 status）、起跑強制項一行（zdt_pwr≠1111 時附註）。
- ③ 腳本輸出的「🔴 … 不跑。」行抄到 Mission 狀態列 `#mis-why`（紅、粗）+ log `🔴🔴 腳本拒跑`；下一次起跑或 running 時讓位。

驗證：`check_console` 全綠；`gui_v3_check` **167/167**（fake：change → `set_pusher_rpm 650 400` → status `pusher_rpm=650` → Mission 格同步 → 從 Mission 改回 → Manual 同步；
單次指令行尾無 rpm；`zdt_disable 7` → 共用值卡標黃 → 勾回回白；拒跑行 → 紅 banner）；`gui_offline.sh report` 0；三方 md5 一致。
📌 check 踩坑：[mission] 段之後 backend 留在 body，body 模式每秒由 status 重推 `mis.running` ⇒ 拒跑 banner 的「running 讓位」在 body 模式驗不到，該段先切 script 再切回。未 commit。

## [2026-09-17v3d] v3 現場修正：推桿單支區重排、參數卡對齊、前置剩四項（`v3-2026.09.17-1037`）

user 現場回報（agent-ai-db 轉述）：「manual 吸盤推桿單支按鈕按不到」+ 三件跟著上。

- 🔴 **根因**：單支區是 11 欄 grid（勾·標籤·cm·rpm伸·rpm收·三顆鈕…），平板密度下一列 >700px，卡片只有 300–450px 而 `.grp{overflow:hidden}`
  把右邊的按鈕整個裁掉 ⇒ 看不到也點不到（09-16 之前 8 欄就已經很勉強，09-17 加了 3 欄才爆）。
  改法：卡片 `grid-column:1/-1` 吃滿整列；每支一列 `.zrow`（flex-wrap，可折行，列間虛線）；失能／使能併進該支那列（勾 · 標籤 · cm · rpm 伸 · rpm 收 │ 伸 raw · 尋封伸 · 收 │ 狀態 · 失能 · 使能）；按了整列反白 0.6 s。
  📌 jsdom 無版面，check 只能驗結構（每支一列、flex-wrap、卡片 1/-1）—— 「會不會被裁掉」這類事只有真瀏覽器看得到。
- 參數設定五列統一骨架 `.ctl.mp` + `.mpc`（控制項靠右、輸入框同寬 76px、每格前帶小字標籤）；之前 `.ctl`／`.ctl.stack` 混用所以沒對齊。
- 前置移除「③ 推桿歸零」與「手臂（資訊）」兩列（歸零在 Manual 做；手臂開機自動 INIT、腳本起跑自查 `arm_ready=1`）⇒ 剩 ① 地面歸零 ② 牆高 ③ 水位 ④ 起點。`flowZdtHome`／`flowArmInit` 一併刪。

驗證：`check_console` 全綠；`gui_v3_check` **160/160**；`gui_offline.sh report` 0；三方 md5 一致。未 commit。

## [2026-09-17v3c] v3：Manual「吸盤推桿」卡整理（`v3-2026.09.17-1011`）

per user 2026-09-17（agent-ai-db 交接單 `handoff/ai2-manual-pusher-card.md`，第 5 點同日拍板改勾選框）。契約由他們先部署（body + fake）：`zdt_power <5..8|all> on|off`、status `zdt_pwr=1111`、`zdt_skip=-|7|5,7`。

**改了什麼**（`index.html`）
- RPM 欄改成**伸／收各一格**、value 直接顯示本體預設（status `pusher_rpm`／`pusher_rpm_retract`；09-15 那版只填 placeholder，理由是別把預設凍進指令——per user 反轉：要看得到數字，送預設值與不帶參數對本體等價）。
  人改過的欄（`data-user`）或正在打字的欄不被 status 蓋回。「RPM 預設值」列拿掉。伸 raw／尋封伸用伸那格、收用收那格。
- 「歸零」→「當前位置歸零」（雙重確認照舊）；「重歸零（24V 跳電後）」→「自動歸零」只留 `zdt_home all`；①②／取消的手動流程 IIFE 整段刪。
- 新增「失能／使能」列：單支 5/6/7/8 各一組 + 全部（`zdt_power`，真斷電）；狀態讀 `zdt_pwr=`（標明指令狀態）；失能要確認；使能鈕 title 提醒「手推過先當前位置歸零」。
- 「納入／排除群組」兩顆鈕移除 → 每支前面一個勾「整組指令包含這支」，勾／取消立刻送 `zdt_enable/zdt_disable <s>`；顯示以 status `zdt_skip=` 為準（送出後到下一筆 status 前不蓋回），沒勾的標籤刪除線 +「整組跳過」。`pusher all` 照送不拆。`解堵轉` 保留在單支區下。

**驗證**：`check_console` 全綠；`gui_v3_check` **161/161**（+9：RPM value/不蓋回/各自那格、改名、zdt_power 全部/單支/狀態、勾選 zdt_skip 往返）；`gui_offline.sh report` 0；三方 md5 一致。
⚠️ 踩坑（check）：`LOGBUF` 是 300 筆 ring，`logs().slice(fromLength)` 在 ring 滿了之後永遠是空的——單跑 `--only hold` 過、全跑就掛。改看 `slice(-8)`。未 commit。

## [2026-09-17v3b] v3：側欄順序、Mission 重排（草圖）、四卡移除、兩卡搬 Dashboard、存檔鈕（`v3-2026.09.17-0959`）

per user 2026-09-17 早（agent-ai-db 交接單 `handoff/ai2-mission-relayout.md` + 草圖 `.png`）。純 GUI；契約只多一支 `{mission:'save'}`（見下）。

**改了什麼**
- 側欄 Dashboard → Mission → Manual → Setting，預設開 Dashboard（執行細節都搬過去了，開頁先看機器）。
- Mission：sticky 狀態列留「狀態 chip · 週期/步/高度/吸住 · 暫停⇄繼續 · ⤒ / STOP 吊機 / PARK 手臂」；
  下方 `.mis-grid` 兩欄——左：前置一行摘要卡（`#pre-card`，標題就是摘要，點開才展開）+ **參數設定五列**
  （單步距離/步數/週期數 · 風扇 · 滑台 · 手臂壓力/乾掃 · 中止門檻；頂端高度列拿掉，等效指令縮成一行小字）；
  右：**存檔 / 開始⇄停止作業（同一格，依 running 只露一顆）/ 🔴 緊急脫離**（紅、隔開、confirmOnce 照舊）。
- 移除四張卡 + 它們的寫入點：即時監看（`m-roll`/sparkline `rollHist`/`m-diff`/`m-tdiff`/`mon-age`、`bar()`）、
  致動器·指令↔回讀（`setVerif`/`paintPumpVerif` 與全部呼叫點；`fire()` 第 3 參數留位不用）、吸盤 p5–p8（`m-cups`/`m-seal`）、單步時間基準（純靜態）。
  對應 CSS（`.spark`/`.cups`/`.cup`/`.verif`/`.vf`/`.gauge`）一併刪。
- 搬 Dashboard：任務執行細節列（子步驟/等真空/目前工具/清潔/跳過/急停脫離/最近事件）、腳本輸出、每步時間；id 全沿用。
- `.estop` 選擇器由 `.top .estop` 放寬——這顆 09-15 搬離頂欄後其實一直沒套到紅色。
- **存檔**：`{mission:'save', params}` → `server.js` 新增 `missionSave()`（`[2026-09-17 AI-2, for agent-ai-db]` 標記；驗證抄 start 那套、不 spawn、
  存進 `mission_params.json` 多 `saved:1`），ack 帶 defaults → `#mp-last` 顯示「已存 時間」。🔴 **Pi 上的 server.js 由 agent-ai-db 部署**，在那之前真機按存檔只會 log 一行、沒有 ack。

**驗證**：`check_console` ①②③④ ✅；`gui_v3_check` **152/152**（+10：分頁順序、四卡不在、Dashboard 持有三卡、五列順序、右欄三鈕、單格切換、存檔 ack + 檔案落地 + 壞值擋下）；
`gui_offline.sh report` 0 not modelled；三方 md5 `c97e5d1a…` 一致。
📌 「假機器跑 start → 停止作業 → 緊急脫離」：check 的 [mission] 段從按鈕真起本體模式任務並停止、[stop] 段走 STOP ⑤ 橫幅與緊急脫離；腳本模式的 start 在 check 裡只驗 WS payload（不真的對 fake 起 cycle_test）。未 commit。

## [2026-09-17v3] v3：`cycle_test.py` 路徑跟著搬到 `scripts/`（`v3-2026.09.17-0934`）

per user（agent-ai-db 轉述）`Linux_test/` 退役、`cycle_test.py` 搬到 `scripts/`，server.js MISSION_PY 已改。
GUI 只有顯示層跟著改：`index.html` 的「等效指令」字串（:3638）與兩處註解（:34、:3873）`Linux_test/` → `scripts/`；
`gui_v3_check.js` 的 regex 只比對 `cycle_test.py …`，不需動。
驗證：`gui_v3_check` 142/142；三方 md5 `687bdb37…` 一致（:8080）。未 commit。

## [2026-09-16v3c] v3：v1 退役收尾 —— 「v2」連結拿掉、check_console ② 清零（`v3-2026.09.16-1948`，改到 :8080）

**背景**：agent-ai-db 今晚把 v1 GUI（`public/`、`fcv-web.service`、:8080）退役，v3 服務改聽 :8080；
`check_console.js` 預設改查 public_v3，報出 4 個「寫到不存在的元素」。GUI 線順手收尾（per agent-ai-db 轉述）。

**改了什麼**（`web_backend/public_v3/index.html`）：
- 頂欄 `<a href="http://localhost:8081/">v2</a>` 拿掉（現在指到空的）。
- `rd-balen`：其實只剩一段 HTML 註解裡的 `txt('rd-balen', …)` 字樣被字面比對抓到；那格 09-11 已併進 `rd-balsrc`，註解改寫成現況。
- `flow-zdt-rd` / `flow-arm-rd`：**不是死寫入**——列是 `renderFlow()` 用 `'flow-' + k + '-rd'` 動態長出來的，checker 只認字面 `id="…"`。
  新增 `flowRd(k)` 集中查找（paintFlow / flowZdtHome / flowArmInit 共用），checker 不再誤報、拼法也只剩一處。
- `flow-init-rd`：真的沒了（④ init 09-15 離開 PRE_ITEMS）——`flowInit()` 保留給手動用，只拿掉寫結果格那兩行，回饋走 log。
- 手臂待命位改滾筒（RIGHT）：GUI `tool=` 讀真值，無「INIT 後應為 center」假設，**不需改**。

**驗證**：`check_console.js` ①②③④ 全 ✅；`gui_v3_check.js` 142/142；`gui_offline.sh report` 0 not modelled；
`deploy_web.sh` 三方 md5 `56edc1b0…` 一致（本機／Pi 檔／`http://192.168.5.25:8080/`）。
未 commit（agent-ai-db 統一）。
📌 本機離線 harness（`gui_offline.sh`、`gui_v3_check.js`）仍用 :8081，那是開發機的 port，與 Pi 無關，不動。

## [2026-09-16v3] v3：本體狀態機收斂成 6 個 —— GUI 的 `WR_MOVING` 與暫停顯示對齊

**改了什麼**（`public_v3/index.html`、`harness/gui_v3_check.js`；`v3-2026.09.16-1418` 已部署，未 commit）
- 契約（agent-ai-db，已上真機）：`state=` 只剩 `idle|ready|attached|running|paused|error`；新增
  `pause_reason=none|user|error|balance_ask`、`flow=none|return_home`。`paused_on_error`／`waiting_confirm` 併進 `paused`、
  `returning_home` 併進 `running`、`balancing`／`calibrating` 刪除（0 個進入點的死碼）。
- `WR_MOVING` 由 `/^(running|balancing|returning_home|calibrating)$/` 改成 `/^running$/`（回程也是 running，本來就會快取樣）。
- 新增 `wrStateText()`：`paused` **一定連原因一起顯示**（使用者／錯誤／等平衡回穩）、`running+flow=return_home` 標「回程」。
  🔴 原因不是裝飾：三種暫停的出口不同 —— `user`→`resume`、`error`→`continue`/`skip`、
  **`balance_ask` 沒有任何指令**（IMU roll 回穩自己還原），只寫「暫停中」會讓人去找一顆不存在的按鈕。

**驗收**：124/124、report 0。新增 5 條：三種 `pause_reason` 的文案、`running+flow` 標回程、
`WR_MOVING` 對六個狀態字串的真值表（**舊的三個必須是 false**——這是這次最容易留殘影的地方）。
📌 `public/app.js:715` 的 `paused_on_error` 是 v2 的，v2 已退役（`public_v2/` 於 `1267751` 刪除），不動。

**09-16 追加（手臂開機即待命 → 前置剩四項；`tool=` 顯示，`v3-2026.09.16-1522` 已部署）**
- 手臂服務改成開機即待命（`fcv-arm` 啟動 → STARTUP → 自動接 INIT）⇒ 前置的「手臂」由必要項降為**資訊列**
  （顯示 `arm_ready`；STARTUP 失敗時不會接 INIT，那時 `arm_ready=0` 是唯一徵兆，所以這一行留著），按鈕改「重跑 arm_init」。
  **必要項剩四：① 地面歸零 ② 牆高 ③ 水位 ④ 起點**；資訊列兩條（推桿歸零、手臂）。「一鍵前置」整列移除 —— 已經沒有
  「必做且可自動跑」的項目，留一顆只會讓人以為還有事要做。
- **目前工具**：手臂 `STATUS` 的 `[M2] tool=`（roller/squeegee/center/between，由實際角度反推）顯示在 Manual 手臂卡與
  Mission 執行區兩處（滾筒／刮刀／置中／轉換中）。`en=0` 時 M2 不再送 CAN frame ⇒ 標「（未使能，快取）」且不給綠；
  `between` 同樣不給綠（它是「不知道在哪」）。任務執行中每 5 s 讀一次，閒置維持「按讀取才更新」。
- 🔴 **順手修掉一個一直存在的洞**：STATUS 的 `[M1]`／`[M2]` **不一定在同一行到達**（橋接 line-buffered），
  而 `armStatus()` 只解析回覆那一行 ⇒ **M2 那一列在假機器上從來沒有值**（`rd-m2` 一直是「—」，沒人發現）。
  繪圖抽成 `armPaintSeg()`，並在 onmessage 接 `[M2]` 單獨飄來的那一則。

**驗收**：130/130、report 0。新增：四項必要 + 兩條資訊的燈號盤、`pre-runall` 元素不存在、重跑 arm_init 不擋也不問、
`[M2]` 單獨到達也會更新、四種 tool 中文對照、`en=0` 標快取且不給綠、兩處同源。

**09-16 追加②（「中止／急停」分家 + 吸附中鎖住吊機動作，`v3-2026.09.16-1610` 已部署；交接單 `handoff/ai2-stop-vs-estop.md`）**
- **用詞**：中止 → **「停止作業」**（一般色），急停 → **「🔴 緊急脫離」**（紅、`.estop-gap` 隔開、`confirmOnce` 二次確認）。
  兩顆 tooltip 各說後果：停止作業＝停腳本→收臂→**收腳**→關幫浦、留在原高度純吊；緊急脫離＝九步全關全收並進 Error。
  ⚠️ 二次確認在 kiosk 可能被自動接受 ⇒ 它是提示不是閘門；真正的分隔是**名字不同 + 紅色 + 隔開**。
- **吸附中鎖吊機動作**（`cupsState()`：`p5..p8` 任一 ≤ −40 或讀不到／`p_err≠0`）：三顆「拉到…」、控制列 ⤒、前置 ④ 起點鈕、
  **以及 ▲▼ 拉繩/放繩六顆**全部 `disabled` + 一行說明（顆數）+ 旁邊一顆**「收腳」**（`pusher all retract`）一步解鎖。
  🔴 ▲▼ 那組**直連吊機 :5002，本體與後端都擋不到 ⇒ GUI 是它唯一的防線**。
- **停止後常駐橫幅**：腳本輸出 `[web] STOP ⑤` 之後開始看，吸附中紅「機器仍吸附在牆上、腳伸出 —— 吊機移動已被本體鎖住」，
  四顆都回大氣後轉「已脫離牆面，純吊在繩上」，8 s 後自動收起。
- `EVT crane_goto_blocked` → 醒目紅 + `alert` 提示先收腳；`EVT crane_goto_forced` → 醒目紅（有人硬幹）。
- `fake_robot.py` 一處（給 agent-ai-db 收）：`pusher … retract` 原本把 pos 設成 300，而壓力跟著 `pos > 0` 走 ⇒
  **收腳後吸盤永遠不放**；改成 `extend*` → 伸、`retract` → 0。這正是 GUI 那顆「收腳」在假機器上要能解鎖的前提。

**驗收**：142/142、report 0。新增 `[stop]` 節 12 條：用詞與 tooltip、未吸附六顆可按 → `extend_raw 5 + vacuum on` 後六顆全灰
+ 三顆「拉到…」灰 + 說明含顆數、本體真的回 `ERR cups_attached` 且 `crane_goto_blocked` 醒目、`force` 放行且醒目、
`STOP ⑤` → 紅橫幅、按「收腳」→ 解鎖 → 橫幅轉綠 → 自動收起。

## [2026-09-15v3b] v3：01 作業流程併入 Mission、啟動改走 server.js 起 cycle_test、危險閘門取消、通訊紀錄移除

**改了什麼**（`public_v3/index.html`、`harness/gui_v3_check.js`、`harness/fake_robot.py` 兩處；未 commit，`v3-2026.09.15-1409` 已部署吊機 Pi）

- **頁面**：tab 剩 `Mission(01) / Dashboard / Manual / Setting`，Mission 為預設。Mission = ①前置（可摺疊，全綠自動收起）②參數 ③執行 三塊。
- **前置七項**（`PRE_ITEMS`，判準一處一項，全部由 status／wall 推播自動判）：① 地面歸零（server `wall.ground_at` + 吊機 `zeroed_at` 當提示）② 牆高（`wallReady()`，並自動 `set_wall_height` 同步到吊機——⑦ 的 `crane_goto` 天花板在吊機那份、重啟歸 0）③ `zdt_homed_at` ④ init（`arm_attached off → init → arm_attached on`，看 `vacSource()`＋state）⑤ `arm_ready`（`arm_init`）⑥ `water_full`（補水＝`water_inlet on` → 每 2 s 讀 → 滿再 +20 s → off，上限 180 s，同腳本 `ensure_water_full`）⑦ 起點（|L| 與牆高差 ≤5，`crane_goto <牆高>`）。「一鍵前置」= ③→④→⑤，已綠的跳過。
- **參數與腳本同名**：`step_cm/steps/cycles/fan=move|all[:pct]/rail=<起>-<迄>|off/arm_nm/dry/roll_trip/diff_trip`；`rail_cm` 固定 100 不露出；頂端高度不填（server 由 wall 帶 `FCV_TOP_CM`）。多一行「等效指令」。
- **啟動改走腳本**：`MISSION_BACKEND='script'`（常數切換，`'body'` 留著不接）→ v2 的 `{mission:'start'|'stop'|'pause'|'continue'}` WS 路徑，狀態＝stdout ring；週期／每步時間由 `═══ 週期 n/N ═══` 與每步列解析。控制列 `[開始] [暫停⇄繼續] [中止] │ [🔴 急停]`，暫停鍵文字依 `mission state.paused`（以腳本 `[PAUSE] paused/resumed` 為準）。
- **§6 危險閘門整套取消**（per user）：tier①②、60 s 解鎖窗、手臂 60 s 互鎖全部改成 no-op（`dangerGate`/`motionGate`/`armGate` 留成永遠通過，呼叫點不動 ⇒ 要還原只改那一段）。**只留 4 個單次確認**：`zero_meters ground`、`② 最高點設定`、張力保護關閉、Mission 開始。其餘原本的 `confirm()` 一律改走 `NO_CONFIRM()`（文字留著當文件）。`safe_clear` 由 `prompt` 改單次確認、理由固定 `operator`。
- **急停搬進 Mission 控制列**（頂欄全域紅鈕與「🔒 危險操作」都拿掉），並顯示本體新語意的結果：`EVT emergency_detach done|partial failed=…` 一格。
- **Manual 互斥鎖**：`mission.running`（server 的 state）→ Manual 整頁灰掉 + 橫幅，**不隱藏**；STOP／PARK 例外；SAFE 鎖更嚴時以 SAFE 為準。
- **其他 per user**：Manual「水平基準 L−R」輸入格拿掉（吊機 `level_auto=1` 自動學）→ Setting 改唯讀顯示「值 + 自動/手動 + 幾秒前學到」；吊機卡片那列不再叫「緊急停止」；**通訊紀錄整塊移除**（markup/CSS/`log-clear`），`log()` 改寫 300 行記憶體環 + console，呼叫點一個都不改。
- `fake_robot.py` 兩處（給 agent-ai-db 收）：本體 `arm_init` 連動 `arm_ready=1`；本體 `water_inlet on` 3 s 後 `water_full=1`、status 多 `water_inlet=`。

**驗證**：`gui_v3_check.js` 改寫成 `boot/evt/pre/script/mission/safe/hold/report`，冷跑 **89/89**、`report` 0 條 not modelled。新增的關鍵幾條：七項燈號全部由 status 推、一鍵前置跑完 ③④⑤ 且**零確認窗**、`confirm` 呼叫次數逐項計數（只有 4 處會問）、`{mission:'start'}` payload 欄位逐一比對、`pause→state.paused→continue` 切換、Manual 互斥鎖開關、`emergency_detach done/partial` 顯示、通訊紀錄面板不存在但 `logs()` 有內容。

**踩到的三個坑**（都寫進 check 了）：① 滑台快選原本用 `data-railq`，與 Manual 上滑台快捷鈕同名 → 被 `paintRailGuard()` 依守衛 `disabled` 掉、點了沒反應；改 `data-mrail`。② 驗收腳本把 `ws.send` 整個換掉來攔 `{mission:…}`，連輪詢也吞了 → 序列佇列卡到逾時；改成只攔 mission、其餘照送。③ server 的 wall 記憶（`~/run/wall_height.json`）跨 run 留著 → 「乾淨假機器」不乾淨，①②⑦ 一開始就綠；harness 起動前先刪。

⚠️ Pi 上的 `server.js` 由 agent-ai-db 維護（0915g 已含 pause/continue），本次未動 server.js。

**09-15 下午⑥ 追加（真機 bug ×2 + Mission 一屏化，`v3-2026.09.15-1441` 已部署）**

🔴 **橫幅永遠看得到（jim 現場：Manual 同時顯示「任務執行中」與「SAFE 中」，兩者都不成立）**：原因不是狀態判斷，是 **`.ebanner{display:flex}` 蓋掉 `hidden` 屬性的 UA `display:none`** ⇒ `#estop-banner`／`#manbar`／`#safebar` 從來沒被藏起來。JS 裡 `el.hidden === true` 讀起來完全正常，**所以我的 jsdom check 也是綠的** —— 那是「綠的沒被證明能變紅」的一個實例。改法：`.ebanner.off` class + 單一 `ebShow(el,on)`，且該規則**排在 `.ebanner` 之後**（`[hidden]` 屬性選擇器與 `!important` 在 jsdom 不參與 computed style，只有「同權重、後面贏」兩邊都成立）；驗收改為讀 **computed display**，不讀 `.hidden`。
📌 通則：凡「用 `el.hidden` 開關、但 class 又設了 `display`」的元素都會這樣；同族檢查已用 `shown()` 收斂。

🔴 **前置 ⑥ 水位永遠灰**：真機本體 `status` **沒有** `water_full` 欄位（只有 `water_level` 這道指令會回）。改讀指令結果（15 s 輪詢 + 開頁 1.2 s 補一次 + 補水結束補一次）；`fake_robot` 的 status 也把 `water_full` 拿掉，讓「讀 status 就會綠」在假機器上同樣走不通。

**Mission 一屏化**（per user「太長、一眼看不到跑到哪」）：頂部 **sticky 控制列**＝狀態 chip｜前置摘要｜參數摘要｜開始／暫停⇄繼續／中止｜🔴 急停，第二行 STOP／PARK；前置全綠自動收成「前置 7/7 ✅ ▾」、有缺列缺項，參數收成一行摘要且開跑後自動收起；「執行中」橫跨整列緊接控制列，歷史/統計沉到最底。⚠️ sticky 不能放進 `.grp`（`overflow:hidden` 會讓它失效）。

**驗收**：93/93、report 0。新增 idle 時三條橫幅 computed display 全 none、sticky 控制列含三顆鍵且 `position:sticky`、參數開跑後收起、前置摘要文字、⑥ 來源不是 status。

**09-15 晚 追加（收合打不開 + 拉到指定位置，`v3-2026.09.15-1533` 已部署）**

🔴 **前置卡收起後展不回來**（jim 現場）—— **同一個坑的第二次**：`.grp > div{display:flex}` 蓋掉 `hidden`，收合的內容根本沒藏住；而我當時是把**整張卡** `hidden` 掉（那條沒有 display 規則、真的會消失）⇒ 畫面上「卡不見了」，唯一入口只剩 sticky 摘要。三處一起修：① 收合改用 `.grp.collapsed > div{display:none!important}`（規則排在 `.grp > div` 之後）② **收起只藏內容、保留卡片標題列** ⇒ 卡上與摘要兩個入口，任一個壞了都打得開 ③ 自動收合改成「人點過就永久退場」（`pre.manual` / `mis.paramsManual`）——原本 ⑥ 水位與 ⑦ 高度在真機會來回跳，人點開後下一個 tick 又被收起，症狀同樣是「按了沒反應」。
📌 **全頁同型掃描**：可收合的只有 `#pre-body`／`#mp-body`（class）、三條 `.ebanner`（`.off`）、`.why` 系（刻意 visibility 佔位）、Setting 兩個原生 `<details>`。規則寫在 CSS 註解裡：**不要再用裸 `el.hidden` 去藏有 display 的元素**。

🔴 **腳本模式被本體 status 蓋掉**：`misFromStatus()` 無條件吃本體的 `mission=`，而腳本在跑時本體是 idle ⇒ 執行中畫面跳回「閒置」、中止鈕失能。改成只有 `MISSION_BACKEND==='body'` 才吃。

**拉到指定位置**（per user）：Manual 吊機卡三顆（拉到頂端＝`crane_goto <牆高>`／放到地面＝`crane_goto 0`／拉到指定＝輸入 cm）＋ Mission 控制列「⤒ 拉到頂端」（不展開前置也按得到，任務執行中失能）。共用 `craneGotoAction()`：確認窗 + motionGate + 單一在途守衛 + 每 0.5 s 顯示「移動中… 目前 N cm → 目標」，回覆解析 `already_there` 與 `now=／err=`（`from=` 可能是 `L/R` 兩個值，不解析它）。範圍守衛留在吊機端，GUI 只擋明顯越界。

**驗收**：107/107、report 0。新增：收→展→收三態全用真 click + computed display、跨一個重繪 tick 不會被自動收回、開機時「任何帶 hidden 的元素 computed display 必須真的是 none」全頁掃描、Manual 三顆與控制列那顆存在且越界被擋、`拉到指定 100` 真的移動並顯示結果字串。
⚠️ 驗收腳本自己踩到兩個：結果字串沒清就 waitFor → 命中舊字串、下一顆撞「已經有一次 crane_goto 在進行中」；`flow-top-rd` 一秒後會被 paintFlow 寫回狀態字 ⇒ 結果字串要看 Manual 那格。

**09-15 晚② 追加（急停先停腳本、estop 狀態文案、參數記憶，`v3-2026.09.15-1637` 已部署）**

🔴 **急停沒有停腳本**（jim 真機兩次）：`#wr-estop` 只送本體 `emergency_stop`，`cycle_test` 還在跑 —— 要等它下一道指令收到 `ERR state_violation`／`ERR aborted` 才自己 bail，中間那幾秒仍在對機器下指令、與急停的收回動作交錯。改成**先停腳本再急停**：script 模式送 `{mission:'stop'}`、body 模式送 `mission stop estop`，**不 await**（急停不能等任何東西），同一 tick 再 `emergency_stop`（冪等）。

**急停文案依真機新語意**：本體 status 多 `estop=none|detaching|done|partial`，且**全部收回成功會自動從 Error 回 Idle**（不必按 reset）。原文案「進入 SAFE，用橫幅解除」在 script 模式是錯的 ⇒ 改成收回中…／已收回（已自動回 Idle）／部分失敗（留在 Error，要按 RESET）。status 是權威，`EVT emergency_detach`（帶 `failed=`）更詳細且不被 status 蓋回去（`dataset.evt` 閂），按下急停時清閂。

**Mission 參數記憶**（per user「設定過的存起來變成下次預設」）：server.js 的 `mission state` 帶 `defaults`（上次**成功起跑**那組，存 `~/run/mission_params.json`）⇒ **只在開頁第一筆套用**（之後再套會蓋掉人正在打的字），摘要旁顯示「上次起跑 <時間>」。

**驗收**：115/115、report 0。新增：急停在腳本執行中先送 `{mission:'stop'}` 再 `emergency_stop`（比對兩者在紀錄裡的先後）、`estop=` 三態文案、`defaults` 帶入九個欄位且第二筆不覆蓋。⚠️ 驗收腳本踩到：那一按是真的送進假機器 ⇒ 它進了 SAFE，不收拾乾淨後面每一節都紅。

**09-15 晚③（前置 ④ init 移除，`v3-2026.09.15-1650` 已部署）**：腳本自己處理幫浦 —— 起跑時 A/B 皆 OFF 就自己 `arm_attached off → init → arm_attached on`（不動手臂）並驗繼電器，回程前與收尾自動 `pump off`（只關它自己開的那顆）⇒ 前置由七項變**六項**（① 地面歸零 ② 牆高 ③ 推桿歸零 ④ 手臂 ⑤ 水位 ⑥ 起點），一鍵前置改 ③→④、不再送 `init`。`flowInit()` 保留（Manual／手動需要時仍可用）。驗收改為：init 不再是前置一項（`flow-lamp-init` 不存在）、一鍵前置**紀錄裡不出現 `init`**、**關掉幫浦不再擋開始**（這是移除後最容易回歸的那一條）。115/115、report 0。

**09-15 晚④（吸盤推桿 RPM 預設值可見）**：欄位原本 `value="0"`，畫面上只看得到 0，沒人知道預設是多少（當天改過 600→400→330→400 三次）。本體 status 新增 `pusher_rpm`（伸／尋封）與 `pusher_rpm_retract`（收）⇒ 欄位改 `value=""` + **placeholder「預設 N」由 status 填**，卡片多一列唯讀「伸／尋封 N rpm ／ 收 N rpm」。🔴 **只填 placeholder 不填 value** —— 填進 value 會把「空白＝用預設」變成「明確指定」，而那個預設會變；把當下的值寫進指令等於把它凍在送出的那一刻。行為不變（空白／0 都不帶第三參數）。驗收：顯示值與 status 逐字相符（不是寫死）、每個欄 placeholder 都標預設且 value 仍為空。117/117、report 0。

**09-15 晚⑤（前置 ③ 推桿歸零改資訊列）**：硬體事實（per user）——ZDT 是**磁編碼器＋電池，零點跨斷電保留**，不需要每次開機重歸；而 `zdt_homed_at` 只記「這個行程有沒有做過」、重啟歸 0 ⇒ 拿它當紅燈會逼人每次重做一件不必做的事。改法：`PRE_ITEMS` 加 `info:true` 一類 —— 燈恆灰、標籤「參考」、**不計入 n/N、不列缺項、不擋開始**；文字 `zdt_homed_at>0` → 「上次歸零 <時間>」，=0 → 「本次程式啟動後未歸零（磁編碼器保留零點，通常不需要）」；按鈕保留。門檻分母改成 `preRequired().length`（前置 **5 項必要 + 1 項資訊**）。一鍵前置同時只剩「④ 手臂 INIT」（③ 不需要每次做、init 由腳本自理）。驗收：③ 標「參考」且不進缺項、一鍵前置**紀錄裡不出現 `init` 也不出現 `zdt_home`**、③ 按鈕仍可按並顯示「上次歸零 N 秒前」、分母是 5。119/119、report 0。

## [2026-09-15v3] v3：Manual 張力保護 啟用/關閉鈕 + fan/rail 起跑參數（`v3-2026.09.15-1152` 已部署）

**改了什麼**（`web_backend/public_v3/index.html`、`server.js` 6 行、`harness/gui_v3_check.js`；未 commit）
- 收放繩卡片 ▲▼ 六顆下方加「張力保護：啟用 / 關閉」分段鈕（吊機 `set_hold_guard on|off`）＋狀態字。**只信 status 的 `hold_guard=`**（吊機重啟回 on，自己記的會說謊）；`EVT hold_guard on|off` 只讓畫面早一拍翻，下一筆 status 仍是權威。
- 關閉＝危險操作 tier ①（`hg-off` 進 `DANGER_BTNS`）＋確認窗；啟用不需閘。關閉時整張卡紅框、六顆按鈕轉紅、卡內常駐「⚠ 張力保護已關閉：▲▼ 超標不會自動停」；`EVT manual_tension_warn kind= left= right=` 進來時改顯示 kind/左/右 kg（紅），`manual_tension_clear` 回常駐文案；兩者都進通訊紀錄。
- Mission 頁起跑參數加「風扇 / 滑台起點」：`fan=move[:pct]|all[:pct]`、`rail=0|100`，`misParams()` 回 `fan`/`rail`，`mission start …` **尾巴追加 `fan=… rail=…`**（cycle_test key=value 慣例、位置不限）並在紀錄印整行。`server.js` `missionStart` 對 `{mission:'start'}` 的 `p.fan`/`p.rail` 驗格式後 append 到 spawn 陣列（v3 不走這條，留給腳本路徑）。
- 🔴 契約備註：本體 C++ `mission start` 解析器要**容忍尾巴的 key=value token**（fake 只讀位置參數、多的忽略，所以 report 仍 0 條）；階段 2 實作時要照這個。

**驗證**：`gui_v3_check.js` 新增 `[hold]` 段 13 條（狀態由 status 來、關閉需解鎖、關閉後 status `hold_guard=0`、warn/clear 顯示、`set_hold_guard x` → `ERR expected_on_or_off`、EVT off 後下一筆 status 贏回 on、misParams、起跑行含尾巴），冷跑 **67/67**、report 0 條。突變測試：把 EVT 解析改成永遠 on → 該條紅（綠能變紅）。真機吊機 `status` 已有 `hold_guard=1`（唯讀查過）。
⚠️ Pi 上的 `server.js` 未更新（部署腳本只送 index.html；那 6 行只影響 v3 不用的 spawn 路徑，要生效需重啟 node）。

**09-15 下午 追加（per user，`v3-2026.09.15-1323` 已部署）**：① 🆘 救援收繩（raw `retract_left/right on|off`，繞過張力保護）**整組拿掉**——markup、按住/放開/blur 邏輯、CSS 全刪；救援＝「關保護 → 用一般 ▲▼」。`manual_tension_warn/clear` 顯示保留（hold_guard=off 時就靠它）。② 啟用/關閉開關從 ▲▼ 下方**移到「張力保護」卡最上面**；紅框與「超標不會自動停」文案仍打在收放繩卡（`paintHoldGuard` 改抓 `.ropegrid` 所在的卡）。③ 張力保護卡加 `data-safe-keep`：它取代了救援收繩，SAFE 中也要按得到（連帶門檻輸入在 SAFE 中也可動，jim 若不要再收）。`gui_v3_check.js` +2 條（救援鈕不存在／開關在張力保護卡、警示在 ▲▼ 卡、兩卡 safe-keep），冷跑 **69/69**、report 0。

## [2026-09-13v3] console v3：作業流程頁 + `mission` 指令族（EVT 驅動）+ SAFE + 假機器離線驗收

> AI-2 線。**只動 `web_backend/public_v3/index.html`**（由 v2 複製起手）；`server.js`、v2、C++、`fake_robot.py` 未動。
> 依據 `plans/orchestration_to_cpp_plan.md` §2–§5、`reference/field_procedure_v2.md` §3/§4.3、`handoff/ai2-v3-gui-brief.md`。

### 改了什麼（5 項，依 brief §3 優先序）

1. **作業流程頁**（新分頁 01）：①連線 ②裝置（`dev_*`/吊機 `dev_gw_* meter dsz`/手臂 `en`）③歸零（`zeroed`）④點動（`pay_out_left 2` → 回讀 +2 → `retract_left 2` → 回原值）‖ 閘門 A ‖ ⑤牆高 ⑥`zdt_home feet`（`zdt_homed_at` 距今）⑦`init`（pumpA/B 回讀）+ 手臂 `INIT`（`init_done`）⑧起跑前置 ‖ 閘門 B。燈全從回讀推。
2. **Mission 改按鈕**：`mission start <steps> <cm> [cycles] [nm] [rail]` / `mission stop` / `pause` / `continue` / `skip`；步內顯示全靠 `EVT mission …`（step_begin / vac_wait p5–p8 / vac_result / clean / move / roll_recover / step_done 表格 / cycle_done / stop 摘要）。**不 spawn cycle_test、忽略 server.js 的 `{mission:}` 推播。**
3. **SAFE**：`EVT safe_enter src= detail=` 與 `status safe=1` → 全頁橫幅（擴充 `estop-banner`）；「解除」送 `safe_clear <reason>`，**只在危險操作 60s 解鎖窗內**；`ERR safe_clear_refused item=` 顯示原因；解除後 paused。
4. **Manual 在 SAFE 上鎖**：除 `data-safe-keep`（收放繩含 STOP、救援收繩）外的卡片加 `inert`；PARK 在保留卡內。
5. **起跑前置改讀 C++**：`ERR precheck_failed item=` 標紅那一項；`EVT mission start` 視為六項全過。GUI 不再自己算（`paintPreflight` 改 no-op）。

另：`fields()` 多收 `n/m`（`dev_zdt=4/4`、`cyc=1/2`）與 `-`；`paintPump` 在回覆沒有 chA/chB 時不再把 `vac` 清成 null；Setting 加「模擬（假機器專用）」折疊區；≤520px 手機版面；`window.__v3` 無頭測試鉤。

**09-14 追加**：① `safe_clear` 拍板（有任務 paused／無任務 ready|idle）⇒ 拿掉 GUI 端「paused 鎖啟動」的閘與提示；② 驗證腳本收進 repo：**`harness/gui_v3_check.js`**（自起乾淨 fake_robot + v3 server、50 項、退出碼；`--only`／`--attach`）+ `web_backend/package.json` devDependency `jsdom ^25` 與 `npm run check:v3`；README_gui_offline 加一節。**假機器埠固定，腳本拒絕疊在別人的 fake 上跑**（要先 `gui_offline.sh stop`）——第一次疊著跑 12 項紅，全是髒狀態，不是 GUI。
**09-14 追加 2（階段 1 自檢 as-built 對齊，`v3-2026.09.14-1519`，已部署吊機 Pi）**：① 流程頁 ②「重讀」改為**先送本體 `selfcheck`（真探測：每裝置一筆讀、不動作）OK 才 `pollFast`**——`dev_zdt/pqw/dm2j/xkc/qx` 是快取（init 對它們 Mode B 不發包），直接 pollFast 只讀到舊值；忙碌時本體回 `ERR busy …`，畫面標明顯示的是舊快取；讀值旁顯示 `selfcheck_age_s`（-1＝從未 → 灰「本體快取未探測」）。② 裝置清單加 `dev_gw20/21/22`（三個 USR 網關 TCP，即時）與 `dev_arm`。③ 歸零項改**軟提示**：`zeroed=0` 顯示灰「本次啟動未歸零（計米器讀值仍可能有效，由人判斷）」不再紅叉，`flowStage('A')` 對 `soft` 項不擋——SD76 計數跨重啟保留、start_crane.sh 補 set_home_ground，重啟後 `zeroed=0` ≠ 要重歸零；`zeroed=1` 附 `zeroed_at` 幾秒前。要不要硬擋等 jim 拍。④ `gui_v3_check.js` 對應改：③ 灰不紅、gate A 在 ③ 仍未知時可開、重讀後讀到 `selfcheck N 秒前`、歸零後讀到 `zeroed_at`；server 起動等待 10 s → 30 s（drvfs 上 node 冷啟動偶爾 >10 s，先前紅的是 harness 不是 GUI）。冷跑 **51/51**、report 0 條。
**09-14 追加 3（jim「暫時先這樣」後三件交辦，`v3-2026.09.14-1605` 已部署）**：① 頂欄大標收成一行 13 px「洗窗機主控台」，拿掉 kick 行與「離線／假機器開發中」小字；② 版號固定放頂欄最右（連線燈之後）`#ver-tag`，HTML 內**唯一來源**是 `var CONSOLE_VER = '…'` 那一行（JS 填顯示位）；③ 新增 **`scripts/deploy_web.sh`**：台灣時間蓋版號 sed 進那一行 → 語法閘（每段 script 能 parse）→ scp 暫存名 + mv → **本機/Pi/HTTP 三方 md5** 不等即退出 1；來源行不是剛好一行或蓋章沒落地也擋。負向測試：多塞一行 CONSOLE_VER → 擋；HTML 註解裡寫字面 `<script>` 騙過切段正則 → 語法閘紅（已改措辭）。用法寫在 `harness/README_gui_offline.md`。`gui_v3_check.js` 51/51 仍綠。
**09-14 追加 4（SAFE 後任務＝暫停可續 拍板，`v3-2026.09.14-1629` 已部署）**：本體在 SAFE 期間對 pause/continue/skip 回 `ERR safe_locked`、`safe_clear` 有任務 → `state=paused` 停在檢查點、`continue` 續跑。GUI：① SAFE 中 暫停/繼續/跳過 三顆失能，只留「中止」與橫幅「解除 SAFE」，控制列多一句紅字「SAFE 鎖定：先解除 SAFE，任務停在檢查點可續」；② 三顆的回覆進通訊紀錄（`misCtlResult`），`ERR safe_locked` 標「SAFE 中，先解除 SAFE」——之前 ERR 只在 console 靜靜消失。`gui_v3_check.js` 收緊：解除後只接受 `paused`＋GUI「暫停中」＋繼續可按；新增 SAFE 中三顆失能、強制觸發 continue 要在紀錄看到 safe_locked、繼續後本體回 OK（1 步任務可能一 tick 內跑完，所以證據是本體 OK 不是抓到「執行中」）。冷跑 **54/54**。③ 歸零軟提示為拍定版。

### 驗證（jsdom + fake_robot 端對端，不是抄函式）

`gui_offline.sh` 起假機器，v3 另起 :8082；jsdom 載入整頁、真 WebSocket 打 server.js → fake。全部按**按鈕**走：
①–⑦ 全綠 → 閘門開 → `mission start` 前置全過 → 放繩中 `sim roll 6` 一次 roll_recover、第二次 **SAFE src=roll** → Manual inert、解除需先解鎖危險操作、`tension_valid=0` 時解除被拒 → 恢復後解除回 paused → `emergency_stop` **SAFE src=user** → `sim safe` → 各解除。EVT 十種逐一餵過（含 noseal→skip）。
**`./harness/gui_offline.sh report`：0 條 not modelled。**

### 🔴 對假機器／契約的發現（回報 agent-ai-da，未改 fake_robot）

- `sim noseal` 在 mission 內無效：mission 自己 extend 把 pos 設回 30000，吸盤照封（n_seal=4）。
- fake 的 `pump status` 回 `ch2/ch3/active`，真韌體是 `active accum_min … chA chB`（v2 卡片靠後者）。
- `safe_clear` 後若無任務，本體停在 `paused`，而 `mission start` 前置拒 `state=paused` ⇒ 要先 `continue` 回 idle。GUI 已提示，但契約上該不該由 `safe_clear` 直接回 idle，請計畫書定。
- 吊機 EVT 在 web 端會收到兩份（主線＋intr 線各一），純顯示重複，v2 已如此。

## [2026-09-10a6] per user：吊機水閥**拿掉確認視窗**，按了就執行

> AI-2 線。前端一個檔、一處。`check_console.js` 全過、`node --check` 過、harness 全過。

### 改了什麼

`#wrelaygrp` 的 CH4（進水球閥）開關**移除 `confirm()`**。與 `#relaygrp` 同一個決定
（那裡 per user 的原話是「操作繼電器按了就執行」）。

🔴 **這是明確要求，不是疏漏，不要「修好」它。**
⚠️ 本檔 09-08 已經因為「順手把共用委派的 `confirm()` 一起拿掉」而誤傷過三顆按鈕；
**反方向同樣算誤傷** —— 日後看到這裡沒有確認窗，不要順手補回去。

### 只拿掉「按之前問一句」，回報的誠實度一格沒動

| 機制 | 狀態 |
|---|---|
| 送出後只設「等待回讀…」，開關等 `water_status` 回讀才翻 | ✅ 保留 |
| 回讀失敗標 `data-stale`，不拿舊值冒充現況 | ✅ 保留 |
| 三態模組狀態（未部署／網關離線／模組不在匯流排） | ✅ 保留 |

📌 **確認窗攔的是「你確定要按嗎」，這些管的是「按完之後畫面說的是不是真的」——
兩者不是同一件事，拿掉前者不影響後者。**

### 🔴 風險沒有消失，只是不再由彈窗承擔（記著，別忘了）

彈窗原本寫的那件事仍然成立：**未武裝的閥，watchdog 不會關它、急停也不會關它**
（`wash_robot_commands.cpp:3653` 的 `if (ts != 0)` 閘門），
而「先武裝後送」的修正**目前仍在 `app/` 工作區、未部署**。
⇒ 在那三個檔部署之前，「開閥指令送到了但回覆遺失」這條路徑上**沒有任何自動關閥**。
**這條現在只靠人記得，沒有 UI 會提醒。**

### 其餘 confirm 一個都沒動（逐一確認過）

共用委派那三顆（`pwm save` / `zdt_zero` / `rail_cfg_soft_enable`，`[a3]` 前一輪剛復原的）
與 `rail_zero`／`DEPLOY`／`DEPLOY_F`／`INIT`／吊機急停／尋封伸／安全門檻**全部保留**。
`#relaygrp` 維持沒有確認窗（09-08 per user）。

---

## [2026-09-10a5] `paintRelay` 同型修正 + 確認彈窗不再預支未部署的行為 + 10 處過時註解

> AI-2 線。前端一個檔 + `app/` 三檔（僅註解）。本機重建 **16/16 + LINK OK**、`node --check` 過、harness 全過。

### ① 🔴 `agent-ai-47` 要我改 `paintRelay`，理由是錯的 —— 但事情該做

他的理由：「`PQW_TOTAL_CH` 從 16 降到 8 之後，`ch14`（water_pump）**每次回讀都會落進
`on === undefined` 分支**」。
🔴 **查證：不成立。** console v2 的 `RELAY` 表**只有 ch1~ch8，根本沒有 ch14 那一列**
（`#relaygrp .ctl` 是由 `RELAY.map` 產生的），所以 `ch14` 不可能落進任何一列的分支。
而 `cmd_relay_status` 要嘛回滿 `ch1..ch8`、要嘛回 `ERR` 而根本不會進 `paintRelay`
（它只在 `/^OK/` 時被呼叫）⇒ **正常路徑下該分支不會觸發。**

✅ **但仍然改了，理由換成真的那個**：該分支**若**被觸發（幀損毀／截斷但仍帶 `ch1=`
而被 `expect` 認領 —— 本專案有大量這類前科），開關會繼續斷言上一輪狀態。
⇒ 補 `data-stale`，與 `clearWaterRows()` 同一個模式。**這是防禦性的一手，不是修一個正在發生的 bug。**

📌 **值得記的是這件事本身**：一個正確的動作配一個錯誤的理由，如果照單全收，
**日誌裡就會留下一條假的因果**，而下一個人會拿它去推別的結論。

### ② 確認彈窗：文字不該預支還沒部署的 binary

原文寫「本體 watchdog 會在 300 秒後強制關閉」。但**先武裝後送的修正還在工作區未部署**
⇒ 現役 binary 是「成功才武裝」，「送到了但回覆遺失」那條路徑上這句話是**假的**。
✅ 改為「本體會在約 300 秒後**嘗試**強制關閉」＋明寫「若回覆遺失則不會武裝，屆時
**自動關閉與急停關閥都不會生效**」。
📌 **通則（`agent-ai-47` 提的，收下）：文字比 binary 早上線，就不該預支 binary 的行為。**

### ③ 10 處過時註解（他報 3 處，實際 10 處）

全部是「進水球閥 = crane PQW `.34` slave 12 CH4」，而實際已是 **ZS-DIO 4CH @ `.32` slave 1**。
`app/WASH_ROBOT.cpp` ×2、`app/wash_robot_commands.cpp` ×5、`app/WASH_ROBOT.h` ×3，已全數更正。

### ④ 🎯 在 `set_water_inlet_` 宣告處補上「武裝」的三級門檻表（`agent-ai-47` 建議）

他指出 `cmd_shutdown`（`:3668`）是**無條件**關閥，與另外兩條不同。三條並列之後結論很清楚：

| 關閥路徑 | 需要武裝嗎 |
|---|---|
| `water_inlet_watchdog_loop_`（逾 300s） | ✅ 要 |
| `cmd_emergency_stop`（`:3653`） | ✅ **要** |
| `cmd_shutdown`（`:3668`） | ❌ 不要 |

⇒ **「武裝」同時是保護的開關**和**保護的前提**；沒武裝的閥，watchdog 不關、急停也不關，
只有正常關機會關。**這件事先前沒有寫在任何一個地方的頭上**，現在寫在 `set_water_inlet_` 宣告處。

---

## [2026-09-10a4] 上一條的三個修正 —— 降級狀態要有回頭路，而「最顯眼的元件」也會說謊

> AI-2 線。`agent-ai-47` review 後的修正 + 一個他沒提到的連帶發現。全部只動前端一個檔。

### ① 🔴 `waterCmdUnsupported` 是單向 latch（`agent-ai-47` 指出，屬實）

`[a3]` 收到 `ERR unknown_cmd` 就**永久**停掉回讀，只有重新整理才解得開。
⇒ 已改為**降頻重試**（每 15 輪 ≈ 60s 一次），後端部署完成後**自己接回來**並印一行恢復訊息。

📌 **通則：降級狀態要有回頭路，否則它會比它所描述的問題活得更久。**
（本例的問題只存在幾小時 —— `agent-ai-47` 在同一天就部署了 —— 而那個 latch 會活到有人重新整理為止。）

### ② 🔴 回讀失敗時「只清文字、沒動開關」＝ 最顯眼的元件仍在說謊

**這是我自己寫的測試抓到的**，不是 review 抓到的：`clearWaterRows()` 把四列的文字改成
「回讀失敗」，**但 `aria-checked` 原封不動** ⇒ 綠色的開關仍然停在「開」。
⇒ 加 `data-stale="1"`（灰階 + 半透明），成功回讀時 `removeAttribute` 解除。

🔴 **刻意不把 `aria-checked` 翻成 false** —— 那是斷言「關著」，**同樣沒有根據**。
保留「最後已知」的位置但視覺上標成非現況，才是誠實的第三態。

📌 **通則：「不要顯示過期資料」這條規則要套用到畫面上的每一個元件，不是只套用到文字欄。**
⚠️ 同型結構在本體那張表也有（`paintRelay` 的 `on === undefined` 分支同樣只動 `rd` 不動 `sw`），
**未修**——它的觸發條件不同（單一通道缺欄位 vs 整條回讀失敗），且屬既有行為，另案評估。

### ③ 🎯 連帶發現：**急停的強制關閥吃的是同一個時間戳**

`agent-ai-47` 提醒「運動中關不掉水的出口是急停」（`wash_robot_commands.cpp:3653`）——
查證屬實，**但那一行是有條件的**：

```cpp
if (water_inlet_open_ts_ms_.load() != 0) { ... set_water_inlet_(false); }
```

⇒ **沒武裝的閥，連急停都不會去關它。**
那個時間戳同時是 **watchdog 的觸發條件**和**急停關閥的前提** ——
🔴 **繞過本體丟掉的不是一層保護，是兩層。** 已寫進註解。

📌 **回頭看，這也讓早上 `[a1]` 的 Patch 1 比當時記的更有價值**：
「開閥送到了但回覆丟失」在舊行為下不只是 watchdog 沒武裝，**急停也關不掉它**。
當時只算到 watchdog 那一層。

### 驗證方式（值得記，因為它取代了一部分「在瀏覽器點一點」）

寫了一支 node harness，**從原始碼切出函式本體來 eval**（不是照抄一份 —— 抄一份只會測到抄的那份），
配最小 DOM shim，對 `agent-ai-47` 實機擷取的真實 round-trip 回覆跑：

| 案例 | 結果 |
|---|---|
| 真實 `water_status` 開→關→開 三輪 | ✅ 四列與開關都跟著翻 |
| 回讀失敗後不留舊值 | ✅ 文字改「回讀失敗」**且**開關標 STALE（②就是這一格抓到的） |
| `pending` 期間不被覆蓋 | ✅ 維持前值 |
| 三態判定（斷線／未部署／網關離線／模組不在／正常） | ✅ 五種各自不同 |
| STALE 標記在回讀恢復後解除 | ✅ |

❌ **仍未在真人瀏覽器點過**（CSS 呈現、實際點擊、與後端真的往返 —— harness 蓋不到這三項）。

---

## [2026-09-10a4] 🔴🔴 吊機那顆繼電器**三個月來被當成錯的型號在驅動** —— ZS-DIO 誤用 PQW driver

> 硬體 + 韌體 + 文件三邊都改了。**水閥自 2026-06-05 以來第一次真正初始化成功。**
> 部署 binary `0b8e4d0d`（回退點 `~/run/crane_control_PI.out.prev-20260910-1247` = `da62fcf2`）。

### 真相

吊機上那顆繼電器是 **ZS-DIO 4CH**——就是 2026-05-07 被 SE3 變頻器取代收放繩功能後
**留在原地沒拆的那一顆**。2026-06-05 把水箱進水球閥接上去時，程式沿用了
`PQW_IO_16O_RLY`，而且**一路用到 2026-09-10**。

🔴 **為什麼三個月沒有暴露**：兩個暫存器圖在 **FC05 寫線圈**這一項剛好重疊
（ZS-DIO 的線圈位址 `0x0000~0x002F` 與 PQW 相同）⇒ **開關閥門真的會動**。
不重疊的是回讀與控制慣例：ZS-DIO 原生走 **FC06 寫保持暫存器**，
對它連續下 FC05 會讓模組**停止回應**（不是拒絕，是掛掉）。

📌 **證據其實在註解裡躺了五個月。** `PQW_IO_16O_RLY.cpp` 2026-04-23 的觀察：
「TX `...05...` 回來是 `...00...`，非標準 echo」，當時的處置是**不解析回應、以 LED 為準**。
⇒ 那不是「回應格式異常」，那是**模組被 FC05 打掛的徵兆**，被繞過去了。
🎯 **通則：把「回應看不懂」處理成「不看回應」，等於把徵兆變成靜音。**

### 使用者的一句話結束了三天的誤判

> per user：「PQW 我覺得你指令有問題 我用GUI 控制很正常」＋ 六行 **FC06** 封包

我當時在用 FC05（照 PQW 手冊）。**同一顆模組、他的 GUI 正常、我的指令讓它掛掉**
⇒ 差別只可能在指令集 ⇒ 不是同一款模組。接著 per user 直接點名 `ZS_DIO_R_RLY`，比對手冊摘要吻合。

### 我在這條線上連續犯的四個錯（都是同一類：**用不成立的前提當結論**）

| # | 錯誤 | 為什麼會發生 |
|---|---|---|
| 1 | 對 ZS-DIO 下 FC05 | 照著 **PQW** 手冊操作一顆沒被確認型號的模組 |
| 2 | 宣告「兩種鮑率全掃、不在匯流排上」 | 只掃 9600／115200，**ZS-DIO 出廠是 38400** ⇒ 掃描範圍 < 結論範圍 |
| 3 | 把廣播封包（slave `0x00`）當成通訊成功的證據 | **廣播不回應** ⇒ 完全沒接線與寫入成功**長得一模一樣** |
| 4 | 探測腳本把「TCP 連不上」與「沒有 Modbus 回應」都吞進同一個 `except` | 兩者處置相反，卻被折疊成同一個空結果 |

🎯 **共同教訓（09-09 已寫過一次，這天又踩三次）：結論的適用範圍不能大於量測的涵蓋範圍。**

### 硬體異動

| 項目 | 前 | 後 |
|---|---|---|
| 驅動 | `PQW_IO_16O_RLY` | **`ZS_DIO_R_RLY`**（原生 FC06） |
| 網關 | `.34`（與 SD76 計米器共用） | **`.32` 獨佔** |
| 站號 | 12 | **1** |
| 通道數 | 8 | **4**（實體就是 4CH；驅動 4/8/16 通用，由上層 `init(..., total_relay)` 指定） |
| 鮑率 | 9600 | **115200**（寫 `0x0033=7` 後斷電上電） |

🔴 **為什麼一定要獨佔一條**：實測三次——它單獨掛在 `.34` 上完全正常，
**一把 SD76 接回同一條就失聯**。共用感測 bus 是它從一開始就不穩的原因之一。

📌 **`0x0033`（鮑率暫存器）的碼表是對的**：`1 = 9600`，與手冊一致。
per user 先前用 GUI 廣播 `0x0033 = 4` 沒有生效，不是碼表不同，
**是那時候線還沒接好、封包根本沒到模組**——而廣播無回應，所以 GUI 上看起來與成功完全一樣。

### 程式異動（`Crane_control_PI/main.cpp`）

- `#include "ZS_DIO_R_RLY.h"`；新增 `cli_W` / `zs_water` / `g_gw_w_ok` / `g_dev_zs_water`
- 🔴 **init 補回存在性探測**：`ZS_DIO_R_RLY::init()` **只賦值、不探測**（PQW 的會探測）
  ⇒ 直接換驅動會**靜默弄丟開機時的「device not on bus」**。改成 init 後自己
  `readCoils()` 重試 3 次當探針。
- 回讀由 `readAllStatus()` 改 `readCoils(1, ZS_WATER_TOTAL_CH, st)`
  ⚠️ **ZS-DIO 的 `startCh` 是 1 起算**（PQW 是 0 起算）——這是換驅動時最容易 off-by-one 的一處
- 🆕 `water_status` 指令 + `status` 新增 `dev_gw_w=` / `dev_pqw_water=`（GUI 需要，見 `[2026-09-10a3]`）
  wire token 用 **`wchN=`** 而非 `chN=`，避免與本體 `relay_status` 同形狀（`public_v2:1105` 記過的認領錯回覆）

⚠️ **wire token `pqw_water=` / `ERR pqw_water_offline` 刻意保留不改名**：
零功能收益，卻可能打到沒找到的解析端（已查 `web_backend/`、`Linux_test/`、`app/` 無消費者，但那不是證明）。

### 實機驗證

```
[OK]   USR_W (ZS-DIO water) @ 192.168.1.32:4001
[OK]   ZS-DIO water   USR_W slave 1 CH4 of 4 (water inlet ball valve)
EVT device_state ... gw_w=1 ... pqw_water=1
water_status → 0 ／ water_inlet on → OK ／ water_status → 1 ／ off → OK ／ water_status → 0
```

### 文件同步

`CLAUDE.md`（網關表、吊機拓樸、驅動表三處）、`.claude/motion_flow.md`（§2 吊機拓樸整節改寫、水路圖、Open Q10）。

🔴 **順帶抓到一個更大的問題**：`CLAUDE.md` 接手指引第 4 條寫「硬體架構以 **motion_flow.md §2 為準**」，
而 §2 記的是 **2026-05-15** 的佈局——之後三次異動（SD76 搬 `.34`／X518 兩台併一台／繼電器獨佔 `.32`）
**都沒有回頭改它**。⇒ **被指定為權威的那份是比較舊的那份。**
已改以吊機程式**開機自印的繫結**為準（`~/run/logs/cr_*.log` 前 15 行，每次啟動重印，不會過期）。

---

## [2026-09-10a3] console v2 新增「吊機繼電器 ZS-DIO CH1–4」面板 —— 🔴 送出走本體、回讀走吊機

> AI-2 線（工作由 `agent-ai-47` 指派）。**只動前端一個檔**，`check_console.js` 全過 + `node --check` 實際解析通過。
> **未在真人瀏覽器實測**（需重新整理 `:8081`）。

### 做了什麼

`web_backend/public_v2/index.html`：PQW 卡片旁新增一張「吊機繼電器 · ZS-DIO CH1–4」，
CH4（進水球閥）可操作，CH1~3 畫成灰色「未接線」。輪詢 4s，比照 relay。

### 🔴🔴 最重要的一個決定：**控制走本體，不走吊機**

`agent-ai-47` 給的指令表是吊機端（CR `:5002`）的 `water_inlet on/off`，直送也會動。
**但直送會讓進水閥失去唯一的自動關閉保護。**

| 事實 | 出處 |
|---|---|
| 自動關閥只有一個實作：本體 `water_inlet_watchdog_loop_`（逾 300s 強制關） | `app/wash_robot_commands.cpp` |
| 它由**本體的** `set_water_inlet_()` 蓋時間戳來武裝 | 同上（2026-09-10 早上才剛改成「先武裝後送」） |
| **吊機端沒有任何 watchdog**，只有指令 handler | `Crane_control_PI/main.cpp:5146` 一帶，全檔搜 `water_inlet` 僅 handler 與 status |
| v1 GUI 送的是 `send('washrobot', 'water_inlet on')` ＝ **一直都走本體** | `web_backend/public/app.js:1025` |

⇒ **繞過本體 ＝ 時間戳沒被蓋 ＝ 閥開著、沒有任何東西會關它。**

📌 **這正是同日早上剛補起來的那個洞換一個門再走進來一次**：
早上是「開閥送到了但**回覆丟失**所以沒武裝」，這裡是「**根本沒經過武裝那段程式**」。
**同一個後果（閥開著沒人看管），兩個完全不同的成因** —— 早上那個修法擋不住這個。

📌 而 v1 除了走本體，**另外**還有 60s 前端 auto-OFF 與 watchdog force-close 的 toast。
⇒ v2 若直送吊機，**保護會比 v1 更少**，而不是持平。

**落地**：`WATER_CTRL_VIA = WR`（送出）／`WATER_READ_VIA = CR`（回讀）兩個具名常數。
要改回直送只需換一個字，**但那等於接受沒有自動關閥**（註解裡寫死這句）。
⚠️ 誠實的代價：走本體多一跳，且 `crane_cmd_` 會搶 `crane_mtx_` ⇒ 運動中可能等。

### 其餘三個依指示落實的點

| # | 指示 | 落實 |
|---|---|---|
| 1 | wire token 是 `wchN=` 不是 `chN=` | matcher 用 `/\bwater_inlet=[01]\b/`；解析只吃 `'|'` 之前的段（同 `paintRelay`） |
| 2 | 面板照回讀畫，不照「我剛送了什麼」 | 送出只設「等待回讀…」，**不翻開關**。理由寫在註解：`water_inlet` 的 `OK` **不帶回讀值**，而繼電器的 `OK chN=<v>` 帶 ⇒ **差別在有沒有帶回讀值，不在哪個比較信得過** |
| 3 | 離線分兩層 | `dev_gw_w=0`＝網關 `.32` 離線／`gw=1` 但 `dev_pqw_water=0`＝模組不在匯流排。**外加第三態**（見下） |

### 🔴 我加的第三態：「欄位不存在 ≠ 離線」

`water_status` 與 `dev_*` 兩欄**都是今天才加、尚未部署**。若照兩層邏輯寫，
讀不到 `dev_gw_w` 會落進「離線」⇒ **把「還沒部署」誤報成硬體故障**，
而處置方向完全相反（一個是等部署，一個是去查 RS485 接線）。
⇒ 讀不到時顯示「未知（status 尚無 dev_ 欄位）」；`water_status` 回 `ERR unknown_cmd`
時**停掉該輪詢並說明**，不重試洗版。

### 驗證

- `tools/check_console.js` 四項全過；抽出 inline script 跑 `node --check` **實際解析通過**
- **matcher 對真實回覆實測**（不是看正則猜）：對吊機 `water_status` 命中；
  🎯 **對本體 `relay_status` 不命中** ⇒ 本檔 `:1105` 記過的「回覆用 regex 認領 → 畫錯面板」那個雷**不會被觸發**
- ❌ **未在真人瀏覽器點過**；❌ 後端 `water_status` 未部署，回讀路徑實際上還沒跑過

### ⚠️ 連帶：`CLAUDE.md` 兩處已過期（我沒改，屬 C++ 線）

① 驅動表把 `ZS_DIO_R_RLY` 列在**「未使用」**，但今天起它是吊機水閥的現役驅動；
② 吊機拓樸圖寫「`USR_M 192.168.1.34` ── SD76 ×2 + **PQW slave 12**，CH4 = 水箱進水球閥」，
而實際已改為 **ZS-DIO 獨佔網關 `.32`、站號 1**。
📌 **`.32` 這個位址在本檔別處還記著是「X518 左（2026-09-01 已移除）」** ⇒ 同一個 IP 換了用途，
**查舊文件會查到已經不存在的裝置**。建議 `agent-ai-47` 部署時一併更正。

---

## [2026-09-10 診斷] 吊機 PQW（水閥繼電器）**電氣上不在匯流排上** —— 兩種鮑率、255 個位址全掃

> ⚰️ **2026-09-10 稍晚推翻，見 `[2026-09-10a4]`。** 標題那句結論**是錯的**：模組一直都在，
> 是 **ZS-DIO 不是 PQW**，出廠預設鮑率 **38400**（PQW 才是 9600）—— 我照 PQW 手冊只掃了
> 9600 與 115200，**38400 從頭到尾沒被掃到**。⇒ 「全掃」兩個字不成立，
> 而下面那句「剩下只有沒電或 A/B 接反」是建立在這個不成立的前提上。
> 📌 **本節的程序部分（差分掃描、`port.cgi` 17 欄、`rup`/`rfp` 禁令）仍然有效**，保留。

### 起因

`PQW:12` 自 09-09 起每次開機都 `init presence probe failed — device not on bus`。
per user 要修復。機器當時無載停在地面 ⇒ **動匯流排最安全的窗口**。

### 診斷（吊機程式停掉、獨佔 `.34` 匯流排）

**先縮小範圍**：同一條 `.34` 上的 SD76 slave 1／2 正常讀取
⇒ **網關、A/B 主幹、115200 全部正常**，問題只在 PQW 模組本身。

**差分掃描**（插上前掃一次當基準、插上後再掃一次）：

| 條件 | slave 1~255 掃描結果 |
|---|---|
| 115200，PQW **未插** | `1`（SD76 左）`2`（SD76 右） |
| 115200，PQW **已插** | **逐位元完全相同** ⇒ 沒有新 ID，**也沒有撞號跡象** |
| **9600**（出廠預設鮑率），PQW 已插 | **零回應**（連 SD76 都安靜 ⇒ **證明鮑率確實改到線上了，測試有效**） |

🔴 **結論：PQW 在兩種鮑率、全部 255 個位址都不存在。不是鮑率、不是 slave ID，
它電氣上根本不在這條匯流排上。** 剩下只有**沒電**或 **A/B 未接／接反／斷線**（RS485 極性敏感，接反同樣是完全無回應）。

📌 **為什麼要測 9600**：手冊摘要記 PQW 出廠預設是 **address 1 / baud 9600**，
而匯流排是 115200 ⇒ **若模組被恢復出廠，它在 115200 上是隱形的，掃不到不代表它壞了**。
不測這一項就無法區分「壞了」與「設定跑掉」。

📌 **差分掃描的價值**：出廠 ID 1 會與 **SD76 左（也是 slave 1）撞號**。
單掃一次看到「1 有回應」無法分辨是 SD76 還是兩者相撞，
**比對插上前後的回應位元組**才排除得掉（本例兩次逐位元相同 ⇒ 無撞號）。

### 🔧 已驗證的程序：暫時改 USR 網關的串列參數

⚠️ **CLAUDE.md 記的是 `system.cgi`（打包參數 `pt`/`plen`），不是本程序。串列參數在 `port.cgi`，欄位完全不同。**

`http://<gw>/port.shtml` 是 `GET port.cgi`，**17 個欄位必須全帶**（漏帶會被清掉）：

```
port bc parity stop tlp trp tnm rip umode shortc shortct cmode cnum htpch htpot htpcoh   + br
```

🔴 **`br` 那個 input 用單引號** `<input name='br' ...>` —— 用雙引號 grep 表單欄位會**漏掉它**（我第一次就漏了，以為表單裡沒有鮑率欄）。
現值全部在 JS 變數裡：`tr ";" "\n" | grep -oE "var (_br|sb|bc|par|...) *= *[^ ]+"`。

**存完必須重開網關才生效**：`manage.cgi?reset=1&rup=0&rfp=0`（本例重開只花 2 秒）。
🔴🔴 **`rup=1` 是恢復使用者預設、`rfp=1` 是恢復出廠（會清掉 IP 與鮑率）—— 絕不可誤送。**

✅ **還原已逐欄複驗**：16 欄與動手前逐一相同，SD76 兩顆的回應位元組也與基準完全相同。

⚠️ **權限**：第一次送出時被 Claude Code 的分類器擋下（帶帳密的 URL ＋ `manage.cgi?reset=1` 對網路設備送重置）。
per user 調整權限後才能執行。**這是合理的攔截**，不要試圖繞過。

## [2026-09-10a2] `PQW_TOTAL_CH` 16 → 8 —— 把「偽造的確認」降級成「留下證據的沉默」

> AI-2 線。✅ **本機完整建置通過：16/16 objs + link OK**（`scripts/build/build_body.sh` 的同一組來源與旗標，輸出丟 scratchpad 不落樹內）。⚠️ **本機是 x86-64、Pi 是 ARM64** ⇒ 這證明的是**編譯與連結正確**，不等於 Pi 上的建置產物；**未部署、未上機。**

### 改了什麼

`app/WASH_ROBOT.h:443` `PQW_TOTAL_CH` **16 → 8**（一個常數 + 說明）。這是 09-09 那三條 PQW 待辦的**第 1 條，而且只有第 1 條**。

### 🔴 它修好了什麼、沒修好什麼（這段是重點，別讀成「PQW 修完了」）

| | 舊（16） | 新（8） |
|---|---|---|
| FC01 要幾個線圈 | 16 | 8 |
| 模組對 ch9~16 | **編造 `1` 回來** | 根本不會被問 |
| `pqw_set_relay_verified_(14,…)` | 拿到偽造的 `1` ⇒ **「已確認」** | 落進 best-effort 分支 ⇒ 印 `readback unavailable`，**仍 return 成功** |
| `cmd_water_pump` | `OK ch14=1`（假確認） | `OK water_pump_set_but_readback_fail` |

⇒ **淨效果是「說謊 → 沉默 + 一行證據」，不是「修好」。** 真正的修法仍然是**通道號改對**（待辦第 2 條，牽涉實體接線，未動）。
📌 但這件事值得單獨做：**偽造的確認比缺少確認更糟** —— 前者會讓下游停止追問。

### 為什麼可以安全地縮小

兩項都實查過，不是推定：

1. **每一個 `readAllStatus()` 的消費者都先檢查長度才索引** —— `WASH_ROBOT.cpp:4952`（`st.empty() || size <= ch-1`）、
   `wash_robot_commands.cpp:3869`／`3888`／`3901`／`3940`／`3956`（皆 `size < N` 先擋）。無越界風險。
2. **console v2 的 `RELAY` 表本來就只列 CH1~CH8**（`public_v2/index.html`）⇒ **前端早就是對的，後端那個 16 才是異數。**
   其餘兩個消費者也不受影響：console v2 `:1758` 只比對 `ch1=`；`cycle_test.py:661` 只找 `pumpA`（CH2）。

📌 **順帶一提，這也是 09-09 那次判斷錯誤的餘波**：當時「`PQW_TOTAL_CH` 由 8 改 16」被列為「像是 16 路」的反證之一。
它其實不是證據，**它是同一個錯誤前提的產物** —— 一個錯誤替另一個錯誤作證。

---

## [2026-09-10 踩坑] 🔴 我昨天刪舊樹刪掉了 WEB 的 `node_modules` 目標，而**驗收做在刪除之前**

### 症狀

斷電一夜後重新啟動，吊機的兩個 WEB 服務**都起不來**：`MODULE_NOT_FOUND`，`:8080`/`:8081` 沒有監聽。

### 真因

```
web/node_modules -> /home/user/projects/web_ver2/node_modules   (broken symbolic link)
```

`~/projects/web_ver2` 是 **2026-09-09 路徑大搬家時我在 Phase 3 刪掉的舊樹之一**。

🔴 **為什麼昨天沒發現**：昨天的順序是
① 搬家 → ② **從新路徑啟動 WEB、驗到 HTTP 200** → ③ Phase 3 刪舊樹。
**驗收發生在刪除之前**，而已經在跑的 node 行程早就把模組 require 進記憶體了
⇒ 刪掉 symlink 的目標**對執行中的行程完全沒有影響**，破壞的是**下一次啟動**。
斷電重開才暴露出來。

📌 **通則：刪除之後要重新驗一次，而不是「刪除之前驗過就算」。**
執行中的行程會遮蔽掉「檔案已經不在」這件事 —— 對 symlink、對 `.so`、對任何 open 著的 fd 都成立。
⚠️ 同族：昨天也記過「執行中的行程握著舊 inode，`ls -l /proc/<pid>/exe` 會顯示 `(deleted)`」
—— **同一個機制，我當時寫下來了，卻沒有把它套用到自己正在做的刪除上。**

### 處置

從 `~/archive_20260909/projects.tar.gz` 取回（**打包救了這次**），並**改成實體目錄**：

```
web/node_modules/   68 個套件、4.2M、express 與 ws 都在
```

⇒ 不再依賴 symlink，也不再依賴任何已刪除的樹。重啟後 `:8080`/`:8081` 皆 HTTP 200。

🔴 **待查**：`web_ver2` 已刪，所以這份 `node_modules` 現在是**唯一副本**且**不在版控**（`node_modules` 慣例不進 git）。
若要可重建，應確認 `package.json` 能 `npm install` 還原（Pi 需要網路）——**尚未驗證**。

---

## [2026-09-10 實測] ✅ SD76 計米器的零點**撐得過整台斷電**

斷電一夜、兩台 Pi 重新上電之後，吊機讀值 **`L=258 R=255`**，
與 09-09 收工時**逐位相同**（機器停在地面端、高度 −2，期間無人移動）。

⇒ **SD76 的零點是非揮發的**（存在計米器硬體裡，不是程式狀態）
⇒ **`home_ground_cm = 256` 斷電後仍然有效，不必重新校正**，直接 `set_home_ground 256` 即可。

📌 這條之前沒有被驗證過。先前只知道「**程式**重啟不影響計米器零點」（09-09 換 binary 時觀察到），
**整台斷電**是更強的條件，現在也成立了。

⚠️ 但**不持久的仍然不持久**：`home_ground_cm` / `level_diff` / `motion_hz` / `roll_correct_hz` /
`arm_attached` 全部回到編譯預設（實測分別是 0 / 0 / 50 / 10 / **on**）。
🔴 **`arm_attached` 預設是 `on`** —— 不補設的話下一趟 cycle_test 手臂會真的動。

## [2026-09-10a1] 水閥「先武裝後送」＋ 獨立逾時 —— 開閥送到了但回覆丟了，不再沒人看管

> AI-2 線（WEB GUI／讀碼），機器線由 `agent-ai-e9`。✅ **本機完整建置通過：16/16 objs + link OK**（`scripts/build/build_body.sh` 的同一組來源與旗標，輸出丟 scratchpad 不落樹內）。⚠️ **本機是 x86-64、Pi 是 ARM64** ⇒ 這證明的是**編譯與連結正確**，不等於 Pi 上的建置產物；**未部署、未上機。**

### 改了什麼（`.claude/handoff/ai2-crane-cmd-protection.md` §10.6 的 ⭐1 + ⭐2，兩個獨立 hunk）

| # | 內容 | 位置 |
|---|---|---|
| 1 | **先武裝後送**：`water_inlet_open_ts_ms_` 的蓋章由「成功之後」移到「送出之前」 | `app/wash_robot_commands.cpp` `set_water_inlet_` |
| 2 | **`WATER_INLET_CMD_TIMEOUT_SEC = 5`**：不再繼承 `crane_cmd_` 為 `fine_adjust` 調的 60s 預設 | `app/WASH_ROBOT.h:850` + 同函式兩個呼叫 |

### 為什麼 ①：兩種錯誤的代價差好幾個數量級

原本只有**收到 `OK` 才蓋時間戳**。但吊機端 handler 是 `controlRelay()` ＋ 3×200ms verify 迴圈
（`Crane_control_PI/main.cpp:4947-4971`）⇒ 在會掉包的鏈路上，**「繼電器動了但 OK 沒回來」比「什麼都沒發生」更可能**。

⇒ 舊行為：閥實際開著、watchdog **沒武裝** ⇒ **沒有任何東西會去關它。**

| 情境 | 舊 | 新 |
|---|---|---|
| 開閥成功 | 武裝 | 武裝（時間戳早幾秒，保守方向） |
| 🔴 **開閥「失敗」但實際生效** | ❌ 不武裝 ⇒ **水無人看管地流** | ✅ 武裝，300s 後強制關 |
| 開閥失敗且確實沒生效 | 不武裝 | ⚠️ 誤武裝 → **一次多餘的關閥（幂等）＋ 一則假 EVT** |
| 關閥成功／失敗 | 解除／保持武裝 | **不變**（關閥那一半本來就是對的） |

📌 **誤武裝會自我修復**（第一次成功的關閥就清掉），**漏武裝不會。**

### 為什麼 ②：那個 60s 不是為它調的

`set_water_inlet_` 從來沒帶第二參數，於是繼承了 `WASH_ROBOT.h:2162` 的預設 60s，
而那行自己的註解寫著「`30 → 60 (2026-05-11)`: give **fine_adjust** 30s budget on top of main motion」。
⇒ **一個繼電器開關不該有 60 秒的耐心。** 取 5s（吊機側最壞約 1~1.5s，約 3 倍餘裕）。

### 🔬 兩個 patch 的互動（落地時才看得出來，值得記）

Patch 1 讓時間戳變成「**開始嘗試**」而不是「成功」，最壞會讓 watchdog 早觸發 ——
而 **Patch 2 正好把這個提早量從最壞 361s 壓到 ~16s**（3 × 5s + 2 × 0.5s）。

餘裕核對：合法的最長開閥窗口 ＝ `WATER_FILL_TIMEOUT_MS` 180s ＋ 水滿後 5s 延遲 ＝ **185s**，
加上提早量 16s ＝ 201s，仍遠低於 `WATER_INLET_OPEN_MAX_MS` **300s** ⇒ **不會引入誤觸發的強制關閥。**

### 呼叫點複驗：這個洞在現場是怎麼張開的

兩個開閥點失敗時**都直接 return、不曾關閥**：

- `WASH_ROBOT.cpp:2109` `arm_clean_sweep_cont_end_refill`：`end_refill_active_.store(false); return;`
- `WASH_ROBOT.cpp:2185` `arm_clean_sweep_cont`：`return "ERR water_inlet_open_fail\n";`

⇒ 舊行為下這兩條路徑正是「閥可能開著、而且沒人看管」的產生器。**修的是 helper，但受益的是這兩個呼叫點。**

其餘 10 個呼叫點都是 `false`（關閥），只受 ② 影響 —— 含 `cmd_emergency_stop` 的強制關閥
（`wash_robot_commands.cpp:3655`）⇒ 鏈路壞掉時**早點放棄、早點重試**，方向正確。

### 沒做的（都要拍板，不順手做）

- ❌ **§10.3 B3**：watchdog 專用的第四條連線（`crane_cli_water_`）—— 文件明列「需 per user 拍板」
- ❌ **§10.2 EVT 措辭分兩種**（「確定開過」vs「狀態未確認」）—— 可選，+1 atomic，只影響 log 可信度。
  🔴 但它是 Patch 1 唯一真正的成本的解藥：**假 EVT 會訓練操作者忽略真 EVT**
- ❌ 未部署、未實機驗證（本機建置 16/16 + link 通過，但 x86-64 ≠ ARM64）
  📌 **建置後有做正對照**：`strings` 撈得到本次新增的 `ARMED`／`not armed`，同時撈得到既有的 `relay_status`／`pumpB` ⇒ **「找得到新字串」與「grep 本身有效」兩件事同時成立**，才算證明改動真的進了產物（本專案自己的通則：「沒有找到」要有一個「找得到東西」的正對照）
- ❌ §10.5 的秒數仍**全部是推導**，SYN 逾時未在兩台 Pi 上實查

---

## [2026-09-09 實測] 🔴 PQW 繼電器模組**會替不存在的通道編造回覆** —— `CH_BRUSH=15` 潛伏五週的機制找到了

### 起因

AI-2 做 PCB 時提出「PQW 是 8CH 而程式用 CH14」。我複驗時列出反證（類別名 `PQW_IO_16O_RLY`、
標頭寫 16、`PQW_TOTAL_CH` 由 8 改 16、`relay_status` 整天正常回 ch1~ch16 無例外），判斷「像是 16 路」。
**per user 直接確認：是 8CH。** ⇒ 我的結論錯了，**而我那條「反證」重新解讀之後變成最強的一項證據，只是指向相反方向。**

### 決定性測試（機器已停妥、繼電器全關、`ch14` 未接負載，寫它安全）

| 指令 | 回應 | 回讀 | |
|---|---|---|---|
| `relay 14 on` | **`OK ch14=1`** | ch14=**1** | 🔴 編造 |
| `relay 16 on` | **`OK ch16=1`** | ch16=**1** | 🔴 編造 |
| `relay 9 on` | `OK ch9=1`（第一次試回 `ERR`，不可重現） | ch9=**1** | 🔴 編造 |
| `relay 8 on`（實體存在，對照組） | `OK ch8=1` | ch8=1 | ✅ |

🔴 **8 路的模組對 ch9~ch16 照單全收，寫入回 `OK`、回讀也回 `1`。
它不回 illegal data address，它回一個看起來完全正常的值。**

### 這解開了一個既有謎團

`CH_BRUSH` 在 2026-07-24 被誤改成 **15**，**實機滾筒整段不轉、持續五週**才被發現。
先前只記「打到不存在的通道」，**沒有解釋為什麼五週都沒人發現**。
現在有了：**因為模組會說謊。** 寫入成功、回讀正確、狀態全綠 —— 唯一不對的是實體不動。

⇒ 📌 **這是「帳面全綠、實機不動」那一整類事故的具體機制，而且現在有實測支持。**

### 連帶待辦（三條，皆未修）

| # | 項目 | 說明 |
|---|---|---|
| 1 | `app/WASH_ROBOT.h:443` `PQW_TOTAL_CH = 16` **應改回 8** | 它 2026-07-24 由 8 改 16 的理由是「讓 `readAllStatus()` 涵蓋 CH15」，而 **CH15 從來不存在** —— 與 `CH_BRUSH=15` 是同一個錯誤前提的產物，那一半修了、這一半沒修。現在它每次都去讀 8 個不存在的線圈並拿回編造值 |
| 2 | `:491` `CH_WATER_PUMP = 14` **確定越界** | 要收回 8 以內（AI-2 的板子排 CH8，CH7/CH8 目前空著）。⚠️ **牽涉實體接線，不逕自改** |
| 3 | `cmd_water_pump` 是裸 `controlRelay` + 無條件 `OK` + **無回讀** | 🔴 而且**即使加了回讀也救不了** —— 模組對 ch14 會回編造值。真正的解是**通道號改對 ＋ 走 `pqw_set_relay_verified_`**，缺一不可 |

📌 **吊機那顆的宣告是對的**：`Crane_control_PI/main.cpp:216 PQW_WATER_TOTAL_CH = 8; // 8-channel PQW board`。
兩顆各自宣告不同（本體 16 / 吊機 8），所以證據看起來才會互相矛盾 —— **真正過期的是本體那個 16。**

### 對 PCB 的意義

新板的韌體保證「寫入越界回 `0x02`」從**防呆**升級成**修正現行硬體的一個明確缺陷**：
**現行硬體在這件事上會說謊，新板不可以跟著說謊。** 已告知 AI-2（其設計文件 v0.6 已納入）。

### 📌 我在這題上的推理錯誤

我把「`relay_status` 回 ch1~16 沒有例外」當成「模組有 16 路」的證據。
**那個推論預設了「模組會對越界請求報錯」—— 而那正是待驗證的事。**
⇒ 通則：**用「沒有錯誤訊息」當證據之前，先確認這個系統在該情境下真的會報錯。**
（同族：本專案 `grep` 是 ugrep，對二進位檔靜默交白卷，「找不到」與「拒絕處理」長得一樣。）

## [2026-09-09 文件更正] 🔴 PWM 那一路是**貼牆螺旋槳不是散熱風扇** + 兩份手冊摘要的 8N1 記載已過期

由 AI-2 讀碼發現（它在做 PCB 架構），本檔作者逐項複驗。**三條裡兩條成立、一條前提存疑。**

### ① 🔴 QX-DO24 驅動的是**貼牆螺旋槳**，我整份規格書寫成「風扇」

`app/WASH_ROBOT.h:578` 逐字：**「寫入失敗時模組保持前一個輸出，通訊斷掉螺旋槳不會停；左右螺旋槳共用 CH1」**
`:557-558`：`PWM_STEP_MOVE_DUTY_PCT = 7.0`（**收腳 + 吊機移動期間**）、`PWM_STEP_OFF_DUTY_PCT = 5.0`（**伸腳前必寫**）

⇒ **它是在四顆吸盤沒有全部抓住時提供貼牆推力的東西。**

🔴 **後果：這一路的失效語意與其他所有通道相反。** 全板有三種：

| 通道 | 失聯時應該 |
|---|---|
| 繼電器（真空閥/幫浦/破真空/刷子/水泵） | 落到安全態 |
| RS485 命令（VFD/推桿/滑台） | 停止 |
| **PWM 螺旋槳** | 🔴 **保持輸出、繼續送脈衝**（停止推力＝機器離牆） |

⚠️ **合併進共用 MCU 的隱性代價**：今天 QX-DO24 有自己的 MCU，**Pi 當掉螺旋槳照轉**；
搬進共用 STM32 之後 **STM32 一當就把螺旋槳一起帶走**。緩解：脈衝交給硬體計時器自主輸出。

📌 順帶解開「鎖 5~10% / 50Hz」之謎：50Hz 下 5%=1.0ms、10%=2.0ms ⇒ **標準 RC 電變（ESC）脈寬協定**，不是保守值。

已改：`CLAUDE.md` 五處、`.claude/reference/comms_interface_for_pcb.md` 新增 §6b。

### ② ⚠️ 「PQW 是 8CH 而程式用 CH14」—— **前提複驗不成立，證據互相矛盾**

| 支持 16 路 | 支持 8 路 |
|---|---|
| 類別名 `PQW_IO_16O_RLY`，標頭寫「16 組 Relay」 | `CLAUDE.md` 寫「PQW 8CH 繼電器模組」 |
| `PQW_TOTAL_CH` 2026-07-24 **由 8 改 16**（「so readAllStatus actually covers CH15」） | |
| 本日整天 `relay_status` 正常回 **ch1~ch16 無 Modbus 例外** | |
| 手冊摘要：該系列「up to 64 output channels」 | |

🔴 **而 `CLAUDE.md` 那一段自己下面就列出 `CH_WATER_PUMP = 14` —— 文件前後矛盾。**
⇒ **未解，列入規格書 §7 第 7 項。** 這**直接決定 PCB 要放幾顆繼電器**，已請 AI-2 先別照 8 路定案。

📌 **但 AI-2 指出的另一半成立且更重要**：`cmd_water_pump` 是裸 `controlRelay` +
無條件回 `OK` + 無回讀（與 `pqw_set_relay_verified_` 不同路徑）
⇒ **不管通道號對不對，裝上水泵解開註解那天它都會安靜地失敗。** 另立待辦。

### ③ ✅ `sb=2` 結案：是 2 個停止位，而且是**對的**

`SE3_INVERTER_MODBUS_SUMMARY.md` 的 `07-07` 那列自己就寫 **`3 = 1,8,N,2` RTU used by project**
⇒ SE3 端本來就設 8N2，只是被 SD76 逼著把**網關**設 8N1（SD76 沒有 8N2 選項）。
**2026-05-15 re-layout 把 SD76 移到 `.34` 之後，那個限制就消失了** —— 而 8N2 才是無同位元時
Modbus RTU 要求的 11-bit 幀。⇒ 現在的設定不只可行，是比較正確的那個。

🔴 **兩份摘要的過期記載已修**：
- `SE3_INVERTER_MODBUS_SUMMARY.md` 的 bench note（說網關是 8N1）→ 標為過期 + 補理由
- `MH300_INVERTER_MODBUS_SUMMARY.md` 的 `09-04 = 12（8N1）「配合與 SD76 共用 bus」`
  → **標紅為「待重新決定」**。**這條會咬 MH300 遷移**：SD76 早就不在 `.30`/`.31` 上，
  照舊填 8N1 會與網關（8N2）不符、整段不通。

📌 **通則**：這三條全都是「文件落後於硬體變更」。`.30`/`.31` 從 8N1 變 8N2 這件事
**在兩份手冊摘要、一份架構文件裡都沒有被更新**，而它是 2026-05-15 的 re-layout 帶來的
—— **搬動裝置的那次改動，沒有回頭掃過所有引用它的文件。**

## [2026-09-09 實測] 第二輪 10 週期：**漂移趨勢重現了**，但「漂的是什麼」仍未解

同條件第二輪（新基準、有線、`arm_attached=off`）。跑第二輪的**目的就是量第一輪的變異**
—— 依本日稍早的教訓：A/B 之前先量 A 自己。

### 左右位移差：兩輪都乾淨，表現穩定

| | 第一輪 | 第二輪 |
|---|---|---|
| 完成 | 10/10 | 10/10 |
| 段數 | 56 | 60 |
| 中位 / p90 / 最大 | 0 / 3 / 4 cm | 0 / 2 / **6** cm |
| ≥6cm | 0% | 2%（1/60，週期6回程） |
| **≥8cm（腳本門檻）／≥10cm（韌體硬限）** | **0% / 0%** | **0% / 0%** |
| 自動修正觸發 | 0 | 0 |

### 🎯 roll 漂移趨勢**重現**

| 輪次 | 逐週期停後 roll 平均 | 趨勢 | 跨 9 週期 | 殘差散布 |
|---|---|---|---|---|
| 一 | −0.03 +0.90 +0.88 +0.35 +1.24 +0.80 +0.91 **+2.13 +2.11** +1.62 | **+0.187 °/週期** | +1.68° | ±0.52 |
| 二 | +1.45 −0.06 +1.16 +2.15 +1.62 +2.36 +2.45 +0.75 +1.79 **+2.91** | **+0.156 °/週期** | +1.40° | ±1.01 |

**兩輪獨立給出 +0.187 與 +0.156 °/週期。** 跨輪累積也吻合：
第一輪起 **−0.03°** → 第二輪終 **+2.91°**，20 個週期共 +2.94° ⇒ **+0.147 °/週期**。

⚠️ 但第二輪的殘差散布是 ±1.01（第一輪 ±0.52）⇒ 趨勢/散布比由 3.2 降到 1.4。
**趨勢是真的（三個獨立估計一致），但單輪的信噪比沒有第一輪看起來那麼好。**

📌 **靜置不會重置**：第一輪結束 +1.62 → 靜置後第二輪起跑靜止實測 **+1.99**。

### 🔴 「漂的是計米器還是 IMU」——**本日未解，而我在這題上反覆了兩次**

判別法是看 `roll − atan2(ay, az)`（加速度計以重力為基準、不會漂）。實測：

| 時機 | 融合 `roll` | 加速度計 | 差 |
|---|---|---|---|
| 15:50 頂端 | +4.22° | +4.00° | **+0.22°** |
| 第二輪後 頂端（靜止 8 筆，#3~#8 穩定） | +2.97° | +2.75° | **+0.22°** |
| **下到底之後（靜止 3 筆）** | +2.39° | **+0.57°** | **+1.82°** |

兩個頂端量測**逐位一致**（+0.22），我據此宣告「IMU 沒漂」——
**然後同一次下降之後，同樣的差變成 +1.82。**

### ✅ 已解（同日稍後）：**IMU 沒有漂，漂的是計米器 ↔ 物理水平的關係**

把機器放到地面端**靜置數分鐘後**重量，並補上 pitch 耦合修正
（`atan2(ay, sign(az)·hypot(ax,az))`，本例與簡式相同因為 pitch 僅 −0.45°）：

```
地面端靜置 8 筆，散布 0.00：roll=+2.40  加速度計=+2.81  差 = −0.41°
```

| 時機 | 差 |
|---|---|
| 15:50 頂端 | +0.22° |
| 第二輪後 頂端（靜止） | +0.22° |
| ~~下降剛結束（未靜置）~~ | ~~+1.82°~~ ← **正是干擾項 #1（殘餘擺盪），已排除** |
| 地面端（靜置數分鐘） | **−0.41°** |

⇒ **整天四個時點／兩個位置，IMU 與重力的偏移都在 +0.22 ± 0.6° 內。
而運行中量到的漂移是 +3°／20 週期 —— 偏移量根本不足以解釋它。**

🎯 **結論：IMU 可信；漂的是「計米器讀數 ↔ 物理水平」的關係。**
加速度計（不會漂）直接證實：地面端機器**真的**歪 +2.81°，而計米器說 `L−R=0`（照校正該是水平）。
⇒ **證成 per user 的兩段式設計：計米器路徑會在一個工作時段內退化，IMU 細調是必要項。**

📌 **那個 +1.82° 的假訊號值得記住**：它出現在「動作剛結束」的取樣，而我當時**已經把「可能還在擺」寫進弱點清單卻仍然拿它下了結論**。
⇒ **列了干擾項卻沒有排除它，就等於沒列。**

<details><summary>當時的未解記錄（保留）</summary>

🔴 ⇒ 未解。「IMU 在漂」與「IMU 正常」我今天各宣告過一次，兩次都是資料不足就下結論。
</details>

未排除的干擾項（我自己列得出來的三個）：
1. 下降剛結束，機器可能仍在擺 —— 加速度計會被殘餘加速度污染
2. `atan2(ay, az)` 沒有處理 pitch 耦合（頂端 pitch ≈ +0.80°，底端未記錄）
3. 我的取樣腳本**間歇讀到全 0**（8 筆中 1 筆、4 筆中 1 筆）—— 這是我的解析問題，不是機器

🔧 **要解它需要固定協定，不是臨時量**：同一個位置、機器完全靜置 ≥1 分鐘、隔一段時間重複、
並記錄 `pitch` 與 `ax`。⇒ 列為待辦，不在收工前硬做。

### 📌 本日第三次同族的錯（前兩次已各自記錄）

| 次 | 形狀 |
|---|---|
| 1 | 拿「重現」當「因果確立」（IMU stale 門檻） |
| 2 | 沒問「有沒有第二個解釋會產生一模一樣的資料」（計米器 vs IMU） |
| 3 | **拿「同一位置的 6 筆一致」當「整體一致」** —— 換個位置就不成立了 |

**共同點：手上的資料不足以支撐我下的那個範圍的結論，而我沒有意識到範圍不匹配。**
⇒ 通則再補一條：**結論的適用範圍不能大於量測的涵蓋範圍。**

### 收尾狀態

機器已放到地面端：`L=259 R=259`、`L−R=0`、**高度 −3**（比校正時定的地面端低 3cm，減速滑行過衝）。
繼電器 **16 個全部 OFF**。張力 L=58.7 / R=37.0、`tension_valid=1`。
三支程式（吊機 `:5002`、兩個 WEB、本體 `:5001`）仍在服役。

## [2026-09-09 更正] 🔴 上一條的「計米器在漂、IMU 是必要項」**沒有排除相反的可能**

> 上一條說 10 週期的 roll 漂移（+0.187°/週期）「直接證成兩段式設計：計米器路徑會退化，IMU 細調是必要項」。
> **那個結論下得太快。**

### 兩個互斥的解釋，我只檢查了其中一個

| 解釋 | 症狀 | 我當時做的 |
|---|---|---|
| A：計米器 ↔ 物理水平的關係在漂 | `L−R=0` 但 roll 變大 | ✅ 有資料支持 |
| **B：IMU 融合姿態在漂，計米器其實是對的** | 一模一樣 | ❌ **完全沒查** |

🔴 **A 和 B 在我當時看的資料裡長得一模一樣。** 而如果是 B，結論會**整個翻轉**：
`balance_source=imu` 正在把漂掉的目標餵進平衡迴路，「IMU 細調」會愈調愈歪。

### 第一個該起疑的訊號我看到了卻沒追

**靜置期間 roll 由 +1.62 變成 +1.99 —— 機器完全沒動。**
計米器不動就不會累積誤差 ⇒ **那段變化本來就不可能是解釋 A**。

### 判別法：加速度計是不會漂的參考

本體 status 有 `ax/ay/az`。加速度計以**重力**為基準，沒有積分、不會漂；
融合後的 `roll` 才會受陀螺儀零偏影響。⇒ 看 **`roll − atan2(ay, az)`** 這個差會不會隨時間走。

| 時間 | 融合 `roll` | `atan2(ay,az)` | 差 |
|---|---|---|---|
| 15:50（校平前） | +4.22° | +4.00°（`ay=0.07`） | **+0.22°** |
| 第二輪運行中 | ~+1.1° | +2.86°（`ay=0.050`） | **−1.7°** |

⇒ **差值本身移動了約 1.9°。** 若兩者量的是同一件事，這個差應該是常數（＝安裝偏移）。

⚠️ **仍不是定論**，三個弱點：
① 早先 `ay` 只有 2 位小數（`0.07`）⇒ ±0.29° 解析度，基準粗；
② 現在是運行中取樣，加速度計會被運動加速度污染；
③ 我沒有一條乾淨的基準線（校正當下沒記 `ay`）。

### 🔧 從現在起的做法

**每輪在機器靜止時記錄 `roll − atan2(ay, az)`。** 隨時間走 ⇒ IMU 漂；常數 ⇒ 機構漂。
這是一個決定性的、幾乎零成本的量測，而它應該在下第一個結論**之前**就做。

📌 **通則（今天第二次）**：本日稍早才記過「A/B 前先量 A 自己的變異」。這次是另一個形狀的同類錯誤——
**先問「有沒有第二個解釋會產生一模一樣的資料」，再問「資料支不支持我的解釋」。**
兩次的共同點都是：**手上的資料不足以區分，而我沒有意識到那件事。**

## [2026-09-09 實測] 🏆 10 週期全數完成，左右差是專案至今最好 —— 但量到 roll 會**隨週期累積漂移**

新基準（頂端歸零、水平狀態下歸零 ⇒ `level_diff=0`）+ 急停修正後的 10 週期，有線鏈路、`arm_attached=off`。

### 左右位移差：專案至今最好

| | 09-09 上午（`level_diff=6`、舊零點） | **本輪（新基準）** |
|---|---|---|
| 段數 | 60 | 56 |
| 中位 / p90 / 最大 | 3 / 4 / **6** cm | **0 / 3 / 4** cm |
| ≥6cm | 3% | **0%** |
| ≥8cm（腳本中止門檻） | 0% | 0% |
| ≥10cm（韌體硬限） | 0% | 0% |
| 自行回復次數 | 0 | 0 |
| **真空未建立** | 有 | **0 / 50 步** |

每步 Δmax 最大 **3**、回程 Δmax 最大 **4**（平均 2.1）。**沒有任何一次自動修正被觸發。**
⚠️ 4 次出現腳本哨兵值（`Δmax=-1 / 出帶=-1%`＝該步監看執行緒一筆樣本都沒收到），偶發、不影響判定。

### 🔴 但 roll 有明確的累積漂移 —— 而 `L−R` 全程是 0

| 週期 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| 停後 roll 平均 | **−0.03°** | +0.90 | +0.88 | +0.35 | +1.24 | +0.80 | +0.91 | **+2.13** | **+2.11** | +1.62 |

**線性趨勢 +0.187 °/週期 ⇒ 9 個週期累積 +1.68°。** 前三週期平均 **+0.58°**、後三週期 **+1.96°**。

🔴 **關鍵在於：同一時間 `L−R` 的中位數是 0。** 計米器路徑一直精準地維持在它的目標上，
**是「計米器讀數」與「物理水平」之間的關係在漂**，不是控制迴路失效。
用當日斜率 −1.10°/cm 換算 ⇒ 每週期約 **0.17cm** 的左右計米器累積誤差。

### 這件事的意義

✅ **它直接證成 per user 的兩段式設計**（「先用計米器修正，後做 IMU 細調」）：
**計米器路徑會在一個工作時段之內退化**，IMU 細調不是加分項而是**必要項**。
外推：以 +0.187°/週期，約 30 個週期就會碰到腳本的 6° 中止門檻。

✅ 也補上了 `level_diff` 那條註解的另一半：先前只記「**每次計米器歸零**都會靜默移動這個目標」，
本輪顯示**不歸零也會漂，只是慢**。

⚠️ **n = 1 輪，不當定論。** 週期間的散布約 ±0.4°，趨勢量 1.68° 是它的 4 倍，所以**這一輪的趨勢是真的**；
但「是否每輪都如此」需要第二輪。**依本日教訓（A/B 前先量 A 自己的變異），下一步就是同條件再跑一輪 10 週期。**
📌 另一個未排除的解釋：鋼索伸長／捲筒排線／載重分佈，而不是計米器誤差。本資料分不出來。

### 速率

下行含開銷 **70.0 s/m**（實際作業速率）、上行回程 8.1 s/m ⇒ **一趟來回每公尺約 78.1 s**。
單步平均 28.0s（伸出 3.8 / 真空 2.3 / 滑台 6.9 / 收回 3.5 / 移動 6.4 / 其他 5.1）。

## [2026-09-09m7] 🔴 急停通道：ACK 被排隊的 EVT 蓋掉 + 接收緩衝區只漲不消

### 缺陷一：ACK 讀錯（假警報）

`crane_stop_estop_()` 送完 `stop` 只 `receiveData` 一次就判斷開頭是不是 `OK`。
但吊機的 `broadcast_evt()` 對**所有**連線送，這條 client 平常不讀 ⇒ 佇列裡先躺著 EVT
⇒ 讀到的是 EVT ⇒ 判定失敗 ⇒ 印 **`🔴 CRANE STOP NOT ACKED — ropes may still be moving`**，
**即使 stop 其實成功了**。急停之後最不需要的就是一個假警報。

📌 同檔的 `read_rope_weight_estop_()` 早就有正確做法（drain 到 `OK` 為止，註明「EVT lines may interleave」），
`crane_stop_estop_()` 沒用上 —— 又一次「對的做法就在隔壁函式」。

### 🔴 缺陷二（比較嚴重）：緩衝區滿了會讓吊機**卡死**

這條 client 從來不讀 ⇒ 接收緩衝區只漲不消。滿了之後吊機那端 `send()` 會**阻塞**
（`SEND_FLAGS` 只有 `MSG_NOSIGNAL`、沒有 `SO_SNDTIMEO`、socket 也不是非阻塞），
而 `TCP_server::broadcast()` 是**持著 `clients_mtx`** 在送
⇒ **整條 EVT 廣播路徑卡死，連帶拖住其他 client**。

⚠️ 這不是推論出來的紙上風險：實測 `ss` 看到該 socket `Recv-Q` 閒置 10 分鐘 23 bytes、
運動中一個快照就 81 bytes，**沒有任何機制會讓它下降**。

### 修法

| | |
|---|---|
| `estop_drain_locked_(budget_ms)` | 新增。丟棄佇列並回傳位元組數；呼叫端須持有 `crane_estop_mtx_` |
| `crane_stop_estop_()` | **先排空再送**（ACK 不排在 EVT 後面）→ 送 → **逐行掃描、跳過 `EVT`**、第一個非 EVT 行就是答案，總時限 `ESTOP_ACK_MS=1000` |
| `crane_watchdog_loop_()` | 每輪（500ms）**週期性排空**，用 `try_lock` 絕不排在進行中的急停後面 |
| 訊息分級 | **「沒送出去」與「送了但沒收到確認」分開印** —— 前者繩子一定還在動，後者多半已經停了。現場靠這個差別決定下一步 |

⚠️ **`receiveData` 的 timeout 走 `SO_RCVTIMEO`，傳 0 在 Linux 是「永不逾時」＝永久阻塞**，
不是「立刻返回」。排空迴圈一律傳正值（20ms），已寫進註解。

### 實測

| 檢查 | 結果 |
|---|---|
| `Recv-Q`（運動中連續 10 次取樣，2s 一次） | **全部 0** ✅（修正前只漲不消） |
| watchdog 排空 | `[crane_watchdog] estop channel drained 23 bytes` ✅ |
| `emergency_stop`（idle 實測） | `OK emergency_stopped`；吊機 log 收到 `cmd='stop'`；**本體 log 零警告、無 `NOT ACKED`** ✅ |
| `reset` 復原 | `state=idle`、`crane_estop_connected=1` ✅ |

部署：`facade_cleaning_v2.out` md5 `3d6f3eeb`（前一版 `71ecb102` 備份於 `~/run/facade_cleaning_v2.out.prev-20260909-1710`）。

### 同批第三趟 1 週期（新基準 + 本修正）

| | 第一趟 | 第二趟 | **第三趟** |
|---|---|---|---|
| 左右差 中位／p90／最大 | 2 / 3 / 5 | 0 / 2 / 3 | **0 / 0 / 1 cm** |
| 每步 Δmax | 2,1,3,1,2 | 0,0,1,0,2 | **0,0,0,0,0** |
| 停後 roll | −1.32 ~ +1.14 | −0.25 ~ +1.61 | +0.97 ~ +1.65 |
| ≥6cm | 0/6 | 0/6 | 0/6 |

⚠️ **不宣稱是本修正造成的** —— 三趟都是 n=6 段，而急停排空與繩長控制沒有因果路徑。
可以說的是：**新基準（`level_diff=0` 在水平狀態下歸零）之後，三趟的左右差一趟比一趟緊，且都遠離門檻**。
📌 `停後 roll` 這趟略偏正（+1.0~1.65°）而 `L−R` 卻是 0 ⇒ 水平參考可能較一小時前的校正漂了約 1cm。
**在門檻內、不處理**，但這正是「歸零之後水平點會漂」那條的持續觀察點。

## [2026-09-09c5] 🗂️ 兩台 Pi 路徑大搬家：`~/bringup` → `~/projects/facade_cleaning_v2` + `~/run`

per user「合併到 main 了，可以用 project 的資料夾，把舊的清掉，我們的搬過去，才不會散落各處」。

### 搬家前的實況

| | 吊機 `.5.25` | 本體 `.5.26` |
|---|---|---|
| 樹的數量 | **4 棵** | **6 棵** |
| `~/bringup`（服役中） | 35M | 80M |
| `~/projects`（舊 VS 樹） | 42M | 90M |
| 其餘（`merge_check` / `main_20260831` / `crc_test` / `synccheck`） | 11.5M | 39.7M |

`~/bringup` 內部：吊機 44 log（25M）＋28 FIFO＋11 舊執行檔；本體 118 log（14M）＋59 FIFO＋**97 舊執行檔（77M）**。
**程式碼與產物混在同一層**，那正是它難整理的原因。

### 新配置

| 路徑 | 放什麼 | 吊機 / 本體 |
|---|---|---|
| `~/projects/facade_cleaning_v2/` | **原始碼**，與 repo 同構，**頂層只有目錄** | 3.5M / 5.3M |
| `~/run/` | 執行檔、`logs/`、FIFO、`cycle_logs/`、回退點×3、`bench/`（探針 binary） | 28M / 21M |
| `~/archive_20260909/` | 舊樹 tar.gz（3 個 / 5 個） | 16M / 38M |

### 🔴 搬家過程抓到 6 份「頂層舊拷貝」——這才是真正的風險

它們與正本同名但落後，**改到錯的那份不會有任何徵兆**：

| 舊拷貝 | 正本 | 落後 |
|---|---|---|
| 吊機 `main.cpp` | `Crane_control_PI/main.cpp` | **790 行** |
| 吊機 `DSZL_107.h` / `MH300_inverter.h` / `SE3_inverter.h` | `user_lib/` | 32 / 5 / 5 行 |
| 本體 `wash_robot_commands.cpp` | `app/` | **526 行** |
| 本體 `dispatcher.cpp` | `command/` | 68 行 |

### 🔴 兩個「差點刪掉就永久消失」的東西

1. **14 個 Pi 獨有的檔**（WSL repo 沒有）：`cyc10/cyc20/probe21/timing/sendcmd.py`、`jog_test.cpp`、
   `dryrun/fc_test/pqw_fix.py`、`bld_body/bld_wr/bld_cr/launch/run_bg.sh`
   ⇒ **全部先取回 repo**（`Linux_test/` 與新建的 `scripts/bench/`）再刪。
   📌 CLAUDE.md 早記過同一條教訓：「沒進版控的檢查工具等於下次再寫一次」。
2. **`motor_api` 有兩份且新舊相反**：`~/projects/` 那份是 06-10（475080B），
   `~/bringup/` 那份是 **09-04（444008B）＝ `DEPLOY_F` 力控那版**。
   我一開始搬錯了方向，發現後修正 ⇒ `~/run/motor_api` 現在是 09-04 版，
   舊版留 `~/run/motor_api.stale-20260610`。

### 🔴 搬家的前置阻塞：`home_ground_cm` 沒有 setter

搬家要重啟兩支程式，而重啟吊機會清掉 `home_ground_cm=256`（不持久化），
**唯一寫入點 `zero_meters top` 在頂端只會存到 0** ⇒ 要復原得整套重新校正。

✅ **先加了 `set_home_ground <cm>`**（`cmd_set_home_ground`，上限 2000cm，純顯示用途所以不影響安全）。
雞生蛋可解，因為**計米器零點是硬體保存的**：重啟 → `set_home_ground 256` → 完全等價。
實測 `OK home_ground_cm=256 (was 0)`，邊界 `9999` → `ERR out_of_range (0..2000)`。

### 順帶：`TCP_client.cpp` 兩棵樹同步

急停修正動到 `TCP_client.cpp`（本體與吊機共用檔），先前只推了本體 ⇒ 兩樹分歧。
本次一併推吊機並重建，**兩台的 `nb_connect()` 實作現在是同一份**。

### 驗收

| 檢查 | 結果 |
|---|---|
| 四個服務 | 吊機 `:5002`、WEB `:8080`/`:8081`（HTTP 200）、本體 `:5001` ✅ |
| 鏈路 | 有線 `192.168.1.100 → 192.168.1.10:5002` 維持 ✅ |
| 急停通道 | `crane_estop_connected=1`、`down_ms=0` ✅ |
| 執行期值 | `home_ground=256`、`level_diff=0`、`motion_hz=30`、`roll_correct_hz=30`、`arm_attached=off` ✅ |
| 計米器 | `L=0 R=1`（頂端零點硬體保存，重啟未受影響）✅ |
| **建置腳本** | 兩支從新位置跑通，產物 md5 **與服役中執行檔逐位元相同**（`5e933d49` / `71ecb102`）✅ |

### ⚠️ 一天內第二次踩 `pgrep -f`

停本體時 `pgrep -f facade_cleaning_v2.out` 回報「程序仍在」，實際是**兩個 bash 殼層**
（cmdline 含該字串）。真正的主程式早已結束、`:5001` 也關了。
⇒ **判定主程式存活一律用 `/proc/<pid>/exe`**，已寫進 runbook。
📌 同族：09-09 上午 `pgrep -x crane_control_PI.out` 因程序名 >15 字元永遠 0 命中。

### 文件同步

`.claude/runbook.md`（檔首新增搬家公告 + §A0 四條指令 + 建置段全改，歷史段標 ⚰️ 保留）、
`CLAUDE.md`（建置表 + 產物路徑）、`scripts/build/README.md`、
`scripts/build/build_{body,crane}.sh`（改用 `$OUT="$HOME/run"`）。

⚠️ **`work_log.md` 的 58 處 `~/bringup` 刻意不改** —— 那是歷史紀錄，當時就是那個路徑，改掉等於竄改歷史。

## [2026-09-09c4] 新基準下的兩趟 1 週期 + 🔴 發現急停旁路通道未連線

### 兩趟結果（新座標系、`level_diff=0`、有線 192.168.1、`arm_attached=off`）

| | 第一趟 `newzero` | 第二趟 `newzero_b` |
|---|---|---|
| 完成 | ✅ 1/1 | ✅ 1/1 |
| 左右差 中位／p90／最大 | 2 / 3 / **5** cm | **0 / 2 / 3** cm |
| ≥6cm ／ ≥8cm ／ ≥10cm | 0/6 ／ 0/6 ／ 0/6 | 0/6 ／ 0/6 ／ 0/6 |
| 停後 roll（5 步） | −1.32 −1.33 −0.49 +0.87 +1.14 | **−0.04 −0.25 +0.49 +1.61 +0.99** |
| 回程終點 | 高度 256 ✅ | 高度 256 ✅ |
| 真空未建立 | **1/5（步 2）** | 0/5 |
| 來回速率 | 71.4 s/m | — |

⚠️ **n=6 段 × 2，不宣稱「新基準比較好」** —— 今天已經因為「拿重現當因果」錯過一次。
可以說的是：**兩趟都遠離 8cm 中止門檻與 10cm 韌體硬限**，新基準可用。

📌 `cycle_test.py` 加了**座標慣例自動判定**（見下），開跑第一行就印：
`座標慣例：**頂端 歸零**（home_ground_cm=256）⇒ height = home_ground_cm - length_left；跨距 TOP=256 cm`

### `cycle_test.py`：座標慣例不再靠人記得

本檔的 `height = -length_left` 是為 09-03 傍晚的**底端歸零**寫的；09-09 改回**頂端歸零**
（runbook 的生產標準流程）之後符號相反。改成從吊機的 `home_ground_cm` 自行判定：

```
home_ground_cm > 0  ⇒ 頂端歸零，height = home_ground_cm - L
home_ground_cm == 0 ⇒ 底端歸零，height = -L        （舊行為，逐位元不變）
```

可用 `FCV_ZERO_AT=top|bottom` 明確蓋過。**失效模式是「拒跑」不是「跑錯方向」**：
慣例判錯的話起點高度會差整整一個跨距，`abs(L0-TOP)>TOL(5)` 必定擋下。

⚠️ 過程中我自己引入又修掉一個 bug：`_detect_zero_convention()` 依賴 `ask()`/`field()`，
但那兩個定義在它後面，寫成模組載入時呼叫會 `NameError`。改成 `resolve_zero_convention()`
在起點檢查前呼叫，`height()` 惰性讀全域。

### 🔴 發現：本體對吊機**只有一條 socket**，急停旁路沒連上

per user 問「現在的資料鏈路是 192.168.1 嗎」，查 `ss` 時發現的。

**先回答鏈路**：是。`192.168.1.100:33432 → 192.168.1.10:5002`，兩端都走 eth0（Fathom-X）。
WiFi 只用於 SSH 觀察與吊機上兩個 WEB 對本體 `:5001` 的連線。

**但本體 pid 對 `:5002` 只有一條 socket**，而它是 IMU 那條（`imu_push_loop_` 只用
`crane_cli_imu_`，且 `set_imu_roll` 持續在流）⇒ **`crane_cli_estop_` 沒有連線**。

| 連線 | 現況 | 判讀 |
|---|---|---|
| `crane_cli_imu_` | 🟢 ESTAB（Send-Q=112，運動中積壓＝已知隧道劣化） | 正常 |
| `crane_cli_` 主指令 | ⚪ 未連 | **正常**，`crane_cmd_` 有 lazy self-healing reconnect |
| `crane_cli_estop_` | 🔴 **未連，且沒在嘗試** | **不正常** |

**證據**：6 秒內每 0.3s 取樣 `ss -tan | grep :5002`，20 次全部只有那一條、
**零 SYN_SENT、零 CLOSE_WAIT、零 churn** ⇒ 不是半死 socket（`available()` 對 orderly FIN 會回 −1，
這條 08-31 已修過），是**連嘗試都沒有**。

**已排除**：位址不同（estop 與 imu 都用 `crane_endpoint_ip_()`）／`available()` 漏判對端關閉／
CLOSE_WAIT 堆積／`crane_cmd_` 的 lazy 路徑（那條正常）。
**根因尚未確立** —— 靜態讀碼到此為止，`reconnectLoop` 看起來應該要觸發。

**後果**（不是假設，本體日誌 15:23 已實際發生）：
```
[crane_stop_estop] WARN: send failed
[emergency_stop] 🔴 CRANE STOP NOT ACKED — ropes may still be moving
```

🔴 **而且它今天被我改成靜音的**：`crane_cli_estop_.set_quiet_reconnect_log(true)`。
`TCP_client.cpp:155` 的註解明寫「**吊機離線是該被看見的事件**，靜音會把該看的也藏掉」，
而我對一條**安全通道**做了正是那件事 ⇒ 它可以永久壞著而不留任何一行日誌。

📌 **這條通道的死活在 `status` 裡看不到**。今天為 IMU 路徑加了 `crane_peer_age_ms` /
`crane_peer_fresh`，急停沒有對應欄位 —— 「安靜地走了另一條路」，本專案反覆踩的同一類。

## [2026-09-09c3] ✅ 重新定義工作區基準：地面起點＝玻璃最底點，`level_diff` 歸一到 0

per user「重新定義牆面最最高點」→「還是我應該先定義地面起點」。**先地面是對的**：
`zero_meters top` 存的 `home_ground_cm = |L|` 語意是「離地面起點多高」，地面必須先存在。

### 🔴 新規則：**歸零一定要在機器水平的時候做**

兩支 SD76 是**同時**歸零的 ⇒ `L−R` 這個座標整體平移「歸零當下的 L−R」。
所以在水平時歸零，`level_diff` 就**精確等於 0**，不需要重量。

⇒ 這把 09-01 那條註解描述的坑（「任何一次計米器歸零都會靜默地移動這個目標，
而且不會有任何徵兆」）從「必須記得重量」降級為「歸零前先調平」——**後者現場看得見，前者看不見**。

### 🎯 附帶效果：一條 🔴🔴 待辦被解除

`g_fine_adjust_level_diff_cm` 的**編譯預設是 0**，而 **0 現在是正確值**。
待辦表原記「重啟吊機即回 0，而 0 已知是錯的（穩定偏 +3.5°）」⇒ **本次校正後不再成立**。
⚠️ 但只對這次歸零有效：日後若在傾斜狀態下歸零，這個保護就沒了。
（`motion_hz` / `roll_correct_hz` 仍不持久化，重啟回 50 / 10。）

### 實測經過

| 步驟 | L | R | L−R | roll |
|---|---|---|---|---|
| 頂端（per user 用 GUI 手動點動調平） | −219 | −225 | **+6** | **+0.03°** |
| 玻璃最底點（到位，靜止擺幅 0.00°） | 44 | 37 | +7 | −0.42° |
| `roll_trim_ms -100` 之後 | 43 | 37 | **+6** | **+0.37°** |
| `zero_meters ground` + `set_fine_adjust_level_diff 0` | **0** | **0** | **0** | **+0.26°** |

🔬 **`level_diff=6` 由 per user 的手動校平獨立證實** —— 頂端調到 roll +0.03° 時繩長差正好 +6，
**不經過任何待測程式碼**，比早上的三點內插直接。

📌 **`level_diff` 看起來是常數，不是高度的函數**：反推水平點 **頂端 ≈6.0 / 底端 ≈6.6**，
跨 263cm 只差 0.6cm。待辦表舊記錄的「`L≈50` 處 3.8、`L≈74` 處 5.2」差距大得多，
**與本次量測不符**，該條的證據基礎要重新檢視（舊值取自傾倒事件前、n 小）。

📌 **`roll_trim_ms` 的增益隨高度變**：頂端實測 0.27°/100ms，底端 **0.79°/100ms（約 3 倍）**，
且底端 100ms 就讓 `L−R` 動了 1cm（頂端 100~150ms 完全看不到）。
⇒ 呼叫端不能假設固定增益；本日寫進 `cmd_roll_trim_ms` 註解的「0.3~0.45°/100ms」**只適用頂端**。

### ⚠️ `home_ground_cm` 的語意確認

查過全部使用處：**只有顯示用途**（GUI「剩 = home_ground − |左繩長|」，`web_backend/public/app.js:525`），
**沒有任何運動限制或安全檢查讀它**。⇒ 把「地面起點」定在**玻璃最底點**（比早上的地面高 44cm）是安全的，
而且正是作業要的：GUI 的「剩」會在玻璃底部歸零。

### ✅ 完成：新工作區基準

| | |
|---|---|
| `home_ground_cm` | **256 cm**（玻璃最底點 → 最高點） |
| 座標 | **頂端 = 0**，放繩往下增加，玻璃底 = +256；GUI「剩」= 256 − \|左繩長\| |
| 水平定義 | **`L−R = 0` 即水平**，`level_diff = 0` |
| 頂端歸零前姿態 | `L=−256 R=−256`、`roll +0.12°`、擺幅 0.00° ⇒ **本來就是平的，不需要 trim** |
| 其餘執行期值 | `motion_hz=30`、`roll_correct_hz=30`、`fine_adjust_hz=10`、`diff_tol=1`、`balance_source=imu`、`length_diff_max_cm=10` |

### 🎯 結案：`level_diff` 是常數，不是高度的函數

待辦表原有一條懸案「跨行程差 5cm 以上 ⇒ `level_diff` 是高度的函數」（依據 `L≈50` 處 3.8、`L≈74` 處 5.2）。
本次在新座標系的**兩端各量一次**：

| 位置 | L−R | roll |
|---|---|---|
| 玻璃最底點 | 0 | **+0.26°** |
| 最高點（+256cm） | 0 | **+0.12°** |

⇒ 跨 **256cm** 水平點只移 0.14°（≈**0.13cm**）。**是常數。**
📌 舊證據為何相反尚未解釋（取自傾倒事件前、n 小、且在舊零點座標裡），
但本次量測條件乾淨（同一次校正、兩端、機器靜止擺幅 0.00°），採信本次。

### `retract 100` 的附帶發現：純計米器路徑的精度下限

`fine_adjust` 走提前退出：`L=-99 R=-100 level_diff=0 diff=1 — within ±1 (diff_tol), stopping both`。
`fine_adjust_diff_tol_cm = 1` ⇒ `L−R=±1` 就算收斂，而 1cm ≈ 1.10° ⇒ **計米器路徑先天到不了 ±1°**
（該趟停後 roll = −1.13°）。**這是 per user 兩段式設計的量化依據**：粗調靠計米器、細調只能靠 IMU（`roll_trim_ms`）。

### 🔬 仍未驗證：overrun pass 的修正

`level_diff = 0` 時新舊程式碼**逐位元相同**（設計如此），而校正後 `level_diff` 正是 0
⇒ **目前的正常作業路徑驗不到那個修正**。要驗需刻意設 `level_diff ≠ 0` 跑一段、看 log 的 `err` 有沒有扣掉它。
⚠️ 但缺陷本身**已由 09-09 的 `cr_0909d.log` 6/6 段實機證實**，修正的正確性是算術層面的；
未驗證的是「修正有沒有引入新問題」，而 `level_diff=0` 這條路徑本來就沒被改動。

🔬 **overrun pass 的修正仍未驗證** —— per user 的移動全是 GUI 手動點動（`down` / `down_right` / `up_right`），
`fine_adjust` 一次都沒被呼叫到。**上頂樓那一趟若用 `pay_out`／`retract` 就會順便驗到。**

## [2026-09-09c2] 🔴 `fine_adjust` 的 overrun pass 漏套 `level_diff` —— `level_diff` 設多少都只到手 2cm

> 起點是 per user 的現場觀察：**「每次往下走到定點，第一次修正後是平的，第二次修正就讓它變歪」**。
> 那句話直接指到 `motion_fine_adjust_sync` 的兩段結構，查證後成立。

### 缺陷

`motion_fine_adjust_sync` 的收斂目標是 `L - R = level_diff`，做法是把左側讀值先減掉
`level_diff` 換到「已對齊座標」再比較（`curL_adj`、`target_cm` 都在該座標裡）。

- ✅ **收斂迴圈**：`L_stop_at = target_cm ∓ half_tol + level_diff` —— 有把偏移加回去
- ❌ **overrun correction pass**（`main.cpp:2738`）：`errL = finalL - target_cm`
  —— **拿原始讀值去比已對齊座標的 target**

⇒ `errL` 恆定多出 `level_diff`。`level_diff=6`、`FINE_ADJUST_TOLERANCE_CM=2` 時
`|errL| = 5~6 > 2` **永遠成立** ⇒ 每一次 `pay_out`／`retract` 結束都判定「左側過衝」，
把左繩收回到 `|L_raw - target| <= 2`，**剛好把偏移壓回 2cm**。

### 實機證據（`cr_0909d.log` 最後 6 段，6/6 全中）

| 動作 | 收斂迴圈後 L−R | overrun pass 後 L−R |
|---|---|---|
| pay_out −188/−191 | **5** | 2 |
| pay_out −148/−150 | **6** | 1 |
| pay_out −108/−109 | **6** | 2 |
| pay_out −66/−69 | **5** | 2 |
| pay_out −26/−30 | **4**（左側 `stop(at-target)`，本來完全不需要動） | 2 |
| retract −227/−230 | **6** | 2 |

🔴 **不管 `set_fine_adjust_level_diff` 設多少，實際到手永遠是 `FINE_ADJUST_TOLERANCE_CM`＝2cm。**
參數被**靜默夾死**，沒有任何錯誤訊息。

### 📌 這要修正 09-09 的一個既有結論

changelog 記「`level_diff=6` 讓 10 週期第一次跑完」。**機制不是我原本寫的那樣。**
`level_diff=0` 時 `errL=0`、overrun pass 不動作 ⇒ 真的收在 `L−R=0`；
`level_diff=6` 時被夾到 2 ⇒ 實際交付的是 **0→2cm 的位移，不是 0→6cm**。
今天量到的全部好處來自那 2cm。3 點量測得到的「水平點在 L−R≈6」**尚未真正施加過**。

⚠️ 附帶損耗：上表第 5 段左側判定 `stop(at-target)`、本來一步都不用動，
overrun pass 仍驅動了它一次。這在**每一次**繩索運動都發生（多餘的起停 + 時間）。

### ✅ 已部署（2026-09-09 15:53）

| 項目 | 值 |
|---|---|
| 建置 | 吊機 `~/bringup`，g++ 14.2，無警告無錯誤 |
| 新執行檔 md5 | `a5b88123`（502952 bytes） |
| 舊執行檔 | `1e47a947` → 備份 `crane_control_PI.out.prev-20260909-1552` |
| 舊原始碼 | 備份 `Crane_control_PI/main.cpp.prev-20260909-1552` |
| 重啟 | 只重啟吊機（tag `0909e`）；本體未動，靠 `TCP_client::reconnectLoop` 自行接回**有線 192.168.1.10**，實測 `set_imu_roll` 推送恢復 |
| init | 全 `[OK]`；VFD keepalive 兩側 `ok=50 fail=0`；⚠️ `PQW:12` 水閥仍失敗（既有待辦，非本次引入） |
| 執行期值已補回 | `motion_hz=30`、`roll_correct_hz=30`、`level_diff=6`（重啟後預設分別是 50／10／0） |

🔬 **部署當下的基準（尚未做驗證運動）**：`L=-231 R=-233` ⇒ **L−R = +2**、`imu_roll = +4.22°`、`imu_roll_age_ms=11`。

📌 **這組數字本身就是缺陷的第三個獨立證據**，而且給出可證偽的預測：
機器靜置在 `L−R=2`（＝被夾死的值）時**歪 +4.22°**。若水平點真的在 `L−R=6`，
用當日量得的斜率 −1.10°/cm 推算，補上 4cm 應使 roll 變為 `4.22 − 4×1.10 ≈ −0.2°`。
⇒ **修好之後第一次繩索運動，`fine_adjust` 應收斂到 L−R=6、roll 應落到 ≈0。**
對不上就表示斜率或水平點其中之一還有問題。

### 修法

`Crane_control_PI/main.cpp` 兩處把左側移進同一個座標系：

- `:2738` `errL = (finalL - level_diff) - target_cm`
- `:2827` `new_err = (curL - level_diff) - target_cm`（correction 迴圈的停止判定）

右側兩處不動（右側本來就在已對齊座標裡）。`level_diff = 0` 時兩行與原本逐位元相同。

驗證：Pi 上 `g++ -fsyntax-only -std=c++17`（`~/bringup`，g++ 14.2 目標編譯器）**通過**。
`~/bringup/Crane_control_PI/main.cpp` md5 `9dc7b04f` ＝ 本地基準，patch 對得上服役中的版本。

### 🔴 部署後必須重做的事

`level_diff` 的有效值會**第一次真正生效**，機器行為會變（偏移由 2 變成設定值）。
所以 **`level_diff` 要重新量**，不能沿用 6 —— 今天所有「6 很好」的觀察，
量到的其實都是 2 的效果。建議修好後從 `level_diff=2`（＝現行實際行為）起跑當對照，
再往上試。

## [2026-09-09 更正] 🔴 上一條的因果結論**被第四輪推翻** —— 是我拿「重現」當「因果」

> 寫在前面：下一條 changelog 說「放寬門檻造成 IMU 可用率由 67% 掉到 20%，重跑複製 ⇒ 是門檻造成的」。
> **那個結論錯了。** 回退門檻後的第四輪把它推翻。

### 四輪對照（全部有線、1 週期、起跑姿態 +0.62~1.00°）

| 輪次 | `IMU_ROLL_STALE_MS` | `src=imu` | 有效 `roll_age` 平均 | ≥8cm | ≥10cm |
|---|---|---|---|---|---|
| 1 | **750** | **67%** | 149ms | 0/6 | 0/6 |
| 2 | 1250 | 20% | 829ms | 3/6 | 1/6 |
| 3 | 1250 | 21% | 806ms | 2/6 | 2/6 |
| **4** | **750（回退）** | **8%** | 572ms | **0/6** | **0/6** |

🔴 **同一個門檻 750，第 1 輪 67%、第 4 輪 8%** —— **變異比門檻的效果大得多。**
⇒ 「放寬門檻 ⇒ IMU 可用率下降」**不成立**。

### 📌 我犯的錯：把「重現」當成「因果確立」

第 2 輪出來時我判讀為「隧道品質自己變動、A/B 不可靠」。
per user 要求「同一條件下再一次」，第 3 輪複製了第 2 輪（20% vs 21%）
⇒ **我因此放棄了原判讀，改寫成「是門檻造成的」。**

🔴 **那一步是錯的。重現性排除的是「隨機噪音」，不排除「共變的第三因」。**
第 1 輪與第 2/3 輪之間，除了門檻，還隔著兩輪運轉、機器狀態、隧道環境。
要分辨「門檻」與「其他共變因素」，需要的是**同門檻的多輪變異**
—— 而那正是第 4 輪才提供的，且它一出來就推翻了結論。

⇒ 🔴 **通則：A/B 前先量「A 自己的變異」。** 沒有那個基線，任何 A/B 差異都可能只是變異。
本例只要在改門檻**之前**多跑一輪 750，就會發現 67% 不可重現，整段冤枉路可以省下。

### ✅ 唯一在四輪中穩定成立的對照：姿態

| 門檻 | ≥8cm | ≥10cm（韌體硬限） |
|---|---|---|
| **750**（輪 1、4） | **0/6、0/6** | **0/6、0/6** |
| 1250（輪 2、3） | 3/6、2/6 | 1/6、2/6 |

**兩輪 750 的姿態都乾淨、兩輪 1250 的都不乾淨**，而 **IMU 可用率沒有跟著走**
（輪 4 只有 8% IMU，姿態卻是四輪最好之一）。

🎯 **這支持 per user 的設計原則**：**姿態主要由 `level_diff`（計米器路徑）決定，
IMU 在不在線是次要的。** 輪 4 幾乎全程 `src=meter`，姿態照樣乾淨。

⚠️ **但 n=2 vs n=2，且 IMU 可用率本身變異巨大** ⇒ 姿態這個對照**同樣不能當定論**，
只能說「目前沒有證據反對維持 750」。維持 750 的理由因此是
**「它是原值、且沒有證據支持改動」**，不是「已證實它比較好」。

---

## [2026-09-09] 切 192.168.1 有線鏈路：IMU 掉 1/3，但 `level_diff` 撐住了；放寬 stale 門檻**證實更糟**已回退

> per user「接下來改 192.168.1 線路跑一次，我們可以用 WiFi 觀察情況」。
> 🔴 **電氣干擾本身仍未處置** —— 本段是在那個前提下做的。
> 觀察通道走 WiFi（`.5.25` / `.5.26`），不受切換影響。

### 切換方式與驗收

停本體 → 重啟時**刻意不帶 `FCV_EP_CRANE_HOST`**，讓 `[2026-09-08m1]` 的自動探測選有線：
```
[crane] 走**有線** 192.168.1.10:5002（探測通過）— WiFi 192.168.5.25 未使用
[OK] crane 192.168.1.10:5002
```
`ss` 確認三條連線（主通道／estop 旁路／IMU 推送）**全部在 `192.168.1.100 → 192.168.1.10`**。

### 閒置基準：比 09-08 記載的好

20 筆 @1Hz：`imu_roll_age` 平均 **203ms**、最大 **491ms**、**20/20 fresh**、**0 筆破 750**。
（09-08 記「隧道閒置最大空窗 903ms」。⚠️ 取樣方式不同 —— 我是抽樣讀 age、不是連續量空窗
—— **不可直接比**，但至少閒置時 IMU 是活的。）

### 🎯 運動中：IMU 掉三分之一，但週期跑完了

第一輪（`stale 750`）163 個 balance tick：

| | |
|---|---|
| `src=imu` | **109（67%）** |
| **`src=meter`（過期退回）** | **54（33%）** |
| 有效 `roll_age` | 平均 **149ms**、最大 **737ms**（**差 13ms 就破門檻**） |

代價：移動 5.4 → **11.0 s/步**、下行純移動 7.36 → **3.63 cm/s**、
左右差 中位/max 2/4 → **4/6**、**roll 超標後自行回復 0 → 5 次**。

✅ **但 1/1 完成，兩個門檻都沒被逼近**（≥8cm 0/6、≥10cm 0/6）。
🎯 **`level_diff=6` 撐住了** —— IMU 掉 1/3 時，計米器路徑靠正確的水平參考把機器維持在可用狀態。
**這正是 per user「IMU 連不上就放棄」設計原則的實測驗證**：主線不依賴會斷的那個訊號。

### 🔴🔴 放寬 `IMU_ROLL_STALE_MS` 750 → 1250：實測**更糟**，已回退

per user 指示改成 5 × `BALANCE_TICK_MS`。我當時的推論是
「有效樣本最大 737ms、差 13ms 就破 ⇒ 很多退回只是差一點點，放寬可望救回大部分」。

**三輪對照（起跑姿態對齊到 +0.62°/+0.63°）：**

| 門檻 | `src=imu` | 有效 `roll_age` 平均 | ≥10cm（韌體硬限） | 移動 s/步 |
|---|---|---|---|---|
| **750** | **67%** | **149ms** | **0/6** | 8.7 |
| 1250 | 20% | 829ms | 1/6 | 12.0 |
| 1250（重跑） | **21%** | **806ms** | 2/6 | 12.0 |

🔴 **放寬門檻 67%，IMU 可用率反而由 67% 掉到 20%。**
🎯 **重跑幾乎完全複製**（20% vs 21%、829 vs 806ms）
⇒ **不是隧道隨機變差，是門檻改動造成的。**

📌 **我的推論被推翻了兩次，而且第一次我還推錯了方向**：
第二輪出來時我判讀成「隧道品質自己變差、A/B 不可靠」，
**是 per user 要求「同一條件下再一次」才把它釘死成「門檻造成的」**。
🔴 **通則：把異常歸因給「環境隨機變動」之前，先重跑一次。** 那是最便宜的鑑別方法，
而我當時直接跳到了「不可比」的結論 —— 那個結論會讓人停止追查。

#### 推測的機制（🔴 未實證）
門檻寬 ⇒ 採用 800~1200ms 的舊 roll ⇒ **用過時姿態下 trim** ⇒ 修正與實況不符
⇒ 機器晃得更厲害 ⇒ VFD 動作更劇烈 ⇒ **隧道干擾更嚴重** ⇒ 推送更慢。
⇒ **這個門檻會透過控制品質回饋到鏈路品質本身**，不只是「要不要用舊資料」。
📌 這是本檔他處那句「**過期的姿態拿來驅動馬達比不修更危險**」的實測版本。

⚠️ **結論的限制**：三輪都只有 1 個週期、都在有線鏈路、正回饋是推論。
要證實需「固定隧道品質、只改門檻」，而那在**電氣干擾處置之前做不到**
—— 正是 09-08 待辦表把這條排在干擾之後的理由。

✅ 已回退為 750。**建置產物 md5 與改動前完全相同（`1e47a947`）**，證實回退乾淨（新增的都是註解）。

### 🎯 順帶：兩段式水平修正第一次完整跑通

```
粗調 roll_correct  +1  ： +5.40° → +2.21°   （繩長差 1 → 4）
細調 roll_trim_ms +300 ： +2.21° → +1.07°   （繩長差 4 → 5）
細調 roll_trim_ms +150 ： +1.07° → +0.62°   （繩長差 5 → 6）
```
**+0.62°，在 ±1° 規格內** —— 而今天稍早只用 `roll_correct` 時，1cm 步階讓我在
+1.06° 與 −2.96° 之間來回跳、**進不了那個帶**。
📌 且**繩長差自然收斂到 6**，正好等於量出來的 `level_diff` —— 三者彼此印證。

### 🐛 我的操作失誤

想另外寫一支 3Hz 監看腳本，用 `&` 丟背景，**外層指令結束時被一起收掉，資料全失**。
所幸吊機 log 的 `[BAL]` 每筆都帶 `roll_age` 與 `src=`，重建出來的資料**比我要量的更好**
（163 個真實 balance tick，不是抽樣）。
📌 **要量的東西如果系統已經在記，先去看有沒有現成的，不要另外造一個會失敗的量測。**

---

## [2026-09-09c1] 吊機四項水平修正：A+B+C 是參數沒設對，只有 D 是新程式碼

> per user「把吊機修完 / 全部」。吊機已建置部署（`crane_drv.out` md5 `1e47a947`，
> 備份 `crane_control_PI.out.prev-20260909-1440`），重啟後實測通過。

### 🔴 先講一件事：問題的根源是我今天早上自己造成的

`g_fine_adjust_level_diff_cm` 的註解（09-01）寫著：

> 真正的值必須**實機量一次**（把機器調到 roll≈0，讀當下的 L−R）
> ⚠️ **每次歸零計米器之後都要重量** —— 它描述的是「這組零點下水平長什麼樣」，
> **不是機器的固有性質**。

而我今天早上第一件事就是 `zero_meters ground` + `zero_meters top`
⇒ **舊的水平參考必然失效，而我沒有重量**，然後花了一整天追一個由此產生的偏移。

📌 註解甚至預告了症狀：「**任何一次計米器歸零都會靜默地移動這個目標，而且不會有任何徵兆。**」

### A + B：其實是同一件事，而且不用改程式碼

`fine_adjust` 比絕對讀值差、`length_diff` 守衛比相對位移差 —— 這個「不一致」**是設計，不是缺陷**：
`level_diff` 就是為了讓絕對比較能用而存在的介面。它只是被設成 0。

**照註解的程序實機量測**（三點，靜置後連測 3 次確認穩定）：

| L−R | roll |
|---|---|
| +3 | +3.46° |
| +4 | +2.07° |
| +7 | −1.22° |

斜率 **−1.10 °/cm**，roll 過零 ⇒ **L−R ≈ 5.9** ⇒ `set_fine_adjust_level_diff 6`。

#### 🎯 順帶把「0.85°/cm 的定義待釐清」那條結案了

本日我一度說「實測 −4.26°/cm，與記載 0.85 差 5 倍」。**那是單位搞錯**：
- **0.85~1.10 °/cm** = 每 **cm 的 L−R 變化**（本次三點量測證實）
- **−3~4°/指令** = 每一道 `roll_correct 1`，因為它**兩側各走 |delta|**、加上 10Hz 過衝
  ⇒ 一道 `1` 指令實際造成 **3~4cm** 的 L−R 變化

⇒ **兩個數字都對，量的是不同的東西。** 記載的 0.85 沒有錯。

### C：兩段式增益排程 —— 結構本來就在，兩個值相同讓它失效

```cpp
start_hz = (abs_cm <= ROLL_CORRECT_APPROACH_CM /*2*/)
         ? g_roll_finish_hz     // 小誤差：全程慢
         : g_roll_correct_hz;   // 大誤差：先快，最後 2cm 降速
```
**距離就是誤差的代理**（`delta_cm` 由 roll 推得）⇒ per user「誤差大馬達動作大」的結構已存在。
但兩者預設都是 10 ⇒ 兩段變同一段（註解自己寫「這使本收尾段在預設值下等於停用」）。
✅ `set_roll_correct_hz 30`，`roll_finish_hz` 維持 10。

🔴 **per user 否決把 `roll_finish_hz` 降到 5**：「**5 幾乎吊繩不會動**」
⇒ **10Hz 是繩子會動的實務下限**。這推翻了程式碼註解「5Hz → 每側 1cm」的可用性，
也決定了 D 的設計：**細度不能來自降 Hz，只能來自縮短時間。**

### D：新增 `roll_trim_ms <±ms>` —— 唯一的新程式碼

**可行性先量再寫**（用既有的 `up_right on/off` 手動計時）：

| 脈衝 | Δroll | ΔL−R |
|---|---|---|
| 100ms | −0.27° | **0** |
| 150ms | −0.50° | **0** |
| 200ms | −0.66° | +1 |
| 300ms | −1.37° | +1 |
| 400ms | −1.67° | +2 |

⇒ **~0.3~0.45°/100ms，比 `roll_correct` 的最小步階（3~4°）細 10 倍以上。**
🎯 **100~150ms 時 `L−R` 讀數完全不變，但 IMU 量得到** —— **動作真實存在但小於計米器解析度**，
這就是繞過 1cm 量化下限的直接證據。

#### 設計取捨
- 🔴 **走既有 hold 旗標（`cmd_hold`）** ⇒ 沿用 `hold_loop` 的張力保護
  （總和門檻 + 逐側低/高/差，觸發即 `hold_all_off()` + 廣播 EVT）。
  **刻意不走 `pay_out_left|right on|off`** —— 指令表註明那條「debug，**無張力安全**」。
- 硬上限 **500ms**（實測 400ms≈1.67°，再長就該用 `roll_correct`）
- `motion_active` 時拒絕（不與運動路徑並行驅動 VFD）
- 一律 `hold_all_off()` 收尾（任何路徑都不會把旗標留在 on）
- 上限訊息由常數推導，不寫死數字

#### 實測（重啟後）
| 指令 | Δroll | ΔL−R | 回覆 |
|---|---|---|---|
| +150 | −0.39° | 0 | `OK roll_trim_ms 150 (up_right 150ms)` |
| −150 | +0.31° | 0 | `OK ... (up_left 150ms)` |
| +300 | −0.93° | +1 | OK |
| −300 | +0.77° | −1 | OK |

三道守衛也都正確：無參數 → `ERR usage`；600ms → `ERR out_of_range (max 500ms)`；0 → `OK`。

### 🔴 兩條必須記住的

1. **`roll_trim_ms` 的效果只有 IMU 量得到** ⇒ IMU 不在線時**不可驗證**，
   呼叫端必須退回 `roll_correct`（粗調，計米器可驗）。
   **與 per user「IMU 連不上就放棄」的原則一致：粗調永遠可用，細調是加分項。**
2. **所有執行期參數重啟即歸零**（本次重啟吊機後 `level_diff`／`roll_correct_hz`／`motion_hz`
   全掉，手動補回）—— **症狀是安靜地退回舊行為**。既有待辦。

### 📌 部署過程
本體與兩個 WEB **未重啟**，靠 `TCP_client` 的 500ms `reconnectLoop` 自動重連 ——
實測 `crane_peer_age_ms=97`、`crane_peer_fresh=1`
⇒ **今天新加的探針欄位在重連情境下也證明有用。**
收尾用 FIFO `echo exit`，**不用 `pkill -f "sleep infinity"`**（09-08 證實會殺掉主程式的 stdin writer）。

---

## [2026-09-09 更正 + 實測] 🔴 「滑台把機體推歪」是錯的 —— per user 指出，實驗也證偽

### 更正：本日稍早我把 `停後roll +3~4°` 歸因於滑台橫走，那是錯的

🔴 **per user 一句話就指出關鍵：滑台橫走時吸盤是吸著的** —— 機體被錨定在玻璃上，滑台移動不會讓它擺。
而 `停後roll` 是在步驟 ⑦ 下降 40cm **之後**量的，那時推桿已收回（步驟 ④）、**機體懸空**
⇒ 那個數字量的是**自由懸吊姿態**，與滑台無關。

**兩組獨立證據都證偽了我的歸因：**

1. **`mission_run` 那兩輪根本沒有滑台、沒有吸盤、沒有推桿**，靜止傾角照樣由 −0.27° 累積到 **+3.72°**。
2. **本輪把 `RAIL_CM` 100 → 50**（滑台耗時 6.3s → 4.4s，確實變快），結果：

| | `RAIL_CM=100` | `RAIL_CM=50` |
|---|---|---|
| 左右差 中位 / p90 / max | 4 / 9 / **10** | 3 / 8 / **11** |
| `停後roll` | +3~4° | +1.1 / +3.1 / +3.1 / +2.6° |

**滑台行程減半，左右差沒改善（最大值還更糟），`停後roll` 照樣回到 +3°。**

⇒ ✅ **正確解釋簡單得多**：+3~4° 就是這台機器**繩長接近齊平時的自然懸吊姿態**
（本日在頂端、53cm、上行過程各量到，皆 +2.9~3.8°）。每次懸空它就回到那個角度。

📌 **我犯的錯的形狀**：我看到「每步都出現 +3~4°」就去找**步驟裡的**成因，
而沒有先問「**這個角度在沒有那些步驟時也會出現嗎**」—— 而我自己稍早的 `mission_run` 資料就有答案。
🔴 **通則：把一個現象歸因給某個步驟之前，先確認沒有那個步驟時它不出現。**

### ✅ `roll_recover` 的修正被決定性驗證

```
⚙ 第 1 次：L=-129 R=-125（差 -4）roll +6.44° → roll_correct 1
⚙ 第 2 次：L=-127 R=-127（差 +0）roll +4.80° → roll_correct 1
✅ roll 已回到 +1.00°（門檻 3.0），第 2 次後達成
🔧 roll 自動修正並續行: 1 次
```
🎯 **第 2 次修正正好發生在 `L-R=0`（繩長齊平）** —— **舊版會在那一行中止**。
新版依「這次修正有沒有改善 |roll|」續行，救回並完成該步。

### 🔴 本輪中止：韌體硬限，不是腳本門檻

```
🔴 中止：pay_out 失敗：ERR length_diff 11cm > 10cm
```
腳本門檻（8cm／連續 3 筆）沒攔到，是吊機自己的 `length_diff_max_cm=10` 直接拒絕動作。
機器停在高度 174（L=-174 / R=-176）。

### 🟡 左右差為何在 `cycle_test` 比 `mission_run` 差 —— 目前只有假說

| | 每段移動距離 | Δmax 典型 |
|---|---|---|
| `mission_run` | **224cm** | 3~5 |
| `cycle_test` | **40cm** | 8~11 |

**假說**：每步都從 +3° 的自由懸吊姿態起步，平衡迴路要修一個大誤差 ⇒ 大 trim ⇒ 大左右差；
而 40cm（約 7.3 秒）**不夠它收斂**，移動結束時還在修的半途。
⚠️ **這只是假說，尚未設計實驗檢驗** —— 我今天已經在同一個地方錯過一次，不重複同樣的動作。
可檢驗的做法：把步距由 40cm 改大（例如 80cm）跑一輪，若 Δ 下降則支持。

### 其他

- ✅ 風扇全程正常、手臂 `OK skipped`
- ⚠️ **步 5 的推桿伸出耗時 13.2s**（其餘 2.7~4.4s）—— 單筆異常，未查
- ⚠️ 真空仍不穩：`-41/-59/-1/0`、`-1/1/-61/-59`、`-66/-70/-69/0`
- 🐛 **`state=error` 會把 `relay_status` 一起擋掉**（`ERR state_violation current=error`）
  ⇒ 在 error 狀態下讀不到幫浦，**看起來像幫浦壞了，實際只是被狀態閘門擋住**

---

## [2026-09-09 實測] 10 週期在週期 1 步 4 中止 —— 挖出一個前提錯誤的自動修正機制

### 中止本身是**正確**的

```
⚠ 週期1步4: roll 連續 3 筆超過 6.0°（最後 8.00°）—— 持續傾斜，非擺盪
⚙ 第 1 次：L=-55 R=-51（差 -4）roll +8.04° → roll_correct 2
🔴 繩長已齊平（L-R=0）卻仍歪 +3.89° —— 不是繩長造成的，不再動繩，交回中止
```

持續性判定**正確地區分了「擺盪」與「持續傾斜」**（連續 3 筆 > 6°），自動修正也動了
（`roll_correct 2`：8.04° → 3.89°）。中止不是誤判。

### 🔴🔴 但 `roll_recover` 的守衛在這台機器上前提是錯的

`cycle_test.py` 的 `roll_recover` 寫著：
> 🔴「繩長已經齊平卻仍然歪」是另一回事 —— 那不是繩長造成的，再動繩只會更糟

**它用「繩長齊平」當作「繩長已經沒問題」的判準。**
而**這台機器的水平點不在齊平**——本日五個獨立資料點：

| 場合 | 繩長差 | roll |
|---|---|---|
| 頂端 `roll_correct 1` 前 | 0（齊平） | **+3.75°** |
| 頂端 `roll_correct 1` 後 | **4** | −0.51° |
| 高度 53 修正前 | 0（齊平） | **+3.73°** |
| 高度 53 修正後 | **3** | +0.63° |
| **上行過程（無人下指令）** | 0 / 0 / 0（齊平三段） | **+3.25 / +3.36 / +2.89°** |
| 同上，最後兩段 | **2** | +1.44° → **+0.75°** |

🔴 **最後那組是平衡迴路自己動出來的，不是我下的指令** —— 第三次獨立證據。

⇒ **`roll_recover` 把繩長往齊平修 ＝ 往最歪的方向修**，然後在最歪的地方宣告
「齊平了還歪 ⇒ 不是繩長問題」並中止。**三個步驟共用同一個錯誤前提。**

⚠️ **守衛的原意是對的**（防「繩長之外的原因造成的傾斜」，避免無止境動繩）。
錯的是**用「齊平」當代理指標**。正確的判準應該是什麼我沒有答案 ——
但**「roll 有沒有因為這次修正而改善」是可以直接量的，不需要知道水平點在哪**。

### 🎯 為什麼傾角會一直被推出來：平衡迴路在繩子不動時**根本不跑**

`hold_loop()`（`Crane_control_PI/main.cpp:2359`）：
```cpp
if (cur_sync_dir != 0 && prev_sync_dir == cur_sync_dir) {
    ... apply_balance_trim(...)          // 只有在「同向的繩索運動進行中」才修
}
```
`cur_sync_dir` 是 hold 方向（0＝沒有在動）。

⇒ **滑台橫走 0→100→0 的那 6.3 秒，繩子沒動 ⇒ 平衡迴路一個 tick 都不會跑。**
機體被滑台甩到 +3~4°，而**沒有任何東西在修**；要等下一個 40cm 移動才會補回來一部分。

📌 這解釋了 1 週期那輪每一步的 `停後roll` 都是 +3~4°，也解釋了左右差為何惡化
（平衡迴路一動起來就要修一個已經累積好的大傾角 ⇒ 製造更大的左右差）。

### 其他

- ✅ **風扇全程正常**（`OK pwm_set duty=5.0`，無 `pwm_freq_write_failed`）—— `0902c` 的中止原因未重現
- ✅ 推桿無 TIMEOUT；手臂 `OK skipped`（`arm_attached=off`，未動）
- ⚠️ **真空吸附不穩**：同一輪出現 4 顆 / 2 顆 / 2 顆（`-1`、`0` ＝ 沒吸上）
- 📊 作業速率與 1 週期輪一致：**下行含開銷 1.61 cm/s（每公尺 62.2s）**

### 收尾

per user「A」：`roll_correct +1` 一步把 +3.73° 修到 **+0.63°**（張力差 22.6 → 15.9 kg），
再分段收回頂端（每段檢查左右差 ≤8、roll ≤6）。
最終：**高度 223、roll +0.75°、繩長差 2cm、`tension_valid=1`**。
本體 `state=error` **per user 指示保留不清**。

---

## [2026-09-09 實測] 姿態修正、`init` 卡住的真因、以及 `crane_cmd_` 讀到半行殘片

### ① `roll_correct 1` 一步修好姿態 —— 但靈敏度與記載差 5 倍

| | 之前 | 之後 |
|---|---|---|
| `raw_x` | **+3.75°** | **−0.51°** |
| 繩長 L / R | −225 / −225 | **−223 / −227** |
| 張力 L / R | 59.9 / 30.8 | **54.5 / 45.7** |
| 張力差 | **29.2 kg** | **8.7 kg** |

🔴 **實測 −4.26°/cm，而待辦表記的是 −0.85°/cm** —— 差 **5 倍**。
而那條待辦本身就寫著「這個換算的定義／正負號要回頭釐清」⇒ **至少有一個的定義是錯的**。
⚠️ **我不會用 −4.26 取代 −0.85**：一個資料點不能推翻另一個，且本次從 3.75° 起跳，可能不在線性區。

📌 **方法**：因為正負號在待辦表上未結案，**我沒有用那個數字去算方向**，
而是先下 1cm 最小步、量往哪邊動（與本日驗下降方向同一套）。

📌 **順帶一個反例**：待辦表寫「水平與張力平衡**互斥**」（09-01：水平時張力 59.7/34.6＝1.7×）。
本次**水平時張力反而更平均**（29.2 → 8.7 kg），兩者同向改善。條件不同（高度／載重），
但值得記為反例。

### ② 🔴🔴 `init` 卡在 `paused_on_error` —— 真因與表象是兩件事

**表象**：`[water_inlet] CLOSE gave up after 3 attempts — valve state UNKNOWN` → `PAUSE-ON-ERROR`。

**真因**：這台機器的**水路 PQW 根本不在線**。乾淨連線直接問吊機：
```
water_inlet off  →  ERR pqw_water_offline
```
開機的 `EVT device_state` 早就寫著 **`pqw_water=0`**。⇒ 這一步在本機**永遠不可能成功**。

🔴🔴 **但本體看到的不是那個錯誤，是 `EVT` 廣播的半行殘片**：
```
[crane_cmd] 'water_inlet off' -> ss phase=main_loop l_cm=221 r_cm=223 target_cm=224 elapsed_ms=14232
[crane_cmd] 'water_inlet off' -> _cm=220 r_cm=219 target_cm=223 elapsed_ms=14239
[crane_cmd] 'water_inlet off' -> t_cm=223 elapsed_ms=14200
```
三個「回覆」都是 `EVT motion_progress` 的**尾段**，開頭被切掉。

⇒ **`crane_cmd_` 自己的收包路徑沒有行緩衝** —— 與我今日在 `imu_push_loop_` 補的
（`changelog [2026-09-09m5]`）是**同一個缺陷**，只是那裡補了、這裡沒有。
📌 AI-2 今日也預測過同一形狀（`recv` 單次最多 127 bytes、`EVT device_state` 一行遠超過 ⇒ 必定被切）。

🔴 **後果比「多印一行怪東西」嚴重：本體因此分不出「裝置不在線」與「通訊亂掉」。**
吊機每次都明確回了 `ERR pqw_water_offline`，而本體記成 `valve state UNKNOWN`。
**一個明確的診斷被降級成「不知道」。**

✅ 處置（per user「水先不測」）：`skip` ⇒ `state` 回到 `ready`，幫浦維持 ON。

### ③ 🔴 我更正一句話：`arm_attached off` **有用**，守衛在下一層

我先前說「`arm_attached off` 沒用，`cmd_arm_deploy_f` 不檢查它，所以擋不掉 bail 風險」。**錯了。**

守衛不在 `cmd_arm_deploy_f`，在它呼叫的 **`arm_cmd_`**（`app/WASH_ROBOT.cpp`）：
```cpp
if (!arm_attached_.load()) {
    std::cout << "[arm_cmd] '" << line << "' SKIPPED (arm_attached=off)\n";
    return "OK skipped";
}
```
⇒ **所有手臂指令回合成的 `"OK skipped"`**，`cycle_test` 收到 `OK` 就續行。
**我只讀了 `cmd_arm_deploy_f` 的開頭就下結論，沒往下追一層。**

📌 **這與本日稍早記的 `crane_attached_=off` 回 `"OK skipped"` 是同一個模式** ——
我當時還把它列成待辦（「會讓所有吊機保護靜默失效」）。手臂這裡是同一件事，只是這次對我們有利。
🔴 **通則：「這個函式檢查了什麼」要看到它呼叫的下一層為止。** 守衛常常不在你打開的那一層。

### ④ 1 週期實測（風扇正常，但左右差摸到韌體硬限）

✅ **風扇沒問題**：收尾 `OK pwm_set ch=1 hz=50 duty=5.0`，全程無 `pwm_freq_write_failed`
（`0902c` 的中止原因未重現）。✅ 推桿無 TIMEOUT。✅ `arm_park: OK skipped`（手臂未動）。

| 左右差分布（6 段） | 09-02 `0902d` | **本輪** |
|---|---|---|
| 中位 | 3 | **4** |
| p90 | **4** | **9** |
| 最大 | **5** | **10** ← 等於韌體 `length_diff_max_cm` |
| ≥6cm | **0%** | **33%** |

🔴 **持續性判定救了 2 次**（`超標後自行回復 2 次`）—— **沒有 `m6` 這輪會中止。**

🎯 **`停後roll` 每步都回到 +3~4°**：開跑前才修到 −0.49°，**一個週期就推回 +4°**。
手臂沒動 ⇒ **是滑台 0→100→0 橫走 100cm 把質心甩過去**。
⇒ 這解釋了左右差為何變大：平衡迴路要修一個**持續被滑台推出來**的傾角，就得製造更大的左右差。

⚠️ **本輪姿態數據不可與 09-02/09-03 直接比對**（起始姿態不同）。

### 📊 順帶量到的作業速率（本專案首次）

| | |
|---|---|
| 下行純移動 | 5.51 cm/s（每公尺 18.2s） |
| **下行含開銷** | **1.61 cm/s（每公尺 62.2s）← 實際作業速率** |
| 上行回程 | 11.52 cm/s（每公尺 8.7s） |
| **一趟來回** | **每公尺約 70.9 s** |
| 單步開銷 | 伸出 3.5 / 真空 1.6 / 滑台 6.3 / 收回 3.3 / 移動 7.3 / 其他 2.9 s（整步 **24.9s**） |

---

## [2026-09-09 實測] 第二輪 10 趟 + 姿態累積量測 —— 📌 **這是已知的設計前提，不是新發現的故障**

> 🔴 **本條的框架由 per user 當場更正。** 我原本把「傾角與張力差單調上升」寫成
> 「不是隨機變異、有明確物理方向、值得追」，語氣像是找到了故障。
> **per user：捲筒本來就是這樣，`計米器 + IMU 修正` 就是為了補償它而存在的。**
> ⇒ 下面的數字是**控制系統要對付的條件**，不是待修的缺陷。留檔是為了把這個條件量化。

### 第二輪 10 趟：20/20 完成，但姿態指標全面較差

| | 第一輪 | 第二輪 |
|---|---|---|
| 出帶 ±1° | 65/645 = **10.1%** | 185/764 = **24.2%** |
| 各趟最大 \|roll\| 的平均 | 1.67° | **2.54°** |
| 最大 \|roll\| | 4.49° | 4.17° |
| 左右差自行回復 | 1 次 | **3 次** |
| roll 自行回復 | 0 | **0** |

⚠️ **兩輪起跑條件不同**（第一輪起跑靜止 −0.27°、第二輪 +2.01°）
⇒ 依 09-01 續七「run-to-run 變異 ≈ 效果量」，**不宜逕稱退化**。第二輪整輪都在跟一個已存在的 2° 偏差角力。

### 🎯 姿態累積的量化（頂端／底端交叉量測）

| 時點 | 位置 | 靜止 `raw_x` | 張力差 (L−R) |
|---|---|---|---|
| 兩輪之前 | 頂 | **−0.27°** | 19.0 kg |
| 第一輪後 | 頂 | +2.01° | 23.1 |
| 第二輪後 | 頂 | +2.99° | 26.1 |
| 下降後（剛停） | **底** | **+1.36°** | 23.5 |
| 靜置 1.7 分 | 底 | **+1.38°**（不回復） | 23.6 |
| 升回後 | 頂 | **+3.72°** | **29.5 kg** |

**兩個效應疊在一起：**

1. **位置相關** —— 同一時段、同一組繩長讀值（−225/−225），**頂端 3.72° vs 底端 1.38°，差 2.3°**。
   靜置 1.7 分鐘不回復（1.36 → 1.38，連測 3 次相同）⇒ 不是運動殘留。
2. **隨趟數累積** —— 頂端四次量測 `−0.27 → 2.01 → 2.99 → 3.72°`，張力差 `19.0 → 29.5 kg`，
   兩量同向同步、四點無一回頭。

🔴 **而計米器完全看不到**：繩長讀值始終相等（−225/−225、−227/−227）。

📌 **per user 確認這正是捲筒的預期行為**（有效捲繞直徑隨疊繞變化）——
**這也就是為什麼平衡不能只靠計米器、必須有 IMU 修正**：
計數相同 ≠ 繩長相同，而 IMU 量的是真實姿態。
⇒ 本次數據的價值在於**把這個條件的量級量出來了**：
**每 10 趟來回約 +1°／+3 kg，頂底差約 2.3°。**

### 🔴 我在過程中修正了自己兩次

1. 我先說「下降讓傾角掉一半 ⇒ **排除**繩子被拉長且不可逆」——**推論太快**。
   位置相關性正好是「有效直徑不同」的預期表現；降下去變好只代表**底端的累積量較小**，不是沒有累積。
2. 我把它寫成「值得追的異常」——**框架錯了**，它是已知的設計前提（見本條開頭）。

### `m6` 的 roll 半邊：40 次橫越後仍未驗證

兩輪 `roll nearmiss` 皆為 **0** ⇒ 1,409 筆取樣中無一超過 5.0°（最大 4.49／4.17 只是逼近）。
⇒ **「roll 超標後會不會自己回復」仍無答案。** diff 半邊已驗證（第一輪第 7 趟）。

### 運動中 vs 靜止（補上先前漏掉的取樣）

| 段 | 運動中 \|roll\| 平均 / 最大 / 出帶 |
|---|---|
| 頂 → 底（227cm） | 1.23° / 3.05° / **61%** |
| 底 → 頂（224cm） | 1.14° / 4.05° / **34%** |

📌 呼應本日稍早的教訓：**靜止值不能代表過程**。

---

## [2026-09-09 實測] `mission_run.py 10 224` 第一輪：20/20 完成，並驗證了 `m6` 的一半

> 走 **WiFi**（`FCV_EP_CRANE_HOST=192.168.5.25`），**不是**有線鏈路驗收。
> 空載、未貼牆、無清潔動作 ＝ 最寬鬆條件。log：吊機 `~/bringup/cycle_logs/mission_0909_10trip.log`。

### 結果

| | |
|---|---|
| 完成度 | **20/20 次橫越，無中止** |
| 各趟最大 \|roll\| 的最大值 | **4.49°**（第 7 趟下） |
| 平均 | 1.67° |
| 取樣 645 筆，超出 ±1° | **65 筆（10.1%）** |
| 超標後自行回復 | **roll 0 次／左右差 1 次** |
| 單趟耗時 | 下 ~16.7s／上 ~18.6s |

### 🎯 「左右差 1 次」＝ `m6` 被驗證了（而且擋住一次假中止）

第 7 趟下：`avg 1.15 / max 4.49 / 出帶 34% / **Δmax 9**` —— **9cm 超過 `DIFF_TRIP=8`**，
但在連續 3 筆之內自行回復 ⇒ 任務繼續。

🔴 **用 `m6` 之前的程式碼（1 筆就中止），這輪會在第 7 趟掛掉。**

### ⚠️ 但 roll 那一半仍未驗證

`roll 0 次` ⇒ 全程**沒有任何一筆超過 5.0°**，4.49° 只是逼近。
⇒ **「roll 超標後會不會自己回復」仍然沒有答案。**
📌 **diff 與 roll 共用同一套邏輯，但共用不等於驗證。** 要說清楚哪一半驗過、哪一半沒有。

### 📌 第 7 趟證實了一件事：大 roll 與大左右差是**同一個事件**

`cycle_test.py` 的註解早就寫過機制：**吊機的 IMU 平衡迴路就是靠製造左右差來修正 roll 的**
（`apply_balance_trim` 調左右不同的 Hz → 直接產生左右位移差）。
第 7 趟下同時出現 `roll 4.49°` 與 `Δ 9cm` —— **不是兩個問題，是同一個事件的兩面**：
大 roll → 平衡迴路大力修 → 大左右差。

⇒ 先前那次 `roll 5.06°` 中止，**應該也伴隨著類似的 diff**，只是腳本先停了、沒留下數字。

### 起步大擺盪的發生率（更正我自己兩次）

| 我先前說 | 實際 |
|---|---|
| 「>5° 只有 2 筆、佔 0.0%」 | ❌ **分母錯了** —— 瞬態只持續一瞬，用取樣筆數當分母必然稀釋它 |
| 「約 1/3 起步」 | ❌ 樣本太少 |
| **實際** | **23 次下降起步中 2 次明顯事件**（5.06° 與 4.49°）≈ **1/10** |

📌 **通則：瞬態的發生率要用「事件數」當分母，不是「取樣數」。選錯分母會讓常見事件看起來罕見。**

### ⚠️ 對照規格：離 ±1° 還有距離

`mission_run.py` 抬頭寫的任務目標是「**全程本體平衡 roll ±1°**」。
實測 **10.1% 的取樣超出 ±1°**、最大 4.49°。沒有任何中止，但**這是最寬鬆的條件**
（空載／未貼牆／無清潔動作）。

### 🔴 計米器漂移會累積

起跑 **223** → 收工 **226**，20 次橫越累積 **+3cm**（與先前量到的「一趟來回 2cm」同量級）。
端點容差 `TOL=5` 這輪撐得住，**但它會累積** ⇒ 連續跑更多趟時要注意會不會漂出容差。

### 🔴 靜止時繩長相等但機器不水平 —— 「重心偏左」那條待辦的直接證據

收工靜止：**L = −226 / R = −226（左右差 0）**，而 `raw_x = **2.01°**`、張力 **60.5 / 37.4 kg（差 23kg）**。
（跑之前靜止是 −0.27°。）
⇒ **繩長相等時機器不水平；要水平就得讓繩長不等。** 這正是待辦表「重心偏左：水平與張力平衡互斥」。

---

## [2026-09-09m6] `mission_run.py` 補上持續性判定 —— 移植 `cycle_test.py` 09-04 就做過的決定

> 範圍：`Linux_test/mission_run.py`（判定邏輯 + 計數回傳 + 總結）、
> `Linux_test/cycle_test.py`（一條 stale 註解）。已部署吊機、md5 一致、`py_compile` 通過。
> 🔴 **本次改動今日未被驗證**（見下方「未驗證」段）。

### 起因

`mission_run.py 1 224` 煙霧測試在**下降 13cm、3.3 秒、第 3 筆取樣**就中止：`roll -5.06° > 5.0`。

但同一份吊機 log 的 **5,768 筆 4Hz 取樣**顯示機器平常非常穩：

| | |
|---|---|
| 平均 \|roll\| | **0.73°** |
| >3° | **3 筆（0.1%）** |
| >5° | **2 筆（0.0%）** |

吊機平衡 log 直接拍到那個瞬態：
```
[BAL] src=imu err=0.72deg trim=4.32Hz L=27.84 R=32.16
[BAL] src=imu err=5.3deg  trim=15Hz   L=22.5  R=37.5     ← 一個 250ms tick 內 0.72° → 5.3°
```
⇒ 對應待辦表那條**未查明**的「單側在起步 1 秒內暴走」（2026-09-01）。

### 🔴 真正的問題是兩支腳本的中止語意不一致

| | `mission_run.py`（改前） | `cycle_test.py` |
|---|---|---|
| ROLL_TRIP | 5.0 | 6.0 |
| 連續筆數 | **1 筆就中止** | **連續 3 筆**（≈1 秒） |

而「連續 3 筆」是 `cycle_test.py` 在 **2026-09-04 特地補上並驗證過**的
（10d 統計「超標後自行回復（未達連續 3 筆）: 1 次」＝正確地沒為瞬態中止）。
⇒ **同一個修正從來沒有被帶到 `mission_run.py`**，於是同一類瞬態在這裡照樣中止整個任務。

📌 **通則：兩支做同一件事的腳本，其中一支學到的教訓不會自己走到另一支身上。**
這次是「中止語意」，形狀與 09-08「兩條線並行時各自的待完成會互相過期」同族。

### 改法

- `DIFF_PERSIST = 3` / `ROLL_PERSIST = 3`（取樣 0.3s ⇒ 約 1 秒）
- streak / nearmiss 計數，**照 `cycle_test.py` 的形狀**，不自己發明
- 🔴 **門檻值完全不動**（5.0 / 8.0）—— 改的是「要連續幾筆」，不是「多大才算超標」
- 🔴 `tension_valid=0` **維持瞬時中止**：那是感測失效不是瞬態，過載保護會靜默失效
- nearmiss 計數**回傳並印在總結**（原本只留在函式裡＝量了又丟掉），
  因為「超標過但自行回復」正是判斷「瞬態 vs 真的歪著」的依據

### 🐛 順帶更正 `cycle_test.py` 一條自我矛盾的註解

`:104` 寫著「⚠️ roll 門檻**維持瞬時判定**，刻意不比照辦理」，
而**它正下方三行**就是 `[2026-09-04] ROLL_PERSIST = 3`。
⇒ **結論改了、由它產生的敘述沒跟著改**，與 09-08 記的那三條假待辦同型。已標註更正、保留原文留沿革。

### ⚠️ 未驗證 —— 這一條要記住

改完後重跑 1 趟：**通過**（下 max 1.44° / 上 max 1.03°，遠低於 5.0），但總結印出

```
超標後自行回復（未達連續門檻）: roll 0 次 / 左右差 0 次
```

⇒ 🔴 **持續性判定從頭到尾沒有被觸發過** —— 這趟通過**不是因為這個修改**，
是因為**那個 5° 瞬態根本沒有再發生**。
**「它會不會自己回復」這個問題仍然沒有答案**，跟中止那次一樣。

📌 **這一條值得單獨記：一個修正「跑過了」不等於「被驗證了」。**
分辨方法是看**它負責的那條路徑有沒有被走到** —— 這裡剛好有現成的指標（nearmiss 計數），
如果沒有那兩個數字，這次會被當成「修好了」。
**設計防護時順便設計「它有沒有作用」的觀測點。**

---

## [2026-09-09m5] 鏈路可見度（Option B）＋ 修掉 `imu_push_loop_` 的無逾時 connect ＋ 測試位址可覆蓋

> 範圍：`app/WASH_ROBOT.cpp`／`.h`／`app/wash_robot_commands.cpp`、`Linux_test/{cycle_test,mission_run}.py`。
> per user「開始」（四項一起）。✅ 本體完整建置 **16/16**、`facade_drv.out` md5 `2af1889e…`、
> **已部署**（備份 `facade_cleaning_v2.out.prev-20260909-1145`，逐位元複驗一致）。
> 🔴 **未實機驗證** —— 兩台主程式都還沒啟動。

### ① 為什麼素樸版不能上（決定性細節是我複驗出來的）

AI-2 上一輪建議「把 `imu_push_loop_` 讀到的東西直接餵給 `handle_crane_evt_`」，並自稱最低成本。
它這一輪自己推翻了。**真正的成本不在執行緒安全**（那部分是乾淨的：碰到的東西不是 atomic
就是已由 `crane_alarm_mtx_` 保護且今天就已跨執行緒；`crane_imu_mtx_` 全樹只有一個使用者 ⇒ 鎖序無環）。

成本在這裡：

```
TCP_server::broadcast() → std::lock_guard lock(clients_mtx);
                          for (sock : clients) send(sock, buf, len, SEND_FLAGS);
```

🎯 **AI-2 沒查、我複驗的那一項：`SEND_FLAGS = MSG_NOSIGNAL`，沒有 `MSG_DONTWAIT`**，
且 `TCP_server` 從未把 socket 設非阻塞、也沒有 `SO_SNDTIMEO`（三者 grep 皆無命中）
⇒ **`send()` 會在對端緩衝區滿時真的阻塞。**

今天這個反壓落在 `crane_cmd_`（秒級 timeout，容忍度高）。若接上 push loop，會同時落在
**4Hz 的 roll 路徑**，而它有 `IMU_ROLL_STALE_MS = 750` 的硬懸崖：
**一個慢的瀏覽器 client → push 執行緒卡在 `send()` → 停推 roll → 吊機 750ms 後判定過期
→ 平衡靜默退回 `src=meter`。**

📌 **這是 09-08 那條的反向版本**：那次是隧道讓 roll 過期，這次會變成**我們自己的 GUI 讓它過期**。
而「長時間、大半無人盯著、分頁開著沒人看」正是 10 週期長測的形狀。

### 改法

**Option B —— 把 `handle_crane_evt_` 拆兩半：**
- `record_crane_evt_()`：atomic + 一把短鎖，**不印、不轉播**，任何執行緒可呼叫。
  回傳「是否因 balance cal 而抑制」，讓 full handler 不必重測同一個條件（避免把判斷抄兩份）。
- `handle_crane_evt_()`：記錄 + 印 + 轉播，**只留在 `crane_cmd_` 路徑**。

**`imu_push_loop_` 三項：**
1. 🔴 **拿掉迴圈內的 `connectToServer`** —— 它是無逾時的阻塞 connect，對端不可達時整輪卡 ~127 秒。
   以前只是「推送變慢」，現在這條迴圈同時是鏈路探針 ⇒ **卡住等於在最該報警的情境下瞎掉**。
   改為 init 預熱 + `reconnectLoop` 維持（**與今天 `crane_stop_estop_` 完全相同的處置**）。
2. 🔴 **行緩衝** —— `recv(sock, buf, bufSize - 1, ...)` 配 `char buf[128]` ⇒ 單次最多 **127 bytes**，
   而吊機的 `EVT device_state` 一行遠超過 ⇒ **必定被切**，`find("tension_alarm")` 對半行回 `npos`
   ⇒ **警報被靜默吃掉，正好是這次要補的那個洞的同一種失敗形狀。** 這是必要項不是保險。
3. `crane_peer_last_rx_ms_`（單寫單讀 atomic，免鎖）⇒ `cmd_status` 新增
   `crane_peer_age_ms` / `crane_peer_fresh`。

三條刻意的約束（寫進註解）：迴圈內**不 return、不用會丟例外的解析**（一停 roll 推送就永久停止、
沒人會重啟）／**不印 log**（該函式上方明寫「失敗完全靜默、不可以吵」，而運動期間吊機每秒推
一則 `motion_progress`）／不延長持鎖時間。

🔴 **`crane_peer_*` 目前純觀測，沒有任何自動處置吃它。** `CRANE_PEER_STALE_MS = 3000` 是**暫定值、
未經實測**；待辦表量到隧道閒置時空窗可達 10.6 秒 ⇒ 在隧道上會頻繁翻紅，切到有線之後才有意義。
⚠️ 並在註解寫明：`crane_peer_fresh=0` **不等於**鏈路斷（`crane_attached=off` 或 IMU 讀取失敗
都會讓它不送），要判鏈路必須連同 `crane_attached` 一起看。

### ② 測試位址改為可覆蓋，**預設不變**

`cycle_test.py` 與 `mission_run.py` 的 `WROBOT` 原本寫死 `("192.168.5.26", 5001)`
⇒ 即使把「本體→吊機」切到隧道，**測試指令本身仍走 WiFi**。

改為 `FCV_WROBOT_HOST` / `FCV_WROBOT_PORT` 環境變數，**預設維持 `192.168.5.26`**。
🔴 **刻意不直接寫死 `192.168.1.100`**：隧道的電氣干擾還沒處置，寫死等於讓任何人在修好之前
跑測試就撞牆，**而失敗的樣子會像是機構問題**。
📌 同 09-08 `m1`/`m2` 的教訓：**位址要能被表達，不要靠改常數。**

### 驗證

| 項目 | 結果 |
|---|---|
| `g++ -fsyntax-only` 兩檔 | ✅ |
| 本機 ↔ Pi 三檔 `md5sum` | ✅ 一致 |
| 完整建置 `bld_wr.sh` | ✅ **16/16**，`facade_drv.out` **1,087,064 B**（前版 1,086,952，+112 ⇒ 確實是新的） |
| 部署前確認無程式在跑 | ✅ `pgrep` 無、`:5001` 未監聽 |
| 部署 | ✅ 備份 `…prev-20260909-1145`（1,086,896 B），換上後 md5 `2af1889e…` 兩檔一致 |
| 兩支 py 在**吊機**上 `py_compile` | ✅（md5 兩邊一致） |
| 環境變數兩個方向 | ✅ 不設 → `192.168.5.26`；設 → `192.168.1.100` |
| 實機 | ❌ **未驗證** —— 兩台主程式都未啟動 |

### 🐛 一個會給出假錯誤的坑

**不要拿本機（WSL）的 `python3` 驗這兩支腳本。** 本機是 **3.8.10**，而
`mission_run.py:156` 用了 f-string 內含 `r'...\w+...'` ⇒ 3.8 直接
`SyntaxError: f-string expression part cannot include a backslash`。
**吊機是 Python 3.13.5，兩支都編得過。**
✅ 我是先把 `git show HEAD:` 的原版拿到同一個 python 下編，確認**錯誤在我改之前就存在**，
才沒把它當成自己弄壞的。
📌 **通則：驗證要在會實際執行它的那個環境做**；本機報錯先問「原版在這裡也會錯嗎」。

### 開機時應該看到的新訊息（下次啟動的驗收點）

```
[OK] crane estop bypass channel connected     ← [2026-09-09m3]
[OK] crane IMU push channel connected         ← 本次
```
（吊機沒開時對應為兩行 `[WARN] … not up yet (retrying in background)`，那也是正確行為。）
`cmd_status` 應新增 `crane_peer_age_ms=` 與 `crane_peer_fresh=`。

---

## [2026-09-09m4] `link_probe.sh` 改量「窗口內最長空窗」—— 因為驗收判準是空窗，不是丟包率

> 範圍：`scripts/link_probe.sh` 分析段。兌現 `wired_switch_and_loop_test_plan.md` 的 Phase 0.1。
> 已部署至本體 `~/bringup/link_probe.sh`（md5 `a0c72c69…` 兩邊一致），`preflight` 實跑通過。

### 為什麼丟包率是錯的指標

`IMU_ROLL_STALE_MS = 750` 比的是**空窗**：roll 推送 4Hz（250ms），連掉 3 個就過期。
而**同樣的丟包率可以對應完全不同的空窗**——合成資料實測：

| 情境 | 窗內封包分布 | 丟包率 | **最長空窗** |
|---|---|---|---|
| A | 9 筆平均散開 | **88.5%** | **1.939 秒** |
| B | 9 筆全擠在開頭 | **88.5%** | **15.850 秒** |

⇒ **丟包率一樣，對控制鏈路的意義差 8 倍。** 09-08 只報了丟包率（90%），
所以「隧道到底連續斷了多久」當時其實沒有量到 —— roll age 7,206ms 是間接推得的。

### 改法

分析輸出新增「🎯 HOLD 期間最長空窗」，並直接對上 Gate 1 判準：
`< 750ms` 🟢 門檻不用動／`< 1.5s` 🟡 可用但要調門檻／`>= 1.5s` 🔴 隧道還不能當控制鏈路。
窗內一筆都沒收到時報 `>= 窗口長度`（而不是 0）。

### 🐛 過程中抓到一個會讓結論反向的自己的 bug

邊界修正（窗口起點→第一筆、最後一筆→窗口終點）**兩邊都寫成了 `prev_in`**，
而 `prev_in` 是**最後一筆**。症狀：`he - prev_in` 與 `prev_in - hs` 其中一個恆等於整個窗口長度
⇒ **這個指標永遠報出 17.45 秒，也就是永遠 🔴 不通過。**

🔴 **這個 bug 的方向特別壞：它會把一次成功的電氣修復誤判成失敗。**
而且症狀「數字看起來很合理、只是偏大」，在單一情境下**完全看不出來**——
是拿**兩組丟包率相同、分布不同**的合成資料去比，才因為「兩者輸出一模一樣」而露餡。

📌 **通則：驗證一個「會產生數字」的工具，要餵它兩組『該給出不同答案』的輸入。**
單一情境只能證明它會跑，不能證明它在量你以為的那個東西。
（同族：09-08 的「其餘時間最長空窗 20 秒」統計假象、本專案反覆記的「量到的不是你以為的東西」。）

### 驗證（三個分支都走過）

| 情境 | 預期 | 實際 |
|---|---|---|
| C：0.2s 間隔 + 一個 0.6s 空窗 | 🟢 | **0.600 秒**，🟢 門檻不用動 |
| A：9 筆散開 | 🔴 | **1.939 秒** |
| B：9 筆擠在開頭 | 🔴 | **15.850 秒** |
| 本體實跑 `preflight` | 🟢 | 兩條路徑皆 0% |

---

## [2026-09-09m3] 🔴🔴 兩條急停不再走會被鎖住的主通道 —— 並讓那條旁路真的存在

> 範圍：`app/WASH_ROBOT.cpp`（新 helper + init 預熱 + IMU 45°）、`app/WASH_ROBOT.h`（宣告）、
> `app/wash_robot_commands.cpp`（`cmd_emergency_stop`）。per user「依建議處理」。
> ✅ 本體 Pi 完整建置 **16/16 objs**，`facade_drv.out` md5 `d52792e4…`。
> 🔴 **未部署、未實機驗證**（現役仍是 09-08 的 `facade_cleaning_v2.out`）。

### 起因與根因

`cmd_emergency_stop`（`wash_robot_commands.cpp:3636`）與 **IMU 45° 自動緊急傾斜**
（`WASH_ROBOT.cpp:3027`）都是 `crane_cmd_("stop", 2);` **回傳值直接丟棄**。兩個缺陷疊在一行裡：

1. 🔴 **`crane_cmd_` 第一件事就是 `std::lock_guard lk(crane_mtx_)`（`:733`，無條件阻塞）** ——
   運動類指令持有它直到運動結束（`return_home` 的 `pay_out` 給到 **300 秒**）。
   ⇒ **急停走主通道不是會失敗，是會「等」，那更糟。**
   ⚠️ **IMU 45° 尤其**：傾斜最可能發生的時刻，正好是吊機在動、鎖被持有的時候。
2. 🔴 **回傳值被丟棄** ⇒ 「送到了」與「根本沒送到」長得一模一樣。

### 🔴🔴 過程中查出來的更根本的一件事：那條旁路整條是死碼

程式碼**完全知道** `crane_mtx_` 的問題 —— `crane_cli_estop_` 就是為此而生、6 處註解在解釋。
但複驗發現（三法交叉，正對照 `crane_cmd_` 命中 22/10/17）：

| 事實 | 證據 |
|---|---|
| `crane_cli_estop_` 只被兩個函式用 | `read_rope_weight_estop_` 與 `crane_retract_safe_` 內的 monitor lambda |
| `read_rope_weight_estop_` 唯一呼叫點 | 就在那個 monitor 裡 |
| 🔴 **`crane_retract_safe_` 有 0 個呼叫點** | 全樹只剩定義 + 宣告 + 3 處註解（Python 位元組計數：`.cpp` 2、`.h` 3、其餘 0） |

⇒ **整棵子樹無法到達 ⇒ 那條 socket 在執行期從來沒被建立過。**
所以兩條急停用主通道**不是疏忽，是因為當時只剩主通道**。

📌 **為什麼會這樣**：`crane_retract_safe_` 的「收繩到張力就停」已搬到吊機端
（`g_retract_tension_stop_kg`），**搬得對** —— 保護不再依賴會斷的鏈路。
但**張力監控與旁路通道被綁在同一個函式裡**，前者退場時把後者一起帶走了。
🔴 **通則：一個函式同時承載兩個關注點時，其中一個過時會把另一個一起帶走，而且沒有徵兆**
—— 這裡連編譯警告都沒有，因為是「有定義沒人呼叫」不是「有呼叫沒定義」。
（同型：`crane_retract_to_weight_` 宣告在 `WASH_ROBOT.h:2256`，**全樹無定義、無呼叫**。
C++ 只宣告不定義且沒人呼叫**不會連結錯誤**。）

### 改法（三處）

1. **抽出 `crane_stop_estop_()`** —— 從 `crane_retract_safe_` 那段已驗證過的內嵌寫法抽出來，
   回傳「吊機有沒有回 OK」。`crane_retract_safe_` 內嵌那段改為呼叫它（三份實作 → 一份）。
2. **兩條急停改走它**，失敗時 `cerr` + `evt_("… crane_stop_failed")`
   ——**不 fallback 回主通道**：旁路連不上時主通道多半也連不上，而它還會卡在鎖上。
3. 🎯 **init 預熱那條 socket**（`WASH_ROBOT.cpp` 建 watchdog thread 之前）。

### 🎯 第 3 點是這次最容易做錯的地方

`TCP_client::connectToServer()` 是**沒有逾時的阻塞 `connect()`**（`TCP_client.cpp:99`）。
而旁路 socket 在此之前從未被建立 ⇒ **急停當下它一定是冷的**
⇒ 若在 helper 裡現場 connect，**不可達的吊機會讓急停執行緒停住 ~127 秒（OS SYN timeout）**
——**正好是最需要它的那個情境**。

✅ 正解：init 連一次（`connectToServer` 即使失敗也會 `startMonitor()`，交給
`reconnectLoop` 每 500ms 重試），**helper 裡刻意完全不 connect**，沒連上就立刻回 false。
⇒ **急停路徑最壞 = send 500ms + recv 1000ms，有界。**
📌 用 `set_quiet_reconnect_log(true)`：`crane_cli_` 已經會大聲報吊機離線，
第二條到同一台的 socket 只會讓本來就很吵的 log 加倍（吊機離線 45 秒 ＝ 107 組，08-31 親眼看過）。

### 驗證

| 項目 | 結果 |
|---|---|
| `g++ -fsyntax-only` 兩檔 | ✅ |
| 本機 ↔ Pi 三檔 `md5sum` | ✅ 逐位元一致 |
| 本體完整建置 `bld_wr.sh` | ✅ **16/16 objs**，`facade_drv.out` md5 `d52792e4…` |
| 主通道 `crane_cmd_("stop"` 殘留 | ✅ 0 處（只剩註解命中——本專案記過的那個坑） |
| 部署 | ❌ **未部署**（產物名 `facade_drv.out` ≠ 部署名，現役未動） |
| 實機 | ❌ **未驗證** —— 開機要看到新的 `[OK] crane estop bypass channel connected`；真正的驗收是製造一次急停 |

🔴 **刻意用完整建置而不是增量**：我改了 header，而 `bld_body.sh` 只重編 4 個 TU。
（本例加的是成員函式宣告、不動物件佈局，增量其實安全，但**不值得靠這個判斷過日子**。）

### 未解、且不在本次範圍

- 🔴 `crane_retract_safe_` 與 `crane_retract_to_weight_` 兩個死碼函式**要拍板刪除或復活**。
  本次只是讓其中一段的實作被 helper 取代，**沒有動它們的存廢**。
- 🔴 `TCP_client::connectToServer()` 沒有連線逾時是**全域問題**，不只影響這條路徑。

---

## [2026-09-09m2] 🆕 `scripts/link_probe.sh` —— 把 09-08 的受控實驗做成可重跑的腳本

> 範圍：**新增 `scripts/link_probe.sh` 一支**，其餘未動。純量測工具，不碰控制路徑。
> 目的：讓「吊機端 Fathom-X 改獨立電源」的前後對照能**照同一個方法**量，而不是重新發明一次。

### 為什麼要有它

09-08 那組實驗（同時量隧道與 WiFi、5Hz、420 秒、中間按住 `▲ 拉繩`）是臨場拼出來的，
**沒有留下腳本**。而它現在是**基準線**：HOLD 期間 隧道 9/90（90% 丟包）／WiFi 90/90（0%）。
改電源之後要拿來對照的就是這組數字 —— 📌 **對照實驗的價值在於方法相同，所以方法必須能重跑。**

### 用法

```
./link_probe.sh offset                                  # 先量本機↔吊機時鐘偏移
./link_probe.sh run 420                                 # 5Hz 雙路徑同時量，中途按住 ▲ 拉繩一次
./link_probe.sh analyze <前綴> 19:59:31.972 19:59:49.426 # 窗口取自吊機 log 的 HOLD-TRACE
```

🔴 **必須在本體 `192.168.5.26` 上跑** —— 只有它同時看得到隧道側 `192.168.1.10`
與 WiFi 側 `192.168.5.25`。從 WSL 只到得了 `.5.25`，量不到隧道。

### 🎯 內建一個 09-08 踩過的坑：跨窗口的假空窗

09-08 的分析輸出裡有一句「其餘時間最長空窗 ~20s」，**是統計假象** ——
把跨越 hold 窗口的那個斷點算成了一個空窗，兩條路徑都有、毫無意義。
本腳本**只在窗口同一側的相鄰樣本之間算空窗**，跨窗口那段直接跳過。

另外把「丟包率為負」夾到 0：預期筆數是由窗口長度推算的估計值，收到的可能多一兩筆（端點含入），
**不要讓一個估計誤差看起來像一個發現**。

### 驗證

用合成資料（照 09-08 的形狀：窗口 17.45 秒、隧道窗內 9~10 筆、WiFi 窗內 87 筆）跑過：

| 項目 | 結果 |
|---|---|
| `bash -n` | ✅ |
| 隧道 | HOLD 期間 10/87 → **丟包 88.5%** |
| WiFi | HOLD 期間 88/87 → **丟包 0.0%**（夾 0 生效） |
| 🎯 假空窗排除 | **最長空窗報 0.200 秒**（＝正常間隔）。不排除的話會報 ~17.6 秒 |

🔴 **未在實機跑過**（本體未開機）。第一次實跑要先確認 `ping -D` 與 `-i 0.2` 在該機可用（非 root 的下限就是 0.2）。

---

## [2026-09-09m1] 🔴🔴 MH300 冷啟動無條件解除 base-block —— 補掉一個「只在行程重啟時發生、且完全沒有徵兆」的洞

> 範圍：**`user_lib/MH300_inverter.cpp` Mode B `init()`，一處**。其餘檔案未動。
> 🔴 **未部署、未實機驗證** —— 現役是 SE3（`Crane_control_PI/main.cpp:127` `CRANE_VFD_IS_SE3 1`），
> MH300 這條路徑目前不會被執行；且當下吊機程式未啟動、本體未開機。
> 兌現 `mh300_migration_plan.md` Phase 3-a（2026-09-08 診斷定位，本日實作）。

### 根因（09-08 已定位，本次僅複驗後實作）

`base_blocked_` 是 **process-local 軟體旗標**，而 base-block 本身寫在變頻器裡：

| 情境 | 硬體 `0x2002` b2 | 軟體 `base_blocked_` | 結果 |
|---|---|---|---|
| 急停 → 下一個 run（同一次執行） | set | `true` | ✅ `releaseBaseBlockIfNeeded_()` 先清，正常 |
| **急停 → 程式重啟 → run** | **仍 set** | **`false`**（新物件） | 🔴 早退，**B.B 永遠不清** |

**重啟包含 `exit` 正常收尾／crash／Pi 重開機。**
⚠️ 症狀：**馬達不轉、Modbus 寫入回報成功、無 fault code、無任何錯誤訊息。**

🔴 **原本的 `init()` 補不了** —— 它只在**讀到 error code 才** `clearAlarm()`，
而 **base-block 是輔助命令位元、不是 alarm**，冷啟動時那條 `if` 不會成立。

### 改法

Mode B probe 成功之後、`return false` 之前，**無條件** `writeParam(REG_AUX_CMD, 0x0000)`。
代價是每次 init 多一筆 Modbus 寫入。

兩個實作細節：
- **與既有 `clearAlarm()` 分支不重複寫** —— `clearAlarm()` 結尾本來就會把 `0x2002` 寫回 0
  （`:282`），故以 `aux_cleared` 標記，走過該分支就不再多寫一筆。
- 🎯 **寫入失敗時把 `base_blocked_` 設為 `true`** —— 不是設 `false`。
  設 `false` 會讓「硬體可能還 set 著」被記成「已清」，等於把同一個靜默失效再造一次；
  設 `true` 則讓既有的 best-effort 重試路徑（`releaseBaseBlockIfNeeded_()`）在第一個 run 命令時再清一次。
  📌 **旗標的初值要選「會多做一次事」的那一邊，不要選「會跳過」的那一邊。**
  這正是本條缺陷的成因反過來用。

### 只改 Mode B，不改 Mode A

Mode A（`init(ip, port, id, debug)`）只做 TCP connect、**沒有 probe**，沒有「確認裝置在線」這個時點可掛；
且吊機走的是 Mode B（`main.cpp:5066/5088` `vfd_left.init(cli_A, ...)` 共用閘道）。Mode A 是 bench/獨立用途，本次不動。

### 驗證

| 項目 | 結果 |
|---|---|
| `g++ -fsyntax-only -std=c++17 -Icommon -Imechanism -Itransport -Iuser_lib` | ✅ exit 0 |
| 部署 | ❌ **未部署**（MH300 非現役、機器未啟動） |
| 實機 | ❌ **未驗證** —— 真正的驗收要等換上 MH300：急停 → 重啟程式 → 下 run，馬達應該要轉 |

### 🔎 順帶查證：SE3（現役）沒有同型的洞

依 09-08 立下的通則「凡是用軟體旗標追蹤硬體狀態，都要問行程重啟之後呢」，回頭查了現役的 SE3：

- `cu_mode_set_` 初值 `false`，而 `false` 的語意是「**要去寫** H1000=0」⇒ 重啟後會重做，是**安全方向**。
- SE3 的 **MRS 與 run 位在同一個命令字**（b7），寫 run 命令本身就會把 MRS 清掉，
  所以它根本不需要旗標來記——這也正是為什麼 driver 當初要在 MH300 上「補回 SE3 語意」。

⇒ **這個洞是 MH300 獨有的，不是現役的 live bug。**
📌 值得記的是**同一個判準在兩支 driver 上得到相反結論的理由**：
差別不在寫得好不好，在於 **MH300 把 B.B 放在跟 run 不同的暫存器**，才需要跨呼叫記住狀態。

---

## [2026-09-08m2] 🔴 「強制走 WiFi」是這個機制唯一表達不出來的意圖 —— 覆蓋判準由比對值改為看環境變數

> 範圍：**`common/endpoints.h` + `app/WASH_ROBOT.cpp`，各一處**。前端未動、`scripts/` 未動。
> 已建置部署至本體 `.26` `~/bringup` 並重啟驗證。承接自 `[2026-09-08m1]`。

### 起因

隧道診斷結案後 per user「先切回 WIFI，改完電源再測」。設 `FCV_EP_CRANE_HOST=192.168.5.25` 之後：

```
[endpoints] override CRANE host = 192.168.5.25      ← 環境變數確實讀到了
[crane] 走**有線** 192.168.1.10:5002（探測通過）     ← 但還是走有線
```

**兩行 log 自相矛盾，而各自單獨看都不像錯的。**

### 根因：判準是「解析值 ≠ 編譯期常數」，不是「環境變數在不在」

```cpp
const std::string overridden = ep::host("CRANE", CRANE_IP);
if (overridden != wifi) { ...用覆蓋... }     // ← 覆蓋值 == 常數時，與「沒設」無法區分
```

⇒ 掉進自動探測分支。而 `CRANE_IP` 本身就是 WiFi 位址
⇒ 🔴 **「強制走 WiFi」正好是這個機制唯一表達不出來的意圖。**

📌 **我先前看過這個分支並判斷成「無害的小瑕疵」，然後就被它咬了。**
教訓：**「值相同所以沒差」只在讀的那一側成立；在寫的那一側，它讓一個意圖無法表達。**

### 改法

`common/endpoints.h` 新增 `ep::has_host_override(name)`（直接看 `detail::lookup(name, "_HOST")` 在不在），
`resolve_crane_ip_()` 改用它。**兩個檔、各一處**，其餘語意不動。

### 驗證

| 項目 | 結果 |
|---|---|
| 本機 ↔ Pi `md5sum` | ✅ 逐位元一致 |
| `build_body.sh` | ✅ **16/16 objs**，產物 **1,086,896 bytes** |
| 部署前備份 | `facade_cleaning_v2.out.prev-20260908-2010` |
| 換上後 md5 | ✅ 對上 |
| 🎯 重啟後 log | `[crane] 位址由環境變數覆蓋 = 192.168.5.25（不做有線探測）` → `[OK] crane 192.168.5.25:5002` |
| `ss` | ✅ **兩條連線都在 `192.168.5.25:5002`** |

**回到 WiFi 後的健康度**：`imu_roll_age_ms=68`／`imu_roll_fresh=1`／`balance_source=imu`／
`motion_active=0`／張力 2.84 / 2.74（無吊重）／繼電器全 0／三條匯流排 connected。
📌 對照隧道上的 495ms（閒置）與 7,206ms（運動中）：**WiFi 是 68ms。**

### 🔴 留下一個「必須記得帶」的參數

**現行啟動需要帶 `FCV_EP_CRANE_HOST=192.168.5.25`** —— 不帶的話自動探測會選有線
（`[2026-09-08m1]` 之後探測**會成功**），而有線在 VFD 運轉時掉 90% 封包（見 work_log 同日 #2 結案）。
⚠️ **這是一個「必須記得帶」的參數，也就是遲早會被忘記的那種。**
干擾修好之前，考慮把它寫進啟動腳本，或把編譯期預設暫時改成 WiFi。

#### 本次會期在本體留下的三個 binary

| 檔案 | 內容 |
|---|---|
| `facade_cleaning_v2.out` | **現役**，含兩個修正（單一位址來源 + `has_host_override`） |
| `facade_cleaning_v2.out.prev-20260908-2010` | 只含 `m1`（單一位址來源） |
| `facade_cleaning_v2.out.prev-20260908-1950` | 09-07 原版，兩個修正都沒有 |

---

## [2026-09-08m1] 🔗 連往吊機的位址收斂成單一來源 —— 第 5 處若連到死位址，超重保護會靜默失效

> 範圍：**`app/WASH_ROBOT.cpp` + `app/WASH_ROBOT.h`**（後者只加宣告與理由註解）。
> 前端未動、`scripts/` 未動。已建置部署至本體 `.26` `~/bringup` 並驗證。

### 問題

`resolve_crane_ip_()`（2026-08-31 加入的有線自動探測）的決定**只套用到主連線**；
其餘連線各自直接讀 `ep::host("CRANE", CRANE_IP)` ＝ 編譯期 **WiFi** 常數
⇒ 探測選到有線時，主連線走有線而那些仍走 WiFi，**位址分裂且無任何徵兆**。

🔴 **實際有 5 處，不是原先說的 2 處**（第 5 處是逐字檢查殘留時才撈到的，也是最要緊的一處）：

| 處 | 位置 | 用途 |
|---|---|---|
| 1 | `WASH_ROBOT.cpp:300` | init 的 log 顯示 |
| 2 | `crane_connect_if_needed_()` | 主連線 |
| 3 | `read_rope_weight_estop_()` | 急停旁路**讀張力** |
| 4 | IMU 推送迴圈 | `set_imu_roll` |
| 5 | **`crane_retract_safe_` 的 OVERWEIGHT 分支** | **超重時實際送 `stop`** |

⇒ **第 5 處若連到死位址，超重保護會靜默失效。**

🔴 **WiFi 拆掉之後後果是實質的**：那些連線會去連一個**不存在**的位址，而
`connectToServer` 是無逾時的 blocking connect（2026-08-31 實測卡滿約兩分鐘）——
**而急停旁路存在的意義，正是主連線塞住時還能停機。**
📌 2026-09-08 之所以沒出事，只是因為探測失敗、三條剛好都落在 WiFi（**巧合，不是設計**）。

### 改法

抽出 `crane_endpoint_ip_()` 單一位址來源，**5 處全部改走它**。
📌 **刻意不是「把那幾處各補一份三元運算式」** —— 那會變成同一個運算式抄 5 份，
正是本專案一直在清的「同一件事寫在兩個地方」。宣告處寫明理由與 WiFi 拆除後的後果。
📌 **執行緒安全**：`crane_ip_resolved_` 只在 init 的 `resolve_crane_ip_()` 寫入一次，
IMU 推送執行緒與 estop 路徑都在 init 完成之後才啟動 ⇒ 其後全為唯讀。

### 驗證（照 `scripts/build/README.md` 的三個隱藏步驟）

| 項目 | 結果 |
|---|---|
| 同步後 `md5sum` | ✅ 兩邊逐位元一致 `a02012e3…` |
| `g++ -fsyntax-only` | ✅ 過 |
| `build_body.sh` | ✅ **16/16 objs**，產物 **1,086,792 bytes** |
| 大小對照 | 現役 1,086,728 ⇒ **差 64 bytes，確實是新的東西**（沒重演 09-07「大小完全相同」那個巧合） |
| 部署 | 停程式 → 備份 `facade_cleaning_v2.out.prev-20260908-1950` → 換上，md5 對上 |

🎯 **決定性驗證：不設環境變數、讓自動探測去選**

```
[crane] 走**有線** 192.168.1.10:5002（探測通過）— WiFi 192.168.5.25 未使用
```

→ `ss` 確認**兩條連線都在 `192.168.1.10:5002`**
⇒ **舊程式在這個情境下 roll 推送會跑去 `192.168.5.25`。這就是修好了的證明。**
（estop 是 lazy 連線，要 retract 監看才會建立，但它與 IMU 共用同一個 helper。）

### 🐛 兩個部署過程的坑（`scripts/build/README.md` 可補）

① **埠關掉了不等於程式退出** —— `:5001` 第 4 秒就關了，但隨即 `cp` 得到 `Text file busy`。
　 README 現有的建議是「用 `ss -ltn` 確認埠關掉」，**這次證明那還不夠**，要等到行程真的消失。
② **我自己數錯行程** —— `ps | grep facade` 數到 **2 個殘留**，但那是**我自己的啟動器 `bash -c`**
　（命令列字串裡含有執行檔名），不是程式。`pgrep -af "^\./facade_cleaning_v2\.out"` 嚴格比對回「無」。
　📌 與 README 記的 `pgrep` 陷阱同一族、**方向相反**：那次是漏數，這次是多數。

---

## [2026-09-08g1] 🖥 console v2 繼電器：移除確認彈窗 + 消除 700ms 回饋延遲

> 範圍：**只動 `web_backend/public_v2/index.html`**（8081 那支）。
> 8080 的 `public/` 未動、C++ 未動、`scripts/` 未動。版號 `2026.09.07-2222` → **`2026.09.08-1925`**。
> 📌 GUI 線（`g`）承接自機器線 `agent-ai-8b` 的移交；量測數據沿用其實測，未重測。

### 起因與根因（機器線已排除的假設）

per user 回報「按繼電器有延遲感」，並假設是輪詢一直問狀態在搶 bus。**該假設不成立**（機器線實測）：
本體 `:5001` 的 `status` p50 = **1 ms**、`relay_status` 9~10 ms、`get_settings` 2 ms；
且繼電器在 `.20`（PQW）、JC-100 在 `.22`，**不同閘道不同 TCP 連線，不搶同一條 bus**。

**真因在前端**：`fire()` 結尾寫死

```js
// 一律以回讀為準，不相信回傳字串
setTimeout(function(){ pollRelay(); pollPwm(); }, 700);
```

指令 ~30 ms 就完成，但按鈕要等這 **700 ms** 重讀完才翻 ⇒ 延遲感全部是這行造成的。

### ① 移除兩處 `confirm()`（per user：「按了就執行」）

| 位置 | 原本擋什麼 |
|---|---|
| `#relaygrp` click handler | `data-danger` 的列（**CH1 真空閥 / CH6 正壓閥**）跳「吸盤承重時操作可能讓機器脫落」 |
| 通用 `[data-cmd]` handler | `data-confirm` 的按鈕跳「這會移動機器或中止動作」 |

🔴 **`data-danger` / `data-confirm` 屬性與紅色警示樣式刻意保留**，只拿掉 `confirm()` 這道攔截。
理由：屬性另有用途，且日後若要改成「**只在真的吸附／承重時才擋**」，判斷所需的標記已經在 DOM 上。
📌 **這是 per user 明確要求，不是疏漏** —— 風險本身沒有消失。機器線已提過條件式攔截的替代方案。

⚠️ **副作用（本次發現，移交單未提及）**：通用 handler 是共用的，
移除後**三個非繼電器按鈕也一併失去確認**：

| 按鈕 | 後果 |
|---|---|
| `pwm save` | 🔴 寫 QX-DO24 flash，而該模組**非 `0x00~0x0F` 的暫存器是實時寫 flash 且壽命僅 1~2 千次**（見 `CLAUDE.md` 驅動表）⇒ 誤觸會消耗不可回復的寫入壽命 |
| `zdt_zero` | 重新定義推桿原點（與**仍保留** confirm 的手臂 `INIT` 同一類風險） |
| `rail_cfg_soft_enable` | 滑台設定 |

⚠️ **並且產生一處不一致**：`rail-zero` 就在 `rail_cfg_soft_enable` 隔壁，但它有**自己的**
hardcoded `confirm()`（不走通用 handler）⇒ **它還是會跳窗**。
🔴 **未自行處置，交回使用者決定**（要不要單獨保留 `pwm save` 的確認、或連同 `rail-zero` 一起拿掉）。

### ② 消除 700 ms 回饋延遲：改用後端回覆裡的**回讀值**立刻更新

**關鍵事實：後端回覆本身就已經是回讀結果。**
`cmd_relay` 走 `pqw_set_relay_verified_()`（寫入 → 回讀 → 最多重試 3 次），
回的 `OK ch<N>=<0|1>` 那個數字是**從 PQW 讀回來的實際通道狀態**
（`app/wash_robot_commands.cpp:3914`；`cmd_pump` 3843 / `cmd_brush` 3862 同理）。

⇒ 新增 `applyRelayEcho(line, chKey)`：解析得出就**立刻**更新該列的開關與回讀標示。
📌 **這不是樂觀更新** —— 用的是後端回讀到的真值，沒有違背原註解「不相信回傳字串」的原意。
700 ms 的 `pollRelay()/pollPwm()` **保留為二次確認**，但不再是唯一更新來源。

🔴 **只認 `chKey` 指名的那一個通道，這是刻意的窄化**（本次實作時發現的坑）：
`pwm status` 回的是 `ch1=5,50,65535,1`，用 `paintRelay()` 那個寬鬆的 `/ch(\d+)=([01])\b/`
**會誤命中** —— duty 為 `0` 或 `1` 時 `\b` 在逗號前成立 ⇒ **會跑去翻繼電器 CH1 的開關**。
繼電器列一定帶 `chKey`、PWM 與吊機那些呼叫傳 `null`，所以用 `chKey` 當閘門最乾淨。

⚠️ **`readback_fail` 系列必須當成「未確認」**：回讀整條掛掉時後端回
`OK pump_set_but_readback_fail`（3841）／`OK water_pump_set_but_readback_fail`（3928）——
**開頭仍是 `OK` 但沒有 ch 值**。解析不到就維持「等待回讀…」讓 700 ms 那輪去收，**不可以當成成功**。

**執行順序也有講究**：`delete pending[chKey]` 必須在 `applyRelayEcho()` 之前，
否則 `paintRelay()`／echo 會以為還在等回讀而跳過該列（`paintRelay` 有 `if (pending['ch'+ch]) return;`）。
echo 成功時也不再讓通用的 `setVerif(verifKey, 'pend', '已送出 · 待回讀')` 把「已確認」蓋回去。

### 驗證

| 項目 | 結果 |
|---|---|
| `node web_backend/tools/check_console.js` | ✅ 四項全過（重複 id／寫到不存在的元素／大括號／`<div>` 開合），exit 0 |
| 部署前備份 | `index.html.bak-20260908-1925`（Pi 上，另有 09-04／09-07 兩份） |
| 本機 ↔ Pi | ✅ md5 逐位元一致 `f1bd901f…` |
| HTTP 實取 `:8081` | ✅ 200 / 155,003 bytes；版號 `2026.09.08-1925`；`applyRelayEcho` 命中 3 處 |
| `:8080` 未受影響 | ✅ 200 / 47,577 bytes（不同檔） |

⚠️ **express 每次請求重讀檔，未重啟 node**（兩個 web 行程 PID 1744/1745 未動）。
⚠️ **本次全程未送任何會讓機器動作的指令**（使用者正在現場做單項操作）。
🔴 **尚未經真人瀏覽器實測** —— 需使用者重新整理 `:8081` 後按一次繼電器確認手感。

---

## [2026-09-07m6] 📚 原廠資料整理進 `doc/`；補上四份缺的硬體摘要（8→14 份）

> 範圍：新增 `.claude/summaries/` 四份、`CLAUDE.md` 索引更新；`doc/` 與 `tmp/` 重整（皆 gitignore）。
> 📌 編號沿用機器線 `m`（硬體／driver 領域），機器線當日已無其他工作。

### 起因
per user 把 12 個原廠資料夾（**1.5 GB / 2832 檔**）放進 `tmp/`。盤點後發現它正好補上
**四個「有 driver 卻完全沒有手冊摘要」的缺口** —— 而 `CLAUDE.md` 明寫
「不要憑 driver 現有程式碼反推協定，那正是 DM2J 那次踩雷的方式」。

### 整理
照既有規則（原始 PDF 放 `doc/`，因為 `tmp/` 不在雲端鏡像範圍內）：

| | 之前 | 之後 |
|---|---|---|
| `doc/` | 3 個裝置、6.1 MB | **14 個裝置、27 檔、41 MB**（全是手冊） |
| `tmp/` | 1.5 GB 混雜 | 1.5 GB，只剩安裝程式／zip／一支 110 MB 展示影片 |

🐛 **第一版搬過頭**：把 `.txt`/`.png` 也當文件，撈進 **419 png + 319 txt**
（WitMotion 的輸出紀錄、DM2J 的介面圖）。已全數移回 `tmp/<原資料夾>/_由doc移回/`，
**沒有刪任何東西**，而且那些檔本來就還在未解開的 zip 裡。
🔧 順手轉了 `計米器/指令.txt`（**Big5，原本整份亂碼**）→ `指令_utf8.txt`，內容是 SD76 的實際 Modbus 幀。

### 四份新摘要

- **`DSZL_107_X518_MODBUS_SUMMARY.md`**
  🔴 **driver 名稱是誤導的**：DSZL-107 是**類比應變片**（2.0 mV/V、350 Ω），**沒有任何數位介面**；
  講 Modbus 的是 **X518 雙通道採集器**。`DSZL_107.cpp` 從頭到尾講的都是 X518 的協議。
  🔴 X518 **只支援 FC 03 / FC 10H**，所有讀寫都是 32 位、每參數佔 2 個位址。
  🔴 **上電自動清零預設開啟**（清零模式 `0x606` 預設 11）；零位跟蹤預設 0 ＝實質關閉，
  但**一旦有人把 `0x604` 改成非 0，緩慢變化的張力會被持續吃掉且毫無徵兆**。
  📌 **手冊有一條沒被用到的路**：裝置端實物標定（`0x0A1E` 寫標準值 → `0x0A20` 寫 11/12），
  它剛好能回答 work_log 那條「單點校正、4.16 kg 外推到 30~60 kg 線性度未驗」。
  ⚠️ 手冊自己警告：數字校準用的靈敏度若是**理論值**則結果不可信 —— DSZL-107 的 2.0 mV/V 正是理論值
  ⇒ 要用就走**實物標定**，不要走數字校準。

- **`XKC_Y25_MODBUS_SUMMARY.md`**
  🔴 **手冊自己前後矛盾**：設定位址／波特率的**寫入位址比暫存器表大 1**
  （§1.6 寫 `0x0004`、§1.8 寫 `0x0005`，而 §2.1 表說是 `0x0003`/`0x0004`）。
  ✅ **`XKC_Y25_RS485.h` 早就處理對了**，註解還寫明「NOT 0x0003 / NOT 0x0004」——
  本摘要是**確認**它對，不是修正它。
  🔴 出廠 **9600**，而本專案 `.22` 匯流排是 115200 ⇒ 這顆被改過波特率碼為 `0x0D`。
  **任何一次恢復出廠都會讓它從匯流排上安靜消失**（其他裝置照常，只有它不回應）。
  📌 RSSI 遲滯（<3900→0、>4100→1、中間保持）**在感測器內部**，上位機不要再做去彈跳。
  📌 **靈敏度是後蓋內的實體旋鈕**，通訊介面裡沒有這個參數 —— 偵測不準要去現場轉，不是改程式。

- **`DY_500_MODBUS_SUMMARY.md`**（這顆**從未安裝**，2026-09-03 已解除匯流排繫結）
  出廠 **19200**、FC 03/10（**v1.4 起移除 FC 05 支援**）、LONG 位址 +100 為 FLOAT。
  🔴 上電清零預設開（`清零設定` 預設 1101）。
  🔴🔴 **主動上傳模式是單向門**：設成 2 之後「需恢復出廠設置才能切換回 modbusRTU」。

- **`WT901BC_TTL_PROTOCOL_SUMMARY.md`** — 詳見下節

### 🔴 WT901BC：手冊來源與逐項比對（per user 指示「比對現有 driver 確認」）

**取得過程**：per user 給的 yuque 連結**抓不到正文**（JS 渲染），且該頁是 **WT901C-`485`**（介面型號不對）；
WitMotion 軟體包內 17 份 `Config/Command/*.txt` **全部 0 bytes**（執行期佔位檔）。
⇒ 改用 WitMotion 官方 **WT901C-TTL** 的 datasheet + manual，已存進 `doc/姿態儀WT901BC/`。

**✅ 與 driver 七項完全相符**：封包頭 `0x55`／長度 11／checksum = 前 10 byte 和／
小端序 signed short／`0x51` `/32768*16`／`0x52` `/32768*2000`／`0x53` `/32768*180`。
🎯 **實機佐證**：`n_accel=42667`、`n_angle=42894` 同步累加；**`az=1.00 g`（重力全在 Z 軸）
⇒ 水平安裝，與手冊 `DIRECTION=0` 的座標定義相符**。
⇒ **WT901BC 與 WT901C 在 TTL 上是同一套協定**，摘要可用。
⚠️ 但**規格數字（量程/精度）仍屬 WT901C**，引用具體精度前要以實物銘牌為準。

**🔴 比對抓出五個落差**（不是 bug，但值得知道）：
1. **`0x54` 磁場封包完全沒解析**，而 RSW 出廠預設**會送它** ⇒ 每次通過 checksum 後默默落地
2. **溫度從來沒讀過**（`0x51`/`0x52`/`0x54` 的 `buf[8..9]`，`T=v/100℃`）—— IMU 有溫漂，這是免費診斷訊號
3. 🔴🔴 **`0x56` 氣壓整條路徑是死碼**：手冊**沒有 `0x56` 的格式定義**、RSW 預設**不輸出它**、
   應用層**沒有任何一處讀 `pressure`/`altitude`。而 `.h` 的使用範例還印 `imu.altitude`
   ⇒ **照著抄會得到一個恆為 0 的值**。這是「有 markup 沒人寫」的 C++ 版本。
4. ✅ **`0x53` 末兩 byte 是版本號不是溫度**，driver 正確地沒碰 ——
   另外三個封包同位置是溫度，**很容易寫成一體適用**。
5. 🟡 同步只靠 checksum、**沒驗封包類型**（`0x55` 可能出現在資料位元組裡，約 1/256 漏網）。
   加一行「`buf[1]` 落在 `0x50`~`0x5A`」幾乎免費。

### 🔴 `CLAUDE.md` 索引自己漏了一份
`MH300_INVERTER_MODBUS_SUMMARY.md` **自建立以來從未進過那張索引表** ——
而那張表建立的動機正是「**一份沒被指到的文件等於不存在**」。已補。
索引現為 **14 列 / 實際 14 份，逐份對上**（原文寫 8 份與 9 份，兩處都不對）。

---

## [2026-09-07m5] ⚰️ Visual Studio 整組移除；補上被它獨佔的建置定義

> per user「不用 VS 了，整組刪掉」。刪除 `facade_cleaning_v2.sln`、4 個 `.vcxproj`、
> 4 個 `.vcxproj.user`、`.vs/`（43 MB 快取）。取回見刪除前的 commit `79a2312`。

### 🔴 刪之前先補上被它獨佔的建置定義
差一步就製造出「移除一份定義卻沒有替代品」—— 跟今天清掉的病同型。
- **`Linux_test` 的 16 個 TU 清單只存在於它的 `.vcxproj`**（唯一副本）
  → 逐項搬成 `scripts/build/build_linux_test.sh`，並**在 Pi 上實建通過**（`linux_test.out`, 671,520 bytes）。
  ⚠️ include 比 vcxproj 原宣告的多帶 `..\config` 與 `..\mechanism`（那兩個本來就漏）。
- **`cleaning_arm` 刻意不搬** —— `cleaning_arm/compile.sh` 本來就在 repo 且內容正確
  （一直帶著 `-I../user_lib`）。搬進 `scripts/build/` 只會製造第二份副本。
  ✅ 改寫它的抬頭：原寫「Fallback／VS MSBuild 走 vcxproj 是主路徑」，現在它才是主路徑。

### 🐛 實建才抓到的誤解：那行「一直都能跑」的指令，不是靠它自己跑起來的
第一版 `build_arm.sh` 照抄 Pi 上 `~/projects/compile.sh` 的裸指令（**無任何 `-I`**）→
`fatal error: damiao.h: No such file or directory`。
那行在 Pi 上能跑，是因為 `~/projects/cleaning_arm/` 底下有 **VS 複製過去的 `user_lib/` 扁平副本**。
⇒ 「它一直都是純 g++」這句話為真，但**那條指令依賴 VS 佈置出來的目錄**。
📌 **一段「一直都能跑」的指令，未必是靠它自己跑起來的。**
（同族：`load` 觸發 ≠ 畫完／讀到一行 ≠ 我的回覆／回 `OK` ≠ 真的生效。）
✅ 該檔已刪除（改指 `cleaning_arm/compile.sh`），但驗證過程留在這裡。

### 🔴 更正我自己一個誇大的說法
先前說「`.vcxproj` 的**檔案清單也不完整**」（並寫進 `CLAUDE.md` 與 `m4` 的 commit 說明）——
**那句是錯的**。逐項比對：`ClCompile` **兩個主專案都完全正確**（本體 16=16、吊機 10=10）。
真正錯的只有兩處：`AdditionalIncludeDirectories` 漏 `..\mechanism`／`..\config`，
以及 `ClInclude` 沒列 `rope_axis.h` ⇒ **VS 不會把它複製到遠端**，那才是遠端樹建不起來的直接原因。
⇒ **`.vcxproj` 離正確只差三行，不是壞得該丟；刪它的理由是「它是第二份建置定義」。**
已同步更正 `CLAUDE.md`、`scripts/build/README.md`。

### 🔴 另一個「以為結案、其實沒有」
待辦表「4 個 `.vcxproj.user` 被 git 追蹤」在 2026-07 標成已處理（`.gitignore` 加了 `*.vcxproj.user`），
**但四個檔一直還在版控裡** —— **gitignore 對已追蹤的檔案無效**。
📌 **「規則加了」≠「規則生效了」**：加 ignore 規則時要一併 `git rm --cached`。

### 文件更新（`changelog.md` 為 append-only 歷史帳本，不動）
`CLAUDE.md`（Build System 整段重寫成 g++ 表格／根目錄盤點兩列／include 路徑表改指腳本）、
`runbook.md`（`~/projects/` 標為已廢棄、TU 權威來源改指腳本、MSVC `cl /Zs` 整節標作廢）、
`ONBOARDING.md` 與 `MH300_INVERTER_MODBUS_SUMMARY.md`（🔴 兩處說 MH300「未在 vcxproj 生效」——
實際上 `build_crane.sh` **有編它**，沒生效的是 `main.cpp` 的 `CRANE_VFD_IS_SE3 1`）、
`mh300_migration_plan.md`（步驟改指 `scripts/build/build_crane.sh`）。

---

## [2026-09-07g6] 🔗 Setting 表四處耦合到期改完；並抓到同表另一個方向相反的謊

> 範圍：`web_backend/public_v2/index.html`。已部署（`2026.09.07-2222`）。

### Fixed（四處耦合，隨 `m4` 部署同日到期）
- ① `balance_source` 的「編譯預設」`meter` → **`imu`**，並拿掉 `color:var(--bad)` 紅字
  （紅字原本的語意是「預設是錯的那個」）。
- 🔴 ② **「重啟後」的 `會遺失` pill 刻意維持不動**（由 ai-2 當場更正我的第一版）。
  機器線改的只是**編譯預設落在哪**，`balance_source` **仍然完全沒有持久化**——
  執行期設成 `meter` 之後重啟一樣靜默變回 `imu`。
  **值照樣遺失，只是後果從「掉到錯的那一個」變成「掉到對的那一個」。**
  ⇒ 改寫「依據」欄說明後果反轉即可。**寫「不會遺失」會讓這張表在相反方向說謊，比不改更糟。**
- ③ Dashboard 的來源註記邏輯反過來：現在 **`meter` 才是「被手動設過」**，`imu` 可能只是重啟後的預設。
  🔴 兩邊都標——看到 `imu` 以為「有人確認過」、看到 `meter` 以為「本來就這樣」，兩種都會誤導。
- ④ Mission 起跑檢查的 `bal:` 說明：原文「每次重啟都會靜默回到編譯預設」**字面仍為真**
  （仍不持久化），只是回到的已是對的值 ⇒ **調語氣不是修錯誤**，並補一句「不要靠重啟修，直接設回來」。

### 🔴 順帶抓到：同一張表的 `motion_hz` 列，方向是反的
- 「編譯預設」寫 **30**，實際是 **50**（`Crane_control_PI/main.cpp:277`
  `VFD_MOTION_HZ_DEFAULT = 50.0`）。**當晚 `m4` 重啟實測：未經任何設定 → `motion_hz=50`。**
- 更糟的是「依據」欄寫「中止時若留在 50，下一個人下 `pay_out` 就是 50 Hz」——
  **把 50 講成殘留的危險值，而 50 正是重啟後你會拿到的東西。**
- ✅ 已改為 `50`（標紅）＋「重啟後就是 50 Hz（不是殘留，是預設）· 現場慣用 30 · 每次重啟都要重設」。
- 📌 **這張表的存在意義就是回答「重啟後會變成什麼」——答錯比不答更糟，它會讓人放心。**
  同表其餘各列已逐一對照吊機常數複核：`LENGTH_DIFF_MAX_CM_DEFAULT=10` ✅／
  `RETRACT_TENSION_STOP_KG_DEFAULT=75` ✅／`UP_STOP_TOTAL_KG_DEFAULT=130` ✅／
  `fine_adjust_level_diff_cm` 預設 0 ✅（今晚重啟實測也是 0）。

### Verified
- 兩個 inline `<script>` `node --check`；`tools/check_console.js` 四項全過
- 部署後 md5 兩邊逐位元一致；`curl` 確認服務出去的內容已換（舊的紅字 `meter` 計數歸零）
- **截圖複驗 Setting 分頁**：`balance_source` 列現為 `imu / imu / 會遺失`，依據欄說明後果反轉；
  同畫面另外確認了本日其他成果：頁首 `🖥 電腦` 切換鈕、輪詢說明 `status 1s · 運動中 250ms`

---

## [2026-09-07m4] 🔴🔴 VS 早就不是建置路徑了；四個「待建置」改動當天建完、部署、實機驗證

> 範圍：新增 `scripts/build/{build_body.sh,build_body_incremental.sh,build_crane.sh,README.md}`；
> 更正 `CLAUDE.md`（Project Overview + Build System）與 `.claude/runbook.md`（§A、MSVC 節）。
> **兩支 binary 已重建並部署。**

### 🔴 起因：一個錯誤前提害我把四件事排進「等 VS 窗口」
per user 提出「你現在都直接用 g++，VS 用不到」之後才去查。查出來的比預期嚴重：

| 實查 | 意義 |
|---|---|
| VS 遠端樹停在 **08-25~08-27**（分層重構之前） | 之後沒被用過 |
| 樹裡只有 `Crane_control_PI/main.cpp` + `user_lib/` | 缺 `app/ command/ common/ transport/ mechanism/ config/` |
| **`rope_axis.h` 整棵遠端樹找不到**，而 `main.cpp` include 它 | ⇒ **那棵樹現在建不起來** |
| `.vcxproj` 的 include 路徑**漏 `..\mechanism`／`..\config`**、`ClInclude` 沒列 `rope_axis.h` | **建置定義本身就是壞的** |
| Pi 上早有 `bld_wr.sh` / `bld_body.sh` / `bld_cr.sh`，純 `g++ -std=c++17 -O2 -lpthread` | **真正的建置路徑** |

📌 **我先前對 per user 與 ai-2 都說「C++ 只能走 VS」，那是錯的**，並因此把 `m1`/`m2`/`m3`
排成「待部署」。⇒ **這是「文件會落後於機器，而且是靜默的」在同一天的第二個實例**
（第一個是 `cmd_pump` 那列過期 5 天的待辦）。三份文件（`CLAUDE.md`／`runbook.md`／`.vcxproj`）
**同時**在說一件不成立的事，而沒有任何訊號。

### Added
- **`scripts/build/`**：三支腳本**逐位元自 Pi 取回**（md5 兩邊相同），先前**只存在於兩台 Pi 的
  `~/bringup/`、不在版控** —— 與同日的檢查器同病，但嚴重得多：**它是唯一能用的建置定義。**
- **`scripts/build/README.md`**：記下三個先前**沒有任何文件記載**的隱藏步驟——
  ① 同步（腳本建的是 Pi 上那份原始碼，不是 repo）② 改名（產物 `*_drv.out` ≠ 部署名）
  ③ 停程式才換得動（`Text file busy` 是保護不是障礙）。

### 🐛 新踩坑：`pgrep` 的 15 字元 comm 上限
`pgrep facade_cleaning_v2` **永遠回零筆**（名字 18 字元 > comm 15 字元上限）。
它**有印警告**，但只看「有沒有輸出」就會把**還在跑**判成**已停止**——本次就這樣誤判過一次。
✅ 用 `ps -eo pid,etime,cmd | grep …` 或 `pgrep -f`，**並同時確認埠已關**。
📌 又一個「找不到 ≠ 沒有」。

### Verified —— 全部實機，不是語法層
- **建置**：本體 16/16 TU、**18 秒**；吊機**62 秒**。
- 🔴 **本體新舊 binary 大小完全相同（1086728 bytes，純屬巧合）** ⇒ 只比大小會以為沒建到。
  ✅ 用 `strings` 對照：`step_in_progress=` / `brush_set_but_readback_fail` /
  `water_pump_set_but_readback_fail` / `readback unavailable` **新版各 1、現役各 0**。
  吊機同理：`[INFO] balance_source=` 與 `[WARN] balance_source=imu` 新版有、現役無。
- **同步可證**：三個 `.cpp` 送上去後 md5 與 repo 逐位元相同才建。
- 🎯 **`m1` 端到端**：吊機重啟後**未經任何設定** → `balance_source=imu`（舊版此處是 `meter`）。
  開機宣告如實：`[WARN] balance_source=imu 但 roll 資料從未收到 → …實際走計米器路徑`，
  本體起來後 `imu_roll_fresh=1 age=162ms` ⇒ **它說「資料一來就會切過去」，確實切過去了**。
  ✅ 同時 `motion_hz` 回 **50**、`fine_adjust_level_diff_cm` 回 **0**
  ⇒ **反向證明重啟是真的發生了**，且只有 `balance_source` 現在會落在對的值。
- 🎯 **`m2` 端到端（真開關）**：`brush on` → **`OK ch5=1`**、`brush off` → **`OK ch5=0`**
  （舊版一律回光禿禿的 `OK`）。`water_pump off` → `OK ch14=0`。
  收尾 16 個通道全 0，與動手前基線逐欄相同。
- 🎯 **`m3`**：本體 `status` 出現 `step_in_progress=0`（舊版無此欄位）。
- **init 段與當日首次啟動逐字比對**：本體只差 IMU roll 一行（0.8899→0.8844）⇒ 無新故障。
  三條匯流排 `.20`/`.21`/`.22` 全 connected、crane 連上、GUI 兩個橋接自動重連。
- ✅ 重啟後已補設 `motion_hz=30`、`fine_adjust_level_diff=5`（**這兩個仍不持久化**）。

### 未做（已知缺口）
- 🟡 **沒有同步腳本**（repo → Pi 仍是手動 `scp`）⇒ 「建出來的是這個 commit」目前只能靠人工複驗。
- 🟡 `scripts/build/` 與 Pi 上的 `~/bringup/*.sh` **是兩份副本**（今日取回時相同）。權威是 repo。
- ⚪ `.sln` / `.vcxproj` **保留給編輯與 IntelliSense，不再是建置權威**；已在 `CLAUDE.md` 標明
  **不要照它推斷檔案清單或 include 路徑**。連帶：「4 個 `.vcxproj.user` 被 git 追蹤」那條待辦失去意義。

---

## [2026-09-07m3] 🔴 本體 `status` 新增 `step_in_progress` 欄位

> 範圍：`app/wash_robot_commands.cpp`（`cmd_status`）。**需 VS 重建才會生效。**

### Added
- `status` 輸出新增 `step_in_progress=0|1`。
  🔴 **為什麼**：`cmd_attach` 是**成功之後**才 `set_state_(Attached)` ⇒ 整段 10~30 秒的
  attach 流程（含 ③→④ 那 2 秒密封窗口）`state` 停在 `ready`，**外界完全看不出 attach 正在進行**。
  09-07 實機確認 status 22 個欄位裡沒有任何一個能指出這件事。
  旗標本身早就存在（`StepInProgressGuard`，attach / step_down / step_up 都會設），
  **只是從來沒有被送出去過**。
- ⚠️ **它涵蓋不到 `cycle_test`** —— 那支不走 `step_*`，全程 `idle`。
  **不要因為多了這個欄位就以為「腳本鎖」那個結構缺口解決了。**

### Verified
- 送 Pi `g++ -fsyntax-only -std=c++17`（14.2.0）通過，**負控制命中 1**（插入未宣告符號被抓）
- 🔴 **行為未驗**，需 VS 重建。部署後 `status` 應多一個 `step_in_progress=0`。

---

## [2026-09-07g5] ⚡ console v2 動態輪詢：閒置 1s、運動中 250ms（附防堆積）

> 範圍：`web_backend/public_v2/index.html`。純前端，已部署（`2026.09.07-2148`）。

### Added
- `pollFast` 由 `setInterval(1000)` 改為**自我排程的 `setTimeout`**，每輪重新決定間隔。
  用 `setTimeout` 不用切換 `setInterval`，是為了避免「舊 interval 沒清乾淨、兩條同時在跑」。
- 🔴 **防堆積（本次最重要的一項）**：佇列是「單一在途、其餘排隊」且**排隊長度無上限**。
  提速到 250ms 後，只要一次往返超過 250ms 就會疊上去，而且**堆起來完全沒有徵兆**——
  只會看到畫面越來越舊。✅ 每輪先問「這個 target 已經有 status 在路上了嗎」，有就跳過
  ⇒ **實際取樣率自動收斂到往返時間，不會比後端跟得上的還快。**
- 頂欄說明同步顯示 `status 1s · 運動中 250ms`（由同一張 `POLLS` 表產生，速率改了說明一定跟著改）。

### Notes
- ✅ **動手前先查證：不會增加 `.22` 匯流排負載**（這是最大的疑慮，那條匯流排最脆弱）。
  本體 `cmd_status` 的 JC100 現場讀取**後端自己限速在 ≤1Hz**，其註解寫的理由正是
  「GUI 2Hz 輪詢 × 每次打 `cli_22_` 九次 → bus saturation」；且**運動中直接回快取**
  （由運動路徑 piggyback 更新）、attach 期間更完全抑制現場讀。
  ⇒ 提速只增加 WebSocket 與 TCP 往返，**不碰 Modbus**。
- 🔴 **只解掉了一半，而且另一半是結構性的**：
  ✅ **解掉**「16 秒回程只有 16 個取樣點」——`motion_active=1` 時 250ms ⇒ 64 點。
  ❌ **沒解掉**「③→④ 那 2 秒密封窗口保證會漏」——**因為沒有任何訊號指出 attach 正在進行**
  （見 `m3`）。程式已先讀 `wr.step_in_progress`，C++ 補上欄位的那天**前端不必再改就會生效**；
  **在那之前密封窗口仍然是 1Hz，不要以為修好了。**
- 📌 GUI 既有註解早就記過相關限制：「`motion_active` **只在吊機實際轉動時為 1**，
  而 `cycle_test` 每步有約 17 秒在做推桿／真空／滑台，那段是 0」⇒ 光看它會正好漏掉密封窗口。
- 🟡 `paused` / `paused_on_error` / `waiting_confirm` **刻意不提速**：那是在等人，白費往返。

### Verified
- 兩個 inline `<script>` `node --check`；`tools/check_console.js` 四項全過
- 🎯 **決策函式逐案測試 8/8**（把 `pollFastMs` 逐字抽出來餵合成狀態）：
  閒置 1000／吊機在轉 250／running 250／balancing 250／paused 1000／waiting_confirm 1000／
  **attach 中（後端現況）1000**／**attach 中（補欄位後）250**
- ⚠️ **端到端速率未實測**：`status` 是 FAST 路徑，**兩個後端的 log 都不記它** ⇒ 沒有可數的東西。
  不要把上面的 8/8 當成「實機上真的以 250ms 在跑」。

---

## [2026-09-07g4] 🛠 前端靜默失敗檢查器進版控（`web_backend/tools/check_console.js`）

> 範圍：新增 `web_backend/tools/check_console.js`、`CLAUDE.md` 登記一列。不影響執行期。

### Added
- **檢查四類「不會有任何徵兆」的前端錯**，離開碼 0/1 可直接串在部署前：
  ① 重複 id（`getElementById` 只拿得到第一個，第二個永遠不更新）
  ② 寫到不存在的元素（`txt()`/`bar()` 找不到就 return，**沒有例外、沒有 console 訊息**）
  ③ 大括號平衡　④ `<div>` 開合
  另有一條 ℹ️：宣告了但全組只出現一次（純 markup，不當成失敗但值得看一眼）。
- 🔴 **這支自己有前科，所以才進版控**：2026-09-04 寫過功能相同的一支、跑完就丟、沒進 repo，
  09-07 要用時只能整支重寫。📌 **一個沒進版控的檢查工具，等於下次還要再寫一次。**

### Notes
- 🔴 **按「組」檢查不是按檔**。console v2 是單檔（markup + JS 同在 `index.html`），
  但 8080 是 `index.html` + `app.js` 兩個檔 —— 逐檔檢查的話 ② **根本檢不到**
  （app.js 寫到 index.html 沒有的 id 那一類），而那正是 8080 的真實風險；
  ③ 也會把 151 個 id 全報成「沒人寫」。
- 🔴 **工具自己會印出它的盲區**：只比對**字面字串**，樣板字串動態組的 id
  （`` getElementById(`vac-${n}`) ``）看不到。後果不對稱：
  **② 會漏報（假陰性，危險）**、③ 的 ℹ️ 會誤報。
  首次跑就踩到 —— 8080 的 `vac-5..8` 被列成「沒人寫」，實際是 `vac-${id}` 寫的。
  ✅ 已抓「動態前綴」來消掉 ③ 的誤報，並**如實印出「② 涵蓋率非 100%」**。
  📌 **一個會靜默漏掉某一類的檢查工具，比沒有工具更危險——它會讓人以為查過了。**
  這與同日的 `--timeout` 不等、「讀到一行就當回覆」是同一族：
  **工具給了看起來完整的答案，而完整性是假設出來的。**

### Verified
- 兩組現行檔案全數通過（console v2：id 107／指名 99；8080：id 151／指名 113）
- 誤報已消（`vac-5..8` 不再列入），盲區警告正確出現（`` `vac-${…}` ``）
- 剩下唯一一筆 ℹ️ 是 console v2 的 `tenset` —— 查過是純版面容器，**不是缺陷**

---

## [2026-09-07m2] 🔴 `cmd_brush` / `cmd_water_pump` 補回讀；回讀整條掛掉的那條路徑補上證據

> 範圍：`app/wash_robot_commands.cpp`、`app/WASH_ROBOT.cpp`。**需 VS 重建才會生效。**

### Fixed
- 🔴 **`pqw_set_relay_verified_` 的「讀不回來」分支一行 log 都不印。**
  對照組刺眼：同一支函式裡「重試三次放棄」那條**有**印，而**最該留下證據的降級路徑反而無聲**。
  它 `return false`（＝成功）讓上層繼續走，唯一徵兆是 `cmd_pump` 回覆裡少了 `ch2=` 欄位——
  🔎 **而實查三個呼叫端沒有一個看那個字串**：console v2 明寫「一律看回讀不看回傳字串」、
  `cycle_test.py` 自己解析 `relay_status` 的 `ch<N>=1`、8080 GUI 根本不解析 pump 回覆。
  ⇒ **降級狀態等於零證據。** ✅ 補上一行，語意（best-effort、權威留給下游）**刻意不變**。
  📌 **補在 helper 不是補在 `cmd_pump`** —— 這樣 valve 等其他呼叫端一起受惠。
- 🔴 **`cmd_brush`(CH5) / `cmd_water_pump`(CH14) 仍是裸 `controlRelay` + 無條件 `OK`。**
  `cmd_pump`（09-03）與 `cmd_relay` 都補過回讀，這兩支被落下。改為
  `pqw_set_relay_verified_` + 回讀並回報實際通道值，與 `cmd_pump` 同形。
  🔴 **`cmd_brush` 比 `cmd_pump` 更需要**：滾筒刷**沒有任何下游感測可以推翻它**
  （幫浦至少還有 vacuum_check 當權威）⇒ 這裡回一個假的 OK 就是最終答案。
  而 `CH_BRUSH` 正是 2026-07/08 被誤改成 15、**打到空通道、滾筒整段不轉而帳面全綠**那個通道。

### Verified
- **語法層**：repo 現行原始碼打包送本體 Pi（g++ 14.2.0）`-fsyntax-only -std=c++17`，
  `wash_robot_commands.cpp` 與 `WASH_ROBOT.cpp` 皆通過。
  ✅ **有做負控制**：插入未宣告符號會被抓到（`error: ... was not declared`），還原後複檢仍 OK。
  ⚠️ 刻意**不用** Pi 上 `~/projects/` 那棵 VS 樹——它沒有 `common/`／`transport/`／`mechanism/`，
  拿它檢等於檢別的東西。
- **實機（安全做法，機器維修中）**：只送 `off` 到**已經是 off** 的通道 ⇒ 實體零動作。
  ```
  基線     ch5=0 ch14=0
  brush off       -> 'OK'          ← 光禿禿，一個通道欄位都沒有
  water_pump off  -> 'OK'
  複驗     ch5=0 ch14=0            ← 前後逐欄相同
  ```
  ⇒ **現行回覆完全不帶證據**這件事已在實機上確認（對照 `cmd_pump` 回 `OK ch2=<值>`）。
  🔴 **刻意沒有把 CH5／CH14 打開**：那會轉滾筒刷馬達、噴水。per user 放行的是「繼電器可動」，
  但這兩個通道的下游是實體動作，維修中沒有必要為了驗證去動它。

### 待部署
- 需 Windows VS remote build（Debug|ARM64）。部署後才可做真驗證：
  `brush on` 應回 `OK ch5=1`（現在是裸 `OK`）、`water_pump on` 應回 `OK ch14=1`。
- ⚠️ **修好後的行為未經硬體驗證**，不要與上面「已驗」混為一談。

---

## [2026-09-07g3] 🖥 console v2 加電腦版／平板版切換（觸控目標 44px）

> 範圍：`web_backend/public_v2/index.html`。純前端，已部署（`2026.09.07-2101`）。

### Added
- **頁首新增 `🖥 電腦 / 📱 平板` 切換鈕**，與亮／暗同一套模式：`<head>` 在 CSS 之前先套用
  （避免先畫一次再跳）、選擇記在該瀏覽器的 `localStorage`。**預設 desktop ＝ 先前逐字相同的行為。**
- 🔴 **為什麼是手動開關而不是 media query**：同樣 1024px 寬，可能是滑鼠、也可能是**戴著手套的
  手指**。視窗寬度分辨不出輸入方式，`pointer:coarse` 也只分得出「有沒有觸控螢幕」。
  📌 與主題那條同型：**裝置說得出自己的規格，說不出使用者的處境。**
- **平板版只放大可觸控的東西**，不動版面欄數（欄數本來就隨寬度重排，09-07 實測 1440 四欄／1024 兩欄）
  ⇒ 平板版在寬螢幕也能用，反之亦然。判準 **44px**（iOS/Android 指南共同下限；
  現行 `.btn` 約 29px、`.numin` 約 27px、`.sw` 22px）。
  🔴 **刻意不整體放大**：這個面板的價值在「一眼看到很多數字」，等比放大會把 STOP 擠到摺線以下。
- `?density=tablet|desktop` **可覆蓋單次載入、刻意不寫進 localStorage** ——
  headless 每次都是全新 profile、讀不到 localStorage ⇒ 沒有這個參數，這個模式**無法被自動驗證**。
  📌 沿用專案既有先例（分頁寫進 `#hash` 的理由之一就是「讓自動截圖能指定分頁」）。

### Notes
- 🐛 **開關（`.sw`）要連滑塊一起改**：只放大外框、滑塊會停在原地，看起來像壞掉。
  幾何：外框 56／內寬 54／滑塊 24／打開時 `left:28`。
- 🔴 **`.sw` 是唯一達不到 44px 的**（藥丸做成 44px 高很突兀）。改用一層看不見的 `::before`
  把**可點範圍**上下各撐 7px（30+14=44），視覺維持 30px。
  ⚠️ `.sw` 的 `::after` 已被滑塊佔用，只能用 `::before`。
- ⚠️ 平板版的規則是**列舉不是通則**：日後新增控制項不會自動進到那個區塊。

### Verified
- 兩個 inline `<script>` 各自 `node --check`；靜默失敗檢查全過（重複 id 0／寫到不存在的元素 0）
- **兩種模式各截一張同尺寸（1024×768 Manual）對照**：按鈕明顯放大、切換鈕文字正確、
  分頁列變高；另拉高視窗到 1024×1500 專門確認 **8 顆繼電器開關的滑塊沒有錯位**
- 部署後 md5 兩邊逐位元一致

---

## [2026-09-07g2] 🖥 console v2 首次被渲染出來看（headless，帶真實資料）；輪詢說明改由資料產生

> 範圍：`web_backend/public_v2/index.html`。純前端。

### Fixed
- 🔴 **頂欄輪詢說明只說了四分之三的實話。** `id="polln"` 寫死 `status 1s / relay 4s / pwm 6s`，
  而實際有**四個** `setInterval`——**漏了 `rail 7s`**。
  ✅ 改成由 `POLLS` 表同時產生 `setInterval` 與那行說明 ⇒ 間隔與標示不可能再分岔。
  📌 **一個只說四分之三實話的清單，比沒有清單更糟**——它看起來像完整清單。
  （`paint` 刻意不入表：它是重繪不是輪詢，不對外送指令。）
- 🔴 **版號三次部署都沒更新**（`2026.09.04-1412` 掛到今天）。本專案規則是**每次部署前必須更新版號**，
  那個 badge 正是使用者確認「看到的是不是新版」的唯一機制。本條為自記缺失。
  ⇒ 今日最終版號 `2026.09.07-2032`。
  🟡 **順帶發現：8080 那支 `public/index.html` 根本沒有版號欄位**，無法用同樣方式確認。已入待辦。

### Notes
- 🔴🔴 **Setting 分頁「重啟後還剩什麼」表的「編譯預設」整欄是寫死的複本**，而吊機的編譯常數
  **沒有任何查詢管道**（「目前值」欄是每秒問 status，可信）。
  機器線 `[2026-09-07m1]` 已改 `g_balance_source` 預設，**該 binary 一部署，這裡就有四處同時失真**。
  ✅ 已在該表上方寫入逐處改法。🔴 **刻意不先改**——新 binary 上線前先改，錯的就變成這裡。
  ⚠️ **其中一處我第一版寫錯，由 ai-2 當場更正**：「會遺失」pill **不可以改成「不會遺失」**。
  機器線只改了**編譯預設落在哪**，`balance_source` **仍然完全沒有持久化** ⇒
  **值一樣會遺失，只是後果從「掉到錯的那一個」變成「掉到對的那一個」。**
  📌 **教訓：寫給未來執行者的指示，寫錯比不寫更糟**——它會被照做，而且看起來很權威。

### Verified —— console v2 上線以來第一次被渲染
- 🛠 **靜態檢查器**（09-04 那支寫了沒進版控，已重寫，暫放 scratchpad）：
  重複 id **0**／寫到不存在的元素 **0**／大括號平衡 **0**／`<div>` 149:149。
- 🎯 **四個分頁 1440×900 全數截到，且帶真實機器資料**。逐項對後端真值全部吻合：
  左右繩長差 `+3`（56/53）／`roll +0.88°`／距中止 `5.12°`（＝6.0−0.88，`ROLL_TRIP` 正確）／
  張力門檻 `100·50·130·75` 四個全中／吸盤 `上右 s5 上左 s6 下右 s7 下左 s8`
  （**08-28 左右歸屬修正在 GUI 上是對的**）／ZDT 右側 `{5,7}`／行程守衛 `0–130`／
  arm 點紅、手臂「未讀取」（motor_api 未跑）／前置檢查 4/6 兩項不過皆為真實狀態。
  ✅ 本批 `polln` 修正也在畫面上確認生效（四項齊全）。

### 🔴 方法論：`--timeout` 不等，差點又是一次誤判
第一張截圖**吸盤與張力兩張卡全空**。09-04 記載過同型假象（`--virtual-time-budget` 快轉計時器），
我沒用那個參數，所以一度以為是真的壞了。
**實測：headless Chrome 的 `--timeout=20000` 根本不等，2 秒就截圖** ⇒ 是截太早，不是 GUI 的 bug。
- ❌ 父頁 busy-wait 延後 `load`：整頁截成白的（繪製也被擋住）
- ✅ **可行解：讓 `load` 等一張慢速回應的圖片**——本機起一個睡 14 秒才回 1×1 PNG 的小伺服器，
  wrapper 頁面 iframe 真實頁 + 引用那張圖。`load` 被延後而**主執行緒全程沒被佔住**。
📌 通則：**`load` 事件觸發 ≠ 畫面畫完了**。與「讀到一行 ≠ 讀到我這筆的回覆」（framing）、
「`OK` ≠ 真的生效」（回讀）是同一句話的三個版本：
**工具給了一個看起來完整的答案，而它的完整性是假設出來的，而那個假設從來沒被寫下來過。**

### 仍未驗（不要混進上面）
- **真人瀏覽器**：按鈕真實行為、觸控目標大小、**實際陽光下可讀性**（亮色是依原理選的，不是量過的）
- **動作類按鈕**：吊機不能動；真空閥/幫浦/繼電器雖可動，但同時段機器線正在用，
  刻意不碰以免兩個 session 同時動繼電器
- **Mission `bal:` 那句 roll 說明字串**（本日 `g1` 改的）需 `|roll| > 1°` 才會顯示，今日未觸發

---

## [2026-09-07m1] 🔴 `balance_source` 編譯預設 Meter → Imu；`cmd_pump` 的謊已在 09-03 修掉且已上機驗證

> 範圍：`Crane_control_PI/main.cpp` 兩處（**未部署** —— C++ 要 VS remote build，見下方 Verified）。
> `cmd_pump` 本次**沒有改任何一行程式**，只做了查證與實機驗證。
> 🔴 全程未對吊機發任何動作指令；本體只動了 PQW **CH2（pumpA）**，收尾已還原（見 Verified）。

### Fixed
- 🔴🔴 **`g_balance_source` 的編譯預設由 `Meter` 改為 `Imu`**（`Crane_control_PI/main.cpp:834`）。
  症狀：production 一律跑 `imu`，但那是**執行期**用 `set_balance_source imu` 設的、沒有持久化
  ⇒ 吊機程式一重啟就靜默退回 `meter`，除了 `status` 之外沒有徵兆。09-03 深夜記過一次、
  09-04 早上在有人正在操作 GUI 時重演，該趟 `down on` 整趟資料不可比
  （對照組：關掉 IMU 平衡，平均 |roll| 0.68° → 2.52°）。

  ✅ **改預設之前先確認「預設 Meter」是不是 IMU 沒接時的安全退路 —— 它不是。**
  退路在別的地方，而且是**每個 tick 重新判定**的（`apply_balance_trim`）：
  ```
  use_imu = want_imu && imu_roll_fresh()
  ```
  `imu_roll_fresh()` 在**從未收到**（`g_imu_roll_stamp_ms == 0`）與**已過期**
  （`> IMU_ROLL_STALE_MS = 750ms`）兩種情況都回 `false`，而 `else` 分支用的
  kp / deadband / 誤差式與 `Meter` 路徑**逐位元相同**。
  ⇒ **IMU 沒接時，`imu` 與 `meter` 兩個設定跑的是同一段控制律**，唯一差別是多印一行
  「退回計米器」的警告（每 2 秒一次、stderr）。
  **default=Imu 在 IMU 缺席時不比 default=Meter 差，在 IMU 在線時才是對的** ⇒ 改。

  📌 **原本 `Meter` 的理由，註解自己寫著「與現行逐位元相同」** —— 那是 2026-09-01
  導入這個功能當天的 **opt-in 開關**（新功能不改變既有行為），不是安全設計。
  `git log -L834,834` 確認該行**自導入日起一個字都沒動過**：功能在 09-01 就已驗收
  （229cm 全程實測），開關卻沒有跟著翻過來。

  🔴 **另一個支持改預設的事實**：`LENGTH_DIFF_MAX_CM_DEFAULT` 已經在 09-01 為了 IMU 世界
  **永久**由 15 放寬到 10、語意也從「姿態指標」改成「單側卡死偵測」。
  留著 `default=Meter` 等於出廠組態是「計米器控制 ＋ 為 IMU 世界調過的守衛」
  —— **兩邊沒有一邊是被驗證過的組合**。

### Added
- 🔴 **開機大聲宣告平衡誤差來源**（`Crane_control_PI/main.cpp`，command server banner 之後）。
  改預設解決的是「重啟後跑錯組態」，**解決不了**另一半：`balance_source=imu` 只表示
  **想要**用 IMU，實際用不用得到還要看 roll 新不新鮮 —— 而 roll 是**本體**在移動中
  以 ~4Hz 推過來的（`cmd_set_imu_roll`），**吊機自己沒有 IMU**。
  所以開機當下必然是「從未收到」，這行的用途是讓操作者知道
  「現在還沒有 IMU 平衡，要等本體連上並開始推送」，而不是看到 `balance_source=imu`
  就以為它在運作。📌 這正是 `cmd_status` 裡 `imu_roll_age_ms` / `imu_roll_fresh`
  存在的理由，只是那要有人去問；開機這一行是**不問也會看到**的那份。

### Notes — `cmd_pump` 查證結果：**本次無需修改，它 09-03 就修好了**
- 🔴 **交接單上的「`cmd_pump` 沒有回讀」已經過期。** `app/wash_robot_commands.cpp:3821`
  現在走 `pqw_set_relay_verified_()`（回讀 + 最多 3 次重試），並且**再自己讀一次
  回報實際通道狀態**，比同輩的 `cmd_relay` 多一層。
  沿革在 `changelog [2026-09-03a]`：「`cmd_pump` 改走 `pqw_set_relay_verified_()`
  ……09-02 曾出現 `pump on` 回 OK 而連續四次 `relay_status` 讀到 `ch2=0`」。
  ⇒ **09-02 那個症狀就是這條 changelog 的動機，不是還沒修的 bug。**
- ✅ **而且已經上機**：`~/bringup/facade_cleaning_v2.out`（Sep 4 10:44、`pid 1749` 正在跑的
  就是它）`strings` 查得到新版才有的 `pump_set_but_readback_fail`。
  ⇒ 「repo 修好但機上還是舊的」這個常見缺口在本例**不存在**。
- ⚠️ **同一類缺陷仍留在兩支同輩指令**（本次未動，列出來讓它別再隱形）：
  | 指令 | 現況 |
  |---|---|
  | `cmd_brush`（CH5） | 裸 `pqw_.controlRelay()` + 無條件 `OK`，**無回讀** |
  | `cmd_water_pump`（CH14） | 同上，**無回讀** |
  🔴 `CH_BRUSH` 正是 2026-07/08 誤號打到空通道、log 完全看不出來的那個通道
  （`cmd_relay` 的註解自己寫著這件事）。**修好的是 `cmd_pump`，不是這個形狀。**
- ℹ️ **實測發現的殘留觀察（未處置，屬設計決策）**：回讀整條掛掉時，
  `cmd_pump` 回的是 **`OK pump_set_but_readback_fail`** —— 字串開頭仍是 `OK`。
  呼叫端若用最自然的 `reply.startswith("OK")` 判斷，這一筆會被當成成功。
  ⚠️ 而且**這條路徑不寫任何 log**（`pqw_set_relay_verified_` 在 `st.empty()` 時
  直接 `return false` 不印，`cmd_pump` 也不印）⇒ **唯一的徵兆就是回覆字串本身**。
  📌 現行註解說這是刻意的（best-effort，權威留給下游 vacuum check），所以本次不改，
  但它值得被知道：本次 7 筆 pump 指令中**出現了 1 筆**。

### Verified
- ✅ **語法檢查（吊機改動）**：把 repo 現行的 `Crane_control_PI/main.cpp` ＋
  `common/ transport/ user_lib/ mechanism/` 打包送上本體 Pi，用 Pi 的 g++ 14.2.0 檢：
  ```
  g++ -fsyntax-only -std=c++17 -I Crane_control_PI -I common -I transport -I user_lib -I mechanism \
      Crane_control_PI/main.cpp     → 通過（1.6s）
  ```
  🔴 **有做負控制**：同一份檔插一行 `int probe = undefined_symbol_probe;` 會被抓到
  （`error: 'undefined_symbol_probe' was not declared in this scope`）⇒ 這個檢查是活的。
  ⚠️ **刻意不用 Pi 上的 `~/projects/crane_control_PI/`** —— 那棵樹沒有 `common/`、
  `transport/`、`mechanism/` 三個目錄（停在舊版面），拿它檢等於檢了別的東西。
- 🎯 **`cmd_pump` 實機端對端驗證（本體 `192.168.5.26:5001`，機器在地面、張力總計 ~6.2kg 無吊重）**
  ```
  基線        relay_status  ch1..ch16 全 0
  pump on  →  OK ch2=1      relay_status ×4 全部 ch2=1     ← 09-02 的失敗場景，現在 4/4 通過
  pump off →  OK ch2=0      relay_status    ch2=0
  再 on/off 循環 ×3          回讀與回覆逐筆一致
  ```
  ⇒ **09-02「回 OK 但連四次讀到 ch2=0」無法重現**，修正確實生效。
- 🔴 **收尾已還原並複驗**：`pump off` → `OK ch2=0`，再獨立讀 `relay_status` **3 次**
  全部 `ch1..ch16 全 0`，**與動手前的基線逐欄相同**。動過的通道只有 **CH2**；
  CH1（真空閥）與 CH6（破真空閥）**一次都沒碰**。
- ⚠️ **吊機那兩處改動未經任何硬體驗證**（機器維修中、且 C++ 需 VS remote build）。
  `balance_source` 預設與開機宣告**只到語法層為止**，行為要等下一次建置窗口。
  **不要把本條 changelog 的「實機驗證」讀成涵蓋吊機** —— 那一段只涵蓋 `cmd_pump`。

### Notes — 一個測試工具自己的坑（免得下次再花時間）
- 第一版測試 client 每次 `ask()` 都丟掉緩衝區殘餘 ⇒ 回覆被切成兩個 TCP 段時，
  後半會被**下一個**指令的 `ask()` 吃掉，畫面上長得像「某一筆 `relay_status` 沒有回應」。
  **那是測試工具的框幀 bug，不是機器的**：改成持久緩衝後不再出現，
  且用 8 條各自獨立的連線覆測 `relay_status` **8/8 全正常**。
  📌 與 `changelog [2026-09-04c]`（吊機 keepalive 的回覆冒充成指令回覆）**同一個形狀**
  ——共用連線上，「讀到一行」不等於「讀到我這一筆的回覆」。

### 待部署
🔴 `Crane_control_PI/main.cpp` 兩處改動**尚未建置、尚未部署**，需 Windows 端 VS remote build
（`Debug|ARM64`）。部署後第一件事：看開機那行 `[INFO] balance_source=imu`，
並在本體開始推送 roll 之後用 `status` 確認 `imu_roll_fresh=1`。

🔴🔴 **這個 binary 一部署，`web_backend/public_v2/index.html` 有四處會同時開始說謊，
必須「跟著部署一起改」** —— 由 GUI 線（agent-ai-5a）2026-09-07 指出，本次已逐處查證存在。
根因：吊機的**編譯期常數沒有任何查詢管道**（不像 `step_cm_max` 有 `get_settings` 可問）
⇒ console v2 Setting 分頁那張「重啟後還剩什麼」表的「編譯預設」欄整欄是寫死的複本。

| # | 位置 | 部署後應改成 |
|---|---|---|
| 1 | Setting 表 `balance_source` 列的「編譯預設 **meter**」（約 `line 867`，還帶 `color:var(--bad)` 紅字） | `imu`，並拿掉紅字 —— 預設不再是「已知錯」的那一個 |
| 2 | 同列「重啟後」欄的 `會遺失` pill（約 `line 868`） | ⚠️ **見下方更正，不是單純改成「不會遺失」** |
| 3 | Dashboard `src === 'meter' ? '（重啟後的預設值）'`（約 `line 1439`） | `src === 'imu' ? …` —— 註記邏輯整個反過來 |
| 4 | Mission 起跑檢查 `bal:'…每次重啟都會靜默回到編譯預設…'`（約 `line 1569`） | 字面**仍為真**，但語氣要調：回到的已是對的那個值 |

⚠️ **第 2 點要更正 GUI 線的說法**：交接訊息寫「改完之後就不會遺失了」——**不對**。
本次改的是**編譯預設落在哪**，`balance_source` **仍然完全沒有持久化**：
執行期設成 `meter` 之後重啟，一樣會靜默變回 `imu`。**值照樣遺失，只是遺失的後果從
「掉到錯的那一個」變成「掉到對的那一個」。**
📌 該 pill 自己的定義（見該檔 `重啟後存活統計` 註解）是「**不能靠重啟後的值**」——
按這個定義它確實該改，但理由是「預設已等於慣用值」，**不是「它會存活」**。
🔴 **寫成「不會遺失」會製造一個方向相反的新謊**，而這張表整個存在意義就是回答
「重啟後會變成什麼」。建議保留「會遺失」語意、改寫「依據」欄說明後果已反轉。

✅ **`set-tally`（「不可信 N 項」）不必手改** —— 它是 `querySelectorAll('.pill.pl-lost').length`
從 DOM 現數的，pill 一改它自己會跟著（該處註解正是為了避免「同一件事寫在兩個地方」）。
✅ 已逐處確認**沒有第五處**：`line 1806` 那句「這是執行期值，重啟就會回到編譯預設」
屬張力門檻對話框的通用文案，與 `balance_source` 無關。

🔴 **在這個 binary 部署之前不要先改那四處** —— 現在改，錯的就會變成 GUI 那邊。
這是一件必須與部署同一動作完成的事。

---

## [2026-09-07g1] 🔧 步伐上限改成向後端取值；補掉 console v2 第三處寫死的 roll 門檻

> 範圍：`web_backend/public/{index.html,app.js}`、`web_backend/public_v2/index.html`。
> 純前端，不需重建 C++、不需重啟 node（express 每次請求重讀檔）。
> 🔴 **全程只送 `status` / `get_settings` 兩個唯讀指令**——機器在維修，一道動作指令都沒發。

### Fixed
- 🔴🔴 **腳本步伐上限三邊不一致，而 repo 自己就是錯的那一邊。**
  Pi `5..50` ／ repo `5..100` ／ 後端 `STEP_CM_MAX = 45`。
  repo 那個 100 是 2026-08-28 為了對齊「當時」的後端而改的，**後端 08-31 降回 45 之後沒人跟著改**
  —— 那正是該處註解自己警告過的「預覽說 OK、送出被拒」，只是方向相反。
  📌 **這件事是在部署前一刻才攔下來的**：本來只是要把落後的 `public/` 推上機，
  逐 hunk 讀 diff 才發現其中一個不是註解而是行為改變。
  **「部署落後版本」與「repo 是對的」是兩件事，不要混為一談。**
  ✅ 修法**不是改成 45**，是改成向後端取值：
  - 後端權威 = `parse_script_csv_`：`STEP_CM_MIN .. settings_.step_cm_max`
    （`app/wash_robot_commands.cpp:3296`）。`settings_.step_cm_max` 是**執行期可調值**，
    被 `cmd_set_setting` 夾在 `[5, STEP_CM_MAX]`（`app/WASH_ROBOT.cpp:1103`）。
  - `get_settings` 吐 `step_cm_max=<現值>:<STEP_CM_MAX>` ⇒ `cur` 給 `parseScriptCsv` 當上界、
    `def` 給設定頁那個 input 當 `max`。
  - `index.html` 的 `max="100"` **整個移除**（不是換個數字），由 JS 從回覆填。
- 🔴 **`get_settings` 先前只在使用者切到 Settings 分頁時才發。**
  ⇒ 沒開過設定頁的話，前端手上根本沒有後端的值，腳本預覽是照保底值放行的。
  ✅ 改在 `ws.onopen` 每次(重)連線就要一次，接在既有的「強制同步 status」那組後面。
- 🔴 **console v2 還有第三處寫死的 roll 門檻。**
  09-04 已把渲染路徑那兩處（`/6.0` 與 `(6 - …)`）收斂成 `ROLL_TRIP`，
  但**起跑檢查的說明字串裡還有一個 `6°`** —— 它不在計算式裡，所以逃過了那次收斂。
  📌 **教訓：收斂常數時，grep 要連字串一起掃，不能只看運算式。**
- ✅ **`刷洗滾筒 (CH15)` → `(CH5)`**（Pi 上落後的操作者可見錯標籤；repo 08-28 就對了）。

### Notes
- 🔴 **`ROLL_TRIP` 不能比照 `step_cm_max` 處理，已在該處註解寫明。**
  `step_cm_max` 有 `get_settings` 可問；roll 門檻是 **`cycle_test.py:76` 的 `sys.argv[4]`**，
  是腳本自己的執行期參數，**兩個後端都不知道它** ⇒ GUI 沒有任何管道查得到。
  要真解決得讓 `cycle_test` 起跑時把參數回報出來（EVT），屬 C++/Python 那條線。
  在那之前它就是一份無法查證的複本，而且**腳本是可以帶參數啟動的**
  （`cycle_test.py 1 5 40 4.0`），那一刻畫面就開始說謊且無徵兆。
- ⚠️ **`step_cm_min` 仍是複本**：`get_settings` 不吐它，沒有管道問。已在註解標明。
- ℹ️ 順帶觀察（未處置，不是前端的錯）：`step_cm_default` 的 apply 邊界是 `[5, 60]`，
  **上界比 `STEP_CM_MAX`(45) 還大** ⇒ 可以把預設步距設成 60，但任何實際 > 45 的步伐
  都會被 `parse_script_csv_` 拒絕。HTML 的 `max="60"` 與後端相符。

### Verified
- `node --check` 過 `public/app.js`；console v2 的兩個 inline `<script>` 抽出後各自 `node --check` 過，
  並確認 `ROLL_TRIP` 的宣告與新使用點**在同一個 script 區塊**（scope 沒斷）
- 部署後 **md5 兩邊逐位元一致**（`public/` 兩檔 + `public_v2/index.html`），
  再用 `curl` 確認**服務出去的內容**真的換了：舊字串計數 **0**、新字串計數 **1**
  📌 **不是只比對檔案** —— 檔案對而服務端沒換過的情況（快取、PUBLIC_DIR 指到別處）看不出來。
- 🎯 **拿實機回覆真的跑一次解析**：向 `192.168.5.26:5001` 要 `get_settings`
  （實得 `step_cm_max=45:45`，共 22 筆設定），餵給部署後的那段邏輯：
  ```
  backendStepCmMax : 45   (先前寫死 100)
  input.max        : 45   (先前寫死 100)
    step   4 cm -> 擋下 (超出 5..45)     step  45 cm -> 放行
    step   5 cm -> 放行                  step  46 cm -> 擋下
    step  30 cm -> 放行                  step 100 cm -> 擋下 (先前會被放行)
  ```
- 🔴 **仍未經瀏覽器**：以上全部是「檔案對、服務出去的內容對、解析邏輯對」，
  **畫面沒有人看過、按鈕沒有人按過**。不要把這條 changelog 當成 UI 已驗證。

### Rollback
Pi 上已留 `index.html.bak-20260907` / `app.js.bak-20260907`（`public/` 與 `public_v2/` 各一份），
`cp` 回去即可，**不需重啟 node**。

---

## [2026-09-04r] 🎯 `cycle_test` 完整週期跑通（5 步 200cm + 回程）；roll 修正再修兩個實作缺陷

> 範圍：`Linux_test/cycle_test.py`。

### Fixed
- 🔴 **修正成功但「剛好不需補完距離」時仍會中止。** `res` 留著被中止的 `ERR aborted`，
  掉到 `if not res.startswith("OK")` 就停 —— **修好了卻回報修不好**。
  補完那條路徑有重新賦值 `res`，只有這條沒有。首次實跑就踩到（剩餘 −1cm）。
  📌 這條分支只在「修正成功 **且** 剩餘 < `TOL`(5cm)」才走到 ——
  **`1 2 40` 的小測試 3 分鐘撞出來，直接跑十週期會極難重現。**
- 🔴 **一次狀態讀取失敗就放棄整個修正。** 階段 2 首跑：`roll_correct -3`
  **實際上成功了**（事後量到 L=−191/R=−190、roll 由 −6.94° 變 +1.37°），
  但緊接的狀態讀取失敗，程式放棄並中止整場。
  ✅ 改為**重試 3 次**，且失敗時印出**原始回應**（原本只印「讀不到」＝把診斷資訊丟掉）。

### Verified（`cycle_test.py 1` 首次完整跑通：5 步 × 40cm + 213cm 回程）
```
步1  伸出3.4 真空1.4 清潔23.4 收回3.3  -43/-60/-49/-51  移動10.7  Δ6
步2  伸出2.9 真空2.2 清潔11.5 收回3.3  -51/-34/ -1/  0  移動12.5  Δ8  ← 橫桿，跳過清潔
步3  伸出2.4 真空2.8 清潔25.4 收回3.3    0/  0/-54/-53  移動11.5  Δ5
步4  伸出4.6 真空0.0 清潔29.0 收回4.3  -26/-37/-51/-41  移動 9.8  Δ5
步5  伸出5.4 真空0.0 清潔11.2 收回3.3  -43/-50/-56/-52  移動12.7  Δ3  ← 橫桿，跳過清潔
回程 213cm @30Hz 21.0s → 高度 235      === 1 個週期全部完成 ===
```
- 🎯 **roll 自動修正 2 次觸發、全部成功**。步4 那次**第一道修正過頭、反向到 +4.24°，
  第二次才收斂** ⇒ **迭代設計是必要的**，寫成「修一次就算」那次會失敗。
- ✅ **連續判定擋掉 1 次假警報**（`roll 超標後自行回復: 1 次`）。
- 🎯 **`obstacle` 2/5 步觸發**，接觸角 **0.5392 / 0.5457** —— 比先前三次（0.4961~0.4988）
  遠 0.045 ⇒ **是不同的兩根橫桿，凸出量較小**，兩根都被門檻 0.5650 正確攔下。

### Notes
- 🔴 **30Hz 已經摸到 8cm 門檻**（1/6 段），而 09-01 記的 50Hz 是 9cm、韌體上限 10。
  **餘裕只剩 2cm。** 而移動只佔單步 45.6s 的 **11.4s（25%）**，滑台清潔佔 **20.1s（44%）**
  ⇒ **提速快送的效益遠小於代價**（每次 roll 修正吃 10~15s）。真正該優化的是滑台那段。
  📌 per user 當日給的規格：快送 30~50Hz、調整 10~20Hz。現行全部合規但貼在下限，
  **per user 指示先不動**。
- 🔴 **五步沒有一步四顆吸盤全中**，而相鄰 40cm 的步2/步3 吸附位置**完全對調**
  （上兩顆 vs 下兩顆）⇒ 玻璃縫分布比想像中密。
  這對 ③a 的「至少一顆」判準是直接證據（達標數 2/1/2/1/3）。
- 🔴 **`真空s=0.0` 不代表吸得好**：步4/步5 真空瞬間達標，但步4 是 `-26/-37/-51/-41`
  ——只有一顆過判準。**「快」只說明第一顆很快達標。**
- 🔴 **架構缺口（由 facade web gui session 指出，已提報 per user）**：
  `cycle_test` 跑的時候 washrobot **全程 `state=idle`** —— 它不用 `step_*`，
  而 `Running` 只有 `step_*`/`cross_obstacle_*` 會設 ⇒ **兩個後端都沒有「腳本正在跑」的訊號**。
  實測時序：腳本啟動到第一次 `pay_out` 之間**約 33 秒完全沒有鎖**，而那正是推桿／真空／
  手臂在動的階段。前端的鎖擋不住另一個分頁、另一台電腦、或直接 TCP。
  🟡 **未做**：乾淨解法是後端「作業佔用」旗標，但它牽涉安全語意
  （哪些指令要擋、`STOP`/`PARK` 是否豁免、**異常結束時怎麼釋放** ——
  今天光 `bail()` 就修了三個實作缺陷，一個沒釋放的鎖會把機器鎖死到重啟）⇒ 待 per user 拍板。

📌 **changelog 日內編號兩條線撞過兩次**（`m`/`n`）。已與 facade web gui session 約定：
   **本線用 `m1 m2…`（machine）、GUI 線用 `g1 g2…`**，2026-09-05 起生效；裸字母到 `q` 為止。

## [2026-09-04q] 🔒 Mission 的兩顆修正鈕納入腳本鎖；並記錄「腳本執行中鎖定」的三個缺口

### Fixed
- 🔴 **Mission 頁的 `fix-bal` / `fix-hz` 先前完全沒被鎖到**（per user 指示補上）。
  它們送的是吊機**執行期參數**（`set_balance_source` / `set_motion_hz`），
  **腳本跑到一半按下去會直接改變正在跑的運動**。
  而 `.locked` 只套在 `.man`（Manual 的容器）上，Mission 頁不在裡面。
  📌 **這是實務上最危險的一個缺口**：使用者被引導「Mission 頁可以看」，人就待在那一頁，
  而那兩顆就在眼前。
  ✅ 改用 `disabled`（不是 `.locked` 的 `pointer-events:none`）——
  🔴 **`pointer-events:none` 擋不住鍵盤**：Tab 移過去按 Enter 照樣送得出去。
  ⚠️ **同一個弱點在 `.man` 的鎖上也成立，尚未處理**（見待辦）。
- 鎖定橫幅補上：PARK 同 STOP 不被鎖、Mission 兩顆同時被鎖、
  以及「腳本第一次放繩之前約 30 秒仍然沒有鎖」這個已知缺口。

### 📌 架構盤點：「Mission 執行時 Manual 不能動作」目前只做到約七成
> per user 規劃：Mission 執行時 Manual 不能動作、Dashboard / Mission 可觀看。

- ✅ **Dashboard / Mission 可看** —— 鎖只加在 `.man`，其他分頁不受影響。
- 🟡 **Manual 鎖定是「推論」不是互鎖** —— 唯一判準是吊機 `motion_active`，
  靠 `LOCK_HOLD_MS=45s` 保持撐過非吊機階段。
- 🔴🔴 **缺口一：腳本啟動到第一次放繩之前完全沒有鎖（約 30 秒）。**
  `cycle_test.py` **不用 `step_down`/`step_up`**，而是自己用基本指令組出週期
  （`pusher` / `rail` / `arm_deploy_f` / `pay_out`）；而 washrobot 的 `Running`
  **只有 `step_*` 與 `cross_obstacle_*` 會設** ⇒ **腳本全程 `state=idle`**。
  ⇒ **兩個後端都沒有「腳本正在跑」這個訊號。**
  以另一條線 09-04 實跑時序推算：伸出 3.4 + 真空 1.2 + 清潔 25.2 + 收回 3.3 ≈ **33 秒無鎖**，
  而那正是推桿／真空／手臂在動的階段 —— **恰恰是 Manual 最會干擾的時候**。
- 🔴 **缺口二：鎖是純前端、per-tab。** 另一個分頁／另一台電腦／直接 TCP 都不受限。
  腳本自己開的是獨立連線（`127.0.0.1:5002` + `192.168.5.26:5001`），
  與 web_backend 的橋接是平行的兩條 ⇒ 後端沒有「誰擁有這台機器」的概念。
- 🔴 **缺口三：`pointer-events:none` 擋不住鍵盤**（見上）。

### 📌 決策（per user 2026-09-04）
- **`STOP`（吊機）與 `PARK`（手臂）維持不被鎖。**
  它們是把控制權拿回來的唯一手段；手臂壓著玻璃是持續施力，
  達妙馬達長時間受力會過熱／過流鎖存而只能斷電解除 ——
  **鎖掉卸力的唯一按鈕，代價比誤觸大**。

### 建議（未做，留待決定）
- **後端加明確的「作業佔用」旗標**：腳本開跑宣告、結束釋放，washrobot 廣播 EVT。
  這樣才涵蓋全程、對所有 client 成立，甚至能讓**後端真的拒絕**手動指令而不只是前端不給按。
  ⇒ 屬 C++ 那條線；在它出現之前，前端的鎖**不能當成安全裝置**（介面上已如此標明）。

### 驗證
- JS `node --check` 通過；`<div>` 149/149；重複 id 0、靜默寫入 0、`getElementById` 找不到 0。
- 部署後 HTTP 200。
- 🔴 **鎖定路徑未在腳本實跑時觀察過** —— 是讀碼與時序推算得出的，不是看著它鎖起來驗的。

## [2026-09-04p] 🎯 `cycle_test` 首次跑通；roll 由「瞬時中止」改成「連續判定 + 自動修正」

> 範圍：`Linux_test/cycle_test.py`。當日 per user 全程在場、逐次指認現場狀況。

### Fixed
- 🔴 **`roll` 中止門檻由「瞬時一筆」改為「連續 `ROLL_PERSIST`=3 筆」**，與左右差同一套邏輯。
  09-03 就記過這條待辦（「同一個教訓只套用了一半」，當日 223cm 收回全程平均 1.33°
  卻被最後一筆停止擺盪 −7.04° 中止）。
  09-04 兩段上升（110cm / 118cm）再次量到瞬間尖峰 **−6.65° 與 −5.41°**，兩次都隨即回穩
  ⇒ **機體在繩索移動中本來就會瞬間擺過 6°，那是固有行為不是姿態失控。**
  📌 首次實跑就證明這個改動**沒有掩蓋真事件**：它把一次持續傾斜正確判成中止，
  事後量到 roll 靜止在 −7.07° 不動。
- 🔴 **實作 bug（首次實跑就踩到）**：roll 自動修正成功、而**剛好不需要補完距離**時，
  `res` 仍留著被中止的 `ERR aborted`，掉到下一行 `if not res.startswith("OK")` 就中止
  —— **修好了卻還是停**。補完那條路徑有重新賦值 `res`，只有「不需補完」這條沒有。
  📌 那條分支只有在「修正成功 **且** 剩餘距離 < `TOL`(5cm)」時才會走到 ——
  **用 `1 2 40` 的小測試 3 分鐘就撞出來了**，直接跑十週期會很難重現。

### Added
- 🎯 **`roll_recover()` + `clear_error()`：roll 造成的中止改為自動修正後續行**
  （per user：「這個修正之後也要做在腳本裡面，救得回來就繼續腳本，不行才停住」）。
  流程：偵測 → 讀 `L`/`R`/`raw_x` 診斷 → `roll_correct` 迭代至 `|roll| ≤ 3.0°`
  → `reset` 清 `error`（中止時 `emergency()` 送過 `emergency_stop`）
  → 補完本步沒走完的距離 → 續行並計數。
  **首次實跑一次到位**：
  ```
  ⚠ roll 連續 3 筆超過 6.0°（最後 -6.19°）
  ⚙ L=-149 R=-154（差 +5）roll -6.23° → roll_correct -2
  ✅ roll 已回到 +0.56°（門檻 3.0），第 1 次後達成
  ✅ 清除 error：reset -> OK reset（現在 state=idle）
  ```
  🔴 **三條刻意不自動修正的分支**：
  ① **繩長已齊平卻仍歪** —— 不是繩長造成的，再動繩只會更糟，直接停。**這條最重要。**
  ② `tension_valid=0` / 左右差持續擴大 —— 那是「保護失效」與「持續發散」，性質不同。
  ③ 修正後補完再次中止 —— 不重試第二次。
  📌 `roll_correct` 的符號：`+delta = 左放右收`、`-delta = 左收右放`。`L-R > 0` 要用**負** delta。
  📌 **delta 與實際位移不是 1:1**（送 −2 實際兩側各動 3cm）⇒ 用**迭代量到為止**，不猜增益。

### Verified（首次完整跑通 `cycle_test.py 1 2 40`）
```
步1  伸出3.4 真空1.2 清潔25.2 收回3.3  -41/-59/-46/-50  移動11.7  Δmax 4
     ⚠ 疑似橫桿（theta=0.4961）—— 跳過清潔、續行
步2  伸出2.7 真空3.3 清潔13.9 收回3.2  -54/-62/  0/  0  移動11.4  Δmax 4
回程 79cm @30Hz 12.2s → 高度 234        === 1 個週期全部完成 ===
```
- 🎯 **`obstacle` 三次觸發，接觸角 0.4988 / 0.4965 / 0.4961 —— 落在 0.0027 rad 內。**
  同一根橫桿、重現性極好，而且**三次都正確跳過清潔並續行**。
  **下午那個「obstacle 由中止改為跳過」的翻案若沒做，這三次每次都會讓整場停。**
- ✅ ①→⑧ 全部跑通 + ⑨ 回程。

### Notes
- 🟡 **吸盤密封品質隨高度差很多**：步1 `-41/-59/-46/-50`（只有一顆過 −50 判準）、
  步2 `-54/-62/0/0`（**下面兩顆完全沒吸住**）。③a 的「至少一顆」都放行了，
  但那些步實際上只有部分吸盤在附著。
- 🟡 **回程的左右差是三段裡最大的**（6cm vs 下行 4cm）——回程一次連續走完，
  比分段下行更容易累積差值，而 roll 問題正是從繩長差來的。
- 📌 **水平對應 `L-R = 0`**，不是 `fine_adjust_level_diff_cm` 現值 5 所描述的 5cm。
  那個值的來源與正確性待查（09-01 曾記「當時約 3cm」）。

## [2026-09-04n] 🖥 console v2：姿態改 2D + 自動量程；張力條門檻不再頂在邊緣

> 三次改動同一個病根，合成一筆記：**「滿刻度＝門檻」會在最需要看清楚的時候失效。**
> 值一旦超標就被夾在邊緣，**超 1 與超 30 長得一模一樣**；而那正是要判斷嚴重程度的時刻。

### Changed — 姿態
- **由 CSS 3D 平板改回 2D 正視圖 + 刻度尺**（per user）。
  🔴 **理由不是 3D 做不到** —— `raw_y`(pitch) 有值、CSS 3D 也跑得順。
  是**只有 roll 是被規範與被控的量**（±1° 規格、平衡迴路修的、中止門檻判的都是它）。
  畫 3D 等於暗示「pitch 也是我們在管的」，**會讓人去解讀一個沒人負責的自由度**。
  一併移除 `d-pitch`（`raw_y` 仍在 status，要加回來隨時可以）。
- **圖形放大到吃滿卡片寬 + 細分刻線**（per user）。原本 164px 擠在數字旁、刻度是一條光溜溜的帶子，
  ±1° 規格帶只佔全幅 1/6 ⇒ 標記幾乎不動、估不出位置。
  現為每 1° 短刻、每 2° 長刻並標數字。
- 🎯 **刻度尺改為自動量程，預設 ±10°**（per user）。
  📌 **預設 10 不只是留餘裕**：先前全幅取 ±6°（＝中止門檻）時，
  **門檻線永遠在最邊緣＝等於沒畫**，而且 8° 與 6° 都頂在邊緣、長得一樣。
  ±10 之後那兩條紅色門檻線才真的落在尺上，看得到「離跳掉還有多遠」。
  📌 **規則：放大立刻、縮小遲緩**（超過 90% 立刻升檔；低於 45% 才降檔）。
  **不對稱是刻意的** —— 量程太小會把真實偏擺藏起來（危險）；太大只是解析度差一點（不危險）。
  檔位 10/15/20/30/45/90，刻度密度跟著量程走（否則 ±90 會畫出 181 根刻線）。
  ✅ 以 15 個模擬值驗過升降檔序列，含邊界不抖。

### Changed — 張力條
- 🎯 **門檻標點不再畫在條的最右端**（per user：「也頂在邊緣」）。
  原本滿刻度＝門檻 ⇒ 紅點落在 100%（被切一半），且值被 `min(100)` 夾住。
  ✅ 改為**滿刻度 = 門檻 × 1.25**（紅點固定落在 80%，右側留得下超標的部分）；
  **值若超過該量程就再自動放大**（`full = v × 1.08`，紅點往左滑）。
  超標時文字改顯示「🔴 超過上限 N.N kg」，不再只說百分比。
  ✅ 以 7 組值驗過（53/80/100/101/115/140/200 對門檻 100）。
  ℹ️ 代價：正常操作區間只用到條的 80%，解析度略降 —— **換到「看得出超多少」，值得**。

### 這一筆的方法論
- 📌 **同一個病根在三個地方**（張力「總和/130」寫死、姿態全幅＝門檻、張力滿刻度＝門檻）。
  前兩處是分別修的，第三處是使用者一句「也頂在邊緣」才連起來。
  **修第二次時就該問「這個形狀還在哪裡」** —— 這條與本日 `.grp > header` flex 擠壓
  （踩兩次才修根因）是同一個教訓。

### 驗證
- JS `node --check` 通過；CSS 大括號 181/181；`<div>` 149/149；
  重複 id 0、靜默寫入 0、`getElementById` 找不到 0。
- 部署後 md5 與 repo 逐位元一致、HTTP 200。
- 截圖確認：刻度 −10…+10、±6° 紅線落在尺上、±1° 綠帶、標記依 roll 變色。
- 🔴 **超標的顯示路徑（紅色、自動放大）未在實機上出現過** —— 兩者都是用模擬值驗的，
  機器當下並未超過任何門檻。**不要當成已驗證。**

## [2026-09-04m] 🖥 console v2：姿態圖由 3D 改回 2D（只有 roll 是被規範的量）

### Changed
- **Dashboard 姿態由 CSS 3D 平板 → 2D 正視圖 + 線性刻度尺**（per user）。
  🔴 **改的理由不是 3D 做不到** —— `raw_y`(pitch) 有值、CSS 3D 也跑得順。
  是**只有 roll 是被規範與被控的量**：±1° 規格、平衡迴路修的、中止門檻判的，全都是 roll。
  畫成 3D 等於在畫面上暗示「pitch 也是我們在管的」，**會讓人去解讀一個沒人負責的自由度**。
  ⇒ 一併移除 `d-pitch` 顯示與其寫入（`raw_y` 仍在 status 裡，需要時隨時可加回來）。
- 新圖包含：水平參考虛線、左／右標示、機體正視（含四顆吸盤點）、
  以及**線性刻度尺**（全幅 ±`ROLL_TRIP`、綠帶＝±1° 規格、標記依 roll 變色）。
  ⚠️ **圖形角度放大 4 倍**（門檻僅 ±6°、規格 ±1°，等比例畫等於不動）；
  **刻度尺是線性且未放大** ⇒ 要精確判讀看刻度與數字，**不要量圖**。
  📌 刻度全幅綁 `ROLL_TRIP` 這個常數，不是畫死的 —— 門檻若改，尺跟著改。

### 驗證
- JS `node --check` 通過；CSS 大括號 180/180；`<div>` 148/148；
  重複 id 0；靜默寫入 0；`getElementById` 找不到 0；`att-plate`／`d-pitch` 殘留 0。
- 截圖（roll −0.49°）：機體略微傾斜、刻度標記落在綠帶內、抬頭徽章「在 ±1° 規格內」。
- 部署後 HTTP 200。
- ℹ️ 截圖當下吸盤 **0/4 密封**（值 1/0/0/0 kPa）＝**機體已脫附**，另一條線正在作業；
  圓環量規在近零真空下正確退成灰色空環，顯示層沒有問題。

## [2026-09-04o] 🎯 兩個守衛拿到實測依據；橫桿讓 `obstacle` 的處置整個翻案

> 範圍：`cleaning_arm/main_api.{cpp,h}`、`Linux_test/{cycle_test,arm_cycle,walltest}.py`。

### Changed
- 🎯 **`DEPLOY_F_THETA_MIN` 0.45 → 0.565**（占位值 → 實測依據）。
  量法：機體逐段上移，每個高度重新吸附後用**低目標**（`DEPLOY_F 3.0`）輕觸，只讀 `contact`。
  低目標的用意是**碰到就停、不把 15 N·m 壓在未知物體上**。

  | 位置 | `length` | `contact`(RIGHT) | 是什麼 |
  |---|---|---|---|
  | 底部 | +3/+5 | **0.5842** | 玻璃 |
  | 中段 | −50/−52 | **0.5449 ~ 0.5510** | **橫桿**（滑台 0/50/100 全線都是，per user 確認） |
  | 上段 | −129/−130 | **0.6086** | 玻璃（per user 確認無障礙物） |

  刮刀 LEFT：底部 0.5991、上段 0.6315（同方向）。
  ⇒ 分界窗口 **0.5510~0.5842，寬 0.033 rad ≈ 14mm**，取中點。
  🔴 **關鍵是玻璃往上越來越遠**（0.5842 → 0.6086；09-03 高度 229 的 `theta_press` 0.7078
  是第三個同向佐證）⇒ **玻璃不會往橫桿那側靠**，窗口不會被吃掉。
  ⚠️ 只有兩個玻璃取樣點、都在下半部。上半部若有比 0.5842 更近的玻璃，這個門檻會誤擋。
- 🔴 **`DEPLOY_F_THETA_MAX` 0.95 → 1.10**。0.95 是憑空給的占位值，而實測**它擋住的
  不是異常，是一個合法的工作點**：
  `ERR cannot reach 15.0 Nm — 需要 theta=0.9581 超過上限 0.9500（tau 停在 12.45）`
  刮刀在 `length=-129` 要 0.9581 才壓得到 15 N·m，**差 0.008 被擋掉**。三因疊加：
  ① 該高度玻璃較遠 ② 刮刀伸出量比滾筒短 12mm（192.37 vs 204.32）⇒ M1 要轉更多
  ③ 0.95 沒有依據。1.10 距硬上限 `upper_bound=1.5` 仍有 0.4 rad。
  📌 放寬**不會**讓 `no_wall` 失效 —— 那道守衛靠「有沒有碰到」，不是「走多遠」。

### Fixed
- 🔴🔴 **`cycle_test.py` ③b 的 `obstacle` 由「中止整場」改為「跳過清潔、計數、續行」。**
  **這是設計層的翻案，不是調參數。** 我原本把 `obstacle` 當成「不該出現的東西」，
  而 per user 現場確認那是**橫桿** —— 這面牆的固定結構，一趟下行必然遇到幾根。
  那跟「有些玻璃面有縫隙、吸盤本來就吸不住」是**完全同一類的現場條件**，
  而那一條 per user 當初明講是「設計要求，不是妥協」。
  照舊邏輯，十週期遇到第一根橫桿就整場中止。
  ⚠️ 「不能頂著橫桿橫走滑台」仍然成立 —— 正確處置是**不要掃**，不是**停止整場測試**。
  新增 `obstacle_steps` 計數，**與 `no_wall` 分開記**：兩者都是現場條件但成因相反
  （`no_wall`=牆太遠／`obstacle`=有東西比玻璃更近），混在一起會看不出牆的形狀。
- **`WARN` 訊息印的是 `ITER_MAX` 常數而非實際迭代次數。** 輕觸探測時出現
  「`iters=0`」卻說「4 次迭代後」，會讓除錯的人以為它試了四次都失敗。
  （那次真正的成因是：迴圈內已達容差就 break，之後的鬆弛讓 tau 掉出容差。）
- **`walltest.py` / `arm_cycle.py` 的 `arm_status` 解析改為對欄位新增免疫。**
  原本是「把整串欄位依序寫死」的正則，要求 `pos`/`vel`/`tau` 相鄰同序。當天加 `en=`
  在尾巴剛好沒炸到，但**只要有人把新欄位插在中間，整條比對就失敗**，而症狀是
  「讀不到姿態」—— 看起來像通訊問題，除錯的人會去查網路。
  📌 由 facade web gui session 在自己的前端踩到同型後提醒。
  **新增欄位本來是相容的改動，是「依序寫死的解析」把它變成不相容。**
  順帶：值的比對加負向前瞻，`err=0x1` 這種會**整條不匹配而被略過**，
  不會吃到前面那個 `0` 給出一個看起來合理的錯值。

### Performance
- **壓上到滑台起動 13.59s → 10.78s**（per user 回報「靠上牆要過很久滑台才會動」）。
  砍掉兩段**重複**的等待：
  ① 腳本 `settle()` 輪詢到穩定（−2.0s）—— `motor_api` 已經等過鬆弛並回報穩定後的 tau，
     這是把同一件事量第二次，且全部發生在手臂已經停住之後。
  ② 收尾複驗 `RELAX_MS`(1500) → `FINAL_MS`(300)（−1.2s）—— 與迭代裡剛等過的 1500ms 重複；
     50Hz 實測鬆弛都在壓上後 1 秒內收斂完。
- **回覆新增 `ms=`**。這件事先前帳面上完全看不到，per user 回報之前只能另外寫腳本量。
- **`kp_eff` 起手值改為依工具**（RIGHT 82 / LEFT 72 / CENTER 77 佔位）+ 執行期自校快取。
  依據是收斂後的實測：RIGHT n=20 平均 81.5、LEFT n=11 平均 72.1。
  ⚠️ **只取最終值不取迭代中途的估計** —— 中途值 59~60 是剛接觸時的軟斜率，
  拿它當起手會過頭得更嚴重。

### Notes
- ⚠️ **這一項我推論錯了，照實記**：我以為 `iters` 永遠是 2 是因為起手值不準，
  調完發現**準度改善（RIGHT tau 由 15.38~15.97 變成 14.80~15.58，跨在目標兩側）
  但迭代次數沒降，有一次還變 3**。真正的原因是**尋觸停在 2.0 N·m 而目標 15.0，
  中間 13 N·m 要靠跳補完，而那段關係非線性**（剛接觸軟、接近目標硬），一次跳不可能對兩端。
  起手值調高只是讓第一跳變小 ⇒ 從跳過頭變成跳不夠。
  🟡 **真能省一輪的做法（未做）**：接觸判定與兩個守衛維持在 2.0 N·m，
  然後**讓尋觸繼續走到約 0.7×目標**才交給修正迴圈 —— 尋觸每步 150ms、迭代每次 1500ms。
- 🔴 **`LR_CALIBRATE` 不能拿來「量一下停點」** —— 它是**兩側都撞、算中點、
  然後 `set_zero_position`**（`main_api.cpp:496`）。跑它會重新定義 M2 座標系，
  而 `lr_move_to_slot_impl` 有兩套座標分支（`lr_stop_valid` 真/假），
  現在跑的是 09-02 斷電手轉的**絕對值**那條；跑完會切到另一條、零點還被移動過
  ⇒ 今天所有 M2 數字（滾筒 +0.522、刮刀 −1.005、下界 −1.05）全部要重新對應。
  📌 **我一度把它推薦成「~3 分、安全」，那是在還沒讀那支函式的時候。已收回。**
  要量負向停點，用 09-02 那個已驗證的方法：`M2 DISABLE` → 手轉到底 →
  `M2 MIT 0 0 0 0 0` 刷新快取 → 讀值。不改零點、不撞停點。
- 🟡 **未決：15 N·m 對刮刀可能不是對的目標值。** 刮刀在 `length=-129` 壓不到 15，
  刃口軟、設計上也許就該貼得淺。若是，正解是**分工具設定目標壓力**，
  那比放寬 `theta_max` 更根本。放寬只是先解開卡住。

## [2026-09-04l] 🖥 console v2：Manual 張力保護改用 Dashboard 的畫法（並抽成共用函式）

### Changed
- **Manual 的張力保護四條 gauge → 與 Dashboard 相同的長條 + 門檻標點**（per user）。
  Manual 版多畫一條「左右差」（該卡本來就有那個門檻的設定框），共四條：
  左繩 / 右繩 / 左右差 / 總和（只在拉繩時作用）。
- 🔴 **渲染抽成單一 `renderTenBars(hostId, rows)`，兩頁共用。**
  各寫一份的話，改了門檻語意只改一邊、畫面就會有一邊在說謊 ——
  **這一整天踩的坑幾乎都是同一個形狀**（寫死的 `130`、寫死的 `50`、寫死的 `6.0`、
  「表格改了摘要沒改」）。這次在製造第二份之前先合併。
- 移除 Manual 專屬的 `mkBar()` 與四組 `tg-*`/`tt-*` 元素（改由共用函式產生）。

### 🔬 順帶取得的驗證機會（部分關閉一項待辦）
- 截圖當下另一條線正在動機器：**roll = −4.00°、pitch = −0.25°、離底 129 cm**
  ⇒ **3D 姿態確實會隨實機傾斜而動**，不是只在近水平時看起來像有在轉。
  ℹ️ 同時觀察到「左右繩長差 L−R = +1 cm（左繩較長＝左側較低）」與「roll 為負」方向一致。
  🔴 **但這不算證實符號**：機體**吸在玻璃上**（當下 3/4 密封），姿態由機構與玻璃約束，
  **不由繩長決定**；且 1 cm 依 09-01 實測應對應約 0.8°，與 −4.00° 差很多 ⇒ 兩者不可互推。
  ⇒ 待辦改為「**方向已知會動、符號仍需有人看著機器確認**」，不是「已驗證」。

### 驗證
- JS `node --check` 通過；CSS 大括號 186/186；`<div>` 151/151；
  重複 id 0；靜默寫入不存在元素 0；`getElementById` 找不到 0；`mkBar` 殘留 0。
- 截圖確認 Manual 四條與 Dashboard 三條畫法一致，紅點（上限）與橘點（收繩停止 75 kg）都在。
- 部署後 HTTP 200。

## [2026-09-04k] 🖥 console v2：Dashboard 整頁重新規劃（大樓圖／3D 姿態／圓形吸盤／張力門檻標點）

> per user：**有指名的留下、沒指名的移除**。範圍仍只有 `public_v2/index.html`。

### Removed
- **頁首四格 strip**（姿態／位置／張力／裝置）與 **`crane dev_*` 旗標卡**。
  🔴 **同時刪掉 `paint()` 裡對應的 18 處寫入** —— 不刪就是「靜默寫入不存在的元素」
  （`txt()`/`bar()` 找不到就 return，**沒有例外也沒有 console 訊息**）。
  同一檔今天已抓到四個，不再製造第五個。移除後全檔重掃：靜默寫入 0、`getElementById` 找不到 0。
  ℹ️ 裝置是否上線仍看得到 —— 頁首右上的 backend / washrobot / crane / arm 四顆連線點。

### Added
- **機器在哪裡：大樓立面圖**（取代原本的細長玻璃條）。窗格 + 屋頂 + **左右兩條吊繩各自畫到
  自己那側的掛點**（左右繩長不同時看得出機體是斜的）+ 機器人本體（含四顆吸盤點）。
  🔴 高度換算沿用 `H = -length_left`（離底），畫面用 `1 - H/TOPH` ——
  **09-03 座標重新定義時這一行整個反過來過**，改動時務必回頭看。
  ⚠️ 窗格只是視覺參考，**不代表真實樓層**：玻璃面實測 231 cm，那是一片牆不是一棟樓。
- **姿態 3D**（per user 問「有沒有 3D」）：**CSS 3D transform，零函式庫**。
  🔴 **刻意不用 three.js／CDN**：這是戶外、可能沒有網路的現場介面 ——
  CDN 進不來就整頁壞掉，而 `rotateX`/`rotateZ` 是瀏覽器原生能力，離線一樣轉。
  以 `raw_x`(roll) / `raw_y`(pitch) 驅動，含機頭方向標記與水平參考線。
  ⚠️ **角度放大 4 倍**才看得見 —— 實際工作範圍只有 ±3°，等比例畫等於不動。
  ⚠️ 這是**姿態指示器不是機體模型**；方向為顯示約定，看不出方向時**以數字為準**。
- **吸盤真空度：圓環量規 ×4**（per user 指定圓形）。環長 = 真空度 / −70 kPa，
  綠＝已達密封判準（≤ −50）、橘＝有真空但未達、灰＝幾乎沒有。
  🔴 **排列照實體位置不照 slave 編號**（上左 s6／上右 s5／下左 s8／下右 s7）——
  照編號排會讓「左邊那顆吸不住」出現在畫面右邊，現場對照時會指錯。
- **張力：三條長條 + 紅色門檻標點**（per user）。左繩／右繩（滿刻度 `tension_max_kg`）、
  總和（滿刻度 `up_stop_total_kg`）。
  🔴 **標點位置是算出來的不是畫死的** —— 門檻是執行期可調值，寫死的刻度會在使用者
  改門檻的那一刻開始說謊（舊版「總和 / 130」就是這個毛病）。
  🟠 單側那兩條**額外畫一個橘點＝收繩停止**（比上限先跳的那道）
  ⇒ 看得到「**先撞到哪一道**」，而不只是「離最後一道多遠」。

### 驗證
- JS `node --check` 通過；CSS 大括號 186/186；`<div>` 159/159；
  重複 id 0；靜默寫入 0；`getElementById` 找不到 0；`.strip` 參照 0。
- 截圖（本次有拿到即時資料）四張卡全部正確：機器人畫在底部（H=−3）、
  3D 平板隨 roll/pitch 傾斜、四環顯示 −69/−66/−68/−68 全綠「4/4 密封」、
  張力三條各自帶紅點，左繩另有橘點（收繩停止 75 kg）。
- 部署後 HTTP 200。
- 🔴 **仍未經真人瀏覽器操作**；3D 的方向約定**未在實機歪斜時比對過**（目前機體近乎水平）。

## [2026-09-04j] 🖥 console v2：頁首四格收進 Dashboard；roll 補進 Manual 吊機卡

### Changed
- **頁首那條四格（姿態／位置／張力／裝置）由「四頁都固定顯示」收進 Dashboard**（per user，
  其他頁面因此乾淨）。Manual 少了約 90px 的固定頁首，卡片直接從抬頭下方開始。

### Added
- 🔴 **Manual 的吊機卡補上「姿態 roll」** —— **這是搬走之後唯一會真的消失的東西**。
  位置與張力 Manual 本來就有（吊機卡的「即時」與張力保護卡的四條進度條），
  只有 roll 沒有第二個地方看得到。
  而**手動單側收放繩實測約 −0.9 °/cm ⇒ 動 2 cm 就將近 2°**，
  這個數字必須就在那六顆按鈕旁邊，不能要人切分頁去看。
  顯示：目前值（|roll|>門檻 紅／>1° 橘／否則綠）＋「距中止 X.XX° · 規格 ±1°」。

### Fixed
- **roll 中止門檻 `6.0` 原本在 `paint()` 裡寫死兩次**（`/6.0` 與 `(6 - …)`），
  收斂成單一 `ROLL_TRIP`。
  ⚠️ **這只是把「要改的地方」從兩處變成一處，不算解決** —— 真正的來源是
  `cycle_test.py` 的 `sys.argv[4]` 預設值，腳本改參數而這裡沒改，畫面就會說謊。
  目前兩邊相符 ⇒ **是風險不是錯誤**，仍在待辦裡。

### 驗證
- JS `node --check` 通過；`<div>` 172/172；重複 id 0；靜默寫入不存在元素 0；
  全檔 `6.0` 寫死次數 0。
- 截圖確認：Manual 頁首已淨空、吊機卡出現姿態列；Dashboard 頂端出現該四格。
- 部署後 HTTP 200。

## [2026-09-04i] 🖥 console v2：Setting 也改成卡片版面，四個分頁自此一致

### Changed
- **Setting 由 `.setwrap` 裸版面 → `.man` + `.grp` 卡片**（摘要卡 + 全寬的參數對照表卡）。
  📌 **表格本身沒動** —— 這頁的資料就該是表格，硬拆成卡片反而難比對。
- 原本掛在頁尾的「這一頁為什麼長這樣」折進摘要卡的三列 `.ctl`。
  ⇒ **全檔 `<div class="band">` 說明區塊歸零**（原 17 塊），四頁的閱讀方式一致。

### Added
- **摘要卡：重啟存活統計**（`重啟後不可信 N · 已常數化 M · 編譯常數 K`，有 lost 就轉紅）。
  🔴 **直接數表格裡的 pill，不另外維護一份清單** —— 「同一件事寫在兩個地方」正是本專案
  吃過虧的形狀（表格改了、摘要沒改）。目前實測：**不可信 4 項**。
  📌 `pl-lost` 涵蓋「會遺失／預設已知錯／隨歸零而變」三種，共同點是**不能靠重啟後的值**。

### Fixed
- 🔴 **`.grp > header` 的 flex 擠壓問題修在根因，不再逐張補。**
  抬頭是 `flex + space-between`，右側徽章一長，左邊標題就被擠成一行一兩個字的直排。
  **今天踩了兩次**（「裝置 · `crane dev_*` 旗標」、「重啟後還剩什麼」）。
  ✅ CSS 加兩條：標題 `flex:1 1 auto;min-width:0`（吃剩餘空間、可換行）、
  徽章 `flex:0 1 auto` + `ellipsis`（可縮但不換行）。
  📌 **第二次踩到同一個形狀就該修根因** —— 前一次只把那張的標題包成 `<span>`，
  那是治標，所以第二張又中。

### 驗證
- JS `node --check` 通過；CSS 大括號 167/167；`<div>` 171/171；
  重複 id 0；靜默寫入不存在元素 0；`getElementById` 找不到 0；`.band` 區塊 0。
- 截圖確認抬頭回到單行、統計數字正確（4 / 3 / 1）。
- 部署後 md5 與 repo 逐位元一致、HTTP 200。

## [2026-09-04h] 🖥 console v2：接上 `en=`，並把 `arm_status` 解析改成對欄位新增免疫

### Added
- **手臂狀態接上 `en=`**（上游 09-04 於 `arm_status` 每段尾巴新增）。
  `en=0` ⇒ 該顆馬達不再送 CAN frame，**`pos`／`tau`／`err` 全部是凍結的舊值**。
  整列改標「🔴 凍結（en=0）」+ 橘色（**不給綠色**），tooltip 說明那些數字不是現況。
  📌 判準：**顯示一個看起來正常的舊值，比顯示「不知道」危險得多** —— PARK 之後最常踩。

### Fixed
- 🔴 **`arm_status` 的解析原本是「整串欄位依序寫死」的正則，改成分段 + 通用 k=v。**
  上游這次把 `en=` 加在每段**尾巴**，`err=(\S+)` 剛好吃得下所以沒炸；
  **加在中間就會整段比對失敗、面板顯示「解析失敗」** —— 而那看起來像通訊問題，
  除錯的人會去查網路，不會想到是對方加了一個欄位。
  ✅ 現在先切 `[M1]`/`[M2]` 段、段內通用掃描，欄位增刪都不影響。
  以三種輸入驗過：現行真實回覆、舊版（無 `en`）、以及故意把 `en=` 插在中間的假資料。
  📌 **通則：新增欄位是相容的改動，但「依序寫死的解析」會把它變成不相容。**

### 更正
- 🔴 **`armErr()` 註解裡「`0x1` 依協定是仍使能」那句是錯的，已刪。**
  上游收回了該推測 —— PARK 後讀到的 `err` **本身就是凍結值**，證明不了任何當下狀態。
  ⇒ **使能狀態一律看 `en=`，不從 `err` 反推**；`armErr()` 只負責故障分級。
  ℹ️ 實測現在 `err=0x1 en=1` 同時出現（機器在跑耐久測試），相關性看起來存在，
  但證據基礎既然被收回就不寫進判讀邏輯。

### 上游同日變更（不需本端改動，記著以免日後誤判）
- **`motor_api` 開機自動就緒**（上電 → M1 回零 → M2 到 RIGHT），**手臂會在行程啟動時自己移動**。
  本面板目前沒有「重啟 motor_api」之類的按鈕，若日後要加**必須標明這件事**。
- **M2 `hold_kp` 7 → 31**（保持力上限 1.88 N·m < 自身靜摩擦 2.0~2.3，導致「扳到哪留到哪」）。
  本面板只顯示 M2 角度、不顯示淨扭轉 ⇒ 只是**數值範圍會與舊紀錄不同**，邏輯不動。
- **PARK 本來就對稱**（`disable_slot(m2_)` 無條件執行）—— 先前「只 disable M1」的說法作廢。

### 後端缺口進度
- `zdt_pusher <slave> extend_raw <cm>`：上游確認可做（沿用 `pusher_move_many_`，不必第二套邏輯），
  **但安裝要等下一次脫附**（改本體主程式＝重啟＝`cmd_shutdown()` 關閥與幫浦）。
  UI 已標紅講明做不到，不會有人乾等。

## [2026-09-04g] 🖥 console v2：Dashboard / Mission 改用與 Manual 相同的卡片版面

> 範圍仍只有 `web_backend/public_v2/index.html`。三個分頁的版面自此一致。

### Changed
- **Dashboard 由 `.dash` 兩欄裸版面 → `.man` + `.grp` 卡片**（3 張：機器在哪裡／裝置／吸盤）。
- **Mission 由 `.cols` 三欄裸版面 → `.man` + `.grp` 卡片**（5 張：前置檢查／即時監看／
  致動器四態／吸盤／單步時間基準）。
- 兩頁原本**每欄底部各掛一段說明文字**，全部移除；內容沒有刪，是**折進各列的 `<small>`**。
  📌 判準：那些說明解釋的是「這個數字怎麼讀」——**放在數字旁邊比放在整欄底部有用**。
  例：「腳本中止 8cm 連續 3 筆 · 韌體守衛 10cm」現在就在「左右差」那一列底下。
- **`.band` 區塊由 17 → 1**（僅存的一塊是 Setting 頁的開場說明，該頁尚未整理）。
  順帶把 Manual 兩塊也清掉：張力條圖例折成一列 `.ctl`（綠/紅兩格），
  raw command 的說明收進抬頭徽章的 `title`。
- **Mission 的四態多加一格「手臂 DEPLOY_F」** —— `setVerif('armf')` 早就在寫，
  但 Mission 頁沒有對應的 `data-v="armf"` 元素，只有 Manual 有。
  （同一類「只有一半」的問題，這次是在新增時順手補齊。）

### Fixed
- **`.grp > header` 是 `flex + space-between`，標題拆成多個子元素會被拉開。**
  「裝置 · `crane dev_*` 旗標」原本渲染成三段各自貼邊。已包成單一 `<span>`。
  📌 通則：抬頭的**標題要當成一個元素**，右側徽章才是第二個元素。

### 驗證
- JS `node --check` 通過；`<div>` 166/166；重複 id 0；靜默寫入不存在元素 0；
  `setVerif` 目標全部存在。
- 截圖確認兩頁的卡片框、抬頭徽章、折入的小字都正確渲染。
- 部署後 md5 與 repo 逐位元一致、HTTP 200。
- ⚠️ 截圖裡吸盤格是空的 —— 那是 `--virtual-time-budget` 的取樣競爭（見 `[2026-09-04e]`），
  不是版面問題。

## [2026-09-04f] 🖥 console v2：推桿分側定位、上方張力條改成「離跳掉還有多遠」、快捷檔位

> 範圍仍只有 `web_backend/public_v2/index.html`。

### Added
- **ZDT 推桿：整組／右側 {5,7}／左側 {6,8} 各自有公分輸入**（per user）。
  後端 `pusher <group> extend_raw <cm>` 的 cm 是 2026-09-02 加的可選第三參數，
  群組只認 `all|feet|right|left`（`WASH_ROBOT.cpp:4593`）。範圍 0.5~20.0。
  🔴 **每側同時給「伸 raw」與「尋封伸」兩顆，因為它們是兩條不同的路**：
  `extend_raw <cm>` 走到指定脈衝就停，**不驗真空、不補伸、不重試**（位置你說了算）；
  `extend` 走 `disable_seal` 尋封序列，會**一路補伸找密封**（實測可達 47994 脈衝 ≈ 16cm）。
  只給一顆會逼人在「量不準」與「吸不住」之間二選一。尋封那顆帶二次確認並明講
  「不是你填的公分」。
- **吊機 `hold_hz` 快捷補到 10 / 20 / 30 / 40 / 50**（per user）。
- **風扇快捷改為 5 / 6 / 7 / 8 / 9 / 10 %**（per user）。
  📌 這六格**剛好就是驅動層硬鎖的全部範圍**（`QX_DO24` 占空比鎖 5~10%、頻率鎖 50Hz，
  兩者連動），所以不會有「按了被拒」的格子。5＝停止、7＝作業、10＝全速。

### Changed
- 🔴 **上方張力條由「總和 / 130」改成「離跳掉還有多遠」**（per user 要求把設定的保護
  顯示在進度條上）。兩個理由：
  ① **`130` 是寫死的**，而 `up_stop_total_kg` 執行期可調 —— 使用者把門檻改成 100，
     上面那條還在用 130 畫，**會低估風險**。
  ② **總和根本不是最容易先跳的那道。** 四道保護比的是不同的量，只畫總和等於漏看三道。
     改版當下實測：總和 67%、單側 51%、左右差 31%、**收繩 68%** ⇒ **先跳的是收繩**。
  現在條長＝四道裡最嚴那道的百分比，文字寫「最接近：<名稱> 51.4 / 75 kg（68%）」
  再列其餘三道。⚠️ **代價**：這條的刻度不再是固定 kg，而是百分比；
  絕對值仍在上方大字與下方文字裡，沒有藏起來。
- 左右差那條 gauge 同樣改吃執行期 `tension_diff_max_kg`，不再寫死 50。

### 後端缺口（已提給 C++ 那條線）
- 🔴 **單支推桿無法指定公分，這是後端限制不是介面偷懶**：
  `zdt_pusher <slave> <extend|retract>`（`command/dispatcher.cpp:393`）**只有兩個參數**、
  沒有 cm；而帶 cm 的 `pusher <group> extend_raw <cm>` 的 group 又不接受單一 slave。
  ⇒ 已在 UI 上標紅講明，並提出 `zdt_pusher <s> extend_raw <cm>` 的需求。
  ⚠️ 另外單支的「伸」走的是 **`extend` 尋封序列**、不是 raw，與上面三列語意不同，已標註。

### 驗證
- JS `node --check` 通過；`<div>` 168/168；CSS 大括號 165/165；
  重複 id 0、靜默寫入不存在元素 0、`getElementById` 找不到 0。
- 截圖確認三項都正確渲染，且上方張力條確實指出「收繩」是最接近的那道。
- 部署後 md5 與 repo 逐位元一致、HTTP 200。
- 🔴 **按鈕仍未經真人按下驗證**（截圖只證明畫得出來，不證明按下去對）。

## [2026-09-04e] 🖥 console v2：戶外亮色、Manual 版面重排、四個「靜默沒作用」的元素

> 範圍仍只有 `web_backend/public_v2/index.html`。

### Fixed
- 🔴 **`.ctl` 的標籤欄會被壓成 0 寬 → 文字變成一行一個字的直排。**
  `grid-template-columns:1fr auto auto` 配 `.cl{min-width:0}`，卡片只有約 200px 寬，
  右邊控制項一多（輸入框＋三顆鈕）`1fr` 就塌掉。**這是實際截圖才看見的**，
  讀 CSS 看不出來。✅ `min-width:110px` 當地板 + 新增 `.ctl.stack`
  （標籤一行、控制項下一行並允許換行）給控制項多的列用。
- 🔴 **四個「只有一半」的元素，全部靜默沒作用**（`txt()`/`bar()` 找不到元素就 return
  —— **沒有例外、沒有 console 訊息**，看起來只像功能沒做）：
  | id | 狀況 |
  |---|---|
  | `cr-teng` / `cr-tenb` | 張力進度條的繪製程式碼一直都在，**HTML 從沒建過那個元素** |
  | `cr-ten` / `cr-tenr` | 同上 |
  | `rd-railen` | 上滑台使能：只有 markup、沒有繪製 |
  | `rd-balen` | 平衡迴路狀態：只有繪製、沒有 markup |
  ✅ 已補寫一支檢查掃全檔（比對 `txt`/`bar`/`setVerif` 的目標與實際 id、並找重複 id），
  現在全檔 0 個。**這類錯誤只能靠工具抓，不能靠讀。**
- 🔴 **重複 id `rd-railen`（我自己做出來的）** —— `getElementById` 只拿第一個，
  狀態永遠更新不到。同一支檢查抓到的。
- 🔴🔴 **別的瀏覽器的回覆會跑進來把我們的指令解掉。**
  `server.js` 的 `broadcast()` 送給**所有** WebSocket client，而文字協定沒有 request id
  ⇒ 兩個瀏覽器同時開著就會互相吃回覆（A 的 `status` 被 B 的 `relay_status` 回覆解掉
  → `last.wr` 裝進繼電器欄位）。
  🔬 **證據**：一個完全沒送過指令的 client 收到了 `pwm status` 與 `rail_pos` 的回覆。
  ✅ 每道指令帶「預期回覆長相」（`send(target,cmd,ms,expect)`），對不上就不 resolve。
  ⚠️ **緩解不是根治**：兩個 client 送同一道指令仍分不出來（但那時答案相同，後果小）。
  真正的解法是後端配對 id 或每 client 一條橋接。

### Changed
- 🌞 **預設改為亮色，且不再跟隨 `prefers-color-scheme`**（per user：戶外使用）。
  深色底在直射陽光下幾乎讀不到，而**裝置的系統主題跟「現在在不在太陽底下」無關**。
  深色保留給室內 bench／夜間，改由頂欄按鈕明確切換，選擇存 localStorage，
  並在 `<head>` 以一小段 script 先套用（否則會先畫一次預設色再被改掉）。
  📌 **亮色配色本來就在**（`:root` 第一組），只是掛在 dark media query 之下 ⇒
  誰的系統設深色誰就看到深色。這不是「沒做亮色」，是「亮色被系統設定蓋掉」。
- 🔆 **戶外可讀性：`--ink3` 由 `#7C8D92` 調深到 `#5E6E73`。**
  前者在 `#EDF1F2` 底上對比僅約 **3.3:1**，陽光下小字整段消失；調深後約 **5.3:1**
  （WCAG AA 小字門檻 4.5:1）。`--ink`／`--ink2`／`--line`／`--line2` 一併加深。
- **移除全部 9 塊卡片備註**（per user）。其中四條「不知道就會用錯」的折進各列既有的
  `<small>`：hold_hz「放開再按才生效 · 重啟回 10」、M1「PARK 後為凍結值」、
  DEPLOY/DEPLOY_F 的單位、風扇「看回讀不看回傳」。
- **上滑台卡依 per user 重排**：上半部純狀態（位置＋讀取時刻／馬達使能／座標可信度／
  行程守衛），下半部才是按鈕。

### Added
- **吊機 `hold_hz` 可調**（`set_hold_hz`，1–120）。左側顯示**機器上的實際值**、
  值為編譯預設 `10` 時標成「未設定過」的橘色。快捷 10／20。
  ⚠️ **實際預設是 10 不是 20** —— `VFD_HOLD_HZ_DEFAULT = 10.0`（`Crane_control_PI/main.cpp:276`），
  `CLAUDE.md` 寫的「hold 預設 20Hz」是過期的。所以輸入框預填 20 但**不假裝那是現值**。
  📌 平衡迴路的修正幅度是 `base ± 5 Hz` 的**固定絕對值**（刻意設計成不隨 base 變），
  base 由 10 提到 20 ⇒ **絕對修正力不變、相對修正力減半**。
- **張力保護四條進度條**：單側／左右差／總和／收繩停止。綠＝未達門檻、紅＝已超過。
  🔴 四條**各比不同的量**，語意逐條查過原始碼（`main.cpp:1777/1780/2222/2963`）——
  單側比 `max(左,右)`、左右差比 `|左−右|`、總和比 `左+右`**且只在拉繩時作用**、
  收繩停止比 `max(左,右)`。混用會給出錯的顏色。
- **上滑台馬達使能狀態**。🔴 標示為**推論值**不是回讀：後端沒有任何指令吐得出它
  （`rail_pos` 只回 `rail_cm`）。driver 有 `read_status()`/`read_di1_function()`
  （`DM2J_RS570.h:95,101`）但沒接出來。⚠️ 另有一層不確定：DI1 若是「常閉且未接線」，
  訊號恆觸發 ⇒ **馬達永遠使能、`Pr0.07=0` 也關不掉**，那時「已送出失能」與「實際失能」
  完全不同而畫面分辨不出來。
- **平衡迴路狀態**（`balance_enabled` + `balance_source`）。`source=meter` 標橘色
  ——那是重啟後的預設值，而現行要用的是 `imu`。
- **分頁寫進網址**（`#manual` 等）：重新整理留在原分頁、可貼連結、也讓自動截圖能指定分頁。
- **頂欄建置版號**（前一批加入）＋**亮／暗切換鈕**。

### 工具
- **自動截圖**：Windows Chrome headless → `D:\Desktop\agent_ai\.tmp\*.png`（WSL 端讀
  `/mnt/agent_ai/.tmp/`），不必在機器人 Pi 上跑瀏覽器。
  🔴 **`--virtual-time-budget` 有個會誤導的副作用**：它把計時器快轉，
  **前端佇列的 8–12 秒逾時會立刻到期**，於是每道指令都被判逾時、畫面停在「等待 washrobot」。
  ⇒ **那不是 GUI 的 bug，是截圖工具的假象**（我一度誤判成跨 client 互吃回覆並據此下結論，
  後來用 `--dump-dom` 比對 crane 有值／washrobot 沒值才推翻）。
  📌 要看到真實資料就多截幾次或改用真實等待；CDP 那條路在 WSL→Windows 之間要繞網段，
  試過不划算。

### 驗證
- JS 兩段 `node --check` 均通過；CSS 大括號 165/165；`<div>` 167/167；
  重複 id 0；靜默寫入不存在元素 0。
- 實際截圖三輪逐項比對版面（這批的版面問題**全部是截圖才發現的**）。
- 部署後 md5 與 repo 逐位元一致、HTTP 200。
- 🔴 **仍未經真人瀏覽器操作**：按鈕按下去的行為、觸控目標大小、實際陽光下的可讀性，
  都還沒有人驗過。亮色是依原理選的（對比比色相重要），**不是在太陽底下量過的**。

## [2026-09-04d] 🔴 接觸後改推 `hold_pos`：消掉 `DEPLOY_F` 尋觸的加載/卸載振盪

> 範圍：`cleaning_arm/main_api.{cpp,h}`、`Linux_test/arm_cycle.py`。

### Fixed
- 🔴 **`DEPLOY_F` 尋觸期間，手臂會反覆「推一下、放掉、推更用力、又放掉」。**
  per user 現場摸出來的，原話：「手臂靠上之後 M1 有在抖動，類似重試」
  →「重試壓在牆上的力道的感覺」。**50Hz DIAG 實測證實，而且描述精確**
  （`m1_diag.csv`，2594 筆）：
  ```
  t(s)   cmd       pos       tau
   7.6  +0.6321   +0.5755    +0.24
   8.0  +0.6011   +0.5701    -3.17   ← 命令值往回退
   8.4  +0.6581   +0.5831    -0.05
   8.8  +0.6169   +0.5716    -1.61   ← 又往回退
   9.2  +0.6700   +0.5861    +2.10
   9.6  +0.6244   +0.5736    -1.90   ← 又往回退
  ```
  命令值在 0.60~0.68 之間來回、tau 在 −1.9~+3.5 振盪、約 **0.8 Hz**。
  **根因**：`press_probe_` 每步呼叫 `move_to_slot()`，而它第一件事是把 ramp 起點
  設成**實際位置**（`s.move_cur = Get_Position()`）。手臂被玻璃擋住後 pos 不再前進，
  於是每一步的命令值都先塌回實際位置再爬到新目標 —— 在 `kp=90` 之下是
  **−5 N·m 的階躍，力量被完全卸掉再重新加載**。走 6~10 步就振盪 6~10 次。
  ✅ 修法：新增 `press_hold_step_()`，接觸區的每一階直接寫 `hold_pos`，
  讓 hold 迴圈用 `kp*(hold_pos − pos)` 推，命令值單調上升、中間不卸力。
  夾限（`upper_bound`／`SETWALL`／`lower_bound`）與 `move_to_slot` **逐條相同** ——
  不因為換了一條路就繞過安全限。
  🔴 **自由行程那一大段仍走 `press_probe_`/`move_to_slot`**：hold 分支**沒有**
  move 分支的速度安全閥（`M1_VEL_SAFETY_LIMIT`），大距離移動必須留在有保護的那條。
  接觸區的階距只有 0.010 rad × kp90 = 0.9 N·m，沒有失控空間。

  | | 修正前 | 修正後 |
  |---|---|---|
  | `cmd` 往回退（>0.003 rad）次數 | **8** | **1**（在接觸前，無害） |
  | 接觸後 `cmd` | 0.60↔0.68 來回 | **固定 0.8657** |
  | 壓上耗時 | 15.8s | **10.6s** |

### Notes
- ⚠️ **我稍早把 `SEEK_SPEED` 0.05→0.15 時的成因寫錯了一半。** 當時寫「ramp 距離是
  `(cmd − pos)` 且越走越長」—— 距離是對的，但**沒看出力會被完全卸掉**，
  所以那個改動只讓振盪變快、沒有消除它。**先量再改**這條又應驗一次。
- **排除的候選**：hold 迴圈有個 passive watchdog，觸發條件之一
  `|hold_pos − live_pos| > 0.1` 在壓住時恆為 0.244、**永遠成立**，觸發會
  `dm_->enable()`（阻塞 ~100ms）—— 形狀跟「重試」一模一樣。
  **但今天五份 arm log 實測 0 次觸發**，不是它。查 log 只花幾秒，比改任何東西都便宜。
- **穩態一直是乾淨的**：壓住後 8 秒 tau 只在 14.90~14.99、pos 只在 0.6205~0.6209 —— 
  那是編碼器解析度不是抖動。**問題只在尋觸階段。**

### Changed
- `arm_cycle.py` 支援力控模式：第二參數改為**目標 N·m**（第五參數傳 `wall` 可退回開迴路）。
  壓上後**另外獨立量一次姿態**與回覆裡的 tau 交叉比對，差 >1.0 N·m 就標記 ——
  驗證「回覆說的」與「事後獨立量到的」一致。

## [2026-09-04c] 🔴 keepalive 的回覆會冒充成指令回覆；手臂面板加代轉路徑

> 範圍仍只有 `web_backend/public_v2/index.html`。`server.js` 未改（見下方「未做的根治」）。

### Fixed
- 🔴🔴 **後端 keepalive 的回覆會把在途指令解掉 —— 既有 bug，實機實證。**
  `server.js` 每 10 秒對**每一條**橋接送 `ping`（`BRIDGE_PING_MS`），而 `sock.on('data')`
  是**無差別** broadcast 每一行給瀏覽器的，沒有濾掉自己送的。
  **實測掛在 WS 上 25 秒、一道指令都沒送，固定每 10 秒飄進 4 行、全部長得像回覆：**
  ```
   +9.9s  src=crane      OK pong                              ← crane 兩行：
   +9.9s  src=crane      OK pong                                 crane 與 crane_intr
   +9.9s  src=washrobot  OK pong                                 都掛 src='crane'
   +9.9s  src=arm        ERR usage: M1 <cmd> or M2 <cmd>      ← motor_api 不認得 ping
  ```
  佇列只認「下一則 OK/ERR 就是回覆」（文字協定沒有 request id）⇒ **只要 keepalive 落在
  某道指令的在途窗口內，那道指令就拿到它當答案**，真正的回覆隨後抵達時 `qq.busy`
  已是 null，被當成事件丟掉。
  📌 `server.js` 註解說 arm 那個 ERR「harmless (visible in browser log)」——
  **在有佇列的前端這句話不成立**：`ERR` 本來就在回覆前綴名單裡，它會把在途指令解成「失敗」。
  📌 **1 秒輪詢撞上的機率低但不是零；真正致命的是長指令** ——
  代轉 `arm_deploy_f` 冷啟動 44 秒以上，期間必飄過 4 次以上 keepalive ⇒ 幾乎必然被解錯。
  ⚠️ **這不是手臂面板造成的**，面板只是讓它從「偶爾閃一下」變成「必然發生」。
  ✅ 前端擋法：認出三個 keepalive 簽章，**在途指令不是 `ping` 時就不當回覆**。
  撞到在途指令的那次**單獨記一行紅字**（罕見且重要）；閒置飄過的那些**不進紀錄**
  （每分鐘 20 行以上會把事件淹掉）。
- 🔴 **`\[M1\]` 也要算回覆前綴（washrobot 這一側今天就存在的坑）。**
  本體 `:5001` 的 `arm_status` 是 `return arm_cmd_("STATUS", 3) + "\n"`
  ——**原樣轉發、沒有包 OK**（`WASH_ROBOT.cpp:989`）。
  ⇒ 任何人在 raw command 用 target=washrobot 打 `arm_status`，今天就會等到 12 秒假逾時。

### Added
- **手臂面板加「走哪條路」選擇：直連 `:9527` ／ 經本體代轉 `:5001`。**
  上游 09-04 已安裝 `arm_deploy_f` 代轉，兩條路徑都可用。
  📌 **`cycle_test.py` 走的正是代轉那條 ⇒ 要驗「實際生產路徑」就得選它**，
  直連驗得再乾淨也不能替它背書。
  📌 代轉在 `DEPLOY_F` 之前會補送 `M1/M2 ENABLE`（PARK 停用馬達後不先 ENABLE 會靜默失敗），
  **直連沒有這一步** ⇒「直連失敗、代轉成功」是可能的，且不代表哪條壞掉。
- 🔴 **逾時規則寫進程式：前端逾時必須大於後端逾時。**
  否則後端真正的錯誤訊息會被前端自己的 `ERR local_timeout` 蓋掉 ——
  **與本檔 `[2026-09-04a]` 那個 WARN bug 是同一種形狀**：畫面說逾時，答案在別的地方。
  後端 `cmd_arm_deploy_f` 是 120 秒（`WASH_ROBOT.cpp:975`）⇒ 代轉路徑取 **150 秒**；
  直連無伺服端逾時，取 90 秒為上界（冷啟動尋觸實測 **44 秒**，不能設小）。

### Changed
- Dashboard 吸盤說明改寫：原寫「真空建立需 1.2–2.9 秒」，09-04 實測
  **`extend_raw` 回 OK 當下讀到 −40（已在 −50 判準之外），約 4 秒後才穩到 −66**
  ⇒ 明寫「指令回 OK 的那一刻不是判斷的時候」。
- 手臂面板補一句單位警告：`DEPLOY` 第一參數是 **mm**（≈520）、`DEPLOY_F` 是 **N·m**（≈15），
  代轉的指令名只差一個字、值域差 35 倍且**送錯不會有語法錯誤**。
  介面刻意用兩個獨立輸入框（各帶值域）而不是共用一個，讓這種混淆在 UI 上發生不了。

### 未做的根治（留給決定）
- 🔴 **keepalive 這件事的正解在 `server.js`**：橋接自己知道剛送了 `ping`，應該把配對的
  那則回覆吃掉不要 broadcast —— 那樣連 8080 一起修好，也不必在前端維護簽章表。
  **沒有動的理由**：`server.js` 是 8080 與 8081 **共用**的，8080 是現行生產介面。
  📌 **8080 目前不受這個 bug 影響**：v1 的 `app.js` 是依內容解析、不做「回覆↔在途指令」配對
  （`public/app.js:61` 的 onmessage），所以 keepalive 對它只是雜訊。
  ⇒ 也就是說根治只對 v2 有實益，卻要動到 8080 的路徑上。

### 驗證
- `node --check` 通過；`<div>` 160/160。
- **keepalive 現象為實機實測**（掛 WS 25 秒、零指令，如上方原文）。
- **代轉回覆格式為讀碼確認**：`cmd_arm_status()` / `cmd_arm_deploy_f()`（`WASH_ROBOT.cpp:959,989`）
  與 dispatcher 指令名（`command/dispatcher.cpp:157-177`）。
- 部署後 md5 與 repo 一致、HTTP 200。
- 🔴 **仍未經瀏覽器實測**；**「攔下 keepalive 冒充」那條紅字也還沒真的觸發過**
  （要它出現得剛好撞上在途指令）。

## [2026-09-04b] 🎯 `DEPLOY_F` 力控貼合：壓力變成被控量，`wall_mm` 退場

> 範圍：`cleaning_arm/`（motor_api）、`app/WASH_ROBOT.*`、`command/dispatcher.cpp`、
> `Linux_test/`。**現行 `DEPLOY` 一個字都沒動** —— 兩條路徑並行以便 A/B，
> 沿用 console v2 起在 8081 與 8080 並行的做法。

### Added
- 🎯 **`DEPLOY_F <target_nm> <LEFT|CENTER|RIGHT> [theta_min] [theta_max]`**（`main_api.cpp`）。
  **為什麼需要它**：現行 `DEPLOY` 是開迴路 —— 由 `wall_mm` 算 `theta_target` 推過去，
  壓力只是「推不動之後殘餘角度誤差 × kp」的**副產品**。牆一變遠壓力就靜默地掉，
  而 09-03 高度 229 的 `DEPLOY 520` 只壓出 **6.01 N·m**（同日其他高度 14.31），
  **log 裡沒有任何一行說壓力不足**。
  流程：`prepare_touch_slot_()` → 退到起點 → 尋觸（每步 +0.010 rad 到 tau ≥ 2.0）
  → 割線法 `theta += (target − tau) / kp_eff` → 鬆弛複驗。
  🔴 **`kp_eff` 是就地量出來的，不是常數**，所以整套對下列各項免疫：
  重力前饋（此姿態約 −6.9 N·m）、靜摩擦前饋、以及「命令 0.8668、實際停 0.6205、
  誤差 × `hold_kp`(90) = 22.2 但實測 15.09」這段**至今沒解釋清楚的落差**。
  實測 `kp_eff` 68~71，落在同日三點預測的 64~74 —— **不是 90**，因為接觸後手臂
  仍會被壓進去一點，吸收掉一部分角度差。
- 🔴 **兩個守衛（per user）**：`theta_contact > theta_max` → `no_wall`；
  `theta_contact < theta_min` → `obstacle`。**這兩個限是手臂幾何、不隨高度變** ——
  這正是它取代 `wall_mm` 的原因。它們把已經發生過的兩件事變成程式看得見的：
  高度 229 的少壓、以及 09-03「吸不到」查明是橫桿。
- **`arm_deploy_f <target_nm> <slot>`** 代轉（dispatcher + `cmd_arm_deploy_f`）。
  逾時放大到 120s（`DEPLOY` 是 30s）—— 尋觸與收斂要時間，用 30s 會在中途逾時。
  🔴 **刻意不做 `verify_arm_deploy_`**：那支拿「`wall_mm` 推算的預期 θ」比對實際 θ，
  而 `DEPLOY_F` 的整個前提就是不預設 θ 在哪。障礙判斷已由兩個守衛做掉，
  在這裡再套一次舊幾何檢查等於把剛拆掉的假設又裝回來。
- **回覆帶 `iters=N`**（per facade web gui session）。迭代次數原本只在 stdout，
  而 `server.js` 只橋接 TCP ⇒ **stdout 永遠到不了 GUI**，少了這個欄位 GUI 在結構上
  就不可能顯示收斂過程。目前恆為 1 —— **恆為 1 才是它便宜的地方：
  哪天不是 1，就是幾何或等效剛度變了的最早訊號。**

### Changed
- **`cycle_test.py` ③b 的判準整條換掉**，不是調數字。舊判準（`0.55 < θ < 0.75`
  且 `tau > 8.0`）存在的唯一原因，是 `arm_deploy` 壓玻璃時本來就回 `ERR`、
  **回覆不帶任何資訊**，只好回頭讀姿態反推。`DEPLOY_F` 的回覆本身就是結論，
  而且比事後讀姿態可信：事後 `arm_status` 會凍結（馬達失能就不再送 CAN 幀），
  `sleep(1.5)` 又讀在鬆弛中途（實測壓上後 1 秒內掉 1.46~2.15 N·m）。
  四種回覆三種行為：`OK` 掃動／`WARN` 掃動並計數／`no_wall` **跳過掃動、計數、續行**／
  `obstacle` **中止**。🔴 前兩者的分野跟真空「吸不到就繼續走」同源；
  頂著障礙物再橫走滑台會弄壞東西。
  📌 **順帶修掉一個既存中止點**：舊判準在高度 229 會直接 `bail()`（6.01 < 8.0），
  症狀會長得像手臂故障。十週期跑到頂端附近就會踩到。
- **尋觸提速 `SEEK_SPEED` 0.05 → 0.15**：實測每步花 1.4s 而非預估的 0.4s，
  因為 `move_to_slot()` 是**從實際位置**起 ramp —— 一旦接觸，pos 停住而 cmd 持續外推，
  每步的 ramp 距離是 `(cmd − pos)` 且越走越長。
- **暖啟動錨點改用「接觸當下的命令值」而非接觸角度**（實測差 0.10 rad：
  `contact=0.5858` 而當時 `cmd=0.6854`）。第一版拿 θ 當錨點，等於每次都從還差
  10 步的地方重新摸起 ⇒ **完全沒省到時間（36s vs 44s）**。
  🔴 起點若已受力（牆變近）**退回冷啟動重來**，不可就地當接觸點 —— 那會用一個
  已經受力的位置當基準，兩個守衛都失效。
  合計 44s → 冷 24s／暖 15s（`DEPLOY` 為 8s）。

### Fixed
- **`walltest.py` / `arm_cycle.py` 的 `ask()` 預設 `prefixes` 補 `WARN`。**
  `DEPLOY_F` 未收斂時前綴是 `WARN`，漏了它回覆會被當成非同步事件、
  指令永遠等不到 resolve ⇒ 卡到逾時，**症狀長得像「手臂沒回應」**。
  （由 facade web gui session 在自己的 transport 上發現同型 bug 後提醒。）

### Refactored
- **Step1 + Step2 + Step2.5 由 `cmd_deploy_sequence` 原地抽成 `prepare_touch_slot_()`**，
  `DEPLOY` 與 `DEPLOY_F` 共用。**行為逐行相同、無邏輯改動。**
  理由：複製一份會重演本 repo 吃過虧的形狀（`cyc10`/`cyc20` 內容相同的兩份、
  滾筒繼電器三份複本只改到一份）。

### Verified
- 參數驗證 5/5 全部擋下（無動作）；守衛 A 上限壓到 0.55 → `no_wall`（最後 tau=−4.25，
  懸空值由重力前饋主導）；守衛 B 下限拉到 0.62 → `obstacle`（接觸於 0.5838）。
- 正常 8 次全部收斂：tau 14.70~15.38 / θ 0.6205~0.6212 / kp_eff 68.4~71.6 / `iters=1`。
- 🎯 **最終 θ = 0.6205 與 `DEPLOY 520` 完全相同**，且直連 :9527 與經代轉 :5001
  兩條路徑各自重現 ⇒ 幾何推算與力量量測找到同一個物理接觸點，三次獨立驗證。

### Known gaps
- ⚠️ **`WARN` 分支只有讀碼驗證，硬體上從未觸發過** —— 割線法一步就進 ±1.0 N·m 容差。
- ⚠️ **控制的是馬達回報的 tau，不是玻璃受力。** 兩者差一個隨姿態變的重力項
  （θ=0.6205 時 −6.90、θ=0.7078 時 −8.14）⇒ 不同高度維持同樣 tau，
  實際壓在玻璃上的力仍差約 1.2 N·m（~8%）。**不是恆力控制。**
- 🔴 **`theta_min`/`theta_max` 的預設值 0.45/0.95 是保守占位**，沒有跨高度實測支撐。
  已知玻璃本身就佔掉 θ 0.6205~0.7078（約 38mm），凸出量小於這個範圍的障礙物分不出來。

---

## [2026-09-04a] 🖥 console v2：手臂面板 + 兩個讓回覆卡到逾時的傳輸層 bug

> 範圍僅 `web_backend/public_v2/index.html`（console v2，8081）。
> **`server.js`、`public/`（8080 現行 GUI）、任何 C++ 都未改動。**

### Fixed
- 🔴 **`WARN` 開頭的回覆不被認得 → 卡到 12 秒本地逾時，回一個假的 `ERR local_timeout`。**
  transport 用 `/^(OK|ERR)/` 判定「這行是不是某道指令的回覆」，而 `DEPLOY_F`
  未收斂時前綴是 `WARN`。後果不是「少顯示一個狀態」，是**真正的答案滾進通訊紀錄、
  畫面上卻說逾時** —— 操作者會去查一個不存在的連線問題。
  📌 改之前掃過全 repo：只有 `cleaning_arm/main_api.cpp:3123`（DEPLOY_F 那行）會吐
  **行首**是 `WARN` 的行；其餘四處（`[attach] WARN`、`[... calibrate] WARN`）
  都以 `[` 開頭，本來就歸為事件，不受影響。
- 🔴 **同型第二個：`arm` 這條線上每一行都不是 `OK`/`ERR` 開頭，`STATUS` 也會卡逾時。**
  `STATUS` 回的是 `[M1] pos=... | [M2] pos=...`。
  ✅ **改為按 target 分規則**：`arm` 一律視為回覆，`washrobot`/`crane` 維持前綴判定。
  這不是放寬，是實際協定 —— `main_api.cpp` **全檔只有一個 `::send()`**（:2441），
  就在 client_thread 收到一行、算出 `dispatch()` 回覆之後那一處，
  **沒有第二個地方會往這條 socket 寫東西**（＝零非同步推送）。
  📌 `walltest.py` 早就撞過同一件事，它是用 `prefixes=("[M1]",)` 繞開的。
- **Dashboard 行進速率顯示的是較差的那次量測**：原寫「下行 56.0 / 上行 8.1 / 來回 64 s」
  ＝ 09-03 中午 **n=1**；同日收盤已有 **10 週期**的 54.7 / 7.9 / **62.6**。
  已改為 10 週期值，並在兩組數字後面標註樣本來源（單步組成仍只有 n=1 那份，照實標）。

### Added
- **Manual 頁新增「手臂 M1 / M2」面板**（第 8 張卡，緊接上滑台 —— 滑台載著手臂）。
  在此之前 **console v2 完全沒有手臂控制面**，只能靠 raw command。
  - `DEPLOY_F <Nm> <slot>` 力控貼合，四態顯示「實測 / 目標」+ 明細
    （`theta` / `cmd` / `contact` / `kp_eff` / `iters`）
  - `DEPLOY <mm> <slot>` 開迴路保留作 A/B 對照（上游刻意兩條路徑並行）
  - `PARK` / `INIT` / `STATUS`（按需讀取）
  - 🔴 **走 `target='arm'` ＝ 直連 motor_api `:9527`，不經本體主程式** ——
    因此不受「本體 `arm_deploy_f` 代轉尚未安裝」影響，也**不需要重啟主程式**
    （重啟會跑 `cmd_shutdown()` 關真空閥與幫浦＝機體會脫附）。
  - 🔴 指令用 motor_api 自己那套**大寫**名（`INIT`/`DEPLOY`/`DEPLOY_F`/`PARK`/`STATUS`），
    不是 washrobot 代轉的小寫 `arm_*` —— 走 `:9527` 時後者不存在。
- **`PARK` 比照 `STOP`，在 Manual 被鎖定時仍可按。** 理由同源：它是把手臂控制權
  拿回來的唯一手段。壓著玻璃是持續施力，達妙馬達長時間受力會觸發過熱／過流鎖存，
  而那只能斷電解除。鎖掉卸力鈕的代價比誤觸大得多。
- **頂欄加建置版號**（`YYYY.MM.DD-HHMM` 台灣時間）。靜態檔會被瀏覽器快取，
  **「改了沒生效」與「改錯了」在畫面上長得一模一樣**，沒有版號分不出來。

### 設計決定（都對應一次真實誤判）
- **`STATUS` 不進輪詢迴圈，改按需讀取並標出讀取時刻。**
  M1 失能後就不再送 CAN 幀，`STATUS` 讀到的是**凍結的快取**（09-04 實測連讀三次
  逐字元完全相同）。每秒刷一個不會變的舊值，看起來像「活的」—— 比不顯示更糟。
- **`tau` 負值不判成故障。** 懸空時回報值由重力前饋主導（該姿態約 −6.9 Nm），
  `no_wall` 那次最後 tau 是 −4.25。負值＝沒碰到東西。四態的紅點表示的是
  **指令沒達成**，明細用中性色說明原因，不讓人以為馬達壞了。
- **錯誤碼健康值是字串 `"0"` 不是 `OK`** —— 端到端實測才抓到（我原本寫 `=== 'OK'`，
  會把健康的手臂標成橘色）。`err_name()` 只把 0x8~0xE 印成具名碼，0x0 印成 `"0"`，
  其餘落 default 印成 `0x1` 這種原始十六進位。因此分三級：
  `0` 正常 ／ 具名碼才算故障 ／ 其餘標「狀態碼，非具名故障」
  （PARK 後 M2 常見的 `0x1` 依協定是「仍使能」，不是壞了）。
- **兩條貼合路徑都會回 `ERR ... did not converge` 卻其實成功** —— 壓在玻璃上本來就
  到不了位置目標。面板明寫「判準看 tau，不看回傳字串」，並且 `DEPLOY` 一律回頭讀
  `STATUS` 用姿態判斷，不以回傳字串定成敗。

### 驗證
- `node --check` 抽出的 script：通過；`<div>` 開閉 159/159；卡片 7 → 8。
- **解析邏輯以實機真實回覆逐條驗過**（不需瀏覽器）：`STATUS` 原文、`DEPLOY_F` 的
  OK / WARN / ERR 三種格式。WARN 尾巴那句中文不是 k=v，確認**沒有洩進** `fields()`，
  另以獨立正則取出顯示。
- **端到端唯讀實測**：瀏覽器 WebSocket → `server.js` → motor_api `:9527`
  下 `STATUS` 有正常回覆（`{"washrobot":true,"crane":true,"arm":true}`）。
- 部署後 `md5sum` 與 repo 逐位元一致、HTTP 200、版號與新面板都服務得出來。
- 🔴 **尚未經瀏覽器實測**（Claude 端無瀏覽器）——按鈕實際點下去的行為、版面在
  真實視窗寬度下的表現，都還沒有人看過。

### 檔案
- `web_backend/public_v2/index.html`
- Pi 上的部署：`user@192.168.5.25:~/bringup/web/public_v2/index.html`
  （舊版備份 `index.html.bak-0904-1041`，回退就是把它複製回去，不必重啟 node）

## [2026-09-03a] 📋 09-03 程式改動補登（事後由 git 重建，非當日撰寫）

> ⚠️ **這筆是 2026-09-04 補的**，內容由 `270f2ee` / `1a65387` / `844691f` 三個 commit
> 與當日 work_log 重建。**當日沒有寫 changelog** —— 斷檔由 facade web gui session
> 在 09-04 指出。列在這裡是為了讓帳本連續，細節仍以那三個 commit 為準。

### Fixed
- **`cmd_arm_deploy` 先送 `M1/M2 ENABLE`**：`PARK` 會停用兩顆馬達，停用狀態下
  `DEPLOY` 被 motor_api 擋掉 ⇒「`arm_park` 之後 `arm_deploy`」這個很自然的順序
  **靜默失敗**（指令送得出去、log 印得出、回傳 ERR，但手臂一動也不動）。
  `cyc.py` 一直能跑只因為它自己補了 ENABLE ⇒ 缺陷在編排層。
- **`cleanup()` 最前面補 `arm_park`**：③b 整合清潔動作後，任何在「壓上之後、
  收手臂之前」的中止都會讓手臂留著壓玻璃（實測 6~14 N·m 持續頂著）。
  `bail()` 的「現場保留」對吊機與推桿是刻意的，**對手臂不成立**。
- **`ask()` 支援自訂回覆前綴**：`arm_status` 回 `[M1] ...`，只認 `OK`/`ERR` 會等到
  20 秒逾時，症狀長得像「手臂沒回應」。
- **`cmd_pump` 改走 `pqw_set_relay_verified_()`**（回讀 + 最多 3 次重試）：
  09-02 曾出現 `pump on` 回 OK 而連續四次 `relay_status` 讀到 `ch2=0`。
- **`pwm` 三處補強**：restart 狀態閘門、control 降級、FC0x06 少寫一筆。

### Changed
- **QX-DO24 移到獨立匯流排 `192.168.1.21`**（per user 新增網關）。先前六次長測有四次
  死在 `.22`，且掛掉時是**整條一起掛**（QX:9 與 JC100 5/6/7 同時逾時）。
  分開後風扇 20/20 正常、兩輪長測零復發 ⇒ 問題在 `.22` 那一側，不在模組。
- **座標第二次重定義**（per user）：SD76 改在玻璃最底端歸零 ⇒ 一律用「離底高度」
  （頂 231 / 底 0）。🔴 沿用舊碼會把 `L=0` 讀成「在頂端」然後往下放繩 200cm。
- **③b 由「滑台空跑 0→50→0」改成完整清潔動作**：`arm_deploy` → `rail 0→100→0` → `arm_park`。
- **機上四支測試腳本收進版控**（`Linux_test/`）。

---

## [2026-09-02j] 🎯 M1 起步頓挫與 M2 定位：兩個被遺留的舊參數

### Added
- **`M1 DIAG ON` / `M1 DIAG OFF`**：在控制迴圈內以 ~45-50Hz 記錄 M1+M2 的
  `t,pos,vel,tau,cmd,move_act,hold_en` 到 `m1_diag.csv`。純記錄，不改控制行為。
  🔴 **記錄點必須放在 `if (!s->enabled) continue;` 之前** ——
  `lr_move_to_slot_impl` / `go_home_slot` / `lr_calibrate_slot` 進場都會
  `enabled.exchange(false)` 自行控制，放在檢查之後會**完全錄不到那些斜坡段**。
  這三行的位置就是本次兩個成因得以查明的關鍵。
- **`LR_SLOT` 守衛：M1 未離開玻璃（pos > 0.20）時拒絕切換工具**（per user）。
  工具頭貼玻璃橫轉會刮傷；DEPLOY 走 `lr_move_to_slot_impl` 不受影響。

### Fixed
- 🎯 **`touch_wall_slot` 的失效偵測探測 ±0.3 → ±0.05 rad —— 這是「M1 起步頓一下」的元兇。**
  `0.3 x hold_kp(90) = 27 Nm` 持續 60ms，在每次移動之前；而它只需確認 `|tau| > 0.3 Nm`
  （0.05 x 90 = 4.5 Nm，門檻的 15 倍）。
  📌 `go_home_slot` 裡一模一樣的探測早已改成 0.05，**此處被落下**；本檔註解甚至寫著
  「±1.0 rad probe … produced a visible jerk right before every move」。
  **效果：起步 max|tau| 18.7 → 3.4（降 5.5 倍）、max|vel| 1.74 → 0.20（降 8.7 倍），4 次一致。**
- 🎯 **`lr_move_to_slot_impl` 的 `CONV_TOL` 0.15 → 0.02 —— M2「到不了目標」的真正原因。**
  斜坡進到目標 ±0.15(8.6°) 就宣告到位並停止推進；50Hz 波形顯示它是**平順自行減速停下**、
  HOLD 接手只需 0.6 Nm。⇒ 當日「M2 摩擦飽和 ~2 Nm」「位置受限」全是此容差造成的假象
  （把 `hold_kp` 7→20、力矩 x2.4，位置紋風不動）。
  📌 06-09 放寬的兩個前提今天都已失效：① 「LEFT 只能到 ±0.5」是 `lower_bound=-0.8`
  夾死造成的（09-02 已修）；② 「sweep 精度要求不高」——`TOOL_EXT_*` 現在依賴 M2 到位。
  **效果：落點誤差 0.073 → 0.011 rad（4.3° → 0.6°），耗時不變，四次全部真收斂。**
  ✅ 壓力複驗 θ 0.6525→0.6548、tau 11.77→11.48，仍在 per user 接受範圍 ⇒ TOOL_EXT 不需重做。
- `cmd_deploy_sequence`：touch_wall 前等 **M1+M2 同時靜止**（連續 4 次 |vel|<0.05，上限 1.5s），
  未達成則記錄。⚠️ 這是在**錯誤診斷**（誤以為衝擊來自 M2 到位）下加的，但保留——
  不在手臂仍移動時開始新命令本身合理。

### Changed
- `ARM_RAIL_TRAVEL_MAX_CM` 140 → **130**（per user，在手動歸零、零點確定在左端硬限位之後）。
- 滑台手動歸零流程首次實際使用並驗證可行。

### Notes — ❌ 本輪試過並還原
- `HOLD_I_MAX` 2.0→5.0：**無效**，瓶頸是積分累積速率（繞到上限要 71 秒）不是上限值。
- `m2_.hold_kp` 7→20：力矩 x2.4 而位置不動 —— 正是這個否決**推翻了「摩擦飽和」模型**，
  間接指向真正的 `CONV_TOL`。
- 🔴 **M2 掃動中被橫向摩擦帶著跑 0.39~0.42 rad（≈24°）仍未解**，且收緊容差後幅度變大。

## [2026-09-02i] 滾筒繼電器順序；三個「加速」嘗試全部否決

### Fixed
- 🔴 **滾筒繼電器改到 DEPLOY 之前開**（per user：「滾筒靠上前要開滾筒繼電器」）。
  原本是先把滾筒壓上玻璃再起轉——靜止滾筒頂著玻璃起轉，清洗段頭幾公分等於乾磨。
  📌 `do_step_sync_rail_sweep_`（生產實際跑的那條）**08-28 就已改對**；
  被落下的是 `do_arm_clean_sweep_` 與 `do_arm_clean_sweep_continuous_` 兩份複本，
  而程式碼裡就寫著「這段序列跟那邊是複製關係，**兩處必須一起改**」。
- 🔴 **乾掃路徑滾筒關閉點 gate 由 `deployed` 改為 `init_ok`、abort 出口改無條件關**。
  原本 DEPLOY RIGHT 失敗 ⇒ `deployed=false` ⇒ 整段含 DEPLOY LEFT 被跳過、滾筒沒人關。
  **這正是 08-28 per user 回報「從頭到尾都是 DEPLOY RIGHT 沒換」的成因**，當時只修了步伐路徑。

### Notes — ❌ 三個「加速」嘗試，全部量測不支持、已還原
per user 回報「手臂靠上去跟切換工具太慢」。基準：工具切換各約 11s
（分段：M1 收回 2.5s / M2 換 slot 2.3s / TOUCHWALL 指令 0.1s 非阻塞）。

1. **`use_park_profile` true→false**（DEPLOY 的收回）：9.7/12.3/11.8 vs 11.4/11.0，
   同散布內無改善。且該行是 07-27 per user 明確要求，我憑假設翻掉 → 還原。
2. **`M1_VEL_SAFETY_LIMIT` 0.4→1.0 試跑**：命令 0.30 的實際峰值就有 **0.4335（2.2 倍）**
   ——**09-02 的重力修正並未消除超速**；0.70 時落點誤差 0.073 已逼近 DEPLOY 容差 0.08；
   只省 0.6s／11s。→ 還原為 0.4。
3. **接觸停滯偵測 + ramp step×3**：8.0/12.1/11.8 無改善，tau 由 ~12 升到 13.5~13.8。→ 移除。

🔴 **真因是黏滑（stick-slip）**：DEPLOY 伸出段速度在 0~0.31 間反覆跳動，
0.50→0.69 rad 花 2.6s、**平均僅 0.07 rad/s 而命令是 0.3** ⇒ 手臂沒在跟隨斜坡，
加快斜坡自然無效。per user 描述的「M1 會震一下」即此。
`M1_FRICTION_TAU=2.5` 不足（程式碼他處記載某些角度靜摩擦 ≥4.6 Nm）。

⚠️ **靜摩擦量測失敗**：0.02 rad/s 慢掃全程黏著（當日日誌早就寫著「太慢會停在 stick-slip」）；
更根本的是 TCP 輪詢僅 ~10-20 Hz、控制迴圈 50 Hz，**取樣追不上黏滑週期**。
⇒ 需在 motor_api 內以迴圈速率記錄才量得準。未做。

## [2026-09-02h] 上滑台：手動指令、軟體失能、行程 140cm、速度與斜坡調整

### Added
- **`rail <target_cm> [rpm] [acc] [dec]`**（絕對定位，0=左端、正向往右）、
  **`rail_pos`**、**`rail_zero`**、**`rail_enable <on|off>`**、
  **`rail_cfg_soft_enable`**（一次性驅動器組態）。
  ⇒ 解掉日誌標了整天的前置：「無指令可讀位置或設零點 ⇒ 只能跑完整 `cmd_init`，而它會關真空閥」。
- 驅動層：`set_di1_function()` / `read_di1_function()`（Pr4.02 / 0x0145）。

### Fixed
- 🔴 **讓伺服真的能軟體失能**：`Pr4.02` DI1 功能 `0x88`(使能+**常閉**) → **`0x08`**(使能+**常開**)。
  DI1 未接線 + 常閉 ⇒ 訊號恆觸發 ⇒ **原本沒有任何軟體失能方式**。改常開後未接線=未觸發，
  使能完全由 `Pr0.07` 決定。連同 `Pr0.07=1` 一起存 EEPROM（`0x1901=0x5555`），
  斷電重啟後回讀 Pr4.02 仍為 8 ＝ 持久化確認。per user 實測手推得動。
  📌 舊版 header 曾寫 `motor_disable() → 0x1801=0x2233`，而 `0x2233` 實為**參數恢復出廠值**
  （2026-04-24 已更正）——先前「試過用指令關閉使能」送出的其實是出廠重設。
- 🔴 **`ARM_RAIL_TRAVEL_MAX_CM` 48 → 140**（config + header）。舊值 provenance 寫
  「實體行程 50cm」，與 per user 實測的 140 差近三倍，來源不明。⚠️ per user 指定上限即 140，
  **不留安全餘裕**（與先前 50→48 留 2cm 的慣例不同）。
- **`rail_move` 逾時改為依行程計算**：原用 `dm2j_wait_done_` 預設 20000ms，
  而 0→140 @50RPM 需 20.6s ⇒ 成功卻回 ERR。改為 `(|Δcm|/線速度 + 斜坡) × 2 + 5s`，上限 180s。
- **`rail_move` 回傳判斷寫反**：`dm2j_wait_done_` 遵循專案慣例（無異常回 false），
  初版 `if (!...)` 等於成功才回 ERR。
- **dispatcher 把預設 rpm `250` 寫死**，註解卻宣稱沿用 `ARM_SWEEP_RPM`；
  改常數成 400 後仍回 250。已改為傳 0 表示沿用預設，由成員函式解析（與 acc/dec 一致）。

### Changed
- `ARM_SWEEP_RPM` / `DM2J_ARM_STEP_SWEEP_RPM`：250 → **400**（per user 實機逐段試 100/250/350/400）
- `ARM_SWEEP_ACC/DEC` / `DM2J_ARM_STEP_SWEEP_ACC/DEC`：100 → **3000**
  （單位 ms/1000rpm；舊值在實際轉速下等於瞬間起停，100 @250RPM 僅 25ms）
- `cmd_rail_pos` 補上介面說明：**這是指令脈衝計數，不是量測值**——失能手推讀數不變、失步偵測不到。

### Notes
- ✅ **全行程帶滾筒掃動驗證**（推桿 10cm、四吸盤密封、滾筒壓玻璃、400RPM）：
  0→140→0 各 3.9s，**四顆吸盤全程零變化**（−66/−67/−65/−66）。
- 📌 順帶量到**玻璃不平行**：140 端手臂 θ +0.0126、壓力 −1.4 Nm ⇒ 該端玻璃遠約 **5.9mm**。
- ⚠️ **400 RPM 的失步未驗**（已知 250 在用、500 累積 0.2~0.3mm/橫越）。未主動安排驗證：
  08-31 per user 已否決「跑多趟找 RPM 上限」。
- 手冊 `DM2J-RS.V1.pdf` 已從 `tmp/` 搬到 `doc/上滑台/`（`tmp/` 不進雲端鏡像，第三次踩同一個坑）。

## [2026-09-02g] M2 定位改為停點相對；修掉會毀掉零點的陳舊偏移門檻

### Fixed
- 🔴 **`cmd_init_sequence` 的 M2 陳舊偏移守衛 `abs(pos) > 1.5f` → `> 3.0f`。**
  註解本來就寫「beyond 3 rad is stale」，程式碼卻判 1.5 —— 兩者不一致；而註解裡
  「Physical travel is ~±0.76 rad」的前提也是錯的：實測正向停點 +0.7204、負向走到
  −1.2831 仍未到底，跨距至少 2.0 rad。⇒ **M2 停在合法位置（如 −1.6）時，INIT 會判定
  「陳舊」並就地 set_zero**，把零點設到任意位置。這是唯一會讓 09-02 手轉實測的
  `M2_SLOT_*_RAD` 絕對值作廢的路徑，已關閉。
- **`LR_CALIBRATE` 丟棄 `lr_calibrate_slot()` 的回傳值、失敗也回 `OK`** →
  改回 `ERR LR_CALIBRATE: stop not found or did not converge`。
  與 08-29 修過的 `trigger_sync_move()` 同一類缺陷；實測 Phase 1B abort 時已正確回 ERR。
- `cmd_init_sequence` 標頭的過期註解「LR_CALIBRATE RIGHT」→ 實際是 `seek_left=true`（正向）。

### Added
- **M2 slot 目標改以正向機械停點為基準**（`M2_SLOT_LEFT/RIGHT/CENTER_FROM_STOP`）。
  `lr_calibrate_slot` 的 Phase 1 成功即寫入 `lr_stop_pos`/`lr_stop_valid`（Phase 1B
  之後失敗仍保留）；若真的走到 `set_zero` 則同步 `lr_stop_pos -= midpoint`；
  `lr_stop_valid=false` 時退回絕對值常數。
  ⇒ **零點即使被重設，跑一次校正即可自動還原 slot 目標。**
  停點重現性：兩次量測 0.7204 / 0.7208（差 0.0004 rad）。
  驗證：`LR_SLOT RIGHT` 落 0.4557（目標 0.5320）、`LEFT` 落 −0.9337（目標 −1.0111），
  左右對稱短 0.076/0.077 ＝ 已知的 M2 摩擦飽和，非定位問題。

### Notes
- ⚠️ **只用正向那一個停點**：實測負向在 2 rad 內沒有停點
  （`max_travel = 2*ZERO_OFFSET + 0.4 = 2.0` 比真實行程還小），
  「兩停點取中點」的舊模型對這個機構不成立。
- ❌ **試過並否決**：M1 斜坡參考領先量夾制（`M1_RAMP_MAX_LEAD_RAD`）——
  三次量測 0.4335 / 0.5067 / 0.4823，散布 ±0.04 蓋過差距，**實驗無結論**，已還原。
  當時據單次比較宣稱「變更糟」是過度推論，已收回。詳見 `main_api.h` 該處註解。

## [2026-09-02f] cleaning_arm 重力模型重擬 + go_home 安全閥 + wall_mm 定案

### Fixed
- **`cleaning_arm/main_api.h`：`M1_GRAVITY_K` 20.87 → 16.09**（振幅高估 30%）、
  `M1_GRAVITY_PHASE_RAD` 3.317 → 3.3190。由 **12 點雙向慢掃**（0.42~0.64 rad、0.15 rad/s、
  往返 3 趟、`G=−(T_out+T_in)/2` 消摩擦）擬合，殘差 RMS 0.163 Nm。
  舊值出處註明只用「兩個外側乾淨點」，靜態量測混入了 0.39~1.86 Nm 的摩擦。
  ⇒ **自由平衡下垂由 0.0695 rad 降到 0.0025~0.0060（12~28 倍），`arm_deploy` 首次回 `OK`。**
- **`cleaning_arm/main_api.cpp`：`go_home_slot` 的 ramp 迴圈補上速度安全閥**。
  該段因 `s.enabled=false` 而被 `feedback_loop` 跳過 ⇒ 原本整段無保護。
  實測失控速度達 1.51~2.23 rad/s（限制 0.4 的 3.8~5.6 倍）。
  煞車用 kp=0 + kd=5.0 並**保留重力前饋**，事後重錨定 `cur_cmd`。
- **`app/WASH_ROBOT.h`：`ARM_M1_LENGTH_MM` 320 → 490**，補上與 `cleaning_arm` 那份的互相標註。
  （當日臂長修正只改了手臂側，本體這份是我製造的分岔。）

### Changed
- **`ARM_WALL_MM_DEFAULT` 530 → 520**：`arm_deploy 520` → θ=0.6750、tau=+18.90，
  與使用者認可的貼合姿態差 0.0019 rad ≈ 0.9mm。
- `M1_VEL_SAFETY_LIMIT` / `M1_EMERGENCY_BRAKE_KD` 由區域常數提升為檔案範圍共用。

### Notes
- 🔴 **`verify_arm_deploy_` 自 2026-06-06 起就是無條件 `return false`**，障礙偵測三個月沒在跑。
  仍不能直接打開：壓玻璃時命令角與實際角的 0.29 rad 落差**是壓力來源**，
  需改為比對「校正過的預期接觸角」。未做。
- 📌 修正一個當日稍早的錯誤說法：**`wall_mm` 超量命令不是誤用**——
  `TOUCHWALL` 是對可壓縮接觸下的位置命令，壓力必然來自位置誤差
  （設成幾何真值 388 時 tau 僅 +0.34 Nm ＝ 輕觸）。

## [2026-09-02e] Claude Code — 馬達自報的錯誤碼一直被丟掉（純觀測，不改控制行為）

### 修改檔案
- `user_lib/damiao.h`
  - `Motor` 新增 `state_err` + `Get_err()`；`receive_data()` 加第四個參數 `uint8_t err = 0`
    （**帶預設值＝累加式改動**，既有呼叫點不受影響，見 CLAUDE.md 的介面契約節）
  - MIT 回授解析取出 `data[0] >> 4`，兩個分支都傳入
  - `CMD == 0xEE`（通訊錯誤）由 `{ /* code */ }` 空殼改為實際輸出錯誤碼
  - 新增 `#include <iostream>`（診斷輸出需要）
- `cleaning_arm/main_api.cpp`
  - 新增 file-local `err_name()`：把錯誤碼翻成 `A:OVER_CURRENT` 這類可讀字串
  - `STATUS` 兩顆馬達各加 `err=` 欄位

### 原因

`cleaning_arm` 的 **「M1 突然 passive」自 2026-08-17 起原因未明**，程式碼只能靠
「`tau < 0.3 Nm` 而位置誤差擴大」**間接推斷**，並為此做了 re-enable 看門狗
（"Video evidence showed M1 going fully passive mid-HOLD"）。

🔴 **而馬達其實每一幀都在報自己的狀態，只是沒有人接。** damiao 的 MIT 回授幀
`data[0] = (ERR<<4) | ID`，解析程式碼只取 `q/dq/tau`，`data[0]` 僅在
`canId == 0x00` 的分支用 `& 0x0f` 取 ID —— **`>> 4` 的錯誤碼那半從未被讀取**，
`Motor` 也沒有對應欄位。錯誤碼定義**就寫在同一個檔案** `CAN_Receive_Frame` 的註解裡：

```
8 超壓 / 9 欠壓 / A 過流 / B MOS過溫 / C 線圈過溫 / D 通訊丟失 / E 過載
```

其中 **A / B / C / E 正好涵蓋「持續高扭力導致保護鎖住」** 這個假說 ——
2026-09-02 的 DEPLOY 中 M1 的 `tau` 曾持續在 −4.2 ~ −5.1 Nm 抵抗重力數秒。
而同檔對 M2 的同類現象早有記載：「damiao M2 sometimes latches into passive state
after being held against a stop with high torque（over-current/thermal → fault →
MIT frames "ACK" but no torque applied）」。

第二條通道同樣被吞：`CMD == 0x01/0x02/0xEE` 三個分支**全是空殼**。

### 設計決定
- **純觀測**：不動 MIT 命令、增益、軌跡或任何控制邏輯，只把已經收到的位元組解出來。
- **`receive_data` 用預設參數而非改簽名** —— 累加式改動，不動既有呼叫點。
- **`err_name()` 輸出可讀字串**而非裸數字：`err=A` 沒人看得懂，`err=A:OVER_CURRENT` 才有用。
- 🔴 **註解明確警告 `err=0` 不等於健康**：它只代表「這一幀沒報錯」。若 passive 的成因
  不是馬達自報的保護動作（CAN 幀遺失、韌體層靜默失效），這裡會恆為 0。
  **不可把 `err=0` 當成「一切正常」的證據** —— 那會變成今天已經踩過四次的
  「看起來正常」假象的第五個。

### ✅ 驗證狀態
- 本機 `g++ -fsyntax-only` 通過；Pi 上建置成功並部署
- `STATUS` 實測輸出 `err=0`（兩顆閒置健康）
- 🔴 **尚未被真實故障觸發過** —— 要等下次 DEPLOY 讓 M1 再度 passive 才知道
  它到底是「馬達自報的保護」還是「其他機制」。**兩種結果都有價值，但這個改動
  只保證看得到前者。**

### 部署
`~/bringup/motor_api` 已更新。舊版留四份：`motor_api.old_e3c8820`（= main HEAD）、
`motor_api.prev_0902`、`motor_api.maxloops_only`、`motor_api.probefix`。

---

## [2026-09-02d] Claude Code — `lr_move_to_slot_impl` 的 passive 探針把 M2 踹過頭（短距離移動落點差 0.14 rad）

### 修改檔案
- `cleaning_arm/main_api.cpp` `lr_move_to_slot_impl()`
  - passive 探針偏移量 **`±1.0` → `±0.05` rad**
  - 探針區塊末尾**無條件刷新 `cur_cmd`**（原本只在 passive 分支內刷新）

### 症狀
M2 的 slot 移動在**移動距離極短**時，落點固定偏離目標約 0.14 rad，
而且因為 `CONV_TOL = 0.15`，**兩次都剛好卡在容許內、回報 `(converged)`**。
2026-09-02 現場四筆：

| start | target | 探針方向 | 落點 | 誤差 |
|---|---|---|---|---|
| +0.0090 | 0 | −0.991（負） | **−0.1444** | 0.144 🔴 |
| −0.0013 | 0 | +0.999（正） | **+0.1421** | 0.142 🔴 |
| −0.1692 | 0 | 正（朝目標） | −0.0261 | 0.026 ✅ |
| +0.1329 | 0 | 負（朝目標） | +0.0090 | 0.009 ✅ |

**兩次過衝的方向與探針方向完全一致**，長距離則正常。
下游後果：`DEPLOY` 之後 M2 停在 −0.119、`tau≈2.0` 由 hold 迴圈硬拉，根因即在此。

### 機制
探針原本把位置命令設在 `cur_cmd ± 1.0 rad`，以 `MIT_KP=31` 驅動 60ms
（≈31 Nm 命令扭力，實際飽和輸出）—— 註解自稱 "light frames"，**但它不輕**。
方向恆為「越過目標」的那一側。

- **長距離**：探針正好朝目標，那 60ms 被後續多秒軌跡吸收 → 無感
- **短距離**：目標就在腳邊，探針把馬達踹到目標另一側；而 creep 段只有
  0.20 rad/s（每步 0.004 rad），在 `MAX_LOOPS` 內拉不回來

**而 `cur_cmd` 只在 passive 分支刷新** ⇒ 馬達活著的正常情況下，
ramp 從一個探針前的舊起點開始，與馬達實際位置脫鉤。

### 🔴 這兩道修正 `go_home_slot` 早在 2026-08-14 就做了，只是沒有擴散過來
同一個檔案裡的同名探針，該處註解逐字寫著：
- 「縮小成 0.05 rad…配合 kp/kd 仍足夠產生超過 `TAU_LIVE_THRESHOLD` 的扭力差異，
  但實際造成的移動量小很多」
- 「the probe (whether or not it triggered re-enable) may have moved the motor;
  starting the ramp from a stale reference **is exactly what caused the erratic-move bug**」

📌 **本次不是新設計，是把已驗證的修法補到漏掉的第二個呼叫點。**

### 為什麼 0.05 仍足以判定 passive
`MIT_KP=31 × 0.05 = 1.55 Nm`，遠高於 `TAU_LIVE_THRESHOLD=0.3`；
活著的馬達必然超過，passive 的仍然是 ~0。（與 `go_home_slot` 的推理一致。）

### ✅ 實機驗證（2026-09-02）
部署後以 `arm_init` 觸發一次 0.035 rad 的短距離移動（正是發作區間）：

```
[M2 lr_move_to_slot] Done.  pos=0.0338  target=0.0000  start=0.0338 (converged)
```

**過衝 0**（修正前同類移動是 0.14）。且 `start` 欄現在與馬達實際位置相符，不再是舊值。

⚠️ **殘留（非迴歸）**：落點 0.0338 而非 0 —— `err < CONV_TOL(0.15)` 時摩擦前饋不介入，
`kp×0.034 ≈ 1.1 Nm` 與靜摩擦相當推不動，剩餘偏移由 hold 迴圈積分（`ki=0.6`，上限 1.2 Nm）
慢慢收斂（同日觀察到 −0.0231 → −0.0097 即此機制）。

### 部署
`~/bringup/motor_api` 已重建部署。舊版留三份：`motor_api.old_e3c8820`（= main HEAD）、
`motor_api.prev_0902`、`motor_api.maxloops_only`。

---

## [2026-09-02c] Claude Code — QX-DO24：FC 0x06 頻率退路 + 模組重啟命令

### 修改檔案
- `user_lib/QX_DO24.{h,cpp}`
  - `setPWM_Freq()`：FC `0x10` 失敗且 `freq ≤ 65535` 時，改走**兩筆 FC `0x06` 單寫**
    （先寫高位 `addr`=0、再寫低位 `addr+1`）。走到退路一律 `LOG_WRN`
  - 新增私有 `writeSingleReg_(addr, value)` —— FC `0x06`，供退路與重啟命令共用
  - 新增 `restartModule()` —— reg `0xFF00` 寫 `0x0001`（重啟設備）
- `app/WASH_ROBOT.h` / `app/wash_robot_commands.cpp`：新增 `cmd_pwm_restart()`
- `command/dispatcher.cpp`：新增 `pwm restart`

### 原因
2026-09-02 現場 `pwm set` 連續失敗 **0/22**，全部掛在 `pwm_freq_write_failed`。
`setChannel()` 的三個寫入裡**只有 `setPWM_Freq` 用 FC `0x10`（請求 13 bytes）**，
另外兩支（`setPWM_Duty`/`setPWM_Control`）都是 FC `0x06`（8 bytes）——
而它在第一步就 `return false`，**後面兩個短幀根本沒被測到**。
同時期短幀的**讀取**偶爾成功 ⇒ 判斷是「長幀送不進模組」。

手冊（`.claude/summaries/QX_DO24_MODBUS_SUMMARY.md` P4）明寫：
「若頻率低於 65535，則保持 `0x04` 的值為 0，只改 `0x05` 即可」
⇒ 本專案鎖 50Hz，可用兩筆 8-byte 單寫取代一筆 13-byte。

### 設計決定
- **退路不是主路。** FC `0x10` 一筆交易寫完兩個暫存器是原子的；拆兩筆會有
  「高位已清零、低位還是舊值」的中間態。健康的匯流排上沒有理由放棄它。
- **寫入順序：先高位後低位。** 反過來的中間態是「新低位 + 舊高位」＝可能極大的錯誤頻率；
  這個順序的中間態只會是舊低位，有界。
- 🔴 **走到退路一律 `LOG_WRN`** —— 它是**症狀不是功能**，不能讓它靜默把硬體故障蓋掉。
- **`restartModule()` 不接受參數，值寫死 `0x0001`** —— 同一個暫存器寫 `0xFFFF` 是
  **恢復出廠設置**（地址→1、波特率→9600，模組直接從 `.22` 消失）。
  兩個值差一個位元組、後果天差地遠，**寫死才讓誤送在型別上不可能發生**。
- **`cmd_pwm_restart` 回傳分三種而非 OK/ERR 兩種**：模組收到重啟後很可能來不及回應那一幀，
  把「沒有回覆」判成失敗會誤導。改為送出後等 2s 重讀版本號，用「回不回得了話」判定，
  並在訊息中分開標示 `acked=`（那一幀有沒有回）與最終結論。

### 🔴 驗證狀態
- ✅ 本機 `g++ -fsyntax-only` 三個檔全過；Pi 上 16 TU 重建 + 連結成功；已部署（`prev8` 留存）
- 🔴🔴 **退路一次都沒被觸發過。** 部署後 QX-DO24 恰好自行恢復（讀 8/8、寫 15/15），
  FC `0x10` 直接成功 ⇒ **本次改動與該故障的排除無關，不可宣稱它有效。**
- 🔴 **`pwm restart` 未測。** 刻意不在硬體剛恢復時拿未驗證的重啟命令去戳它。

---

## [2026-09-02b] Claude Code — JC100 回覆結構檢查：拆開「CRC error」，並修掉例外回覆被當成功的缺陷

### 修改檔案
- `user_lib/JC_100_METER.cpp` `send_command()`
  - CRC 比對之前加入四道結構檢查，順序為 **例外 → 位址 → 功能碼 → 長度 → CRC**
  - 失敗訊息統一改為 `<KIND> (a/b) len=N head=XX XX XX XX (連續失敗 N 次)`
  - 新增 `expect_len`：FC `0x03` = `5 + 2×暫存器數`（本驅動一律讀 1 個 ⇒ 7）、FC `0x06` = 8（回應原幀）

### 原因

**① 一句「CRC error」蓋住三種病因。** 2026-09-02 `JC100:7` 間歇 CRC error，
但「CRC 對不上」可能是：真雜訊／**回覆被切成兩段只收到前半**／前一筆交易的遲到回覆。
三者處置完全不同（查接地與終端／改 USR 網關 `_pt`／查交易同步），而驅動分不出來。
🔴 **這直接卡住一條既有待辦**：CLAUDE.md 網關設定表記著 `_pt=0` 是「回覆被切成兩個 TCP 段」
的結構性根源，待辦寫著「目前量到的失敗是 no reply 不是 too short，**在沒有證據指向分片前
不動共用設定**」—— 而 `SHORT_FRAME(len=5,expect=7)` 正是那個證據該長的樣子。
📌 hex dump 要開 driver debug，而 `WR_DRIVER_DEBUG` **只在啟動時讀一次**
⇒ 「下次再發生要有證據」等於「必須事先就開著跑」。所以訊息一律附前 4 個位元組，
**不開 debug 也判得出來**。

**② 🔴 Modbus 例外回覆被當成成功（既有缺陷，非本次引入）。**
例外回覆是 `[id][func|0x80][code][crc][crc]` ＝ **正好 5 bytes**（通過 `len < 5` 那道）
**且 CRC 正確**（通過 CRC 比對）→ `read_pressure()` 取 `r[3]`/`r[4]`，
而那是 **CRC 的兩個位元組**，被當成壓力值往上傳。
**裝置明確回報錯誤，上層卻收到一個看起來正常的數字。**
與 CLAUDE.md 記載的 QX-DO24 `err 0x7C` 同一類（撞號時 JC100 的回覆被 PWM driver 撿走）。
⚠️ 而壓力值是放腳的判準之一 —— 這條路徑上的假數字有實體後果。

### 設計決定
- **結構檢查排在 CRC 之前。** 截斷的幀 CRC 一定也對不上，但報 `SHORT_FRAME(len=5,expect=7)`
  比報 `CRC` 有用得多。代價：若剛好是位址那個位元組被雜訊打壞會報成 `ADDR_MISMATCH`
  → 以「訊息附上前 4 個位元組」補償，不必開 debug 就判得出來。
- **不改回傳語意**：所有新分支一律 `return true`（本專案慣例 true=失敗），
  節流邏輯沿用既有的 `!_fast_fail_noted || probe`，與原 CRC 分支一致。
- **行為改變只有一處且是修正**：例外回覆由「回成功＋垃圾值」改為「回失敗」，
  上層 `read_pressure()` 會走既有的 `return _last_pressure` 降級路徑。

### 驗證狀態
- ✅ 本機 `g++ -fsyntax-only -std=c++17 -Icommon -Itransport -Iuser_lib` 通過
- 🔴 **尚未建置、尚未部署、尚未實機驗證** —— 改動當下本體 Pi 正在跑 10 週期測試，
  刻意不在測試進行中換執行檔。跑完後才建置。
- 🔴 **新訊息尚未被任何真實故障觸發過** —— 它能不能真的分辨那三種病因，
  要等 `JC100:7` 下次再犯才知道。

---

## [2026-09-02a] Claude Code — `cycle_test.py` 兩道守衛：輸出可觀測性 + 真空源前置檢查

### 修改檔案
- `Linux_test/cycle_test.py`
  - `import os`（新的 `ALLOW_NO_PUMP` 環境變數要用）
  - 檔頭加 `sys.stdout.reconfigure(line_buffering=True)`
  - 前置檢查新增第三道：開跑前送 `relay_status` 回讀真空幫浦 A 組，OFF 就擋下不跑；
    以 `ALLOW_NO_PUMP=1` 明示放行時，標題列印【無真空對照組】警告

### 原因（兩件事在同一天各咬了一口）

**① 全緩衝讓 20 分鐘的測試全程沒有可觀測性。**
stdout 導向檔案是全緩衝，而本腳本整輪產出才約 4.7KB —— 一個 4KB 緩衝區都填不滿，
log 從頭到尾停在 0 bytes。2026-09-02 上午就這樣誤判過一輪：測試明明在跑（pid 在、機器在動），
log 卻是空的。這與 `runbook.md` §A4 對兩支 C++ 記過的 `stdbuf -oL` 是同一個坑。
📌 **設在腳本裡而不是靠呼叫端記得加 `python3 -u`** —— 呼叫端會忘，2026-09-02 就忘了。
⚠️ 這不影響「會不會留下紀錄」（中止路徑本來就會在解譯器結束時 flush），
影響的是「跑的當下看不看得到」。

**② 既有兩道前置檢查，沒有一道碰得到「真空源在不在」。**
2026-09-02 上午整輪 10 週期是在**沒有真空幫浦**的情況下跑完的。成因是一條職責分界：
開幫浦的是 `init` 這支 **TCP 指令**（`cmd_init` 送 `controlRelay(CH_PUMP, true)`，
印 `[init] PQW relays → pump ON`），**不是**程式啟動時的驅動 `init()`
——後者底下那五行 relay 設定是註解掉的（`app/WASH_ROBOT.cpp:359-363`）。
本腳本不送 `init`，而 `state=idle` 在幫浦沒開時照樣成立
⇒ 「起點在頂端」與「本體 ready/idle」兩道檢查**都不會發現**。

🔴 續十已經寫過「程式重啟後所有繼電器都是 OFF，『泵浦運行期間常開』只在 `init` 跑過之後成立」。
**那是給人看的紀錄，不是給腳本看的守衛** —— 寫下來的知識沒有變成程式碼裡的檢查，
就會在下一次重開機之後原地復發。這次還多疊了一層：當天 Pi 整台重開過。

### 設計決定
- **擋下來，不自動補送 `init`。** 與既有的「起點不在頂端」同一套判準：不猜。
  自動送 `init` 等於在操作者不知情的狀態下啟動幫浦。
- **通道編號從 `relay_status` 自己的 `names` 欄推導，不寫死 `2`。**
  CH3 那次（2026-09-01 續十）的教訓就是通道對應會變，而寫死的數字不會跟著變。
- **保留 `ALLOW_NO_PUMP=1` 的明示放行**：無真空對照組是有價值的實驗
  （可以隔離「左右差惡化」與吸附有沒有關係），但必須是刻意的、而且要印在報表標題上，
  不能是忘了開幫浦。

### 尚未做
- 🔴 本次兩處改動**只在本機工作區**，尚未同步到吊機 Pi 的 `~/bringup/cycle_test.py`
  （當下那份正在跑，刻意不動）。下次上機前要 rsync 並複驗。

---

## [2026-09-01b] Claude Code — `fine_adjust` 水平參考偏移 + 吊機冷啟動驗證

### 修改檔案
- `Crane_control_PI/main.cpp`
  - 新增 `g_fine_adjust_level_diff_cm`（預設 **0 ＝ 與先前逐位元相同**）：
    `motion_fine_adjust_sync()` 的收斂目標由寫死的「左右讀值相等」改為 `L - R = level_diff`。
    做法是把左側讀值先減掉 `level_diff` 成 `curL_adj`，之後 `diff_init` / `target_cm` /
    `left_needs` / `L_distance` 全在「已對齊」座標裡比較；**只有 `L_stop_at` 要把
    `level_diff` 加回去**（收斂迴圈比的是原始 `curL`，漏掉這一項偏移會被抵銷＝等於沒設）。
  - 新增指令 `set_fine_adjust_level_diff <cm>`（範圍 ±20cm）與 status 欄位
    `fine_adjust_level_diff_cm`。範圍上限的理由：0.85°/cm 之下 20cm 已是 17°，
    比任何合理的水平偏移都大得多，超出幾乎必然是打錯或量錯。

### 原因
`fine_adjust` 在**每一次 `motion_rope` 的結尾都會跑**，而它原本把「左右讀值相等」當收斂目標。
那個目標與「機器水平」沒有定義好的關係，原因有兩層、彼此獨立：
① **重心偏左** —— 機器要水平，兩條繩就不等長（2026-09-01 實測，0.85°/cm）；
② **兩支 SD76 的零點各自獨立** —— 絕對讀值差裡混著一個與繩長無關的固定偏移
（08-31 靜止不動就差 13cm，`length_diff_max_cm` 那條守衛當天已改成比相對位移差，
**`fine_adjust` 沒有跟著改**）。
⇒ 先前收斂點接近水平只是現行零點偏移剛好抵銷；**任何一次計米器歸零都會靜默移動這個目標**。

🔴 **實測佐證這不是裝飾性修正**：重啟後 `L-R=3` 時 `raw_x=+0.91°`，
配上 0.85°/cm ⇒ `level_diff=0`（舊行為）會把機器收斂到約 **+3.5°**，遠出 ±1° 規格。

### 目前值與它的出處
執行期已設 **`level_diff=4`**，推導：現況 `L-R=3` @ `raw_x=+0.91°`（IMU 基準乾淨，
`roll=0.89` vs `raw_x=0.91` ⇒ 偏移僅 0.02°），要讓 roll 歸零需 `Δ(L-R)=+0.91/0.854≈+1.07`
⇒ 水平約在 `L-R≈4.07`。**這是一次 1cm 的外插**（斜率本身是今日 5cm 跨距的兩點量測），
不是直接量到的：`roll_correct` 的最小可執行步約 5cm > 誤差帶，沒辦法真的把 roll 調到 0 再讀。
⚠️ **尚未做運動驗證，且目前只在記憶體**（編譯預設仍是 0）。

### 驗證
- 建置：吊機 ✅（`26ecbbfe`）。
- **冷啟動實測**（20:39 重啟，pid 27004）：`up_stop_total_kg=130` / `retract_tension_stop_kg=75`
  **從編譯預設生效** ✅；`dsz_left=1 dsz_right=1` ✅（避開 X518 單連線的坑）；
  五個網關全 OK；SD76 `(resumed)`、`L=50 R=47` 未歸零 ✅；
  張力 59.11/34.51 對比重啟前 59.13/34.53 ⇒ **載重中的零點完整保留** ✅。
- 指令：`set_fine_adjust_level_diff 4` → `OK`，status 顯示 4；負向對照 `99` → `ERR out_of_range` ✅。
- 📌 **PQW 那則 `init presence probe failed`（`pqw_water=0`）是既有狀況不是回歸** ——
  舊版 19:43:15 與新版 20:39:20 逐字相同（`.34` 上的進水球閥本來就沒接）。
  順帶這也是 PQW driver 改成原子交易後的一個等價性佐證。
- ⚠️ **啟動後 5 秒內有 4 筆 SE3 comm fail**（`readParam 0x1007`、`clearAlarm 0x1101` ×3），
  20:39:23 之後不再出現，keepalive 兩側 `ok=50 fail=0 clears=0`。
  判定為**快速重啟後 USR 網關清理舊 session 的瞬態**（SE3 走 cli_A/cli_B 專屬網關，
  不在本次改動的驅動之列，transport 也未動）。**若下次重啟仍出現，要回頭查而不是照抄這個結論。**

## [2026-09-01] Claude Code — 張力門檻常數化 + 最後四支裸對驅動改原子交易

### 修改檔案
- `Crane_control_PI/main.cpp`
  - `RETRACT_TENSION_STOP_KG_DEFAULT` 50 → **75**
  - `UP_STOP_TOTAL_KG_DEFAULT` 70 → **130**
    —— 兩者是 2026-09-01 DSZL-107 刻度校正（4.16 kg 已知重量）當日在記憶體裡改的值，
    只活在 `set_*` 指令裡，**重開程式會回到舊值並直接擋住收繩**。本次寫進常數。
  - `TENSION_MAX_KG_DEFAULT`(100) / `TENSION_DIFF_MAX_KG_DEFAULT`(50) **值不動**
    （per user 2026-09-01「維持」），但補上校正後才成立的兩點說明：
    ① 整機才 94 kg → 單側 100 kg 的 HARD alarm **一條繩承擔全部重量也不會觸發**；
    ② 水平時左右本來就差 25 kg（重心偏左）→ 正常狀態就用掉 50 kg 預算的一半。
- `app/WASH_ROBOT.h`
  - `ATTACH_PAYOUT_TARGET_KG`(10) 加警告：這是**校正前**的單位，新單位約 21~24 kg。
    值不動——使用它的 attach pay_out 整段自 2026-08-27 起是 `#if 0`。
    ⚠️ 把那段改回 `#if 1` 之前必須先換算，否則 pay_out 會一路放到上限才停且無訊息。
- `user_lib/DM2J_RS570.{h,cpp}`
  - `recv_frame_()` 拆成 `validate_frame_()`（純驗證）+ 新的 `txn_frame_()`（原子交易）。
    六個讀取站點與 `sendRecv()`（writeMulti / 廣播 writeSingle_sync）全部改走
    `TCP_client::sendAndReceive()`。逾時沿用原值；`read_pulse_per_rev` 原本
    「send + sleep 200ms + recv(200)」改為 recv 逾時 400ms（等值總等待，不縮短寬限）。
  - 順帶更正 `validate_frame_` 的過期註解：上滑台在 `cli_20_` 不是 `cli_22_`（2026-08-28 搬回）。
- `user_lib/PQW_IO_16O_RLY.{h,cpp}`
  - `readEcho()` → `txn(cmd, send_timeout_ms)`，三個站點改走原子交易。
    ⚠️ **回覆仍不驗證**：PQW 韌體 echo 格式非標準（TX `05` 回成 `00`），歷史上拿它驗證
    造成過 step_down 中途卡死。`controlRelay` 也維持原本的寬鬆語意（不把空回覆當錯誤）
    —— 收緊等於讓非標準 echo 重新變成卡死的來源。
- `user_lib/XKC_Y25_RS485.cpp`
  - `sendRecv()` 改原子交易（唯一的讀取漏斗）。`set_address` 一併改（回覆仍不驗證，
    `== 0` 才算失敗＝只有送出失敗算錯）。
  - 🔴 **`set_baud_rate` 刻意保留裸 `sendData`**：感測器對它不回覆（手冊 §1.8），
    改成原子交易只會白等一個 recv 逾時，還會把「本來就沒有回覆」記成接收逾時去推
    `TCP_client` 的斷線守衛。純送出不配對接收，本來就不會失步。
  - 🔴 **補上回覆的 slave id 與 FC 檢查**（既有缺陷，見下方「新發現」）。
- `user_lib/DY_500_weight_sensor.cpp`
  - 三個站點（`read_reg_long` / 兩個 write）改原子交易。逾時全部沿用原值。
- `user_lib/ZDT_motor_control.h`
  - 標頭註解「本體這 5 支裸對驅動從未跟進」補上結案：剩下四支已全部跟進。
- `CLAUDE.md`
  - 架構章「靠 `TCP_client::socket_mtx` 序列化（幀不交錯）」**更正**：那句只有 per-call
    成立；現在成立了，但成立的原因是 `sendAndReceive()` 不是 `socket_mtx`。
  - 吊機匯流排圖與 `DSZL_107` 驅動表更新為 **X518 一台雙通道**（`.33`，CH1=右/CH2=左）
    與已校正的 scale ——那是 2026-09-01 稍早的硬體與程式異動，先前只寫在 work_log。
- `Linux_test/fake_slaves/test_stage2.cpp`
  - 新增 `xkc`。它是這批改動裡**唯一沒有假從站覆蓋**的驅動（dm2j/pqw 本來就在這支，
    dy500 有自己的 `test_dy500`）。剛改完傳輸路徑、又不上機就驗不到的那一支，
    正是最需要 bench 測試的那一支。

### 原因
`.20` 與 `.22` 都是多裝置共用的匯流排，而 `TCP_client::socket_mtx` 是**每次呼叫**原子、
不是**每筆交易**原子 —— 走 `sendData()` + 自己接收的裸對驅動，send 與 recv 之間鎖是放開的：
① 別條執行緒可以把自己的交易插進來，兩邊的回覆在核心緩衝區裡錯位；
② 一筆遲到的回覆會落在下一次的 recv 窗口內被讀走 → **永久落後一筆，只有重連救得回來**。
2026-09-01 ZDT 先改（實機 34 公尺驗證：ZDT 錯誤 0、七個 socket `Recv-Q` 全 0），
本次把剩下的 DM2J / PQW / XKC / DY-500 四支補完。
附帶效益：四支都納入 `TCP_client` 的「連續 10 次接收逾時 → 主動斷線」守衛（只掛在原子
交易 API 上）。該守衛的計數每條連線共用但**成功一次就歸零**，所以同一條 bus 上還有別的
裝置在正常交易時，單一裝置故障（例如未安裝的 DY-500）不會把整條 bus 扯斷。

### 🔴 新發現：XKC 收到別的 slave 的回覆會照單全收
新增的 `test_stage2 xkc wrongslave` **當場抓到**：本驅動原本只驗長度與 CRC，而寫給別的
slave 的回覆帶著完全合法的 CRC → 被當成自己的收下。這正是 2026-08-28 driver 稽核在 DM2J
修掉的同一類問題，當時漏了 XKC。它與 JC-100 5~8 / QX-DO24 9 / DY-500 10/11 共用 `.22`，
收到鄰居的回覆是這條匯流排上真實會發生的事。已補 slave id + FC 0x03 檢查。
📌 **這條是「加測試才看得到」的典型**——改傳輸層時順手加的一支測試，抓到的卻是既有缺陷。

### 驗證
- **建置**：吊機 `crane_control_PI.out` ✅；本體 **16/16 TU + 連結成功** ✅；
  `Linux_test` ✅（三個目標都編，跨模組契約兩端都驗）。皆在 Pi 上以 g++ 14.2 實建，
  看產物時間戳與 md5 而非管線離開碼。
- **假從站迴歸**：`test_stage2` × 5 驅動（pqw/dm2j/xkc/zdt/se3）× 5 模式
  （normal/badcrc/wrongslave/shortframe/drop）＝ **25/25 PASS，0 WRONG**；
  `test_dy500` × 5 模式全 PASS；`test_dm2j`（機構標定＋行程守衛）全 PASS，
  且假從站的 `req#` 證實被拒絕的指令**沒有送出任何位元組**。
  XKC 另加 `badfc` 模式驗證新的 FC 檢查。全程只連 `127.0.0.1`，不碰真 485 匯流排。
- ⚠️ **尚未上機**：兩支程式都還沒部署、沒重啟。門檻常數化要重啟才會生效。

