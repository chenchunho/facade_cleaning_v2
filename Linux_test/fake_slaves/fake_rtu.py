#!/usr/bin/env python3
"""Fake Modbus-RTU-over-TCP slave — drives user_lib/ drivers through their
reply-validation paths without any hardware.

Every user_lib driver except DSZL_107 speaks Modbus RTU tunnelled through a
USR-TCP232 transparent gateway on port 4001. This serves the same wire format
on localhost, and can deliberately corrupt the reply so a driver's validation
can be tested. DSZL_107 speaks Modbus TCP (MBAP) — use fake_dszl_tcp.py.

    python3 fake_rtu.py --mode badcrc --slave 1 --port 14001

Why this exists: on 2026-08-28 SD76 and DSZL were both found to accept garbled
frames and write past the caller's stack buffer. Both were fixed, and both fixes
were verified here — first against the UNPATCHED driver to prove the defect was
real, then against the patched one. Compiling is not verifying.

🔴 Two traps this file exists to help you avoid:

  1. Know whether the driver's init() probes. If it does, request #1 is the
     probe and must be answered normally or init fails before the test starts;
     the fault then belongs on request #2. `--fault-from` controls this.
     Getting it wrong makes every scenario "pass" while testing nothing.

  2. For length-overflow scenarios the byte count must land INSIDE the window
     where the frame still fits the driver's receive buffer but the payload
     exceeds the CALLER's buffer. An extreme value (0xFF) usually just gets
     caught by an existing length check and looks like "no defect here".
"""
import argparse, socket, struct, sys


def crc16(buf: bytes) -> int:
    crc = 0xFFFF
    for b in buf:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def framed(payload: bytes) -> bytes:
    """Append a correct RTU CRC (low byte first)."""
    c = crc16(payload)
    return payload + bytes([c & 0xFF, c >> 8])


def normal_reply(req: bytes, slave: int, fill: int) -> bytes:
    """Build the reply a healthy slave would send for this request."""
    fc = req[1]
    if fc in (0x03, 0x04):                       # read holding / input registers
        qty = struct.unpack('>H', req[4:6])[0]
        bc = qty * 2
        return framed(bytes([slave, fc, bc]) + bytes([fill]) * bc)
    if fc == 0x01:                               # read coils
        qty = struct.unpack('>H', req[4:6])[0]
        bc = (qty + 7) // 8
        return framed(bytes([slave, fc, bc]) + bytes([fill]) * bc)
    if fc in (0x05, 0x06):                       # write single — echo the request
        return framed(bytes([slave]) + req[1:6])
    if fc == 0x10:                               # write multiple — addr + qty
        return framed(bytes([slave, fc]) + req[2:6])
    raise SystemExit(f'fake_rtu: unsupported function code 0x{fc:02X}')


def corrupt(reply: bytes, req: bytes, mode: str, slave: int, bc_override: int):
    """Return the bytes to send, or None to send NOTHING (mode 'drop')."""
    if mode == 'normal':
        return reply
    if mode == 'drop':
        # [2026-08-28] Silence — the slave simply does not answer.
        # 這是唯一能驗到「重試」與「逾時」路徑的模式：其他模式都會回一個
        # 壞掉的東西，走的是「收到了但不合法」那條路，跟「根本沒收到」不同。
        # 實機動機：QX-DO24 的 PWM 寫入約 20% 出現 `no reply (timeout)`，
        # 而當天等不到失敗自然發生，重試救援路徑始終沒被觸發。
        return None
    if mode == 'badcrc':                         # data intact, one CRC bit flipped
        return reply[:-2] + bytes([reply[-2] ^ 0x01, reply[-1]])
    if mode == 'wrongslave':                     # reply addressed to another slave
        return framed(bytes([(slave + 1) & 0xFF]) + reply[1:-2])
    if mode == 'badfc':                          # function code we did not ask for
        return framed(bytes([reply[0], 0x7F]) + reply[2:-2])
    if mode == 'shortframe':                     # truncated, no CRC at all
        return reply[:len(reply) // 2] or reply[:1]
    if mode == 'bigcount':                       # byte count 0xFF — often caught by
        return framed(bytes([slave, req[1], 0xFF]) + b'\xAA' * 255)   # an existing check
    if mode == 'overflow':                       # byte count chosen to land in the
        bc = bc_override                         # caller-buffer overflow window
        return framed(bytes([slave, req[1], bc]) + b'\xBB' * bc)
    raise SystemExit(f'fake_rtu: unknown mode {mode}')


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--mode', required=True,
                    help='normal | drop | badcrc | wrongslave | badfc | shortframe '
                         '| bigcount | overflow')
    ap.add_argument('--slave', type=int, default=1)
    ap.add_argument('--port', type=int, default=14001)
    ap.add_argument('--fill', type=lambda s: int(s, 0), default=0x11,
                    help='byte used for register data in a healthy reply')
    ap.add_argument('--overflow-bc', type=int, default=100,
                    help='byte count used by --mode overflow; pick a value INSIDE '
                         'the window (frame still fits the driver receive buffer, '
                         'payload exceeds the caller buffer)')
    ap.add_argument('--drop-count', type=int, default=0, metavar='N',
                    help='mode=drop 時只丟掉前 N 個該出錯的請求，之後恢復正常回覆。'
                         '0 = 一直丟。用它驗「重試在第幾次救回來」：例如 driver 重試 '
                         '3 次時，--drop-count 2 應該在第 3 次成功。')
    ap.add_argument('--fault-from', type=int, default=1, metavar='N',
                    help='request number at which the fault starts; use 2 when the '
                         'driver init() probes, so the probe is answered normally')
    args = ap.parse_args()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', args.port))
    srv.listen(1)
    print(f'[fake_rtu] mode={args.mode} slave={args.slave} '
          f'fault-from=#{args.fault_from} listening 127.0.0.1:{args.port}', flush=True)

    conn, _ = srv.accept()
    conn.settimeout(15)   # drop 模式下 driver 每次重試要等滿逾時，5s 不夠
    n = 0
    dropped = 0
    try:
        while True:
            req = conn.recv(256)
            if not req or len(req) < 6:
                break
            n += 1
            healthy = normal_reply(req, args.slave, args.fill)
            mode = args.mode if n >= args.fault_from else 'normal'
            if mode == 'drop' and args.drop_count > 0 and dropped >= args.drop_count:
                mode = 'normal'          # 額度用完 → 恢復正常，驗證重試能救回來
            out = corrupt(healthy, req, mode, args.slave, args.overflow_bc)
            if out is None:
                dropped += 1
                print(f'[fake_rtu] req#{n} fc=0x{req[1]:02X} mode=drop -> (無回覆 #{dropped})',
                      flush=True)
                continue
            conn.sendall(out)
            print(f'[fake_rtu] req#{n} fc=0x{req[1]:02X} mode={mode} -> {len(out)}B', flush=True)
    except socket.timeout:
        pass
    finally:
        conn.close()
        srv.close()


main()
