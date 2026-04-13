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

#include "WASH_ROBOT.h"

#define vacuum_motor		11
#define vacuum_valve_left	12
#define vacuum_valve_right	14
#define vacuum_valve_center	13

//TCP_client cli_20;
//TCP_client cli_21;

//PQW_IO_16O_RLY relay;
//PQW_IO_16O_RLY relay_2;

//DM2J_RS570 drv_1;
//DM2J_RS570 drv_2;
//DM2J_RS570 drv_3;
//DM2J_RS570 drv_4;

//ZDT_motor_control m1;

//JC_100_METER meter_1;
//JC_100_METER meter_2;
//JC_100_METER meter_3;
//JC_100_METER meter_4;
//JC_100_METER meter_5;
//JC_100_METER meter_6;
//JC_100_METER meter_7;


int main() {

	WashRobot robot;
	bool robot_initialized = false;

	// ============================================================
	//  ★ 單腳測試設定 — 換腳測試時修改這裡 ★
	// ============================================================
	SingleLegTestConfig testCfg;
	testCfg.tcp_ip           = "10.0.0.3";
	testCfg.tcp_port         = 4001;
	testCfg.zdt_slave        = 7;      // ZDT 無刷馬達 slave ID
	testCfg.jc100_slave      = 9;      // JC100 壓力感測器 slave ID（0 = 不使用）
	testCfg.dm2j_slave       = 2;      // DM2J 步進馬達 slave ID
	testCfg.relay_slave      = 1;      // PQW 繼電器 slave ID
	testCfg.valve_ch         = 2;      // 真空閥繼電器通道
	testCfg.vacuum_motor_ch  = 3;      // 抽真空馬達通道（0 = 不控制）
	testCfg.enable_rpm       = 1000;
	testCfg.enable_pulses    = 144000;
	testCfg.retract_rpm      = 500;
	testCfg.retract_pulses   = 72000;
	testCfg.zero_rpm         = 1000;
	testCfg.axis_rpm         = 300;
	testCfg.axis_min_cm      = -38.0;
	testCfg.axis_max_cm      =  38.0;
	testCfg.pressure_threshold = -50;
	testCfg.adjust_back_cm   = 5;

	// ============================================================

	std::string cmd;
	while (true) {
		std::cout << "> ";
		std::getline(std::cin, cmd);

		if (cmd == "exit") {
			break;
		}
		else if (cmd == "init") {
			if (robot.init()) {
				robot_initialized = true;
				std::cout << "Robot initialized." << std::endl;
			} else {
				std::cerr << "Init failed." << std::endl;
			}
		}
		else if (cmd == "doinit") {
			if (!robot_initialized) {
				std::cerr << "Run 'init' first." << std::endl;
			} else {
				robot.doInit();
			}
		}
		else if (cmd == "right enable") {
			if (!robot_initialized) { std::cerr << "Run 'init' first.\n"; }
			else robot.enableRight();
		}
		else if (cmd == "right disable") {
			if (!robot_initialized) { std::cerr << "Run 'init' first.\n"; }
			else robot.disableRight();
		}
		else if (cmd == "pressure") {
			if (!robot_initialized) { std::cerr << "Run 'init' first.\n"; }
			else {
				int val = robot.readPressure(0);
				std::cout << "Pressure: " << val << " (x0.1 kPa)" << std::endl;
			}
		}
		else if (cmd == "move 0") {
			if (!robot_initialized) { std::cerr << "Run 'init' first.\n"; }
			else robot.move(1, 300, 0.0);
		}
		else if (cmd == "move 10") {
			if (!robot_initialized) { std::cerr << "Run 'init' first.\n"; }
			else robot.move(1, 300, 10.0);
		}
		else if (cmd == "shutdown") {
			if (!robot_initialized) { std::cerr << "Run 'init' first.\n"; }
			else { robot.doShutdown(); break; }
		}
		else if (cmd == "move") {
			if (!robot_initialized) { std::cerr << "Run 'init' first.\n"; }
			else robot.moveRight();
		}
		else if (cmd.rfind("test leg wash", 0) == 0) {
			// Usage: test leg wash <cycles> <step_cm>
			int cycles = 1, step = 30;
			std::istringstream iss(cmd);
			std::string w1, w2, w3;
			iss >> w1 >> w2 >> w3 >> cycles >> step;
			std::cout << "Test single leg wash: cycles=" << cycles
			          << "  step=" << step << " cm" << std::endl;
			robot.testSingleLegWash(testCfg, cycles, step);
		}
		else if (cmd.rfind("start cleaning all with step", 0) == 0) {
			if (!robot_initialized) { std::cerr << "Run 'init' first.\n"; }
			else {
				// Usage: start cleaning all with step <cm>
				int step = 30;
				std::istringstream iss(cmd);
				std::string w;
				for (int i = 0; i < 5; i++) iss >> w;
				if (iss >> step) {
					std::cout << "Start cleaning all, step=" << step << " cm" << std::endl;
					robot.startCleaningAll(step);
				} else {
					std::cout << "Usage: start cleaning all with step <cm>" << std::endl;
				}
			}
		}
		else if (!cmd.empty()) {
			std::cout << "Unknown command: " << cmd << std::endl;
		}
	}

	return 0;

	/*

	// -------------------峻禾-----------------------
	
	

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

void startAutoWash();
	
	
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

	if (drv_1.init(cli_20, 1, false)) {
		std::cerr << "Failed to connect DM2J_RS570 controller 1." << std::endl;
		return 1;
	}
	if (drv_2.init(cli_20, 2, false)) {
		std::cerr << "Failed to connect DM2J_RS570 controller 2." << std::endl;
		return 1;
	}
	if (drv_3.init(cli_20, 3, false)) {
		std::cerr << "Failed to connect DM2J_RS570 controller 3." << std::endl;
		return 1;
	}
	if (drv_4.init(cli_20, 4, false)) {
		std::cerr << "Failed to connect DM2J_RS570 controller 4." << std::endl;
		return 1;
	}
	if (!m1.init(cli_20, 2)) {
		std::cerr << "Failed to connect ZDT_motor_control 1." << std::endl;
		return 1;
	}
	/*
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
	if (meter_3.init(cli_21, 11)) {
		std::cerr << "Failed to connect pressure meter 3." << std::endl;
		return 1;
	}
	if (meter_4.init(cli_21, 12)) {
		std::cerr << "Failed to connect pressure meter 4." << std::endl;
		return 1;
	}
	if (meter_5.init(cli_21, 13)) {
		std::cerr << "Failed to connect pressure meter 5." << std::endl;
		return 1;
	}
	if (meter_6.init(cli_21, 14)) {
		std::cerr << "Failed to connect pressure meter 6." << std::endl;
		return 1;
	}
	if (meter_7.init(cli_21, 15)) {
		std::cerr << "Failed to connect pressure meter 7." << std::endl;
		return 1;
	}

	if (!relay.init(cli_21, 1)) {
		std::cerr << "Failed to connect relay controller." << std::endl;
		return 1;
	}
	/*
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
			std::cout << "���e��m: " << a << " cm" << std::endl;
		}
		else if (cmd == "move1 set zero") {
			drv_1.home_set_current_pos_zero();
		}
		else if (cmd.rfind("move1", 0) == 0) {  // �P�_�O�_�H "move" �}�Y
			std::stringstream ss(cmd);
			std::string keyword;
			int rpm = 0, cm = 0;

			ss >> keyword;   // Ū�X "move"
			ss >> rpm >> cm; // Ū�X��ӼƦr

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
			std::cout << "���e��m: " << a << " cm" << std::endl;
		}
		else if (cmd == "move2 set zero") {
			drv_2.home_set_current_pos_zero();
		}
		else if (cmd.rfind("move2", 0) == 0) {  // �P�_�O�_�H "move" �}�Y
			std::stringstream ss(cmd);
			std::string keyword;
			int rpm = 0, cm = 0;

			ss >> keyword;   // Ū�X "move"
			ss >> rpm >> cm; // Ū�X��ӼƦr

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
			std::cout << "���e��m: " << a << " cm" << std::endl;
		}
		else if (cmd == "move3 set zero") {
			drv_3.home_set_current_pos_zero();
		}
		else if (cmd.rfind("move3", 0) == 0) {  // �P�_�O�_�H "move" �}�Y
			std::stringstream ss(cmd);
			std::string keyword;
			int rpm = 0, cm = 0;

			ss >> keyword;   // Ū�X "move"
			ss >> rpm >> cm; // Ū�X��ӼƦr

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
			std::cout << "���e��m: " << a << " cm" << std::endl;
		}
		else if (cmd == "move4 set zero") {
			drv_4.home_set_current_pos_zero();
		}
		else if (cmd.rfind("move4", 0) == 0) {  // �P�_�O�_�H "move" �}�Y
			std::stringstream ss(cmd);
			std::string keyword;
			int rpm = 0, cm = 0;

			ss >> keyword;   // Ū�X "move"
			ss >> rpm >> cm; // Ū�X��ӼƦr

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
			std::cout << "\r���O: " << std::fixed << std::setprecision(1) << std::setw(6) << pressure << " kPa" << std::endl;
			std::cout << "\r���O: " << std::fixed << std::setprecision(1) << std::setw(6) << pressure2 << " kPa" << std::endl;

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
		// �B�z "set process up X"
		// ---------------------------------------------
		else if (cmd.rfind("downsync", 0) == 0) {

			int x = -1;  // �Ψ��x�s X
			std::istringstream iss(cmd);

			std::string t1;
			iss >> t1 >> x;
			// t1 = set
			// t2 = process
			// t3 = up
			// x  = �Ʀr

			if (x < 0) {
				std::cout << "Usage: set process up <number>" << std::endl;
				break;
			}

			std::cout << "Running: process upsync with X = " << x << std::endl;

			x = x * -1;
			// ------------------------
			//   �}�l����y�{
			// ------------------------
			//Right move
			doLeftVacuumDisable();
			Sleep(200);
			doRightVacuumDisable();
			waitEnter();

			while (true) {
				doMove_sync(700, x);
				if (askPositionOK(x)) {
					std::cout << "��m�T�{����\n";
					break;
				}

				x = askNewX()*-1;   // ���s���o�ϥΪ̿�J
			}
			waitEnter();
			doLeftVacuumEnable();
			doRightVacuumEnable();
			while (true) {
				if (askPositionOK(x)) {
					std::cout << "��m�T�{����\n";
					break;
				}
				doLeftVacuumDisable();
				Sleep(200);
				doRightVacuumDisable();
				x = askNewX()*-1;   // ���s���o�ϥΪ̿�J
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

			int x = -1;  // �Ψ��x�s X
			std::istringstream iss(cmd);

			std::string t1;
			iss >> t1 >> x;
			// t1 = set
			// t2 = process
			// t3 = up
			// x  = �Ʀr

			if (x < 0) {
				std::cout << "Usage: set process up <number>" << std::endl;
				break;
			}

			std::cout << "Running: process up with X = " << x << std::endl;

			x = x * -1;
			// ------------------------
			//   �}�l����y�{
			// ------------------------
			//Right move
			doRightVacuumDisable();
			waitEnter();

			while (true) {
				doMove_2(700, x);

				if (askPositionOK(x)) {
					std::cout << "��m�T�{����\n";
					break;
				}

				x = askNewX()*-1;   // ���s���o�ϥΪ̿�J
			}
			waitEnter();
			doRightVacuumEnable();
			while (true) {
				if (askPositionOK(x)) {
					std::cout << "��m�T�{����\n";
					break;
				}
				doRightVacuumDisable();
				x = askNewX()*-1;   // ���s���o�ϥΪ̿�J
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
					std::cout << "��m�T�{����\n";
					break;
				}
				x = askNewX()*-1;   // ���s���o�ϥΪ̿�J
			}
			waitEnter();
			doLeftVacuumEnable();

			while (true) {
				if (askPositionOK(x)) {
					std::cout << "��m�T�{����\n";
					break;
				}
				doLeftVacuumDisable();
				x = askNewX()*-1;   // ���s���o�ϥΪ̿�J
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

			int x = -1;  // �Ψ��x�s X
			std::istringstream iss(cmd);

			std::string t1;
			iss >> t1 >> x;
			// t1 = set
			// t2 = process
			// t3 = up
			// x  = �Ʀr

			if (x < 0) {
				std::cout << "Usage: set process up <number>" << std::endl;
				break;
			}

			std::cout << "Running: process down with X = " << x << std::endl;


			// ------------------------
			//   �}�l����y�{
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
					std::cout << "��m�T�{����\n";
					break;
				}

				x = askNewX()*-1;   // ���s���o�ϥΪ̿�J
			}
			waitEnter();
			doRightVacuumEnable();
			while (true) {
				if (askPositionOK(x)) {
					std::cout << "��m�T�{����\n";
					break;
				}
				doRightVacuumDisable();
				x = askNewX()*-1;   // ���s���o�ϥΪ̿�J
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
					std::cout << "��m�T�{����\n";
					break;
				}
				x = askNewX()*-1;   // ���s���o�ϥΪ̿�J
			}
			waitEnter();
			doLeftVacuumEnable();

			while (true) {
				if (askPositionOK(x)) {
					std::cout << "��m�T�{����\n";
					break;
				}
				doLeftVacuumDisable();
				x = askNewX()*-1;   // ���s���o�ϥΪ̿�J
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
	*/
}

/*
void waitEnter() {
	std::cout << "Press Enter to continue...";
	std::cin.get();
}

// ---------------------- �Ƶ{���� ----------------------
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
		std::cout << "�ثe��m x = " << x << " �O�_���T�H (y/n): ";
		std::string ans;
		std::cin >> ans;

		if (ans == "y" || ans == "Y") return true;
		if (ans == "n" || ans == "N") return false;

		std::cout << "�п�J y �� n\n";
	}
}
int askNewX()
{
	int newX;
	std::cout << "�п�J�s�� x ��: ";
	std::cin >> newX;
	return newX;
}
*/