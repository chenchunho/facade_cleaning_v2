import socket, struct, time
def crc16(b):
    c = 0xFFFF
    for x in b:
        c ^= x
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c
def xact(s, frame, label):
    f = frame + struct.pack("<H", crc16(frame))
    s.sendall(f); time.sleep(0.4)
    try: r = s.recv(256)
    except Exception: r = b""
    print("%-42s TX %s | RX %s" % (label,
          " ".join("%02X" % x for x in f),
          " ".join("%02X" % x for x in r) if r else "(無回應)"))
    return r
def rd(s, addr, cnt, label):
    r = xact(s, bytes([12, 0x03, addr >> 8, addr & 0xFF, 0, cnt]), label)
    if len(r) >= 5 and r[1] == 0x03:
        bc = r[2]
        return [(r[3+i*2] << 8) | r[4+i*2] for i in range(bc // 2)]
    return None

s = socket.create_connection(("192.168.1.34", 4001), timeout=5); s.settimeout(1.2)
time.sleep(0.2)
try: s.recv(4096)
except Exception: pass

print("=== 寫入前 ===")
print("  0x0040~0x0044 =", rd(s, 0x0040, 5, "read config"))
print()
print("=== 寫 0x0044 = 7 (115200) ===")
xact(s, bytes([12, 0x06, 0x00, 0x44, 0x00, 0x07]), "FC06 write 0x0044=7")
print("  讀回 0x0044 =", rd(s, 0x0044, 1, "verify 0x0044"))
print()
print("=== 寫 0x0043 = 12 (站號) ===")
xact(s, bytes([12, 0x06, 0x00, 0x43, 0x00, 0x0C]), "FC06 write 0x0043=12")
print("  讀回 0x0043 =", rd(s, 0x0043, 1, "verify 0x0043"))
print()
print("=== 寫入後全貌 ===")
print("  0x0040~0x0044 =", rd(s, 0x0040, 5, "read config"))
print()
print("=== 通訊仍正常? ===")
xact(s, bytes([12, 0x01, 0x00, 0x00, 0x00, 0x10]), "FC01 讀線圈")
s.close()
