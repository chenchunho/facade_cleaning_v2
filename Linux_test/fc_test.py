import socket, struct, time
def crc16(b):
    c = 0xFFFF
    for x in b:
        c ^= x
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c
def tx(s, frame, label):
    f = frame + struct.pack("<H", crc16(frame))
    s.sendall(f)
    time.sleep(0.35)
    try:
        r = s.recv(256)
    except Exception:
        r = b""
    txs = " ".join("%02X" % x for x in f)
    rxs = " ".join("%02X" % x for x in r) if r else "(無回應)"
    print(label)
    print("   TX " + txs)
    print("   RX " + rxs)
s = socket.create_connection(("192.168.1.34", 4001), timeout=5)
s.settimeout(1.0)
time.sleep(0.2)
try:
    s.recv(4096)
except Exception:
    pass
tx(s, bytes([12, 0x01, 0x00, 0x00, 0x00, 0x10]), "① FC01 讀線圈 CH1-16  <- readAllStatus 走這條")
tx(s, bytes([12, 0x03, 0x00, 0x86, 0x00, 0x01]), "② FC03 讀狀態暫存器 0x0086 (CH1-16 bitmap)")
tx(s, bytes([12, 0x03, 0x00, 0x00, 0x00, 0x01]), "③ FC03 讀 0x0000 (對照組,已知可用)")
s.close()
