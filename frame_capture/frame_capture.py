#!/usr/bin/env python3
# ============================================================================
# frame_capture.py — 持續解碼 RTSP，atomic 寫入最新 JPEG 供 detect_server 讀
#
# 架構（跑在 washrobot Pi .100 上）：
#
#   camera (.1.10:554) ──RTSP──▶ frame_capture.py ──▶ /tmp/cam_latest.jpg
#                                                           │
#                                                           └──▶ detect_server.py
#                                                                 (讀檔 + 推論)
#
# 特性：
#   - 持續解碼子碼流（主碼流對 YOLO 640×640 是浪費）
#   - `os.replace()` Linux atomic，避免 detect_server 讀到半寫入檔
#   - 斷線自動重連
#   - FPS 限流（預設 10fps）避免過度寫檔
#   - SIGINT / SIGTERM 清理退出
#   - debug 每 10 秒印一次 fps 統計
#
# 規範權威：.claude/camera_obstacle_plan.md
# ============================================================================

import argparse
import os
import signal
import sys
import threading
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import cv2

# ---------- defaults（可被 CLI 覆蓋）----------
DEFAULT_RTSP_URL = (
    "rtsp://192.168.1.110:554/"
    "user=admin&password=&channel=1&stream=1.sdp?"   # stream=1 = 子碼流
)
# 2026-05-21: 相機實際 IP 為 .110 / .111（cam1=.110, cam2=.111），
# 之前 README/code 預設的 .10 是測試時暫存值，已 retire。
DEFAULT_OUT_PATH   = "/tmp/cam_latest.jpg"
DEFAULT_FPS_LIMIT  = 10          # 每秒寫檔次數上限
DEFAULT_JPEG_Q     = 85          # JPEG quality (0~100)
DEFAULT_RECONNECT_DELAY = 2.0    # 斷線重連間隔 (秒)
DEFAULT_LOG_INTERVAL = 10.0      # stderr log 間隔 (秒)
DEFAULT_CAM_ID       = "cam1"    # overlay 文字「<cam_id> | timestamp」用
DEFAULT_HTTP_PORT    = 5004      # 內建 HTTP server port (0=disable)
DEFAULT_HTTP_BIND    = "0.0.0.0" # 監聽介面


def _draw_overlay(frame, cam_id):
    """Burn camera id + timestamp into the frame (bottom-left corner).

    OpenCV putText is cheap (~1-2ms on Pi 4) so doing it here keeps backend
    purely file-reading. Black outline (2 px stroke) then green fill for
    legibility against any background.
    """
    text = f"{cam_id} | {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}"
    org  = (10, frame.shape[0] - 12)
    font = cv2.FONT_HERSHEY_SIMPLEX
    cv2.putText(frame, text, org, font, 0.6, (0, 0, 0),     3, cv2.LINE_AA)
    cv2.putText(frame, text, org, font, 0.6, (0, 255, 0),   1, cv2.LINE_AA)


# ---------- shared latest JPEG (producer: decode loop / consumer: HTTP server) ----------
class FrameBuffer:
    """Thread-safe holder for the most recent encoded JPEG bytes.

    Producer (decode loop) calls publish() each time a new frame is written
    to disk. Consumers (HTTP /mjpeg + /snap handlers) call snapshot() to get
    a snapshot of the latest bytes, or wait_for_next() to block until a new
    one arrives. Condition variable is used so consumers don't busy-poll.
    """
    def __init__(self):
        self._cv    = threading.Condition()
        self._bytes = None
        self._seq   = 0

    def publish(self, jpg_bytes):
        with self._cv:
            self._bytes = jpg_bytes
            self._seq  += 1
            self._cv.notify_all()

    def snapshot(self):
        with self._cv:
            return self._bytes, self._seq

    def wait_for_next(self, last_seq, timeout=1.0):
        """Block until a new frame arrives (seq > last_seq) or timeout.

        Returns (bytes, seq) on success, (None, last_seq) on timeout.
        """
        with self._cv:
            if self._seq <= last_seq:
                self._cv.wait(timeout=timeout)
            if self._seq <= last_seq:
                return None, last_seq
            return self._bytes, self._seq


# ---------- HTTP server (serves /mjpeg/<cam_id> + /snap/<cam_id>) ----------
def _make_http_handler(cam_id, buffer):
    """Factory closure: bind cam_id + buffer into the handler class."""

    class _Handler(BaseHTTPRequestHandler):
        # Silence default access log — too noisy at 10 fps × N clients
        def log_message(self, format, *args):
            pass

        def _is_snap(self):
            return self.path == f"/snap/{cam_id}" or self.path.startswith(f"/snap/{cam_id}?")

        def _is_mjpeg(self):
            return self.path == f"/mjpeg/{cam_id}" or self.path.startswith(f"/mjpeg/{cam_id}?")

        def do_GET(self):
            if self.path in ("/health", "/health/"):
                self._send_text(200, "ok\n")
                return
            if self._is_snap():
                self._serve_snap()
                return
            if self._is_mjpeg():
                self._serve_mjpeg()
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

        def _serve_snap(self):
            jpg, _ = buffer.snapshot()
            if jpg is None:
                self._send_text(503, "frame_not_ready\n")
                return
            try:
                self.send_response(200)
                self.send_header("Content-Type", "image/jpeg")
                self.send_header("Content-Length", str(len(jpg)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(jpg)
            except (ConnectionResetError, BrokenPipeError):
                pass

        def _serve_mjpeg(self):
            try:
                self.send_response(200)
                self.send_header("Content-Type",
                                 "multipart/x-mixed-replace; boundary=frame")
                self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
                self.send_header("Pragma", "no-cache")
                self.end_headers()
                last_seq = 0
                while True:
                    jpg, last_seq = buffer.wait_for_next(last_seq, timeout=2.0)
                    if jpg is None:
                        continue   # timeout, loop to check shutdown via client disconnect
                    self.wfile.write(b"--frame\r\n")
                    self.wfile.write(b"Content-Type: image/jpeg\r\n")
                    self.wfile.write(f"Content-Length: {len(jpg)}\r\n\r\n".encode("ascii"))
                    self.wfile.write(jpg)
                    self.wfile.write(b"\r\n")
                    self.wfile.flush()
            except (ConnectionResetError, BrokenPipeError, ConnectionAbortedError):
                pass   # client disconnected, normal

    return _Handler


def _start_http_server(bind_host, port, cam_id, buffer):
    """Start HTTP server in daemon thread. Returns (server, thread)."""
    handler = _make_http_handler(cam_id, buffer)
    # ThreadingHTTPServer so /mjpeg long-lived connection doesn't block /snap
    httpd = ThreadingHTTPServer((bind_host, port), handler)
    t = threading.Thread(target=httpd.serve_forever, daemon=True,
                         name=f"http_server:{port}")
    t.start()
    sys.stderr.write(
        f"[frame_capture] http server :{port} (mjpeg + snap for {cam_id})\n"
    )
    return httpd, t


# ---------- signal handler ----------
_shutdown = False

def _on_signal(signum, frame):
    global _shutdown
    _shutdown = True
    sys.stderr.write(f"[frame_capture] signal {signum}, shutting down\n")


# ---------- main loop ----------
def run(url, out_path, fps_limit, jpeg_q, reconnect_delay, log_interval,
        cam_id, http_port, http_bind):
    tmp_path = out_path + ".tmp"
    min_interval = 1.0 / fps_limit if fps_limit > 0 else 0.0

    last_write = 0.0
    write_count = 0
    read_fail_count = 0
    last_log_t = time.time()

    sys.stderr.write(f"[frame_capture] target {url}\n")
    sys.stderr.write(f"[frame_capture] output {out_path} @ max {fps_limit} fps (jpeg q={jpeg_q})\n")

    # Shared latest-JPEG buffer + HTTP server (consumed by web_backend reverse proxy)
    buffer = FrameBuffer()
    if http_port > 0:
        _start_http_server(http_bind, http_port, cam_id, buffer)
    else:
        sys.stderr.write("[frame_capture] http server disabled (--http-port 0)\n")

    while not _shutdown:
        # RTSP over TCP is more reliable than default UDP on lossy networks.
        os.environ["OPENCV_FFMPEG_CAPTURE_OPTIONS"] = "rtsp_transport;tcp"
        cap = cv2.VideoCapture(url, cv2.CAP_FFMPEG)

        if not cap.isOpened():
            sys.stderr.write(
                f"[frame_capture] open fail, retry in {reconnect_delay}s\n"
            )
            cap.release()
            _sleep_interruptible(reconnect_delay)
            continue

        sys.stderr.write(f"[frame_capture] connected\n")

        # Drain buffered frames (some RTSP clients hold stale frames on (re)connect)
        cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

        while not _shutdown:
            ok, frame = cap.read()
            if not ok:
                read_fail_count += 1
                sys.stderr.write(
                    f"[frame_capture] read fail (streak={read_fail_count}), reconnecting\n"
                )
                break

            read_fail_count = 0
            now = time.time()

            if now - last_write >= min_interval:
                try:
                    _draw_overlay(frame, cam_id)
                    # Encode in-memory first so HTTP server gets exactly the same
                    # bytes that go to disk, atomically. cv2.imencode returns
                    # (ok, ndarray); we slice .tobytes() for HTTP transport.
                    ok2, enc = cv2.imencode(".jpg", frame,
                                            [cv2.IMWRITE_JPEG_QUALITY, jpeg_q])
                    if ok2:
                        jpg_bytes = enc.tobytes()
                        # Write to disk for detect_server.py (file consumer)
                        with open(tmp_path, "wb") as f:
                            f.write(jpg_bytes)
                        os.replace(tmp_path, out_path)   # atomic on Linux
                        # Publish to HTTP server (memory consumer, web_backend proxy)
                        buffer.publish(jpg_bytes)
                        last_write = now
                        write_count += 1
                except Exception as e:
                    sys.stderr.write(f"[frame_capture] write fail: {e}\n")

            # periodic health log
            if now - last_log_t >= log_interval:
                elapsed = now - last_log_t
                fps = write_count / elapsed if elapsed > 0 else 0.0
                age_ms = _file_age_ms(out_path)
                sys.stderr.write(
                    f"[frame_capture] writes={write_count} "
                    f"({fps:.1f} fps), latest_age={age_ms}ms\n"
                )
                write_count = 0
                last_log_t = now

        cap.release()
        if not _shutdown:
            _sleep_interruptible(reconnect_delay)

    sys.stderr.write(f"[frame_capture] stopped\n")


def _sleep_interruptible(seconds):
    """Sleep that wakes up promptly on _shutdown."""
    end = time.time() + seconds
    while not _shutdown and time.time() < end:
        time.sleep(0.1)


def _file_age_ms(path):
    try:
        return int((time.time() - os.path.getmtime(path)) * 1000)
    except FileNotFoundError:
        return -1


# ---------- entry ----------
def main():
    ap = argparse.ArgumentParser(
        description="Persistent RTSP decoder -> atomic-write latest JPEG"
    )
    ap.add_argument("--url",     default=DEFAULT_RTSP_URL,
                    help=f"RTSP URL (default: sub stream)")
    ap.add_argument("--out",     default=DEFAULT_OUT_PATH,
                    help=f"output JPEG path (default: {DEFAULT_OUT_PATH})")
    ap.add_argument("--fps",     type=int, default=DEFAULT_FPS_LIMIT,
                    help=f"max write fps (default: {DEFAULT_FPS_LIMIT})")
    ap.add_argument("--quality", type=int, default=DEFAULT_JPEG_Q,
                    help=f"JPEG quality 0~100 (default: {DEFAULT_JPEG_Q})")
    ap.add_argument("--reconnect-delay", type=float, default=DEFAULT_RECONNECT_DELAY)
    ap.add_argument("--log-interval",    type=float, default=DEFAULT_LOG_INTERVAL)
    ap.add_argument("--cam-id",          default=DEFAULT_CAM_ID,
                    help=f"label shown in overlay (default: {DEFAULT_CAM_ID})")
    ap.add_argument("--http-port",       type=int, default=DEFAULT_HTTP_PORT,
                    help=f"HTTP server port for /mjpeg + /snap, 0 to disable (default: {DEFAULT_HTTP_PORT})")
    ap.add_argument("--http-bind",       default=DEFAULT_HTTP_BIND,
                    help=f"HTTP bind address (default: {DEFAULT_HTTP_BIND})")
    args = ap.parse_args()

    signal.signal(signal.SIGINT,  _on_signal)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, _on_signal)

    run(args.url, args.out, args.fps, args.quality,
        args.reconnect_delay, args.log_interval, args.cam_id,
        args.http_port, args.http_bind)


if __name__ == "__main__":
    main()
