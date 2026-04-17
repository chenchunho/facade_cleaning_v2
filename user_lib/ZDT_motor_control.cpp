#include "ZDT_motor_control.h"
#include "log_utils.h"
#include <thread>
#include <chrono>

const double PULSE_TO_DEG = 360.0 / 65536.0;

//=========== init ===========

ZDT_motor_control::ZDT_motor_control() {
	_log_tag = "ZDT:?";
}

ZDT_motor_control::~ZDT_motor_control() {
	if (!is_external_client && client != nullptr) {
		delete client;
		client = nullptr;
	}
}

bool ZDT_motor_control::init(const std::string& ip, int port, int ID, bool debug) {
	debug_mode = debug;
	slave_id = (uint8_t)ID;
	_log_tag = "ZDT:" + std::to_string(ID);
	client = new TCP_client();
	is_external_client = false;
	bool connected = client->connectToServer(ip, port);
	if (connected)
		LOG_INF(_log_tag, "Connection SUCCESS");
	else
		LOG_ERR(_log_tag, "Connection FAILED");
	return !connected;
}

bool ZDT_motor_control::init(TCP_client& extClient, int ID, bool debug) {
	debug_mode = debug;
	slave_id = (uint8_t)ID;
	_log_tag = "ZDT:" + std::to_string(ID);
	this->client = &extClient;
	is_external_client = true;
	LOG_INF(_log_tag, "External Client bound");
	return false;
}

//=========== control: trigger commands (3.1) ===========

bool ZDT_motor_control::set_zero() {
	auto cmd = build_write_single_register(0x000A, 0x0001);
	LOG_HEX(_log_tag, "TX set_zero", cmd.data(), (int)cmd.size());
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(200);
	LOG_HEX(_log_tag, "RX set_zero", resp.data(), (int)resp.size());
	return !(resp.size() > 0 && resp == cmd);
}

bool ZDT_motor_control::calibrate_encoder() {
	auto cmd = build_write_single_register(0x0006, 0x0001);
	LOG_HEX(_log_tag, "TX calibrate_encoder", cmd.data(), (int)cmd.size());
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	auto resp = readEcho(500);
	LOG_HEX(_log_tag, "RX calibrate_encoder", resp.data(), (int)resp.size());
	return !(resp.size() > 0 && resp == cmd);
}

bool ZDT_motor_control::reset_motor() {
	auto cmd = build_write_single_register(0x0008, 0x0001);
	LOG_HEX(_log_tag, "TX reset_motor", cmd.data(), (int)cmd.size());
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(200);
	LOG_HEX(_log_tag, "RX reset_motor", resp.data(), (int)resp.size());
	return !(resp.size() > 0 && resp == cmd);
}

//=========== control: motion commands (3.2) ===========

bool ZDT_motor_control::motion_control_driver_EN(bool status) {
	std::vector<uint8_t> cmd = {
		slave_id, 0x10, 0x00, 0xF3, 0x00, 0x02, 0x04, 0xAB,
		(uint8_t)(status ? 0x01 : 0x00), 0x00, 0x00
	};
	uint16_t crc = modbusCRC(cmd.data(), (int)cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));

	LOG_HEX(_log_tag, status ? "TX Driver EN ON" : "TX Driver EN OFF", cmd.data(), (int)cmd.size());
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(200);
	LOG_HEX(_log_tag, "RX Driver EN", resp.data(), (int)resp.size());
	return !(resp.size() == 8 && resp[1] == 0x10);
}

//=========== read: system status (3.7.2) ===========

bool ZDT_motor_control::get_system_status() {
	// 3.7.2 Emm bulk read: Func 0x04, Reg 0x0043, Qty 0x0010
	std::vector<uint8_t> cmd = { slave_id, 0x04, 0x00, 0x43, 0x00, 0x10 };
	uint16_t crc = modbusCRC(cmd.data(), (int)cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));

	LOG_HEX(_log_tag, "TX get_status", cmd.data(), (int)cmd.size());
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;

	auto resp = readEcho(300);
	LOG_HEX(_log_tag, "RX get_status", resp.data(), (int)resp.size());

	// Emm response: Addr(1) + Func(1) + ByteCount(1) + Data(32) + CRC(2) = 37 bytes
	if (resp.size() < 37) return true;

	int p = 3; // skip Addr, Func, ByteCount
	auto get_u16 = [&](int& pos) {
		uint16_t v = ((uint16_t)resp[pos] << 8) | resp[pos + 1]; pos += 2; return v;
	};
	auto get_u32 = [&](int& pos) {
		uint32_t v = ((uint32_t)resp[pos] << 24) | ((uint32_t)resp[pos + 1] << 16)
		           | ((uint32_t)resp[pos + 2] << 8) | resp[pos + 3];
		pos += 4; return v;
	};

	const double EMM_TO_DEG = 360.0 / 65536.0;

	// Reg 1: [total byte count, param count] — skip
	get_u16(p);

	// Reg 2: bus voltage (mV)
	this->status.bus_voltage = get_u16(p);

	// Reg 3: phase current (mA) — Emm bulk read has no bus current field
	this->status.phase_current = get_u16(p);
	this->status.bus_current = 0;

	// Reg 4: linearized encoder — Emm bulk read has no raw encoder field
	this->status.encoder_linear = get_u16(p);
	this->status.encoder_raw = 0;

	// Reg 5-7: target pos (sign u16 + val u32)
	uint16_t t_sign = get_u16(p);
	uint32_t t_val  = get_u32(p);
	this->status.target_pos = (t_sign == 1 ? -1.0 : 1.0) * t_val * EMM_TO_DEG;

	// Reg 8-9: real speed (sign u16 + val u16, Emm uses RPM directly)
	uint16_t s_sign = get_u16(p);
	uint16_t s_val  = get_u16(p);
	this->status.real_speed = (s_sign == 1 ? -1.0 : 1.0) * s_val;

	// Reg 10-12: real pos (sign u16 + val u32)
	uint16_t r_sign = get_u16(p);
	uint32_t r_val  = get_u32(p);
	this->status.real_pos = (r_sign == 1 ? -1.0 : 1.0) * r_val * EMM_TO_DEG;

	// Reg 13-15: position error (sign u16 + val u32)
	uint16_t e_sign = get_u16(p);
	uint32_t e_val  = get_u32(p);
	this->status.pos_error = (e_sign == 1 ? -1.0 : 1.0) * e_val * EMM_TO_DEG;

	// Reg 16: [home flags(H), motor flags(L)]
	uint16_t state_reg = get_u16(p);
	uint8_t home_flags  = (uint8_t)(state_reg >> 8);
	uint8_t motor_flags = (uint8_t)(state_reg & 0xFF);

	// home status flags (3.3.4)
	this->status.encoder_ready     = (home_flags >> 0) & 0x01;
	this->status.calibration_ready = (home_flags >> 1) & 0x01;
	this->status.is_homing         = (home_flags >> 2) & 0x01;
	this->status.home_failed       = (home_flags >> 3) & 0x01;
	this->status.over_temp_flag    = (home_flags >> 4) & 0x01;
	this->status.over_current_flag = (home_flags >> 5) & 0x01;

	// motor status flags (3.4.14)
	this->status.is_enabled       = (motor_flags >> 0) & 0x01;
	this->status.pos_reached      = (motor_flags >> 1) & 0x01;
	this->status.stall_flag       = (motor_flags >> 2) & 0x01;
	this->status.stall_protection = (motor_flags >> 3) & 0x01;
	this->status.left_limit       = (motor_flags >> 4) & 0x01;
	this->status.right_limit      = (motor_flags >> 5) & 0x01;
	this->status.power_loss_flag  = (motor_flags >> 7) & 0x01;

	// Emm bulk read has no temperature
	this->status.temperature = 0;

	return true;
}

bool ZDT_motor_control::wait_until_pos_reached(int timeout_ms, int poll_interval_ms) {
	auto start = std::chrono::steady_clock::now();
	while (true) {
		if (get_system_status() && status.pos_reached) {
			return true;
		}
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - start).count();
		if (elapsed >= timeout_ms) {
			LOG_ERR(_log_tag, "wait_until_pos_reached TIMEOUT: %ld ms", (long)elapsed);
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
	}
}

//=========== control: release stall / emergency stop ===========

bool ZDT_motor_control::release_stall_flag() {
	// Write register 0x000E = 0x0001
	auto cmd = build_write_single_register(0x000E, 0x0001);

	LOG_HEX(_log_tag, "TX release_stall", cmd.data(), (int)cmd.size());

	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return false;

	auto resp = readEcho(200);

	LOG_HEX(_log_tag, "RX release_stall", resp.data(), (int)resp.size());

	// Standard Modbus 06 reply should echo the request
	return !(resp.size() > 0 && resp == cmd);
}

bool ZDT_motor_control::emergency_stop(bool sync) {
	// 3.2.12 emergency stop: Func 0x06, Reg 0x00FE, Data [0x98, sync flag]
	uint16_t data = (0x98 << 8) | (sync ? 0x01 : 0x00);
	auto cmd = build_write_single_register(0x00FE, data);

	LOG_HEX(_log_tag, "TX emergency_stop", cmd.data(), (int)cmd.size());
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;

	auto resp = readEcho(200);
	LOG_HEX(_log_tag, "RX emergency_stop", resp.data(), (int)resp.size());

	// FC 0x06 reply echoes the command
	return !(resp.size() > 0 && resp == cmd);
}

//=========== control: speed mode ===========

bool ZDT_motor_control::motion_control_speed_mode(int dir, int acc_rpm, int rpm, int sync, int retry) {
	// 1. Build write-multiple-registers (0x10) frame, register 0x00F6
	std::vector<uint8_t> cmd = {
		slave_id, 0x10, 0x00, 0xF6, 0x00, 0x03, 0x06,
		(uint8_t)dir,           // reg1 H: direction (00:CW / 01:CCW)
		(uint8_t)acc_rpm,       // reg1 L: accel (0-255, 0=direct start no ramp)
		(uint8_t)(rpm >> 8),    // reg2 H: speed hi
		(uint8_t)(rpm & 0xFF),  // reg2 L: speed lo
		(uint8_t)sync,          // reg3 H: sync flag (00:immediate / 01:buffered)
		0x00                    // reg3 L: fixed 0x00
	};

	uint16_t crc = modbusCRC(cmd.data(), (int)cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));

	int attempt = 0;
	while (attempt <= retry) {
		char tx_label[48];
		if (attempt > 0)
			std::snprintf(tx_label, sizeof(tx_label), "TX Speed Mode (Retry %d)", attempt);
		else
			std::snprintf(tx_label, sizeof(tx_label), "TX Speed Mode");
		LOG_HEX(_log_tag, tx_label, cmd.data(), (int)cmd.size());

		if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) {
			LOG_ERR(_log_tag, "Send failed, waiting for retry");
			attempt++;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}

		auto resp = readEcho(200);

		if (resp.empty())
			LOG_ERR(_log_tag, "RX Speed Mode: TIMEOUT");
		else
			LOG_HEX(_log_tag, "RX Speed Mode", resp.data(), (int)resp.size());

		// validate (func 0x10, start addr 0x00F6, qty 0x0003)
		bool success = false;
		if (resp.size() == 8 && resp[1] == 0x10 && resp[3] == 0xF6) {
			uint16_t rx_crc = (resp[7] << 8) | resp[6];
			if (rx_crc == modbusCRC(resp.data(), 6)) {
				success = true;
			}
		}

		if (success) {
			return false; // no error
		}
		else {
			// release stall and prepare for retry
			LOG_WRN(_log_tag, "RX Invalid or Error. Executing release_stall_flag...");

			release_stall_flag();

			attempt++;
		}
	}

	LOG_ERR(_log_tag, "Speed Mode failed after reaching max retry limit");
	return true; // error
}

//=========== control: position mode ===========

bool ZDT_motor_control::motion_control_pos_mode(int dir, int acc_rpm, int rpm, int pulse, int mode, int sync, int retry) {
	// mode: 0=relative, 1=absolute (only valid values)
	if (mode != 0 && mode != 1) {
		LOG_ERR(_log_tag, "Invalid mode %d, must be 0 (relative) or 1 (absolute)", mode);
		return true;
	}

	// 1. Build write-multiple-registers (0x10) frame
	std::vector<uint8_t> cmd = {
		slave_id, 0x10, 0x00, 0xFD, 0x00, 0x05, 0x0A,
		(uint8_t)dir,           // data1: direction
		(uint8_t)acc_rpm,       // data2: accel (0-255, 0=direct start no ramp)
		(uint8_t)(rpm >> 8),    // data3: speed hi
		(uint8_t)(rpm & 0xFF),  // data4: speed lo
		(uint8_t)(pulse >> 24), // data5: pulse B3 (high)
		(uint8_t)(pulse >> 16), // data6: pulse B2
		(uint8_t)(pulse >> 8),  // data7: pulse B1
		(uint8_t)(pulse & 0xFF),// data8: pulse B0 (low)
		(uint8_t)mode,          // data9: mode (0 relative / 1 absolute)
		(uint8_t)sync           // data10: sync (0 async / 1 sync)
	};

	// append CRC16
	uint16_t crc = modbusCRC(cmd.data(), (int)cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));

	int attempt = 0;
	while (attempt <= retry) {
		char tx_label[48];
		if (attempt > 0)
			std::snprintf(tx_label, sizeof(tx_label), "TX Pos Mode (Retry %d)", attempt);
		else
			std::snprintf(tx_label, sizeof(tx_label), "TX Pos Mode");
		LOG_HEX(_log_tag, tx_label, cmd.data(), (int)cmd.size());

		if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) {
			LOG_ERR(_log_tag, "Send failed, waiting for retry");
			attempt++;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}

		auto resp = readEcho(300);

		if (resp.empty())
			LOG_ERR(_log_tag, "RX Pos Mode: TIMEOUT");
		else
			LOG_HEX(_log_tag, "RX Pos Mode", resp.data(), (int)resp.size());

		bool success = false;
		if (resp.size() == 8 && resp[1] == 0x10 && resp[3] == 0xFD) {
			uint16_t rx_crc = (resp[7] << 8) | resp[6];
			if (rx_crc == modbusCRC(resp.data(), 6)) {
				success = true;
			}
		}

		if (success) {
			// wait for motion to finish
			if (!wait_until_pos_reached()) {
				return false; // no error
			}
			else {
				LOG_WRN(_log_tag, "Waiting for moving timeout");
				return true; // error: timeout
			}
		}
		else {
			readEcho(500); // drain remaining data from buffer

			LOG_WRN(_log_tag, "RX Invalid or CRC Error. Releasing stall flag...");

			release_stall_flag();

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			attempt++;
		}
	}

	LOG_ERR(_log_tag, "Pos Mode failed after reaching max retry limit");
	return true; // error
}

bool ZDT_motor_control::motion_control_pos_mode_nowait(int dir, int acc_rpm, int rpm, int pulse, int mode, int sync, int retry) {
	// mode: 0=relative, 1=absolute (only valid values)
	if (mode != 0 && mode != 1) {
		LOG_ERR(_log_tag, "Invalid mode %d, must be 0 (relative) or 1 (absolute)", mode);
		return true;
	}

	// 1. Build write-multiple-registers (0x10) frame
	std::vector<uint8_t> cmd = {
		slave_id, 0x10, 0x00, 0xFD, 0x00, 0x05, 0x0A,
		(uint8_t)dir,           // data1: direction
		(uint8_t)acc_rpm,       // data2: accel (0-255, 0=direct start no ramp)
		(uint8_t)(rpm >> 8),    // data3: speed hi
		(uint8_t)(rpm & 0xFF),  // data4: speed lo
		(uint8_t)(pulse >> 24), // data5: pulse B3 (high)
		(uint8_t)(pulse >> 16), // data6: pulse B2
		(uint8_t)(pulse >> 8),  // data7: pulse B1
		(uint8_t)(pulse & 0xFF),// data8: pulse B0 (low)
		(uint8_t)mode,          // data9: mode (0 relative / 1 absolute)
		(uint8_t)sync           // data10: sync (0 async / 1 sync)
	};

	// append CRC16
	uint16_t crc = modbusCRC(cmd.data(), (int)cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));

	int attempt = 0;
	while (attempt <= retry) {
		char tx_label[48];
		if (attempt > 0)
			std::snprintf(tx_label, sizeof(tx_label), "TX Pos Mode (Retry %d)", attempt);
		else
			std::snprintf(tx_label, sizeof(tx_label), "TX Pos Mode");
		LOG_HEX(_log_tag, tx_label, cmd.data(), (int)cmd.size());

		if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) {
			LOG_ERR(_log_tag, "Send failed, waiting for retry");
			attempt++;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}

		auto resp = readEcho(300);

		if (resp.empty())
			LOG_ERR(_log_tag, "RX Pos Mode: TIMEOUT");
		else
			LOG_HEX(_log_tag, "RX Pos Mode", resp.data(), (int)resp.size());

		bool success = false;
		if (resp.size() == 8 && resp[1] == 0x10 && resp[3] == 0xFD) {
			uint16_t rx_crc = (resp[7] << 8) | resp[6];
			if (rx_crc == modbusCRC(resp.data(), 6)) {
				success = true;
			}
		}

		if (success) {
			return false; // no error
		}
		else {
			readEcho(500); // drain remaining data from buffer

			LOG_WRN(_log_tag, "RX Invalid or CRC Error. Releasing stall flag...");

			release_stall_flag();

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			attempt++;
		}
	}

	LOG_ERR(_log_tag, "Pos Mode failed after reaching max retry limit");
	return true; // error
}

//=========== control: misc triggers ===========

bool ZDT_motor_control::factory_reset() {
	// 3.1.5 factory reset: Func 0x06, Reg 0x000F, Data 0x0001
	auto cmd = build_write_single_register(0x000F, 0x0001);
	LOG_HEX(_log_tag, "TX factory_reset", cmd.data(), (int)cmd.size());
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(500);
	LOG_HEX(_log_tag, "RX factory_reset", resp.data(), (int)resp.size());
	return !(resp.size() > 0 && resp == cmd);
}

//=========== control: homing (3.3) ===========

bool ZDT_motor_control::trigger_home(int mode, bool sync) {
	// 3.3.2 trigger home: Func 0x06, Reg 0x009A(Emm), Data [mode, sync flag]
	// mode: 00=single-turn nearest 01=single-turn dir 02=no-limit collision 03=limit 04=absolute zero 05=power-loss
	uint16_t data = ((uint16_t)mode << 8) | (sync ? 0x01 : 0x00);
	auto cmd = build_write_single_register(0x009A, data);
	LOG_HEX(_log_tag, "TX trigger_home", cmd.data(), (int)cmd.size());
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(200);
	LOG_HEX(_log_tag, "RX trigger_home", resp.data(), (int)resp.size());
	return !(resp.size() > 0 && resp == cmd);
}

bool ZDT_motor_control::abort_home() {
	// 3.3.3 force abort home: Func 0x06, Reg 0x009C(Emm), Data [0x48, 0x00]
	auto cmd = build_write_single_register(0x009C, 0x4800);
	LOG_HEX(_log_tag, "TX abort_home", cmd.data(), (int)cmd.size());
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(200);
	LOG_HEX(_log_tag, "RX abort_home", resp.data(), (int)resp.size());
	return !(resp.size() > 0 && resp == cmd);
}

bool ZDT_motor_control::trigger_sync_move() {
	// 3.2.13 multi-axis sync move: Func 0x06, Reg 0x00FF, Data [0x66, 0x00]
	// sent with broadcast address 0x00
	std::vector<uint8_t> cmd = { 0x00, 0x06, 0x00, 0xFF, 0x66, 0x00 };
	uint16_t crc = modbusCRC(cmd.data(), (int)cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));
	LOG_HEX(_log_tag, "TX trigger_sync", cmd.data(), (int)cmd.size());
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(200);
	LOG_HEX(_log_tag, "RX trigger_sync", resp.data(), (int)resp.size());
	return resp.empty();
}

bool ZDT_motor_control::set_home_zero_position(bool store) {
	// 3.3.1 set single-turn home zero: Func 0x06, Reg 0x0093(Emm), Data [0x88, store flag]
	uint16_t data = (0x88 << 8) | (store ? 0x01 : 0x00);
	auto cmd = build_write_single_register(0x0093, data);
	LOG_HEX(_log_tag, "TX set_home_zero", cmd.data(), (int)cmd.size());
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(200);
	LOG_HEX(_log_tag, "RX set_home_zero", resp.data(), (int)resp.size());
	return !(resp.size() > 0 && resp == cmd);
}

//=========== utility: CRC / build / echo ===========

uint16_t ZDT_motor_control::modbusCRC(const uint8_t* data, int len) {
	uint16_t crc = 0xFFFF;
	for (int i = 0; i < len; i++) {
		crc ^= data[i];
		for (int j = 0; j < 8; j++) {
			if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
			else crc >>= 1;
		}
	}
	return crc;
}

std::vector<uint8_t> ZDT_motor_control::build_write_single_register(uint16_t reg_addr, uint16_t value) {
	std::vector<uint8_t> cmd = { slave_id, 0x06, (uint8_t)(reg_addr >> 8), (uint8_t)(reg_addr & 0xFF), (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
	uint16_t crc = modbusCRC(cmd.data(), (int)cmd.size());
	cmd.push_back(crc & 0xFF); cmd.push_back(crc >> 8);
	return cmd;
}

void ZDT_motor_control::printHex(const std::vector<uint8_t>& data, const std::string& prefix) {
	LOG_HEX(_log_tag, prefix.c_str(), data.data(), (int)data.size());
}

std::vector<uint8_t> ZDT_motor_control::readEcho(int timeout_ms) {
	uint8_t buf[128];
	int n = client->receiveData((char*)buf, sizeof(buf), timeout_ms);
	if (n <= 0) return {};
	return std::vector<uint8_t>(buf, buf + n);
}
