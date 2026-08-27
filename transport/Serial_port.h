// 🐛 GUARD 衝突（2026-08-27 發現，尚未修正）
// 本檔與 `user_lib/SerialPort.h` 是**兩個不同的實作**，卻共用同一個 include
// guard `SERIAL_PORT_H`：
//   - 本檔：本專案的序列埠，被 WASH_ROBOT.h / WT901BC_TTL.h / Linux_test 使用
//   - user_lib/SerialPort.h（322 行）：cleaning_arm 的 damiao 那一套，
//     經 user_lib/damiao.h 引入
// 目前不爆，只因兩者的使用者剛好不重疊。**一旦同一個編譯單元同時碰到兩者，
// 第二個會被 guard 靜默吃掉** —— 症狀是「某個 class 莫名找不到」，而編譯錯誤
// 不會指向真因。
// 📌 修正方向（待評估）：把 guard 改成與路徑對應的唯一名稱，或改用 #pragma once。
//    動之前要確認沒有別處依賴這個 guard 名稱做條件編譯。
// 待辦見 .claude/work_log.md 待辦總表。
#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

#include <string>
#include <vector>

enum SerialConfig {
	SERIAL_5N1, SERIAL_6N1, SERIAL_7N1, SERIAL_8N1,
	SERIAL_5N2, SERIAL_6N2, SERIAL_7N2, SERIAL_8N2,
	SERIAL_5E1, SERIAL_6E1, SERIAL_7E1, SERIAL_8E1,
	SERIAL_5E2, SERIAL_6E2, SERIAL_7E2, SERIAL_8E2,
	SERIAL_5O1, SERIAL_6O1, SERIAL_7O1, SERIAL_8O1, 
	SERIAL_5O2, SERIAL_6O2, SERIAL_7O2, SERIAL_8O2  
};

class Serial_port {
public:
	Serial_port();
	~Serial_port();

	bool init(const std::string& port_name, int baudrate = 115200, SerialConfig config = SERIAL_8N1, bool debug = false);
	bool connect();
	void disconnect();
	bool reconnect();
	bool is_connected() const;

	int send(const char* data, int length, int tx_multiplier = 10, int tx_constant = 50);
	int receive(char* buffer, int length, int rx_idle_ms = 1);

private:
#ifdef _WIN32
	void* hSerial;
#else
	int fd;
#endif
	std::string port_name;
	int baudrate;
	SerialConfig config;
	bool connected;
	bool debug_mode;
	std::string _log_tag;

	bool configure_port();
};

#endif