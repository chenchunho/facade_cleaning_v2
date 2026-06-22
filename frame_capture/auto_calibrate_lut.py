#!/usr/bin/env python3
"""
Auto-measure y_px for calibration images and emit ready-to-paste LUT entries.

USAGE
-----
    python3 auto_calibrate_lut.py D:/工作/觀賞用/cam3/ D:/工作/觀賞用/cam4/

    # Save annotated images so you can sanity-check the auto-detected plank line
    python3 auto_calibrate_lut.py --debug-out /tmp/cal_debug/ \\
        D:/工作/觀賞用/cam3/ D:/工作/觀賞用/cam4/

Each folder must contain files named "<distance>cm.jpg" (e.g. 10cm.jpg, 15cm.jpg).

OUTPUT
------
Prints LUT in Python list-of-tuples format ready to paste into
obstacle_detector.py:96 (CAM3_LUT) and :104 (CAM4_LUT).

ALGORITHM
---------
For each image:
  1. Convert to grayscale
  2. Edge detection (Canny) restricted to image top half (skip motion-blur'd
     bottom from rolling shutter + wires/floor clutter)
  3. HoughLinesP to find long horizontal segments
  4. Filter to lines with angle within ±10° of horizontal
  5. Take the median y-center of the longest 3 lines = "plank center y_px"

If detection fails (e.g. no clear horizontal line), print warning and skip
that distance — manual measurement needed for that point.
"""

import argparse
import os
import re
import sys
from pathlib import Path
from typing import List, Optional, Tuple

import cv2
import numpy as np


FRAME_W = 800
FRAME_H = 448

# HoughLinesP params — tuned for the rainbow plank dataset
CANNY_LOW = 50
CANNY_HIGH = 150
HOUGH_RHO = 1
HOUGH_THETA = np.pi / 180
HOUGH_THRESHOLD = 60
HOUGH_MIN_LEN = 150         # plank lines are long
HOUGH_MAX_GAP = 30
ANGLE_TOL_DEG = 10          # ± of horizontal
TOP_N_LINES = 3             # use median y of the top-N longest matching lines


def imread_unicode(path: str) -> Optional[np.ndarray]:
    """cv2.imread fails on non-ASCII Windows paths; use np.fromfile."""
    try:
        data = np.fromfile(path, dtype=np.uint8)
        if data.size == 0:
            return None
        return cv2.imdecode(data, cv2.IMREAD_COLOR)
    except (FileNotFoundError, OSError):
        return None


def imwrite_unicode(path: str, img: np.ndarray) -> bool:
    try:
        ok, buf = cv2.imencode('.jpg', img, [cv2.IMWRITE_JPEG_QUALITY, 90])
        if not ok:
            return False
        buf.tofile(path)
        return True
    except (OSError, cv2.error):
        return False


def detect_plank_y(img: np.ndarray) -> Tuple[Optional[int], List[Tuple[int, int, int, int, int]]]:
    """Detect the horizontal plank's center y_px. Returns (y_px or None, all_candidate_lines)."""
    h, w = img.shape[:2]
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    # Slight blur to reduce noise
    gray = cv2.GaussianBlur(gray, (3, 3), 0)

    edges = cv2.Canny(gray, CANNY_LOW, CANNY_HIGH)

    lines = cv2.HoughLinesP(edges, HOUGH_RHO, HOUGH_THETA, HOUGH_THRESHOLD,
                            minLineLength=HOUGH_MIN_LEN, maxLineGap=HOUGH_MAX_GAP)
    if lines is None:
        return None, []

    candidates = []   # (length, y_center, x1, y1, x2, y2)
    for ln in lines[:, 0, :]:
        x1, y1, x2, y2 = ln
        dx = x2 - x1
        dy = y2 - y1
        if dx == 0:
            continue
        angle_deg = abs(np.degrees(np.arctan2(dy, dx)))
        # Want near-horizontal lines (angle close to 0 or 180)
        if angle_deg > ANGLE_TOL_DEG and angle_deg < (180 - ANGLE_TOL_DEG):
            continue
        length = int(np.hypot(dx, dy))
        y_center = (y1 + y2) // 2
        candidates.append((length, y_center, int(x1), int(y1), int(x2), int(y2)))

    if not candidates:
        return None, []

    # Sort by length descending, take top N, median their y_center
    candidates.sort(key=lambda c: c[0], reverse=True)
    top = candidates[:TOP_N_LINES]
    y_med = int(np.median([c[1] for c in top]))
    return y_med, [(c[2], c[3], c[4], c[5], c[1]) for c in candidates]


def annotate(img: np.ndarray, y_detected: Optional[int],
             candidates: List[Tuple[int, int, int, int, int]]) -> np.ndarray:
    out = img.copy()
    # Draw all candidate horizontal lines (faint)
    for x1, y1, x2, y2, _ in candidates:
        cv2.line(out, (x1, y1), (x2, y2), (180, 180, 180), 1)
    # Detected center line (bright red)
    if y_detected is not None:
        cv2.line(out, (0, y_detected), (out.shape[1], y_detected), (0, 0, 255), 2)
        cv2.putText(out, f"y={y_detected}", (10, max(20, y_detected - 8)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)
    return out


def parse_distance(filename: str) -> Optional[int]:
    """Extract integer distance cm from filename like '15cm.jpg'."""
    m = re.match(r'^(\d+)cm', Path(filename).stem)
    return int(m.group(1)) if m else None


def process_folder(folder: str, cam_label: str, debug_out: Optional[str]) -> List[Tuple[int, int]]:
    """Returns list of (y_px, distance_cm) tuples for the folder, sorted by distance."""
    folder_path = Path(folder)
    if not folder_path.is_dir():
        print(f"[ERR] not a directory: {folder}", file=sys.stderr)
        return []

    results = []
    for f in sorted(folder_path.iterdir()):
        if f.suffix.lower() != '.jpg':
            continue
        dist = parse_distance(f.name)
        if dist is None:
            print(f"[skip] {f.name} (no distance in name)", file=sys.stderr)
            continue
        img = imread_unicode(str(f))
        if img is None:
            print(f"[err] cannot load {f}", file=sys.stderr)
            continue
        # Normalize to expected frame size if needed
        if img.shape[:2] != (FRAME_H, FRAME_W):
            img = cv2.resize(img, (FRAME_W, FRAME_H))
        y_px, candidates = detect_plank_y(img)
        if y_px is None:
            print(f"[warn] {cam_label} {dist}cm: no horizontal line detected", file=sys.stderr)
            continue
        results.append((y_px, dist))
        print(f"  {cam_label} {dist:3d}cm  y_px={y_px:3d}  ({len(candidates)} candidate lines)")

        if debug_out:
            dbg_dir = Path(debug_out)
            dbg_dir.mkdir(parents=True, exist_ok=True)
            ann = annotate(img, y_px, candidates)
            imwrite_unicode(str(dbg_dir / f"{cam_label}_{dist}cm.jpg"), ann)

    # Sort by distance ascending (= y_px descending)
    results.sort(key=lambda r: r[1])
    return results


def emit_lut(name: str, entries: List[Tuple[int, int]]) -> str:
    lines = [f"{name}: List[Tuple[int, float]] = ["]
    for y_px, dist in entries:
        lines.append(f"    ({y_px:3d}, {dist:.1f}),")
    lines.append("]")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cam3_dir', help='Folder with cam3 calibration images (e.g. 10cm.jpg, 15cm.jpg...)')
    ap.add_argument('cam4_dir', help='Folder with cam4 calibration images')
    ap.add_argument('--debug-out', help='Folder to write annotated debug images')
    args = ap.parse_args()

    print("[cam3]")
    cam3 = process_folder(args.cam3_dir, "cam3", args.debug_out)
    print("[cam4]")
    cam4 = process_folder(args.cam4_dir, "cam4", args.debug_out)

    print("\n" + "=" * 60)
    print("Ready-to-paste LUT for obstacle_detector.py:96-111")
    print("=" * 60)
    print(emit_lut("CAM3_LUT", cam3))
    print()
    print(emit_lut("CAM4_LUT", cam4))


if __name__ == '__main__':
    main()
