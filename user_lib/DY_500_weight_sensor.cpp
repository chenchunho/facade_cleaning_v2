#include "DY_500_weight_sensor.h"
#include "log_utils.h"
#ifdef _WIN32
#include <windows.h>
#include <thread>
#else
#include <unistd.h>
#define Sleep(ms) usleep((ms) * 1000)
#endif

#include <cstring>
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>


//=========== init ===========

DY_500_weight_sensor::DY_500_weight_sensor()
	: client(nullptr), debug_mode(false), slaveID(1),
	  lastValidWeight(0.0f), weightErrorCount(0)
{
	_log_tag = "DY500:?";
}

DY_500_weight_sensor::~DY_500_weight_sensor()
{
}

bool DY_500_weight_sensor::init(const std::string& ip, int port, int ID, bool debug)
{
	slaveID = (uint8_t)ID;
	debug_mode = debug;
	_log_tag = "DY500:" + std::to_string(ID);
	client = &ownedClient;
	return !client->connectToServer(ip, port);
}

bool DY_500_weight_sensor::init(TCP_client& extClient, int ID, bool debug)
{
	this->client = &extClient;
	this->slaveID = (uint8_t)ID;
	this->debug_mode = debug;
	_log_tag = "DY500:" + std::to_string(ID);

	return false;
}


//=========== control: communication params ===========

void DY_500_weight_sensor::set_communication_parm(int ID, int baud, int format)
{
	bool valid = true;

	if (ID < 1 || ID > 255) {
		LOG_ERR(_log_tag, "ID out of range: %d", ID);
		valid = false;
	}

	if (baud < 1 || baud > 7) {
		LOG_ERR(_log_tag, "Baud out of range: %d", baud);
		valid = false;
	}

	if (format < 0 || format > 3) {
		LOG_ERR(_log_tag, "Format out of range: %d", format);
		valid = false;
	}

	if (!valid) {
		LOG_ERR(_log_tag, "communication params not applied");
		return;
	}

	modbus_write_single(0x9C74, ID);
	modbus_write_single(0x9C72, baud);
	modbus_write_single(0x9C70, format);

	LOG_INF(_log_tag, "communication params updated: ID=%d Baud=%d Format=%d", ID, baud, format);
}

//=========== utility: CRC16 ===========

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

//=========== utility: Modbus Read (Function 03) ===========

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

	LOG_HEX(_log_tag, "TX read", req, 8);

	if (!client->sendData((char*)req, 8, 100))
		return true;

	char buf[128];
	int n = client->receiveData(buf, sizeof(buf), 400);
	if (n < 7) return true;

	LOG_HEX(_log_tag, "RX read", buf, n);

	memcpy(rx, buf, n);
	rxLen = n;
	return false;
}

//=========== utility: Modbus Write Single ===========

bool DY_500_weight_sensor::modbus_write_single(uint16_t addr, uint16_t value)
{
	return modbus_write_long(addr, (int32_t)value);
}

//=========== utility: Modbus Write LONG ===========

bool DY_500_weight_sensor::modbus_write_long(uint16_t addr, int32_t value)
{
	uint8_t req[13] = {
		slaveID, 0x10,
		(uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
		0x00, 0x02,       // 2 registers
		0x04,              // 4 bytes data
		(uint8_t)((value >> 24) & 0xFF),
		(uint8_t)((value >> 16) & 0xFF),
		(uint8_t)((value >> 8) & 0xFF),
		(uint8_t)(value & 0xFF)
	};

	uint16_t crc = CRC16(req, 11);
	req[11] = crc & 0xFF;
	req[12] = crc >> 8;

	LOG_HEX(_log_tag, "TX write_long", req, (int)sizeof(req));

	if (!client->sendData((char*)req, sizeof(req), 100))
		return true;

	char buf[32];
	int n = client->receiveData(buf, sizeof(buf), 300);
	return n < 8;
}

//=========== utility: parse long (Big Endian) ===========

int32_t DY_500_weight_sensor::parse_long(uint8_t* buf, int index)
{
	uint32_t raw =
		((uint32_t)buf[index] << 24) |
		((uint32_t)buf[index + 1] << 16) |
		((uint32_t)buf[index + 2] << 8) |
		(uint32_t)buf[index + 3];

	return (int32_t)raw;
}

//=========== read: register long (with retry + delay) ===========

bool DY_500_weight_sensor::read_reg_long(uint16_t addr, int32_t& out)
{
	for (int retry = 0; retry < 3; retry++)
	{
		uint8_t buf[64];
		int len;

		if (!modbus_read(addr, 2, buf, len))
		{
			out = parse_long(&buf[3], 0);
			Sleep(8);
			return false;
		}

		Sleep(8);
	}

	return true;
}

//=========== read ===========

bool DY_500_weight_sensor::get_weight_long(int32_t& outValue)
{
	return read_reg_long(0x9C40, outValue);
}

bool DY_500_weight_sensor::get_weight_float(float& outValue)
{
	constexpr int ERROR_THRESHOLD = 10;

	uint8_t buf[64];
	int len = 0;

	bool hasError = false;

	// 1. read data (FLOAT addr 0x9CA4, 2 registers)
	if (modbus_read(0x9CA4, 2, buf, len) || len < 7)
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

		// 2. validity check
		if (raw == 0x00000000 ||
			raw == 0xFFFFFFFF ||
			std::isnan(fValue) ||
			std::isinf(fValue) ||
			fValue < -5000.0f ||
			fValue > 5000.0f)
		{
			hasError = true;
		}
		else
		{
			lastValidWeight = fValue;
			outValue = fValue;
			weightErrorCount = 0;
			return false;
		}
	}

	weightErrorCount++;

	outValue = lastValidWeight;

	if (weightErrorCount < ERROR_THRESHOLD) {
		return false;
	}

	LOG_ERR(_log_tag, "get_weight_float: consecutive errors reached threshold");
	return true;
}

bool DY_500_weight_sensor::get_decimal_point(int& dp)
{
	int32_t val;
	if (read_reg_long(0x9C64, val))
		return true;

	dp = (int)val;
	return false;
}

//=========== control: clear ===========

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
		return true;

	char buf[32];
	int n = client->receiveData(buf, sizeof(buf), 200);
	return n < 8;
}

//=========== read: all params ===========

bool DY_500_weight_sensor::read_all_parm()
{
	bool err = false;

	err |= read_reg_long(0x9C40, reg.weight);
	err |= read_reg_long(0x9C42, reg.adc_value);
	err |= read_reg_long(0x9C44, reg.da_value);
	err |= read_reg_long(0x9C46, reg.system_status);
	err |= read_reg_long(0x9C48, reg.multi_function);
	err |= read_reg_long(0x9C4A, reg.calibrate_weight);

	err |= read_reg_long(0x9C54, reg.power_on_zero_range);
	err |= read_reg_long(0x9C56, reg.stable_range);
	err |= read_reg_long(0x9C58, reg.stable_time);
	err |= read_reg_long(0x9C5A, reg.zero_track);
	err |= read_reg_long(0x9C5C, reg.digital_filter);
	err |= read_reg_long(0x9C5E, reg.auto_zero_cfg);
	err |= read_reg_long(0x9C60, reg.auto_zero_trigger);
	err |= read_reg_long(0x9C62, reg.auto_zero_delay);
	err |= read_reg_long(0x9C64, reg.decimal_point);

	err |= read_reg_long(0x9C68, reg.rated_output);
	err |= read_reg_long(0x9C6A, reg.sample_speed);
	err |= read_reg_long(0x9C6E, reg.protocol_mode);
	err |= read_reg_long(0x9C70, reg.data_format);
	err |= read_reg_long(0x9C72, reg.baudrate);
	err |= read_reg_long(0x9C74, reg.station_id);

	err |= read_reg_long(0x9C76, reg.auto_send_interval);
	err |= read_reg_long(0x9C78, reg.system_zero);
	err |= read_reg_long(0x9C7A, reg.span_factor);
	err |= read_reg_long(0x9C7C, reg.sensor_sensitivity);
	err |= read_reg_long(0x9C7E, reg.AD0);
	err |= read_reg_long(0x9C80, reg.AD1);
	err |= read_reg_long(0x9C82, reg.sensor_range);
	err |= read_reg_long(0x9C84, reg.auto_calibration);
	err |= read_reg_long(0x9C86, reg.trans_zero);
	err |= read_reg_long(0x9C88, reg.trans_full);
	err |= read_reg_long(0x9C8A, reg.trans_start);

	return err;
}

//=========== utility: print parms ===========

void DY_500_weight_sensor::print_parm()
{
	auto show = [&](const char* name, int32_t val, uint16_t addr)
	{
		LOG_DBG(_log_tag, "0x%04X  %-22s : DEC=%-12d HEX=0x%08X",
			addr, name, val, (uint32_t)val);
	};

	LOG_DBG(_log_tag, "==== DY500 RegisterMap (DEC + HEX) ====");

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

	LOG_DBG(_log_tag, "========================================");
}
