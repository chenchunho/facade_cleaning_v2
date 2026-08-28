// Drives DSZL_107 against the fake X518 on 127.0.0.1:15002.
// init(ip, port, id) does not probe, so every request is a real read.
#include <cstdio>
#include <cstring>
#include "DSZL_107.h"

int main(int argc, char** argv) {
    const char* mode   = (argc > 1) ? argv[1] : "normal";
    const bool  expect = (std::strcmp(mode, "normal") == 0);

    DSZL_107 d;
    if (d.init("127.0.0.1", 15002, 1, false)) {
        std::printf("RESULT mode=%s init=FAIL\n", mode);
        return 2;
    }

    int32_t v = -12345;                       // sentinel
    bool err = d.get_tension_long(v);         // convention: true = failure

    const bool ok = expect ? (!err && v == 1234) : err;
    std::printf("RESULT mode=%-10s read=%-7s value=%-8d %s\n",
                mode, err ? "FAIL" : "OK", (int)v, ok ? "PASS" : "*** WRONG ***");
    return ok ? 0 : 1;
}
