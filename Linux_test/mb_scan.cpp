// Raw Modbus RTU helper over the USR gateway — read-only scan + register read.
//   mb_scan scan <first> <last>        : FC03 probe each slave id, report answers
//   mb_scan read <slave> <addr> <cnt>  : FC03 read holding registers
// Deliberately raw (not via a driver class) so ANY slave id / register is reachable.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "TCP_client.h"

static uint16_t crc16(const uint8_t* b, int n) {
    uint16_t c = 0xFFFF;
    for (int i = 0; i < n; i++) { c ^= b[i]; for (int j = 0; j < 8; j++) c = (c & 1) ? ((c >> 1) ^ 0xA001) : (c >> 1); }
    return c;
}

// returns reply length, fills resp; 0 = no/!valid reply
static int txrx(TCP_client& cli, uint8_t slave, uint16_t addr, uint16_t cnt, uint8_t* resp, int cap) {
    uint8_t req[8];
    req[0] = slave; req[1] = 0x03;
    req[2] = addr >> 8; req[3] = addr & 0xFF;
    req[4] = cnt >> 8;  req[5] = cnt & 0xFF;
    uint16_t c = crc16(req, 6);
    req[6] = c & 0xFF; req[7] = c >> 8;
    int n = cli.sendAndReceive((char*)req, 8, (char*)resp, cap, 100, 300);
    return n;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: mb_scan scan <first> <last> | mb_scan read <slave> <addr> <cnt>\n"); return 2; }
    TCP_client cli;
    if (!cli.connectToServer("192.168.1.34", 4001, false)) { printf("[FATAL] gw connect failed\n"); return 1; }
    uint8_t resp[256];

    if (std::string(argv[1]) == "scan") {
        int a = atoi(argv[2]), b = atoi(argv[3]);
        printf("掃描 slave %d..%d (FC03 @0x0000 x1, 逾時 300ms)\n", a, b);
        int found = 0;
        for (int id = a; id <= b; ++id) {
            int n = txrx(cli, (uint8_t)id, 0x0000, 1, resp, sizeof(resp));
            if (n >= 5) {
                printf("  ★ slave %3d 有回應 (%d bytes):", id, n);
                for (int i = 0; i < n && i < 12; i++) printf(" %02X", resp[i]);
                printf("\n");
                ++found;
            }
        }
        printf("共 %d 個位址有回應\n", found);
        return 0;
    }
    if (std::string(argv[1]) == "read") {
        int sl = atoi(argv[2]); int ad = (int)strtol(argv[3], nullptr, 0); int ct = atoi(argv[4]);
        int n = txrx(cli, (uint8_t)sl, (uint16_t)ad, (uint16_t)ct, resp, sizeof(resp));
        printf("slave %d  FC03 @0x%04X x%d -> %d bytes:", sl, ad, ct, n);
        for (int i = 0; i < n && i < 40; i++) printf(" %02X", resp[i]);
        printf("\n");
        if (n >= 5 && resp[1] == 0x03) {
            int bc = resp[2];
            for (int i = 0; i < bc / 2; i++)
                printf("   reg 0x%04X = %5d (0x%04X)\n", ad + i,
                       (resp[3 + i*2] << 8) | resp[4 + i*2], (resp[3 + i*2] << 8) | resp[4 + i*2]);
        }
        return 0;
    }
    printf("unknown mode\n"); return 2;
}
