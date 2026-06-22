#include "TCP_client.h"
#include "log_utils.h"
#include <cstring>

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
			std::fprintf(stderr,
			    "[%s] [WRN] [%s] reconnecting %s:%d ...\n",
			    ::user_lib_log::now_ts().c_str(),
			    _log_tag.c_str(),
			    last_ip.c_str(), last_port);
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
				if (res > 0) success = true;
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
				std::fprintf(stderr,
				    "[%s] [INF] [%s] reconnect success\n",
				    ::user_lib_log::now_ts().c_str(),
				    _log_tag.c_str());
				LOG_INF(_log_tag, "Reconnect success");
			}
			else {
#ifdef _WIN32
				closesocket(new_sock);
#else
				::close(new_sock);
#endif
				std::fprintf(stderr,
				    "[%s] [ERR] [%s] reconnect failed (will retry in 500ms)\n",
				    ::user_lib_log::now_ts().c_str(),
				    _log_tag.c_str());
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

	int result = send(sock, buf, len, 0);
	if (result > 0) LOG_HEX(_log_tag, "TX", buf, len);
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
	if (received <= 0) return (received == 0) ? -1 : 0;

	LOG_HEX(_log_tag, "RX", buf, received);
	buf[received] = 0;
	return received;
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
	int sent = send(sock, tx_buf, tx_len, 0);
	if (sent <= 0) return 0;
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
	if (received <= 0) return (received == 0) ? -1 : 0;
	LOG_HEX(_log_tag, "RX", rx_buf, received);
	rx_buf[received] = 0;
	return received;
}

//=========== utility: available / close ===========

int TCP_client::available() {
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
	int count = 0;
	if (ioctl(sock, FIONREAD, &count) < 0) return -1;
	return count;
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
