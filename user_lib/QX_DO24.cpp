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

bool QX_DO24::setChannel(int channel, double duty, int freq, uint16_t control) {
	LOG_DBG(_log_tag, "--- Setting Channel %d ---", channel);

	if (!setPWM_Duty(channel, duty)) return false;
	if (!setPWM_Freq(channel, freq)) return false;
	if (!setPWM_Control(channel, control)) return false;

	return true;
}

//=========== control: PWM Duty (0x06) ===========

// Widen/narrow the duty safety window. Must be called EXPLICITLY — the
// constructor defaults are the conservative 5~10% of the currently wired motor
// so that forgetting to configure yields a safe range, not a dangerous one.
void QX_DO24::setDutyLimits(double min_pct, double max_pct) {
	if (min_pct > max_pct) return;                       // nonsense range: ignore
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
	if (duty_percent < 0.0 || duty_percent > 100.0) return false;   // reg range 0~1000

	// Safety clamp — see setDutyLimits() in the header. Defaults to the wired
	// motor's 5%=stop / 10%=full-speed window; anything outside is refused
	// rather than clamped, so a wrong number is a visible failure, not a
	// silently-different speed.
	if (duty_percent < duty_min_pct || duty_percent > duty_max_pct) {
		LOG_ERR(_log_tag, "duty %.1f%% outside safety limits [%.1f, %.1f] — refused",
		        duty_percent, duty_min_pct, duty_max_pct);
		return false;
	}

	uint16_t val = static_cast<uint16_t>(std::round(duty_percent * 10.0));
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

bool QX_DO24::sendAndReceive(const std::vector<uint8_t>& request, std::vector<uint8_t>& response) {
	if (!client) return false;

	LOG_HEX(_log_tag, "TX", request.data(), (int)request.size());

	if (!client->sendData(reinterpret_cast<const char*>(request.data()), (int)request.size(), 500)) return false;

	response.clear();
	uint8_t buf[256];
	size_t expected_len;
	if (request[1] == 0x10)      expected_len = 8;                                             // write-multi fixed reply
	else if (request[1] == 0x03) expected_len = 5 + ((request[4] << 8) | request[5]) * 2;       // read reply: hdr+data+crc
	else                          expected_len = request.size();                                // write-single: echo
	auto start = std::chrono::steady_clock::now();

	while (true) {
		int n = client->receiveData(reinterpret_cast<char*>(buf), sizeof(buf), 20);
		if (n > 0) response.insert(response.end(), buf, buf + n);

		if (response.size() >= expected_len) break;

		auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > 500) break;
	}

	LOG_HEX(_log_tag, "RX", response.data(), (int)response.size());

	if (response.size() < 5) return false;
	uint16_t calc_crc = modbusCRC(response.data(), (int)response.size() - 2);
	uint16_t recv_crc = response[response.size() - 2] | (response[response.size() - 1] << 8);
	return (calc_crc == recv_crc);
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
