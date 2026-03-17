#include "DIHOOL_control.h"
#include "TCP_client.h"
#include <iostream>
#include <cstdio>
#include <unistd.h>

DIHOOL_control::DIHOOL_control()
	: _client(nullptr), _ownedClient(false), _debug(false), _id(1), _total_motor(1)
{
}

DIHOOL_control::~DIHOOL_control()
{
	if (_ownedClient && _client)
	{
		delete _client;
		_client = nullptr;
	}
}

bool DIHOOL_control::init(const std::string& ip, int port, int id, int total_motor, bool debug)
{
	_debug = debug;
	_id = id;
	_total_motor = total_motor;
	_ownedClient = true;

	_client = new TCP_client();
	if (!_client->connectToServer(ip, port))
	{
		if (_debug) std::cerr << "[DIHOOL] TCP connect failed\n";
		delete _client;
		_client = nullptr;
		return false;
	}

	return true;
}

bool DIHOOL_control::init(TCP_client& extClient, int id, int total_motor, bool debug)
{
	_debug = debug;
	_id = id;
	_total_motor = total_motor;
	_ownedClient = false;
	_client = &extClient;

	return true;
}

//================================================================
// 動作控制
//================================================================

bool DIHOOL_control::stop() { return sendCommand(0x01, 0x01); }
bool DIHOOL_control::moveUp() { return sendCommand(0x01, 0x02); }
bool DIHOOL_control::moveDown() { return sendCommand(0x01, 0x04); }
bool DIHOOL_control::resetMotor() { return sendCommand(0x01, 0x08); }

//================================================================
// 設定單一馬達位置
//================================================================

bool DIHOOL_control::setMotorPosition(int motorIndex, uint8_t mm)
{
	if (motorIndex < 1 || motorIndex > 4)
		return false;

	uint8_t regLow = 0x02 + (motorIndex - 1);
	return sendCommand(regLow, mm);
}

//================================================================
// ⭐ 設定全部馬達位置（依 total_motor）
//================================================================

bool DIHOOL_control::setMotorPosition_all(uint8_t mm)
{
	if (_total_motor < 1 || _total_motor > 4)
		return false;

	bool ok = true;

	for (int i = 1; i <= _total_motor; i++)
	{
		uint8_t regLow = 0x02 + (i - 1);  // 02, 03, 04, 05

		// === 建立封包 ===
		std::vector<uint8_t> packet;

		packet.push_back((uint8_t)_id);
		packet.push_back(0x06);
		packet.push_back(0x00);
		packet.push_back(regLow);
		packet.push_back(0x00);
		packet.push_back(mm);

		uint16_t crc = crc16_modbus(packet);
		packet.push_back(crc & 0xFF);
		packet.push_back(crc >> 8);

		// === Debug: TX ===
		if (_debug)
		{
			printf("[TX][ALL M%d] ", i);
			for (auto b : packet) printf("%02X ", b);
			printf("\n");
		}

		// === 實際送資料 ===
		if (!_client->sendData((const char*)packet.data(), packet.size(), 100))
		{
			ok = false;
		}

		// === Debug: RX ===
		if (_debug)
		{
			char buf[64];
			int len = _client->receiveData(buf, sizeof(buf), 200);

			if (len > 0)
			{
				printf("[RX][ALL M%d] ", i);
				for (int j = 0; j < len; j++)
					printf("%02X ", (uint8_t)buf[j]);
				printf("\n");
			}
			else
			{
				printf("[RX][ALL M%d] <no reply>\n", i);
			}
		}

		// 避免 RS485 gateway 堵塞
		usleep(1000);
	}

	return ok;
}


//================================================================
// 修改設備 ID
//================================================================

bool DIHOOL_control::setDeviceID(uint8_t newID)
{
	std::vector<uint8_t> packet;

	packet.push_back(0x00);    // 廣播
	packet.push_back(0x06);
	packet.push_back(0x00);
	packet.push_back(0x0A);
	packet.push_back(0x00);
	packet.push_back(newID);

	uint16_t crc = crc16_modbus(packet);
	packet.push_back(crc & 0xFF);
	packet.push_back(crc >> 8);

	if (!sendRawPacket(packet))
		return false;

	if (_debug)
	{
		char buf[64];
		int len = _client->receiveData(buf, sizeof(buf), 200);
		if (len > 0)
		{
			printf("[RX] ");
			for (int i = 0; i < len; i++)
				printf("%02X ", (uint8_t)buf[i]);
			printf("\n");
		}
		else printf("[RX] <no reply>\n");
	}

	_id = newID;
	return true;
}

//================================================================
// 發送 DIHOOL 指令
//================================================================

bool DIHOOL_control::sendCommand(uint8_t regLow, uint8_t dataLow)
{
	std::vector<uint8_t> packet;

	packet.push_back((uint8_t)_id);
	packet.push_back(0x06);
	packet.push_back(0x00);
	packet.push_back(regLow);
	packet.push_back(0x00);
	packet.push_back(dataLow);

	uint16_t crc = crc16_modbus(packet);
	packet.push_back(crc & 0xFF);
	packet.push_back(crc >> 8);

	return sendRawPacket(packet);
}

//================================================================
// 發送 Raw Packet
//================================================================

bool DIHOOL_control::sendRawPacket(const std::vector<uint8_t>& packet)
{
	if (!_client)
		return false;

	if (_debug)
	{
		printf("[TX] ");
		for (auto b : packet) printf("%02X ", b);
		printf("\n");
	}

	return _client->sendData((const char*)packet.data(), packet.size(), 100);
}

//================================================================
// CRC16-MODBUS
//================================================================

uint16_t DIHOOL_control::crc16_modbus(const std::vector<uint8_t>& buf)
{
	uint16_t crc = 0xFFFF;

	for (uint8_t b : buf)
	{
		crc ^= b;
		for (int i = 0; i < 8; i++)
		{
			if (crc & 1)
				crc = (crc >> 1) ^ 0xA001;
			else
				crc >>= 1;
		}
	}
	return crc;
}
