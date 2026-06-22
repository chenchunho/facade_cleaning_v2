# SE3 Mode 6 (Hybrid Modbus + GPIO Relay) Migration Plan

**Status:** 提案 / 未開工
**Created:** 2026-05-14 (Sadie + Claude)
**Goal:** 把 SE3-210 變頻器的 RUN/STOP 控制從 Modbus H1001 路徑遷移到 STF/STR digital terminal（GPIO + relay 驅動），消除整類 firmware state machine race / bus contention 問題。

---

## 1. 為什麼要做（Root cause 與 short-term fix 的限制）

### 1.1 共通病根

所有 short-term workaround 本質都在跟 SE3 firmware 內部 state machine 賽跑：

| 故障模式 | 觸發條件 | 現有 workaround |
|---|---|---|
| stop ramp / DC brake 期間拒收 H1001 | motion_rope stop → fine_adjust 立即發 RUN | 1500ms settle + reliable_run_one 6x200ms（2026-05-14） |
| reversal lockout 拒收 H1001 | STF stop 後立刻 STR（或反向） | correction 反向時 +2s settle（2026-05-13q） |
| CU mode latch glitch | ensureCuMode_ 後 H1001 馬上下發 | 150ms latch wait |
| watchdog deadlock | run cmd 失敗→寫 H1000=0→firmware 拒（馬達跑時不能改 mode） | freeze→stop（2026-05-13） |
| keepalive 與 critical write 撞 | SE3-keepalive 跟 setFreq/run 在 mutex 上排隊 | keepalive 1Hz、interval 500ms |
| RS485 bus contention | meter_loop 50ms poll 把 SE3 寫排隊 | METER_POLL_MS_MOTION 50→150ms（2026-05-14） |

每個都「壓下去一個冒出另一個」，因為共用一條 root cause：**用 Modbus RTU 控制 time-critical 的 RUN/STOP**。

### 1.2 為什麼 Modbus RTU 不適合 RUN/STOP

- Modbus RTU 是 polling-based serial protocol，沒有 hard real-time 保證
- RS485 bus 序列化每個 transaction：~30-100ms per round-trip
- USR-TCP232 gateway 加一層 forwarding latency（10-30ms 變動）
- SE3 firmware 內部對 H1001 寫入有多個 state machine 過渡，總拒收窗從 100ms 到數秒不等
- L/R 兩台變頻器要同步啟動 → 兩個獨立 Modbus 命令，無法真正 simultaneous（即使 thread 並發 fire）

### 1.3 Mode 6 解決的部分

| | Mode 3（現況）| Mode 6（提案）|
|---|---|---|
| Frequency | H1002 Modbus | H1002 Modbus（不變） |
| RUN/STOP | H1001 Modbus | **STF/STR digital terminal** |
| L/R 同步精度 | ~30-100ms（bus 序列化） | **微秒級**（GPIO 同時 toggle） |
| State machine 拒收 RUN | 常態問題 | terminal input 不走 H1001 path，幾乎沒有 |
| Reversal lockout | 1-2s（Modbus path） | 仍存在但短得多（terminal path） |
| Bus contention 影響 RUN | ✓ 會造成 fail | ✗ 完全不走 bus |
| Bus contention 影響 setFreq | ✓ | ✓（但不 time-critical，可背景 retry）|
| Watchdog deadlock | 存在 | 消失（沒有 H1001 寫了）|

---

## 2. 硬體改動

### 2.1 BOM

| 項目 | 規格 | 數量 | 估價 |
|---|---|---|---|
| 4CH relay module | 5V 觸發 / 24V switching / opto-isolated | 1 | NT$150-300 |
| Dupont 母對母線 | 20cm | 8 | 已有 |
| 多芯線 (24V STF/STR 信號) | 0.5mm² 雙絞 | ~2m | 已有 |
| 端子台螺絲 / 套管 | M3 | - | 已有 |

### 2.2 Wiring

```
Crane RPi GPIO ──→ 4CH relay module ──→ SE3 STF/STR 端子
  GPIO_L_STF ─→ Relay1 (NO contact) ─→ SE3_left.STF terminal
  GPIO_L_STR ─→ Relay2 (NO contact) ─→ SE3_left.STR terminal
  GPIO_R_STF ─→ Relay3 (NO contact) ─→ SE3_right.STF terminal
  GPIO_R_STR ─→ Relay4 (NO contact) ─→ SE3_right.STR terminal

Common:
  SE3.SD (digital input common) ─→ Relay COM (per channel)
                                   或 SE3.PC (24V source) 視 SE3 內部接法
```

**選 GPIO pin（待 Jim 確認 RPi 5 可用 pin）：**
- 建議用 BCM 5/6/13/19（PWM 不衝突、預設 input pull-up）
- 或用 GPIO header 上 BCM 17/27/22/23

### 2.3 Interlock

- **軟體 interlock**：同一台 SE3 的 STF + STR 不能同時 ON
  - GPIO write 順序：先寫 OFF 那條 → 等 1 GPIO tick (~1ms) → 寫 ON 那條
- **SE3 firmware 自身保護**：兩腳同時 ON 會被 SE3 拒收（不會燒），但會 fault report，所以軟體要正確序列化
- **物理 interlock（可選）**：relay module 本身常有 latching interlock，但軟體已足夠

---

## 3. SE3 panel 改動（兩台都要）

| 參數 | 現值 (Mode 3) | 新值 (Mode 6) | 說明 |
|---|---|---|---|
| **P.79** | 3 | **6** | 操作模式：混合 3 = Modbus 頻率 + STF/STR 端子 RUN |
| P.55-57 (DC brake) | 已設 | **保留** | 重物 hold（之前 2026-05-09 設好） |
| P.7 / P.8 (acc/dec) | 已設 | **保留**（左右仍要對齊）| |
| P.5 (multi-speed) | 0 | **0 保持** | 確保 H1002 是頻率源（>0 會走多段速覆蓋 H1002）|
| 11-00~02 (通訊) | 8N1, 19200bps | **保留** | setFreq 仍走 Modbus |
| 07-09 (通訊 timeout) | 2s | **保留** | setFreq fail-safe |
| 07-10 (timeout 行為) | 1（無保護）| **保留** | 仍需 keepalive 防靜默（2026-05-13）|

**改 P.79 前須做：**
1. 先把馬達停下、變頻器 OPT 解除
2. 從 P.79=3 退到 P.79=2（外部）→ 再進 P.79=6（防 latch 卡住）
3. 改完手動測：寫 H1002=10Hz 不會啟動馬達，閉合 STF 端子才啟動

---

## 4. 軟體改動（`Crane_control_PI/main.cpp` 與 `user_lib/`）

### 4.1 新增（Jim 範圍）

`user_lib/GPIO_relay.h/.cpp`：

```cpp
class GPIO_relay {
public:
    // Init with GPIO chip + line numbers (sysfs or libgpiod)
    bool init(int chip, int line_stf, int line_str, const std::string& tag);

    // Set direction. Internally: drop reverse first, wait, raise forward.
    bool set_pay_out();   // STR ON, STF OFF
    bool set_retract();   // STF ON, STR OFF
    bool stop();          // both OFF

    // Atomic state read
    bool is_running() const;
    bool current_dir_pay_out() const;

private:
    int _line_stf, _line_str;
    std::string _log_tag;
    bool debug_mode = false;
    // ...
};
```

實作要點：
- 用 libgpiod (`<gpiod.h>`) — RPi 5 上 pigpio 不能用
- 切換方向時先 OFF 反向 → `usleep(1000)` → ON 正向（軟體 interlock）
- 不持有 process-level GPIO state — 每個 `set_*` 都直接寫，呼叫端不需要追蹤狀態

### 4.2 main.cpp 刪除

| 區塊 | 行數估計（現況）| 動作 |
|---|---|---|
| `SE3_DIR_PAY_OUT` / `SE3_DIR_RETRACT` macro | ~5 | 刪除 |
| `reliable_run_one` | ~15 | 刪除（被 GPIO 路徑取代）|
| `reliable_stop_one` 內部對 H1001 stop 的呼叫 | ~10 | 改呼叫 GPIO stop |
| `dual_se3_sync_start` Phase B（並發 H1001 寫）| ~30 | 改一次 GPIO write 同時 latch L+R |
| watchdog deadlock workaround（H1001 fail → H1000=0）| ~20 | 整段刪 |
| reversal lockout 2s settle | ~10 | 縮短到 500ms（保險）|
| fine_adjust 進場 1500ms settle | ~5 | 縮短到 500ms（保險）|

### 4.3 main.cpp 新增

```cpp
GPIO_relay gpio_left, gpio_right;

bool gpio_sync_run(bool L_pay_out, bool R_pay_out) {
    // Same-direction: one shot
    if (L_pay_out == R_pay_out) {
        return L_pay_out ? (gpio_left.set_pay_out() || gpio_right.set_pay_out())
                         : (gpio_left.set_retract() || gpio_right.set_retract());
    }
    // Opposite-direction: drop both reverse first, then raise both forward
    // (interlock at hardware level — both STF/STR briefly OFF guaranteed)
    return false; // detail TBD
}

bool gpio_sync_stop() {
    bool eL = gpio_left.stop();
    bool eR = gpio_right.stop();
    return eL || eR;
}
```

`motion_rope` / `fine_adjust` / `cmd_stop` / `cmd_pay_out` / `cmd_retract` 的 SE3 RUN/STOP 路徑全部改呼叫 `gpio_*`。

### 4.4 main.cpp 保留

- `reliable_setfreq_one`（H1002 Modbus，不 time-critical，仍可背景 retry）
- 所有 status read（H1001 read 仍可用，只是不再寫）
- `meter_loop` / SD76 / CLV900 不動
- Keepalive：仍需 1Hz 防 SE3 通訊 timeout（07-09 / 07-10），但 keepalive 改成只讀 status，不寫 H1001

---

## 5. 工作量估算

| Phase | 內容 | 負責 | 估時 |
|---|---|---|---|
| 1 | 一台 SE3 panel 設 P.79=6，手動跳線 STF/STR 驗證馬達動 + Modbus 寫 H1002 確認頻率切換 | Sadie | 半天 |
| 2 | 採購 4CH relay module、Jim 寫 `GPIO_relay` driver class + Linux_test 單元測試 | Jim | 1-2 天 |
| 3 | 接線（左右各 2 路、共 4 條） + interlock 驗證 | Sadie | 半天 |
| 4 | Sadie 改 `Crane_control_PI/main.cpp`：dual_se3_sync_start / motion_rope / fine_adjust / cmd_stop / cmd_pay_out / cmd_retract 全部 H1001 路徑換 GPIO | Sadie | 2-3 天 |
| 5 | 移除 watchdog / reliable_run retry / settle 等 workaround 死碼 | Sadie | 半天 |
| 6 | Bench 驗證 sync 精度（L/R 同時啟動誤差）+ 各種錯誤路徑 + DC brake 仍正常 hold | Sadie | 2-3 天 |

**總計：約 2 週**（1 週開發 + 1 週驗證）。

---

## 6. 風險與緩解

| 風險 | 機率 | 影響 | 緩解 |
|---|---|---|---|
| RPi 5 GPIO library 選擇（libgpiod 版本相容性）| 中 | 中 | Jim 在 Phase 2 確認 libgpiod 版本，必要時改用 sysfs `/dev/gpiochip0` direct write |
| Relay 機械壽命 | 低 | 低 | 一般 10^5 動作，每洗窗 ~50 次 RUN/STOP → 2000 次洗窗 = 5-10 年 |
| STF/STR interlock race（GPIO 寫順序錯誤）| 低 | 中 | 軟體序列化 + SE3 firmware 內建保護（fault report 不損硬體）|
| Mode 6 下 Modbus setFreq 不被執行 | 低 | 高 | Phase 1 手動驗證階段確認；P.5 多段速必為 0 |
| 反向 lockout 仍存在但更短，仍需 settle | 中 | 低 | Phase 6 量測實際 lockout 時間，調整 GPIO 切方向時的 wait |
| 採購交期 | 低 | 低 | 4CH relay module 一般庫存品，1-2 天到貨 |

---

## 7. 不在本計畫內（明確 out of scope）

- **替換 SE3 為其他變頻器**（EtherCAT/CANopen drives）：成本太高，本計畫先不考慮
- **Per-SE3 dedicated RS485 gateway**（拓樸分離）：解 bus contention 一半，但不解 firmware state machine 問題，且要新採購 USR + 重接線
- **替換鋼索吊車為其他升降機制**：超出本計畫範圍
- **改 RPi 為 PLC**：硬實時 OS 改造工程量過大

---

## 8. Migration 完成後的 follow-up

- 更新 `CLAUDE.md` SE3_inverter 描述（Mode 3 → Mode 6）
- 更新 `.claude/motion_flow.md` 控制路徑說明
- 更新 `.claude/summaries/SE3_INVERTER_MODBUS_SUMMARY.md` 標註 H1001 不再寫
- 移除 `.claude/work_log.md` 內已過時的 H1001 fail debug 記錄（保留歷史，加 deprecated 標籤）

---

## 9. 開工前 checklist

- [ ] Sadie 確認 bench 上的 short-term fix 穩定（L/R fail rate ≤1/30s @ 50 cycle）
- [ ] Sadie 在 mailbox / 直接溝通跟 Jim 確認 `GPIO_relay` 介面細節
- [ ] Jim 確認 RPi 5 上 libgpiod 可用版本
- [ ] 採購 4CH relay module 並到貨
- [ ] 預留 2 週連續開發 + 驗證時段（避免跟 production deploy 重疊）

---

**附註：** 這份計畫提案於 2026-05-14，short-term fix（reliable_run_one 6x200ms + meter_loop 150ms）部署前完成。如果 short-term fix 已能穩定 bench 到滿意程度，本計畫可延後啟動。
