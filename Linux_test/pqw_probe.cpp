// Read-only PQW probe on the SAME .34 gateway as the SD76 meters.
// Discriminates "whole RS485 trunk down" from "meter branch/power only".
#include <cstdio>
#include "TCP_client.h"
#include "PQW_IO_16O_RLY.h"
int main() {
    TCP_client cli;
    if (!cli.connectToServer("192.168.1.34", 4001, false)) { printf("[FATAL] gw connect failed\n"); return 1; }
    PQW_IO_16O_RLY p;
    const bool e = p.init(cli, 12, /*debug=*/true);
    printf("PQW slave 12 @ .34: init %s\n", e ? "FAILED" : "ok");
    if (e) return 1;
    auto st = p.readAllStatus();
    printf("PQW readAllStatus: %s (%zu channels)\n", st.empty() ? "FAILED/empty" : "ok", st.size());
    return 0;
}
