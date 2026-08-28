# fake_slaves — 不用硬體驗證 driver 的回覆處理

`user_lib/` 的 driver 只有接上真裝置才跑得到「收到壞幀會怎樣」的路徑，
而那正是最容易出事、也最少被測到的地方。這裡的假從站在 `127.0.0.1` 上
提供同樣的線路格式，並且**可以故意把回覆弄壞**。

## 為什麼會有這個目錄

2026-08-28 的 driver 稽核發現 `SD76_length_meters` 與 `DSZL_107` 都會接受
壞掉的 Modbus 幀，並把長度欄位直接拿去 `memcpy` 進呼叫端的堆疊緩衝
（2~64 位元組）。實測：SD76 `byteCount=0xFF` → **SIGSEGV**；
DSZL `bc=100` → **SIGBUS**。

兩支都修了，而且**先對未修補的版本跑一次證明缺陷存在，再跑修補後的版本**。
📌 **編譯過不等於驗證過**——這個目錄存在就是為了讓「真的拿它做一次事」有工具。

## 用法

```bash
# 1) 開假從站（背景），2) 跑測試程式，3) 等從站收工
python3 fake_rtu.py --mode badcrc --slave 1 --port 14001 &
sleep 0.4 && ./test_sd76 badcrc
```

| 檔案 | 用途 |
|---|---|
| `fake_rtu.py` | **Modbus RTU over TCP**——`user_lib/` 除 DSZL 外全部走這個（USR-TCP232 透傳） |
| `fake_dszl_tcp.py` | **Modbus TCP（MBAP）**——只有 `DSZL_107`（X518 原生 :502） |
| `test_<device>.cpp` | 各 driver 的測試程式，一支一個 |

### 故障模式

`normal`／`badcrc`／`wrongslave`／`badfc`／`shortframe`／`bigcount`／`overflow`
以及 **`drop`（2026-08-28 新增）**。

🔴 **`drop` 跟其他模式差在本質**：其他模式都會回一個「壞掉的東西」，走的是
「收到了但不合法」那條路；`drop` 是**根本不回**，那是唯一能驗到
**重試與逾時**的模式。

搭配 `--drop-count N`：只丟掉前 N 個該出錯的請求，之後恢復正常。
用它可以驗「重試在第幾次救回來」——例如 driver 重試上限 3 次時，
`--drop-count 2` 應該在第 3 次成功並印出 `recovered on attempt 3`。

**加它的動機**：2026-08-28 實機量到 QX-DO24 的 PWM 寫入約 20% 出現
`no reply (timeout)`，據此加了交易層重試；但當天再測 15 次**一次都沒失敗**，
`recovered on attempt` 計數是 0 —— **救援路徑實作了卻從未被執行過**。
📌 **等硬體自然出錯不是驗證方法。**

### 已有的測試

| 測試 | 驗什麼 |
|---|---|
| `test_sd76` / `test_dszl` / `test_dy500` | 壞幀 → 呼叫端堆疊覆寫（原始動機，見上） |
| `test_stage2` | 第二階段回覆處理 |
| **`test_qx_do24`** 🆕 | **重試救援路徑**（`recover`／`alldrop`）＋ 失敗原因是否與「參數超範圍」分得開 |
| **`test_dm2j`** 🆕 | **機構標定換算**（讀數比值應等於導程）＋ **行程守衛**（超範圍要在送出任何位元組之前就拒絕） |

### 驗證狀態（2026-08-28）

| 項目 | 狀態 |
|---|---|
| `fake_rtu.py` 的 `drop` 模式 | ✅ **已實測**（本機 Python client）：`--drop-count 2` → 第 1、2 次逾時無回覆、第 3 次起恢復；不帶 `--drop-count` → 三次全滅 |
| 既有模式未被破壞 | ✅ `normal` / `badcrc` 三次皆正常回 9 bytes |
| `test_qx_do24.cpp` / `test_dm2j.cpp` | 🔴 **尚未編譯、尚未執行** —— 寫的當下兩台 Pi 已交還給 main 分支的人，而本專案的 C++ 只能在 Pi 上編 |

🔴 **所以這兩支測試本身還沒被驗證過。** 下次拿到 Pi 的第一件事就是編起來跑一遍：
`test_qx_do24 recover` 應該在 log 看到 `recovered on attempt 3`，
`test_dm2j` 的守衛那兩發要對照假從站輸出確認 **req# 沒有增加**。
📌 **寫了測試不等於測試會動** —— 這正是本檔開頭那句「編譯過不等於驗證過」再升一層。

### 🔴 這個框架**測不到**什麼（別誤以為有涵蓋）

假從站只能站在 **driver 與匯流排之間**。凡是換算或判準寫在**應用層**的，
這裡都碰不到：

- **推桿的 `3000 pulse/cm`**——`WashRobot::cm_to_pulses_for_slave_` 是 private member，
  要測它得把整個 `WashRobot` 起來、連上所有裝置。**硬寫只會得到一支測不到東西的測試**。
  它目前的保護是「單一常數 `CUP_PULSE_PER_CM` + 實機量測紀錄」，不是自動測試。
- **左右歸屬**（`ZDT_RF1/LF1...`）——同理，且它的正確性只有實體排列能決定。
- **時序類參數**（`*_EST_MS`）——假從站的回應速度跟真裝置無關。

📌 寫新測試前先問：**這個東西的判準在 driver 裡，還是在應用層？**
在應用層的話，這裡寫什麼都是自我安慰。

編譯測試程式（在 Pi 上，路徑依實際擺放調整）：

```bash
g++ -std=c++17 -O2 -I../../user_lib -I../../transport -o test_sd76 \
    test_sd76.cpp ../../user_lib/SD76_length_meters.cpp ../../transport/TCP_client.cpp -lpthread
```

## 🔴 兩個會讓測試「全部通過卻什麼都沒測到」的陷阱

**① 先確認 driver 的 `init()` 會不會 probe。**
會 probe 的話，第 1 個請求是 probe，必須正常回覆否則 `init()` 就先失敗了，
故障要放在第 2 個請求（`--fault-from 2`）。
`SD76` 有兩個 overload：`init(TCP_client&, ...)` **會** probe、
`init(ip, port, ...)` **不會**。第一版測試用錯 overload，結果五個故障情境
一個都沒被觸發、卻全部「通過」。

**② 溢位情境的 byte count 必須落在窗口內。**
窗口＝「幀還塞得進 driver 的收包緩衝」且「payload 超過**呼叫端**的緩衝」。
取極端值（`0xFF`）通常只會被既有的長度檢查擋掉，看起來像「這裡沒缺陷」——
DSZL 就是這樣差點被判無缺陷，`bc=255` 回 FAIL，換成 `bc=100` 才 SIGBUS。
用 `--overflow-bc` 指定。

## 限制

- 只驗**協定層**的處理，不驗裝置語意（暫存器值對不對、時序對不對）
- 不取代實機驗證。這裡通過只代表「壞幀不會把程式打壞」
