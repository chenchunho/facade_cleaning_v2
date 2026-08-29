# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 給 Claude CLI 的交接指引（接手必讀）

新 session 接手此專案時，**務必先做以下動作再開始工作**：

1. 讀 **`.claude/work_log.md`** — 最新進度 + 待完成項目（最新紀錄在最上方）；每個決策條目上方會標示「**規範權威：** xxx」，指向該決策落在哪份規範文件（CLAUDE.md 某節 / motion_flow.md §X），要確認完整規範就跳過去讀
2. 讀 **`.claude/motion_flow.md`** — 完整運動流程規格（硬體表、phase、狀態機、指令協定、參數常數）
3. 讀 **`.claude/runbook.md`** — 啟動順序、Web GUI 按鈕對應、raw command 指令集、典型流程、緊急處置（知道「怎麼用」系統）
4. ⚠️ 本 CLAUDE.md 的硬體架構圖可能**落後於 motion_flow.md**，以 **motion_flow.md §2 為準**

待完成工作、已討論但尚未實作的設計決策，都在上述 `.claude/` 文件中。

### 文件架構（2026-08-27 定義）

📌 **每份文件回答一個問題。寫東西之前先想「這是在回答哪一個」，避免同一件事寫進三個地方。**

| 文件 | 角色 | 回答的問題 | 讀法 | 變動頻率 |
|---|---|---|---|---|
| **`CLAUDE.md`**（本檔） | 架構規範 | 這系統長什麼樣（建置、程式結構、匯流排拓樸、編碼慣例） | 接手時全讀一次 | 低 |
| **`.claude/<世代>` 規格** | 運動 / 應用層規格 | 它**應該**怎麼動 | 全讀 | 中 |
| **`.claude/runbook.md`** | 操作手冊 | 我怎麼**用**它（啟動順序、按鈕、raw command、緊急處置） | 查用 | 中 |
| **`.claude/work_log.md`** | 現況 | 我接手要先知道什麼（**待辦總表** + 踩過的坑） | **只讀最上面** | 高 |
| **`.claude/changelog.md`** | 變更帳本 | 這行程式碼為什麼長這樣 | 只在追溯時往回查 | 每次改動 |
| **`.claude/summaries/`** | 硬體手冊摘要 | **這顆裝置的協定長什麼樣**（暫存器、功能碼、錯誤碼） | 寫／改 driver 前查 | 低 |

**寫入規則**：改了程式 → 寫 `changelog`；改變了「現況或待辦」→ 才動 `work_log`；改變了「應該怎樣」→ 動規格文件或本檔。**三者不重複寫同一件事。**

- **唯一一份彙整 TODO 表放在 `.claude/work_log.md` 最上方**，其他地方不要再開第二份待辦清單
- 決策 / 規範 / 架構變動一律寫進 **git 追蹤的 .md 檔**，不要只留在對話或 Claude 的本機 auto memory
  —— memory 不跟著 repo 走，換台機器就沒了

#### `.claude/` 完整索引 —— 🔴 **新增檔案必須在這裡加一列**

📌 **2026-08-28 建立。動機**：`summaries/`（8 份手冊摘要、1,228 行）在此之前**沒有出現在任何索引裡**，
接手的人只能靠 driver 現有程式碼反推協定——**那正是 DM2J 那次踩雷的方式**。
一份沒被指到的文件等於不存在。

| 檔案 | 是什麼 | 狀態 |
|---|---|---|
| `work_log.md` | 現況 + **唯一待辦總表** | 🟢 活的，**只讀最上面** |
| `changelog.md` | 變更帳本（append-only，不壓縮） | 🟢 活的，只在追溯時查 |
| `runbook.md` | 操作手冊：啟動順序、按鈕、raw command、緊急處置 | 🟢 活的 |
| `per_program_cautions.md` | 各程式「最容易踩、後果最嚴重」的交接摘要（2026-08-28 由 main 帶入） | 🟢 活的，但 §0.2 的 bus 表已隨同批程式改動過期，見檔內 2026-08-28 更正 |
| `summaries/` | 8 份硬體手冊摘要（原始 PDF 不在 repo） | 🟢 活的，見上一節 |
| `motion_flow.md` | **v1 規格**（已凍結） | 🟡 §2 硬體表已過期，保留作狀態機與指令協定的原始推導。📌 **2026-08-28 破例加過一次註記**（非規格變更）：§8 失效模式表說「張力過高→立即 crane stop」，同章「緊急收繩」段說「不會停」，**兩段互相矛盾**；逐行驗過程式碼後在表上加了除外註。凍結的意思是不再演進，不是「明知有誤導也不標」 |
| `v2_app_redesign_plan.md` | **v2 規格 = 現行程式碼的權威** | 🟢 活的 |
| `洗窗機器人設計彙整.md` | **v3 新架構設計**（沿用舊硬體改寫） | 🟢 活的，27 項待辦在 `work_log.md` |
| `mh300_migration_plan.md` | SE3 → Delta MH300 變頻器遷移 | 🟡 **進行中且未完成**。🔴 Phase 0 的 keypad 參數表是**唯一副本** |
| `crane_balance_hold_plan.md` | 吊機平衡保持 | 🟡 **暫緩，但 2026-08-27 前提已反轉**（同步步伐放繩期間四顆吸盤全放、無錨定）→ 需重新評估 |
| `step_speedup_phase1_plan.md` | 單 step 25-30s → 12-15s 加速 | 🟡 標「進行中」，含瓶頸量測表 |
| `mailbox.md` | ⚰️ **墓碑檔**：多人協作信箱，2026-08-27 退休 | ⚪ 16 條未結案項已全數併入 `work_log.md` 待辦總表 |
| `gen_deploy_pdf.py` | 產生 `deploy_and_test.pdf` 的腳本 | ⚪ **只能在 Windows 跑**（寫死 `C:\Windows\Fonts\msjh.ttc`），需 `fpdf` |
| `archive/camera_obstacle_plan.md` | ⚰️ 已作廢：相機路線整條移除 | ⚪ Phase 3~6 一項未做 |
| `archive/scripted_run_plan.md` | ⚰️ 已完成（實作超出原規劃） | ⚠️ **計畫裡兩處決策已被推翻，照著做會做錯**（見墓碑抬頭） |
| `archive/se3_mode6_migration_plan.md` | ⚰️ 已作廢（從未開工，被 MH300 取代） | 🔴 **但內容仍然有效**——bench 現在跑的還是 SE3。§1.1 是唯一一張「SE3 故障 ↔ workaround」對照表；§7 記著四個已被否決的方向 |

🔴 **「已歸檔」不等於「內容失效」**——`se3_mode6_migration_plan.md` 就是活生生的例子：
計畫本身死了，但它裝著現行硬體唯一的故障對照表。**歸檔時墓碑抬頭一定要寫清楚
「裡面哪些知識仍然有效」**（見下方「plan 檔的生命週期」）。

#### 🔴 規格文件有三個世代，不要拿錯

| 世代 | 文件 | 狀態 |
|---|---|---|
| v1 | `.claude/motion_flow.md` | **已凍結**。§2 硬體表仍是 `DM2J×5`／`ZDT×9`，與現行程式碼不符；保留作為狀態機與指令協定的原始推導 |
| v2 | `.claude/v2_app_redesign_plan.md` | ✅ **現行程式碼的權威**（4 吸盤／2 區真空／吊機繩位移／無 DM2J） |
| v3 | `.claude/洗窗機器人設計彙整.md` | 🆕 新架構方向：四輪滾動＋兩具 22 吋螺旋槳貼牆＋橫向滑台＋雙主控，**沿用既有硬體改寫** |

> ⚠️ 因為是「舊硬體改寫」，`user_lib/` 的驅動層大致沿用 —— **既有的驅動層技術債不會隨應用層重寫而消失，它跟著硬體走**。

#### `.claude/summaries/` — 寫 driver 之前先來這裡

📌 **原始 PDF 手冊不在 repo 裡**（放在 Windows 端 `D:\洗窗戶機器人\電控設備資料\`），
所以這 8 份摘要對只有 repo 的人來說**就是手冊本身**。動任何 `user_lib/` 的 driver 之前先查這裡，
不要憑 driver 現有的程式碼反推協定 —— 那正是 DM2J 那次踩雷的方式。

| 檔案 | 裝置 | 特別值得看的 |
|---|---|---|
| `SE3_INVERTER_MODBUS_SUMMARY.md` | 士林 SE3-210 變頻器（**bench 現用**） | 310 行、最完整。**錯誤碼表 H1007/H1008**、`P.79` 模式切換、通訊逾時的安全設定、H1101-H1106 magic command |
| `ZDT_MODBUS_SUMMARY.md` | ZDT 閉環步進（吸盤推桿） | 兩種韌體（X / Emm5.0）暫存器語意不同，**拿錯會讀到垃圾** |
| `DM2J_RS_MODBUS_SUMMARY.md` | DM2J-RS570（上滑台） | ⚠️ **2026-04-24 之前的版本多處錯誤**，driver 也踩了同一個雷（見檔內警告）；含 `Known Driver Bugs` 一節 |
| `CLV900_INVERTER_MODBUS_SUMMARY.md` | CLV900 變頻器（中間絞盤，未安裝） | **不支援 `0x10` 多寫**、故障碼 U0-01 |
| `SD76_MODBUS_SUMMARY.md` | SD76-C 計米器 | 暫存器圖、工作模式、錯誤碼、資料編碼（BCD） |
| `JC_100_MODBUS_SUMMARY.md` | JC-100 真空壓力表 | 量程與單位（0.1 kPa int16 signed） |
| `PQW_IO_MODBUS_SUMMARY.md` | PQW 繼電器模組 | 線圈位址、暫存器圖、輸出模式 |
| `ZS_DIO_MODBUS_SUMMARY.md` | ZS-DIO 繼電器（已被 SE3 取代） | 保留作歷史對照 |

🔴 **一個具體例子**：既有待辦「VFD 故障碼顯示是壞的（`vfd_fault` 一邊報假警一邊讀不到）」，
它要的 SE3 錯誤碼對照表**就在 `SE3_INVERTER_MODBUS_SUMMARY.md` 的
`## Error Code Reference (H1007 / H1008)`** —— 不必再去翻 PDF。

📌 **摘要一律註明來源檔案路徑**（見各檔開頭的 `Source:`），這樣才追得回原始手冊。

#### plan 檔的生命週期

**plan 只在「進行中」存在。** 完成或作廢就移進 `.claude/archive/`，並在檔案開頭加一段抬頭寫清楚：為什麼退場、**裡面哪些知識仍然有效**（唯一副本的量測值、被否決的方向、踩坑結論）。

- 🔴 **歸檔不是刪除**：一份計畫可以「作為計畫已死」但仍是某個硬體量測值或某條踩坑的唯一記載
- 🔴 **退場前先把未結案項目搬進待辦總表**，否則債會跟著檔案一起消失
- **不要在 plan 檔裡維護待辦清單** —— 待辦只有一份，就在 `work_log.md` 最上方

#### `work_log.md` 的壓縮規則

`work_log` 會持續長大，需定期壓縮；**`changelog` 不壓**（append-only，價值就在完整，平常也沒人需要讀它）。

判準：**「裝了、沒問題，就不用再保留紀錄。」** 例行維護與流水帳做完即丟，只留四種：
**待辦／決策（尤其被否決的）／伏筆／踩坑**。🔴 **待辦項目絕不可壓掉。**

### 模組邊界：user_lib 的介面契約

**原則：** `user_lib/*.h` 的 public API 簽名是**跨模組契約** — 應用層（`WASH_ROBOT.{h,cpp}`、各 `main.cpp`）與測試程式（`Linux_test/`）全都綁在上面。**改簽名的爆炸半徑遠大於改實作。**

- 🔴 **改 public API 簽名**（參數、回傳型別、method 名稱）＝架構層級改動：所有呼叫端都得跟著改，動手前先掃過全 repo 的呼叫點
- **優先用累加式改動**：加新 method / 新 overload，而不是改既有簽名，讓既有呼叫端維持可編譯
- 🟡 **不動 public API 的內部改動**（private method、實作邏輯 bug fix）風險低，可以直接改
- **新增 class（新硬體驅動）＝架構變動**：牽涉拓樸、slave ID 配置、初始化流程，要一併更新本檔的架構圖與裝置驅動表
- **設計 API 之前，先把「我需要的行為是什麼」寫清楚**（要達成什麼動作、時序與錯誤條件），再回頭決定介面長什麼樣 — 不要從「加一個 method」開始想

#### 🔴 契約管了簽名，沒管**語意** — `init()` 的 bool 有兩種相反意思

📌 **2026-08-29 由第一次跑起來的 `test_qx_do24` 揭露。**

`user_lib/` 14 支 driver 的 `init()` 簽名全部是 `bool init(...)`，**但回傳語意有兩派**：

| 語意 | driver | 數量 |
|---|---|---|
| **`false` = 成功**（Modbus 風格，`true` = error） | SE3 / MH300 / DM2J / SD76 / DSZL / DY500 / JC100 / PQW / XKC / ZDT / ZS_DIO / CLV900 | **12** |
| **`true` = 成功**（一般 bool 風格） | **`QX_DO24`**（唯一活著的異類）、`DIHOOL_control`（全 repo 無呼叫端＝死碼） | 2 |

🔴 **從 `.h` 看不出來是哪一派** —— 兩者的宣告逐字相同。呼叫端寫 `if (dev.init(...))`
在 12 支上的意思是「失敗了」，在 QX_DO24 上的意思是「成功了」。

**目前應用層沒有踩到**（2026-08-29 逐一查過）：SE3/MH300 的呼叫端寫
`if (!vfd.init(...)) { OK } else { WARN }` ＝ 對；QX_DO24 的唯一呼叫端
`app/WASH_ROBOT.cpp:204` **根本不檢查回傳值**。**唯一的受害者是那支從未被執行過的測試。**

- 🔴 **新增 driver 一律用 `false` = 成功**（12 比 2，且吊機與本體的主線都在這一派）
- 🔴 **新增 `QX_DO24` 的呼叫點時特別小心**，或先把它對齊多數派（語意變更，需先確認呼叫端）
- 📌 **通則**：「簽名相同」不代表「可以照同一種方式呼叫」。介面契約若只釘型別、不釘
  **成功／失敗的方向**，編譯器一個字都不會說 —— 而錯的那邊會安靜地永遠走錯分支

⚠️ **不要用「函式最後一個 `return`」去判斷是哪一派** —— 2026-08-29 我這樣推，
把 SE3 / MH300 誤歸成 `true`=成功（它們最後一個 return 是**失敗**路徑）。**逐支讀成功路徑。**

## Project Overview

C++ 機器人控制系統，包含洗窗機器手臂（wash robot arm）與吊車升降系統（crane lift），目標平台為 Raspberry Pi（ARM/ARM64），透過 Visual Studio 的 "Visual C++ for Linux Development" 從 Windows 交叉編譯部署。

## Build System

**IDE:** Visual Studio (solution: `facade_cleaning_v2.sln`)
> 🐛 **2026-08-28 更正**：本檔原本寫 `washrobot_new_PI.sln`——那是 fork 前 v1 的檔名，repo 裡根本沒有這個檔。照舊寫法跑會直接失敗。  
**Build method:** MSBuild with remote SSH deployment to Linux/ARM targets  
**No CMake or Makefile** — all build configuration is in `.vcxproj` files  

Target platforms: ARM, ARM64, x86, x64 — all with Debug/Release configurations.  
Compiled binaries land in `bin/[arch]/[config]/` within each project directory.

To build from command line (if using MSBuild):
```
# 🔴 Platform 必須是 ARM64——只有 Debug|ARM64 設了 AdditionalIncludeDirectories
msbuild facade_cleaning_v2.sln /p:Configuration=Debug /p:Platform=ARM64
```

## Repository Structure

📌 **2026-08-27 起分層**：操作層 → 應用層 → 裝置層。上層呼叫下層，**下層不認識上層**。

```
web_backend/         # ── 操作層：Node.js server + 前端 GUI
app/                 # ── 應用層：機器人編排（步態、狀態機、真空重試、校正）
                     #    WASH_ROBOT.{h,cpp}
user_lib/            # ── 裝置層：單一硬體的驅動
                     #    ZDT / JC100 / SD76 / MH300 / SE3 / DSZL / PQW / QX_DO24 …
transport/           # ── 傳輸層：裝置層之下，與硬體種類無關
                     #    TCP_client / TCP_server / Serial_port
facade_cleaning_v2/  # 洗窗本體主控 binary（main.cpp，薄；邏輯在 app/）
Crane_control_PI/    # 吊機主控 binary
                     #    ⚠️ 應用層尚未抽出，編排邏輯仍寫在 main.cpp（4,400+ 行）
cleaning_arm/        # 手臂控制 binary
                     #    ⚠️ 自成一格：不使用 user_lib，自建 socket 層（main_api.{h,cpp}）
Linux_test/          # bench 互動式硬體測試工具（含 probe_dm2j.cpp：上滑台機構標定/行程量測）
frame_capture/       # Python 影像工具（相機路線已作廢，見 .claude/archive/）
scripts/             # tmux launcher：wr.sh / crane.sh / cams.sh
tmp/                 # 暫存工作區（已 gitignore，不進版控）
```

#### 根目錄完整盤點 —— 🔴 **新增檔案必須在這裡加一列**

📌 **2026-08-28 建立。** 上面的樹狀圖只畫了程式目錄，根目錄還有 8 個檔／目錄
**從未被任何索引提到**。同 `.claude/` 索引的理由：沒被指到的檔案等於不存在。

| 項目 | 是什麼 | 狀態 |
|---|---|---|
| `README.md` | repo 門面。🔴 **唯一記載 fork 出身**：自 `washrobot_new_PI` commit `9f174f9`（tag `v2-fork-from-v1`）於 2026-06-25 分出 | 🟡 「跟 v1 主要差別」多數仍是 TBD |
| `ONBOARDING.md` | **52 KB、11 章的知識庫**：硬體驅動踩坑、工程心法、v2 步態引擎詳解、crane 通訊 hardening 三疊 bug、已退役子系統 | 🟢 活的。⚠️ 第 2 章「尚未解決」已改為指標，**待辦只在 `work_log.md`** |
| `facade_cleaning_v2.sln` | VS 方案檔（**不是** `washrobot_new_PI.sln`） | 🟢 活的 |
| `deploy_and_test.pdf` | 部署測試說明，由 `.claude/gen_deploy_pdf.py` 產生 | 🟡 產生腳本只能在 Windows 跑 |
| `dm2j_manual_utf8.txt` | DM2J 手冊的**可讀**文字擷取（簡體中文） | 🟡 已被 `.claude/summaries/DM2J_RS_MODBUS_SUMMARY.md` 濃縮，保留作原文對照 |
| `.vs/`（43 MB）／`tmp/` | VS 快取／暫存工作區 | ⚪ 已在 `.gitignore`，不進版控 |

🗑️ **2026-08-28 已刪除 4 個檔（228 KB）**：`main_tmp.txt`（v1 時期 `main.cpp` 開頭註解的舊副本，
抬頭仍寫 `washrobot_new_PI`、`.21` 匯流排——**看它會得到錯的拓樸**）、
`dm2j_manual.txt`／`dm2j_manual2.txt`／`zdt_modbus.txt`（PDF 文字擷取，編碼壞掉無法閱讀，
且都已被 `.claude/summaries/` 取代）。
📌 **理由是「看了會被誤導」而不只是「沒用」。** 四個檔都在版控裡，需要時從 git 歷史取回。

> 🐛 **`user_lib/SerialPort.h` 與 `transport/Serial_port.h` 是兩個不同的檔，卻共用同一個
> include guard `SERIAL_PORT_H`**。前者是 `cleaning_arm` 的 damiao 那一套（經 `user_lib/damiao.h`），
> 後者是本專案的序列埠。目前不爆是因為使用者不重疊 —— 但**只要哪天同一個編譯單元碰到兩者，
> 第二個會被 guard 靜默吃掉**，症狀是「某個 class 莫名找不到」，沒有任何錯誤訊息指向真因。
> 因此 `SerialPort.h` **刻意留在 `user_lib/`、不併入 `transport/`**，避免兩套並存的假象。
>
> 🔴 **`user_lib/` 只放裝置驅動。** 2026-08-27 之前 `WASH_ROBOT.{h,cpp}`（15,321 行，
> 佔全專案 37%）也放在這裡，但它是**編排層不是驅動**——放著會讓「`user_lib` 是裝置驅動」
> 這句話（下方「模組邊界」節的前提）當場失效。已移到 `app/`。
>
> ⚠️ **建置設定**：8 個組態裡**只有 `Debug|ARM64` 設了 `AdditionalIncludeDirectories`**
> （`..\app;..\user_lib`），其餘 7 個原本就沒有、也編不起來。實際使用的就是這一個
> （Pi 是 aarch64，部署到 `bin/ARM64/Debug/`）。**移動檔案時要記得同步這一行，
> 只改 `ClCompile`/`ClInclude` 不夠——標頭會找不到。**

## Architecture

> 📌 **2026-08-28 全面改寫：本節由原始碼逐檔掃描重建。**
> 先前的架構圖是 **v1**（`DM2J×5`／`ZDT×9`／三區真空／`.21` 匯流排），與現行程式碼差距已大到會誤導。
> v1 的原始推導保留在 `.claude/motion_flow.md`（已凍結），**不要在這裡重建 v1**。
> 🔴 **權威來源是原始碼常數**，本節每個數字都標了出處檔案與行號，改硬體時一併改這裡。

### 執行時的行程拓樸（先看這個——它決定你要 ssh 去哪台）

**兩台 Pi，但程式不是各跑各的**。2026-08-28 實機觀測：

```
                    瀏覽器
                      │ WebSocket + HTTP
                      ▼
  ┌─────────────────────────────────────────────┐
  │ 吊機 Pi  raspberry-cran                      │   ⚠️ web GUI 在吊機這台，不在本體
  │   node server.js            :8080            │
  │   Crane_control_PI          :5002            │
  └───────┬──────────────────────────┬──────────┘
          │ TCP 文字協定              │ TCP 文字協定
          ▼                          ▼
  ┌───────────────────┐    （橋接兩邊，見下）
  │ 本體 Pi  washrobot │
  │   facade_cleaning_v2   :5001                 │
  │   motor_api（手臂）     :9527  ← 127.0.0.1 本機 │
  └───────────────────┘
```

- **web_backend 刻意放在吊機側**：本體在半空中掛掉時，GUI 仍能透過吊機手動收繩救援
- **本體會主動當 TCP client 連吊機 `:5002`**（`app/WASH_ROBOT.h` `CRANE_IP`），
  自動步態下移時由本體下 `pay_out_left/right <cm>` 同步放繩 —— 兩台之間是**本體指揮吊機**
- **手臂 `motor_api` 跑在本體 Pi 的 `127.0.0.1:9527`**，本體用
  `arm_cmd_("INIT"/"DEPLOY"/"PARK"/"STATUS")` 下指令

| 位址 | 機器 | 帳號 | 備註 |
|---|---|---|---|
| `192.168.1.100` / `192.168.5.26` | 本體 `washrobot` | `nexuni` | 有線／WiFi |
| `192.168.1.10` / `192.168.5.17` | 吊機 `raspberry-cran` | `user` | 🔴 **有線是 `.10` 不是 `.101`** |

⚠️ **三份文件對吊機 IP 的說法不一致**：`web_backend/server.js` 的 `CRANE_IP` 預設值仍是
`192.168.1.101`（過期）、`runbook.md` 的表也寫 `.101`、只有 08-27 實測記到 `.10`。
現行程式實際走的是 `app/WASH_ROBOT.h` 的 `CRANE_IP = "192.168.5.17"`（**WiFi**）。

### 匯流排拓樸（as-built，2026-08-28 由原始碼確認）

所有 RS485 裝置都掛在 **USR-TCP232 透明傳輸網關**後面，程式以 **Modbus-TCP over :4001** 連網關。

#### 網關本身的設定（2026-08-28 由網頁後台實查，先前沒有任何記錄）

`.20` 與 `.22` 都是 **USR-TCP232-304**，設定**完全一致**：

| 項目 | 值 | 備註 |
|---|---|---|
| 波特率 / 格式 | **115200 8N1** | 🔴 `.22` bus 上**所有**裝置都是 115200（2026-08-28 per user 更正，舊記載的 9600 已作廢） |
| 本地埠 | `4001` | TCP server 模式（`cmode=0`） |
| 最大連線數 | `4` (`_cnum`) | ⚠️ 多個 client 同時發指令會讓回覆錯位；實查當下**只有本體 `.1.100` 一條連線** |
| **Modbus 閘道模式** | **關閉**（`mdm=0` / `mde=0`） | 純透明傳輸 → **封包邊界完全由字元間隔決定** |
| **串口打包時間** | **`_pt = 0`（自動）** | 🔴 115200 下字元間隔僅約 0.3ms → **這是 2026-08-28b 那個「回覆被切成兩個 TCP 段」的結構性根源**。設成 5ms 可從根本解決，代價是每筆交易多 ≤5ms（`status` 讀 4 顆 → +20ms）。**尚未改**——目前量到的失敗是 `no reply` 不是 `too short`，在沒有證據指向分片前不動共用設定 |
| 串口打包長度 | `_plen = 400` | |
| 韌體 | `V1.1.03` | |
| 後台 | `http://<ip>/`，`admin` / `admin` | per user：不是重要裝置，可記錄。頁面：`port.shtml`（序列埠）／`system.shtml`（打包參數）／`modbus.shtml`／`status.shtml`（連線與流量） |

📌 **`192.168.1.21` 是同型網關但 2026-08-28 實測不可達**（沒上電或未接網路）。
v1 時代 ZDT 掛在這台，v2 已退役（`app/WASH_ROBOT.h`：`.21/cli_21_ retired`）。
**查 `.22` 間歇性無回應時特地確認過它** —— 若它還活著且 A/B 併在同一對線上，
就會是同一條匯流排上的第二個主站；實測不可達，此假設排除。

📌 **網關之外的實體網路**（不由程式控制，但斷了什麼都連不上）：
外部訊號 →（2-wire tether 雙絞線）**Fathom-X Tether Interface Board** →（Ethernet）
**8 Port PoE Switch** → 兩台 Pi、全部 USR 網關、以及 **PoE 防水 2MP 攝影機 × 4**
（左上/左下/右上/右下；`frame_capture/` 走 RTSP，預設 cam1 `.110` / cam2 `.111`）。

#### 本體 washrobot（權威：`app/WASH_ROBOT.h` + `WashRobot::init()`）

```
Raspberry Pi 5（本體主控）
  ├─ USB→TTL  /dev/ttyUSB0 @ WT901BC ──── 姿態儀 IMU（Serial_port，非 Modbus）
  ├─ 127.0.0.1:9527 ──────────────────── motor_api → damiao USB-CAN
  │                                        M1 DM10010L 大臂 / M2 DM4340_48V 工具頭
  │
  ├─ USR #1  192.168.1.20  (cli_20_)  ─── 「動力 + 滑台 bus」
  │     ├─ ZDT slave 5,6 ── 右腳 上/下 推桿（SMC LEYG25）
  │     ├─ ZDT slave 7,8 ── 左腳 上/下 推桿
  │     ├─ PQW slave 12 ─── 8CH 繼電器（2026-08-27 從 .22 搬來）
  │     └─ DM2J slave 14 ── 上滑台（乘載機械手臂；2026-08-28 從 .22 搬回）
  │
  └─ USR #3  192.168.1.22  (cli_22_)  ─── 「感測 bus」
        ├─ JC-100 slave 5~8 ── 真空壓力計（與 ZDT 同號：推桿 N 末端的吸盤 = 真空表 N）
        ├─ QX-DO24 slave 9 ─── PWM（2026-08-28 由 6 改號並重新啟用，見下）
        ├─ DY-500 slave 10,11 ─ 鋼索重量感測（**未安裝**，polling 關閉）
        └─ XKC-Y25 slave 13 ── 水箱水位（不探測，首次讀取才會發現缺件）
```

🔴 **同號不衝突的理由**：ZDT 5-8 在 `.20`、JC100 5-8 在 `.22`，**兩條實體 bus**。
`CUP_SLAVE_FIRST/LAST`（`WASH_ROBOT.h:524`）是唯一真實來源，所有遍歷吸盤的迴圈都吃它。

📌 **2026-08-28 兩處異動（來自 main 的 bench 修正，合併進來）**：
- **DM2J 上滑台 `.22` → `.20`**：實體接線一直在 `.20`，程式卻對 `.22` 發指令 →
  每次掃動 `writeMulti no response` ×3，而流程照印「rail sweep done」。
  ⚠️ 現在滑台與 ZDT 推桿共用 `.20`，靠 `TCP_client::socket_mtx` 序列化（幀不交錯）；
  但 `pusher_two_stage_retract_` 持有的是 `zdt_bus_mtx_`，**DM2J 不拿那把鎖**——安全但不互斥。
- **QX-DO24 PWM `slave 6` → `9` 並解除停用**：6 撞上改號後的 JC100 右腳下吸盤真空表。
  ⚠️ `app/WASH_ROBOT.h:1082` 的成員註解仍寫「`.22 = ... arm-rail ...`」，**已過期**。

#### 吊機 crane（權威：`Crane_control_PI/main.cpp:161-168`）

```
Raspberry Pi（吊機主控）
  ├─ USR_A  192.168.1.30 ── SE3 變頻器（左鋼索）        ← 控制 bus
  ├─ USR_B  192.168.1.31 ── SE3 變頻器（右鋼索）        ← 控制 bus
  ├─ USR_M  192.168.1.34 ── SD76 計米 ×2 + PQW slave 12  ← 感測 bus
  │                          PQW CH4 = 水箱進水球閥
  ├─ X518   192.168.1.32:502 ── DSZL-107 左張力（原生 Modbus TCP，非 :4001）
  └─ X518   192.168.1.33:502 ── DSZL-107 右張力
```

📌 **左右 SE3 各佔一條 bus**（不是共線）：半雙工 RTU 下兩台共線會序列化，2026-05-15 量到
200-300ms drift，拆開後降到 ~30-50ms。張力計各自獨佔一條，避免 X518 高採樣率被別的輪詢拖慢。

⚠️ **`CRANE_VFD_IS_SE3`（`main.cpp:116`）目前是 `1`——bench 實際仍在跑 SE3，不是 MH300。**
MH300 driver 已存在但遷移未完成（故障碼那段仍讀 SE3 的 H1007/H1008）。

### 吸盤控制邏輯（🔴 已從 v1 的三區變成**單閥四吸盤**）

```
PQW 8CH 繼電器（本體 .20 slave 12）        權威：app/WASH_ROBOT.h:449-474
  ├─ CH1  VT307 電磁閥 ── 全部 4 顆吸盤（唯一一顆閥）
  │        ⚠️ CH_VALVE_LEFT == CH_VALVE_RIGHT == 1（2026-08-27 左右合併）
  ├─ CH2  dp0105 真空產生器（運轉期間常開）
  ├─ CH6  🔴 破真空閥（2026-08-27 從 CH14 搬來）
  ├─ CH14 水箱噴水泵浦（2026-08-27 從 CH6 讓位過來）
  └─ CH15 手臂滾筒刷馬達
```

🔴🔴 **CH6 與 CH14 絕不可同號**：若水泵仍指向 CH6，清洗時開水泵＝開破真空閥
→ 4 顆吸盤同時失去真空 → **機器在貼牆狀態下脫落**。這兩個常數的沿革註解務必保留。

⚠️ v1 的「腳組 / 身體組 / 中心」三區真空、`ZDT×9`、`DM2J` 腳輪滑軌**全部退場**。
現在只有 4 顆吸盤（= 2 隻腳 × 上下各一），步態靠**左右交替**而非上下分區。

### 應用層：狀態機與併發模型

**狀態機**（`app/WASH_ROBOT.h:377`，11 個狀態）

```
Idle → Ready → Attached → Running ⇄ Paused
                   │         ├→ WaitingConfirm（等 confirm_balance）
                   │         └→ PausedOnError（等 continue / skip / emergency_stop）
                   ├→ Balancing / ReturningHome / Calibrating
                   └→ Error（只剩 status / ping / reset / return_home 可用）
```

**指令分兩條路徑**（`facade_cleaning_v2/main.cpp:388-412`）—— 這是個容易誤改的設計：

| 路徑 | 指令 | 執行方式 |
|---|---|---|
| **FAST** | `ping` `status` `pause` `resume` `continue` `skip` `emergency_stop` `reset` `zdt_release_stall` | 直接在收包執行緒同步跑，立即回覆 |
| **SLOW** | 其餘全部（會搶 `motion_mtx_` 或阻塞等人介入的） | 另開 detached thread |

🔴 **為什麼要分**：長時間運動指令若佔住收包執行緒，同一條 TCP 連線就送不進
`continue`/`skip`/`stop` → **GUI 死鎖**。這是修過的 bug，不要合併回單一路徑。

**背景執行緒**

| 本體（`WASH_ROBOT.h`） | 吊機（`Crane_control_PI/main.cpp:4364+`） |
|---|---|
| `crane_wd_thread_` 吊機看門狗 | `watchdog_loop` 心跳逾時 → abort |
| `crane_keepalive_thread_` | `hold_loop` 張力監控 → 超標 `hold_all_off()` |
| `water_inlet_watchdog_thread_` | `meter_loop` SD76 輪詢（含 >30cm 跳變過濾） |
| `imu_mon_thread_` | `vfd_keepalive_loop` |
| `pressure_poll_thread_`（保留但**從不啟動**） | |

### 🔴 掃描時發現的三件事（記在這裡免得下次重掃）

**1. `WASH_ROBOT.cpp` 有 3,879 行死碼（佔全檔 30%）**
16 個 `#if 0` 區塊，最大一塊 897 行。都是 v1 退役程式碼「留作參考」。
🔴 **它們已經無法靠把 `#if 0` 改成 `#if 1` 復活**——裡面引用的 `ZDT_LB1`／`ZDT_RB1`／`ZDT_C`
等符號**在 `WASH_ROBOT.h` 裡已經不存在了**（預處理器把它們吃掉才沒報錯）。
所以「留作參考」只剩**閱讀**價值，沒有復原價值。

**2. 有些註解描述的是舊配置，程式碼本身是對的**
例如 `WASH_ROBOT.h:513` 開頭仍寫「right{1,2} / left{3,4}」（下一行才更正為 5-8）、
`:1020` 寫「.20 = ZDT pushers 1-4」、`cmd_water_pump` 宣告處寫「PQW CH6」但常數是 CH14。
📌 **判準：常數定義 > 附近註解**。

**3. `init()` 一失敗就 `return`，一次只會看到最前面那一個問題**
（本專案慣例 `true`＝失敗）。硬體連線驗證可能要跑好幾輪才挖得完。

### 電源架構（參考，v1 時期記錄，未隨 v2 更新）

⚠️ 下表是 v1 的配置，`DM2J×5` 那條已不存在。保留是因為**這是電源分組的唯一記載**，
但**不要拿它推論現行硬體**。

```
[總電源] AC 220V
  ├─ [A] EPP-200-24 → 馬達剎車與機械手臂
  ├─ [B] EPP-200-24 → 氣動、感測 I/O 與通訊介面（推桿 ZDT、JC-100、DY-500、PQW 繼電器）
  ├─ [C] EPP-200-48 → 8 Port PoE Switch、DM2J 步進控制器（v1 為 ×5）
  └─ [變壓器] DC 5V → Raspberry Pi、USR-TCP232
```


### Communication

All hardware uses **Modbus-TCP** over TCP/IP (port 4001). The `TCP_client` class provides a cross-platform socket wrapper (WinSock2 on Windows, BSD sockets on Linux) with auto-reconnect via a monitor thread.

Every device driver sends/receives raw Modbus frames: function codes 0x01 (read coils), 0x03 (read registers), 0x05/0x06 (write single), 0x10 (write multiple). CRC16-CCITT (polynomial 0xA001) is used for validation.

Socket timeouts: 100-500ms per device. TCP monitor thread: 500ms reconnect polling interval.

### Device Drivers (`user_lib/`)

**使用中：**

| Class | Device | Interface | Description |
|---|---|---|---|
| `TCP_client` | TCP socket abstraction | WinSock2/BSD | Cross-platform TCP with auto-reconnect & monitor thread |
| `TCP_server` | TCP listener | WinSock2/BSD | washrobot :5001 / crane :5002，多 client、line-buffered |
| `Serial_port` | Serial port (Windows/Linux) | Native | TTL serial communication (8N1, multiple baud rates) |
| `DM2J_RS570` | 步進馬達驅動器 × 1（v2 只剩上滑台） | Modbus-TCP (RS485_1 .20 slave 14) | **上滑台（乘載機械手臂）@ 192.168.1.20 slave 14**（2026-08-28 per user 確認實體接在 .20）。cm 精度，PR/JOG/Home 模式，PPR=10000（1cm=10000 pulses，與 ZDT 的 3000/cm 不同）。<br>bus 沿革：.20 slave 5 → 2026-05-26 搬到 .22 slave 14（v1 時代為了讓 arm sweep 跟 feet rail 並行不撞 bus）→ **2026-08-28 搬回 .20 slave 14**。在搬回之前程式對 .22 發指令而實體在 .20，每次掃動都是 `writeMulti no response` × 3，且流程仍照印「rail sweep done」（fire-and-forget 不看回傳值，已一併修正）。<br>⚠ 現在與 ZDT 推桿 5~8 / PQW 12 共用 .20：rail sweep 是背景執行緒、與主執行緒伸腳並行，靠 `TCP_client::socket_mtx` 序列化（幀不會交錯），但注意 `pusher_two_stage_retract_` 持有的是 `zdt_bus_mtx_`，DM2J 不拿那把鎖。<br>v1 的左腳/左輪/右腳/右輪 @ RS485_1 slave 1~4 在 v2 已移除 |
| `ZDT_motor_control` | 閉環步進驅動卡 × 9 | Modbus-TCP (RS485_2) | 驅動 SMC LEYG25 推桿，encoder 回饋，堵轉保護 |
| `JC_100_METER` | 真空氣壓感測器 × 9 | Modbus-TCP (RS485_3) | 讀取壓力 (0.1 kPa)，裝於各推桿末端吸盤 |
| `DY_500_weight_sensor` | 鋼索重量感測器 × 2 | Modbus-TCP (RS485_3) | 讀取重量 (int32/float)，裝於機體與鋼索連接處 |
| `PQW_IO_16O_RLY` | 8CH 繼電器模組 × 2 | Modbus-TCP | washrobot cli_22_ slave 12 (CH1-6 + CH8)：dp0105 泵浦 + VT307 電磁閥 + 刷洗/水泵；crane cli_M slave 12 (CH4 only, 2026-06-05 搬遷)：水箱進水球閥（原 washrobot CH7 改空著） |
| `WT901BC_TTL` | 九軸姿態儀 | USB→TTL Serial 115200 | 背景執行緒連續讀取，checksum 驗證；Roll+Pitch 平衡監控 |
| `damiao` (header-only) + `SerialPort` | damiao 清潔手臂馬達 × 2 (M1+M2) | USB-CAN (/dev/ttyACM0 @ 921600) | M1 大臂 DM10010L (slave 0x01) + M2 工具頭 DM4340_48V (slave 0x02)；廠商驅動 header-only，由獨立服務 `cleaning_arm/motor_api` 使用，TCP :9527 對外。washrobot 透過 `arm_cmd_` 跨 process 下指令 (127.0.0.1:9527)。整個專案唯一走 CAN 的裝置 |
| `SD76_length_meters` | 計米器 × 3 | Modbus-TCP (USR_M 感測 bus, .34) | 左 (USR_M.34 slave 1) / 右 (USR_M.34 slave 2) / 中間 (USR_M.34 slave 4, 未安裝)；2026-05-15 re-layout 全部 SD76 移到此 bus，int32 讀取，支援 pause/resume/zero。2026-06-05 起此 bus 多了 PQW slave 12（進水球閥）共用 |
| `DSZL_107` | 張力感測器 × 2（X518 採集板） | Modbus-TCP (獨佔 gateway) | 左 (USR_C.32 slave 1) / 右 (USR_D.33 slave 1)，各獨佔一條 RS485 bus；scale factor 預設 0.01（待實機校正）。Washrobot 透過 `crane_cmd_("tension")` 跨 PI 拿 kg。 |
| `CLV900_inverter` | 變頻器 × 1 | Modbus-TCP (USR_A.30 slave 3) | 中間絞盤變頻器，控制 bus 上（未安裝） |
| `SE3_inverter` | 士林變頻器 × 2 | Modbus-TCP (USR_A 控制 bus) | 左 (USR_A.30 slave 1) / 右 (USR_A.30 slave 2)；2026-05-07 取代原 ZS_DIO_R_RLY 繼電器；2026-05-15 re-layout 右 SE3 從 USR_B 移到 USR_A、slave 1→2；hold 預設 20Hz / 自動運動 30Hz；reg 0x1101 控制位元、0x1002 頻率（RAM）、0x100A 輸出頻率 |
| `QX_DO24` | 4 路 PWM 輸出模組 × 1（新硬體，2026-08） | Modbus-TCP (RS485_3 .22 **slave 9**) | 四川旗芯 QX-DO24，4 通道獨立占空比/頻率/控制（0=關/65535=持續輸出/1~65534=脈衝數）。**安全限制（driver 強制）**：占空比鎖 5~10%（5%=停止/10%=全速）、頻率鎖 50Hz——兩者連動，只鎖一個等於沒鎖。`Linux_test` menu 34 + Web GUI「PWM 控制」panel 已接（`pwm set/save/status`）。**bench 用廠商工具 USB-485 直連驗證過**：通道1 @ 50Hz / 占空比 5~10% 驅動馬達成功。<br>**slave 沿革（2026-08-28 per user 已改為 9）**：原本選 6 的前提是「v2 的 JC100 只用 1~4」，但 2026-08-27 把吸盤編號改成 5-8 之後 **slave 6 就撞上右腳下吸盤的真空表**，bench 出現 `[ERR] [QX:6] device rejected FC 0x10: err 0x7C`（0x7C 不是合法 Modbus exception code，那是 JC100 的回覆被 PWM driver 撿走）。撞號不只是雜訊：FC 0x10 會把 write-multiple 打進 JC100 的組態暫存器，而 JC100 壓力值是步伐的放腳判準 → 掉落風險。user 已用 USB-485 直連把模組改成 **slave 9**（cli_22_ 上 5-8 JC100／10,11 DY500／13 XKC／14 DM2J，9 是空的）。<br>**波特率（2026-08-28 per user 更正）**：模組 115200，且 **.22 這條 bus 上所有裝置都是 115200** —— 先前記載的「其他裝置 9600、必須把模組改回 9600」**不正確**，已作廢。<br>✅ **2026-08-28 已證實接在 gateway 上**：`pwm status` 有回應（`ch1=5,50,65535,1 ch2=... ch3=ERR ch4=50,1000,0,0 duty_min=5 duty_max=10 freq_lock=50`）。⚠️ 兩件待追：`ch3=ERR`（其餘三通道正常）；`ch4` 存著 `duty=50/freq=1000`，**在 driver 安全鎖（5~10% / 50Hz）之外**，那是模組殘留的廠商測試組態，啟用 ch4 前要先覆蓋。📌 `init()` 的 `presence not probed` 永遠證明不了這件事 —— 它不發包 |

**未使用：**

| Class | Description |
|---|---|
| `DIHOOL_control` | 馬達定位控制器，已編譯未整合 |
| `ZS_DIO_R_RLY` | 8CH 繼電器模組（之前用作吊機左右收/放繩，2026-05-07 改用 SE3_inverter；class 保留供未來其他用途） |

### Driver Initialization Pattern

All device drivers support two initialization modes:
```cpp
// Mode A: create internal TCP connection
bool init(const std::string& ip, int port, int ID, bool debug = false);

// Mode B: share external TCP connection (recommended for multiple devices on same controller)
bool init(TCP_client& extClient, int ID, bool debug = false);
```

### Concurrency Model

- `TCP_client`: background `reconnectLoop()` monitor thread (500ms polling), `std::mutex socket_mtx` for thread-safe socket access
- `WT901BC_TTL`: dedicated `_worker_thread` for continuous serial read, `std::atomic<bool> read_error` flag
- All shared state uses `std::atomic<>` or mutex protection

## Testing

There is no automated test framework. Testing is done interactively via `Linux_test/main.cpp`, which provides a menu-driven console interface to exercise each device command (enable, disable, zero, position, speed, home, stop, etc.).

To test a device, deploy `Linux_test` to the target machine and run interactively. The test connection address defaults to `10.0.0.42:4001` in the test harness.

## Key Conventions

- **Language:** C++11/14/17, no external dependencies beyond POSIX/WinSock and the C++ standard library
- **Platform guards:** `#ifdef _WIN32` / `#else` used throughout `TCP_client` and `Serial_port` for platform-specific code
- **Modbus slave IDs:** Each physical device has a fixed slave ID baked into the driver call; check the `main.cpp` of each application for the mapping
- **Units:** Motor positions use pulse counts (`int32_t`) or cm (`double` for DM2J steppers); pressure in 0.1 kPa units; weight as `int32_t` and `float`; temperature in °C (`double`); angles in degrees (`double`)
- **Code comments:** Often written in Traditional Chinese (校零=calibration, 觸發=trigger, 回零=home)
- **Error handling:** Drivers store last valid reading (e.g., `_last_pressure`, `lastValidWeight`) for graceful degradation on connection loss
- **Debug logging:** Conditional hex dump output via `printHex()` / `log_hex()`, enabled by `debug` flag in init

## Coding Style

### 函式回傳值規範（Return Value Convention）

所有 bool 函式統一遵循以下規範：

- 函式執行**成功（無異常）**時，回傳 `false`
- 函式執行**失敗（有錯誤）**時，回傳 `true` 或具體的 error code

```cpp
// ✓ 正確寫法
bool init(const std::string& ip, int port) {
    if (!client->connectToServer(ip, port))
        return true;   // error: connect failed
    return false;       // success
}

// 呼叫端
if (drv.init("192.168.1.20", 4001)) {
    std::cerr << "init failed" << std::endl;
    return 1;
}
```

> **注意：** 此規範與常見的 `true=success` 相反，新增或修改函式時務必遵守。

### 溝通語言與註解語言

- 溝通使用**繁體中文**
- 程式碼註解使用**英文**

### 物件內 function 區塊分隔註解

在 class 中，不同性質的 function 群組之間須加上區塊分隔註解：

```cpp
//=========== init ===========

//=========== control ===========

//=========== read ===========

//=========== utility ===========
```

### Log 格式規範（user_lib 統一）

`user_lib/` 所有驅動的 log **必須**透過 `user_lib/log_utils.h` 的巨集輸出，格式固定：

```
[HH:MM:SS.mmm] [LEVEL] [DEVICE:ID] <message>
```

**Levels：**

| Macro | Level | 用途 |
|---|---|---|
| `LOG_ERR(tag, fmt, ...)` | ERR | 嚴重錯誤（連線斷、CRC 錯、timeout 超限）|
| `LOG_WRN(tag, fmt, ...)` | WRN | 警告（重試、異常值但可繼續）|
| `LOG_INF(tag, fmt, ...)` | INF | 重要流程訊息（動作完成、狀態轉換）|
| `LOG_DBG(tag, fmt, ...)` | DBG | 除錯訊息 |
| `LOG_HEX(tag, note, data, len)` | DBG | Hex dump |

**所有 level 都由 `debug_mode` 成員統一控制：**

- `debug_mode == false`（預設）→ 完全靜默，一行都不印
- `debug_mode == true` → ERR/WRN/INF/DBG/HEX 全部輸出
- 理由：錯誤本來就透過 bool return（true=error）通知呼叫端，log 純為除錯觀察用；驅動庫預設不吵，由使用者決定何時打開

**呼叫端要求：**

- 每個驅動 class 必須有成員 `std::string _log_tag`，在 `init()` 裡設為 `"DEVICE:ID"`（例：`"DM2J:3"`、`"ZDT:5"`、`"TCP"`）
- 每個驅動 class 必須有成員 `bool debug_mode`（所有 LOG_* 巨集都依賴它）
- **禁止**在驅動內直接用 `printf` / `std::cout` / `std::cerr`（regression 守則）

**範例：**

```cpp
LOG_ERR(_log_tag, "PPR read failed");
LOG_INF(_log_tag, "target %.3f cm -> %d pulses", pos_cm, pulses);
LOG_DBG(_log_tag, "status=0x%08X", st);
LOG_HEX(_log_tag, "TX", buf, len);
```

輸出範例：

```
[14:32:01.123] [INF] [DM2J:3] target 15.500 cm -> 6200 pulses
[14:32:01.145] [DBG] [DM2J:3] TX 03 03 00 5F 00 02 35 86
[14:32:01.178] [ERR] [DM2J:3] Motion timeout
```

規範範圍：`user_lib/` 資料夾內。`main.cpp` / 應用層的 log 不受此約束（但建議對齊）。
