#!/usr/bin/env python3
# Quick TCP port scan for X518 (Wiznet-based DAQ)
# Tries common Modbus / industrial gateway ports.

import socket, sys

IP = sys.argv[1] if len(sys.argv) > 1 else '192.168.1.100'
PORTS = [502, 503, 4001, 8000, 8080, 5000, 23, 80, 4196, 21, 1024, 9000]

print(f'scanning {IP} ...')
for p in PORTS:
    s = socket.socket()
    s.settimeout(0.4)
    try:
        s.connect((IP, p))
        print(f'  {p:>5} : OPEN')
        s.close()
    except (socket.timeout, ConnectionRefusedError, OSError) as e:
        kind = type(e).__name__
        print(f'  {p:>5} : {kind}')
