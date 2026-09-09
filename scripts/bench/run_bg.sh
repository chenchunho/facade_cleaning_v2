#!/bin/bash
# 背景啟動：用 FIFO 保持 stdin 開著（程式讀到 EOF 會自己關掉），
# 之後可用  echo exit > /tmp/<name>.fifo  優雅停止。
set -u
NAME="$1"; LOG="$2"; shift 2
FIFO="/tmp/${NAME}.fifo"
rm -f "$FIFO"; mkfifo "$FIFO"
setsid nohup sleep infinity > "$FIFO" 2>/dev/null &
setsid nohup "$@" < "$FIFO" > "$LOG" 2>&1 &
echo "$NAME started (log=$LOG, fifo=$FIFO)"
