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
| `build_body.sh` | 本體 `facade_cleaning_v2`（16 個 TU，完整） | `~/run/facade_drv.out` | `facade_cleaning_v2.out` |
| `build_body_incremental.sh` | 本體，**只重編 4 個 TU** 後重連結 | `~/run/facade_cleaning_v2.new` | 同上 |
| `build_crane.sh` | 吊機 `Crane_control_PI`（單次 g++） | `~/run/crane_drv.out` | `crane_control_PI.out` |
| `build_linux_test.sh` | bench 工具 `Linux_test`（16 TU） | `Linux_test/linux_test.out` | 手動 scp |
| **`cleaning_arm/compile.sh`** ← 不在本目錄 | 手臂 `motor_api` | `cleaning_arm/motor_api` | `~/run/motor_api` |

🔴 **`cleaning_arm` 刻意不搬進本目錄**：它本來就有 `compile.sh` 且內容是對的，
搬過來只會製造第二份副本。**一份來源** —— 那正是這次整理的主題。

全部是純 `g++ -std=c++17 -O2 -I… -lpthread`，**沒有任何外部相依**。
本體完整建置在 Pi 5 上約 **18 秒**（`-P4` 平行），吊機約 **62 秒**（單次編譯，未平行）。

🔴 **注意 `-O2`，不是 Debug。** `CLAUDE.md` 舊文寫「`bin/ARM64/Debug`」——那是 VS 時代的路徑，
現在的產物在 `~/run/`（2026-09-09 由 `~/bringup/` 搬家），而且是最佳化過的。

## 🔴 三個先前沒有任何文件記載的隱藏步驟

這些以前只存在於操作者的記憶裡，**照著腳本跑完不會自動發生**：

1. **同步**：腳本建的是 **Pi 上 `~/projects/facade_cleaning_v2/` 那份原始碼**（2026-09-09 前是 `~/bringup/`），不是 repo。
   動手前必須把改動 `scp` 上去，並**逐位元複驗**（`md5sum` 兩邊比對）——
   否則你建的是什麼，沒有任何東西能證明。
2. **改名**：產物是 `facade_drv.out` / `crane_drv.out`，**部署名不一樣**。
   換上去之前先 `cp -p <部署名> <部署名>.prev-<日期>` 留備份。
3. **停程式才換得動**：binary 正在跑的時候 `cp` 會被 `Text file busy` 擋下（這是好事）。
   先 `echo exit > <fifo>`，**等它真的停**再換。
   🔴 **2026-09-08 補：「埠關掉了」不等於「程式退出了」。**
   本體 `:5001` 第 **4 秒**就從 `ss -ltn` 消失，但隨即 `cp` 仍拿到 `Text file busy`——
   關埠只代表 listener 收掉，行程還在 join 執行緒、跑正規關機路徑（本體約 5 秒、吊機約 10 秒）。
   ⇒ **下方「同時確認埠已關」這條在停止判定上不夠**，要等到**行程本身消失**才可以換檔。

## ⚠️ 停止程式時的一個陷阱（2026-09-07 踩到）

`pgrep facade_cleaning_v2` **永遠回零筆**——`pgrep` 比對的是 15 字元的 comm，而這個名字有 18 字元。
它會印警告，但如果你只看「有沒有輸出」就會把**還在跑**判成**已停止**。
✅ 用 `ps -eo pid,etime,cmd | grep facade` 或 `pgrep -f`，並**同時確認埠已關**
（⚠️ 埠關了不等於停了，見上方隱藏步驟 3 的 2026-09-08 補註）。

### ⚠️ 反方向的同一族陷阱：數行程會**多**數（2026-09-08 踩到）

`ps | grep facade` 數到 **2 個「殘留」**，實際上程式已經退乾淨了——
那兩筆是**啟動器自己的 `bash -c`**，因為它的命令列字串裡含有執行檔名。

| 寫法 | 這次的結果 | 問題 |
|---|---|---|
| `pgrep facade_cleaning_v2` | 0（永遠） | comm 截斷 ⇒ **漏數**（2026-09-07） |
| `ps \| grep facade` | 2 | 命中自己的啟動器命令列 ⇒ **多數**（2026-09-08） |
| ✅ `pgrep -af "^\./facade_cleaning_v2\.out"` | 0（正確） | 錨定執行檔本身 |

📌 **兩次是同一族、方向相反**：一次把還在跑的判成停了，一次把停了的判成還在跑。
**判準：停止判定要錨定執行檔本身，不要比對「命令列裡有沒有這個字」** ——
下指令的那條命令列本身就含有它。

## 建置後一定要驗的一件事

**`strings <新binary> | grep <這次新增的字串>`**，並拿現役 binary 做對照組。
2026-09-07 建出來的本體 binary 與現役**大小完全相同**（1086728 bytes）——
純屬巧合，但如果不做這個檢查，看起來就像「建了個一模一樣的東西」。

## 未做（已知缺口）

- 🟡 **沒有同步腳本**：repo → Pi 目前是手動 `scp`。有它才談得上「建出來的是這個 commit」。
- 🟡 **repo 這份與 Pi 上那份是兩份副本**（2026-09-07 取回時 md5 相同）。
  **權威是 repo**；改了要送上去。日後若要根治，該讓部署腳本從 repo 推。
- 🟡 `build_body_incremental.sh` 依賴 `obj/` 已被完整建置填過，**單獨跑會連結失敗**。
