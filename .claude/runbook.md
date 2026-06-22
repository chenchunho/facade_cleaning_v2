# Runbook — 從冷開機到可操控

> 本文件說明如何啟動整套系統、透過 GUI 與 raw command 操控、典型流程與緊急處置。
> 硬體拓撲與指令語意權威：`.claude/motion_flow.md`；本文件只列「怎麼用」。

---

## A. 啟動順序

### 0. 一鍵啟動（tmux launcher，bench / 測試用）

每台 Pi 上都有對應的 launcher script，會用 tmux 把該機所有程式各開一個 window：

```bash
# crane Pi (192.168.1.101)
ssh pi@192.168.1.101
cd ~/washrobot_new_PI       # repo
chmod +x scripts/*.sh       # 第一次用要給執行權限
./scripts/crane.sh start    # 開 Crane_control_PI + web_backend + 一個空 shell
./scripts/crane.sh attach   # 進去看 log

# washrobot Pi (192.168.1.100)
ssh pi@192.168.1.100
cd ~/washrobot_new_PI
chmod +x scripts/*.sh
./scripts/wr.sh start       # 開 washrobot_new_PI + frame_capture + 一個空 shell
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
WR_BIN=/path/to/washrobot_new_PI ./scripts/wr.sh start
CRANE_BIN=/path/to/Crane_control_PI WEB_DIR=/path/to/web_backend ./scripts/crane.sh start
```

**測試模式吊車**（crane_shim 取代主吊車）：

```bash
CRANE_BIN="python3 $HOME/washrobot_new_PI/crane_shim/crane_shim.py" \
  ./scripts/crane.sh start
```

下面 1~3 是**手動逐項啟動**的對照版本（看 log 直接、能控制每個程式分開重啟）。launcher 內部就是把這些指令各塞進一個 tmux window。

### 1. Crane RPi (192.168.1.101) — 先啟動

```bash
# SSH 進 crane Pi
ssh pi@192.168.1.101

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

#### 1-alt. 測試模式：用 crane_shim 取代 Crane_control_PI

若主吊車硬體尚未上線、想用簡易吊車（Crane_easy_PI @ .5.26）跑自動下洗測試：

```bash
# Terminal 1 改跑 shim（取代 Crane_control_PI）
cd ~/washrobot_new_PI/crane_shim
python3 crane_shim.py --rate-down 3.0 --rate-up 3.0
# → 印出 "[shim] ready :5002"
# Terminal 2 (web_backend) 照跑不變
```

**⚠️ 限制：** 開環估算放繩時間、無左右差動、無自動召回、距離限 ≤ 3m、人員需現場。完整規範 + 指令對照 + 安全守則見 `.claude/easy_crane_test_mode.md`。

### 2. Washrobot RPi (192.168.1.100)

```bash
ssh pi@192.168.1.100
cd ~/washrobot_new_PI/bin/ARM/Release
./washrobot_new_PI
# → 印出 "[OK] command server :5001"
# 會自動 lazy connect crane :5002（連不到會 WARN 但不擋 boot）
```

### 3. Easy Crane RPi (192.168.5.26) — 獨立簡易吊車（可選）

```bash
ssh nexuni@192.168.5.26
cd ~/Crane_easy_PI/bin/ARM/Release
./Crane_easy_PI
# → 印出 "[OK] easy crane server :5003"
# 跟主系統完全獨立；不啟動也不影響 washrobot / main crane 運作
```

### 4. 瀏覽器

```
http://192.168.1.101:8080
```

頂部三顆 dot 應變綠（washrobot / crane / easy）；banner 隱藏表示主系統正常模式。
easy crane dot 若紅不影響 banner（獨立子系統），僅 easy crane panel 灰化。

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
| **easy crane（獨立）** | ↑ 拉繩（按住）| `up on`（放開 → `up off` + `stop`）+ 每 500ms `ping` 心跳 |
| | ↓ 釋放繩（按住）| `down on`（放開 → `down off` + `stop`）|
| | refresh / STOP | `status` / `stop` |
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

### C2b. Easy Crane 接受（`:5003`，獨立子系統）

```
up   <on|off>               # 拉繩（retract）ON/OFF，press-and-hold 模式
down <on|off>               # 釋放繩（pay out）ON/OFF
stop                        # 兩路繼電器立即 OFF
status                      # 回 `OK weight=<kg> up=0|1 down=0|1 up_stop_kg=<kg> weight_valid=0|1`
ping                        # watchdog 心跳（任何 inbound 都算心跳）
set_up_stop_kg <kg>         # runtime 設定收繩停止門檻（預設 -20，可正可負）
```

**四層防呆：**
1. Server watchdog — 動作中超過 2 秒沒 inbound → 自動 all_off + EVT `watchdog_timeout`
2. UP 重量門檻 — UP 過程 `weight < up_stop_kg` 持續 300ms → 自動停 + EVT `weight_limit`（門檻前端可調）
3. DY500 讀取失敗 — 連續讀失敗超過 500ms 且動作中 → 自動停 + EVT `weight_read_fail`；動作開始前若感測器讀不到則拒絕指令
4. 前端 press-and-hold + 每 500ms `ping` heartbeat（任何網路抖動 2 秒內必觸發 (1)）

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
| easy crane 失聯（動作中）| server watchdog 2 秒內自動 all_off；前端 dot 轉紅、panel 灰化；不影響主系統 |
| easy crane 重量超標 | weight_loop 300ms 內自動 all_off + EVT；前端收到 EVT 把按鈕狀態解除 |

---

## F. 失聯模式行為對照（motion_flow §8）

| washrobot | crane | 模式 | Banner | 可用區塊 |
|---|---|---|---|---|
| ✅ | ✅ | 正常 | 隱藏 | 全部 |
| ❌ | ✅ | 救援模式 | 橘色「WASHROBOT 失聯」| crane panel + 🆘 緊急收繩 |
| ✅ | ❌ | Crane 失聯 | 紅色「禁止下移」| washrobot panel（但 `run`/`step_down` 會失敗）|
| ❌ | ❌ | 全斷 | 紅色「全線失聯」| 僅 raw command + log |

切換進入失聯時前端**自動送一次 stop** 給仍活的那一側（crane 送 `stop`，washrobot 送 `emergency_stop`）。
