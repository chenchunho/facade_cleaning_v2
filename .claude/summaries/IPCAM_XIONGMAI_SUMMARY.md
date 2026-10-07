# IP 攝影機（XiongMai / 雄邁）Modbus 外設清單摘要

> 來源：2026-09-10 對 `192.168.1` 區網實機掃描 + XM 私有協定（port 34567）直接讀取設定。
> 兩台為 per user 當日新增。**本檔記的是實查值，不是手冊值。**

## 快速索引（要用 RTSP 直接看這裡）

| | 攝影機 1 | 攝影機 2 |
|---|---|---|
| IP | `192.168.1.112` | `192.168.1.113` |
| 帳號 / 密碼 | `admin` / **（空密碼）** | `admin` / **（空密碼）** |
| 主碼流 RTSP | `rtsp://192.168.1.112:554/user=admin&password=&channel=1&stream=0.sdp?` | `rtsp://192.168.1.113:554/user=admin&password=&channel=1&stream=0.sdp?` |
| 子碼流 RTSP | `rtsp://192.168.1.112:554/user=admin&password=&channel=1&stream=1.sdp?` | `rtsp://192.168.1.113:554/user=admin&password=&channel=1&stream=1.sdp?` |
| MAC | `00:12:34:a2:73:28` | `00:12:34:a2:73:3a` |
| 序號 | `03bf8f2a512f02c9` | `73b052d7a8a20879` |

🔴 **RTSP 網址三個容易錯的點：**
1. `password=` 後面**留空**就是密碼（因為 admin 是空密碼），照原樣貼。
2. **結尾的 `?` 不能省** —— XM 平台少了它 DESCRIBE 直接回別的錯誤；實測有 `?` 才回 `200 OK`。
3. 網址含 `&`，在命令列（ffplay/ffmpeg）**一定要用引號包起來**，否則被 shell 當背景符切掉。
4. `stream=0` 主碼流（1080P/4Mbps）、`stream=1` 子碼流（HD1/1Mbps）。要省頻寬／多路併看用子碼流。

DESCRIBE 實測（2026-09-10）：`stream=0` 與 `stream=1` 皆回 `RTSP/1.0 200 OK`。
Dahua 風格的 `/cam/realmonitor?...` 路徑回 `401`，**這個平台不吃**，別用。

## 機型與韌體（兩台完全相同）

| | |
|---|---|
| 平台 | **XiongMai（雄邁）** IPC，H264DVR 1.0 RTSP server |
| 硬體 | `IPC_GK7201V200_G3H_S38`（海思 GK7201V200 SoC） |
| 韌體 | `V5.00.R02.000929ST.10010.141000.0020000` |
| Build | `2024-04-08 13:26:36` |
| 感測 | 1/2.7" 級，最高 1080P（1920×1080）|

## 開放埠

| Port | 用途 |
|---|---|
| 80 | Web 介面（`<title>Web Viewer</title>`，需 ActiveX/OCX 或 XM 專用 App）|
| 554 | RTSP（見上）|
| 8000 | XM app 服務 |
| **8443** | HTTPS（SSLPort，設定值；未實測憑證）|
| **8899** | XM 私有協定（onvif/app 探測用）|
| **34567** | XM 私有控制協定（TCP，本摘要靠它讀設定）；UDP 對應 34568 |

## 網路設定（實查）

| 欄位 | 兩台相同 |
|---|---|
| 遮罩 | `255.255.255.0` |
| 閘道 | `192.168.1.1` |
| DNS | 🟡 **2026-09-10 改為 `8.8.8.8` / `8.8.4.4`**（原為 `114.114.114.114`）|
| 監看模式 | TCP，TCPMaxConn=10 |
| **NTP** | 🟡 **2026-09-10 已開啟**（`pool.ntp.org` + `time.windows.com`，tz=14），**但此網段目前無對外網路 ⇒ NTP 對不到、實質無效**（實測：故意設錯 1 小時後觸發 NTP，時間沒被拉回）。有 internet uplink 之前它是裝飾 |
| **時區** | tz=**14 = GMT+8**（Beijing/Taipei）。🔴 **實測 XM 把牆鐘時間直接存 RTC，改 tz 不會位移畫面時間 —— tz 只在 NTP 對時那一刻才用**。所以目前 tz 值無實際作用 |
| **時間來源** | 2026-09-10 由本機（GMT+8）手動推送，兩台一致。之後靠各自石英鐘走，會慢慢漂；要長期準確需先給這網段 internet uplink（NTP 才會生效）|
| 語言 | 簡中 |
| 制式 | **PAL / 25fps** |

## 編碼（主/子碼流，兩台相同）

| | 壓縮 | 解析 | FPS | 位元率 | 控制 | GOP | 音訊 |
|---|---|---|---|---|---|---|---|
| 主碼流 | H.264 | 1080P | 25 | 4096 kbps | VBR | 2 | 開 |
| 子碼流 | H.264 | HD1 | 25 | 1024 kbps | VBR | 2 | 開 |

📌 GOP=2 秒（PAL 下 = 50 frame 一個 I-frame）。若下游要低延遲或逐幀分析，這個值偏大，可調小。

## 🔴 安全注意（照 CLAUDE.md「不對外開放私有埠」的既有立場）

- **admin 空密碼** —— 目前只在區網內、可接受，但**這台若要做遠端存取必須先設密碼**（2026-09-10 未設，per user 未要求）。
- 雄邁平台歷年有多個公開的未授權存取／後門韌體漏洞（`34567` / `8899` 是常見入口）。
  🔴 **`34567` / `8899` / `8000` 絕不可在路由器上做 port forward。** 遠端看一律走 VPN 或反向代理（只轉 RTSP/HTTPS 且加認證）。
- 韌體 build 2024-04，非最新；`GK7201V200` 系列有已知 CVE，區網隔離是目前唯一的緩解。

## 🆕 OSD（2026-10-07 per user 兩支都關掉）

`AVEnc.VideoWidget` 的 `TimeTitleAttribute`（左上時間）與 `ChannelTitleAttribute`（左下 `CAM01`）
各有 `EncodeBlend`（燒進編碼串流＝RTSP／Dashboard 看到的）與 `PreviewBlend`（機身預覽），**四個都設 false**。
理由：鐘沒對時（10-07 兩支停在 09-14 / 09-10，會誤導）、兩支標題都叫 `CAM01`。
- 原設定備份：`config/ipcam/videowidget_{112,113}_before_osd_off_20261007.json`（改前整段，兩支皆 `true`）
- 開回：`python3 scripts/xm_ipcam.py <ip> osd on`（在本體跑；寫前自動備份到 `~/run/cam_osd_backup_*.json`）

## 🆕 多餘功能關閉 + 編碼調整（2026-10-07 per user）

| 設定段 | 改了什麼 |
|---|---|
| `Simplify.Encode` | 主/子碼流 `AudioEnable` → false；**子碼流 GOP 2 → 1 s**（開畫面/重連更快、MSE 播放器需要） |
| `Detect.MotionDetect` / `Detect.HumanDetection` | `Enable` → false（人形偵測是 AI 演算法，吃機身運算） |
| `NetWork.Nat` | `NatEnable` → false（XMeye 雲端 P2P `secu100.net`）。🔴 寫入回 **Ret=603 = 已存、重開機後生效** |
| `NetWork.PMS` | 推播 `push.umeye.cn` → 關 |
| `NetWork.OnlineUpgrade` | `Enable`/`AutoCheck`/`AutoUpgradeImp` → false（**不可讓它自己升級韌體**） |
| `General.AutoMaintain` | `AutoRebootDay` Tuesday 03:00 → `Never` |
| `Record` | `RecordMode` → `ClosedRecord`（本來就沒記憶卡） |
| `Camera.Param` | `EsShutter`（電子慢快門）`0x2` → `0x0`；A/B 實驗**不影響 fps** |

- 備份：`config/ipcam/features_{112,113}_before_cleanup_20261007.json`（改前 27 段整份，兩支完全相同）。
- 重開機（`OPMachine` `{"Action":"Reboot"}`，msgid 1450）後全部保留。
- ⚠️ **重開後兩支子碼流實測 20.0 fps**（`ffprobe` pts 計算 10 s／201 張），重開前量到 25（`nb_read_packets`）。原因未明，已排除慢快門；主碼流 1080P 本來就回報 20/1。20 fps 夠用，沒再追。
- 沒動：曝光/白平衡/日夜切換等影像參數（避免畫面變化）、RTSP 伺服器、UPnP/DDNS/Email/FTP（本來就關）。

## 讀取工具

🆕 **2026-10-07 搬回 repo：`scripts/xm_ipcam.py`**（`<ip> get [Name] | osd off|on | time`，在本體 Pi 上跑）。
原本 XM 私有協定的讀取腳本（login → getConfig）保存在
`Linux_test/xm_ipcam.py`（09-16 隨 `Linux_test/` 移除，git `842e774` 之前仍有）：連 `34567`，20-byte header `ff|ver|00 00|sid(4)|seq(4)|total|cur|msgid(2)|len(4)`，
密碼走 XM「sofia」雜湊（MD5 後每 2 byte 相加 mod 62 映射到 `[0-9A-Za-z]`，取 8 碼）。
登入 1000／讀設定 1042／寫設定 1040／系統資訊 1020／設時間 1450／查時間 1452。
`Ret=100` 成功 / `Ret=203` 帳密錯 / `Ret=607` 無此設定段。
