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

	// [2026-08-28] Same atomic drain→send→recv transaction as sendAndReceive(),
	// but the recv side LOOPS and accumulates until the reply goes quiet, instead
	// of taking whatever one recv() call happens to return.
	//
	// Why this exists — the USR-TCP232 gateway decides when to forward RS485
	// bytes onto TCP based on the inter-character gap (~3.5 character times):
	//     9600 baud  -> ~3.6ms : an 8-byte Modbus frame is long complete before
	//                            the gateway packs it -> always arrives in one
	//                            recv() -> plain sendAndReceive() is fine
	//   115200 baud  -> ~0.3ms : the gateway can pack and forward after the first
	//                            few bytes, so the SAME 8-byte frame arrives as
	//                            TWO TCP segments -> a single recv() returns a
	//                            truncated frame -> CRC/echo check fails
	// QX-DO24 is the only 115200 device in this project; everything else
	// (JC-100 / SD76 / SE3 / DM2J / ZDT / PQW) is 9600. That is exactly why
	// 05a3c7e's "8 byte frames arrive in one piece in practice" assumption held
	// for those and broke PWM: the old QX_DO24 code had its own accumulate loop,
	// and switching it to the atomic (single-recv) API silently removed that.
	//
	// Quiet-period termination rather than an expected-length argument: a length
	// would force this class to parse Modbus function codes, and error frames are
	// SHORTER than success replies (6 bytes) so it would also need that special
	// case or it would burn the full timeout on every rejected command.
	//
	// Cost: every call pays `quiet_ms` after the last byte. Use only where reply
	// fragmentation is actually possible; leave 9600 devices on sendAndReceive().
	//
	// Returns total accumulated byte count (>0), 0 on send failure / nothing
	// received, -1 on disconnect.
	int sendAndReceiveQuiet(const char* tx_buf, int tx_len,
	                        char* rx_buf, int rx_size,
	                        int send_timeout_ms, int total_timeout_ms,
	                        int quiet_ms);

	// 🔴 [2026-09-01] 送出前排空接收緩衝區。**與 sendAndReceive() 內部用的是同一段邏輯**
	// （非阻塞讀到空為止，上限 4096），抽出來給「接收端有自己組幀邏輯、無法改用
	// sendAndReceive 的 driver」在交易開頭呼叫。
	//
	// 為什麼需要：ZDT / DM2J / PQW 走的是 sendData() + 自己的 readEcho()/recv_frame_()，
	// **沒有送出前排空**。只要有一筆回覆遲到，緩衝區就永遠落後一筆 —— 之後每次讀到的
	// 都是上一筆交易的回覆 → 校驗失敗 → **永久失步，只能重連才能恢復**。
	// 2026-09-01 DSZL_107 因為完全相同的缺陷一天內卡死四次
	//（證據：`stale/foreign reply txid=3510 want=3511`，正好差一筆；socket 仍 ESTAB
	// 但 Recv-Q 卡著一筆完整回覆）。DSZL 已改用 sendAndReceive；這三支因為接收端
	// 有組幀邏輯不能照搬，改為在交易開頭顯式排空。
	//
	// ⚠️ **刻意不放進 sendData() 內部**：crane_cli_ / arm_cli_ 也用 sendData()，
	// 那些是文字協定連線，排空會把吊機主動推送的 EVT 廣播吃掉。
	//
	// ⚠️ 這不是原子交易（drain 與 send 之間可能被插入）。呼叫端若有並行需求，
	// 仍應由上層的 bus mutex 序列化 —— 現況本來就是如此。
	// 回傳排空的位元組數（0 = 本來就是乾淨的）。
	int drainRx();

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
	// design for instances you DO want it for. Does not affect reconnect
	// behavior itself, only the logging. (The original example instance,
	// depth_cam, was removed 2026-09-01; arm_cli_ is the live user.)
	void set_quiet_reconnect_log(bool quiet) { quiet_reconnect_log_ = quiet; }

private:
	socket_t sock;
	bool initialized = false;
	bool debug_mode = false;
	bool quiet_reconnect_log_ = false;
	// [2026-08-31] 重連日誌限流用的狀態。見 reconnectLoop() 的說明。
	// 只由 reconnectLoop 這一條執行緒讀寫，不需要同步。
	int     reconn_fail_streak_   = 0;   // 目前這串連續失敗已經幾次（成功即歸零）
	int64_t reconn_down_since_ms_ = 0;   // 這串失敗的起點（單調時鐘）
	int64_t reconn_last_log_ms_   = 0;   // 上次印摘要的時間
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