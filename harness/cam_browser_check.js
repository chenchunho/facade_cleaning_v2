// cam_browser_check.js — REAL-browser check of the console v3 camera card (2026-10-07). jsdom (gui_v3_check.js)
// has no MediaSource, so the H.264 <video> path can only be verified in a real Chrome. Read-only: opens the
// Dashboard, samples each <video> 4× (mode / readyState / playhead lag / decoded & dropped frames), screenshots.
//
// Windows host (WSL2 cannot reach Chrome's debug port; run both on the Windows side):
//   1) "C:\Program Files\Google\Chrome\Application\chrome.exe" --headless=new --remote-debugging-port=9333
//        --user-data-dir=<agent_ai>\.tmp\cdp-profile --autoplay-policy=no-user-gesture-required about:blank
//   2) node harness\cam_browser_check.js <webSocketDebuggerUrl from http://127.0.0.1:9333/json/version> <out-dir> [gui-url]
// 10-07 baseline (Chrome 154, test machines): mp4 both, 20 fps, lag ~0.4 s, dropped ~1 %, playbackRate 1.0.
const path = require('path');
const WS = require(path.join(__dirname, '..', 'web_backend', 'node_modules', 'ws'));
const fs = require('fs');
const [,, wsUrl, outDir, guiUrl] = process.argv;
const ws = new WS(wsUrl);
let id = 0; const pend = {}; const logs = [];
const send = (method, params = {}, sessionId) => new Promise(res => { const i = ++id; pend[i] = res; ws.send(JSON.stringify({ id: i, method, params, sessionId })); });
ws.on('message', d => { const m = JSON.parse(d); if (m.id && pend[m.id]) { pend[m.id](m.result || m.error); delete pend[m.id]; }
  else if (m.method === 'Runtime.consoleAPICalled') logs.push('console.' + m.params.type + ': ' + m.params.args.map(a => a.value || a.description).join(' '));
  else if (m.method === 'Runtime.exceptionThrown') logs.push('EXC: ' + (m.params.exceptionDetails.exception || {}).description); });
const sleep = ms => new Promise(r => setTimeout(r, ms));
const PROBE = `(() => { const r = {state: document.getElementById('d-cam-state').textContent, ver: (document.getElementById('ver-tag')||{}).textContent, cams: []};
  document.querySelectorAll('#d-cam-card .camf').forEach(f => { const v = f.querySelector('video'), b = v.buffered;
    const end = b.length ? b.end(b.length - 1) : 0;
    let q = null; try { q = v.getVideoPlaybackQuality(); } catch (e) {}
    r.cams.push({ id: f.dataset.cam, mode: f.dataset.mode, st: f.querySelector('.cam-st').textContent, title: f.querySelector('.cam-st').title,
      ready: v.readyState, paused: v.paused, t: +v.currentTime.toFixed(3), lag: +(end - v.currentTime).toFixed(3), w: v.videoWidth, h: v.videoHeight,
      frames: q ? q.totalVideoFrames : null, dropped: q ? q.droppedVideoFrames : null, rate: v.playbackRate }); });
  return JSON.stringify(r); })()`;
ws.on('open', async () => {
  const { targetId } = await send('Target.createTarget', { url: 'about:blank' });
  const { sessionId } = await send('Target.attachToTarget', { targetId, flatten: true });
  await send('Runtime.enable', {}, sessionId); await send('Page.enable', {}, sessionId);
  await send('Emulation.setDeviceMetricsOverride', { width: 1400, height: 1000, deviceScaleFactor: 1, mobile: false }, sessionId);
  await send('Page.navigate', { url: guiUrl || 'http://192.168.5.31:8080/#dashboard' }, sessionId);
  const t0 = Date.now(); const samples = [];
  for (let i = 0; i < 4; i++) { await sleep(i ? 3000 : 6000);
    const r = await send('Runtime.evaluate', { expression: PROBE, returnByValue: true }, sessionId);
    samples.push({ at_s: ((Date.now() - t0) / 1000).toFixed(1), ...JSON.parse(r.result.value) }); }
  const shot = await send('Page.captureScreenshot', { format: 'png', clip: { x: 0, y: 0, width: 1400, height: 760, scale: 1 } }, sessionId);
  fs.writeFileSync(outDir + '/cam_card.png', Buffer.from(shot.data, 'base64'));
  console.log(JSON.stringify({ samples, logs: logs.slice(-15) }, null, 1));
  await send('Browser.close'); process.exit(0);
});
setTimeout(() => { console.log('TIMEOUT', JSON.stringify(logs)); process.exit(1); }, 60000);
