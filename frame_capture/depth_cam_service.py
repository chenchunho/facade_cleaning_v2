#!/usr/bin/env python3
# ============================================================================
# depth_cam_service.py — persistent D435i obstacle-detection service for the
# depth-camera continuous step-down loop (WASH_ROBOT.cpp cmd_run_depth_avoid).
#
# Architecture (mirrors cleaning_arm/motor_api's arm_cmd_ TCP pattern, and
# frame_capture.py's HTTP /snap/<cam_id> pattern):
#
#   WASH_ROBOT.cpp --TCP :9530 text cmds--> depth_cam_service.py
#                                                |
#                                                +-- pyrealsense2 pipeline
#                                                |   (kept open, background
#                                                |    thread always has the
#                                                |    latest color+depth)
#                                                +-- HTTP :5008 /snap/depth
#                                                |   (last AFTER result —
#                                                |    annotated/masked depth,
#                                                |    web_backend reverse-
#                                                |    proxies this for the
#                                                |    depth_avoid modal)
#                                                +-- HTTP :5008 /snap/depth_before
#                                                |   /snap/depth_after
#                                                |   (raw, un-annotated color
#                                                |    frame from that step's
#                                                |    BEFORE/AFTER capture —
#                                                |    2026-07-21, so a
#                                                |    candidates=0 step can
#                                                |    still be visually
#                                                |    debugged, see AFTER doc
#                                                |    below)
#                                                +-- HTTP :5008 /snap/depth_live
#                                                |   /snap/depth_live_depth
#                                                    (raw CURRENT color frame /
#                                                     colorized CURRENT depth
#                                                     map, no analysis,
#                                                     independent of any
#                                                     BEFORE/AFTER cycle —
#                                                     manual "拍照" button in
#                                                     the standalone camera
#                                                     panel shows both)
#
# Detection pipeline itself is NOT reimplemented here — reuses
# depth_reflection_bench.py's plane-fit / protrusion / sill-shape-filter /
# fragment-merge functions as-is (see that file's module docstring for the
# angle-correction rationale behind each step) and obstacle_detector.py's
# compute_optical_flow / build_motion_mask (same reuse depth_reflection_bench
# itself does). This file only adds: persistent pipeline + TCP command loop +
# HTTP snapshot serving, i.e. turns the interactive bench tool into a
# long-running service callable from C++.
#
# TCP protocol (line-based, \n-terminated, plain text — matches project
# convention, OK/ERR replies):
#
#   BEFORE\n
#       Capture the current color+depth (from the background poll thread's
#       latest frame, not a fresh blocking read — see PollLoop below) and
#       store as the "before" frame for the next AFTER call. Also publishes
#       the raw color JPEG via HTTP /snap/depth_before (2026-07-21 — debug
#       aid, see AFTER below for why).
#       Reply: OK before_captured\n
#
#   AFTER\n
#       Capture current color+depth (raw color JPEG published via HTTP
#       /snap/depth_after, same 2026-07-21 addition), run motion mask against
#       the stored BEFORE frame, fit background plane, detect sill
#       candidates, save an annotated JPEG (served via HTTP /snap/depth),
#       reply with a summary.
#       No BEFORE captured yet -> ERR no_before_frame\n
#       Otherwise -> OK candidates=<N> max_height_cm=<X.X> max_protrusion_cm=<Y.Y>\n
#       (candidates=0 -> max_height_cm=0.0 max_protrusion_cm=0.0)
#       Threshold decision (is this "big enough to pause for") is left to
#       WASH_ROBOT.cpp, not decided here — this service reports geometry only,
#       same driver/app split as user_lib vs application layer elsewhere in
#       this project.
#
#       [2026-07-21] /snap/depth alone (the annotated RESULT frame) wasn't
#       enough to debug a "nothing detected" step: WASH_ROBOT.cpp's modal
#       only ever fetched it when big_obstacle=yes, so a candidates=0 step
#       left the operator with zero visibility into what the camera actually
#       saw. /snap/depth_before and /snap/depth_after now always publish the
#       raw (un-annotated) color frame from each capture, and the GUI modal
#       fetches all three every step regardless of outcome.
#
#   PING\n
#       Reply: OK pong\n  (liveness check)
#
# 規範權威：.claude/camera_obstacle_plan.md (v1 RGB precedent),
#           project_v2_depth_camera_sill_detection memory (design rationale)
# ============================================================================

import os
import socketserver
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from obstacle_detector import compute_optical_flow, build_motion_mask  # noqa: E402
from depth_reflection_bench import (  # noqa: E402
    DEPTH_MASK_EXTRA_CLOSE_PX, MAX_DETECT_DISTANCE_M, MIN_PLANE_FIT_POINTS,
    deproject_grid, fit_plane_two_pass, detect_frame_candidates,
    draw_candidates, colorize_depth,
)

try:
    import pyrealsense2 as rs
except ImportError:
    print("pyrealsense2 not installed. See project memory / build notes for "
          "building librealsense from source on aarch64.", file=sys.stderr)
    sys.exit(1)

TCP_PORT   = 9530
TCP_BIND   = "127.0.0.1"   # WASH_ROBOT.cpp runs on the same Pi — no need to expose off-box
HTTP_PORT  = 5008   # 2026-07-20: NOT 5006 — scripts/cams.sh reserves 5004-5007 for cam1-4
HTTP_BIND  = "0.0.0.0"     # web_backend reverse-proxies this from off-box
CAM_ID     = "depth"       # -> /snap/depth, matches CAMERAS.depth in server.js


# ---------- shared latest-JPEG buffer for HTTP /snap/depth (reused pattern from frame_capture.py) ----------
class FrameBuffer:
    def __init__(self):
        self._cv = threading.Condition()
        self._bytes = None

    def publish(self, jpg_bytes):
        with self._cv:
            self._bytes = jpg_bytes
            self._cv.notify_all()

    def snapshot(self):
        with self._cv:
            return self._bytes


def _make_http_handler(cam_id, snap_buffers, state):
    """snap_buffers: dict of URL-suffix -> FrameBuffer, e.g.
    {"": result_buf, "_before": before_buf, "_after": after_buf} serves
    /snap/{cam_id}, /snap/{cam_id}_before, /snap/{cam_id}_after respectively.
    live_path is handled separately since it reads state.latest_color()
    directly instead of a published buffer (see _serve_live)."""
    live_path = f"/snap/{cam_id}_live"
    live_depth_path = f"/snap/{cam_id}_live_depth"
    snap_paths = {f"/snap/{cam_id}{suffix}": buf for suffix, buf in snap_buffers.items()}

    class _Handler(BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            pass

        def do_GET(self):
            if self.path in ("/health", "/health/"):
                self._send_text(200, "ok\n")
                return
            if self.path == live_depth_path or self.path.startswith(live_depth_path + "?"):
                self._serve_live_depth()
                return
            if self.path == live_path or self.path.startswith(live_path + "?"):
                self._serve_live()
                return
            path_only = self.path.split("?", 1)[0]
            if path_only in snap_paths:
                self._serve_snap(snap_paths[path_only])
                return
            self._send_text(404, "not_found\n")

        def _send_text(self, code, body):
            try:
                self.send_response(code)
                self.send_header("Content-Type", "text/plain; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body.encode("utf-8"))
            except Exception:
                pass

        def _write_jpg(self, jpg):
            try:
                self.send_response(200)
                self.send_header("Content-Type", "image/jpeg")
                self.send_header("Content-Length", str(len(jpg)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(jpg)
            except (ConnectionResetError, BrokenPipeError):
                pass

        def _serve_snap(self, buf):
            # Whatever was last published into this specific buffer (result /
            # before / after) — only updates once per BEFORE or AFTER call
            # (see handle_before/handle_after's buf.publish). Used by the
            # run_depth_avoid modal, not a live view.
            jpg = buf.snapshot()
            if jpg is None:
                self._send_text(503, "frame_not_ready\n")
                return
            self._write_jpg(jpg)

        def _serve_live(self):
            # Raw current color frame, no motion-mask/plane-fit analysis —
            # for the standalone camera panel's manual snapshot button, which
            # wants "what does the camera see right now" independent of
            # whether a depth_avoid BEFORE/AFTER cycle has ever run.
            color = state.latest_color()
            if color is None:
                self._send_text(503, "frame_not_ready\n")
                return
            ok, enc = cv2.imencode(".jpg", color, [cv2.IMWRITE_JPEG_QUALITY, 90])
            if not ok:
                self._send_text(500, "encode_failed\n")
                return
            self._write_jpg(enc.tobytes())

        def _serve_live_depth(self):
            # Colorized depth map of the CURRENT frame (heatmap via
            # colorize_depth, same rendering AFTER uses) — pairs with
            # _serve_live's color image so the manual "拍照" button can show
            # both at once. No motion-mask/plane-fit, just raw depth right now.
            _, depth_m = state.latest()
            if depth_m is None:
                self._send_text(503, "frame_not_ready\n")
                return
            depth_color = colorize_depth(depth_m)
            ok, enc = cv2.imencode(".jpg", depth_color, [cv2.IMWRITE_JPEG_QUALITY, 90])
            if not ok:
                self._send_text(500, "encode_failed\n")
                return
            self._write_jpg(enc.tobytes())

    return _Handler


# ---------- RealSense pipeline: background poll thread keeps "latest frame" fresh ----------
class DepthCamState:
    """Owns the RealSense pipeline + intrinsics + latest color/depth arrays.
    A background thread continuously polls frames so BEFORE/AFTER command
    handlers just grab whatever's latest (< ~33ms old at 30fps) instead of
    each blocking on its own wait_for_frames() — keeps command-to-capture
    latency tight and consistent, which matters since BEFORE/AFTER are timed
    against the robot's actual step motion by the C++ caller."""

    def __init__(self):
        self.pipeline = rs.pipeline()
        config = rs.config()
        config.enable_stream(rs.stream.color, 640, 480, rs.format.bgr8, 30)
        config.enable_stream(rs.stream.depth, 640, 480, rs.format.z16, 30)
        profile = self.pipeline.start(config)

        self.align = rs.align(rs.stream.color)
        intr = profile.get_stream(rs.stream.color).as_video_stream_profile().get_intrinsics()
        self.fx, self.fy, self.ppx, self.ppy = intr.fx, intr.fy, intr.ppx, intr.ppy
        print(f"[depth_cam_service] color intrinsics fx={self.fx:.1f} fy={self.fy:.1f} "
              f"ppx={self.ppx:.1f} ppy={self.ppy:.1f}", flush=True)

        py_grid, px_grid = np.mgrid[0:480, 0:640]
        self.px_grid = px_grid.astype(np.float32)
        self.py_grid = py_grid.astype(np.float32)

        self._lock = threading.Lock()
        self._latest_gray = None
        self._latest_depth_m = None
        self._latest_color = None

        self.before_gray = None
        self.before_depth_m = None

        self._stop = False
        self._thread = threading.Thread(target=self._poll_loop, daemon=True, name="depth_poll")
        self._thread.start()

    def _poll_loop(self):
        while not self._stop:
            frames = self.pipeline.wait_for_frames()
            frames = self.align.process(frames)
            color_frame = frames.get_color_frame()
            depth_frame = frames.get_depth_frame()
            if not color_frame or not depth_frame:
                continue
            color = np.asanyarray(color_frame.get_data())
            depth_mm = np.asanyarray(depth_frame.get_data())
            gray = cv2.cvtColor(color, cv2.COLOR_BGR2GRAY)
            depth_m = depth_mm.astype(np.float32) / 1000.0
            with self._lock:
                self._latest_gray = gray
                self._latest_depth_m = depth_m
                self._latest_color = color

    def latest(self):
        with self._lock:
            return self._latest_gray, self._latest_depth_m

    def latest_color(self):
        with self._lock:
            return self._latest_color

    def stop(self):
        self._stop = True
        self._thread.join(timeout=2.0)
        self.pipeline.stop()


def handle_before(state: DepthCamState, before_buf: FrameBuffer) -> str:
    gray, depth_m = state.latest()
    if gray is None:
        return "ERR no_frame_yet\n"
    state.before_gray = gray.copy()
    state.before_depth_m = depth_m.copy()

    # [2026-07-21] Publish the raw (un-annotated) color frame from THIS
    # capture — separate from /snap/depth_live's "right now" since this one
    # stays frozen at exactly what BEFORE saw, for comparison against the
    # matching AFTER once that lands. state.latest() and latest_color() are
    # two separate lock acquisitions (could be off by one ~33ms poll tick) —
    # fine here, this photo is for human debugging only, not detection math.
    color = state.latest_color()
    if color is not None:
        ok, enc = cv2.imencode(".jpg", color, [cv2.IMWRITE_JPEG_QUALITY, 90])
        if ok:
            before_buf.publish(enc.tobytes())

    return "OK before_captured\n"


def handle_after(state: DepthCamState, result_buf: FrameBuffer, after_buf: FrameBuffer) -> str:
    if state.before_gray is None:
        return "ERR no_before_frame\n"
    after_gray, after_depth_m = state.latest()
    if after_gray is None:
        return "ERR no_frame_yet\n"

    # Raw AFTER color, same rationale as handle_before's before_buf.publish.
    after_color = state.latest_color()
    if after_color is not None:
        ok, enc = cv2.imencode(".jpg", after_color, [cv2.IMWRITE_JPEG_QUALITY, 90])
        if ok:
            after_buf.publish(enc.tobytes())

    # Same convention as depth_reflection_bench.py's 'a' key: flow(after, before)
    # so the mask lands aligned to AFTER's pixel grid.
    flow = compute_optical_flow(after_gray, state.before_gray)
    mask, _mag, _threshold = build_motion_mask(flow)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE,
                             cv2.getStructuringElement(cv2.MORPH_RECT,
                                 (DEPTH_MASK_EXTRA_CLOSE_PX, DEPTH_MASK_EXTRA_CLOSE_PX)))

    masked_depth_m = after_depth_m.copy()
    masked_depth_m[mask == 0] = 0.0

    # [2026-07-21] Mirrors depth_reflection_bench.py's interactive 'a'-key
    # diagnostics (that file's main loop, lines ~570-584) — this service had
    # silently returned candidates=0 with ZERO console output whenever the
    # motion mask left too few points to fit a plane, which looked
    # indistinguishable from "ran fine, just nothing there" during a real
    # run_depth_avoid session. Always print the raw/masked valid-depth % so
    # a sparse mask (low-texture surface, too-small step, no real parallax)
    # is visible; only print the point-count line on the actual plane-fit
    # failure path below.
    valid_before_pct = float((after_depth_m > 0).mean() * 100)
    valid_after_pct = float((masked_depth_m > 0).mean() * 100)
    print(f"[depth_cam_service] AFTER captured — valid depth: "
          f"raw={valid_before_pct:.1f}% -> masked={valid_after_pct:.1f}%", flush=True)

    X, Y, Z = deproject_grid(masked_depth_m, state.px_grid, state.py_grid,
                              state.fx, state.fy, state.ppx, state.ppy)
    plane_valid = (masked_depth_m > 0) & (masked_depth_m <= MAX_DETECT_DISTANCE_M)
    # [2026-07-21] Two-pass fit (see fit_plane_two_pass docstring) — a single
    # naive least-squares fit folds a large-enough real obstacle (e.g. a
    # sill/plank spanning a big share of the frame's in-range pixels) into
    # the "background", which then measures as ~0cm protrusion against a
    # plane that's partly itself. Second pass excludes pass-1's blobs and
    # refits on what's left.
    plane, protrusion_map, candidates, rejected, fit_stats = fit_plane_two_pass(
        masked_depth_m, X, Y, Z, plane_valid, state.fx, state.fy, state.ppx)
    if plane is None:
        # [2026-07-22] No plane at all is no longer a hard stop — distance
        # is the primary signal the robot actually needs (to plan its next
        # step), and it doesn't require a background plane. Only protrusion/
        # height-based severity needs one. Falls back to distance+shape-only
        # detection (detect_frame_candidates with protrusion_map=None) so a
        # real, in-range, correctly-shaped object still gets reported even
        # when there's no separate background material in view to compare
        # it against (the mirror-reflective bench case this was found in).
        print(f"[depth_cam_service] only {int(plane_valid.sum())} valid points "
              f"(<{MIN_PLANE_FIT_POINTS} needed) — no background plane this "
              f"capture, falling back to distance+shape only (no protrusion)", flush=True)
        candidates, rejected = detect_frame_candidates(masked_depth_m, None, state.fx, state.fy, state.ppx)
    else:
        # [2026-07-21] Diagnose whether the two-pass refit (21i) is actually
        # doing anything on this scene, and whether the excluded-obstacle
        # "background" is a real coherent surface or still noise (see
        # fit_plane_two_pass docstring — this is the number that tells us if
        # a heavily mirror-reflective background is leaking un-filtered
        # noise into plane_valid upstream of this function, which no amount
        # of refitting here can fix).
        if fit_stats['pass2_plane'] is not None:
            print(f"[depth_cam_service] two-pass refit: excluded {fit_stats['excluded_points']}px, "
                  f"refit on {fit_stats['refit_points']}px, "
                  f"refit residual mean={fit_stats['refit_residual_mean_cm']:+.1f}cm "
                  f"std={fit_stats['refit_residual_std_cm']:.1f}cm", flush=True)
        else:
            print(f"[depth_cam_service] two-pass refit: pass 2 skipped "
                  f"(only {fit_stats['refit_points']}px left after excluding "
                  f"{fit_stats['excluded_points']}px — using pass-1 plane)", flush=True)

    depth_color = colorize_depth(masked_depth_m)
    depth_color = draw_candidates(depth_color, candidates)
    ok, enc = cv2.imencode(".jpg", depth_color, [cv2.IMWRITE_JPEG_QUALITY, 90])
    if ok:
        result_buf.publish(enc.tobytes())

    print(f"[depth_cam_service] AFTER: {len(candidates)} candidate(s), "
          f"{len(rejected)} rejected", flush=True)
    # [2026-07-22] The accepted candidate's own numbers were never printed
    # here — only the count. Made it hard to spot, from the log alone, when
    # the reported min_distance_cm/height didn't actually match the
    # identified candidate (see the fix above this block for the real bug
    # that surfaced this gap: a rejected speck's distance was briefly
    # reported instead of the real candidate's own).
    for c in candidates:
        prot_str = f"{c['protrusion_m']*100:+.1f}cm" if c['protrusion_m'] is not None else "n/a"
        print(f"[depth_cam_service]   candidate: near={c['near_m']*100:.1f}cm "
              f"center={c['center_distance_m']*100:.1f}cm "
              f"protrusion={prot_str} height={c['height_m']*100:.1f}cm "
              f"bbox_px=({c['x']},{c['y']},{c['w_px']}x{c['h_px']})", flush=True)
    # [2026-07-21] Same gap as the plane-fit log above: rejected blobs carry
    # a human-readable `reasons` list (width/aspect/protrusion/std failures,
    # see detect_frame_candidates) but this service only ever printed the
    # bare count — mirrors depth_reflection_bench.py's interactive printout
    # (lines ~614-617) so "1 rejected" is diagnosable instead of a dead end.
    for r in rejected[:10]:   # biggest-area first (detect_frame_candidates sorts them), cap the spam
        print(f"[depth_cam_service]   rejected: near={r['near_m']*100:.1f}cm "
              f"bbox_px=({r['x']},{r['y']},{r['w_px']}x{r['h_px']}) "
              f"area={r['area']}px -- {', '.join(r['reasons'])}", flush=True)

    # [2026-07-22] Distance must always be reported when there's ANYTHING
    # real in view, but a confirmed sill-shaped candidate's OWN distance
    # always wins over anything in `rejected` — a rejected blob is, by
    # definition, something we decided ISN'T the sill (wrong shape, or
    # 2026-07-22's "not the widest" demotion), often a small stray fragment
    # nowhere near as significant as the actual identified obstacle. Real-
    # hardware bug this fixes: a tiny 544px rejected speck at 55.2cm (bottom
    # frame edge, likely noise) was closer than the real candidate at
    # 56.2cm, so the OLD "nearest of everything" rule reported 55.2cm as
    # the obstacle distance — a number that didn't correspond to the thing
    # actually being tracked, and downstream remaining_travel_cm math with
    # it. Falling back to `rejected` is now ONLY for when there's no
    # candidate at all (nothing sill-shaped this step) — still better than
    # a flat 0.0 in that case, but never overrides a real identified sill.
    if candidates:
        # [2026-07-22] center_distance_m (straight-ahead of the camera's
        # optical axis), NOT near_m (closest pixel anywhere in a bbox that
        # can span most of the frame width) — see detect_frame_candidates'
        # docstring. min_distance_cm feeds WASH_ROBOT.cpp's along-travel
        # geometry, which assumes a straight-ahead measurement; near_m can
        # be off to one side and made that geometry overcount forward
        # distance on real hardware (reported ~30cm, actual ~20cm).
        min_distance_cm = candidates[0]['center_distance_m'] * 100.0
    else:
        min_distance_cm = (min(r['near_m'] for r in rejected) * 100.0) if rejected else 0.0

    if not candidates:
        return f"OK candidates=0 max_height_cm=0.0 max_protrusion_cm=0.0 min_distance_cm={min_distance_cm:.1f}\n"

    max_height_cm = max(c['height_m'] for c in candidates) * 100.0
    # protrusion_m is None when no background plane was available (see
    # detect_frame_candidates docstring) — height_m doesn't depend on the
    # plane so it's always present; protrusion falls back to 0.0 (reads as
    # "unknown", not "confirmed flush") when nothing computed it.
    known_protrusions = [abs(c['protrusion_m']) for c in candidates if c['protrusion_m'] is not None]
    max_protrusion_cm = (max(known_protrusions) * 100.0) if known_protrusions else 0.0
    return (f"OK candidates={len(candidates)} "
            f"max_height_cm={max_height_cm:.1f} "
            f"max_protrusion_cm={max_protrusion_cm:.1f} "
            f"min_distance_cm={min_distance_cm:.1f}\n")


class _TCPHandler(socketserver.StreamRequestHandler):
    def handle(self):
        state = self.server.depth_state
        while True:
            raw = self.rfile.readline()
            if not raw:
                break
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            if line == "BEFORE":
                reply = handle_before(state, self.server.before_buf)
            elif line == "AFTER":
                reply = handle_after(state, self.server.result_buf, self.server.after_buf)
            elif line == "PING":
                reply = "OK pong\n"
            else:
                reply = f"ERR unknown_cmd {line}\n"
            try:
                self.wfile.write(reply.encode("utf-8"))
            except (ConnectionResetError, BrokenPipeError):
                break


class _TCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    state = DepthCamState()
    result_buf = FrameBuffer()
    before_buf = FrameBuffer()
    after_buf  = FrameBuffer()

    http_handler = _make_http_handler(
        CAM_ID, {"": result_buf, "_before": before_buf, "_after": after_buf}, state)
    httpd = ThreadingHTTPServer((HTTP_BIND, HTTP_PORT), http_handler)
    threading.Thread(target=httpd.serve_forever, daemon=True, name="http_server").start()
    print(f"[depth_cam_service] http server :{HTTP_PORT} "
          f"(/snap/{CAM_ID} result, /snap/{CAM_ID}_before, /snap/{CAM_ID}_after, "
          f"/snap/{CAM_ID}_live raw, /snap/{CAM_ID}_live_depth raw depth)", flush=True)

    tcpd = _TCPServer((TCP_BIND, TCP_PORT), _TCPHandler)
    tcpd.depth_state = state
    tcpd.result_buf = result_buf
    tcpd.before_buf = before_buf
    tcpd.after_buf  = after_buf
    print(f"[depth_cam_service] tcp server {TCP_BIND}:{TCP_PORT} "
          f"(BEFORE / AFTER / PING)", flush=True)

    try:
        tcpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        tcpd.shutdown()
        state.stop()


if __name__ == "__main__":
    main()
