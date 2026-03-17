#include "ZS_DIO_R_RLY.h"
#include <iostream>
#include <thread>
#include <chrono>

//---------------------------------------------------------------
// CRC16 (Modbus RTU) 計算
//---------------------------------------------------------------
uint16_t ZS_DIO_R_RLY::crc16_modbus(const uint8_t* data, size_t len)
{
	uint16_t crc = 0xFFFF;

	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int j = 0; j < 8; j++) {
			if (crc & 0x0001)
				crc = (crc >> 1) ^ 0xA001;
			else
				crc >>= 1;
		}
	}
	return crc;
}

//---------------------------------------------------------------
// 依地址 + 數值產生 Write Single Register (0x06) 封包
//---------------------------------------------------------------
std::vector<uint8_t> ZS_DIO_R_RLY::buildRelayCommand(uint16_t regAddr, uint16_t value)
{
	std::vector<uint8_t> cmd(8);

	cmd[0] = 0x01;                       // Slave ID (固定 1)
	cmd[1] = 0x06;                       // Function code = 0x06 (Write Single Register)
	cmd[2] = (regAddr >> 8) & 0xFF;      // Register High
	cmd[3] = (regAddr >> 0) & 0xFF;      // Register Low
	cmd[4] = (value >> 8) & 0xFF;      // Data High
	cmd[5] = (value >> 0) & 0xFF;      // Data Low

	uint16_t crc = crc16_modbus(cmd.data(), 6);
	cmd[6] = crc & 0xFF;                 // CRC Low
	cmd[7] = (crc >> 8) & 0xFF;          // CRC High

	return cmd;
}

//---------------------------------------------------------------
// Constructor
//---------------------------------------------------------------
ZS_DIO_R_RLY::ZS_DIO_R_RLY()
{
	// 不再初始化任何硬寫死封包
}

ZS_DIO_R_RLY::~ZS_DIO_R_RLY()
{
	if (!use_external_client)
		close();
}

//---------------------------------------------------------------
// 初始化（內部 TCP client）
//---------------------------------------------------------------
bool ZS_DIO_R_RLY::init(const std::string& ip, int port, int total_relay, bool debug)
{
	use_external_client = false;
	debugEnabled = debug;
	relay_count = total_relay;

	relay_on_cmds.clear();
	relay_off_cmds.clear();

	// 自動產生 n 個 command
	for (int i = 0; i < relay_count; i++) {
		relay_on_cmds.push_back(buildRelayCommand(i, 1));
		relay_off_cmds.push_back(buildRelayCommand(i, 0));
	}

	// ALL ON/OFF（固定使用 reg=0x0034）
	relay_all_on = buildRelayCommand(0x0034, 1);
	relay_all_off = buildRelayCommand(0x0034, 0);

	if (debugEnabled)
		std::cout << "[RLY] init internal, relay_count=" << relay_count << std::endl;

	return internal_client.connectToServer(ip, port);
}

//---------------------------------------------------------------
// 初始化（外部 TCP client）
//---------------------------------------------------------------
bool ZS_DIO_R_RLY::init(TCP_client& extClient, int total_relay, bool debug)
{
	ext_client = &extClient;
	use_external_client = true;
	debugEnabled = debug;
	relay_count = total_relay;

	relay_on_cmds.clear();
	relay_off_cmds.clear();

	for (int i = 0; i < relay_count; i++) {
		relay_on_cmds.push_back(buildRelayCommand(i, 1));
		relay_off_cmds.push_back(buildRelayCommand(i, 0));
	}

	relay_all_on = buildRelayCommand(0x0034, 1);
	relay_all_off = buildRelayCommand(0x0034, 0);

	if (debugEnabled)
		std::cout << "[RLY] init external, relay_count=" << relay_count << std::endl;

	return true;
}

//---------------------------------------------------------------
// 控制單顆繼電器
//---------------------------------------------------------------
void ZS_DIO_R_RLY::controlRelay(int id, bool status)
{
	if (id < 1 || id > relay_count) {
		if (debugEnabled)
			std::cout << "[RLY] ERR invalid id=" << id
			<< " (max=" << relay_count << ")" << std::endl;
		return;
	}

	const std::vector<uint8_t>& cmd =
		status ? relay_on_cmds[id - 1] : relay_off_cmds[id - 1];

	if (debugEnabled)
		std::cout << "[RLY] CH" << id << " -> " << (status ? "ON" : "OFF") << std::endl;

	client().sendData(
		reinterpret_cast<const char*>(cmd.data()),
		(int)cmd.size(),
		100
	);

	std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

//---------------------------------------------------------------
// 全部繼電器 ON/OFF
//---------------------------------------------------------------
void ZS_DIO_R_RLY::controlAll(bool status)
{
	const std::vector<uint8_t>& cmd = status ? relay_all_on : relay_all_off;

	if (debugEnabled)
		std::cout << "[RLY] ALL -> " << (status ? "ON" : "OFF") << std::endl;

	client().sendData(
		reinterpret_cast<const char*>(cmd.data()),
		(int)cmd.size(),
		100
	);

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

//---------------------------------------------------------------
// 關閉 internal client
//---------------------------------------------------------------
void ZS_DIO_R_RLY::close()
{
	if (debugEnabled)
		std::cout << "[RLY] internal client closed" << std::endl;

	internal_client.close();
}
