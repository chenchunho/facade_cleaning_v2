# MH300 Inverter (台達 MH300) — Commissioning 參數與 SE3 差異摘要

Source: `.claude/mh300_migration_plan.md` Phase 0 / Phase 3（keypad 值經手冊確認）

> 📌 **2026-08-31 建立。** 這份的內容原本**只存在於 `mh300_migration_plan.md` 裡**，
> 是**唯一副本**（待辦總表記為 🔴）。計畫檔完成後會被封存甚至清除，而
> **keypad 參數是實體換機時唯一能靠的東西**——面板設錯就完全通不了訊，
> 而且那時候 log 上只會看到「連不上」，看不出是參數問題。
> → 移到 `summaries/`（本專案裝置協定摘要的權威位置）。
>
> ⚠️ **現況：bench 仍在跑 SE3**（`Crane_control_PI/main.cpp` `#define CRANE_VFD_IS_SE3 1`）。
> 本檔是**遷移時要用**的資料，不是目前生效的設定。SE3 的對應資料見
> `SE3_INVERTER_MODBUS_SUMMARY.md`。

## Keypad Commissioning 參數（兩台都要設）

| 參數 | 值 | 說明 |
|---|---|---|
| **09-00** | **1 / 2** | 站號（對齊 `se3_left=1` / `se3_right=2`）|
| **09-01** | **9.6** | baud，單位 kbps —— **填數值本身**（不是代碼）|
| **09-04** | **12** | 8N1 RTU —— 配合與 SD76 共用同一條 bus 的 8N1 |
| **00-20** | **1** | 頻率來源 = RS-485 |
| **00-21** | **2** | 運轉來源 = RS-485 |
| 07-00~04 | 依載重調 | DC brake / 煞車截波（配 **BR300W070-S** 制動電阻）|
| **01-12 / 01-13** | 依現況 | 加 / 減速時間 —— 🔴 **左右必須對齊，否則不同步停車** |

- 制動電阻 **BR300W070-S** 接線；bus 接到原 SE3 的 USR gateway（沿用既有拓樸）
- `00-20` / `00-21` 也可用 driver 的 `configureModbusControl()` 一次性寫入
  （寫 EEPROM，**只跑一次**）

## 🔴 與 SE3 的關鍵邏輯差異（換機時非機械替換）

**急停 recovery —— 必改。**

| | SE3 | MH300 |
|---|---|---|
| run 命令暫存器 | `0x1001` | `0x2000` |
| B.B / MRS（base block）| **與 run 同一個 reg** `0x1001` | **獨立在 `0x2002`** |

現況慣用法是「`emergencyStop()` → `stopDecel()` 清 MRS」——**在 SE3 成立**（同 reg）。
🔴 **在 MH300 不成立**：`stopDecel` 清不掉 `0x2002` 的 B.B → **急停後馬達被 base-block 卡死**。
→ 換機時這些 callsite 要改成 **`clearAlarm()`**（driver 內部會寫 `0x2002=0` 解 B.B）。
📌 遷移計畫已列出需稽核的 callsite（`mh300_migration_plan.md` Phase 3）。

## 相關檔案

- Driver：`user_lib/MH300_inverter.{h,cpp}`（已存在，未在 vcxproj 生效）
- 型號切換：`Crane_control_PI/main.cpp` 的 `#define CRANE_VFD_IS_SE3`
- 完整遷移步驟（Phase 0~4）：`.claude/mh300_migration_plan.md`
