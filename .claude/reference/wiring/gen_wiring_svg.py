# Generates two one-line wiring diagrams (SVG) for facade_cleaning_v2 control cabinets.
# Coordinates are computed so stubs/trunks never cross except where noted (junction dots mark real joins).
import html

RAIL = {  # semantic colours (CSS vars resolved on page via class); here we use class names
 'ac':'r-ac','v24':'r-24','v48':'r-48','v5':'r-5','v57':'r-57','bus':'r-bus','pwm':'r-pwm','eth':'r-eth','can':'r-can','unk':'r-unk'}

def esc(s): return html.escape(s, quote=False)

class SVG:
    def __init__(s,w,h,label):
        s.w,s.h=w,h; s.parts=[]; s.label=label
    def rect(s,x,y,w,h,cls,rx=2,dashed=False):
        d=' stroke-dasharray="4 3"' if dashed else ''
        s.parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" class="{cls}"{d}/>')
    def text(s,x,y,t,cls='t',anchor='start',size=None):
        st=f' style="font-size:{size}px"' if size else ''
        s.parts.append(f'<text x="{x}" y="{y}" text-anchor="{anchor}" class="{cls}"{st}>{esc(t)}</text>')
    def line(s,x1,y1,x2,y2,cls,dashed=False,arrow=False,width=None):
        d=' stroke-dasharray="5 3"' if dashed else ''
        a=' marker-end="url(#ah)"' if arrow else ''
        wd=f' stroke-width="{width}"' if width else ''
        s.parts.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" class="{cls}"{d}{a}{wd}/>')
    def poly(s,pts,cls,dashed=False,arrow=False,width=None):
        d=' stroke-dasharray="5 3"' if dashed else ''
        a=' marker-end="url(#ah)"' if arrow else ''
        wd=f' stroke-width="{width}"' if width else ''
        p=' '.join(f'{x},{y}' for x,y in pts)
        s.parts.append(f'<polyline points="{p}" class="{cls}" fill="none"{d}{a}{wd}/>')
    def dot(s,x,y,cls):
        s.parts.append(f'<circle cx="{x}" cy="{y}" r="3.2" class="dot {cls}"/>')
    def q(s,x,y):
        s.parts.append(f'<circle cx="{x}" cy="{y}" r="7" class="qmark"/><text x="{x}" y="{y+4}" text-anchor="middle" class="qtxt">?</text>')
    def box(s,x,y,w,h,title,sub=None,cls='node',dashed=False,tag=None):
        s.rect(x,y,w,h,cls,dashed=dashed)
        if sub:
            s.text(x+9,y+15,title,'nt'); s.text(x+9,y+29,sub,'ns')
        else:
            s.text(x+9,y+h/2+4.5,title,'nt')
        if tag: s.text(x+w-8,y+h-6,tag,'tag','end')
    def render(s):
        defs='<defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>'
        return (f'<svg viewBox="0 0 {s.w} {s.h}" role="img" aria-label="{esc(s.label)}" class="wire">{defs}'+''.join(s.parts)+'</svg>')

# ---------------------------------------------------------------- BODY
def body():
    S=SVG(1170,720,'本體電控箱單線接線圖:AC 進線經四顆電源轉為 24V/48V/5V/57.6V 匯流排,各裝置由匯流排取電並經 RS485 三段、CAN、UART、PWM 與 Ethernet 連到網關、Pi5 與交換器')
    # bars (x) ordered so the most-used (24V) is nearest the device column
    BAR={'v5':65,'v57':175,'v48':285,'v24':395}
    # AC feed line
    S.text(17,11,'200 m 臍帶 AC · 單相 220V(與吊機同一組;容量 ?)','lbl')
    S.line(17,18,443,18,'l-ac',width=2)
    psus=[('v5',17,'5V PSU','本體專用',False),('v57',127,'NPP-1700-48 ×2','57.6V 並聯 · 位置?',True),
          ('v48',237,'LRS-150-48','48V · 150 W',False),('v24',347,'LRS-150-24','24V·150W 唯一',False)]
    for rail,x,t,sub,dash in psus:
        S.line(x+48,18,x+48,28,'l-ac',width=2)
        S.box(x,28,96,40,t,sub,cls=('node psu-'+rail),dashed=dash)
    # bars
    BAR_END={'v24':588,'v48':358,'v57':450,'v5':690}
    for rail,x in BAR.items():
        S.line(x,68,x,BAR_END[rail],'l-'+rail,width=3)
    # 5V bottom run → gateway column feed at x=880
    S.poly([(65,690),(880,690),(880,258)],'l-v5',width=3)
    S.text(470,684,'5V(本體專用)→ 網關 ×3','lbl-r r-5','middle')

    DX,DW,DH=440,200,36
    rows={'zdt':110,'pqw':156,'loads':202,'brake':294,'dm2j':340,'qx':386,'esc':432,'jc':478,'xkc':524,'dam':570,'imu':616}
    C=lambda k:rows[k]+DH/2
    # device boxes
    S.box(DX,rows['zdt'],DW,DH,'ZDT 步進推桿 ×4','slave 5,6,7,8 · 吸盤伸縮',tag='B1')
    S.box(DX,rows['pqw'],DW,DH,'PQW 繼電器','slave 12 · 實體 8CH',tag='B1')
    S.rect(DX,rows['loads'],DW,82,'node grp')
    S.text(DX+9,rows['loads']+14,'PQW 乾接點負載(24V)','nt')
    for i,t in enumerate(['CH1 VT307 真空閥(4 吸盤共用)','CH2 幫浦 A  ·  CH3 幫浦 B','CH4 噴水馬達 · CH5 滾筒 ×2','CH6 正壓閥(幾顆?) · CH7–8 空']):
        S.text(DX+9,rows['loads']+29+i*13,t,'ns')
    S.box(DX,rows['brake'],DW,DH,'上滑台抱閘','失電抱閘 → 常時通電')
    S.box(DX,rows['dm2j'],DW,DH,'DM2J-RS570 + 57CM26BZ','slave 14 · 上滑台',tag='B1')
    S.box(DX,rows['qx'],DW,DH,'QX-DO24 PWM 模組','slave 9 · 只用 ch1',tag='B2')
    S.box(DX,rows['esc'],DW,DH,'FLAME 100A ESC ×2 → 螺旋槳 ×2','兩顆並接同一路 PWM',cls='node crit')
    S.box(DX,rows['jc'],DW,DH,'JC-100 真空壓力 ×4','slave 5,6,7,8',tag='B3')
    S.box(DX,rows['xkc'],DW,DH,'XKC-Y25 液位','slave 13 · 低水位',tag='B3')
    S.box(DX,rows['dam'],DW,DH,'Damiao M1 + M2 手臂馬達','M1 0x01 / M2 0x02 · 接 24V',cls='node crit')
    S.box(DX,rows['imu'],DW,DH,'WT901 IMU','/dev/ttyUSB0 · USB 供電')
    # power stubs (left) — dot at bar junction
    def pstub(k,rail):
        y=C(k); x=BAR[rail]; S.line(DX,y,x,y,'l-'+rail,width=2); S.dot(x,y,'d-'+rail)
    for k in ['zdt','pqw','brake','dam']: pstub(k,'v24')
    y=rows['loads']+41; S.line(DX,y,BAR['v24'],y,'l-v24',width=2); S.dot(BAR['v24'],y,'d-v24')
    S.text(DX-6,y-5,'經接點','lbl-r','end')
    pstub('dm2j','v48'); pstub('esc','v57')
    for k in ['qx','jc','xkc']: pstub(k,'v24')
    y=C('imu'); S.text(DX-6,y+4,'電由 Pi5 USB 供','lbl-r','end')
    # PQW → loads control ; QX → ESC pwm
    S.line(DX+100,rows['pqw']+DH,DX+100,rows['loads'],'l-bus',arrow=True,width=1.5)
    S.text(DX+112,rows['loads']-2,'乾接點 CH1–6','lbl')
    S.line(DX+100,rows['qx']+DH,DX+100,rows['esc'],'l-pwm',arrow=True,width=2)
    S.text(DX+112,rows['esc']-2,'PWM ch1 一路','lbl-pwm')
    # signal trunks
    TR={'B1':710,'B2':735,'B3':760,'PI':785}
    GX,GW=900,160
    grows={'teth':110,'fx':156,'sw':202,'u20':248,'u21':294,'u22':340,'pi':386}
    GC=lambda k:grows[k]+DH/2
    S.box(GX,grows['teth'],GW,DH,'2-wire tether','HomePlug AV · 頂樓來')
    S.box(GX,grows['fx'],GW,DH,'Fathom-X 載波(機身端)','吃 24V · VFD 一轉掉 90%',cls='node crit')
    S.box(GX,grows['sw'],GW,DH,'5-port hub','非 PoE(機身無相機)')
    S.box(GX,grows['u20'],GW,DH,'USR-TCP232-304  .20','B1 動力/滑台 · 8N1')
    S.box(GX,grows['u21'],GW,DH,'USR-TCP232-304  .21','B2 PWM 獨佔 · 8N1')
    S.box(GX,grows['u22'],GW,DH,'USR-TCP232-304  .22','B3 感測 · 8N1')
    S.box(GX,grows['pi'],GW,DH,'Raspberry Pi 5','獨立供電(來源?)')
    S.line(GX+80,grows['teth']+DH,GX+80,grows['fx'],'l-eth',arrow=True,width=1.5)
    S.line(GX+80,grows['fx']+DH,GX+80,grows['sw'],'l-eth',arrow=True,width=1.5)
    # trunks: (trunk, gateway row, device rows, rail class, label)
    def trunk(tk,gk,dks,cls,lbl):
        x=TR[tk]; ys=[C(d) for d in dks]+[GC(gk)-6]
        S.line(x,min(ys),x,max(ys),cls,width=2)
        for d in dks:
            y=C(d); S.line(DX+DW,y,x,y,cls,width=2); S.dot(x,y,'d-'+cls[2:])
        y=GC(gk)-6; S.line(x,y,GX,y,cls,width=2); S.dot(x,y,'d-'+cls[2:])
        S.text(x,min(ys)-5,lbl,'lbl-b','middle')
    trunk('B1','u20',['zdt','pqw','dm2j'],'l-bus','B1 RS485')
    trunk('B2','u21',['qx'],'l-bus','B2')
    trunk('B3','u22',['jc','xkc'],'l-bus','B3')
    # Pi direct: CAN + UART
    x=TR['PI']; y0=C('dam'); y1=C('imu'); yg=GC('pi')-6
    S.line(x,yg,x,y1,'l-can',width=2)
    S.line(DX+DW,y0,x,y0,'l-can',width=2); S.dot(x,y0,'d-can'); S.text(DX+DW+6,y0-4,'USB-CAN /dev/ttyACM0','lbl-can')
    S.line(DX+DW,y1,x,y1,'l-can',width=2); S.dot(x,y1,'d-can'); S.text(DX+DW+6,y1-4,'USB(UART + 5V)','lbl-can')
    S.line(x,yg,GX,yg,'l-can',width=2); S.dot(x,yg,'d-can')
    S.text(x,yg-5,'Pi 直連','lbl-b','middle')
    # 24V → Fathom-X (machine end): route over the top, clear of every column
    yf=GC('fx'); S.poly([(BAR['v24'],90),(860,90),(860,yf),(GX,yf)],'l-v24',width=2); S.dot(BAR['v24'],90,'d-v24')
    S.text(620,86,'24V → 載波模組(per user)','lbl-r','middle')
    # 5V to gateways (x=880 vertical, stubs at center+6)
    for k in ['u20','u21','u22']:
        y=GC(k)+6; S.line(880,y,GX,y,'l-v5',width=2); S.dot(880,y,'d-v5')
    # Ethernet: switch → vertical x=1105 → stubs
    EX=1105
    S.poly([(GX+GW,GC('sw')),(EX,GC('sw')),(EX,GC('pi'))],'l-eth',dashed=True,width=1.5)
    for k in ['u20','u21','u22','pi']:
        y=GC(k); S.line(GX+GW,y,EX,y,'l-eth',dashed=True,width=1.5); S.dot(EX,y,'d-eth')
    S.text(EX+4,GC('sw')-6,'Ethernet','lbl-b')
    return S.render()

# ---------------------------------------------------------------- CRANE
def crane():
    S=SVG(1140,800,'吊機電控箱單線接線圖:AC 進線直接餵三台變頻器與 5V 電源;SE3 左右各獨佔一段 RS485,SD76 與 ZS-DIO 各一段,X518 走原生 Ethernet;交換器經 Fathom-X 載波連向機身')
    AX=60
    S.text(AX-35,11,'單相 220V AC(與本體同一組;容量 ?)','lbl')
    S.line(AX,18,AX,540,'l-ac',width=3)
    S.text(AX+8,34,'無斷路器/保險絲','lbl-crit')
    DX,DW,DH=120,160,36
    rows={'se3l':90,'se3r':150,'mh':210,'sd76':270,'zs':330,'x518':390}
    C=lambda k:rows[k]+DH/2
    S.box(DX,rows['se3l'],DW,DH,'SE3 變頻器 左','slave 1',tag='.30')
    S.box(DX,rows['se3r'],DW,DH,'SE3 變頻器 右','slave 1',tag='.31')
    S.box(DX,rows['mh'],DW,DH,'MH300 變頻器','段/slave/供電 皆未確認',dashed=True)
    S.box(DX,rows['sd76'],DW,DH,'SD76 計米 ×3','左 1 / 右 2 / 中 4',tag='.34')
    S.box(DX,rows['zs'],DW,DH,'ZS-DIO 繼電器 4CH','slave 1',tag='.32')
    S.box(DX,rows['x518'],DW,DH,'X518 張力採集','雙通道 · 原生 Modbus-TCP',tag='.33')
    # AC stubs
    for k in ['se3l','se3r']:
        y=C(k); S.line(AX,y,DX,y,'l-ac',width=2); S.dot(AX,y,'d-ac')
    y=C('mh'); S.line(AX,y,DX,y,'l-ac',dashed=True,width=2); S.q(AX+30,y-14)
    y=C('sd76'); S.line(AX,y,DX,y,'l-ac',width=2); S.dot(AX,y,'d-ac'); S.text(DX-6,y-5,'220V','lbl-r','end')
    # crane 24V PSU (MEAN WELL 150 W) below the device column, fed from AC; feeds ZS-DIO, X518, Fathom-X
    PY_=450; S.box(DX,PY_,DW,DH,'MW 150W 24V(吊機)','型號待確認 · 餵 X518/ZS-DIO/載波',cls='node psu-v24')
    S.line(AX,PY_+DH/2+6,DX,PY_+DH/2+6,'l-ac',width=2); S.dot(AX,PY_+DH/2+6,'d-ac')
    S.poly([(DX,PY_+DH/2-6),(100,PY_+DH/2-6),(100,C('zs'))],'l-v24',width=2)
    for k in ['zs','x518']:
        y=C(k); S.line(100,y,DX,y,'l-v24',width=2); S.dot(100,y,'d-v24')
    # loads right of device (motors etc.)
    LX,LW=320,130
    S.box(LX,rows['se3l'],LW,DH,'三相馬達 左','ø6 鋼索')
    S.box(LX,rows['se3r'],LW,DH,'三相馬達 右','ø6 鋼索')
    S.box(LX,rows['mh'],LW,DH,'中央捲盤馬達','水管+隧道電纜 200 m')
    S.box(LX,rows['sd76'],LW,DH,'計米輪 ×3','中(4) runtime 讀不到',cls='node warn')
    S.box(LX,rows['zs'],LW,DH,'進水球閥','CH4 · CH1–3 空')
    S.box(LX,rows['x518'],LW,DH,'拉力感測器 ×2','左 CH2 / 右 CH1')
    for k in ['se3l','se3r','mh']:
        y=C(k); S.line(DX+DW,y,LX,y,'l-ac',width=2.5); S.text((DX+DW+LX)/2,y-4,'U/V/W','lbl-r','middle')
    y=C('sd76'); S.line(LX,y,DX+DW,y,'l-sig',width=1.5,arrow=True)
    y=C('zs'); S.line(DX+DW,y,LX,y,'l-sig',width=1.5,arrow=True); S.text((DX+DW+LX)/2,y-4,'接點','lbl-r','middle')
    y=C('x518'); S.line(LX,y,DX+DW,y,'l-sig',width=1.5,arrow=True); S.text((DX+DW+LX)/2,y-4,'mV','lbl-r','middle')
    # gateways column aligned row-to-row
    GX,GW=630,160
    grows={'u30':90,'u31':150,'u34':270,'u32':330,'psu5':510,'pi':570,'sw':630,'fx':690,'teth':750}
    GC=lambda k:grows[k]+DH/2
    S.box(GX,grows['u30'],GW,DH,'USR-TCP232-304  .30','8N2 · SE3 左獨佔')
    S.box(GX,grows['u31'],GW,DH,'USR-TCP232-304  .31','8N2 · SE3 右獨佔')
    S.box(GX,grows['u34'],GW,DH,'USR-TCP232-304  .34','8N1 · SD76 感測')
    S.box(GX,grows['u32'],GW,DH,'USR-TCP232-304  .32','8N1 · 水閥獨佔')
    S.box(GX,grows['psu5'],GW,DH,'5V PSU','吊機專用,與本體分開',cls='node psu-v5')
    S.box(GX,grows['pi'],GW,DH,'吊機 Pi','獨立 5V 變壓器')
    S.box(GX,grows['sw'],GW,DH,'8-port PoE Switch','吊機端')
    S.box(GX,grows['fx'],GW,DH,'Fathom-X 載波(頂樓端)','吃 24V',cls='node crit')
    S.box(GX,grows['teth'],GW,DH,'2-wire tether','HomePlug AV → 機身')
    # RS485 straight lines from load-box right edge to gateway (y = center-6)
    for dk,gk in [('se3l','u30'),('se3r','u31'),('sd76','u34'),('zs','u32')]:
        y=C(dk)-6
        S.line(LX+LW,y,GX,y,'l-bus',width=2)
    # but devices with a load box: RS485 actually originates at device; draw from device right edge passing above load box? keep: route from device top-right via y=center-6 across load box is ugly; instead draw from device box right edge at y=center-12 over the top of load box
    # (override) redraw properly:
    S.parts=[p for p in S.parts if not (p.startswith('<line') and 'class="l-bus"' in p)]
    for dk,gk in [('se3l','u30'),('se3r','u31'),('sd76','u34'),('zs','u32')]:
        y=rows[dk]-9
        S.poly([(DX+DW-20,rows[dk]),(DX+DW-20,y),(600,y),(600,GC(gk)-6),(GX,GC(gk)-6)],'l-bus',width=2)
        S.text(DX+DW-14,y-4,'RS485 .'+gk[1:],'lbl-b')
    y=rows['mh']-9
    S.poly([(DX+DW-20,rows['mh']),(DX+DW-20,y),(560,y)],'l-bus',dashed=True,width=2); S.q(570,y)
    S.text(DX+DW-14,y-4,'RS485 掛哪段? (碼寫 CLV900 slave 3 @.30)','lbl-b')
    # X518 ethernet → switch (vertical ethernet trunk at x=810)
    EX=810
    y=rows['x518']-9
    S.poly([(DX+DW-20,rows['x518']),(DX+DW-20,y),(EX,y)],'l-eth',dashed=True,width=1.5); S.dot(EX,y,'d-eth')
    S.text(DX+DW-14,y-4,'Ethernet .33:502(只允許 1 條 TCP)','lbl-b')
    S.line(EX,rows['se3l']-9,EX,GC('sw'),'l-eth',dashed=True,width=1.5)
    S.line(GX+GW,GC('sw'),EX,GC('sw'),'l-eth',dashed=True,width=1.5); S.dot(EX,GC('sw'),'d-eth')
    for k in ['u30','u31','u34','u32','pi']:
        y=GC(k); S.line(GX+GW,y,EX,y,'l-eth',dashed=True,width=1.5); S.dot(EX,y,'d-eth')
    S.text(EX+6,rows['se3l']-14,'Ethernet','lbl-b')
    S.line(GX+80,grows['teth'],GX+80,grows['fx']+DH,'l-eth',arrow=True,width=1.5)
    S.line(GX+80,grows['fx'],GX+80,grows['sw']+DH,'l-eth',arrow=True,width=1.5)
    # 24V → Fathom-X (rooftop end)
    yf=GC('fx'); S.poly([(200,PY_+DH),(200,yf),(GX,yf)],'l-v24',width=2)
    S.text(210,yf-5,'24V → 載波模組(per user)','lbl-r')
    # AC → 5V PSU (horizontal, below all device rows)
    y=GC('psu5'); S.line(AX,y,GX,y,'l-ac',width=2); S.dot(AX,y,'d-ac')
    # 5V vertical x=612 from PSU up to .30, stubs at center+6
    S.line(618,y,618,GC('u30')+6,'l-v5',width=2); S.line(GX,y,618,y,'l-v5',width=2); S.dot(618,y,'d-v5')
    for k in ['u30','u31','u34','u32']:
        yy=GC(k)+6; S.line(618,yy,GX,yy,'l-v5',width=2); S.dot(618,yy,'d-v5')
    S.text(612,GC('u30')-2,'5V','lbl-r r-5','end')
    return S.render()

open('body.svg','w',encoding='utf-8').write(body())
open('crane.svg','w',encoding='utf-8').write(crane())
print("ok", len(body()), len(crane()))
