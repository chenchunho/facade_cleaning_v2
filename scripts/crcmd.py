import socket, sys, time
# [2026-09-10] host:port is optional and defaults to the crane. Both programs'
# local consoles only accept exit/quit/status (crane main.cpp:5542,
# facade_cleaning_v2/main.cpp:139-142) — every other command must come in over
# TCP, so this is the only way to set runtime params on either side.
host, port = "127.0.0.1", 5002
args = sys.argv[1:]
if args and ":" in args[0] and not args[0].startswith(("set_", "water_", "arm_")):
    host, _, p_ = args[0].partition(":")
    port = int(p_)
    args = args[1:]
cmds = args
s = socket.create_connection((host, port), timeout=5)
s.settimeout(0.3)
buf = b""
def drain(deadline, until_reply=False):
    # [2026-10-01] until_reply: return as soon as one non-EVT line has arrived
    # (every command answers with a single OK/ERR line). It used to sit out the
    # full 2 s per command, which made the crane unit's ExecStartPost replay of
    # 19 settings lines take ~80 s of the 90 s start timeout.
    global buf
    out = []
    while time.time() < deadline:
        if until_reply and any(not l.startswith("EVT") for l in out):
            break
        try:
            d = s.recv(65536)
            if not d: break
            buf += d
        except socket.timeout:
            pass
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            out.append(line.decode(errors="replace"))
    return out
drain(time.time() + 0.5)          # discard whatever was queued
for c in cmds:
    s.sendall((c + "\n").encode())
    lines = [l for l in drain(time.time() + 2.0, until_reply=True) if not l.startswith("EVT")]
    print(f"→ {c}")
    for l in lines:
        print(f"   {l}")
s.close()
