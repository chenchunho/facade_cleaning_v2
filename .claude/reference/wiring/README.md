# wiring/ — 電控箱單線接線圖(2026-09-13)

| 檔 | 內容 |
|---|---|
| `body_cabinet.svg` | 本體電控箱:AC → 四顆電源 → 24V/48V/5V/57.6V 匯流排 → 各裝置;三段 RS485、CAN/UART、PWM、Ethernet |
| `crane_cabinet.svg` | 吊機電控箱:AC → 三台變頻器 + 5V;四段 RS485、X518 原生 Ethernet |
| `gen_wiring_svg.py` | **產生器**。座標用程式算,線路零誤接。改接線就改這支重跑,不要手改 SVG |

**慣例**:實線依電壓分色;● 實心點 = 接點,交叉無點 = 不相連;`?` = 尚無資料(供電來源 / 段 / 位置)。
通道與 slave 由 `app/WASH_ROBOT.h`、`Crane_control_PI/main.cpp` 抽出。權威敘述在 `../../HARDWARE.md` §3.2。
檢視版(深色模式可用):https://claude.ai/code/artifact/ff3cf70a-add5-4fec-9118-74066159a8d1

⚠️ 這是**信號層單線圖,不是施工圖** —— 端子台/線號/線徑/斷路器全無資料(HARDWARE.md §3.2C)。
