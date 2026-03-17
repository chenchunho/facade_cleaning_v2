#include "TCP_server.h"
#include <iostream>
#include <cstring>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <chrono>

// 取得目前的系統時間，格式化為 [YYYY-MM-DD HH:MM:SS.mmm]
std::string TCP_server::getCurrentTimestamp() {
	using namespace std::chrono;
	auto now = system_clock::now();
	auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
	auto timer = system_clock::to_time_t(now);
	std::tm bt;
#ifdef _WIN32
	localtime_s(&bt, &timer); // Windows 安全版本時間轉換
#else
	localtime_r(&timer, &bt); // Linux 安全版本時間轉換
#endif
	std::ostringstream oss;
	oss << "[" << std::put_time(&bt, "%Y-%m-%d %H:%M:%S")
		<< "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
	return oss.str();
}

// 內部日誌功能：將事件與十六進位數據印至控制台
void TCP_server::printLog(const std::string& tag, socket_t s, const char* data, int len) {
	std::cout << getCurrentTimestamp() << tag;
	if (s != INVALID_SOCKET) std::cout << "[ID:" << s << "] ";
	if (data && len > 0) {
		for (int i = 0; i < len; ++i) {
			printf("%02X ", (unsigned char)data[i]); // 以 HEX 格式印出資料
		}
	}
	std::cout << std::endl;
}

#ifdef _WIN32
// Windows 環境下的建構子：初始化 Winsock
TCP_server::TCP_server() : listen_sock(INVALID_SOCKET), initialized(false), running(false) {
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) initialized = true;
}
TCP_server::~TCP_server() { stop(); if (initialized) WSACleanup(); }
#else
// Linux 環境下的建構子
TCP_server::TCP_server() : listen_sock(INVALID_SOCKET), initialized(true), running(false) {}
TCP_server::~TCP_server() { stop(); }
#endif

// 啟動伺服器：建立 Socket 並開始監聽
bool TCP_server::start(int port, bool debug) {
	if (!initialized) return false;
	debug_mode = debug;

	// 建立 TCP Socket
	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock == INVALID_SOCKET) return false;

	// 設定 SO_REUSEADDR，允許伺服器快速重啟
	int opt = 1;
	setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

	sockaddr_in serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY; // 監聽所有網路介面
	serverAddr.sin_port = htons(port);

	// 綁定 Port
	if (bind(listen_sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		if (debug_mode) printLog("[ERROR] Bind failed (Port " + std::to_string(port) + ")");
#ifdef _WIN32
		closesocket(listen_sock);
#else
		::close(listen_sock);
#endif
		return false;
	}

	// 開始監聽連線
	if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) return false;

	running = true;
	// 啟動負責 accept 新客戶端的執行緒
	accept_thread = std::thread(&TCP_server::acceptLoop, this);

	if (debug_mode) printLog("[INFO] Server listening on port " + std::to_string(port));
	return true;
}

// 監聽連線的迴圈
void TCP_server::acceptLoop() {
	while (running) {
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(listen_sock, &readfds);

		// 使用 select 設定超時，讓執行緒能感應 running 標記的變更
		struct timeval timeout { 0, 500000 }; // 0.5 秒
		int sel = select((int)listen_sock + 1, &readfds, NULL, NULL, &timeout);

		if (sel > 0 && FD_ISSET(listen_sock, &readfds)) {
			sockaddr_in clientAddr;
			socklen_t clientSize = sizeof(clientAddr);
			socket_t clientSock = accept(listen_sock, (sockaddr*)&clientAddr, &clientSize);

			if (clientSock == INVALID_SOCKET) {
#ifdef _WIN32
				int err = WSAGetLastError();
				if (err == WSAEWOULDBLOCK) continue;
#else
				if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
#endif
				continue;
			}

			// 將新 Socket 加入清單，使用 Mutex 保護
			{
				std::lock_guard<std::mutex> lock(clients_mtx);
				clients.push_back(clientSock);
			}

			if (debug_mode) printLog("[NEW CONNECTION]", clientSock);

			// 為新客戶端建立獨立處理執行緒
			std::thread t(&TCP_server::handleClient, this, clientSock);
			t.detach();
		}
	}
}

// 處理個別客戶端的通訊
void TCP_server::handleClient(socket_t clientSock) {
	char buffer[4096];

	// 設定 Socket 接收超時，避免 recv 永遠阻塞
#ifdef _WIN32
	int timeout = 1000; // 1 秒
	setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
	struct timeval tv;
	tv.tv_sec = 1; tv.tv_usec = 0;
	setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

	while (running) {
		int bytesReceived = recv(clientSock, buffer, sizeof(buffer) - 1, 0);

		if (bytesReceived > 0) {
			buffer[bytesReceived] = '\0';

			// 觸發外部回呼函式，傳遞來源 ID 與資料
			if (onReceive) {
				onReceive(clientSock, buffer, bytesReceived);
			}

			if (debug_mode) printLog("[RX]", clientSock, buffer, bytesReceived);
		}
		else if (bytesReceived == 0) {
			// 客戶端正常中斷連線
			if (debug_mode) printLog("[DISCONNECTED]", clientSock);
			break;
		}
		else {
			// 檢查是否為超時，超時則繼續循環
#ifdef _WIN32
			int err = WSAGetLastError();
			if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) continue;
#else
			if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
#endif
			// 其他真正的通訊錯誤
			if (debug_mode) printLog("[ERROR/CLOSED]", clientSock);
			break;
		}
	}

	// 從清單中移除連線
	{
		std::lock_guard<std::mutex> lock(clients_mtx);
		clients.remove(clientSock);
	}
#ifdef _WIN32
	closesocket(clientSock);
#else
	::close(clientSock);
#endif
}

// 傳送資料給指定客戶端
bool TCP_server::sendToClient(socket_t clientSock, const char* buf, int len) {
	int res = send(clientSock, buf, len, 0);
	return res != SOCKET_ERROR;
}

// 向所有連線中的客戶端廣播資料
void TCP_server::broadcast(const char* buf, int len) {
	std::lock_guard<std::mutex> lock(clients_mtx);
	for (auto const& sock : clients) {
		send(sock, buf, len, 0);
	}
}

// 回傳目前連線中的客戶端 ID 向量 (副本以確保執行緒安全)
std::vector<socket_t> TCP_server::getConnectedClients() {
	std::lock_guard<std::mutex> lock(clients_mtx);
	return std::vector<socket_t>(clients.begin(), clients.end());
}

// 停止伺服器：關閉執行緒並清空所有連線
void TCP_server::stop() {
	if (!running) return;
	running = false;

	// 等待監聽執行緒結束
	if (accept_thread.joinable()) accept_thread.join();

	// 關閉所有客戶端 Socket
	std::lock_guard<std::mutex> lock(clients_mtx);
	for (auto const& sock : clients) {
#ifdef _WIN32
		closesocket(sock);
#else
		::close(sock);
#endif
	}
	clients.clear();

	// 關閉伺服器監聽 Socket
	if (listen_sock != INVALID_SOCKET) {
#ifdef _WIN32
		closesocket(listen_sock);
#else
		::close(listen_sock);
#endif
		listen_sock = INVALID_SOCKET;
	}
}