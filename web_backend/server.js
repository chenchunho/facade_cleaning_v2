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

// Backend-driven keepalive for each TCP bridge.
// Reason: easy_crane is on a different subnet (.5.x) from the backend (.1.x), so the
// TCP session crosses a router/NAT that silently kills idle sessions after ~15-60 min.
// Without keepalive, backend never sees the drop until the next write fails. This
// is especially bad when the browser tab is backgrounded — setInterval throttles to
// 1s+ and eventually stops feeding the 50ms status poll, leaving the TCP idle.
//
// Belt + suspenders:
//   (1) OS-level TCP keepalive on every bridge socket (setKeepAlive)
//   (2) App-level `ping\n` every BRIDGE_PING_MS from backend itself (not dep. on browser)
const BRIDGE_PING_MS = 10000;

// WebSocket-level heartbeat (browser ↔ backend).
// Without this, a backgrounded/inactive tab can keep the ws open while its
// setInterval / setTimeout are throttled to 1s+ or frozen entirely — NAT or
// middlebox may silently drop the idle TCP under it, and neither side notices
// until the user tries to interact. By pinging at WS level and terminating
// any client that hasn't ponged, dead connections are cut within ~2 intervals.
const WS_PING_INTERVAL_MS = 30000;

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
    const state = { sock: null, connected: false, buf: '', reconnectTimer: null };

    function scheduleReconnect() {
        // Dedupe: Node sockets fire 'error' AND 'close' for the same failure, and without
        // this guard each failed connect would schedule 2 retries → exponential pile-up
        // of SYN-SENT sockets → OOM kill within minutes when a target is unreachable.
        if (state.reconnectTimer) return;
        state.reconnectTimer = setTimeout(() => {
            state.reconnectTimer = null;
            connect();
        }, RECONNECT_MS);
    }

    function connect() {
        // Destroy any lingering old socket to prevent fd leak on repeated failures.
        if (state.sock && !state.sock.destroyed) {
            try { state.sock.destroy(); } catch (e) {}
        }

        const sock = new net.Socket();
        state.sock = sock;
        state.buf = '';

        sock.setNoDelay(true);
        // OS-level TCP keepalive — probe after 30s idle, kills socket if peer unreachable.
        sock.setKeepAlive(true, 30000);

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

        // Swallow 'error' so it doesn't crash the process; 'close' always follows and
        // drives the reconnect. (Previously both events each scheduled a retry.)
        sock.on('error', (err) => {
            if (state.connected) console.log(`[${name}] error: ${err.message}`);
        });
        sock.on('close', () => {
            if (state.connected) console.log(`[${name}] disconnect`);
            state.connected = false;
            broadcastStatus();
            scheduleReconnect();
        });
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

    // App-level keepalive — send `ping\n` every BRIDGE_PING_MS regardless of browser
    // activity. Guarantees the NAT stays open and write-path failures surface fast.
    // ping is idempotent and supported by all three targets (washrobot / crane-shim / easy_crane).
    setInterval(() => { send('ping'); }, BRIDGE_PING_MS);

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
    ws.isAlive = true;
    ws.on('pong', () => { ws.isAlive = true; });

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

//=========== ws heartbeat ===========
// Every WS_PING_INTERVAL_MS send a ws ping to each connected client. The
// browser's ws implementation auto-replies with pong (transparent to app.js).
// If no pong arrives between two ticks, terminate the client so the browser's
// onclose handler fires and triggers app.js reconnect.
const wsHeartbeat = setInterval(() => {
    wss.clients.forEach((ws) => {
        if (ws.isAlive === false) {
            console.log('[ws] terminating unresponsive client');
            return ws.terminate();
        }
        ws.isAlive = false;
        try { ws.ping(); } catch (e) {}
    });
}, WS_PING_INTERVAL_MS);

wss.on('close', () => clearInterval(wsHeartbeat));

//=========== start ===========

server.listen(HTTP_PORT, () => {
    console.log(`[web_backend] listening http://0.0.0.0:${HTTP_PORT}`);
    console.log(`[web_backend] washrobot  target = ${WASHROBOT_IP}:${WASHROBOT_PORT}`);
    console.log(`[web_backend] crane      target = ${CRANE_IP}:${CRANE_PORT}`);
    console.log(`[web_backend] easy_crane target = ${EASY_CRANE_IP}:${EASY_CRANE_PORT}`);
});
