#!/bin/bash
# bench_capture_motion.sh — bench helper for motion parallax obstacle detection.
#
# Workflow:
#   1. Snapshot current cam3 + cam4 frames as "before"
#   2. Pause for user to physically shift robot 1-3cm
#   3. Snapshot again as "after"
#   4. Run obstacle_detector.py in motion mode for both cams
#   5. Print results side by side
#
# Usage:
#   ./bench_capture_motion.sh
#
# Prereq:
#   - frame_capture.py running, writing /tmp/cam3_latest.jpg + /tmp/cam4_latest.jpg
#   - obstacle_detector.py path set in DETECTOR (override via env var DETECTOR=...)

set -u

# ── config ─────────────────────────────────────────────────────────────
DETECTOR="${DETECTOR:-/home/nexuni/projects/obstacle_detector.py}"
COMBINER="${COMBINER:-/home/nexuni/projects/obstacle_combine.py}"
CAM3_LATEST="/tmp/cam3_latest.jpg"
CAM4_LATEST="/tmp/cam4_latest.jpg"
CAM3_BEFORE="/tmp/cam3_before.jpg"
CAM3_AFTER="/tmp/cam3_after.jpg"
CAM4_BEFORE="/tmp/cam4_before.jpg"
CAM4_AFTER="/tmp/cam4_after.jpg"
DEBUG_OUT="${DEBUG_OUT:-/tmp/dbg/}"
FRAME_STALE_LIMIT=5   # seconds — warn if frame_capture latest is older than this

# ── colors (terminal) ──────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m'   # no color

# ── helper functions ───────────────────────────────────────────────────
check_file() {
    local path="$1"
    local label="$2"
    if [ ! -f "$path" ]; then
        echo -e "${RED}[ERR]${NC} $label not found: $path"
        return 1
    fi
    local age=$(( $(date +%s) - $(stat -c %Y "$path") ))
    if [ "$age" -gt "$FRAME_STALE_LIMIT" ]; then
        echo -e "${YELLOW}[WARN]${NC} $label is ${age}s old (>${FRAME_STALE_LIMIT}s) — frame_capture stuck?"
    fi
    return 0
}

snap() {
    local src="$1"
    local dst="$2"
    cp "$src" "$dst" || { echo -e "${RED}[ERR]${NC} cp $src → $dst failed"; exit 1; }
    # quick file size sanity check
    local sz=$(stat -c %s "$dst")
    if [ "$sz" -lt 10000 ]; then
        echo -e "${YELLOW}[WARN]${NC} $dst is only ${sz} bytes (suspiciously small)"
    fi
}

run_detector() {
    local cam_id="$1"
    local before="$2"
    local after="$3"
    echo -e "${BLUE}━━━ $cam_id ━━━${NC}"
    python3 "$DETECTOR" \
        --motion-before "$before" \
        --motion-after "$after" \
        --motion-cam "$cam_id" \
        --debug-out "$DEBUG_OUT" \
        --pretty
}

run_combined() {
    echo -e "${BLUE}━━━ COMBINED (OR — conservative) ━━━${NC}"
    if [ ! -f "$COMBINER" ]; then
        echo -e "${YELLOW}[WARN]${NC} combiner not found: $COMBINER — skipping combine step"
        echo "       Set env var COMBINER=/path/to/obstacle_combine.py"
        return
    fi
    python3 "$COMBINER" \
        --cam3-before "$CAM3_BEFORE" --cam3-after "$CAM3_AFTER" \
        --cam4-before "$CAM4_BEFORE" --cam4-after "$CAM4_AFTER" \
        --detector "$DETECTOR" \
        --debug-out "$DEBUG_OUT" \
        --pretty
}

# ── main ───────────────────────────────────────────────────────────────
echo -e "${BLUE}=== bench motion parallax capture ===${NC}"

# Sanity check detector + frame_capture output
if [ ! -f "$DETECTOR" ]; then
    echo -e "${RED}[ERR]${NC} detector not found: $DETECTOR"
    echo "       Set env var DETECTOR=/path/to/obstacle_detector.py to override"
    exit 1
fi

echo "Detector:    $DETECTOR"
echo "Debug out:   $DEBUG_OUT"
echo ""

check_file "$CAM3_LATEST" "cam3 latest" || exit 1
check_file "$CAM4_LATEST" "cam4 latest" || exit 1
echo ""

# Step 1: snapshot BEFORE
echo -e "${BLUE}[1/4]${NC} Snapshotting BEFORE (current pos)..."
snap "$CAM3_LATEST" "$CAM3_BEFORE"
snap "$CAM4_LATEST" "$CAM4_BEFORE"
echo "  cam3 → $CAM3_BEFORE"
echo "  cam4 → $CAM4_BEFORE"
echo ""

# Step 2: user shifts robot
echo -e "${YELLOW}[2/4] 請手動位移機體 1-3cm (推一下 / crane retract 1cm / 上滑台移)${NC}"
echo -n "      Done? Press Enter to snapshot AFTER (or Ctrl-C to abort): "
read -r _

# Step 3: snapshot AFTER (wait a moment so frame_capture has fresh frame post-movement)
echo ""
echo -e "${BLUE}[3/4]${NC} Waiting 1s for frame_capture to update..."
sleep 1
echo "       Snapshotting AFTER (new pos)..."
snap "$CAM3_LATEST" "$CAM3_AFTER"
snap "$CAM4_LATEST" "$CAM4_AFTER"
echo "  cam3 → $CAM3_AFTER"
echo "  cam4 → $CAM4_AFTER"
echo ""

# Step 4: run combined detector (cam3 + cam4 + OR-combine)
echo -e "${BLUE}[4/4]${NC} Running detector + combiner..."
echo ""
run_combined
echo ""

echo -e "${GREEN}=== done ===${NC}"
echo "Debug images in: $DEBUG_OUT"
echo "  mag_heatmap.jpg     — flow magnitude (bright = high motion)"
echo "  motion_mask.jpg     — white = kept (real obstacle), black = masked (reflection)"
echo "  masked_after.jpg    — after-frame with mask applied"
echo "  annotated.jpg       — detected features overlaid on after-frame"
echo "  before_after.jpg    — side by side reference"
