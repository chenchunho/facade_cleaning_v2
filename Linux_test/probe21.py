#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""唯讀探測：QX-DO24（slave 9）是否已經掛在 .21 上。
   讀 reg 0x22（固件版本，只讀）—— 不寫入任何東西。"""
import socket, sys

def crc16(d):
    c = 0xFFFF
    for b in d:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c

def probe(ip, slave=9, reg=0x0022):
    req = bytes([slave, 0x03, reg >> 8, reg & 0xFF, 0x00, 0x01])
    c = crc16(req)
    req += bytes([c & 0xFF, c >> 8])
    try:
        s = socket.create_connection((ip, 4001), timeout=4); s.settimeout(3)
        s.sendall(req)
        r = s.recv(64); s.close()
    except Exception as e:
        return "連線失敗: %s" % e
    if not r:
        return "無回應（線可能還沒接，或 slave 不在這條上）"
    if len(r) >= 5 and r[0] == slave and r[1] == 0x03:
        return "✅ 回應 slave=%d 版本=%d  raw=%s" % (r[0], (r[3] << 8) | r[4], r.hex())
    return "回應但不符預期: %s" % r.hex()

for ip in ("192.168.1.21", "192.168.1.22"):
    print("%-14s %s" % (ip, probe(ip)))
