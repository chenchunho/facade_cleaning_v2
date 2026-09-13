#!/usr/bin/env python3
"""fake_robot — stand-in for washrobot (:5001), crane (:5002) and arm (:9527)
so the web GUI can run with no hardware and no Pi.

    python3 harness/fake_robot.py                # serve all three on 127.0.0.1
    python3 harness/fake_robot.py --report       # summarise what the GUI asked for

Why this exists (2026-09-13, per user): derive the feature set the C++ side
must provide *from the GUI*, by watching what the GUI actually sends.
Every incoming line is appended to harness/gui_cmd_log.txt tagged KNOWN or
UNKNOWN.  UNKNOWN == "the GUI needs this and nothing here models it yet".

Differences from harness/fake_text_server.py: that one answers `OK` to
everything so a shutdown path terminates deterministically.  This one keeps a
small simulated state and answers in the *real* reply formats (key=value lines
copied from cmd_status() / cmd_relay_status() / crane status / arm STATUS),
otherwise the GUI panels stay blank and its `expect` regexes time out.

Fidelity is deliberately shallow: enough for panels to paint and buttons to
round-trip.  Real safety logic, timing and error paths are NOT modelled.

[2026-09-13] Implements the *frozen* new interface from
.claude/plans/orchestration_to_cpp_plan.md §2–§4 ahead of the C++:
  mission start/stop/status/params + EVT mission …   (plan §2)
  SAFE: emergency_stop → safe; safe_clear; crane safe_enter/safe_clear  (plan §3)
  self-check fields: dev_*, zdt_homed_at, arm_ready, arm init_done, PING, crane zeroed  (plan §4)
  test hooks (NOT real protocol): `sim roll <deg>` / `sim tension_valid 0` / `sim top <cm>` / `sim noseal` / `sim safe [src]`
"""
import argparse
import collections
import datetime as dt
import os
import socketserver
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_PATH = os.path.join(HERE, 'gui_cmd_log.txt')
_log_lock = threading.Lock()


def log_cmd(target, line, known):
    ts = dt.datetime.now().strftime('%H:%M:%S.%f')[:-3]
    with _log_lock:
        with open(LOG_PATH, 'a', encoding='utf-8') as f:
            f.write(f'{ts}\t{target}\t{"KNOWN" if known else "UNKNOWN"}\t{line}\n')


# ----------------------------------------------------------------- shared sim
class Sim:
    """One shared world: crane rope lengths drive body height; arm tau converges."""

    def __init__(self):
        self.lock = threading.Lock()
        # crane (ground-zero convention: 0 at ground, negative going up)
        self.len_l = -120.0
        self.len_r = -120.0
        self.len_m = 0.0
        self.ten_l = 46.0
        self.ten_r = 47.5
        self.home_ground = 0
        self.motion_hz = 30
        self.balance_source = 'imu'
        self.imu_roll = 0.3
        self.water_inlet = 0
        self.moving = None          # (side, target_len, started)
        # body
        self.state = 'idle'
        self.relay = [0] * 8         # ch1..ch8
        self.pump_active = 'A'
        self.pwm_duty = 5.0
        self.pwm_hz = 50
        self.rail_cm = 0.0
        self.rail_running = 0
        self.zdt = {s: {'pos': 0, 'en': 1} for s in (5, 6, 7, 8)}
        self.pres = {5: -2.1, 6: -1.8, 7: -2.4, 8: -2.0}   # kPa, ~0 = not sealed
        self.water_full = 0
        self.crane_attached = 'on'
        self.arm_attached = 'on'
        # arm
        self.m1 = {'pos': 0.40, 'vel': 0.0, 'tau': 0.0, 'hold': 0, 'moving': 0, 'en': 0}
        self.m2 = {'pos': 0.8558, 'vel': 0.0, 'tau': 0.0, 'hold': 0, 'moving': 0, 'en': 0}
        self.deploy_target = None
        # [2026-09-13 plan §3/§4] SAFE + self-check + mission (interface frozen in
        # .claude/plans/orchestration_to_cpp_plan.md §2–§4; keep this file in lock-step)
        self.safe = 0; self.safe_src = ''
        self.crane_safe_locked = 0
        self.crane_zeroed = 0
        self.zdt_homed_at = 0
        self.arm_init_done = 0
        self.tension_valid = 1
        self.top_cm = -120.0            # 'wall top' the mission precheck compares against
        self.mission = None             # Mission instance while running
        threading.Thread(target=self._tick, daemon=True).start()

    def _tick(self):
        while True:
            time.sleep(0.25)
            with self.lock:
                if self.moving:
                    side, tgt, _ = self.moving
                    cur = self.len_l if side in ('left', 'both') else self.len_r
                    step = 0.8
                    done = True
                    for s in (['left', 'right'] if side == 'both' else [side]):
                        v = self.len_l if s == 'left' else self.len_r
                        if abs(tgt - v) > step:
                            v += step if tgt > v else -step
                            done = False
                        else:
                            v = tgt
                        if s == 'left': self.len_l = v
                        else: self.len_r = v
                    if done:
                        self.moving = None
                # vacuum: sealed cups pull toward -66 kPa when valve (ch1) on
                for k in self.pres:
                    tgt = -66.0 if (self.relay[0] and self.zdt[k]['pos'] > 0) else -2.0
                    self.pres[k] += (tgt - self.pres[k]) * 0.25
                # arm force control converges to target
                if self.deploy_target is not None:
                    self.m1['tau'] += (self.deploy_target - self.m1['tau']) * 0.3
                    self.m1['pos'] = min(1.10, self.m1['pos'] + 0.02)
                    if abs(self.deploy_target - self.m1['tau']) < 0.05:
                        self.m1['moving'] = 0
                # rail sweep progress
                if self.rail_running:
                    self.rail_cm = (self.rail_cm + 5) % 100
                    if self.rail_cm == 0:
                        self.rail_running = 0

    # ---- reply builders (formats copied from the C++ side) ----
    def crane_status(self):
        s = self
        return (f'OK length_left={s.len_l:.1f} length_right={s.len_r:.1f} length_middle=ERR'
                f' tension_left={s.ten_l:.1f} tension_right={s.ten_r:.1f} tension_valid=1'
                f' up_left=0 up_right=0 down_left=0 down_right=0'
                f' up_stop_total_kg=130 tension_max_kg=80 tension_diff_max_kg=25 length_diff_max_cm=10'
                f' retract_tension_stop_kg=50 dsz_left_scale=-0.0205816 dsz_right_scale=-0.0236364'
                f' meter_left_scale=1 meter_right_scale=1 meter_middle_scale=1'
                f' home_ground_cm={s.home_ground} hold_hz=20 motion_hz={s.motion_hz} middle_hz=30'
                f' balance_enabled=1 balance_kp=2.0 balance_cap_ratio=0.5 balance_deadband=0.5'
                f' balance_hz_min=5 balance_hz_max_offset=10 fine_adjust_hz=10 freeze_hz=0 kick_hz=0'
                f' roll_correct_hz=5 roll_finish_hz=3 fine_adjust_diff_tol_cm=2 fine_adjust_level_diff_cm=6'
                f' balance_source={s.balance_source} balance_imu_kp_ratio=1.0 balance_imu_deadband=0.5'
                f' imu_roll={s.imu_roll:.2f} imu_roll_age_ms=120 imu_roll_fresh=1'
                f' dev_vfd_left=1 dev_vfd_right=1 dev_meter_left=1 dev_meter_right=1 dev_meter_middle=0'
                f' dev_clv900=0 dev_dsz_left=1 dev_dsz_right=1 dev_gw_a=1 dev_gw_b=1 dev_gw_m=1 dev_gw_c=1 dev_gw_d=0'
                f' dev_gw_w=1 dev_pqw_water=1 water_inlet={s.water_inlet}'
                f' zeroed={s.crane_zeroed} safe={s.crane_safe_locked} safe_src={s.safe_src or "-"}\n')

    def body_status(self):
        s = self
        cups = ' '.join(f'p{k}={v:.1f}' for k, v in s.pres.items())
        seal = ' '.join(f's{k}={1 if v < -30 else 0}' for k, v in s.pres.items())
        zdt = ' '.join(f'z{k}={s.zdt[k]["pos"]}' for k in s.zdt)
        return (f'OK state={s.state} crane_attached={s.crane_attached} crane_peer_age_ms=200 crane_peer_fresh=1'
                f' crane_estop_connected=1 crane_estop_down_ms=0 crane_idle_ms=300 crane_idle_ms_max=900'
                f' crane_idle_ms_max_motion=600 crane_wd_warn_ms=3000 crane_wd_abort_ms=8000'
                f' arm_attached={s.arm_attached} obstacle_detect=off follower_mode=imu first_step=right'
                f' step_in_progress=0 p_err=0 {cups} {seal} {zdt}'
                f' roll={s.imu_roll:.2f} pitch=0.10 ax=0.01 ay=0.00 az=0.99 raw_x=0 raw_y=0 raw_z=0'
                f' n_accel=100 n_angle=100 imu_guard=on'
                f' active={s.pump_active} pump=on base=A accum_min=12 auto_rotate=30'
                f' rail_cm={s.rail_cm:.1f} water_full={s.water_full}'
                f' dev_zdt=4/4 dev_pqw=1 dev_dm2j=1 dev_jc100=4/4 dev_xkc=1 dev_qx=1 dev_imu=1 dev_arm={1 if s.m1["en"] else 0}'
                f' zdt_homed_at={s.zdt_homed_at} arm_ready={1 if (s.m1["en"] and s.arm_init_done) else 0}'
                f' safe={s.safe} safe_src={s.safe_src or "-"}'
                f' {s.mission.status_kv() if s.mission else "mission=idle"}\n')

    def relay_status(self):
        chs = ' '.join(f'ch{i+1}={v}' for i, v in enumerate(self.relay))
        return f'OK {chs} | names ch1=valve ch2=pumpA ch3=pumpB(A/B輪替) ch5=brush ch6=正壓閥 ch4=噴水加壓馬達(手臂)\n'

    def pwm_status(self):
        return (f'OK ch1={self.pwm_duty:.0f},{self.pwm_hz},65535,1 ch2=5,50,0,0 ch3=ERR ch4=50,1000,0,0'
                f' duty_min=5 duty_max=10 freq_lock=50 active_ch=1\n')

    def arm_status(self):
        def one(tag, m):
            extra = f' init_done={self.arm_init_done}' if tag == 'M1' else ''
            return (f'[{tag}] pos={m["pos"]:.4f} vel={m["vel"]:.4f} tau={m["tau"]:.4f} hold={m["hold"]}'
                    f' moving={m["moving"]} err=NONE en={m["en"]} settle_cnt=0{extra}')
        return one('M1', self.m1) + '\n' + one('M2', self.m2) + '\n'


BCAST = {}   # name -> server.broadcast, filled in main()


def _bcast(name, line):
    f = BCAST.get(name)
    if f: f(line if line.endswith('\n') else line + '\n')


def enter_safe(src, detail=''):
    """Plan §3.3: body owns SAFE; crane locks. Order is the spec's order."""
    s = SIM
    with s.lock:
        if s.safe: return
        if s.mission: s.mission.request_stop('safe')
        s.crane_safe_locked = 1; s.moving = None              # ② crane safe_enter: stop + lock
        # ③ vacuum valve kept as-is (no pusher motion, no vacuum break)
        s.deploy_target = None; s.m1['tau'] = 0.0; s.m1['pos'] = 0.40  # ④ arm_retract (stays enabled)
        s.relay[3] = 0; s.relay[4] = 0                         # ⑤ water_pump off, brush off
        s.pwm_duty = 5.0                                       # ⑥ fan OFF (per user 09-13: 直接關)
        s.safe = 1; s.safe_src = src; s.state = 'safe'
    _bcast('washrobot', f'EVT safe_enter src={src} detail={detail or "-"}')
    _bcast('crane', f'EVT safe_enter src={src}')


class Mission(threading.Thread):
    """cycle_test.py `full` re-expressed as a state machine that speaks plan §2.2 EVTs."""
    _next_id = 0

    def __init__(self, steps, step_cm, cycles, nm, rail_cm):
        super().__init__(daemon=True)
        Mission._next_id += 1
        self.id = Mission._next_id
        self.steps, self.step_cm, self.cycles, self.nm, self.rail_cm = steps, step_cm, cycles, nm, rail_cm
        self.phase, self.cyc, self.step, self.sub, self.n_seal = 'starting', 0, 0, '-', 0
        self.skipped = self.no_seal = self.warn = self.roll_fixed = 0
        self.stop_reason = None
        self.paused = threading.Event(); self.paused.set()   # set == running

    def status_kv(self):
        return (f'mission={self.phase} mission_id={self.id} cyc={self.cyc}/{self.cycles} step={self.step}/{self.steps}'
                f' sub={self.sub} n_seal={self.n_seal} skipped={self.skipped} no_seal={self.no_seal}')

    def request_stop(self, reason): self.stop_reason = reason; self.paused.set()
    def evt(self, kind, **kv):
        _bcast('washrobot', 'EVT mission ' + kind + ''.join(f' {k}={v}' for k, v in kv.items()))

    def _sleep(self, sec):
        """Interruptible sleep honouring pause/stop."""
        t = time.time()
        while time.time() - t < sec:
            if self.stop_reason: return False
            self.paused.wait(0.1)
        return self.stop_reason is None

    def run(self):
        s = SIM
        try:
            self.phase = 'running'
            with s.lock: s.state = 'running'; top = s.top_cm
            self.evt('start', id=self.id, steps=self.steps, step_cm=self.step_cm, cycles=self.cycles, top=f'{top:.0f}', bal=s.balance_source)
            for cyc in range(1, self.cycles + 1):
                self.cyc = cyc
                for i in range(1, self.steps + 1):
                    self.step = i
                    with s.lock: h = s.len_l
                    self.evt('step_begin', cyc=cyc, step=i, h=f'{h:.0f}')
                    self.sub = 'fan_off';  s.pwm_duty = 5.0
                    self.sub = 'vacuum_on'
                    with s.lock: s.relay[0] = 1
                    self.sub = 'extend'
                    with s.lock:
                        for k in s.zdt: s.zdt[k]['pos'] = 30000
                    if not self._sleep(1.0): break
                    self.sub = 'vac_wait'; t0 = time.time(); n = 0
                    while time.time() - t0 < 10.0:
                        with s.lock: pr = dict(s.pres)
                        n = sum(1 for v in pr.values() if v <= -50)
                        self.n_seal = n
                        self.evt('vac_wait', n_seal=n, **{f'p{k}': f'{v:.0f}' for k, v in pr.items()}, t=f'{time.time()-t0:.1f}')
                        if n >= 1: break
                        if not self._sleep(0.3): break
                    if self.stop_reason: break
                    t_vac = time.time() - t0
                    skip_clean = 1 if n == 0 else 0
                    if skip_clean: self.no_seal += 1
                    self.evt('vac_result', n_seal=n, skip_clean=skip_clean)
                    if not skip_clean:
                        for tool, wet in (('RIGHT', 1), ('LEFT', 0)):
                            self.sub = f'clean_{tool}'
                            with s.lock:
                                s.deploy_target = self.nm; s.m1['en'] = s.m2['en'] = 1; s.m1['moving'] = 1
                                s.m2['pos'] = 0.8558 if tool == 'RIGHT' else 0.1913
                            if not self._sleep(1.5): break
                            if wet:
                                with s.lock: s.relay[3] = 1; s.relay[4] = 1
                            with s.lock: s.rail_running = 1
                            if not self._sleep(2.0): break
                            with s.lock: s.rail_running = 0; s.rail_cm = 0.0; s.relay[3] = 0; s.relay[4] = 0
                            with s.lock: s.deploy_target = None; s.m1['tau'] = 0.0; s.m1['pos'] = 0.40; s.m1['moving'] = 0
                            self.evt('clean', tool=tool, wet=wet, result='ok')
                        if self.stop_reason: break
                    else:
                        self.skipped += 1
                        self.evt('clean', tool='-', wet=0, result='skipped')
                    self.sub = 'retract'
                    with s.lock:
                        for k in s.zdt: s.zdt[k]['pos'] = 300
                    if not self._sleep(0.8): break
                    self.sub = 'fan_on'; s.pwm_duty = 7.0
                    if not self._sleep(1.0): break
                    self.sub = 'move'
                    with s.lock:
                        s.moving = ('both', s.len_l - self.step_cm, time.time())   # down = height decreases (ground-zero)
                    self.evt('move', verb='pay_out', cm=self.step_cm, hz=s.motion_hz)
                    _bcast('crane', f'EVT motion_progress cmd=pay_out cm={self.step_cm}')
                    while True:
                        with s.lock: mv = s.moving; roll = s.imu_roll; tv = s.tension_valid
                        if tv == 0:
                            enter_safe('tension', 'tension_valid=0'); break
                        if abs(roll) > 5.0:
                            # plan §3.2: one roll_recover attempt, then SAFE
                            if self.roll_fixed == 0:
                                self.roll_fixed += 1
                                with s.lock: s.imu_roll = 0.4
                                self.evt('roll_recover', result='ok', roll_before=f'{roll:.1f}')
                            else:
                                enter_safe('roll', f'roll={roll:.1f}'); break
                        if mv is None: break
                        if not self._sleep(0.25): break
                    if self.stop_reason: break
                    if not self._sleep(0.3): break
                    with s.lock: ra = s.imu_roll
                    self.evt('step_done', cyc=cyc, step=i, t_ext='1.0', t_vac=f'{t_vac:.1f}', t_clean='7.0', t_ret='0.8',
                             t_move=f'{self.step_cm/3.2:.1f}', roll_after=f'{ra:+.2f}')
                if self.stop_reason: break
                self.sub = 'return_top'; s.pwm_duty = 5.0
                with s.lock: s.moving = ('both', top, time.time())
                while True:
                    with s.lock: mv = s.moving
                    if mv is None or not self._sleep(0.25): break
                self.evt('cycle_done', cyc=cyc, up_s=f'{abs(top - h)/3.2:.1f}')
            reason = self.stop_reason or 'done'
        except Exception as e:  # keep the fake alive; surface the bug as a stop reason
            reason = f'bail:{type(e).__name__}'
        finally:
            with s.lock:
                s.deploy_target = None; s.pwm_duty = 5.0; s.relay[3] = s.relay[4] = 0
                if not s.safe: s.state = 'idle'
                s.mission = None
            self.phase = 'stopped'
            self.evt('stop', reason=reason, summary=f'"no_seal={self.no_seal} warn={self.warn} skipped={self.skipped} roll_fixed={self.roll_fixed}"')


def mission_precheck():
    """Plan §2.3 — same criteria as cycle_test, evaluated on the sim."""
    s = SIM
    if abs(s.len_l - s.top_cm) > 5: return f'ERR precheck_failed item=top detail=h={s.len_l:.0f}_top={s.top_cm:.0f}\n'
    if s.state not in ('idle', 'ready'): return f'ERR precheck_failed item=state detail=state={s.state}\n'
    if not (s.relay[1] or s.relay[2]): return 'ERR precheck_failed item=pump detail=pumpA=0_pumpB=0\n'
    if abs(s.imu_roll) > 1.0: return f'ERR precheck_failed item=roll detail=roll={s.imu_roll:.2f}\n'
    if s.balance_source != 'imu':
        s.balance_source = 'imu'; _bcast('washrobot', 'EVT mission precheck_fix item=bal set=imu')
    return None


SIM = Sim()


# ------------------------------------------------------------- dispatchers
def wr_dispatch(line, bcast):
    """washrobot :5001 — returns (reply, known)"""
    s = SIM
    p = line.split()
    c = p[0] if p else ''
    a = p[1:]
    # ---- plan §2 / §3: commands that must NOT hold s.lock across thread starts ----
    if c == 'mission':
        sub = a[0] if a else ''
        if sub == 'start':
            with s.lock:
                if s.safe: return 'ERR busy state=safe\n', True
                if s.mission: return f'ERR busy state={s.state} mission_id={s.mission.id}\n', True
                err = mission_precheck()
                if err: return err, True
                try:
                    steps = int(a[1]); step_cm = int(a[2])
                    cycles = int(a[3]) if len(a) > 3 else 1
                    nm = float(a[4]) if len(a) > 4 else 8.0
                    rail = int(a[5]) if len(a) > 5 else 100
                except (ValueError, IndexError):
                    return 'ERR usage: mission start <steps> <step_cm> [cycles] [nm] [rail_cm]\n', True
                s.mission = Mission(steps, step_cm, cycles, nm, rail)
                s.mission.start()
                return f'OK mission started id={s.mission.id}\n', True
        if sub == 'stop':
            with s.lock: m = s.mission
            if not m: return 'ERR no_mission\n', True
            m.request_stop(a[1] if len(a) > 1 else 'user'); return 'OK stopping\n', True
        if sub == 'status':
            with s.lock: return ('OK ' + (s.mission.status_kv() if s.mission else 'mission=idle') + '\n'), True
        if sub == 'params':
            return 'OK VAC_OK_KPA=-50 VAC_WAIT_S=10 FAN_ON=7 FAN_OFF=5 DOWN_HZ=30 UP_HZ=40 TOL=5 BOTTOM=-20\n', True
        return 'ERR usage: mission start|stop|status|params\n', True
    if c == 'emergency_stop':
        enter_safe('user', 'emergency_stop'); return 'OK safe_entered src=user\n', True
    if c == 'safe_clear':
        with s.lock:
            if not s.safe: return 'ERR not_in_safe\n', True
            if s.tension_valid == 0: return 'ERR safe_clear_refused item=tension_valid\n', True
            if abs(s.imu_roll) > 5.0: return f'ERR safe_clear_refused item=roll detail={s.imu_roll:.1f}\n', True
            s.safe = 0; src = s.safe_src; s.safe_src = ''; s.crane_safe_locked = 0
            s.state = 'paused'
        reason = ' '.join(a) or '-'
        _bcast('washrobot', f'EVT safe_clear by=gui reason={reason} was={src}')
        _bcast('crane', 'EVT safe_clear by=body')
        return 'OK safe_cleared state=paused\n', True
    if c == 'sim':   # test hooks for GUI development (not part of the real protocol)
        with s.lock:
            if a[:1] == ['roll'] and len(a) > 1: s.imu_roll = float(a[1]); return f'OK sim roll={s.imu_roll}\n', True
            if a[:1] == ['tension_valid'] and len(a) > 1: s.tension_valid = int(a[1]); return f'OK sim tension_valid={s.tension_valid}\n', True
            if a[:1] == ['top'] and len(a) > 1: s.top_cm = float(a[1]); return f'OK sim top={s.top_cm}\n', True
            if a[:1] == ['noseal']: 
                for k in s.zdt: s.zdt[k]['pos'] = 0
                return 'OK sim noseal (pushers reported at 0 -> cups never seal)\n', True
        if a[:1] == ['safe']: enter_safe(a[1] if len(a) > 1 else 'sim', 'test-hook'); return 'OK sim safe\n', True
        return 'ERR usage: sim roll <deg>|tension_valid 0|1|top <cm>|noseal|safe [src]\n', True
    with s.lock:
        if s.safe and c in ('pusher', 'zdt_pusher', 'vacuum', 'rail', 'rail_sweep', 'rail_jog', 'rail_pos', 'arm_deploy_f',
                            'arm_deploy', 'step_down', 'step_up', 'run', 'run_saved', 'run_script', 'pwm', 'init', 'resume', 'continue'):
            return 'ERR safe_locked\n', True
        if c == 'ping': return 'OK pong\n', True
        if c == 'status': return s.body_status(), True
        if c == 'relay_status': return s.relay_status(), True
        if c == 'relay' and len(a) >= 2 and a[0].isdigit():
            ch = int(a[0]); on = a[1] == 'on'
            if 1 <= ch <= 8: s.relay[ch - 1] = 1 if on else 0
            return f'OK ch{ch}={1 if on else 0}\n', True
        if c == 'pump':
            if a[:1] == ['status']: return f'OK ch2={s.relay[1]} ch3={s.relay[2]} active={s.pump_active}\n', True
            if a[:1] == ['swap']:
                s.pump_active = 'B' if s.pump_active == 'A' else 'A'
                s.relay[1], s.relay[2] = (1, 0) if s.pump_active == 'A' else (0, 1)
                return f'OK active={s.pump_active}\n', True
            if a[:1] == ['on']: s.relay[1] = 1; return 'OK ch2=1 active=A\n', True
            if a[:1] == ['off']: s.relay[1] = s.relay[2] = 0; return 'OK ch2=0 active=A\n', True
        if c == 'vacuum':
            on = a[:1] != ['off']; s.relay[0] = 1 if on else 0
            return f'OK ch1={s.relay[0]}\n', True
        if c == 'brush': s.relay[4] = 1 if a[:1] != ['off'] else 0; return f'OK ch5={s.relay[4]}\n', True
        if c == 'water_pump': s.relay[3] = 1 if a[:1] != ['off'] else 0; return f'OK ch4={s.relay[3]}\n', True
        if c == 'water_level': return f'OK water_full={s.water_full} rssi=4000\n', True
        if c == 'pwm':
            if a[:1] == ['status']: return s.pwm_status(), True
            if a[:1] == ['set'] and len(a) >= 3:
                try: s.pwm_duty = max(5.0, min(10.0, float(a[2])))
                except ValueError: pass
                return f'OK ch1={s.pwm_duty:.0f},{s.pwm_hz}\n', True
            return 'OK\n', True
        if c in ('zdt_pusher', 'pusher') and len(a) >= 2:
            tgt = [5, 6, 7, 8] if a[0] in ('all', 'feet') else [int(a[0])] if a[0].isdigit() else []
            for k in tgt:
                if k in s.zdt: s.zdt[k]['pos'] = 30000 if a[1] == 'extend' else 300
            return f'OK {a[0]} {a[1]}\n', True
        if c in ('zdt_home', 'zdt_zero'):
            for k in s.zdt: s.zdt[k]['pos'] = 0
            if c == 'zdt_home': s.zdt_homed_at = int(time.time())
            return 'OK homed=5,6,7,8\n', True
        if c in ('zdt_enable', 'zdt_disable', 'zdt_release_stall', 'zdt_power'):
            return 'OK\n', True
        if c == 'rail_sweep':
            if a[:1] == ['status']: return f'OK running={s.rail_running} pos={s.rail_cm:.1f}\n', True
            s.rail_running = 1; return 'OK rail sweep started\n', True
        if c in ('rail', 'rail_jog', 'rail_pos', 'rail_zero', 'rail_enable', 'rail_cfg_soft_enable'):
            if c == 'rail_pos' and a and a[0].replace('.', '', 1).isdigit(): s.rail_cm = float(a[0])
            if c == 'rail_zero': s.rail_cm = 0.0
            return f'OK rail_cm={s.rail_cm:.1f}\n', True
        if c == 'init':
            s.state = 'ready'; s.relay[1] = 1; s.pump_active = 'A'   # real cmd_init turns pump A on
            threading.Timer(0.5, lambda: bcast('EVT init_complete ok=1\n')).start()
            return 'OK init started\n', True
        if c in ('pause', 'resume', 'continue', 'skip', 'reset', 'shutdown', 'recover', 'realign'):
            m = s.mission
            if c == 'pause':
                s.state = 'paused'
                if m: m.paused.clear()
            if c in ('resume', 'continue'):
                s.state = 'running' if m else 'idle'
                if m: m.paused.set()
            if c == 'skip' and m: m.skipped += 1; m.paused.set()
            if c == 'reset': s.state = 'idle'; s.relay = [0] * 8
            return 'OK\n', True
        if c == 'arm_deploy_f' and a:
            try: s.deploy_target = float(a[0])
            except ValueError: s.deploy_target = 8.0
            s.m1['en'] = s.m2['en'] = 1; s.m1['moving'] = 1
            return f'OK tau={s.deploy_target:.2f} theta={s.m1["pos"]:.3f} iters=3 kp_eff=60\n', True
        if c in ('arm_park', 'arm_retract'):
            s.deploy_target = None; s.m1['tau'] = 0.0; s.m1['pos'] = 0.40; s.m1['moving'] = 0
            return 'OK\n', True
        if c == 'arm_status': return 'OK ' + s.arm_status().replace('\n', ' ').strip() + '\n', True
        if c in ('arm_init', 'arm_deploy', 'arm_sweep', 'arm_clean_sweep', 'arm_clean_sweep_dry', 'arm_attached', 'crane_attached'):
            if c == 'arm_attached' and a: s.arm_attached = a[0]
            if c == 'crane_attached' and a: s.crane_attached = a[0]
            return 'OK\n', True
        if c in ('get_settings', 'set_setting', 'save_settings'):
            return 'OK settings={}\n', True
        if c in ('imu_zero', 'imu_level', 'imu_guard', 'water_inlet', 'set_first_step', 'set_follower_mode',
                 'set_pump_rotate_min', 'set_crane_wd_warn_ms', 'set_crane_wd_abort_ms', 'reset_crane_idle_max',
                 'step_down', 'step_up', 'run', 'run_saved', 'run_script', 'save_script', 'load_script',
                 'list_scripts', 'delete_script', 'return_home'):
            if c in ('step_down', 'step_up', 'run', 'run_saved', 'run_script'): s.state = 'running'
            return 'OK\n', True
    return f'ERR unknown_command {c}\n', False


def cr_dispatch(line, bcast):
    """crane :5002"""
    s = SIM
    p = line.split()
    c = p[0] if p else ''
    a = p[1:]
    with s.lock:
        if c == 'safe_enter':
            s.crane_safe_locked = 1; s.moving = None; return 'OK safe_locked=1\n', True
        if c == 'safe_clear':
            s.crane_safe_locked = 0; return 'OK safe_locked=0\n', True
        if s.crane_safe_locked and c in ('pay_out', 'retract', 'pay_out_left', 'pay_out_right', 'retract_left', 'retract_right',
                                         'hold', 'roll_correct', 'align_lengths', 'fine_adjust', 'side_measured'):
            return 'ERR safe_locked\n', True
        if c == 'ping': return 'OK pong\n', True
        if c == 'status': return s.crane_status(), True
        if c == 'clear_error': return 'OK cleared\n', True
        if c == 'roll_correct':
            s.imu_roll = 0.3; return 'OK roll_correct done\n', True
        if c == 'tension': return f'OK left={s.ten_l:.1f} right={s.ten_r:.1f} valid={s.tension_valid}\n', True
        if c == 'water_status':
            return f'OK water_inlet={s.water_inlet} wch1=0 wch2=0 wch3=0 wch4={s.water_inlet} | names wch4=inlet\n', True
        if c == 'water_inlet' and a:
            s.water_inlet = 1 if a[0] == 'on' else 0
            return f'OK water_inlet={s.water_inlet}\n', True
        if c in ('pay_out', 'retract', 'pay_out_left', 'pay_out_right', 'retract_left', 'retract_right') and a:
            try: cm = float(a[0])
            except ValueError: return 'ERR bad_arg\n', True
            side = 'left' if c.endswith('_left') else 'right' if c.endswith('_right') else 'both'
            sign = 1 if c.startswith('pay_out') else -1
            base = s.len_l if side != 'right' else s.len_r
            s.moving = (side, base + sign * cm, time.time())
            threading.Timer(0.3, lambda: bcast(f'EVT motion_progress cmd={c} cm={cm:.0f}\n')).start()
            return f'OK {c} {cm:.0f} started\n', True
        if c == 'stop' or c == 'emergency_stop':
            s.moving = None; return 'OK stopped\n', True
        if c in ('hold', 'hold_all_off'):
            return 'OK\n', True
        if c == 'zero_meters':
            s.len_l = s.len_r = 0.0; s.home_ground = 0; s.crane_zeroed = 1; s.top_cm = 0.0
            return 'OK zeroed\n', True
        if c == 'set_home_ground' and a:
            try: s.home_ground = int(float(a[0]))
            except ValueError: pass
            return f'OK home_ground_cm={s.home_ground}\n', True
        if c == 'set_imu_roll' and a:
            try: s.imu_roll = float(a[0])
            except ValueError: pass
            return 'OK\n', True
        if c == 'set_balance_source' and a:
            s.balance_source = a[0]; return f'OK balance_source={s.balance_source}\n', True
        if c == 'set_motion_hz' and a:
            try: s.motion_hz = int(a[0])
            except ValueError: pass
            return f'OK motion_hz={s.motion_hz}\n', True
        if c.startswith('set_') or c in ('roll_correct', 'align_lengths', 'fine_adjust', 'roll_trim_ms',
                                          'side_measured', 'dual_vfd_sync_start', 'meter_cal', 'dszl_zero'):
            return 'OK\n', True
    return f'ERR unknown_command {c}\n', False


def arm_dispatch(line, bcast):
    """arm :9527 — native upper-case protocol"""
    s = SIM
    p = line.split()
    c = p[0].upper() if p else ''
    a = p[1:]
    with s.lock:
        if c == 'STATUS': return s.arm_status(), True
        if c == 'PING': return 'OK pong\n', True     # plan §4: new; real arm currently answers ERR
        if c == 'INIT':
            s.m1['en'] = s.m2['en'] = 1; s.m1['pos'] = 0.40; s.arm_init_done = 1; return 'OK init done\n', True
        if c == 'DEPLOY_F' and a:
            try: s.deploy_target = float(a[0])
            except ValueError: s.deploy_target = 8.0
            s.m1['en'] = s.m2['en'] = 1; s.m1['moving'] = 1
            return f'OK tau={s.deploy_target:.2f} theta={s.m1["pos"]:.3f} iters=3 kp_eff=60\n', True
        if c == 'DEPLOY':
            s.m1['pos'] = 0.60; return 'OK deployed\n', True
        if c == 'PARK':
            s.deploy_target = None; s.m1['tau'] = 0.0; s.m1['pos'] = 0.40; s.m1['moving'] = 0
            return 'OK parked\n', True
        if c in ('M1', 'M2') and a:
            m = s.m1 if c == 'M1' else s.m2
            sub = a[0].upper()
            if sub == 'ENABLE': m['en'] = 1
            elif sub == 'DISABLE': m['en'] = 0
            return 'OK\n', True
    # real arm (main_api.cpp:3890) answers exactly this; web_backend's `ping` keepalive hits it every time
    return f'ERR unknown command: {c}\n', False


# ------------------------------------------------------------- TCP plumbing
class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        srv = self.server
        srv.clients.add(self)
        try:
            while True:
                raw = self.rfile.readline()
                if not raw:
                    return
                line = raw.decode('utf-8', 'replace').strip()
                if not line:
                    continue
                reply, known = srv.dispatch(line, srv.broadcast)
                log_cmd(srv.name, line, known)
                if srv.verbose:
                    print(f'[{srv.name}] {"   " if known else "?? "}{line!r} -> {reply.strip()[:80]}', flush=True)
                try:
                    self.wfile.write(reply.encode('utf-8')); self.wfile.flush()
                except OSError:
                    return
        finally:
            srv.clients.discard(self)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, addr, name, dispatch, verbose):
        super().__init__(addr, Handler)
        self.name, self.dispatch, self.verbose = name, dispatch, verbose
        self.clients = set()

    def broadcast(self, text):
        for c in list(self.clients):
            try:
                c.wfile.write(text.encode('utf-8')); c.wfile.flush()
            except OSError:
                self.clients.discard(c)


def report():
    if not os.path.exists(LOG_PATH):
        print('no log yet:', LOG_PATH); return
    seen = collections.defaultdict(collections.Counter)
    unknown = collections.defaultdict(collections.Counter)
    with open(LOG_PATH, encoding='utf-8') as f:
        for ln in f:
            parts = ln.rstrip('\n').split('\t', 3)
            if len(parts) < 4: continue
            _, tgt, tag, cmd = parts
            head = ' '.join(cmd.split()[:2]) if cmd.split()[:1] in (['pwm'], ['pump'], ['rail_sweep'], ['zdt_pusher'], ['relay']) else cmd.split()[0]
            seen[tgt][head] += 1
            if tag == 'UNKNOWN': unknown[tgt][head] += 1
    for tgt in ('washrobot', 'crane', 'arm'):
        print(f'\n=== {tgt}: {len(seen[tgt])} distinct commands ===')
        for cmd, n in seen[tgt].most_common():
            flag = '  ?? not modelled' if cmd in unknown[tgt] else ''
            print(f'  {n:4d}  {cmd}{flag}')
    tot_unk = sum(len(v) for v in unknown.values())
    print(f'\n{tot_unk} command(s) the GUI sent that fake_robot does not model -> real features the C++ side must provide.')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--wr', type=int, default=5001)
    ap.add_argument('--cr', type=int, default=5002)
    ap.add_argument('--arm', type=int, default=9527)
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--report', action='store_true', help='summarise gui_cmd_log.txt and exit')
    ap.add_argument('--reset-log', action='store_true')
    args = ap.parse_args()
    if args.report:
        report(); return
    if args.reset_log and os.path.exists(LOG_PATH):
        os.remove(LOG_PATH)
    servers = [Server((args.host, args.wr), 'washrobot', wr_dispatch, not args.quiet),
               Server((args.host, args.cr), 'crane', cr_dispatch, not args.quiet),
               Server((args.host, args.arm), 'arm', arm_dispatch, not args.quiet)]
    for s in servers:
        BCAST[s.name] = s.broadcast
        threading.Thread(target=s.serve_forever, daemon=True).start()
        print(f'[fake_robot] {s.name:9s} listening {args.host}:{s.server_address[1]}', flush=True)
    print(f'[fake_robot] logging every command to {LOG_PATH}', flush=True)
    try:
        while True: time.sleep(3600)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
