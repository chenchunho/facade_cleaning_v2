// ------------ Cross-platform Sleep() & Pause() ------------
#ifdef _WIN32
#include <windows.h>      
#define Pause() system("PAUSE")
#else
#include <unistd.h>      
#include <iostream>

#define Sleep(ms) usleep((ms) * 1000)
#define Pause() do { \
        std::cout << "Press ENTER to continue..."; \
        std::cin.get(); \
    } while(0)
#endif



#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <string>
#include <sstream>
#include <thread>
#include <iomanip>

#include "PQW_IO_16O_RLY.h"
#include "DM2J_RS570.h"
#include "ZDT_motor_control.h"
#include "JC_100_METER.h"

#define vacuum_motor		11
#define vacuum_valve_left	12
#define vacuum_valve_right	14
#define vacuum_valve_center	13

TCP_client cli_20;
TCP_client cli_21;
PQW_IO_16O_RLY relay;
PQW_IO_16O_RLY relay_2;
DM2J_RS570 drv_1;
DM2J_RS570 drv_2;
DM2J_RS570 drv_3;
DM2J_RS570 drv_4;
ZDT_motor_control m1;
ZDT_motor_control m2;
ZDT_motor_control m3;
ZDT_motor_control m4;
ZDT_motor_control m5;
ZDT_motor_control m6;
ZDT_motor_control m7;
JC_100_METER meter_1;
JC_100_METER meter_2;
JC_100_METER meter_3;
JC_100_METER meter_4;
JC_100_METER meter_5;
JC_100_METER meter_6;
JC_100_METER meter_7;


void waitEnter();
void doInit();
void doVacuumEnable();
void doVacuumDisable();
void doLeftVacuumEnable();
void doLeftVacuumDisable();
void doRightVacuumEnable();
void doRightVacuumDisable();
void doCenterVacuumEnable();
void doCenterVacuumDisable();
void doShutdown();
void doMove_1(int rpm, double cm);
void doMove_2(int rpm, double cm);
void doMove_3(int rpm, double cm);
void doMove_4(int rpm, double cm);
void doMove_sync(int rpm, double cm);
void doWashEable();
void doWashDisable();
bool askPositionOK(int x);
int askNewX();


int main() {

	if (!cli_20.connectToServer("192.168.1.20", 4001)) {
		std::cerr << "Failed to connect 485 controller." << std::endl;
		system("PAUSE");
		return 1;
	}
	if (!cli_21.connectToServer("192.168.1.21", 4001)) {
		std::cerr << "Failed to connect 485 controller." << std::endl;
		system("PAUSE");
		return 1;
	}

	if (!drv_1.init(cli_20, 1, false)) {
		std::cerr << "Failed to connect DM2J_RS570 controller 1." << std::endl;
		return 1;
	}
	if (!drv_2.init(cli_20, 2, false)) {
		std::cerr << "Failed to connect DM2J_RS570 controller 2." << std::endl;
		return 1;
	}
	if (!drv_3.init(cli_20, 3, false)) {
		std::cerr << "Failed to connect DM2J_RS570 controller 3." << std::endl;
		return 1;
	}
	if (!drv_4.init(cli_20, 4, false)) {
		std::cerr << "Failed to connect DM2J_RS570 controller 4." << std::endl;
		return 1;
	}

	if (!m1.init(cli_21, 2)) {
		std::cerr << "Failed to connect ZDT_motor_control 1." << std::endl;
		return 1;
	}
	if (!m2.init(cli_21, 3)) {
		std::cerr << "Failed to connect ZDT_motor_control 2." << std::endl;
		return 1;
	}
	if (!m3.init(cli_21, 4)) {
		std::cerr << "Failed to connect ZDT_motor_control 3." << std::endl;
		return 1;
	}
	if (!m4.init(cli_21, 5)) {
		std::cerr << "Failed to connect ZDT_motor_control 4." << std::endl;
		return 1;
	}
	if (!m5.init(cli_21, 6)) {
		std::cerr << "Failed to connect ZDT_motor_control 5." << std::endl;
		return 1;
	}
	if (!m6.init(cli_21, 7)) {
		std::cerr << "Failed to connect ZDT_motor_control 6." << std::endl;
		return 1;
	}
	if (!m7.init(cli_21, 8)) {
		std::cerr << "Failed to connect ZDT_motor_control 7." << std::endl;
		return 1;
	}

	if (!meter_1.init(cli_21, 9)) {
		std::cerr << "Failed to connect pressure meter 1." << std::endl;
		return 1;
	}
	if (!meter_2.init(cli_21, 10)) {
		std::cerr << "Failed to connect pressure meter 2." << std::endl;
		return 1;
	}
	if (!meter_3.init(cli_21, 11)) {
		std::cerr << "Failed to connect pressure meter 3." << std::endl;
		return 1;
	}
	if (!meter_4.init(cli_21, 12)) {
		std::cerr << "Failed to connect pressure meter 4." << std::endl;
		return 1;
	}
	if (!meter_5.init(cli_21, 13)) {
		std::cerr << "Failed to connect pressure meter 5." << std::endl;
		return 1;
	}
	if (!meter_6.init(cli_21, 14)) {
		std::cerr << "Failed to connect pressure meter 6." << std::endl;
		return 1;
	}
	if (!meter_7.init(cli_21, 15)) {
		std::cerr << "Failed to connect pressure meter 7." << std::endl;
		return 1;
	}

	if (!relay.init(cli_21, 1)) {
		std::cerr << "Failed to connect relay controller." << std::endl;
		return 1;
	}
	if (!relay_2.init(cli_21, 16)) {
		std::cerr << "Failed to connect relay controller." << std::endl;
		return 1;
	}

	std::string cmd;
	while (true) {
		std::cout << "> ";
		std::getline(std::cin, cmd);

		if (cmd == "exit") {
			break;
		}

		else if (cmd == "init") {
			doInit();
		}
		else if (cmd == "vacuum enable") {
			doVacuumEnable();
		}
		else if (cmd == "vacuum disable") {
			doVacuumDisable();
		}
		else if (cmd == "left vacuum enable") {
			doLeftVacuumEnable();
		}
		else if (cmd == "left vacuum disable") {
			doLeftVacuumDisable();
		}
		else if (cmd == "right vacuum enable") {
			doRightVacuumEnable();
		}
		else if (cmd == "right vacuum disable") {
			doRightVacuumDisable();
		}
		else if (cmd == "center vacuum enable") {
			doCenterVacuumEnable();
		}
		else if (cmd == "center vacuum disable") {
			doCenterVacuumDisable();
		}
		else if (cmd == "shutdown") {
			doShutdown();
		}

		else if (cmd == "move1 get pos") {
			double a = 0;
			drv_1.read_position_cm(a);
			std::cout << "當前位置: " << a << " cm" << std::endl;
		}
		else if (cmd == "move1 set zero") {
			drv_1.home_set_current_pos_zero();
		}
		else if (cmd.rfind("move1", 0) == 0) {  // 判斷是否以 "move" 開頭
			std::stringstream ss(cmd);
			std::string keyword;
			int rpm = 0, cm = 0;

			ss >> keyword;   // 讀出 "move"
			ss >> rpm >> cm; // 讀出兩個數字

			if (ss.fail()) {
				std::cout << "Usage: move <rpm> <cm>" << std::endl;
			}
			else {
				std::cout << "Move: RPM=" << rpm << "  CM=" << cm << std::endl;
				doMove_1(rpm, cm);
			}
		}

		else if (cmd == "move2 get pos") {
			double a = 0;
			drv_2.read_position_cm(a);
			std::cout << "當前位置: " << a << " cm" << std::endl;
		}
		else if (cmd == "move2 set zero") {
			drv_2.home_set_current_pos_zero();
		}
		else if (cmd.rfind("move2", 0) == 0) {  // 判斷是否以 "move" 開頭
			std::stringstream ss(cmd);
			std::string keyword;
			int rpm = 0, cm = 0;

			ss >> keyword;   // 讀出 "move"
			ss >> rpm >> cm; // 讀出兩個數字

			if (ss.fail()) {
				std::cout << "Usage: move <rpm> <cm>" << std::endl;
			}
			else {
				std::cout << "Move: RPM=" << rpm << "  CM=" << cm << std::endl;
				doMove_2(rpm, cm);
			}
		}

		else if (cmd == "move3 get pos") {
			double a = 0;
			drv_3.read_position_cm(a);
			std::cout << "當前位置: " << a << " cm" << std::endl;
		}
		else if (cmd == "move3 set zero") {
			drv_3.home_set_current_pos_zero();
		}
		else if (cmd.rfind("move3", 0) == 0) {  // 判斷是否以 "move" 開頭
			std::stringstream ss(cmd);
			std::string keyword;
			int rpm = 0, cm = 0;

			ss >> keyword;   // 讀出 "move"
			ss >> rpm >> cm; // 讀出兩個數字

			if (ss.fail()) {
				std::cout << "Usage: move <rpm> <cm>" << std::endl;
			}
			else {
				std::cout << "Move: RPM=" << rpm << "  CM=" << cm << std::endl;
				doMove_3(rpm, cm);
			}
		}

		else if (cmd == "move4 get pos") {
			double a = 0;
			drv_4.read_position_cm(a);
			std::cout << "當前位置: " << a << " cm" << std::endl;
		}
		else if (cmd == "move4 set zero") {
			drv_4.home_set_current_pos_zero();
		}
		else if (cmd.rfind("move4", 0) == 0) {  // 判斷是否以 "move" 開頭
			std::stringstream ss(cmd);
			std::string keyword;
			int rpm = 0, cm = 0;

			ss >> keyword;   // 讀出 "move"
			ss >> rpm >> cm; // 讀出兩個數字

			if (ss.fail()) {
				std::cout << "Usage: move <rpm> <cm>" << std::endl;
			}
			else {
				std::cout << "Move: RPM=" << rpm << "  CM=" << cm << std::endl;
				doMove_4(rpm, cm);
			}
		}

		else if (cmd.rfind("get meter", 0) == 0)
		{
			//std::stringstream iss(cmd)
			//	int x = -1;
		}
		else if (cmd == "1") {
			int val = meter_1.read_pressure();
			int val2 = meter_2.read_pressure();

			double pressure = val ;
			double pressure2 = val2 ;
			std::cout << "\r壓力: " << std::fixed << std::setprecision(1) << std::setw(6) << pressure << " kPa" << std::endl;
			std::cout << "\r壓力: " << std::fixed << std::setprecision(1) << std::setw(6) << pressure2 << " kPa" << std::endl;

			std::cerr << "M1 test." << std::endl;
		}
		else if (cmd == "2") {
			relay_2.controlRelay(3, false);
			std::cerr << "M1 test." << std::endl;
		}
		else if (cmd == "3") {
			m1.motion_control_pos_mode(00, 255, 1000, 144000, 1, 0, 1);
			m2.motion_control_pos_mode(00, 255, 1000, 144000, 1, 0, 1);
			m3.motion_control_pos_mode(00, 255, 300, 30000, 1, 0, 1);
			m4.motion_control_pos_mode(00, 255, 300, 30000, 1, 0, 1);
			m5.motion_control_pos_mode(00, 255, 300, 30000, 1, 0, 1);
			m6.motion_control_pos_mode(00, 255, 1000, 144000, 1, 0, 1);
			m7.motion_control_pos_mode(00, 255, 1000, 144000, 1, 0, 1);
			std::cerr << "M1 test." << std::endl;
		}
		else if (cmd == "4") {
			m1.motion_control_pos_mode(00, 255, 1000, 0, 1, 0, 1);
			m2.motion_control_pos_mode(00, 255, 1000, 0, 1, 0, 1);
			m3.motion_control_pos_mode(00, 255, 300, 0, 1, 0, 1);
			m4.motion_control_pos_mode(00, 255, 300, 0, 1, 0, 1);
			m5.motion_control_pos_mode(00, 255, 300, 0, 1, 0, 1);
			m6.motion_control_pos_mode(00, 255, 1000, 0, 1, 0, 1);
			m7.motion_control_pos_mode(00, 255, 1000, 0, 1, 0, 1);
			std::cerr << "M1 test." << std::endl;
		}

		else if (cmd == "wash enable") {
			doWashEable();
		}
		else if (cmd == "wash disable") {
			doWashDisable();
		}

		// ---------------------------------------------
		// 處理 "set process up X"
		// ---------------------------------------------
		else if (cmd.rfind("upsync", 0) == 0) {

			int x = -1;  // 用來儲存 X
			std::istringstream iss(cmd);

			std::string t1;
			iss >> t1 >> x;
			// t1 = set
			// t2 = process
			// t3 = up
			// x  = 數字

			if (x < 0) {
				std::cout << "Usage: set process up <number>" << std::endl;
				break;
			}

			std::cout << "Running: process upsync with X = " << x << std::endl;

			x = x * -1;
			// ------------------------
			//   開始執行流程
			// ------------------------
			//Right move
			doLeftVacuumDisable();
			Sleep(200);
			doRightVacuumDisable();
			waitEnter();

			while (true) {
				doMove_sync(700, x);
				if (askPositionOK(x)) {
					std::cout << "位置確認完成\n";
					break;
				}

				x = askNewX()*-1;   // 重新取得使用者輸入
			}
			waitEnter();
			doLeftVacuumEnable();
			doRightVacuumEnable();
			while (true) {
				if (askPositionOK(x)) {
					std::cout << "位置確認完成\n";
					break;
				}
				doLeftVacuumDisable();
				Sleep(200);
				doRightVacuumDisable();
				x = askNewX()*-1;   // 重新取得使用者輸入
				doMove_sync(700, x);
				doLeftVacuumEnable();
				doRightVacuumEnable();
			}


			//center move
			doCenterVacuumDisable();
			Sleep(3000);
			waitEnter();
			doMove_sync(700, 0);
			waitEnter();
			doCenterVacuumEnable();
			std::cout << "Process Done." << std::endl;
		}

		else if (cmd.rfind("up", 0) == 0) {

			int x = -1;  // 用來儲存 X
			std::istringstream iss(cmd);

			std::string t1;
			iss >> t1 >> x;
			// t1 = set
			// t2 = process
			// t3 = up
			// x  = 數字

			if (x < 0) {
				std::cout << "Usage: set process up <number>" << std::endl;
				break;
			}

			std::cout << "Running: process up with X = " << x << std::endl;

			x = x * -1;
			// ------------------------
			//   開始執行流程
			// ------------------------
			//Right move
			doRightVacuumDisable();
			waitEnter();

			while (true) {
				doMove_2(700, x);

				if (askPositionOK(x)) {
					std::cout << "位置確認完成\n";
					break;
				}

				x = askNewX()*-1;   // 重新取得使用者輸入
			}
			waitEnter();
			doRightVacuumEnable();
			while (true) {
				if (askPositionOK(x)) {
					std::cout << "位置確認完成\n";
					break;
				}
				doRightVacuumDisable();
				x = askNewX()*-1;   // 重新取得使用者輸入
				doMove_2(700, x);
				doRightVacuumEnable();
			}

			//Left move
			waitEnter();
			doLeftVacuumDisable();
			waitEnter();

			while (true) {
				doMove_3(700, x);
				if (askPositionOK(x)) {
					std::cout << "位置確認完成\n";
					break;
				}
				x = askNewX()*-1;   // 重新取得使用者輸入
			}
			waitEnter();
			doLeftVacuumEnable();

			while (true) {
				if (askPositionOK(x)) {
					std::cout << "位置確認完成\n";
					break;
				}
				doLeftVacuumDisable();
				x = askNewX()*-1;   // 重新取得使用者輸入
				doMove_3(700, x);
				doLeftVacuumEnable();
			}

			//center move
			doCenterVacuumDisable();
			Sleep(3000);
			waitEnter();
			doMove_sync(700, 0);
			waitEnter();
			doCenterVacuumEnable();
			std::cout << "Process Done." << std::endl;
		}
		else if (cmd.rfind("down", 0) == 0) {

			int x = -1;  // 用來儲存 X
			std::istringstream iss(cmd);

			std::string t1;
			iss >> t1 >> x;
			// t1 = set
			// t2 = process
			// t3 = up
			// x  = 數字

			if (x < 0) {
				std::cout << "Usage: set process up <number>" << std::endl;
				break;
			}

			std::cout << "Running: process down with X = " << x << std::endl;


			// ------------------------
			//   開始執行流程
			// ------------------------
			//center move
			doCenterVacuumDisable();
			Sleep(3000);
			waitEnter();
			doMove_sync(700, x * -1);
			waitEnter();
			doCenterVacuumEnable();

			waitEnter();

			//Right move
			doRightVacuumDisable();
			waitEnter();

			while (true) {
				doMove_2(700, 0);

				if (askPositionOK(x)) {
					std::cout << "位置確認完成\n";
					break;
				}

				x = askNewX()*-1;   // 重新取得使用者輸入
			}
			waitEnter();
			doRightVacuumEnable();
			while (true) {
				if (askPositionOK(x)) {
					std::cout << "位置確認完成\n";
					break;
				}
				doRightVacuumDisable();
				x = askNewX()*-1;   // 重新取得使用者輸入
				doMove_2(700, x);
				doRightVacuumEnable();
			}


			//Left move
			waitEnter();
			doLeftVacuumDisable();
			waitEnter();

			while (true) {
				doMove_3(700, 0);
				if (askPositionOK(x)) {
					std::cout << "位置確認完成\n";
					break;
				}
				x = askNewX()*-1;   // 重新取得使用者輸入
			}
			waitEnter();
			doLeftVacuumEnable();

			while (true) {
				if (askPositionOK(x)) {
					std::cout << "位置確認完成\n";
					break;
				}
				doLeftVacuumDisable();
				x = askNewX()*-1;   // 重新取得使用者輸入
				doMove_3(700, x);
				doLeftVacuumEnable();
			}

			std::cout << "Process Done." << std::endl;
		}


		else if (!cmd.empty()) {
			std::cout << "Unknown command: " << cmd << std::endl;
		}
	}

	return 0;
}

void waitEnter() {
	std::cout << "Press Enter to continue...";
	std::cin.get();
}

// ---------------------- 副程式區 ----------------------
void doInit() {
	relay.controlRelay(vacuum_valve_left, false);
	relay.controlRelay(vacuum_valve_right, false);
	relay.controlRelay(vacuum_valve_center, false);
	relay.controlRelay(vacuum_motor, true);
	m1.motion_control_pos_mode(00, 255, 1000, 144000, 1, 0, 1);
	m2.motion_control_pos_mode(00, 255, 1000, 144000, 1, 0, 1);
	m3.motion_control_pos_mode(00, 255, 300, 30000, 1, 0, 1);
	m4.motion_control_pos_mode(00, 255, 300, 30000, 1, 0, 1);
	m5.motion_control_pos_mode(00, 255, 300, 30000, 1, 0, 1);
	m6.motion_control_pos_mode(00, 255, 1000, 144000, 1, 0, 1);
	m7.motion_control_pos_mode(00, 255, 1000, 144000, 1, 0, 1);
	doWashDisable();
	std::cout << "Initialization done." << std::endl;
}

void doVacuumEnable() {
	relay.controlRelay(vacuum_valve_left, true);
	relay.controlRelay(vacuum_valve_right, true);
	relay.controlRelay(vacuum_valve_center, true);
	std::cout << "vacuum enable." << std::endl;
}
void doVacuumDisable() {
	relay.controlRelay(vacuum_valve_left, false);
	relay.controlRelay(vacuum_valve_right, false);
	relay.controlRelay(vacuum_valve_center, false);
	std::cout << "vacuum disable." << std::endl;
}

void doRightVacuumEnable() {
	relay.controlRelay(vacuum_valve_left, true);
	m1.motion_control_pos_mode(00, 255, 1000, 144000, 1, 0, 1);
	m2.motion_control_pos_mode(00, 255, 1000, 144000, 1, 0, 1);
}
void doRightVacuumDisable() {
	relay.controlRelay(vacuum_valve_left, false);
	//m1.motion_control_pos_mode(00, 255, 500, 72000, 1, 0, 1);
	m1.motion_control_pos_mode(00, 255, 1000, 0, 1, 0, 1);
	m2.motion_control_pos_mode(00, 255, 500, 72000, 1, 0, 1);
	Sleep(4000);
	//m1.motion_control_pos_mode(00, 255, 1000, 0, 1, 0, 1);
	m2.motion_control_pos_mode(00, 255, 1000, 0, 1, 0, 1);
}

void doLeftVacuumEnable() {
	relay.controlRelay(vacuum_valve_right, true);
	m6.motion_control_pos_mode(00, 255, 1000, 144000, 1, 0, 1);
	m7.motion_control_pos_mode(00, 255, 1000, 144000, 1, 0, 1);
}
void doLeftVacuumDisable() {
	relay.controlRelay(vacuum_valve_right, false);
	m6.motion_control_pos_mode(00, 255, 500, 72000, 1, 0, 1);
	m7.motion_control_pos_mode(00, 255, 500, 72000, 1, 0, 1);
	Sleep(4000);
	m6.motion_control_pos_mode(00, 255, 1000, 0, 1, 0, 1);
	m7.motion_control_pos_mode(00, 255, 1000, 0, 1, 0, 1);
}

void doCenterVacuumEnable() {
	relay.controlRelay(vacuum_valve_center, true);
	m3.motion_control_pos_mode(00, 255, 300, 30000, 1, 0, 1);
	m4.motion_control_pos_mode(00, 255, 300, 30000, 1, 0, 1);
	m5.motion_control_pos_mode(00, 255, 300, 30000, 1, 0, 1);
}
void doCenterVacuumDisable() {
	relay.controlRelay(vacuum_valve_center, false);
	m3.motion_control_pos_mode(00, 255, 80, 15000, 1, 0, 1);
	m4.motion_control_pos_mode(00, 255, 80, 15000, 1, 0, 1);
	m5.motion_control_pos_mode(00, 255, 80, 15000, 1, 0, 1);
	Sleep(2000);
	m3.motion_control_pos_mode(00, 255, 300, 0, 1, 0, 1);
	m4.motion_control_pos_mode(00, 255, 300, 0, 1, 0, 1);
	m5.motion_control_pos_mode(00, 255, 300, 0, 1, 0, 1);
}

void doShutdown() {
	relay.controlRelay(vacuum_valve_left, true);
	relay.controlRelay(vacuum_valve_right, true);
	relay.controlRelay(vacuum_valve_center, true);
	relay.controlRelay(vacuum_motor, false);
	doWashDisable();
	std::cout << "shutdown." << std::endl;
}

void doMove_1(int rpm, double cm) {
	// --- Safety Boundary Check ---
	if (cm < -38.0 || cm > 38.0) {
		std::cerr << "[ERROR] Position " << cm << " cm is OUT OF RANGE (-38.0 to 38.0). Command aborted." << std::endl;
		return;
	}

	if (rpm < 0 || rpm > 700) {
		std::cerr << "[ERROR] Speed " << rpm << " RPM is OUT OF RANGE (0 to 700). Command aborted." << std::endl;
		return;
	}

	// --- Execute Movement ---
	drv_1.PR_move_cm(
		0,        // PR0
		1,        // mode 1 = Absolute
		rpm,      // rpm
		cm,       // cm
		100,      // acc
		100       // dec
	);

	// --- Status Log ---
	std::cout << "[LOG] M1 move initiated. Target: " << cm << " cm | Speed: " << rpm << " RPM" << std::endl;
}
void doMove_2(int rpm, double cm) {
	// --- Safety Boundary Check ---
	if (cm < -38.0 || cm > 38.0) {
		std::cerr << "[ERROR] Position " << cm << " cm is OUT OF RANGE (-38.0 to 38.0). Command aborted." << std::endl;
		return;
	}

	if (rpm < 0 || rpm > 700) {
		std::cerr << "[ERROR] Speed " << rpm << " RPM is OUT OF RANGE (0 to 700). Command aborted." << std::endl;
		return;
	}

	// --- Execute Movement ---
	drv_2.PR_move_cm(
		0,        // PR0
		1,        // mode 1 = Absolute
		rpm,      // rpm
		cm,       // cm
		100,      // acc
		100       // dec
	);

	// --- Status Log ---
	std::cout << "[LOG] M2 move initiated. Target: " << cm << " cm | Speed: " << rpm << " RPM" << std::endl;
}
void doMove_3(int rpm, double cm) {
	// --- Safety Boundary Check ---
	if (cm < -38.0 || cm > 38.0) {
		std::cerr << "[ERROR] Position " << cm << " cm is OUT OF RANGE (-38.0 to 38.0). Command aborted." << std::endl;
		return;
	}

	if (rpm < 0 || rpm > 700) {
		std::cerr << "[ERROR] Speed " << rpm << " RPM is OUT OF RANGE (0 to 700). Command aborted." << std::endl;
		return;
	}

	// --- Execute Movement ---
	drv_3.PR_move_cm(
		0,        // PR0
		1,        // mode 1 = Absolute
		rpm,      // rpm
		cm,       // cm
		100,      // acc
		100       // dec
	);

	// --- Status Log ---
	std::cout << "[LOG] M3 move initiated. Target: " << cm << " cm | Speed: " << rpm << " RPM" << std::endl;
}
void doMove_4(int rpm, double cm) {
	// --- Safety Boundary Check ---
	if (cm < -38.0 || cm > 38.0) {
		std::cerr << "[ERROR] Position " << cm << " cm is OUT OF RANGE (-38.0 to 38.0). Command aborted." << std::endl;
		return;
	}

	if (rpm < 0 || rpm > 700) {
		std::cerr << "[ERROR] Speed " << rpm << " RPM is OUT OF RANGE (0 to 700). Command aborted." << std::endl;
		return;
	}

	// --- Execute Movement ---
	drv_4.PR_move_cm(
		0,        // PR0
		1,        // mode 1 = Absolute
		rpm,      // rpm
		cm,       // cm
		100,      // acc
		100       // dec
	);

	// --- Status Log ---
	std::cout << "[LOG] M4 move initiated. Target: " << cm << " cm | Speed: " << rpm << " RPM" << std::endl;
}
void doMove_sync(int rpm, double cm) {
	// --- Safety Boundary Check ---
	if (cm < -38.0 || cm > 38.0) {
		std::cerr << "[ERROR] Position " << cm << " cm is OUT OF RANGE (-38.0 to 38.0). Command aborted." << std::endl;
		return;
	}

	if (rpm < 0 || rpm > 700) {
		std::cerr << "[ERROR] Speed " << rpm << " RPM is OUT OF RANGE (0 to 700). Command aborted." << std::endl;
		return;
	}

	drv_2.PR_move_cm_set(
		1,        // PR1
		1,        // mode 1 = Absolute
		rpm,      // rpm
		cm,       // cm
		100,      // acc
		100       // dec
	);
	drv_3.PR_move_cm_set(
		1,        // PR0
		1,        // mode 1 = Absolute
		rpm,      // rpm
		cm,       // cm
		100,      // acc
		100       // dec
	);
	drv_2.PR_trigger_sync(1);

	// --- Status Log ---
	//std::cout << "[LOG] move sync initiated. Target: " << cm << " cm | Speed: " << rpm << " RPM" << std::endl;
}
void doWashEable() {
	std::cout << "Wash Eable." << std::endl;
}
void doWashDisable() {
	std::cout << "Wash Disable." << std::endl;
}
bool askPositionOK(int x)
{
	while (true) {
		std::cout << "目前位置 x = " << x << " 是否正確？ (y/n): ";
		std::string ans;
		std::cin >> ans;

		if (ans == "y" || ans == "Y") return true;
		if (ans == "n" || ans == "N") return false;

		std::cout << "請輸入 y 或 n\n";
	}
}
int askNewX()
{
	int newX;
	std::cout << "請輸入新的 x 值: ";
	std::cin >> newX;
	return newX;
}