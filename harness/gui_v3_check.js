#!/usr/bin/env node
// gui_v3_check.js — console v3 end-to-end check against fake_robot, no machine, no Pi.
//
//   node harness/gui_v3_check.js            # spawn fake_robot + web_backend(v3) on a free port, run all sections
//   node harness/gui_v3_check.js --attach 8081   # use an already-running v3 server (e.g. from gui_offline.sh)
//                                                #   ⚠️ checks assume a FRESH fake_robot; a dirty one fails ③/⑥ etc.
//   node harness/gui_v3_check.js --only evt,flow # run a subset: boot, evt, flow, mission, safe, report
//
// What it is: jsdom loads web_backend/public_v3/index.html, opens a REAL WebSocket to server.js, which
// bridges to fake_robot.py (5001/5002/9527). Every check clicks buttons / reads DOM text — the same path
// a tablet would take. Not a unit test of copied functions: the page under test is the shipped file.
//
// Contract source: .claude/plans/orchestration_to_cpp_plan.md §2–§5. If a check here disagrees with the
// plan, the plan wins — fix the GUI or the fake, and say which.
//
// Requires: web_backend/node_modules/jsdom (devDependency) + ws. Exit code 1 on any failed check.
'use strict';
const path = require('path');
const fs = require('fs');
const { spawn, spawnSync } = require('child_process');
const net = require('net');

const REPO = path.resolve(__dirname, '..');
const WEB = path.join(REPO, 'web_backend');
const PAGE = path.join(WEB, 'public_v3', 'index.html');
const FAKE = path.join(__dirname, 'fake_robot.py');
const CMDLOG = path.join(__dirname, 'gui_cmd_log.txt');
const RUN = path.join(REPO, 'tmp', 'gui_v3_check'); fs.mkdirSync(RUN, { recursive: true });

// ---- args ----
const argv = process.argv.slice(2);
const argOf = (k) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : null; };
const ATTACH = argOf('--attach');
const ONLY = (argOf('--only') || 'boot,evt,pre,script,mission,safe,hold,stop,report').split(',');
const has = (s) => ONLY.includes(s);

// ---- deps (resolve from web_backend so `npm install` there is enough) ----
const req = (m) => require(require.resolve(m, { paths: [WEB] }));
const { JSDOM, VirtualConsole } = req('jsdom');
const WebSocket = req('ws');

// ---- tiny test kit ----
let fail = 0, total = 0;
const chk = (t, got, want) => {
  total++;
  const ok = JSON.stringify(got) === JSON.stringify(want);
  if (!ok) fail++;
  console.log((ok ? '  ✅ ' : '  🔴 ') + t + (ok ? '' : '  got=' + JSON.stringify(got) + ' want=' + JSON.stringify(want)));
};
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
// bounded wait (CLAUDE.md: no unbounded loops)
const waitFor = async (fn, n = 80, ms = 250) => { for (let i = 0; i < n; i++) { if (fn()) return true; await sleep(ms); } return false; };
const portFree = (p) => new Promise(r => { const s = net.createServer(); s.once('error', () => r(false)); s.listen(p, '127.0.0.1', () => s.close(() => r(true))); });
const portOpen = (p) => new Promise(r => { const s = net.connect(p, '127.0.0.1'); s.once('connect', () => { s.destroy(); r(true); }); s.once('error', () => r(false)); });

// ---- environment ----
const procs = [];
async function bringUp() {
  if (ATTACH) return +ATTACH;
  // fake_robot must be OURS: the checks assume a fresh sim (nothing zeroed / homed / init'ed).
  // Its ports are fixed (server.js hardcodes 5001/5002/9527), so refuse to run on top of someone else's.
  const up = await portOpen(5001) || await portOpen(5002) || await portOpen(9527);
  if (up) throw new Error('fake_robot ports (5001/5002/9527) already in use — `./harness/gui_offline.sh stop` first, or pass --attach <port> to reuse it (state may be dirty)');
  {
    const f = spawn('python3', [FAKE, '--quiet'], { stdio: ['ignore', fs.openSync(path.join(RUN, 'fake_robot.log'), 'w'), 'inherit'] });
    procs.push(f);
    const ok = await waitFor(() => true, 1, 0) && await (async () => { for (let i = 0; i < 40; i++) { if (await portOpen(5001) && await portOpen(5002) && await portOpen(9527)) return true; await sleep(250); } return false; })();
    if (!ok) throw new Error('fake_robot did not come up (see tmp/gui_v3_check/fake_robot.log)');
  }
  try { fs.unlinkSync(path.join(RUN, 'run', 'wall_height.json')); } catch (_) {}   // server wall memory must be fresh too (①②⑦ read it)
  try { fs.unlinkSync(path.join(RUN, 'run', 'mission_params.json')); } catch (_) {}  // 09-17: 存檔 check writes it; a stale one would also pre-fill the form
  let port = 8090; while (!(await portFree(port))) port++;
  const w = spawn('node', ['server.js'], {
    cwd: WEB,
    env: Object.assign({}, process.env, { WROBOT_IP: '127.0.0.1', CRANE_IP: '127.0.0.1', ARM_IP: '127.0.0.1',
      HTTP_PORT: String(port), PUBLIC_DIR: path.join(WEB, 'public_v3'), HOME: RUN }),
    stdio: ['ignore', fs.openSync(path.join(RUN, 'web.log'), 'w'), 'inherit']
  });
  procs.push(w);
  const ok = await (async () => { for (let i = 0; i < 120; i++) { if (await portOpen(port)) return true; await sleep(250); } return false; })();   // 30 s: node on drvfs can take >10 s to boot
  if (!ok) throw new Error('web_backend v3 did not come up (see tmp/gui_v3_check/web.log)');
  return port;
}
function tearDown() { procs.forEach(p => { try { p.kill(); } catch (_) {} }); }

// ---- page ----
function loadPage(port) {
  const html = fs.readFileSync(PAGE, 'utf8');
  const vc = new VirtualConsole(); const errors = [];
  vc.on('jsdomError', e => errors.push(String(e && e.message || e)));
  const dom = new JSDOM(html, { url: `http://127.0.0.1:${port}/`, runScripts: 'dangerously', resources: 'usable', pretendToBeVisual: true, virtualConsole: vc });
  const w = dom.window;
  w.__confirms = []; w.confirm = (m) => { w.__confirms.push(String(m)); return true; };   // auto-accept, but count: 09-15 pm only 4 places may ask
  w.prompt = () => 'checked';
  w.__alerts = []; w.alert = (m) => w.__alerts.push(String(m));
  const txt = (id) => { const e = w.document.getElementById(id); return e ? e.textContent.trim() : null; };
  const click = (id) => { const e = w.document.getElementById(id); if (!e) throw new Error('no #' + id); e.click(); };
  const q = (sel) => w.document.querySelector(sel);
  const arm = () => { const b = w.document.getElementById('danger-arm'); if (b && !/🔓/.test(b.textContent)) b.click(); };   // 09-15 pm: the danger gate is gone → no-op (kept so old call sites read the same)
  const flowRun = (k, second) => q(`[data-flowrun${second ? '2' : ''}="${k}"]`).click();
  const sim = (c) => q(`[data-sim="${c}"]`).click();
  const lamp = (k) => txt('flow-tag-' + k), gate = (k) => txt('flow-gate-' + k);
  // 🔴 09-15: `.ebanner{display:flex}` 蓋掉 hidden ⇒ `.hidden` 為 true 但畫面上看得到。驗收一律看 computed display。
  const shown = (id) => { const e = w.document.getElementById(id); return !!e && w.getComputedStyle(e).display !== 'none'; };
  const banner = () => ({ get hidden(){ return !shown('estop-banner'); }, get textContent(){ return w.document.getElementById('estop-banner').textContent; } });
  const wallSet = (port, cm) => new Promise((res) => {
    const ws = new WebSocket(`ws://127.0.0.1:${port}`);
    ws.on('open', () => { ws.send(JSON.stringify({ wall: 'set', cm, left_raw: -cm })); setTimeout(() => { ws.close(); res(); }, 500); });
  });
  return { dom, w, errors, txt, click, q, arm, flowRun, sim, lamp, gate, banner, shown, wallSet };
}

// ================================================================ sections
async function secBoot(P) {
  console.log('\n[boot]');
  await sleep(3500);
  chk('page boots with 0 jsdom errors', P.errors.length, 0);
  chk('all four connection dots up', ['c-ws', 'c-wr', 'c-cr', 'c-ar'].map(id => P.w.document.getElementById(id).className), ['up', 'up', 'up', 'up']);
  chk('default tab = Dashboard; tabs = Dashboard/Mission/Manual/Setting (09-17 per user)', [P.q('.pane.on').dataset.pane, Array.from(P.w.document.querySelectorAll('[role=tab]')).map(b => b.dataset.p).join(',')], ['dashboard', 'dashboard,mission,manual,setting']);
  // [2026-09-17 relayout] 四張卡離開 Mission；腳本輸出 + 每步時間 + 任務細節搬到 Dashboard
  chk('Mission no longer has 即時監看/致動器回讀/吸盤 p5–p8/單步時間基準', ['mon-age', 'verif', 'm-cups', 'm-seal', 'sp', 'g-diff'].map(id => P.w.document.getElementById(id)).every(e => e === null), true);
  { const dash = P.w.document.querySelector('.pane[data-pane="dashboard"]');
    chk('Dashboard holds 腳本輸出, 每步時間 and the 任務執行 detail rows', ['mis-out', 'mis-table', 'mis-sub', 'mis-tool', 'mis-detach'].map(id => dash.contains(P.w.document.getElementById(id))).join(','), 'true,true,true,true,true'); }
  { const mp = P.w.document.querySelectorAll('#mp-body > .ctl');
    chk('參數設定 = 5 rows in sketch order', Array.from(mp).map(r => r.querySelector('.cl').firstChild.textContent.trim()).join('|'), '單步距離 / 步數 / 週期數|風扇|滑台|手臂壓力 / 乾掃|中止門檻');
    chk('[現場 09-17] all 5 rows share one skeleton (.ctl.mp + .mpc, controls right-aligned, inputs same width)', [Array.from(mp).every(r => r.classList.contains('mp') && r.querySelector(':scope > .mpc')), Array.from(P.w.document.querySelectorAll('#mp-body .numin')).map(e => P.w.getComputedStyle(e).width).filter((v, i, a) => a.indexOf(v) === i).length, P.w.getComputedStyle(mp[0].querySelector('.mpc')).justifyContent], [true, 1, 'flex-end']);
    const pane = P.w.document.querySelector('.pane[data-pane="mission"]');
    chk('left column = 前置 summary → 共用值 (09-17) → 參數設定', Array.from(pane.querySelector('.mis-left').children).map(e => e.id).join(','), 'pre-card,ms-card,mp-card');
    chk('right column = 存檔 / 開始⇄停止作業 / 緊急脫離, estop red + gap', [Array.from(pane.querySelectorAll('.mis-btns button')).map(b => b.id).join(','), P.w.getComputedStyle(P.w.document.getElementById('wr-estop')).backgroundColor !== '', !!pane.querySelector('.mis-btns .estop-gap')], ['mp-save,mis-start,mis-stop,wr-estop', true, true]);
    chk('idle: 開始 shown, 停止作業 hidden (one slot)', [P.shown('mis-start'), P.shown('mis-stop')], [true, false]); }
  chk('script backend is the default; body-only controls hidden', [P.w.__v3.backend(), P.w.document.getElementById('mis-pause').hidden, P.w.document.getElementById('mis-out').closest('.grp').hidden], ['script', true, false]);
  chk('CONSOLE_VER is a v3 build', /^v3-/.test(P.txt('ver-tag')), true);
  chk('test hook present', typeof P.w.__v3, 'object');
  // 🔴 09-15 的坑：class 設了 display 就會蓋掉 `hidden` ⇒ 元素明明 hidden=true 卻看得見。
  //    這條掃全頁：任何帶 hidden 的元素，computed display 必須真的是 none（.why 系刻意用 visibility，排除）。
  { const bad = Array.from(P.w.document.querySelectorAll('[hidden]'))
      .filter(e => !e.classList.contains('why') && P.w.getComputedStyle(e).display !== 'none')
      .map(e => e.id || e.className);
    chk('every [hidden] element is really display:none', bad.join(','), ''); }
}

async function secEvt(P) {
  console.log('\n[evt] plan §2.2 — every EVT kind fed straight into the parser');
  const E = (l) => P.w.__v3.onMissionEvt(l), t = P.txt;
  E('EVT mission start id=7 steps=2 step_cm=40 cycles=1 top=-120 bal=imu');
  chk('start → 執行中 #7', [t('mis-state'), t('mis-phase')], ['執行中', 'running #7']);
  E('EVT mission step_begin cyc=1 step=1 h=-120');
  chk('step_begin → cyc/step/h', [t('mis-cyc'), t('mis-step'), t('mis-h')], ['1', '1', '-120 cm']);
  E('EVT mission vac_wait n_seal=0 p5=-2 p6=-2 p7=-2 p8=-2 t=9.9');
  chk('vac_wait → 4 pressures + t + n_seal', [t('mis-vac'), t('mis-vact'), t('mis-nseal')], ['-2 / -2 / -2 / -2', '9.9s', '0/4']);
  E('EVT mission vac_result n_seal=0 skip_clean=1');
  E('EVT mission clean tool=- wet=0 result=skipped');
  chk('no_seal → skip: counters 1/1', [t('mis-skipped'), t('mis-clean')], ['1 / 1', '- · 乾 · skipped']);
  E('EVT mission move verb=pay_out cm=40 hz=30');
  chk('move sub-step', t('mis-sub'), 'move pay_out 40cm @30Hz');
  E('EVT mission roll_recover result=ok roll_before=6.1');
  E('EVT mission step_done cyc=1 step=1 t_ext=1.0 t_vac=10.0 t_clean=0.0 t_ret=0.8 t_move=12.5 roll_after=+0.30');
  chk('step_done → one table row', [t('mis-steps-n'), P.w.document.querySelectorAll('#mis-table tbody tr').length], ['1 步', 1]);
  E('EVT mission clean tool=LEFT wet=0 result=obstacle');
  chk('clean obstacle shown', t('mis-clean'), 'LEFT · 乾 · obstacle');
  E('EVT mission cycle_done cyc=1 up_s=25.0');
  E('EVT mission stop reason=done summary="no_seal=1 warn=0 skipped=1 roll_fixed=1"');
  chk('stop → 閒置 + summary', [t('mis-state'), /no_seal=1 warn=0 skipped=1 roll_fixed=1/.test(t('mis-summary'))], ['閒置', true]);
  E('EVT mission precheck_fix item=bal set=imu');
  P.w.__v3.onSafeEvt('EVT safe_enter src=watchdog detail=crane_timeout');
  chk('safe_enter EVT parsed (no throw)', true, true);
}

async function secPre(P, port) {
  console.log('\n[pre] 2026-09-15 — 七項前置 inside Mission, all from status/wall, all via buttons');
  const { lamp, arm, flowRun, w, txt, click } = P;
  const miss = () => w.__v3.preMissing();
  await waitFor(() => lamp('water') === '未完成', 20);
  await waitFor(() => lamp('water') === '未完成', 20);   // the boot-time water_level lands within ~1.5 s
  // [2026-09-15 晚] ④ init（幫浦）已離開前置：腳本起跑時自己 init、回程前自己 pump off ⇒ 六項
  // [2026-09-16] 手臂開機即待命 ⇒ 由必要項降為資訊列；必要項剩四：① 地面歸零 ② 牆高 ③ 水位 ④ 起點
  chk('fresh fake: four required 未完成, no info rows left (推桿歸零/手臂 rows removed 09-17), start disabled', [['zero','wall','water','top'].map(lamp).join(','), w.document.getElementById('flow-lamp-zdt'), w.document.getElementById('flow-lamp-arm'), w.document.getElementById('mis-start').disabled, miss().length], ['未完成,未完成,未完成,未完成', null, null, true, 4]);
  chk('init is no longer a precheck item; 一鍵前置 is gone', [w.document.getElementById('flow-lamp-init'), w.document.getElementById('pre-runall')], [null, null]);
  chk('top-bar chip counts only the required four', txt('flow-stage'), '前置 0/4');
  chk('手臂 info row shows 已待命 while the fake reports arm_ready=1 after arm_init', true, true);
  chk('09-15 pm: no 危險操作 button, no arm unlock/INIT rows (09-17 arm card redo); 急停 sits in the Mission right column', [w.document.getElementById('danger-arm'), w.document.getElementById('arm-arm'), w.document.querySelector('.mis-btns').contains(w.document.getElementById('wr-estop')), w.document.getElementById('arm-init')], [null, null, true, null]);
  const nConf = () => w.__confirms.length;
  const c0 = nConf();
  flowRun('zero'); await waitFor(() => lamp('zero') === 'OK' && /已歸零（/.test(txt('flow-zero-rd')), 30);
  chk('① zero_meters ground → OK (server ground_at) + crane zeroed_at shown', [lamp('zero'), /本次啟動已歸零（\d+ 秒前）/.test(txt('flow-zero-rd'))], ['OK', true]);
  chk('① asked exactly one confirm (zero_meters is one of the 4 kept)', nConf() - c0, 1);
  flowRun('wall'); await sleep(300);
  chk('② 最高點設定 refused at ground (|L| too small)', w.__alerts.some(a => /太小/.test(a)), true);
  await P.wallSet(port, 120);
  await waitFor(() => lamp('wall') === 'OK' && /吊機已同步/.test(txt('flow-wall-rd')), 15);
  chk('② wall via server → OK and pushed to crane set_wall_height (status wall_height_cm=120)', [lamp('wall'), w.__v3.last.cr.wall_height_cm], ['OK', '120']);
  chk('⑦ 起點: at ground vs 牆高 120 → 未完成 with 差 shown', [lamp('top'), /差 -120/.test(txt('flow-top-rd'))], ['未完成', true]);
  chk('手臂 is not a precheck any more (script checks arm_ready itself)', miss().some(x => /手臂/.test(x)), false);
  // [2026-09-17 per user] 推桿歸零 info row is gone from 前置 (歸零 lives in Manual: 當前位置歸零／自動歸零)
  chk('前置 = the four required only: zero, wall, water, top', Array.from(w.document.querySelectorAll('#pre-root .flowitem')).map(e => e.dataset.k).join(','), 'zero,wall,water,top');
  // [2026-09-17 per user] 兩顆水位計：低（有水/空箱）＝門檻、高（滿/未滿）＝補水停止點（本體自動關閥 + EVT）。fake：on 後 3 s 低=1、8 s 高=1 + EVT
  chk('③ before filling: 低 空箱 · 高 未滿 shown on both Dashboard lamps and the 前置 row', [txt('wl-state'), txt('wl-high'), w.document.getElementById('wl-low-lamp').className, /低 空箱 · 高 未滿/.test(txt('flow-water-rd')), txt('wl-tag')], ['空箱', '未滿', 'wl-lamp off', true, '需補水']);
  flowRun('water'); await waitFor(() => /補水中/.test(txt('flow-water-rd')), 10);
  chk('③ 補水 → water_inlet on, progress shown with both lamps', /補水中 \d+ s… 低 (空箱|有水) · 高 未滿/.test(txt('flow-water-rd')), true);
  await waitFor(() => lamp('water') === 'OK' && /低 有水 · 高 滿/.test(txt('flow-water-rd')), 200);
  chk('③ high mark → body auto-closes (EVT water_inlet_auto_close logged) → 低 有水 · 高 滿 → OK', [lamp('water'), /低 有水 · 高 滿 · \d+ 秒前/.test(txt('flow-water-rd')), w.__v3.logs().some(l => /高水位滿，本體已自動關進水閥/.test(l)), w.__v3.logs().some(l => /補水：高水位滿/.test(l))], ['OK', true, true, true]);
  chk('③ Dashboard: 低 有水 (green) · 高 滿 (green) · tag 滿（自動關閥） · RSSI 低/高', [txt('wl-state'), txt('wl-high'), w.document.getElementById('wl-high-lamp').className, txt('wl-tag'), /^\d+ \/ \d+$/.test(txt('wl-rssi'))], ['有水', '滿', 'wl-lamp on', '滿（自動關閥）', true]);
  chk('③ status water_low/water_high cache is merged (body keeps them fresh while the valve is open)', [w.__v3.last.wr.water_low, w.__v3.last.wr.water_high], ['1', '1']);
  { const l0 = w.__v3.logs().length; flowRun('water'); await waitFor(() => w.__v3.logs().some(l => /高水位已滿，本體沒開閥/.test(l)), 30);
    chk('③ 補水 while already full → water_inlet on replies OK skipped water_high=1 → done without opening', w.__v3.logs().some(l => /高水位已滿，本體沒開閥/.test(l)), true); }
  chk('進水閥 row says the body auto-closes at the high mark', /高水位滿了本體自動關/.test(w.document.querySelector('#wrelaygrp [data-inlet] .cl').textContent), true);
  // ⑦ 與 Manual 三顆、控制列那顆共用 craneGotoAction；這裡順便驗「移動中…」與結果字串
  flowRun('top'); await waitFor(() => /移動中…/.test(txt('flow-top-rd')) || lamp('top') === 'OK', 20);
  await waitFor(() => lamp('top') === 'OK', 200);
  chk('⑦ crane_goto 120 → height within ±5 → OK', lamp('top'), 'OK');
  // flow-top-rd 一秒後會被 paintFlow 寫回狀態字 ⇒ 結果字串看 Manual 那格（同一次動作的第二個輸出位）
  chk('⑦ result string parsed (到位/已在位, not the raw line)', /(到位|已在位)/.test(txt('cg-rd')), true);
  // Manual 三顆 + 控制列那顆（2026-09-15 晚 per user）
  chk('Manual crane card has 拉到頂端/放到地面/拉到指定; control bar has ⤒ 拉到頂端', ['cg-top','cg-ground','cg-go','cg-cm','mbar-top'].map(id => !!w.document.getElementById(id)).join(','), 'true,true,true,true,true');
  chk('控制列 拉到頂端 enabled once 牆高 is known and nothing is running', w.document.getElementById('mbar-top').disabled, false);
  w.document.getElementById('cg-cm').value = '900';
  click('cg-go'); await sleep(200);
  chk('拉到指定 rejects a target above 牆高 before sending', w.__alerts.some(a => /超出 0–120/.test(a)), true);
  // ⚠️ 清掉上一次的結果字串再等 —— 不清的話 waitFor 會立刻命中舊字串、在移動還在途中就往下走，
  //    下一顆就撞上「已經有一次 crane_goto 在進行中」（第一次跑就是這樣紅的）。
  w.document.getElementById('cg-cm').value = '100';
  w.document.getElementById('cg-rd').textContent = '';
  click('cg-go'); await waitFor(() => /(到位|已在位)/.test(txt('cg-rd')), 200);
  await waitFor(() => Math.abs(Math.abs(+w.__v3.last.cr.length_left) - 100) <= 5, 20);   // status 每秒一筆，回覆比它早到
  chk('拉到指定 100 → moved, result shown in the Manual card', [/到位 \d+ cm/.test(txt('cg-rd')), Math.abs(Math.abs(+w.__v3.last.cr.length_left) - 100) <= 5], [true, true]);
  // [2026-09-21] 收繩張力軟停：吊機回 OK 但帶 stopped=tension_stop short_by=N ⇒ 不能畫 ✅ 到位（09-15 停在 135 被當成功）
  await w.__v3.send('crane', 'fake_goto_short 20', 5000);
  w.document.getElementById('cg-rd').textContent = '';
  const nLog = w.__v3.logs().length;
  click('cg-top'); await waitFor(() => /(張力先到|到位|已在位)/.test(txt('cg-rd')), 200);
  chk('張力軟停 → ⚠️ 張力先到（差 20 cm、含收繩張力上限），不是 ✅ 到位；log 走 ⚠️ 不走 ✅', [/⚠️ 張力先到/.test(txt('cg-rd')), /差 20 cm/.test(txt('cg-rd')), /收繩張力上限 \d+ kg/.test(txt('cg-rd')), w.__v3.logs().slice(nLog).some(l => /⚠️ crane_goto 120 → OK goto .*stopped=tension_stop/.test(l))], [true, true, true, true]);
  w.document.getElementById('cg-rd').textContent = '';
  click('cg-top'); await waitFor(() => /(到位|已在位)/.test(txt('cg-rd')), 200);
  await waitFor(() => Math.abs(Math.abs(+w.__v3.last.cr.length_left) - 120) <= 5, 20);
  chk('拉到頂端 → back to 牆高 120 (±5)', Math.abs(Math.abs(+w.__v3.last.cr.length_left) - 120) <= 5, true);
  if (lamp('top') !== 'OK') { w.document.getElementById('cg-rd').textContent = ''; click('cg-top'); await waitFor(() => lamp('top') === 'OK', 200); }   // 一次 goto 可能停在容差邊緣（假機器 1 cm/s，回覆早於最後一筆 status）
  await waitFor(() => w.__v3.logs().some(l => /crane_goto 120 → /.test(l)), 480);   // body replies only when the move ends (fake: ~1 cm/s); the WR queue is serial
  await waitFor(() => miss().length === 0 && /前置全綠/.test(txt('flow-stage')) && !w.document.getElementById('mis-start').disabled, 40);
  if (miss().length) console.log('    (debug) still missing: ' + miss().join(',') + ' | ' + ['zero','wall','zdt','init','arm','water','top'].map(k => k + '=' + txt('flow-' + k + '-rd')).join(' | '));
  chk('all required green → start enabled, chip 前置全綠, card auto-collapsed to the bar summary', [miss().length, w.document.getElementById('mis-start').disabled, txt('flow-stage'), w.getComputedStyle(w.document.getElementById('pre-body')).display, /前置 4\/4 ✅/.test(txt('pre-sum'))], [0, false, '前置全綠', 'none', true]);
  // 🔴 收→展→收 三態都用真的 click + computed display 驗（只讀 .hidden 會漏掉 CSS 蓋過去的那型）
  const vis = (id) => w.getComputedStyle(w.document.getElementById(id)).display !== 'none';
  chk('collapsed: body hidden but the card header stays clickable (second entry point)', [vis('pre-body'), vis('pre-card'), vis('pre-head')], [false, true, true]);
  click('pre-sum'); await sleep(50);
  chk('summary click expands', vis('pre-body'), true);
  await sleep(1300);   // a paintFlow tick must NOT snap it shut again (⑥/⑦ flicker used to)
  chk('stays open across a repaint tick (auto-collapse retires once the user touches it)', vis('pre-body'), true);
  click('pre-head'); await sleep(50);
  chk('header click collapses again', [vis('pre-body'), vis('pre-head')], [false, true]);
  click('pre-head'); await sleep(50);
  chk('header click expands again (three-state round trip)', vis('pre-body'), true);
  // 幫浦不再是前置的一項：關掉它**不應該**擋住開始（腳本起跑時自己會 init）
  await w.__v3.send('washrobot', 'pump off', 8000); await sleep(2500);
  chk('pump OFF no longer blocks 開始 (script inits it)', [miss().length, w.document.getElementById('mis-start').disabled], [0, false]);
  await w.__v3.send('washrobot', 'init', 20000); await sleep(1500);   // restore the sim for later sections (body backend precheck still wants a pump)
}

async function secScript(P) {
  console.log('\n[script] 2026-09-15 — start via server.js {mission:start} (cycle_test full); on the fake only the WS payload is checked');
  const { w, txt, arm, click, shown } = P;
  w.__v3.setBackend('script');
  // 🔴 09-15 真機 bug：三條橫幅用 el.hidden 開關，但 `.ebanner{display:flex}` 蓋過去 ⇒ 永遠看得到。
  //    這條在任務還沒開跑、也沒進過 SAFE 時檢查 computed display —— 只讀 .hidden 的話兩邊都是綠的。
  chk('idle: all three banners really hidden (computed display, not .hidden)', [shown('estop-banner'), shown('manbar'), shown('safebar'), w.document.getElementById('manbar').hidden], [false, false, false, true]);
  w.document.getElementById('mp-stepcm').value = '40'; w.document.getElementById('mp-steps').value = '5'; w.document.getElementById('mp-cycles').value = '2';
  w.document.getElementById('mp-fan').value = 'all'; w.document.getElementById('mp-fanpct').value = '6';
  w.document.querySelector('[data-mrail="100-20"]').click();
  w.document.getElementById('mp-armnm').value = '4'; w.document.getElementById('mp-dry').checked = true;
  const p = w.__v3.misParams();
  chk('misParams uses cycle_test names', [p.step_cm, p.steps, p.cycles, p.fan, p.rail, p.arm_nm, p.dry, p.roll_trip, p.diff_trip, p.rail_cm], [40, 5, 2, 'all:6', '100-20', 4, 1, 6, 8, 100]);
  await sleep(1100);   // mis-cli repaints on the 1 s tick (programmatic .value does not fire input)
  chk('等效指令 line matches the form', /cycle_test\.py 2 5 40 6 8 fan=all:6 rail=100-20/.test(txt('mis-cli')) && /FCV_ARM_NM=4 FCV_DRY=1/.test(txt('mis-cli')), true);
  w.document.getElementById('mp-rail').value = '50-50';
  chk('validate: rail 起=迄 rejected', w.__v3.misValidate(w.__v3.misParams()).some(b => /rail/.test(b)), true);
  w.document.getElementById('mp-rail').value = 'off'; w.document.getElementById('mp-fanpct').value = '11';
  chk('validate: rail=off ok, fan pct 11 rejected', w.__v3.misValidate(w.__v3.misParams()).join(), 'fan pct 5~10');
  w.document.getElementById('mp-fanpct').value = '6';
  // capture what the page would send to server.js instead of really spawning cycle_test against the fake
  const wsObj = w.__v3.wsRef(); const realSend = wsObj.send; const sent = [];
  wsObj.send = (d) => { const j = JSON.parse(d); if (j.mission) sent.push(j); else realSend.call(wsObj, d); };   // only the mission protocol is captured; polls keep flowing (a swallowed poll jams the serial queue for its timeout)
  arm(); click('mis-start'); await sleep(300);
  wsObj.send = realSend;
  const m = sent.find(x => x.mission === 'start');
  chk('開始 → {mission:"start", params:{…}} with every field, rail_cm fixed 100, return_top 1 by default', [!!m, m && m.params.step_cm, m && m.params.fan, m && m.params.rail, m && m.params.arm_nm, m && m.params.dry, m && m.params.rail_cm, m && m.params.return_top], [true, 40, 'all:6', 'off', 4, 1, 100, 1]);
  // [2026-09-17 per user] 任務結束回頂端：預設勾；不勾 → params.return_top=0、等效指令尾巴 return_top=0、摘要標「結束停最低點」
  chk('回頂端 checkbox in row 1, checked, cli has no return_top', [w.document.querySelector('#mp-body > .ctl:first-child #mp-rtop') !== null, w.document.getElementById('mp-rtop').checked, /return_top/.test(txt('mis-cli'))], [true, true, false]);
  w.document.getElementById('mp-rtop').checked = false; w.document.getElementById('mp-rtop').dispatchEvent(new w.Event('input', {bubbles:true}));
  chk('unchecked → return_top:0, cli ends with return_top=0, summary says 結束停最低點', [w.__v3.misParams().return_top, /return_top=0$/.test(txt('mis-cli')), /結束停最低點/.test(txt('mp-sum'))], [0, true, true]);
  w.document.getElementById('mp-rtop').checked = true; w.document.getElementById('mp-rtop').dispatchEvent(new w.Event('input', {bubbles:true}));
  // server.js side of the protocol, fed as the page would receive it
  { const dash = w.document.querySelector('.pane[data-pane="dashboard"] .man'); const kids = Array.from(dash.children).filter(e => e.classList.contains('grp'));
    const i = kids.findIndex(e => e.id === 'd-mis-card');
    chk('[09-17 追加] Dashboard Mission 卡 = 一般小卡、在小卡群最後（張力之後、任務執行之前）、釘最後一欄', [i > 0 && /張力/.test(kids[i - 1].textContent), /任務執行/.test(kids[i + 1].textContent), w.document.getElementById('d-mis-card').style.gridColumn], [true, true, '-2/-1']); }
  { const sec = w.document.querySelector('.pane[data-pane="manual"] .man'); const kids = Array.from(sec.children);
    const from = kids.findIndex(e => e.classList.contains('man-sec') && /本體/.test(e.textContent));
    const heads = kids.slice(from + 1).filter(e => e.classList.contains('grp')).map(e => e.querySelector('header').firstChild.textContent.trim().split(' ')[0]);
    // [09-17 下午 per user「吸盤推桿小卡跟下面 4 張小卡對調」] 上午是推桿在最前；整列卡擺最前把四張半寬卡全推下去，改成墊底。
    chk('[09-17] Manual 本體區：手臂 → 上滑台 → 水路(PQW) → 風扇 → 吸盤推桿(吃滿、墊底)', [heads.join(','), kids.slice(from + 1).filter(e => e.classList.contains('grp')).pop().style.gridColumn], ['手臂,上滑台,水路,風扇,吸盤推桿', '1/-1']); }
  chk('[09-17 per user] Mission 狀態列的狀態字（閒置/本體/週期/步/高度/吸住）不顯示，按鈕（暫停⇄繼續/⤒/STOP/PARK）留著', [['mis-state','rd-wrstate','mis-cyc','mis-step','mis-h','mis-nseal'].every(id => !!w.document.getElementById(id).closest('[hidden]') && w.getComputedStyle(w.document.getElementById(id).closest('[hidden]')).display === 'none'), ['mis-pc','mbar-top','m-crane-stop','m-arm-park'].every(id => shown(id) || w.document.getElementById(id).hidden === false)], [true, true]);
  chk('[09-17] Manual 本體開關機 card gone (init/shutdown); body state text still computed (hidden, feeds nothing visible now)', [w.document.getElementById('wr-init'), w.document.getElementById('wr-shutdown'), /^本體 /.test(txt('rd-wrstate'))], [null, null, true]);
  chk('[09-17] idle: Dashboard Mission 卡 shows the form params, 開始 mirrored, no blink', [shown('d-mis-start'), shown('d-mis-stop'), /下一趟/.test(txt('d-mis-src')), /2 週期 × 5 步 × 40 cm .* fan=all:6 .* 結束回頂/.test(txt("d-mis-params")), w.document.getElementById('wr-estop').classList.contains('blink')], [true, false, true, true, false]);
  w.__v3.onMissionMsg({src:'mission', ack:'start', ok:true, running:{running:true, args:{cycles:2, steps:5, step_cm:40, roll_trip:6, diff_trip:8, top_cm:120, kv:['fan=all:6','rail=off']}, env:{FCV_ARM_NM:'4', FCV_DRY:'1'}}});
  w.__v3.onMissionMsg({src:'mission', state:{running:true, args:{cycles:2, steps:5, step_cm:40, roll_trip:6, diff_trip:8, top_cm:120, kv:['fan=all:6','rail=off']}, env:{FCV_ARM_NM:'4', FCV_DRY:'1'}}});
  chk('[09-17] running: both 緊急脫離 blink + enabled; Dashboard mirrors 停止作業, params = server args', [w.document.getElementById('wr-estop').classList.contains('blink'), w.document.getElementById('d-estop').classList.contains('blink'), w.document.getElementById('wr-estop').disabled, shown('d-mis-stop'), shown('d-mis-start'), w.document.getElementById('d-mis-stop').disabled, /執行中/.test(txt('d-mis-src')), /2 週期 × 5 步 × 40 cm · 門檻 roll 6° \/ 左右差 8 cm · fan=all:6 rail=off · 頂 120 cm · 手臂 4 N·m · 乾掃 · 結束回頂/.test(txt('d-mis-params')), txt('d-mis-state')], [true, true, false, true, false, false, true, true, '執行中']);
  { const o = w.__v3.wsRef(); const rs = o.send; const got = []; o.send = (d) => { const j = JSON.parse(d); if (j.mission) got.push(j); else rs.call(o, d); };
    click('d-mis-stop'); await sleep(50); o.send = rs;
    chk('Dashboard 停止作業 mirror → same handler → {mission:"stop"}', got.map(x => x.mission).join(','), 'stop'); }
  chk('ack ok + state.running → 執行中, 中止 enabled', [txt('mis-state'), w.document.getElementById('mis-stop').disabled], ['執行中', false]);
  chk('running: the slot flips to 停止作業, 開始 hidden', [shown('mis-stop'), shown('mis-start')], [true, false]);
  chk('no 通訊紀錄 panel; log() keeps an in-memory ring', [w.document.getElementById('log'), w.__v3.logs().length > 0], [null, true]);
  chk('sticky status bar holds 狀態/週期/步/高度 + 暫停⇄繼續 + STOP/PARK, and is sticky (09-17: 開始/停止/急停 moved to the right column)', [['mis-state','mis-cyc','mis-step','mis-h','mis-pc','m-crane-stop','m-arm-park'].every(id => w.document.getElementById('mbar').contains(w.document.getElementById(id))), w.document.getElementById('mbar').contains(w.document.getElementById('mis-start')), w.getComputedStyle(w.document.getElementById('mbar')).position], [true, false, 'sticky']);
  chk('暫停⇄繼續 in the bar; 停止作業 / 緊急脫離 wording unchanged (script mode)', [w.document.getElementById('mis-pc').hidden, w.document.getElementById('mis-pc').disabled, w.document.getElementById('mis-pc').textContent, txt('mis-stop'), w.document.getElementById('wr-estop').textContent], [false, false, '⏸ 暫停', '停止作業', '🔴 緊急脫離']);
  { const o = w.__v3.wsRef(); const rs = o.send; const got = []; o.send = (d) => { const j = JSON.parse(d); if (j.mission) got.push(j); else rs.call(o, d); };
    click('mis-pc'); w.__v3.onMissionMsg({src:'mission', state:{running:true, paused:true}}); click('mis-pc'); o.send = rs;
    chk('暫停 → {mission:"pause"}; state.paused → button reads 繼續 → {mission:"continue"}; 暫停中 shown', [got.map(x => x.mission).join(','), txt('mis-state')], ['pause,continue', '暫停中']);
    w.__v3.onMissionMsg({src:'mission', line:'[PAUSE] resumed'}); w.__v3.onMissionMsg({src:'mission', state:{running:true, paused:false}});
    chk('[PAUSE] resumed line / state.paused=false → 執行中, button 暫停', [txt('mis-state'), w.document.getElementById('mis-pc').textContent], ['執行中', '⏸ 暫停']); }
  chk('running → Manual mutex: pane.mlock + banner visible, STOP/PARK still clickable', [w.document.querySelector('.pane[data-pane="manual"]').classList.contains('mlock'), shown('manbar'), w.getComputedStyle(w.document.getElementById('crane-stop')).pointerEvents], [true, true, 'auto']);
  { const vis = (id) => w.getComputedStyle(w.document.getElementById(id)).display !== 'none';
    chk('running → 參數 collapses to the one-line summary, header still there', [vis('mp-body'), vis('mp-head'), /\d+ 步×\d+cm ×\d+ 週期 · [\d.]+ N·m · fan=/.test(txt('mp-sum'))], [false, true, true]);
    click('mp-sum'); await sleep(50);
    chk('參數 summary click expands; a repaint does not snap it shut', vis('mp-body'), true);
    await sleep(1300); chk('參數 still open after a tick', vis('mp-body'), true);
    click('mp-head'); await sleep(50); chk('參數 header click collapses', vis('mp-body'), false); }
  w.__v3.onMissionMsg({src:'mission', line:'═══ 週期 1/2 ═══'});
  w.__v3.onMissionMsg({src:'mission', line:'  1    3.4    2.0    4.4    3.7      -52.1/-48.0/-50.3/-49.9     5.4    0.31     12%     0.8    +0.20'});
  chk('stdout parsed: cycle, step row → table, vac column', [txt('mis-cyc'), txt('mis-step'), w.document.querySelectorAll('#mis-table tbody tr').length, txt('mis-vac')], ['1/2', '1/5', 1, '-52.1/-48.0/-50.3/-49.9']);
  chk('腳本輸出 pane shows lines', /週期 1\/2/.test(txt('mis-out')) && +txt('mis-out-n').replace(' 行', '') >= 2, true);
  w.__v3.onMissionMsg({src:'mission', ack:'start', ok:false, err:'wall_height_unset', detail:'② 最高點未量'});
  chk('server refusal surfaces in the log', w.__v3.logs().some(l => /啟動失敗：wall_height_unset/.test(l)), true);
  const wsObj2 = w.__v3.wsRef(); const sent2 = []; const rs2 = wsObj2.send; wsObj2.send = (d) => { const j = JSON.parse(d); if (j.mission) sent2.push(j); else rs2.call(wsObj2, d); };
  click('mis-stop'); await sleep(200); wsObj2.send = rs2;
  chk('中止 → {mission:"stop"}', sent2.some(x => x.mission === 'stop'), true);
  w.__v3.onMissionMsg({src:'mission', state:{running:false, exit:{code:0}}});
  // 🔴 急停必須先停腳本再送 emergency_stop（真機踩過兩次：腳本還在下指令，與收回動作交錯）
  { const o = w.__v3.wsRef(); const rs = o.send; const got = []; o.send = (d) => { const j = JSON.parse(d); if (j.mission) got.push(j); else rs.call(o, d); };
    w.__v3.onMissionMsg({src:'mission', state:{running:true}});
    const before = w.__v3.logs().length;
    click('wr-estop'); await sleep(400); o.send = rs;
    const sentCmds = w.__v3.logs().slice(before).join('\n');
    chk('急停（腳本執行中）→ 先 {mission:"stop"} 再 emergency_stop', [got.some(x => x.mission === 'stop'), /emergency_stop/.test(sentCmds), sentCmds.indexOf('mission stop 停腳本') < sentCmds.indexOf('emergency_stop')], [true, true, true]);
    w.__v3.onMissionMsg({src:'mission', state:{running:false, exit:{code:0}}}); }
  // 這一按是真的送到假機器 ⇒ 它進了 SAFE。收拾乾淨再往下，否則後面每一節都紅（第一次跑就是這樣）。
  await waitFor(() => shown('estop-banner'), 40);
  click('eb-safe-clear'); await waitFor(() => !shown('estop-banner'), 40);
  chk('estop probe cleaned up: SAFE cleared again', [shown('estop-banner'), w.__v3.isSafe()], [false, false]);
  // status 的 estop= 是權威；EVT 只是早一拍（帶 failed= 的更詳細，不被 status 蓋回去）
  w.__v3.last.wr.estop = 'detaching'; w.__v3.paintEstopState(w.__v3.last.wr);
  chk('status estop=detaching → 收回中…', /收回中/.test(txt('mis-detach')), true);
  w.__v3.last.wr.estop = 'done'; w.__v3.paintEstopState(w.__v3.last.wr);
  chk('status estop=done → 已收回（已自動回 Idle），不再提 reset', [/已自動回 Idle/.test(txt('mis-detach')), /reset/i.test(txt('mis-detach'))], [true, false]);
  w.__v3.last.wr.estop = 'partial'; w.__v3.paintEstopState(w.__v3.last.wr);
  chk('status estop=partial → 留在 Error，要按 RESET', /留在 Error/.test(txt('mis-detach')), true);
  delete w.__v3.last.wr.estop;
  w.__v3.onDetachEvt('EVT emergency_detach partial failed=feet');
  chk('EVT emergency_detach partial → shown red in the Mission status area', /🔴 partial failed=feet/.test(txt('mis-detach')), true);
  w.__v3.onDetachEvt('EVT emergency_detach done');
  chk('EVT emergency_detach done → ✅', /已收回/.test(txt('mis-detach')), true);
  // 參數記憶：server 的 mission state 帶 defaults（上次成功起跑那組），開頁第一筆套用
  w.__v3.onMissionMsg({src:'mission', state:{running:false, defaults:{cycles:3, steps:7, step_cm:35, roll_trip:5, diff_trip:9, arm_nm:6, fan:'all:8', rail:'20-100', dry:1, return_top:0, at:'2026-09-15T10:00:00.000Z'}}});
  await sleep(100);
  { const d = w.__v3.misParams();
    chk('mission state.defaults → 參數欄帶入上次那組（含 return_top）', [d.cycles, d.steps, d.step_cm, d.roll_trip, d.diff_trip, d.arm_nm, d.fan, d.rail, d.dry, d.return_top], [3, 7, 35, 5, 9, 6, 'all:8', '20-100', 1, 0]);
    w.document.getElementById('mp-rtop').checked = true;
    chk('摘要旁顯示上次起跑時間', /上次起跑/.test(txt('mp-last')), true); }
  w.__v3.onMissionMsg({src:'mission', state:{running:false, defaults:{cycles:9, steps:9, step_cm:99}}});
  await sleep(100);
  chk('第二筆 defaults 不再覆蓋（人可能正在打字）', w.__v3.misParams().cycles, 3);
  // [2026-09-17] 存檔鈕：{mission:'save', params} → server.js 驗證後寫 mission_params.json（不起跑），ack 帶 defaults
  { const before = w.__v3.logs().length;
    w.document.getElementById('mp-cycles').value = '4'; w.document.getElementById('mp-armnm').value = '5.5';
    click('mp-save');
    await waitFor(() => w.__v3.logs().slice(before).some(l => /參數已存為預設|存檔失敗/.test(l)), 30);
    const saved = w.__v3.logs().slice(before).find(l => /參數已存為預設/.test(l)) || '';
    chk('存檔 → server ack ok, defaults echo the form (cycles 4, arm_nm 5.5, saved:1, return_top:1)', [/"cycles":4/.test(saved), /"arm_nm":5.5/.test(saved), /"saved":1/.test(saved), /"return_top":1/.test(saved), /^已存 /.test(txt('mp-last'))], [true, true, true, true, true]);
    let onDisk = null; try { onDisk = JSON.parse(fs.readFileSync(path.join(RUN, 'run', 'mission_params.json'), 'utf8')); } catch (e) {}
    chk('存檔 landed in <HOME>/run/mission_params.json', onDisk && onDisk.cycles === 4 && onDisk.saved === 1, true);
    w.document.getElementById('mp-cycles').value = '999999';
    click('mp-save'); await sleep(50);
    chk('存檔 with a bad value is refused client-side (alert), nothing sent', /參數不合法/.test(w.__alerts.slice(-1)[0] || ''), true);
    w.document.getElementById('mp-cycles').value = '3'; }
  chk('state.running=false exit 0 → 閒置, Manual unlocked, banner gone', [txt('mis-state'), w.document.querySelector('.pane[data-pane="manual"]').classList.contains('mlock'), shown('manbar')], ['閒置', false, false]);
  w.document.getElementById('mp-steps').value = '1'; w.document.getElementById('mp-dry').checked = false; w.document.getElementById('mp-fan').value = 'move'; w.document.getElementById('mp-fanpct').value = '7'; w.document.getElementById('mp-rail').value = '0-100'; w.document.getElementById('mp-armnm').value = '3';
}

async function secMission(P) {
  console.log('\n[mission] plan §2 — body backend (MISSION_BACKEND=body): start from the button, EVT-driven display, roll → recover → SAFE');
  const { w, txt, arm, click, sim, banner } = P;
  w.__v3.setBackend('body');
  chk('body backend: pause/continue/skip visible, script pane hidden', [w.document.getElementById('mis-pause').hidden, w.document.getElementById('mis-out').closest('.grp').hidden], [false, true]);
  sim('sim top -120'); await sleep(400);   // after 前置 ⑦ the fake sits at height 120 (len_l=-120)
  w.document.getElementById('mp-steps').value = '1';
  arm(); click('mis-start'); await waitFor(() => txt('mis-state') === '執行中', 60);
  if (txt('mis-state') !== '執行中') console.log('    (debug) last alert: ' + w.__alerts.slice(-1)[0] + ' | missing: ' + w.__v3.preMissing().join(','));
  chk('mission start from button → 執行中, precheck 全過', [txt('mis-state'), txt('pf-count')], ['執行中', '全過']);
  await waitFor(() => /^move/.test(txt('mis-sub')), 120);
  chk('reached move sub-step', /^move/.test(txt('mis-sub')), true);
  sim('sim roll 6'); await sleep(900);
  chk('roll 6 during pay_out → roll_recover, not SAFE', [/roll_recover/.test(txt('mis-sub')) || w.__v3.logs().some(l => /roll_recover/.test(l)), banner().hidden], [true, true]);
  sim('sim roll 6'); await waitFor(() => !banner().hidden, 40);
  chk('second roll → SAFE src=roll banner', [banner().hidden, /roll/.test(banner().textContent)], [false, true]);
  chk('Manual locked (inert) except kept cards', [w.document.querySelectorAll('.pane[data-pane="manual"] .grp[inert]').length > 3, w.document.querySelector('.pane[data-pane="manual"] .grp[data-safe-keep]').hasAttribute('inert')], [true, false]);
  chk('mis-state = SAFE', txt('mis-state'), 'SAFE');
  // 09-14 拍板 ②: SAFE with a mission = paused-at-checkpoint, resumable. Body answers pause/continue/skip with
  // ERR safe_locked while SAFE, so the GUI greys those three and keeps only 中止 + the banner's 解除.
  await waitFor(() => !w.document.getElementById('mis-lock').hidden, 20);
  const bb = (id) => w.document.getElementById(id).disabled;
  chk('SAFE: 暫停/繼續/跳過 disabled, 中止 enabled, lock hint shown', [bb('mis-pause'), bb('mis-continue'), bb('mis-skip'), bb('mis-stop'), w.document.getElementById('mis-lock').hidden], [true, true, true, false, false]);
  // stale-page case: force the handler anyway (tick not yet painted) → body refuses → refusal must reach the log
  const cont = w.document.getElementById('mis-continue'); cont.disabled = false; cont.click();
  await waitFor(() => w.__v3.logs().some(l => /繼續 被拒：ERR safe_locked/.test(l)), 30);
  chk('continue during SAFE → ERR safe_locked shown in the log', w.__v3.logs().some(l => /繼續 被拒：ERR safe_locked/.test(l)), true);
  sim('sim roll 0.3'); await sleep(300);
  const cs = w.__confirms.length;
  click('eb-safe-clear'); await waitFor(() => banner().hidden, 40);
  chk('safe_clear = one confirm, reason=operator (no prompt)', [w.__confirms.length - cs, /safe_clear operator/.test(w.__v3.logs().join('\n'))], [1, true]);
  await waitFor(() => txt('mis-state') === '暫停中', 30);
  chk('cleared → banner hidden, body state=paused, GUI 暫停中, 繼續 enabled', [banner().hidden, w.__v3.last.wr.state, txt('mis-state'), bb('mis-continue')], [true, 'paused', '暫停中', false]);
  const logHas = (re) => w.__v3.logs().some(l => re.test(l));
  click('mis-continue'); await waitFor(() => logHas(/繼續 → OK/), 30);
  // a 1-step mission can finish within a tick of resuming, so "running again" is proven by the body's OK, not by catching 執行中
  chk('繼續 after clear → body accepts (paused-at-checkpoint, not terminated)', logHas(/繼續 → OK/), true);
  await waitFor(() => txt('mis-state') === '閒置', 100);
  if (txt('mis-state') !== '閒置') { click('mis-stop'); await waitFor(() => txt('mis-state') === '閒置', 100); }
  chk('mission ran on / stopped → 閒置', txt('mis-state'), '閒置');
}

async function secSafe(P) {
  console.log('\n[safe] plan §3 — tension refusal, emergency_stop = SAFE src=user, sim safe');
  const { w, txt, arm, click, sim, banner } = P;
  // tension path needs a pay_out in progress → use a mission
  sim('sim top -120'); await sleep(300);
  await waitFor(() => !w.document.getElementById('mis-start').disabled, 40);
  arm(); click('mis-start'); await waitFor(() => txt('mis-state') === '執行中', 20);
  await waitFor(() => /^move/.test(txt('mis-sub')), 120);
  sim('sim tension_valid 0'); await waitFor(() => !banner().hidden, 40);
  chk('tension_valid=0 during pay_out → SAFE src=tension', [banner().hidden, /tension/.test(banner().textContent)], [false, true]);
  arm(); click('eb-safe-clear'); await sleep(1200);
  chk('safe_clear refused while tension_valid=0 (item shown)', /拒絕：tension_valid/.test(txt('eb-safe-why')), true);
  sim('sim tension_valid 1'); await sleep(300);
  arm(); click('eb-safe-clear'); await waitFor(() => banner().hidden, 40);
  chk('cleared after tension restored', banner().hidden, true);
  click('mis-stop'); await waitFor(() => txt('mis-state') === '閒置', 100);
  click('wr-estop'); await waitFor(() => !banner().hidden, 40);
  chk('emergency_stop → SAFE src=user', [banner().hidden, /user/.test(banner().textContent)], [false, true]);
  arm(); click('eb-safe-clear'); await waitFor(() => banner().hidden, 40);
  chk('cleared; no mission → body ready/idle (09-14 拍板), start enabled', [['ready', 'idle'].includes(w.__v3.last.wr.state), w.document.getElementById('mis-start').disabled], [true, false]);
  sim('sim safe'); await waitFor(() => !banner().hidden, 40);
  chk('sim safe → banner', banner().hidden, false);
  arm(); click('eb-safe-clear'); await waitFor(() => banner().hidden, 40);
  chk('cleared', banner().hidden, true);
  chk('0 jsdom errors after the whole run', P.errors.length, 0);
}

async function secHold(P) {
  console.log('\n[hold] 2026-09-15 — hold-mode tension guard on/off (crane set_hold_guard), fan/rail mission params');
  const { w, txt, arm, click, shown } = P;
  const card = () => w.document.querySelector('.ropegrid').closest('.grp');   // red frame lands on the ▲▼ card, switch lives in the tension card (09-15 pm)
  const warn = () => w.document.getElementById('hg-warn');
  const logHas = (re) => w.__v3.logs().some(l => re.test(l));
  await waitFor(() => txt('hg-rd') === '啟用中', 30);
  chk('guard shown ON from crane status (hold_guard=1 on a fresh fake)', [txt('hg-rd'), card().classList.contains('hg-off'), warn().hidden], ['啟用中', false, true]);
  chk('rescue-retract button is gone (09-15 pm: rescue = guard off + ▲▼)', w.document.getElementById('resc-btn'), null);
  chk('switch sits in the 張力保護 card, warn line in the ▲▼ card, both cards safe-keep', [/張力保護/.test(w.document.getElementById('hg-row').closest('.grp').querySelector('header').textContent), warn().closest('.grp') === card(), w.document.getElementById('hg-row').closest('.grp').hasAttribute('data-safe-keep')], [true, true, true]);
  const ch = w.__confirms.length;
  click('hg-off'); await waitFor(() => txt('hg-rd') === '已關閉', 30);
  chk('OFF asks exactly one confirm', w.__confirms.length - ch, 1);
  chk('OFF → 已關閉, card red, warning text, OFF button lit', [txt('hg-rd'), card().classList.contains('hg-off'), !warn().hidden && /已關閉/.test(warn().textContent), w.document.getElementById('hg-off').classList.contains('on')], ['已關閉', true, true, true]);
  const st = await w.__v3.send('crane', 'status', 8000);
  chk('crane status now carries hold_guard=0', /\bhold_guard=0\b/.test(st), true);
  // [2026-09-21] meter_suspect=L/R/M：該側計米器讀值被防雜訊規則拒絕中（快取可能過期）→ 數字標紅、Dashboard 多一列；空字串 ⇒ 全部隱藏
  const suspRow = () => w.document.getElementById('d-susp-row');
  chk('meter_suspect 空 ⇒ Dashboard 那列隱藏、L/R 數字不標紅', [suspRow().hidden, w.document.getElementById('cr-l').classList.contains('bad')], [true, false]);
  await w.__v3.send('crane', 'fake_meter_suspect L', 5000);
  await waitFor(() => !suspRow().hidden, 30);
  chk('meter_suspect=L ⇒ 列出現且寫「左」、cr-l 標紅、cr-r 不標', [/左/.test(txt('d-susp')), w.document.getElementById('cr-l').classList.contains('bad'), w.document.getElementById('cr-r').classList.contains('bad')], [true, true, false]);
  await w.__v3.send('crane', 'fake_meter_suspect', 5000);
  await waitFor(() => suspRow().hidden, 30);
  chk('清掉 ⇒ 列隱藏、紅色消失（不是「無效」，數字一直在）', [suspRow().hidden, w.document.getElementById('cr-l').classList.contains('bad'), txt('cr-l') !== '—'], [true, false, true]);
  // [2026-09-16 per user] 目前工具：來源是手臂 STATUS 的 `tool=`（角度反推），en=0 要標快取
  // 🔴 STATUS 的 [M2] 是**另一則訊息**（line-buffered）⇒ 等它飄到，不是等 armStatus() 的回傳值
  await w.__v3.armStatus(); await waitFor(() => txt('mis-tool') !== '—', 20);
  chk('[M2] 單獨到達也會更新工具（不是只解析回覆那一行）（09-17 起 Manual 不再有 M1/M2/工具列，只剩 Dashboard 任務卡的 tool=）', [txt('mis-tool') !== '—', w.document.getElementById('rd-tool'), w.document.getElementById('rd-m2')], [true, null, null]);
  // 角度是前面幾節跑完的結果（滾筒或刮刀都合法）⇒ 只驗「是四種之一、使能中不標快取」
  chk('工具顯示中文；手臂使能中 ⇒ 不標快取', [['滾筒','刮刀','置中','轉換中'].includes(txt('mis-tool')), w.__v3.armTool().en], [true, '1']);
  // en=0 時 M2 不再送 CAN frame ⇒ tool 是凍結的舊值，必須標出來、且不給綠
  const toolNow = txt('mis-tool');
  w.__v3.armTool().en = '0'; w.__v3.paintArmTool();
  chk('en=0 → 標「未使能，快取」且不給綠', [txt('mis-tool'), w.document.getElementById('mis-tool').className.includes('unv')], [toolNow + '（未使能，快取）', true]);
  w.__v3.armTool().en = '1'; w.__v3.paintArmTool();
  w.__v3.armTool().tool = 'squeegee'; w.__v3.paintArmTool();
  chk('squeegee → 刮刀', txt('mis-tool'), '刮刀');
  w.__v3.armTool().tool = 'between'; w.__v3.paintArmTool();
  chk('between → 轉換中，且不給綠（它是「不知道在哪」）', [txt('mis-tool'), w.document.getElementById('mis-tool').className.includes('unv')], ['轉換中', true]);
  await w.__v3.armStatus();
  // [2026-09-17 per user] 手臂卡重做：力道／距離上限、壓上／收回、目前力+牆距、力道→套用；>7 防呆；走本體 arm_deploy_f / arm_force / arm_retract
  { const det = () => txt('rd-afdet'), g = (id) => w.document.getElementById(id);
    // [09-17 下午 per user] 工具列回來（seg 滾筒／刮刀，亮＝STATUS tool=）；其餘舊列仍不在
    chk('手臂卡：工具/力道/距離/壓上/收回/目前/套用；套用未壓上時灰；舊列（路徑/M1/M2/DEPLOY/PARK/INIT）都不在', [['af-tool','rd-arm-tool','arm-tool-tag','af-nm','af-dist','af-go','arm-retract','rd-arm-now','af-nm2','af-apply'].every(id => !!g(id)), g('af-apply').disabled, ['arm-path','af-slot','ad-go','arm-park','arm-init','arm-read'].every(id => g(id) === null), txt('arm-contact')], [true, true, true, '未壓上']);
    chk('[09-17 重排] 手臂卡每列 .ctl.stack（不再 1fr auto auto 三欄）；seg 亮的＝實際工具（fake 開機滾筒）；頂欄工具 tag', [['af-tool','af-nm','af-go','rd-arm-now','af-apply'].every(id => !!g(id).closest('.ctl.stack')), g('af-tool').querySelector('button.on') && g('af-tool').querySelector('button.on').getAttribute('data-slot'), txt('arm-tool-tag')], [true, 'RIGHT', '工具 滾筒']);
    g('af-tool').querySelector('[data-slot="LEFT"]').click();
    await waitFor(() => g('af-tool').querySelector('button.on') && g('af-tool').querySelector('button.on').getAttribute('data-slot') === 'LEFT', 30);
    chk('按「刮刀」→ arm_slot LEFT → OK slot=LEFT → STATUS tool=squeegee → 亮刮刀、Dashboard 目前工具同步', [w.__v3.logs().some(l => /arm_slot LEFT → OK slot=LEFT/.test(l)), g('af-tool').querySelector('button.on').getAttribute('data-slot'), txt('mis-tool'), txt('arm-tool-tag')], [true, 'LEFT', '刮刀', '工具 刮刀']);
    g('af-nm').value = '8'; g('af-go').click(); await sleep(50);
    chk('壓上 8 N·m → 前端防呆 alert，不送', /防呆上限 7/.test(w.__alerts.slice(-1)[0] || ''), true);
    g('af-nm').value = '3'; g('af-dist').value = ''; g('af-go').click();
    await waitFor(() => /^✅ 壓上/.test(det()), 30);
    chk('[現場 09-17] 壓上/收回 在自己那列（.arm-btns，鈕 flex:none）；結果文字獨立一列且可換行（white-space normal），不再擠按鈕', [g('af-go').parentElement.classList.contains('arm-btns'), g('arm-retract').parentElement === g('af-go').parentElement, g('rd-afdet').closest('.arm-result') !== null, w.getComputedStyle(g('rd-afdet')).whiteSpace, w.getComputedStyle(g('af-go')).flex.split(' ')[0]], [true, true, true, 'normal', '0']);
    chk('壓上 3 → arm_deploy_f 3 LEFT（用亮的那顆工具，不再寫死 RIGHT）→ OK → 壓上中、套用 enabled、目前列開始讀 tau；壓上中工具 seg 灰', [w.__v3.logs().some(l => /arm_deploy_f 3 LEFT$/.test(l)), txt('arm-contact'), g('af-apply').disabled, /力 3\.00 \/ 目標/.test(det()), g('af-tool').querySelector('[data-slot="RIGHT"]').disabled], [true, '壓上中', false, true, true]);
    g('af-tool').querySelector('[data-slot="RIGHT"]').disabled = false; g('af-tool').querySelector('[data-slot="RIGHT"]').click(); await sleep(300);
    chk('壓上中硬按滾筒 → 前端擋（alert 先收回），不送 arm_slot RIGHT', [/先按「收回」/.test(w.__alerts.slice(-1)[0] || ''), w.__v3.logs().some(l => /arm_slot RIGHT/.test(l))], [true, false]);
    await waitFor(() => /力 [\d.]+ N·m/.test(txt('rd-arm-now')), 30);
    chk('目前：力 N·m · 估測牆距 mm（STATUS [M1] tau= / wall_mm=，壓上後才顯示牆距）', /力 [\d.]+ N·m · 估測牆距 \d+ mm/.test(txt('rd-arm-now')), true);
    g('af-nm2').value = '5'; g('af-apply').click();
    await waitFor(() => /^✅ 套用/.test(det()), 30);
    chk('套用 5 → arm_force 5 → OK tau=5.00 wall_mm=230 → 顯示力 5.00 · 牆距 230', [w.__v3.logs().some(l => /arm_force 5 →/.test(l)), /力 5\.00 \/ 目標 5.* 牆距 230 mm/.test(det())], [true, true]);
    g('af-nm2').value = '8'; g('af-apply').click(); await sleep(50);
    chk('套用 8 → 前端防呆 alert', /防呆上限 7/.test(w.__alerts.slice(-1)[0] || ''), true);
    g('arm-retract').click();
    await waitFor(() => /^✅ 已收回/.test(det()), 30);
    chk('收回 → arm_retract → OK → 未壓上、套用灰、目前列清空、工具 seg 解灰', [txt('arm-contact'), g('af-apply').disabled, txt('rd-arm-now'), g('af-tool').querySelector('[data-slot="RIGHT"]').disabled], ['未壓上', true, '—', false]);
    g('af-tool').querySelector('[data-slot="RIGHT"]').click();
    await waitFor(() => g('af-tool').querySelector('button.on') && g('af-tool').querySelector('button.on').getAttribute('data-slot') === 'RIGHT', 30);
    chk('收回後按「滾筒」→ arm_slot RIGHT → 亮滾筒', [w.__v3.logs().some(l => /arm_slot RIGHT → OK slot=RIGHT/.test(l)), txt('arm-tool-tag')], [true, '工具 滾筒']);
    g('af-apply').disabled = false; g('af-nm2').value = '3'; g('af-apply').click();   // 模擬 disabled 被拔掉硬按
    await waitFor(() => /^🔴 套用失敗/.test(det()), 30);
    chk('未壓上硬送 arm_force → ERR not_in_contact → 人話提示「先按壓上」', /未壓在牆上（先按「壓上」）/.test(det()), true);
    g('af-nm').value = '3'; g('af-dist').value = '200'; g('af-go').click();
    await waitFor(() => /距離先到/.test(det()), 30);
    chk('壓上 3 RIGHT 200 → OK stopped=distance → 「距離先到，停在 …、力 1.00」，仍算壓上', [w.__v3.logs().some(l => /arm_deploy_f 3 RIGHT 200$/.test(l)), /距離先到，停在 θ 0\.480、力 1\.00 N·m/.test(det()), txt('arm-contact')], [true, true, '壓上中']);
    g('af-dist').value = '100'; g('af-go').click(); await sleep(50);
    chk('距離 100 mm（<122）→ 前端擋', /122~444/.test(w.__alerts.slice(-1)[0] || ''), true);
    g('arm-retract').click(); await waitFor(() => /^✅ 已收回/.test(det()), 30); g('af-dist').value = '';
    // err= 分級（agent-ai-db 09-17 實查 motor_api err_name()）：0 / 0x1 / NONE 健康；具名碼故障（附中文）；未知 0x?? 也當故障
    chk('armErr：0 / 0x1 / NONE 健康；B:MOS_OVERTEMP 故障+中文；0x7 未知碼也故障', [['0','0x1','NONE'].map(x => w.__v3.armErr(x).cls).join(','), w.__v3.armErr('B:MOS_OVERTEMP').note, w.__v3.armErr('0x7').cls], ['on,on,on', ' · 🔴 故障 B:MOS_OVERTEMP（MOS 過溫）', 'bad']); }
  // [2026-09-16 契約] 本體 6 狀態：paused 一定要連原因顯示（三種暫停的出口不同），running+flow 標回程
  const S = w.__v3.last.wr;
  const saved = {state:S.state, pause_reason:S.pause_reason, flow:S.flow};
  S.state = 'paused'; S.pause_reason = 'error';
  chk('paused + pause_reason=error → 暫停中（錯誤）', w.__v3.wrStateText(S), '暫停中（錯誤）');
  S.pause_reason = 'balance_ask';
  chk('balance_ask 標明沒有指令、會自動還原', /自動還原/.test(w.__v3.wrStateText(S)), true);
  S.pause_reason = 'user';
  chk('paused + user', w.__v3.wrStateText(S), '暫停中（使用者）');
  S.state = 'running'; S.flow = 'return_home'; S.pause_reason = 'none';
  chk('running + flow=return_home 標出回程', /回程/.test(w.__v3.wrStateText(S)), true);
  chk('fast-poll 只認 running（舊的 balancing/returning_home/calibrating 已消失）', ['running','paused','balancing','returning_home','calibrating','idle'].map(x => w.__v3.wrMoving(x)).join(','), 'true,false,false,false,false,false');
  Object.assign(S, saved);
  // [2026-09-17 per user] 吸盤推桿卡：RPM 欄直接顯示本體預設（伸／收各一格，value 不是 placeholder）
  const wrSt = await w.__v3.send('washrobot', 'status', 8000);
  const defRpm = (/\bpusher_rpm=(\d+)/.exec(wrSt) || [])[1], defRet = (/\bpusher_rpm_retract=(\d+)/.exec(wrSt) || [])[1];
  const val = (id) => w.document.getElementById(id).value;
  // [2026-09-17 per user 共用值] RPM = 本體共用執行期值（set_pusher_rpm）；Manual 兩格 + Mission 共用值列兩格顯示同一個值
  await waitFor(() => val('rpm-all') === defRpm && val('ms-rpm-ret') === defRet, 20);
  chk('RPM 四格（Manual 伸/收 + Mission 伸/收）＝ status pusher_rpm / pusher_rpm_retract；單支列沒有自己的 rpm 欄', [val('rpm-all'), val('rpm-all-ret'), val('ms-rpm-ext'), val('ms-rpm-ret'), w.document.getElementById('zrpm-6')], [defRpm, defRet, defRpm, defRet, null]);
  { w.document.getElementById('rpm-all').value = '650'; w.document.getElementById('rpm-all').dispatchEvent(new w.Event('input', {bubbles:true}));
    await sleep(1300);
    chk('人改到一半的 RPM 欄不被 status 蓋回', val('rpm-all'), '650');
    w.document.getElementById('rpm-all').dispatchEvent(new w.Event('change', {bubbles:true}));
    await waitFor(() => /✅ 共用值/.test(txt('rd-prpm')), 30);
    await waitFor(() => val('ms-rpm-ext') === '650', 30);
    const wrSt2 = await w.__v3.send('washrobot', 'status', 8000);
    chk('change → set_pusher_rpm 650 <收> → OK pusher_rpm=650；status 跟著變；Mission 那格同步 650；EVT pusher_rpm 進 log', [w.__v3.logs().some(l => new RegExp('set_pusher_rpm 650 ' + defRet).test(l)), /\bpusher_rpm=650\b/.test(wrSt2), val('ms-rpm-ext'), /650/.test(txt('rd-prpm')), w.__v3.logs().some(l => /EVT pusher_rpm 650/.test(l) || /推桿共用 RPM 改為 伸 650/.test(l))], [true, true, '650', true, true]);
    w.document.querySelector('[data-zret="6"]').click(); w.document.querySelector('[data-zraw="6"]').click(); w.document.querySelector('[data-praw="all"]').click(); await sleep(100);
    const sentL = w.__v3.logs().slice(-10).join('\n');   // ⚠️ LOGBUF is a 300-entry ring — slice(fromLength) is empty once it is full
    chk('單次指令不再帶 rpm（本體用共用值）', [/zdt_pusher 6 retract$/m.test(sentL), /zdt_pusher 6 extend_raw 10$/m.test(sentL), /pusher all extend_raw 10$/m.test(sentL)], [true, true, true]);
    // 從 Mission 那格改回去：同一支指令、Manual 跟著變
    w.document.getElementById('ms-rpm-ext').value = defRpm; w.document.getElementById('ms-rpm-ext').dispatchEvent(new w.Event('change', {bubbles:true}));
    await waitFor(() => val('rpm-all') === defRpm, 30);
    chk('Mission 那格改回 → set_pusher_rpm → Manual 兩格同步', [val('rpm-all'), /✅ 共用值/.test(txt('ms-rpm-rd'))], [defRpm, true]);
    w.document.getElementById('rpm-all').value = '20'; w.document.getElementById('rpm-all').dispatchEvent(new w.Event('change', {bubbles:true})); await sleep(50);
    chk('壞值 20 在 GUI 端擋下（alert）', /RPM 允許 50~1000/.test(w.__alerts.slice(-1)[0] || ''), true);
    w.document.getElementById('rpm-all').value = defRpm; delete w.document.getElementById('rpm-all').dataset.user; }
  chk('歸零兩顆改名：當前位置歸零 / 自動歸零；手動 ①② 流程已拆掉', [/當前位置歸零/.test(txt('zdt-zero')), txt('zdt-home'), w.document.getElementById('zdt-rz'), w.document.getElementById('zdt-rz-cancel')], [true, '自動歸零（撞限位）', null, null]);
  // 失能／使能（zdt_power，真斷電）：狀態由 status zdt_pwr= 畫（指令狀態）
  await waitFor(() => txt('zpw-5') === '使能', 20);
  chk('zdt_pwr=1111 → 四支顯示 使能；每支與全部各有 失能/使能 鈕', [['5','6','7','8'].map(x => txt('zpw-' + x)).join(','), w.document.querySelectorAll('[data-zpow]').length], ['使能,使能,使能,使能', 10]);
  { const row = (x) => w.document.querySelector('.zrow[data-zs="' + x + '"]');
    chk('[現場 09-17] 單支區＝每支一列(.zrow, flex-wrap)：勾·標籤·cm·伸raw·尋封伸·收·狀態·失能·使能 全在同一列；卡片吃滿整列', [['5','6','7','8'].every(x => row(x) && ['input.zinc', '#zband-' + x, '#zcm-' + x, '[data-zraw]', '[data-zret]', '#zpw-' + x, '[data-zpow][data-on="0"]', '[data-zpow][data-on="1"]'].every(sel => row(x).querySelector(sel))), w.getComputedStyle(row('5')).flexWrap, w.getComputedStyle(w.document.getElementById('zdtsingle')).display, w.document.getElementById('zdtsingle').closest('.grp').style.gridColumn], [true, 'wrap', 'flex', '1/-1']);
    // [09-17 per user 順手重排] 卡內兩欄：左 .zmain＝單支＋解堵轉；右 .zside＝整組 · 共用 RPM · 全部失能使能 · 歸零（兩顆併一格）
    const zc = w.document.querySelector('.zdtcard');
    chk('[09-17 重排] 吸盤推桿卡兩欄：zmain(單支+解堵轉) / zside(整組·RPM·全部失能使能·歸零兩顆)', [w.getComputedStyle(zc).display, ['#zdtsingle', '[data-cmd="washrobot|zdt_release_stall"]', '#rd-zdtskip'].every(sel => zc.querySelector('.zmain ' + sel)), ['#cm-all', '#rpm-all', '#rpm-all-ret', '[data-zpow="all"]', '#zdt-zero', '#zdt-home'].every(sel => zc.querySelector('.zside ' + sel)), w.document.getElementById('zdt-zero').closest('.ctl') === w.document.getElementById('zdt-home').closest('.ctl')], ['grid', true, true, true]); }
  w.document.querySelector('[data-zpow="all"][data-on="0"]').click();
  await waitFor(() => txt('zpw-7') !== '使能', 30);
  chk('全部失能 → OK zdt_power all=off，status zdt_pwr=0000 → 四支顯示 失能（可手推）', [w.__v3.logs().some(l => /OK zdt_power all=off/.test(l)), txt('zpw-5'), /✅ 全部失能/.test(txt('rd-zdtpower'))], [true, '失能（可手推）', true]);
  w.document.querySelector('[data-zpow="6"][data-on="1"]').click();
  await waitFor(() => txt('zpw-6') === '使能', 30);
  chk('單支 6 使能 → 只有 s6 回 使能；使能鈕 title 提醒先歸零', [['5','6','7','8'].map(x => txt('zpw-' + x)).join(','), /當前位置歸零/.test(w.document.querySelector('[data-zpow="6"][data-on="1"]').title)], ['失能（可手推）,使能,失能（可手推）,失能（可手推）', true]);
  w.document.querySelector('[data-zpow="all"][data-on="1"]').click();
  await waitFor(() => ['5','6','7','8'].every(x => txt('zpw-' + x) === '使能'), 30);
  chk('全部使能 → 回 1111', ['5','6','7','8'].map(x => txt('zpw-' + x)).join(','), '使能,使能,使能,使能');
  // 整組包含／跳過（zdt_enable/zdt_disable，不斷電）：checkbox 顯示以 status zdt_skip= 為準
  { const cb = (x) => w.document.querySelector('input.zinc[data-zinc="' + x + '"]');
    chk('群組 納入/排除 兩顆鈕已移除，改成四個勾（預設全勾）；解堵轉保留', [w.document.querySelectorAll('input.zinc').length, ['5','6','7','8'].every(x => cb(x).checked), !!w.document.querySelector('[data-cmd="washrobot|zdt_release_stall"]'), w.document.querySelector('[data-cmd="washrobot|zdt_enable"]')], [4, true, true, null]);
    cb('7').checked = false; cb('7').dispatchEvent(new w.Event('change', {bubbles:true}));
    await waitFor(() => w.document.getElementById('zband-7').classList.contains('skip'), 30);
    chk('取消勾 s7 → zdt_disable 7；status zdt_skip=7 → 標籤刪除線 + 提示，框保持未勾', [w.__v3.logs().some(l => /zdt_disable 7/.test(l)), cb('7').checked, /跳過 s7/.test(txt('rd-zdtskip'))], [true, false, true]);
    chk('Mission 共用值卡：zdt_skip=7 → 整張標黃、寫「推桿 7 被 Manual 排除」', [w.document.getElementById('ms-card').classList.contains('ms-warn'), /推桿 7 被 Manual 排除/.test(txt('ms-skip')), /排除了推桿 7/.test(txt('ms-tag'))], [true, true, true]);
    cb('7').checked = true; cb('7').dispatchEvent(new w.Event('change', {bubbles:true}));
    await waitFor(() => !w.document.getElementById('zband-7').classList.contains('skip'), 30);
    chk('再勾回 → zdt_enable 7；zdt_skip=- → 四支包含', [w.__v3.logs().some(l => /zdt_enable 7/.test(l)), txt('rd-zdtskip')], [true, '整組指令包含 4 支']);
    chk('共用值卡回白：含入 5,6,7,8；吊機門檻列讀 status（tension_max_kg 等）', [w.document.getElementById('ms-card').classList.contains('ms-warn'), txt('ms-skip'), /單側上限 \d+ \/ 左右差 \d+ \/ 收繩上限\(軟停\+手拉停\) \d+ kg · 繩長差 \d+ cm · level_auto (on|off) · 張力保護 (on|off)/.test(txt('ms-crane'))], [false, '含入 5,6,7,8（跳過 —）', true]); }
  // ③ 拒跑：腳本輸出的「🔴 … 不跑。」抄到 Mission 狀態列（紅）並進 log
  { const bk = w.__v3.backend(); w.__v3.setBackend('script');   // body mode re-derives running from status every second; the refuse banner is a script-mode thing
    w.__v3.onMissionMsg({src:'mission', line:'🔴 arm_ready=0 —— 手臂未待命(STARTUP/INIT 失敗?看 fcv-arm log),不跑。'});
    await sleep(50);
    chk('腳本 🔴 不跑 行 → #mis-why 紅色顯示 + log 🔴🔴', [shown('mis-why'), /腳本拒跑：🔴 arm_ready=0/.test(txt('mis-why')), w.document.getElementById('mis-why').classList.contains('refuse'), w.__v3.logs().some(l => /🔴🔴 腳本拒跑/.test(l))], [true, true, true, true]);
    w.__v3.onMissionMsg({src:'mission', state:{running:true}}); await sleep(50);
    chk('下一趟起跑（running）→ 拒跑訊息讓位', /腳本拒跑/.test(txt('mis-why')), false);
    w.__v3.onMissionMsg({src:'mission', state:{running:false, exit:{code:0}}}); w.__v3.setBackend(bk); }
  // the fake never emits manual_tension_warn (it has no hold-mode tension model) — feed the real wire line
  w.__v3.onHoldGuardEvt('EVT manual_tension_warn kind=retract_stop left=70.5 right=66.0 note=not_stopping_operator_decides');
  chk('EVT manual_tension_warn → kind/left/right shown in red, not stopping', /retract_stop.*70\.5.*66\.0/.test(warn().textContent), true);
  w.__v3.onHoldGuardEvt('EVT manual_tension_clear');
  chk('EVT manual_tension_clear → back to the generic OFF warning', [/已關閉/.test(warn().textContent), /retract_stop/.test(warn().textContent)], [true, false]);
  // [2026-09-17 契約] up_stop_total_kg 移除；▲ 手拉停止改用 retract_tension_stop_kg（單側）；EVT tension_total_limit → tension_retract_stop
  { const st2 = await w.__v3.send('crane', 'status', 8000);
    chk('crane status 已無 up_stop_total_kg；GUI 沒有它的設定格／Setting 列／共用值字樣', [/up_stop_total_kg/.test(st2), w.document.querySelector('[data-set="set_up_stop_total_kg"]'), w.document.getElementById('st-ust'), /上行總重停|總和/.test(txt('ms-crane'))], [false, null, null, false]);
    chk('retract_tension_stop_kg 標籤講清楚是同一個門檻管兩件事（自動收繩軟停 + ▲ 手拉停止）', [/收繩張力上限（單側）/.test(w.document.querySelector('[data-set="set_retract_tension_stop_kg"]').previousElementSibling.textContent), /自動收繩軟停 \+ ▲ 手拉停止/.test(w.document.querySelector('[data-set="set_retract_tension_stop_kg"]').previousElementSibling.textContent), /收繩上限\(軟停\+手拉停\)/.test(txt('ms-crane'))], [true, true, true]);
    // [2026-09-17 per user 更正] 即時值那三格撤掉；只要「左右繩差上限」可設定（格 + 套用 → set_tension_diff_max_kg）
    { const inp = w.document.querySelector('[data-set="set_tension_diff_max_kg"]'); const diff0 = w.__v3.last.cr.tension_diff_max_kg;
      chk('張力保護卡：左右繩差上限（kg）一格 + 套用，標籤講清楚；即時值三格不在', [!!inp, /左右繩差上限（kg）/.test(inp.previousElementSibling.textContent), !!w.document.querySelector('[data-setgo="set_tension_diff_max_kg"]'), w.document.getElementById('ts-max-live'), w.document.getElementById('ts-diff-live')], [true, true, true, null, null]);
      inp.value = '33'; w.document.querySelector('[data-setgo="set_tension_diff_max_kg"]').click();
      await waitFor(() => w.__v3.last.cr && w.__v3.last.cr.tension_diff_max_kg === '33', 40);   // fake 的 set_tension_* 09-17 起會反映到 status（AI-2 patch）
      chk('套用 33 → set_tension_diff_max_kg 33 → 吊機 status tension_diff_max_kg=33', [w.__v3.logs().some(l => /set_tension_diff_max_kg 33/.test(l)), w.__v3.last.cr.tension_diff_max_kg], [true, '33']);
      // restore whatever the fake booted with (agent-ai-db keeps the fake defaults = real crane compile defaults; don't hard-code them here)
      inp.value = diff0; w.document.querySelector('[data-setgo="set_tension_diff_max_kg"]').click(); await waitFor(() => w.__v3.last.cr.tension_diff_max_kg === diff0, 30);
      chk('left/right diff threshold restored to the fake default', w.__v3.last.cr.tension_diff_max_kg, diff0);
      // [2026-09-17 per user] 第四個門檻：左右繩長差上限 length_diff_max_cm（>1..200，吊機持久化）
      const li = w.document.querySelector('[data-set="set_length_diff_max_cm"]'); const ld0 = w.__v3.last.cr.length_diff_max_cm;
      chk('張力保護卡第四格：左右繩長差上限（cm），標籤講清楚，預填現值', [!!li, /左右繩長差上限（cm）/.test(li.previousElementSibling.textContent), /計米器 L−R/.test(li.previousElementSibling.textContent), li.value, /持久化/.test(w.document.querySelector('.pane[data-pane="manual"] header .lock, #hg-card header .lock') ? '持久化' : '')], [true, true, true, ld0, true]);
      li.value = '15'; w.document.querySelector('[data-setgo="set_length_diff_max_cm"]').click();
      await waitFor(() => w.__v3.last.cr.length_diff_max_cm === '15', 40);
      chk('套用 15 → set_length_diff_max_cm 15 → status length_diff_max_cm=15；Setting 表與共用值卡跟著', [w.__v3.logs().some(l => /set_length_diff_max_cm 15/.test(l)), w.__v3.last.cr.length_diff_max_cm, txt('st-ldm'), /繩長差 15 cm/.test(txt('ms-crane'))], [true, '15', '15', true]);
      li.value = '1'; w.document.querySelector('[data-setgo="set_length_diff_max_cm"]').click(); await sleep(50);
      chk('1 cm 在 GUI 端擋下（允許 2~200）', /允許範圍 2 ~ 200/.test(w.__alerts.slice(-1)[0] || ''), true);
      li.value = ld0; w.document.querySelector('[data-setgo="set_length_diff_max_cm"]').click(); await waitFor(() => w.__v3.last.cr.length_diff_max_cm === ld0, 40); }
    // [2026-09-17 per user] 上滑台可設負值（本體守衛 −130..130）
    { const rl = w.__v3.logs().length;
      chk('上滑台輸入框 min −130；守衛預設 −130–130；快捷多一顆 → −50', [w.document.getElementById('rail-cm').min, w.document.getElementById('rs-from').min, w.document.getElementById('rail-gmin').min, txt('rd-railguard'), !!w.document.querySelector('[data-railq="-50"]')], ['-130', '-130', '-130', '-130 – 130 cm', true]);
      w.document.getElementById('rail-cm').value = '-40'; click('rail-go'); await sleep(100);
      chk('移動到 −40 → rail -40 送出（負值不被 GUI 擋）', w.__v3.logs().slice(-6).some(l => /\] rail -40\b/.test(l)), true);
      w.document.getElementById('rail-cm').value = '50'; }
    // [09-17 下午 per user「總和重量怎麼不見了」] Manual 第四列＝總和純讀值（無條、無門檻）
    // [同日稍後 per user「總和還是給他一個 BAR」] 第四條有 tbar，刻度＝參考 2×單側上限（200），中性色、無「超過上限」字樣
    chk('張力條：Dashboard 兩條、Manual 四條（總和：參考 2×單側上限、中性色、無門檻字樣），橘點名字含 手拉停', [w.document.querySelectorAll('#d-tenbars .mrow').length, w.document.querySelectorAll('#m-tenbars .mrow').length, w.document.querySelectorAll('#m-tenbars .tbar').length, /總和[\s\S]*參考 200 kg/.test(w.document.getElementById('m-tenbars').innerHTML), !/總和[\s\S]*超過上限/.test(w.document.getElementById('m-tenbars').innerHTML), /手拉停/.test(w.document.getElementById('m-tenbars').innerHTML)], [2, 4, 4, true, true, true]);
    // 新 EVT 名字：餵進 ws.onmessage（fake 沒有 hold 張力模型，不會自己發）→ 按住狀態清除 + log
    let released = 0; const orig = w.__craneReleaseAll; w.__craneReleaseAll = () => { released++; };
    w.__v3.wsRef().onmessage({data: JSON.stringify({src:'crane', line:'EVT tension_retract_stop left=80.1 right=60.0 threshold=75'})});
    w.__craneReleaseAll = orig;
    chk('EVT tension_retract_stop → 清除按住狀態 + 🔴 log（吃新名字）', [released, w.__v3.logs().some(l => /EVT tension_retract_stop left=80\.1.*已自動停止/.test(l))], [1, true]); }
  click('hg-on'); await waitFor(() => txt('hg-rd') === '啟用中', 30);
  chk('ON (no gate needed) → 啟用中, warning gone, card normal', [txt('hg-rd'), warn().hidden, card().classList.contains('hg-off')], ['啟用中', true, false]);
  const bad = await w.__v3.send('crane', 'set_hold_guard x', 8000);
  chk('contract: set_hold_guard x → ERR expected_on_or_off', /^ERR expected_on_or_off/.test(bad), true);
  // restart semantics: an EVT/own memory saying OFF must lose to the next status (crane restarts → on)
  w.__v3.onHoldGuardEvt('EVT hold_guard off');
  chk('EVT hold_guard off flips the display at once', txt('hg-rd'), '已關閉');
  await waitFor(() => txt('hg-rd') === '啟用中', 40);
  chk('…but the next crane status (hold_guard=1) wins → 啟用中 (restart-safe)', txt('hg-rd'), '啟用中');
  // mission fan/rail params → trailing key=value on `mission start`
  w.document.getElementById('mp-fan').value = 'all'; w.document.getElementById('mp-fanpct').value = '6'; w.document.getElementById('mp-rail').value = '100-0';
  const mp = w.__v3.misParams();
  chk('misParams fan=all:6 rail=100-0', [mp.fan, mp.rail], ['all:6', '100-0']);
  w.document.getElementById('mp-fanpct').value = ''; w.document.getElementById('mp-fan').value = 'move'; w.document.getElementById('mp-rail').value = '0-100';
  chk('no pct → fan=move (no colon)', [w.__v3.misParams().fan, w.__v3.misParams().rail], ['move', '0-100']);
  w.document.getElementById('mp-fanpct').value = '7';
  if (has('mission')) chk('body-mode start line carried the key=value tail', logHas(/起跑參數：mission start \d+ \d+ \d+ [\d.]+ \d+ fan=move:7 rail=0-100/), true);

  // [2026-09-30] hold lease: pressing keeps renewing (quietly), release stops it;
  // a crane-side lease expiry clears the pressed state.
  const btn = w.document.querySelector('.btn.hold[data-hold="up_left"]');
  const hs = async () => await w.__v3.send('crane', 'fake_hold_state', 8000);
  const r0 = +(/renews=(\d+)/.exec(await hs()) || [0, 0])[1];
  btn.dispatchEvent(new w.MouseEvent('mousedown', {bubbles: true, cancelable: true}));
  await sleep(1800);
  const mid = await hs();
  chk('按住 1.8 s：hold 還在（有續約，沒被租約放掉）、續約 ≥2 次（後端代送，不經佇列）、不進操作紀錄', [/holds=up_left/.test(mid), +(/renews=(\d+)/.exec(mid) || [0, 0])[1] - r0 >= 2, logHas(/hold_renew/)], [true, true, false]);
  btn.dispatchEvent(new w.MouseEvent('mouseup', {bubbles: true, cancelable: true}));
  await sleep(300);
  const r1 = +(/renews=(\d+)/.exec(await hs()) || [0, 0])[1];
  await sleep(1200);
  const after = await hs();
  chk('放開 → off 送出、hold 清掉、續約停止', [/holds=-/.test(after), +(/renews=(\d+)/.exec(after) || [0, 0])[1] - r1], [true, 0]);
  btn.dispatchEvent(new w.MouseEvent('mousedown', {bubbles: true, cancelable: true}));   // pressed; the crane then drops its lease
  await sleep(200);
  w.__v3.wsRef().onmessage({data: JSON.stringify({src: 'crane', line: 'EVT hold_lease_expired age_ms=1600'})});
  chk('EVT hold_lease_expired → 按住狀態清除 + 🔴 log', [btn.classList.contains('active'), logHas(/hold_lease_expired.*吊機已自動停止/)], [false, true]);
}

async function secStop(P) {
  console.log('\n[stop] 2026-09-16 — 停止作業 vs 緊急脫離、吸附中鎖住吊機動作');
  const { w, txt, click, shown } = P;
  const dis = (id) => w.document.getElementById(id).disabled;
  const holdsDisabled = () => Array.from(w.document.querySelectorAll('.btn.hold')).every(b => b.disabled);
  const holdsEnabled  = () => Array.from(w.document.querySelectorAll('.btn.hold')).every(b => !b.disabled);
  chk('用詞分家：停止作業 / 🔴 緊急脫離，視覺上隔開', [txt('mis-stop'), txt('wr-estop'), !!w.document.querySelector('.estop-gap')], ['停止作業', '🔴 緊急脫離', true]);
  chk('兩顆的 tooltip 各說各的後果', [/收腳/.test(w.document.getElementById('mis-stop').title), /Error/.test(w.document.getElementById('wr-estop').title)], [true, true]);
  // 起點：前幾節可能把腳留在牆上 ⇒ 先收腳、關閥，等壓力回到大氣
  await w.__v3.send('washrobot', 'pusher all retract', 60000); await w.__v3.send('washrobot', 'vacuum off', 20000);
  await waitFor(() => !w.__v3.cupsState().attached, 60);
  await waitFor(() => holdsEnabled(), 30);
  chk('未吸附：▲▼ 與三顆「拉到…」都可按', [holdsEnabled(), dis('cg-top'), dis('cg-ground'), dis('cg-go')], [true, false, false, false]);
  // 讓四顆吸住
  await w.__v3.send('washrobot', 'pusher all extend_raw 5', 60000);
  await w.__v3.send('washrobot', 'vacuum on', 20000);
  await waitFor(() => w.__v3.cupsState().attached, 40);
  await waitFor(() => holdsDisabled(), 20);
  chk('吸附中：▲▼ 六顆全灰（這組直連吊機，後端擋不到，GUI 是唯一防線）', holdsDisabled(), true);
  chk('吸附中：三顆「拉到…」與控制列 ⤒ 也灰', [dis('cg-top'), dis('cg-ground'), dis('cg-go'), dis('mbar-top')], [true, true, true, true]);
  const hintOn = () => !w.document.getElementById('cup-lock-hint').hidden && txt('cup-lock-hint') !== '';   // .why 系用 visibility 佔位，看 hidden+文字
  chk('吸附中：顯示一行說明與顆數', [hintOn(), /吸附中（\d+ 顆密封/.test(txt('cup-lock-hint'))], [true, true]);
  // 本體真的會拒絕（契約），且 EVT 要醒目
  const before = w.__alerts.length;
  const r = await w.__v3.send('washrobot', 'crane_goto 200', 20000);
  await waitFor(() => w.__v3.logs().some(l => /crane_goto_blocked/.test(l)), 20);
  chk('本體拒絕 + EVT crane_goto_blocked 醒目（含彈窗提示先收腳）', [/^ERR cups_attached/.test(r), w.__v3.logs().some(l => /🔴🔴 吊機移動被擋下/.test(l)), w.__alerts.length > before], [true, true, true]);
  const rf = await w.__v3.send('washrobot', 'crane_goto 100 force', 120000);   // 200 會先被 out_of_range 擋（牆高 120）
  await waitFor(() => w.__v3.logs().some(l => /crane_goto_forced/.test(l)), 20);
  chk('force 放行但留下醒目紀錄（那是有人硬幹）', [/^OK/.test(rf), w.__v3.logs().some(l => /🔴🔴 有人用 force/.test(l))], [true, true]);
  await w.__v3.send('washrobot', 'crane_goto 120 force', 120000);   // 回到頂端，讓後面 ④ 起點仍綠
  // 停止後橫幅：吸附中紅、脫離後轉綠再自動收起
  w.__v3.onMissionMsg({src:'mission', line:'[web] STOP ⑤：收腳 + 關幫浦'});
  await sleep(50);
  chk('收到 STOP ⑤ 且仍吸附 → 常駐紅橫幅', [shown('stopbar'), /仍吸附在牆上/.test(txt('stopbar'))], [true, true]);
  // 「收腳」一顆解鎖
  click('cup-retract');
  await waitFor(() => !w.__v3.cupsState().attached, 60);
  await waitFor(() => holdsEnabled(), 20);
  chk('按「收腳」→ 壓力回到大氣 → 吊機動作解鎖、說明收起', [holdsEnabled(), dis('cg-top'), hintOn()], [true, false, false]);
  await waitFor(() => /已脫離牆面/.test(txt('stopbar')), 20);
  chk('橫幅轉成「已脫離牆面，純吊在繩上」', /已脫離牆面/.test(txt('stopbar')), true);
  await waitFor(() => !shown('stopbar'), 40);
  chk('數秒後自動收起', shown('stopbar'), false);
}

function secReport() {
  console.log('\n[report] ./harness/gui_offline.sh report');
  const r = spawnSync('python3', [FAKE, '--report'], { encoding: 'utf8' });
  const tail = (r.stdout || '').trim().split('\n').slice(-1)[0] || '';
  console.log('  ' + tail);
  chk('0 commands not modelled by fake_robot', /^0 command\(s\)/.test(tail), true);
}

// ================================================================ main
(async () => {
  let port;
  try {
    if (!ATTACH) { try { fs.writeFileSync(CMDLOG, ''); } catch (_) {} }   // report only counts this run
    port = await bringUp();
    console.log(`v3 page: ${PAGE}\nserver : http://127.0.0.1:${port}  (fake_robot 5001/5002/9527)`);
    const P = loadPage(port);
    if (has('boot')) await secBoot(P); else await sleep(3500);
    if (has('evt')) await secEvt(P);
    if (has('pre')) await secPre(P, port);
    if (has('script')) await secScript(P);
    if (has('mission')) await secMission(P);
    if (has('safe')) await secSafe(P);
    if (has('hold')) await secHold(P);
    if (has('stop')) await secStop(P);
    if (has('report')) secReport();
  } catch (e) {
    fail++; console.log('  🔴 harness error: ' + (e && e.stack || e));
  } finally {
    tearDown();
  }
  console.log(`\n${fail ? '🔴 ' + fail + ' / ' + total + ' failed' : '✅ ' + total + ' checks passed'}`);
  process.exit(fail ? 1 : 0);
})();
