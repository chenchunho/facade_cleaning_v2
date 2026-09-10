# crane watchdog 補回 —— 預寫的 patch（**尚未套用**）

> 2026-09-10，AI-2 線。agent-ai-e9 拍板走 **(a)+(b)**：今天只做註解歸位(a)，
> **補回的 patch 先寫好不要上**；(b) vs (c) 是安全決策，已送 per user。
>
> 🔴 **這份文件裡的程式碼不在任何 `.cpp` 裡。** 套用前請重讀「風險」那節。

---

## 現況（複驗過的事實）

| 事實 | 出處 |
|---|---|
| `crane_last_ok_ms_` **寫 3 處、讀 0 處** | `WASH_ROBOT.cpp:45`(ctor) `:823`(OK 回覆) `:2472`(motion_progress) |
| `WATCHDOG_TIMEOUT_MS = 2000` **無任何運算式使用** | `WASH_ROBOT.h:838` |
| 比較曾存在，於 `4d1409c`(06-22) 之前消失 | 舊實作 `6abd8c6:user_lib/WASH_ROBOT.cpp:391` |
| `crane_keepalive_loop_` **執行緒從未啟動**（自 2026-05-15 註解掉） | `WASH_ROBOT.cpp:405-406`；`crane_keepalive_running_` ctor 即 `false` |
| `motion_active_` / `abort_flag` / `evt_` **都還在**，接得回去 | — |

📌 **三個寫入點都還帶著「為了不讓 2s watchdog 誤觸發」的註解在維護** ⇒ 有人一直在有意識地餵它。
這是「補回」而不是「刪除」的主要論據：刪掉等於承認**吊機斷線時本體不需要中止動作**，那是安全決策不是清理。

---

## Patch（插在 `crane_watchdog_loop_` 迴圈末尾，張力警報那段之後）

```cpp
        // [2026-09-10] 補回 4d1409c 之前存在的逾時比較。
        // crane_last_ok_ms_ 由三處刷新：crane_cmd_ 收到 OK(:823)、
        // EVT motion_progress(:2472)、ctor 初始化(:45)。
        const int64_t idle_ms = now_ms_() - crane_last_ok_ms_.load();
        if (idle_ms > WATCHDOG_TIMEOUT_MS) {
            if (motion_active_.load()) {
                abort_flag = true;
                evt_("crane_watchdog timeout — abort_flag set idle_ms="
                     + std::to_string(idle_ms));
            } else {
                evt_("crane_watchdog timeout idle idle_ms=" + std::to_string(idle_ms));
            }
        }
```

**與舊版的差異（刻意的）**：
1. **不送 `crane_cmd_("ping", 2)`**。舊版每 500ms 在主通道送一次 ping ⇒ 搶 `crane_mtx_`。
   2026-05-15 停用 keepalive 的理由（`WASH_ROBOT.cpp:399`「New design: no continuous ping.
   Each crane_cmd_ self-heals on fail.」）**同樣適用於這裡**。
2. **EVT 帶上 `idle_ms`**，否則觸發之後無從判斷是「差一點」還是「早就斷了」。

---

## 🔴 風險（套用前必讀）

| # | 風險 | 說明 |
|---|---|---|
| 1 | 🔴🔴 **會改變執行期行為** | 吊機 2 秒沒回應即 `abort_flag = true` ⇒ **運動中止**。這是本 patch 的目的，也是它的風險 |
| 2 | 🔴 **`WATCHDOG_TIMEOUT_MS = 2000` 這個值從未在現行架構下驗證過** | 它是 04/05 月的值，那時**有** 500ms 的 ping 在餵。**不送 ping 之後，刷新只剩「真的有指令往來」時** ⇒ 長時間純本體動作（ZDT 伸縮 4s+、DM2J 滑台 2-3s）期間沒有任何東西刷新它 ⇒ **極可能誤觸發**。這正是 2026-05-15 當初加 keepalive 要解決的問題 |
| 3 | ⚠️ 與 `crane_attached_` 的互動 | 迴圈開頭 `if (!crane_attached_.load()) continue;` 已擋掉脫離模式，但**重新 attach 時要記得刷新** `crane_last_ok_ms_`，否則一 attach 就立刻逾時 |

### ⇒ 建議的落地順序（**不要一步到位**）

1. **先只發 EVT、不設 `abort_flag`**（把 `motion_active_` 那個分支也改成只 `evt_`），跑幾輪觀察 `idle_ms` 的真實分布
2. 依實測分布**重新選 `WATCHDOG_TIMEOUT_MS`**（2s 幾乎確定太短）
3. 確認不會誤觸發之後，才把 `abort_flag` 接回去

📌 **風險 2 是這份 patch 最重要的一句**：舊版能用 2 秒，是因為它自己每 500ms 餵一次。
**把 ping 拿掉又保留 2 秒，等於把一個原本自洽的設計拆成兩半只裝回一半** ——
那正是這條待辦一開始的成因（張力監控退場時把旁路通道一起帶走）。
