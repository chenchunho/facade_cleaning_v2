# v2 應用層重寫計畫 — init / attach / step（新機械）

> 規範權威：本檔（應用層流程）。機械架構見 memory `project_v2_mechanical_gait`；
> 吊機變頻器見 `project_new_crane_vfd_mh300` + `.claude/mh300_migration_plan.md`。
> 本階段 scope：**init / attach / step_down / step_up**。其他（return_home /
> balance_cal / 清洗 / obstacle）之後另議。

## 決定紀錄（2026-07-07 user 拍板）

- 吸盤：4 顆，**推桿 slave 1,2=右腳 / 3,4=左腳**（右腳 上=1/下=2、左腳 上=3/下=4；單顆機構同 v1：ZDT/SMC 推桿 + 真空 + JC100）
- 真空：**2 區**，右閥 CH1（{1,2}）/ 左閥 CH3（{3,4}）/ 泵 CH2。**無中心吸盤**
- 位移：**單側吊機繩放/收 + SD76 計米量測**（無 DM2J）
- **step 位移量：每側各放/收「使用者輸入的 step_cm」全量**（非拆半）；順序 **右側先、左側後**（2026-07-07 user 詳細步驟為準，取代最早口述的「左先」示意）
- **伸腳一律沿用 v1「一點一點補伸 seal」**（`smart_extend_subset_` + disable-seal + `fine_tune` 逐步找 seal）；**禁止單發盲伸**。attach 與 step 皆同
- **「吸不好重吸」重試/backup 全保留**（2026-07-07 user 強調）：step 每側沿用 v1 `cycle_group_` 重試框架（extend→驗真空→沒吸牢就退一點 backup→重試 N 次→rescue→全失敗才 Paused）。**只把 v1 backup 的 DM2J rail 退位改成吊機繩微移到新牆點**（pre_cycle 主位移＝crane pay_out/retract step；backup＝crane 微移 VACUUM_BACKUP_CM）。**backup 方向＝主位移的反方向、退回原位**（2026-07-08 修正）：step_down 主 pay_out（下）→ backup retract（上）；step_up 主 retract（上）→ backup pay_out（下）。**累積退回以原位為上限**（`cumulative_backup_cm ≤ step`，vacuum retry + obstacle rescue 共用；退到原位仍失敗 → PausedOnError），對應 v1 rail `[0,step]` range 的作用（v2 無 rail 改用計數）。`feet_target_capped_` 需保持 LIVE（cycle_group_ 非-body 分支會呼叫）
- 水平：**IMU roll/pitch + 左右計米繩長差**，兩者都用（step 收尾做 check，兩側同 step_cm 本身即對稱）
- 橫向：**無**（只垂直上下）
- 單側量測放繩：**crane 端新增指令**（見 §5）
- 清洗手臂：**仍需要但目前未裝** → step 收尾 sweep **先註解掉**、留 TODO

## 1. 核心架構轉變：DM2J 滑軌 → 吊機繩

v1 垂直位移 = DM2J 腳滑軌（編碼器精準，`rail_pos_cm_`）當相對位移致動器。
v2 = **單側吊機繩放/收、SD76 計米量測位移**。連帶作廢：
- 步伐補償（滑軌閉回 baseline）、`rail_pos_cm_` / `body_residual_cm_` 追蹤
- `dm2j_*` 全部（滑軌 `dm2j_pair_move_abs_` / 輪組 / 臂軌 `arm_sweep_fire_nowait_`）
- 分區 腳/身/中 → 左/右

## 2. 設定層改動（group config）

改 `WASH_ROBOT.cpp` 的 group 定義（v1 L5431+）：

| 項目 | v1 | v2 |
|---|---|---|
| `group_slaves_` | feet{1,3,4,2} body{5,6,7,8} center{9} | **右{1,2} / 左{3,4}**（右腳 上=1/下=2、左腳 上=3/下=4，2026-07-07 user 確認）|
| `group_valve_ch_` | feet=CH2 body=CH3 center=CH4 | **左=CH3 / 右=CH1**（PQW @ .22，2026-07-07 user 確認）|
| `preset_extend_pulse_for_slave_` | 9 slave 各值 | **4 slave**（1-4，實測校）|
| `cm_to_pulses_for_slave_` | feet 2857 / body 3000 pulses/cm | 4 顆實測（機構同腳組？待確認）|
| `disabled_zdt_slaves_` | {9} | {}（無中心）|

## 3. init() v2

- **連線**：ZDT 推桿 1-4、JC100 1-4、PQW（左右閥 + 泵 + 刷/水泵）、IMU(WT901)、crane TCP、（手臂 TCP 未裝先跳過）
- **移除**：所有 DM2J init（滑軌/輪組/臂軌）、中心杯、DY500
- 不伸推桿（同 v1）；`last_seal_pulse_[0..3]` 初始化 preset
- IMU baseline、crane_wd / water_inlet watchdog thread 保留
- state → Ready

## 4. attach() / step 流程

### attach()（懸空 → 四杯吸附，**左右同時伸**）
> 機體掛在吊機繩上、無 anchor 需求 → 4 杯同時伸最快最省。（2026-07-08 user 指定同時伸）
1. 開右閥 CH1 + 左閥 CH3
2. `smart_extend_subset_("all", {1,2,3,4})` disable-seal **一點一點補伸**（`pusher_move_many_` 同動）→ 邊伸邊等真空
3. `vacuum_check_("all")` → 補吸失敗杯（依左右分組重伸）
4. 安全閘：pay_out 前再驗真空，有杯沒吸牢就跳過放繩（繩續承重）
5. 吊機放繩到目標張力（`crane_pay_out_to_weight_`）→ state=Attached
   - （水平校正 IMU+繩長差：目前未加，留 TODO）

### do_step_down_(step_cm)（前提：4 杯吸附、水平；step_cm = 使用者輸入全量）
```
前置：確認兩側真空度 OK + 推桿無 stall（vacuum_check_("all") + stall 檢查）→ 失敗 PausedOnError
右半週期（左側 2 杯撐住機體）：
 1. 右閥 OFF → vacuum_wait_release_({1,2}) → pusher_two_stage_retract_({1,2}) 脫牆
 2. crane_cmd_("pay_out_right <step_cm>")  ← 右繩放 step_cm，crane 計米閉環停
 3. 右閥 ON → smart_extend_subset_("right",{1,2}) 一點一點補伸 seal → vacuum_check_("right")
左半週期（右側 2 杯撐住機體）：
 4. 左閥 OFF → vacuum_wait_release_({3,4}) → pusher_two_stage_retract_({3,4}) 脫牆
      ※ user 步驟省略左半縮腳，實體必要（貼牆放繩會刮牆）→ 補上，待實機確認
 5. crane_cmd_("pay_out_left <step_cm>")
 6. 左閥 ON → smart_extend_subset_("left",{3,4}) 一點一點補伸 → vacuum_check_("left")
收尾：
 7. 水平 check：IMU roll≈0 且 |左繩長−右繩長|<tol，否則 PausedOnError（兩側同 step_cm 應自然對稱）
 8. // TODO 清洗 sweep（手臂未裝，先註解）
 9. state/EVT
```
**不變式：** 任一時刻只放開一側，另一側 2 杯撐住；放開前先確認對側吸牢。

### do_step_up_(step_cm)
對稱：第 2/5 步改 `crane_cmd_("retract_right/left <step_cm>")` 把該側往上拉。其餘同 down（一樣右先左後、伸腳補伸）。

### 安全不變式（貫穿 step）
- **任一時刻至少一側 2 杯吸牢**：每次放開一側前，先 `vacuum_check_` 另一側確認撐住；驗證失敗 → PausedOnError，絕不同時 4 杯全放
- 沿用 v1 的 `try_or_pause_` / abort_flag / PausedOnError 機制

## 5. 需新增：crane 端單側量測放/收繩

**Crane_control_PI 新增 4 個指令**（用 crane 自己的 meter_loop + MH300 單側 vfd）：
- `pay_out_left <cm>` / `pay_out_right <cm>` — 該側繩放到該側 SD76 前進 <cm> 停
- `retract_left <cm>` / `retract_right <cm>` — 該側繩收到該側計米後退 <cm> 停

實作要點：
- 讀該側 SD76 起始長度 → 起動該側 vfd（用 `VFD_DIR_PAY_OUT/RETRACT` 巨集方向）→ 輪詢 SD76 delta ≥ cm → `stopDecel()`
- 張力安全同 motion_rope（單側過載 / 停止門檻）
- washrobot 讀 crane cmd_status 的 `length_left`/`length_right` 做水平判定
- ⚠ 跟已完成的 MH300 遷移相容（單側 vfd_left/vfd_right 已可獨立控制）

## 5.5 realign（v2 精簡版，2026-07-08 已實作）

v1 realign 有 feet+body+center、靠 crane 收放繩輔助(Phase 1/5) + feet/body 交替。v2 只剩左右腳 4 顆，**改成 4 顆一起、保持吸住縮回 preset、不動吊機繩**：

- **不解真空**：4 顆全程吸著，推桿 relative-mode 縮回 preset → 機體被吸盤拉近牆面、吸盤不脫離。維持「絕不同時放開 4 顆」安全不變式（見 `project_v2_mechanical_gait.md`）。
- **不動 crane**：無 pay_out / retract（v1 的 Phase 1 crane assist、Phase 5 restore 全刪）。
- **動作**沿用 v1 Phase 2（feet 本來就不解真空那段）：Stage 0 jog 卸預載 → Stage A 慢縮 1/3 破黏著 → Stage B 快縮 2/3，sync 同動。
- 收尾重讀位置更新 `last_seal_pulse_` baseline、`last_feet_max_over_cm_` 歸零。`force=false` 先驗全吸；stall → PausedOnError；末端脫封只 warn。
- `cmd_realign` / `do_feet_realign_` 已重新實作；main.cpp `realign` 指令接回。決策經 AskUserQuestion 確認「不解真空、保持吸住縮」。
- **已接回 step（2026-07-08）**：`do_step_down_` / `do_step_up_` 收尾（兩側都完成、4 顆全吸）呼叫 `do_feet_realign_(apply_threshold=true, caller_holds_lock=true)`。觸發時機由物理定死＝**每步末**（mid-step 一定有一側放開，無法 realign）。**門檻**沿用 `REALIGN_THRESHOLD_CM=1.5` / `REALIGN_THRESHOLD_MEAN_CM=1.0`（settings 可調），超過才動、自動節流。in-step 失敗**非致命**（log + evt_，step 照回 OK；realign 不解真空、stall 也仍錨定安全）。`caller_holds_lock` 解 step 已持 `motion_mtx_` 的同執行緒 deadlock。手動 `cmd_realign` 走 `(false,false)`（無門檻、自己鎖）。

## 5.6 arm sweep 重新接回（arm 裝回後怎麼加）— 2026-07-08 拆除紀錄

**現況**：清潔手臂（damiao M1/M2 + 上滑台 DM2J:14 + 刷/水泵）v2 短期未裝。`cmd_run` / `cmd_run_script` 原本包著 v1「邊走邊刷」的 arm-sweep pipeline，但 v2 `do_step_down_/up_` 把 sweep hook `(void)` 掉 → 整套是 dead code 還會誤連 arm service (127.0.0.1:9527)。已拆成純 step 迴圈。

**保留的參考碼**（`#if 0`，不編譯，日後照抄/參考）：
- `_retired_cmd_run_v1_sweep_`（WASH_ROBOT.cpp）— 完整的 continuous sweep pipeline：`launch_round` / `fut_sweep` / `SweepJoin` RAII / iter1 ba + iter2+ af 的 before/after hook 疊代。
- `_retired_cmd_run_script_v1_sweep_` — script 版同款 pipeline（依 step[i].sweep 決定 launch）。
- `_retired_do_step_down_v1_` / `_retired_do_step_up_v1_` — v1 step body，裡面有 hook 的**原始觸發點**（before/after_feet_rail_hook 在 DM2J feet rail 移動前後 fire；during/after_body_rail_hook 在 body rail）。

**重新接回步驟**：
1. **決定 hook 在 v2 step 的觸發點**（關鍵！不是 uncomment 就好）：v2 沒有 DM2J feet rail，v1 的「after_feet_rail」語意（feet 滑軌移動後）在 v2 沒有對應物。v2 step 結構是「右側 cycle_group_ → 左側 cycle_group_」。建議對應：
   - `before_feet_rail_hook` → step 最前、動 crane 前（機體仍穩定 4 顆吸住，適合 join 前一輪 sweep）
   - `after_feet_rail_hook` → 兩側都完成、機體到新垂直位置後（realign 之前或之後，適合 launch 下一輪 sweep）
   - 依實際 sweep 需求（手臂要在機體穩定吸附、不動時刷）微調。
2. **do_step_down_ / do_step_up_**：拿掉 hook 的 `(void)`，在上面決定的點呼叫 `if (hook) hook();`。
3. **cmd_run / cmd_run_script**：參考 `_retired_..._v1_sweep_` 把 pipeline 接回，但 hook 語意要改成對應 v2 的兩側 cycle_group_（不是 v1 的 body/feet phase）。
4. **arm_attached_ gating**：`do_arm_clean_sweep_continuous_` 已 gate on `arm_attached_`；arm 裝好把它設 on 即可（`arm_attached on`）。
5. **硬體 / 服務**：確認 `cleaning_arm/motor_api` 服務 (TCP:9527)、DM2J:14 上滑台、PQW 刷(CH5)/水泵(CH6) 都在線。
6. **sweep_af direction**：目前 `run` 的 `down_sweep_af`/`up_sweep_af` 當 plain step 跑；接回後改回真正 sweep 版。

**規範權威：** 本節 + changelog 2026-07-08l。

## 6. 複用的 v1 積木（大半可留）

| 子系統 | 複用 helper | 改什麼 |
|---|---|---|
| 真空 | `vacuum_valve_` `pqw_set_relay_verified_` `vacuum_check_` `vacuum_wait_release_` `read_pressure_` | 重分左右 2 區 |
| 推桿伸 | `pusher_extend_with_disable_seal_` `smart_extend_subset_` `fine_tune_extend_per_slave_` | 分組 {1,2}/{3,4} |
| 推桿縮 | `pusher_two_stage_retract_` | slave 1-4 |
| 重試框架 | `cycle_group_` template | pre_cycle 改「單側放繩」取代「滑軌移動」|
| IMU | `imu_take_baseline_` `imu_monitor_loop_` roll 校正 | 配合繩長差 |
| 吊機 | `crane_cmd_` `crane_pay_out_to_weight_` | + 單側量測放繩（§5）|

## 7. 移除 / 停用

- `dm2j_*`（滑軌 / 輪組 / 臂軌）全部：`dm2j_pair_move_abs_` / `dm2j_wheels_move_verified_` / `dm2j_read_pos_robust_` / cmd_wheels / cmd_dm2j_group
- `rail_pos_cm_` / `body_residual_cm_` / 步伐補償邏輯
- 中心杯（slave 9、CH4）、DY500
- arm sweep（`do_arm_clean_sweep_*` / `arm_sweep_fire_nowait_`）→ step 收尾呼叫先註解、class 保留

## 8. 待確認 / 風險

- PQW 左右閥的實際 CH 腳位（v1 是 CH2/3/4，v2 用哪 2 個）
- 推桿 pulse/cm（4 顆機構是否同 v1 腳組 2857 或身組 3000）→ 實測
- step Δ 預設值 + 指令介面（沿用 `step_cm_default`？）
- 水平容差（IMU roll tol、繩長差 tol）→ 實測調
- 中途單側脫離時機體「差動傾斜」的機械容許量（左下右不動的暫態）→ 實機看
- bus 佈局（推桿 1-4 / PQW / JC100 掛哪條 cli）→ 依實際接線

## 9. 建議實作順序

1. crane 端 §5 四個單側量測指令（先，step 依賴它）
2. WASH_ROBOT group config（§2）+ init（§3）
3. attach（§4）
4. do_step_down_ / do_step_up_（§4）
5. 水平校正整合（IMU + 繩長差）
6. GUI 按鈕對應（左右閥、單側繩、step）

## 10. 計米器失效 → IMU 平衡保護 / 降級步進（2026-07-08 規劃，未實作）

> **核心洞見：** 單側下降時機體以另一側為支點傾斜 → `tan(roll) ≈ 單側下降量 / span`
> （span = 左右吸盤柱水平距）。所以 **IMU roll 直接量到左右高低差**，可同時當
> 「保護硬限」與「降級定位」。（roll = imu_.x、pitch = imu_.y。）

### 現有可複用積木
- `imu_monitor_loop_` 背景執行緒（連續讀 roll/pitch + 緊急傾斜偵測，v2 保留）
- crane 單側繩兩種指令：`pay_out/retract_right|left <cm>`（計米閉環）＋ `... on|off`（raw 連續，不靠計米）
- crane 失聯 watchdog（motion_flow §8）：raw on 期間 washrobot↔crane 斷 → crane 自動停

### 三層設計
**L1 偵測 meter 壞：** crane `cmd_side_measured` 已有 meter-death 偵測（讀不到/卡住不動→abort）；
washrobot step 前查 crane status 該側 meter 健康旗標，step 中收到 meter-death→切降級。

**L2 IMU 保護（永遠開，與 meter 好壞無關；meter 壞時升級為主防線）：**
任何 winch 動作期間，washrobot 用自身 IMU 硬限：
- **ROLL 硬上限**：`|roll − baseline| > θ_expected + MARGIN` → 立即送 crane `off` + PausedOnError + EVT（防過衝/繩纏/cup 滑翻覆）
- **PITCH 硬上限**：`|pitch − baseline| > MAX_PITCH_DEG` → 保護停（單側步進不該動 pitch，動了＝異常）
- 加 debounce（連續 N 次超限才觸發），因單側脫牆掛繩會鐘擺晃動

**L3 降級步進（meter dead 時；raw on/off + IMU 閉環取代計米）：**
- 右半：`pay_out_right on` → 輪詢 roll → 到 `θ_target = atan(step/span)` 或安全上限 → `off`
- 左半：`pay_out_left on` → 輪詢 roll → **回 baseline(±ROLL_TOL)** → `off`（＝重新水平）
- 兜底：washrobot 端 max on-time 上限 + crane 失聯 watchdog 雙保險
- ⚠ 鐘擺晃動 → 保護硬限很穩，降級定位較粗（θ_target 有誤差、每步強制 re-level 收斂）

### 預設策略（分情況；user 未定案前的預設，可改）
- **單側 meter 壞（另側好）** → 允許 L3 IMU 降級步進（roll 仍可量傾斜）
- **兩側全壞 / sensing bus 全斷** → 拒絕自動步進（EVT+PausedOnError），只留 L2 保護 + 人工小量點動

### 需新增
- 常數：`MAX_STEP_ROLL_DEG`(θ_expected+margin)、`MAX_PITCH_DEG`、`ROLL_TOL`、`span`（機體幾何，實測）、降級 max on-time、debounce N
- washrobot：meter-health 查詢、step 內 roll/pitch guard（整合進 cycle_group_ pre_cycle/backup 的 crane 動作段）、IMU 閉環 helper（raw on → poll roll → off）、降級分支
- 確認 crane dispatcher 已有 raw `pay_out/retract_right|left on|off`（cmd_manual 分支，應已存在）
- **span 校正**：實機量左右吸盤柱水平距，或用「已知 step、量 roll 反推」

### 待實機定
- θ_expected / MAX_STEP_ROLL_DEG / MAX_PITCH_DEG / ROLL_TOL / debounce N → bench 調
- 鐘擺晃動幅度與收斂圈數 → 實機看
- 降級步進的 step 上限是否要縮（保守）

## 11. 正常運作下的「累積歪」— 左右繩長差校正（2026-07-08 user 回報，方案未定案）

**症狀（Sadie bench）：** 計米**正常**時，多走幾步後機體開始歪。根因：step 是「右 `pay_out_right step` → 左 `pay_out_left step`」，**兩側各用自己 SD76 獨立走 step**，每步左右小誤差（計數/滑動/停止精度/繩伸張）獨立累積 → 左右繩長差 → roll。與 §10 不同（§10 是 meter *壞掉*；這裡 meter *正常*但累積）。

**user 提的兩方案 + 分析：**
- **方案1（左繩目標＝右計米）：** 右走完讀右計米，左**放/收到 左計米=右計米**（非各走 step）。washrobot 讀 crane status 的 `length_left`/`length_right`，`delta=length_right−length_left`，`delta>0→pay_out_left delta` / `<0→retract_left |delta|`，對齊後才重伸吸附。→ 每步重新對齊、誤差**不累積**。簡單、確定性、開迴路。殘差＝左右計米**互相** scale/offset 差（固定、不長大）。右變 master。
- **方案2（IMU 平衡，左繩移動時修）：** 左繩窗口（左吸盤已解、機體繞右側轉）讀 `roll=imu_.x−imu_roll0_`，脈衝左繩到 `|roll|<TOL`（重用 balance-cal 迴圈 + `BAL_CAL_ROLL_TOL_DEG=1.0°`）。量真實傾斜、對計米誤差免疫、兼作 §10 的「meter 壞」保護。缺點：閉迴路、要沉降/調參/防 IMU 雜訊。
- **方案3（結合）：** 方案1 粗調 + IMU 微調殘差。最穩、對計米互差也免疫、程式最多。

**Claude 建議：** 症狀是**累積**（來自左右獨立），方案1 就能確定性殺掉累積、程式最少、無迴路調參 → **先做方案1 驗證**；殘差/計米互差再疊方案2（IMU 微調正好也接上 §10 的 meter-fail 保護）。**實作點：** 兩者都改在左側 `run_side` 的 crane 動作段（左吸盤已解、重伸吸附前的窗口）。

**狀態（2026-07-08 更新）：** Sadie 選**方案1，已實作**。左側 `run_side` 傳 `is_follower=true`；`crane_level_match_cmd_()` helper 讀 crane status `length_left`/`length_right`（cm）算 delta、回 `pay_out_/retract_left <delta>` 讓左對齊右；右側 `is_follower=false` 走固定 step（master）。deadband `LEVEL_MATCH_TOL_CM=0.5`；crane 讀不到→fixed step fallback；**單次移動 / 兩邊計米差上限 `LEVEL_MAX_DELTA_CM=80cm`**（2026-07-08q：原本 `step+15` 太緊、把 re-seal 造成的合法大修正[如 47cm]擋掉退回固定 step → 越積越歪；改成走完整 |delta| re-level，只有 >80cm 才 clamp）。step_down/up 都套。**重吸(backup)後靠下一步的絕對對齊自動補償**——同 v1 DM2J 絕對 rail 目標機制（carryover 已移除，見 WASH_ROBOT.cpp:6483）。**方案2（IMU 微調殘差）留待方案1 bench 驗證後視需要再疊。實作全在 washrobot（crane length 本來就是 cm，無需改 crane）。** **規範權威：** 本節。

**狀態（2026-07-08u 更新）：方案1 → 方案B（共同絕對目標），已實作、取代方案1。** bench 追出兩個問題：(a) 殘差來源是 crane 量測移動過衝 → 07-08s 在 crane `cmd_side_measured` 加減速接近尾段（<1cm）；(b) 方案1 的 master/follower 有 over-travel 破口——某側 reseal 失敗 backup 退回原位 → 下一步 follower 一次補 **2×step**（log 實測 `pay_out_left 40`），單側盪太遠。**方案B**：Sadie 要求「step 開始前先確認兩邊計米再算收放繩」→ **步前讀兩邊、鎖共同絕對目標**：down `target=min(L,R)+step`、up `target=max(L,R)−step`（＝從**落後側**推進一個 step）。兩側都走到此絕對目標 → **落後側正好走 step、領先側讓步（≤step，gap 大甚至微退）** → 任何單側每步都 **不超過 step**、一步收斂、不累積、且不再有盲走 master。實作：`crane_level_match_cmd_` 拆 `read_crane_meters_`+`crane_abs_target_cmd_`；run_side `is_follower`→`tgt_valid,tgt_len`；單側 backstop `min(step+LEVEL_MOVE_MARGIN_CM(5), LEVEL_MAX_DELTA_CM(80))`，超過 clamp（餘量下一步補）。詳 changelog 07-08u。**washrobot 重編**（crane 協定沒變）。**規範權威：** 本節。

## 12. 長繩 → 計米不可信 → IMU 常態接手平衡（2026-07-08 user 提出，策略未定案）

> **user 擔心：** 繩子拉越長，計米器「作用變小」→ 想加 IMU 判斷平衡。**這擔心是對的**：長繩時 (a) 計米累積誤差變大、(b) 繩在張力下彈性伸長變大且隨負載變 → **「兩邊計米相等」≠「真的水平」**。方案B（§11）純靠計米對平，長繩就會殘留真實傾斜。

### 關鍵物理限制（決定所有設計）
- 4 顆吸盤吸牆時，roll 被**吸附點高度**釘死，改繩長只改張力不改 roll → **吸著時 IMU 讀不到繩長不平衡**。
- 只有某側**解真空、角落只靠繩吊著**時 roll 才反映那側繩長。且 step 進行到一半機體本來就故意歪 ~一個 step（`tan(roll)≈單側下降量/span`，見 §10）。
- ⚠ `imu_roll0_`(baseline) 必須＝機體真水平時的 roll，否則 IMU 對平會對到錯的「水平」。→ 相依項：baseline 校正來源要確認。

### 與 §10 / §11 的關係
- §10 的 L2（IMU 硬限保護，永遠開）＝這裡的「監測」層，直接沿用。
- §10 的 L3（IMU 閉環降級步進）＝機制上就是「用 roll 驅動單側繩到水平」，但 §10 設計成**只有 meter 死才啟用**。本節的新增點：**meter 沒死、只是長繩不可信時，也讓 IMU 常態參與 follower 的平衡**（把 L3 的第二側 re-level 從「fallback」升級成「常態」）。
- §11 方案B 仍負責**每步下降距離（絕對量）**；IMU 負責**平衡（左右相對）**。兩者互補：計米管走多遠、IMU 管有沒有歪。

### 三個整合策略（待 user 選）
**策略1（Claude 推薦）：第二側常態用 IMU 對平，不 free-hang**
- Step down：第一側（右）照方案B 用計米走 `step` → 重吸（成為新水平 datum）。第二側（左）解真空後 **驅動左繩直到 `|roll−baseline| < ROLL_TOL`**（＝跟已吸好的右對齊），計米當粗導引 + 安全上限（`crane_abs_target_cmd_` 的移動量當 backstop）。
- 用已吸好的第一側當 datum → 第二側每步真實水平、不靠計米精度、還順便把角落帶到對的高度好吸。維持「絕不放開 4 顆」不變式（只有移動側解）。
- 複用：§10 L3 的「raw `pay_out/retract_left on` → poll roll → `off`」閉環 helper + L2 watchdog。
- 代價：解真空窗口 roll 有雜訊（框架撓曲/CG/鐘擺）→ 要 settle + debounce + watchdog；絕對下降量隨第一側計米微飄（但**保持水平**，正是要的）。

**策略2：每 N 步 free-hang 用 IMU 硬重校 + 重記計米 offset**
- 觸發（N 步 / 計米差 / roll 超標）→ free-hang 在 4 腳、IMU+crane 校平、記「真水平時 R−L 計米 offset」，方案B 之後維持該 offset（target 帶 offset）。
- 最真實，但**要放開全部 4 顆**（危險、要安全 gate）、慢，且 v1 balance-cal 是 body+中心吸盤版、**v2 目前 stub、要先 port 成 4 腳版**（見 [[project_v2_retained_features]]）。

**策略3：IMU 只監測/警告（不自動修）**
- 每步解真空窗口讀 roll，超標 → EVT 警告/暫停（或自動觸發策略2）。最省最安全、兼作 §10 計米壞掉保護，但不自動修傾斜。

### Claude 建議
**策略1 當主力（優雅、不 free-hang、正中長繩擔心、每步真水平）＋策略3 監測當保底**；策略2 留作偶爾硬重置。

### 狀態（2026-07-09a）：策略1 ＋ 監測已實作
Sadie 選策略1。實作 `follower_imu_level_(move_group)`：follower(左) run_side 粗走(方案B measured)到 target 後、重伸吸盤前，反覆讀 `roll=imu_.x−imu_roll0_`，`|roll|<FOLLOWER_ROLL_TOL_DEG(0.5°)` 停；否則 `span·tan(roll)` 估距下**小的 tension-safe measured move**(非 raw on，保留 tension/meter-death 保護)、settle 再讀，≤`FOLLOWER_IMU_MAX_PASSES(3)`。第一側(右)不做(datum)。免疫長繩計米不準＝每 pass 重讀真 roll 收斂。非致命(IMU壞/roll panic 15°/crane fail/未收斂→log+EVT 照走)，兼作監測(EVT `imu_level_ok/no_converge/roll_panic`)。詳 changelog 07-09a。**07-09c：改成執行期可切換兩模式 `set_follower_mode imu|meter`（imu=策略1、meter=純方案B 原本方法），status 顯示 `follower_mode=`，預設 meter。** **待實機：`FOLLOWER_SPAN_CM`(placeholder 100) 要量、`imu_roll0_` 基準時機確認、tol/settle/passes bench 調。** 策略2/3 未實作。

### 需新增（策略1，初估）
- 常數：`FOLLOWER_IMU_LEVEL`（開關）、`ROLL_TOL_DEG`、roll settle ms、debounce N、單側 max on-time、IMU 對平時計米安全上限（沿用方案B backstop）
- washrobot：follower `run_side` 的 crane 動作段改成「先方案B 粗走到 target 附近 → 切 IMU 閉環微調到 roll=baseline」；IMU 閉環 helper（raw on→poll roll→off，複用 §10 L3）；第一側仍純方案B
- 確認 crane raw `pay_out/retract_left|right on|off` 存在（§10 已列，cmd_manual 分支）
- baseline(`imu_roll0_`) 來源與可信度確認

**規範權威：** 本節（＋機制細節見 §10 L2/L3）。
