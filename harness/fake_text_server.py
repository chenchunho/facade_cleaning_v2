#!/usr/bin/env python3
"""fake_text_server — 假的吊機 / 手臂端點（行導向文字協定）。

    python3 fake_text_server.py --port 15002 --name crane

🔴 為什麼需要：沒有它，`crane_cmd_` / `arm_cmd_` 會對著連不上的位址**無界重試**，
   而關閉序列裡就有這些呼叫。結果是「程式被 harness 砍掉時跑到哪裡」由時序決定 ——
   2026-08-30 實測 base 的軌跡停在 00:19:31、cand 停在 00:19:57，
   **那個差異看起來完全像行為差異，實際上只是誰多活了 25 秒。**

   本專案已經記過同型的教訓：「等埠開了才往下走，不要用 sleep 猜」。
   這裡是它的另一面 —— **收尾也要是確定性的，不能靠「砍掉的那一刻剛好在哪」。**

一律回 `OK`：目的不是模擬吊機，是讓那條路徑**有界地結束**。
⚠️ 代價：吊機端真正的回覆內容沒有被涵蓋。要測那些得起真正的 Crane_control_PI，
   那是另一個題目（見 .claude/refactor_plan.md 的 crane_smoke.txt）。
"""
import argparse
import socketserver
import sys


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        while True:
            line = self.rfile.readline()
            if not line:
                return
            try:
                self.wfile.write(b'OK\n')
                self.wfile.flush()
            except OSError:
                return


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--name', default='text')
    ap.add_argument('--host', default='127.0.0.1')
    a = ap.parse_args()
    srv = Server((a.host, a.port), Handler)
    print(f'[fake_text] {a.name} on {a.host}:{a.port}', file=sys.stderr, flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
