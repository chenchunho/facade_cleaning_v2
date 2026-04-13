//=========== connection ===========

const logEl = document.getElementById('log');
const dotW  = document.getElementById('dot-washrobot');
const dotC  = document.getElementById('dot-crane');

let ws = null;

function connectWs() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    ws = new WebSocket(`${proto}://${location.host}`);

    ws.onopen  = () => logSys('ws connected');
    ws.onclose = () => {
        logSys('ws closed — retrying in 2s');
        setDot(dotW, false);
        setDot(dotC, false);
        setTimeout(connectWs, 2000);
    };
    ws.onerror = () => logSys('ws error');
    ws.onmessage = (e) => {
        let m;
        try { m = JSON.parse(e.data); } catch { return; }

        if (m.src === 'status') {
            setDot(dotW, m.washrobot);
            setDot(dotC, m.crane);
            return;
        }
        logRx(m.src, m.line);
    };
}

function setDot(el, ok) { el.classList.toggle('ok', !!ok); }

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

//=========== start ===========

connectWs();
