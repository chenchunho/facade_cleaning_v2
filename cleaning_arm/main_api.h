#pragma once
// ==========================================================
//  DamiaoAPI -- dual-motor init + background TCP command server
//
//  Minimal usage:
//      DamiaoAPI api;
//      if (!api.init("COM10", 921600u,
//              {damiao::DM10010L,   0x01, 0x11},   // M1
//              {damiao::DM4340_48V, 0x02, 0x22}))  // M2
//          return 1;
//      api.start();
//      api.stop();
//
//  TCP commands (line-terminated with \n or \r\n):
//      All commands require M1 or M2 prefix (space-separated).
//
//      M1 / M2 (both):
//        ENABLE                              -> OK
//        DISABLE                             -> OK
//        ZERO                                -> OK
//        STATUS                              -> pos=X vel=Y tau=Z hold=0/1 moving=0/1
//        MIT <kp> <kd> <q> <dq> <tau>       -> OK
//        MODE <1-7>                          -> OK / FAIL
//        PARAM <reg_id>                      -> <value>
//        HOME                                -> OK
//        HOLD                                -> OK
//        UNHOLD                              -> OK
//        MOVETO <rad> [speed_rad_s]          -> OK target=X speed=Y
//        MOVING                              -> 0 / 1
//
//      M1 only (大馬達):
//        SETWALL <mm>                        -> OK  (0 = no limit)
//        APPROACH <clearance_mm> [speed_rad_s] -> OK clearance=X speed=Y
//        TOUCHWALL <wall_mm> <LEFT|CENTER|RIGHT> [clearance_mm>=0] [speed] -> OK wall=X slot=Y clearance=Z speed=W [warn=SETWALL_MAY_LIMIT]
//        CALIBRATE                           -> OK  (push negative to stop, set zero)
//
//      M2 only (小馬達左右軸):
//        LR_CALIBRATE [LEFT|RIGHT]           -> OK  (default: LEFT)
//        LR_SLOT <LEFT|CENTER|RIGHT> [spd]   -> OK slot=X speed=Y
//
//      SYS (無 M1/M2 前綴):
//        INIT                                              -> OK
//        DEPLOY <wall_mm> <LEFT|CENTER|RIGHT> [clr_mm] [spd]  -> OK wall=X slot=Y clearance=Z [warn=SETWALL_MAY_LIMIT]
//        PARK                                              -> OK
// ==========================================================

#include "damiao.h"

#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>

// ---- cross-platform socket alias ----------------------------------------
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef SOCKET socket_t;
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <arpa/inet.h>
#  include <unistd.h>
   typedef int socket_t;
#  define INVALID_SOCKET (-1)
#  define SOCKET_ERROR   (-1)
   inline int closesocket(int s) { return ::close(s); }
#endif
// -------------------------------------------------------------------------

class DamiaoAPI
{
public:
    using CommandHandler = std::function<std::string(const std::string& cmd_line)>;

    // ---- motor configuration (for init) -------------------------------------
    struct MotorConfig {
        damiao::DM_Motor_Type type;
        uint32_t slave_id;
        uint32_t master_id;
    };

    DamiaoAPI()  = default;
    ~DamiaoAPI() { stop(); }

    // ------------------------------------------------------------------
    bool init(const char*   port,
#ifdef _WIN32
              uint32_t      baud,
#else
              speed_t       baud,
#endif
              MotorConfig   m1,
              MotorConfig   m2,
              int           tcp_port = 9527);

    void start();
    void stop();

    void registerCommand(const std::string& key, CommandHandler handler);

    // ---- arm geometry constants (M1 / large motor) --------------------------
    // 🔴 [2026-09-02] 320 → 490（**實測有效值**，三點量測擬合，殘差 <0.1mm）。
    //
    // 量法（per user 提議）：四顆推桿伸到固定行程頂住平整玻璃 → 機身被撐在確定的
    // standoff → M1 斷電、per user 手動把手臂壓到「真正貼平」→ 讀編碼器角度。
    // 推桿行程精確（3000 脈衝/cm，WASH_ROBOT.h 標定段有五條獨立證據），是可靠的自變數。
    //
    //   推桿 10.0cm（機身距玻璃 20.0cm，尺量 20）→ θ = 0.5419
    //   推桿 12.5cm（機身距玻璃 22.5cm，尺量 23）→ θ = 0.5941
    //   推桿 15.0cm（機身距玻璃 25.0cm，尺量 25.5）→ θ = 0.6464
    //   Δθ 兩段各為 0.0522 / 0.0523 —— 線性，且第三點命中預測到 0.0001 rad
    //
    // 最小二乘：機身距離 = **490** × sin(θ − VERTICAL_OFFSET) + 121.0  [mm]
    //           殘差 +0.0 / −0.1 / +0.0 mm
    //
    // 🔴 **舊值 320 每次只算到實際的 65%**（機身移 50mm，模型只算出 32.7mm）。
    // 這是今天所有校正問題的共同上游：`theta_target` 由 `wall_mm` 經此式算出，
    // 所以每個 `wall_mm` 對應的角度都偏小 —— 幾何量到的牆距完全不能用、
    // 必須靠經驗把 `ARM_WALL_MM_DEFAULT` 一路調到 380，而 `verify_arm_deploy_`
    // 的預期角度也建立在同一條錯式子上（障礙偵測的基準一直是錯的）。
    //
    // ⚠️ **命名為「有效值」是刻意的**：490 也可能不是真實臂長，而是**編碼器角度標度
    //    偏約 1.53 倍**（那樣 320 是對的、錯的是 θ 本身，連 M1 的界限 0~1.5 rad
    //    都要重新解讀）。今天只量了「θ 與距離的關係」，兩種解釋在資料上完全等價。
    //    要分辨必須**獨立驗證角度**（例如用量角器量兩個 θ 下的實際擺角）—— 尚未做。
    // 🔴 **本值在 `app/WASH_ROBOT.h` 有一份複本 `ARM_M1_LENGTH_MM`（供 verify_arm_deploy_ 用）。**
    //    兩份必須同步；2026-09-02 改這一份時一度忘了另一份，造成分岔。
    static constexpr float ARM_LENGTH_MM       = 490.0f;   // 2026-09-02: 320→490（三點實測擬合，見上）
    static constexpr float PASSIVE_EXT_MM      = 86.46f;
    static constexpr float VERTICAL_OFFSET_RAD = 0.38f;

    // M2-slot-specific tool extension beyond the passive joint (mm)
    // [2026-08-18 per user] LEFT/RIGHT values SWAPPED — the physical tool heads
    // were swapped left-for-right on the hardware, so each slot now presents the
    // other one's extension. Was LEFT=148.09 / RIGHT=134.07. CENTER unchanged.
    // Mirrored in user_lib/WASH_ROBOT.h (ARM_M2_TOOL_LEFT_MM / _RIGHT_MM) — that
    // copy drives verify_arm_deploy_'s expected-θ check, so the two MUST stay in
    // sync or DEPLOY verification will compare against the wrong angle.
    // 🔴 [2026-09-02 per user 實機三點量測] LEFT 134.07→192.37、RIGHT 148.09→204.32。
    //   量法：推桿 10cm、四吸盤 -66/-67 kPa 密封固定站位，M1 斷電由使用者手壓到
    //   「完全貼合」，讀 M1 角度（各讀 6 次，極差 0.0004 / 0.0003）：
    //       滾筒(RIGHT) θ=0.5728    刮刀(LEFT) θ=0.6006
    //   換算用 CENTER 已驗收的壓力條件（命令角超出貼合角 0.294 rad → 19 Nm、per user 認可）：
    //       TOOL_EXT = 520 - PASSIVE_EXT(86.46) - 490*sin(θ + 0.294 - 0.38)
    //
    // 📌 **兩個值都少了約 57mm，而且是同一個量** —— 兩工具的相對差（實測 11.95mm
    //   vs 舊值 14.02mm）幾乎沒錯，⇒ 08-18 那次左右對調是對的，錯的是共同偏移。
    //   來源幾乎確定是今天的 ARM_LENGTH_MM 320→490：這兩個值是在**舊臂長**下反推的，
    //   把臂長誤差整個吸收進去了。**日後再動 ARM_LENGTH_MM，這兩個必須一起重量。**
    //
    // 🟡 **CENTER 的 160.00 未經同樣的驗證**（per user「CENTER 用原本的就可以，沒差」）。
    //   它現在是三者中唯一沒被實測過的，也是 wall_mm=520 的錨點——自洽但未獨立驗證。
    static constexpr float TOOL_EXT_LEFT_MM   = 192.37f;
    static constexpr float TOOL_EXT_CENTER_MM = 160.00f;
    static constexpr float TOOL_EXT_RIGHT_MM  = 204.32f;

    // 🔴 [2026-09-02 per user 斷電手轉實測] M2 兩個工作位置的**絕對**角度。
    //   斷電手轉到工作位置後讀值，各讀 5 次、極差皆為 0.0000：
    //       滾筒(RIGHT) = +0.5316    CENTER = -0.4099    刮刀(LEFT) = -1.0115
    //   零點確認**撐得過 motor_api 重啟**（重啟前後 M1/M2 讀值差 0.0000，
    //   解掉 08-13 註解裡「看起來會但沒驗證」那個問題）⇒ 絕對值可以當常數用。
    //   ⚠️ 但 lr_calibrate_slot 會**重設零點**，跑過校正後這兩個值即失效。
    //
    // 📌 **刻意不再由 lr_half_range 對稱推導**：舊式 LEFT=-half+0.05 / RIGHT=+half-0.1，
    //   兩邊退讓量本來就不一樣大（0.05 vs 0.10）——那正是機構不對稱的證據，卻被硬塞
    //   進對稱模型。實測中點在 -0.2400、半幅 0.7716，與假設的 0(中點)/0.7275 都不符。
    static constexpr float M2_SLOT_LEFT_RAD  = -1.0115f;  // 刮刀
    static constexpr float M2_SLOT_RIGHT_RAD =  0.5316f;  // 滾筒

    // 🔴 [2026-09-02] 同樣兩個工作位置，改以**正向機械停點**為基準表示。
    //   實測（LR_CALIBRATE Phase 1）：正向停點 = **+0.7204**（tau 3.44，明確撞到）。
    //       滾筒 0.5316 − 0.7204 = −0.1888     刮刀 −1.0115 − 0.7204 = −1.7319
    //
    // 📌 **為什麼要有這一組**：上面的絕對值只在零點不變時成立。停點是**真實的機械
    //   特徵**，零點怎麼變它都在那裡 ⇒ 只要校正找得到它，slot 目標就能自動還原。
    //   `lr_move_to_slot_impl` 優先用這組，`lr_stop_valid` 為 false 時才退回絕對值。
    //
    // ⚠️ **只用正向那一個停點**，不用「兩個停點取中點」的舊模型：實測負向走到
    //   −1.2831 仍在 0.5 rad/s 前進、tau 僅 −1.8（純摩擦），2 rad 預算用盡而 abort
    //   ⇒ **該側在可及範圍內沒有停點**，中點模型對這個機構不成立。
    static constexpr float M2_SLOT_LEFT_FROM_STOP  = -1.7319f;  // 刮刀
    static constexpr float M2_SLOT_RIGHT_FROM_STOP = -0.1888f;  // 滾筒
    static constexpr float M2_SLOT_CENTER_FROM_STOP = -0.7204f; // CENTER（絕對 0 相對於停點）

    // ---- M2 / small motor constants (左右軸) --------------------------------
    static constexpr float ZERO_OFFSET = 0.8f;   // M2 only: calibration back-off / lr slot offset

    // ---- M1 gravity feedforward (0 = disabled) ------------------------------
    // [2026-08-14 per user] 原本假設 tau_ff = ARM_MASS_KG*g*L*sin(pos-VERTICAL_OFFSET_RAD)，
    // 套用實秤 2.3kg 之後暴衝反而更嚴重（vel 1.5→1.78 rad/s、overshoot 0.117→
    // 0.219 rad）。事後用慢速實測 + M1 STATUS 量了 3 個乾淨的靜止點
    // (0.6495,-9.5238) / (0.7662,-12.3565) / (0.8330,-12.7473)，回推發現：
    //   1. 真正的重力零點角度落在 M1 硬體範圍 [0,1.5] 之外（約 3.32 rad）——
    //      代表整個可移動範圍內重力都沒有天然平衡點，會一路把手臂往外拉，
    //      跟 VERTICAL_OFFSET_RAD=0.38 的假設完全對不上，這就是先前補償
    //      方向錯誤、越補越糟的根因
    //   2. 換算「等效重量」約 6.65kg，比實秤的 2.3kg 重快 3 倍——公式把整重
    //      當「集中在 ARM_LENGTH_MM 末端」的點質量算，跟實際重心分布/連桿
    //      結構的落差，屬於這個簡化模型的已知限制
    // 改用直接從實測數據反推出的 M1_GRAVITY_K/M1_GRAVITY_PHASE_RAD，取代
    // ARM_MASS_KG 那條路徑（ARM_MASS_KG 常數本身已移除，兩處呼叫點都改吃這組
    // 新常數；VERTICAL_OFFSET_RAD 維持只用於牆距三角函數，不要跟這組重力常數混用）。
    // 🔴 [2026-09-02] K: 20.87 → 16.09（相位幾乎不變：3.317 → 3.319）。
    //
    // 舊值的出處是「**兩個外側乾淨點**解出、中間點驗證誤差 ~6%」—— 只有兩個點，
    // 而且是靜態量測，**必然混著摩擦**（當日雙向掃描量到摩擦 0.39~1.86 Nm）。
    //
    // 新值來自雙向慢掃（M1 在 0.42↔0.65 之間往返 3 趟，速度 0.15 rad/s）：
    //   向外 T_out(θ) = −G(θ) + f(θ)      向內 T_in(θ) = −G(θ) − f(θ)
    //   ⇒ G(θ) = −(T_out+T_in)/2  摩擦相消； f(θ) = (T_out−T_in)/2  附帶得到摩擦曲線
    // 12 個分箱點（每箱 ≥8 個 |v|>0.05 的樣本取中位數）最小二乘：
    //   **G(θ) = 16.09 × sin(θ − 0.1774)，殘差 RMS 0.163 Nm、最大 0.349**
    //
    // 🔴 **舊值在工作區高估約 30%**，後果是 `kp*err` 必須抵銷多出來的前饋才能平衡，
    //    手臂因此系統性停在命令角之前（下垂）—— 那是 2026-09-02 一整天
    //    「DEPLOY 到不了目標、wall_mm 只能靠經驗一路往上調」的根因。
    //
    // ⚠️ **擬合區間是 0.42~0.64**（受玻璃阻擋，掃不到更遠）。舊值的擬合區是 0.65~0.83，
    //    兩者在 0.64 相差 2.17 Nm —— 剛性手臂的重力矩不可能不連續，所以**至少有一邊的
    //    量測是錯的**。新值有 12 點、殘差 0.16；舊值有 2 點且未消摩擦 ⇒ 採信新值。
    //    但 **0.65 以上仍是外推，尚未實測**。
    // 📌 速度必須 >0.1 rad/s 才是真的滑動：0.03 rad/s 實測會停在 stick-slip
    //    （40 秒只走 0.16 rad），量到的是靜摩擦反覆累積，不是動摩擦。
    static constexpr float M1_GRAVITY_K          = 16.09f;   // 2026-09-02: 20.87→16.09（雙向掃描 12 點擬合）
    static constexpr float M1_GRAVITY_PHASE_RAD  = 3.3190f;  // 2026-09-02: 3.317→3.3190（相位幾乎未變）
    // [2026-08-14b per user] 三個實測點都落在 0.65~0.83 rad，PARK 目標
    // (PARK_STOP_MARGIN=0.05) 遠在這個範圍之外——套進 go_home_slot 後實測「PARK
    // 收不到底」，手算發現 pos=0.05 處外推出來的補償方向是「往伸出推」，跟收回
    // 方向相反，會在終點前提早跟 kp 打平、卡住。在有更多資料驗證這段之前，只在
    // 有實測驗證過的範圍內套用前饋，範圍外一律不補償（退回純 kp/kd），避免拿
    // 外推錯誤的方向去扯後腿。
    // [2026-08-17] 0.55 → 0.20。上面那筆的觀察（低角度處方向會反過來）是對的，
    // 但門檻設在 0.55 過度保守：tau_ff = K*sin(pos - PHASE) 的變號點是
    // pos = PHASE - π = 3.317 - 3.14159 = 0.1754 rad，也就是 0.1754 以上公式
    // 方向都還是正確的（負值 = 往收回方向出力 = 抵銷重力）。原本 0.1754~0.55
    // 這整段方向正確卻被擋掉，而那正好是 PARK 收回最後、最吃力的一段：
    // pos=0.4 時真實需求約 4.74 Nm，沒有前饋就得靠 kp 硬頂出 4.74/26 = 0.18 rad
    // 的位置落後才生得出力 —— 這就是「PARK 無力收回原點」的直接來源。
    // 另外舊門檻還有個副作用：跨越 0.55 的瞬間 tau_ff 會在 0 和 -7.53 Nm 之間
    // 階躍，手臂只要在門檻附近抖動就會被這個 7.5 Nm 的跳變放大成震盪。
    // 新值 0.20 略高於變號點 0.1754，留一點餘裕；0.20 以下仍然不補償（那裡公式
    // 方向確實是錯的）。注意 0.1754~0.65 屬於外推區（實測點最低只到 0.6495），
    // sin 形式對單一剛體重力矩是正確的物理形式，但 K 值若偏大，外推區會過補償——
    // 首次驗證請留意 PARK 末段有沒有出現「自己往回衝」的過補償跡象。
    static constexpr float M1_GRAVITY_MIN_VALID_RAD = 0.20f;   // 變號點 PHASE-π=0.1754，留餘裕

    // ---- M1 Coulomb friction breakaway assist -------------------------------
    // [2026-08-18 per user] PARK/DEPLOY 修好重力前饋之後仍有「停一下再突然滑一段」
    // 的分段感。從 bench trace 逐行差分（每行 240ms，命令速度應為 0.0168 rad/行）
    // 可以看到典型 stick-slip：Δpos 在 -0.0011 / -0.0004 / 0.0000（卡住）與
    // -0.0271 / -0.0344 / -0.0400（突然滑，末段甚至超過命令速度，tau 翻正在煞車）
    // 之間交替。
    // 用「總 tau 減去該點重力」量出來的淨推力：
    //   pos=0.5217 淨 0.78 Nm → 推不動
    //   pos=0.4522 淨 1.28 Nm → 突破
    //   pos=0.4946 滑動中淨僅 0.14 Nm 就能維持 0.1 rad/s
    // → 靜摩擦約 1.0 Nm、動摩擦約 0.15 Nm，相差 6~7 倍，正是 stick-slip 的成因。
    // （更早一版曾估 3.8 Nm，那是拿 tau_ff 還算錯的 log 推的，基準不對，已作廢。）
    //
    // 補償策略：只在「快要動不動」時幫忙推一把，動起來就退場。若全速期間持續補
    // 償，反而會加劇末段衝過頭（動摩擦太小，多推的力沒有東西吸收）。因此用速度
    // 線性衰減而非 on/off 開關——硬切換會在門檻附近反覆進出，製造新的抖動。
    //   scale = 1 - min(|vel| / FADE_VEL, 1)
    //   vel=0 → 補滿 0.8 Nm；vel=0.05 → 補一半；vel>=0.10 → 完全不補
    // 取 0.8 Nm 略低於量到的靜摩擦 1.0 Nm：寧可欠補讓它慢一點鬆動，也不要過補
    // 造成手臂自己往前溜。DEADBAND 讓到位附近不補，避免在 target 兩側來回推。
    // HOLD 分支刻意不套用——靜止撐住本來就靠靜摩擦幫忙，補了只會造成緩慢漂移。
    // [2026-08-18 per user] 0.8 → 1.5。0.8 是照 pos≈0.52 量到的靜摩擦 1.0 訂的，
    // 但後續 bench 顯示靜摩擦隨角度大幅變化——手臂伸得越遠、軸承側向負載越大：
    //     pos≈0.22 → ~2.3 Nm ／ pos≈0.52 → ~1.0 Nm ／ pos≈0.83 → ≥4.6 Nm
    // 0.8 只夠應付最輕的那一段，於是 PARK 兩道關卡都只以 0.002~0.004 rad 的餘裕
    // 擦過，DEPLOY 300 也曾以 err=0.0501738 對 0.05 容差差 0.0002 rad 判失敗。
    // 提到 1.5 是折衷（低角度需求 2.3、中段只要 1.0），不是精確補償；真要精準得
    // 讓它隨角度變化，複雜度高，非必要不做。fade 機制仍在，動起來就退場，所以
    // 中段過補償的風險有限。
    // [2026-08-18 per user] 1.5 → 2.5。配合上面把 DEPLOY 收尾的 hold_pos 改鎖
    // move_target：手臂停穩後 fade 回滿，這個值就是它能多拿到的推力。1.5 只讓
    // 淨力到 ~4.5 Nm，仍略低於 pos≈0.83 實測的 ≥4.6 Nm 靜摩擦；2.5 讓淨力到
    // ~5.5 Nm 才真的越過。仍遠低於馬達額定，且只在低速時生效（fade 機制），
    // 中高速段完全不參與，所以不會加劇過衝。
    // ❌ [2026-09-02 試過、無可量測效果，已還原] **斜坡參考領先量夾制
    //    （M1_RAMP_MAX_LEAD_RAD=0.05）。不要在沒有更好的量測方法前重試。**
    //
    // 現象：從 PARK（pos≈0.047）起步時手臂震一下。`MOVETO 0.45 0.15` 的峰值速度：
    //
    //     夾制前 0.4335 (2.9x)    夾制後 0.5067 (3.4x)    還原後 0.4823 (3.2x)
    //
    // 🔴 **三次的散布 ±0.04 蓋過了三者的差距 ⇒ 這個實驗沒有結論。**
    //    當下我曾據此宣稱「夾制讓情況更糟」——**那是單次比較下的過度推論，已收回。**
    //    真正能說的只有：**夾制沒有帶來可量測的改善**，而且無論改不改，
    //    峰值都穩定落在 0.43~0.51，**始終高於 M1_VEL_SAFETY_LIMIT(0.4)**。
    //
    // ⚠️ 0.4335 正是 2026-08-18 記下的同一個數字 ⇒ 現象可重現，而且當日的重力模型
    //    修正（K 20.87→16.09）沒有消除它 —— **成因不在重力前饋。**
    //
    // 夾制的構想與它的疑慮（保留供日後參考，但兩者都未經證實）：
    //   構想 — pos < M1_GRAVITY_MIN_VALID_RAD(0.20) 時重力前饋硬設為 0，而該區靜摩擦
    //          約 2.3 Nm → 手臂卡住、斜坡 move_cur 仍往前跑 → kp*誤差 累積 → 掙脫時
    //          一次釋放。限制領先量即可避免累積。
    //   疑慮 — 夾住之後扭力會**持續**維持在 kp*lead(=4.5 Nm)，遠高於運動摩擦，
    //          可能把一次性衝擊換成持續過推（終端速度 ≈ kp*lead/kd = 0.9 rad/s）。
    //
    // 📌 **下一步不要再加補償機制、也不要再調夾制值。** 兩件事要先做：
    //    ① **改善量測**：單次峰值的散布太大，需要多次重複取統計量才分辨得出效果。
    //    ② **驗證增益本身**：kp=90 是當日為了補償**錯誤的重力模型**從 34 一路加到 90 的。
    //       重力已修正，kp=90 / kd=5 這組很可能過度欠阻尼 —— 先查阻尼比。

    // ❌ [2026-09-02 試過並移除] **接觸後快轉斜坡（M1_CONTACT_* / ramp step x3）——
    //    無時間效益，卻提高施力速率，已收回。**
    //
    // 構想：wall_mm 是超量命令，DEPLOY 520 的命令角 0.969 比手臂能到的 ~0.65 多 0.29 rad，
    //   斜坡要以 0.3 rad/s 爬完那段永遠走不到的距離。偵測到位置停滯就把 step x3。
    // 結果：工具切換 8.0 / 12.1 / 11.8 s，改前 11.4 / 11.0 s ——**同一散布內，無改善**；
    //   而 tau 由 ~12 升到 13.5~13.8（快轉確實生效，只是不影響總時間）。
    //
    // 🔴 **為什麼沒用（密集取樣量出來的真因）**：伸出段的速度在 0 與 0.31 之間反覆跳動
    //   （+0.104 +0.128 +0.214 +0.006 +0.263 … -0.018 +0.311 …）＝ **黏滑 stick-slip**。
    //   手臂卡住→力矩累積→掙脫竄一下→再卡住。0.50→0.69 rad 花 2.6s，
    //   **平均僅 0.07 rad/s，遠低於命令的 0.3** ⇒ 手臂根本沒在跟隨斜坡，加快斜坡自然無效。
    //   per user 現場描述「切換工具靠上時 M1 都會震一下」，指的就是這個。
    //
    // 📌 **真正的方向是靜摩擦**：M1_FRICTION_TAU=2.5 是用來破靜摩擦的，
    //   但程式碼他處記載某些角度的靜摩擦 **>=4.6 Nm**，補償明顯不足。
    //   ⚠️ 未量測前不要調——當日已有三次「憑假設改參數、被量測推翻」。

    static constexpr float M1_FRICTION_TAU          = 2.5f;    // 靜摩擦隨角度 1.0~4.6，取能越過高端的值
    static constexpr float M1_FRICTION_FADE_VEL     = 0.10f;   // rad/s，此速度以上完全不補
    static constexpr float M1_FRICTION_DEADBAND_RAD = 0.02f;   // rad，誤差小於此不補

    // ---- direct motor control (C++ API, default targets M2) -----------------
    void  enable();
    void  disable();
    void  set_zero();
    bool  switch_mode(damiao::Control_Mode mode);
    void  control_mit(float kp, float kd, float q, float dq, float tau);
    void  go_home();
    void  hold_position();
    void  release_hold();
    bool  is_holding() const;

    // ---- M2 trajectory (左右軸) ---------------------------------------------
    void  lr_calibrate(bool seek_left = true);
    bool  lr_move_to_slot(int slot, float speed_rad_s = 0.4f);   // 2026-06-06: returns true on converge, false on timeout

    // ---- M1 trajectory (大馬達臂) -------------------------------------------
    bool  calibrate_arm();                                          // push negative to stop, set zero; false = stop not found
    void  set_wall_distance(float mm);                              // 0 = no limit
    bool  approach_wall(float clearance_mm, float speed_rad_s = 0.3f);
    // touch_wall: move M1 so slot-specific tool tip is at clearance_mm from wall.
    // m2_slot: -1=LEFT  0=CENTER  +1=RIGHT
    // If SETWALL is active with a smaller wall_dist, move_to_slot may silently
    // clamp theta_target; call 'M1 SETWALL 0' first to disable the safety limit.
    bool  touch_wall(float wall_dist_mm, int m2_slot,
                     float clearance_mm = 0.0f, float speed_rad_s = 0.3f);

    // ---- shared trajectory --------------------------------------------------
    void  move_to(float target_rad, float speed_rad_s = 0.3f);     // smooth move (default: M2)
    bool  is_moving() const;

    float get_position() const;
    float get_velocity() const;
    float get_torque()   const;

    // ---- access underlying objects ------------------------------------------
    damiao::Motor&         m1_motor() { assert(m1_.motor); return *m1_.motor; }
    damiao::Motor&         m2_motor() { assert(m2_.motor); return *m2_.motor; }
    damiao::Motor_Control& ctrl()     { assert(dm_); return *dm_; }

private:
    // ---- per-motor state ----------------------------------------------------
    struct MotorSlot {
        enum class SlotId { M1, M2 } id = SlotId::M2;
        std::string name;
        std::unique_ptr<damiao::Motor> motor;
        float lower_bound { 0.0f };    // move_to lower clamp: M1=0.0, M2=-ZERO_OFFSET
        float upper_bound { 1e9f };    // hard upper clamp: M1=1.2 rad, M2=unconstrained

        float hold_kp { 5.0f };       // per-slot MIT hold/move gain (DEPLOY path: move_to_slot/feedback_loop move_act)
        float hold_kd { 1.0f };

        // [2026-07-24 per user] PARK (go_home_slot) needs less torque than DEPLOY
        // for M1 — separate gains so tuning one doesn't affect the other.
        // Defaults mirror hold_kp/hold_kd; init() overrides per-slot as needed.
        float park_kp { 5.0f };
        float park_kd { 1.0f };

        // [2026-07-24 per user] PARK ramp trajectory speed (was a single shared
        // local const in go_home_slot) — split per-slot so slowing M1's PARK
        // down doesn't touch M2. Default matches the original shared value.
        float park_speed { 0.45f };

        // [2026-08-18 per user] DEPLOY 的預設 ramp 速度（M1 only）。原本是
        // cmd_deploy_sequence() 裡的 local 預設值，改成成員以便用
        // `M1 SET_DEPLOY_SPEED <v>` 在執行期調整——GUI 的 DEPLOY 按鈕只送
        // `DEPLOY <mm> <slot>`、不帶速度參數，所以唯有改這個預設值才影響得到它
        // （指令列仍可用第 4 個參數做單次覆蓋）。init() 會覆寫成實際採用值。
        float deploy_speed { 0.15f };

        std::atomic<bool> enabled  { false };
        std::atomic<bool> hold_en  { false };
        std::atomic<bool> move_act { false };

        float hold_pos    { 0.0f };
        float move_target { 0.0f };
        float move_cur    { 0.0f };
        float move_speed  { 0.3f };
        float move_tau_ff { 0.0f };   // tau captured at hold→move transition; gravity proxy
        float hold_tau_ff { 0.0f };   // gravity proxy for hold; set at HOLD or move→hold
        float wall_dist   { 0.0f };   // M1: SETWALL value; M2: always 0

        float hold_ki          { 0.0f };   // integral gain; 0 = disabled
        float hold_err_integral{ 0.0f };   // integrator state (protected by motor_mutex_)
        // ❌ [2026-09-02 試過並還原] 曾把此值 2.0 → 5.0，想讓 M2 的積分上限
        //   從 1.2 Nm 提到 3.0 Nm 以跨過摩擦。**實測完全無效，因為瓶頸不是上限、是累積速率**：
        //       積分每 tick 增加 err x dt = 0.07 x 0.02 = 0.0014
        //       ⇒ 繞到 5.0 要 **71 秒**（舊上限 2.0 也要 28 秒）
        //   實測等 6 秒時 tau=0.64 = hold_kp x err(0.49) + tau_i(0.25)，與此完全吻合。
        //   ⇒ **要改的是 hold_kp（或 hold_ki），不是這個 clamp。** 已還原。
        // ⚠️ 命名誤導：本值 clamp 的是**積分狀態（rad·s）**，不是 N·m；
        //   實際力矩上限是 hold_ki x HOLD_I_MAX。
        static constexpr float HOLD_I_MAX = 2.0f;   // anti-windup clamp (積分狀態，非 N·m)

        // [2026-08-13 per user] M2 only: measured half-range from lr_calibrate_slot's
        // two-sided seek (Phase 1 + Phase 1B), replacing the old fixed ZERO_OFFSET
        // assumption for LEFT/RIGHT slot targets in lr_move_to_slot_impl.
        // [2026-08-14 per user] Auto-seek proved unreliable across restarts (and
        // lr_half_range/lr_calibrated don't persist across a motor_api restart —
        // plain in-memory struct fields). Hand-measured via M2 DISABLE -> move by
        // hand -> M2 MIT 0 0 0 0 0 (refresh stale Get_Position cache) -> M2 STATUS
        // at LEFT/CENTER/RIGHT, with `M2 ZERO` called at physical CENTER first so
        // this is measured from the right origin. Using the SMALLER of the two
        // measured half-distances (CENTER->RIGHT=0.7275 vs CENTER->LEFT=0.7603) —
        // the larger one would leave only ~0.005 rad margin on the LEFT target
        // before the real hard stop, too thin. ASSUMES the motor's own zero
        // reference (set via `M2 ZERO`) survives a motor_api restart (appears to,
        // based on bench logs) — if that assumption ever breaks, re-measure and
        // update this constant, or restore the ZERO_OFFSET default and re-run
        // LR_CALIBRATE.
        float lr_half_range { 0.7275f };
        // [2026-09-02] 正向機械停點在當前座標系的位置，與其有效性旗標。
        // 由 lr_calibrate_slot 的 Phase 1 寫入（僅正向 seek 時）；set_zero 後同步平移。
        float lr_stop_pos { 0.0f };
        bool  lr_stop_valid { false };

        // [2026-08-14 per user] M2 only: true once lr_half_range holds a value we
        // actually trust (either a converged two-sided LR_CALIBRATE, or a manual
        // SET_HALF_RANGE). cmd_init_sequence() checks this so repeat INIT calls
        // don't re-run the auto-seek (still unreliable — false-early stops, or
        // seeks that travel huge distances finding no resistance at all) and
        // stomp a good value; once trusted, INIT just moves to CENTER instead.
        // Defaults true here since lr_half_range above is now a real hand-measured
        // value, not the old ZERO_OFFSET placeholder — trust it from first boot.
        bool lr_calibrated { true };

        // [2026-08-17 per user] M1 only: continuous passive-state recovery cooldown
        // for feedback_loop()'s HOLD/MOVE branches. Video evidence showed M1 going
        // fully passive mid-HOLD (not just at the start of a new command, where the
        // existing touch_wall_slot/go_home_slot pre-checks already cover it) and
        // free-falling to near-horizontal with nobody/nothing catching it in
        // software — the user had to grab it by hand. dm_->enable() blocks ~100ms,
        // so this counts down in feedback_loop ticks (20ms each) to avoid calling
        // it every single tick and stalling M2's servicing too.
        int passive_recover_cooldown_ticks { 0 };
    };

    // ---- private slot operations --------------------------------------------
    void        enable_slot(MotorSlot& s);
    void        disable_slot(MotorSlot& s);
    void        set_zero_slot(MotorSlot& s);
    // [2026-07-24 per user] use_park_profile=true (default) = slow/gentle PARK
    // tuning (park_kp/park_kd/park_speed + stop-short-of-hard-limit margin).
    // false = original fast DEPLOY-matching behavior (hold_kp/hold_kd, 0.45
    // rad/s, target=0) — used by cmd_deploy_sequence's internal "retract before
    // re-extending to a new slot" step, which must NOT inherit PARK's tuning.
    // [2026-08-18] Was void. Returns true = actually reached target (within
    // ARRIVE_TOL); false = ramp/settle finished without converging. Callers that
    // release the motor afterwards (cmd_park_sequence) MUST check this — dropping
    // a disable on a still-elevated arm lets it fall. Other call sites may ignore
    // the result, which keeps their previous behavior unchanged.
    bool        go_home_slot(MotorSlot& s, bool use_park_profile = true);
    void        hold_slot(MotorSlot& s);
    void        release_hold_slot(MotorSlot& s);
    void        move_to_slot(MotorSlot& s, float target_rad, float speed_rad_s);
    bool        approach_wall_slot(MotorSlot& s, float clearance_mm, float speed_rad_s);
    bool        touch_wall_slot(MotorSlot& s, float wall_dist_mm, int m2_slot,
                                float clearance_mm, float speed_rad_s);
    bool        lr_calibrate_slot(MotorSlot& s, bool seek_left);   // 2026-07-27: true = converged, false = stop not found / MAX_TRAVEL / Phase 2 not converged
    bool        lr_move_to_slot_impl(MotorSlot& s, int slot, float speed_rad_s);   // 2026-06-06: true=converged

    // 🔴 [2026-09-02 per user] M1 診斷記錄器 —— 查「啟動時頓一下」的成因。
    //   **必須在控制迴圈內記錄**：透過 TCP 輪詢 STATUS 只有 ~10-20 Hz，
    //   而黏滑週期與控制迴圈都是 50 Hz，輪詢抓到的是混疊值
    //   （09-02 曾據此算出正負號互相矛盾的「掙脫力矩」）。
    //   純記錄，不改變任何控制行為。指令：DIAG ON / DIAG OFF（OFF 時寫出 CSV）。
    // [2026-09-02] 擴充為雙軸：M2 的黏滑是「M1 起步踢擊 / 換 slot 慢 / 掃動中被帶著跑」的共同根因，
    //   要動它的增益之前先取得同等品質的波形（M1 的成功經驗就是先量再改）。
    struct DiagRow { float t, pos, vel, tau, cmd; unsigned char flags; unsigned char motor; };
    std::atomic<bool> diag_on_{ false };
    std::vector<DiagRow> diag_rows_;
    std::mutex diag_mtx_;
    std::chrono::steady_clock::time_point diag_t0_;

    bool        calibrate_arm_slot(MotorSlot& s);

    // ---- compound sequences (blocking -- run on client_thread) --------------
    std::string cmd_init_sequence();
    std::string cmd_deploy_sequence(const std::string& params);
    std::string cmd_park_sequence();
    std::string cmd_status_sequence();

    // ---- move-completion poll helper (true=done; false=timeout) -------------
    static bool wait_for_move(MotorSlot& s, int timeout_ms = 15000);

    // ---- TCP internals ------------------------------------------------------
    void        server_loop();
    void        client_thread(socket_t client_sock);
    void        feedback_loop();
    std::string dispatch(const std::string& line);
    std::string dispatch_motor(MotorSlot& s, const std::string& cmd_line);

    // ---- underlying objects -------------------------------------------------
    std::shared_ptr<SerialPort>            serial_;
    std::unique_ptr<damiao::Motor_Control> dm_;

    // ---- motor slots --------------------------------------------------------
    MotorSlot m1_;
    MotorSlot m2_;

    // ---- TCP ----------------------------------------------------------------
    int               tcp_port_    = 9527;
    socket_t          listen_sock_ = INVALID_SOCKET;
    std::atomic<bool> running_     { false };
    std::thread       server_thread_;
    std::thread       feedback_thread_;

    // ---- custom command table -----------------------------------------------
    std::unordered_map<std::string, CommandHandler> cmd_map_;

    // ---- serial port lock (shared by both motors) ---------------------------
    mutable std::mutex motor_mutex_;

#ifdef _WIN32
    bool wsa_ok_ = false;
#endif
};
