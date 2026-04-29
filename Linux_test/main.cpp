// ============================================================
//  Linux_test — single-device interactive test harness
//
//  One test per device. Each test prompts for its own IP / slave / params,
//  runs, cleans up (disables motors / closes relays), then returns to the
//  main menu. Type 'q' at the main menu to exit.
// ============================================================

#include "Serial_port.h"
#include "PQW_IO_16O_RLY.h"
#include "ZDT_motor_control.h"
#include "DM2J_RS570.h"
#include "WT901BC_TTL.h"
#include "JC_100_METER.h"
#include "XKC_Y25_RS485.h"
#include "SD76_length_meters.h"
#include "ZS_DIO_R_RLY.h"
#include "TCP_client.h"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <limits>
#include <sstream>
#include <atomic>
#include <vector>
#include <set>
#include <functional>
#include <cmath>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <conio.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#endif

using namespace std;

// Pre-probe TCP reachability with short timeout. TCP_client::connectToServer
// uses a blocking connect() with no timeout (Linux default ~130s SYN retries),
// so we probe first to fail fast on unreachable hosts.
static bool quick_tcp_probe(const string& ip, int port, int timeout_ms = 2000) {
#ifdef _WIN32
    SOCKET sk = socket(AF_INET, SOCK_STREAM, 0);
    if (sk == INVALID_SOCKET) return false;
    u_long mode = 1; ioctlsocket(sk, FIONBIO, &mode);
#else
    int sk = socket(AF_INET, SOCK_STREAM, 0);
    if (sk < 0) return false;
    int flags = fcntl(sk, F_GETFL, 0);
    fcntl(sk, F_SETFL, flags | O_NONBLOCK);
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    int res = connect(sk, (sockaddr*)&addr, sizeof(addr));
    bool ok = false;

    bool pending = false;
#ifdef _WIN32
    if (res == 0) ok = true;
    else pending = (WSAGetLastError() == WSAEWOULDBLOCK);
#else
    if (res == 0) ok = true;
    else pending = (errno == EINPROGRESS);
#endif

    if (pending) {
        fd_set wf; FD_ZERO(&wf); FD_SET(sk, &wf);
        timeval tv{ timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        if (select((int)sk + 1, nullptr, &wf, nullptr, &tv) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(sk, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
            ok = (err == 0);
        }
    }

#ifdef _WIN32
    closesocket(sk);
#else
    close(sk);
#endif
    return ok;
}

// SMC LEYG25 pusher constants (matches WASH_ROBOT.h PUSHER_* values)
static constexpr int PUSHER_EXTEND_PULSE  = 144000;    // full stroke (legacy, used by option 3/6/7)
static constexpr int PUSHER_RETRACT_PULSE = 0;
static constexpr int PUSHER_RPM           = 1000;
static constexpr int PUSHER_ACC           = 255;
// Small back-off pulse count for vacuum-failure re-grip (slight retract before re-extend)
static constexpr int PUSHER_BACKOFF_PULSE = 120000;

// Per-group partial extend targets used by options 8 / 11 (step-no-rail tests).
// Values set by measurement on the actual robot (2026-04-23):
//   feet pushers reach ~8cm at  23000 pulses (2026-04-28 從 7 cm 加長)
//   body pushers reach ~10cm at 30000 pulses
static constexpr int PUSHER_EXTEND_FEET_PULSE = 23000;   // feet: ~8 cm (was 20000=~7cm before 2026-04-28)
static constexpr int PUSHER_EXTEND_BODY_PULSE = 30000;   // body: ~10 cm

// Slave mappings (matches WASH_ROBOT architecture)
//   ZDT feet:   1, 2, 5, 6   (left-front, left-back, right-front, right-back)
//   ZDT body:   3, 4, 7, 8
//   ZDT center: 9
//   DM2J rail:  1 (left foot), 3 (right foot)
//   JC-100:     slaves 1..9 match ZDT 1..9 (vacuum sensor per cup)
//   PQW slave:  12 (relay, 8CH)
//     CH1 pump / CH2 feet valve / CH3 body valve / CH4 center valve
static constexpr int DM2J_LEFT_RAIL  = 1;
static constexpr int DM2J_RIGHT_RAIL = 3;
static constexpr int DM2J_RPM        = 200;
static constexpr int DM2J_ACC        = 50;
static constexpr int DM2J_DEC        = 100;
static constexpr int PQW_SLAVE       = 12;
static constexpr int PQW_CH_PUMP         = 1;
static constexpr int PQW_CH_VALVE_FEET   = 2;
static constexpr int PQW_CH_VALVE_BODY   = 3;
static constexpr int PQW_CH_VALVE_CENTER = 4;
static constexpr int VACUUM_SETTLE_MS    = 2000;
static constexpr int VACUUM_RELEASE_MS   = 300;


//=========== 1. IMU (WT901BC) ===========
static void test_imu() {
    cout << "\n--- IMU (WT901BC) ---\n";
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
    imu.init(&ser, true);
    this_thread::sleep_for(chrono::milliseconds(500));

    cout << "[IMU] Reading... press Enter to stop\n" << fixed << setprecision(2);

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


//=========== 2. DM2J step motor ===========
static void test_dm2j() {
    cout << "\n--- DM2J_RS570 step motor ---\n";
    string ip;
    cout << "Gateway IP [192.168.1.20]: ";
    getline(cin, ip);
    if (ip.empty()) ip = "192.168.1.20";

    cout << "Slave ID [3]: ";
    string s; getline(cin, s);
    int slave = s.empty() ? 3 : stoi(s);

    cout << "Mode [a=PR abs / r=PR rel / s=PR abs (set→200ms→trigger) / j=JOG 2s] [a]: ";
    string mode_s; getline(cin, mode_s);
    char mode_c = mode_s.empty() ? 'a' : mode_s[0];

    double cm = 0;
    if (mode_c == 'a' || mode_c == 'r' || mode_c == 's') {
        cout << "Move cm [5]: ";
        getline(cin, s);
        cm = s.empty() ? 5.0 : stod(s);
    }

    if (!quick_tcp_probe(ip, 4001)) {
        cerr << "[ERR] " << ip << ":4001 unreachable (2s timeout)\n"; return;
    }
    TCP_client cli;
    if (!cli.connectToServer(ip, 4001, false)) {
        cerr << "[ERR] Cannot connect to " << ip << ":4001\n"; return;
    }

    DM2J_RS570 drv;
    if (drv.init(cli, slave, true)) {
        cerr << "[ERR] DM2J slave " << slave << " init fail\n";
        cli.close(); return;
    }

    double pos = 0;
    if (!drv.read_position_cm(pos))
        cout << "  current position: " << pos << " cm\n";

    // Pre-steps: reset_alarm + motor_enable
    cout << "  → reset_alarm + motor_enable (pre-step)\n";
    if (drv.reset_alarm())
        cerr << "  [WARN] reset_alarm write fail\n";
    this_thread::sleep_for(chrono::milliseconds(50));
    if (drv.motor_enable())
        cerr << "  [WARN] motor_enable write fail\n";
    this_thread::sleep_for(chrono::milliseconds(100));

    if (mode_c == 'j') {
        // JOG test — same subsystem as vendor tool's "forward turn" button.
        cout << "  → JOG forward 2 seconds @ 200 rpm\n";
        drv.set_jog_speed(200);
        drv.jog_forward();
        this_thread::sleep_for(chrono::milliseconds(2000));
        drv.jog_stop();
        cout << "  [OK] JOG stop sent\n";
    } else if (mode_c == 'r') {
        cout << "  → move " << cm << " cm (relative, mode=2)\n";
        drv.PR_move_cm(0, 2, 500, cm, 50, 100);
        cout << "  [OK] move command sent\n";
    } else if (mode_c == 's') {
        // Split pattern: set PR block → 200ms → trigger → wait for RUN → wait for done.
        // Tracks status transitions (idle → RUN → done) so we don't false-positive
        // on stale default status bits.
        double pos_pre = 0; drv.read_position_cm(pos_pre);
        cout << "  → move " << cm << " cm (absolute, split: set → 200ms → trigger)\n";
        cout << "  position (pre-trigger): " << pos_pre << " cm\n";
        drv.PR_move_cm_set(0, 1, 500, cm, 50, 100);
        this_thread::sleep_for(chrono::milliseconds(200));
        drv.PR_trigger(0);

        // Wait for RUN bit to APPEAR first (avoid reading stale pre-trigger status).
        bool saw_run = false;
        for (int e = 0; e < 1000; e += 50) {
            uint32_t st = 0;
            if (drv.read_status(st)) break;
            if (st & 0x0004) { saw_run = true; cout << "  [RUN started] at " << e << "ms\n"; break; }
            this_thread::sleep_for(chrono::milliseconds(50));
        }
        if (!saw_run) {
            cout << "  [WARN] RUN bit never appeared — drive didn't start motion!\n";
        }
        // Now wait for RUN to clear + CMD_DONE+PATH_DONE
        for (int e = 0; e < 10000; e += 100) {
            uint32_t st = 0;
            if (drv.read_status(st)) break;
            if (!(st & 0x0004) && (st & 0x0010) && (st & 0x0020)) {
                cout << "  [OK] completed at " << e << "ms (post-RUN)\n"; break;
            }
            this_thread::sleep_for(chrono::milliseconds(100));
        }
        double pos_post = 0; drv.read_position_cm(pos_post);
        cout << "  position (post): " << pos_post << " cm (Δ=" << (pos_post - pos_pre) << ")\n";
    } else {
        cout << "  → move " << cm << " cm (absolute, mode=1)\n";
        drv.PR_move_cm(0, 1, 500, cm, 50, 100);
        cout << "  [OK] move command sent\n";
    }

    // Position verification after any motion
    this_thread::sleep_for(chrono::milliseconds(200));
    double pos_after = 0;
    if (!drv.read_position_cm(pos_after))
        cout << "  position after: " << pos_after << " cm (before was " << pos
             << ", Δ=" << (pos_after - pos) << ")\n";

    cli.close();
}


//=========== 3. ZDT SMC pusher ===========
static void test_zdt() {
    cout << "\n--- ZDT SMC pusher ---\n";
    string ip;
    cout << "Gateway IP [192.168.1.21]: ";
    getline(cin, ip);
    if (ip.empty()) ip = "192.168.1.21";

    cout << "Slave ID [1]: ";
    string s; getline(cin, s);
    int slave = s.empty() ? 1 : stoi(s);

    cout << "Target pulse [144000=full extend / 0=full retract / 200mm=144000 pulses]: ";
    string p; getline(cin, p);
    int target_pulse = p.empty() ? 144000 : stoi(p);

    if (!quick_tcp_probe(ip, 4001)) {
        cerr << "[ERR] " << ip << ":4001 unreachable (2s timeout)\n"; return;
    }
    TCP_client cli;
    if (!cli.connectToServer(ip, 4001, false)) {
        cerr << "[ERR] Cannot connect to " << ip << ":4001\n"; return;
    }

    ZDT_motor_control drv;
    if (drv.init(cli, slave, true)) {
        cerr << "[ERR] ZDT slave " << slave << " init fail\n";
        cli.close(); return;
    }

    cout << "  → release_stall_flag\n";
    drv.release_stall_flag();
    this_thread::sleep_for(chrono::milliseconds(100));

    cout << "  → enable\n";
    if (drv.motion_control_driver_EN(true)) {
        cerr << "  [ERR] enable failed\n"; cli.close(); return;
    }
    this_thread::sleep_for(chrono::milliseconds(200));

    cout << "  → move to " << target_pulse << " pulses @ " << PUSHER_RPM << " rpm\n";
    if (!drv.motion_control_pos_mode(0, PUSHER_ACC, PUSHER_RPM, target_pulse, 1, 0, 1)) {
        cerr << "  [ERR] move command send failed\n";
    } else {
        // Poll status with auto stall recovery. wait_until_pos_reached() in user_lib
        // does not differentiate stall from generic timeout, so we roll our own here.
        constexpr int MAX_STALL_RETRIES     = 3;
        constexpr int POLL_INTERVAL_MS      = 200;
        constexpr int PER_ATTEMPT_TIMEOUT_MS = 10000;

        int  retries = 0;
        bool reached = false;
        auto t0 = chrono::steady_clock::now();

        while (true) {
            this_thread::sleep_for(chrono::milliseconds(POLL_INTERVAL_MS));

            if (drv.get_system_status()) {
                cerr << "  [ERR] get_system_status failed\n";
                break;
            }

            if (drv.status.pos_reached) {
                reached = true;
                break;
            }

            if (drv.status.stall_flag) {
                cout << "  [STALL] detected at real_pos=" << drv.status.real_pos
                     << "°, attempt " << (retries + 1) << "/" << MAX_STALL_RETRIES << "\n";

                if (retries >= MAX_STALL_RETRIES) {
                    cerr << "  [ABORT] stall persists after " << MAX_STALL_RETRIES
                         << " retries\n";
                    break;
                }

                // Recovery: stop motion -> clear stall -> re-enable -> re-send pos cmd
                drv.emergency_stop(true);
                this_thread::sleep_for(chrono::milliseconds(200));
                drv.release_stall_flag();
                this_thread::sleep_for(chrono::milliseconds(100));
                if (drv.motion_control_driver_EN(true)) {
                    cerr << "  [ERR] re-enable failed during recovery\n";
                    break;
                }
                this_thread::sleep_for(chrono::milliseconds(200));
                if (drv.motion_control_pos_mode(0, PUSHER_ACC, PUSHER_RPM,
                                                target_pulse, 1, 0, 1)) {
                    cerr << "  [ERR] re-send pos_mode failed during recovery\n";
                    break;
                }

                retries++;
                t0 = chrono::steady_clock::now();
                continue;
            }

            auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now() - t0).count();
            if (elapsed_ms > PER_ATTEMPT_TIMEOUT_MS) {
                cerr << "  [WARN] attempt timeout (" << elapsed_ms
                     << " ms) without stall or pos_reached\n";
                break;
            }
        }

        if (reached) {
            cout << "  [OK] reached target (stall retries=" << retries << ")\n";
        }
    }

    drv.get_system_status();
    cout << "  final: enabled=" << drv.status.is_enabled
         << " pos_reached=" << drv.status.pos_reached
         << " stall=" << drv.status.stall_flag
         << " real_pos=" << drv.status.real_pos << "°\n";

    //cout << "  → disable (cleanup)\n";
    //drv.motion_control_driver_EN(false);
    cli.close();
}


//=========== 4. JC-100 vacuum pressure sensor ===========
static void test_jc100() {
    cout << "\n--- JC-100 vacuum pressure ---\n";
    string ip;
    cout << "Gateway IP [192.168.1.22]: ";
    getline(cin, ip);
    if (ip.empty()) ip = "192.168.1.22";

    cout << "Slave ID [1]: ";
    string s; getline(cin, s);
    int slave = s.empty() ? 1 : stoi(s);

    if (!quick_tcp_probe(ip, 4001)) {
        cerr << "[ERR] " << ip << ":4001 unreachable (2s timeout)\n"; return;
    }
    TCP_client cli;
    if (!cli.connectToServer(ip, 4001, false)) {
        cerr << "[ERR] Cannot connect to " << ip << ":4001\n"; return;
    }

    JC_100_METER drv;
    if (drv.init(cli, slave, true)) {
        cerr << "[ERR] JC-100 slave " << slave << " init fail\n";
        cli.close(); return;
    }

    cout << "[JC-100:" << slave << "] Reading... press Enter to stop\n";
    atomic<bool> stop_flag(false);
    thread input_thread([&]() {
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        stop_flag = true;
    });

    while (!stop_flag.load()) {
        int p = drv.read_pressure();
        if (drv.error_flag)
            cout << "\r[JC-100:" << slave << "] READ ERROR                              ";
        else
            cout << "\r[JC-100:" << slave << "] pressure = " << setw(6) << p
                 << " (0.1 kPa) = " << setw(7) << setprecision(2) << fixed << (p / 10.0) << " kPa    ";
        cout.flush();
        this_thread::sleep_for(chrono::milliseconds(200));
    }
    cout << "\n";
    if (input_thread.joinable()) input_thread.join();
    cli.close();
}


//=========== 5. PQW 8CH relay ===========
static void test_pqw() {
    cout << "\n--- PQW 8CH relay ---\n";
    string ip;
    cout << "Gateway IP [192.168.1.22]: ";
    getline(cin, ip);
    if (ip.empty()) ip = "192.168.1.22";

    cout << "Slave ID [12]: ";
    string s; getline(cin, s);
    int slave = s.empty() ? 12 : stoi(s);

    if (!quick_tcp_probe(ip, 4001)) {
        cerr << "[ERR] " << ip << ":4001 unreachable (2s timeout)\n"; return;
    }
    TCP_client cli;
    if (!cli.connectToServer(ip, 4001, false)) {
        cerr << "[ERR] Cannot connect to " << ip << ":4001\n"; return;
    }

    PQW_IO_16O_RLY drv;
    if (drv.init(cli, slave, 8, true)) {
        cerr << "[ERR] PQW slave " << slave << " init fail\n";
        cli.close(); return;
    }

    bool state[9] = {false};   // index 1..8

    cout << "\nPQW " << slave << " (8CH) — washrobot channel map:\n"
         << "  CH1: dp0105 真空泵浦 (shared, 9 cups)\n"
         << "  CH2: VT307 腳組電磁閥\n"
         << "  CH3: VT307 身體組電磁閥\n"
         << "  CH4: VT307 中心電磁閥\n"
         << "  CH5: 刷洗滾筒馬達\n"
         << "  CH6: 水箱泵浦\n"
         << "  CH7: 水箱進水球閥\n"
         << "  CH8: 保留\n"
         << "\n"
         << "Note: Modbus echo check is unreliable on some PQW firmware.\n"
         << "      ALWAYS verify via physical LED / click / device response.\n\n";

    while (true) {
        cout << "Input [on N / off N / a=all ON / o=all OFF / s=state / q=back to menu]: ";
        string in; getline(cin, in);
        if (in == "q" || in.empty()) break;

        // controlAll only checks echo length (>= 8 bytes), not content. A garbled
        // 8-byte response still reports success. So we label it [SENT] not [OK] —
        // physical LED check is the only reliable verification.
        if (in == "a") {
            bool err = drv.controlAll(true);
            cout << "  [SENT] all ON — " << (err ? "echo too short" : "echo-len OK, content NOT verified")
                 << " → check LEDs physically\n";
            for (int i = 1; i <= 8; ++i) state[i] = true;
            continue;
        }
        if (in == "o") {
            bool err = drv.controlAll(false);
            cout << "  [SENT] all OFF — " << (err ? "echo too short" : "echo-len OK, content NOT verified")
                 << " → check LEDs physically\n";
            for (int i = 1; i <= 8; ++i) state[i] = false;
            continue;
        }
        if (in == "s") {
            cout << "  state: ";
            for (int i = 1; i <= 8; ++i) cout << "CH" << i << "=" << (state[i] ? "ON " : "OFF ");
            cout << "\n"; continue;
        }

        // Parse "on N" / "off N"
        istringstream iss(in);
        string verb; int ch = 0;
        iss >> verb >> ch;
        bool want_on;
        if      (verb == "on")  want_on = true;
        else if (verb == "off") want_on = false;
        else { cout << "  [!] usage: on N | off N | a | o | s | q\n"; continue; }
        if (ch < 1 || ch > 8) { cout << "  [!] channel out of range 1..8\n"; continue; }

        // controlRelay does write + readback verification. If physical write succeeded
        // but readback returns garbled Modbus frame, it reports error falsely.
        // Print a WARN so the user can physically verify (LED / click).
        if (drv.controlRelay(ch, want_on))
            cerr << "  [WARN] CH" << ch << " " << (want_on ? "ON" : "OFF")
                 << " readback mismatch — check LED physically (write may still have succeeded)\n";
        else
            cout << "  [OK] CH" << ch << " " << (want_on ? "ON" : "OFF") << "\n";
        state[ch] = want_on;   // optimistic: trust write regardless of readback
    }

    cout << "[CLEANUP] turning all channels OFF\n";
    drv.controlAll(false);
    cli.close();
}


//=========== 19. ZDT positions (read all 9 ZDT pushers) ===========
//
// Connects to .21 (ZDT bus), inits 9 slaves, reads each one's MotorSystemStatus
// and prints a table:
//   slave / pos(deg) / pos(cm est) / enabled / pos_reached / group
//
// cm estimation per group (calibration from WASH_ROBOT.h pusher constants):
//   feet (1-4): 20000 pulses ≈ 7 cm,  default 51200 ppr → ~0.04978 cm/deg
//   body (5-8): 30000 pulses ≈ 10 cm, default 51200 ppr → ~0.04741 cm/deg
//   center (9):                            same ratio as body
// Constants: cm = pos_deg × cm_per_deg.  If the user changes ZDT microstepping
// from default (51200 ppr) the cm estimate is wrong but pos(deg) stays correct.
static void test_zdt_positions() {
    cout << "\n--- ZDT positions (read all 9 pushers) ---\n";
    string ip;
    cout << "Gateway IP [192.168.1.21]: ";
    getline(cin, ip);
    if (ip.empty()) ip = "192.168.1.21";

    if (!quick_tcp_probe(ip, 4001)) {
        cerr << "[ERR] " << ip << ":4001 unreachable (2s timeout)\n"; return;
    }
    TCP_client cli;
    if (!cli.connectToServer(ip, 4001, false)) {
        cerr << "[ERR] Cannot connect to " << ip << ":4001\n"; return;
    }

    ZDT_motor_control drv[10];   // index 1..9 in use, [0] unused
    bool init_ok[10] = {false};
    for (int s = 1; s <= 9; ++s) {
        if (!drv[s].init(cli, s, false)) init_ok[s] = true;
        else cerr << "[WARN] ZDT slave " << s << " init fail (will skip)\n";
    }

    // Group label + cm/deg conversion factor per group
    auto group_label = [](int s) -> const char* {
        if (s == 1) return "R-foot1";
        if (s == 2) return "R-foot2";
        if (s == 3) return "L-foot1";
        if (s == 4) return "L-foot2";
        if (s == 5) return "R-body1";
        if (s == 6) return "L-body1";
        if (s == 7) return "R-body2";
        if (s == 8) return "L-body2";
        if (s == 9) return "Center ";
        return "?";
    };
    auto cm_per_deg = [](int s) -> double {
        if (s >= 1 && s <= 4) return 7.0  / (20000.0 * 360.0 / 51200.0);   // ~0.04978
        if (s >= 5 && s <= 9) return 10.0 / (30000.0 * 360.0 / 51200.0);   // ~0.04741
        return 0.0;
    };

    cout << "\n[Enter] = re-read, [q] = back to menu\n";
    while (true) {
        cout << "\nslave  pos(deg)   cm(est)  enabled  reached  stall  group\n";
        cout << "-----  --------  --------  -------  -------  -----  -------\n";
        for (int s = 1; s <= 9; ++s) {
            if (!init_ok[s]) {
                cout << "  " << s << "    [init fail]\n";
                continue;
            }
            if (drv[s].get_system_status()) {
                cout << "  " << s << "    [read fail]\n";
                continue;
            }
            const auto& st = drv[s].status;
            double cm = st.real_pos * cm_per_deg(s);
            cout << "  " << s
                 << "   " << setw(8) << fixed << setprecision(2) << st.real_pos
                 << "  " << setw(8) << fixed << setprecision(2) << cm
                 << "      " << (st.is_enabled    ? "ON " : "off")
                 << "       " << (st.pos_reached  ? "ON " : "off")
                 << "    " << (st.stall_flag      ? "Y" : "-")
                 << "    " << group_label(s) << "\n";
        }

        cout << "\n[Enter=re-read / q=back]: ";
        string in; getline(cin, in);
        if (in == "q" || in == "Q") break;
    }

    cli.close();
}


//=========== 6. ZDT group (multi-pusher at once) ===========
static void test_zdt_group() {
    cout << "\n--- ZDT multi-pusher group ---\n";

    string ip;
    cout << "Gateway IP [192.168.1.21]: ";
    getline(cin, ip);
    if (ip.empty()) ip = "192.168.1.21";

    cout << "Skip slaves (comma, empty=none) [9]: ";
    string skip_in; getline(cin, skip_in);
    if (skip_in.empty()) skip_in = "9";

    set<int> skip_set;
    {
        stringstream ss(skip_in);
        string tok;
        while (getline(ss, tok, ',')) {
            try { skip_set.insert(stoi(tok)); } catch (...) {}
        }
    }

    cout << "Target pulse [144000=extend / 0=retract]: ";
    string p; getline(cin, p);
    int target_pulse = p.empty() ? 144000 : stoi(p);

    vector<int> actives;
    for (int i = 1; i <= 9; ++i)
        if (!skip_set.count(i)) actives.push_back(i);
    if (actives.empty()) { cerr << "[ERR] no active slaves\n"; return; }

    cout << "[INFO] controlling slaves:";
    for (int s : actives) cout << " " << s;
    cout << " → " << target_pulse << " pulses\n";

    if (!quick_tcp_probe(ip, 4001)) {
        cerr << "[ERR] " << ip << ":4001 unreachable (2s timeout)\n"; return;
    }
    TCP_client cli;
    if (!cli.connectToServer(ip, 4001, false)) {
        cerr << "[ERR] Cannot connect to " << ip << ":4001\n"; return;
    }

    ZDT_motor_control drvs[10];   // index 1..9 in use; [0] unused
    for (int s : actives) {
        if (drvs[s].init(cli, s, false)) {
            cerr << "[ERR] ZDT slave " << s << " init bind fail\n";
            cli.close(); return;
        }
    }

    // Per-slave state (index 1..9)
    bool   done[10]    = {};
    bool   aborted[10] = {};
    int    retries[10] = {};
    string last_err[10];

    // sync=1 → ZDT queues the command; nothing moves until trigger_sync_move() broadcast.
    // sync=0 → execute immediately (used for stall recovery resend, where the rest of the
    //          group has already moved and we don't want to wait for another broadcast).
    //
    // Use the _nowait variant: motion_control_pos_mode() internally blocks on
    // wait_until_pos_reached() which never returns under sync=1 (motor hasn't started yet),
    // so every slave would hit a 10s timeout and be marked aborted. _nowait just sends the
    // frame and returns — we poll status ourselves in Phase 2.
    auto send_pos = [&](int s, int sync_flag) -> bool {
        return drvs[s].motion_control_pos_mode_nowait(0, PUSHER_ACC, PUSHER_RPM, target_pulse, 1, sync_flag, 1);
    };

    // Phase 1: release + enable + QUEUE pos cmd (sync=1, deferred) for each active slave
    for (int s : actives) {
        drvs[s].release_stall_flag();
        this_thread::sleep_for(chrono::milliseconds(50));
        if (drvs[s].motion_control_driver_EN(true)) {
            cerr << "  [ERR] slave " << s << " enable failed\n";
            aborted[s] = true; last_err[s] = "enable_fail"; continue;
        }
        this_thread::sleep_for(chrono::milliseconds(100));
        if (send_pos(s, 1)) {
            cerr << "  [ERR] slave " << s << " pos_mode queue failed\n";
            aborted[s] = true; last_err[s] = "send_fail"; continue;
        }
        cout << "  → slave " << s << " cmd queued\n";
    }

    // Phase 1b: broadcast sync trigger — all queued ZDT execute at the same instant.
    // trigger_sync_move() sends to broadcast slave 0x00; any driver instance works.
    int trig = -1;
    for (int s : actives) if (!aborted[s]) { trig = s; break; }
    if (trig < 0) {
        cerr << "[ABORT] no slave successfully queued; nothing to trigger\n";
    } else {
        cout << "[TRIGGER] broadcast sync move → all queued slaves execute now\n";
        // Modbus broadcast has no reply by spec; driver returns true on empty echo
        // which is expected success here — ignore the return value.
        drvs[trig].trigger_sync_move();
    }

    // Phase 2: unified poll loop with per-slave stall auto-recovery.
    //
    // Done criteria per slave (any):
    //   (a) pos_reached bit set
    //   (b) stall_flag set → stall recovery below
    //   (c) velocity fallback: |real_speed| ≤ STOP_RPM for STOP_CONSECUTIVE polls
    //       (some ZDT firmware never set pos_reached even after physical stop)
    constexpr int    MAX_STALL_RETRIES    = 3;
    constexpr int    POLL_INTERVAL_MS     = 200;
    constexpr int    OVERALL_TIMEOUT_MS   = 15000;
    constexpr double STOP_RPM             = 5.0;
    constexpr int    STOP_CONSECUTIVE     = 3;     // ~600ms stationary
    constexpr int    MIN_WAIT_MS          = 500;   // ignore velocity while still accelerating

    std::vector<int> stop_cnt(10, 0);    // index 1..9 used

    auto t0 = chrono::steady_clock::now();
    while (true) {
        this_thread::sleep_for(chrono::milliseconds(POLL_INTERVAL_MS));
        auto elapsed = chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - t0).count();

        bool all_settled = true;
        for (int s : actives) {
            if (done[s] || aborted[s]) continue;
            all_settled = false;

            if (drvs[s].get_system_status()) continue;   // comms hiccup; retry next poll

            if (drvs[s].status.pos_reached) {
                done[s] = true;
                cout << "  [OK] slave " << s << " reached (pos_reached bit)\n";
                continue;
            }

            // Velocity fallback — motor physically stopped but firmware didn't assert pos_reached
            double rpm = drvs[s].status.real_speed;
            if (rpm < 0) rpm = -rpm;
            if (elapsed >= MIN_WAIT_MS && rpm <= STOP_RPM) {
                if (++stop_cnt[s] >= STOP_CONSECUTIVE) {
                    done[s] = true;
                    cout << "  [OK] slave " << s << " stopped (rpm~0) at real_pos="
                         << drvs[s].status.real_pos << "° — marking done\n";
                    continue;
                }
            } else {
                stop_cnt[s] = 0;
            }

            if (drvs[s].status.stall_flag) {
                cout << "  [STALL] slave " << s
                     << " at real_pos=" << drvs[s].status.real_pos
                     << "° attempt " << (retries[s] + 1) << "/" << MAX_STALL_RETRIES << "\n";

                if (retries[s] >= MAX_STALL_RETRIES) {
                    aborted[s] = true; last_err[s] = "stall_persistent";
                    cerr << "  [ABORT] slave " << s << " stall persists\n"; continue;
                }

                drvs[s].emergency_stop(true);
                this_thread::sleep_for(chrono::milliseconds(100));
                drvs[s].release_stall_flag();
                this_thread::sleep_for(chrono::milliseconds(50));
                if (drvs[s].motion_control_driver_EN(true)) {
                    aborted[s] = true; last_err[s] = "reenable_fail";
                    cerr << "  [ABORT] slave " << s << " re-enable failed\n"; continue;
                }
                this_thread::sleep_for(chrono::milliseconds(100));
                // Recovery resend: sync=0 (immediate). Other slaves may already be moving;
                // we don't want this one to wait for another broadcast trigger.
                if (send_pos(s, 0)) {
                    aborted[s] = true; last_err[s] = "resend_fail";
                    cerr << "  [ABORT] slave " << s << " resend failed\n"; continue;
                }
                retries[s]++;
                t0 = chrono::steady_clock::now();   // reset overall timeout on recovery
            }
        }

        if (all_settled) break;

        auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - t0).count();
        if (elapsed_ms > OVERALL_TIMEOUT_MS) {
            cerr << "  [WARN] overall timeout (" << elapsed_ms
                 << " ms); remaining slaves marked stuck\n";
            for (int s : actives)
                if (!done[s] && !aborted[s]) { aborted[s] = true; last_err[s] = "timeout"; }
            break;
        }
    }

    // Summary
    int ok_cnt = 0, ab_cnt = 0;
    cout << "\n[SUMMARY]\n";
    for (int s : actives) {
        drvs[s].get_system_status();
        cout << "  slave " << s << ": ";
        if (done[s]) {
            cout << "OK (retries=" << retries[s] << ")\n"; ok_cnt++;
        } else {
            cout << "ABORT [" << last_err[s]
                 << "] real_pos=" << drvs[s].status.real_pos
                 << "° stall=" << drvs[s].status.stall_flag << "\n"; ab_cnt++;
        }
    }
    cout << "total reached=" << ok_cnt << " aborted=" << ab_cnt << "\n";

    cout << "[CLEANUP] disabling all\n";
    //for (int s : actives) drvs[s].motion_control_driver_EN(false);
    cli.close();
}


//=========== 7. Full step sequence (8 pushers staged 7/10cm + rail + vacuum + retry) ===========
//
// Replicates WASH_ROBOT do_step_down_ minus the state machine and IMU logic.
// Prompts for IPs + step distance + num steps + vacuum threshold + retry count.
//
// Assumes operator has put the robot on the wall with ALL vacuum already engaged
// (feet + body + center all suctioned). We don't auto-attach the initial position —
// that's the operator's job.
//
// Per step:
//   Phase A (Feet):
//     release feet valve → retract feet pushers → rail +step_cm →
//     extend feet pushers → engage feet valve → settle → verify JC-100 1,2,3,4
//     → retry grip (rail back-off -5cm + re-extend) if any below threshold
//   Phase B (Body — center skipped):
//     release body valve → retract body pushers → rail -step_cm →
//     extend body pushers → engage body valve → settle →
//     verify JC-100 5,6,7,8 → retry (rail back-off +5cm) if needed
//   Note: ZDT 9 (center pusher) + PQW CH4 (center valve) intentionally unused.
//
// Rail moves are RELATIVE (mode=0) so one full step returns rail to starting pos.
// ============================================================

// Queue + sync-trigger + poll-until-done for a ZDT group. Returns true on error.
//
// Done criteria per slave (any of):
//   (a) pos_reached bit set          — canonical, but some ZDT firmware/tolerance combos
//                                      leave this 0 even after motor physically stops
//   (b) stall_flag set                — stall detected; treat as done-with-warning
//   (c) |real_speed| <= STOP_RPM for  — velocity-based fallback: motor actually stopped,
//       STOP_CONSECUTIVE polls,         even if pos_reached bit never flipped
//       after MIN_WAIT_MS warm-up
static bool zdt_group_move_sync(ZDT_motor_control* drvs, const std::vector<int>& slaves,
                                 int target_pulse, int timeout_ms = 6000) {
    if (slaves.empty()) return false;

    // Per-call retry helper. ZDT/RS485 frame misalignment causes sporadic "enable fail"
    // or "pos queue fail" — retry with back-off before giving up on a slave.
    auto retry_cmd = [](int max_try, int backoff_ms, const std::function<bool()>& fn) -> bool {
        for (int t = 0; t < max_try; ++t) {
            if (!fn()) return false;                            // success
            this_thread::sleep_for(chrono::milliseconds(backoff_ms));
        }
        return true;                                            // all attempts failed
    };

    // Queue each slave. Skip (not abort) if a slave fails all retries — other slaves
    // still run. Failed ones just sit idle; the poll loop below will eventually
    // time them out and log which ones didn't make it.
    std::vector<int> queued;
    std::vector<int> skipped;
    for (int s : slaves) {
        drvs[s].release_stall_flag();
        this_thread::sleep_for(chrono::milliseconds(50));

        if (retry_cmd(3, 120, [&]() { return drvs[s].motion_control_driver_EN(true); })) {
            cerr << "    [ERR] ZDT slave " << s << " enable fail after 3 tries — skipping\n";
            skipped.push_back(s);
            continue;
        }
        this_thread::sleep_for(chrono::milliseconds(80));

        if (retry_cmd(3, 120, [&]() {
            return drvs[s].motion_control_pos_mode_nowait(
                0, PUSHER_ACC, PUSHER_RPM, target_pulse, 1, 1, 1);
        })) {
            cerr << "    [ERR] ZDT slave " << s << " pos queue fail after 3 tries — skipping\n";
            skipped.push_back(s);
            continue;
        }
        queued.push_back(s);
    }

    if (queued.empty()) {
        cerr << "    [ABORT] no slaves successfully queued\n";
        return true;
    }

    // Broadcast trigger — every queued slave executes simultaneously.
    // Note: Modbus broadcast (slave 0) has NO reply per spec. trigger_sync_move()
    // returns true (=error per convention) on empty reply, which is actually expected
    // success here. We ignore the return value; real error detection is the poll
    // loop timeout below.
    drvs[queued[0]].trigger_sync_move();

    // Settle-detection tuning. Three independent "stopped" checks (any one triggers
    // done after STOP_CONSECUTIVE consecutive positives, once past MIN_WAIT_MS warm-up):
    //   (c1) velocity ≤ STOP_RPM   — software-reported speed near zero
    //   (c2) |Δreal_pos| ≤ POS_DELTA_DEG  — position didn't change (robust even if firmware
    //                                       reports noisy/residual speed)
    constexpr double STOP_RPM          = 20.0;  // tolerant envelope (was 5)
    constexpr double POS_DELTA_DEG     = 0.15;  // <0.15° change = stationary
    constexpr int    STOP_CONSECUTIVE  = 3;     // 3 × POLL_MS = ~450ms stationary
    constexpr int    POLL_MS           = 150;
    constexpr int    MIN_WAIT_MS       = 500;

    std::vector<bool>   done(queued.size(), false);
    std::vector<int>    stop_cnt(queued.size(), 0);
    std::vector<double> prev_pos(queued.size(), 1e9);   // sentinel for first poll
    auto t0 = chrono::steady_clock::now();

    while (true) {
        this_thread::sleep_for(chrono::milliseconds(POLL_MS));
        auto elapsed = chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - t0).count();

        bool all = true;
        for (size_t i = 0; i < queued.size(); ++i) {
            if (done[i]) continue;
            int s = queued[i];
            if (drvs[s].get_system_status()) { all = false; continue; }

            // (a) canonical pos_reached
            if (drvs[s].status.pos_reached) { done[i] = true; continue; }

            // (b) stall
            if (drvs[s].status.stall_flag) {
                cerr << "    [STALL] slave " << s
                     << " at real_pos=" << drvs[s].status.real_pos << "°\n";
                done[i] = true; continue;
            }

            // (c) motion-stopped fallback — speed-based OR position-delta-based.
            //     Either signal suffices; position-delta is more reliable because
            //     some ZDT firmwares report noisy/nonzero speed at rest.
            double rpm = drvs[s].status.real_speed;
            if (rpm < 0) rpm = -rpm;
            double pos_now   = drvs[s].status.real_pos;
            double pos_delta = (prev_pos[i] > 1e8) ? 999.0 : pos_now - prev_pos[i];
            if (pos_delta < 0) pos_delta = -pos_delta;
            prev_pos[i] = pos_now;

            bool stopped_by_speed = (rpm <= STOP_RPM);
            bool stopped_by_pos   = (pos_delta <= POS_DELTA_DEG);

            if (elapsed >= MIN_WAIT_MS && (stopped_by_speed || stopped_by_pos)) {
                if (++stop_cnt[i] >= STOP_CONSECUTIVE) {
                    cout << "    [INFO] slave " << s << " settled (rpm="
                         << drvs[s].status.real_speed << " Δpos=" << pos_delta
                         << "°) — done at real_pos=" << pos_now << "°\n";
                    done[i] = true; continue;
                }
            } else {
                stop_cnt[i] = 0;
            }
            all = false;
        }
        if (all) {
            if (!skipped.empty()) {
                cerr << "    [NOTE] " << skipped.size() << " slave(s) skipped at queue stage:";
                for (int s : skipped) cerr << " " << s;
                cerr << " (returning error for caller awareness)\n";
                return true;
            }
            return false;
        }
        if (elapsed > timeout_ms) {
            cerr << "    [WARN] group timeout (" << elapsed << "ms); stuck slaves:";
            for (size_t i = 0; i < queued.size(); ++i)
                if (!done[i]) cerr << " " << queued[i];
            if (!skipped.empty()) {
                cerr << " | skipped:";
                for (int s : skipped) cerr << " " << s;
            }
            cerr << "\n";
            return true;
        }
    }
}

// Read pressure on each JC-100 slave; print each, return true if ALL <= threshold.
static bool vacuum_verify(JC_100_METER* jcs, const std::vector<int>& slaves, int threshold_01kpa) {
    bool ok = true;
    cout << "    pressures (0.1 kPa): ";
    for (int s : slaves) {
        int p = jcs[s].read_pressure();
        cout << "[" << s << "]=" << p << (jcs[s].error_flag ? "?" : (p <= threshold_01kpa ? "✓" : "✗")) << " ";
        if (jcs[s].error_flag || p > threshold_01kpa) ok = false;
    }
    cout << (ok ? "  → OK\n" : "  → FAIL\n");
    return ok;
}

// Read pressure on each JC-100 slave and just print — no threshold check, no retry.
// Used by diagnostic tests where operator inspects pressures visually.
static void vacuum_report(JC_100_METER* jcs, const std::vector<int>& slaves) {
    cout << "    pressures (0.1 kPa): ";
    for (int s : slaves) {
        int p = jcs[s].read_pressure();
        cout << "[" << s << "]=" << p << (jcs[s].error_flag ? "?" : "") << " ";
    }
    cout << "\n";
}

// Two-stage extend: go to half target → wait 1s → go to full target.
// Used by options 8/11 to reduce contact shock when cups seal on wall.
//
// IMPORTANT: stage 2 runs regardless of stage 1 result. A stage-1 "timeout"
// (zdt_group_move_sync returning true) is common when pos_reached bit isn't
// set by ZDT firmware — we must not short-circuit and leave the motor at
// half-extent. We combine both stage statuses in the return value.
static bool zdt_group_extend_staged(ZDT_motor_control* drvs, const std::vector<int>& slaves,
                                     int full_target_pulse, int stage_delay_ms = 1000) {
    int half = full_target_pulse / 2;
    cout << "    stage 1: → " << half << " pulses (half)\n";
    bool err1 = zdt_group_move_sync(drvs, slaves, half);
    cout << "    (pause " << stage_delay_ms << "ms)\n";
    this_thread::sleep_for(chrono::milliseconds(stage_delay_ms));
    cout << "    stage 2: → " << full_target_pulse << " pulses (full)\n";
    bool err2 = zdt_group_move_sync(drvs, slaves, full_target_pulse);
    return err1 || err2;
}

// DM2J sync pair move: queue left+right, broadcast trigger, wait for both done.
// Uses PR_move_cm_set() + PR_move_cm_trigger_all() which waits for the calling slave;
// briefly polls the other slave status afterward as a safety check.
// Poll left + right until both positions are stable near their respective targets.
// DM2J status register bit layout observed on this firmware does not match the
// summary doc (bits appear in upper 16 bits of driver's 32-bit combined status,
// and even then decoding is uncertain). Use position-based stability detection
// instead — same pattern as ZDT per project_zdt_firmware_quirks memory.
// Returns true on error/timeout, false when both reached and stable.
static bool dm2j_pair_poll_done(DM2J_RS570& left, DM2J_RS570& right,
                                 double left_target, double right_target) {
    const int timeout_ms = 15000;
    const int poll_interval_ms = 150;
    const int STABLE_THRESHOLD = 3;          // ~450ms of stability before declaring done
    const double STABLE_DELTA_CM = 0.01;     // < 0.1 mm drift between polls counts as stopped
    const double POSITION_TOLERANCE_CM = 0.5; // ±5 mm of target counts as arrived

    double last_left = 1e9, last_right = 1e9;   // sentinel: first poll never counts as stable
    int stable_left = 0, stable_right = 0;
    bool left_done = false, right_done = false;
    int elapsed = 0;

    while (elapsed < timeout_ms && !(left_done && right_done)) {
        if (!left_done) {
            double p = 0;
            if (left.read_position_cm(p)) { cerr << "    [WARN] left position read fail\n"; return true; }
            if (std::fabs(p - last_left) < STABLE_DELTA_CM) stable_left++; else stable_left = 0;
            last_left = p;
            if (stable_left >= STABLE_THRESHOLD && std::fabs(p - left_target) < POSITION_TOLERANCE_CM)
                left_done = true;
        }
        if (!right_done) {
            double p = 0;
            if (right.read_position_cm(p)) { cerr << "    [WARN] right position read fail\n"; return true; }
            if (std::fabs(p - last_right) < STABLE_DELTA_CM) stable_right++; else stable_right = 0;
            last_right = p;
            if (stable_right >= STABLE_THRESHOLD && std::fabs(p - right_target) < POSITION_TOLERANCE_CM)
                right_done = true;
        }
        if (left_done && right_done) return false;
        this_thread::sleep_for(chrono::milliseconds(poll_interval_ms));
        elapsed += poll_interval_ms;
    }
    cerr << "    [WARN] rail poll timed out (left_done=" << left_done
         << " right_done=" << right_done << " last_left=" << last_left
         << " last_right=" << last_right << ")\n";
    return true;
}

// Move both rails to a shared absolute target (cm). Used by cleanup to return
// rails to 0 without knowing current position.
static bool dm2j_pair_rail_move_abs(DM2J_RS570& left, DM2J_RS570& right, int pr_num, double target_cm) {
    cout << "    DM2J " << DM2J_LEFT_RAIL << "+" << DM2J_RIGHT_RAIL
         << " → absolute " << fixed << setprecision(3) << target_cm << " cm\n";
    left.PR_move_cm_set (pr_num, 1, DM2J_RPM, target_cm, DM2J_ACC, DM2J_DEC);
    right.PR_move_cm_set(pr_num, 1, DM2J_RPM, target_cm, DM2J_ACC, DM2J_DEC);
    // Individual triggers (not broadcast) — see dm2j_pair_rail_move comment.
    left.PR_trigger(pr_num);
    right.PR_trigger(pr_num);
    return dm2j_pair_poll_done(left, right, target_cm, target_cm);
}

static bool dm2j_pair_rail_move(DM2J_RS570& left, DM2J_RS570& right, int pr_num, double cm) {
    cout << "    DM2J " << DM2J_LEFT_RAIL << "+" << DM2J_RIGHT_RAIL
         << " → " << (cm >= 0 ? "+" : "") << cm << " cm (via absolute mode)\n";

    // mode=0 (summary claims "relative") is observed as no-op on this firmware —
    // drive acks PR block + trigger but never enters motion. mode=1 (absolute) is
    // verified working via menu 2. So read current position, compute abs target.
    double left_pos = 0, right_pos = 0;
    if (left.read_position_cm(left_pos)) {
        cerr << "    [WARN] left rail position read fail\n"; return true;
    }
    if (right.read_position_cm(right_pos)) {
        cerr << "    [WARN] right rail position read fail\n"; return true;
    }
    double left_target  = left_pos  + cm;
    double right_target = right_pos + cm;
    cout << "    left  " << fixed << setprecision(3) << left_pos  << " → " << left_target  << " cm\n"
         << "    right " << fixed << setprecision(3) << right_pos << " → " << right_target << " cm\n";

    left.PR_move_cm_set (pr_num, 1, DM2J_RPM, left_target,  DM2J_ACC, DM2J_DEC);
    right.PR_move_cm_set(pr_num, 1, DM2J_RPM, right_target, DM2J_ACC, DM2J_DEC);

    // Individual triggers (NOT broadcast). Broadcast to slave 0 would also fire
    // slaves 2/4/5 (wheels + upper slide) with stale PR data — dangerous. Serialize
    // over same TCP socket → left/right start ~5-20ms apart (~0.4-1mm position
    // offset at DM2J_RPM=200, 10mm pitch). Acceptable if rail coupling has compliance.
    left.PR_trigger(pr_num);
    right.PR_trigger(pr_num);

    return dm2j_pair_poll_done(left, right, left_target, right_target);
}


// ---------------------------------------------------------------------------
// SYNC variant: broadcast trigger so both rails start in the SAME Modbus frame
// (zero TCP serialization skew — true hardware sync, <<1ms).
//
// PRECONDITION: bystander DM2J slaves on the bus (wheels 2,4 / arm 5) must have
// their PR<pr_num> pre-set to rpm=0 (safe no-op) BEFORE calling this. Broadcast
// triggers ALL slaves — bystanders with rpm=0 PR will execute but not move.
// Call dm2j_set_safe_pr(bystanders, pr_num) once at caller init to enforce.
// ---------------------------------------------------------------------------
static bool dm2j_pair_rail_move_sync(DM2J_RS570& left, DM2J_RS570& right,
                                      int pr_num, double cm) {
    cout << "    DM2J " << DM2J_LEFT_RAIL << "+" << DM2J_RIGHT_RAIL
         << " → " << (cm >= 0 ? "+" : "") << cm << " cm (broadcast sync)\n";

    double left_pos = 0, right_pos = 0;
    if (left.read_position_cm(left_pos)) {
        cerr << "    [WARN] left rail position read fail\n"; return true;
    }
    if (right.read_position_cm(right_pos)) {
        cerr << "    [WARN] right rail position read fail\n"; return true;
    }
    double left_target  = left_pos  + cm;
    double right_target = right_pos + cm;
    cout << "    left  " << fixed << setprecision(3) << left_pos  << " → " << left_target  << " cm\n"
         << "    right " << fixed << setprecision(3) << right_pos << " → " << right_target << " cm\n";

    // Queue targets on both slaves first (non-triggering set)
    left.PR_move_cm_set (pr_num, 1, DM2J_RPM, left_target,  DM2J_ACC, DM2J_DEC);
    right.PR_move_cm_set(pr_num, 1, DM2J_RPM, right_target, DM2J_ACC, DM2J_DEC);

    // Single broadcast trigger fires feet (1,3) simultaneously.
    // Bystanders (2,4,5) receive it too but their PR<pr_num> = rpm=0 → no motion.
    left.PR_trigger_sync(pr_num);

    return dm2j_pair_poll_done(left, right, left_target, right_target);
}

// Absolute-target sync variant (same precondition as above)
static bool dm2j_pair_rail_move_abs_sync(DM2J_RS570& left, DM2J_RS570& right,
                                          int pr_num, double target_cm) {
    cout << "    DM2J " << DM2J_LEFT_RAIL << "+" << DM2J_RIGHT_RAIL
         << " → absolute " << fixed << setprecision(3) << target_cm << " cm (broadcast sync)\n";
    left.PR_move_cm_set (pr_num, 1, DM2J_RPM, target_cm, DM2J_ACC, DM2J_DEC);
    right.PR_move_cm_set(pr_num, 1, DM2J_RPM, target_cm, DM2J_ACC, DM2J_DEC);
    left.PR_trigger_sync(pr_num);
    return dm2j_pair_poll_done(left, right, target_cm, target_cm);
}

// Set bystander slaves' PR<pr_num> to rpm=0 (safe no-op for broadcast-sync use)
static void dm2j_set_safe_pr(std::vector<DM2J_RS570*>& slaves, int pr_num) {
    for (auto* d : slaves) {
        d->PR_move_set(pr_num, 1 /*absolute*/, 0 /*rpm=0*/, 0, 50, 100);
    }
}


static void test_full_step() {
    cout << "\n--- Full step sequence (8 pushers staged 7/10cm + rail + vacuum + retry) ---\n";

    string ip20, ip21, ip22;
    cout << "DM2J gateway IP  [192.168.1.20]: ";  getline(cin, ip20);  if (ip20.empty()) ip20 = "192.168.1.20";
    cout << "ZDT gateway IP   [192.168.1.21]: ";  getline(cin, ip21);  if (ip21.empty()) ip21 = "192.168.1.21";
    cout << "JC/PQW gateway IP[192.168.1.22]: ";  getline(cin, ip22);  if (ip22.empty()) ip22 = "192.168.1.22";

    int step_cm = 10, num_steps = 1, threshold = -300, retry_cnt = 3;
    double rail_backup_cm = 5.0;
    string s;
    cout << "Step distance cm [10]: ";                        getline(cin, s); if (!s.empty()) step_cm   = stoi(s);
    cout << "Number of steps  [1]: ";                         getline(cin, s); if (!s.empty()) num_steps = stoi(s);
    cout << "Vacuum threshold (0.1 kPa, -300=-30kPa) [-300]: "; getline(cin, s); if (!s.empty()) threshold = stoi(s);
    cout << "Vacuum retry count [3]: ";                       getline(cin, s); if (!s.empty()) retry_cnt = stoi(s);
    cout << "Retry rail back-off cm (DM2J retract per retry) [5]: "; getline(cin, s); if (!s.empty()) rail_backup_cm = stod(s);

    // No longer pre-clamp rail_backup_cm. Instead retry_grip_rail dynamically skips
    // any retry whose back-off would push rail past the phase-origin (rail_cur for
    // feet / rail_after_A for body). Preserved warning below when user input clearly
    // exceeds per-step capacity, so operator knows some retries will be skipped.
    if (retry_cnt > 0 && rail_backup_cm * retry_cnt > step_cm) {
        cout << "  [WARN] rail_backup_cm=" << rail_backup_cm << " × retry_cnt=" << retry_cnt
             << " = " << (rail_backup_cm * retry_cnt) << "cm > step_cm=" << step_cm << "\n";
        cout << "         retries exceeding per-step forward travel will be skipped at runtime\n";
    }

    // Probe all 3 gateways
    if (!quick_tcp_probe(ip20, 4001)) { cerr << "[ERR] " << ip20 << ":4001 unreachable\n"; return; }
    if (!quick_tcp_probe(ip21, 4001)) { cerr << "[ERR] " << ip21 << ":4001 unreachable\n"; return; }
    if (!quick_tcp_probe(ip22, 4001)) { cerr << "[ERR] " << ip22 << ":4001 unreachable\n"; return; }

    // Connect 3 TCP clients
    TCP_client cli20, cli21, cli22;
    if (!cli20.connectToServer(ip20, 4001, false)) { cerr << "[ERR] DM2J TCP connect fail\n"; return; }
    if (!cli21.connectToServer(ip21, 4001, false)) { cerr << "[ERR] ZDT TCP connect fail\n";  cli20.close(); return; }
    if (!cli22.connectToServer(ip22, 4001, false)) { cerr << "[ERR] .22 TCP connect fail\n";  cli20.close(); cli21.close(); return; }

    // Init drivers
    DM2J_RS570 dm2j_L, dm2j_R;
    if (dm2j_L.init(cli20, DM2J_LEFT_RAIL,  true) ||
        dm2j_R.init(cli20, DM2J_RIGHT_RAIL, true)) {
        cerr << "[ERR] DM2J init fail\n"; cli20.close(); cli21.close(); cli22.close(); return;
    }

    // Bystander DM2J slaves (wheels 2,4 / arm 5). They share the same RS485 bus
    // and WILL receive our PR_trigger_sync broadcasts. Init them + set their
    // PR1 to rpm=0 (safe no-op) so the broadcast doesn't accidentally drive them.
    DM2J_RS570 dm2j_wheelL, dm2j_wheelR, dm2j_arm;
    bool bystanders_ok = true;
    if (dm2j_wheelL.init(cli20, 2, false) ||
        dm2j_wheelR.init(cli20, 4, false) ||
        dm2j_arm   .init(cli20, 5, false)) {
        cerr << "[WARN] bystander DM2J init fail — broadcast sync may move wheels/arm\n";
        bystanders_ok = false;
    }
    if (bystanders_ok) {
        std::vector<DM2J_RS570*> bystanders = {&dm2j_wheelL, &dm2j_wheelR, &dm2j_arm};
        dm2j_set_safe_pr(bystanders, 1);   // PR1 rpm=0 so broadcast PR1 = no-op for them
        cout << "  → bystander PR1 set to rpm=0 (wheels 2,4 / arm 5)\n";
    }

    ZDT_motor_control zdts[10];
    for (int i = 1; i <= 9; ++i) {
        if (zdts[i].init(cli21, i, false)) {
            cerr << "[ERR] ZDT slave " << i << " init fail\n";
            cli20.close(); cli21.close(); cli22.close(); return;
        }
    }

    JC_100_METER jcs[10];
    for (int i = 1; i <= 9; ++i) {
        if (jcs[i].init(cli22, i, false)) {
            cerr << "[ERR] JC-100 slave " << i << " init fail\n";
            cli20.close(); cli21.close(); cli22.close(); return;
        }
    }

    PQW_IO_16O_RLY pqw;
    if (pqw.init(cli22, PQW_SLAVE, 8, false)) {
        cerr << "[ERR] PQW init fail\n";
        cli20.close(); cli21.close(); cli22.close(); return;
    }

    cout << "\n[SETUP] all drivers initialized.\n";
    cout << "PRE-FLIGHT: place robot against wall. This test will perform initial attach\n"
         << "            (extend feet + body pushers, then wait for operator to open valves)\n"
         << "            before starting the step cycle.\n";
    cout << "Press Enter to start, 'q' to abort: ";
    getline(cin, s);
    if (s == "q" || s == "Q") { cli20.close(); cli21.close(); cli22.close(); return; }

    // Ensure pump is on (idempotent, fast-exit if already)
    pqw.controlRelay(PQW_CH_PUMP, true);

    // Slave mapping (updated 2026-04-23):
    //   feet  left=3,4 / right=1,2 → all feet = {1,2,3,4}
    //   body  left=6,8 / right=5,7 → all body = {5,6,7,8}
    //   center = 9 (NOT used in this test — center pusher + CH4 skipped)
    std::vector<int> feet_slaves = {1, 2, 3, 4};
    std::vector<int> body_slaves = {5, 6, 7, 8};

    // ===== Initial attach phase 1: extend pushers with valves CLOSED =====
    // Operator can inspect cup placement before vacuum is engaged.
    cout << "\n======== INITIAL ATTACH (valves closed) ========\n";

    cout << "  → extend feet pushers ~8 cm (ZDT 1,2,3,4 → " << PUSHER_EXTEND_FEET_PULSE << " pulses)\n";
    zdt_group_extend_staged(zdts, feet_slaves, PUSHER_EXTEND_FEET_PULSE);

    cout << "  → extend body pushers ~10 cm (ZDT 5,6,7,8 → " << PUSHER_EXTEND_BODY_PULSE << " pulses)\n";
    zdt_group_extend_staged(zdts, body_slaves, PUSHER_EXTEND_BODY_PULSE);

    std::vector<int> all_slaves_for_report = {1, 2, 3, 4, 5, 6, 7, 8};
    cout << "  → read JC-100 1..8 (valves still CLOSED — expect ≈0)\n";
    vacuum_report(jcs, all_slaves_for_report);

    // User gate — operator decides when to engage vacuum
    cout << "\n  Press Enter to OPEN valves (CH" << PQW_CH_VALVE_FEET
         << "+CH" << PQW_CH_VALVE_BODY << ") and start step cycle, 'q' to abort: ";
    getline(cin, s);
    bool user_aborted = (s == "q" || s == "Q");

    if (!user_aborted) {
        cout << "  → open feet + body valves\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, true);
        pqw.controlRelay(PQW_CH_VALVE_BODY, true);
        cout << "  → settle " << VACUUM_SETTLE_MS << "ms\n";
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → read JC-100 1..8 (after valves opened)\n";
        vacuum_report(jcs, all_slaves_for_report);
    }

    // Rail baseline — record the rail position at "step 0" (after initial attach).
    // Each step's invariant: rail returns to baseline at end-of-step. If Phase B retry
    // leaves rail offset from baseline, next step's Phase A reduces target by that
    // offset so body can fully catch up (Phase B closes to baseline regardless of
    // feet delta). See memory: "步伐補償".
    double rail_baseline_L = 0, rail_baseline_R = 0;
    if (!user_aborted) {
        if (dm2j_L.read_position_cm(rail_baseline_L) || dm2j_R.read_position_cm(rail_baseline_R)) {
            cerr << "[ABORT] rail baseline read failed — skipping step cycle\n";
            user_aborted = true;
        } else {
            cout << "  rail baseline recorded: L=" << fixed << setprecision(3) << rail_baseline_L
                 << " cm, R=" << rail_baseline_R << " cm\n";
        }
    }

    // Rail-backup retry strategy (like WASH_ROBOT feet_backup / body_backup):
    //   Per retry, retract pushers → rail retreats rail_backup_cm in opposite direction of
    //   phase forward → re-extend pushers → re-engage valve → re-verify.
    //   This lets the robot try to attach at a slightly different vertical position.
    //   rail_backup_signed_cm: -rail_backup_cm for feet, +rail_backup_cm for body.
    //   Per attempt it accumulates, so after N retries rail is at target ± (N × backup).
    //   Magnitude is user-configurable and clamped so total back-off ≤ step_cm (rail cannot
    //   retreat past the pre-step position). See prompt block near top of this function.
    auto retry_grip_rail = [&](const std::string& group_name,
                               const std::vector<int>& zdt_group,
                               const std::vector<int>& jc_group,
                               int valve_ch,
                               int extra_valve_ch,
                               double rail_backup_signed_cm,
                               double max_total_backoff_cm) -> bool {
        // Pick extend target based on group (feet = 1-4 / body = 5-8)
        int extend_target = (!zdt_group.empty() && zdt_group.front() <= 4)
                            ? PUSHER_EXTEND_FEET_PULSE
                            : PUSHER_EXTEND_BODY_PULSE;
        const double per_backoff = std::fabs(rail_backup_signed_cm);
        double cumulative_backoff = 0.0;
        for (int r = 1; r <= retry_cnt; ++r) {
            // Skip retry if its back-off would push rail past phase origin
            if (cumulative_backoff + per_backoff > max_total_backoff_cm + 1e-6) {
                cout << "  [RETRY " << r << "/" << retry_cnt << "] skipped — cumulative back-off "
                     << (cumulative_backoff + per_backoff) << "cm would exceed limit "
                     << max_total_backoff_cm << "cm (rail at phase origin)\n";
                return false;
            }
            cout << "  [RETRY " << r << "/" << retry_cnt << "] re-grip " << group_name
                 << " (rail " << (rail_backup_signed_cm >= 0 ? "+" : "")
                 << rail_backup_signed_cm << "cm, cum "
                 << (cumulative_backoff + per_backoff) << "/" << max_total_backoff_cm << ")\n";
            pqw.controlRelay(valve_ch, false);
            if (extra_valve_ch) pqw.controlRelay(extra_valve_ch, false);
            this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

            // Retract pushers so they clear wall for rail motion
            zdt_group_move_sync(zdts, zdt_group, PUSHER_RETRACT_PULSE);

            // Rail back off (relative) in the retreat direction for this phase
            if (dm2j_pair_rail_move_sync(dm2j_L, dm2j_R, 1, rail_backup_signed_cm)) {
                cerr << "  [ABORT RETRY] rail backup failed on attempt " << r << "\n";
                return false;   // give up: next retry wouldn't be at a new position
            }
            cumulative_backoff += per_backoff;

            // Valve ON BEFORE extend — vacuum pulling on wall contact
            pqw.controlRelay(valve_ch, true);
            if (extra_valve_ch) pqw.controlRelay(extra_valve_ch, true);

            // Re-extend to wall at new position (cup pre-vacuumed), staged half→full
            zdt_group_extend_staged(zdts, zdt_group, extend_target);


            this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));
            if (vacuum_verify(jcs, jc_group, threshold)) return true;
        }
        return false;
    };

    for (int step = 1; step <= num_steps && !user_aborted; ++step) {
        cout << "\n======== STEP " << step << "/" << num_steps << " ========\n";

        // New order (2026-04-24): body FIRST, feet SECOND.
        // Both rail moves use ABSOLUTE targets (dm2j_pair_rail_move_abs_sync):
        //   Phase A (body): rail → absolute +step_cm  (body descends, rail extends)
        //   Phase B (feet): rail → absolute 0         (feet catches up, rail retracts)
        // Step compensation logic removed — rely on absolute positioning instead.

        // ===== Phase A: Body (first) =====
        cout << "\n▶ Phase A: Body\n";
        cout << "  → release body valve (CH" << PQW_CH_VALVE_BODY << " OFF)\n";
        pqw.controlRelay(PQW_CH_VALVE_BODY, false);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

        cout << "  → retract body pushers (ZDT 5,6,7,8)\n";
        zdt_group_move_sync(zdts, body_slaves, PUSHER_RETRACT_PULSE);

        cout << "  → rail → absolute +" << step_cm << " cm (body descends)\n";
        if (dm2j_pair_rail_move_abs_sync(dm2j_L, dm2j_R, 1, (double)step_cm)) {
            cerr << "[ABORT] body rail move failed. Stopping step cycle.\n";
            break;
        }

        // Valve ON BEFORE extend — vacuum pulling so cup seals instantly on wall contact
        cout << "  → pre-engage body valve (CH" << PQW_CH_VALVE_BODY << " ON)\n";
        pqw.controlRelay(PQW_CH_VALVE_BODY, true);

        cout << "  → extend body pushers ~10 cm staged (ZDT 5,6,7,8 → " << PUSHER_EXTEND_BODY_PULSE << " pulses)\n";
        zdt_group_extend_staged(zdts, body_slaves, PUSHER_EXTEND_BODY_PULSE);

        cout << "  → settle " << VACUUM_SETTLE_MS << "ms\n";
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → verify JC-100 5,6,7,8 vs threshold " << threshold << "\n";
        if (!vacuum_verify(jcs, body_slaves, threshold)) {
            // Body phase forward = rail +cm, retry backs off by -rail_backup_cm each.
            if (!retry_grip_rail("body", body_slaves, body_slaves,
                                 PQW_CH_VALVE_BODY, 0, -rail_backup_cm, (double)step_cm)) {
                cerr << "\n[ABORT] body vacuum fail after " << retry_cnt << " retries. Stopping.\n";
            //    break;
            }
        }

        // ===== Phase B: Feet (second) =====
        cout << "\n▶ Phase B: Feet\n";
        cout << "  → release feet valve (CH" << PQW_CH_VALVE_FEET << " OFF)\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, false);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

        cout << "  → retract feet pushers (ZDT 1,2,3,4)\n";
        zdt_group_move_sync(zdts, feet_slaves, PUSHER_RETRACT_PULSE);

        cout << "  → rail → absolute 0 cm (feet catches up)\n";
        if (dm2j_pair_rail_move_abs_sync(dm2j_L, dm2j_R, 1, 0.0)) {
            cerr << "[ABORT] feet rail move failed. Stopping step cycle.\n";
            break;
        }

        // Valve ON BEFORE extend
        cout << "  → pre-engage feet valve (CH" << PQW_CH_VALVE_FEET << " ON)\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, true);

        cout << "  → extend feet pushers ~8 cm staged (ZDT 1,2,3,4 → " << PUSHER_EXTEND_FEET_PULSE << " pulses)\n";
        zdt_group_extend_staged(zdts, feet_slaves, PUSHER_EXTEND_FEET_PULSE);

        cout << "  → settle " << VACUUM_SETTLE_MS << "ms\n";
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → verify JC-100 1,2,3,4 vs threshold " << threshold << "\n";
        if (!vacuum_verify(jcs, feet_slaves, threshold)) {
            // Feet phase forward = rail -cm (+step→0), retry backs off by +rail_backup_cm each.
            if (!retry_grip_rail("feet", feet_slaves, feet_slaves,
                                 PQW_CH_VALVE_FEET, 0, +rail_backup_cm, (double)step_cm)) {
                cerr << "\n[ABORT] feet vacuum fail after " << retry_cnt << " retries. Stopping.\n";
            //    break;
            }
        }

        cout << "\n  ✓ STEP " << step << " complete\n";

        if (step < num_steps) {
            cout << "  press Enter for next step, 'q' to stop here: ";
            getline(cin, s);
            if (s == "q" || s == "Q") break;
        }
    }

    // ===== Full cleanup: release vacuum, retract all pushers to 0, pump off =====
    // Order matters: valves OFF first (cups release), then retract pushers (safe to
    // pull back without tearing seal), then pump OFF, then disable drivers.
    cout << "\n======== CLEANUP ========\n";
    cout << "  → release all valves (CH" << PQW_CH_VALVE_FEET
         << "/CH" << PQW_CH_VALVE_BODY << "/CH" << PQW_CH_VALVE_CENTER << " OFF)\n";
    pqw.controlRelay(PQW_CH_VALVE_FEET,   false);
    pqw.controlRelay(PQW_CH_VALVE_BODY,   false);
    pqw.controlRelay(PQW_CH_VALVE_CENTER, false);
    pqw.controlRelay(PQW_CH_PUMP, false);
    this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

    cout << "  → retract feet pushers (ZDT 1,2,3,4) to 0\n";
    zdt_group_move_sync(zdts, feet_slaves, PUSHER_RETRACT_PULSE);

    cout << "  → retract body pushers (ZDT 5,6,7,8) to 0\n";
    zdt_group_move_sync(zdts, body_slaves, PUSHER_RETRACT_PULSE);

    cout << "  → return DM2J rails to absolute 0\n";
    if (dm2j_pair_rail_move_abs_sync(dm2j_L, dm2j_R, 1, 0.0)) {
        cerr << "    [WARN] DM2J return-to-0 failed (rails may not be at 0)\n";
    }

    cout << "  → all PQW relays OFF (pump + any stray CH5-8)\n";
    pqw.controlAll(false);

    cout << "  → disable ZDT drivers\n";
    for (int i = 1; i <= 9; ++i) zdts[i].motion_control_driver_EN(false);

    cli20.close(); cli21.close(); cli22.close();
}


//=========== 8. Full step sequence WITHOUT rail (pusher + vacuum only) ===========
//
// Same as option 7 but skips the DM2J rail move. Each step is essentially a
// detach → retract → extend → re-attach cycle per group. Useful when:
//   - rail / DM2J hardware not ready
//   - testing on a flat surface where rail would collide
//   - validating pusher + vacuum + retry logic in isolation
// ============================================================

static void test_full_step_no_rail() {
    cout << "\n--- Full step sequence WITHOUT rail (pusher + vacuum only) ---\n";

    string ip21, ip22;
    cout << "ZDT gateway IP   [192.168.1.21]: ";  getline(cin, ip21);  if (ip21.empty()) ip21 = "192.168.1.21";
    cout << "JC/PQW gateway IP[192.168.1.22]: ";  getline(cin, ip22);  if (ip22.empty()) ip22 = "192.168.1.22";

    int num_steps = 1, threshold = -300, retry_cnt = 3;
    string s;
    cout << "Number of steps [1]: ";                          getline(cin, s); if (!s.empty()) num_steps = stoi(s);
    cout << "Vacuum threshold (0.1 kPa, -300=-30kPa) [-300]: "; getline(cin, s); if (!s.empty()) threshold = stoi(s);
    cout << "Vacuum retry count [3]: ";                       getline(cin, s); if (!s.empty()) retry_cnt = stoi(s);

    if (!quick_tcp_probe(ip21, 4001)) { cerr << "[ERR] " << ip21 << ":4001 unreachable\n"; return; }
    if (!quick_tcp_probe(ip22, 4001)) { cerr << "[ERR] " << ip22 << ":4001 unreachable\n"; return; }

    TCP_client cli21, cli22;
    if (!cli21.connectToServer(ip21, 4001, false)) { cerr << "[ERR] ZDT TCP connect fail\n"; return; }
    if (!cli22.connectToServer(ip22, 4001, false)) { cerr << "[ERR] .22 TCP connect fail\n"; cli21.close(); return; }

    ZDT_motor_control zdts[10];
    for (int i = 1; i <= 9; ++i) {
        if (zdts[i].init(cli21, i, false)) {
            cerr << "[ERR] ZDT slave " << i << " init fail\n";
            cli21.close(); cli22.close(); return;
        }
    }

    JC_100_METER jcs[10];
    for (int i = 1; i <= 9; ++i) {
        if (jcs[i].init(cli22, i, false)) {
            cerr << "[ERR] JC-100 slave " << i << " init fail\n";
            cli21.close(); cli22.close(); return;
        }
    }

    PQW_IO_16O_RLY pqw;
    if (pqw.init(cli22, PQW_SLAVE, 8, false)) {
        cerr << "[ERR] PQW init fail\n"; cli21.close(); cli22.close(); return;
    }

    cout << "\n[SETUP] drivers initialized.\n";
    cout << "PRE-FLIGHT: place robot against wall. This test will perform initial attach\n"
         << "            (extend all 8 pushers + engage valves) before starting the step cycle.\n";
    cout << "Press Enter to start, 'q' to abort: ";
    getline(cin, s);
    if (s == "q" || s == "Q") { cli21.close(); cli22.close(); return; }

    pqw.controlRelay(PQW_CH_PUMP, true);

    // Slave mapping (updated 2026-04-23):
    //   feet  left=3,4 / right=1,2 → all feet = {1,2,3,4}
    //   body  left=6,8 / right=5,7 → all body = {5,6,7,8}
    //   center = 9
    std::vector<int> feet_slaves = {1, 2, 3, 4};
    std::vector<int> body_slaves = {5, 6, 7, 8};
    // Note: ZDT slave 9 (center) and PQW CH4 (center valve) intentionally skipped.

    auto retry_grip = [&](const std::string& group_name,
                          const std::vector<int>& zdt_group,
                          const std::vector<int>& jc_group,
                          int valve_ch,
                          int extra_valve_ch) -> bool {
        // Pick extend target based on group (feet = 1-4 / body = 5-8)
        int extend_target = (!zdt_group.empty() && zdt_group.front() <= 4)
                            ? PUSHER_EXTEND_FEET_PULSE
                            : PUSHER_EXTEND_BODY_PULSE;
        for (int r = 1; r <= retry_cnt; ++r) {
            cout << "  [RETRY " << r << "/" << retry_cnt << "] re-grip " << group_name << "\n";
            pqw.controlRelay(valve_ch, false);
            if (extra_valve_ch) pqw.controlRelay(extra_valve_ch, false);
            this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));
            zdt_group_move_sync(zdts, zdt_group, PUSHER_BACKOFF_PULSE);

            // Valve ON BEFORE re-extend — cup pre-vacuumed on wall contact
            pqw.controlRelay(valve_ch, true);
            if (extra_valve_ch) pqw.controlRelay(extra_valve_ch, true);

            zdt_group_extend_staged(zdts, zdt_group, extend_target);

            this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));
            if (vacuum_verify(jcs, jc_group, threshold)) return true;
        }
        return false;
    };

    // ===== Initial attach phase 1: extend pushers with valves CLOSED =====
    // Operator can inspect cup placement before vacuum is engaged.
    cout << "\n======== INITIAL ATTACH (valves closed) ========\n";

    cout << "  → extend feet pushers ~8 cm (ZDT 1,2,3,4 → " << PUSHER_EXTEND_FEET_PULSE << " pulses)\n";
    zdt_group_extend_staged(zdts, feet_slaves, PUSHER_EXTEND_FEET_PULSE);

    cout << "  → extend body pushers ~10 cm (ZDT 5,6,7,8 → " << PUSHER_EXTEND_BODY_PULSE << " pulses)\n";
    zdt_group_extend_staged(zdts, body_slaves, PUSHER_EXTEND_BODY_PULSE);

    std::vector<int> all_slaves_for_report = {1, 2, 3, 4, 5, 6, 7, 8};
    cout << "  → read JC-100 1..8 (valves still CLOSED — expect ≈0)\n";
    vacuum_report(jcs, all_slaves_for_report);

    // User gate — operator decides when to engage vacuum
    cout << "\n  Press Enter to OPEN valves (CH" << PQW_CH_VALVE_FEET
         << "+CH" << PQW_CH_VALVE_BODY << ") and continue, 'q' to abort: ";
    getline(cin, s);
    bool user_aborted = (s == "q" || s == "Q");

    if (!user_aborted) {
        cout << "  → open feet + body valves\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, true);
        pqw.controlRelay(PQW_CH_VALVE_BODY, true);
        cout << "  → settle " << VACUUM_SETTLE_MS << "ms\n";
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → read JC-100 1..8 (after valves opened)\n";
        vacuum_report(jcs, all_slaves_for_report);
    }

    for (int step = 1; step <= num_steps && !user_aborted; ++step) {
        cout << "\n======== STEP " << step << "/" << num_steps << " (no-rail) ========\n";

        // Phase A: Feet
        cout << "\n▶ Phase A: Feet (in place)\n";
        cout << "  → release feet valve (CH" << PQW_CH_VALVE_FEET << " OFF)\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, false);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

        cout << "  → retract feet pushers (ZDT 1,2,3,4)\n";
        zdt_group_move_sync(zdts, feet_slaves, PUSHER_RETRACT_PULSE);

        // Valve ON BEFORE extend — cup pre-vacuumed so it seals instantly on wall contact
        cout << "  → pre-engage feet valve (CH" << PQW_CH_VALVE_FEET << " ON)\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, true);

        cout << "  → extend feet pushers ~8 cm (into pre-vacuumed cups)\n";
        zdt_group_extend_staged(zdts, feet_slaves, PUSHER_EXTEND_FEET_PULSE);

        cout << "  → settle " << VACUUM_SETTLE_MS << "ms\n";
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → read JC-100 1,2,3,4 (report only, no threshold check)\n";
        vacuum_report(jcs, feet_slaves);

        // Phase B: Body (center skipped)
        cout << "\n▶ Phase B: Body (in place)\n";
        cout << "  → release body valve (CH" << PQW_CH_VALVE_BODY << " OFF)\n";
        pqw.controlRelay(PQW_CH_VALVE_BODY, false);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

        cout << "  → retract body pushers (ZDT 5,6,7,8)\n";
        zdt_group_move_sync(zdts, body_slaves, PUSHER_RETRACT_PULSE);

        // Valve ON BEFORE extend
        cout << "  → pre-engage body valve (CH" << PQW_CH_VALVE_BODY << " ON)\n";
        pqw.controlRelay(PQW_CH_VALVE_BODY, true);

        cout << "  → extend body pushers ~10 cm (into pre-vacuumed cups)\n";
        zdt_group_extend_staged(zdts, body_slaves, PUSHER_EXTEND_BODY_PULSE);

        cout << "  → settle " << VACUUM_SETTLE_MS << "ms\n";
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → read JC-100 5,6,7,8 (report only, no threshold check)\n";
        vacuum_report(jcs, body_slaves);

        cout << "\n  ✓ STEP " << step << " complete\n";

        if (step < num_steps) {
            cout << "  press Enter for next step, 'q' to stop here: ";
            getline(cin, s);
            if (s == "q" || s == "Q") break;
        }
    }

    // ===== Full cleanup: release vacuum, retract all pushers to 0, pump off =====
    // Order matters: valves OFF first (cups release), then retract pushers (safe to
    // pull back without tearing seal), then pump OFF, then disable drivers.
    cout << "\n======== CLEANUP ========\n";
    cout << "  → release all valves (CH" << PQW_CH_VALVE_FEET
         << "/CH" << PQW_CH_VALVE_BODY << "/CH" << PQW_CH_VALVE_CENTER << " OFF)\n";
    pqw.controlRelay(PQW_CH_VALVE_FEET,   false);
    pqw.controlRelay(PQW_CH_VALVE_BODY,   false);
    pqw.controlRelay(PQW_CH_VALVE_CENTER, false);
    this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

    cout << "  → retract feet pushers (ZDT 1,2,3,4) to 0\n";
    zdt_group_move_sync(zdts, feet_slaves, PUSHER_RETRACT_PULSE);

    cout << "  → retract body pushers (ZDT 5,6,7,8) to 0\n";
    zdt_group_move_sync(zdts, body_slaves, PUSHER_RETRACT_PULSE);

    cout << "  → pump OFF (CH" << PQW_CH_PUMP << ")\n";
    pqw.controlRelay(PQW_CH_PUMP, false);

    cout << "  → disable ZDT drivers\n";
    for (int i = 1; i <= 9; ++i) zdts[i].motion_control_driver_EN(false);

    cli21.close(); cli22.close();
}


//=========== 9. SD76 length meter (計米器) ===========
//
// Live-reads the meter display value. Supports reset/pause/resume.
// Main crane has 3 meters: slave 2 (左鋼索), 3 (右鋼索), 4 (中間管線) on gateway .30.
// Easy crane doesn't use SD76.
// ============================================================
static void test_sd76() {
    cout << "\n--- SD76 length meter ---\n";
    string ip;
    cout << "Gateway IP [192.168.1.30]: ";
    getline(cin, ip);
    if (ip.empty()) ip = "192.168.1.30";

    cout << "Slave ID (2=左鋼索 / 3=右鋼索 / 4=中間管線) [2]: ";
    string s; getline(cin, s);
    int slave = s.empty() ? 2 : stoi(s);

    if (!quick_tcp_probe(ip, 4001)) {
        cerr << "[ERR] " << ip << ":4001 unreachable (2s timeout)\n"; return;
    }
    TCP_client cli;
    if (!cli.connectToServer(ip, 4001, false)) {
        cerr << "[ERR] Cannot connect to " << ip << ":4001\n"; return;
    }

    SD76_length_meters drv;
    if (drv.init(cli, slave, true)) {
        cerr << "[ERR] SD76 slave " << slave << " init fail\n"; cli.close(); return;
    }

    cout << "[SD76:" << slave << "] commands: r=reset  p=pause  s=resume  q=quit\n";
    cout << "Live reading (press Enter for menu):\n";

    atomic<bool> stop_flag(false);
    atomic<char> cmd_flag{0};
    thread input_thread([&]() {
        string line;
        while (!stop_flag.load() && getline(cin, line)) {
            if (line.empty()) continue;
            char c = line[0];
            if (c == 'q' || c == 'Q') { stop_flag = true; break; }
            cmd_flag = c;
        }
    });

    while (!stop_flag.load()) {
        int32_t v = 0;
        bool err = drv.readUpperInteger(v);
        uint8_t mode = 0, alarm = 0;
        bool err_s = drv.readStatus(mode, alarm);

        if (err) {
            cout << "\r[SD76:" << slave << "] READ ERROR                                   ";
        } else {
            cout << "\r[SD76:" << slave << "] length = " << setw(10) << v
                 << "  status: mode=0x" << hex << (int)mode
                 << " alarm=0x" << (int)alarm << dec
                 << (err_s ? " (status read err)" : "")
                 << "    ";
        }
        cout.flush();

        char c = cmd_flag.exchange(0);
        if (c == 'r' || c == 'R') {
            cout << "\n  → resetAll()\n";
            drv.resetAll();
        } else if (c == 'p' || c == 'P') {
            cout << "\n  → pauseMeter()\n";
            drv.pauseMeter();
        } else if (c == 's' || c == 'S') {
            cout << "\n  → resumeMeter()\n";
            drv.resumeMeter();
        }

        this_thread::sleep_for(chrono::milliseconds(300));
    }
    cout << "\n";
    if (input_thread.joinable()) input_thread.join();
    cli.close();
}


//=========== 10. ZS_DIO winch relay (捲揚機) ===========
//
// Controls the winch direction relays. Supports:
//   - Main crane: slave 1 on .1.30, CH1=左收繩 / CH2=右收繩 / CH3=左放繩 / CH4=右放繩
//   - Easy crane: slave 1 on .1.21,  CH7=DOWN(放繩) / CH8=UP(拉繩)
// ============================================================
static void test_zsdio() {
    cout << "\n--- ZS_DIO winch relay ---\n";
    string ip;
    cout << "Gateway IP (main crane=.1.30 / easy crane=.1.21) [192.168.1.30]: ";
    getline(cin, ip);
    if (ip.empty()) ip = "192.168.1.30";

    cout << "Slave ID [1]: ";
    string s; getline(cin, s);
    int slave = s.empty() ? 1 : stoi(s);

    cout << "Total relay count (main crane 8 / easy crane 16) [8]: ";
    getline(cin, s);
    int total_relay = s.empty() ? 8 : stoi(s);

    if (!quick_tcp_probe(ip, 4001)) {
        cerr << "[ERR] " << ip << ":4001 unreachable (2s timeout)\n"; return;
    }
    TCP_client cli;
    if (!cli.connectToServer(ip, 4001, false)) {
        cerr << "[ERR] Cannot connect to " << ip << ":4001\n"; return;
    }

    ZS_DIO_R_RLY drv;
    if (drv.init(cli, slave, total_relay, true)) {
        cerr << "[ERR] ZS_DIO slave " << slave << " init fail\n"; cli.close(); return;
    }

    cout << "\nChannel hints (depending on config):\n"
         << "  Main crane (.1.30): CH1=左收繩  CH2=右收繩  CH3=左放繩  CH4=右放繩\n"
         << "  Easy crane (.1.21): CH7=DOWN(放繩)  CH8=UP(拉繩)\n"
         << "\n"
         << "⚠ SAFETY: turn off all channels (press 'o') before leaving.\n"
         << "  Verify LED / winch physically — readback is not always reliable.\n\n";

    while (true) {
        cout << "Input [on N / off N / a=all ON / o=all OFF / r=read N / q=back to menu]: ";
        string in; getline(cin, in);
        if (in == "q" || in.empty()) break;

        if (in == "a") {
            cout << (drv.controlAll(true)  ? "  [WARN] all ON reported error — check winches\n"
                                           : "  [OK] all ON (check physically)\n");
            continue;
        }
        if (in == "o") {
            cout << (drv.controlAll(false) ? "  [WARN] all OFF reported error — check winches\n"
                                           : "  [OK] all OFF\n");
            continue;
        }

        istringstream iss(in);
        string verb; int ch = 0;
        iss >> verb >> ch;
        if (ch < 1 || ch > total_relay) { cout << "  [!] channel out of range 1.." << total_relay << "\n"; continue; }

        if (verb == "on" || verb == "off") {
            bool want_on = (verb == "on");
            if (drv.controlRelay(ch, want_on))
                cerr << "  [WARN] CH" << ch << " " << verb
                     << " readback mismatch — check physically\n";
            else
                cout << "  [OK] CH" << ch << " " << verb << "\n";
        } else if (verb == "r") {
            std::vector<bool> states;
            if (drv.readCoils(ch, 1, states) || states.empty())
                cerr << "  [ERR] readCoils failed\n";
            else
                cout << "  CH" << ch << " = " << (states[0] ? "ON" : "OFF") << "\n";
        } else {
            cout << "  [!] usage: on N | off N | r N | a | o | q\n";
        }
    }

    cout << "[CLEANUP] turning all channels OFF\n";
    drv.controlAll(false);
    cli.close();
}


//=========== 11. Full step WITHOUT rail + vacuum verify + retry ===========
//
// Same structure as option 8 but each phase (initial attach, feet step, body step)
// verifies vacuum against threshold. Fail → retract that group + re-engage valve +
// re-extend + re-settle + re-verify. Up to N retries. All retries fail → ABORT.
// ============================================================
static void test_full_step_no_rail_verify() {
    cout << "\n--- Full step WITHOUT rail + vacuum verify + retry ---\n";

    string ip21, ip22;
    cout << "ZDT gateway IP   [192.168.1.21]: ";  getline(cin, ip21);  if (ip21.empty()) ip21 = "192.168.1.21";
    cout << "JC/PQW gateway IP[192.168.1.22]: ";  getline(cin, ip22);  if (ip22.empty()) ip22 = "192.168.1.22";

    int num_steps = 1, threshold = -300, retry_cnt = 3;
    string s;
    cout << "Number of steps [1]: ";                          getline(cin, s); if (!s.empty()) num_steps = stoi(s);
    cout << "Vacuum threshold (0.1 kPa, -300=-30kPa) [-300]: "; getline(cin, s); if (!s.empty()) threshold = stoi(s);
    cout << "Vacuum retry count [3]: ";                       getline(cin, s); if (!s.empty()) retry_cnt = stoi(s);

    if (!quick_tcp_probe(ip21, 4001)) { cerr << "[ERR] " << ip21 << ":4001 unreachable\n"; return; }
    if (!quick_tcp_probe(ip22, 4001)) { cerr << "[ERR] " << ip22 << ":4001 unreachable\n"; return; }

    TCP_client cli21, cli22;
    if (!cli21.connectToServer(ip21, 4001, false)) { cerr << "[ERR] ZDT TCP connect fail\n"; return; }
    if (!cli22.connectToServer(ip22, 4001, false)) { cerr << "[ERR] .22 TCP connect fail\n"; cli21.close(); return; }

    ZDT_motor_control zdts[10];
    for (int i = 1; i <= 9; ++i) {
        if (zdts[i].init(cli21, i, false)) {
            cerr << "[ERR] ZDT slave " << i << " init fail\n";
            cli21.close(); cli22.close(); return;
        }
    }

    JC_100_METER jcs[10];
    for (int i = 1; i <= 9; ++i) {
        if (jcs[i].init(cli22, i, false)) {
            cerr << "[ERR] JC-100 slave " << i << " init fail\n";
            cli21.close(); cli22.close(); return;
        }
    }

    PQW_IO_16O_RLY pqw;
    if (pqw.init(cli22, PQW_SLAVE, 8, false)) {
        cerr << "[ERR] PQW init fail\n"; cli21.close(); cli22.close(); return;
    }

    cout << "\n[SETUP] drivers initialized.\n";
    cout << "PRE-FLIGHT: place robot against wall. Test will do initial attach then\n"
         << "            verify vacuum after each phase; failures trigger re-grip.\n";
    cout << "Press Enter to start, 'q' to abort: ";
    getline(cin, s);
    if (s == "q" || s == "Q") { cli21.close(); cli22.close(); return; }

    pqw.controlRelay(PQW_CH_PUMP, true);

    std::vector<int> feet_slaves = {1, 2, 3, 4};
    std::vector<int> body_slaves = {5, 6, 7, 8};

    // retry_grip: release valve → retract → valve ON → re-extend → settle → verify.
    // Returns true if any retry succeeds, false if all retries exhausted.
    // Pick extend target based on group (feet=1-4 → 7cm, body=5-8 → 10cm).
    auto retry_grip = [&](const std::string& group_name,
                          const std::vector<int>& zdt_group,
                          int valve_ch) -> bool {
        int extend_target = (!zdt_group.empty() && zdt_group.front() <= 4)
                            ? PUSHER_EXTEND_FEET_PULSE
                            : PUSHER_EXTEND_BODY_PULSE;
        for (int r = 1; r <= retry_cnt; ++r) {
            cout << "  [RETRY " << r << "/" << retry_cnt << "] re-grip " << group_name << "\n";
            pqw.controlRelay(valve_ch, false);
            this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));
            zdt_group_move_sync(zdts, zdt_group, PUSHER_RETRACT_PULSE);

            pqw.controlRelay(valve_ch, true);
            zdt_group_extend_staged(zdts, zdt_group, extend_target);

            this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));
            if (vacuum_verify(jcs, zdt_group, threshold)) return true;
        }
        return false;
    };

    // ===== Initial attach phase 1: extend pushers with valves CLOSED =====
    cout << "\n======== INITIAL ATTACH (valves closed) ========\n";

    cout << "  → extend feet pushers ~8 cm (ZDT 1,2,3,4 → " << PUSHER_EXTEND_FEET_PULSE << " pulses)\n";
    zdt_group_extend_staged(zdts, feet_slaves, PUSHER_EXTEND_FEET_PULSE);

    cout << "  → extend body pushers ~10 cm (ZDT 5,6,7,8 → " << PUSHER_EXTEND_BODY_PULSE << " pulses)\n";
    zdt_group_extend_staged(zdts, body_slaves, PUSHER_EXTEND_BODY_PULSE);

    {
        std::vector<int> all8 = {1, 2, 3, 4, 5, 6, 7, 8};
        cout << "  → read JC-100 1..8 (valves still CLOSED — expect ≈0)\n";
        vacuum_report(jcs, all8);
    }

    // User gate
    cout << "\n  Press Enter to OPEN valves (CH" << PQW_CH_VALVE_FEET
         << "+CH" << PQW_CH_VALVE_BODY << ") and continue, 'q' to abort: ";
    getline(cin, s);
    bool user_aborted = (s == "q" || s == "Q");

    if (!user_aborted) {
        cout << "  → open feet + body valves\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, true);
        pqw.controlRelay(PQW_CH_VALVE_BODY, true);
        cout << "  → settle " << VACUUM_SETTLE_MS << "ms\n";
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → verify initial feet vacuum JC-100 1,2,3,4 vs threshold " << threshold << "\n";
        if (!vacuum_verify(jcs, feet_slaves, threshold)) {
            if (!retry_grip("feet (initial)", feet_slaves, PQW_CH_VALVE_FEET)) {
                cerr << "  [FAIL] initial feet attach failed after " << retry_cnt
                     << " retries — continuing anyway\n";
            }
        }

        cout << "  → verify initial body vacuum JC-100 5,6,7,8 vs threshold " << threshold << "\n";
        if (!vacuum_verify(jcs, body_slaves, threshold)) {
            if (!retry_grip("body (initial)", body_slaves, PQW_CH_VALVE_BODY)) {
                cerr << "  [FAIL] initial body attach failed after " << retry_cnt
                     << " retries — continuing anyway\n";
            }
        }
    }

    // ===== Step loop =====
    for (int step = 1; step <= num_steps && !user_aborted; ++step) {
        cout << "\n======== STEP " << step << "/" << num_steps << " (no-rail, verify) ========\n";

        // Phase A: Feet
        cout << "\n▶ Phase A: Feet (in place)\n";
        cout << "  → release feet valve (CH" << PQW_CH_VALVE_FEET << " OFF)\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, false);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

        cout << "  → retract feet pushers (ZDT 1,2,3,4)\n";
        zdt_group_move_sync(zdts, feet_slaves, PUSHER_RETRACT_PULSE);

        cout << "  → pre-engage feet valve (CH" << PQW_CH_VALVE_FEET << " ON)\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, true);

        cout << "  → extend feet pushers ~8 cm (into pre-vacuumed cups)\n";
        zdt_group_extend_staged(zdts, feet_slaves, PUSHER_EXTEND_FEET_PULSE);

        cout << "  → settle " << VACUUM_SETTLE_MS << "ms\n";
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → verify JC-100 1,2,3,4 vs threshold " << threshold << "\n";
        if (!vacuum_verify(jcs, feet_slaves, threshold)) {
            if (!retry_grip("feet", feet_slaves, PQW_CH_VALVE_FEET)) {
                cerr << "  [FAIL] feet vacuum fail after " << retry_cnt
                     << " retries — continuing to body phase\n";
            }
        }

        // Phase B: Body
        cout << "\n▶ Phase B: Body (in place)\n";
        cout << "  → release body valve (CH" << PQW_CH_VALVE_BODY << " OFF)\n";
        pqw.controlRelay(PQW_CH_VALVE_BODY, false);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

        cout << "  → retract body pushers (ZDT 5,6,7,8)\n";
        zdt_group_move_sync(zdts, body_slaves, PUSHER_RETRACT_PULSE);

        cout << "  → pre-engage body valve (CH" << PQW_CH_VALVE_BODY << " ON)\n";
        pqw.controlRelay(PQW_CH_VALVE_BODY, true);

        cout << "  → extend body pushers ~10 cm (into pre-vacuumed cups)\n";
        zdt_group_extend_staged(zdts, body_slaves, PUSHER_EXTEND_BODY_PULSE);

        cout << "  → settle " << VACUUM_SETTLE_MS << "ms\n";
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → verify JC-100 5,6,7,8 vs threshold " << threshold << "\n";
        if (!vacuum_verify(jcs, body_slaves, threshold)) {
            if (!retry_grip("body", body_slaves, PQW_CH_VALVE_BODY)) {
                cerr << "  [FAIL] body vacuum fail after " << retry_cnt
                     << " retries — continuing to next step\n";
            }
        }

        cout << "\n  ✓ STEP " << step << " complete\n";

        if (step < num_steps) {
            cout << "  press Enter for next step, 'q' to stop here: ";
            getline(cin, s);
            if (s == "q" || s == "Q") break;
        }
    }

    // ===== Full cleanup: release vacuum, retract all pushers to 0, pump off =====
    cout << "\n======== CLEANUP ========\n";
    cout << "  → release all valves (CH" << PQW_CH_VALVE_FEET
         << "/CH" << PQW_CH_VALVE_BODY << "/CH" << PQW_CH_VALVE_CENTER << " OFF)\n";
    pqw.controlRelay(PQW_CH_VALVE_FEET,   false);
    pqw.controlRelay(PQW_CH_VALVE_BODY,   false);
    pqw.controlRelay(PQW_CH_VALVE_CENTER, false);
    this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

    cout << "  → retract feet pushers (ZDT 1,2,3,4) to 0\n";
    zdt_group_move_sync(zdts, feet_slaves, PUSHER_RETRACT_PULSE);

    cout << "  → retract body pushers (ZDT 5,6,7,8) to 0\n";
    zdt_group_move_sync(zdts, body_slaves, PUSHER_RETRACT_PULSE);

    cout << "  → pump OFF (CH" << PQW_CH_PUMP << ")\n";
    pqw.controlRelay(PQW_CH_PUMP, false);

    cout << "  → disable ZDT drivers\n";
    for (int i = 1; i <= 9; ++i) zdts[i].motion_control_driver_EN(false);

    cli21.close(); cli22.close();
}


//=========== 12. Full step WITH rail, report only (no verify/retry) ===========
//
// Same structure as option 7 (rail + pushers + vacuum) but skips the vacuum
// threshold check and retry grip — just prints pressures after each phase and
// continues. Diagnostic counterpart to option 7, like option 8 is to option 11.
// ============================================================
static void test_full_step_report() {
    cout << "\n--- Full step WITH rail, report only ---\n";

    string ip20, ip21, ip22;
    cout << "DM2J gateway IP  [192.168.1.20]: ";  getline(cin, ip20);  if (ip20.empty()) ip20 = "192.168.1.20";
    cout << "ZDT gateway IP   [192.168.1.21]: ";  getline(cin, ip21);  if (ip21.empty()) ip21 = "192.168.1.21";
    cout << "JC/PQW gateway IP[192.168.1.22]: ";  getline(cin, ip22);  if (ip22.empty()) ip22 = "192.168.1.22";

    int step_cm = 10, num_steps = 1;
    string s;
    cout << "Step distance cm [10]: "; getline(cin, s); if (!s.empty()) step_cm   = stoi(s);
    cout << "Number of steps  [1]: ";  getline(cin, s); if (!s.empty()) num_steps = stoi(s);

    if (!quick_tcp_probe(ip20, 4001)) { cerr << "[ERR] " << ip20 << ":4001 unreachable\n"; return; }
    if (!quick_tcp_probe(ip21, 4001)) { cerr << "[ERR] " << ip21 << ":4001 unreachable\n"; return; }
    if (!quick_tcp_probe(ip22, 4001)) { cerr << "[ERR] " << ip22 << ":4001 unreachable\n"; return; }

    TCP_client cli20, cli21, cli22;
    if (!cli20.connectToServer(ip20, 4001, false)) { cerr << "[ERR] DM2J TCP connect fail\n"; return; }
    if (!cli21.connectToServer(ip21, 4001, false)) { cerr << "[ERR] ZDT TCP connect fail\n";  cli20.close(); return; }
    if (!cli22.connectToServer(ip22, 4001, false)) { cerr << "[ERR] .22 TCP connect fail\n";  cli20.close(); cli21.close(); return; }

    DM2J_RS570 dm2j_L, dm2j_R;
    if (dm2j_L.init(cli20, DM2J_LEFT_RAIL,  true) ||
        dm2j_R.init(cli20, DM2J_RIGHT_RAIL, true)) {
        cerr << "[ERR] DM2J init fail\n"; cli20.close(); cli21.close(); cli22.close(); return;
    }

    ZDT_motor_control zdts[10];
    for (int i = 1; i <= 9; ++i) {
        if (zdts[i].init(cli21, i, false)) {
            cerr << "[ERR] ZDT slave " << i << " init fail\n";
            cli20.close(); cli21.close(); cli22.close(); return;
        }
    }

    JC_100_METER jcs[10];
    for (int i = 1; i <= 9; ++i) {
        if (jcs[i].init(cli22, i, false)) {
            cerr << "[ERR] JC-100 slave " << i << " init fail\n";
            cli20.close(); cli21.close(); cli22.close(); return;
        }
    }

    PQW_IO_16O_RLY pqw;
    if (pqw.init(cli22, PQW_SLAVE, 8, false)) {
        cerr << "[ERR] PQW init fail\n"; cli20.close(); cli21.close(); cli22.close(); return;
    }

    cout << "\n[SETUP] all drivers initialized.\n";
    cout << "PRE-FLIGHT: place robot against wall. Test will do initial attach then\n"
         << "            report pressures after each phase (no threshold check).\n";
    cout << "Press Enter to start, 'q' to abort: ";
    getline(cin, s);
    if (s == "q" || s == "Q") { cli20.close(); cli21.close(); cli22.close(); return; }

    pqw.controlRelay(PQW_CH_PUMP, true);

    std::vector<int> feet_slaves = {1, 2, 3, 4};
    std::vector<int> body_slaves = {5, 6, 7, 8};

    // ===== Initial attach phase 1: extend pushers with valves CLOSED =====
    cout << "\n======== INITIAL ATTACH (valves closed) ========\n";

    cout << "  → extend feet pushers ~8 cm (ZDT 1,2,3,4 → " << PUSHER_EXTEND_FEET_PULSE << " pulses)\n";
    zdt_group_extend_staged(zdts, feet_slaves, PUSHER_EXTEND_FEET_PULSE);

    cout << "  → extend body pushers ~10 cm (ZDT 5,6,7,8 → " << PUSHER_EXTEND_BODY_PULSE << " pulses)\n";
    zdt_group_extend_staged(zdts, body_slaves, PUSHER_EXTEND_BODY_PULSE);

    std::vector<int> all8 = {1, 2, 3, 4, 5, 6, 7, 8};
    cout << "  → read JC-100 1..8 (valves still CLOSED — expect ≈0)\n";
    vacuum_report(jcs, all8);

    cout << "\n  Press Enter to OPEN valves (CH" << PQW_CH_VALVE_FEET
         << "+CH" << PQW_CH_VALVE_BODY << ") and continue, 'q' to abort: ";
    getline(cin, s);
    bool user_aborted = (s == "q" || s == "Q");

    if (!user_aborted) {
        cout << "  → open feet + body valves\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, true);
        pqw.controlRelay(PQW_CH_VALVE_BODY, true);
        cout << "  → settle " << VACUUM_SETTLE_MS << "ms\n";
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → read JC-100 1..8 (after valves opened)\n";
        vacuum_report(jcs, all8);
    }

    for (int step = 1; step <= num_steps && !user_aborted; ++step) {
        cout << "\n======== STEP " << step << "/" << num_steps << " (rail, report) ========\n";

        // ===== Phase A: Feet =====
        cout << "\n▶ Phase A: Feet\n";
        cout << "  → release feet valve (CH" << PQW_CH_VALVE_FEET << " OFF)\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, false);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

        cout << "  → retract feet pushers (ZDT 1,2,3,4)\n";
        zdt_group_move_sync(zdts, feet_slaves, PUSHER_RETRACT_PULSE);

        cout << "  → rail +" << step_cm << " cm\n";
        dm2j_pair_rail_move(dm2j_L, dm2j_R, 1, (double)step_cm);

        cout << "  → pre-engage feet valve (CH" << PQW_CH_VALVE_FEET << " ON)\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, true);

        cout << "  → extend feet pushers ~8 cm (into pre-vacuumed cups)\n";
        zdt_group_extend_staged(zdts, feet_slaves, PUSHER_EXTEND_FEET_PULSE);

        cout << "  → settle " << VACUUM_SETTLE_MS << "ms\n";
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → read JC-100 1,2,3,4 (report only, no threshold check)\n";
        vacuum_report(jcs, feet_slaves);

        // ===== Phase B: Body =====
        cout << "\n▶ Phase B: Body\n";
        cout << "  → release body valve (CH" << PQW_CH_VALVE_BODY << " OFF)\n";
        pqw.controlRelay(PQW_CH_VALVE_BODY, false);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

        cout << "  → retract body pushers (ZDT 5,6,7,8)\n";
        zdt_group_move_sync(zdts, body_slaves, PUSHER_RETRACT_PULSE);

        cout << "  → rail -" << step_cm << " cm (body slides relative to feet)\n";
        dm2j_pair_rail_move(dm2j_L, dm2j_R, 1, -(double)step_cm);

        cout << "  → pre-engage body valve (CH" << PQW_CH_VALVE_BODY << " ON)\n";
        pqw.controlRelay(PQW_CH_VALVE_BODY, true);

        cout << "  → extend body pushers ~10 cm (into pre-vacuumed cups)\n";
        zdt_group_extend_staged(zdts, body_slaves, PUSHER_EXTEND_BODY_PULSE);

        cout << "  → settle " << VACUUM_SETTLE_MS << "ms\n";
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → read JC-100 5,6,7,8 (report only, no threshold check)\n";
        vacuum_report(jcs, body_slaves);

        cout << "\n  ✓ STEP " << step << " complete\n";

        if (step < num_steps) {
            cout << "  press Enter for next step, 'q' to stop here: ";
            getline(cin, s);
            if (s == "q" || s == "Q") break;
        }
    }

    // ===== Full cleanup =====
    cout << "\n======== CLEANUP ========\n";
    cout << "  → release all valves (CH" << PQW_CH_VALVE_FEET
         << "/CH" << PQW_CH_VALVE_BODY << "/CH" << PQW_CH_VALVE_CENTER << " OFF)\n";
    pqw.controlRelay(PQW_CH_VALVE_FEET,   false);
    pqw.controlRelay(PQW_CH_VALVE_BODY,   false);
    pqw.controlRelay(PQW_CH_VALVE_CENTER, false);
    this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

    cout << "  → retract feet pushers (ZDT 1,2,3,4) to 0\n";
    zdt_group_move_sync(zdts, feet_slaves, PUSHER_RETRACT_PULSE);

    cout << "  → retract body pushers (ZDT 5,6,7,8) to 0\n";
    zdt_group_move_sync(zdts, body_slaves, PUSHER_RETRACT_PULSE);

    cout << "  → pump OFF (CH" << PQW_CH_PUMP << ")\n";
    pqw.controlRelay(PQW_CH_PUMP, false);

    cout << "  → disable ZDT drivers\n";
    for (int i = 1; i <= 9; ++i) zdts[i].motion_control_driver_EN(false);

    cli20.close(); cli21.close(); cli22.close();
}


//=========== 13. Water tank (PQW CH5/6/7) ===========
// Wait up to N seconds, polling stdin every 100ms.
// Returns true if user pressed Enter (abort), false on timeout.
static bool water_wait_or_abort(int seconds) {
    auto start = std::chrono::steady_clock::now();
    int total_ms = seconds * 1000;
    while (true) {
        auto now = std::chrono::steady_clock::now();
        int elapsed_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed_ms >= total_ms) break;
        int remain_s = (total_ms - elapsed_ms + 999) / 1000;
        cout << "\r  [running] " << setw(4) << remain_s << "s remaining (Enter to abort)   " << flush;

#ifdef _WIN32
        if (_kbhit()) {
            while (_kbhit()) (void)_getch();
            cout << "\r" << string(60, ' ') << "\r" << flush;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
#else
        fd_set rf; FD_ZERO(&rf); FD_SET(STDIN_FILENO, &rf);
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 100 * 1000;
        int n = select(STDIN_FILENO + 1, &rf, nullptr, nullptr, &tv);
        if (n > 0 && FD_ISSET(STDIN_FILENO, &rf)) {
            string junk; getline(cin, junk);
            cout << "\r" << string(60, ' ') << "\r" << flush;
            return true;
        }
#endif
    }
    cout << "\r" << string(60, ' ') << "\r" << flush;
    return false;
}

static void test_water_tank() {
    cout << "\n--- Water tank test (PQW CH5/6/7) ---\n";
    string ip;
    cout << "Gateway IP [192.168.1.22]: ";
    getline(cin, ip);
    if (ip.empty()) ip = "192.168.1.22";

    cout << "Slave ID [12]: ";
    string s; getline(cin, s);
    int slave = s.empty() ? 12 : stoi(s);

    if (!quick_tcp_probe(ip, 4001)) {
        cerr << "[ERR] " << ip << ":4001 unreachable (2s timeout)\n"; return;
    }
    TCP_client cli;
    if (!cli.connectToServer(ip, 4001, false)) {
        cerr << "[ERR] Cannot connect to " << ip << ":4001\n"; return;
    }

    PQW_IO_16O_RLY drv;
    if (drv.init(cli, slave, 8, true)) {
        cerr << "[ERR] PQW slave " << slave << " init fail\n";
        cli.close(); return;
    }

    constexpr int CH_BRUSH = 5;
    constexpr int CH_PUMP  = 6;
    constexpr int CH_VALVE = 7;
    constexpr int MAX_VALVE_SEC = 120;   // anti-flood safeguard
    constexpr int MAX_WASH_SEC  = 300;
    constexpr int MAX_GAP_SEC   = 60;

    auto set_ch = [&](int ch, bool on, const char* label) {
        if (drv.controlRelay(ch, on))
            cerr << "  [WARN] " << label << " CH" << ch << " " << (on ? "ON" : "OFF")
                 << " readback mismatch — check LED physically\n";
        else
            cout << "  [OK] " << label << " CH" << ch << " " << (on ? "ON" : "OFF") << "\n";
    };

    auto shutdown_all = [&]() {
        cout << "[CLEANUP] CH5/6/7 all OFF\n";
        drv.controlRelay(CH_BRUSH, false);
        drv.controlRelay(CH_PUMP,  false);
        drv.controlRelay(CH_VALVE, false);
    };

    cout << "\nChannel map:\n"
         << "  CH5: 刷洗滾筒馬達 (brush)\n"
         << "  CH6: 水箱泵浦 (pump)\n"
         << "  CH7: 水箱進水球閥 (inlet valve)\n"
         << "\nNote: Modbus echo check is unreliable on some PQW firmware —\n"
         << "      always verify via physical LED / water flow / motor sound.\n";

    while (true) {
        cout << "\n模式 [1=手動 toggle  2=補水(timed)  3=刷洗  4=完整循環(sensor補水→閒置→刷洗)  q=離開]: ";
        string mode; getline(cin, mode);
        if (mode == "q" || mode == "Q" || mode.empty()) break;

        if (mode == "1") {
            bool st_b = false, st_p = false, st_v = false;
            cout << "  [手動] input: b on|off | p on|off | v on|off | a=all ON | o=all OFF | s=state | q=back\n";
            while (true) {
                cout << "  > ";
                string in; getline(cin, in);
                if (in == "q" || in.empty()) break;
                if (in == "a") {
                    set_ch(CH_BRUSH, true, "brush");
                    set_ch(CH_PUMP,  true, "pump ");
                    set_ch(CH_VALVE, true, "valve");
                    st_b = st_p = st_v = true;
                    continue;
                }
                if (in == "o") {
                    set_ch(CH_BRUSH, false, "brush");
                    set_ch(CH_PUMP,  false, "pump ");
                    set_ch(CH_VALVE, false, "valve");
                    st_b = st_p = st_v = false;
                    continue;
                }
                if (in == "s") {
                    cout << "    brush=" << (st_b ? "ON " : "OFF")
                         << "  pump="  << (st_p ? "ON " : "OFF")
                         << "  valve=" << (st_v ? "ON " : "OFF") << "\n";
                    continue;
                }
                istringstream iss(in);
                string which, verb; iss >> which >> verb;
                bool want_on;
                if      (verb == "on")  want_on = true;
                else if (verb == "off") want_on = false;
                else { cout << "    [!] usage: b|p|v on|off\n"; continue; }
                if      (which == "b") { set_ch(CH_BRUSH, want_on, "brush"); st_b = want_on; }
                else if (which == "p") { set_ch(CH_PUMP,  want_on, "pump "); st_p = want_on; }
                else if (which == "v") { set_ch(CH_VALVE, want_on, "valve"); st_v = want_on; }
                else cout << "    [!] unknown target '" << which << "' — use b/p/v\n";
            }
            shutdown_all();
            continue;
        }

        if (mode == "2") {
            cout << "  補水秒數 (1~" << MAX_VALVE_SEC << ") [30]: ";
            string t; getline(cin, t);
            int secs = t.empty() ? 30 : stoi(t);
            if (secs <= 0 || secs > MAX_VALVE_SEC) {
                cout << "    [!] 秒數超過範圍\n"; continue;
            }
            cout << "  → 進水閥 ON " << secs << " 秒 (Enter 提前中止)\n";
            set_ch(CH_VALVE, true, "valve");
            bool aborted = water_wait_or_abort(secs);
            set_ch(CH_VALVE, false, "valve");
            cout << (aborted ? "  [ABORT] 使用者中止\n" : "  [DONE] 補水完成\n");
            continue;
        }

        if (mode == "3") {
            cout << "  刷洗秒數 (1~" << MAX_WASH_SEC << ") [20]: ";
            string t; getline(cin, t);
            int secs = t.empty() ? 20 : stoi(t);
            if (secs <= 0 || secs > MAX_WASH_SEC) {
                cout << "    [!] 秒數超過範圍\n"; continue;
            }
            cout << "  → 水泵 + 刷子 ON " << secs << " 秒 (Enter 提前中止)\n";
            set_ch(CH_PUMP,  true, "pump ");
            set_ch(CH_BRUSH, true, "brush");
            bool aborted = water_wait_or_abort(secs);
            set_ch(CH_BRUSH, false, "brush");
            set_ch(CH_PUMP,  false, "pump ");
            cout << (aborted ? "  [ABORT] 使用者中止\n" : "  [DONE] 刷洗完成\n");
            continue;
        }

        if (mode == "4") {
            cout << "  水位 sensor slave ID (XKC-Y25-RS485) [13]: ";
            string ssid; getline(cin, ssid);
            int sensor_id = ssid.empty() ? 13 : stoi(ssid);

            cout << "  補水 timeout 上限 (1~" << MAX_VALVE_SEC << ") [60]: ";
            string t1; getline(cin, t1);
            int fill_s = t1.empty() ? 60 : stoi(t1);
            if (fill_s <= 0 || fill_s > MAX_VALVE_SEC) { cout << "    [!] 超過範圍\n"; continue; }

            cout << "  閒置秒數 (0~" << MAX_GAP_SEC << ") [5]: ";
            string tg; getline(cin, tg);
            int gap_s = tg.empty() ? 5 : stoi(tg);
            if (gap_s < 0 || gap_s > MAX_GAP_SEC) { cout << "    [!] 超過範圍\n"; continue; }

            cout << "  刷洗秒數 (1~" << MAX_WASH_SEC << ") [20]: ";
            string t2; getline(cin, t2);
            int wash_s = t2.empty() ? 20 : stoi(t2);
            if (wash_s <= 0 || wash_s > MAX_WASH_SEC) { cout << "    [!] 超過範圍\n"; continue; }

            // Try to init water level sensor sharing this menu's TCP client.
            // If init fails, fall back to timed valve open (legacy behavior).
            XKC_Y25_RS485 lvl;
            bool sensor_ok = !lvl.init(cli, sensor_id, true);
            if (!sensor_ok)
                cerr << "  [WARN] XKC sensor init failed (slave " << sensor_id
                     << ") — falling back to TIMED fill ("
                     << fill_s << "s)\n";

            cout << "  → 階段 1/3：進水閥 ON";
            if (sensor_ok)
                cout << " — 等水位 sensor 偵測滿位 (timeout " << fill_s << "s)\n";
            else
                cout << " " << fill_s << "s (timed fallback)\n";
            set_ch(CH_VALVE, true, "valve");

            bool a = false;
            if (sensor_ok) {
                // Sensor-driven fill: poll output every 200ms until = 1, with timeout + abort support.
                auto start = std::chrono::steady_clock::now();
                int total_ms = fill_s * 1000;
                uint16_t out = 0, rssi = 0;
                bool full = false, aborted = false, timeout = false;

                while (true) {
                    auto now = std::chrono::steady_clock::now();
                    int elapsed_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
                    if (elapsed_ms >= total_ms) { timeout = true; break; }

                    // Read sensor — keep last good values on transient comms err
                    if (!lvl.read_state(out, rssi)) {
                        int remain_s = (total_ms - elapsed_ms + 999) / 1000;
                        cout << "\r  [水位] output=" << out << " RSSI=" << setw(5) << rssi
                             << " 剩 " << setw(4) << remain_s << "s (Enter 中止)   " << flush;
                        if (out == 1) { full = true; break; }
                    } else {
                        cout << "\r  [水位] read err — retry...                    " << flush;
                    }

                    // Abort polling — non-blocking stdin check (matches water_wait_or_abort)
#ifdef _WIN32
                    if (_kbhit()) {
                        while (_kbhit()) (void)_getch();
                        aborted = true; break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
#else
                    fd_set rf; FD_ZERO(&rf); FD_SET(STDIN_FILENO, &rf);
                    struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 200 * 1000;
                    int n = select(STDIN_FILENO + 1, &rf, nullptr, nullptr, &tv);
                    if (n > 0 && FD_ISSET(STDIN_FILENO, &rf)) {
                        string junk; getline(cin, junk);
                        aborted = true; break;
                    }
#endif
                }
                cout << "\r" << string(70, ' ') << "\r" << flush;

                set_ch(CH_VALVE, false, "valve");

                if (aborted) { cout << "  [ABORT] 補水階段中止 (output=" << out << " RSSI=" << rssi << ")\n"; continue; }
                if (timeout) { cout << "  [TIMEOUT] " << fill_s << "s 內水位未達滿位 (output=" << out << " RSSI=" << rssi << ")\n"; continue; }
                if (full)    { cout << "  [DONE] 水位達滿 — output=" << out << " RSSI=" << rssi << "\n"; }
            } else {
                // Fallback: legacy timed fill
                a = water_wait_or_abort(fill_s);
                set_ch(CH_VALVE, false, "valve");
                if (a) { cout << "  [ABORT] 補水階段中止\n"; continue; }
            }

            if (gap_s > 0) {
                cout << "  → 階段 2/3：閒置 " << gap_s << "s\n";
                a = water_wait_or_abort(gap_s);
                if (a) { cout << "  [ABORT] 閒置階段中止\n"; continue; }
            }

            cout << "  → 階段 3/3：水泵 + 刷子 ON " << wash_s << "s\n";
            set_ch(CH_PUMP,  true, "pump ");
            set_ch(CH_BRUSH, true, "brush");
            a = water_wait_or_abort(wash_s);
            set_ch(CH_BRUSH, false, "brush");
            set_ch(CH_PUMP,  false, "pump ");
            cout << (a ? "  [ABORT] 刷洗階段中止\n" : "  [DONE] 完整循環完成\n");
            continue;
        }

        cout << "  [!] unknown mode '" << mode << "'\n";
    }

    shutdown_all();
    cli.close();
}


//=========== 14. DM2J live position monitor ===========
static void test_dm2j_monitor() {
    cout << "\n--- DM2J live position + status monitor ---\n";
    string ip;
    cout << "Gateway IP [192.168.1.20]: ";
    getline(cin, ip);
    if (ip.empty()) ip = "192.168.1.20";

    cout << "Slave ID [2]: ";
    string s; getline(cin, s);
    int slave = s.empty() ? 2 : stoi(s);

    if (!quick_tcp_probe(ip, 4001)) {
        cerr << "[ERR] " << ip << ":4001 unreachable (2s timeout)\n"; return;
    }
    TCP_client cli;
    if (!cli.connectToServer(ip, 4001, false)) {
        cerr << "[ERR] Cannot connect to " << ip << ":4001\n"; return;
    }

    DM2J_RS570 drv;
    // debug=false: suppress per-poll TX/RX hex dump so the live line stays readable
    if (drv.init(cli, slave, false)) {
        cerr << "[ERR] DM2J slave " << slave << " init fail\n";
        cli.close(); return;
    }

    cout << "[DM2J:" << slave << "] Monitoring... press Enter to stop\n";
    atomic<bool> stop_flag(false);
    thread input_thread([&]() {
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        stop_flag = true;
    });

    while (!stop_flag.load()) {
        double pos_cm = 0;
        bool pos_err = drv.read_position_cm(pos_cm);

        uint32_t st = 0;
        bool st_err = drv.read_status(st);

        // Decode status flag string (same bits as print_status)
        string flags;
        if (!st_err) {
            if (st & 0x0001)   flags += "[FAULT] ";
            if (st & 0x0002)   flags += "[ENABLE] ";
            if (st & 0x0004)   flags += "[RUN] ";
            if (st & 0x0010)   flags += "[CMD_DONE] ";
            if (st & 0x0020)   flags += "[PATH_DONE] ";
            if (st & 0x0040)   flags += "[HOME_DONE] ";
            if (flags.empty()) flags = "[--]";
        }

        ostringstream line;
        line << "\r[DM2J:" << slave << "] ";
        if (pos_err) line << "pos=ERR       ";
        else         line << "pos=" << setw(8) << setprecision(3) << fixed << pos_cm << " cm ";
        line << "| ";
        if (st_err) line << "status=ERR";
        else        line << "status=0x" << setw(8) << setfill('0') << hex << uppercase << st
                         << dec << setfill(' ') << " " << flags;
        line << "          ";   // pad to overwrite previous longer line
        cout << line.str();
        cout.flush();

        this_thread::sleep_for(chrono::milliseconds(200));
    }
    cout << "\n";
    if (input_thread.joinable()) input_thread.join();
    cli.close();
}


//=========== 15. DM2J zero current position ===========
static void test_dm2j_zero() {
    cout << "\n--- DM2J set current position as zero ---\n";
    string ip;
    cout << "Gateway IP [192.168.1.20]: ";
    getline(cin, ip);
    if (ip.empty()) ip = "192.168.1.20";

    cout << "Slave ID [1]: ";
    string s; getline(cin, s);
    int slave = s.empty() ? 1 : stoi(s);

    if (!quick_tcp_probe(ip, 4001)) {
        cerr << "[ERR] " << ip << ":4001 unreachable (2s timeout)\n"; return;
    }
    TCP_client cli;
    if (!cli.connectToServer(ip, 4001, false)) {
        cerr << "[ERR] Cannot connect to " << ip << ":4001\n"; return;
    }

    DM2J_RS570 drv;
    if (drv.init(cli, slave, true)) {
        cerr << "[ERR] DM2J slave " << slave << " init fail\n";
        cli.close(); return;
    }

    double pos_before = 0;
    if (drv.read_position_cm(pos_before))
        cout << "  [WARN] read position failed — proceeding anyway\n";
    else
        cout << "  current position: " << fixed << setprecision(3) << pos_before << " cm\n";

    cout << "  This will set the CURRENT shaft position as the new zero.\n"
         << "  Type 'yes' to confirm, anything else to cancel: ";
    string c; getline(cin, c);
    if (c != "yes") {
        cout << "  [CANCEL] aborted, no change made\n";
        cli.close();
        return;
    }

    drv.home_set_current_pos_zero();
    cout << "  [SENT] home_set_current_pos_zero (0x6002 = 0x0021)\n";

    // Give the drive a moment to latch the new zero before read-back
    this_thread::sleep_for(chrono::milliseconds(200));

    double pos_after = 0;
    if (drv.read_position_cm(pos_after))
        cout << "  [WARN] read-back failed — verify manually via menu 14\n";
    else
        cout << "  position after zero: " << fixed << setprecision(3) << pos_after << " cm\n";

    cli.close();
}


//=========== 16. DM2J group sync move (feet 1,3 / wheels 2,4) ===========
//
// 某些機構把 slave 1,3（左右腳）或 2,4（左右輪）剛性連接，必須硬體同步移動，
// 否則會扭壞機構。做法：
//   - PR1 專給 {1,3} 用 / PR2 專給 {2,4} 用
//   - 所有在 bus 上的 slave 啟動時把 PR1 / PR2 都設成 rpm=0 (safe no-op)
//   - 只在目標 slave 刷新目標 PR，其他 slave 的對應 PR 保持 rpm=0
//   - 廣播 PR_trigger_sync 時，非目標 slave 執行 rpm=0 → 不動
//   - 動完後把目標 PR 重置回 safe，避免下一次廣播殘留
//
// 涵蓋的 slave: 1,2,3,4,5（DM2J 全部）— washrobot 架構裡 bus 上只有這 5 顆
static void test_dm2j_group_sync() {
    cout << "\n--- DM2J group sync move (feet 1,3 / wheels 2,4) ---\n";
    string ip;
    cout << "Gateway IP [192.168.1.20]: ";
    getline(cin, ip);
    if (ip.empty()) ip = "192.168.1.20";

    if (!quick_tcp_probe(ip, 4001)) {
        cerr << "[ERR] " << ip << ":4001 unreachable (2s timeout)\n"; return;
    }
    TCP_client cli;
    if (!cli.connectToServer(ip, 4001, false)) {
        cerr << "[ERR] Cannot connect to " << ip << ":4001\n"; return;
    }

    // Init all 5 DM2J slaves sharing one TCP connection.
    // debug=false to avoid log flood during safe-state writes (~10 PR sets × hex dump).
    DM2J_RS570 drv[6];   // index 1..5
    for (int s = 1; s <= 5; ++s) {
        if (drv[s].init(cli, s, false)) {
            cerr << "[ERR] DM2J slave " << s << " init fail\n";
            cli.close(); return;
        }
    }

    // Initialize safe PR state on ALL 5 slaves: PR1 and PR2 both rpm=0
    cout << "  → init safe PR state (PR1, PR2 = rpm=0 on all 5 slaves)\n";
    for (int s = 1; s <= 5; ++s) {
        drv[s].PR_move_set(1, 1 /*absolute*/, 0 /*rpm=0*/, 0, 50, 100);
        drv[s].PR_move_set(2, 1,              0,           0, 50, 100);
    }

    auto wait_done = [](DM2J_RS570& d, int timeout_ms) -> bool {
        for (int e = 0; e < timeout_ms; e += 100) {
            uint32_t st = 0;
            if (d.read_status(st)) return true;                      // comms err
            if (st & 0x0001)        return true;                      // fault
            if ((st & 0x0010) && (st & 0x0020)) return false;        // done
            this_thread::sleep_for(chrono::milliseconds(100));
        }
        return true;   // timeout
    };

    // Interactive loop — one move per iteration
    while (true) {
        cout << "\nGroup [f=feet 1,3 / w=wheels 2,4 / q=quit]: ";
        string g; getline(cin, g);
        if (g == "q" || g.empty()) break;

        std::vector<int> targets;
        int pr_slot = 0;
        if (g == "f")      { targets = {1, 3}; pr_slot = 1; }
        else if (g == "w") { targets = {2, 4}; pr_slot = 2; }
        else { cout << "  [!] invalid group\n"; continue; }

        cout << "Target cm (absolute) [5]: ";
        string c; getline(cin, c);
        double target_cm = c.empty() ? 5.0 : stod(c);

        cout << "RPM [500]: ";
        string r; getline(cin, r);
        int rpm = r.empty() ? 500 : stoi(r);

        // Enable target motors via user_lib API (Pr0.07 = 1 force enable).
        for (int s : targets) {
            cout << "  → motor_enable slave " << s << "\n";
            if (drv[s].motor_enable())
                cerr << "  [WARN] enable slave " << s << " failed\n";
        }
        this_thread::sleep_for(chrono::milliseconds(300));

        // Set real PR on target slaves (other slaves keep rpm=0)
        cout << "  → set PR" << pr_slot << " real move on target slaves\n";
        for (int s : targets) {
            if (drv[s].PR_move_cm_set(pr_slot, 1 /*absolute*/, rpm, target_cm, 50, 100))
                cerr << "  [WARN] set PR on slave " << s << " failed\n";
        }

        // Broadcast sync trigger — non-target slaves execute their rpm=0 PR (no motion)
        cout << "  → broadcast PR_trigger_sync(" << pr_slot << ")\n";
        drv[targets[0]].PR_trigger_sync(pr_slot);

        // Wait both targets done
        cout << "  → waiting for completion (10s timeout)...\n";
        bool any_fail = false;
        for (int s : targets) {
            if (wait_done(drv[s], 10000)) {
                cerr << "  [WARN] slave " << s << " timeout / fault\n";
                any_fail = true;
            } else {
                cout << "  [OK] slave " << s << " done\n";
            }
        }

        // Reset target PR slot back to safe (prevent stale-trigger next time)
        cout << "  → reset PR" << pr_slot << " to safe on all 5 slaves\n";
        for (int s = 1; s <= 5; ++s) {
            drv[s].PR_move_set(pr_slot, 1, 0, 0, 50, 100);
        }

        // Report final positions
        for (int s : targets) {
            double pos = 0;
            if (!drv[s].read_position_cm(pos))
                cout << "  slave " << s << " final pos: " << fixed << setprecision(3) << pos << " cm\n";
        }

        (void)any_fail;
    }

    // Cleanup: reset all PR slots + disable all motors
    cout << "\n[CLEANUP] reset all PR slots + disable 5 motors\n";
    for (int s = 1; s <= 5; ++s) {
        drv[s].PR_move_set(1, 1, 0, 0, 50, 100);
        drv[s].PR_move_set(2, 1, 0, 0, 50, 100);
        drv[s].motor_disable();
    }

    cli.close();
}


//=========== 17. Emergency cleanup (relays OFF / pushers retract / rails home) ===========
//
// Brings the entire machine to a known-safe parked state, regardless of what
// operation was interrupted. Mirrors the cleanup block at end of option 7 but
// runs as a standalone utility — useful when a test aborted / crashed and left
// valves open, pushers extended, rails mid-travel, etc.
//
// Sequence (order matters — release vacuum BEFORE retracting pushers so cups
// unseal cleanly, then pump off, then retract, then rails, then disable):
//   1. PQW CH2/3/4 OFF (release all suction valves) + 300ms settle
//   2. PQW all channels OFF (pump CH1 + brush/water CH5-7 + spare CH8)
//   3. ZDT 1..9 release_stall + enable → retract to 0 (all 9 pushers)
//   4. ZDT 1..9 disable
//   5. DM2J: bystanders (2,4,5) PR1 = rpm=0 safe
//   6. DM2J 1,3 enable → return to absolute 0 (broadcast sync)
//   7. DM2J 1..5 disable
//
// If a gateway is unreachable its layer is skipped with a [WARN].
static void test_cleanup() {
    cout << "\n--- Emergency cleanup (all relays OFF / pushers retract / rails home) ---\n";
    string ip20, ip21, ip22;
    cout << "DM2J gateway IP  [192.168.1.20]: ";  getline(cin, ip20);  if (ip20.empty()) ip20 = "192.168.1.20";
    cout << "ZDT gateway IP   [192.168.1.21]: ";  getline(cin, ip21);  if (ip21.empty()) ip21 = "192.168.1.21";
    cout << "JC/PQW gateway IP[192.168.1.22]: ";  getline(cin, ip22);  if (ip22.empty()) ip22 = "192.168.1.22";

    bool reach20 = quick_tcp_probe(ip20, 4001);
    bool reach21 = quick_tcp_probe(ip21, 4001);
    bool reach22 = quick_tcp_probe(ip22, 4001);
    if (!reach20) cerr << "  [WARN] " << ip20 << " unreachable — DM2J cleanup skipped\n";
    if (!reach21) cerr << "  [WARN] " << ip21 << " unreachable — ZDT cleanup skipped\n";
    if (!reach22) cerr << "  [WARN] " << ip22 << " unreachable — PQW cleanup skipped\n";

    TCP_client cli20, cli21, cli22;
    if (reach22) cli22.connectToServer(ip22, 4001, false);
    if (reach21) cli21.connectToServer(ip21, 4001, false);
    if (reach20) cli20.connectToServer(ip20, 4001, false);

    cout << "\n======== CLEANUP ========\n";

    // --- Step 1+2: PQW relays (release vacuum FIRST, then all off) ---
    // Use per-channel controlRelay (function 0x05) rather than controlAll — the
    // latter only checks echo length, and some PQW firmware revisions don't
    // honour the special "all" register at 0x0085 (observed 2026-04-21).
    if (reach22) {
        PQW_IO_16O_RLY pqw;
        if (!pqw.init(cli22, PQW_SLAVE, 8, false)) {
            cout << "  → release valves (CH" << PQW_CH_VALVE_FEET
                 << "/" << PQW_CH_VALVE_BODY
                 << "/" << PQW_CH_VALVE_CENTER << " OFF)\n";
            pqw.controlRelay(PQW_CH_VALVE_FEET,   false);
            pqw.controlRelay(PQW_CH_VALVE_BODY,   false);
            pqw.controlRelay(PQW_CH_VALVE_CENTER, false);
            this_thread::sleep_for(chrono::milliseconds(300));   // let cups unseal

            cout << "  → pump CH1 OFF + all other relays OFF (per-channel writes)\n";
            for (int ch = 1; ch <= 8; ++ch) {
                pqw.controlRelay(ch, false);
            }
            // Belt-and-suspenders: also try the bulk all-off
            pqw.controlAll(false);
        } else {
            cerr << "  [WARN] PQW init fail — skip relay cleanup\n";
        }
    }

    // --- Step 3+4: ZDT pushers 1..9 retract ---
    if (reach21) {
        ZDT_motor_control zdts[10];
        bool zok = true;
        for (int i = 1; i <= 9; ++i) {
            if (zdts[i].init(cli21, i, false)) {
                cerr << "  [WARN] ZDT slave " << i << " init fail\n";
                zok = false;
            }
        }
        if (zok) {
            cout << "  → release any ZDT stall flags + enable drivers\n";
            for (int i = 1; i <= 9; ++i) {
                zdts[i].release_stall_flag();
                zdts[i].motion_control_driver_EN(true);
            }
            this_thread::sleep_for(chrono::milliseconds(200));

            std::vector<int> all_zdt = {1,2,3,4,5,6,7,8,9};
            cout << "  → retract all 9 ZDT pushers to 0\n";
            zdt_group_move_sync(zdts, all_zdt, PUSHER_RETRACT_PULSE);

            cout << "  → disable all ZDT drivers\n";
            for (int i = 1; i <= 9; ++i) zdts[i].motion_control_driver_EN(false);
        }
    }

    // --- Step 5-7: DM2J rails home + bystanders safe + disable ---
    if (reach20) {
        DM2J_RS570 dm2j_L, dm2j_R;
        bool dm_ok = !dm2j_L.init(cli20, DM2J_LEFT_RAIL,  false)
                  && !dm2j_R.init(cli20, DM2J_RIGHT_RAIL, false);
        if (dm_ok) {
            // Bystanders: safe PR1 so sync broadcast doesn't drive wheels/arm
            DM2J_RS570 bL, bR, arm;
            bL.init(cli20, 2, false);
            bR.init(cli20, 4, false);
            arm.init(cli20, 5, false);
            std::vector<DM2J_RS570*> bystanders = {&bL, &bR, &arm};
            cout << "  → bystander PR1 set to rpm=0 (wheels 2,4 / arm 5)\n";
            dm2j_set_safe_pr(bystanders, 1);

            cout << "  → enable DM2J rails 1,3\n";
            dm2j_L.motor_enable();
            dm2j_R.motor_enable();
            this_thread::sleep_for(chrono::milliseconds(300));

            cout << "  → return DM2J rails (1,3) to absolute 0\n";
            if (dm2j_pair_rail_move_abs_sync(dm2j_L, dm2j_R, 1, 0.0)) {
                cerr << "  [WARN] rail return-to-0 failed — position may not be 0\n";
            }

            cout << "  → disable DM2J motors 1..5\n";
            dm2j_L.motor_disable();   // slave 1
            bL.motor_disable();       // slave 2
            dm2j_R.motor_disable();   // slave 3
            bR.motor_disable();       // slave 4
            arm.motor_disable();      // slave 5
        } else {
            cerr << "  [WARN] DM2J rail init fail — skip rail cleanup\n";
        }
    }

    cout << "\n======== CLEANUP DONE ========\n";

    if (reach20) cli20.close();
    if (reach21) cli21.close();
    if (reach22) cli22.close();
}


//=========== 18. XKC-Y25-RS485 water level sensor config ===========
//
// 連線到既有 RS485 gateway，操作水位 sensor：
//   r  連續讀（output + RSSI，Enter 停止）
//   s  單次讀
//   i  改 slave ID（會發 0x06 寫 reg 0x0004，sensor LED 應閃爍代表成功；
//      改完之後 sensor 用新 ID 回應，自己要重新進 menu 用新 ID 連）
//   f  出廠還原（broadcast 0xFF，sensor 重置為 slave=1 / 9600 baud）
//   q  退出
//
// 警告：
//   - 共用 RS485 bus 上的 baud rate 必須跟 gateway 一致。本 menu 不開放改 baud
//     避免拆掉整條 bus。需要改 baud 請手動操作 gateway 設定後再改 sensor。
//   - 出廠還原是 broadcast，bus 上所有 sensor 都會收到。XKC_Y25 sensor 會吃這個
//     指令、其他型號 sensor 多半會 ignore（reg 0x0004 對應的功能不同）。
//      但仍建議在僅接 XKC sensor 的 bus 操作。
static void test_xkc_y25() {
    cout << "\n--- XKC-Y25-RS485 water level sensor ---\n";
    string ip;
    cout << "Gateway IP [192.168.1.22]: ";
    getline(cin, ip);
    if (ip.empty()) ip = "192.168.1.22";

    cout << "Slave ID [13]: ";
    string sid; getline(cin, sid);
    int slave = sid.empty() ? 13 : stoi(sid);

    if (!quick_tcp_probe(ip, 4001)) {
        cerr << "[ERR] " << ip << ":4001 unreachable (2s timeout)\n"; return;
    }
    TCP_client cli;
    if (!cli.connectToServer(ip, 4001, false)) {
        cerr << "[ERR] Cannot connect to " << ip << ":4001\n"; return;
    }

    XKC_Y25_RS485 lvl;
    if (lvl.init(cli, slave, true)) {
        cerr << "[ERR] XKC sensor init fail (slave " << slave << ")\n";
        cli.close(); return;
    }

    cout << "\nConnected. Actions: r=live read | s=single read | i=set ID | f=factory reset | q=quit\n";

    while (true) {
        cout << "\n[XKC:" << slave << "] action: ";
        string a; getline(cin, a);
        if (a.empty() || a == "q" || a == "Q") break;

        if (a == "s") {
            uint16_t out = 0, rssi = 0;
            if (lvl.read_state(out, rssi)) {
                cerr << "  [ERR] read fail (cached output=" << lvl.last_output()
                     << " rssi=" << lvl.last_rssi() << ")\n";
            } else {
                cout << "  output=" << out << " (" << (out ? "有水" : "無水")
                     << ")  RSSI=" << rssi
                     << "  (參考門檻：< 3900 無 / > 4100 有)\n";
            }
            continue;
        }

        if (a == "r") {
            cout << "  → live read every 200ms (Enter to stop)\n";
            atomic<bool> stop_flag(false);
            thread input_thread([&]() {
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                stop_flag = true;
            });
            while (!stop_flag.load()) {
                uint16_t out = 0, rssi = 0;
                if (lvl.read_state(out, rssi)) {
                    cout << "\r  output=ERR  RSSI=ERR              " << flush;
                } else {
                    cout << "\r  output=" << out << " (" << (out ? "有水" : "無水")
                         << ")  RSSI=" << setw(5) << rssi << "       " << flush;
                }
                this_thread::sleep_for(chrono::milliseconds(200));
            }
            cout << "\n";
            if (input_thread.joinable()) input_thread.join();
            continue;
        }

        if (a == "i") {
            cout << "  new slave ID (1~254): ";
            string n; getline(cin, n);
            if (n.empty()) { cout << "  [CANCEL] empty input\n"; continue; }
            int new_id = stoi(n);
            if (new_id < 1 || new_id > 254) { cout << "  [!] out of range\n"; continue; }
            cout << "  → set slave " << slave << " → " << new_id << "?  type 'yes' to confirm: ";
            string c; getline(cin, c);
            if (c != "yes") { cout << "  [CANCEL]\n"; continue; }

            if (lvl.set_address((uint8_t)new_id)) {
                cerr << "  [ERR] set_address failed — sensor may not have changed\n";
            } else {
                cout << "  [OK] address change frame sent (LED 應閃爍代表成功)\n"
                     << "       sensor 現在以 ID " << new_id << " 回應，本 menu 將退出讓你重連。\n";
                break;   // exit and let user re-enter at new ID
            }
            continue;
        }

        if (a == "f") {
            cout << "  → 出廠還原（broadcast FF，重置為 slave=1 / 9600 baud）\n"
                 << "    type 'reset' to confirm: ";
            string c; getline(cin, c);
            if (c != "reset") { cout << "  [CANCEL]\n"; continue; }

            // Raw broadcast frame per manual §2.0:  FF 06 00 04 00 02 5C 14
            // Bypass the driver since slaveID is fixed in the class instance.
            uint8_t tx[8] = { 0xFF, 0x06, 0x00, 0x04, 0x00, 0x02, 0x5C, 0x14 };
            if (!cli.sendData((const char*)tx, 8, 500)) {
                cerr << "  [ERR] broadcast send fail\n"; continue;
            }
            cout << "  [SENT] broadcast factory reset — sensor LED 應閃 2 下\n"
                 << "    確認動作後，sensor 以 slave=1 / 9600 回應，請退出 menu 重連\n";
            break;
        }

        cout << "  [!] unknown action — use r / s / i / f / q\n";
    }

    cli.close();
}


//=========== main: menu loop ===========
static void print_menu() {
    cout << "\n========== Linux_test ==========\n"
         << "  1  IMU (WT901BC)   — serial port + live Roll/Pitch/Yaw\n"
         << "  2  DM2J step motor — IP + slave + move cm\n"
         << "  3  ZDT SMC pusher  — IP + slave + target pulse\n"
         << "  4  JC-100 pressure — IP + slave + live kPa read\n"
         << "  5  PQW 8CH relay   — IP + slave + channel on/off\n"
         << "  6  ZDT group       — 1~9 with skip list, unified target pulse\n"
         << "  7  Full step seq   — 8 pushers staged 7/10cm + rail + vacuum + retry grip\n"
         << "  8  Step no-rail    — pushers + vacuum only (report, no verify)\n"
         << "  9  SD76 meter      — length meter live read + reset/pause/resume\n"
         << " 10  ZS_DIO winch    — crane winch relay (main/easy crane)\n"
         << " 11  Step no-rail +v — pushers + vacuum with verify + retry grip\n"
         << " 12  Full step report — rail + pushers + vacuum (report only, no verify)\n"
         << " 13  Water tank     — PQW CH5/6/7 手動 + 補水/刷洗/循環腳本\n"
         << " 14  DM2J monitor   — live position + status bits (read-only)\n"
         << " 15  DM2J zero pos  — set current shaft position as new zero (confirm)\n"
         << " 16  DM2J group sync — feet (1,3) or wheels (2,4) hardware-sync move\n"
         << " 17  Emergency cleanup — all relays OFF / pushers retract / rails home\n"
         << " 18  XKC water sensor — XKC-Y25-RS485 read / set ID / factory reset\n"
         << " 19  ZDT positions   — read all 9 ZDT pushers (deg + cm estimate)\n"
         << "  q  Quit\n"
         << "================================\n"
         << "Select: ";
}

int main() {
    while (true) {
        print_menu();
        string line;
        if (!getline(cin, line)) break;   // EOF / ctrl-D

        if (line == "q" || line == "Q") break;
        if      (line == "1") test_imu();
        else if (line == "2") test_dm2j();
        else if (line == "3") test_zdt();
        else if (line == "4") test_jc100();
        else if (line == "5") test_pqw();
        else if (line == "6") test_zdt_group();
        else if (line == "7") test_full_step();
        else if (line == "8") test_full_step_no_rail();
        else if (line == "9") test_sd76();
        else if (line == "10") test_zsdio();
        else if (line == "11") test_full_step_no_rail_verify();
        else if (line == "12") test_full_step_report();
        else if (line == "13") test_water_tank();
        else if (line == "14") test_dm2j_monitor();
        else if (line == "15") test_dm2j_zero();
        else if (line == "16") test_dm2j_group_sync();
        else if (line == "17") test_cleanup();
        else if (line == "18") test_xkc_y25();
        else if (line == "19") test_zdt_positions();
        else if (line.empty()) continue;
        else cout << "[!] unknown selection '" << line << "'\n";
    }
    cout << "bye\n";
    return 0;
}
