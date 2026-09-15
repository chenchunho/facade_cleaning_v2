#!/usr/bin/env python3
"""Hostile fake crane: answers every command with OK, but glues a long EVT
broadcast right behind the reply in the SAME send, so a 512-byte recv gets
`OK…\n` + the head of the EVT and the tail arrives in the next recv.
This is the 2026-09-09 `water_inlet off` ×3 shape."""
import socket, threading, sys, time
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 15002
EVT = 'EVT motion_progress phase=main_loop ' + ' '.join(f'l_cm={220+i} r_cm={219+i} t_cm={223+i} kg_l=41.2 kg_r=60.1' for i in range(12)) + '\n'
assert len(EVT) >= 600, len(EVT)

def client(c):
    buf = b''
    try:
        while True:
            d = c.recv(4096)
            if not d: break
            buf += d
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                cmd = line.decode(errors='replace').strip()
                if not cmd: continue
                reply = f'OK {cmd}\n'
                # reply + EVT in one segment; then a second EVT split in two chunks with a gap
                # reply + a flood of 12 EVT lines (~7.2 KB) — more than the 4096-byte
                # drain cap in TCP_client::sendData(), so the cut lands mid-line.
                c.sendall((reply + EVT * 12).encode())
                time.sleep(0.05)
                half = len(EVT) // 2
                c.sendall(EVT[:half].encode()); time.sleep(0.15); c.sendall(EVT[half:].encode())
    except OSError:
        pass
    finally:
        c.close()

s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', PORT)); s.listen(8)
while True:
    c, _ = s.accept()
    threading.Thread(target=client, args=(c,), daemon=True).start()
