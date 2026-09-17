// ============================================================================
// washrobot web_backend
//
// Bridges browser WebSocket clients to C++ TCP command servers:
//   washrobot  @ 192.168.1.100:5001
//   crane      @ 192.168.1.10:5002
//
// Browser ↔ backend protocol (JSON over WebSocket):
//   → {target: "washrobot"|"crane"|"arm", cmd: "<line>"}   send command
//   ← {src:    "washrobot"|"crane"|"arm", line: "OK ..."} reply / EVT
//   ← {src: "status", washrobot: bool, crane: bool, arm: bool}   connection state
// ============================================================================

const express = require('express');
const http = require('http');
const net = require('net');
const path = require('path');
const { WebSocketServer } = require('ws');

//=========== config ===========

const HTTP_PORT       = process.env.HTTP_PORT    || 8080;
// [2026-09-03] 靜態目錄可由環境變數指定，讓 console v2 能以**獨立行程**跑在別的埠
// （8081）而不動到現行的 8080：兩邊各自持有自己的橋接連線，一邊掛掉不影響另一邊。
// 預設值與先前逐字相同 —— 不帶這個變數啟動時，行為完全沒變。
// [2026-09-16 per user] v2 console (`public/`, :8080, fcv-web.service) retired; v3 is the only console.
const PUBLIC_DIR      = process.env.PUBLIC_DIR   || path.join(__dirname, 'public_v3');
const WASHROBOT_IP    = process.env.WROBOT_IP    || '192.168.1.100';
const WASHROBOT_PORT  = 5001;
// [2026-08-28] 192.168.1.101 → 192.168.1.10. The crane Pi's wired address is
// .10; nothing has ever answered at .101 (verified: ping from the crane itself
// gets no reply). This is wrong regardless of wired-vs-WiFi, so it would still
// have failed after the two Pis are linked over eth.
// NOTE: WASHROBOT_IP above (.1.100) is CORRECT and must stay — the two Pis are
// simply not eth-linked yet on the bench, so override it with WROBOT_IP=<wifi>
// until they are.
const CRANE_IP        = process.env.CRANE_IP     || '192.168.1.10';
const CRANE_PORT      = 5002;
// cleaning_arm motor_api runs on washrobot Pi (same host as washrobot itself).
// target='arm' supports BOTH paths:
//   (1) direct: panel-arm sends raw motor_api commands (INIT/DEPLOY/PARK/STATUS)
//       so user can test arm alone when washrobot isn't running.
//   (2) washrobot-orchestrated: arm_attached / arm_clean_sweep still go through
//       washrobot, which internally relays to 127.0.0.1:9527.
// Both end up at the same motor_api process; just different control surfaces.
const ARM_IP          = process.env.ARM_IP       || WASHROBOT_IP;
const ARM_PORT        = 9527;

// [2026-08-27 per user] 攝影機反向代理整個移除 —— 「以後不用串接攝影機」。
// 原本這裡有 CAMERAS 對照表（cam1/cam2/depth/depth_live/depth_before/
// depth_after/depth_live_depth → washrobot Pi 的 5004/5005/5008），下面還有
// proxyToCam() 與 /snap/:cam_id、/mjpeg/:cam_id 兩條路由。前端的 Camera
// 分頁與 app.js 的 wireCamera/wireDepthCamera 也一併刪除。
// frame_capture.py 本身沒動；depth_cam_service.py 已於 2026-09-01 刪除。

const RECONNECT_MS = 1000;   // 2026-04-29: 3000 → 1000 配合前端 panel-disabled 3s debounce，瞬斷情境總計約 1s 即恢復、UI 不會 flicker

// Backend-driven keepalive for each TCP bridge.
// Reason: a bridge crossing a router/NAT can have its TCP session silently
// killed after an idle period. Without keepalive, backend never sees the drop
// until the next write fails. This is especially bad when the browser tab is
// backgrounded — setInterval throttles to 1s+ and eventually stops feeding the
// status poll, leaving the TCP idle.
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
app.use(express.static(PUBLIC_DIR));
app.get('/health', (_req, res) => res.json({ ok: true }));

const server = http.createServer(app);

//=========== 牆面高度（2026-09-11 per user，地面歸零慣例）===========
//
// 🔴 慣例：**地面最低點 = SD76 0**，升到頂端 length_left 為負，取絕對值 = 牆高。
//    「② 最高點設定」在頂端**純讀** |length_left| 存進來，**不歸零**（`zero_meters top`
//    會把 0 搬到頂端，與慣例相反 —— 2026-09-11 實機踩過一次、已撤回）。
// 為什麼放在 server.js 而不是瀏覽器：牆高是「這個工地」的值，平板與筆電要看到同一個；
//    也不放吊機的 home_ground_cm —— cycle_test.py 用 home_ground_cm>0 判斷「頂端歸零慣例」，
//    塞進去會讓腳本誤判慣例。存檔在 ~/run/wall_height.json，node 重啟不掉。
// full 需要 FCV_TOP_CM=牆高：從這裡帶進 env（cycle_test 拿不到時會落到預設 231 = 錯）。
const fs = require('fs');
const WALL_FILE = path.join(process.env.HOME || '/home/user', 'run', 'wall_height.json');
// [2026-09-11 per user] ① 地面歸零與 ② 最高點各自獨立、各自記憶：
//   ① 的「SD76=0」活在吊機硬體，這裡只記**上次成功的時間**（ground_at）讓 GUI 標示；
//   ② 的牆高（cm）在這裡。任務啟動要兩者都有；只重做其中一個，另一個沿用記憶值。
let wall = { cm: 0, at: null, left_raw: null, ground_at: null };
try {
    const w = JSON.parse(fs.readFileSync(WALL_FILE, 'utf8')) || {};
    wall = {
        cm:        (Number.isFinite(w.cm) && w.cm > 0) ? w.cm : 0,
        at:        w.at || null,
        left_raw:  (w.left_raw === undefined ? null : w.left_raw),
        ground_at: w.ground_at || null
    };
} catch (_) { /* 沒檔＝尚未建置 */ }
function wallSave() {
    try { fs.mkdirSync(path.dirname(WALL_FILE), { recursive: true }); fs.writeFileSync(WALL_FILE, JSON.stringify(wall)); }
    catch (e) { console.error('[wall] save failed', e && e.message); }
}
function wallMsg() { return { src: 'wall', cm: wall.cm, at: wall.at, left_raw: wall.left_raw, ground_at: wall.ground_at }; }

// [2026-09-15 per user] Mission 參數記憶:**上一次成功起跑的那組**就是下一次的預設值。
// 放 server 不放瀏覽器,理由同牆高:平板與筆電要看到同一組;node 重啟也不掉
// (~/run/mission_params.json)。只在 spawn 成功後寫,失敗的組合不會變成預設。
// 🔴 只存「人設定的參數」,不存 FCV_TOP_CM(那是牆高,另有來源)與 running 狀態。
const MPARAM_FILE = path.join(process.env.HOME || '/home/user', 'run', 'mission_params.json');
let missionDefaults = null;
try { missionDefaults = JSON.parse(fs.readFileSync(MPARAM_FILE, 'utf8')) || null; } catch (_) { /* 沒檔＝用內建預設 */ }
function missionDefaultsSave(p) {
    missionDefaults = p;
    try { fs.mkdirSync(path.dirname(MPARAM_FILE), { recursive: true }); fs.writeFileSync(MPARAM_FILE, JSON.stringify(p)); }
    catch (e) { console.error('[mission] defaults save failed', e && e.message); }
}

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
    // ping is idempotent and supported by washrobot / crane-shim.
    setInterval(() => { send('ping'); }, BRIDGE_PING_MS);

    connect();
    return { name, send, isConnected: () => state.connected };
}

const washrobot  = makeBridge('washrobot',  WASHROBOT_IP,  WASHROBOT_PORT);
const crane      = makeBridge('crane',      CRANE_IP,      CRANE_PORT);
// Connectivity-indicator only. cleaning_arm's motor_api doesn't accept `ping`
// (rejects with ERR), but that's OK — the TCP socket itself staying open is
// what isConnected() reports, and the ERR reply just gets broadcast on src=arm
// (visible in browser log, harmless).
const arm        = makeBridge('arm',        ARM_IP,        ARM_PORT);

// Second crane TCP connection dedicated to interrupt-class commands (stop /
// status / home_status / ping). Reason: crane server uses one handler thread
// per TCP connection, and motion_rope (pay_out / retract / align_lengths)
// blocks that thread for the full motion. With a single connection, `stop\n`
// sat in the socket buffer until motion ended — making STOP visibly broken.
// WashRobot has the same pattern (crane_cli_estop_) for exactly this reason.
// Both connections share src='crane' so the frontend treats replies uniformly.
const crane_intr = makeBridge('crane', CRANE_IP, CRANE_PORT);
const CRANE_INTR_FIRST_TOKENS = new Set(['stop', 'status', 'home_status', 'ping']);
function routeCrane(cmd) {
    const tok = cmd.trim().split(/\s+/)[0];
    if (CRANE_INTR_FIRST_TOKENS.has(tok)) {
        // Prefer intr connection; fall back to main if intr is down so user's
        // STOP attempt still reaches crane (will be queued behind motion_rope,
        // but better than silent fail).
        return crane_intr.isConnected() ? crane_intr : crane;
    }
    return crane;
}

function broadcastStatus() {
    broadcast({
        src: 'status',
        washrobot:  washrobot.isConnected(),
        crane:      crane.isConnected(),
        arm:        arm.isConnected()
    });
}

//=========== mission runner ===========
//
// 🔴🔴 **不要用 JS 重寫 `cycle_test.py`。** 那 948 行裡有連續超標判定、roll 救回的
//   最小改善量、座標慣例自動判定、起點拒跑 —— 重寫等於分岔出第二份，而分岔的那份
//   會退化。這裡只做一件事：**spawn 現有腳本、把 stdout 串回頁面。**
//   腳本與 web 都在吊機 Pi 上（腳本連 127.0.0.1:5002），不用跨機。

const { spawn } = require('child_process');

const MISSION_PY   = process.env.MISSION_PY  || path.join(__dirname, '..', 'scripts', 'cycle_test.py')   // [2026-09-17] Linux_test/ 目錄退役,腳本搬到 scripts/;
const MISSION_CWD  = process.env.MISSION_CWD || path.join(__dirname, '..');
const MISSION_RING = 3000;          // 保留最後 N 行，供重新連上的瀏覽器補看

const mission = {
    proc: null, args: null, env: null, startedAt: 0,
    // [2026-09-16] detachAfterExit：STOP 的第 ⑤ 步（收腳）要等 python 真的結束才能送，
    // 不然會跟腳本自己的 cleanup 在同一條 Modbus 匯流排上打架。旗標在 missionStop 設、
    // 在 proc close 消費。
    ring: [], exit: null, stopping: false, detachAfterExit: false
};

function missionSnapshot() {
    return {
        running:   !!mission.proc,
        paused:    !!mission.paused,     // [2026-09-15] script-mode pause (SIGUSR1/SIGUSR2, see missionPause)
        defaults:  missionDefaults,      // [2026-09-15] 上次成功起跑的參數 = 下次預設(GUI 開頁帶入)
        args:      mission.args,
        env:       mission.env,
        startedAt: mission.startedAt,
        exit:      mission.exit,
        stopping:  mission.stopping
    };
}

function missionSend(obj) { broadcast(Object.assign({ src: 'mission' }, obj)); }

function missionPush(line) {
    // [2026-09-15] 也寫進 node 的 stdout(→ ~/run/logs/web_*.log)。在此之前任務輸出**只存在於
    // WS ring 與瀏覽器**：從 GUI 起跑的那一趟,事後在 Pi 上完全查不到,而現場正是從 GUI 起跑的。
    console.log('[mission] ' + new Date().toISOString().slice(11, 19) + ' ' + line);
    mission.ring.push(line);
    if (mission.ring.length > MISSION_RING) mission.ring.shift();
    missionSend({ line });
}

// 數值驗證：型別 + 範圍都不過就回 null。**驗完才放進 argv**（硬要求 ②）。
function numArg(v, lo, hi, asInt) {
    const n = Number(v);
    if (!isFinite(n) || n < lo || n > hi) return null;
    return asInt ? String(Math.round(n)) : String(n);
}

function missionStart(p, reply) {
    // ④ 單一實例：已經在跑就拒絕，並回報正在跑的那組參數。
    if (mission.proc)
        return reply({ ok: false, err: 'already_running', running: missionSnapshot() });

    // 🔴 參數順序是 cycles steps step_cm **roll_trip diff_trip**
    //    （`cycle_test.py:67-69,103-104` 實查）。`mission_run.py` 剛好相反，
    //    兩個都是浮點數、接反不會報錯，只會讓門檻互換 —— 別接錯。
    const cycles = numArg(p.cycles,    1,   999, true);
    const steps  = numArg(p.steps,     1,    99, true);
    const stepCm = numArg(p.step_cm,   1,   200, true);
    const roll   = numArg(p.roll_trip, 0.5,  45, false);
    const diff   = numArg(p.diff_trip, 0.5, 100, false);
    // [2026-09-11 per user，同日反轉] 地面歸零慣例下 home_ground_cm 恆為 0，腳本拿不到
    // FCV_TOP_CM 會落到預設 231（錯）⇒ **牆高由 server 的 wall 儲存帶進去**，不由表單填。
    // 未建置就拒絕啟動，不讓腳本用錯高度往下衝（它的起點檢查會擋，但按了才知道）。
    const topCm  = (wall.cm > 0 && wall.ground_at) ? wall.cm : null;   // ① 與 ② 都要有
    const railCm = numArg(p.rail_cm,   0,   999, true);
    const bad = [];
    if (cycles === null) bad.push('cycles(1~999)');
    if (steps  === null) bad.push('steps(1~99)');
    if (stepCm === null) bad.push('step_cm(1~200)');
    if (roll   === null) bad.push('roll_trip(0.5~45)');
    if (diff   === null) bad.push('diff_trip(0.5~100)');
    if (railCm === null) bad.push('rail_cm(0~999)');
    if (bad.length) return reply({ ok: false, err: 'bad_params', detail: bad });
    if (topCm === null) return reply({ ok: false, err: 'wall_height_unset', detail: (wall.ground_at ? '' : '① 地面歸零未做；') + (wall.cm > 0 ? '' : '② 最高點未量') });

    const args = ['-u', MISSION_PY, cycles, steps, stepCm, roll, diff];
    // [2026-09-15 per user] cycle_test full 的 key=value 參數（位置不限）：fan=move[:pct]|all[:pct]、
    // rail=<起>-<迄>|off（同日由 0|100 升級；舊 0|100 腳本仍收）。只在帶了才 append；格式不對就拒絕，不讓腳本起跑後才炸。
    const kv = [];
    if (p.fan  !== undefined && p.fan  !== '') { if (/^(move|all)(:(5|6|7|8|9|10))?$/.test(String(p.fan)))  kv.push('fan='  + p.fan);  else bad.push('fan(move[:5-10]|all[:5-10])'); }
    if (p.rail !== undefined && p.rail !== '') {
        const r = String(p.rail);
        const m = /^(\d{1,3})-(\d{1,3})$/.exec(r);
        const okRail = r === 'off' || /^(0|100)$/.test(r) || (m && +m[1] <= 130 && +m[2] <= 130 && m[1] !== m[2]);
        if (okRail) kv.push('rail=' + r); else bad.push('rail(<起>-<迄> 0..130, 起≠迄 | off)');
    }
    // 壓力 / 乾掃走環境變數（腳本既有介面）：arm_nm 1~15、dry=1。
    const armNm = (p.arm_nm !== undefined && p.arm_nm !== '') ? numArg(p.arm_nm, 1, 15, false) : undefined;
    if (armNm === null) bad.push('arm_nm(1~15)');
    if (bad.length) return reply({ ok: false, err: 'bad_params', detail: bad });
    args.push(...kv);
    // ⚠️ `cycle_test.py:63` 的預設 host 是 **192.168.5.26**（本體 WiFi，2026-09-10 起已不通）
    //    ⇒ `FCV_WROBOT_HOST` 不是可選的，不帶就會連到一個不存在的位址。
    const env = Object.assign({}, process.env, {
        FCV_WROBOT_HOST: WASHROBOT_IP,
        FCV_TOP_CM:      topCm,       // = 牆高（② 最高點設定量到的 |length_left|）
        FCV_RAIL_CM:     railCm
    });
    if (armNm !== undefined) env.FCV_ARM_NM = armNm;
    if (p.dry === 1 || p.dry === '1' || p.dry === true) env.FCV_DRY = '1';

    let proc;
    try {
        proc = spawn('python3', args, { cwd: MISSION_CWD, env });   // ② 陣列形式，不組 shell 字串
    } catch (e) {
        return reply({ ok: false, err: 'spawn_failed', detail: String(e && e.message) });
    }

    mission.proc      = proc;
    mission.paused    = false;
    // 存成下次的預設(只有 spawn 成功才走到這裡)
    missionDefaultsSave({
        cycles: Number(cycles), steps: Number(steps), step_cm: Number(stepCm),
        roll_trip: Number(roll), diff_trip: Number(diff), rail_cm: Number(railCm),
        fan: (p.fan === undefined || p.fan === '') ? null : String(p.fan),
        rail: (p.rail === undefined || p.rail === '') ? null : String(p.rail),
        arm_nm: (armNm === undefined) ? null : Number(armNm),
        dry: (p.dry === 1 || p.dry === '1' || p.dry === true) ? 1 : 0,
        at: new Date().toISOString()
    });
    mission.args      = { cycles, steps, step_cm: stepCm, roll_trip: roll, diff_trip: diff,
                          top_cm: topCm, rail_cm: railCm, kv };
    mission.env       = { FCV_WROBOT_HOST: WASHROBOT_IP, FCV_TOP_CM: topCm, FCV_RAIL_CM: railCm,
                          FCV_ARM_NM: env.FCV_ARM_NM, FCV_DRY: env.FCV_DRY };
    mission.startedAt = Date.now();
    mission.ring      = [];
    mission.exit      = null;
    mission.stopping  = false;

    missionPush(`[web] spawn: python3 -u cycle_test.py ${cycles} ${steps} ${stepCm} ${roll} ${diff}${kv.length ? ' ' + kv.join(' ') : ''}`);
    missionPush(`[web] env: FCV_WROBOT_HOST=${WASHROBOT_IP} FCV_TOP_CM=${topCm}（牆高，server 儲存） FCV_RAIL_CM=${railCm}`);

    // stdout/stderr 合成同一條串流：進度印在 stdout、例外與 traceback 在 stderr，
    // 分開送的話出事時看到的會是斷開的兩半。
    let buf = '';
    const onData = (chunk) => {
        buf += chunk.toString();
        let i;
        while ((i = buf.indexOf('\n')) >= 0) {
            const line = buf.slice(0, i).replace(/\r$/, '');
            buf = buf.slice(i + 1);
            // [2026-09-15] 腳本在 pause_point 印 `[PAUSE] paused …` / `[PAUSE] resumed`，以它為權威
            if (/^\[PAUSE\] paused/.test(line))  { mission.paused = true;  missionSend({ state: missionSnapshot() }); }
            if (/^\[PAUSE\] resumed/.test(line)) { mission.paused = false; missionSend({ state: missionSnapshot() }); }
            missionPush(line);
        }
    };
    proc.stdout.on('data', onData);
    proc.stderr.on('data', onData);

    proc.on('close', (code, signal) => {
        if (buf.length) missionPush(buf);           // 最後一行沒有換行時不要吞掉
        const wasStopping = mission.detachAfterExit;
        mission.proc      = null;
        mission.paused    = false;
        mission.stopping  = false;
        mission.detachAfterExit = false;
        mission.exit      = { code, signal, at: Date.now() };
        missionPush(`[web] 行程結束 code=${code}${signal ? ' signal=' + signal : ''}`);
        missionSend({ state: missionSnapshot() });
        if (wasStopping) missionDetachFeet();
    });
    proc.on('error', (e) => { missionPush(`[web] 🔴 spawn error: ${e && e.message}`); });

    reply({ ok: true, running: missionSnapshot() });
    missionSend({ state: missionSnapshot() });
}

// ① 🔴🔴 **STOP 的四步順序 —— 這是安全語意，不是整潔問題。**
//
//   `cycle_test.py` 是 `try: … finally: cleanup()`，而 **`cleanup()` 第一件事就是
//   `arm_park`（手臂卸力）**。手臂壓玻璃是**持續施力**（實測 6~14 Nm 持續頂著），
//   達妙馬達長時間受力會觸發過熱／過流鎖存，**只能斷電解除**（2026-09-03 踩過）。
//
//   🔴 而腳本**沒有 `import signal`**（我實查過）⇒ 沒有自訂 handler：
//     · **SIGTERM / SIGKILL → `finally` 不跑** ⇒ 手臂留著壓玻璃、`motion_hz` 卡在 50
//     · **SIGINT → 丟 KeyboardInterrupt，穿過 try 到 finally** ⇒ 卸力 + 參數還原
//   ⇒ **一定要用 SIGINT，不能用 SIGTERM。** 我第一版寫 SIGTERM，那會讓手臂留在牆上。
//
//   📌 而且**安全動作由後端透過已開的 bridge 直接做，不賭 python 收得到訊號** ——
//      python 可能正卡在某個 socket recv 上。前兩步不依賴它。
//
//   五步：① 吊機 `stop`（立刻停鋼索）② 本體 `arm_park`（卸力的 backstop）
//        ③ SIGINT（觸發腳本自己的 cleanup）④ 寬限後仍在 → SIGKILL
//        ⑤ **proc 結束後**本體 `pusher all retract` + `pump off`（收腳脫離牆面，2026-09-16 加）
const MISSION_SIGINT_GRACE_MS = 12000;   // cleanup 裡 arm_park 的 ask timeout 是 90s，
                                         // 但第 ② 步已經直接送過，不必等那麼久。

// [2026-09-15 per user] 暫停 = 維持現狀(腳吸著、位置不動),但手臂收回、水泵/滾刷關。
// 腳本自己在檢查點(伸腳前 / 每把工具前 / 放繩前 / 回程前)收到 SIGUSR1 才停,所以最多延遲一段
// 滑台或一次壓牆;SIGUSR2 續跑。`mission.paused` 以腳本印出的 `[PAUSE]` 行為準,這裡只記「已要求」。
function missionPause(reply) {
    if (!mission.proc)    return reply({ ok: false, err: 'not_running' });
    if (mission.stopping) return reply({ ok: false, err: 'stopping' });
    try { mission.proc.kill('SIGUSR1'); } catch (e) { return reply({ ok: false, err: 'signal_failed', detail: String(e && e.message) }); }
    missionPush('[web] PAUSE 要求已送出（腳本到下一個檢查點才停：收臂、關水泵/滾刷、腳維持吸附）');
    reply({ ok: true });
}
function missionContinue(reply) {
    if (!mission.proc)    return reply({ ok: false, err: 'not_running' });
    try { mission.proc.kill('SIGUSR2'); } catch (e) { return reply({ ok: false, err: 'signal_failed', detail: String(e && e.message) }); }
    missionPush('[web] CONTINUE 已送出');
    reply({ ok: true });
}

// [2026-09-16 per user] STOP 的第 ⑤ 步：**收腳**。
//   「中止跟急停容易被誤解,而且中止還吸附在牆上容易被誤操作」——
//   在此之前「中止」停完是「腳還吸在牆上、推桿伸著」，畫面上卻只寫著「已停止」。
//   那個狀態下任何一個移動吊機的動作都是拿鋼索扯機器（本體 `crane_goto` 已加硬守衛擋掉，
//   但擋掉不等於安全，安全是**根本不要停在那個狀態**）。所以停止＝離開牆面：純吊在繩上。
//   要「停下來看一眼、原位續跑」請用**暫停**（那條路徑刻意保留吸附）。
// 🔴 一定要等 python 結束才送：`pusher all retract` 與腳本 cleanup 會搶同一條 Modbus 匯流排。
// 📌 `pusher all retract` 內建「關閥 → 洩壓 → CH6 正壓 → 兩段收回」，不必也不可以自己拆步驟。
function missionDetachFeet() {
    const ok1 = washrobot.send('pusher all retract');
    missionPush(`[web] STOP ⑤：本體 pusher all retract（收腳、脫離牆面）` +
                (ok1 ? ' 已送出（約 4 s，回覆看 washrobot 那行）' : ' —— 🔴 送不出去，本體橋接未連線'));
    // 幫浦：腳本 cleanup 會關，但 SIGKILL 收場時不會 —— 補一次（冪等）。
    // 排在 retract 之後送，本體單連線序列處理，順序即是收腳完才關幫浦。
    const ok2 = washrobot.send('pump off');
    if (!ok2) missionPush('[web] STOP ⑤b：pump off 送不出去，本體橋接未連線');
}

function missionStop(reply) {
    if (!mission.proc)     return reply({ ok: false, err: 'not_running' });
    if (mission.stopping)  return reply({ ok: false, err: 'already_stopping' });
    mission.stopping = true;
    mission.detachAfterExit = true;   // ⑤：等 proc close 再收腳（見 missionDetachFeet）
    missionSend({ state: missionSnapshot() });

    // 步驟 ①：立刻對吊機送 stop（走 intr 連線，繞開可能塞住的主連線）。不等 python。
    const craneTarget = routeCrane('stop');
    const craneOk = craneTarget.send('stop');
    missionPush(`[web] STOP ①：吊機 stop（${craneTarget.name}）` +
                (craneOk ? ' 已送出' : ' —— 🔴 送不出去，橋接未連線'));

    // 步驟 ②：本體 arm_park —— **手臂卸力的 backstop**。
    //   即使 python 卡死收不到 SIGINT、或 cleanup 跑不完，手臂也保證會被收回。
    //   重複卸力是 no-op；不卸力的代價是馬達鎖存後只能斷電。
    const armOk = washrobot.send('arm_park');
    missionPush(`[web] STOP ②：本體 arm_park（手臂卸力）` +
                (armOk ? ' 已送出' : ' —— 🔴 送不出去，本體橋接未連線'));

    // 步驟 ③：SIGINT 讓腳本跑自己的 cleanup（風扇關、motion_hz 還原、再 arm_park 一次）。
    setTimeout(() => {
        if (!mission.proc) return;
        missionPush('[web] STOP ③：送 SIGINT（觸發腳本 finally:cleanup）');
        try { mission.proc.kill('SIGINT'); } catch (e) {}

        // 步驟 ④：寬限後仍在 → SIGKILL。到這一步 cleanup 已經沒機會跑了，
        //         但 ① 與 ② 已經把兩件安全動作做完，所以可以接受。
        setTimeout(() => {
            if (!mission.proc) return;
            missionPush('[web] STOP ④：SIGINT 後仍在跑 → SIGKILL' +
                        '（cleanup 未完成，但 ①② 的停機與卸力已經送出）');
            try { mission.proc.kill('SIGKILL'); } catch (e) {}
        }, MISSION_SIGINT_GRACE_MS);
    }, 700);

    reply({ ok: true });
}

//=========== ws event ===========

wss.on('connection', (ws) => {
    ws.isAlive = true;
    ws.on('pong', () => { ws.isAlive = true; });

    ws.send(JSON.stringify({
        src: 'status',
        washrobot:  washrobot.isConnected(),
        crane:      crane.isConnected(),
        arm:        arm.isConnected()
    }));

    // ③ 行程不綁瀏覽器連線 ⇒ 新連上（含**重新整理／換分頁／斷線重連**）要能接得回去：
    //    先送目前狀態，再把 ring buffer 一次補完。
    //    🔴 不補的話，重新整理之後畫面會是空的，而任務**還在跑** ——
    //    「看起來沒在跑」比「沒有畫面」危險得多。
    ws.send(JSON.stringify({ src: 'mission', state: missionSnapshot() }));
    ws.send(JSON.stringify(wallMsg()));   // 牆高：每個新連線都要知道
    if (mission.ring.length)
        ws.send(JSON.stringify({ src: 'mission', backlog: mission.ring.slice() }));

    ws.on('message', (data) => {
        let msg;
        try { msg = JSON.parse(data.toString()); }
        catch { return ws.send(JSON.stringify({ src: 'error', line: 'invalid_json' })); }

        // ---- 任務控制（不走 TCP 橋接，因此在 cmd 檢查之前處理）----
        //
        // 🔴 [2026-09-10] **接受兩種形狀，內部正規化成一種。**
        //   本檔原生是 `{mission:"start"|"stop"|"state"}`，但協定討論時另一個形狀
        //   `{target:"mission", cmd:"start"|"stop"|"status"}` 也被寫進交接訊息裡。
        //   不接受的話，照那份文件寫的 client 會收到 `unknown_target` —— 而那個錯誤
        //   看起來像「後端沒裝好」，不像「協定寫錯」。
        //
        //   🐛 **這個不一致差點沒被發現**：兩邊各自「測過」都看到 state 回來了，
        //      但那是**連上時的主動推送**（下方 `ws.on('connection')` 會先送一次），
        //      **不管送什麼都會出現** ⇒ 兩個人都把它當成自己那次請求的回覆。
        //      要分辨得送一個**只有請求路徑才產得出**的東西（例如未知動作 → `ack`）。
        //   📌 通則：驗證請求／回覆時，回覆必須是**這次請求獨有**的，
        //      否則你驗到的可能是一個跟請求無關的訊息。
        if (msg.target === 'mission' && typeof msg.cmd === 'string') {
            msg = { mission: msg.cmd === 'status' ? 'state' : msg.cmd, params: msg.args || msg.params };
        }
        if (msg.wall) {
            const reply = (o) => ws.send(JSON.stringify(Object.assign({ src: 'wall', ack: msg.wall }, o)));
            if (msg.wall === 'get')   return reply(Object.assign({ ok: true }, wallMsg()));
            if (msg.wall === 'clear') { wall = { cm: 0, at: null, left_raw: null, ground_at: null }; wallSave(); broadcast(wallMsg()); return reply({ ok: true }); }
            // ① 成功後由前端呼叫：只更新 ground_at，**牆高保留**（只重校最低點時沿用記憶值）
            if (msg.wall === 'ground') {
                wall.ground_at = new Date().toISOString();
                wallSave(); broadcast(wallMsg());
                return reply(Object.assign({ ok: true }, wallMsg()));
            }
            // ② 只更新牆高，**ground_at 保留**（只重校最高點時沿用 ①）
            if (msg.wall === 'set') {
                const cm = Number(msg.cm);
                if (!Number.isFinite(cm) || cm < 1 || cm > 999) return reply({ ok: false, err: 'bad_cm(1~999)' });
                wall.cm = Math.round(cm); wall.at = new Date().toISOString();
                wall.left_raw = Number.isFinite(Number(msg.left_raw)) ? Number(msg.left_raw) : null;
                wallSave(); broadcast(wallMsg());
                return reply(Object.assign({ ok: true }, wallMsg()));
            }
            return reply({ ok: false, err: 'unknown_wall_action' });
        }
        if (msg.mission) {
            const reply = (o) => ws.send(JSON.stringify(Object.assign({ src: 'mission', ack: msg.mission }, o)));
            if (msg.mission === 'start')  return missionStart(msg.params || {}, reply);
            if (msg.mission === 'stop')   return missionStop(reply);
            if (msg.mission === 'pause')    return missionPause(reply);      // [2026-09-15]
            if (msg.mission === 'continue') return missionContinue(reply);   // [2026-09-15]
            if (msg.mission === 'state')  return reply({ ok: true, state: missionSnapshot() });
            return reply({ ok: false, err: 'unknown_mission_action' });
        }

        if (typeof msg.cmd !== 'string' || !msg.cmd.length)
            return ws.send(JSON.stringify({ src: 'error', line: 'empty_cmd' }));

        const target = msg.target === 'washrobot'  ? washrobot
                     : msg.target === 'crane'      ? routeCrane(msg.cmd)
                     : msg.target === 'arm'        ? arm
                     : null;
        if (!target) return ws.send(JSON.stringify({ src: 'error', line: 'unknown_target' }));

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
    console.log(`[web_backend] crane      target = ${CRANE_IP}:${CRANE_PORT} (main + intr connection for stop/status bypass)`);
    // 🔴 印出來是刻意的：部署到別的目錄時，`MISSION_PY` 會指向一個不存在的檔，
    //    而 spawn 失敗只會在按下啟動時才現形。開機就印，一眼看得出指到哪。
    console.log(`[web_backend] mission    script = ${MISSION_PY} (cwd ${MISSION_CWD})`);
});
