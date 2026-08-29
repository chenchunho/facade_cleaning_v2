// Arm-rail absolute move with explicit rpm. Usage: rail_move <rpm> <target_cm> [acc] [dec]
// Goes through the driver so the lead conversion and the [0,48] travel guard both apply.
#include <cstdio>
#include <cstdlib>
#include "TCP_client.h"
#include "DM2J_RS570.h"

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: rail_move <rpm> <target_cm> [acc] [dec]\n"); return 2; }
    const int    RPM    = atoi(argv[1]);
    const double TARGET = atof(argv[2]);
    const int    ACC    = (argc >= 4) ? atoi(argv[3]) : 100;
    const int    DEC    = (argc >= 5) ? atoi(argv[4]) : 100;
    const double LEAD = 7.731, TRAVEL_MAX = 48.0;

    printf("=== rail_move: rpm=%d target=%.2f cm acc=%d dec=%d ===\n", RPM, TARGET, ACC, DEC);
    printf("=== linear speed = %.1f cm/s ===\n", RPM * LEAD / 60.0);

    TCP_client cli;
    if (!cli.connectToServer("192.168.1.20", 4001, false)) { printf("[FATAL] connect failed\n"); return 1; }
    DM2J_RS570 drv;
    if (drv.init(cli, 14, /*debug=*/true)) { printf("[FATAL] init failed\n"); return 1; }  // false = success
    drv.set_lead_cm_per_rev(LEAD);
    drv.set_travel_limit_cm(0.0, TRAVEL_MAX);

    double before = -999, after = -999;
    bool eb = drv.read_position_cm(before);   // false = success
    printf("BEFORE pos=%.3f cm (read %s)\n", before, eb ? "FAILED" : "ok");

    bool err = drv.PR_move_cm(0, 1, RPM, TARGET, ACC, DEC);
    printf("PR_move_cm -> %s\n", err ? "true (ERROR/rejected)" : "false (ok)");

    bool ea = drv.read_position_cm(after);
    printf("AFTER  pos=%.3f cm (read %s)\n", after, ea ? "FAILED" : "ok");
    if (!eb && !ea) printf("COORD DELTA = %.3f cm   <-- 座標移動量；實體要拿尺量\n", after - before);
    return 0;
}
