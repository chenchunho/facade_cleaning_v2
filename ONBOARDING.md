# ONBOARDING.md — facade_cleaning_v2 洗窗機器人交接筆記

## 這份文件是什麼

這份文件是把 Claude Code 在 `facade_cleaning_v2` 這個 repo 上多個 session 累積下來的「auto memory」（存在協作者本機、不進 git 的除錯/設計筆記，共 59 篇）整理彙整而成，目的是**交接給下一位接手這個專案的協作者**。原始 memory 涵蓋這幾個月的硬體除錯、架構決策、還沒解決的坑，散落在多個獨立檔案裡；這份文件把它們按主題重新組織，方便一次讀懂。

**這是某個時間點（2026-08-13 前後）的快照，不是即時狀態。** 程式碼在這之後可能已經被繼續修改；文件裡引用的檔名、行號、函式簽名、暫存器位址都可能已經過時。**遇到這份文件跟實際程式碼/git log 不一致時，一律以程式碼與 git log 為準**，這份文件只負責告訴你「當時為什麼這樣做」的背景與踩過的坑，不是即時規格書。專案本身也有 git 追蹤的規範文件（`CLAUDE.md`、`.claude/motion_flow.md`、`.claude/runbook.md`、`.claude/work_log.md`），那些才是持續維護的權威文件；本文件是一次性的歷史知識搬遷，不會再被同步更新。

另外要知道：本專案（v2）是從前一代 `washrobot_new_PI`（v1）分岔（fork）出來的新機械架構重寫。v1 的部分記憶被判斷為「v1 特有、已過期、不適用 v2」而**沒有**收進這份文件，包括：v1 特定的硬體事故（馬達進水腐蝕）、v1 bench 網路設定、v1 的 4-gateway 拓樸、ZDT 推桿堵轉偵測缺口等。如果之後真的需要查 v1 的細節，去看 `D:\washrobot_new_PI\` 底下的程式碼與文件。

---

## ⚠️ 尚未解決 / 待處理事項

📌 **這一節的內容已於 2026-08-27 併入 `.claude/work_log.md` 最上方的「🔴 待辦總表」，
請直接去那裡看，不要在這裡維護第二份清單。**

原因：專案改為單人開發，退休了 `.claude/mailbox.md` 的協作信箱機制。原本散在
mailbox（16 條）、本節（6 條）、`work_log.md` 各日期條目的待辦段（44 條）三個地方的
未結案項目，全部合併成一張帶優先度、涉及檔案、現況（未修／已修／待查）與**原始日期**
的表，讓每一筆技術債放了多久看得見。

本節原本的 6 條都在那張表裡，來源欄標成 `ONBOARDING §1` ~ `§6`，並且在併入時逐條打開
原始碼查證過現況，細節（現象、根因、修法、判斷方法）也一併搬過去，沒有刪減：

| 原編號 | 項目 | 現況（2026-08-27 查證） |
|---|---|---|
| §1 | Crane `cmd_side_measured` 沒重置 `abort_flag`，stop 後步態永久卡死 | 🔴 未修 |
| §2 | Follower 側 IMU 校平疑似被切到 meter 模式 | 🟡 待查 |
| §3 | Crane 端偶發 `ERR meter_left_read_fail`，根因未知 | 🟡 待查 |
| §4 | D435i 深度相機戶外強光風險 | 🟢 已作廢（攝影機路線 2026-08-26/27 整個移除） |
| §5 | 規範文件（CLAUDE.md / motion_flow.md §2）硬體架構圖已跟程式碼脫節 | 🟡 未修 |
| §6 | **緊急收繩按鈕實際上沒有張力保護，跟安全性文件描述相反** | 🔴 未修（安全性） |

⚠️ 這份 ONBOARDING 其餘章節（1. 專案總覽 / 2. 硬體驅動 Firmware 踩坑知識 / 3. 工程方法）
是硬體與工程知識，**不隨協作流程退休，繼續有效**。

---

## 1. 專案總覽

`facade_cleaning_v2` 是一套高樓外牆洗窗機器人 + 吊車升降控制系統：分散式架構（washrobot Pi + crane Pi 各跑一支 C++ 主程式，透過 TCP 文字協定互相下指令），25+ 個 Modbus-TCP 硬體裝置（步進驅動、真空閥、壓力/張力/長度感測器、變頻器、IMU），多條 RS485-over-TCP bus，外加一個 Node.js Web GUI 橋接瀏覽器 WebSocket 到兩個裝置的 TCP。詳細硬體表 / 通訊協定 / 座標常數請直接看 `CLAUDE.md` 與 `.claude/motion_flow.md`（但記得上面「尚未解決事項 #5」提到這兩份文件本身也有過時的部分）。

**專案負責歸屬（重要，跟 CLAUDE.md 寫的角色分工表不同）：** CLAUDE.md 裡原本寫的「Jim = 規範+裝置驅動 / Sadie = 應用層+前端+測試工具」角色分工表**在 v2 專案已經不是實際運作方式**。Sadie 明確表示過兩次：v2 這個 repo 全部都是她一人負責，沒有 Jim 的界線——不只 `user_lib/`，包含 `cleaning_arm/main_api.cpp`（damiao 手臂馬達底層驅動服務）等任何看起來像「裝置驅動層」的檔案都是。接手後不用對任何檔案提「這可能是別人的範圍、要先確認」之類的顧慮，發現 bug 直接描述根因並改。

---

## 2. 硬體驅動 / Firmware 踩坑知識

這一節是 `user_lib/` 各裝置驅動在實機上踩過的坑——多半是「手冊寫的跟實機行為不一樣」或「跨裝置共用 bus 時的隱性 race」。**這些知識大多是 v1 時期建立的，但原理跟排查方法在 v2 遇到類似症狀時仍然通用**（v2 沿用了 ZDT、SD76、Modbus-TCP gateway 這些底層元件）。

### 2.1 ZDT 閉環步進驅動卡（驅動 SMC LEYG25 推桿，v1/v2 共用）

驅動 4 顆（v2）/9 顆（v1）推桿的 ZDT 卡有幾個不標準行為：

1. **`pos_reached` bit 不可靠**——馬達物理已停但 bit 不 set。不能只靠這個 bit 判斷完成，建議三層 fallback：`stall_flag=1` 也算停了（堵轉但至少停）；`|real_speed| ≤ 20 RPM` 連續 3 次 poll（~450ms）；`|Δreal_pos| ≤ 0.15°` 連續 3 次 poll（最可靠）。參考 `Linux_test/main.cpp zdt_group_move_sync()`。
2. **`trigger_sync_move()` 回傳值不可信**——Modbus broadcast（slave 0x00）依規範無 reply，driver 看 `readEcho` 空就回 true（看起來像 error），但實際廣播已送出、馬達會響應。呼叫端要忽略回傳值，改靠 poll 判斷 motion 是否發生。
3. **RS485-over-TCP-gateway frame 對齊問題**——連續送 Modbus 指令時 TCP buffer 殘留 echo 會干擾下一個指令的 readEcho，偶發 enable/pos_mode 失敗（不是硬體問題）。建議 per-slave 做 2-3 次 retry + 120ms back-off；群組指令時某 slave 失敗應 skip 該 slave 而非中斷整組。
4. **sync=1 + broadcast trigger pattern**——用 `motion_control_pos_mode_nowait(sync=1)` 排隊，再從任一 driver instance 呼叫 `trigger_sync_move()` 廣播觸發，所有排隊 slave 同時啟動，避免姿勢不對稱。

### 2.2 DM2J-RS570（v1 用於腳/輪/滑軌，v2 已移除硬體但除錯手法仍有參考價值）

由手冊 `doc/DM2J-RS.V1.pdf` V1.0 確認的真相，跟舊版理解（`.claude/summaries/DM2J_RS_MODBUS_SUMMARY.md`）幾乎全部相反：

- **Status register `0x1003` 是單一 16-bit register（不是跨 `0x1003`+`0x1004` 的 32-bit）**。Bit 對照：0=FAULT、1=ENABLE、2=RUN、4=CMD_DONE、5=PATH_DONE、6=HOME_DONE（**不是 bit 16**）。上電預設 bit4+bit5=1（`0x0030`）。
- 舊 driver 的 `read_status` 把 16-bit 值放進 32-bit 高位，導致所有 bit mask 檢查全錯位、內部 poll 永遠 timeout（即使馬達實際有動）。Workaround 是用位置穩定偵測（`dm2j_pair_poll_done`），跟 ZDT 的做法一樣。
- **Enable 有兩種機制**：硬體 DI1 預設 SRV-ON、常閉 (NC)，上電自動 enable；軟體強制寫 `1` 到 `0x000F`（Pr0.07）優先於 DI1。`0x1111 → 0x1801` **不是 enable，是「復位當前報警」**——舊 workaround 看起來能用是因為清 fault + DI1 硬體本來就在 enable。
- **`0x1801` 控制字對照**：`0x1111`=復位當前報警、`0x1122`=復位歷史報警、`0x2211`=存參數到 EEPROM、`0x2222`=參數初始化、`0x2233`=恢復出廠值（不是 disable）、`0x2244`=映射參數存 EEPROM、`0x4001`/`0x4002`=JOG 左/右。
- **PR Mode 欄位 bit 0-3**：`0`=路徑未配置（收 trigger 但不動）、`1`=絕對位置、`2`=相對位置、`3`=速度模式——舊註解「0=relative, 1=absolute」是顛倒的。
- `DM2J_RS570::motor_enable()/motor_disable()/save_params()` 只有 header 宣告、`.cpp` 沒實作，呼叫會 link 錯。

### 2.3 SD76-C 計米器（左右鋼索長度量測，v1/v2 共用）

- **SCAL 暫存器（`0x0014`-`0x0015`）手冊標「Counter Multiplier」但實機行為是除數（K-factor）**：`display_units = pulses ÷ (SCAL × 10^(-DP))`。SCAL 加大 → 顯示變慢長。Driver 對外 API 維持 multiplier 語意（M=display/pulses），內部換算 `M_app = 10^DP / SCAL`。
- **通訊模式（`00-16=3`）下，部分 config 暫存器（含 `0x0020` DP register）的 Modbus 寫入會被 firmware 靜默忽略**——回 OK 但值沒進 EEPROM、readback 仍是原值。要改 DP 必須從面板把 `00-16` 設成非 3、改完再設回 3。**Driver `setEffectiveScale` 不動 DP**，只處理 SCAL（SCAL 寫確認可以走 Modbus）；超出範圍清楚回 ERR + 提示面板操作步驟。
- 換型號 / firmware 升級要重新驗證這兩條結論——目前結論只代表這台 bench 機器。

### 2.4 SE3-210 變頻器（v1/bench 遺留的士林變頻器知識；v2 定案改用 Delta MH300，但移植時可對照）

- **Modbus CU mode latch**：面板 `P.79=3`（通訊模式）不夠，還要透過 Modbus **自己寫 `H1000=0`** 才能開放 `H1001` 運轉指令通道（手冊 §7-3 例一）。寫入後**要等 ~50-150ms** 才真正 latch，太快送 `H1001` 會被靜默拒絕——driver `ensureCuMode_()` 加了 150ms sleep + `cu_mode_set_` sticky cache。運轉中**不能**重寫 `H1000`（會被拒絕、干擾 bus）。
- **`07-10`（P.153）只有兩個選項**：`0`=報警並空轉停車（保護開啟）、`1`=不報警並繼續運行（**不是**減速停止，跟其他 Shihlin 機型命名完全相反，之前有查錯資料誤導設定）。搭配 `07-09`(P.53, 通訊間隔容許時間 2.0s) × `07-08`(P.52, 容許異常次數 2) = 4 秒無 Modbus 就異警空轉停。**必須配 keepalive thread**（`se3_keepalive_loop`，每 1 秒讀 `0x1001`），否則 Freeze/fine-adjust/hold 等靜默期會誤觸發 OPT。
- **啟動時 OPT chicken-and-egg**：SE3 上電殘留 OPT alarm 時，init probe 會失敗、init 判定 device 不存在、keepalive 因此跳過該邊、永遠沒人清 alarm；手動面板清 alarm 也會被新一輪 init 的短窗口再度觸發。**解法：driver init Mode B 自己加 retry + clearAlarm fallback**（最多幾次、每次失敗嘗試 `clearAlarm()` + 200ms sleep）。
- **兩台 SE3 必須各自獨立 TCP session**（`cli_A_se3_L`/`cli_A_se3_R`），不能共用一條 `cli_A`——單一連線會發生 frame contamination（一台的 reply 被另一台的 driver 誤讀），導致 init 失敗、進 chicken-and-egg。
- **DC 注入煞車**（重物吊車關鍵）：`10-00`(P.10, 動作頻率, 出廠3.00Hz) / `10-01`(P.11, 動作時間, 出廠0.5s) / `10-02`(P.12, 動作電壓, 出廠4%/2%)。**三者任一=0 → DC 制動完全不動作 → 馬達自由停車**。重物建議：`10-01=1.5~2.0s`、`10-02=10~15%`。改參數要先 `00-16=0` 脫離通訊模式，改完設回 `3`。
- **P.7/P.8/P.20/P.21（加減速時間相關）左右兩台必須完全一致**，否則同步停車會看到一邊明顯比較慢（純軟體層修完 race 之後，這是純物理層問題，改程式沒用）。數學驗算：hold 20Hz、P.20=50Hz、P.21=0 時，`滑停秒數 = 20/50 × P.8`，P.8 差 3 秒會造成 1.2 秒的停車時間差，跟實測 0.5-2s 吻合。

### 2.5 Modbus-TCP gateway 共通問題（跨所有 driver）

- **Stale buffer 症狀**：USR-TCP232 是「透傳」gateway，RS485 上任何 byte 都會被丟進 TCP socket。舊 transaction 逾時後遲到的 reply、或共用 bus 上別的裝置的 reply，都可能殘留在 kernel recv buffer，下次 `recv()` 一次性收到「stale + new」，driver 只檢查前幾 bytes 容易誤判成 `bad reply len=N`。受影響的 driver（`receiveData(...,256,...)` pattern）：`SE3_inverter`、`CLV900_inverter`、`SD76_length_meters`、`JC_100_METER`。
  - **最終解法**：`TCP_client` 加 `sendAndReceive(tx, txLen, rx, rxSize, sendT, recvT)`，整個 Modbus transaction 在**一個 lock_guard 範圍內**做 drain→send→recv，並發 caller 完全 sequential 排隊。上述 4 個 driver 已遷移。**未遷移**（目前無並發場景，但架構改動時要注意）：DM2J、ZDT、PQW、DSZL、DY500、XKC、ZS_DIO。
- **`TCP_client` Linux 殭屍連線 bug（2026-07-23 修）**：Linux 上 `available()` 用 `ioctl FIONREAD` 偵測不到對方正常關閉連線（graceful close 時 FIONREAD 回 0，跟「單純沒資料」分不清），`sendData`/`receiveData` 失敗時也沒把 `connected` 設回 false，導致 `reconnectLoop()` 永遠不觸發重連，client 卡死在假的「已連線」殭屍狀態。已修（Linux 分支比照 Windows 改用 `recv(MSG_PEEK)`）。**這個 bug 影響所有用 `TCP_client` 的裝置**（depth_cli_/arm_cli_/crane_cli_/所有 Modbus gateway 連線），不只是被順帶發現它的那個功能。
- **Driver init Mode B（共用外部 TCP_client）預設不做 probe**：TCP 連到 gateway 成功（永遠連得上）就被誤判成「device alive」，實際硬體不存在也不會被抓到，後續 polling loop 每次都要等滿 recv timeout 才失敗，佔用共用 socket mutex 拖慢同 gateway 上其他裝置。已修：`SD76_length_meters`、`SE3_inverter` 的 Mode B init 加了最小 Modbus probe。**可能還有同問題的 driver**（未驗證）：`DSZL_107`、`CLV900_inverter`、`DY_500_weight_sensor`、`PQW_IO_16O_RLY`、`JC_100_METER`。判斷方法：拔掉硬體但啟動還顯示 `[OK]` 就要懷疑這個 pattern。

### 2.6 Crane RS485 bus 格式統一 8N1

SD76-C（面板沒有 UART 設定入口，鎖死 8N1）+ SE3（RTU 模式沒有 8N1 選項，只能 8N2/8E1/8O1）沒有正規共同格式。解法：USR gateway 統一設 8N1，靠 **SE3 UART 對收端寬容**——SE3 送出時是 8N2（多 1 個 stop bit），USR 收時把多出的 stop bit 當 inter-frame idle gap，雙向都通。長時間 stress test 沒做，如遇隨機 CRC error，考慮把 SE3 跟 SD76 分到不同 USR 物理隔離。

### 2.7 X518 (DSZL-107) 張力感測採集板架構問題

bench 上的 X518 是**自帶 Ethernet 口**版本，原生講 Modbus TCP（port 502），但 `user_lib/DSZL_107.cpp` driver 寫的是 Modbus RTU 框架（CRC16 + slave ID byte），假設架構是「RS485 → USR-TCP232 透傳」——跟 CLAUDE.md 系統表描述不一致，接手要先確認實際拓樸是哪一種（RTU 中繼 vs 改 driver 支援 TCP vs driver 加 mode flag）。

其他細節：X518 物理上只有 **2 通道**（CH1+CH2），手冊標題「双通道」；曾經誤讀成 8 通道（讀到隔壁 register 的垃圾值）。`0xA20` 是多功能命令暫存器：寫1=zero CH1、寫2=zero CH2、寫7=zero all、**寫40（decimal）=保存參數到 flash**——任何參數修改/校零都要接著寫 40 才會持久化（`DSZL_107::save_params()` 已封裝，`Crane_control_PI` 在校零/設定單位後會呼叫）。出廠 IP `192.168.1.120`、port 502、mode register `0x644`（1=Modbus TCP）。手冊路徑：`D:\洗窗戶機器人\電控設備資料\張力感測器\x518多通道数据采集器操作手册v1.1.pdf`（中文 PDF 抽取有 cmap 問題，欄位對照務必用實機 dump 交叉確認）。

### 2.8 真空吸盤 seal 最佳實踐順序

實測歸納的兩個 pattern，寫吸附/步伐序列一律照這個順序：

**Pattern 1 — valve-before-extend**：先開電磁閥（真空開始抽）→ 再 extend pushers 到牆面，吸盤碰牆瞬間已有負壓，seal 立刻形成。舊做法（extend 完再開閥）會讓空氣先跑進邊緣導致漏氣。

**Pattern 2 — staged extend（兩階段推桿）**：先 extend 到目標一半 → pause 1000ms（讓真空閥建立負壓）→ 再 extend 到全目標（負壓協助 seal）。**Stage 2 必須無條件執行**，不能因為 Stage 1 的 pos_reached 判斷不穩就 early return（否則馬達卡在半程）。v2 沿用同一套 pattern（見第 6 節）。

---

## 3. 工程方法 / Pattern（v1 建立、v2 繼續適用的通用心法）

這些不是特定裝置的知識，是**寫控制邏輯時反覆出現的設計 pattern 跟併發陷阱**。有些原始實作是 v1 的（例如步伐補償是 rail-based），但背後的思路在 v2 的吊機驅動步態裡有直接對應（level-match 邏輯）。

### 3.1 步伐補償（v1 rail 邏輯，概念延伸到 v2）

v1 的「步伐補償」是這個專案曾經定義的 canonical inchworm 走路邏輯：每個 step 結束時 DM2J 鐵軌位置必須回到 `rail_baseline`，Phase A 目標動態縮小為 `STEP_CM − offset`、Phase B 動態關回 baseline，retry 有「這個 phase 自己能撤銷多少」的預算上限（不能挖到上輪留下的 offset）。v2 拿掉了 DM2J rail，這套邏輯本身不直接適用，但**「用當前量測值算動態 target、而不是死用常數」的核心思路，被 v2 的左右繩防歪 level-match 邏輯繼承**（見 6.4 節）。

### 3.2 Obstacle 偵測 gate：電流門檻 + 位置 gate

ZDT 伸腳的 obstacle 偵測從「純電流」演進到「電流+位置」雙 gate：純電流門檻（`DISABLE_PHASE_CURRENT_LIMIT_MA`，800→900→**1200mA**）會把「正常壓牆 jam」跟「真障礙物」都判定成 obstacle（壓牆電流可飆到 1.7A）。修法是加回**絕對位置** gate（不是角度誤差）：電流超標時看卡死的絕對位置——`final_pulse ≥ preset − OBSTACLE_ENDPOINT_GATE_CM(1.5cm)` 判定為壓牆 endpoint（比照堵轉 endpoint 處理，defer 不中斷其他 slave）；小於這個 gate 才是真障礙物。**核心洞察**：壓牆跟障礙物馬達卡死時電流特徵相同，調門檻治不了，差別在卡死的位置——推到接近 preset 才飆是牆，半路就飆是障礙物。

### 3.3 Motion parallax：反光/雜亂場景的視覺突破

反光玻璃+雜亂工作室場景下，純 OpenCV 單張分析跟預訓練 NPU 模型（訓練的是真實鋁窗框，對木條 0 detection）都失敗，靠 stereo disparity 對玻璃鏡面也失效（兩台相機看到同一個反射場景）。**解法是 motion parallax**：機體與場景相對位移 1cm 時，真實牆面物體（相機距離 ~20cm）影像移位 10-20px（強 parallax），反射的虛擬場景（虛擬深度 2-5m）移位 <1px（弱 parallax）——optical flow magnitude 用 `median × 2.5` 當門檻就能自然分區，不用標註資料不用訓練。這個「用相對移動製造的視差區分真實物體 vs 反射/雜訊」的思路，後來也被用在深度相機的窗框偵測上（見第 8 節）。**遇到反光/雜亂場景，不要先浪費時間試純 OpenCV 或 NPU 預訓練模型，直接考慮 motion parallax。**

### 3.4 機體幾何公式（v1 8 吸盤機構，v2 已改架構、公式需要重新推導）

v1 的 step_over（跨障礙物步幅）公式：`step = cup_diameter + body-feet_offset + FD + W + safety = 20 + 10 + FD + W + 3 = 33 + FD + W`（cup 直徑20cm、body 領先 feet 10cm、safety 3cm）。這是從「body cup 先跨過障礙物、feet cup 是 trailing cup 要多走」的幾何關係推出來的。**v2 機械架構完全不同**（4 吸盤、無 body/feet 分層、垂直移動改吊機繩驅動），這個公式的推導邏輯（先想清楚哪個 cup 是 leading/trailing、用幾何關係反推最小安全步幅）可以參考，但公式本身要重新推導——v2 的跨障礙物用的是完全不同的機制（伸腳到 2×preset 站離牆，見 6.6 節），不是走這個公式。

### 3.5 EVT 廣播 / PausedOnError 巢狀踩坑

**Pitfall 1**：Crane 的 `cmd_server.broadcast(...)` 會把 EVT 行送給所有 connected client，washrobot 在等 RPC reply 時可能先收到一行 `EVT ...`，誤把它當成 reply。修法（`crane_cmd_` 已實作）：receive loop 看到開頭 `"EVT "` 就呼叫 `handle_crane_evt_(line)` 後 `continue`，不 `return`，繼續等真正的 OK/ERR。**任何新寫的 RPC reader 都要照抄這個 filter，否則會踩坑。**

**Pitfall 2**：`await_user_intervention_()` 進入時會記下 `state_before_pause_ = 進來前的 state`，供 `cmd_continue` 恢復用。如果某個 code path 沒走這個函式、直接手動 `set_state_(PausedOnError)`，之後任何真正呼叫 `await_user_intervention_` 的地方會把 `state_before_pause_` 覆寫成 `PausedOnError`（因為此時的 "prev state" 已經是 PausedOnError 了），造成 `cmd_continue` 把 state 設回 `PausedOnError`，while loop 永遠跑，retry/skip 按鈕看起來壞掉。修法：`if (prev != State::PausedOnError) state_before_pause_ = prev;`——**任何新增的「直接 set PausedOnError」code path 都要保留這個 nested-await guard**。

### 3.6 併發鎖 pattern：共用 bus 的序列化

這個專案有幾條物理 RS485 bus 被多個裝置/多個執行緒共用，`TCP_client::socket_mtx` 只保護單次 send/recv，**不保護「送出請求→等對應回覆」這整個交易**——多執行緒各自送請求、各自等回覆，會收到對方的回覆。修法一律是加一把**業務邏輯層**的 mutex（不是 socket 層），包住整段交易：

- v1：`dm2j_motion_mtx_` 序列化 cli_20_ bus 上的 DM2J motion（背景 arm sweep thread + 主 thread feet rail）。
- v2：`zdt_bus_mtx_` 序列化 cli_20_ bus 上的 4 顆 ZDT（背景 `feet_topup_unsealed_` async + 主 thread 另一側 pusher 操作）。**這是一把新鎖，不是重用 `dm2j_motion_mtx_`**——檢查後發現 ZDT 相關函式完全沒鎖舊鎖（那個名字雖然叫 dm2j 但沒被 ZDT 函式使用），`dm2j_motion_active_` 只是一個給 arm sweep 監控用的 atomic 旗標，不是互斥鎖。

**任何新加的「會持 bus > 100ms」操作，都要用這類 mutex 包住整段交易**，背景執行緒跟主執行緒都要遵守。另外要記得同一 thread 對同一個 `std::mutex` 不能重複 lock（非 reentrant）——`cmd_attach` 呼叫 `do_feet_realign_` 內部又 lock 同一把鎖曾經死當，修法是用 `std::unique_lock` 手動 unlock 再呼叫、呼叫完再 lock 回來。

### 3.7 重心校正流程（v1 完整實作，v2 保留概念但尚未搬移）

v1 的 balance_calibration 是 5 階段流程：preload（拉到目標張力）→ release_body（解真空+兩段式縮回）→ release_feet_center（同上）→ free_hang_settle(3s) → balancing（IMU 閉環，連續 motor + 50ms inner poll）。三種結束路徑都回 Idle：RECORD（保存 offset）/ABORT/中途 ABORT，真正系統錯誤才進 PausedOnError。入口有雙門檻 `|roll| ∈ [0.5°, 15°]`（太平不用校、太歪太危險）。**v2 目前 stub 掉這個功能**（main.cpp dispatch 回 `ERR removed_in_v2`），之後重做時預期會走 IMU + crane 差動邏輯（跟 level-match 用同一套感測器），不是照抄 v1 實作（v1 靠解開 body 真空讓機體自由懸掛，v2 沒有 body 吸盤分組）。

---

## 4. v2 機械架構總覽（跟 v1 的核心差異）

v2 對 v1 做了大幅簡化：

| 項目 | v1 | v2 |
|---|---|---|
| 吸盤數量 | 8 顆（feet 4 + body 4） | **4 顆**（右2 + 左2） |
| 垂直移動機構 | DM2J 滑軌（rail） | **拿掉**，改由吊機繩驅動 |
| 真空分區 | 3 區（腳組/身體組/中心） | **2 區**（左/右），無中心吸盤 |
| 步態 | rail 步伐補償 + 滑軌位移 | 吊機繩左右交替/同步放收繩 |
| 橫向移動 | 有輪組 | **無**（只能垂直上下，換帶靠人工/吊機重定位）|

**推桿 slave 對應**（2026-07-07 確認，修正過先前記反的版本）：右腳上=slave1、右腳下=slave2、左腳上=slave3、左腳下=slave4。對應 PQW（@192.168.1.22 slave 12）：CH1=右腳電磁閥（對應slave{1,2}）、CH2=抽真空幫浦 dp0105、CH3=左腳電磁閥（對應slave{3,4}）——注意這跟 v1 的 CH 對應（v1 CH1=泵浦）不一樣。水平校正靠 IMU (WT901) roll/pitch + SD76 左右計米交叉驗證；一步位移量由 SD76 計米量測（放繩到計米前進 Δcm 為準，不是 rail pulse）。

**核心安全不變式（除了 sync gait 之外全部走法都遵守）：** 任一時刻至少有一側（2 顆吸盤）吸住撐住機體，另一側才可以鬆開移動——絕不同時 4 顆全放。`do_step_sync_`（見 6.5 節）是目前唯一刻意打破這個不變式的重複執行走法，改動相關邏輯前務必記得這個前提不一樣。

基本步態流程（以 step_down 為例，右先左後）：確認兩側真空+推桿無 stall → **右**：解真空→縮腳→放右繩 step_cm→開真空→補伸右腳→確認 → **左**：同樣流程 → 水平check（IMU roll≈0 + 左右繩長差 tol）→ 完成一步。伸腳一律沿用 2.8 節的 valve-before-extend + staged extend，禁止單發盲伸。

程式層面的完整重寫計畫記在 git 追蹤的 `.claude/v2_app_redesign_plan.md`，若要查「v2 某功能原本設計打算怎麼做」可以先去那份文件找。

---

## 5. v2 吊機硬體：VFD 從 SE3 遷移到 Delta MH300

v2 定案吊機左右鋼索 winch 變頻器換成 **Delta VFD-MH300**（型號 `VFD11AMH21ANSAA`，3HP/230V單相），取代原本士林 SE3-210；理由是 SE3 的 CU mode latch + OPT silent latch 踩過太多次，Delta 是業界主流、文件完整。**馬達本身保留不換**，只換變頻器 + 制動電阻（`BR300W070-S`，300W/70Ω）。

Driver `user_lib/MH300_inverter.{h,cpp}` 已寫好（public API 對齊 SE3_inverter 可 drop-in），但**尚未加入任何 vcxproj build、未接 `Crane_control_PI/main.cpp`**（等實際換裝再做）。目前 bench 硬體仍是 SE3，透過 `Crane_control_PI/main.cpp` 頂端的編譯期開關 `#define CRANE_VFD_IS_SE3`（1=SE3 / 0=MH300）決定 include 哪個 header + `using CraneVFD = ...`，程式碼一律用 `CraneVFD` 型別。**換裝時只要把這個開關改 0 重編**，兩個 driver 的 `runForward/Reverse`、`stopDecel`、`emergencyStop`、`setFreqHz`、`readStatusWord`、`readFaultCode`、`clearAlarm` 都同簽名；唯一差異是 keepalive fault 偵測（`vfd_poll_fault_()`/`vfd_status_is_fault_()` 兩個 helper，SE3 讀 `0x1001` 檢查 b7，MH300 讀 `0x2100` 檢查 low byte）也隨開關切換。

MH300 Register map 摘要（已用完整手冊 `DELTA_IA-MDS_MH300_UM_TC_20260505.pdf` 核對過，driver 用的是修正後的值）：

| 用途 | Register/Param | 值/說明 |
|---|---|---|
| Modbus slave addr | `09-00` | 1~254 |
| Comm baud rate | `09-01` | 填 kbps 值本身（如9.6），不是 index |
| Comm 資料格式 | `09-04`（**不是 09-02**） | 8N1(RTU)=12、8N2=13、8E1=14、8O1=15 |
| 頻率命令來源=Modbus | `00-20=1` | 對應 SE3 的 P.79=3 |
| 運轉命令來源=Modbus | `00-21=2` | |
| Run/Stop | `2000H` | 0x01=Stop、0x02=Run、0x12=正轉、0x22=反轉 |
| Freq setpoint | `2001H` | 0.01Hz 單位（3000=30.00Hz） |
| 狀態字 | `2101H`（**不是 2100H**，2100H 是錯誤碼） | run/stop + FWD/REV |
| 實際輸出頻率 | `2102H` | 0.01Hz 單位 |
| Fault clear | `2002H` bit1(=0x0002) | 對照 SE3 的 H1101=0x9696 |
| DC 注入煞車 | `07-00`~`07-04` | 對照 SE3 P.10-00/01/02 |
| 加減速時間 | `01-12`(accel)/`01-13`(decel) | 對照 SE3 P.7/P.8 |

**跟 SE3 比最大的簡化：MH300 不需要 CU mode latch**（不用先寫一個「解鎖」再寫運轉指令），driver 已經拿掉 `ensureCuMode_()` 這段邏輯。**待實機**：電流/電壓 scale 校正（driver 內暫填 0.1）、換裝後的急停 recovery 差異、真正切到 MH300 時必須低頻重新手動驗證方向對應（macro 上有警語）。

---

## 6. v2 步態引擎詳解

v2 的步態設計是整個專案這幾個月演進最密集的部分，這一節按時間順序講清楚設計是怎麼演化過來的，方便理解「為什麼現在長這樣」。

### 6.1 init/attach 流程（沿用 v1 大改後的版本）

`cmd_init_impl_` **不伸任何 pusher**（feet 停在 0）——init 後機器無任何 cup 抓力，必須靠繩支撐，init→attach 之間不能 idle。`cmd_attach` 走**串行**（不是同時開多個 valve）：開 FEET valve → `smart_extend_subset_("feet")` 伸到 preset + 等真空 → `do_feet_realign_` mid-attach realign → 開 BODY/其他 valve → 伸對應吸盤 → vacuum_check + 補漏 → pay_out。任何呼叫 `do_feet_realign_` 的函式都要用 `std::unique_lock` 手動 unlock/lock（避免同 thread 對 `motion_mtx_` 重複 lock 死當）。

### 6.2 Realign：4 顆全程保持吸住縮回 preset

v2 的 `do_feet_realign_`/`cmd_realign` 只處理 4 顆 feet，**全程不解真空**——每支推桿 relative-mode 縮回 preset，被吸住的吸盤把機體拉近牆面、吸盤不脫離，也**完全不動吊機繩**。這是因為 v2 只剩左右腳 4 顆、沒有 v1 的 body/center 組可以交替，如果解開全部 4 顆真空就違反「任何時刻至少一側 2 顆吸住」的核心不變式。

動作沿用 v1 realign phase2 的驗證過的手法（outward jog 卸彈性預載 → 慢縮破黏 → 快縮），但**2026-07-14 改成一律單段**（不分三階段）：實測 drift 通常只 ~1cm，三段開銷（每段固定450-600ms）不值得，而且舊的慢縮 stage 反而是脱封元凶。單段 @ 70rpm（`REALIGN_RETRACT_RPM_FULL`）自己就能溫和破黏，realign 時間從 ~3.5s 降到 ~1s 且不再脫封。

**Seal 前置條件放寬（2026-07-13）**：原本要 4 顆全吸才動，改成**每側 ≥1 顆吸住即可**，只有「整側全掉」才拒絕；配合下面 6.3 節的 stop_on_first_seal，每側常只吸 1 顆，嚴格版會讓 end-of-step realign 幾乎每次失敗。

**接回時機**：`do_step_down_`/`do_step_up_` 收尾（兩側都完成、4顆全吸）呼叫 `(apply_threshold=true, caller_holds_lock=true)`——`apply_threshold=true` 表示會先算 drift（`REALIGN_THRESHOLD_CM=1.5`/`_MEAN_CM=1.0`），沒超過就 skip（多數步末是便宜 no-op）；`caller_holds_lock=true` 表示 step 已持鎖不用重鎖。In-step 失敗**非致命**（不設 PausedOnError，因為 realign 不解真空、stall 後 4 顆仍吸著機體仍錨定）；手動 `cmd_realign` 才會在 stall 時進 PausedOnError。

### 6.3 「每側 ≥1 顆即算吸好」的判準統一

`do_step_down_`/`do_step_up_` 的真空判準（2026-07-08 定案）：**一組（=一側 2 顆）只要 ≥1 顆到 `VACUUM_THRESHOLD_KPA` 就算吸好，繼續下一步**，不是 4 顆或整組全吸。三個 gate 都用同一個 helper `group_seal_ok_(group, out_unsealed)`：(1) reseal 成功判準——動側 ≥1 顆就 proceed；(2) anchor 前檢查——撐重側 ≥1 顆才准放開動側；(3) 起步前檢查——左右各 ≥1 顆才准起步。**伸腳補伸迴圈本身也早退**：`pusher_extend_with_disable_seal_` 的 `stop_on_first_seal=true`（step 專用）讓迴圈一旦有 ≥1 顆真密封就 break，不再補伸其他 cup。

**換邊前的 top-up 補第 2 顆**：proceed 後不是就放著，`feet_topup_unsealed_(group)` 會對還沒吸的 cup 再跑一次伸腳（不 rescue，valve 保持 ON），盡量恢復每側 2 顆的安全裕度。2026-07-15 改成 `std::async` 背景執行，讓「換邊」立刻發生、topup 跟另一側動作並行——這正是引入 6.7 節 `zdt_bus_mtx_` 的原因（背景 topup 跟主 thread 另一側可能同時碰 ZDT bus）。

**安全取捨（已徵得同意）**：最壞情況機體只靠 2 顆（每側 1 顆）撐住半空作業。這個範圍只套用在 step_down/step_up 的 `cycle_group_`；attach 走各自的 `smart_extend_subset_`，邏輯不受影響。

### 6.4 左右防歪 level-match：方案1 → 方案B 的演進

**問題**：v2 每步左右各用自身 SD76 獨立走固定 `step`，誤差獨立累積，走多步後機體 roll 明顯。

**方案1（master/follower）**：右側=master 走固定 step，左側=follower 放/收到「左計米=右計米」（`delta = master_len − self_len`）。每步重新對齊，誤差不累積。Bench 驗證有效但有殘差（left-right 差穩定在 ±3~6cm）——追查發現殘差來源是 crane 端 `cmd_side_measured` 全速衝到目標才減速，減速斜坡繩子繼續滑造成 2-6cm 過衝。修法：離目標剩 5cm 時降速到 10Hz（`g_fine_adjust_hz`）再停，過衝壓到 <1cm（crane 需重編）。

**方案B（取代方案1，解決 over-travel 破口）**：方案1 有個危險破口——某側 reseal 失敗、backup 退回原位，下一步 follower 要一次補 2×step，單側在只有對側吸盤錨定時盪太遠。改法：**步前先讀兩邊計米，鎖一個共同絕對目標**——down 時 `target=min(L,R)+step`、up 時 `target=max(L,R)-step`（從落後側推進一個 step）。兩側都走向這個絕對目標：落後側正好走 step、領先側讓步（走 ≤step）——任何單側每步都不超過 step，一步收斂回水平，不累積。核心函式拆成 `read_crane_meters_(L,R)` + `crane_abs_target_cmd_(...)`。

**交替先動（2026-07-09）**：先動側（右或左）＝datum（純方案B 計米對齊）、後動側＝follower（多加 IMU 對平）。`run`/`run_script` 每步交替；起始腳可用 `set_first_step left|right` 切換（`first_step_right_`），理由是固定右先走會讓右側絕對位置隨計米微飄，交替後左右對稱分攤。

**follower IMU 對平（`follower_imu_level_`）**：長繩情況下「兩邊計米相等」不等於「真水平」（計米累積誤差 + 繩張力彈性伸長）。加了第二層校正：follower 側粗走（方案B）到 target 後，重伸吸盤前，反覆讀 `roll = imu_.x − imu_roll0_`，未收斂（`|roll| ≥ FOLLOWER_ROLL_TOL_DEG=1.0°`）就用 `FOLLOWER_SPAN_CM(placeholder 100cm) × tan(roll)` 估距做小的 tension-safe measured move，最多 3 pass。可用 `set_follower_mode imu|meter` 切換（預設 `imu`，見上面 OPEN 事項 #2）。**物理限制要記住**：吸盤吸牆時 roll 被吸附點釘死，只有某側解真空、靠繩吊著時 roll 才反映該側繩長不平衡，所以 IMU 校平只能在 follower 側解真空移動的那個窗口做。

**已排除方向：靠左右繩差轉彎**——繩差只產生 roll(傾斜)不是 yaw，機構未為橫向移動設計，真要水平移動需要別的機構（吊車台車/專門橫向步態）。

### 6.5 同步步伐 `do_step_sync_`（跟交替步態並存，不是取代）

`step_down_sync`/`step_up_sync` 是 2026-07-22~23 新增的另一種走法：兩側 valve **同時**關+等釋放+兩段式縮回 → crane 兩側繩子**同步**放/收（`dual_vfd_sync_start` 真同時觸發，不是交替）→ `do_sync_imu_roll_correct_()` IMU 差動微調（獨立速度 `g_roll_correct_hz`=5Hz）→ 兩側 valve 同時開 → 4 顆吸盤**同時**伸出 → 上滑台小行程掃動。

**⚠ 安全性質跟其他所有走法不同（刻意如此）**：放繩期間 4 顆吸盤全部同時放開，完全靠吊機繩索承重，是唯一打破「至少一側錨定」不變式的重複執行走法。

**4 顆同時伸出的判準架構修正（值得記住的教訓）**：最初想用「一側伸完再伸另一側」讓早停邏輯套用，被直接否決——「兩邊的腳組一定要一起放」是硬性要求，不能為了實作方便犧牲機構層級的同時性。正確做法是回頭改底層共用函式：`pusher_extend_with_disable_seal_` 新增 `stop_group_ids`（每個 slave 標記所屬組），讓同一次 `trigger_sync_move` 呼又內「每組獨立判斷是否已有 ≥1 顆真密封、獨立凍結」，而不是原本整批共用一個全域早停旗標。**這個參數是 optional、預設空**，既有呼叫端（`cmd_attach`/交替步態的 `cycle_group_`）完全不受影響。

**上滑台掃動並行化**：手臂還沒裝時只動滑軌本身，用獨立常數 `DM2J_ARM_STEP_SWEEP_*`（跟真正清洗引擎的 `ARM_SWEEP_*` 分開）。因為要跟伸腳的 JC100 壓力輪詢（同一條 cli_22_ bus）並行，改用非阻塞的 `arm_sweep_fire_nowait_`——這是 2026-05-26 就解過的「cli_22_ bus contention」老問題在新場景又踩到一次，直接套用既有解法。

**整側完全沒吸住的失敗處理（2026-07-27，改成主動找吸點）**：原本邏輯是重試一次還失敗就直接回 `ERR side_unsealed`。改成迴圈退繩找吸點——4 顆全收縮 → 吊機往反方向退 `STEP_SYNC_BACKOFF_CM`(10cm) → 4 顆重新同時伸出 → 重新檢查，不行就重複，直到吸住或累計退回距離等於這一步原本的距離；真的退到起點都吸不住才最終失敗。

### 6.6 跨障礙物 `do_cross_obstacle_`

一般 step 過不了牆面突出物（窗框）。跨障礙物流程：把腳伸到 **2×preset**（約11.4/12cm）讓機體站離牆騰出空間跨過，兩邊都跨完再一起縮回正常長度。**獨立的 orchestrator，不改 `do_step_*_` 本體**，避免衝突。Phase1 首側跨（錨側伸 2×preset 跟移動側縮腳同時做，用非阻塞觸發省時間）→ crane 移 → 重吸 2×preset；Phase2 次側跨（錨已在2×，不再伸）→ crane 移 → 重吸 2×preset；Phase3 用 `do_feet_realign_` 把四顆縮回正常 preset。

**共用碼唯一的改動**：`cycle_group_` 加第7個參數 `feet_target_override`（optional function，預設空→完全向後相容），有值時 bypass 一般的 snowball cap，讓 cup 搆到站離牆後更遠的牆面。**未加額外張力/傾斜監控**——錨側伸 2× 撐離牆會改變重心/繩張力，crane rope move 自帶的張力安全是唯一的保護，如果實機發現撐離牆不穩需要另外加保護。

支援 script CSV 的 `x` 後綴（該步跨障礙物），可跟 sweep 的 `n` 後綴並存。

### 6.7 已知的兩個併發/計算 bug（都已修，記錄修法供類似情境參考）

**ZDT bus 並發**（見 3.6 節）：`zdt_bus_mtx_` 是為了 6.3 節的背景 topup 新增的。

**退繩重試預算算錯（2026-07-15 修）**：`do_step_up_`/`do_step_down_`/`do_cross_obstacle_` 吸不好重試退繩時，會退到比這一步開始前的原始位置還低。根因：退繩上限用的是固定 step 常數，但方案B 下實際搬移量 `mv` 可能小於 step（領先側讓步時），用 step 當預算會允許退超過起點。修法：`crane_abs_target_cmd_` 新增輸出參數 `out_mv_cm`（純量、跟方向無關，每條 return 路徑都填），`backup_cm` 的 `remaining` 改用實際 `fwd_mv_cm` 而不是 step 常數。跟方向判斷完全無關，三個呼叫點可共用同一套邏輯。

### 6.8 清潔手臂：從拆除到裝回（2026-07-24 起）

`cmd_run`/`cmd_run_script` 的 v1「邊走邊刷」sweep pipeline 因為手臂當時未裝，被拆成純 step 迴圈（`#if 0` 保留原始碼在 `_retired_..._v1_` 系列）。**2026-07-24 手臂已實機裝上**，但只有 `do_step_sync_` 這條新路徑接了真手臂動作（`do_step_sync_rail_sweep_()`），`do_step_down_`/`do_step_up_`（交替走法）還沒接（照 `.claude/v2_app_redesign_plan.md` §5.6 的步驟）。

硬體變動：PQW 確認物理有 16 CH；`CH_BRUSH`（刷洗滾筒馬達）從 CH5 移到 **CH15**；水閥/水泵管路仍未接（相關呼叫維持註解）。

**這段除錯過程本身有幾個通用教訓值得記住**（`cleaning_arm/main_api.cpp`，damiao M1/M2 馬達）：
- **M2 出力不足 vs passive 誤判**：PARK 卡 18 秒，第一版假說猜是 CAN 層 passive latch，port 了偵測邏輯結果沒用——後來發現是 `go_home_slot()` 用的 kp（≈2.5）比 DEPLOY 用的 `lr_move_to_slot_impl()`（`MIT_KP=28` + 摩擦力前饋）差 10 倍以上，M2 靜摩擦力大、扭力不夠脫離位置。修法是讓 PARK 的 M2 也走已驗證可靠的 `lr_move_to_slot_impl`，不是繼續猜新的 kp/kd。
- **共用函式改動要考慮所有呼叫路徑**：加的 passive-probe 邏輯後來因為「移動前莫名彈一下」被撤回——因為 `go_home_slot()` 是共用函式，PARK 跟 DEPLOY 的 M1 retract 都會呼叫，探測邏輯無條件套用在所有呼叫者身上，物理上真的彈了一下。
- **靜默失敗要變成看得到的 ERR**：`cmd_deploy_sequence()` Step 3 的 `wait_for_move(m1_)` 回傳值曾被丟掉（"best-effort"注解），不管有沒有真的轉到位都回 OK——這解釋了「距離越跑越遠」的現象（同一槽位重複跑，距離累積偏移）。修法是補上檢查，跟 Step 2 (M2) 對稱處理。同樣模式在 `lr_calibrate_slot()`（INIT 階段）也發生過（`void` 回傳、三種失敗都靜默 return），已改成 `bool` 回傳並讓上層接住。
- **反覆出現的症狀值得往下查根因，不要只在每個函式各自補檢查**：「vel/tau 有讀值但 pos 完全凍結」這個症狀在 `go_home_slot`、`lr_move_to_slot_impl`、`lr_calibrate_slot` 三個不同函式都遇到過——這暗示 M2 本身有間歇性完全無回應的硬體層級問題（CAN通訊/供電/馬達本身），軟體端逐一補檢查只是治標，值得認真往硬體方向查一次。

---

## 7. Crane 通訊 hardening（2026-07-14，三個疊在一起的 bug）

一次 debug session 修了三個疊加造成 v2 步態不穩的 bug：

1. **`crane_cmd_` 逾時吞掉真正錯誤原因**——非 OK 回覆時原本直接丟棄字串，改成印出 `[crane_cmd] '<cmd>' -> <reply>` / `FAILED after 2 attempts`。
2. **`cmd_side_measured` 沒搶 `motion_mtx`**——`motion_rope`/`cmd_roll_correct` 都有 try_lock 保護，這個後加的函式漏接。因為 washrobot 端逾時（`crane_motion_timeout_sec_(cm)=ceil(cm/10)+5`，30cm 只有8秒，遠比實際耗時短）會強制斷線重連、重送同一句指令，crane 端 `TCP_server` 每連線一個 thread，舊 thread 還在跑、新 thread 又開一個，**兩個 thread 同時驅動同一顆 VFD**。已補鎖。
3. **`read_crane_meters_` 沒有 sanity check，髒值讓方向算反**——SD76 讀到一次髒值（`self_len=3.37e7`），`delta = target_len − self_len` 變成巨大負數，方向從該有的 `pay_out_left` 反成 `retract_left`，per-move cap 只夾了量沒夾方向，觸發 IMU emergency abort（49.6° 傾斜）。已加 `CRANE_METER_SANITY_MAX_CM=20000`（200m）超出範圍當讀取失敗，走 `fixed_step()` fallback。

**修 bug 時的方法論教訓（user 明確要求的驗證標準）**：修完「看起來像髒值/異常輸入」的 bug 後，不能只加 sanity check 就結束，要把整條計算鏈（方向判斷公式、上下游目標值公式、硬體方向對應）**用事故當下的真實數字重新代入驗算**一遍，明確講清楚「邏輯本身沒問題、問題出在哪個輸入」還是「邏輯本身也有問題」。這次驗算過 `delta` 方向判斷、`min/max ± step` 目標公式、VFD 硬體方向 macro，確認都沒問題、純粹是輸入被污染。

---

## 8. 深度相機（D435i）窗框偵測 ⚰️ **已移除（2026-09-01）**

> 🔴 **本節描述的功能已不存在。** per user「沒有要用」，2026-09-01 整套移除：
> `cmd_run_depth_avoid` / `depth_cam_cmd_` / `DEPTH_CAM_*` 常數 / `depth_cli_`（C++）、
> `frame_capture/depth_cam_service.py`＋`depth_cam_test_client.py`＋`depth_reflection_bench.py`
> （已從版控刪除）、`wr.sh` 的 depth window、harness 的 `depthcam` 假端點。
> dispatcher 對三個指令回 `ERR removed_2026_09`。
>
> **本節保留的理由是方法論教訓，不是操作說明**——尤其 8.3 那條「公式邏輯對不代表數字準，
> 常數本身是實體量測值，必須跟現場皮尺交叉驗證」，在這個專案已經重複應驗多次
> （2026-08-28 上滑台導程 7.7 倍、2026-08-31 推桿 `CUP_PULSE_PER_CM`）。
> **不要照著本節的檔名或函式名去找程式碼，它們都不在了。**


窗框避障原本用 RGB 相機 + motion parallax（3.3節）+ NPU/LUT，2026-07-16 起探索用 Intel RealSense D435i 深度相機取代，理論上不需要手動校正 LUT。**戶外強光風險是這條路線最大的未知數，見最前面的「尚未解決事項 #4」。**

### 8.1 Bench 工具架構

工具：`frame_capture/depth_reflection_bench.py`（Windows 開發機手動跑，`pip install pyrealsense2`）。核心架構：(1) 把 motion parallax 技術（3.3節同一套 `obstacle_detector.py` 函式）套用在 depth 頻道上濾反光；(2) **背景平面擬合算凸出量**——相機有俯角，牆面在畫面上不同位置的原始距離本來就不一樣，改成每次 capture 動態擬合背景平面（3輪離群值剔除），凸出量=該點垂直於平面的距離，不受相機角度影響；(3) 寬扁比+最小寬度過濾窗檻形狀；(4) 碎片合併——**要用凸出量差而不是原始距離差**當合併判準（同一根斜看窗檻的原始距離變化可以超過10cm，用原始距離合併會失敗）；(5) 同質性檢查（`protrusion_std`）——用凸出量的變異度而不是原始深度範圍來判斷是不是被 connected-components 誤合併了兩個表面。

### 8.2 距離優先設計（鏡面反光讓背景平面擬合失效）

實機接上真正的 `depth_cam_service.py`（TCP:9530）除錯時發現：**鏡面反光場景下，「擬合背景平面」這個做法可能整個沒有獨立背景可用**——鏡子反射的內容距離太遠或被 motion mask 濾掉，畫面裡「有效+夠近+有偵測到移動」的像素幾乎全是木板本身（曾測到 99.9% 是木板），沒有真正的背景點可以擬合。**設計轉向**：候選物判準改成距離+形狀為主，凸出量降級成「算得到就顯示，算不到不擋偵測」；背景平面擬合完全失敗時，退回純距離+形狀判斷而不是回報 candidates=0。理由：user 主要需求是距離（算下一步安全步幅），不是精確凸出量。

**幾何轉換公式**（`WASH_ROBOT.cpp::cmd_run_depth_avoid`）：`horizontal_cm = sqrt(min_distance_cm² − DEPTH_CAM_STANDOFF_CM²)`、`remaining_cm = horizontal_cm − DEPTH_CAM_LEAD_OFFSET_CM`（右三角形模型，相機垂直於牆面 standoff、俯角看向前方）。**踩過兩層坑**：(1) 候選框內最近像素不保證在畫面正中央，偏心會被誤當往前距離造成系統性高估（改用主點左右各30px窄帶內找最近距離，`center_distance_m`）；(2) 修完偏心後數字還是差很多——回推發現是**安裝幾何常數本身量錯**：`DEPTH_CAM_LEAD_OFFSET_CM` 原本量成16cm，實際是**32cm**；`DEPTH_CAM_STANDOFF_CM` 從50cm修正到**56cm**（2026-07-23 重新量測確認）。**教訓：公式邏輯對不代表數字準，常數本身是實體量測值，必須跟現場皮尺交叉驗證。**

跨越障礙物步幅建議公式：`remaining_travel_cm < DEPTH_AVOID_LOW_CLEARANCE_CM(20cm)` 時，預設步幅建議 = `remaining_travel_cm + max_height_cm + 20(吸盤直徑) + 5(緩衝)`，夾到 `STEP_CM_MAX`，只改預設值不強制鎖定。

**⚠ 這一輪改動（`TCP_client.cpp`、`WASH_ROBOT.h/.cpp`）在寫下時還沒編譯部署過**（本機無法 remote build），新的幾何常數也還沒重新實機驗證，一般（非鏡面）窗戶場景完全沒測過。

### 8.3 參考文獻

找過攀爬機器人視覺相關論文找靈感：LORIS（CMU）已讀完但參考價值有限（視覺只是給人工選點看,抓附判斷靠馬達電流本體感覺）。還沒讀、依相關性排序的候選：**ReachBot 抓握點偵測**（JPL, arXiv 2312.09302，最相關——判斷深度資料裡的局部凸起是不是真正可抓的點）、RGB-D Terrain Perception and Dense Mapping for Legged Robots（Belter et al.）、Footholds optimization for legged robots（Springer）、Mind Your Steps（arXiv 2606.08253，用 D455 同系列相機）。

---

## 9. 已退役子系統：Easy Crane

2026-08-04：`Crane_easy_PI`（跑在192.168.5.26的獨立簡易單馬達捲揚裝置）已物理拔除，相關程式碼**已刪除**（不是註解保留）：`Crane_easy_PI/`、`crane_shim/`（協定轉譯層，假裝自己是主吊車、把指令轉給 easy crane）、`.claude/easy_crane_test_mode.md`、`facade_cleaning_v2.sln` 裡的專案項目、GUI 面板、`Linux_test` 選單裡的範例文字、`runbook.md` 相關章節全部清掉。

**唯一意外牽連到核心程式的地方**：`WASH_ROBOT.cpp` 的 `read_rope_weight_max_kg_()` 原本有第三層 fallback `read_easy_weight_kg_()`（主吊車張力讀不到時，改讀 crane status 裡靠 crane_shim 轉譯過來的 easy crane DY-500 讀值）。這個 fallback 已整個刪除，改成只剩兩層 fallback（DSZL-107 → washrobot DY-500 cache）。

**教訓（值得記住）**：移除看似獨立的 GUI 功能前，一定要全 repo 搜一次相關字串——這次刪除時才發現牽連到核心的張力 fallback 邏輯，事前沒有任何 memory 特別記過這個關聯。**如果之後想恢復「沒有真主吊車硬體時測整個系統」的能力，需要重新設計一個替代方案**，不能指望這次刪掉的東西還在。

---

## 10. Web GUI 演進

### 10.1 美學方向

GUI 走「深空極光」風格（glassmorphism + neon）：深藍紫漸層底 (`#0a0e27 → #1a0f3a → #0f1530`) + 兩顆漂移 aurora blob；panel 用 glassmorphism（`backdrop-filter: blur(16px) saturate(140%)` + 半透明紫底 + 頂部青→紫漸線）；霓虹 accent cyan `#00e5ff` / purple `#a855f7` / pink `#ec4899`；標題 gradient text；字體 Inter (UI) + JetBrains Mono (數值/log)；log panel 保留深色終端底（可讀性）但外層 glass frame。這套配方已驗證過打中需求，新增 panel/按鈕比照 `.panel-emergency`/`.btn-emergency` 的做法（同 glass 架構 + 語意色變化）延伸，不要回到純終端黑底風。

按鈕旁的說明文字（hint/note/警告）一律用 `<ul class="hint"><li>` 列點格式，不用 `<p>` 句號串接長段落（列點對操作員更清楚，一眼看到每條規則）。CSS：`ul.hint` 走 `padding-left:20px; font-size:12px; color:#bbb`。

### 10.2 2026-08 結構調整

`web_backend/public/index.html`：原本一個大「auto cycle (washrobot)」panel 拆成三個：**共用**（step cm、init/attach/detach、run/pause/status 等）、**同步上下移動**（sync gait 按鈕+安全提示）、**左右輪流移動**（交替走法+清洗變體+跨障礙物+防歪模式）。跨障礙物歸類在「左右輪流移動」（結構上比較接近交替家族）。所有按鈕 `id`/`data-cmd` 沒變，只是搬 `<section>`，`app.js` 的選擇器/綁定不受影響。標題也同步簡化（"washrobot control" → "facade cleaning control"）。Easy Crane 面板整個移除（呼應第9節退役）。**這批改動尚未實機開瀏覽器驗證過。**

---

## 11. 保留但尚未在 v2 重做的功能

`facade_cleaning_v2/main.cpp` 目前對 `obstacle_*`/`balance_calibrate_*`/`confirm_balance` 全部回 `ERR removed_in_v2`——**這是暫時 stub，不是廢棄決定**。Web GUI 上「⚖️ 重心校正」與「🎥 窗框避障」兩個面板都保留在畫面上（user 明確要求保留），現在按下去會拿到錯誤，等 backend 重做才會通。重做時記得把那幾行 `removed_in_v2` 換回真正實作：

- **重心校正**：預期走 IMU + crane 差動那套邏輯（跟 level-match 共用感測器），不是照抄 v1 的「解開 body 真空自由懸掛」實作（v2 沒有 body 吸盤分組）。
- **窗框避障**：目前深度相機路線是主力方向（第8節），但戶外強光測試還沒做（尚未解決事項#4）。曾經討論過超聲波感測器作為備案（不受反光影響，但 beam cone 造成空間解析度粗，需要每顆吸盤附近各裝一顆才能覆蓋，是全新硬體整合）——純討論，沒有做決定。

無中心吸盤：v2 只有4顆，`vacuum center`/`pusher center`/`zdt_zero center`/slave 9 等 v1 遺留的中心吸盤相關指令全部已從 Web GUI 移除。「中心校正」一詞在 v2 語境裡指重心校正 panel，跟（已不存在的）中心吸盤無關，避免混淆。

真正退役、不會再回來的（跟上面「保留但未重做」不同）：DM2J 輪/滑軌相關指令（move/wheels/dm2j_group/dm2j_zero）、`wheels_attached`、`tilt_mode`。
