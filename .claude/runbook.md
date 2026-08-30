# Runbook — 從冷開機到可操控

> 本文件說明如何啟動整套系統、透過 GUI 與 raw command 操控、典型流程與緊急處置。
> 硬體拓撲與指令語意權威：`.claude/motion_flow.md`；本文件只列「怎麼用」。

---

## A. 啟動順序

> 🔴 **2026-08-28 路徑全面更正。** 本檔原本寫的 deploy 路徑
> （`~/<project>/bin/ARM/Release/<name>`）**五個地方都錯**：少了 `projects/`、
> 平台寫 `ARM`（實際 `ARM64`）、組態寫 `Release`（實際 `Debug`）、檔名少了 `.out`、
> web 目錄寫 `washrobot_web_backend`（實際 `web_ver2`，前者根本不存在）。
> 照舊寫法跑 `scripts/*.sh` 會直接報錯。`scripts/crane.sh`／`wr.sh` 的預設值已同步更正。

### 連線資訊（2026-08-27 實測）

兩台 Pi 各有**兩條路**：有線是 bench 正式配置，WiFi 是備援／驗證用。**帳號兩台不一樣。**

| 機器 | hostname | 有線（正式） | WiFi（備援） | 帳號 |
|---|---|---|---|---|
| 洗窗本體 | `washrobot` | `192.168.1.100` | `192.168.5.26` | **`nexuni`** |
| 吊機 | `raspberry-cran` | `192.168.1.10` | `192.168.5.17` | **`user`** |

- 兩台皆 **aarch64 / Debian 13 (trixie) / g++ 14.2**
- 🔴 **帳號不是 `pi`**（2026-08-28 已全檔更正為 `nexuni@` / `user@`）
- 🔴 **吊機有線是 `192.168.1.10` 不是 `.101`**（2026-08-28 全檔更正）。✅ `web_backend/server.js` 的 `CRANE_IP` 預設值**已於 `f4e0d02` 改為 `.1.10`**（2026-08-29 複查確認；本行原本寫「仍是 `.101`」＝過期）。⚠️ 現行的兩支 C++ 走的是 `app/WASH_ROBOT.h` 的 `CRANE_IP = "192.168.5.17"`（WiFi）—— **eth 串接後要回頭改它**，見待辦總表
- 📌 `192.168.5.26` 在 changelog／work_log 裡以 `[TEST MODE]` 出現過（`CRANE_IP` 曾被暫時改成它），
  **那不是筆誤，就是這台的 WiFi 位址**
- ⚠️ **測試環境實體位於倉庫（新國街）**，與 `192.168.5.0/24` 的其他設備同網段。
  這兩台是專案測試機，不納入 `remote_hosts/` 管理
- 金鑰登入：Windows 端與 WSL 端用同一把 `id_ed25519`，已裝上兩台的 `authorized_keys`

> 🔴 **C++ 改動只能在 Pi 上驗證。** Visual Studio 的 "Visual C++ for Linux Development" 是把原始碼
> 送到 Pi、用 **Pi 上的 g++** 編譯——建置不發生在 Windows。沒有 Pi 就無法驗證任何 C++ 改動。
> 快速語法檢查（不產生檔案）：
> `ssh nexuni@192.168.5.26 "cd ~/projects/<專案>/user_lib && g++ -fsyntax-only -std=c++17 -I. <檔案>.cpp"`

### 0. 一鍵啟動（tmux launcher，bench / 測試用）

每台 Pi 上都有對應的 launcher script，會用 tmux 把該機所有程式各開一個 window：

```bash
# crane Pi (192.168.1.10)
ssh user@192.168.1.10
cd ~/facade_cleaning_v2       # repo（scripts/ 所在處，與部署路徑不同）
chmod +x scripts/*.sh       # 第一次用要給執行權限
./scripts/crane.sh start    # 開 Crane_control_PI + web_backend + 一個空 shell
./scripts/crane.sh attach   # 進去看 log

# washrobot Pi (192.168.1.100)
ssh nexuni@192.168.1.100
cd ~/facade_cleaning_v2
chmod +x scripts/*.sh
./scripts/wr.sh start       # 開 facade_cleaning_v2 + frame_capture + 一個空 shell
./scripts/wr.sh attach
```

**tmux 操作**：

| 動作 | 鍵 |
|------|----|
| 切 window（main / cam / web / shell …） | `Ctrl-b` 然後 `0`/`1`/`2` |
| 列表選 window | `Ctrl-b w` |
| **離開但程式繼續跑**（SSH 斷了也沒事） | `Ctrl-b d`（detach） |
| **只關當前 window 的程式** | `Ctrl-C` |
| 重開剛剛關的程式 | `↑` 然後 `Enter` |
| 全關 | `./scripts/{wr,crane}.sh stop` |

**路徑覆蓋**：預設值就是下面手動段落那組實際部署路徑（`~/projects/<project>/bin/ARM64/Debug/<name>.out`）。若路徑不一樣：

```bash
WR_BIN=/path/to/facade_cleaning_v2 ./scripts/wr.sh start
CRANE_BIN=/path/to/Crane_control_PI WEB_DIR=/path/to/web_backend ./scripts/crane.sh start
```

**測試模式吊車**（crane_shim 取代主吊車）：

```bash
CRANE_BIN="python3 $HOME/facade_cleaning_v2/crane_shim/crane_shim.py" \
  ./scripts/crane.sh start
```

下面 1~3 是**手動逐項啟動**的對照版本（看 log 直接、能控制每個程式分開重啟）。launcher 內部就是把這些指令各塞進一個 tmux window。

### 1. Crane RPi (192.168.1.10) — 先啟動

```bash
# SSH 進 crane Pi
ssh user@192.168.1.10

# Terminal 1: 吊機主程式（C++）
cd ~/projects/crane_control_PI/bin/ARM64/Debug
./crane_control_PI.out
# → 印出 "[OK] command server :5002"

# Terminal 2: Web Backend（Node.js）
cd ~/projects/web_ver2
node server.js
# → 印出 "[web_backend] listening http://0.0.0.0:8080"
# 或用 systemd unit 背景跑
```

### 2. Washrobot RPi (192.168.1.100)

```bash
ssh nexuni@192.168.1.100
cd ~/projects/facade_cleaning_v2/bin/ARM64/Debug
./facade_cleaning_v2.out
# → 印出 "[OK] command server :5001"
# 會自動 lazy connect crane :5002（連不到會 WARN 但不擋 boot）
```

### 3. 瀏覽器

```
http://192.168.1.10:8080
```

頂部 dot 應變綠（washrobot / crane / arm）；banner 隱藏表示主系統正常模式。

---

## A2. 上機檢查表 —— `fix/driver-crc`

> 📌 **2026-08-29 決策（per user）：直接上 `fix/driver-crc`，不再分兩段。**
>
> 原計畫是先上 `refactor/app-layer`「證明搬家沒搬壞」、再上 driver 分支。
> **那個計畫的前提在 08-28 中午就失效了**：整理分支後來陸續被 cherry-pick 進
> 9 個刻意的行為改變（`MSG_NOSIGNAL`／`CRANE_IP`+VFD 型號／上滑台 7.731 換算+行程守衛／
> `ARM_SWEEP_RPM` 1000→250／QX_DO24 PWM 重試+回讀／`zdt_pusher` 範圍分岔／
> `CUP_PULSE_PER_CM`／左右歸屬／`SO_ERROR`），兩條分支已經分不出「純搬家」與「改了行為」。
> 要維持分兩段就得另開一條真正只有搬家的分支，代價大於收益。
>
> 🔴 **放棄分兩段的代價要講清楚**：上機若出現非預期行為，**不再能靠「哪一段出現的」來歸因**。
> 取而代之的是下面那張清單——**上機前先讀一遍，知道哪些行為本來就會不一樣**。
>
> ⚠️ **本分支尚未含 `origin/main` 的 `6523b54`**（2026-08-29 10:52，對方的同步步伐 PWM +
> 恢復內建清洗 + 停用自動補救）。要不要先合併進來是**上機前的獨立決定**，見本節末。

### 本分支相對 `0d5f6bc` 的行為改變清單（🔴 上機要盯的就是這些）

🔴 **2026-08-28 合併後，基準線變了**：本分支已合入 `origin/main` 的 `0d5f6bc`
（對方的 bench 修正批次）。所以「功能等價」現在是**相對 `0d5f6bc`**，不是相對舊的 `e3c8820`。
`0d5f6bc` 自己帶來的行為改變（牆距 400、掃動等待 4500ms、破真空 300ms gap、
JC-100 fast-fail、PWM 改 slave 9 啟用、上滑台搬 `.20`）**不是搬家造成的**，
判讀時要跟「搬家有沒有搬壞」分開看。

`0d5f6bc` → `fix/driver-crc` 的差異分三塊。**只有第三塊會在機器上看得出來。**

#### 塊一：無行為影響

| 類別 | 內容 |
|---|---|
| 檔案搬家 | `WASH_ROBOT.{h,cpp}` → `app/`；`TCP_client`／`TCP_server`／`Serial_port` → `transport/` |
| 刪除 | `windows_test/`（不在 Pi 建置目標裡） |
| 非二進位 | `web_backend/server.js` 的 `CRANE_IP` `.1.101` → `.1.10`（另一支行程，不影響上機的兩支二進位） |
| 新增測試 | `Linux_test/fake_slaves/`（假從站，不進主二進位） |

#### 塊二：只有輸出字串會變（行為相同）

| 內容 | 上機會看到 |
|---|---|
| 吊機 `init()` 的 VFD 型號改吃 `CRANE_VFD_NAME` 巨集（原本寫死 `MH300` 在 `#if` 外） | `VFD left (SE3)` 而非 `(MH300)`。**baseline 印的那個是假的** |
| `send(..., 0)` → `send(..., MSG_NOSIGNAL)` | 無可觀察差異（兩支 `main.cpp` 本來就 `signal(SIGPIPE, SIG_IGN)`） |
| `status` 新增 `p_err=`、`cmd_attach` 回覆新增 `partial_seal=N` | **只在失敗／部分密封時才附加**，正常路徑看不到 |

#### 塊三：🔴 刻意的行為改變 —— 上機要盯的就是這 9 條

| # | 改動 | 上機會看到什麼 |
|---|---|---|
| 1 | **上滑台 cm↔pulse 換算修正**（皮帶軸實測 7.731 cm/圈，原假設 1）＋ 行程守衛 48cm | `init()` 多印 `lead=7.731 cm/rev travel<=48 cm`；**所有上滑台距離變成真公分**（約為修正前的 1/7.7）。修正前 `ARM_SWEEP_CM=17` 實際下 131cm 而滑台只有 50cm＝每次撞到底 |
| 2 | **`ARM_SWEEP_RPM` 1000 → 250**（per user） | 掃動變慢。⚠️ 250rpm 實際仍是 **32.2 cm/s**，且 08-28 已實測失步 → **RPM/ACC/DEC 重評仍是待辦** |
| 3 | **吸盤左右歸屬修正** RF={5,6}/LF={7,8} → 右={5,7}／左={6,8} | **交替步伐從「不可用」變成可用**。🔴 **從未實機驗證**，第一次跑 `do_step_down_`／`do_step_up_` 要有人在旁邊看 |
| 4 | **推桿 `CUP_PULSE_PER_CM` 2857 → 3000** | 推桿行程約 +5%（08-27 那次「更正」本身是錯的） |
| 5 | **9 支 driver 補回覆驗證**（CRC／byteCount／幀長） | 壞幀由靜默吞掉變成明確失敗 → **ZDT 在步態迴圈裡，步態中途失敗頻率可能上升**；PQW／SD76 會讓既有兩條待辦更常出現。**三者都是把問題變可見，不是新故障** |
| 6 | **`SO_ERROR` 驗證**（非阻塞 connect） | 連到沒人聽的埠不再印 `reconnect success`。實機雙向斷言已驗過 |
| 7 | **DM2J 16 個 `void` 改回傳 `bool`**（含兩個「停止」）＋ `zdt_pusher`/`disable`/`enable` 範圍分岔修正 | 上滑台寫入失敗終於會被偵測到；三個指令從「不可能成功」變成可用 |
| 8 | **QX-DO24 PWM 交易層重試 + 回讀驗證** | `[QX:9] no reply` 會重試而非直接失敗。⚠️ **與 `origin/main` `6523b54` 的應用層重試會疊加**，見本節末 |
| 9 | **`cmd_side_measured` 補 `abort_flag` 重置**＋緊急收繩補張力警示（**刻意不加自動停止**） | 被 stop 過一次後不再永久回 `ERR aborted`；緊急收繩超張力會印警示但**不會停** |

📌 **另有 39 處 null-client 守衛（8 支 driver）**：正常路徑碰不到，只有未 init 的實例被呼叫時
才從「segfault 整個行程」變成「回傳錯誤」。**看不到就是對的。**

🔴 **`0d5f6bc` 自己帶來的行為改變**（牆距 400、掃動等待 4500ms、破真空 300ms gap、
JC-100 fast-fail、PWM 改 slave 9 啟用、上滑台搬 `.20`）**不在上表**——那是對方的修正，
baseline 已含。判讀時不要算到本分支頭上。


### 🔴 那 9 條的逐條實機檢查表（2026-08-30 建立）

> **上機時間貴，不該花在現場想怎麼驗。** 每條寫明：**看什麼／正常長什麼樣／
> 不正常長什麼樣**。順序刻意由「不會動的」排到「會動的」——
> 前面幾條沒過就不要往下走。
>
> 🔴 **全程要有第二個人在旁邊**，且手放在急停上。第 ③ 條是唯一一條「錯了會讓
> 機器在貼牆狀態下放錯邊」的，它排在需要有人看著的位置。

#### 階段 A — 開機就看得到（不動馬達）

| # | 看什麼 | 正常 | 不正常 |
|---|---|---|---|
| ① | `init()` 印的上滑台標定 | `lead=7.731 cm/rev travel<=48 cm` | 印 `lead=1` → profile 沒載到或常數被改回 |
| ⑦ | `zdt_pusher 5 extend` | 回 `OK`（或實際動作的回覆） | 回 `ERR usage:zdt_pusher_<1..4>` → 拿到舊版二進位 |
| ⑨a | `cmd_side_measured` 在 `stop` 之後 | 送一次 `emergency_stop`／`reset`，再下 v2 step 指令 → 正常執行 | 永久回 `ERR aborted` → `abort_flag` 沒被重置 |
| ⑥ | **吊機關機**時本體的重連訊息 | `reconnect success` **0 次**；只看到 reconnect failed | 吊機沒開卻印 `reconnect success` → `SO_ERROR` 修正沒進去 |

⚠️ **⑨b 已知未修**：`cmd_arm_sweep()` 進場**沒有**重置 `abort_flag`（姊妹函式有）。
所以任何一次 `stop`／`emergency_stop` 之後，`arm_sweep` 會**永久回 `ERR aborted`
且完全沒有輸出**（`try_or_pause_` 的中止路徑不印任何東西）。
🔴 **這不是這次上機要驗的東西，是要知道它會這樣** —— 遇到就先 `reset`／`init`。

#### 階段 B — 動單一軸，拿尺量

| # | 看什麼 | 正常 | 不正常 |
|---|---|---|---|
| ① | `arm_sweep` 的**實際位移** | 指令 17cm → **拿尺量到約 17cm**（08-28 最小平方殘差 ±0.15cm） | 走 7.7 倍（會直接撞到底）／行程守衛拒絕合法指令 |
| ④ | 推桿行程 | 指令 16cm → 實際 16cm（08-28 實測 47994 脈衝 = 16cm） | 差約 5% → `CUP_PULSE_PER_CM` 還是 2857 |
| ② | 掃動速度與失步 | 見下方專門段落 | 見下方 |

🔴 **拿尺量兩次，不要看座標。** 驅動器只數脈衝 —— **失步時座標永遠顯示正確、
一個字都不會說**。08-28 就是這樣讓「每個 cm 指令走 7.7 倍」隱形了四個月。

🔴 **② 的驗法有陷阱**：`ARM_SWEEP_RPM` 250 在 08-28 **已實測失步**。
而「回 0 點正確」對失步**沒有鑑別力** —— 上滑台的零點是**機械硬限位**，
不管中途失步多少都會頂回同一位置。
→ **參考點不能是限位本身**：在行程中段做一個記號，來回之後量那個記號。
📌 使用者指定要試 **500rpm**（＝64.4 cm/s）；起點已建好但中途中止過。

#### 階段 C — 交替步伐（🔴 最危險的一條）

| # | 看什麼 | 正常 | 不正常 → **立即急停** |
|---|---|---|---|
| ③ | 放開一側時**另一側的兩顆是否都還吸著** | 放開右側 `{5,7}` 時，左側 `{6,8}` 兩顆都吸住 | 另一側只有一顆吸著、或吸的是 `{5,6}`／`{7,8}` 這種上下對 |

🔴 **這條的修正依據只有 2026-08-28 使用者的口頭確認，程式從未在機器上跑過交替步伐。**
`WASH_ROBOT.h` 的註解自己寫著「第一次跑 `do_step_down_`/`do_step_up_` 要有人在旁邊」。
→ **先在低處、機器不吊在半空中的狀態下跑一次**。

⚠️ 相關但獨立：`group_seal_ok_` 目前是「4 顆有 2 顆吸住就算 OK」。
那是左右歸屬錯誤時期的權宜之計 —— **歸屬修好後那個前提消失**，
是否改回「每側各 ≥1」**要使用者決定**（待辦表有這條）。

#### 階段 D — 觀察，不是驗收

| # | 看什麼 | 說明 |
|---|---|---|
| ⑤ | driver 回覆驗證上線後的**失敗率** | 壞幀由靜默吞掉變**明確失敗** → 步態中途失敗頻率**可能上升**。🔴 **那是把問題變可見，不是新故障。** 記下基線頻率即可 |
| ⑧ | 上滑台寫入失敗是否被偵測到 | 正常運作時看不到差異。只有真的失敗時才有訊息 |
| ⑨c | 緊急收繩的張力警示 | 超張力會**印警示但不會自動停**（刻意的，見 `b1234ad`）。確認警示有出現即可 |

#### 🔴 這次上機**不驗**重構本身

階段 1~5 的重構已由 `harness/compare.sh` 證明位元組等價，但那套工具有**已知天花板**：
- 覆蓋 9 條預期差異裡的 **5 條**
- **錯誤路徑結構上測不到**（假從站永遠送好幀）——⑤⑧ 兩條就是這一類

→ 所以上機時若出現**清單外**的異常，嫌疑順序是：
**(1) 這 9 條之一** → **(2) harness 沒覆蓋到的路徑** → (3) 重構搬壞。
前兩者的機率高得多。

---

### 0. 進場前確認（🔴 不要跳過）

```
ssh user@192.168.5.17 'who; ss -ltn | grep -E ":(5002|8080)"; ps -eo pid,etime,comm | grep -E "crane|node"'
```

本體同理（`nexuni@192.168.5.26`、埠 `5001`）。
🔴 **判斷程式在不在用 `ss -ltn` 看埠或 `ps -eo comm` 比執行檔名，絕不用 `pkill -f`／`pgrep -f`**
——它會比中執行它的那條 SSH 指令自己（2026-08-27 踩過）。

### 1. 建置（在 Pi 上，另開目錄，不碰現有部署）

```
rsync -a --delete <repo>/{common,config,mechanism,transport,user_lib,Crane_control_PI} user@192.168.5.17:~/bringup/
```
```
ssh user@192.168.5.17 'cd ~/bringup && g++ -std=c++17 -O2 -Icommon -Imechanism -Itransport -Iuser_lib -o crane_control_PI.out Crane_control_PI/main.cpp transport/TCP_client.cpp transport/TCP_server.cpp user_lib/{CLV900_inverter,DSZL_107,DY_500_weight_sensor,PQW_IO_16O_RLY,MH300_inverter,SD76_length_meters,SE3_inverter}.cpp -lpthread'
```

本體（多一個 `app/`，14 個編譯單元、平行編約 25 秒）：
```
rsync -a --delete <repo>/{app,command,common,config,mechanism,transport,user_lib,facade_cleaning_v2} nexuni@192.168.5.26:~/bringup/
```
```
ssh nexuni@192.168.5.26 'cd ~/bringup && mkdir -p obj && printf "%s\n" facade_cleaning_v2/main.cpp app/WASH_ROBOT.cpp app/wash_robot_commands.cpp command/dispatcher.cpp transport/{Serial_port,TCP_client,TCP_server}.cpp user_lib/{DM2J_RS570,DY_500_weight_sensor,FrameAnalyzer,JC_100_METER,PQW_IO_16O_RLY,QX_DO24,WT901BC_TTL,XKC_Y25_RS485,ZDT_motor_control}.cpp | xargs -P4 -I{} sh -c "g++ -std=c++17 -O2 -Iapp -Icommand -Icommon -Imechanism -Itransport -Iuser_lib -c {} -o obj/\$(basename {} .cpp).o" && g++ -o facade_cleaning_v2.out obj/*.o -lpthread'
```

🔴 **第三個目標：`Linux_test`（2026-08-29 補）** —— 它與應用層一樣綁在 `user_lib/*.h` 的
public 簽名上，**上面兩條指令都沒有涵蓋它**。08-28 的 `3c75351` 把 `DM2J_RS570.h` 的
16 個 `void` 改成 `bool`＝跨模組契約改動，**只編前兩支等於只驗到契約的一端**：
```
rsync -a --delete <repo>/Linux_test nexuni@192.168.5.26:~/bringup/
```
```
ssh nexuni@192.168.5.26 'cd ~/bringup && g++ -std=c++17 -O2 -Icommon -Itransport -Iuser_lib -o linux_test.out Linux_test/main.cpp transport/{Serial_port,TCP_client,TCP_server}.cpp user_lib/{PQW_IO_16O_RLY,ZDT_motor_control,DM2J_RS570,WT901BC_TTL,JC_100_METER,XKC_Y25_RS485,SD76_length_meters,ZS_DIO_R_RLY,SE3_inverter,MH300_inverter,QX_DO24}.cpp -lpthread'
```
📌 **假從站測試**（`Linux_test/fake_slaves/`）的建置指令在各 `test_*.cpp` 的檔頭，
且**全程只連 `127.0.0.1`，不碰真 485 匯流排** —— 拿到機器時值得順手跑一輪。

⚠️ **驗建置結果要看產物，不要看管線離開碼**：`g++ … 2>&1 | tail` 的離開碼是 `tail` 的
（2026-08-29 差點誤判）。用 `ls -la` + `md5sum` 確認檔案時間戳與雜湊真的變了。

⚠️ **`~/bringup/` 是刻意跟 `~/projects/` 分開的**：`~/projects/` 是 Visual Studio 遠端建置的落點，
另一位開發者在 `main` 上迭代時會重建並覆蓋它。

### 2. 先跑起來，但不要覆蓋現有部署

🔴 **不要用 `--help` 之類的旗標試用法——這兩支 `main()` 一開頭就連硬體，等於直接啟動**
（2026-08-28 踩過，連上了運轉中的 485 匯流排）。

直接執行 `~/bringup/` 底下那支即可（它會佔用 5002 / 5001，所以必須先確認沒有別的實例在跑）。

### 3. 驗收：`init()` 的逐項輸出

**吊機**應出現五個網關：`.30`／`.31`（SE3 左右）、`.34`（SD76 計米）、`.32`／`.33`（X518 張力）。

**本體**逐項對照：

| 預期輸出 | 對應 |
|---|---|
| `[OK] USR .20 (ZDT) / .22 (sensors+PQW) connected` | 兩個 485 網關 |
| `[OK] DM2J arm rail (slave 14 @ cli_20_)` | 手臂滑台（🔴 **2026-08-28 由 `.22` 搬回 `.20`**——舊表寫 `cli_22_`，照舊表看會把正確判成故障） |
| `[OK] ZDT 5~8` | 4 支吸盤推桿（08-27 才改號） |
| `[OK] JC-100 5~8` | 4 顆真空壓力計 |
| `[OK] PQW slave 12 @ cli_20_ (.20)` | 繼電器（08-27 才搬 bus） |
| `[OK] XKC water level slave 13` | 水位（不探測） |
| `[OK] QX-DO24 PWM slave 9 (presence not probed)` | 🔴 **2026-08-28 已改號 6→9 並解除停用**——舊表寫 `[--] DISABLED`。⚠️ `presence not probed`＝init 不發包，**模組若還插在 USB-485 轉換器上、沒接到 gateway A/B 端子，這裡照樣印 OK**，要到第一個 `pwm set` 才會 timeout |
| `[--] DY-500 slaves 10/11 not installed` | 預期就是未安裝 |

🔴 **`init()` 任何一項失敗就直接 `return`（本專案慣例 `true`＝失敗），一次只會看到最前面那一個問題。**
08-27 才改過吸盤改號與 PQW 搬 bus 兩件事，可能要跑好幾輪才挖得完。

跑的時候帶 `WR_DRIVER_DEBUG=0`，否則 25 個裝置的 hex dump 會把輸出淹掉。

### 4. 驗收判準

🔴 **2026-08-29 起，這輪的目的變了。**
原本是「證明搬家沒有搬壞」，所以判準是**與 baseline 逐字一致**。
改為直接上 `fix/driver-crc` 之後，**逐字一致不再是目標，也不再可能**——
本分支帶著上面整張清單的刻意改動。

**新判準：把上面那張清單當成預期差異表逐條核對。**
- 清單上有的差異 → **預期，打勾**
- 清單上沒有的差異 → 🔴 **停下來查**，那才是真正的訊號

📌 **這正是本專案「政策反轉時，舊斷言會把正確報成故障」那條坑**：
若照舊拿「逐字一致」當判準，本分支會**整片報紅**，而每一條紅都是設計好的。
下面保留的兩段（baseline 比對法、吊機 VFD 例外）仍然有用，但它們現在的角色是
**解釋差異的來源**，不是通過／失敗的門檻。

🔴 **2026-08-28 起，不能再拿 `~/projects/` 的現行 binary 當比對基準。**
那支是對方在 `0d5f6bc` **之前**建的（本體 `~/projects` 停在 08-25），
而本分支已含 `0d5f6bc`。直接比會看到一堆差異，**那些來自對方自己的修正，不是搬家搬壞**——
照舊判準會去查一個根本沒壞的東西。

**正確做法**：比對對象是**同一份原始碼樹的兩個建置**——
`0d5f6bc`（純 main）與本分支，兩者都在 `~/bringup/` 之外的隔離目錄建起來，
`init()` 輸出、`status`／`ping`／`tension` 讀值應逐字一致。
只有這樣「不一致」才唯一地指向搬家。

🔴 **2026-08-28 起「與 baseline 逐字一致」已不再成立於本體**：上滑台換算修正
（`[2026-08-28k]`）是刻意的行為改變 —— 本體 `init()` 會多出
`lead=7.731 cm/rev travel<=48 cm`，且所有上滑台移動距離都會變成真正的公分（約為修正前的 1/7.7）。
**本體要比對等價性，只能拿 `9fa4fe1` 之前的 commit 建。**

🔴 **吊機的例外**：VFD 兩行
```
[OK]   VFD left (SE3)  USR_A slave 1        ← 本分支
[OK]   VFD left (MH300)  USR_A slave 1      ← baseline（寫死，說謊）
```
right 同理，另加兩行 `[WARN] ... init failed` 路徑（正常不會出現）。
⚠️ **「唯一預期會不同」這句話已於 2026-08-29 過期**——那是分兩段時期的說法。現在請照塊三那張表核對。
（2026-08-28 首次比對時吊機是 28 行完全一致、本體只差 IMU 即時讀值。）

（📌 這與本專案「政策反轉時舊斷言會把正確報成故障」是同型：**改了基準線就要連判準一起翻面**。）

### 5. 退場

`~/bringup/` 整個刪掉即可——**全程沒有動過 `~/projects/`**，所以沒有還原步驟。
要正式部署（覆蓋 `~/projects/.../bin/ARM64/Debug/*.out`）是另一個決定，
🔴 **覆蓋前先備份原檔**，否則另一位開發者的建置成果就沒了。

### 上機前的獨立決定：要不要先合併 `origin/main` `6523b54`

`origin/main` 於 **2026-08-29 10:52** 多了一個 commit（Sadie-fang）：
同步步伐 PWM 輸出 + 恢復步伐內建清洗 + **停用所有自動補救**。本分支尚未合併它。

🔴 **合併與否是決定，不是照做**，兩個方向各有代價：

| 選項 | 代價 |
|---|---|
| **先合併再上機** | 一次上兩批未驗證的改動；但避免上機後又要重來一次 |
| **先上本分支、之後再合** | 變數較少、歸因較容易；但對方仍在 `main` 上迭代，債會愈積愈多 |

🔴🔴 **不論選哪個，合併時必查這一條**：`6523b54` 在**應用層**替 `[QX:9] no reply`
加了「最多重試 3 次、間隔 120ms」，而本分支的 `9af86e4` 在 **driver 交易層**
加了重試 + 回讀驗證。**兩邊沒有碰到同幾行，git 不會報衝突**，但合併後會變成
**重試套重試**（最壞 3×N 次），時序也跟著變。
📌 **這正是本專案記過的「合併乾淨 ≠ 語意接得上」**——`sendAndReceiveQuiet` 繞過
`MSG_NOSIGNAL` 那次是同一個機制。

其他要一併確認的：`CH_BRUSH` 15 → 5（本專案 `CLAUDE.md` 的 PQW 表仍寫 CH15＝滾筒刷，
**表要跟著改**，且要確認 CH5 沒有別的用途）／`STEP_CM_MAX` 80 → 100／
吊機 `UP_STOP_TOTAL_KG_DEFAULT` 50 → 70（**這是安全門檻**）。
⚠️ 對方的 commit message 自己寫著「**實機行為全部未驗證**」。

### 尚未決定的一項

`scripts/wr.sh` 仍會開一個 window 跑 `depth_cam_service.py`（深度相機）。攝影機路線
2026-08-27 已永久移除，那個 window 只會失敗，不影響主程式。**要不要註解掉待使用者決定**
（待辦總表已有此條）。

---

## A3. 本機語法檢查（不需要 Pi，幾秒鐘）

📌 **2026-08-29 由 `origin/main` 帶進來、並在本機實測校正過。**
專案沒有 CMake/Makefile，改完 C++ 一向只能等部署到 Pi 才知道編不編得過。
Windows 端的 MSVC `cl /Zs`（只做語法檢查、不產 obj）可以當場驗。

🔴 **對方文件寫的路徑在這台機器是錯的**，已校正兩處：
1. `Visual Studio\2022\Community\...` → **這台是 `18\Community`**（`2022\` 是空的殘留目錄）
2. `/I user_lib` → **本專案已分層**，要 `/I app /I transport /I user_lib`

### 從 WSL 呼叫（`cmd.exe` 不在 PATH，要用絕對路徑）

```
/mnt/c/Windows/System32/cmd.exe /c "D:\Desktop\agent_ai\projects\facade_cleaning_v2\tmp\syncheck.bat user_lib\ZDT_motor_control.cpp"
```

`tmp/syncheck.bat`（🔴 **必須是 CRLF 換行**——LF 會讓 `cmd.exe` 把 `cd /d` 之類的行拆壞，
症狀是「'/d' 不是內部或外部命令」這種看起來毫無關聯的錯誤）：

```
@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d D:\Desktop\agent_ai\projects\facade_cleaning_v2
cl /nologo /utf-8 /std:c++17 /EHsc /Zs /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /FIwinsock2.h /FIws2tcpip.h /I app /I transport /I user_lib %1
```

四個 flag 缺一不可（少了會被大量假錯誤淹沒，看起來像你改壞了）：

| flag | 少了會怎樣 |
|---|---|
| `/utf-8` | MSVC 用 cp950 讀檔 → 中文註解變成一堆 `C2001 常數中包含新行字元` |
| `/FIwinsock2.h /FIws2tcpip.h` | `windows.h` 先於 `winsock2.h` → ~100 個 `sockaddr 重複定義`，**在第一個 include 就爆，根本讀不到你改的碼** |
| `/DNOMINMAX` | `windows.h` 的 `min`/`max` 巨集吃掉 `std::max(...)` → 20+ 個 `C2589` |

### 🔴 判讀方式：比對 baseline 的**錯誤集合**，不是絕對數量

這是 Linux 目標的專案，MSVC 有些地方比 GCC 嚴格。**已知的既有 MSVC-only 錯誤**：
`app/WASH_ROBOT.cpp` 的 `CRANE_METER_SANITY_MAX_CM` 在 lambda 內被 MSVC 要求顯式 capture
（`C3493` ×1 + `C2064` ×2 = **固定 3 個**），GCC/C++17 不需要。**那三個不是你改出來的。**

baseline 要用**同一組 flag、整棵樹**建，才比得準：

```
rm -rf tmp/base && mkdir -p tmp/base && git archive <baseline-commit> | tar -x -C tmp/base
```
然後把 `cd /d` 指到 `...\tmp\base` 再跑一次。

⚠️ **不要用 `find /c "error"` 的數字當判準**：撞到 `C1003 錯誤計數超過 100 停止編譯` 時
數字會被截斷成**假的相等**。要看錯誤**種類與相對位置**；行號會因為新增註解而位移。
⚠️ **行號相同不代表比對有效**——2026-08-29 就遇過兩邊都報 7254/7259，
一度懷疑是把同一個檔編了兩次。**先 `cmp` 確認兩份確實不同、再確認差異起點在錯誤點之後**，
才能說「錯誤集合相同」。（那次是真巧合：合併的第一處改動在 7325 行。）

### 🔴 這條路能證明什麼、不能證明什麼

- ✅ 能證明：**語法、型別、宣告一致性**。回傳型別寫錯、少個分號、簽名不匹配都會抓到
- ❌ **不能**證明：GCC/ARM64 編得過（不同編譯器）、**連結得起來**（`/Zs` 不產 obj 也不 link）、
  **行為正確**。它取代不了 Pi 上的建置，只是把「等部署才知道」的迴圈從幾分鐘縮到幾秒


## B. Web GUI 面板（按鈕即送指令）

| Panel | 按鈕 | 對應指令 |
|---|---|---|
| **auto cycle (washrobot)** | init / attach / detach / arm_sweep / step_down | `init` / `attach` / `detach` / `arm_sweep` / `step_down` |
| | run + 步數輸入 | `run <n>` |
| | pause / resume / STOP (robot) | `pause` / `resume` / `emergency_stop` |
| | status / reset / shutdown | `status` / `reset` / `shutdown` |
| | tilt ON / tilt OFF | `tilt_mode on` / `tilt_mode off` |
| | **↩ 召回回地面** | 先 `home_status` 讀 remaining → modal 確認 → `return_home <cm>` |
| **manual — vacuum** | feet/body/center/all × ON/OFF | `vacuum <g> <on\|off>` |
| **manual — pusher** | feet/body/center × EXTEND/RETRACT | `pusher <g> <extend\|retract>` |
| **manual — DM2J move** | motor 選單 + cm + move | `move <motor> <cm>` |
| **crane** | pay_out / retract + cm 輸入 | `pay_out <cm>` / `retract <cm>` |
| | status / home_status / STOP (crane) | `status` / `home_status` / `stop` |
| | 鋼索歸零（地面起點 / 洗窗起點） | `zero_meters ground` / `zero_meters top` |
| | left/right × pay_out/retract ON/OFF | `pay_out_left on` / `retract_right off` … |
| **🆘 緊急收繩** | 按住 | `retract_left on` + `retract_right on`（放開 → off + `stop`）|
| **raw command** | 自由輸入 | 任何上面未覆蓋的指令 |

---

## C. Raw command 指令集

### C1. Washrobot 接受（`:5001`）

> 🔴 **2026-08-28 由分派器（`facade_cleaning_v2/main.cpp`）逐一對照重寫。**
> 舊版列 18 個指令，其中 **3 個早已 `removed_in_v2`**（`move` / `tilt_mode` /
> `confirm_balance`），而**漏掉約 48 個實際可用的**——包括現在真正在跑的
> `step_down_sync` 同步步伐、整個 `arm_*` 手臂家族、`pwm`、`realign`、`zdt_*`。
> 照舊版操作的人會送出必定失敗的指令，同時不知道能用的東西。
> 📌 **改指令時請一併更新這裡**；分派器現有 81 個分支、其中 **15 個是 `removed_in_v2` 墓碑**。

#### 步伐（最常用）
| 指令 | 說明 |
|---|---|
| `step_down_sync` / `step_up_sync` | **同步步伐**——4 顆吸盤一起動，不依賴分側錨定 |
| `step_down` / `step_up` | **交替步伐**——🔴 依賴左右分側；2026-08-28 才修好歸屬，**尚未實機驗證** |
| `step_down_with_sweep` / `step_up_with_sweep` | 步伐 + 清洗 |
| `step_down_sweep_after_feet` / `step_up_sweep_after_feet` | 清洗時機在伸腳之後 |
| `step_down_sweep_ba` / `step_up_sweep_ba` | 清洗時機：before/after 變體 |
| `run <steps> [cm] [down\|up] [alt\|sync]` | 連續 n 步（`alt`＝交替、`sync`＝同步） |
| `cross_obstacle_down` / `cross_obstacle_up` | 跨越障礙 |
| `pause` / `resume` / `continue` / `skip` | 流程控制 |
| `emergency_stop` | 立即停機 |
| `reset` / `recover` | Error → Idle（需確認現場安全） |

#### 吸附與推桿
| 指令 | 說明 |
|---|---|
| `attach` / `detach` | 吸附 / 脫附 |
| `vacuum <group> <on\|off>` | 真空閥（⚠️ v2 只剩單一閥，左右已合併於 CH1） |
| `pusher <group> <extend\|retract>` | 整組推桿 |
| `zdt_pusher <5..8> <extend\|retract>` | **單支**推桿。⚠️ `extend` 走的是 `disable_seal` **迭代尋封序列**（可推到 ~47994 脈衝／16cm），**不是移動到預設的 36000** |
| `zdt_disable <5..8>` / `zdt_enable <5..8>` | 停用／恢復單支推桿（故障時用） |
| `zdt_zero <right\|left\|all>` | 歸零 |
| `zdt_release_stall` | 解除堵轉 |
| `realign` | 四腳位置重新對齊 |

> 🔴 **slave 編號是 5~8，不是 1~4**（2026-08-27 改號）。實體排列（由上往下看）：
> 右 = {5 上, 7 下}、左 = {6 上, 8 下}。

#### 手臂與清洗
| 指令 | 說明 |
|---|---|
| `arm_init` / `arm_park` / `arm_status` | 手臂校正 / 收回 / 狀態 |
| `arm_deploy <wall_mm> <LEFT\|CENTER\|RIGHT>` | 手臂貼牆 |
| `arm_sweep` | 上滑台掃動一次（開刷、0→17cm→0） |
| `arm_clean_sweep <wall_mm> <rounds>` | 連續清洗 |
| `arm_clean_sweep_dry` | 乾式清洗（不噴水） |
| `brush <on\|off>` / `pump <on\|off>` / `water_pump <on\|off>` / `water_inlet <on\|off>` | 刷子 / 泵浦 / 水路 |
| `water_level` | 水箱水位 |
| `pwm <set\|save\|status>` | 螺旋槳 PWM。`set <ch> <hz> <control> <duty%>`；🔴 **左右螺旋槳共用 CH1**，duty 5%=停 / 10%=全速、鎖 50Hz |

#### 狀態、設定與腳本
| 指令 | 說明 |
|---|---|
| `ping` / `status` | 存活 / 狀態（⚠️ status 的壓力值**看不出是新鮮值還是 timeout 快取**） |
| `get_settings` / `set_setting <key> <value>` / `save_settings` | 執行期設定 |
| `set_first_step <left\|right>` / `set_follower_mode <imu\|meter>` | 步伐參數 |
| `imu_guard <on\|off>` / `imu_zero` | IMU 保護 / 歸零 |
| `crane_attached <on\|off>` / `arm_attached <on\|off>` | 子系統啟用旗標 |
| `save_script <name> <csv>` / `load_script <name>` / `list_scripts` / `delete_script <name>` | 腳本 |
| `run_script [up\|down] [alt\|sync] <csv>` / `run_saved <name> [up\|down] [alt\|sync]` | 執行腳本 |
| `return_home <descent_cm>` | 召回 |
| `init` / `shutdown` | 初始化 / 關閉主程式 |
| `run_depth_avoid` / `depth_avoid_continue <cm>` / `depth_avoid_stop` | 深度避障（⚠️ 相機路線已作廢，這幾個的實際可用性未驗） |

#### ⚰️ 已移除（送出只會得到 `ERR removed_in_v2`，**不要再寫進文件**）
`move`、`tilt_mode`、`confirm_balance`、`wheels`、`wheels_attached`、
`dm2j_group`、`dm2j_zero`、`obstacle_check`、`obstacle_detect`、`obstacle_response`、
`run_avoid`、`balance_calibrate_start`、`balance_calibrate_record`、
`balance_calibrate_abort`、`balance_calibrate_status`

### C2. Crane 接受（`:5002`）

```
pay_out <cm>                   # 雙繩同步放 N cm + 中間管線同步
retract <cm>                   # 雙繩同步收
pay_out_left  <on|off>         # 手動左放繩
pay_out_right <on|off>         # 手動右放繩
retract_left  <on|off>         # 手動左收繩
retract_right <on|off>         # 手動右收繩
middle_set <rpm> <pay|retract|stop>   # 手動中間絞盤變頻器
zero_meters <ground|top>       # 鋼索歸零（地面起點 / 洗窗起點，後者存 home_ground_cm）
home_status                    # 回 `OK home_ground_cm=N left=M right=M middle=M remaining=R`
roll_correct <delta_cm>        # Phase 5 左右鋼索差動（正值=左放右收）
stop                           # 所有繩動作立刻停（繼電器 OFF + 變頻器 stop）
ping                           # watchdog
status                         # 查狀態（三條計米器 + home_ground_cm）
```

### C3. 主動事件（Washrobot 推到 GUI log）

```
EVT state_changed <old> <new>
EVT balance_ask roll=<deg> pitch=<deg>       # 會觸發 GUI modal
EVT watchdog_timeout <peer>                   # peer = crane / web
EVT vacuum_fail <group> retry=<n>
EVT tension_alarm <kind> left=<kg> right=<kg>
EVT imu_emergency balance=<deg>
```

---

## D. 典型流程（對應 motion_flow.md §4）

| Phase | 操作 | 指令序 |
|---|---|---|
| **1. 部署** | 吊機放繩到地面 → 接線 → GUI 連線自檢 → 歸零 → 拉上頂樓 → 再歸零 | 按鈕「鋼索歸零（地面起點）」→ 操作員掛鋼索 → 吊機拉升（手動 retract）→ 按鈕「鋼索歸零（洗窗起點）」 |
| **2. Init** | 收輪 + 推桿伸 + IMU baseline | `init` |
| **3. 吸附** | 人工貼牆 → 中心推桿伸 + 三區閥開 + 驗證 | `attach`（若真空不足回 `ERR attach_vacuum_fail`，人工檢查）|
| **4. 下移** | 自動尺蠖 + 清洗 | `run <n>`（或單步 `step_down`）|
| **5. 平衡校正** | 系統自動觸發 `EVT balance_ask` → 彈 modal | modal 按 Yes → `confirm_balance yes`（僅 Roll） |
| **6. 召回** | 破真空 + 收推桿 + 放繩回地面 | 按鈕「↩ 召回回地面」→ modal 確認（或 raw `return_home <cm>`）|

---

## E. 緊急處置

| 情境 | 操作 |
|---|---|
| washrobot 異常但未失聯 | GUI 按「STOP (robot)」→ `emergency_stop` |
| 吊機異常但未失聯 | GUI 按「STOP (crane)」→ `stop` |
| washrobot 整台失聯（懸空） | GUI 進救援模式（橘 banner）→ 「🆘 按住收繩」把機體拉回頂樓 |
| crane 失聯但 washrobot 活著 | GUI 進紅 banner 模式，washrobot `run`/`step_down` 被鎖；人工現場救援 |
| 全線失聯 | GUI 全紅 banner；人工去頂樓 E-stop + 直接控電箱 |

---

## F. 失聯模式行為對照（motion_flow §8）

| washrobot | crane | 模式 | Banner | 可用區塊 |
|---|---|---|---|---|
| ✅ | ✅ | 正常 | 隱藏 | 全部 |
| ❌ | ✅ | 救援模式 | 橘色「WASHROBOT 失聯」| crane panel + 🆘 緊急收繩 |
| ✅ | ❌ | Crane 失聯 | 紅色「禁止下移」| washrobot panel（但 `run`/`step_down` 會失敗）|
| ❌ | ❌ | 全斷 | 紅色「全線失聯」| 僅 raw command + log |

切換進入失聯時前端**自動送一次 stop** 給仍活的那一側（crane 送 `stop`，washrobot 送 `emergency_stop`）。
