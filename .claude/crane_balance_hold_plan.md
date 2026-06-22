# Crane Balance Hold 計畫（暫緩）

> **狀態：** 規劃完成、暫不實作
> **建立：** 2026-06-05（Sadie）
> **暫緩理由：** ROI 評估後認為「相對工程量加速幅度不夠大」，先做其他加速項

---

## 目標

讓 crane 在 step / 靜止期間**持續維持機體平衡**，搭配 step 加速設計使用（Phase 2 hybrid 架構）。

機體左右不均（左邊較重），純張力 (DSZL) 控制會讓機體向重邊傾斜；改用**長度同步 (SD76) + IMU 校正**的混合方案。

---

## 設計方案（Y）— SD76 主控 + IMU 慢校正

### 控制階層
```
主 loop（250ms，現有 BAL trim 邏輯）：
  SD76 左右長度差 → SE3 freq 微調

校正 loop（1Hz IMU push from washrobot）：
  IMU roll → 計算 target length offset
  把 offset 灌進主 loop 的 error 計算
```

### 為什麼不用純 DSZL
機體重量不均 → DSZL 一定不平衡 → 用 DSZL 鎖張力會讓機體傾斜

### 為什麼也不用純 SD76
高樓層 / 長鋼索會累積誤差（rope stretch、drum slip、磨損）→ 需要 IMU 校正

### 通訊架構
- washrobot → crane: 1Hz EVT push「imu_status roll=X pitch=Y」
- crane 收到 → 解析 → 存 atomic → balance loop 用
- 失聯偵測：3 秒沒收到 IMU → fallback 到純 SD76（offset=0）

---

## Crane 端現有資產（不用重做）

`Crane_control_PI/main.cpp` 已經有：

| 功能 | 實作位置 |
|---|---|
| SD76 length-based balance loop | `apply_balance_trim()` |
| BALANCE_KP / DEADBAND / CAP_RATIO / TICK_MS | 全 atomic、web GUI 可調 |
| DSZL 安全 monitor | `g_tension_max_kg` / `g_tension_diff_max_kg` |
| Watchdog | idle 2s / hold 2s / motion 10s timeout |
| SE3 dual-side freq control | 已有 |

**現有 balance 只在 motion_rope / hold_loop 中跑**。要新增「靜止持續維持」模式。

---

## 需要新增的東西

### Crane 端

| 類別 | 內容 |
|---|---|
| Globals (atomic) | `g_balance_hold_active`, `g_imu_roll_rad`, `g_imu_pitch_rad`, `g_imu_last_update_ms` |
| Tunables (atomic) | `g_balance_imu_kp` (cm/rad), `g_balance_imu_deadband_rad`, `g_balance_imu_max_offset_cm`, `g_balance_diff_warn_kg`, `g_balance_diff_critical_kg` |
| Dispatch cmds | `imu_status roll=X pitch=Y`, `start_balance_hold <target_kg>`, `stop_balance_hold`, `set_balance_imu_kp <v>` |
| Loop modification | `apply_balance_trim()` 加 offset 參數（IMU 計算） |
| Watchdog | 加 BALANCE_HOLD mode（無 timeout 或心跳機制） |
| State guard | balance_hold active 時拒絕 motion_rope（避免兩 loop 搶 SE3） |

### WASH_ROBOT 端

| 類別 | 內容 |
|---|---|
| 新 thread | `imu_push_loop_` — 1Hz push IMU 到 crane |
| 新 method | `cmd_balance_hold_start(target_kg)` / `cmd_balance_hold_stop()` |

### Web GUI

| 內容 |
|---|
| balance_hold panel：start/stop 按鈕、IMU roll/pitch 即時顯示、IMU stale 警示、目前 offset、左右 SD76、左右 DSZL |
| 即時 input：set_balance_imu_kp / deadband |

---

## 風險矩陣

| 風險 | 嚴重度 | Mitigation |
|---|---|---|
| 跟 motion_rope / hold_loop 衝突 | 🟡 | state guard：balance_hold active 時拒絕 motion cmd；motion cmd 時自動暫停 balance_hold |
| SE3 長期頻繁 setFreqHz | 🟢→🟡 | deadband + was_trimmed latch；現有 BAL trim 已驗證可長跑 |
| IMU 失聯 fallback | 🟡 | 3s stale 偵測 → 退純 SD76 + warning EVT；不直接 abort |
| IMU 噪訊放大 | 🟡 | EWMA 平滑（推 / 收任一端做）+ deadband + 單次 max offset 限制 |
| state machine 變複雜 | 🟡 | 先做 hold 的子模式，不重構 state |
| DSZL 安全觸發 | 🟡 | 分級：>warn (log) / >critical (退 hold_safety) / >emergency (e-stop) |

---

## 工時估算

| 項目 | 工時 |
|---|---|
| crane 暴露持續 balance hold（純 SD76）| 2 天 |
| washrobot IMU push thread | 1 天 |
| crane 接 IMU + offset 整合 | 2 天 |
| web GUI panel | 1-2 天 |
| 實機 stress test + 跟 step 整合 | 3-5 天 |
| **合計** | **~1.5-2 週** |

---

## 開始前的前置決策

1. **DSZL 校正必先做**（30 分鐘 bench，需要標準重物）— 否則任何 tension 邏輯都建在 placeholder 上
2. **整合到 step_down 的時機**：要先有 Phase 2 hybrid 設計才有意義；現在 step 是 cup anchor mode，balance_hold 派不上用場
3. **state machine 重構 vs 局部加 sub-state**：先做局部加 sub-state、夠用再說

---

## 何時重啟這個計畫

- 當 Phase 1（settle / vacuum / realign）已驗證、step 時間打到瓶頸不能再縮
- 當機體左右不均的問題實機看到嚴重影響清洗品質
- 當 DSZL 校好、可以信任張力讀值
- 當有 use case 真的需要「機體靜止 + 維持平衡」的場景（例如清洗時間長、機體會被刷子推歪）

目前以上條件都沒到 — 先做 Phase 1 看效果。
