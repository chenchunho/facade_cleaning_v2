# AI-2:v3 GUI 加「Manual 張力保護 啟用/關閉」按鈕(+ full 兩個新參數)

> 2026-09-15 per user:「manual 張力保護 要有啟用跟關閉按鈕 AI-2 處理」。
> 契約(吊機 C++ + fake_robot)**已由 agent-ai-db 加好並部署到真機**(crane tag 0915c),你只做 v3 GUI。

## 1. 契約(已存在,不要改)

| 方向 | 內容 |
|---|---|
| 送 | `set_hold_guard on` / `set_hold_guard off`(吊機 :5002,走 CRANE bridge) |
| 回 | `OK hold_guard=1` / `OK hold_guard=0`;壞參數 `ERR expected_on_or_off` |
| status | 新欄位 `hold_guard=1|0`(排在 `up_stop_total_kg=` 後面) |
| EVT | 狀態**真的改變**時廣播 `EVT hold_guard on|off`(重複設同值不發) |
| 重啟 | **不持久化,重啟一律回 on**(保護是預設,不是例外) |

語意:`hold_guard` 管的是 **hold 模式**(GUI 的 ▲▼ 拉繩/放繩 = `up|down on|off`、`up_left …`)的張力保護 ——
`up_stop_total_kg` 總和門檻 + 單側 low/high/diff。
- on:超標 → `hold_all_off()` + `EVT tension_total_limit …` / `EVT tension_alarm …`(現行行為)。
- off:**同一組檢查只警示不停**,走緊急收繩那條:`EVT manual_tension_warn kind=… left=… right=… note=not_stopping_operator_decides`,
  恢復時 `EVT manual_tension_clear`。操作員用眼睛決定何時放開。
- 對 `pay_out/retract <cm>`(motion_rope)**沒有影響**,那邊的張力保護是另一套(`tension_max_kg` 等),不歸這個開關。
- 緊急收繩(raw `pay_out_left on` 等)本來就不受保護,也不受此開關影響。

## 2. 要做的

1. Manual 頁吊機區:兩顆按鈕「張力保護:啟用 / 關閉」(或一顆 toggle),**目前狀態要顯示**(讀 `status` 的 `hold_guard`,
   並吃 `EVT hold_guard on|off` 即時翻),off 時整個 hold 區塊給明顯的警示色/文字「張力保護已關閉,超標不會自動停」。
2. off 狀態下收到 `EVT manual_tension_warn` 要醒目顯示 kind/left/right(現在緊急收繩那段應該已有這個顯示,共用即可),
   `manual_tension_clear` 就清掉。
3. 重啟吊機後 GUI 不能還顯示 off:以 `status` 的 `hold_guard` 為準,不要只靠自己記的 state。
4. 順手:作業流程頁起跑參數加兩格(cycle_test `full` 的 key=value 參數,**位置不限**,`server.js` 的 spawn 陣列在
   `[cycles, steps, stepCm, roll, diff]` 後面直接 append 字串即可——這一步要動 `server.js` 一行,可以):
   - `fan=move[:pct]`(預設,只在下行移動時開,pct 預設 7)/ `fan=all[:pct]`(全程同值,預設 6;`all:5` = 不開)
   - `rail=0`(預設:滾筒 0→100、刮刀 100→0)/ `rail=100`(反向;開跑前腳本自己先把滑台移到 100)
   fake_robot 的 mission 模擬不吃這兩個參數(它不 spawn cycle_test),GUI 只要把字串傳出去、並在 log 顯示。

## 3. 驗收(fake_robot 已支援)

```
set_hold_guard off  → OK hold_guard=0 + EVT hold_guard off
status              → … hold_guard=0 …
set_hold_guard on   → OK hold_guard=1 + EVT hold_guard on
set_hold_guard x    → ERR expected_on_or_off
```
`./harness/gui_offline.sh report` 不得出現 `?? not modelled`。

## 4. 回報

改完 `.claude/changelog.md` 記一條,版號照 `scripts/deploy_web.sh` 流程更新,SendMessage 給 `agent-ai-db`。
**不要 commit**(全 repo 目前都未 commit,等 user 說)。
