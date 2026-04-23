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

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
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
static constexpr int PUSHER_EXTEND_PULSE  = 144000;
static constexpr int PUSHER_RETRACT_PULSE = 0;
static constexpr int PUSHER_RPM           = 1000;
static constexpr int PUSHER_ACC           = 255;
// Small back-off pulse count for vacuum-failure re-grip (slight retract before re-extend)
static constexpr int PUSHER_BACKOFF_PULSE = 120000;    // ~16mm back off from full extend

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
static constexpr int DM2J_RPM        = 500;
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

    cout << "Move cm [5]: ";
    getline(cin, s);
    double cm = s.empty() ? 5.0 : stod(s);

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

    cout << "  → move " << cm << " cm (absolute)\n";
    drv.PR_move_cm(0, 1, 500, cm, 50, 100);
    cout << "  [OK] move command sent\n";

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
        if (drvs[s].init(cli, s, true)) {
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
        // Broadcast has no reply; driver returns non-zero if send itself failed.
        if (drvs[trig].trigger_sync_move())
            cerr << "  [WARN] trigger_sync_move send failure reported\n";
    }

    // Phase 2: unified poll loop with per-slave stall auto-recovery
    constexpr int MAX_STALL_RETRIES      = 3;
    constexpr int POLL_INTERVAL_MS       = 200;
    constexpr int OVERALL_TIMEOUT_MS     = 15000;

    auto t0 = chrono::steady_clock::now();
    while (true) {
        this_thread::sleep_for(chrono::milliseconds(POLL_INTERVAL_MS));

        bool all_settled = true;
        for (int s : actives) {
            if (done[s] || aborted[s]) continue;
            all_settled = false;

            if (drvs[s].get_system_status()) continue;   // comms hiccup; retry next poll

            if (drvs[s].status.pos_reached) {
                done[s] = true;
                cout << "  [OK] slave " << s << " reached\n";
                continue;
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


//=========== 7. Full step sequence (8 pushers + rail + vacuum + retry) ===========
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
//   Phase B (Body + Center):
//     release body+center valves → retract body+center pushers → rail -step_cm →
//     extend body+center pushers → engage body+center valves → settle →
//     verify JC-100 5,6,7,8,9 → retry (rail back-off +5cm) if needed
//
// Rail moves are RELATIVE (mode=0) so one full step returns rail to starting pos.
// ============================================================

// Queue + sync-trigger + poll-until-done for a ZDT group. Returns true on error.
static bool zdt_group_move_sync(ZDT_motor_control* drvs, const std::vector<int>& slaves,
                                 int target_pulse, int timeout_ms = 10000) {
    if (slaves.empty()) return false;
    // Queue each (release stall, ensure enable, pos_mode with sync=1)
    for (int s : slaves) {
        drvs[s].release_stall_flag();
        this_thread::sleep_for(chrono::milliseconds(30));
        if (drvs[s].motion_control_driver_EN(true)) {
            cerr << "    [ERR] ZDT slave " << s << " enable fail\n"; return true;
        }
        this_thread::sleep_for(chrono::milliseconds(50));
        if (drvs[s].motion_control_pos_mode_nowait(0, PUSHER_ACC, PUSHER_RPM, target_pulse, 1, 1, 1)) {
            cerr << "    [ERR] ZDT slave " << s << " pos queue fail\n"; return true;
        }
    }
    // Broadcast trigger — every queued slave executes simultaneously
    if (drvs[slaves[0]].trigger_sync_move())
        cerr << "    [WARN] trigger_sync_move reported send error\n";

    // Poll until all done or timeout
    std::vector<bool> done(slaves.size(), false);
    auto t0 = chrono::steady_clock::now();
    while (true) {
        this_thread::sleep_for(chrono::milliseconds(200));
        bool all = true;
        for (size_t i = 0; i < slaves.size(); ++i) {
            if (done[i]) continue;
            int s = slaves[i];
            if (drvs[s].get_system_status()) { all = false; continue; }
            if (drvs[s].status.pos_reached) { done[i] = true; continue; }
            if (drvs[s].status.stall_flag) {
                cerr << "    [STALL] slave " << s << " at real_pos=" << drvs[s].status.real_pos << "°\n";
                // Don't auto-recover here — leave to operator; mark as done-with-warning
                done[i] = true;
                continue;
            }
            all = false;
        }
        if (all) return false;
        if (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - t0).count() > timeout_ms) {
            cerr << "    [WARN] group timeout; continuing anyway\n"; return true;
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

// DM2J sync pair move: queue left+right, broadcast trigger, wait for both done.
// Uses PR_move_cm_set() + PR_move_cm_trigger_all() which waits for the calling slave;
// briefly polls the other slave status afterward as a safety check.
static bool dm2j_pair_rail_move(DM2J_RS570& left, DM2J_RS570& right, int pr_num, double cm) {
    cout << "    DM2J " << DM2J_LEFT_RAIL << "+" << DM2J_RIGHT_RAIL
         << " → " << (cm >= 0 ? "+" : "") << cm << " cm (relative)\n";
    left.PR_move_cm_set(pr_num, 0, DM2J_RPM, cm, DM2J_ACC, DM2J_DEC);
    right.PR_move_cm_set(pr_num, 0, DM2J_RPM, cm, DM2J_ACC, DM2J_DEC);
    // trigger_all broadcasts sync trigger and polls left's status till done
    if (left.PR_move_cm_trigger_all(pr_num)) {
        cerr << "    [WARN] left rail move timed out / faulted\n"; return true;
    }
    // Quick confirmation on right
    for (int i = 0; i < 40; ++i) {   // up to 4s
        uint32_t st = 0;
        if (right.read_status(st)) break;
        if ((st & 0x0010) && (st & 0x0020)) return false;   // cmd_done + path_done
        if (st & 0x0001) { cerr << "    [WARN] right rail fault st=0x" << std::hex << st << std::dec << "\n"; return true; }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    cerr << "    [WARN] right rail wait post-trigger timed out\n";
    return true;
}


static void test_full_step() {
    cout << "\n--- Full step sequence (8 pushers + rail + vacuum + retry) ---\n";

    string ip20, ip21, ip22;
    cout << "DM2J gateway IP  [192.168.1.20]: ";  getline(cin, ip20);  if (ip20.empty()) ip20 = "192.168.1.20";
    cout << "ZDT gateway IP   [192.168.1.21]: ";  getline(cin, ip21);  if (ip21.empty()) ip21 = "192.168.1.21";
    cout << "JC/PQW gateway IP[192.168.1.22]: ";  getline(cin, ip22);  if (ip22.empty()) ip22 = "192.168.1.22";

    int step_cm = 10, num_steps = 1, threshold = -300, retry_cnt = 3;
    string s;
    cout << "Step distance cm [10]: ";                        getline(cin, s); if (!s.empty()) step_cm   = stoi(s);
    cout << "Number of steps  [1]: ";                         getline(cin, s); if (!s.empty()) num_steps = stoi(s);
    cout << "Vacuum threshold (0.1 kPa, -300=-30kPa) [-300]: "; getline(cin, s); if (!s.empty()) threshold = stoi(s);
    cout << "Vacuum retry count [3]: ";                       getline(cin, s); if (!s.empty()) retry_cnt = stoi(s);

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
    cout << "⚠ PRE-FLIGHT: Robot must already be attached on wall with ALL vacuum engaged.\n";
    cout << "  This test does NOT perform initial attach.\n";
    cout << "Press Enter to start, 'q' to abort: ";
    getline(cin, s);
    if (s == "q" || s == "Q") { cli20.close(); cli21.close(); cli22.close(); return; }

    // Ensure pump is on (idempotent, fast-exit if already)
    pqw.controlRelay(PQW_CH_PUMP, true);

    // Slave mapping (updated 2026-04-23):
    //   feet  left=3,4 / right=1,2 → all feet = {1,2,3,4}
    //   body  left=6,8 / right=5,7 → all body = {5,6,7,8}
    //   center = 9
    std::vector<int> feet_slaves = {1, 2, 3, 4};
    std::vector<int> body_slaves = {5, 6, 7, 8};
    std::vector<int> center_slaves = {9};
    std::vector<int> body_center_slaves = {5, 6, 7, 8, 9};

    // Rail-backup retry strategy (like WASH_ROBOT feet_backup / body_backup):
    //   Per retry, retract pushers → rail retreats RAIL_BACKUP_CM in opposite direction of
    //   phase forward → re-extend pushers → re-engage valve → re-verify.
    //   This lets the robot try to attach at a slightly different vertical position.
    //   rail_backup_signed_cm: -5 for feet (rail retreats toward body), +5 for body (rail
    //   retreats toward feet). Per attempt it accumulates, so after 3 retries rail is at
    //   target ± (3 × backup).
    static constexpr double RAIL_BACKUP_CM = 5.0;
    auto retry_grip_rail = [&](const std::string& group_name,
                               const std::vector<int>& zdt_group,
                               const std::vector<int>& jc_group,
                               int valve_ch,
                               int extra_valve_ch,
                               double rail_backup_signed_cm) -> bool {
        for (int r = 1; r <= retry_cnt; ++r) {
            cout << "  [RETRY " << r << "/" << retry_cnt << "] re-grip " << group_name
                 << " (rail " << (rail_backup_signed_cm >= 0 ? "+" : "")
                 << rail_backup_signed_cm << "cm)\n";
            pqw.controlRelay(valve_ch, false);
            if (extra_valve_ch) pqw.controlRelay(extra_valve_ch, false);
            this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

            // Retract pushers so they clear wall for rail motion
            zdt_group_move_sync(zdts, zdt_group, PUSHER_RETRACT_PULSE);

            // Rail back off (relative) in the retreat direction for this phase
            dm2j_pair_rail_move(dm2j_L, dm2j_R, 1, rail_backup_signed_cm);

            // Re-extend to wall at new position
            zdt_group_move_sync(zdts, zdt_group, PUSHER_EXTEND_PULSE);

            pqw.controlRelay(valve_ch, true);
            if (extra_valve_ch) pqw.controlRelay(extra_valve_ch, true);
            this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));
            if (vacuum_verify(jcs, jc_group, threshold)) return true;
        }
        return false;
    };

    for (int step = 1; step <= num_steps; ++step) {
        cout << "\n======== STEP " << step << "/" << num_steps << " ========\n";

        // ===== Phase A: Feet =====
        cout << "\n▶ Phase A: Feet\n";
        cout << "  → release feet valve (CH" << PQW_CH_VALVE_FEET << " OFF)\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, false);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

        cout << "  → retract feet pushers (ZDT 1,2,3,4)\n";
        zdt_group_move_sync(zdts, feet_slaves, PUSHER_RETRACT_PULSE);

        cout << "  → rail +" << step_cm << " cm\n";
        dm2j_pair_rail_move(dm2j_L, dm2j_R, 1, (double)step_cm);

        cout << "  → extend feet pushers\n";
        zdt_group_move_sync(zdts, feet_slaves, PUSHER_EXTEND_PULSE);

        cout << "  → engage feet valve (CH" << PQW_CH_VALVE_FEET << " ON) + settle " << VACUUM_SETTLE_MS << "ms\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, true);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → verify JC-100 1,2,3,4 vs threshold " << threshold << "\n";
        if (!vacuum_verify(jcs, feet_slaves, threshold)) {
            // Feet phase forward = rail +cm, so retry backs off = rail -RAIL_BACKUP_CM
            if (!retry_grip_rail("feet", feet_slaves, feet_slaves,
                                 PQW_CH_VALVE_FEET, 0, -RAIL_BACKUP_CM)) {
                cerr << "\n[ABORT] feet vacuum fail after " << retry_cnt << " retries. Stopping.\n";
                break;
            }
        }

        // ===== Phase B: Body + Center =====
        cout << "\n▶ Phase B: Body + Center\n";
        cout << "  → release body+center valves (CH" << PQW_CH_VALVE_BODY
             << " + CH" << PQW_CH_VALVE_CENTER << " OFF)\n";
        pqw.controlRelay(PQW_CH_VALVE_BODY, false);
        pqw.controlRelay(PQW_CH_VALVE_CENTER, false);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

        cout << "  → retract body+center pushers (ZDT 5,6,7,8,9)\n";
        zdt_group_move_sync(zdts, body_center_slaves, PUSHER_RETRACT_PULSE);

        cout << "  → rail -" << step_cm << " cm (body slides relative to feet)\n";
        dm2j_pair_rail_move(dm2j_L, dm2j_R, 1, -(double)step_cm);

        cout << "  → extend body+center pushers\n";
        zdt_group_move_sync(zdts, body_center_slaves, PUSHER_EXTEND_PULSE);

        cout << "  → engage body+center valves + settle " << VACUUM_SETTLE_MS << "ms\n";
        pqw.controlRelay(PQW_CH_VALVE_BODY, true);
        pqw.controlRelay(PQW_CH_VALVE_CENTER, true);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → verify JC-100 5,6,7,8,9 vs threshold " << threshold << "\n";
        if (!vacuum_verify(jcs, body_center_slaves, threshold)) {
            // Body phase forward = rail -cm, so retry backs off = rail +RAIL_BACKUP_CM
            if (!retry_grip_rail("body+center", body_center_slaves, body_center_slaves,
                                 PQW_CH_VALVE_BODY, PQW_CH_VALVE_CENTER, +RAIL_BACKUP_CM)) {
                cerr << "\n[ABORT] body vacuum fail after " << retry_cnt << " retries. Stopping.\n";
                break;
            }
        }

        cout << "\n  ✓ STEP " << step << " complete\n";

        if (step < num_steps) {
            cout << "  press Enter for next step, 'q' to stop here: ";
            getline(cin, s);
            if (s == "q" || s == "Q") break;
        }
    }

    cout << "\n[CLEANUP] disabling ZDT pushers (valves/pump left as-is for safety)\n";
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
    cout << "⚠ PRE-FLIGHT: Robot must already be attached with ALL vacuum engaged.\n";
    cout << "Press Enter to start, 'q' to abort: ";
    getline(cin, s);
    if (s == "q" || s == "Q") { cli21.close(); cli22.close(); return; }

    pqw.controlRelay(PQW_CH_PUMP, true);

    // Slave mapping (updated 2026-04-23):
    //   feet  left=3,4 / right=1,2 → all feet = {1,2,3,4}
    //   body  left=6,8 / right=5,7 → all body = {5,6,7,8}
    //   center = 9
    std::vector<int> feet_slaves        = {1, 2, 3, 4};
    std::vector<int> body_center_slaves = {5, 6, 7, 8, 9};

    auto retry_grip = [&](const std::string& group_name,
                          const std::vector<int>& zdt_group,
                          const std::vector<int>& jc_group,
                          int valve_ch,
                          int extra_valve_ch) -> bool {
        for (int r = 1; r <= retry_cnt; ++r) {
            cout << "  [RETRY " << r << "/" << retry_cnt << "] re-grip " << group_name << "\n";
            pqw.controlRelay(valve_ch, false);
            if (extra_valve_ch) pqw.controlRelay(extra_valve_ch, false);
            this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));
            zdt_group_move_sync(zdts, zdt_group, PUSHER_BACKOFF_PULSE);
            zdt_group_move_sync(zdts, zdt_group, PUSHER_EXTEND_PULSE);
            pqw.controlRelay(valve_ch, true);
            if (extra_valve_ch) pqw.controlRelay(extra_valve_ch, true);
            this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));
            if (vacuum_verify(jcs, jc_group, threshold)) return true;
        }
        return false;
    };

    for (int step = 1; step <= num_steps; ++step) {
        cout << "\n======== STEP " << step << "/" << num_steps << " (no-rail) ========\n";

        // Phase A: Feet
        cout << "\n▶ Phase A: Feet (in place)\n";
        cout << "  → release feet valve (CH" << PQW_CH_VALVE_FEET << " OFF)\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, false);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

        cout << "  → retract feet pushers (ZDT 1,2,3,4)\n";
        zdt_group_move_sync(zdts, feet_slaves, PUSHER_RETRACT_PULSE);

        cout << "  → extend feet pushers (back to wall)\n";
        zdt_group_move_sync(zdts, feet_slaves, PUSHER_EXTEND_PULSE);

        cout << "  → engage feet valve + settle " << VACUUM_SETTLE_MS << "ms\n";
        pqw.controlRelay(PQW_CH_VALVE_FEET, true);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → verify JC-100 1,2,3,4 vs threshold " << threshold << "\n";
        if (!vacuum_verify(jcs, feet_slaves, threshold)) {
            if (!retry_grip("feet", feet_slaves, feet_slaves, PQW_CH_VALVE_FEET, 0)) {
                cerr << "\n[ABORT] feet vacuum fail after " << retry_cnt << " retries\n"; break;
            }
        }

        // Phase B: Body + Center
        cout << "\n▶ Phase B: Body + Center (in place)\n";
        cout << "  → release body+center valves (CH" << PQW_CH_VALVE_BODY
             << " + CH" << PQW_CH_VALVE_CENTER << " OFF)\n";
        pqw.controlRelay(PQW_CH_VALVE_BODY, false);
        pqw.controlRelay(PQW_CH_VALVE_CENTER, false);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_RELEASE_MS));

        cout << "  → retract body+center pushers (ZDT 5,6,7,8,9)\n";
        zdt_group_move_sync(zdts, body_center_slaves, PUSHER_RETRACT_PULSE);

        cout << "  → extend body+center pushers (back to wall)\n";
        zdt_group_move_sync(zdts, body_center_slaves, PUSHER_EXTEND_PULSE);

        cout << "  → engage body+center valves + settle " << VACUUM_SETTLE_MS << "ms\n";
        pqw.controlRelay(PQW_CH_VALVE_BODY, true);
        pqw.controlRelay(PQW_CH_VALVE_CENTER, true);
        this_thread::sleep_for(chrono::milliseconds(VACUUM_SETTLE_MS));

        cout << "  → verify JC-100 5,6,7,8,9 vs threshold " << threshold << "\n";
        if (!vacuum_verify(jcs, body_center_slaves, threshold)) {
            if (!retry_grip("body+center", body_center_slaves, body_center_slaves,
                            PQW_CH_VALVE_BODY, PQW_CH_VALVE_CENTER)) {
                cerr << "\n[ABORT] body vacuum fail after " << retry_cnt << " retries\n"; break;
            }
        }

        cout << "\n  ✓ STEP " << step << " complete\n";

        if (step < num_steps) {
            cout << "  press Enter for next step, 'q' to stop here: ";
            getline(cin, s);
            if (s == "q" || s == "Q") break;
        }
    }

    cout << "\n[CLEANUP] disabling ZDT pushers (valves/pump left as-is)\n";
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


//=========== main: menu loop ===========
static void print_menu() {
    cout << "\n========== Linux_test ==========\n"
         << "  1  IMU (WT901BC)   — serial port + live Roll/Pitch/Yaw\n"
         << "  2  DM2J step motor — IP + slave + move cm\n"
         << "  3  ZDT SMC pusher  — IP + slave + target pulse\n"
         << "  4  JC-100 pressure — IP + slave + live kPa read\n"
         << "  5  PQW 8CH relay   — IP + slave + channel on/off\n"
         << "  6  ZDT group       — 1~9 with skip list, unified target pulse\n"
         << "  7  Full step seq   — 8 pushers + rail + vacuum + retry grip\n"
         << "  8  Step no-rail    — pushers + vacuum only (skip DM2J rail)\n"
         << "  9  SD76 meter      — length meter live read + reset/pause/resume\n"
         << " 10  ZS_DIO winch    — crane winch relay (main/easy crane)\n"
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
        else if (line.empty()) continue;
        else cout << "[!] unknown selection '" << line << "'\n";
    }
    cout << "bye\n";
    return 0;
}
