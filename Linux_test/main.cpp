#include "Serial_port.h"
#include "PQW_IO_16O_RLY.h"
#include "ZDT_motor_control.h"
#include "DM2J_RS570.h"
#include "WT901BC_TTL.h"
#include "JC_100_METER.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <sstream>
using namespace std;
#define vacuum_valve_left	3

int main() {
	Serial_port mySerial;
	WT901BC_TTL imu;
	TCP_client cli_21;

	/*
	DM2J_RS570 drv_1;
	drv_1.init(cli_21, 2, false);
	double a = 0;
	//drv_1.read_position_cm(a);
	drv_1.PR_move_cm(
		0,        // PR0
		1,        // mode 1 = Absolute
		100,      // rpm
		5,       // cm
		100,      // acc
		100       // dec
	);
	cout << "step" << endl;
	return 0;
	*/
	// 測試pqw 抽真空
	/*
	for (int i = 0; i < 16; i++) {
		PQW_IO_16O_RLY relay;
		cout << "init" << endl;
		cli_21.connectToServer("10.0.0.48", 4001);
		relay.init(cli_21, i, false);
		cout << i << endl;
		relay.controlRelay(vacuum_valve_left, true);
	}
	return 0;
	*/
	// 測試zdt 

	ZDT_motor_control m1;
	if (!cli_21.connectToServer("10.0.0.48", 4001, false)) {
		std::cerr << "Failed to connect to server." << std::endl;
		system("PAUSE");
		return 1;
	}

	// --- Init motor (shared TCP, Slave ID = 7) ---
	if (!m1.init(cli_21, 7, true)) {
		std::cerr << "Failed to init ZDT motor." << std::endl;
		cli_21.close();
		return 1;
	}
	std::cout << "=== ZDT Motor Control Test ===" << std::endl;
	std::cout << "Commands:" << std::endl;
	std::cout << "  enable             - Enable motor" << std::endl;
	std::cout << "  disable            - Disable motor" << std::endl;
	std::cout << "  zero               - Set current position as zero" << std::endl;
	std::cout << "  status             - Read motor status" << std::endl;
	std::cout << "  stop               - Emergency stop" << std::endl;
	std::cout << "  pos <rpm> <pulse>  - Position mode (e.g. pos 500 72000)" << std::endl;
	std::cout << "  speed <rpm>        - Speed mode (e.g. speed 300)" << std::endl;
	std::cout << "  home               - Go to zero position (pulse=0)" << std::endl;
	std::cout << "  release stall      - Release stall flag" << std::endl;
	std::cout << "  reset              - Reset motor" << std::endl;
	std::cout << "  calibrate          - Calibrate encoder" << std::endl;
	std::cout << "  exit               - Exit program" << std::endl;
	std::cout << "==============================" << std::endl;

	std::string cmd;
	while (true) {
		std::cout << "> ";
		std::getline(std::cin, cmd);

		if (cmd == "exit") {
			break;
		}
		else if (cmd == "enable") {
			m1.motion_control_driver_EN(1);
			std::cout << "Motor enabled." << std::endl;
		}
		else if (cmd == "disable") {
			m1.motion_control_driver_EN(0);
			std::cout << "Motor disabled." << std::endl;
		}
		else if (cmd == "zero") {
			m1.set_zero();
			std::cout << "Zero position set." << std::endl;
		}
		else if (cmd == "status") {
			if (m1.get_system_status()) {
				std::cout << "--- Motor Status ---" << std::endl;
				std::cout << "  Bus Voltage:   " << m1.status.bus_voltage << " mV" << std::endl;
				std::cout << "  Bus Current:   " << m1.status.bus_current << " mA" << std::endl;
				std::cout << "  Temperature:   " << m1.status.temperature << " C" << std::endl;
				std::cout << "  Real Position: " << m1.status.real_pos << " deg" << std::endl;
				std::cout << "  Real Speed:    " << m1.status.real_speed << " RPM" << std::endl;
				std::cout << "  Target Pos:    " << m1.status.target_pos << " deg" << std::endl;
				std::cout << "  Pos Error:     " << m1.status.pos_error << " deg" << std::endl;
				std::cout << "  Enabled:       " << (m1.status.is_enabled ? "Yes" : "No") << std::endl;
				std::cout << "  Pos Reached:   " << (m1.status.pos_reached ? "Yes" : "No") << std::endl;
				std::cout << "  Stall Flag:    " << (m1.status.stall_flag ? "Yes" : "No") << std::endl;
			}
			else {
				std::cerr << "Failed to read status." << std::endl;
			}
		}
		else if (cmd == "stop") {
			m1.emergency_stop(false);
			std::cout << "Emergency stop sent." << std::endl;
		}
		else if (cmd.rfind("pos", 0) == 0) {
			// pos <rpm> <pulse>
			std::istringstream ss(cmd);
			std::string keyword;
			int rpm = 0, pulse = 0;
			ss >> keyword >> rpm >> pulse;

			if (ss.fail()) {
				std::cout << "Usage: pos <rpm> <pulse>" << std::endl;
				std::cout << "  Example: pos 500 72000" << std::endl;
			}
			else {
				// dir=0, acc=255, mode=1(absolute), sync=0, retry=1
				m1.motion_control_pos_mode(0, 255, rpm, pulse, 1, 0, 1);
				std::cout << "Position move: RPM=" << rpm << " Pulse=" << pulse << std::endl;
			}
		}
		else if (cmd.rfind("speed", 0) == 0) {
			std::istringstream ss(cmd);
			std::string keyword;
			int rpm = 0;
			ss >> keyword >> rpm;

			if (ss.fail()) {
				std::cout << "Usage: speed <rpm>" << std::endl;
			}
			else {
				// dir=0, acc=255, sync=0, retry=1
				m1.motion_control_speed_mode(0, 255, rpm, 0, 1);
				std::cout << "Speed mode: RPM=" << rpm << std::endl;
			}
		}
		else if (cmd == "home") {
			// 回到零位: pulse=0, absolute mode
			m1.motion_control_pos_mode(0, 255, 500, 0, 1, 0, 1);
			std::cout << "Homing to zero position..." << std::endl;
		}
		else if (cmd == "release stall") {
			m1.release_stall_flag();
			std::cout << "Stall flag released." << std::endl;
		}
		else if (cmd == "reset") {
			m1.reset_motor();
			std::cout << "Motor reset." << std::endl;
		}
		else if (cmd == "calibrate") {
			m1.calibrate_encoder();
			std::cout << "Encoder calibration started." << std::endl;
		}
		else if (!cmd.empty()) {
			std::cout << "Unknown command: " << cmd << std::endl;
		}
	}

	
	// 測試jc100 壓力表
	/*	while (true) {
		for (int i = 0; i < 16; i++) {
			JC_100_METER meter_1;
			meter_1.init(cli_21, i, true);
			int val = meter_1.read_pressure();
			double pressure = val;
			std::cout << "\r壓力: " << std::fixed << std::setprecision(1) << std::setw(6) << pressure << " kPa" << std::endl;
			sleep(0.5);
		}
	}
	*/

	if (mySerial.init("/dev/ttyUSB3", 115200, SERIAL_8N1, false)) {
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