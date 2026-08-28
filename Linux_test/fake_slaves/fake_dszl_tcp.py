#!/usr/bin/env python3
"""Fake DSZL-107 (X518) Modbus-TCP slave for driver validation.

DSZL speaks Modbus TCP (MBAP header), not RTU — there is no CRC, so the checks
under test are the transaction id, the unit id and the byte count.

Unlike fake_sd76.py this applies the fault to EVERY request: DSZL's
init(ip, port, id) does not probe, and read_reg_long retries 3x, so there is no
"first request is special" to account for. Binds 127.0.0.1 only.
"""
import socket, sys, struct

SLAVE = 1
PORT  = 15002

def reply(req, mode):
    txid, proto, _len, unit, fc = struct.unpack('>HHHBB', req[:8])
    qty = struct.unpack('>H', req[10:12])[0]
    bc  = qty * 2
    data = bytes([0x00, 0x00, 0x04, 0xD2])[:bc] or b'\x00' * bc   # 1234 as int32

    if mode == 'normal':
        pdu = bytes([unit, 0x03, bc]) + data
        return struct.pack('>HHH', txid, 0, len(pdu)) + pdu
    if mode == 'badtxid':                      # late reply to an earlier transaction
        pdu = bytes([unit, 0x03, bc]) + data
        return struct.pack('>HHH', (txid + 1) & 0xFFFF, 0, len(pdu)) + pdu
    if mode == 'wrongunit':                    # reply addressed to another unit
        pdu = bytes([unit + 1, 0x03, bc]) + data
        return struct.pack('>HHH', txid, 0, len(pdu)) + pdu
    if mode == 'bigcount':                     # byteCount 255: frame (264B) exceeds the
        pdu = bytes([unit, 0x03, 0xFF]) + b'\xAA' * 255   # 256B receive buffer, so the
        return struct.pack('>HHH', txid, 0, len(pdu)) + pdu  # pre-existing n<9+bc check
                                               # already caught this one.
    if mode == 'overflow':                     # byteCount 100 — INSIDE the overflow window:
        bc2 = 100                              # frame is 109B (fits in 256), payload is
        pdu = bytes([unit, 0x03, bc2]) + b'\xBB' * bc2     # 103B -> written into buf[64].
        return struct.pack('>HHH', txid, 0, len(pdu)) + pdu
    if mode == 'truncated':                    # claims bc but sends 1 data byte
        pdu = bytes([unit, 0x03, bc]) + b'\x11'
        return struct.pack('>HHH', txid, 0, len(pdu)) + pdu
    raise SystemExit('unknown mode ' + mode)

def main():
    mode = sys.argv[1]
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', PORT)); srv.listen(1)
    print(f'[fake_dszl] mode={mode} listening 127.0.0.1:{PORT}', flush=True)
    conn, _ = srv.accept(); conn.settimeout(5)
    n = 0
    try:
        while True:
            req = conn.recv(256)
            if not req or len(req) < 12:
                break
            n += 1
            r = reply(req, mode)
            conn.sendall(r)
            print(f'[fake_dszl] req#{n} -> {len(r)}B', flush=True)
    except socket.timeout:
        pass
    finally:
        conn.close(); srv.close()

main()
