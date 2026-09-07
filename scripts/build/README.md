# 建置：Pi 上的 g++，不是 Visual Studio

> 🔴 **2026-09-07 建立。動機是一個查出來的事實：Visual Studio 早就不是建置路徑了，
> 而三份文件（`CLAUDE.md`、`runbook.md`、`.vcxproj`）都還在說它是。**

## 為什麼 VS 已經不能用（2026-09-07 實查）

| 查到的 | 意義 |
|---|---|
| Pi 上 VS 的遠端樹停在 **2026-08-25 ~ 08-27** | 08-30 分層重構之後就沒被用過 |
| 樹裡只有 `Crane_control_PI/main.cpp` + `user_lib/` | 沒有 `app/`、`command/`、`common/`、`transport/`、`mechanism/`、`config/` |
| `rope_axis.h` **整棵遠端樹找不到**，而 `main.cpp` include 它 | ⇒ **那棵樹現在建不起來** |
| `.vcxproj` 的 `AdditionalIncludeDirectories` **漏了 `..\mechanism`**、`..\config` | 建置定義本身就是壞的 |
| `.vcxproj` 的 `ClInclude` 沒列 `rope_axis.h` | 同上 |

⇒ **`.vcxproj` 不再是建置權威。** 它留著只有編輯／IntelliSense 的價值，
**不要照它去推斷檔案清單或 include 路徑** —— 已被證明是錯的。

## 實際的建置路徑

| 腳本 | 目標 | 產物 | 部署名 |
|---|---|---|---|
| `build_body.sh` | 本體 `facade_cleaning_v2`（16 個 TU，完整） | `~/bringup/facade_drv.out` | `facade_cleaning_v2.out` |
| `build_body_incremental.sh` | 本體，**只重編 4 個 TU** 後重連結 | `~/bringup/facade_cleaning_v2.new` | 同上 |
| `build_crane.sh` | 吊機 `Crane_control_PI`（單次 g++） | `~/bringup/crane_drv.out` | `crane_control_PI.out` |

全部是純 `g++ -std=c++17 -O2 -I… -lpthread`，**沒有任何外部相依**。
本體完整建置在 Pi 5 上約 **18 秒**（`-P4` 平行），吊機約 **62 秒**（單次編譯，未平行）。

🔴 **注意 `-O2`，不是 Debug。** `CLAUDE.md` 舊文寫「`bin/ARM64/Debug`」——那是 VS 時代的路徑，
現在的產物在 `~/bringup/`，而且是最佳化過的。

## 🔴 三個先前沒有任何文件記載的隱藏步驟

這些以前只存在於操作者的記憶裡，**照著腳本跑完不會自動發生**：

1. **同步**：腳本建的是 **Pi 上 `~/bringup/` 那份原始碼**，不是 repo。
   動手前必須把改動 `scp` 上去，並**逐位元複驗**（`md5sum` 兩邊比對）——
   否則你建的是什麼，沒有任何東西能證明。
2. **改名**：產物是 `facade_drv.out` / `crane_drv.out`，**部署名不一樣**。
   換上去之前先 `cp -p <部署名> <部署名>.prev-<日期>` 留備份。
3. **停程式才換得動**：binary 正在跑的時候 `cp` 會被 `Text file busy` 擋下（這是好事）。
   先 `echo exit > <fifo>`，**等它真的停**再換。

## ⚠️ 停止程式時的一個陷阱（2026-09-07 踩到）

`pgrep facade_cleaning_v2` **永遠回零筆**——`pgrep` 比對的是 15 字元的 comm，而這個名字有 18 字元。
它會印警告，但如果你只看「有沒有輸出」就會把**還在跑**判成**已停止**。
✅ 用 `ps -eo pid,etime,cmd | grep facade` 或 `pgrep -f`，並**同時確認埠已關**。

## 建置後一定要驗的一件事

**`strings <新binary> | grep <這次新增的字串>`**，並拿現役 binary 做對照組。
2026-09-07 建出來的本體 binary 與現役**大小完全相同**（1086728 bytes）——
純屬巧合，但如果不做這個檢查，看起來就像「建了個一模一樣的東西」。

## 未做（已知缺口）

- 🟡 **沒有同步腳本**：repo → Pi 目前是手動 `scp`。有它才談得上「建出來的是這個 commit」。
- 🟡 **repo 這份與 Pi 上那份是兩份副本**（2026-09-07 取回時 md5 相同）。
  **權威是 repo**；改了要送上去。日後若要根治，該讓部署腳本從 repo 推。
- 🟡 `build_body_incremental.sh` 依賴 `obj/` 已被完整建置填過，**單獨跑會連結失敗**。
