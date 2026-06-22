#!/usr/bin/env python3
# X518 probe — Modbus TCP, port 502
#   1) read CH1 tension raw (proves connectivity)
#   2) read current IP registers (so we can decode the encoding)

import socket, struct, sys

IP   = sys.argv[1] if len(sys.argv) > 1 else '192.168.1.100'
PORT = 502
UNIT = 1

def mbtcp_read(s, addr, qty):
    txid = 0x0001
    pdu  = struct.pack('>BHH', 0x03, addr, qty)
    mbap = struct.pack('>HHHB', txid, 0x0000, len(pdu)+1, UNIT)
    s.send(mbap + pdu)
    r = s.recv(256)
    if len(r) < 9 or r[7] != 0x03:
        print(f'  [ERR] addr=0x{addr:04X} bad reply: {r.hex()}')
        return None
    bc = r[8]
    return r[9:9+bc]

def parse_long_be(b, off=0):
    v = struct.unpack('>i', b[off:off+4])[0]
    return v

s = socket.socket()
s.settimeout(2)
print(f'connecting to {IP}:{PORT} ...')
s.connect((IP, PORT))
print('OK')

# --- 1) channel data ---
print('\n[CH1+CH2 tension raw @ 0x0A00, 4 reg = 2 longs]')
data = mbtcp_read(s, 0x0A00, 4)
if data and len(data) >= 8:
    ch1 = parse_long_be(data, 0)
    ch2 = parse_long_be(data, 4)
    print(f'  CH1 raw = {ch1}    (kg @ scale 0.01 = {ch1*0.01:.2f})')
    print(f'  CH2 raw = {ch2}')

# --- 2) device IP, port, slave ---
print('\n[device config registers]')
for name, addr, qty in [
    ('IPH/IPL (0x063E, 4 reg)', 0x063E, 4),
    ('Modbus port (0x0642, 2 reg)', 0x0642, 2),
    ('mode 1=mbTCP/2=ASC_TCP (0x0644, 2 reg)', 0x0644, 2),
    ('TargetIP IPH/IPL (0x0646, 4 reg)', 0x0646, 4),
    ('Slave ID (0x064C, 2 reg)', 0x064C, 2),
    ('Baud idx (0x0636, 2 reg)', 0x0636, 2),
    ('Format idx (0x0638, 2 reg)', 0x0638, 2),
    ('Unit (0x0614, 2 reg)', 0x0614, 2),
]:
    data = mbtcp_read(s, addr, qty)
    if data:
        print(f'  {name}: {data.hex()}', end='')
        if len(data) == 4:
            print(f'  (long={parse_long_be(data)})')
        elif len(data) == 8:
            print(f'  (long1={parse_long_be(data, 0)}, long2={parse_long_be(data, 4)})')
        else:
            print()

s.close()
