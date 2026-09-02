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
| 🟡 | 🆕 **右腳推桿阻力明顯高於左腳，假說是「右側離牆較近」** —— 2026-09-02 晚輪（每顆 50 次伸出）：右上 2025ms/1039mA、右下 2400ms/1089mA vs 左上 1650ms/874mA、左下 1500ms/882mA ⇒ 右腳為左腳的 **1.4~1.6 倍**時間、高約 20% 電流（早上同樣態，使用者當日檢修後**仍在**）。⏸ **per user 推測「右邊重心較重」——張力資料指向相反**（整天 左 52~62kg / 右 37~43kg，比值 1.4× **左邊重**，與續十二「重心偏左」一致），且**推桿水平、鋼索垂直，不同軸**。🆕 **吸附率交叉比對支持幾何解釋**：50 步中「只有右腳吸到」11 次 vs「只有左腳吸到」2 次（**5.5 倍**）⇒ 右側離牆較近 → 右推桿較早接觸玻璃、接觸後仍要推到指令位置 → 時間長電流高，同時右吸盤較易吸到。**一個原因解釋兩組獨立量測**；競爭解釋「左吸盤漏氣」解釋不了電流那一半。🔧 **分辨方法：懸掛時用尺量四個吸盤位置到牆面的間距。** | 機構（非程式） | **未查明** ✔ | 2026-09-02 |
| 🔴🔴 | 🆕 **風扇會干擾 `.22` 匯流排** —— 2026-09-02 兩份 log 一致：風扇開啟期間 `.22` 裝置（JC100 ×4、QX:9）錯誤密度是關閉期間的 **≈15×**（462 行/246 錯 vs 1341 行/48 錯），且風扇開啟後 **3 秒內** 兩顆 JC100 同時進 fast-fail。⚠️ 分母是 log 行數不是交易數，絕對比例偏誤，但效果量與時序都指向同一件事。🔴 **這推翻了同日先寫下的兩個推測**（「左腳接頭間歇接觸」「`.22` 匯流排本身有問題」——風扇關著時 JC100 連讀 10 次全一致）。**兩個候選機制未分辨**：① EMI 耦合進 RS485；② 電源下陷（電源架構 [B] EPP-200-24 同時供應「氣動、感測 I/O 與通訊介面」）。🔧 **分辨方法：量風扇啟動瞬間 [B] 的電壓** —— 掉得明顯＝②（要分電源），否則偏 ①（要屏蔽/接地/走線分離）。**處置完全不同，量了再動。** 🆕 **2026-09-02 晚間新診斷給出病因層級的證據**：135 筆非 timeout 錯誤分類為 `CRC 22 / ADDR_MISMATCH 12 / FUNC_MISMATCH 10 / MODBUS_EXCEPTION 1 / **SHORT_FRAME 0**`，且訊息附帶的位元組顯示是**正確位址被打壞幾個位元**（`0x08`→`0x28` bit5、`0x06`→`0x0E` bit3、`0x03`→`0x13` bit4），後續位元組仍在正確位置 ⇒ **線上位元級損毀（電氣），不是分片、不是錯位**。| 電氣（非程式）；影響 `.22` 全部裝置 | **病因已定性（電氣雜訊），機制未分辨** ✔ | 2026-09-02 |
| 🔴 | 🆕 **QX-DO24（`.22` slave 9，風扇）間歇性全滅，原因未明** —— 2026-09-02 12:44 起寫入 **0/22**（讀取偶爾成功 ⇒ 失敗在「主站→模組」方向，長幀進不去）。逐一排除：網關死連線❌／`_pt` 分片❌（0 與 5 各 0/5）／slave ID❌／整台斷電❌／匯流排本身❌（同線 JC100 完美）／電力接線❌（per user 正常）。🔴 **13:22 換 binary 重啟後突然全好**（讀 8/8、寫 15/15、錯位消失），**per user 期間沒動任何實體** ⇒ 「485 接收端被燒」的推測收回（燒毀不會自癒）。⚠️ **同樣是重啟程式，13:05 沒用、13:22 有用 ⇒ 間歇、隨時可能再犯。**下次再犯時：新加的 FC `0x06` 退路會自動接手並印 `LOG_WRN`，`pwm restart` 是另一條短幀命令 | `user_lib/QX_DO24.cpp`；硬體 | **未查明（現已恢復）** ✔ | 2026-09-02 |
| 🔴 | 🆕 **`cmd_pump` 會謊報成功** —— 2026-09-02 `pump on` 回 `OK`，連讀 4 次 `relay_status` 都是 `ch2=0`（再送一次才真的開，3/3 回讀確認）。`cmd_relay`（續十新增）**有**回讀、`cmd_pump` **沒有** ⇒ 續十「指令回 OK ≠ 繼電器真的動了」的又一個實例。🔧 **修法：`cmd_pump` 比照 `cmd_relay` 補回讀。** 📌 同批還有 `zdt_release_stall` 第一次 `ok=2 fail=2`、重送三次全 `ok=4` —— `.20` 在程式重啟後頭幾筆交易有瞬態，值得一併查是否該加重試 | `app/wash_robot_commands.cpp` `cmd_pump` | **未修** ✔ | 2026-09-02 |
| ⚪ | 🆕 **七支驅動裡只有 `QX_DO24` 有分片防護**（🔴 **2026-09-02 當日證偽為本專案的實際病因**：新診斷上線後 197 筆 `.22` 錯誤裡 **`SHORT_FRAME` 掛零**，實際是位址／功能碼的**位元級損毀**。結構性暴露仍在，但優先度由 🟡 下調 ⚪） —— `.20`/`.22` **都是 115200 8N1、`_pt` 都是 0**，網關依字元間隔打包（115200 下僅約 0.3ms）⇒ 回覆可能被切成兩個 TCP 段。只有 `QX_DO24` 用會累積分片的 `sendAndReceiveQuiet`，其餘六支（ZDT/PQW/DM2J/JC100/XKC/DY500）都是普通 `sendAndReceive`。⚠️ **`QX_DO24.cpp` 原註解宣稱「JC100/SD76/SE3 都是 9600、本模組是全專案唯一 115200」——那半句是錯的，而錯的那半句正是防護沒擴散出去的原因**（已於 2026-09-02 更正）。📌 症狀不會長成「分片」的樣子：截斷的幀在 JC100 表現為 **CRC error** ⇒ 要靠新加的 `SHORT_FRAME(len=N,expect=M)` 才分得出來。🧪 `_pt=5` 已在 `.22` 實測過但**量不到效果**（當時的故障與分片無關），已改回 0 | `user_lib/*.cpp`；`.20`/`.22` 網關 | **未修** ✔ | 2026-09-02 |
| 🔴 | 🆕 **`cycle_test.py` 10 週期一次都沒跑完，而且一輪比一輪早掛** —— 2026-09-01 三輪：`cycle10b` 掛在**週期9**、`cycle10c` 掛在**週期7**、`cycle10d` 掛在**週期6**（前兩輪是左右差 9cm>8，最後一輪是 roll 7.97°>6.0）。單週期兩次都完成。🔴🔴 **左右差分布在兩輪之間明顯惡化、中間沒有相關改動**：10c（36段）中位 2 / p90 5 / 最大 8；10d（35段）中位 **3** / p90 **7** / 最大 **9**；且 **10d 之內也在惡化**（週期1~3 的 Δmax 多為 1~5，週期4~6 出現 7/7/9）。⚠️ 依續七「run-to-run 變異 ≈ 效果量」的教訓**不逕稱機構退化**，但「單輪之內與跨輪之間同時越跑越差」值得當觀察重點。✅ 附帶：「連續 3 筆」修正驗證通過（10d 統計：「超標後自行回復（未達連續 3 筆）: 1 次」＝正確地沒為瞬態中止）。🆕 **2026-09-02 `cycle10_0902` 10/10 全部跑完**（60 段：中位 3 / p90 **5** / 最大 **6**、≥6cm 僅 3%、自行回復 **0** 次）＝昨天那個惡化沒有重演。🔴🔴 **但這輪全程沒有真空源**（`relay_status` 直接回讀 `ch2=pumpA` 為 0），定位是**無真空對照組**，**不可與 10c/10d 直接比對** —— 它只證明「吸盤完全不參與時，同套機構動作 60 段最大差 6cm」。⇒ **吸附/脫離是嫌疑最大的變因，但這是一次觀察不是結論**（另有三個混淆項：靜置一夜、開跑前做過一次乾淨的收回頂端、`level_diff` 由 4 改 5）。**下一步就是開幫浦跑同樣的 10 週期。** | `Linux_test/cycle_test.py`；紀錄在吊機 `~/bringup/cycle_logs/` | **待有真空的對照輪** ✔ | 2026-09-01（續十八）盤點、2026-09-02 更新 |
| 🔴 | 🆕 **跑 `cycle_test` 前必須先確認 `balance_source=imu`** —— 編譯預設是 `meter`，**吊機程式一重啟就會變回去**，而除了 status 之外沒有任何徵兆。續八對照組：關掉 IMU 平衡 → 平均 |roll| 0.68°→**2.52°**、出帶 22.7%→**67%** ⇒ **沒設就跑，整輪數據不可比，而且看起來只會像「機構又變差了」**。同理 `fine_adjust_level_diff_cm=5` 也是執行期值。📌 這兩項與「`balance_source` 編譯預設」那一列是同一件事的兩面：一列講要不要改預設，本列講**在改之前每次跑測試都要手動設** | 操作流程；`Crane_control_PI/main.cpp:834` | **未修（需每次手動設）** ✔ | 2026-09-01（續十八） |
| 🔴 | 🆕 **單側在起步 1 秒內暴走 5cm（右繩張力掉到 7kg）** —— 2026-09-01 第 2 趟 `pay_out 20`：起步不到 1 秒、**一個 `[BAL]` tick 都還沒跑**（tick=250ms），右側多放 5cm（L+3/R+8），右繩張力一度 **7.07kg**（幾乎鬆繩），`tension_diff` 守衛於 51.0 中止。⚠️ **不是**續七那個「一側沒啟動」—— `sync_start` 啟動驗證通過、兩側都確認在轉。停止後右張力回到 22.6 ⇒ 那個 7 是運動中瞬態，但 5cm 位置差是真的。📌 假說（**未證實，n=1**）：起步瞬間重量分佈偏移 → 右繩卸載 → 開迴路 VFD 輕載下轉更快 → 放更多 → 正回饋。🔴 值得注意的是**平衡迴路在這個時間尺度上根本來不及作用**（250ms tick vs <1s 的事件） | `Crane_control_PI/main.cpp` motion_rope / balance tick | **未查明** ✔ | 2026-09-01（續十六）實測 |
| 🟡 | 🆕 **總張力讀值隨姿態變動超過 10kg** —— 同一台機器：歪 6.75° 時總和 81.9kg、水平時 92.8kg。✅ 這解釋了續十五記的「20cm 下降掉 4.7kg」＝**不是感測器漂移，是總和本身隨傾角變**（該列可結）。🔴 安全意義：`up_stop_total_kg=130` 吃的正是這個總和，而它會隨姿態浮動 10kg 以上 —— 訂門檻時要把這個浮動算進餘裕  | `Crane_control_PI/main.cpp` hold_loop | **已理解，門檻未調** ✔ | 2026-09-01（續十七） |
| 🟡 | 🆕 **單側量測收繩固定過衝約 1cm** —— `retract_right 3` 實走 4、`retract_right 2` 實走 3（同一晚、同一側、連續兩次）。與待辦表「`roll_correct` 致動解析度不足（指令 1cm 實動約 5cm）」同源但量級小得多，推測差別在 `side_measured` 短程走 10Hz 起步（log：`短程 3cm <= approach 8cm → 直接以 10Hz 起步`）。**下小步矯正時要預期多走 1cm** | `Crane_control_PI/main.cpp` `cmd_side_measured` | **未修（已知並可補償）** ✔ | 2026-09-01（續十七） |
| 🔴 | 🆕 **`balance_source` 的編譯預設是 `meter`，但 production 用的是 `imu`** —— 2026-09-01 冷啟動實測：重啟前 status 是 `imu`，重啟後變回 `meter`（`main.cpp:834` `g_balance_source {BalanceSource::Meter}`，註解「預設 Meter＝與現行逐位元相同」）。🔴 **後果不小**：續八對照組實測「關掉 IMU 平衡 → 平均 |roll| 0.68°→**2.52°**、出帶 22.7%→**67%**」⇒ **任何一次重啟都會靜默地把姿態控制退回那個狀態**，除了 status 之外沒有任何徵兆。與今天修掉的張力門檻是**同一類**問題（值只活在記憶體）。⏸ 未改預設：切到 Imu 依 `main.cpp:790` 的註解牽涉「放寬安全門檻（per user 2026-09-01 核准）」，屬於要使用者拍板的事，不自行改  | `Crane_control_PI/main.cpp:834` | **未修（待決策）** ✔ | 2026-09-01 冷啟動實測 |
| 🔴 | **`fine_adjust` 的 `level_diff`：機制已驗證、值已收斂到 5，但 🔴 可能根本不該是常數** —— 機制端對端確認（`pay_out 20` 實測：IMU 迴路自己停在 L−R=4，fine_adjust 算出 diff=0 正確不動作；**舊行為 `0` 會主動把機器拉到 L−R=0**）。斜率取得**三筆一致量測** −1.14／−1.05／−1.16 °/cm ⇒ **≈−1.1°/cm**，確認續十二 `−0.85` 的正負號正確（量值偏小）。反推水平點：`L≈74` 處為 **4.9 / 5.4 / 5.2** ⇒ 執行期已設 **`level_diff=5`**。🔴🔴 **但 `L≈50` 處反推是 ≈3.8** —— 下降 24cm、水平點移約 1.4cm ⇒ **單一常數撐不過 0→229cm 全行程**。（未證實：`L≈50` 那點在傾倒事件前、張力分佈不同，n 也小。）**⇒ 編譯預設維持 0，理由已不是「值不準」而是「它可能是函數不是常數」。** 🔧 下一步：在 `L≈50 / 150 / 229` 各量一次水平點，先確認是常數還是函數，再決定寫常數或做線性項  | `Crane_control_PI/main.cpp` `motion_fine_adjust_sync()` | **機制已修 ✔／值待定型** ✔ | work_log 2026-09-01（續十四→十七） |
| 🟡 | 🆕 **`0.85°/cm` 這個換算的定義／正負號要回頭釐清** —— 續十二由兩點（繩長差 3cm→+0.92°、8cm→−3.35°）得到 −0.85°/cm，今天多處推理靠它（含 `level_diff=4` 的取值）。但 2026-09-01 `pay_out 20` 實測L−R 由 3→4 時 roll 由 +0.91→+1.05（**+0.14°/cm，正負號相反**）。n=1 不足以推翻它，但足以說明**「繩長差」當時指的是不是 status 的 `length_left - length_right`，需要確認**（是 L−R？R−L？還是指令差而非讀值差？） | 量測定義；影響 `level_diff` 取值 | **待釐清** ✔ | 2026-09-01（續十五） |
| 🟡 | 🆕 **20cm 下降造成總張力掉 4.7kg（93.6 → 88.9，−5%）而機器並沒有變輕** —— 疑為滑輪／繩索在不同繩長下的摩擦與遲滯。**刻度校正後 kg 有絕對意義、而且安全門檻直接吃它**（`up_stop_total` 130 / `retract_tension_stop` 75），所以這個 5% 的漂移值得單獨查一次，不要當雜訊放過 | `user_lib/DSZL_107.cpp`；機構 | **未查** ✔ | 2026-09-01（續十五）實測 |
| 🔴 | 🆕 **重心偏左：水平與張力平衡互斥，要決定怎麼處置** —— 2026-09-01 實測：機器歪 −3.35° 時張力 46.9/45.9（1.02×），調到水平 +0.92° 時變成 59.7/34.6（**1.73×**）。**機器要水平，左繩就得承擔約 1.7 倍重量；兩側張力相等的那個狀態，機器是歪的。**三個選項：**配重**（治本，但要往 94 kg 的機器再加重量）／**改吊點**（不加重量，但要動機構且牽涉 `FOLLOWER_SPAN_CM` 幾何）／**接受並讓控制器補償**（純軟體，但 `roll_correct` 的最小可執行步比死區大，迴路結構上收斂不了）。🔴 **在這件事決定之前，調任何控制參數都是在補症狀** —— 對照組已證實姿態誤差是**單向漂移不是擺盪**。📌 這也改寫了三個先前的判斷：①「左右繩長相等」不是目標；② 平衡迴路製造左右差是**幾何要求**不只是暫態；③「下行比上行差」可能同源（放繩時張力低，重心偏移被放大） | 機構決定；影響 `Crane_control_PI/main.cpp` 的平衡與門檻 | **待決策** ✔ | work_log 2026-09-01（續十二） |
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
| 🟡 | MH300 實機必驗清單未跑：方向映射、電流 scale、2101H run bit、fault code | `Crane_control_PI/main.cpp`（`VFD_DIR_*` 巨集）、`.claude/mh300_migration_plan.md` | **未修** ✔（註解仍寫 `RE-VERIFY on MH300`） | work_log 2026-07-07 |
| ✅ | **4 個 `.vcxproj.user` 被 git 追蹤** → 不同 bench 的 Remote Target 互相覆蓋（Connection Manager 顯示空白）。⚠️ 原記 5 個，`windows_test/` 已於 `a69f82f` 整個移除 → 實際 4 個 | `Crane_control_PI/`、`Linux_test/`、`cleaning_arm/`、`facade_cleaning_v2/` | ✅ **已修（2026-08-29）**：四個檔 `git rm --cached`（**留在本機**）＋ `.gitignore` 加 `*.vcxproj.user`。移除前已確認「哪個專案建置到哪台 Pi」**不是唯一副本**（`.claude/runbook.md:22-23` 與 `CLAUDE.md:263-264` 都有）。📌 真正的理由是內容含只在該台機器有意義的連線 handle（如 `-1125135748`），本來就不可共用 | work_log 2026-07-15｜2026-08-29 複查確認 |
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
| 🟡 | 2026-07 那整批改動**從未編譯 / 部署驗證**（本機無法 remote build）：TCP_client 殭屍連線修復要驗自癒、WASH_ROBOT 安裝幾何常數、同步步伐、partial-seal 判準、crane 端 `Crane_control_PI` 建議先單獨 build 綠燈；`1829964` 等 commit 仍在本機 main **未 push** | `transport/TCP_client.cpp`、`app/WASH_ROBOT.{h,cpp}`、`Crane_control_PI/main.cpp`、`facade_cleaning_v2/main.cpp`、`web_backend/public/*` | **待查** | work_log 2026-07-07 / 07-15 / 07-21 / 07-22 / 07-23（7 筆合併） |
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

## 2026-09-02（續二十一）— 滑台手動歸零上線使用；行程 140→130；驗證與收尾

### ✅ 手動歸零流程首次實際使用

`rail_enable off` → per user 手推到左端硬限位 → `rail_enable on` → `rail_zero`。
當日新增的軟體失能（Pr4.02 DI1 改常開）讓這個流程成立，實際跑了兩次都正常。
📌 **零點現在確定落在左端硬限位**，不再是「開機當下的位置」。

### 行程 `ARM_RAIL_TRAVEL_MAX_CM` 140 → 130（per user）

先前的 140 是**舊零點**起算的可動距離、且不留餘裕。手動歸零後零點確定在左端硬限位，
per user 定為 130（實際保留約 10cm）。config + header 皆已更新並部署驗證
（啟動 log 印出 `ARM_RAIL_TRAVEL_MAX_CM = 130.0`）。

⚠️ **當時發現的衝突已處理**：per user 口述仍是「0-140-0」，但那是舊零點的數字；
從新零點下 140 有撞右端的風險，**改跑 0-130-0 並說明理由**。

### ✅ 完整驗證（新零點、新行程）

```
壓上後   M1 θ=0.6548  tau=11.48   M2=0.5209   吸盤 -66 -68 -67 -67
到 130   M1 θ=0.6624  tau=10.70   M2=0.6373   吸盤 -66 -68 -67 -68   3.7s
回 0     M1 θ=0.6525  tau=11.77   M2=0.2291   吸盤 -66 -68 -67 -67   3.7s
收回後   M1 θ=0.0490  tau=-0.05   M2=0.0109
```

滑台落點 129.999857 / 0.000000，吸盤全程零變化。

### 🔴 M2 在掃動中被帶著跑，幅度比先前更大

```
0.519 → 0.638 → 0.223     擺動 0.415 rad ≈ 24°（10 週期腳本 #1）
0.519 → 0.640 → 0.255     擺動 0.385 rad（#2）
```

先前（CONV_TOL=0.15 時）是 0.46→0.64→0.43，擺動 0.21。
⇒ 收緊容差讓 **起點** 更準，但**掃動中抵抗橫向摩擦的能力沒變**（HOLD 的 `hold_kp=7`，
當日沒動——因為當時的量測被容差假象誤導）。
📌 **現在知道「摩擦飽和」是假象之後，`hold_kp` 值得重新評估**，
但**必須在掃動中量**，不是在靜態換 slot 時量。

### ⚠️ 自記：我的測試腳本沒有收尾保護

10 週期腳本在第 3 輪被中止時，**滾筒繼電器仍開著、手臂仍壓在玻璃上** ——
滾筒在玻璃上空轉。這正是當日在程式碼裡修過兩次的同一類缺陷
（`do_arm_clean_sweep_` 的「abort 出口要無條件關滾筒」），**而我自己的腳本沒做**。
✅ per user 確認：日後循環測試腳本一律加 `try/finally`，中止時保證關滾筒 + 收手臂。

### 收尾狀態

滑台已手動歸零並停在 0；手臂 PARK、M1/M2 停用；推桿收回、真空釋放；**繼電器全 OFF**。
本體重啟為 130 行程版（單一實例），`motor_api` 單一實例執行中。

### 待完成

- 🔴 **M2 掃動中被帶著跑 0.39~0.42 rad**（唯一還沒解的機構問題）。要在掃動中量 `hold_kp`。
- 🟡 「M2 摩擦約 2 Nm」這個數字是容差假象下量的，仍待重新量測
- 🟡 400 RPM 的滑台失步未驗（要拿尺）

---

## 2026-09-02（續二十）— 🎯 「M1 起步頓一下」解決：元兇是一個 0.3 rad 的失效偵測探測

### 診斷：衝擊發生在「M2 停穩之後、move 開始之前」

`t02_1.csv`（雙軸 50Hz，取樣 22ms）：

```
t=1.75  M2 停穩（vel→0，tau 由 1.58 掉到 0.07）
t=1.88  M1 仍完全靜止  +0.0490  vel -0.0061  tau -0.147   | M2 靜止 tau +0.075
t=1.90  **M1 突然 +0.0712、vel +0.7998**                    | M2 已靜止 150ms
t=1.94  進入 MOVE，tau=17.534
```

⇒ **M2 不是元兇**（當日一度誤判為「M2 到位的機械衝擊」，還為此加了「等雙軸靜止」）。
衝擊落在 DEPLOY 序列**自己**的空檔裡。

### 🎯 元兇：`touch_wall_slot` 的被動狀態偵測探測

```cpp
float probe_setpt = cur_pos + (theta_target > cur_pos ? 0.3f : -0.3f);
for (int k = 0; k < 3; ++k) control_mit(s.hold_kp, s.hold_kd, probe_setpt, 0, 0);  // 3 x 20ms
```

**0.3 rad x hold_kp(90) = 27 Nm，持續 60ms，在每一次移動之前。**
而它的目的只是確認 `|tau| > TAU_LIVE_THRESHOLD = 0.3 Nm`。

📌 **`go_home_slot` 裡一模一樣的探測早就被改成 ±0.05，`touch_wall` 這處被落下** ——
而本檔註解甚至寫著「±1.0 rad probe … **produced a visible jerk right before every move**」。
**問題被描述過、被修過一半。**（當日第三次遇到「兩處只改一處」。）

### ✅ 探測偏移 0.3 → 0.05（0.05 x 90 = 4.5 Nm，是判定門檻的 15 倍）

| | max\|tau\| | max\|vel\| |
|---|---|---|
| 原始 | 18.71 / 18.61 / 2.59 | 1.72 / 1.74 / 0.23 |
| 等 M1+M2 靜止 | 9.72 / 11.18 / 2.88 / 17.24 | 0.38 / 0.34 / 0.14 / 1.53 |
| + CONV_TOL=0.02 | 17.53 / 17.34 / 17.24 / 17.34 | 0.80 / 0.95 / 0.63 / 0.84 |
| **+ 探測 0.3→0.05** | **3.37 / 3.57 / 2.78 / 2.59** | **0.20 / 0.20 / 0.13 / 0.13** |

⇒ **力矩尖峰降 5.5 倍、速度踢擊降 8.7 倍，四次全部一致、無離群值。**
速度峰值 0.13~0.20 遠低於 `M1_VEL_SAFETY_LIMIT = 0.4`。DEPLOY 耗時 7.7~9.2s（未變差）。

⚠️ 「等 M1+M2 靜止」是在錯誤診斷下加的（M2 在衝擊前已靜止 150ms），**但保留**：
不在手臂仍移動時開始新命令，本身是合理的穩健性。

### 📌 當日最後的方法論對照

**九次憑假設調參數，全部被量測推翻**；而**四次先量再改，全部成功**：
重力模型（12 點雙向掃描）、工具外伸量（實機手壓）、M2 位置（斷電手轉）、
以及本輪的兩件（M2 收斂容差、起步探測）——後兩件的關鍵都只是
**把診斷記錄點往前移三行**，把原本漏掉的區段錄下來。

---

## 2026-09-02（續十九）— 🎯 M2 疑案破了：不是摩擦、不是力矩，是一個 8.6° 的收斂容差

### 關鍵在把診斷記錄點移到 `enabled` 檢查之前

`lr_move_to_slot_impl` / `go_home_slot` / `lr_calibrate_slot` 進場時都會
`s.enabled.exchange(false)` 把該軸從 `feedback_loop` 摘出去自行控制。
DIAG 的記錄點原本在 `if (!s->enabled) continue;` **之後**
⇒ **那些自帶斜坡的區段完全錄不到**（先前 M2 波形從 t=0.28 跳到 2.72 的空隙就是整個斜坡）。
✅ 移到檢查之前後錄到完整波形（位置/速度/扭力來自 CAN 接收執行緒，與本迴圈是否服務該軸無關）。

### 🎯 波形直接給出答案

```
t=0.38~1.86  斜坡巡航，vel≈-0.48，tau≈-1.5~-2.1
t=1.86~2.47  進 creep 區，vel 由 -0.30 平順降到 -0.086     ← **自己減速，不是被卡住**
t=2.60       停在 -0.9337，cmd 才由 +0.5316 切為 -1.0115
t=2.6~5.2    位置完全凍結，tau 僅 -0.6 = hold_kp x err(7 x 0.078 = 0.55) ✅
```

⇒ **斜坡是平順地主動結束的**，停在距目標 0.078 rad 處；HOLD 接手只需 0.6 Nm 維持現狀
⇒ **根本沒有人在往目標推。**

**成因：`lr_move_to_slot_impl` 的 `CONV_TOL = 0.15f`（8.6°）** —— 進到目標 ±0.15 就宣告到位。
我們量到的 0.068~0.078 全部落在容差內。

🔴 **當日所有關於 M2 的推論都是這個容差造成的假象**：
「摩擦飽和 ~2 Nm」「位置受限」「保持力上限不足」——
把 `hold_kp` 由 7 加到 20（力矩 x2.4）位置紋風不動，正是因為它**早已「收斂」**。

### 📌 06-09 放寬容差的兩個前提，今天都不成立了

1. 「motor LEFT 方向實測只能到 ±0.5」——那是 **`lower_bound = -0.8` 把 LEFT 目標夾死**造成的，
   09-02 已修為 -1.05。**成因消失，容差卻留著。**
2. 「sweep 對角度精度要求不高」——09-02 起 `TOOL_EXT_*` 是在「工具頭確實位於 slot 位置」
   的前提下標定的，差 8.6° 就改變接觸幾何。

### ✅ `CONV_TOL` 0.15 → 0.05，實測有效

| | 落點誤差 | 回傳 |
|---|---|---|
| 0.15（舊） | 0.069 / 0.078 / 0.069 / 0.076 | OK（其實是容差寬鬆）|
| **0.05** | **0.041 / 0.039 / 0.038 / 0.041** | **OK（真的收斂）** |

**壓力驗證（TOOL_EXT 標定是否作廢）：**

| | 驗收「剛好」時 | 收緊後 |
|---|---|---|
| M1 θ | 0.6525 | 0.6548（+0.0023 ≈ 1.1mm）|
| tau | 11.77 | 11.48（−0.29 Nm）|
| M2 | 0.4576 | 0.4923 / 0.4931 |

⇒ 工具頭多轉 0.035 rad，壓力只變 0.3 Nm，**落在 10 週期測試中 per user 接受的
11.48~11.58 範圍內** ⇒ **`TOOL_EXT` 標定仍然有效，不需重做。**

### 待完成

- ✅ **已再收緊至 `CONV_TOL = 0.02` 並定案**：

  | CONV_TOL | 落點誤差 | 停滯 tau |
  |---|---|---|
  | 0.15（原始） | 0.069 ~ 0.078 | 0.6 ~ 0.9 |
  | 0.05 | 0.038 ~ 0.041 | 0.29 ~ 0.40 |
  | **0.02（定案）** | **0.0103 ~ 0.0126** | **0.06 ~ 0.17** |

  每檔 4 次全部真收斂、耗時不變（4.2~5.1s）。**誤差始終約為容差的一半 ⇒ 仍是容差限制、
  不是物理極限**，機構還有餘裕；停在 0.02（≈1.1°）是因為 0.011 rad ≈ 0.6° 已遠低於
  工具幾何在意的量級，再收緊只是壓縮低溫/負載變化的邊界餘裕。
  ✅ **壓力複驗**：M2 由 0.4576 推進到 0.5175~0.5198，而 `DEPLOY 520 RIGHT` 的
  M1 θ 0.6525 → 0.6548（+0.0023 ≈ 1.1mm）、tau 11.77 → 11.48~11.58，
  **落在十週期測試中 per user 接受的範圍內 ⇒ `TOOL_EXT` 標定不需重做。**
- 🟡 M1 起步踢擊是否因 M2 推得更久而改變，未複測
- 🟡 「M2 摩擦約 2 Nm」這個當日多處引用的數字要重新檢視——它是在容差假象下量出來的

---

## 2026-09-02（續十八）— 追 M2 根因：推翻「摩擦」模型，浮現一個未解的矛盾

### ✅ 保留的兩項

- **DIAG 擴充為雙軸**（M1 + M2，CSV 加 `motor` 欄）。M1 的成功經驗就是「先有 50Hz 波形再動手」。
- 🔴 **`LR_SLOT` 加入「M1 必須先離開玻璃」的守衛**（per user：「要先離開玻璃 再切換工具」）。
  工具頭貼著玻璃橫轉會刮傷玻璃與工具。`cmd_deploy_sequence` 的 Step 1 本來就會先收回 M1，
  但**裸的 `LR_SLOT` 指令毫無保護** —— ⚠️ **09-02 我自己錄波形時就在 M1=0.5713（伸出貼牆）
  的狀態下轉了 M2**，是 per user 當場指出的。守衛門檻 0.20 rad，實測正確擋下（M1=0.4503 時拒絕）。
  📌 只擋外部指令；DEPLOY 走 `lr_move_to_slot_impl`，不受影響。

### M2 換 slot 的 50Hz 波形

```
t=2.72  tau = -7.788            ← 斜坡段（自帶 MIT_KP≈30）力矩可達 7.8 Nm，移動毫無問題
t=3.18  pos 停在 -0.8017         目標 -1.0115，差 0.2098
t=3.2~5.9  tau 由 -1.55 緩爬到 -1.88，位置完全不動
```

⇒ 力矩在**斜坡段充足**，問題全在**交給 HOLD 之後**。

### ❌ 兩個修法，都被量測推翻

**① `HOLD_I_MAX` 2.0 → 5.0（想把 M2 積分上限由 1.2 Nm 提到 3.0 Nm）—— 完全無效**

瓶頸不是上限、是**累積速率**：`積分 += err x dt = 0.07 x 0.02 = 0.0014/tick`
⇒ 繞到 5.0 要 **71 秒**（舊上限 2.0 也要 28 秒）。實測等 6 秒時
`tau = 0.64 = hold_kp x err(0.49) + tau_i(0.25)`，完全吻合。**已還原。**
⚠️ 順帶更正命名誤導：該值 clamp 的是**積分狀態（rad·s）**，不是 N·m。

**② `m2_.hold_kp` 7 → 20 —— 力矩增 2.4 倍，位置幾乎不動**

| kp | 落點誤差 | 停滯 tau |
|---|---|---|
| 7 | 0.069 / 0.078 | 0.64 / 0.92 |
| 20 | 0.068 / 0.073 | **1.57 / 1.58** |

🔴 **這推翻了整天沿用的「M2 摩擦飽和」模型**：若是庫倫摩擦 F，平衡條件 `kp x err = F`
⇒ 誤差應隨 kp 反比縮小（0.07 → 0.024）。**實測沒有** ⇒ **不是力矩受限，是位置受限**：
某個東西讓 M2 就是停在 **≈+0.463（滾筒側）/ ≈−0.937（刮刀側）**。**已還原 kp=7。**

### 🔴 未解的矛盾（下一步的起點）

「位置受限」與兩件已確立的事實衝突：

1. **per user 徒手轉得到 +0.5316 / −1.0115**（09-02 手轉實測，各讀 5 次極差 0.0000）
2. **`LR_CALIBRATE` 量到的正向機械停點是 +0.7204**，遠在 0.463 之外

⇒ **既不是機械極限，也不是單純摩擦。**
🔧 下一步：**在 `lr_move_to_slot_impl` 的斜坡段錄 M2 的 50Hz 波形**
（現在 DIAG 已支援雙軸），看它究竟在哪一刻、以多大力矩、在什麼條件下停住。
⚠️ **在解開這個矛盾之前，不要再調 M2 的增益** —— 本輪兩次都證明沒用。

### 當日「憑假設調參數」累計：9 次，全部被量測推翻

斜坡夾制／park profile（兩次）／放寬速度安全閥／接觸快轉／收回 fast profile／
HOLD_I_MAX／m2 hold_kp，以及把使用者的「滑台只動一點點」追成標定錯誤。
✅ **唯一真正有效的，都是先取得量測再動**：重力模型（12 點雙向掃描）、
工具外伸量（實機手壓）、M2 位置（斷電手轉）、起步踢擊的成因（50Hz 內部記錄）。

---

## 2026-09-02（續十七）— 起步踢擊：查明是 M2 的機械衝擊，改為等雙軸靜止，3/4 改善但未根除

### 🔴 決定性證據：尖峰在 HOLD 中就發生，不是控制器造成的

`set2.csv`（取樣 22ms）：

```
t=1.68  pos=0.0486  vel=-0.0183  tau= -0.049   ← 已靜止 0.4s 以上
t=1.70  pos=0.0532  vel=+0.6044  tau=+12.454   ← **仍是 HOLD（move_act=0）**
t=1.72  pos=0.0650  vel=+0.1648  tau=+13.724   ← 仍是 HOLD
t=1.75  進入 MOVE                tau=+14.310   ← 只是繼承前一刻的狀態
```

HOLD 時 `err = −0.0032`、`hold_kp=90` ⇒ `kp×err = −0.29 Nm`。
**12.45 Nm 不可能是控制器下的** ⇒ 它是**馬達抵抗外力衝擊時的輸出扭力讀值**。

⇒ **控制器不是元凶，M2 傳來的機械衝擊才是。** 這一併解釋了為什麼當日前面
三個修法（斜坡快轉、放寬安全閥、收回改 fast profile）全部無效——它們針對的都不是成因。

### ✅ 修法：touch_wall 前等 **M1 與 M2 同時靜止**

第一版只等 M1（連續 3 次 |vel|<0.05、上限 1s），只擋住 2/3。
🔴 原因：**`wait_for_move(m2_)` 只確認 M2 的「軟體斜坡」跑完，不代表 M2 實體停了**——
M2 有摩擦飽和（保持力上限 ~2 Nm ≈ 實測摩擦），會持續爬行數秒，
**它到位/卡住的那一下正是打到 M1 的衝擊來源**。
⇒ 改為兩軸都要連續 4 次 |vel| < 0.05（上限 1.5s），未達成則記錄。

**轉換窗口（HOLD 尾段 + 起步）的峰值：**

| | max\|tau\| | max\|vel\| |
|---|---|---|
| 修正前 | 18.71 / 18.61 / 2.59 | 1.72 / 1.74 / 0.23 |
| 只等 M1 | 17.92 / 14.31 / 15.29 | 1.26 / 0.60 / 0.56 |
| **等 M1+M2** | **9.72 / 11.18 / 2.88 / 17.24** | **0.38 / 0.34 / 0.14 / 1.53** |

⇒ **4 次中 3 次明顯改善**（tau 中位數 18→10、vel 1.7→0.36），**但 #4 仍爆掉**。
代表衝擊**可以在兩顆馬達都讀到靜止之後才到**——與 M2 黏滑一致（卡住→讀數靜止→突然鬆脫）。
✅ 保留此改動：有量測支撐、代價最多 1.5s，而 DEPLOY 總時間 7.6~8.2s 未變差。

### 待完成

- 🔴 **起步踢擊未根除**（1/4 仍有 17 Nm / 1.5 rad/s）。真正的解法要處理 **M2 的黏滑本身**，
  而不是在 M1 這端等它。與「M2 摩擦飽和」是同一個根因——當日已第三次指向它
  （換 slot 慢 2.3s、掃動中被帶著跑 0.17 rad、起步衝擊）。
- 🟡 `M1_GRAVITY_MIN_VALID_RAD=0.20` 的低角度無補償仍未驗證是否有獨立影響
- 🟡 診斷記錄取樣抖動（22.2ms、偶有 190ms 空隙）未查

---

## 2026-09-02（續十六）— 「M1 起步頓一下」：加 50Hz 內部記錄查出真實波形；修法再次被推翻，但找到具體漂移機制

### ✅ 新增 `DIAG ON/OFF`（M1 診斷記錄器）

在 `feedback_loop` 內以控制迴圈速率記錄 `t,pos,vel,tau,cmd,move_act,hold_en` 到
`/home/nexuni/bringup/m1_diag.csv`。**純記錄，不改變任何控制行為。**

📌 **為什麼非做不可**：TCP 輪詢 `STATUS` 只有 ~10-20 Hz，而黏滑與控制迴圈都是 50 Hz
⇒ 輪詢抓到的是混疊值（當日曾據此算出正負號互相矛盾的「掙脫力矩」）。
⚠️ 指令要帶前綴：**`M1 DIAG ON`**，不是 `DIAG ON`。
📌 實測取樣間隔 **22.2 ms（≈45 Hz）而非 20 ms**，且偶有 190 ms 空隙——迴圈本身有抖動。

### 🔴 量到的真實波形（3 次 DEPLOY）

| | 起步 pos | 第一 tick vel | 第一 tick tau | 起步 10 筆 max\|vel\| |
|---|---|---|---|---|
| #1 | 0.0952 | **+0.7387** | **+18.02** | 0.74 |
| #2 | 0.0956 | **+0.7021** | **+18.02** | 0.70 |
| #3 | 0.0448 | +0.226 | +2.30 | 0.23 |

⇒ **2/3 出現 18 Nm / 0.7 rad/s 的起步踢擊，隨即反向彈回** —— 這就是 per user 說的「頓一下」。
（先前單次量到的 9.7 Nm 是同一現象的較小樣本；當時我一度以為是壓玻璃留下的
`hold_tau_ff` 汙染，**判別實驗證明不是**——自由空間起步的 Case A 完全沒有尖峰。）

### ❌ 修法：收回改 fast profile + 逾時記錄 → **更糟，已還原**

推論：DEPLOY 第一步收回後等 `pos<0.05`，最多 2 秒，**等不到也直接往下走且無任何記錄**；
#1/#2 起步時 pos=0.095 遠高於門檻 ⇒ 判定「收回逾時、手臂沒定位就起步」。
於是 (a) `use_park_profile` true→false（kp 5→90）、(b) 加上 `|vel|<0.05` 條件、(c) 逾時記錄。

**結果：**

```
改前 起步 pos 0.095/0.096/0.045  tau 18.0/18.0/2.3  max|vel| 0.70   出現 2/3
改後 起步 pos 0.079/0.083/0.079  tau 16.2/16.5/16.3 max|vel| 1.09~1.12  出現 3/3
```

🔴 **而且「收回逾時」記錄 0 次** —— 收回其實有完成（pos<0.05 且 |vel|<0.05 都達成），
**但起步時 pos 已經是 0.079**。⇒ **我的診斷只對了一半。**
✅ profile 已還原 true（維持 07-27 per user 的決定）；**逾時記錄與速度條件保留**（純穩健性）。

### 🔴 真正的機制：收回之後、touch_wall 之前，手臂被重力往外拉

| | 收回最低點 | 進入 MOVE 時 | 漂移 |
|---|---|---|---|
| kp=5（現行） | 0.0486 / 0.0486 / 0.0441 | 0.0952 / 0.0956 / 0.0448 | **+0.047** |
| kp=90（已還原） | 0.0044 / 0.0071 / 0.0044 | 0.0795 / 0.0833 / 0.0792 | **+0.075** |

那段空檔正是 **M2 在換 slot 的 ~2 秒**。

📌 **推論（有數據支撐，但尚未做判別實驗）**：
`M1_GRAVITY_MIN_VALID_RAD = 0.20` 以下**完全不套用重力前饋**，而重力變號點是 **0.1774**
⇒ 在 θ≈0.05 處**重力是往外推的（模型給 +2.05 Nm）**。手臂收回後在**無重力補償**的狀態下
被自身重力往外拉，2 秒漂到 0.08~0.095，`touch_wall` 就從那個「正在移動」的狀態起步。
**kp=90 收得更深（0.004）⇒ 漂得更多 ⇒ 踢擊更大**，與量到的方向一致。

⚠️ **今天到此為止不再改**：這是當日第七次「憑推論改參數」，前六次全被量測推翻。
   下一步應該先用 `DIAG` 做判別實驗（在 M2 換 slot 期間記錄 M1，看漂移速率是否符合
   重力模型在該角度的預測），確認機制後再動 `M1_GRAVITY_MIN_VALID_RAD`。

### 待完成

- 🔴 **起步踢擊未解**（18 Nm / 0.7~1.1 rad/s，遠超 `M1_VEL_SAFETY_LIMIT=0.4`）
- 🔴 判別實驗：M2 換 slot 期間 M1 的漂移是否＝低角度無重力補償
- 🟡 `M1_GRAVITY_MIN_VALID_RAD=0.20` 這個門檻同時牽涉起步震動、黏滑、本次漂移三件事
- 🟡 診斷記錄的取樣抖動（22.2ms、偶有 190ms 空隙）本身未查

---

## 2026-09-02（續十五）— 滾筒模式 10 週期耐久測試：全數通過，重複性極高

### 序列（per user 指定）

`滾筒繼電器 ON → 放手臂(DEPLOY 520 RIGHT) → 滑台 0→140→0 → 收手臂(PARK) → 滾筒 OFF`
站位：推桿 10cm、四吸盤密封、滑台 400RPM / acc-dec 3000。

### ✅ 10/10 通過，總耗時 244s

| 項目 | 範圍 | 散布 |
|---|---|---|
| 壓上角度 θ | 0.6540 ~ 0.6548 | **0.0008 rad ≈ 0.4mm** |
| 壓上力矩 tau | 11.48 ~ 11.58 | **0.10 Nm** |
| 掃動 0→140 / 140→0 | 3.9 / 3.9 s | 每輪完全相同 |
| 收手臂 | 3.8 ~ 4.3 s | 0.5s |
| 全輪 | 22.4 ~ 24.7 s | 2.3s |
| 吸盤 | −66 ~ −68 kPa | **全程零衰減** |

⇒ **壓上姿態散布僅 0.4mm**，代表當日定的 `TOOL_EXT_RIGHT=204.32` + `wall_mm=520`
在重複動作下非常穩定。

📌 **唯一明顯變動的是「壓上時間」7.0~9.2s（散布 2.2s）** —— 正是黏滑造成的，
每次掙脫時機不同。再次印證「慢」的根因不在參數設定（見續十四）。

### 兩個穩定重現的機構事實

- **`Δθ@140` = +0.0080 ~ +0.0107，10 輪全部同號同量級** ⇒ 140 端玻璃遠約 4~5mm。
  加上先前三輪的獨立量測（+0.0126 / +0.0122 / +0.0122），**確定不是雜訊**。
- **M2 每輪都被掃動摩擦帶著跑 0.46 → 0.64（0.17 rad ≈ 10°）**，10 輪完全一致。
  與 M2 保持力矩僅 ~2 Nm 同根因。

### 🔴 自記：第一版腳本讓失敗完全靜默

第一次跑時 #2~#4 的「壓上」都是 **0.0 秒、θ 停在 0.049（PARK 位置）**——手臂根本沒動。
成因：**`PARK` 會停用馬達**，而我只在迴圈**外**啟用一次 ⇒ 第 2 輪起 `DEPLOY` 立刻回
`ERR: M1 not enabled`。而**我沒有檢查回覆**，所以滑台照掃、log 照印，看起來像正常在跑。

✅ 修法兩點：
1. **每輪重新 `M1/M2 ENABLE`**
2. **不靠 DEPLOY 的回傳值判斷成功**——壓在可壓縮接觸上它本來就回 ERR（設計使然）。
   改用**實際姿態**判定：θ 落在 0.55~0.75 且 tau > 8 Nm 才算真的壓上，否則立即中止並印出原因。

📌 這是當日「讓失敗看得見」這條原則的又一個案例（對照 `trigger_sync_move`、
`LR_CALIBRATE` 兩個丟棄回傳值的缺陷）——**只是這次是我自己寫的測試腳本。**

---

## 2026-09-02（續十四）— 「手臂靠上去/切換工具太慢」：三次嘗試全被量測推翻，真因是黏滑

### 實測基準

工具切換 **各約 11 秒**（`DEPLOY 520 LEFT` 11.4s / `RIGHT` 11.0s）。分段量測：

```
① M1 收回(HOME)      2.5s
② M2 換 slot(LEFT)   2.3s
③ TOUCHWALL 指令     0.1s   ← 非阻塞，只負責啟動移動
```

### ❌ 三次嘗試，三次都被量測推翻（全部已還原，數據都寫進程式碼註解）

| # | 假設 | 改動 | 結果 |
|---|---|---|---|
| 1 | 收回用了慢 profile | `use_park_profile` true→false | 9.7/12.3/11.8 vs 11.4/11.0 —— **同散布內，無改善**。而且那一行是 **07-27 per user 明確要求**的，我憑假設翻掉了 → 已還原 |
| 2 | 靠上速度受限於安全閥 | `M1_VEL_SAFETY_LIMIT` 0.4→1.0 試跑 | 見下表 —— **不值得，已還原** |
| 3 | 斜坡在爬走不到的距離 | 接觸停滯偵測 + step×3 | 8.0/12.1/11.8 —— 無改善；tau 由 ~12 升到 13.5~13.8（快轉有效但不影響時間）→ 已還原 |

**#2 的量測**（自由空間 θ0.10↔0.55 往返）：

| 命令 | 實際峰值 | 耗時 | 落點誤差 |
|---|---|---|---|
| 0.30 | 0.4335 / 0.6532 | 1.57 / 1.52s | 0.030 / 0.006 |
| 0.50 | 1.0073 / 0.6654 | 1.15 / 0.92s | 0.055 / 0.053 |
| 0.70 | 1.1905 / 0.9585 | 0.94 / 0.66s | **0.073** / 0.045 |

① **追蹤本來就不乾淨**——命令 0.30 的峰值就有 0.4335（2.2 倍），**與當日起步震動量到的是同一個數字**
   ⇒ 09-02 的重力模型修正**並未消除超速**。
② 落點誤差隨速度惡化，0.70 時 0.073 已逼近 DEPLOY 收斂容差 0.08。
③ 只省 0.6s／全程 11s（5%）。

### 🔴 真因：黏滑（stick-slip）

密集取樣 DEPLOY 的伸出段，速度軌跡：

```
+0.104 +0.128 +0.140 +0.214 +0.006 +0.263 +0.128 +0.128 +0.055 +0.006
-0.006 +0.079 -0.018 +0.238 +0.311 +0.250 +0.250 -0.018 +0.153 -0.006
```

**在 0 與 0.31 之間反覆跳動** ⇒ 卡住→力矩累積→掙脫竄一下→再卡住。
0.50→0.69 rad 花 2.6s，**平均僅 0.07 rad/s，命令卻是 0.3**。

⇒ **手臂根本沒在跟隨斜坡**，所以加快任何斜坡都不會有效（這解釋了 #1 與 #3 為何無效）。
⇒ per user 現場描述「切換工具靠上時 M1 都會震一下」，指的就是這個。
📌 `M1_FRICTION_TAU=2.5` 是破靜摩擦用的，但程式碼他處記載某些角度靜摩擦 **≥4.6 Nm**，補償不足。

### 🔴 靜摩擦量測失敗 —— 兩個錯

1. **用 0.02 rad/s 慢掃** → 全程黏著，`|v|>0.03` 的有效樣本只湊出一個分箱，結果是垃圾
   （G=+4.44 vs 模型 −5.48）。⚠️ **當日早上的日誌就寫著「0.03 太慢會停在 stick-slip，需要 ≥0.1」**，我重複了同一個錯。
2. **更根本**：掙脫是瞬間事件，而 TCP 輪詢 `STATUS` 只有 ~10~20 Hz、控制迴圈是 50 Hz
   ⇒ **取樣追不上黏滑週期，抓到的是混疊值**。先前算出的三個「掙脫 tau 減重力」正負號互相矛盾，就是這個原因。

⇒ **要量準必須在 `motor_api` 內部以迴圈速率（50 Hz）記錄 pos/vel/tau 到檔案**，離線分析。未做。

### 待完成

- 🔴 **黏滑未解**：需先加 50Hz 內部記錄 → 量出靜摩擦對角度的曲線 → 才能決定 `M1_FRICTION_TAU`
  該是常數還是角度函數。**在那之前不要再調速度或斜坡相關參數**（今天已三次被推翻）。
- 🟡 M2 摩擦飽和（~2 Nm）造成換 slot 的 2.3s 與掃動中被帶著跑 0.18 rad —— 同類問題

---

## 2026-09-02（續十三）— 滾筒繼電器順序修正（又一次「兩份只改一份」）；滾筒模式掃動驗證

### 🔴 滾筒繼電器要在 DEPLOY **之前**開（per user）

「滾筒靠上前要開滾筒繼電器」——原本是先把滾筒壓上玻璃、再讓它開始轉。
靜止的滾筒頂著玻璃才起轉，對滾筒與玻璃都不好，清洗段的頭幾公分等於乾磨。

📌 **`do_step_sync_rail_sweep_`（步伐內建、生產實際跑的那條）08-28 就已經改對了**，
而且定義了開關窗口「DEPLOY RIGHT 之前開 → DEPLOY LEFT 之前關」。
🔴 **被落下的是另外兩條複本**：`do_arm_clean_sweep_`（乾掃測試）與
`do_arm_clean_sweep_continuous_`（連續清洗），兩者都還是「DEPLOY 後才開」。
⚠️ 程式碼裡明明就寫著「這段序列跟那邊是複製關係，**兩處必須一起改**」。

✅ 兩條都改成 DEPLOY 前開。連續清洗那條在兩個失敗出口補上關閉（該函式會 return 出去，
沒有後續關閉點）。

### ✅ 順手修掉乾掃路徑的同型 bug（08-28 在步伐路徑修過的那個）

乾掃路徑的滾筒**關閉點原本 gate 在 `deployed`**：

```
DEPLOY RIGHT 失敗 → deployed=false → 整段（含 DEPLOY LEFT）被跳過
                 → 滾筒一直轉沒人關、手臂停原位、滑台空掃兩趟
```

**這正是 08-28 per user 回報「從頭到尾都是 DEPLOY RIGHT 沒換」的成因**，當時只在步伐路徑修了。
✅ 改為：abort 出口**無條件關**（「沒開過時關它是 no-op，開著沒關才是問題」），
換邊條件 `deployed` → `init_ok`。與步伐路徑政策一致。

📌 我第一版是改成「DEPLOY 失敗就提早關」，那與 08-28 既定政策相反，已改回對齊。

### ✅ 滾筒模式全行程掃動（推桿 10cm、四吸盤密封、滾筒轉動並壓玻璃、400RPM）

```
壓上後   M1 θ=0.6506 tau=11.97   M2=0.4576   吸盤 -66 -68 -66 -67
到 140   M1 θ=0.6628 tau=10.60   M2=0.6399   吸盤 -66 -68 -67 -67    3.9s
回 0     M1 θ=0.6498 tau=12.06   M2=0.4469   吸盤 -66 -68 -67 -67    3.9s
```

吸盤全程穩定。M1 的 θ +0.0122 / 壓力 −1.4 Nm **重現了上一輪的結果**
⇒ 「140 端玻璃比 0 端遠約 6mm」是可重複的量測，不是單次雜訊。

### 🔴 新發現：M2 在掃動中被摩擦力帶著跑

```
0.4576  →(掃到 140)  0.6399  →(回 0)  0.4469        擺動 0.18 rad ≈ 10°
```

滾筒壓著玻璃橫移時，摩擦力矩把工具頭往行進方向推；回程推回來。
M2 保持力矩上限只有 ~2 Nm（今天量到的飽和值），撐不住。
⇒ **滾筒在掃動中不是固定姿態，去程與回程的接觸角不同**，直接影響清洗均勻度。
📌 與「M2 保持力矩不足、兩個 slot 都短 0.077 rad」是**同一個根因**。

### ⚠️ 自記：把使用者的觀察追成了錯誤方向

per user 問「為什麼剛剛滑台只動一點點」。我查 log 確認脈衝數學正確（140cm → 181,089 脈衝、
耗時 3.9s 相符），於是推論「`lead=7.731` 一定錯了、要拿尺重標」，還準備了量測流程。
🔴 **實際上他看到的是我自己跑的 `rail 1`（1 公分）預設值驗證。** 標定沒有問題。
📌 教訓：使用者說「剛剛」時，**先確認指的是哪一次動作**，不要直接跳到「量測值有問題」。

### 待完成

- 🔴 **per user：手臂靠上去 + 切換工具太慢，要加速**（切工具的慢正是 M2 摩擦飽和造成的爬行）
- 🔴 M2 掃動中被帶著跑 0.18 rad —— 與上一條同根因
- 🟡 `water_on` 把開滾筒綁著（乾式那輪不轉），per user 表示之後再處理

---

## 2026-09-02（續十二）— 上滑台整合完成：軟體失能、手動歸零、行程 140cm、全行程帶滾筒掃動驗證

### 🔴 讓伺服真的能軟體失能（原本做不到）

per user 要的流程是「失能 → 手推到左端 → 使能 → 歸零」。第一次試 `rail_enable off` **沒有關掉**。
查手冊（`doc/上滑台/DM2J-RS.V1.pdf`，已從 `tmp/` 搬出——`tmp/` 不進雲端鏡像，**第三次踩同一個坑**）：

```
DI1 出廠 = 使能(SRV-ON) + **常閉**，而 DI1 **沒接線** ⇒ 訊號恆觸發
Pr0.07 只有 0(交回 DI1) / 1(強制使能)，**沒有「強制失能」**
⇒ 這台在原始接線＋原始參數下，沒有任何軟體失能方式
```

📌 **per user 說「我有試過用指令關閉使能」**——查到 `work_log:6903`（2026-04-24）：驅動的**舊版
header 就寫著 `motor_disable() → 0x1801 = 0x2233`**，而 `0x2233` 實際是「**參數恢復出廠值**」。
⇒ 當時那次「試過」送出去的是出廠重設，不是失能。**這台從來沒有可用的軟體失能。**

✅ **解法**：`Pr4.02`(0x0145) DI1 功能 `136(0x88)` → **`8`**（保留 SRV-ON、只改成**常開**）
⇒ 未接線 = 未觸發，使能完全由 `Pr0.07` 決定。同時把 `Pr0.07=1` 一起存（否則重啟即失能）。
存 EEPROM 確認 `0x1901=0x5555`；**斷電重啟後回讀 Pr4.02 仍為 8**（出廠是 136）＝ 持久化確認。
✅ per user 實測：失能後**手推得動**，歸零流程成立。

⚠️ 選 `0x08`（改極性）而非 `0`（無效輸入）：前者是手冊明確定義的行為，且日後要接實體使能開關可直接用。

### 🔴 `rail_pos` 不是量測值

per user：「**因為沒有回授**」。驅動器的位置暫存器數的是**送出去的脈衝**：
- 失能後手推滑台，**讀數完全不變**（實測手推前後皆 0.000000）⇒ 歸零流程結束**必須** `rail_zero`
- **失步一律偵測不到**（08-28 實測 500 RPM 累積 0.2~0.3mm/橫越，只能拿尺量）

📌 `ARM_RAIL_LEAD_CM_PER_REV` 的 provenance 早就寫著「驅動器只數脈衝不可信」，
但那件事**一直沒有反映在指令介面上**，已補進 `cmd_rail_pos` 的註解。

### 🔴 行程 48 → 140cm

per user 現場告知：手動歸零後的零點起算尚可移動 **140cm**。
舊值 `48` 的 provenance 寫「實體行程 50cm 扣餘裕」，**與實測差近三倍，來源不明**。
（設計彙整當初就寫「滑台有效行程建議 1.2m 以上」，140 才對得上。）
⚠️ per user 指定上限即 140 ⇒ **不留安全餘裕**，與先前 50→48 留 2cm 的慣例不同。

### 參數調整（per user 實機逐段試）

| | 舊 | 新 |
|---|---|---|
| `ARM_SWEEP_RPM` / `DM2J_ARM_STEP_SWEEP_RPM` | 250 | **400** |
| `ARM_SWEEP_ACC/DEC` / `DM2J_ARM_STEP_SWEEP_ACC/DEC` | 100 | **3000** |

acc 單位 ms/1000rpm ⇒ 斜坡 = acc×rpm/1000。**舊值在實際轉速下等於瞬間起停**
（100 @250RPM = 25ms）。沿革註解寫「200→100 配合 RPM 1000」——那是 RPM 還是 1000 的時代，
08-28 降到 250 之後斜坡值沒跟著回頭檢討。
📌 邊際效益遞減：350→400 全程只快 0.2s（4.1→3.9s），斜坡時間隨轉速線性增長吃掉大部分增益。
⚠️ **400 的失步未經驗證**：已知 250 先前在用、500 不可用（0.2~0.3mm/橫越）。
   未主動安排驗證——08-31 per user 已否決「跑多趟找 RPM 上限」。

### ✅ 全行程帶滾筒掃動驗證（推桿 10cm、四吸盤密封、滾筒壓玻璃）

```
掃動前   M1 θ=0.6495  tau=12.06     吸盤 -66 -67 -65 -66
到 140   M1 θ=0.6621  tau=10.70     吸盤 -66 -67 -65 -66     3.9s
回 0     M1 θ=0.6510  tau=11.97     吸盤 -66 -67 -65 -66     3.9s
```

**四顆吸盤全程零變化** —— 400RPM/51.5cm/s 全行程掃動、滾筒壓著玻璃，機身吸附穩定。

📌 **順帶量到玻璃不平行**：140 端手臂伸得較出去（θ +0.0126）、壓力較低（−1.4 Nm）：
`Δreach = 490 × [sin(0.6621−0.38) − sin(0.6495−0.38)] =` **5.9 mm**
⇒ **140cm 那端的玻璃比 0 端遠約 6mm。** 目前由滾筒的可壓縮性吸收，不成問題，
但行程再加長會等比放大。

### 🔴 自記：兩個「註解說了謊」的錯，都是我寫的

1. **dispatcher 把 `250` 寫死，旁邊註解卻寫「省略則沿用 `ARM_SWEEP_RPM=250`」。**
   改常數成 400 後 `rail` 仍回 `rpm=250` 才發現。分派器看不到那些私有常數，
   **唯一正確做法是把預設值的決定權留在成員函式裡**（已改為傳 0 表示沿用預設，與 acc/dec 一致）。
2. **`rail_move` 用了 `dm2j_wait_done_` 的預設逾時 20000ms**，而 0→140 @50RPM 要 20.6s
   ⇒ 動作成功卻回 ERR（落點 139.999906）。已改為依行程計算：
   `(|Δcm|/線速度 + 斜坡時間) × 2 + 5s`，上限 180s。
   📌 同一支函式我先前還把**回傳慣例寫反**過一次（見續十一）——**三個錯都出在同一個新增指令上。**

### 待完成

- 🟡 **400 RPM 的失步未驗**（拿尺量是唯一方法）
- 🟡 `ARM_SWEEP_DECEL_MASK_MS = 1000` 未隨斜坡從 25ms 變成 1.2s 而重新檢討
- 🟡 玻璃在 140 端遠約 6mm——行程加長時要重新評估
- 🟡 缺一支唯讀的驅動器組態查詢指令（目前要讀 Pr4.02 只能跑會寫入的 `rail_cfg_soft_enable`，
  等於多寫一次 EEPROM）

---

## 2026-09-02（續十一）— 上滑台：補齊手動指令，解掉「測滑台前要先解決」那條前置

### ✅ 新增四支指令（解掉 09-02 標了一整天的前置）

日誌原文：「上滑台：**無指令可讀位置或設零點** ⇒ 設零點的唯一路徑是跑完整 `cmd_init`，
而它會關真空閥。**測滑台前要先解決這個。**」

| 指令 | 作用 |
|---|---|
| `rail <target_cm> [rpm]` | 絕對定位（0=左端、正向往右）；rpm 省略為 250 |
| `rail_pos` | 讀目前座標 |
| `rail_zero` | 設當前位置為零點（寫 `0x6002=0x0021`） |
| `rail_enable <on\|off>` | 伺服使能，供**手動歸零流程**用 |

**手動歸零流程（per user 提出）**：`rail_enable off` → 人手推到左端硬限位 →
`rail_enable on` → `rail_zero`。
📌 **比「開機當下的位置就是 0」可靠得多** —— 左端硬限位是真實的物理基準，
而舊做法的零點取決於上次斷電前有沒有照流程移回 0（流程保證，非機制保證）。
📌 per user 澄清：**煞車與使能是分開的**（斷電煞車只在斷電時咬住），上電狀態下手推得動。
   我原本擔心「煞車咬住推不動」，該顧慮不成立、已從註解移除。
📌 使用時機：**只在上電後有需要時才做**，不是每次動作前都跑。
🟡 仍未驗證：`motor_disable()` 寫 `0x000F=0` 是「解除強制使能（交回 DI1）」，
   是否真的鬆開取決於 DI1 接線狀態。

⚠️ **不要改用 Pi 上獨立的 `rail_pos` / `rail_move_drv` 工具替代**：它們會對 `.20`
另開一條 TCP 連線，而滑台(slave 14)與 ZDT 推桿 5~8、PQW 12 同在該匯流排，
兩個客戶端的 RTU 幀會在核心緩衝區錯位 —— 那正是 09-01 匯流排卡死的成因。

### 實測（推桿 10cm、四吸盤 −66/−67 密封）

```
0 → 3cm @100RPM   落點 2.999628   誤差 0.0004cm = 3.7 µm
3 → 0cm @100RPM   落點 0.000000
0 → 3cm @100RPM   落點 2.999628   （與第一趟同值，重複性一致）
```

✅ **per user 確認：正方向 = 往右**，與 `WASH_ROBOT.h:624` 既有記載一致。
⚠️ 但讀數是驅動器**自己的計數**，不是外部量測 —— **失步它看不出來**
（per user：「不知道有沒有失步，只能選擇相信」）。要驗失步只能拿尺量記號，
08-28 就是這樣測出 500 RPM 累積 0.2~0.3mm/橫越的。

### 🔴 自記：把回傳慣例寫反了

`rail_move` 初版寫 `if (!dm2j_wait_done_(...)) return "ERR ..."`。
而 `dm2j_wait_done_` 遵循 `projects/CLAUDE.md` 的慣例（**無異常回傳 `false`**）：
完成→false、通訊錯誤／故障／逾時→true。⇒ **動作成功時才回 ERR**。
實測 `rail 3 100` 位置精準到 3.7µm 卻回 `ERR rail_move_timeout` 才發現。
📌 **這條慣例就寫在我讀過的 CLAUDE.md 裡**，仍然弄反了。
✅ 已修（並改掉會誤導的訊息——該函式把通訊錯誤／故障／逾時壓成同一個 true，
不能宣稱是哪一種）。稽核其他呼叫端：**只有這一處用到，無既有錯誤。**
🟡 修正版已建置為 `facade_cleaning_v2.new` 但**尚未換檔**（換檔要重啟、會放掉真空），
   測試期間 `ERR rail_move_timeout` 實際代表成功，以 `rail_pos` 為準。

### 待完成

- 🟡 **換上修正版 binary**（下次自然脫離時）
- 🟡 大行程（17cm＝`ARM_SWEEP_CM`）與累積失步的**拿尺**驗證
- 🟡 `rail_enable off` 是否真的能手推（DI1 相依）未驗

---

## 2026-09-02（續十）— 解掉「INIT 重設零點」：真正的破壞路徑不在校正，而在一個門檻

### 🔴 找錯過一次：破壞路徑不是 `lr_calibrate`

原本認定「`INIT` → `lr_calibrate` → `set_zero` 會毀掉 09-02 手轉實測的 M2 絕對角度」。
實跑一次 `LR_CALIBRATE` 才知道**這條路根本走不到 `set_zero`**：

```
Phase 1  (正向) → 停點 pos=+0.7204，tau 3.44      ✅ 明確撞到
Phase 1B (負向) → 走到 −1.2831 仍以 0.5 rad/s 前進、tau 僅 −1.8（純摩擦）
                  🔴 max_travel(2.0) 用盡 abort ⇒ **該側在可及範圍內沒有停點**
```

而 Phase 1B 失敗會在 `set_zero` **之前**就 return ⇒ 校正從來沒有真的重設過零點。

📌 **順帶推翻兩個長期假設**：
- `ZERO_OFFSET=0.8` / `lr_half_range=0.7275` 描述的「對稱 ±0.76 行程」**不存在**。
  真實行程是 **+0.72 到 −1.28 以外**，跨距至少 2.0 rad，嚴重不對稱。
- `max_travel = 2*ZERO_OFFSET + 0.4 = 2.0` **比真實行程還小**，所以負向搜尋必定超時。

### 🔴 真正的破壞路徑：`cmd_init_sequence` 的陳舊偏移守衛

```cpp
// Physical travel is ~±0.76 rad; beyond 3 rad is stale — force set_zero
if (std::abs(pos) > 1.5f) { ... set_zero ... }
```

**註解說「超過 3 rad 才算 stale」，程式碼判的是 1.5** —— 兩者本來就不一致。
而 ±0.76 這個前提是錯的 ⇒ **M2 停在 −1.6 這種完全合法的位置時，INIT 會判定「陳舊」
並就地 `set_zero`**，零點被設在任意位置。這才是會讓實測絕對值全部作廢的那條路。
✅ **門檻 1.5 → 3.0**（採用註解本來就宣稱的值），並更正註解裡的行程描述。

### ✅ 停點相對定位（讓零點就算掉了也能還原）

正向停點是唯一可靠的物理基準（兩次量測 **0.7204 / 0.7208，差 0.0004 rad**）。
slot 目標改以它為基準表示：

```
M2_SLOT_LEFT_FROM_STOP  = -1.7319   刮刀 = 停點 - 1.7319 = -1.0111（手轉實測 -1.0115）
M2_SLOT_RIGHT_FROM_STOP = -0.1888   滾筒 = 停點 - 0.1888 = +0.5320（手轉實測 +0.5316）
M2_SLOT_CENTER_FROM_STOP = -0.7204
```

- `lr_calibrate_slot` 的 Phase 1 成功即寫入 `lr_stop_pos`（**即使 Phase 1B 之後失敗也保留**）
- 萬一將來真的走到 `set_zero`，同步 `lr_stop_pos -= midpoint` 平移
- `lr_stop_valid` 為 false 時退回絕對值常數
- ⚠️ **刻意只用正向那一個停點**，不用「兩停點取中點」——負向沒有停點，中點模型對這個機構不成立

**實測驗證**：`LR_SLOT RIGHT` 落點 0.4557（目標 0.5320，短 0.076）、
`LR_SLOT LEFT` 落點 −0.9337（目標 −1.0111，短 0.077）。
左右對稱短少 ≈ `7×0.076 + 1.2 = 1.73 Nm`，正是已知的 M2 摩擦飽和，與定位機制無關。
📌 對照修改前：刮刀目標 −0.6775 且被 −0.8 下界夾死；現在 −0.9337，
比 per user 目視認可的 −0.9165 更靠近工作位置。

### ✅ 順帶修掉的兩個回報缺陷

- **`LR_CALIBRATE` 丟棄 `lr_calibrate_slot` 的回傳值，失敗也回 `OK`**
  （與 08-29 修過的 `trigger_sync_move()` 同一類）。已改為回 `ERR ...`，實測生效。
- `cmd_init_sequence` 的標頭註解寫「`LR_CALIBRATE RIGHT`」，實際傳的是 `seek_left=true`（正向）。
  ⚠️ **我一度據那句過期註解斷定「INIT 找的是沒有停點的那側」，是錯的**，已更正註解。

### 待完成

- 🔴 **M2 摩擦飽和仍未解**：保持力上限 1.73~2.22 Nm，兩個 slot 都短 ~0.077 rad。
  加大增益前要先確認那 ~2 Nm 是機構本來就緊還是異常。
- 🟡 **負向究竟有沒有機械停點未知**（2 rad 內沒有）。要嘛加大 `max_travel` 找出來，
  要嘛承認沒有——目前的單停點設計不依賴它，但 Phase 1B 每次都會白跑 4 秒並回報失敗。
- 🟡 `M1_GRAVITY_MIN_VALID_RAD`/起步震動仍未解（見續九）。

---

## 2026-09-02（續九）— M1 從 0 點起步震動：現象確認、修法失敗、結論收回

### 現象（per user 現場觀察）

「手臂 M1 從 0 點要啟動時會震一下」。以 `MOVETO 0.45 0.15` 量測：

```
峰值速度 0.4335 rad/s = 命令速度 0.15 的 2.9 倍，發生在 pos≈0.25
🔴 已高於 M1_VEL_SAFETY_LIMIT(0.4)
```

⚠️ **0.4335 正是 2026-08-18 記下的同一個數字** ⇒ 現象可重現，
而且**當日的重力模型修正（K 20.87→16.09）沒有消除它** —— 成因不在重力前饋。

### 試過的修法：斜坡參考領先量夾制 → 無可量測效果，已還原

推論：`pos < M1_GRAVITY_MIN_VALID_RAD(0.20)` 時重力前饋**硬設為 0**，而該區靜摩擦約 2.3 Nm
→ 手臂卡住、斜坡 `move_cur` 仍以命令速度前進 → `kp × 誤差` 累積 → 掙脫瞬間一次釋放。
於是限制 `move_cur` 領先實際位置不超過 0.05 rad（M1 only —— M2 的領先量正是它唯一的
出力來源，夾了會把出力砍到 0.35 Nm，而它光摩擦就要 ~2 Nm）。

**三次量測：夾制前 0.4335 (2.9x) → 夾制後 0.5067 (3.4x) → 還原後 0.4823 (3.2x)**

### 🔴 自記：我下了一個資料撐不住的因果結論

看到 0.4335 → 0.5067 我當場宣稱「**夾制讓情況變差**」，並據此還原。
還原後量到 0.4823 才發現：**三次的散布 ±0.04 蓋過了三者的差距，這個實驗根本沒有結論。**
能說的只有「夾制沒有帶來可量測的改善」。**已收回該說法，程式碼註解也改成誠實版本。**

📌 **這是當日第三次同型錯誤**（前兩次：單點反推重力偏 2 倍、拿一分鐘後的讀數質疑
使用者的現場判斷）。**共同模式：拿單次量測下因果結論。**
✅ 對照組：同一天量吸盤密封與 M2 三個位置時都要求「連續 N 次相同」，那些結論都站得住。
**差別只在有沒有重複取樣 —— 不是判斷力問題，是流程問題。**

### 📌 收工時的一個誤判（記下來免得再犯）

收腳後看到吸盤壓力變成 0/1/1/1、`ch1=valve` 關閉，我先當成異常去查 log。
實際是 **`pusher all retract` 自己做的**：

```
[vacuum_release] all released after 300ms
[2stage_retract] CH6 ON (break-vacuum charge)   ← 正壓閥破真空
[2stage_retract] CH6 OFF
```

**兩段式收腳＝先放真空 → 打正壓破真空 → 才收腳**，是設計行為（收腳本來就是脫離動作的一部分）。
⇒ 校正全程真空正常。我在下收腳指令時還說了「真空仍在（重啟時才放）」——**那句話當下就是錯的**，
指令本身就會放掉。⚠️ **`pusher all retract` 不是單純的機構動作，它會解除吸附。**

### 待完成

- 🔴 **震動未解決**，且峰值 0.43~0.51 始終高於安全閥門檻 0.4（每次起步都在觸發邊緣）
- 🔴 **下一步不要再加補償機制、也不要再調夾制值**，先做兩件事：
  ① **改善量測** —— 單次峰值散布太大，需重複取樣才分辨得出效果
  ② **驗證增益** —— `hold_kp=90` 是當日為補償**錯誤的重力模型**從 34 一路加到 90 的，
     重力已修正，`kp=90 / kd=5` 很可能過度欠阻尼，先查阻尼比再說

---

## 2026-09-02（續八）— M2 手轉三點校正 + 工具外伸量重量，滾筒與刮刀均 per user 驗收「剛好」

### 量測結果（推桿 10cm、四吸盤 −66/−67 kPa 密封，同一站位）

**① M2 斷電手轉，三個工作位置的絕對角度**（各讀 5 次，極差皆 **0.0000**）：

| 位置 | 手轉實測 | 舊程式目標 | 差 |
|---|---|---|---|
| 滾筒 RIGHT | **+0.5316** | +0.6275 | +0.0959（5.5°）|
| CENTER | **−0.4099** | 0 | +0.4099（23.5°）|
| 刮刀 LEFT | **−1.0115** | −0.6775 | **+0.3340（19.1°）** |

舊值由單一個 `lr_half_range=0.7275` 對稱推導，兩邊退讓量卻不一樣大（0.05 vs 0.10）
——**那正是機構不對稱的證據，卻被硬塞進對稱模型**。實測中點在 −0.2400、半幅 0.7716。
✅ 已改為兩個獨立實測常數 `M2_SLOT_LEFT_RAD / M2_SLOT_RIGHT_RAD`，不再經 `half_range` 換算。
（CENTER 維持 0 —— per user「用原本的就可以，沒差」。）

**② M1 手壓貼合角 → 工具外伸量**（M1 斷電、使用者手壓到完全貼合，各讀 6 次）：

| | 貼合 θ | 算出 TOOL_EXT | 舊值 | 差 |
|---|---|---|---|---|
| 滾筒 RIGHT | 0.5728（極差 0.0004）| **204.32** | 148.09 | +56.23 |
| 刮刀 LEFT | 0.6006（極差 0.0003）| **192.37** | 134.07 | +58.30 |

🔴 **兩個都少了約 57mm，而且是同一個量**；兩工具的**相對差**（11.95 vs 舊 14.02）幾乎沒錯
⇒ **08-18 那次左右對調是對的，錯的是共同偏移。**
📌 來源幾乎確定是當日的 `ARM_LENGTH_MM` 320→490 —— 這兩個值是在**舊臂長**下反推的，
把臂長誤差整個吸收了。**日後再動 `ARM_LENGTH_MM`，這兩個必須一起重量。**

**③ 驗收**：`wall_mm=520` 下 `DEPLOY 520 RIGHT` → θ=0.6525 / tau 11.77 Nm，
`DEPLOY 520 LEFT` → θ=0.6823 / tau 11.09 Nm，**兩者壓入量 0.0797 與 0.0817 幾乎相同**
（代表兩個 TOOL_EXT 彼此自洽），**per user 目視兩者皆「剛好」**。

### 🔴 M2 保持力矩不足以到達任何目標（本次最重要的發現）

M2 每次移動都停在目標之前，tau 穩定在 1.9~2.2 Nm。代入公式完全吻合：

```
保持力上限 = hold_kp × 誤差 + hold_ki × HOLD_I_MAX = 7 × err + 0.6 × 2.0
  刮刀端 err 0.095 → 1.87 Nm（實測 1.9）
  CENTER  err 0.145 → 2.22 Nm（實測 2.19）
  滾筒端 err 0.123 → 2.06 Nm（實測 2.02）
```

而程式註解寫的是 `ki*HOLD_I_MAX=1.2 Nm > ~0.8 Nm friction` ——
🔴 **假設摩擦 0.8 Nm，實測約 2 Nm，低估 2.5 倍。**

📌 **不是局部緊點，是全行程都這樣**（三個位置的飽和值都吻合公式）。
⇒ 「手轉實測 vs 程式目標」的差距**不能全算在目標值錯誤上**，有一部分是馬達根本到不了。
📌 **這件事三個月前就被撞見過**：`[2026-06-11e]` 的註解正是「測試硬體是否真飽和」，
`[2026-07-24]` 記「LEFT 實測不夠過去 → 把 target 拉近」——
**當時的處理是把目標移近，而不是補足力矩**，跟當日 M1 用 `hold_kp` 補償重力誤差同一個模式。

### ✅ 順帶解掉的兩件事

- **零點撐得過 `motor_api` 重啟** —— 兩次重啟前後 M1/M2 讀值差 **0.0000**。
  解掉 `main_api.h` 從 08-13 掛到現在的「看起來會但沒驗證」。零點存在馬達韌體裡。
- **`m2_.lower_bound` −0.8 → −1.05**：刮刀在 −1.0115，**超出舊下界 0.2115 rad**，
  `MOVETO`/`LR_SLOT` 一律被夾住 ⇒ 程式從來沒把刮刀轉到工作位置過。

### ⚠️ 自記：本輪三個錯誤

1. **多送的 `M2 HOLD` 破壞了正確的保持目標。** `hold_slot()` 是 `hold_pos = Get_Position()`，
   而移動結束時程式**本來就已經**設好 `hold_en=1` 且 `hold_pos = move_target`。
   我在移動後補送 HOLD，把 −1.0115 覆蓋成卡住的 −0.7433，然後拿「tau≈0」去推論機構卡死。
2. **把 `err` 半位元組稱為「錯誤碼」。** 實際是**狀態碼**：0=disabled、1=enabled，只有 ≥0x8 才是故障。
   當時 M2 顯示 `err=0x1` 差點被我當成故障 —— 而同一時刻 M1 顯示 `err=0`，它正是斷電狀態。
3. **用 CENTER 的壓力常數（over-command 0.294）跨工具套用。** CENTER 那次手壓角 0.6731 與
   馬達落點 0.675 幾乎相同 ⇒ 該工具幾乎不可壓縮；滾筒與刮刀都會壓縮 0.08 rad。
   **這個常數不能跨工具套用**，只是碰巧兩個可壓縮工具彼此一致。

### 待完成

- 🔴🔴 **`INIT` 會跑 `lr_calibrate`，而它會重設零點 ⇒ 新的 `M2_SLOT_*_RAD` 絕對值全部作廢。**
  本輪為此**刻意沒有跑 INIT**。**正常流程恢復前必須先解決**（改成校正後回寫偏移、或讓 slot 目標
  改用相對於校正零點的量）。
- 🔴 **M2 保持力矩不足**：需要提高 `hold_kp` / `HOLD_I_MAX`，但**那 2 Nm 摩擦是機構本來就緊、
  還是有異常，尚未確認**——加大力矩前要先查，硬推可能傷機構。
- 🟡 **本體 binary `facade_cleaning_v2.new` 已建置但未換檔**（含 `ARM_M2_TOOL_*` 同步）。
  換檔要重啟，而 `cmd_shutdown` 會關掉 `CH_VALVE`/`CH_PUMP` ⇒ **重啟等於放掉真空**，
  本輪刻意不做（arm_deploy 的幾何全在 motor_api，本體那份只給死碼 `verify_arm_deploy_` 用）。
- 🟡 `TOOL_EXT_CENTER = 160.00` 是三者中唯一未經實測驗證的，且它是 `wall_mm=520` 的錨點。

---

## 2026-09-02（續七）— 🎯 **重擬重力模型，下垂縮小 12~28 倍**；#1/#2/#3 三項處理結果

### ✅ #3 `go_home_slot` 速度安全閥（已部署）

把 `M1_VEL_SAFETY_LIMIT` / `M1_EMERGENCY_BRAKE_KD` 由 `feedback_loop()` 的**區域常數抽到檔案範圍**，
並在 `go_home_slot` 的 ramp 迴圈加入同樣的煞車（kp=0、只留 kd，**保留重力前饋**——
08-17 已證實煞車時把 tau_ff 歸零等於放掉重力補償、數學上撐不住手臂），
煞車後把 `cur_cmd` 重錨定到真實位置。

📌 **抽成共用不是為了整潔**：原本安全閥只在 `feedback_loop` 的 HOLD/MOVE 分支，
而 `go_home_slot` 進入時把 `s.enabled=false`，`feedback_loop` 因此跳過 M1 ⇒ **整段 ramp 無保護**。
2026-08-18 的註解記過這個缺口但沒補；當日實測 |vel| 達 **1.5069 / 1.5690 / 2.2283 rad/s**
（三次獨立量測，限制值的 3.8~5.6 倍），而 08-18 記的只有 0.4335。**嚴重度被低估一個數量級。**

### 🔴 #2 `verify_arm_deploy_` —— 我先前的描述是錯的

**它不是「被補償值弄壞」，而是 2026-06-06 起就無條件 `return false`**（bench 沒有真牆、每次都會誤報），
下面整段是死碼。三個月來障礙偵測完全沒有在跑。

而那段死碼揭露了另一件事：**本體有一份自己的手臂幾何常數**
（`ARM_M1_LENGTH_MM` 等），與 `cleaning_arm/main_api.h` 各自獨立。
🔴 **當日的臂長修正（320→490）只改了手臂側，本體那份仍是 320 —— 我親手製造了分岔。**
✅ 已同步並在兩邊互相標註。（不能真的共用：`cleaning_arm` 是刻意的獨立服務邊界。）

🔴 **仍不能打開**：DEPLOY 壓著玻璃時，命令角（0.969）與實際角（0.675）的 0.29 rad 落差
**是壓力的來源、不是故障**，拿它跟命令角比對必然誤判。
🔧 **正確修法**：`verify_arm_deploy_` 應比對**校正過的預期接觸角**（每 slot 存一個），
而不是由 `wall_mm` 反算的命令角。這是設計改動，尚未做。

### 🎯 #1 重擬重力模型 —— 當日最大的成果

**量法**：推桿 19cm 把機身撐到距玻璃 29cm（用修正後的幾何算出可掃到 θ≤0.73），
M1 在 **0.42↔0.65 往返 3 趟、速度 0.15 rad/s**（0.03 太慢會停在 stick-slip，當日實測過），
`G = −(T_out+T_in)/2` 消掉摩擦。12 個分箱點（每箱 ≥8 個 |v|>0.05 樣本取中位數）：

```
新擬合  G(θ) = 16.09 × sin(θ − 0.1774)    殘差 RMS 0.163 Nm、最大 0.349
現行    G(θ) = 20.87 × sin(θ − 0.1754)
```

**相位幾乎相同（差 0.002 rad），錯的只有振幅：高估 30%。**
📌 舊值的出處註明「**兩個外側乾淨點**解出」——兩個點、且靜態量測**必然混著摩擦**
（當日量到摩擦 0.39~1.86 Nm）。這正是我自己早上用單點反推時偏 2 倍的同一個坑。

**效果（同一站位、自由平衡）：**

| | 命令 θ | 落點 θ | 下垂 |
|---|---|---|---|
| 舊模型 | 0.5481 | 0.4786 | **0.0695** |
| 新模型 | 0.4895 | 0.4870 | **0.0025** |
| 新模型 | 0.5307 | 0.5247 | **0.0060** |

**下垂縮小 12~28 倍（約 3mm 以內），而且 `arm_deploy` 首次回傳 `OK`。**

### ⚠️ 我要修正一個當日稍早寫下的錯誤說法

我曾把「`wall_mm` 被當成力道旋鈕」寫成錯誤用法。**那個說法不對。**

`TOUCHWALL` 是對**可壓縮接觸**下的**位置**命令，壓力來自 `kp × (命令角 − 實際角)`。
把目標設成幾何真值（388）⇒ 誤差趨近零 ⇒ **沒有壓力**
（實測 `arm_deploy 388` 落在 0.5941、`tau` 僅 +0.34 Nm ＝ 輕觸）。
⇒ **超量命令是產生壓力的唯一方式，不是誤用。**

**真正被修好的是自由空間的準確度** —— 而障礙偵測需要的正是那一段。

### ✅ `ARM_WALL_MM_DEFAULT` 最終值 520

`arm_deploy 520` → θ=0.6750、tau=+18.90，與 per user 認可的貼合姿態（θ=0.6731、tau≈19.0）
差 **0.0019 rad ≈ 0.9mm**。（沿革：400 → 380 → 530 → **520**。）

### ⚠️ 自記：當日反覆出現的三類錯誤

1. **忘了套用自己當天寫下的教訓**：`pump on` 回 `OK` 但 `ch2=0`，per user 發現「pump 沒開」。
   我當天稍早才把「`cmd_pump` 會謊報成功、一律要回讀」寫進日誌，然後自己沒做。
2. **穩定判定太早**：吸盤讀一次就下結論（上排其實只是慢 8~10 秒）。之後改為「連續 3~4 次相同才判定」。
3. **用會匹配到自己命令列的樣式數行程**（`pgrep -f` / `grep -c`），當日至少五次，
   兩次誤判「仍在跑」、一次誤判「有兩個實例」。**正解**：`pgrep -x <exe>`
   或 `ps -eo pid,args | awk '$2=="./exe"'`。

### 待完成

- 🔴 **`verify_arm_deploy_` 改為比對校正過的接觸角**，然後才能移除那個 `return false`
- 🟡 **`hold_kp=90` 是舊模型下的補償**，重力修正後可能可以調回較低值（震盪風險較小）
- 🟡 **重力擬合區間 0.42~0.64**，0.65 以上仍是外推（受玻璃阻擋）；與舊擬合區在 0.64 差 2.17 Nm，
  剛性手臂不可能不連續 ⇒ **至少有一邊的量測是錯的**，採信新值（12 點 vs 2 點）但未驗證高角度
- 🟡 四顆推桿接觸力道不均（10cm 時 peakI 589~1264 mA）＝機身與玻璃不平行

---

## 2026-09-02（續六）— 🔴🔴 **幾何模型錯了 1.53 倍**；「不是馬達出力不夠」有了證據；手臂貼合定案

### 🎯 三點量測推翻 `ARM_LENGTH_MM = 320`

per user 設計的量法：**四顆推桿伸到固定行程頂住平整玻璃 → 機身被撐在確定 standoff →
M1 斷電 → per user 手動壓到貼平 → 讀編碼器角度**。推桿行程精確（3000 脈衝/cm，五條獨立證據），
是可靠的自變數。

| 推桿 | 機身距玻璃（per user 尺量） | θ（8 次讀值，離散 0.0004） |
|---|---|---|
| 10.0 cm | 20.0 cm | 0.5419 |
| 12.5 cm | 23.0 cm | 0.5941 |
| 15.0 cm | 25.5 cm | 0.6464 |

Δθ 兩段各 **0.0522 / 0.0523** —— 線性，且第三點**命中預測到 0.0001 rad**。

最小二乘：**機身距離 = 490 × sin(θ − 0.38) + 121.0 mm，殘差 +0.0 / −0.1 / +0.0 mm**

🔴 **舊值 320 每次只算到實際的 65%**（機身移 50mm，模型算出 32.7mm）。
⇒ **這是今天所有校正問題的共同上游**：`theta_target` 由 `wall_mm` 經此式算出，
每個 `wall_mm` 對應的角度都偏小，所以幾何量到的牆距完全不能用、必須靠經驗把
`ARM_WALL_MM_DEFAULT` 一路往上調；`verify_arm_deploy_` 的預期角度也建立在同一條錯式子上。

⚠️ **命名為「有效值」是刻意的**：490 也可能是**編碼器角度標度偏 1.53 倍**（那樣 320 沒錯、
錯的是 θ）。今天只量了「θ 與距離的關係」，兩種解釋在資料上等價。**要分辨需獨立驗證角度
（量角器），尚未做。**

### 🎯 per user 問「有沒有可能是馬達出力不夠」—— 不是，而且差很遠

`damiao.h` 的 `limit_param`：DM10010L = `{12.5, 25, 200}` ⇒ **TAU_MAX = 200 Nm**。
✅ 這個值由資料反證：當日所有 `tau` 讀值都量化在 **0.0977 Nm** 的階梯上，
而 MIT 協定把扭力編成 12-bit 跨 ±TAU_MAX ⇒ `400/4096 = 0.0977` **完全吻合**。

而實測峰值約 7 Nm ⇒ **只用到 3.5%**。手臂停在「`kp×err` 平衡（被高估的）重力前饋」的位置，
`hold_kp=34` 時 `kp×err` 只有 2.4 Nm —— **它不是推不動，是沒被要求去推**。
📌 本檔談 kd 上限的註解早就指出這個槓桿：「只能往 kp（範圍 0~500，現在才用 34，空間還很大）去要」。

**`hold_kp` 34 → 60 → 90 實測**（同一 `wall_mm=440`）：

| kp | 壓入量 | 靜置 tau |
|---|---|---|
| 34（含積分） | +8.8 mm | — |
| 60 | +40.1 mm | — |
| 90 | +46.2 mm | 3.86 Nm |

⚠️ 2026-08-14 曾以 `40.0/6.0` 發生失控震盪，敢調上來的依據是**當時的 kd=6.0 因編碼溢位
實際只有 1.00 Nm**（08-17 才修），現在 kd=5.0 是真值 ⇒ 阻尼是當時的 5 倍。實測靜置無震盪。

### 🔴 `m1_.hold_ki` 當日加、當日移除

加它讓殘差由 0.093 降到 0.069（有效）。**但頂住玻璃時位置誤差永遠不會消失，積分持續累積**
—— 實測貼合後 10 秒 tau 由 +0.24 爬到 +1.51 且未停。
⇒ **貼合狀態不是穩定狀態，校 `wall_mm` 等於對著移動目標校。** 已移除。
🔧 要重新啟用必須先做 **anti-windup on contact**。M2 的 `hold_ki=0.6` 不受影響（水平軸，不會頂牆累積）。

### ✅ 貼合定案：`ARM_WALL_MM_DEFAULT = 530`、`hold_kp = 90`

站位：**四顆推桿 `extend_raw 10cm` + 吸盤吸附平整玻璃**（可重現的基準）。

| `wall_mm` | 壓入量 | 靜置 tau | per user 目視 |
|---|---|---|---|
| 440 | +46.2 mm | 3.86 | 不夠 |
| 470 | +52.0 mm | 8.55 | 有比較貼合 |
| 510 | +61.2 mm | 14.99 | 更貼合 |
| **530** | **+63.0 mm** | **19.0** | **完全貼合** ✅ |

🔴 **530 之後進入剛度牆**：每 +1mm 壓入的代價由 0.7 Nm 跳到 **2.2 Nm**，再往上換到的是「力」不是「貼合」。
📌 **吸盤在 19 Nm 反作用力下毫無衰退**（四顆全程 −66~−68 kPa），不是限制因素。

### 🎯 關鍵物理：工具傾角與 M1 角度是耦合的

per user 現場觀察：**滾筒上端先貼、下端有縫，再推進就會轉正貼合。**
因為工具裝在旋轉手臂末端，**M1 的角度同時決定滾筒相對玻璃的傾角**。

⇒ **這解釋了為什麼壓入量（63mm）遠大於工具行程（手壓量到 first-touch→flat 僅 18.9mm）**，
也解釋了**為什麼「手壓貼平」不能當工作基準** —— 手推不到工作壓力，量到的是輕觸姿態。
**我整條校正鏈一度建立在這個太淺的目標上。**

### ⚠️ 自記：這一輪的判斷錯誤

1. **拿被積分污染的讀值去質疑 per user 的觀察**（詳見續五）
2. **「上排吸盤沒吸到」** —— 其實只是比下排慢 8~10 秒，我讀一次就下結論。
   判準應是「讀到穩定為止」，之後的量測都改成連續三次相同才判定
3. **保護判準連續三次設計錯誤**：`tau` 翻正（只在平衡態成立）／速度掉下來（起步就誤觸發）／
   加了啟動保護仍誤判（0.4145 其實無障礙，斷電後手臂自己走到 0.5018 證明）
4. **把 per user 確認的真實接觸重新解釋成「卡住」** —— 用數據推翻現場觀察
5. **`|vel|` 2.2283 的歸因**：分段統計後確認在 `arm_deploy` 的**收回段**（`go_home`，
   今天查到沒有速度安全閥的那條），**不是往玻璃推的那一段** —— 方向上遠不如原先擔心的危險

📌 通則（今天反覆出現）：**「頂到」與「卡住」在 `pos`/`vel`/`tau` 上完全一樣。**
現場有人看得見時，不要用數據去推翻觀察；要嘛設計一個能分辨的量測，要嘛直接問。

### 待完成

- 🔴 **重力前饋在工作區高估約 25%** —— `wall_mm=530` 與 `kp=90` 都是**補償值**。
  修好模型後兩者都要重設。真實幾何牆距（推桿10cm站位）約 **325**
- 🔴 **DEPLOY 仍永遠回 `ERR touch_wall did not converge`**（命令角遠在玻璃後方）
  ⇒ **`verify_arm_deploy_` 的障礙偵測仍然失效** —— 它每次都停得比預期早
- 🔴 **`go_home_slot` 沒有速度安全閥**（今日第三次量到，長行程收回時 |vel| 達 2.2 rad/s）
- 🟡 `m1_.hold_ki` 需 anti-windup on contact 才能重新啟用
- 🟡 **獨立驗證編碼器角度標度**（量角器），以分辨「臂長 490」vs「角度標度偏 1.53 倍」
- 🟡 四顆推桿接觸力道不均（10cm 時 peakI 589~1264 mA，左下最重、右下最輕）＝機身與玻璃不平行

---

## 2026-09-02（續五）— 手量牆距 **285.3 / 298.8 mm**，而程式設的是 **400**；並查明 DEPLOY 落點短少的成因

### 🎯 per user 提議的量法解掉了整個僵局

先前所有牆距估計都是**從「手臂頂住玻璃」的狀態反推**，必然混著結構潰變與控制器出力。
per user：**「你可以 disable 馬達，我手動靠上去，你量測位置」** —— 純幾何、零控制器介入。

⚠️ 執行時必須用今天稍早查到的方法：**disable 之後 `Get_Position()` 回傳凍結快取**，
每次讀取前要送 `M1 MIT 0 0 0 0 0` 觸發 CAN 交換刷新（`main_api.h` 對 M2 記載過同一手法）。

**結果（兩輪各 5~8 次讀值，離散度 0.0004 rad ≈ 0.02°，`tau≈0` ＝ 無出力）：**

| 狀態（per user 區分） | θ | 模型換算樞軸→尖端 |
|---|---|---|
| **剛碰到** | 0.5018 | **285.3 mm** ← 真實幾何牆距 |
| **完整靠上** | 0.5442 | **298.8 mm** ← 工作設定點 |
| 差值 | 0.0424 rad | **13.5 mm ＝ 工具壓縮行程** |
| （對照）DEPLOY 400 頂住 | 0.5755 | 308.6 mm ← **比完整貼合再過壓 10mm** |

🔴 **`ARM_WALL_MM_DEFAULT = 400` 比完整貼合遠 101mm、比首次接觸遠 115mm。**

### 🔴 這解釋了那段常數沿革，也指出它調錯了旋鈕

`330 → 360 → 380 → 400`（2026-07-27 / 08-28 兩次 per user），每次都是為了「貼得更好」。
但真正需要的幾何值是 **299**，低於其中任何一個。

**為什麼會需要調那麼高：因為下垂。** 手臂落點比命令角度短約 0.07 rad（見下），
要讓它真的走到 0.5442，命令得下到 ≈0.614 ⇒ `wall_mm ≈ 320`。400 則過頭。

⇒ **現行作法是拿幾何參數去補償控制誤差**：`wall_mm` 名義上是「牆有多遠」，
實際被當成「要多用力」。**這就是它一路被調高卻始終對不準的原因——調的是錯的旋鈕。**

### 下垂的成因：重力模型在工作區高估

雙向慢掃（向外／向內各一趟，摩擦相消：`G = −(T_out+T_in)/2`）：

| θ | G 實測 | G 模型 | 誤差 |
|---|---|---|---|
| 0.43 | 4.11 | 5.26 | −1.14 |
| 0.44 | 4.98 | 5.46 | −0.48 |
| 0.45 | 4.27 | 5.66 | −1.39 |
| 0.46 | 4.51 | 5.86 | −1.35 |
| 0.47 | 4.21 | 6.06 | −1.85 |

**乾淨區間平均高估 ≈ 1.24 Nm（約 25%）**（0.48/0.49 已剔除——外掃 `T_out` 在那裡從 −2.59
竄到 −0.96，是牆面反作用力介入）。
✅ **方法自我驗證**：附帶算出的摩擦 0.26~1.40 Nm，與標頭記載的「pos≈0.52 → ~1.0 Nm」吻合。

🔴 **但擬不出新模型**：乾淨跨距只有 0.05 rad、散佈 ±0.4 Nm，硬擬 `K`/`PHASE` 兩個參數
會重演「窄範圍擬合→外推壞掉」的原罪。而且 `G_meas` 在 0.43~0.47 幾乎是平的、模型是單調上升的
—— **形狀就對不上，不只是差一個係數**。要重擬需掃到 0.75~0.85（與舊擬合區 0.65~0.83 重疊），
而手臂前方只有 285mm、掃到 0.85 需要約 400mm。

### ⚠️ 自記：今天在同一件事上錯了四次

**「頂到東西」與「摩擦卡住」在 `pos`/`vel`/`tau` 上長得完全一樣**，我反覆想從數據分辨，四次都錯：

1. 保護判準用「`tau` 翻正 ＝ 接觸」→ 誤停。**那只在平衡狀態成立**；ramp 對著卡住的手臂
   繼續前進時 `tau` 本來就會翻號
2. 據此把 per user 確認的**真實接觸**重新解釋成「卡住」→ **用數據推翻了現場觀察**
3. 改用「速度掉下來 ＝ 接觸」→ **在起步瞬間誤觸發**（判準沒考慮「還沒進入被判定的狀態」）
4. 加了啟動保護後仍在 0.4145 誤判接觸 —— 而**斷電後手臂自己落到 0.5018**，證明那裡沒有障礙

📌 **決定性的一課**：`disable` 之後手臂自己落到的位置（0.5018）與 per user 手動靠上的位置
**完全相同** ⇒ 它當時就是落在玻璃上。**這也讓我先前「無動力靜止 ⇒ 重力矩 ≤ 摩擦」的推論作廢**
（它是被玻璃擋住，不是重力小）。
🔧 **通則：現場有人看得見時，不要用數據去推翻觀察；要嘛設計一個能分辨的量測，要嘛直接問。**

### 🔴 還沒驗證的前提

上述所有 mm 數字都由幾何模型換算：
`reach = PASSIVE_EXT(86.46) + TOOL_EXT_CENTER(160.00) + ARM_LENGTH(320) × sin(θ − VERTICAL_OFFSET(0.38))`
**這四個常數今天都沒有被實體量測驗證過。** 待辦：手臂完整靠上時用尺量樞軸→玻璃，
與模型的 298.8mm 對照；不吻合則整條 `theta_target` 換算都要重算。

### ✅ 定案：`ARM_WALL_MM_DEFAULT` 400 → 380（原始碼已改，🔴 尚未建置）

per user 逐步壓貼、我讀角度，量到第三個也是最關鍵的狀態：

| 狀態 | θ | mm | 判定者 |
|---|---|---|---|
| 剛碰到（幾何牆距） | 0.5018 | 285.3 | 量測 |
| 手壓（**尚未貼平**） | 0.5490 | 300.3 | — |
| **真正貼平（工作點）** | **0.5613** | **304.2** | **per user 目視確認** |
| 工具壓縮行程 | 0.0595 | **18.9** | |

（8 次讀值離散 **0.0000**、`tau≈0`；`M1 DISABLE` + 每讀前 `MIT 0 0 0 0 0` 刷新快取。）

**實測 `arm_deploy 376` 落在 θ=0.5598 ＝ 距貼平僅 −1.2mm**，依下垂斜率反推
「剛好貼平」對應 **wall_mm ≈ 378** ⇒ 取 **380**。

🔴 **而歷史值是 330 → 360 → 380 → 400** ⇒ **380 本來就是對的，錯的是 2026-08-28 那步 380→400。**
先前那些人靠經驗把 `wall_mm` 調到 380，實質是在補償下垂，而且已經補到位了。

⚠️ **380 不是幾何牆距（285），是「補償下垂後剛好貼平」的值** —— 只在此牆距與負載下成立。

### ⚠️ 自記：我拿被污染的讀值去質疑 per user 的觀察

per user 說「壓少了」時，我量到馬達在 0.5663 ＝ **比手壓位置更外**，於是回覆「數字上是壓更深」。
**錯在我用的是一分鐘後的讀值** —— 那時我加的積分項已經把手臂從 0.5598 推到 0.5663。
按時間軸還原：

| 時刻 | θ | 距貼平 |
|---|---|---|
| DEPLOY 376 剛完成 | **0.5598** | **−1.2 mm** ← per user 看到的就是這個，「壓少了」**完全正確** |
| 一分鐘後（積分推的） | 0.5663 | +1.6 mm ← 我拿來比對的值 |

📌 **教訓有兩層**：① 比對現場觀察時，要用**觀察當下**的讀值，不是事後的；
② **我加的 `hold_ki` 本身就是污染源** —— 頂住時位置誤差不會消失，積分持續累積、越推越深
（實測 +1.2mm → +1.6mm，會一路爬到 `0.6×2.0=1.2 Nm` 上限）。
🔴 **`m1_.hold_ki` 必須配「接觸/飽和時抑制累積」（anti-windup on contact）才能留**，
否則它會在每次頂住玻璃時持續加壓。目前**尚未處理**。

### 待完成
- 🔴 **`ARM_WALL_MM_DEFAULT=380` 尚未建置部署** —— 重建本體要重啟，而 `cmd_shutdown`
  會關真空閥與幫浦、吸盤鬆開、牆距改變。**待可鬆開吸盤時再做。**
  📌 期間可用 `arm_deploy 380 CENTER`（牆距是指令參數，不吃該常數）
- 🔴 **`m1_.hold_ki` 需加 anti-windup on contact，否則頂住時會持續加壓**
- 🔴 **用尺驗證幾何模型**（上述）
- 🔴 **`wall_mm` 與下垂要一起修**：只改 `wall_mm=299` 而不修模型 → 手臂因下垂碰不到玻璃；
  只修模型不改 `wall_mm` → 準確地伸到玻璃後方 101mm
- 🔴 重擬重力模型（需 0.75~0.85 的掃描空間，即樞軸前方約 400mm 淨空）

---

## 2026-09-02（續四）— 查明 M2 短距離過衝的機制：**passive 探針把馬達踹過頭**，修法早已存在於同檔案

### 起點：per user「main 上的功能是可以用的」

依此比對 `cleaning_arm/` 與 `main`，結果**與預期相反**：

- 兩分支只差 **一個常數**（`MAX_LOOPS` 100→150），M1 passive、`go_home` 無速度安全閥、
  PARK 過衝這些**逐字相同**
- 🔴 **而且 Pi 上跑的舊 binary 就是 `e3c8820` ＝ main 的 HEAD**
  ⇒ **今天的失敗全部發生在「main 那一版」上，不是分支引入的迴歸**

⚠️ **自記：我一度用檔案 mtime（Aug 27）推斷 Pi 的程式碼版本並宣稱它缺 PARK 修正 —— 錯的。**
`rsync -a` 保留 mtime，日期只反映來源檔的時間戳。**正確作法是比對內容雜湊**
（用 `git show <commit>:<path> | md5sum` 逐一比對才定位到 `e3c8820`）。

### 🎯 機制：探針的方向與過衝方向完全一致

`lr_move_to_slot_impl()` 在軌跡開始前有一段 passive 偵測，把位置命令設在
**`cur_cmd ± 1.0 rad`**、以 `MIT_KP=31` 驅動 60ms（≈31 Nm 命令扭力，實際飽和）。
註解自稱 "light frames"，**但它不輕**；方向恆為「越過目標」那一側。

| start | target | 探針方向 | 落點 | |
|---|---|---|---|---|
| +0.0090 | 0 | **負** | **−0.1444** | 🔴 |
| −0.0013 | 0 | **正** | **+0.1421** | 🔴 |
| −0.1692 | 0 | 正（朝目標） | −0.0261 | ✅ |
| +0.1329 | 0 | 負（朝目標） | +0.0090 | ✅ |

**長距離**探針正好朝目標、60ms 被多秒軌跡吸收；**短距離**目標就在腳邊，
探針把馬達踹到另一側，而 creep 只有 0.20 rad/s（每步 0.004 rad）拉不回來。
且 `cur_cmd` **只在 passive 分支刷新** ⇒ 正常情況下 ramp 從探針前的舊起點開始。

🔴 **而且它回報成功**：0.142/0.144 都剛好卡在 `CONV_TOL=0.15` 內 ⇒ 印 `(converged)`。
**DEPLOY 之後 M2 停在 −0.119、`tau≈2.0` 由 hold 硬拉，根因就是這個。**

### ✅ 修法不是新設計 —— `go_home_slot` 早在 2026-08-14 就做了同樣兩道

同一個檔案裡的同名探針，該處註解逐字寫著「縮小成 0.05 rad…移動量小很多」與
「the probe … may have moved the motor; starting the ramp from a stale reference
**is exactly what caused the erratic-move bug**」。
**`lr_move_to_slot_impl` 停留在 08-14 之前的舊形式，修正沒有擴散到第二個呼叫點。**

已補上：探針偏移 `±1.0 → ±0.05`、探針後**無條件**刷新 `cur_cmd`。
（`MIT_KP=31 × 0.05 = 1.55 Nm` 仍遠高於 `TAU_LIVE_THRESHOLD=0.3`，判定能力不受影響。）

### ✅ 實機驗證

部署後以 `arm_init` 觸發 0.035 rad 的短距離移動（正是發作區間）：

```
[M2 lr_move_to_slot] Done.  pos=0.0338  target=0.0000  start=0.0338 (converged)
```

**過衝 0**（修正前同類移動是 0.14），且 `start` 欄與實際位置相符、不再是舊值。
⚠️ 殘留（非迴歸）：停在 0.0338 而非 0——`err < CONV_TOL` 時摩擦前饋不介入，
餘量由 hold 積分慢慢收。

### 📌 今天第三次「同一個修正沒有擴散」

1. `QX_DO24` 是七支驅動裡**唯一**有分片防護的（擋住擴散的是一句錯誤註解）
2. `cmd_relay` 有回讀、**`cmd_pump` 沒有**
3. `go_home_slot` 有探針兩道緩解、**`lr_move_to_slot_impl` 沒有**

⇒ **這個專案的典型缺陷不是「沒人想到」，是「想到了、修了一處、沒有掃第二個呼叫點」。**
🔧 **可行的對策**：修完一個缺陷後，用該缺陷的**特徵**（而非函式名）grep 全檔，
例如這次可用 `probe_setpt` / `TAU_LIVE_THRESHOLD` 一次找出所有同型探針。

---

## 2026-09-02（續三）— 手臂首次接回：INIT/PARK 通過，DEPLOY 失敗但**量到了長期查不出的不穩定平衡點**

### 前置：吸附 + 起 motor_api

per user 手動移到可觀測高度（`L=235`）後：幫浦 ON → 真空閥 ON → `pusher all extend_raw`
→ **四顆吸盤全部吸住並穩定在 −66 ~ −68 kPa**（判準 `VACUUM_THRESHOLD_KPA=-40`），
上升曲線 −2 → −30 → −58 → −66 乾淨、沒有一顆落後。
📌 張力吸附前後幾乎沒變（88.4 → 87.4 kg）⇒ **吸盤是把機器拉貼牆面，重量仍全由鋼索承擔**。

**`motor_api` 啟動是安全的**（讀碼確認）：`init()` 只做「開序列埠 → 建 Motor_Control →
設定 M1/M2 邊界與增益 → bind/listen」，**沒有任何 ENABLE 或運動**；主執行緒只 sleep。
🔴 **`runbook.md` §A 完全沒提到 `motor_api` / 9527 / 手臂啟動**（只寫吊機、本體、Web GUI）——文件缺口。

### ✅ 新事實：damiao 馬達的零點撐得過**整台斷電**

`main_api.h` 的註解只敢說：

> ASSUMES the motor's own zero reference (set via `M2 ZERO`) **survives a motor_api restart**

而今天 13:03 是**整台斷電**（馬達也斷）。起 motor_api 後讀到 M1 `pos=0.0093`（≈機械停點）、
M2 `pos=-0.1692`（行程 ±0.73 內），**經 per user 目視確認 M1 確實頂在停點、M2 確實在中間附近**
⇒ **零點撐過了馬達斷電**，假設可以延伸。
📌 這個確認很重要：兩個讀值都落在「看起來合理」的範圍，**光看數字分不出真假**，
而待辦表記著「重裝後若位置落在 ±1.5 rad 內，INIT 會**靜默**移到錯的 CENTER」。

### ✅ INIT ×2、PARK ×1 全部通過

| | 結果 |
|---|---|
| `arm_init` (1) | OK 2.9s。M1 `CALIBRATE` 推到停點重設零點 → `-0.0002`；M2 `-0.1692` → CENTER |
| `arm_park` | OK 1.7s。**M1 確認在原點才 disable**（符合 08-18「只有真的到家才放手」的守衛） |
| `arm_init` (2) | OK。M2 `0.1329` → `0.0090` |

🎯 **`kd` 協定上限修正（08-17/08-18）的第一筆正面證據**：INIT 時 M2 從 −0.169 回中心，
**過衝約 0.05 rad（31%）**。對照待辦表記載的舊症狀「**衝過 target 兩倍距離**」（會是 0.34 rad）
⇒ 阻尼確實生效了。（M1 的 5.0 要長行程才驗得到，見下。）

### 🔴 `arm_deploy 400 CENTER` 失敗 —— 但這一趟的數據價值很高

`ERR DEPLOY: M1 touch_wall did not converge`（8.2s）。手臂 log：

```
[M1 touch_wall_slot] theta_target=0.8804
[M1 SAFETY] vel=0.445667 rad/s exceeds 0.4 limit — emergency brake (kd=5, tau_ff=-1.46123) engaged, pos=0.245481
[M1 MOVE] passive suspected (err=0.153/0.182/0.221 rad, tau<0.3 Nm), re-enabling   ×3
[DEPLOY] M1 touch_wall FAIL — pos=0.600251 target=0.880441 err=0.28019
```

**兩個獨立的問題疊在一起：**

**① 🎯 量到了不穩定平衡點 —— 這解決了一條長期分歧。**
`tau` 在 `pos ≈ 0.21~0.25` 之間**翻號**（+3.4 → +0.83 → −2.0 → −4.2 Nm），
而**緊急煞車正好在 `pos=0.2455` 觸發**，物理上完全吻合（過了平衡點重力由阻力變助力）。
程式碼（08-17）寫著這件事查不出來：「幾何估計 **~0.75** vs 擬合換算 **~0.18**，兩者對不上」
⇒ **今天的實測站在 ~0.18 那一邊（0.21~0.25）。**

**② passive 才是失敗主因，不是撞到東西。**
`pos=0.60` 之後三次 passive：**`tau` 低於 0.3 Nm 而位置誤差持續擴大** ⇒ 馬達不出力。
撞到障礙物會是 `tau` 很高。這是 08-17 就記載並為此做了看門狗的既有問題
（"Video evidence showed M1 going fully passive mid-HOLD"），看門狗重新使能三次都沒拉回來。

📌 **兩者的因果**：DEPLOY **順著重力**伸出（程式碼明寫「一旦失控會被重力持續加速，
正是當年暴衝的那條路徑；PARK 逆重力則不會」）⇒ passive 一發生就被重力加速 ⇒ 撞上安全閥。
**所以速度尖峰不是「控制器調太快」，是 passive + 重力的結果；降速治不了 passive。**

✅ **安全閥與失敗路徑都正確動作**：`M1_VEL_SAFETY_LIMIT=0.4` 攔下了 0.4457；
失敗後 **M1 保持 enabled 並持在 0.6003**（`hold=1`、`tau=0.83`），沒有交給重力。

### ⚠️ 自記：外部輪詢看不到暫態

我以 4Hz 從外部輪詢 STATUS，測到的 `|vel|` 峰值是 **0.3846**，而韌體 log 記的是 **0.4457**
—— **我漏掉了真正的峰值，還據此說「只差 4% 沒觸發安全閥」，事實上它已經觸發了。**
📌 **暫態要看韌體自己的 log，不要用外部取樣去證明「沒有超過」。**

### 🔴 新發現的兩個缺陷

1. **`STATUS` 對 disabled 的馬達回傳凍結的舊幀，且無任何提示。**
   PARK 之後連讀 8 次、12 秒，`pos/vel/tau` **逐字完全相同**（含 `vel=-0.2711`、`tau=-2.4957`）。
   我一度判成「M2 放開後還在動」——**那是 PARK 過程中最後一次 CAN 交換的快照**。
   `Get_Position()` 只在有幀交換時更新，disable 後沒有幀。
   刷新後真值是 `M2 pos=0.1329 vel≈0 tau≈0`（靜止）。
   📌 刷新方法（`MIT 0 0 0 0 0`）只寫在 `main_api.h` 一段**關於手量流程**的註解裡，
   `STATUS` 本身完全沒有反映。🔧 修法：`cmd_status_sequence()` 對 disabled slot 先送零扭力 MIT
   再讀，或至少輸出標記 `stale`。**與 `cmd_pump` 謊報成功、JC100 例外回覆被當有效值同一類。**
2. **`cmd_arm_status` 回傳的不是 `OK`/`ERR` 開頭**，而是手臂的原始行 `[M1] pos=... | [M2] ...`
   （`WASH_ROBOT.cpp:921` = `arm_cmd_("STATUS",3) + "\n"`）。
   本體所有其他指令都回 OK/ERR ⇒ **任何「讀到 OK/ERR 為止」的客戶端都會卡到逾時**
   （`cycle_test.py` 的 `ask()` 正是這樣寫）。
   ✅ 已確認 **GUI 沒有用到 `arm_status`**（`web_backend/` 遞迴搜尋零命中）⇒ 改格式是安全的。
   ⏸ 未改：當下機器正吸附在牆上，重啟本體會關真空閥、鬆開吸盤，不值得為格式問題打斷測試。

### ✅ `arm_park` 從 0.6 rad 收回成功 —— 但暴露兩件事

`arm_park` OK（3.8s），M1 由 0.6003 收回、disable，最終靜止於 **0.0166**（刷新後即時值）。

**① 🔴 `go_home` 這條路徑沒有速度安全閥 —— 已知，但今天量到的超標大一個數量級。**

韌體 log 第一筆：`[M1 go_home] cmd=0.6024 pos=0.5644 **vel=-1.5690** tau=-1.7094 tau_ff=-7.9150`
⇒ **是 `M1_VEL_SAFETY_LIMIT=0.4` 的 3.9 倍，全程沒有任何 `[M1 SAFETY]` 觸發。**
對照同日 DEPLOY：`vel=0.4457` 一超標就立刻煞車。

原因**已記載於 2026-08-18 的註解**：「實際速度衝到 −0.4335（超過 `M1_VEL_SAFETY_LIMIT=0.4`
的設計值，**而 `go_home_slot` 這條路徑沒有速度安全閥**）」。安全閥實作在
`feedback_loop()`（`main_api.cpp:1915`）服務 HOLD/MOVE 分支，而 `go_home_slot()` 自己跑 ramp
直接驅動馬達、**繞過該檢查**。
🔴 **但 08-18 記的是超出 8%（0.4335），今天是超出 292%（1.5690）** ——
今天是從 `pos=0.6003`（完全伸出、重力力矩最大、`tau_ff=-7.915`）起步的**第一個取樣**，
而 08-18 那筆在末段 creep 區（0.0414）。**行程的這一段從來沒有被量過。**

**② 🔴 PARK 落點比觸發 08-18 回退的那次更差。**

```
ramp ARRIVED after 107/900 loops (2140ms) pos=0.0170 target=0.0500
settle DONE pos=0.0181     → 最終靜止 0.0166
```

`PARK_STOP_MARGIN=0.05` 的用意是**在主動控制下永遠不接觸機械硬停點**。
08-18 把 `CREEP_SPEED` 由 0.12 退回 0.10，理由正是「0.12 實測開始過衝：ramp ARRIVED 落在
**pos=0.0414**…距硬停點只剩 0.041 rad，安全邊際太薄」。
**今天用的就是回退後的 0.10，落點卻是 0.0170 —— 比那次還低一半。**
⇒ **回退沒有解決問題，只是當時沒有從 0.6 rad 這麼遠的地方 PARK 過。**
起點越遠 → 進入 creep 區的動量越大 → 落點越低。

### 現況（收筆時）

機器 `L=235` 懸掛、四顆吸盤 −66~−68 kPa 吸附中、幫浦與閥 ON、
**手臂已 `arm_park` 收回並斷電**：M1 `pos=0.0166`、M2 `pos=0.0349`，兩顆 `hold=0`、`tau≈0`。
⚠️ 幫浦與真空閥仍 ON、吸盤仍吸附中。

### 待完成

- 🔴🔴 **`go_home_slot` 沒有速度安全閥**：今日實測 `vel=-1.5690`＝限制值的 **3.9 倍**且無煞車。
  08-18 已記載此缺口但當時只量到 0.4335（超出 8%）⇒ **嚴重度被低估了一個數量級**。
  🔧 把 `feedback_loop()` 的速度檢查搬成共用、讓 `go_home_slot` 的 ramp 也走一遍
- 🔴 **PARK 落點失控於長行程**：`PARK_STOP_MARGIN=0.05`，今日從 0.6 rad 收回實際落在 **0.0170**
  （比觸發 08-18 `CREEP_SPEED` 回退的 0.0414 還低一半，而現在用的就是回退後的值）
  ⇒ **回退只是遮住了症狀**，真正的變數是「起點距離 → 進 creep 區的動量」。**起點越遠落點越低。**
- 🔴 **M1 passive 的根因**（08-17 起未解，看門狗只能重試不能治本）
- 🔴 `arm_deploy` 未成功過；`theta_target=0.8804`(wall 400/CENTER) vs 實際只到 0.60
- 🟡 `cmd_status_sequence` 對 disabled slot 補刷新或標記 stale
- 🟡 `cmd_arm_status` 回覆格式對齊 OK/ERR
- 🟡 `runbook.md` 補手臂啟動（`motor_api` + 9527）
- 🟡 上滑台：**無指令可讀位置或設零點**（`dm2j_zero` 已 `removed_in_v2`，驅動層有
  `read_position_cm` 但分派器沒暴露）⇒ 設零點的唯一路徑是跑完整 `cmd_init`，
  而它會關真空閥。**測滑台前要先解決這個。**

---

## 2026-09-02（續二）— **有真空的 10 週期跑完，是四輪裡最好的**；新診斷證偽分片理論；右腳不對稱另有解釋

### `cycle10_0902d` 10/10 完成 —— 四輪對照

| | 段數 | 中位 | p90 | 最大 | ≥6cm | 結果 |
|---|---|---|---|---|---|---|
| 09-01 `10c` | 36 | 2 | 5 | 8 | 8% | 🔴 掛週期 7 |
| 09-01 `10d` | 35 | 3 | **7** | **9** | 14% | 🔴 掛週期 6 |
| 09-02 早（**無真空**） | 60 | 3 | 5 | 6 | 3% | ✅ 10/10 |
| **09-02 晚（有真空）** | **60** | 3 | **4** | **5** | **0%** | ✅ **10/10** |

**60 段沒有任何一段超過 5cm**，離腳本門檻 8 與韌體硬底線 10 都很遠；`出帶%` 多數 0~7%
（早上無真空那輪是 16~35%）。⇒ **09-01 那個「越跑越差」在兩種條件下都沒有重現。**

### 🔴🔴 新診斷立刻證偽了分片理論

本輪 `.22` 錯誤約 197 筆（`JC100:7 ×91`、`:8 ×77`、`QX:9 ×20`），與早上同量級
（風扇每步開關，符合干擾預測）。**135 筆非 timeout 的分類**：

```
CRC 22 / ADDR_MISMATCH 12 / FUNC_MISMATCH 10 / MODBUS_EXCEPTION 1 / SHORT_FRAME 0
```

**而附上的位元組讓病因無所遁形**（新訊息不必開 driver debug）：

| 訊息 | 期望 | 實收 | 差異 |
|---|---|---|---|
| `ADDR_MISMATCH (40/8) head=28 03 02 00` | `0x08` | `0x28` | **bit 5** |
| `ADDR_MISMATCH (14/6) head=0E 03 02 00` | `0x06` | `0x0E` | **bit 3** |
| `FUNC_MISMATCH (19/3) head=08 13 02 00` | `0x03` | `0x13` | **bit 4** |
| `FUNC_MISMATCH (11/3) head=08 0B B5 50` | `0x03` | `0x0B` | **bit 3** |

**不是「別的裝置的回覆」，是正確的位址／功能碼被打壞了幾個位元**（後面的 `03 02 00` 仍在正確位置）。

🔴 **兩個結論**：
1. **`SHORT_FRAME` 掛零 ⇒ 分片理論證偽。** `_pt` 那條路是錯的（幸好已改回 0）。
   待辦表「七支驅動只有一支有分片防護」那列的**優先度應下調**——結構性暴露仍在，但
   **本專案量到的實際故障不是分片**。
2. **`MODBUS_EXCEPTION ×1` ⇒ 當天修的既有缺陷，當天就抓到一次真實發生。**
   舊碼會把它當有效讀數（5 bytes + CRC 正確，同時通過兩道檢查），
   然後把 **CRC 的兩個位元組**當壓力值往上傳。

⇒ 病因是**線上的位元級損毀（電氣）**，配上早上量到的「風扇開啟時錯誤率 15×」，整條線索一致。
**位元損毀同時符合 EMI 耦合與電源下陷** ⇒ 量 [B] 電壓仍是分辨兩者的關鍵。

📌 QX 的 FC `0x06` 退路**本輪 0 次觸發**（那 20 筆靠既有三次重試救回）。**仍未被驗證。**

### 🔴 右腳阻力：修理後仍在，但原因可能不是「右邊比較重」

本輪 ZDT 伸出（每顆 50 次）：

| 推桿 | 位置 | 時間中位 | peakI 中位 | 中途輪詢 |
|---|---|---|---|---|
| ZDT:5 | 右上 | **2025ms** | **1039mA** | 33 |
| ZDT:6 | 右下 | **2400ms** | **1089mA** | 46 |
| ZDT:7 | 左上 | 1650ms | 874mA | 16 |
| ZDT:8 | 左下 | 1500ms | 882mA | 4 |

右腳仍是左腳的 **1.4~1.6 倍**時間、高約 20% 電流（早上：5=2400/6=2400/7=1650/8=1575，
ZDT:5 略改善但不對稱還在）。

⏸ **per user 推測「右邊重心較重所以電流高」——張力資料指向相反方向**：
整天量到的都是**左 52~62kg / 右 37~43kg（比值 1.4×，左邊重）**，與 09-01 續十二的
「重心偏左」一致。且**推桿水平推向玻璃、鋼索垂直承重，不同軸**。

🆕 **改用吸附率交叉比對，指向幾何而非重量**（本輪 50 步，門檻「該側任一顆 ≤ −10 kPa」）：

```
兩側都吸到 19 (38%) ／ 只有右腳 11 (22%) ／ 只有左腳 2 (4%) ／ 都沒吸到 18 (36%)
```

**只有單側吸到時，右腳次數是左腳的 5.5 倍。**
⇒ **假說：右側離牆較近。** 右推桿在 10cm 行程中較早接觸玻璃 → 接觸後仍要推到指令位置
→ **時間長、電流高**；同時右吸盤較容易吸到。**一個原因解釋兩組獨立量測。**
⚠️ 競爭解釋「左吸盤漏氣」解釋不了電流那一半。
🔧 **分辨方法：懸掛時用尺量四個吸盤位置到牆面的間距。** 右側明顯較近即確認。

---

## 2026-09-02（續）— 有真空的兩輪都中止；**查出風扇會干擾 `.22` 匯流排**；QX-DO24 全滅後自癒

### 三輪測試一覽（本日）

| log | 條件 | 結果 |
|---|---|---|
| `cycle10_0902` 09:41 | 🔴 **無真空**（幫浦沒開，見上一節） | ✅ 10/10 完成，60 段：中位 3 / p90 5 / 最大 6 |
| `cycle10_0902b` 12:41 | ✅ 有真空 | 🔴 週期 4 中止 —— **使用者斷電**（機構問題） |
| `cycle10_0902c` 12:44 | ✅ 有真空 | 🔴 週期 2 中止 —— **PWM 模組無回應** |

**`0902b` 的 21 段：中位 2 / p90 4 / 最大 7。與無真空那輪（中位 3 / p90 5）比較沒有變差**
⇒ 早上寫下的「吸附/脫離是嫌疑最大的變因」**不成立**。樣本只有 21 段，但它已經跑進
09-01 兩輪開始惡化的區間（週期 4）卻沒惡化。

📌 **`0902b` 的中止原因我一度講反了。** 腳本收到的是 `extend_raw TIMEOUT`，我先判成
「機構卡住 → 腳本先中止 → 才斷電」。實際相反：**突然斷電的對端不送 RST，client 只會看到沉默直到逾時**
—— TIMEOUT 正是「對端憑空消失」的簽名。收尾那行 `EXC:[Errno 111] Connection refused / OK`
（本體已死、吊機仍在）也印證是先斷電。

### 🔴🔴 本日最重要的發現：風扇會干擾 `.22` 匯流排

`0902c` 中止於 `風扇開啟失敗：ERR pwm_freq_write_failed_no_reply_timeout`。查 log 的**順序**：

```
83: [pwm_set] duty=7            ← 風扇開
85: [pwm_set] OK — 三個寫入都成功
86: 12:44:29 [ERR] JC100:7 TIMEOUT ×3 → fast-fail
87: 12:44:31 [ERR] JC100:8 TIMEOUT ×3 → fast-fail
89: 12:44:32 [ERR] QX:9 no reply
```

**風扇開啟後 3 秒內兩顆 JC100 同時進 fast-fail。** 以整份 log 統計（依 `duty` 值追蹤風扇狀態）：

| | 風扇**開** | 風扇**關** | 倍率 |
|---|---|---|---|
| `wr_0902.log`（10 週期） | 462 行 / **246 錯** | 1341 行 / **48 錯** | **≈15×** |
| `wr_0902c.log` | 41 行 / **24 錯** | 130 行 / **16 錯** | ≈5× |

⚠️ **分母是 log 行數不是交易數**（風扇關的期間夾雜大量 `wait_many` 行，那時沒在打 `.22`），
絕對比例會偏誤；但效果量夠大，加上時序證據，不像是分母造成的假象。

🔴 **這推翻了我當天先後寫下的兩個推測**：
- ~~「左腳接頭間歇接觸」~~ —— JC100:7/8 只是最敏感的兩顆，不是壞掉
- ~~「`.22` 匯流排本身有問題」~~ —— 風扇關著時 JC100 連讀 10 次全部一致

**兩個候選機制未分辨**：① EMI 耦合進 RS485；② 電源下陷（`CLAUDE.md` 電源架構：
**[B] EPP-200-24 → 氣動、感測 I/O 與通訊介面**，若風扇同吃這一路，啟動電流會讓軌壓一起塌）。
**分辨方法：量風扇啟動瞬間 [B] 的電壓。** 處置完全不同（分電源 vs 屏蔽/走線分離）。

### `_pt = 5` 實驗：做了，量不到效果，已改回 0

`.22` 網關 `_pt` 由 0 改 5ms（`CLAUDE.md` 那條待辦寫著「在沒有證據指向分片前不動」，
而當時我以為 QX 的錯位就是證據）。結果：

| | QX 寫入 | JC100 |
|---|---|---|
| `_pt = 5` | **0/5** | 5/5 ✅ |
| `_pt = 0` | **0/5** | 5/5 ✅ |

**完全沒有差別**，且 JC100 在改之前就已經乾淨 ⇒ **「5ms 有沒有用」等於沒被真正測到**。已還原為 0。
📌 操作方法已寫進 `CLAUDE.md` 網關表（七欄位要全帶、存完必須 `manage.cgi?reset=1` 重開機才生效、
`rup=1`/`rfp=1` 分別是恢復使用者預設與**恢復出廠**，絕不可誤送）。實測 `pt` 會寫進 flash。

🔴 **順帶查出的結構性事實**：`.20` 與 `.22` **都是 115200 8N1、`_pt` 都是 0**，而
**七支驅動裡只有 `QX_DO24` 用會累積分片的 `sendAndReceiveQuiet`**，
其餘六支（ZDT/PQW/DM2J/JC100/XKC/DY500）全無防護。
⚠️ **`QX_DO24.cpp` 的註解宣稱「JC100/SD76/SE3 都是 9600、本模組是全專案唯一的 115200」——
那半句是錯的，而錯的那半句正是防護沒有擴散出去的原因。** 已更正。

### QX-DO24：寫入 0/22 → 自己好了

12:44 起 `pwm set` 全數失敗。排除過程（每一項都有量測）：

| 假說 | 結果 |
|---|---|
| 網關握著斷電留下的死連線 | ❌ 重啟本體乾淨重連後仍壞 |
| `_pt` 分片 | ❌ 0 與 5 各測 5 次都是 0/5 |
| slave ID 被打回預設 | ❌ 讀取偶爾成功 ⇒ 還在 slave 9 |
| 閂鎖故障、斷電可解 | ❌ 13:03 整台斷電後仍壞 |
| 匯流排本身 | ❌ 同線的 JC100 完美 |
| 電力接線 | ❌ per user 正常 |

**方向性很清楚**：讀（8-byte 短幀）偶爾成功、寫（FC `0x10` 13-byte 長幀）**0/22**
⇒ 失敗在「主站→模組」，Modbus 從站收到壞幀會靜默丟棄，正是 `no reply` 的成因。
一度據此推測「接收端被燒」（手冊保修條款明列「電源錯接到 485 導致 485 段燒毀」，
而端子 `VCC/GND` 就緊鄰 `A/B`）。

🔴 **13:22 換上新 binary 重啟後，突然全好了**：讀 8/8、寫 15/15、錯位消失、log 零錯誤。
**per user：期間沒有動任何實體。** ⇒ 「燒毀」推測**收回**（燒毀不會自癒）。
⚠️ **同樣是重啟程式，13:05 那次沒用、13:22 那次有用** ⇒ **間歇性、原因未明、隨時可能再犯。**

### 本日程式改動（全部已建置部署，🔴 但都未被真實故障驗證）

1. **`JC_100_METER.cpp`** —— 拆開「CRC error」為 `MODBUS_EXCEPTION`/`ADDR_MISMATCH`/
   `FUNC_MISMATCH`/`SHORT_FRAME(len=N,expect=M)`/`CRC`，訊息附前 4 個位元組（不必開 debug）。
   🔴 **順帶修掉既有缺陷**：Modbus 例外回覆是 5 bytes 且 CRC 正確 → 同時通過 `len<5` 與 CRC 兩道
   → `read_pressure()` 把 **CRC 的兩個位元組**當壓力值往上傳。**而壓力值是放腳的判準之一。**
2. **`QX_DO24.cpp`** —— `setPWM_Freq` 加 FC `0x06` 退路（手冊：freq ≤65535 只需寫 `0x05`；
   兩筆 8-byte 取代一筆 13-byte），**走到即 `LOG_WRN`**，不讓它靜默掩蓋硬體故障。
   新增 `restartModule()`（reg `0xFF00`=`0x0001`，8-byte 短幀，長幀不通時可能是唯一遞得進去的命令）
   與指令 `pwm restart`。**值寫死，讓誤送 `0xFFFF`（恢復出廠）在型別上不可能。**
   ⚠️ **退路一次都沒觸發過**（QX 恢復後 FC `0x10` 直接成功）——**不可宣稱它有效**。

### 🔴 新發現的缺陷：`cmd_pump` 會謊報成功

13:30 前置時 `pump on` 回 `OK`，而連讀 4 次 `relay_status` 都是 `ch2=0` ⇒ **那次寫入靜默失敗**。
再送一次就成功（3/3，回讀 `ch2=1`）。`cmd_relay`（續十新增）有回讀、**`cmd_pump` 沒有**。
⇒ 續十那句「指令回 OK 不等於繼電器真的動了」的又一個實例，**`cmd_pump` 該補回讀**。
📌 同一時間 `zdt_release_stall` 也是第一次 `ok=2 fail=2`、重送三次全 `ok=4`
—— `.20` 在程式重啟後的頭幾筆交易有瞬態。

### 文件

- 🆕 **`.claude/summaries/QX_DO24_MODBUS_SUMMARY.md`**（第 9 份），`CLAUDE.md` 索引已加列
- **手冊 PDF 由 `tmp/` 搬到 `doc/PWM模組/`** —— `tmp/` 不在雲端鏡像範圍內
  （08-31 SD76-C 同型事件）。這條規則也補進了 `CLAUDE.md` 的 `summaries/` 說明
- `CLAUDE.md` 網關表：`_pt` 現況、改法、`manage.cgi` 三個參數的語意差異

### ⚠️ 自記：本日第二批方法論錯誤

前一節記過 `pgrep -f` 自我匹配與 `nc -q` 吃掉 SLOW 回覆。今日下半場再添三筆：
- **把「工具沒生效」讀成「設定沒存進去」**：`system.cgi` 存檔後 `system.shtml` 顯示的是
  **執行中**的值，要重開機才會更新。我一度以為 CGI 忽略了寫入
- **`pgrep -f 'facade_cleaning_v2.out'` 又自我匹配一次**（同一個坑當天第三次）→ 改用
  `ps -eo args | awk '/[f]acade.../'` 的括號技巧
- **停程式後只等 7 秒就換檔啟動**（關閉流程有 water_inlet 三次重試約 15s）→ 事後查證只有
  1 個實例、兩網關各 1 連線，沒有造成「一條匯流排兩個主站」，但那是運氣不是設計

---

## 2026-09-02 — 10 週期第一次跑完，但**幫浦沒開**；`cycle_test.py` 補兩道守衛

### 現場條件（開工狀態）

兩台 Pi 09:34 才開機、程式都沒起。`~/bringup/` 的原始碼與工作區**逐位元一致**
（`main.cpp` / `cycle_test.py` md5 相同），`crane_control_PI.out` 建於 09-01 20:38 ＝最新版，不需重建。

續十八那份「明天跑之前的檢查清單」逐項核對：

| # | 項目 | 結果 |
|---|---|---|
| 1 | `balance_source=imu` | ❌→✅ 冷啟動掉回 `meter`，已設；`imu_roll_fresh=1 age=100ms` 確認資料真的在流 |
| 2 | `fine_adjust_level_diff_cm=5` | ❌→✅ 冷啟動掉回 0，已設 |
| 3 | 起點在頂端 | ❌→✅ 計米器**跨重開機保留**（SD76 `resumed`），仍停在昨晚的 `L=74 R=68`；`retract 74` @30Hz 收回，6.1s、左右位移差全程最大 3cm、收在 `L=-1 R=-6`（`L−R=+5`、roll −0.81°） |
| 4 | 門檻 `130 / 75` | ✅ 冷啟動即生效 |
| 5 | log 導到 `cycle_logs/` | ✅ |

📌 **`[crane] 有線 192.168.1.10 探測不通（300ms）→ 走 WiFi 192.168.5.25:5002`**
＝續十四的 `CRANE_IP` 伏筆消除**在現場實際生效了**，第一次看到它自己選路。

### 🔴🔴 這一輪全程沒有真空源 —— 前置清單 5 項全過，但漏了第 6 條

`cycle10_0902` 10 個週期全部跑完之後才發現（使用者問「真空幫浦是不是沒有開啟」）。
`relay_status` 事後直接回讀：**16 通道全部 0，含 `ch2=pumpA`**。

**成因是一條職責分界：開幫浦的是 `init` 這支 TCP 指令**
（`cmd_init` → `controlRelay(CH_PUMP, true)`，印 `[init] PQW relays → pump ON`），
**不是**程式啟動時的驅動 `init()` —— 後者底下那五行 relay 設定是註解掉的
（`app/WASH_ROBOT.cpp:359-363`）。本腳本不送 `init`，而 `state=idle` 在幫浦沒開時照樣成立
⇒ 既有兩道前置檢查（起點在頂端 / 本體 ready-idle）**沒有一道碰得到這件事**。

🔴 **續十已經寫過這條**（「程式重啟後所有繼電器都是 OFF，『泵浦運行期間常開』只在 `init`
跑過之後成立」）。**寫下來的知識沒有變成程式碼裡的檢查，就會在下一次重開機之後原地復發。**
這次還多疊一層：當天 Pi 整台重開過。

⏸ per user「沒關係，跑完看結果」——未中止，改把這輪定位成**無真空對照組**。

### 結果：10/10 完成，而且是三輪裡最好的一輪

| | 段數 | 最小 | 中位 | p90 | 最大 | ≥6cm | 結果 |
|---|---|---|---|---|---|---|---|
| `cycle10c` 09-01 18:29 | 36 | 1 | 2 | 5 | **8** | 8% | 🔴 掛在週期 7 |
| `cycle10d` 09-01 18:50 | 35 | 1 | 3 | **7** | **9** | 14% | 🔴 掛在週期 6 |
| **`cycle10_0902` 09-02 09:41** | **60** | 0 | 3 | **5** | **6** | **3%** | ✅ **10/10** |

**昨天那個「單輪之內與跨輪之間同時越跑越差」今天沒有重演。** 尾巴整個縮回去：
最大值再也沒碰到 8（腳本門檻）或 10（韌體硬底線）；「超標後自行回復」**0 次**
＝不是靠平衡迴路救回來的，是根本沒超標。週期 9/10 的 Δmax（4 / 6）與週期 1/2（5 / 5）同級。
roll 全程守住：回程 roll均 0.60~1.01，停後 roll 絕大多數 <1°，最大 +2.82（門檻 6.0）。

🔴🔴 **但它不能拿去跟 10c/10d 比。** 它能證明的只有一句：
**在吸盤完全不參與的條件下，同套機構動作跑 60 段，左右差最大 6cm。**
⇒ **吸附/脫離是嫌疑最大的變因，但這是一次觀察不是結論** —— 續七「run-to-run 變異 ≈ 效果量」
在這裡同樣適用，而且今天另有三個混淆項：機器靜置一夜、開跑前做過一次乾淨的收回頂端、
`level_diff` 由 4 改 5。**要分辨，下一輪就是開幫浦跑同樣的 10 週期。**

📌 順帶一個值得帶著看的樣態：`移動s` 多數 5.3~6.5s，但有 11 段是 **10.0~11.0s**，
而那些段的**停後 roll 明顯偏大**（+1.94 ~ +2.82，其餘多在 ±0.7 內）。
另 `伸出s` 在後段週期的**第 5 步**偏長（6.4 / 6.7 / 7.5 / 7.7 / 8.3s，其餘多為 2.6~4.6s）——
無真空時 `extend_raw` 是固定 10cm、不做補伸重試，**時間本來不該變動**。兩者都未查明。

### 🔴 `JC100:7` / `JC100:8` 今天全程掉線 —— 這是新的

89 / 82 次 TIMEOUT+CRC（`JC100:5` 僅 5 次、`:6` 掛零），自 09:44:20（第 1 週期進行中）開始，
已進 fast-fail 節流。昨天 `cycle10d` 的四顆壓力欄**四個位置都有讀值**（`-19/-6/0/0`、`-1/1/-4/-8`）
⇒ 右側兩顆是今天才掉的。

🔴 **報表照樣印出 `0/-1/1` 這種數字，那是 last-valid 快取不是實讀**
—— 今天四顆壓力欄有一半不可信。今天沒真空所以不影響結論，
**但下一輪開幫浦時這兩顆必須先修好，否則等於瞎跑。**
⚠️ 不能拿 init 的 `[OK] JC-100 5~8` 反駁：那行後面就寫著 `presence not probed`，init 根本不發包。

### `cycle_test.py` 補兩道守衛（本機工作區，🔴 尚未同步到 Pi）

1. **`sys.stdout.reconfigure(line_buffering=True)`** —— stdout 導向檔案是全緩衝，
   整輪產出才 ~4.7KB，一個 4KB 緩衝區都填不滿 → log 從頭到尾停在 0 bytes。
   今天上午就這樣誤判過一輪（測試在跑、機器在動，log 是空的）。與 runbook §A4 對兩支 C++
   記過的 `stdbuf -oL` 同一個坑。**設在腳本裡，不靠呼叫端記得加 `python3 -u`。**
2. **開跑前回讀 `relay_status` 確認真空源**，OFF 就擋下不跑。
   - **不自動補送 `init`**（與「起點不在頂端」同一套判準：不猜；自動送等於在操作者不知情下啟動幫浦）
   - **通道編號從 `relay_status` 自己的 `names` 欄推導，不寫死 2** —— CH3 那次的教訓就是通道對應會變
   - 保留 `ALLOW_NO_PUMP=1` 明示放行，並在標題列印【無真空對照組】

### ⚠️ 我今天用錯兩次工具，兩次都報了錯的狀態（方法論，會再犯）

- **`pgrep -f 'python3 cycle_test.py'` 會匹配到我自己那條 `bash -c` 的命令列** → 腳本早就結束了
  還一直回報 RUNNING，連掛在背景的等待任務也因此空等到 timeout。**改用 `pgrep -a python3` 看實體。**
- **`nc -q<N>` 送完就半關閉 stdin，會吃掉 SLOW 路徑的回覆** → `relay_status` / `pwm status` /
  甚至 `no_such_cmd` 全都「沒有回應」，我一度判定 `relay_status` 是壞的。
  FAST 指令（`status` / `ping`）同步回覆所以看起來正常，**這個假象只在 SLOW 指令上出現**。
  **要用跟腳本 `ask()` 一樣的持續連線去問**，回得好好的。
- 📌 通則：**兩次都是「工具的行為」被讀成「系統的狀態」。** 判定一個東西壞掉之前，
  先確認自己的探測方式在**已知正常**的對象上會給出正確答案（`status`/`ping` 當時就在手邊，我沒比對）。

### 環境現況（收工時）

- 機器停在**頂端** `L=0 R=-4`，`roll −0.72°`，`motion_active=0`，張力 57.9/41.4
- `motion_hz=30`（腳本收尾寫回）、風扇 5%（關）、**16 個繼電器全 OFF**
- 吊機 `crane_control_PI.out` / 本體 `facade_cleaning_v2.out` 皆在跑
  （log：`~/bringup/crane_0902.log`、`~/bringup/wr_0902.log`）
- 🆕 **Web GUI 已起**：`http://192.168.5.25:8080`（`~/projects/web_ver2`，與工作區
  `web_backend/server.js` md5 相同；env `WROBOT_IP=192.168.5.26 CRANE_IP=127.0.0.1`；
  log `~/bringup/web_0902.log`）。washrobot / crane 皆 connected，**arm 未起＝該 dot 紅色是預期**
- 🔴 **未 commit**：本次兩處腳本改動，加上工作區既有的續十二~續十八那批

### 待完成

- 🔴 **開幫浦跑一輪有真空的 10 週期**（送 `init` → 確認 `[init] PQW relays → pump ON` → `relay_status` 複驗 `ch2=1`）
- 🔴 **先修 `JC100:7` / `JC100:8`**，否則壓力欄一半是假的
- 🔴 把 `cycle_test.py` 兩處改動 **rsync 到吊機 `~/bringup/` 並複驗**
- 🟡 未查明：11 段 `移動s` 10~11s 且停後 roll 偏大；後段週期第 5 步 `伸出s` 偏長

---

## 2026-09-01（續十八）— 盤點今日 `cycle_test.py` 的 8 次執行；**最後一輪（10d）先前沒進日誌**

per user：明天要跑這個腳本。先把今天實際跑過幾次、結果如何盤清楚。

### 腳本是哪一支

**`Linux_test/cycle_test.py`** —— 機構週期耐久測試。一個週期 = 由頂端向下 5 步 × 40cm
（走滿 200cm）+ 一口氣拉回頂端。每一步：
**① 風扇 5%（關）→ ② vacuum feet on → ③ pusher all extend_raw 10cm（不驗真空度）→
④ pusher all retract → ⑤ 風扇 7% → ⑥ delay 1s → ⑦ crane pay_out 40 → ⑧ 靜置 300ms**。
＝ 使用者說的「吸盤 + 風扇 + 移動」。

📌 **執行紀錄原本只在吊機 Pi 的 `/tmp/cycle*.log`（重開機就沒了）**
→ ✅ **已複製到 `~/bringup/cycle_logs/`**（14 個檔，含 recover 系列）。

### 今日 8 次執行一覽

| 時間 | 檔案 | 規模 | 結果 |
|---|---|---|---|
| 16:43 | `cycle1` | 1 週期 | 🔴 週期1回程：**roll −6.95° > 6.0** |
| 17:06 | `cycle2` | 1 週期 | 🔴 週期1回程：**左右差 9cm > 8** |
| 17:17 | `cycle3` | 1 週期 | ✅ **完成**（回程 roll均 0.71、出帶 22%） |
| 17:21 | `cycle10` | 10 週期 | ⏹ 只跑到週期1步3（人為中斷） |
| 17:52 | `cycle4` | 1 週期 | ✅ **完成**（回程 roll均 0.90、出帶 33%） |
| 18:11 | `cycle10b` | 10 週期 | 🔴 **週期9**回程：左右差 9cm > 8 |
| 18:29 | `cycle10c` | 10 週期 | 🔴 **週期7**步1：左右差 9cm > 8 |
| 18:50 | `cycle10d` | 10 週期 | 🔴 **週期6**回程：**roll 7.97° > 6.0** |

🔴 **10 週期一次都沒跑完**（最遠 9 → 7 → 6，**一輪比一輪早掛**）。單週期兩次都完成。

### 🔴 `cycle10d` 先前沒有進日誌 —— 而它是驗證「連續 3 筆」修正的那一輪

續十一記的是 `cycle10c`（36 段、中位 2 / p90 5 / 最大 8）。**10d 是之後才跑的，日誌裡沒有它。**

✅ **修正本身做對了**：10d 的表頭已是「左右差>8cm **連續 3 筆**」，結尾統計寫
「**超標後自行回復（未達連續 3 筆）: 1 次 —— 平衡迴路在工作，不是故障**」
＝ 它正確地**沒有**為了一次瞬態而中止。這條修正驗證通過。

🔴 **但這一輪還是掛了，而且是換成 roll 門檻掛的**（週期6回程 7.97°）。

🔴🔴 **更值得注意：左右差分布明顯惡化，而中間沒有任何相關改動**

| | 段數 | 最小 | 中位 | p90 | 最大 | ≥6cm |
|---|---|---|---|---|---|---|
| `cycle10c`（18:29） | 36 | 1 | **2** | **5** | **8** | 3/36 (8%) |
| `cycle10d`（18:50） | 35 | 1 | **3** | **7** | **9** | 5/35 (14%) |

而且 **10d 之內也在惡化**：週期1~3 的 Δmax 多為 1~5，週期4~6 出現 7 / 7 / **9**。
⚠️ **不要直接說「機構在退化」** —— 續七已經記過「run-to-run 變異 ≈ 效果量」的教訓，
兩輪之間也可能只是狀態不同。但**「越跑越差」在單輪之內與跨輪之間同時出現**，值得當成
明天的觀察重點，而不是雜訊。

### 📌 真空壓力普遍很弱（四顆壓力 kPa 欄）

大量 `0 / -1 / -2`，偶爾才有 `-30 ~ -50`。
📌 **腳本自己就聲明第 ③ 步不驗真空度、「不能拿來宣稱吸附系統可用」**，所以這不算測試失敗；
但它與續十的發現直接對得上：**CH3 其實是真空幫浦 B 組、`init` 從沒開過 → 真空系統長期只有一半在運轉**。
⏸ per user 09-01「幫浦先用 A 組，B 之後再規劃」——本項只記載，不改。

---

## 🔴🔴 明天跑之前的檢查清單（今晚的改動會靜默影響結果）

1. 🔴🔴 **確認 `balance_source=imu`。**
   **編譯預設是 `meter`，只要吊機程式重啟過就會變回去**，而 status 以外沒有任何徵兆。
   續八對照組：關掉 IMU 平衡 → 平均 |roll| 0.68°→**2.52°**、出帶 22.7%→**67%**
   ⇒ **這一項沒設，明天整輪數據都不可比。**
   `printf 'set_balance_source imu\n' | nc -q1 127.0.0.1 5002`
2. 🔴 **確認 `fine_adjust_level_diff_cm=5`**（同樣是執行期值，編譯預設 0，重啟會掉）。
   `set_fine_adjust_level_diff 5`
3. 🟡 **起點要在頂端**：腳本預期 `起點 L=0`。**今晚收工時機器停在 `L=74 R=68`**（不是頂端），
   且刻意停在 `L−R=+6`（roll −0.94°）。開跑前要先收回頂端。
4. 🟡 **確認門檻是新值**：`up_stop_total_kg=130` / `retract_tension_stop_kg=75`
   （已寫進編譯預設，冷啟動即生效，複驗一次即可）。
5. 🟡 **log 導到 `~/bringup/cycle_logs/` 而不是 `/tmp`**，避免重開機遺失。

📌 **今天已知會影響這個腳本的三個新事實**（明天看數據時要帶著）：
- **水平點不是 0，約在 `L−R≈5`，而且可能隨繩長變**（續十七）——
  腳本統計的「停後 roll」要配合這件事讀。
- **單側可能在起步 1 秒內暴走 5cm**（續十六），平衡迴路的 250ms tick 來不及作用。
- **總張力讀值隨傾角浮動 10kg 以上**（續十七），看 tension 欄時要記得。

---

## 2026-09-01（續十七）— 兩步收右繩把機器救回規格內；斜率取得三筆一致量測，**但發現水平偏移可能不是常數**

### 矯正過程（per user，分兩步、中間讀 roll）

| 步驟 | L | R | L−R | raw_x | 張力 L / R | 總計 | 比值 |
|---|---|---|---|---|---|---|---|
| 中止後 | 74 | 75 | −1 | **+6.75°** | 59.23 / 22.67 | 81.9 | 2.61× |
| `retract_right 3`（實走 4） | 74 | 71 | +3 | **+2.55°** | 56.66 / 35.53 | 92.2 | 1.59× |
| `retract_right 2`（實走 3） | 74 | 68 | +6 | **−0.94°** ✅ | 51.54 / 41.27 | 92.8 | 1.25× |

✅ **機器回到 ±1° 內並停在該處**（`state=idle`、`tension_valid=1`、雙繩皆載重）。
📌 選 `retract_right` 而非 `pay_out_left` 是對的：**姿態與張力分佈同時改善**
（右繩 22.67 → 41.27 接回載重，比值 2.61× → 1.25×）。
⚠️ **兩步都過衝約 1cm**（指令 3 走 4、指令 2 走 3）——與待辦表「`roll_correct` 致動解析度不足」同源。
下小步時要預期多走 1cm。

### 🎯 斜率：三筆連續量測，一致

| 區間 | Δ(L−R) | Δroll | 斜率 |
|---|---|---|---|
| 第1趟收工 → 第2趟中止 | −5 | +5.70 | **−1.14°/cm** |
| 中止 → 收 3 | +4 | −4.20 | **−1.05°/cm** |
| 收 3 → 收 2 | +3 | −3.49 | **−1.16°/cm** |

⇒ **≈ −1.1°/cm**，三筆同號且量級相符，續十二 `−0.85°/cm` 的**正負號確定正確**（量值偏小）。
這已不是「兩點決定一條線」的循環論證。

### 🔴🔴 但同一組資料指出：水平偏移**可能不是常數，而是隨繩長變**

由各點反推「roll=0 時的 L−R」：

| 量測位置 | 反推的水平點 |
|---|---|
| `L≈50`（本晚最初，L−R=3 @ +0.91°） | **≈ 3.8** |
| `L≈74`（本段三點） | **≈ 4.9 / 5.4 / 5.2** |

⇒ 下降 24cm，水平點移了約 **1.4cm**。若這是真的，**單一常數撐不過 0→229cm 的全行程**
（外插到 229 會差到十幾 cm ＝ 十幾度，顯然不可能全對）。
⚠️ **未證實**：`L≈50` 那一點是在傾倒事件之前、張力分佈不同的狀態下量的，n 也小。
但它足以說明**「水平偏移是一個常數」這個假設本身要先驗證**，不能默認。
📌 可能機制（未驗）：繩長改變滑輪/捲筒處的繩角，或貼牆接觸狀態改變。

### 目前設定與為什麼**仍不寫進編譯預設**

執行期已設 **`level_diff=5`**（本區段最佳估計 ≈5.2）。**編譯預設維持 0。**
理由不再是「值不準」——現在值相當可信了——而是**上面那條：它可能根本不該是常數**。
把一個或許隨行程變化的量寫死成單一常數，是在製造下一個「同一個數字兩種語意」型的坑。
🔴 **下一步應該是：在 `L≈50` / `L≈150` / `L≈229` 各量一次水平點**，先確認它是常數還是函數，再決定要寫常數還是要做成表/線性項。

### 📌 順帶：總張力讀值隨姿態變動，幅度超過 10kg

81.9（歪 6.75°）→ 92.8（水平）。續十五記的「20cm 下降掉 4.7kg」現在有解釋了：
**不是感測器漂移，是總和本身隨傾角變**。
🔴 這件事有安全意義：`up_stop_total_kg=130` 是吃**總和**的門檻，而總和會隨姿態浮動 10kg 以上。

---

## 2026-09-01（續十六）— 第 2 趟被張力差守衛中止；**右繩在 1 秒內暴走 5cm**，但也因此量到了斜率

### 事件

第 2 趟 `pay_out 20`（起點 `L=71 R=67`＝L−R **+4**、`raw_x=+1.05°`、張力 55.5/33.3）：

```
[20:53:55.084] [HOLD-TRACE] sync_start EXIT OK (both running, verified)
EVT tension_alarm kind=diff left=58.0607 right=7.06728
ERR tension_diff
[fine_adjust] skipped (tension_diff)
```

**整段不到 1 秒，一個 [BAL] tick 都沒來得及跑**（`BALANCE_TICK_MS`=250）。
停下後：`L=74 R=75`（L−R **−1**）、`raw_x=+6.75°`、張力 59.19/22.57。

⇒ **右側在不到 1 秒內多放了 5cm**（L +3 / R +8），右繩張力一度掉到 **7.07 kg**（幾乎鬆繩），
`tension_diff` 守衛（門檻 50）在 51.0 觸發並中止。**守衛做對了它該做的事。**

📌 停止後右側張力由 7.07 回到 22.57 ⇒ 那 7.07 是**運動中的瞬態**；但位置差 5cm 是真的，
不是量測假象。可能的機制（**未證實，n=1**）：起步瞬間重量分佈偏移 → 右繩卸載 →
開迴路 VFD 在輕載下轉得更快 → 放更多 → 更鬆，一個正回饋。
⚠️ `sync_start` 的啟動驗證**通過**（兩側都確認在轉），所以這次**不是**續七那個「一側沒啟動」。

### 🎯 但這次意外給了最有價值的一筆量測：斜率的正負號確定了

| | L−R | raw_x |
|---|---|---|
| 第 1 趟收工 | +4 | +1.05° |
| 第 2 趟中止 | −1 | +6.75° |

Δ(L−R) = **−5**、Δroll = **+5.70** ⇒ **−1.14°/cm**。

🔴 **這確認了續十二 `−0.85°/cm` 的正負號，並推翻我在續十五由 n=1 算出的 +0.14°/cm**
（當時就已註明「不下斜率結論」，現在證實那是雜訊）。
⇒ **水平點（roll=0）落在 L−R ≈ +4.9**。
⚠️ **誠實標註**：上表兩點就是決定這條線的兩點，所以「兩邊都算出 4.9」是**同一次量測**不是兩次獨立驗證。
它比續十五那次 1cm 外插好得多（跨距 5cm、且正負號與續十二獨立吻合），但仍是 n=1 的兩點。

⇒ **`level_diff` 的最佳估計由 4 修正為 5。** 仍未達「可以寫進編譯預設」的證據門檻。

### 🔴 目前狀態與待處置

機器**懸吊、停穩、歪 +6.75°**：雙繩皆有張力（59.19 / 22.57，總 81.8kg）、馬達已停、
`motion_active=0`、`state=idle`、`tension_valid=1`。**不是危險狀態，但不該就這樣放著。**

**建議的矯正**（未執行，待使用者決定）：要把 roll 由 +6.75 拉回 0，需 Δ(L−R) ≈ **+5.9cm**。
🔴 **優先用 `retract_right`（收右繩）而不是 `pay_out_left`**：右繩目前只承 22.57kg、左繩 59.19kg，
收右繩會同時改善**姿態與張力分佈**；放左繩則會讓已經偏重的左側再往下。
建議**分兩次 3cm、中間讀 roll**，不要一次下 6 —— 這台今晚剛示範過「一側在 1 秒內跑掉 5cm」。
（`retract_left|right <cm>` 是量測式單側收繩，走 `retract_tension_stop_kg`=75 的軟停保護；
右側現在 22.57kg，離 75 很遠。）

### 📌 這次也回答了續十三留下的一個問題

`tension_diff_max_kg=50` 我今天標記為「正常狀態就用掉一半預算（25kg）」。
現在有實例了：**它在 51.0 觸發，攔下了一次真實的單側失控**。
⇒ 這個門檻**不宜再放寬**；如果之後要調，方向應該是**收緊**而不是放鬆。

---

## 2026-09-01（續十五）— `pay_out 20` 運動驗證：機制確認可用，但**值沒被驗證通過**

per user 指定方向：往下放 20cm。

### 🔴 先記一件冷啟動翻出來的事：`balance_source` 的編譯預設是 `meter`

重啟後 status 顯示 `balance_source=meter`，而重啟前是 `imu`
（`main.cpp:834`：`g_balance_source {BalanceSource::Meter}`，註解寫「預設 Meter＝與現行逐位元相同」）。
⇒ **這是今天一直在修的同一類東西：值只活在記憶體，重開就沒了。**
而它的後果不小 —— 續八的對照組實測「關掉 IMU 平衡 → 平均 |roll| 0.68°→**2.52°**、出帶 22.7%→**67%**」，
也就是**任何一次重啟都會靜默地把姿態控制退回那個狀態**，而 status 以外沒有任何徵兆。
本次已先 `set_balance_source imu` 再測，否則測到的不是 production 行為。**已另立待辦列。**

### 測試條件

`balance_source=imu`（kp_ratio 0.2 / deadband 0.5°）、`imu_roll_fresh=1`、`tension_valid=1`、
`level_diff=4`、起點 `L=50 R=47`（L−R=**3**）、`raw_x=+0.91°`、總張力 93.6kg。

### 結果

```
[BAL] src=imu err=-0.89deg → -0.95 → -2.75 → -3.11 → -2.13   （err = -roll，pay_out 時 direction=+1）
[motion_rope] sync stop (leader=L) trigger L=70 R=65 base_L=50 base_R=47 delta_L=20 delta_R=18
              target=20 after_200ms L=70 R=66
[fine_adjust] L=71 R=67 level_diff=4 diff=0 — within ±1 (diff_tol), stopping both
```
收工：`L=71 R=67`（L−R=**4**）、`raw_x=+1.05°`（靜置 15 秒後穩定）、總張力 88.9kg。

### ✅ 兩件確認了的事

1. **機制端對端正確**：`diff = (71−4) − 67 = 0` → 判定已在目標、不動作。
   **若 `level_diff=0`（舊行為），它會算出 diff=4 > 1，主動把機器從 IMU 迴路剛安頓好的位置
   拉到 L−R=0** —— 也就是這條修正確實攔下了一次「越修越歪」。
2. **`≈4` 得到一個獨立佐證，而且這次是量到的不是外插的**：
   IMU 平衡迴路的唯一目標是 roll=0，而它**自己把機器停在 L−R=4**。
   `fine_adjust` 只是認出「已經到了」。

### ❌ 但值**沒有**驗證通過，不可以就這樣寫進編譯預設

- **收工 `raw_x=+1.05°`，仍在 ±1° 之外**（起點是 +0.91°，比之前還差一點點）。
- 我的預測是 +0.06°（3→4cm × −0.854°/cm）。**實測 +1.05°，差了 1 度。**
- 🔴 **局部斜率與續十二的 −0.85°/cm 連正負號都對不上**：L−R 由 3→4（+1cm）時
  roll 由 +0.91→+1.05（**+0.14°/cm**）。
  ⚠️ **但 n=1，而且過程中 roll 瞬間衝到 +3.11°** —— 依本專案自己反覆記過的教訓
  （續七「一整天用 n=1 比較，三次增強控制的效果全部低於雜訊」、續九「樣本不足時
  連『看起來有效』都不要寫」），**這裡不能下斜率結論**。
  它能說的只有兩件事：① 4.07 那個外插**未獲確認**；
  ② **`0.85°/cm` 這個數字的定義／正負號值得回頭釐清**（是 L−R？R−L？還是指令差不是讀值差？）
  ——今天已經有好幾處推理靠它，包括我自己設 `level_diff=4` 的理由。

### 📌 兩則順帶觀察（是觀察，不是結論）

- **下行瞬態重現**：過程中 roll 衝到 +3.11°，平衡迴路把它拉回 ~1.05°。
  與既有記載的「下行比上行差」「起步瞬態」一致。
- ⚠️ **總張力 93.6 → 88.9 kg（−4.7）**，而機器並沒有變輕。20cm 的下降不該讓總重量掉 5%。
  可能是滑輪/繩索在不同繩長下的摩擦與遲滯。**現在 kg 有絕對意義、而且安全門檻吃它**，
  所以這個 5% 的漂移值得單獨查一次，不要當雜訊放過。

### 待辦更新

- 🔴 **`level_diff` 要再跑 2~3 趟才能定案**（看 IMU 迴路是否穩定停在 L−R=4、收工 roll 是否 ≤1°）。
  在那之前**維持執行期設定、編譯預設仍是 0**。
  📌 判準沒變：`4` 仍嚴格優於 `0`（0 會主動把機器拉離水平），但「優於」不等於「已驗證」。
- 🔴 **`balance_source` 編譯預設 `meter` vs production 用 `imu`**（見上）。
- 🟡 **總張力 5% 漂移**待查。

---

## 2026-09-01（續十四）— 吊機冷啟動驗證通過；`fine_adjust` 水平參考偏移上線（執行期 4）

使用者回報機器可用 → 續十三決定「留到下次上機」的事今晚就做完了。

### ✅ 冷啟動驗證（20:39 重啟，pid 24446 → 27004）

用它自己的 `exit` 指令走正規關閉（stdin 是 `/tmp/crane.fifo`，keeper 是 `sleep infinity`），
確認行程消失、埠 5002 釋放、**X518 連線釋放**之後才起新的。逐項對照：

| 項目 | 結果 |
|---|---|
| `up_stop_total_kg` / `retract_tension_stop_kg` | **130 / 75 從編譯預設生效** ✅ |
| `dsz_left` / `dsz_right` | **1 / 1** ✅（避開 X518 單連線那個坑） |
| 五個網關 | `gw_a=gw_b=gw_m=gw_c=gw_d=1` ✅ |
| SD76 | `(resumed)`、`L=50 R=47` **未歸零** ✅ |
| 張力零點 | 59.11/34.51 vs 重啟前 59.13/34.53 ⇒ **載重中的零點完整保留** ✅ |

📌 **重啟前預測的三件事全部成立**（不歸零張力計／計米器 resume／唯一馬達動作是 `allMotionOff`）。

🔴 **兩則要記住的觀察**：
- `PQW:12 init presence probe failed`（`pqw_water=0`）**是既有狀況不是回歸** ——
  舊版 19:43:15 與新版 20:39:20 **逐字相同**（`.34` 上的進水球閥本來就沒接）。
  順帶這也是 PQW 改原子交易後的一個等價性佐證。
- ⚠️ **啟動後 5 秒內 4 筆 SE3 comm fail**（`readParam 0x1007`、`clearAlarm 0x1101` ×3），
  20:39:23 之後不再出現，keepalive 兩側 `ok=50 fail=0 clears=0`。判為**快速重啟後
  USR 網關清理舊 session 的瞬態**（SE3 走專屬網關，不在本次改動之列，transport 也未動）。
  🔴 **下次重啟若仍出現，要回頭查，不要照抄這個結論。**

### ✅ `fine_adjust` 水平參考偏移上線

`g_fine_adjust_level_diff_cm`（**預設 0 ＝ 與先前逐位元相同**）＋ `set_fine_adjust_level_diff <cm>`
＋ status 欄位。收斂目標由寫死的「左右讀值相等」改為 `L - R = level_diff`。
實作重點：左側讀值先減 `level_diff` 成 `curL_adj` 參與所有比較，
**只有 `L_stop_at` 要把它加回去**（收斂迴圈比的是原始 `curL`，漏掉就等於沒設）。

🔴 **實測佐證這不是裝飾性修正**：重啟後 `L-R=3` 時 `raw_x=+0.91°`
⇒ 舊行為（`level_diff=0`）會把機器收斂到約 **+3.5°**，遠出 ±1° 規格。

**執行期已設 `level_diff=4`**，出處：現況 `L-R=3` @ `raw_x=+0.91°`
（IMU 基準乾淨：`roll=0.89` vs `raw_x=0.91`，偏移僅 0.02°），
要讓 roll 歸零需 `Δ(L-R)=+0.91/0.854≈+1.07` ⇒ 水平約在 `L-R≈4.07`。
🔴 **這是一次 1cm 的外插，不是直接量到的** —— 斜率 0.85°/cm 本身是今日 5cm 跨距的兩點量測；
而且**沒辦法真的把 roll 調到 0 再讀**，因為 `roll_correct` 的最小可執行步約 5cm > 誤差帶
（就是待辦表裡「致動解析度不足」那條）。負向對照：`set_fine_adjust_level_diff 99` → `ERR out_of_range`。

### 🔴 這件事還沒完，剩兩步

1. **運動驗證**：跑一趟短程（20~40cm）`retract`/`pay_out`，看收工時 `raw_x` 是否落在 ±1° 內。
   這是唯一能證明外插值對的方法。**需要動馬達，尚未執行。**
2. **驗證後要把 4 寫進編譯預設** —— 否則就是重演今天剛修掉的那個陷阱
   （值只活在記憶體，重開回到 0，而 **0 現在已知是錯的**，不是保守值）。
   📌 判準上 `4` 嚴格優於 `0`：就算斜率有偏差，落點也只差一兩度，而 `0` 是穩定的 +3.5°。

---

## 2026-09-01（續十三）— 門檻常數化、最後四支裸對驅動改原子交易，並抓到 XKC 的既有缺陷

接續十二列的三條待辦。**第 1、3 條做完，第 2 條（重心偏左）是決策題，備妥資料等拍板。**

### 1. 張力門檻常數化 ✅

`RETRACT_TENSION_STOP_KG_DEFAULT` 50 → **75**、`UP_STOP_TOTAL_KG_DEFAULT` 70 → **130**
（`Crane_control_PI/main.cpp`）。續十二是用 `set_*` 改在記憶體裡驗證的，重開就回舊值 ——
而 **70 kg 已經低於整機自重 94 kg，一按 UP hold 就會立刻 `hold_all_off`**。
新舊的相對關係維持 08-28 的設計（130 < 75×2，UP hold 仍會先於 retract 觸發），
只是整組換算到校正後的單位。⚠️ **要重啟才生效。**

🔴 **順帶查出兩件同類的事（值都沒動，都加了註解 + 待辦列）**：
- `TENSION_MAX_KG_DEFAULT`(100) 是**單側**門檻，而整機才 94 kg
  → **一條繩承擔全部重量也不會觸發**。它現在只擋得到「單繩受力超過整機重量」，
  擋不到「另一條繩鬆脫」。要擋得到，值須落在 (75, 94) 之間。
- `TENSION_DIFF_MAX_KG_DEFAULT`(50) 對上「水平時本來就有的 25 kg 差」
  → **正常狀態就用掉一半預算**。
  📌 兩者都建議等「重心偏左」拍板後一起調 —— 現在改是對著會變的基準調。
- `app/WASH_ROBOT.h` 的 `ATTACH_PAYOUT_TARGET_KG`(10) 也是校正前的單位（新單位約 21~24），
  但那段 attach pay_out 自 08-27 起是 `#if 0`＝死碼，故只加警告不動值。

### 3. 最後四支裸對驅動改原子交易 ✅

DM2J / PQW / XKC / DY-500 全部改走 `TCP_client::sendAndReceive()`，逾時**一律沿用原值**
（不趁機改時序）。**本體不再有裸對驅動**（`DIHOOL_control` 除外——全 repo 無呼叫端＝死碼）。

| 驅動 | 匯流排 | 做法 |
|---|---|---|
| DM2J | `.20`（與 ZDT 5~8、PQW 12 共用） | `recv_frame_` 拆成 `validate_frame_` + 新 `txn_frame_`；6 個讀取站點 + `sendRecv`（含廣播）全改 |
| PQW | `.20` | `readEcho()` → `txn(cmd, send_timeout)`，3 個站點 |
| XKC | `.22`（與 JC100 5~8、QX 9、DY500 10/11 共用） | `sendRecv()` 內部改；`set_address` 一併改 |
| DY-500 | `.22` | 3 個站點各自改（本裝置未安裝、polling 關閉） |

**刻意保留裸送出的只剩兩處，共同理由是「不配對接收」**：ZDT `trigger_sync_move`（廣播無回覆）
與 XKC `set_baud_rate`（手冊 §1.8 明載不回覆）。後者若改成原子交易，只會白等一個 recv 逾時，
還會把「本來就沒有回覆」記成接收逾時去推 `TCP_client` 的斷線守衛。

📌 **關於那個斷線守衛的一個具體確認**：它的計數是**每條連線**共用的，但
`sendAndReceive` 成功一次就 `note_rx_ok()` 歸零。所以同一條 bus 上只要還有別的裝置在正常交易，
單一裝置故障（例如未安裝的 DY-500 若日後打開 polling）**不會**把整條 bus 扯斷。
這是查證過的，不是推測。

### 🔴 新發現：XKC 收到別的 slave 的回覆會照單全收（既有缺陷）

改完之後發現 XKC 是這批裡**唯一沒有假從站覆蓋**的驅動（dm2j/pqw 本來就在 `test_stage2`，
dy500 有自己的 `test_dy500`），於是把它加進 `test_stage2`。**第一次跑就抓到**：

```
RESULT xkc   wrongslave  read=OK      out=0x1111     *** WRONG ***
```

本驅動原本只驗長度與 CRC，而**寫給別的 slave 的回覆帶著完全合法的 CRC**，於是被當成自己的收下。
這正是 2026-08-28 driver 稽核在 DM2J 修掉的同一類問題，當時漏了 XKC。而它與 JC-100 5~8 /
QX-DO24 9 / DY-500 10/11 共用 `.22`，**收到鄰居的回覆是這條匯流排上真實會發生的事**。
✅ 已補 slave id + FC 0x03 檢查，`wrongslave` 與新加的 `badfc` 都轉為 FAIL，`normal` 仍 OK（負向對照）。

📌 **教訓：改傳輸層時順手加的一支測試，抓到的卻是既有缺陷。**
「剛改完傳輸路徑、又不上機就驗不到」的那一支，正是最需要 bench 測試的那一支。

### 2. 重心偏左 —— 決策題，資料備妥

沒有實作，但**把「軟體上哪裡假設了左右對稱」查完了**，結論比想像中集中：

- ✅ **平衡迴路沒問題**：IMU 路徑的目標是 roll=0，非零繩長差是它自然的解；
  計米器 fallback 比的是**相對位移差**（`prog = direction × (now − base)`），會保留起始偏移。
- ✅ **`length_diff_max_cm` 守衛沒問題**：08-31 已改成比相對位移差（原因也一樣 ——
  兩支 SD76 零點各自獨立，靜止不動就差 13cm）。
- 🔴 **`fine_adjust` 有問題，而且它在每一次 motion_rope 的結尾都會跑**：
  `motion_fine_adjust_sync()` 用 `diff_init = curL_init − curR_init` 的**絕對差**收斂到 0
  （容許 1cm），`align_lengths` 更是明寫 `target = max(L, R)`。
  但 ① 水平對應的是**非零**繩長差（續十二實測當時約 3cm）；
  ② 絕對差裡還混著兩支 SD76 那個與繩長無關的固定零點偏移。
  ⇒ **它收斂到的點與「水平」沒有定義好的關係**；目前接近水平只是現行零點偏移剛好抵銷，
  **任何一次計米器歸零都會靜默地移動這個目標**。
  🔧 提議的修法：給它一個「水平參考偏移」（roll≈0 時的 L−R），收斂到該值而不是 0；
  預設 0 ＝ 行為與現在逐位元相同，另加 `set_*` 讓下次上機一步量到就能校。
  ⏸ **未實作 —— 值必須實機量，而且要等下面那個機構決定。**

**要拍板的是機構層面的三選一**（三者互斥、影響後續所有調參）：

| 選項 | 治什麼 | 代價 |
|---|---|---|
| **配重** | 根因：把重心移回吊點中線 | 往已經 94 kg 的機器再加重量 |
| **改吊點** | 同上，且不加重量 | 要動機構；牽涉 `FOLLOWER_SPAN_CM` 等幾何常數 |
| **接受 + 控制器補償** | 只治症狀 | `roll_correct` 的**最小可執行步比死區大**（指令 1cm 實動約 5cm、roll 變 −4.73°，而容許帶只有 2°）→ 迴路結構上收斂不了。要走這條得先讓吊機能執行小差動，或把容許帶開到比最小步大 |

📌 對照組已證實姿態誤差是**單向漂移不是擺盪**（關掉平衡 → 平均 |roll| 0.68°→2.52°、
出帶 22.7%→67%），也就是有一個持續的單向不平衡在推，控制器是在對抗它。
**在這件事決定之前，調任何控制參數都是在補症狀。**

### 驗證

- **建置（Pi 上 g++ 14.2，看產物時間戳與 md5、不看管線離開碼）**：
  吊機 `crane_control_PI.out` ✅／本體 **16/16 TU + 連結成功** ✅／`Linux_test` ✅。
  三個目標都編＝跨模組契約兩端都驗（`user_lib/*.h` 是跨模組契約，只編前兩支等於只驗一端）。
- **假從站迴歸（全程只連 `127.0.0.1`，不碰真 485）**：
  `test_stage2` 5 驅動（pqw/dm2j/xkc/zdt/se3）× 5 模式（normal/badcrc/wrongslave/shortframe/drop）
  ＝ **25/25 PASS、0 WRONG**；`test_dy500` 5 模式全 PASS；
  `test_dm2j`（機構標定 + 行程守衛）全 PASS，且假從站的 `req#` 證實被拒絕的指令**沒有送出任何位元組**。
- 🔴 **尚未上機**：兩支程式都還沒部署、沒重啟。**門檻常數化要重啟才生效。**
  機器目前仍懸吊在 `L=50 R=47`、`raw_x=+0.92°`、張力 59.3/34.6，兩支程式運行中。

### 🔴🔴 收尾時查到的事：`~/bringup/` 就是部署目錄，「編譯 ≠ 不碰部署」是錯的

準備部署時查 `/proc/<pid>/exe` 才發現：**兩台的程式都直接跑在 `~/bringup/` 底下**
（吊機 pid 24446、本體 pid 9561，`cwd` 也都是 `~/bringup`）。而 runbook §1 的標題寫的是
**「建置（在 Pi 上，另開目錄，不碰現有部署）」** —— 那句話是錯的，已更正。

⇒ **本次的建置指令已經把兩支服役中的執行檔換掉了**（兩個 `/proc/<pid>/exe` 都顯示
`(deleted)`＝行程握著舊 inode 繼續跑）。方向是對的（新版正是要的），但**那是運氣不是設計**。

📌 **這件事反過來改變了「今晚要不要重啟」的答案**：
原本的理由是「門檻只在記憶體、意外重啟會回 70/50 而 70 < 整機自重 94kg ＝ 收不了繩」，
而今天連線卡死已經害我們重啟過三次，所以那不是假設性風險。
**但磁碟上的執行檔現在已經是新版**，任何一次啟動（計畫內或意外）都會拿到 130/75
→ **那個窗口已經不存在了**。重啟只是讓執行中的行程去對齊磁碟，而它記憶體裡的值本來就是對的
⇒ **零收益，換一次懸吊中的重啟風險。決定：今晚不重啟**，留到下次上機、有人在場時冷啟動並複驗。

✅ **唯一有時效性的事已做**：把兩支**執行中**的執行檔存成回滾點——那些 inode 隨行程結束就消失。
`crane_control_PI.prev5`（md5 `a1ec5622`）／`facade_cleaning_v2.prev8`（md5 `d11c64c9`）。
新版分別是 `3d9367af` / `753b65a7`。

🔴 **重啟前已逐項確認過是安全的**（下次上機直接用）：
① **啟動不會對載重中的張力計歸零** —— 零點在 X518 flash，`init` 只寫單位暫存器 + 套 scale，
   `save_params()` 刻意只在 `cmd_zero_tension` 才呼叫；
② **計米器 `resumeMeter()` 沿用保存值** —— `L=50 / R=47` 會留著，不會歸零；
③ **啟動時唯一的馬達動作是 `allMotionOff()`**（"Safe startup state"）。
⚠️ 仍要留意既有的兩個坑：**X518 只允許一條 TCP 連線**（舊行程沒完全退出就起新的 →
`dev_dsz=0` 且旗標只在 init 設一次＝整個 session 讀不到張力）、判斷行程用 `ss -ltn`／`ps -eo comm`，
**絕不用 `pkill -f`／`pgrep -f`**。啟動方式：`cwd=~/bringup`、`./crane_control_PI.out`、
stdin ← `/tmp/crane.fifo`、stdout/stderr → `/tmp/crane.log`、ppid=1（detached）。

### 待辦（接手先看）

1. 🔴 **重心偏左三選一**（見上表）—— 它是姿態問題的根，先於任何調參。
2. 🔴 **`fine_adjust` 的水平參考偏移** —— 修法已想好，等 1 決定後實機量一次即可。
3. 🟡 **下次上機第一件事：冷啟動兩支程式並複驗** `status` 的 `up_stop_total_kg=130`
   / `retract_tension_stop_kg=75`（磁碟已是新版，不需要再部署，只需要重啟），
   順便確認 `dsz_left=1 dsz_right=1`（X518 單連線的坑）。
4. 🟡 `TENSION_MAX` / `TENSION_DIFF` 等 1 決定後一起調。

---

## 2026-09-01（續十二）— X518 改單台雙通道、張力刻度校正完成，並查出**重心偏左**

### 硬體變更：X518 由兩台改一台雙通道（per user）

原本左右各一台（`.32` 左 / `.33` 右，兩台都只用 CH1）。使用者移除一台，**剩下的
`.33` 一台接兩個通道：CH1 = 右、CH2 = 左**。

| 檔案 | 改動 |
|---|---|
| `DSZL_107.h/.cpp` | 新增 `set_channel(1\|2)`；讀值 CH1=`0x0A00` / CH2=`0x0A02`；新增通道感知的 `do_zero()` |
| `main.cpp` | 單一 `DSZL_IP`、兩側共用 `cli_C`；`DSZL_CH_RIGHT=1` / `DSZL_CH_LEFT=2`；歸零全改 `do_zero()`；`set_unit_kg` 改只呼叫一次（**裝置層級不是通道層級**） |

📌 **保留兩個 DSZL_107 物件**（而非單物件 + `get_both_long`）：各自要有獨立的 scale、
錯誤計數、last-valid 快取與 `@L`/`@R` 標記，而上層 `read_tensions()` / `cmd_tension` /
歸零 / 安全檢查全建立在左右對稱結構上；而且左右**確實需要不同 scale**。
📌 `cli_D` 刻意保留不刪 —— 沒有 `connectToServer` 就不連線、成本為零，刪掉會連帶動到
`g_gw_d_ok` 與 GUI 正在解析的 status 欄位。

🔴 **`do_zero_ch1()` 仍是 public 的坑**：兩側共用一台之後，直接呼叫它會讓左側物件去
歸零右側的通道，**而且完全不會報錯**（Modbus 寫入本身成功）。已在標頭與呼叫點寫警告。
✅ 實測驗證分流正確：歸零左側時右側維持 `0.000000` 完全沒動。

### 🔴 `set_log_side()` 從來沒有生效過（兩個 init 多載不一致）

```cpp
init(const std::string& ip, ...)  → _log_tag = "DSZL:" + ID + (side 併回)   ✅
init(TCP_client& extClient, ...)  → _log_tag = "DSZL:" + ID                 ❌ 直接覆蓋
```
**專案用的正是後者**，所以 08-31 加的側別標記等於沒做 —— 當日整份 log 裡 `@L`/`@R`
各出現 **0 次**，全是分不出左右的 `[DSZL:1]`。而標頭註解卻寫著「由 init 併進 tag」。
已修，兩個多載行為一致。

### ⚠️ 新的操作限制：X518 只允許一條 TCP 連線

外部探測工具（`Linux_test/x518_*.py`）**必須先停吊機程式**，否則一律被拒絕。
當日踩過：`ping` 通但 `502` 拒連，差點誤判成裝置沒起來 —— 實際是被程式佔著。

### 🎯 張力刻度校正完成（已知重量 4.16 kg）

| | 右 (CH1) | 左 (CH2) |
|---|---|---|
| 4.16kg 時 raw | −176 | −202.1 |
| **scale** | **−0.0236364** | **−0.0205816** |
| 解析度 | 42.3 counts/kg | 48.6 counts/kg |
| 雜訊 | ±1 count (±0.024kg) | **±3.5 counts (±0.07kg)** |
| 負向對照 | ✅ 回 0 | ✅ 回 0 |

已寫進 `main.cpp` 常數（`DSZL_SCALE_RIGHT` / `DSZL_SCALE_LEFT`，先前兩側共用一個
`-0.01` 是錯的），重啟驗證開機即帶正確值。

🔴 **跳過歸零直接校跨距會得到完全錯誤的結果。** 第一次嘗試時沒先 `zero_tension`，
拿「碰巧讀到 0」當基準，量出來**方向（+）與大小（12 counts/kg）全錯**，
而且**負向對照沒回零**（移除後停在 raw 117 且穩定 25 秒）才抓到。
📌 **正確順序**：① 空載 `zero_tension` → ② 掛已知重量讀 raw → ③ 算 scale →
④ 移除必須回 0。**順序是必要的，不是形式。**
⚠️ 左側雜訊是右側的 3 倍，用在過載保護無妨，但要拿張力做細緻判斷前要先處理。
⚠️ 單點校正（過原點），4.16kg 外推到工作範圍 30~60kg 的線性度未驗。

### 🔴 校正的連鎖後果：安全門檻全部變得太緊

校正後同一個實體負載的讀數變成 **2.1~2.4 倍**，而門檻是拿未校正讀值調出來的：

| 參數 | 舊 | 新 | 實測 |
|---|---|---|---|
| `up_stop_total_kg` | 70 | **130** | 94.3（總） |
| `retract_tension_stop_kg` | 50 | **75** | 59.7（單側） |
| `tension_max_kg` | 100 | 維持 | 59.7 |
| `tension_diff_max_kg` | 50 | 維持 | 25.1（差） |

🔴 **這兩個新值只在記憶體裡，重開程式回到 70/50 —— 那會直接擋住收繩。**
它們跟 scale 一樣是**隨校正改變意義**的東西，**待寫進 `main.cpp` 常數**。

📌 **整機重量首次量到：約 94 kg。**

### 🔴🔴 重心偏左：水平與張力平衡互斥

調整繩長差時量到的（`retract_left 4`，實際走 5cm）：

| 狀態 | roll | 左張力 | 右張力 | 比值 |
|---|---|---|---|---|
| 當日稍早（舊 scale，大致水平） | ~0° | 29.74 | 12.53 | **2.37×** |
| 繩長差 8cm（歪 −3.35°） | −3.35° | 46.86 | 45.88 | 1.02× |
| **繩長差 3cm（水平 +0.92°）** | **+0.92°** | **59.73** | **34.60** | **1.73×** |

> **機器要水平，左繩就必須承擔約 1.7 倍的重量。兩側張力相等的那個狀態，機器是歪的。**

⚠️ **自記：我在一小時內把這件事說反了兩次。** 先說「2.37× 不對稱是機構根因」，
再說「重接線後消失了，那是量測假象」—— 第二次錯在**拿歪著的狀態去跟水平的狀態比較**。
不對稱是真的，它**只在水平時顯現**；三組數據裡只要機器接近水平，就是左側承重多。

**這改寫三個先前的判斷：**
1. 🔴 **「左右繩長相等」不是目標。** 繩長差 3cm 才是這台機器水平的正確偏移量；
   硬調成 0 差，機器會往正方向歪約 2.5°。
2. 🔴 **平衡迴路製造左右差是幾何要求，不只是控制器暫態。** 它靠 IMU 驅動、目標是水平，
   而水平在這台機器上**必然對應不等的繩長與不等的張力**。下午把 Δmax 守衛改成
   「連續超標」方向是對的，但理由比當時講的更根本。
3. 🟡 **「下行比上行差」可能同源**：放繩時張力低，重心偏移對姿態的影響被放大。未驗，
   但現在有可檢驗的機制了。

📌 幾何換算 **0.85°/cm**（5cm → 4.27°），與稍早 `roll_correct` 量到的 0.95°/cm 吻合。

### 收工現況

`L=50 R=47`、`raw_x=+0.92°`（±1° 內）、張力 59.7/34.6（總 94.3kg）、`tension_valid=1`、
繼電器 16 通道全關、風扇 5%、本體 `state=idle`、兩支程式運行中。

### 🔴 待辦（接手先看這三條）

1. **門檻常數化**：`up_stop_total_kg=130` / `retract_tension_stop_kg=75` 目前只在記憶體，
   重開就回舊值並擋住收繩。
2. **重心偏左的處置**：是配重、改吊點、還是接受並讓控制器補償？這是姿態問題的根，
   在它之前調任何控制參數都是在補症狀。
3. **裸 send/recv 對還有四支**：DM2J / PQW / XKC / DY_500（ZDT 已原子化）。

---

## 2026-09-01（續十一）— 🔴🔴 查到連線失步的**真正根因**：`available()` 跨執行緒改 socket 模式且不拿鎖

### 起因：使用者的一句提問

跑週期測試時 `.20` 匯流排卡死（四顆 ZDT 全滅、`Recv-Q=8` 持續、只有重啟程式能救）。
使用者問：**「有沒有可能是 TCP_client 因為太多人引用，本身沒有 mutex，導致多個物件同時送出封包相撞？」**

順著查下去，答案比問題本身更嚴重。

### 第一層（使用者猜對的部分）：`socket_mtx` 是「每次呼叫」原子，不是「每筆交易」原子

裸的 `sendData()` + `receiveData()` 對之間**鎖是放開的**，共用同一個 `TCP_client` 的另一條
執行緒可以插進來：`T1 send(A) → T2 send(B) → T1 recv() 拿到 B 的回覆`。

📌 **程式碼早就記載過這件事**：`TCP_client::sendAndReceive()` 的標頭註解寫著
「Pre-existing send/recv pair pattern **was racy because the mutex was released between
the two calls**」—— 它就是為此而寫的（為吊機端共用網關）。**但本體的 5 支裸對驅動從未跟進。**

`.20` 上同時掛著 **ZDT 5~8 + PQW 12 + DM2J 14**，三支全是裸對；而 app 層的 `zdt_bus_mtx_`
**只有 ZDT 在拿**（`WASH_ROBOT.cpp:165` 自己寫明 DM2J 不拿，PQW 也不拿）。
⚠️ **CLAUDE.md 有一句要更正**：說 rail sweep 與主執行緒「靠 `TCP_client::socket_mtx`
序列化（**幀不會交錯**）」—— 那句只有 per-call 成立。

### 🔴🔴 第二層（真正的根因）：`available()` 不拿鎖，而且會改 socket 模式

```cpp
int TCP_client::available() {            // ← 沒有 lock_guard
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);   // 改共用 socket 的模式
    int r = recv(sock, &tmp, 1, MSG_PEEK);
    fcntl(sock, F_SETFL, flags);                // 還原
```

而 `reconnectLoop` **對每一個連線、每 500ms 呼叫它一次**
（`if (!connected || available() < 0)`，連線正常時短路讓它一定被執行）。

| | 故障 |
|---|---|
| **(a)** | 監看緒設非阻塞的瞬間，worker 正在 `recv()` → 立刻 EAGAIN → 判定「無回覆」→ 裝置隨後送到的回覆沒人讀 → **失步** |
| **(b)** | `sendAndReceive`/`drainRx` 的排空也會 `F_GETFL`/設 `O_NONBLOCK`/還原。若 `available()` **在排空期間**讀到 flags，讀到的「原值」已含 `O_NONBLOCK`，還原時把非阻塞寫回去 → **socket 永久非阻塞，之後每次 recv 立刻 EAGAIN、每筆交易都「逾時」** |

### 🔬 現場指紋（決定性證據）

```
17:24:53.170 [ZDT:5] RX Pos Mode: TIMEOUT
17:24:53.271 [ZDT:5] RX Pos Mode: TIMEOUT     ← 只差 101ms
```
那條重試路徑上，兩次 TIMEOUT 之間**應該**有 `readEcho(500)` + `release_stall_flag()` +
`sleep(100)` ≥600ms，且 recv 逾時本身設 300ms。**實測 101ms ⇒ 那些 recv 根本沒有等。**
（稍早 17:21:28.712 → .812 也是 100ms，一模一樣。）

**這一條同時解釋了今天所有對不上的觀察：**
- `Recv-Q` 持續非 0 —— 回覆有到，但每次 recv 立刻 EAGAIN 不讀它
- **只有重連救得回來** —— 新 socket ＝ 全新 flags
- 隨機、跨網關、跨兩支程式 —— 每個 `TCP_client` 都有自己的 500ms 監看緒
- **補了排空還是會卡** —— 排空清得掉緩衝區，清不掉 socket 模式

🔴 **這推翻了今天稍早的兩次歸因**：先前記的「遲到回覆落在下一筆 recv 窗口」與
「執行緒交易交錯」都是**可能存在但不是主因**的機制。真正一直在製造失步的是這條。

### 修法（已上線）

| 項目 | 內容 |
|---|---|
| `available()` | ① 拿 `socket_mtx`（reconnectLoop 是先呼叫、返回後才建 lock_guard，非巢狀，無死鎖）② Linux 改用 `recv(MSG_PEEK\|MSG_DONTWAIT)`，**完全不碰 `fcntl`** —— 不去動共用狀態比加鎖更徹底。Windows 無 `MSG_DONTWAIT`，維持 ioctl 但移進鎖內 |
| ZDT 原子化 | 17 個送出點收斂成一支 `txn()`（`sendAndReceive`）。**只有 `trigger_sync_move` 刻意保留裸送出** —— 廣播無回覆，`txn()` 會把「送出失敗」與「無回覆」收斂成同一個空值，等於拿掉它唯一的錯誤偵測 |

📌 檔案內剩下的所有 `fcntl` 模式切換（`drainRx`/`sendData`/`sendAndReceive`/`sendAndReceiveQuiet`
的排空）**全部在 `socket_mtx` 內**，這條競態徹底關閉。
📌 **附帶效益**：ZDT 自動納入「連續 10 次接收逾時 → 主動斷線」守衛。先前它走裸對，
守衛看不到它 —— 卡死時**沒有任何機制救得回來**，只能人工重啟程式。

⚠️ **一個明著記的行為變更**：`release_stall_flag()` 原本在**送出失敗**時回 `false`（＝無錯），
改用原子交易後無法區分送出失敗與無回覆，一律視為失敗。比原本誠實（指令沒送出去卻回報成功
是錯的），但它是行為變更。

🔴 **未改**：`DM2J` / `PQW` / `XKC` / `DY_500` 四支仍是裸對（per user：先改 ZDT 一支、實機驗證）。

---

### 同日其他修正

**`do_sync_imu_roll_correct_` 發散守衛**（週期測試第 5 步實測發散）
```
pass 0 roll=+2.69° → roll_correct  2
pass 1 roll=-5.29° → roll_correct -5   ← 反號且更大
pass 2 roll=+6.39° → roll_correct  6   ← 再反號再更大
NOT converged — roll=-6.53°  → 回程一啟動就撞中止門檻
```
原邏輯只有「收斂了就停」與「試滿 3 輪」，**沒有「越修越糟就住手」的概念**。
已加：上一輪修完 `|roll|` 沒變小就立刻停手。同時保護正式走法（`do_step_sync_` 呼叫同一函式）。

🔴 **更深一層：致動解析度不足，這個迴路結構上無法收斂。** 手動送 `roll_correct 1`（最小步）
實測 roll 變化 **−4.73°**，而吊機位置顯示 `L 放 2 / R 收 3`＝**指令 1cm、實際動了約 5cm**。
回推 4.73°÷5cm ≈ 0.95°/cm ⇒ **幾何模型（`FOLLOWER_SPAN_CM=100`）其實是對的，
問題是吊機做不出小的差動位移**。而容許帶 `FOLLOWER_ROLL_TOL_DEG` 只有 2° —— 最小可執行步
（≈5°）比死區還大。**修法方向不是校幾何常數，是讓吊機能執行小差動，或把容許帶開到比最小步大。**

**`cmd_pwm_set` 關閉方向不再被狀態擋**：中止時腳本送 `pwm ... duty=5`（停止）收到
`ERR state_violation current=error`（`emergency_stop` 先把本體打進 Error）。那次剛好風扇本來
就關著，但**若中止發生在「風扇已開、吊機移動中」，螺旋槳會停在 7% 一直吹而腳本無法關掉**。
判準：**把螺旋槳關掉永遠不是危險方向**。只有 `duty > PWM_STEP_OFF_DUTY_PCT` 才維持 gating。

**四支新指令**：`relay_status` / `relay <ch> <on|off>`（見續十）、
`pusher <group> extend_raw`（不尋封的伸出）、`imu_level`（單獨執行步後 IMU 校平）。

🔴 **`extend_raw` 的存在理由（per user）**：**有些玻璃面有縫隙**，吸盤落在縫上本來就吸不住
—— 那是現場條件不是故障。而 `smart_extend_subset_` 會為了找封一路補伸（最多 ~16cm）並重試，
在有縫的面上是徒勞且會把推桿推到接近行程極限。**「吸不到就繼續走」是設計要求，不是妥協。**

---

### ⚠️ 一個我犯的錯，值得單獨記

**`init` 會用當下姿態取 IMU 基準（`imu_take_baseline_`）。我在提醒過這個風險之後，
還是在機器歪 2.92° 的狀態下按了 init**，把 2.92° 定義成「水平」。後果兩層：
① `status` 的 `roll` 恆報 0，**中止門檻整個偏移 2.9°**（真實 +6° 讀成 +3.1° 不觸發）；
② `imu_push_loop_` 推給吊機的也是扣基準的值 → **吊機平衡迴路會把機器維持在歪 2.9° 的姿態**。

**兩個對策已落地**：
- 測試腳本的監看與統計**一律讀 `raw_x`**（IMU 直出、與基準無關）。**安全門檻不可以建立在會漂的基準上。**
- 要重取基準用 `imu_zero`（只動 IMU 基準），不要用 `init`（會連帶重設上滑台零點、又撞吊機水閥）

---

### 🎯 10 週期實跑：根因修正驗證通過，並查出左右差守衛在跟平衡迴路打架

**第一輪（8 個完整週期 + 第 9 個的 5 步下行，約 34 公尺、45 次推桿伸縮、530 次單顆運動）：**

| 指標 | 結果 |
|---|---|
| ZDT 真正的 ERR / TIMEOUT / CRC / pos_mode FAIL | **0** |
| 吊機五個網關 `Recv-Q` | 全 0 |
| 本體 `.20` / `.22` `Recv-Q` | 0 / 0 |
| 連續逾時斷線守衛觸發 | 0（沒被用到） |
| `PAUSE-ON-ERROR` | 2 次，都是已知報廢的吊機 PQW 水閥 |

🎯 **對照修正前：`.20` 在第 1 個週期第 4 步就卡死到要人工重啟。**
SE3 仍有 17 次 `comm fail`、QX 25 次逾時 —— **全部自行恢復，沒有一個演變成卡死**。
這正是修正前後的差別：以前這些逾時會把 socket 留在非阻塞狀態、演變成永久失步。

### 🔴 左右差守衛：我裝了一個會在控制器最用力工作時把它關掉的保護

兩輪都中止在「左右差 9cm > 我設的 8」（一次 50Hz 回程、一次 30Hz 回程、一次下行第 1 步），
而**每次終點左右都相等** —— 是瞬態不是走偏。加了分布統計之後真相很清楚：

```
=== 左右位移差分布（36 段）===
  最小 1   中位 2   p90 5   最大 8 cm
  >= 6cm:  3/36 (8%)    >= 8cm: 1/36 (3%)   >= 10cm: 0/36 (0%)
```

🔴 **根本問題：`apply_balance_trim` 就是靠製造左右差來修正 roll 的**（調的是左右不同的 Hz
→ 直接產生左右位移差），而腳本監看的正是同一個量。中止那次的瞬時值 9cm，**當下
`raw_x=4.35°`** —— 那不是繩子卡住，是控制器在修一個大傾角。

✅ **改法（per user）**：從「瞬時超標」改為「**連續 `DIFF_PERSIST`(3) 筆超標**」才中止
（取樣約 0.3s → 約 1 秒持續）。真正該擋的「一側繩子卡住」特徵是 **Δ 持續擴大且不回頭**；
平衡修正則是衝一下就收斂。**門檻本身不動**，韌體的 `length_diff_max_cm=10` 仍是硬底線。
📌 統計另外記「超標後自行回復」的次數 —— 那是平衡迴路在工作的證據，不是故障。
⚠️ **roll 門檻維持瞬時判定，刻意不比照辦理**：那是機體姿態的安全線，一筆真實的 6° 就該停。

### 🔴 姿態：下行明顯比上行差（今天每一輪都成立）

第二輪跑之前機器剛好靜止在 `raw_x=0.02°`，趁這個時機 `init` 取到**今天最乾淨的基準
（偏移 0.02°）**，所以以下數字可以直接對照 ±1°：

| 週期 | 下行 `roll均` | 回程 `roll均` |
|---|---|---|
| 1 | 1.16 – 2.35 | 0.77 |
| 2 | 1.21 – 2.93 | 0.96 |
| 3 | 0.85 – 1.29 | 0.89 |
| 4 | **0.40 – 1.14** | 0.73 |
| 5 | **0.40 – 0.88** | 0.85 |
| 6 | 1.16 – 2.07 | 0.97 |

**回程（上行）穩定在 0.73–0.97°，本身就在 ±1° 內**；下行變異大得多（0.40–2.93）。
第 4、5 週期下行也進了 ±1°，**證明做得到，只是不穩定**。
📌 **下行比上行差**指向機構（放繩時張力低、繩子鬆弛、左右不對稱被放大），不是控制參數。
這是目前姿態問題最明確的線索。

📌 **測試腳本 `Linux_test/cycle_test.py` 已進版控**（含上述所有判準的理由）。

### 週期測試（機構耐久，per user 設計）

一個週期 = 下行 5 步 × 40cm（走滿 200cm）+ 回程拉回頂端。每步：
風扇關 → 開真空閥 → `extend_raw` 10cm → `pusher all retract`（內建關閥→洩壓→CH6 正壓 500ms→
兩段收回）→ 風扇 7% → delay 1s → `pay_out 40` → 靜置 → （`imu_level` 已移除）。

📌 **`imu_level` 已從週期移除**（per user 原本要保留，實測發散後移除）。移動中的姿態由吊機端
IMU 平衡迴路負責就夠了 —— 拿掉它之後 5 步全乾淨。
📌 **回程由 50Hz 改 30Hz**：50Hz 實測左右差瞬間 **9cm**，而韌體自己的 `length_diff_max_cm` 是 10
—— 放寬腳本門檻沒有意義，韌體會先擋。30Hz 多次驗證瞬態差最大 7cm。

**根因修正後的第一趟（1 週期）**：零中止、`.20`/`.22` `Recv-Q` 皆 0、**ZDT 錯誤 0**
（上一輪是四顆全滅）、連續逾時斷線守衛 0 次觸發。

⚠️ **兩件不能宣稱**：
① **姿態變好不能記在傳輸層修正頭上** —— 出帶率由 88~100% 掉到 0~25% 很漂亮，但**基準偏移
同時由 0.85° 降到 0.36°**，判定門檻整個位移了。兩者之間沒有機制上的因果。
② **一趟乾淨不等於根因解決** —— 今天的卡死都是跑 20~40 分鐘才浮現，而那趟只有 2 分鐘。
`available()` 的競態窗口是每 500ms 一次、每條連線各自獨立，要撞上需要時間累積。

---

## 2026-09-01（續十）— 繼電器逐顆實測：8 格裡 1 格是錯的，而且錯的那格關掉了半套真空系統

### 起因

要跑週期測試前先確認本體繼電器對應。發現**根本沒有回讀路徑**：既有指令全是語意層的
（`vacuum`/`pump`/`brush`/`water_pump`，只覆蓋 CH1/2/5/14），CH3/4/7/8 沒有任何指令碰得到，
`cmd_status` 也不含繼電器欄位 —— **實體到底 ON 沒 ON 只能靠「送過哪些指令」推斷**。

### 新增兩支指令

| 指令 | 說明 |
|---|---|
| `relay_status` | 回讀 16 通道（PQW FC 0x01 `readAllStatus()`），附已知用途名稱 |
| `relay <ch> <on\|off>` | 通用單通道控制，**送出後自動回讀該通道** |

🔴 **`relay` 只允許 Idle / Ready**。理由是安全不是潔癖：CH1 是**唯一**一顆真空閥，貼牆時關掉
＝4 顆同時失去真空；CH6 是正壓閥，開它是主動灌氣解封。任何「吸盤可能正在承重」的狀態下開放
raw 繼電器控制，等於提供一條讓機器脫落的捷徑。

📌 **`relay_status` 是唯一安全的查法。** 不要另開 TCP 直接查 `.20` —— 那條匯流排上還有
ZDT 推桿 5~8 與程式自己的輪詢，插入的幀會與運行中的交易搶匯流排（今日的連線失步已吃過這類虧）。

📌 **`relay` 送出後一定回讀**：「指令回 OK」不等於「繼電器真的動了」。2026-07/08 的 `CH_BRUSH`
誤號事件（打到沒接東西的繼電器、log 完全看不出來）就是因為呼叫端既不回讀也不檢查回傳值。

### 逐顆實測結果（CH1~CH8，使用者現場回報實體反應）

| CH | 程式登記 | **實測** | 結果 |
|---|---|---|---|
| 1 | 真空閥 | 真空閥 | ✅ |
| 2 | 真空泵浦 | 真空幫浦 **A 組** | 補「A 組」 |
| 3 | ~~空（原左腳閥）~~ | 🔴 **真空幫浦 B 組** | **記載錯誤** |
| 4 | 空 | 空 | ✅ |
| 5 | 滾筒刷馬達 | 上滑台機械手臂清潔滾筒 | ✅（08-29 更正確認為對） |
| 6 | 破真空閥 | **正壓閥** | 語意不足 |
| 7 | 空 | 空 | ✅ |
| 8 | 空 | 空 | ✅ |

**8 格裡 6 格正確、1 格語意不足、1 格根本是錯的。**

### 🔴 CH3 的兩層後果

**① 真空系統長期只有一半在運轉。** `init` 只送 `CH_PUMP`(=2)，CH3 被當成空通道
→ **幫浦 B 組從開機到現在沒被開啟過**。
📌 **這很可能就是吸盤密封長期需要 `smart_extend_subset_` 反覆補伸（最多推到 ~16cm）才吸得住的原因**
—— 一直被當成機構或吸盤本身的問題在調。**在查明之前不要再那樣歸因。**
⏸ **per user：「幫浦先用 A 組就好，B 之後再規劃」** —— 本次刻意不啟用，只更正記載。

**② 拆掉一顆地雷。** `WASH_ROBOT.h` 原本明文寫著：

> 「若之後又改回兩顆獨立閥，只要把 `CH_VALVE_LEFT` 改回 3，所有邏輯自動還原。」

照做會讓二十幾處閥呼叫去驅動**幫浦 B 組**，症狀是「閥指令回 OK、實際在開關幫浦」。
該句已刪，改為「要改回雙閥請用 CH4 / CH7 / CH8（實測為空），**不可挪用 CH3**」。

### CH6 語意更正：正壓閥

使用者說明：吸盤吸在玻璃上時，脫離**除了**關真空閥讓接口回到大氣壓，**也可以同時給正壓加速脫離**。
**4 顆吸盤共用**這一顆（不分側，與 CH1 同）。通電時間 per user **500 ms** —— 正好等於
`BREAK_VACUUM_TOTAL_ON_MS` 現值，**不用改**。常數名 `CH_BREAK_VACUUM` 保留（避免動既有呼叫點），
語意以註解為準。

📌 這條會改到週期測試的步序：原計畫「關真空閥 → 收推桿」應為
**關真空閥 → CH6 正壓 500ms → 收推桿**。

### 順帶更正的文件

- `motion_flow.md` §3 **整節重寫**。原圖是 **v1 接線**（CH1=泵浦／CH2=腳組閥／CH3=身體組／
  CH4=中心／CH6=水泵／CH7=進水閥、位置寫 `.22`），**六條全部過時**，照它接線會直接接錯。
  期間的搬動：06-05 進水閥移交吊機、07-07 四吸盤改線、08-27 單閥化＋PQW 搬到 `.20`＋水泵讓位、
  08-29 滾筒改回 CH5。
- `runbook.md` §C1 補上 `relay_status` / `relay` 兩支。

### 現場狀態

**16 通道全部 OFF**（逐一送出並回讀確認），機器懸於頂端 `L=0`、推桿全在 0、無吸盤承重、風扇關。
📌 順帶記一筆：**程式重啟後所有繼電器都是 OFF**，「泵浦運行期間常開」只在 `init` 跑過之後成立。

---

## 2026-09-01（續九）— 🎯 貼牆＋風扇 10 趟來回全程跑完；傳輸層修正上線；🔴 「貼牆 0% 出帶」被推翻

### 做了什麼

**1. 傳輸層：連續接收逾時後主動斷線（續八列的頭號阻礙，已修）**

`TCP_client` 新增 `rx_timeout_streak_` 與 `note_rx_timeout()`：連續 **10** 次接收逾時就
`connected = false`，交給既有的背景 `reconnectLoop` 重建 socket —— 重建會丟掉核心緩衝區，
**這是唯一能清掉 RTU 失步的手段**（RTU 沒有交易序號，遲到回覆會被當成下一筆的回覆，
同型別請求連 CRC 都會過 → 靜默採用錯誤資料且永遠慢一筆；不同型別則永遠 bad reply）。

🔴 **兩個刻意的設計選擇，改動前務必先讀懂：**

| 決定 | 理由 |
|---|---|
| **只在 `sendAndReceive` / `sendAndReceiveQuiet` 計數，裸 `receiveData` 不計** | `crane_cli_` / `arm_cli_` / `crane_cli_estop_` / `crane_cli_imu_`（`WASH_ROBOT.cpp:742/845/2438/2848`）是**文字協定的輪詢迴圈，逾時是常態**——就是在等對方推播。計進去會週期性把好端端的連線扯斷 |
| **門檻取 10，不是 3~5** | SE3 的 `reliable_*_one` 單次操作內含 **8 次重試**。門檻若低於它，一叢本來就會失敗的重試會在叢內觸發斷線，把「這次操作失敗」升級成「連線也被拆掉」。10 × 150ms ≈ 1.5 秒，相對於「永遠不會好」已經夠快 |

連線成功／重連成功／對端正常關閉三處都會歸零計數。三個目標（吊機／本體／`Linux_test`）
全部重編通過並換上線 —— `TCP_client.h` 改了 public class 定義，`Linux_test` 也綁在上面。

**2. 貼牆＋風扇 7%，10 趟來回（20 次橫越，45.8 m）—— 全程完成**

```
  趟   方向      秒    n    avg    max     出帶%  Δmax
  1    下   18.3   37   0.96   2.42     43%     7      1    上   19.1   36   1.04   4.87     36%     5
  2    下   17.4   25   0.76   1.88     28%     5      2    上   18.1   34   0.65   2.28     26%     5
  3    下   17.2   22   1.16   3.31     41%     5      3    上   18.1   39   0.72   3.68     18%     4
  4    下   17.6   31   0.82   2.53     23%     3      4    上   21.1   39   0.99   3.54     46%     4
  5    下   17.6   20   0.88   2.40     30%     4      5    上   18.1   25   0.78   2.86     32%     4
  6    下   17.4   29   1.27   3.90     52%     6      6    上   19.9   37   0.62   2.32     14%     4
  7    下   19.3   26   1.09   4.24     31%     6      7    上   19.4   39   0.70   2.41     33%     4
  8    下   17.5   35   0.64   3.32     20%     5      8    上   19.5   35   0.52   1.82     11%     4
  9    下   21.3   38   1.51   4.35     66%     3      9    上   19.1   27   1.06   2.72     52%     7
 10    下   17.3   27   0.65   2.53     19%     4     10    上   19.5   43   0.60   3.88     19%     4
```
`motion_hz=30`、`balance_source=imu`、中止門檻 roll 6°／差 8cm。
**644 筆取樣、零中止、零 `vfd_start_fail`、`tension_valid` 全程 1。**

### 🔴 更正：續八的「貼牆 0% 出帶、最大 0.69°」是取樣不足造成的假象

那筆數字是 **95cm 單程、9 筆取樣**。這次拉滿 **229cm × 20 趟、644 筆**之後：

| | 自由懸吊（續七/八） | 貼牆＋風扇（本次） |
|---|---|---|
| 超出 ±1° | 22.7%（976 筆） | **31.7%（644 筆）** |
| 各趟最大 \|roll\| 平均 | — | 3.06° |
| 最差單筆 | — | 4.87° |

🔴 **「貼牆＋風扇就能把姿態壓進 ±1°」不成立。** 貼牆不但沒改善，數字還略差。
📌 **教訓（今日第二次犯同一類錯）**：9 筆取樣、單一條件、n=1 —— 續七已經記過
「run-to-run 變異 ≈ 效果量」，續八還是用 9 筆下了結論。**樣本數不足時不要下結論，
連「看起來有效」都不要寫。**

### 傳輸層修正的實測結果：已上線，但**這次一次都沒觸發**

| 指標 | 數字 |
|---|---|
| 連續逾時斷線（新機制） | **0 次**（計數從未累積到 10） |
| `vfd_start_fail` | **0 次**（先前 5 次起步量測**全掛**） |
| SE3 `comm fail` | 20 次，**全部被呼叫端重試接住** |

🔴 **所以不能宣稱「不再卡死是這條修正的功勞」** —— 它是保險，不是本次成功的原因。
`vfd_start_fail` 從「5/5 全掛」變成「20 次橫越 0 次」的差異**目前無法歸因**
（唯一同時發生的變化是兩支程式都重啟過）。**保留這條修正，但它仍是未驗證的路徑。**

### 待辦更新

- ✅ **傳輸層 recv 逾時不斷線**（續八的頭號阻礙）—— **已修上線，但未被實際觸發過**，
  降為 🟡「機制未經現場驗證」。要驗它需要能重現卡死，而卡死目前不再自發出現。
- 🔴 **姿態 ±1° 未達標，且已知貼牆不是解法。** 下一個該查的是**左右張力長期不對稱**
  （本次收工 `27.92 / 15.16`＝1.8 倍；貼牆時曾到 2.27 倍）。續八的關平衡對照組顯示
  那是**單向持續漂移、不是振盪** → 控制器一直在對抗一個**機構上的固定偏差**。
  消掉偏差比繼續調 kp 有機會。⚠️ 這條也讓 **DSZL 量值校正**（需已知重量）重要性上升：
  現在只知道兩側比值，不知道絕對值對不對。
- 🟡 起步瞬態（疑靜摩擦）仍未量測 —— 本次 10 cm 定位時又看到一次（左側前 3 秒不動、
  右側已走 8cm），但整趟收斂。

### 現場狀態（收工）

`L=0 R=-2`（頂端）、`roll=-0.42°`、`tension_valid=1`、**風扇已關（`ch1=5%`）**、兩支程式運行中。

---

## 2026-09-01（續八）— 🎯 10 趟任務完成；貼牆實測 0% 出帶；連線失步的真正機制查明

> 🔴 **本篇的「貼牆 0% 出帶」已於 2026-09-01（續九）被推翻** —— 那是 95cm／9 筆取樣的假象，
> 完整 229cm × 20 趟（644 筆）實測為 **31.7% 出帶**。

### 🎯 任務達成：10 趟來回 / 20 次橫越 / 45.8 m 全部完成

```
取樣 976 筆   超出 ±1°: 222 (22.7%)
各趟最大 |roll| 的最大值 4.54°   平均 2.33°
單程 17.0–18.4 秒，20 段全部 OK，無中止
```

| 面向 | 結果 |
|---|---|
| **可靠性** | ✅ **達標**。20/20 段 OK、耗時完全穩定無劣化、`tension_valid` 全程 1 |
| **姿態 ±1°** | 🔴 **未達**（22.7% 出帶） |

📌 **第一次嘗試（roll 門檻 5°）在第 7 趟下行中止**，放寬到 6° 後跑完全程。
兩輪對比：出帶 ~30% → **22.7%**，各趟範圍 20–52% → **12–38%**。
📌 **「下行系統性較差」的觀察在 n=20 下不成立** —— 上 12–37%、下 18–38%，那也是雜訊。

### 🔬 控制實驗：關掉平衡跑對照組 —— **推翻我自己的假設**

先前推測「迴路延遲 ≈ 半個擺動週期 → 控制器在激發擺盪」（因為三次增強控制都沒改善）。
**對照組直接否定它**：

| | 平衡開（n=10 上行） | **平衡關**（對照組） |
|---|---|---|
| 平均 \|roll\| | 0.68° | **2.52°**（3.7 倍） |
| 出帶 | 22.7% | **67%** |
| 完成度 | 20/20 段 | 🔴 **走 96cm 就中止（6.59°）** |

🔴 **軌跡形狀是關鍵**：`-4.68 → -5.63 → -6.59` **單調惡化，不是來回擺盪**。
→ 系統有**持續性的單向不平衡**，控制器正在持續對抗它；不是控制器在幫倒忙。
→ 增強控制方向是對的，只是補不完。**根因在機構**——張力長期 26 vs 15（左側 1.7–2.3 倍）。

### 🎯 貼牆 + 風扇：**0% 出帶，規格達成**

per user 08-31 描述的實際作業模式是「風扇把本體壓在玻璃上、四輪頂住、**風扇維持開啟**向下移動」。
**今天先前全部測試都是自由懸吊 —— 那是最壞情況，而且不是實際作業狀態。**

`pwm set 1 50 65535 7`（＝`step_move_on` 的 7%），貼牆下行 95cm：

| | 自由懸吊（平衡開） | 自由懸吊（平衡關） | **貼牆 + 風扇** |
|---|---|---|---|
| 出帶比例 | 22.7% | 67% | **0%** |
| 平均 \|roll\| | 0.68° | 2.52° | **0.36°** |
| 最大 \|roll\| | 4.54° | 6.59° | **0.69°** |

⚠️ **僅 9 筆取樣**（95cm/9 秒），**不足以稱為已證明**，但每一筆都在規格內、最大值也不勉強。

**兩個觀察點的答案**：
- 平衡控制器**仍在動作**（本趟 7 次，err 約 −0.7~−1.1°）→ **貼牆不是讓誤差消失，是讓它不再累積**；兩者都在起作用。
- 🔴 **張力不對稱反而加劇**：自由懸吊 28.16/14.25（1.98 倍）→ 貼牆 27.13/11.94（**2.27 倍**）。
  姿態結果是好的，但這讓「DSZL 量值刻度未校正」那條待辦有了新的重要性。

### 🔴 起步瞬態（貼牆模式特有）

完整協議（關扇上行 → 頂端開扇 → 開扇下行 229cm）在**起步 0.4 秒**就中止：
```
t=0.2  L走0 R走0  roll -0.14   ← 靜止，貼附良好
t=0.6  L走6 R走1  roll -4.08   ← 左側跑掉 6cm、右側只走 1cm
```
推測是**靜摩擦**：四輪被風扇壓在玻璃上，起步要克服靜摩擦，哪一側先掙脫就先走。
**自由懸吊時不存在這個現象。** 而 95cm 那趟沒事——它起步前已貼著一段時間且剛移動過。
🟡 **未能量測**：5 次重複測試全部 `ERR vfd_start_fail`（見下），什麼都沒測到。

### 🔴🔴 連線失步：真正的機制終於查明（今日第 6+ 次）

**三個假設被逐一否掉的完整過程**（記著免得重走）：

| 假設 | 否定證據 |
|---|---|
| USR 網關 `_pt=0` 幀分片 | `comm fail` 的定義是**收到 0 bytes**；分片會走**另一條訊息** `bad reply len=%d`。**`_pt` 未動**，08-28「沒證據不動共用設定」的決策依然成立 |
| 裝置變慢／故障 | 停程式後唯讀探測 `se3_latency.py`：**兩側皆 4.8–5.5ms、6/6 成功、回覆逐位元相同** |
| ~~失步自我延續~~ | 🔴 **這個我否定錯了** —— 見下 |

🔴 **我否定「自我延續」時查錯了 driver**：查 `TCP_client::sendAndReceive` 發現有 drain 就下結論，
**但 DSZL 走的是 `sendData` + `receiveData`（無 drain）**，SE3 才走 `sendAndReceive`。
**兩支 driver 走不同路徑**，這也解釋了為何 SE3 的故障間歇會自癒、DSZL 的是永久的。

**真正的機制**：
```
交易 N   送出 → 逾時 → 失敗
回覆 N   在逾時之後才到
交易 N+1 drain（回覆 N 還沒到，排空到的是空的）→ 送出 → recv 期間回覆 N 抵達
         → 被當成 N+1 的答案 → 對不上 → 之後每一筆都落後一筆
```
🔴 **drain 只能清「已躺在緩衝區」的資料，抓不到「在 recv 窗口內才抵達」的遲到回覆。**
所以 `drainRx` / `sendAndReceive` **兩個修正都不夠**。

**證據**：`ss` 顯示 socket 仍 `ESTAB` 但 `Recv-Q` 卡著**正好一筆完整回覆**
（DSZL Modbus TCP = 13 bytes；SE3 Modbus RTU = 7 bytes），且 `txid` 訊息**正好差一筆**。

### ✅ DSZL 的修法有效並**實測復原一次**

Modbus TCP 有 `txid` 可比對 → **不符時丟棄、繼續讀（不重送）**，一筆交易內重新同步。
```
丟棄遲到回覆: 1 次   →  tension_valid 全程維持 1
```
📌 **這是正面證據**，不是「沒發生所以看起來沒事」。
⚠️ 刻意**不重送請求**：重送只會讓佇列再多一筆答案，把落後一筆變成落後兩筆。

### 🔴 但 SE3 不能照抄 —— 建議的通用修法在傳輸層

SE3 是 **RTU over 透明網關，沒有 txid**，只有 slave/FC/CRC，無法分辨「這是上一筆的回覆」。

查 `TCP_client::sendAndReceive`：**recv 逾時只回傳 −1，不會把連線標記為失效**
（只有 `send` 失敗才設 `connected=false`）。
🔴 **所以一次逾時就足以讓連線永久卡死，而且沒有任何機制會把它救回來。**

**建議修法**：連續 N 次 recv 逾時後**主動斷線**，讓既有的背景重連清掉失步狀態。
對**所有 RTU driver 一次生效**，不必逐支處理。
📌 這也解釋了整體模式：**重啟程式之所以有效，正是因為它強制重建連線**
—— 我們一直在手動做這件事該自動做的事。

### ⚠️ 自記：我犯的三個錯

1. 🔴🔴 **`mission_run.py` 的方向寫反，被使用者在執行前攔下。**
   機器在頂端（L=0），腳本每趟固定先 `retract`（往上）—— 從頂端再往上收 229cm
   會把機器拉進吊機／屋頂結構。**根本問題不只是順序寫反：腳本讀了 `status`
   卻沒有用它決定方向。** 已改為由實測位置推導 + 區間守衛（會把機器帶出
   `[TOP, BOTTOM]` 的指令一律拒絕；不在端點時**不猜方向**，直接停）。
   **教訓：一個可以被人寫錯的順序不是保護。**
2. **否定「自我延續」時查錯 driver 的傳輸路徑**（見上）。
3. **一整天用 n=1 做比較**：50Hz 三趟穩定 30–36%，但 30Hz 在無相關改動下由 11% 跳到 33%
   —— **趟與趟的變異與我在比較的效果一樣大**。三次「增強控制」的改動（修飽和／
   比例式 kp／減速縮放）**效果都低於雜訊，不宣稱有效**。停下來改用任務本身取樣才對。

### 本輪程式改動（全部經 20 次橫越驗證，除標註者外）

| 改動 | 說明 |
|---|---|
| `TCP_client::drainRx()` + 27 處呼叫 | ZDT×17／DM2J×7／PQW×3 走 `sendData`+自有組幀，無排空。**刻意不放進 `sendData()`**：`crane_cli_`/`arm_cli_` 是文字協定，排空會吃掉吊機的 `EVT` 廣播 |
| `DSZL_107` 改用 `sendAndReceive` + **txid 重新同步** | 🟡 txid 那部分**僅實測復原一次**，長期穩定性未證明 |
| `mission_run.py` 方向守衛 | 由實測位置推導、區間外拒絕、不在端點不猜 |
| `apply_balance_trim` 逐側 base_hz | 解除接近段的平衡 gate |
| `hz_max = base + max(offset, cap/2)` | cap 隨速度縮放但 offset 不會 → 高速時 cap 達不到 |
| 減速距離隨速度縮放（只放大不縮小） | 🟡 效果低於雜訊，未證明 |
| `balance_imu_kp` 改為 base 的比例 | 🟡 同上 |
| `length_diff_max` 15 → 10 | IMU 模式下它是**卡死偵測**不是姿態指標 |
| 新工具 | `mission_run.py`／`startup_probe.py`／`se3_latency.py` |

### 待完成

- 🔴🔴 **傳輸層：recv 連續逾時後主動斷線**（頭號阻礙，今日 6+ 次，修法已明確）
- 🔴 **貼牆完整行程 229cm 未跑成**（起步瞬態中止）；貼牆只有 9 筆取樣
- 🔴 **起步靜摩擦未量測**（5 次嘗試全被 `vfd_start_fail` 擋掉）
- 🔴 **DSZL 量值刻度未校正** —— 貼牆後張力不對稱到 2.27 倍，這條的重要性提高
- 🟡 `FOLLOWER_ROLL_TOL_DEG` 2.0→1.0（Part B）仍未做
- 🟡 方向上限（`pay_out ≤ 30Hz`）的程式強制未做，目前靠人記得設

---

## 2026-09-01（續七）— 任務目標定案、四項修正實機驗證、以及一個尚未查明的連線卡死

### 🎯 任務目標（per user）：**地面↔頂端 10 趟來回，全程本體平衡（roll ±1°）**

**作業區間（per user 確認）**：
```
上限  L=0     頂端（今日歸零處）
下限  L=229   離地 88 cm —— per user「可以接受，是安全的」
單程  229 cm  ×  20 次橫越  =  45.8 m
```

🔴 **推翻「牆面長度 208 cm」**：per user 現場量測 `L=229` 時**離地 88 cm**
→ 頂端到地面實際 317 cm。稍早在 `L=208` 說的「離地一點點」換算其實是**離地約 109 cm**。
📌 這也讓早上那筆觀察前後一致了——當時就指出「張力只掉 10%、機器應該仍完全吊著」。
**不是計米器滑差，是目視估計偏樂觀。**

### ✅ 四項修正，全部有實機對照數據

以同一段 40 cm 反覆測試，逐項隔離效果：

| 趟 | 誤差來源 | 接近段 gate | `diff_tol` | 最大左右差 | 移動中最大 \|roll\| | 終點 roll |
|---|---|---|---|---|---|---|
| 1 | meter | 關閉（原行為） | 2 | 4 cm | 3.64° | −0.16° |
| 2 | meter | ✅ 修正 | 2 | 2 cm | 1.68° | 🔴 −1.63° |
| 3 | meter | ✅ | ✅ 1 | 3 cm | 1.89° | −0.76° |
| 4 | **imu** | ✅ | ✅ 1 | **1 cm** | **1.32°** | **−0.41°** |

**① 平衡 gate（`apply_balance_trim` 改為逐側 base_hz）**
原本呼叫端永遠傳 `g_vfd_motion_hz`，但 `motion_rope` 有**逐側獨立**的三段煞車
（`motion_hz` → `/2` @15cm → `fine_adjust_hz` @8cm）。減速後平衡一寫入就把速度拉回去
——所以當初（2026-07-23）的處置是**在接近段整個關掉平衡**。
🔴 代價實測：`pay_out 40cm` 的 0–25cm 左右差 ≤1cm（死區內，零筆 `[BAL]`），
**25–41cm 無平衡 → 差距長到 4cm、roll 3.6°**。偏差正是在那個空白窗口長出來的。
✅ 改傳各側**當前實際生效**的 base 後，gate 不必再關接近段。`[BAL]` log 實證：
`L=11 R=5`（右側已 half-slow）、`reset to base L=5 R=10`（逐側 reset）。
📌 **無平衡窗口是固定 15cm**：40cm 行程佔 37%，**229cm 只佔 6.5%**
——我們一直在用最不利的距離做測試。

**② `FINE_ADJUST_TOLERANCE_CM` 拆出左右差容許值**
同一個常數被用在兩件語意不同的事：①每側**位置**誤差（高度，±2cm 無妨）
②左右**差**（**姿態**，1cm ≈ 1°）。**又是「同一個數字兩種語意」**（同 08-31 減速遮罩）。
實例：`retract 40` 收在 `L=171 R=169`，`fine_adjust` 判定「diff=2 — within ±2」就不對齊
→ 終點 roll −1.63° 出規格。✅ 新增 `g_fine_adjust_diff_tol_cm`（預設 1）+ setter + status。

**③ IMU 驅動平衡的符號 —— 🔴 我寫錯並在實機上放大了偏差**
```cpp
err = g_imu_roll_deg.load();              // 🔴 錯：少了 direction
err = -(double)direction * g_imu_roll_deg.load();   // ✅ 正確
```
`direction` 是 +1(pay_out)/−1(retract)。**同樣的傾斜，放繩與收繩需要加速的是相反那側**：
收繩時左繩太長要**收快左側**；放繩時左繩太長要**放慢左側**。
🔴 **後果**：`pay_out 20cm` 期間 roll 由 −1.66° **單調惡化到 −5.18°**，被監看中止。
✅ 修正後同一個 `pay_out 20cm`：+1.39° → −1.71°（最大偏離）→ **+0.36° 收斂**，`OK` 完成。
`[BAL]` 顯示雙向修正（`err=-1.41→trim=-2.82` 與 `err=+1.85→trim=+3.70`）。
📌📌 **教訓：單一方向的測試不足以驗證符號。** 我第一次只測 retract，剛好是對的方向。
⚠️ 這正是計畫裡自己標的風險——「一個錯誤的 roll 值會主動把機器弄歪」，只是錯的是符號不是數值。

**④ `dual_vfd_sync_start` 加啟動驗證**
原本結尾直接 `HOLD_TRACE("EXIT OK (both running)")` —— **那句話從來沒被檢查過**，
只確認「寫入成功」。實例（13:12 @50Hz）：重試回報 `errL=0`、宣告 both running，
**左側馬達沒轉**；0.6 秒後左右差 7cm、roll 6.8°，靠 `length_diff_max_cm=5` 才中止。
✅ 改為 Phase B 後平行 `readStatusWord()` 驗 bit0 running，未通過走既有緊急路徑。
做法沿用同檔 `fine_adjust` 既有的 post-start 讀取（差別：那裡只印診斷，這裡當判準）。
🔴 **窗口 200→500ms**：首次實測就用掉 **191ms**，只差 9ms 就會誤判正常啟動。
✅ 實測不誤擋：`啟動驗證通過 (191ms, L+R running)`。
✅ 實測有效：`retract 229` 時 Phase A 就攔下，**機器完全沒動**（對比未修正時歪 6.8°）。

### 🔴🔴 未查明：SE3 連線會卡死，重啟才能恢復（今天第三次）

| 時間 | 裝置 | 證據 |
|---|---|---|
| 11:41 | DSZL `.32` | `Recv-Q=13`、`stale/foreign reply txid=3510 want=3511` |
| 13:12 | SE3 `@L` | 間歇 `writeParam comm fail`，重試回報成功但馬達沒轉 |
| 13:27 | SE3 `@R` | `Recv-Q=7`，keepalive 由 50/50 → 28/22 → **0/50**（~90 秒內劣化） |

**三個假設全被證據否掉**（過程記著，免得重走）：
- ❌ **USR 網關 `_pt=0` 幀分片**：`comm fail` 的定義是 `sendModbus` 收到 **0 bytes**；
  分片會走**另一條訊息** `bad reply len=%d`，而我們沒看到那一條。
  → 08-28「沒有證據前不動共用設定」的決策**在今天的證據下依然成立**，`_pt` 未動。
- ❌ **失步自我延續**：`TCP_client::sendAndReceive` **有 drain**，同一把鎖內、送出前非阻塞排空。
- ❌ **裝置變慢**：停程式後唯讀探測（`se3_latency.py`，新增）
  **兩側都 4.8–5.5ms、6/6 成功、回覆逐位元相同**。裝置/網關/網路全部健康。

**已量到的事實**：程式端每筆交易失敗、**日誌無任何 reconnect/disconnect**、socket 仍 `ESTAB`
且卡著一筆未讀回覆、重啟即恢復（右側 keepalive 立刻回到 `ok=10 fail=0`）。
🔴 **機制未知。** 這是 10 趟耐久測試的頭號阻礙——中途卡死意味著中止 + 重啟。

### ⚠️ 自記：我的兩個操作失誤

1. **重啟太快**：`5002` 關閉後立刻啟動，舊 process 還握著 X518 的連線（有連線數上限）
   → 新 process `dev_dsz=0`，而**旗標只在 init 設一次、本專案無 hot re-init**。
   runbook 自己警告過「印出 `[SHUTDOWN]` 不等於結束」，**我當天稍早還正確做過這個檢查**。
   → 之後一律等「`5002` 關閉 **且** 網關連線數歸零」才啟動。
2. **`pkill -f <字串>` 兩次殺到自己**：ssh 遠端命令列本身含該字串。改用 `pkill -x`。

### 待完成

- 🔴 **SE3/DSZL 連線卡死的機制**（頭號阻礙，三次重現，重啟是唯一 workaround）
- 🔴 **229 cm 全程從未成功跑過**（最長 40 cm）
- 🔴 **50 Hz 從未成功跑過**（唯一一次嘗試撞上 SE3 故障）
- 🔴 10 趟連續 + 完整軌跡證據（`retract_seg.py` 已加統計輸出：最大 roll／出帶比例／最大左右差）
- 🟡 `BALANCE_IMU_KP=2.0` / `deadband=0.5` 仍是未調的初值（只在 10 Hz、≤40cm 驗過）
- 🟡 Part B（`FOLLOWER_ROLL_TOL_DEG` 2.0→1.0）仍待上述驗證後再做

---

## 2026-09-01（續六）— IMU 驅動平衡迴路實作完成（階段 1 驗證通過，未上機驗證）

### 為什麼要做：離散步階這條路在原理上走不通

per user 規格是**移動全程** roll ±1°（帶寬 2°）。計米器路徑有兩道天花板，**都比帶寬大**：

| 天花板 | 量級 |
|---|---|
| **量化**：指令與計米器讀數都是整數 cm；balance deadband 1cm | **0.8–1.5°** |
| **鋼索彈性遲滯**：同樣繩長 168/169 實測出 roll **+0.21°** 與 **+1.90°** | **1.69°** |

📌 彈性這條原始碼早有記載（`cmd_side_measured`：「被拉伸的鋼索回彈，
**計米器量到的變化不全是捲筒轉動**」）——我今天量到的是同一件事的另一面。

🔴 **而 per user 補充「5–50 是可設定範圍，10–50 才是實際操作範圍」之後，
Part A（減速段）也失去效果**：`finish_hz` 只能訂 10，與 `roll_correct_hz` 相同 →
預設值下減速段等於停用。**降速消除過衝，但消除不了 1cm 的量化下限。**
（Part A 仍保留：把 `roll_correct_hz` 調到 30 做大角度修正時，收尾降回 10 有意義。）

### 設計：只換誤差來源，不換控制律

```
現在   err = progL − progR   （計米器差，cm）
改為   err = roll            （IMU 姿態，度）
```
對稱分配 / cap / `hz_min-max` / 250ms tick **全部沿用**——控制律今天實測有效
（30cm 段左右差全程 0–3cm），問題從來不在控制律。

🔴 **用「度」不換算成 cm**：換算會憑空造出假的長度，正是 08-31 減速遮罩那個缺陷的形狀
（同一個數字被當成兩種語意）。IMU 路徑有自己的 `kp`（Hz/度）與 `deadband`（度）。

### 吊機端（`Crane_control_PI/main.cpp`）

- `BalanceSource{Meter,Imu}` + `g_balance_source`，**預設 `Meter`＝與現行逐位元相同**
- `g_imu_roll_deg` + `g_imu_roll_stamp_ms`、`imu_roll_fresh()`
- 常數：`BALANCE_IMU_KP_DEFAULT=2.0`（Hz/度）、`BALANCE_IMU_DEADBAND_DEFAULT=0.5`（度）、
  `IMU_ROLL_STALE_MS=750`（3 tick）、`IMU_ROLL_SANITY_DEG=20`
- 指令：`set_imu_roll` / `set_balance_source` / `set_balance_imu_kp` / `set_balance_imu_deadband`
- `apply_balance_trim` 依來源選誤差；**IMU 過期一律退回計米器**並限流警告（2 秒一次）
- `status` 增 `balance_source` / `imu_roll` / `imu_roll_age_ms` / `imu_roll_fresh` /
  `balance_imu_kp` / `balance_imu_deadband`
- `[BAL]` log 補上 `src=` 與**單位隨來源變**（原本寫死 `cm`，IMU 模式會把度誤讀成公分）

### 本體端（`app/WASH_ROBOT.{h,cpp}`）

- 新增 `imu_push_loop_()` 執行緒 + `crane_cli_imu_` / `crane_imu_mtx_`
- 🔴 **獨立執行緒，不塞進 `imu_monitor_loop_`**——那是 45° 傾斜緊急停止的偵測迴圈，
  放網路 I/O 進去，網路一卡就延遲保護。**保護迴圈不可被非保護工作阻塞。**
- 🔴 **第三條連線**（照 `crane_cli_estop_` 的模式），不搶 `crane_mtx_`——
  `do_step_sync_` 期間主連線正阻塞在 `pay_out_*` 的回覆等待上。
  依據：`WASH_ROBOT.h:1201`「Shim is multi-connection (per-conn thread)」。
- 📌 **實作時偏離計畫一處**：原計畫「非移動中不推」，改成**一律推**。
  理由：資料永遠新鮮 → 動作一開始就能用，而過期機制就**只在真正的故障**
  （本體掛掉／網路斷）時觸發，那才是它該有的語意。成本每 250ms 一行短指令。
- 推送失敗**完全靜默且不重試**；讀掉回覆避免堆積
  （今天 DSZL 的 `Recv-Q=13` / `txid` 失步就是回覆沒讀乾淨造成的）。

### ✅ 階段 1 驗證（完全不動馬達）

| 項目 | 結果 |
|---|---|
| 資料流通 | `imu_roll=0.69` 與本體一致；`age_ms` 162/116/62 循環，**從未超過門檻** |
| 預設不改變行為 | `balance_source=meter` |
| 健全性上界 | `set_imu_roll 25` / `-25` → **`ERR roll_out_of_sanity_range`**；`1.5` → `OK` |
| 來源切換 | `garbage` → `ERR expected_meter_or_imu`；`imu` / `meter` → `OK` |
| **過期退回** | 停本體 → age **4345→7389ms 持續成長、`imu_roll_fresh=0`**；重啟 → 恢復 `fresh=1` |

### 🎯 順帶：Part 0（IMU 軸向）在啟動路徑上得到驗證

```
今天早上   [OK] IMU /dev/ttyUSB0 roll=-150.32   pitch=0.922852     ← 印的是 yaw
現在       [OK] IMU /dev/ttyUSB0 roll=0.692139  pitch=0.148315     ← 正確
```

### 🔴 這個方案不能解決什麼（已寫進原始碼註解）

**一側 VFD 沒啟動時調頻率一樣救不了**（今天 10cm 試走實測 `delta_L=0` 而回報 `err=0`）。
那條仍靠 `length_diff_max_cm=5` 的中止保護與 `fine_adjust` 事後對齊。
**兩套機制互補，IMU 平衡不取代繩長差保護。**

### ⚠️ 性質改變（原始碼註解已標明）

本檔原本明寫 *balance is nice-to-have, not load-bearing*（失敗只記 log）。
改成 IMU 驅動後**一個錯誤的 roll 值會主動把機器弄歪**。
因此過期退回與健全性上界**是本功能成立的前提，不是防呆加分項**——兩者都已實測。

### 待完成

- 🔴 **階段 2**：機器**放低**後，`balance_source=imu`、短距 retract、手動灌 roll，
  看 `[BAL] src=imu` 的 Hz 分配是否隨灌入值變化
- 🔴 **階段 3**：實跑並與**今天的基線**比對（第 1 段 30cm：左右差 0→3cm、roll −0.87→−3.07°）
- 🔴 `BALANCE_IMU_KP=2.0` / `deadband=0.5` 都是**未經實測的初值**，需在低處調
- 🟡 Part B（`FOLLOWER_ROLL_TOL_DEG` 2.0→1.0）仍待 Part A/IMU 平衡驗證後再做
- 🟡 `launch.sh` 每次啟動漏一個 `sleep` process（今天累積 3+1 個，已手動清）

---

## 2026-09-01（續五）— 🔴🔴 IMU 差動校平從 08-27 起就是個 no-op（實測證實並修復）

### 起因：規劃 `roll_correct` 減速段時，順手查「本體是否已有 IMU 閉迴路」

有——`do_sync_imu_roll_correct_()`，做的正是需要的事（讀平均 roll → 幾何換算 cm →
下 `roll_correct` → 沉澱 → 最多 3 輪）。**不必重造。** 但讀它的時候發現軸向不對。

### 🔴🔴 缺陷：兩支自動校平讀 `imu_.z`（偏航），卻減去 `imu_roll0_`（從 `imu_.x` 取的滾轉基準）

```
cmd_status 的 roll= / cmd_imu_zero / imu_monitor_loop_ / imu_take_baseline_  → imu_.x / imu_.y  ✅
follower_imu_level_        (wash_robot_commands.cpp:554,557,606)             → imu_.z          🔴
do_sync_imu_roll_correct_  (wash_robot_commands.cpp:642,645,685)             → imu_.z          🔴
init 的 [OK] IMU print     (WASH_ROBOT.cpp:313)                              → imu_.z          🔴
```

**實測證據（`cmd_status` 本身就有輸出，08-27 為了解決這類爭議加的）**：
```
ax=-0.00  ay=0.03  az=1.00        ← 重力全在 Z 軸 = IMU 水平安裝
raw_x=1.87  raw_y=0.12  raw_z=-151.05
```
`az=1.00` 證明現在是**水平安裝** → `imu_.x` 是尤拉滾轉、`imu_.z` 是磁力計航向角。

📌 **今天早上的啟動 log 就是證據，我當時沒抓到**：
`[OK] IMU /dev/ttyUSB0 roll=-150.32 pitch=0.922852` —— **−150 是 yaw，0.92 才是真正的 roll**。

### 🔴🔴 實際後果不是「修正方向錯」，是「從來沒有修正過」

```cpp
const double roll = read_roll_avg();                       // 讀到 -151.05
if (std::fabs(roll) > BAL_CAL_ROLL_PANIC_DEG /* 15.0 */) { // |−151| > 15 恆為真
    evt_("step_sync_imu_roll_panic ..."); return;          // non-fatal，印一行就 return
}
```
→ **每次都走 ROLL PANIC 分支直接放棄。** 而 `do_sync_imu_roll_correct_` 由
`do_step_sync_()`（**v2 正式走法**）呼叫，`wash_robot_commands.cpp:1893`。
**IMU 差動校平在活路徑上，從 2026-08-27 起就是個 no-op。**

### 🔗 這條線索把 08-04 那次調整也解釋了

```cpp
FOLLOWER_ROLL_TOL_DEG = 2.0
// 2026-08-04 per user: 1.0→2.0；2026-07-23: 0.5→1.0
// —— small tilts were triggering trim passes that then oscillated sign each pass
```
容許值曾經是 1.0°，因為**修正會來回震盪**而放寬到 2.0°。
震盪的兩個成因今天都量到了：① 最小步階 2.9–3.3° > 帶寬（見續四）；② 軸向錯誤。
**當時是治症狀（放寬容許值），沒治病因。** 而 `2.0` 直接牴觸 per user 的 ±1° 規格。

### ⚠️ 自記：我下錯結論一次，撤回後才用實測解決

第一次改完，殘留掃描翻出 `WASH_ROBOT.cpp:2785`「**實測** roll 改讀 yaw(`imu_.z`)
才會隨左右傾斜穩定變化」→ 我判斷自己的前提錯了，**`git checkout` 撤回**。
接著讀 `status` 的加速度三軸，`az=1.00` 才證明是水平安裝、原判斷成立，重新套用。

📌 **兩個教訓**：
1. **`grep` 單行看註解會被誤導** —— 2785 是 08-26 垂直安裝時代的敘述，
   **正確的那句（08-27 改回水平、用 x/y）就在下一行**。整段讀就不會誤判。
2. **撤回是對的** —— 當時我沒有證據，只有兩段互相矛盾的註解。
   正確做法就是撤回 → 量測 → 再改，而不是挑一段自己相信的註解。

### ✅ 已修（8 處）

| 檔案 | 內容 |
|---|---|
| `app/wash_robot_commands.cpp` | `follower_imu_level_` 3 處、`do_sync_imu_roll_correct_` 3 處：`imu_.z` → `imu_.x` |
| `app/WASH_ROBOT.cpp:313` | init print `roll=imu_.z pitch=imu_.x` → `roll=imu_.x pitch=imu_.y` |
| 註解 | 把實測證據（`az=1.00`／`raw_z=-151.05`／早上的 log）與 ROLL PANIC 後果寫進原始碼 |

⚠️ `raw_z=` 的診斷輸出保留（那本來就該印 z）。`2785` 的歷史敘述**不動**——它在上下文中是正確的。

✅ **建置驗證**：本體 16/16 TU + 連結成功、零警告零錯誤，新 binary 已就位
（`~/bringup/facade_cleaning_v2.out`，md5 `e6642bb3…`）。
⚠️ **執行中的仍是舊 process**（Linux 保留 inode）——**下次重啟才會生效**。

### 🔴 尚未做行為驗證

軸向修正的**行為驗證需要跑一次 `step_sync`**（那才會走到 `do_sync_imu_roll_correct_`），
而機器目前懸空。**未驗證，不宣稱已生效。**

### 待完成（Part A/B/C 尚未開始）

- 🔴 **Part A**：`roll_correct` 加減速段（`ROLL_CORRECT_APPROACH_CM` + `g_roll_finish_hz`
  + 短程直接慢速起步，照 `cmd_side_measured` 08-31 的教訓）
- 🔴 **Part B**：`FOLLOWER_ROLL_TOL_DEG` 2.0 → 1.0（**必須在 Part A 之後**，否則重演 08-04 震盪）
- 🔴 **Part C**：`FOLLOWER_SPAN_CM = 100.0` 是 PLACEHOLDER，需實測校正
- 🔴 Part 0 的行為驗證（需 `step_sync`，機器要在可作業位置）

---

## 2026-09-01（續四）— 實機總測試：三個修正在現場得到證實，並挖出兩件會擋住 ±1° 的事

### 🔴🔴 現況（下次接手先讀這條）

**機器懸空停在 `L=168 R=169`（per user 指示留在此位置）**，`roll=+0.21°`、`state=idle`、
`tension_valid=1`、VFD keepalive `fail=0`、無漂移。兩台程式與 GUI 均在執行中。
**牆面總長度實測 208 cm**（頂樓歸零 → 落到離地一點點）。**尚未收繩回 0 位置**，剩約 168 cm。

### ✅ 三個近日修正在現場得到證實

| 修正 | 現場證據 |
|---|---|
| SE3 型號字串（08-31） | 吊機 init 印 `VFD left/right (SE3)`，非寫死的 `MH300` |
| `CRANE_IP` 開機自動選路（08-31） | `有線 192.168.1.10 探測不通（300ms）→ 走 WiFi 192.168.5.25` → `[OK] crane`。**有線確實斷**（另測 `.1.100` 也不通）；沒有這個修正會卡滿 TCP SYN timeout 兩分鐘 |
| 深度相機移除（09-01） | 本體 init **一行 `depth_cam` 都沒有** |
| `LOG_ERR` 脫離 `debug_mode`（08-31） | **見下方 DSZL 故障** —— 這次直接靠它定位根因 |
| PQW 存在性探測（08-31） | 吊機 init `[ERR] PQW init presence probe failed`，正確反映模組實體拔除 |

### 🔴 DSZL 張力兩側同時 ERR —— 根因查明，且推翻「裝置故障」的第一印象

**症狀**：`tension_left=ERR tension_right=ERR tension_valid=0`，而 `dev_dsz_*` 仍是 1。
**若在此狀態下驅動馬達，過載保護與收繩軟停會靜默失效**：
```cpp
static std::string tension_safety_check(double& l, double& r) {
    if (read_tensions(l, r)) return "";   // 讀取失敗 = 回傳「無警報」
```
→ **已據此中止原定計畫，未在無保護下動馬達。**

🔴 **根因是 TCP 連線上的協議失步**，不是裝置壞掉：
```
ESTAB  Recv-Q=13  192.168.1.10:36324 → 192.168.1.32:502   ← 13 bytes 卡在接收佇列
```
遲到的回覆留在緩衝區 → 之後每次讀取都拿到上一筆 → slave/CRC 不符 → 全數被拒。

🔴 **`502 Connection refused` 是誤導**：X518 **ping 通、ARP 正常**（MAC `00:08:dc:11:11:20/21`）。
拒絕連線是因為**吊機程式已佔滿 X518 的連線數上限**——早上探測能連，正是因為當時程式沒跑。
**關掉程式後 502 立刻恢復 OPEN，探測讀值 25.47/13.42 與故障前一致。**

✅ **workaround 生效**（待辦表既有記載：「根因未知，workaround 是重開 crane 程式」）：
正規關機（`exit` 走 console，非 `kill`）→ 連線釋放 → 重啟 → `tension_valid=1`。
📌 **SD76 計米值 208/209 完好保留**（計數在裝置端，重啟不影響）。
⚠️ **`set_motion_hz` 等 runtime 設定會遺失，重啟後必須重設。**

📌 **`LOG_ERR` 那個修改在這裡是決定性的**：`[ERR] [DSZL:1] get_tension_kg: consecutive errors
reached threshold` + 限流抑制訊息。沒有它只會看到一個沒有理由的 `tension=ERR`
——正是待辦「crane 端偶發 read_fail，根因未知」當初查不下去的原因。
🟡 但標籤是 `[DSZL:1]`，**左右 slave 都是 1，這條路徑分不出哪一側**（08-31 補的 `@L`/`@R` 沒涵蓋 `get_tension_kg`）。

### 🔴🔴 10 cm 試走：左側 VFD 靜默沒啟動（08-31 那個缺陷的新形態）

```
[sync_start] Phase A setFreq err=0 / Phase B run err=0        ← 回報兩側都在跑
[motion_rope] sync stop (leader=R) delta_L=0 delta_R=-10      ← 左側整個主迴圈沒動
[ERR] [SE3:1@L] writeParam reg=0x1001 val=0x0002 comm fail    ← 真正的錯誤此時才浮出
[fine_adjust] align-to-leader ... final L=198 R=198 — done    ← 靠收尾對齊救回來
```
**這是 08-31「VFD 寫入間歇失敗，會吃掉減速命令」的同一個缺陷，這次被吃掉的是啟動命令。**

🔴 **修正一條先前的記載**：平衡機制在這個故障模式下**無效**。
| 情境 | 長度差平衡控制器 |
|---|---|
| 一側**比較慢** | ✅ 有效（調頻率拉快落後側） |
| 一側**根本沒啟動** | 🔴 **無效** —— 調一個沒在轉的馬達的頻率沒有意義 |
救回來的是主迴圈**之後**的 `fine_adjust` align-to-leader，那是收尾對齊、不是即時平衡。
且 `length_diff_max_cm=15` 沒觸發（差 10 < 15），**只差 5 cm 就會中止**。

### 🎯 幾何標定（本日最有用的量測）：**0.8°/cm，繩距約 80 cm**

三個觀測點反推：
| L | R | R−L | roll |
|---|---|---|---|
| 208 | 209 | +1 | +0.09° |
| 198 | 198 | 0 | −0.87° |
| 170 | 167 | −3 | −3.12° |

**換算出來的保護門檻真實含意**：
| 機制 | 數值 | **對應傾斜** |
|---|---|---|
| 平衡控制器 deadband | 1 cm | **0.8°** |
| 韌體 `length_diff_max_cm` | 15 cm | **≈ 11°** |
| IMU 緊急停止 | 45° | **需 60 cm 繩差** |

🔴🔴 **兩個結論**：
① **在這個幾何下，IMU 的 45° 緊急停止實質上永遠不會先於繩長差保護觸發** —— 它不是第二道防線。
② **`length_diff_max_cm=15` 允許機體歪到 11 度**。該門檻 08-31 訂下時就標了 🟡「待確認」，
   **現在有換算依據了：若要守 ±1°，它應該是 1.25 cm 而不是 15 cm。**

### ✅ `roll_correct` 實測有效，正負號確認

`-delta = 左收右放 → roll 往正`（原始碼註解如此，實測相符）。
`roll_correct -1` → L 170→168、R 167→169（R−L=+1）→ **roll −3.12° → +0.20°**，
預測值 +0.09°，**模型準確**。
📌 **順帶第二次佐證「小增量會走兩倍」**：下 `-1`，兩側各實走 **2 cm**
——與早上 `pay_out_right 1` 回報 `moved=2cm` 同一現象。待辦表那條「單位換算未經驗證」現在有兩筆證據。

### 🔴🔴 per user：±1° 是**移動全程**的要求 → 目前控制守不住

```
預算            ±1°  = ±1.25 cm 繩長差
控制器 deadband  1 cm = 0.8°          ← 死區就吃掉 80% 預算
第 1 段實測      3 cm = 2.4°          ← 規格的 2.4 倍（且該段兩側都正常啟動、非故障）
```
死區之外還有整條迴路延遲（tick 250ms → 調頻 → SE3 寫入 → 馬達反應 → 繩長改變）。
🔴 **即使 deadband 設 0，延遲鏈仍會產生殘差。這需要調校甚至改控制律，不是改 config 能解決。**
🔴 **且不該在機器懸空時試參數** —— 每次試都是一次真實升降。
→ **依此中止收繩，機器留在 168/169（per user）。**

### 📌 per user 提問：兩份 config（測試區／戶外）—— 機制已存在，但有兩個缺口

`common/profile.h` 的路徑解析**已支援每份 profile 各自用環境變數指向不同檔案**：
```cpp
std::string var = "FCV_PROFILE_" + which;   // 例: FCV_PROFILE_axis_profile
std::string path = getenv(var) ?: ("config/" + which + ".txt");
```

🔴 **但切分依據應是「變更理由」而非「地點」**（`profile.h` 檔頭自己訂的規則）：
`axis_profile` 跟著**機器**走、`device_profile` 跟著**設備型號**走。
導程 7.731 是機器性質，**換場地不會變** → 複製兩份 `axis_profile` 等於製造兩個會不同步的真相來源。
→ 應新增第三份 **`site_profile`**（跟著**場地**走）。

**缺口**：
- 🔴 **吊機端完全沒有接 profile**（`Crane_control_PI/main.cpp` 零個 `profile::`），而平衡參數全在吊機
- 🔴 **安全互鎖不可進 config**（`profile.h` 明訂）：`length_diff_max_cm` / `tension_max_kg` /
  `IMU_EMERGENCY_DEG` 留在程式碼；只有 `balance_kp` / `deadband` / `cap_ratio` / 各種 hz 屬調校
- ⚠️ **既存不一致**：`length_diff_max_cm` 與 `tension_max_kg` **目前已可用 `set_*` 執行期修改**，
  這已違反「安全互鎖留在程式碼當斷言」那條規則。要不要收，是另一個決定。

### ✅ Web GUI 部署（順帶修掉兩個會讓它連不上的錯）

Pi 上 `~/projects/web_ver2/` 是舊版，兩個 IP 都錯：`CRANE_IP=192.168.1.101`（過期，repo 早已修為 `.1.10`）、
`WASHROBOT_IP=192.168.1.100`（**實測不通**，有線網路是斷的）。
→ 備份舊版（`server.js.bak-20260901`、`public.bak-20260901`）、部署 repo 版本，
以 `WROBOT_IP=192.168.5.26 CRANE_IP=127.0.0.1` 啟動。
✅ **端到端控制驗證**：瀏覽器 WebSocket → server.js → 兩支 C++ 程式，皆有回覆。
→ 待辦表「Pi 上 web_ver2 落後 repo 一個 commit」**可結案**。

### ⚠️ 工具面自記

- **`pkill -f <字串>` 兩次殺到自己**：ssh 遠端命令列本身含有該字串 → 連線被砍、回 255。
  遠端找 process 不要用會匹配到自己命令列的 pattern。
- **讀吊機回覆不能只讀第一行**：EVT 進度廣播與最終 OK/ERR 走同一條連線，
  第一次 `retract 10` 只讀到 `EVT motion_progress` 就關連線。已改為讀到 `OK`/`ERR` 開頭才停。
- 新增 `retract_seg.py`（scratchpad → Pi）：並行監看 roll 與左右位移差，超標主動下 `stop`。
  本日第 1 段即由它中止（roll −3.07° > 3.0°）。

### 待完成

- 🔴 **收繩回 0 位置尚未完成**（剩 168 cm）。先解決 ±1° 全程的控制問題再繼續。
- 🔴 **±1° 全程可達性未知**：需在低處做「差距 vs 時間」響應量測（不同 kp/deadband），
  找出殘差主導項是死區、增益、還是迴路延遲。**不要在懸空時試參數。**
- 🔴 **`length_diff_max_cm=15` 應依 0.8°/cm 重新訂**（守 ±1° 需 1.25 cm）
- 🟡 `site_profile` 設計 + 吊機接 profile + 參數/保護切分清單（計畫已在上方，待實作核准）
- 🟡 SE3 VFD 寫入間歇失敗（本日重現，08-31 既有 🔴🔴）
- 🟡 DSZL log 補 `@L`/`@R` 到 `get_tension_kg` 路徑
- 🟡 X518 連線數上限的實際值未量（本日只知「程式佔用時外部無法連」）

---

## 2026-09-01（續三）— 硬體盤點交付；兩項 per user 更正

### 交付：由原始碼盤出的硬體清單

權威來源是 `app/WASH_ROBOT.h` 與 `Crane_control_PI/main.cpp:171-202` 的常數，不是文件轉述。
**計算主機 3 + 通訊基礎設施 7 + 本體 16 + 吊機 7 = 33 個裝置**（攝影機不計，見下）。

### ✅ per user 兩項更正（已寫進 `CLAUDE.md`）

**① 五台 USR 網關全部都是 `USR-TCP232-304`，後台帳密一律 `admin` / `admin`。**
先前只有 `.20` / `.22` 經 08-28 網頁後台實查有型號記錄，**吊機側的 `.30` / `.31` / `.34`
從來沒記過型號** —— 現已補齊，並加了一張五台網關的對照表（位址 ↔ 掛什麼）。
📌 `.20`/`.22` 的詳細設定值仍標為「實查」，其餘三台標為「推定相同，未逐台實查」，
**不要把推定寫成實測**。

**② 攝影機 ×4 不列入本版架構**（per user「這個版本不用」）。
原記「PoE 防水 2MP ×4（左上/左下/右上/右下），`frame_capture/` 走 RTSP，cam1 `.110`／cam2 `.111`」
→ 改為明確標註「實體或許還掛在 switch 上，但**本版程式不使用**」：
2D 相機路線 2026-08-27 已作廢（`wr.sh` 的 cam1/cam2 window 早已註解）、
D435i 深度相機 2026-09-01 整套移除。**盤點硬體時不要再算進來。**

### 🟡 仍未處理：2D 相機的程式碼殘留

界線目前劃在深度相機。2D 那套還留著：
- `obstacle_detect_enabled_` **仍出現在 `cmd_status` 輸出**（`obstacle_detect=off`，永遠 off
  ——setter `cmd_obstacle_detect` 無實作、dispatcher 回 `ERR removed_in_v2`）
  🔴 **動它會改變 `status` 字串，可能影響前端解析** ← 這是當初劃界的理由
- `frame_capture/` 的 `obstacle_combine.py` / `obstacle_detector.py` / `obstacle_monitor.py` /
  `frame_capture.py` / `auto_calibrate_lut.py` / `bench_capture_motion.sh` 六個檔
- `scripts/wr.sh` 的 cam1/cam2 window（已註解，但變數與 URL 仍在）

📌 既然使用者已明確表示「這個版本不用」，**這批可以照深度相機的做法一併拔除**，
但 `status` 欄位那條要先確認前端有沒有在解析。

### 三個決定架構的硬體事實（盤點時再次確認）

1. **真空是單閥單泵**：一顆繼電器控 4 顆吸盤 → 交替步伐架構上不可用（08-31 已停用）
2. **兩軸可觀測性天差地遠**：DM2J 上滑台開迴路**無回授**（失步軟體偵測不到，只能拿尺）；
   ZDT 推桿有編碼器（`real_pos`/`pos_error`/`stall`）
3. **移動不是機器自己走，是吊機收放繩**：風扇壓住貼玻璃、四輪頂住，吸盤只在定點當錨

---

## 2026-09-01（續二）— 架構盤點：用 include 圖掃出一個反向依賴，`log_utils.h` 搬家

### 起因：per user「確認一下目前的程式架構長什麼樣子」

📌 **沒有照抄 `CLAUDE.md`，而是用 `#include` 圖實測「上層呼叫下層，下層不認識上層」這個宣稱。**
分層是可以被機器驗證的，而這次驗出東西。

### 🔴 查到一個真實的反向依賴

```
transport/TCP_client.cpp   ┐
transport/TCP_server.cpp   ├─→  #include "log_utils.h"   （在 user_lib/）
transport/Serial_port.cpp  ┘
```
**最底層的傳輸層反向依賴驅動層。** 但根因不是 `transport` 寫錯——是 **`log_utils.h` 放錯地方**：
它是「統一 log 格式」的橫切基礎設施（18 個使用者：`user_lib` 15、`transport` 3），
**不是裝置驅動**，而 `CLAUDE.md` 白紙黑字寫著「🔴 `user_lib/` 只放裝置驅動」。

⚠️ **這個破口是 08-31 我自己擴大的**：`LOG_ERR` 脫離 `debug_mode` 那次改的正是這個檔
（一處生效於 15 支 driver），當時**沒注意到 `transport` 也在用它**。

### ✅ 已處理：`user_lib/log_utils.h` → `common/log_utils.h`

- `git mv`（git 認得是 rename，歷史不斷）
- include guard `USER_LIB_LOG_UTILS_H` → `COMMON_LOG_UTILS_H`
- 檔頭補上搬家理由與使用者分佈
- **18 個 `#include` 一個都不用改** —— 全是裸寫 `#include "log_utils.h"`，靠 `-I` 解析
- ✅ 重掃 include 圖：**反向依賴歸零**

### 🔴🔴 差點漏掉的坑：兩套建置路徑不一致

`CLAUDE.md` 自己警告過「移動檔案要同步 `AdditionalIncludeDirectories`，只改 `ClInclude` 不夠」，
而這次正好踩到：

| 專案 | 搬家前的 include 路徑 | 用 log_utils？ |
|---|---|---|
| `facade_cleaning_v2` | `..\app;..\command;..\common;...` ✅ 有 common | 是 |
| `Crane_control_PI` | `..\common;..\transport;..\user_lib` ✅ 有 common | 是 |
| **`Linux_test`** | `..\transport;..\user_lib` 🔴 **沒有 common** | **是** |

🔴 **`g++` 那三條命令列全都有 `-Icommon`，所以照編過；但 VS 遠端建置會找不到標頭。**
→ 已補 `..\common` 進 `Linux_test.vcxproj`，三個 `ClInclude` 路徑也一併更新。
📌 **教訓：這個專案有兩套建置路徑（g++ 命令列 / VS vcxproj），只驗一套等於沒驗。**

### ✅ 建置驗證（三個目標全跑）

| 目標 | 結果 |
|---|---|
| 本體 `facade_cleaning_v2` | ✅ 16/16 TU + 連結，1,018,664 bytes |
| `Linux_test`（**正是會踩到的那個**） | ✅ 連結成功，667,176 bytes |
| 吊機 `Crane_control_PI` | ✅ 連結成功，496,664 bytes |

產物全部清除，兩台 Pi 無殘留 process。

### 🟡 另兩件已記進 CLAUDE.md（未處理）

- **機構層目前是懸空的**：`rope_axis.h` **只有 `Crane_control_PI` 在用，`app/` 完全不碰**
  → 分層圖畫的 `app → mechanism → user_lib` **那條鏈現在是斷的**，`app/` 直接打 `user_lib`。
  對本體合理（沒有繩軸），但圖會讓人以為鏈是活的。已在 `CLAUDE.md` 就地註明。
- **`CLAUDE.md` 記的 vcxproj 設定已過期**（原寫 `..\app;..\user_lib`，實際四個專案都不同）
  → 已改為四專案對照表。

### 📌 架構現況數字（2026-09-01 實測）

`app/` **12,016 行、佔全專案 C++ 約四成**，下面每層都是千行等級。
08-30 重構把 `main.cpp` 由 522 → 149 行、抽出 `command/`（417 行）與 `mechanism/`（69 行），
**但重量仍在任務層**。`Crane_control_PI/main.cpp` 也還是 4,707 行單檔（應用層尚未抽出）。
本體 16 個 TU、吊機 10 個、`user_lib` 16 支驅動。

---

## 2026-09-01（續）— 深度相機連根拔除（約 470 行 + 3 支 Python），並清掉兩處死碼

### per user：「深度相機殘留，沒有要用，移除程式碼跟待辦」→ 選 B（連根拔除）

📌 **先盤點才動刀，結果範圍遠大於待辦表所記的兩條**（待辦只記了「主程式仍在探測」
與「`wr.sh` 仍啟動它」）。實際殘留橫跨 C++／腳本／Python／harness／前端註解。

### 已移除

| 檔案 | 內容 |
|---|---|
| `app/WASH_ROBOT.cpp` | −339 行：`init()` 的 `depth_cli_` 連線、`depth_cam_cmd_()`、`cmd_run_depth_avoid()`＋`_continue`＋`_stop` |
| `app/WASH_ROBOT.h` | −130 行：8 個 `DEPTH_*` 常數、5 個 `depth_last_*`、`depth_cli_`/`depth_mtx_`、`DEPTH_CAM_IP/PORT`、3 個方法宣告 |
| 同上（連帶） | **v1 攝影機避障的死宣告**：`cmd_run_avoid` / `cmd_obstacle_check` / `cmd_obstacle_response` **三者皆無實作**，dispatcher 早就硬回 `ERR removed_in_v2`；連同因此孤立的 `obstacle_ask_pending_` / `obstacle_user_response_` / `OBSTACLE_ASK_TIMEOUT_S` |
| `command/dispatcher.cpp` | 3 個指令改回 `ERR removed_2026_09` |
| `scripts/wr.sh` | `WR_DEPTH_CAM` 變數／存在性檢查／depth window／usage |
| `harness/run_trace.sh` | `depthcam` 假端點（:15530）＋ `FCV_EP_DEPTHCAM_*` |
| `frame_capture/` | **git rm 3 支**：`depth_cam_service.py`、`depth_cam_test_client.py`、`depth_reflection_bench.py`（84KB） |

📌 **dispatcher 刻意保留為明確拒絕，而不是刪掉指令** —— 舊 GUI 或存檔的 `run_script`
會拿到 `ERR removed_2026_09`，而不是「不認得這個指令」。**回錯誤比不認得更可診斷**
（同 2026-08-31 停用交替步伐時的處置）。

### 🔴 順帶更正兩段「會誤導的註解」

`web_backend` 裡有兩段寫著「**後端 `run_depth_avoid` 指令仍在，需要時可用 raw command**」，
其中 `app.js:1209` 那段還警告「若之後有人用 raw command 直接發 `run_depth_avoid`，
**機器仍可能自己跨障礙物**，而前端已經不會顯示提示了」。
→ 兩段都已隨本次移除更正。**這類註解不更新就是主動誤導**，比沒有註解更糟。

### ⚠️ 自記：我在 hook 那件事上下錯過結論，被呼叫端打臉

我先記「`do_step_down_` 的四個 hook 參數全是死參數」。查呼叫端發現**是錯的**：
`wash_robot_commands.cpp:2717/2900`（與 `do_step_up_` 的 2646/2815）**確實傳入非空的
feet hook**。正確的圖像是三層，而我只看到最淺的一層：

1. **只有兩個 body hook** 是 `cmd_run_avoid` 專屬，所有呼叫端一律傳 `{}` → 真死參數
2. **兩個 feet hook 有人傳**（`cmd_step_*_sweep_after_feet` / `_before_after`），
   但 v2 的 `do_step_*_` 內部把它們 `(void)` 掉 → **「傳了不會觸發」，不是「沒人傳」**
3. 而這件事**目前被更上游的問題蓋住**：兩支 `do_step_*_` 開頭即
   `return "ERR alt_gait_disabled_single_valve"`（08-31 停用交替步伐）
   → `(void)` 那幾行根本在 return 之下＝**死碼中的死碼**，整條路徑到不了

📌 **教訓**：判斷「參數是不是死的」不能只看函式內部有沒有用到，**必須同時看呼叫端有沒有傳**。
兩者答案不同時，那個落差本身就是缺陷（傳了不會生效，而且沒有任何訊息說明）。

### ✅ 兩處死碼已處理（per user「開始處理」）

- **`last_step_planned_cm_` / `last_step_achieved_cm_` 已移除**：全 repo 掃描確認
  **只有宣告，無讀者也無寫者**。原註解宣稱「`do_step_down_` writes after Phase A complete」
  ——**實際從來沒有寫過**，唯一的讀者 `cmd_run_avoid` 也早就沒有實作。
- **`during_body_rail_hook` / `after_body_rail_hook` 已從 `do_step_down_` 移除**
  （宣告 + 實作 + 3 個呼叫端的 `{}`）。**副作用是好的：`do_step_down_` 與 `do_step_up_`
  的簽名現在一致**（先前 down 比 up 多兩個參數，正是這兩個）。
- 🟡 **feet hook 刻意保留**：它們有真實呼叫端，拔掉會改動 sweep 分段的介面。
  已在標頭註明「傳了不會觸發」與「恢復 sweep 分段時要先真的接回去」。

### ✅ 建置驗證（看產物，不看管線離開碼）

| 目標 | 結果 |
|---|---|
| 本體 16/16 TU + 連結 | ✅ 零警告零錯誤，`fcv_depthrm.out` 1,018,720 bytes |
| **連結成功的意義** | **無 undefined reference ＝ 沒有殘留呼叫指向已刪函式** |
| `Linux_test` | ✅ 通過（runbook 特別警告前兩條建置指令蓋不到它） |
| 移除 hook 後重建 | ✅ 16/16 + 連結，`fcv_h.out` 1,018,664 bytes |
| 吊機端 | ✅ 稍早已驗（DSZL 註解那輪） |

驗證用產物全部清除；`5001`／`8080` 未監聽，兩台 Pi 無殘留 process。

### 📌 界線：只拔深度相機，不碰 2D 攝影機那套

`obstacle_detect_enabled_` **仍在 `cmd_status` 輸出裡**（`obstacle_detect=off`），
屬於另一個已退休的功能（192.168.1.112/113 + YOLOv8/Hailo）。動它會改變 `status` 字串、
可能影響前端解析 → **未動，另記**。同理 `frame_capture/` 的 `obstacle_*.py` 四支保留。

### ⚠️ 有意的行為改變，不是回歸

harness 少了 `depthcam` 假端點 → **等價比對的軌跡基線會與 2026-08-30 那份不同**。
日後要再跑等價比對，**基線必須重取**，不要把這個差異讀成回歸。

### 📌 文件處置：會誤導的改掉，歷史紀錄留著

- `ONBOARDING.md` §8（深度相機整節）**加移除標頭但不刪節** —— 裡面
  「**公式邏輯對不代表數字準，常數本身是實體量測值，必須跟現場皮尺交叉驗證**」
  這條方法論教訓在本專案已重複應驗（08-28 上滑台導程 7.7 倍、08-31 推桿 `CUP_PULSE_PER_CM`）。
  標頭明寫「不要照著本節的檔名或函式名去找程式碼，它們都不在了」。
- `runbook.md`「尚未決定的一項」已結案；指令表該列劃掉。
- `.claude/changelog.md`（68 處）與本檔的歷史條目**刻意不動** —— 那是歷史，不是現況。

### ⏸ 新架構待辦表整張暫緩（per user）

四條 🔴 安全項（硬體看門狗／ESC 電壓版本／漏電保護／螺旋槳防護）標為 **⏸ 暫緩**。
🔴 **優先度刻意維持 🔴、沒有降級** —— 「暫緩」是「現在不做」，不是「風險降低了」。
**恢復條件已寫進表頭**：新架構開始實體製作時，這四條必須在通電前處理完。

### ✅ 待辦表校準（2026-09-01，per user）—— 🔴 由 3 降為 2

**5 列結案**，其中 1 列是**記載本身錯誤**，4 列是**優先度欄沒跟上自己的現況欄**。

🔴 **「上滑台 RPM 常數」那列有兩處錯**（不只是優先度沒改）：
- ① 它警告「`ARM_SWEEP_RPM=1000` 幾乎確定過快」，但 `WASH_ROBOT.h:752` **在寫下該列的同一天
  （08-28）就已 per user 改成 250**、`DM2J_ARM_STEP_SWEEP_RPM` 更早（07-27）就是 250
  → **「現況：未修」是錯的記載**，不是待辦。
- ② 剩餘部分（250 要重新評估／ACC-DEC）已於 **08-31 per user 拍板否決**。
- 📌 **留著的後果是具體的**：下次開工掃待辦會看到一條紅字寫「RPM 常數全錯、未修」，
  於是**再提一次已經被否決的建議** —— 這正是這張表設計來防的事。

**另 4 列**（現況欄自己已寫 ✅ 已修，只有優先度欄沒改，皆 08-29 修的）：
`tension_safety_check_values` 註解／`trigger_sync_move()` 廣播永遠回報失敗／
`CLV900_inverter` 缺 null-client 防護／4 個 `.vcxproj.user` 被 git 追蹤。

📌 **這張表 08-29 才校準過一次（當時落後 7 列），今天又抓到 5 列。**
**結構性原因：修東西的人不一定回頭改優先度欄。** 現況欄寫得很勤，優先度欄會漂。
→ 值得每次開工用一支掃描比對「優先度 vs 現況欄」，而不是靠人記得。

**校準後：🔴 2（皆為 DSZL 量值，同一件事）／🟡 33／🟢 25／✅ 31。**

### 待完成

- 🟡 **本次全部改動尚未 commit**（15 檔修改 + 3 檔刪除 + 1 新檔 `dszl_sign_test.py`）
- 🟡 `obstacle_detect_enabled_` 與 `frame_capture/obstacle_*.py`（2D 攝影機路線）未處理
- 🟡 feet hook「傳了不會觸發」的落差仍在（等交替步伐或 sweep 分段要恢復時一併處理）
- 🟡 等價比對基線需重取

---

## 2026-09-01 — DSZL-107 正負號兩側實測結案（🔴🔴 降 🔴），量值仍卡在沒有已知重量

### 背景：使用者問「`main` 那邊是不是有校正好的值可以直接用」

**沒有。七個分支全部都是 `-0.01`**（`main`／`origin/main`／`refactor/app-layer`／`fix/msg-nosignal`／
`main-final-harness`／`docs/single-dev-restructure`／`fix/driver-crc`）。全歷史只有兩個 commit 碰過它
（`4d1409c` 引入、`2b16601` 改註解），**沒有一次是校正**。

🔴 **而且 scale 目前沒有任何持久化路徑**：`main.cpp` 的註解寫「scale is driver-local, doesn't need
save_params」，它只活在 `std::atomic` 裡，唯一寫入端是 GUI 的 `set_dsz_scale`
（`web_backend/public/app.js:1823`）→ **重開機就回到 `-0.01`，且不會有任何訊息**。
對照組：**零點是有持久化的**（`do_zero_*` 寫 X518 RAM 後 `save_params()` 進 EEPROM）。
📌 **所以將來量到量值之後，「值要放哪裡」必須先決定**，否則量完隔天就沒了。

### 🔴 「反推」不能用來結刻度那條待辦（方法論）

使用者原本要求「用反推估值結案」。**反推拿到的是 raw，不是 scale**：`raw = kg ÷ scale`，
而我們正是用待驗證的 `-0.01` 去反推 → 跟原本的 kg 是同一筆測量的兩種寫法，**沒有新資訊**。
scale 是「raw 計數 → kg」的橋，**沒有任何參考力就架不起來**，這是資訊上的限制不是工夫問題。
→ 量值只能以「宣告為估計值」的形式結案，**不能宣稱校正過**。

### ✅ 但正負號不需要已知重量 —— 只需要力的「方向」

📌 **這是本次的關鍵拆解**：量值需要力的**大小**，正負號只需要**方向**。
兩者卡在不同的東西上，**混在一起看就會以為整條待辦都得等砝碼**。

**而正負號才是危險的那一半**（`tension_safety_check_values` 實測行為，比舊記載精確）：
```cpp
if (l_kg > max_kg) return "high_left";      // 只檢查過高
if (r_kg > max_kg) return "high_right";     // 只檢查過高
if (fabs(l_kg - r_kg) > diff_kg) return "diff";
```
若右側極性相反、負載時 `r_kg` 變大負值：
- `r_kg > max_kg` **永遠不觸發** → 右側過載保護實質死掉
- `fabs(l_kg - r_kg)` 在兩側都受力時會**意外**擋下來，但理由是 `diff`（傾斜）不是 `high_right`
- **只有右繩單獨受力時**（`l≈0, r≈−50`，`|50| > 50` 為偽）→ **一條都不觸發，完全沒有保護**

### 工具：`Linux_test/dszl_sign_test.py`（唯讀，新增）

基於版控既有的 `Linux_test/x518_probe.py`（本來就唯讀且已會讀單位暫存器）改成取樣版。
🔴 **結構上只實作 FC03，沒有任何寫入路徑** → 不可能歸零、改單位或動到 X518 flash。
純 Python 免編譯。左 `192.168.1.32:502`／右 `192.168.1.33:502`，slave 皆 1。

### ✅ 實測結果（機器在地上、手動施力、每組 25 筆 @200ms）

| 階段 | left_raw | right_raw |
|---|---|---|
| 基線（不碰） | **−1.2**（spread 1） | **67.0**（spread 0） |
| 拉左繩 | **−402.8**（Δ **−401.6**） | 81.9（串音 +14.9） |
| 拉右繩 | −78.6（串音 −77.4） | **−235.6**（Δ **−302.6**） |
| 放開（負向對照） | **11.8** | **48.2** |

🎯 **兩側同為負 → `right untested but assumed same wiring` 的假設是對的，但現在是量測值。**
訊號量 300～400 counts vs 基線雜訊 spread 1～2，**差兩個數量級，無模稜空間**。
✅ **負向對照通過**：放開後兩側都回基線附近（殘留 +13.0／−18.8，約 3.2%／6.2%，屬遲滯或落地位置差異）
→ 讀值**雙向跟隨施力**，不是單向鎖存。
→ **`r_kg > max_kg` 的右側過載保護確認會觸發，先前擔心的靜默失效不存在。**

📌 **順帶解掉一個疑慮**：右側基線 25 筆全是 67、**spread=0**，一度懷疑是凍結的舊值；
施力時它會動（spread 也由 0 變 1）→ **是活的，只是非常安靜**。

⚠️ **串音不列為發現，只列為觀察**：拉左時右為 **+14.9**、拉右時左為 **−77.4**，
兩次方向不一致。手拉的力道與幾何都沒控制，**不足以下結論**。排平衡控制時值得回頭做受控版本。

### 🔴 順帶挖到：X518 單位暫存器會靜默改變讀值的尺度

`DSZL_107.h:130`：**單位暫存器 `0x0614`，`1=t 2=kg 3=g 4=kN 5=N 6=lb`，出廠預設 `5=N`**。
吊機 `init()` 呼叫 `set_unit_kg()`，但 **`set_unit` 只寫 RAM**，而 `main.cpp` 明寫
「We deliberately do NOT call save_params() here」。
→ **X518 一斷電就退回 flash 的值；此時若沒重跑吊機程式才去讀，數字會差約 9.8 倍而毫無徵兆。**
✅ 本次實測兩側皆 `unit=2 (kg)`，狀態正確。已寫進常數註解。

### 📌 更正一個我自己稍早的反推

我依 08-29 的 kg 反推 raw 應為 **−2645／−1685**，實測是 **−1.2／67.0**。
原因單純：**當日機器在地上、繩子是鬆的**，08-29 那組 26.45／16.85 kg 是**有載**讀值。
→ 兩顆 cell 空載都接近零，**零點正常**。反推本身沒錯，錯在我沒先確認負載狀態。

### 已完成

- ✅ **正負號兩側實測結案**，待辦總表該列 **🔴🔴 → 🔴**（量值），另一列（張力 placeholder）同步註記
- ✅ `Crane_control_PI/main.cpp` 的 scale 註解改寫：移除「right untested but assumed same wiring」，
  寫入量測數據、安全性後果、量值仍未校正的明確標示、左右將需不同量值、以及單位暫存器的陷阱
- ✅ **建置驗證**：吊機 Pi 上重編通過、零警告零錯誤（產物 `crane_signfix.out` 496,664 bytes，
  驗完即刪，避免日後分不清在跟哪個 binary 講話）。diff 經機器檢查**非註解行為 0**
- ✅ 收尾：驗證用 binary 已移除、5001/5002 皆未監聽、未留任何 process

### 待完成

- 🔴 **量值校正**：需要已知重量（使用者確認手邊沒有）。有砝碼再排，機器不用動。
  公式 `kg / (loaded_raw − zero_raw)`（帶正負號），左右各一次。
- 🔴 **量到之後值要放哪裡必須先決定** —— 現行 scale 無持久化，重開機即失效。
  且左右幾乎必然需要不同量值 → **要拆成左右兩個常數**，現在共用一個只因正負號相同。
- 🟡 `Linux_test/dszl_sign_test.py` 尚未進版控（工作區新檔），`main.cpp` 註解改動亦未 commit。
- 🟡 兩側機械耦合的受控量測（本次串音方向不一致，未下結論）。

---

## 2026-08-31（續）— `fix/driver-crc` 上機：階段 A 的無動作檢查通過

### 已完成

- **`fix/driver-crc` 已在兩台 Pi 建置完成**（`~/bringup/`，與 `main` 的 `~/main_20260831/` 完全分開）：
  本體 `facade_drv.out` **16/16 TU**、吊機 `crane_drv.out`，兩邊零警告零錯誤。
  📌 **runbook A2 §1 寫「14 個編譯單元」已過期**——階段 2 加了 `command/dispatcher.cpp`、
  階段 5 把 `WASH_ROBOT.cpp` 拆成兩個 TU，實際是 **16**。檔案清單本身是對的，只有數字舊了。
- 🔴 **更正一條過期記載**：runbook §A2 開頭寫「本分支尚未含 `origin/main` 的 `6523b54`」，
  但 `git merge-base --is-ancestor` 確認**它在 08-29 就由 `b5cb251` 合併進來了**。
  → **第 ⑧ 條的「重試套重試」現在是活的**（應用層 3 次 × driver 交易層重試），不是理論風險。
- 🔴 **`app/WASH_ROBOT.h` 的 `CRANE_IP` 已由 `.17` 改為 `.25`**（含 IP 履歷註解，沿用該常數既有慣例）。
  不改就會重演 `main` 那次「init 停住兩分鐘」的假死。
  ⚠️ **尚未 commit**（工作區改動，與本檔和 `runbook.md` 的更新一起）——要不要進版控由使用者決定。

### ✅ 階段 A 結果（不動馬達的兩條）

| # | 項目 | 判準 | 實測 |
|---|---|---|---|
| ① | 上滑台 cm↔pulse 標定 | `lead=7.731 cm/rev travel<=48 cm` | ✅ 完全吻合，`[profile]` 兩行都載到（`ARM_RAIL_LEAD_CM_PER_REV=7.731`／`ARM_RAIL_TRAVEL_MAX_CM=48.0`） |
| ⑥ | `SO_ERROR` 非阻塞 connect | 吊機關時 `reconnect success` **0 次** | ✅ **雙向斷言**：吊機關 → success=**0**／failed=**107**；吊機開 → success=**1**、`crane_attached=on`。**負向對照證明這個檢查有鑑別力** |
| 塊二 | 吊機 VFD 型號字串 | 應印 `(SE3)` 而非寫死的 `MH300` | ✅ `VFD left (SE3)`／`VFD right (SE3)`。對照組：同日 `main` 上印的是 `(MH300)`＝假的 |

### 🔴 順帶挖到的一條（新，未列在 9 條裡）

- **`crane_cli_` 沒有 `set_quiet_reconnect_log(true)`，吊機一關就洗版**：每 500ms 兩行
  （`reconnecting` + `reconnect failed`），45 秒就 **107 組**，會把其他訊息整個沖掉。
  `arm_cli_`（2026-07-23）與 `depth_cli_`（2026-08-27）都因為同樣理由加過靜音旗標，
  **唯獨吊機這條沒加**——而吊機是三者中唯一會真的離線又真的需要重連的。
  🟡 但**不要照抄靜音**：arm/depth 是「永遠不會接上、純噪音」，吊機離線是**要被看見的事件**。
  合理做法是**降頻或只在狀態轉換時印**（首次失敗印一次、之後每 N 秒摘要一次、恢復時印一次）。

### ✅ 階段 A 全部完成（2026-08-31，使用者在機器旁、機器在地上）

| # | 判準 | 實測 |
|---|---|---|
| ① | `lead=7.731 cm/rev travel<=48 cm` | ✅ 完全吻合 |
| ⑥ | 吊機關時 `reconnect success` 0 次 | ✅ **雙向斷言**：關 → 0／failed 107；開 → 1、`crane_attached=on` |
| ⑦ | `zdt_pusher` 範圍 5..8 且可動作 | ✅ **拆兩段驗**：先送範圍外的 `zdt_pusher 4 extend` → 回 `ERR usage:zdt_pusher_<5..8>`（**零風險，直接從執行中的二進位讀出 `CUP_SLAVE_FIRST..LAST`**；舊版會是 `<1..4>` 或 `ERR invalid_slave`）；再送 `zdt_pusher 5 extend` → `OK`，推桿實際伸出後已 `retract` 復位 |
| ⑨a | `stop` 後不下 `reset` 直接下指令 | ✅ `stop` → `pay_out_right 1` → **`OK moved=2cm`**（有動）。沒有這行修正會回 `ERR aborted` |
| 塊二 | 吊機 VFD 型號字串 | ✅ 印 `(SE3)`；同日 `main` 印的是寫死的 `(MH300)` |

📌 **⑦ 的驗法建議寫回檢查表**：「範圍外指令的錯誤字串」是零風險的二進位版本指紋，
比直接送會動的指令更適合當第一道關卡。

🔴 **檢查表把 ⑨a 放在「階段 A — 不動馬達」是放錯了**：`cmd_side_measured` 在**吊機**
（`Crane_control_PI/main.cpp:2913`），掛在 `pay_out_*`／`retract_*` 下＝**放繩收繩**，
而修正（`28dfa30` 的 `abort_flag = false`）位置在 try_lock 之後、迴圈之前，
`reliable_start_one()` 又在迴圈之前 → **沒修的話 VFD 會先啟動再立刻停**。
**這條沒有零動作的驗法**，下次排程要當成會動的項目。

### ✅ 階段 B ① 通過 —— 上滑台換算修正,實機拿尺驗證

- **指令 17cm → driver 換算 21989 脈衝**(`PPR=10000, lead=7.7310`),驗算 `17÷7.731×10000=21989` 吻合。
  修正前 `lead=1` 會是 **170000 脈衝**(7.7 倍)。
- **使用者拿尺實測:約 17cm**,與座標一致 → **換算正確,且 250rpm 這一趟沒有可見失步**。
  🔴 這就是藏了四個月的那個 bug(每個 cm 指令走 7.7 倍),**現在有實體量測背書**。
- 📌 **驗法**:`arm_sweep` **不能拿來量**——它是「絕對移到 17cm → 再回 0」,靜止時量不到。
  改用 `Linux_test/rail_move.cpp`(`rail_move <rpm> <target_cm>`,絕對移動後停住,
  走同一支 driver 所以 lead 換算與 [0,48] 行程守衛都生效)。
  本次用 `fix/driver-crc` 原始碼現編為 `~/bringup/rail_move_drv`,acc/dec 對齊 `ARM_SWEEP_ACC/DEC=100`。
  另補了一支唯讀的 `~/bringup/rail_pos`(只讀座標不動作),進場前確認位置用。
- 🔴 **「上電後座標=0」不是機械原點**,是上電當下把計數器歸零 → **`[0,48]` 行程守衛是相對那個任意起點算的**。
  進場前要目視確認移動方向還有足夠實體空間,否則守衛放行但實體會先撞到端點。
  📌 **反過來也有好處**:因為 0 不是機械硬限位,「回 0 點」在這個情境下**是有鑑別力的參考點**
  ——runbook §A2 那句「回 0 點正確對失步沒有鑑別力」的前提是零點=機械硬限位,**上電歸零時不成立**。

### 🔴🔴 階段 B ② 結論:**500rpm 會失步,不能採用**(實測數字)

**協議**:`rail_move_drv 500` 在 0 ↔ 17cm 之間跑 **10 趟來回 = 20 次橫越**,最後停回 0,拿尺量記號。
(0 是上電歸零的任意位置、**不是機械硬限位**,所以「回 0 點」在此情境下是有效參考點。)

| 階段 | 尺量偏移 | 座標 |
|---|---|---|
| 2 趟(4 次橫越) | **對齊記號,±1-2mm** | 0.000 |
| 10 趟(20 次橫越) | **4-6mm** | 0.000 |

- **每次橫越約 0.2-0.3mm 失步**(4-6mm ÷ 20),full-scale 17cm 上約 0.15%。
- 🔴🔴 **全程 20 次橫越:通訊錯誤 0、座標每次都是完美的 0.000 / 17.000。**
  **原因是結構性的(per user):DM2J 是開迴路步進,沒有編碼器回授** ——
  位置暫存器存的是**累計命令脈衝數**,不是量測值。它報的是「我以為我走了多少」。
  ⚠️ **所以這個失步沒有任何軟體手段可以偵測**——不是加 log、加驗證、加告警能解決的那一類。
  能發現它的只有**外部量測**(拿尺、或加編碼器/原點感測器)。
  📌 這也是為什麼 runbook 那句「拿尺量兩次,不要看座標」不是謹慎,是**唯一可行的方法**。
- 🔴🔴 **只跑 2 趟會得到「沒失步」的錯誤結論**——4 次橫越的累積量(約 1mm)剛好落在量測誤差裡。
  **失步測試的樣本數本身就是方法的一部分**,下次排這條至少 10 趟。
- 📌 **與 08-28「250rpm 已實測失步」的關係**:本次 250rpm 單程 17cm 沒有可見失步、500rpm 累積失步,
  兩者不衝突——**單程本來就檢不出這個量級**。08-28 那筆的條件(負載/acc/dec/趟數)未記載,無法直接比較。
- 📌 **決策(2026-08-31 per user):RPM 的搜尋不做,由使用者視情況自行調整。**
  🔴 **不要再提議「跑 10 趟協議去找可用 RPM 上限」** —— 已提出並被否決一次。
  已知數字留著供參考:**目前值 250;500 實測累積失步 0.2-0.3mm/橫越,不可用**。
  ⚠️ 理由也留著:開迴路的前提下,「找到不失步的 RPM」只對**當下的負載/摩擦條件**成立,
  負載變了就要重驗 —— 這正是它適合由現場的人視情況調、而不是訂一個常數的原因。
  真正的解是加回授或原點感測器。
- 🟡 **懸著沒結的一筆**:08-28 記載「250rpm 已實測失步」但未記條件(負載/acc/dec/趟數);
  本日 250rpm 單程 17cm 無可見失步。兩者不衝突(單程檢不出這個量級),但那筆記載仍未被驗證過。
  **依上述決策不另行安排測試**,若日後現場觀察到 250 失步,記得把條件記下來。
  ⚠️ 還要確認**是否有週期性 re-home**(`arm_init`)——若沒有,這 0.2-0.3mm/橫越會無限累積,
  而 `[0,48]` 行程守衛守的是**座標**,座標不會漂,所以守衛也擋不住實體越界。

### ✅ 階段 B ④ 通過 —— 推桿 `CUP_PULSE_PER_CM = 3000` 正確

- **命令絕對 48000 脈衝(= 16.000 cm),編碼器位移 48007 脈衝**(差 7 脈衝),`stall=0`、`pos_error=-0.67°`。
- **使用者拿尺實測:約 16cm** → ✅ **`CUP_PULSE_PER_CM = 3000` 正確,
  08-27 那次「更正」成 2857 確實是改錯了**(舊值會讓 16cm 指令只走 15.24cm,尺上差 8mm,量得出來)。
- 📌 **量測協議**:先 `cup_move 5 0` 收到零點讓使用者做記號,再下絕對 48000 → 位移正好是整數 16cm,
  **不必動驅動器零點**(`set_zero()` 會改變 app 的 `last_seal_pulse_` 前提,能不動就不動)。

### ✅ 階段 C ③ 核心通過 —— 吸盤左右歸屬,首次實機驗證

**對應表四顆全對**(逐組推 5cm,由使用者回報實體位置):

| slave | 實體位置 | 程式歸屬(`ZDT_RF1=5, ZDT_RF2=7 / ZDT_LF1=6, ZDT_LF2=8`) | 結果 |
|---|---|---|---|
| 5 | 右上 | 右 | ✅ |
| 7 | 右下 | 右 | ✅ |
| 6 | 左上 | 左 | ✅ |
| 8 | 左下 | 左 | ✅ |

🔴 **這是 `RF={5,6}/LF={7,8}` → `右={5,7}／左={6,8}` 這個修正第一次在機器上得到驗證。**
舊歸屬會讓「右組」的兩顆分屬實體左右兩側 —— 交替步伐因此「不可用」。

📌 **驗法(建議取代檢查表原本的做法)**:**不要用跑交替步伐來驗左右歸屬。**
歸屬是**對應關係問題**,逐組推吸盤就能單獨驗,而且把「對應錯」與「真空時序/密封問題」分開
——跑步伐會把兩者混在一起,任何一邊出問題都可能被誤讀成另一邊。
本次協議:推同一組的兩顆 → 使用者回報是哪兩顆 → 收回 → 換另一組。全程機器在地上、吸盤懸空、零風險。

### 🔴🔴 現行操作模式(per user 2026-08-31)—— 這才是機器實際怎麼動的

**使用者口述的完整流程**:

1. 本體從地面由**吊機拉到頂樓**
2. **開啟左右兩側風扇**把本體壓在玻璃上 → 4 顆輪子頂住玻璃
3. **開始向下移動,風扇維持開啟**。**「移動」＝吊機收放繩**,不是機器自己走
4. 移至定點時**關閉風扇**
5. **伸出推桿約 10cm,吸在玻璃面上**
6. 手臂的清洗滾筒**高約 50cm、垂直於地面**,透過上滑台左右移動
7. **所以下一次移動要小於 50cm**(否則會漏掉一條沒清到的帶)

**風扇控制 ＝ QX_DO24 PWM 模組,工作範圍 5-10%**(per user)。

🎯 **`do_step_sync_` 與這個流程逐步吻合**——它就是現行模式的實作:

| 使用者描述 | `do_step_sync_` 對應 |
|---|---|
| （前置） | 1. `vacuum_valve_("feet", false)` + 確認 4 顆都釋放 |
| **開風扇壓住** | `pwm_set_duty_only_(PWM_STEP_MOVE_DUTY_PCT=7%)` `step_move_on` |
| （前置） | 收腳(4 顆一起,兩段式 + 破真空) |
| **移動＝吊機收放繩** | 吊機移到共同絕對目標(方案B) |
| **移至定點關閉風扇** | 3.5 `pwm_set_duty_only_(PWM_STEP_OFF_DUTY_PCT=5%)` `step_move_off` |
| **伸出推桿吸玻璃** | 4. `vacuum_valve_("feet", true)` → 5. 四顆同時伸出 |

📌 **步驟 3.5 的實作是對的且值得保留**:開風扇時忽略失敗,**關風扇時用 `try_or_pause_`**
——註解寫明「確定關掉之後才可以抽真空 + 伸出推桿,寫不進去就停下來」。**順序本身就是安全需求。**

🔴 **所以交替步伐的處置有答案了:`do_step_down_`/`do_step_up_` 是 v1 遺留,應停用/移除。**
它與現行模式在**兩個獨立層面**都不成立:
- **硬體**:假設分側真空,而幫浦與閥各只有一顆繼電器控 4 顆(見下一節)
- **運動模型**:假設機器用吸盤「走」,而實際移動是**吊機收放繩**,行進間靠**風扇**貼附,
  吸盤只在定點當錨。**根本沒有「交替」這件事。**

### 🔴 兩個對不上的數字(待使用者確認)

| 項目 | 程式 | 使用者口述 | 差異 |
|---|---|---|---|
| 單步上限 `STEP_CM_MAX` | **100** | **要小於 50**(滾筒高 50cm) | 🔴 守衛允許的是物理有效值的 **2 倍** |
| 推桿伸出 `PUSHER_EXTEND_FEET_PULSE(_LOWER)` | **36000 = 12.0cm** | **約 10cm** | 🟡 差 2cm |

- ✅ **已處置(per user「降回 50 以下」):`STEP_CM_MAX` 100 → 45**。
  取 45 而非 50 是**留 5cm 重疊**——吊機定位本身有誤差(計米器 scale 0.5、減速滑行;
  本日實測 `pay_out_right 1` 回報 `moved=2cm` 而計米器走了 9 個單位),零重疊等於把誤差
  全押在「剛好接上」。
  🔴🔴 **同時修掉一個會讓這次修改失效的洞**:`WASH_ROBOT.cpp:1263` 執行期 `step_cm_max`
  的 apply 上界**寫死 `100`**、與 `STEP_CM_MAX` 脫鉤 → **光改常數改不動執行期路徑**,
  而 `load_settings_at_boot` 也走同一條 `cmd_set_setting` → **舊 `settings.json` 可以把上限帶回 100**。
  已改成吃 `STEP_CM_MAX`。
  📌 **重複的字面值本身才是 bug** —— 兩個地方各寫一次 100,改一個就以為改完了。
  ⚠️ 另更正 `WASH_ROBOT.h` 的過期註解「(5..80)」→「(5..45)」。
  ✅ 已重新編譯驗證(16/16,零錯誤),**尚未部署到正式路徑**。
- ✅ **已處置(per user「先改 10cm」):`PUSHER_EXTEND_FEET_PULSE(_LOWER)` 36000 → 30000**(12.0 → 10.0cm)。
  📌 **語意確認過才改**:這個值是 `smart_extend` 的**標稱密封目標**,不是 phase-1 目標
  ——`phase1_targets = 本值 - PHASE1_BUFFER_PULSES`(先快進到差 1cm 處),之後每輪 `+INCR_PULSE`
  往前爬找密封(⑦ 實測 slave 5 由 33000 爬到 45000)。**改小它＝改小標稱貼合位置,仍保有補伸能力。**
  執行期邊界 `10000..50000`(3.3~16.7cm)本來就涵蓋 30000,**沒有 `STEP_CM_MAX` 那種脫鉤問題**。

### ✅ 兩項常數改動的實機驗證(含負向對照)

重編後在機器上實測(`get_settings` 格式為 `現值:編譯期預設`):

| 測試 | 結果 |
|---|---|
| `pusher_extend_feet_pulse` / `_lower` | ✅ `30000:30000` |
| `step_cm_max` | ✅ `45:45` |
| `set_setting step_cm_max 100` | ✅ `ERR invalid_value_or_out_of_range` ← **脫鉤的洞堵住了** |
| `set_setting step_cm_max 45` | ✅ `OK`(邊界內仍可設) |
| `step_down 60` | ✅ `ERR step_cm_out_of_range 60 (allowed 5..45)` |

🔴 **負向對照的意義**:若只改常數、沒改 `WASH_ROBOT.cpp:1263`,第一項會回 `OK`,
而且開機載入舊 `settings.json` 也會把上限帶回 100 —— **改了等於沒改,而且看不出來**。

⚠️ **正式機若已存在 `settings.json`,裡面的舊值會在開機時覆蓋新預設**
(bench 上是「settings.json not found」所以吃到預設)。部署前要檢查正式機的設定檔。

### ⚠️ 一次被中斷卻仍然執行了的啟動(自記)

18:16 那次 `launch.sh` 的工具呼叫**被使用者中斷**,但 **SSH 指令已送出、程式已經起來**,
PID 3112 一路跑到 18:35。後果:18:33/18:35 重編的新 binary 啟動時 `[FATAL] TCP server :5001 fail`,
而 `sendcmd` 接到的是**舊 process** → `get_settings` 回報 `100:100` / `36000:36000`,
一度看起來像「改了沒生效」。
📌 **教訓**:中斷只擋住「我看到結果」,擋不住已經送出的副作用。
**驗證常數是否生效時,要先確認接到的是哪一個 process**(比對啟動時間/binary md5),不能只看回報值。



### ⚠️ 本次工具使用的一個瑕疵(自記)

`cup_move <slave> 0` 我下了 6 次**絕對 0**,但程式碼刻意避開該值:
`PUSHER_RETRACT_PULSE = 300`(≈0.1cm),註解寫「高速收到 0＝機械原點會**撞 hardstop「叩」一聲**;
停在原點前 0.1cm 避免撞擊」。每次 `stall=0`、使用者未回報異音,
但那是在 **900rpm** 下重複撞機械限位。**之後用此工具收回一律用 300,不要用 0。**

### 🔴🔴🔴 交替步伐 `do_step_down_`/`do_step_up_` 架構上不可用 —— 硬體沒有分側真空

**硬體事實(per user 2026-08-31)**:
**真空幫浦是一顆繼電器控 4 顆吸盤,三口二位閥也是一顆繼電器控 4 顆。**
→ **分側真空在硬體上不存在。**

**程式碼卻假設它存在**。`do_step_down_` 的呼叫:
```
run_side("right", "left", {ZDT_RF1, ZDT_RF2}, CH_VALVE_RIGHT, ...)
run_side("left",  "right", {ZDT_LF1, ZDT_LF2}, CH_VALVE_LEFT,  ...)
```
而 `CH_VALVE_LEFT = CH_VALVE_RIGHT = 1`(2026-08-27 起),`group_valve_ch_()` 兩個 group 也都回 1。

🔴 **`pre_cycle` 的前兩步互相矛盾**:
1. `group_seal_ok_(anchor_group)` —— 確認**錨定側**還吸著(≥1 顆),不然拒絕放開移動側
2. `pqw_set_relay_verified_(valve_ch, false)` —— 關掉「移動側」的閥

第 2 步關的是**唯一那顆閥** → **第 1 步剛驗證過的錨定側,在第 2 步跟著失去真空**。
⚠️ **安全檢查驗證的正是下一行會摧毀的東西**,而且 relay 寫入會 verify 成功、log 一切正常。

📌 **`valve_ch` 這個參數是虛構的抽象**;`CH_VALVE_LEFT`/`CH_VALVE_RIGHT` 兩個名字讓程式碼
**讀起來像有兩顆閥**,是這個坑能存在四個月的原因。

🔴 **檢查表 ③ 的「交替步伐從不可用變成可用」不成立**——左右歸屬修好了(本日已實機驗證四顆全對),
但**單閥讓交替步伐仍然不能用**。歸屬與閥是同一個功能的兩個獨立軸向,修好一個不代表另一個。
📌 這也解釋了 `WASH_ROBOT.h` 為何特地註明「v2 正式走法 `do_step_sync_` 本來就是 4 顆同放同吸」——
**同放同吸不是設計選擇,是硬體唯一支援的方式。**

### ✅ `LOG_ERR` 脫離 `debug_mode` —— 修好之後第一次啟動就抓到真故障

**改動**(`user_lib/log_utils.h`,一處生效於全部 15 支 driver / 137 個呼叫點):
`LOG_ERR` 由 `if (debug_mode)` 改為**無條件 + 每呼叫點限流**
(60 秒窗內印前 3 次,第 4 次印一行抑制通知;`debug_mode=true` 時不限流)。
- **為什麼不全開**:計米器 250ms 輪詢,持續失敗＝每秒 4 行,137 個呼叫點全開會洗版。
- **限流規則沿用本專案既有慣例**(`meter_read_robust` 的 `reject_count<3` + 60s 重置)。
- **`static` 放在 `do`-block 內** → 每個巨集展開點各自獨立,計數器是 **per 呼叫點**;
  用 `std::atomic` 因為 driver 由多條背景執行緒呼叫。補了 `#include <atomic>`。
- ⚠️ 該檔自己記著兩個巨集衛生陷阱(不能用 `.data()`、巨集內不能放 `//`),已避開。

**驗證**:同樣 `debug` 關閉的條件下,舊 binary 的 `crane_off.log` 有 **0 行 `[ERR]`**;
新 binary 的 `crane_new.log` 有 **5 行**,且未觸發抑制(41 行總量,不洗版)。

### 🔴🔴 修好 log 之後立刻抓到:SD76 兩支計米器離線(現場需檢查)

新 binary 第一次啟動就印出先前完全看不到的:
```
[ERR]  [SD76:1] init Mode B probe failed — device not on bus (slave 1)
[WARN] SD76 left init failed — left auto-distance disabled
[ERR]  [SD76:2] init Mode B probe failed — device not on bus (slave 2)
[WARN] SD76 right init failed — right auto-distance disabled
```
`status`:`dev_meter_left=0 dev_meter_right=0`、`length_left/right=ERR`,
但 **`dev_gw_m=1` 且 `.34:4001` TCP OPEN** → **問題在 gateway 之後的 RS485 段或計米器本身,不是網路。**

📌 **這條把今天下午的謎團串起來了**:

| 時間 | 狀態 |
|---|---|
| 17:12 / 17:34 | init `[OK] SD76 left/right (resumed)`,`length` 讀得到 `-249 / -245` |
| ~17:47 | 兩側同時 `length=ERR`(**當時查不出原因** —— 拒絕理由被 `debug_mode` 蓋住) |
| 17:5x | 重啟後又正常(所以一度被判為「不可重現」) |
| **19:16** | **init 探測即失敗,`dev_meter=0`,持續不回應** |

→ **下午那次不是偶發,是這個故障的開端。**

🔴🔴 **19:20 獨立探測把範圍收斂了(不啟動吊機程式,`~/bringup/meter_probe`、`pqw_probe`)**:

| 檢查點 | 結果 |
|---|---|
| gateway `.34` TCP | ✅ 正常(connect OK) |
| SD76 slave 1、2 | ❌ 靜默 |
| **PQW slave 12(同一條 RS485)** | ❌ **同樣靜默**(`status reply slave 254 != 12`) |

📌 **回來的不是「某個裝置在位址 254」**:連測 5 次得到 `254,254,252,252,254`
——會變動、且都貼近 0xFF(0xFE/0xFC 只差一個 bit)＝**空閒/浮接匯流排的雜訊**,不是從站回覆。

🔴 **結論:`.34` 後面整條 RS485 幹線都啞了**,不是計米器本身、不是計米器分支、不是網路。
→ 現場檢查範圍:**gateway `.34` 的 RS485 側 / A/B 幹線 / 該段供電 / 終端電阻與偏壓**(浮接的典型成因)。
⚠️ **連帶影響:吊機的進水球閥(PQW CH4)也掛在這條上,目前失控。**
⏱ 17:12 時 `[OK] PQW water USR_M slave 12 CH4` 仍正常 → **故障發生在今天 17:5x ~ 19:16 之間。**

#### ✅ 20:30 現況結案:PQW 暫不接,計米器與其餘全部正常

**per user「PQW 先不接,專注計米器跟其他」** —— 判準是計米器是步伐的位置回授,進水閥只影響加水。

✅ **計米器穩定性 10/10**;吊機端到端跑通:
五個 gateway 全 `[OK]`、`SD76 left/right (resumed)`、`length_left=-249 length_right=-236`、
`dev_meter=1/1`、`tension_valid=1`。

📌 **今天加的 PQW 探測正在做正確的事**:模組未接 → `[ERR] init presence probe failed after 3 tries`
+ `[WARN] PQW water init failed — water_inlet cmd will fail`。
**沒有這個修改的話,這裡會印 `[OK] PQW water ... (water inlet ball valve)`** —— 帳面上有一顆不存在的閥。

#### 🔴🔴 未解:三個裝置同時在線就垮(**不是單一裝置故障**)

| 匯流排組成 | 結果 |
|---|---|
| 兩支計米器(2 裝置) | ✅ 10/10 |
| PQW 單獨(1 裝置,斷電重開後) | ✅ FC01/FC03 全通 |
| **三個都在** | ❌ **整條匯流排全是垃圾**(每個位址都回 `FE`) |

**任何兩個都好、三個就垮 → 電氣負載/終端阻抗問題,不是裝置壞掉。**
而這條匯流排三個裝置**跑了好幾個月都正常** → **變因是今天的接線動作。**
📌 現場檢查方向:① **終端電阻只該在匯流排兩端各一顆**(約 120Ω;PQW 位置變了可能讓原本的末端不再是末端)
② **PQW 不要接成長分支**(stub 反射,三裝置時疊加超過容忍度)③ 新接那段的 **A/B 極性**。
(SD76 手冊:「必要时在仪表后部端子并接一只 100Ω 左右的电阻」)

#### ⚠️ 自記:我在這段追查裡下錯過三個結論

1. **「鮑率超規格」** —— 拿手冊的 9600 上限去推翻現場事實,而**當下就有反證**
   (同一支 binary 幾小時前還讀得到值)。已還原,詳見上方。
2. **「PQW 模組故障」** —— 插拔 A/B 對照看似乾淨,但**沒有測「PQW 單獨在線」**;
   一測就發現它自己完全正常。**A/B 對照只證明了「相關」,我當成了「因果」。**
3. **「位址 1 專屬的洪水」** —— 用「從 2 開始掃就沒事」去驗證,
   但**前一輪掃描已經把緩衝區清空了**,所以第二次當然沒有。**驗證方法本身有缺陷。**
   真相是:**匯流排在壞的時候每個位址都吐垃圾**,而掃描的第一筆會一次撈光累積的殘留,
   看起來就像「只有第一個位址有問題」。

🔴 **共同的失誤形態:拿到一個支持假說的觀察就收手,沒有先問「有沒有別的解釋能產生同樣的觀察」。**
三次都是**少做一個對照**——少「PQW 單獨測試」、少「反證檢查」、少「換順序重測且先清緩衝」。

🟡 **工具缺陷(已知,使用時要注意)**:`~/bringup/mb_scan` 的回覆會**晚一拍**——
第一筆撈走緩衝殘留後,之後每筆拿到的是**上一筆**的回覆
(實測:查 slave 2 收到 `01 03 ...`=slave 1 的、查 slave 3 收到 `02 03 ...`=slave 2 的)。
→ **`mb_scan` 只適合粗略看「匯流排有沒有東西在動」,判定裝置在不在要用 `meter_probe`／`pqw_probe`**
(那兩支走 `TCP_client::sendAndReceive()` 的原子交易)。

#### 🔄 20:05-20:10 **推翻前述結論**:PQW 單獨在匯流排上完全正常

per user「模組先假設他是好的」→ 把匯流排上只留 PQW,用原始 Modbus 幀直接測(`~/bringup/mb_scan`):

**① 全位址掃描(1~255)**
```
★ slave  12 有回應 (7 bytes): 0C 03 02 00 00 95 85    ← 完全合法的 FC03 回覆
★ slave   1 有回應 (8 bytes): FF FF FF FF FF FF FF FE  ← 雜訊(計米器已不在線)
```
**PQW 在 115200 下、位址 12,回應正常。**(順帶回答 user 的「會不會變 9600」:**不是**。)

**② FC01 / FC03 三項全通**
```
FC01 讀線圈 CH1-16   RX 0C 01 02 00 00 94 3D   ← readAllStatus 走的就是這條
FC03 讀 0x0086       RX 0C 03 02 00 00 95 85
FC03 讀 0x0000       RX 0C 03 02 00 00 95 85
```

🔴 **所以「模組故障」的結論是錯的。** 更新後的證據矩陣:

| 匯流排組成 | PQW | 計米器 |
|---|---|---|
| 只有計米器 | — | ✅ 10/10 |
| **只有 PQW** | ✅ FC01/FC03 全通 | — |
| **兩者都在** | ❌ | ❌ **一起垮** |

**單獨都好,湊在一起全垮 → 這是匯流排層級的問題,不是單一裝置壞掉。**
最符合的假說:**PQW 的 RS485 收發器發送後不釋放匯流排**(未回高阻抗)——
單獨在線沒人跟它搶所以看不出來,有其他裝置要回話就撞在一起,連它自己後續交易也毀掉。
⚠️ **但尚未排除**:單純是裝置數量/終端電阻不足(換任何第三個裝置都會這樣)。
📌 **決定性測試(未做)**:只接**一支**計米器 + PQW。仍垮 → PQW 拉住匯流排;正常 → 負載/終端問題
(SD76 手冊:「必要时在仪表后部端子并接一只 100Ω 左右的电阻」)。

#### ✅ 20:10 已修:PQW 儲存設定被清空(執行值與儲存值不一致)

讀 `0x0040~0x0044` 全部是 **0**,而它實際跑在 **位址 12 / 115200**:

| 暫存器 | 修前 | 應為 | 說明 |
|---|---|---|---|
| `0x0043` 站號 | **0** | **12** | 🔴 **0 不合法**(規格範圍 1-255) |
| `0x0044` 鮑率 | **0 = 4800** | **7 = 115200** | 🔴 與實際運作值不符 |
| `0x0040` 同位 / `0x0042` 停止位 / `0x0041` watchdog | 0 / 0 / 0 | 同 | ✅ 本來就對 |

→ **一旦斷電重開,它會用 位址 0 / 4800 起來** = 直接失聯。
✅ **已用 FC06 逐一寫回並讀回驗證**:`[0, 0, 0, 12, 7]`,寫入後 FC01 仍正常。
📌 **順帶證明這些暫存器是真的有實作的**(寫得進、讀得回、值正確)
→ 所以修前讀到全 0 是**設定區真的被清空**,不是「未實作固定回 0」。
🟡 **設定區被清空**這件事本身,可能與「拉垮匯流排」同根因(模組內部異常),也可能是獨立兩件事 —— 未判定。

#### 🔴 20:02 PQW 模組確認故障(插回複測)—— **必須維持拔除**

使用者把 PQW 插回去複測,**乾淨的 A/B 對照**:

| | 拔除 | 插回 | 再拔除 |
|---|---|---|---|
| PQW 自身 | — | ❌ `readAllStatus` 失敗 | ❌(預期,已拔) |
| SD76 左 | ✅ `-249` | ❌ init failed | ✅ `-249` |
| SD76 右 | ✅ `-236` | ❌ init failed | ✅ `-236` |

🔴 **結論:PQW 模組故障,且會拉垮整條 RS485。** 插回去就把兩支計米器一起帶走,拔掉就恢復。
✅ **再次拔除後穩定性複驗:10/10 全過**(第一次讀取失敗是拔除當下的餘波,非持續問題)。

📌 **處置(per user 已執行):維持拔除。** 判準是**計米器遠比進水閥重要** ——
`length_valid` 是 `pay_out_*`／`retract_*` 的前置檢查(`cmd_side_measured` 進場即擋),
沒有計米器整個步伐不能跑;進水閥只影響加水,可等模組換好。

🔴 **待辦:PQW 模組需更換/檢修。** 換上新模組後要**同時複驗兩件事**:
① PQW 自己通;② **兩支計米器仍然正常**(確認新模組沒有同樣的拉垮行為)。

🟡 **工具備註**:`~/bringup/pqw_probe` 與 `meter_probe` 是本次新增的獨立唯讀探測(不需啟動主程式)。
⚠️ 20:02 那次 `pqw_probe` 印 `init ok` 是**舊執行檔** —— 19:56 加的存在性探測只重編了 `crane_drv.out`,
沒重編這支工具。已補編,現在正確回報 `init presence probe failed after 3 tries` → `init FAILED`。
📌 **改了 driver 之後,獨立探測工具也要一起重編**,否則工具會用舊行為給出誤導的結論。

#### ✅ 19:57 已修:`PQW_IO_16O_RLY::init()` 補上存在性探測(雙向驗證通過)

**問題**:`init()` 原本只設欄位就 `return false`(成功),**從不碰匯流排** →
TCP 連上共用閘道就被當成「PQW 活著」,模組**實體移除後 init 仍印 `[OK]`**。
下游每次 `set_water_inlet_()` 靜默失敗,而開機 log 宣稱閥還在。
📌 SD76 在 **2026-05-15 踩過一模一樣的坑**(「meters physically unplugged still showed `[OK] resumed`」),
用 **Mode B probe** 解決;PQW 一直沒補。

**做法**(`user_lib/PQW_IO_16O_RLY.cpp`,比照 SD76):init 尾端做一次 **FC01 read-all** 探測。
- 選 FC01 的理由:**唯讀**(不碰任何繼電器)、就是 verify 路徑既有的呼叫、
  且 `parseReadResponse()` 08-28 起已對不可用回覆(短幀/slave 不符/CRC 錯)回**空 vector**
  → 「empty」在此已是既有的失敗訊號,不需新增協定。
- ⚠️ **重試 3 次、間隔 120ms**:兩個呼叫端對 init 失敗的處置**不對稱** ——
  **本體是 `[FATAL]` 直接中止整個 init**(PQW 控真空閥/泵/刷/破真空,沒有它確實不該開機),
  **吊機是 `[WARN]` + 標記裝置不可用**。若不重試,啟動時一次匯流排抖動就會讓本體開不起來。

**雙向驗證(同一時刻、兩台機器互為對照)**:

| 條件 | 期望 | 實測 |
|---|---|---|
| 吊機 · PQW **已實體移除** | WARN + `pqw_water=0` | ✅ `[ERR] init presence probe failed after 3 tries`／`[WARN] PQW water init failed`／`pqw_water=0`(先前是假的 `1`) |
| 本體 · PQW **在位** | 照常 `[OK]`、init 走完 | ✅ `[OK] PQW slave 12 @ cli_20_ (.20)`、`5001` LISTEN |

📌 **負向對照是必要的**:只看吊機 WARN 不能證明探測正確,還要證明它**不會把在位的裝置講成不存在**
——本體那台若被誤判就是 `[FATAL]` 開不了機。
(同日 `group_seal_ok_` 的教訓正好相反方向:那裡是**只有正向、缺負向**才讓 bug 活了下來。)
🟡 吊機計米器不受影響(`meter_left=1 meter_right=1`),證明探測沒有干擾同匯流排的其他裝置。

#### ✅ 19:52 結案 —— 根因是 **PQW 繼電器模組把整條 RS485 拉垮**

**使用者把 PQW 從匯流排上移除,兩支計米器立刻恢復:**
```
SD76 left  (slave 1): init ok   readUpperInteger ok value=-249
SD76 right (slave 2): init ok   readUpperInteger ok value=-236
```
- **連續 6 次全部成功、數值穩定**;hex 幀完全乾淨:
  `TX 01 03 00 00 00 01 84 0A` → `RX 01 03 02 00 0B F9 83`(標準 Modbus 回覆)
  `TX 01 03 00 21 00 02 94 01` → `RX 01 03 04 FF FF FF 07 FA 25`(`FF FF FF 07` = -249)
- **數值與當日下午最後一次讀到的完全吻合**(`left=-249` 整天未動;`right=-236` 是 ⑨a 放繩 1cm 後的值)
  → **計米器一直有在計數,只是被拖到說不出話。**
- **面板亮著**(per user)→ 計米器全程有電。

🔴 **一顆故障裝置可以癱瘓整條 RS485。** 這是本次最該記住的一條:
`.34` 上三個裝置(SD76 ×2 + PQW)同時失聯,**不是共用電源、不是 A/B 幹線、不是鮑率、不是程式**,
而是**其中一顆把匯流排拉住**(latch-up/短路類),讓 gateway 一直讀到框架錯誤(那片 `FE`)。

📌 **我的嫌疑排序錯了**:當時把「共用電源」排第一、「A/B 斷線」第二,
**完全沒有把「其中一顆裝置故障後拖垮整條匯流排」列進去** —— 而那才是答案。
⚠️ **下次遇到「同一條 RS485 上多個裝置同時失聯」,要先想「有沒有哪一顆把匯流排拉住」,
而不是先找它們的共同上游。** 診斷手法很簡單:**逐一移除裝置**,這是最快的二分法。

✅ **19:53 端到端確認**:`fix/driver-crc` 吊機全跑通 —— 五個 gateway `[OK]`、
`SD76 left/right (resumed)`、`length_left=-249 length_right=-236`、`dev_meter=1`、張力正常。

🔴🔴 **但 init 印 `[OK] PQW water USR_M slave 12 CH4` —— 那是假的,模組已被實體移除。**
`PQW_IO_16O_RLY::init()` **不做存在性探測**,寫得出去就算成功。
SD76 在 2026-05-15 踩過一模一樣的坑(「meters physically unplugged still showed `[OK] resumed`」),
後來補了 **Mode B probe** 才解決 —— **PQW 沒有補**。
→ 實務後果:步伐流程呼叫 `set_water_inlet_()` 會靜默失敗,而 init 的 `[OK]` 讓人以為閥還在。
📌 **建議比照 SD76 給 PQW 補一個 init 探測**(讀一次 FC01 狀態,讀不到就 `[WARN]` + 標記 dev 不可用)。

🟡 **順帶浮出(LOG_ERR 修好之後才看得到)**:init 時兩顆 SE3 各印一次
`[ERR] [SE3:1] writeParam reg=0x1101 val=0x9696 comm fail` / `clearAlarm: write H1101=0x9696 failed`,
但隨後 `[OK] VFD left/right (SE3)` 照常。**先前完全不可見**,可能是既有且無害,也可能不是 → **列入觀察**。

🔴 **連帶後果:PQW 已實體移除 → 吊機的進水球閥(CH4)目前沒有控制**,
`Crane_control_PI` 的 `PQW water USR_M slave 12 CH4` 會 init 失敗。**該模組需更換/檢修。**

📌 **同時再次否證鮑率假說**:全程 `115200` 正常運作,幀乾淨。

#### 19:26-19:40 追查:gateway 鮑率錯誤(已修)+ 裝置無回應(未解)

**① `main` 分支對照(per user 提議)——排除了程式因素**
`~/main_20260831/crane_control_PI.out` 跑起來**同樣失敗**(`SD76 left/right init failed`、
`dev_meter=0`、`length=ERR`),而**同一支 binary 17:5x 還讀得到 `-249/-245`**
→ **不是 `fix/driver-crc` 的新驗證太嚴,是硬體/設定變了。**
📌 這個對照能成立,是因為 `main` 的建置當天有保留下來(per user「之後可能會用到」)。

**② ❌ 走錯的一條路:誤判「鮑率超規格」是根因(我的錯,已還原)**

使用者提供 `tmp/SD76-C/*.docx`,手冊寫「`BAUSET` 通讯速率:默认为9600.(**范围 9600-4800**)」,
而 gateway `.34` 是 **115200** → 我**斷定這就是根因**並提議改成 9600。
🔴 **結論是錯的。per user:本專案這批 SD76-C 實際跑在 115200,可用。**
`USR_M` 長期就是 115200,SD76 與同匯流排的 PQW 都正常運作過。

📌 **我當時手上就有否證這個假說的證據,卻沒有用它**:
**同一支 `main` binary 幾小時前(17:5x)還讀得到 `-249/-245`** —— 如果 115200 真的超規格,
那次就不可能成功。**「它之前是怎麼正常運作的?」這個問題本來就該先問。**
⚠️ **教訓:規格書的參數範圍是「這型號的通用說明」,不等於「現場這批的實際能力」。
不要拿規格書去推翻現場既有的事實。** 權威版已寫進 `summaries/SD76_MODBUS_SUMMARY.md`。

**③ 實際做過又還原:gateway `.34` 鮑率 115200 → 9600 → 115200**
- 途徑:`http://192.168.1.34` web UI(帳號 admin,憑證存在 crane Pi 的 `~/.gwauth`,600)
- 流程:`port.cgi?...&br=9600&...`(**其餘欄位全部帶原值**)→ `manage.cgi?reset=1&rup=0&rfp=0` 重啟
- ⚠️ **`rup`(回復使用者預設)/`rfp`(回復出廠設定)在同一個表單**,必須明確帶 0,誤觸會清掉整台設定
- ⚠️ **設定是暫存的,不重啟不生效** —— 第一次只送 `port.cgi` 時回讀仍是 115200,一度誤以為寫入失敗
- 結果:`_br = 9600`,其餘(`8/N/1 stop`、`4001`、`TCP Server`)原封不動,10 秒內回線

**④ 兩種鮑率下的症狀(實測紀錄,但**都不是鮑率造成的**)**

| 閘道鮑率 | RX 內容 | 我當時的(錯誤)判讀 |
|---|---|---|
| 115200(**正確值**) | **滿版 `FE`**(250 個) | 誤判為「鮑率不符的框架錯誤」 |
| 9600(改錯) | **完全空白** | 誤判為「鮑率對了、線路安靜」 |

🔴 **兩個判讀都錯**:改成 9600 之後**仍然不通**,改回 115200 症狀原樣回來
→ **鮑率從頭到尾都不是問題,故障一直在裝置端(未解)。**
📌 症狀對照本身仍有保存價值(日後真的遇到鮑率不符時可比對),但**不可再拿來當鮑率的證據**。

剩下的嫌疑:
**① 計米器供電**(兩支 + PQW 同時啞 → 共用電源最可能)／**② gateway 之後的 A/B 斷線**／
③ 裝置本身位址或鮑率被改(三個一起變,機率低)。
📌 **下一步(現場)**:看計米器面板亮不亮。亮著就能直接連按 `SET` 讀出 `Adrset`／`BAUSET`／`UArt`
(方法見 `summaries/SD76_MODBUS_SUMMARY.md`)。
⚠️ **gateway 現況已還原為 `115200 / 8 / N / 1 stop / 4001 / TCP Server`**(與故障前一致)。
📌 **操作紀錄留著**(下次要改設定時可直接用):
`port.cgi?...` 寫入 → **必須** `manage.cgi?reset=1&rup=0&rfp=0` 重啟才生效(只送 port.cgi 回讀仍是舊值);
⚠️ **`rup`(回復使用者預設)/ `rfp`(回復出廠設定)與重啟在同一個表單**,必須明確帶 0。

🟡 **順帶發現:`PQW_IO_16O_RLY::init()` 回報 ok,裝置其實不在**(只有後續 `readAllStatus` 失敗)。
這正是 SD76 在 2026-05-15 踩過、後來補 Mode B 探測解決的同一個毛病 —— **PQW 沒有對應的探測。**

📌 **`Mode B probe` 本身就是為這種情況加的**:`SD76_length_meters.cpp:53` 註解
「Bench 2026-05-15: meters physically unplugged still showed `[OK] resumed`」
——**探測是對的、也有效,只是它的錯誤訊息一直沒人看得到。**

🔴 **這次的整體教訓**:第 ⑤ 條的用意是「把問題變可見」,但**可見性本身也需要被驗證**。
一個「會偵測、會記錄、但記錄看不到」的機制,實務上等於沒有偵測。

### 🔴🔴 `group_seal_ok_` 的布林值忽略 `group` 參數 —— **在活的路徑上**(2026-08-31 發現)

```cpp
bool WashRobot::group_seal_ok_(const std::string& group, std::vector<int>& out_unsealed) {
    const std::vector<int> all_unsealed = vacuum_check_("all");
    const std::vector<int> all_slaves   = group_slaves_("all");
    ... // group 只拿去填 out_unsealed（診斷清單）
    const int sealed_total = (int)all_slaves.size() - (int)all_unsealed.size();
    return sealed_total >= SEAL_MIN_CUPS_TOTAL;   // ← 完全沒有用到 group
}
```

`SEAL_MIN_CUPS_TOTAL = 2`,`group_slaves_("all")` = 四顆
→ **`group_seal_ok_("right")` 與 `group_seal_ok_("left")` 永遠回傳相同的值。**

`do_step_sync_`(**活的正式路徑**,不是已停用的那三支)裡:
```cpp
bool right_ok = group_seal_ok_("right", right_unsealed);
bool left_ok  = group_seal_ok_("left",  left_unsealed);
if (!right_ok || !left_ok) { ...停住不動... }
```
整段等價於「四顆裡有沒有 ≥2 顆吸住」。

🔴 **危險情境**:右上+右下都吸住、左邊兩顆全空 → `sealed_total = 2` → 兩個旗標都 true
→ **步伐照常進行,而整個左側是脫離的。**

⚠️ **而它正上方的註解寫著「only a WHOLE side unsealed is a hard stop」**
——**正是程式碼偵測不到的那一種**。與 `do_cross_obstacle_` 那句
「The anchor valve is NEVER toggled here」是**同一個模式**:註解陳述意圖,程式碼做相反的事。
📌 本日兩處都屬這一類 → **看這個 codebase 的安全性斷言時,不要把註解當證據。**

**為什麼會這樣(來歷說得通)**:`WASH_ROBOT.cpp:4863` 註解寫明
「[2026-08-28 per user] 現規則:4 顆裡總共有 SEAL_MIN_CUPS_TOTAL(=2) 顆吸住」——**是刻意的**。
而它當時會被接受,是因為 `right = {5,6}`(一邊各拿一顆)**分側判準根本算不準**,只能退而求其次看總數。
🔴 **歸屬已於 2026-08-31 修好並實機驗證(右={5,7}／左={6,8},四顆全對)→ 那個前提消失了**,
而「≥2/4」現在**允許一整個實體側面脫離**。

**建議修法**(未實作):讓 `group_seal_ok_` 真的看 group —— 回傳「該組 ≥1 顆吸住」,
這樣 `right_ok && left_ok` 才真的是「每側各 ≥1」,與註解宣稱的一致。

✅ **已於 2026-08-31 稍晚改回分側判準(per user「2 3 開始」)。**

📌 **重要更正:我先前把它稱為「bug」並不準確。** 它是**有意識的權宜之計,而且程式碼自己
寫下了退場條件** —— `WASH_ROBOT.h:637` 與 `WASH_ROBOT.cpp` 的 08-28 註解都明載:
> 「⚠ 已知取捨(**不是疏漏**):本規則擋不住『吸住的 2 顆剛好在同一側』…
>   **左右歸屬確認後應改回分側判準。**」

**該退場條件於 2026-08-31 達成**(實機逐組推吸盤,right={5,7}=右上+右下、left={6,8}=左上+左下,
四顆全對)→ 這次改動不是推翻設計決策,是**執行程式碼自己寫下的計畫**。

**做法**:
- 新增 `SEAL_MIN_CUPS_PER_SIDE = 1`;`group_seal_ok_` 改為**真的看 group**
- 單側 group → 該側自己 ≥1 顆吸住;`"all"`/`"feet"` → **兩側各自都要達標**
- 🔴 **整側被 `zdt_disable` 停用時 `group_slaves_` 回空** → 該側視為不阻擋,
  否則停用一側等於讓步伐永遠無法進行(而停用是使用者的明確意圖)
- 保留單次 `vacuum_check_("all")` 掃描再導出兩側答案(原註解:每顆 3 取樣、顆間隔 50ms,
  分兩次掃 group 會多花一倍 bus 時間)
- `SEAL_MIN_CUPS_TOTAL` **已無任何程式碼使用**,保留供沿革與日後回退;標頭已註明 `[unused in code]`

**✅ 驗證完成(編譯 16/16 + 實機三向測試)**

📌 **測試路徑用 `realign`**(`cmd_realign` → `do_feet_realign_`)——它分別呼叫兩側,
**且在任何動作之前就拒絕**,錯誤訊息還會指名是哪一側(`right=ALL` / `left=ALL`)。
比跑 `step_*_sync` 安全得多(那個會驅動吊機)。
⚠️ 順帶一提:`do_feet_realign_` 的註解與訊息**本來就是分側語意**
(「Refuse ONLY if a WHOLE side is off」/「need >=1 sealed per side」)——**舊程式碼給不出來**。
又一處註解與實作不符,被這次改動修掉。

| 測試 | 條件(實測壓力) | 預期 | 實測 |
|---|---|---|---|
| **A** | 四顆全空 `p5..p8 = 0` | 兩側都報 off | ✅ `realign_refuse_side_off **right=ALL left=ALL**` |
| **B(決定性)** | 右側 `p5=-66 p7=-67` 吸住、左側 `p6=1 p8=0` 空 | **只報 left** | ✅ `realign_refuse_side_off **left=ALL**` |
| **C(反向對照)** | 兩側各 1 顆:`p6=-68`(左)、`p7=-67`(右) | **放行並執行** | ✅ `[realign] v2 done`,無誤擋 |

🔴 **B 是決定性的**:訊息裡**只有 `left`、沒有 `right`** —— **舊版兩個旗標永遠相等,
產生不出這個輸出**;它會判定兩側都 OK 然後繼續動作,而整個左側是脫離的。
🔴 **C 同樣不可省**:只驗「壞的會擋」不夠,還要驗「好的不會被誤擋」。
**這個洞能活這麼久,正是因為當初只驗了一半。**
📌 順帶確認 `PUSHER_EXTEND_FEET_PULSE` 改動生效:log 印 `preset=30000`(10cm)。
📌 測試用真空:`pump on` + `vacuum feet on`,使用者手持平面貼靠指定吸盤;測完已 `vacuum feet off` + `pump off`。
🔴 **實際作業時要盯的**:出現「一側兩顆全空但步伐照跑」時,那就是這條;
`out_unsealed` 的診斷清單是對的(它有看 group),**只有布林值是錯的**,所以 log 會印出正確的未吸清單、
卻仍然放行 —— **看 log 的人會以為系統知道並判斷過了。**
⚠️ `do_feet_realign_`(2155/2156)也用同一個函式,同樣受影響。

### ⚠️ 一次被截斷的 grep 導致錯誤結論(自記)

先前用 `grep -rn "group_seal_ok_(" app/*.cpp | head` 查呼叫端,**`head` 截斷了輸出**,
只看到 6 個(全在已停用的三支函式裡),因而向使用者說「這條可能可以直接關掉」。
**完整清單是 16 個,其中 `do_step_sync_` 與 `do_feet_realign_` 都是活的** —— 結論完全相反。
📌 **判斷「某個東西還有沒有人用」時,不要對呼叫端清單用 `head`。**

### ✅ `CRANE_IP` 伏筆消除 —— 改成開機自動選路(有線優先、WiFi 備援)

**原本的伏筆**(08-28 per user):兩台 Pi 之後會用 eth 串接,**屆時要回頭改 `WASH_ROBOT.h` 的 `CRANE_IP`**。
🔴 **它不會有任何徵兆**:串上線之後程式照樣走 WiFi,有線路徑就在旁邊沒被用到,log 完全正常。
而風險是實質的 —— 機器吊在半空中時控制流量跑在 WiFi 上;**而 2026-08-31 當天正好示範了 WiFi 會漂**
(`.17 → .25`,程式連不上、卡了兩分鐘)。有線位址不會漂。

**做法**:新增 `CRANE_IP_ETH = 192.168.1.10` + `resolve_crane_ip_()`,**開機時先探測有線,通了就用**。
→ **串接當天自動生效,不必記得回來改常數。把「要記得做的事」換成「自己會做對的事」。**

🔴 **兩個設計上的坑,都刻意避開了:**

1. **不能用 `connectToServer` 去試** —— 它是**無逾時的 blocking connect**,對不存在的主機會卡滿
   TCP SYN timeout(當天實測約兩分鐘)。「先試有線」若用它實作,**在沒串 eth 的現況下每次開機
   先卡兩分鐘,比不做還糟**。
   → 自己寫有界探測:非阻塞 connect + `select` + **`getsockopt(SO_ERROR)`**,逾時 300ms。
   📌 那個 `SO_ERROR` 檢查正是 2026-08-28 修過的缺陷(select 說可寫≠連上),這裡從一開始就帶上。
2. **邏輯不能放進 `ep::host`** —— `common/endpoints.h` 的設計規則明載
   「**沒設環境變數時行為必須位元等價**」,等價性測試靠它。在它上面加探測會破壞測試前提。
   → 放在 `resolve_crane_ip_`,而且**偵測到環境變數覆蓋時完全不探測、直接照用**。

**三條分支實機全驗**:

| 分支 | 預期 | 實測 |
|---|---|---|
| 環境變數覆蓋 | 照用、**不探測** | ✅ `位址由環境變數覆蓋 = 127.0.0.1（不做有線探測）` |
| 有線不通 | 300ms 內退 WiFi、**不卡頓** | ✅ 開機到 command server 就緒 **1 秒** |
| WiFi 路徑 | 實際連得上 | ✅ `[OK] crane 192.168.5.25:5002`、`crane_attached=on` |

🟡 **已知小缺**:覆蓋判定是 `overridden != CRANE_IP`,所以把 `FCV_EP_CRANE_HOST` 設成
**與預設值相同**時無法辨識為覆蓋(會照常探測)。實務上無影響(結果一樣),但要知道。
📌 兩台的 eth0 **已經都設在 `192.168.1.0/24`**(本體 `.100`／吊機 `.10`),只差實體網線。

### ✅ 減速遮罩組(①+⑧)已修 —— 根因是「同一個數字被當成兩種語意」

**查證過程先排除了一條岔路**:同一個迴圈裡有 `dm2j_active_now` gate,滑台動作期間會整段跳過 tau 偵測
——一度以為遮罩是被它取代的死碼。**但不是**:`dm2j_motion_active_` 只由
`dm2j_pair_move_abs_` / `pusher_move_many_` / `pusher_two_stage_retract_` 設定,
而掃動走的是 `arm_sweep_fire_nowait_` → `PR_move_cm_nowait`(只寫不 poll),**不設那個旗標**
→ **gate 不適用,tau 偵測全程開著,遮罩確實是該保護減速期的那道,而它壞了。**

**根因**:`est_ms` 被同時當成兩種東西 ——
- 「**監看多久**」(逾時):刻意設得寬鬆(3900/4500ms),註解說「估太長只是多等一下,
  safe、no correctness impact」
- 「**動多久**」(遮罩錨點):`elapsed > est_ms - MASK`

🔴 **那句「no correctness impact」是錯的**:估太長就把遮罩整個推到真實運動之外。
17cm @250rpm 實際約 **578ms**(推算)／**553ms**(實測),而遮罩窗口是 **3500~4500ms**;
再加上迴圈在 motion complete 時會 **early-exit**,根本走不到窗口
→ **這道保護從加進來就沒生效過,而且完全靜默。**

⚠️ **而「把 est_ms 調小」這個直覺修法會直接關掉偵測**:`est_ms ≤ MASK` 時
`est_ms - MASK ≤ 0` → 條件恆為真 → **整趟偵測全程關閉**,同樣靜默。這就是 ⑧ 那條耦合。

**修法:把兩種語意分開。**
- `est_ms` = 監看逾時(維持寬鬆,估太長只是多等)
- **`motion_ms` = 真實運動時間**(新參數,遮罩錨點)
- 新增 `arm_rail_motion_ms_(target, from, rpm, acc, dec)`:用**實測導程 7.731** 算
  巡航 + 斜坡(`acc/dec` 單位是 ms/1000rpm → 實際斜坡 = `rpm/1000 × 值`)
- 呼叫端 `arm_sweep_fire_nowait_` 讀目前座標當起點算出 `motion_ms` 傳入,並印出來
- 🔴 **`motion_ms <= 0` 退回舊行為** → 讀不到座標或算不出來時,行為與修改前完全相同

**驗算**:17cm@250rpm=578ms、10cm@250rpm=360ms、17cm@1000rpm=332ms —— 與待辦記載的
「真實運動 553ms」吻合。

⚠️ **本次修改編譯通過(16/16)但未經實機驗證** —— 要觀察 tau 行為需要實際跑掃動
(會開滾筒刷 CH_BRUSH + 上滑台 17cm)。**bench 有機會時應拿 log 的
`motion complete at t=Xms` 回頭校驗 `arm_rail_motion_ms_` 的推算值。**
📌 小坑:`WASH_ROBOT.h` 未 include `<cmath>`,標頭內的取絕對值改用手寫比較(少加 include)。

### ✅ SE3 開機 FAULT bit 查明:是 OPT(通訊逾時),良性 —— 但故障歷史已被它洗掉

**直接讀兩顆的狀態字與故障歷史**(`~/bringup/se3_fault`,唯讀,不啟動吊機程式):

```
SE3 左 @ .30   H1001 = 0x0080  b7 SET
               H1007 = 0xA0A0 → 異常1 = 160 OPT / 異常2 = 160 OPT
               H1008 = 0xA0A0 → 異常3 = 160 OPT / 異常4 = 160 OPT
SE3 右 @ .31   H1001 = 0x0080  b7 SET
               H1007 = 0xA0A0 → 異常1 = 160 OPT / 異常2 = 160 OPT
```

**① 開機 FAULT bit 是良性的,而且是正確安全設定的代價。**
`07-10 = 0` ＝「通訊中斷即報警 OPT + 空轉停車」,摘要標為**「with keepalive 的建議值」**。
我們每次停掉吊機程式 → keepalive 停 → 逾時 → OPT 鎖存 → 下次開機看到 b7。
🔴 **決策:不要改 07-10。** 另一個值(=1)是「不報警繼續運行」——通訊斷了馬達還在轉,
吊機上不可接受。**這條記下來,免得日後有人為了消掉開機警告而去改它。**

**② 真問題:故障歷史對診斷真故障是無效的。**
`H1007`/`H1008` **四個槽位全部是 OPT** → 任何真實故障(OC/OV/OHT/SCP)只要發生過,
**都已被例行關機推擠出歷史**。
📌 **這解釋了待辦 ⑥「VFD 故障碼顯示是壞的」為何一直查不清楚**:讀 H1007 永遠得到 OPT,
等於沒有資訊。**不是讀取壞掉,是內容已經沒有鑑別力。**
⚠️ 連帶:`H1007/H1008` 位址其實**規格書有明載**(`SE3_INVERTER_MODBUS_SUMMARY.md` §Error Code
Reference),程式碼註解說「addresses are unverified」是過期的。

**③ 已改:開機訊息能分辨 OPT 與真故障**(`SE3_inverter.cpp` init 的 FAULT 分支)
- 是 OPT → `LOG_WRN`「最近異常=OPT(160) 通訊逾時 — **上次關機的預期殘留**」
- 不是 OPT → **`LOG_ERR`**「**不是 OPT,可能是真故障**,查 H1007/H1008」
- 讀不到 → `LOG_WRN`「無法判斷」
📌 **分流原則:預期的事安靜(WRN)、意外的事大聲(ERR,不受 debug_mode 限制)。**
實機驗證(需 `CRANE_DRIVER_DEBUG=1` 才看得到 WRN):
`[WRN] [SE3:1@L] init: FAULT bit set (status=0x0080), 最近異常=OPT(160) …` 左右都正確。

🟡 **順帶暴露:`LOG_WRN` 仍然被 `debug_mode` 蓋住**(今天只解禁了 `LOG_ERR`)。
本次的良性訊息因此預設看不到 —— 對「良性」而言可接受,但**warning 整體不可見**這件事
本身值得評估。未改,列為待辦。

### ✅ B 段安全項 + 從「放回 12cm」意外挖出的一串問題

**做完的(B 段可在軟體側處理的部分)**
- **④ `run_depth_avoid` 自動改走 cross → 已停用**:改成偵測到障礙就**停下來說明原因**
  (`depth_avoid_obstacle_needs_manual`),而不是跑到下一輪撞上守衛回一個看不懂的 ERR。
  📌 一般步伐**本來就已經是 `do_step_sync_`**(2026-07-28 per user 改過),只有 auto-cross 那條有問題。
- **③a `cmd_hold` 補 motion 互斥**:稽核六支會驅動 VFD 的指令,`motion_rope`／`cmd_roll_correct`／
  `cmd_align_lengths`／`cmd_side_measured` 都有 `try_lock(motion_mtx)`,**只有 `cmd_hold` 沒有**
  →GUI hold 可在 motion 跑到一半時同時驅動同一顆 VFD。
  ⚠️ **只鎖 `on`,`off` 永遠放行** —— `off` 是解除操作,擋掉等於「動作中無法放開 hold」,比原problem更危險。
  📌 `cmd_manual` 也沒有鎖,但那是**刻意的原始旁路**(註解自陳 bypasses safety),**不該加**。
  🟡 反向(hold 生效期間再啟動 motion)未做 —— 要在 motion 進入點加 `any_hold_active()` 檢查,
  是行為改變,應由使用者拍板。
- **③b 左右繩長差硬警報(新增)**:`motion_rope` 每輪檢查,超標即 abort 兩側
  (安全原則同既有的 vfd fault:一側卡住=兩側都停)。附 `set_length_diff_max_cm` runtime setter、
  `status` 曝露欄位、上下界負向對照都驗過。
  🔴 **第一版是錯的,上機資料當場打臉**:寫成 `|left - right|` 絕對差,而靜止時
  `left=-249 / right=-236` 就差 **13cm**、門檻 15cm ——**機器沒動就快觸發**。
  原因:**兩支 SD76 零點各自獨立**,固定偏移正常。已改為
  `|(cur_L-base_L) - (cur_R-base_R)|`(本次動作期間的相對位移差)。
  📌 **沒有上機看那兩個數字,這個 bug 會一路帶到現場才炸。**
  🟡 門檻預設 15cm 是保守起手值,**需要使用者確認**。刻意不從傾角回推
  (`FOLLOWER_SPAN_CM=100` 標著 PLACEHOLDER,拿未校正跨距換算會得到看似精確但無根據的數字)。

**B 段做不了的(誠實列出)**:② DSZL 刻度／⑤ 張力刻度 → **要掛已知重物實測**;
⑥ VFD 故障碼 → 要逐項排查且 08-29 症狀已變;⑦ 上滑台 RPM → **per user 由使用者自行調整**,不重開。
①+⑧(減速遮罩與 `*_EST_MS` 耦合)未做。

### 🔴🔴 從「左繩放回 12cm」意外挖出:VFD 寫入間歇失敗,而且會吃掉減速命令

**起因是我的失誤**:驗證 `cmd_hold` 互斥時下了 `up_left on` → `sleep 1` → `off`,
**`up_left on` 不是設旗標,它真的收繩** —— 左繩被收了 12cm(`-249 → -261`)。
📌 **這是同一天第二次犯同一個錯**(稍早把 `step_down_sync 10` 放進驗證清單)。
**驗證清單只該放預期被拒絕的指令;會動的必須事先講明。**

**放回去的過程暴露了真問題。** log 顯示**每個動作指令都恰好有一次 `SE3` 寫入失敗**:

| 指令 | 失敗的寫入 | 結果 |
|---|---|---|
| `pay_out_left 12` | `0x1000=0x0000`(停止,有 `reliable_stop_one` 重試)| 過衝 5cm |
| `retract_left 1` @5Hz | `0x1000`(同上) | **準確 1cm** |
| `retract_left 4` @5Hz | **`0x1002=0x01F4`(減速到 5Hz)** | **過衝到 10cm** |

🔴 **修:短程直接以 `fine_adjust_hz` 起步。** 舊行為一律以 `motion_hz(50Hz)` 起步,再靠
slow-approach 寫一筆減速;而 **`cm <= CMD_MEASURED_APPROACH_CM(8)` 時該條件第一輪就成立**
= 「用 50Hz 起步、立刻送一筆減速」。**那筆只要失敗一次,短程就整段跑在 50Hz**,
4cm 瞬間走完、重試來不及。短程本來就不需要高速段。

📌 **兩個被推翻的解釋(自記)**:
1. 我先把過衝全歸因於「機械滑行」——**per user 是 6mm 鋼索的彈性**:減速時張力驟降,
   被拉伸的鋼索回彈,計米器量到的長度變化**不全是捲筒轉動**。這解釋了為何速度越高過衝越大。
2. 但這一輪的**大**過衝主因不是彈性,是**減速命令根本沒送到**。
   **5Hz 時滑行 ≈ 0(1cm 指令走 1cm)** 同時支持兩者:低速→彈性小,且高速段被跳過。

### ✅ driver log 分不出左右 —— 已修(SE3 / DSZL / MH300)

`_log_tag` 只由 slave id 組成,而**左右兩顆的 slave 都是 1**(各自獨佔一條 gateway,號碼不必錯開)
→ `[SE3:1] ... comm fail` **分不出是哪一顆**。今天 `LOG_ERR` 解禁後這些訊息才看得見,
卻卡在這裡 —— **看得見、但一半的診斷價值被標籤吃掉**(當晚查 VFD 寫入失敗就卡在這點)。

**做法**:三支 driver 加 `set_log_side("L"/"R")`(冪等),**存成 `_log_side` 由 `init()` 併進 tag**
→ 呼叫端在 **init 之前**設定,**init 內部印的那幾行也帶得到標籤**(那正是最需要歸屬的位置)。
SD76 是 slave 1/2,本來就分得出,不需要。

**驗證**:第一次跑「未帶側別 = 0」——**但那證明不了任何事**(該輪根本沒有 SE3 訊息,
通訊失敗是間歇的)。開 `CRANE_DRIVER_DEBUG=1` 強制產生才驗到:
```
[WRN] [SE3:1@L] init probe OK but FAULT bit set (status=0x0080) — clearAlarm
[WRN] [SE3:1@R] init probe OK but FAULT bit set (status=0x0080) — clearAlarm
```
📌 **「沒有壞訊息」不等於「修好了」** —— 要讓它印出來才算驗到。

### 🔴 順帶查明:兩顆 SE3 每次開機都帶著 FAULT bit

上面那兩行顯示**左右兩顆 init 時 `status=0x0080`(FAULT)都是設起來的**,程式每次開機都要 `clearAlarm`。
→ 今晚那些 `clearAlarm: write H1101 comm fail` **不是隨機出現**,是每次開機都要做這件事,而那筆寫入偶爾失敗。
🔴 **這把待辦 ⑥「VFD 故障碼顯示是壞的」串起來了**:`fault_code=READ_FAIL` 是偵測到 FAULT(0x80)
之後去讀 `0x1007` 故障碼、而該筆讀取失敗。**FAULT 本身是真的,不是誤報。**
🟡 **待查:為什麼每次開機都帶 FAULT?**(上次斷電時的殘留?還是真有反覆發生的故障?)

### 📌 SD76 兩支已依使用者指示歸零

`FC06 reg 0x0000 = 0x0003`(= 下排復位 `0x0001` + 上排復位 `0x0002`,依手冊清零控制碼),
前後讀值均留存:`-255 / -236` → `0 / 0`。**只清計數器,不驅動任何東西。**
🔴 **使用者指示:左繩不可再收繩** —— 本 session 之後不再出現 `retract_left`。

### ✅ init 訊息誠實化 —— 系統掃描「還有誰的 `init()` 不碰匯流排」

PQW 那個坑提示了一個**類型**,所以把 15 支 driver 的 `init()` 全掃一遍
(判準:函式體內有沒有實際的匯流排交易):

| 有探測(4) | 無探測(10) |
|---|---|
| `MH300_inverter`、`PQW_IO_16O_RLY`(本日新增)、`SD76_length_meters`、`SE3_inverter` | `CLV900`、`DIHOOL`、`DM2J_RS570`、`DSZL_107`、`DY_500`、`JC_100_METER`、`QX_DO24`、`XKC_Y25`、`ZDT_motor_control`、`ZS_DIO_R` |

🔴 **但「不探測」本身不是問題,誤導才是。** 對照應用層的 init 訊息:
- **老實的**:`[OK] XKC water level slave 13 (sensor presence not probed)`、
  `[OK] QX-DO24 PWM slave 9 (presence not probed)`
- 🔴 **不老實的(3 處)**:`DM2J arm rail`／`ZDT 5~8`／`JC-100 5~8` —— **無條件印 `[OK]`**,
  而它們的 `init()` 只綁 client、設 slave 號,**裝置實體拔掉一樣會印 `[OK]`**。

✅ **已補上 `(presence not probed)`**,用詞比照同段本來就誠實的兩行。實機驗證輸出正確。
📌 **副作用是好的**:`[OK] PQW slave 12 @ cli_20_ (.20)` 現在是**唯一沒有但書**的一行
→ **「沒有但書」從此代表「真的驗過在線」**,這個標記變得有意義。

🟡 **未替那 9 個裝置加探測是刻意的決定,不是偷懶**:本體 `init()` 的失敗路徑是
**`[FATAL]` + 中止整個 init**,替 DM2J + ZDT×4 + JC-100×4 加探測 = **9 個新的開機失敗點**,
一次匯流排抖動就開不了機。要加得比照 PQW 帶重試(本日 PQW 就是為此加了 3 次重試),
**是獨立的決定,應該由使用者在了解取捨後拍板。**

### ✅ `ch3=11` 結案:模組端殘留,無害(per user「只用到 CH1」)

**當初的疑點**:`pwm status` 顯示 `ch3=11`,而 driver 的 `duty_max=10` —— 超出上限。

**查證結果:那不是本軟體寫的,也不可能是。**
- `QX_DO24::setPWM_Duty()` **會拒絕** `[duty_min_pct, duty_max_pct] = [5,10]` 之外的值
- `pwm_set_duty_only_()` **只寫 `PWM_STEP_CH`(=1)**
- `cmd_pwm_status` 讀的是**模組上的實際暫存器**(`getPWM_Duty/Freq/Control`)
→ `ch3=11%` 與 `ch4=50%/1000Hz/ctrl=0` 都是**模組端既有狀態**,
  可能是廠商工具設的、或改 slave 號(6→9)之前留下的。

**per user:左右兩顆風扇共用 CH1;CH2/3/4 沒接東西、不管。** → 無害,結案。

**順手改善**(`cmd_pwm_status`):輸出尾端新增 `active_ch=1 (ch2-4 unused: module-side residue…)`。
📌 理由:原本四個通道平鋪、沒有任何線索說明只有 ch1 是活的 ——
**我就是因為看到 `ch3=11` 超出上限而以為是缺陷,追了一輪才確認無害。**
⚠️ **只新增欄位、不動既有欄位格式**,避免打壞既有解析。實機驗證輸出正確。

📌 **通則:「狀態輸出把所有通道平鋪、但只有一個是活的」本身就是一個陷阱** ——
讀的人無從分辨「異常值」與「無關的殘留」。成本是一個欄位。

### ✅ `crane_cli_` 重連洗版已修 —— 改為「保留狀態轉換、限流重複失敗」

**問題**(2026-08-31 上機時親眼看到):吊機關機時 `reconnectLoop` 每 500ms 印兩行
(`reconnecting` + `reconnect failed`),**45 秒 = 107 組 / 214 行**,把 log 裡其他訊息整個沖掉。

📌 **刻意不採用 `set_quiet_reconnect_log(true)`**(`arm_cli_` 2026-07-23 / `depth_cli_` 2026-08-27
用的那個旗標):那兩者是「**已知還沒裝、永遠不會接上**」＝純噪音;
而**吊機離線是該被看見的事件**,靜音會把該看的一起藏掉。

**做法**(改在 `transport/TCP_client.cpp` 的 `reconnectLoop`,對所有實例生效):
- 頭 `RECONN_LOG_BURST`(=3)次照原樣逐次印 → **短暫抖動仍然完整可見**
- 之後每 `RECONN_SUMMARY_MS`(=30s)一行摘要:**已嘗試 N 次 / 離線 X 秒**
- **成功一定印**,並帶上**斷線時長與嘗試次數**
- 狀態存在 `reconn_fail_streak_` / `reconn_down_since_ms_` / `reconn_last_log_ms_`
  （只由 `reconnectLoop` 單一執行緒讀寫，不需同步）
- `quiet_reconnect_log_` 的語意完全不動(arm / depth 仍是全靜音)

**實機雙向驗證**(條件與當日下午完全相同:吊機關機、本體對它重連):

| | 舊版(45s) | 新版(100s) |
|---|---|---|
| `reconnecting` | ~107 | **3** |
| `reconnect failed` | ~107 | **3** |
| 30s 摘要 | — | **3** |
| **總行數** | **214** | **9** |

恢復訊息:`reconnect success (斷線 175.5s, 嘗試 345 次)`;`crane_attached=on`,重連行為本身未受影響。

🔴 **診斷價值反而增加**:「離線多久 / 試了幾次」正是**舊版洗版時看不到的東西**——
它每 500ms 印一次,卻沒有任何一行告訴你累計狀態。
📌 **這條的通則:洗版的解法不是靜音,是換一個「每行都帶新資訊」的輸出設計。**

### ✅ ⑨b 已修 —— 但**同時推翻了它自己宣稱的嚴重性**

**修**:`cmd_arm_sweep()` 進場補 `abort_flag = false`(取得 `motion_mtx_` 之後,比照姊妹函式
`cmd_arm_clean_sweep_dry`;`cmd_side_measured` 的同型修正是 `28dfa30`)。
📌 `do_arm_sweep_` **只有一個呼叫端**(`cmd_arm_sweep:364`),所以放在 `cmd_arm_sweep` 不影響其他路徑。

🔴 **但 ONBOARDING §1 / runbook §A2 宣稱的嚴重性是誇大的,已實測推翻**:
> 原文:「任何一次 `stop`／`emergency_stop` 之後,`arm_sweep` 會**永久**回 `ERR aborted`,
>       只能重開主程式才能恢復」

稽核 **全部 4 個**設 `abort_flag = true` 的位置 + 實機確認:

| 設 abort_flag 的位置 | 是否同時進 Error |
|---|---|
| `cmd_emergency_stop` | ✅ 有(`set_state_(State::Error)` 在函式**最後一行**) |
| `imu_monitor_loop_` | ✅ 有 |
| `cmd_shutdown` / `stop()` | 收工路徑,不適用 |

- **`cmd_reset` 會清掉 `abort_flag`** → Error → `reset` 就恢復,**不需要重開程式**
- 而且 `cmd_arm_sweep` 開頭的 `State::Error` 檢查會**先**攔下來,`abort_flag` 那條**根本走不到**
- 實機:idle 時送 `emergency_stop` → **`state=error`** → `reset` 回 `OK reset`

→ **目前沒有已知可達路徑**會讓 `abort_flag` 停在 true 而狀態不是 Error。
📌 **仍然修的理由**:姊妹函式都有、只有它沒有＝不一致;成本一行,防的是未來新增
「設 `abort_flag` 但不進 Error」的路徑。**但不該再宣稱它是「永久卡死」。**

### ⚠️ 自記:這一輪我又用了有缺陷的方法,兩次

1. **稽核用固定行數窗口** —— 先用「函式起點 +14 行」判斷有沒有 `abort_flag = false`,
   結果窗口從 `cmd_arm_sweep`(9 行)**溢進下一個函式**,把姊妹函式的那行算到它頭上 → **假 ✅**。
   ✅ 改成用「下一個函式起點」界定真實範圍才得到正確答案。
2. **同樣的錯又犯一次** —— 用 `sed -n '3429,3446p'` 看 `cmd_emergency_stop` 有沒有設 Error,
   而 `set_state_(State::Error)` 在 **3448**,差兩行。因此推論出「emergency_stop 不設 Error →
   idle 時 abort_flag 會卡住」這個**錯誤的可達路徑**,實機一測就被推翻。
🔴 **通則:用固定行數的窗口去判斷「某段程式碼裡沒有 X」是不可靠的 —— 只能證明「窗口內沒有」。**
   要斷言「沒有」,範圍必須由語法界定(函式邊界),或直接對整個檔案 grep 再看歸屬。

### ✅ 交替步伐 / 跨障礙已停用(2026-08-31 實作 + 實機驗證)

**觸發這件事的關鍵發現**:`command/dispatcher.cpp` 的 `run` 與 `run_script` **預設 gait 都是 `"alt"`**
→ **`run 10` 這種最自然的下法就會走到危險路徑**。不是只有手動指令,自動循環與腳本都是。

**受影響的完整清單**(先前只找到一半):

| 路徑 | 走哪支 | |
|---|---|---|
| `run` / `run_script` **預設 alt** | `do_step_down_/up_` | 🔴 |
| `step_down`/`step_up` + `_with_sweep`/`_sweep_after_feet`/`_sweep_ba` | `do_step_down_/up_` | 🔴 |
| `cross_obstacle_down`/`_up` | `do_cross_obstacle_` | 🔴 **同一個模式** |
| `*_sync`、`run ... sync` | `do_step_sync_` | ✅ |

🔴🔴 **`do_cross_obstacle_` 上方那句註解是本次最危險的東西**:
> 「The anchor valve is NEVER toggled here … (**shared** per-side valve stays ON)」

**作者寫了 "shared"、知道閥是共用的,卻推出相反的結論。** 程式碼關的是「移動側」的 `valve_ch`,
而因為閥共用,那**就是**錨定側的閥。**一句明確保證「絕不會發生」的註解,寫在唯一會發生的那行上方**
——而且是在把身體撐離牆面到 2× 腳長、最不穩定的構型裡。註解已就地更正。

**實作(五處)**:
1. `do_step_down_` / `do_step_up_` / `do_cross_obstacle_` 進場守衛(第二道,涵蓋 `run_script` 直接呼叫)
2. **`command/dispatcher.cpp` 分派層攔截 10 個指令名**(第一道)
3. `dispatcher` 的 `run` / `run_script` 預設 gait `"alt"` → `"sync"`
4. `cmd_run` / `cmd_run_script` 收到 `gait=="alt"` 立即回錯

📌 **為什麼一定要有分派層那一道**:`cmd_step_*` 會**先 `set_state_(State::Running)` 才呼叫 `do_step_*_`**,
只靠深層守衛的話,ERR 會讓機器停在 `State::Error`——**把「指令被拒絕」偽裝成「動作失敗」**,
要 `reset` 才能繼續,而且會害人去查根本不存在的硬體故障。**第一版就是這樣,實機測出來才改的。**

**實機驗證(8 條危險路徑全擋、狀態機零變動)**:
`step_down` / `step_up` / `cross_obstacle_down` / `cross_obstacle_up` / `step_down_with_sweep` /
`step_up_sweep_ba` → 皆回 `ERR alt_gait_disabled_single_valve — use *_sync`,**無 EVT state_changed**;
`run 3 10 down alt` / `run_script alt 10,10` → `ERR alt_gait_disabled_single_valve (use gait=sync)`。
🟡 **訊息小瑕疵**:`cross_obstacle_up` 的建議寫成 `step_down_sync`(三元判斷只比對 `step_up` 前綴)。

### ⚠️ 我把一個會動的指令放進驗證清單(自記)

驗證時把 `step_down_sync 10` 放進「安全路徑」那組 —— **它是安全的 gait,但它是真的會動的指令**,
當場就開始跑。機器在地上、立刻 `emergency_stop` 攔下,四顆吸盤停在 `PUSHER_RETRACT_PULSE=300`(0.1cm)。
📌 **「安全」指的是這條路徑不會放掉四顆真空,不是「執行它不會有動作」。驗證清單只該放預期被拒絕的指令。**

**但這次意外跑起來反而正面驗證了兩條先前沒驗過的安全行為**:
1. **吊機連不上時 `PAUSE-ON-ERROR` 停下來等指示,沒有硬闖**
   (`crane_cmd 'pay_out 10' reconnect failed` → `[PAUSE-ON-ERROR] step_down_sync_crane_move`)
2. **abort 路徑會把風扇關回去**:`[pwm_step] step_abort_off -> ch1 duty=5.0% OK`,
   `pwm status` 確認 `ch1=5`，**且使用者現場確認風扇實體有轉、之後停了**。

### 📌 風扇接線確認(per user)

- **左右兩側風扇共用 `CH1`** → `PWM_STEP_CH = 1` 只寫一個通道是**正確的**,不是漏驅動。
- 🟡 **`ch3=11` 未解**:`pwm status` 顯示 `duty_min=5 duty_max=10`,而 ch3 停在 **11**,超出宣告上限。
  非風扇通道,本次未追。

### 🔴 待決定(使用者)

- ✅ **已處置(2026-08-31):三支全部停用,見上方「交替步伐 / 跨障礙已停用」。**
  ⚠️ 原碼一行未動,之後要正式移除或改寫成單閥架構時仍在原地。
  📌 **跨障礙不需要替代方案**——per user,現行跨障礙就是一般移動:4 顆輪子(**有避震器**)
  貼住玻璃、開風扇、吊機放繩滑過去,**沒有專屬動作**。`do_cross_obstacle_` 實作的是一個
  **已經不存在的能力**,擋掉它不損失任何東西。
- ~~**`do_step_down_`/`do_step_up_` 要怎麼處置?** 三條路:~~(已決,保留選項供日後正式清理參考)
  ① **停用/移除**(v2 只走 `do_step_sync_`,交替步伐是 v1 遺留)
  ② **改成純機械放開**(不碰閥,只縮回移動側推桿讓它失去接觸)——但 `CH_BREAK_VACUUM` **也是全域的**,
     破真空一打就是四顆一起,同樣不能用來只放開一側
  ③ **加硬體**(每側各一顆閥)才能讓交替步伐真正可用
  ⚠️ **在做出決定之前,不要在懸吊狀態下呼叫 `step_down`/`step_up`**(非 `_sync` 版)。
- **`cmd_vacuum(group, on)` 接受 `right`/`left` 但實際是全域** → 手動指令同樣誤導,
  至少該讓它對非 `feet`/`all` 的 group 回明確錯誤,而不是靜默地操作全部。

### 🟡 ③ 尚未驗的那半:真空密封行為

檢查表 ③ 原本要看的是「放開一側時**另一側兩顆是否都還吸著**」。
🔴 **這條在現行硬體上問錯了問題**——單閥單泵,放開一側必然放開四顆(見上一節)。
本次吸盤懸空,真空行為未驗;但**即使驗了也只會重現硬體限制,不會驗到左右歸屬**。

🔴 **但歸屬確認之後,有一條決策現在該收了**:`group_seal_ok_` 目前是
**「4 顆有 2 顆吸住就算 OK」**——那是**左右歸屬錯誤時期的權宜之計**
(當時「一組」根本橫跨實體左右兩側,只能放寬到看總數)。
**歸屬既然已經修好並實機驗證,那個前提就消失了**,判準應該改回**「每側各 ≥1」**才有物理意義:
現在的判準允許「右上+右下都吸住、左邊兩顆全空」通過,而那正是最危險的狀態。
⚠️ **這是使用者決策**(待辦表既有條目),但**依據已經齊了**。

### 🔴 ZDT 有編碼器、DM2J 沒有 —— 兩軸的可觀測性天差地遠

同一個問題(「它真的走了那麼多嗎」),兩軸的答案來源完全不同:

| 軸 | driver | 回授 | 失步能否在軟體偵測 |
|---|---|---|---|
| 上滑台 | DM2J_RS570 | **無**(開迴路步進) | 🔴 **結構上不可能** —— 位置暫存器是累計命令脈衝數 |
| 吸盤推桿 | ZDT | **有**(`real_pos` / `pos_error` / `stall_flag`) | ✅ 可以,本次實測位移誤差僅 7 脈衝 |

📌 **這解釋了為什麼 ① ② 只能靠尺、而 ④ 有兩個獨立證據**:
④ 的編碼器讀數獨立於 `CUP_PULSE_PER_CM`(它量的是角度),所以能單獨證明「沒失步」;
常數對不對仍只有尺能答(命令與編碼器換算共用同一個常數,自己驗不了自己)。
🔴 **排實機驗證時要先問這個軸有沒有回授** —— 有回授的可以讓程式自己驗,沒有的每一次都要有人拿尺。

**ZDT 換算實測**:10 脈衝/度(3600 脈衝/圈),由 ⑦ 的四組 log 反推
(36000/3601.71、39000/3901.87、42000/4201.85、45000/4501.76 全部落在 9.995~9.996)。

### 🔴 `do_arm_sweep_` 會開滾筒刷(排 ② 之前要知道)

`do_arm_sweep_` 第一件事是 `pqw_.controlRelay(CH_BRUSH, true)`,而 `6523b54` 把 `CH_BRUSH` 由 **15 改成 5**。
程式面已確認 **CH5 在標頭裡沒有跟任何其他常數撞號**(CH1 吸盤閥／CH2 真空泵／CH5 刷／CH6 破真空／CH14 水泵),
⑦ 的 log 也實際看到 `CH6 ON/OFF` 走破真空,對得上。
🟡 **實體接線上 CH5 是不是真的接滾筒刷,仍只能現場確認**——runbook 那條待辦尚未結案。

### 🔴🔴 過程中挖到的一條（比 9 條裡的 ⑤ 更具體）

**吊機計米器出現過一次「兩側同時 `length=ERR`」，而且沒有任何訊息說明原因。**

- 觸發當下：吊機連續跑約 15 分鐘後，`pay_out_right 1` 回 `ERR meter_right_read_fail`，
  `status` 顯示 `length_left=ERR length_right=ERR`，但 `dev_meter_left/right=1`（裝置在、scale 讀得到）。
- 🔴 **不要重複我走過的兩條錯路**：
  - **「新幀驗證擋掉每一筆讀取」是錯的** —— 之後三次重啟（含關 debug）全部正常，**不可重現**。
  - **`stop`／`CRANE_DRIVER_DEBUG` 都不是變因** —— 各自做過隔離測試，前後長度不變。
- 📌 **`length_valid` 不會鎖死**（`meter_read_robust`：`if (!valid_flag.load()) return {true, v1, false};`
  ——失效後第一筆無條件接受）→ 要維持 ERR 好幾秒，必須是**兩側同時持續硬失敗**。
  兩支 SD76 共用 gateway `cli_M` ＝ 與「gateway 層級的間歇故障」一致。
  同一輪開 debug 也確實看到 `[SE3:1] readParam comm fail`／`clearAlarm failed`（gateway A）。
- 🔴 **真正該修的是「看不見」**：`user_lib/log_utils.h:88` 的 `LOG_ERR` **被 `debug_mode` 蓋住**，
  而 SD76 新驗證的五條拒絕理由（slave mismatch／FC／byteCount／truncated／CRC）**全部走 `LOG_ERR`**
  → **預設情況下一條都不會印**，只剩一個沒有理由的 `length_*=ERR`。
  ⚠️ 第 ⑤ 條的整個用意是「把問題變可見」，但診斷訊息本身藏在預設關閉的旗標後面
  ——**這條下次再犯就一樣查不出來**。
  建議：把這五條拒絕改成**不受 `debug_mode` 管的限流計數**（例如每 60 秒摘要一次 + 累計數進 `status`）。

### 🟡 另一個待釐清（不是缺陷，先記著）

`pay_out_right 1` 回報 `moved=2cm`，但 `length_right` 由 `-245` 變 `-236`＝**9 個單位**
（`meter_right_scale=0.5`）。減速滑行是合理解釋（`CMD_MEASURED_APPROACH_CM=8`，
而目標只有 1cm ＝ 比減速距離還短），**但單位換算未經驗證，不下定論**。
→ 依賴小增量步進之前值得先弄清楚。

### 待完成（本次上機剩下的）

- 🔴 **階段 B（拿尺量）／階段 C（交替步伐）全部未做**。階段 C 的 ③ 是最危險的一條
  （吸盤左右歸屬 `右={5,7}／左={6,8}`，**程式從未在機器上跑過交替步伐**），
  runbook 要求**先在低處、機器不吊在半空中**跑第一次。
- 🟡 階段 B 的 ② 要用 **500rpm**（per user），且**參考點不能用機械限位**
  （限位會把失步藏起來）——要在行程中段做記號。
- 📌 兩支程式**目前仍在跑**（本體 `:5001`／吊機 `:5002`，`~/bringup/` 的 `-drv` 版）。
  收尾方式同 §A4：`echo exit > ~/bringup/drv_in`／`crane_drv_in`，再清 `sleep infinity`。

---

## 2026-08-31 — 機器讓出來，`main` 分支端到端跑起來（暫時測試用）

### 已完成

- **兩台 Pi 都在 `main` 上跑起來了**，GUI 端到端通：吊機 `:5002`／本體 `:5001`／GUI `:8080` 全 LISTEN，
  本體 log `[OK] crane 192.168.5.25:5002`，`status` 回 `OK state=idle crane_attached=on … imu_guard=on`，
  GUI 首頁 HTTP 200／47KB。**目的是暫時測動作，不覆蓋任何現有部署**
  （另開 `~/main_20260831/`，`~/bringup` 與 `~/projects` 都沒動）。
- **收尾：程式全部停掉，建置整個保留**（per user「之後可能會用到」）。
  走 console `exit` 的正規關機路徑（本體 `cmd_shutdown()`+`stop()`、吊機 join 執行緒後 `allMotionOff()`），
  不是 `kill`；三個埠都確認關閉、FIFO 的常駐 writer 也清掉了。
  保留位置：`.26:~/main_20260831/`（5.6M，**`CRANE_IP` 已 patch 成 `.25`**）／`.25:~/main_20260831/`（3.5M）。
  **啟動與收尾方式的權威版在 `runbook.md` §A4**，下次不必重編。
  ⚠️ `[SHUTDOWN] stopping...` 印出來不等於結束——本體約 5 秒、吊機約 10 秒才真的退出，要看 `ss -ltn`。
- **建置的是 `origin/main` `6523b54`**，不是本機 `main` `e3c8820`（純落後 2 個 commit、無分岔）。
  本機 `main` **刻意沒有 fast-forward**——動 git 狀態不在這次的要求裡。
  用 `git worktree` 開在 `/mnt/agent_ai/.tmp/facade-main-wt`（已於收尾移除）。
- 本體 14 個 TU、吊機 10 個，**兩邊都是零警告零錯誤**，產物時間戳與 md5 都逐一確認過。
- 吊機 `init()` 五個網關全過（`.30`／`.31` SE3、`.34` SD76、`.32`／`.33` X518）、
  VFD keepalive 10/10 `fail=0`；本體 `init()` **逐項與 runbook 的驗收表一項不差**。

### 🔴 這輪挖到的四個坑（權威版已寫進 `runbook.md`）

1. 🔴🔴 **吊機 WiFi IP 從 `.17` 漂到 `.25`，而 `CRANE_IP` 是編譯期常數、無 env 覆蓋**
   （`WASH_ROBOT.h`，全檔只有 `WR_DRIVER_DEBUG` 一個 `getenv`）→ 只能改碼重編。
   症狀是 `init()` 印到 `[--] DY-500 …` 後**整個停住約 2 分鐘**、5001 遲遲不開：
   `crane_connect_if_needed_()` 是 blocking `connect()`，對**不存在的主機**（丟包、無 RST）
   會卡滿 TCP SYN timeout。**runbook 原本寫的「連不到會 WARN 但不擋 boot」只在對方送 RST 時成立**
   （`127.0.0.1` 的 arm／depth_cam 就是瞬間 WARN）。
2. 🔴 **`main` 的目錄結構與重構分支完全不同**（無 `app/`／`command/`／`mechanism/`／`transport/`），
   A2 §1 那兩條建置指令**套不到 `main`**。已在 runbook 新增 §A4 放 `main` 專用的建置與啟動。
3. 🔴🔴 **stdin 給 `/dev/null` 會讓程式跑完 `init` 就自己關掉**——兩支 `main()` 結尾都是
   `while (getline(cin, line))`，EOF 即 `[SHUTDOWN]`。而因為 `init` 全 `[OK]`，**log 看起來像成功**。
   已改用 FIFO + 常駐 `sleep infinity` writer（`launch.sh`，兩台都佈好了）。
   ⚠️ 另外 **`stdbuf -oL` 不能省**：stdout 導檔是全緩衝，沒有它 log 會停在 0 bytes 像卡死
   （這輪就這樣誤判了一輪）。
4. 🔴 **兩台 Pi 都沒有裝 `tmux`** → `scripts/wr.sh`／`crane.sh` 這兩支 launcher 目前跑不起來。
   runbook §A 之前寫的一鍵啟動流程**現況是不可用的**。

### 待完成

- 🔴 **`CRANE_IP` 應該外部化**（env 或 config 檔）。重構分支的階段 4 已經在做機構標定外部化，
  這條可以一起收——不然吊機每漂一次 IP 就要重編一次。
- 🔴 **blocking `connect()` 應該帶 timeout**。「lazy connect 不擋 boot」的設計意圖是對的，
  但只在對方送 RST 時成立；主機整台不在時它擋 boot 兩分鐘，而且沒有任何訊息說明在等什麼。
- 🟡 本次跑的是 `WR_DRIVER_DEBUG=ON`（launcher 沒帶環境變數）。idle 時零成長，
  但**測動作時會出 hex dump**。要關就重啟：
  `WR_DRIVER_DEBUG=0 bash ~/main_20260831/launch.sh facade_cleaning_v2.out run.log wr_in`
- 🟡 `~/.ssh/config` 的 `IdentityFile` 是列舉式的，已補 `192.168.5.25`。
  金鑰 08-31 更名為 `claudeuser`（非 SSH 預設檔名）之後，**沒列到的位址一律 publickey 拒絕**，
  症狀長得像帳號壞掉。舊 config 備份在 `~/.ssh/config.bak-20260831-crane`。

---

## 2026-08-30（續）— 逐條驗證預期差異：抓到 0/4，並挖出一個真缺陷

### 🔴 結果很不舒服但很重要：現有腳本一條預期差異都偵測不到

新增 `harness/expected_diffs.sh` —— **逐條反轉**每條預期差異，看 harness 抓不抓得到。
（不用中間基準比：那些 commit 沒有 harness 的儀器，要一個個 cherry-pick；
而且它回答的是「那個 commit 當時長什麼樣」，不是**「現有腳本偵測得到嗎」**。）

`smoke.txt`，基準軌跡 150 筆 → **抓到 0 ／ 覆蓋缺口 4**：

| 條 | 結果 | 原因 |
|---|---|---|
| ① 上滑台 7.731 換算 | 抓不到 | `arm_sweep` 中止，DM2J **完全沒被碰到** |
| ② `ARM_SWEEP_RPM` | 抓不到 | 同上 |
| ③ 吸盤左右歸屬 | 抓不到 | RF/LF 是**步態**用的，`zdt_pusher` 直接指定從站 |
| ④ `CUP_PULSE_PER_CM` | 抓不到 | `zdt_pusher` 走 **preset 脈衝數**，不經過 cm 換算 |

📌 每條原因都合理，合起來的意思是：**`smoke.txt` 幾乎沒碰到任何一條預期差異所在的路徑。**
它驗到的第 7 條之所以看得見，是因為那條在基準上讓指令**根本不能執行** ——
那是最容易看到的一種差異，也是**最不需要 harness 幫忙**的一種。

🔴 **所以先前那些「✅ 等價」的綠燈，保護範圍比看起來小得多。**
⚠️ 另有一條**結構上永遠測不到**：driver 的回覆驗證只在**壞幀**時才有行為，
而 harness 的假從站永遠送好幀 → 那條要靠 `Linux_test/fake_slaves/`，兩套工具互補。

### 🐛 追覆蓋缺口時挖出一個真缺陷：`cmd_arm_sweep` 沒有重置 `abort_flag`

`arm_sweep` 一律回 `ERR aborted`，DM2J **一筆交易都沒有**，而且**完全沒有任何輸出**。

逐層追下去：`try_or_pause_`（`WASH_ROBOT.h:1710`）的第一行就是
`if (abort_flag.load()) return true;` —— **在呼叫 fn() 之前中止，靜默**。
決定性實驗：先跑 `arm_clean_sweep_dry`（它**有**重置 `abort_flag`，
`WASH_ROBOT.cpp:5539` 的註解寫著「比照 `do_step_sync_`：進入點清掉上一次殘留的 abort」）
再跑 `arm_sweep` → **DM2J TX 從 0 變成 6**。

🔴 **`cmd_arm_sweep()`（`:5525`）沒有那一行，姊妹函式有。**
**與 2026-08-28 修的 `cmd_side_measured`（`28dfa30`）是同一族缺陷** ——
「被 stop 過一次後，這個指令永久回 `ERR aborted`」。
⚠️ 而且症狀是**沒反應**：`try_or_pause_` 的中止路徑一個字都不印。

📌 **追錯過兩次，兩次都靠「先查再說」擋下**：
先懷疑 `std::atomic<bool> abort_flag;` 宣告沒給初始值（C++17 下 `atomic` 預設建構
是不確定值）—— **查了建構子，有 `: abort_flag(false)`，懷疑不成立**。
再懷疑 `arm_sweep_obstacle_pending_` 未初始化 —— **也在建構子裡**。
兩次都差一點寫成結論。

### 修好腳本後的最終結果：`rail.txt` 抓到 1/4

`rail.txt` 加上 `arm_clean_sweep_dry` 前置之後重跑（基準軌跡 31 筆）：

| 條 | `smoke.txt` | `rail.txt` |
|---|---|---|
| ① 上滑台 7.731 換算 | ❌ | ✅ **抓到**（位元組 6 行差異） |
| ② `ARM_SWEEP_RPM` | ❌ | ❌ |
| ③ 吸盤左右歸屬 | ❌ | ❌ |
| ④ `CUP_PULSE_PER_CM` | ❌ | ❌ |

📌 **② 抓不到本身有訊息**：`arm_clean_sweep_dry` 會掃上滑台（所以 ① 看得到），
但 `ARM_SWEEP_RPM` **只有 `arm_sweep` 本身在用** —— 而 `arm_sweep` 因為
`abort_flag` 那個缺陷仍然中止。**修好那個缺陷會同時解鎖 ②。**
③④ 如預期：`rail.txt` 完全不碰推桿與步態。

### ✅ 再推進：`init` 當前置之後，②也抓到了 → **4/9**

`rail.txt` 改成 `init` + `arm_sweep`（基準軌跡從 31 → **89 筆**）：

| 條 | 結果 |
|---|---|
| ① 上滑台 7.731 換算 | ✅ 抓到（位元組 2 行差異） |
| ② `ARM_SWEEP_RPM` | ✅ **抓到**（位元組 4 行差異） |
| ③ 左右歸屬 ／ ④ `CUP_PULSE_PER_CM` | ❌ 仍抓不到 |

🔴 **關鍵是找到對的前置指令**：`cmd_init_impl_()`（`:5230`）**同時**做兩件事 ——
重置 `abort_flag`（解鎖 `arm_sweep`）＋ 把狀態設成 `Ready`（解鎖 `attach`）。
⚠️ `init` 本身回 `ERR imu_baseline_fail`，但 `abort_flag = false` 在**失敗之前**執行 ——
所以 `arm_sweep` 拿到 `OK arm_sweep_done`、13 筆 DM2J TX。**副作用剛好是我們要的。**

📌 順帶實測到 ① 的真實形狀：`PR_move_cm_nowait 17.000 cm -> 21989 pulses (lead=7.7310)`。
反轉成 1.0 會變成 170000 脈衝 —— **而滑台只有 50cm**。這就是那個四個月沒人發現的缺陷。

### 🔴 ③④ 為什麼仍抓不到（已查清，非猜測）

- **③ 左右歸屬**：`ZDT_RF/LF` 是**步態**（`do_step_down_`/`do_step_up_`）與 `attach` 的分側判準用的。
  `attach` 需要 `State::Ready`，而 `init` 因 IMU 基準線失敗沒走到 `set_state_(State::Ready)`（`:5326`）。
- **④ `CUP_PULSE_PER_CM`**：只在**深層路徑**用到 —— `cm_to_pulses_for_slave_` 的三個呼叫點都在
  seal 迴圈裡（障礙物回退邊界 `:4186/4190`、伸出上限 `cap` `:4723`）。
  `zdt_pusher extend` 走 **preset 脈衝數**，除非移動逼近 cap，否則反轉不會有可觀察差異。
  **這條天生難測**，不是腳本寫得不好。

### 🐛 IMU 假序列埠：機制對了，但主程式收不到（未解）

`fake_serial.py` 改成送**固定筆數**的有效 WT901 幀（`0x55 0x53` + 8 個 0 + checksum `0xA8`）。
**單獨測試 PTY 完全正常**（讀到 220 bytes、幀正確），但主程式那側 `imu_take_baseline_`
仍然 `n == 0`。已排除的：
- ✅ PTY canonical 模式 —— 已 `tty.setraw()`，且 `Serial_port::init` 自己也清了 `ICANON`（`:262`）
- ✅ `ISTRIP` 砍掉 checksum 高位元（`0xA8`）—— 8N1 走 `:250` 的 else 分支，`ISTRIP` 是清掉的
- ✅ 寫入時機 —— 已改成延遲 3s 後分批寫，仍然無效

🔴 **還沒找到真因。** 這是 ③ 的唯一阻擋點。

### ✅ 最終：`full.txt`（`init` → `attach` → `arm_sweep`）抓到 3/4 → **5/9**

修好假 IMU 之後，`init` 成功 → 狀態進 `Ready` → `attach` 成功，基準軌跡 **185 筆**：

| 條 | 結果 |
|---|---|
| ① 上滑台 7.731 換算 | ✅ 位元組 2 行差異 |
| ② `ARM_SWEEP_RPM` | ✅ 位元組 4 行差異 |
| ③ 吸盤左右歸屬 | ✅ **位元組 52 行差異**（最大的一個，符合「31 處使用點」） |
| ④ `CUP_PULSE_PER_CM` | ❌ 仍抓不到 |

🔴 **④ 是天生難測，不是腳本寫得不好**：`cm_to_pulses_for_slave_` 的三個呼叫點
全在 seal 迴圈的**深層邊界**（障礙物回退 `:4186/4190`、伸出上限 `cap` `:4723`），
只有當移動逼近那些邊界時才有可觀察差異。

### 🐛 假 IMU 的真因是「視窗」不是「時機點」

`imu_take_baseline_` 的取樣視窗是 **3 秒**（`IMU_BASELINE_SEC`），而假 IMU 原本
一次爆發送完 400 幀（約 0.3 秒）—— **資料落在視窗外**，`n == 0`。
改成 `delay 0.5s + 4000 幀分批`（橫跨約 3 秒）之後全部打通。

⚠️ 追這條排除過**三個錯誤假設**，每一個都查了才放棄：
PTY canonical 模式（已 `setraw`，且 `Serial_port::init` 自己也清 `ICANON`）／
`ISTRIP` 砍掉 checksum 高位元 `0xA8`（8N1 走 `:250` 的 else 分支，`ISTRIP` 是清掉的）／
寫入時機太早（延遲後仍無效）。

🔴 **但按時間餵資料立刻引入非確定性**：`status` 印的 `n_angle` 兩次是 320 vs 256。
→ 新增 `normalize_replies.py` 遮蔽 `n_angle=` / `n_accel=` ——
它們與時間戳同一類：**量的是「跑了多久」，不是「做了什麼」**。
⚠️ 代價寫在檔頭：IMU 讀取量本身不再被比對。**排除清單刻意做得很短，每加一個欄位就少一分保護。**

### 🔴 這套 harness 的結構性上限（比覆蓋率更根本）

harness 刻意提供一個**什麼都不會壞**的環境 —— 那正是等價比對需要的（兩邊走同一條路徑）。
**但它同時代表：所有「錯誤路徑」的改動在這裡都不會有可觀察差異。**
⑤ 回覆驗證（只在壞幀時）／⑥ `SO_ERROR`（只在連不上時，且差異只在 log）／
⑧ DM2J `void→bool`（讓寫入失敗被偵測，但這裡不會失敗）／⑨ 張力警示（吊機端）——
**四條都是這一類。**

📌 **這不是缺口，是分工**：`harness/` 測正常路徑，`Linux_test/fake_slaves/` 刻意送壞幀測錯誤處理。
**完整的等價性證明需要兩套一起跑。** 只跑 `harness` 而宣稱「等價」，
等於宣稱「**正常情況下**等價」—— 那是真的，但不是全部。已寫進計畫 §5.6。

### ✅ 08-30 收尾再推進：⑥ 納入保護 → **6/9**

`harness/check_so_error.sh`：把 `CRANE` 端點指到**沒人監聽**的埠，斷言 log 裡
`reconnect success` 出現 0 次。
🔴 **為什麼要獨立一支**：這條的差異**只出現在 log** —— 不在匯流排位元組、
也不在指令回覆，`compare.sh` 的兩個判準都看不到它。但「看不到」不等於「不重要」：
修正前實測**吊機關著卻印了 20 次 `reconnect success`**。
✅ 負控制：把 `SO_ERROR` 檢查拿掉重建 → 抓到 **19 次假成功**並正確變紅。

🐛 做這支時連踩兩個坑：
1. `grep -c` 對多檔會逐檔輸出「檔名:數字」，而 `|| echo 0` 在找不到時**再多印一個 0**
2. 🔴 `run_trace.sh` 硬設 `FCV_EP_*` **靜默蓋掉外部給的值** → 這支想指向沒人監聽的埠
   卻被蓋回 15002，連得上、一次 failed 都沒有。
   **症狀是「測試跑了但測的是別的東西」** —— 幸好腳本有「零筆資料不算通過」的守衛。
   → `run_trace` 的端點全改成 `${VAR:-default}`

✅ **順帶驗證互補軌道完好**：`Linux_test/fake_slaves/` 的 6 支測試程式在我改過
`log_utils.h` 的 `LOG_HEX` 之後**全部還編得起來**。

### 🔴 9 條預期差異的保護狀態盤點（誠實版）

| 條 | 狀態 |
|---|---|
| ① 上滑台 7.731 換算 | ✅ `full.txt` |
| ② `ARM_SWEEP_RPM` | ✅ `full.txt` |
| ③ 吸盤左右歸屬 | ✅ `full.txt`（位元組 52 行差異） |
| ⑦ `zdt_pusher` 範圍分岔 | ✅ `compare.sh` vs `main-final` |
| ⑥ `SO_ERROR` | ✅ `check_so_error.sh`（針對性斷言 + 負控制） |
| ④ `CUP_PULSE_PER_CM` | ❌ **天生難測** —— 只在 seal 迴圈的深層邊界用到 |
| ⑤ driver 回覆驗證 | 🔴 **結構上這裡永遠測不到**（假從站永遠送好幀）→ 靠 `fake_slaves/` |
| ⑧ DM2J `void→bool` ／ ⑨ `abort_flag`/張力警示 | ❌ 需程式碼層反轉 |

**＝ 6/9 在保護範圍內**（起點 1/9 → 2 → 4 → 5 → 6）。
🔴 **剩下 3 條**：④ 天生難測（seal 迴圈深層邊界）／⑤⑧ 是**錯誤路徑**，harness 的無故障環境結構上測不到 —— 那兩條靠 `Linux_test/fake_slaves/`（已驗證 6 支測試程式仍可建置）。 這個數字比「✅ 等價」誠實得多，也是後續該追的指標。

### 待完成

- 🔴 **`cmd_arm_sweep()` 補 `abort_flag = false`**（與姊妹函式一致）。
  ⚠️ 這是**行為改變**，會讓 `main-final` 上不能用的指令變成可用 → 依判準要進「預期差異」清單，
  等階段 −1 定下基準後再做
- 🟡 `try_or_pause_` 的靜默中止值得加一行診斷 —— 現在的症狀是「沒反應」，無從查起
- 🔴 **覆蓋缺口 4 條仍未關**：需要能走到步態（`step_down_sync`）與 cm 換算路徑的腳本


## 2026-08-30（續三）— 收尾：架構文件補齊、⑥ 納入保護、最終回歸乾淨

### ✅ 最終回歸：`main-final-harness` vs `HEAD`（全部重構之後）

判準 1（TCP 回覆）**完全相同**。判準 2 的差異**恰好兩行**，而且**數字本身就是證明**：

```
base:  ... 00 02 7B 0B  03 E8 ...   脈衝 162,571 ／ rpm 1000
cand:  ... 00 00 52 24  00 FA ...   脈衝  21,028 ／ rpm  250
```

**162,571 ÷ 21,028 = 7.731** —— **harness 從匯流排上的位元組獨立重算出了皮帶軸導程**。
rpm 1000→250 是預期差異 ②。

→ **差異 100% 是 ① 與 ②，沒有任何清單外的差異。**
階段 1~5 的全部重構（刪 3,993 行死碼、抽指令層、機構層三個增量、config 外部化、
拆 `WASH_ROBOT.cpp`）**對外行為逐位元不變**。

### ✅ 還了三筆架構文件的債

`CLAUDE.md` 的樹狀圖與根目錄盤點表**都沒有** `command/`、`mechanism/`、`config/` ——
我**連違三次專案自己的規則**「🔴 新增檔案必須在這裡加一列」，而架構描述還停在
「`WASH_ROBOT` 是一個檔」。下一個讀 `CLAUDE.md` 的人會拿到錯的圖。
→ 樹狀圖改寫成 **6 層 + 橫切**，每層標明存在理由；盤點表補三列。

### ⚠️ 一句該記住的

這一輪我在**同一天內**踩了三次「只驗到一端」：新增 `common/` 時、加 `-Imechanism` 時、
補 `CLAUDE.md` 時。第一次之後我還特地寫了「三個建置入口都要同步」。
📌 **寫下來不等於做到。** 真正擋住第二、三次的不是那句話，是**每次都實際跑一遍**
（三條 g++ 全部真編一次、`grep -c` 逐目錄數一次）。

### 收尾狀態

- **122 個 commit**（自 `main-final`），工作區乾淨
- 預期差異保護 **6/9**（起點 1/9）
- `tmp/` 累積 155MB 建置產物 —— 已 gitignore，**刻意保留**（下次接手可省一輪重建；
  要清就 `rm -rf tmp/`，不影響任何東西）
- 🔴 **下一步是上機**，照 `runbook.md` §A2 的逐條檢查表


## 2026-08-30（續二）— 上機前準備：修好會編不過的指令 + 逐條實機檢查表

**決定：先上機，不再往下重構。** 理由：那 9 條修正**一條都沒有實機證據**，而其中
三條需要有人在旁邊看著（本來就得排機器時間）；再往下重構只會讓「是重構搬壞的
還是那 9 條本來就錯」更難分 —— **那正是 08-29 放棄分兩段上機時付出的代價，
不該再付第二次**。harness 不會過期，機器時間會。

### 🔴 複核建置指令時抓到一個會讓上機直接卡住的錯

吊機的 g++ 是 `-Icommon -Itransport -Iuser_lib`，**缺 `-Imechanism`** ——
而 `Crane_control_PI/main.cpp` 現在 include `rope_axis.h`。**照那條指令上機編不過。**

📌 原因：`-Imechanism` 我只加進了 `harness/build.sh`，忘了 runbook。
**又是「只驗到一端」** —— 這個坑今天已經踩過一次（新增 `common/` 時），
當時還特地寫了「三個建置入口都要同步」。**寫下來不等於做到。**

✅ 三條建置指令**全部用真實編譯驗過**（不是只讀）：
吊機 455,280 bytes ／ 本體 1,278,064 bytes ／ `Linux_test` 682,712 bytes。

### 🆕 `runbook.md` §A2 新增「那 9 條的逐條實機檢查表」

每條寫明**看什麼／正常長什麼樣／不正常長什麼樣**。順序由「不會動的」排到
「會動的」，前面沒過就不要往下走：

| 階段 | 內容 |
|---|---|
| **A** 開機就看得到 | ① 標定字串／⑦ `zdt_pusher`／⑨a `abort_flag`／⑥ `SO_ERROR` |
| **B** 動單一軸拿尺量 | ① 實際位移／④ 推桿行程／② 失步 |
| **C** 交替步伐 | ③ —— 🔴 唯一一條「錯了會讓機器在貼牆狀態下放錯邊」 |
| **D** 觀察不是驗收 | ⑤ 失敗率基線／⑧／⑨c |

🔴 **兩個驗法陷阱寫進表裡**：
- **拿尺量兩次，不要看座標** —— 驅動器只數脈衝，失步時座標永遠顯示正確、一個字不說
- **「回 0 點正確」對失步沒有鑑別力** —— 上滑台零點是**機械硬限位**，
  不管失步多少都頂回同一位置。**參考點不能是限位本身**，要在行程中段做記號

⚠️ **已知未修也寫進去**：`cmd_arm_sweep` 沒重置 `abort_flag` → 任何一次 `stop`
之後 `arm_sweep` **永久回 `ERR aborted` 且完全沒有輸出**。遇到先 `reset`／`init`。

📌 明寫「**這次上機不驗重構本身**」，並給出異常時的嫌疑順序：
**(1) 這 9 條之一 → (2) harness 沒覆蓋到的路徑 → (3) 重構搬壞**。
前兩者機率高得多 —— harness 的天花板是 5/9，且**錯誤路徑結構上測不到**。


## 2026-08-30（續）— 階段 2 完成，階段 3/4/5 各推進第一批增量

**每一步都用 `compare.sh` 驗過，全部兩個判準相同。**

### ✅ 階段 2 完成：指令層抽出
`facade_cleaning_v2/main.cpp` **522 → 149 行**；`dispatch()` 373 行搬進
`command/dispatcher.{h,cpp}`，內容逐字不變，唯一改動是**把全域 `robot` 改成參數**
—— 這層不該知道「只有一台機器」。

### 🟡 階段 3：三個增量（吊機）
先做前置：**harness 原本只跑洗窗本體，動吊機而沒有驗證等於盲搬**。
擴充後涵蓋 `SE3:1`／`SD76:1,2`／`DSZL:1` —— 虛擬軸的全部三種裝置。
🔴 吊機 driver 原本全部寫死 `debug=false` → **一個 hex dump 都不會產生**，
判準 2 會是空對空的假通過。加了 `CRANE_DRIVER_DEBUG=1`（預設仍關）。

1. `RopeAxis` 型別化 + 3 個選邊點
2. **軸帶狀態**（`length`/`vfd_fault`/`manual_motion`/…）+ `resolve_meter_side` 收斂
3. `meter_loop` 的左右重複收斂成迴圈 —— 🔴 **順序必須維持左→右**，
   換順序就不再等價，真機上也會改變 bus 交易次序

📌 **這層的實際收益**：「忘記替另一側也做一次」從結構上變成不可能 ——
而那正是 08-28 吸盤左右歸屬那個缺陷的形狀。

### 🟡 階段 4：機構標定外部化，帶 provenance
`common/profile.h`（零外部相依）+ `config/axis_profile.txt`。
🔴 **設計規則沿用 `endpoints.h`：沒有設定檔時行為逐位元不變。**
🔴 **分兩份 profile 因為變更理由不同** —— 換一顆同型馬達不該把機構標定重置，
那正是 7.731 那個缺陷的形狀。
🔴 **12 個安全互鎖不 externalize**：**一個可以被人改錯的數字不是保護。**
⚠️ `.provenance` 不是裝飾：覆寫生效時連同來源一起印，沒有就印「⚠ 無 provenance」——
**看不到來源的覆寫本身就是警訊。**

### 🟡 階段 5：`WASH_ROBOT.cpp` 依語意分界拆成兩個 TU
**9,465 → 5,213 行** + `wash_robot_commands.cpp` 4,275 行。
分界是原檔**既有的** `//=========== commands ===========`，不是任意切一刀。
🔴 拆之前掃過「跨檔案會斷的東西」：匿名 namespace 的 `apply_to_atomic_`、
file-static `WEIGHT_NO_DATA_KG` —— 兩者使用點**全在前半**，故留在原檔。

🐛 **第一次做錯**：只挑 `#include` 行複製到新檔，把 `#ifdef _WIN32` 守衛丟掉，
結果在 Linux 上去 include `windows.h`。
📌 **抽「看起來相關的行」而不是「整個區塊」是典型的搬移事故。**


## 2026-08-30 — 階段 0 完成：等價比對跑通，差異 100% 歸因於預期差異

裝了 `g++-10` 之後把整套 harness 實際跑起來（`d62a6aa` `d4253bf`）。

### ✅ 結果

| 腳本 | 判準 1（TCP 回覆） | 判準 2（分裝置位元組） |
|---|---|---|
| `readonly.txt` | ✅ 完全相同 | ✅ 完全相同 |
| `smoke.txt` | 唯一差異＝ 4 條 `zdt_pusher` | 只有 `ZDT:5-8`／`JC100:5-8` |

`smoke` 的差異 **100% 歸因於預期差異第 7 條**（分派器收 1-4、應用層收 5-8 的範圍分岔）：
base 回 `ERR usage:zdt_pusher_<1..4>`、cand 回 `OK`。ZDT/JC100 的位元組差異全是它的下游。
**`PQW` / `QX` / `XKC` 完全相同。沒有任何清單外的差異。**

### 🔴 過程撞到的**四個儀器缺陷** —— 每一個都會在 diff 上偽裝成行為差異

1. **基準側用舊版非原子 `LOG_HEX`** → `XKC:13` 出現「只有 RX 沒有 TX」。
   **拿一份被打亂的量測去比一份乾淨的，比出來的差異是儀器造成的。**
   → log 原子性也 backport 進基準分支（純儀器、不動匯流排位元組）
2. **`normalize.py` 用 `^` 錨定** → driver 的 log 行被應用層 `std::cerr` 接在前面時
   （`[water_inlet] off attempt 2/3 failed: [00:19:31] [DBG] [XKC:13] TX ...`）
   **被靜默丟掉**，而丟掉的那一筆在 diff 上就是「這一側少一次交易」。
   `LOG_HEX` 已是原子的，但應用層的 `cerr << a << b << c` 不是。
   → 改 `search`，並把「像 hex dump 卻沒解析成功」的行**算出來報告**
3. 🔴 **收尾不是確定性的**：`crane_cmd_`／`arm_cmd_` 對連不上的位址**無界重試**，
   而關閉序列裡就有這些呼叫 → 程式被 kill 時跑到哪由時序決定。
   實測 base 停在 `00:19:31`、cand 停在 `00:19:57` ——
   **看起來完全像行為差異，實際只是誰多活了 25 秒。**
   → 起假吊機/手臂/depthcam（一律回 OK，讓那條路徑有界結束）＋ 送 `exit` 等自行結束，
     逾時就明說「這一輪尾端不可信」
4. **假從站回泛用亂值** → `pos_reached` 永不成立、真空永遠密封不了，
   每個運動指令等滿 15s 內部逾時。→ ZDT 狀態與 JC-100 壓力塑形

### 📌 這一輪的通則

**量測儀器必須兩側一致，而且收尾必須是確定性的。**
四個缺陷裡有三個的症狀完全一樣：diff 上出現「一側少了東西」。
而真正的行為差異在 diff 上長得**一模一樣** —— 分不出來的話，就會去查一個沒壞的東西。
📌 與本專案「結構檢查通過 ≠ 功能可用」是同一族：**驗證工具自己也要被驗證。**

### ⚠️ 誠實的限制：9 條預期差異只驗到 1 條，而且有結構性原因

被走到的第 7 條在基準上的表現是**「指令根本不能執行」**，
它**遮蔽了所有下游的預期差異** —— base 從來沒走到那裡，自然比不出換算（7.731）、
左右歸屬、回覆驗證那些差別。
🔴 要驗其餘 8 條，需要一個「該指令能執行」的中間基準
（例如 `0d5f6bc` 之後、`8c59eac` 之前的某個 commit）。

### 🔴 收尾時又踩一次「零覆蓋的假綠燈」——而且是我自己設計的測試

寫了 `harness/cmds/rail.txt` 想驗上滑台的 7.731 換算差異（基準是 1.0，同一個 cm 指令
送出的脈衝該差 7.7 倍）。跑完**兩個判準都 ✅**。

但 `DM2J:14` **一筆交易都沒有** —— `arm_sweep` 兩側都回 `ERR aborted`，上滑台根本沒被驅動。
**那個綠燈只證明了「兩邊都沒做事」。**

📌 這是同一個教訓的第三次：`test_qx_do24` 斷言寫反（一個檢查都沒跑到卻通過）→
`normalize` 空對空的假通過 → 現在這個。**共同形狀是「零筆資料的相等」。**

→ **修法不是修那份腳本，是讓假綠燈看得見**：`compare.sh` 現在強制列出
「實際走到的裝置與筆數」＋「OK/ERR 比例」，並註明**回 ERR 的指令等於沒走到那條路徑**。
綠燈不附上覆蓋範圍就無法被解讀。

（`rail.txt` 保留但標記走不通 —— `arm_sweep` 需要更多前置狀態。它記錄了「要驗哪三條
預期差異、為什麼要繞開 `zdt_pusher`」，那個資訊本身有價值。）

### 待完成

- 🟡 用中間基準驗其餘 8 條預期差異
- 🔴 階段 −1（HEAD 上機）仍等機器 —— harness 證明「行為沒被搬壞」，
  **證明不了「那 9 條修正在真機上是對的」**
- 🟢 階段 2（抽指令層）現在可以開工：工具齊了、基準線建立了


## 2026-08-29（續十二）— 清掉三件小的；過程中我自己踩了三十分鐘前才寫下的那個地雷

### 已完成（`cfb76f3`）

**① 三個 `*_WALL_MM` → 共同起點 `ARM_WALL_MM_DEFAULT`**
🔴 **先更正我上一輪的建議**：我說「讓三邊吃同一個常數」，讀完沿革發現那會**摧毀一個真實能力** ——
`DM2J_ARM_STEP_SWEEP_WALL_MM` 在 2026-07-24 建立時就是**刻意跟當時 330 的 `ARM_CLEAN_WALL_MM` 分開**，
三個歷史上真的分開調過。但最近兩輪（07-27、08-28）三個是一起改的，實務上已是同一個旋鈕。
→ 做法改成**「共同起點 + 可個別覆寫」**：改預設值三個一起動（符合最近的實際用法），
要分開就把該處換成字面值並寫明理由。順帶更正 `:900` 那句過期的「separate from ARM_CLEAN_WALL_MM(330)」。

**② `DISABLE_POS_ERROR_LIMIT_DEG` 補「後果」而非只標「未使用」**
🔴 也更正我上一輪講得過重：它其實已經標得很清楚。真正缺的是**後果**——
原註解說了常數沒被用，沒說「所以 obstacle 偵測現在**純靠相電流**，位置誤差那道閘**不存在**」。
不接上也不刪：接上會是新增 `main-final` 沒有的安全檢查＝功能改變，違反等價判準。

**③ 21 個同名巨集全部移除，要現值的地方明寫 `(settings_.xxx.load())`**
67 處替換、16 個巨集有使用點。之後常數名稱**在任何位置都只有一個意思**，歧義消失。

### 🔴🔴 過程中我自己踩了那個地雷 —— 而且是三十分鐘前才寫下的

**先是數錯**：我記「4 個巨集」，實際是 **21 個**。grep 要求名稱與 `(settings_` 之間恰好一個空格，
而那些是**對齊排版的多個空格**，只有 4 個長名稱剛好擠掉對齊。
📌 **今天第三次被 grep pattern 咬**（`!client\b` 誤判 8/10 支 driver、hex regex 造成空對空假通過、
現在這個）。**稽核工具的輸出是待查清單，不是結論** —— 這條 memory 今天應驗三次。

**然後真的做壞了**：第一次改的時候我用正規表示式刪掉一整塊 `#define`，
**把中間沒注意到的巨集一起刪了而沒有替換使用點**。後果是
`ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_HANGING`（繩重上限，安全互鎖）／`VACUUM_SEAL_DEEP_KPA`／
`DISABLE_RETRY_MAX_ITERS` 等**靜默從「執行期設定」變成「編譯期預設值」**。
🔴 **`cl /Zs` 完全通過、零警告** —— 正是這次改動要消滅的那個地雷形狀，我自己示範了一遍。

✅ **`harness/prove_noop.sh` 抓到了。** 重做之後，prove_noop 的差異**恰好只有 ① 的三行 WALL_MM**，
67 處巨集替換的預處理輸出**逐位元相同**。

📌 **這是那個工具第一次真正派上用場，而且救的是我自己。** 三十分鐘前寫它的時候，
理由是「純搬動的階段用它比 compare.sh 便宜」；實際價值是**它在我沒有察覺的時候攔下了一個
會靜默改變安全門檻的錯誤**。

### ⚠️ 順帶發現：5 個「設定了但沒有效果」的 setting

`PUSHER_EXTEND_BODY_PULSE{,_SHORT}`／`RETRACT_SLOW_PEEL_CM`／`STEP_CM_DEFAULT`／`STEP_MARGIN_CM`
的巨集**一處都沒被用到**。那些 setting **可以 `set_setting`、會出現在 `status`、會存檔**，
但**沒有任何程式碼讀它** —— 操作者改了會看到值變了，機器完全不理。
（`PUSHER_EXTEND_BODY_*` 是 v1 body 推桿殘留，v2 已無 body 推桿。）
🔴 **刻意不動**：移除可設定的 key 會改變指令介面＝功能改變，不是整理。

### 待完成

- 🟡 5 個「設定了沒效果」的 setting：要嘛接上、要嘛從指令介面移除。**兩者都是功能改變**，
  等階段 −1 上機驗證完、確定基準之後再處理
- 🔴 `sudo apt install -y g++`（階段 2 的前提）
- 🔴 階段 −1 上機


## 2026-08-29（續十一）— 三件不用機器的前置：常數盤點、覆蓋率、吊機拆解分析

### ① 225 個常數分類（階段 4 前置）—— 順帶抓到三件事

| 類別 | 數量 | 外部化政策 |
|---|---|---|
| **安全互鎖** | **12** | 🔴 **不外部化**——可以被人改錯的數字不是保護 |
| 機構標定 | 6 | ✅ 外部化，**必須帶 provenance** |
| 設備協定 | 25 | ✅ 外部化 |
| 流程參數 | 182 | ✅ 可外部化 |

🔴 **三個 `*_WALL_MM` 都是 400，零 `static_assert` 保證一致，而且歷史上已經分岔過**：
`:900` 的註解到現在還寫著 `— separate from ARM_CLEAN_WALL_MM(330)`，那個常數早就是 400 了。
`:1587` 寫「跟 `ARM_CLEAN_WALL_MM` 統一」——**那是人工同步的意圖，不是機制**。
📌 依踩坑索引：**修法不是把數字改對，是讓三邊吃同一個常數**。

🟡 `DISABLE_POS_ERROR_LIMIT_DEG = 5.0` **宣告了但全專案 0 處讀它**，卻擺在安全常數群裡
（註解自己標 `(currently unused)` 算誠實，但會讓人以為有這個保護）。

🔴 **4 個常數被「同名的 `#define`」遮蔽**（`:1308`~`:1323`），其中**兩個是安全互鎖**
（撞障礙物電流保險、繩重上限）。從 `.h` 讀到 `static constexpr double ... = 40.0` 會以為
那是生效值，實際上 `#define` 之後同名的東西是執行期可調的 settings。
✅ **今天沒有 bug** —— `#define` 之前的兩處用法正好都合理需要預設值
（`:75` 用常數初始化 settings、`:1198` 印「現值:預設值」），慣用法是刻意的。
🔴 **但它是埋著的地雷**：任何人在 `:1308` 之前新增引用，會**靜默拿到編譯期預設值而非現值**，
而安全門檻讀到預設值是錯的方向，編譯器不警告、執行期沒訊號。**階段 4 會正面撞上。**

### ② 指令腳本覆蓋率 9% → 55%

權威清單取自**分派器**（`facade_cleaning_v2/main.cpp`）而非 runbook §C1，共 **81 個指令**，
原本腳本只碰到 8 個。分成 `readonly`(12) / `motion`(34) / `settings`(17) / `smoke`(13)。
`emergency_stop` 刻意排在 `motion` 最後——它會讓狀態機進入需要人工復歸的狀態，
後面的指令全回 ERR，**那不是差異是設計**。

🔴 **36 個永久排除的逐條寫了理由**（寫檔案破壞可重現性／`shutdown` 終止行程／
要先處在特定狀態／整趟步態太長／攝影機路線實體未接），免得日後有人以為是漏掉。
⚠️ README 明寫：**55% 不代表 55% 的正確性**，而是「沒送到的 36 個指令，相關的重構改動
不會出現在軌跡裡，而 diff 依然是綠的」。

### ③ 吊機拆解分析（階段 3 前置）—— 虛擬軸其實已經存在

4,514 行：**37 個 `cmd_*`**（→ 指令層，本體是 84，兩支同樣的問題）／
**6 條背景執行緒**（→ 任務層常駐部分，其中 `hold_loop` 是**現成的安全層樣板**）／12 個 driver 實例。

🔴 **最強的證據**：以 `_left`／`_right` 成對出現的識別字**約 340 處** ——
`vfd` 86／`g_length` 48／`meter` 29／`dsz` 28／`hold_up` 28／`hold_down` 26／`g_dev_*` 74。
一條繩 = `vfd`（速度輸出）+ `meter`（位置回授）+ `dsz`（張力），
而且已經有 `resolve_meter_side()` 這種把 side 映射到裝置的臨時 helper。

📌 **機構層在概念上早就存在，只是拼寫成 `_left`/`_right` 後綴而不是一個型別。**
🔴 **不做的代價是可量測的**：每個新功能都得記得「左邊做一次、右邊做一次」——
08-28 抓到的「吸盤左右歸屬錯了四個月、每個分側判準實際上都在看『一邊各一顆』
＝等於沒有保護」，就是同一個 class 的缺陷在本體那邊的實例。

→ 階段 3 目標：抽 `RopeAxis` 兩個實例，**「側」變成參數而不是名字的一部分**。

### 待完成

- 🔴 `sudo apt install -y g++`（階段 2 之後的前提）
- 🔴 階段 −1 上機
- 🟡 三個 `*_WALL_MM` 收斂成一個常數 + 補 `static_assert`（小、獨立、可先做）
- 🟡 `DISABLE_POS_ERROR_LIMIT_DEG` 要嘛接上要嘛刪掉
- 🟡 4 個同名 `#define` 的慣用法，建議在階段 4 之前換成明確的 `settings_.xxx.load()`


## 2026-08-29（續十）— 階段 1 完成：刪 3,993 行死碼，而且是**證明**不是相信

### 已完成

**① 階段 1：刪掉 18 塊 `#if 0`／3,993 行**（`92b382d`）
`app/WASH_ROBOT.cpp` **13,435 → 9,442 行（-30%）**。

🔴 **動手前先查有沒有 `#else`**：`#if 0 ... #else ... #endif` 的 else 分支是**活的程式碼**，
整塊刪掉會是災難。實測 18 塊**沒有任何一塊含 `#else`/`#elif`** → 全部可整塊刪。

**② 這不是「看起來應該沒事」，是證明**
用 `cl /EP`（預處理但**不輸出 `#line`**——用 `/E` 的話行號位移會讓每一行都不同）
比對刪除前後，去掉空行後**逐位元相同**：非空行 **110,433 = 110,433**。
空行差 3,993＝剛好是刪掉的行數（MSVC 在 `#if 0` 原處輸出空行）。
→ **對編譯器而言這次刪除完全不存在。**

✅ **負控制**：加一行 `int fcv_probe = 1;` 之後比對確實變紅 → 這個比對在量東西。
✅ `cl /Zs` 錯誤集合不變（同樣 `C3493` + `C2064`×2、同一個符號），行號 7255→5565 是位移。

**③ 🆕 `harness/prove_noop.sh`**（`e7a8988`）—— 把上面那套做成可重複使用的工具

📌 **做完階段 1 才發現：計畫裡訂的「trace diff = 0」不是最強的判準，也不是最便宜的。**

| 工具 | 證明什麼 | 成本 |
|---|---|---|
| `prove_noop.sh` | **整個編譯單元**的預處理輸出逐位元相同 | 幾秒，**不需要 g++、不需要跑任何東西** |
| `compare.sh` | **跑到的路徑上**位元組與回覆相同 | 要建置 + 執行 |

`compare.sh` 最弱的一環是覆蓋率（腳本沒碰到的路徑永遠是綠的）；`prove_noop` 涵蓋**每一行**。
反過來它做不到的是：語意真的變了就一定紅，**抽函式那種改動用不上它**。
→ **原則：能用 `prove_noop` 證的階段，就不要動 `compare.sh`。** 已寫進計畫 §5.5 與 harness README。

工具雙向驗證過：工作區 vs HEAD 綠／HEAD vs HEAD~1（刪死碼）綠／
**HEAD vs `0fcd139`（端點注入前）正確地紅**，且 diff 精準指出是 `endpoints.h` 的插入
與 11 個 `ep::host` 呼叫點。零行輸出當錯誤（exit 2）——空對空的 `cmp` 會回「相同」，那是假通過。

### 📌 這輪的通則

**「應該不影響」與「證明不影響」之間差了一個負控制。**
刪死碼是全專案最有信心的一種改動（預處理器本來就丟掉了），但**正因為有信心才更該證**——
沒證的話，日後階段 2 出現差異時，第一個要排除的就是「會不會是階段 1 其實動到了東西」。
花五分鐘證一次，換掉後面每一階段的一整條懷疑路徑。

### 待完成

- 🔴 **`sudo apt install -y g++`** 仍是階段 2 之後的前提（`prove_noop` 不需要，但 `compare.sh` 需要）
- 🔴 階段 −1（HEAD 上機）仍等機器
- 🟡 階段 2（抽指令層）可以開工，但它是**語意改動**，`prove_noop` 蓋不住 → 需要 `compare.sh` → 需要 g++


## 2026-08-29（續九）— 重構計畫定案；階段 0 的工具做完（但還沒端到端跑過）

### 已完成

**① `.claude/refactor_plan.md` 進版控**（`CLAUDE.md` 的 `.claude/` 索引已加一列）

📌 **參考文件 `architecture.md` 只是通用描述，不照抄** —— 它假設「一個裝置 = 一個軸」，
而這台機器上 14 支 driver 裡**真正的位置軸只有 2 支**（ZDT 推桿、DM2J 上滑台）。
SE3／MH300／CLV900 是 VFD，只有速度輸出、**沒有位置回授**；
🔴 **吊機的一條繩 = 三個裝置、三條匯流排**（SE3 寫變頻器 + SD76 讀位置 + DSZL 讀張力），
而且是刻意拆開的。強行套 `IAxis`，這個閉環會沒有家 ——
它現在就住在 `Crane_control_PI/main.cpp` 裡跟協定攪在一起。

→ **分層改 4 → 6 層 + 1 橫切**：新增 **機構層**（虛擬軸、機構標定的家、行程守衛）
與 **指令層**（84 個 `cmd_*` 的家、FAST/SLOW 雙路徑、範圍驗證吃同一個常數）；
**安全層是橫切的監看 + 否決，刻意不放在呼叫路徑上**（放路徑上的閘門遲早被繞過 ——
`sendAndReceiveQuiet` 繞過 `MSG_NOSIGNAL` 就是實例）。
`cleaning_arm` **不拉進來**：它已經是乾淨的跨行程服務邊界。

**② 成功判準先定義好了**（per user：「跟 `main` 最後一版功能效果一樣才算整理成功」）

🔴 **但基準不能是 `main` 本身** —— 本分支相對 main 有 9 條刻意的行為改變，其中好幾條修的是
main 上此刻正在壞的東西（滑台每次掃動撞到底／交替步伐不可用／推桿差 5%）。可用的基準是：

```
等價基準 ＝ main-final 的功能 ＋ runbook §A2 塊三那 9 條預期差異
```

那份清單今天稍早才寫（原本是上機檢查表），**直接升格為重構的等價規格**。
已 `git tag main-final`（`6523b54`）釘住參考點。

**③ 階段 0 的工具全部做完**

- **端點注入** `common/endpoints.h`（`4ce7352`）：`ep::host/port` 讀 `FCV_EP_<NAME>_HOST/_PORT`，
  11 個連線點。🔴 **沒設環境變數時行為必須逐位元不變** —— 用來做等價量測的東西，
  自己不能擾動被量的對象。原常數留在原處當 fallback（連同承重的沿革註解）。
  ⚠️ **新 header 目錄要同步三個建置入口**：兩個 `.vcxproj`、runbook 的 `g++ -Icommon`、
  runbook 的 `rsync` 要帶 `common/` —— 只改一個就是「只驗到一端」
- **`harness/`**（`ff47a3d`）：`fake_bus.py`（一台假網關服務整條 bus）／`build.sh`／
  `run_trace.sh`／`normalize.py`／`compare.sh`／`negative_control.sh`／`cmds/`

🔴 **判準 2 分裝置比對，不是全域 diff**：主程式有多條背景執行緒各自寫 stderr，
全域行序由排程器決定，同一支程式跑兩次就不同 —— 直接 diff 會滿江紅**而且每條紅都是假的**。
代價寫在 README：改變「先跟哪個裝置說話」的順序抓不到，那一半靠判準 1（TCP 文字回覆，確定性）。

### ✅ 工具本身已自我測試（Python 部分不需要 g++）

`fake_bus` 五項全過：CRC 正確／**相同請求逐位元相同**（等價比對的前提）／不同 slave 值不同／
**廣播 `slave 0x00` 不回覆**（`trigger_sync_move` 的正確性建立在這件事上，假從站若禮貌地回一個
ack 就會把那條路徑測成錯的）／黏包切幀。
`normalize` 五項：分裝置正確、`INF` 行排除、時間戳剝除、**差一個 byte 抓得到**、空輸入 `exit 2`。

### 🐛 寫測試時自己踩了一次今天在記錄的坑

比對兩份時間戳不同的資料，得到「相等 ✅」—— **實際上兩邊都解析失敗、都是空字串**。
真因是 regex 要求 hex 後面必須跟空白（真實 `LOG_HEX` 有尾空白，我的測試資料沒有）。
📌 **與「明文掃描器讀零個檔卻回報未發現明文」完全同型**：**零筆資料的通過不是通過。**
已放寬 regex，並在 README 立成規則：**每個比較之前先斷言「解析到的筆數 > 0」**。

### 🔴 待完成

- 🔴 **本機沒有 `g++`，harness 尚未端到端跑過**。apt 有 9.3（支援 C++17），
  但 `sudo` 要密碼 → **需要使用者跑一次 `sudo apt install -y g++`**。
  📌 **等價比對不需要目標平台**：兩個版本在同一台機器上建就能比，x86-64 就夠；
  能不能在 Pi 上跑是 runbook §A2 的事，兩件事不要混
- 🔴 **裝好之後第一件事是 `negative_control.sh`，不是 `compare.sh`** —— 沒有紅過的綠燈不算綠燈
- 🔴 階段 −1（HEAD 上機驗證）仍等機器；本分支 9 條行為改變一條都沒有實機證據
- 🟡 指令腳本覆蓋率是這套驗證最弱的一環，`cmds/*.txt` 沒送到的指令 diff 永遠是綠的


## 2026-08-29（續八）— 合併 `origin/main` `6523b54`：對方是實機驗證過的那一份

📌 **前提更正（per user 2026-08-29）**：`6523b54` 的 commit message 寫「實機行為全部未驗證」，
**該句已過期**——那支已由該工程師實機測試過，並跑完由上往下洗的流程。
→ **在行為上他們是權威，我們是要對齊的一方。** 使用者指示：參考它的功能流程，融合進我們的分支。

### 已完成：merge `6523b54` into `fix/driver-crc`

**rename 追蹤成功**（`user_lib/WASH_ROBOT.*` → `app/`），`.cpp` **自動合乾淨**、
衝突只有 `.h` 與兩份文件。🔴 **`.cpp` 合乾淨正是最該手動查的地方**（本專案記過的
「合併乾淨 ≠ 語意接得上」）。

**`app/WASH_ROBOT.h` 衝突的解法**：取對方**整段 PWM 常數**，保留我方**左右歸屬註解**——
對方那份是 08-27 改號「之前」的舊文字（`right{1,2}/left{3,4}`），他們沒有動到它。
`ZDT_RF1/RF2 = 5,7`、`ZDT_LF1/LF2 = 6,8` 在衝突區之外，未受影響。

**文件衝突**：`work_log` 兩邊都往最上面加，但我方最上面是**待辦總表** →
對方兩則移入日期區並加註來源。`changelog` 是 append-only 帳本 → **雙方 13+N 條全部保留**。

### 🔴🔴 真正的收穫：PWM 重試疊加（git 一個字都不會說）

| | 位置 | 做法 |
|---|---|---|
| 對方 `6523b54` | **應用層** `pwm_set_duty_only_` | `for attempt=1..3`，間隔 120ms |
| 我方 `9af86e4` | **driver 交易層** `QX_DO24::sendAndReceive` | 3 次 + 40ms backoff |

兩邊沒碰到同幾行 → **無 merge conflict**，但合併後是 **3×3 = 最壞 9 次交易 + 480ms 純睡眠**，
而這段位在 `do_step_sync_` 的**收腳／伸腳之前**（gait 關鍵路徑，本來就有 300ms 靜置）。

🔴 **關鍵事實**：對方實機驗證時，**driver 層還沒有重試** → 被驗證過的行為是
「最多 3 次交易、間隔 120ms」，**不是 9 次**。留兩層等於上了一個沒有人驗過的組態。

**解法：`PWM_STEP_WRITE_TRIES` 3 → 1，重試單獨留在 driver 層。**
不是把重試拿掉，是把它收斂到一層。選 driver 層的理由是**涵蓋面較廣**——它同時保護
面板路徑（`cmd_pwm_set`），而寫入失敗時模組會**保持前一個輸出**（通訊斷掉螺旋槳不會停），
左右螺旋槳又共用 CH1，不能指望每個呼叫端都記得重試。
⚠️ **殘留差異已寫進 `.h` 註解**：重試間隔 40ms 而非 120ms（總視窗 80ms vs 240ms）。
若實機顯示掉包需要更長的讓路時間，**要調的是 `QX_DO24` 的 `kBackoffMs`，不是應用層那個常數**。

### 其他一併處理／查證

- **`CH_BRUSH` 15 → 5**（取對方）。✅ **已查 CH5 未被佔用**（CH1 閥／CH2 泵／CH6 破真空／CH14 水泵）→ 不撞號。
  🔴 **但兩邊紀錄互相矛盾**：我方原註解寫「2026-07-24 per user: 5→15, arm now physically installed」，
  他們寫「實體確認；15 是誤改，導致滾筒一直不轉」。**兩邊都聲稱有實體依據** → 以實機跑過的那份為準，
  並把矛盾原地記在 `.h` 註解裡，不要洗掉。`CLAUDE.md` 的 PQW 表已同步（CH15 → CH5）
- 🐛 **對方 commit message 列了一項這個 commit 沒有的改動**：「crane `UP_STOP_TOTAL_KG_DEFAULT` 50→70」
  —— 實查 `6523b54` **完全沒動 `Crane_control_PI/`**，那是 `0d5f6bc` 就改掉的（`e3c8820`=50／`0d5f6bc`=70）。
  我方早已含此值。**訊息與內容不符，不影響結果但會誤導追溯**
- ✅ **逐項確認雙方防線都活著**：我方 `SEND_FLAGS`／`SO_ERROR`／50 處 null-client 守衛／
  `trigger_sync_move` 回 `false`／左右歸屬／`CUP_PULSE_PER_CM`／`7.731`；
  對方 `PWM_STEP_MOVE_DUTY_PCT`／`CRANE_MOVE_SETTLE_MS`／`imu_persistently_bad_`
- ✅ 逐項確認**對方新碼沒有繞過我方新防線**：他們未動 `trigger_sync_move` 呼叫、未動左右歸屬符號、
  未新增 raw `send()`；新增的 `do_step_sync_rail_sweep_("run_script_pre", ...)` 用的是既有兩參數形式，
  與我方新增的第三個預設參數相容

### 🆕 對方帶來一個直接解我今天困境的東西

他們的 `2026-08-28 — 開發環境` 條目：**Windows 端有 VS 2022，`cl /Zs` 可以當場做 C++ 語法檢查**
（四個 flag 缺一不可：`/utf-8`／`/FIwinsock2.h /FIws2tcpip.h`／`/DNOMINMAX`；
判讀要**比對 HEAD 的錯誤集合**而非絕對數量，因為 MSVC 比 GCC 嚴格）。
🔴 **今天所有 C++ 改動仍是一行未編**——下一步就試這條路。

### ✅ 而且真的編了 —— 今天第一次有編譯器背書

照對方那條路走通了，**但他們文件裡的路徑在這台機器是錯的**，校正兩處：
`Visual Studio\2022\Community` → **這台是 `18\Community`**（`2022\` 是空的殘留目錄）；
`/I user_lib` → 本專案已分層，要 `/I app /I transport /I user_lib`。
另外從 WSL 呼叫要用 `cmd.exe` 絕對路徑，且 **.bat 必須是 CRLF**——LF 會讓 `cmd.exe`
把 `cd /d` 拆壞，症狀是「'/d' 不是內部或外部命令」這種看起來毫無關聯的錯誤。
完整可用版本已寫進 `runbook.md` **§A3**。

**結果（合併後 vs 合併前 `6285402`，同一組 flag、整棵樹各建一次）：**

| 目標 | 合併後 | baseline | 判定 |
|---|---|---|---|
| 12 支 driver（含今天改的 8 支 + CLV900 + 本來就守好的 3 支） | **0** | — | ✅ |
| `Crane_control_PI/main.cpp` | **0** | 0 | ✅ |
| `Linux_test/main.cpp`（涵蓋 DM2J 簽名契約） | **0** | 0 | ✅ |
| `app/WASH_ROBOT.cpp` | 3（`C3493`×1 + `C2064`×2 @ 7254/7259） | **同樣 3 個、同樣行號** | ✅ 既有 MSVC-only |

→ **今天的 39 處 null-client 守衛、ZDT 廣播回傳值、以及這次合併，都沒有引入任何語法/型別錯誤。**

🔴 **差點誤判**：兩邊錯誤行號都是 7254/7259，而合併替該檔加了 245 行——**行號不該一致**。
先 `cmp` 確認兩份確實不同（13433 vs 13216 行）、再確認合併的第一處改動在 **7325 行**
（＝錯誤點之後），才敢說「錯誤集合相同」。**若沒查這一步，就會拿一個可能是「同一個檔編兩次」
的結果當通過。** 這條已寫進 §A3。

📌 **能證明什麼要講清楚**：`cl /Zs` 證明的是**語法、型別、宣告一致性**；
**不能**證明 GCC/ARM64 編得過、**不能**證明連結得起來（`/Zs` 不產 obj 也不 link）、
**不能**證明行為正確。它取代不了 Pi 上的建置，只是把迴圈從幾分鐘縮到幾秒。

### 待完成

- 🔴 **仍需 Pi 上用 g++ 實建 + 實跑**（`cl /Zs` 只是語法層）
- 🟡 PWM 重試間隔 40ms vs 對方驗證過的 120ms —— 實機觀察掉包率後再決定要不要調 `kBackoffMs`
- 🟡 `CH_BRUSH` 5 vs 15 的矛盾記載，建議跟對方當面對一次


## 2026-08-29（續七）— 上機分支拍板；並發現 main 今早多了一個會跟我們對撞的 commit

### 已完成

**① 上機分支拍板：直接上 `fix/driver-crc`，不再分兩段**（per user）

原計畫「先上整理分支證明搬家沒搬壞、再上 driver 分支」的前提，在 08-28 中午就失效了
（見續四）。要維持分兩段就得另開一條真正只有搬家的分支，代價大於收益。

🔴 **代價已明確寫進文件**：上機若出現非預期行為，**不再能靠「哪一段出現的」來歸因**。
`runbook.md` §A2 **整段翻面**：
- 標題與前提改成 `fix/driver-crc`
- 原本的「為什麼整理分支是功能等價的」→ 改寫為**「本分支相對 `0d5f6bc` 的行為改變清單」**，
  分三塊：無行為影響／只有輸出字串會變／🔴 **9 條刻意的行為改變**（逐條寫「上機會看到什麼」）
- 🔴 **§4 驗收判準跟著翻面**：舊判準是「與 baseline 逐字一致」，照用會**整片報紅，而每一條紅
  都是設計好的**。新判準是「拿塊三那張表當預期差異表核對——**表上沒有的差異才是訊號**」。
  📌 這是本專案「政策反轉時舊斷言會把正確報成故障」的第三次應驗，這次是主動翻面而非事後發現

**② `origin/main` 今早多了 `6523b54`**（Sadie-fang，2026-08-29 10:52，+921/-22，7 檔）
「同步步伐 PWM 輸出 + 恢復內建清洗 + 停用所有自動補救」。**本分支尚未合併。**

🔴🔴 **它跟我們的 `9af86e4` 直接對撞，而 git 一個字都不會說：**

| | 位置 | 做法 |
|---|---|---|
| **他們** `6523b54` | **應用層** `WashRobot::pwm_set_duty_only_` | `for (attempt=1..PWM_STEP_WRITE_TRIES)` 呼叫 `setPWM_Duty`，間隔 120ms |
| **我們** `9af86e4` | **driver 交易層** `QX_DO24::sendAndReceive` | 3 次重試 + 40ms backoff |

合併後就是**重試套重試**：最壞 **3 × 3 = 9 次交易**，外加 `2×120ms + 3×2×40ms = 480ms` 純睡眠，
而這段程式碼位在 `do_step_sync_` 的**收腳／伸腳之前**（gait 關鍵路徑，本來就有 300ms 靜置）。
🔴 **兩邊沒碰到同幾行 → 不會有 merge conflict**。
📌 **正解不是留兩層**，而是擇一——他們加應用層重試的理由是「根因未明」，
而那正是我們 driver 層重試要蓋的情況。**我方 commit 自己也註明「重試未被觸發、救援路徑仍未驗證」。**
📌 與「`sendAndReceiveQuiet` 繞過 `MSG_NOSIGNAL`」是同一個機制：**合併乾淨 ≠ 語意接得上**。

**其他要一併處理的（合併時）：**
- **`CH_BRUSH` 15 → 5**。✅ **已查：CH5 沒被佔用**（現況 CH1 閥／CH2 泵／CH6 破真空／CH14 水泵／CH15 刷），
  **不會撞號**。🔴 **但紀錄互相矛盾**：我方 `WASH_ROBOT.h:485` 寫「2026-07-24 per user: 5→15,
  arm now physically installed」，他們寫「實體確認；15 是 2026-07-24 誤改，導致滾筒一直不轉」。
  **兩邊都聲稱有實體依據。** 經驗證據（滾筒不轉）比宣告強，但這條要當面確認。
  ⚠️ 本專案 `CLAUDE.md` 的 PQW 表仍寫 CH15＝滾筒刷，**合併後要跟著改**
- 吊機 `UP_STOP_TOTAL_KG_DEFAULT` 50 → 70（**這是安全門檻**）／`STEP_CM_MAX` 80 → 100
- 他們動的是 `user_lib/WASH_ROBOT.{h,cpp}`，我方已搬到 `app/` → 靠 git rename 追蹤（`0d5f6bc` 那次成功過）
- ⚠️ **對方 commit message 自己寫著「實機行為全部未驗證」**，驗證方式是 `cl /Zs` 語法檢查

### 待完成

- 🔴 **合併 `6523b54` 與否是決定，不是照做**——已在 `runbook.md` §A2 末段列出兩個選項與各自代價
- 🔴 **若決定合併：PWM 重試必須擇一層**，不要留兩層
- 🔴 有機器時：把 08-29 這一整串 commit 編一遍（仍是一行未編）


## 2026-08-29（續六）— 掃完 null-client；順帶抓到我自己前一小時寫錯的數字

**仍然沒有機器。** 續五說「剩下 9 支要有機器才敢一次上」，使用者指示「不用機器的都做」。

### 🔴 先更正續五的一個錯誤：不是 10 支，是 8 支

續五那次掃描用的 grep pattern 是 `!client\b`，**而 `!client->sendData(...)` 也會匹配這個 pattern**
（`!client` 後面接 `-`，正好是 word boundary）。後果是雙向的：

- **誤判成「沒守」**：`JC_100_METER:57` 與 `XKC_Y25_RS485:70,180,214` 其實守了，
  只是寫法是 `if (!client || !client->isConnected())`
- **誤判成「守好了」**：`DM2J_RS570` 只守了 `sendRecv`，六支 `read_*` 與 `recv_frame_` 是裸的

逐函式讀原始碼之後，實際是 **8 支 driver、38 個進入點**。
📌 **這是 memory 裡那條「稽核工具的輸出是待查清單，不是結論」的第 N 次應驗** ——
而且我是在**已經因為同一件事寫了一整天日誌之後**又犯一次。第一版 pattern 甚至連
「守衛」與「解參考」都分不開，數字卻被我直接寫進待辦表和 commit message。

### 已完成

**① null-client 守衛掃完 8 支 / 38 處**（+ 續五的 CLV900 = 39）

| driver | 處數 | 守衛回傳 |
|---|---|---|
| `ZDT_motor_control` | 18 | 17 支 `bool` → `true`；`readEcho` → `{}` |
| `DM2J_RS570` | 7 | `recv_frame_` → `-1`；6 支 `read_*` → `true` |
| `PQW_IO_16O_RLY` | 5 | `readEcho`/`readAllStatus` → `{}`；`close()` → `return;` |
| `DY_500_weight_sensor` | 3 | `true` |
| `DSZL_107` | 2 | `true` |
| `MH300` / `SD76` / `SE3` | 各 1 | `{ respLen = 0; return true; }` |

本來就守好的三支：`JC_100_METER`／`XKC_Y25_RS485`／`QX_DO24`。

⚠️ **`PQW_IO_16O_RLY.cpp` 的大括號計數本來就不平衡（35/34）**，不是這次造成的：
`:217` 有一行 `if (id < 1 || id > 16){//relay_count) {` —— 註解裡那個 `{` 讓天真的計數器數不對。
語法沒問題，**刻意不動它**（那是另一件事，不該夾帶）。

**② `scripts/wr.sh` 的矛盾註解**（續四留下的）
三處（檔頭用法說明、檢查區、start 區）都寫著「暫時／之後接回去時取消註解即可」，
而 2026-08-27c 的決策是**永久移除**。已全部改成「永久不接」，並註明保留那兩段
只為記錄它們曾經怎麼啟動、不是待辦。`bash -n` 通過（**這次是真的直譯器輸出，不是我自己 echo 的**）。
⚠️ **depth window（`:66`）刻意沒動**——那是另一條標著「待 user 決定」的待辦，不是我可以順手拍板的。

### 🔴 仍然：C++ 一行都沒編過

39 處守衛全部未編譯（本機無 `cc1plus`、兩台 Pi 不可達）。做的是人工複核：
38 個插入點逐一確認落在函式的開頭大括號之後、回傳型別與各函式簽名相符、
八個檔的大括號 delta 平衡。**這不等於編得過。**

### 待完成

- 🔴 **有機器時第一件事：把 08-29 這一整串 commit 編一遍**（`runbook.md` §建置），三支二進位都要
- 🔴 **上機前要決定整理分支怎麼處理**（續四的 🔴🔴，仍未決）
- 🟡 `scripts/wr.sh:66` 的 depth window 要不要關掉 —— 待 user 決定


## 2026-08-29（續五）— 清三條純程式碼待辦；其中一條掃出它其實是 10 支的問題

**仍然沒有機器。** 三條都是「不需要實機就能修」的舊債，最久的放了 4 個月。

### 已完成

**① ZDT `trigger_sync_move()` 廣播永遠回報失敗**（mailbox 2026-04-30，債齡 4 個月）
`user_lib/ZDT_motor_control.cpp:599` 以 `return resp.empty();` 收尾，而 slave `0x00` 是 Modbus
廣播、依規範**沒有從站會回覆** → `readEcho(200)` 每次必逾時 → 每次必回報失敗。現場症狀是
body extend 真的動了、log 卻每步印一次 `trigger_sync_move FAIL`。
改為送出成功即 `return false`。`readEcho(200)` **保留但降格為排空**——上一筆交易的遲到回覆若留在
socket buffer 裡，會被下一筆誤讀成它的回覆；結果一律丟棄。
🔴 **200ms 刻意不動**：它不是排空所需，但它在步態迴圈裡每個 cycle 都跑，沒有人量過拿掉之後的
時序長什麼樣。**縮短它是對運動迴圈的計時改變，該獨立成一次有機器驗證的改動，不該夾帶在
一個修回傳值的 commit 裡。**（順帶查證：`app/WASH_ROBOT.cpp:4559` 註解說的「~150ms warm-up」
來自 poll 迴圈開頭的 `sleep_ms_(poll_ms)`，**不是**來自這個 200ms，所以兩者沒有耦合。）
三處呼叫端的過期註解與 TODO 已同步（`app/WASH_ROBOT.cpp` ×2、`Linux_test/main.cpp` ×1），
`.h` 補上「回傳值只反映送出成功與否，不能確認從站真的動了」。

**② `CLV900_inverter` 缺 null-client 防護**（mailbox 2026-05-14）
`sendModbus` 進場加守衛，沿用 `DM2J_RS570::sendRecv` 既有慣例（`true` = 錯誤）。

🔴 **但順手掃全 `user_lib/` 之後，這條的前提是錯的：洞在 12 支裡的 10 支，不是 CLV900 一支。**
掃法是「建構子把 `client` 設為 `nullptr` ∧ 傳輸函式沒有 `!client` 守衛」，命中
CLV900／DSZL_107／DY_500／JC_100／MH300／PQW／SD76／SE3／XKC_Y25／ZDT；只有 `DM2J_RS570` 與
`QX_DO24` 本來就守了。**CLV900 之所以被單獨開票，只是因為當時有人剛好踩到它。**
若就這樣把那列勾成「已修」，剩下 9 支會連同「這件事處理過了」的印象一起消失——
**跟本日上半場清掉的那八條假結案是同一個機制**。已另立一列 🔴 記錄全域範圍，未併入 CLV900 那列結案。

**③ 4 個 `.vcxproj.user` 被 git 追蹤**（work_log 2026-07-15）
`git rm --cached` 四個檔（**留在本機**）＋ `.gitignore` 加 `*.vcxproj.user`。
⚠️ **移除前先確認過它不是唯一副本**：這四個檔記著「哪個專案建置到哪台 Pi」，而
`.claude/runbook.md:22-23` 與 `CLAUDE.md:263-264` 都有同一份對應，才動手。
📌 真正該移除的理由比「互相覆蓋」更硬：內容含 `-1125135748` 這種**只在該台機器有意義的
VS 連線 handle**，它在別台機器上不可能是對的，本質上就不是可共用的資料。

### 🔴 未編譯 —— 這次的 C++ 改動一行都沒編過

本機 `gcc` 存在但**沒有 `cc1plus`**（無 C++ frontend），兩台 Pi 也連不到（`192.168.5.26:22` timeout）。
所以只做了人工複核：大括號配對（5 檔皆平衡）、`LOG_ERR`/`_log_tag` 在兩支 driver 中皆已在用、
回傳值慣例與同檔其他函式一致。**這不等於編得過。**
🔴 **下次有機器時，第一件事是把這三個 commit 編一遍**，建置指令見 `runbook.md` §建置。

📌 **過程中自己示範了一次踩坑索引裡的「印出來 ≠ 檢查過」**：跑語法檢查那道指令我在尾巴接了
`echo "(無輸出＝語法通過)"`，而編譯器其實是 fatal error 中止的——那行字照樣印了出來。
**把「通過」寫成無條件輸出，等於做了一個永遠成立的斷言。**

### 待完成

- 🔴 **三個 commit 都未編譯**，有機器時優先補
- 🔴 **null-client 其餘 9 支未修**（同一行守衛，零風險，但 9 檔未編譯的改動要有機器才敢一次上）
- 🟡 `scripts/wr.sh:50-52` 註解改成「永久不接」（續四留下的）


## 2026-08-29（續四）— 「已修 ✔」那半邊的校驗：32 條裡 8 條的記載是錯的

**沒有機器可用，所以做的是純原始碼／版控的工作。** 08-29 上午的待辦總表校準只查了「未修」那半邊，
當時就記著「**反向危害更貴：標已修而實際沒修的列，永遠不會有人再去看它**」。這次把 32 條標「已修」
的全部打開比對原始碼。

### 已完成

**逐條比對 32 條「已修」→ 24 條屬實、8 條記載有誤。**

✅ **屬實且已逐字看過原始碼的（24 條）**：`SO_ERROR`（`transport/TCP_client.cpp:208,214`）／
`CRANE_VFD_NAME`（四處都吃巨集）／上滑台 7.731 換算層＋行程守衛／`CUP_PULSE_PER_CM=3000`／
左右歸屬 `ZDT_RF1/2={5,7}`、`ZDT_LF1/2={6,8}`／SD76 `readRegister` 三道驗證（byteCount → 幀長 → CRC）／
`server.js` `CRANE_IP=192.168.1.10`／`cmd_side_measured` 的 `abort_flag=false`（:2924）／
`MSG_NOSIGNAL`（含合併帶進的 `sendAndReceiveQuiet` :495 也已補）／`apply_keepalive`／
DSZL MBAP + `save_params(0xA20=40)`／SE3 `readFaultCode`/`invalidateCuModeCache`/`clearAlarm`/recv 150ms／
SD76 SCAL 當除數／DM2J 四項（`0x0040` HOME_DONE mask、`0x000F` enable、`0x1801=0x2211` save、`read_status`）／
`dm2j_manual_enable` 已不存在／`frame_capture` 三檔已進版控／`do_step_sync_rail_sweep_` 已接回／
`CLAUDE.md ## Architecture` 已由原始碼重建。

🔴 **① 三條「`refactor/app-layer` 上仍未修」的跨分支警語，全部是錯的**
`SO_ERROR` 由 `56bfa5c`、上滑台 7.731 換算由 `9fa4fe1` **都已 cherry-pick 進整理分支**，
左右歸屬 `8d4d2d5` 本來就在整理分支上。三條警語會讓人以為整理分支還會把滑台一路撞到底。

🔴🔴 **② 由 ① 順藤摸出真正大的那條：整理分支已經不是「純整理、功能等價」**
`git log origin/main..refactor/app-layer --no-merges -- '*.cpp' '*.h'` 列出 **9 個刻意的行為改變**
（MSG_NOSIGNAL／`CRANE_IP`+VFD 型號／7.731 換算+行程守衛／`ARM_SWEEP_RPM` 1000→250／QX_DO24 PWM
重試+回讀／`zdt_pusher` 範圍分岔／`CUP_PULSE_PER_CM`／左右歸屬／`SO_ERROR`）。
**上機計畫的整個前提是「分開上機才分得清『行為變了』是搬家搬壞還是 driver 改的」——那個區分已經沒了。**
日誌裡「唯一實質改動是 `send()` 加 `MSG_NOSIGNAL`」的結論停在 08-28 上午。已入表為 🔴🔴 待辦。

🔴 **③ 四條攝影機列的「作廢」理由是假的**
理由寫「攝影機路線已移除」，實際移除的是 **GUI**：`cmd_run_depth_avoid`、`depth_cam_cmd_`、
`DEPTH_CAM_IP/PORT`、`FrameAnalyzer` 全都還活在 `app/WASH_ROBOT.{h,cpp}`。
其中 `remaining_travel_cm` 那條**最不該被作廢**——`DEPTH_CAM_STANDOFF_CM=56.0` 與
`DEPTH_CAM_LEAD_OFFSET_CM=32.0` 正是既有 🔴 待辦「`run_depth_avoid` 後端仍會自行改走 cross 步伐」
所用算式的輸入，卻被標成「已作廢、不用管」。已恢復為未驗證。

🔴 **④ 表格 19 列的檔案路徑在分層重構後已不存在**（`user_lib/WASH_ROBOT.*` → `app/`、
`user_lib/TCP_client.cpp` → `transport/`）。08-29 上午只修到 2 列，是同一個坑的 9 倍規模。**已全數校正**
（表格區才改；下方各日期條目的原文**刻意不動**，那是當時的紀錄）。
順帶：`.claude/camera_obstacle_plan.md` 已搬進 `.claude/archive/`。

⚠️ **⑤ `scripts/wr.sh` 的 cam1/cam2 確實已註解（記載屬實），但檔內註解與決策矛盾**：
`:50-52` 仍寫「之後接回去時把下面這段一起取消註解即可」，而決策是**永久不接**。

### 📌 這輪的通則

**「已修」是一種會過期的狀態，而它過期時不會有任何訊息。**
三條跨分支警語在寫下的當天是對的，cherry-pick 之後就變成錯的；四條攝影機列的作廢理由在
「移除 GUI」那天看起來成立，但沒有人回頭確認後端。**與踩坑索引「政策反轉時舊斷言會把正確報成故障」
互為鏡像**：那次是斷言沒跟著政策翻面，這次是結論沒跟著程式碼翻面。
📌 **勾掉一列的當下要寫「憑什麼說它修好了」（commit / 檔案:行號），不是只寫「已修」**——
這次能在半天內查完 32 條，靠的正是那些有寫 commit 號的列。

### 待完成

- 🔴 **上機前要決定整理分支怎麼處理**（(a) 接受帶行為改變照上／(b) 另開純搬家分支／(c) 直接上 `fix/driver-crc`）
- 🟡 `scripts/wr.sh:50-52` 的註解改成「永久不接」
- 🟡 本次只驗「程式碼有沒有那段」，**沒有編譯、沒有實機**——四條被翻回「未驗證」的攝影機列仍待實機


## 2026-08-29（續三）— 上滑台方向確認；homing 這條路確定放棄

### 🔴 使用者提供的關鍵設計脈絡（本輪最有價值的一件）
**「沒有原點感測器。因為有斷電煞車，所以斷電前都要先移回 0 點，這樣每次都一樣。」**

這一句同時解決了兩件事，也推翻了原本寫在程式註解裡的「真解」：

- ✅ **`cmd_init()` 用 `0x0021`（設當前位置為零）是對的做法，不是缺陷** ——
  作業流程（斷電前回 0）＋ 斷電煞車保持位置，保證開機時滑台就在硬限位。
- 🔴 **`WASH_ROBOT.h:624` 的「真正的解法是啟用驅動器的 homing」是錯的** ——
  **沒有原點感測器，`home_start()`（`0x0020`）沒有東西可以觸發**。
  這條註解與待辦表那列都要改寫。
- 🟡 **殘餘風險**（保護來自流程而非機制）：**異常斷電／停電時來不及回 0**，
  下次開機 init 會把當時的位置當成零點，座標系整個偏移且無人被告知。

### 唯讀盤查（步驟 A，純 fc=0x03，零寫入）
直接送 Modbus 讀取幀，**刻意不走應用層**——`cmd_init()` 裡有 `home_set_current_pos_zero()`
是寫入，走應用層有踩到的風險。

| 暫存器 | 值 | 判讀 |
|---|---|---|
| `0x1003` status | `0x0032` | FAULT=0 ENABLE=1 RUN=0 CMD_DONE=1 PATH_DONE=1 **HOME_DONE=0** |
| `0x600A` home mode | `0x0002` | Bit0 方向=0／Bit1 感測器型式=1／Bit2 速度來源=0 |
| `0x600F`/`0x6010` 快/慢速 | **200 / 50 rpm** | ＝ **25.8 / 6.4 cm/s** |
| `0x6011`/`0x6012` acc/dec | 100 / 100 ms/krpm | |
| `0x6015` overrun | **0** | 衝程保護沒設過 |
| `0x602C` 位置 | 0 | |

🔴 **`HOME_DONE=0` 是「從未做過真正回零」的硬證據**（原本只是推論）。
🔴 **而 homing 的速度是危險的**：`0x600F=200 rpm` ＝ 25.8 cm/s，滑台總行程只有 50cm
→ **全速 2 秒走完全程**，且 `overrun=0`。⚠️ **`sensor_type=1` 只是驅動器裡的設定值，
不代表實體真的裝了感測器** —— 這正是差點賭下去的地方。

📌 **位址是查 `summaries/DM2J_RS_MODBUS_SUMMARY.md` 拿的，不是從 driver 反推。**
我原本以為快慢速在 `0x600B/0x600C`，**摘要說是 `0x600F/0x6010`** ——
CLAUDE.md 那條「動 driver 前先查 summaries」當場救了一次。

### ✅ 方向確認（實機）
用 **PR move 絕對 +1cm @ 30 rpm** 試（不是 JOG，理由見下），使用者目視：**往右**。

→ **座標正方向 = 往右，0 點 = 左端硬限位。**
與 `WASH_ROBOT.h:624` 註解「開機前應確認滑台已在最左端」**一致**，現在有實測依據。

### 📌 為什麼沒用 JOG（使用者原本指定 JOG 100rpm 走 1cm）
JOG 是連續點動，距離由「我們送停止指令的時機」決定：

- 100 rpm ＝ **12.9 cm/s** → 1cm 只要 **78 ms**
- 而一趟 WiFi → SSH → USR 網關 → RS485 的往返就要幾十毫秒

**送出「停止」時它大概已經走了 2~3 cm。** 改用 PR move 之後：距離由驅動器執行、
走完自己停，且**行程守衛 `[0,48]` 會擋下負方向**（今天剛用 `test_dm2j` 驗過守衛是在
送出任何位元組之前就拒絕）。使用者採納。

### ⚠️ 我在自己寫的測試程式裡又踩了一次 false=成功
`jog_test.cpp` 印 `read_position_cm ok=0` 看起來像失敗，其實那是 error flag，
**`0` ＝ 成功**；而 `if (okb && oka)` 判斷寫反，DELTA 那行因此沒印出來。
🔴 **這是在同一天發現這個慣例、剛修好 `test_qx_do24` 同型錯誤之後犯的。**
📌 **知道一個坑存在，跟不會再踩，是兩回事。** 後續 `rail_move.cpp` 已改為
`(read %s)` 印 ok/FAILED 並用 `!err` 判定。

### 本輪的移動紀錄（全部經過行程守衛，實體沒有異常）
`0 → 1.0`（30rpm，方向試探）→ `1.0 → 5.0`（30rpm，建立乾淨起點）→ `5.0 → 0`（30rpm）。
**滑台最後停在座標 0.0**，與「斷電前回 0」的作業流程一致。
⚠️ **但「實體是否確實頂到左端硬限位」使用者尚未確認**（本輪未重設零點）。

### 中止與收尾
使用者要求暫時退出。四支程式（吊機 `crane_control_PI.out.new`:5002／
web backend:8080／本體 `facade_cleaning_v2.out.new`:5001／`motor_api`:9527）**全部關閉**，
FIFO 持有者與 fifo 檔一併清除，**兩台複驗埠與行程皆乾淨**。
🔴 `~/projects/` 全程未動；`.out.new` 仍未取代 `.out`（部署是另一個決定）。

⚠️ **`echo exit > fifo` 對本體那支沒有生效**（吊機那支正常），最後是 `kill -TERM` 收掉的。
runbook §A 記過「`exit` 只對兩支 C++ 主程式有效」，**但這次連本體主程式自己都沒吃**。
原因未查，下次收尾要預期可能得用 TERM。

### 待完成
- 🔴 **500 rpm 的失步測試沒跑到** —— 使用者原本指定要試 500rpm（64.4 cm/s），
  已建好起點（5cm）但改為先回 0、接著要開網頁，之後中止。**RPM 重評仍未開始。**
  📌 測法已定案：**每輪拿尺量兩次**（移動前後），因為驅動器只數脈衝、
  座標永遠顯示正確，失步時它一個字都不會說
- 🔴 **`WASH_ROBOT.h:622-625` 的註解要改寫**（「真正的解法是啟用 homing」已被推翻）
- 🟡 web backend 起得來（8080 有回應），但**使用者這次沒實際開頁面驗證**

---

## 2026-08-29（續二）— §A2 init 檢查表：兩台都跑起來了，四項實機驗證通過

### 做了什麼
兩台 Pi 各跑一次 `~/bringup/` 的新建置，**只到 `init()` 完成 + 唯讀指令**。
🔴 **馬達一次沒動過**、`~/projects/` 全程未動、沒有覆蓋任何現有二進位。
跑完兩台都優雅停止（`echo exit > fifo`），埠釋放、FIFO 持有者的 `sleep` 也清掉，
**兩台複驗乾淨**。

⚠️ **這輪跑的是 `fix/driver-crc` 的建置，不是 §A2 原本指定的 `refactor/app-layer`**
—— §A2 的目的是「證明搬家沒搬壞」（比對 baseline `0d5f6bc`），本輪的目的不同：
**驗證交接後那 6 個 commit 沒有壞掉啟動路徑**，比對基準是 08-28 那次的輸出。

### 硬體前提（先確認才有辦法判讀 init 輸出）
七個網關全部通電可達：吊機 `.30`/`.31`/`.34`/`.32`/`.33`、本體 `.20`/`.22`
（在各自的 Pi 上 ping，`192.168.1.0/24` 是兩台的有線網段，從 WSL 連不到是正常的）。

### 吊機 —— 五個網關 + 全部裝置 OK
`init complete — accepting commands`，5002 開啟。唯讀指令 `ping`／`status`／
`tension`／`vfd_fault` 全部有正常回應。

### 本體 —— 逐項對照 runbook §3 的預期表，**八項全部符合**
`USR .20/.22` ／ `DM2J slave 14 @ cli_20_` ／ `ZDT 5~8` ／ `JC-100 5~8` ／
`PQW slave 12 @ cli_20_` ／ `XKC 13` ／ `QX-DO24 PWM slave 9 (presence not probed)` ／
`DY-500 10/11 not installed`。

### ✅ 這輪拿到的四項實機驗證（都是待辦表上「已修但未實機驗證」的）

| 項目 | 證據 |
|---|---|
| `f4e0d02` VFD 型號不再寫死 | `[OK] VFD left (**SE3**) USR_A slave 1` —— 08-28 之前這裡寫死印 `MH300` |
| `ce8ba81` 重連補 `getsockopt(SO_ERROR)` | **吊機關著時本體 `reconnect success` 出現 0 次、`reconnect failed` 101 次**。原缺陷正是「吊機沒開卻印 20 次 `reconnect success`」→ **雙向斷言成立** |
| `[2026-08-28k]` 上滑台換算 | `[OK] DM2J arm rail … **lead=7.731 cm/rev travel<=48 cm**` |
| `3c75351` DM2J 16 個 `void`→`bool` | init 全程正常＝**簽名改動沒有破壞啟動路徑**（契約另一端 `Linux_test` 也已編過） |

### 🔴 複測推翻了待辦表上的一條歸因

`vfd_fault` 兩側**都回 `ERR read_fail`**（08-27 記的是「left 報假警 `f1~f4=160/OPT`、
right read_fail」）。而該列原本的歸因是「`mh300_migration_plan` Phase 3-3 未完成、
仍讀 SE3 的 H1007/H1008」—— 🔴 **這歸因站不住**：本機 `CRANE_VFD_IS_SE3 1`，
**跑的就是 SE3，讀 SE3 位址本來就該是對的**。

真正的線索應該是表格裡另一列（`SE3_inverter::readFaultCode()` 的 `0x1007`/`0x1008`
位址待驗）。**兩列很可能講的是同一件事**，已在表上互相指路。
📌 **與踩坑索引「錯誤歸因錯了比沒有訊息更糟」同型**——照原歸因去查 MH300 遷移，
會查一個根本沒壞的東西。

### 另外兩條數字過期
- `retract_tension_stop_kg` 實際是 **50**，不是 08-27 記的 25 → 「左側 26.45 已越過門檻」
  **不成立**，該列由 🔴 降為 🟡。⚠️ 但**根因沒變**：刻度仍是 `-0.01` 佔位值，
  所以「差 9.6 kg」這個數字同樣不可信
- 🆕 **本體主程式自己也還在探測深度相機**（`[WARN] depth_cam 127.0.0.1:9530`）。
  既有待辦只記了 `scripts/wr.sh:67` 會**啟動** `depth_cam_service.py`，**漏了主程式端**

### ⚠️ 誠實記錄：兩個新欄位「編譯了但沒被執行到」
`8320bf3` 加的 `status` `p_err=` 與 `cmd_attach` `partial_seal=N` 都是**條件式輸出**。
連跑 8 次 `status`（32 筆 JC-100 讀取）**全部成功 → `p_err` 一次都沒出現**。
那是**正確行為**（程式碼註解自己就寫「沒有 p_err ≠ 數值正確」），但也意味著
**這條路徑仍未被驗證**。`partial_seal` 需要真的 attach（會動作），沒測。
📌 **與 `recovered on attempt` 同型**：實作了、編譯了、跑起來了，
**都不等於那條路徑被執行過**。已進待辦表。

### 順帶：`.22` bus 這次很穩
8 輪 ×4 顆 JC-100 ＝ 32 筆讀取全部成功，一次 `no reply` 都沒有。
⚠️ **但這不能拿來否定「PWM 間歇 no reply」那條待辦**——JC-100 與 QX-DO24 是
不同裝置，且待辦記的是**寫入**（`fc=0x10`）失敗，這裡量的是**讀取**。
📌 **「這次沒發生」不是「不會發生」**，尤其樣本與症狀都不同（踩坑索引「上次量到的
數字不是常數」同型）。

### 待完成
- 🔴 **`.out.new` 仍未取代 `.out`** —— 是否正式部署（覆蓋 `~/projects/.../bin/`）
  是另一個決定，🔴 **覆蓋前要先備份原檔**，否則另一位開發者的建置成果會沒了
- 🔴 **那 6 個 commit 只驗到「啟動路徑沒壞」**：`abort_flag` 重置與緊急收繩張力警示
  都需要**送會動的指令**才驗得到，本輪刻意沒做
- 🟡 `runbook.md` 連線資訊段仍寫「`server.js` 的 `CRANE_IP` 預設值**仍是 `.101`**（過期）」
  —— 該值已於 `f4e0d02` 修好，**runbook 自己落後了**（與今日兩次校準同型）

---

## 2026-08-29（續）— 機器回來了：編譯交接後的六個 commit，兩支測試第一次跑起來就抓到缺陷

### 機器狀態
兩台 Pi 都空著（`who` 無他人 sshd、5001/5002/8080/9527 四埠全無佔用、無殘留行程）。
本體 `192.168.5.26` 剛開機 11 分鐘、吊機 `192.168.5.17` up 20:34。
🔴 **全程只寫 `~/bringup/`，`~/projects/` 一個字都沒動**（那是 VS 遠端建置落點，
另一位開發者在 `main` 上迭代會重建它）。**沒有部署、沒有覆蓋任何現有二進位。**

### 為什麼第一件事是編譯
08-28 16:30 機器交出之後又做了 6 個 commit（`3c75351` ~ `5560318`），
其中 5 個動了 C++，**一個都沒編過**。而 `3c75351` 改的是 `DM2J_RS570.h` 的
public 簽名（16 個 `void` 改 `bool`）＝ `CLAUDE.md` 明訂的**跨模組契約**。

### 建置結果（全部在 Pi 上，`~/bringup/`）

| 目標 | 結果 | 產物 |
|---|---|---|
| 吊機 `crane_control_PI` | ✅ 通過（50.5s） | `crane_control_PI.out.new` md5 `416bd26b…`（與 08-28 那份不同＝確認不是舊二進位） |
| 本體 `facade_cleaning_v2` | ✅ 通過（14 編譯單元 -P4） | `facade_cleaning_v2.out.new` md5 `e397710…` |
| **`Linux_test`** | ✅ 通過 | `linux_test.out` —— **DM2J 簽名改動的契約另一端，runbook 的建置指令沒有涵蓋它** |
| `test_dm2j` / `test_qx_do24` | ✅ 通過 | 兩支**從未編譯過**的測試（`7dec156` 自己記著這件事） |

⚠️ **驗產物而不是驗管線**：兩支 build 我都接了 `| tail`，
**離開碼是 `tail` 的**（本專案踩坑索引裡就有這一條），所以另外 `ls` + `md5sum`
確認檔案時間戳與雜湊真的變了才算數。

### 🔴 `test_qx_do24` 第一次執行：一個斷言都沒跑到

```
[FATAL] init failed
```

而假從站 log 顯示它**一個請求都沒收到**。查下去：`QX_DO24::init()` 是 Mode B
（不發包），本來就不該有請求 —— 真因是**斷言寫反了**。
`QX_DO24::init()` 回 **`true` = 成功**，測試卻寫 `if (pwm.init(...)) FATAL`，
**把成功判成失敗**。

📌 **這是踩坑索引裡「斷言本身寫錯會把成功判成失敗」的又一次**，
但這次多了一層：**那支測試是 08-28 寫的，寫的當下機器已交出、無法編譯，
所以錯誤活了一整天而看起來完全正確。**

### 📌 順帶揭露一件更大的事：契約管了簽名，沒管語意

`user_lib/` 14 支 driver 的 `init()` 簽名全部是 `bool init(...)`，**回傳語意卻有兩派**：

- **`false` = 成功**（Modbus 風格）：SE3 / MH300 / DM2J / SD76 / DSZL / DY500 /
  JC100 / PQW / XKC / ZDT / ZS_DIO / CLV900 —— **12 支**
- **`true` = 成功**：**`QX_DO24`**（唯一活著的異類）、`DIHOOL_control`（無呼叫端＝死碼）

🔴 **從 `.h` 看不出來** —— 兩派的宣告逐字相同。`if (dev.init(...))` 在 12 支上
是「失敗了」，在 QX_DO24 上是「成功了」。

✅ **應用層沒有踩到**（逐一查過）：SE3/MH300 寫 `if (!vfd.init(...)) { OK } else { WARN }`
＝ 對；QX_DO24 的唯一呼叫端 `WASH_ROBOT.cpp:204` **不檢查回傳值**。
**唯一的受害者就是那支從未跑起來的測試。**
已寫進 `CLAUDE.md` 介面契約節，並在待辦表加一列（是否把 QX 對齊多數派待決）。

### ⚠️ 我在這輪犯的錯（當場更正）
用「函式最後一個 `return`」去推是哪一派，把 **SE3 / MH300 誤歸成 `true`=成功**
—— 它們最後一個 return 是**失敗**路徑。而我差一點就把這個錯誤分類寫進
`CLAUDE.md`（權威文件）和程式碼註解。
📌 **與「更正也是一種主張」同型**：真正救回來的是「寫進權威文件前先逐支讀原始碼」，
不是任何工具。⚠️ 一度還因此擔心現役吊機的 VFD 判斷是反的 —— 讀完整段才確認是對的。

### 測試執行結果（假從站全在 `127.0.0.1`，**不碰真 485 匯流排**）

| 測試 | 結果 | 關鍵正面斷言 |
|---|---|---|
| `test_qx_do24 normal` | ✅ 3/3 | req#1 `fc=0x10`、req#2 `fc=0x06` |
| `test_qx_do24 recover` | ✅ 2/2 | **`recovered on attempt 3/3` 真的出現** —— 08-28 加的重試救援路徑**第一次被執行到**（當時實機測 15 次一次都沒失敗，計數是 0） |
| `test_qx_do24 alldrop` | ✅ 2/2 | 全滅後 `last_fail_str() = no_reply_timeout`（不是 `OutOfRange`＝訊息不再張冠李戴） |
| `test_dm2j` | ✅ 7/7 | 讀數比值 **7.7310 = 導程**；🔴 **被拒絕的 60cm／-5cm 完全沒出現在假從站 req# 序列**＝守衛確實在送出任何位元組**之前**就擋下，不是事後回報 |

### 待完成
- 🔴 **`.out.new` 尚未取代 `.out`** —— 是否上機部署是使用者的決定，本輪刻意不動
- 🔴 **編譯通過 ≠ 行為正確**：吊機三個 commit（`abort_flag` 重置、緊急收繩張力警示）
  與本體三個（DM2J 回傳值、`p_err`／`partial_seal`、遮罩註解）**都還沒實機執行過**
- 🟡 `runbook.md` §1 的建置指令**不含 `Linux_test`**，而它是 `user_lib` 契約的另一端
  → 建議補進去，否則簽名改動會再一次只驗到一半
- 🟡 `runbook.md` 連線資訊那段仍寫「`server.js` 的 `CRANE_IP` 預設值**仍是 `.101`**（過期）」
  —— 該值已於 `f4e0d02` 修好，**runbook 也落後了**（與今早待辦表校準同型）

---

## 2026-08-29 — 待辦總表校準：這張「單一權威」自己落後於程式碼 7 列

### 機器狀態
兩台 Pi 仍在 main 分支的人手上（08-28 16:30 讓出）。本段全部是無硬體工作，
**沒有連線任何 Pi、沒有編譯、沒有部署**。

### 起因
接手時照 `CLAUDE.md` 的順序讀待辦總表，抽查其中幾條標「**未修** ✔」的高優先項時，
打開原始碼發現東西已經在那裡了。逐條回查之後：**80 列裡有 7 列與程式碼不符。**

### 七列的分佈（三種不同的過期方式）

| 類型 | 列 | 實況 |
|---|---|---|
| **已修但仍標未修** | SD76 `readRegister()` 不驗 CRC | `1a15588` 已補三道檢查（byteCount → 幀長 → CRC），且**先夾 byteCount 再拿它當長度用** |
| | `send()` 沒帶 `MSG_NOSIGNAL` | `9e1ad1b` 已在兩檔各定義 `SEND_FLAGS` |
| | `server.js` `CRANE_IP` 預設值寫錯 | `f4e0d02` 已改 `.10` 並在原地留下理由 |
| | `init()` 印寫死的 `MH300` | `f4e0d02` 已改吃 `CRANE_VFD_NAME` |
| **檔案路徑過期** | 上述 `MSG_NOSIGNAL` 那列、`SO_ERROR` 那列 | 仍寫 `user_lib/TCP_client.cpp`，但分層重構已把它搬到 `transport/`，**該路徑現在根本不存在** |
| **重複列** | `TCP_client` 缺 `SO_ERROR` | 與表格第一列是同一件事，2026-06-09 與 2026-08-28 各記了一次 |
| **數字過期** | 5 個 `.vcxproj.user` 被追蹤 | 實際 4 個（`windows_test/` 已於 `a69f82f` 整個移除）；且原文「`.gitignore` 未加」易被讀成整個檔不存在——**它存在，有 12 條規則，只是沒加這一條** |

另有一列由「未修」改為「**部分處理**」：緊急收繩張力保護。`b1234ad` 已在 `hold_loop()`
補上警示與廣播（此前該路徑張力既不檢查也不回報），但**刻意不呼叫 `hold_all_off()`**
——§8 明訂緊急模式由操作員眼睛判定，自動停止會擋住救援。**這不是「修好了」，是「換了一種
不完整」**，所以留在表上而非劃掉。

### 📌 這次的通則：**單一權威清單本身也會落後於它描述的東西**

本專案已經記過「同一件事寫在兩個地方，遲早分岔」（`zdt_pusher` 範圍無交集那次）。
這次分岔的兩處是 **程式碼** 與 **描述程式碼的那張表** —— 而表被明文指定為「單一權威」，
**正因為它是權威，沒有人會去懷疑它。**

🔴 **危害是雙向的，而且反向那個更貴**：
- 正向：去修一個已經修好的東西（浪費，但會當場發現）
- **反向：表上一列寫著「已修 ✔」而實際沒修，就永遠不會有人再去看它** ——
  這一輪抽查的是「未修」那半邊，**「已修」那半邊還沒有人查過**

📌 **做法上的修正**：`✔` 這個記號原本的定義是「這次有實際比對原始碼」，但它**不帶日期**，
所以看不出是哪一次比對的。這次校準過的列一律在來源欄補上 `｜2026-08-29 複查原始碼確認`。

### 抽查過但**確認仍然未修**的（表上狀態正確，不用重查）
- `run_depth_avoid` / `depth_avoid_continue` / `depth_avoid_stop` 三個指令仍 dispatch 到真實實作
  （`facade_cleaning_v2/main.cpp:184-192`），前端已無 UI → **後端仍能自行改走 cross 步伐**
- `trigger_sync_move()` 仍以 `return resp.empty();` 收尾（`ZDT_motor_control.cpp:611`）。
  廣播（slave `0x00`）本來就不會有回應 → **永遠回報 error**。
  ⚠️ 目前無害只因為四個呼叫端**都不接回傳值**；哪天有人加上 `if (trigger_sync_move()) …`
  就會整條同步移動全滅，而它看起來完全像是正確的錯誤處理
- `CLV900_inverter` 仍無 null-client 防護（`:32` 直接 `client->connectToServer`）
- 上滑台零點仍是 `0x0021`「設當前位置為零」而非 homing；`home_start()` 仍無人呼叫
- `ARM_SWEEP_DECEL_MASK_MS` 遮罩仍未生效（`5560318` 只是把成因寫進註解，未改行為）

### 待完成
- 🔴 **表格「已修 ✔」那半邊尚未複查** —— 本輪只抽查了「未修」側
- 本輪所有校準都是文件變更，**未動任何程式碼**

---

<!-- [2026-08-29 merge 6523b54] 以下兩則來自 origin/main（Sadie-fang）。
     原本在對方檔案的最上方；本檔最上方是待辦總表，故移到日期區。
     🔴 對方 commit message 寫「實機行為全部未驗證」——**該句已過期**：
     per user 2026-08-29，那支已由該工程師實機測試過，並完成由上往下洗的流程。 -->

## 2026-08-29 — 📌 交接：同步步伐 PWM 輸出 + 清潔恢復 + 自動補救全面停用（下次接手先讀這條）

> **規範權威：** `.claude/changelog.md` `[2026-08-28f]` ~ `[2026-08-28u]` 共 13 條（逐筆有檔案+行為+理由）。本節只整理「現在是什麼狀態、還缺什麼」。
> 本輪只動 `user_lib/WASH_ROBOT.{h,cpp}`、`Crane_control_PI/main.cpp`、`web_backend/public/{index.html,app.js}`，沒有新增檔案、沒有動任何 driver class 的 public API。

### 一句話

同步步伐（`do_step_sync_`）這一輪加了 PWM 輸出控制、恢復了內建清洗、並把**所有自動補救機制全部停用**——現在吸不好就停住等人，不再自己退回重試或補推。

---

### A. 同步步伐現在長這樣（改動最集中的地方）

```
解真空 -> 等 4 顆釋放 -> [PWM 占空比 7%] -> 靜置 300ms -> 兩段式收腳
-> arm INIT 並行 + 吊機左右同步放/收繩 -> 靜置 300ms
-> IMU 差動校平（內部再等 800ms）-> [PWM 占空比 5% = 關] -> 抽真空
-> 4 顆同時伸出 ┬-> 清洗（DEPLOY RIGHT 滾筒 -> 滑台 0->17 -> DEPLOY LEFT 刮刀 -> 滑台 ->0 -> PARK）
                └-> (並行)
```

**PWM（QX-DO24, cli_22_ slave 9, 通道 1）**
- 只寫「占空比」一個暫存器，頻率與控制字完全不碰（`pwm_set_duty_only_()`）；**不會寫 flash**，唯一寫 flash 的入口仍是面板的「保存參數」按鈕
- 7% = 運轉、5% = 停止（driver 強制鎖 5~10%）
- 開啟失敗只印 warning 不擋步伐；**關閉失敗會用 `try_or_pause_` 擋住**——「該關沒關就去伸腳」是唯一有安全意義的那一側
- `fail()`（所有提早返回的唯一出口）會補寫 5%，避免步伐中途 abort 時輸出一直開著
- bench 遇過偶發 `[QX:9] no reply (timeout)`（同一步稍後的寫入卻成功）→ 已加最多 3 次重試、間隔 120ms。**根因未查明**，重試是止血

**清洗恢復**
- `STEP_SYNC_ARM_CLEAN_ENABLED` false → **true**（上滑台 DM2J:14 已接上，user 確認）
- `CH_BRUSH` **15 → 5**（實體確認接在 CH5；2026-07-24 那次改成 15 是錯的，導致「流程照跑但滾筒不轉」查了很久）
- 滾筒開關窗口 = **DEPLOY RIGHT 之前開、DEPLOY LEFT 之前關**，中間全程轉；DEPLOY RIGHT 失敗不改變關閉點
- 貼牆距離三個常數統一 **380 → 400mm**

### B. 自動補救全部停用（安全語意改變，務必知道）

v2 現在的態度是**吸不好就交給人判斷**。兩個機制先後停用，原碼都用 `#if 0` 保留在原地：

| 機制 | 原行為 | 現在 |
|---|---|---|
| 後退重吸（`[2026-08-28k]`）| 整側沒吸住 → 全縮回 → 吊機退 10cm → 重吸 → 一路退到起點 | 直接回 `ERR side_unsealed`，**停住不動** |
| 原地補伸（`[2026-08-28t]`）| 整側全裸 → 對該側再 `smart_extend_subset_` 一次 | 不補推，直接進 `group_seal_ok_` 判定 |

停用補伸的理由來自 bench log：第一次伸出時 `disable_seal` 已經打到 `WALL I=1242mA ... endpoint`，結論是「推到牆、真空還是吸不起來」，再推一輪只是用更大電流頂玻璃。

**停住時的實體狀態**：推桿伸出但未吸住，機器靠鋼索吊著；PWM 已寫回 5%；手臂已 PARK（停住之前會先等清洗掃動結束）。呼叫端會切 `State::Error` 並停止後續步伐。

「每側 ≥1 顆即算吸好」(`group_seal_ok_`) 的判準沒有改變。

### C. IMU 校正失效的根因（已修）

bench log `[step_sync_imu] IMU read_error mid-pass — stop`：整步水平校正被取消，同一步吊機回報左右差 2cm 就這樣累積下去。

**根因**：`WT901BC_TTL::read_error` 是**逐封包**旗標——checksum 錯一包設 true、下一包正常就清回 false。裝置 115200 持續串流，偶發壞一包很正常。但兩處 gate 都是**瞬間讀一次**就判定 IMU 不可用。同函式內的 `read_roll_avg` 早就做對了（6 樣本、跳過壞的、全壞才 fallback），gate 反而比它嚴格。

**修法**：新增 `imu_persistently_bad_(samples=6, gap_ms=50)`，掃 300ms 視窗，期間有任何一次正常就當可用。`do_sync_imu_roll_correct_`（sync）與 `follower_imu_level_`（alt）兩條路徑都改了。

### D. 其他

- `cmd_run_script`：迴圈開始前先清洗一次（`run_script_pre`）——清洗綁在步伐尾段，沒有這段的話**起始位置那一格永遠不會被洗到**。按下 run 之後會先原地不動約 30 秒才開始走，那是在洗第一格，不是當機
- `STEP_CM_MAX` 80 → **100**（per user：script 要能到 100cm）。⚠ 這是共用上限，手動單步 / 自動循環 / 深度避障 clamp 一起被放寬
- 前端 `parseScriptCsv` 的 5..50 → 5..100（原本前端 50、後端 80 本來就不一致）
- crane `UP_STOP_TOTAL_KG_DEFAULT` 50 → **70**（UP hold 的左右總和門檻）

---

### ⚠ 下次接手要做的事

**1. 重新編譯 + 部署（本輪改動全部未實機驗證）**

| 程式 | 要不要編 | 原因 |
|---|---|---|
| `facade_cleaning_v2/`（washrobot @ .100）| ✅ 一定要 | 全 repo 唯一 `#include "WASH_ROBOT.h"`，本輪改動幾乎都在這 |
| `cleaning_arm/motor_api`（:9527）| ✅ 要 | `main_api.cpp` 的 `MAX_LOOPS` 100→150（`[2026-08-28j]`）。`cd cleaning_arm && ./compile.sh` |
| `Crane_control_PI`（@ .101）| ⚠️ 看上次編譯時間 | 只有 `UP_STOP_TOTAL_KG_DEFAULT` 一項 |
| `web_backend/` | ❌ 不用編 | 靜態檔；重開 node server + 瀏覽器硬重新整理（否則吃到快取舊 `app.js`，script 上限還是卡 50）|
| `Linux_test/` | ⚠️ 只有要用 bench 工具時 | 不 include `WASH_ROBOT.h`，本輪改動與它無關 |

本機只做過 `cl /Zs` 語法檢查（方法見本檔 2026-08-28 那條），錯誤集合與 HEAD 相同=無新增語法問題。**語法對 ≠ 行為對。**

**2. 上機第一輪要盯的 log**

- `[pwm_step] step_move_on -> ch1 duty=7.0% OK`——沒有這行就是 PWM 沒起來（看有沒有「第 N 次才成功」或「3 次全滅」）
- 滾筒有沒有真的轉。⚠ PQW 韌體的 echo 只驗長度不驗內容，**寫入回 OK 不代表繼電器真的切了**，通道錯更是完全抓不到——只能看實體 LED
- `[DM2J:14] writeMulti no response` 應該要消失（上滑台已接上）
- `arm deploy RIGHT (brush) failed`——DEPLOY 失敗會**靜默降級**成「只掃滑台、不開刷」，看起來正常但沒在清洗

**3. 已知未解**

- **`DEPLOY 400` 兩側 bench 都失敗過**：LEFT `err=0.293 rad`、RIGHT 觸發 `[M1 SAFETY] vel>0.4` 緊急煞車。牆距從 380 加大到 400 後 M1 外擺更多、重力力矩更大，這個症狀可能更明顯。要修是動 `cleaning_arm/main_api.cpp` 的 touch_wall 收尾（重力前饋 / kp / 速限），不是牆距數字
- **`ROLL PANIC -150.9°`**：不可能是真實姿態。可能是 `read_roll_avg` 在 `n==0` 時 fallback 讀到壞的原始 `imu_.z`，也可能是角度在 ±180 繞回。本輪修的 gate 會讓更多樣本被採用、**可能**連帶減少，但沒有加任何離群值剔除
- **`ERR meter_left_read_fail`（吊機左計米器）**：老問題（見 memory `project_v2_crane_meter_read_fail_OPEN`），workaround 是重開 crane 程式。本輪查出**為什麼一直查不到根因**——`meter_read_robust()` 的 hard-fail 路徑 `return {false,0,true}` 之前一行 log 都沒有，而 `meter_left.init(cli_M, ..., false)` 讓 SD76 driver 的 LOG_ERR 也關著，兩層都靜默。另外**單一次讀取失敗就會 `valid=false`**，沒有連續失敗容忍，一個掉幀就足以擋掉整個 `pay_out`。建議修法（尚未實作，需動 `Crane_control_PI/main.cpp`）：hard-fail 路徑加 rate-limited log + 連續 N 次才 invalidate + 回覆字串帶連線狀態
- **script 的 `n`（不刷洗）旗標在 v2 仍然無作用**——只影響 log 的 `mode=transit` 字樣與統計，實際每步都會清洗。GUI 輸入框提示「n=不刷洗」目前是假的
- **文件落後**：`CLAUDE.md` 硬體表的 PQW 通道表仍是 v1 的 8CH 分區描述；`Linux_test` menu 5 畫面上那張 channel map 也是舊的（寫 CH6 水箱泵浦、CH16 破真空）。現行 map 是 **CH1 閥 / CH2 泵浦 / CH5 滾筒 / CH6 破真空 / CH14 水泵**。這次「滾筒不轉」某種程度就是這些過期文件養出來的

**4. 這一輪的取捨（不是疏漏，改之前先看理由）**

- PWM 只寫占空比、不寫頻率/控制字——per user 明確要求。代價：5~10% 對應「停止~全速」只在頻率已是 50Hz 時成立
- 交替走法 `do_step_down_`/`do_step_up_` 與 `do_cross_obstacle_` **沒有**加 PWM 控制，也沒有 pre-sweep——它們一次只放一側、每步有多段吊機移動，語意對不上，要加需另外定義
- 吊機移動後的靜置有兩段（`CRANE_MOVE_SETTLE_MS` 300 + `FOLLOWER_IMU_SETTLE_MS` 800 = 1.1s），刻意不合併：一個是「動作結束收尾」、一個是「取樣前靜置」


## 2026-08-28 — 開發環境：本機可以在 commit 前做 C++ 語法檢查

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
