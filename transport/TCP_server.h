#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <list>
#include <vector>
#include <functional>

/**
 * ============================================================================
 * [TCP_server 使用範例 / Usage Example]
 * ============================================================================
 * * int main() {
 * TCP_server server;
 *
 * // 1. 設定收到資料的回呼函式 (使用 Lambda 可以存取 server 物件)
 * server.setReceiveCallback([&](socket_t id, const char* data, int len) {
 * printf("收到來自 ID %d 的資料: %s\n", (int)id, data);
 * * // 收到 PING 回覆 PONG
 * if (std::string(data).find("PING") != std::string::npos) {
 * server.sendToClient(id, "PONG\n", 5);
 * }
 * });
 *
 * // 2. 啟動伺服器 (監聽 Port 4001, 開啟 Debug 日誌)
 * if (server.start(4001, true)) {
 * while (server.isRunning()) {
 * // 3. 隨時取得連線中的客戶端清單
 * auto clients = server.getConnectedClients();
 * // 執行廣播範例
 * server.broadcast("Hello Everyone!", 15);
 * std::this_thread::sleep_for(std::chrono::seconds(10));
 * }
 * }
 * return 0;
 * }
 * ============================================================================
 */

 // ---------------- Windows/Linux 平台相容處理 ----------------
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#else
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

// 定義回呼函式型別：接收端 Socket ID, 資料指標, 資料長度
using DataCallback = std::function<void(socket_t, const char*, int)>;

class TCP_server {
public:
	TCP_server();
	~TCP_server();

	// 啟動伺服器：port=連接埠, debug=是否印出TX/RX日誌
	bool start(int port, bool debug = false);

	// 停止伺服器並中斷所有連線
	void stop();

	// 功能介面
	// 對特定客戶端 ID 發送資料
	bool sendToClient(socket_t clientSock, const char* buf, int len);

	// 對所有已連線的客戶端發送資料
	void broadcast(const char* buf, int len);

	// 取得目前伺服器是否正在運作
	bool isRunning() { return running; }

	// --- 核心擴充功能 ---
	// 取得目前所有連線的客戶端 Socket ID 清單 (執行緒安全)
	std::vector<socket_t> getConnectedClients();

	// 設定資料接收的回呼處理
	void setReceiveCallback(DataCallback cb) { onReceive = cb; }

private:
	socket_t listen_sock;
	bool initialized = false;
	bool debug_mode = false;
	std::atomic<bool> running;

	std::thread accept_thread;
	std::mutex clients_mtx;
	std::list<socket_t> clients;

	DataCallback onReceive = nullptr;
	std::string _log_tag;

	void acceptLoop();
	void handleClient(socket_t clientSock);
};

#endif