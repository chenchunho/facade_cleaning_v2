#include "CLV900_inverter.h"
#include "log_utils.h"
#include <string.h>
#include <math.h>

//=========== init ===========

CLV900_inverter::CLV900_inverter()
{
	client = nullptr;
	owns = false;
	deviceID = 1;
	debug_mode = false;
	_log_tag = "CLV900:?";
}

CLV900_inverter::~CLV900_inverter()
{
	if (owns && client)
		delete client;
}

bool CLV900_inverter::init(const std::string& ip, int port, int id, bool debug)
{
	client = new TCP_client();
	owns = true;

	deviceID = id;
	debug_mode = debug;
	_log_tag = "CLV900:" + std::to_string(id);

	if (!client->connectToServer(ip, port)) {
		LOG_ERR(_log_tag, "connect failed %s:%d", ip.c_str(), port);
		return true;
	}

	return false;
}

bool CLV900_inverter::init(TCP_client& extClient, int id, bool debug)
{
	client = &extClient;
	owns = false;
	deviceID = id;
	debug_mode = debug;
	_log_tag = "CLV900:" + std::to_string(id);
	return false;
}

//=========== utility: CRC16 (Modbus, init 0xFFFF, poly 0xA001) ===========

uint16_t CLV900_inverter::crc16(const uint8_t* buf, int len)
{
	uint16_t crc = 0xFFFF;

	for (int i = 0; i < len; i++) {
		crc ^= buf[i];
		for (int j = 0; j < 8; j++)
			crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
	}
	return crc;
}

//=========== utility: TX/RX ===========

bool CLV900_inverter::sendModbus(const uint8_t* req, int reqLen,
	uint8_t* resp, int& respLen)
{
	LOG_HEX(_log_tag, "TX", req, reqLen);

	// Atomic transaction — see TCP_client::sendAndReceive doc. CLV900 shares
	// cli_A in Crane_control_PI with SE3 left + SD76 left/middle, so without
	// atomic locking concurrent threads' replies could intermix.
	int got = client->sendAndReceive((const char*)req, reqLen,
	                                 (char*)resp, 256,
	                                 200, 300);
	if (got <= 0) {
		respLen = 0;
		return true;
	}
	respLen = got;

	LOG_HEX(_log_tag, "RX", resp, respLen);
	return false;
}

//=========== utility: generic param write/read ===========

bool CLV900_inverter::writeParam(uint16_t reg, uint16_t value)
{
	uint8_t req[8];

	req[0] = (uint8_t)deviceID;
	req[1] = 0x06;
	req[2] = reg >> 8;
	req[3] = reg & 0xFF;
	req[4] = value >> 8;
	req[5] = value & 0xFF;

	uint16_t c = crc16(req, 6);
	req[6] = c & 0xFF;
	req[7] = c >> 8;

	uint8_t resp[256];
	int respLen = 0;

	if (sendModbus(req, 8, resp, respLen))
		return true;

	// Echo for FC 0x06 = 8 bytes identical to the request.
	if (respLen < 8) return true;
	if (resp[0] != req[0] || resp[1] != 0x06) return true;
	if (resp[2] != req[2] || resp[3] != req[3]) return true;
	if (resp[4] != req[4] || resp[5] != req[5]) return true;

	return false;
}

bool CLV900_inverter::readParam(uint16_t reg, uint16_t& value)
{
	uint8_t req[8];

	req[0] = (uint8_t)deviceID;
	req[1] = 0x03;
	req[2] = reg >> 8;
	req[3] = reg & 0xFF;
	req[4] = 0x00;
	req[5] = 0x01;     // count = 1 register

	uint16_t c = crc16(req, 6);
	req[6] = c & 0xFF;
	req[7] = c >> 8;

	uint8_t resp[256];
	int respLen = 0;

	if (sendModbus(req, 8, resp, respLen))
		return true;

	// Expected response: addr | 0x03 | 0x02 | hi | lo | crc_lo | crc_hi
	if (respLen < 7) return true;
	if (resp[0] != (uint8_t)deviceID) return true;
	if (resp[1] != 0x03) return true;       // exception (0x83) handled here
	if (resp[2] != 0x02) return true;

	value = (uint16_t)((resp[3] << 8) | resp[4]);
	return false;
}

//=========== control (write reg 0x0002) ===========

bool CLV900_inverter::runForward()  { return writeParam(0x0002, 1); }
bool CLV900_inverter::runReverse()  { return writeParam(0x0002, 2); }
bool CLV900_inverter::jogForward()  { return writeParam(0x0002, 3); }
bool CLV900_inverter::jogReverse()  { return writeParam(0x0002, 4); }
bool CLV900_inverter::stopFree()    { return writeParam(0x0002, 5); }
bool CLV900_inverter::stopDecel()   { return writeParam(0x0002, 6); }
bool CLV900_inverter::resetFault()  { return writeParam(0x0002, 7); }

//=========== control: frequency setpoint (write reg 0x0001) ===========

bool CLV900_inverter::setFreqRaw(int16_t value)
{
	if (value >  10000) value =  10000;
	if (value < -10000) value = -10000;

	// Reg 0x01 takes a signed value in the wire as plain 16-bit two's complement.
	return writeParam(0x0001, (uint16_t)value);
}

bool CLV900_inverter::setFreqPercent(double pct)
{
	if (pct >  100.0) pct =  100.0;
	if (pct < -100.0) pct = -100.0;
	int v = (int)((pct * 100.0) + (pct >= 0 ? 0.5 : -0.5));   // round
	return setFreqRaw((int16_t)v);
}

bool CLV900_inverter::setFreqHz(double hz, double max_hz)
{
	if (max_hz <= 0.0) return true;
	double pct = (hz / max_hz) * 100.0;
	return setFreqPercent(pct);
}

//=========== read: monitor ===========

bool CLV900_inverter::readRunStatus(uint16_t& status)
{
	return readParam(0x1000, status);
}

bool CLV900_inverter::readFaultCode(uint16_t& code)
{
	return readParam(0x1001, code);
}

bool CLV900_inverter::readSetFreq(double& hz)
{
	uint16_t v = 0;
	if (readParam(0x1002, v)) return true;
	hz = (int16_t)v / 10.0;
	return false;
}

bool CLV900_inverter::readRunFreq(double& hz)
{
	uint16_t v = 0;
	if (readParam(0x1003, v)) return true;
	hz = (int16_t)v / 10.0;
	return false;
}

bool CLV900_inverter::readSpeedRpm(uint16_t& rpm)
{
	return readParam(0x1004, rpm);
}

bool CLV900_inverter::readOutputVoltage(uint16_t& volts)
{
	return readParam(0x1005, volts);
}

bool CLV900_inverter::readOutputCurrent(double& amps)
{
	uint16_t v = 0;
	if (readParam(0x1006, v)) return true;
	amps = v / 10.0;
	return false;
}

bool CLV900_inverter::readBusVoltage(uint16_t& volts)
{
	return readParam(0x1008, volts);
}

bool CLV900_inverter::readOutputTorque(double& pct)
{
	uint16_t v = 0;
	if (readParam(0x1009, v)) return true;
	pct = (int16_t)v / 10.0;   // signed 0.1 %
	return false;
}

bool CLV900_inverter::readIgbtTemp(uint16_t& celsius)
{
	return readParam(0x101A, celsius);
}

//=========== utility: convenience ===========

bool CLV900_inverter::configureModbusControl()
{
	if (writeParam(0xF000, 2)) return true;   // F0-00 = 2  run via Modbus
	if (writeParam(0xF001, 8)) return true;   // F0-01 = 8  freq via Modbus
	return false;
}
