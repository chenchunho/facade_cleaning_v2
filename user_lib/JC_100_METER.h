#ifndef JC_100_METER_H
#define JC_100_METER_H

#include "TCP_client.h"
#include <string>
#include <vector>
#include <cstdint>

/**
 * JC-100-RS485 壓力感測器類別
 * 通訊協定: Modbus RTU (Over TCP)
 */
class JC_100_METER {
public:
	JC_100_METER();
	~JC_100_METER();

	// --- 初始化 ---
	bool init(const std::string& ip, int port, int ID, bool debug = false);
	bool init(TCP_client& extClient, int ID, bool debug = false);

	// --- 核心數據 ---
	int  read_pressure(void);                // 0001H: 當前壓力值 (需根據型號除以 10 或 1000)

	// --- 參數讀取 (Getter) ---
	int  get_response_time();               // 0011H: 反應時間 (0:2.5ms, 1:5ms, 2:10ms, 3:20ms...)
	int  get_pressure_unit();               // 0012H: 單位 (0:kPa, 1:MPa, 2:kgf/cm2, 3:bar...)
	int  get_output_mode();                 // 0013H: 模式 (0:遲滯模式, 1:窗型比較模式)
	int  get_output_logic();                // 0014H: 邏輯 (0:NO 常開, 1:NC 常閉)
	int  get_slave_address();               // 0015H: 站號 (1-247)
	int  get_baud_rate();                   // 0016H: 波特率 (0:1200, 1:2400, 2:4800, 3:9600, 4:19200)
	int  get_hysteresis();                  // 0017H: 遲滯設定
	int  get_setpoint_1();                  // 0018H: 比較值 1 (P-1)
	int  get_setpoint_2();                  // 0019H: 比較值 2 (P-2)
	int  get_display_color();               // 001AH: 顏色 (0:常紅/動綠, 1:常綠/動紅, 2:常紅, 3:常綠)

	// --- 參數設定 (Setter) ---
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
	bool zero_calibration();                // 0020H: 校零指令 (寫入 1)

	// --- 工具 ---
	uint16_t modbusCRC(const uint8_t* data, int len);

	// --- 異常狀態標記 ---
	// 0: 通訊正常, 1: 通訊異常 (Timeout 或 CRC 錯誤)
	int error_flag;

private:
	TCP_client* client = nullptr;
	int _slaveID;
	bool _debug;
	bool _isExternalClient;
	int _last_pressure = 0; // 用於儲存最後一次成功的讀取值
	bool send_command(uint8_t func, uint16_t reg, uint16_t data, std::vector<uint8_t>& res);
	void log_hex(const std::string& prefix, const uint8_t* data, int len);
};

#endif

/*  main example

#include "TCP_client.h"
#include "JC_100_METER.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <conio.h>

TCP_client cli_21;
JC_100_METER meter;

int main() {
	if (!cli_21.connectToServer("192.168.1.21", 4001, false)) {
		return 1;
	}
	meter.init(cli_21, 1, false);

	std::cout << "--- JC-100 壓力監控 (異常時保持舊值) ---" << std::endl;

	auto interval = std::chrono::milliseconds(100);
	auto next_run = std::chrono::steady_clock::now();

	while (!_kbhit()) {
		// 即使通訊斷開，此函式現在也會回傳 last_pressure
		int val = meter.read_pressure();
		double pressure = val / 10.0;

		std::cout << "\r壓力: " << std::fixed << std::setprecision(1) << std::setw(6) << pressure << " kPa";

		// 另外印出 error_flag 提醒使用者
		if (meter.error_flag == 1) {
			std::cout << " [狀態: 通訊中斷!!]" << std::flush;
		}
		else {
			std::cout << " [狀態: 正常]     " << std::flush;
		}

		next_run += interval;
		std::this_thread::sleep_until(next_run);
	}

	cli_21.close();
	return 0;
}
*/
