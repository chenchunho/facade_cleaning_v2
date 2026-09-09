#!/usr/bin/env python3
# 量「送出壓上指令 → 手臂真的不動了 → 回覆 → settle 完成 → rail 送出」各段耗時。
import socket, sys, time, re
s = socket.create_connection(("127.0.0.1", 5001), timeout=10); f = s.makefile("r")
def ask(cmd, to, pre=("OK","ERR","WARN")):
    s.sendall((cmd+"\n").encode()); dl=time.time()+to
    while time.time()<dl:
        s.settimeout(max(0.2,dl-time.time()))
        try: l=f.readline()
        except socket.timeout: break
        if not l: break
        l=l.strip()
        if l and l.startswith(pre): return l
    return "<timeout>"
def arm():
    s.sendall(b"arm_status\n"); dl=time.time()+15
    while time.time()<dl:
        s.settimeout(max(0.2,dl-time.time()))
        try: l=f.readline()
        except socket.timeout: break
        if not l: break
        if l.strip().startswith("[M1]"):
            seg=l[:l.find("|")] if "|" in l else l
            d={k:float(v) for k,v in re.findall(r"([A-Za-z_]\w*)=(-?[0-9.]+)(?![0-9A-Za-z.])",seg)}
            return d
    return None
T0=time.time()
def mark(x): print("  %-34s +%6.2fs" % (x, time.time()-T0))
slot = sys.argv[1] if len(sys.argv)>1 else "RIGHT"
print("=== %s ===" % slot)
mark("送出 arm_deploy_f")
rep = ask("arm_deploy_f 15.0 %s" % slot, 150)
t_rep = time.time()
mark("arm_deploy_f 回覆")
print("     %s" % rep[:100])
# settle：逐次記錄，看要幾圈
t=time.time(); d=arm(); n=1     # 與 arm_cycle.py 一致：力控模式單次讀取
mark("交叉比對讀取（%d 次）" % n)
print("     ↑ 這段 %.2fs 全部發生在手臂已經停住之後" % (time.time()-t_rep))
mark("送出 rail 100")
r=ask("rail %d" % 100, 60)
mark("rail 100 完成")
r=ask("rail 0", 60); mark("rail 0 完成")
r=ask("arm_park", 90); mark("arm_park 完成")
s.close()
