// ⚰️ RETIRED FROM PRODUCTION — 2026-05-07, marked 2026-08-28
//
// 捲揚機繼電器控制。2026-05-07 起左右鋼索改由兩台 SE3-210 變頻器驅動
// （Crane_control_PI/main.cpp:41），本 driver **已不在任何主控程式裡**：
// 全 repo 只剩 `Linux_test/main.cpp` 的第 10 項選單引用它，作為 bench 工具。
//
// 🔴 **本檔刻意不隨 2026-08-28 的 driver 稽核一起硬化。** 那一輪替
// SD76 / DSZL / DY-500 / SE3 / MH300 / CLV900 / ZDT / PQW / DM2J 補上了
// 回覆驗證（slave id / 功能碼 / 長度邊界 / CRC）。本檔同樣缺這些檢查，
// 但既然 production 不再使用，改它只會增加沒人驗證的改動面。
//   → parseBitResponse() 已有 `3 + byte_count + 2` 的長度邊界，走 vector，
//     **沒有記憶體覆寫風險**；缺的是 slave id 與 CRC，後果僅止於
//     bench 工具讀到錯的繼電器狀態。
//
// 📌 **哪些內容仍然有效**：暫存器與線圈位址對照見
// `.claude/summaries/ZS_DIO_MODBUS_SUMMARY.md`（保留作歷史對照）。
//
// ⚠️ **若日後要讓它重回 production，先補回覆驗證再說**——照
// `PQW_IO_16O_RLY::parseReadResponse()` 的寫法即可，那支是同類型的
// 繼電器模組、同樣走 FC 0x01。
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
