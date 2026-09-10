import socket, struct, json, hashlib, sys

def xm_hash(pw):
    md = hashlib.md5(pw.encode()).digest()
    out = ''
    for i in range(8):
        n = (md[2*i] + md[2*i+1]) % 62
        if n < 10:   out += chr(48+n)
        elif n < 36: out += chr(65+n-10)
        else:        out += chr(97+n-36)
    return out

class XM:
    # 20-byte header: ff|ver|00 00|sid(4)|seq(4)|total|cur|msgid(2)|len(4)
    def __init__(self, host, port=34567):
        self.s = socket.create_connection((host, port), timeout=6)
        self.s.settimeout(6)
        self.sid = 0; self.seq = 0
    def send(self, msgid, obj):
        data = (json.dumps(obj) + '\n\x00').encode()
        head = struct.pack('<BBHIIBBHI', 0xff,0,0,self.sid,self.seq,0,0,msgid,len(data))
        self.s.sendall(head+data); self.seq += 1
    def recv(self):
        head=b''
        while len(head)<20:
            c=self.s.recv(20-len(head))
            if not c: return None
            head+=c
        _,_,_,sid,seq,_,_,mid,ln = struct.unpack('<BBHIIBBHI', head)
        self.sid=sid
        body=b''
        while len(body)<ln:
            c=self.s.recv(ln-len(body))
            if not c: break
            body+=c
        try: return json.loads(body.rstrip(b'\x00\n').decode('utf-8','replace'))
        except Exception: return body.rstrip(b'\x00\n').decode('utf-8','replace')
    def login(self,u,p):
        self.send(1000,{"EncryptType":"MD5","LoginType":"DVRIP-Web","PassWord":xm_hash(p),"UserName":u})
        return self.recv()
    def get(self,name):
        self.send(1042,{"Name":name,"SessionID":"0x%08X"%self.sid})
        return self.recv()

if __name__=="__main__":
    host=sys.argv[1]
    x=XM(host)
    r=x.login("admin","admin")
    print("LOGIN",host,"Ret=",r.get("Ret") if isinstance(r,dict) else r)

# ---- CLI: python3 xm_ipcam.py <ip> [dump|time]  (admin 空密碼) ----
if __name__ == "__main__":
    import sys
    host = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.112"
    op   = sys.argv[2] if len(sys.argv) > 2 else "dump"
    x = XM(host); print("login Ret=", x.login("admin", "").get("Ret"))
    if op == "time":
        import datetime
        now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        x.send(1450, {"Name":"OPTimeSetting","OPTimeSetting":now,"SessionID":"0x%08X"%x.sid}); x.recv()
        x.send(1452, {"Name":"OPTimeQuery","SessionID":"0x%08X"%x.sid})
        print("set", now, "-> dev", x.recv().get("OPTimeQuery"))
    else:
        for n in ["NetWork.NetCommon","NetWork.NetDNS","NetWork.NetNTP","Simplify.Encode"]:
            print("\n@@@", n); print(x.get(n))
