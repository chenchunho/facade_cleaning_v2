#ifndef ZS_DIO_R_RLY_H
#define ZS_DIO_R_RLY_H

#include "TCP_client.h"
#include <vector>
#include <string>
#include <cstdint>

class ZS_DIO_R_RLY {
public:
	ZS_DIO_R_RLY();
	~ZS_DIO_R_RLY();

	//=========== init ===========

	// Mode A: create internal TCP connection
	bool init(const std::string& ip, int port, int ID, int total_relay = 16, bool debug = false);

	// Mode B: share external TCP connection
	bool init(TCP_client& extClient, int ID, int total_relay = 16, bool debug = false);

	//=========== control ===========

	// single relay on/off (0x06, register 0x0000~0x002F)
	// returns false on success, true on error (retries 3 times)
	bool controlRelay(int ch, bool status);

	// all relay on/off (0x06, register 0x0034, value 0=all off, 1=all on)
	// returns false on success, true on error (retries 3 times)
	bool controlAll(bool status);

	// bitmask group control (0x06, register 0x0035/0x0036/0x0037)
	// group: 1=CH1~16, 2=CH17~32, 3=CH33~48
	// returns false on success, true on error (retries 3 times)
	bool controlGroup(int group, uint16_t bitmask);

	//=========== read ===========

	// read all relay states (wrapper for readCoils, matches PQW_IO_16O_RLY interface)
	std::vector<bool> readAllStatus();

	// read relay output states via coils (0x01)
	// startCh: 1-based, count: number of channels
	bool readCoils(int startCh, int count, std::vector<bool>& states);

	// read discrete input states (0x02)
	// startCh: 1-based, count: number of channels
	bool readDiscreteInputs(int startCh, int count, std::vector<bool>& states);

	// read relay group state as bitmask via input registers (0x04)
	// group: 1=CH1~16 (reg 0x0030), 2=CH17~32 (reg 0x0031), 3=CH33~48 (reg 0x0032)
	bool readGroupState(int group, uint16_t& bitmask);

	//=========== utility ===========

	void close();

private:
	TCP_client internal_client;
	TCP_client* ext_client = nullptr;
	bool use_external_client = false;
	bool debug_mode = false;
	std::string _log_tag;

	uint8_t slave_id = 0x01;
	int relay_count = 16;

	// pre-built commands for single relay control
	std::vector<std::vector<uint8_t>> relay_on_cmds;
	std::vector<std::vector<uint8_t>> relay_off_cmds;

	// pre-built commands for all on/off
	std::vector<uint8_t> relay_all_on;
	std::vector<uint8_t> relay_all_off;

	// packet building
	uint16_t crc16_modbus(const uint8_t* data, size_t len);
	std::vector<uint8_t> buildWriteRegCmd(uint16_t regAddr, uint16_t value);
	std::vector<uint8_t> buildReadCmd(uint8_t funcCode, uint16_t startAddr, uint16_t quantity);

	// send command and receive response
	std::vector<uint8_t> sendAndReceive(const std::vector<uint8_t>& cmd, int timeout_ms = 200);

	// parse bit-based response (0x01/0x02)
	bool parseBitResponse(const std::vector<uint8_t>& resp, int count, std::vector<bool>& states);

	// verify 0x06 echo matches sent command, returns false on match, true on mismatch
	bool verifyEcho(const std::vector<uint8_t>& cmd, const std::vector<uint8_t>& resp);

	static const int MAX_RETRY = 3;

	inline TCP_client& client() {
		return use_external_client ? *ext_client : internal_client;
	}
};

#endif // ZS_DIO_R_RLY_H
