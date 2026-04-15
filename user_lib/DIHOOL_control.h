#pragma once
#include <string>
#include <vector>
#include <cstdint>

class TCP_client;

class DIHOOL_control
{
public:
	DIHOOL_control();
	~DIHOOL_control();

	bool init(const std::string& ip, int port, int id, int total_motor, bool debug = false);
	bool init(TCP_client& extClient, int id, int total_motor, bool debug = false);

	// 基本動作
	bool stop();
	bool moveUp();
	bool moveDown();
	bool resetMotor();

	// 單一電機位置 (1~4)
	bool setMotorPosition(int motorIndex, uint8_t mm);

	// 全部電機位置 (依 total_motor)
	bool setMotorPosition_all(uint8_t mm);

	// 修改設備編號
	bool setDeviceID(uint8_t newID);

private:
	TCP_client* _client;
	bool _ownedClient;
	bool debug_mode;
	int _id;           // RS485 現在的 ID
	int _total_motor;  // 總電機數 (1~4)
	std::string _log_tag;

	bool sendCommand(uint8_t regLow, uint8_t dataLow);
	bool sendRawPacket(const std::vector<uint8_t>& packet);

	uint16_t crc16_modbus(const std::vector<uint8_t>& buf);
};
