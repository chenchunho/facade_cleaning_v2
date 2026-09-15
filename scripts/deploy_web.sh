#!/usr/bin/env bash
# scripts/deploy_web.sh — push console v3 (web_backend/public_v3/index.html) to the crane Pi.
#
#   ./scripts/deploy_web.sh              # stamp version, scp, verify, print version + 3 md5
#   ./scripts/deploy_web.sh --dry-run    # stamp + show what would be sent, no scp
#   PI=user@192.168.5.25 ./scripts/deploy_web.sh   # override target (default below)
#
# What it guarantees (jim's rule: every deploy carries a fresh version so he can tell new from cached):
#   1. version = v3-YYYY.MM.DD-HHMM in Taiwan time, sed'ed into the ONE source line in index.html
#      (`var CONSOLE_VER = '...'`) — the repo copy changes too, so the stamp is committed with the code.
#   2. refuses to deploy if the source line is missing / duplicated, or the stamp did not land.
#   3. atomic on the Pi (scp to a temp name, then mv) — a page load never sees a half-written file.
#   4. three-way md5: local file / file on the Pi / page served by http://<pi>:8081 — any mismatch → exit 1.
#      server.js serves static files, so no restart is needed; the HTTP md5 is the proof.
# Exit 0 only when all of the above hold. (Project convention: 0 = no error.)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
SRC="$REPO/web_backend/public_v3/index.html"
PI="${PI:-user@192.168.5.25}"
PI_HOST="${PI#*@}"
PI_DIR="${PI_DIR:-projects/facade_cleaning_v2/web/public_v3}"   # Pi side is web/ not web_backend/
PI_PORT="${PI_PORT:-8081}"
SSH_OPTS=(-o ConnectTimeout=8 -o BatchMode=yes)
DRY=0; [[ "${1:-}" == "--dry-run" ]] && DRY=1

die(){ echo "🔴 $*" >&2; exit 1; }
md5(){ md5sum | cut -c1-32; }

[[ -f "$SRC" ]] || die "not found: $SRC"

# --- 1. stamp: exactly one source line, or refuse ----------------------------
PAT="^var CONSOLE_VER = 'v3-[0-9]{4}\.[0-9]{2}\.[0-9]{2}-[0-9]{4}';"
n=$(/bin/grep -cE "$PAT" "$SRC" || true)
[[ "$n" == 1 ]] || die "expected exactly 1 version source line in index.html, found $n (pattern: $PAT)"

VER="v3-$(TZ=Asia/Taipei date +%Y.%m.%d-%H%M)"
sed -i -E "s|$PAT|var CONSOLE_VER = '$VER';|" "$SRC"
/bin/grep -qF "var CONSOLE_VER = '$VER';" "$SRC" || die "stamp did not land in $SRC"
[[ $(/bin/grep -cE "^var CONSOLE_VER = " "$SRC") == 1 ]] || die "more than one CONSOLE_VER line after stamping"

# quick syntax gate: every inline <script> must still parse (a broken edit must not reach the Pi)
node -e '
const fs=require("fs");const h=fs.readFileSync(process.argv[1],"utf8");
let i=0;for(const m of h.matchAll(/<script>([\s\S]*?)<\/script>/g)){i++;try{new Function(m[1]);}catch(e){console.error("script block",i,":",e.message);process.exit(1);}}
if(!i){console.error("no <script> block found");process.exit(1);}' "$SRC" || die "index.html has a JS syntax error — not deploying"

LOCAL_MD5=$(md5 < "$SRC")
echo "version : $VER"
echo "local   : $LOCAL_MD5  $SRC"
if [[ $DRY == 1 ]]; then echo "(dry-run: not sending to $PI:$PI_DIR)"; exit 0; fi

# --- 2. atomic copy ------------------------------------------------------------
TMP=".index.html.$$.tmp"
scp "${SSH_OPTS[@]}" -q "$SRC" "$PI:$PI_DIR/$TMP" || die "scp to $PI:$PI_DIR failed"
ssh "${SSH_OPTS[@]}" "$PI" "cd '$PI_DIR' && mv -f '$TMP' index.html" || die "mv on the Pi failed (temp file $PI_DIR/$TMP may be left behind)"

# --- 3. three-way verify -------------------------------------------------------
PI_MD5=$(ssh "${SSH_OPTS[@]}" "$PI" "md5sum '$PI_DIR/index.html'" | cut -c1-32)
HTTP_MD5=$(curl -s --max-time 10 -H 'Cache-Control: no-cache' "http://$PI_HOST:$PI_PORT/" | md5)
echo "pi file : $PI_MD5  $PI:$PI_DIR/index.html"
echo "http    : $HTTP_MD5  http://$PI_HOST:$PI_PORT/"
if [[ "$LOCAL_MD5" != "$PI_MD5" || "$LOCAL_MD5" != "$HTTP_MD5" ]]; then
  die "md5 mismatch — the Pi is NOT serving what is in the repo (server not on :$PI_PORT, wrong PUBLIC_DIR, or copy failed)"
fi
echo "✅ deployed $VER — local / pi / http md5 all equal"
