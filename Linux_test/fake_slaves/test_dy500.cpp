// Drives DY_500_weight_sensor against fake_rtu.py on 127.0.0.1:14002.
//
// Uses get_weight_long -> read_reg_long -> modbus_read(addr, 2, buf[64], len).
// read_reg_long retries 3x, so the fake slave sees up to 3 requests; neither
// init() overload probes, so the fault belongs on request #1 (--fault-from 1).
//
// Overflow window for this driver: the receive buffer is char buf[128] and the
// caller's is uint8_t buf[64], so any frame longer than 64 bytes overruns.
// --overflow-bc 100 gives a 105-byte frame: inside the window.
#include <cstdio>
#include <cstring>
#include "DY_500_weight_sensor.h"

int main(int argc, char** argv) {
    const char* mode   = (argc > 1) ? argv[1] : "normal";
    const bool  expect = (std::strcmp(mode, "normal") == 0);

    DY_500_weight_sensor d;
    if (d.init("127.0.0.1", 14002, 1, false)) {
        std::printf("RESULT mode=%s init=FAIL\n", mode);
        return 2;
    }

    int32_t v = -12345;                       // sentinel
    bool err = d.get_weight_long(v);          // convention: true = failure

    // fake_rtu.py --fill 0x11 over 2 registers -> 0x11111111
    const bool ok = expect ? (!err && v == 0x11111111) : err;
    std::printf("RESULT mode=%-11s read=%-7s value=0x%08X %s\n",
                mode, err ? "FAIL" : "OK", (unsigned)v, ok ? "PASS" : "*** WRONG ***");
    return ok ? 0 : 1;
}
