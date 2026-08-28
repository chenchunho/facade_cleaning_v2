# Runbook — 從冷開機到可操控

> 本文件說明如何啟動整套系統、透過 GUI 與 raw command 操控、典型流程與緊急處置。
> 硬體拓撲與指令語意權威：`.claude/motion_flow.md`；本文件只列「怎麼用」。

---

## A. 啟動順序

### 連線資訊（2026-08-27 實測）

兩台 Pi 各有**兩條路**：有線是 bench 正式配置，WiFi 是備援／驗證用。**帳號兩台不一樣。**

| 機器 | hostname | 有線（正式） | WiFi（備援） | 帳號 |
|---|---|---|---|---|
| 洗窗本體 | `washrobot` | `192.168.1.100` | `192.168.5.26` | **`nexuni`** |
| 吊機 | `raspberry-cran` | `192.168.1.10` | `192.168.5.17` | **`user`** |

- 兩台皆 **aarch64 / Debian 13 (trixie) / g++ 14.2**
- 🔴 **帳號不是 `pi`**（2026-08-28 已全檔更正為 `nexuni@` / `user@`）
- 🔴 **吊機有線是 `192.168.1.10` 不是 `.101`**（2026-08-28 全檔更正）。⚠️ `web_backend/server.js` 的 `CRANE_IP` 預設值**仍是 `.101`**（過期），現行程式實際走 `app/WASH_ROBOT.h` 的 `CRANE_IP = "192.168.5.17"`（WiFi）
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
cd ~/facade_cleaning_v2       # repo
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

**路徑覆蓋**：預設用本節下面手動段落寫的 deploy 路徑（`~/<project>/bin/ARM/Release/...`）。若路徑不一樣：

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
cd ~/Crane_control_PI/bin/ARM/Release
./Crane_control_PI
# → 印出 "[OK] command server :5002"

# Terminal 2: Web Backend（Node.js）
cd ~/washrobot_web_backend
node server.js
# → 印出 "[web_backend] listening http://0.0.0.0:8080"
# 或用 systemd unit 背景跑
```

### 2. Washrobot RPi (192.168.1.100)

```bash
ssh nexuni@192.168.1.100
cd ~/facade_cleaning_v2/bin/ARM/Release
./facade_cleaning_v2
# → 印出 "[OK] command server :5001"
# 會自動 lazy connect crane :5002（連不到會 WARN 但不擋 boot）
```

### 3. 瀏覽器

```
http://192.168.1.10:8080
```

頂部 dot 應變綠（washrobot / crane / arm）；banner 隱藏表示主系統正常模式。

---

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

```
init                           # Phase 2：收輪 → 泵浦 ON → 推桿伸 → IMU baseline
attach                         # Phase 3：中心推桿伸 → 三區閥開 → 真空驗證
detach                         # 脫附回 Ready
step_down                      # 單步（腳下移 + 身體下移 + 清洗一次）
run <n>                        # 連續 n 步
pause / resume                 # 暫停 / 恢復
emergency_stop                 # 立即停機（ZDT/DM2J 完成當前動作）
reset                          # Error → Idle（需確認現場安全）
ping                           # watchdog
status                         # 查狀態
vacuum <g> <on|off>            # g = feet / body / center / all
pusher <g> <extend|retract>    # g = feet / body / center
move <motor> <cm>              # motor = left_foot / right_foot / arm
arm_sweep                      # 執行一次上滑台清洗來回
tilt_mode <on|off>             # Phase 5 平衡校正模式（僅 Roll）
confirm_balance <yes|no>       # 回應 EVT balance_ask
return_home <cm>               # Phase 6 召回（吸盤脫附 → 推桿收 → crane 放繩 cm）
shutdown                       # 關閉主程式
```

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
