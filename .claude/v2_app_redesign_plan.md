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
- **「吸不好重吸」重試/backup 全保留**（2026-07-07 user 強調）：step 每側沿用 v1 `cycle_group_` 重試框架（extend→驗真空→沒吸牢就退一點 backup→重試 N 次→rescue→全失敗才 Paused）。**只把 v1 backup 的 DM2J rail 退位改成吊機繩微移到新牆點**（pre_cycle 主位移＝crane pay_out/retract step；backup＝crane 微移 VACUUM_BACKUP_CM）。`feet_target_capped_` 需保持 LIVE（cycle_group_ 非-body 分支會呼叫）
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

### attach()（懸空 → 四杯吸附，序列化左→右）
1. 左閥 ON → `smart_extend_subset_("left", {3,4})` disable-seal **一點一點補伸** → JC100 驗證
2. 右閥 ON → `smart_extend_subset_("right", {1,2})` **一點一點補伸** → 驗證
3. `vacuum_check_("all")` → 補吸失敗杯
4. **水平檢查**（IMU roll≈0 + |左繩長−右繩長|<tol）→ 單側微調校正
5. 吊機放繩到目標張力（`crane_pay_out_to_weight_`）→ state=Attached

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
