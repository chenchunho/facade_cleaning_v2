#ifndef JC_100_METER_H
#define JC_100_METER_H

#include "TCP_client.h"
#include <string>
#include <vector>
#include <cstdint>
#include <thread> // 跨平台延遲
#include <chrono> // 跨平台時間單位

class JC_100_METER {
public:
	JC_100_METER();
	~JC_100_METER();

	// --- 初始化 ---
	bool init(const std::string& ip, int port, int ID, bool debug = false);
	bool init(TCP_client& extClient, int ID, bool debug = false);

	// --- 核心數據 (保留成功時回傳新值，失敗時回傳舊值邏輯) ---
	int  read_pressure(void);

	// --- 參數讀取 (Getter) 全部保留 ---
	int  get_response_time();
	int  get_pressure_unit();
	int  get_output_mode();
	int  get_output_logic();
	int  get_slave_address();
	int  get_baud_rate();
	int  get_hysteresis();
	int  get_setpoint_1();
	int  get_setpoint_2();
	int  get_display_color();

	// --- 參數設定 (Setter) 全部保留 ---
	bool set_response_time(int value);
	bool set_pressure_unit(int value);
	bool set_output_mode(int value);
	bool set_output_logic(int value);
	bool set_slave_address(int newID);
	bool set_baud_rate(int value);
	bool set_hysteresis(int value);
	bool set_setpoint_1(int value);
	bool set_setpoint_2(int value);
	bool set_display_color(int value);

	// --- 命令 ---
	bool zero_calibration();

	// --- 工具 ---
	uint16_t modbusCRC(const uint8_t* data, int len);

	int error_flag; // 0: 正常, 1: 異常

private:
	TCP_client* client = nullptr;
	int _slaveID;
	bool _debug;
	bool _isExternalClient;
	int _last_pressure = 0;
	bool send_command(uint8_t func, uint16_t reg, uint16_t data, std::vector<uint8_t>& res);
	void log_hex(const std::string& prefix, const uint8_t* data, int len);
};

#endif

//#include "TCP_client.h"
//#include "JC_100_METER.h"
//#include <iostream>
//#include <iomanip>
//#include <chrono>
//#include <thread>
//
//#ifdef _WIN32
//#include <conio.h>
//#else
//#include <stdio.h>
//#include <termios.h>
//#include <unistd.h>
//#include <fcntl.h>
//// Linux 下模擬 kbhit 的簡易實作
//int _kbhit(void) {
//	struct termios oldt, newt;
//	int ch, oldf;
//	tcgetattr(STDIN_FILENO, &oldt);
//	newt = oldt;
//	newt.c_lflag &= ~(ICANON | ECHO);
//	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
//	oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
//	fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
//	ch = getchar();
//	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
//	fcntl(STDIN_FILENO, F_SETFL, oldf);
//	if (ch != EOF) { ungetc(ch, stdin); return 1; }
//	return 0;
//}
//#endif
//
//int main() {
//	TCP_client cli_21;
//	JC_100_METER meter;
//
//	if (!cli_21.connectToServer("192.168.1.21", 4001, false)) {
//		return 1;
//	}
//	meter.init(cli_21, 1, false);
//
//	std::cout << "--- JC-100 壓力監控 (按任意鍵停止) ---" << std::endl;
//
//	auto interval = std::chrono::milliseconds(100);
//	auto next_run = std::chrono::steady_clock::now();
//
//	while (!_kbhit()) {
//		int val = meter.read_pressure();
//		double pressure = val / 10.0;
//
//		std::cout << "\r壓力: " << std::fixed << std::setprecision(1) << std::setw(6) << pressure << " kPa";
//
//		if (meter.error_flag == 1) {
//			std::cout << " [狀態: 通訊中斷!!]" << std::flush;
//		}
//		else {
//			std::cout << " [狀態: 正常]     " << std::flush;
//		}
//
//		next_run += interval;
//		std::this_thread::sleep_until(next_run);
//	}
//
//	cli_21.close();
//	return 0;
//}