# AI-2:v3 Manual「吸盤推桿」卡整理(per user 2026-09-17)

> 契約新增(已部署本體 + fake_robot):`zdt_power all on|off` → `OK zdt_power all=on|off`;失敗 `ERR zdt_power_fail slaves=5,7`;
> 壞參數 `ERR usage:zdt_power_<5..8|all>_<on|off>`。單支 `zdt_power <5..8> on|off` 不變(回 `OK`)。
> status 新欄位 **`zdt_pwr=1111`**(slave 5..8 順序,1=使能;**是最後一次指令的狀態,不是回讀**——開機預設 1111)。fake_robot 已鏡像。
> status 新欄位 **`zdt_skip=-`** / `zdt_skip=7` / `zdt_skip=5,7`:目前被 `zdt_disable` 排除在整組指令外的推桿(真值在本體)。fake_robot 已鏡像。

## 要做的

1. **RPM 欄要顯示預設值**:現在空白＝用本體預設,但 user 看不到那是多少。把 `status` 的 `pusher_rpm=` / `pusher_rpm_retract=`
   當成欄位的 `value`(伸/收各自),不是 placeholder;user 改了就送、沒改就送預設值,兩者對本體等價。
   「RPM 預設值」那列(`:1319`)因此可以拿掉。
2. **歸零(`:1336`,`zdt_zero`)→ 改名「當前位置歸零」**,下面小字移除;雙重確認照舊。
3. **重歸零(24V 跳電後)(`:1346`)→ 改名「自動歸零」**,下面小字移除。整段只留 `zdt_home all` 那顆(改名後的「自動歸零」);
   「① 真失能,準備手推 / 取消(只使能)」那組手動流程**拆出來**變成第 4 點的失能按鈕,不再綁在歸零流程裡。
   ⇒ 歸零只剩兩種:**當前位置歸零**、**自動歸零**。
4. **新增失能/使能按鈕**(真斷電 torque-off,`zdt_power`):
   - 單支:5/6/7/8 各一組「失能 / 使能」(`zdt_power <s> off|on`)
   - 全部:「全部失能 / 全部使能」(`zdt_power all off|on`)
   失能狀態用 status 的 `zdt_pwr=` 顯示(標明「指令狀態」,不是回讀)。
   ⚠️ 失能後再使能,馬達會回到**當時的目標位置**——若人手推過,使能瞬間會彈;先「當前位置歸零」再使能(原小字就是在講這個,拿掉小字後請在使能鈕的 title 留一句)。
5. **「納入群組 / 排除群組」兩顆鈕移除,改成四個打勾框(per user 2026-09-17)**:
   - 單支那區每支(5/6/7/8)前面一個 **checkbox「整組指令包含這支」**,預設勾。
   - 勾/取消勾 → 立刻送 `zdt_enable <s>` / `zdt_disable <s>`;框的**顯示狀態以 status 的 `zdt_skip=` 為準**(不是自己記的),
     這樣真值在本體:腳本、`pusher all`、急停收腳都會跳過被取消勾的那支,GUI 重開也不會丟。
   - 整組指令(`pusher all …`)**照送不改**——排除誰由本體決定,GUI 不要自己拆成單支去送(收腳的四支同步破真空時序會被拆掉)。
   - 沒勾的那支要有明顯提示(灰底/刪除線 +「整組指令會跳過」);單支指令對它照樣可用。
   - `zdt_release_stall`(堵轉解除)那顆保留,放到單支區旁邊。

## 驗收

check_console 全綠、gui_v3_check 全過(新增:`zdt_power all off` → `OK zdt_power all=off`)、report 0 not modelled。
照舊不 commit,完成 SendMessage `agent-ai-db`。
