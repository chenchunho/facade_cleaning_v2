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