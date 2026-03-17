#ifndef ZS_DIO_R_RLY_H
#define ZS_DIO_R_RLY_H

#include "TCP_client.h"
#include <vector>
#include <string>

class ZS_DIO_R_RLY {
public:
	ZS_DIO_R_RLY();
	~ZS_DIO_R_RLY();

	// 內部 client 初始化
	bool init(const std::string& ip, int port, int total_relay = 16, bool debug = false);

	// 外部 client 初始化
	bool init(TCP_client& extClient, int total_relay = 16, bool debug = false);

	// 單顆繼電器控制
	void controlRelay(int id, bool status);

	// 全部 ON/OFF
	void controlAll(bool status);

	// 關閉（僅關閉 internal client）
	void close();

private:
	TCP_client internal_client;        // 內部 client
	TCP_client* ext_client = nullptr;  // 外部帶入 client
	bool use_external_client = false;
	bool debugEnabled = false;

	int relay_count = 16;

	// 自動生成的指令集
	std::vector<std::vector<uint8_t>> relay_on_cmds;
	std::vector<std::vector<uint8_t>> relay_off_cmds;

	// ALL ON/OFF
	std::vector<uint8_t> relay_all_on;
	std::vector<uint8_t> relay_all_off;

	// CRC + 封包生成
	uint16_t crc16_modbus(const uint8_t* data, size_t len);
	std::vector<uint8_t> buildRelayCommand(uint16_t regAddr, uint16_t value);

	// 自動選 client
	inline TCP_client& client() {
		return use_external_client ? *ext_client : internal_client;
	}
};

#endif // ZS_DIO_R_RLY_H
