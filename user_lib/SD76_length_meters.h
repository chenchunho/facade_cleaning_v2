#pragma once
#include <stdint.h>
#include <string>
#include "TCP_client.h"

class SD76_length_meters {
public:
	SD76_length_meters();
	~SD76_length_meters();

	bool init(const std::string& ip, int port, int id, bool debug = false);
	bool init(TCP_client& extClient, int id, bool debug = false);

	// 單讀上排（0x0001~0x0002）
	bool readUpperDisplayValue(int& value);

	// 讀取上下兩排（0x0001~0x0004）
	bool readUpperLowerDisplayValue(int& upper, int& lower);

	// 清零（相當於復位）
	bool resetAll();

private:
	bool readRegister(uint16_t addr, uint16_t count, uint8_t* raw);
	bool sendModbus(const uint8_t* req, int reqLen, uint8_t* resp, int& respLen);
	bool writeSingleRegister(uint16_t addr, uint16_t value);
	uint16_t crc16(const uint8_t* buf, int len);

	int decodeSignedBCD6(const uint8_t raw[4]);   // 解析 6 碼 BCD + 負號位

private:
	TCP_client* client;
	bool owns;
	int deviceID;
	bool debugPrint;
};
