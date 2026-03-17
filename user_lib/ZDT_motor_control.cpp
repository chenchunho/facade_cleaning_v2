#include "ZDT_motor_control.h"
#include <iostream>
#include <cstdio>
#include <thread>
#include <chrono>

const double PULSE_TO_DEG = 360.0 / 65536.0;

ZDT_motor_control::ZDT_motor_control() {}

ZDT_motor_control::~ZDT_motor_control() {
	if (!is_external_client && client != nullptr) {
		delete client;
		client = nullptr;
	}
}

bool ZDT_motor_control::init(const std::string& ip, int port, int ID, bool debug) {
	debug_mode = debug;
	slave_id = (uint8_t)ID;
	client = new TCP_client();
	is_external_client = false;
	bool connected = client->connectToServer(ip, port);
	if (debug_mode) std::cout << "[INIT] Connection " << (connected ? "SUCCESS" : "FAILED") << " ID: " << ID << "\n";
	return connected;
}

bool ZDT_motor_control::init(TCP_client& extClient, int ID, bool debug) {
	debug_mode = debug;
	slave_id = (uint8_t)ID;
	this->client = &extClient;
	is_external_client = true;
	if (debug_mode) std::cout << "[INIT] External Client bound to ID: " << ID << "\n";
	return true;
}

bool ZDT_motor_control::set_zero() {
	auto cmd = build_write_single_register(0x000A, 0x0001);
	if (debug_mode) printHex(cmd, "[TX set_zero]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return false;
	auto resp = readEcho(200);
	if (debug_mode) printHex(resp, "[RX set_zero]");
	return (!resp.empty() && resp == cmd);
}

bool ZDT_motor_control::calibrate_encoder() {
	auto cmd = build_write_single_register(0x0006, 0x0001);
	if (debug_mode) printHex(cmd, "[TX calibrate_encoder]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return false;
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	auto resp = readEcho(500);
	if (debug_mode) printHex(resp, "[RX calibrate_encoder]");
	return (!resp.empty() && resp == cmd);
}

bool ZDT_motor_control::reset_motor() {
	auto cmd = build_write_single_register(0x0008, 0x0001);
	if (debug_mode) printHex(cmd, "[TX reset_motor]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return false;
	auto resp = readEcho(200);
	if (debug_mode) printHex(resp, "[RX reset_motor]");
	return (!resp.empty() && resp == cmd);
}

bool ZDT_motor_control::motion_control_driver_EN(bool status) {
	std::vector<uint8_t> cmd = {
		slave_id, 0x10, 0x00, 0xF3, 0x00, 0x02, 0x04, 0xAB,
		(uint8_t)(status ? 0x01 : 0x00), 0x00, 0x00
	};
	uint16_t crc = modbusCRC(cmd.data(), (int)cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));

	if (debug_mode) printHex(cmd, status ? "[TX Driver EN ON]" : "[TX Driver EN OFF]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return false;
	auto resp = readEcho(200);
	if (debug_mode) printHex(resp, "[RX Driver EN]");
	return (resp.size() == 8 && resp[1] == 0x10);
}

bool ZDT_motor_control::get_system_status() {
	// TX: 01 04 00 43 00 10 00 12 (根據您的 Log)
	std::vector<uint8_t> cmd = { slave_id, 0x04, 0x00, 0x43, 0x00, 0x10, 0x00, 0x12 };

	if (debug_mode) printHex(cmd, "[TX get_status]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return false;

	auto resp = readEcho(300);
	if (debug_mode) printHex(resp, "[RX get_status]");

	// 必須符合 37 bytes: 01 04 20 (3) + Data (32) + CRC (2)
	if (resp.size() < 37) return false;

	int p = 3; // 跳過 Addr, Func, Len
	auto get_u8 = [&](int& pos) { return resp[pos++]; };
	auto get_u16 = [&](int& pos) { uint16_t v = (resp[pos] << 8) | resp[pos + 1]; pos += 2; return v; };
	auto get_u32 = [&](int& pos) {
		uint32_t v = (uint32_t)((resp[pos] << 24) | (resp[pos + 1] << 16) | (resp[pos + 2] << 8) | resp[pos + 3]);
		pos += 4; return v;
	};

	const double TO_DEG = 360.0 / 65536.0;

	// --- 數據區開始 ---
	get_u8(p); // 23
	get_u8(p); // 09
	this->status.bus_voltage = get_u16(p); // 5E 49
	this->status.bus_current = get_u16(p); // 00 04
	this->status.encoder_linear = get_u16(p); // 4F 85

	// 目標位置 (符號2 + 數值4)
	uint16_t t_sign = get_u16(p);
	uint32_t t_pulse = get_u32(p);
	this->status.target_pos = (t_sign == 1 ? -1.0 : 1.0) * t_pulse * TO_DEG;

	// 實時轉速 (符號2 + 數值2)
	uint16_t s_sign = get_u16(p);
	uint16_t s_val = get_u16(p);
	this->status.real_speed = (s_sign == 1 ? -1.0 : 1.0) * s_val;

	// 實時位置 (符號2 + 數值4)
	uint16_t r_sign = get_u16(p);
	uint32_t r_pulse = get_u32(p);
	this->status.real_pos = (r_sign == 1 ? -1.0 : 1.0) * r_pulse * TO_DEG;

	// 位置誤差 (符號2 + 數值4)
	uint16_t e_sign = get_u16(p);
	uint32_t e_pulse = get_u32(p);
	this->status.pos_error = (e_sign == 1 ? -1.0 : 1.0) * e_pulse * TO_DEG;

	// --- 狀態位元精確修正 ---
	// 根據您的 Log: ... 00 05 03 8F 18 12
	// 00 05 是最後一個 u16 (可能是誤差的一部分或保留位)
	// 03 8F 是我們要的狀態位元

	p = 33; // 強制將指針移向最後 4 byte 前 (Data 區的最後兩個暫存器)
	uint16_t state_reg = get_u16(p); // 讀到 03 8F

	uint8_t hB = (uint8_t)(state_reg >> 8);   // High Byte = 03
	uint8_t mB = (uint8_t)(state_reg & 0xFF); // Low Byte  = 8F

	// --- 回零狀態 (hB = 03 -> 0000 0011) ---
	this->status.encoder_ready = (hB >> 0) & 0x01; // bit 0: 1
	this->status.calibration_ready = (hB >> 1) & 0x01; // bit 1: 1
	this->status.is_homing = (hB >> 2) & 0x01; // bit 2: 0
	this->status.home_failed = (hB >> 3) & 0x01; // bit 3: 0
	this->status.over_temp_flag = (hB >> 4) & 0x01; // bit 4: 0
	this->status.over_current_flag = (hB >> 5) & 0x01; // bit 5: 0

	// --- 電機狀態 (mB = 8F -> 1000 1111) ---
	this->status.is_enabled = (mB >> 0) & 0x01; // bit 0: 1 (使能)
	this->status.pos_reached = (mB >> 1) & 0x01; // bit 1: 1 (到位)
	this->status.stall_flag = (mB >> 2) & 0x01; // bit 2: 1 (堵轉)
	this->status.stall_protection = (mB >> 3) & 0x01; // bit 3: 1 (堵保)
	this->status.left_limit = (mB >> 4) & 0x01; // bit 4: 0
	this->status.right_limit = (mB >> 5) & 0x01; // bit 5: 0
	this->status.power_loss_flag = (mB >> 7) & 0x01; // bit 7: 1 (掉電)

	return true;
}

bool ZDT_motor_control::release_stall_flag() {
	// 根據您的需求：寫入暫存器 0x000E，值為 0x0001
	auto cmd = build_write_single_register(0x000E, 0x0001);

	if (debug_mode) printHex(cmd, "[TX release_stall]");

	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return false;

	auto resp = readEcho(200);

	if (debug_mode) printHex(resp, "[RX release_stall]");

	// 標準 Modbus 06 功能碼回應應與發送指令相同
	return (!resp.empty() && resp == cmd);
}

bool ZDT_motor_control::emergency_stop(bool sync) {
	// 根據您的 TX 範例：Addr 01, Func 10, Reg 00FE, Qty 0001, Len 02, Data 98XX
	std::vector<uint8_t> cmd = {
		slave_id,
		0x10,           // 功能碼 10 (寫入多個暫存器)
		0x00, 0xFE,     // 暫存器地址 0x00FE
		0x00, 0x01,     // 暫存器數量 0x0001
		0x02,           // 數據長度 2 Bytes
		0x98,           // 數據高位固定為 0x98
		(uint8_t)(sync ? 0x01 : 0x00) // 數據低位：同步標誌
	};

	uint16_t crc = modbusCRC(cmd.data(), (int)cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));

	if (debug_mode) printHex(cmd, "[TX emergency_stop]");

	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return false;

	auto resp = readEcho(200);
	if (debug_mode) printHex(resp, "[RX emergency_stop]");

	// 回應檢查：地址、功能碼、暫存器地址與數量是否相符
	return (resp.size() == 8 && resp[1] == 0x10 && resp[3] == 0xFE);
}

bool ZDT_motor_control::motion_control_speed_mode(int dir, int acc_rpm, int rpm, int sync, int retry) {
	// 1. 構建寫入多個暫存器 (0x10) 指令封包，地址 0x00F6
	std::vector<uint8_t> cmd = {
		slave_id, 0x10, 0x00, 0xF6, 0x00, 0x03, 0x06,
		(uint8_t)dir,           // 數據1: 方向
		(uint8_t)acc_rpm,       // 數據2: 加速度
		(uint8_t)(rpm >> 8),    // 數據3: 速度高8位
		(uint8_t)(rpm & 0xFF),  // 數據4: 速度低8位
		(uint8_t)(sync >> 8),   // 數據5: 同步高8位
		(uint8_t)(sync & 0xFF)  // 數據6: 同步低8位
	};

	uint16_t crc = modbusCRC(cmd.data(), (int)cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));

	int attempt = 0;
	while (attempt <= retry) {
		// --- TX LOG ---
		std::string tx_label = "[TX Speed Mode]";
		if (attempt > 0) tx_label += " (Retry " + std::to_string(attempt) + ")";
		if (debug_mode) printHex(cmd, tx_label);

		// 發送數據
		if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) {
			if (debug_mode) std::cout << "[ERROR] Send failed, waiting for retry..." << std::endl;
			attempt++;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}

		// 讀取回應
		auto resp = readEcho(200);

		// --- RX LOG ---
		if (debug_mode) {
			if (resp.empty()) std::cout << "[RX Speed Mode]: TIMEOUT" << std::endl;
			else printHex(resp, "[RX Speed Mode]");
		}

		// --- 檢查回應正確性 (功能碼 0x10, 起始地址 0x00F6, 數量 0x0003) ---
		bool success = false;
		if (resp.size() == 8 && resp[1] == 0x10 && resp[3] == 0xF6) {
			uint16_t rx_crc = (resp[7] << 8) | resp[6];
			if (rx_crc == modbusCRC(resp.data(), 6)) {
				success = true;
			}
		}

		if (success) {
			return true;
		}
		else {
			// --- 異常處理：執行解除堵轉並準備重試 ---
			if (debug_mode) std::cout << "[WARN] RX Invalid or Error. Executing release_stall_flag..." << std::endl;

			release_stall_flag(); // 執行指令 01 06 00 0E 00 01

			//std::this_thread::sleep_for(std::chrono::milliseconds(100));
			attempt++;
		}
	}

	if (debug_mode) std::cout << "[FATAL] Speed Mode failed after reaching max retry limit." << std::endl;
	return false;
}

bool ZDT_motor_control::motion_control_pos_mode(int dir, int acc_rpm, int rpm, int pulse, int mode, int sync, int retry) {
	// 1. 構建寫入多個暫存器 (0x10) 指令封包
	std::vector<uint8_t> cmd = {
		slave_id, 0x10, 0x00, 0xFD, 0x00, 0x05, 0x0A,
		(uint8_t)dir,           // 數據1: 方向
		(uint8_t)acc_rpm,       // 數據2: 加速度
		(uint8_t)(rpm >> 8),    // 數據3: 速度高位
		(uint8_t)(rpm & 0xFF),  // 數據4: 速度低位
		(uint8_t)(pulse >> 24), // 數據5: 脈衝數 Byte3 (高)
		(uint8_t)(pulse >> 16), // 數據6: 脈衝數 Byte2
		(uint8_t)(pulse >> 8),  // 數據7: 脈衝數 Byte1
		(uint8_t)(pulse & 0xFF),// 數據8: 脈衝數 Byte0 (低)
		(uint8_t)mode,          // 數據9: 模式 (0相對/1絕對)
		(uint8_t)sync           // 數據10: 同步 (0非同步/1同步)
	};

	// 計算並附加 CRC16
	uint16_t crc = modbusCRC(cmd.data(), (int)cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));

	int attempt = 0;
	while (attempt <= retry) {
		// --- TX LOG ---
		std::string tx_label = "[TX Pos Mode]";
		if (attempt > 0) tx_label += " (Retry " + std::to_string(attempt) + ")";
		if (debug_mode) printHex(cmd, tx_label);

		// 發送數據
		if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) {
			if (debug_mode) std::cout << "[ERROR] Send failed, waiting for retry..." << std::endl;
			attempt++;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}

		// 讀取回應
		auto resp = readEcho(300);

		// --- RX LOG ---
		if (debug_mode) {
			if (resp.empty()) std::cout << "[RX Pos Mode]: TIMEOUT" << std::endl;
			else printHex(resp, "[RX Pos Mode]");
		}

		// --- 檢查回應正確性 ---
		bool success = false;
		if (resp.size() == 8 && resp[1] == 0x10 && resp[3] == 0xFD) {
			// CRC 驗證確保通訊無誤
			uint16_t rx_crc = (resp[7] << 8) | resp[6];
			if (rx_crc == modbusCRC(resp.data(), 6)) {
				success = true;
			}
		}

		if (success) {
			return true; // 成功接收正確回應
		}
		else {
			// --- 異常處理：執行解除堵轉並準備重試 ---
			if (debug_mode) std::cout << "[WARN] RX Invalid or CRC Error. Releasing stall flag..." << std::endl;

			// 呼叫解除堵轉 (內部也有自己的 TX/RX LOG)
			release_stall_flag();

			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 給予馬達處理緩衝
			attempt++;
		}
	}

	if (debug_mode) std::cout << "[FATAL] Pos Mode failed after reaching max retry limit." << std::endl;
	return false;
}

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
	std::cout << prefix << ": ";
	if (data.empty()) { std::cout << "EMPTY\n"; return; }
	for (auto b : data) printf("%02X ", b);
	std::cout << std::endl;
}

std::vector<uint8_t> ZDT_motor_control::readEcho(int timeout_ms) {
	uint8_t buf[128];
	int n = client->receiveData((char*)buf, sizeof(buf), timeout_ms);
	if (n <= 0) return {};
	return std::vector<uint8_t>(buf, buf + n);
}