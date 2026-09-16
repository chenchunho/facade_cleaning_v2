#!/usr/bin/env bash
# facade_cleaning_v2 — 開發期部署腳本(2026-09-16,程式改 systemd 之後)
#
#   ./scripts/deploy.sh body    # 本體 facade_cleaning_v2.out  → fcv-body (user service @ .26)
#   ./scripts/deploy.sh arm     # 手臂 motor_api               → fcv-arm  (user service @ .26)
#   ./scripts/deploy.sh crane   # 吊機 crane_control_PI.out    → fcv-crane(system service @ .25)
#   ./scripts/deploy.sh web     # GUI(index.html)—— 不重啟 node,交給 scripts/deploy_web.sh
#   ./scripts/deploy.sh server  # server.js → 重啟 fcv-web-v3(v2 的 fcv-web 09-16 退役)
#   ./scripts/deploy.sh script  # cycle_test.py → 兩台 Pi(無服務,不必重啟)
#   ./scripts/deploy.sh status  # 四支服務狀態 + 機器現況
#
# 每個目標一律做完整五步:同步原始碼 → Pi 上編譯 → **備份現役 binary** → 換檔 → 重啟服務 → 驗證啟動訊息。
#
# 🔴 為什麼要備份:`compile.sh` / `build_*.sh` 的產物與現役檔同名或會被直接覆蓋,
#    2026-09-14 就發生過「重編蓋掉現役 motor_api 而沒有 .prev」。這裡自動命名
#    `<name>.prev-MMDD-HHMM`,回退只要 cp 回去 + restart。
# 🔴 為什麼先 rm 再 cp:覆蓋執行中的執行檔會 `Text file busy`(舊流程踩過)。
#
# ⚠️ 重啟前的兩件事(腳本會擋/會問):
#   1. **本體**:腳吸著時重啟 → SIGTERM 不關繼電器,真空會留著;新程序的**開機盤點**(2026-09-16 C 案)
#      會把 state 設成 **Error** 並且**不自動鬆開**(要收拾請按 return_home,或人工 pusher all retract 後 reset)。
#      所以仍建議先收腳再重啟。腳本偵測到吸附會要你確認。
#   2. **手臂**:重啟會跑 STARTUP ⇒ **M1 一定會收回機械零點**(M1 沒回到零就不動 M2,見 main_api.cpp)。
#      壓牆中重啟等於當場卸力收回,確定沒有壓著再按。
set -uo pipefail

BODY=nexuni@192.168.5.26
CRANE=user@192.168.5.25
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAMP="$(date +%m%d-%H%M)"
SSH="ssh -o ConnectTimeout=8"

die() { echo "🔴 $*" >&2; exit 1; }
say() { echo "── $*"; }

# 吸附中就要人確認(本體/手臂重啟都會動到現場)
confirm_if_attached() {
    local st
    st=$($SSH "$BODY" 'python3 ~/run/crcmd.py 127.0.0.1:5001 status 2>/dev/null | tr " " "\n" | /bin/grep -E "^p[5-8]="' </dev/null 2>/dev/null | tr '\n' ' ')
    [ -z "$st" ] && { echo "⚠️  讀不到本體壓力(程式沒跑?)——繼續。"; return 0; }
    local sealed
    # 🔴 `$2 <= -40` 不能直接比:尾端空白會產生 $2="" 的空欄,awk 拿空字串跟 "-40" 做**字串**比較
    #    ⇒ "" <= "-40" 為真,四顆都沒吸也會算出 1 顆(2026-09-16 第一次跑就踩到)。`NF==2` + `+0` 強制數值。
    sealed=$(echo "$st" | tr ' ' '\n' | awk -F= 'NF==2 && $2+0 <= -40 {n++} END{print n+0}')
    echo "   目前四顆壓力:$st(吸著 $sealed 顆)"
    if [ "$sealed" -gt 0 ]; then
        echo "🔴 機器目前**吸附中**。重啟前建議先送 pusher all retract。"
        read -r -p "   仍要繼續?(yes/N) " a
        [ "$a" = "yes" ] || die "已取消"
    fi
}

deploy_body() {
    confirm_if_attached
    say "同步原始碼"
    scp -q "$REPO"/app/WASH_ROBOT.{h,cpp} "$REPO"/app/wash_robot_commands.cpp "$BODY":~/projects/facade_cleaning_v2/app/ || die "scp app 失敗"
    scp -q "$REPO"/command/dispatcher.cpp "$BODY":~/projects/facade_cleaning_v2/command/ || die "scp command 失敗"
    say "Pi 上編譯"
    # [2026-09-16] `~/run/build_body.sh` 是 Pi 上的副本,repo 改了 TU 清單(例如拿掉 DY_500)它不會跟著變 ⇒ 每次先同步。
    scp -q "$REPO"/scripts/build/build_body.sh "$BODY":~/run/build_body.sh || die "scp build_body.sh 失敗"
    $SSH "$BODY" 'bash ~/run/build_body.sh 2>&1 | tail -2' </dev/null | /bin/grep -q facade_drv.out || die "編譯失敗(見上)"
    say "備份 + 換檔 + 重啟 fcv-body"
    $SSH "$BODY" "cd ~/run && cp -p facade_cleaning_v2.out facade_cleaning_v2.out.prev-$STAMP && rm -f facade_cleaning_v2.out && cp -p facade_drv.out facade_cleaning_v2.out && md5sum facade_cleaning_v2.out && systemctl --user restart fcv-body" </dev/null || die "換檔/重啟失敗"
    say "驗證"
    $SSH "$BODY" 'for i in $(seq 1 25); do ss -ltn | grep -q ":5001" && break; sleep 2; done; systemctl --user is-active fcv-body; tail -40 ~/run/logs/body_service.log | /bin/grep -aE "^\[(OK|WARN)\] (crane 1|selfcheck|command server)|開機盤點"' </dev/null
}

deploy_arm() {
    confirm_if_attached
    say "同步原始碼"
    # 🔴 `compile.sh` 編的是 `*.cpp` 全部 —— 只同步 main_api.{h,cpp} 會讓 Pi 上的 main.cpp
    #    停在舊版,症狀是「本機改好了、Pi 上編不過」(2026-09-16 踩到)。整個目錄的來源檔一起送。
    scp -q "$REPO"/cleaning_arm/*.cpp "$REPO"/cleaning_arm/*.h "$BODY":~/projects/facade_cleaning_v2/cleaning_arm/ || die "scp 失敗"
    say "Pi 上編譯(compile.sh 會就地產生 motor_api)"
    # 🔴 compile.sh 失敗時 g++ 會留下舊的 motor_api,而 md5sum 照樣成功 ⇒ 只看 md5 會**誤判成功**
    #    (2026-09-16 踩到:cmd_init_sequence 是 private,編譯錯了但腳本印 ✅ 完成)。
    #    改成把編譯輸出接回來檢查有沒有 `error:`。
    local out
    out=$($SSH "$BODY" "cd ~/projects/facade_cleaning_v2/cleaning_arm && cp -p motor_api motor_api.prev-$STAMP && bash compile.sh 2>&1 | tail -20 && echo ---MD5--- && md5sum motor_api" </dev/null)
    echo "$out"
    echo "$out" | /bin/grep -q "error:" && die "編譯失敗(見上) —— 現役 motor_api 未被取代"
    say "重啟 fcv-arm(會跑 STARTUP:M1 回機械零點 → M2 滾筒槽)"
    $SSH "$BODY" 'systemctl --user restart fcv-arm' </dev/null || die "重啟失敗"
    $SSH "$BODY" 'for i in $(seq 1 20); do ss -ltn | grep -q ":9527" && break; sleep 2; done; systemctl --user is-active fcv-arm; tail -20 ~/run/logs/arm_service.log | /bin/grep -aE "STARTUP\]|ERR"' </dev/null
}

deploy_crane() {
    say "同步原始碼"
    scp -q "$REPO"/Crane_control_PI/main.cpp "$CRANE":~/projects/facade_cleaning_v2/Crane_control_PI/ || die "scp 失敗"
    say "Pi 上編譯"
    scp -q "$REPO"/scripts/build/build_crane.sh "$CRANE":~/run/build_crane.sh || die "scp build_crane.sh 失敗"   # 同上,副本要跟著 repo
    $SSH "$CRANE" 'bash ~/run/build_crane.sh 2>&1 | tail -2' </dev/null | /bin/grep -q crane_drv.out || die "編譯失敗(見上)"
    say "備份 + 換檔 + 重啟 fcv-crane"
    $SSH "$CRANE" "cd ~/run && cp -p crane_control_PI.out crane_control_PI.out.prev-$STAMP && rm -f crane_control_PI.out && cp -p crane_drv.out crane_control_PI.out && md5sum crane_control_PI.out && sudo systemctl restart fcv-crane" </dev/null || die "換檔/重啟失敗"
    say "驗證(ExecStartPost 會補送 home_ground/motion_hz/wall_height,約 20 s)"
    $SSH "$CRANE" 'for i in $(seq 1 30); do ss -ltn | grep -q ":5002" && break; sleep 2; done; sleep 22; systemctl is-active fcv-crane; python3 ~/run/crcmd.py status | tr " " "\n" | /bin/grep -E "^(home_ground_cm|wall_height_cm|motion_hz|length_left)="' </dev/null
}

deploy_server() {
    say "同步 server.js 並重啟兩支 node"
    scp -q "$REPO"/web_backend/server.js "$CRANE":/tmp/server.js.new || die "scp 失敗"
    $SSH "$CRANE" "D=~/projects/facade_cleaning_v2/web; cp -p \$D/server.js \$D/server.js.bak-$STAMP && cp /tmp/server.js.new \$D/server.js && md5sum \$D/server.js && sudo systemctl restart fcv-web-v3" </dev/null || die "換檔/重啟失敗"
    $SSH "$CRANE" 'sleep 6; systemctl is-active fcv-web-v3; tail -8 ~/run/logs/web_v3_service.log | /bin/grep -a connected' </dev/null
}

deploy_script() {
    say "同步 cycle_test.py 到兩台(無服務,下次起跑就是新版)"
    python3 -m py_compile "$REPO"/Linux_test/cycle_test.py || die "語法錯誤,沒有送出"
    for h in "$CRANE" "$BODY"; do
        scp -q "$REPO"/Linux_test/cycle_test.py "$h":~/projects/facade_cleaning_v2/Linux_test/ || die "scp $h 失敗"
    done
    md5sum "$REPO"/Linux_test/cycle_test.py
    $SSH "$CRANE" 'md5sum ~/projects/facade_cleaning_v2/Linux_test/cycle_test.py' </dev/null
    $SSH "$BODY"  'md5sum ~/projects/facade_cleaning_v2/Linux_test/cycle_test.py' </dev/null
}

show_status() {
    echo "=== 吊機 .25 ==="
    $SSH "$CRANE" 'systemctl is-active fcv-crane fcv-web-v3 | tr "\n" " "; echo; python3 ~/run/crcmd.py status 2>/dev/null | tr " " "\n" | /bin/grep -E "^(length_left|length_right|wall_height_cm|home_ground_cm|hold_guard|level_auto)=" | tr "\n" " "; echo' </dev/null
    echo "=== 本體 .26 ==="
    $SSH "$BODY" 'systemctl --user is-active fcv-arm fcv-body | tr "\n" " "; echo; python3 ~/run/crcmd.py 127.0.0.1:5001 status 2>/dev/null | tr " " "\n" | /bin/grep -E "^(state|p[5-8]|arm_ready|estop|pusher_rpm)" | tr "\n" " "; echo' </dev/null
}

case "${1:-}" in
    body)   deploy_body ;;
    arm)    deploy_arm ;;
    crane)  deploy_crane ;;
    web)    exec "$REPO/scripts/deploy_web.sh" ;;   # 版號戳記 + 三方 md5,不必重啟 node
    server) deploy_server ;;
    script) deploy_script ;;
    status) show_status ;;
    *) sed -n '2,30p' "${BASH_SOURCE[0]}"; exit 2 ;;
esac
echo "✅ 完成:$1"
