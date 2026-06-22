# 攝影機窗框避障 — 規劃與參數記錄

> **狀態：** 規劃階段，程式碼未實作
> **建立：** 2026-04-23（Sadie）
> **權威：** 本文件（待實作後部分內容遷移到 motion_flow.md）

---

## 1. 系統架構

```
┌─ washrobot Pi (192.168.1.1100) ─────────────────────────────┐
│                                                            │
│  frame_capture.py (持續跑) ◀──RTSP──  camera (.1.10:554)  │
│  └─ atomic write /tmp/cam_latest.jpg                      │
│                                                            │
│  washrobot_new_PI (C++, TCP :5001)                        │
│  └─ FrameAnalyzer ──UDP──▶ detect_server.py (:5040)       │
│                             (Hailo NPU, 已由他人寫好)      │
│                                                            │
└────────────────────────────────────────────────────────────┘
```

所有元件在同一台 Pi（.100）。

---

## 2. 可能變動的參數（用前先看）

### 2a. 攝影機

| 參數 | 當前值 | 說明 | 變動機率 |
|---|---|---|---|
| `CAMERA_IP` | `192.168.1.110` (cam1) / `192.168.1.111` (cam2) | bench 實測（2026-05-21 nmap 掃出）| 低 |
| `CAMERA_PORT` | `554` | RTSP 標準 | 低 |
| `CAMERA_USER` | `admin` | 原廠預設 | 中（可能改密碼）|
| `CAMERA_PASS` | `` (空) | 原廠預設 | 中 |
| `RTSP_MAIN_URL` | `rtsp://192.168.1.110:554/user=admin&password=&channel=1&stream=0.sdp?` | 1080p 主碼流 | 低 |
| `RTSP_SUB_URL` | `rtsp://192.168.1.110:554/user=admin&password=&channel=1&stream=1.sdp?` | 流暢子碼流（實際使用）| 低 |
| `CAMERA_VENDOR` | Xiongmai H264DVR | 雄邁 | — |
| **`CAM_TO_WALL_CM`** | **18** | feet cup 伸出 9.7cm + camera 8.5cm offset = 18.2cm（2026-06-01 量測） | 中 |
| **`CAM_TO_FEET_OFFSET_CM`** | **13.5** | camera 比 feet 位置「往前/上」偏移；偵測 distance 要 -13.5 才是 feet 實際距離。2026-06-04 從 6.5 改到 13.5（吸盤伸出時 cup tip 比 camera 更靠前，原 6.5 沒算到 cup 伸出 body 那段） | 中 |
| `CAMERA_POSITION` | 機體底部 | cam3 .112 左下、cam4 .113 右下 | — |
| `CAMERA_ORIENTATION` | 下俯 54°（從水平算）| optical axis 對準 25cm 距離點 | 已實測 |

### 2b. 網路 / Server 協定

| 參數 | 當前值 | 說明 | 變動機率 |
|---|---|---|---|
| `DETECT_SERVER_HOST` | `127.0.0.1` | localhost | 低 |
| `DETECT_SERVER_PORT` | `5040` | UDP | 低 |
| `FRAME_PATH` | `/tmp/cam_latest.jpg` | frame_capture 寫這裡 | 低 |
| `DETECT_TIMEOUT_MS` | `2000` | 單次 UDP 等 server 回應（對齊 server 建議 2.0s）| 中 |
| `NO_REPLY_LIMIT` | `3` | 連續 timeout 幾次算 server_down | 中 |
| `FRAME_STALE_MS` | `1000` | /tmp mtime 超過這個時間 → 當 fail-closed | 中 |

### 2c. 決策邏輯（washrobot 端）

| 參數 | 當前值 | 說明 | 變動機率 |
|---|---|---|---|
| `STEP_CM` | `30` | washrobot 正常單步（WASH_ROBOT.h 同名常數）| 低 |
| **`MAX_STEP_CM`** | **60**（暫定）| 跳過窗框能接受的最大單步 | **待實測確認** |
| `SAFETY_CM` | `5` | 跨越時預留緩衝 | 中 |
| `CONF_THRESHOLD` | `0.5` | detections 的 conf 低於此值忽略 | 中 |

### 2d. Detect server 內建（不改）

server 端固定值：
- 模型輸入 640×640
- `CONF_THR` 0.4（server 內部初篩）
- `IOU_THR` 0.45
- Class: `window_frame`

Server 回傳欄位（寫作本日已更新）：
```json
{
  "detections": [{
    "x1", "y1", "x2", "y2",           // bbox pixels
    "conf",                            // float 0~1
    "class",                           // "window_frame"
    "width_px", "height_px",           // bbox pixel size
    "width_cm", "height_cm",           // 投影到牆面的實際尺寸
    "distance_cm",                     // bbox 中心光軸距離
    "near_edge_cm"                     // bbox 4 角最近者光軸距離 ← 決策主要依據
  }]
}
// 錯誤時: {"error": "..."}
```

### Null 處理規則（server 文件）

| 欄位 | 可能 null | client 處理 |
|---|---|---|
| `x1..y2` | ❌ 絕無 | bbox 左上/右下 |
| `conf` | ❌ 絕無 | 可加第二層門檻（`CONF_THRESHOLD`）過濾 |
| `class` | ❌ 絕無 | 目前只有 `window_frame` |
| `width_px` / `height_px` | ❌ 絕無 | `x2-x1` / `y2-y1` |
| `width_cm` / `height_cm` | ✅ 可能 null | 幾何投影失敗（`--wall-side` 設錯）→ 當「無法評估尺寸」 |
| `distance_cm` | ✅ 可能 null | 同上 |
| **`near_edge_cm`** | ✅ 可能 null | **決策主要依據**；null → 當 BLOCK（fail-closed）|

### Client 收發狀態機（5 種狀態）

```
send UDP path → recvfrom
  ├─ 2s timeout → no_reply  (累計 no_reply_streak)
  │                ├─ 連續 < NO_REPLY_LIMIT (3) → no_reply（下次還可試）
  │                └─ 連續 ≥ 3 → server_down（fail-closed）
  ├─ 收到 {"error": "..."} → error
  ├─ 收到 {"detections": []} → empty（沒偵測到窗框 = 安全）
  └─ 收到 {"detections": [...]} → ok
```

Python 參考實作（server 文件附的骨架）：

```python
SERVER = ("127.0.0.1", 5040)
TIMEOUT_SEC = 2.0
NO_REPLY_LIMIT = 3

def detect(jpg_path):
    try:
        sock.sendto(jpg_path.encode(), SERVER)
        data, _ = sock.recvfrom(65536)
    except socket.timeout:
        no_reply_streak += 1
        if no_reply_streak >= NO_REPLY_LIMIT:
            return {"status": "server_down"}
        return {"status": "no_reply"}

    no_reply_streak = 0
    reply = json.loads(data.decode())
    if "error" in reply:    return {"status": "error", "message": reply["error"]}
    if not reply["detections"]: return {"status": "empty"}
    return {"status": "ok", "detections": reply["detections"]}
```

C++ FrameAnalyzer 會對齊這個狀態機。

### 2e. frame_capture 參數

| 參數 | 當前值 | 說明 | 變動機率 |
|---|---|---|---|
| `FPS_LIMIT` | `10` | 節流寫入（每 100ms 最多一張）| 中 |
| `JPEG_QUALITY` | `85` | OpenCV imwrite quality | 低 |
| `RECONNECT_DELAY_S` | `2.0` | RTSP 斷線重試間隔 | 低 |
| `USE_TCP_TRANSPORT` | `true` | RTSP over TCP（比 UDP 穩）| 低 |

---

## 3. 決策邏輯（C++ FrameAnalyzer）

### 對齊 server 的 5 狀態

```cpp
enum class AnalyzerStatus {
    OK,           // 有 detections
    EMPTY,        // 沒偵測到 → 安全
    ERROR,        // server 回 {"error": "..."}
    NO_REPLY,     // UDP 2s timeout，連續未達上限
    SERVER_DOWN   // 連續 >= NO_REPLY_LIMIT 次 no_reply
};
```

### 決策函式

```cpp
enum class Decision { OK_PROCEED, STEP_LARGE, BLOCK };

Decision decide_from_result(AnalyzerStatus st, const std::vector<Detection>& dets,
                             int step_cm, int max_step_cm, int safety_cm) {
    // 異常 → fail-closed
    if (st == AnalyzerStatus::SERVER_DOWN) return Decision::BLOCK;
    if (st == AnalyzerStatus::ERROR)       return Decision::BLOCK;
    if (st == AnalyzerStatus::NO_REPLY)    return Decision::BLOCK;   // 保守處理
    if (st == AnalyzerStatus::EMPTY)       return Decision::OK_PROCEED;
    // st == OK
    
    // 取最近那個 detection
    const Detection* nearest = nullptr;
    for (auto& d : dets) {
        if (d.conf < CONF_THRESHOLD) continue;
        if (std::isnan(d.near_edge_cm)) continue;
        if (!nearest || d.near_edge_cm < nearest->near_edge_cm)
            nearest = &d;
    }
    if (!nearest) return Decision::OK_PROCEED;   // 都低於門檻或都 null

    // 窗框還很遠
    if (nearest->near_edge_cm > step_cm + safety_cm) return Decision::OK_PROCEED;

    // 能不能跳過去
    if (std::isnan(nearest->height_cm)) return Decision::BLOCK;   // 無法估跨越量
    float skip_needed = nearest->near_edge_cm + nearest->height_cm + safety_cm;
    if (skip_needed <= max_step_cm) return Decision::STEP_LARGE;

    return Decision::BLOCK;
}
```

**特殊情況：**
- `near_edge_cm` null → 當 BLOCK（fail-closed）
- `height_cm` null 但 `near_edge_cm` 有值且很近 → 無法算跨越 → BLOCK
- 所有 detections conf 都 < threshold → 視同 EMPTY / OK_PROCEED
- `no_reply` 單次 → BLOCK（保守；下次還會試）
- `server_down`（連續 3 次 timeout）→ BLOCK + EVT `server_down`

---

## 4. GUI 整合（washrobot 指令）

### 新指令
```
obstacle_detect <on|off>     # 開啟 / 關閉 pre-step check
obstacle_status              # 回 OK enabled=0|1 last_check=ok|block|stale
```

### EVT 廣播
```
EVT obstacle_detect state=<on|off>
EVT frame_detected near_edge_cm=<n> height_cm=<h> conf=<c>
EVT frame_blocked reason=<too_close|too_wide|server_err>
```

### GUI 按鈕（web_backend/public/）
- `#btn-obstacle-toggle` — click toggle
- `#obstacle-last` — 顯示最新偵測結果

---

## 5. 已確認 / 待確認

### ✅ 已確認
- Server port 5040、request 純路徑字串、response JSON（上表）
- 使用**主碼流 (`stream=0`)** — 子碼流被 camera 內部 ROI 裁切，**不能用**（2026-06-01 發現）
- 相機 RTSP URL + 帳密
- `CAM_TO_WALL_CM = 18`（2026-06-01 量測：feet cup 9.7cm + offset 8.5cm）
- `CAM_TO_FEET_OFFSET_CM = 6.5`（camera 在 feet 前方 5-8cm）
- 鏡頭俯角 54°（從水平），cam3 + cam4 雙裝
- `MAX_STEP_CM = 60`（暫定）

### 📊 校正資料 LUT (2026-06-01)

每個 cam 獨立 LUT：image_y → camera_distance_cm。
piecewise linear interpolation 即可。

```python
# cam3 (.112) bottom-left
CAM3_LUT = [
    (410, 10),   # plank 中心 y_px, distance cm
    (340, 15),
    (220, 25),
    (135, 35),
    ( 70, 45),
    ( 40, 50),
]

# cam4 (.113) bottom-right (微差，獨立校正)
CAM4_LUT = [
    (390, 10),
    (330, 15),   # interpolated
    (210, 25),
    (125, 35),   # interpolated
    ( 65, 45),   # interpolated
    ( 50, 50),
]
```

**邊界：** 有效偵測 camera_distance 10~50cm。超出範圍：
- < 10cm → 太近（也可能已在畫面 outside）→ 視為 "too_close" emergency
- > 50cm → 太遠（畫面外）→ 視為「沒障礙物」

**feet 換算：** 所有 decision 邏輯用 `feet_distance = camera_distance - CAM_TO_FEET_OFFSET_CM`。

### 🟡 待確認
- 相機安裝位置 / 朝向（影響 server 的投影模型是否適用，若 server 假設固定姿態要對齊）
- `CAM_TO_WALL_CM` 實機量測
- `MAX_STEP_CM` 依 DM2J 軌道極限
- Fail-closed 行為 OK 嗎（server 掛/ 拍不到 → BLOCK，要不要 fail-open override）
- 偵測到 → GUI 要不要彈 modal 還是自動 STEP_LARGE

---

## 6. 分階段 roadmap

| Phase | 內容 | 狀態 |
|---|---|---|
| 0 | Server 部署驗證（detect_server.py 可跑）| 🟡 等驗 |
| 1 | `frame_capture.py` + RTSP 驗證 | 🟡 準備寫 |
| 2 | `FrameAnalyzer` C++（UDP + JSON parse + decide）| 🟡 準備寫 |
| 3 | washrobot 指令 + EVT（`obstacle_detect on/off` / `obstacle_status`）| ❌ |
| 4 | GUI toggle 按鈕 | ❌ |
| 5 | 整合 `do_step_down_` pre-check（先 log-only 模式）| ❌ [跨界: user_lib] |
| 6 | 開啟實際 block 行為 + 現場驗證 | ❌ |

---

## 7. 規範邊界

- `frame_capture.py` — Sadie 範圍（新增工具）
- `FrameAnalyzer` C++ — 暫放 `washrobot_new_PI/` 應用層（Sadie 範圍）
- 整合 `do_step_down_` → **跨界 user_lib**，要開 PR
- detect_server.py / 模型 → 另一個 agent 範圍，不碰
