#include "SD76_length_meters.h"
#include "log_utils.h"
#include <string.h>

//=========== init ===========

SD76_length_meters::SD76_length_meters()
{
	client = nullptr;
	owns = false;
	deviceID = 1;
	debug_mode = false;
	_log_tag = "SD76:?";
}

SD76_length_meters::~SD76_length_meters()
{
	if (owns && client)
		delete client;
}

bool SD76_length_meters::init(const std::string& ip, int port, int id, bool debug)
{
	client = new TCP_client();
	owns = true;

	deviceID = id;
	debug_mode = debug;
	_log_tag = "SD76:" + std::to_string(id);

	if (!client->connectToServer(ip, port)) {
		LOG_ERR(_log_tag, "connect failed %s:%d", ip.c_str(), port);
		return true;
	}

	return false;
}

bool SD76_length_meters::init(TCP_client& extClient, int id, bool debug)
{
	client = &extClient;
	owns = false;
	deviceID = id;
	debug_mode = debug;
	_log_tag = "SD76:" + std::to_string(id);

	// Modbus probe: read work mode register (0x0000, 1 register) to verify the
	// device actually responds on the RS485 bus behind the shared TCP gateway.
	// Without this probe, TCP connect to the USR-TCP232 gateway returning OK
	// (gateway alive) is mistaken for "SD76 alive" — meter_loop will hammer a
	// missing device with 300ms recv timeouts on every cycle, holding the
	// shared cli mutex and starving paired SE3 commands of dispatch latency.
	// Bench 2026-05-15: meters physically unplugged still showed [OK] resumed
	// at startup; symptom was 200-300ms drift in dual_se3_concurrent dispatch
	// long after the fix to the retry architecture itself.
	uint8_t probe[2];
	if (readRegister(0x0000, 1, probe)) {
		LOG_ERR(_log_tag, "init Mode B probe failed — device not on bus (slave %d)", id);
		return true;
	}
	return false;
}

//=========== utility: CRC ===========

uint16_t SD76_length_meters::crc16(const uint8_t* buf, int len)
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

bool SD76_length_meters::sendModbus(const uint8_t* req, int reqLen,
	uint8_t* resp, int& respLen)
{
	LOG_HEX(_log_tag, "TX", req, reqLen);

	// Atomic transaction — see TCP_client::sendAndReceive doc. SD76 shares
	// cli_A / cli_B in Crane_control_PI with SE3 + CLV900; meter_loop polls
	// while cmd_hold / motion_rope writes commands → must not interleave.
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

//=========== utility: Read register ===========

bool SD76_length_meters::readRegister(uint16_t addr, uint16_t count, uint8_t* raw)
{
	uint8_t req[8];

	req[0] = deviceID;
	req[1] = 0x03;
	req[2] = addr >> 8;
	req[3] = addr & 0xFF;
	req[4] = count >> 8;
	req[5] = count & 0xFF;

	uint16_t c = crc16(req, 6);
	req[6] = c & 0xFF;
	req[7] = c >> 8;

	uint8_t resp[256];
	int respLen = 0;

	if (sendModbus(req, 8, resp, respLen))
		return true;

	if (respLen < 5) return true;
	if (resp[1] != 0x03) return true;

	int byteCount = resp[2];
	memcpy(raw, &resp[3], byteCount);

	if (debug_mode) {
		char note[32];
		std::snprintf(note, sizeof(note), "REG 0x%04X", addr);
		LOG_HEX(_log_tag, note, raw, byteCount);
	}

	return false;
}

//=========== utility: 6-digit BCD decode + sign ===========

int SD76_length_meters::decodeSignedBCD6(const uint8_t raw[4])
{
	bool neg = (raw[0] & 0x80) != 0;

	auto bcd2 = [](uint8_t b) {
		return ((b >> 4) & 0x0F) * 10 + (b & 0x0F);
	};

	int absVal =
		bcd2(raw[1]) * 10000 +
		bcd2(raw[2]) * 100 +
		bcd2(raw[3]);

	return neg ? -absVal : absVal;
}

//=========== read ===========

bool SD76_length_meters::readUpperDisplayValue(int& value)
{
	uint8_t raw[4];

	if (readRegister(0x0001, 2, raw))
		return true;

	value = decodeSignedBCD6(raw);
	return false;
}

bool SD76_length_meters::readUpperLowerDisplayValue(int& upper, int& lower)
{
	uint8_t raw[8];

	if (readRegister(0x0001, 4, raw))
		return true;

	upper = decodeSignedBCD6(&raw[0]);
	lower = decodeSignedBCD6(&raw[4]);

	return false;
}

bool SD76_length_meters::readStatus(uint8_t& workMode, uint8_t& alarmStatus)
{
	uint8_t raw[2];

	if (readRegister(0x0000, 1, raw))
		return true;

	workMode = raw[0];      // high byte
	alarmStatus = raw[1];   // low byte
	return false;
}

bool SD76_length_meters::readUpperInteger(int32_t& value)
{
	uint8_t raw[4];

	if (readRegister(0x0021, 2, raw))
		return true;

	value = (int32_t)(((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) |
	                   ((uint32_t)raw[2] << 8)  | (uint32_t)raw[3]);
	return false;
}

bool SD76_length_meters::readLowerInteger(int32_t& value)
{
	uint8_t raw[4];

	if (readRegister(0x0023, 2, raw))
		return true;

	value = (int32_t)(((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) |
	                   ((uint32_t)raw[2] << 8)  | (uint32_t)raw[3]);
	return false;
}

//=========== control ===========

bool SD76_length_meters::resetAll()
{
	return writeSingleRegister(0x0000, 0x0003);
}

bool SD76_length_meters::pauseMeter()
{
	return writeSingleRegister(0x0000, 0x0004);
}

bool SD76_length_meters::resumeMeter()
{
	return writeSingleRegister(0x0000, 0x0008);
}

//=========== utility: Modbus write single register (0x06) ===========

bool SD76_length_meters::writeSingleRegister(uint16_t addr, uint16_t value)
{
	uint8_t req[8];

	req[0] = deviceID;
	req[1] = 0x06;
	req[2] = addr >> 8;
	req[3] = addr & 0xFF;
	req[4] = value >> 8;
	req[5] = value & 0xFF;

	uint16_t c = crc16(req, 6);
	req[6] = c & 0xFF;
	req[7] = c >> 8;

	uint8_t resp[256];
	int respLen = 0;

	return sendModbus(req, 8, resp, respLen);
}

//=========== utility: Modbus write multiple registers (0x10) ===========

bool SD76_length_meters::writeMultipleRegisters(uint16_t addr, uint16_t count,
                                                 const uint8_t* data, int dataLen)
{
	if (dataLen != count * 2) return true;
	if (count == 0 || count > 125) return true;

	const int reqLen = 7 + dataLen + 2;   // header(7) + data + CRC(2)
	uint8_t req[256];
	if (reqLen > (int)sizeof(req)) return true;

	req[0] = deviceID;
	req[1] = 0x10;
	req[2] = addr >> 8;
	req[3] = addr & 0xFF;
	req[4] = count >> 8;
	req[5] = count & 0xFF;
	req[6] = (uint8_t)dataLen;
	memcpy(&req[7], data, dataLen);

	uint16_t c = crc16(req, 7 + dataLen);
	req[7 + dataLen]     = c & 0xFF;
	req[7 + dataLen + 1] = c >> 8;

	uint8_t resp[256];
	int respLen = 0;

	if (sendModbus(req, reqLen, resp, respLen)) return true;
	if (respLen < 8) return true;
	if (resp[1] != 0x10) return true;
	return false;
}

//=========== utility: 6-digit BCD encode (no sign byte; SCAL is unsigned) ===========

void SD76_length_meters::encodeBCD6(uint32_t value, uint8_t out[4])
{
	if (value > 999999) value = 999999;   // clamp to 6 BCD digits

	out[0] = 0x00;   // sign byte unused for SCAL
	out[1] = (uint8_t)((((value / 100000) % 10) << 4) | ((value / 10000) % 10));
	out[2] = (uint8_t)((((value / 1000)   % 10) << 4) | ((value / 100)   % 10));
	out[3] = (uint8_t)((((value / 10)     % 10) << 4) |  (value           % 10));
}

//=========== calibration ===========

bool SD76_length_meters::readScale(uint32_t& scal_value, uint8_t& dp_upper)
{
	// SCAL @ 0x0014-0x0015 (2 reg = 4 bytes). BCD layout matches decodeSignedBCD6.
	uint8_t raw[4];
	if (readRegister(0x0014, 2, raw)) {
		LOG_ERR(_log_tag, "readScale: SCAL read failed");
		return true;
	}
	auto bcd2 = [](uint8_t b) {
		return ((b >> 4) & 0x0F) * 10 + (b & 0x0F);
	};
	scal_value = (uint32_t)bcd2(raw[1]) * 10000
	           + (uint32_t)bcd2(raw[2]) * 100
	           + (uint32_t)bcd2(raw[3]);

	// DP @ 0x0020 (1 reg = 2 bytes). Manual: low byte=upper DP, high byte=lower DP.
	// readRegister byte order: raw[0]=high byte of register, raw[1]=low byte.
	uint8_t draw[2];
	if (readRegister(0x0020, 1, draw)) {
		LOG_ERR(_log_tag, "readScale: DP read failed");
		return true;
	}
	dp_upper = draw[1];

	double pe = 1.0;
	for (int i = 0; i < dp_upper && i < 6; ++i) pe *= 10.0;
	const double app_M = (scal_value > 0) ? (pe / (double)scal_value) : 0.0;
	LOG_DBG(_log_tag, "readScale: SCAL=%u DP=%u (app multiplier=%g; device K-factor=%g)",
	        scal_value, (unsigned)dp_upper, app_M, (double)scal_value / pe);
	return false;
}

bool SD76_length_meters::writeScale(uint32_t scal_value, uint8_t dp_upper, bool write_dp)
{
	if (scal_value > 999999) {
		LOG_ERR(_log_tag, "writeScale: SCAL %u > 999999 (6 BCD digits max)", scal_value);
		return true;
	}
	if (dp_upper > 5) {
		LOG_ERR(_log_tag, "writeScale: DP %u out of [0,5]", (unsigned)dp_upper);
		return true;
	}

	uint8_t enc[4];
	encodeBCD6(scal_value, enc);
	if (writeMultipleRegisters(0x0014, 2, enc, 4)) {
		LOG_ERR(_log_tag, "writeScale: SCAL write failed");
		return true;
	}

	if (write_dp) {
		// Read current DP register to preserve lower-display DP byte.
		uint8_t draw[2];
		if (readRegister(0x0020, 1, draw)) {
			LOG_ERR(_log_tag, "writeScale: DP pre-read failed");
			return true;
		}
		const uint8_t dp_lower_keep = draw[0];   // high byte of register
		const uint16_t new_dp_value = ((uint16_t)dp_lower_keep << 8) | dp_upper;
		if (writeSingleRegister(0x0020, new_dp_value)) {
			LOG_ERR(_log_tag, "writeScale: DP write failed");
			return true;
		}
	}

	LOG_INF(_log_tag, "writeScale: SCAL=%u DP=%u%s committed",
	        scal_value, (unsigned)dp_upper, write_dp ? "" : " (DP preserved)");
	return false;
}

bool SD76_length_meters::getEffectiveScale(double& multiplier)
{
	// Application semantic: M = display / pulses. Device stores K = pulses /
	// display in SCAL × 10^(-DP). Invert: M = 10^DP / SCAL.
	uint32_t scal = 0;
	uint8_t  dp = 0;
	if (readScale(scal, dp)) return true;
	if (scal == 0) {
		LOG_ERR(_log_tag, "getEffectiveScale: device SCAL=0 (would div0); device misconfigured");
		return true;
	}
	if (dp > 6) {
		LOG_ERR(_log_tag, "getEffectiveScale: device DP=%u out of expected range", (unsigned)dp);
		return true;
	}
	double p = 1.0;
	for (int i = 0; i < dp; ++i) p *= 10.0;
	multiplier = p / (double)scal;
	return false;
}

bool SD76_length_meters::setEffectiveScale(double multiplier)
{
	if (!(multiplier > 0.0)) {
		LOG_ERR(_log_tag, "setEffectiveScale: non-positive multiplier %g", multiplier);
		return true;
	}

	// PRESERVE current DP. Bench finding 2026-05-09 (Sadie): SD76 firmware
	// rejects Modbus writes to DP register 0x0020 while in 通訊模式 (00-16=3) —
	// same class of mode-latch as SE3 H1000 / SE3 P.79=3. DP can only be set
	// via SD76 panel: 00-16=非3 → 改 DP → 00-16=3.
	//
	// Earlier driver auto-picked highest DP for max precision; that silently
	// failed (SCAL changed but DP didn't, leaving effective scale 10^Δ× off).
	// Now we use device's CURRENT DP and error out if multiplier doesn't fit
	// the range, with a hint for the operator to bump DP from panel.
	uint32_t scal_cur = 0;
	uint8_t  dp_cur = 0;
	if (readScale(scal_cur, dp_cur)) return true;
	if (dp_cur > 5) {
		LOG_ERR(_log_tag, "setEffectiveScale: device DP=%u out of expected [0,5]", (unsigned)dp_cur);
		return true;
	}

	static const double pow10[] = {1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0};
	const double scaled = pow10[dp_cur] / multiplier;

	if (scaled < 1.0) {
		LOG_ERR(_log_tag,
		        "setEffectiveScale: multiplier %g too LARGE for current DP=%u (10^DP/M=%.4f rounds to 0). "
		        "Increase DP via SD76 panel: 00-16=非3 → 改 DP → 00-16=3",
		        multiplier, (unsigned)dp_cur, scaled);
		return true;
	}
	if (scaled > 999999.0) {
		LOG_ERR(_log_tag,
		        "setEffectiveScale: multiplier %g too SMALL for current DP=%u (10^DP/M=%.0f overflows 6 BCD). "
		        "Decrease DP via SD76 panel: 00-16=非3 → 改 DP → 00-16=3",
		        multiplier, (unsigned)dp_cur, scaled);
		return true;
	}

	const uint32_t new_scal = (uint32_t)(scaled + 0.5);
	if (new_scal == 0) {
		LOG_ERR(_log_tag, "setEffectiveScale: SCAL would round to 0; reject");
		return true;
	}
	LOG_INF(_log_tag, "setEffectiveScale: multiplier=%g → SCAL=%u (DP=%u preserved)",
	        multiplier, new_scal, (unsigned)dp_cur);
	return writeScale(new_scal, dp_cur, false);   // preserve DP
}

bool SD76_length_meters::scaleByRatio(double ratio)
{
	if (!(ratio > 0.0)) {
		LOG_ERR(_log_tag, "scaleByRatio: non-positive ratio %g", ratio);
		return true;
	}
	double cur = 0.0;
	if (getEffectiveScale(cur)) return true;
	const double target = cur * ratio;
	LOG_INF(_log_tag, "scaleByRatio: %g × %g = %g", cur, ratio, target);
	return setEffectiveScale(target);
}
