# AI-2:v3 —— 側欄順序、Mission 頁重排(依 `ai2-mission-relayout.png`)、四張卡移除、兩張搬 Dashboard

> 2026-09-17 早 per user。純 GUI,契約不動。草圖 `ai2-mission-relayout.png`(user 手繪 `Mission.png`)。

## 1. 左側選單順序

`Dashboard` → `Mission` → `Manual` → `Setting`(現在是 Mission 在最前)。預設開哪一頁不指定,照你判斷(建議 Dashboard)。

## 2. Mission 頁重排 —— 照草圖

```
┌ 參數設定 ──────────────────┐      ┌────────┐
│ 單步距離 / 步數 / 週期數     │      │  存檔   │
│ 風扇                        │      ├────────┤
│ 滑台                        │      │開始/停止│
│ 手臂壓力 / 乾掃              │      ├────────┤
│ 中止門檻                    │      │  急停   │
└────────────────────────────┘      └────────┘
```

- 左欄「參數設定」一張卡,**五列、順序照上**:① 單步距離/步數/週期數 ② 風扇 ③ 滑台 ④ 手臂壓力/乾掃 ⑤ 中止門檻。
  現有五個輸入群(`fan=` / `rail=a-b` / `arm_nm`+`dry` / `roll_trip`+`diff_trip` / cycles+steps+step_cm)**內容與參數不變,只是排進這五列**。
  「頂端高度」那列(server 帶 `FCV_TOP_CM`,不填)不在草圖上 —— 收進小字或拿掉,你判斷。
- 右欄三顆大鈕,**上下排**:**存檔**(= 現在的 defaults 持久化,`mission_params.json`)、**開始／停止**(一顆,依狀態切換文字:閒置=開始、執行中=停止作業)、**急停**(= 昨天改名的「緊急脫離」,紅、隔開、確認)。
  ⚠️ 昨天 `ai2-stop-vs-estop.md` 的「停止作業／緊急脫離」用詞與二次確認**照舊**,只是排到右欄。
- 前置七項的摘要列 / 展開卡:草圖沒畫。**保留但收成一行摘要**放在參數設定卡上方(它是開始的閘門,不能拿掉),你判斷放法。

## 3. 從 Mission 頁**移除**(整張卡,含 JS 輪詢)

| 卡 | 現在位置 | 處置 |
|---|---|---|
| 即時監看 | `index.html:900` | 移除 |
| 致動器 · 指令 ↔ 回讀 | `:918` | 移除 |
| 吸盤 p5–p8 | `:932` | 移除(Dashboard 本來就有壓力) |
| 單步時間基準 | `:940` | 移除 |

移除時順手把只有它們在用的輪詢/繪圖函式一起清,跑 `check_console.js` 確認沒有寫到不存在的元素。

## 4. 從 Mission 頁**搬到 Dashboard**

| 卡 | 現在位置 | 搬去 |
|---|---|---|
| 腳本輸出 `cycle_test.py` | `:828` | Dashboard(任務 stdout ring,執行中要看得到) |
| 每步時間 | `:889` | Dashboard(與腳本輸出相鄰) |

搬過去後 Mission 頁執行中區塊只剩:狀態列(週期/步/高度)+ 三顆鈕。任務進行中的細節到 Dashboard 看。

## 5. 驗收

- `check_console.js` ①②③④ 全綠;`gui_v3_check.js` 全過(移除的卡對應的 chk 一併移除或改指 Dashboard);`gui_offline.sh report` 0 not modelled。
- 用假機器跑一趟 mission start → 停止作業 → 緊急脫離,確認三顆鈕的行為與昨天一致、Dashboard 看得到 stdout 與每步時間。

## 6. 回報

版號照 `deploy_web.sh`,changelog 記一條,SendMessage `agent-ai-db`。**不要 commit**。
