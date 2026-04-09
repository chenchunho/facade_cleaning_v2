#include "DY_500_weight_sensor.h"
#ifdef _WIN32
#include <windows.h>
#include <thread>
#else
#include <unistd.h>
#define Sleep(ms) usleep((ms) * 1000)
#endif

#include <iostream>
#include <cstring>
#include <cstdio>
#include <cmath>        // <--- Linux 需要這個
#include <thread>       // <--- Linux 需要這個
#include <chrono>       // <--- Linux 需要這個


/**************************************************
 * 小工具：整數轉 HEX 字串 (安全 snprintf)
 **************************************************/
static std::string to_hex(int32_t v)
{
	char buf[16];
	std::snprintf(buf, sizeof(buf), "0x%08X", (uint32_t)v);
	return std::string(buf);
}

DY_500_weight_sensor::DY_500_weight_sensor()
	: client(nullptr), debugMode(false), slaveID(1),
	  lastValidWeight(0.0f), weightErrorCount(0)
{
}

DY_500_weight_sensor::~DY_500_weight_sensor()
{
}

/**************************************************
 * 初始化 MODBUS-TCP 連線
 **************************************************/
bool DY_500_weight_sensor::init(const std::string& ip, int port, int ID, bool debug)
{
	slaveID = (uint8_t)ID;
	debugMode = debug;
	client = &ownedClient;
	return client->connectToServer(ip, port);
}

bool DY_500_weight_sensor::init(TCP_client& extClient, int ID, bool debug)
{
	// 把外部 TCP_client 複製到內部（若 TCP_client 是安全的）
	this->client = &extClient;
	this->slaveID = (uint8_t)ID;
	this->debugMode = debug;

	return true;
}


/**************************************************
 * 修改通訊參數（ID / Baud / Format）
 **************************************************/
void DY_500_weight_sensor::set_communication_parm(int ID, int baud, int format)
{
	bool valid = true;

	if (ID < 1 || ID > 255) {
		if (debugMode) std::cout << "[ERROR] ID out of range: " << ID << "\n";
		valid = false;
	}

	if (baud < 1 || baud > 7) {
		if (debugMode) std::cout << "[ERROR] Baud out of range: " << baud << "\n";
		valid = false;
	}

	if (format < 0 || format > 3) {
		if (debugMode) std::cout << "[ERROR] Format out of range: " << format << "\n";
		valid = false;
	}

	if (!valid) {
		if (debugMode) std::cout << "[ABORT] Parameters not applied.\n";
		return;
	}

	modbus_write_single(0x9C74, ID);
	modbus_write_single(0x9C72, baud);
	modbus_write_single(0x9C70, format);

	if (debugMode)
	{
		std::cout << "[OK] Communication parameters updated:\n";
		std::cout << "     ID     = " << ID << "\n";
		std::cout << "     Baud   = " << baud << "\n";
		std::cout << "     Format = " << format << "\n";
	}
}

/**************************************************
 * CRC16
 **************************************************/
uint16_t DY_500_weight_sensor::CRC16(const uint8_t* data, int len)
{
	uint16_t crc = 0xFFFF;

	for (int i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i];
		for (int j = 0; j < 8; j++) {
			if (crc & 1)
				crc = (crc >> 1) ^ 0xA001;
			else
				crc >>= 1;
		}
	}
	return crc;
}

/**************************************************
 * Modbus Read (Function 03) – Timeout 改 400ms
 **************************************************/
bool DY_500_weight_sensor::modbus_read(uint16_t addr, uint16_t quantity,
	uint8_t* rx, int& rxLen)
{
	uint8_t req[8];

	req[0] = slaveID;
	req[1] = 0x03;
	req[2] = addr >> 8;
	req[3] = addr & 0xFF;
	req[4] = quantity >> 8;
	req[5] = quantity & 0xFF;

	uint16_t crc = CRC16(req, 6);
	req[6] = crc & 0xFF;
	req[7] = crc >> 8;

	if (debugMode) {
		printf("TX: ");
		for (int i = 0; i < 8; i++)
			printf("%02X ", req[i]);
		printf("\n");
	}

	if (!client->sendData((char*)req, 8, 100))
		return false;

	char buf[128];
	int n = client->receiveData(buf, sizeof(buf), 400);
	if (n < 7) return false;

	if (debugMode) {
		printf("RX: ");
		for (int i = 0; i < n; i++)
			printf("%02X ", (uint8_t)buf[i]);
		printf("\n");
	}

	memcpy(rx, buf, n);
	rxLen = n;
	return true;
}

/**************************************************
 * Modbus Write Single Register (Function 10)
 * DY-500 每個參數佔 2 寄存器 (LONG = 4 bytes)
 * 封包: ID 10 AddrH AddrL 00 02 04 D0 D1 D2 D3 CRC_L CRC_H
 **************************************************/
bool DY_500_weight_sensor::modbus_write_single(uint16_t addr, uint16_t value)
{
	return modbus_write_long(addr, (int32_t)value);
}

/**************************************************
 * Modbus Write LONG (Function 10, 2 registers)
 **************************************************/
bool DY_500_weight_sensor::modbus_write_long(uint16_t addr, int32_t value)
{
	uint8_t req[13] = {
		slaveID, 0x10,
		(uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
		0x00, 0x02,       // 寫 2 個寄存器
		0x04,              // 4 bytes 資料
		(uint8_t)((value >> 24) & 0xFF),
		(uint8_t)((value >> 16) & 0xFF),
		(uint8_t)((value >> 8) & 0xFF),
		(uint8_t)(value & 0xFF)
	};

	uint16_t crc = CRC16(req, 11);
	req[11] = crc & 0xFF;
	req[12] = crc >> 8;

	if (debugMode) {
		printf("TX: ");
		for (int i = 0; i < (int)sizeof(req); i++)
			printf("%02X ", req[i]);
		printf("\n");
	}

	if (!client->sendData((char*)req, sizeof(req), 100))
		return false;

	char buf[32];
	int n = client->receiveData(buf, sizeof(buf), 300);
	return n >= 8;
}

/**************************************************
 * 4 bytes to int32 (Big Endian)
 **************************************************/
int32_t DY_500_weight_sensor::parse_long(uint8_t* buf, int index)
{
	uint32_t raw =
		((uint32_t)buf[index] << 24) |
		((uint32_t)buf[index + 1] << 16) |
		((uint32_t)buf[index + 2] << 8) |
		(uint32_t)buf[index + 3];

	return (int32_t)raw;
}

/**************************************************
 * 改良版：讀取 LONG (含 Retry + Delay)
 **************************************************/
bool DY_500_weight_sensor::read_reg_long(uint16_t addr, int32_t& out)
{
	for (int retry = 0; retry < 3; retry++)
	{
		uint8_t buf[64];
		int len;

		if (modbus_read(addr, 2, buf, len))
		{
			out = parse_long(&buf[3], 0);
			Sleep(8);   // 給儀表一點休息時間
			return true;
		}

		Sleep(8);
	}

	return false;
}

/**************************************************
 * 取得 LONG 重量 (0x9C40)
 **************************************************/
bool DY_500_weight_sensor::get_weight_long(int32_t& outValue)
{
	return read_reg_long(0x9C40, outValue);
}

/**************************************************
 * 取得 FLOAT 重量 (0x9CA4)
 **************************************************/
bool DY_500_weight_sensor::get_weight_float(float& outValue)
{
	constexpr int ERROR_THRESHOLD = 10;      // 連續錯誤門檻

	uint8_t buf[64];
	int len = 0;

	bool hasError = false;

	// 1. 讀取資料 (FLOAT 地址 0x9CA4, 2 registers)
	if (!modbus_read(0x9CA4, 2, buf, len) || len < 7)
	{
		hasError = true;
	}
	else
	{
		uint32_t raw =
			((uint32_t)buf[3] << 24) |
			((uint32_t)buf[4] << 16) |
			((uint32_t)buf[5] << 8) |
			(uint32_t)buf[6];

		float fValue = 0.0f;
		memcpy(&fValue, &raw, sizeof(float));

		// 2. 判斷資料合法性
		if (raw == 0x00000000 ||      // 空值
			raw == 0xFFFFFFFF ||      // 無資料
			std::isnan(fValue) ||
			std::isinf(fValue) ||
			fValue < -5000.0f ||
			fValue > 5000.0f)
		{
			hasError = true;
		}
		else
		{
			// 成功讀到有效資料
			lastValidWeight = fValue;
			outValue = fValue;
			weightErrorCount = 0;
			return true;               // 成功
		}
	}

	// --- 有錯誤才會跑到這裡 ---

	weightErrorCount++;

	// 使用上一筆有效值
	outValue = lastValidWeight;

	// 連續錯誤未達門檻，仍回傳 true（使用上次有效值）
	if (weightErrorCount < ERROR_THRESHOLD) {
		return true;
	}

	return false;                      // 連續錯誤 >= 門檻，確認異常
}



/**************************************************
 * 取得小數點位置 (0x9C64)
 **************************************************/
bool DY_500_weight_sensor::get_decimal_point(int& dp)
{
	int32_t val;
	if (!read_reg_long(0x9C64, val))
		return false;

	dp = (int)val;
	return true;
}

/**************************************************
 * 清零
 **************************************************/
bool DY_500_weight_sensor::do_clear()
{
	uint8_t req[13] = {
		slaveID, 0x10,
		0x06, 0x2A,
		0x00, 0x02,
		0x04,
		0x00,0x00,0x00,0x01
	};

	uint16_t crc = CRC16(req, 11);
	req[11] = crc & 0xFF;
	req[12] = crc >> 8;

	if (!client->sendData((char*)req, sizeof(req), 100))
		return false;

	char buf[32];
	int n = client->receiveData(buf, sizeof(buf), 200);
	return n >= 8;
}

/**************************************************
 * ★★★ 逐項讀取所有參數（加 Delay + Retry） ★★★
 **************************************************/
bool DY_500_weight_sensor::read_all_parm()
{
	bool ok = true;

	ok &= read_reg_long(0x9C40, reg.weight);
	ok &= read_reg_long(0x9C42, reg.adc_value);
	ok &= read_reg_long(0x9C44, reg.da_value);
	ok &= read_reg_long(0x9C46, reg.system_status);
	ok &= read_reg_long(0x9C48, reg.multi_function);
	ok &= read_reg_long(0x9C4A, reg.calibrate_weight);

	ok &= read_reg_long(0x9C54, reg.power_on_zero_range);
	ok &= read_reg_long(0x9C56, reg.stable_range);
	ok &= read_reg_long(0x9C58, reg.stable_time);
	ok &= read_reg_long(0x9C5A, reg.zero_track);
	ok &= read_reg_long(0x9C5C, reg.digital_filter);
	ok &= read_reg_long(0x9C5E, reg.auto_zero_cfg);
	ok &= read_reg_long(0x9C60, reg.auto_zero_trigger);
	ok &= read_reg_long(0x9C62, reg.auto_zero_delay);
	ok &= read_reg_long(0x9C64, reg.decimal_point);

	ok &= read_reg_long(0x9C68, reg.rated_output);
	ok &= read_reg_long(0x9C6A, reg.sample_speed);
	ok &= read_reg_long(0x9C6E, reg.protocol_mode);
	ok &= read_reg_long(0x9C70, reg.data_format);
	ok &= read_reg_long(0x9C72, reg.baudrate);
	ok &= read_reg_long(0x9C74, reg.station_id);

	ok &= read_reg_long(0x9C76, reg.auto_send_interval);
	ok &= read_reg_long(0x9C78, reg.system_zero);
	ok &= read_reg_long(0x9C7A, reg.span_factor);
	ok &= read_reg_long(0x9C7C, reg.sensor_sensitivity);
	ok &= read_reg_long(0x9C7E, reg.AD0);
	ok &= read_reg_long(0x9C80, reg.AD1);
	ok &= read_reg_long(0x9C82, reg.sensor_range);
	ok &= read_reg_long(0x9C84, reg.auto_calibration);
	ok &= read_reg_long(0x9C86, reg.trans_zero);
	ok &= read_reg_long(0x9C88, reg.trans_full);
	ok &= read_reg_long(0x9C8A, reg.trans_start);

	return ok;
}

/**************************************************
 * 印出參數（HEX地址 + DEC 值 + HEX 值）
 **************************************************/
void DY_500_weight_sensor::print_parm()
{
	auto show = [&](const char* name, int32_t val, uint16_t addr)
	{
		printf("0x%04X  %-22s : DEC=%-12d HEX=%s\n",
			addr, name, val, to_hex(val).c_str());
	};

	std::cout << "==== DY500 RegisterMap (DEC + HEX) ====\n";

	show("weight", reg.weight, 0x9C40);
	show("adc_value", reg.adc_value, 0x9C42);
	show("da_value", reg.da_value, 0x9C44);
	show("system_status", reg.system_status, 0x9C46);
	show("multi_function", reg.multi_function, 0x9C48);
	show("calibrate_weight", reg.calibrate_weight, 0x9C4A);

	show("power_on_zero_range", reg.power_on_zero_range, 0x9C54);
	show("stable_range", reg.stable_range, 0x9C56);
	show("stable_time", reg.stable_time, 0x9C58);
	show("zero_track", reg.zero_track, 0x9C5A);
	show("digital_filter", reg.digital_filter, 0x9C5C);
	show("auto_zero_cfg", reg.auto_zero_cfg, 0x9C5E);
	show("auto_zero_trigger", reg.auto_zero_trigger, 0x9C60);
	show("auto_zero_delay", reg.auto_zero_delay, 0x9C62);
	show("decimal_point", reg.decimal_point, 0x9C64);

	show("rated_output", reg.rated_output, 0x9C68);
	show("sample_speed", reg.sample_speed, 0x9C6A);
	show("protocol_mode", reg.protocol_mode, 0x9C6E);
	show("data_format", reg.data_format, 0x9C70);
	show("baudrate", reg.baudrate, 0x9C72);
	show("station_id", reg.station_id, 0x9C74);

	show("auto_send_interval", reg.auto_send_interval, 0x9C76);
	show("system_zero", reg.system_zero, 0x9C78);
	show("span_factor", reg.span_factor, 0x9C7A);
	show("sensor_sensitivity", reg.sensor_sensitivity, 0x9C7C);
	show("AD0", reg.AD0, 0x9C7E);
	show("AD1", reg.AD1, 0x9C80);
	show("sensor_range", reg.sensor_range, 0x9C82);
	show("auto_calibration", reg.auto_calibration, 0x9C84);
	show("trans_zero", reg.trans_zero, 0x9C86);
	show("trans_full", reg.trans_full, 0x9C88);
	show("trans_start", reg.trans_start, 0x9C8A);

	std::cout << "========================================\n";
}
