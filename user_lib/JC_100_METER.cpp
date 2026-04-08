#include "JC_100_METER.h"
#include <iostream>
#include <iomanip>

// MODBUS 寄存器位址 (依據 JC-100-RS485 說明書)
namespace JC100_REG {
	constexpr uint16_t PRESSURE      = 0x0001;  // 當前氣壓值 (R)
	constexpr uint16_t SETPOINT      = 0x0010;  // OUT1目標值 (R/W)
	constexpr uint16_t UPPER_LIMIT   = 0x0011;  // OUT1上限值 (R/W)
	constexpr uint16_t LOWER_LIMIT   = 0x0012;  // OUT1下限值 (R/W)
	constexpr uint16_t OUTPUT_MODE   = 0x0013;  // 輸出模式 EASY/HYS/WCMP (R/W)
	constexpr uint16_t DISPLAY_COLOR = 0x0014;  // 顯示顏色 R_ON/G_ON/RED/GREEN (R/W)
	constexpr uint16_t PRESSURE_UNIT = 0x0015;  // 壓力單位 MPa/kPa/... (R/W)
	constexpr uint16_t NO_NC         = 0x0016;  // 常開常閉 NO/NC (R/W)
	constexpr uint16_t RESPONSE_TIME = 0x0017;  // 反應時間 2.5ms~5000ms (R/W)
	constexpr uint16_t HYSTERESIS    = 0x0018;  // 應差固定值 1~8級 (R/W)
	constexpr uint16_t ECO_MODE      = 0x0019;  // ECO模式 OFF/Std/FULL (R/W)
	constexpr uint16_t SWITCH_STATUS = 0x001A;  // 開關量輸出狀態 (R)
	constexpr uint16_t ZERO_CAL      = 0x0020;  // 校零設定 (W)
}

JC_100_METER::JC_100_METER() : _slaveID(1), _debug(false), client(nullptr), _isExternalClient(false), error_flag(0) {}

JC_100_METER::~JC_100_METER() {
	if (!_isExternalClient && client != nullptr) {
		delete client;
		client = nullptr;
	}
}

bool JC_100_METER::init(TCP_client& extClient, int ID, bool debug) {
	_slaveID = ID;
	_debug = debug;
	client = &extClient;
	_isExternalClient = true;
	return true;
}

bool JC_100_METER::init(const std::string& ip, int port, int ID, bool debug) {
	_slaveID = ID;
	_debug = debug;
	_isExternalClient = false;
	if (client) delete client;
	client = new TCP_client();
	return client->connectToServer(ip, port, debug);
}

// ============================================================
//  Modbus 底層通訊
// ============================================================

bool JC_100_METER::send_command(uint8_t func, uint16_t reg, uint16_t data, std::vector<uint8_t>& res) {
	if (!client || !client->isConnected()) {
		error_flag = 1;
		return false;
	}

	uint8_t frame[8] = {
		(uint8_t)_slaveID, func,
		(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
		(uint8_t)(data >> 8), (uint8_t)(data & 0xFF),
		0, 0
	};
	uint16_t crc = modbusCRC(frame, 6);
	frame[6] = crc & 0xFF;
	frame[7] = crc >> 8;

	if (_debug) log_hex("JC-100 TX -> ", frame, 8);

	if (!client->sendData((const char*)frame, 8, 500)) {
		error_flag = 1;
		return false;
	}

	char rxBuf[256];
	int len = client->receiveData(rxBuf, 256, 1000);

	if (len < 5) {
		error_flag = 1;
		if (_debug) std::cout << "JC-100 [TIMEOUT ERROR]" << std::endl;
		return false;
	}

	if (_debug) log_hex("JC-100 RX <- ", (uint8_t*)rxBuf, len);

	uint16_t cCrc = modbusCRC((uint8_t*)rxBuf, len - 2);
	uint16_t rCrc = (uint8_t)rxBuf[len - 2] | ((uint8_t)rxBuf[len - 1] << 8);
	if (cCrc != rCrc) {
		error_flag = 1;
		if (_debug) std::cout << "JC-100 [CRC ERROR]" << std::endl;
		return false;
	}

	error_flag = 0;
	res.assign((uint8_t*)rxBuf, (uint8_t*)rxBuf + len);
	return true;
}

void JC_100_METER::log_hex(const std::string& prefix, const uint8_t* data, int len) {
	std::cout << prefix << "[ ";
	for (int i = 0; i < len; ++i)
		std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
	std::cout << "]" << std::dec << std::endl;
}

// ============================================================
//  即時數據 (0x0001)
// ============================================================

int JC_100_METER::read_pressure() {
	std::vector<uint8_t> r;
	if (send_command(0x03, JC100_REG::PRESSURE, 0x0001, r)) {
		_last_pressure = (int16_t)(r[3] << 8 | r[4]);
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		return _last_pressure;
	}
	else {
		if (_debug) std::cout << "[Warning] 通訊異常，回傳上一筆紀錄: " << _last_pressure << std::endl;
		return _last_pressure;
	}
}

// ============================================================
//  OUT1 設定 (0x0010~0x0012) — 值為帶符號 int16
// ============================================================

int  JC_100_METER::get_setpoint()    { std::vector<uint8_t> r; return send_command(0x03, JC100_REG::SETPOINT,    0x0001, r) ? (int16_t)(r[3] << 8 | r[4]) : -9999; }
bool JC_100_METER::set_setpoint(int v)    { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::SETPOINT,    (uint16_t)v, r); }

int  JC_100_METER::get_upper_limit() { std::vector<uint8_t> r; return send_command(0x03, JC100_REG::UPPER_LIMIT, 0x0001, r) ? (int16_t)(r[3] << 8 | r[4]) : -9999; }
bool JC_100_METER::set_upper_limit(int v) { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::UPPER_LIMIT, (uint16_t)v, r); }

int  JC_100_METER::get_lower_limit() { std::vector<uint8_t> r; return send_command(0x03, JC100_REG::LOWER_LIMIT, 0x0001, r) ? (int16_t)(r[3] << 8 | r[4]) : -9999; }
bool JC_100_METER::set_lower_limit(int v) { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::LOWER_LIMIT, (uint16_t)v, r); }

// ============================================================
//  輸出設定 (0x0013, 0x0016)
// ============================================================

int  JC_100_METER::get_output_mode() { std::vector<uint8_t> r; return send_command(0x03, JC100_REG::OUTPUT_MODE, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
bool JC_100_METER::set_output_mode(int v) { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::OUTPUT_MODE, (uint16_t)v, r); }

int  JC_100_METER::get_no_nc()       { std::vector<uint8_t> r; return send_command(0x03, JC100_REG::NO_NC,       0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
bool JC_100_METER::set_no_nc(int v)       { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::NO_NC,       (uint16_t)v, r); }

// ============================================================
//  顯示設定 (0x0014, 0x0015)
// ============================================================

int  JC_100_METER::get_display_color()  { std::vector<uint8_t> r; return send_command(0x03, JC100_REG::DISPLAY_COLOR, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
bool JC_100_METER::set_display_color(int v)  { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::DISPLAY_COLOR, (uint16_t)v, r); }

int  JC_100_METER::get_pressure_unit()  { std::vector<uint8_t> r; return send_command(0x03, JC100_REG::PRESSURE_UNIT, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
bool JC_100_METER::set_pressure_unit(int v)  { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::PRESSURE_UNIT, (uint16_t)v, r); }

// ============================================================
//  控制參數 (0x0017~0x0019)
// ============================================================

int  JC_100_METER::get_response_time() { std::vector<uint8_t> r; return send_command(0x03, JC100_REG::RESPONSE_TIME, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
bool JC_100_METER::set_response_time(int v) { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::RESPONSE_TIME, (uint16_t)v, r); }

int  JC_100_METER::get_hysteresis()    { std::vector<uint8_t> r; return send_command(0x03, JC100_REG::HYSTERESIS,    0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
bool JC_100_METER::set_hysteresis(int v)    { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::HYSTERESIS,    (uint16_t)v, r); }

int  JC_100_METER::get_eco_mode()      { std::vector<uint8_t> r; return send_command(0x03, JC100_REG::ECO_MODE,      0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
bool JC_100_METER::set_eco_mode(int v)      { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::ECO_MODE,      (uint16_t)v, r); }

// ============================================================
//  狀態讀取 (0x001A) — 唯讀，無 setter
// ============================================================

int JC_100_METER::get_switch_output_status() { std::vector<uint8_t> r; return send_command(0x03, JC100_REG::SWITCH_STATUS, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }

// ============================================================
//  命令 (0x0020)
// ============================================================

bool JC_100_METER::zero_calibration() { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::ZERO_CAL, 0x0001, r); }

// ============================================================
//  CRC16 (Modbus RTU)
// ============================================================

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
