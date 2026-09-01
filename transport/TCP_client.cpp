#include "TCP_client.h"
#include "log_utils.h"
#include <cstring>

// [2026-08-31] 重連日誌限流參數。頭 RECONN_LOG_BURST 次照原樣逐次印（短暫抖動
// 仍然完整可見），之後每 RECONN_SUMMARY_MS 一行摘要。狀態轉換（成功）不受限流。
static constexpr int     RECONN_LOG_BURST  = 3;
static constexpr int64_t RECONN_SUMMARY_MS = 30000;

#ifndef _WIN32
#include <netinet/tcp.h>   // TCP_KEEPIDLE / TCP_KEEPINTVL / TCP_KEEPCNT
#endif

namespace {
// Apply TCP keepalive so the kernel detects a dead connection ("半開放" — peer
// powered off / cable yanked / NAT timeout / etc) within ~19s instead of the
// default ~2hr. Without this, sendAndReceive sits on a stale socket sending
// data into the void until the next monitor poll happens to call available()
// AND that returns -1 — the latter takes minutes on some kernels because send
// alone doesn't always trigger TCP RST detection.
//
// Idle 10s + 3 probes × 3s interval = 19s worst-case dead detection. Below
// that range, false positives on slow/loaded networks; above, sendAndReceive
// timeouts pile up before the monitor can swap the socket.
//
// Linux-only (full per-connection control); Windows just enables SO_KEEPALIVE
// with system-default timing (~2hr) — slightly worse than Linux but still
// better than nothing for laptop dev.
inline void apply_keepalive(int sock_fd) {
#ifdef _WIN32
    BOOL yes = TRUE;
    setsockopt((SOCKET)sock_fd, SOL_SOCKET, SO_KEEPALIVE,
               (const char*)&yes, sizeof(yes));
#else
    int yes = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
    int idle = 10, intvl = 3, cnt = 3;
    setsockopt(sock_fd, IPPROTO_TCP, TCP_KEEPIDLE,  &idle,  sizeof(idle));
    setsockopt(sock_fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(sock_fd, IPPROTO_TCP, TCP_KEEPCNT,   &cnt,   sizeof(cnt));
#endif
}

// Writing to a socket whose peer has already closed raises SIGPIPE on Linux, and
// SIGPIPE's default disposition TERMINATES THE PROCESS — no error return, no
// exception, just a dead program and a "Broken pipe" line in the shell. This was
// hit in the field (washrobot died mid-run when the peer dropped).
// MSG_NOSIGNAL turns that into an ordinary -1/EPIPE return, which the existing
// `<= 0` checks below already treat as a dead socket. Windows has neither SIGPIPE
// nor the flag, so it stays 0 there.
// [2026-08-27] Moved down here from the callers: signal(SIGPIPE, SIG_IGN) only
// protects a main.cpp that remembers to call it, and 1 of the binaries linking
// this driver did not (Linux_test). Per-send is the defence that cannot be
// forgotten by a future caller.
#ifdef _WIN32
constexpr int SEND_FLAGS = 0;
#else
constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#endif
} // namespace

//=========== init ===========

#ifdef _WIN32
TCP_client::TCP_client() : sock(INVALID_SOCKET), initialized(false), connected(false), monitor_running(false) {
	_log_tag = "TCP";
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) initialized = true;
}
TCP_client::~TCP_client() { close(); if (initialized) WSACleanup(); }
#else
TCP_client::TCP_client() : sock(INVALID_SOCKET), initialized(true), connected(false), monitor_running(false) {
	_log_tag = "TCP";
}
TCP_client::~TCP_client() { close(); }
#endif

bool TCP_client::connectToServer(const std::string& ip, int port, bool debug) {
	std::lock_guard<std::mutex> lock(socket_mtx);
	debug_mode = debug;
	last_ip = ip;
	last_port = port;
	_log_tag = "TCP " + ip + ":" + std::to_string(port);

	if (!initialized) return false;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET) return false;

	sockaddr_in server{};
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
#ifdef _WIN32
	InetPtonA(AF_INET, ip.c_str(), &server.sin_addr);
#else
	inet_pton(AF_INET, ip.c_str(), &server.sin_addr);
#endif

	if (connect(sock, (sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
		LOG_ERR(_log_tag, "Initial connection failed");
#ifdef _WIN32
		closesocket(sock);
#else
		::close(sock);
#endif
		sock = INVALID_SOCKET;
		connected = false;
		// start Monitor even on failure so it can retry later
		startMonitor();
		return false;
	}

	apply_keepalive(sock);
	connected = true;
	rx_timeout_streak_.store(0);
	LOG_INF(_log_tag, "Connected to %s:%d", ip.c_str(), port);
	startMonitor();
	return true;
}

//=========== worker thread: monitor ===========

void TCP_client::startMonitor() {
	if (monitor_running) return;
	monitor_running = true;
	if (monitor_thread.joinable()) monitor_thread.detach();
	monitor_thread = std::thread(&TCP_client::reconnectLoop, this);
}

void TCP_client::reconnectLoop() {
	while (monitor_running) {
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		if (!connected || available() < 0) {
			if (!monitor_running) break;

			std::lock_guard<std::mutex> lock(socket_mtx);
			connected = false;
			if (sock != INVALID_SOCKET) {
#ifdef _WIN32
				closesocket(sock);
#else
				::close(sock);
#endif
				sock = INVALID_SOCKET;
			}

			// Reconnect events are operationally critical (every reconnect ~500ms-1s
			// during which all sendAndReceive calls fail) — log unconditionally so
			// diagnosis doesn't depend on debug_mode being enabled at startup.
			// quiet_reconnect_log_ is a separate, explicit per-instance opt-out
			// (see header) — not gated by debug_mode.
			//
			// 🔴 [2026-08-31] 但「無條件」在對端長時間離線時會洗版：每 500ms 兩行
			//    （reconnecting + reconnect failed），實測吊機關機 45 秒 = 107 組，
			//    把 log 裡其他訊息整個沖掉（2026-08-31 上機時親眼看到）。
			//    ⚠️ 不採用 quiet_reconnect_log_ 的理由：arm 是「已知還沒裝、
			//    永遠不會接上」＝純噪音；而**吊機離線是該被看見的事件**，靜音會把該看的也藏掉。
			//    → 改為「保留狀態轉換、限流重複失敗」：頭幾次照印，之後每
			//    RECONN_SUMMARY_MS 一行摘要（含累計次數與離線時長），恢復時一定印。
			if (reconn_fail_streak_ == 0) reconn_down_since_ms_ = ::user_lib_log::now_ms_mono();
			if (!quiet_reconnect_log_ && reconn_fail_streak_ < RECONN_LOG_BURST) {
				std::fprintf(stderr,
				    "[%s] [WRN] [%s] reconnecting %s:%d ...\n",
				    ::user_lib_log::now_ts().c_str(),
				    _log_tag.c_str(),
				    last_ip.c_str(), last_port);
			}
			LOG_INF(_log_tag, "Attempting to reconnect...");

			socket_t new_sock = socket(AF_INET, SOCK_STREAM, 0);
			if (new_sock == INVALID_SOCKET) continue;

			// 1. set non-blocking
#ifdef _WIN32
			u_long mode = 1;
			ioctlsocket(new_sock, FIONBIO, &mode);
#else
			int flags = fcntl(new_sock, F_GETFL, 0);
			fcntl(new_sock, F_SETFL, flags | O_NONBLOCK);
#endif

			sockaddr_in server{};
			server.sin_family = AF_INET;
			server.sin_port = htons(last_port);
#ifdef _WIN32
			InetPtonA(AF_INET, last_ip.c_str(), &server.sin_addr);
#else
			inet_pton(AF_INET, last_ip.c_str(), &server.sin_addr);
#endif

			// 2. attempt connect
			int res = connect(new_sock, (sockaddr*)&server, sizeof(server));

			bool success = false;
#ifdef _WIN32
			if (res == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
#else
			if (res == SOCKET_ERROR && errno == EINPROGRESS) {
#endif
				// 3. wait with 1s select timeout
				fd_set writefds;
				FD_ZERO(&writefds);
				FD_SET(new_sock, &writefds);
				struct timeval timeout;
				timeout.tv_sec = 1;
				timeout.tv_usec = 0;

				res = select((int)new_sock + 1, NULL, &writefds, NULL, &timeout);
				// [2026-08-28] select() > 0 alone is NOT success. A connect that
				// FAILED (ECONNREFUSED on a port nobody listens to) also makes the
				// socket writable, so the old `if (res > 0) success = true` reported
				// "reconnect success" and set connected = true against a dead peer.
				// Measured on the bench: crane :5002 had no listener at all
				// (confirmed with ss -ltn) and washrobot still logged 20 consecutive
				// "reconnect success" lines, one every 500 ms.
				// POSIX requires reading SO_ERROR to tell the two apart — that is
				// the only thing that distinguishes them.
				if (res > 0) {
					int so_err = 0;
#ifdef _WIN32
					int errlen = sizeof(so_err);
					if (getsockopt(new_sock, SOL_SOCKET, SO_ERROR,
					               (char*)&so_err, &errlen) == 0 && so_err == 0) {
						success = true;
					}
#else
					socklen_t errlen = sizeof(so_err);
					if (getsockopt(new_sock, SOL_SOCKET, SO_ERROR,
					               &so_err, &errlen) == 0 && so_err == 0) {
						success = true;
					}
#endif
				}
			}
			else if (res == 0) {
				success = true;
			}

			// 4. set back to blocking
#ifdef _WIN32
			mode = 0;
			ioctlsocket(new_sock, FIONBIO, &mode);
#else
			fcntl(new_sock, F_SETFL, flags);
#endif

			if (success) {
				apply_keepalive(new_sock);
				sock = new_sock;
				connected = true;
				rx_timeout_streak_.store(0);
				// 狀態轉換一定印，而且帶上「斷了多久／試了幾次」——這兩個數字正是
				// 被舊版洗版沖掉、事後最想知道的東西。
				if (!quiet_reconnect_log_) {
					const int64_t down_ms = ::user_lib_log::now_ms_mono() - reconn_down_since_ms_;
					std::fprintf(stderr,
					    "[%s] [INF] [%s] reconnect success (斷線 %.1fs, 嘗試 %d 次)\n",
					    ::user_lib_log::now_ts().c_str(),
					    _log_tag.c_str(),
					    down_ms / 1000.0, reconn_fail_streak_ + 1);
				}
				reconn_fail_streak_ = 0;
				reconn_last_log_ms_ = 0;
				LOG_INF(_log_tag, "Reconnect success");
			}
			else {
#ifdef _WIN32
				closesocket(new_sock);
#else
				::close(new_sock);
#endif
				++reconn_fail_streak_;
				if (!quiet_reconnect_log_) {
					const int64_t now = ::user_lib_log::now_ms_mono();
					if (reconn_fail_streak_ <= RECONN_LOG_BURST) {
						std::fprintf(stderr,
						    "[%s] [ERR] [%s] reconnect failed (will retry in 500ms)\n",
						    ::user_lib_log::now_ts().c_str(),
						    _log_tag.c_str());
						reconn_last_log_ms_ = now;
					}
					else if (now - reconn_last_log_ms_ >= RECONN_SUMMARY_MS) {
						std::fprintf(stderr,
						    "[%s] [ERR] [%s] 仍未連上 %s:%d — 已嘗試 %d 次 / 離線 %.0fs（每 %ds 回報一次）\n",
						    ::user_lib_log::now_ts().c_str(),
						    _log_tag.c_str(), last_ip.c_str(), last_port,
						    reconn_fail_streak_,
						    (now - reconn_down_since_ms_) / 1000.0,
						    (int)(RECONN_SUMMARY_MS / 1000));
						reconn_last_log_ms_ = now;
					}
				}
			}
			}
		}
	}

//=========== utility: send/recv ===========

bool TCP_client::sendData(const char* buf, int len, int timeout_ms) {
	std::lock_guard<std::mutex> lock(socket_mtx);
	if (!connected || sock == INVALID_SOCKET) return false;

	// Drain stale bytes from kernel recv buffer before sending the next request.
	//
	// USR-TCP232 transparent gateways forward EVERY byte from the RS485 side into
	// the TCP socket — including:
	//   - late replies whose Modbus transaction already timed out caller-side
	//   - cross-talk from polling on shared bus (e.g. SD76 + SE3 + CLV900 on USR_A)
	//   - partial / corrupted frames from prior bus glitches
	// These accumulate in the kernel recv buffer. Without draining, the next
	// receiveData() returns the stale tail concatenated with the genuine reply,
	// and the driver-side validation reports "bad reply len=N" (N >> expected)
	// and aborts the transaction.
	//
	// Safe because TCP_client is used exclusively for request-response Modbus
	// gateway traffic in this project (washrobot/crane cross-PI uses TCP_server,
	// not TCP_client). No streaming-receive caller exists that would lose data.
	{
#ifdef _WIN32
		u_long mode = 1;
		ioctlsocket(sock, FIONBIO, &mode);
#else
		int orig_flags = fcntl(sock, F_GETFL, 0);
		fcntl(sock, F_SETFL, orig_flags | O_NONBLOCK);
#endif
		char trash[256];
		int total_drained = 0;
		while (true) {
			int got = recv(sock, trash, sizeof(trash), 0);
			if (got <= 0) break;
			total_drained += got;
			if (total_drained > 4096) break;   // safety: cap, don't drain forever
		}
#ifdef _WIN32
		mode = 0;
		ioctlsocket(sock, FIONBIO, &mode);
#else
		fcntl(sock, F_SETFL, orig_flags);
#endif
		if (total_drained > 0) {
			LOG_DBG(_log_tag, "drained %d stale bytes before TX", total_drained);
		}
	}

#ifdef _WIN32
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
#else
	struct timeval tv;
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

	int result = send(sock, buf, len, SEND_FLAGS);
	if (result > 0) {
		LOG_HEX(_log_tag, "TX", buf, len);
	} else {
		// send() failing (EPIPE/ECONNRESET/...) means the socket is dead, not
		// just slow. Without this, `connected` stays true forever whenever
		// available()'s health check doesn't happen to catch the same break
		// first — reconnectLoop only fires on `!connected`, so a caller-side
		// send failure must itself demote the flag or the client never
		// self-heals (see reconnectLoop's design comment above apply_keepalive).
		connected = false;
	}
	return result > 0;
}

int TCP_client::receiveData(char* buf, int bufSize, int timeout_ms) {
	std::lock_guard<std::mutex> lock(socket_mtx);
	if (!connected || sock == INVALID_SOCKET) return -1;

#ifdef _WIN32
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
#else
	struct timeval tv;
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

	int received = recv(sock, buf, bufSize - 1, 0);
	if (received <= 0) {
		// received == 0 is an orderly remote close (real disconnect) — demote
		// so reconnectLoop notices, same reasoning as sendData() above.
		// received < 0 here (timeout/EWOULDBLOCK from SO_RCVTIMEO) is just
		// "no reply yet", normal for a slow device — must NOT mark the
		// connection dead over that or every timeout would force a needless
		// reconnect on an otherwise-fine socket.
		if (received == 0) connected = false;
		return (received == 0) ? -1 : 0;
	}

	LOG_HEX(_log_tag, "RX", buf, received);
	buf[received] = 0;
	return received;
}

// [2026-09-01] 見標頭的說明。與 sendAndReceive() 內部的排空邏輯相同。
int TCP_client::drainRx()
{
	std::lock_guard<std::mutex> lock(socket_mtx);
	if (!connected || sock == INVALID_SOCKET) return 0;
#ifdef _WIN32
	u_long mode = 1;
	ioctlsocket(sock, FIONBIO, &mode);
#else
	int orig_flags = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, orig_flags | O_NONBLOCK);
#endif
	char trash[256];
	int total = 0;
	while (true) {
		int got = recv(sock, trash, sizeof(trash), 0);
		if (got <= 0) break;
		total += got;
		if (total > 4096) break;
	}
#ifdef _WIN32
	mode = 0;
	ioctlsocket(sock, FIONBIO, &mode);
#else
	fcntl(sock, F_SETFL, orig_flags);
#endif
	if (total > 0)
		LOG_DBG(_log_tag, "drainRx: 丟棄 %d 個殘留位元組（前一筆交易的遲到回覆）", total);
	return total;
}

// [2026-09-01] 連續接收逾時達門檻 → 主動斷線，讓背景 reconnectLoop 收拾。
//
// 為什麼需要：**單次**逾時不該斷線（慢裝置是常態，見 receiveData() 的說明），
// 但「永遠逾時」和「這次比較慢」在 recv 這一層長得一模一樣。而 RTU 沒有交易序號，
// 一旦回覆遲到就會落入**自我延續的失步**：
//   t=0   送 A → 150ms 逾時（A 的回覆還在路上）
//   t=190 排空（緩衝區還是空的）→ 送 B
//   t=200 A 的回覆抵達 → recv 立刻拿到它，當成 B 的回覆
// 同型別的請求（讀狀態 vs 讀狀態）連 CRC 都會過 → **靜默採用錯誤資料且永遠慢一筆**；
// 不同型別則永遠 bad reply。兩種都不會自己好，因為送出前排空永遠早一步。
// 2026-09-01 左側 SE3 就是這樣卡死（keepalive 0/50、socket 仍 ESTAB、Recv-Q 卡著
// 一筆完整回覆），連續 5 次起步量測全都停在 ERR vfd_start_fail。
//
// 斷線會重建 socket，核心緩衝區隨之丟棄 —— 這是唯一能清掉失步的手段。
//
// 門檻取 10 而不是 3~5：SE3 的 reliable_*_one 單次操作內含 8 次重試，門檻若低於它，
// 一叢本來就會失敗的重試會在叢內觸發斷線，把「這次操作失敗」變成「連線也被拆掉」。
// 10 次 × 150ms ≈ 1.5 秒 —— 相對於「永遠不會好」已經夠快。
void TCP_client::note_rx_timeout()
{
	const int streak = rx_timeout_streak_.fetch_add(1) + 1;
	if (streak < RX_TIMEOUT_DISCONNECT_N) return;
	LOG_ERR(_log_tag, "連續 %d 次接收逾時 —— 主動斷線以清除可能的失步", streak);
	rx_timeout_streak_.store(0);
	connected = false;
}

int TCP_client::sendAndReceive(const char* tx_buf, int tx_len,
                               char* rx_buf, int rx_size,
                               int send_timeout_ms, int recv_timeout_ms)
{
	std::lock_guard<std::mutex> lock(socket_mtx);
	if (!connected || sock == INVALID_SOCKET) return -1;

	// Drain stale bytes that earlier abandoned/failed transactions left behind.
	// Inside the same lock as the upcoming send+recv → no concurrent caller
	// can have pending in-flight reply bytes that this drain might steal.
	{
#ifdef _WIN32
		u_long mode = 1;
		ioctlsocket(sock, FIONBIO, &mode);
#else
		int orig_flags = fcntl(sock, F_GETFL, 0);
		fcntl(sock, F_SETFL, orig_flags | O_NONBLOCK);
#endif
		char trash[256];
		int total_drained = 0;
		while (true) {
			int got = recv(sock, trash, sizeof(trash), 0);
			if (got <= 0) break;
			total_drained += got;
			if (total_drained > 4096) break;
		}
#ifdef _WIN32
		mode = 0;
		ioctlsocket(sock, FIONBIO, &mode);
#else
		fcntl(sock, F_SETFL, orig_flags);
#endif
		if (total_drained > 0) {
			LOG_DBG(_log_tag, "drained %d stale bytes before TX (atomic)", total_drained);
		}
	}

	// Send
#ifdef _WIN32
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&send_timeout_ms, sizeof(send_timeout_ms));
#else
	struct timeval tv;
	tv.tv_sec  = send_timeout_ms / 1000;
	tv.tv_usec = (send_timeout_ms % 1000) * 1000;
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
	int sent = send(sock, tx_buf, tx_len, SEND_FLAGS);
	if (sent <= 0) {
		connected = false;   // dead socket, not just slow — see sendData()'s comment
		return 0;
	}
	LOG_HEX(_log_tag, "TX", tx_buf, tx_len);

	// Receive (mutex still held from before the send → atomic transaction)
#ifdef _WIN32
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&recv_timeout_ms, sizeof(recv_timeout_ms));
#else
	tv.tv_sec  = recv_timeout_ms / 1000;
	tv.tv_usec = (recv_timeout_ms % 1000) * 1000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
	int received = recv(sock, rx_buf, rx_size - 1, 0);
	if (received <= 0) {
		// == 0: orderly remote close, real disconnect. < 0: timeout/EWOULDBLOCK,
		// just "no reply yet" — see receiveData()'s comment for why a SINGLE
		// one of those must not demote `connected`; note_rx_timeout() handles
		// the "it never comes back" case that comment doesn't cover.
		if (received == 0) { connected = false; rx_timeout_streak_.store(0); return -1; }
		note_rx_timeout();
		return 0;
	}
	note_rx_ok();
	LOG_HEX(_log_tag, "RX", rx_buf, received);
	rx_buf[received] = 0;
	return received;
}

// [2026-08-28] Fragmentation-tolerant sibling of sendAndReceive(). See the
// header for WHY this exists (USR-TCP232 packs by inter-character gap; at
// 115200 that gap is ~0.3ms so one Modbus frame can arrive as two TCP
// segments). Identical drain+send half — only the recv half differs.
int TCP_client::sendAndReceiveQuiet(const char* tx_buf, int tx_len,
                                    char* rx_buf, int rx_size,
                                    int send_timeout_ms, int total_timeout_ms,
                                    int quiet_ms)
{
	std::lock_guard<std::mutex> lock(socket_mtx);
	if (!connected || sock == INVALID_SOCKET) return -1;

	// Drain stale bytes — same rationale as sendAndReceive(): inside the same
	// lock as the upcoming send+recv, so no concurrent caller can have pending
	// in-flight reply bytes that this drain might steal.
	{
#ifdef _WIN32
		u_long mode = 1;
		ioctlsocket(sock, FIONBIO, &mode);
#else
		int orig_flags = fcntl(sock, F_GETFL, 0);
		fcntl(sock, F_SETFL, orig_flags | O_NONBLOCK);
#endif
		char trash[256];
		int total_drained = 0;
		while (true) {
			int got = recv(sock, trash, sizeof(trash), 0);
			if (got <= 0) break;
			total_drained += got;
			if (total_drained > 4096) break;   // safety: cap, don't drain forever
		}
#ifdef _WIN32
		mode = 0;
		ioctlsocket(sock, FIONBIO, &mode);
#else
		fcntl(sock, F_SETFL, orig_flags);
#endif
		if (total_drained > 0) {
			LOG_DBG(_log_tag, "drained %d stale bytes before TX (atomic/quiet)", total_drained);
		}
	}

	// Send
#ifdef _WIN32
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&send_timeout_ms, sizeof(send_timeout_ms));
#else
	struct timeval tv;
	tv.tv_sec  = send_timeout_ms / 1000;
	tv.tv_usec = (send_timeout_ms % 1000) * 1000;
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
	// [2026-08-28 merge] SEND_FLAGS, not 0. sendAndReceiveQuiet() arrived from main
	// while the MSG_NOSIGNAL fix was on this branch, so the text merged cleanly and
	// left this one send() unprotected — a peer disconnect here would SIGPIPE the
	// whole process. QX_DO24 is this function's only caller and Linux_test (which
	// links it) does not install signal(SIGPIPE, SIG_IGN). See the SEND_FLAGS
	// comment at the top of this file.
	int sent = send(sock, tx_buf, tx_len, SEND_FLAGS);
	if (sent <= 0) {
		connected = false;   // dead socket, not just slow — see sendData()'s comment
		return 0;
	}
	LOG_HEX(_log_tag, "TX", tx_buf, tx_len);

	// Receive, accumulating until the reply goes quiet (mutex still held → the
	// whole transaction stays atomic against other threads on this socket).
	//
	// Two clocks:
	//   - total_timeout_ms : hard cap measured from now; bounds "device never
	//                        answered" and also a pathological dribble.
	//   - quiet_ms         : per-read socket timeout AFTER the first byte. Once a
	//                        read times out with bytes already in hand, the frame
	//                        is complete — that is the terminating condition.
	// Before any byte arrives we wait on the remaining total budget, not quiet_ms,
	// so a slow-to-answer device isn't cut off after a few ms.
	const auto start = std::chrono::steady_clock::now();
	int total = 0;

	while (total < rx_size - 1) {
		const auto elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - start).count();
		const int remaining = total_timeout_ms - elapsed;
		if (remaining <= 0) break;

		const int wait_ms = (total == 0) ? remaining
		                                 : (quiet_ms < remaining ? quiet_ms : remaining);
#ifdef _WIN32
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&wait_ms, sizeof(wait_ms));
#else
		tv.tv_sec  = wait_ms / 1000;
		tv.tv_usec = (wait_ms % 1000) * 1000;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
		int got = recv(sock, rx_buf + total, rx_size - 1 - total, 0);
		if (got > 0) {
			total += got;
			continue;               // more may still be coming — keep reading
		}
		if (got == 0) {             // orderly remote close = real disconnect
			connected = false;
			rx_timeout_streak_.store(0);
			return (total > 0) ? total : -1;
		}
		// got < 0 = timeout/EWOULDBLOCK. With bytes in hand this is the normal
		// end of frame; with none it means the device never answered.
		break;
	}

	if (total <= 0) { note_rx_timeout(); return 0; }
	note_rx_ok();
	LOG_HEX(_log_tag, "RX", rx_buf, total);
	rx_buf[total] = 0;
	return total;
}

//=========== utility: available / close ===========

// 🔴🔴 [2026-09-01] 兩處修正 —— 這支先前是**跨執行緒改共用 socket 模式而且不拿鎖**。
//
// 舊版行為：`fcntl(F_GETFL)` → `fcntl(F_SETFL, flags|O_NONBLOCK)` → `recv(MSG_PEEK)`
//           → `fcntl(F_SETFL, flags)` 還原，**全程沒有 socket_mtx**。
// 而 reconnectLoop 對**每一個連線、每 500ms** 呼叫它一次（連線正常時
// `if (!connected || available() < 0)` 的短路讓 available() 一定被執行）。
//
// 造成兩種故障：
//
// (a) **讓別條執行緒的阻塞 recv() 瞬間返回**
//     監看緒把 socket 設成非阻塞的那一瞬間，worker 正在 sendAndReceive 的 recv()
//     裡 → 立刻 EAGAIN → 驅動判定「無回覆」→ 裝置隨後送到的回覆沒人讀 → 失步。
//
// (b) **把 socket 永久留在非阻塞** ← 這個是永久性的
//     sendAndReceive/drainRx 的排空也會 F_GETFL / 設 O_NONBLOCK / 還原。
//     若 available() 在**排空期間**讀到 flags，它讀到的「原值」已含 O_NONBLOCK，
//     還原時就把非阻塞寫回去 → **之後每次 recv 立刻 EAGAIN、每筆交易都「逾時」**。
//
// 🔬 現場指紋（2026-09-01 `.20` 匯流排卡死）：
//     17:24:53.170 [ZDT:5] RX Pos Mode: TIMEOUT
//     17:24:53.271 [ZDT:5] RX Pos Mode: TIMEOUT   ← 只差 101ms
//     那條路徑上兩次 TIMEOUT 之間應有 readEcho(500) + release_stall_flag + sleep(100)
//     ≥600ms，且 recv 逾時本身設 300ms。**實測只差 101ms ＝ 那些 recv 根本沒有等。**
//     這同時解釋了：Recv-Q 持續非 0（回覆有到但每次 recv 立刻 EAGAIN 不讀）、
//     只有重連救得回來（新 socket ＝ 全新 flags）、隨機且跨網關跨程式發生
//     （每個 TCP_client 都有自己的 500ms 監看緒）、以及為什麼補了排空還是會卡
//     （排空清得掉緩衝區，清不掉 socket 模式）。
//
// 修法：
//   ① 拿 socket_mtx。reconnectLoop 是「先呼叫本函式、返回後才建 lock_guard」，
//      不是巢狀持有，沒有死鎖風險。代價只是健康檢查可能排在一筆長 recv 之後。
//   ② Linux 改用 MSG_DONTWAIT，**完全不碰 fcntl** —— 根本不去動共用狀態，
//      比加鎖更徹底：即使日後有人忘了鎖，也不會再把模式改壞。
//      Windows 沒有 MSG_DONTWAIT，維持 ioctl，但現在在鎖內。
int TCP_client::available() {
	std::lock_guard<std::mutex> lock(socket_mtx);
	if (sock == INVALID_SOCKET) return -1;
#ifdef _WIN32
	u_long mode = 1;
	ioctlsocket(sock, FIONBIO, &mode);
	char tmp;
	int r = recv(sock, &tmp, 1, MSG_PEEK);
	int err = WSAGetLastError();
	mode = 0;
	ioctlsocket(sock, FIONBIO, &mode);
	if (r > 0) return 1;
	if (err == WSAEWOULDBLOCK) return 0;
	return -1;
#else
	// MSG_PEEK (not ioctl FIONREAD) so a peer-closed connection (orderly FIN,
	// recv() returns 0) is actually detected. FIONREAD only reports queued
	// byte count and reads 0 for "no data yet" and "peer closed, no data"
	// alike — it never signals the close itself, so reconnectLoop's
	// `available() < 0` check silently never fired on a clean remote
	// shutdown and the connection stayed "connected" forever. Mirrors the
	// Windows branch above.
	// MSG_DONTWAIT 取代先前的 fcntl 切換 —— 見上方 (a)(b) 的說明。
	char tmp;
	int r = recv(sock, &tmp, 1, MSG_PEEK | MSG_DONTWAIT);
	int err = errno;
	if (r > 0) return 1;
	if (r == 0) return -1;                                // peer closed
	if (err == EWOULDBLOCK || err == EAGAIN) return 0;     // no data, still open
	return -1;
#endif
}

void TCP_client::close() {
	monitor_running = false;
	if (monitor_thread.joinable()) {
		// must not join under lock — would deadlock
		monitor_thread.join();
	}
	std::lock_guard<std::mutex> lock(socket_mtx);
	if (sock != INVALID_SOCKET) {
#ifdef _WIN32
		closesocket(sock);
#else
		::close(sock);
#endif
		sock = INVALID_SOCKET;
	}
	connected = false;
}
