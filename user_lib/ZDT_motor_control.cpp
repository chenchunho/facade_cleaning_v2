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
	return !connected;
}

bool ZDT_motor_control::init(TCP_client& extClient, int ID, bool debug) {
	debug_mode = debug;
	slave_id = (uint8_t)ID;
	this->client = &extClient;
	is_external_client = true;
	if (debug_mode) std::cout << "[INIT] External Client bound to ID: " << ID << "\n";
	return false;
}

bool ZDT_motor_control::set_zero() {
	auto cmd = build_write_single_register(0x000A, 0x0001);
	if (debug_mode) printHex(cmd, "[TX set_zero]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(200);
	if (debug_mode) printHex(resp, "[RX set_zero]");
	return !(resp.size() > 0 && resp == cmd);
}

bool ZDT_motor_control::calibrate_encoder() {
	auto cmd = build_write_single_register(0x0006, 0x0001);
	if (debug_mode) printHex(cmd, "[TX calibrate_encoder]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	auto resp = readEcho(500);
	if (debug_mode) printHex(resp, "[RX calibrate_encoder]");
	return !(resp.size() > 0 && resp == cmd);
}

bool ZDT_motor_control::reset_motor() {
	auto cmd = build_write_single_register(0x0008, 0x0001);
	if (debug_mode) printHex(cmd, "[TX reset_motor]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(200);
	if (debug_mode) printHex(resp, "[RX reset_motor]");
	return !(resp.size() > 0 && resp == cmd);
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
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(200);
	if (debug_mode) printHex(resp, "[RX Driver EN]");
	return !(resp.size() == 8 && resp[1] == 0x10);
}

bool ZDT_motor_control::get_system_status() {
	// 3.7.2 讀取系統狀��參數（Emm）: Func 0x04, Reg 0x0043, Qty 0x0010
	std::vector<uint8_t> cmd = { slave_id, 0x04, 0x00, 0x43, 0x00, 0x10 };
	uint16_t crc = modbusCRC(cmd.data(), (int)cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));

	if (debug_mode) printHex(cmd, "[TX get_status]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;

	auto resp = readEcho(300);
	if (debug_mode) printHex(resp, "[RX get_status]");

	// Emm 回應: Addr(1) + Func(1) + ByteCount(1) + Data(32) + CRC(2) = 37 bytes
	if (resp.size() < 37) return true;

	int p = 3; // 跳過 Addr, Func, ByteCount
	auto get_u16 = [&](int& pos) {
		uint16_t v = ((uint16_t)resp[pos] << 8) | resp[pos + 1]; pos += 2; return v;
	};
	auto get_u32 = [&](int& pos) {
		uint32_t v = ((uint32_t)resp[pos] << 24) | ((uint32_t)resp[pos + 1] << 16)
		           | ((uint32_t)resp[pos + 2] << 8) | resp[pos + 3];
		pos += 4; return v;
	};

	const double EMM_TO_DEG = 360.0 / 65536.0;

	// Reg 1: [總字節數, 參數個數] — 跳過
	get_u16(p);

	// Reg 2: 總線電壓 (mV)
	this->status.bus_voltage = get_u16(p);

	// Reg 3: 相電流 (mA) — Emm 批量讀取無總線電流欄位
	this->status.phase_current = get_u16(p);
	this->status.bus_current = 0;

	// Reg 4: 線性化編碼器值 — Emm 批量讀取無原始編碼器值
	this->status.encoder_linear = get_u16(p);
	this->status.encoder_raw = 0;

	// Reg 5-7: 目標位置 (符號 u16 + 數值 u32)
	uint16_t t_sign = get_u16(p);
	uint32_t t_val  = get_u32(p);
	this->status.target_pos = (t_sign == 1 ? -1.0 : 1.0) * t_val * EMM_TO_DEG;

	// Reg 8-9: 實時轉速 (符號 u16 + 數值 u16, Emm 單位為 RPM)
	uint16_t s_sign = get_u16(p);
	uint16_t s_val  = get_u16(p);
	this->status.real_speed = (s_sign == 1 ? -1.0 : 1.0) * s_val;

	// Reg 10-12: 實時位置 (符號 u16 + 數值 u32)
	uint16_t r_sign = get_u16(p);
	uint32_t r_val  = get_u32(p);
	this->status.real_pos = (r_sign == 1 ? -1.0 : 1.0) * r_val * EMM_TO_DEG;

	// Reg 13-15: 位置誤差 (符號 u16 + 數值 u32)
	uint16_t e_sign = get_u16(p);
	uint32_t e_val  = get_u32(p);
	this->status.pos_error = (e_sign == 1 ? -1.0 : 1.0) * e_val * EMM_TO_DEG;

	// Reg 16: [回零狀態標誌(H), 電機狀態標誌(L)]
	uint16_t state_reg = get_u16(p);
	uint8_t home_flags  = (uint8_t)(state_reg >> 8);
	uint8_t motor_flags = (uint8_t)(state_reg & 0xFF);

	// 回零狀態標誌 (3.3.4)
	this->status.encoder_ready     = (home_flags >> 0) & 0x01;
	this->status.calibration_ready = (home_flags >> 1) & 0x01;
	this->status.is_homing         = (home_flags >> 2) & 0x01;
	this->status.home_failed       = (home_flags >> 3) & 0x01;
	this->status.over_temp_flag    = (home_flags >> 4) & 0x01;
	this->status.over_current_flag = (home_flags >> 5) & 0x01;

	// 電機狀態標誌 (3.4.14)
	this->status.is_enabled       = (motor_flags >> 0) & 0x01;
	this->status.pos_reached      = (motor_flags >> 1) & 0x01;
	this->status.stall_flag       = (motor_flags >> 2) & 0x01;
	this->status.stall_protection = (motor_flags >> 3) & 0x01;
	this->status.left_limit       = (motor_flags >> 4) & 0x01;
	this->status.right_limit      = (motor_flags >> 5) & 0x01;
	this->status.power_loss_flag  = (motor_flags >> 7) & 0x01;

	// Emm 批量讀取不包含溫度
	this->status.temperature = 0;

	return false;
}

bool ZDT_motor_control::wait_until_pos_reached(int timeout_ms, int poll_interval_ms) {
	auto start = std::chrono::steady_clock::now();
	while (true) {
		if (!get_system_status() && status.pos_reached) {
			return false;
		}
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - start).count();
		if (elapsed >= timeout_ms) {
			if (debug_mode) std::cout << "[TIMEOUT] wait_until_pos_reached: " << elapsed << " ms" << std::endl;
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
	}
}

bool ZDT_motor_control::release_stall_flag() {
	// 根據您的需求：寫入暫存器 0x000E，值為 0x0001
	auto cmd = build_write_single_register(0x000E, 0x0001);

	if (debug_mode) printHex(cmd, "[TX release_stall]");

	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;

	auto resp = readEcho(200);

	if (debug_mode) printHex(resp, "[RX release_stall]");

	// 標準 Modbus 06 功能碼回應應與發送指令相同
	return !(resp.size() > 0 && resp == cmd);
}

bool ZDT_motor_control::emergency_stop(bool sync) {
	// 3.2.12 立即停止: Func 0x06, Reg 0x00FE, Data [0x98, 同步標誌]
	uint16_t data = (0x98 << 8) | (sync ? 0x01 : 0x00);
	auto cmd = build_write_single_register(0x00FE, data);

	if (debug_mode) printHex(cmd, "[TX emergency_stop]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;

	auto resp = readEcho(200);
	if (debug_mode) printHex(resp, "[RX emergency_stop]");

	// 0x06 回應為指令回波
	return !(resp.size() > 0 && resp == cmd);
}

bool ZDT_motor_control::motion_control_speed_mode(int dir, int acc_rpm, int rpm, int sync, int retry) {
	// 1. 構建寫入多個暫存器 (0x10) 指令封包，地址 0x00F6
	std::vector<uint8_t> cmd = {
		slave_id, 0x10, 0x00, 0xF6, 0x00, 0x03, 0x06,
		(uint8_t)dir,           // 寄存器1 H: 方向 (00:CW / 01:CCW)
		(uint8_t)acc_rpm,       // 寄存器1 L: 加速度 (0-255, 0=direct start no ramp)
		(uint8_t)(rpm >> 8),    // 寄存器2 H: 速度高8位
		(uint8_t)(rpm & 0xFF),  // 寄存器2 L: 速度低8位
		(uint8_t)sync,          // 寄存器3 H: 同步標誌 (00:立即 / 01:緩存)
		0x00                    // 寄存器3 L: 固定 0x00
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
			return false; // no error
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
	return true; // error
}

bool ZDT_motor_control::motion_control_pos_mode(int dir, int acc_rpm, int rpm, int pulse, int mode, int sync, int retry) {
	// mode: 0=relative, 1=absolute (only valid values)
	if (mode != 0 && mode != 1) {
		if (debug_mode) std::cout << "[ERROR] Invalid mode " << mode << ", must be 0 (relative) or 1 (absolute)" << std::endl;
		return true;
	}

	// 1. 構建寫入多個暫存器 (0x10) 指令封包
	std::vector<uint8_t> cmd = {
		slave_id, 0x10, 0x00, 0xFD, 0x00, 0x05, 0x0A,
		(uint8_t)dir,           // 數據1: 方向
		(uint8_t)acc_rpm,       // 數據2: 加速度 (0-255, 0=direct start no ramp)
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
			// 等待移動完成
			if (!wait_until_pos_reached()) {
				return false; // no error
			}
			else {
				if (debug_mode) std::cout << "[WARN] Waiting for moving timeout..." << std::endl;
				return true; // error: timeout
			}
		}
		else {
			readEcho(500); // drain remaining data from buffer

			// --- 異常處理：執行解除堵轉並準備重試 ---
			if (debug_mode) std::cout << "[WARN] RX Invalid or CRC Error. Releasing stall flag..." << std::endl;

			// 呼叫解除堵轉 (內部也有自己的 TX/RX LOG)
			release_stall_flag();

			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 給予馬達處理緩衝
			attempt++;
		}
	}

	if (debug_mode) std::cout << "[FATAL] Pos Mode failed after reaching max retry limit." << std::endl;
	return true; // error
}

bool ZDT_motor_control::motion_control_pos_mode_nowait(int dir, int acc_rpm, int rpm, int pulse, int mode, int sync, int retry) {
	// mode: 0=relative, 1=absolute (only valid values)
	if (mode != 0 && mode != 1) {
		if (debug_mode) std::cout << "[ERROR] Invalid mode " << mode << ", must be 0 (relative) or 1 (absolute)" << std::endl;
		return true;
	}

	// 1. 構建寫入多個暫存器 (0x10) 指令封包
	std::vector<uint8_t> cmd = {
		slave_id, 0x10, 0x00, 0xFD, 0x00, 0x05, 0x0A,
		(uint8_t)dir,           // 數據1: 方向
		(uint8_t)acc_rpm,       // 數據2: 加速度 (0-255, 0=direct start no ramp)
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
			return false; // no error
		}
		else {
			readEcho(500); // drain remaining data from buffer

			// --- 異常處理：執行解除堵轉並準備重試 ---
			if (debug_mode) std::cout << "[WARN] RX Invalid or CRC Error. Releasing stall flag..." << std::endl;

			// 呼叫解除堵轉 (內部也有自己的 TX/RX LOG)
			release_stall_flag();

			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 給予馬達處理緩衝
			attempt++;
		}
	}

	if (debug_mode) std::cout << "[FATAL] Pos Mode failed after reaching max retry limit." << std::endl;
	return true; // error
}

bool ZDT_motor_control::factory_reset() {
	// 3.1.5 恢復出廠設置: Func 0x06, Reg 0x000F, Data 0x0001
	auto cmd = build_write_single_register(0x000F, 0x0001);
	if (debug_mode) printHex(cmd, "[TX factory_reset]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(500);
	if (debug_mode) printHex(resp, "[RX factory_reset]");
	return !(resp.size() > 0 && resp == cmd);
}

bool ZDT_motor_control::trigger_home(int mode, bool sync) {
	// 3.3.2 觸發回零: Func 0x06, Reg 0x009A(Emm), Data [回零模式, 同步標誌]
	// mode: 00=單圈就近 01=單圈方向 02=無限位碰撞 03=限位 04=絕對零點 05=掉電位置
	uint16_t data = ((uint16_t)mode << 8) | (sync ? 0x01 : 0x00);
	auto cmd = build_write_single_register(0x009A, data);
	if (debug_mode) printHex(cmd, "[TX trigger_home]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(200);
	if (debug_mode) printHex(resp, "[RX trigger_home]");
	return !(resp.size() > 0 && resp == cmd);
}

bool ZDT_motor_control::abort_home() {
	// 3.3.3 強制中斷回零: Func 0x06, Reg 0x009C(Emm), Data [0x48, 0x00]
	auto cmd = build_write_single_register(0x009C, 0x4800);
	if (debug_mode) printHex(cmd, "[TX abort_home]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(200);
	if (debug_mode) printHex(resp, "[RX abort_home]");
	return !(resp.size() > 0 && resp == cmd);
}

bool ZDT_motor_control::trigger_sync_move() {
	// 3.2.13 觸發多機同步運動: Func 0x06, Reg 0x00FF, Data [0x66, 0x00]
	// 以廣播地址 0x00 發送
	std::vector<uint8_t> cmd = { 0x00, 0x06, 0x00, 0xFF, 0x66, 0x00 };
	uint16_t crc = modbusCRC(cmd.data(), (int)cmd.size());
	cmd.push_back((uint8_t)(crc & 0xFF));
	cmd.push_back((uint8_t)(crc >> 8));
	if (debug_mode) printHex(cmd, "[TX trigger_sync]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(200);
	if (debug_mode) printHex(resp, "[RX trigger_sync]");
	return resp.empty();
}

bool ZDT_motor_control::set_home_zero_position(bool store) {
	// 3.3.1 設置單圈回零零點位置: Func 0x06, Reg 0x0093(Emm), Data [0x88, 是否存儲]
	uint16_t data = (0x88 << 8) | (store ? 0x01 : 0x00);
	auto cmd = build_write_single_register(0x0093, data);
	if (debug_mode) printHex(cmd, "[TX set_home_zero]");
	if (!client->sendData((char*)cmd.data(), (int)cmd.size(), 100)) return true;
	auto resp = readEcho(200);
	if (debug_mode) printHex(resp, "[RX set_home_zero]");
	return !(resp.size() > 0 && resp == cmd);
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