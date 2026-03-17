#ifndef QX_DO24_H
#define QX_DO24_H

#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <iostream>

class TCP_client;

class QX_DO24 {
public:
	QX_DO24();
	~QX_DO24();

	// 初始化
	bool init(const std::string& ip, int port, int ID = 1, bool debug = false);
	bool init(TCP_client& extClient, int ID = 1, bool debug = false);

	// 綜合控制：依序執行 Duty -> Freq -> Control，全部成功才回傳 true
	bool setChannel(int channel, double duty, int freq, uint16_t control);

	// 獨立控制函式 (皆含 500ms 等待與正確性檢查)
	bool setPWM_Duty(int channel, double duty_percent);
	bool setPWM_Freq(int channel, int freq);
	bool setPWM_Control(int channel, uint16_t val);

private:
	TCP_client* client = nullptr;
	std::unique_ptr<TCP_client> owned_client;
	int deviceID = 6;
	bool debug = false;

	// 通訊核心
	bool sendAndReceive(const std::vector<uint8_t>& request, std::vector<uint8_t>& response);
	uint16_t modbusCRC(const uint8_t* data, int len);
};

#endif