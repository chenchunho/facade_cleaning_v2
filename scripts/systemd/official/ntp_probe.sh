#!/bin/bash
# [2026-09-21] official 兩台 Pi 沒有 NTP(裝置專網,時鐘各漂十幾小時,log 對不上)。
# 這支只「探測」哪條路可行,不改設定 —— 三個問題答完才知道要裝什麼:
#   ① 主路由 192.168.1.1 有沒有回 NTP(有 → 兩台 timesyncd 直接指它,零安裝)
#   ② 這台出不出得去公網(有 → timesyncd 用預設 pool 即可)
#   ③ 兩者都沒有 → 吊機裝 chrony 當 `local stratum 10` 源、本體指吊機(至少兩台一致);chrony 要先離線帶 .deb
# 用法:在吊機或本體上 `bash ntp_probe.sh`
set -u
echo "== $(hostname) $(date '+%F %T %Z') =="
echo "-- timesyncd:"; timedatectl show -p NTP -p NTPSynchronized -p TimeUSec 2>/dev/null | tr '\n' ' '; echo
systemctl is-active systemd-timesyncd chrony 2>/dev/null | tr '\n' ' '; echo
echo "-- ① 主路由 NTP(UDP 123):"
if command -v chronyd >/dev/null; then chronyd -Q 'server 192.168.1.1 iburst' 2>&1 | tail -2
else python3 - <<'PY'
import socket,struct,time
try:
    s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.settimeout(3)
    s.sendto(b'\x1b'+47*b'\0',('192.168.1.1',123)); d,_=s.recvfrom(48)
    t=struct.unpack('!12I',d)[10]-2208988800; print('router NTP OK, its time =',time.strftime('%F %T',time.gmtime(t)),'UTC')
except Exception as e: print('router NTP: no reply (',e,')')
PY
fi
echo "-- ② 公網:"; (timeout 4 bash -c 'echo > /dev/tcp/1.1.1.1/53' 2>/dev/null && echo "internet reachable (1.1.1.1:53)") || echo "no internet"
getent hosts pool.ntp.org >/dev/null 2>&1 && echo "DNS OK" || echo "DNS fails"
echo "-- 另一台 Pi 的時間差(需 ssh key):"
for h in 192.168.1.10 192.168.1.100; do [ "$h" = "$(hostname -I | awk '{print $1}')" ] && continue
  o=$(timeout 6 ssh -o BatchMode=yes -o ConnectTimeout=3 nexuni@$h 'date +%s' 2>/dev/null) && echo "  $h: $(( $(date +%s) - o )) s 差" || echo "  $h: ssh 不通"
done
