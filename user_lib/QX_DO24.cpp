#include "QX_DO24.h"
#include "TCP_client.h"
#include "log_utils.h"
#include <cmath>
#include <chrono>

//=========== init ===========

QX_DO24::QX_DO24() {
	_log_tag = "QX:?";
}

QX_DO24::~QX_DO24() {
	if (owned_client) owned_client->close();
}

bool QX_DO24::init(const std::string& ip, int port, int ID, bool debug) {
	// C++11 compat for Linux (no std::make_unique)
	owned_client = std::unique_ptr<TCP_client>(new TCP_client());
	deviceID = ID;
	debug_mode = debug;
	_log_tag = "QX:" + std::to_string(ID);
	if (!owned_client->connectToServer(ip, port)) {
		LOG_ERR(_log_tag, "connect failed %s:%d", ip.c_str(), port);
		return false;
	}
	client = owned_client.get();
	return true;
}

bool QX_DO24::init(TCP_client& extClient, int ID, bool debug) {
	client = &extClient;
	deviceID = ID;
	debug_mode = debug;
	_log_tag = "QX:" + std::to_string(ID);
	return true;
}

//=========== control: composite setChannel ===========

// Order is FREQ -> DUTY -> CONTROL, and that order is deliberate:
//
//   * Frequency first, because duty is only meaningful relative to it. A duty
//     written while the module is still at its 1000Hz power-on default is a
//     completely different pulse width than the caller intended (10% = 0.1ms
//     instead of 2.0ms). The old order (duty -> freq) meant that on a channel
//     that was ALREADY running, the motor briefly saw new-duty x old-frequency.
//   * Control last, so the output only goes live once frequency AND duty are
//     both already correct — the load never sees an intermediate state.
//
// This matches the manual start-up sequence documented in Linux_test menu 34.
// NOTE: a mid-sequence failure leaves the earlier writes applied (e.g. freq set
// but duty not). Caller should re-read with the get* functions if it needs to
// know the resulting state.
bool QX_DO24::setChannel(int channel, double duty, int freq, uint16_t control) {
	LOG_DBG(_log_tag, "--- Setting Channel %d ---", channel);

	if (!setPWM_Freq(channel, freq)) return false;
	if (!setPWM_Duty(channel, duty)) return false;
	if (!setPWM_Control(channel, control)) return false;

	return true;
}

//=========== control: PWM Duty (0x06) ===========

// Widen/narrow the duty safety window. Must be called EXPLICITLY — the
// constructor defaults are the conservative 5~10% of the currently wired motor
// so that forgetting to configure yields a safe range, not a dangerous one.
void QX_DO24::setDutyLimits(double min_pct, double max_pct) {
	// positive form so NaN limits are rejected rather than stored (stored NaN
	// would make every later duty check fail — fail-safe, but by accident)
	if (!(min_pct <= max_pct)) return;                   // nonsense range or NaN: ignore
	if (min_pct < 0.0)   min_pct = 0.0;
	if (max_pct > 100.0) max_pct = 100.0;
	duty_min_pct = min_pct;
	duty_max_pct = max_pct;
	LOG_INF(_log_tag, "duty safety limits set to [%.1f, %.1f]%%", duty_min_pct, duty_max_pct);
}

// Widen/narrow the frequency safety window. Same fail-safe reasoning as
// setDutyLimits: default is the single value (50Hz) the duty window depends on.
void QX_DO24::setFreqLimits(int min_hz, int max_hz) {
	if (min_hz > max_hz) return;                 // nonsense range: ignore
	if (min_hz < 1)      min_hz = 1;
	if (max_hz > 200000) max_hz = 200000;
	freq_min_hz = min_hz;
	freq_max_hz = max_hz;
	LOG_INF(_log_tag, "freq safety limits set to [%d, %d] Hz", freq_min_hz, freq_max_hz);
}

// Set duty ratio. Fractional percent supported: reg = round(pct * 10), e.g.
// 7.5% -> 75. Refuses anything outside the safety window (see setDutyLimits).
bool QX_DO24::setPWM_Duty(int channel, double duty_percent) {
	// ch>3 would write into 0x04+ = channel-1 frequency registers, silently
	// corrupting frequency instead of failing. QX-DO24 has 4 channels only.
	if (!client || channel < 0 || channel > 3) return false;

	// Safety window — see setDutyLimits() in the header. Defaults to the wired
	// motor's 5%=stop / 10%=full-speed range; anything outside is refused rather
	// than clamped, so a wrong number is a visible failure, not a silently
	// different speed.
	//
	// ⚠ Written in POSITIVE form on purpose. The natural-looking
	//     if (duty < min || duty > max) reject;
	// lets NaN straight through — every comparison against NaN is false, so
	// neither branch fires. `duty` then reaches static_cast<uint16_t>(NaN),
	// which is UNDEFINED BEHAVIOUR (commonly 0 => motor stops without warning,
	// but formally any value; if it lands in 0~1000 the device happily accepts
	// an arbitrary duty). `!(duty >= min && duty <= max)` rejects NaN correctly.
	// Reachable via `d 1 nan` (istream >> double accepts "nan") or any caller
	// computing duty from a division that yields 0.0/0.0.
	if (!(duty_percent >= duty_min_pct && duty_percent <= duty_max_pct)) {
		LOG_ERR(_log_tag, "duty %.1f%% outside safety limits [%.1f, %.1f] (or NaN) — refused",
		        duty_percent, duty_min_pct, duty_max_pct);
		return false;
	}

	long raw = std::lround(duty_percent * 10.0);
	if (raw < 0 || raw > 1000) return false;   // device reg range 0~1000; unreachable
	                                           // while limits stay inside 0~100, kept
	                                           // as a hard backstop on the wire value
	uint16_t val  = static_cast<uint16_t>(raw);
	uint16_t addr = 0x0000 + channel;

	std::vector<uint8_t> req = {
		(uint8_t)deviceID, 0x06,
		(uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
		(uint8_t)(val >> 8), (uint8_t)(val & 0xFF)
	};

	uint16_t crc = modbusCRC(req.data(), (int)req.size());
	req.push_back(crc & 0xFF); req.push_back(crc >> 8);

	std::vector<uint8_t> res;
	// must receive echo exactly matching
	return (sendAndReceive(req, res) && res == req);
}

//=========== control: PWM Frequency (0x10, 32-bit register write) ===========

bool QX_DO24::setPWM_Freq(int channel, int freq) {
	if (!client || channel < 0 || channel > 3) return false;
	if (freq < 1 || freq > 200000) return false;   // manual: 1~200000 Hz

	// Safety clamp — see setFreqLimits(). Locked to 50Hz by default because the
	// duty window (5%=stop / 10%=full) is only valid at 50Hz; at the module's
	// 1000Hz power-on default the same percentages are meaningless pulse widths.
	if (freq < freq_min_hz || freq > freq_max_hz) {
		LOG_ERR(_log_tag, "freq %d Hz outside safety limits [%d, %d] — refused",
		        freq, freq_min_hz, freq_max_hz);
		return false;
	}
	uint16_t addr = 0x0004 + (channel * 2);

	// ABCD byte order: reg[addr]=high word, reg[addr+1]=low word. The high word
	// must carry bits 31..16 — hardcoding it to 0 silently truncated every
	// frequency above 65535 (e.g. 100000 -> 34464) with no error reported.
	std::vector<uint8_t> req = {
		(uint8_t)deviceID, 0x10, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
		0x00, 0x02, 0x04,
		(uint8_t)((freq >> 24) & 0xFF), (uint8_t)((freq >> 16) & 0xFF),
		(uint8_t)((freq >> 8) & 0xFF),  (uint8_t)(freq & 0xFF)
	};
	uint16_t crc = modbusCRC(req.data(), (int)req.size());
	req.push_back(crc & 0xFF); req.push_back(crc >> 8);

	std::vector<uint8_t> res;
	if (!sendAndReceive(req, res)) return false;
	// FC 0x10 std reply is 8 bytes
	return (res.size() >= 8 && res[1] == 0x10 && res[3] == (addr & 0xFF));
}

//=========== control: PWM Control (0x06) ===========

bool QX_DO24::setPWM_Control(int channel, uint16_t val) {
	// ch>3 would run past 0x0F into 0x10 = [保存输出] (limited flash write cycles).
	if (!client || channel < 0 || channel > 3) return false;
	uint16_t addr = 0x000C + channel;

	std::vector<uint8_t> req = {
		(uint8_t)deviceID, 0x06, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
		(uint8_t)(val >> 8), (uint8_t)(val & 0xFF)
	};
	uint16_t crc = modbusCRC(req.data(), (int)req.size());
	req.push_back(crc & 0xFF); req.push_back(crc >> 8);

	std::vector<uint8_t> res;
	return (sendAndReceive(req, res) && res == req);
}

//=========== control: save current values as power-on default (0x10) ===========

// ⚠ Touches flash. See the warning block in QX_DO24.h before calling.
// Writes any non-zero value to reg 0x10; the module snapshots 0x00~0x0F.
bool QX_DO24::saveOutputAsDefault() {
	if (!client) return false;

	const uint16_t addr = 0x0010;
	const uint16_t val  = 0x0001;          // manual: "写入一个任意非 0 值"

	std::vector<uint8_t> req = {
		(uint8_t)deviceID, 0x06,
		(uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
		(uint8_t)(val >> 8), (uint8_t)(val & 0xFF)
	};
	uint16_t crc = modbusCRC(req.data(), (int)req.size());
	req.push_back(crc & 0xFF); req.push_back(crc >> 8);

	LOG_INF(_log_tag, "SAVE to flash (reg 0x10) — limited write cycles, once per power-up");

	std::vector<uint8_t> res;
	return (sendAndReceive(req, res) && res == req);
}

//=========== control: readback (FC 0x03) ===========

bool QX_DO24::getPWM_Duty(int channel, double& duty_percent) {
	if (channel < 0 || channel > 3) return false;
	std::vector<uint16_t> regs;
	if (!readRegs(0x0000 + channel, 1, regs)) return false;
	duty_percent = regs[0] / 10.0;
	return true;
}

bool QX_DO24::getPWM_Freq(int channel, uint32_t& freq) {
	if (channel < 0 || channel > 3) return false;
	std::vector<uint16_t> regs;
	if (!readRegs(0x0004 + (channel * 2), 2, regs)) return false;
	freq = (static_cast<uint32_t>(regs[0]) << 16) | regs[1];   // ABCD order, matches setPWM_Freq
	return true;
}

bool QX_DO24::getPWM_Control(int channel, uint16_t& val) {
	if (channel < 0 || channel > 3) return false;
	std::vector<uint16_t> regs;
	if (!readRegs(0x000C + channel, 1, regs)) return false;
	val = regs[0];
	return true;
}

bool QX_DO24::getVersion(uint16_t& version) {
	std::vector<uint16_t> regs;
	if (!readRegs(0x0022, 1, regs)) return false;
	version = regs[0];
	return true;
}

//=========== utility: read holding registers (FC 0x03) ===========

bool QX_DO24::readRegs(uint16_t addr, uint16_t count, std::vector<uint16_t>& out) {
	if (!client) return false;

	std::vector<uint8_t> req = {
		(uint8_t)deviceID, 0x03,
		(uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
		(uint8_t)(count >> 8), (uint8_t)(count & 0xFF)
	};
	uint16_t crc = modbusCRC(req.data(), (int)req.size());
	req.push_back(crc & 0xFF); req.push_back(crc >> 8);

	std::vector<uint8_t> res;
	if (!sendAndReceive(req, res)) return false;

	// expect: slave, 0x03, byteCount, data..., crcLo, crcHi
	size_t expected = 5 + (size_t)count * 2;
	if (res.size() < expected || res[1] != 0x03 || res[2] != count * 2) return false;

	out.clear();
	for (uint16_t i = 0; i < count; ++i) {
		out.push_back((uint16_t)((res[3 + i * 2] << 8) | res[4 + i * 2]));
	}
	return true;
}

//=========== utility: send/receive (500ms window) ===========

// Protocol doc "modbus-RTU_Protocol_description" V1.4 §D 错误返回帧.
// ⚠ NOT the standard Modbus exception layout. Standard is 5 bytes
//     ID | FC|0x80 | ExceptionCode | crcLo | crcHi
// but this vendor inserts a 数据长度 byte (always 0x01), giving 6 bytes:
//     ID | FC|0x80 | 0x01 | ERR | crcLo | crcHi
// so the error code lives at index 3, not index 2.
static const char* qx_err_name(uint8_t err) {
	switch (err) {
		case 0x01: return "功能碼不支援";
		case 0x02: return "暫存器位址不合法";
		case 0x03: return "暫存器值不合法";
		case 0x04: return "校驗錯誤";
		case 0x06: return "裝置忙或該暫存器不可更改";
		case 0xFF: return "其它錯誤";
		default:   return "未定義錯誤碼";
	}
}

bool QX_DO24::sendAndReceive(const std::vector<uint8_t>& request, std::vector<uint8_t>& response) {
	if (!client) return false;

	LOG_HEX(_log_tag, "TX", request.data(), (int)request.size());

	// ⚠ MUST use TCP_client::sendAndReceive (the atomic drain→send→recv), NOT a
	// bare sendData()+receiveData() pair.
	//
	// This module shares the RS485_3 gateway (cli_22_) with JC-100 ×4, PQW relay,
	// XKC water level and the DM2J arm rail — and the Web GUI's PWM panel runs on
	// a different thread from the gait. TCP_client's mutex only covers ONE call,
	// so a send/recv pair releases it in between: another thread can slip its own
	// request into that gap and the two replies get read by the wrong callers.
	//
	// The JC-100 pressure readings are what gate "is this side still sealed
	// enough to let the other side go" during a step, so corrupting them is a
	// fall risk, not just a comms nuisance. JC-100 was migrated to the atomic
	// API for exactly this reason; PWM has to play by the same rule.
	char rx[256];
	int len = client->sendAndReceive(reinterpret_cast<const char*>(request.data()),
	                                 (int)request.size(), rx, sizeof(rx), 500, 500);
	response.clear();
	if (len > 0) response.insert(response.end(), (uint8_t*)rx, (uint8_t*)rx + len);

	LOG_HEX(_log_tag, "RX", response.data(), (int)response.size());

	if (response.size() < 5) return false;
	uint16_t calc_crc = modbusCRC(response.data(), (int)response.size() - 2);
	uint16_t recv_crc = response[response.size() - 2] | (response[response.size() - 1] << 8);
	if (calc_crc != recv_crc) {
		LOG_ERR(_log_tag, "CRC mismatch (calc %04X != recv %04X)", calc_crc, recv_crc);
		return false;
	}

	// Device rejected the request — surface WHY. Without this the caller only
	// sees a generic false and the actual reason (bad address / bad value /
	// device busy) is thrown away, which matters most on first wiring-up.
	if (response[1] & 0x80) {
		uint8_t err = (response.size() >= 4) ? response[3] : 0;   // vendor layout: ERR at [3]
		LOG_ERR(_log_tag, "device rejected FC 0x%02X: err 0x%02X (%s)",
		        (unsigned)(response[1] & 0x7F), (unsigned)err, qx_err_name(err));
		return false;
	}
	return true;
}

//=========== utility: Modbus CRC ===========

uint16_t QX_DO24::modbusCRC(const uint8_t* data, int len) {
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
