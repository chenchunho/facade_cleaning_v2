#include "SD76_length_meters.h"
#include <stdio.h>
#include <string.h>

SD76_length_meters::SD76_length_meters()
{
	client = nullptr;
	owns = false;
	deviceID = 1;
	debugPrint = false;
}

SD76_length_meters::~SD76_length_meters()
{
	if (owns && client)
		delete client;
}

bool SD76_length_meters::init(const std::string& ip, int port, int id, bool debug)
{
	client = new TCP_client();
	owns = true;

	if (!client->connectToServer(ip, port))
		return true;

	deviceID = id;
	debugPrint = debug;
	return false;
}

bool SD76_length_meters::init(TCP_client& extClient, int id, bool debug)
{
	client = &extClient;
	owns = false;
	deviceID = id;
	debugPrint = debug;
	return false;
}

//
// CRC
//
uint16_t SD76_length_meters::crc16(const uint8_t* buf, int len)
{
	uint16_t crc = 0xFFFF;

	for (int i = 0; i < len; i++) {
		crc ^= buf[i];
		for (int j = 0; j < 8; j++)
			crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
	}
	return crc;
}

//
// TX/RX
//
bool SD76_length_meters::sendModbus(const uint8_t* req, int reqLen,
	uint8_t* resp, int& respLen)
{
	if (debugPrint) {
		printf("[TX] ");
		for (int i = 0; i < reqLen; i++) printf("%02X ", req[i]);
		printf("\n");
	}

	if (!client->sendData((const char*)req, reqLen, 200))
		return true;

	respLen = client->receiveData((char*)resp, 256, 300);

	if (debugPrint && respLen > 0) {
		printf("[RX] ");
		for (int i = 0; i < respLen; i++) printf("%02X ", resp[i]);
		printf("\n");
	}

	return respLen <= 0;
}

//
// Read register
//
bool SD76_length_meters::readRegister(uint16_t addr, uint16_t count, uint8_t* raw)
{
	uint8_t req[8];

	req[0] = deviceID;
	req[1] = 0x03;
	req[2] = addr >> 8;
	req[3] = addr & 0xFF;
	req[4] = count >> 8;
	req[5] = count & 0xFF;

	uint16_t c = crc16(req, 6);
	req[6] = c & 0xFF;
	req[7] = c >> 8;

	uint8_t resp[256];
	int respLen = 0;

	if (sendModbus(req, 8, resp, respLen))
		return true;

	if (respLen < 5) return true;
	if (resp[1] != 0x03) return true;

	int byteCount = resp[2];
	memcpy(raw, &resp[3], byteCount);

	if (debugPrint) {
		printf("[REG 0x%04X] ", addr);
		for (int i = 0; i < byteCount; i++) printf("%02X ", raw[i]);
		printf("\n");
	}

	return false;
}

//
// 6-digit BCD decode + sign (your meter format)
// raw[] = S b1 b2 b3
// S = sign bit7, b1,b2,b3 = BCD bytes
//
int SD76_length_meters::decodeSignedBCD6(const uint8_t raw[4])
{
	bool neg = (raw[0] & 0x80) != 0;

	auto bcd2 = [](uint8_t b) {
		return ((b >> 4) & 0x0F) * 10 + (b & 0x0F);
	};

	int absVal =
		bcd2(raw[1]) * 10000 +
		bcd2(raw[2]) * 100 +
		bcd2(raw[3]);

	return neg ? -absVal : absVal;
}

//
// 單讀上排（0x0001~0x0002）
//
bool SD76_length_meters::readUpperDisplayValue(int& value)
{
	uint8_t raw[4];

	if (readRegister(0x0001, 2, raw))
		return true;

	value = decodeSignedBCD6(raw);
	return false;
}

//
// 讀取上下兩排（0x0001~0x0004）
//
bool SD76_length_meters::readUpperLowerDisplayValue(int& upper, int& lower)
{
	uint8_t raw[8];

	if (readRegister(0x0001, 4, raw))
		return true;

	upper = decodeSignedBCD6(&raw[0]);
	lower = decodeSignedBCD6(&raw[4]);

	return false;
}

//
// 控制：清零 (相當於復位) = 寫 0x0000 = 0x0003
//
bool SD76_length_meters::resetAll()
{
	return writeSingleRegister(0x0000, 0x0003);
}

//
// Modbus 06 寫單一寄存器
//
bool SD76_length_meters::writeSingleRegister(uint16_t addr, uint16_t value)
{
	uint8_t req[8];

	req[0] = deviceID;
	req[1] = 0x06;
	req[2] = addr >> 8;
	req[3] = addr & 0xFF;
	req[4] = value >> 8;
	req[5] = value & 0xFF;

	uint16_t c = crc16(req, 6);
	req[6] = c & 0xFF;
	req[7] = c >> 8;

	uint8_t resp[256];
	int respLen = 0;

	return sendModbus(req, 8, resp, respLen);
}
