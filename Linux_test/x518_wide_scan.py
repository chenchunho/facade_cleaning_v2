#!/usr/bin/env python3
# Wider X518 diagnostic:
#   1) TCP scan more ports
#   2) UDP probe common discovery ports
#   3) Try binding source to 192.168.1.77 explicitly (in case routing weirdness)

import socket, sys, time

IP = sys.argv[1] if len(sys.argv) > 1 else '192.168.1.100'

# Wider TCP range
TCP_PORTS = [
    502, 503, 504, 4001, 4196, 4001, 5001, 5002, 5003, 5004, 5005,
    6000, 6001, 8000, 8001, 8080, 80, 23, 21, 1024, 9000,
    1234, 12345, 7000, 9999, 9100, 5557, 5760, 30000,
    1502, 802, 601, 161, 5020,
]
# common UDP discovery
UDP_PORTS = [502, 1900, 5353, 30303, 30718, 1024, 23, 17500, 8000]

def tcp_one(ip, port, src='192.168.1.77', tmo=0.6):
    s = socket.socket()
    s.settimeout(tmo)
    try:
        s.bind((src, 0))
    except Exception:
        pass
    try:
        s.connect((ip, port))
        s.close()
        return 'OPEN'
    except ConnectionRefusedError:
        return 'refused'
    except socket.timeout:
        return 'timeout'
    except OSError as e:
        return f'os:{e.errno}'

def udp_probe(ip, port, src='192.168.1.77', tmo=0.5):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(tmo)
    try:
        s.bind((src, 0))
    except Exception:
        pass
    # send a few common discovery payloads
    payloads = [
        b'\x00\x01\x00\x00\x00\x06\x01\x03\x0A\x00\x00\x02',  # Modbus TCP-like
        b'X' * 4,
        b'\x00\x00\x00\x00',
    ]
    for p in payloads:
        try:
            s.sendto(p, (ip, port))
        except Exception:
            pass
    try:
        data, addr = s.recvfrom(512)
        return f'GOT {len(data)}B from {addr}'
    except socket.timeout:
        return 'no-reply'
    finally:
        s.close()

print(f'=== TCP scan {IP} (src bind 192.168.1.77) ===')
for p in TCP_PORTS:
    r = tcp_one(IP, p)
    if r != 'timeout':
        print(f'  {p:>5} : {r}')
print('  (timeouts hidden)')

print(f'\n=== UDP probe {IP} ===')
for p in UDP_PORTS:
    r = udp_probe(IP, p)
    print(f'  {p:>5} : {r}')

print('\n=== Listen on PC :8000 for 5s (in case X518 is TCP client) ===')
ls = socket.socket()
ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
try:
    ls.bind(('192.168.1.77', 8000))
    ls.listen(1)
    ls.settimeout(5.0)
    print('  waiting on 192.168.1.77:8000 ...')
    try:
        c, addr = ls.accept()
        print(f'  CONNECT from {addr}')
        c.settimeout(1.0)
        try:
            data = c.recv(256)
            print(f'  recv {len(data)}B: {data.hex()}')
        except socket.timeout:
            print('  (connected but no data within 1s)')
        c.close()
    except socket.timeout:
        print('  no connection (X518 is not in TCP client mode to this addr/port)')
except OSError as e:
    print(f'  bind failed: {e}')
finally:
    ls.close()
