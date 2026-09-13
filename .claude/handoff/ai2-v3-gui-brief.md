# ai2-v3-gui-brief.md — 給 AI-2:基於 v2 做 console v3(離線、對假機器)

> 2026-09-13 per user:「你現在有虛擬資料伺服器,我想叫 AI-2 基於 V2 基礎做一個 V3 來看看」。
> 這份是交接。**介面已凍結**在計畫書,假機器已實作,**不需要機器、不需要 Pi**。

## 1. 先讀(順序)

1. `.claude/plans/orchestration_to_cpp_plan.md` **§2–§5** —— 介面契約(mission 指令族 + EVT 格式、SAFE 進入/解除、自檢欄位)與 **§5 作業流程頁的規格**。這是 v3 的設計依據。
2. `.claude/reference/field_procedure_v2.md` §3 / §4.3 —— 現場流程七步與硬性守則(GUI 的順序與閘門從這來)。
3. `harness/README_gui_offline.md` —— 假機器怎麼跑、`report` 怎麼看。

## 2. 起環境(一行)

```bash
./harness/gui_offline.sh            # fake_robot(5001/5002/9527)+ web_backend v2(:8081)
```
v3 另開一個埠,**不動 v2**:
```bash
cd web_backend && WROBOT_IP=127.0.0.1 CRANE_IP=127.0.0.1 ARM_IP=127.0.0.1 HTTP_PORT=8082 \
  PUBLIC_DIR=$PWD/public_v3 HOME=$PWD/../tmp/gui_offline node server.js
```
`server.js` **不要改**(`PUBLIC_DIR` 已可切);v3 = `web_backend/public_v3/`(從 `public_v2/index.html` 複製起手)。

## 3. 做什麼(= 計畫 §5,依優先序)

| # | 項目 | 依據 |
|---|---|---|
| 1 | **作業流程頁**:①連線 ②裝置(`dev_*`)③歸零(`zeroed`)④點動(送 `pay_out_left 2`/`retract_left 2` 回讀 length)‖ **全綠才開放升頂** ‖ ⑤牆高 ⑥`zdt_home feet`(顯示 `zdt_homed_at` 距今)⑦`init`(pumpA/B 回讀)+ 手臂 `INIT`(`init_done`)⑧起跑前置 ‖ **全綠才開放 Mission**。每項:狀態燈 + 一鍵執行 + 驗證回讀(沿用現有「修正」鈕模式) | 計畫 §4、§5;field_procedure §3 |
| 2 | **Mission 改按鈕**:`mission start <steps> <cm> [cycles] [nm] [rail]` / `mission stop` / `pause` / `continue` / `skip`;步內顯示**全靠 `EVT mission …`**(哪一步、`n_seal`、工具/結果、跳過計數、`step_done` 時間欄)。**不再 spawn cycle_test、不再讀 stdout** | 計畫 §2.1–2.2 |
| 3 | **SAFE**:`EVT safe_enter src=… detail=…` → 全頁 banner(擴充現有 `estop-banner`)+ 顯示 src;「解除」鈕送 `safe_clear <reason>`,**只在危險操作 60 s 解鎖窗內可按**;`ERR safe_clear_refused item=…` 要顯示原因;解除後狀態回 `paused` | 計畫 §3.4–3.5 |
| 4 | **Manual 在 SAFE 上鎖**:只留 status / stop / safe_clear;**吊機 raw hold 不鎖**(救援) | 計畫 §5 |
| 5 | 起跑前置改讀 C++ 回覆(`ERR precheck_failed item=…`),不再自己算 | 計畫 §2.3 |

**不做**:任何 C++ / `server.js` / v2 的改動;多列作業;環境感測;聲光。

## 4. 假機器給你的測試鉤(不是真協定,v3 可以放一個「模擬」折疊區)

```
sim roll 6          → 放繩中 roll 超標:第一次 roll_recover,第二次進 SAFE src=roll
sim tension_valid 0 → 放繩中進 SAFE src=tension;safe_clear 會被拒直到 sim tension_valid 1
sim top <cm>        → 設「牆頂」高度,讓 precheck top 過/不過(zero_meters ground 會把 top 設 0)
sim noseal          → 吸盤永遠吸不到 → vac_result n_seal=0 skip_clean=1
sim safe [src]      → 直接進 SAFE
emergency_stop      → SAFE src=user(這是真協定,語意已改:不再是回 idle)
```
一條典型的完整路徑:`init` → `zdt_home feet` → 手臂 `INIT` → `zero_meters ground` → `sim top 0` → `mission start 2 40`
→ 看 EVT 跑完 → `sim tension_valid 0` + `mission start 1 40` → SAFE → `sim tension_valid 1` → `safe_clear ok`。

## 5. 驗收

- `./harness/gui_offline.sh report`:v3 點過一輪後**不得有 `?? not modelled`**(有就是 v3 用了契約外的指令 —— 回來討論,不要自己發明)。
- 作業流程頁的兩道閘門在假機器上真的擋得住(pump OFF 時 Mission 不可按;`zeroed=0` 時升頂不可按)。
- SAFE 三條路徑(tension / roll / user)都會亮 banner、Manual 上鎖、解除回 paused。
- 手機寬度可用(現場是平板)。

## 6. 回報方式

改完在 `.claude/changelog.md` 記一條(≤40 行),`work_log.md` 待辦總表若有新發現的契約缺口就加一列標 `AI-2 v3`,然後 SendMessage 給 `agent-ai-da`。
契約要改(例如 EVT 少一個欄位)**先問**,不要直接改 fake_robot —— 那份要跟計畫書與未來的 C++ 三方一致。
