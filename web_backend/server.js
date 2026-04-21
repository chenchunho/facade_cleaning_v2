// ============================================================================
// washrobot web_backend
//
// Bridges browser WebSocket clients to three C++ TCP command servers:
//   washrobot  @ 192.168.1.100:5001
//   crane      @ 192.168.1.101:5002
//   easy_crane @ 192.168.5.26:5003   (independent simple crane)
//
// Browser ↔ backend protocol (JSON over WebSocket):
//   → {target: "washrobot"|"crane"|"easy_crane", cmd: "<line>"}   send command
//   ← {src:    "washrobot"|"crane"|"easy_crane", line: "OK ..."} reply / EVT
//   ← {src: "status", washrobot: bool, crane: bool, easy_crane: bool}   connection state
// ============================================================================

const express = require('express');
const http = require('http');
const net = require('net');
const path = require('path');
const { WebSocketServer } = require('ws');

//=========== config ===========

const HTTP_PORT       = process.env.HTTP_PORT    || 8080;
const WASHROBOT_IP    = process.env.WROBOT_IP    || '192.168.1.100';
const WASHROBOT_PORT  = 5001;
const CRANE_IP        = process.env.CRANE_IP     || '192.168.1.101';
const CRANE_PORT      = 5002;
const EASY_CRANE_IP   = process.env.EASY_CRANE_IP || '192.168.5.26';
const EASY_CRANE_PORT = 5003;

const RECONNECT_MS = 3000;

//=========== app + http ===========

const app = express();
app.use(express.static(path.join(__dirname, 'public')));
app.get('/health', (_req, res) => res.json({ ok: true }));
const server = http.createServer(app);

//=========== ws ===========

const wss = new WebSocketServer({ server });

function broadcast(obj) {
    const s = JSON.stringify(obj);
    wss.clients.forEach(c => {
        if (c.readyState === 1) c.send(s);
    });
}

//=========== TCP bridge ===========

function makeBridge(name, ip, port) {
    const state = { sock: null, connected: false, buf: '' };

    function connect() {
        const sock = new net.Socket();
        state.sock = sock;

        sock.setNoDelay(true);
        sock.connect(port, ip, () => {
            state.connected = true;
            console.log(`[${name}] connected ${ip}:${port}`);
            broadcastStatus();
        });

        sock.on('data', (chunk) => {
            state.buf += chunk.toString('utf8');
            let idx;
            while ((idx = state.buf.indexOf('\n')) !== -1) {
                let line = state.buf.slice(0, idx);
                state.buf = state.buf.slice(idx + 1);
                if (line.endsWith('\r')) line = line.slice(0, -1);
                if (!line) continue;
                broadcast({ src: name, line });
            }
        });

        const onClose = (why) => {
            if (state.connected) console.log(`[${name}] disconnect (${why})`);
            state.connected = false;
            broadcastStatus();
            setTimeout(connect, RECONNECT_MS);
        };
        sock.on('error', (err) => onClose(err.message));
        sock.on('close', () => onClose('close'));
    }

    function send(cmd) {
        if (!state.connected || !state.sock) return false;
        const line = cmd.endsWith('\n') ? cmd : cmd + '\n';
        try {
            state.sock.write(line);
            return true;
        } catch (e) {
            return false;
        }
    }

    connect();
    return { name, send, isConnected: () => state.connected };
}

const washrobot  = makeBridge('washrobot',  WASHROBOT_IP,  WASHROBOT_PORT);
const crane      = makeBridge('crane',      CRANE_IP,      CRANE_PORT);
const easy_crane = makeBridge('easy_crane', EASY_CRANE_IP, EASY_CRANE_PORT);

function broadcastStatus() {
    broadcast({
        src: 'status',
        washrobot:  washrobot.isConnected(),
        crane:      crane.isConnected(),
        easy_crane: easy_crane.isConnected()
    });
}

//=========== ws event ===========

wss.on('connection', (ws) => {
    ws.send(JSON.stringify({
        src: 'status',
        washrobot:  washrobot.isConnected(),
        crane:      crane.isConnected(),
        easy_crane: easy_crane.isConnected()
    }));

    ws.on('message', (data) => {
        let msg;
        try { msg = JSON.parse(data.toString()); }
        catch { return ws.send(JSON.stringify({ src: 'error', line: 'invalid_json' })); }

        const target = msg.target === 'washrobot'  ? washrobot
                     : msg.target === 'crane'      ? crane
                     : msg.target === 'easy_crane' ? easy_crane
                     : null;
        if (!target) return ws.send(JSON.stringify({ src: 'error', line: 'unknown_target' }));
        if (typeof msg.cmd !== 'string' || !msg.cmd.length)
            return ws.send(JSON.stringify({ src: 'error', line: 'empty_cmd' }));

        if (!target.send(msg.cmd))
            ws.send(JSON.stringify({ src: 'error', line: `${target.name}_not_connected` }));
    });
});

//=========== start ===========

server.listen(HTTP_PORT, () => {
    console.log(`[web_backend] listening http://0.0.0.0:${HTTP_PORT}`);
    console.log(`[web_backend] washrobot  target = ${WASHROBOT_IP}:${WASHROBOT_PORT}`);
    console.log(`[web_backend] crane      target = ${CRANE_IP}:${CRANE_PORT}`);
    console.log(`[web_backend] easy_crane target = ${EASY_CRANE_IP}:${EASY_CRANE_PORT}`);
});
