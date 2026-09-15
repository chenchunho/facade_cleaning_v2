# 狀態機:急停與 Error 的語意(2026-09-15 per user 規劃 + 已實作)

> 起因(per user):「本體處於 Error —— 所有動作指令都會被擋下」→ ①**急停後 Manual 要能操作**
> ②**全部收回後應該回到 Idle 的感覺**。本檔記下拍板後的規則與實作位置。

## 1. 11 個狀態(不變)

| 狀態 | 意思 | 進入 |
|---|---|---|
| Idle | 程式起來、未 init;繼電器全關 | 開機 / `reset` / **急停收回全成功** |
| Ready | init 完成(幫浦開),未吸附 | `init` / `detach` |
| Attached | 四顆吸盤吸著 | `attach` / `recover` |
| Running | 步態或流程進行中 | `step_*` / `run` |
| WaitingConfirm | 等 `confirm_balance` | 平衡詢問 |
| Paused | 使用者暫停 | `pause` |
| PausedOnError | 某子動作失敗,等 `continue`/`skip` | 流程內失敗 |
| Balancing / ReturningHome / Calibrating | 對應流程進行中 | 各自指令 |
| **Error** | 硬故障 / **急停收回中**;自動流程擋下,人工動作放行 | `emergency_stop`、流程硬失敗 |

## 2. 急停的完整時序(as-built)

```
emergency_stop
  ├─ 立即(同步,FAST 路徑):abort 旗標、四顆 ZDT 停轉、經 estop 通道停吊機、
  │                        進水閥關、手臂標記未校正 → set_state_(Error)、estop=detaching
  └─ 背景 emergency_detach_()(~10 s):
       關滾刷 → 關水泵 → 收手臂(en=0 視為已收,跳過)→ 風扇 5%(停)
       → 吸盤閥關 → 等鬆開 → 兩段收腳 → 關真空幫浦 A/B
       ├─ 全部 ok  → estop=done   → **Error 自動轉 Idle**(EVT `emergency_detach done state=idle`)
       └─ 任一失敗 → estop=partial → **留在 Error**(EVT `… partial failed=<項目>`),等人看過
```

- 轉 Idle 用 `compare_exchange`:期間若操作員自己按了 `reset` 或觸發別的流程,**不覆蓋**。
- 回 Idle 後要再 `init` 才會開幫浦——與開機後同一條路,語意一致(Idle = 沒有真空、沒有伸出)。
- status 新欄位 **`estop=none|detaching|done|partial`**,GUI 可直接顯示「收回中 / 已收回 / 部分失敗」。

## 3. Error 中放行什麼(2026-09-15 改)

**判準:Error 擋的是「自動流程」,不是「人的手」。**

| 放行(人工單一動作) | 仍擋(自動流程) |
|---|---|
| `pusher` / `vacuum` / `zdt_home` / `zdt_zero` | `step_down/up*`、`run*`、`attach`、`mission` |
| `rail` / `rail_jog` / `rail_sweep` | `arm_sweep`、`arm_clean_sweep(_dry)` |
| `pwm`(風扇)/ `relay_status` / `brush` / `water_pump` / `water_inlet` | `pump_swap`(輪替自動化) |
| `pump` / `pump a|b` | `balance_*` / `return_home` 之外的編排 |
| `arm_retract` / `arm_park` / `arm_status`(本來就沒閘門) | |
| `crane_goto`(維持 idle/ready;急停後已回 Idle 就能用) | |

實作:把那些函式裡唯一的 `if (cur == State::Error) return state_violation_(cur);` 拿掉,
每處留 `[2026-09-15 per user] Error 放行` 註記(14 處)。

## 4. 兩條出路(不變)

- `reset` → Idle(不碰硬體)。急停全成功時**已自動走完這步**,不需要人按。
- `recover` → 若四顆仍吸著,直接回 Attached(給「還貼在牆上、只是某步失敗」用)。
- `return_home` 在 Error 也可用(自動收臂收腳、關幫浦、放繩到地面)。

## 5. 與計畫書 SAFE 的關係(刻意分岔,不要對齊)

`harness/fake_robot.py` 的 `emergency_stop` 走的是計畫書 §3 的 **SAFE**(階段 3 的 C++ mission
引擎才會有);真機今天走本檔的 **Error + emergency_detach**。GUI 兩種都要能顯示
(script 模式看真機、body 模式看 SAFE)。**不要把其中一邊改成另一邊。**
