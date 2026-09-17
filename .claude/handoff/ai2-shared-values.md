# AI-2:Manual / Mission「共用值」—— GUI 端(per user 2026-09-17)

> 模型(per user 拍板):**Manual 與 Mission 共用同一份機器狀態,真值在本體/吊機。Manual 調什麼,Mission 就用什麼;
> Mission 起跑時把安全項強制開啟。** 不另做「Mission 自己的一份參數」。計畫全文 `.claude/plans/mission_manual_isolation.md`。
> 本體/吊機/腳本/unit 都已改好部署,GUI 只做下面三件。

## 契約(已部署,fake_robot 已鏡像)

| 項目 | 內容 |
|---|---|
| 本體 `set_pusher_rpm <伸> [收]` | 共用執行期 RPM。`0`=不改該項;範圍 50..1000。回 `OK pusher_rpm=450 pusher_rpm_retract=380`;壞值 `ERR range:50..1000 (0=keep)`;會廣播 `EVT pusher_rpm <伸> <收>` |
| 本體 status `pusher_rpm=` / `pusher_rpm_retract=` | **現在是執行期值**(以前是編譯常數),`set_pusher_rpm` 改了就變 |
| 持久化 | 本體 `~/run/body_settings.txt`(推桿 RPM)、吊機 `~/run/crane_settings.txt`(五個張力/繩長差門檻),程式在 `set_*` 時自動寫,開機 unit 自動回放 ⇒ **重啟不再回編譯預設**。GUI 不用做「存為開機預設」鈕 |
| 腳本起跑(cycle_test full) | 強制 `set_hold_guard on`、`set_level_auto on`、`crane_attached on`、`zdt_power all on`;已吸附或 `arm_ready≠1` 拒跑並印原因;log 頭印「共用值快照」(zdt_skip / RPM / 五門檻 / level_auto / k / hold_guard)。`FCV_PREFLIGHT_ONLY=1` 只做檢查不起跑 |

## 要做的

1. **Manual 推桿卡的 RPM 欄改成「設共用值」**:伸/收兩格 value 照舊讀 status;人改完(change/blur)送 `set_pusher_rpm <伸> <收>`,
   之後所有單次指令(伸 raw / 尋封伸 / 收)**不再帶 rpm 參數**(本體用共用值)。`EVT pusher_rpm` 來了就更新兩格(別人改的也看得到)。
2. **Mission 頁:前置摘要旁加一行唯讀「共用值」**,讀 status(本體 + 吊機):
   `推桿含入 5,6,7,8(跳過 —) · RPM 伸 400 / 收 400 · 張力 max 100 / diff 50 / 回程停 75 kg · 左右差 10 cm · 起跑強制:張力保護、level_auto、推桿全使能`
   - `zdt_skip` 有值時整行標黃並寫「推桿 7 被 Manual 排除,整組指令會跳過它」——這是 user 最在意的「Manual 留了什麼」。
   - RPM 那兩格在 Mission 頁**可改**,送同一個 `set_pusher_rpm`(共用,兩頁互通);其餘唯讀,要改去 Setting/Manual。
3. **Mission 頁「開始」前**(可選,建議做):先送 `FCV_PREFLIGHT_ONLY=1` 版跑一次?——**不用**,server.js 起跑就是同一支腳本,拒跑原因會直接印在腳本輸出;
   只要確保「拒跑」時 GUI 把最後幾行腳本輸出醒目顯示(`🔴 起跑…不跑`),user 才知道為什麼沒動。

## 驗收

- fake:`set_pusher_rpm 450 380` → status `pusher_rpm=450 pusher_rpm_retract=380` + `EVT pusher_rpm 450 380`;Manual 改 RPM → Mission 那行跟著變;
  `zdt_disable 7` → Mission 共用值行標黃。check_console 全綠、gui_v3_check 全過、report 0。
- 不 commit;完成 SendMessage `agent-ai-db`。
