# Scripted Run（自訂步驟序列）規劃

> **狀態：** 規劃完成、待實作
> **建立：** 2026-06-05（Sadie）
> **位階：** 用戶功能（不影響核心 step 流程）

---

## 功能概述

新增使用者可預先輸入「step cm 序列」，按 Run 自動依序執行。類似 `cmd_run` 但每 step cm 可不同。

## 決策（已確認）

| 項目 | 決策 |
|---|---|
| 方向 | **固定往下**（不支援 up）|
| 每 step sweep on/off | **支援**（2026-06-06）— 預設 sweep；`n` 後綴 = 不 sweep（純下移）|
| GUI | **簡單 CSV input** + textarea，不做 per-step row 編輯 |
| step 上限 | **不限**（user 自負責，但實作上加 1000 step 軟上限避免誤輸入）|
| `*` shorthand | **支援**（例如 `30*5,20*3` = 5×30 + 3×20）|
| 存 / 載入 script | **支援**（命名 script + 持久化）|

### Per-step sweep flag — 設計決策（2026-06-06）

問題：能不能每 step 個別決定有沒有刷洗？

答案：可以。`n` 後綴方案：

| 寫法 | 含義 |
|---|---|
| `30` | 30 cm + sweep（預設）|
| `30n` | 30 cm 純下移、不 sweep |
| `30n*3` | 3 個 30 cm 純下移 |
| `30,30n*2,30` | sweep / 純下 / 純下 / sweep |

**為什麼選 default = sweep**：99% step 都會要 sweep（不然不會用清洗機器人），預設最短 = 共用 case，且 pre-2026-06-06 saved script 含義不變、不需遷移。

**「純下移」的 C++ path**：`do_step_down_(skip_cleaning_sweep=true)` — private method，cmd_run_script 在同 class 直接 call。**不能用 `cmd_step_down(cm)`**，因為它預設會跑 Phase C `do_arm_clean_sweep_()`（WASH_ROBOT.cpp:6326）。

### Pipeline 模式（2026-06-06，跟 cmd_run 一致）

**問題**：MVP 第一版 cmd_run_script per-step 自包（sweep step 呼 cmd_step_down_sweep_after_feet、transit 呼 do_step_down_），每 step 結束前都 join sweep + PARK arm，沒 overlap → 跟 cmd_run pipeline 比慢 ~5-8s/step。

**改成 cmd_run pipeline 完全照抄**：

| 階段 | 動作 |
|---|---|
| Pre-loop | 若 `step[0].sweep` → `launch_round()` 預先 fire（pre-feet sweep） |
| iter 1 `before_feet_rail_hook` | join pre-feet sweep（若有 fire）→ obstacle check |
| iter 1 `after_feet_rail_hook` | 若 `step[0].sweep` → 再 launch 一輪 sweep |
| iter k (k≥2) `after_feet_rail_hook` | 先 join 上一輪 sweep（如果有）→ obstacle check → 若 `step[k-1].sweep` → launch 新一輪 |
| 全 loop 結束 | join 剩下最後一輪 sweep（如果有）→ obstacle check → `ensure_arm_parked_after_rope_("run_script_end")` |

**Sweep 跟 step overlap 時段**（一輪 sweep 覆蓋的時間範圍）：
- Pre-loop sweep：iter 1 Phase A 全程 + Phase B release/retract（feet rail 之前 join）
- iter k after_hook 的 sweep：iter k Phase B 末段 feet re-extend + iter k+1 Phase A + Phase B release/retract + feet rail（next iter after_hook join）

### Transit step 的取捨

`step[k] = transit, step[k+1] = ?` 情境：

| step[k+1] | 行為 |
|---|---|
| sweep | iter k+1 after_hook launch sweep（正常）— iter k+1 Phase A 沒 sweep 在背景（loss：跟 transit→sweep transition 一樣失去 Phase A overlap）|
| transit | iter k after_hook 不 launch（因為 step[k].sweep=false）— iter k+1 全程沒 sweep（符合預期）|

`step[k] = sweep, step[k+1] = transit` 情境：
- iter k after_hook 還是會 launch（because step[k].sweep）
- 該 sweep 跑進 iter k+1 Phase A（背景刷洗）— transit 期間還是有 arm motion
- iter k+1 after_hook join 該 sweep（feet rail done 才 join）

**Trade-off**：transit 不是 100% arm idle，前段可能還有 sweep 餘音。物理上安全（arm 跟 robot 一起下降，wall_mm 不變），但若 transit 是因為「該位置不能讓 arm deploy（例如避障）」，那這 trade-off 不可接受。

**未來可選優化**（若需要嚴格 transit = arm idle）：
- 在 transit step 進 loop 前 sync-wait `fut_sweep`（會阻塞 ~5-8s）
- 或：當 `step[k+1].transit && step[k].sweep` 時，iter k after_hook 跳過 launch（但 step k 的 sweep 就沒做）

---

## TCP 指令集

### 主要指令

```
run_script <csv>
  csv: "30,20,50" or "30*5,20*3" or 混合 "30,30*2,50"
  Reply:
    OK script_started total_steps=N total_cm=X
    ERR invalid_csv reason=...
    ERR step_cm_out_of_range step=K cm=Y
    ERR aborted_at_step=K reason=...
  EVT:
    EVT script_progress step=K/N cm=Y action=running
    EVT script_progress step=K/N cm=Y action=completed
    EVT script_complete total=N status=ok|fail [step=K reason=...]
```

### Script 管理指令

```
save_script <name> <csv>           # 存到 disk
list_scripts                        # 列出已存的 script 名稱
load_script <name>                  # 讀回 CSV (人工檢查用)
delete_script <name>
run_saved <name>                    # load + run 一氣呵成

Reply 範例:
  OK saved name=cleaning_full csv=30*5,20*3
  OK scripts [cleaning_full, quick_test, long_descend]
  OK csv=30*5,20*3
  OK deleted name=cleaning_full
  ERR not_found name=...
  ERR name_invalid (只允許字母、數字、底線、橫線、最長 32 chars)
```

---

## CSV Format spec

### 基本
- 逗號分隔的整數：`30,20,50`
- 每個整數 = 一個 step 的 cm
- 範圍：`STEP_CM_MIN..STEP_CM_MAX`（目前 5..50）

### `*` 重複 shorthand
- `30*5` = 5 個 30cm step
- `30*5,20*3` = 8 step（前 5 個 30，後 3 個 20）
- `30,30*2,50` = 4 step（30, 30, 30, 50）

### `n` 後綴 = 不刷洗
- `30n` = 30 cm 純下移、不刷洗
- `30n*3` = 3 個純下移
- `30n*3,30*5` = 3 跳 + 5 清

### 解析語法
```
csv      ::= token (',' token)*
token    ::= int 'n'? ('*' int)?    # cm [no-sweep flag] [repeat]
int      ::= [0-9]+
```
- `n` 緊跟 cm 數字之後、在 `*` 之前
- C++ / JS parser 同步先 peel `*<count>`，再 peel 尾端的 `n`

### 邊界檢查
- 任一 cm 超過 STEP_CM_MIN/MAX → reject 整個 csv（早 fail）
- count > 1000 → reject（單一 token 重複太多）
- 總 step 數 > 1000 → reject
- 空 csv → reject
- 解析失敗 → reject (token 內容跟在 error message)

---

## Script 儲存

### 檔案位置
`./scripts.json` (washrobot 啟動 CWD 旁邊)，跟 `settings.json` 同層

### 格式
```json
{
  "cleaning_full":  "30*5,20*3",
  "quick_test":     "10,10,10",
  "long_descend":   "50*3"
}
```

### 行為
- 啟動時讀一次到 `std::map<std::string, std::string> saved_scripts_`
- save_script 更新 map + 寫檔
- 寫檔失敗 → 回 ERR，map 不更新
- 名稱檢核：`^[A-Za-z0-9_-]{1,32}$`

---

## WashRobot.cpp 實作骨架

### 新成員（WASH_ROBOT.h）
```cpp
std::map<std::string, std::string> saved_scripts_;
std::mutex                         saved_scripts_mtx_;
static constexpr int               SCRIPT_TOTAL_STEP_MAX = 1000;
static constexpr int               SCRIPT_REPEAT_MAX     = 1000;
```

### CSV parser
```cpp
// Parse "30,20*3,50" → {30, 20, 20, 20, 50}
// Returns empty vec + sets error_msg on fail.
bool parse_script_csv_(const std::string& csv,
                       std::vector<int>& out,
                       std::string& error_msg);
```

### Run 邏輯
```cpp
std::string cmd_run_script(const std::string& csv) {
    std::vector<int> steps;
    std::string err;
    if (!parse_script_csv_(csv, steps, err)) return "ERR " + err + "\n";

    // sanity
    if (steps.empty())            return "ERR empty_script\n";
    if (steps.size() > SCRIPT_TOTAL_STEP_MAX)
        return "ERR too_many_steps max=" + std::to_string(SCRIPT_TOTAL_STEP_MAX) + "\n";
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i] < STEP_CM_MIN || steps[i] > STEP_CM_MAX) {
            return "ERR step_cm_out_of_range step=" + std::to_string(i+1)
                 + " cm=" + std::to_string(steps[i]) + "\n";
        }
    }

    // Reuse cmd_run 的初始化
    arm_sweep_obstacle_pending_.store(false);
    arm_sweep_skip_rest_of_run_.store(false);

    int total_cm = 0;
    for (int cm : steps) total_cm += cm;

    std::cout << "[run_script] " << steps.size() << " steps total " << total_cm << "cm\n";
    evt_("script_progress step=0/" + std::to_string(steps.size())
         + " total_cm=" + std::to_string(total_cm));
    set_state_(State::Running);

    for (size_t i = 0; i < steps.size(); ++i) {
        int cm = steps[i];
        evt_("script_progress step=" + std::to_string(i+1) + "/"
             + std::to_string(steps.size())
             + " cm=" + std::to_string(cm) + " action=running");

        // 固定 down_sweep_af
        std::string r = cmd_step_down_sweep_after_feet(cm);

        if (r.rfind("OK", 0) != 0) {
            evt_("script_complete status=fail step=" + std::to_string(i+1)
                 + " reason=" + r);
            set_state_(State::Error);
            return "ERR aborted_at_step=" + std::to_string(i+1)
                 + " reason=" + r;
        }
        evt_("script_progress step=" + std::to_string(i+1) + "/"
             + std::to_string(steps.size()) + " action=completed");
    }

    evt_("script_complete total=" + std::to_string(steps.size()) + " status=ok");
    set_state_(State::Attached);
    return "OK script_done\n";
}
```

### Script 管理（saved_scripts_ 操作）
```cpp
std::string cmd_save_script(const std::string& name, const std::string& csv);
std::string cmd_list_scripts();
std::string cmd_load_script(const std::string& name);
std::string cmd_delete_script(const std::string& name);
std::string cmd_run_saved(const std::string& name);   // = load + cmd_run_script

// internal
bool load_saved_scripts_from_disk_();
bool save_saved_scripts_to_disk_();
bool validate_script_name_(const std::string& name);
```

---

## main.cpp dispatch

```cpp
if (cmd == "run_script") {
    std::string csv; iss >> csv;
    if (iss.fail()) return "ERR usage:run_script_<csv>\n";
    return robot.cmd_run_script(csv);
}
if (cmd == "save_script") {
    std::string name, csv; iss >> name >> csv;
    if (iss.fail()) return "ERR usage:save_script_<name>_<csv>\n";
    return robot.cmd_save_script(name, csv);
}
if (cmd == "list_scripts")    return robot.cmd_list_scripts();
if (cmd == "load_script")     { ... }
if (cmd == "delete_script")   { ... }
if (cmd == "run_saved")       { ... }
```

---

## Web GUI

### Panel 設計
```
┌─ Scripted Run ─────────────────────────┐
│                                         │
│ Script CSV:                             │
│ ┌─────────────────────────────────────┐│
│ │ 30*5, 20*3                          ││ ← textarea
│ └─────────────────────────────────────┘│
│ Parsed: 5×30 + 3×20 = 8 steps, 210 cm  │ ← 即時 preview
│                                         │
│ [▶ Run]  [💾 Save as...]                │
│                                         │
│ Saved scripts:                          │
│ ┌─────────────────────┐                │
│ │ cleaning_full       │ [Load] [Delete]│
│ │ quick_test          │ [Load] [Delete]│
│ │ long_descend        │ [Load] [Delete]│
│ └─────────────────────┘                │
│ [↻ Refresh list]                        │
│                                         │
│ Progress:                               │
│   Step 3/8 (50cm, descending)           │
│   ████████░░░░░░░░░░░░░  43%            │
└─────────────────────────────────────────┘
```

### JS 行為
- CSV input → 即時呼叫 client-side parser 顯示 preview（不錯時顯示 step 數 + 總 cm）
- `Run` → send `run_script <csv>`
- `Save as` → 跳對話框問 name → send `save_script <name> <csv>`
- `Load` → fetch `load_script <name>` → CSV 填回 textarea
- `Delete` → 確認後 send `delete_script <name>`
- 訂閱 EVT `script_progress` / `script_complete` → 更新進度條 + 高亮

### EVT 訂閱
從現有 ws push channel 接：
- `script_progress step=K/N cm=Y action=running` → step indicator
- `script_complete status=ok|fail` → final state

---

## Edge cases / 失敗處理

| 情境 | 處理 |
|---|---|
| Empty csv | reject early |
| CSV parse error | reject with token 訊息 |
| Step cm 超範圍 | reject 整個 script |
| Total step > 1000 | reject |
| Mid-script fail (step K) | abort，state=Error，evt 報 step K + reason |
| User 按 pause (cmd_pause) | step 內部 try_or_pause_ 會等 → 接續下個 step（reuse 既有機制）|
| User 按 emergency_stop | abort_flag → step 退出 → loop 退出 |
| Save 同名 script | overwrite（不警告，user 自己負責）|
| Load 不存在 script | ERR not_found |

---

## 不做 / 後期再加

- ❌ 方向混合（per-step direction）
- ❌ Per-step row 編輯 GUI
- ❌ 跨方向 pipeline
- ❌ 跑到一半 resume from step K（user 可手動 cmd_skip 跳過、或重新 run_script 從第 K 個開始）
- ❌ 條件式 step（例如「下到某高度為止」）

---

## 工程量

| 項目 | 工時 |
|---|---|
| CSV parser + step list 驗證 | 半天 |
| `cmd_run_script` 主邏輯 | 半天 |
| Script 儲存（json 讀寫）| 半天 |
| Dispatch 加 6 個指令 | 1 小時 |
| Web GUI panel + JS | 1 天 |
| EVT push / 進度條 | 半天 |
| 實機驗證 + edge case | 半天 |
| **合計** | **~3-4 天** |
