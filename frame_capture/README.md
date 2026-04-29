# frame_capture

持續解碼 RTSP 攝影機 → atomic 寫入最新 JPEG，供 `detect_server.py` 讀取做窗框偵測。

規範權威：`.claude/camera_obstacle_plan.md`

---

## 為何存在

`detect_server.py`（YOLOv8 + Hailo NPU）的 request 是一個檔案路徑。washrobot C++ 程式下指令前要有一張「當下最新」的影像在 `/tmp/cam_latest.jpg`。方法有三：

| 方案 | 延遲 | CPU | 複雜度 |
|---|---|---|---|
| 每次 query 才 `ffmpeg` 拍一張 | 🔴 500ms~1.5s（TCP + I-frame）| 🟡 每次 spawn | 低 |
| **本腳本（持續解碼、atomic 寫）** | 🟢 < 200ms | 🟢 ~5-10% CPU | 低 |
| C++ + OpenCV 直接拉 | 🟢 最低 | 🟡 | 🔴 build 肥 |

取中間：用 Python + OpenCV 跑一條解碼 loop，washrobot 不用等。

---

## 啟動

預設值適用現有硬體（Xiongmai 相機 @ 192.168.1.10，子碼流）：

```bash
python3 frame_capture.py
```

自訂：

```bash
python3 frame_capture.py \
  --url "rtsp://192.168.1.10:554/user=admin&password=&channel=1&stream=1.sdp?" \
  --out /tmp/cam_latest.jpg \
  --fps 10 \
  --quality 85
```

### CLI 參數

| Flag | 預設 | 說明 |
|---|---|---|
| `--url` | 子碼流 RTSP URL | 覆蓋用 |
| `--out` | `/tmp/cam_latest.jpg` | 輸出 JPEG |
| `--fps` | `10` | 每秒最多寫幾張（節流）|
| `--quality` | `85` | JPEG quality 0~100 |
| `--reconnect-delay` | `2.0` | 斷線重試間隔（秒）|
| `--log-interval` | `10.0` | stderr 統計印出間隔（秒）|

---

## 實機驗證

### 1. 先用 ffmpeg 確認 RTSP 通

```bash
ffmpeg -rtsp_transport tcp \
  -i "rtsp://192.168.1.10:554/user=admin&password=&channel=1&stream=1.sdp?" \
  -frames:v 1 -f image2 /tmp/cam_test.jpg -y

file /tmp/cam_test.jpg        # 應為 "JPEG image data"
```

### 2. 跑 frame_capture.py

```bash
python3 frame_capture.py
```

預期輸出：
```
[frame_capture] target rtsp://192.168.1.10:554/...
[frame_capture] output /tmp/cam_latest.jpg @ max 10 fps (jpeg q=85)
[frame_capture] connected
[frame_capture] writes=98 (9.8 fps), latest_age=42ms
[frame_capture] writes=99 (9.9 fps), latest_age=38ms
...
```

### 3. 另一 terminal 看檔是否持續更新

```bash
watch -n 0.5 'ls -la /tmp/cam_latest.jpg'
# mtime 應每 100ms 左右更新
```

### 4. 拔網線測自動重連

拔掉攝影機網線 → 應該看到 `read fail` → `reconnecting`；插回 → 應該自動 `connected`。

---

## 部署（systemd 背景跑）

`/etc/systemd/system/frame_capture.service`：

```ini
[Unit]
Description=RTSP frame capture for washrobot window frame detection
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=nexuni
ExecStart=/usr/bin/python3 /home/nexuni/washrobot_new_PI/frame_capture/frame_capture.py
Restart=on-failure
RestartSec=2
StandardError=append:/var/log/frame_capture.log

[Install]
WantedBy=multi-user.target
```

啟用：
```bash
sudo systemctl daemon-reload
sudo systemctl enable --now frame_capture
sudo systemctl status frame_capture
tail -f /var/log/frame_capture.log
```

---

## 故障排除

| 現象 | 可能原因 | 解法 |
|---|---|---|
| `open fail` 一直重試 | 相機不通 / 錯 IP / 錯帳密 | `ping 192.168.1.10`、檢查 URL |
| `read fail` 頻繁 | 網路不穩 / 相機 CPU 過熱 | 確認 PoE Switch 正常 |
| fps 遠低於 10 | 子碼流本身 fps 低 | 改用主碼流（`stream=0`）但會吃更多 CPU |
| latest_age 飆高（> 1000ms）| 寫檔卡住 / 磁碟滿 | `df -h`、看 `/tmp` 是否可寫 |
| CPU 用量高 | 主碼流 + 高 fps | 用 `--fps 5` 降低頻率 |

---

## 參數備註

- `fps=10` 對窗框偵測很夠（機器下移速度 < 5cm/s）
- `quality=85` JPEG 對 YOLO 偵測窗框夠用（再高是浪費）
- RTSP over **TCP** 比 UDP 穩，script 已設 `rtsp_transport;tcp` 環境變數
- `CAP_PROP_BUFFERSIZE=1` 讓 OpenCV 只保留最新 frame（降低延遲）

---

## 不要做的事

- ❌ 不要在 washrobot C++ 裡直接呼叫 ffmpeg/OpenCV 拉 RTSP（build 很肥，且每次呼叫慢）
- ❌ 不要共享 `/tmp/cam_latest.jpg` 給遠端機器（改用 HTTP 或 WebSocket）
- ❌ 不要同時跑兩個 instance（會互相覆蓋）
