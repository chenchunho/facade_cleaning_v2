#!/bin/bash
# Launch detached with FIFO stdin (console loop reads getline(cin); /dev/null EOFs
# and triggers [SHUTDOWN]) and line-buffered logging. usage: launch.sh <dir> <bin> <log> <fifo>
DIR="$1"; BIN="$2"; LOG="$3"; FIFO="$4"
cd "$DIR" || exit 1
rm -f "$FIFO" "$LOG"
mkfifo -m 600 "$FIFO" || exit 1
setsid nohup sh -c "exec sleep infinity > '$FIFO'" >/dev/null 2>&1 </dev/null &
setsid nohup stdbuf -oL -eL "./$BIN" > "$LOG" 2>&1 < "$FIFO" &
sleep 1
echo "launched $BIN (fifo=$FIFO log=$LOG)"
