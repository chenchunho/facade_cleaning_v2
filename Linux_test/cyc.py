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
    # 解析不到 4 顆就回 None —— 空 list 會讓下面的 bad 判斷變成空的，
    # 也就是「讀不到壓力」會被當成「沒有失壓」而放行。
    v = [int(x) for x in re.findall(r'p[5-8]=(-?\d+)', ask('status'))]
    return v if len(v) == 4 else None
# [2026-09-03 per user] 行程與滾筒繼電器改成參數 —— 不再另外複製一支腳本。
#   usage: cyc.py [週期數] [滑台行程cm] [brush|nobrush]
# 🔴 `nobrush` 的由來：09-03 滾筒電路故障，per user 要求在滾筒**不轉**的狀態下跑。
#    ⚠️ 不轉的滾筒壓著玻璃橫走＝拖著它刮。per user 明確要求靠上，這裡照做，
#    但記在這裡：那不是常態，日後看紀錄要知道當時滾筒沒轉。
N         = int(sys.argv[1]) if len(sys.argv) > 1 else 10
RAIL_CM   = int(sys.argv[2]) if len(sys.argv) > 2 else 130
USE_BRUSH = (sys.argv[3].lower() != 'nobrush') if len(sys.argv) > 3 else True
# [2026-09-03 per user] 工具槽也參數化：RIGHT=滾筒 / LEFT=刮刀 / CENTER。
# DEPLOY 走 lr_move_to_slot_impl，會自己先收 M1 再切工具再壓上 —— 順序由程式保證，
# 不需要（也不應該）在外面先下 LR_SLOT。
SLOT = sys.argv[4].upper() if len(sys.argv) > 4 else 'RIGHT'
assert SLOT in ('LEFT', 'CENTER', 'RIGHT'), 'slot 必須是 LEFT/CENTER/RIGHT'
TOOL = {'RIGHT': '滾筒', 'LEFT': '刮刀', 'CENTER': 'CENTER'}[SLOT]
print('=== %d 週期：%s -> 壓上(%s) -> 滑台 0-%d-0 -> 收手臂%s ===' %
      (N, '滾筒ON' if USE_BRUSH else '滾筒關(電路故障)', TOOL, RAIL_CM,
       ' -> 滾筒OFF' if USE_BRUSH else ''))
t_all=time.time()
for n in range(1, N + 1):
    t0=time.time()
    mc('M1 ENABLE'); mc('M2 ENABLE'); time.sleep(0.5)   # PARK 會停用馬達
    if USE_BRUSH: ask('brush on')
    td=time.time(); mc('DEPLOY 520 %s' % SLOT); td=time.time()-td
    time.sleep(1.5)
    p_dep, tau_dep, m2_dep = arm()
    # 用實際姿態判斷有沒有真的壓上（DEPLOY 壓玻璃時本來就回 ERR）
    if not (0.55 < p_dep < 0.75 and tau_dep > 8.0):
        print('#%-2d 🔴 手臂沒有壓上  θ=%.4f tau=%+.2f'%(n,p_dep,tau_dep))
        (ask('brush off') if USE_BRUSH else None); mc('PARK'); break
    t1=time.time(); ask('rail %d' % RAIL_CM); t1=time.time()-t1
    p_rail, tau_rail, m2_rail = arm()
    t2=time.time(); ask('rail 0');   t2=time.time()-t2
    p_ret, tau_ret, m2_ret = arm()
    t3=time.time(); mc('PARK'); t3=time.time()-t3
    time.sleep(1.0)
    if USE_BRUSH: ask('brush off')
    c=cups()
    if c is None:
        print('#%-2d 🔴 吸盤壓力讀不到（status 解析不到 4 顆）—— 視同失敗' % n)
        (ask('brush off') if USE_BRUSH else None); mc('PARK'); break
    bad=[i+5 for i,v in enumerate(c) if v > -50]      # 只看有沒有真的失壓，不要求逐格相同
    print('#%-2d 壓上 %.1fs θ=%.4f tau=%+6.2f | 掃 %.1f/%.1f | 收 %.1fs | '
          'Δθ@%d=%+.4f | M2 %.3f→%.3f→%.3f 擺動 %.3f | 吸盤 %s | %.1fs%s'
          %(n, td, p_dep, tau_dep, t1, t2, t3, RAIL_CM, p_rail-p_dep,
            m2_dep, m2_rail, m2_ret, max(m2_dep,m2_rail,m2_ret)-min(m2_dep,m2_rail,m2_ret),
            ' '.join(str(v) for v in c), time.time()-t0,
            '' if not bad else '  🔴 吸盤 %s 失壓'%bad))
    if bad:
        print('🔴 中止：吸盤失壓'); break
    time.sleep(1.0)
print('=== 結束，總耗時 %.0fs ==='%(time.time()-t_all))
