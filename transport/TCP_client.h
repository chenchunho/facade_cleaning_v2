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

	// Atomic Modbus-style transaction. Holds the internal socket mutex from
	// drain → send → recv so a concurrent caller on the same TCP_client cannot
	// interleave its own send between our send and recv (which would corrupt
	// both replies in the kernel TCP buffer).
	//
	// Required when multiple threads share one TCP_client for distinct Modbus
	// devices on the same RS485 gateway — e.g. Crane_control_PI's cli_A serves
	// SE3 left + SD76 left + SD76 middle + CLV900, accessed from cmd_hold,
	// motion_rope and meter_loop threads. Pre-existing send/recv pair pattern
	// was racy because the mutex was released between the two calls.
	//
	// Returns received byte count on success (>0), 0 on send failure,
	// -1 on disconnect or recv timeout.
	int sendAndReceive(const char* tx_buf, int tx_len,
	                   char* rx_buf, int rx_size,
	                   int send_timeout_ms, int recv_timeout_ms);

	int available();
	void close();
	bool isConnected() { return connected; }

	// [2026-07-23 per user] reconnectLoop's "reconnecting.../reconnect success"
	// lines are intentionally unconditional (bypass debug_mode — see .cpp
	// comment) so real connection drops are always visible. But that means an
	// endpoint that's KNOWN not to be installed/running yet (e.g. the cleaning
	// arm's motor_api before the arm is physically mounted) spams that log
	// every ~500ms with nothing actionable in it. This mutes just those three
	// lines for a specific instance without touching the unconditional-log
	// design for instances you DO want it for (e.g. depth_cam during active
	// testing). Does not affect reconnect behavior itself, only the logging.
	void set_quiet_reconnect_log(bool quiet) { quiet_reconnect_log_ = quiet; }

private:
	socket_t sock;
	bool initialized = false;
	bool debug_mode = false;
	bool quiet_reconnect_log_ = false;
	std::atomic<bool> connected;

	std::string last_ip;
	int last_port;
	std::string _log_tag;
	std::thread monitor_thread;
	std::atomic<bool> monitor_running;
	std::mutex socket_mtx;

	void startMonitor();
	void reconnectLoop();
};

#endif