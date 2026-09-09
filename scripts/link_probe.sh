#!/bin/bash
# scripts/link_probe.sh — 隧道 vs WiFi 雙路徑同時丟包量測（Fathom-X 干擾實驗用）
#
# 用途：重現 2026-09-08 的受控實驗，作為「吊機端 Fathom-X 改獨立電源」前後的 A/B 對照。
#   09-08 基準：HOLD 期間 隧道 9/90（90% 丟包）／WiFi 90/90（0%），其餘時間兩者皆 0%。
#
# 🔴 必須在**本體 washrobot**（192.168.5.26）上跑 —— 只有它同時看得到兩條路徑：
#   隧道側 192.168.1.10（吊機有線，經 Fathom-X）／ WiFi 側 192.168.5.25（吊機無線）。
#   從 WSL 只到得了 192.168.5.25，量不到隧道。
#
# 用法：
#   ./link_probe.sh run  [秒數]        # 預設 420，5Hz。跑完印出檔名
#   ./link_probe.sh analyze <前綴> <hold起> <hold迄>
#       hold 起迄 = 吊機 log 的 HOLD-TRACE 時間戳（"HH:MM:SS" 或 epoch 秒，可帶小數）
#   ./link_probe.sh offset             # 量吊機與本體的時鐘偏移（🔴 在操作端 WSL 跑）
#   ./link_probe.sh preflight          # 閒置丟包檢查（run 會自動先跑，FORCE=1 可略過）
#
# 典型流程：
#   1) ./link_probe.sh offset                       # 🔴 在操作端跑（09-08 實測 0.26 秒）
#   2) ./link_probe.sh run 420                      # 開始後請 per user 中途按住 ▲ 拉繩一次
#   3) 從吊機 log 取 HOLD-TRACE 起迄
#   4) ./link_probe.sh analyze <前綴> 19:59:31.972 19:59:49.426
#
# ⚠️ 一次只驗一個變因：改電源就只改電源，位置與線材不要同時動，否則分不出是哪個。

set -u

TUNNEL_IP="${TUNNEL_IP:-192.168.1.10}"
WIFI_IP="${WIFI_IP:-192.168.5.25}"
BODY_TUNNEL_IP="${BODY_TUNNEL_IP:-192.168.1.100}"   # 本體 eth0（隧道側）
BODY_WIFI_IP="${BODY_WIFI_IP:-192.168.5.26}"        # 本體 wlan0（WiFi 側）
INTERVAL=0.2                      # 5Hz —— 非 root 允許的最小間隔就是 0.2
OUTDIR="${OUTDIR:-$HOME/bringup/linkprobe}"

usage() { sed -n '2,28p' "$0"; exit 1; }

cmd_offset() {
    # 🔴 這一步要在**操作端**（WSL）跑，不是在本體上跑 —— 本體沒有吊機的 host key。
    # 分別問兩台的時鐘再相減，兩次 SSH 往返的誤差對 17 秒等級的窗口足夠小。
    local tb tc
    tb=$(ssh -o BatchMode=yes -o ConnectTimeout=5 "nexuni@${BODY_WIFI_IP}" 'date +%s.%N') || {
        echo "🔴 SSH 到本體 ${BODY_WIFI_IP} 失敗" >&2; return 1; }
    tc=$(ssh -o BatchMode=yes -o ConnectTimeout=5 "user@${WIFI_IP}" 'date +%s.%N') || {
        echo "🔴 SSH 到吊機 ${WIFI_IP} 失敗" >&2; return 1; }
    awk -v b="$tb" -v c="$tc" 'BEGIN{
        d = c - b
        printf "吊機時鐘 − 本體時鐘 = %+.3f 秒\n", d
        if (d < 0) d = -d
        if (d > 2) print "🔴 偏移大於 2 秒 —— HOLD 窗口只有 ~17 秒，先校時再量"
        else       print "🟢 偏移遠小於 HOLD 窗口長度，可用"
    }'
}

# 🔴🔴 冷路徑假象防呆（2026-09-09 實地踩到）
# 現象：吊機閒置一陣子後，**本體→吊機** 80~100% 丟包，而**同一時刻吊機→本體 0%**。單向。
# 暖機之後 1Hz 與 5Hz 各測都回到 0% 丟包 / 8.5ms ⇒ 是冷路徑，不是故障。
# ⚠️ 它長得跟 VFD 干擾一模一樣，但跟 VFD 毫無關係 —— 沒先暖機就開量，會量到一個假的基準線。
warm_and_check() {
    local ip="$1" label="$2" loss
    # 暖機 burst：結果**丟棄**，只為了把入向路徑叫醒。
    # 🔴 刻意不靠「叫對面反向 ping」——那需要本體能 SSH 到吊機，而它沒有吊機的 host key
    # （2026-09-09 實測 `Host key verification failed`）。原本那個寫法被 `|| true` 吞掉，
    # **暖機從來沒發生過，而輸出跟暖成功一模一樣**。⇒ 防呆本身不可以有靜默失敗的分支。
    ping -c 15 -i "$INTERVAL" -W1 "$ip" >/dev/null 2>&1 || true
    loss=$(ping -c 25 -i "$INTERVAL" -W2 "$ip" 2>/dev/null \
           | awk -F'[,%]' '/packet loss/{gsub(/ /,"",$3); print $3+0}')
    if [ -z "${loss:-}" ]; then
        printf "  %-6s %-15s 🔴 ping 沒有回報統計（主機不在線？）\n" "$label" "$ip"; return 1
    fi
    printf "  %-6s %-15s 暖機後閒置丟包 %s%%" "$label" "$ip" "$loss"
    awk -v l="$loss" 'BEGIN{exit !(l>2)}' && { echo "   🔴 暖機後仍在丟 ⇒ 真故障，不是冷路徑"; return 1; }
    echo "   🟢"; return 0
}

cmd_preflight() {
    echo "preflight：機器閒置時兩條路徑暖機後都應該接近 0% 丟包"
    local bad=0
    warm_and_check "$TUNNEL_IP" "隧道" || bad=1
    warm_and_check "$WIFI_IP"   "WiFi" || bad=1
    if [ "$bad" -ne 0 ]; then
        echo
        echo "🔴 暖機 burst 跑過了還在丟 ⇒ 這不是冷路徑，是真的有問題，**先不要開始量**。"
        echo "   確定要照跑：FORCE=1 $0 run"
        return 1
    fi
    echo "🟢 preflight 通過，可以開始量"
}

cmd_run() {
    if [ "${FORCE:-0}" != "1" ]; then
        cmd_preflight || return 1
        echo
    fi
    local secs="${1:-420}"
    local count; count=$(awk -v s="$secs" -v i="$INTERVAL" 'BEGIN{printf "%d", s/i}')
    local stamp; stamp=$(date +%Y%m%d-%H%M%S)
    local pfx="${OUTDIR}/probe-${stamp}"
    mkdir -p "$OUTDIR"

    echo "量測 ${secs}s @5Hz（每路徑 ${count} 筆）"
    echo "  隧道 ${TUNNEL_IP}  →  ${pfx}.tunnel"
    echo "  WiFi ${WIFI_IP}  →  ${pfx}.wifi"
    echo "🎯 現在請 per user 在中途按住一次 ▲ 拉繩，並記下吊機 log 的 HOLD-TRACE 起迄"

    # -D 印 unix 時間戳（分析要靠它切窗口）；兩條同時起跑才具可比性
    ping -D -i "$INTERVAL" -c "$count" "$TUNNEL_IP" > "${pfx}.tunnel" 2>&1 &
    local p1=$!
    ping -D -i "$INTERVAL" -c "$count" "$WIFI_IP"   > "${pfx}.wifi"   2>&1 &
    local p2=$!
    wait $p1 $p2

    echo "完成。分析：$0 analyze ${pfx} <hold起> <hold迄>"
}

# "HH:MM:SS[.mmm]" 或 epoch → epoch 秒（小數）
to_epoch() {
    case "$1" in
        *:*) local d; d=$(date +%Y-%m-%d)
             local base frac
             base="${1%%.*}"; frac="${1#"$base"}"; [ "$frac" = "$1" ] && frac=""
             awk -v e="$(date -d "$d $base" +%s)" -v f="0$frac" 'BEGIN{printf "%.3f", e+f}' ;;
        *)   printf "%.3f" "$1" ;;
    esac
}

analyze_one() {
    local file="$1" label="$2" hs="$3" he="$4"
    [ -r "$file" ] || { echo "🔴 讀不到 $file"; return 1; }
    # ping -D 的行首是 [1757000000.123456]，只取有 icmp_seq 的回應行
    awk -v hs="$hs" -v he="$he" -v label="$label" -v iv="$INTERVAL" '
        /icmp_seq=/ {
            t = $1; gsub(/[\[\]]/, "", t); t = t + 0
            if (t >= hs && t <= he) {
                # 🎯 窗口「內」的最長空窗 —— 這才是驗收判準。
                # 丟包率說不出「連續斷了多久」，而 IMU_ROLL_STALE_MS 比的正是空窗：
                # roll 推送 4Hz（250ms），門檻 750ms ＝ 連掉 3 個就過期。
                # 90% 丟包若平均散開，最長空窗可能只有 2 秒；若集中，就是 17 秒全斷。
                # 兩者的丟包率一樣，對控制鏈路的意義完全不同。
                inw++
                if (first_in == 0) first_in = t
                if (prev_in > 0) { g = t - prev_in; if (g > maxgap_in) maxgap_in = g }
                prev_in = t
                next
            }
            outw++
            # 🔴 只在「同一側」的相鄰樣本之間算空窗 —— 跨越 hold 窗口的那一段不算。
            # 09-08 就是沒排除它，量出一個不存在的「其餘時間最長空窗 20 秒」，
            # 兩條路徑都有、純屬統計假象，不要當結論。
            side = (t < hs) ? 0 : 1
            if (prev_t > 0 && side == prev_side) {
                g = t - prev_t
                if (g > maxgap) maxgap = g
            }
            prev_t = t; prev_side = side
        }
        END {
            exp_in = (he - hs) / iv
            printf "%-6s HOLD 期間 %3d / %-5.0f 預期", label, inw, exp_in
            # 預期筆數是由窗口長度推算的估計值，收到的可能比它多一兩筆（端點含入）。
            # 負的丟包率沒有意義，夾到 0，不要讓一個估計誤差看起來像一個發現。
            if (exp_in > 0) {
                loss = (1 - inw/exp_in) * 100; if (loss < 0) loss = 0
                printf "  → 丟包 %5.1f%%", loss
            }
            printf "\n"
            # 窗口邊界到第一筆／最後一筆之間也算空窗，否則「整段全斷」會因為窗內
            # 只剩 1 筆而算出 maxgap_in = 0，看起來反而最漂亮。
            # 🔴 頭是 first_in、尾是 prev_in —— 兩邊用錯同一個變數，會讓這個指標
            # 永遠報出整個窗口長度，也就是永遠不通過：一次成功的電氣修復會被
            # 誤判成失敗。（2026-09-09 合成資料驗證時抓到，實測前就修掉。）
            if (inw == 0) {
                gap_in = he - hs
                printf "       🔴 HOLD 期間最長空窗 >= %.3f 秒（窗內一筆都沒收到）\n", gap_in
            } else {
                if (first_in - hs > maxgap_in) maxgap_in = first_in - hs
                if (he - prev_in > maxgap_in) maxgap_in = he - prev_in
                printf "       🎯 HOLD 期間最長空窗 %.3f 秒", maxgap_in
                if      (maxgap_in < 0.750) printf "   🟢 < IMU_ROLL_STALE_MS(750ms)，門檻不用動\n"
                else if (maxgap_in < 1.500) printf "   🟡 破 750ms 但 < 1.5s，可用但要調門檻\n"
                else                        printf "   🔴 >= 1.5s，隧道還不能當控制鏈路\n"
            }
            printf "       其餘時間 %d 筆，最長空窗 %.3f 秒（已排除跨窗口的假空窗）\n", outw, maxgap
        }
    ' "$file"
}

cmd_analyze() {
    [ $# -eq 3 ] || usage
    local pfx="$1" hs he
    hs=$(to_epoch "$2"); he=$(to_epoch "$3")
    echo "HOLD 窗口 ${2} ~ ${3}（$(awk -v a="$hs" -v b="$he" 'BEGIN{printf "%.2f", b-a}') 秒）"
    echo "⚠️ 窗口取自吊機時鐘 —— 先跑過 offset 確認偏移遠小於窗口長度"
    echo
    analyze_one "${pfx}.tunnel" "隧道" "$hs" "$he"
    analyze_one "${pfx}.wifi"   "WiFi" "$hs" "$he"
    echo
    echo "📌 判讀：隧道丟包大幅下降而 WiFi 不變 ⇒ 電源耦合，獨立供電有效"
    echo "        隧道丟包沒改善                  ⇒ 輻射／線間耦合，要往磁環・遮蔽線・走線分離走"
}

case "${1:-}" in
    run)     shift; cmd_run "$@" ;;
    analyze) shift; cmd_analyze "$@" ;;
    offset)    cmd_offset ;;
    preflight) cmd_preflight ;;
    *)       usage ;;
esac
