#ifndef PQW_IO_16O_RLY_H
#define PQW_IO_16O_RLY_H

#include "TCP_client.h"
#include <vector>
#include <string>
#include <cstdint>

/**********************************************************************
 *  PQW_IO_16O_RLY 使用說明（Modbus-TCP 16 乾接點繼電器模組）
 *  ---------------------------------------------------------------
 *
 *  ● 本類專門用來控制 16 組 Relay 的遠端 I/O 模組
 *  ● 支援 Modbus-TCP 0x05、0x06、0x01 讀寫
 *  ● 內建 Retry / Echo / Status double-check，保證繼電器確實切換
 *
 * ==================================================================
 *  一、初始化方式（任選一種）
 * ==================================================================
 *
 *  方式 1：使用本類別自行建立 TCP 連線
 *  ---------------------------------------------------------------
 *      PQW_IO_16O_RLY rly;
 *      rly.init("192.168.1.50", 502, 16, true);
 *
 *      // 若成功，之後可直接控制
 *      rly.controlRelay(1, true);     // 開 Relay 1
 *      rly.controlAll(false);         // 全部關閉
 *
 *
 *  方式 2：使用外部已經建立好的 TCP_client（不重新 connect）
 *  ---------------------------------------------------------------
 *      TCP_client cli;
 *      cli.connectToServer("192.168.1.50", 502);
 *
 *      PQW_IO_16O_RLY rly;
 *      rly.init(cli, 16, true);       // 使用外部 TCP
 *
 *  ★ 適用於：要用同一條 TCP 連線控制多個設備
 *
 *
 * ==================================================================
 *  二、控制單顆 Relay
 * ==================================================================
 *
 *      rly.controlRelay(1, true);     // Relay 1 ON
 *      rly.controlRelay(1, false);    // Relay 1 OFF
 *
 *  內部機制：
 *      1. 發送 0x05 單點指令
 *      2. 接收 Echo（確認寫入）
 *      3. 送 0x01 讀回狀態
 *      4. 若不符 → Retry（最多 5 次）
 *
 *
 * ==================================================================
 *  三、控制全部 Relay（全開 / 全關）
 * ==================================================================
 *
 *      rly.controlAll(true);          // 全部 ON
 *      rly.controlAll(false);         // 全部 OFF
 *
 *
 * ==================================================================
 *  四、讀取所有 Relay 狀態
 * ==================================================================
 *
 *      std::vector<bool> st = rly.readAllStatus();
 *      for (int i = 0; i < st.size(); i++)
 *          printf("Relay %d = %d\n", i+1, st[i]);
 *
 *
 **********************************************************************/

class PQW_IO_16O_RLY {
public:
	PQW_IO_16O_RLY();
	~PQW_IO_16O_RLY();

	// 初始化 方式 A（本類建立 TCP 連線）
	bool init(const std::string& ip, int port, int ID, int total_relay = 16, bool debug = false);

	// 初始化 方式 B（使用外部建立好的 TCP_client）
	bool init(TCP_client& extClient, int ID, int total_relay = 16, bool debug = false);

	//==========================================================
	// 控制單顆 Relay（含 Retry + Echo + 狀態讀回）
	//==========================================================
	bool controlRelay(int id, bool status);

	//==========================================================
	// 全部控制（含 Retry）
	//==========================================================
	bool controlAll(bool status);

	//==========================================================
	// 讀全部 Relay 狀態（0x01）
	//==========================================================
	std::vector<bool> readAllStatus();

	// 關閉 TCP 連線
	void close();

private:
	TCP_client *client;
	bool owns_client = false;

	int relay_count = 16;
	bool debug_mode = false;
	uint8_t slave_id = 0x01;
	std::string _log_tag;

	uint16_t modbusCRC(const uint8_t* data, int len);

	std::vector<uint8_t> buildSingleRelayCmd(int relay_num, bool status);
	std::vector<uint8_t> buildAllRelayCmd(bool status);
	std::vector<uint8_t> buildReadCmd();

	std::vector<bool> parseReadResponse(const std::vector<uint8_t>& resp);

	// 🔴 [2026-09-01] 原子交易：drain → send → recv 全程握住 TCP_client 的
	// socket_mtx，取代原本的「drainRx() + sendData() + readEcho()」三段式。
	//
	// 為什麼非改不可：本模組（slave 12）與 ZDT 推桿 5~8、DM2J 上滑台 14 共用
	// `.20` 這條匯流排，而三段式的鎖在 send 與 recv 之間是放開的 → 別條執行緒
	// 可以把自己的交易插進來，兩邊的回覆在核心緩衝區裡錯位。08-28 補的
	// `drainRx()` 只清得掉已經躺在緩衝區的位元組，抓不到「在 recv 窗口內才抵達」
	// 的遲到回覆 —— 那一筆會被下一次交易讀走，從此永久落後一筆。
	//
	// 附帶效益：納入 TCP_client 的「連續 10 次接收逾時 → 主動斷線」守衛
	//（只掛在原子交易 API 上）。該守衛的計數每條連線共用但成功一次就歸零。
	//
	// ⚠️ 回覆**不做驗證**，維持原本 readEcho() 的語意：PQW 韌體的 echo 格式非標準
	// （TX `... 05 ...` 會被回成 RX `... 00 ...`），歷史上拿它做驗證造成過
	// step_down 中途卡死（見 controlRelay() 的註解）。這裡只負責把 bytes 取回來。
	// 逾時沿用各站點原本的數字（recv 一律 200，send 由呼叫端指定）。
	// 回傳空 vector = 送出失敗或無回覆。
	std::vector<uint8_t> txn(const std::vector<uint8_t>& cmd, int send_timeout_ms);
	void printHex(const std::vector<uint8_t>& data, const std::string& tag);

	bool checkAllStatus(bool target);
};

#endif
