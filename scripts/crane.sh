#!/bin/bash
# scripts/crane.sh — crane Pi bench launcher (tmux)
#
# Usage:
#   ./scripts/crane.sh start    # 全開（main + web_backend + 一個空 shell）
#   ./scripts/crane.sh attach   # 進去看 log
#   ./scripts/crane.sh stop     # 全關
#   ./scripts/crane.sh status   # 看哪些 window 在跑
#
# tmux 操作 — 見 scripts/wr.sh 註解（按鍵相同）
#
# 路徑覆蓋（預設依 .claude/runbook.md 的 deploy 路徑）：
#   CRANE_BIN=/path/to/Crane_control_PI ./scripts/crane.sh start
#   WEB_DIR=/path/to/web_backend          ./scripts/crane.sh start
#
# 若要跑 crane_shim（測試模式）取代主吊車，自己改 CRANE_BIN 指向 shim：
#   CRANE_BIN="python3 $HOME/facade_cleaning_v2/crane_shim/crane_shim.py" \
#     ./scripts/crane.sh start

set -u

SESSION="crane"
# [2026-08-28] 預設值改為 Pi 上的實際部署位置（實機確認）。舊值五處都錯：
# 少了 projects/、平台寫 ARM（實際 ARM64，也是唯一設了 include 路徑的組態）、
# 組態寫 Release（實際 Debug）、檔名少了 .out、web 目錄名根本不存在。
# 這個佈局是 Visual Studio 遠端建置產生的：MSBuild 把原始碼送上 Pi、
# 用 Pi 的 g++ 編譯，輸出落在 bin/<Platform>/<Configuration>/<name>.out。
CRANE_BIN="${CRANE_BIN:-$HOME/projects/crane_control_PI/bin/ARM64/Debug/crane_control_PI.out}"
WEB_DIR="${WEB_DIR:-$HOME/projects/web_ver2}"

case "${1:-attach}" in
    start)
        if tmux has-session -t "$SESSION" 2>/dev/null; then
            echo "session '$SESSION' already running — '$0 attach' to view"
            exit 0
        fi
        # CRANE_BIN can be a path to a binary OR a command string ("python3 ...")
        # so don't enforce -x; only check single-word path existence.
        first_word="${CRANE_BIN%% *}"
        if [[ "$first_word" == /* && ! -x "$first_word" ]]; then
            echo "ERROR: crane binary not found / not executable: $first_word" >&2
            echo "(override: CRANE_BIN=/path/to/binary $0 start)" >&2
            exit 1
        fi
        if [[ ! -f "$WEB_DIR/server.js" ]]; then
            echo "ERROR: web_backend/server.js not found at $WEB_DIR" >&2
            echo "(override: WEB_DIR=/path/to/web_backend $0 start)" >&2
            exit 1
        fi
        tmux new-session -d -s "$SESSION" -n main "$CRANE_BIN"
        tmux new-window  -t "$SESSION"    -n web  "cd $WEB_DIR && node server.js"
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
        echo "env overrides: CRANE_BIN, WEB_DIR" >&2
        exit 1
        ;;
esac
