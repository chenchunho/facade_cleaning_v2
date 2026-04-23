#!/usr/bin/env python3
# ============================================================================
# crane_shim.py — Testing-mode shim that replaces Crane_control_PI on :5002.
#
# Purpose:
#   Let washrobot run its automatic step_down / run <n> loop while using the
#   simplified Crane_easy_PI subsystem (:5003) instead of the full main crane.
#   The shim listens on the main-crane port (:5002), translates distance-based
#   commands (pay_out <cm> / retract <cm>) into timed easy-crane actions
#   (down on -> sleep -> down off + stop / up on -> sleep -> up off + stop),
#   and transparently replies to washrobot & web_backend.
#
# Launch: run this INSTEAD of Crane_control_PI on the crane RPi (.101).
#   Both cannot bind :5002 simultaneously.
#
# Command translation: see README.md and .claude/easy_crane_test_mode.md.
#
# Author: Sadie (per 2026-04-21 design)
# ============================================================================

import argparse
import socket
import sys
import threading
import time
import signal

# ---------- config (CLI-overridable) ----------
DEFAULT_LISTEN_HOST   = '0.0.0.0'
DEFAULT_LISTEN_PORT   = 5002
DEFAULT_EASY_HOST     = '192.168.5.26'
DEFAULT_EASY_PORT     = 5003
DEFAULT_RATE_DOWN     = 3.0          # cm/s, pay-out (release rope) speed; measure on hardware
DEFAULT_RATE_UP       = 3.0          # cm/s, retract (pull rope) speed
KEEPALIVE_INTERVAL_S  = 0.5          # feed easy watchdog (easy's timeout = 2s) during motion
EASY_REPLY_TIMEOUT_S  = 2.0
MOTION_LOCK_WAIT_S    = 1.0          # how long a 2nd pay_out waits before returning busy


# ---------- easy-crane client (single shared socket) ----------
class EasyClient:
    """Serialized line-based client to Crane_easy_PI on :5003."""

    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.sock = None
        self.lock = threading.Lock()           # serializes bytes on the wire
        self.rx_buf = b''

    def _connect(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3.0)
        s.connect((self.host, self.port))
        s.settimeout(None)
        self.sock = s
        self.rx_buf = b''

    def _disconnect(self):
        try:
            if self.sock:
                self.sock.close()
        except Exception:
            pass
        self.sock = None
        self.rx_buf = b''

    def _recv_line(self, timeout):
        """Read one '\\n'-terminated line from self.sock within timeout."""
        deadline = time.monotonic() + timeout
        while b'\n' not in self.rx_buf:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('easy reply timeout')
            self.sock.settimeout(remaining)
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError('easy closed connection')
            self.rx_buf += chunk
        idx = self.rx_buf.index(b'\n')
        line = self.rx_buf[:idx]
        self.rx_buf = self.rx_buf[idx + 1:]
        return line.decode('utf-8', errors='replace').rstrip('\r')

    def send(self, cmd, timeout=EASY_REPLY_TIMEOUT_S):
        """Send one command line, return reply line. Thread-safe."""
        with self.lock:
            if self.sock is None:
                self._connect()
            try:
                payload = (cmd.rstrip('\n') + '\n').encode('utf-8')
                self.sock.sendall(payload)
                return self._recv_line(timeout)
            except (OSError, TimeoutError, ConnectionError) as e:
                self._disconnect()
                raise e

    def close(self):
        with self.lock:
            self._disconnect()


# ---------- shim core ----------
class Shim:
    def __init__(self, easy: EasyClient, rate_down, rate_up):
        self.easy = easy
        self.rate_down = rate_down
        self.rate_up = rate_up
        self.motion_lock = threading.Lock()
        self.motion_abort = threading.Event()
        self.running = True

    def shutdown(self):
        """Called on SIGINT / SIGTERM — abort any in-flight motion + stop easy."""
        self.running = False
        self.motion_abort.set()
        try:
            self.easy.send('stop')
        except Exception:
            pass
        self.easy.close()

    # ---------- command handlers ----------
    def handle_motion(self, cm, direction):
        """direction = 'up' (retract) or 'down' (pay_out). cm = distance (int)."""
        if cm <= 0:
            return 'ERR bad_cm_must_be_positive\n'
        rate = self.rate_up if direction == 'up' else self.rate_down
        duration = cm / rate

        acquired = self.motion_lock.acquire(timeout=MOTION_LOCK_WAIT_S)
        if not acquired:
            return 'ERR shim_busy another_motion_in_progress\n'

        try:
            self.motion_abort.clear()

            try:
                reply = self.easy.send(f'{direction} on')
            except Exception as e:
                return f'ERR easy_link_down {e}\n'
            if not reply.startswith('OK'):
                return f'ERR easy_{direction}_on_fail {reply}\n'

            start = time.monotonic()
            aborted = False
            link_err = None
            try:
                while True:
                    elapsed = time.monotonic() - start
                    if elapsed >= duration:
                        break
                    # Wait a keepalive tick OR until aborted, whichever comes first.
                    self.motion_abort.wait(min(KEEPALIVE_INTERVAL_S, duration - elapsed))
                    if self.motion_abort.is_set():
                        aborted = True
                        break
                    # Feed easy's 2s watchdog.
                    try:
                        self.easy.send('ping', timeout=1.0)
                    except Exception as e:
                        link_err = str(e)
                        break
            finally:
                # Always attempt to stop the motor, even on abort / link error.
                try:
                    self.easy.send(f'{direction} off')
                except Exception:
                    pass
                try:
                    self.easy.send('stop')
                except Exception:
                    pass

            if link_err:
                return f'ERR easy_link_down_mid_motion {link_err}\n'
            if aborted:
                return 'ERR aborted\n'
            actual = time.monotonic() - start
            return f'OK shim {direction}={cm}cm duration={actual:.2f}s rate={rate}cm/s\n'
        finally:
            self.motion_abort.clear()
            self.motion_lock.release()

    def handle_stop(self):
        """Stops any in-flight motion AND sends easy stop immediately."""
        self.motion_abort.set()
        try:
            reply = self.easy.send('stop')
        except Exception as e:
            return f'ERR easy_link_down {e}\n'
        return 'OK\n' if reply.startswith('OK') else f'ERR easy_stop_fail {reply}\n'

    def handle_status(self):
        try:
            reply = self.easy.send('status', timeout=1.5)
        except Exception as e:
            return f'ERR easy_link_down {e}\n'
        # easy status: "OK weight=<kg> up=<0|1> down=<0|1> up_stop_kg=<kg> weight_valid=<0|1>"
        # Repackage with a shim_mode marker so upstream can distinguish.
        if reply.startswith('OK'):
            return f'{reply} shim_mode=1\n'
        return reply + '\n'

    def handle_manual(self, direction_on_off_cmd, parts):
        """
        pay_out_left / pay_out_right  -> easy down on/off
        retract_left / retract_right  -> easy up on/off
        (Easy has no left/right distinction; shim maps both sides to the same relay.)
        """
        if len(parts) < 2 or parts[1] not in ('on', 'off'):
            return f'ERR usage:{direction_on_off_cmd}_<on|off>\n'
        state = parts[1]
        if direction_on_off_cmd.startswith('pay_out'):
            easy_cmd = f'down {state}'
        else:
            easy_cmd = f'up {state}'
        try:
            reply = self.easy.send(easy_cmd)
        except Exception as e:
            return f'ERR easy_link_down {e}\n'
        return (reply + '\n') if reply.startswith('OK') else (reply + '\n')

    # ---------- dispatch ----------
    def dispatch(self, line):
        parts = line.strip().split()
        if not parts:
            return 'ERR empty\n'
        cmd = parts[0]

        if cmd == 'ping':
            return 'OK shim_pong\n'

        if cmd == 'pay_out':
            if len(parts) < 2:
                return 'ERR usage:pay_out_<cm>\n'
            try:
                cm = int(float(parts[1]))
            except ValueError:
                return 'ERR bad_cm\n'
            return self.handle_motion(cm, 'down')

        if cmd == 'retract':
            if len(parts) < 2:
                return 'ERR usage:retract_<cm>\n'
            try:
                cm = int(float(parts[1]))
            except ValueError:
                return 'ERR bad_cm\n'
            return self.handle_motion(cm, 'up')

        if cmd in ('stop', 'emergency_stop'):
            return self.handle_stop()

        if cmd == 'status':
            return self.handle_status()

        if cmd in ('pay_out_left', 'pay_out_right', 'retract_left', 'retract_right'):
            return self.handle_manual(cmd, parts)

        # Test-mode explicit blocks — callers should know these don't work here.
        if cmd == 'home_status':
            return 'ERR shim_no_home_use_manual_easy_crane\n'
        if cmd == 'roll_correct':
            return 'ERR shim_no_roll_correct\n'

        # No-ops for main-crane-only features (accept so upstream doesn't error).
        if cmd == 'zero_meters':
            return 'OK shim_noop\n'
        if cmd == 'middle_set':
            return 'OK shim_noop\n'

        return f'ERR unknown_cmd {cmd}\n'


# ---------- TCP server ----------
def serve_client(conn, addr, shim: Shim):
    sys.stderr.write(f'[shim] client connected {addr}\n')
    buf = b''
    try:
        while shim.running:
            try:
                data = conn.recv(4096)
            except OSError:
                break
            if not data:
                break
            buf += data
            while b'\n' in buf:
                idx = buf.index(b'\n')
                line = buf[:idx].decode('utf-8', errors='replace').rstrip('\r')
                buf = buf[idx + 1:]
                if not line:
                    continue
                reply = shim.dispatch(line)
                try:
                    conn.sendall(reply.encode('utf-8'))
                except OSError:
                    break
    finally:
        try:
            conn.close()
        except Exception:
            pass
        sys.stderr.write(f'[shim] client disconnected {addr}\n')


def main():
    ap = argparse.ArgumentParser(description='Crane shim — translates main-crane pay_out/retract to easy crane on :5003')
    ap.add_argument('--listen-host', default=DEFAULT_LISTEN_HOST)
    ap.add_argument('--listen-port', type=int, default=DEFAULT_LISTEN_PORT)
    ap.add_argument('--easy-host',   default=DEFAULT_EASY_HOST)
    ap.add_argument('--easy-port',   type=int, default=DEFAULT_EASY_PORT)
    ap.add_argument('--rate-down', type=float, default=DEFAULT_RATE_DOWN,
                    help='pay-out speed in cm/s (measure on hardware, default 3.0 placeholder)')
    ap.add_argument('--rate-up',   type=float, default=DEFAULT_RATE_UP,
                    help='retract speed in cm/s (measure on hardware, default 3.0 placeholder)')
    args = ap.parse_args()

    sys.stderr.write(f'[shim] starting listen={args.listen_host}:{args.listen_port} '
                     f'easy={args.easy_host}:{args.easy_port} '
                     f'rate_down={args.rate_down}cm/s rate_up={args.rate_up}cm/s\n')

    easy = EasyClient(args.easy_host, args.easy_port)
    try:
        easy.send('ping', timeout=2.0)
        sys.stderr.write(f'[shim] easy crane {args.easy_host}:{args.easy_port} OK\n')
    except Exception as e:
        sys.stderr.write(f'[shim] WARN easy crane not reachable yet: {e} (will retry on demand)\n')

    shim = Shim(easy, args.rate_down, args.rate_up)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        srv.bind((args.listen_host, args.listen_port))
    except OSError as e:
        sys.stderr.write(f'[shim] FATAL bind :{args.listen_port} {e} '
                         f'(is Crane_control_PI still running?)\n')
        return 1
    srv.listen(8)
    sys.stderr.write(f'[shim] ready :{args.listen_port}\n')

    def on_signal(signum, frame):
        sys.stderr.write(f'[shim] signal {signum}, shutting down\n')
        shim.shutdown()
        try:
            srv.close()
        except Exception:
            pass

    signal.signal(signal.SIGINT, on_signal)
    if hasattr(signal, 'SIGTERM'):
        signal.signal(signal.SIGTERM, on_signal)

    try:
        while shim.running:
            try:
                conn, addr = srv.accept()
            except OSError:
                break
            t = threading.Thread(target=serve_client, args=(conn, addr, shim), daemon=True)
            t.start()
    finally:
        shim.shutdown()
        try:
            srv.close()
        except Exception:
            pass
        sys.stderr.write('[shim] stopped\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
