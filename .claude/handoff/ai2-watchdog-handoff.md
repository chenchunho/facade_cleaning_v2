# 吊機 watchdog 心跳診斷交付（AI-2 → agent-ai-60）

- **日期**：2026-09-09
- **範圍**：只診斷，**未改任何程式碼、未部署、未送任何會讓機器動作的指令、未寫 `changelog.md` / `work_log.md`**
- **依據**：樹內原始碼（working tree）+ git history。**未 SSH 到任何機器**（`.25` 上 `crane_control_PI` 未啟動、`.26` 未開機，無執行期可觀察，唯讀查詢也拿不到有意義的東西）
- **HEAD**：`d969841`；`app/WASH_ROBOT.{cpp,h}` working tree 有未 commit 改動，下述發現**在 HEAD 與 working tree 皆成立**（已用 `git show HEAD:` 逐項複核）

---

## 0. 結論摘要

| # | 結論 | 判定 |
|---|---|---|
| A | 假說「心跳不分連線來源」**成立**，且**比敘述更強**：心跳是**單一全域變數**，由**任何一條連線的任何一個 byte** 餵飽 | ✅ 確認 |
| B | 餵它的那條連線**根本不過網路** —— GUI 走的是吊機**自己機器上的 localhost**。⇒ 這個 watchdog **在結構上不可能偵測到任何網路故障** | ✅ 確認 |
| C | **roll 不在這個 watchdog 的監看範圍內，從來就不是**。roll 有自己的獨立時效檢查（`IMU_ROLL_STALE_MS`），而且 09-08 當天**它正常運作**（所以平衡退回 `src=meter`） | ⚠️ 修正敘述 |
| D | 🔴🔴 **新發現：本體端「監看吊機連線」的 watchdog 是死碼** —— `crane_last_ok_ms_` 被寫 3 處、**沒有任何一處讀它**；`WATCHDOG_TIMEOUT_MS`（本體端）只出現在自己的定義與兩行註解裡。**檢查曾經存在，在 2026-04-15 ~ 06-22 之間被移除，餵它的機制卻全部留著** | 🆕 未登記 |

⇒ **綜合**：吊機↔本體這條控制鏈路，**目前兩個方向都沒有可運作的連線存活偵測**。吊機端那個看起來在跑但看的是 localhost；本體端那個連比較都沒做。

---

## 1. Watchdog 的實際實作（吊機端）

### 1.1 心跳誰餵、在哪裡餵

```
Crane_control_PI/main.cpp:603   static std::atomic<uint64_t> last_ping_ms(0);   ← 單一全域
Crane_control_PI/main.cpp:1727  static void touch_heartbeat() { last_ping_ms.store(now_ms()); ... }
Crane_control_PI/main.cpp:4983  static void on_receive(socket_t sock, const char* data, int len) {
Crane_control_PI/main.cpp:4984      touch_heartbeat();          ← 全檔唯一呼叫點
```

🔴 **`touch_heartbeat()` 全檔只被呼叫一次**，就在 TCP 接收回呼的第一行——**在解析成指令之前**。
語意是原始碼註解自己寫的（main.cpp:1722）：

> `Called on any inbound TCP data — treats any byte as "peer alive".`

⇒ 它量的是**「還有沒有人在跟我講話」**，不是「控制鏈路活著」，更不是「感測資料還在來」。
而且 `last_ping_ms` **只有一個**，不分 socket、不分來源、不分用途。

### 1.2 誰接在 :5002 上

`CMD_PORT = 5002`（main.cpp:187），**multi-client**（main.cpp:56 註解明載）。實際同時連上的有 **5 條**：

| # | 連線 | 來源 | 位置 |
|---|---|---|---|
| 1 | `crane_cli_`（主指令） | 本體 | `app/WASH_ROBOT.cpp:720` |
| 2 | `crane_cli_estop_`（急停旁路） | 本體 | `app/WASH_ROBOT.cpp:2523` |
| 3 | `crane_cli_imu_`（roll 推送，第三條連線） | 本體 | `app/WASH_ROBOT.cpp:2935` |
| 4 | `crane`（主橋接） | web_backend | `web_backend/server.js:178` |
| 5 | `crane_intr`（stop/status 旁路） | web_backend | `web_backend/server.js:192` |

**這 5 條全部經過同一個 `on_receive()`，全部餵同一個 `last_ping_ms`。**

### 1.3 🔴 決定性的一點：web_backend 跑在吊機自己身上，走 loopback

- `CLAUDE.md:356` — 「**web_backend 刻意放在吊機側**：本體在半空中掛掉時，GUI 仍能透過吊機手動收繩救援」（設計是對的，後果是意外的）
- `.claude/runbook.md:126` 實際啟動命令：
  ```
  ssh user@192.168.5.25 'cd ~/bringup/web && WROBOT_IP=192.168.5.26 CRANE_IP=127.0.0.1 HTTP_PORT=8080 ... node server.js ...'
  ```
  ⇒ **`CRANE_IP=127.0.0.1`**。#4 #5 兩條連線**不經過隧道、不經過 WiFi、不經過任何網路介面**。

而 web_backend 在這兩條 loopback 連線上**無條件**送心跳：

```
web_backend/server.js:67   const BRIDGE_PING_MS = 10000;
web_backend/server.js:171  setInterval(() => { send('ping'); }, BRIDGE_PING_MS);   ← 每個 bridge 都有
```

再加上前端的 status 輪詢（`status` 在 `CRANE_INTR_FIRST_TOKENS` 裡 → 走 #5）：

```
web_backend/public_v2/index.html:1735  var POLL_FAST_MS_IDLE   = 1000;
web_backend/public_v2/index.html:1736  var POLL_FAST_MS_MOTION = 250;
```

⇒ **只要瀏覽器開著，`last_ping_ms` 每 ≤1 秒就被 localhost 刷新一次**，`WATCHDOG_TIMEOUT_MS_IDLE = 2000` 永遠到不了。
**即使沒有瀏覽器**，backend 自己的 10 秒 ping 仍在餵（10s > 2s，所以純 backend 情況下 idle watchdog 還是會響一次，但那只是 `EVT watchdog_timeout state=idle`，不做任何事）。

📌 **這比「GUI 輪詢蓋住了它」更嚴重**：不是兩條網路連線互相掩護，而是**一條完全不上網的連線在替一個網路監測器背書**。這個 watchdog **在結構上就不可能報告網路故障**。

### 1.4 逾時常數的語意與現值

| 常數 | 值 | 位置 | 語意 |
|---|---|---|---|
| `WATCHDOG_TIMEOUT_MS_IDLE` | **2000** | main.cpp:487 | idle **與 hold 中**的門檻。hold＝按住不放，GUI 一斷就要快停 |
| `WATCHDOG_TIMEOUT_MS_MOTION_MIN` | **10000** | main.cpp:488 | 自動 `motion_rope` 的**下限**（不是值） |
| `g_motion_dynamic_timeout_ms` | 執行期 | main.cpp:489 | `motion_rope`/`cmd_roll_correct` 進入時算 `cm/5cm·s⁻¹ + 10s buffer`（`MotionTimeoutScope`, main.cpp:1780） |
| `HEARTBEAT_CHECK_MS` | **250** | main.cpp:490 | watchdog 執行緒的輪詢週期 |

選哪一個（main.cpp:1746）：

```cpp
const bool in_auto_motion = motion_active.load() && !any_hold_active();
```

🔴 **`hold` 走的是 2000ms 那條**（`any_hold_active()` 為真 → `in_auto_motion` 為假）。
09-08 那次實驗是按住 `▲ 拉繩` 17.45 秒＝hold，所以當時的門檻確實是 **2000ms**，敘述無誤。

### 1.5 觸發後做什麼（main.cpp:1756-1766）

```cpp
if (watchdog_fired.exchange(true)) continue;      // 只響一次
if (motion_active.load()) {
    abort_flag = true; hold_all_off(); allMotionOff();
    broadcast_evt("EVT watchdog_timeout state=aborted\n");
} else {
    broadcast_evt("EVT watchdog_timeout state=idle\n");   // ← 只發事件，不動硬體
}
```

### 1.6 ⚠️ 附帶發現：全部斷線 ＝ watchdog 完全不檢查

```cpp
main.cpp:1738   if (cmd_server.getConnectedClients().empty()) continue;
```

**沒有任何 client 時直接跳過**，連 `state=idle` 都不發。實務上因為 web_backend 在同一台、會 1 秒重連（`RECONNECT_MS = 1000`），這條幾乎踩不到——但它**又是一層「本機行程活著就當作沒事」**。
（`TCP_server` 沒有 disconnect 回呼可用：`transport/TCP_server.h` 只提供 `setReceiveCallback`。所以「連線掉了」這個事件在吊機端**根本沒有入口**。）

---

## 2. 假說驗證

### 2.1 ✅ 成立：「心跳不分連線來源」

**逐字成立，且是最強的形式**——不是「分不清楚」，而是**設計上就只有一個全域計時器，任何 byte 都算數**（§1.1）。

### 2.2 ⚠️ 需要修正：roll 從來就不在它的監看範圍內

原敘述隱含「roll 斷了 7.2 秒 → watchdog 應該要響 → 被 GUI 蓋住了」。**前半不成立**：

roll 有**自己的、獨立的**時效機制，而且**它當天是正常運作的**：

```
main.cpp:884   static constexpr int IMU_ROLL_STALE_MS = 750;    // 3 × BALANCE_TICK_MS
main.cpp:896   static bool imu_roll_fresh(int64_t* out_age_ms) {
main.cpp:897       const int64_t st = g_imu_roll_stamp_ms.load();
main.cpp:898       if (st == 0) { ...; return false; }            // 從未收到
main.cpp:900       const int64_t age = steady_now_ms() - st;
main.cpp:901       return age <= IMU_ROLL_STALE_MS;               // 過期
main.cpp:1581  const bool use_imu = want_imu && imu_roll_fresh(&imu_age);   // ← 每個 tick 重新判定
```

⇒ roll 一過期，`apply_balance_trim` **每一個 tick** 自動退回計米器路徑並每 2 秒印一行 stderr 警告（main.cpp:1610-1619）。
**09-08「平衡全程退回 `src=meter`」正是這個機制在正確動作**，不是失效的徵兆。

📌 所以正確的說法是：
> **watchdog 沒響不是因為 roll 的問題被蓋住了，而是因為 watchdog 從來不看 roll。**
> 它看的是「還有沒有人跟我講話」，而那個問題的答案（因為 localhost）**永遠是「有」**。

真正的缺陷因此**不是**「roll 沒被監看」（roll 被監看得很好），而是：

> 🔴 **`Crane_control_PI` 沒有任何一個機制在監看「本體那條控制鏈路還活著嗎」。**
> 唯一長得像的那個（watchdog）量的是本機行程，不是鏈路。

### 2.3 🆕🔴🔴 反方向也沒有：本體端的吊機 watchdog 是死碼

檢查另一個方向（本體監看吊機）時發現**更嚴重的一件事**。

**事實 1 —— 計時器被寫 3 處，0 處讀取：**

```
app/WASH_ROBOT.h:1491              std::atomic<int64_t> crane_last_ok_ms_;
app/WASH_ROBOT.cpp:45              , crane_last_ok_ms_(0)            ← 初始化
app/WASH_ROBOT.cpp:786             crane_last_ok_ms_ = now_ms_();    ← 寫（收到 OK 回覆）
app/WASH_ROBOT.cpp:2418            crane_last_ok_ms_ = now_ms_();    ← 寫（motion_progress EVT）
app/wash_robot_commands.cpp:4822   crane_last_ok_ms_ = now_ms_();    ← 寫
（沒有任何一行讀它）
```

**事實 2 —— 門檻常數只出現在自己的定義與註解裡：**

```
app/WASH_ROBOT.h:830     static constexpr int WATCHDOG_TIMEOUT_MS = 2000;
app/WASH_ROBOT.cpp:2414  // 註解：Refresh watchdog timestamp so 2s WATCHDOG_TIMEOUT_MS doesn't fire...
app/WASH_ROBOT.cpp:2823  // 註解：...silently lets crane_last_ok_ms_ age past WATCHDOG_TIMEOUT_MS (2s)
（沒有任何運算式用到它）
```

**事實 3 —— `crane_watchdog_loop_()`（WASH_ROBOT.cpp:2784-2818）只做一件事**：把 `crane_alarm_pending_`（張力警報）排空並升級成 `PausedOnError`。**沒有任何逾時比較。**

**事實 4 —— 它曾經存在。** `9f33c6a`（2026-04-15）的 `user_lib/WASH_ROBOT.cpp:236-249`：

```cpp
if (!crane_wd_running_.load()) break;

crane_cmd_("ping", 2);                                    // ← 無條件 500ms 一次

int64_t elapsed = now_ms_() - crane_last_ok_ms_.load();   // ← 比較
if (elapsed > WATCHDOG_TIMEOUT_MS) {
    if (motion_active_.load()) {
        abort_flag = true;
        evt_("crane_watchdog timeout");
    } else {
        evt_("crane_watchdog timeout idle");
    }
}
```

到 `4d1409c`（2026-06-22，「累積一個月 bench tuning」的大 commit）就沒有了。中間的移除點因為那個 commit 把一個月壓成一筆而無法再細分。

**事實 5 —— 餵它的機制全部留著、而且還在長大。** ping 從「無條件 500ms」被搬進 `crane_keepalive_loop_()`（WASH_ROBOT.cpp:2833，**1Hz、且只在 `motion_active_` 時送**），`motion_progress` 那條刷新（:2418）是 2026-05 為了「避免 false-abort」特地加的，兩處註解到今天都還在解釋「為什麼要避免 watchdog 誤觸發」。

⇒ 🔴 **一個沒有消費者的計時器，養著一整套餵食機構，還附三段解釋為什麼要餵它。**
📌 **通則（建議收進踩坑索引）：判斷一個保護機制在不在，要找「誰讀這個值」，不是找「誰寫這個值」。寫入點、常數、餵食執行緒、解釋註解可以全部健在，而比較那一行不存在——`grep` 到符號會讓人以為它活著。**

⚠️ **影響評估**：這件事讓「隧道斷掉時本體會不會自己停下來」**目前沒有答案**。我**沒有**追完 `crane_cmd_` 各呼叫點的逾時/錯誤處理是否構成替代保護（`try_or_pause_` 等路徑可能在**指令失敗時**擋下來）——但那是「送指令才會發現」，**不是**背景偵測；hold 或等待中不送指令的期間沒有覆蓋。**這條建議獨立驗證後再下結論，不要直接採信我的推測。**

---

## 3. 修法選項

⚠️ 三個選項都**不解決** 09-08 的隧道干擾本身；它們解決的是「鏈路死掉時系統知不知道」。
📌 **與 `IMU_ROLL_STALE_MS` 那條的排序關係**：本條與它相反，**不需要**排在干擾後面——本條不涉及調鬆任何門檻，不會蓋住任何東西。

### 選項 1（最小、最像既有設計）：心跳依來源分流

把 `last_ping_ms` 由 1 個拆成「**每條 socket 一個**」，watchdog 改判「**指定角色的那條**是否新鮮」。

- 作法：`on_receive` 已經拿得到 `socket_t sock`，改成 `last_ping_by_sock[sock]`；再要一條「哪個 socket 是本體」的判定（最省的是加一道 `hello <role>` 握手，本體三條連線在 connect 後各送一次）。
- 代價：`TCP_server` 沒有 disconnect 回呼 → socket 表要自己收（可用 `getConnectedClients()` 每輪對帳，既有 API 夠用）。需要動本體三處 connect 點各加一行。
- 風險：**中**。改到 hot path 的第一行；`hello` 沒送到就會變成「本體被判定為不存在」＝比現在更容易誤觸發。
- 得到什麼：watchdog 終於量的是**鏈路**。**不含** roll 的時效（那本來就有，且已在運作）。

### 選項 2（最小改動、最快拿到能見度）：只加觀測，先不改判定 ⭐ 建議先做

不動 watchdog 的觸發邏輯，只在 `cmd_status` 增加**分來源的年齡欄位**（`peer_age_ms_body` / `peer_age_ms_gui`），並在超過門檻時**發 EVT 但不 abort**。

- 代價：**很低**。既有 `imu_roll_age_ms` / `imu_roll_fresh` 就是這個形狀的先例（main.cpp:3864-3870），照抄即可。
- 風險：**低**。純新增，不改任何既有判定路徑，不可能製造新的誤觸發。
- 得到什麼：**下次上機就能量到「本體鏈路實際斷多久」**，而這正是選項 1/3 要用來訂門檻的數字——目前**沒有人有這個數字**（09-08 量到的 7.2 秒是 roll age，不是鏈路 age）。
- 📌 這條同時把 §2.3 的死碼問題暴露出來：本體端若也加一個對稱欄位，就能一眼看出它從來沒動過。

### 選項 3（語意最正確、代價最高）：watchdog 改看「最後一筆有效感測資料」

把判準由「最後一次通訊」換成「最後一筆**有效輸入**」（roll 新鮮 ∨ 計米器有效 ∨ 張力有效）。

- 代價：**高**，而且**語意會打架**。吊機的計米器與張力是**本機 Modbus**（不過隧道），永遠新鮮 ⇒ 只要「任一有效」就等於永遠不觸發，**跟現在一樣壞**；要「roll 必須新鮮」則 roll 本來就是可選降級路徑（`balance_source=meter` 是合法組態），會把一個合法組態變成故障。
- 風險：**高**。可能製造出「IMU 沒接就不能開機」這種新的硬相依。
- 📌 **不建議**。這個方向的正確形式其實是選項 1——「感測資料的時效」已經由 `imu_roll_fresh()` 各自負責了，watchdog 該負責的本來就是**鏈路**，兩件事不要合併。

### 另外必須單獨處理的一條（不在上面三選一裡）

🔴 **§2.3 的死碼**：無論選 1/2/3，本體端 `crane_watchdog_loop_()` 的逾時比較都要**補回來或明確拍板不要**。
目前的狀態（餵食機構齊全、消費者不存在、註解還在解釋為什麼要餵）是三者裡最壞的一種——**它會讓下一個讀這段碼的人以為保護存在**。
兩個方向都可以，但要有人拍板：

- **(a) 補回** `9f33c6a` 的比較（連同 `crane_keepalive_loop_` 只在 motion 時 ping 這件事一起重新評估——不送 ping 的期間 `crane_last_ok_ms_` 本來就會老化，直接接回去會誤觸發）
- **(b) 明確移除**：刪掉 `crane_last_ok_ms_`、`WATCHDOG_TIMEOUT_MS`、三處寫入與三段註解，並在 `crane_watchdog_loop_()` 標明它現在只管張力警報。

---

## 4. 我做了什麼 / 沒做什麼

**做了**：讀 `Crane_control_PI/main.cpp`（watchdog、心跳、IMU 時效、init）、`app/WASH_ROBOT.{cpp,h}`、`app/wash_robot_commands.cpp`、`web_backend/server.js`、`web_backend/public_v2/index.html`、`transport/TCP_server.{h,cpp}`、`.claude/runbook.md`、`CLAUDE.md`；用 `git show HEAD:` 確認發現不是 working-tree 的暫時狀態；用 git history 定位死碼的移除區間。所有行號引用皆逐條核對過。

**沒做**（需要你或使用者接手）：
- ❌ 未 SSH、未跑任何機器（`.25` 程式未啟動、`.26` 未開機）
- ❌ **未實測**「roll 斷 7.2 秒的同時，`last_ping_ms` 實際是被哪一條連線餵的」——§1.3 是**由原始碼與啟動參數推導**的，不是量到的。**選項 2 就是為了把它變成量得到的**
- ❌ 未追完 `crane_cmd_` 各呼叫點的失敗處理是否構成 §2.3 的替代保護（見該節 ⚠️）
- ❌ 未改任何碼、未寫 `changelog.md` / `work_log.md`

**建議併入日誌時的最小三條**：
1. watchdog 心跳單一全域 + GUI 走 localhost ⇒ **結構上不可能偵測網路故障**（比原記載更強，可更新 work_log:39 那列）
2. 🆕 本體端吊機 watchdog 是死碼（2026-04-15 有、06-22 前被移除，餵食機構全留著）—— **新待辦**
3. 🆕 踩坑：**保護機制存不存在，要找「誰讀這個值」，不是「誰寫這個值」**
