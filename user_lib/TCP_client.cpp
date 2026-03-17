#include "TCP_client.h"
#include <iostream>
#include <cstring>
#include <iomanip>    // 用於 std::put_time, std::setfill, std::setw
#include <ctime>      // 用於 std::tm
#include <sstream>    // <--- 補上這行，修正 C2079 錯誤
#include <chrono>     // 確保時間工具可用

// 取得目前時間字串 [YYYY-MM-DD HH:MM:SS.ms]
std::string TCP_client::getCurrentTimestamp() {
	using namespace std::chrono;
	auto now = system_clock::now();
	auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
	auto timer = system_clock::to_time_t(now);
	std::tm bt;
#ifdef _WIN32
	localtime_s(&bt, &timer);
#else
	localtime_r(&timer, &bt);
#endif
	std::ostringstream oss;
	oss << "[" << std::put_time(&bt, "%Y-%m-%d %H:%M:%S")
		<< "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
	return oss.str();
}

void TCP_client::printLog(const std::string& tag, const char* data, int len) {
	std::cout << getCurrentTimestamp() << tag;
	if (data && len > 0) {
		for (int i = 0; i < len; ++i) {
			printf("%02X ", (unsigned char)data[i]);
		}
	}
	std::cout << std::endl;
}

#ifdef _WIN32
TCP_client::TCP_client() : sock(INVALID_SOCKET), initialized(false), connected(false), monitor_running(false) {
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) initialized = true;
}
TCP_client::~TCP_client() { close(); if (initialized) WSACleanup(); }
#else
TCP_client::TCP_client() : sock(INVALID_SOCKET), initialized(true), connected(false), monitor_running(false) {}
TCP_client::~TCP_client() { close(); }
#endif

bool TCP_client::connectToServer(const std::string& ip, int port, bool debug) {
	std::lock_guard<std::mutex> lock(socket_mtx);
	debug_mode = debug;
	last_ip = ip;
	last_port = port;

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
		if (debug_mode) printLog("[ERROR] Initial connection failed");
#ifdef _WIN32
		closesocket(sock);
#else
		::close(sock);
#endif
		sock = INVALID_SOCKET;
		connected = false;
		// 即便連線失敗也啟動 Monitor，讓它之後能自動嘗試
		startMonitor();
		return false;
	}

	connected = true;
	if (debug_mode) printLog("[INFO] Connected to " + ip + ":" + std::to_string(port));
	startMonitor();
	return true;
}

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

			if (debug_mode) printLog("[RECONNECT] Attempting to reconnect...");

			socket_t new_sock = socket(AF_INET, SOCK_STREAM, 0);
			if (new_sock == INVALID_SOCKET) continue;

			// 1. 設定為非阻塞模式
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

			// 2. 嘗試連線
			int res = connect(new_sock, (sockaddr*)&server, sizeof(server));

			bool success = false;
#ifdef _WIN32
			if (res == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
#else
			if (res == SOCKET_ERROR && errno == EINPROGRESS) {
#endif
				// 3. 使用 select 等待 1 秒超時
				fd_set writefds;
				FD_ZERO(&writefds);
				FD_SET(new_sock, &writefds);
				struct timeval timeout;
				timeout.tv_sec = 1; // 設定連線超時為 1 秒
				timeout.tv_usec = 0;

				res = select((int)new_sock + 1, NULL, &writefds, NULL, &timeout);
				if (res > 0) success = true; // Socket 變得可寫，代表連線成功
			}
			else if (res == 0) {
				success = true; // 直接連線成功
			}

			// 4. 將模式設回阻塞 (重要！)
#ifdef _WIN32
			mode = 0;
			ioctlsocket(new_sock, FIONBIO, &mode);
#else
			fcntl(new_sock, F_SETFL, flags);
#endif

			if (success) {
				sock = new_sock;
				connected = true;
				if (debug_mode) printLog("[RECONNECT] Success!");
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
	if (debug_mode && result > 0) printLog("[TX] ", buf, len);
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

	if (debug_mode) printLog("[RX] ", buf, received);
	buf[received] = 0;
	return received;
}

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
		// 不要在 lock 內部 join，避免死鎖
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