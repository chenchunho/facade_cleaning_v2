// One-shot direction probe for the arm rail: absolute PR move to +1.0 cm @ 30 rpm.
// Goes through the driver so the lead conversion and travel guard both apply.
#include <cstdio>
#include "TCP_client.h"
#include "DM2J_RS570.h"

int main() {
    const char* IP = "192.168.1.20";
    const int   PORT = 4001, SLAVE = 14;
    const double LEAD = 7.731, TRAVEL_MAX = 48.0;
    const int    RPM = 30, ACC = 100, DEC = 100;
    const double TARGET_CM = 1.0;

    TCP_client cli;
    if (!cli.connectToServer(IP, PORT, false)) {
        printf("[FATAL] connect %s:%d failed\n", IP, PORT);
        return 1;
    }
    DM2J_RS570 drv;
    // DM2J::init returns false on success (Modbus-style true=error).
    if (drv.init(cli, SLAVE, /*debug=*/true)) {
        printf("[FATAL] init failed\n");
        return 1;
    }
    drv.set_lead_cm_per_rev(LEAD);
    drv.set_travel_limit_cm(0.0, TRAVEL_MAX);

    double before = -999.0, after = -999.0;
    bool okb = drv.read_position_cm(before);
    printf("\n=== BEFORE: read_position_cm ok=%d pos=%.3f cm ===\n", (int)okb, before);

    printf("=== SENDING: PR_move_cm(pr=0, mode=1 ABS, rpm=%d, pos=%.2f cm, acc=%d, dec=%d) ===\n",
           RPM, TARGET_CM, ACC, DEC);
    bool err = drv.PR_move_cm(0, 1, RPM, TARGET_CM, ACC, DEC);
    printf("=== PR_move_cm returned %s ===\n", err ? "true (ERROR/rejected)" : "false (ok)");

    bool oka = drv.read_position_cm(after);
    printf("=== AFTER: read_position_cm ok=%d pos=%.3f cm ===\n", (int)oka, after);

    if (okb && oka)
        printf("=== DELTA = %.3f cm ===\n", after - before);
    return 0;
}
