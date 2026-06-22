//=========== dom ===========

const logEl        = document.getElementById('log');
const dotW         = document.getElementById('dot-washrobot');
const dotC         = document.getElementById('dot-crane');
const dotE         = document.getElementById('dot-easy-crane');
const dotA         = document.getElementById('dot-arm');
const bannerEl     = document.getElementById('banner');
const panelsRobot  = document.querySelectorAll('.panel-washrobot');
const panelsCrane  = document.querySelectorAll('.panel-crane');
const panelsEasy   = document.querySelectorAll('.panel-easy_crane');
const panelsArm    = document.querySelectorAll('.panel-arm');
const modalBalance = document.getElementById('modal-balance');
const modalReturn  = document.getElementById('modal-return');

let ws = null;
let lastStatus = { washrobot: null, crane: null, easy_crane: null, arm: null }; // null = not yet known
let pendingHomeStatus = null; // { resolve, timeoutId }

//=========== connection ===========

function _onVisibilityChange() {
    if (document.visibilityState !== 'visible') return;
    if (!ws || ws.readyState === WebSocket.CLOSED || ws.readyState === WebSocket.CLOSING) {
        logSys('tab visible — force reconnect (skipping 2s retry timer)');
        connectWs();
    }
}

function connectWs() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    ws = new WebSocket(`${proto}://${location.host}`);

    ws.onopen  = () => {
        logSys('ws connected');
        // Force-sync state from backend on every (re)connect — otherwise
        // frontend may show stale STATUS / vacuum readings if a state_changed
        // EVT was missed during the WS outage. status reply contains state=...
        // + p1..p9 + crane_attached + roll/pitch which all auto-update via
        // existing parsers in onWashrobotLine.
        setTimeout(() => {
            send('washrobot', 'status');
            send('crane',     'status');
        }, 200);
    };
    ws.onclose = () => {
        logSys('ws closed — retrying in 2s');
        setDot(dotW, false);
        setDot(dotC, false);
        setDot(dotE, false);
        setDot(dotA, false);
        updateArmButtonStates(false);
        applyMode(false, false, false, false);
        setTimeout(connectWs, 2000);
    };
    ws.onerror = () => logSys('ws error');

    // Backgrounded tabs have their setTimeout throttled (Chrome 1s+) or frozen
    // entirely. When the user returns, we don't want to wait for the 2s retry
    // timer — force an immediate reconnect if the socket is dead.
    // (Handler attached once at script load; re-attaching per connectWs() is safe
    // because connectWs is usually called from within this same handler.)
    document.addEventListener('visibilitychange', _onVisibilityChange);
    ws.onmessage = (e) => {
        let m;
        try { m = JSON.parse(e.data); } catch { return; }

        if (m.src === 'status') {
            setDot(dotW, m.washrobot);
            setDot(dotC, m.crane);
            setDot(dotE, m.easy_crane);
            setDot(dotA, m.arm);   // cleaning arm dot — connectivity only (no cross-device debounce)
            updateArmButtonStates(!!m.arm);
            // Arm uses its own panel-arm class (applyMode handles disable).
            // No debounce — apply immediately on change. handleStatusChange()
            // only re-runs applyMode when w/c/e change, so we must trigger it
            // ourselves when only arm flips (otherwise panel stays disabled
            // even after the dot turns green).
            const armChanged = (lastStatus.arm !== !!m.arm);
            lastStatus.arm = !!m.arm;
            handleStatusChange(!!m.washrobot, !!m.crane, !!m.easy_crane);
            if (armChanged) {
                applyMode(lastStatus.washrobot, lastStatus.crane, lastStatus.easy_crane, lastStatus.arm);
            }
            return;
        }
        // Mute high-frequency status poll replies to keep log clean;
        // manual refresh reply looks identical but losing one log line is OK UX.
        const isEasyPoll = m.src === 'easy_crane' &&
                           m.line.startsWith('OK weight=') &&
                           m.line.includes('up_stop_kg=');
        const isCranePoll = m.src === 'crane' &&
                            m.line.startsWith('OK length_left=') &&
                            m.line.includes('tension_left=');
        // [2026-06-01] Washrobot status auto-poll (every 500ms for IMU panel)
        // reply pattern: "OK state=<name> crane_attached=... arm_attached=...
        // p1=... pitch=N". Mute log spam — manual refresh button still works
        // via /status text in the input box (same line but typed by user, not muted).
        const isWashrobotPoll = m.src === 'washrobot' &&
                                m.line.startsWith('OK state=') &&
                                m.line.includes('p1=') &&
                                m.line.includes('pitch=');
        if (!isEasyPoll && !isCranePoll && !isWashrobotPoll) logRx(m.src, m.line);

        if (m.src === 'washrobot')       onWashrobotLine(m.line);
        else if (m.src === 'crane')      onCraneLine(m.line);
        else if (m.src === 'easy_crane') onEasyCraneLine(m.line);
    };
}

function setDot(el, ok) { el.classList.toggle('ok', !!ok); }

// Disable `.arm-action` buttons when cleaning_arm motor_api is unreachable.
// (panel-washrobot already covers the case where washrobot itself is down.)
// arm_attached on/off buttons are intentionally NOT .arm-action — user should
// be able to toggle even when arm motor_api is offline.
function updateArmButtonStates(connected) {
    document.querySelectorAll('.arm-action').forEach(el => {
        el.disabled = !connected;
        el.title    = connected ? '' : 'arm motor_api 未連線（網頁 header arm dot 紅）';
    });
}

//=========== degraded mode ===========

// Debounce "device went down" transitions to absorb brief reconnects.
// Recovery (false→true) is instant; only "down" (true→false) waits N ms before
// touching panel-disabled / cross-stop. Reason: washrobot restart, network blip,
// or just the 1s server reconnect cycle would otherwise flicker the UI and
// trigger spurious cross-device emergency_stop.
const DEBOUNCE_DOWN_MS = 3000;
let pendingRawStatus = { washrobot: null, crane: null, easy_crane: null };
let pendingDownTimers = { washrobot: null, crane: null, easy_crane: null };

function handleStatusChange(wNew, cNew, eNew) {
    pendingRawStatus = { washrobot: wNew, crane: cNew, easy_crane: eNew };
    debounceDeviceTransition_('washrobot', wNew);
    debounceDeviceTransition_('crane',     cNew);
    debounceDeviceTransition_('easy_crane', eNew);
}

function debounceDeviceTransition_(name, newVal) {
    if (lastStatus[name] === newVal) return;   // no change to applied state

    if (newVal === true) {
        // Recovery — cancel pending down timer, apply immediately
        if (pendingDownTimers[name]) {
            clearTimeout(pendingDownTimers[name]);
            pendingDownTimers[name] = null;
            logSys(`${name} reconnected within ${DEBOUNCE_DOWN_MS}ms — UI not changed`);
        }
        applyDeviceTransition_(name, true);
    } else {
        // Going down — start (or restart) debounce timer
        if (pendingDownTimers[name]) clearTimeout(pendingDownTimers[name]);
        pendingDownTimers[name] = setTimeout(() => {
            pendingDownTimers[name] = null;
            // Verify still down at fire time (could have flapped multiple times)
            if (pendingRawStatus[name] === false) {
                applyDeviceTransition_(name, false);
            }
        }, DEBOUNCE_DOWN_MS);
    }
}

function applyDeviceTransition_(name, newVal) {
    // Cross-device auto-stop: when transitioning OK→bad and the other was up.
    // Only fires after debounce confirms real disconnect.
    if (newVal === false) {
        const wasBothUp = lastStatus.washrobot === true && lastStatus.crane === true;
        if (wasBothUp) {
            if (name === 'washrobot') {
                send('crane', 'stop');
                logSys('washrobot 失聯 (>3s) → 自動送 stop 給 crane');
            } else if (name === 'crane') {
                send('washrobot', 'emergency_stop');
                logSys('crane 失聯 (>3s) → 自動送 emergency_stop 給 washrobot');
            }
        }
        // Reset crane busy/init state on disconnect so reconnect shows "初始化中"
        // banner until first status reply lands.
        if (name === 'crane') {
            craneInitDone     = false;
            craneMotionActive = false;
            if (typeof updateCraneButtonStates === 'function') updateCraneButtonStates();
        }
    }
    lastStatus[name] = newVal;
    applyMode(lastStatus.washrobot, lastStatus.crane, lastStatus.easy_crane, lastStatus.arm);
}

// States during which manual easy_crane control is locked out (frontend safety
// belt — easy_crane has hardware interlock that would interrupt shim's open-loop
// timed motion if user clicks buttons during auto operation).
const AUTO_BUSY_STATES = new Set(['running', 'balancing', 'paused_on_error', 'returning_home']);
function isWashrobotAutoBusy() {
    return AUTO_BUSY_STATES.has(washrobotState);
}

function applyMode(w, c, e, a) {
    panelsRobot.forEach(p => p.classList.toggle('panel-disabled', !w));
    panelsCrane.forEach(p => p.classList.toggle('panel-disabled', !c));
    // easy_crane disabled if (TCP down) OR (washrobot in auto-busy state)
    const easyLocked = !e || isWashrobotAutoBusy();
    panelsEasy .forEach(p => p.classList.toggle('panel-disabled', easyLocked));
    // cleaning arm panel: disabled only when arm motor_api is down.
    // (panel-arm only contains buttons that go DIRECTLY to motor_api — INIT /
    // DEPLOY / PARK / STATUS. washrobot-orchestrated arm commands like
    // arm_attached + clean_sweep live in a separate panel-washrobot section
    // and disable when washrobot is down via panelsRobot above.)
    panelsArm  .forEach(p => p.classList.toggle('panel-disabled', !a));

    // Banner 只反映主系統（washrobot + crane），easy crane 獨立子系統不影響
    bannerEl.className = 'banner';
    if (!w && !c) {
        bannerEl.classList.add('banner-err');
        bannerEl.textContent = '⚠ 全線失聯，請檢查網路';
    } else if (!w && c) {
        bannerEl.classList.add('banner-warn');
        bannerEl.textContent = '⚠ WASHROBOT 失聯（救援模式）— 僅 crane 可控，確認機體是否懸吊在外並手動收繩';
    } else if (w && !c) {
        bannerEl.classList.add('banner-err');
        bannerEl.textContent = '⚠ CRANE 失聯 — 禁止啟動下移循環';
    } else {
        bannerEl.classList.add('banner-hidden');
        bannerEl.textContent = '';
    }
}

//=========== log ===========

function addLog(cls, text) {
    const line = document.createElement('div');
    line.className = cls;
    line.textContent = text;
    logEl.appendChild(line);
    logEl.scrollTop = logEl.scrollHeight;
}
function logSys(t)  { addLog('log-sys', `[sys] ${t}`); }
function logTx(tgt, cmd) { addLog('log-tx', `→ ${tgt}: ${cmd}`); }
function logRx(src, line) {
    let cls = 'log-ok';
    if (line.startsWith('ERR'))      cls = 'log-err';
    else if (line.startsWith('EVT')) cls = 'log-evt';
    else if (src === 'error')        cls = 'log-err';
    addLog(cls, `← ${src}: ${line}`);
}

document.getElementById('btn-clear').onclick = () => { logEl.innerHTML = ''; };

//=========== send ===========

function send(target, cmd) {
    if (!ws || ws.readyState !== 1) return logSys('ws not ready');
    ws.send(JSON.stringify({ target, cmd }));
    logTx(target, cmd);
}

// Anti-double-click guard for long-running motion buttons. Bench 2026-05-13:
// GUI single click produced two pay_out cmds on crane (root cause unclear —
// possibly mouse stutter / event duplication). Now: disable for `ms` after
// click to swallow back-to-back clicks. Server-side try_lock on motion_mtx
// is the real protection; this just gives UI feedback + saves one round trip.
function disableBriefly(btn, ms) {
    btn.disabled = true;
    setTimeout(() => { btn.disabled = false; }, ms);
}
const MOTION_DEBOUNCE_MS = 1500;

// any button with data-tgt + data-cmd is a one-shot command
document.querySelectorAll('button[data-tgt][data-cmd]').forEach(btn => {
    btn.addEventListener('click', () => {
        send(btn.dataset.tgt, btn.dataset.cmd);
        if (btn.dataset.cmd === 'align_lengths') disableBriefly(btn, MOTION_DEBOUNCE_MS);
    });
});

//=========== device line handlers ===========

// Track which crane devices/gateways are available. Updated from cmd_status
// replies (dev_* / gw_* fields) and EVT device_state broadcasts. Buttons in
// the crane panel have data-required="<token,...>"; updateCraneDeviceUI()
// disables them when any required token's flag is false.
//
// Token vocabulary matches the server-side flag names. Group shorthands like
// "motion_full" / "meters_all" are expanded via DEVICE_TOKEN_GROUPS.
const craneDevices = {
    gw_a: true, gw_b: true, gw_m: true, gw_c: true, gw_d: true,
    se3_left: true, se3_right: true,
    meter_left: true, meter_right: true, meter_middle: true,
    clv900: true,
    dsz_left: true, dsz_right: true,
};
const DEVICE_TOKEN_GROUPS = {
    motion_full: ['se3_left','se3_right','meter_left','meter_right','meter_middle','clv900','dsz_left','dsz_right'],
    motion_diff: ['se3_left','se3_right','meter_left','meter_right','dsz_left','dsz_right'],
    meters_all:  ['meter_left','meter_right','meter_middle'],
    // bench-friendly variants — middle pipeline (meter_middle + clv900) is
    // optional hardware; server-side motion_rope and zero_meters skip middle
    // gracefully if not installed. Buttons using these groups stay enabled
    // when only middle is missing.
    motion_lr:   ['se3_left','se3_right','meter_left','meter_right','dsz_left','dsz_right'],
    meters_lr:   ['meter_left','meter_right'],
};
const DEVICE_LABEL_TW = {
    gw_a: 'USR_A 閘道(SE3 左)', gw_b: 'USR_B 閘道(SE3 右)', gw_m: 'USR_M 閘道(計米器)',
    gw_c: 'USR_C 閘道(張力左)', gw_d: 'USR_D 閘道(張力右)',
    se3_left: '左繩變頻器', se3_right: '右繩變頻器',
    meter_left: '左繩計米', meter_right: '右繩計米', meter_middle: '中間管線計米',
    clv900: '中間絞盤變頻器',
    dsz_left: '左繩張力', dsz_right: '右繩張力',
};
function expandDeviceTokens(str) {
    if (!str) return [];
    const out = [];
    str.split(',').map(s => s.trim()).filter(Boolean).forEach(tok => {
        if (DEVICE_TOKEN_GROUPS[tok]) out.push(...DEVICE_TOKEN_GROUPS[tok]);
        else out.push(tok);
    });
    return out;
}
// Parse `dev_<name>=0|1` and `gw_<name>=0|1` from any crane reply line.
// Returns true if any flag changed.
function parseCraneDeviceState(line) {
    let changed = false;
    Object.keys(craneDevices).forEach(k => {
        // matches both "dev_se3_left=1" (cmd_status) and "se3_left=1" (EVT device_state)
        const re = new RegExp(`\\b(?:dev_)?${k}=(\\d)\\b`);
        const m = line.match(re);
        if (m) {
            const val = (m[1] === '1');
            if (craneDevices[k] !== val) { craneDevices[k] = val; changed = true; }
        }
    });
    return changed;
}
// Busy/init state from crane cmd_status + EVT init_done. Combined with the
// per-device data-required gating in updateCraneButtonStates().
//   craneInitDone     : false until crane main() finishes init phase (~few s)
//   craneMotionActive : true during pay_out / retract / align_lengths /
//                       zero_meters_with_motion / any active hold button.
//                       Server sets this in MotionScope RAII + hold_loop.
let craneInitDone     = false;
let craneMotionActive = false;

// Replaces former updateCraneDeviceUI(): composes three button-disable layers:
//   1. data-required tokens (device down → disable)
//   2. crane init not done (everything except .crane-readonly disabled)
//   3. motion_active (auto-motion locks .crane-action; auto-motion-without-
//      local-hold locks .crane-hold-btn too; local hold leaves hold buttons
//      enabled so user can release / switch sides)
// .crane-readonly (status / home_status / STOP) NEVER disabled — STOP must
// always be reachable, and read-only commands cost nothing.
function updateCraneButtonStates() {
    const anyLocalHold = Object.values(craneHoldState).some(v => v);

    // .crane-readonly: always enabled regardless of init/motion/dev state
    document.querySelectorAll('.crane-readonly').forEach(el => {
        el.disabled = false;
        el.title    = '';
    });

    // .crane-action + .crane-hold-btn: combine the three layers
    document.querySelectorAll('.crane-action, .crane-hold-btn').forEach(el => {
        const reasons = [];
        // Layer 1: data-required device availability
        const req = el.getAttribute('data-required');
        if (req) {
            const tokens  = expandDeviceTokens(req);
            const missing = tokens.filter(t => craneDevices[t] === false);
            if (missing.length) {
                reasons.push('不可用：' + missing.map(t => DEVICE_LABEL_TW[t] || t).join('、'));
            }
        }
        // Layer 2: init not done
        if (!craneInitDone) reasons.push('crane 初始化中…');
        // Layer 3: motion-active gating (differs for action vs hold buttons)
        if (craneMotionActive) {
            if (el.classList.contains('crane-hold-btn')) {
                if (!anyLocalHold) reasons.push('自動動作執行中');
            } else {
                reasons.push('執行中');
            }
        }
        el.disabled = reasons.length > 0;
        el.title    = reasons.join(' / ');
    });

    // Per-device down banner (kept from former updateCraneDeviceUI)
    const banner = document.getElementById('crane-device-state-row');
    const msg    = document.getElementById('crane-device-state-msg');
    if (banner && msg) {
        const down = Object.entries(craneDevices).filter(([_, v]) => !v).map(([k]) => k);
        if (down.length === 0) {
            banner.style.display = 'none';
            msg.textContent = '';
        } else {
            banner.style.display = '';
            msg.textContent = down.map(k => DEVICE_LABEL_TW[k] || k).join('、') + ' 不可用 — 相關控制已停用';
        }
    }

    // Busy banner at top of crane panel
    const busyRow   = document.getElementById('crane-busy-row');
    const busyLabel = document.getElementById('crane-busy-label');
    if (busyRow && busyLabel) {
        if (!craneInitDone) {
            busyRow.classList.add('active');
            busyLabel.textContent = '⏳ crane 初始化中… 請稍候';
        } else if (craneMotionActive && !anyLocalHold) {
            busyRow.classList.add('active');
            busyLabel.textContent = '⏳ 自動動作執行中…（STOP 仍可按）';
        } else if (craneMotionActive && anyLocalHold) {
            busyRow.classList.add('active');
            busyLabel.textContent = '🖐 手動拉/放繩中…（其他動作鎖定）';
        } else {
            busyRow.classList.remove('active');
            busyLabel.textContent = '';
        }
    }
}

// Back-compat alias — older call sites may still invoke this name.
function updateCraneDeviceUI() { updateCraneButtonStates(); }

function onCraneLine(line) {
    if (pendingHomeStatus && line.startsWith('OK home_ground_cm=')) {
        const m = line.match(/remaining=(-?\d+)/);
        const remaining = m ? parseInt(m[1], 10) : null;
        clearTimeout(pendingHomeStatus.timeoutId);
        const resolver = pendingHomeStatus.resolve;
        pendingHomeStatus = null;
        resolver(remaining);
    }

    // Device state from cmd_status reply OR EVT device_state. Single regex pass
    // catches both formats; UI refresh only when something actually changed.
    let stateChanged = parseCraneDeviceState(line);

    // init_done / motion_active are emitted in cmd_status; init_done also gets
    // its own EVT init_done broadcast at end of crane main() init sequence.
    const idm = line.match(/\binit_done=(\d)\b/);
    if (idm) {
        const v = (idm[1] === '1');
        if (craneInitDone !== v) { craneInitDone = v; stateChanged = true; }
    } else if (line.startsWith('EVT init_done')) {
        if (!craneInitDone) { craneInitDone = true; stateChanged = true; }
    }
    const mam = line.match(/\bmotion_active=(\d)\b/);
    if (mam) {
        const v = (mam[1] === '1');
        if (craneMotionActive !== v) { craneMotionActive = v; stateChanged = true; }
    }
    if (stateChanged) updateCraneButtonStates();

    // Parse crane status fields (cmd_status reply): tension_left/right, hold flags,
    // up_stop_total_kg. Update DOM cells; also resync local hold button state.
    const tlm = line.match(/tension_left=(-?\d+\.?\d*)/);
    const trm = line.match(/tension_right=(-?\d+\.?\d*)/);
    const tvm = line.match(/tension_valid=(\d+)/);
    if (tlm && trm && tvm) {
        const valid = (tvm[1] === '1');
        const elL = document.getElementById('crane-tension-left');
        const elR = document.getElementById('crane-tension-right');
        const elT = document.getElementById('crane-tension-total');
        if (valid) {
            const tl = parseFloat(tlm[1]);
            const tr = parseFloat(trm[1]);
            if (elL) elL.textContent = tl.toFixed(2);
            if (elR) elR.textContent = tr.toFixed(2);
            if (elT) elT.textContent = (tl + tr).toFixed(2);
        } else {
            if (elL) elL.textContent = 'ERR';
            if (elR) elR.textContent = 'ERR';
            if (elT) elT.textContent = 'ERR';
        }
    }
    const ulm = line.match(/up_stop_total_kg=(-?\d+\.?\d*)/);
    if (ulm) {
        const el = document.getElementById('crane-up-stop-total-current');
        if (el) el.textContent = parseFloat(ulm[1]).toFixed(1);
    }
    const tmm2 = line.match(/tension_max_kg=(-?\d+\.?\d*)/);
    if (tmm2) {
        const el = document.getElementById('crane-tension-max-current');
        if (el) el.textContent = parseFloat(tmm2[1]).toFixed(1);
    }
    const tdm = line.match(/tension_diff_max_kg=(-?\d+\.?\d*)/);
    if (tdm) {
        const el = document.getElementById('crane-tension-diff-current');
        if (el) el.textContent = parseFloat(tdm[1]).toFixed(1);
    }
    const rtm = line.match(/retract_tension_stop_kg=(-?\d+\.?\d*)/);
    if (rtm) {
        const el = document.getElementById('crane-retract-tension-stop-current');
        if (el) el.textContent = parseFloat(rtm[1]).toFixed(1);
    }
    // Frequency current values (read-only display next to input)
    const updateHzCell = (id, raw) => {
        const el = document.getElementById(id);
        if (el) el.textContent = parseFloat(raw).toFixed(1) + ' Hz';
    };
    const hhm = line.match(/\bhold_hz=(-?\d+\.?\d*)/);
    const hmm = line.match(/\bmotion_hz=(-?\d+\.?\d*)/);
    const hwm = line.match(/\bmiddle_hz=(-?\d+\.?\d*)/);
    if (hhm) updateHzCell('crane-hold-hz-current',   hhm[1]);
    if (hmm) updateHzCell('crane-motion-hz-current', hmm[1]);
    if (hwm) updateHzCell('crane-middle-hz-current', hwm[1]);

    // DSZL scale current values (small numbers can be in scientific notation)
    const dsLm = line.match(/\bdsz_left_scale=(-?\d+\.?\d*(?:[eE][+-]?\d+)?)/);
    const dsRm = line.match(/\bdsz_right_scale=(-?\d+\.?\d*(?:[eE][+-]?\d+)?)/);
    const updateScaleCell = (id, raw) => {
        const el = document.getElementById(id);
        if (el) el.textContent = parseFloat(raw).toString();
    };
    if (dsLm) updateScaleCell('crane-dsz-left-scale-current',  dsLm[1]);
    if (dsRm) updateScaleCell('crane-dsz-right-scale-current', dsRm[1]);

    // SD76 length readouts: cmd_status emits length_left / length_right /
    // length_middle (cm, int) + home_ground_cm. ERR string passed through.
    const updateLenCell = (id, raw) => {
        const el = document.getElementById(id);
        if (!el) return;
        if (raw === 'ERR') el.textContent = 'ERR';
        else               el.textContent = parseInt(raw, 10);
    };
    const llm = line.match(/length_left=(-?\d+|ERR)/);
    const lrm = line.match(/length_right=(-?\d+|ERR)/);
    const lmm = line.match(/length_middle=(-?\d+|ERR)/);
    if (llm) updateLenCell('crane-length-left',   llm[1]);
    if (lrm) updateLenCell('crane-length-right',  lrm[1]);
    if (lmm) updateLenCell('crane-length-middle', lmm[1]);
    const hgm = line.match(/home_ground_cm=(-?\d+)/);
    if (hgm) {
        const home = parseInt(hgm[1], 10);
        const elH = document.getElementById('crane-home-ground');
        if (elH) elH.textContent = home;
        // 剩 = home_ground − |left|; only shown when left length is valid
        const elR = document.getElementById('crane-remaining');
        if (elR) {
            if (llm && llm[1] !== 'ERR') {
                const left = parseInt(llm[1], 10);
                elR.textContent = (home - Math.abs(left));
            } else {
                elR.textContent = '?';
            }
        }
    }

    // Resync hold button state from server flags. Server is authoritative —
    // safety auto-stop / watchdog timeout will clear flags server-side; client
    // must reflect that even if user is still pressing.
    const flagMap = {
        'up_left':    'btn-crane-up-left',
        'up_right':   'btn-crane-up-right',
        'down_left':  'btn-crane-down-left',
        'down_right': 'btn-crane-down-right',
    };
    let holdChanged = false;
    for (const [field, btnId] of Object.entries(flagMap)) {
        const re = new RegExp(`\\b${field}=(\\d)\\b`);
        const m = line.match(re);
        if (!m) continue;
        const on = (m[1] === '1');
        const btn = document.getElementById(btnId);
        if (btn) btn.classList.toggle('active', on);
        // Sync per-button local flag too (used by hold logic + busy gating)
        if (craneHoldState[field] !== on) {
            craneHoldState[field] = on;
            holdChanged = true;
        }
    }
    // Combined up/down "active" tint = both sides on
    const btnUp   = document.getElementById('btn-crane-up');
    const btnDown = document.getElementById('btn-crane-down');
    if (btnUp)   btnUp  .classList.toggle('active', craneHoldState.up_left  && craneHoldState.up_right);
    if (btnDown) btnDown.classList.toggle('active', craneHoldState.down_left && craneHoldState.down_right);
    // Hold state transitions affect crane-hold-btn enable gating
    if (holdChanged) updateCraneButtonStates();

    // EVT alarms: tension_total_limit / tension_alarm — surface to log header
    if (line.startsWith('EVT tension_total_limit') || line.startsWith('EVT tension_alarm')) {
        // Server already auto-stopped; flags will resync via next status poll.
        // Just visually flash the buttons / clear local state.
        Object.keys(craneHoldState).forEach(k => craneHoldState[k] = false);
        Object.values(flagMap).forEach(id => {
            const btn = document.getElementById(id);
            if (btn) btn.classList.remove('active');
        });
        if (btnUp)   btnUp  .classList.remove('active');
        if (btnDown) btnDown.classList.remove('active');
    }
}

let washrobotState = 'unknown';
let lastPauseContext = '';

function onWashrobotLine(line) {
    // [2026-05-29] Settings page: get_settings reply contains "key=cur:def"
    // pairs. Forward to settings handler if attached (settings page initialized).
    if (window.handleSettingsReply) window.handleSettingsReply(line);

    // [2026-06-09] water_inlet GUI panel state machine — reply tracking + EVT
    if (typeof wiHandleReplyLine === 'function')    wiHandleReplyLine(line);
    if (typeof wiHandleWatchdogEvt === 'function')  wiHandleWatchdogEvt(line);

    if (line.startsWith('EVT balance_ask')) {
        const r = line.match(/roll=(\S+)/);
        const p = line.match(/pitch=(\S+)/);
        showBalanceModal(r ? r[1] : '?', p ? p[1] : '?');
    }

    // [2026-06-04] run_avoid modal trigger.
    //   EVT obstacle_ask action=X step_cm=Y iter=Z reason=...
    if (line.startsWith('EVT obstacle_ask')) {
        const a = line.match(/action=(\S+)/);
        const s = line.match(/step_cm=(\S+)/);
        const i = line.match(/iter=(\S+)/);
        const r = line.match(/reason=(.+)$/);
        showObstacleModal(
            a ? a[1] : '?',
            s ? s[1] : '?',
            i ? i[1] : '?',
            r ? r[1].trim() : ''
        );
    }
    // Auto-close modal if loop ended (cancel/abort/done)
    if (line.startsWith('EVT run_avoid_done') ||
        line.startsWith('EVT run_avoid_user_cancel') ||
        line.startsWith('EVT run_avoid_block') ||
        line.startsWith('EVT run_avoid_detector_fail') ||
        line.startsWith('EVT run_avoid_step_down_fail') ||
        line.startsWith('EVT run_avoid_user_timeout')) {
        hideObstacleModal();
    }

    // [2026-06-05] Scripted run — replies + progress EVTs.
    //   OK scripts=[a,b,c]     ← list_scripts reply
    //   OK csv=<csv>           ← load_script reply (refill textarea)
    //   EVT script_start total_steps=N total_cm=X
    //   EVT script step K/N cm=Y
    //   EVT script_complete status=ok|fail [step=K reason=...]
    {
        const lsm = line.match(/^OK scripts=\[(.*)\]/);
        if (lsm) {
            const names = lsm[1].split(',').map(s => s.trim()).filter(Boolean);
            renderSavedScripts(names);
        }
        const lcm = line.match(/^OK csv=(.+)$/);
        if (lcm) {
            $scriptCsv.value = lcm[1].trim();
            updateScriptPreview();
        }
        const sst = line.match(/^EVT script_start total_steps=(\d+) total_cm=(\d+)/);
        if (sst) showScriptProgress(0, parseInt(sst[1], 10), 0, '');
        const sstep = line.match(/^EVT script step (\d+)\/(\d+) cm=(\d+)(?: mode=(\S+))?/);
        if (sstep) showScriptProgress(parseInt(sstep[1], 10),
                                      parseInt(sstep[2], 10),
                                      parseInt(sstep[3], 10),
                                      sstep[4] || '');
        if (line.startsWith('EVT script_complete')) {
            finishScriptProgress(line.includes('status=ok'));
        }
    }

    // Track washrobot state from:
    //   - cmd_status reply: "OK state=<name> ..."
    //   - state change EVT: "EVT state_changed <old> <new>"  ← new state is 2nd arg
    const sm = line.match(/\bstate=(\S+)/)
            || line.match(/EVT\s+state_changed\s+\S+\s+(\S+)/);
    if (sm) {
        washrobotState = sm[1];
        updateErrorPauseUI();
        // Re-evaluate easy_crane lock (auto-busy state changes lock state)
        applyMode(lastStatus.washrobot, lastStatus.crane, lastStatus.easy_crane, lastStatus.arm);
    }

    // Capture the failure context when entering PausedOnError
    if (line.startsWith('EVT error_pause')) {
        const cm = line.match(/context=(\S+)/);
        lastPauseContext = cm ? cm[1] : '?';
        updateErrorPauseUI();
    }

    // Update vacuum readings panel from any line containing pN=value
    parseVacuumValues(line);

    // [2026-06-06] Update water level cell from cmd_water_level reply
    // "OK water_full=<0|1> rssi=<N>" or "ERR xkc_unreachable"
    parseWaterLevel(line);

    // [2026-06-01] Update IMU panel from any line containing roll=N pitch=N
    parseImuValues(line);

    // [2026-06-02] Update balance calibration panel from EVT balance_cal*
    parseBalanceCalibration(line);

    // Sync crane_attached toggle status from cmd_status reply or EVT
    const am = line.match(/\bcrane_attached=?\s*(on|off)/);
    if (am) {
        const on = (am[1] === 'on');
        const txt = document.getElementById('crane-attached-status');
        if (txt) txt.textContent = `(currently: ${am[1]})`;
        // Mirror to the prominent badge in crane panel
        const badge = document.getElementById('crane-link-badge');
        if (badge) {
            badge.textContent = on ? '🟢 ATTACHED (washrobot 驅動)' : '⚪ DETACHED (skip)';
            badge.classList.toggle('link-ok',   on);
            badge.classList.toggle('link-down', !on);
        }
    }

    // [2026-05-29] Sync arm_attached toggle status (mirrors crane_attached handler)
    const aa = line.match(/\barm_attached=?\s*(on|off)/);
    if (aa) {
        const on = (aa[1] === 'on');
        const txt = document.getElementById('arm-attached-status');
        if (txt) txt.textContent = `(currently: ${aa[1]})`;
        const badge = document.getElementById('arm-link-badge');
        if (badge) {
            badge.textContent = on ? '🟢 ATTACHED (washrobot 驅動)' : '⚪ DETACHED (skip)';
            badge.classList.toggle('link-ok',   on);
            badge.classList.toggle('link-down', !on);
        }
    }

    // [2026-06-01] Sync obstacle_detect toggle status (same pattern as arm/crane)
    const od = line.match(/\bobstacle_detect=?\s*(on|off)/);
    if (od) {
        const on = (od[1] === 'on');
        const txt = document.getElementById('obstacle-detect-status');
        if (txt) txt.textContent = `(currently: ${od[1]})`;
        const badge = document.getElementById('obstacle-link-badge');
        if (badge) {
            badge.textContent = on ? '🟢 ENABLED (pre-step check active)' : '⚪ DISABLED (skip)';
            badge.classList.toggle('link-ok',   on);
            badge.classList.toggle('link-down', !on);
        }
    }
}

function updateErrorPauseUI() {
    const isPaused = (washrobotState === 'paused_on_error');
    const isError  = (washrobotState === 'error');
    const btnC = document.getElementById('btn-continue');
    const btnS = document.getElementById('btn-skip');
    const btnR = document.getElementById('btn-recover');
    const status = document.getElementById('error-pause-status');
    const label = document.getElementById('error-pause-label');
    const ctx = document.getElementById('error-pause-context');
    if (!btnC || !btnS || !btnR || !status || !label || !ctx) return;

    // 繼續 / 略過 only available in PausedOnError
    btnC.disabled = !isPaused;
    btnS.disabled = !isPaused;
    // recover only available in Error (after step_down failed etc)
    btnR.disabled = !isError;

    if (isPaused) {
        status.classList.add('active');
        label.textContent = 'ERROR —';
        ctx.textContent = lastPauseContext || '(unknown)';
    } else if (isError) {
        status.classList.add('active');
        label.textContent = 'ERROR — 可按「恢復至 Attached」驗真空回復';
        ctx.textContent = '';
    } else {
        status.classList.remove('active');
        label.textContent = washrobotState;
        ctx.textContent = '';
    }
}

// Initialize UI on script load
updateErrorPauseUI();
// updateCraneButtonStates() initial call moved to bottom of file — it reads
// craneHoldState which is a `const` declared later, so calling it here would
// throw a temporal-dead-zone ReferenceError that halts the entire script
// (observed 2026-05-14: caused all three status dots to stay red because
// connectWs() never ran).

//=========== vacuum readings panel ===========

// Parse all pN=value occurrences from any line and update vac-N cells +
// color-code by attachment strength.
function parseVacuumValues(line) {
    const re = /\bp(\d+)=(-?\d+\.?\d*)/g;
    let m;
    let updated = false;
    while ((m = re.exec(line)) !== null) {
        const id = parseInt(m[1], 10);
        const val = parseFloat(m[2]);
        const cell = document.getElementById(`vac-${id}`);
        if (!cell) continue;
        cell.textContent = `p${id} = ${val}`;
        cell.classList.remove('vac-strong', 'vac-weak', 'vac-none');
        if (val <= -50)      cell.classList.add('vac-strong');   // attached
        else if (val <= -10) cell.classList.add('vac-weak');     // partial seal
        else                 cell.classList.add('vac-none');     // detached
        updated = true;
    }
    return updated;
}

const btnRefreshVacuum = document.getElementById('btn-refresh-vacuum');
if (btnRefreshVacuum) {
    btnRefreshVacuum.onclick = () => send('washrobot', 'status');
}

//=========== water level (XKC) ===========
// Parse "water_full=<0|1> rssi=<N>" from cmd_water_level reply.
// Cell shows "FULL rssi=N" (green) / "NOT FULL rssi=N" (orange) / "ERR" (red).
function parseWaterLevel(line) {
    const cell = document.getElementById('water-level-cell');
    if (!cell) return;
    const m = line.match(/water_full=(\d+)\s+rssi=(\d+)/);
    if (m) {
        const full = m[1] === '1';
        const rssi = m[2];
        cell.textContent = full ? `FULL  (rssi=${rssi})` : `NOT FULL  (rssi=${rssi})`;
        cell.classList.remove('vac-strong', 'vac-weak', 'vac-none');
        cell.classList.add(full ? 'vac-strong' : 'vac-weak');
        return;
    }
    if (line.includes('xkc_unreachable')) {
        cell.textContent = 'ERR xkc_unreachable';
        cell.classList.remove('vac-strong', 'vac-weak');
        cell.classList.add('vac-none');
    }
}

const btnRefreshWaterLevel = document.getElementById('btn-refresh-water-level');
if (btnRefreshWaterLevel) {
    btnRefreshWaterLevel.onclick = () => send('washrobot', 'water_level');
}

// ====================================================================
// [2026-06-09] Water inlet interactive panel
//
// Backend already protects against leak via:
//   - set_water_inlet_(false) retry 3× per call
//   - watchdog thread force-close after 5 min
//   - emergency_stop / stop() forced close on exit
//
// GUI layer adds user-facing protection:
//   - Visual state indicator (gray=OFF, green pulse=ON, red=error)
//   - "Open for X sec" elapsed counter
//   - Auto-OFF 60s after click ON (re-click resets timer)
//   - Toast banner when backend EVT water_inlet_watchdog_force_close arrives
//
// Only tracks GUI-originated open/close. Sweep-flow opens are invisible here
// (sweep manages its own valve state). Watchdog EVT does surface either way.
// ====================================================================

const WATER_INLET_AUTO_OFF_SEC = 60;

const $wiDot        = document.getElementById('water-inlet-dot');
const $wiState      = document.getElementById('water-inlet-state');
const $wiTimers     = document.getElementById('water-inlet-timers');
const $wiElapsed    = document.getElementById('water-inlet-elapsed');
const $wiCountdown  = document.getElementById('water-inlet-countdown');
const $wiBtnOn      = document.getElementById('btn-water-inlet-on');
const $wiBtnOff     = document.getElementById('btn-water-inlet-off');
const $wiToast      = document.getElementById('water-inlet-watchdog-toast');
const $wiToastText  = document.getElementById('water-inlet-watchdog-text');
const $wiToastDismiss = document.getElementById('btn-water-inlet-watchdog-dismiss');

let wiState         = 'off';   // 'off' | 'on' | 'pending' | 'error'
let wiOpenStartMs   = 0;
let wiAutoOffTimer  = null;
let wiTickTimer     = null;
let wiPendingAction = null;   // 'on' | 'off' — what we're waiting for OK on

function wiApplyStateClass() {
    if (!$wiDot) return;
    $wiDot.classList.remove('water-inlet-dot-off', 'water-inlet-dot-on',
                             'water-inlet-dot-pending', 'water-inlet-dot-error');
    $wiDot.classList.add('water-inlet-dot-' + wiState);
    $wiState.textContent = {
        off:     'OFF',
        on:      'ON',
        pending: '...',
        error:   'ERROR',
    }[wiState] || 'OFF';
}

function wiStartTickLoop() {
    if (wiTickTimer) return;
    wiTickTimer = setInterval(() => {
        if (wiState !== 'on') return;
        const elapsedSec = Math.floor((Date.now() - wiOpenStartMs) / 1000);
        const remainingSec = Math.max(0, WATER_INLET_AUTO_OFF_SEC - elapsedSec);
        $wiElapsed.textContent = elapsedSec + 's';
        $wiCountdown.textContent = 'auto-OFF in ' + remainingSec + 's';
    }, 250);
}
function wiStopTickLoop() {
    if (wiTickTimer) { clearInterval(wiTickTimer); wiTickTimer = null; }
}

function wiClearAutoOff() {
    if (wiAutoOffTimer) { clearTimeout(wiAutoOffTimer); wiAutoOffTimer = null; }
}

function wiSetOn() {
    wiState = 'on';
    wiOpenStartMs = Date.now();
    $wiTimers.hidden = false;
    wiApplyStateClass();
    wiClearAutoOff();
    wiStartTickLoop();
    // Auto-OFF after timeout
    wiAutoOffTimer = setTimeout(() => {
        logSys('water_inlet auto-OFF timer fired (' + WATER_INLET_AUTO_OFF_SEC + 's elapsed)');
        wiPendingAction = 'off';
        wiState = 'pending';
        wiApplyStateClass();
        send('washrobot', 'water_inlet off');
    }, WATER_INLET_AUTO_OFF_SEC * 1000);
}

function wiSetOff() {
    wiState = 'off';
    wiOpenStartMs = 0;
    $wiTimers.hidden = true;
    wiApplyStateClass();
    wiClearAutoOff();
    wiStopTickLoop();
}

function wiSetError(msg) {
    wiState = 'error';
    wiApplyStateClass();
    logSys('water_inlet ERROR: ' + (msg || 'unknown'));
    // Don't clear auto-off — if user had ON pending, the off timer may still
    // be valid (backend retry might recover). Stop tick (no valid open ts).
    wiStopTickLoop();
    $wiTimers.hidden = true;
}

if ($wiBtnOn) {
    $wiBtnOn.onclick = () => {
        wiPendingAction = 'on';
        wiState = 'pending';
        wiApplyStateClass();
        send('washrobot', 'water_inlet on');
    };
}
if ($wiBtnOff) {
    $wiBtnOff.onclick = () => {
        wiPendingAction = 'off';
        wiState = 'pending';
        wiApplyStateClass();
        send('washrobot', 'water_inlet off');
    };
}
if ($wiToastDismiss) {
    $wiToastDismiss.onclick = () => { $wiToast.hidden = true; };
}

function wiHandleReplyLine(line) {
    // Matches strictly to avoid colliding with other OK replies:
    //   "OK"                    — cmd_water_inlet success (cmd_water_inlet returns "OK\n", server strips \n)
    //   "ERR water_inlet_fail"  — set_water_inlet_ all 3 retries failed
    // Reply doesn't echo cmd, so we track pending via wiPendingAction. The
    // strict bare "OK" match avoids confusion with "OK state=...", "OK scripts=[...]",
    // "OK csv=...", etc.
    if (!wiPendingAction) return;
    if (line === 'OK') {
        if (wiPendingAction === 'on')  wiSetOn();
        if (wiPendingAction === 'off') wiSetOff();
        wiPendingAction = null;
    } else if (line.startsWith('ERR water_inlet_fail')) {
        wiSetError('backend close/open failed after 3 retries');
        wiPendingAction = null;
    }
}

function wiHandleWatchdogEvt(line) {
    // EVT water_inlet_watchdog_force_close open_sec=312
    const m = line.match(/^EVT water_inlet_watchdog_force_close open_sec=(\d+)/);
    if (!m) return;
    const sec = parseInt(m[1], 10);
    $wiToastText.textContent =
        '⚠ Watchdog 強制關閉進水球閥 — 已開啟 ' + sec + ' 秒 (> 5 分鐘上限)';
    $wiToast.hidden = false;
    // Reset GUI state to OFF (backend confirmed it closed via watchdog)
    wiSetOff();
}

// Initialize state display
wiApplyStateClass();

//=========== IMU panel ===========
// Parse roll=N pitch=N from any line. Both are relative to imu_roll0_ /
// imu_pitch0_ baselines captured at washrobot init.
//
// Color thresholds:
//   |angle| <= 2°  green  (well-balanced)
//   |angle| <= 5°  yellow (drift, monitor)
//   |angle| <= 10° orange (significant, may need correction)
//   |angle| >  10° red    (approaching IMU_ASK_DEG=15°, modal will pop)
//
// Bar visualisation: full-width bar with center marker; fill grows from
// center toward right (positive) or left (negative). Scale ±20° = full bar.
function parseImuValues(line) {
    const mr = line.match(/\broll=(-?\d+\.?\d*)/);
    const mp = line.match(/\bpitch=(-?\d+\.?\d*)/);
    if (mr) updateImuCell('imu-roll',  'imu-roll-bar',  parseFloat(mr[1]));
    if (mp) updateImuCell('imu-pitch', 'imu-pitch-bar', parseFloat(mp[1]));
}

function updateImuCell(cellId, barId, val) {
    const cell = document.getElementById(cellId);
    const bar  = document.getElementById(barId);
    if (!cell) return;
    cell.textContent = `${val.toFixed(2)} °`;
    cell.classList.remove('imu-green', 'imu-yellow', 'imu-orange', 'imu-red');
    const abs = Math.abs(val);
    if      (abs <=  2) cell.classList.add('imu-green');
    else if (abs <=  5) cell.classList.add('imu-yellow');
    else if (abs <= 10) cell.classList.add('imu-orange');
    else                cell.classList.add('imu-red');
    if (bar) {
        // ±20° = full half-bar width (50%). Clamp.
        const pct = Math.max(-50, Math.min(50, val / 20 * 50));
        if (pct >= 0) {
            bar.style.left  = '50%';
            bar.style.width = `${pct}%`;
        } else {
            bar.style.left  = `${50 + pct}%`;
            bar.style.width = `${-pct}%`;
        }
    }
}

//=========== balance calibration panel ===========
// EVTs from washrobot during calibration:
//   "EVT balance_cal phase=preload"
//   "EVT balance_cal phase=balancing"
//   "EVT balance_cal_iter <N> roll=<r> L=<l> R=<r>"
//   "EVT balance_cal_recorded offset=<X>"
function parseBalanceCalibration(line) {
    // Phase change
    const mp = line.match(/balance_cal\s+phase=(\S+)/);
    if (mp) {
        const phase = mp[1];
        document.getElementById('balcal-phase-text').textContent = `phase: ${phase}`;
        const badge = document.getElementById('balcal-state-badge');
        if (phase === 'idle' || phase === 'done') {
            badge.textContent = '⚪ idle';
            badge.classList.remove('link-ok', 'link-down');
        } else if (phase.startsWith('aborted')) {
            badge.textContent = '🔴 aborted';
            badge.classList.remove('link-ok'); badge.classList.add('link-down');
        } else if (phase === 'awaiting_record') {
            badge.textContent = '🟡 awaiting RECORD';
            badge.classList.remove('link-ok'); badge.classList.add('link-down');
        } else {
            badge.textContent = `🟢 ${phase}`;
            badge.classList.add('link-ok'); badge.classList.remove('link-down');
        }
        // [2026-06-02 v7] Awaiting RECORD reminder: pulse RECORD/ABORT + show banner
        // so user sees they MUST press one of these before doing other actions.
        // State machine remains strict (still ERR state_violation for ATTACH etc),
        // but UI makes the next required action obvious.
        const banner   = document.getElementById('balcal-awaiting-banner');
        const btnRec   = document.getElementById('btn-balcal-record');
        const btnAbort = document.getElementById('btn-balcal-abort');
        if (phase === 'awaiting_record') {
            if (banner)   banner.style.display = '';
            if (btnRec)   btnRec  .classList.add('balcal-pulse');
            if (btnAbort) btnAbort.classList.add('balcal-pulse');
        } else {
            if (banner)   banner.style.display = 'none';
            if (btnRec)   btnRec  .classList.remove('balcal-pulse');
            if (btnAbort) btnAbort.classList.remove('balcal-pulse');
        }
    }
    // Iter update with sensor values
    const mi = line.match(/balance_cal_iter\s+(\d+)\s+roll=(-?\d+\.?\d*)\s+L=(-?\d+\.?\d*)\s+R=(-?\d+\.?\d*)/);
    if (mi) {
        document.getElementById('balcal-iter').textContent       = mi[1];
        document.getElementById('balcal-roll').textContent       = `${parseFloat(mi[2]).toFixed(2)} °`;
        document.getElementById('balcal-tension-l').textContent  = `${parseFloat(mi[3]).toFixed(1)} kg`;
        document.getElementById('balcal-tension-r').textContent  = `${parseFloat(mi[4]).toFixed(1)} kg`;
    }
    // Recorded result
    const mr = line.match(/balance_cal_recorded\s+offset=(-?\d+\.?\d*)/);
    if (mr) {
        document.getElementById('balcal-last-offset').textContent = `${parseFloat(mr[1]).toFixed(2)} cm`;
        const t = new Date();
        document.getElementById('balcal-last-time').textContent  = `(${t.toLocaleString()})`;
    }
}

// Wire balance calibration buttons
(function initBalanceCalibration() {
    const btnStart  = document.getElementById('btn-balcal-start');
    const btnRecord = document.getElementById('btn-balcal-record');
    const btnAbort  = document.getElementById('btn-balcal-abort');
    if (btnStart)  btnStart .addEventListener('click', () => {
        if (!confirm('Phase 1-4 將自動跑：預載鋼索 → 釋放所有 cup → IMU 自動平衡。\n機體會掛在純鋼索上，第一次校正建議離地 50cm 以內。\n\n確定要開始？')) return;
        send('washrobot', 'balance_calibrate_start');
    });
    if (btnRecord) btnRecord.addEventListener('click', () => {
        send('washrobot', 'balance_calibrate_record');
    });
    if (btnAbort)  btnAbort .addEventListener('click', () => {
        send('washrobot', 'balance_calibrate_abort');
    });
})();

function onEasyCraneLine(line) {
    // Parse weight: "OK weight=-3.2 up=0 down=0 up_stop_kg=-20 weight_valid=1"
    const mw = line.match(/weight=(-?\d+\.?\d*)/);
    if (mw) {
        const el = document.getElementById('easy-weight');
        if (el) el.textContent = parseFloat(mw[1]).toFixed(2);
    }
    const ms = line.match(/up_stop_kg=(-?\d+\.?\d*)/);
    if (ms) {
        const el = document.getElementById('easy-up-stop-current');
        if (el) el.textContent = parseFloat(ms[1]).toFixed(2);
    }
    // Sync button state from server — one-way: only clear stale local claims when
    // server cleared the relay (safety tripped / external stop). We don't flip
    // local ON state from server ON, to avoid races right after a click.
    // UP=0 resets both easyUpActive (HOLD) and easyAutoActive (AUTO), since both
    // drive the same physical relay.
    const mu = line.match(/up=(\d)/);
    const md = line.match(/down=(\d)/);
    if (mu && md && typeof releaseAllEasyHolds !== 'undefined') {
        const serverUp   = mu[1] === '1';
        const serverDown = md[1] === '1';
        if (!serverUp) {
            if (easyUpActive)   { easyUpActive   = false; btnEasyUp.classList.remove('active');   }
            if (easyAutoActive) { easyAutoActive = false; btnEasyAuto.classList.remove('active'); }
        }
        if (!serverDown && easyDownActive) {
            easyDownActive = false;
            btnEasyDown.classList.remove('active');
        }
        updateEasyButtonLabels();
    }
    // Any safety EVT: locally release button state to match server-side all_off
    if (line.startsWith('EVT watchdog_timeout') ||
        line.startsWith('EVT weight_limit') ||
        line.startsWith('EVT weight_read_fail')) {
        releaseAllEasyHolds();
    }
}

//=========== composed commands ===========

function readStepCm() {
    const cm = parseInt(document.getElementById('step-cm').value, 10);
    if (!(cm >= 5 && cm <= 60)) {
        logSys(`step cm out of range: ${cm} (allowed 5-60)`);
        return null;
    }
    return cm;
}

document.getElementById('btn-step-down').onclick = () => {
    const cm = readStepCm();
    if (cm === null) return;
    send('washrobot', `step_down ${cm}`);
};

document.getElementById('btn-step-up').onclick = () => {
    const cm = readStepCm();
    if (cm === null) return;
    send('washrobot', `step_up ${cm}`);
};

document.getElementById('btn-step-up-sweep').onclick = () => {
    const cm = readStepCm();
    if (cm === null) return;
    send('washrobot', `step_up_with_sweep ${cm}`);
};

document.getElementById('btn-step-up-sweep-af').onclick = () => {
    const cm = readStepCm();
    if (cm === null) return;
    send('washrobot', `step_up_sweep_after_feet ${cm}`);
};

document.getElementById('btn-step-down-sweep-af').onclick = () => {
    const cm = readStepCm();
    if (cm === null) return;
    send('washrobot', `step_down_sweep_after_feet ${cm}`);
};

document.getElementById('btn-step-up-sweep-ba').onclick = () => {
    const cm = readStepCm();
    if (cm === null) return;
    send('washrobot', `step_up_sweep_ba ${cm}`);
};

document.getElementById('btn-step-down-sweep-ba').onclick = () => {
    const cm = readStepCm();
    if (cm === null) return;
    send('washrobot', `step_down_sweep_ba ${cm}`);
};

document.getElementById('btn-step-down-sweep').onclick = () => {
    const cm = readStepCm();
    if (cm === null) return;
    send('washrobot', `step_down_with_sweep ${cm}`);
};

// Cleaning arm DEPLOY — read wall_mm + slot, send "DEPLOY <mm> <slot>" DIRECTLY
// to motor_api (port 9527). Same wire format as washrobot's arm_deploy pass-through;
// we just skip the washrobot relay so this works when only arm is up.
(function(){
    const btn = document.getElementById('btn-arm-deploy');
    if (!btn) return;
    btn.onclick = () => {
        const mm   = parseInt(document.getElementById('arm-wall-mm').value, 10);
        const slot = document.getElementById('arm-slot').value;
        if (!Number.isFinite(mm) || mm <= 0) {
            alert('wall_mm must be a positive integer');
            return;
        }
        send('arm', `DEPLOY ${mm} ${slot}`);
    };
})();

// Cleaning arm CLEAN SWEEP — read wall_mm + rounds, send "arm_clean_sweep <mm> <rounds>".
// washrobot 端流程 (2026-05-20v 改):
//   Phase A: XKC 水位 sensor 補水 (60s timeout, 沒滿則開 CH7 球閥)
//   Phase B: arm INIT (~10s, M1+M2 calibrate)
//   Phase C × rounds: { LEFT 滾筒+水 sweep, RIGHT 刮刀乾 sweep }
//   Phase D: RAII PARK + 全 OFF
(function(){
    const btn = document.getElementById('btn-arm-clean-sweep');
    if (!btn) return;
    btn.onclick = () => {
        const mm     = parseInt(document.getElementById('arm-clean-wall-mm').value, 10);
        const rounds = parseInt(document.getElementById('arm-clean-rounds').value, 10);
        if (!Number.isFinite(mm) || mm <= 0) {
            alert('wall_mm must be a positive integer');
            return;
        }
        if (!Number.isFinite(rounds) || rounds < 1 || rounds > 20) {
            alert('rounds must be 1..20');
            return;
        }
        if (!confirm(`Start CLEAN SWEEP?\n\nwall_mm = ${mm}\nrounds = ${rounds}\n\nThis will:\n- Phase A: refill water tank to full (XKC sensor, 60s timeout)\n- Phase B: arm INIT (~10s)\n- Phase C × ${rounds}:\n    LEFT  → DEPLOY LEFT  → pump+brush ON  → 上滑台 sweep\n    RIGHT → DEPLOY RIGHT → pump+brush OFF → 上滑台 sweep\n- Phase D: PARK + all water/brush OFF (RAII)`)) return;
        send('washrobot', `arm_clean_sweep ${mm} ${rounds}`);
    };
})();

// Single-slave ZDT pusher manual control
document.getElementById('btn-zdt-extend').onclick = () => {
    const slave = document.getElementById('zdt-slave-select').value;
    send('washrobot', `zdt_pusher ${slave} extend`);
};
document.getElementById('btn-zdt-retract').onclick = () => {
    const slave = document.getElementById('zdt-slave-select').value;
    send('washrobot', `zdt_pusher ${slave} retract`);
};

document.getElementById('btn-run').onclick = () => {
    const n = parseInt(document.getElementById('run-steps').value, 10);
    if (!(n > 0)) return;
    const cm = readStepCm();
    if (cm === null) return;
    const dir = document.getElementById('run-direction').value;   // "down" | "up"
    send('washrobot', `run ${n} ${cm} ${dir}`);
};

// ↓ 走到地面（含清洗）— 從 crane-remaining 算 N 步、confirm、跑 run ... down_sweep_af
document.getElementById('btn-descend-to-ground').onclick = () => {
    const cm = readStepCm();
    if (cm === null) return;

    // 讀取網頁上既有的 crane-remaining 顯示值
    const remainingEl = document.getElementById('crane-remaining');
    const remainingTxt = remainingEl ? remainingEl.textContent.trim() : '';
    const remaining = parseInt(remainingTxt, 10);

    if (!Number.isFinite(remaining)) {
        alert('讀不到剩餘距離（crane-remaining）— 請確認 crane 已連線並執行過 zero_meters top');
        return;
    }
    if (remaining <= 0) {
        alert('已在地面或更低（剩餘距離 = ' + remaining + ' cm），無需 step_down');
        return;
    }

    const n = Math.ceil(remaining / cm);
    const totalCm = n * cm;
    const msg = `↓ 走到地面 + 連續清洗（跨 iter overlap）\n\n`
              + `剩餘距離: ${remaining} cm\n`
              + `步長: ${cm} cm\n`
              + `總步數: ${n} 步 × ${cm} cm = ${totalCm} cm\n\n`
              + `pipeline 模式：iter N 的 sweep 跟 iter N+1 的 step pre-feet 並行。\n`
              + `預估時間: 比 sequential 模式快 ~25%\n\n`
              + `確認開始?`;
    if (!confirm(msg)) return;

    send('washrobot', `run ${n} ${cm} down`);
};

// ====================================================================
// [2026-06-05] Scripted run — CSV of per-step cm values + optional
// per-step `n` no-sweep flag. C++ side: cmd_run_script in WASH_ROBOT.cpp.
// See .claude/scripted_run_plan.md for the full spec.
//
// Token grammar (mirror C++ parse_script_csv_):
//     <int>[n]['*'<count>]
// - <int>   : cm (5..50)
// - 'n'     : (optional) marks step as no-sweep (transit only)
// - '*<N>'  : (optional) repeat shorthand
// Default = sweep (matches 99% use case + preserves backward-compat).
// ====================================================================

// Returns
//   { ok: true, steps: [{cm, sweep}, ...], totalCm, nSweep, nTransit }
//   | { ok: false, err: '...' }
function parseScriptCsv(csv) {
    if (!csv || !csv.trim()) return { ok: false, err: '空 CSV' };
    const tokens = csv.split(',').map(t => t.replace(/\s+/g, '')).filter(Boolean);
    if (!tokens.length) return { ok: false, err: '沒有有效 token' };
    const steps = [];
    for (let i = 0; i < tokens.length; ++i) {
        const tok = tokens[i];

        // Peel '*<count>' first so we can inspect the cm part for trailing 'n'.
        let head = tok;
        let count = 1;
        const star = tok.indexOf('*');
        if (star >= 0) {
            head = tok.substring(0, star);
            const cntStr = tok.substring(star + 1);
            if (!/^-?\d+$/.test(cntStr)) {
                return { ok: false, err: `token #${i+1} repeat 格式錯誤: "${tok}"` };
            }
            count = parseInt(cntStr, 10);
        }

        // Peel optional trailing 'n' = no-sweep flag.
        let sweep = true;
        if (head && (head.endsWith('n') || head.endsWith('N'))) {
            sweep = false;
            head = head.slice(0, -1);
        }

        if (!head || !/^-?\d+$/.test(head)) {
            return { ok: false, err: `token #${i+1} 格式錯誤: "${tok}"` };
        }
        const cm = parseInt(head, 10);

        if (cm < 5 || cm > 50)
            return { ok: false, err: `token #${i+1} cm=${cm} 超出 5..50` };
        if (count < 1 || count > 1000)
            return { ok: false, err: `token #${i+1} count=${count} 超出 1..1000` };
        for (let k = 0; k < count; ++k) {
            steps.push({ cm, sweep });
            if (steps.length > 1000) return { ok: false, err: '總 step 數 > 1000' };
        }
    }
    if (!steps.length) return { ok: false, err: '展開後 0 步' };
    const totalCm   = steps.reduce((a, s) => a + s.cm, 0);
    const nSweep    = steps.filter(s => s.sweep).length;
    const nTransit  = steps.length - nSweep;
    return { ok: true, steps, totalCm, nSweep, nTransit };
}

const $scriptCsv      = document.getElementById('script-csv');
const $scriptPreview  = document.getElementById('script-preview-text');
const $scriptList     = document.getElementById('saved-scripts-list');
const $scriptProgRow  = document.getElementById('script-progress-row');
const $scriptProgText = document.getElementById('script-progress-text');
const $scriptProgCm   = document.getElementById('script-progress-cm');
const $scriptProgFill = document.getElementById('script-progress-fill');

function updateScriptPreview() {
    const csv = $scriptCsv.value;
    if (!csv.trim()) {
        $scriptPreview.textContent = '— 等待輸入 —';
        $scriptPreview.className = 'script-preview-dim';
        return;
    }
    const r = parseScriptCsv(csv);
    if (!r.ok) {
        $scriptPreview.textContent = '✗ ' + r.err;
        $scriptPreview.className = 'script-preview-err';
    } else {
        // Show breakdown when mixed; collapse when all-sweep (the common case).
        const mix = r.nTransit > 0
            ? ` (${r.nSweep} sweep + ${r.nTransit} transit)`
            : '';
        $scriptPreview.textContent =
            `✓ ${r.steps.length} 步${mix}，總 ${r.totalCm} cm`;
        $scriptPreview.className = 'script-preview-ok';
    }
}
$scriptCsv.addEventListener('input', updateScriptPreview);

document.getElementById('btn-run-script').onclick = () => {
    const csv = $scriptCsv.value.trim();
    if (!csv) { alert('CSV 是空的'); return; }
    const r = parseScriptCsv(csv);
    if (!r.ok) { alert('CSV 不正確：\n' + r.err); return; }
    const mix = r.nTransit > 0
        ? ` (${r.nSweep} sweep + ${r.nTransit} transit)`
        : '';
    const msg = `▶ Run Script\n\n`
              + `${r.steps.length} 步${mix}，總 ${r.totalCm} cm\n\n`
              + `CSV: ${csv}\n\n`
              + `確認開始?`;
    if (!confirm(msg)) return;
    // Server reads rest-of-line as CSV — spaces tolerated; we send as-is.
    send('washrobot', `run_script ${csv}`);
};

document.getElementById('btn-save-script').onclick = () => {
    const csv = $scriptCsv.value.trim();
    if (!csv) { alert('CSV 是空的'); return; }
    const r = parseScriptCsv(csv);
    if (!r.ok) { alert('CSV 不正確：\n' + r.err); return; }
    const name = prompt('Script 名稱（字母/數字/底線/橫線，最長 32）:');
    if (!name) return;
    if (!/^[A-Za-z0-9_-]{1,32}$/.test(name)) {
        alert('名稱不合法 — 只允許 [A-Za-z0-9_-]，最長 32 字元');
        return;
    }
    send('washrobot', `save_script ${name} ${csv}`);
    // Refresh after a short delay so the new row appears.
    setTimeout(() => send('washrobot', 'list_scripts'), 300);
};

document.getElementById('btn-list-scripts').onclick = () => {
    send('washrobot', 'list_scripts');
};

function renderSavedScripts(names) {
    if (!names.length) {
        $scriptList.innerHTML = '<span class="script-preview-dim">沒有已存 script</span>';
        return;
    }
    // Escape name for HTML — names are already validated [A-Za-z0-9_-] so this
    // is defense-in-depth.
    const esc = s => s.replace(/[&<>"']/g, c =>
        ({ '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;' }[c]));
    $scriptList.innerHTML = names.map(n => `
        <div class="saved-script-row">
            <span class="saved-script-name">${esc(n)}</span>
            <button class="saved-script-load"   data-name="${esc(n)}">Load</button>
            <button class="saved-script-run primary" data-name="${esc(n)}">▶ Run</button>
            <button class="saved-script-delete danger" data-name="${esc(n)}" title="delete">×</button>
        </div>
    `).join('');
    $scriptList.querySelectorAll('.saved-script-load').forEach(b => {
        b.onclick = () => send('washrobot', `load_script ${b.dataset.name}`);
    });
    $scriptList.querySelectorAll('.saved-script-run').forEach(b => {
        b.onclick = () => {
            if (!confirm(`執行 saved script "${b.dataset.name}"?`)) return;
            send('washrobot', `run_saved ${b.dataset.name}`);
        };
    });
    $scriptList.querySelectorAll('.saved-script-delete').forEach(b => {
        b.onclick = () => {
            if (!confirm(`刪除 "${b.dataset.name}"?`)) return;
            send('washrobot', `delete_script ${b.dataset.name}`);
            setTimeout(() => send('washrobot', 'list_scripts'), 300);
        };
    });
}

function showScriptProgress(step, total, cm, mode) {
    $scriptProgRow.hidden = false;
    $scriptProgText.textContent = step === 0
        ? `準備中 — 共 ${total} 步`
        : `Step ${step} / ${total}`;
    if (cm > 0) {
        const modeLbl = mode === 'transit' ? ' transit' : (mode === 'sweep' ? ' sweep' : '');
        $scriptProgCm.textContent = `(${cm} cm${modeLbl})`;
        $scriptProgCm.className = 'script-progress-cm' +
            (mode === 'transit' ? ' script-progress-mode-transit' : '');
    } else {
        $scriptProgCm.textContent = '';
        $scriptProgCm.className = 'script-progress-cm';
    }
    const pct = total > 0 ? Math.round((step / total) * 100) : 0;
    $scriptProgFill.style.width = pct + '%';
    $scriptProgFill.classList.remove('script-progress-fail');
}
function finishScriptProgress(ok) {
    $scriptProgText.textContent = ok ? '✓ Script 完成' : '✗ Script 失敗';
    if (!ok) $scriptProgFill.classList.add('script-progress-fail');
    // Auto-hide after 8s so the row doesn't linger.
    setTimeout(() => { $scriptProgRow.hidden = true; }, 8000);
}

document.getElementById('btn-dm2j-group').onclick = () => {
    const group = document.getElementById('dm2j-group').value;
    const cm = parseFloat(document.getElementById('dm2j-group-cm').value);
    if (isNaN(cm)) return;
    if (group === 'arm') {
        // arm = single slave, use existing cmd_move
        send('washrobot', `move arm ${cm}`);
    } else {
        // feet / wheels = group sync, use new cmd_dm2j_group
        send('washrobot', `dm2j_group ${group} ${cm}`);
    }
};

document.getElementById('btn-dm2j-zero').onclick = () => {
    const group = document.getElementById('dm2j-group').value;
    if (!confirm(`Set current position as zero for ${group}? This shifts the coordinate frame and cannot be auto-undone.`)) return;
    send('washrobot', `dm2j_zero ${group}`);
};

document.getElementById('btn-payout').onclick = () => {
    const cm = parseInt(document.getElementById('crane-cm').value, 10);
    if (!(cm > 0)) return;
    send('crane', `pay_out ${cm}`);
    disableBriefly(document.getElementById('btn-payout'), MOTION_DEBOUNCE_MS);
};

document.getElementById('btn-retract').onclick = () => {
    const cm = parseInt(document.getElementById('crane-cm').value, 10);
    if (!(cm > 0)) return;
    send('crane', `retract ${cm}`);
    disableBriefly(document.getElementById('btn-retract'), MOTION_DEBOUNCE_MS);
};

document.getElementById('btn-raw').onclick = () => {
    const tgt = document.getElementById('raw-tgt').value;
    const cmd = document.getElementById('raw-cmd').value.trim();
    if (!cmd) return;
    send(tgt, cmd);
    document.getElementById('raw-cmd').value = '';
};

document.getElementById('raw-cmd').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') document.getElementById('btn-raw').click();
});

//=========== Phase 6 return_home flow ===========

function requestHomeStatus(timeoutMs = 3000) {
    return new Promise((resolve) => {
        if (pendingHomeStatus) pendingHomeStatus.resolve(null); // cancel old
        const timeoutId = setTimeout(() => {
            if (pendingHomeStatus) {
                pendingHomeStatus = null;
                resolve(null);
            }
        }, timeoutMs);
        pendingHomeStatus = { resolve, timeoutId };
        send('crane', 'home_status');
    });
}

document.getElementById('btn-return-home').onclick = async () => {
    const remaining = await requestHomeStatus();
    if (remaining === null) {
        logSys('home_status 無回應或格式錯誤，取消召回');
        return;
    }
    if (remaining <= 0) {
        logSys(`home_status remaining=${remaining}，不需放繩（或尚未 zero_meters top）`);
        return;
    }
    showReturnModal(remaining);
};

document.getElementById('btn-return-confirm').onclick = () => {
    const cm = parseInt(modalReturn.dataset.cm, 10);
    hideReturnModal();
    if (cm > 0) send('washrobot', `return_home ${cm}`);
};
document.getElementById('btn-return-cancel').onclick = hideReturnModal;

function showReturnModal(cm) {
    modalReturn.dataset.cm = String(cm);
    document.getElementById('modal-return-cm').textContent = cm;
    modalReturn.classList.remove('modal-hidden');
}
function hideReturnModal() { modalReturn.classList.add('modal-hidden'); }

//=========== balance_ask modal ===========

document.getElementById('btn-balance-yes').onclick = () => {
    send('washrobot', 'confirm_balance yes');
    hideBalanceModal();
};
document.getElementById('btn-balance-no').onclick = () => {
    send('washrobot', 'confirm_balance no');
    hideBalanceModal();
};

function showBalanceModal(roll, pitch) {
    document.getElementById('modal-balance-roll').textContent  = roll;
    document.getElementById('modal-balance-pitch').textContent = pitch;
    modalBalance.classList.remove('modal-hidden');
}
function hideBalanceModal() { modalBalance.classList.add('modal-hidden'); }

// [2026-06-04] run_avoid obstacle modal
const modalObstacle = document.getElementById('modal-obstacle');
function showObstacleModal(action, stepCm, iter, reason) {
    document.getElementById('modal-obstacle-action').textContent = action;
    document.getElementById('modal-obstacle-step').textContent   = stepCm;
    document.getElementById('modal-obstacle-iter').textContent   = iter;
    document.getElementById('modal-obstacle-reason').textContent = reason || '(無原因說明)';
    modalObstacle.classList.remove('modal-hidden');
}
function hideObstacleModal() { modalObstacle.classList.add('modal-hidden'); }

// Wire run_avoid button + modal confirm/cancel
const btnRunAvoid = document.getElementById('btn-run-avoid');
if (btnRunAvoid) {
    btnRunAvoid.onclick = () => {
        if (!confirm(
            '開始 RUN with avoidance?\n\n' +
            '系統會在每個 step 前自動偵測障礙物、跳通知問你確認。\n' +
            '隨時可按 STOP (robot) 中斷。'
        )) return;
        send('washrobot', 'run_avoid');
    };
}
const btnObstacleConfirm = document.getElementById('btn-obstacle-confirm');
if (btnObstacleConfirm) {
    btnObstacleConfirm.onclick = () => {
        send('washrobot', 'obstacle_response 1');
        hideObstacleModal();
    };
}
const btnObstacleCancel = document.getElementById('btn-obstacle-cancel');
if (btnObstacleCancel) {
    btnObstacleCancel.onclick = () => {
        send('washrobot', 'obstacle_response 0');
        hideObstacleModal();
    };
}

//=========== press-and-hold helper ===========

// Generic press-and-hold binding.
//   onPress(): called once on mousedown / touchstart
//   onRelease(): called once on mouseup / touchend / leave
//   onTick(ms): called every 100ms while held (for UI updates, heartbeat)
function bindHold(btn, onPress, onRelease, onTick) {
    let active = false;
    let tickTimer = null;
    let t0 = 0;

    function start(e) {
        if (e) e.preventDefault();
        if (active) return;
        active = true;
        btn.classList.add('active');
        t0 = Date.now();
        onPress();
        if (onTick) {
            tickTimer = setInterval(() => onTick(Date.now() - t0), 100);
        }
    }
    function stop(e) {
        if (e) e.preventDefault();
        if (!active) return;
        active = false;
        btn.classList.remove('active');
        if (tickTimer) { clearInterval(tickTimer); tickTimer = null; }
        onRelease();
    }

    btn.addEventListener('mousedown',   start);
    btn.addEventListener('touchstart',  start,  { passive: false });
    btn.addEventListener('mouseup',     stop);
    btn.addEventListener('mouseleave',  stop);
    btn.addEventListener('touchend',    stop);
    btn.addEventListener('touchcancel', stop);

    return { stop: () => stop(null), isActive: () => active };
}

//=========== emergency retract (crane, rescue mode) ===========

const btnEmergency = document.getElementById('btn-emergency');
bindHold(btnEmergency,
    () => {
        send('crane', 'retract_left on');
        send('crane', 'retract_right on');
    },
    () => {
        btnEmergency.textContent = '🆘 按住收繩';
        send('crane', 'retract_left off');
        send('crane', 'retract_right off');
        send('crane', 'stop');
    },
    (ms) => {
        btnEmergency.textContent = `🆘 收繩中… ${(ms / 1000).toFixed(1)}s（放開停止）`;
    }
);

//=========== easy crane buttons ===========
//
// Three buttons:
//   ↑ UP   — press-and-hold: `up on` on press, `up off` + `stop` on release
//   ↓ DOWN — press-and-hold: `down on` on press, `down off` + `stop` on release
//   🤖 AUTO — click toggle: click 1 starts `up on` and lets server auto-stop when
//     weight < up_stop_kg (server-side weight_loop issues all_off + EVT weight_limit);
//     click 2 = manual cancel. EVT weight_limit / watchdog_timeout / weight_read_fail
//     all reset AUTO button state via releaseAllEasyHolds().
//
// Server is authoritative on all three buttons — status poll (every 50ms) parses
// up=/down= and resets stale local state if a safety tripped between clicks.

const btnEasyAuto = document.getElementById('btn-easy-auto');
const btnEasyUp   = document.getElementById('btn-easy-up');
const btnEasyDown = document.getElementById('btn-easy-down');

let easyUpActive   = false;   // held via UP button (HOLD)
let easyDownActive = false;   // held via DOWN button (HOLD)
let easyAutoActive = false;   // started via AUTO button (click toggle)

function updateEasyButtonLabels() {
    btnEasyUp.textContent   = easyUpActive   ? '↑ 拉繩中…'         : '↑ 拉繩（按住）';
    btnEasyDown.textContent = easyDownActive ? '↓ 釋放繩中…'       : '↓ 釋放繩（按住）';
    btnEasyAuto.textContent = easyAutoActive ? '🤖 AUTO 拉繩中…（點擊停止）' : '🤖 AUTO 拉到上限（點擊）';
}

// Press-and-hold UP
function easyStartUpHold() {
    if (easyUpActive) return;
    easyUpActive = true;
    btnEasyUp.classList.add('active');
    send('easy_crane', 'up on');
    updateEasyButtonLabels();
}
function easyStopUpHold() {
    if (!easyUpActive) return;
    easyUpActive = false;
    btnEasyUp.classList.remove('active');
    send('easy_crane', 'up off');
    send('easy_crane', 'stop');
    updateEasyButtonLabels();
}

// Press-and-hold DOWN
function easyStartDownHold() {
    if (easyDownActive) return;
    easyDownActive = true;
    btnEasyDown.classList.add('active');
    send('easy_crane', 'down on');
    updateEasyButtonLabels();
}
function easyStopDownHold() {
    if (!easyDownActive) return;
    easyDownActive = false;
    btnEasyDown.classList.remove('active');
    send('easy_crane', 'down off');
    send('easy_crane', 'stop');
    updateEasyButtonLabels();
}

// AUTO toggle — click to start UP motion, server auto-stops at weight threshold
function easyStartAuto() {
    if (easyAutoActive) return;
    easyAutoActive = true;
    btnEasyAuto.classList.add('active');
    send('easy_crane', 'up on');
    updateEasyButtonLabels();
}
function easyStopAuto() {
    if (!easyAutoActive) return;
    easyAutoActive = false;
    btnEasyAuto.classList.remove('active');
    send('easy_crane', 'up off');
    send('easy_crane', 'stop');
    updateEasyButtonLabels();
}

// Called on EVT weight_limit / watchdog_timeout / weight_read_fail — server already
// did all_off; this just resyncs client-side button state.
function releaseAllEasyHolds() {
    if (easyUpActive)   { easyUpActive   = false; btnEasyUp.classList.remove('active'); }
    if (easyDownActive) { easyDownActive = false; btnEasyDown.classList.remove('active'); }
    if (easyAutoActive) { easyAutoActive = false; btnEasyAuto.classList.remove('active'); }
    updateEasyButtonLabels();
}

btnEasyAuto.addEventListener('click', () => {
    easyAutoActive ? easyStopAuto() : easyStartAuto();
});

// UP/DOWN event wiring — pure press-and-hold.
function onUpPress(e)     { if (e) e.preventDefault(); easyStartUpHold(); }
function onUpRelease(e)   { if (e) e.preventDefault(); easyStopUpHold(); }
function onDownPress(e)   { if (e) e.preventDefault(); easyStartDownHold(); }
function onDownRelease(e) { if (e) e.preventDefault(); easyStopDownHold(); }

btnEasyUp.addEventListener('mousedown',    onUpPress);
btnEasyUp.addEventListener('touchstart',   onUpPress,   { passive: false });
btnEasyUp.addEventListener('mouseup',      onUpRelease);
btnEasyUp.addEventListener('mouseleave',   onUpRelease);
btnEasyUp.addEventListener('touchend',     onUpRelease);
btnEasyUp.addEventListener('touchcancel',  onUpRelease);

btnEasyDown.addEventListener('mousedown',   onDownPress);
btnEasyDown.addEventListener('touchstart',  onDownPress,  { passive: false });
btnEasyDown.addEventListener('mouseup',     onDownRelease);
btnEasyDown.addEventListener('mouseleave',  onDownRelease);
btnEasyDown.addEventListener('touchend',    onDownRelease);
btnEasyDown.addEventListener('touchcancel', onDownRelease);

updateEasyButtonLabels();

// Auto-poll easy_crane weight every 50ms for live display (only when connected).
// Uses silent send + muted log on the reply path to avoid flooding the log panel.
// Backend cost is minimal — cmd_status reads atomics, weight_loop itself already
// polls DY500 at WEIGHT_POLL_MS; status poll just returns the cached value.
function sendSilent(target, cmd) {
    if (!ws || ws.readyState !== 1) return;
    ws.send(JSON.stringify({ target, cmd }));
}
setInterval(() => {
    if (lastStatus.easy_crane) sendSilent('easy_crane', 'status');
}, 50);

// UP stop threshold — live input, no set button.
// Debounce 150ms so typing "-5" doesn't send intermediate "-" as an invalid value.
(function () {
    const input = document.getElementById('easy-up-stop-input');
    let timer = null;
    input.addEventListener('input', () => {
        clearTimeout(timer);
        timer = setTimeout(() => {
            const v = parseFloat(input.value);
            if (isNaN(v)) return;
            send('easy_crane', `set_up_stop_kg ${v}`);
        }, 150);
    });
})();

//=========== crane hold-to-pull (real crane) ===========

// Local mirror of server flags (kept in sync via onCraneLine status parse)
const craneHoldState = {
    up_left:    false,
    up_right:   false,
    down_left:  false,
    down_right: false,
};

// Generic hold-button wiring helper.
//   btnId    : DOM id of the button
//   onCmd    : crane cmd to send on press (e.g., "up_left on")
//   offCmd   : crane cmd to send on release (e.g., "up_left off")
function wireCraneHold(btnId, onCmd, offCmd) {
    const btn = document.getElementById(btnId);
    if (!btn) return;
    let pressed = false;

    function press(e) {
        if (e) e.preventDefault();
        if (pressed) return;
        pressed = true;
        btn.classList.add('active');
        send('crane', onCmd);
    }
    function release(e) {
        if (e) e.preventDefault();
        if (!pressed) return;
        pressed = false;
        btn.classList.remove('active');
        send('crane', offCmd);
    }

    btn.addEventListener('mousedown',   press);
    btn.addEventListener('touchstart',  press,  { passive: false });
    btn.addEventListener('mouseup',     release);
    btn.addEventListener('mouseleave',  release);
    btn.addEventListener('touchend',    release);
    btn.addEventListener('touchcancel', release);
}

// Combined ↑/↓ (both ropes) + per-side
wireCraneHold('btn-crane-up',         'up on',         'up off');
wireCraneHold('btn-crane-down',       'down on',       'down off');
wireCraneHold('btn-crane-up-left',    'up_left on',    'up_left off');
wireCraneHold('btn-crane-up-right',   'up_right on',   'up_right off');
wireCraneHold('btn-crane-down-left',  'down_left on',  'down_left off');
wireCraneHold('btn-crane-down-right', 'down_right on', 'down_right off');

// Auto-poll crane status every 200ms when crane is connected, to refresh
// tension display + sync hold flag state. Lower freq than easy_crane (50ms)
// because real crane has more devices on shared cli_30 — keep bus load light.
setInterval(() => {
    if (lastStatus.crane) sendSilent('crane', 'status');
}, 200);

// [2026-06-01] Auto-poll washrobot status every 500ms when connected, to
// refresh IMU panel (roll/pitch) + vacuum readings live. Slower than crane
// because washrobot status is heavier (reads all 9 JC100 pressures + IMU).
setInterval(() => {
    if (lastStatus.washrobot) sendSilent('washrobot', 'status');
}, 500);

// Tension threshold inputs — live, debounced 150ms.
// up_stop_total = hold-mode 收繩 L+R 總和；tension_max / tension_diff = motion_rope。
(function () {
    const wire = (inputId, cmd) => {
        const input = document.getElementById(inputId);
        if (!input) return;
        let timer = null;
        input.addEventListener('input', () => {
            clearTimeout(timer);
            timer = setTimeout(() => {
                const v = parseFloat(input.value);
                if (isNaN(v) || v <= 0) return;
                send('crane', `${cmd} ${v}`);
            }, 150);
        });
    };
    wire('crane-up-stop-total-input',       'set_up_stop_total_kg');
    wire('crane-tension-max-input',         'set_tension_max_kg');
    wire('crane-tension-diff-input',        'set_tension_diff_max_kg');
    wire('crane-retract-tension-stop-input','set_retract_tension_stop_kg');
})();

// Frequency inputs — live, debounced 200ms. Server validates against MAX
// and only updates atomic on success; mid-motion change applies on next
// motor-start cmd.
function wireFreqInput(inputId, cmd) {
    const input = document.getElementById(inputId);
    if (!input) return;
    let timer = null;
    input.addEventListener('input', () => {
        clearTimeout(timer);
        timer = setTimeout(() => {
            const v = parseFloat(input.value);
            if (isNaN(v) || v <= 0) return;
            send('crane', `${cmd} ${v}`);
        }, 200);
    });
}
wireFreqInput('crane-hold-hz-input',   'set_hold_hz');
wireFreqInput('crane-motion-hz-input', 'set_motion_hz');
wireFreqInput('crane-middle-hz-input', 'set_middle_hz');

// DSZL scale inputs — same debounce pattern as freq inputs. Negative values
// allowed (default -0.01 sign-flips the wiring-inverted raw reading).
function wireScaleInput(inputId, side) {
    const input = document.getElementById(inputId);
    if (!input) return;
    let timer = null;
    input.addEventListener('input', () => {
        clearTimeout(timer);
        timer = setTimeout(() => {
            const v = parseFloat(input.value);
            if (isNaN(v) || v === 0) return;
            send('crane', `set_dsz_scale ${side} ${v}`);
        }, 200);
    });
}
wireScaleInput('crane-dsz-left-scale-input',  'left');
wireScaleInput('crane-dsz-right-scale-input', 'right');

//=========== camera streaming ===========
//
// MJPEG stream attached via <img src="/mjpeg/cam1"> in HTML. Browser keeps the
// connection open and renders each multipart JPEG part. We wire two extra
// behaviours here:
//   1) onerror → show offline overlay, schedule reconnect (cache-busted URL)
//   2) snapshot button → fetch /snap/:id and trigger a file download
//
// Reconnect strategy: 3s delay + Date.now() suffix to force a fresh request
// (browser caches MJPEG connections aggressively otherwise).

function wireCamera(camId) {
    const dock    = document.querySelector(`.cam-cell[data-cam-id="${camId}"]`);
    if (!dock) return;
    const img     = document.getElementById(`${camId}-stream`);
    const offline = document.getElementById(`${camId}-offline`);
    const status  = document.getElementById(`${camId}-status`);
    const snapBtn = document.getElementById(`${camId}-snap`);
    const reload  = document.getElementById(`${camId}-reload`);
    if (!img) return;

    let reconnectTimer = null;

    function setStatus(text, cls) {
        if (!status) return;
        status.textContent = text;
        status.classList.remove('live', 'offline');
        if (cls) status.classList.add(cls);
    }

    function reloadStream() {
        if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
        offline.classList.remove('visible');
        setStatus('streaming', 'live');
        img.src = `/mjpeg/${camId}?t=${Date.now()}`;
    }

    img.addEventListener('load',  () => {
        offline.classList.remove('visible');
        setStatus('streaming', 'live');
    });
    img.addEventListener('error', () => {
        offline.classList.add('visible');
        setStatus('offline', 'offline');
        if (reconnectTimer) return;
        reconnectTimer = setTimeout(() => {
            reconnectTimer = null;
            reloadStream();
        }, 3000);
    });

    if (reload) reload.addEventListener('click', reloadStream);

    if (snapBtn) snapBtn.addEventListener('click', async () => {
        const orig = snapBtn.textContent;
        snapBtn.disabled = true;
        snapBtn.textContent = '📸 取得中…';
        try {
            const r = await fetch(`/snap/${camId}?t=${Date.now()}`);
            if (!r.ok) throw new Error(`HTTP ${r.status}`);
            const blob = await r.blob();
            const url  = URL.createObjectURL(blob);
            const ts   = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
            const a    = document.createElement('a');
            a.href = url;
            a.download = `${camId}_${ts}.jpg`;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
            snapBtn.textContent = '✓ 已下載';
            setTimeout(() => { snapBtn.textContent = orig; }, 1500);
        } catch (e) {
            console.error(`[${camId}] snapshot failed:`, e);
            snapBtn.textContent = '✗ 失敗';
            setTimeout(() => { snapBtn.textContent = orig; }, 1500);
        } finally {
            snapBtn.disabled = false;
        }
    });

    setStatus('streaming', 'live');
}
wireCamera('cam1');
wireCamera('cam2');
// To add more: wireCamera('cam3'); wireCamera('cam4');

//=========== page navigation (left sidebar) ===========
//
// Panels carry data-page="home|manual|camera"; CSS hides the ones not on the
// active page (driven by <body data-page>). Buttons just switch that attribute.
// Last-selected page is remembered in localStorage.
(function initPageNav() {
    const PAGES   = ['home', 'manual', 'camera', 'settings'];
    const buttons = document.querySelectorAll('#sidebar .nav-btn');

    function setPage(page) {
        if (!PAGES.includes(page)) page = 'home';
        document.body.dataset.page = page;
        buttons.forEach(b => b.classList.toggle('active', b.dataset.page === page));
        try { localStorage.setItem('wr_active_page', page); } catch (e) {}
    }

    buttons.forEach(b => b.addEventListener('click', () => setPage(b.dataset.page)));

    let saved = 'home';
    try { saved = localStorage.getItem('wr_active_page') || 'home'; } catch (e) {}
    setPage(saved);
})();

//=========== start ===========

// Initial paint of crane busy banner + button disable layer. Must run AFTER
// craneHoldState const is declared (line ~940) — calling earlier hits TDZ
// ReferenceError and halts the whole script (3 dots stay red, WS never opens).
updateCraneButtonStates();

//=========== Settings page handlers ===========
// Wire the Load / Apply All / Save buttons + per-input dirty highlighting.
// Replies from washrobot:
//   get_settings → "OK key=current:default key=current:default ..."
//   set_setting  → "OK key=value" or "ERR ..."
//   save_settings → "OK settings_saved settings.json" or "ERR ..."
(function initSettingsPage() {
    const inputs = document.querySelectorAll('.settings-grid input[data-setting]');
    const statusEl = document.getElementById('settings-status');

    // Track values applied/loaded so we can highlight dirty inputs.
    const lastApplied = {};

    function flashStatus(msg, ok = true) {
        if (!statusEl) return;
        statusEl.textContent = msg;
        statusEl.style.color = ok ? 'var(--cyan)' : 'var(--danger, #ff7777)';
    }

    function markDirty(input) {
        const key = input.dataset.setting;
        const isDirty = (lastApplied[key] !== undefined)
                     && (String(input.value) !== String(lastApplied[key]));
        input.classList.toggle('dirty', isDirty);
    }

    inputs.forEach(i => i.addEventListener('input', () => markDirty(i)));

    // Hook into the existing line parser so we catch "OK <k>=<v>:<d> ..." replies
    // and populate inputs. Mounting a fresh listener on the same hidden bus is
    // simpler than threading callbacks through send().
    const origLogRx = window.logRx;
    window.handleSettingsReply = function (line) {
        // get_settings format: "OK key1=cur1:def1 key2=cur2:def2 ..."
        if (!line.startsWith('OK')) return;
        const pairs = line.matchAll(/(\w+)=([-\d.]+):([-\d.]+)/g);
        let found = 0;
        for (const m of pairs) {
            const key = m[1], cur = m[2], def = m[3];
            const input = document.querySelector(`input[data-setting="${key}"]`);
            if (input) {
                input.value = cur;
                lastApplied[key] = cur;
                input.classList.remove('dirty');
                ++found;
            }
            const defEl = document.querySelector(`.default[data-default-for="${key}"]`);
            if (defEl) defEl.textContent = `(default: ${def})`;
        }
        if (found > 0) flashStatus(`Loaded ${found} setting(s) from washrobot`);
    };

    document.getElementById('btn-settings-load')?.addEventListener('click', () => {
        send('washrobot', 'get_settings');
        flashStatus('requesting...', true);
    });

    document.getElementById('btn-settings-apply-all')?.addEventListener('click', async () => {
        let applied = 0, failed = 0;
        for (const input of inputs) {
            const key = input.dataset.setting;
            const val = input.value;
            if (val === '' || val === lastApplied[key]) continue;   // skip unchanged
            send('washrobot', `set_setting ${key} ${val}`);
            lastApplied[key] = val;
            input.classList.remove('dirty');
            ++applied;
            // Small spacing between cmds — washrobot processes sequentially.
            await new Promise(r => setTimeout(r, 50));
        }
        flashStatus(`Applied ${applied} setting(s) (errors in log if any)`);
    });

    document.getElementById('btn-settings-save')?.addEventListener('click', () => {
        send('washrobot', 'save_settings');
        flashStatus('saving to settings.json...', true);
    });

    // Auto-load on first Settings page open. Tracks last-page via localStorage
    // so the load fires when the user actually navigates there, not at boot.
    let didAutoLoad = false;
    const observer = new MutationObserver(() => {
        if (!didAutoLoad && document.body.dataset.page === 'settings') {
            didAutoLoad = true;
            send('washrobot', 'get_settings');
        }
    });
    observer.observe(document.body, { attributes: true, attributeFilter: ['data-page'] });
})();

connectWs();
