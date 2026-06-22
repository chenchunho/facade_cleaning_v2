#!/usr/bin/env python3
"""
Window-frame / sill obstacle detector for washrobot step_down avoidance.

STANDALONE: pure OpenCV + numpy. No UDP server, no washrobot connection.
For testing with calibration images before integrating into FrameAnalyzer.

USAGE
-----
    # Single-frame test
    python3 obstacle_detector.py \
        --cam3 cal_cam3_obs_25cm.jpg \
        --cam4 cal_cam4_obs_25cm.jpg

    # Batch over a folder (auto-pair by filename pattern)
    python3 obstacle_detector.py --batch D:/工作/觀賞用/

    # Dump annotated debug frames
    python3 obstacle_detector.py \
        --cam3 X.jpg --cam4 Y.jpg --debug-out /tmp/debug/

GEOMETRY NOTES
--------------
Cameras:
  - cam3 .112 bottom-left, cam4 .113 bottom-right
  - 54° downward tilt, optical axis hits wall at 25cm distance
  - Camera-to-wall distance: ~18cm (feet cup 9.7cm + offset 8.5cm)
  - Camera-to-feet offset: 6.5cm (camera 5-8cm forward of feet contact point)

Robot:
  - Top row to bottom row: 48.8cm gap + 20cm cup dia = 68.8cm center-to-center
  - Cup diameter 20cm (radius 10cm)
  - Each row: feet (orange) on sides + body (green) in middle, 8cm lateral offset

Decision strategy (User's plan, 2026-06-01):
  - Default step_cm = 25
  - Look-ahead: feet_distance ≤ 25cm range
  - if obstacle:
      * try step_short = feet_distance - 1 (cup edge safety)
      * if step_short < STEP_MIN (5) → step_over = feet_distance + W + offset
      * if step_over > STEP_MAX (40) → BLOCK
  - if obstacle_height > 8cm (ZDT clearance) → always BLOCK
  - Two-step crossing: step 1 clears bottom row; obstacle ends between rows.
    Strategy B (current): alert user, don't auto-plan step 2.
"""

import argparse
import json
import os
import re
import sys
import time
from dataclasses import asdict, dataclass, field
from glob import glob
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import cv2
import numpy as np

# ────────────────────────────────────────────────────────────────────
# Constants (geometry / calibration)
# ────────────────────────────────────────────────────────────────────

# Frame dimensions (mainstream JPEG from frame_capture)
FRAME_W = 800
FRAME_H = 448

# ROI (avoid fisheye edges — central 80% horizontal, full vertical)
ROI_X1 = 80
ROI_X2 = 720
ROI_Y1 = 0
ROI_Y2 = 448

# Camera-to-feet offset (cm). camera reads `camera_distance_cm`, feet are
# OFFSET cm BEHIND the camera viewing direction. So feet_dist = cam_dist - offset.
# 2026-06-04: 6.5 → 13.5 — bench 實測 cup 比舊 6.5 估計再近 7cm
# (吸盤伸出時 cup tip 比 camera 更靠前，原 6.5 是 camera 跟 robot body 邊緣的距離，
# 沒算到 cup 從 body 再伸出去那段)
CAM_TO_FEET_OFFSET_CM = 13.5

# ZDT cup max retract from wall (cm). Obstacle taller than this can't be
# cleared even with full retract → BLOCK.
ZDT_MAX_CLEARANCE_CM = 8.0

# Step size constraints (cm)
STEP_DEFAULT_CM = 25
STEP_MIN_CM     = 5
STEP_MAX_CM     = 50  # 2026-06-05: 45 → 50 — 與 WASH_ROBOT::STEP_CM_MAX 對齊
STEP_SAFETY_CM  = 1   # cup edge safety for stop-short

# Cup geometry (for decision logic)
CUP_RADIUS_CM   = 10

# Body 前腳 cup 跟 feet 前腳 cup 在 step direction 的段位差。
# 結構：body 在 feet 下方 (body leads descent direction)，camera 裝在 body 前腳。
# Phase 2 feet 要追上 body anchor point + step，feet 是 trailing cup —
# step_over 公式要保 feet trailing edge 也跨過 obstacle far edge，所以要加這 10cm。
# See memory project_machine_geometry.md.
BODY_FEET_OFFSET_CM = 10

# LUTs: image y-pixel (top of plank / obstacle band) → camera_distance_cm
# Built from 2026-06-01 bench calibration (rainbow plank at 10/15/25/35/45/50cm)
# Format: [(y_px, camera_distance_cm), ...] sorted by y_px DESCENDING
# (large y_px = bottom of image = closer obstacle)
CAM3_LUT: List[Tuple[int, float]] = [
    (410, 10.0),
    (340, 15.0),
    (220, 25.0),
    (135, 35.0),
    ( 70, 45.0),
    ( 40, 50.0),
]
CAM4_LUT: List[Tuple[int, float]] = [
    (390, 10.0),
    (330, 15.0),   # interpolated (re-measure later)
    (210, 25.0),
    (125, 35.0),   # interpolated
    ( 65, 45.0),   # interpolated
    ( 50, 50.0),
]

# Detection thresholds (2026-06-01 tuned from bench calibration set)
HOUGH_THRESHOLD       = 80    # min votes for a line
HOUGH_MIN_LENGTH_PX   = 200   # min line segment length (need long horizontal — real
                              # wall obstacles span much of frame width; workshop
                              # clutter is short edges)
HOUGH_MAX_GAP_PX      = 30    # join nearby segments
HORIZONTAL_TOL_DEG    = 25    # accept lines within ±25° of horizontal
                              # (slack for fisheye curvature at frame edges)

# Classification thresholds (heuristic, tune from bench data — 2026-06-01)
# We use |shadow_diff| as primary obstacle signal: wood plank (dark) vs floor/wall (lighter)
# produces a strong vertical brightness gradient at the plank edge.
# 2026-06-04: 30 → 25 — bench 觀察真實板出現在 frame 中段時 shadow_diff 只有 ~27，
# 卡在舊門檻 30 邊界外被誤判 ambiguous → decision proceed → 會撞。
OBSTACLE_MIN_ABS_SHADOW    = 25   # |above - below| > this → obstacle
CRACK_MAX_ABS_SHADOW       = 8    # |above - below| < this → crack (no real height)
OBSTACLE_GROUP_DY_PX       = 80   # merge multiple lines within this y range (single plank
                                  # generates many parallel lines → group as 1 obstacle)
OBSTACLE_MIN_LINE_LENGTH_PX = 180  # require long line for "obstacle" classification
                                   # 2026-06-02: 350 → 180 — motion parallax already
                                   # filters reflections, so length filter only needs
                                   # to gate noise (plank lines are ~200-280 px)
# 2026-06-04: backup signal — 即使 shadow_diff 沒過 OBSTACLE_MIN_ABS_SHADOW，
# 只要線夠長（明顯的大物體）就升 obstacle，confidence 稍低（0.7 vs 1.0）。
# 為了 cover 弱對比真實 obstacle（中段擺位的木板可能 shadow 只 +20~+28）。
OBSTACLE_BACKUP_LINE_LENGTH_PX = 250

# Cross-camera matching: a feature is "real" if cam3 and cam4 both see one
# within this distance tolerance (cm). 4 → 8 (looser, cam3/cam4 distance estimates
# can diverge by several cm due to LUT interpolation + fisheye)
CROSS_CAM_DIST_TOL_CM      = 8.0

# ────────────────────────────────────────────────────────────────────
# Data classes
# ────────────────────────────────────────────────────────────────────

@dataclass
class LineCandidate:
    """Raw horizontal line detected by Hough."""
    y_center_px: int
    x1_px: int
    x2_px: int
    length_px: int

@dataclass
class Feature:
    """Classified feature (obstacle / crack / ambiguous)."""
    type: str                          # 'obstacle' | 'crack' | 'ambiguous'
    y_center_px: int
    camera_distance_cm: float
    feet_distance_cm: float
    estimated_width_cm: float = 0.0    # along travel direction (rough)
    estimated_height_cm: float = 0.0   # rough from thickness/shadow
    thickness_px: int = 0
    shadow_diff: float = 0.0
    confidence: float = 0.0
    source_cam: str = ""               # 'cam3' or 'cam4'

@dataclass
class Decision:
    """Result of step planning."""
    action: str        # 'proceed' | 'short' | 'over' | 'block'
    step_cm: float
    reason: str
    alert: Optional[str] = None   # extra warning for user

@dataclass
class Result:
    """Full detector output."""
    timestamp: float
    cam3_path: str
    cam4_path: str
    cam3_features: List[Feature] = field(default_factory=list)
    cam4_features: List[Feature] = field(default_factory=list)
    matched_features: List[Feature] = field(default_factory=list)
    filtered_reflections: int = 0
    decision: Optional[Decision] = None

# ────────────────────────────────────────────────────────────────────
# Distance LUT (y_px → camera_distance_cm)
# ────────────────────────────────────────────────────────────────────

def y_to_distance(y_px: int, lut: List[Tuple[int, float]]) -> Optional[float]:
    """Piecewise linear interpolation with edge extrapolation.

    Returns:
      - within LUT range → interpolated distance
      - just outside (< tol px) → linearly extrapolated using nearest 2 points
      - far outside → None (feature truly out of reliable detection zone)

    2026-06-04: 「太近」方向（large y_px）保留寬鬆 tolerance — miss 近物會撞牆，安全考量。
    「太遠」方向（small y_px）改嚴 — 之前舊外插值會把 y=23 算成 cam_dist=37cm，
    實際物理上沒這麼遠（LUT 邊緣 slope 跟真實 geometry 已開始發散）。
    現在縮成 5px tolerance，超出就 return None（這條線不會被當 feature 用，
    遠物等下一次 step 接近時自然會落入 LUT 範圍才偵測）。"""
    EXTRAP_TOL_NEAR_PX = 20   # large y (太近) — 保留寬鬆，安全優先
    EXTRAP_TOL_FAR_PX  = 5    # small y (太遠) — 嚴格，避免亂外插

    # Above top of LUT (y too large = obstacle too close to bottom of frame)
    if y_px >= lut[0][0]:
        if y_px <= lut[0][0] + EXTRAP_TOL_NEAR_PX:
            # Extrapolate using top 2 points
            y_hi, d_lo = lut[0]
            y_lo, d_hi = lut[1]
            slope = (d_hi - d_lo) / (y_lo - y_hi)
            return max(0.0, d_lo + slope * (y_hi - y_px))
        return None
    # Below bottom of LUT (y too small = obstacle too close to top of frame)
    if y_px <= lut[-1][0]:
        if y_px >= lut[-1][0] - EXTRAP_TOL_FAR_PX:
            y_hi, d_lo = lut[-2]
            y_lo, d_hi = lut[-1]
            slope = (d_hi - d_lo) / (y_lo - y_hi)
            return d_lo + slope * (y_hi - y_px)
        return None
    for i in range(len(lut) - 1):
        y_hi, d_lo = lut[i]
        y_lo, d_hi = lut[i + 1]
        if y_lo <= y_px <= y_hi:
            t = (y_hi - y_px) / (y_hi - y_lo)
            return d_lo + t * (d_hi - d_lo)
    return None

# ────────────────────────────────────────────────────────────────────
# Line detection
# ────────────────────────────────────────────────────────────────────

def crop_roi(img: np.ndarray) -> Tuple[np.ndarray, Tuple[int, int]]:
    """Crop to central ROI. Returns (cropped_img, (x_offset, y_offset))."""
    if img.shape[1] < ROI_X2 or img.shape[0] < ROI_Y2:
        # Smaller frame than expected — use full image
        return img, (0, 0)
    return img[ROI_Y1:ROI_Y2, ROI_X1:ROI_X2].copy(), (ROI_X1, ROI_Y1)

def detect_horizontal_lines(img_gray: np.ndarray, roi_offset: Tuple[int, int]) -> List[LineCandidate]:
    """Canny + Hough P → horizontal-ish line segments. Coords in ORIGINAL frame."""
    # Smooth slightly to suppress small textures
    blurred = cv2.GaussianBlur(img_gray, (5, 5), 0)
    edges   = cv2.Canny(blurred, 40, 120)

    lines = cv2.HoughLinesP(
        edges,
        rho=1,
        theta=np.pi / 180,
        threshold=HOUGH_THRESHOLD,
        minLineLength=HOUGH_MIN_LENGTH_PX,
        maxLineGap=HOUGH_MAX_GAP_PX,
    )
    if lines is None:
        return []

    out: List[LineCandidate] = []
    ox, oy = roi_offset
    for ln in lines:
        x1, y1, x2, y2 = ln[0]
        # angle from horizontal
        dx = x2 - x1
        dy = y2 - y1
        if dx == 0:
            continue
        angle_deg = abs(np.degrees(np.arctan2(dy, dx)))
        # Treat angles near 0 or 180 as horizontal
        angle_from_horizontal = min(angle_deg, abs(180 - angle_deg))
        if angle_from_horizontal > HORIZONTAL_TOL_DEG:
            continue
        y_c = int((y1 + y2) / 2) + oy
        out.append(LineCandidate(
            y_center_px=y_c,
            x1_px=int(min(x1, x2)) + ox,
            x2_px=int(max(x1, x2)) + ox,
            length_px=int(np.hypot(dx, dy)),
        ))
    return out

# ────────────────────────────────────────────────────────────────────
# Per-line classification (obstacle / crack / ambiguous)
# ────────────────────────────────────────────────────────────────────

def measure_thickness(img_gray: np.ndarray, y_center: int, x_lo: int, x_hi: int) -> int:
    """Measure vertical "band" of edge density around y_center.
    Wide band → obstacle (a 3D thing occupying multiple pixels vertically).
    Thin band → crack (sharp single edge).
    Returns thickness in pixels (band where edge density exceeds floor)."""
    # Sample column at midpoint; could do average over x range for robustness
    x_sample = (x_lo + x_hi) // 2
    half = 60
    y0 = max(0, y_center - half)
    y1 = min(img_gray.shape[0], y_center + half)
    col = img_gray[y0:y1, x_sample]
    if len(col) < 10:
        return 0
    # Compute |gradient| along column
    grad = np.abs(np.diff(col.astype(np.int32)))
    threshold = max(20, np.median(grad) * 3)
    band = grad >= threshold
    # Largest contiguous band
    if not band.any():
        return 0
    # Run-length: find longest True run
    changes = np.diff(band.astype(np.int8))
    starts = np.where(changes == 1)[0] + 1
    ends   = np.where(changes == -1)[0] + 1
    if band[0]:
        starts = np.r_[0, starts]
    if band[-1]:
        ends = np.r_[ends, len(band)]
    if len(starts) == 0 or len(ends) == 0:
        return 0
    lengths = ends - starts
    return int(lengths.max())

def measure_shadow(img_gray: np.ndarray, y_center: int, x_lo: int, x_hi: int) -> float:
    """Brightness DIFFERENCE: avg(above_line) − avg(below_line).
    Positive → line has dark band below (shadow → obstacle).
    Near zero → flat seam / crack."""
    half = 25
    y_above_top    = max(0, y_center - 2 * half)
    y_above_bot    = max(0, y_center - 3)         # 3px gap to avoid edge itself
    y_below_top    = min(img_gray.shape[0], y_center + 3)
    y_below_bot    = min(img_gray.shape[0], y_center + 2 * half)
    if y_above_top >= y_above_bot or y_below_top >= y_below_bot:
        return 0.0
    above = img_gray[y_above_top:y_above_bot, x_lo:x_hi]
    below = img_gray[y_below_top:y_below_bot, x_lo:x_hi]
    if above.size == 0 or below.size == 0:
        return 0.0
    return float(above.mean() - below.mean())

def _line_quality_penalty(img_gray: np.ndarray, mag: Optional[np.ndarray],
                          line: LineCandidate,
                          thickness: Optional[int] = None
                          ) -> Tuple[float, float, float]:
    """Penalty 0.0~1.0 (1.0 = perfect line). Returns (penalty, residual_std_px, motion_cv).

    Three sub-checks (multiplied together):
    A. Straightness — re-run Canny in band around line, for each column find
       nearest edge y, compute std of (edge_y - predicted_y). Low std = straight.
       For curvy objects (cables, wires), edges deviate from Hough line significantly.
    B. Motion uniformity — coefficient of variation (std/mean) of motion magnitude
       in band around line. Real wall obstacle = uniform mag. Thin object on
       different-depth surface (cable on floor) = motion varies. Only checked
       if `mag` provided.
    C. Thickness (2026-06-09c) — cable/wire 通常 thickness 1-2px，真實 wall obstacle
       (wood/aluminum frame) thickness ≥ 3-4px。若 caller 沒傳 thickness 跳過此層。

    Thresholds (2026-06-09 v3):
    - Straightness: OK ≤ 3px, BAD ≥ 8px → penalty 1.0 → 0.5 linear
    - Motion CV: OK ≤ 0.6, BAD ≥ 1.2 → penalty 1.0 → 0.5 linear
    - Thickness: OK ≥ 3px, BAD = 1px → penalty 1.0 → 0.5 linear
      (2026-06-09g: 從 OK=4 放寬到 OK=3，木板 thickness=2 救回 obstacle)
    """
    # ── A. Straightness check ──────────────────────────────────────
    band_half = 10
    y_pred = line.y_center_px
    x1, x2 = line.x1_px, line.x2_px
    y_lo = max(0, y_pred - band_half)
    y_hi = min(img_gray.shape[0], y_pred + band_half + 1)
    band = img_gray[y_lo:y_hi, x1:x2 + 1]
    residual_std = 0.0
    if band.shape[0] >= 5 and band.shape[1] >= 10:
        blurred_band = cv2.GaussianBlur(band, (3, 3), 0)
        band_edges = cv2.Canny(blurred_band, 40, 120)
        center_in_band = y_pred - y_lo
        residuals = []
        for col_idx in range(band_edges.shape[1]):
            edge_ys = np.where(band_edges[:, col_idx] > 0)[0]
            if len(edge_ys) == 0:
                continue
            nearest = edge_ys[np.argmin(np.abs(edge_ys - center_in_band))]
            residuals.append(int(nearest) - center_in_band)
        if len(residuals) >= 5:
            residual_std = float(np.std(residuals))

    straight_penalty = 1.0
    RESID_OK_PX  = 3.0
    RESID_BAD_PX = 8.0
    if residual_std > RESID_OK_PX:
        t = min(1.0, (residual_std - RESID_OK_PX) / (RESID_BAD_PX - RESID_OK_PX))
        straight_penalty = 1.0 - 0.5 * t

    # ── B. Motion uniformity check ─────────────────────────────────
    motion_cv = -1.0   # -1 = not measured
    motion_penalty = 1.0
    if mag is not None and mag.shape == img_gray.shape:
        band_mag = mag[y_lo:y_hi, x1:x2 + 1]
        if band_mag.size > 0 and band_mag.mean() > 0.01:
            motion_cv = float(band_mag.std() / band_mag.mean())
            CV_OK  = 0.6
            CV_BAD = 1.2
            if motion_cv > CV_OK:
                t = min(1.0, (motion_cv - CV_OK) / (CV_BAD - CV_OK))
                motion_penalty = 1.0 - 0.5 * t

    # ── C. Thickness check (2026-06-09c, 放寬 2026-06-09g) ──────────
    # Cable/wire ~1-2px、wall obstacle (wood/Al frame) ≥ 3-4px
    # 2026-06-09g: THICK_OK 4→3。bench 木板 thickness=2 經常出現（gradient
    # band 算法限制），原 4→1 penalty 0.67 把木板誤殺成 ambiguous。
    # 放寬後 thickness=2 penalty=0.75，cable thickness=1 仍 penalty 0.5。
    thickness_penalty = 1.0
    if thickness is not None:
        THICK_OK  = 3
        THICK_BAD = 1
        if thickness < THICK_OK:
            t = min(1.0, (THICK_OK - thickness) / (THICK_OK - THICK_BAD))
            thickness_penalty = 1.0 - 0.5 * t

    return straight_penalty * motion_penalty * thickness_penalty, residual_std, motion_cv


def classify_line(img_gray: np.ndarray, line: LineCandidate,
                  mag: Optional[np.ndarray] = None) -> Tuple[str, float, int, float]:
    """Return (type, confidence, thickness_px, shadow_diff).

    Primary signal: |shadow_diff| — strong brightness contrast above vs below
    the line means a 3D object occludes light differently (= obstacle).
    Cracks / flat seams: low brightness contrast.

    2026-06-09: 加 line quality penalty (straightness + motion uniformity)
    用來降「黃色電線」「不規則邊緣」這類 bench 雜物的 confidence，避免單側
    cam 看到雜物導致 cross-cam AND combine 仍被觸發的風險。`mag` 為 motion
    magnitude (motion_parallax_detect 路徑會提供)；其他路徑省略只啟用 straightness。
    """
    thickness = measure_thickness(img_gray, line.y_center_px, line.x1_px, line.x2_px)
    shadow    = measure_shadow(img_gray,    line.y_center_px, line.x1_px, line.x2_px)
    abs_shadow = abs(shadow)

    quality_penalty, _resid, _cv = _line_quality_penalty(
        img_gray, mag, line, thickness=thickness)
    # Quality < threshold = obviously curvy / non-uniform → downgrade obstacle to ambiguous
    # 2026-06-09g: 0.5 → 0.4。木板 bench case cam3 thickness=2 邊緣 penalty ~0.43，
    # 0.5 會誤殺；0.4 保住 obstacle。Cable thickness=1 + curvy 通常 penalty < 0.3 仍擋。
    QUALITY_DOWNGRADE_TH = 0.4

    if abs_shadow >= OBSTACLE_MIN_ABS_SHADOW:
        # Long horizontal line + strong contrast → real obstacle.
        # Short line with strong contrast = workshop clutter → downgrade.
        if line.length_px >= OBSTACLE_MIN_LINE_LENGTH_PX:
            conf = min(1.0, 0.6 + abs_shadow / 150.0 + line.length_px / 1500.0)
            conf *= quality_penalty
            if quality_penalty < QUALITY_DOWNGRADE_TH:
                return 'ambiguous', conf, thickness, shadow
            return 'obstacle', conf, thickness, shadow
        # Strong contrast but short → ambiguous
        return 'ambiguous', 0.4 * quality_penalty, thickness, shadow
    # 2026-06-04: backup — 弱對比但超長線 → 仍升 obstacle (conf 略低)。
    # cover「擺中段、shadow_diff 不夠強但物理上明顯大」的情境。
    # 但必須有最低 shadow 訊號 (|shadow| >= 10)，避免地板裂縫 / 雜物的長橫線
    # (shadow ≈ 0) 被誤升為 obstacle (observed bench 2026-06-04 y=315 shadow=+1.7 偽陽性)
    BACKUP_MIN_ABS_SHADOW = 10
    if line.length_px >= OBSTACLE_BACKUP_LINE_LENGTH_PX and abs_shadow >= BACKUP_MIN_ABS_SHADOW:
        conf = 0.7 * quality_penalty
        if quality_penalty < QUALITY_DOWNGRADE_TH:
            return 'ambiguous', conf, thickness, shadow
        return 'obstacle', conf, thickness, shadow
    if abs_shadow < CRACK_MAX_ABS_SHADOW:
        # Visible line but no significant contrast → flat feature
        return 'crack', 0.6, thickness, shadow
    return 'ambiguous', 0.3, thickness, shadow

# ────────────────────────────────────────────────────────────────────
# Per-camera processing
# ────────────────────────────────────────────────────────────────────

def imread_unicode(path: str) -> Optional[np.ndarray]:
    """cv2.imread + Windows unicode path support."""
    try:
        arr = np.fromfile(path, dtype=np.uint8)
        if arr.size == 0:
            return None
        return cv2.imdecode(arr, cv2.IMREAD_COLOR)
    except Exception:
        return None

def imwrite_unicode(path: str, img: np.ndarray) -> bool:
    """cv2.imwrite + Windows unicode path support."""
    try:
        ext = os.path.splitext(path)[1]
        ok, buf = cv2.imencode(ext or '.jpg', img)
        if not ok:
            return False
        buf.tofile(path)
        return True
    except Exception:
        return False

def process_camera(img_path: str, cam_id: str) -> List[Feature]:
    """Load image, detect obstacles, return Feature list."""
    img = imread_unicode(img_path)
    if img is None:
        print(f"[detector] WARN: cannot read {img_path}", file=sys.stderr)
        return []
    if img.shape[:2] != (FRAME_H, FRAME_W):
        # Resize if necessary (e.g. mainstream different aspect ratio)
        img = cv2.resize(img, (FRAME_W, FRAME_H))
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    roi_gray, roi_off = crop_roi(gray)
    lines = detect_horizontal_lines(roi_gray, roi_off)

    lut = CAM3_LUT if cam_id == 'cam3' else CAM4_LUT
    features: List[Feature] = []

    # Deduplicate lines (within 8px y center → same physical edge)
    lines.sort(key=lambda ln: ln.y_center_px, reverse=True)
    dedup: List[LineCandidate] = []
    for ln in lines:
        if dedup and abs(dedup[-1].y_center_px - ln.y_center_px) < 8:
            # Keep the longer one
            if ln.length_px > dedup[-1].length_px:
                dedup[-1] = ln
            continue
        dedup.append(ln)

    # First pass: classify all lines
    raw_features: List[Feature] = []
    for ln in dedup:
        d_cam = y_to_distance(ln.y_center_px, lut)
        if d_cam is None:
            continue   # outside calibrated range
        d_feet = d_cam - CAM_TO_FEET_OFFSET_CM
        if d_feet < 0:
            continue   # impossible (behind feet)
        ftype, conf, thick, shadow = classify_line(gray, ln)
        width_cm_est = max(2.0, (ln.x2_px - ln.x1_px) * 0.04)
        # Height: when grouped (later), span across grouped lines → real height
        height_cm_est = max(0.5, thick * 0.06)
        raw_features.append(Feature(
            type=ftype,
            y_center_px=ln.y_center_px,
            camera_distance_cm=d_cam,
            feet_distance_cm=d_feet,
            estimated_width_cm=width_cm_est,
            estimated_height_cm=height_cm_est,
            thickness_px=thick,
            shadow_diff=shadow,
            confidence=conf,
            source_cam=cam_id,
        ))

    # Second pass: group obstacle features within OBSTACLE_GROUP_DY_PX
    # A plank generates multiple parallel lines (top edge, bottom edge, internal
    # grain). Group them into one feature spanning the cluster — the result's
    # y_center is the BOTTOM (max y_px) = near edge (closer to camera), and the
    # height estimate comes from the y span of the cluster.
    obstacles = [f for f in raw_features if f.type == 'obstacle']
    others    = [f for f in raw_features if f.type != 'obstacle']
    obstacles.sort(key=lambda f: f.y_center_px, reverse=True)

    grouped: List[Feature] = []
    cluster: List[Feature] = []
    for f in obstacles:
        if not cluster or (cluster[-1].y_center_px - f.y_center_px) <= OBSTACLE_GROUP_DY_PX:
            cluster.append(f)
        else:
            grouped.append(_merge_cluster(cluster))
            cluster = [f]
    if cluster:
        grouped.append(_merge_cluster(cluster))

    return grouped + others

def _merge_cluster(cluster: List[Feature]) -> Feature:
    """Merge multiple obstacle lines (same plank) into one feature.
    Use the BOTTOM line (max y_px) as the obstacle's near edge.
    Use the y_px span to estimate height."""
    bottom = max(cluster, key=lambda f: f.y_center_px)
    top    = min(cluster, key=lambda f: f.y_center_px)
    # height proxy: y_px span × distance-dependent cm/px (rough)
    span_px = bottom.y_center_px - top.y_center_px
    # At ~25cm distance: ~5 px/cm vertical, scales with distance
    cm_per_px = max(0.05, bottom.camera_distance_cm / 200.0)
    height_est = max(0.5, span_px * cm_per_px + 1.0)
    # Confidence: max of cluster + bonus for cluster size
    conf = min(1.0, max(f.confidence for f in cluster) + 0.05 * (len(cluster) - 1))
    return Feature(
        type='obstacle',
        y_center_px=bottom.y_center_px,
        camera_distance_cm=bottom.camera_distance_cm,
        feet_distance_cm=bottom.feet_distance_cm,
        estimated_width_cm=max(f.estimated_width_cm for f in cluster),
        estimated_height_cm=height_est,
        thickness_px=span_px,
        shadow_diff=max((f.shadow_diff for f in cluster), key=abs),
        confidence=conf,
        source_cam=cluster[0].source_cam,
    )

# ────────────────────────────────────────────────────────────────────
# Cross-camera matching (filter reflections)
# ────────────────────────────────────────────────────────────────────

def cross_camera_match(cam3_features: List[Feature],
                       cam4_features: List[Feature]) -> Tuple[List[Feature], int]:
    """Real feature appears in BOTH cams at consistent distance.
    Reflection appears in one (or both at wildly different distances).
    Returns (matched, filtered_count)."""
    matched: List[Feature] = []
    used_cam4_idx = set()
    filtered = 0
    for f3 in cam3_features:
        best_j = -1
        best_dd = float('inf')
        for j, f4 in enumerate(cam4_features):
            if j in used_cam4_idx:
                continue
            if f4.type != f3.type and 'ambiguous' not in (f4.type, f3.type):
                continue
            dd = abs(f3.feet_distance_cm - f4.feet_distance_cm)
            if dd < best_dd:
                best_dd = dd
                best_j = j
        if best_j >= 0 and best_dd <= CROSS_CAM_DIST_TOL_CM:
            # Merge: average distance, take max confidence + thickness
            f4 = cam4_features[best_j]
            used_cam4_idx.add(best_j)
            avg_cam = (f3.camera_distance_cm + f4.camera_distance_cm) / 2
            avg_feet = (f3.feet_distance_cm + f4.feet_distance_cm) / 2
            merged_type = f3.type if f3.confidence >= f4.confidence else f4.type
            matched.append(Feature(
                type=merged_type,
                y_center_px=(f3.y_center_px + f4.y_center_px) // 2,
                camera_distance_cm=avg_cam,
                feet_distance_cm=avg_feet,
                estimated_width_cm=max(f3.estimated_width_cm, f4.estimated_width_cm),
                estimated_height_cm=max(f3.estimated_height_cm, f4.estimated_height_cm),
                thickness_px=max(f3.thickness_px, f4.thickness_px),
                shadow_diff=max(f3.shadow_diff, f4.shadow_diff),
                confidence=min(1.0, (f3.confidence + f4.confidence) / 2 + 0.15),  # bonus for cross-cam
                source_cam='both',
            ))
        else:
            filtered += 1
    # Lone cam4 features without cam3 match → also filtered
    filtered += sum(1 for j in range(len(cam4_features)) if j not in used_cam4_idx)
    return matched, filtered

# ────────────────────────────────────────────────────────────────────
# Decision logic (strategy B — single-step only)
# ────────────────────────────────────────────────────────────────────

def decide_step(features: List[Feature]) -> Decision:
    """Plan next step. Strategy B: only handles bottom-row crossing.
    After step over: alert user that obstacle is between rows."""
    obstacles = [f for f in features
                 if f.type == 'obstacle' and f.confidence >= 0.4]

    if not obstacles:
        return Decision(action='proceed', step_cm=STEP_DEFAULT_CM,
                        reason='no_obstacle')

    # Take nearest
    nearest = min(obstacles, key=lambda f: f.feet_distance_cm)
    FD = nearest.feet_distance_cm
    W  = nearest.estimated_width_cm
    H  = nearest.estimated_height_cm

    # Height check
    if H > ZDT_MAX_CLEARANCE_CM:
        return Decision(action='block', step_cm=0,
                        reason=f'obstacle_too_high {H:.1f}cm > {ZDT_MAX_CLEARANCE_CM}cm')

    if FD < 0:
        return Decision(action='block', step_cm=0,
                        reason=f'obstacle_at_or_past_feet FD={FD:.1f}cm')

    # Far enough → default
    if FD > STEP_DEFAULT_CM:
        return Decision(action='proceed', step_cm=STEP_DEFAULT_CM,
                        reason=f'obstacle_far FD={FD:.1f}cm')

    # 2026-06-04 (per Sadie): 預設優先 step_over（跨過去），不再選 step_short
    # 因為 distance 估算誤差 ~3cm，short 容易踩到障礙物中段。
    # 寧多勿少：step_over 額外加 STEP_OVER_SAFETY_CM 預留 cup 落地餘裕。
    STEP_OVER_SAFETY_CM = 3   # 額外越過 obstacle far edge 的安全餘裕
    # 2026-06-05 公式重新推導 (Sadie): 要保 Phase 2 feet (trailing cup) trailing edge
    # 也跨過 obstacle far edge。step = cup_diameter + body_feet_offset + FD + W + safety
    # = 20 + 10 + FD + W + 3 = 33 + FD + W
    # 舊公式 FD+W+CAM_TO_FEET_OFFSET (13.5)+safety 只算到 feet cup edge 過 obstacle，
    # 沒考慮 cup 自身寬度 + body-feet 段位差，導致 body cup 落在 obstacle 上吸不住。
    step_over = FD + W + (2 * CUP_RADIUS_CM) + BODY_FEET_OFFSET_CM + STEP_OVER_SAFETY_CM
    if step_over <= STEP_MAX_CM:
        return Decision(
            action='over',
            step_cm=round(step_over, 1),
            reason=f'jump_over FD={FD:.1f}cm W={W:.1f}cm safety={STEP_OVER_SAFETY_CM}cm',
            alert='obstacle_will_be_between_rows; plan_next_step_manually',
        )

    # step_over 超過行程上限 → 取 STEP_MAX，feet trailing cup 走最遠 (壓最小部分)
    # 新公式 (2026-06-05)：feet trailing edge 新位置 = feet_old + STEP_MAX - cup_radius
    # obstacle far edge = feet_old + (cup_radius + body_feet_offset) + FD + W
    # crushed_into = obstacle_far - feet_trailing_edge = (2*cup_radius + body_feet_offset + FD + W) - STEP_MAX
    # 即 30 + FD + W - STEP_MAX (對 STEP_MAX=50)
    if STEP_MAX_CM > FD:
        crushed_into = max(0.0,
            (2 * CUP_RADIUS_CM + BODY_FEET_OFFSET_CM + FD + W) - STEP_MAX_CM)
        return Decision(
            action='over_partial',
            step_cm=float(STEP_MAX_CM),
            reason=f'partial_over FD={FD:.1f} W={W:.1f} step_max={STEP_MAX_CM} '
                   f'(cannot_fully_clear, crushed_into={crushed_into:.1f}cm)',
            alert='cup_may_crush_obstacle_far_edge; verify_physically',
        )

    # MAX 連 near edge 都到不了 (FD 已超 MAX、obstacle 還更遠) → 走 short 停在前面
    step_short = FD - STEP_SAFETY_CM
    if step_short >= STEP_MIN_CM:
        return Decision(action='short', step_cm=round(step_short, 1),
                        reason=f'stop_before_obstacle FD={FD:.1f}cm '
                               f'(over_needed={step_over:.1f}>{STEP_MAX_CM})')

    return Decision(action='block', step_cm=0,
                    reason=f'no_safe_option FD={FD:.1f}cm step_over={step_over:.1f} '
                           f'step_short={step_short:.1f}')

# ────────────────────────────────────────────────────────────────────
# Main API
# ────────────────────────────────────────────────────────────────────

def detect_pair(cam3_path: str, cam4_path: str) -> Result:
    r = Result(timestamp=time.time(),
               cam3_path=cam3_path,
               cam4_path=cam4_path)
    r.cam3_features = process_camera(cam3_path, 'cam3')
    r.cam4_features = process_camera(cam4_path, 'cam4')
    r.matched_features, r.filtered_reflections = cross_camera_match(
        r.cam3_features, r.cam4_features)
    r.decision = decide_step(r.matched_features)
    return r

def result_to_dict(r: Result) -> dict:
    d = asdict(r)
    return d

# ────────────────────────────────────────────────────────────────────
# Debug visualization
# ────────────────────────────────────────────────────────────────────

COLORS = {
    'obstacle':  (0, 0, 255),     # red
    'crack':     (0, 255, 255),   # yellow
    'ambiguous': (255, 0, 255),   # magenta
}

def annotate(img_path: str, features: List[Feature], out_path: str) -> None:
    img = imread_unicode(img_path)
    if img is None:
        return
    if img.shape[:2] != (FRAME_H, FRAME_W):
        img = cv2.resize(img, (FRAME_W, FRAME_H))
    # ROI box (faint)
    cv2.rectangle(img, (ROI_X1, ROI_Y1), (ROI_X2, ROI_Y2 - 1), (80, 80, 80), 1)
    for f in features:
        c = COLORS.get(f.type, (200, 200, 200))
        y = f.y_center_px
        cv2.line(img, (ROI_X1, y), (ROI_X2, y), c, 2)
        label = f"{f.type} d={f.feet_distance_cm:.0f}cm h={f.estimated_height_cm:.1f}cm conf={f.confidence:.2f}"
        cv2.putText(img, label, (ROI_X1 + 5, y - 6),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, c, 1, cv2.LINE_AA)
    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    imwrite_unicode(out_path, img)

# ────────────────────────────────────────────────────────────────────
# Motion parallax detector (real-vs-reflection discrimination)
# ────────────────────────────────────────────────────────────────────
#
# Physics:
#   When camera shifts by Δ cm parallel to wall surface:
#     * Real wall features (at ~20cm camera distance) move by ~K1 px in image
#       (strong parallax — close objects shift a lot per cm of camera motion)
#     * Reflected workshop scene (at virtual depth 2-5m via glass mirror)
#       moves by ~K2 px in image where K2 << K1 (far objects barely shift)
#   → Dense optical flow magnitude separates them cleanly.
#
# Usage:
#   1. Robot at position P, capture frame_before
#   2. Robot moves down ~2-3cm (or slide cam mount sideways 1cm on bench)
#   3. Capture frame_after
#   4. Pass both to detector → motion_mask kills reflections; Hough runs on
#      mask → only real-wall lines survive

# Motion threshold: pixels with flow magnitude exceeding this are "moving"
# (= real wall features). Tune empirically — depends on actual camera shift.
# 2026-06-05: 2.5 → 1.5 — bench 觀察細長物體 (木條 ~1-2cm 厚) 在 Farnebäck flow
# 下產生的 magnitude 很小（線本身內部無紋理，只有上下邊緣有梯度），舊 threshold
# (=median*2.5) 把這些細物體 mask 掉。降到 1.5 讓邊緣信號也能 pass.
MOTION_MAG_FACTOR = 1.5   # threshold = median × factor
MOTION_MIN_MAG    = 1.5   # absolute floor (px) — flow noise below this
MOTION_MASK_DILATE_PX = 5  # dilate mask to capture line edges

def compute_optical_flow(gray_before: np.ndarray, gray_after: np.ndarray) -> np.ndarray:
    """Dense Farnebäck optical flow. Returns (H, W, 2) float32 flow vectors."""
    flow = cv2.calcOpticalFlowFarneback(
        gray_before, gray_after,
        flow=None,
        pyr_scale=0.5, levels=3, winsize=21,
        iterations=3, poly_n=7, poly_sigma=1.5, flags=0)
    return flow

def build_motion_mask(flow: np.ndarray) -> Tuple[np.ndarray, np.ndarray, float]:
    """From flow vectors compute (motion_mask, magnitude_map, threshold).
    mask: uint8 0/255, motion_mag > threshold."""
    mag = np.sqrt(flow[..., 0] ** 2 + flow[..., 1] ** 2).astype(np.float32)
    # 2026-06-04: trim fisheye-distorted left/right 15% before computing median.
    # 邊緣畸變區 motion 被人工放大（fisheye 投影 → 邊緣 pixel 對 camera 位移敏感），
    # 把整體 median 拉高、threshold 變太嚴 → 中央真實 obstacle 反而被 mask 殺掉
    # (observed cam3 fisheye 嚴重時 median=8.7 threshold=21.9 木板 motion ~20-25
    # 剛好被切掉 → features=[])。
    h, w = mag.shape
    trim_x = int(w * 0.15)
    central_mag = mag[:, trim_x:w - trim_x]
    median = float(np.median(central_mag))
    threshold = max(MOTION_MIN_MAG, median * MOTION_MAG_FACTOR)
    # mask 仍套全圖（邊緣 high-motion 可能對 ROI 內 line 影響不大、保留邊緣也可
    # 順便 catch 邊緣物體；只在 threshold 計算時 trim）
    mask = (mag > threshold).astype(np.uint8) * 255
    # Morphological closing fills holes inside motion blobs (e.g. wood grain
    # inside a plank may not have uniform motion → would leave the plank
    # body riddled with holes, breaking Hough line detection)
    # 2026-06-05: 15 → 5 — 大 kernel 把細長物體 (木條 thin rod) 吞掉。
    # 5px closing 保留細物體輪廓、仍能補正常 plank 內部小破洞。
    k_close = cv2.getStructuringElement(cv2.MORPH_RECT, (5, 5))
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, k_close)
    # Dilate so lines on the boundary of "real" regions don't get clipped
    if MOTION_MASK_DILATE_PX > 0:
        k = cv2.getStructuringElement(cv2.MORPH_RECT,
                                       (MOTION_MASK_DILATE_PX, MOTION_MASK_DILATE_PX))
        mask = cv2.dilate(mask, k)
    return mask, mag, threshold

def motion_parallax_detect(before_path: str, after_path: str, cam_id: str,
                           debug_out: Optional[str] = None) -> dict:
    """Run motion parallax detection. Returns result dict + (if debug_out) saves
    visualization frames."""
    img_before = imread_unicode(before_path)
    img_after  = imread_unicode(after_path)
    if img_before is None or img_after is None:
        return {'error': 'cannot_load_images', 'before': before_path, 'after': after_path}
    if img_before.shape != img_after.shape:
        return {'error': 'frame_shape_mismatch'}
    # Resize if not 800x448
    if img_before.shape[:2] != (FRAME_H, FRAME_W):
        img_before = cv2.resize(img_before, (FRAME_W, FRAME_H))
        img_after  = cv2.resize(img_after,  (FRAME_W, FRAME_H))

    gray_before = cv2.cvtColor(img_before, cv2.COLOR_BGR2GRAY)
    gray_after  = cv2.cvtColor(img_after,  cv2.COLOR_BGR2GRAY)

    # 2026-06-04 (per Sadie): 用 AFTER 幀算 distance，因為 detector 是給「下一步」
    # 規劃用 — after 幀代表 cup 現在實際位置（上一步的最後）。
    # Flow 方向倒過來算 (after → before)，這樣 mask 在 after 幀座標系統下對齊。
    flow = compute_optical_flow(gray_after, gray_before)
    mask, mag, threshold = build_motion_mask(flow)

    # 2026-06-04: Motion 太小 → before/after 幾乎一樣，mask threshold 變小、
    # 結果含大量雜訊偽陽性。直接 block 並要求 user 重拍。
    # 2026-06-09: 從「整張 median」改成「p90 AND max」雙條件：
    # 木板等局部 obstacle 只佔畫面一部分 → median 被大背景拉低、但 p90 / max 仍高。
    # 只要 p90 或 max 有任一個夠大，就視為「有東西在動」、允許 detector 跑。
    # 兩者都很小才確定整張靜止 → block。
    MOTION_MIN_P90_PX = 3.0    # 90th percentile < 3px = 大部分區域沒動
    MOTION_MIN_MAX_PX = 10.0   # max < 10px = 連局部都沒移動 → 整張真的靜止
    motion_median = float(np.median(mag))
    motion_p90    = float(np.percentile(mag, 90))
    motion_max    = float(np.max(mag))
    if motion_p90 < MOTION_MIN_P90_PX and motion_max < MOTION_MIN_MAX_PX:
        return {
            'mode': 'motion_parallax',
            'cam_id': cam_id,
            'before_path': before_path,
            'after_path': after_path,
            'motion_threshold_px': float(threshold),
            'motion_median_px': motion_median,
            'motion_p90_px': motion_p90,
            'motion_max_px': motion_max,
            'mask_coverage_pct': float(mask.sum() / 255 / mask.size * 100),
            'warning': f'insufficient_motion p90={motion_p90:.2f}px max={motion_max:.2f}px',
            'features': [],
            'decision': asdict(Decision(action='block', step_cm=0,
                                         reason=f'motion_too_small p90={motion_p90:.2f}px '
                                                f'max={motion_max:.2f}px '
                                                f'(機體位移不夠 / before+after 太接近、重拍)')),
        }

    # Apply mask to AFTER-frame: zero out static pixels (= reflections)
    masked_gray = cv2.bitwise_and(gray_after, gray_after, mask=mask)

    # Run line detection on masked image
    roi_gray, roi_off = crop_roi(masked_gray)
    lines = detect_horizontal_lines(roi_gray, roi_off)

    # Classify each surviving line (use AFTER full-image gray for shadow
    # measurement — match the line detection frame for consistent geometry).
    # 2026-06-09: 傳 mag 進去啟用 line quality (straightness + motion uniformity) penalty。
    lut = CAM3_LUT if cam_id == 'cam3' else CAM4_LUT
    features = []
    for ln in lines:
        d_cam = y_to_distance(ln.y_center_px, lut)
        if d_cam is None:
            continue
        d_feet = d_cam - CAM_TO_FEET_OFFSET_CM
        if d_feet < 0:
            continue
        ftype, conf, thick, shadow = classify_line(gray_after, ln, mag=mag)
        features.append({
            'type': ftype,
            'y_center_px': ln.y_center_px,
            'camera_distance_cm': d_cam,
            'feet_distance_cm': d_feet,
            'line_length_px': ln.length_px,
            'thickness_px': thick,
            'shadow_diff': float(shadow),
            'confidence': conf,
            'source': 'motion_parallax',
        })

    # 2026-06-05: Raw line fallback — detect lines on after frame WITHOUT motion mask.
    # Catches thin/uniform features (wood strips, window edges, wire) that motion-
    # parallax mask kills (Farnebäck flow on thin lines = weak signal → masked out).
    # Marked as obstacle conf=0.5 (above decide_step's 0.4 threshold, but lower than
    # motion-parallax obstacles 0.7-1.0). Dedupe vs existing motion features by y_px.
    roi_gray_raw, roi_off_raw = crop_roi(gray_after)
    raw_lines = detect_horizontal_lines(roi_gray_raw, roi_off_raw)
    DEDUP_Y_TOL_PX = 30
    existing_ys = [f['y_center_px'] for f in features]
    for ln in raw_lines:
        # Skip if too close to motion-path feature (dedupe)
        if any(abs(ln.y_center_px - y) < DEDUP_Y_TOL_PX for y in existing_ys):
            continue
        d_cam = y_to_distance(ln.y_center_px, lut)
        if d_cam is None:
            continue
        d_feet = d_cam - CAM_TO_FEET_OFFSET_CM
        if d_feet < 0:
            continue
        # Still call classify_line for diagnostic shadow_diff/thickness, but
        # OVERRIDE type to 'obstacle' since we deliberately bypassed motion filter
        # (fallback's whole purpose is to catch what motion missed).
        # 2026-06-09: 也傳 mag 用 quality penalty 過濾 raw_fallback 偽陽性。
        _, q_conf, thick, shadow = classify_line(gray_after, ln, mag=mag)
        # Take the quality penalty as a confidence cap for raw_fallback (max 0.5)
        rf_conf = min(0.5, q_conf if q_conf > 0 else 0.5)
        features.append({
            'type': 'obstacle',
            'y_center_px': ln.y_center_px,
            'camera_distance_cm': d_cam,
            'feet_distance_cm': d_feet,
            'line_length_px': ln.length_px,
            'thickness_px': thick,
            'shadow_diff': float(shadow),
            'confidence': rf_conf,
            'source': 'raw_fallback',
        })

    # ── 2026-06-09h: Y-proximity grouping for accurate W estimation ──
    # 舊：W = line.x2-x1 = obstacle 沿牆「橫向 lateral 長度」(image X 軸)
    #      但 step_over 公式要的 W 是 obstacle 在 step direction 的厚度 (image Y 軸)
    #      → 兩個是垂直的維度、公式 W 一直被高估
    #
    # 新：把 features 按 y_px 接近的 cluster 成組（top edge + bottom edge 視為同物體）
    #     - 組內 ≥2 line：W = Y span × cm_per_px（真實厚度）
    #     - 組內 1 line（只看到 top edge）：W = DEFAULT_OBSTACLE_W_CM = 3cm（保守 fallback）
    OBSTACLE_GROUP_DY_PX  = 80
    DEFAULT_OBSTACLE_W_CM = 3.0

    # Sort features by y ascending, assign group_id by gap
    features_sorted = sorted(features, key=lambda f: f['y_center_px'])
    group_id = 0
    for i, f in enumerate(features_sorted):
        if i == 0:
            f['_group_id'] = group_id
        else:
            prev = features_sorted[i - 1]
            if f['y_center_px'] - prev['y_center_px'] <= OBSTACLE_GROUP_DY_PX:
                f['_group_id'] = prev['_group_id']
            else:
                group_id += 1
                f['_group_id'] = group_id

    # Compute W per group + annotate each feature
    groups: Dict[int, List[dict]] = {}
    for f in features_sorted:
        groups.setdefault(f['_group_id'], []).append(f)
    for gid, members in groups.items():
        ys = [m['y_center_px'] for m in members]
        span_px = max(ys) - min(ys)
        if len(members) >= 2 and span_px > 0:
            d_near = max(m['camera_distance_cm'] for m in members)
            cm_per_px = max(0.05, d_near / 200.0)
            group_w = max(DEFAULT_OBSTACLE_W_CM, span_px * cm_per_px)
        else:
            group_w = DEFAULT_OBSTACLE_W_CM
        for m in members:
            m['estimated_width_cm'] = group_w
            m['_group_size']        = len(members)
            m['_group_span_px']     = span_px
    # cleanup internal field
    for f in features:
        f.pop('_group_id', None)

    # Decision (reuse single-cam decision — no cross-cam matching in motion mode)
    fake_features = [Feature(
        type=f['type'], y_center_px=f['y_center_px'],
        camera_distance_cm=f['camera_distance_cm'],
        feet_distance_cm=f['feet_distance_cm'],
        estimated_width_cm=f.get('estimated_width_cm', DEFAULT_OBSTACLE_W_CM),
        estimated_height_cm=max(0.5, f['thickness_px'] * 0.06),
        thickness_px=f['thickness_px'],
        shadow_diff=f['shadow_diff'],
        confidence=f['confidence'],
        source_cam=cam_id,
    ) for f in features]
    decision = decide_step(fake_features)

    result = {
        'mode': 'motion_parallax',
        'cam_id': cam_id,
        'before_path': before_path,
        'after_path': after_path,
        'motion_threshold_px': float(threshold),
        'motion_median_px': float(np.median(mag)),
        'motion_max_px': float(np.max(mag)),
        'mask_coverage_pct': float(mask.sum() / 255 / mask.size * 100),
        'features': features,
        'decision': asdict(decision),
    }

    if debug_out:
        Path(debug_out).mkdir(parents=True, exist_ok=True)
        # 1. Flow magnitude heatmap (jet colormap, normalized)
        mag_norm = np.clip(mag / max(1.0, threshold * 2), 0, 1)
        heatmap = cv2.applyColorMap((mag_norm * 255).astype(np.uint8), cv2.COLORMAP_JET)
        imwrite_unicode(os.path.join(debug_out, 'mag_heatmap.jpg'), heatmap)
        # 2. Motion mask
        mask_vis = cv2.cvtColor(mask, cv2.COLOR_GRAY2BGR)
        imwrite_unicode(os.path.join(debug_out, 'motion_mask.jpg'), mask_vis)
        # 3. Masked image (reflections killed) — applied to AFTER frame
        # (matches what feature detection runs on, per 2026-06-04 flip)
        masked_color = cv2.bitwise_and(img_after, img_after, mask=mask)
        imwrite_unicode(os.path.join(debug_out, 'masked_after.jpg'), masked_color)
        # 4. Annotated AFTER frame with detected lines
        ann = img_after.copy()
        cv2.rectangle(ann, (ROI_X1, ROI_Y1), (ROI_X2, ROI_Y2 - 1), (80, 80, 80), 1)
        for f in features:
            c = COLORS.get(f['type'], (200, 200, 200))
            y = f['y_center_px']
            cv2.line(ann, (ROI_X1, y), (ROI_X2, y), c, 2)
            lbl = f"{f['type']} d={f['feet_distance_cm']:.0f}cm c={f['confidence']:.2f}"
            cv2.putText(ann, lbl, (ROI_X1 + 5, y - 6),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.45, c, 1, cv2.LINE_AA)
        imwrite_unicode(os.path.join(debug_out, 'annotated.jpg'), ann)
        # 5. Side-by-side before/after (just for visual reference)
        sbs = np.hstack([img_before, img_after])
        imwrite_unicode(os.path.join(debug_out, 'before_after.jpg'), sbs)

    return result

# ────────────────────────────────────────────────────────────────────
# CLI
# ────────────────────────────────────────────────────────────────────

def parse_cli():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cam3', help='cam3 JPEG path (single-frame test)')
    ap.add_argument('--cam4', help='cam4 JPEG path (single-frame test)')
    ap.add_argument('--batch', help='Folder for batch (expects cam3/ + cam4/ subfolders OR pair by filename)')
    ap.add_argument('--debug-out', help='Folder to dump annotated debug frames')
    ap.add_argument('--pretty', action='store_true', help='Pretty-print JSON')

    # [2026-06-02] Motion parallax mode: discriminate real wall features from
    # reflections using optical flow between two camera positions.
    ap.add_argument('--motion-before', help='Frame BEFORE camera moved (motion parallax mode)')
    ap.add_argument('--motion-after',  help='Frame AFTER camera moved (motion parallax mode)')
    ap.add_argument('--motion-cam', default='cam3', choices=['cam3', 'cam4'],
                    help='Which LUT to use for distance (default cam3)')
    return ap.parse_args()

def find_pairs(folder: str) -> List[Tuple[str, str, str]]:
    """Find (label, cam3_path, cam4_path) tuples in folder.
    Strategy:
      1. If folder has cam3/ and cam4/ subfolders, pair by matching filenames.
      2. Else look for files matching *cam3*.jpg + *cam4*.jpg with same suffix.
    """
    p = Path(folder)
    pairs: List[Tuple[str, str, str]] = []
    cam3_dir = p / 'cam3'
    cam4_dir = p / 'cam4'
    if cam3_dir.is_dir() and cam4_dir.is_dir():
        for f3 in sorted(cam3_dir.glob('*.jpg')):
            f4 = cam4_dir / f3.name
            if f4.exists():
                pairs.append((f3.stem, str(f3), str(f4)))
        return pairs
    # Fallback: pattern match in single folder
    cam3_files = sorted(p.glob('*cam3*.jpg'))
    for f3 in cam3_files:
        candidate = Path(str(f3).replace('cam3', 'cam4'))
        if candidate.exists():
            pairs.append((f3.stem, str(f3), str(candidate)))
    return pairs

def main():
    args = parse_cli()

    # Motion parallax mode (NEW 2026-06-02)
    if args.motion_before and args.motion_after:
        result = motion_parallax_detect(
            args.motion_before, args.motion_after,
            args.motion_cam, debug_out=args.debug_out)
        if args.pretty:
            print(json.dumps(result, indent=2, ensure_ascii=False))
        else:
            print(json.dumps(result, ensure_ascii=False))
        return

    pairs: List[Tuple[str, str, str]] = []
    if args.cam3 and args.cam4:
        pairs.append(('single', args.cam3, args.cam4))
    elif args.batch:
        pairs = find_pairs(args.batch)
        if not pairs:
            print(f"[detector] no cam3/cam4 pairs found in {args.batch}", file=sys.stderr)
            sys.exit(2)
    else:
        print("Need --cam3+--cam4 OR --batch", file=sys.stderr)
        sys.exit(2)

    all_results = []
    for label, cam3p, cam4p in pairs:
        r = detect_pair(cam3p, cam4p)
        d = result_to_dict(r)
        d['label'] = label
        all_results.append(d)

        if args.debug_out:
            out3 = os.path.join(args.debug_out, f"{label}_cam3.jpg")
            out4 = os.path.join(args.debug_out, f"{label}_cam4.jpg")
            annotate(cam3p, r.cam3_features, out3)
            annotate(cam4p, r.cam4_features, out4)
            # Annotated "matched" version overlaid on cam3
            out_match = os.path.join(args.debug_out, f"{label}_matched.jpg")
            annotate(cam3p, r.matched_features, out_match)

    if args.pretty:
        print(json.dumps(all_results, indent=2, ensure_ascii=False))
    else:
        print(json.dumps(all_results, ensure_ascii=False))

if __name__ == '__main__':
    main()
