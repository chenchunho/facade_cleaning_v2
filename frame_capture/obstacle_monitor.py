#!/usr/bin/env python3
"""
Continuous obstacle-detector monitor — reads the latest cam3 + cam4 frames
from disk every N seconds, runs obstacle_detector.py, prints the result.

For bench validation: leave it running while you walk around / place
obstacles in front of the cameras and watch what detector reports.

PREREQ
------
frame_capture.py must be running (typically two instances, one per camera),
each writing to its --output-path:
    frame_capture.py --rtsp ... --output-path /tmp/cam3_latest.jpg --cam-id cam3
    frame_capture.py --rtsp ... --output-path /tmp/cam4_latest.jpg --cam-id cam4

USAGE
-----
    # Default: read /tmp/cam3_latest.jpg + /tmp/cam4_latest.jpg every 30s
    python3 obstacle_monitor.py

    # Custom paths + interval
    python3 obstacle_monitor.py \\
        --cam3 /tmp/cam3_latest.jpg --cam4 /tmp/cam4_latest.jpg \\
        --interval 30

    # Single shot (run once and exit)
    python3 obstacle_monitor.py --once

    # Save annotated debug frames each iteration
    python3 obstacle_monitor.py --debug-out /tmp/monitor_debug/
"""

import argparse
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path


HERE = Path(__file__).resolve().parent
DEFAULT_DETECTOR = HERE / "obstacle_detector.py"
DEFAULT_CAM3_PATH = "/tmp/cam3_latest.jpg"
DEFAULT_CAM4_PATH = "/tmp/cam4_latest.jpg"
DEFAULT_INTERVAL_S = 30
FRAME_STALE_LIMIT_S = 5   # warn if frame older than this (frame_capture stalled?)


def now_str():
    return datetime.now().strftime("%H:%M:%S")


def frame_age_s(path: str) -> float:
    """Returns age of file in seconds, or -1 if file doesn't exist."""
    try:
        mtime = os.path.getmtime(path)
        return time.time() - mtime
    except (OSError, FileNotFoundError):
        return -1.0


def run_detector(detector_path: Path, cam3_path: str, cam4_path: str, debug_out: str = None) -> dict:
    """Subprocess the obstacle_detector.py and return parsed JSON dict."""
    cmd = ["python3", str(detector_path), "--cam3", cam3_path, "--cam4", cam4_path]
    if debug_out:
        cmd += ["--debug-out", debug_out]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
    except subprocess.TimeoutExpired:
        return {"error": "detector_timeout_15s"}
    except FileNotFoundError:
        return {"error": "python3_not_found"}
    if proc.returncode != 0:
        return {
            "error": f"detector_returncode={proc.returncode}",
            "stderr": proc.stderr.strip()[-500:],
        }
    # detector prints JSON on stdout — sometimes preceded by [warn] lines
    out = proc.stdout.strip()
    if not out:
        return {"error": "detector_no_output", "stderr": proc.stderr.strip()[-500:]}
    # Find the FIRST top-level '{' (skip any leading log lines), then use
    # raw_decode so trailing whitespace/newlines don't trigger "Extra data".
    start = out.find("{")
    if start < 0:
        return {"error": "no_json_in_output", "raw": out[-500:]}
    try:
        obj, _idx = json.JSONDecoder().raw_decode(out[start:])
        return obj
    except json.JSONDecodeError as e:
        return {"error": f"json_parse_fail: {e}", "raw": out[start:start + 500]}


def format_summary(result: dict, cam3_age: float, cam4_age: float) -> str:
    """One-line per-iter summary suitable for tail -f viewing."""
    parts = [f"[{now_str()}]"]
    parts.append(f"cam3_age={cam3_age:.1f}s")
    parts.append(f"cam4_age={cam4_age:.1f}s")

    if "error" in result:
        parts.append(f"ERROR={result['error']}")
        return " ".join(parts)

    # detector's static (non-motion) mode returns a Result dict with:
    #   cam3_features / cam4_features      — per-camera raw detections
    #   matched_features                   — features seen in both cams (real obstacles)
    #   filtered_reflections               — only one cam → reflection
    #   decision = {action, step_cm, reason, alert}
    decision = result.get("decision", {})
    if isinstance(decision, dict):
        action = decision.get("action", "?")
        step_cm = decision.get("step_cm", "?")
        reason = decision.get("reason", "")
        parts.append(f"action={action}")
        parts.append(f"step={step_cm}cm")
        if reason:
            parts.append(f"({reason})")
    else:
        parts.append(f"decision={decision}")

    matched = result.get("matched_features") or []
    cam3_f  = result.get("cam3_features") or []
    cam4_f  = result.get("cam4_features") or []
    refl    = result.get("filtered_reflections", 0)   # int count, NOT a list
    parts.append(f"matched={len(matched)} c3={len(cam3_f)} c4={len(cam4_f)} refl={refl}")

    if matched:
        m_sorted = sorted(matched, key=lambda f: f.get("feet_distance_cm", 999.0))
        f0 = m_sorted[0]
        parts.append(
            f"nearest={f0.get('type','?')}@{f0.get('feet_distance_cm', 0):.1f}cm"
            f"_conf={f0.get('confidence', 0):.2f}"
        )

    return " ".join(parts)


def print_separator():
    print("-" * 78)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cam3', default=DEFAULT_CAM3_PATH,
                    help=f'cam3 latest frame path (default {DEFAULT_CAM3_PATH})')
    ap.add_argument('--cam4', default=DEFAULT_CAM4_PATH,
                    help=f'cam4 latest frame path (default {DEFAULT_CAM4_PATH})')
    ap.add_argument('--interval', type=int, default=DEFAULT_INTERVAL_S,
                    help=f'seconds between checks (default {DEFAULT_INTERVAL_S})')
    ap.add_argument('--once', action='store_true', help='run a single check and exit')
    ap.add_argument('--debug-out', help='folder to save annotated debug frames')
    ap.add_argument('--verbose', action='store_true', help='print full detector JSON each iter')
    ap.add_argument('--detector', default=str(DEFAULT_DETECTOR),
                    help=f'path to obstacle_detector.py (default {DEFAULT_DETECTOR})')
    args = ap.parse_args()

    detector_path = Path(args.detector).resolve()

    print_separator()
    print(f"[obstacle_monitor] starting")
    print(f"  cam3:     {args.cam3}")
    print(f"  cam4:     {args.cam4}")
    print(f"  interval: {args.interval}s")
    print(f"  detector: {detector_path}")
    if args.debug_out:
        print(f"  debug:    {args.debug_out}")
    print_separator()

    if not detector_path.exists():
        print(f"[ERR] detector not found: {detector_path}", file=sys.stderr)
        print(f"[hint] use --detector /path/to/obstacle_detector.py to override",
              file=sys.stderr)
        sys.exit(1)

    iter_count = 0
    try:
        while True:
            iter_count += 1
            cam3_age = frame_age_s(args.cam3)
            cam4_age = frame_age_s(args.cam4)

            # Pre-check: frames exist + not too stale
            problems = []
            if cam3_age < 0:
                problems.append(f"cam3 file missing ({args.cam3})")
            elif cam3_age > FRAME_STALE_LIMIT_S:
                problems.append(f"cam3 stale ({cam3_age:.1f}s — frame_capture stuck?)")
            if cam4_age < 0:
                problems.append(f"cam4 file missing ({args.cam4})")
            elif cam4_age > FRAME_STALE_LIMIT_S:
                problems.append(f"cam4 stale ({cam4_age:.1f}s)")

            if problems:
                print(f"[{now_str()}] iter={iter_count} SKIP — " + " | ".join(problems))
            else:
                result = run_detector(detector_path, args.cam3, args.cam4, args.debug_out)
                print(format_summary(result, cam3_age, cam4_age))
                if args.verbose:
                    print(json.dumps(result, indent=2, ensure_ascii=False))

            if args.once:
                break
            try:
                time.sleep(args.interval)
            except KeyboardInterrupt:
                print(f"\n[obstacle_monitor] interrupted — exit (ran {iter_count} iter)")
                break
    except KeyboardInterrupt:
        print(f"\n[obstacle_monitor] interrupted — exit (ran {iter_count} iter)")


if __name__ == '__main__':
    main()
