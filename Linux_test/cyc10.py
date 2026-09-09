import socket,time,re,sys
sys.stdout.reconfigure(line_buffering=True)
def ask(c,t=200):
    s=socket.create_connection(('127.0.0.1',5001),timeout=10); s.settimeout(t)
    s.sendall((c+'\n').encode()); b=b''
    while True:
        d=s.recv(4096)
        if not d: break
        b+=d
        for l in b.decode(errors='replace').splitlines():
            if l.startswith('OK') or l.startswith('ERR'): s.close(); return l.strip()
    return 'EOF'
ms=socket.create_connection(('127.0.0.1',9527),timeout=5); ms.settimeout(120)
mf=ms.makefile('rwb',buffering=0)
def mc(x):
    mf.write((x+'\n').encode()); return mf.readline().decode(errors='replace').strip()
def arm():
    a=re.search(r'\[M1\] pos=([-\d.]+) vel=([-\d.]+) tau=([-\d.]+).*?\[M2\] pos=([-\d.]+)', mc('STATUS'))
    return float(a.group(1)), float(a.group(3)), float(a.group(4))
def cups():
    return [int(v) for v in re.findall(r'p[5-8]=(-?\d+)', ask('status'))]
print('=== 20 週期：滾筒ON -> 放手臂 -> 滑台 0-140-0 -> 收手臂 -> 滾筒OFF ===')
print('（每輪重新 ENABLE：PARK 會停用馬達；並檢查 DEPLOY 是否真的壓上）')
t_all=time.time(); fails=0
for n in range(1, 21):
    t0=time.time()
    mc('M1 ENABLE'); mc('M2 ENABLE'); time.sleep(0.5)   # PARK 會停用，每輪必須重開
    ask('brush on')
    td=time.time(); rd=mc('DEPLOY 520 RIGHT'); td=time.time()-td
    time.sleep(1.5)
    p_dep, tau_dep, m2_dep = arm()
    # 🔴 檢查真的壓上：壓上時 θ 應在 0.60~0.72、tau > 8 Nm。DEPLOY 回 ERR 是正常的
    #    （壓在可壓縮接觸上不會收斂），所以不能用回傳值判斷，要看實際姿態。
    pressed = (0.55 < p_dep < 0.75) and tau_dep > 8.0
    if not pressed:
        fails += 1
        print('#%-2d 🔴 手臂沒有壓上  θ=%.4f tau=%+.2f  DEPLOY回覆=%s' % (n, p_dep, tau_dep, rd))
        ask('brush off'); mc('PARK'); break
    t1=time.time(); ask('rail 140'); t1=time.time()-t1
    p_140, tau_140, m2_140 = arm()
    t2=time.time(); ask('rail 0'); t2=time.time()-t2
    t3=time.time(); mc('PARK'); t3=time.time()-t3
    time.sleep(1.0)
    ask('brush off')
    c=cups()
    bad=[i+5 for i,v in enumerate(c) if v > -50]
    print('#%-2d 壓上 %.1fs θ=%.4f tau=%+6.2f | 掃 %.1f/%.1f s | 收 %.1fs | '
          'Δθ@140=%+.4f Δtau=%+.2f | M2 %.3f->%.3f | 吸盤 %s | 全輪 %.1fs%s'
          %(n, td, p_dep, tau_dep, t1, t2, t3,
            p_140-p_dep, tau_140-tau_dep, m2_dep, m2_140,
            ' '.join(str(v) for v in c), time.time()-t0,
            '' if not bad else '  🔴 吸盤 %s 失去密封'%bad))
    if bad:
        print('🔴 中止：吸盤失去密封'); break
    time.sleep(1.0)
print('=== 結束，總耗時 %.0fs，未壓上次數 %d ==='%(time.time()-t_all, fails))
