// ============================================================================
//  probe_dm2j — 上滑台 (DM2J_RS570) 機構標定與行程量測工具
//  ---------------------------------------------------------------------------
//  🔴 這支存在的理由（2026-08-28）：
//     驅動器只認脈衝。它回報的位置永遠是「命令計數器」，跟滑台實際在哪裡
//     可以差很多 —— 實測差了 7.7 倍，而 log、狀態、位置回讀三邊都看不出來。
//     **要知道機構真的走了多遠，唯一辦法是拿尺量。** 這支工具的用途就是
//     讓「驅動器說的」和「尺量到的」可以並排比較。
//
//     發現經過見 .claude/changelog.md [2026-08-28-drv5 ＝ 2026-08-28k]。
//
//  刻意不經過 WASH_ROBOT：不開刷子、不碰 PQW、不碰 ZDT、不需要主程式在跑。
//  ⚠ 主程式正在跑時不要用這支 —— 兩個連線會搶同一個 USR gateway。
//
//  建置（在 Pi 上；本專案沒有 CMake/Makefile，C++ 只能在 Pi 編）：
//    g++ -std=c++17 -O2 -Itransport -Iuser_lib \
//        -o probe_dm2j Linux_test/probe_dm2j.cpp \
//        transport/TCP_client.cpp user_lib/DM2J_RS570.cpp -lpthread
//
//  用法：
//    probe_dm2j read                  唯讀：印出目前位置與 PPR，不動任何東西
//    probe_dm2j goto  <cm> <rpm>      原始移動，**不套用標定**（cm = 馬達圈數）
//    probe_dm2j calib <cm> <rpm>      套用與主程式相同的標定與行程守衛後移動
//    probe_dm2j sweep <n> <rpm> [cm]  來回 n 次，每趟印位置（原始，不套標定）
//
//  📌 `goto` 與 `calib` 的差別就是這支工具的重點：
//     goto 5  → 馬達轉 5 圈 → 實際約 38 cm
//     calib 5 → 實際約 5 cm
//     兩者驅動器都回報「5.0000」。
//
//  ⚠ 標定量測方法（2026-08-28 實機，250 rpm，拿尺量）：
//     指令 1→7cm、2→15cm、5→38cm
//     最小平方：實際 = 7.731 × 指令 − 0.615（殘差全在 ±0.15cm 內）
//     截距 −0.6cm = 皮帶自硬限位起步的鬆弛量
//     預測性驗證：反推物理 20cm → 指令 2.666 → 量到 20cm ✅
//
//  🔴 量失步時注意：物理 0 點是**最左端機械硬限位**。不管中間失步多少，
//     回零都會頂回同一位置 —— **回零位置對「有沒有失步」沒有鑑別力**。
//     要驗失步，參考點不能是限位本身。
// ============================================================================
#include "TCP_client.h"
#include "DM2J_RS570.h"
#include <cstdio>
#include <cstdlib>
#include <string>

// 與 app/WASH_ROBOT.h 的 ARM_RAIL_* 保持一致；改那邊記得改這裡。
static const char*  IP       = "192.168.1.20";
static const int    PORT     = 4001;
static const int    SLAVE    = 14;
static const double LEAD     = 7.731;   // = ARM_RAIL_LEAD_CM_PER_REV
static const double TRAVEL   = 48.0;    // = ARM_RAIL_TRAVEL_MAX_CM
static double       SWEEP_CM = 17.0;

int main(int argc, char** argv) {
    std::string mode = (argc >= 2) ? argv[1] : "read";

    TCP_client cli;
    if (!cli.connectToServer(IP, PORT, false)) {
        printf("[FATAL] gateway %s:%d connect failed\n", IP, PORT);
        return 1;
    }
    DM2J_RS570 d;
    if (d.init(cli, SLAVE, false)) { printf("[FATAL] DM2J slave %d init failed\n", SLAVE); return 1; }

    uint16_t ppr = 0;
    if (d.read_pulse_per_rev(ppr)) printf("[WARN] PPR read failed\n");
    else                           printf("PPR = %u\n", ppr);

    double pos = 0;
    if (d.read_position_cm(pos)) { printf("[FATAL] position read failed\n"); return 1; }
    printf("start position (drive frame, 未套標定) = %.4f\n", pos);

    if (mode == "read") { printf("read-only, nothing moved.\n"); return 0; }

    if (mode == "goto" || mode == "calib") {
        double target = (argc >= 3) ? atof(argv[2]) : 0.0;
        int    rpm    = (argc >= 4) ? atoi(argv[3]) : 250;
        if (mode == "calib") {
            d.set_lead_cm_per_rev(LEAD);
            d.set_travel_limit_cm(0.0, TRAVEL);
            printf("\n=== calib goto %.3f cm @ %d rpm (lead=%.3f, limit 0~%.0f) ===\n",
                   target, rpm, LEAD, TRAVEL);
        } else {
            printf("\n=== raw goto %.3f (馬達圈數，未套標定) @ %d rpm ===\n", target, rpm);
        }
        bool err = d.PR_move_cm(0, 1, rpm, target, 100, 100);
        double after = 0; d.read_position_cm(after);
        printf("回傳 %s ／ 驅動器回報 = %.4f\n",
               err ? "true (拒絕或失敗)" : "false (執行)", after);
        if (mode == "goto")
            printf("→ 拿尺量實際位移。真實導程 = 實際公分 / %.3f\n", target);
        return 0;
    }

    if (mode == "sweep") {
        int n   = (argc >= 3) ? atoi(argv[2]) : 1;
        int rpm = (argc >= 4) ? atoi(argv[3]) : 250;
        if (argc >= 5) SWEEP_CM = atof(argv[4]);
        printf("\n=== sweep x%d @ %d rpm, 0 -> %.1f -> 0 (原始，未套標定) ===\n", n, rpm, SWEEP_CM);
        for (int i = 1; i <= n; ++i) {
            double a = 0, c = 0;
            d.read_position_cm(a);
            if (d.PR_move_cm(0, 1, rpm, SWEEP_CM, 100, 100)) { printf("[%d] OUT move FAILED\n", i);  return 1; }
            if (d.PR_move_cm(0, 1, rpm, 0.0,      100, 100)) { printf("[%d] HOME move FAILED\n", i); return 1; }
            d.read_position_cm(c);
            printf("[%2d] before=%8.4f  back_home=%8.4f\n", i, a, c);
            fflush(stdout);
        }
        printf("\n🔴 回零殘差恆為 0 不代表沒失步 —— 零點是硬限位，會把誤差吃掉。\n");
        return 0;
    }

    printf("usage: probe_dm2j <read|goto|calib|sweep> ...\n");
    return 1;
}
