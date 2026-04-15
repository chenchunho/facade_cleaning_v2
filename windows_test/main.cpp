// ============================================================================
// windows_test — WashRobot single-device test tool (Windows)
//
// 啟動時選裝置類型，輸入 IP / slave ID，進入該裝置的互動測試選單。
// IMU 另外輸入 COM port。
// ============================================================================

#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <conio.h>

#include "TCP_client.h"
#include "Serial_port.h"
#include "DM2J_RS570.h"
#include "ZDT_motor_control.h"
#include "JC_100_METER.h"
#include "PQW_IO_16O_RLY.h"
#include "WT901BC_TTL.h"

static void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// =========== DM2J ===========

static void test_dm2j() {
    std::string ip; int port = 4001, slave = 1;
    std::cout << "IP   (預設 192.168.1.20): "; std::getline(std::cin, ip);
    if (ip.empty()) ip = "192.168.1.20";
    std::cout << "Port (預設 4001): "; std::string tmp; std::getline(std::cin, tmp);
    if (!tmp.empty()) port = std::stoi(tmp);
    std::cout << "Slave ID (1~5): "; std::getline(std::cin, tmp);
    if (!tmp.empty()) slave = std::stoi(tmp);

    TCP_client cli;
    if (!cli.connectToServer(ip.c_str(), port)) {
        std::cerr << "[ERR] 連線失敗 " << ip << ":" << port << "\n"; return;
    }
    DM2J_RS570 drv;
    if (drv.init(cli, slave, false)) {
        std::cerr << "[ERR] DM2J slave " << slave << " init 失敗\n"; return;
    }
    std::cout << "[OK] DM2J slave " << slave << " @ " << ip << "\n\n"
              << "  status           讀取狀態\n"
              << "  getpos           讀取位置 (cm)\n"
              << "  setzero          設定當前位置為零\n"
              << "  move <rpm> <cm>  絕對位置移動\n"
              << "  jog fwd/rev/stop JOG 控制\n"
              << "  jogspeed <rpm>   設定 JOG 速度\n"
              << "  home             執行 Homing\n"
              << "  exit             離開\n\n";

    std::string cmd;
    while (true) {
        std::cout << "DM2J[" << slave << "]> "; std::getline(std::cin, cmd);
        if (cmd == "exit") break;
        else if (cmd == "status") {
            uint32_t st = 0;
            if (!drv.read_status(st)) drv.print_status(st);
            else std::cerr << "[ERR] 讀取失敗\n";
        }
        else if (cmd == "getpos") {
            double cm = 0;
            if (!drv.read_position_cm(cm))
                std::cout << "位置: " << std::fixed << std::setprecision(2) << cm << " cm\n";
            else std::cerr << "[ERR] 讀取失敗\n";
        }
        else if (cmd == "setzero") {
            drv.home_set_current_pos_zero();
            std::cout << "零點已設定\n";
        }
        else if (cmd.rfind("move", 0) == 0) {
            std::istringstream ss(cmd); std::string kw; int rpm = 0; double cm = 0;
            ss >> kw >> rpm >> cm;
            if (ss.fail()) { std::cout << "用法: move <rpm> <cm>\n"; continue; }
            drv.PR_move_cm(0, 1, rpm, cm, 100, 100);
            std::cout << "移動: rpm=" << rpm << " cm=" << cm << "\n";
        }
        else if (cmd == "jog fwd")  { drv.jog_forward(); std::cout << "JOG 正轉\n"; }
        else if (cmd == "jog rev")  { drv.jog_reverse(); std::cout << "JOG 反轉\n"; }
        else if (cmd == "jog stop") { drv.jog_stop();    std::cout << "JOG 停止\n"; }
        else if (cmd.rfind("jogspeed", 0) == 0) {
            std::istringstream ss(cmd); std::string kw; int rpm = 0;
            ss >> kw >> rpm;
            if (ss.fail()) { std::cout << "用法: jogspeed <rpm>\n"; continue; }
            drv.set_jog_speed(rpm);
            std::cout << "JOG 速度: " << rpm << " RPM\n";
        }
        else if (cmd == "home") {
            drv.home_set_mode(0x0002);
            drv.home_set_high_speed(200);
            drv.home_set_low_speed(50);
            drv.home_set_acc_time(50);
            drv.home_set_dec_time(50);
            drv.home_start();
            std::cout << "Homing 啟動\n";
        }
        else if (!cmd.empty()) std::cout << "未知指令: " << cmd << "\n";
    }
    cli.close();
}

// =========== ZDT ===========

static void test_zdt() {
    std::string ip; int port = 4001, slave = 1;
    std::cout << "IP   (預設 192.168.1.21): "; std::getline(std::cin, ip);
    if (ip.empty()) ip = "192.168.1.21";
    std::cout << "Port (預設 4001): "; std::string tmp; std::getline(std::cin, tmp);
    if (!tmp.empty()) port = std::stoi(tmp);
    std::cout << "Slave ID (1~9): "; std::getline(std::cin, tmp);
    if (!tmp.empty()) slave = std::stoi(tmp);

    TCP_client cli;
    if (!cli.connectToServer(ip.c_str(), port)) {
        std::cerr << "[ERR] 連線失敗 " << ip << ":" << port << "\n"; return;
    }
    ZDT_motor_control drv;
    if (drv.init(cli, slave, false)) {
        std::cerr << "[ERR] ZDT slave " << slave << " init 失敗\n"; return;
    }
    std::cout << "[OK] ZDT slave " << slave << " @ " << ip << "\n\n"
              << "  enable             啟用馬達\n"
              << "  disable            停用馬達\n"
              << "  zero               設定當前位置為零\n"
              << "  status             讀取狀態\n"
              << "  stop               緊急停止\n"
              << "  pos <rpm> <pulse>  位置模式移動\n"
              << "  speed <rpm>        速度模式\n"
              << "  home               回零 (pulse=0)\n"
              << "  stall              解除堵轉旗標\n"
              << "  reset              重置馬達\n"
              << "  exit               離開\n\n";

    std::string cmd;
    while (true) {
        std::cout << "ZDT[" << slave << "]> "; std::getline(std::cin, cmd);
        if (cmd == "exit") break;
        else if (cmd == "enable")  { drv.motion_control_driver_EN(1); std::cout << "馬達啟用\n"; }
        else if (cmd == "disable") { drv.motion_control_driver_EN(0); std::cout << "馬達停用\n"; }
        else if (cmd == "zero")    { drv.set_zero(); std::cout << "零點已設定\n"; }
        else if (cmd == "stop")    { drv.emergency_stop(false); std::cout << "緊急停止\n"; }
        else if (cmd == "status") {
            if (!drv.get_system_status()) {
                auto& s = drv.status;
                std::cout << "  Bus Voltage:  " << s.bus_voltage  << " mV\n"
                          << "  Bus Current:  " << s.bus_current  << " mA\n"
                          << "  Temperature:  " << s.temperature  << " C\n"
                          << "  Real Pos:     " << s.real_pos     << " deg\n"
                          << "  Real Speed:   " << s.real_speed   << " RPM\n"
                          << "  Target Pos:   " << s.target_pos   << " deg\n"
                          << "  Enabled:      " << (s.is_enabled  ? "Yes" : "No") << "\n"
                          << "  Pos Reached:  " << (s.pos_reached ? "Yes" : "No") << "\n"
                          << "  Stall:        " << (s.stall_flag  ? "Yes" : "No") << "\n";
            } else std::cerr << "[ERR] 讀取失敗\n";
        }
        else if (cmd.rfind("pos", 0) == 0) {
            std::istringstream ss(cmd); std::string kw; int rpm = 0, pulse = 0;
            ss >> kw >> rpm >> pulse;
            if (ss.fail()) { std::cout << "用法: pos <rpm> <pulse>\n"; continue; }
            drv.motion_control_pos_mode(0, 255, rpm, pulse, 1, 0, 1);
            std::cout << "位置移動: rpm=" << rpm << " pulse=" << pulse << "\n";
        }
        else if (cmd.rfind("speed", 0) == 0) {
            std::istringstream ss(cmd); std::string kw; int rpm = 0;
            ss >> kw >> rpm;
            if (ss.fail()) { std::cout << "用法: speed <rpm>\n"; continue; }
            drv.motion_control_speed_mode(0, 255, rpm, 0, 1);
            std::cout << "速度模式: " << rpm << " RPM\n";
        }
        else if (cmd == "home")  { drv.motion_control_pos_mode(0, 255, 500, 0, 1, 0, 1); std::cout << "回零中...\n"; }
        else if (cmd == "stall") { drv.release_stall_flag(); std::cout << "堵轉旗標解除\n"; }
        else if (cmd == "reset") { drv.reset_motor(); std::cout << "馬達重置\n"; }
        else if (!cmd.empty()) std::cout << "未知指令: " << cmd << "\n";
    }
    cli.close();
}

// =========== JC-100 ===========

static void test_jc100() {
    std::string ip; int port = 4001, slave = 1;
    std::cout << "IP   (預設 192.168.1.22): "; std::getline(std::cin, ip);
    if (ip.empty()) ip = "192.168.1.22";
    std::cout << "Port (預設 4001): "; std::string tmp; std::getline(std::cin, tmp);
    if (!tmp.empty()) port = std::stoi(tmp);
    std::cout << "Slave ID (1~9): "; std::getline(std::cin, tmp);
    if (!tmp.empty()) slave = std::stoi(tmp);

    TCP_client cli;
    if (!cli.connectToServer(ip.c_str(), port)) {
        std::cerr << "[ERR] 連線失敗 " << ip << ":" << port << "\n"; return;
    }
    JC_100_METER meter;
    if (meter.init(cli, slave, false)) {
        std::cerr << "[ERR] JC-100 slave " << slave << " init 失敗\n"; return;
    }
    std::cout << "[OK] JC-100 slave " << slave << " @ " << ip << "\n"
              << "持續讀取壓力，按任意鍵停止...\n\n";

    while (!_kbhit()) {
        int val = meter.read_pressure();
        double kpa = val / 10.0;
        std::cout << "\r壓力: " << std::fixed << std::setprecision(1) << std::setw(7) << kpa << " kPa"
                  << (meter.error_flag == 1 ? "  [通訊中斷!]  " : "  [正常]       ") << std::flush;
        sleep_ms(100);
    }
    _getch();
    std::cout << "\n";
    cli.close();
}

// =========== PQW Relay ===========

static void test_pqw() {
    std::string ip; int port = 4001, slave = 12, ch_count = 8;
    std::cout << "IP       (預設 192.168.1.22): "; std::getline(std::cin, ip);
    if (ip.empty()) ip = "192.168.1.22";
    std::cout << "Port     (預設 4001): "; std::string tmp; std::getline(std::cin, tmp);
    if (!tmp.empty()) port = std::stoi(tmp);
    std::cout << "Slave ID (預設 12): "; std::getline(std::cin, tmp);
    if (!tmp.empty()) slave = std::stoi(tmp);
    std::cout << "CH 數量  (預設 8): "; std::getline(std::cin, tmp);
    if (!tmp.empty()) ch_count = std::stoi(tmp);

    TCP_client cli;
    if (!cli.connectToServer(ip.c_str(), port)) {
        std::cerr << "[ERR] 連線失敗 " << ip << ":" << port << "\n"; return;
    }
    PQW_IO_16O_RLY relay;
    if (relay.init(cli, slave, ch_count, false)) {
        std::cerr << "[ERR] PQW slave " << slave << " init 失敗\n"; return;
    }
    std::cout << "[OK] PQW slave " << slave << " (" << ch_count << "CH) @ " << ip << "\n\n"
              << "  on <ch>    開啟繼電器\n"
              << "  off <ch>   關閉繼電器\n"
              << "  all_off    全部關閉\n"
              << "  exit       離開\n\n";

    std::string cmd;
    while (true) {
        std::cout << "PQW[" << slave << "]> "; std::getline(std::cin, cmd);
        if (cmd == "exit") break;
        else if (cmd.rfind("on", 0) == 0) {
            std::istringstream ss(cmd); std::string kw; int ch = 0;
            ss >> kw >> ch;
            if (ss.fail() || ch < 1 || ch > ch_count) { std::cout << "用法: on <ch>\n"; continue; }
            if (relay.controlRelay(ch, true)) std::cerr << "[ERR] 操作失敗\n";
            else std::cout << "CH" << ch << " ON\n";
        }
        else if (cmd.rfind("off", 0) == 0) {
            std::istringstream ss(cmd); std::string kw; int ch = 0;
            ss >> kw >> ch;
            if (ss.fail() || ch < 1 || ch > ch_count) { std::cout << "用法: off <ch>\n"; continue; }
            if (relay.controlRelay(ch, false)) std::cerr << "[ERR] 操作失敗\n";
            else std::cout << "CH" << ch << " OFF\n";
        }
        else if (cmd == "all_off") {
            bool err = false;
            for (int i = 1; i <= ch_count; ++i) err |= relay.controlRelay(i, false);
            std::cout << (err ? "[ERR] 部分失敗\n" : "全部 OFF\n");
        }
        else if (!cmd.empty()) std::cout << "未知指令: " << cmd << "\n";
    }
    cli.close();
}

// =========== IMU ===========

static void test_imu() {
    std::string com = "COM3";
    std::cout << "COM port (預設 COM3): "; std::getline(std::cin, com);
    if (com.empty()) com = "COM3";

    Serial_port serial;
    if (serial.init(com, 115200)) {
        std::cerr << "[ERR] serial open 失敗 (" << com << ")\n"; return;
    }
    WT901BC_TTL imu;
    imu.init(&serial, false);
    sleep_ms(500);
    std::cout << "[OK] IMU @ " << com << "\n"
              << "持續讀取姿態，按任意鍵停止...\n\n";

    while (!_kbhit()) {
        std::cout << "\r"
                  << "Roll:"  << std::fixed << std::setprecision(2) << std::setw(7) << imu.x << " "
                  << "Pitch:" << std::fixed << std::setprecision(2) << std::setw(7) << imu.y << " "
                  << "Yaw:"   << std::fixed << std::setprecision(2) << std::setw(7) << imu.z << " | "
                  << "Err:" << (imu.read_error.load() ? "YES" : "NO ")
                  << "(" << std::setfill('0') << std::setw(4) << imu.error_count.load()
                  << std::setfill(' ') << ")"
                  << "    " << std::flush;
        sleep_ms(50);
    }
    _getch();
    imu.stop();
    std::cout << "\n";
}

// =========== Main ===========

int main() {
    std::cout << "=== WashRobot Device Test ===\n\n"
              << "  1. DM2J   (滑桿 / 輪子)\n"
              << "  2. ZDT    (推桿)\n"
              << "  3. JC-100 (壓力感測)\n"
              << "  4. PQW    (繼電器)\n"
              << "  5. IMU    (WT901BC)\n"
              << "  0. 離開\n\n";

    while (true) {
        std::cout << "選擇裝置> ";
        std::string sel; std::getline(std::cin, sel);
        if      (sel == "1") test_dm2j();
        else if (sel == "2") test_zdt();
        else if (sel == "3") test_jc100();
        else if (sel == "4") test_pqw();
        else if (sel == "5") test_imu();
        else if (sel == "0") break;
        else if (!sel.empty()) std::cout << "請輸入 0~5\n";

        std::cout << "\n=== WashRobot Device Test ===\n\n"
                  << "  1. DM2J   (滑桿 / 輪子)\n"
                  << "  2. ZDT    (推桿)\n"
                  << "  3. JC-100 (壓力感測)\n"
                  << "  4. PQW    (繼電器)\n"
                  << "  5. IMU    (WT901BC)\n"
                  << "  0. 離開\n\n";
    }
    return 0;
}
