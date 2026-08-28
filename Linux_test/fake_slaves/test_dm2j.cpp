// ============================================================================
//  test_dm2j — 驗 DM2J_RS570 的「機構標定換算」與「行程守衛」
//  ---------------------------------------------------------------------------
//  🔴 這支存在的理由（2026-08-28）：
//     上滑台是皮帶軸 **7.731 cm/圈**，而 driver 的 *_cm 介面原本寫死「1 圈 = 1 cm」
//     —— 每個 cm 指令都走了 7.7 倍。`ARM_SWEEP_CM = 17` 實際下 131cm 的指令，
//     而滑台總行程只有 50cm，**每次掃動都一路撞到底**。
//     完全隱形：驅動器只數脈衝，回報的永遠是漂亮的整數。
//
//     修法是 set_lead_cm_per_rev() + set_travel_limit_cm()。這支把那兩件事
//     變成可重跑的回歸測試，**免得日後有人又把換算「更正」回去**
//     —— 同一天推桿那邊就發生過：08-27 的「更正」本身才是錯的。
//
//  建置（在 Pi 上）：
//    g++ -std=c++17 -O2 -I../../user_lib -I../../transport -o test_dm2j \
//        test_dm2j.cpp ../../user_lib/DM2J_RS570.cpp ../../transport/TCP_client.cpp -lpthread
//
//  用法：
//    python3 fake_rtu.py --mode normal --slave 14 --port 14001 &
//    sleep 0.4 && ./test_dm2j
//
//  🔴 行程守衛那一項要看**假從站的輸出**來確認：被拒絕的指令
//     **不應該讓 req# 增加**。守衛的價值就在「在送出任何位元組之前就拒絕」——
//     若它只是事後回報失敗，機構早就被推到底了。
// ============================================================================
#include "DM2J_RS570.h"
#include "TCP_client.h"
#include <cstdio>
#include <cmath>
#include <string>

static int fails = 0;
static void check(bool cond, const char* what) {
    printf("%s  %s\n", cond ? "[PASS]" : "[FAIL]", what);
    if (!cond) ++fails;
}

int main(int argc, char** argv) {
    const int PORT  = (argc >= 2) ? atoi(argv[1]) : 14001;
    const int SLAVE = 14;
    const double LEAD   = 7.731;   // = ARM_RAIL_LEAD_CM_PER_REV
    const double TRAVEL = 48.0;    // = ARM_RAIL_TRAVEL_MAX_CM

    TCP_client cli;
    if (!cli.connectToServer("127.0.0.1", PORT, false)) {
        printf("[FATAL] 連不到假從站 127.0.0.1:%d —— 先啟動 fake_rtu.py\n", PORT);
        return 1;
    }
    DM2J_RS570 d;
    if (d.init(cli, SLAVE, /*debug=*/true)) { printf("[FATAL] init failed\n"); return 1; }

    // ---- 1) 預設導程 1.0 時，讀回的 cm 應等於 pulse/ppr ---------------------
    double cm_lead1 = 0;
    const bool r1 = d.read_position_cm(cm_lead1);
    check(!r1, "預設導程：read_position_cm 成功");

    // ---- 2) 套用實測導程後，同一筆讀數應該剛好放大 LEAD 倍 ------------------
    // 這是「換算真的套用了」最直接的證據：底層位元組沒變，只有換算變了。
    d.set_lead_cm_per_rev(LEAD);
    double cm_lead2 = 0;
    const bool r2 = d.read_position_cm(cm_lead2);
    check(!r2, "套用導程後：read_position_cm 成功");
    if (!r1 && !r2) {
        const double ratio = (cm_lead1 != 0.0) ? (cm_lead2 / cm_lead1) : 0.0;
        printf("       lead=1.0 -> %.4f ／ lead=%.3f -> %.4f ／ 比值 %.4f\n",
               cm_lead1, LEAD, cm_lead2, ratio);
        check(std::fabs(ratio - LEAD) < 0.01,
              "讀數比值應等於導程（證明換算確實經過新的乘除，不是巧合）");
    }

    // ---- 3) 行程守衛：範圍內放行、範圍外拒絕 --------------------------------
    d.set_travel_limit_cm(0.0, TRAVEL);

    printf("--- 以下兩發請對照假從站輸出：只有第一發該讓 req# 增加 ---\n");
    check(!d.PR_move_cm_nowait(0, 1, 250, 17.0, 100, 100),
          "範圍內 17cm：應被執行（回 false）");
    check(d.PR_move_cm_nowait(0, 1, 250, 60.0, 100, 100),
          "超範圍 60cm > 48cm：應被拒絕（回 true），且**不送出任何位元組**");
    check(d.PR_move_cm_nowait(0, 1, 250, -5.0, 100, 100),
          "負值 -5cm < 0：同樣應被拒絕");

    // ---- 4) 守衛停用時要回到原本行為（避免守衛變成沒得關的枷鎖）------------
    d.set_travel_limit_cm(0.0, 0.0);
    check(!d.PR_move_cm_nowait(0, 1, 250, 60.0, 100, 100),
          "守衛停用（lo==hi）後：60cm 應恢復放行");

    printf("\n=== test_dm2j: %s ===\n", fails ? "有失敗" : "全部通過");
    return fails ? 1 : 0;
}
