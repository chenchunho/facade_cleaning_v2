# WEB GUI v1 ↔ v2 功能對照表

> 🔴 **2026-09-11 狀態：本文件已降級為參考。** jim 同日拍板「不看 v1，v2 自己定義新的方式」——
> §3 的「待移植組別」**不是待辦**，只是 v1 有什麼的紀錄。v1 仍在 :8080 可用（2026-09-11 晚起停機，
> 因未套危險操作閘門），v2 依作業需求各自演進。

> 2026-09-11 · 作者 AI-2（前端／web 守備）。目的：把 v1（`web_backend/public/`，:8080）的功能
> 逐步整合進 v2（`web_backend/public_v2/index.html`，:8081），最終讓 v2 成為唯一的 GUI。
> **v1 在整合完成前不移除。**
>
> 方法：把 v1 的 `<section>` 逐段列出、抽出每段的 `data-cmd` 與控制項 id，逐一在 v2 全檔比對
> 指令關鍵字，再用中文標籤反查交叉驗證。「v2 已有」都指**有按鈕／輸入框可按**，
> 不算 raw command 打得出來。
>
> 狀態：✅ v2 已有（等效或更好）／⚠️ v2 有但機制不同或只有一半／❌ v2 沒有

## 0. 一句話摘要

**v2 在「看」上遠勝，v1 在「做」上遠勝。** v1 的 17 個 section 裡，v2 完整涵蓋 6 個、部分涵蓋 3 個、
完全沒有 8 個。缺的集中在**流程操作與校正**，不在顯示。

安全項（tier-1）已於 2026-09-10 補齊：本體急停＋RECOVER/RESET 出口、救援收繩、init/shutdown、水位、
跨裝置自動急停。

## 1. 逐段對照（依 v1 的 section）

### 1.1 `auto cycle`（v1 home 頁最上面那段）

| 功能 | v1 指令 | v2 | 備註 |
|---|---|---|---|
| 走步 run | `run <n> <cm> <dir> sync` | ⚠️ | v2 用 **Mission**（`cycle_test.py`）取代，是**不同機制**：Mission 是整趟任務含清洗，run 是純走步 |
| 走到地面（含清洗） | `btn-descend-to-ground` | ❌ | |
| 單步點動 | `step_down_sync <cm>` / `step_up_sync <cm>` | ❌ | |
| 流程控制 | `pause` / `resume` / `continue` / `skip` | ❌ | Mission 的 STOP 是另一件事 |
| 復原 | `recover` / `reset` | ✅ | 2026-09-10 補：`state=error` 時頂欄下方橫幅出兩顆鈕（RECOVER 不鬆真空／RESET 有確認窗） |
| 本體急停 | `emergency_stop` | ✅ | 2026-09-10 補：頂欄紅鈕，無確認窗，四個分頁都在 |
| 開關機 | `init` / `shutdown` | ✅ | 2026-09-10 補：Manual → 洗窗機本體。shutdown 兩道確認窗 |
| 吸附／脫附 | `attach` / `detach` | ❌ | |
| 手臂清掃 | `arm_sweep` | ❌ | 見 1.9 |
| 回頂 | `return_home <cm>` | ❌ | |
| 重新對齊 | `realign` | ❌ | feet-only sealed retract to preset |
| IMU | `imu_zero` / `imu_guard on|off` | ❌ | |
| ZDT 解卡 | `zdt_release_stall` | ✅ | Manual → ZDT |
| 狀態 | `status` | ✅ | v2 常駐輪詢，不需按鈕 |

### 1.2 `SCRIPT RUN`（腳本系統）

| 功能 | v1 指令 | v2 |
|---|---|---|
| 執行腳本 | `run_script <dir> sync <csv>` | ❌ |
| 存／讀／列／刪 | `save_script` / `load_script` / `list_scripts` / `delete_script` | ❌ |
| 執行已存 | `run_saved <name> <dir> sync` | ❌ |

整組缺。這是 v1 最大的一塊獨有功能。

### 1.3 `manual — vacuum`

| 功能 | v1 指令 | v2 | 備註 |
|---|---|---|---|
| 真空幫浦 | `pump on|off`（輪替中那顆） | ⚠️ | v2 只有 `pump a|b on|off`（單顆）。**沒有「作用在輪替中那顆」的按鈕** |
| 手動輪替 | `pump swap` | ✅ | Dashboard 真空幫浦卡〔立即輪替〕，含 40s 狀態機 |
| 輪替門檻 | `set_pump_rotate_min` | ✅ | 兩邊都在 PQW 繼電器旁（per jim 2026-09-10） |
| 輪替狀態 | `pump status` | ✅ | v2 是完整卡片，v1 是一列精簡版 |
| 真空閥 | `vacuum feet on|off` | ✅ | v2 用 `relay 1 on|off` 打同一顆，等效 |

### 1.4 `manual — cleaning`

| 功能 | v1 | v2 | 備註 |
|---|---|---|---|
| 進水球閥 | `water_inlet on|off` + 60s 自動關 + 看門狗警示 | ✅ | v2 直送吊機 ZS-DIO CH4；deadman 在吊機端 |
| 水位 | `water_level` | ✅ | 2026-09-10 補，Dashboard |
| 噴水加壓馬達 | `water_pump on|off` | ✅ | PQW CH4，送語意指令 |
| 滾筒刷 | `brush on|off` | ✅ | PQW CH5 |

### 1.5 `PWM 控制 (QX-DO24)` — ✅ v2 完整（set / save / restart / status）

### 1.6 `manual — vacuum readings` / `📐 IMU 姿態` — ✅ v2 更好（吸盤圓環、roll 圖像化、3D 姿態）

### 1.7 `crane`

| 功能 | v1 指令 | v2 | 備註 |
|---|---|---|---|
| 掛載開關 | `crane_attached on|off` | ❌ | |
| 狀態 | `status` / `home_status` | ⚠️ | `status` 常駐；**`home_status` 沒有** |
| 停止 | `stop` | ✅ | Manual + Mission + 覆蓋層三處 |
| 計米歸零 | `zero_meters ground|top` / `zero_meter left|right|middle` | ❌ | |
| 對齊 | `align_lengths` | ❌ | |
| 張力歸零 | `zero_tension all` | ❌ | |
| 張力 scale | `set_dsz_scale <side> <v>` | ❌ | |
| 距離收放 | `pay_out <cm>` / `retract <cm>` | ❌ | v2 只有按住式 |
| 左右獨立收放 | `pay_out_left/right`、`retract_left/right` | ✅ | v2 `up_left/down_left/…` 走 `cmd_hold`，**更安全**（有張力保護） |
| 張力門檻 ×4 | `set_up_stop_total_kg` 等 | ✅ | Setting 頁 |
| 頻率 hold_hz / motion_hz | `set_hold_hz` / `set_motion_hz` | ✅ | |
| 頻率 middle / kick / fine_adjust / roll_correct | `set_*_hz` | ❌ | 四個細項頻率沒有入口 |

### 1.8 `manual — pusher` — ✅ v2 更好（單支指定長度 `extend_raw`、逐支歸零、使能）

### 1.9 `🦾 清潔手臂` / `🦾🚿 arm 整合操作`

| 功能 | v1 | v2 | 備註 |
|---|---|---|---|
| INIT / PARK / STATUS / DEPLOY | 直接對 arm :9527 | ✅ | v2 另有 `DEPLOY_F` 力控 |
| `arm_init` | 經本體 | ✅ | |
| 手臂掛載 | `arm_attached on|off` | ❌ | |
| 整合清掃 | `arm_clean_sweep <mm> <rounds>` / `arm_clean_sweep_dry` / `arm_sweep` | ❌ | washrobot 編排的整段流程 |

### 1.10 `⚖️ 重心校正` — ❌ 整組缺（`balance_calibrate_start|record|abort`、`confirm_balance yes|no`）

### 1.11 `🆘 緊急收繩` — ✅ 2026-09-10 補（走 `cmd_manual`，刻意無保護，與 hold 不合併）

### 1.12 `raw command` / `log` — ✅ v2 有（通訊紀錄更完整）

### 1.13 `⚙️ Wall-tune Settings`

| 功能 | v1 | v2 | 備註 |
|---|---|---|---|
| 讀回 | `get_settings` | ⚠️ | v2 有「參數對照表」「重啟後還剩什麼」兩張**唯讀**卡 |
| 逐項套用 | `set_setting <key> <val>` | ❌ | |
| 存檔 | `save_settings` | ❌ | 寫 settings.json 重啟保留 |

## 2. v2 獨有（v1 沒有的）

- Dashboard：機器在哪裡（大樓＋機器人圖）、吸盤真空圓環、姿態 3D、張力條含門檻標點
- Mission：`cycle_test.py` 執行與串流、起跑前六項檢查與閘門、STOP 四步序（吊機 stop → arm_park → SIGINT → SIGKILL）
- 平衡 AUTO/MANUAL 下拉、`fine_adjust_level_diff` 設定入口
- ZDT 單支指定長度、逐支歸零、使能／失能
- 滑台 rail 0/50/130 + 校正歸零
- 吊機 ZS-DIO 四路繼電器面板
- 真空幫浦 A/B 輪替完整卡（含 40s 切換狀態機、真空異常紅字橫幅）
- 本體 Error 復原橫幅（RECOVER／RESET）
- 版號（`CONSOLE_VER`，頂欄與啟動紀錄同源）
- 亮／暗、電腦／平板切換

## 3. 待移植的組別（等 jim 排優先序）

| # | 組別 | 內容 | 性質 |
|---|---|---|---|
| A | **腳本系統** | run_script / save / load / list / delete / run_saved | 操作 |
| B | **流程控制** | pause / resume / continue / skip；單步點動；走到地面 | 操作 |
| C | **手臂整合清掃** | arm_clean_sweep / _dry / arm_sweep；arm_attached | 操作 |
| D | **重心校正** | balance_calibrate start/record/abort；confirm_balance | 校正 |
| E | **歸零** | zero_meters / zero_meter / zero_tension / align_lengths / imu_zero | 校正 |
| F | **掛載開關** | attach / detach / crane_attached / imu_guard | 設定 |
| G | **吊機細項** | pay_out/retract 距離式；home_status；return_home；realign；四個細項 Hz；dsz scale | 冷門 |
| H | **Wall-tune 設定** | set_setting / save_settings（v2 只有唯讀） | 冷門 |
| I | **幫浦「輪替中那顆」開關** | `pump on|off` | 小 |

## 4. 移植原則（照 v2 現有的規矩）

1. **送指令一律走 `fire()`**（有 log、有 700ms 補輪詢），除非指令本身超過 30s（那就直接 `send()` 給長逾時，像 `pump swap` 那樣）。
2. **確認窗分級**：來不及想的（急停）不加；要想清楚的（RESET、zdt_zero）加一道；會讓機器掉下來的（SHUTDOWN、detach 貼牆時）加兩道。
3. **顯示照回讀畫，不照「我剛送了什麼」畫**。
4. **真空源判定只走 `vacSource()`**，不要再各自 grep `ch2=1`。
5. **本體指令的 `set_*` 用 `data-tgt="wr"`**，沒標的送吊機。
6. **每次部署更新 `CONSOLE_VER`**，用暫存名 + `mv` 原子部署，本機／Pi／HTTP 三方 md5 對過才算。
7. **新功能先寫 harness**（從 index.html 抽真實函式本體跑），綠燈要先證明過會紅（突變測試）。
