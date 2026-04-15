#include "PQW_IO_16O_RLY.h"
#include "log_utils.h"
#include <cstring>
#include <chrono>
#include <thread>

//=========== init ===========

PQW_IO_16O_RLY::PQW_IO_16O_RLY() : client(nullptr), owns_client(false) {
	_log_tag = "PQW:?";
}

PQW_IO_16O_RLY::~PQW_IO_16O_RLY() {
	if (client) {
		controlAll(false);
		client->close();
		if (owns_client)
			delete client;
	}
}

bool PQW_IO_16O_RLY::init(const std::string& ip, int port, int ID, int total_relay, bool debug)
{
	relay_count = total_relay;
	debug_mode = debug;
	slave_id = (uint8_t)ID;
	_log_tag = "PQW:" + std::to_string(ID);

	client = new TCP_client();
	owns_client = true;

	if (!client->connectToServer(ip, port)) {
		LOG_ERR(_log_tag, "connect failed %s:%d", ip.c_str(), port);
		return true;
	}

	LOG_INF(_log_tag, "Connected, slave_id=%d", (int)slave_id);

	return false;
}

bool PQW_IO_16O_RLY::init(TCP_client& extClient, int ID, int total_relay, bool debug)
{
	relay_count = total_relay;
	debug_mode = debug;
	slave_id = (uint8_t)ID;
	_log_tag = "PQW:" + std::to_string(ID);
	this->client = &extClient;

	LOG_INF(_log_tag, "initialized with external TCP_client, slave_id=%d", (int)slave_id);

	return false;
}

//=========== utility: Modbus CRC ===========

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

//=========== utility: build single relay command (0x05) ===========

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

//=========== utility: build all on/off command (special addr 0x0085) ===========

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

//=========== utility: build read all status command (0x01) ===========

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

//=========== utility: hex dump ===========

void PQW_IO_16O_RLY::printHex(const std::vector<uint8_t>& data, const std::string& tag)
{
	LOG_HEX(_log_tag, tag.c_str(), data.data(), (int)data.size());
}

//=========== utility: read echo ===========

std::vector<uint8_t> PQW_IO_16O_RLY::readEcho()
{
	uint8_t buf[32];
	int n = client->receiveData((char*)buf, sizeof(buf), 200);

	if (n <= 0) return {};

	return std::vector<uint8_t>(buf, buf + n);
}

//=========== utility: parse read response (0x01) ===========

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

//=========== control: single relay (with echo + readback) ===========

bool PQW_IO_16O_RLY::controlRelay(int id, bool status)
{
	if (id < 1 || id > 16){//relay_count) {
		LOG_ERR(_log_tag, "Relay ID out of range: %d", id);
		return true;
	}

	auto cmd = buildSingleRelayCmd(id, status);
	printHex(cmd, "TX single relay");

	client->sendData((char*)cmd.data(), cmd.size(), 50);

	auto echo = readEcho();
	printHex(echo, "RX echo");

	auto readcmd = buildReadCmd();
	client->sendData((char*)readcmd.data(), readcmd.size(), 50);

	auto resp = readEcho();
	auto states = parseReadResponse(resp);

	return (states[id - 1] != status);
}

//=========== control: all relay ===========

bool PQW_IO_16O_RLY::controlAll(bool status)
{
	auto cmd = buildAllRelayCmd(status);
	printHex(cmd, "TX all relay");

	if (!client->sendData((char*)cmd.data(), cmd.size(), 100))
		return true;

	auto echo = readEcho();
	printHex(echo, "RX echo ALL");

	if (echo.size() < 8) return true;
	return false;
}

//=========== read: all status ===========

std::vector<bool> PQW_IO_16O_RLY::readAllStatus()
{
	auto cmd = buildReadCmd();
	printHex(cmd, "TX read status");

	client->sendData((char*)cmd.data(), cmd.size(), 100);

	auto resp = readEcho();
	printHex(resp, "RX read status");

	return parseReadResponse(resp);
}

//=========== utility: close ===========

void PQW_IO_16O_RLY::close()
{
	client->close();
}
