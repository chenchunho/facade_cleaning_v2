// Drives SD76_length_meters against the fake slave on 127.0.0.1:14001.
//
// Uses the init(TCP_client&, id, debug) overload — the one Crane_control_PI
// actually uses (shared cli_M gateway). It issues a Mode-B probe on init, so
// the fake slave sees request #1 = probe and request #2 = readStatus.
// Run fake_rtu.py with --fault-from 2 so the probe is answered normally.
//
// The init(ip, port, ...) overload does NOT probe; using it here would leave
// the fault reply unrequested and every scenario would silently "pass".
#include <cstdio>
#include <cstring>
#include "SD76_length_meters.h"
#include "TCP_client.h"

int main(int argc, char** argv) {
    const char* mode   = (argc > 1) ? argv[1] : "normal";
    const bool  expect = (std::strcmp(mode, "normal") == 0);  // only 'normal' should succeed

    TCP_client cli;
    if (!cli.connectToServer("127.0.0.1", 14001)) {
        std::printf("RESULT mode=%s connect=FAIL\n", mode);
        return 2;
    }

    SD76_length_meters m;
    if (m.init(cli, 1, false)) {          // request #1 — probe, answered normally
        std::printf("RESULT mode=%s init=FAIL (probe rejected)\n", mode);
        return 2;
    }

    // 0xEE = sentinel: on failure the driver must leave the caller's buffer alone.
    uint8_t work = 0xEE, alarm = 0xEE;
    bool err = m.readStatus(work, alarm); // request #2 — the fault under test

    // fake_rtu.py fills register data with --fill (default 0x11) uniformly.
    const bool ok = expect ? (!err && work == 0x11 && alarm == 0x11) : err;
    std::printf("RESULT mode=%-11s read=%-7s work=0x%02X alarm=0x%02X  %s\n",
                mode, err ? "FAIL" : "OK", work, alarm, ok ? "PASS" : "*** WRONG ***");
    return ok ? 0 : 1;
}
