#!/usr/bin/env python3
"""
D435i depth + motion-parallax reflection bench test.

Question under test: can the motion-parallax mask that already works for
RGB reflection filtering (obstacle_detector.py, 2026-06-02 breakthrough) also
be used to null out reflection-corrupted DEPTH pixels on glass? Real, close
surfaces (the window frame) show strong parallax when camera/target shifts a
known amount; a reflected scene (virtual depth via the glass mirror) shows
almost none — same physics, just applied to the depth channel instead of the
RGB line-detection channel.

Reuses compute_optical_flow() / build_motion_mask() from obstacle_detector.py
as-is (already tuned params) instead of reimplementing.

USAGE
-----
    pip install pyrealsense2
    python3 depth_reflection_bench.py

Live window: RGB | colorized Depth | colorized Depth-with-mask-applied

Keys:
    b   capture BEFORE frame (color + depth)
    a   capture AFTER frame, compute motion mask from before→after optical
        flow, apply mask to AFTER depth (kills low-parallax = suspect-
        reflection pixels), show result in the 3rd panel
    r   drag a rectangle to set a detection ROI — candidate search (connected
        components) is restricted to this box, so a noisy/overexposed corner
        elsewhere in frame can't bridge into the sill blob via mask closing
        and drag its protrusion_std over threshold. Background plane fitting
        still uses the full frame (more clean background = better fit), only
        candidate detection is restricted. Drawn as a yellow box on all
        panels. ENTER/SPACE confirms, ESC/c cancels the drag.
    c   clear the ROI (back to full-frame detection)
    s   save 3 debug photos (before RGB / after RGB / result = masked depth
        with detection boxes) + a text sidecar with the numbers behind them
        to ./depth_test_out/<timestamp>_*.{jpg,txt} — for sharing when
        something looks wrong
    q   quit

Workflow: point the camera at the glass+window-frame mockup, press 'b',
physically shift the camera OR the target ~1cm (same as the original bench
methodology — moving the target is easier to reproduce on a bench), press
'a'. Compare panel 2 (raw depth, expect reflection to show as plausible-but-
wrong values) against panel 3 (masked depth, expect reflection region to
drop out / go black while the real window-frame edge survives).

DETECTION (added after mask validation)
----------------------------------------
On each 'a' capture, after masking, the surviving valid-depth blobs are found
(connected components) and each is reported as a frame candidate: near
distance (closest point — matches the `near_edge_cm` field in
camera_obstacle_plan.md's decision schema) and real-world height/width
converted via the depth camera's own intrinsics (no LUT/geometry-projection
guesswork needed here, unlike the RGB-camera pipeline). Drawn as a box +
label on panel 3, printed to console.

PROTRUSION (2026-07-16 rework — plane fit, not a fixed distance constant)
---------------------------------------------------------------------------
The camera is mounted at a downward tilt looking at the sill, not head-on.
That means a bare flat wall/board does NOT sit at one fixed distance across
the frame — different rows see it at different ranges purely from the
viewing angle (this is exactly why the old RGB pipeline needed a per-row
y_px->distance LUT instead of one constant). A single WALL_REFERENCE_M
constant would misread that angle-induced gradient as protrusion.

Because this camera gives real per-pixel depth (not just RGB + geometry
guesswork), we don't need a manually-calibrated LUT to fix this: each 'a'
capture deprojects the valid masked-depth pixels to 3D (camera intrinsics,
no distortion correction) and fits a background plane to them (one outlier-
rejection refit pass excludes the sill's own points so it doesn't bias the
plane). Protrusion is then each blob's perpendicular distance to THAT plane
— correct regardless of tilt, self-calibrating every capture, no angle or
mounting-distance measurement needed.

This is measurement validation only — no step_over/block decision logic yet.
Compare the printed distance/height/protrusion against your mockup's known
physical dimensions to judge accuracy before wiring this into the actual
step-planning decision.
"""

import os
import sys
import time
from pathlib import Path

import cv2
import numpy as np

try:
    import pyrealsense2 as rs
except ImportError:
    print("pyrealsense2 not installed. Run: pip install pyrealsense2", file=sys.stderr)
    sys.exit(1)

# Reuse the already-tuned motion mask from obstacle_detector.py instead of
# reimplementing the same physics twice.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from obstacle_detector import compute_optical_flow, build_motion_mask  # noqa: E402

OUT_DIR = Path(__file__).parent / "depth_test_out"
WIN_NAME = "D435i  RGB | Depth | Masked-Depth  (b/a/s/q)"

# Colorization + detection range (meters). Also the ceiling fed into
# fit_background_plane's plane_valid gate (MAX_DETECT_DISTANCE_M), so this
# isn't just a cosmetic clip — too low and legitimate background points get
# excluded before the plane fit ever sees them.
#
# [2026-07-21] 0.60 -> 0.80: camera is mounted ~50cm above the wall looking
# DOWN at an angle (俯視), not straight-on. At an oblique tilt, the slant
# range to a point at the same physical 50cm standoff is longer than 50cm —
# how much longer depends on the tilt angle, growing further for points
# nearer the top of the frame (more grazing angle = longer slant distance).
# Real bench numbers with the 0.60 ceiling: masked=21.3% of frame passed the
# motion mask (~65k px) but only 82 survived the distance gate — i.e. most of
# the frame's real, in-range background was being thrown out as "too far",
# not actually beyond the physical working distance.
DEPTH_MIN_M = 0.05
DEPTH_MAX_M = 0.80

# 2026-07-16: extra closing on the motion mask, applied only in this depth
# path (obstacle_detector.build_motion_mask's own 5x5 closing is tuned for
# RGB line detection and is left untouched). A flat, low-texture sill gives a
# noisy/speckled Farneback flow signal, which otherwise leaves real holes
# scattered through a genuinely continuous surface — fragmenting one sill
# into several small pieces before connected-components even runs.
DEPTH_MASK_EXTRA_CLOSE_PX = 21

# ── Frame-candidate detection tunables (⚠ placeholders, see module docstring) ──
MIN_BLOB_AREA_PX     = 150    # reject small noise specks after masking
# [2026-07-22] No longer used to REJECT a candidate in detect_frame_candidates
# (distance+shape is the gate now — protrusion needs a background plane,
# which isn't always fittable, see that function's docstring) — kept defined
# since the validation story below is real tuning history, in case
# protrusion-based rejection is worth re-enabling once background-plane
# availability is more reliable (e.g. via the geometry-prior option discussed
# but not yet built).
PROTRUSION_MIN_M     = 0.01   # |protrusion| below this = same plane as background, not a real feature
MAX_DETECT_DISTANCE_M = DEPTH_MAX_M   # ignore anything farther than this — keeps background
                                       # clutter beyond the wall out of detect_frame_candidates

# Depth-homogeneity check (2026-07-16, revised same day): a real window
# corner has the sill touching the wall/window above it with NO gap in valid
# depth between them — connected-components sees one blob spanning two
# different real surfaces (sill + receding wall), which the width/aspect
# filter alone can't catch. This is what produced the -126cm/-91cm nonsense
# readings.
#
# First attempt used a RAW depth-range check (no angle correction) as the
# primary guard — wrong: a real sill viewed at a steep angle naturally has a
# big raw-depth range end-to-end just from perspective (verified: a legit
# 600px-wide single sill under a steep tilt hit 13.5cm raw range and got
# incorrectly rejected/truncated down to a small surviving fragment). Raw
# distance was never the right thing to gate on.
#
# `protrusion_std` (angle-corrected — perpendicular distance to the fitted
# background plane) is the geometrically correct test: a single coherent
# surface at a roughly constant real-world offset has LOW protrusion
# variance regardless of how much its raw depth swings with position; two
# genuinely different surfaces (sill vs. corner wall) do not share one
# offset and show high variance. Verified against both cases with the
# background plane fit the way main() actually does it (against the whole
# frame's valid area, not just the blob in question) — long single sill:
# std 0.4cm (passes); sill+corner-wall mix: std 5.7cm (correctly rejected).
MAX_PROTRUSION_STD_M = 0.02   # 2cm — reject blobs whose internal protrusion spread exceeds this

# Background-plane fit (2026-07-16): camera is tilted, so a bare wall isn't
# at one fixed distance across the frame — fit a plane to the valid points
# each capture instead of assuming a constant. See module docstring.
MIN_PLANE_FIT_POINTS = 500     # too few valid points -> skip detection this capture, don't fit garbage
PLANE_FIT_OUTLIER_M  = 0.015   # 2nd-pass refit excludes points this far off the rough plane
                                # (excludes the sill's own points so they don't bias the fit)

# Sill-shape filter — same intent as obstacle_detector.py's Hough line search
# (HORIZONTAL_TOL_DEG + HOUGH_MIN_LENGTH_PX): a window sill is a WIDE, SHORT
# horizontal bar spanning most of the frame, not an arbitrary blob. Reject
# anything that isn't shaped like that (tall/narrow clutter, isolated specks,
# corners) even if it survived the area/protrusion filters above.
SILL_MIN_ASPECT_RATIO = 2.5   # w_px / h_px must be at least this (wide & short)
SILL_MIN_WIDTH_PX     = 150   # must span at least this much of the 640px frame width (~23%)


def deproject_grid(depth_m: np.ndarray, px_grid: np.ndarray, py_grid: np.ndarray,
                    fx: float, fy: float, ppx: float, ppy: float) -> tuple:
    """Vectorized pinhole deprojection (no lens-distortion correction — the
    RealSense color stream's distortion is small at this resolution/FOV and
    this is a plane-fit input, not a precision measurement). Returns (X, Y, Z)
    arrays in meters, same shape as depth_m."""
    Z = depth_m
    X = (px_grid - ppx) * Z / fx
    Y = (py_grid - ppy) * Z / fy
    return X, Y, Z


PLANE_FIT_REFIT_ITERS = 3   # rough fit + this many outlier-reject/refit rounds


def fit_background_plane(X: np.ndarray, Y: np.ndarray, Z: np.ndarray,
                          valid_mask: np.ndarray):
    """Least-squares fit Z ≈ a*X + b*Y + c over valid_mask points, iterating
    outlier-rejection refits (drop points > PLANE_FIT_OUTLIER_M off the
    CURRENT fit, refit on what's left, repeat) so contamination — the sill's
    own points, or a large protruding region — doesn't leave a lingering
    bias in the background plane it's meant to be measured against. One
    refit pass was tested and left a measurable bias when the contaminating
    region was a sizeable fraction of the frame (background falsely read as
    ~2-3cm 'protruding' from its own fit); iterating converges closer to the
    true background.

    Returns (a, b, c) or None if too few valid points to fit."""
    xs, ys, zs = X[valid_mask], Y[valid_mask], Z[valid_mask]
    if xs.size < MIN_PLANE_FIT_POINTS:
        return None
    A_full = np.column_stack([xs, ys, np.ones_like(xs)])
    A, zs_fit = A_full, zs
    coeffs = None
    for _ in range(1 + PLANE_FIT_REFIT_ITERS):
        coeffs, *_ = np.linalg.lstsq(A, zs_fit, rcond=None)
        a, b, c = coeffs
        norm_mag = float(np.sqrt(a * a + b * b + 1.0))
        residual = (a * xs + b * ys + c - zs) / norm_mag
        inliers = np.abs(residual) < PLANE_FIT_OUTLIER_M
        if inliers.sum() < MIN_PLANE_FIT_POINTS:
            break   # too few inliers to refine further — keep the last good fit
        A, zs_fit = A_full[inliers], zs[inliers]
    return (float(coeffs[0]), float(coeffs[1]), float(coeffs[2]))


def protrusion_map_from_plane(X: np.ndarray, Y: np.ndarray, Z: np.ndarray,
                               plane: tuple) -> np.ndarray:
    """Per-pixel signed perpendicular distance to the fitted background
    plane. Positive = closer to camera than the plane (protrudes toward the
    camera); negative = farther (recessed). Sign/normalization independent
    of the camera's tilt — see module docstring."""
    a, b, c = plane
    norm_mag = np.sqrt(a * a + b * b + 1.0)
    return (a * X + b * Y + c - Z) / norm_mag


# Fragment merge — depth-sensor dropout noise can split ONE physical sill
# into several side-by-side connected components (each individually too
# square/narrow to pass the shape filter above). Same intent as
# obstacle_detector.py's OBSTACLE_GROUP_DY_PX line-cluster merge, just
# applied to depth blobs: merge blobs that are horizontally close, at
# roughly the same row, and roughly the same distance BEFORE the shape
# filter runs, so a fragmented sill still reads as one wide bar.
#
# 2026-07-16: the "same distance" check compares PROTRUSION (angle-corrected),
# not raw near_m — same reasoning as the homogeneity check. A long sill under
# a steep tilt has real raw-depth drift end to end, so two fragments of the
# SAME sill can legitimately differ by more than a few cm in raw distance;
# comparing protrusion instead (consistent offset from the background plane)
# correctly recognizes them as one surface regardless of that drift.
MERGE_MAX_GAP_PX            = 100    # bridge horizontal gaps up to this wide between fragments
MERGE_MAX_Y_GAP_PX          = 40     # fragments must be within this many px vertically to count as "same row"
MERGE_MAX_PROTRUSION_DIFF_M = 0.03   # and within this many meters of each other's protrusion
# [2026-07-22] Only used when `prot` is unavailable for a pair (no background
# plane could be fit this capture — see detect_frame_candidates/handle_after's
# distance-only fallback). Deliberately more permissive than the protrusion
# check: raw distance isn't angle-corrected, so it can't distinguish "same
# tilted sill, natural drift" from "different surface" as cleanly — this is
# a best-effort fallback, not a replacement, for when there's no plane at all
# to compare against. Rough first guess, not yet tuned against real drift
# measurements on an actual long/tilted sill.
MERGE_MAX_NEAR_DIFF_M_FALLBACK = 0.10


def _merge_close_blobs(blobs: list) -> list:
    """Greedily union blobs that are horizontally adjacent, same-row, and at
    a similar protrusion — collapses a noise-fragmented sill back into one
    bounding box before the shape filter sees it. `blobs`: list of dicts
    with x, y, w_px, h_px, near_m, prot (prot may be None if no background
    plane was available this capture — see detect_frame_candidates). Repeats
    until no pair merges.

    [2026-07-22] When either blob's `prot` is None, falls back to comparing
    near_m (raw distance) instead — see MERGE_MAX_NEAR_DIFF_M_FALLBACK for
    why this is a weaker, best-effort substitute, not equivalent to the
    protrusion check.
    """
    merged = list(blobs)
    changed = True
    while changed:
        changed = False
        out = []
        used = [False] * len(merged)
        for i in range(len(merged)):
            if used[i]:
                continue
            a = merged[i]
            for j in range(i + 1, len(merged)):
                if used[j]:
                    continue
                b = merged[j]
                a_x2, b_x2 = a['x'] + a['w_px'], b['x'] + b['w_px']
                a_y2, b_y2 = a['y'] + a['h_px'], b['y'] + b['h_px']
                x_gap = max(b['x'] - a_x2, a['x'] - b_x2, 0)
                # vertical overlap/gap: 0 if ranges overlap, else the gap between them
                y_gap = max(b['y'] - a_y2, a['y'] - b_y2, 0)
                if a['prot'] is not None and b['prot'] is not None:
                    dist_ok = abs(a['prot'] - b['prot']) <= MERGE_MAX_PROTRUSION_DIFF_M
                else:
                    dist_ok = abs(a['near_m'] - b['near_m']) <= MERGE_MAX_NEAR_DIFF_M_FALLBACK
                if x_gap <= MERGE_MAX_GAP_PX and y_gap <= MERGE_MAX_Y_GAP_PX and dist_ok:
                    nx, ny = min(a['x'], b['x']), min(a['y'], b['y'])
                    nx2, ny2 = max(a_x2, b_x2), max(a_y2, b_y2)
                    if a['prot'] is None and b['prot'] is None:
                        merged_prot = None
                    elif a['prot'] is None:
                        merged_prot = b['prot']
                    elif b['prot'] is None:
                        merged_prot = a['prot']
                    else:
                        merged_prot = a['prot'] if abs(a['prot']) >= abs(b['prot']) else b['prot']
                    a = {'x': nx, 'y': ny, 'w_px': nx2 - nx, 'h_px': ny2 - ny,
                         'near_m': min(a['near_m'], b['near_m']), 'prot': merged_prot}
                    used[j] = True
                    changed = True
            out.append(a)
        merged = out
    return merged


def colorize_depth(depth_m: np.ndarray) -> np.ndarray:
    """uint16(mm)-derived depth-in-meters -> BGR heatmap. 0 (invalid) -> black."""
    invalid = depth_m <= 0
    clipped = np.clip(depth_m, DEPTH_MIN_M, DEPTH_MAX_M)
    norm = ((clipped - DEPTH_MIN_M) / (DEPTH_MAX_M - DEPTH_MIN_M) * 255).astype(np.uint8)
    color = cv2.applyColorMap(norm, cv2.COLORMAP_JET)
    color[invalid] = (0, 0, 0)
    return color


def detect_frame_candidates(masked_depth_m: np.ndarray, protrusion_map: np.ndarray,
                             fx: float, fy: float, ppx: float, roi_rect: tuple = None) -> tuple:
    """Connected-component analysis on surviving (post-mask) valid depth,
    filtered down to WINDOW-SILL-SHAPED blobs only (wide + short horizontal
    bar spanning most of the frame) — mirrors obstacle_detector.py's Hough
    line search intent (long + near-horizontal), just applied to depth blobs
    instead of RGB edges.

    `protrusion_map`: per-pixel perpendicular distance to the fitted
    background plane (see fit_background_plane / protrusion_map_from_plane),
    same shape as masked_depth_m — NOT a fixed-distance subtraction, so it's
    valid even though the camera views the sill at a tilt.

    [2026-07-22] `protrusion_map` may be None — no background plane could be
    fit this capture (e.g. not enough separate background material in view
    to fit against; see project notes on the mirror-reflective bench case,
    where the "background" pixel budget within working range was almost
    entirely the test object itself). Distance (near_m) is the primary,
    always-available signal needed to plan the robot's next step; protrusion
    is now best-effort EXTRA context, not a gate — a candidate is no longer
    rejected for having too little or too-noisy protrusion. When
    protrusion_map is None, `protrusion_m` in every candidate is also None
    (height_m is unaffected — it's derived from pixel size + near_m alone,
    not from the plane).

    Each blob = a candidate sill (reflections already screened out by the
    motion mask upstream). Returns (candidates, rejected):
      candidates: **at most one** dict {x, y, w_px, h_px, near_m,
          center_distance_m, height_m, width_m, protrusion_m} — the single
          WIDEST shape-passing blob (see 2026-07-22 note below); protrusion_m
          is None if no plane was available. Kept as a list for API
          stability even though it's now 0 or 1 elements.
      rejected: list of dicts {x, y, w_px, h_px, area, near_m, reasons} for
          every blob that passed the area filter but failed a later one —
          diagnostic output for when nothing survives and it's unclear why.
    `height_m`: the blob's real-world VERTICAL extent (h_px projected via fy)
        — the sill's physical thickness as the robot travels past it.
    `width_m`: the blob's real-world horizontal extent (w_px projected via
        fx) — kept only for the aspect-ratio sill-shape filter / debugging;
        not the primary measurement.
    `protrusion_m`: the blob's most extreme (largest-magnitude, sign kept)
        perpendicular distance to the background plane — positive = sticks
        out toward the camera, negative = recessed. None if no plane.
    `center_distance_m`: closest depth within a narrow column centered on
        `ppx` (the camera's optical center / straight-ahead column) inside
        this candidate's bbox — see the 2026-07-22 note further down for why
        this, not `near_m`, is what WASH_ROBOT.cpp's along-travel remaining-
        distance geometry should actually be fed.
    height_m/width_m via direct pinhole projection (px * Z / f) — no LUT
    needed, unlike the RGB-camera pipeline.

    `roi_rect`: optional (x, y, w, h) in the same pixel grid as
    masked_depth_m. When set, connected-components only runs inside this
    box — a bench-test workaround for scene clutter (an overexposed corner
    object, say) that the motion mask correctly keeps as "real" but that
    isn't the feature under test: without an ROI it can bridge into the
    sill blob via the extra closing pass and blow out protrusion_std. Does
    NOT affect the background plane fit (that still uses the whole frame).
    """
    # Exclude far-away pixels (background beyond the wall, clutter, etc.)
    # BEFORE connected-components — filtering by blob near_m afterward would
    # still let a blob spanning near+far pixels through.
    valid = (masked_depth_m > 0) & (masked_depth_m <= MAX_DETECT_DISTANCE_M)
    if roi_rect is not None:
        rx, ry, rw, rh = roi_rect
        roi_mask = np.zeros_like(valid)
        roi_mask[ry:ry + rh, rx:rx + rw] = True
        valid = valid & roi_mask
    valid = valid.astype(np.uint8) * 255
    num_labels, labels, stats, _ = cv2.connectedComponentsWithStats(valid, connectivity=8)

    raw_blobs = []
    for label in range(1, num_labels):  # 0 = background
        x, y, w_px, h_px, area = stats[label]
        if area < MIN_BLOB_AREA_PX:
            continue   # too small to even be worth logging
        label_mask = (labels == label)
        blob_depths = masked_depth_m[label_mask]
        blob_depths = blob_depths[blob_depths > 0]
        if blob_depths.size == 0:
            continue
        if protrusion_map is not None:
            blob_prot = protrusion_map[label_mask]
            prot_val = float(blob_prot[np.argmax(np.abs(blob_prot))]) if blob_prot.size else None
        else:
            prot_val = None
        raw_blobs.append({'x': int(x), 'y': int(y), 'w_px': int(w_px), 'h_px': int(h_px),
                           'near_m': float(blob_depths.min()), 'prot': prot_val})

    # Depth-sensor dropout can split one physical sill into several
    # side-by-side fragments — merge those back into one bbox before
    # judging shape, else each fragment looks too square/narrow on its own.
    merged_blobs = _merge_close_blobs(raw_blobs)

    candidates = []
    rejected = []
    for blob in merged_blobs:
        x, y, w_px, h_px, near_m = blob['x'], blob['y'], blob['w_px'], blob['h_px'], blob['near_m']
        area = w_px * h_px   # approx post-merge; only used for the diagnostic print
        aspect = (w_px / h_px) if h_px else 0.0

        protrusion_m = None
        protrusion_std_m = None
        if protrusion_map is not None:
            # Merging only tracked bboxes, not pixel labels — re-derive the
            # blob's own valid pixels from the (rectangular) bbox region so
            # the protrusion read-out ignores gaps/other objects that may
            # fall inside the union rectangle.
            #
            # [2026-07-21] Must re-apply the SAME distance ceiling used to
            # build `valid` up in the connected-components step, not just
            # `> 0`. A wide union bbox (e.g. a sill spanning most of the
            # frame width) can have completely unrelated out-of-range
            # clutter sitting inside its rectangle — reflective/cluttered
            # scenes especially, where far mirror-reflected noise sits right
            # next to a real near object. That clutter is already excluded
            # from `valid` (and thus from ever becoming its own labeled
            # blob), but without this gate it leaks back in here via the raw
            # depth array and inflates protrusion_std with a real
            # candidate's own real protrusion, causing a false "spans
            # multiple surfaces" rejection.
            region_slice = masked_depth_m[y:y + h_px, x:x + w_px]
            region_valid = (region_slice > 0) & (region_slice <= MAX_DETECT_DISTANCE_M)
            region_prot = protrusion_map[y:y + h_px, x:x + w_px][region_valid]
            if region_prot.size > 0:
                # [2026-07-21] median, not max-abs — a real sill's top
                # surface is mostly flat, but depth sensors reliably spike at
                # object edges (mixed foreground/background pixels straddling
                # the boundary), and max-abs picks up exactly that single
                # noisy edge pixel instead of the representative height.
                # Real-hardware check: bench measured true protrusion
                # ~3-4cm, max-abs reported 5.9cm. Median is robust to a
                # handful of edge-noise outliers as long as they're a
                # minority of the blob's pixels, which they are for any
                # candidate wide/tall enough to pass the shape filter below.
                protrusion_m = float(np.median(region_prot))
                protrusion_std_m = float(region_prot.std())

        # Sill shape: wide and short, spanning a good chunk of the frame —
        # rejects tall/narrow clutter, corners, and isolated noise specks
        # that happen to survive the motion mask but aren't a horizontal bar.
        # [2026-07-22] Distance (near_m, already gated to MAX_DETECT_DISTANCE_M
        # above) + shape is now the ONLY gate — protrusion (when available)
        # is reported on the accepted candidate but no longer used to reject
        # it. See protrusion_map docstring above for why.
        reasons = []
        if w_px < SILL_MIN_WIDTH_PX:
            reasons.append(f"width {w_px}px<{SILL_MIN_WIDTH_PX}px")
        if h_px == 0 or aspect < SILL_MIN_ASPECT_RATIO:
            reasons.append(f"aspect {aspect:.1f}<{SILL_MIN_ASPECT_RATIO}")
        if reasons:
            rejected.append({'x': int(x), 'y': int(y), 'w_px': int(w_px), 'h_px': int(h_px),
                              'area': int(area), 'near_m': near_m, 'reasons': reasons})
            continue

        height_m = float(h_px) * near_m / fy
        width_m = float(w_px) * near_m / fx
        candidates.append({
            'x': int(x), 'y': int(y), 'w_px': int(w_px), 'h_px': int(h_px),
            'near_m': near_m, 'height_m': height_m, 'width_m': width_m,
            'protrusion_m': protrusion_m,
        })
    candidates.sort(key=lambda c: c['near_m'])

    # [2026-07-22] A real window sill/frame spans nearly the whole facade
    # width the robot is climbing on — by construction (SILL_MIN_ASPECT_RATIO/
    # SILL_MIN_WIDTH_PX are tuned for "a long bar spanning most of the
    # frame") it should be the single widest in-range feature in view. Real-
    # hardware check: with the distance+shape-only fallback (2026-07-22),
    # a small clutter object (a tool, ~150-200px wide) coincidentally passed
    # the same width/aspect minimums the actual plank did, reporting 2
    # candidates instead of 1. Keep only the widest as THE sill; demote the
    # rest back to rejected (with a reason) rather than silently reporting a
    # stray object as if it were the sill.
    if len(candidates) > 1:
        widest = max(candidates, key=lambda c: c['w_px'])
        for c in candidates:
            if c is not widest:
                rejected.append({'x': c['x'], 'y': c['y'], 'w_px': c['w_px'], 'h_px': c['h_px'],
                                  'area': c['w_px'] * c['h_px'], 'near_m': c['near_m'],
                                  'reasons': [f"not the widest candidate ({c['w_px']}px < "
                                              f"widest {widest['w_px']}px) — likely clutter, not the sill"]})
        candidates = [widest]

    # [2026-07-22] center_distance_m — `near_m` is the closest pixel ANYWHERE
    # in the candidate's bbox, which can span most of the frame width. The
    # along-travel geometry conversion in WASH_ROBOT.cpp's
    # cmd_run_depth_avoid (sqrt(min_distance_cm^2 - DEPTH_CAM_STANDOFF_CM^2)
    # - DEPTH_CAM_LEAD_OFFSET_CM) is only exact for a point exactly on the
    # camera's own optical axis (image column == ppx, i.e. straight ahead of
    # the robot) — for any other column, part of the extra slant range is
    # LATERAL (left/right) offset, not forward distance, and that formula
    # has no way to tell the difference, so it overcounts forward distance
    # for an off-axis near_m. Real-hardware bug this fixes: reported
    # remaining_travel_cm ~30cm from a near_m that turned out to be off to
    # one side, actual on-site measurement ~20cm. Restricting the near-point
    # search to a column centered on ppx gives a distance that's actually
    # "straight ahead", matching what the geometry formula assumes. Falls
    # back to near_m if the candidate doesn't span the optical center at all
    # (rare — candidates are wide, near-full-frame blobs by construction).
    center_half_width_px = 30
    for c in candidates:
        col_lo = max(c['x'], int(ppx) - center_half_width_px)
        col_hi = min(c['x'] + c['w_px'], int(ppx) + center_half_width_px)
        center_distance_m = c['near_m']
        if col_hi > col_lo:
            center_slice = masked_depth_m[c['y']:c['y'] + c['h_px'], col_lo:col_hi]
            center_valid = center_slice[center_slice > 0]
            if center_valid.size > 0:
                center_distance_m = float(center_valid.min())
        c['center_distance_m'] = center_distance_m

    rejected.sort(key=lambda r: -r['area'])
    return candidates, rejected


def fit_plane_two_pass(masked_depth_m: np.ndarray, X: np.ndarray, Y: np.ndarray, Z: np.ndarray,
                        plane_valid: np.ndarray, fx: float, fy: float, ppx: float, roi_rect: tuple = None):
    """fit_background_plane, then a second pass that excludes whatever
    blob(s) the first pass found (candidate OR rejected — a real obstacle
    that got rejected for looking "flush with the plane" is exactly the
    contamination case this exists to fix) and refits on what's left.

    2026-07-21: real-hardware bench hit — a wooden plank test obstacle spans
    a big enough fraction of the frame's in-range pixels (tens of thousands
    of px out of a low-six-figure valid-pixel count) that
    fit_background_plane's ordinary least-squares fit folds the plank's own
    points into the "background", and the plank then measures as ~0cm
    protrusion against a plane that is, in part, itself. The existing
    outlier-rejection iterations inside fit_background_plane (PLANE_FIT_
    REFIT_ITERS) can't recover from this — they reject a MINORITY of
    points as outliers, and here the contaminating object isn't a minority.

    Second-pass refit isn't guaranteed to be a strict improvement in every
    scene (if the "blob" pass 1 found was actually noise, excluding it just
    throws away good points for no reason) — but for the case this targets
    (a real object big enough to bias the fit), it's a clear win, and if too
    few points remain to refit (e.g. the excluded regions were most of the
    frame), this falls back to the pass-1 result rather than erroring.

    `roi_rect`: passed straight through to both detect_frame_candidates
    calls (see that function's docstring) — bench-tool-only clutter
    workaround, unused by the production depth_cam_service.py caller.

    Returns (plane, protrusion_map, candidates, rejected, stats) — plane/
    protrusion_map are None and candidates/rejected are empty lists if even
    the first pass can't fit (too few valid points overall); `stats` is
    still populated (pass1-only) in that case for whatever diagnosis is
    possible.

    [2026-07-21] `stats` is diagnostic-only, added after this DIDN'T fix a
    real bench case (mirror-reflective background): need to see WHETHER the
    excluded-plank refit actually changed anything, and whether the
    remaining "background" points are actually plane-shaped at all once the
    plank's gone. A large `refit_residual_std_cm` means the "background"
    itself isn't a coherent surface — i.e. the motion mask let genuine
    mirror-reflection noise leak into `plane_valid` in the first place, and
    no amount of exclude-and-refit here can fix that (the bug would be
    upstream, in the motion mask / obstacle_detector.py threshold, not in
    this function). A near-unchanged plane1 vs plane2 despite excluding a
    big fraction of points would point to a different bug in the exclusion
    itself. Caller decides whether/how to print this — this function stays
    silent like fit_background_plane/detect_frame_candidates.
    """
    stats = {
        'pass1_plane': None, 'pass2_plane': None,
        'pass1_points': int(plane_valid.sum()), 'excluded_points': 0,
        'refit_points': 0, 'refit_residual_mean_cm': None, 'refit_residual_std_cm': None,
    }

    plane1 = fit_background_plane(X, Y, Z, plane_valid)
    if plane1 is None:
        return None, None, [], [], stats
    stats['pass1_plane'] = plane1

    protrusion_map1 = protrusion_map_from_plane(X, Y, Z, plane1)
    candidates1, rejected1 = detect_frame_candidates(masked_depth_m, protrusion_map1, fx, fy, ppx, roi_rect)

    refit_valid = plane_valid.copy()
    for blob in candidates1 + rejected1:
        refit_valid[blob['y']:blob['y'] + blob['h_px'], blob['x']:blob['x'] + blob['w_px']] = False
    stats['excluded_points'] = stats['pass1_points'] - int(refit_valid.sum())
    stats['refit_points'] = int(refit_valid.sum())

    plane2 = fit_background_plane(X, Y, Z, refit_valid)
    if plane2 is None:
        return plane1, protrusion_map1, candidates1, rejected1, stats
    stats['pass2_plane'] = plane2

    protrusion_map2 = protrusion_map_from_plane(X, Y, Z, plane2)
    # Residual of the refit's OWN input against the plane it produced — small
    # means "background" (post-exclusion) is a coherent surface, large means
    # it's still noise (see docstring above).
    refit_residual_m = protrusion_map2[refit_valid]
    stats['refit_residual_mean_cm'] = float(refit_residual_m.mean()) * 100.0
    stats['refit_residual_std_cm'] = float(refit_residual_m.std()) * 100.0

    candidates2, rejected2 = detect_frame_candidates(masked_depth_m, protrusion_map2, fx, fy, ppx, roi_rect)
    return plane2, protrusion_map2, candidates2, rejected2, stats


def draw_candidates(img: np.ndarray, candidates: list) -> np.ndarray:
    """Box + label overlay for detect_frame_candidates() output."""
    out = img.copy()
    for c in candidates:
        x, y, w_px, h_px = c['x'], c['y'], c['w_px'], c['h_px']
        cv2.rectangle(out, (x, y), (x + w_px, y + h_px), (0, 255, 0), 2)
        # protrusion_m is None when no background plane was available this
        # capture (see detect_frame_candidates docstring) — distance/height
        # don't depend on the plane so they're always real numbers.
        prot_label = f"{c['protrusion_m']*100:+.1f}cm" if c['protrusion_m'] is not None else "n/a"
        label = (f"d={c['near_m']*100:.1f}cm  "
                 f"prot={prot_label}  "
                 f"h={c['height_m']*100:.1f}cm")
        cv2.putText(out, label, (x, max(0, y - 8)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2, cv2.LINE_AA)
    return out


def main():
    pipeline = rs.pipeline()
    config = rs.config()
    config.enable_stream(rs.stream.color, 640, 480, rs.format.bgr8, 30)
    config.enable_stream(rs.stream.depth, 640, 480, rs.format.z16, 30)

    try:
        profile = pipeline.start(config)
    except Exception as e:  # noqa: BLE001 - surface the raw librealsense error
        print(f"[depth_bench] pipeline.start() failed: {e}", file=sys.stderr)
        print("[depth_bench] check: USB3 port (not USB2/hub), no other app "
              "holding the camera (close RealSense Viewer first), try a "
              "lower resolution if this persists.", file=sys.stderr)
        sys.exit(1)

    # Depth is captured from a slightly different physical viewpoint than
    # color; align it into the color frame's pixel grid so the RGB-derived
    # motion mask lines up with the depth pixels it's meant to gate. Aligned
    # depth adopts the COLOR sensor's intrinsics, so grab fx/fy from there —
    # that's the projection to use for px->cm conversion on masked_depth_m.
    align = rs.align(rs.stream.color)
    color_intrinsics = profile.get_stream(rs.stream.color).as_video_stream_profile().get_intrinsics()
    fx, fy = color_intrinsics.fx, color_intrinsics.fy
    ppx, ppy = color_intrinsics.ppx, color_intrinsics.ppy
    print(f"[depth_bench] color intrinsics fx={fx:.1f}px fy={fy:.1f}px "
          f"ppx={ppx:.1f} ppy={ppy:.1f}")

    # Pixel-coordinate grid for deproject_grid() — same every frame, build once.
    py_grid, px_grid = np.mgrid[0:480, 0:640]
    px_grid = px_grid.astype(np.float32)
    py_grid = py_grid.astype(np.float32)

    # Resizable window + downscaled preview: 3 panels at native 640x480 stack
    # to 1920px wide, wider than most screens' visible area, so the rightmost
    # panel gets clipped. Preview is shown at DISPLAY_SCALE; saved files (key
    # 's') still use the full-resolution arrays untouched.
    DISPLAY_SCALE = 0.6
    cv2.namedWindow(WIN_NAME, cv2.WINDOW_NORMAL)

    before_gray = None
    before_depth_m = None
    after_color = None
    after_depth_m = None
    mask = None
    depth_raw_color = None
    depth_masked_color = None
    last_candidates = []
    last_rejected = []
    last_threshold = 0.0
    last_valid_pct = (0.0, 0.0)
    roi_rect = None   # (x, y, w, h) full-res pixel coords, or None = full frame

    print("[depth_bench] b=capture BEFORE  a=capture AFTER+mask  r=set ROI  "
          "c=clear ROI  s=save  q=quit")

    try:
        while True:
            frames = pipeline.wait_for_frames()
            frames = align.process(frames)
            color_frame = frames.get_color_frame()
            depth_frame = frames.get_depth_frame()
            if not color_frame or not depth_frame:
                continue

            color = np.asanyarray(color_frame.get_data())
            depth_mm = np.asanyarray(depth_frame.get_data())
            depth_m = depth_mm.astype(np.float32) / 1000.0
            depth_raw_color = colorize_depth(depth_m)

            display_color = color.copy()
            display_depth_raw = depth_raw_color.copy()
            if roi_rect is not None:
                rx, ry, rw, rh = roi_rect
                cv2.rectangle(display_color, (rx, ry), (rx + rw, ry + rh), (0, 255, 255), 2)
                cv2.rectangle(display_depth_raw, (rx, ry), (rx + rw, ry + rh), (0, 255, 255), 2)

            panels = [display_color, display_depth_raw]
            if depth_masked_color is not None:
                panels.append(depth_masked_color)
            stacked = np.hstack(panels)
            preview = cv2.resize(stacked, None, fx=DISPLAY_SCALE, fy=DISPLAY_SCALE,
                                  interpolation=cv2.INTER_AREA)
            cv2.imshow(WIN_NAME, preview)

            key = cv2.waitKey(1) & 0xFF
            if key == ord('q'):
                break

            elif key == ord('r'):
                print("[depth_bench] a new window popped up at native "
                      "resolution — drag a box on IT (not the main window), "
                      "then ENTER/SPACE to confirm (ESC to cancel)")
                roi_win = "Select ROI - drag, then ENTER"
                sel = cv2.selectROI(roi_win, color, showCrosshair=True, fromCenter=False)
                cv2.destroyWindow(roi_win)
                if sel[2] > 0 and sel[3] > 0:
                    roi_rect = (int(sel[0]), int(sel[1]), int(sel[2]), int(sel[3]))
                    print(f"[depth_bench] ROI set to {roi_rect}")
                else:
                    print("[depth_bench] ROI selection cancelled")

            elif key == ord('c'):
                roi_rect = None
                print("[depth_bench] ROI cleared — detecting over the full frame")

            elif key == ord('b'):
                before_gray = cv2.cvtColor(color, cv2.COLOR_BGR2GRAY)
                before_depth_m = depth_m.copy()
                depth_masked_color = None
                mask = None
                print("[depth_bench] BEFORE captured — now shift camera/target "
                      "~1cm, then press 'a'")

            elif key == ord('a'):
                if before_gray is None:
                    print("[depth_bench] press 'b' first")
                    continue
                after_color = color.copy()
                after_gray = cv2.cvtColor(color, cv2.COLOR_BGR2GRAY)
                after_depth_m = depth_m.copy()

                # Mirrors obstacle_detector.motion_parallax_detect's exact
                # convention: flow(after, before) so the mask lands aligned
                # to AFTER's pixel grid — that's the depth frame we keep.
                flow = compute_optical_flow(after_gray, before_gray)
                mask, mag, threshold = build_motion_mask(flow)

                # Extra closing pass, depth-path-only (does NOT touch
                # obstacle_detector.build_motion_mask, so the proven RGB
                # pipeline is untouched). Farneback flow on a flat, low-
                # texture surface (a plain sill) is noisy — build_motion_mask's
                # own 5x5 closing isn't enough to stop that noise from
                # speckling holes through an otherwise-real, continuous
                # surface, fragmenting it before connected-components even
                # runs. A bigger kernel here bridges those speckle holes.
                mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE,
                                         cv2.getStructuringElement(cv2.MORPH_RECT,
                                             (DEPTH_MASK_EXTRA_CLOSE_PX, DEPTH_MASK_EXTRA_CLOSE_PX)))

                masked_depth_m = after_depth_m.copy()
                masked_depth_m[mask == 0] = 0.0  # null low-parallax = suspect reflection
                depth_masked_color = colorize_depth(masked_depth_m)

                valid_before_pct = float((after_depth_m > 0).mean() * 100)
                valid_after_pct = float((masked_depth_m > 0).mean() * 100)
                print(f"[depth_bench] AFTER captured — motion threshold={threshold:.2f}px  "
                      f"valid depth: raw={valid_before_pct:.1f}% -> masked={valid_after_pct:.1f}%")

                # Deproject to 3D and fit the background plane fresh from THIS
                # capture — self-calibrating against the camera's tilt, no
                # mounting-angle/distance measurement needed. See module docstring.
                X, Y, Z = deproject_grid(masked_depth_m, px_grid, py_grid, fx, fy, ppx, ppy)
                plane_valid = (masked_depth_m > 0) & (masked_depth_m <= MAX_DETECT_DISTANCE_M)
                # [2026-07-21] Two-pass fit — see fit_plane_two_pass docstring.
                # A large-enough real obstacle in-frame can bias a single
                # naive least-squares fit into folding itself into the
                # "background", measuring as ~0cm protrusion against a plane
                # that's partly itself.
                plane, protrusion_map, candidates, rejected, fit_stats = fit_plane_two_pass(
                    masked_depth_m, X, Y, Z, plane_valid, fx, fy, ppx, roi_rect)
                if plane is None:
                    # [2026-07-22] Distance+shape fallback — see the matching
                    # comment in depth_cam_service.py's handle_after(). No
                    # plane doesn't mean no detection anymore, just no
                    # protrusion/height-severity this capture.
                    print(f"[depth_bench] only {int(plane_valid.sum())} valid points "
                          f"(<{MIN_PLANE_FIT_POINTS} needed) — no background plane "
                          f"this capture, falling back to distance+shape only (no protrusion)")
                    candidates, rejected = detect_frame_candidates(masked_depth_m, None, fx, fy, ppx, roi_rect)
                elif fit_stats['pass2_plane'] is not None:
                    print(f"[depth_bench] two-pass refit: excluded {fit_stats['excluded_points']}px, "
                          f"refit on {fit_stats['refit_points']}px, "
                          f"refit residual mean={fit_stats['refit_residual_mean_cm']:+.1f}cm "
                          f"std={fit_stats['refit_residual_std_cm']:.1f}cm")
                else:
                    print(f"[depth_bench] two-pass refit: pass 2 skipped "
                          f"(only {fit_stats['refit_points']}px left after excluding "
                          f"{fit_stats['excluded_points']}px — using pass-1 plane)")
                depth_masked_color = draw_candidates(depth_masked_color, candidates)
                if roi_rect is not None:
                    rx, ry, rw, rh = roi_rect
                    cv2.rectangle(depth_masked_color, (rx, ry), (rx + rw, ry + rh), (0, 255, 255), 2)
                # Always show rejected fragments, not just when candidates is
                # empty — a capture can find ONE candidate while silently
                # dropping other real fragments of the same sill, which is
                # exactly the "why did it only grab part of the frame" case.
                if not candidates:
                    print(f"[depth_bench] no frame candidates survived masking — "
                          f"{len(rejected)} blob(s) passed the area filter but failed later:")
                else:
                    print(f"[depth_bench] {len(candidates)} candidate(s), "
                          f"{len(rejected)} other blob(s) rejected:")
                for r in rejected[:10]:   # biggest-area first, cap the spam
                    print(f"[depth_bench]   rejected: near={r['near_m']*100:.1f}cm "
                          f"bbox_px=({r['x']},{r['y']},{r['w_px']}x{r['h_px']}) "
                          f"area={r['area']}px -- {', '.join(r['reasons'])}")
                for i, c in enumerate(candidates):
                    prot_str = f"{c['protrusion_m']*100:+.1f}cm" if c['protrusion_m'] is not None else "n/a"
                    print(f"[depth_bench]   candidate {i}: near={c['near_m']*100:.1f}cm "
                          f"protrusion={prot_str} "
                          f"height={c['height_m']*100:.1f}cm "
                          f"bbox_px=({c['x']},{c['y']},{c['w_px']}x{c['h_px']})")

                # Remember this capture's numbers for the 's' save (key 's'
                # bundles these into a text sidecar alongside the 3 photos).
                last_candidates = candidates
                last_rejected = rejected
                last_threshold = threshold
                last_valid_pct = (valid_before_pct, valid_after_pct)

            elif key == ord('s'):
                if mask is None:
                    print("[depth_bench] nothing to save yet — capture b then a first")
                    continue
                OUT_DIR.mkdir(exist_ok=True)
                ts = int(time.time())
                # Exactly 3 debug photos (b / a / result) + a text sidecar with
                # the numbers behind them, so a screenshot alone isn't missing
                # context when shared for debugging later.
                cv2.imwrite(str(OUT_DIR / f"{ts}_b_before.jpg"),
                            cv2.cvtColor(before_gray, cv2.COLOR_GRAY2BGR))
                cv2.imwrite(str(OUT_DIR / f"{ts}_a_after.jpg"), after_color)
                cv2.imwrite(str(OUT_DIR / f"{ts}_result.jpg"), depth_masked_color)

                lines = [
                    f"motion_threshold_px={last_threshold:.2f}",
                    f"valid_depth_pct raw={last_valid_pct[0]:.1f}% masked={last_valid_pct[1]:.1f}%",
                    f"candidates={len(last_candidates)}",
                ]
                for i, c in enumerate(last_candidates):
                    prot_str = f"{c['protrusion_m']*100:+.1f}cm" if c['protrusion_m'] is not None else "n/a"
                    lines.append(f"  candidate {i}: near={c['near_m']*100:.1f}cm "
                                 f"protrusion={prot_str} "
                                 f"height={c['height_m']*100:.1f}cm "
                                 f"bbox_px=({c['x']},{c['y']},{c['w_px']}x{c['h_px']})")
                # Always include rejected fragments, not just when there are
                # zero candidates — otherwise a capture that found ONE
                # candidate silently hides why other real fragments (of the
                # same sill) got dropped.
                lines.append(f"rejected={len(last_rejected)}")
                for r in last_rejected[:10]:
                    lines.append(f"  rejected: near={r['near_m']*100:.1f}cm "
                                  f"bbox_px=({r['x']},{r['y']},{r['w_px']}x{r['h_px']}) "
                                  f"area={r['area']}px -- {', '.join(r['reasons'])}")
                (OUT_DIR / f"{ts}_info.txt").write_text("\n".join(lines), encoding="utf-8")

                print(f"[depth_bench] saved {OUT_DIR}/{ts}_b_before.jpg, "
                      f"{ts}_a_after.jpg, {ts}_result.jpg, {ts}_info.txt")
    finally:
        pipeline.stop()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
