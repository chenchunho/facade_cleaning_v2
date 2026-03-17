#include "QX_DO24.h"
#include "TCP_client.h"
#include <cstdio>
#include <cmath>
#include <iostream>
#include <chrono>

QX_DO24::QX_DO24() {}

QX_DO24::~QX_DO24() {
	if (owned_client) owned_client->close();
}

bool QX_DO24::init(const std::string& ip, int port, int ID, bool debugMode) {
	// 解決 Linux 下 std::make_unique 問題 (C++11 相容)
	owned_client = std::unique_ptr<TCP_client>(new TCP_client());
	if (!owned_client->connectToServer(ip, port)) return false;
	client = owned_client.get();
	deviceID = ID;
	debug = debugMode;
	return true;
}

bool QX_DO24::init(TCP_client& extClient, int ID, bool debugMode) {
	client = &extClient;
	deviceID = ID;
	debug = debugMode;
	return true;
}

// 綜合設定：依序設定 Duty -> Freq -> Control
bool QX_DO24::setChannel(int channel, double duty, int freq, uint16_t control) {
	if (debug) std::cout << "[QX_DO24] --- Setting Channel " << channel << " ---" << std::endl;

	if (!setPWM_Duty(channel, duty)) return false;
	if (!setPWM_Freq(channel, freq)) return false;
	if (!setPWM_Control(channel, control)) return false;

	return true;
}

// 1. 設定佔空比 (0x06) - 支援小數點並自動計算 CRC
bool QX_DO24::setPWM_Duty(int channel, double duty_percent) {
	if (!client || channel < 0 || channel > 23) return false;

	// 將 3.8% 轉為 38 (0x26)。如果要得到 0x25 (37)，請輸入 3.7
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
	// 必須收到回傳且內容完全一致 (Echo)
	return (sendAndReceive(req, res) && res == req);
}

// 2. 設定頻率 (0x10) - 32-bit 暫存器寫入
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
	// FC 0x10 的標準回傳為 8 bytes
	return (res.size() >= 8 && res[1] == 0x10 && res[3] == (addr & 0xFF));
}

// 3. 設定控制值 (0x06)
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

// 核心發送與接收邏輯 (限時 500ms)
bool QX_DO24::sendAndReceive(const std::vector<uint8_t>& request, std::vector<uint8_t>& response) {
	if (!client) return false;
	if (debug) {
		printf("[QX_DO24] TX: ");
		for (uint8_t b : request) printf("%02X ", b);
		printf("\n");
	}

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

	if (debug) {
		printf("[QX_DO24] RX (%d bytes): ", (int)response.size());
		for (uint8_t b : response) printf("%02X ", b);
		printf("\n");
	}

	if (response.size() < 5) return false;
	uint16_t calc_crc = modbusCRC(response.data(), (int)response.size() - 2);
	uint16_t recv_crc = response[response.size() - 2] | (response[response.size() - 1] << 8);
	return (calc_crc == recv_crc);
}

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