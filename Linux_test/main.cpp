#include "Serial_port.h"
#include "WT901BC_TTL.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>

int main() {
	Serial_port mySerial;
	WT901BC_TTL imu;

	if (mySerial.init("/dev/ttyUSB0", 115200, SERIAL_8N1, false)) {
		imu.init(&mySerial, false);

		std::cout << "\n(Roll, Pitch, Yaw, Pressure)...\n" << std::endl;

		while (true) {
			// 1. 先將填充字元設回空白，確保 Roll/Pitch/Yaw 正常顯示
			std::cout << "\r" << std::setfill(' ');

			std::cout << "R:" << std::fixed << std::setprecision(2) << std::setw(7) << imu.x << " "
				<< "P:" << std::fixed << std::setprecision(2) << std::setw(7) << imu.y << " "
				<< "Y:" << std::fixed << std::setprecision(2) << std::setw(7) << imu.z << " | ";

			std::cout << "Press: " << std::fixed << std::setprecision(2) << std::setw(8) << imu.pressure << " hPa | ";
			std::cout << "altitude: " << std::fixed << std::setprecision(2) << std::setw(8) << imu.altitude << " M | ";
			// 2. 只有在顯示錯誤計數時才切換到 '0'，顯示完立即切換回 ' ' (空白)
			std::cout << "Err: " << (imu.read_error ? "YES" : "NO ") << " ("
				<< std::setfill('0') << std::setw(4) << imu.error_count
				<< std::setfill(' ') << ")"
				<< "    " << std::flush;

			std::this_thread::sleep_for(std::chrono::milliseconds(50));

		}
	}
	return 0;
}