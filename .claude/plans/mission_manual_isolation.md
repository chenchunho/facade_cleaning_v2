# 計畫:Manual 與 Mission 隔離 —— 「Manual 測完的值要明確填進 Mission,不能悄悄帶進去」

> 2026-09-17 per user:「打勾框驅動的排除機制應該跟 MISSION 獨立,在 Manual 做的設定不應該在 MISSION 被影響;
> 要評估有什麼東西是要在 Mission 再設定一次的,因為 Manual 是用來做手動測試,測完的值要填入 MISSION。」
> 狀態:**計畫,未實作,等拍板。**

## 0. 原則

1. **Mission 起跑時所有會影響步態/安全的執行期狀態都要由 Mission 明確設定**(參數或固定值),不繼承 Manual 留下的。
2. **Manual 是實驗場**:改什麼都可以,但影響範圍止於 Manual。
3. **機器設定與校正值**(牆高、計米器 scale、張力門檻…)是第三類:兩邊共用、屬於 Setting,要持久化,**不是任務參數**。
4. 真值一律在本體/吊機;GUI 只顯示與填寫。

## 1. 盤點:Manual / Setting 能改的執行期狀態,對 Mission(cycle_test)的影響

### 1.1 🔴 會悄悄帶進 Mission 的(要處理)

| 狀態 | 誰能改 | 現在 Mission 怎麼受影響 | 處置 |
|---|---|---|---|
| **推桿排除清單** `zdt_skip`(`zdt_disable`) | Manual 打勾框 | `pusher all` 直接跳過被排除的那支;急停收腳也跳過 | **升為 Mission 參數 `skip=`**(四個勾,預設全含)。腳本起跑先 `zdt_enable` 5–8 全部,再依參數 `zdt_disable` |
| **吊機 `level_auto`** | Setting/Manual | 關了之後水平基準不再學,rail=130 那種會撞 10 cm 中止 | 腳本起跑**固定送 `set_level_auto on`**(任務不允許關) |
| **`crane_attached` / `arm_attached`** | Manual | `crane_attached off` 時 `crane_goto`/步伐全部 `OK skipped` ⇒ 腳本以為在動其實沒動 | 腳本起跑**檢查並強制 on**(`arm_attached on` 已有,補 crane);off 就拒跑 |
| **吊機 `motion_hz` 家族**(`motion_hz`/`roll_correct_hz`/`fine_adjust_hz`/`kick_hz`/`freeze_hz`/`roll_finish_hz`) | Setting | `motion_hz` 腳本自己設(30 下行/50 回程)並還原;其餘四個沿用當時值 | `motion_hz` 已處理;其餘歸「機器設定」(§1.3),起跑時**印進 log 頭** |
| **本體 `set_setting` 22 鍵**(`vacuum_seal_deep_kpa`、`pusher_extend_*_pulse`、`vacuum_backup_cm`、`retract_slow_peel_cm`…) | Setting | `pusher all extend` 的尋封/深度/退讓全吃這些 | 歸「機器設定」;起跑印進 log 頭;持久化(§3 步驟 3) |
| **吊機張力/平衡門檻**(`tension_max_kg`、`retract_tension_stop_kg`、`up_stop_total_kg`、`length_diff_max_cm`、`balance_*`) | Setting | 直接決定回程軟停、中止 | 同上 |

### 1.2 🟢 不影響 Mission 的(記錄即可)

| 狀態 | 為什麼不影響 |
|---|---|
| 推桿 RPM(Manual 單次帶 `[rpm]`) | 本體沒有 RPM setter,`pusher_rpm=400` 是常數;腳本送 `pusher all …` 不帶 rpm ⇒ 永遠用常數。🟡 若要讓 Mission 調 RPM → 可選參數 `rpm=`(等拍板) |
| `hold_guard` | 只管 hold 模式(▲▼),腳本走 `pay_out/retract/goto`,另一套門檻 |
| `fine_adjust_level_diff` 手動值 | level_auto on 時每步被學來的值蓋掉 |
| `imu_guard`、`obstacle_detect`、`follower_mode`、`first_step`、`tilt_mode` | cycle_test 不走 C++ `step_*`,這些只在 C++ 步態生效。⚠️ **未來編排搬回 C++ 時要納入 profile** |
| `pump_rotate_min`、`crane_wd_*`、`water_open_max_ms` | 幫浦輪替/看門狗/水閥 deadman,與步態無關 |

### 1.3 ⚪ 機器設定與校正值(共用,不是任務參數)

牆高 `wall_height`、地面歸零、`meter_scale`、`dsz_scale`、`level_deg_per_cm`、`home_ground`、上面 §1.1 後三列。
**問題**:除了牆高/home_ground/motion_hz 由 ExecStartPost 補送,其餘**重啟全部回編譯預設**,Setting 頁改過的值只活到下次重啟。

### 1.4 物理狀態(Manual 測完留下的)

| 狀態 | 腳本前置有沒有處理 |
|---|---|
| 滑台位置 | ✅ 起跑先 `rail <RAIL_START>` |
| 工具槽 | ✅ 每次清潔前自己 `LR_SLOT` |
| 手臂伸著 | ✅ `M1 MOVETO 0` 保險 |
| 風扇 | ✅ `fan(FAN_OFF)` |
| 幫浦 | ✅ 沒開會自己 init |
| 推桿伸著/吸附中 | 🟡 起跑時若已吸附:第一步 `pusher all extend` 會 at-target skip、尋封照做 —— 能跑但不是乾淨起點。**建議前置檢查:吸附中拒跑,要求先收腳** |
| 推桿失能(`zdt_pwr` 有 0) | 🔴 **沒處理**:失能那支 `pusher all` 會失敗 → PAUSE。**起跑檢查 `zdt_pwr=1111`,否則拒跑並提示** |

## 2. 需要拍板的

1. 推桿 RPM 要不要成為 Mission 參數(`rpm=`,預設空=常數 400)?
2. §1.3 機器設定持久化要不要**現在**做(步驟 3,工程量最大)?還是先只做「起跑印進 log 頭」讓每趟可追溯?
3. Mission 頁「從 Manual 帶入」按鈕的範圍:只帶 `zdt_skip` 四個勾?還是把 §1.3 當前值也快照成一份「任務設定檔」?

## 3. 實作步驟(拍板後)

| 步驟 | 內容 | 誰 | 動到 |
|---|---|---|---|
| 1 | **腳本起跑「任務快照」**:`zdt_enable` 全部 → 依 `skip=` 排除;`set_level_auto on`;檢查 `crane_attached`/`arm_attached`/`zdt_pwr=1111`/未吸附,不符拒跑;把 §1.1/§1.3 當前值全部印進 log 頭 | agent-ai-db | `scripts/cycle_test.py`、`server.js`(收 `skip=`) |
| 2 | **Mission 頁新欄位**:推桿含入四個勾(獨立於 Manual,存進 mission_params.json);[可選] `rpm=`;「從 Manual 帶入」鈕(讀 status `zdt_skip` 填進 Mission 的勾) | AI-2 | `public_v3/index.html` |
| 3 | **機器設定持久化**:本體 `set_setting` 22 鍵 + 吊機門檻/balance/hz 家族 → `~/run/robot_settings.json`,開機 ExecStartPost 補送;Setting 頁「存為開機預設」 | agent-ai-db + AI-2 | 本體/吊機 C++(或只在 unit 補送)、systemd、Setting 頁 |
| 4 | 未來 C++ `mission start` 吃同一份 profile(計畫書 §3 已預留 key=value 尾巴) | — | 階段 3 |

步驟 1+2 一天內可完成;步驟 3 另排。
