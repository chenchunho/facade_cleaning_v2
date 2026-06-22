#!/usr/bin/env python3
"""
Combine cam3 + cam4 motion-parallax detections into a single decision.

2026-06-05 改 AND logic (per Sadie): 兩 cam 都看到 obstacle 才當 obstacle。
原本 OR (conservative) 的問題：bench 雜亂場景下單側 cam 看到地板雜物 (cable,
crack) 會誤觸發 over，但雜物不是 wall obstacle。AND logic 利用了 cam3+cam4 從
不同視角看同一面牆應該得到一致結果的事實，filter 掉單側誤報。

USAGE
-----
    python3 obstacle_combine.py \\
        --cam3-before /tmp/cam3_before.jpg --cam3-after /tmp/cam3_after.jpg \\
        --cam4-before /tmp/cam4_before.jpg --cam4-after /tmp/cam4_after.jpg \\
        --detector /home/nexuni/projects/obstacle_detector.py \\
        --debug-out /tmp/dbg/

COMBINE LOGIC (AND with BLOCK safety override):
    - BLOCK: single cam BLOCK → BLOCK (safety override)
    - 兩 cam 都報 obstacle (short/over_partial/over) → 取較保守那邊
    - 只有一 cam 報 obstacle，另一 cam proceed → downgrade 到 proceed (single-cam likely false positive)
    - 兩 cam 都 proceed → proceed

Output: combined JSON with `cam3`, `cam4`, `combined` sections.
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path


# Action conservativeness ranking (lower = more conservative)
ACTION_PRIORITY = {
    'block':         0,   # safest — don't move
    'short':         1,   # stop before obstacle
    'over_partial':  2,   # try to over but can't fully clear
    'over':          3,   # jump over (assumes obstacle clearable)
    'proceed':       4,   # no obstacle, normal walk
}


def run_detector(detector_path: str, cam_id: str, before: str, after: str,
                 debug_out: str = None) -> dict:
    cmd = [
        'python3', detector_path,
        '--motion-before', before,
        '--motion-after', after,
        '--motion-cam', cam_id,
    ]
    if debug_out:
        cmd += ['--debug-out', debug_out + f'_{cam_id}']
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        return {'error': 'detector_timeout', 'cam_id': cam_id}
    if proc.returncode != 0:
        return {'error': f'detector_rc={proc.returncode}',
                'stderr': proc.stderr.strip()[-500:],
                'cam_id': cam_id}
    out = proc.stdout.strip()
    if not out:
        return {'error': 'no_output', 'cam_id': cam_id}
    start = out.find('{')
    if start < 0:
        return {'error': 'no_json', 'raw': out[-300:], 'cam_id': cam_id}
    try:
        obj, _ = json.JSONDecoder().raw_decode(out[start:])
        return obj
    except json.JSONDecodeError as e:
        return {'error': f'json_parse: {e}', 'raw': out[start:start + 300],
                'cam_id': cam_id}


OBSTACLE_ACTIONS = {'short', 'over_partial', 'over'}


def combine_decisions(r3: dict, r4: dict) -> dict:
    """Combine cam3 + cam4 detector results into one decision.

    AND logic (2026-06-05): 兩 cam 都看到 obstacle 才當 obstacle。
    + BLOCK safety override: 單側 BLOCK 也觸發 BLOCK (避免危險動作)。"""
    def _decision(r):
        if 'error' in r:
            return {'action': 'block', 'step_cm': 0,
                    'reason': f'detector_error: {r["error"]}',
                    'alert': None}
        return r.get('decision', {'action': 'block', 'step_cm': 0,
                                   'reason': 'no_decision_in_result',
                                   'alert': None})

    d3 = _decision(r3)
    d4 = _decision(r4)
    a3 = d3.get('action', 'block')
    a4 = d4.get('action', 'block')

    def _result(chosen, chosen_cam, rationale):
        return {
            'action': chosen['action'],
            'step_cm': chosen['step_cm'],
            'reason': chosen.get('reason', ''),
            'alert': chosen.get('alert'),
            'chosen_from': chosen_cam,
            'combine_rationale': rationale,
            'cam3_action': a3,
            'cam4_action': a4,
            'cam3_step_cm': d3.get('step_cm', 0),
            'cam4_step_cm': d4.get('step_cm', 0),
        }

    # ── Safety override: BLOCK 即使單側也觸發 ─────────────────────────
    if a3 == 'block' and a4 == 'block':
        return _result(d3, 'both', 'both BLOCK (safety)')
    if a3 == 'block':
        return _result(d3, 'cam3', f'cam3 BLOCK override (cam4={a4})')
    if a4 == 'block':
        return _result(d4, 'cam4', f'cam4 BLOCK override (cam3={a3})')

    # ── AND logic: 兩 cam 都報 obstacle 才當 obstacle ──────────────────
    a3_obs = a3 in OBSTACLE_ACTIONS
    a4_obs = a4 in OBSTACLE_ACTIONS

    if a3_obs and a4_obs:
        # Both cams see obstacle — 取較保守
        p3 = ACTION_PRIORITY.get(a3, 0)
        p4 = ACTION_PRIORITY.get(a4, 0)
        if p3 < p4:
            return _result(d3, 'cam3', f'both obs, cam3 safer ({a3} < {a4})')
        if p4 < p3:
            return _result(d4, 'cam4', f'both obs, cam4 safer ({a4} < {a3})')
        # Same action — pick step appropriately
        if a3 == 'over':
            # Both over → LARGER step (寧多勿少)
            if d3.get('step_cm', 0) >= d4.get('step_cm', 0):
                return _result(d3, 'both', 'both over, cam3 step larger')
            return _result(d4, 'both', 'both over, cam4 step larger')
        if a3 == 'short':
            # Both short → SMALLER step (stop earlier)
            if d3.get('step_cm', 99) <= d4.get('step_cm', 99):
                return _result(d3, 'both', 'both short, cam3 step smaller')
            return _result(d4, 'both', 'both short, cam4 step smaller')
        # over_partial — same outcome
        return _result(d3, 'both', f'both {a3}, identical action')

    if a3_obs and not a4_obs:
        # 只有 cam3 看到 — likely false positive (bench 雜物 / 鏡頭問題)
        downgraded = dict(d4)
        downgraded['reason'] = (f'AND_downgrade: cam3 reported {a3}@{d3.get("step_cm")}cm '
                                f'but cam4 proceed — likely single-cam false positive')
        downgraded['alert'] = f'single_cam_obstacle_warning: cam3 alone saw {a3}; verify visually'
        return _result(downgraded, 'AND_downgrade',
                       f'AND_downgrade: cam3={a3} cam4=proceed → proceed')

    if a4_obs and not a3_obs:
        downgraded = dict(d3)
        downgraded['reason'] = (f'AND_downgrade: cam4 reported {a4}@{d4.get("step_cm")}cm '
                                f'but cam3 proceed — likely single-cam false positive')
        downgraded['alert'] = f'single_cam_obstacle_warning: cam4 alone saw {a4}; verify visually'
        return _result(downgraded, 'AND_downgrade',
                       f'AND_downgrade: cam3=proceed cam4={a4} → proceed')

    # Both proceed
    return _result(d3, 'both', 'both proceed')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cam3-before', required=True)
    ap.add_argument('--cam3-after',  required=True)
    ap.add_argument('--cam4-before', required=True)
    ap.add_argument('--cam4-after',  required=True)
    ap.add_argument('--detector',
                    default='/home/nexuni/projects/obstacle_detector.py')
    ap.add_argument('--debug-out')
    ap.add_argument('--pretty', action='store_true')
    args = ap.parse_args()

    if not Path(args.detector).is_file():
        print(f'[ERR] detector not found: {args.detector}', file=sys.stderr)
        sys.exit(1)

    r3 = run_detector(args.detector, 'cam3', args.cam3_before, args.cam3_after, args.debug_out)
    r4 = run_detector(args.detector, 'cam4', args.cam4_before, args.cam4_after, args.debug_out)

    combined = combine_decisions(r3, r4)

    out = {
        'cam3': r3,
        'cam4': r4,
        'combined': combined,
    }
    indent = 2 if args.pretty else None
    print(json.dumps(out, indent=indent, ensure_ascii=False))


if __name__ == '__main__':
    main()
