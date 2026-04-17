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

// 1. set duty ratio — supports fractional (3.8 -> 38 = 0x26)
bool QX_DO24::setPWM_Duty(int channel, double duty_percent) {
	if (!client || channel < 0 || channel > 23) return false;

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
	if (!client || channel < 0 || channel > 23) return false;
	uint16_t addr = 0x0004 + (channel * 2);

	std::vector<uint8_t> req = {
		(uint8_t)deviceID, 0x10, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
		0x00, 0x02, 0x04, 0x00, 0x00, (uint8_t)(freq >> 8), (uint8_t)(freq & 0xFF)
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
	if (!client || channel < 0 || channel > 23) return false;
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

//=========== utility: send/receive (500ms window) ===========

bool QX_DO24::sendAndReceive(const std::vector<uint8_t>& request, std::vector<uint8_t>& response) {
	if (!client) return false;

	LOG_HEX(_log_tag, "TX", request.data(), (int)request.size());

	if (!client->sendData(reinterpret_cast<const char*>(request.data()), (int)request.size(), 500)) return false;

	response.clear();
	uint8_t buf[256];
	size_t expected_len = (request[1] == 0x10) ? 8 : request.size();
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
