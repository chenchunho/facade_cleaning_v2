#!/usr/bin/env bash
# crane_cmd_ line-buffer regression (2026-09-14): body vs a crane that floods EVT behind every reply.
# 用法: ./harness/crane_linebuf_test.sh <body binary> <輸出目錄>   期望 OK 6/6(修前 2/6)。
# 🔴 需要 :5001 空著(gui_offline 的 fake 先 stop)。
set -u
BIN="${1:?body binary}"; OUT="${2:?輸出目錄}"; H="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; S=$H
mkdir -p "$OUT"; PIDS=()
cleanup(){ for p in "${PIDS[@]:-}"; do kill "$p" 2>/dev/null || true; done; wait 2>/dev/null || true; }
trap cleanup EXIT
python3 "$H/fake_bus.py" --port 15020 --proto rtu --dm2j 14 2>>"$OUT/fakes.log" & PIDS+=($!)
python3 "$H/fake_bus.py" --port 15021 --proto rtu 2>>"$OUT/fakes.log" & PIDS+=($!)
python3 "$H/fake_bus.py" --port 15022 --proto rtu --jc100 5,6,7,8 2>>"$OUT/fakes.log" & PIDS+=($!)
python3 "$S/hostile_crane.py" 15002 2>>"$OUT/fakes.log" & PIDS+=($!)
python3 "$H/fake_text_server.py" --port 15527 --name arm 2>>"$OUT/fakes.log" & PIDS+=($!)
PTYF="$OUT/imu_pty"; rm -f "$PTYF"
python3 "$H/fake_serial.py" --path-file "$PTYF" --delay 0.5 --frames 4000 2>>"$OUT/fakes.log" & PIDS+=($!)
for _ in $(seq 1 100); do [[ -s "$PTYF" ]] && break; sleep 0.05; done
export FCV_EP_IMU_HOST="$(cat "$PTYF")"
export FCV_EP_USR20_HOST=127.0.0.1 FCV_EP_USR20_PORT=15020 FCV_EP_USR21_HOST=127.0.0.1 FCV_EP_USR21_PORT=15021
export FCV_EP_USR22_HOST=127.0.0.1 FCV_EP_USR22_PORT=15022 FCV_EP_CRANE_HOST=127.0.0.1 FCV_EP_CRANE_PORT=15002
export FCV_EP_ARM_HOST=127.0.0.1 FCV_EP_ARM_PORT=15527 WR_DRIVER_DEBUG=0
for p in 15020 15021 15022 15002 15527; do for _ in $(seq 1 60); do (exec 3<>/dev/tcp/127.0.0.1/$p) 2>/dev/null && break; sleep 0.05; done; done
"$BIN" >"$OUT/stdout.log" 2>"$OUT/stderr.log" < <(sleep 300) & PIDS+=($!)
for _ in $(seq 1 200); do (exec 3<>/dev/tcp/127.0.0.1/5001) 2>/dev/null && break; sleep 0.1; done
python3 - "$OUT" <<'EOF'
import socket, time, sys
out = sys.argv[1]
s = socket.create_connection(('127.0.0.1', 5001), timeout=5); s.settimeout(0.5)
time.sleep(1.0)
try:
    while s.recv(65536): pass
except socket.timeout: pass
res = []
for i in range(6):
    s.sendall(b'water_inlet off\n'); buf = b''; t = time.time()
    while time.time() - t < 12:
        try: buf += s.recv(65536)
        except socket.timeout: pass
        lines = [l for l in buf.decode(errors='replace').split('\n') if l and not l.startswith('EVT')]
        if lines: break
    res.append(lines[0] if lines else '<<TIMEOUT>>')
    time.sleep(0.3)
s.sendall(b'exit\n'); s.close()
print('\n'.join(f'#{i+1} {r}' for i, r in enumerate(res)))
ok = sum(1 for r in res if r.startswith('OK'))
print(f'OK {ok}/6')
EOF
sleep 2
echo "--- crane_cmd log lines:"; /bin/grep -a "crane_cmd\]" "$OUT/stdout.log" | head -12
