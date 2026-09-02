// Drives the four stage-2 drivers (SE3 / PQW / ZDT / DM2J) against fake_rtu.py.
//
//   ./test_stage2 <driver> <mode> <port>
//
// Covers the stage-2 batch (se3/pqw/zdt/dm2j) and the stage-3 one
// (mh300/clv900) — same shape, so one program serves both.
//
// [2026-09-01] xkc added. It was the only driver in the "bare send/recv pair →
// atomic sendAndReceive" batch with no fake-slave coverage at all (dm2j / pqw
// were already here, dy500 has its own test_dy500). A driver that has just had
// its transport path rewritten and cannot be exercised without the machine is
// exactly the one that needs a test on the bench.
//
// Each driver is asked for one read and the result is checked against what the
// mode should produce: only "normal" may succeed. None of these init()
// overloads probe, so run fake_rtu.py with --fault-from 1.
#include <cstdio>
#include <cstring>
#include <vector>
#include "SE3_inverter.h"
#include "PQW_IO_16O_RLY.h"
#include "ZDT_motor_control.h"
#include "DM2J_RS570.h"
#include "MH300_inverter.h"
#include "CLV900_inverter.h"
#include "XKC_Y25_RS485.h"

int main(int argc, char** argv) {
    if (argc < 4) { std::printf("usage: test_stage2 <se3|pqw|zdt|dm2j|mh300|clv900|xkc> <mode> <port>\n"); return 2; }
    const char* drv    = argv[1];
    const char* mode   = argv[2];
    const int   port   = std::atoi(argv[3]);
    const bool  expect = (std::strcmp(mode, "normal") == 0);

    bool err = true;
    char detail[64] = "";

    if (std::strcmp(drv, "se3") == 0) {
        SE3_inverter d;
        if (d.init("127.0.0.1", port, 1, false)) { std::printf("RESULT %s/%s init=FAIL\n", drv, mode); return 2; }
        double hz = -1.0;
        err = d.readOutputFreqHz(hz);
        std::snprintf(detail, sizeof(detail), "hz=%.2f", hz);
    } else if (std::strcmp(drv, "pqw") == 0) {
        PQW_IO_16O_RLY d;
        if (d.init("127.0.0.1", port, 1, 16, false)) { std::printf("RESULT %s/%s init=FAIL\n", drv, mode); return 2; }
        std::vector<bool> st = d.readAllStatus();
        err = st.empty();                       // empty == "could not read"
        std::snprintf(detail, sizeof(detail), "states=%d", (int)st.size());
    } else if (std::strcmp(drv, "zdt") == 0) {
        ZDT_motor_control d;
        if (d.init("127.0.0.1", port, 1, false)) { std::printf("RESULT %s/%s init=FAIL\n", drv, mode); return 2; }
        uint16_t cfg[15] = {0};
        err = d.read_driver_config(cfg);
        std::snprintf(detail, sizeof(detail), "cfg1=0x%04X", cfg[1]);
    } else if (std::strcmp(drv, "dm2j") == 0) {
        DM2J_RS570 d;
        if (d.init("127.0.0.1", port, 1, false)) { std::printf("RESULT %s/%s init=FAIL\n", drv, mode); return 2; }
        uint16_t e = 0xEEEE;
        err = d.read_error_code(e);
        std::snprintf(detail, sizeof(detail), "err=0x%04X", e);
    } else if (std::strcmp(drv, "mh300") == 0) {
        MH300_inverter d;
        if (d.init("127.0.0.1", port, 1, false)) { std::printf("RESULT %s/%s init=FAIL\n", drv, mode); return 2; }
        double hz = -1.0;
        err = d.readOutputFreqHz(hz);
        std::snprintf(detail, sizeof(detail), "hz=%.2f", hz);
    } else if (std::strcmp(drv, "clv900") == 0) {
        CLV900_inverter d;
        if (d.init("127.0.0.1", port, 1, false)) { std::printf("RESULT %s/%s init=FAIL\n", drv, mode); return 2; }
        double hz = -1.0;
        err = d.readRunFreq(hz);
        std::snprintf(detail, sizeof(detail), "hz=%.2f", hz);
    } else if (std::strcmp(drv, "xkc") == 0) {
        // read_state() reads 2 registers (OutPut, RSSI) through sendRecv(),
        // which is the single funnel this driver's transport change touched.
        XKC_Y25_RS485 d;
        if (d.init("127.0.0.1", port, 1, false)) { std::printf("RESULT %s/%s init=FAIL\n", drv, mode); return 2; }
        uint16_t out = 0xEEEE, rssi = 0xEEEE;
        err = d.read_state(out, rssi);
        std::snprintf(detail, sizeof(detail), "out=0x%04X", out);
    } else {
        std::printf("unknown driver %s\n", drv); return 2;
    }

    const bool ok = expect ? !err : err;
    std::printf("RESULT %-5s %-11s read=%-7s %-14s %s\n",
                drv, mode, err ? "FAIL" : "OK", detail, ok ? "PASS" : "*** WRONG ***");
    return ok ? 0 : 1;
}
