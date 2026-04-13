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

	// Read status register (0x0000): high byte=work mode, low byte=alarm status
	bool readStatus(uint8_t& workMode, uint8_t& alarmStatus);

	// Read integer display values (signed int32, alternative to BCD)
	bool readUpperInteger(int32_t& value);   // 0x0021~0x0022
	bool readLowerInteger(int32_t& value);   // 0x0023~0x0024

	// Control
	bool resetAll();         // Write 0x0003 to reg 0x0000
	bool pauseMeter();       // Write 0x0004 to reg 0x0000
	bool resumeMeter();      // Write 0x0008 to reg 0x0000

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
