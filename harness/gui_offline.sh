#!/usr/bin/env bash
# Run the v2 web GUI against fake_robot.py — no Pi, no hardware.
#
#   ./harness/gui_offline.sh            # start fake robot + web_backend (console v3) on :8081
#   ./harness/gui_offline.sh stop
#   ./harness/gui_offline.sh report     # what did the GUI ask for?
#
# Then open http://localhost:8081  (WSL2: works from the Windows browser as-is).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
RUN="$REPO/tmp/gui_offline"; mkdir -p "$RUN"
PORT="${HTTP_PORT:-8081}"

case "${1:-start}" in
  stop)
    for f in "$RUN"/*.pid; do [[ -f "$f" ]] && { kill "$(cat "$f")" 2>/dev/null || true; rm -f "$f"; }; done
    echo "stopped"; exit 0;;
  report)
    python3 "$HERE/fake_robot.py" --report; exit 0;;
esac

[[ -d "$REPO/web_backend/node_modules" ]] || (cd "$REPO/web_backend" && npm install --silent)

python3 "$HERE/fake_robot.py" --quiet > "$RUN/fake_robot.log" 2>&1 &
echo $! > "$RUN/fake_robot.pid"
# wait for the three ports, bounded (CLAUDE.md: no unbounded loops)
for i in $(seq 1 40); do
  ok=1; for p in 5001 5002 9527; do (echo > /dev/tcp/127.0.0.1/$p) 2>/dev/null || ok=0; done
  [[ $ok == 1 ]] && break; sleep 0.25
done
[[ $ok == 1 ]] || { echo "fake_robot did not come up, see $RUN/fake_robot.log"; exit 1; }

# [2026-09-14 per user] v2 and v3 merged into one console (public_v3); v2 retired (git history: feebc03)
start_web() {  # $1=port $2=public dir $3=tag
  ( cd "$REPO/web_backend" && \
    WROBOT_IP=127.0.0.1 CRANE_IP=127.0.0.1 ARM_IP=127.0.0.1 \
    HTTP_PORT="$1" PUBLIC_DIR="$REPO/web_backend/$2" HOME="$RUN" \
    setsid nohup node server.js > "$RUN/web_$3.log" 2>&1 < /dev/null & echo $! > "$RUN/web_$3.pid" )
  for i in $(seq 1 40); do (echo > /dev/tcp/127.0.0.1/$1) 2>/dev/null && break; sleep 0.25; done
}
start_web "$PORT" public_v3 v3

echo "fake robot : washrobot :5001  crane :5002  arm :9527   (log: $RUN/fake_robot.log)"
echo "web GUI    : http://localhost:$PORT   (console v3 = 唯一主控台;log: $RUN/web_v3.log)"
echo "commands   : $HERE/gui_cmd_log.txt   →  ./harness/gui_offline.sh report"
