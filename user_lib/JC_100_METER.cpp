#include "JC_100_METER.h"
#include <iostream>
#include <iomanip>

JC_100_METER::JC_100_METER() : _slaveID(1), _debug(false), client(nullptr), _isExternalClient(false) {}

JC_100_METER::~JC_100_METER() {
	if (!_isExternalClient && client != nullptr) {
		delete client;
		client = nullptr;
	}
}

// 初始化: 透過現有連線注入
bool JC_100_METER::init(TCP_client& extClient, int ID, bool debug) {
	_slaveID = ID;
	_debug = debug;
	client = &extClient;
	_isExternalClient = true;
	return true;
}

// 初始化: 建立新連線
bool JC_100_METER::init(const std::string& ip, int port, int ID, bool debug) {
	_slaveID = ID; _debug = debug; _isExternalClient = false;
	if (client) delete client;
	client = new TCP_client();
	return client->connectToServer(ip, port, debug);
}

// --- 通訊邏輯 ---
bool JC_100_METER::send_command(uint8_t func, uint16_t reg, uint16_t data, std::vector<uint8_t>& res) {
	if (!client || !client->isConnected()) {
		error_flag = 1; // 連線斷開視為異常
		return false;
	}

	uint8_t frame[8] = { (uint8_t)_slaveID, func, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
						 (uint8_t)(data >> 8), (uint8_t)(data & 0xFF), 0, 0 };
	uint16_t crc = modbusCRC(frame, 6);
	frame[6] = crc & 0xFF; frame[7] = crc >> 8;

	if (_debug) log_hex("JC-100 TX -> ", frame, 8);

	if (!client->sendData((const char*)frame, 8, 500)) {
		error_flag = 1; // 發送失敗
		return false;
	}

	char rxBuf[256];
	int len = client->receiveData(rxBuf, 256, 1000); // 1秒 Timeout 設定

	if (len < 5) {
		error_flag = 1; // 收不到封包或長度不足 (TIMEOUT)
		if (_debug) std::cout << "JC-100 [TIMEOUT ERROR]" << std::endl;
		return false;
	}

	if (_debug) log_hex("JC-100 RX <- ", (uint8_t*)rxBuf, len);

	uint16_t cCrc = modbusCRC((uint8_t*)rxBuf, len - 2);
	if (cCrc != ((uint8_t)rxBuf[len - 2] | ((uint8_t)rxBuf[len - 1] << 8))) {
		error_flag = 1; // CRC 校驗失敗
		if (_debug) std::cout << "JC-100 [CRC ERROR]" << std::endl;
		return false;
	}

	// 若執行到此處，代表通訊完全成功
	error_flag = 0;
	res.assign((uint8_t*)rxBuf, (uint8_t*)rxBuf + len);
	return true;
}
void JC_100_METER::log_hex(const std::string& prefix, const uint8_t* data, int len) {
	std::cout << prefix << "[ ";
	for (int i = 0; i < len; ++i) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
	std::cout << "]" << std::dec << std::endl;
}

// --- 數據實作 ---
int JC_100_METER::read_pressure() {
	std::vector<uint8_t> r;
	// 呼叫 send_command，內部會根據通訊結果更新 error_flag
	if (send_command(0x03, 0x0001, 0x0001, r)) {
		// 通訊成功：更新最後一次壓力值並回傳
		_last_pressure = (int16_t)(r[3] << 8 | r[4]);
		usleep(5 * 1000);
		return _last_pressure;
	}
	else {
		// 通訊失敗：error_flag 已被設為 1，此時回傳上一筆成功的資料
		if (_debug) {
			std::cout << "[Warning] 通訊異常，回傳上一筆紀錄: " << _last_pressure << std::endl;
		}
		return _last_pressure;
	}
}

// Getter 系列
int JC_100_METER::get_response_time() { std::vector<uint8_t> r; return send_command(0x03, 0x0011, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
int JC_100_METER::get_pressure_unit() { std::vector<uint8_t> r; return send_command(0x03, 0x0012, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
int JC_100_METER::get_output_mode() { std::vector<uint8_t> r; return send_command(0x03, 0x0013, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
int JC_100_METER::get_output_logic() { std::vector<uint8_t> r; return send_command(0x03, 0x0014, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
int JC_100_METER::get_slave_address() { std::vector<uint8_t> r; return send_command(0x03, 0x0015, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
int JC_100_METER::get_baud_rate() { std::vector<uint8_t> r; return send_command(0x03, 0x0016, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
int JC_100_METER::get_hysteresis() { std::vector<uint8_t> r; return send_command(0x03, 0x0017, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
int JC_100_METER::get_setpoint_1() { std::vector<uint8_t> r; return send_command(0x03, 0x0018, 0x0001, r) ? (int16_t)(r[3] << 8 | r[4]) : -9999; }
int JC_100_METER::get_setpoint_2() { std::vector<uint8_t> r; return send_command(0x03, 0x0019, 0x0001, r) ? (int16_t)(r[3] << 8 | r[4]) : -9999; }
int JC_100_METER::get_display_color() { std::vector<uint8_t> r; return send_command(0x03, 0x001A, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }

// Setter 系列
bool JC_100_METER::set_response_time(int v) { std::vector<uint8_t> r; return send_command(0x06, 0x0011, v, r); }
bool JC_100_METER::set_pressure_unit(int v) { std::vector<uint8_t> r; return send_command(0x06, 0x0012, v, r); }
bool JC_100_METER::set_output_mode(int v) { std::vector<uint8_t> r; return send_command(0x06, 0x0013, v, r); }
bool JC_100_METER::set_output_logic(int v) { std::vector<uint8_t> r; return send_command(0x06, 0x0014, v, r); }
bool JC_100_METER::set_slave_address(int v) { std::vector<uint8_t> r; if (send_command(0x06, 0x0015, v, r)) { _slaveID = v; return true; } return false; }
bool JC_100_METER::set_baud_rate(int v) { std::vector<uint8_t> r; return send_command(0x06, 0x0016, v, r); }
bool JC_100_METER::set_hysteresis(int v) { std::vector<uint8_t> r; return send_command(0x06, 0x0017, v, r); }
bool JC_100_METER::set_setpoint_1(int v) { std::vector<uint8_t> r; return send_command(0x06, 0x0018, v, r); }
bool JC_100_METER::set_setpoint_2(int v) { std::vector<uint8_t> r; return send_command(0x06, 0x0019, v, r); }
bool JC_100_METER::set_display_color(int v) { std::vector<uint8_t> r; return send_command(0x06, 0x001A, v, r); }

bool JC_100_METER::zero_calibration() { std::vector<uint8_t> r; return send_command(0x06, 0x0020, 0x0001, r); }

uint16_t JC_100_METER::modbusCRC(const uint8_t* data, int len) {
	uint16_t crc = 0xFFFF;
	for (int i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i];
		for (int j = 8; j != 0; j--) {
			if ((crc & 0x0001) != 0) { crc >>= 1; crc ^= 0xA001; }
			else crc >>= 1;
		}
	}
	return crc;
}