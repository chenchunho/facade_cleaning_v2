# Phase 2.3 可行性評估：把 EVT 接到 `imu_push_loop_` + `peer_age_ms`（AI-2）

- **日期**：2026-09-09（第五輪）
- **緣起**：這是**我自己上一輪提的建議**，`agent-ai-60` 對它的執行緒安全提出疑慮。**疑慮成立方向是對的，但真正的成本不在執行緒安全。**
- **範圍**：只讀不改。未改碼、未編譯、未部署、未啟動主程式、未 SSH。未寫 `changelog.md` / `work_log.md`
- **行號**：本輪全部重新定位（`agent-ai-60` 今日的 `crane_stop_estop_` 改動已讓 `app/WASH_ROBOT.cpp` 位移）

---

## 0. 結論先講（Q5）

> ## ⚠️ **我原本那句「最低成本」講得太快 —— 在它的素樸形式下不成立。**
>
> **執行緒安全本身沒問題**（§1：無鎖環、狀態全部已受保護、`evt_` 早就是多執行緒進入點）。
> **但 `handle_crane_evt_` 的「轉播」那一半是昂貴的一半**，而我上一輪把整個函式當成一個單位來建議，漏掉兩個實質成本：
>
> | # | 成本 | 嚴重度 |
> |---|---|---|
> | 1 | **EVT 會被轉播兩次** —— 吊機是廣播給**所有** client，兩條 socket 各收一份 | 中（GUI log 加倍） |
> | 2 | 🔴 **`evt_` → `broadcast()` 是持鎖的阻塞 `send()`** ⇒ 把 GUI 的反壓接到**roll 資料路徑**上，而 roll 有 **750ms 的硬懸崖** | **高** |
> | 3 | 🔴 **`imu_push_loop_` 自己就有那個無逾時 `connectToServer`** ⇒ 當探針用時，**在「不可達」情境下正好是瞎的** | **高（且是既有 bug）** |
>
> ✅ **但把 `handle_crane_evt_` 拆成兩半之後，它就真的是最低成本了**（§5 Option B）：
> 只從 push loop 呼叫「記錄」那半（atomic + 短鎖，不印、不轉播），三個成本**同時消失**，
> 安全效益（吊機警報在 hold／閒置期間送得到本體）**完全保留**。
>
> 🔴 成本 3 **獨立於本提案**，是 `imu_push_loop_` 自己的 bug，與你今天在 `crane_stop_estop_` 修掉的是同一個。**跑 10 次 LOOP TEST 之前值得單獨處理。**

---

## 1. Q1：執行緒安全逐項判定

### 1.1 `handle_crane_evt_` 實際會碰到的每一個東西

位置：`app/WASH_ROBOT.cpp:2411-2447`。逐行列出**它寫或讀的全部狀態**：

| # | 標的 | 型別 / 保護 | 從新執行緒呼叫安全嗎 |
|---|---|---|---|
| 1 | `std::cout << "[crane_evt] " << line` | 無鎖 | ⚠️ **已經是多執行緒共用**（`imu_monitor_loop_`／watchdog／背景 sweep 都在印）。不會 UB，但**行內交錯**已是現況。不是新風險，但見 §1.4 |
| 2 | `balance_cal_running_.load()` | `std::atomic<bool>`（`WASH_ROBOT.h:1632`） | ✅ 安全 |
| 3 | `crane_alarm_kind_` / `crane_alarm_detail_` | `std::string`，**由 `crane_alarm_mtx_` 保護**（`.h:1828-1830`） | ✅ 安全 —— 而且**今天就已經是跨執行緒的**：寫在 `crane_cmd_`（持 `crane_mtx_`），讀在 `crane_watchdog_loop_`（**不持** `crane_mtx_`，`:2856-2861`）。多一個寫者不改變這個結構 |
| 4 | `crane_alarm_pending_.store(true)` | `std::atomic<bool>`（`.h:1827`） | ✅ 安全 |
| 5 | `crane_last_ok_ms_ = now_ms_()` | `std::atomic<int64_t>`（`.h:1491`） | ✅ 安全。📌 **而且它是死的**（前一份 §2.3：0 處讀取）⇒ 從哪條執行緒寫都不影響任何行為 |
| 6 | `evt_("crane_relay " + line)` | → `evt_cb` → `cmd_server.broadcast()` | ✅ **型別安全**，⚠️ **但語意上有代價**，見 §1.3 / §2 |

**`evt_` 的完整鏈路**（`app/WASH_ROBOT.cpp:454-459` → `facade_cleaning_v2/main.cpp:115-117` → `transport/TCP_server.cpp`）：

```cpp
void WashRobot::evt_(const std::string& msg) {
    if (!evt_cb) return;               // std::function
    ...
    evt_cb(s);
}
// main.cpp:115  robot.evt_cb = [](const std::string& line) {
//                   cmd_server.broadcast(line.c_str(), (int)line.size()); };
// TCP_server::broadcast → std::lock_guard<std::mutex> lock(clients_mtx);
//                         for (sock : clients) send(sock, buf, len, SEND_FLAGS);
```

✅ `evt_cb` 在 `robot.init()` **之前**就指派好了（`main.cpp:115` vs `:124`，該處註解明寫「Wire EVT broadcast before calling init (background threads may fire events during init)」）⇒ **`std::function` 沒有讀寫競爭**，設定一次、之後唯讀。
✅ `broadcast()` 取 `clients_mtx` ⇒ 容器安全。
✅ **`evt_` 今天就已經被多條執行緒呼叫**（`crane_watchdog_loop_:2865`、`imu_monitor_loop_`、背景 sweep）⇒ **多一個呼叫者不是新的類別**。

### 1.2 鎖序：**沒有環，不會死鎖**

| 執行緒 | 取得順序 |
|---|---|
| `crane_cmd_` 路徑（現行） | `crane_mtx_` → `crane_alarm_mtx_`（範圍內釋放）→ `clients_mtx` |
| `crane_watchdog_loop_`（現行） | `crane_alarm_mtx_`（範圍內釋放）→ `clients_mtx` → `state_mtx_` |
| **`imu_push_loop_`（提案）** | `crane_imu_mtx_` → `crane_alarm_mtx_` → `clients_mtx` |

🔴 **判定死鎖的關鍵事實：`crane_imu_mtx_` 全樹只有一個使用者。**

```
/bin/grep -rn "crane_imu_mtx_" app/
  app/WASH_ROBOT.cpp:2995    std::lock_guard<std::mutex> lk(crane_imu_mtx_);   ← imu_push_loop_
  app/WASH_ROBOT.h:1573      std::mutex crane_imu_mtx_;                        ← 宣告
```

⇒ **沒有任何路徑會在持有 `crane_alarm_mtx_` 或 `clients_mtx` 時去取 `crane_imu_mtx_`**（因為根本沒有第二個取用點）⇒ **不可能形成循環等待** ⇒ ✅ **不需要新增任何鎖，也不會有鎖序問題。**

📌 順帶確認 `crane_watchdog_loop_` 的寫法是對的：它把 `crane_alarm_mtx_` 包在**內層 scope**（`:2858-2862`），出了 scope 才呼叫 `evt_` 與 `set_state_`。**如果它是持著 alarm 鎖去呼叫 `evt_`，上面那條就會變成潛在的環。** 這是既有程式碼做對的一件事，改動時不要破壞它。

### 1.3 但是：持鎖時間會變長，而這把鎖後面掛的是 roll

`imu_push_loop_` 在 `crane_imu_mtx_` **範圍內**做完整個 send/recv（`:2995-3006`）。把 `handle_crane_evt_` 塞進去，等於在這把鎖裡再加上「一次 `cout` + 一次 `broadcast`（每個 client 一次阻塞 `send`）」。

因為這把鎖沒有第二個使用者，**鎖競爭不是問題**。問題是**迴圈週期**：見 §2。

### 1.4 一個小但真實的語意衝突

`imu_push_loop_` 的設計意圖寫在 `app/WASH_ROBOT.cpp` 該函式上方：

> 🔴 **失敗完全靜默且不重試** —— 這是「錦上添花」的資料流，不可以拖慢或吵到任何東西。

而 `handle_crane_evt_` 的**第一行就是 `std::cout`**。在運動期間吊機每秒推一則 `motion_progress`（`Crane_control_PI/main.cpp:343` `EVT_PROGRESS_INTERVAL_MS = 1000`）⇒ 直接接上去會讓這條「不可以吵」的迴圈開始每秒印一行。**這本身就是拆分（§5 Option B）的理由之一。**

---

## 2. 🔴 兩個我上一輪沒算到的成本

### 2.1 成本一：EVT 會被轉播兩次

吊機的 `broadcast_evt()`（`Crane_control_PI/main.cpp:1718-1720`）是**推播給所有 client**。本體有三條 socket 連著吊機（`crane_cli_` / `crane_cli_estop_` / `crane_cli_imu_`）⇒ **每一則 EVT，這三條各自收到一份**。

今天只有 `crane_cli_` 會 drain 並呼叫 `handle_crane_evt_` ⇒ 每則 EVT 轉播一次。
提案之後 `crane_cli_imu_` 也會 ⇒ **兩條都轉播 ⇒ GUI 每則 EVT 看到兩次**（只要兩條 socket 都在，而正常情況下都在）。

| 後果 | 嚴重度 |
|---|---|
| GUI log 每則 crane EVT 出現兩行 | 中（會讓人以為發生了兩次） |
| `crane_alarm_pending_` 被設兩次 | 無害（同一個警報，且是 `.store(true)`） |
| `crane_last_ok_ms_` 被寫兩次 | 無害（而且它是死的） |

📌 **這條是 Option B 自動解決的**：記錄那半不轉播。

### 2.2 🔴 成本二：把 GUI 的反壓接到 roll 資料路徑上（這條最重要）

```cpp
// transport/TCP_server.cpp
void TCP_server::broadcast(const char* buf, int len) {
    std::lock_guard<std::mutex> lock(clients_mtx);
    for (auto const& sock : clients) {
        send(sock, buf, len, SEND_FLAGS);      // ← 阻塞 socket 上的 send
    }
}
```

socket 是阻塞模式。若某個 client（例如一個卡住的瀏覽器分頁、或一條慢的 WiFi）沒有在讀，核心送出緩衝區填滿之後 **`send()` 會阻塞**，而且是**持著 `clients_mtx`** 阻塞。

**今天**這個反壓落在 `crane_cmd_` 上（指令路徑）—— 指令本來就有秒級的 timeout，容忍度高。
**提案之後**它會**同時**落在 `imu_push_loop_` 上，而那條迴圈是：

```
Crane_control_PI/main.cpp:884   static constexpr int IMU_ROLL_STALE_MS = 750;
Crane_control_PI/main.cpp:1581  const bool use_imu = want_imu && imu_roll_fresh(&imu_age);
```

⇒ 🔴 **roll 推送只要停超過 750ms，吊機端的平衡就靜默退回 `src=meter`。**
⇒ **一個慢的瀏覽器 client，會讓吊機的平衡控制器降級。** 而且降級是靜默的（只有 stderr 每 2 秒一行）。

📌 這正是 09-08 那條待辦要對付的東西的**反向版本**：那次是隧道讓 roll 過期，這次會變成**我們自己的 GUI** 讓 roll 過期。
📌 而且**在 10 次 LOOP TEST 那種「長時間、大半無人盯著」的場景下最容易發生** —— 瀏覽器分頁開著沒人看、被作業系統降頻、ws 反壓累積，正是這個形狀。

⇒ **這一條讓「素樸版」在我看來不該上。**

### 2.3 🔴 成本三（既有 bug，獨立於本提案）：push loop 自己就會卡住

```cpp
// app/WASH_ROBOT.cpp:2995-3001（現況）
std::lock_guard<std::mutex> lk(crane_imu_mtx_);
if (!crane_cli_imu_.isConnected()) {
    if (!crane_cli_imu_.connectToServer(crane_endpoint_ip_(),
                                        ep::port("CRANE", CRANE_PORT)))
        continue;   // 靜默重試於下一輪
    crane_cli_imu_.set_quiet_reconnect_log(true);
}
```

🔴 **這是你今天剛從 `crane_stop_estop_` 拿掉的那個無逾時阻塞 `connectToServer`，一模一樣的形狀，在這裡還在。**

後果分兩種情境：

| 情境 | push loop 行為 | 當探針好不好用 |
|---|---|---|
| **掉包**（主機可達、回覆丟失）＝ 09-08 那種 | socket 保持連線 ⇒ 不呼叫 connect ⇒ 週期維持 ~250+300ms | ✅ **好用**，4Hz 的探針 |
| **主機不可達**（SYN 黑洞） | 每輪卡在 connect ~127 秒（推導值，見前份 §10.5 B 欄） | ❌ **瞎的** —— 探針在最該報警的情境下每 2 分鐘才動一次 |

⇒ **把它當鏈路探針之前，這一條要先修**，否則 `peer_age_ms` 在「吊機整台不見了」的時候會給出一個每 127 秒才更新一次的數字。

✅ **修法與你在 `crane_stop_estop_` 用的完全相同**：init 預熱 `crane_cli_imu_`（`connectToServer` 一次，失敗也會 `startMonitor()` 交給 500ms `reconnectLoop`），迴圈裡改成「沒連上就 `continue`，絕不 connect」。
📌 而且 `crane_cli_imu_` **原本就會**在第一次成功後由 `reconnectLoop` 維持，所以這個改動只是把「誰負責建立連線」統一到既有的背景執行緒 —— **與你今天的改動同一條理由、同一個 pattern**。

---

## 3. Q2：patch 形狀與 continue 路徑

### 3.1 現行 continue 路徑（原樣照抄，`app/WASH_ROBOT.cpp:2984-3009`）

```cpp
while (imu_push_running_.load()) {
    sleep_ms_(IMU_PUSH_PERIOD_MS);
    if (!imu_push_running_.load()) break;
    if (!crane_attached_.load())   continue;   // (a) bench 脫離模式
    if (imu_.read_error.load())    continue;   // (b) IMU 讀不到 → 讓吊機端自然過期

    ... 組 line ...
    std::lock_guard<std::mutex> lk(crane_imu_mtx_);
    if (!crane_cli_imu_.isConnected()) {
        if (!crane_cli_imu_.connectToServer(...)) continue;   // (c) 連不上
        crane_cli_imu_.set_quiet_reconnect_log(true);
    }
    if (!crane_cli_imu_.sendData(line.c_str(), (int)line.size(), 200)) continue;  // (d) 送失敗
    char buf[128];
    crane_cli_imu_.receiveData(buf, sizeof(buf), 100);       // ← 目前讀完就丟
}
```

✅ **(a)~(d) 四條 `continue` 全部在 `receiveData` 之前**，而新增的解析／dispatch 全部在 `receiveData` **之後**、迴圈本體最末端。
⇒ **原有的錯誤處理與靜默重試語意完全不受影響**：沒送出去就不會有回覆，跳過解析是正確的。

⚠️ **三條必須遵守的約束**，否則就會打亂它：
1. 🔴 **不可以在新增段落裡 `return` 或讓例外逸出** —— 迴圈一結束，roll 推送就永久停止（沒有人會重啟它），而 750ms 之後平衡靜默降級。**解析要包在不會拋的程式碼裡**（`std::string::find` / `substr` 皆不拋；不要引入 `stod`）。
2. 🔴 **不可以在這裡印 log**（見 §1.4 的「不可以吵」設計意圖）。
3. **不可以延長 `crane_imu_mtx_` 的持有時間到會影響 4Hz 週期的程度** —— 這正是 §2.2。

### 3.2 提案的 patch 形狀（Option B：只記錄，不轉播）

**(i) 拆 `handle_crane_evt_`（`app/WASH_ROBOT.cpp:2411`）**

```cpp
// Record-only half: safe to call from ANY thread, including the 4Hz IMU push
// loop. No logging, no evt_ relay — both are the expensive half and both would
// double up (the crane broadcasts each EVT to every client, so two channels
// draining means two relays of the same line).
// Touches only: balance_cal_running_ (atomic), crane_alarm_* (crane_alarm_mtx_),
// crane_last_ok_ms_ (atomic). No lock is held across this call's own locks.
void WashRobot::record_crane_evt_(const std::string& line) {
    if (line.find("tension_alarm") != std::string::npos ||
        line.find("tension_total_limit") != std::string::npos) {
        if (balance_cal_running_.load() &&
            line.find("tension_total_limit") != std::string::npos) {
            // suppressed during balance cal — unchanged rationale, see below
        } else {
            std::lock_guard<std::mutex> lk(crane_alarm_mtx_);
            crane_alarm_kind_ = (line.find("tension_total_limit") != std::string::npos)
                              ? "tension_total_limit" : "tension_alarm";
            crane_alarm_detail_ = line;
            crane_alarm_pending_.store(true);
        }
    }
    if (line.find("motion_progress") != std::string::npos)
        crane_last_ok_ms_ = now_ms_();
}

// Full handler: record + log + relay. Stays on the crane_cmd_ path only.
void WashRobot::handle_crane_evt_(const std::string& line) {
    std::cout << "[crane_evt] " << line << "\n";
    record_crane_evt_(line);
    evt_("crane_relay " + line);
}
```

⚠️ **一處行為差異要注意**：原本 balance-cal 抑制分支會印一行 `"[crane_evt] suppressed (balance cal in progress)"`。搬進 record-only 之後那行不見了。**建議把它留在 `handle_crane_evt_` 裡**（多判一次），或接受它只在指令路徑上出現。這是 log 差異不是行為差異，但 review 時會被看到，先講。

**(ii) `imu_push_loop_` 尾端（`:3006` 之後）**

```cpp
    const int n = crane_cli_imu_.receiveData(buf, sizeof(buf), 100);
    if (n > 0) {
        crane_peer_last_rx_ms_.store(now_ms_());     // §4
        evt_rx_.append(buf, (size_t)n);              // loop-local line buffer, §3.3
        size_t pos;
        while ((pos = evt_rx_.find('\n')) != std::string::npos) {
            std::string one = evt_rx_.substr(0, pos);
            evt_rx_.erase(0, pos + 1);
            if (!one.empty() && one.back() == '\r') one.pop_back();
            if (one.rfind("EVT ", 0) == 0) record_crane_evt_(one);
            // 非 EVT（set_imu_roll 的 "OK"）照舊丟棄
        }
        if (evt_rx_.size() > 4096) evt_rx_.clear();  // 防呆：沒有換行時不要無限長大
    }
```

📌 這一段**沒有新增任何鎖**，`record_crane_evt_` 自己取 `crane_alarm_mtx_`（短、且無環，見 §1.2）。

---

## 4. Q3：收包邊界 —— 行緩衝是**必要的**，不是保險

### 4.1 `receiveData` 沒有任何框架化

```cpp
// transport/TCP_client.cpp
int TCP_client::receiveData(char* buf, int bufSize, int timeout_ms) {
    ... setsockopt(SO_RCVTIMEO) ...
    int received = recv(sock, buf, bufSize - 1, 0);   // ← 單次 recv，拿到多少算多少
    ...
}
```

**單次 `recv()`，無框架化、無累積。** ⇒ 一次呼叫可能拿到：半行／一行／多行／一行半。這是 TCP，本來就沒有訊息邊界。

### 4.2 🔴 有具體證據會被切半，不是理論風險

`buf` 是 128 bytes，而 `recv(sock, buf, bufSize - 1, 0)` ⇒ **單次最多 127 bytes**。

吊機的 `make_device_state_line()`（`Crane_control_PI/main.cpp`，開機時 `:5267` 廣播）組出來是：

```
EVT device_state gw_a=1 gw_b=1 gw_m=1 gw_c=1 gw_d=1 vfd_left=1 vfd_right=1 \
meter_left=1 meter_right=1 meter_middle=1 clv900=1 dsz_left=1 dsz_right=1 pqw_water=1
```

⇒ **約 165 bytes > 127** ⇒ **這一則必定被切成至少兩次 `recv`。**

而 `motion_progress`（約 85 bytes）雖然單獨放得下，但只要前面接了一個 `OK\n` 或另一則 EVT，尾巴照樣會被切掉。

🔴 **不做行緩衝的後果是靜默漏警報**：`line.find("tension_alarm")` 對半行回傳 `npos` ⇒ **警報被吃掉，沒有任何錯誤訊息**。這正好是我們想補的那個洞的**同一種失敗形狀**。

### 4.3 成本

**約 10~12 行，且是本樹已經用了三次的既有樣板**：

| 既有實例 | 位置 |
|---|---|
| `crane_cmd_` 的 drain 迴圈 | `app/WASH_ROBOT.cpp:790-805` |
| 吊機的 `on_receive` | `Crane_control_PI/main.cpp:4986-4996`（`thread_local std::string rx_buf`） |
| `web_backend/server.js` 的 bridge | `state.buf` 累積 + split |

⇒ **成本低，但是必要項不是加分項。** 建議用**迴圈區域變數**（`std::string evt_rx_;` 宣告在 `while` 之前），不要用成員變數——沒有第二個使用者，放成員只會多一個要考慮執行緒安全的東西。

---

## 5. Q4：`peer_age_ms` 那一半

### 5.1 變數

```cpp
// app/WASH_ROBOT.h，放在 crane_cli_imu_ / crane_imu_mtx_ 附近（:1567-1575 那一區）
// Link liveness probe. The IMU push loop is the only body->crane traffic that
// runs during hold and idle, so it is the natural place to score the link.
// Any inbound byte on that socket means the crane answered.
std::atomic<int64_t> crane_peer_last_rx_ms_{0};   // 0 = never
static constexpr int CRANE_PEER_STALE_MS = 3000;  // 12 push periods
```

**由誰更新**：`imu_push_loop_`，在 `receiveData` 回傳 `n > 0` 時 `store(now_ms_())`（見 §3.2 (ii)）。**只有這一個寫者、只有 `cmd_status` 一個讀者，且是 atomic** ⇒ 無需鎖。

### 5.2 `cmd_status` 加欄（照吊機 `imu_roll_age_ms` 的形狀）

吊機端的樣板（`Crane_control_PI/main.cpp:3864-3870`）是「age + fresh 兩欄」：

```cpp
const bool fresh = imu_roll_fresh(&age);
oss << " imu_roll=" << ...;
oss << " imu_roll_age_ms=" << age;
oss << " imu_roll_fresh=" << (fresh ? 1 : 0);
```

本體對應位置在 `app/wash_robot_commands.cpp:3743`（`crane_attached=` 那一行）旁邊：

```cpp
oss << " crane_attached=" << (crane_attached_.load() ? "on" : "off");
// [2026-09-09] Link liveness as measured by the IMU push channel — the only
// body->crane traffic that exists during hold and idle.
{
    const int64_t st = crane_peer_last_rx_ms_.load();
    const int64_t age = (st == 0) ? -1 : (now_ms_() - st);
    oss << " crane_peer_age_ms=" << age;
    oss << " crane_peer_fresh=" << ((st != 0 && age <= CRANE_PEER_STALE_MS) ? 1 : 0);
}
```

`-1` ＝「從未收到」，與吊機 `imu_roll_fresh()` 的 `stamp==0` 慣例一致。

### 5.3 ⚠️ 兩個判讀陷阱，一定要寫進註解

1. 🔴 **不推送時 age 會自然長大，那不是故障。** 迴圈有四條 `continue`（§3.1），其中 `crane_attached_=off`（bench 脫離）與 `imu_.read_error`（IMU 拔掉／壞掉）都會讓迴圈不送 ⇒ 不會有回覆 ⇒ age 一路增加。
   ⇒ **`crane_peer_fresh=0` 不等於「鏈路斷了」**，它只等於「這條通道最近沒有收到位元組」。
   ⇒ 建議同時把原因印出來，例如再加一欄 `crane_peer_probing=<0|1>`（＝這一輪有沒有真的送出去），否則第一次看到 `fresh=0` 的人會誤判。
2. ⚠️ **`CRANE_PEER_STALE_MS = 3000` 是我挑的，沒有實測依據。** 推理是：4Hz 推送 ＋ 每輪 100ms 收包窗，健康鏈路每秒約 4 次更新；3 秒 ＝ 容許連續漏 12 次，避免抖動。**但 09-08 量到閒置時隧道空窗可達 10.6 秒**（你們的待辦表第 38 列），所以在**隧道**上 3000ms 會頻繁翻紅。
   ⇒ **切到 `192.168.1` 有線之後這個值才有意義；在隧道上要先量再定。** 這與 `IMU_ROLL_STALE_MS` 那條待辦是同一個性質的問題（門檻要配鏈路），**建議一開始就只當觀測欄位、不接任何自動處置**，這也正是我上一輪「選項 2」的原意。

---

## 6. Q5：所以它到底是不是最低成本？

### 6.1 修正我上一輪的說法

| 我上一輪說的 | 實際 |
|---|---|
| 「`imu_push_loop_` 已經每 250ms 在讀那條 socket，讀完直接丟掉。改成丟給 `handle_crane_evt_` 就好，成本最低」 | ⚠️ **「就好」是錯的**。`handle_crane_evt_` 的後兩件事（`cout`、`evt_` 轉播）**不能**從那條迴圈呼叫，理由是 §2.1 的重複轉播與 §2.2 的 roll 反壓 |

**我漏掉的原因很具體**：我上一輪是從「呼叫點在哪裡」的角度看這個函式（它只被呼叫一次 ⇒ 加一個呼叫者很便宜），**沒有看它裡面做了什麼**。📌 這剛好是我自己在前一份提的那條通則的反面 —— 我當時說「要找誰讀這個值」，這次該問的是「**這個函式對它的呼叫者索取什麼**」。

### 6.2 選項比較

| | 做法 | 成本 | 風險 |
|---|---|---|---|
| **A** | 素樸版：`imu_push_loop_` 直接呼叫 `handle_crane_evt_` | 最少的字 | 🔴 **不建議**：重複轉播 + roll 反壓（§2.1/§2.2） |
| **B** ⭐ | 拆成 `record_crane_evt_`（記錄）／`handle_crane_evt_`（記錄+印+轉播），push loop 只叫前者 | 拆一個函式（~15 行）＋ 行緩衝（~12 行）＋ age 欄位（~8 行） | 低。**無新鎖、無鎖序問題、無新執行緒** |
| **C** | 在 `crane_cli_`（主通道）加背景 drain 執行緒 | — | ❌ **絕對不行**：`crane_cmd_` 同步讀同一個 socket，兩個讀者會互相偷走回覆 |
| **D** | 第四條專用 EVT 訂閱 socket | +1 socket + helper + init 預熱 | 中。最乾淨，但與水閥那條的 B3 是同一個「要不要再加通道」的決定 |

### 6.3 結論

> ✅ **Option B 是真正的最低成本，而且我認為值得在 10 次 LOOP TEST 之前做。**
> 它讓吊機的 `tension_alarm` / `tension_total_limit` 在 **hold 與閒置期間**送得到本體 —— 而那正是目前完全沒有覆蓋的時段，也正是長時間無人盯著的測試會待最久的時段。
>
> 🔴 **但有一個前置條件：§2.3 的 `connectToServer` 要先修。** 否則在「吊機整台不見了」的情境下，這條探針每 127 秒才動一次，而那正是最需要它的時候。那個修法你今天已經在 `crane_stop_estop_` 做過一次，**是同一個 pattern 的第二次套用**。

**建議順序**：
1. 🔴 **先修 `imu_push_loop_` 的 `connectToServer`**（init 預熱 + 迴圈內不 connect）—— 它獨立於本提案、本身就是 bug，而且是 §2.3 的前置
2. ⭐ **Option B 的拆分 + 行緩衝** —— 拿回「hold/閒置期間收得到吊機警報」
3. **`crane_peer_age_ms` / `crane_peer_fresh` 觀測欄位** —— 只觀測、不接自動處置，門檻等切到有線之後再量（§5.3）

⚠️ **三項合起來仍然只動 `app/WASH_ROBOT.{cpp,h}` 與 `app/wash_robot_commands.cpp` 的 `cmd_status`，不動吊機、不動任何運動路徑、不新增執行緒。**

---

## 7. 未做 / 未驗證

- ❌ **未套用任何改動、未編譯、未部署**（依指示）；上面所有程式碼是提案文字
- ❌ **未執行期驗證**：兩台主程式都沒啟動，`crane_peer_age_ms` 在真實鏈路上的分佈**沒有數據**，`CRANE_PEER_STALE_MS = 3000` 因此是推理值不是量測值（§5.3）
- ❌ §2.3 的「~127 秒」沿用前一份 §10.5 的推導（Linux `tcp_syn_retries` 預設值），**未在兩台 Pi 上實查**
- ❌ **未確認 `send()` 在本專案的 client socket 上實際會不會阻塞到有感** —— §2.2 是從「阻塞 socket + 持鎖 broadcast」推導的結構性風險，**沒有實測過反壓場景**。這條是我判定「素樸版不該上」的主要依據，**如果你要推翻它，這是該量的東西**
- ❌ 未評估 `record_crane_evt_` 拆出後對既有 `handle_crane_evt_` 呼叫點（`app/WASH_ROBOT.cpp:806`）的等價性做形式驗證 —— 只做了逐行閱讀
- ❌ 未寫 `changelog.md` / `work_log.md`
