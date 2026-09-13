# gui_offline — 沒有機器也能開 GUI(2026-09-13)

```bash
./harness/gui_offline.sh          # 起 fake_robot(5001/5002/9527)+ web_backend v2(:8081)
./harness/gui_offline.sh report   # GUI 到底送了什麼 → 程式面需要的功能清單
./harness/gui_offline.sh stop
```
開 **http://localhost:8081**(WSL2 從 Windows 瀏覽器直接開就通)。

## 用途:從 GUI 推導程式面需要的功能

`fake_robot.py` 把**每一條**收到的指令寫進 `harness/gui_cmd_log.txt`,標 `KNOWN`(有模擬)或
`UNKNOWN`(沒模擬 ⇒ 回 `ERR unknown_command`,跟真程式一樣)。點過一輪 GUI 後跑 `report`,
`?? not modelled` 那些就是「GUI 需要、但目前沒人保證存在」的介面。

已知回覆格式是照 C++ 抄的(`cmd_status` / `cmd_relay_status` / 吊機 status / 手臂 `[M1] ...`),
所以面板會有東西、`expect` 正則會過。**狀態會動**:放繩會漸變、吸盤開閥後壓力會往 −66 kPa 收、
`DEPLOY_F` 的 tau 會收斂到目標、rail_sweep 會跑。

## 第一次跑就抓到的

- **web_backend 每 `BRIDGE_PING_MS` 對手臂送 `ping`,但手臂協定只有 7 條指令**(`main_api.cpp:3890`
  回 `ERR unknown command: ping`)。保活目的達到(有回就算),但手臂 log 會被 ERR 洗版。

## 它刻意不做的

安全邏輯、時序、錯誤路徑、真空封合失敗、張力保護 —— 這些要真機器。它只保證「面板會畫、按鈕會來回」。
