# AI-2:v3「作業流程」併入 Mission 頁,啟動改走 server.js 起 cycle_test(per user 2026-09-15 下午)

> 拍板(user):① 01 作業流程頁**拿掉、併進 Mission,名稱保持 Mission**;② 滑台格式 `rail=<起>-<迄>`;
> ③ 啟動**先走腳本**(server.js spawn `cycle_test.py full …`),參數欄位命名與未來 C++ `mission start` 一致,
>   到時只換「啟動送哪裡」。
> 後端已就緒:server.js(Pi 上已換檔重啟,tag 0915f)、cycle_test `rail=a-b`(兩台 Pi 已同步)。

## 1. 頁面結構(一頁三塊,由上而下)

### A. 前置(可摺疊,預設展開直到全綠)
把 01 的七項搬進來,每項「狀態燈 + 讀值 + 動作鈕」,狀態**從 status 自動判**,不靠自己記:

| 項 | 判定來源 | 動作鈕 |
|---|---|---|
| ① 地面歸零 | 吊機 `zeroed=1`(+ `zeroed_at`) | `zero_meters ground`(現有流程) |
| ② 最高點/牆高 | 吊機 `wall_height_cm>0`(server 的 wall 儲存同步) | 現有量測流程 |
| ③ 推桿歸零 | 本體 `zdt_homed_at>0` | `zdt_home feet` |
| ④ init(幫浦) | 本體 `init_done`/`pump status chA|chB=1` | `arm_attached off` → `init` → `arm_attached on` |
| ⑤ 手臂 | 本體 `arm_ready=1`(手臂 `init_done`) | `arm_init` |
| ⑥ 水位 | 本體 `water_level` `water_full=1` | 「補水」= `water_inlet on` … 滿 +20 s → off(照腳本 `ensure_water_full`,上限 180 s) |
| ⑦ 起點 | 吊機高度 = 牆高 ±5 | `crane_goto <牆高>`(本體代轉;或吊機 `goto`) |

「一鍵前置」= ③→④→⑤ 連跑(歸零/量牆高/升頂仍要人按)。沒全綠就擋「開始」並列出缺哪項(現在 `#mis-why` 的做法)。

### B. 參數(對應 cycle_test full,**同名**)
| 欄 | 送法 | 預設 | 驗證 |
|---|---|---|---|
| 單步距離 cm | 位置參數 `step_cm` | 40 | 1~200 |
| 步數 | `steps` | 5 | 1~99 |
| 週期數 | `cycles` | 1 | 1~999 |
| 風扇 | `fan=move[:pct]` / `fan=all[:pct]`(all:5 = 不開) | move:7 | pct 5~10 |
| 滑台 | `rail=<起>-<迄>` 或 `off` | 0-100 | 0~130、起≠迄;UI 可給快選 0-100 / 100-0 / 20-100 / 100-20 |
| 手臂壓力 N·m | `arm_nm`(server 轉 `FCV_ARM_NM`) | 3 | 1~15 |
| 乾掃 | `dry=1`(server 轉 `FCV_DRY=1`) | 關 | — |
| roll/左右差門檻 | `roll_trip` `diff_trip` | 6 / 8 | 現有 |
| 頂端高度 | **不填**,server 從 wall 儲存帶 `FCV_TOP_CM` | — | 未量就 `wall_height_unset` |

### C. 執行
「開始」+「中止」+ 現有即時狀態區(步/高度/四顆壓力/子步驟/等真空)。
啟動 = 現有 v2 的 `mission start` → server.js `missionStart(p)`(WS 訊息格式沿用 v2:`{mission:'start', params:{cycles,steps,step_cm,roll_trip,diff_trip,rail_cm,fan,rail,arm_nm,dry}}`),
狀態 = 腳本 stdout ring(v2 的 `mission` state/ring 路徑)。**v3 現在那條送本體 `mission start` 的路徑先留著不接**(C++ 引擎未做),
用一個常數切換 `MISSION_BACKEND = 'script' | 'body'`,預設 script。

## 2. server.js 已有(不用你動)
- `missionStart`:位置參數 + `fan=`、`rail=<a>-<b>|off|0|100`、`arm_nm`→`FCV_ARM_NM`、`dry`→`FCV_DRY`;壞參數回 `bad_params` detail 列。
- 起跑印 `[web] spawn: python3 -u cycle_test.py 1 5 40 6 8 fan=move:7 rail=0-100`。
- `rail_cm`(FCV_RAIL_CM,滑台總行程 100)還是要帶,v2 表單原本就有;UI 可藏起來固定 100。

## 3. 假機器
fake_robot 不 spawn 腳本;`gui_offline.sh` 走的是本體 `mission` 族。這頁的 script 模式在假機器上只驗到「送出的 WS 訊息內容正確」即可(在 check 裡 assert 參數字串),真跑要真機。

## 4. 驗收
- 前置七項在真機 status 下顯示正確(我這邊可配合看)。
- 開始鈕送出的參數字串與表單一致;缺前置時擋下並指出缺項。
- 舊 01 頁拿掉後,頁首 tab 剩 Dashboard / Mission / Manual / Setting(順序你排)。
- `gui_v3_check.js` 綠、`report` 0 條 not modelled;手機寬度可用。

## 5. 回報
changelog 一條、版號更新、部署(`scripts/deploy_web.sh`)、SendMessage `agent-ai-db`。不要 commit。

## 6. 追加(per user 2026-09-15 下午②):危險按鈕機制取消、Manual 互斥鎖、急停搬進 Mission

1. **危險按鈕 tier / 60 s 解鎖窗 / 確認窗整套拿掉**。只留**單次確認彈窗**(按「確定」即執行,無計時)於:
   `zero_meters ground|top`、張力保護**關閉**。其餘 Manual 按鈕全部直接按。
2. **互斥鎖**:Mission 按「開始」→ Manual 整頁 disable(灰掉 + 頂端一句「任務執行中,Manual 已鎖定;要接手先按 Mission 的中止」),
   **不是隱藏**。以 server.js `mission.running`(WS `mission state`)為準,頁面重整也對;中止/腳本結束/exit 非 0 → 自動解鎖。
   Manual 的 STOP / PARK 不鎖(那兩顆只停繩、收臂)。
3. **本體急停搬進 Mission 控制列**,頂欄全域紅鈕拿掉:`[開始] [中止] │ [🔴 急停]`。
   - 中止 = 停腳本(現有 `mission stop`,腳本自己收尾:收臂/收腳/風扇停),本體維持 ready。
   - 急停 = 本體 `emergency_stop`(現有 handler,含 reset 出路提示),任務沒在跑也可按。
   - Setting 頁 sim 的 `emergency_stop` 按鈕照舊(假機器用)。
4. SAFE 相關(safebar、`data-safe-keep`)邏輯不變;只是「危險操作 60 s 解鎖窗」這個概念沒有了 → `safe_clear` 鈕改成單次確認。
