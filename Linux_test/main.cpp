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
#include <limits>
#include <sstream>
using namespace std;
#define vacuum_valve_left	3

int main() {
	Serial_port mySerial;
	WT901BC_TTL imu;
	TCP_client cli_21;
	JC_100_METER meter_1;
	PQW_IO_16O_RLY relay;
	ZDT_motor_control m1;
	DM2J_RS570 drv_1;
	if (!cli_21.connectToServer("10.0.0.42", 4001, false)) {
		std::cerr << "Failed to connect to server." << std::endl;
		system("PAUSE");
		return 1;
	}
	if (!m1.init(cli_21, 7, false)) {
		std::cerr << "Failed to init ZDT motor." << std::endl;
		cli_21.close();
		return 1;
	}
	if (!drv_1.init(cli_21, 2, false)) {
		std::cerr << "Failed to init DM2J_RS570." << std::endl;
		cli_21.close();
		return 1;
	}
	meter_1.init(cli_21, 9, false);
	relay.init(cli_21, 1, 16, false);

	cout << "Start window cleaning..." << endl;
	cout << "Press enter to continue..." << endl;
	std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

	relay.controlRelay(3, true);
	relay.controlRelay(2, true);

	m1.motion_control_pos_mode(00, 255, 1000, 144000, 1, 0, 1);
	//drv_1.PR_move_cm(0, 1, 500, 5, 50, 100);


	double original_pos;
	//讀取一開始方位
	if (drv_1.read_position_cm(original_pos)) {
		std::cout << "[INFO] Start position: " << original_pos << " cm" << std::endl;
	}

	bool is_fail = true;
	while (is_fail) {
		// 確認壓力計
		int val = meter_1.read_pressure();
		double pressure = val;
		std::cout << "\[INFO] Pressure now: " << std::fixed << std::setprecision(1) << std::setw(6) << pressure << " kPa" << std::endl;
		if (pressure >= -50) cout << "[WARN] The suction cup isn't sticking properly, try next location." << endl;
		else {
			// 成功!
			is_fail = false;
			break;
		}
		// 解真空
		relay.controlRelay(2, false);

		// 縮腳
		m1.motion_control_pos_mode(00, 255, 500, 72000, 1, 0, 1);
		sleep(2);
		m1.motion_control_pos_mode(00, 255, 1000, 0, 1, 0, 1);

		// 後退五公分
		double cm = 0;
		int rpm = 500;
		if (drv_1.read_position_cm(cm)) {
			std::cout << "[INFO] Position: " << cm << " cm" << std::endl;
		}
		else {
			std::cerr << "[ERROR] Failed to read position." << std::endl;
			continue;
		}
		cm -= 5;
		if (cm < -38.0 || cm > 38.0) {
			std::cerr << "[ERROR] Position " << cm << " cm OUT OF RANGE (-38 ~ 38)." << std::endl;
			continue;
		}
		else if (rpm < 0 || rpm > 700) {
			std::cerr << "[ERROR] Speed " << rpm << " RPM OUT OF RANGE (0 ~ 700)." << std::endl;
			continue;
		}
		else {
			drv_1.PR_move_cm(0, 1, rpm, cm, 100, 100);
			std::cout << "[INFO] Move: RPM=" << rpm << " CM=" << cm << std::endl;
		}

		// 腳下去重吸
		relay.controlRelay(2, true);
		m1.motion_control_pos_mode(00, 255, 1000, 144000, 1, 0, 1);
		//sleep(2);
	}

	double final_pos;
	//讀取一開始方位
	if (drv_1.read_position_cm(final_pos)) {
		std::cout << "[INFO] Final position: " << final_pos << " cm" << std::endl;
	}

	cout << "Total move " << final_pos - original_pos << endl;

	return 0;


	relay.init(cli_21, 1, 16, true);
	relay.controlRelay(3, false);
	relay.controlRelay(2, false);
	cout << "relay close" << endl;
	//relay.controlRelay(3, false);
	cout << "relay open" << endl;
	
	while (true) {
		int val = meter_1.read_pressure();
		double pressure = val;
		std::cout << "\rPRESSURE: " << std::fixed << std::setprecision(1) << std::setw(6) << pressure << " kPa" << std::endl;
		sleep(2);
	}

	// DM2J_RS570
	/*
	// --- Connect to RS485-TCP controller ---
	if (!cli_20.connectToServer("10.0.0.42", 4001, true)) {
		std::cerr << "Failed to connect to server." << std::endl;
		system("PAUSE");
		return 1;
	}

	// --- Init DM2J driver (shared TCP, Slave ID = 1) ---
	if (!drv_1.init(cli_20, 2, true)) {
		std::cerr << "Failed to init DM2J_RS570." << std::endl;
		cli_20.close();
		return 1;
	}
	// 兩隻腳以上
	drv_1.PR_move_set(0, 1, 500, 10* 10000, 100, 100);
	drv_1.PR_move_cm_trigger_all(0);


	std::cout << "=== DM2J_RS570 Motor Control Test ===" << std::endl;
	std::cout << "Commands:" << std::endl;
	std::cout << "  version              - Read firmware version" << std::endl;
	std::cout << "  status               - Read motor status" << std::endl;
	std::cout << "  getpos               - Read current position (cm)" << std::endl;
	std::cout << "  setzero              - Set current position as zero" << std::endl;
	std::cout << "  move <rpm> <cm>      - Absolute move (e.g. move 200 10.5)" << std::endl;
	std::cout << "  speed <rpm> <pulse>  - Speed move (e.g. speed 200 5000)" << std::endl;
	std::cout << "  speedstop            - Stop speed move" << std::endl;
	std::cout << "  jog fwd              - JOG forward" << std::endl;
	std::cout << "  jog rev              - JOG reverse" << std::endl;
	std::cout << "  jog stop             - JOG stop" << std::endl;
	std::cout << "  jogspeed <rpm>       - Set JOG speed" << std::endl;
	std::cout << "  home                 - Start homing" << std::endl;
	std::cout << "  exit                 - Exit program" << std::endl;
	std::cout << "======================================" << std::endl;

	std::string cmd;
	while (true) {
		std::cout << "> ";
		std::getline(std::cin, cmd);

		if (cmd == "exit") {
			break;
		}
		else if (cmd == "version") {
			uint16_t v1 = 0, v2 = 0;
			if (drv_1.read_version(v1, v2)) {
				std::cout << "Firmware version: " << v1 << "." << v2 << std::endl;
			}
			else {
				std::cerr << "Failed to read version." << std::endl;
			}
		}
		else if (cmd == "status") {
			uint16_t st = 0;
			if (drv_1.read_status(st)) {
				drv_1.print_status(st);
			}
			else {
				std::cerr << "Failed to read status." << std::endl;
			}
		}
		else if (cmd == "getpos") {
			double cm = 0;
			if (drv_1.read_position_cm(cm)) {
				std::cout << "Position: " << cm << " cm" << std::endl;
			}
			else {
				std::cerr << "Failed to read position." << std::endl;
			}
		}
		else if (cmd == "setzero") {
			drv_1.home_set_current_pos_zero();
			std::cout << "Zero position set." << std::endl;
		}
		else if (cmd.rfind("move", 0) == 0) {
			// move <rpm> <cm>
			std::istringstream ss(cmd);
			std::string keyword;
			int rpm = 0;
			double cm = 0;
			ss >> keyword >> rpm >> cm;

			if (ss.fail()) {
				std::cout << "Usage: move <rpm> <cm>" << std::endl;
				std::cout << "  Example: move 200 10.5" << std::endl;
			}
			else {
				if (cm < -38.0 || cm > 38.0) {
					std::cerr << "[ERROR] Position " << cm << " cm OUT OF RANGE (-38 ~ 38)." << std::endl;
				}
				else if (rpm < 0 || rpm > 700) {
					std::cerr << "[ERROR] Speed " << rpm << " RPM OUT OF RANGE (0 ~ 700)." << std::endl;
				}
				else {
					drv_1.PR_move_cm(0, 1, rpm, cm, 100, 100);
					std::cout << "Move: RPM=" << rpm << " CM=" << cm << std::endl;
				}
			}
		}
		else if (cmd.rfind("speedstop", 0) == 0) {
			drv_1.speed_move_stop();
			std::cout << "Speed move stopped." << std::endl;
		}
		else if (cmd.rfind("speed", 0) == 0) {
			// speed <rpm> <pulse>
			std::istringstream ss(cmd);
			std::string keyword;
			int rpm = 0, pulse = 0;
			ss >> keyword >> rpm >> pulse;

			if (ss.fail()) {
				std::cout << "Usage: speed <rpm> <pulse>" << std::endl;
			}
			else {
				drv_1.speed_move(0, 0, rpm, pulse);
				std::cout << "Speed move: RPM=" << rpm << " Pulse=" << pulse << std::endl;
			}
		}
		else if (cmd == "jog fwd") {
			drv_1.jog_forward();
			std::cout << "JOG forward." << std::endl;
		}
		else if (cmd == "jog rev") {
			drv_1.jog_reverse();
			std::cout << "JOG reverse." << std::endl;
		}
		else if (cmd == "jog stop") {
			drv_1.jog_stop();
			std::cout << "JOG stopped." << std::endl;
		}
		else if (cmd.rfind("jogspeed", 0) == 0) {
			std::istringstream ss(cmd);
			std::string keyword;
			int rpm = 0;
			ss >> keyword >> rpm;

			if (ss.fail()) {
				std::cout << "Usage: jogspeed <rpm>" << std::endl;
			}
			else {
				drv_1.set_jog_speed(rpm);
				std::cout << "JOG speed set to " << rpm << " RPM." << std::endl;
			}
		}
		else if (cmd == "home") {
			drv_1.home_set_mode(0x0002);
			drv_1.home_set_high_speed(200);
			drv_1.home_set_low_speed(50);
			drv_1.home_set_acc_time(50);
			drv_1.home_set_dec_time(50);
			drv_1.home_start();
			std::cout << "Homing started." << std::endl;
		}
		else if (!cmd.empty()) {
			std::cout << "Unknown command: " << cmd << std::endl;
		}
	}

	cli_20.close();
	return 0;
	*/
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
  // id 1, relay sk 幫浦 2, sk relay 3
  /*
	cli_21.connectToServer("10.0.0.42", 4001);

	PQW_IO_16O_RLY relay;
	cout << "init" << endl;
	relay.init(cli_21, 1, 16, true);
	relay.controlRelay(3, false);
	relay.controlRelay(2, false);
	cout << "relay close" << endl;
	//relay.controlRelay(3, false);
	cout << "relay open" << endl;
	relay.close();
	return 0;
	*/
	// 測試zdt 
  /*
	ZDT_motor_control m1;
	if (!cli_21.connectToServer("10.0.0.42", 4001, false)) {
		std::cerr << "Failed to connect to server." << std::endl;
		system("PAUSE");
		return 1;
	}

	// --- Init motor (shared TCP, Slave ID = 7) ---
	if (!m1.init(cli_21, 7, false)) {
		std::cerr << "Failed to init ZDT motor." << std::endl;
		cli_21.close();
		return 1;
	}
	//m1.release_stall_flag();
	// 用最慢的速度
	//m1.motion_control_pos_mode(0, 10, 10, 100, 1, 0, 1);
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
	*/
	// 測試jc100 壓力表
/*
if (!cli_21.connectToServer("10.0.0.42", 4001, false)) {
	std::cerr << "Failed to connect to server." << std::endl;
	system("PAUSE");
	return 1;
}
	JC_100_METER meter_1;

	meter_1.init(cli_21, 9, true);

	while (true) {
		int val = meter_1.read_pressure();
		double pressure = val;
		std::cout << "\rPRESSURE: " << std::fixed << std::setprecision(1) << std::setw(6) << pressure << " kPa" << std::endl;
		sleep(2);
	}
	return 0;
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