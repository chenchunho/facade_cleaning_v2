#!/bin/bash
# scripts/cams.sh — 同時啟動 4 支攝影機的 frame_capture.py
#
# Usage:
#   ./scripts/cams.sh start    # 4 支全開（background）
#   ./scripts/cams.sh stop     # 全關
#   ./scripts/cams.sh status   # 看哪幾支在跑
#   ./scripts/cams.sh tail     # tail 4 支的 log
#
# 路徑覆蓋（預設用 repo 內的 frame_capture/frame_capture.py）：
#   WR_CAM=/path/to/frame_capture.py ./scripts/cams.sh start
#
# 攝影機 IP 覆蓋（預設依 work_log.md / changelog.md）：
#   CAM1_IP=192.168.1.110 CAM2_IP=192.168.1.111 \
#   CAM3_IP=192.168.1.112 CAM4_IP=192.168.1.113 ./scripts/cams.sh start
#
# Output:
#   /tmp/camN_latest.jpg          — frame_capture 輸出的最新 frame (snapshot)
#   /tmp/camN.log                 — 該 instance stdout+stderr
#   /tmp/camN.pid                 — process id (for stop)
#
# HTTP endpoint（每支獨立 port）：
#   curl http://localhost:5004/snap/cam1
#   curl http://localhost:5005/snap/cam2
#   curl http://localhost:5006/snap/cam3
#   curl http://localhost:5007/snap/cam4

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WR_CAM="${WR_CAM:-$ROOT/frame_capture/frame_capture.py}"

CAM1_IP="${CAM1_IP:-192.168.1.110}"
CAM2_IP="${CAM2_IP:-192.168.1.111}"
CAM3_IP="${CAM3_IP:-192.168.1.112}"
CAM4_IP="${CAM4_IP:-192.168.1.113}"

# 共用 RTSP URL 模板（stream=1 是子碼流，省 CPU）
build_url() {
    local ip="$1"
    echo "rtsp://${ip}:554/user=admin&password=&channel=1&stream=1.sdp?"
}

# cam_id  ip          http_port
CAMS=(
    "cam1  $CAM1_IP  5004"
    "cam2  $CAM2_IP  5005"
    "cam3  $CAM3_IP  5006"
    "cam4  $CAM4_IP  5007"
)

start_one() {
    local cam_id="$1"
    local ip="$2"
    local port="$3"
    local pidf="/tmp/${cam_id}.pid"
    local logf="/tmp/${cam_id}.log"
    local out="/tmp/${cam_id}_latest.jpg"
    local url
    url="$(build_url "$ip")"

    if [[ -f "$pidf" ]] && kill -0 "$(cat "$pidf")" 2>/dev/null; then
        echo "  $cam_id: already running (pid $(cat "$pidf"))"
        return
    fi

    nohup python3 "$WR_CAM" \
        --url "$url" \
        --out "$out" \
        --cam-id "$cam_id" \
        --http-port "$port" \
        >"$logf" 2>&1 &
    local pid=$!
    echo "$pid" > "$pidf"
    echo "  $cam_id: pid $pid  ip $ip  port $port  → $out (log: $logf)"
}

stop_one() {
    local cam_id="$1"
    local pidf="/tmp/${cam_id}.pid"
    if [[ ! -f "$pidf" ]]; then
        echo "  $cam_id: no pidfile"
        return
    fi
    local pid
    pid="$(cat "$pidf")"
    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid"
        echo "  $cam_id: killed pid $pid"
    else
        echo "  $cam_id: pid $pid not running (stale pidfile)"
    fi
    rm -f "$pidf"
}

status_one() {
    local cam_id="$1"
    local ip="$2"
    local port="$3"
    local pidf="/tmp/${cam_id}.pid"
    if [[ -f "$pidf" ]] && kill -0 "$(cat "$pidf")" 2>/dev/null; then
        printf "  %s: ✓ pid %s  ip %s  port %s\n" "$cam_id" "$(cat "$pidf")" "$ip" "$port"
    else
        printf "  %s: ✗ not running (ip %s  port %s)\n" "$cam_id" "$ip" "$port"
    fi
}

case "${1:-status}" in
    start)
        if [[ ! -f "$WR_CAM" ]]; then
            echo "ERROR: frame_capture.py not found: $WR_CAM" >&2
            echo "(override: WR_CAM=/path/to/frame_capture.py $0 start)" >&2
            exit 1
        fi
        echo "starting 4 cams..."
        for line in "${CAMS[@]}"; do
            read -r cam_id ip port <<< "$line"
            start_one "$cam_id" "$ip" "$port"
        done
        ;;
    stop)
        echo "stopping 4 cams..."
        for line in "${CAMS[@]}"; do
            read -r cam_id ip port <<< "$line"
            stop_one "$cam_id"
        done
        ;;
    restart)
        "$0" stop
        sleep 1
        "$0" start
        ;;
    status)
        echo "cam status:"
        for line in "${CAMS[@]}"; do
            read -r cam_id ip port <<< "$line"
            status_one "$cam_id" "$ip" "$port"
        done
        ;;
    tail)
        # tail 4 個 log，按 Ctrl-C 結束
        tail -F /tmp/cam1.log /tmp/cam2.log /tmp/cam3.log /tmp/cam4.log
        ;;
    *)
        echo "usage: $0 {start|stop|restart|status|tail}" >&2
        exit 1
        ;;
esac
