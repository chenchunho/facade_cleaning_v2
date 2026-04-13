#include "ZS_DIO_R_RLY.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

//=========== utility ===========

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

std::vector<uint8_t> ZS_DIO_R_RLY::buildWriteRegCmd(uint16_t regAddr, uint16_t value)
{
	std::vector<uint8_t> cmd = {
		slave_id,
		0x06,
		(uint8_t)(regAddr >> 8),
		(uint8_t)(regAddr & 0xFF),
		(uint8_t)(value >> 8),
		(uint8_t)(value & 0xFF)
	};

	uint16_t crc = crc16_modbus(cmd.data(), cmd.size());
	cmd.push_back(crc & 0xFF);
	cmd.push_back((crc >> 8) & 0xFF);

	return cmd;
}

std::vector<uint8_t> ZS_DIO_R_RLY::buildReadCmd(uint8_t funcCode, uint16_t startAddr, uint16_t quantity)
{
	std::vector<uint8_t> cmd = {
		slave_id,
		funcCode,
		(uint8_t)(startAddr >> 8),
		(uint8_t)(startAddr & 0xFF),
		(uint8_t)(quantity >> 8),
		(uint8_t)(quantity & 0xFF)
	};

	uint16_t crc = crc16_modbus(cmd.data(), cmd.size());
	cmd.push_back(crc & 0xFF);
	cmd.push_back((crc >> 8) & 0xFF);

	return cmd;
}

std::vector<uint8_t> ZS_DIO_R_RLY::sendAndReceive(const std::vector<uint8_t>& cmd, int timeout_ms)
{
	printHex(cmd, "[TX]");

	client().sendData(
		reinterpret_cast<const char*>(cmd.data()),
		(int)cmd.size(),
		100
	);

	uint8_t buf[64];
	int n = client().receiveData(reinterpret_cast<char*>(buf), sizeof(buf), timeout_ms);

	if (n <= 0)
		return {};

	std::vector<uint8_t> resp(buf, buf + n);
	printHex(resp, "[RX]");
	return resp;
}

void ZS_DIO_R_RLY::printHex(const std::vector<uint8_t>& data, const std::string& tag)
{
	if (!debugEnabled) return;

	std::cout << tag << ": ";
	for (size_t i = 0; i < data.size(); i++)
		printf("%02X ", data[i]);
	std::cout << std::endl;
}

bool ZS_DIO_R_RLY::verifyEcho(const std::vector<uint8_t>& cmd, const std::vector<uint8_t>& resp)
{
	// 0x06 echo should be identical to sent command (8 bytes)
	if (resp.size() != cmd.size())
		return true;

	// check function code error flag (high bit set = exception)
	if (resp.size() >= 2 && (resp[1] & 0x80)) {
		if (debugEnabled)
			std::cout << "[ZS_DIO] ERR exception code=0x"
			          << std::hex << (int)resp[2] << std::dec << std::endl;
		return true;
	}

	for (size_t i = 0; i < cmd.size(); i++) {
		if (cmd[i] != resp[i])
			return true;
	}

	return false;
}

bool ZS_DIO_R_RLY::parseBitResponse(const std::vector<uint8_t>& resp, int count, std::vector<bool>& states)
{
	// response format: [slave_id] [func] [byte_count] [data...] [crc_lo] [crc_hi]
	if (resp.size() < 5)
		return true;

	int byte_count = resp[2];
	if ((int)resp.size() < 3 + byte_count + 2)
		return true;

	states.clear();
	states.resize(count, false);

	for (int i = 0; i < count; i++) {
		int byte_idx = i / 8 + 3;
		int bit_idx = i % 8;

		if (byte_idx < (int)resp.size() - 2)
			states[i] = (resp[byte_idx] >> bit_idx) & 1;
	}
	return false;
}

//=========== init ===========

ZS_DIO_R_RLY::ZS_DIO_R_RLY() {}

ZS_DIO_R_RLY::~ZS_DIO_R_RLY()
{
	if (!use_external_client)
		close();
}

bool ZS_DIO_R_RLY::init(const std::string& ip, int port, int ID, int total_relay, bool debug)
{
	use_external_client = false;
	debugEnabled = debug;
	slave_id = (uint8_t)ID;
	relay_count = total_relay;

	// build pre-computed commands for single relay control
	relay_on_cmds.clear();
	relay_off_cmds.clear();

	for (int i = 0; i < relay_count; i++) {
		relay_on_cmds.push_back(buildWriteRegCmd(i, 1));
		relay_off_cmds.push_back(buildWriteRegCmd(i, 0));
	}

	relay_all_on = buildWriteRegCmd(0x0034, 1);
	relay_all_off = buildWriteRegCmd(0x0034, 0);

	if (debugEnabled)
		std::cout << "[ZS_DIO] init internal, slave_id=" << (int)slave_id
		          << " relay_count=" << relay_count << std::endl;

	if (!internal_client.connectToServer(ip, port))
		return true;

	return false;
}

bool ZS_DIO_R_RLY::init(TCP_client& extClient, int ID, int total_relay, bool debug)
{
	ext_client = &extClient;
	use_external_client = true;
	debugEnabled = debug;
	slave_id = (uint8_t)ID;
	relay_count = total_relay;

	relay_on_cmds.clear();
	relay_off_cmds.clear();

	for (int i = 0; i < relay_count; i++) {
		relay_on_cmds.push_back(buildWriteRegCmd(i, 1));
		relay_off_cmds.push_back(buildWriteRegCmd(i, 0));
	}

	relay_all_on = buildWriteRegCmd(0x0034, 1);
	relay_all_off = buildWriteRegCmd(0x0034, 0);

	if (debugEnabled)
		std::cout << "[ZS_DIO] init external, slave_id=" << (int)slave_id
		          << " relay_count=" << relay_count << std::endl;

	return false;
}

//=========== control ===========

bool ZS_DIO_R_RLY::controlRelay(int ch, bool status)
{
	if (ch < 1 || ch > relay_count) {
		if (debugEnabled)
			std::cout << "[ZS_DIO] ERR invalid ch=" << ch
			          << " (max=" << relay_count << ")" << std::endl;
		return true;
	}

	const std::vector<uint8_t>& cmd =
		status ? relay_on_cmds[ch - 1] : relay_off_cmds[ch - 1];

	for (int attempt = 0; attempt < MAX_RETRY; attempt++) {
		if (debugEnabled)
			std::cout << "[ZS_DIO] CH" << ch << " -> " << (status ? "ON" : "OFF")
			          << " (attempt " << attempt + 1 << ")" << std::endl;

		auto resp = sendAndReceive(cmd);

		if (!verifyEcho(cmd, resp))
			return false;

		if (debugEnabled)
			std::cout << "[ZS_DIO] CH" << ch << " echo mismatch, retrying..." << std::endl;
	}

	if (debugEnabled)
		std::cout << "[ZS_DIO] CH" << ch << " FAILED after " << MAX_RETRY << " retries" << std::endl;
	return true;
}

bool ZS_DIO_R_RLY::controlAll(bool status)
{
	const std::vector<uint8_t>& cmd = status ? relay_all_on : relay_all_off;

	for (int attempt = 0; attempt < MAX_RETRY; attempt++) {
		if (debugEnabled)
			std::cout << "[ZS_DIO] ALL -> " << (status ? "ON" : "OFF")
			          << " (attempt " << attempt + 1 << ")" << std::endl;

		auto resp = sendAndReceive(cmd);

		if (!verifyEcho(cmd, resp))
			return false;

		if (debugEnabled)
			std::cout << "[ZS_DIO] ALL echo mismatch, retrying..." << std::endl;
	}

	if (debugEnabled)
		std::cout << "[ZS_DIO] ALL FAILED after " << MAX_RETRY << " retries" << std::endl;
	return true;
}

bool ZS_DIO_R_RLY::controlGroup(int group, uint16_t bitmask)
{
	if (group < 1 || group > 3) {
		if (debugEnabled)
			std::cout << "[ZS_DIO] ERR invalid group=" << group << " (1~3)" << std::endl;
		return true;
	}

	// group 1 -> reg 0x0035, group 2 -> reg 0x0036, group 3 -> reg 0x0037
	uint16_t regAddr = 0x0034 + group;
	auto cmd = buildWriteRegCmd(regAddr, bitmask);

	for (int attempt = 0; attempt < MAX_RETRY; attempt++) {
		if (debugEnabled)
			std::cout << "[ZS_DIO] GROUP" << group << " bitmask=0x"
			          << std::hex << bitmask << std::dec
			          << " (attempt " << attempt + 1 << ")" << std::endl;

		auto resp = sendAndReceive(cmd);

		if (!verifyEcho(cmd, resp))
			return false;

		if (debugEnabled)
			std::cout << "[ZS_DIO] GROUP" << group << " echo mismatch, retrying..." << std::endl;
	}

	if (debugEnabled)
		std::cout << "[ZS_DIO] GROUP" << group << " FAILED after " << MAX_RETRY << " retries" << std::endl;
	return true;
}

//=========== read ===========

std::vector<bool> ZS_DIO_R_RLY::readAllStatus()
{
	std::vector<bool> states;
	readCoils(1, relay_count, states);
	return states;
}

bool ZS_DIO_R_RLY::readCoils(int startCh, int count, std::vector<bool>& states)
{
	if (startCh < 1 || count < 1) return true;

	auto cmd = buildReadCmd(0x01, startCh - 1, count);
	auto resp = sendAndReceive(cmd);

	if (resp.empty())
		return true;

	return parseBitResponse(resp, count, states);
}

bool ZS_DIO_R_RLY::readDiscreteInputs(int startCh, int count, std::vector<bool>& states)
{
	if (startCh < 1 || count < 1) return true;

	auto cmd = buildReadCmd(0x02, startCh - 1, count);
	auto resp = sendAndReceive(cmd);

	if (resp.empty())
		return true;

	return parseBitResponse(resp, count, states);
}

bool ZS_DIO_R_RLY::readGroupState(int group, uint16_t& bitmask)
{
	if (group < 1 || group > 3) return true;

	// group 1 -> input register 0x0030, group 2 -> 0x0031, group 3 -> 0x0032
	uint16_t regAddr = 0x002F + group;
	auto cmd = buildReadCmd(0x04, regAddr, 1);
	auto resp = sendAndReceive(cmd);

	// response format: [slave_id] [func] [byte_count] [data_hi] [data_lo] [crc_lo] [crc_hi]
	if (resp.size() < 7)
		return true;

	bitmask = ((uint16_t)resp[3] << 8) | resp[4];
	return false;
}

//=========== utility ===========

void ZS_DIO_R_RLY::close()
{
	if (debugEnabled)
		std::cout << "[ZS_DIO] internal client closed" << std::endl;

	internal_client.close();
}
