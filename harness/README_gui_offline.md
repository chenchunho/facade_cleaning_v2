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

## v3 自動驗收：`harness/gui_v3_check.js`（2026-09-14）

```bash
./harness/gui_offline.sh stop      # 它要自己起一個乾淨的 fake_robot（埠固定 5001/5002/9527，不能並存）
node harness/gui_v3_check.js       # 或 cd web_backend && npm run check:v3
node harness/gui_v3_check.js --only evt,flow        # 子集：boot / evt / flow / mission / safe / report
node harness/gui_v3_check.js --attach 8081          # 掛到現成的 v3 server（⚠️ 假機器狀態要乾淨，否則 ③⑥ 會紅）
```

jsdom 載入 `web_backend/public_v3/index.html`、開**真的** WebSocket 到 `server.js`、橋到 fake_robot；
每一項都是**按按鈕、讀 DOM 文字**，走的是平板會走的路，不是抄函式出來測。
`confirm()` 一律模擬成自動接受（kiosk 最壞情況）——閘門不能靠它。
50 項涵蓋：開機 / 十種 `EVT mission` / 作業流程 ①–⑧ 與兩道閘門 / mission 從按鈕起跑、roll 兩次進 SAFE /
tension 拒絕解除、`emergency_stop`＝SAFE src=user、`sim safe` / 最後跑 `--report` 要求 **0 not modelled**。
依賴：`web_backend/package.json` 的 devDependency `jsdom`（`cd web_backend && npm install`）。退出碼 1＝有紅。


## 部署到吊機 Pi：`scripts/deploy_web.sh`（2026-09-14）

jim 的慣例：**每次部署前必須更新版號**，他才分得出「看到的是新版」還是「瀏覽器快取」。
腳本把這件事做死，不靠人記得：

```
./scripts/deploy_web.sh              # 蓋版號 → scp → 三方 md5 複驗 → 印版號 + 三個 md5
./scripts/deploy_web.sh --dry-run    # 只蓋版號、只印本機 md5，不送
PI=user@192.168.5.25 PI_PORT=8081 ./scripts/deploy_web.sh   # 覆蓋目標（預設就是這組）
```

- 版號 `v3-YYYY.MM.DD-HHMM` 台灣時間，sed 進 `web_backend/public_v3/index.html` **唯一**一行
  `var CONSOLE_VER = '…'`（頂欄右側 `#ver-tag` 由 JS 從它填入，HTML 裡沒有第二份）。
  repo 裡的檔跟著變，commit 時一起進。
- **會擋的情況（退出非 0、不送）**：來源行不是剛好一行／蓋章沒落地／任一 `<script>` 段語法錯。
- Pi 端先 scp 到暫存名再 `mv`，頁面不會讀到寫一半的檔；Pi 上目錄是 `~/projects/facade_cleaning_v2/web/`（不是 `web_backend/`）。
- 三方 md5：本機檔 / Pi 檔 / `curl http://192.168.5.25:8081/` 送出的頁面，三個不相等就退出 1
  （常見原因：:8081 不是這個 server、`PUBLIC_DIR` 指錯目錄）。server.js 是靜態檔服務，不需重啟。
- ⚠️ 註解裡不要寫字面的 `<script>` 標籤：語法閘用正則切 script 段，會被騙（09-14 踩過）。
