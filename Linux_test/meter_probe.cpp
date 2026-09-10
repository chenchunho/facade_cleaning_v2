// Read-only SD76 probe — no motion, no crane program needed.
// Reports per-slave whether the meter answers on the shared .34 gateway.
#include <cstdio>
#include "TCP_client.h"
#include "SD76_length_meters.h"

int main() {
    TCP_client cli;
    if (!cli.connectToServer("192.168.1.34", 4001, false)) {
        printf("[FATAL] gateway 192.168.1.34:4001 connect failed\n"); return 1;
    }
    printf("gateway 192.168.1.34:4001 connected OK\n");
    for (int id = 1; id <= 2; ++id) {
        const char* side = (id == 1) ? "left " : "right";
        SD76_length_meters m;
        const bool init_err = m.init(cli, id, /*debug=*/true);   // false = success
        printf("SD76 %s (slave %d): init %s\n", side, id, init_err ? "FAILED" : "ok");
        if (init_err) continue;
        int32_t v = 0;
        const bool rd_err = m.readUpperInteger(v);
        printf("SD76 %s (slave %d): readUpperInteger %s value=%d\n",
               side, id, rd_err ? "FAILED" : "ok", v);
    }
    return 0;
}
