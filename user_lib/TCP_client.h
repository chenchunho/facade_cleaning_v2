#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

// ---------------- Windows ----------------
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#else
// ---------------- Linux / Raspberry Pi ----------------
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
typedef int socket_t;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#endif

class TCP_client {
public:
	TCP_client();
	~TCP_client();

	bool connectToServer(const std::string& ip, int port, bool debug = false);
	bool sendData(const char* buf, int len, int timeout_ms);
	int receiveData(char* buf, int bufSize, int timeout_ms);
	int available();
	void close();
	bool isConnected() { return connected; }

private:
	socket_t sock;
	bool initialized = false;
	bool debug_mode = false;
	std::atomic<bool> connected;

	std::string last_ip;
	int last_port;
	std::thread monitor_thread;
	std::atomic<bool> monitor_running;
	std::mutex socket_mtx;

	// 輔助工具
	void printLog(const std::string& tag, const char* data = nullptr, int len = 0);
	std::string getCurrentTimestamp();
	void startMonitor();
	void reconnectLoop();
};

#endif