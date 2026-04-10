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
	void controlAll(bool status);

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
	bool debug_mode = true;
	uint8_t slave_id = 0x01;

	uint16_t modbusCRC(const uint8_t* data, int len);

	std::vector<uint8_t> buildSingleRelayCmd(int relay_num, bool status);
	std::vector<uint8_t> buildAllRelayCmd(bool status);
	std::vector<uint8_t> buildReadCmd();

	std::vector<bool> parseReadResponse(const std::vector<uint8_t>& resp);

	std::vector<uint8_t> readEcho();
	void printHex(const std::vector<uint8_t>& data, const std::string& tag);

	bool checkAllStatus(bool target);
};

#endif
