#!/usr/bin/env python3
# xm_ipcam.py — XiongMai IP camera config over the XM private protocol (TCP 34567).
# Restored 2026-10-07 from Linux_test/xm_ipcam.py (09-10; removed with Linux_test/ on 09-16) and
# extended with `osd`. Protocol notes: .claude/summaries/IPCAM_XIONGMAI_SUMMARY.md.
#
# Run it ON THE BODY PI — the cameras (.112/.113) sit on the body's wired LAN:
#   python3 xm_ipcam.py <ip> get [Name]      # read one config section (default AVEnc.VideoWidget)
#   python3 xm_ipcam.py <ip> osd off|on      # time + channel-title overlay burned into the stream
#   python3 xm_ipcam.py <ip> time            # push this Pi's wall clock to the camera
# `osd` saves the full section to ~/run/cam_osd_backup_<last octet>_<stamp>.json before writing.
# 2026-10-07 per user: OSD turned OFF on both (the camera clocks were stale, both titles read "CAM01").
#
# admin has an EMPTY password on both cameras (per user, LAN only). Never port-forward 34567/8899/8000.
import datetime, hashlib, json, os, socket, struct, sys, time


def xm_hash(pw):
    """XM 'sofia' hash: MD5, then each byte pair summed mod 62 -> [0-9A-Za-z], 8 chars."""
    md = hashlib.md5(pw.encode()).digest()
    out = ''
    for i in range(8):
        n = (md[2 * i] + md[2 * i + 1]) % 62
        out += chr(48 + n) if n < 10 else chr(65 + n - 10) if n < 36 else chr(97 + n - 36)
    return out


class XM:
    # 20-byte header: ff|ver|00 00|sid(4)|seq(4)|total|cur|msgid(2)|len(4)
    LOGIN, SET_CONFIG, GET_CONFIG, SET_TIME, GET_TIME = 1000, 1040, 1042, 1450, 1452

    def __init__(self, host, port=34567):
        self.s = socket.create_connection((host, port), timeout=6)
        self.s.settimeout(6)
        self.sid = 0
        self.seq = 0

    def send(self, msgid, obj):
        data = (json.dumps(obj) + '\n\x00').encode()
        head = struct.pack('<BBHIIBBHI', 0xff, 0, 0, self.sid, self.seq, 0, 0, msgid, len(data))
        self.s.sendall(head + data)
        self.seq += 1

    def _read(self, n):
        b = b''
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c:
                return None
            b += c
        return b

    def recv(self):
        head = self._read(20)
        if head is None:
            return None
        _, _, _, sid, _, _, _, _, ln = struct.unpack('<BBHIIBBHI', head)
        self.sid = sid
        body = self._read(ln) or b''
        txt = body.rstrip(b'\x00\n').decode('utf-8', 'replace')
        try:
            return json.loads(txt)
        except ValueError:
            return {'raw': txt}

    def call(self, msgid, obj):
        obj = dict(obj, SessionID='0x%08X' % self.sid)
        self.send(msgid, obj)
        return self.recv() or {}

    def login(self, user='admin', pw=''):
        self.send(self.LOGIN, {'EncryptType': 'MD5', 'LoginType': 'DVRIP-Web', 'PassWord': xm_hash(pw), 'UserName': user})
        return self.recv() or {}

    def get(self, name):
        return self.call(self.GET_CONFIG, {'Name': name})

    def set(self, name, value):
        return self.call(self.SET_CONFIG, {'Name': name, name: value})


def osd(x, ip, on):
    name = 'AVEnc.VideoWidget'
    g = x.get(name)
    if g.get('Ret') != 100:
        sys.exit('%s get %s failed: %s' % (ip, name, g))
    bk = os.path.expanduser('~/run/cam_osd_backup_%s_%s.json' % (ip.split('.')[-1], time.strftime('%m%d-%H%M%S')))
    with open(bk, 'w') as f:
        json.dump(g, f, ensure_ascii=False, indent=1)
    w = g[name]
    for ch in w:
        for k in ('TimeTitleAttribute', 'ChannelTitleAttribute'):
            if k in ch:
                ch[k]['EncodeBlend'] = on     # overlay in the encoded stream (what RTSP / Dashboard sees)
                ch[k]['PreviewBlend'] = on    # overlay in the camera's own preview
    r = x.set(name, w)
    back = x.get(name).get(name, [])
    print('%s osd %s: backup %s, set Ret=%s, readback %s' % (
        ip, 'on' if on else 'off', bk, r.get('Ret'),
        [(k, c[k]['EncodeBlend']) for c in back for k in ('TimeTitleAttribute', 'ChannelTitleAttribute') if k in c]))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__ or 'usage: xm_ipcam.py <ip> get [Name] | osd off|on | time')
    ip, op = sys.argv[1], sys.argv[2]
    x = XM(ip)
    r = x.login()
    if r.get('Ret') != 100:
        sys.exit('%s login failed: %s' % (ip, r))
    if op == 'get':
        print(json.dumps(x.get(sys.argv[3] if len(sys.argv) > 3 else 'AVEnc.VideoWidget'), ensure_ascii=False, indent=1))
    elif op == 'osd' and len(sys.argv) > 3 and sys.argv[3] in ('on', 'off'):
        osd(x, ip, sys.argv[3] == 'on')
    elif op == 'time':
        now = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        x.call(x.SET_TIME, {'Name': 'OPTimeSetting', 'OPTimeSetting': now})
        print('%s set %s -> camera %s' % (ip, now, x.call(x.GET_TIME, {'Name': 'OPTimeQuery'}).get('OPTimeQuery')))
    else:
        sys.exit('usage: xm_ipcam.py <ip> get [Name] | osd off|on | time')


if __name__ == '__main__':
    main()
