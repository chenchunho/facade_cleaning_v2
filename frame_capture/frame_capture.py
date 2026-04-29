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
import time

import cv2

# ---------- defaults（可被 CLI 覆蓋）----------
DEFAULT_RTSP_URL = (
    "rtsp://192.168.1.10:554/"
    "user=admin&password=&channel=1&stream=1.sdp?"   # stream=1 = 子碼流
)
DEFAULT_OUT_PATH   = "/tmp/cam_latest.jpg"
DEFAULT_FPS_LIMIT  = 10          # 每秒寫檔次數上限
DEFAULT_JPEG_Q     = 85          # JPEG quality (0~100)
DEFAULT_RECONNECT_DELAY = 2.0    # 斷線重連間隔 (秒)
DEFAULT_LOG_INTERVAL = 10.0      # stderr log 間隔 (秒)


# ---------- signal handler ----------
_shutdown = False

def _on_signal(signum, frame):
    global _shutdown
    _shutdown = True
    sys.stderr.write(f"[frame_capture] signal {signum}, shutting down\n")


# ---------- main loop ----------
def run(url, out_path, fps_limit, jpeg_q, reconnect_delay, log_interval):
    tmp_path = out_path + ".tmp"
    min_interval = 1.0 / fps_limit if fps_limit > 0 else 0.0

    last_write = 0.0
    write_count = 0
    read_fail_count = 0
    last_log_t = time.time()

    sys.stderr.write(f"[frame_capture] target {url}\n")
    sys.stderr.write(f"[frame_capture] output {out_path} @ max {fps_limit} fps (jpeg q={jpeg_q})\n")

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
                    cv2.imwrite(tmp_path, frame, [cv2.IMWRITE_JPEG_QUALITY, jpeg_q])
                    os.replace(tmp_path, out_path)   # atomic on Linux
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
    args = ap.parse_args()

    signal.signal(signal.SIGINT,  _on_signal)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, _on_signal)

    run(args.url, args.out, args.fps, args.quality,
        args.reconnect_delay, args.log_interval)


if __name__ == "__main__":
    main()
