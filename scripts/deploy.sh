#!/usr/bin/env bash
# facade_cleaning_v2 — 開發期部署腳本(2026-09-16,程式改 systemd 之後)
#
#   ./scripts/deploy.sh body    # 本體 facade_cleaning_v2.out  → fcv-body (user service @ .26)
#   ./scripts/deploy.sh arm     # 手臂 motor_api               → fcv-arm  (user service @ .26)
#   ./scripts/deploy.sh crane   # 吊機 crane_control_PI.out    → fcv-crane(system service @ .25)
#   ./scripts/deploy.sh web     # GUI(index.html)—— 不重啟 node,交給 scripts/deploy_web.sh
#   ./scripts/deploy.sh server  # server.js → 重啟 fcv-web-v3(v2 的 fcv-web 09-16 退役)
#   ./scripts/deploy.sh script  # scripts/cycle_test.py → 兩台 Pi(無服務,不必重啟;09-17 由 Linux_test/ 搬來)
#   ./scripts/deploy.sh cam     # scripts/cam_relay.py + fcv-cam.service → 本體(Dashboard 攝影機 :8091,2026-10-07)
#   ./scripts/deploy.sh status  # 服務狀態 + 機器現況
#
#   FCV_TARGET=official ./scripts/deploy.sh <同上>   # [2026-09-21] 正式機:吊機 nexuni@192.168.1.10、本體 nexuni@192.168.1.100
#   ./scripts/deploy.sh prep-official                # 一次性:檢查 official 的 ssh key + sudo -n(見下)
#
# 🔴 official 與測試機的差別只有三件,全部由 FCV_TARGET 切:
#   1. 位址(上面兩個);2. 吊機 sudo:測試機 user 有 NOPASSWD,official 的 nexuni 沒有 → 本腳本一律 `sudo -n`,
#      第一次用之前請在 official 上放一行 sudoers(只放行 restart 這兩支服務,不是整個 NOPASSWD):
#        echo 'nexuni ALL=(root) NOPASSWD: /usr/bin/systemctl restart fcv-crane, /usr/bin/systemctl restart fcv-web-v3' | sudo tee /etc/sudoers.d/fcv-deploy
#   3. ssh key:official 兩台都還沒裝(09-19 是用密碼+paramiko 做的)→ `ssh-copy-id nexuni@192.168.1.10` / `…@192.168.1.100`。
#   裝置位址/站號/校正值**不在這裡**,在 Pi 上的 systemd drop-in(副本 scripts/systemd/official/),部署程式不會動到它們。
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

case "${FCV_TARGET:-test}" in
    # [2026-10-07] test crane is WiFi DHCP and drifts (.25 -> .31 on 10-07) => FCV_CRANE overrides it,
    #   e.g. `FCV_CRANE=user@192.168.5.31 ./scripts/deploy.sh script`. The body side follows the same IP via
    #   its drop-in `fcv-body.service.d/crane-ip.conf` (see work_log 10-07), not via this script.
    test)     BODY=nexuni@192.168.5.26;  CRANE="${FCV_CRANE:-user@192.168.5.25}"; TARGET_LABEL="測試機" ;;
    official) BODY=nexuni@192.168.1.100; CRANE=nexuni@192.168.1.10;  TARGET_LABEL="正式機 official" ;;
    *) echo "🔴 FCV_TARGET 只認 test / official(得到 '$FCV_TARGET')" >&2; exit 2 ;;
esac
SUDO="sudo -n"   # 兩台都走 -n:沒 NOPASSWD 就明確失敗,不會卡在密碼提示(見檔頭第 2 點)
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
    # [2026-10-01] 驅動層 / 傳輸層 / common 也要送:09-30 只改了 user_lib(ZDT/QX/DM2J)與 transport 的 guard,
    #   本函式原本只送 app/ + command/ ⇒ Pi 上拿舊驅動編 → 編譯失敗(那次幸好失敗;若簽名沒變就會**靜默**編出半新半舊)。
    scp -q "$REPO"/user_lib/*.cpp "$REPO"/user_lib/*.h "$BODY":~/projects/facade_cleaning_v2/user_lib/ || die "scp user_lib 失敗"
    scp -q "$REPO"/transport/*.cpp "$REPO"/transport/*.h "$BODY":~/projects/facade_cleaning_v2/transport/ || die "scp transport 失敗"
    scp -q "$REPO"/common/*.h "$BODY":~/projects/facade_cleaning_v2/common/ || die "scp common 失敗"
    scp -q "$REPO"/facade_cleaning_v2/main.cpp "$BODY":~/projects/facade_cleaning_v2/facade_cleaning_v2/ || die "scp main 失敗"
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
    # [2026-10-01] 同 deploy_body:驅動/傳輸/common 一起送,否則只改驅動時 Pi 用舊檔編。
    scp -q "$REPO"/user_lib/*.cpp "$REPO"/user_lib/*.h "$CRANE":~/projects/facade_cleaning_v2/user_lib/ || die "scp user_lib 失敗"
    scp -q "$REPO"/transport/*.cpp "$REPO"/transport/*.h "$CRANE":~/projects/facade_cleaning_v2/transport/ || die "scp transport 失敗"
    scp -q "$REPO"/common/*.h "$CRANE":~/projects/facade_cleaning_v2/common/ || die "scp common 失敗"
    scp -q "$REPO"/mechanism/*.h "$CRANE":~/projects/facade_cleaning_v2/mechanism/ || die "scp mechanism 失敗"
    say "Pi 上編譯"
    scp -q "$REPO"/scripts/build/build_crane.sh "$CRANE":~/run/build_crane.sh || die "scp build_crane.sh 失敗"   # 同上,副本要跟著 repo
    $SSH "$CRANE" 'bash ~/run/build_crane.sh 2>&1 | tail -2' </dev/null | /bin/grep -q crane_drv.out || die "編譯失敗(見上)"
    say "備份 + 換檔 + 重啟 fcv-crane"
    $SSH "$CRANE" "cd ~/run && cp -p crane_control_PI.out crane_control_PI.out.prev-$STAMP && rm -f crane_control_PI.out && cp -p crane_drv.out crane_control_PI.out && md5sum crane_control_PI.out && $SUDO systemctl restart fcv-crane" </dev/null || die "換檔/重啟失敗(official 請先做 prep-official)"
    say "驗證(ExecStartPost 會補送 home_ground/motion_hz/wall_height,約 20 s)"
    $SSH "$CRANE" 'for i in $(seq 1 30); do ss -ltn | grep -q ":5002" && break; sleep 2; done; sleep 22; systemctl is-active fcv-crane; python3 ~/run/crcmd.py status | tr " " "\n" | /bin/grep -E "^(home_ground_cm|wall_height_cm|motion_hz|length_left)="' </dev/null
}

deploy_server() {
    say "同步 server.js 並重啟兩支 node"
    scp -q "$REPO"/web_backend/server.js "$CRANE":/tmp/server.js.new || die "scp 失敗"
    $SSH "$CRANE" "D=~/projects/facade_cleaning_v2/web; cp -p \$D/server.js \$D/server.js.bak-$STAMP && cp /tmp/server.js.new \$D/server.js && md5sum \$D/server.js && $SUDO systemctl restart fcv-web-v3" </dev/null || die "換檔/重啟失敗(official 請先做 prep-official)"
    $SSH "$CRANE" 'sleep 6; systemctl is-active fcv-web-v3; tail -8 ~/run/logs/web_v3_service.log | /bin/grep -a connected' </dev/null
}

deploy_script() {
    say "同步 cycle_test.py 到兩台(無服務,下次起跑就是新版)"
    python3 -m py_compile "$REPO"/scripts/cycle_test.py || die "語法錯誤,沒有送出"
    for h in "$CRANE" "$BODY"; do
        scp -q "$REPO"/scripts/cycle_test.py "$h":~/projects/facade_cleaning_v2/scripts/ || die "scp $h 失敗"
    done
    md5sum "$REPO"/scripts/cycle_test.py
    $SSH "$CRANE" 'md5sum ~/projects/facade_cleaning_v2/scripts/cycle_test.py' </dev/null
    $SSH "$BODY"  'md5sum ~/projects/facade_cleaning_v2/scripts/cycle_test.py' </dev/null
}

# [2026-10-07 per user] Dashboard 攝影機轉播(本體 :8091,user service fcv-cam)。
#   攝影機在本體那段有線網路,吊機看不到 ⇒ 服務只裝在本體。重啟只會斷畫面,不影響控制。
deploy_cam() {
    say "同步 cam_relay.py + fcv-cam.service 到本體"
    python3 -m py_compile "$REPO"/scripts/cam_relay.py || die "語法錯誤,沒有送出"
    scp -q "$REPO"/scripts/cam_relay.py "$BODY":~/projects/facade_cleaning_v2/scripts/ || die "scp cam_relay.py 失敗"
    scp -q "$REPO"/scripts/systemd/fcv-cam.service "$BODY":~/.config/systemd/user/fcv-cam.service || die "scp unit 失敗"
    $SSH "$BODY" 'systemctl --user daemon-reload && systemctl --user enable fcv-cam >/dev/null 2>&1; systemctl --user restart fcv-cam && sleep 2 && systemctl --user is-active fcv-cam && curl -s -m 3 http://127.0.0.1:8091/cam/status; echo; md5sum ~/projects/facade_cleaning_v2/scripts/cam_relay.py' </dev/null || die "fcv-cam 啟動失敗(看本體 ~/run/logs/cam_service.log)"
    md5sum "$REPO"/scripts/cam_relay.py
}

# [2026-09-21] official 一次性前置檢查:ssh key 進得去、吊機 sudo -n 放行 restart。只讀不改。
prep_official() {
    [ "${FCV_TARGET:-test}" = official ] || die "請加 FCV_TARGET=official"
    local ok=1
    for h in "$CRANE" "$BODY"; do
        if $SSH -o BatchMode=yes "$h" true </dev/null 2>/dev/null; then echo "✅ ssh key OK  $h"
        else echo "🔴 ssh key 未裝  $h  → ssh-copy-id $h"; ok=0; fi
    done
    if $SSH -o BatchMode=yes "$CRANE" 'sudo -n systemctl status fcv-crane >/dev/null 2>&1' </dev/null 2>/dev/null; then echo "✅ sudo -n OK   $CRANE"
    else echo "🔴 sudo -n 不通  $CRANE  → 見檔頭第 2 點的 sudoers 一行"; ok=0; fi
    [ "$ok" = 1 ] || die "official 前置未完成"
}

show_status() {
    echo "=== 吊機 ${CRANE#*@}($TARGET_LABEL)==="
    $SSH "$CRANE" 'systemctl is-active fcv-crane fcv-web-v3 | tr "\n" " "; echo; python3 ~/run/crcmd.py status 2>/dev/null | tr " " "\n" | /bin/grep -E "^(length_left|length_right|wall_height_cm|home_ground_cm|hold_guard|level_auto)=" | tr "\n" " "; echo' </dev/null
    echo "=== 本體 ${BODY#*@} ==="
    $SSH "$BODY" 'systemctl --user is-active fcv-arm fcv-body fcv-cam | tr "\n" " "; echo; python3 ~/run/crcmd.py 127.0.0.1:5001 status 2>/dev/null | tr " " "\n" | /bin/grep -E "^(state|p[5-8]|arm_ready|estop|pusher_rpm)" | tr "\n" " "; echo' </dev/null
}

case "${1:-}" in
    body)   deploy_body ;;
    arm)    deploy_arm ;;
    crane)  deploy_crane ;;
    web)    PI="$CRANE" exec "$REPO/scripts/deploy_web.sh" ;;   # 版號戳記 + 三方 md5,不必重啟 node;PI 跟著 FCV_TARGET
    server) deploy_server ;;
    script) deploy_script ;;
    cam)    deploy_cam ;;
    status) show_status ;;
    prep-official) prep_official ;;
    *) sed -n '2,40p' "${BASH_SOURCE[0]}"; exit 2 ;;
esac
echo "✅ 完成:$1($TARGET_LABEL)"
