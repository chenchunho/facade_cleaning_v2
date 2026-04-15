#include "TCP_server.h"
#include "log_utils.h"
#include <cstring>

//=========== init ===========

#ifdef _WIN32
TCP_server::TCP_server() : listen_sock(INVALID_SOCKET), initialized(false), running(false) {
	_log_tag = "TCPSVR";
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) initialized = true;
}
TCP_server::~TCP_server() { stop(); if (initialized) WSACleanup(); }
#else
TCP_server::TCP_server() : listen_sock(INVALID_SOCKET), initialized(true), running(false) {
	_log_tag = "TCPSVR";
}
TCP_server::~TCP_server() { stop(); }
#endif

bool TCP_server::start(int port, bool debug) {
	if (!initialized) return false;
	debug_mode = debug;
	_log_tag = "TCPSVR:" + std::to_string(port);

	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock == INVALID_SOCKET) return false;

	// SO_REUSEADDR allows fast server restart
	int opt = 1;
	setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

	sockaddr_in serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(port);

	// bind port
	if (bind(listen_sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		LOG_ERR(_log_tag, "Bind failed (Port %d)", port);
#ifdef _WIN32
		closesocket(listen_sock);
#else
		::close(listen_sock);
#endif
		return false;
	}

	// start listening
	if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) return false;

	running = true;
	// start accept thread
	accept_thread = std::thread(&TCP_server::acceptLoop, this);

	LOG_INF(_log_tag, "Server listening on port %d", port);
	return true;
}

//=========== worker thread: accept loop ===========

void TCP_server::acceptLoop() {
	while (running) {
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(listen_sock, &readfds);

		// use select with timeout so the thread can notice running=false
		struct timeval timeout { 0, 500000 }; // 0.5 sec
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

			// add new socket to clients list under mutex
			{
				std::lock_guard<std::mutex> lock(clients_mtx);
				clients.push_back(clientSock);
			}

			LOG_INF(_log_tag, "NEW CONNECTION [ID:%d]", (int)clientSock);

			// spawn dedicated handler thread
			std::thread t(&TCP_server::handleClient, this, clientSock);
			t.detach();
		}
	}
}

//=========== worker thread: handle client ===========

void TCP_server::handleClient(socket_t clientSock) {
	char buffer[4096];

	// set recv timeout so recv doesn't hang forever
#ifdef _WIN32
	int timeout = 1000; // 1 sec
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

			// trigger external callback
			if (onReceive) {
				onReceive(clientSock, buffer, bytesReceived);
			}

			if (debug_mode) {
				char note[32];
				std::snprintf(note, sizeof(note), "RX ID:%d", (int)clientSock);
				LOG_HEX(_log_tag, note, buffer, bytesReceived);
			}
		}
		else if (bytesReceived == 0) {
			// client normally closed connection
			LOG_INF(_log_tag, "DISCONNECTED [ID:%d]", (int)clientSock);
			break;
		}
		else {
			// check for timeout, continue loop if so
#ifdef _WIN32
			int err = WSAGetLastError();
			if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) continue;
#else
			if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
#endif
			// real communication error
			LOG_ERR(_log_tag, "ERROR/CLOSED [ID:%d]", (int)clientSock);
			break;
		}
	}

	// remove from clients list
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

//=========== utility: send/broadcast ===========

bool TCP_server::sendToClient(socket_t clientSock, const char* buf, int len) {
	int res = send(clientSock, buf, len, 0);
	return res != SOCKET_ERROR;
}

void TCP_server::broadcast(const char* buf, int len) {
	std::lock_guard<std::mutex> lock(clients_mtx);
	for (auto const& sock : clients) {
		send(sock, buf, len, 0);
	}
}

std::vector<socket_t> TCP_server::getConnectedClients() {
	std::lock_guard<std::mutex> lock(clients_mtx);
	return std::vector<socket_t>(clients.begin(), clients.end());
}

//=========== utility: stop ===========

void TCP_server::stop() {
	if (!running) return;
	running = false;

	// wait for accept thread to exit
	if (accept_thread.joinable()) accept_thread.join();

	// close all client sockets
	std::lock_guard<std::mutex> lock(clients_mtx);
	for (auto const& sock : clients) {
#ifdef _WIN32
		closesocket(sock);
#else
		::close(sock);
#endif
	}
	clients.clear();

	// close listening socket
	if (listen_sock != INVALID_SOCKET) {
#ifdef _WIN32
		closesocket(listen_sock);
#else
		::close(listen_sock);
#endif
		listen_sock = INVALID_SOCKET;
	}
}
