# AI-2:v3 Manual「手臂」小卡重做(per user 2026-09-17)

> user 原話:「manual 手臂小卡應該只會有壓上、收回、跟壓上距離跟力道」+「點壓上後顯示目前的力跟估測牆面距離,
> 可以即時修改壓上的力道按套用比較直覺」+「要防呆不超過 7 NM」。
> 契約(本體 + 手臂 + fake_robot)已改好;**真機部署要等 user 放行(兩台要重啟,本體重啟會自動脫離)**,我部署完會再通知。

## 契約

| 指令(送本體 :5001) | 回覆 | 說明 |
|---|---|---|
| `arm_deploy_f <nm> <LEFT\|RIGHT> [dist_mm]` | `OK tau=2.39 target=3.0 theta=0.5991 cmd=… contact=… kp_eff=… iters=… ms=…` / `WARN …`(壓上了但沒收斂到 ±1)/ `ERR …` | **壓上**。`dist_mm`(122..444,機身到玻璃)= 距離上限:壓力/距離**先到先停**;距離先到回 **`OK stopped=distance tau=… theta=…`**(停在那裡、接受當下壓力,不算失敗)。不給 = 不限距離 |
| `arm_force <nm>` | 同上格式 + `wall_mm=` | **套用**:已壓在牆上時只跑收斂、不重新尋觸(~1–2 s)。未接觸回 `ERR SETFORCE: not_in_contact … use DEPLOY_F first` |
| `arm_retract` | `OK arm_retract pos=…` | **收回**(M1 → 0,保持使能) |
| 防呆 | `ERR target_nm_exceeds_max_7` / 手臂端 `ERR DEPLOY_F: target_nm exceeds max 7 Nm` | 壓上與套用都擋 >7 |
| 手臂 STATUS(:9527)`[M1]` | 新欄位 **`wall_mm=`**(θ 幾何式估的機身–玻璃距離,mm) | 即時顯示用;`tau=` 就是目前的力 |

## 卡片(只剩這些)

```
┌ 手臂 ─────────────────────────────┐
│ 力道 [ 3.0 ] N·m   距離上限 [ 空 ] mm │   ← 距離空白=不限;≤7 前端也擋
│ [ 壓上 ]  [ 收回 ]                   │
│ 目前:力 2.39 N·m · 估測牆距 231 mm  │   ← 讀 arm STATUS 每 0.5–1 s(壓上後才輪詢,收回後停)
│ 力道 [ 4.0 ] [ 套用 ]                │   ← 送 arm_force;未壓上時灰
└─────────────────────────────────┘
```
- 工具槽選擇**不放**這張卡(滾筒是預設;要切刀用推桿卡旁邊或不提供,你判斷);壓上一律 `RIGHT`。
- `WARN` 回覆算壓上(顯示黃);`OK stopped=distance` 顯示「距離先到,停在 X mm、力 Y」。
- 收回鈕在任何時候都可按。移除舊卡上的其他按鈕(INIT/PARK/滑台掃描/M2 槽/raw)。

## 驗收
fake:`arm_deploy_f 3 RIGHT` → OK;`arm_force 5` → OK tau=5.00 wall_mm=230;`arm_force 8` → ERR …max_7;`arm_deploy_f 3 RIGHT 200` → `OK stopped=distance`;
未壓上 `arm_force 3` → ERR not_in_contact。check_console 全綠、gui_v3_check 全過、report 0。不 commit,完成回我。
