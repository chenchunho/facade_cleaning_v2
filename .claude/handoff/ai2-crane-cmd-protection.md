# `crane_cmd_` 是否構成本體端的替代保護？（AI-2 → agent-ai-60，第二輪）

- **日期**：2026-09-09
- **前置**：`tmp/ai2-watchdog-handoff.md` §2.3 末尾標為未驗證的那條
- **範圍**：只讀不改。**未改碼、未部署、未 SSH、未啟動任何主程式、未寫 `changelog.md` / `work_log.md`**
- **依據**：working tree 原始碼（發現皆與 HEAD `d969841` 一致）

---

## 0. 一句話結論（Q4）

> ## ❌ **不會 —— 在 09-08 那種情境下不會。**
>
> 精確版是**分情況，而分界不是「隧道斷多久」，是「本體當下有沒有正在送指令」**：
>
> | 情境 | 本體會不會發現吊機不見了 |
> |---|---|
> | **正在送 crane 指令**（step_down / retract / pay_out 進行中） | ✅ 會。`crane_cmd_` 兩次嘗試後回空字串，呼叫端多數會停（但**分四級**，見 §2） |
> | **hold 期間**（GUI 按住 ▲／▼） | ❌ **完全不會**。本體根本不在這條路徑上 |
> | **閒置 / 等待使用者 / 等 PausedOnError** | ❌ **完全不會** |
> | **本體自己在忙、沒空跟吊機講話**（推桿伸出 4s、DM2J 走行 2-3s） | ❌ **完全不會** |
>
> ⇒ **`crane_cmd_` 的自癒是「送指令才會發現」，不是偵測。** 09-08 隧道掉 90% 那次正好落在「hold 期間」——**本體端這一側同樣是全盲的**，而且原因與吊機端那個 loopback 完全無關、是另一個獨立的洞。

🔴 **並且，背景偵測不是「不小心沒做」，是 2026-05-15 明確拍板拿掉的** —— 但那次拍板的前提（「每次 `crane_cmd_` 自癒」＝ 每次都會送指令）**只在動作流程中成立**，hold 與閒置期間不成立。詳見 §3.1。

---

## 1. `crane_cmd_` 本身（Q1）

`app/WASH_ROBOT.cpp:723-811`，宣告在 `app/WASH_ROBOT.h:2124`。

### 1.1 逾時

```
app/WASH_ROBOT.h:2124   std::string crane_cmd_(const std::string& line, int timeout_sec = 60);
```

**預設 60 秒**（註解：`30 → 60 (2026-05-11): give fine_adjust 30s budget on top of main motion`）。實際各呼叫點差異極大：

| 用途 | timeout | 位置 |
|---|---|---|
| `ping` / `tension` / `stop` | **2** | WASH_ROBOT.cpp:2465, 2847, 3027 |
| `status`（讀計米器） | **3** | wash_robot_commands.cpp:574 |
| 運動類（`pay_out_left` 等） | `crane_motion_timeout_sec_(cm)` 動態 | wash_robot_commands.cpp:757, 877, 996… |
| `return_home` 的 `pay_out` | **300** | wash_robot_commands.cpp:4636 |
| `water_inlet on/off` | 預設 **60** | wash_robot_commands.cpp:4257 |

⚠️ 內層 `receiveData(..., 500)` 是 500ms 一次的輪詢，`timeout_sec` 是整段 deadline。**兩次嘗試各一個完整 deadline** ⇒ 最壞等待約 `2 × timeout_sec` + 連線時間。`ping` 那條實際最壞 ~4 秒。

### 1.2 失敗回什麼

| 情況 | 回傳 |
|---|---|
| 兩次嘗試都拿不到非-EVT 回覆 | `""`（空字串，:809） |
| 第二次連線失敗 | `""`（:757） |
| 🔴 **`crane_attached_ == false`** | **`"OK skipped"`（合成的 OK，:728-731）** |
| 正常 | 吊機那一行（`OK ...` / `ERR ...`） |

**自癒機制**（:743-761）：兩次嘗試，第二次強制 `close()` 再重連，處理「zombie socket」（`isConnected()==true` 但實際已死，例如 NAT entry 被淘汰、對端 kernel 重啟沒送 RST）。這一段設計是好的，**它確實會發現連線死掉——前提是有人去送指令**。

### 1.3 呼叫端有沒有檢查回傳值

**32 個 `crane_cmd_(` 出現點**（含註解與宣告；實際呼叫 20 處）。檢查與否見 §2。
關鍵是**回傳空字串會讓 `rfind("OK", 0) != 0` 成立** ⇒ 有檢查的呼叫端都能正確判定為失敗。**這一層是對的。**

🔴 **但 `crane_attached_=false` 是個天窗**：合成 `"OK skipped"` 會讓**每一個**檢查都通過。預設是 `true`（WASH_ROBOT.cpp:59），只有 `cmd_crane_attached` 能關（wash_robot_commands.cpp:4816），屬 bench 用途——但一旦被關著忘了開，**本體端所有吊機相關保護同時且靜默地失效**，`cmd_status` 只會多印一個 `crane_attached=off`。

---

## 2. 逐一分級（Q2）

失敗處置**分四級**，嚴重度由高到低：

### 級別 A：真的會停 —— `try_or_pause_` → `PausedOnError`（7 處）

`wash_robot_commands.cpp:996, 1039, 1395, 1431, 1693, 1719, 2075`
形狀一律是：

```cpp
if (try_or_pause_([this, cs, to]() { return crane_cmd_(cs, to).rfind("OK", 0) != 0; },
                  "<context>"))
    return ...;   // 使用者選 Abort
```

`try_or_pause_`（`app/WASH_ROBOT.h:1878-1926`）：失敗 → `await_user_intervention_()` → **阻塞等人按 Retry / Skip / Abort**。

⚠️ **這是「停下來等人」，不是「自動安全停止」**。機器吊在牆上、繩子停在原處、等操作者回應。以「不要在未知狀態下繼續動」而言是對的；以「鏈路斷了要有人知道」而言，它**只在流程剛好走到那一行時才會發生**。

### 級別 B：中止該流程並回 ERR（3 處）—— 其中 1 處是真正的 fail-safe

| 位置 | 行為 |
|---|---|
| `wash_robot_commands.cpp:4636` | `pay_out descent_cm` 失敗 → `return fail("ERR crane_pay_out_fail\n")`（`return_home`） |
| `WASH_ROBOT.cpp:2750` | `pay_out 1` 非 OK → 回 ERR |
| **`WASH_ROBOT.cpp:2600-2604`** | ⭐ **真正的 fail-safe** |

⭐ **`crane_retract_safe_` 的前置檢查是全檔唯一「吊機通訊死 ⇒ 拒絕動作」的地方**：

```cpp
const double pre = read_rope_weight_max_kg_();
if (pre <= WEIGHT_NO_DATA_KG) {
    std::cout << "[crane_retract_safe] WEIGHT SENSOR OFFLINE — refuse retract for safety\n";
    return "ERR rope_weight_sensor_offline";
}
```

推導鏈：吊機通訊死 → tier-1 `crane_cmd_("tension", 2)` 回 `""` → 落到 tier-2 DY-500 快取 → `weight_comm_ok_[0..1]` 開機初始化為 `false`（WASH_ROBOT.cpp:292-293）**且背景輪詢已於 2026-05-29 移除**（:378-379 註解自陳「becomes dead code but harmless — sensors aren't installed anyway」）→ 回 `WEIGHT_NO_DATA_KG` → **拒絕收繩**。

✅ 語意正確：拉不到張力就不准收繩。**但它只在「有人要收繩」的那一刻才問。**

### 級別 C：只記 log / 發 EVT，往下跑（明確標註 non-fatal，4 處）

| 位置 | 行為 |
|---|---|
| `wash_robot_commands.cpp:757` | `[imu_level] crane move fail — stop trim (non-fatal)` → `return`，流程續行 |
| `wash_robot_commands.cpp:877` | `[step_sync_imu] roll_correct fail — stop trim (non-fatal)` → 同上 |
| `wash_robot_commands.cpp:574` | `read_crane_meters_` 解析失敗 → 呼叫端**靜默退回 `fixed_step()`** |
| `wash_robot_commands.cpp:4257` | `set_water_inlet_` 重試 3 次（500ms 間隔）後 `return true`，並印 `valve state UNKNOWN` |

📌 級別 C 全部是**姿態微調 / 量測精修**類，降級本身合理（「修不動就別修，交給後面」）。問題是**它們同時也是本體最頻繁對吊機講話的路徑** ⇒ 鏈路斷掉時，最可能先撞到的就是這幾條，而它們一律不升級、只留一行 log。

⚠️ `read_crane_meters_` 那條是**靜默降級**：從量測步伐退回固定步伐，操作者在 GUI 上看不到差別。

### 級別 D：回傳值直接丟棄（4 處）—— 🔴 含兩條急停路徑

| 位置 | 內容 |
|---|---|
| 🔴 `app/wash_robot_commands.cpp:3636` | `cmd_emergency_stop()` 裡的 `crane_cmd_("stop", 2);` —— **回傳值未檢查** |
| 🔴 `app/WASH_ROBOT.cpp:3027` | IMU 45° 緊急傾斜的 `crane_cmd_("stop", 2);` —— **回傳值未檢查** |
| `app/WASH_ROBOT.cpp:2847` | `crane_keepalive_loop_` 的 ping（**該執行緒沒有啟動**，見 §3.1） |
| `app/WASH_ROBOT.cpp:2921` | `imu_push_loop_` 的 `set_imu_roll` —— **刻意完全靜默、不重試**（WASH_ROBOT.cpp:2919 註解明載） |

🔴 **急停送不到吊機時，沒有任何徵兆。** 兩條路徑都會在本體端正確生效（`abort_flag = true`、`motion_active_ = false`、`set_state_(State::Error)`），但**吊機那邊的繩子不會停**，而本體不知道、也不告訴任何人。

---

## 3. hold / 等待期間有沒有背景偵測（Q3）

### 3.1 本體端所有背景執行緒的清點（`app/WASH_ROBOT.cpp:342-380`）

| 執行緒 | 週期 | 會不會碰吊機 | 能不能發現吊機不見了 |
|---|---|---|---|
| `imu_monitor_loop_` | 100ms | ❌（讀本機序列埠 IMU） | ❌ |
| `imu_push_loop_` | **250ms** | ✅ 送 `set_imu_roll` | ❌ **失敗完全靜默、不重試、不設任何旗標** |
| `crane_watchdog_loop_` | 500ms | ❌ **完全不送任何東西** | ❌ 見 §3.2 |
| `water_inlet_watchdog_loop_` | 10s | 間接（逾時才送關閥） | ❌ |
| ~~`crane_keepalive_loop_`~~ | — | 🔴 **沒有啟動**（:363-370 被註解掉） | — |

🔴 **`crane_keepalive_thread_` 自 2026-05-15 起就沒有被啟動**，原始碼裡的理由寫得很清楚：

```
// [DISABLED 2026-05-15] crane_keepalive_loop_ thread no longer started.
// Reason: 14t added it to prevent watchdog false-aborts during long
// washrobot-side ops, but 14v further analysis showed the underlying bug
// is zombie TCP socket on crane_cli_ (isConnected=true but dead).
// New design: no continuous ping. Each crane_cmd_ self-heals on fail.
// See crane_watchdog_loop_ header comment for rationale.
```

📌 **這個決定本身沒有錯**，它解決的是「zombie socket 造成 watchdog 誤觸發」。錯的是**它的前提**：

> 「**Each crane_cmd_ self-heals on fail**」成立的條件是「**會有 `crane_cmd_`**」。
> 而 hold 期間、閒置期間、`PausedOnError` 等人期間、本體自己在跑推桿／走行的期間，**一個 `crane_cmd_` 都不會送**。

⇒ 「連續 ping」被拿掉的同時，**唯一的背景偵測也被拿掉了**，而替代方案只覆蓋動作流程。§ai2-watchdog-handoff §2.3 查到的「逾時比較被移除」是同一時期的事，兩者合起來就是**背景偵測整條消失**。

### 3.2 🔴🔴 `crane_watchdog_loop_` 沒有任何背景輸入源

上一輪已確認它只做一件事：把 `crane_alarm_pending_` 排空並升級成 `PausedOnError`。
**這一輪追出它更根本的問題**——那個旗標**只可能在指令在途時被設**：

```
handle_crane_evt_ 的呼叫點，全樹只有一個：
    app/WASH_ROBOT.cpp:783    ← 在 crane_cmd_() 的收包 drain 迴圈裡面
```

而吊機的 `broadcast_evt()`（`Crane_control_PI/main.cpp:1719`）是**推播給所有 client** 的。本體這一側**只有在 `crane_cmd_` 等回覆時才會讀 socket**。

⇒ 🔴 **沒有指令在途時，吊機廣播的 EVT（`tension_alarm` / `tension_total_limit` / `watchdog_timeout`）全部堆在 socket 接收緩衝區裡沒人讀。**

⇒ 所以 `crane_watchdog_loop_` 這條「背景」執行緒：
- 不送任何東西
- 沒有逾時比較
- 唯一的輸入旗標只能由「指令在途」設定

**它是一條每 500ms 醒來、檢查一個只有在別人講話時才會變的旗標的執行緒。**
而開機時它照樣印 `[OK] crane watchdog started`（WASH_ROBOT.cpp:352）。

⚠️ 附帶：`read_rope_weight_estop_`（:2529-2545）drain EVT 行時**只找 `OK` 開頭，EVT 一律丟棄、不 dispatch 給 `handle_crane_evt_`**。所以連 estop 通道撿到的張力警報也是掉的。

### 3.3 hold 期間的完整盲區（09-08 那個情境）

09-08 按住 `▲ 拉繩` 時，實際的資料流是：

```
操作者瀏覽器 ──WiFi──▶ web_backend(:8080/8081，跑在吊機上) ──loopback──▶ Crane_control_PI
本體 .26 ────────────────────────────────────────────────────────────────  不在這條路徑上
```

- **本體的 `motion_active_` 是 false**（動的是吊機，不是本體）⇒ 就算 keepalive 有啟動也不會 ping
- **本體不會送任何 `crane_cmd_`** ⇒ 級別 A/B/C 的保護一個都不會被觸發
- **`imu_push_loop_` 每 250ms 在送**，但失敗完全靜默 ⇒ 唯一真的在跑的那條，剛好是唯一不回報的
- **吊機端的 watchdog 被 loopback 餵飽**（上一輪 §1.3）

⇒ **兩側同時全盲，而且是兩個互相獨立的原因。**

### 3.4 🔴 同一個洞的鏡像：hold 期間操作者掉線，繩子不會停

吊機 watchdog 的 2 秒 idle 門檻，設計目的原始碼寫得很明白（`Crane_control_PI/main.cpp:475-476`）：

> `IDLE / hold-active : 2s   fail-safe; hold = press-and-hold, must stop fast if GUI disconnects`

**這正是它存在的理由，而它做不到。** 追完釋放路徑：

| 釋放途徑 | 是否存在 |
|---|---|
| 瀏覽器事件送 `<hold> off` | ✅ `public_v2/index.html:1993-2008`：`mouseup`/`mouseleave`/`touchend`/`touchcancel`/`pointercancel` + `window.blur` + `visibilitychange` |
| 吊機 `hold_loop` 張力超標 → `hold_all_off()` | ✅ `main.cpp:2258-2274`（**只管張力，沒有時間上限**） |
| 吊機 watchdog 逾時 → `hold_all_off()` | ⚠️ 存在但**被 loopback 餵飽，不會觸發** |
| web_backend 在瀏覽器 ws 斷線時補送 `off` | ❌ **沒有**。`server.js:253-255` 的 `ws.terminate()` 只砍 ws，`grep 'off'` 在 server.js **零命中** |
| 吊機在 TCP client 斷線時清 hold | ❌ **沒有**。`transport/TCP_server.h` 只提供 `setReceiveCallback`，**沒有 disconnect 回呼** |

⇒ **釋放 hold 完全依賴瀏覽器主動送 `off`。** 切分頁、視窗失焦、鎖螢幕都有綁（v2 比 v1 多綁了 `blur` / `visibilitychange`，那是對的），但**瀏覽器當掉、平板沒電、按住的當下 WiFi 掉**這三種——**沒有任何一層會放開繩子**，只有張力超標才會停。

📌 這與 §3.3 是**同一個 loopback 缺陷的兩面**：對外它看不到鏈路斷，對內它看不到操作者走了。

---

## 4. 🔴 順帶發現（不在題目範圍內，但建議單獨排一條）

**兩條急停路徑走的是會被 `crane_mtx_` 卡住的那條通道。**

`crane_cmd_` 第一件事是 `std::lock_guard<std::mutex> lk(crane_mtx_)`（WASH_ROBOT.cpp:733），而運動類指令會**持有這把鎖直到運動結束**（`return_home` 的 `pay_out` timeout 給到 **300 秒**）。

程式碼本身**完全知道這個問題**——`crane_cli_estop_` 就是為此而生，全檔 6 處註解在解釋（:2512-2513、:2616、:2632、:1437、:1567、WASH_ROBOT.h:2215）。但：

| 路徑 | 用哪條通道 |
|---|---|
| `crane_retract_safe_` 的張力監控 → stop | ✅ `crane_cli_estop_`（:2633-2640） |
| GUI 的吊機急停按鈕 | ✅ 不經本體，走 web_backend `crane_intr`（`stop` 在 `CRANE_INTR_FIRST_TOKENS`） |
| 🔴 `cmd_emergency_stop()` | ❌ **`crane_cmd_`（主通道，搶 `crane_mtx_`）** |
| 🔴 IMU 45° 自動緊急停止 | ❌ **`crane_cmd_`（同上）** |

⇒ 推論：吊機運動進行中觸發本體急停時，那道 `stop` 會**排在運動後面**才送出去。IMU 45° 那條尤其值得看——它是**自動的、安全關鍵的，而且傾斜最可能發生的時刻正好就是吊機在動的時候**。

⚠️ **這條我沒有實測**（機器主程式未啟動），是由鎖的持有範圍推導的。**請獨立驗證再下結論**，特別是確認有沒有我沒看到的其他路徑會先讓吊機停下來。

---

## 5. 建議

按「代價 ÷ 拿回多少能見度」排序：

1. **把 `handle_crane_evt_` 接到一個真正的背景讀取器上**（最小、收益最大）
   目前 EVT 只在指令在途時被讀。給 `crane_cli_` 一條背景 drain（或直接讓已經在跑的 `imu_push_loop_` 把它收到的行丟給 `handle_crane_evt_` 而不是 `receiveData` 完就丟）⇒ **吊機的張力警報與 watchdog 事件在 hold / 閒置期間終於送得到本體**。
   📌 `imu_push_loop_` 已經每 250ms 在讀那條 socket 了（WASH_ROBOT.cpp:2941-2943），現在讀完直接丟掉。**這是全案成本最低的一個修法**。
2. **`imu_push_loop_` 加一個「連續失敗次數」計數並曝露到 `cmd_status`**
   不改它「靜默不重試」的設計（那是對的），只是把它已經知道的事情記下來。它是 hold 期間**唯一**還在跑的本體→吊機流量，等於現成的鏈路探針，**只差沒人記分**。這也正好餵給上一輪建議的選項 2。
3. **§3.4 的 hold 釋放**：在 `server.js` 的 ws `close` / `terminate` 時補送 `up off` / `down off`（web_backend 在吊機上、走 loopback，一定送得到），作為瀏覽器沒送成的兜底。**這條與 watchdog 怎麼修無關，可以獨立先做。**
4. **§4 的急停通道**：改用 `crane_cli_estop_`（需先獨立驗證）。
5. `crane_attached_=false` 的合成 `OK skipped` 至少要在 `cmd_status` 以外的地方持續可見（例如每 N 秒印一行警告），避免忘了開。

---

## 6. 我沒做的事

- ❌ 未 SSH、未啟動任何主程式（依指示）
- ❌ §4 是靜態推導，**未實測**
- ❌ 未追 `water_inlet_watchdog_loop_` 在吊機失聯時的完整行為（它要靠 `crane_cmd_` 去關閥，鏈路斷時應該也關不掉，但我沒追完）
- ❌ 未評估「補回背景 ping」會不會讓 2026-05-15 那個 zombie-socket 誤觸發問題重現——**建議 1/2 刻意避開了這一點**（不新增流量，只是把已經在跑的流量記下來）
- ❌ 未寫 `changelog.md` / `work_log.md`

---
---

# 第三輪補充（2026-09-09，AI-2）

回答 agent-ai-60 的收尾兩題。一樣**只讀不改**：未改碼、未部署、未 SSH、未啟動主程式、未寫 `changelog.md` / `work_log.md`。

---

## 7. Q1：`crane_cli_estop_` 到底接到哪裡？有沒有任何一條會在主通道被鎖住時先讓吊機停？

> # ❌ **沒有。而且比「用錯通道」更嚴重 —— 那條旁路通道整條是無法到達的死碼。**

### 7.1 證據鏈（三層，每層都窮舉過）

**第 1 層：`crane_cli_estop_` 只出現在兩個函式裡。**

```
app/WASH_ROBOT.cpp:2521-2534   read_rope_weight_estop_()        ← 讀張力
app/WASH_ROBOT.cpp:2633-2640   crane_retract_safe_() 的 monitor lambda ← 送 stop
（其餘 5 處全部是註解或宣告：:2512、:2914、WASH_ROBOT.h:1440/1567/2111、transport/TCP_client.h:141）
```

**全樹唯一一處用 `crane_cli_estop_` 送出 `stop` 的，是 `crane_retract_safe_` 內部那個張力監控執行緒。**

**第 2 層：`read_rope_weight_estop_` 的唯一呼叫點，也在同一個 monitor 裡。**

```
app/WASH_ROBOT.cpp:2625        double w = read_rope_weight_estop_();
（其餘 3 處為定義 :2516 與註解 :2515、WASH_ROBOT.h:2218 宣告）
```

**第 3 層：🔴 `crane_retract_safe_` 有 0 個呼叫點。**

用三種方法各查一次（依 CLAUDE.md「空輸出要當結論時先換工具複核」）：

| 方法 | `app/wash_robot_commands.cpp` | `app/WASH_ROBOT.cpp` |
|---|---|---|
| `/bin/grep -c "crane_retract_safe_"`（GNU） | **0** | 2（＝定義 :2586 ＋ 註解 :2515） |
| Python 直接數位元組 `b'retract_safe'` | **0** | 10（＝上列 2 ＋ 8 個 `[crane_retract_safe]` log 字串） |
| `git show HEAD:` 同上 | **0** | 同上 |

全樹（含 `Linux_test/`、`harness/`、`tmp/`）除了 `.claude/changelog.md`（歷史紀錄）與 `app/WASH_ROBOT.{cpp,h}` 自己以外，**沒有任何檔案提到它**。

```
app/WASH_ROBOT.h:2239    std::string crane_retract_safe_(int cm, int timeout_sec = 0);   ← 宣告
app/WASH_ROBOT.cpp:2586  std::string WashRobot::crane_retract_safe_(int cm, int timeout_sec) {   ← 定義
（沒有任何呼叫）
```

### 7.2 結論

```
crane_retract_safe_        ← 0 個呼叫點（死碼）
   └─ monitor thread
        ├─ read_rope_weight_estop_   ← 只被這裡呼叫
        └─ crane_cli_estop_ 送 "stop" ← 全樹唯一的旁路 stop
```

⇒ **整棵子樹無法到達。`crane_cli_estop_` 這條 socket 在執行期永遠不會被建立、永遠不會送出任何東西。**

⇒ 回答第 1 題：**沒有。本體端不存在任何一條「主通道被 `crane_mtx_` 鎖住時仍能讓吊機停」的路徑。**
上一輪 §4 我寫「用錯通道、正確的那條就在旁邊」——**那是錯的，要更正**：正確的那條**已經沒有了**。`cmd_emergency_stop` 與 IMU 45° 用主通道不是疏忽，是**因為現在只剩主通道**。

📌 **順帶查到同型的一條**：`crane_retract_to_weight_` 宣告在 `app/WASH_ROBOT.h:2256`，**全樹沒有定義、也沒有呼叫**（Python 位元組計數：`.h` 1 次、`changelog.md` 18 次、其餘 0）。C++ 允許只宣告不定義，只要沒人呼叫就不會連結錯誤 ⇒ **編譯器不會告訴你這件事**。

### 7.3 ⚠️ 但優先度要往下調 —— 張力保護是**真的搬走了**，不是掉了

在把這條排進待辦之前，必須先講清楚**沒有失去什麼**，否則會排錯優先度：

`crane_retract_safe_` 原本提供的「收繩張力到限就停」，**已經在吊機端重新實作，而且位置更好**：

```
Crane_control_PI/main.cpp:718    static std::atomic<double> g_retract_tension_stop_kg {...};
Crane_control_PI/main.cpp:2995   ... std::max(l_kg, r_kg) >= g_retract_tension_stop_kg.load()
Crane_control_PI/main.cpp:3143   return "OK tension_reached\n";
Crane_control_PI/main.cpp:3579   if (is_retract && this_kg >= g_retract_tension_stop_kg.load())
```

📌 這是 `changelog.md:15147-15149` 記載的那次改動（「新增第三個門檻 `g_retract_tension_stop_kg`」）。**搬到吊機端是正確的方向**——保護不再需要一條可能斷掉的網路鏈路。本體端的 monitor 因此變成多餘，然後失去呼叫端。

🔴 **真正的問題是：多餘的是「張力監控」，不是「旁路通道」。** 兩者被綁在同一個函式裡，所以**張力監控退場時，把旁路通道一起帶走了**——而旁路通道還有另一個用途（讓本體在主通道忙碌時仍能講話），那個用途沒有替代品。

📌 **通則（建議與你收的那條並列）：一個函式同時承載兩個關注點時，其中一個過時會讓另一個一起消失，而且沒有任何徵兆。** 這裡消失的那個（旁路通道）連編譯警告都不會有，因為它是「有人定義、沒人呼叫」而不是「有人呼叫、沒人定義」。

### 7.4 那麼吊機在動的時候，到底還有什麼會讓它停？

**全部都在吊機自己身上，完全不需要鏈路**（`motion_rope` 主迴圈 `main.cpp:2919-3010`，`cmd_roll_correct` 同型 `:3259-3296`）：

| 保護 | 位置 | 需要鏈路嗎 |
|---|---|---|
| 吊機自己的 `abort_flag`（`cmd_stop` 設） | :2919 / :3259 | 需要有人送 `stop`（GUI 走 loopback ✅） |
| `MOTION_TIMEOUT_MS = 120000`（120 秒硬上限） | :216, :2923, :3263 | ❌ 不需要 |
| SE3 VFD fault（任一側 fault → 兩側停） | :2955, :3268 | ❌ 不需要 |
| 計米器死亡 `METER_LOST_GRACE_MS = 500` | :350, :2972, :3283 | ❌ 不需要 |
| 張力安全（過高／左右差） | :3007, :3296 | ❌ 不需要 |
| 收繩軟停 `g_retract_tension_stop_kg` | :2995, :3579 | ❌ 不需要 |

✅ **吊機端的自我保護是完整的。** 所以「急停送不到」的實際後果，**只限於吊機自己看不見的那一類危險**：

> 🔴 **本體傾斜 45°（IMU 緊急）—— 吊機沒有 IMU，這件事只有本體知道，而告訴吊機的唯一管道正好是被鎖住的那一條。**

⚠️ 傾斜**可能**會連帶造成左右張力差而觸發 `tension_diff` 保護，但**那不是為此設計的**，我也沒有數據支持它一定會觸發。**不要把它當成替代保護。**

⇒ **優先度建議：中，不是高。** 理由是吊機端自我保護完整、且 120 秒硬上限存在；但「本體知道、吊機不知道」那一類（IMU 45°）確實沒有覆蓋，而那正是最需要跨機通報的一類。

---

## 8. Q2：`water_inlet_watchdog_` 在吊機失聯時的完整行為

### 8.1 前提：閥門在**吊機**上

```
app/wash_robot_commands.cpp:4243   // [2026-06-05] Water inlet moved to crane PQW (192.168.1.34 slave 12 CH4).
```

⇒ 本體要開關水閥，**一定要經過吊機鏈路**。這個 watchdog 從設計上就對鏈路有依賴。

### 8.2 吊機端沒有任何自動關閉

窮舉 `Crane_control_PI/main.cpp` 的 `water_inlet` / `CH_WATER_INLET`（13 處，全部列出）：`:212` 常數、`:4937-4971` 指令處理（`controlRelay` + 三次 verify-retry）、`:5136-5138` init log。

⇒ 🔴 **吊機端沒有逾時、沒有自動關閉、沒有 hold 語意。** 閥門就停在最後一次被設定的狀態。**唯一的強制關閉在本體端**，而它需要鏈路。

### 8.3 鏈路斷掉時的實際行為

`water_inlet_watchdog_loop_`（`app/wash_robot_commands.cpp:4284-4301`）：

```cpp
sleep 10s
if (water_inlet_open_ts_ms_ == 0) continue;              // 未武裝
if (now - ts <= WATER_INLET_OPEN_MAX_MS /* 300s */) continue;
evt_("water_inlet_watchdog_force_close ...");
set_water_inlet_(false);                                  // ← 需要鏈路
```

→ `set_water_inlet_(false)`（`:4253-4276`）：`crane_cmd_("water_inlet off")` 重試 **3 次**、間隔 500ms、**每次用預設 timeout 60 秒**。

**行為**：
1. ✅ **武裝狀態是對的**：失敗時 `ts` 保持不變 → 下一輪 10 秒後會再試，**不會放棄**（`:4299` 註解自陳這是刻意的）。
2. ❌ **但它永遠不會成功**，因為閥門在對面。⇒ **鏈路斷 + 閥門開著 ＝ 水一直流，本體只能每 10 秒印一行。**
3. 🔴 **「每 10 秒」在故障時不成立**：`crane_cmd_` 沒帶 timeout ⇒ 吃預設 **60 秒**，而 `crane_cmd_` 內部是**兩次嘗試各一個完整 deadline** ⇒ 單次呼叫最壞 ~120 秒，× 3 次重試 ＋ 2×500ms ≈ **最壞 361 秒**。這整段是**同步跑在 watchdog 執行緒上**的 ⇒ 一次強制關閉可能讓這條 watchdog 停擺約 6 分鐘。
4. 🔴 它還會搶 `crane_mtx_`（`crane_cmd_` 第一行）⇒ 與 §4 的急停路徑**互相排隊**。

### 8.4 🔴🔴 真正的洞：閥門可能「實際開著，但 watchdog 沒有武裝」

`set_water_inlet_` 的順序（`:4267-4275`）是 **先判定失敗就離開，成功才蓋時間戳**：

```cpp
if (err) {
    std::cerr << "... gave up after 3 attempts — valve state UNKNOWN\n";
    return true;                              // ← 直接離開，ts 沒被寫
}
if (on) water_inlet_open_ts_ms_.store(now_ms_());   // ← 只有成功才武裝
```

⇒ **開閥指令送到了吊機、繼電器實際動作了，但回覆在路上丟了** ⇒ 本體判定失敗 → `ts` 不武裝 → **watchdog 永遠不會看它一眼**。
閥門實際是開的，而本體的紀錄是「沒開成功、狀態未知」。

🔴 **這正好是 09-08 那種鏈路的形狀** —— 不是斷線，是**掉包**：指令過得去、回覆回不來。而 `crane_cmd_` 的失敗判定是「**沒收到回覆**」，它**分不出「沒送到」與「送到了但回覆丟了」**。

⚠️ 而 `water_inlet` 那條指令在吊機端是 `controlRelay` + 三次 verify-retry（`main.cpp:4947-4971`），**成功率相當高** ⇒ 「指令生效但本體以為失敗」不是理論情況，是這條路徑最可能的失敗樣態。

📌 **通則：對「有物理副作用」的遠端指令，收不到回覆不等於沒生效。** 本體這裡把「未確認」當成「未發生」來記帳，而安全側應該相反——**未確認要當成「可能已發生」**，也就是**先武裝 watchdog 再送指令**，成功才是解除警戒的理由。

📌 同一形狀在 `:4275` 的關閥路徑上是**安全的**：關閥成功才 `store(0)` 解除武裝，失敗則保持武裝 ⇒ 方向是對的。**只有開閥那一半反了。**

### 8.5 附帶：`cmd_emergency_stop` 也會走到這裡

`cmd_emergency_stop`（`app/wash_robot_commands.cpp:3631-3646`）在 `water_inlet_open_ts_ms_ != 0` 時呼叫 `set_water_inlet_(false)`，註解寫「ignore failure here (watchdog will retry later)」——這是對的。但依 §8.3-3，那一步本身最壞會**同步阻塞約 361 秒**，而它排在急停處理裡面。⚠️ 未實測。

---

## 9. 第三輪的更正與新增

| # | 內容 | 性質 |
|---|---|---|
| 1 | **更正上一輪 §4**：不是「用錯通道」，是**旁路通道整條已死**。`crane_retract_safe_` 零呼叫點 ⇒ `crane_cli_estop_` 永不建立 | 🔴 更正 |
| 2 | `crane_retract_to_weight_`：宣告存在、**無定義無呼叫**，編譯器不會報 | 🆕 |
| 3 | 張力保護**確實已搬到吊機端**（`g_retract_tension_stop_kg`），方向正確 ⇒ §4 優先度**由高調為中** | ⚠️ 調整 |
| 4 | 吊機端 `motion_rope` 自我保護完整且不需鏈路（含 120s 硬上限）⇒ 急停送不到的實際暴露面**只有 IMU 45°** | 🆕 |
| 5 | 水閥：吊機端**無任何自動關閉**，強制關閉唯一在本體且需鏈路 | 🆕 |
| 6 | 🔴🔴 **開閥失敗時 watchdog 不武裝** ⇒ 掉包情境下可能「閥開著、沒人看管」 | 🆕 高 |
| 7 | 水閥強制關閉最壞同步阻塞 ~361 秒（預設 timeout 60 × 內部 2 次 × 重試 3 次），且會搶 `crane_mtx_` | 🆕 |
| 8 | 通則：**一個函式同時承載兩個關注點時，其中一個過時會把另一個一起帶走，且無徵兆** | 📌 |
| 9 | 通則：**對有物理副作用的遠端指令，收不到回覆 ≠ 沒生效；安全側應「先武裝、後送出」** | 📌 |

### 未做 / 未驗證

- ❌ 全部為讀碼推導，**無任何執行期驗證**（機器主程式未啟動，依指示未動）
- ❌ §8.3-3 / §8.5 的阻塞秒數是由 timeout 常數與重試次數推算，**未實測**
- ❌ 未確認 45° 傾斜是否會連帶觸發吊機端 `tension_diff` 保護（§7.4 已標明不可當替代保護）
- ❌ 未查 `crane_retract_safe_` / `crane_retract_to_weight_` 失去呼叫端的確切 commit（`wash_robot_commands.cpp` 自拆分建立起就是 0，更早的要往 `user_lib/` 時期追）

---
---

# 第四輪：水閥修法提案（2026-09-09，AI-2）

⚠️ **行號以本輪重讀時的 working tree 為準** —— `agent-ai-60` 的 `crane_stop_estop_` 改動已讓
`app/WASH_ROBOT.cpp` 位移，本節所有行號都是重新取的，與前三節可能不一致。
一樣**只讀不改**：以下是提案，**未套用**。

---

## 10. 水閥：先武裝後送

### 10.1 Patch 1 —— 真正的 bug（小、明確、建議先做）

**位置**：`app/wash_robot_commands.cpp:4260-4284`（`set_water_inlet_` 全函式）

**現行**（:4276-4283）：

```cpp
    if (err) {
        std::cerr << "[water_inlet] " << (on ? "OPEN" : "CLOSE")
                  << " gave up after " << RETRY_MAX << " attempts — valve state UNKNOWN\n";
        return true;                          // ← ts 從頭到尾沒被碰過
    }
    // Successful op — update watchdog tracker.
    if (on) {
        water_inlet_open_ts_ms_.store(now_ms_());     // ← 只有成功才武裝
    } else {
        water_inlet_open_ts_ms_.store(0);   // disarmed
    }
```

**提案**：把武裝搬到迴圈**之前**，解除保留在成功之後。

```cpp
bool WashRobot::set_water_inlet_(bool on) {
    constexpr int RETRY_MAX    = 3;
    constexpr int RETRY_GAP_MS = 500;

    // [2026-09-09] Arm BEFORE sending, not after succeeding.
    //
    // A remote command with a physical side effect can take effect while its
    // reply is lost. That is not hypothetical here: the crane's handler does
    // controlRelay() plus a 3x verify-retry loop (main.cpp:4947-4971), so
    // "relay moved but the OK never came back" is the LIKELIER failure than
    // "nothing happened" on a lossy link.
    //
    // Stamping only on a confirmed OK left the valve physically open with the
    // watchdog disarmed -> nothing would ever close it.
    //
    // The two errors are not symmetric:
    //   false arm    -> one redundant close (idempotent) + one spurious EVT
    //   false disarm -> water runs unattended until someone notices
    if (on) water_inlet_open_ts_ms_.store(now_ms_());

    bool err = true;
    for (int attempt = 0; attempt < RETRY_MAX; ++attempt) {
        std::string reply = crane_cmd_(on ? "water_inlet on" : "water_inlet off");
        if (reply.rfind("OK", 0) == 0) { err = false; break; }
        std::cerr << "[water_inlet] " << (on ? "on" : "off")
                  << " attempt " << (attempt + 1) << "/" << RETRY_MAX
                  << " failed: " << reply;
        if (attempt < RETRY_MAX - 1) sleep_ms_(RETRY_GAP_MS);
    }
    if (err) {
        std::cerr << "[water_inlet] " << (on ? "OPEN" : "CLOSE")
                  << " gave up after " << RETRY_MAX
                  << " attempts — valve state UNKNOWN (watchdog "
                  << (water_inlet_open_ts_ms_.load() ? "ARMED" : "not armed") << ")\n";
        return true;                         // ts left as-is
    }
    // Disarm ONLY on a confirmed close. A confirmed open needs no re-stamp —
    // the pre-send stamp is deliberately conservative (it fires earlier, which
    // is the safe direction).
    if (!on) water_inlet_open_ts_ms_.store(0);
    return false;
}
```

**改動實質只有三處**：① 迴圈前加一行 `if (on) store(now)`；② `if (err)` 的 log 補上武裝狀態；③ 結尾的 `if/else` 變成只留 `if (!on) store(0)`。

**行為差異表**（只有一列變壞，且變壞的那列很便宜）：

| 情境 | 現行 | 提案後 |
|---|---|---|
| 開閥成功 | 武裝 | 武裝（時間戳早幾秒，**保守方向**） |
| 🔴 **開閥「失敗」但實際生效**（回覆丟失） | ❌ **不武裝 ⇒ 沒人看管** | ✅ 武裝 |
| 開閥失敗且確實沒生效 | 不武裝 | ⚠️ **誤武裝** → 一次多餘的關閥（見 §10.2） |
| 關閥成功 | 解除 | 解除（**不變**） |
| 關閥失敗 | 保持武裝 | 保持武裝（**不變**） |

📌 **關閥那一半本來就是對的**（成功才解除），Patch 1 沒有動它，只是把開閥那一半的方向轉正。

⚠️ **重試迴圈中途成功時，時間戳是「開始嘗試」而不是「成功」** —— 這是刻意的。最壞情況下這會讓 watchdog 早幾分鐘觸發，而早觸發的代價是一次多餘的關閥。

### 10.2 副作用：誤武裝會發生什麼？

**流程**：ts 被武裝 → 300 秒後 watchdog 判定逾時 → 印 `FORCE CLOSE` + 發 `EVT water_inlet_watchdog_force_close` → 送 `water_inlet off` → 成功則 ts 歸零、結束。

**代價**：
1. **一次多餘的關閥指令。** 吊機端是 `controlRelay(CH4, false)` + verify（`main.cpp:4947-4971`）—— **關一個已經關著的閥是幂等的**，沒有物理副作用。
2. **一則假的 EVT。** 這是唯一真正的成本。
3. 鏈路仍斷時，這兩件事**每個 tick 重複一次**（ts 不會歸零）。

🔴 **值得單獨提出來的一點：假 EVT 會訓練操作者忽略真 EVT。** 建議 Patch 1 順帶把訊息分成兩種措辭，讓「確定開過」與「狀態未確認」在 log 上分得出來：

```cpp
// 開閥成功過 -> "valve open for Ns — FORCE CLOSE"
// 開閥未確認 -> "valve MAY be open (open cmd unconfirmed) for Ns — FORCE CLOSE"
```

需要多一個 `std::atomic<bool> water_inlet_open_confirmed_`，由 `set_water_inlet_(true)` 成功時設 true、關閥成功時設 false。**這是可選的**，不影響安全性，只影響 log 的可信度。

**結論：誤武裝可以接受。** 兩種錯誤的代價差了好幾個數量級（一則假 EVT vs 水無人看管地流），而且**誤武裝會自我修復**（第一次成功的關閥就把它清掉），**漏武裝不會**。

### 10.3 watchdog 的強制關閥要不要改走旁路通道？

**現況**：`water_inlet_watchdog_loop_`（`:4292-4309`）呼叫 `set_water_inlet_(false)` → `crane_cmd_` → **第一行就是 `std::lock_guard<std::mutex> lk(crane_mtx_)`**（`app/WASH_ROBOT.cpp:756`）。運動指令持有這把鎖直到運動結束（`return_home` 的 `pay_out` 預算 300s）。

🔴 **先講一個會誤導人的陷阱**：**光是把 timeout 調短，並不會讓這條路徑變成有界。**
`lock_guard` 沒有逾時，`connectToServer()` 也沒有（`transport/TCP_client.cpp` 是裸的阻塞 `connect()`，這點 `agent-ai-60` 已在 `crane_stop_estop_` 的註解裡記下）。`timeout_sec` 只管收包 deadline。**三個阻塞來源，timeout 只碰得到一個。**

**三個選項：**

| | 做法 | 優點 | 缺點 |
|---|---|---|---|
| **B1** | 改走 `crane_cli_estop_`（沿用 `crane_stop_estop_` 的形狀） | 零新增 socket，通道已在 init 預熱、由 reconnectLoop 維持 | 🔴 **把非緊急流量放上緊急車道**。急停最壞會排在一次水閥指令後面 ＝ 多 1.5s（send 500 + recv 1000） |
| **B2** | 保留主通道，只傳短 timeout | 改動最小 | ❌ **不成立**：`crane_mtx_` 與 `connectToServer` 都不受 timeout 管，仍可阻塞數分鐘 |
| **B3** ⭐ | 給水閥 watchdog **自己的第四條連線**（`crane_cli_water_`），並照 `crane_stop_estop_` 的規則：init 預熱、helper 內**絕不 connect**、沒連上就立刻失敗 | 有界、不搶 `crane_mtx_`、不污染緊急車道 | 多一條 socket；與既有三條形成第四份類似程式碼 |

**建議 B3。** 理由不是「比較乾淨」，是**這個 repo 已經為同一個問題做過兩次同樣的決定**，而且都留了理由：

- `crane_cli_estop_`：`WASH_ROBOT.h:1437` 「Bypasses `crane_mtx_` to avoid deadlock with the …」
- `crane_cli_imu_`：`WASH_ROBOT.h:1567` 「🔴 用第三條連線（照 `crane_cli_estop_` 的模式），**不搶 `crane_mtx_`**」

⇒ **一個每 10 秒醒來、可能要在運動期間動硬體的背景執行緒，正好就是這個模式的適用對象。**

🔴 **反對 B1 的具體理由**：`crane_cli_imu_` 當初就是為了「不要讓 4Hz 的背景推送去搶已經有人用的鎖」而生的。B1 等於把同樣的錯誤搬到 `crane_estop_mtx_` 上 —— 而那把鎖後面現在掛的是急停。**急停通道的價值就在於它沒有競爭者，加一個競爭者就是在花掉那個價值。**

⚠️ **但也要誠實說：B1 的實際代價只有 1.5 秒，而 B3 要多寫一份。** 如果 per user 傾向不再增加 socket，B1 是可接受的妥協 —— 條件是**在註解裡寫明「這條通道現在有第二個使用者」**，否則下一個人會以為它仍然是獨佔的。

📌 **另一個觀察，可能改變優先度**：這個 watchdog 只有**一件工作**。它被卡在一次強制關閥裡面時，它正在做的就是那件工作 —— 「10 秒 tick 變成 12 分鐘 tick」本身**沒有損失任何功能**，只是同一個重試變慢。
⇒ **真正的傷害不在 watchdog 自己身上，而在它持有 `crane_mtx_` 期間會擋住所有其他 `crane_cmd_`**（運動指令在內）。在 `agent-ai-60` 把急停移出主通道之後，這個傷害的範圍已經縮小了，但沒有消失。

### 10.4 `timeout` 沒帶這件事：一起改還是獨立？

**建議：一起改，但列成獨立的一條待辦與獨立的 hunk。**

理由是它**不是同一個缺陷類別**：Patch 1 是「記帳方向反了」（安全語意），這條是「用了不該用的預設值」（參數疏漏）。合成一條會讓 review 的人以為改了一件事。

- `set_water_inlet_` 呼叫 `crane_cmd_` 時**沒有帶第二參數**（`app/wash_robot_commands.cpp:4265`）⇒ 吃 `WASH_ROBOT.h:2124` 的預設 **60 秒**
- 那個 60 秒的來歷寫在同一行：「`30 → 60 (2026-05-11): give fine_adjust 30s budget on top of main motion`」 ⇒ 🔴 **它是為運動指令調的，水閥只是沒有人去帶參數而繼承了它**
- 一個繼電器開關**不該有 60 秒的耐心**

**建議值 5 秒**，有依據不是拍腦袋：吊機端 handler 的 verify 迴圈是 3 次 × `sleep_for(200ms)`（`Crane_control_PI/main.cpp:4955-4956`）＋ 4 次 Modbus 往返 ⇒ 伺服器側最壞約 1~1.5 秒。**5 秒約 3 倍餘裕。**

```cpp
// app/WASH_ROBOT.h — 與 WATER_INLET_OPEN_MAX_MS 放在一起
// A relay toggle should not inherit the 60s default, which was tuned for
// fine_adjust. Crane-side handler worst case is ~1.5s (3x200ms verify loop
// plus Modbus round trips, Crane_control_PI/main.cpp:4947-4971).
static constexpr int WATER_INLET_CMD_TIMEOUT_SEC = 5;

// app/wash_robot_commands.cpp:4265
std::string reply = crane_cmd_(on ? "water_inlet on" : "water_inlet off",
                               WATER_INLET_CMD_TIMEOUT_SEC);
```

⚠️ **影響範圍是所有 8 個 `set_water_inlet_` 呼叫點**（含 sweep 補水流程 `WASH_ROBOT.cpp:2095` / `:2171` 與 `cmd_emergency_stop` 的強制關閥 `wash_robot_commands.cpp:3655`）。正常情況下吊機 1 秒內就回，改了看不出差別；**只在故障時才會顯現，而顯現的方式正是我們要的（早點放棄、早點重試）**。

### 10.5 🔴 時間推算：哪些是程式碼確定的，哪些是推導的

`agent-ai-60` 要求標明。上一輪那個 **361 秒對「掉包」情境是對的，對「主機不可達」情境是低估的**。

**A. 從程式碼可以確定（常數 + 控制流，無假設）**

| 事實 | 出處 |
|---|---|
| `RETRY_MAX = 3`、`RETRY_GAP_MS = 500` | `wash_robot_commands.cpp:4261-4262` |
| `set_water_inlet_` **不帶** timeout ⇒ 吃預設 | `:4265` |
| `crane_cmd_` 預設 `timeout_sec = 60` | `WASH_ROBOT.h:2124` |
| `crane_cmd_` 迴圈 `attempt < 2`，**每次嘗試各有一個完整 deadline** | `WASH_ROBOT.cpp:765` |
| `crane_cmd_` 第一行是**無逾時**的 `lock_guard(crane_mtx_)` | `WASH_ROBOT.cpp:756` |
| `connectToServer()` 是**裸的阻塞 `connect()`，無逾時、無 non-blocking** | `transport/TCP_client.cpp`（`connect()` 直呼，無 `O_NONBLOCK` / 無 `SO_SNDTIMEO`） |
| watchdog **同步**呼叫 `set_water_inlet_`（不是丟背景） | `wash_robot_commands.cpp:4307` |
| 吊機端 handler 的 verify 迴圈 3 × 200ms | `Crane_control_PI/main.cpp:4955-4956` |

**B. 推導 / 依賴環境（不是這份程式碼決定的）**

| 假設 | 為什麼不確定 |
|---|---|
| SYN 逾時 ≈ **127 秒** | 是 Linux `tcp_syn_retries=6` 的**預設值**，不在本 repo 任何地方；兩台 Pi 未實查 |
| 落在哪一種情境 | 取決於呼叫當下 `isConnected()` 是否已經反映真實狀態（TCP 沒收到 RST 時會是 stale） |
| 60 秒 deadline 是否燒好燒滿 | **只有在「send 成功但回覆沒來」時才會**；send 直接失敗會走 `continue`，不消耗 deadline |

**C. 兩種情境的最壞值**

| 情境 | `crane_cmd_` 最壞 | `set_water_inlet_` 最壞 | 說明 |
|---|---|---|---|
| **掉包**（主機可達、回覆丟失）—— 09-08 那種 | 2 × 60s = **120s** | 3 × 120 + 2 × 0.5 = **361s** | ✅ 上一輪那個數字，**對這個情境是對的** |
| **主機不可達**（SYN 黑洞） | 2 × SYN ≈ **254s**（deadline 根本走不到，卡在 connect） | 3 × 254 + 1 ≈ **763s ≈ 12.7 分鐘** | 🔴 **上一輪低估了**。這裡的 60s 完全不參與 |

⚠️ 兩欄都**不含** `crane_mtx_` 的等待時間 —— 那一段**無上界**，取決於當時有沒有運動在跑。
⚠️ 兩欄都是**推導值，未實測**（機器主程式未啟動）。要實測只需在兩台之間插一條 `iptables -j DROP`，不需要動機器。

### 10.6 建議的落地順序

| # | 內容 | 規模 | 風險 | 建議 |
|---|---|---|---|---|
| 1 | **Patch 1：先武裝後送** | 3 處小改 | 低（只有「誤武裝→一次多餘關閥」一種新行為，且幂等） | ⭐ **先做** |
| 2 | **`WATER_INLET_CMD_TIMEOUT_SEC = 5`** | 1 常數 + 1 呼叫點 | 低（正常路徑看不出差別） | ⭐ 同批做，**獨立 hunk 獨立一列待辦** |
| 3 | EVT 措辭分「已確認開過」/「未確認」 | +1 atomic | 極低 | 可選，但能防止假 EVT 稀釋真 EVT |
| 4 | **B3：watchdog 專用第四條連線** | 新 socket + helper + init 預熱 | 中（新增通道，要照 `crane_stop_estop_` 的規則寫：**helper 內絕不 connect**） | 🔴 **需 per user 拍板**，不要順手做 |

📌 **1 與 2 合起來把「閥門開著沒人看管」這個洞補起來；4 只是讓補救更快，不影響會不會補救。** 兩者可以分開評估。

### 10.7 這一輪沒做的事

- ❌ **未套用任何 patch**（依指示只讀不改）；上面的程式碼是提案文字，不是已寫入的內容
- ❌ **未編譯、未部署、未實機驗證**
- ❌ §10.5 的秒數**全部是推導**，SYN 逾時值未在兩台 Pi 上實查
- ❌ 未確認 `WATER_INLET_OPEN_MAX_MS = 300s` 這個門檻本身是否仍合適（`WASH_ROBOT.h:839` 註解說 sweep 補水典型 60-120s，看起來沒問題，但我沒查現行 sweep 流程的實際耗時）
- ❌ 未寫 `changelog.md` / `work_log.md`
