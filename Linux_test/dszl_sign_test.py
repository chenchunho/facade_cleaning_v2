#!/usr/bin/env python3
# DSZL-107 / X518 sign test — READ-ONLY.
#
# Purpose: determine the raw->kg SIGN for each load cell empirically, without
# needing a calibrated weight. Only the DIRECTION of a force change is needed:
# sample raw at rest, apply a known-direction force, sample again.
#
# SAFETY: this script implements FC03 (read holding registers) and NOTHING
# else. There is no write path in it at all, so it cannot zero a channel,
# change the unit register, or touch X518 flash. Compare x518_probe.py, which
# is also read-only but one-shot.
#
# Usage:
#   ./dszl_sign_test.py <label> [samples] [interval_ms]
#
# Left  cell = X518 #1 @ 192.168.1.32:502 slave 1   (DSZL_LEFT_IP  in crane main.cpp)
# Right cell = X518 #2 @ 192.168.1.33:502 slave 1   (DSZL_RIGHT_IP in crane main.cpp)

import socket, struct, sys, time

SIDES = [("left", "192.168.1.32"), ("right", "192.168.1.33")]
PORT = 502
UNIT_ID = 1

UNIT_NAMES = {1: "t", 2: "kg", 3: "g", 4: "kN", 5: "N", 6: "lb"}


def mbtcp_read(sock, addr, qty):
    """FC03 only. Returns payload bytes or None. No write function exists here."""
    pdu = struct.pack(">BHH", 0x03, addr, qty)
    mbap = struct.pack(">HHHB", 0x0001, 0x0000, len(pdu) + 1, UNIT_ID)
    sock.send(mbap + pdu)
    r = sock.recv(256)
    if len(r) < 9 or r[7] != 0x03:
        return None
    return r[9 : 9 + r[8]]


def parse_long_be(b, off=0):
    return struct.unpack(">i", b[off : off + 4])[0]


def connect(ip):
    s = socket.socket()
    s.settimeout(2)
    s.connect((ip, PORT))
    return s


def main():
    label = sys.argv[1] if len(sys.argv) > 1 else "sample"
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 25
    interval = (int(sys.argv[3]) if len(sys.argv) > 3 else 200) / 1000.0

    socks = {}
    for side, ip in SIDES:
        try:
            socks[side] = connect(ip)
        except Exception as e:
            print(f"[ERR] {side} {ip}:{PORT} connect failed: {e}")
            return 2

    # Device unit register — read-only check. set_unit() in the driver writes
    # RAM only and the crane program never calls save_params() after it, so a
    # power-cycled X518 can silently come back in N instead of kg (~9.8x).
    print(f"=== unit register 0x0614 ===")
    for side, ip in SIDES:
        d = mbtcp_read(socks[side], 0x0614, 2)
        if d and len(d) >= 4:
            u = parse_long_be(d)
            print(f"  {side:5s} unit={u} ({UNIT_NAMES.get(u, '?')})")
        else:
            print(f"  {side:5s} unit=READ_FAIL")

    print(f"\n=== label={label}  samples={n}  interval={interval*1000:.0f}ms ===")
    print(f"{'#':>3} {'left_raw':>12} {'right_raw':>12}")

    series = {"left": [], "right": []}
    for i in range(n):
        row = {}
        for side, ip in SIDES:
            d = mbtcp_read(socks[side], 0x0A00, 4)
            row[side] = parse_long_be(d, 0) if (d and len(d) >= 8) else None
            if row[side] is not None:
                series[side].append(row[side])
        print(
            f"{i:3d} "
            f"{('ERR' if row['left'] is None else row['left']):>12} "
            f"{('ERR' if row['right'] is None else row['right']):>12}"
        )
        time.sleep(interval)

    print(f"\n=== summary  label={label} ===")
    for side, _ in SIDES:
        v = series[side]
        if not v:
            print(f"  {side:5s} NO VALID SAMPLES")
            continue
        mean = sum(v) / len(v)
        print(
            f"  {side:5s} n={len(v):3d}  min={min(v):>10d}  max={max(v):>10d}  "
            f"mean={mean:>12.1f}  spread={max(v)-min(v):>8d}"
        )

    for s in socks.values():
        s.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
