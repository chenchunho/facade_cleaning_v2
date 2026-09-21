#!/bin/bash
# [2026-09-19] ExecStartPre for fcv-crane on official-crane.
# Pi boots ~40 s before the PoE switch / USR gateways are up; without this gate
# fcv-crane came up with every device "[WARN] ... skipped" and stayed that way
# until someone restarted it (device flags are set once at init).
# Waits for the two SD76 gateways' :4001 (they are the last to come up), max 180 s,
# then lets the service start anyway (the program prints WARN per device).
# 🔴 must be bash: /dev/tcp is a bash-only feature — with #!/bin/sh (dash) the test
#    never succeeds and the loop burns the full 180 s every boot.
i=0
while [ "$i" -lt 90 ]; do
  if (echo > /dev/tcp/192.168.1.30/4001) 2>/dev/null && (echo > /dev/tcp/192.168.1.34/4001) 2>/dev/null; then
    echo "[wait_devices] gateways ready after ${i}x2s"; exit 0
  fi
  i=$((i+1)); sleep 2
done
echo "[wait_devices] timeout 180s — 仍放行(程式會印 WARN)"; exit 0
