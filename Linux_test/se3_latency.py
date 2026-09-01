#!/usr/bin/env python3
# se3_latency.py — READ-ONLY SE3 response-latency probe.
#
# Sends Modbus-RTU FC03 (read 1 holding register) through the USR transparent
# gateway and measures how long the reply takes, with a LONG timeout (2s) so a
# merely-slow device is distinguishable from a dead one.
#
# Why: 2026-09-01 the right SE3 went to keepalive 0/50 with Recv-Q=7 stuck on
# its gateway socket. The driver's recv timeout is 150ms (shortened 300->150 on
# 2026-05-14). If the device now answers in >150ms, every transaction times out
# while the late reply lands in the buffer — a stable failure loop that looks
# like "dead" but is actually "slow".
#
# FC03 only — no write path exists in this script.
#
# usage: se3_latency.py <ip> [reg_hex] [n]

import socket, struct, sys, time


def crc16(b):
    c = 0xFFFF
    for ch in b:
        c ^= ch
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c


ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.31"
reg = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x1001
n = int(sys.argv[3]) if len(sys.argv) > 3 else 8

req = bytes([1, 3, (reg >> 8) & 0xFF, reg & 0xFF, 0, 1])
req += struct.pack("<H", crc16(req))

print(f"{ip}:4001  FC03 reg=0x{reg:04X}  n={n}  (recv timeout 2000ms)")
print(f"{'#':>3} {'latency':>9}  {'bytes':>5}  reply")

lat, fails = [], 0
try:
    s = socket.create_connection((ip, 4001), timeout=5)
except Exception as e:
    print(f"[ERR] connect failed: {e}")
    sys.exit(2)

for i in range(n):
    # drain anything stale first (same as TCP_client does)
    s.setblocking(False)
    try:
        while s.recv(256):
            pass
    except BlockingIOError:
        pass
    except Exception:
        pass
    s.setblocking(True)

    s.settimeout(2.0)
    t0 = time.time()
    try:
        s.sendall(req)
        r = s.recv(256)
        ms = (time.time() - t0) * 1000
        lat.append(ms)
        print(f"{i:>3} {ms:>8.1f}ms  {len(r):>5}  {r.hex()}")
    except socket.timeout:
        fails += 1
        print(f"{i:>3} {'TIMEOUT':>9}  (>2000ms — 無回應)")
    except Exception as e:
        fails += 1
        print(f"{i:>3} {'ERR':>9}  {e}")
    time.sleep(0.2)
s.close()

print()
if lat:
    print(f"成功 {len(lat)}/{n}   最小 {min(lat):.1f}ms  最大 {max(lat):.1f}ms  "
          f"平均 {sum(lat)/len(lat):.1f}ms")
    over = [x for x in lat if x > 150]
    print(f"超過 driver 的 150ms recv 逾時: {len(over)}/{len(lat)}"
          + (f"   最大 {max(over):.1f}ms" if over else ""))
else:
    print(f"🔴 全部失敗 ({fails}/{n}) — 裝置在 2 秒內完全無回應")
