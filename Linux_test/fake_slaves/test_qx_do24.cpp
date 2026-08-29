// ============================================================================
//  test_qx_do24 — 驗 QX_DO24 的「傳輸層重試」與「失敗原因外露」
//  ---------------------------------------------------------------------------
//  🔴 這支存在的理由（2026-08-28）：
//     實機量到 PWM 寫入約 **20% 出現 `no reply (timeout)`**，於是加了
//     交易層重試（3 次 + 40ms backoff）。但當天再測 15 次**一次都沒失敗**，
//     `recovered on attempt` 計數是 0 —— **救援路徑實作了卻從未被執行過**。
//     等硬體自然出錯是不可靠的驗證方式；假從站可以隨時重現它。
//
//     ⚠ 而且失敗那一發剛好落在「停止螺旋槳」的指令上，而模組在寫入失敗時
//     **會保持前一個輸出繼續轉**。這條路徑值得被真的測到。
//
//  建置（在 Pi 上）：
//    g++ -std=c++17 -O2 -I../../user_lib -I../../transport -o test_qx_do24 \
//        test_qx_do24.cpp ../../user_lib/QX_DO24.cpp ../../transport/TCP_client.cpp -lpthread
//
//  用法（假從站與測試要成對啟動）：
//    python3 fake_rtu.py --mode normal --slave 9 --port 14001 &
//    sleep 0.4 && ./test_qx_do24 normal
//
//    python3 fake_rtu.py --mode drop --slave 9 --port 14001 --drop-count 2 &
//    sleep 0.4 && ./test_qx_do24 recover     # 前 2 次不回，第 3 次成功
//
//    python3 fake_rtu.py --mode drop --slave 9 --port 14001 &
//    sleep 0.4 && ./test_qx_do24 alldrop     # 一直不回 → 3 次全滅
//
//  📌 QX_DO24 的 init() 是 Mode B：**不發包**。所以 --fault-from 預設 1 就對，
//     不必像 SD76 那樣把故障往後挪一格（見 README 的陷阱 ①）。
// ============================================================================
#include "QX_DO24.h"
#include "TCP_client.h"
#include <cstdio>
#include <cstring>
#include <string>

static int fails = 0;
static void check(bool cond, const char* what) {
    printf("%s  %s\n", cond ? "[PASS]" : "[FAIL]", what);
    if (!cond) ++fails;
}

int main(int argc, char** argv) {
    const std::string scenario = (argc >= 2) ? argv[1] : "normal";
    const int PORT  = (argc >= 3) ? atoi(argv[2]) : 14001;
    const int SLAVE = 9;

    TCP_client cli;
    if (!cli.connectToServer("127.0.0.1", PORT, false)) {
        printf("[FATAL] 連不到假從站 127.0.0.1:%d —— 先啟動 fake_rtu.py\n", PORT);
        return 1;
    }
    QX_DO24 pwm;
    // 🔴 [2026-08-29] QX_DO24::init() 回 **true = 成功**，不是 Modbus 風格的
    //    「true = error」。本檔原本寫 `if (pwm.init(...)) FATAL`，把成功判成失敗
    //    ——第一次編起來跑就卡在這裡，一個斷言都沒跑到。
    //    ⚠️ 不要「順手改回來」：`bool init(...)` 的簽名在兩種語意下長得一模一樣，
    //    看 .h 分不出來（見 CLAUDE.md 介面契約節）。
    //    📌 逐支讀過原始碼的結果（2026-08-29）：`user_lib/` 14 支 driver 裡
    //       **只有 QX_DO24 一支是 true=成功**（DIHOOL_control 亦是，但全 repo 無呼叫端＝死碼），
    //       其餘 12 支（含 SE3 / MH300 / DM2J）都是 Modbus 風格的 false=成功。
    //    ⚠️ 我一度用「函式最後一個 return」去推，把 SE3 / MH300 也歸成 true=成功——
    //       **推錯了**，它們最後一個 return 是失敗路徑。歸因前要逐支讀，不要靠形狀猜。
    if (!pwm.init(cli, SLAVE, /*debug=*/true)) {
        printf("[FATAL] init failed\n");
        return 1;
    }

    // 一律用合法參數，才不會在安全守衛就被擋下 —— 這支測的是「傳輸」不是「參數」。
    const int    CH   = 0;      // driver API 是 0-based（= 面板上的 CH1）
    const int    HZ   = 50;     // 鎖定值
    const double DUTY = 5.0;    // 5% = 停止，即使真的接上硬體也不會轉

    if (scenario == "normal") {
        check(pwm.setPWM_Freq(CH, HZ),  "正常從站：setPWM_Freq 成功");
        check(pwm.setPWM_Duty(CH, DUTY),"正常從站：setPWM_Duty 成功");
        check(pwm.last_fail() == QX_DO24::Fail::None, "成功後 last_fail 應為 None");
    }
    else if (scenario == "recover") {
        // 前 2 次不回、第 3 次正常 → 重試（上限 3 次）應該剛好救回來。
        // 🔴 這正是實機上等不到的那條路徑。
        const bool ok = pwm.setPWM_Freq(CH, HZ);
        check(ok, "丟前 2 次：重試應在第 3 次成功（看 log 的 'recovered on attempt 3'）");
        check(pwm.last_fail() == QX_DO24::Fail::None, "救回來之後 last_fail 應為 None");
    }
    else if (scenario == "alldrop") {
        const bool ok = pwm.setPWM_Freq(CH, HZ);
        check(!ok, "一直不回：3 次重試全滅，應回 false");
        check(pwm.last_fail() == QX_DO24::Fail::NoReply,
              "失敗原因應為 NoReply（不是 OutOfRange —— 那正是先前訊息說謊的地方）");
        printf("       last_fail_str() = %s\n", pwm.last_fail_str());
    }
    else if (scenario == "outofrange") {
        // 對照組：參數超範圍必須**在送出之前**就被擋，且原因要跟通訊失敗分得開。
        check(!pwm.setPWM_Duty(CH, 20.0), "duty 20% 應被安全守衛拒絕");
        check(pwm.last_fail() == QX_DO24::Fail::OutOfRange,
              "原因應為 OutOfRange，而不是任何傳輸層錯誤");
    }
    else {
        printf("usage: test_qx_do24 <normal|recover|alldrop|outofrange> [port]\n");
        return 2;
    }

    printf("\n=== %s: %s ===\n", scenario.c_str(), fails ? "有失敗" : "全部通過");
    return fails ? 1 : 0;
}
