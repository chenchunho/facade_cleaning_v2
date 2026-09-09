# scripts/bench/ — 從 Pi 取回的臨時腳本

📌 **2026-09-09 建立。** 兩台 Pi 搬家（`~/bringup` → `~/projects/facade_cleaning_v2` + `~/run`）時，
發現 **14 個檔只存在於 Pi 上、repo 沒有**。直接刪就永久消失，所以先取回。

CLAUDE.md 早記過同一條教訓：*「沒進版控的檢查工具等於下次再寫一次」*
（`web_backend/tools/check_console.js` 就被寫過兩次）。

## 本目錄

| 檔 | 是什麼 | 狀態 |
|---|---|---|
| `bld_cr.sh` | 吊機建置（Pi 上的手寫版） | ⚪ **已被 `scripts/build/build_crane.sh` 取代**，留作對照 |
| `bld_wr.sh` / `bld_body.sh` | 本體建置（同上，兩個版本） | ⚪ 同上，被 `build_body.sh` 取代 |
| `launch.sh` | 08-31 時期的啟動包裝 | ⚪ 已被 runbook §A0 取代 |
| `run_bg.sh` | 背景執行包裝 | ⚪ 同上 |

🔴 **這四支都不是現行做法**——保留純粹是因為它們是唯一副本，而且記錄了搬家前 Pi 上的實際做法。
**要建置請用 `scripts/build/`**（權威版），要啟動請看 `.claude/runbook.md` §A0。

## 一併取回但放在 `Linux_test/` 的

`cyc10.py` `cyc20.py` `probe21.py` `timing.py` `sendcmd.py` `jog_test.cpp`
`dryrun.py` `fc_test.py` `pqw_fix.py` —— 都是上機當下寫的一次性探針，
與該目錄既有的 `cyc.py` / `fan_probe.py` / `walltest.py` 同性質。
