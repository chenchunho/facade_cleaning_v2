# 各程式重點注意事項（交接用）

寫給**接手這個專案的下一位同事**。這份只講「每支程式各自最容易踩、踩到後果最嚴重」的事，
不是完整規格。

- 系統整體背景 / 歷史脈絡 → `ONBOARDING.md`
- 操作步驟（怎麼開機、按哪顆按鈕）→ `.claude/runbook.md`
- 運動流程規格 → `.claude/motion_flow.md`
- 每次改動的來龍去脈 → `.claude/changelog.md`

> ⚠ 本文寫於 **2026-08-28**。程式會繼續改，**跟程式碼不一致時以程式碼為準**。

---

## 0. 先看這個：跨程式的共通地雷

### 0.1 回傳值慣例是反的（而且有一個例外）

專案規定 **`false` = 成功、`true` = 錯誤**（見 `CLAUDE.md`）。`user_lib/` 幾乎所有驅動都遵守。

**唯一例外：`QX_DO24`（PWM 模組）是 `true` = 成功。**

這個不一致實際害過人：Linux_test menu 34 剛寫好時整個成功/失敗顯示是反的——寫入成功印
`[WARN] error`、失敗印 `[OK]`。`QX_DO24.h` 開頭有大字警告，寫任何呼叫端前先確認極性。

### 0.2 RS485 bus 配置（2026-08-27 起）

只有兩條 gateway，都是 port 4001：

| Gateway | Slave |
|---|---|
| `192.168.1.20` (`cli_20_`) | ZDT 推桿 **5~8**、PQW 繼電器 **12** |
| `192.168.1.22` (`cli_22_`) | JC-100 真空表 **5~8**、XKC 水位 **13**、DM2J 上滑台 **14**、~~QX PWM 6~~（停用中）|

⚠ **吸盤/真空表的 slave 在 2026-08-27 從 1-4 改成 5-8。** 程式碼裡還有數十處註解沿用舊編號在
描述 bus 競爭，**以 `WASH_ROBOT.cpp` `init()` 上方那段為準**。

⚠ **一條 RS485 只能有一個波特率。** 想加新裝置時，要把新裝置改成配合 bus（目前 9600），
不要把整條 bus 改去配合新裝置——那會動到所有既有且正常運作的裝置。

### 0.3 同一條 bus 上的 Modbus 交易必須是 atomic

`TCP_client` 的 mutex **只保護單次呼叫**。用 `sendData()` + `receiveData()` 兩次分開呼叫，
中間會放開鎖，另一個執行緒可以把請求插進去，**兩邊的回覆被錯誤的呼叫者讀走**。

要用 `TCP_client::sendAndReceive()`（drain→send→recv 全程握鎖）。已遷移：`JC_100_METER`、
`SD76_length_meters`、`SE3_inverter`、`CLV900_inverter`、`QX_DO24`。**尚未遷移**：`DM2J`、
`ZDT`、`PQW`、`DSZL`、`DY500`、`XKC`。

**為什麼嚴重**：JC-100 的壓力值是步伐判斷「這一側還吸得夠牢、可以放開另一側」的依據，
污染它是**掉落風險**，不只是通訊雜訊。

### 0.4 目前被 gate 停用的功能（不是壞掉，是刻意關的）

| 常數（`WASH_ROBOT.h`）| 狀態 | 原因 |
|---|---|---|
| `PWM_ENABLED` | `false` | QX-DO24 的 slave 6 撞 JC-100 slave 6，見 §5 |
| `STEP_SYNC_ARM_CLEAN_ENABLED` | `false` | 上滑台還沒裝好，log 一直噴 `DM2J:14 writeMulti no response` |

兩個都在 header 註解裡寫了重新啟用的前提。**不要只把 flag 改成 true 就上機**。

---

## 1. `facade_cleaning_v2/` — washrobot 主程式（TCP :5001）

跑在 washrobot Pi (192.168.1.100)。所有硬體、運動邏輯、背景執行緒都在
`user_lib/WASH_ROBOT.{h,cpp}`；`main.cpp` 只有 TCP server 跟指令分派。

### ⚠ 核心安全不變式
**任一時刻至少有一側（2 顆吸盤）吸住撐住機體，另一側才可以鬆開移動——絕不同時 4 顆全放。**

**唯一刻意打破這條的是 `do_step_sync_`（同步步伐）**：放繩期間 4 顆全部放開，完全靠吊機繩索
承重。動到相關邏輯前務必記得這個前提不一樣。

### 其他注意
- **`cmd_init_impl_` 不伸任何推桿** — init 後機器沒有任何抓力，必須靠繩支撐，
  **init → attach 之間不能 idle**
- **真空判準是「每側 ≥1 顆吸好就繼續」**，不是 4 顆或整組全吸。最壞情況機體只靠 2 顆
  （每側 1 顆）撐在半空。這是已徵得同意的取捨，不是 bug
- **`pusher right|left` 不是 `feet` 的別名** — 4 顆是實體獨立的 ZDT 馬達，`right`={5,6}、
  `left`={7,8}，步態邏輯依賴它們分側動作。Web GUI 不再分開顯示只是操作面的決定，
  **後端分支不能當成 alias 砍掉**（真空閥則相反，實體只剩一顆 CH1，是真的分不開）
- **重心校正 / 窗框避障目前是 stub**（回 `ERR removed_in_v2`），是暫時停用不是廢棄

---

## 2. `Crane_control_PI/` — 吊機主控（TCP :5002）

跑在 crane Pi (192.168.1.101)。

### ⚠ 變頻器型號用編譯期巨集切換
```cpp
#define CRANE_VFD_IS_SE3 1   // 1 = 士林 SE3-210 / 0 = 台達 MH300
```
目前 bench 跑 **SE3**。兩顆 driver 的 public API 是同簽名 drop-in，切換只要改這一行 + 重編。

MH300 已實機測試過，但**方向 / DC brake / fault code 這幾項跟現場接線、裝機狀況綁定，
每次重新安裝都要重新確認一次**——不是「之前測過就永遠有效」的那種項目。

### ⚠ 已知未修的 bug：`abort_flag` 沒重置
`cmd_side_measured`（v2 step 專用）進入時**沒有**把全域 `abort_flag` 重置成 `false`，
但它的三個兄弟函式（`motion_rope` / `cmd_roll_correct` / 另一個 MotionScope 函式）都有。

**症狀**：跑到一半按 stop 之後，之後不管做什麼都變成「無法連線吊機」，**必須重開
`Crane_control_PI` 整個程式**才會恢復（重開讓 static 變數重新初始化）。

**修法**：在拿到 `motion_mtx`/`MotionScope` 之後補一行 `abort_flag = false;`。
（已確認 2026-08-28 仍未修。）

---

## 3. `cleaning_arm/` — 清潔手臂 motor_api（TCP :9527）

**獨立 process**，不是 washrobot 的一部分。washrobot 透過 `arm_cmd_()` 連
`127.0.0.1:9527` 跨 process 下指令。M1 = DM10010L（大臂擺動）、M2 = DM4340_48V（刷頭朝向）。

### ⚠ 這隻手臂沒有平衡點
重心在最遠端，整個可動範圍內**沒有天然平衡點**（倒單擺）。跟「馬達沒力頂多停在原地」的
直覺相反——**M1 一旦沒出力，手臂會直接自由落下**。

手動測試 / 校準（DISABLE 後用手轉）時**旁邊一定要有人扶著**，等 `HOLD` 下完確認撐住了才放手。

### ⚠ MIT 的 `kd` 絕對不能超過 5.0
CAN 協定把 `kd` 編成 12-bit 欄位，規格範圍 `[0, 5]`。給超過的值**不會報錯也不會被夾住，
而是 bit 溢位變成一個不相關、通常小很多的值**（kd=12 → 實際 2.00）。

這是先前 M1 在 DEPLOY/PARK 暴衝、震盪、軟掉的**根本原因**——當時一直以為是硬體問題，
調高 kd 想加阻尼，結果溢位後阻尼反而更小。`damiao.h` 現在已加夾限，但**協定上限仍是 5.0**，
所以所有 `hold_kd`/`park_kd` 都寫死 5.0。**覺得阻尼不夠時加 kd 沒有用，要往 kp / 速度 /
前饋力矩想。**

### 🚫 重力模型目前是錯的（工具頭換過，尚未重新量測）
`M1_GRAVITY_K = 20.87` / `M1_GRAVITY_PHASE_RAD = 3.317` 是**舊工具頭**擬合出來的。

工具頭換過（輕了 633g）之後，bench log 顯示：
- `pos=0.5835` 靜止 HOLD 只需 **+3.17 Nm**，模型卻算出 **−8.28 Nm**（正負號相反、量級差 2.6 倍）
- PARK 一起步 vel 就衝到 **−1.53 rad/s**（`park_speed` 只有 0.35），手臂是被過大的 tau_ff 往下拉

**算過了：633g 最多只能解釋約 2 Nm（K 從 20.87 → 18.4），落差是它的五倍以上**，
代表重心方向 φ 也變了，**不能只等比例縮 K**。需要 4 個不同角度的實測點重新解 K 和 φ。

⚠ **在重新量測完成前，PARK 有實際危險**，而且 `go_home_slot` 這條路徑**沒有速度安全閥**。

### 其他
- **PARK 對已 disable 的 M1 曾經完全沒反應**（`if (m1_.enabled)` gate 住，成功 PARK 結尾會
  disable，所以第二次以後什麼都不做還回 OK）。已修成「離 home 超過 0.15 rad 就重新 enable
  再 home」
- M2 有約 0.08 rad 穩態誤差，目前視覺可接受、未處理

---

## 4. `web_backend/` — Node.js Web GUI（HTTP :8080）

**跑在 crane Pi (.101)，不是 washrobot Pi。這是刻意的**——washrobot 在半空中掛掉時，
GUI 仍可透過 crane 手動收繩救援。

- 後端只做**純轉送**：把指令原封送到 washrobot :5001 / crane :5002 / arm :9527，
  不解析內容。所以**前端的任何檢查都只是即時回饋，不是防護**——raw command 面板可以
  繞過前端直接送任何指令
- 分頁只有 **home / manual / settings** 三個。攝影機介面已於 2026-08-27 **永久移除**
  （前端 panel + app.js + server.js 反向代理 + CSS 全刪），**不要再加回來**
- 加/刪跨檔案功能時，**要 grep 掃一遍被刪識別字的所有引用點**。曾經刪掉 `CAMERAS` 常數卻
  漏掉啟動 log 那行的引用 → web_backend 一啟動就 ReferenceError 掛掉，編輯當下不會報錯，
  只有下次重啟服務才炸

---

## 5. `Linux_test/` — bench 互動測試工具

單一裝置的手動測試選單，跟生產程式**共用 `user_lib/` 驅動但完全獨立執行**。

### ⚠ 它不受生產程式的安全 gate 保護
`PWM_ENABLED` 之類的常數在 `WASH_ROBOT.h` 裡，**Linux_test 讀不到**。所以生產程式已經擋掉的
危險操作，在 Linux_test 裡照樣送得出去。

### 🚫 menu 34（QX-DO24 PWM）目前不可用預設值執行
預設是 `.22` + **slave 6**，但 **slave 6 現在同時是 JC-100 真空表**（吸盤改號 1-4→5-8 的
連帶副作用）。`setPWM_Freq` 用的 **FC 0x10 是 write-multiple，會真的寫進 JC-100 的組態
暫存器**（含 slave ID / 波特率）。JC-100 壓力值是步伐的放腳判準，**弄壞是掉落風險**。

bench 已經撞到過：
```
[ERR] [QX:6] device rejected FC 0x10: err 0x7C
```
`0x7C` 不是合法 Modbus exception code（標準只到 0x0B）——那是 JC-100 的回覆被撿走。

**重新啟用 PWM 的三個前提（缺一件就會再撞或再吵）：**
1. 用 USB-485 直連把模組 slave ID 改到 `cli_22_` 上真正空的號（可用 1~4、9、15+），
   並同步改 `PWM_SLAVE`
2. 同樣直連把波特率從 115200 改回 **9600**（寫 `0x21=3`）配合 bus，
   **必須在接上 bus 之前改完**，否則接上去就無法通訊、也改不回來
3. 把 `PWM_ENABLED` 改成 `true`

### 其他
- **menu 34 離開時刻意不關閉輸出** — 對 PWM 伺服/調速器而言，斷訊號＝失去保持力，
  「離開測試工具」不等於「要求改變實體輸出狀態」。要關就自己下 `off <ch>`。
  ⚠ 這跟繼電器類裝置「預設關閉比較安全」的直覺**相反**，不要一套邏輯套所有裝置
- 測 JC-100（menu 4）之類的讀取時，**先把 washrobot 主程式停掉**。USR-TCP232 是透傳閘道，
  多個 TCP client 同時連著時會把每筆 RS485 回覆**廣播給所有 client**，造成 frame 污染

---

## 6. 沒有自動測試

整個專案**沒有任何自動化測試框架**，也**沒有本機編譯環境**（VS Linux 交叉編譯要 remote build）。

這代表：
- 改完的東西在部署前**沒有編譯過**是常態，**第一步永遠是先 build 確認綠燈**
- 驗證靠 `Linux_test` 手動跑 + bench 實機觀察
- 涉及封包/數值的改動，**用手算或腳本模擬驗證一遍**比較實在（changelog 裡有幾次就是這樣
  抓到 NaN 穿透、頻率截斷、CRC 對不上等問題）
