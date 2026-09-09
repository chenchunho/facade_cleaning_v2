#!/usr/bin/env python3
# Send one command line to the washrobot/crane command server and print the reply.
# Reads for a bounded window so async EVT broadcasts are shown too.
import socket, sys, time
host, port, cmd = sys.argv[1], int(sys.argv[2]), sys.argv[3]
wait = float(sys.argv[4]) if len(sys.argv) > 4 else 6.0
s = socket.create_connection((host, port), timeout=5)
s.settimeout(0.5)
time.sleep(0.3)
try:                                   # drain anything sent on connect
    s.recv(65536)
except Exception:
    pass
print(">>> %s" % cmd, flush=True)
s.sendall((cmd + "\n").encode())
end = time.time() + wait
buf = b""
while time.time() < end:
    try:
        d = s.recv(65536)
        if not d:
            break
        buf += d
    except socket.timeout:
        if buf:
            break
print("<<< " + (buf.decode(errors="replace").rstrip() if buf else "(no reply within %.0fs)" % wait), flush=True)
s.close()
