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
	if (client && owns_client) {
		controlAll(false);
		client->close();
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

	// [2026-08-31] Presence probe — mirrors SD76_length_meters' "Mode B probe".
	//
	// 🔴 Why this exists: before it, init() only assigned fields and returned
	// success without ever touching the bus. TCP connect to the shared
	// USR-TCP232 gateway succeeding (gateway alive) was reported as
	// "[OK] PQW ... (water inlet ball valve)" even when the module was
	// PHYSICALLY REMOVED — observed 2026-08-31 on the crane, where the module
	// had just been unplugged to clear an RS485 fault and init still said OK.
	// Downstream every set_water_inlet_() then fails silently while the boot
	// log claims the valve is present.
	//
	// SD76 hit the identical trap on 2026-05-15 ("meters physically unplugged
	// still showed [OK] resumed") and solved it with exactly this pattern.
	//
	// FC01 read-all is the natural probe: it is read-only (touches no relay),
	// it is the same call the verify path already uses, and parseReadResponse()
	// already returns an EMPTY vector for any unusable reply (short frame /
	// wrong slave / bad CRC), so "empty" is an established failure signal here.
	//
	// ⚠️ Retried PROBE_TRIES times: the washrobot caller treats init failure as
	// [FATAL] and aborts the whole boot, so a single transient bus hiccup at
	// startup must not brick startup. The crane caller is non-fatal ([WARN] +
	// device marked unavailable), which is the behaviour this probe is meant
	// to enable there.
	constexpr int PROBE_TRIES    = 3;
	constexpr int PROBE_GAP_MS   = 120;
	for (int attempt = 1; attempt <= PROBE_TRIES; ++attempt) {
		const auto st = readAllStatus();
		if ((int)st.size() >= relay_count) return false;   // answered — device is present
		if (attempt < PROBE_TRIES)
			std::this_thread::sleep_for(std::chrono::milliseconds(PROBE_GAP_MS));
	}
	LOG_ERR(_log_tag, "init presence probe failed after %d tries — device not on bus (slave %d)",
	        PROBE_TRIES, (int)slave_id);
	return true;
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
	// [2026-08-29] Null-client guard: the constructor leaves `client` as nullptr
	// and only init() sets it, so a call on an un-init'd (or failed-init)
	// instance dereferences nullptr and takes the whole process down.
	// Application layers already gate these calls, but that is the caller
	// remembering to be careful — the driver must not be a landmine.
	if (!client) return {};
	uint8_t buf[32];
	int n = client->receiveData((char*)buf, sizeof(buf), 200);

	if (n <= 0) return {};

	return std::vector<uint8_t>(buf, buf + n);
}

//=========== utility: parse read response (0x01) ===========

std::vector<bool> PQW_IO_16O_RLY::parseReadResponse(const std::vector<uint8_t>& resp)
{
	// [2026-08-28] Validate the FC01 reply before believing it.
	//
	// An EMPTY vector is the "could not read" signal, and both callers already
	// handle it that way (WASH_ROBOT.cpp pqw_set_relay_verified_ and
	// Crane_control_PI water-inlet verify both treat st.empty() as "can't
	// verify, accept as best-effort"). That is why validation failures return
	// {} rather than a default-constructed state.
	//
	// 🐛 The old code returned a FULL vector of `false` for a short frame,
	// which callers could not distinguish from a genuine "all relays off" —
	// a garbled reply could therefore confirm an OFF command that never
	// physically happened.
	//
	// ⚠️ Deliberately NOT applied to controlRelay()'s echo path: PQW firmware
	// echoes writes in a non-standard format and the old read-back-and-compare
	// there caused intermittent false failures that trapped the robot
	// mid-sequence with no recoverable path (work_log 2026-04-23 / 04-27).
	// That decision stands; this only hardens the FC01 *read*.
	if (resp.size() < 5) return {};

	if (resp[0] != slave_id) {
		LOG_ERR(_log_tag, "status reply slave %d != %d", (int)resp[0], (int)slave_id);
		return {};
	}
	if (resp[1] != 0x01) {          // 0x81 = Modbus exception reply
		LOG_ERR(_log_tag, "status reply FC 0x%02X (expected 0x01)", (int)resp[1]);
		return {};
	}

	const size_t bc = resp[2];
	if (resp.size() < 3 + bc + 2) {
		LOG_ERR(_log_tag, "status frame truncated: %d < %d",
		        (int)resp.size(), (int)(3 + bc + 2));
		return {};
	}

	const uint16_t rx_crc = (uint16_t)resp[3 + bc] | ((uint16_t)resp[4 + bc] << 8);
	if (modbusCRC(resp.data(), (int)(3 + bc)) != rx_crc) {
		LOG_ERR(_log_tag, "status CRC mismatch");
		return {};
	}

	std::vector<bool> out(relay_count, false);
	for (int i = 0; i < relay_count; i++) {
		size_t byte_i = i / 8 + 3;
		int bit_i = i % 8;

		if (byte_i < 3 + bc)
			out[i] = (resp[byte_i] >> bit_i) & 1;
	}
	return out;
}

//=========== control: single relay (with echo + readback) ===========

bool PQW_IO_16O_RLY::controlRelay(int id, bool status)
{
	if (!client) return true;
	if (id < 1 || id > 16){//relay_count) {
		LOG_ERR(_log_tag, "Relay ID out of range: %d", id);
		return true;
	}

	auto cmd = buildSingleRelayCmd(id, status);
	printHex(cmd, "TX single relay");

	// Send the relay command. Genuine TCP send failure (gateway down) → real
	// error reported via return true.
	client->drainRx();   // [2026-09-01] 交易開頭排空：無此行則一筆遲到回覆會造成永久失步（見 TCP_client::drainRx 說明）
	if (!client->sendData((char*)cmd.data(), cmd.size(), 50))
		return true;

	// Drain echo for log-only diagnostic. PQW firmware echo format is
	// non-standard (TX `... 05 ...` echoed as RX `... 00 ...`) so we DO NOT
	// parse it for verification — physical relay LED is the source of truth.
	// The previous read-back-then-compare path triggered intermittent false
	// failures (e.g. step_down body_valve_off_fail) that trapped the robot
	// mid-sequence with no recoverable path. See work_log 2026-04-23
	// "PQW relay module 回應格式異常" + 2026-04-27 trap on step_down feet rail.
	auto echo = readEcho();
	printHex(echo, "RX echo");

	return false;
}

//=========== control: all relay ===========

bool PQW_IO_16O_RLY::controlAll(bool status)
{
	if (!client) return true;
	auto cmd = buildAllRelayCmd(status);
	printHex(cmd, "TX all relay");

	client->drainRx();   // [2026-09-01] 交易開頭排空：無此行則一筆遲到回覆會造成永久失步（見 TCP_client::drainRx 說明）
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
	if (!client) return {};
	auto cmd = buildReadCmd();
	printHex(cmd, "TX read status");

	client->drainRx();   // [2026-09-01] 交易開頭排空：無此行則一筆遲到回覆會造成永久失步（見 TCP_client::drainRx 說明）
	client->sendData((char*)cmd.data(), cmd.size(), 100);

	auto resp = readEcho();
	printHex(resp, "RX read status");

	return parseReadResponse(resp);
}

//=========== utility: close ===========

void PQW_IO_16O_RLY::close()
{
	if (!client) return;
	client->close();
}
