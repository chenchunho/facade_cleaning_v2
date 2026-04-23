//=========== dom ===========

const logEl        = document.getElementById('log');
const dotW         = document.getElementById('dot-washrobot');
const dotC         = document.getElementById('dot-crane');
const dotE         = document.getElementById('dot-easy-crane');
const bannerEl     = document.getElementById('banner');
const panelsRobot  = document.querySelectorAll('.panel-washrobot');
const panelsCrane  = document.querySelectorAll('.panel-crane');
const panelsEasy   = document.querySelectorAll('.panel-easy_crane');
const modalBalance = document.getElementById('modal-balance');
const modalReturn  = document.getElementById('modal-return');

let ws = null;
let lastStatus = { washrobot: null, crane: null, easy_crane: null }; // null = not yet known
let pendingHomeStatus = null; // { resolve, timeoutId }

//=========== connection ===========

function connectWs() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    ws = new WebSocket(`${proto}://${location.host}`);

    ws.onopen  = () => logSys('ws connected');
    ws.onclose = () => {
        logSys('ws closed — retrying in 2s');
        setDot(dotW, false);
        setDot(dotC, false);
        setDot(dotE, false);
        applyMode(false, false, false);
        setTimeout(connectWs, 2000);
    };
    ws.onerror = () => logSys('ws error');
    ws.onmessage = (e) => {
        let m;
        try { m = JSON.parse(e.data); } catch { return; }

        if (m.src === 'status') {
            setDot(dotW, m.washrobot);
            setDot(dotC, m.crane);
            setDot(dotE, m.easy_crane);
            handleStatusChange(!!m.washrobot, !!m.crane, !!m.easy_crane);
            return;
        }
        // Mute high-frequency easy_crane status poll replies to keep log clean;
        // manual refresh reply looks identical but losing one log line is OK UX.
        const isEasyPoll = m.src === 'easy_crane' &&
                           m.line.startsWith('OK weight=') &&
                           m.line.includes('up_stop_kg=');
        if (!isEasyPoll) logRx(m.src, m.line);

        if (m.src === 'washrobot')       onWashrobotLine(m.line);
        else if (m.src === 'crane')      onCraneLine(m.line);
        else if (m.src === 'easy_crane') onEasyCraneLine(m.line);
    };
}

function setDot(el, ok) { el.classList.toggle('ok', !!ok); }

//=========== degraded mode ===========

function handleStatusChange(wNew, cNew, eNew) {
    // Auto-stop surviving side when main pair transitions from "both up" to "one down"
    const wasBothUp = lastStatus.washrobot === true && lastStatus.crane === true;
    if (wasBothUp) {
        if (!wNew && cNew) {
            send('crane', 'stop');
            logSys('washrobot 失聯 → 自動送 stop 給 crane');
        } else if (!cNew && wNew) {
            send('washrobot', 'emergency_stop');
            logSys('crane 失聯 → 自動送 emergency_stop 給 washrobot');
        }
    }
    // Easy crane: if it drops while in motion, server-side watchdog will self-stop.
    // No cross-device action needed (independent subsystem).
    lastStatus = { washrobot: wNew, crane: cNew, easy_crane: eNew };
    applyMode(wNew, cNew, eNew);
}

function applyMode(w, c, e) {
    panelsRobot.forEach(p => p.classList.toggle('panel-disabled', !w));
    panelsCrane.forEach(p => p.classList.toggle('panel-disabled', !c));
    panelsEasy .forEach(p => p.classList.toggle('panel-disabled', !e));

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

// any button with data-tgt + data-cmd is a one-shot command
document.querySelectorAll('button[data-tgt][data-cmd]').forEach(btn => {
    btn.addEventListener('click', () => send(btn.dataset.tgt, btn.dataset.cmd));
});

//=========== device line handlers ===========

function onCraneLine(line) {
    if (pendingHomeStatus && line.startsWith('OK home_ground_cm=')) {
        const m = line.match(/remaining=(-?\d+)/);
        const remaining = m ? parseInt(m[1], 10) : null;
        clearTimeout(pendingHomeStatus.timeoutId);
        const resolver = pendingHomeStatus.resolve;
        pendingHomeStatus = null;
        resolver(remaining);
    }
}

function onWashrobotLine(line) {
    if (line.startsWith('EVT balance_ask')) {
        const r = line.match(/roll=(\S+)/);
        const p = line.match(/pitch=(\S+)/);
        showBalanceModal(r ? r[1] : '?', p ? p[1] : '?');
    }
}

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

document.getElementById('btn-run').onclick = () => {
    const n = parseInt(document.getElementById('run-steps').value, 10);
    if (!(n > 0)) return;
    send('washrobot', `run ${n}`);
};

document.getElementById('btn-move').onclick = () => {
    const motor = document.getElementById('move-motor').value;
    const cm = parseFloat(document.getElementById('move-cm').value);
    if (isNaN(cm)) return;
    send('washrobot', `move ${motor} ${cm}`);
};

document.getElementById('btn-payout').onclick = () => {
    const cm = parseInt(document.getElementById('crane-cm').value, 10);
    if (!(cm > 0)) return;
    send('crane', `pay_out ${cm}`);
};

document.getElementById('btn-retract').onclick = () => {
    const cm = parseInt(document.getElementById('crane-cm').value, 10);
    if (!(cm > 0)) return;
    send('crane', `retract ${cm}`);
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

//=========== start ===========

connectWs();
