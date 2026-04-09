#include "PQW_IO_16O_RLY.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>

PQW_IO_16O_RLY::PQW_IO_16O_RLY() {}
PQW_IO_16O_RLY::~PQW_IO_16O_RLY() {
	controlAll(false);
	client->close();
}

/*--------------------------------------------------------------
  初始化：由本類別主動連線至設備
--------------------------------------------------------------*/
bool PQW_IO_16O_RLY::init(const std::string& ip, int port, int ID, int total_relay, bool debug)
{
	relay_count = total_relay;
	debug_mode = debug;
	slave_id = (uint8_t)ID;

	if (!client->connectToServer(ip, port)) {
		std::cout << "[ERROR] Unable to connect to " << ip << ":" << port << "\n";
		return false;
	}

	if (debug_mode)
		std::cout << "Connected to PQW-IO module. slave_id=" << (int)slave_id << "\n";

	return true;
}

/*--------------------------------------------------------------
  初始化：使用外部的 TCP_client（不重新建立連線）
--------------------------------------------------------------*/
bool PQW_IO_16O_RLY::init(TCP_client& extClient, int ID, int total_relay, bool debug)
{
	relay_count = total_relay;
	debug_mode = debug;
	slave_id = (uint8_t)ID;
	this->client = &extClient;

	if (debug_mode)
		std::cout << "PQW_IO_16O_RLY initialized with external TCP_client-> slave_id=" << (int)slave_id << "\n";

	return true;
}

/*--------------------------------------------------------------
  Modbus CRC
--------------------------------------------------------------*/
uint16_t PQW_IO_16O_RLY::modbusCRC(const uint8_t* data, int len)
{
	uint16_t crc = 0xFFFF;

	for (int i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i];
		for (int j = 0; j < 8; j++)
			crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
	}
	return crc;
}

/*--------------------------------------------------------------
  建立單顆繼電器指令 (Function 0x05)
--------------------------------------------------------------*/
std::vector<uint8_t> PQW_IO_16O_RLY::buildSingleRelayCmd(int relay_num, bool status)
{
	uint16_t addr = relay_num - 1;

	uint8_t value_hi = status ? 0xFF : 0x00;
	uint8_t value_lo = 0x00;

	std::vector<uint8_t> cmd = {
		(uint8_t)slave_id,
		(uint8_t)0x05,
		(uint8_t)(addr >> 8),
		(uint8_t)(addr & 0xFF),
		(uint8_t)value_hi,
		(uint8_t)value_lo
	};

	uint16_t crc = modbusCRC(cmd.data(), cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));

	return cmd;
}

/*--------------------------------------------------------------
  建立全開/全關指令 (特殊地址 0x0085)
--------------------------------------------------------------*/
std::vector<uint8_t> PQW_IO_16O_RLY::buildAllRelayCmd(bool status)
{
	uint8_t val_hi = status ? 0xFF : 0x00;

	std::vector<uint8_t> cmd = {
		(uint8_t)slave_id, (uint8_t)0x06,
		(uint8_t)0x00, (uint8_t)0x85,
		(uint8_t)val_hi, (uint8_t)0x00
	};

	uint16_t crc = modbusCRC(cmd.data(), cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));

	return cmd;
}

/*--------------------------------------------------------------
  建立讀取全部繼電器狀態指令 (Function 0x01)
--------------------------------------------------------------*/
std::vector<uint8_t> PQW_IO_16O_RLY::buildReadCmd()
{
	std::vector<uint8_t> cmd = {
		(uint8_t)slave_id,
		(uint8_t)0x01,
		(uint8_t)0x00, (uint8_t)0x00,
		(uint8_t)0x00, (uint8_t)relay_count
	};

	uint16_t crc = modbusCRC(cmd.data(), cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));

	return cmd;
}

/*--------------------------------------------------------------
  Debug 印 HEX
--------------------------------------------------------------*/
void PQW_IO_16O_RLY::printHex(const std::vector<uint8_t>& data, const std::string& tag)
{
	if (!debug_mode) return;

	std::cout << tag << ": ";
	for (size_t i = 0; i < data.size(); i++)
		printf("%02X ", data[i]);
	std::cout << "\n";
}

/*--------------------------------------------------------------
  讀取 Echo 回應
--------------------------------------------------------------*/
std::vector<uint8_t> PQW_IO_16O_RLY::readEcho()
{
	uint8_t buf[32];
	int n = client->receiveData((char*)buf, sizeof(buf), 200);

	if (n <= 0) return {};

	return std::vector<uint8_t>(buf, buf + n);
}

/*--------------------------------------------------------------
  解析讀取全部線圈狀態 (Function 0x01)
--------------------------------------------------------------*/
std::vector<bool> PQW_IO_16O_RLY::parseReadResponse(const std::vector<uint8_t>& resp)
{
	std::vector<bool> out(relay_count, false);

	if (resp.size() < 5) return out;

	for (int i = 0; i < relay_count; i++) {
		size_t byte_i = i / 8 + 3;
		int bit_i = i % 8;

		if (byte_i < resp.size())
			out[i] = (resp[byte_i] >> bit_i) & 1;
	}
	return out;
}

/*--------------------------------------------------------------
  控制單顆繼電器（含 Echo 回應與讀回確認）
--------------------------------------------------------------*/
bool PQW_IO_16O_RLY::controlRelay(int id, bool status)
{
	if (id < 1 || id > 16){//relay_count) {
		std::cout << "[ERROR] Relay ID out of range.\n";
		return false;
	}

	auto cmd = buildSingleRelayCmd(id, status);
	printHex(cmd, "[TX single relay]");

	client->sendData((char*)cmd.data(), cmd.size(), 50);

	auto echo = readEcho();
	printHex(echo, "[RX echo]");

	auto readcmd = buildReadCmd();
	client->sendData((char*)readcmd.data(), readcmd.size(), 50);

	auto resp = readEcho();
	auto states = parseReadResponse(resp);

	return (states[id - 1] == status);
}

/*--------------------------------------------------------------
  控制全部繼電器
--------------------------------------------------------------*/
void PQW_IO_16O_RLY::controlAll(bool status)
{
	auto cmd = buildAllRelayCmd(status);
	printHex(cmd, "[TX all relay]");

	client->sendData((char*)cmd.data(), cmd.size(), 100);

	auto echo = readEcho();
	printHex(echo, "[RX echo ALL]");
}

/*--------------------------------------------------------------
  讀取全部繼電器狀態
--------------------------------------------------------------*/
std::vector<bool> PQW_IO_16O_RLY::readAllStatus()
{
	auto cmd = buildReadCmd();
	printHex(cmd, "[TX read status]");

	client->sendData((char*)cmd.data(), cmd.size(), 100);

	auto resp = readEcho();
	printHex(resp, "[RX read status]");

	return parseReadResponse(resp);
}

/*--------------------------------------------------------------
  關閉 TCP 連線
--------------------------------------------------------------*/
void PQW_IO_16O_RLY::close()
{
	client->close();
}
