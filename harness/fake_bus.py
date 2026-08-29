#!/usr/bin/env python3
"""fake_bus — 一台假的 USR 網關，服務整條匯流排上的所有 slave。

與 `Linux_test/fake_slaves/fake_rtu.py` 的差別：那支是**單一 slave + 故意送壞幀**，
用來驗 driver 的回覆驗證路徑；這支是**整條 bus + 永遠送好幀**，用來讓整支主程式
跑起來並產生可比對的黃金軌跡（見 .claude/refactor_plan.md §5）。

    python3 fake_bus.py --port 15020 --proto rtu
    python3 fake_bus.py --port 15032 --proto tcp     # X518 (DSZL) 原生 Modbus TCP

🔴 最重要的性質：**回覆必須是請求的確定性函式。**
   等價性比對的前提是「同樣的 TX 一定得到同樣的 RX」。只要假從站有一點隨機性
   （時間、亂數、計數器），兩次執行就會分岔，而那個分岔**看起來會跟重構搬壞
   一模一樣** —— 你會去查一個根本沒壞的東西。
   因此：不使用亂數、不使用時鐘、不保存跨請求狀態。

📌 值是「合理的假值」不是真值。目的不是模擬機器，是讓兩個版本走過**同一條路徑**。
   只要兩邊看到相同的值，走到哪裡都可以比對；值本身對不對不影響等價性結論。
   （反過來說：**這支工具證明不了功能正確，只證明兩個版本行為相同。**）
"""
import argparse
import socket
import socketserver
import struct
import sys
import threading

# ── 線路格式 ────────────────────────────────────────────────────────────────


def crc16(buf: bytes) -> int:
    crc = 0xFFFF
    for b in buf:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def rtu_frame(payload: bytes) -> bytes:
    c = crc16(payload)
    return payload + bytes([c & 0xFF, c >> 8])


# ── 確定性的暫存器值 ────────────────────────────────────────────────────────
#
# 值只由 (slave, addr, index) 決定 —— 沒有時間、沒有亂數、沒有跨請求狀態。
# 刻意避開 0x0000 與 0xFFFF：前者常被當成「沒讀到」，後者常被當成錯誤碼，
# 兩者都會讓主程式走進錯誤分支而縮短軌跡覆蓋。


def reg_value(slave: int, addr: int, i: int) -> int:
    return ((slave << 8) ^ (addr + i) ^ 0x2A5A) & 0x7FFF | 0x0100


def zdt_status_payload() -> bytes:
    """ZDT 3.7.2 bulk read（FC 0x04 / Reg 0x0043 / Qty 0x0010）的合理回覆。

    🔴 為什麼需要這個特例：泛用的確定性亂值會讓 `pos_reached` 位元永遠不成立，
       於是每一個運動指令都等滿內部 15 秒逾時才回來 —— **motion 路徑只測到
       逾時分支，而且一輪要跑好幾分鐘**。要讓「運動完成」這條主線被走到，
       假從站就得回答得像個到位的馬達。
       📌 這是「泛用假值」不夠用的第一個案例：值本身不影響等價性結論，
          但**值會決定程式走哪條路徑**，而沒被走到的路徑 diff 永遠是綠的。

    欄位順序見 user_lib/ZDT_motor_control.cpp 的 get_system_status()。
    """
    import struct as _s
    r = []
    r.append(0x2010)                 # reg1: [byte count, param count] — 不解析
    r.append(24000)                  # reg2: 匯流排電壓 mV
    r.append(300)                    # reg3: 相電流 mA（低，不觸發障礙物判定）
    r.append(0)                      # reg4: 線性化編碼器
    r += [0, 0, 0]                   # reg5-7: 目標位置 sign + u32
    r += [0, 0]                      # reg8-9: 實際速度 sign + u16（0 = 停住）
    r += [0, 0, 0]                   # reg10-12: 實際位置 sign + u32
    r += [0, 0, 0]                   # reg13-15: 位置誤差 sign + u32
    # reg16: 高位=home flags（bit0 encoder_ready / bit1 calibration_ready）
    #        低位=motor flags（bit0 is_enabled / bit1 pos_reached）
    r.append((0x03 << 8) | 0x03)
    return b''.join(_s.pack('>H', v) for v in r)


def read_payload(slave: int, addr: int, qty: int) -> bytes:
    out = bytearray()
    for i in range(qty):
        out += struct.pack('>H', reg_value(slave, addr, i))
    return bytes(out)


def build_reply(req: bytes, proto: str, jc100=None):
    """依請求組出一個健康從站會送的回覆。回 None = 這個請求不回應（廣播）。"""
    if proto == 'rtu':
        slave, fc = req[0], req[1]
        body = req
    else:                                   # MBAP：前 6 bytes 是 header
        slave, fc = req[6], req[7]
        body = req[6:]

    # 🔴 廣播（slave 0x00）依 Modbus 規範沒有回覆。這支工具必須忠實地
    #    「什麼都不回」—— ZDT trigger_sync_move 的正確行為就建立在這件事上，
    #    假從站若禮貌地回一個 ack，就會把那條路徑測成錯的。
    if slave == 0x00:
        return None

    if fc in (0x03, 0x04):                  # read holding / input registers
        addr, qty = struct.unpack('>HH', body[2:6])
        if fc == 0x04 and addr == 0x0043 and qty == 0x0010:
            payload = zdt_status_payload()
        elif jc100 and slave in jc100 and fc == 0x03 and addr == 0x0001 and qty == 1:
            # JC-100 真空壓力，0.1 kPa 有號。回 -60.0 kPa（門檻是 -45）＝「吸住了」。
            # 🔴 與 ZDT 狀態同一個理由：泛用假值會讓 disable_seal 一直重試等密封，
            #    於是只測到重試/逾時分支，成功路徑永遠沒被走過。
            payload = struct.pack('>h', -600)
        else:
            payload = read_payload(slave, addr, qty)
        pdu = bytes([slave, fc, len(payload)]) + payload
    elif fc == 0x01:                        # read coils
        addr, qty = struct.unpack('>HH', body[2:6])
        bc = (qty + 7) // 8
        pdu = bytes([slave, fc, bc]) + bytes([(addr + i) & 0xFF | 0x01 for i in range(bc)])
    elif fc in (0x05, 0x06):                # write single — 回聲
        pdu = bytes([slave]) + body[1:6]
    elif fc == 0x10:                        # write multiple — slave/fc/addr/qty
        pdu = bytes([slave, fc]) + body[2:6]
    else:
        pdu = bytes([slave, fc | 0x80, 0x01])   # illegal function

    if proto == 'rtu':
        return rtu_frame(pdu)
    return req[0:4] + struct.pack('>H', len(pdu)) + pdu


# ── 切幀 ───────────────────────────────────────────────────────────────────
#
# ⚠ 透傳網關沒有幀邊界，邊界靠字元間隔決定（見 CLAUDE.md 的 `_pt = 0` 那段）。
#   這裡改用「依功能碼算出應有長度」來切，因為 TCP 上收到的可能是黏在一起的
#   兩個請求，也可能是被切成兩段的一個請求 —— 兩種都要處理，否則測出來的
#   分幀行為會是工具的行為而不是程式的行為。


def rtu_expected_len(buf: bytes):
    if len(buf) < 2:
        return None
    fc = buf[1]
    if fc in (0x01, 0x03, 0x04, 0x05, 0x06):
        return 8
    if fc == 0x10:
        if len(buf) < 7:
            return None
        return 9 + buf[6]
    return 8


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(None)
        buf = b''
        while True:
            try:
                chunk = self.request.recv(4096)
            except OSError:
                return
            if not chunk:
                return
            buf += chunk
            while True:
                if self.server.proto == 'rtu':
                    need = rtu_expected_len(buf)
                else:
                    if len(buf) < 6:
                        break
                    need = 6 + struct.unpack('>H', buf[4:6])[0]
                if need is None or len(buf) < need:
                    break
                req, buf = buf[:need], buf[need:]
                reply = build_reply(req, self.server.proto, self.server.jc100)
                if reply is not None:
                    try:
                        self.request.sendall(reply)
                    except OSError:
                        return


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--proto', choices=('rtu', 'tcp'), default='rtu',
                    help='rtu = RTU over USR gateway :4001；tcp = MBAP（X518 :502）')
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--jc100', default='',
                    help='這條 bus 上的 JC-100 真空表 slave（逗號分隔）。'
                         '刻意做成參數而不是猜：.20 上的 ZDT 也用 5-8，靠 bus 區分。')
    a = ap.parse_args()

    srv = Server((a.host, a.port), Handler)
    srv.proto = a.proto
    srv.jc100 = {int(x) for x in a.jc100.split(',') if x.strip()}
    # 唯一一行輸出，而且送 stderr —— 這支工具的 stdout 必須是乾淨的，
    # 免得混進被比對的軌跡裡。
    print(f'[fake_bus] {a.proto} on {a.host}:{a.port}', file=sys.stderr, flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
