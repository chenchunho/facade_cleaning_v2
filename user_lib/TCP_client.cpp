#include "TCP_client.h"
#include "log_utils.h"
#include <cstring>

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
				sock = new_sock;
				connected = true;
				LOG_INF(_log_tag, "Reconnect success");
			}
			else {
#ifdef _WIN32
				closesocket(new_sock);
#else
				::close(new_sock);
#endif
			}
			}
		}
	}

//=========== utility: send/recv ===========

bool TCP_client::sendData(const char* buf, int len, int timeout_ms) {
	std::lock_guard<std::mutex> lock(socket_mtx);
	if (!connected || sock == INVALID_SOCKET) return false;

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
