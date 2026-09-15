# orchestration_to_cpp_plan.md — 編排搬回 C++、SAFE 狀態機、自檢欄位

> 📌 **2026-09-13 建立,等核准再動碼**(大改先給計畫)。依據三條拍板(`reference/field_procedure_v2.md` §6):
> ① 週期編排搬回 C++ ② SAFE 狀態機最小版 ③ 開機自檢 = GUI 軟閘門(作業流程 checklist)。
> 流程權威 `field_procedure_v2.md`;硬體邊界 `HARDWARE.md` §0(兩箱只共用 220V 與隧道 ⇒ 跨箱只走 TCP)。
> 本檔回答「介面長什麼樣、怎麼證明等價、分幾階段」。**介面先凍結 → fake_robot 先實作 → GUI 離線開發 → C++ 上機只驗 C++。**

---

## 0. 為什麼

- 現在真正的步態在 `Linux_test/cycle_test.py`,C++ 的 `step_*`/`run` 被繞過;**姿態監看(roll 中止 + 復原)也在 Python** ⇒ 不跑測試腳本就沒有這層保護,正式作業反而最裸。
- 「會動手」的安全動作散在四處(吊機 hold_loop / 吊機 watchdog / 本體急停 / Python 監看),**沒有一個統一的安全狀態**,也沒有「維持吸盤」這種跨箱協調。
- GUI 若要不看 stdout,C++ 必須把「在哪一步、等到幾顆、跳過幾步」用 EVT 講出來。

## 1. 範圍 / 非範圍

| 做 | 不做(本期) |
|---|---|
| 本體 `mission` 指令族(= cycle_test full 的等價 C++ 實作)+ 步內 EVT | 多列作業、橫移、回頂重跑 |
| 本體 `State::Safe` + 吊機 `safe_enter/safe_clear`,觸發三源(張力 / watchdog / roll) | 環境感測(風、漏水)、聲光通報硬體 |
| 本體 `status` 補自檢欄位;手臂 `STATUS` 補 `init_done` | 吊機硬閘門(否決,擋救援) |
| fake_robot 同步新介面;GUI 作業流程頁 + Mission 改按鈕 + SAFE banner | 清潔度感測、重刷 |
| 等價驗證(指令序列 + harness) | 把 cycle_test 刪掉(降級為對照工具,**保留**) |

---

## 2. A · `mission` 指令族(本體 :5001)

### 2.1 指令與回覆

| 指令 | 回覆 | 說明 |
|---|---|---|
| `mission start <steps> <step_cm> [cycles=1] [nm=8] [rail_cm=100]` | `OK mission started id=<n>` / `ERR precheck_failed item=<k> detail=…` / `ERR busy state=…` | 先跑 §2.3 前置檢查,全過才起背景執行緒 |
| `mission stop [reason]` | `OK stopping` | 收尾 = cycle_test `cleanup()`:`arm_retract`、fan OFF、motion_hz 寫回;**吊機與推桿不復位** |
| `pause` / `continue` / `skip` | (既有) | **重用**現有 `try_or_pause_` / `PauseAction`,不另造一套 |
| `mission status` | `OK mission=<phase> cyc=1/1 step=3/5 sub=vac_wait n_seal=2 t_step=12.3 skipped=1 …` | 也併入 `status` 的 `mission=` 欄 |
| `mission params` / `set_setting mission.* v` | 既有 settings.json | `VAC_OK_KPA=-50 VAC_WAIT_S=10 FAN_ON=7 FAN_OFF=5 DOWN_HZ/UP_HZ TOL BOTTOM` 進 settings(現為 Python 常數) |

狀態:重用 `State::Running`(mission 執行中)/ `Paused` / `PausedOnError`;`status` 加 `mission=` 子狀態欄,**不新增 State**(除了 §3 的 `Safe`)。

### 2.2 EVT(GUI 只吃這些,不解析 log)

```
EVT mission start id= steps= step_cm= cycles= top= bal=imu|meter
EVT mission step_begin cyc= step= h=<cm>
EVT mission vac_wait n_seal=<0-4> p5= p6= p7= p8= t=<s>          (③a 每 0.3 s 一筆或只在變化時)
EVT mission vac_result n_seal= skip_clean=0|1
EVT mission clean tool=RIGHT|LEFT wet=0|1 result=ok|warn|no_wall|obstacle|skipped
EVT mission move verb=pay_out cm= hz=                              (⑦ 開始)
EVT mission step_done cyc= step= t_ext= t_vac= t_clean= t_ret= t_move= roll_after=
EVT mission cycle_done cyc= up_s=
EVT mission stop reason=<user|precheck|bail:…|safe> summary="no_seal=1 warn=0 skipped=1 roll_fixed=0"
```
格式與現有 `EVT init_complete` / 吊機 EVT 一致(空白分隔 k=v),GUI 用前綴分流。

### 2.3 前置檢查(搬進 C++,對應 GUI「起跑前置 6 項」)

| 項 | 判準(與 cycle_test 逐字相同) | 失敗回覆 |
|---|---|---|
| top | 吊機 `length_left` → 高度 = TOP ± TOL | `ERR precheck_failed item=top` |
| state | 本體 idle / ready | `…item=state` |
| pump | `relay_status` **names 欄推導** pumpA/pumpB 任一 =1(不寫死通道) | `…item=pump` |
| bal | `balance_source=imu`;是 meter 就**自動改並 EVT 大聲說**(不擋) | — |
| hz | `set_motion_hz DOWN_HZ` | — |
| roll | \|roll\| ≤ 1° | `…item=roll` |

### 2.4 cycle_test → C++ 原語對照(全部既有,零新驅動碼)

| cycle_test | C++ |
|---|---|
| `fan(pct)` + `pwm restart` 重試 | `cmd_pwm` 路徑(`pwm_.setDuty` + restart) |
| `vacuum feet on` | `cmd_vacuum` → `pqw_` CH1 |
| `pusher all extend_raw` / `retract` | `smart_extend_subset_("feet")` 或 `pusher_extend_with_disable_seal_` / `pusher_two_stage_retract_` — 🔴 **選 `extend_raw` 等價路徑**(一段到 10 cm),不用兩段式,保持與 09-11 實跑等價;兩段式是之後的改進 |
| ③a 等真空(任一 ≤ −50,10 s) | `vacuum_wait_release_` 反向版 → 新 `vacuum_wait_seal_(n_needed=1, timeout)` 讀 `meter_[]`;`group_seal_ok_` 判準改 ≥1 |
| `arm_deploy_f` / `arm_retract` / `water_pump` / `brush` / `water_level` | `arm_cmd_("DEPLOY_F …")` / `cmd_water_pump` / `cmd_brush` / `cmd_water_level` |
| `rail 100` / `rail 0` | `rail_sweep_run_` 同步版(等完成) |
| `pay_out 40` + 監看執行緒 | `crane_cmd_("pay_out 40")` + 新 `mission_monitor_` 執行緒(§3.2) |
| `roll_recover` + `clear_error` | `crane_cmd_("roll_correct …")` 序列 + 吊機 `clear_error` |
| `set_motion_hz` | `crane_cmd_` |
| `bail()` | `try_or_pause_` → `PausedOnError`;GUI `continue/skip/emergency_stop` |

執行緒模型:`mission start` 在 detached worker 跑(同 `cmd_run` :4211 的做法);持 `motion_mtx_`;`pause/stop` 走 FAST 路徑。

---

## 3. B · SAFE 狀態機(最小版)

### 3.1 定義

新 `State::Safe`(第 12 個)。**本體是 SAFE 的擁有者**;吊機有一個 `safe_locked` 旗標。

| 觸發源 | 現況 | 接法 |
|---|---|---|
| 張力(過載 / 差值 / 上限 / 收繩停) | 吊機 `hold_loop` 已 `hold_all_off` + `EVT tension_alarm` | 吊機**先自己停**(不變)→ 本體 `crane_alarm_pending_` 已接收 → 轉 SAFE → 回送 `safe_enter`(冪等) |
| 通訊 | 吊機 watchdog(motion 逾時)有;**本體端監看吊機是死碼**(待辦 🔴🔴) | 🔴 **前置:先修本體 watchdog**(AI-2 的 `ai2-watchdog-restore-patch.md` 已預寫,等拍板)。修好後逾時 → SAFE |
| 姿態 | Python | `mission_monitor_`(§3.2)roll > 門檻連續 N 次 → 先 `roll_recover` 一次,失敗 → SAFE |
| 人 | `emergency_stop` | 改為進 SAFE(不再只是 Idle) |
| 實體急停 DI | 未知有無(HARDWARE §3.2C #12) | 預留:有的話接 DI → SAFE |

### 3.2 `mission_monitor_`(取代 cycle_test 監看執行緒;**mission 外也常駐**)

每 250 ms:讀吊機 `status`(tension_valid / imu_roll / L−R)+ 本體 IMU。判準與 cycle_test `monitored_crane_move()` 逐字相同:
`tension_valid=0` → SAFE(不修);左右差持續擴大 → SAFE(不修);roll 連續超標 → 先 `roll_recover`,失敗 → SAFE。
📌 mission 外也跑 ⇒ 手動 hold 放繩時也有姿態保護 —— 這是搬回 C++ 的核心收益。

### 3.3 進入動作(順序固定)

```
本體:  ① mission worker 停(下一個 try_or_pause_ 檢查點) ② 送吊機 safe_enter ③ 真空閥保持(不動 pusher、不破真空)
       ④ arm_retract(不失能) ⑤ water_pump off、brush off ⑥ fan → **直接關(FAN_OFF)**,不看封合
       ⑦ EVT safe_enter src=<tension|watchdog|roll|user|di> detail=…
吊機:  safe_enter → stop + hold_all_off + safe_locked=1;拒絕 pay_out/retract/hold(回 ERR safe_locked);status 加 safe=1
```
✅ **2026-09-14 per user 拍板:① 是「暫停可續」,不是終止** —— mission worker 停在下一個 `try_or_pause_` 檢查點、任務物件保留;
SAFE 期間 `continue`/`skip` 一律回 `ERR safe_locked`;只有 `safe_clear` 通過後才能 `continue`(或 `mission stop` 放棄)。
理由:重用 `PausedOnError` 現成路徑、SAFE 多半在步進中途、昨天「有 mission → paused」本來就預設任務還活著。fake 已同步(不再 `request_stop('safe')`)。
✅ ⑥ **2026-09-13 per user 拍板:直接關**。理由與「吸附時關槳」一致 —— SAFE 下靠吸盤/鋼索撐,不靠推力;槳轉著是額外風險(單路 PWM、斷線維持輸出)。設計「槳降至保壓」不採用。

### 3.4 解除

`safe_clear <reason>`(**只從 GUI,且在危險操作 60 s 解鎖窗內**)→ 本體驗證:`tension_valid=1`、\|roll\| < 門檻、吊機/手臂連線 fresh → 送吊機 `safe_clear` → 轉 **`Paused`**(不是 Running;mission 可 `continue` 或 `mission stop`)→ `EVT safe_clear by=gui reason=…`。
驗證不過 → `ERR safe_clear_refused item=…`。
✅ **2026-09-14 per user 拍板(依建議)**:**有 mission → `paused`;無 mission → `ready`(幫浦開)或 `idle`。** fake_robot 已如此。
✅ **2026-09-14 per user 拍板②**:自檢頁 ③ `zeroed=0` 是**軟提示不硬擋**(重啟 ≠ 計米器讀值失效;真要擋的是讀值不合理,之後用 length 與 home_ground 的關係判)。
~~🟡 待拍板(AI-2 做 v3 時發現,2026-09-13)~~:無任務時 `safe_clear` 回哪個狀態? 上一段「轉 Paused」是以有 mission 為前提;
無任務時回 `paused` 會讓 `mission start` 被前置檢查(state 需 idle/ready)擋住,得先 `continue` 才能起。
**建議**:有 mission → `paused`;無 mission → **`ready`**(SAFE 前若已 init)或 `idle`。fake_robot 先照建議實作,待拍板後對齊。

### 3.5 通報

EVT `safe_enter/safe_clear` + `status safe=1 safe_src=`;GUI 全頁 banner(現有 `estop-banner` 擴充)+ log。聲光硬體不在本期。

---

## 4. C · 自檢欄位(GUI checklist 需要、C++ 要吐的)

| 程式 | 新欄位 / 指令 | 來源 |
|---|---|---|
| 本體 `status` | `dev_gw20= dev_gw21= dev_gw22= dev_zdt=<n_ok>/4 dev_pqw= dev_dm2j= dev_jc100=<n_ok>/4 dev_xkc= dev_qx= dev_imu= dev_arm= selfcheck_age_s=<s|-1>` | ~~`init()` 各驅動結果旗標~~ 🔴 **09-14 實作時推翻**:本體 `init()` 對 ZDT/DM2J/JC-100/XKC/QX 是 Mode B(只綁 client,**不發包**),init 旗標永遠是 1、證明不了裝置在線。改為 **`selfcheck` 指令做真探測**(每裝置一筆讀、不動作;`zdt_bus_mtx_` try-lock,忙碌回 `ERR busy`),`status` 只回快取 + `selfcheck_age_s`;`init()` 結尾自動跑一次。`dev_gw2x`/`dev_jc100`/`dev_imu`(2 s 內有 0x53 封包)/`dev_arm` 是即時的。**GUI「重讀」要先送 `selfcheck` 再 poll。** |
| 本體 `status` | `zdt_homed_at=<epoch|0>` | `zdt_home` 成功時記;**blip 無法自動偵測**,GUI 顯示「距上次 X 分鐘」由人判斷 |
| 本體 `status` | `arm_ready=0|1` | 最近一次 `arm_status` 的 `en=1` 且手臂 `init_done=1` |
| 手臂 `STATUS` | `init_done=0|1` 加在 `[M1]` 行尾 | `INIT` 成功置 1 |
| 手臂 | `PING` → `OK pong` | ✅ 09-14 per user 拍板:**手臂加 PING**(不改 web_backend 保活);解掉 `ERR unknown command` 洗版 |
| 吊機 `status` | `zeroed=0|1 zeroed_at=<epoch|0>`(本行程內 `zero_meters` 成功過)、`safe=0|1 safe_src=` | 已有 `dev_gw_*/dev_meter_*/dev_dsz_*`。⚠️ **行程記憶**:SD76 計數跨重啟保留、`start_crane.sh` 會補 `set_home_ground`,所以重啟後 `zeroed=0` ≠ 要重新歸零 —— GUI 顯示「本次啟動未歸零」由人判斷(09-14) |
| 吊機 | `safe_enter` / `safe_clear`(§3) | 新 |
| 點動 | 不需 C++:GUI 送 `pay_out_left 2` → 看 `length_left` 變 2 → 再 `retract_left 2` | GUI 驗 |

---

## 5. D · GUI 作業流程頁(由 A/B/C 推導;實作歸 AI-2/GUI 線)

```
作業流程(取代「散按鈕 + 前置檢查」)
 ① 連線     c-wr/c-cr/c-ar 三綠 + 走隧道/WiFi
 ② 裝置     本體 dev_* 全 OK · 吊機 dev_gw_*/dev_meter_*(中繩除外)/dev_dsz_* · 手臂 en=1
 ③ 歸零     吊機 zeroed=1(一鍵 zero_meters ground)
 ④ 點動     一鍵 pay_out_left 2 / retract_left 2,回讀 length 變化
 ───── 全綠才開放「升頂」(hold up 按鈕仍在 Manual,不鎖) ─────
 ⑤ 升頂記牆高(現有 wall 功能)
 ⑥ zdt_home feet(一鍵 + zdt_homed_at 距今)
 ⑦ init(pumpA/B 回讀)· 手臂 INIT(init_done=1)
 ⑧ 起跑前置 6 項(現有,改讀 C++ precheck 回覆而非自己算)
 ───── 全綠才開放「Mission start」 ─────
Mission:start/stop/pause/continue/skip 按鈕 + EVT mission 驅動的步內顯示(哪一步 / n_seal / 工具 / 跳過計數)
SAFE:全頁 banner + src + 「解除」(危險操作解鎖窗內)→ 回 Paused
Manual:SAFE 時上鎖(只留 status/stop/safe_clear;吊機 raw hold 仍可 —— 救援)
```

---

## 6. E · fake_robot 先行

介面(§2.1/2.2/§3/§4)凍結後**先在 `harness/fake_robot.py` 實作**:`mission` 狀態機推 EVT、`safe_enter/clear`、新欄位。
⇒ GUI 在沒機器的情況下開發完;`gui_cmd_log` 同時記錄 GUI 對新介面的依賴。

---

## 7. F · 等價驗證(證明 C++ mission = cycle_test)

1. **指令序列等價**(不需機器):cycle_test 打 fake_robot → `gui_cmd_log` 得序列 A;C++ mission(本機 g++ x86_64 建)打 fake_robot → 序列 B。**逐條 diff**,允許的差異只有時間戳與 `status` 輪詢次數。這是最強且最便宜的一條。
2. **語法 / 預處理**:本機 `g++ -fsyntax-only`;純搬動部分用 `-E -P` 比對(09-12 已驗證這條路能走)。
3. **harness 匯流排位元組**:`harness/compare.sh` 對「跑舊 cmd_run」vs「跑 mission」—— 只驗共用原語沒被改壞。
4. **上機**:先 `mission start 1 40 1` 一步,對照 09-11 的 full 表格(伸出 s / 真空 s / 清潔 s / 收回 s / 移動 s)。

---

## 8. 分階段(每階段可獨立 commit、可回退)

| 階 | 內容 | 風險 | 驗證 |
|---|---|---|---|
| 0 | 介面凍結(本檔 §2–§4)+ fake_robot 實作 + GUI 開始 | 無 | fake 自測 |
| **前置** | **`crane_cmd_` 行緩衝**(EVT 半行誤當回覆)+ **本體 watchdog 復活**(AI-2 patch)—— ✅ 09-13 per user 確認為階段 3 前必修 | 中 | 拔線 / 注入半行 EVT 測 |
| 1 | ✅ **09-14 完成、上機驗過**:自檢欄位(§4):本體 dev_* / `selfcheck` / zdt_homed_at / arm_ready;手臂 init_done + PING;吊機 zeroed/zeroed_at | 低(只加欄位) | 真機 `selfcheck all_ok=1 zdt=4/4 pqw=1 dm2j=1 xkc=1 qx=1`、`PING`→`OK pong`、`init_done=0`(只 STARTUP 未 INIT,正確) |
| 2 | **前置修 watchdog**(本體端監看吊機,AI-2 patch)| 中 | 拔線測 |
| 3 | **mission 引擎**(§2)+ `mission_monitor_`(§3.2) | 中高 | §7-1 序列等價 + 上機一步 |
| 4 | **SAFE**(§3):State::Safe、吊機 safe_locked、三源接線、解除 | 中高 | 人為觸發三源各一次 |
| 5 | GUI 切換到新介面(Mission 不再 spawn 腳本)| 低 | fake + 上機 |

**cycle_test 全程保留**當對照;階段 3 完成前它仍是正式路徑。

---

## 9. 前置與風險

- ✅ **09-14 修畢、真機部署**:`crane_cmd_` 收包緩衝改成員 + 送前整行消化(真因含 `TCP_client::sendData()` 的 4 KB 靜默排空);
  回歸測試 `harness/crane_linebuf_test.sh`(修前 2/6 → 修後 6/6)。
- 🟡 本體 watchdog:09-10 已補回為**觀測模式**(`crane_wd_abort_ms=0`)。運動中峰值尚無實測 ⇒ ④ full 跑完讀 `crane_idle_ms_max_motion` 訂門檻;abort → SAFE 在階段 4 接。
- `motion_mtx_` 非重入:mission worker 持鎖,內部呼叫 `do_feet_realign_` 類函式要走 `unique_lock` 手動放(`engineering_pitfalls.md` §2.4)。
- `arm_cmd_` INIT 逾時真因未明(待辦);mission 不在步內送 INIT,只在 checklist ⑦。
- `emergency_stop` 語意改變(Idle → Safe)要同步 GUI 與 runbook。
- 吊機執行期參數不持久化(待辦):`mission` 依賴 `motion_hz/balance_source`,階段 3 前至少讓 mission 自己每次設。

---

## 10. 非範圍(明列,免得被撿回來)

多列作業 · 環境感測 · 聲光通報 · 清潔度判定 · 兩段式伸桿(等 mission 等價後再改進)· 吊機硬閘門 · 刪 cycle_test。
