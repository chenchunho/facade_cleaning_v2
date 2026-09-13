# Work Log

## 🆕 2026-09-13 文書日②:電控箱改線路圖、供電來源填齊、§0 總綱、**GUI 離線推導工具**

### 已完成
- **電控箱接線圖改為單線圖**(per user「要的是線路圖」):`reference/wiring/` 兩張 SVG + 產生器
  `gen_wiring_svg.py`(座標程式算、零誤接、改接線改產生器)。commit `f7dbf5f`。
- **供電來源 per user 全數填齊**:本體 24V 上還有 QX-DO24/JC-100×4/XKC/**Fathom-X 載波**(⇒ blip 時機身端
  隧道跟著斷);WT901 走 Pi5 USB;SD76×3 直接吃 220V;吊機**另有獨立 MW 150W 24V**(X518/ZS-DIO/載波);
  吊機 Pi 獨立 5V 變壓器;交換器 吊機 8-port PoE / 本體 5-port hub;**機身沒有相機**;MH300 段位先留問號。
- 🎯 **§0 系統邊界總綱(per user)**:兩箱只共用 220V 進線與隧道通訊,其餘各自負責,**與軟體邊界對齊**。
  判斷波及:箱內不跨箱 / 220V 側同時打兩箱 / 隧道側只斷通訊。SOFTWARE.md §1 互指。
- 缺資料 10→13 項(新增 #12 實體急停有無、#13 24V 幹線拓樸與線徑);已答 4 項。
- 🆕 **`harness/gui_offline.sh` + `fake_robot.py`**(per user「假的資料伺服器讓 GUI 啟動」):三個假端點
  (5001/5002/9527)回**真格式**(照 C++ `cmd_status`/`relay_status`/吊機 status/手臂 `[M1]`)、狀態會動、
  會推 EVT;**每條收到的指令寫 `gui_cmd_log.txt` 標 KNOWN/UNKNOWN**,`report` 列出 GUI 需要但沒模擬的 ⇒
  **從 GUI 推導程式面功能**。本機 node 20 + `npm install` 已裝;端到端(HTTP 200、WS 三目標、EVT)驗證通過。
  🔴 第一次跑就抓到:**web_backend 每 BRIDGE_PING_MS 對手臂送 `ping`,手臂只有 7 條指令,回
  `ERR unknown command: ping`**(`main_api.cpp:3890`)—— 保活達到目的但手臂 log 被洗。

### 📌 決策(2026-09-13 per user,詳 `reference/field_procedure_v2.md` §6)
1. **週期編排搬回 C++**:cycle_test 的 full 迴圈 → 本體 `mission` 指令族(含前置檢查與 roll 監看執行緒);
   cycle_test 降級為對照/耐久工具。**理由:姿態保護跟著腳本走 = 正式作業反而沒有保護。**
2. **安全模式狀態機做最小版**:跨兩箱 `SAFE`(吊機停鎖 + 本體維持吸盤/停手臂/關水/槳保壓),
   觸發接現有三源(張力 / watchdog / roll),通報 EVT + GUI banner。環境感測不在本期。
3. **開機自檢 = GUI 軟閘門(B)**:§3 七步 + 現有前置檢查合成一條有順序的「作業流程」checklist,
   全綠才開放升頂 / Mission;**吊機 raw hold 不鎖**(救援)。否決 C++ 硬閘門(會擋救援)。
   🔴 **這三條是 GUI 改版的前提**,GUI 設計以此為準。

### 待完成
- ✅ ~~計畫書~~ **已寫** `plans/orchestration_to_cpp_plan.md`(mission 指令族 + EVT / SAFE 三源接線與解除 / 自檢欄位 /
  GUI 作業流程頁 / fake_robot 先行 / **指令序列等價驗證** / 6 階段)。✅ **09-13 per user 核准**;SAFE 進入時風扇**直接關**(拍板);前置(行緩衝、watchdog)確認為階段 3 前必修。
  前置待辦被拉高:`crane_cmd_` 無行緩衝(EVT 半行)、本體 watchdog 死碼 —— 都是階段 3 前必修。
- 🟡 **使用者點一輪 v2 GUI 後跑 `./harness/gui_offline.sh report`** → 得到「GUI 需要的功能清單」,
  再對照 SOFTWARE.md §2/§3 指令面決定哪些是缺口、哪些是 GUI 該拿掉。
- 🟡 手臂 `ping`:決定手臂加 `PING`,或 web_backend 對 arm 改用 `STATUS` 保活(二選一,別兩邊都改)。
- (承前)24V 治本三選項 / LRS-150-24 OLP 手冊核對 / 急停切 57.6V / 開箱拍照補 §3.2C。

## 🆕 2026-09-12 文書日:軟/硬架構文件成對、.claude 整理、巨檔壓縮、**24V blip 根因確認**、ONBOARDING 歸檔、瘦身第一刀

### 已完成

**🔴 24V blip 根因(per user 確認)**:全系統**只有一顆 LRS-150-24(150 W)**,而**手臂 M1(DM10010L)+ M2 也接在它上面**,
與 ZDT×4/抱閘/滾筒×2/幫浦×2/繼電器/閥共用。M1 15 Nm 壓橫桿堵轉的峰值電流拉垮 24V → 繼電器/幫浦掉、ZDT 計數器亂。
軟體降力 8 Nm(昨天)是緩解;**治本要分離供電或加大 24V**。詳 `HARDWARE.md` §5。
📌 設計階段其實早有預警(總表「LRS-150-24 容量 6.5A 對現有負載偏緊」),昨天那次等於把它應驗。

**文件體系**(commit `1ecc6be` / `0d8408d` / `6b39f74` / `48e3b5b`)
- `SOFTWARE.md`(as-is 軟體架構)+ `HARDWARE.md`(硬體架構,per user 校正 5 點)成對,取代 ONBOARDING。
- `.claude/` 整理為 reference/plans/archive;changelog 按月輪替 1.98MB→316KB + 往後每條瘦身(per user);
  work_log 07-22~09-09 壓成摘要 #8–#15(927KB→293KB);refactor_plan 標 ⏸ 暫緩(jim 拍板:改針對性瘦身)。
- **SOFTWARE.md 全文對原始碼驗證 37 項**:行數 8/8、吊機 §3.2 九個行號區段 9/9 **逐項命中**;
  修正 7 項(指令數 ~115→**99**、v1 死碼分類、驅動清單漏 DIHOOL、`start_*.sh` 未版控、public_v2 單檔、
  cli_C 註解已修、cmd_ 計數語意)。🔴 **再動這些數字前請重跑比對。**
- **壓縮保全清單 81 條核對清空**:20 條已涵蓋/已結案、2 條前提消失作廢、59 條併入總表新節(分 6 群)。
- **ONBOARDING 歸檔**:21 項關鍵踩坑比對後 **15 項別處查不到** ⇒ 抽成 `reference/engineering_pitfalls.md`
  (驅動踩坑 6 類 + 工程方法 5 類 + 8 條方法論教訓);原檔凍結為 `archive/ONBOARDING-2026-08-13.md`。
  🔴 **§4 v1↔v2 機械差異、§6 步態演進無可替代,整份留在歸檔檔** —— `motion_flow.md` 至今是 v1 快照。

**瘦身第一刀(純刪,零行為改動)**
`PalletizerController.h`(352 行)、`DIHOOL_control.{h,cpp}`(0 引用且本來就沒進 build 清單)、
手臂死常數 `FINE_*`×4 + `STIFF_*`×5、本體 header 12 條死 `cmd_` 宣告(102→**90**,與定義完全對齊)。
✅ **本機兩層驗證**:① 7 支原始碼 `g++ -fsyntax-only` 全過零錯誤;② 預處理輸出比對 HEAD
(harness `prove_noop.sh` 的方法論,改用 `g++ -E -P`)—— 本體差異**恰好只有那 12 行**、手臂**恰好只有那 9 個**
(外加 3 行 `__assert_fail` 的 `__LINE__` 位移,非語意)、吊機**完全相同** ⇒ 對編譯器無副作用。

**電控箱接線圖(HARDWARE.md §3.2,182→358 行)**
應使用者要求新增**兩個電控箱的信號層接線圖** + PQW 通道表 + 吸盤腳位對應(通道對應**由程式碼抽出**,可重跑)。
🔴 **順帶抓到三個矛盾**:① **PQW 實體是 8 路不是 16**(`PQW_TOTAL_CH=8`;驅動類別名 `PQW_IO_16O_RLY` 是 16 路版,
且 `WASH_ROBOT.h:442` 還留著一句「now 16CH physically」**與 20 行後的 `=8` 自相矛盾**,已一併更正);
② **真空閥現為「四顆吸盤共用一顆 VT307」**(2026-08-27 改單閥),原文件寫得像每顆一顆;
③ 正壓閥數量對不上(§1 寫 ×2、程式寫「4 顆共用」一路 CH6)—— 待目視。

**per user 解答兩個關鍵缺口(2026-09-12 晚)**
1. 🔴 **兩顆螺旋槳 ESC 並接同一路 PWM** ⇒ 三個後果(已寫進 §6):
   **結構上無法差動**(程式只寫 `PWM_STEP_CH=1` 是**對的**,不是漏寫一路);
   **單邊失效無法偵測也無法補償**(另一顆仍全推 → 淨力矩,無任何回授)
   ⇒ 待辦「單邊推力失效」的處置**只能走硬體**(NPP 的 DC OK 進 DI 互鎖),**軟體差動補償這條路是死的**;
   **單點失效同時帶走兩具**(一條訊號線 + 斷線維持輸出)⇒ **急停必須能切斷 57.6V 動力**,送 PWM 停止值不夠。
2. 🔴 **斷路器/保險絲都沒裝** ⇒ 全系統**無任何外加過流保護**(新增 §5.1)。
   🎯 **這補完了 blip 的圖像**:唯一在動作的保護是 **PSU 自身 OLP**,而 LRS 系列是「限流 + 故障排除後自動恢復」
   (hiccup)—— **正好解釋 blip 為何垂降後自己回來、不需要有人復歸**。那不是接線故障,是電源自我保護再恢復。
   ⚠️ **此為強假說,尚未核對 LRS-150-24 手冊的 OLP 條件與恢復行為** → 待查。
   📌 **它改變了治本的選項排序**:原本只想「分離供電 or 換大 PSU」,但**加裝過流保護是獨立且更便宜的第三刀**,
   而且是目前唯一處理「短路」情境的手段(換大 PSU 反而讓可用故障電流變大)⇒ 三者並行評估,不是二選一。
   AC 側同樣無保護而機器**帶水作業** —— 與待辦「漏電保護 RCD 為必要非選配」🔴(09-01 暫緩)是同一缺口。

**發佈的檢視頁面**(private artifact,可在別的裝置上讀)
- 踩坑手冊 `engineering_pitfalls.md` → https://claude.ai/code/artifact/869c98f4-39ac-417e-b461-45f3140aad28
- 電控架構(電控箱/電源/匯流排,依電壓分色重畫)→ https://claude.ai/code/artifact/ff3cf70a-add5-4fec-9118-74066159a8d1

### 🔴 三個被推翻的判斷(重要,避免重犯)

1. **`DEPLOY_F_THETA_MIN` 不是死常數** —— 我早上把它列進死常數,實際它是 `DEPLOY_F [θmin]` 的預設值兼
   `th_min >= th_max` 驗證下界(`main_api.cpp:3031`),**刪了編不過**。行為用途雖已於 09-11 移除,
   要清得連指令簽名一起改。
2. **`removed_in_v2` 那 15 條不同質** —— 我早上寫「已掏空、不必再砍」,但 ONBOARDING §11 記著
   **重心校正 5 條 + 窗框避障 4 條是「暫時 stub 不是廢棄決定」**,GUI 面板 per user 明確要求保留。
   SOFTWARE.md §2.3 已改成兩類表(真退役 6 條可刪 / 暫時 stub 9 條要留)。**差點把等著重做的接口當死碼砍了。**
3. 🎯 **「本機不能 build」是假的** —— 本機有 **g++ 9.4.0**,能 `-fsyntax-only`、能產 x86_64 `.o`、
   能做預處理等價比對,只是產不出 aarch64。已更正三處(`CLAUDE.md` harness 列、`harness/README.md`
   開頭的 `apt install g++`、待辦總表「2026-07 那批從未編譯驗證」)。
   ⇒ **`harness/` 等價比對工具其實一直可以跑**(設計上就是 x86_64 自己跟自己比,見 `build.sh` 抬頭),
   卡著只是因為那句話沒人複驗。**下次動重構,這條驗證路是通的。**

### per user 拍板

- **8 Nm 對刮刀可接受** —— 滾筒與刮刀共用同一 target,**不為刮刀另訂力道目標**(別再提議拆)。
- **DSZL-107 已校正、讀值準** —— 量測面全部結案(正負號＋量值＋線性度),我原判「線性度未驗」是過時的。
  🔴 唯一遺留是**校出來的 scale 沒地方存**:存吊機端 setting/config;吊機端暫不規劃的話本體端也會用到,
  介面走 **Web GUI 改數值**那個既有功能。**併入「吊機執行期參數不持久化」一起做**。

### 待完成

- 🔴 **24V 供電治本(選項已擴為三)**:量總負載/峰值 → 在「分離供電 / 換大 PSU / **加裝過流保護**」之間決定(`HARDWARE.md` §5、§5.1)。
- 🔴 **核對 LRS-150-24 手冊的 OLP/hiccup 行為**,驗證「blip = PSU 自身過載保護再自動恢復」這個假說。
- 🔴 **急停要能切斷 57.6V 動力**(螺旋槳單路 PWM + 斷線維持輸出 ⇒ 送停止值送不到)。
- 🟡 **開箱各拍 3–5 張照片**(整體/電源區/網關區/端子台)→ 可一次補完 `HARDWARE.md` §3.2C 缺的 8 項。
- 🔴 **測完改回隧道 192.168.1**(web `WROBOT_IP`、本體 `FCV_EP_CRANE_HOST`)—— 現暫全 WiFi。
- 🟡 **下次上機開頭**:先 `zdt_home feet`(09-11 最後一次 blip 後未復位)→ 重跑 full 驗真 8 Nm
  → **本輪純刪改動的 aarch64 實建**(`build_body.sh` / 手臂 `compile.sh`)。
- 🟡 **瘦身第二刀(須上機驗證)**:`DY_500` 移除(在 body+crane 兩份 build 清單內 + `WASH_ROBOT.h` 有 include);
  本體 v1 死碼 11 條(對照 `reference/v1_v2_feature_map.md`)。
- 🟡 `start_*.sh` 收進 repo `scripts/run/` —— 啟動參數(`HOME_GROUND` 預設 256、寫死 IP、web 佈署路徑)
  目前只活在兩台 Pi 的 `~/run/`,**完全沒版控**。
- 🟡 `motion_flow.md` §2 硬體表仍是 v1(總表「規範文件架構圖與程式碼脫節」)—— 純文書,可無機器進行。

## 🆕 2026-09-11(深夜④)full 又 24V blip:真因=15Nm override 沒降到 → 已補

**事故(per user)**:降力版 full 跑到橫桿步**又 24V 掉電**。同一個橫桿問題。使用者判斷:**滾筒(在 24V bus 上)15Nm 壓在橫桿上卡住→堵轉過流→24V blip**。—— 這也印證 blip 根因就是**高力壓桿堵轉過流**,不是別的。

**為何降力版還是 15Nm(我漏的)**:我先前只降了**手臂 DEFAULT**(main_api.h `DEPLOY_F_TARGET_NM 15→8`),但 **cycle_test.py 是顯式傳值** —— 第 137 行 `ARM_TARGET_NM = 15.0`,經 `arm_deploy_f 15.0 <slot>` 代轉,覆蓋掉手臂 default。所以 full 全程仍是 15Nm,橫桿步照樣 slam → blip。單獨 `arm_deploy_f 8` 測試沒事是因為我當時顯式傳 8。
- 🔴 教訓:**降力必須改「顯式傳值處」,不能只改 default**。DEPLOY_F 的 target 由呼叫端決定。

**已修**:cycle_test.py `ARM_TARGET_NM = float(os.environ.get("FCV_ARM_NM","8"))`(15→8,env 可覆蓋),已部署 Pi。下次 full 才是真 8Nm。低力刷過(8Nm)先前雙工具 + 滑台 0-100-0 掃已實測**零 blip**,方案本身正確,只差這個 override。

### 待完成
- 🔴 **這次 blip 後 ZDT 位置計數器可能又亂**(24V blip 慣例會打亂 slave 5-8)——下次動 ZDT/跑 full 前先 `zdt_home feet` 復位確認。
- 🟡 重跑 full(真 8Nm):升頂→pump on→`full 1 5 40`,驗橫桿步低力刷過、不 blip、跑完整趟。
- 🔴 測完改回隧道 192.168.1(web WROBOT_IP 5.26→1.100、本體 FCV_EP_CRANE_HOST 5.25→1.10)。現暫全 WiFi。
- 🟡 手臂 M1 過速煞車(retract 太快觸發 BRAKE)。~~未用常數 FINE_*/STIFF_* 可清~~ ✅ 2026-09-12 已刪(THETA_MIN 查證後保留,非死常數)。


## 🆕 2026-09-11(深夜③)橫桿問題最終定案:M1 判據放棄 → 降力刷過

**結論:M1(手臂)訊號物理上分不出橫桿與玻璃,放棄偵測,改「降低下壓力道、低力刷過」。**

**逐一否決的偵測法(全部實測失敗)**:
1. **contact θ / th_min(距離)**:橫桿接觸點沿長滾筒可高可低 → θ 飄 0.55–0.60,與玻璃 0.58–0.59 **重疊**。同一橫桿近同高度量到 0.551(probe)vs 0.598(full run),差 ~20mm。full run step5 就因 θ=0.598>th_min 漏判、壓下去。(bar_test2 那 34/34 是各位置剛好撞低-θ 的巧合。)
2. **剛度 Δtau/Δcmd(Step 4c)**:假設「硬鋼=高剛度」**錯**——滾筒偏心壓桿是繞桿 pivot、不是頂著建力,初期剛度低。實測**橫桿 9.8 = 玻璃 9.8**(工具/滾筒柔度主導低負載回應,蓋掉牆材差異)。完全無法區分。

**最終方案(per user)**:`DEPLOY_F_TARGET_NM 15→8`、`DEPLOY_F_COARSE_NM 13→6`;**移除偵測**(main_api.cpp 刪掉細掃 Step4b + th_min 守衛B + 剛度 Step4c,保留 no_wall 守衛A 與 cannot_reach)。手臂低力刷過一切,刷到橫桿也無傷。motor_api 已重編部署(tag arm_0911lf,20:23)。

**驗證(height 81 橫桿位、四顆全吸)**:
- 玻璃 `arm_deploy_f 8` → OK tau 7.47/7.86(到 8 就停,不衝 15)。
- 橫桿 `arm_deploy_f 8` → OK tau 7.96(輕頂 8Nm,無 slam)。
- 🎯 **完整 blip 情境**(deploy 8 頂橫桿 + rail 0→100→0 掃)→ **零 blip**:四顆全程 -66~-70、rail 掃出掃回 OK、幫浦正常。blip 根因=高力堵轉過流,降力即解。
- ⚠️ rail 0 偶發 "pos read failed"(DM2J 位置讀回 glitch,移動本身 OK、非 blip)。

### 待完成
- 🔴 **測完改回隧道 192.168.1**(per user「暫時全 WiFi 測完改回」):web WROBOT_IP 5.26→1.100、本體 FCV_EP_CRANE_HOST 5.25→1.10(重啟本體)。目前全鏈路暫走 WiFi。
- 🟡 重跑 full(降力版):驗整趟清潔在有橫桿立面低力刷過、不中斷、不 blip。
- ✅ ~~未用常數 FINE_*/STIFF_* 留在 .h~~ **2026-09-12 已刪 9 個**;🔴 THETA_MIN 查證後**不是**死常數(仍為 `DEPLOY_F` θmin 預設值),保留。
- 🟡 cup5/cup7 JC100 隨位置間歇(現場幾何,非壞);DM2J 滑台無 homing、偶發 pos 讀回失敗。
- 🟡 AI-2 v2 GUI 危險鈕閘門(2026.09.11-1919)已上;目前只開 v2、v1 停。


## 🆕 2026-09-11(深夜②)手臂橫桿偵測:力控+距離「距離放精細」(細掃) + V2 GUI 誤觸互鎖

**決策轉向(per user)**:放棄「吊機高度帶」判據(高度隨環境變、帶寬難定;高度帶已做成 opt-in
`FCV_SKIP_BANDS`,不給就不啟用,擱著)。改回原始設計「**下壓力道 vs 距離,看哪個先達到**」,並**把「距離」這側限縮、做精細**。

**根因(為何距離分不出橫桿)**:尋觸粗掃步進 `DEPLOY_F_COARSE_STEP=0.030rad≈13mm`,而橫桿(~212mm)
只比最近玻璃(~216mm)近 ~4mm —— 13mm 的尺量不出 4mm。且 th_min=0.565 還低於橫桿接觸(0.5659)。
🔴 滾筒是長圓柱,橫桿偏心接觸會讓粗掃 contact θ 偏高(過頭壓進桿裡才判觸)。

**修法(motor_api,已重編部署)**:
- main_api.cpp 尋觸後加 **Step 4b 細掃(fine re-seek)**:退開 FINE_BACKOFF(0.045rad)、以 FINE_STEP(0.005rad~2mm)重逼近,把 theta_contact 量到 ±2mm。
- main_api.h 新增 DEPLOY_F_FINE_STEP/FINE_BACKOFF(0.045)/FINE_SETTLE_MS(120)/FINE_MAX(24);**THETA_MIN 0.565→0.570**。
- 編譯:`cleaning_arm/compile.sh`(Pi 上 aarch64);重啟手臂:停舊 motor_api→FIFO 法起新(tag arm_0911fs)。

**橫桿測試(deploy-based,bar_test2,height 102)**:每次真的 arm_deploy_f 壓、看判定。
- 實測橫桿**真實 fine contact θ ≈ 0.542–0.557**(粗掃量的 0.566–0.584 是過頭假象),離 th_min 0.570 有 **6–8mm 餘裕**(比原估 2mm 寬鬆)。
- **全部判 obstacle**(輕觸 2Nm 就判掉、不進 13Nm 粗壓 = 不撞、不 blip)。✅ 對照組(玻璃位置應全 OK)因 GUI 事件中斷,待續。
- 📌 bar_test2 的 ask() 原本固定抽滿 timeout(每次 deploy ~90s),已改「收到 OK/ERR 就返回」+ 送前清殘留(修掉 status bleed 的 r[0] 誤判)。

**🔴 V2 GUI 安全 bug(已送 AI-2 修)**:重啟 web 後手臂「自己靠上」兩次 —— arm log 證實 GUI 連著時送了 DEPLOY_F。
af-go(壓上,index.html:4304)有 window.confirm() 兩步保護卻仍送出 → 推測 kiosk/wayvnc 本機瀏覽器自動接受 confirm,或觸控誤觸。**已切斷**(web 全停)。已請 AI-2 加「手臂解鎖」武裝互鎖(載入預設 OFF、不只靠 confirm、送一次後復位、DEPLOY/PARK 一併),更新版號回報。
✅ 兩次誤觸都被新偵測擋成 obstacle、無 slam、無 blip —— 真實情境再次驗證修法。

### 待完成
- 🔴 **測完改回隧道 192.168.1**(per user「暫時全部 WiFi,測完再改回」):① 本體 `FCV_EP_CRANE_HOST` 192.168.5.25→**192.168.1.10**(需重啟本體=關繼電器);② web v2 `WROBOT_IP` 192.168.5.26→**192.168.1.100**(重啟 web)。現全鏈路暫走 WiFi。
- ✅ **端對端驗證完成(2026-09-11)34/34 全對**:橫桿① h102 θ0.542–0.557 4/4、橫桿② h87 θ0.544–0.552 10/10、橫桿③ h68 θ0.542–0.544 10/10(皆 obstacle)、玻璃 h18 θ0.582–0.592 10/10 OK(tau 14.2–15.4Nm)。三種橫桿高度(68–102)接觸 θ 都穩定 0.54–0.56、玻璃 0.58–0.59。th_min 0.570 乾淨分離(離橫桿 ~8mm、離玻璃 ~5mm),兩種橫桿高度都穩定。**細掃+th_min 修法確認可用。**
- ✅ **玻璃對照組完成(2026-09-11)**:height 18cm、四顆全吸(-66~-68)、bar_test2 10× → **10/10 OK**(tau 14.2–15.4Nm、iters 0–1、~8–9s/次)。玻璃 contact θ **0.582–0.592**,橫桿 **0.542–0.557**,th_min 0.570 居中,兩側各 ~5–6mm 餘裕、間隔 ~11mm 乾淨分離。**細掃+th_min 修法端對端驗證成功**。
- 🟡 AI-2 修 V2 手臂武裝互鎖 → 回報版號 → 使用者重開 V2 驗證。
- 🟡 M1 過速安全煞車:arm_retract 從遠處收回時 vel 超 0.4 觸發 BRAKE(pos 收到 0 沒事),收回速度或起步或許要調。
- 🔴 24V blip 根因仍未解(偵測擋下壓桿後觸發機率大降,但根因在)。
- 🟡 cup7/cup8 JC100 壞、DM2J 滑台無 homing 重啟未校正。


## 🆕 2026-09-11(深夜)橫桿撞擊真因 + 高度帶跳過(執行期,不進記憶)

**事故**:full 重跑期間手臂(滾筒 RIGHT)**第二次**撞橫桿仍下壓清潔 → 24V bus blip → 使用者雙電腦斷電。
恢復:crane `start_crane.sh <tag> 0`(🔴 **HOME_GROUND 必須 0** —— 預設 256 是頂零約定,會打回頂零)、
body `start_body.sh`、arm `cd cleaning_arm && motor_api`(FIFO 法);SD76 絕對位置重啟保留(height 102 可信);
ZDT 伸出+計數器亂 → `zdt_home feet` 逐顆自探退硬限位歸零(4/4 dir=1 OK);pump 關著降 24V 負載才收。

**真因診斷**(`arm_deploy_f 3 RIGHT` 對橫桿實測):
- 橫桿 contact θ=**0.5659**,th_min=0.565 → 只差 0.0009 rad **剛好逃過守衛 B**(θ<th_min→obstacle)→ 續壓到 coarse 13Nm(=撞)。
- 🔴 `DEPLOY_F_COARSE_NM=13`:粗壓一律到 13Nm 才細收斂,**跟 target 無關** —— 我用 target=3 想「輕壓」是錯的,coarse 仍到 14.31Nm。
- 🔴🔴 **th_min 修法不可靠**(per user + 圖):滾筒是長圓柱,橫桿撞在滾筒上的**接觸點沿桿可高可低** → M1 量到的 contact θ 會飄、甚至落進玻璃 θ 區間 → **單靠接觸角分不出橫桿/玻璃**。

**解法(per user)**:改用**與接觸角無關**的判據 = **吊機高度**(絕對、精準)。橫桿高度**隨環境變 → 不進記憶檔**,做成執行期參數。
- cycle_test.py 新增 `FCV_SKIP_BANDS`(逗號分隔;每項 "lo-hi" 或單一 "center"+`FCV_SKIP_MARGIN_CM` 預設15)+ `in_skip_band()` + `crossbar_skip_steps` 計數 + 總結行。清潔前比對當步 `cur` 高度,落帶→**跳過手臂 deploy(連滾筒帶刮刀)、續到下一位置**,根本不壓。md5 4fd71fe8。
- 測試法(per user,非 full):原地重複壓 —— `bar_test.py`(scratchpad→Pi /tmp)。

**測試① 橫桿位置(height 102、帶 87-117)壓 10 次**:✅ 跳過 10 / 實壓 0 / 無異常;arm log 零 DEPLOY_F、只有 M1 MOVETO 0(收回)。手臂全程沒壓上橫桿。

### 待完成
- 🟡 **測試② 對照組**:移到無橫桿位置,同帶 → 手臂應 10/10 實壓+收回(等使用者移位、重建吸附後跑)。
- 🟡 帶寬 ±15cm(cur 預設)是初值 —— 需依滾筒長度/實際干涉範圍調 `FCV_SKIP_MARGIN_CM`。
- 🔴 24V blip 根因未解(手臂壓桿堵轉過流?邊際 PSU/斷路器?)—— 高度帶避免壓桿後應大幅降低觸發,但根因仍在。
- 🟡 cup7/cup8 JC100 感測器壞(實體有吸,(B) 容忍);DM2J 滑台無 homing、重啟後未校正。
- 🟡 GUI 同步:FCV_SKIP_BANDS 之後要不要做進 GUI(執行期設定,不落記憶)。


## 🆕 2026-09-11(晚)ZDT 24V-blip 零點恢復 + 新指令 + summary 複審(per user)

**事故**:full 期間 **24V bus blip 一次**(繼電器+真空馬達+ZDT 都在 24V),弄亂 ZDT 吸盤推桿(slave 5-8)驅動器**位置計數器**(計數器以為 0/收回、物理伸出 10cm)。ZDT 位置基準只能靠在已知位 set_zero 重建。

**踩坑**:`zdt_disable` **不會失能馬達**(只是「從群組操作排除」,`disabled_zdt_slaves_.insert`)—— jim 按了以為鬆開其實沒有。真失能要 `motion_control_driver_EN(false)`,原本沒指令暴露。

**新增本體指令(已部署):**
- `zdt_power <5-8> <on|off>` → 真 driver_EN torque on/off。body `c9dbcd00`。
- `zdt_home <feet|all>` → 自動堵轉歸零:**相對模式(mode=0)+ 方向自偵測**(讀位置→小步試探→判收回 dir)→ 往收回撞硬限位堵轉(zdt_wait defer_stall=成功)→ set_zero → hold。body 最終 `b1ecfe02`。
  - 🔴 **第一版 bug(已修)**:用「絕對負目標 -66000」想收回 → ZDT pulse **無號**、負值變巨大正值 → **伸錯方向撞 20cm 硬限位並在那 set_zero**(更糟)。改相對+自偵測方向後正確。實機驗證:伸 5cm → zdt_home → dir 自判=1、往內收到 0cm、set_zero、伸 5cm 交叉驗證零點正確。

**兩套重歸零方法(都實機驗過):**
1. `zdt_power off` → 手推到 0cm → `zdt_zero all` → `zdt_power on`。(AI-2 已上 GUI:v2 `-1716`,並把「使能/失能」鈕改名「納入/排除群組」註明不斷電)
2. `zdt_home all`(自動,一鍵)。GUI 可選加。

**🔴 ZDT_MODBUS_SUMMARY.md vs 驅動 複審(per user):**
- 暫存器/幀格式全對(set_zero 0x000A / release_stall 0x000E / EN 0x00F3 / pos 0x00FD / speed 0x00F6 / estop 0x00FE / batch 0x04-0x0043 / 狀態旗標 / CRC16)。
- 🔴 **發現1:ZDT 有原生 homing 但 body 沒用**。驅動 `trigger_home`(0x009A,含 **mode 0x02 無感撞限位**)/`set_home_zero_position`(0x0093)/`abort_home`(0x009C)齊備、對上手冊 3.3,但 `app/*.cpp` 從沒呼叫。**「這台沒 homing、不要 home_start」註解是講 DM2J 上滑台(0x6002←0x0020),不是 ZDT**——兩者被混淆。⇒ 我手刻的 zdt_home 等於重造 ZDT 原生撞限位歸位。🔮 **待辦(不急):試 `trigger_home(0x02)`,能用就換原生歸位(參數 3.3.6 撞限速度/電流/時間門檻可能要調);不能用留 zdt_home。**
- ⚠️ **發現2:pos_mode pulse 無號、驅動沒擋負值**。手冊 pulse 0x0~0xFFFFFFFF 無號、方向靠 dir;`motion_control_pos_mode(int pulse)` 收有號直接打包、負值→巨大正值→反向衝(=zdt_home v1 的 bug)。🔴 **待辦:驅動加 pulse<0 守衛(拒絕/abs+報錯)。**

📌 **教訓**:①`zdt_disable`≠失能(命名坑,AI-2 已改名);②ZDT pulse 無號,收回要靠 dir+相對、不能用負絕對;③ZDT(cups)與 DM2J(rail)的 homing 能力/暫存器完全不同,別混。

---

## 🆕 2026-09-11(晚)手臂力控距離量測 + 搆不到/異物跳過(per user)

🔮 **待辦(per user,做進 GUI):牆面距離量測/剖面** —— DEPLOY_F 的 `contact`(M1 角度 rad)可經**實測幾何式換算成 mm**:`牆距mm = 490·sin(θ−0.38)+121`(main_api.h:ARM_LENGTH_MM=490 / VERTICAL_OFFSET_RAD=0.38,三點量測擬合殘差<0.1mm)。GUI 可顯示每個位置的牆距、標出異常(近=異物、遠=搆不到風險)。M1 上限 θ=1.10 → 最遠可及 **444mm**。
- 實測參考(2026-09-11):正常牆距 216~253mm;某高度整片牆遠到 311~343mm(比正常遠 78~96mm)→ 刮刀 reach 餘裕小、遠牆時先 cannot-reach;橫桿=硬且微凸(226mm、比正常近~7mm)但力瞬間爆(WARN 17.24)。

🔴 **待辦:DEPLOY_F obstacle 判定精修** —— 橫桿(硬、微凸 7mm)目前回 **WARN 非 obstacle**(正常 firm press 也可到 16.6,17.24 只高一點、難用 tau 門檻乾淨分)。要真正「壓到異物就不刷」需在 motor_api DEPLOY_F 用**幾何+力上升斜率**辨識異物(接觸比預期近 + 力瞬間爆),回 obstacle。需 motor_api 改+重編。目前先靠 arm_clean_combo:cannot_reach/obstacle→跳過(見下)。

📌 **決策(per user):手臂力控設計 = 「壓到異物就不刷、跳過、到下一位置」**。full 的 `arm_clean_combo` 據此改:DEPLOY_F 回 **cannot_reach 或 obstacle → 跳過該把清潔、收手臂、續到下一位置(不 bail)**。(cannot_reach 先前落到 else→bail,是 step4 中止主因。)

---

## 🆕 2026-09-11(傍晚,續)🔴 牆面座標慣例反轉 = **地面歸零**(per user 更正)

**per user 拍板:慣例是「地面最低點 = SD76 0」,不是頂端歸零。** 我先前做反了(在頂端跑 `zero_meters top` 把 0 移到頂端 + home_ground_cm=261),導致「移動到0」把機器往上送到頂端(應往下到地面)。

**正確慣例:**
- 地面最低點 `zero_meters ground` → SD76=0。**不要**在頂端 `zero_meters top`。
- 頂端讀 `status` length_left = 負值,**取 abs = 牆面最大高度**(這面牆 261)。純讀、不歸零。
- home_ground_cm 維持 0。「降到 0 / 降到最低」= 地面安全點。

**實機已修正:** 把機器從頂端降回地面安全點(pay_out ~261、分段看張力、未落地)→ `zero_meters ground`(SD76=0)+ `set_home_ground 0`(清掉 261)。現況:機器停地面、**SD76=0、home_ground_cm=0**、懸吊(tension 46/56、valid=1)、全收卸力、手臂使能。

🔴 **連帶影響(與昨天/上午的 FCV_TOP_CM 結論相反,已轉達 AI-2 撤回 (a)):**
- home_ground_cm=0 ⇒ cycle_test 拿不到 TOP(會落預設 231=錯)⇒ **full 需明確帶 `FCV_TOP_CM=牆高(261)`**。
- GUI「② 最高點設定」**改為「讀頂端 length_left 取 abs 存牆高」,不用 `zero_meters top`**;「① 地面歸零」保留 `zero_meters ground`;START gate 改看「①做過 + ②量到牆高>0」不看 home_ground_cm。
- full 等手臂修好才跑,不急。

📌 **牆高建置持久化/自動載入 + ①② 獨立(per user,AI-2 v2 `2026.09.11-1507`):**
- 牆高存吊機 `~/run/wall_height.json`(`{cm,at,left_raw}`),node/程式重啟不掉;GUI WS 連線第一則即推 `{src:'wall',cm}` → 開啟即顯示,不用重按。卡片標「記憶值 + 量測時間戳」。
- **① 最低點 / ② 最高點 獨立、各自記憶、可單獨重校**(per user:有時只校一個就能用)。START gate 看「兩者都有效」不看「這次都按過」。
- 🔴 **耦合方向(記著免踩)**:牆高(②)相對「地面 0」(①)量出。**只重 ② 一定安全**(相對現有地面重量);**只重 ①** 若在同一地面點歸零則牆高仍有效,若地面基準移位則舊牆高 stale → 要順手重 ②。起跑守衛 `|H−牆高|≤5` 是安全網。

✅ **新流程實機驗證(per user「先升頂讀牆高試試」+「放到最低點」):** 地面 `zero_meters ground`(L=0)→ 升頂讀 length_left=-260 → GUI「② 最高點設定」(AI-2 v2 `2026.09.11-1456`)存 `~/run/wall_height.json` = `{"cm":260,"left_raw":-260}` ✅ 端到端通。之後降回最低點 L=0/0、懸吊安全。牆高 260(首量 261,差 1cm=升降後計米器正常漂移)。此值即 full 的 FCV_TOP_CM 來源(server.js 帶入)。

📌 **教訓**:兩點式歸零若用 `zero_meters top`,0 會落在頂端 —— 與操作者「降到0=地面」的直覺相反。此機一律**地面歸零 + 頂端只讀不歸零**。

---

## 🆕 2026-09-11(傍晚)full 首度上機 → 手臂 M1 機構異常中止(per user)

**座標建置(兩點式,per user 定義的牆面高度慣例):**
- 底端:機器懸吊處 `zero_meters ground` → SD76 左右=0(牆面最低點)。
- 頂端:user 用 GUI 升到最高玻璃點,讀 SD76=-261 → `zero_meters top` → `home_ground_cm=261`(這棟樓可清高度)、頂端歸零。cycle_test.py 自動偵測頂端慣例、TOP=261。

**init 卡 paused_on_error(非機構):**
- init 幫浦 A ON 後,關進水閥步驟 `crane_cmd water_inlet off` → 吊機回 `water_inlet_relay_fail` ×3 → `[PAUSE-ON-ERROR] init_water_inlet_off`。
- ⚠️ **進水球閥(ZS-DIO USR_W `.32`、slave 1、**CH4**)= 間歇、非硬故障**(2026-09-11 傍晚更正,原誤判為板子/RS485 掛):init 當下 `water_inlet off`/`water_status` 連 3 次失敗(`CH4 FAILED`/`water_read_fail`)是**載重/bus 忙時的間歇 Modbus 逾時**;**閒置重測 water_status ×3 + water_inlet off 全 OK、閥讀 water_inlet=0(關)**,jim 從 WEB 控制也一直正常。⇒ 不是阻斷項,根治要看 .32 在忙時的 timing。
- per user 閥邏輯:**水箱只有低水位感測**,補水=低水位觸發→開閥→定時5分關(盲時,無滿水訊號)。現水箱滿(XKC output=1)⇒ 閥邏輯上關著,full 用水箱既有水(water_pump),一趟不會抽到低水位 ⇒ **`skip` 該步安全**、init 完成到 idle。

**full 1 5 40 上機(本體 Pi:FCV_WROBOT_HOST=127.0.0.1 / FCV_CRANE_HOST=192.168.5.25 WiFi):**
- 起點 L=261=TOP、平衡走 IMU、幫浦 A ON,週期 1/1 開跑。
- step1:四顆壓力 -47/-61/-49/-52(全封);step2 -26/-56/0/-1(部分封,至少一顆≤-50 續行)。RIGHT WARN tau 16-17(過壓紮實貼牆、掃動照做)。roll 全程 <0.65、Δmax≤3,遠低於中止門檻。DEPLOY_F 提速有效:兩組合清潔 ~37s/步。
- 🔴 **user 中止於 step ~4「手臂機構異常」**(吊機降到 length 163=離底端 98cm 處)。

**手臂 log 佐證(arm_0911g.log):M1(伸出/壓上軸)異常** —
- `[M1 SAFETY] vel 超 0.4 限速 → emergency brake` 反覆,實測 vel 衝到 -1.0~-1.15 rad/s(遠超命令)。
- `[M1 HOLD] passive suspected … re-enabling`、`[M1 go_home] motor passive … re-enabling` 反覆 → 間歇失力再自動重使能。
- ⇒ **M1 超速 + 間歇失力**(加先前 M2 皮帶滑動史)。需實體檢查才可再動。

**安全收尾(per user 逐步):** arm_retract(M1 收離牆卸力、不失能)→ pusher all retract(四腳脫附)→ pump off(全繼電器 0)→ 放繩降(分段看張力,未落地)→ per user「不用放到地板,SD76=0 是安全位置」→ **retract 回頂端 length 0/0**(分段、看上限/張力,未撞頂)。機器停頂、懸吊、卸力、安全。

🔴 **待辦:**
1. **手臂 M1 機構異常**——實體檢查/修(超速+間歇失力;log 證據如上)。修好前不再驅動手臂。
2. **進水球閥(.32 CH4)載重時間歇 Modbus 逾時**(閒置/WEB 正常,非硬故障)——init 關閥步驟偶爾會 paused_on_error,skip 安全(閥本就關);要根治看 .32 忙時 timing。
3. **full 上機驗證未完成**(step ~4 中止),手臂修好後重跑 `full 1 5 40`。

📌 GUI(AI-2 已上線 `2026.09.11-1419`):Mission/Manual 兩頁「牆面高度建置」卡(①地面歸零 zero_meters ground / ②最高點設定 zero_meters top,顯示 home_ground_cm,重啟回0提醒)。

🔴 **FCV_TOP_CM / GUI 路徑 bug(AI-2 指出、已驗證,決策採 (a)):**
- cycle_test.py:266 只有 **env 不存在**時 TOP 才 fallback=home_ground_cm。但 `web_backend/server.js:288/302` **一律**送 `FCV_TOP_CM=topCm`,表單 `mp-topcm` 預設 **231** ⇒ **GUI 啟動的 full/mission 永遠帶 231(或使用者填的數),不會落到 home_ground_cm**。
- fail-safe:若送 231 而機器在真實頂端(height=261),起點檢查 `abs(261−231)=30>5` 會**擋下「起點不在頂端」**,不會用錯 TOP 亂衝 ⇒ 現況是「GUI 按 START 被拒」,非靜默錯跑。但 GUI-launched full 對非 231 的牆現在是壞的。
- ⚠️ **今天中止那趟 full TOP=261 是對的**——我從本體 Pi shell 跑、**未設 FCV_TOP_CM**,fallback=261(抬頭「TOP=261 cm / 起點 L=261」為證)。中止純因手臂 M1 異常。
- **決策 (a)**(AI-2 執行,腳本不動):server.js 拿掉 FCV_TOP_CM env、表單那格改唯讀顯示 home_ground_cm;且 **home_ground_cm=0(尚未建置)時 disable START**。

---

## 🆕 2026-09-11(傍晚)牆面高度建置慣例(per user)+ full 前置

**per user 定義的「牆面高度建置」慣例(之後每面牆都這樣做):**
1. 機器開到**地面最低點** → **SD76 設 0** ⇒ 指令 `zero_meters ground`(左右計米器歸 0、`home_ground_cm` 不動、機器不動)。此點 = 底端 height 0。
2. 機器開到**最高玻璃點** → 讀當下 SD76 繩長 = **建築物可清高度(跨距)** ⇒ 指令 `zero_meters top`(先把 `home_ground_cm` 記成 |當下 left SD76|,再在頂端歸零)。
3. 之後 full 模式即知頂端(length≈0、height=home_ground_cm=TOP)與底端,起點檢查 `abs(height−TOP)≤5` 才過。

- 📌 **對應韌體**:`Crane_control_PI/main.cpp` cmd_zero_meters(`ground`|`top`)。`home_ground_cm` **不持久化**(重啟回 0)——斷電重開後要重跑此兩點式(或用 `set_home_ground <cm>` 直接寫回上次的跨距,見 main.cpp:3785)。
- 📌 **cycle_test.py** 的座標自動判定:`home_ground_cm>0`⇒頂端歸零(height=home_ground_cm−length_left,TOP=home_ground_cm);=0⇒底端歸零(TOP 預設 231)。所以做完 `zero_meters top` 後 full 會自動用對的頂端慣例。

**本次進度(2026-09-11 傍晚,實機):**
- 開機後吊機斷電重置 → `home_ground_cm=0`、機器吊在 length_left=258(tension_valid=1、張力 60/44kg,懸空非落地)。
- per user「現在這個位置設牆面最低點」→ 已 `zero_meters ground` ⇒ SD76 左右=0(機器沒動)。✅ 底端基準立好。
- ⬜ 待:user 把機器升到**最高玻璃點**→ 我讀 SD76 + `zero_meters top` 鎖跨距 → `init`(開幫浦)→ `full 1 5 40`。
- 本體狀態:state=idle、crane/arm attached、繼電器全 OFF(無真空,要 init)、M1 收回 pos≈0、M2 在刮刀槽(0.19)、皆使能。
- 🔴 升降前要 `pusher all retract` 收吸盤(升降不刮牆);本體無推桿「伸縮位置」回讀指令(p5..p8 是真空壓力 kPa 不是位置)。

---

## 🆕 2026-09-11(午後續)上滑台使能顯示 + DI1 硬體事實(per user)

- **上滑台馬達使能顯示**:GUI 初始由「狀態未知/尚未下過使能指令」改為「**使能(上電預設)**」,移除小字「推論值/後端無回讀/失能後可手推」(AI-2 v2 `-1353`)。之後照送出的 rail_enable on/off 更新。
- 🔴 **硬體事實(DM2J_RS570.h:90-92)**:DM2J 出廠 **DI1=常閉(0x88)+未接線 ⇒ 訊號恆觸發 ⇒ 馬達永遠使能,連 Pr0.07=0(rail_enable off)都關不掉**。所以:
  - 上電預設使能 = 對(per user)。
  - ⚠️ **`rail_enable off` 在現接線下可能不生效** ⇒ 校正歸零第①步「失能→手推」可能推不動(畫面說失能、馬達仍鎖)。要真正能失能需改 DI1 接線/設定(Pr4.02→0x08)。jim 做校正若推不動就是這個。
  - 🔮 需要「真實使能回讀」時可加 rail 狀態回讀指令(驅動有 read_status 未接成指令)——等遇到再說。
- (吸盤推桿 ZDT 的「失能後可手推」保留,那是 ZDT 馬達、與上滑台 DM2J 不同。)

---

## 🆕 2026-09-11(下午)rail_jog + 幫浦重疊 3s + 上滑台 GUI(per user)

- **真空幫浦輪替重疊時間 5→3s**(`PUMP_SWAP_OVERLAP_MS`,per user;昨 30→5、今 5→3)。併聯管路 B 真空幾秒即跟上。
- 🆕 **上滑台 JOG(rail_jog,本體新指令)**——`cleaning_arm` 無關,是 body:`rail_jog fwd|rev|stop [rpm]`(rpm 預設 100、10-500)。速度模式,cmd_rail_move 的 0-130 硬限位管不到 ⇒ 兩道保護:
  - **deadman**:前端按住每 300ms 重送 fwd/rev 刷新時間戳,`rail_jog_monitor_loop_`(100ms)>600ms 沒收到自動 jog_stop(EVT `rail_jog_deadman_stop`)。
  - **行程守衛**:jog 中每 100ms 讀位置,近 130-1 或 ≤1 自動停 + `EVT rail_jog_limit`。⚠️ 座標未校正時守衛是假的(靠歸零保證)。
  - 實機驗:**fwd=往正(130)、rev=往0**(pos 50 送一次 fwd,deadman 600ms 自動停、pos→52.3)。deadman、行程守衛都通。
  - 檔:`app/WASH_ROBOT.{h,cpp}`(成員+start/stop 執行緒)、`app/wash_robot_commands.cpp`(cmd_rail_jog + monitor loop)、`command/dispatcher.cpp`。body build `70f06e56`。備份 `.prev-20260911-railjog`。
- **web GUI 上滑台(AI-2)**:`-1156` 卡片檢討(移 rail_pos 回讀/7s 輪詢、RPM 上限 500、兩步歸零、守衛=GUI 軟限位)、`-1201` 區間測試(串 rail 三段、只擋下一段)、`-1213` **JOG 上線**(方向對、放開即停、deadman 實測通)。⚠️ AI-2 補:**按住期間暫停對本體其他輪詢**——web→本體單一共用 socket,心跳可能排在慢回覆(relay_status 逾時 20s)後、超過 600ms deadman 在手還按著時被停成一頓一頓。
  - 🔮 **伏筆**:若現場仍頓挫,下一步把 `RAIL_JOG_DEADMAN_MS` 600→1000(本體 rebuild);代價=失聯時多跑 400ms。先不動看實際。
  - 併聯重疊 3s 後,AI-2 已把 v1/v2 所有「30s/約40秒」文字改為「3s/約11秒」(`SWAP_WAIT_MS` 75s 上限留著)。

- 📌 **決策(被否決,per user):JOG 前端移除**。實測有 lag——心跳走 web→本體共用 socket、往返貼近 600ms deadman;走隧道更糟。AI-2 拔前端(v2 `-1220`)。🔴 **後端 `rail_jog` 保留**(指令+deadman+行程守衛都對,問題只在 web 延遲;要 JOG 走 `crcmd.py` 直打本體無此問題)。
  - 🔴 **教訓**:**按住式(hold-to-move)控制不適合 web 共用 socket + 隧道**。吊機 hold 鈕能用是因它在吊機 Pi 本機(127.0.0.1:5002);本體那條要過隧道就頓。日後本體「按住才動」功能先想這條。
  - AI-2 上滑台重排(同寬輸入框 64px / 同尺寸按鈕 72×34 / 使用順序列),v2 `-1220`。

- 🆕 **ZDT 推桿 RPM 可設定(per user)**——長度已有(extend_raw <cm>),RPM 原為編譯常數。`pusher/zdt_pusher extend_raw <cm> [rpm]` 與 `... retract [rpm]` 尾端加**可選 rpm**(0/省略=沿用常數 extend 600/retract 500,逐位元不變),範圍 50..1000。cmd_pusher/cmd_zdt_pusher +rpm 參數穿到 pusher_move_many_ / pusher_two_stage_retract_(後者加 rpm 預設參數,9 個既有呼叫端不變)。⚠️ dispatcher:retract 無 cm,rpm token 直接接在 action 後(分開解析,不讀成 cm)。尋封 extend 不動。實測 extend_raw 3 200→OK、3 2000→ERR、retract 300→OK。body `8f21cad8`。前端(AI-2 `-1242`)整組+單支各有 RPM 欄、50-1000 前後端雙擋。
  - 🔮 **伏筆(前端,AI-2)**:**新增輪詢要同步加 `quietRe` 靜音**,否則通訊紀錄被輪詢回覆淹(300 行約 5 分洗光)。這次補了 relay/pump/water/rail_sweep 五條(近兩天加的都漏了)。⚠️ crcmd.py 讀取逾時比 pusher retract(~2.9s)短、可能印不出回覆——測長動作用 raw socket。

- 🆕 **上滑台區間來回 rail_sweep(本體新指令,per user:改本體端、瀏覽器關了照跑完)**——原本前端串三段、斷線就停半路。`rail_sweep <from> <to> [rpm]` / `stop` / `status`。**非同步**(detached thread,仿 cmd_pump_swap_):from→to→from 三段,每段複用 cmd_rail_move、段間檢查 stop、EVT 報進度(rail_sweep_leg/done/stopped/failed)。`rail_sweep_running_` atomic 擋重複啟動。實測 20→60→20 完成 `last=done`。body build `7b7a8363`。dispatcher `rail_sweep`。
  - 前端(AI-2 `-1233`)已改接:run→一道 rail_sweep + 每 2s status + EVT 畫進度、stop 送 rail_sweep stop;**開頁先問 status → 本體正在跑就直接進進行中畫面**(機器擁有流程才做得到)。
  - 🔮 **原則(與 JOG 那條互為正反)**:**長動作要機器擁有、非同步回 OK(rail_sweep);短動作才由瀏覽器串。** 按住式(JOG)不適合 web+隧道。

- 🆕 **full 完整腳本整合雙組合清潔(per user「準備整合」+ 3 決定:①每步雙組合 ②每步噴水 ③收手臂不失能)**:
  - **body 新指令 `arm_retract`**:收回 M1 但**不失能**(送 M1 MOVETO 0 + 等回零,馬達保持通電 holding)。理由 per user:「失能只在校正位置時,其他狀態不該失能——否則手臂會亂跑」。清潔流程每組合之間收手臂用它、不用 arm_park(arm_park 會失能)。dispatcher `arm_retract`。body build `a6889e6a`。
  - **cycle_test.py full 模式**:每步清潔段由「單滾筒 arm_deploy_f+rail+arm_park」改為**輔助函式 `arm_clean_combo(slot, wet)` 呼叫兩次**——滾筒(wet:開水+滾刷+滑台掃)+ 刮刀(dry:乾掃)。維持 via-body(arm_deploy_f/arm_retract 代轉,不直連 9527)、WARN 容忍、no_wall/obstacle 跳過續行、水量守衛(空箱 bail)、用水順序(M1下才開水/滑台回0後收/收前先關水)。
  - 兩台 Pi 部署、py_compile 過、arm_retract 實測 `OK pos=-0.0002`。⚠️ **尚未上機跑完整 full 週期驗證**(需 re-init + 腳貼牆)。
  - ✅ **定案(per user):「只要機器上電,arm 都應該使能——失能手臂會亂跑」**。`cleanup()`(結尾/中止)由 arm_park 改為 `arm_retract`(同樣收 M1 離牆卸力/解過熱疑慮、但不失能)。**全 cycle_test 已無任何 arm_park 呼叫。** 唯一失能時機 = 校正位置(手轉量測時的 M2 DISABLE)。
- 🔴 **踩坑(重犯)**:為部署 rail_jog 重啟本體,送 `exit` → `cmd_shutdown` **關掉所有繼電器**(pump/閥)→ **腳吸著時失去真空、推桿未收**。昨天 work_log 已記過「重啟會關繼電器」,今天卻在腳吸附狀態下重啟。**教訓:部署重啟前,若腳吸著,先 `pusher all retract` 收腳(或確認機器落地);或改用不觸發 cmd_shutdown 的停法。** 事後 `pusher all retract` 收腳(ZDT 使能跨重啟保留、不需 pump)。

---

## 🆕 2026-09-11 DEPLOY_F 力控提速 + v1/v2 方向變更(per user)

**開機**:兩台 Pi 昨關今開,全程序復原(crane :5002 / motor_api :9527 M2 新 slot 保住 / body :5001 WiFi / web :8080+:8081 by AI-2),有線隧道開機後才 UP。

**手臂 DEPLOY_F 力控提速(per user「壓上去太慢」)**——`cleaning_arm/main_api.{h,cpp}`,motor_api rebuild:
- 診斷:慢不在力控本身。真正瓶頸是 **Step4 尋觸小步爬過空氣**(0.010rad×150ms、cmd 0.40→~0.95 約 55 步 ≈ 8s);暖啟動對「兩槽交替」失效(共用單一 last_touch、RIGHT/LEFT 幾何不同)⇒ 近乎每次冷啟動。
- 四招(per user 逐步拍板 B→A→再加):
  1. **粗壓段**(Step5,option B→A):輕觸後大步 0.030+80ms 快壓到 `DEPLOY_F_COARSE_NM`(9→**13**),收斂剩 ~1 步。
  2. **RELAX_MS 1500→900**(實測鬆弛 1 秒內完成、Step7 FINAL 再補)。
  3. **deploy_speed 0.15→0.35**(接近牆空氣段加速,安全上限 0.4;接觸後不變)。
  4. 🔴 **尋觸也改粗步**(Step4 用 COARSE_STEP/COARSE_SETTLE)——**這招才是關鍵**,空氣段 8s→1.4s。
- 結果:**deploy ~10s → ~5.5-6s、週期 51→36s**;貼牆 tau 14-15Nm 穩定、腳吸盤零失壓。備份 `~/run/motor_api.prev-20260911-{coarse,A,spd,seek,trim}`。
- 📌 未動的:尋觸接觸後仍慢壓、收斂割線法邏輯不變;只加速空氣段與粗壓。
- 🔴 **觸頂(per user「收在 36s」)**:再試砍 settle(COARSE_SETTLE 80→60、RELAX 900→700)**會 overshoot**(刮刀 tau 衝 16.75、回 WARN 中止)——tau 未鬆弛完就讀→過壓,**已退回 80/900**。速度招(deploy_speed 0.35→0.40、FINAL 300→200)保留但幾乎沒再省。**36s = 實際下限**:剩 ~5.5s deploy 是物理接近行程(收回→牆 ~2s,收回為轉槽淨空不可省)+ 粗壓 + 收斂 1 步(900ms 鬆弛是準度必需)+ 複驗。再壓就是拿品質換,不划算。
- 📌 **水平參考偏移 `fine_adjust_level_diff` = 有用、勿移除**(per user 問):它定義「L−R 差多少算水平」,本機水平時 L−R≈6(非 0),設 0 會追 L=R 把機器歪 ~3°(即昨天「停後 roll +3°」元兇)。GUI 可改名講清楚,但不可拿掉。(AI-2 已改名「水平基準 L−R」+ 一行說明、保留。)

**✅ arm 雙組合耐久 10/10 通過**(`cycle_test.py arm 10 100`,提速後):兩把刀每趟貼上、tau 滾筒 14.2~16.0 / 刮刀 14.1~16.6(偶過壓 16),**四顆腳吸盤全程 -66~-70、10 趟零失壓**,每趟 ~40s(原 51s)、總 399s。
- 🔴 **腳本改:deploy 接受 OK 或 WARN(tau≥10 即算貼牢)**——WARN=壓上了但沒收斂到 15±tol(如過壓到 16),對清潔掃動仍是有效紮實貼牆,不該中止;ERR(no_wall/obstacle/cannot_reach)仍拒。(先前 10 趟在 #2 就是被舊「只收 OK」判死。)
- 🟡 **待選**:tau 常落 15~16.6 略偏高;要嚴格收 15 可 COARSE_NM 13→11(留更多空間給精收斂),代價每趟略慢。per user 暫不收緊。
- 🔮 **下一步(per user)**:把此 arm 雙組合整合進 **full 完整測試腳本**(見下方整合計畫)。

**web GUI(AI-2,版號 `2026.09.11-1131`)**:移除 raw command(逐顆通電改走 crcmd.py)、收放繩卡片重排、水平參考偏移改名保留、運動中平衡壓一行、按鈕 3×2 放大、速度區分段鈕反白+精簡。🔴 **決策**:v1→v2 逐項移植**作廢**,v2 自訂新方式(見上一則決策)。⚠️ v2 無 raw command 後,缺的指令(recover/zero_meter…)去 :8080 或 crcmd.py。
**web GUI 上滑台卡片(AI-2,版號 `2026.09.11-1156`)**:標題只留「上滑台」;移除 rail_pos 回讀/7s 輪詢/座標可信度列/rail_cfg_soft_enable 列;RPM 上限修 1000→**500**(本體實限 `cmd_rail_move`);校正歸零改**兩步鈕**(rail_enable off→手推左端硬限位→rail_enable on+rail_zero,與 cmd_rail_enable 註解的三道指令一致、後端不動)。
- 🔮 **伏筆/決策**:**行程守衛「可設定」落在 GUI 端軟限位**(localStorage/每瀏覽器一份),因**本體沒有 `set_rail_max` 指令**、硬限位 `ARM_RAIL_TRAVEL_MAX_CM=130` 不受影響。若日後要真正可設定的**硬限位**,需在本體加 `set_rail_max`(本體側工作)。

📌 **決策(被否決,per user)**:原打算把 v1 功能**逐項移植**進 v2(AI-2 已做完整對照 `.claude/reference/v1_v2_feature_map.md`、分 A~I 九組)。jim 改方向:**「不用看 V1,V2 自己定義新方式」** ⇒ **不做逐項移植**,v2 依實際作業需要自訂功能、不追 v1 清單。
- ⇒ 「切換前問 jim 日常按哪幾項」待辦**作廢**;`v1_v2_feature_map.md` 降為參考文件、§3 待移植表不再是待辦。
- tier-1 安全五項(昨補)**保留**(v2 自己該有,與 v1 無關)。v1 續跑 :8080 暫不移除;v2 何時成唯一 GUI 改由「v2 自訂功能到哪」決定,不再由 v1 parity 決定。
- (留這行免得下次盤點又把那九組當待辦拿出來排。)

---


## ✅ 2026-09-10 傍晚 手臂清潔耐久 + M2 slot 重新定位(per user,實機)

**背景**:兩台 Pi 斷電重開後復原(crane :5002 / body :5001 WiFi / motor_api :9527 / web:8080+8081 由 AI-2),init + ZDT 收 0。

**M2 選刀馬達**:
- 斷電前 M2 出現**間歇 passive/fault 弱出力**(卡在中心/刮刀推不過去、tau 忽有忽無)——**電源重置即清除**(下次再遇可先試電源重置)。
- 🔴 舊 slot 值(刮刀 -1.0115 / 滾筒 +0.5316)**失效**:`LR_SLOT LEFT` 一致卡 -0.65 到不了。疑似高扭力硬撐後 M2 傳動(皮帶/聯軸器)滑動,編碼器↔實體對應改變(位移不均勻、auto-home 滾筒 +0.55 vs 手量 +0.86 差 18°)。
- per user 手轉實測(失能 + `M2 MIT 0 0 0 0 0` 刷新才讀得到活值)→ **新值:刮刀 +0.1913 / 滾筒 +0.8558**,寫進 `cleaning_arm/main_api.h`(RAD + FROM_STOP 各一組,FROM_STOP=RAD−0.7204)+ rebuild motor_api(compile.sh)。改後兩把刀都貼得到牆(各 ~14.8Nm)。
- 🔴 **待辦/風險**:若之後貼牆吃高扭力**又滑**,這兩值會再跑掉,屆時要先解決 M2 傳動滑動(已寫進 main_api.h 註解)。備份 `~/run/motor_api.prev-20260910-slots`。

**手臂操作守則(per user,已寫進 run_arm)**:🔴 M1 下去才開水、滑台回 0 後 M1 拉回、M1 拉回前先關水、**切 M2 槽前 M1 必已收回**(LR_SLOT/DEPLOY 有「M1 未離開玻璃就拒絕」守衛兜底)。

**cycle_test.py arm 模式大改**(`b54..→7ce48ae6`):從舊單槽 `DEPLOY 520`(位置、會失敗)改成**雙組合/週期 + 力控 DEPLOY_F 15**:滾筒(切槽→deploy→開水→滾刷→滑台0-100-0→關水→M1拉回)+ 刮刀(切槽→deploy→滑台0-100-0→M1拉回);前置檢查腳吸盤已吸牢、每組合後檢查 <-50kPa。**兩個位置參數守衛已修**(arm/crane 模式先前會被 full 模式 `int(argv)` 解析炸掉——arm 從沒實機跑過才浮現)。
⚠️ **踩坑**:`cycle_test.py` 的 WROBOT 預設是真機 IP,**在 WSL 上「乾跑」= 連真機跑真動作**(我誤跑一次、啟動了手臂+噴水,已收拾)。沒有「本機乾跑」這回事。

**10 趟耐久結果**:**9 趟全過**(兩把刀 14.6~14.8Nm、滑台掃 6.2s、腳吸盤全程 -66~-70 **零失壓**、每趟 ~50s)。第 10 趟因**水箱抽乾**(water_full=0)被安全守衛擋下(關水+收 M1,沒乾抽)——非機構問題。**水箱容量 ≈ 9 噴水趟**,跑更多需中途補水。
🎁 **A/B 幫浦輪替實戰驗證**:耐久跑途中(accum 到 30 分)**自動 A→B 換手成功**,而腳吸盤全程 -66~-70 沒掉 ⇒ make-before-break 在真實負載下無縫、真空沒斷。收尾時 `active=B`。

**收尾機器狀態**:M1 收回、M2 滾筒、腳吸盤仍吸著(閥+幫浦B 開)、噴水/刷 off、水箱空。→ **2026-09-10 收工**:jim 關機器,crane Pi(.25)由我 `shutdown -h now` 關妥,body Pi(.26)jim 自關(.26 sudo 需密碼、無 key.txt,照規範通知使用者)。

### 前端/web(AI-2 今日,統一由本體 session 記)

🔴 **待辦**:
- **v2 尚未取代 v1,:8080 仍是 v1**。tier-1 安全五項已補齊,但 tier-2/3 還缺:腳本系統(save/load/list/delete/run_script/run_saved)、流程控制(pause/resume/continue/skip/recover/reset)、重心校正、手臂整合清掃、各種歸零(計米/張力/IMU)、attach/detach、imu_guard、align/realign/return_home、張力 scale、設定持久化。🔴 **切換前先問 jim 這些他日常按哪幾項**(不必全補,但不能切過去才發現少了每天用的)。
- v1 備份(切換用):本機 `/mnt/agent_ai/.tmp/fcv2-web-v1-backup-2026-09-10.tar.gz`、吊機 Pi `~/web-v1-backup-2026-09-10.tar.gz`。⚠️ .tmp 不進雲端鏡像。
- **本體 emergency_stop 按鈕從沒實際按過** —— 等 jim 安排安全時機驗證按鈕真的打到 body。
- v1 無版號機制,改了要**強制重新整理**才看得到。

📌 **決策(含被否決)**:
- **Manual 完全不鎖**(jim 拍板):機制保留為關掉的 flag,`setCraneLock()` 改回 `var locked=misRunning` 一行即恢復。CLI cycle_test 偵測「不補」(同方向)。
- 🔴 **後端刻意不消 `cmd_pump_swap_` 微秒競態窗**:消它要把 acquire 移出 pump_swap_、風險大於收益,且前端等待狀態機(先見 swapping=1 再見 0 + 75s 上限)本來就得存在(扛舊韌體無 swapping 欄位/切換中斷線)。**不寫下來下個人會當 bug 去補。**
- swap 鈕不照 `OK swap_started` 放開(那只是「開始」,切換要 40s)。
- **PQW CH4 送語意 `water_pump` 不送 `relay 4`**(通道搬過 6→14→4,relay 4 綁號、下次搬會靜默打錯,CH6 是破真空閥)。
- **輪替門檻用套用鈕不用 debounce 即時送**(即時送會把打字中途值送出觸發真輪替;且 debounce 版擋掉 0=停用這個合法值)。
- **確認窗分級**:急停不加/RESET 一道(鬆真空)/SHUTDOWN 兩道(脫附)。
- 跨裝置自動急停 = 還原 v1 既有 fail-safe,非新自主破壞行為。

🔮 **伏筆**:
- `vac`+`vacSource()` 是 v2 真空源判定**唯一事實來源**(三路徑寫入、一處讀出);日後別再各自 grep `ch2=1`(那正是今天修 6 處的原因)。
- 救援收繩走 `cmd_manual`(無保護)、一般收放繩走 `cmd_hold`(有保護),**刻意不合併**(前者價值就在繞過保護、保護壞時還能用)。
- `data-setgo` 加了 `data-tgt="wr"`,本體 `set_*` 可直接沿用;沒標的逐字不變。

⚠️ **踩坑**:
- 🔴 跨裝置自動急停初版判準錯(只看「對面現在在線」⇒ 吊機本來沒接時開網頁會 3s 後自動送 estop 給 body);正解是 `wasBothUp`(掉線**前**兩台都在)。
- 🔴 v1「水箱泵浦(CH6)」標籤停在最早版、而 CH6 現在是破真空閥 —— **通道搬家必須同步搬標籤**。
- `pump status` 舊韌體回 `ERR expected_on_or_off` 非 `unknown_cmd`(pump 指令一直在、只是不認 status)。
- `chA=-1` 是讀不到非關著;三態 null 不可當 false。
- `pgrep -f "node server.js"` 命中自己 ssh 指令列 → 用 `pgrep -af`;`cd X && ...` 的 cd 失敗會短路但後續照印「通過」→ 一律絕對路徑;紅燈先確認紅在哪(harness 自身 bug 差點去改沒壞的碼)。

📌 web 收尾:切換前 :8080=v1 / :8081=v2(版號 `2026.09.10-2040`);crane Pi 已關機,web 隨之下線,下次開機需重起 node。工作區未 commit(jim 長期偏好)。

---


## ✅ 2026-09-10 18:5x 噴水馬達 CH4 修正 + crane 切 WiFi(per user)

- 🔴 **per user:本體 PQW CH4 = 手臂噴水加壓馬達**。程式原本噴水泵寫 `CH_WATER_PUMP=14`,但
  (1) PQW 板是 8CH(`PQW_TOTAL_CH=8`)⇒ CH14 超範圍、位址不到;(2) 舊註解「尚未接管路」⇒ 沒人發現它打不到。
  ⇒ 改 `CH_WATER_PUMP` **14 → 4**(所有呼叫端用常數,自動跟著改)。CH4 不撞已指派(1閥/2泵A/3泵B/5刷/6破真空),尤其 **≠ CH6 破真空**(同號會在清洗時開破真空→脫落)。`relay_status` 標籤改 `ch4=噴水加壓馬達(手臂)`。
- 部署:scp 2 檔(md5 驗)→ build(16/16)→ `grep -a` 驗中文字串在(`strings` 不吐 UTF-8,改用 grep -a)→ 備份 `.prev-20260910-ch4` → 停(15s 退)→ 換(md5 `e6b2af9d`)→ 重啟。
- 🔴 **crane 連線改走 WiFi**:重啟帶 `FCV_EP_CRANE_HOST=192.168.5.25`(有覆蓋就完全不探測)。log:`位址由環境變數覆蓋 = 192.168.5.25`、`[OK] crane 192.168.5.25:5002`、estop/IMU 通道都連上。(之前重啟兩次都因無覆蓋自動探測選了有線 tunnel。)
- 實測:`relay_status` → `ch4=噴水加壓馬達(手臂)` ✅;`pump status`/`water_level` 正常。
- 🔴 **水位計 per user 裝在水箱最低位置** ⇒ 它是「水箱空了沒」偵測,不是「滿了沒」。`water_full=1`=還有水(蓋過最低點)、`=0`=空了。
  - ⚠️ **操作**:water_full=0 時**不可開 CH4 噴水馬達**(乾抽)。放水觸發驗過:乾 rssi<1700 / 濕 rssi>4800、water_full 翻 1,乾濕分明、不誤觸發。
  - 🟡 **待確認(未動)**:refill 邏輯把 `out==1` 當「滿了→停補水」,最低點安裝下語意是反的(水碰到底就判滿)。若這顆是低水位/空箱用途,補水判斷要改。等 user 定。
- 備份鏈:`.prev-20260910-pump`(輪替版)→ `.prev-20260910-ch4`(輪替+CH4,現役前一版)。

---

## ✅ 2026-09-10 18:2x 幫浦 A/B 輪替 — **整批部署上線(per user 拍板)**

jim 確認「機器在地面/安全」後整批同批上機,端到端已驗:
- **本體**:scp 4 檔(md5 逐位元驗)→ `build_body.sh`(16/16 objs)→ strings 驗新字串在、現役無 → 備份 `.prev-20260910-pump` → fifo `echo exit` 優雅停(8s 行程退乾淨,非只關埠)→ 換檔(md5 `90386f7b`)→ 重啟。啟動 log 印 `[OK] pump A/B rotation started (每 30 分輪替，0=停用)`。
- **實測**(WiFi .5.26:5001):`pump status` → `OK active=A accum_min=0 rotate_min=30 auto_rotate=1 counting=0 swapping=0 chA=0 chB=0`;`relay_status` 標 `ch3=pumpB(A/B輪替)`。(重啟後未 init ⇒ counting=0/幫浦 off,正常。)
- **`cycle_test.py`**:兩台 Pi md5 `130eac08` 一致。
- **前端(AI-2)**:部署到吊機 Pi,版號 `2026.09.10-1829`,三方 md5 `66469e44`、http=200,並用真機回覆餵進真正的 `paintPump`/`vacSource` 渲染驗過(counting=0→「未計時」、真空源 false 因未 init、`ch3=pumpB` 不污染 relay 解析)。
- 🟡 **下一觀察點**:jim 跑 `init` 後應 chA=1 counting=1 ⇒ 標籤轉「計時中」、真空源轉綠、起跑前檢查列 ✕→✓。AI-2 會看一眼。
- 🔴 **仍待實機驗**:讓它跑滿 30 分自動輪替一次 / `pump swap` 手動切(~40s)/ 故意斷 B 看切回 A。手動 swap 兩邊都先不按,等 jim 吸盤測完一起驗。
- 備份鏈:`~/run/facade_cleaning_v2.out.prev-20260910-pump`(前一版 = e8f772d,DM2J retry 那版)。要回滾:停程式 → `cp .prev-20260910-pump facade_cleaning_v2.out` → 重啟。

---

## 🆕 2026-09-10 真空幫浦 A/B 輪替(磨損平均,per user)

**需求**:A、B 兩顆真空幫浦輪替使用。目前這顆累計 ON 時間超過 30 分 → 換另一顆。
**關鍵前提(per user 2026-09-10)**:**A、B 兩條氣管併聯在同一真空管路** ⇒ 開任一顆都對整管加壓,
所以 B 不用單獨測、切換可先開後關真空不斷。切換序 per user:**開 B → 跑 30 秒 → 關 A → 真空異常就切回 A**。

**實作(本體,已 syntax-check 通過,未實機驗證)**:
- 新背景執行緒 `pump_rotate_loop_`(仿 `water_inlet_watchdog_loop_`),start() 起、stop() 停。
- 計時:`pump_active_since_ms_`/`pump_accum_ms_`;init 武裝(active=A、關 B、歸零)、`pump off`/shutdown/return_home 暫停(since=0)。
- 切換 `pump_swap_(from,to)`:**make-before-break** —— 開 to → 併聯 `PUMP_SWAP_OVERLAP_MS`(30s) → 記基準真空 → 關 from →
  `PUMP_SWAP_VERIFY_MS`(8s)內驗:基準本來就深(≤-30kPa)且關後劣化逾 `PUMP_SWAP_DEGRADE_KPA`(15kPa)⇒ **判真空異常 → 重開 from、`pump_auto_rotate_enabled_=false`(防抖,等人工重啟)**。
- 門檻 `g_pump_rotate_ms_` 預設 30 分,**0=停用輪替**(永遠留在目前顆=改動前行為)。
- 指令:`pump on|off`(改為作用在**目前輪替中那顆**)、`pump status`、`pump swap`(手動立即換,成功則恢復自動輪替)、`pump a|b on|off`(顯式單顆)、`set_pump_rotate_min <分>`(0=停用)。
- shutdown/return_home 改為**A、B 兩顆都關**(輪替後 B 可能正在跑);init 開 A 時**一併關 B**回到已知起點。
- `CH_PUMP_B` 從「未啟用」改註解為「用於 A/B 輪替」;`relay_status` 標籤同步。

**檔案**:`app/WASH_ROBOT.h`(成員+常數+public 指令宣告)、`app/WASH_ROBOT.cpp`(start/stop)、
`app/wash_robot_commands.cpp`(init 武裝、cmd_pump、pump_swap_/pump_rotate_loop_/best_vacuum_kpa_/cmd_pump_status_/_swap_/_ch_/set_pump_rotate_min_、shutdown、return_home)、`command/dispatcher.cpp`(pump 子指令 + set_pump_rotate_min)。

🔴 **既有邏輯要一起改(AI-2 讀碼抓出,照實際碼確認)**:前端+腳本有 6 處把真空源寫死 CH2,輪到 B 會誤判:
- `Linux_test/cycle_test.py:874-`(我的守備)—— pump 檢查只找 pumpA、chA=0 就 `sys.exit(1)` **整趟不跑**(比 GUI 鎖鈕嚴重)。**✅ 已修**:改「A 或 B 任一 ON 即放行」(names 欄推導兩顆,py_compile 過)。B 未啟用的今天行為不變。
- 前端 5 處(index.html:2021/2140/573/660/3310 + RELAY 表 2572)——AI-2 守備,已規劃:CH2 列改送 `pump a`、CH3 列改送 `pump b`;真空源判定改 **OR**(併聯 30s 窗 ch2=ch3=1 是刻意,非異常);致動器燈改用 `pump status` 的 chA/chB。

🔴 **待辦(需機器)**:
- 實機驗證:`pump status` / `pump swap` / 讓它跑滿 30 分自動輪替一次 / 故意讓 B 斷氣看是否切回 A。
- 尚未 build/部署本體(等使用者確認再 `scripts/build/build_body.sh` + 部署)。
- **WEB(AI-2)**:卡片(Monitor 頁真空量規旁)「目前運轉 A/B + 累計 N/30 分 + 立即輪替鈕 + auto_rotate=0 紅字警」——AI-2 **正在寫、寫好 hold 不部署**。後端指令規格已給。
- 📌 **部署協定**:jim 拍板 → 我 build+部署本體 → 通知 AI-2 → AI-2 上線前端 → 兩邊一起實測。第 5 條 `pump a|b` 在本體 build 前送出會 `ERR usage`,故整包同一時間點上。

📌 **決策**:累計**不跨重開機**(記憶體內,init 回 A 起算;30 分週期單場作業內就會輪到,夠用)。要跨電源保存再說。
📌 **決策**:切換偵測「真空異常」用**相對劣化**(關前 vs 關後),cup 未密封時基準≈大氣→不誤判;只有本來撐著真空才會觸發切回。
📌 **決策(AI-2 指出後改)**:`cmd_pump_swap_` 改**非同步** —— pump_swap_ 同步 ~40s(併聯 30s+驗真空 8s),web 對本體只有一條共用 socket,同步會卡畫面 40s 且撞 fire() 的 30s 逾時。現起 detached thread、立回 `OK swap_started`,結果靠 EVT + `pump status`。加 `swapping` 欄位 + `pump_swap_in_progress_` 互斥鎖(自動 loop 與手動 swap 不同時動繼電器)。自動輪替本來就在背景 loop、不阻塞。
   **swap 契約**(前後端共識):`OK swap_started`=開始非完成;`swapping` 由 pump_swap_ 內部 guard 設(自動+手動兩路徑共用,故自動切換中前端鈕也鎖);`swapping=1` 涵蓋併聯 30s+驗真空 8s 全程;並發 → `ERR swap_in_progress`(命令層早擋)或 `pump_swap_busy`(內層 guard)。前端放開鈕條件=**先見 swapping=1 再見 0** + 75s 上限(旗標在 detached thread 內才設,回覆早於它一個微秒窗;且要扛舊韌體無此欄位/連線斷)。**後端不消那個窗**:會與內部 guard 的 exchange 對撞,且前端那套本就必須存在,消了是零淨值。

---

## 🆕 2026-09-10 腳本整併:cycle_test.py 統一 runner(per user)

6 支週期/運動腳本 → **1 支 `cycle_test.py`,用第一個位置參數選模式**:

| 指令 | 做什麼 | 取代 |
|---|---|---|
| `cycle_test.py modes` | 列出所有模式與參數 | — |
| `cycle_test.py full [cycles] [steps] [step_cm] [roll_trip] [diff_trip]` | 完整清潔週期 | (原本體) |
| `cycle_test.py crane [trips]` | 純吊機頂↔底來回 + 姿態統計 | `mission_run.py` |
| `cycle_test.py arm [cycles] [rail_cm] [slot]` | 手臂清潔動作耐久 | `cyc.py` / `arm_cycle.py` |

🔴 **向後相容保留**:第一參數是數字(非模式字)= full,所以 `cycle_test.py 1 5 40` 與 Mission 後端(不帶模式)照舊。
🎯 **crane 模式複用 `monitored_crane_move`** ⇒ 讀 `raw_x`、座標由 `resolve_zero_convention` 自動判定 ——
   **mission_run.py 原有的「座標慣例過期、讀 roll 非 raw_x」兩個 bug 在此從結構上不存在**(共用同一份 height/監看)。
🔴 **full 模式程式碼逐字未改**:模式分派放在 full 主程式之前,非 full 就先跑完 sys.exit,full 直接落下去。

**檔案處置**:
- `cyc10.py` / `cyc20.py` —— **刪除**(兩者逐位元相同、被參數化的 cyc 取代,零損失,git 有)。
- `mission_run.py` / `cyc.py` / `arm_cycle.py` —— **轉成轉址墓碑**(執行會印「改用 cycle_test.py <mode>」並 exit;原始碼在 git)。
- ⚠️ **crane / arm 模式尚未實機驗證**(需機器)。full 模式今日已多趟驗過。墓碑保留轉址,原檔在 git,模式若有 bug 可 `git show` 取回對照。

📌 兩台 Pi 已部署(md5 `71143132`)。純 `Linux_test/` Python,與前端/C++ 無關。

---


## 🆕 2026-09-10 待辦：DM2J 上滑台 `rail_move` 間歇失敗,驅動要加重試(per user)

**現象**:2026-09-10 WiFi 週期測試步2 `rail 0 復位失敗：ERR rail_move_command_failed`,整輪 bail。
**性質**:間歇。同一趟步1 的 rail 正常;bail 後手動重送 `rail 0` **一次就成功**(rail_cm=0)。
**與 WiFi 無關**:DM2J 在本體本地 `.20` bus(與 ZDT 推桿 5-8 + PQW 共線),cycle_test 走 127.0.0.1 打它,不經隧道。是 `.20` bus 的間歇 hiccup(可能與同 bus 的 ZDT/PQW 交易爭用有關)。

**來源**:`app/wash_robot_commands.cpp:438` —— `PR_move_cm_nowait(...)` 回 true(送出即失敗)→ `ERR rail_move_command_failed`。**送出階段就失敗,不是動作中逾時**(逾時是另一條 `rail_move_not_confirmed`)。

🔴 **per user:修改驅動增加重試。** 兩個落點:
- **(a) 驅動層**(per user 指定「改驅動」):`user_lib/DM2J_RS570` 的 `PR_move_cm_nowait`/底層 `txn_frame_` 送出無回應時重試 N 次(比照其他驅動的 3 次)。
- (b) 命令層:`cmd_rail_move` 的 `PR_move_cm_nowait` 失敗時重試——較淺,但擋不到 driver 其他呼叫點。
⚠️ **重試要有上限 + 印出來**(哪一次成功),別靜默;`rail_move_not_confirmed`(動作中逾時)那條**不要**一起重試——那時滑台可能還在動,重送會疊指令。
📌 需重建本體 binary + 重啟本體才生效,所以先記著,擇機做。

---


## 🆕 2026-09-10 待辦：Manual 手臂 M1/M2「靠上/離開」+ DEPLOY_F 雙上限（per user 已定案設計，未實作）

**設計已與 per user 討論定案，尚未寫碼、機器未修好無法實機驗。**

**靠上玻璃** —— 在 `DEPLOY_F` 上加距離參數，變成**壓力/距離雙上限，誰先到停誰**：
```
DEPLOY_F <目標壓力N·m> <slot> [預估距離mm]
```
- 壓力先到 → 停，`OK stopped=pressure`
- **距離先到 → 停在那、接受當下壓力、照樣清潔**（per user 選 A：15 N·m 非硬需求，停在預估距離比為差幾 N·m 頂到硬上限安全），`OK stopped=distance`（tau 可 < 目標，不算失敗）
- 既有兩守衛保留：接觸點 < `THETA_MIN(0.565)` → `obstacle`；沒碰到走到 `THETA_MAX(1.10)` → `no_wall`
- 距離單位沿用 `wall_mm`（現有幾何都用它，`main_api.h` 的 wall_mm→theta 映射）
- 預估距離可用上次接觸點 `deploy_f_last_touch_cmd_` 預填

**離開玻璃** —— `PARK`，完全收回，無參數（per user：完全收）。

**落地分工**：
- 🔴 **motor_api（`cleaning_arm/main_api.cpp`）＝我**：DEPLOY_F 加距離上限分支、回覆加 `stopped=pressure|distance`。動到力控尋觸迴圈 + 壓玻璃，**寫完 syntax check、標未經硬體驗證、先不部署**，等機器修好。
- **Manual UI ＝ AI-2**：靠上鈕 + 目標壓力框(預設15) + 預估距離框(上次接觸點預填) + 離開鈕 + slot 選擇(LEFT刮刀/CENTER/RIGHT滾筒)。

📌 為什麼雙上限是對的：只有壓力 → 壓力到不了時力控一路摸到硬上限（走太遠）；只有距離 → 玻璃比預估近時壓爆。兩個誰先到 = 各擋一種風險。

---


## 🔴🔴 2026-09-09：兩台 Pi 路徑大搬家 —— **本檔以下所有 `~/bringup` 都是歷史**

`~/bringup/` 已不存在。新配置：

| 路徑 | 放什麼 |
|---|---|
| `~/projects/facade_cleaning_v2/` | **原始碼**（與 repo 同構，頂層只有目錄） |
| `~/run/` | 執行檔、`logs/`、FIFO、`cycle_logs/`、回退點、`bench/` |
| `~/archive_20260909/` | 舊樹 tar.gz |

📌 **本檔的 58 處 `~/bringup` 刻意保留不改** —— 那些是當時的事實，改掉等於竄改歷史。
操作路徑一律以 `.claude/runbook.md` 檔首的搬家公告為準；細節見 `changelog.md [2026-09-09c5]`。

---

## 🆕 2026-09-09 新開待辦：roll 隨週期累積漂移（+0.187°/週期）

10 週期實測：停後 roll 由第 1 週期 **−0.03°** 漂到第 8/9 週期 **+2.13/+2.11°**，
線性趨勢 **+0.187 °/週期**、9 個週期累積 **+1.68°** ——
🔴 **而同一時間 `L−R` 的中位數是 0**，計米器路徑一直精準維持在目標上。
⇒ 漂的是「計米器讀數 ↔ 物理水平」的關係（換算約 **0.17cm/週期** 的左右累積誤差），不是控制迴路失效。

🔴🔴 **同日更正：這個結論下得太快。** 另有一個會產生**一模一樣資料**的解釋沒被排除——**漂的是 IMU 融合姿態、計米器其實是對的**。若為後者，結論整個翻轉：`balance_source=imu` 正把漂掉的目標餵進平衡迴路。
📌 第一個該起疑的訊號我看到卻沒追：**靜置期間 roll 由 +1.62 → +1.99，機器完全沒動** —— 計米器不動就不會累積誤差。
🎯 **2026-09-09 傍晚已解：IMU 沒有漂，原結論成立。** 判別法：加速度計以重力為基準不會漂 ⇒ 看 `roll − atan2(ay,az)`。**整天四個時點／兩個位置，偏移都在 +0.22 ± 0.6° 內**（15:50 頂端 +0.22／第二輪後頂端 +0.22／地面端靜置 −0.41），而漂移量是 **+3°／20 週期** —— 偏移根本不足以解釋。⚠️ 中途一個 +1.82° 的假訊號來自「動作剛結束、機器還在擺」時取樣，靜置後消失。⇒ **漂的是計米器讀數 ↔ 物理水平的關係，兩段式設計（計米器粗調 + IMU 細調）是必要的。** 🔬 兩輪獨立量到的漂移率 **+0.187 / +0.156 °/週期**（跨輪累積 +0.147），趨勢重現。🔧 待辦：考慮每 N 個週期插入 `roll_trim_ms` 細調，或把 IMU 細調納入回程收尾。原判別法紀錄：初步：15:50 差 **+0.22°**、第二輪運行中差 **−1.7°**，⇒ 差值本身移動了 1.9°，**傾向 IMU 在漂**，但基準粗（早先 `ay` 只有 2 位小數）且取樣在運動中，**尚不是定論**。
🔧 從現在起每輪**靜止時**記錄該差值。外推約 30 個週期會碰到 6° 中止門檻（此外推在兩種解釋下都成立）。

⚠️ **n=1 輪不當定論**（週期間散布 ±0.4°，趨勢量是它的 4 倍 ⇒ 本輪趨勢為真，但「是否每輪如此」未知）。
📌 未排除的替代解釋：鋼索伸長／捲筒排線／載重分佈。
🔧 **下一步：同條件再跑一輪 10 週期**（依本日教訓：A/B 前先量 A 自己的變異）。若確認，
   考慮在每 N 個週期插入一次 `roll_trim_ms` 細調，或把 IMU 細調納入回程收尾。

---

## 🔴🔴 2026-09-09 新開待辦：PQW 繼電器模組**會替不存在的通道編造回覆**

**實測**（機器停妥、繼電器全關、`ch14` 未接負載）：8 路的模組對 `ch9`~`ch16`
**照單全收，寫入回 `OK`、回讀也回 `1`**；對照組 `ch8`（實體存在）行為相同 ⇒ **從回應完全分不出來**。

🎯 **這解開了既有謎團**：`CH_BRUSH` 2026-07-24 誤改成 15、**實機滾筒整段不轉持續五週**才被發現。
先前只記「打到不存在的通道」，沒解釋為什麼五週沒人發現 —— **因為模組會說謊，帳面全綠。**

| # | 待辦 | 狀態 |
|---|---|---|
| 1 | ~~`app/WASH_ROBOT.h:443` `PQW_TOTAL_CH = 16` → **改回 8**~~ → ✅ **2026-09-10 已改（AI-2 線）**，見 `changelog [2026-09-10a2]`。**本機完整建置通過（16/16 objs + link），未部署／未上機**（x86-64 ≠ ARM64，證明的是編譯連結正確）。 🔴 **但這不等於 PQW 修完了**：效果只是「模組偽造 `ch14=1` 的假確認」→「best-effort 分支印一行 `readback unavailable` 但仍回成功」＝**說謊降級為沉默＋證據**，真正的修法仍是第 2 列的通道號。安全性實查：6 個 `readAllStatus()` 消費者**全部先檢查長度才索引**，且 console v2 的 `RELAY` 表本來就只列 CH1~CH8（**前端早就是對的，後端那個 16 才是異數**） | ✅ 已改 |
| 2 | `:491` `CH_WATER_PUMP = 14` 確定越界，要收回 8 以內（AI-2 板子排 CH8）。⚠️ **牽涉實體接線，不逕自改**。<br>📌 **2026-09-10 補一個會改變優先度的事實：console v2 完全沒有 water_pump 的 UI 入口**（`RELAY` 表只有 ch1~ch8，`web_backend/public_v2/index.html:2077`）⇒ 這條越界路徑**只有 raw command 打得到**，不是使用者隨時會踩的洞。**優先度可降，但不可關閉**——要決的仍是通道號（水路實體有沒有接） | 🔴 未修 |
| 3 | ~~`cmd_water_pump` 是裸 `controlRelay` + 無條件 `OK` + 無回讀~~ 🔴 **2026-09-10 更正：這段敘述已過期。** 回讀與 `pqw_set_relay_verified_` **09-07 就補上了**（見下方 09-07 那列與 `changelog [2026-09-07m2]`），`wash_robot_commands.cpp:3970` 現在會回 `ch14=`。**還沒修的只有通道號**（＝第 2 列），不是回讀。原文「即使加回讀也救不了」講的是機制，**結論仍然成立** —— 模組對 ch14 回編造值，所以現在那個 `ch14=1` 正是一個看起來完全正常的假確認 | 🔴 **通道號未修**（回讀已有） |

📌 吊機那顆宣告是對的（`Crane_control_PI/main.cpp:216 = 8`）；**過期的是本體那個 16**。
📌 對 PCB：新板「寫入越界回 `0x02`」由防呆升級為**修正現行硬體的明確缺陷**（已納入 AI-2 設計 v0.6）。

## 🔴 2026-09-09 AI-2 交接的待辦（讀碼發現，尚未處置）

交接文件已由 `tmp/` 搬到 **`.claude/handoff/`**（`tmp/` 同時被 `.gitignore` 與
`mirror-backup.sh` 的 EXCLUDES 排除 ⇒ **零備份**，三份 `.md` 原本只有一份）。

| # | 待辦 | 出處 |
|---|---|---|
| 1 | 🔴 **本體端 crane watchdog 是死碼**：`crane_last_ok_ms_` 寫 3 處**讀 0 處**、`WATCHDOG_TIMEOUT_MS` 無任何運算式使用、`crane_watchdog_loop_` 只排空張力警報。比較曾存在（`9f33c6a` 04-15），到 `4d1409c` 06-22 消失。**補回或明確刪除，二選一，現狀最壞** | `ai2-watchdog-handoff.md` |

#### 🔴 2026-09-10 複驗（AI-2 線）：上表第 1、2 列屬實，**但底下還有一層**

三件新的，都未動手（改行為的部分要等機器線測完）：

| # | 發現 | 位置 |
|---|---|---|
| A | ⚠️ **`crane_keepalive_loop_` 的存在理由已經沒了**（它的檔頭寫著「background ping … **so crane_watchdog doesn't false-abort**」，而那個 false-abort 的判斷式不存在了）。🔴 **但我 09-10 初版把它寫成「現在每秒送一次 ping、搶 `crane_mtx_`」是錯的** —— agent-ai-e9 指出並經我複驗：`:405-406` 的執行緒啟動**自 2026-05-15 就被註解掉**、`crane_keepalive_running_` 建構時即 `false` ⇒ **迴圈體從來沒有執行過，零執行期成本**。⇒ 正確的描述是**死碼餵死碼**（兩邊都不動），不是「活碼餵死消費者」。📌 **我的錯誤本身就是通則 #1 再演一次**：我讀了函式體與檔頭註解，卻沒問「誰啟動這個執行緒」——**跟「誰讀這個值」是同一個問題的另一層**。連帶：(c) 刪除的清理範圍比我估的更乾淨 | `app/WASH_ROBOT.cpp:3025`；啟動點 `:405-406`（已註解） |
| B | ⚠️ **`:2957` 的函式檔頭註解掛錯函式** —— `crane_watchdog_loop_` 頭上寫的是「Per-side retract until both L/R tension >= target_kg … Used by `bal_cal_preload_`」，那是 `crane_retract_to_weight_` 的說明。函式體被刪時註解留在原地，黏到下一個函式頭上 | `app/WASH_ROBOT.cpp:2957-2959` |
| C | ✅ **舊實作已從 git 撈回**（`6abd8c6:user_lib/WASH_ROBOT.cpp:391`）⇒ 「補回」有底稿不是重寫；`motion_active_`／`abort_flag`／`evt_` 都還在，接得回去 | — |

📌 **A 修正後仍值得留一句，但要留對的那句**：原本我想記「死碼也可以是還在跑、消費者已死的迴圈」——**那個結論建立在錯誤的前提上**。真正的收穫是**查證的順序**：判斷一個迴圈有沒有在跑，要先找它的**啟動點**，不是讀它的迴圈體。
`crane_last_ok_ms_` 的三個寫入點（含 `:2472` 專為 `motion_progress` 補的那個）**都還帶著「為了不讓 2s watchdog 誤觸發」的註解在維護** ——
⇒ 有人一直在有意識地餵它。**這正是 AI-2 通則 #1「要找誰讀這個值，不是誰寫這個值」的第二個實例。**

🔴 **傾向補回而非刪除**：刪掉等於承認「吊機斷線時本體不需要中止動作」，那是**安全決策不是清理**。
但補回會改變執行期行為（2s 沒回應就 `abort_flag`）⇒ **不要在上機測試的當天上。**
若最終選刪除，**A 的 keepalive 必須一起刪**，否則留下更難懂的殘骸。
| 2 | 🔴 `crane_retract_safe_` **零呼叫點** ⇒ `crane_cli_estop_` 整條旁路在 09-09 接上急停之前**從未於執行期建立過**。`crane_retract_to_weight_` 更是**宣告了沒定義也沒呼叫** | 同上 |
| 3 | ~~🔴 **水閥**：`set_water_inlet_` 先判失敗才 return、成功才蓋時間戳 ⇒ 開閥送到了但回覆丟了 ＝ 閥開著、watchdog 沒武裝、沒人看管~~ → ✅ **2026-09-10 已改（AI-2 線）**：先武裝後送 ＋ 獨立 `WATER_INLET_CMD_TIMEOUT_SEC = 5`，兩個獨立 hunk，見 `changelog [2026-09-10]`。**本機完整建置通過（16/16 objs + link），未部署／未上機**（x86-64 ≠ ARM64，證明的是編譯連結正確）。 🔬 兩者互動：Patch 1 讓時間戳變成「開始嘗試」，Patch 2 正好把提早量由最壞 361s 壓到 ~16s ⇒ 合法最長開閥窗 185s + 16s ＜ `WATER_INLET_OPEN_MAX_MS` 300s，**不會誤觸發強制關閥**。🟡 **§10.2 的 EVT 措辭分兩種（「確定開過」/「未確認」）未做，待拍板** —— 那是 Patch 1 唯一真成本（假 EVT 稀釋真 EVT）的解藥。❌ §10.3 B3 第四條連線未做（需 per user） | `ai2-crane-cmd-protection.md` §10 |
| 4 | 🟡 Phase 2.3 若要做走 **Option B**（拆 `record_crane_evt_`），素樸版不該上。前置是先修 `imu_push_loop_` 的無逾時 `connectToServer` —— **與 09-09 在 `crane_stop_estop_` 修的是同一個** | `ai2-peer-visibility.md` §2 |

### 📌 AI-2 帶回的通則（值得進踩坑索引）

1. **判斷保護機制在不在，要找「誰讀這個值」不是「誰寫這個值」** —— 寫入點、常數、餵食執行緒、解釋註解可以全部健在，而**比較那一行不存在**
2. **一個函式同時承載兩個關注點時，其中一個過時會把另一個一起帶走且無徵兆**（張力監控退場時把 estop 旁路一起帶走）
3. **只宣告、沒定義、沒呼叫端 —— 編譯器一個字都不會說**
4. **對有物理副作用的遠端指令，收不到回覆 ≠ 沒生效**；安全側應「**先武裝、後送出**」
5. **結論對不代表推理對**（AI-2 用「一個沒接東西的繼電器」證明模組只有 8 路，那句講的是沒接負載不是沒有通道；結論碰巧對）
6. **越界不報錯的裝置會替不存在的通道編造回覆**（見上方 PQW 實測）

7. 🔴🔴 **「看到有那段程式碼」不等於「那段程式碼會執行／會適用」—— 一定要讀到閘門那一行。**
   📌 **2026-09-10 一天之內四次，兩個 session 各兩次**，形狀完全相同：

   | # | 誰 | 讀到了什麼 | 沒讀到的閘門 | 錯誤的結論 |
   |---|---|---|---|---|
   | 1 | AI-2 | `crane_keepalive_loop_` 的函式本體與檔頭註解 | **執行緒啟動點 `:405-406` 早已註解掉** | 「它每秒送一次 ping、搶 `crane_mtx_`」 |
   | 2 | agent-ai-47 | `emergency_stop` 裡有 `set_water_inlet_(false)` | **外面包著 `if (ts != 0)`** | 「運動中關不掉水的出口是急停」（無條件） |
   | 3 | AI-2 | 自己寫的降級分支會印訊息 | **latch 沒有解除路徑** | 「停掉輪詢並說明」＝以為可接受 |
   | 4 | agent-ai-47 | 後端 `PQW_TOTAL_CH` 由 16 降為 8 | **前端 `RELAY` 表本來就只有 ch1~ch8** | 「`ch14` 每次回讀都會落進 undefined 分支」 |

   ⇒ **這是通則 #1 的推廣**：#1 說「找誰讀這個值」，#7 說**任何一段程式碼都要找它的啟用條件**
   —— 執行緒的啟動點、`if` 閘門、迴圈的解除路徑、資料的實際範圍，都是同一種東西。
   **它們全都不在你正在讀的那幾行裡，而那幾行看起來完全自洽。**

8. 🔴 **一個正確的動作配一個錯誤的理由，照單全收會在日誌裡留下一條假因果** ——
   而下一個人會拿它去推別的結論。**動作對不對與理由對不對要分開驗，兩者都要記對的那個。**
   （2026-09-10 `paintRelay` 的 `data-stale`：該做，但不是因為 `ch14`。真正的理由是
   「幀損毀但仍帶 `ch1=` 而被 `expect` 認領」，那個有前科支撐。）

## 🔴 2026-09-09 文件更正（三處，皆已改）

| 項目 | 內容 |
|---|---|
| **QX-DO24 是貼牆螺旋槳不是散熱風扇** | `WASH_ROBOT.h:578`「通訊斷掉**螺旋槳不會停**；左右螺旋槳共用 CH1」。⇒ **失效語意與其他通道相反**：失聯要**保持輸出**（停止推力＝機器離牆）。`CLAUDE.md` 五處已改 |
| **`.30`/`.31` 網關是 8N2 不是 8N1** | 六台實查。SE3 本來就設 `07-07=3=1,8,N,2`，被 SD76 逼著設 8N1；SD76 已於 05-15 移到 `.34` ⇒ 限制消失 |
| 🔴 **`MH300` 摘要 `09-04 = 8N1` 已過期** | 「配合與 SD76 共用 bus」的理由不再成立。**照舊填會與網關（8N2）不符、整段不通** ⇒ 已標紅「待重新決定」，會咬 MH300 遷移 |

📌 三條的共同點：**文件落後於硬體變更**。`.30`/`.31` 由 8N1 變 8N2 是 05-15 re-layout 帶來的，
**在兩份手冊摘要與一份架構文件裡都沒有被更新** —— 搬動裝置那次沒有回頭掃過所有引用它的文件。

---

## 📋 2026-09-10 給 AI-2 的決定（跨 session 訊息未送達，改寫在這裡）

⚠️ **agent-ai-e9 → AI-2 的訊息兩度被擋（一則過期未放行）**，所以決定改寫進本檔。AI-2 請以此為準。

| 項目 | 決定 | 理由 |
|---|---|---|
| **GUI 三顆按鈕確認彈窗**（`pwm save` / `zdt_zero` / `rail_cfg_soft_enable`） | ✅ **復原** | 判準是**不對稱性**：復原錯了，代價是多按一次確認、五行就能再拿掉；不復原錯了，**`pwm save` 燒掉的 flash 壽命（1~2 千次）回不來**。AI-2 的對照組把這變成事實而非判斷 —— `rail_zero` 掛著**一模一樣的 `data-confirm="1"` 卻還有彈窗**，唯一差別是 handler 寫在自己那裡而不在共用委派裡 ⇒ **差異來自程式碼結構，不是誰比較危險**。🔴 **`#relaygrp` 完全不碰**（per user 明確要求）。📌 **這是 agent-ai-e9 拍的板，不是 per user 要求的**，已向使用者揭露、他隨時可退 |
| **`PQW_TOTAL_CH` 16→8** | ✅ 同意已做，**同意但書** | AI-2 那句要留：**「偽造的確認比缺少確認更糟，因為前者會讓下游停止追問」**。這比「改回 8」本身有價值。📌 附帶證據：console v2 的 RELAY 表**本來就只列 CH1~CH8** ⇒ **前端早就是對的，後端那個 16 才是異數** |
| **watchdog** | 🔴 **等 per user**：補回(b) 還是刪除(c) | 補回會改變執行期行為（2s 無回應即 `abort_flag`），**是安全決策不是清理**。今天只做零行為變更的註解歸位(a)。patch 可先寫好**不要上** |
| **水泵通道號** | 🔴 **等 per user** | 需先確認**管路到底接了沒**。沒接＝純軟體改號可現在做；接了＝等現場 |

### 🔬 agent-ai-e9 查到、AI-2 要的現場事實

```
crane_retract_safe_      呼叫點 0（只有定義 :2768 + 三處註解）
read_rope_weight_estop_  唯一呼叫點 :2807，就在 crane_retract_safe_ 體內 ⇒ 一起死
crane_cli_estop_ 建立點三處：
  :374  init() 預熱                 🟢 活（今天開機實跑到）
  :2572 crane_stop_estop_ 有界自救   🟢 活（09-09 加）
  :2705 read_rope_weight_estop_     🔴 死
```
⇒ **「09-09 之前從未於執行期建立過」成立**；現在活的兩條**與 `crane_retract_safe_` 無關**，刪它不會動到急停通道。
📌 補充：`crane_keepalive_loop_` 在 `WASH_ROBOT.cpp:406` **本來就被註解掉沒在跑** ⇒ 死碼餵死消費者，兩邊都不動，(c) 的清理範圍比估的更乾淨。

### ✅ 2026-09-10 AI-2 回報：三項決定已落地 + 小工作結果

| 項目 | 狀態 |
|---|---|
| **GUI 三顆按鈕確認彈窗** | ✅ **已復原**。共用委派重新 gate 在 `data-confirm` 上 + 逐指令確認文字（`CONFIRM_MSG`）。🔴 **`#relaygrp` 未碰** |
| **watchdog (a) 註解歸位** | ✅ **已做**（零行為變更）。移除黏錯的 `crane_retract_to_weight_` 註解，補上如實的檔頭 |
| **watchdog (b) 補回 patch** | ✅ **已寫、刻意未套用** → `.claude/handoff/ai2-watchdog-restore-patch.md`。🔴 **拍板前必讀下方那條**，(b) 不是貼上去就好 |
| **`PQW_TOTAL_CH` 16→8** | ✅ 已做（見 `changelog [2026-09-10a2]`） |
| **水泵通道號** | ⏸ 未動，等 per user |

#### 🆕 2026-09-10 console v2 新增「吊機繼電器 ZS-DIO CH1–4」（agent-ai-47 指派）

✅ 已做，見 `changelog [2026-09-10a3]`。**只動前端一個檔**，未在真人瀏覽器實測。

✅ **2026-09-10 per user：CH4 的確認視窗已移除**（按了就執行，同 `#relaygrp`）。
🔴 **明確要求、不是疏漏，不要補回去。** 回讀誠實度的三個機制（等回讀才翻開關／
`data-stale`／三態模組狀態）**全部保留** —— 攔截與誠實回報是兩件事。
⚠️ **但風險轉成只靠人記得**：未武裝的閥 watchdog 與急停都不會關，
而「先武裝後送」還在 `app/` 未部署 ⇒ 在那之前「送到了但回覆遺失」沒有任何自動關閥，
**而且現在沒有 UI 會提醒這件事**。見 `changelog [2026-09-10a6]`。

🔴🔴 **一個要拍板的偏離**：`agent-ai-47` 給的是吊機端指令（CR `:5002`），
**我把控制改走本體**（`WATER_CTRL_VIA = WR`），回讀才走吊機。理由：

**自動關閥只有一個實作 —— 本體的 `water_inlet_watchdog_loop_`（300s 強制關），
而它由本體的 `set_water_inlet_()` 蓋時間戳武裝；吊機端沒有任何 watchdog。**
⇒ 直送吊機 ＝ 時間戳沒被蓋 ＝ **閥開著、沒有東西會關它**。
（v1 `public/app.js:1025` 送的也是 `washrobot`，且**另外**還有 60s 前端 auto-OFF
＋ watchdog toast ⇒ 直送吊機會讓 v2 的保護**比 v1 更少**。）

📌 **這是同日早上剛補的那個洞換一個門走進來**：早上是「回覆丟失所以沒武裝」，
這裡是「根本沒經過武裝那段程式」——**同一個後果、兩個成因，早上的修法擋不住這個**。

⚠️ 代價：走本體多一跳，`crane_cmd_` 會搶 `crane_mtx_` ⇒ 運動中可能等。
**要改回直送只需換一個具名常數，但那等於接受沒有自動關閥。**

🟡 **另外兩件待 `agent-ai-47` 處理**（屬 C++ 線，我沒動）：
① `CLAUDE.md` 驅動表仍把 `ZS_DIO_R_RLY` 列在「未使用」；
② 吊機拓樸圖仍寫「`.34` PQW slave 12 CH4 ＝ 進水球閥」，實際已是 **ZS-DIO 獨佔 `.32`**。
🔴 **而 `.32` 在本檔別處還記著是「X518 左（09-01 已移除）」⇒ 同一個 IP 換了用途，
查舊文件會查到不存在的裝置。**

#### 🔴🔴 watchdog (b) 拍板前必讀：**照舊版直接貼會誤觸發**

寫 patch 時挖到的，per user 決定 (b)/(c) 之前應該先看到這條：

**舊版能用 `WATCHDOG_TIMEOUT_MS = 2000`，是因為它自己每 500ms 送一次 `ping` 在餵。**
而 `crane_keepalive_loop_` 已於 2026-05-15 停用，理由寫在 `WASH_ROBOT.cpp:399`
（「New design: no continuous ping. Each `crane_cmd_` self-heals on fail.」）——**那個理由同樣適用於 watchdog**。

⇒ 若「補回比較、但不送 ping」，`crane_last_ok_ms_` 就只有**真的有指令往來**時才刷新，
而長時間純本體動作（ZDT 伸縮 4s+、DM2J 滑台 2-3s）期間**沒有任何東西刷新它**
⇒ **2 秒門檻極可能誤觸發 ⇒ `abort_flag` ⇒ 運動中止。**
📌 **而那正是 2026-05-15 當初加 keepalive 要解決的問題**——繞了一圈回到原點。

🎯 **這件事本身就是這條待辦的成因再演一次：把一個自洽的設計拆成兩半、只裝回一半。**
（原成因：張力監控退場時把急停旁路一起帶走。）

⇒ **建議分三步，不要一步到位**：① 先只發 EVT、不設 `abort_flag` ② 依實測 `idle_ms` 分布**重選門檻**（2s 幾乎確定太短）③ 確認不誤觸發後才接回 `abort_flag`。

#### 🔬 GUI 復原的驗證（不是「改完就算」）

- 逐 tag 審計：**帶 `data-cmd` + `data-confirm` 的剛好就是那三顆**（`pwm save`／`zdt_zero`／`rail_cfg_soft_enable`）
- `rail-zero` **有 `data-confirm` 但沒有 `data-cmd`** ⇒ 委派抓不到它，走自己的 handler ⇒ **不會變成連按兩次確認**
- 🔴 **繼電器列兩個屬性都沒有**（`RELAY.map` 只產生 `data-ch` / `data-danger`）⇒ **委派永遠攔不到繼電器**，per user 的「按了就執行」結構上就成立，不是靠我小心
- `tools/check_console.js` 全過；另把 2 個 inline script 抽出來跑 `node --check` **實際解析通過**（該工具只驗大括號平衡，不解析 JS）

#### ⑤ `web/node_modules` 可重建性 —— ✅ 可重建，**但有一個會咬人的但書**

🔴 **我沒有對兩台 Pi 下任何指令**（依指示），以下全在 WSL + scratchpad 完成，**沒有真的安裝、也沒寫進樹內**：

- ✅ `express@^4.19.2` 與 `ws@^8.18.0` **在 registry 都抓得到**（各有數十個符合的版本）
- ✅ `npm install --dry-run`（scratchpad 的 `package.json` 副本）**完整解析成功：70 個套件**
- ⚠️ **Pi 那端有沒有網路我查不到**（不能下指令）——這一項要你確認

🔴🔴 **但書：`web_backend/` 底下沒有任何 lockfile**（`package-lock.json` / `shrinkwrap` / `yarn.lock` 全無，且 git 也只追蹤 7 個檔）。
兩個相依都是 **caret 範圍**（`^4.19.2` / `^8.18.0`）⇒ **`npm install` 給的是「當下最新的相容版」，不是 Pi 上現在跑的那個版本。**
實測解析結果是 **express `4.22.2`／ws `8.21.3`** —— 已經比 `package.json` 寫的高了好幾個 minor。
⇒ **「可重建」成立，「可還原成同一份」不成立。**

🎯 **正解是把 `package-lock.json` 進版控**（幾十 KB，比 `node_modules` 小三個數量級）。
🔴 **但一定要在 Pi 上、從現有的 `node_modules` 產** —— 在這裡產只會把**現在最新的版本**釘死，
**正好丟掉那份唯一副本裡真正要保存的東西**。📌 這跟「衍生副本過期」是同一類錯誤，只是方向相反。

📌 **順帶更正一條**：`node_modules` **不在 `.gitignore` 裡**（我逐行看過）。
它不是被規則排除的，是**從來沒有人加過** ⇒ 要收進版控**沒有 gitignore 的阻礙**，不需要 `-f`。

## 🟢 2026-09-10 開工現況

四支程式已起（吊機 `:5002` / WEB `:8080`+`:8081` / 本體 `:5001`），繼電器全 0，機器在地面端。

| 項目 | 狀態 |
|---|---|
| ✅ **SD76 零點撐過整台斷電** | `L=258 R=255` 與昨天收工逐位相同 ⇒ `home_ground_cm=256` 直接可用，不必重新校正（**這條以前沒驗證過**） |
| 🔴 **Fathom-X 隧道沒通** | 本體自動退回 WiFi（`有線 192.168.1.10 探測不通 → 走 WiFi`）。兩台 eth0 都 UP、各自看得到自己那側的網關，跨不過去 ⇒ **實體項**。📌 但昨天實驗顯示隧道在 VFD 運轉時掉 90% 封包，走 WiFi 對測試反而有利 |
| ⚠️ **WEB 起不來（已修）** | `web/node_modules` 是指向我昨天刪掉的 `~/projects/web_ver2` 的 symlink。已從 tarball 取回並改成實體目錄。🔴 **它現在是唯一副本且不在版控**，`npm install` 能否還原未驗證 |
| 🔴 `level_diff` **刻意留 0 待實測** | 現在 `L−R=+3` 而 `roll=+1.46°`，依昨天斜率該是 −3.2° ⇒ **約 4.7° 偏移**，要調平後重量 |

## ✅ 2026-09-10 收工現況（13:55）

四支程式全在跑，**五條跨機連線全部走有線隧道 `192.168.1`**（per user「都走192.168.1」）：

| 鏈路 | 實走（`ss -tnp` 實抓，非設定值） |
|---|---|
| 本體 → 吊機（主 / estop bypass / IMU push） | `192.168.1.100 → 192.168.1.10:5002` ×3 |
| WEB :8080 / :8081 → 本體 | `192.168.1.10 → 192.168.1.100:5001` ×2 |
| WEB → 吊機 | `127.0.0.1:5002` —— 同一台 Pi，本來就不過網路 |

⚠️ **切之前 WEB 是漏網的**：本體是用 `FCV_EP_CRANE_HOST=192.168.1.10` 起的，
但兩個 node server 起得更早、帶的是 `WROBOT_IP=192.168.5.26`（WiFi），**切隧道時沒有跟著重起**。
📌 **通則：改鏈路要盤點「所有跨機的行程」，不是只改當時手上那一支。**
🔧 啟動腳本已固化：吊機 `~/run/start_web.sh`、本體 `~/run/start_body.sh`（IP 寫在腳本裡，不再靠手打）。

🐛 **踩到一個 shell 自殺**：`ssh <pi> 'pkill -f "node server.js"; …; node server.js …'`
—— 遠端那個 `bash -c` **自己的命令列裡就含有 `node server.js`** ⇒ pkill 連自己一起殺，
兩個 node 停掉但後面重啟那幾行沒跑到，`ss` 也沒印（ssh 回 255）。
🔧 **kill 與 start 要分成兩次 ssh**，或 start 走腳本讓 pattern 不出現在命令列上。

### 本體已部署（13:43）

binary `557eb6fd`（回退 `~/run/facade_cleaning_v2.out.prev-20260910-1343` = `3d6f3eeb`），編譯 16/16。
內含 AI-2 的五項：水閥**先武裝後送** + `WATER_INLET_CMD_TIMEOUT_SEC=5`／`PQW_TOTAL_CH` 16→8／
watchdog 註解歸位／**10 處**過時註解更正／`set_water_inlet_` 宣告處的**武裝三級門檻表**。

✅ 實測 `relay_status` 現在回 `ch1..ch8`（原本回到 16，其中 9~16 是模組**編造**的）。
✅ `arm_attached` 重啟後回到建構預設 `on`，已手動設回 `off`。

🔴 **為什麼這次要部署**：水閥的確認彈窗當天稍早依 per user 拿掉了，
而那個彈窗原本承載「未武裝的閥，watchdog 與急停**都不會**關」這句警告
⇒ 拿掉之後「先武裝後送」從「有彈窗擋著的洞」變成**唯一的防線**。

### 🐛 同一個坑，一天走進去兩次

`.claude/runbook.md` 早就寫著「FIFO 本地 console 只認 `exit`/`quit`/`status`，其餘靜默丟棄」，
我上午照樣對**吊機**送了四道 `set_*`（全丟），下午又對**本體**送了 `arm_attached off` / `relay_status`（全丟）。

🔴 第二次還多一層：本節先前**只寫了吊機**，於是「本體的應該是好的」這個假設**從來沒被檢查過**
——實際上是**同一段三行程式碼**（吊機 `main.cpp:5542`、本體 `facade_cleaning_v2/main.cpp:139-142`）。
📌 **一個坑只記在它被發現的那個地方，等於默許同族的其他地方繼續踩。**
🔧 `scripts/crcmd.py` 已支援 `host:port`，兩台都能打；runbook 已補上本體那一半。

### console v2 今日累計（AI-2 線，全部已部署到 `:8081`）

吊機繼電器面板（控制走 WR／回讀走 CR）→ latch 改降頻重試 → `data-stale` →
三顆按鈕確認窗復原 → **水閥 CH4 確認窗移除**（per user）→ **Manual 依機器重新編排**
（`▍吊機 :5002` / `▍本體 :5001` 兩段，raw command 刻意不歸段）。目前 `44620baf`。
❌ **仍未在瀏覽器看過** —— 最後這項改的是**版面**，而版面正是自動化驗證完全蓋不到的那一類。

| 項目 | 狀態 |
|---|---|
| 🎉 **吊機水閥第一次真正上線** | ZS-DIO 誤用 PQW driver 三個月，已改正。`gw_w=1 pqw_water=1`，on/off round-trip 實測通過。詳見 `changelog [2026-09-10a4]` |
| ✅ 吊機 binary | `0b8e4d0d`（回退 `~/run/crane_control_PI.out.prev-20260910-1247` = `da62fcf2`）。新增 `water_status`、`status` 補 `dev_gw_w=`/`dev_pqw_water=` |
| ✅ console v2 | AI-2 的「吊機繼電器 ZS-DIO CH1–4」面板已部署到 `:8081`（備份 `index.html.bak-20260910-1300`）。**尚未在真人瀏覽器點過** |
| ✅ 本體 | 吊機重啟後兩條通道都自己接回（斷線 9316s / 18318 次重試後 reconnect success）。`arm_attached=off` |
| ✅ 執行期參數 | `home_ground_cm=256` `motion_hz=30` `roll_correct_hz=30`（重啟後已重設） |
| ✅ IMU | `n_accel=82819` —— 昨天斷電後 `n_accel=0` 的現象**沒有復發** |
| 🔴 `level_diff` **仍是 0 且未實測** | 現在 `L=5 R=-2`（L−R=+7）而 `roll=+1.20°`。**跑任何可比較的週期測試前必須先調平再重量** |

### 🔴 新開待辦：吊機端沒有水閥 deadman

自動關閥**只有一個實作**，在**本體**（`water_inlet_watchdog_loop_`，300s），由本體的
`set_water_inlet_()` 蓋時間戳武裝。吊機端**沒有任何計時器**。

⇒ 兩個後果：
1. GUI 若直接對吊機下 `water_inlet on`（繞過本體），閥開著就**沒有任何東西會關它**
   —— 這是 AI-2 把控制路徑改走本體的原因（`changelog [2026-09-10a3]`），該決定正確、維持。
2. 🔴 **但走本體也堵不住全部**：本體被 SIGKILL（非正常退出，`stop()` 的補關跑不到）時，
   閥一樣永遠開著。**繼電器在吊機手上，deadman 就該在吊機手上。**

🔧 **建議**：吊機端補一個「收到 `water_inlet on` 起算，逾 N 秒自動關」的執行緒
（ZS-DIO 本身也有硬體通訊檢測暫存器 `0x0030`，斷線 N 秒全關 —— 可能更便宜且更可靠）。
⚠️ **未動手，等 per user 拍板。**

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
