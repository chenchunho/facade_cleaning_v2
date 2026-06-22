# Easy Crane Test Mode — 用簡易吊車跑 washrobot 自動下洗

> **目的：** 讓 washrobot 的自動循環（`run`/`step_down`）在主吊車尚未到位時，
> 用 `Crane_easy_PI`（簡易吊車，:5003）充當下方放繩機構跑測試。
>
> **權威：** 本文件為測試模式唯一規範。`motion_flow.md` 的正式協定不變；
> 本文件只描述**測試**用的 shim 及其限制。

---

## 1. 適用時機

**✅ 可以用：**
- 主吊車硬體（`Crane_control_PI` 目標機）尚未上線、缺 `DSZL_107` 張力感測 / 中間絞盤變頻器
- 想驗證 washrobot 的 `init → attach → step_down × n → return_home` 邏輯
- 距離 **1~3 公尺以內** 的受控 demo
- 人員**在現場**、緊急收繩鈕手邊

**❌ 不要用：**
- 正式 deploy
- 超過 3 公尺下降
- 需要 Phase 5（平衡校正）自動化
- 需要 Phase 6（自動召回）
- 無人監控

---

## 2. 架構

```
 ┌─ washrobot (.100:5001) ─┐
 │                         │ pay_out <cm>
 │                         ▼
 │               ┌── crane_shim.py (.101:5002) ──┐
 │               │  (偽裝成 Crane_control_PI)      │
 │               │  翻譯 cm → 時間                │
 │               └────────────┬───────────────────┘
 │                            │ down on / (sleep) / down off / stop
 │                            ▼
 │               ┌── Crane_easy_PI (.5.26:5003) ──┐
 └── web_backend ┤  (ZS_DIO 繼電器 + DY500 重量)    │
   (.101:8080)  └──────────────────────────────────┘
```

- **washrobot + web_backend 無感**：都連 `.101:5002`，不知道背後是 shim
- **shim 不碰 washrobot / easy crane 程式** — 純協定轉譯層
- **Crane_control_PI 必須關閉**（同搶 :5002）

---

## 3. 啟動順序

相較 runbook.md §A **只差一步**（把 `./Crane_control_PI` 換成 `crane_shim.py`）：

```bash
# Crane Pi (.101) — Terminal 1
cd ~/washrobot_new_PI/crane_shim
python3 crane_shim.py --rate-down 3.0 --rate-up 3.0
# → [shim] ready :5002

# Crane Pi (.101) — Terminal 2
cd ~/washrobot_web_backend && node server.js

# Easy Crane Pi (.5.26)
cd ~/Crane_easy_PI/bin/ARM/Release && ./Crane_easy_PI

# Washrobot Pi (.100)
cd ~/washrobot_new_PI/bin/ARM/Release && ./washrobot_new_PI

# 瀏覽器
http://192.168.1.101:8080
```

頂部 dot 期望：washrobot 綠 + crane 綠（其實是 shim）+ easy 綠。

---

## 4. 指令對照表（完整）

| washrobot / web 下的指令 | shim 行為 | 回應 |
|---|---|---|
| `ping` | 直接回應（不經 easy）| `OK shim_pong` |
| `pay_out <cm>` | easy `down on` → sleep(cm/rate_down) → `down off` + `stop` | `OK shim down=<cm>cm duration=<s>s rate=<r>cm/s` |
| `retract <cm>` | easy `up on` → sleep(cm/rate_up) → `up off` + `stop` | `OK shim up=<cm>cm ...` |
| `stop` / `emergency_stop` | 中止 motion + easy `stop` | `OK` |
| `status` | 轉發 easy `status`，追加 `shim_mode=1` | `OK weight=... up=... down=... shim_mode=1` |
| `pay_out_left/right <on\|off>` | easy `down <on\|off>` | 轉發 easy 回應 |
| `retract_left/right <on\|off>` | easy `up <on\|off>` | 轉發 easy 回應 |
| `zero_meters <ground\|top>` | no-op | `OK shim_noop` |
| `middle_set <rpm> <pay\|retract\|stop>` | no-op | `OK shim_noop` |
| `home_status` | **拒絕** | `ERR shim_no_home_use_manual_easy_crane` |
| `roll_correct <delta_cm>` | **拒絕** | `ERR shim_no_roll_correct` |
| 其他 | 拒絕 | `ERR unknown_cmd <cmd>` |

### 關鍵機制

| 機制 | 實作 |
|---|---|
| **Easy watchdog 餵食** | motion 中每 500 ms 送 `ping` 給 easy（easy 的 watchdog 2s）|
| **併發 motion** | `motion_lock` 1 個，第二個 motion 等 1 秒拿不到 → `ERR shim_busy` |
| **stop 搶占** | `stop`/`emergency_stop` 不拿 motion_lock，直接設 abort flag + 送 easy `stop`，進行中的 motion 下個 500ms tick 就會看到 flag 結束 |
| **Easy 斷線** | motion 中 ping 失敗 → `ERR easy_link_down_mid_motion`，finally 仍會嘗試送 off + stop |
| **SIGINT / SIGTERM** | shim 關閉前自動送 easy `stop` |
| **ping 不經 easy** | shim 直接回應，避免被 motion 中的 easy_lock 擋住觸發 washrobot crane_watchdog |

---

## 5. 功能落差（vs 主 crane）

| 主 crane 功能 | 測試模式下 |
|---|---|
| 左右繩獨立計米 + 同步放 | ❌ 單繩，無左右差動 |
| 中間絞盤變頻器同步放線 | ❌ 無中間絞盤（水管電線**事前預放**） |
| `home_status` 回報剩餘 cm | ❌ 回 ERR |
| `zero_meters ground/top` 存基準 | ❌ no-op，不存 |
| `roll_correct <delta>` 左右差動 | ❌ 回 ERR（Phase 5 跳過）|
| Phase 6 自動召回 | ❌ 手動按 easy ↓ 下放 |
| 距離精度（encoder 回饋）| ❌ 開環估算 ±10~20% |
| 張力監控 `DSZL_107` | ❌ 無（但 easy DY500 重量有）|
| **shim 監聽 easy EVT** | ❌ shim 開環睡覺不聽 EVT；easy 自我保護觸發（weight_limit / weight_read_fail / watchdog_timeout）時繩**物理會停**，但 shim 照睡完全程回 OK 給 washrobot → 累積位置誤差。實務上 `retract 15cm` 小動作幾乎不觸發，最常見是 DY500 通訊抖動觸發 read_fail。若現場發現「狀態正常但機器沒下到預期位置」→ 查 shim stderr + easy log 的 EVT |

---

## 6. 可用測試流程

### 6a. 單步下洗（推薦起手）

```
init → attach → step_down  →（觀察 1 分鐘）→ detach（人工 + 手動收繩）
```

**預期：**
- `step_down` 一次約 `pay_out 45cm`（STEP_CM 30 + MARGIN 15）+ DM2J feet/body 各 30cm + `retract 15cm`
- shim 每個 pay_out/retract 回 `OK shim ... duration=<s>s`
- 機器實際下降 ~30 cm（目視估）

### 6b. 連續 N 步

```
init → attach → run 3  →（觀察）→ pause →（人工補放水管線）→ resume
```

**注意：** 每步之間 shim 不會補中間管線，水管/電線要事前**預放總長度 ×2 以上**攤好。

### 6c. 手動救援（若 washrobot 崩了）

- Web GUI 上面 crane 那顆 dot 會綠（shim 還在回 ping）
- 按 GUI 左右手動按鈕 → shim 轉發 `down/up on/off` 到 easy → 正常作動
- 或直接開 easy crane panel（:5003 獨立）做物理救援

---

## 7. 安全守則（上機必讀）

| # | 規則 | 原因 |
|---|---|---|
| 1 | **距離限 ≤ 3 m** | 中間水管電線無主動放線，超過預放長度會扯斷 |
| 2 | **人員現場 + 緊急收繩鈕手邊** | DSZL_107 沒上，張力只靠 DY500 重量間接估 |
| 3 | **第一次只跑 `step_down` 一次**，別直接 `run 5` | 驗證 rate_down 估對不對 |
| 4 | **每步之間看 9 顆 JC_100 真空表** | 真空失效 = 機器靠 shim 繩受力，但 shim 精度差 |
| 5 | **`run N` 跑一半覺得下得太快／太慢**，立刻 `pause` → 調 rate → 重啟 shim → `resume` | 開環估算必需校正 |
| 6 | **召回用 easy crane panel 的 ↓ 按鈕手動下放**，不要按「↩ 召回回地面」 | shim `home_status` 回 ERR |
| 7 | **shim 日誌（stderr）要開著**，看 motion 實際時間 vs 指令 cm | 出問題第一手證據 |
| 8 | **結束測試先 `emergency_stop` + 手動接管 easy**，再 Ctrl-C shim | shim 關閉會自動送 easy stop，但保險 |

---

## 8. 開工前 checklist

- [ ] Easy crane Pi 開機、`Crane_easy_PI` 有在跑、GUI 可看到重量讀數
- [ ] `Crane_control_PI` **沒** 在跑（`pgrep Crane_control_PI` 要空）
- [ ] crane_shim.py 用正確 `--rate-*` 啟動
- [ ] web_backend 重啟（確保連到 shim，不是舊的 `Crane_control_PI`）
- [ ] 瀏覽器 3 顆 dot 都綠
- [ ] 水管電線頂樓預放 ≥ 6m（雙倍於下降距離）
- [ ] 緊急收繩按鈕測試（按一下確認 easy 反應）
- [ ] 現場 ≥ 2 人（盯 GUI + 盯現場）

---

## 9. 撤除測試模式 — ⚠️ 必看清單

主吊車硬體到位後，**以下所有臨時改動都要還原**：

### 9a. 程式改動（需重 build + 重 deploy）

| 檔案 | 原值 | 測試模式值 | 還原 | 狀態 |
|---|---|---|---|---|
| `user_lib/WASH_ROBOT.h:125` `CRANE_IP` | `"192.168.1.101"` | `"192.168.5.26"`（bench 網段） | **改回 `"192.168.1.101"`** | ⚠️ **重新設為 .5.26 (2026-05-08)** — bench 用，production 部署前要還原 |
| `user_lib/WASH_ROBOT.h:184` `WATCHDOG_TIMEOUT_MS` | `2000` | `60000` | **改回 2000** | ✅ 已撤除 2026-05-07 |
| `user_lib/WASH_ROBOT.cpp` `read_rope_weight_max_kg_` 走 easy crane shim | DSZL-107 | easy crane DY500 | 改成 `crane_cmd_("tension")` 為 primary | ✅ 已撤除 2026-05-07（保留 easy crane 為 fallback） |
| `user_lib/WASH_ROBOT.cpp` init() 開頭 `bool dbg = true;` | `false`（2026-04-21c 原）/ `true`（2026-04-21c~04-24p 階段）| `true`（env var 可改）| **改回 `bool dbg = false`**；env var 機制保留 | ⚠️ 仍是 test 值 |
| `Crane_easy_PI/main.cpp:319,320` relay / dy500 init `debug` | `false` | `true` | **改回 false** | ⚠️ 仍是 test 值（2026-05-05 後似已 false） |

> 每個改動旁都標了 `[TEST MODE 2026-04-21]` 註解，grep `TEST MODE` 可快速找到所有還原點：
> ```bash
> grep -rn "TEST MODE" user_lib/ Crane_easy_PI/ washrobot_new_PI/
> ```

### 9b. 執行環境改動

1. **停 shim**：`Ctrl-C` 關 shim（或 `systemctl stop crane_shim` 若有裝）
2. **啟正式 `Crane_control_PI`**：`cd ~/Crane_control_PI/bin/ARM/Release && ./Crane_control_PI`
3. **web_backend 重啟**（確保連到 main crane 而非殘留的 shim socket）
4. **移除 IP alias**（若有加）：`sudo ip addr del 192.168.1.101/24 dev eth0`（或重開機自動消失）

### 9c. 功能驗證

撤除後驗證：
- [ ] GUI 「↩ 召回回地面」按鈕能呼叫 `home_status` 並解析 remaining
- [ ] Phase 5 balance modal 按 Yes 能執行 `roll_correct`
- [ ] `zero_meters ground/top` 會實際寫基準（不再是 shim no-op）
- [ ] `step_down` 跑完後，主 crane 的繼電器 + 中間絞盤變頻器都有作動
- [ ] washrobot terminal 不再狂噴 debug hex dump

### 9d. 保留

- `crane_shim/` 資料夾留在 git 裡備用（下次有需要再啟動）
- `.claude/easy_crane_test_mode.md` 本文件保留（下次測試參考）
