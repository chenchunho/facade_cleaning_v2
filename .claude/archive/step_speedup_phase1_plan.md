# Step 加速計畫 — Phase 1（不動 Crane）

> **狀態：** 進行中
> **建立：** 2026-06-05（Sadie）
> **目標：** 單 step 25-30s → 12-15s，不動 crane 架構

---

## 已知 step 時間瓶頸

| 項目 | 每次耗時 | 一 step 出現次數 | 小計 |
|---|---|---|---|
| `VACUUM_RELEASE_WAIT_MS` (timeout=1500ms) | 多在 800-1500ms 間 | 2 | 2-3s |
| `2stage_retract stage1` | **3896ms 固定盲等** ⚠️ | 2 | 7.8s |
| `2stage_retract stage2` | ~1500ms（依距離） | 2 | 3s |
| `disable_seal Phase 1 fast extend` | ~1500ms | 2 | 3s |
| `disable_seal iter loop` | 0-4s × iter（依 seal 速度） | 2 | 2-8s |
| `pqw verify settle` (200ms × ~6) | ~1.2s | 2 | 2.4s |
| `DM2J rail move` | ~3s | 2 | 6s（部分跟 crane 並行）|
| `crane pay_out/retract` | ~5s | 2 | 10s（部分並行）|

**最大殺手 = `2stage_retract stage1` 的 3896ms 固定盲等**。

---

## 加速項清單（按 ROI 排序）

### 🏆 Tier 1: 高 ROI 低風險

#### F1.2 — Realign threshold 降低
- `REALIGN_THRESHOLD_CM` 3.0 → 1.5
- `REALIGN_THRESHOLD_MEAN_CM` 2.0 → 1.0
- **預期省**：3-5s / step
- **原理**：drift 早 trigger → 每次工作量小 → body cup 不再撞 endpoint → disable_seal iter 減少
- **連鎖效益**：影響整個 step 的 disable_seal 速度
- **風險**：低；2026-06-01h fix (Stage 0 non-fatal) 已讓 realign 不易卡

#### F1.3 Step A — Vacuum SEAL threshold 鬆綁
- `VACUUM_SEAL_DEEP_KPA` -60 → -45
- **預期省**：3-5s / step
- **原理**：cup 吸到 -45kPa（~14kg 撐力 / cup）就接受 sealed，不必到 -60
- **配套**：vacuum_check 的 detach threshold 對應放寬到 -40
- **風險**：低；物理上 -45kPa 已足夠撐機體

#### A — 2stage_retract stage1 改 polling exit（**最大塊**）
- 現狀：fixed 3896ms 盲等
- 改：polling cup 位置（達 stage1 endpoint）或 vacuum（p > -10kPa）→ 早 exit
- **預期省**：6-8s / step
- **風險**：cup 未真脫壁就跳 stage2 → ZDT stall
- **Mitigation**：min 800ms 才允許 exit、stage1 stall 退回 fixed delay

#### B — VACUUM_RELEASE_WAIT_MS 進一步壓縮
- 現狀：1500ms（已從 4000 壓到 1500）
- 改：timeout 1500 → 800（vacuum_wait_release_ 已 polling-based）
- **預期省**：~1.4s / step
- **風險**：低，跟 A 同類

#### D — PQW verify settle 動態縮短
- 現狀：200ms（cli_22_ bus contention 防護）
- 改：step_in_progress_=true 時用 50ms（GUI poll 已被鎖住），手動 cmd 仍 200ms
- **預期省**：~1s / step
- **風險**：低；step 期間 cli_22_ 已較空

### 🥈 Tier 2: 中 ROI 中風險

#### E — ZDT 速度提升
- `PUSHER_RPM` 700 → 800、`PUSHER_RPM_RETRACT_FULL` 500 → 600
- **預期省**：~1-2s / step
- **風險**：高 RPM ramp 電流大、stall 機率上升

#### F — disable_seal Phase 1 RPM 提升
- 700 → 800-1000
- **預期省**：~500ms / step
- **風險**：類似 E

#### G — 移除冗餘 verify
- `cmd_recover` vacuum_check 改成只查 anchor group
- attach 內部重複 check 審視
- **預期省**：~1s（recover 不常觸發）
- **風險**：低（不是安全 net）

### 🥉 Tier 3: 高 ROI 高風險（架構改）

#### H — Phase A / Phase B 部份重疊
- body 釋放 + retract 期間 feet rail 提前動
- **預期省**：5-8s / step
- **風險**：高，body cup 還沒脫 wall 就動 rail → 拉脫

#### I — Pipeline 連續 step
- step N 的 Phase B 跟 step N+1 的 Phase A 重疊
- **預期省**：3-5s / step（連跑時）
- **風險**：高，state machine 大改

---

## 執行順序

| 週 | 動作 | 累積節省 |
|---|---|---|
| W1 | **F1.2** → **F1.3 Step A** | 6-10s |
| W1.5 | **B** (vacuum_release polling timeout 縮短) | +1.4s |
| W2 | **A** (stage1 polling exit) | +6-8s |
| W2.5 | **D** (pqw settle dynamic) | +1s |
| W3 | 觀察整體 — 目標 step ~12-15s | (~15s 累積) |
| W4 | 視效果決定 E/F/G | +2-3s |
| W5+ | 達標停；不達標才考慮 H/I | — |

每項改動：commit 一次 + 跑 5-10 step 驗證 + 量時間。退步就 rollback。

---

## 驗證 metric（每項都看）

- **單 step 平均時間**（cmd_step_down → step_done）
- **disable_seal iter 數**（log 數）
- **realign 觸發頻率**（log 數）
- **body cup WALL endpoint 撞牆次數**（log 數）
- **JC100 timeout 次數**（cli_22_ bus 健康度）
- **任何 PausedOnError / stall**（regression 紅旗）
