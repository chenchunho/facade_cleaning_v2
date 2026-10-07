#!/usr/bin/env python3
# cam_relay.py — camera relay for the console v3 Dashboard (2026-10-07 per user; was cam_mjpeg.py the same day).
#
# Runs on the BODY Pi (systemd user service fcv-cam, :8091). Why the body and not the crane:
#   the two XiongMai cameras (.112 / .113) sit on the body's wired LAN (192.168.1.x behind the body
#   switch); the crane's eth0 segment cannot see them (10-07 ping sweep + XM broadcast search).
#   The browser loads the stream straight from the body's WiFi address, so video crosses WiFi once.
#
# Two outputs per camera, each its own ffmpeg (= its own RTSP session on the camera):
#   /cam/<id>.mp4    H.264 passthrough, fragmented MP4, one fragment per frame -> <video> + MediaSource.
#                    No decode / no re-encode: ~1 % CPU, 25 fps at the camera's ~1 Mbps. DEFAULT in the GUI.
#                    Kept WARM (FCV_CAM_WARM=1): the pipe runs even with no viewer so opening is instant —
#                    the cost is camera->body on the wired LAN only; nothing goes on WiFi without a viewer.
#                    A new viewer gets the init segment + every fragment since the last keyframe (GOP cache,
#                    <= 1 s with the camera GOP at 1 s) and then live fragments.
#   /cam/<id>.mjpg   MJPEG multipart, re-encoded at FCV_CAM_FPS / FCV_CAM_Q. Fallback for browsers without
#                    MediaSource (older iPhone Safari). On demand only (decode + encode, ~5 % CPU per camera).
#   /cam/<id>.jpg    one JPEG from the MJPEG pipe (starts it, waits up to ~6 s)
#   /cam/status      JSON per camera (+ per-mode detail under "modes"); CORS * (the GUI is served from the crane)
#
# 10-07 measurements (MJPEG, 5 fps q9): ~5.5 % of one core, 1.0~1.6 Mbps per camera, which looked choppy
# ("有點慢" per user) — the cameras themselves deliver 25 fps; the 5 fps was ours. Hence the MP4 path.
#
# Config (env):
#   FCV_CAM_PORT=8091  FCV_CAMS="1=192.168.1.112,2=192.168.1.113"  FCV_CAM_STREAM=1 (0 main / 1 sub)
#   FCV_CAM_WARM=1  FCV_CAM_FPS=5  FCV_CAM_Q=9  FCV_CAM_IDLE_S=10  FCV_CAM_MAX_CLIENTS=4
#
# XM RTSP URL quirks (.claude/summaries/IPCAM_XIONGMAI_SUMMARY.md): admin has an EMPTY password and the
# trailing '?' is mandatory. The URL never leaves this process (status reports the host only).

import collections, json, os, re, select, signal, socket, struct, subprocess, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT        = int(os.environ.get("FCV_CAM_PORT", "8091"))
STREAM      = int(os.environ.get("FCV_CAM_STREAM", "1"))
WARM        = os.environ.get("FCV_CAM_WARM", "1").strip() in ("1", "on", "yes")
FPS         = float(os.environ.get("FCV_CAM_FPS", "5"))
QUALITY     = int(os.environ.get("FCV_CAM_Q", "9"))
IDLE_S      = float(os.environ.get("FCV_CAM_IDLE_S", "10"))
MAX_CLIENTS = int(os.environ.get("FCV_CAM_MAX_CLIENTS", "4"))
STALL_S     = 8.0      # running but no output for this long -> kill ffmpeg and restart
START_S     = 12.0     # first output must arrive within this long after spawn (RTSP handshake + first GOP)
SNAP_HOLD_S = 15.0     # a .jpg request keeps the MJPEG pipe running this long
MP4_RING    = 75       # fragments kept for viewers (~3 s at 25 fps); a viewer further behind resyncs at a keyframe
SNDBUF      = 64 * 1024  # small kernel send buffer: a slow WiFi viewer falls behind visibly instead of piling up seconds


def log(msg):
    print("[%s] %s" % (time.strftime("%H:%M:%S"), msg), flush=True)


def parse_cams(spec):
    cams = {}
    for tok in spec.split(","):
        tok = tok.strip()
        if tok:
            cid, host = tok.split("=", 1)
            cams[cid.strip()] = host.strip()
    return cams


def read_exact(f, n):
    data = b""
    while len(data) < n:
        chunk = f.read(n - len(data))
        if not chunk:
            return None
        data += chunk
    return data


class Pipe:
    """One ffmpeg for one camera and one output kind; started when wanted, killed when idle or stalled."""

    kind = "?"
    warm = False

    def __init__(self, cam):
        self.cam = cam
        self.cond = threading.Condition()
        self.clients = 0
        self.last_client_t = -1e9  # monotonic; -1e9 because right after boot monotonic is small
        self.snap_until = 0.0
        self.proc = None
        self.proc_t = 0.0
        self.out_t = 0.0           # monotonic time of the latest output unit (frame / fragment)
        self.state = "idle"        # idle / starting / live / error
        self.err = ""
        self.restarts = 0
        self.fps_win = collections.deque()
        threading.Thread(target=self._worker, name="cam%s-%s" % (cam.cid, self.kind), daemon=True).start()

    #=========== demand ===========

    def wanted(self):
        now = time.monotonic()
        return self.warm or self.clients > 0 or now < self.snap_until or (now - self.last_client_t) < IDLE_S

    def add_client(self):
        with self.cond:
            if self.clients >= MAX_CLIENTS:
                return False
            self.clients += 1
            self.cond.notify_all()
            return True

    def drop_client(self):
        with self.cond:
            self.clients = max(0, self.clients - 1)
            self.last_client_t = time.monotonic()

    #=========== ffmpeg ===========

    def input_args(self):
        return ["ffmpeg", "-nostdin", "-loglevel", "error",
                "-rtsp_transport", "tcp", "-timeout", "5000000",          # RTSP socket timeout, microseconds
                "-fflags", "nobuffer", "-flags", "low_delay",
                "-i", self.cam.url, "-an"]

    def cmd(self):
        raise NotImplementedError

    def read_output(self, out):
        raise NotImplementedError

    def mark_output(self):
        # caller holds self.cond
        now = time.monotonic()
        self.out_t = now
        self.state = "live"
        self.fps_win.append(now)
        while self.fps_win and now - self.fps_win[0] > 5.0:
            self.fps_win.popleft()
        self.cond.notify_all()

    def _stderr_reader(self, proc):
        for raw in iter(proc.stderr.readline, b""):
            line = raw.decode("utf-8", "replace").strip()
            if line:
                # never echo the RTSP URL (it carries the credential fields even though the password is empty)
                self.err = line.replace(self.cam.url, "rtsp://%s/..." % self.cam.host)[-200:]

    def kill(self):
        p = self.proc
        if p and p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=2)
            except subprocess.TimeoutExpired:
                p.kill()

    def on_start(self):
        pass

    def _worker(self):
        backoff = 1.0
        while True:
            with self.cond:
                while not self.wanted():
                    self.state = "idle"
                    self.cond.wait(timeout=1.0)
            self.state, self.err = "starting", ""
            self.on_start()
            t0 = time.monotonic()
            try:
                self.proc = subprocess.Popen(self.cmd(), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                             stderr=subprocess.PIPE, bufsize=0)
            except OSError as e:
                self.state, self.err = "error", "spawn: %s" % e
                time.sleep(5)
                continue
            self.proc_t = t0
            log("cam%s %s start %s" % (self.cam.cid, self.kind, self.cam.host))
            threading.Thread(target=self._stderr_reader, args=(self.proc,), daemon=True).start()
            try:
                self.read_output(self.proc.stdout)
            except Exception as e:          # a parser bug must not kill the worker thread
                self.err = "reader: %s" % e
            self.kill()
            rc = self.proc.poll()
            ran = time.monotonic() - t0
            got = self.out_t >= t0
            if self.wanted():
                # Exited while someone still wants it -> camera / network problem.
                self.restarts += 1
                self.state = "error"
                if not self.err:
                    self.err = "ffmpeg exited rc=%s" % rc if got else "no output from camera"
                log("cam%s %s stopped rc=%s after %.0fs: %s" % (self.cam.cid, self.kind, rc, ran, self.err))
                backoff = 1.0 if got and ran > 30 else min(backoff * 2, 10.0)
                time.sleep(backoff)
            else:
                log("cam%s %s idle stop after %.0fs" % (self.cam.cid, self.kind, ran))
                backoff = 1.0

    def supervise(self):
        """Called every second: kill ffmpeg when nobody wants it or when it stalls."""
        p = self.proc
        if not p or p.poll() is not None:
            return
        now = time.monotonic()
        if not self.wanted():
            self.kill()
        elif self.out_t < self.proc_t and now - self.proc_t > START_S:
            self.err = self.err or "no first output within %.0fs" % START_S
            self.kill()
        elif self.out_t >= self.proc_t and now - self.out_t > STALL_S:
            self.err = "stream stalled (%.0fs without output)" % (now - self.out_t)
            self.kill()

    def status(self):
        now = time.monotonic()
        with self.cond:
            w = self.fps_win
            fps = (len(w) - 1) / (w[-1] - w[0]) if len(w) > 1 and w[-1] > w[0] else 0.0
            return {"state": self.state, "clients": self.clients, "fps": round(fps, 1),
                    "last_frame_age_ms": int((now - self.out_t) * 1000) if self.out_t else -1,
                    "err": self.err, "restarts": self.restarts}


class MjpegPipe(Pipe):
    kind = "mjpeg"

    def __init__(self, cam):
        self.frame = None
        self.seq = 0
        super().__init__(cam)

    def cmd(self):
        return self.input_args() + ["-vf", "fps=%g" % FPS, "-q:v", str(QUALITY),
                                    "-f", "mpjpeg", "-boundary_tag", "ffmpeg", "-"]

    def want_snapshot(self):
        with self.cond:
            self.snap_until = time.monotonic() + SNAP_HOLD_S
            self.cond.notify_all()

    def read_output(self, out):
        # mpjpeg: "--ffmpeg\r\nContent-type: image/jpeg\r\nContent-length: N\r\n\r\n<N bytes>\r\n"
        # Length-framed parsing instead of scanning for FFD8/FFD9 — a quant table can contain FF D9.
        while True:
            line = out.readline()
            if not line:
                return
            if not line.startswith(b"--"):
                continue
            length = -1
            while True:
                h = out.readline()
                if not h:
                    return
                h = h.strip()
                if not h:
                    break
                m = re.match(rb"(?i)content-length:\s*(\d+)", h)
                if m:
                    length = int(m.group(1))
            if length <= 0:
                continue
            data = read_exact(out, length)
            if data is None:
                return
            with self.cond:
                self.frame = data
                self.seq += 1
                self.mark_output()


class Mp4Pipe(Pipe):
    kind = "mp4"
    warm = WARM

    def __init__(self, cam):
        self.init = None           # ftyp + moov
        self.codec = ""            # e.g. avc1.4D401F, from moov/avcC
        self.ring = collections.deque(maxlen=MP4_RING)   # (seq, is_key, bytes)
        self.seq = 0
        self.gen = 0               # bumps on every ffmpeg (re)start: viewers must re-send the init segment
        super().__init__(cam)

    def cmd(self):
        # frag_every_frame: one moof+mdat per frame -> no muxer-side buffering (latency ~1 frame).
        return self.input_args() + ["-c:v", "copy", "-f", "mp4",
                                    "-movflags", "empty_moov+default_base_moof+frag_every_frame",
                                    "-flush_packets", "1", "-"]

    def on_start(self):
        with self.cond:
            self.init = None
            self.ring.clear()
            self.gen += 1
            self.cond.notify_all()

    @staticmethod
    def _is_keyframe(mdat_payload):
        # AVCC: [4-byte length][NAL]... ; IDR slice = NAL type 5
        i, n = 0, len(mdat_payload)
        while i + 5 <= n:
            ln = struct.unpack(">I", mdat_payload[i:i + 4])[0]
            if mdat_payload[i + 4] & 0x1F == 5:
                return True
            i += 4 + ln
        return False

    def read_output(self, out):
        head, moof = b"", None
        while True:
            h = read_exact(out, 8)
            if h is None:
                return
            size, typ = struct.unpack(">I4s", h)
            if size == 1:
                ext = read_exact(out, 8)
                if ext is None:
                    return
                size = struct.unpack(">Q", ext)[0]
                h += ext
            if size < len(h):
                raise ValueError("bad box size %d (%r)" % (size, typ))
            body = read_exact(out, size - len(h))
            if body is None:
                return
            box = h + body
            if typ in (b"ftyp", b"moov"):
                head += box
                if typ == b"moov":
                    i = box.find(b"avcC")
                    codec = "avc1.%02X%02X%02X" % (box[i + 5], box[i + 6], box[i + 7]) if i > 0 else ""
                    with self.cond:
                        self.init, self.codec = head, codec
                        self.cond.notify_all()
            elif typ == b"moof":
                moof = box
            elif typ == b"mdat" and moof is not None:
                frag, key = moof + box, self._is_keyframe(body)
                moof = None
                with self.cond:
                    self.seq += 1
                    self.ring.append((self.seq, key, frag))
                    self.mark_output()

    def status(self):
        s = super().status()
        s["codec"] = self.codec
        return s


class Camera:
    def __init__(self, cid, host):
        self.cid = cid
        self.host = host
        self.url = "rtsp://%s:554/user=admin&password=&channel=1&stream=%d.sdp?" % (host, STREAM)
        self.mjpeg = MjpegPipe(self)
        self.mp4 = Mp4Pipe(self)
        self.pipes = (self.mp4, self.mjpeg)

    def status(self):
        modes = {p.kind: p.status() for p in self.pipes}
        # Top-level fields describe the pipe that matters right now: one with viewers, else a running one.
        active = [p for p in self.pipes if p.clients > 0] or [p for p in self.pipes if p.state != "idle"]
        main = modes[active[0].kind] if active else modes["mp4"]
        return {"id": self.cid, "host": self.host, "state": main["state"],
                "clients": sum(m["clients"] for m in modes.values()), "fps": main["fps"],
                "last_frame_age_ms": main["last_frame_age_ms"], "err": main["err"],
                "restarts": sum(m["restarts"] for m in modes.values()), "codec": self.mp4.codec, "modes": modes}


CAMS = {cid: Camera(cid, host) for cid, host in
        parse_cams(os.environ.get("FCV_CAMS", "1=192.168.1.112,2=192.168.1.113")).items()}


def supervisor():
    while True:
        for c in CAMS.values():
            for p in c.pipes:
                try:
                    p.supervise()
                except Exception as e:      # keep supervising the others
                    log("supervise cam%s %s: %s" % (c.cid, p.kind, e))
        time.sleep(1.0)


class Handler(BaseHTTPRequestHandler):
    server_version = "fcv-cam/2"
    protocol_version = "HTTP/1.0"     # one request per connection; streams end when the socket closes

    def log_message(self, fmt, *args):
        pass                          # per-request lines would flood the log; viewers are logged on connect

    def _send(self, code, body, ctype="text/plain; charset=utf-8"):
        b = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")   # the GUI is served from the crane (:8080)
        self.end_headers()
        self.wfile.write(b)

    def _stream_headers(self, ctype):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Pragma", "no-cache")
        self.send_header("Access-Control-Allow-Origin", "*")   # fetch() of the .mp4 from the crane-served page
        self.send_header("Connection", "close")
        self.end_headers()

    def _peer_closed(self):
        # Nothing gets written while a camera is down, so a closed tab would never raise BrokenPipe and
        # would hold a viewer slot forever. Readable + zero-byte peek = the peer closed.
        try:
            r, _, _ = select.select([self.connection], [], [], 0)
            return bool(r) and self.connection.recv(1, socket.MSG_PEEK) == b""
        except OSError:
            return True

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/cam/status":
            body = {"warm": WARM, "fps_target": FPS, "q": QUALITY, "stream": STREAM, "max_clients": MAX_CLIENTS,
                    "cams": [c.status() for c in CAMS.values()]}
            return self._send(200, json.dumps(body), "application/json")
        m = re.match(r"^/cam/([^/.]+)\.(mjpg|jpg|mp4)$", path)
        if not m or m.group(1) not in CAMS:
            return self._send(404, "endpoints: /cam/status, /cam/<id>.mp4, /cam/<id>.mjpg, /cam/<id>.jpg  ids: %s\n"
                              % ",".join(CAMS))
        cam, ext = CAMS[m.group(1)], m.group(2)
        if ext == "jpg":
            return self._snapshot(cam.mjpeg)
        pipe = cam.mp4 if ext == "mp4" else cam.mjpeg
        if not pipe.add_client():
            return self._send(503, "camera %s: too many %s viewers (max %d)\n" % (cam.cid, pipe.kind, MAX_CLIENTS))
        peer = self.client_address[0]
        log("cam%s %s viewer + %s (now %d)" % (cam.cid, pipe.kind, peer, pipe.clients))
        try:
            self.connection.settimeout(10)    # a stuck viewer must not hold a slot forever
            self.connection.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, SNDBUF)
            if ext == "mp4":
                self._stream_mp4(pipe)
            else:
                self._stream_mjpeg(pipe)
        except (BrokenPipeError, ConnectionResetError, socket.timeout, OSError):
            pass
        finally:
            pipe.drop_client()
            log("cam%s %s viewer - %s (now %d)" % (cam.cid, pipe.kind, peer, pipe.clients))

    def _snapshot(self, pipe):
        pipe.want_snapshot()
        deadline = time.monotonic() + 6.0
        with pipe.cond:
            while time.monotonic() < deadline and (pipe.frame is None or time.monotonic() - pipe.out_t > 2.0):
                pipe.cond.wait(timeout=0.5)
            frame = pipe.frame if pipe.frame is not None and time.monotonic() - pipe.out_t <= 2.0 else None
        if frame is None:
            return self._send(503, "camera %s: no frame (%s)\n" % (pipe.cam.cid, pipe.err or pipe.state))
        return self._send(200, frame, "image/jpeg")

    def _stream_mjpeg(self, pipe):
        self._stream_headers("multipart/x-mixed-replace; boundary=frame")
        last = -1
        while True:
            with pipe.cond:
                if pipe.seq == last:
                    pipe.cond.wait(timeout=2.0)
                fresh = pipe.seq != last and pipe.frame is not None
                if fresh:
                    frame, last = pipe.frame, pipe.seq
            if not fresh:
                if self._peer_closed():
                    return
                continue                  # keep the socket; the <img> keeps showing the last frame
            self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n" % len(frame))
            self.wfile.write(frame)
            self.wfile.write(b"\r\n")

    def _stream_mp4(self, pipe):
        # One continuous fMP4 byte stream: init segment, then fragments starting at a keyframe.
        # If ffmpeg restarts (new gen) we END the response instead of splicing: the new pipe's timestamps
        # restart, and a backwards jump inside one MediaSource stalls playback. The page reconnects.
        self._stream_headers("video/mp4")
        gen, nxt = -1, None           # nxt = seq of the next fragment to send (None = (re)sync at a keyframe)
        while True:
            out = []
            with pipe.cond:
                if gen != -1 and pipe.gen != gen:
                    return
                if gen == -1 or pipe.init is None:
                    if pipe.init is None:
                        pipe.cond.wait(timeout=2.0)
                    if pipe.init is not None:
                        gen, nxt = pipe.gen, None
                        out.append(pipe.init)
                if pipe.init is not None and gen == pipe.gen:
                    ring = list(pipe.ring)
                    if nxt is None or (ring and nxt < ring[0][0]):
                        # join / fell behind the ring: start from the newest keyframe we hold (GOP cache)
                        keys = [s for s, k, _ in ring if k]
                        nxt = keys[-1] if keys else None
                    if nxt is not None:
                        out.extend(f for s, _, f in ring if s >= nxt)
                        nxt = (ring[-1][0] + 1) if ring and ring[-1][0] >= nxt else nxt
                    if not out:
                        pipe.cond.wait(timeout=2.0)
            if out:
                self.wfile.write(b"".join(out))
            elif self._peer_closed():
                return


def main():
    def on_term(signum, _frame):
        for c in CAMS.values():
            for p in c.pipes:
                p.kill()
        sys.exit(0)
    signal.signal(signal.SIGTERM, on_term)
    threading.Thread(target=supervisor, name="supervisor", daemon=True).start()
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    srv.daemon_threads = True
    log("fcv-cam on :%d  cams=%s  stream=%d  mp4 warm=%s  mjpeg fps=%g q=%d  idle=%gs max_clients=%d"
        % (PORT, ",".join("%s@%s" % (c.cid, c.host) for c in CAMS.values()), STREAM, WARM, FPS, QUALITY,
           IDLE_S, MAX_CLIENTS))
    try:
        srv.serve_forever()
    finally:
        for c in CAMS.values():
            for p in c.pipes:
                p.kill()


if __name__ == "__main__":
    main()
