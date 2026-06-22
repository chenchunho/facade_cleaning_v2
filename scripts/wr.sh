#!/bin/bash
# scripts/wr.sh — washrobot Pi bench launcher (tmux)
#
# Usage:
#   ./scripts/wr.sh start    # 全開（main + cam1 + cam2 + 一個空 shell）
#   ./scripts/wr.sh attach   # 進去看 log
#   ./scripts/wr.sh stop     # 全關
#   ./scripts/wr.sh status   # 看哪些 window 在跑
#
# tmux 內：
#   Ctrl-b 0/1/2     切 window
#   Ctrl-b w         列表選 window
#   Ctrl-b d         detach（程式繼續跑、SSH 斷了也沒事）
#   Ctrl-C           只關當前 window 的程式
#   ↑ Enter          重開剛剛關的程式（recall last command）
#
# 路徑覆蓋（預設依 .claude/runbook.md 的 deploy 路徑）：
#   WR_BIN=/path/to/washrobot_new_PI ./scripts/wr.sh start
#   WR_CAM=/path/to/frame_capture.py ./scripts/wr.sh start
#
# 攝影機 IP 覆蓋（預設 cam1=.110、cam2=.111；2026-05-21 從 .10 改）：
#   CAM1_IP=192.168.1.110 CAM2_IP=192.168.1.111 ./scripts/wr.sh start

set -u

SESSION="wr"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WR_BIN="${WR_BIN:-$HOME/washrobot_new_PI/bin/ARM/Release/washrobot_new_PI}"
WR_CAM="${WR_CAM:-$ROOT/frame_capture/frame_capture.py}"
CAM1_IP="${CAM1_IP:-192.168.1.110}"
CAM2_IP="${CAM2_IP:-192.168.1.111}"
CAM1_URL="rtsp://${CAM1_IP}:554/user=admin&password=&channel=1&stream=1.sdp?"
CAM2_URL="rtsp://${CAM2_IP}:554/user=admin&password=&channel=1&stream=1.sdp?"

case "${1:-attach}" in
    start)
        if tmux has-session -t "$SESSION" 2>/dev/null; then
            echo "session '$SESSION' already running — '$0 attach' to view"
            exit 0
        fi
        if [[ ! -x "$WR_BIN" ]]; then
            echo "ERROR: washrobot binary not found / not executable: $WR_BIN" >&2
            echo "(override: WR_BIN=/path/to/binary $0 start)" >&2
            exit 1
        fi
        if [[ ! -f "$WR_CAM" ]]; then
            echo "ERROR: frame_capture.py not found: $WR_CAM" >&2
            echo "(override: WR_CAM=/path/to/frame_capture.py $0 start)" >&2
            exit 1
        fi
        tmux new-session -d -s "$SESSION" -n main "$WR_BIN"
        tmux new-window  -t "$SESSION"    -n cam1 \
            "python3 $WR_CAM --url '$CAM1_URL' --cam-id cam1 --http-port 5004 --out /tmp/cam1_latest.jpg"
        tmux new-window  -t "$SESSION"    -n cam2 \
            "python3 $WR_CAM --url '$CAM2_URL' --cam-id cam2 --http-port 5005 --out /tmp/cam2_latest.jpg"
        tmux new-window  -t "$SESSION"    -n shell
        echo "started session '$SESSION':"
        tmux list-windows -t "$SESSION"
        echo "→ '$0 attach' to view"
        ;;
    stop)
        if tmux has-session -t "$SESSION" 2>/dev/null; then
            tmux kill-session -t "$SESSION"
            echo "killed session '$SESSION'"
        else
            echo "session '$SESSION' not running"
        fi
        ;;
    attach)
        if ! tmux has-session -t "$SESSION" 2>/dev/null; then
            echo "session '$SESSION' not running — '$0 start' first" >&2
            exit 1
        fi
        tmux attach -t "$SESSION"
        ;;
    status)
        if tmux has-session -t "$SESSION" 2>/dev/null; then
            echo "running:"
            tmux list-windows -t "$SESSION"
        else
            echo "not running"
        fi
        ;;
    *)
        echo "usage: $0 {start|stop|attach|status}" >&2
        echo "env overrides: WR_BIN, WR_CAM" >&2
        exit 1
        ;;
esac
