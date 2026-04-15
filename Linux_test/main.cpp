#include "Serial_port.h"
#include "PQW_IO_16O_RLY.h"
#include "ZDT_motor_control.h"
#include "DM2J_RS570.h"
#include "WT901BC_TTL.h"
#include "JC_100_METER.h"
#include "TCP_client.h"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <limits>
#include <sstream>
#include <atomic>

using namespace std;

// ============================================================
//  IMU test  ─  read Roll / Pitch / Yaw continuously
// ============================================================
static void test_imu() {
    string port;
    cout << "Serial port [/dev/ttyUSB0]: ";
    getline(cin, port);
    if (port.empty()) port = "/dev/ttyUSB0";

    Serial_port ser;
    if (!ser.init(port, 115200)) {
        cerr << "[ERR] Cannot open " << port << endl;
        return;
    }

    WT901BC_TTL imu;
    imu.init(&ser, false);

    // Wait for first packet
    this_thread::sleep_for(chrono::milliseconds(500));

    cout << "[IMU] Reading... press Enter to stop\n";
    cout << fixed << setprecision(2);

    // Non-blocking: run display loop, check cin in background
    atomic<bool> stop_flag(false);
    thread input_thread([&]() {
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        stop_flag = true;
    });

    while (!stop_flag.load()) {
        if (imu.read_error.load()) {
            cout << "\r[IMU] ERROR (errors=" << imu.error_count.load() << ")          ";
        } else {
            cout << "\r"
                 << "Roll="  << setw(8) << imu.x << "°  "
                 << "Pitch=" << setw(8) << imu.y << "°  "
                 << "Yaw="   << setw(8) << imu.z << "°  "
                 << "P="     << setw(8) << imu.pressure << "hPa  "
                 << "Alt="   << setw(7) << imu.altitude << "m   ";
        }
        cout.flush();
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    cout << "\n";

    if (input_thread.joinable()) input_thread.join();
    imu.stop();
}

// ============================================================
//  DM2J quick test  ─ connect slave, move 5 cm
// ============================================================
static void test_dm2j() {
    string ip;
    int slave;
    cout << "IP [192.168.1.20]: ";
    getline(cin, ip);
    if (ip.empty()) ip = "192.168.1.20";

    cout << "Slave ID [3]: ";
    string s; getline(cin, s);
    slave = s.empty() ? 3 : stoi(s);

    TCP_client cli;
    if (!cli.connectToServer(ip, 4001, false)) {
        cerr << "[ERR] Cannot connect to " << ip << ":4001\n";
        return;
    }

    DM2J_RS570 drv;
    if (drv.init(cli, slave, false)) {
        cerr << "[ERR] DM2J slave " << slave << " init fail\n";
        cli.close();
        return;
    }

    double pos = 0;
    if (!drv.read_position_cm(pos))
        cout << "[DM2J] Current pos: " << pos << " cm\n";

    cout << "Move cm [5]: ";
    getline(cin, s);
    double cm = s.empty() ? 5.0 : stod(s);

    drv.PR_move_cm(0, 1, 500, cm, 50, 100);
    cout << "[DM2J] Move sent: " << cm << " cm\n";
}

// ============================================================
//  Main menu
// ============================================================
int main() {
    cout << "=== Linux_test ===\n"
         << "  1  IMU (WT901BC) — read Roll/Pitch/Yaw\n"
         << "  2  DM2J_RS570   — quick move test\n"
         << "Select: ";

    string line;
    getline(cin, line);

    if (line == "1") {
        test_imu();
    } else if (line == "2") {
        test_dm2j();
    } else {
        cerr << "Unknown selection\n";
        return 1;
    }

    return 0;
}
