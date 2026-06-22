// ============================================================================
// Crane_control_PI — Rooftop crane controller
//
// Hardware (see CLAUDE.md / motion_flow.md §2):
//   RPi @ 192.168.1.101
//
//   USR-TCP232-304 #A @ 192.168.1.30 : 4001 (control bus — both inverters)
//     slave 1 : SE3-210 #1  左鋼索變頻器（士林 SE3）
//     slave 2 : SE3-210 #2  右鋼索變頻器
//     slave 3 : (reserved)  CLV900 middle 絞盤變頻器 (未安裝)
//     slave 4 : (reserved)  SD76 middle 中間管線計米 (未安裝)
//
//   USR-TCP232-304 #B @ 192.168.1.31 : 4001 (sensing bus — both meters)
//     slave 1 : SD76 #1     左鋼索計米
//     slave 2 : SD76 #2     右鋼索計米
//
//   X518 (DSZL-107 #1) directly on switch @ 192.168.1.32 : 502
//     Modbus TCP, internal slave/unit ID = 1, 左鋼索張力 (CH1)
//
//   X518 (DSZL-107 #2) directly on switch @ 192.168.1.33 : 502
//     Modbus TCP, internal slave/unit ID = 1, 右鋼索張力 (CH1)
//
// Topology rationale (2026-05-15 re-layout): control bus and sensing bus
// physically split — USR_A carries ONLY inverter writes (SE3 left + right),
// USR_B carries ONLY meter reads (SD76 left + right). Trade-off vs the older
// "left rope / right rope" split:
//   + Meter polling (every 150-250ms) never blocks SE3 dispatch — fixes the
//     200-300ms dispatch drift seen 2026-05-15 when meter_loop was hammering
//     SD76 timeouts on a shared bus
//   + Single point of meaning per bus: USR_A down = all motion lost / USR_B
//     down = all distance feedback lost (easier to diagnose)
//   - Both SE3 share one RS485 bus → Modbus RTU half-duplex forces the two
//     threads in dual_se3_sync_retry to serialize on TCP_client::socket_mtx.
//     Drift floor ≈ 30-50ms per round-trip (vs theoretical 10ms when each
//     side had its own physical bus). Still 4-10x better than the 200-300ms
//     contention drift on the old layout.
// Tension sensors (X518) speak Modbus TCP natively from their built-in
// Ethernet — no USR gateway needed (path B per 2026-05-08 commission
// decision; original spec assumed RS485+USR).
//
// 2026-05-07: ZS_DIO_R_RLY (relay) replaced by 2x SE3 inverters for left/right
// rope control (variable speed via Modbus, was bang-bang relay).
//
// 2026-05-08: DSZL_107 driver hot-patched RTU+CRC → Modbus TCP+MBAP, X518
// units moved from USR_C/USR_D gateway endpoints to direct switch ports.
// .32/.33 now talk Modbus TCP on :502 directly (was :4001 via USR).
//
// 2026-05-15: bus re-layout — both SE3 on USR_A, both SD76 on USR_B.
// Slave IDs reassigned: SE3 right P.36 1→2, SD76 left slave 2→1.
//
// Command TCP server @ :5002 (line-based text protocol, multi-client)
//   pay_out <cm> / retract <cm>       # 雙繩 + 中間管線同步 (× K)
//   pay_out_left|right <on|off>       # 個別控制（debug）
//   retract_left|right <on|off>
//   up|down on|off                    # hold-to-pull 雙繩同時
//   up_left|up_right on|off           # hold 個別
//   down_left|down_right on|off
//   set_up_stop_total_kg <kg>         # hold-mode 收繩 L+R 總和門檻
//   set_tension_max_kg <kg>           # motion_rope 單側過載硬警報
//   set_tension_diff_max_kg <kg>      # motion_rope 左右張力差硬警報
//   set_retract_tension_stop_kg <kg>  # retract 收繩軟停張力（到了當完成、非錯誤）
//   middle_set <rpm> <pay|retract|stop>
//   zero_meters <ground|top>
//   home_status
//   roll_correct <delta_cm>           # + = 左放右收
//   tension                           # read DSZL-107 left/right kg
//   zero_tension <left|right|all>     # zero a tension channel
//   stop / status / ping
//
// Reply format: OK [data]\n / ERR <reason>\n / EVT <type> <data>\n
//
// Safety: during motion_rope, every poll iter checks tension via DSZL-107.
// On breach (low/high/diff) — broadcast EVT tension_alarm + abort motion.
// ============================================================================

#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#ifndef _WIN32
#include <signal.h>
#endif

#include "TCP_client.h"
#include "TCP_server.h"
#include "SD76_length_meters.h"
#include "CLV900_inverter.h"
#include "DSZL_107.h"
#include "SE3_inverter.h"
#include "PQW_IO_16O_RLY.h"

// ============ Manual-button trace (2026-05-15) ============
// Forward declaration of ts_now() — body defined at ~line 953. HOLD_TRACE macro
// expands inline so must be defined before first use (dual_se3_sync_start at
// ~line 690 uses it). Putting the forward decl + macro here lets later code
// reference HOLD_TRACE freely.
static std::string ts_now();
#define HOLD_TRACE(msg) \
    std::cout << "[" << ts_now() << "] [HOLD-TRACE] " << msg << "\n"

// ============ Hardware config ============

// Network endpoints (post 2026-05-15 physical-separation re-layout):
//   USR_A .30 :4001 — RS485 dedicated to SE3 LEFT only  (1 device, no contention)
//   USR_B .31 :4001 — RS485 dedicated to SE3 RIGHT only (1 device, no contention)
//   USR_M .34 :4001 — RS485 sensing bus, both SD76 meters (no SE3 cross-traffic)
//   DSZL  .32 :502  — X518 #1 direct, left tension      (Modbus TCP native)
//   DSZL  .33 :502  — X518 #2 direct, right tension     (Modbus TCP native)
//
// Physical separation rationale: USR-TCP232-304 is a transparent serial
// server — no Modbus-aware routing, so multi-TCP-client mode broadcasts
// every RS485 reply to all connected clients → frame contamination
// (R socket gets L's reply, fails slave ID check). Avoid by giving each SE3
// its own dedicated USR with single TCP client.
static constexpr const char* USR_A_IP      = "192.168.1.30";   // USR gateway: SE3 left only
static constexpr const char* USR_B_IP      = "192.168.1.31";   // USR gateway: SE3 right only
static constexpr const char* USR_M_IP      = "192.168.1.34";   // USR gateway: SD76 meters (sensing bus)
static constexpr const char* DSZL_LEFT_IP  = "192.168.1.32";   // X518 direct: left tension
static constexpr const char* DSZL_RIGHT_IP = "192.168.1.33";   // X518 direct: right tension
static constexpr int         USR_PORT      = 4001;             // USR-TCP232 transparent port
static constexpr int         DSZL_PORT     = 502;              // X518 native Modbus TCP port
static constexpr int         CMD_PORT      = 5002;

// Slave IDs — see header comment for full mapping.
// 2026-05-15 physical-separation layout:
//   SE3 left = slave 1 (on .30, alone)
//   SE3 right = slave 1 (on .31, alone — 2026-05-26 reverted ID 2→1 because
//                         right SE3 panel P.36 was actually 1 (never set to 2);
//                         since each SE3 has its own bus, ID 1 is fine)
//   SD76 left = slave 1, SD76 right = slave 2 (on .34, shared sensing bus)
//   CLV900 middle (future) = slave 3 (on .30 with SE3 left, mutex serialized)
static constexpr int SE3_LEFT_SLAVE     = 1;   // on USR_A (.30) — alone
static constexpr int SE3_RIGHT_SLAVE    = 1;   // on USR_B (.31) — alone (2026-05-26: 2→1, panel P.36 was always 1)
static constexpr int METER_MIDDLE_SLAVE = 4;   // on USR_M — middle SD76, future install
static constexpr int INVERTER_SLAVE     = 3;   // on USR_A — CLV900 middle winch (future, shares SE3 left bus)

static constexpr int METER_LEFT_SLAVE   = 1;   // on USR_M (.34)
static constexpr int METER_RIGHT_SLAVE  = 2;   // on USR_M (.34)

static constexpr int DSZL_LEFT_SLAVE    = 1;   // X518 internal default unit ID
static constexpr int DSZL_RIGHT_SLAVE   = 1;   // X518 internal default unit ID

// [2026-06-05] Water inlet ball valve relay — moved from washrobot side.
// Shares cli_M (.34) bus with SD76 meters. PQW slave 12, CH4 = ball valve.
static constexpr int PQW_WATER_SLAVE    = 12;  // on USR_M (.34) — shares bus with SD76 meters
static constexpr int CH_WATER_INLET     = 4;   // CH4 of PQW_WATER_SLAVE = ball valve (tank refill from rooftop)
static constexpr int PQW_WATER_TOTAL_CH = 8;   // 8-channel PQW board

// Motion tunables (motion_flow.md §6)
static constexpr int    MOTION_TIMEOUT_MS    = 120000;
static constexpr int    POLL_INTERVAL_MS     = 20;   // 50→20 (2026-05-15): motion_rope main loop tick. atomic cache load only, no extra bus traffic. Reduces stop-trigger lag at 30Hz/30cm-s motion: was ~50ms tick + 150ms cache lag = ~6cm worst, now ~20+100ms = ~3.6cm.
static constexpr double MIDDLE_WINCH_RATIO_K = 1.00;
static constexpr double CLV900_MAX_HZ        = 50.0;  // F8-03 default
static constexpr double SE3_MAX_HZ           = 50.0;  // SE3 upper bound for setFreqHz

// Frequency presets — runtime adjustable via set_hold_hz / set_motion_hz /
// set_middle_hz cmds. Defaults conservative (10 Hz) for first deploy / bench
// validation; bump up via GUI once direction + safety verified.
static constexpr double SE3_HOLD_HZ_DEFAULT      = 10.0;
static constexpr double SE3_MOTION_HZ_DEFAULT    = 30.0;  // 10 → 20 (2026-05-13): user wants faster traversal; rope ~10 cm/s; fine_adjust still 10 Hz for precision tail
static constexpr double MIDDLE_WINCH_HZ_DEFAULT  = 10.0;
static std::atomic<double> g_se3_hold_hz     {SE3_HOLD_HZ_DEFAULT};
static std::atomic<double> g_se3_motion_hz   {SE3_MOTION_HZ_DEFAULT};
static std::atomic<double> g_middle_winch_hz {MIDDLE_WINCH_HZ_DEFAULT};

// Periodic progress EVT during long ops (motion_rope main loop + fine_adjust).
// Lets washrobot's watchdog know crane is alive even while RPC reply is
// pending (long pay_out can take 30+s if fine_adjust kicks in), and gives GUI
// a live view of L/R progress.
static constexpr int EVT_PROGRESS_INTERVAL_MS = 1000;

// Meter-death detection during motion: if g_length_*_valid stays false for
// longer than this (2-3 cache cycles), abort motion. Without it, a dead
// meter means distance check never fires → motor runs until MOTION_TIMEOUT_MS
// (12+ meters of uncontrolled rope at 10Hz). 500ms = 2-3 missed cache reads
// tolerated for transient bus contention.
static constexpr int METER_LOST_GRACE_MS = 500;

// Freeze Hz: at end of main motion loop, instead of stopDecel (which engages
// DC brake → cold-start stall when fine_adjust later tries to redirect),
// drop to FREEZE_HZ. 4Hz chosen to stay above SE3 DC brake start frequency
// (panel 10-00, 3Hz default) — motor keeps running at low Hz, no brake.
//
// 2026-05-12 design: combined with sync-to-midpoint fine_adjust. Motors stay
// in freeze state during fine_adjust direction toggles — SE3 firmware
// handles the 0Hz transit while run cmd stays active throughout.
static constexpr double FREEZE_HZ_DEFAULT = 4.0;
static std::atomic<double> g_freeze_hz {FREEZE_HZ_DEFAULT};

// User bench convention (corrected from bench observation 2026-05-12):
//   pay_out direction → display value INCREASES
//   retract direction → display value DECREASES
// Bench evidence: L=111 R=108 → user wants L retract (display ↓) and R pay_out
// (display ↑), which only works if retract ↓ and pay_out ↑.
// Used by motion_fine_adjust_sync to map "needs display ↑/↓" to motor command.
static constexpr bool PAY_OUT_INCREASES_DISPLAY = true;

// Post-motion fine-adjust (bidirectional, per-side, target-centric).
//
// After motion_rope main loop ends, each side may be at slightly different
// displayed position than its target (ramp distance, cache lag, motor
// asymmetry). For each side:
//   err = current - target
//   |err| ≤ TOLERANCE → already OK, skip
//   err > 0 (went past) → run REVERSE direction by abs(err)
//   err < 0 (didn't reach) → run ORIGINAL direction by abs(err)
// Both sides corrected independently, can run simultaneously even in
// opposite directions if their errors have different signs.
//
// Tolerance: ±2 cm per side (user spec 2026-05-11)
// Speed: fine_adjust_hz (runtime tunable; default 10Hz)
// Timeout: 30 s (each side individually; loop bails if either side stalls)
static constexpr int    FINE_ADJUST_TOLERANCE_CM = 2;
static constexpr double FINE_ADJUST_HZ_DEFAULT   = 10.0;
static constexpr int    FINE_ADJUST_TIMEOUT_MS   = 30000;
static std::atomic<double> g_fine_adjust_hz {FINE_ADJUST_HZ_DEFAULT};

// Kick start: briefly run at higher Hz to break cold-start static friction
// (motor from rest needs more torque to start than to maintain motion).
// After KICK_DURATION_MS, drop to fine_adjust_hz for precision convergence.
// Bench 2026-05-13: cold start at 10Hz works intermittently; 20Hz × 500ms
// gives consistent break-through.
//
// Distance-adaptive (2026-05-14): kick at 20Hz × 500ms moves rope ~10cm
// (20Hz ≈ 20cm/s at typical rope/drum ratio). For short corrections
// (< KICK_DISTANCE_THRESHOLD_CM), this overshoots before convergence loop
// can stop the motor — the kick is a blind sleep, no target check inside.
// Below threshold: skip kick, start directly at precision Hz; short
// distances seem to cold-start at 10Hz fine on bench. Above threshold:
// kick as before.
static constexpr double KICK_HZ_DEFAULT       = 20.0;
static constexpr int    KICK_DURATION_MS      = 500;
static constexpr int    KICK_DISTANCE_THRESHOLD_CM = 15;
static std::atomic<double> g_kick_hz {KICK_HZ_DEFAULT};

// (overshoot compensation removed 2026-05-09 after SD76 calibration completed:
// 1cm displayed = 1cm physical, residual ramp/cache slip is small enough that
// pre-stopping early just under-shoots. motion_rope / cmd_roll_correct now
// stop at exactly target cm. If physical overshoot becomes problematic again,
// tune via SE3 panel P.8 (decel time) or DC injection brake instead of
// application-side compensation.)

// Direction convention (verified on bench 2026-05-08, both left + right cranes
// — see Linux_test menu 23 註解 + menu 25 dual-sync + changelog 2026-05-08g/h):
//   STF (runForward) = retract  收繩 (cup goes up)
//   STR (runReverse) = pay_out  放繩 (cup goes down)
// Counter-intuitive vs the term "forward", caused by motor wiring / P.17 setup.
// Re-verify at low Hz before any production sequence — rewiring or P.17 change
// will flip this back. Linux_test menu 25 has a startup prompt that maps verbs
// to STF/STR if you need to test the inverted case again.
#define SE3_DIR_PAY_OUT(inv)   inv.runReverse()    // STR per bench 2026-05-08
#define SE3_DIR_RETRACT(inv)   inv.runForward()   // STF per bench 2026-05-08

// Watchdog tunables (motion_flow.md §6)
//
// Motion-aware timeout (2026-05-09): motion_rope can take seconds and during
// the start sequence Modbus retries / cli bus contention occasionally produce
// > 2s GUI-side TCP silence (heartbeat = any inbound byte). Pure 2s timeout
// fired mid-motion → abort_flag → "ERR aborted" + EVT watchdog_recovered.
//
// Three regimes:
//   IDLE / hold-active : 2s          fail-safe; hold = press-and-hold, must
//                                    stop fast if GUI disconnects
//   motion_rope main   : dynamic     computed from cm × est_speed + 10s buffer
//                                    (set via g_motion_dynamic_timeout_ms);
//                                    falls back to _MOTION_MIN if 0
//   "motion_rope running" detected via motion_active && !any_hold_active.
//
// 2026-05-12 evolution: was constant 10s, tripped mid pay_out 100 (~50 cm walked
// + ERR aborted). Switched to dynamic: motion_rope/cmd_roll_correct set the
// expected duration at entry. Short motions still get short watchdog (= tight
// fail-safe), long motions get proportional. _MOTION_MIN floor covers tiny
// motions where compute < min (e.g. pay_out 1 cm).
static constexpr int WATCHDOG_TIMEOUT_MS_IDLE       = 2000;
static constexpr int WATCHDOG_TIMEOUT_MS_MOTION_MIN = 10000;
static std::atomic<int> g_motion_dynamic_timeout_ms {0};
static constexpr int HEARTBEAT_CHECK_MS         = 250;

// Tension safety thresholds (motion_flow.md §6.5; tune on site)
// NOTE: TENSION_MIN_KG was used for "low tension = slack/break alarm" but as
// of 2026-05-08 that check is disabled (see tension_safety_check_values). Kept
// here in case low-tension detection is reintroduced after DSZL is calibrated.
static constexpr double TENSION_MIN_KG          = 0.5;   // (currently UNUSED)
// motion_rope tension thresholds. These are only DEFAULTS — the live values are
// the g_tension_max_kg / g_tension_diff_max_kg atomics below, runtime-adjustable
// from the web GUI (set_tension_max_kg / set_tension_diff_max_kg). DSZL-107
// uncalibrated → re-tighten on site after scale factor validated.
static constexpr double TENSION_MAX_KG_DEFAULT      = 100.0; // single-side overload threshold (HARD alarm)
// Left/right imbalance check uses ABSOLUTE kg difference (was percent of avg
// before 2026-05-08; percent at low avg is noisy and false-alarms during hold).
static constexpr double TENSION_DIFF_MAX_KG_DEFAULT = 50.0;  // L/R imbalance threshold (HARD alarm)
// Soft retract-stop threshold. During a RETRACT, rope tension reaching this
// means the slack is collected / rope is taut → stop and finish NORMALLY (a
// successful completion, NOT the hard overload alarm above — no EVT
// tension_alarm). Keep this BELOW TENSION_MAX_KG. Runtime-adjustable via
// set_retract_tension_stop_kg / web.
static constexpr double RETRACT_TENSION_STOP_KG_DEFAULT = 25.0;

// Hold-mode total tension threshold (sum of left+right). When any UP hold is
// active and total exceeds this, hold_loop calls hold_all_off + EVT.
// Placeholder — tune on site after DSZL-107 scale factor validated.
static constexpr double UP_STOP_TOTAL_KG_DEFAULT = 50.0;
static constexpr int    HOLD_LOOP_ACTIVE_MS      = 50;     // poll period when any hold flag set
static constexpr int    HOLD_LOOP_IDLE_MS        = 200;    // poll period when no hold flags (just refresh tension cache)

// ============ Globals ============

// 4 TCP clients, one per gateway. Each runs its own connection / receive
// thread inside TCP_client; they're independent → concurrent reads from
// different gateways have no bus contention with each other.
// 2026-05-15 physical-separation layout (final after split misadventures):
// Each SE3 gets its OWN USR gateway → 1 device per gateway → no possible
// frame contamination from multi-TCP-client broadcast. SD76 meters share
// a third gateway (.34) but no SE3 cross-traffic, so SD76's slave-ID-less
// driver doesn't get poisoned by stray SE3 replies.
//
// History of pain: 14h/i tried 2 TCP sessions to one USR (still .30); R kept
// getting L's replies because USR-TCP232-304 broadcasts to all clients (no
// Modbus-aware routing). 14j reverted to single cli_A (frame contamination
// at init). Final answer: physical bus separation — only way to be sure.
static TCP_client         cli_A;       // .30 — SE3 left only  (+ future CLV900 if installed)
static TCP_client         cli_B;       // .31 — SE3 right only
static TCP_client         cli_M;       // .34 — SD76 meters (sensing bus, both meters share)
static TCP_client         cli_C;       // .32 — X518 left tension  (direct TCP :502)
static TCP_client         cli_D;       // .33 — X518 right tension (direct TCP :502)

static SD76_length_meters meter_left;     // on cli_M slave 1
static SD76_length_meters meter_right;    // on cli_M slave 2
static SD76_length_meters meter_middle;   // on cli_M slave 4 (future install)
static CLV900_inverter    inverter;       // middle winch (cli_A slave 3, future install)
static SE3_inverter       se3_left;       // left rope  (cli_A slave 1)
static SE3_inverter       se3_right;      // right rope (cli_B slave 2)
static DSZL_107           dsz_left;       // left tension (cli_C slave 1)
static DSZL_107           dsz_right;      // right tension (cli_D slave 1)
// [2026-06-05] Water inlet ball valve relay, moved from washrobot to crane side.
// On cli_M (.34) slave 12 CH4 — shares sensing bus with SD76 meters. Bus traffic
// is mostly meter polling (~50-100ms) + occasional relay write (sweep flow).
static PQW_IO_16O_RLY     pqw_water;      // water inlet ball valve (cli_M slave 12 CH4)
static TCP_server         cmd_server;

static std::atomic<bool> abort_flag(false);
static std::mutex        motion_mtx;

// Phase 1 top-zero snapshot: rope length from top to ground.
// Set by `zero_meters top` (= |SD76 left| just before reset).
static std::atomic<int32_t> home_ground_cm(0);

// Watchdog state
static std::atomic<uint64_t> last_ping_ms(0);     // 0 = no activity yet
static std::atomic<bool>     motion_active(false);
static std::atomic<bool>     watchdog_fired(false);
static std::atomic<bool>     watchdog_stop(false);
static std::thread           watchdog_thread;

// Init completion flag — set true at end of main()'s init sequence (after all
// driver init + meter scale cache + helper threads spawned). GUI reads via
// cmd_status to grey out action buttons during early boot. Stays true for the
// rest of the process lifetime.
static std::atomic<bool>     g_init_done(false);

// Hold-mode state (press-and-hold buttons in GUI)
//   Each flag tracks whether the corresponding relay is held ON by GUI.
//   Cleared either by GUI sending "off", by hold_loop on safety breach,
//   or by watchdog on TCP silence timeout.
static std::atomic<bool> hold_up_left(false);     // se3_left  retract (motor reverse)
static std::atomic<bool> hold_up_right(false);    // se3_right retract
static std::atomic<bool> hold_down_left(false);   // se3_left  pay_out (motor forward)
static std::atomic<bool> hold_down_right(false);  // se3_right pay_out
static std::atomic<bool> hold_loop_stop(false);
static std::thread       hold_thread;

// Tension cache (updated by hold_loop, read by cmd_status / cmd_tension)
static std::atomic<double> g_tension_left  {0.0};
static std::atomic<double> g_tension_right {0.0};
static std::atomic<bool>   g_tension_valid {false};

// SD76 length cache (updated by meter_loop, read by cmd_status / cmd_home_status).
// Without this, every cmd_status hit 3 Modbus FC03 reads (3 × ~150ms via USR
// gateway) = ~450ms per status. GUI auto-polls status at 200ms so processing
// can't keep up → dispatcher queue grows unboundedly → user button presses
// stall behind a backlog of status calls (observed >1 min lag on 2026-05-08).
//
// _valid mirrors per-meter availability/read success. cmd_status falls back
// to "ERR" string when invalid (matches pre-cache behavior on Modbus failure).
static std::atomic<int32_t> g_length_left   {0};
static std::atomic<int32_t> g_length_right  {0};
static std::atomic<int32_t> g_length_middle {0};
static std::atomic<bool>    g_length_left_valid   {false};
static std::atomic<bool>    g_length_right_valid  {false};
static std::atomic<bool>    g_length_middle_valid {false};
static std::atomic<bool>    g_meter_loop_stop {false};
static std::thread          g_meter_thread;

// SE3 keepalive thread: when motion or hold is active, ping both SE3 every
// ~1s with a cheap readStatusWord. Resets SE3's internal "通訊出錯次數" counter
// (07-08) so it never accumulates to trigger OPT alarm (when 07-10 = 0).
//
// Why needed: our normal code path doesn't talk to SE3 continuously during:
//   - freeze period (after one side reaches cm, balance disabled, no writes)
//   - fine_adjust loop (only reads meter cache, no SE3 writes)
//   - hold mode (initial run cmd then just monitors tension)
//
// With 07-09 (comm timeout window) = 2s, these silent periods would trip OPT.
// Keepalive at 1s cadence keeps SE3 happy while still allowing 07-10 = 0
// (alarm + coast stop on real disconnect) for safety.
static constexpr int SE3_KEEPALIVE_INTERVAL_MS = 1000;  // 500→1000 (2026-05-15): test if keepalive interleaving with cmd_hold's sync_start causes Phase B L 316ms retry. middle disabled (2026-05-14e) reduced cli_A traffic so 1s should still beat 07-09 (~2s) safely.
static std::atomic<bool>    g_se3_keepalive_stop {false};
static std::thread          g_se3_keepalive_thread;

// SE3 fault state tracking. Set/cleared by se3_keepalive_loop based on
// status word bit 7. Read by motion_rope / cmd_roll_correct main loops to
// abort BOTH sides when either side is in fault (e.g., OPT alarm).
// Safety principle: if one rope motor can't run, stop the other immediately
// to prevent asymmetric load on the crane structure.
static std::atomic<bool> g_se3_left_fault  {false};
static std::atomic<bool> g_se3_right_fault {false};
// SD76 poll period — fast in idle (GUI gets fresh length), aggressive in
// motion to minimize cache lag for distance-based stop decisions.
//
// History:
//   1000ms → motion_rope overshoot ~10cm @ 10 Hz (cache lag dominates)
//   200ms  → overshoot still ~10 cm at low cm targets (eff_cm + ramp dominate)
//   50ms   → cache lag drops to ~50ms worst, freeing distance precision to
//            be limited by SE3 ramp / DC brake (the unfixable physics part)
//   150ms  → mitigation for cli_A bus contention (2026-05-14): pre-re-layout,
//            cli_A carried 3+ devices (SE3_left + SD76_left + SD76_middle +
//            CLV900). 50ms polling queued SE3 H1001/setFreq writes long enough
//            that L-side keepalive saw 6% fail rate. 150ms cut traffic ~67%.
//
// 2026-05-15 physical-separation layout: each SE3 has its own gateway
// (cli_A → SE3 left, cli_B → SE3 right); SD76 meters share cli_M (sensing
// bus, no SE3 cross-traffic). meter_loop polls SD76 left + right on cli_M.
// SE3 dispatch is fully isolated from meter reads — 150ms motion cadence
// is preserved for fine_adjust cache freshness rather than contention
// mitigation.
//
// SE3 startup writeParam contention covered by reliable_start_one 8x100ms
// retry (2026-05-09i). Balance trim setFreqHz writes have no retry — single
// device per gateway means no intra-bus serialization delay (was ~30-50ms
// on shared cli_A pre-physical-separation).
static constexpr int        METER_POLL_MS_IDLE   = 250;
static constexpr int        METER_POLL_MS_MOTION = 100;   // 150→100 (2026-05-15 physical-separation): cli_M now dedicated to SD76 only (no SE3 cross-traffic), so faster polling has plenty of bus headroom. Cuts cache lag worst-case from 150ms to 100ms → smaller stop overshoot.

// Total UP threshold (atomic for runtime adjustment via set_up_stop_total_kg)
static std::atomic<double> g_up_stop_total_kg {UP_STOP_TOTAL_KG_DEFAULT};
// motion_rope tension thresholds (atomic for runtime adjustment via web —
// set_tension_max_kg / set_tension_diff_max_kg / set_retract_tension_stop_kg)
static std::atomic<double> g_tension_max_kg          {TENSION_MAX_KG_DEFAULT};
static std::atomic<double> g_tension_diff_max_kg     {TENSION_DIFF_MAX_KG_DEFAULT};
static std::atomic<double> g_retract_tension_stop_kg {RETRACT_TENSION_STOP_KG_DEFAULT};

// ============ Dynamic balance (length-diff feedback) ============
//
// Both ropes nominally run at g_se3_motion_hz (or g_se3_hold_hz for hold mode).
// Open-loop, two SE3 inverters drift apart over time → after a few meters one
// side is visibly ahead. balance_loop tweaks each SE3's freq based on the
// length-diff error to pull the lagging side faster + slow the leading side.
//
// Control law (P controller, symmetric split):
//   error    = direction * (length_left - length_right)   // + = left ahead
//              direction = +1 for pay_out, -1 for retract (length grows / shrinks)
//   cap      = base_hz * cap_ratio                          (cap_ratio default 0.5)
//   trim     = clamp(Kp * error, -cap, +cap)
//   hz_max   = base_hz + hz_max_offset                      (offset default 5)
//   left_hz  = clamp(base_hz - trim/2, hz_min, hz_max)
//   right_hz = clamp(base_hz + trim/2, hz_min, hz_max)
//
// Symmetric split: total speed roughly preserved (both ends move toward
// midpoint simultaneously). Within deadband: skip — avoids spamming SE3
// setFreqHz with tiny changes (cli_A bus pressure). When trim returns to
// zero, write base_hz back once via was_trimmed latch.
//
// Engaged contexts:
//   - motion_rope (auto pay_out / retract <cm>): always
//   - hold_loop (held button) when both same-direction flags set
//
// Disengaged when balance_enabled=false, length cache invalid, or only one
// side held in hold mode (asymmetric debug). On disengage with was_trimmed
// latched, reset both SE3 to base_hz once so they don't keep their last trim.
static constexpr double BALANCE_KP_DEFAULT       = 1.0;   // Hz / cm
// Cap is a RATIO of base_hz (was absolute Hz prior to 2026-05-14). Effective
// trim cap = base_hz × ratio. Each side gets ±cap/2. Scales naturally with
// motion speed: at base 10 Hz, ratio 0.5 → cap 5 Hz → L=7.5/R=12.5; at base
// 5 Hz, same ratio → cap 2.5 Hz → L=3.75/R=6.25 (avoids near-zero hz on slow
// side at low motion speed). Per-side hz still clamped by hz_min/hz_max.
static constexpr double BALANCE_TRIM_CAP_RATIO_DEFAULT = 0.5;
static constexpr double BALANCE_DEADBAND_DEFAULT = 1.0;   // cm, |err| below = no trim
static constexpr double BALANCE_HZ_MIN_DEFAULT       = 5.0;   // Hz, hard floor on per-side hz after trim (2→5 on 2026-05-15: avoid near-stall on slow side under aggressive cap)
// Note: hz_max changed from absolute Hz (30) to **offset above base_hz** (default 5).
// effective hz_max = base_hz + offset. base_hz comes from apply_balance_trim caller
// (g_se3_hold_hz or g_se3_motion_hz). With base=10Hz → max=15Hz; base=30Hz → max=35Hz.
// Reason (2026-05-15): old absolute 30Hz cap meant balance couldn't speed R above
// motion_hz=30, so balance could only slow L (asymmetric authority). Offset-based
// gives symmetric ±5Hz headroom around base regardless of how fast user sets base.
static constexpr double BALANCE_HZ_MAX_OFFSET_DEFAULT = 5.0;  // Hz above base_hz, NOT absolute
static constexpr int    BALANCE_TICK_MS          = 250;   // 500→250 (2026-05-15 physical-separation): each SE3 has dedicated USR/RS485 bus, so balance setFreq writes don't compete with anything. 4 Hz tick reacts faster to mid-motion drift; setFreq Modbus rate ~4/s/side is well within bus capacity.
static std::atomic<bool>   g_balance_enabled  {true};
static std::atomic<double> g_balance_kp        {BALANCE_KP_DEFAULT};
static std::atomic<double> g_balance_trim_cap_ratio  {BALANCE_TRIM_CAP_RATIO_DEFAULT};
static std::atomic<double> g_balance_deadband  {BALANCE_DEADBAND_DEFAULT};
static std::atomic<double> g_balance_hz_min           {BALANCE_HZ_MIN_DEFAULT};
static std::atomic<double> g_balance_hz_max_offset    {BALANCE_HZ_MAX_OFFSET_DEFAULT};   // semantic: above base_hz

// DSZL-107 raw → kg scale per side (atomic for runtime adjustment via
// set_dsz_scale). Default NEGATIVE — bench observation 2026-05-08:
// "force ↑ → raw ↓" on left cell (right untested but assumed same wiring).
// Sign-flip via negative scale is equivalent to swapping the differential
// signal pair on X518's input terminal, but doesn't require unwiring.
//
// Magnitude 0.01 matches the DSZL_107 driver default; once a known weight
// has been hung on each cell, recompute as `kg / (loaded_raw - zero_raw)`
// (signed) and update via GUI.
static constexpr double DSZL_SCALE_DEFAULT  = -0.01;
static std::atomic<double> g_dsz_left_scale  {DSZL_SCALE_DEFAULT};
static std::atomic<double> g_dsz_right_scale {DSZL_SCALE_DEFAULT};

// SD76 length meter calibration — device-side (SD76 EEPROM via SCAL/DP regs).
// Driver: SD76_length_meters::scaleByRatio() / readScale() / writeScale().
//
// Calibration flow (cal_zero + cal_set):
//   1. pull rope to baseline position
//   2. cal_zero left           → snapshot current displayed cm into baseline
//   3. pull rope a measured distance (e.g., exactly 100 cm by tape)
//   4. cal_set left 100        → ratio = 100 / (now - baseline);
//                                  driver: device SCAL ×= ratio (in EEPROM);
//                                  no local file
// Direct override (advanced):
//   set_meter_scale left 1.0526 → driver writes effective multiplier directly.
//
// Calibration persists in SD76 EEPROM → travels with the device, survives
// Pi reboot / reimage. Replace SD76 → recalibrate.
//
// g_meter_*_device_scale: atomic cache of last-read effective scale (refreshed
// at init, after cal_set, and on explicit read_meter_scale cmd). cmd_status
// exposes for GUI display; not used by motion logic (SD76 already applies it
// before reporting cm).
static std::atomic<double> g_meter_left_device_scale   {1.0};
static std::atomic<double> g_meter_right_device_scale  {1.0};
static std::atomic<double> g_meter_middle_device_scale {1.0};
static std::atomic<bool>   g_meter_left_scale_valid    {false};
static std::atomic<bool>   g_meter_right_scale_valid   {false};
static std::atomic<bool>   g_meter_middle_scale_valid  {false};

// cal_zero baseline snapshot per side, in DISPLAYED cm (post-scale, since SD76
// already applies its EEPROM scale before display). INT32_MIN sentinel =
// "not yet zeroed"; cal_set returns ERR until cal_zero has been called.
// Transient state — not persisted (the resulting SCAL is what persists, in SD76).
static constexpr int32_t METER_CAL_UNSET = INT32_MIN;
static std::atomic<int32_t> g_meter_left_cal_baseline   {METER_CAL_UNSET};
static std::atomic<int32_t> g_meter_right_cal_baseline  {METER_CAL_UNSET};
static std::atomic<int32_t> g_meter_middle_cal_baseline {METER_CAL_UNSET};

// Device availability flags — set during init. False means the corresponding
// gateway didn't connect or device init() failed; commands that require the
// device return "ERR <name>_unavailable" instead of attempting Modbus calls.
//
// Implements graceful degradation per 2026-05-08 bench finding (one USR
// gateway down ≠ system down — other bus continues). Post-2026-05-15 bus
// re-layout, the failure modes split cleanly:
//   USR_A down → 所有 SE3 motion 失能（含 motion_rope / hold / roll_correct）
//                ，但 SD76 length 與 DSZL tension 仍正常讀取
//   USR_B down → 所有 SD76 length 失能（自動距離 motion 失能、hold 仍可用），
//                SE3 / DSZL 不受影響
//
// Per-gateway flags reflect TCP connect success; per-device flags reflect
// driver init() success (which requires the gateway to be up first).
//
// Flags are set once at startup. If hardware is fixed mid-run, restart crane
// to re-init. (TCP_client's reconnect loop handles transient network blips,
// but driver-level state isn't re-claimed automatically.)
static std::atomic<bool> g_gw_a_ok           {false};   // USR_A .30 — SE3 left
static std::atomic<bool> g_gw_b_ok           {false};   // USR_B .31 — SE3 right
static std::atomic<bool> g_gw_m_ok           {false};   // USR_M .34 — SD76 meters
static std::atomic<bool> g_gw_c_ok           {false};   // USR_C .32 — DSZL left
static std::atomic<bool> g_gw_d_ok           {false};   // USR_D .33 — DSZL right
static std::atomic<bool> g_dev_se3_left      {false};
static std::atomic<bool> g_dev_se3_right     {false};
static std::atomic<bool> g_dev_meter_left    {false};
static std::atomic<bool> g_dev_meter_right   {false};
static std::atomic<bool> g_dev_meter_middle  {false};
static std::atomic<bool> g_dev_clv900        {false};
static std::atomic<bool> g_dev_dsz_left      {false};
static std::atomic<bool> g_dev_dsz_right     {false};
static std::atomic<bool> g_dev_pqw_water     {false};   // [2026-06-05] PQW water-inlet relay (cli_M slave 12)

// Build EVT device_state line reflecting current device flags. Broadcast at
// startup (after init complete) and from cmd_status for fresh GUI sync.
static std::string make_device_state_line() {
    std::ostringstream oss;
    oss << "EVT device_state"
        << " gw_a="        << (g_gw_a_ok.load()         ? 1 : 0)
        << " gw_b="        << (g_gw_b_ok.load()         ? 1 : 0)
        << " gw_m="        << (g_gw_m_ok.load()         ? 1 : 0)
        << " gw_c="        << (g_gw_c_ok.load()         ? 1 : 0)
        << " gw_d="        << (g_gw_d_ok.load()         ? 1 : 0)
        << " se3_left="    << (g_dev_se3_left.load()    ? 1 : 0)
        << " se3_right="   << (g_dev_se3_right.load()   ? 1 : 0)
        << " meter_left="  << (g_dev_meter_left.load()  ? 1 : 0)
        << " meter_right=" << (g_dev_meter_right.load() ? 1 : 0)
        << " meter_middle="<< (g_dev_meter_middle.load()? 1 : 0)
        << " clv900="      << (g_dev_clv900.load()      ? 1 : 0)
        << " dsz_left="    << (g_dev_dsz_left.load()    ? 1 : 0)
        << " dsz_right="   << (g_dev_dsz_right.load()   ? 1 : 0)
        << " pqw_water="   << (g_dev_pqw_water.load()   ? 1 : 0)
        << "\n";
    return oss.str();
}

// ============ Meter scale (device-side helper) ============

// Refresh atomic cache for one side from device. Called at init + after
// cal_set / set_meter_scale + on explicit read_meter_scale cmd.
// Returns true on Modbus failure (cache unchanged, valid flag cleared).
static bool refresh_meter_scale_cache(SD76_length_meters& m,
                                       std::atomic<double>& cache,
                                       std::atomic<bool>&   valid) {
    double s = 1.0;
    if (m.getEffectiveScale(s)) {
        valid.store(false);
        return true;
    }
    cache.store(s);
    valid.store(true);
    return false;
}

// ============ Low-level helpers ============

// Forward declarations (definitions later in the file)
static void hold_all_off();
static void allMotionOff();

// Reliable start: setFreqHz + run direction as one unit, retry on transient
// failure (TCP packet loss / SE3 firmware busy / Modbus contention timeout).
//
// Same rationale as reliable_stop_one — without retry the first transient
// fail surfaces as "ERR se3_*_start_fail" to the GUI even though the next
// attempt 80ms later would have succeeded. With retry the user just sees
// the motor start (typically ≤200ms even when the first attempt failed).
//
// 8 attempts × 100ms backoff = up to ~900ms wall time worst case before
// giving up. setFreqHz writes RAM (cheap, idempotent) so re-setting freq on
// retry is safe even if the previous freq write succeeded.
//
// Bumped from 5x80ms to 8x100ms 2026-05-09 — Sadie observed manual cmd
// "sometimes doesn't move" on either side randomly, indicating 5x80=400ms
// wasn't covering the full transient envelope (SE3 CU-mode reclaim + cli
// bus contention can stretch beyond 500ms during meter_loop polling).
// Watchdog motion timeout 10000ms / idle 2000ms — 900ms still safe.
//
// Returns true iff all 8 attempts failed (motor didn't start; caller should
// rollback any other side that did start).
static bool reliable_start_one(SE3_inverter& inv, double hz, bool pay_out) {
    constexpr int MAX_ATTEMPTS = 8;
    for (int i = 0; i < MAX_ATTEMPTS; ++i) {
        if (!inv.setFreqHz(hz, SE3_MAX_HZ)) {
            bool runErr = pay_out ? SE3_DIR_PAY_OUT(inv) : SE3_DIR_RETRACT(inv);
            if (!runErr) return false;
        }
        if (i < MAX_ATTEMPTS - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return true;
}

// Reliable stopDecel: stopDecel can fail transiently (TCP packet loss / SE3
// firmware momentary busy / RS485 jitter / Modbus contention beyond timeout).
// Without retry, the user sees "released button but motor didn't stop" and
// has to release+re-engage to trigger a fresh stopDecel — the second attempt
// usually succeeds because the transient is gone. Move that retry inside the
// helper so the user never sees the transient as visible motor behavior.
//
// 8 attempts × 100ms backoff = up to ~900ms wall time worst case before giving
// up. SE3_inverter::stopDecel already has its own internal CU-mode watchdog
// reclaim (kicks in after 2 consecutive H1001 fails) so each attempt may
// itself trigger driver-level retry — this outer loop covers transients the
// driver-level watchdog can't recover from in a single invocation.
//
// Stop is more critical than start (failed stop = motor keeps running, dangerous;
// failed start = user re-presses), so kept symmetric with reliable_start_one
// at 8x100ms (bumped from 5x80ms 2026-05-09 alongside start retry upgrade).
//
// Returns true iff all 8 attempts failed (caller must treat as hard error;
// motor likely still running — escalation would be a separate decision).
static bool reliable_stop_one(SE3_inverter& inv) {
    constexpr int MAX_ATTEMPTS = 8;
    for (int i = 0; i < MAX_ATTEMPTS; ++i) {
        if (!inv.stopDecel()) return false;
        if (i < MAX_ATTEMPTS - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return true;
}

// Dispatch the same SE3 method on left + right inverters concurrently via
// std::thread. Keeps stop / run timing symmetric from the user's POV even
// when one side hits a driver-level retry path (watchdog CU re-claim /
// stale-buffer recovery / Modbus retransmit) that adds 0.5-2 seconds latency.
//
// Sequential dispatch made the slower side appear to "lag" the faster side
// by whatever retry latency happened to land on it — total wall time was
// T_left + T_right and the second side started its decel that much later.
// Concurrent dispatch caps wall time at max(T_left, T_right) and both sides
// receive their command at the same moment.
//
// Both threads are short-lived (one Modbus round trip each, ~50ms typical,
// up to ~600ms worst case under watchdog reclaim). Spawn cost negligible.
//
// USE FOR: single-shot ops where retry is not desired (emergencyStop,
// stopDecel for MRS clear). For retry-based ops (setFreq / run cmd / stop
// with retry), use dual_se3_sync_retry below — that bounds inter-side drift
// even when one side needs retry, while this template would let the faster
// side return immediately and the slower side keep retrying solo.
template <typename Fn>
static bool dual_se3_concurrent(Fn fn) {
    bool eL = false, eR = false;
    const auto t0 = std::chrono::steady_clock::now();
    auto tL_done = t0, tR_done = t0;
    std::thread tL([&]{ eL = fn(se3_left);  tL_done = std::chrono::steady_clock::now(); });
    std::thread tR([&]{ eR = fn(se3_right); tR_done = std::chrono::steady_clock::now(); });
    tL.join();
    tR.join();
    // Diagnostic: if one side's command dispatch took much longer than the
    // other, log the wall times. Helps distinguish software-side asymmetry
    // (driver-level retry / watchdog reclaim on one side) from motor-side
    // asymmetry (panel P.8 decel ramp / DC brake setup not aligned).
    const auto durL = std::chrono::duration_cast<std::chrono::milliseconds>(tL_done - t0).count();
    const auto durR = std::chrono::duration_cast<std::chrono::milliseconds>(tR_done - t0).count();
    if (std::abs(durL - durR) > 100) {
        std::cout << "[dual_se3_concurrent] asymmetric cmd dispatch L=" << durL
                  << "ms R=" << durR << "ms (errL=" << eL << " errR=" << eR << ")\n";
    }
    return eL || eR;
}

// Attempt-synchronized retry for paired SE3 commands. Both threads run a
// single attempt concurrently, both wait for each other to finish (barrier),
// then both retry together when EITHER side failed. Bounds inter-side drift
// to one attempt's wall time even when only one side hits a transient — vs
// the old independent-retry pattern (reliable_*_one ran in two threads via
// dual_se3_concurrent) where the failing side could run solo for N × attempt
// cycles (300-700ms drift typical) while the other side retried independently.
//
// NOTE (2026-05-15 physical-separation layout): SE3_left and SE3_right are
// on PHYSICALLY SEPARATE USR gateways (cli_A → USR_A .30, cli_B → USR_B .31).
// No mutex contention, no shared bus, no USR multi-client broadcast issue —
// the two SE3 commands literally travel through different cables to different
// SE3 inverters. Drift floor is now bounded only by thread scheduling jitter
// (~1ms) plus per-side Modbus RTT variance (~10-50ms).
//
// History of pain (so we don't repeat): 14h tried 2 TCP sessions on one USR
// (.30) → USR-TCP232-304 broadcasts RS485 replies to all clients (no Modbus
// routing) → R received L's frames → driver rejected → intermittent fail.
// 14j reverted to single cli_A → init chicken-and-egg with OPT residual.
// 14m driver init Mode B fix solved init issue. THIS layout (separate USR per
// SE3) eliminates the broadcast issue entirely at hardware level.
//
// Operations MUST be idempotent — the side that succeeded on attempt 1 will
// re-issue the same command on attempt 2 if the other side failed. SE3
// commands used here (setFreqHz to H1002, runForward/runReverse setting STF/
// STR bits in H1001, stopDecel clearing H1001) are all level-triggered, so
// re-issuing the same value is firmware-side no-op.
//
// CAVEAT for run cmd (Phase B): residual mechanical drift = ~1 attempt cycle
// when one side's H1001 write succeeds attempt 1 and other fails attempt 1.
// The succeeded side's motor IS running from attempt 1, and re-issuing on
// attempt 2 doesn't restart it. Net effect: drift reduced from 300-700ms
// (independent retry) to ~150ms (one recv timeout cycle). Eliminating the
// remaining 150ms would require commit-or-rollback (stop succeeded side,
// retry both fresh) — not done because the mechanical cost (200ms decel +
// 1-2s DC brake reset + re-accel) is worse than the 150ms drift.
//
// fnL / fnR: per-side single-attempt functions returning true on failure.
// max_attempts / backoff_ms: as in reliable_*_one (typically 8x100 or 2x50).
// tag: log identifier, prints on retry / failure / drift > 100ms.
template <typename FnL, typename FnR>
static bool dual_se3_sync_retry(FnL fnL, FnR fnR,
                                int max_attempts, int backoff_ms,
                                const char* tag) {
    bool eL = false, eR = false;
    int last_attempt = 0;
    int64_t last_durL_ms = 0, last_durR_ms = 0;
    const auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < max_attempts; ++i) {
        last_attempt = i + 1;
        bool atL = false, atR = false;
        const auto tA0 = std::chrono::steady_clock::now();
        auto tL_done = tA0, tR_done = tA0;
        std::thread tL([&]{ atL = fnL(se3_left);  tL_done = std::chrono::steady_clock::now(); });
        std::thread tR([&]{ atR = fnR(se3_right); tR_done = std::chrono::steady_clock::now(); });
        tL.join();
        tR.join();

        last_durL_ms = std::chrono::duration_cast<std::chrono::milliseconds>(tL_done - tA0).count();
        last_durR_ms = std::chrono::duration_cast<std::chrono::milliseconds>(tR_done - tA0).count();
        eL = atL;
        eR = atR;

        if (!atL && !atR) break;                           // both succeeded — done
        if (i < max_attempts - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    const int64_t drift = std::abs(last_durL_ms - last_durR_ms);

    if (last_attempt > 1 || eL || eR || drift > 100) {
        std::cout << "[sync_retry:" << tag << "] attempts=" << last_attempt
                  << "/" << max_attempts
                  << " final L=" << last_durL_ms << "ms R=" << last_durR_ms << "ms"
                  << " drift=" << drift << "ms"
                  << " total=" << total_ms << "ms"
                  << " errL=" << eL << " errR=" << eR << "\n";
    }
    return eL || eR;
}

// Convenience overload: same single-attempt fn used on both sides (most cases).
template <typename Fn>
static bool dual_se3_sync_retry(Fn fn, int max_attempts, int backoff_ms,
                                const char* tag) {
    return dual_se3_sync_retry<Fn, Fn>(fn, fn, max_attempts, backoff_ms, tag);
}

// setFreqHz only with retry (no run command). Used by dual_se3_sync_start
// Phase A so the variable retry latency is absorbed BEFORE the run command,
// not bundled with it. Returns true if all attempts failed.
static bool reliable_setfreq_one(SE3_inverter& inv, double hz) {
    constexpr int MAX_ATTEMPTS = 8;
    for (int i = 0; i < MAX_ATTEMPTS; ++i) {
        if (!inv.setFreqHz(hz, SE3_MAX_HZ)) return false;
        if (i < MAX_ATTEMPTS - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return true;
}

// Per-side run cmd with bounded retry. Default 2 attempts × 50ms used by
// Phase B of dual_se3_sync_start (sync-sensitive, keeps L/R drift small).
// Callers that fire RUN after a recent stopDecel (motion_rope → fine_adjust,
// correction pass after reversal lockout) override with larger window:
//   - fine_adjust entry: 6 × 200ms = 1200ms window covers SE3 stop ramp +
//     DC brake injection (P.8 decel + P.55-57 brake duration, ~1-2s)
//   - correction reversal: 6 × 200ms additionally absorbs firmware reversal
//     lockout tail (already settled 2s outside, retry catches residual)
static bool reliable_run_one(SE3_inverter& inv, bool pay_out,
                             int max_attempts = 2, int backoff_ms = 50) {
    for (int i = 0; i < max_attempts; ++i) {
        const bool err = pay_out ? SE3_DIR_PAY_OUT(inv) : SE3_DIR_RETRACT(inv);
        if (!err) return false;
        if (i < max_attempts - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        }
    }
    return true;
}

// Two-phase sync-start for dual rope inverters.
//   Phase A: setFreqHz on both (concurrent, with per-side retry — variable
//            latency absorbed here before run command fires)
//   Phase B: run command on both (concurrent, with bounded 2x50ms retry —
//            drift between sides ≤ ~100ms vs the old reliable_start_one
//            0-700ms drift when transient hit on one side only)
//
// Trade-off vs reliable_start_one:
//   + Better sync (run cmds fired within ~10-100ms even with 1 retry)
//   + Reasonable reliability (2 attempts handles typical SE3 transients)
//   - Worst-case drift 100ms if one side needs retry; vs original 0ms drift
//     but high fail rate (bench: ~30% Phase B fails seen 2026-05-09)
//
// Returns true (error) if either phase fails after retry. On any failure,
// both sides get stopDecel to leave a clean state for the next attempt.
static bool dual_se3_sync_start(double hz, bool left_pay_out, bool right_pay_out) {
    HOLD_TRACE("sync_start ENTRY hz=" << hz << " L_pay=" << left_pay_out
               << " R_pay=" << right_pay_out);

    // Pre-flight: clear any residual OPT/fault on either SE3 before issuing
    // motion cmds. Without this, "happen to be in OPT right now" coincidence
    // (especially on bench where R triggers OPT every ~4s — see keepalive
    // clears=8/30s) causes Phase B to fail because SE3 firmware rejects
    // run cmds while in fault state. Cheap (~150ms each side, only on fault).
    // Atomic check first (read-only) — no Modbus traffic if both sides clean.
    const bool l_fault = g_se3_left_fault.load();
    const bool r_fault = g_se3_right_fault.load();
    if (l_fault || r_fault) {
        HOLD_TRACE("sync_start pre-flight: clearAlarm L=" << l_fault << " R=" << r_fault);
        std::cout << "[sync_start] pre-flight clearAlarm L_fault=" << l_fault
                  << " R_fault=" << r_fault << "\n";
        if (l_fault) se3_left.clearAlarm();
        if (r_fault) se3_right.clearAlarm();
        // clearAlarm itself has internal sleep_for(200ms) so SE3 firmware has
        // time to settle before Phase A fires. No additional sleep needed.
    }

    // Phase A: setFreqHz with attempt-synchronized retry. setFreqHz is idempotent
    // (H1002 RAM write, no motor side-effect) so both threads re-issue together
    // on either-side failure — bounds drift even when one side hits a transient.
    HOLD_TRACE("sync_start Phase A start (sync setFreq L+R)");
    const auto tA0 = std::chrono::steady_clock::now();
    auto setfreq_fn = [hz](SE3_inverter& inv) { return inv.setFreqHz(hz, SE3_MAX_HZ); };
    const bool freq_err = dual_se3_sync_retry(setfreq_fn, 8, 100, "sync_start.setfreq");
    const auto tA1 = std::chrono::steady_clock::now();
    const auto durA = std::chrono::duration_cast<std::chrono::milliseconds>(tA1 - tA0).count();
    std::cout << "[sync_start] Phase A setFreq total=" << durA << "ms err=" << freq_err
              << " (sync_retry — see [sync_retry:sync_start.setfreq] for per-attempt detail)\n";
    if (freq_err) {
        HOLD_TRACE("sync_start EXIT (Phase A fail)");
        std::cout << "[sync_start] Phase A FAILED — abort\n";
        return true;
    }
    HOLD_TRACE("sync_start Phase A OK -> Phase B start (sync run L+R)");

    // Phase B: run command, attempt-synchronized retry. Per-side direction may
    // differ (roll_correct: L pay, R retract) so two separate lambdas captured.
    // Residual mechanical drift ≤ 1 attempt cycle (~150ms) — see dual_se3_sync_retry
    // doc for why eliminating that would require commit-or-rollback.
    const auto tB0 = std::chrono::steady_clock::now();
    auto run_left_fn  = [left_pay_out](SE3_inverter& inv)  {
        return left_pay_out  ? SE3_DIR_PAY_OUT(inv) : SE3_DIR_RETRACT(inv);
    };
    auto run_right_fn = [right_pay_out](SE3_inverter& inv) {
        return right_pay_out ? SE3_DIR_PAY_OUT(inv) : SE3_DIR_RETRACT(inv);
    };
    const bool run_err = dual_se3_sync_retry(run_left_fn, run_right_fn, 2, 50, "sync_start.run");
    const auto tB1 = std::chrono::steady_clock::now();
    const auto durB = std::chrono::duration_cast<std::chrono::milliseconds>(tB1 - tB0).count();
    std::cout << "[sync_start] Phase B run total=" << durB << "ms err=" << run_err
              << " (L_pay=" << left_pay_out << " R_pay=" << right_pay_out
              << " — see [sync_retry:sync_start.run] for per-attempt detail)\n";
    if (run_err) {
        // One side may already be running (asymmetric rope = robot tilt risk).
        // EMERGENCY abort path — emergencyStop (H1001 b7 MRS = output cutoff)
        // kills motor power in ~50ms vs ~800ms+ for decel. Then clear MRS so
        // next run cmd works normally.
        HOLD_TRACE("sync_start Phase B FAIL -> EMERGENCY stop both -> EXIT");
        std::cout << "[sync_start] Phase B failed after retry — EMERGENCY stop both\n";
        dual_se3_concurrent([](SE3_inverter& inv){ return inv.emergencyStop(); });
        dual_se3_concurrent([](SE3_inverter& inv){ return inv.stopDecel();     });
        return true;
    }
    HOLD_TRACE("sync_start EXIT OK (both running)");
    return false;
}

// Stop both rope inverters (decel ramp). Used by allMotionOff / hold_all_off.
// Both sides retry stopDecel in lockstep (attempt-synchronized) so the side
// that succeeds first re-issues a no-op stopDecel on subsequent attempts while
// the other side keeps trying — bounds drift to ~150ms vs the old independent
// reliable_stop_one which let one side complete in 5ms while the other ran
// solo for 300-700ms.
static void allRopeInvertersOff() {
    dual_se3_sync_retry([](SE3_inverter& inv){ return inv.stopDecel(); },
                        8, 100, "all_rope_off");
}

static void allMotionOff() {
    allRopeInvertersOff();
    // Skip CLV900 if it was never inited (middle hardware not installed).
    // CLV900_inverter has client=nullptr by default → writeParam→sendModbus
    // would null-deref. allMotionOff is called at startup before any motion
    // can begin, so we must guard against the uninited case here.
    if (g_dev_clv900.load()) inverter.stopDecel();
}

// Emergency stop ALL motion immediately (output cutoff, no decel ramp).
// Use when safety event detected — faster than decel stop but harsher.
// Note: emergencyStop is single-shot (no retry) — by the time we're here,
// safety event already happened and we want maximum speed cutoff. CLV900
// stop also single-shot for same reason.
static void allMotionEmergencyStop() {
    dual_se3_concurrent([](SE3_inverter& inv){ return inv.emergencyStop(); });
    if (g_dev_clv900.load()) inverter.stopDecel();   // CLV900 decel (no MRS-equivalent here); skip if uninited
}

// SE3 start retry policy: the underlying Modbus writes (setFreqHz / run cmd)
// occasionally fail due to stale-buffer / CU-mode-latch glitches / TCP packet
// retransmit / SE3-to-SE3 serialization on cli_A (both SE3 share the control
// bus post-2026-05-15 re-layout). Driver has internal watchdog for run cmd
// but not for setFreqHz, so we add app-level retry via reliable_start_one
// (5 attempts × 80ms backoff = ~400ms worst case). Earlier policy was 3 ×
// 100ms but bench observed transients lasting > 300ms; bumped to 5 × 80ms
// 2026-05-08 to handle longer windows.

// Start one SE3 rope inverter at SE3_MOTION_HZ in given direction.
//   pay_out=true  → release rope (motor "forward" by convention)
//   pay_out=false → retract rope (motor "reverse")
// Returns true on error after all retries exhausted.
static bool se3StartRopeMotion(SE3_inverter& inv, bool pay_out) {
    return reliable_start_one(inv, g_se3_motion_hz.load(), pay_out);
}

// Same but with HOLD_HZ (used by hold-to-pull for slower manual operation).
static bool se3StartRopeHold(SE3_inverter& inv, bool pay_out) {
    return reliable_start_one(inv, g_se3_hold_hz.load(), pay_out);
}

// Direction convention (wiring-dependent — flip fwd/rev if inverted on site):
//   pay_out = runForward, retract = runReverse.
static bool middleStart(bool pay_out) {
    if (inverter.setFreqHz(g_middle_winch_hz.load(), CLV900_MAX_HZ)) return true;
    return pay_out ? inverter.runForward() : inverter.runReverse();
}

// Apply one balance trim step. Caller passes base_hz (current setpoint),
// motion direction (+1 pay_out / -1 retract), and snapshot of length cache
// at motion start (cm). was_trimmed is a per-motion latch — true when the
// previous tick wrote a non-base trim to the SE3s; allows the helper to
// reset both sides to base_hz exactly once when trim crosses back into
// deadband / disable / cache-invalid (instead of leaving them stuck at the
// last trimmed Hz).
//
// Failure of setFreqHz is non-fatal — log and continue. The motion loop
// already has timeout / abort / tension safety as guard rails; balance is
// nice-to-have, not load-bearing.
static void apply_balance_trim(double base_hz, int direction,
                                int32_t base_left, int32_t base_right,
                                bool& was_trimmed) {
    auto reset_to_base = [&]() {
        if (!was_trimmed) return;
        // [2026-05-29] Parallel L+R setFreq (was sequential). Sequential meant
        // L received new Hz ~20-50ms before R → during that window the system
        // ran asymmetric Hz. Use dual_se3_sync_retry's dual-lambda overload
        // with 1 attempt / 0 backoff (BAL doesn't need retry — next tick will
        // re-issue on failure).
        dual_se3_sync_retry(
            [base_hz](SE3_inverter& inv){ return inv.setFreqHz(base_hz, SE3_MAX_HZ); },
            [base_hz](SE3_inverter& inv){ return inv.setFreqHz(base_hz, SE3_MAX_HZ); },
            1, 0, "bal_reset");
        was_trimmed = false;
        std::cout << "[BAL] reset to base " << base_hz << " Hz\n";
    };

    if (!g_balance_enabled.load())                                  { reset_to_base(); return; }
    if (!g_length_left_valid.load() || !g_length_right_valid.load()){ reset_to_base(); return; }

    const int32_t l_now = g_length_left .load();
    const int32_t r_now = g_length_right.load();
    const double progL = (double)direction * (double)(l_now - base_left);
    const double progR = (double)direction * (double)(r_now - base_right);
    const double err   = progL - progR;     // + = left ahead

    if (std::fabs(err) <= g_balance_deadband.load()) { reset_to_base(); return; }

    const double kp        = g_balance_kp.load();
    const double cap_ratio = g_balance_trim_cap_ratio.load();
    const double cap       = base_hz * cap_ratio;   // scales with motion speed
    double trim = kp * err;
    if (trim >  cap) trim =  cap;
    if (trim < -cap) trim = -cap;

    const double hz_min = g_balance_hz_min.load();
    const double hz_max = base_hz + g_balance_hz_max_offset.load();   // dynamic: base + offset
    double left_hz  = base_hz - trim / 2.0;
    double right_hz = base_hz + trim / 2.0;
    if (left_hz  < hz_min) left_hz  = hz_min;
    if (left_hz  > hz_max) left_hz  = hz_max;
    if (right_hz < hz_min) right_hz = hz_min;
    if (right_hz > hz_max) right_hz = hz_max;

    // [2026-05-29] Parallel L+R trim (was sequential). See reset_to_base comment.
    dual_se3_sync_retry(
        [left_hz ](SE3_inverter& inv){ return inv.setFreqHz(left_hz,  SE3_MAX_HZ); },
        [right_hz](SE3_inverter& inv){ return inv.setFreqHz(right_hz, SE3_MAX_HZ); },
        1, 0, "bal_trim");
    was_trimmed = true;
    std::cout << "[BAL] err=" << err << "cm trim=" << trim
              << "Hz L=" << left_hz << " R=" << right_hz << "\n";
}

// ============ Watchdog ============

static uint64_t now_ms() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Wall-clock timestamp [HH:MM:SS.mmm] for manual-button trace logs (2026-05-15).
// Used by HOLD_TRACE() to correlate server-side events with bench observation
// (which button was pressed when, where the cmd actually stalled). Wall-clock
// (not steady_clock) so it matches the operator's stopwatch / video timestamps.
static std::string ts_now() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t   = system_clock::to_time_t(now);
    const auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, (int)ms.count());
    return std::string(buf);
}

// Manual-button trace macro (2026-05-15). Always prints regardless of debug
// flags — this is for active bench diagnosis of cmd_hold path issues.
// Format: [HH:MM:SS.mmm] [HOLD-TRACE] <msg>
// Grep-friendly tag; turn off by removing the macro (logs become no-op).
#define HOLD_TRACE(msg) \
    std::cout << "[" << ts_now() << "] [HOLD-TRACE] " << msg << "\n"

static void broadcast_evt(const std::string& s) {
    cmd_server.broadcast(s.c_str(), (int)s.size());
}

// Called on any inbound TCP data — treats any byte as "peer alive".
// Forward decl — defined later (line ~747). watchdog_loop uses this for
// motion-aware timeout split (motion_rope: 10s, hold/idle: 2s).
static inline bool any_hold_active();

static void touch_heartbeat() {
    last_ping_ms.store(now_ms());
    if (watchdog_fired.exchange(false)) {
        broadcast_evt("EVT watchdog_recovered\n");
    }
}

static void watchdog_loop() {
    while (!watchdog_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(HEARTBEAT_CHECK_MS));

        if (cmd_server.getConnectedClients().empty()) continue;

        const uint64_t last = last_ping_ms.load();
        if (last == 0) continue;  // no heartbeat yet since startup

        // motion_rope auto-running gets a longer leash than hold/idle.
        // Dynamic: motion_rope/cmd_roll_correct compute expected duration at
        // entry and set g_motion_dynamic_timeout_ms; floored by _MOTION_MIN.
        const bool in_auto_motion = motion_active.load() && !any_hold_active();
        int timeout_ms = WATCHDOG_TIMEOUT_MS_IDLE;
        if (in_auto_motion) {
            const int dyn = g_motion_dynamic_timeout_ms.load();
            timeout_ms = (dyn > WATCHDOG_TIMEOUT_MS_MOTION_MIN)
                         ? dyn
                         : WATCHDOG_TIMEOUT_MS_MOTION_MIN;
        }
        if (now_ms() - last <= (uint64_t)timeout_ms) continue;

        // exchange guards against re-firing on every tick
        if (watchdog_fired.exchange(true)) continue;

        if (motion_active.load()) {
            abort_flag = true;
            hold_all_off();   // clear hold flags so next reconnect doesn't auto-resume
            allMotionOff();
            broadcast_evt("EVT watchdog_timeout state=aborted\n");
        } else {
            broadcast_evt("EVT watchdog_timeout state=idle\n");
        }
    }
}

// RAII guard: marks motion_active=true on construction, false on destruction.
struct MotionScope {
    MotionScope()  { motion_active.store(true);  }
    ~MotionScope() { motion_active.store(false); }
};

// RAII for watchdog dynamic timeout. Set on motion_rope/cmd_roll_correct entry,
// reset to 0 (falls back to _MOTION_MIN) on exit. Tunable via the est_speed
// constant — conservative ~5 cm/s @ 10 Hz observed on bench.
struct MotionTimeoutScope {
    MotionTimeoutScope(int cm) {
        constexpr int est_speed_cm_per_s = 5;       // conservative; @ ~10 Hz
        constexpr int buffer_ms          = 10000;   // 10s buffer for ramp + fine_adjust
        const int est_ms = (cm * 1000) / est_speed_cm_per_s + buffer_ms;
        g_motion_dynamic_timeout_ms.store(est_ms);
    }
    ~MotionTimeoutScope() { g_motion_dynamic_timeout_ms.store(0); }
};

// ============ Tension safety ============

// Read DSZL-107 left + right kg. Returns false on success.
static bool read_tensions(double& left_kg, double& right_kg) {
    if (dsz_left .get_tension_kg(left_kg))  return true;
    if (dsz_right.get_tension_kg(right_kg)) return true;
    return false;
}

// Pure value-level safety check. Returns empty string if OK; otherwise an
// alarm reason ("high_left" / "high_right" / "diff").
//
// 2026-05-08 update: TENSION_MIN_KG (low) check removed per user request —
// bench / unloaded-rope state naturally reads near-zero tension and would
// constantly false-alarm. Only HIGH tension (overload / jam) and left/right
// imbalance still trip safety. motion_flow.md §6.5 needs corresponding spec
// update (mailbox to Jim).
static std::string tension_safety_check_values(double l_kg, double r_kg) {
    const double max_kg  = g_tension_max_kg.load();
    const double diff_kg = g_tension_diff_max_kg.load();
    if (l_kg > max_kg) return "high_left";
    if (r_kg > max_kg) return "high_right";
    // Absolute diff: |left - right| > threshold → tilt risk
    if (std::fabs(l_kg - r_kg) > diff_kg) return "diff";
    return "";
}

// Combined: read DSZL + check. Returns alarm string or empty (read failure
// treated as no-alarm — caller decides whether to escalate).
static std::string tension_safety_check(double& left_kg, double& right_kg) {
    if (read_tensions(left_kg, right_kg)) return "";
    return tension_safety_check_values(left_kg, right_kg);
}

// Format a tension_alarm EVT line and broadcast.
static void broadcast_tension_alarm(const std::string& kind, double l_kg, double r_kg) {
    std::ostringstream oss;
    oss << "EVT tension_alarm kind=" << kind
        << " left="  << l_kg
        << " right=" << r_kg << "\n";
    broadcast_evt(oss.str());
}

// ============ Hold mode (press-and-hold relay control with tension safety) ============

// Atomic helper: any hold flag set?
static inline bool any_hold_active() {
    return hold_up_left.load()   || hold_up_right.load() ||
           hold_down_left.load() || hold_down_right.load();
}

// Turn off ALL hold motion + flags. "All stop together" per spec — even if
// only UP threshold breached, also stop DOWN. Stops both SE3 inverters
// via attempt-synchronized retry so transient Modbus failures don't leave
// the rope running after a safety trip, and both sides decel in lockstep
// (drift ≤ ~150ms vs old reliable_stop_one independent threads at 300-700ms).
static void hold_all_off() {
    hold_up_left.store(false);
    hold_up_right.store(false);
    hold_down_left.store(false);
    hold_down_right.store(false);
    dual_se3_sync_retry([](SE3_inverter& inv){ return inv.stopDecel(); },
                        8, 100, "hold_all_off");
    motion_active.store(false);
}

// Background SD76 cache thread. Poll 3 length meters at METER_POLL_MS so
// cmd_status can return cached values without hitting Modbus per request.
// Skips meters whose device flag is false (gateway down / init failed).
//
// SD76 displayed cm already reflects its own SCAL/DP calibration (set via
// cal_set → driver writes EEPROM). g_length_* stores the displayed value
// directly — all motion / balance / status consumers see calibrated cm
// without any application-side scale layer.
// SD76 read sanity filter — two-stage:
//   (1) Small jump (≤ 15cm vs cache): accept immediately (normal motion).
//       30cm/s max motor × 250ms poll = 7.5cm typical; 15cm has ~2x margin.
//   (2) Big jump (> 15cm): could be real fast motion OR single-frame
//       corruption (SD76 driver readRegister doesn't validate CRC — mailbox
//       to Jim). Do a SECOND immediate readUpperInteger:
//         - if v1 and v2 agree within 5cm → real big jump → accept v2
//         - else → one is corruption → reject, keep prev cache
//
// Why the old single-threshold approach failed (bench 2026-05-14): if cache
// happened to be near 0 (e.g., after fine_adjust), a corrupted "0" read had
// small jump from cache, was accepted, cache stuck at 0 forever. Next REAL
// reads (34/41/47cm) all looked like big jumps from 0 → all rejected →
// cache never recovered → balance saw fake huge err → trim cap → motor jolt.
constexpr int32_t METER_SMALL_JUMP_CM         = 15;   // normal motion threshold
constexpr int32_t METER_CONSISTENCY_CM        = 5;    // v1/v2 agreement for "real big jump"
// Max physically possible per-poll change: 50Hz (SE3_MAX) × 1cm/Hz × 0.25s
// (slowest poll METER_POLL_MS_IDLE) ≈ 12.5cm. 30cm gives 2.4x margin for
// kick-start transients / mechanical slip. Above this, even two reads that
// agree are treated as sustained corruption (bench 2026-05-14: SD76 returned
// 0 for ~1s, fooled the double-read consensus into accepting "0", motion_rope
// declared false reached-target with L=124 actual vs cache=0).
constexpr int32_t METER_MAX_PHYSICAL_JUMP_CM  = 30;

// Two-stage robust read for one SD76. Returns:
//   accepted=true + out=new value (caller stores)
//   accepted=false (caller keeps cache; logs already done)
struct MeterReadResult {
    bool    accepted;
    int32_t value;
    bool    read_hard_fail;   // both attempts failed → caller marks invalid
};

static MeterReadResult meter_read_robust(
    SD76_length_meters& meter, std::atomic<int32_t>& cache,
    std::atomic<bool>& valid_flag, const char* tag,
    int& reject_count, uint64_t& last_log_reset_ms)
{
    int32_t v1 = 0;
    if (meter.readUpperInteger(v1)) {
        return {false, 0, true};   // hard fail
    }

    if (!valid_flag.load()) {
        return {true, v1, false};   // first read after invalidation, accept
    }

    const int32_t prev  = cache.load();
    const int32_t diff1 = std::abs(v1 - prev);
    if (diff1 <= METER_SMALL_JUMP_CM) {
        return {true, v1, false};   // normal motion / stationary
    }

    // Big jump — confirm with immediate second read
    int32_t v2 = 0;
    if (meter.readUpperInteger(v2)) {
        // Re-read failed — can't confirm v1, reject (keep cache valid)
        const uint64_t now = now_ms();
        if (now - last_log_reset_ms > 60000) { last_log_reset_ms = now; reject_count = 0; }
        if (reject_count++ < 3) {
            std::cout << "[meter] " << tag << " suspicious read (re-read fail): prev="
                      << prev << " new=" << v1 << " (jump=" << (v1 - prev) << "cm)\n";
        }
        return {false, 0, false};
    }

    if (std::abs(v2 - v1) <= METER_CONSISTENCY_CM) {
        // v1 and v2 agree — usually means real big jump (e.g., motor moved
        // fast / brief meter_loop stall / valid post-zero settle). But check
        // physical plausibility: if the jump from prev exceeds what any motor
        // could physically produce in one poll interval, this is SUSTAINED
        // corruption (SD76 returning same wrong value for multiple frames,
        // bench 2026-05-14 saw sustained "0" reading fooling double-read).
        const int32_t diff_to_prev = std::abs(v2 - prev);
        if (diff_to_prev <= METER_MAX_PHYSICAL_JUMP_CM) {
            return {true, v2, false};   // big but plausible — accept
        }
        // Consistent reads but physically impossible — reject as sustained
        // corruption. valid_flag stays true so cache holds last good value.
        const uint64_t now = now_ms();
        if (now - last_log_reset_ms > 60000) { last_log_reset_ms = now; reject_count = 0; }
        if (reject_count++ < 3) {
            std::cout << "[meter] " << tag << " sustained corruption rejected: prev="
                      << prev << " v1=v2=" << v2
                      << " (jump=" << diff_to_prev << "cm > " << METER_MAX_PHYSICAL_JUMP_CM
                      << "cm physical limit)\n";
        }
        return {false, 0, false};
    }

    // v1 and v2 disagree → one is single-frame corruption.
    // If v2 looks reasonable vs prev, take it (likely v1 was the bad one).
    // Otherwise reject both.
    const int32_t diff2 = std::abs(v2 - prev);
    if (diff2 <= METER_SMALL_JUMP_CM) {
        return {true, v2, false};
    }
    const uint64_t now = now_ms();
    if (now - last_log_reset_ms > 60000) { last_log_reset_ms = now; reject_count = 0; }
    if (reject_count++ < 3) {
        std::cout << "[meter] " << tag << " corrupted read rejected: prev=" << prev
                  << " v1=" << v1 << " v2=" << v2
                  << " (both suspicious — keeping prev)\n";
    }
    return {false, 0, false};
}

static void meter_loop() {
    int l_rej = 0, r_rej = 0, m_rej = 0;
    uint64_t l_last_log = 0, r_last_log = 0, m_last_log = 0;
    while (!g_meter_loop_stop.load()) {
        if (g_dev_meter_left.load()) {
            auto r = meter_read_robust(meter_left, g_length_left, g_length_left_valid,
                                       "left", l_rej, l_last_log);
            if (r.read_hard_fail) g_length_left_valid.store(false);
            else if (r.accepted)  { g_length_left.store(r.value); g_length_left_valid.store(true); }
            // else: reject, keep cache + valid
        }
        if (g_dev_meter_right.load()) {
            auto r = meter_read_robust(meter_right, g_length_right, g_length_right_valid,
                                       "right", r_rej, r_last_log);
            if (r.read_hard_fail) g_length_right_valid.store(false);
            else if (r.accepted)  { g_length_right.store(r.value); g_length_right_valid.store(true); }
        }
        if (g_dev_meter_middle.load()) {
            auto r = meter_read_robust(meter_middle, g_length_middle, g_length_middle_valid,
                                       "middle", m_rej, m_last_log);
            if (r.read_hard_fail) g_length_middle_valid.store(false);
            else if (r.accepted)  { g_length_middle.store(r.value); g_length_middle_valid.store(true); }
        }
        // Motion-aware poll rate: when motors are running, slow down to avoid
        // contending with SE3 / CLV900 writes on the same TCP_client mutex.
        const bool motion_busy = motion_active.load() || any_hold_active();
        std::this_thread::sleep_for(std::chrono::milliseconds(
            motion_busy ? METER_POLL_MS_MOTION : METER_POLL_MS_IDLE));
    }
}

// Background safety + tension cache thread.
//   Always-running. When any hold flag set: poll DSZL-107 every HOLD_LOOP_ACTIVE_MS,
//   check threshold/safety, stop on breach.
//   When no holds: poll at HOLD_LOOP_IDLE_MS just to keep cmd_status / GUI fresh.
//   No bus_mtx — relies on user not running motion_rope concurrently with hold mode.
//
// Also drives dynamic balance when both sides held in same direction (both UP
// or both DOWN). Asymmetric / single-side holds skip balance — that's
// intentional debug usage. State tracked via prev_sync_dir; on sync entry
// snapshot length cache as base for progress error, on sync exit reset any
// trimmed still-held side back to base_hz.

// SE3 keepalive thread (see g_se3_keepalive_* declaration for rationale).
// ALWAYS runs (not gated on motion_active) — SE3 firmware starts counting
// 07-08/09 timeouts as soon as it's powered up, regardless of whether we're
// commanding motion. If we don't talk to SE3 within 4 seconds (07-09 × 07-08
// default), OPT alarm fires and motor is locked until manual reset.
//
// Bench observation 2026-05-13: with motion-gated keepalive, OPT triggered at
// program startup because SE3 was already silent for > 4s before crane started.
// Always-on keepalive fixes this — SE3 sees continuous traffic from boot.
//
// Bus cost: 1 × 50ms read per second per gateway (cli_A → SE3 left,
// cli_B → SE3 right) = ~5% per gateway. Each SE3 has its own USR/RS485 bus
// post 2026-05-15 physical-separation layout, so no cross-side contention.
// Decode SE3 fault code byte → short mnemonic. Source: SE3-INVERTER manual
// §異常代碼 table. Only the codes seen in this project's bench history are
// fully named; rare codes log as "?" with raw hex preserved in the caller.
static const char* se3_fault_name(uint8_t code) {
    switch (code) {
        case 0:   return "-";        // empty slot (no fault recorded here)
        case 16:  return "OC1";      // overcurrent (accel)
        case 17:  return "OC2";      // overcurrent (constant)
        case 18:  return "OC3";      // overcurrent (decel)
        case 19:  return "OC0";      // overcurrent (other)
        case 32:  return "OV1";      // overvoltage (accel)
        case 33:  return "OV2";      // overvoltage (constant)
        case 34:  return "OV3";      // overvoltage (decel)
        case 35:  return "OV0";      // overvoltage (other)
        case 48:  return "THT";      // inverter heatsink overtemp
        case 49:  return "THN";      // motor overtemp
        case 50:  return "NTC";      // NTC sensor
        case 64:  return "EEP";      // EEPROM error
        case 66:  return "PID";
        case 82:  return "IPF";      // momentary power loss
        case 97:  return "OLS";      // overload stall
        case 98:  return "OL2";      // motor overload
        case 129: return "AErr";     // auto-tune fail
        case 144: return "OHT";      // heatsink overtemp (alt code)
        case 160: return "OPT";      // communication timeout
        case 179: return "SCP";      // short-circuit protection
        case 192: return "CPU";
        case 193: return "CPR";
        default:  return "?";
    }
}

// Read and format SE3 fault history (H1007 + H1008) for diagnostic logging.
// Caller passes the inverter ref + a side label ("left" / "right"). On read
// failure, returns a placeholder string so log still tells us we tried.
static std::string format_se3_fault_codes(SE3_inverter& inv) {
    uint8_t f1 = 0, f2 = 0, f3 = 0, f4 = 0;
    if (inv.readFaultCode(f1, f2, f3, f4)) {
        return "fault_code=READ_FAIL";
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf),
        "fault_codes=[%u/%s %u/%s %u/%s %u/%s]",
        f1, se3_fault_name(f1),
        f2, se3_fault_name(f2),
        f3, se3_fault_name(f3),
        f4, se3_fault_name(f4));
    return std::string(buf);
}

static void se3_keepalive_loop() {
    // Diagnostic counters — log every 30s so bench can verify keepalive is
    // actually running and SE3 is responding.
    int ticks = 0;
    int l_ok = 0, l_fail = 0;
    int r_ok = 0, r_fail = 0;
    uint16_t last_l_status = 0, last_r_status = 0;
    int l_clear_count = 0, r_clear_count = 0;

    // Auto-clear alarm cooldown: only attempt clearAlarm every CLEAR_COOLDOWN_MS
    // per side. Initialize "60 seconds ago" so first detection fires immediately
    // (time_point::min() causes overflow in subtraction → cooldown never satisfied).
    //
    // 2026-05-14: lowered 5000 → 500ms. Original 5s was sized to avoid spamming
    // H1101 writes when the Pi is genuinely offline. But for the much more
    // common case — transient RS485 byte loss tripping OPT every few seconds —
    // 5s of cooldown means motor coasts (空轉) for 5s every OPT cycle, which
    // (a) drifts L vs R rope by 5s × hold_hz × cm/Hz (heard audibly as motor
    // stop-restart loop), and (b) makes balance trim useless during the
    // stopped period. 500ms is short enough to recover the motor before
    // significant drift while still bounding clearAlarm rate to ~2/sec/side
    // worst case (each clearAlarm internally sleeps 200ms anyway).
    constexpr int CLEAR_COOLDOWN_MS = 500;
    auto last_clear_l = std::chrono::steady_clock::now() - std::chrono::seconds(60);
    auto last_clear_r = std::chrono::steady_clock::now() - std::chrono::seconds(60);

    // Throttle fault-code reads to ≤1/side per FAULT_CODE_READ_INTERVAL_MS.
    // format_se3_fault_codes does 2x readParam (H1007 + H1008), adding ~50-300ms
    // per call. Without throttle, every fault tick would do these reads, eating
    // keepalive bandwidth and risking the OTHER side's 07-09=2s silence trip
    // (2026-05-14 cascade scenario). With 5s throttle, frequent-fault scenarios
    // still get a fault-code sample every 5s (= 6 samples per 30s window when
    // fault is recurring), enough for diagnosis without blocking the loop.
    constexpr int FAULT_CODE_READ_INTERVAL_MS = 5000;
    auto last_fault_code_l = std::chrono::steady_clock::now() - std::chrono::seconds(60);
    auto last_fault_code_r = std::chrono::steady_clock::now() - std::chrono::seconds(60);

    while (!g_se3_keepalive_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(SE3_KEEPALIVE_INTERVAL_MS));
        if (g_se3_keepalive_stop.load()) break;

        // Cheap reads. readStatusWord = single Modbus 0x03, ~50ms per side.
        // Failure tolerated — TCP_client has its own reconnect; if SE3 truly
        // gone, motion_rope's meter_death detection or watchdog will catch it.
        //
        // Retry-once on transient fail (2026-05-14): bench observed 30-60% per-
        // side fail rate on cli_A → SE3 slave 1 due to RS485 byte loss. Without
        // retry, 3-4 consecutive fails (likely with 60% single-fail rate) push
        // past SE3 07-09 × 07-08 = 4s and trip OPT. With retry-once, effective
        // fail rate ≈ fail² (60% → 36%, 30% → 9%) — and crucially SE3 sees a
        // valid request on either attempt, which resets its silence counter.
        const auto now_pt = std::chrono::steady_clock::now();
        if (g_dev_se3_left.load()) {
            uint16_t st = 0;
            bool read_err = se3_left.readStatusWord(st);
            if (read_err) read_err = se3_left.readStatusWord(st);   // retry once
            if (read_err) {
                l_fail++;
            } else {
                l_ok++;
                last_l_status = st;
                const bool fault = (st & 0x0080) != 0;
                g_se3_left_fault.store(fault);    // expose for motion_rope abort
                if (fault) {
                    const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now_pt - last_clear_l).count();
                    if (since >= CLEAR_COOLDOWN_MS) {
                        // Re-added fault-code read 2026-05-15g, throttled to 5s per
                        // side (see FAULT_CODE_READ_INTERVAL_MS above). Original
                        // 2026-05-14 revert was due to unthrottled reads cascading
                        // into the other side's OPT — throttle keeps that bounded.
                        std::string fc_str = "(fault_code throttled)";
                        const auto since_fc = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now_pt - last_fault_code_l).count();
                        if (since_fc >= FAULT_CODE_READ_INTERVAL_MS) {
                            fc_str = format_se3_fault_codes(se3_left);
                            last_fault_code_l = now_pt;
                        }
                        std::cout << "[SE3-keepalive] left FAULT (status=0x" << std::hex << st
                                  << std::dec << ") " << fc_str
                                  << " — auto-clear via H1101=0x9696\n";
                        se3_left.clearAlarm();
                        last_clear_l = now_pt;
                        l_clear_count++;
                    }
                }
            }
        }
        if (g_dev_se3_right.load()) {
            uint16_t st = 0;
            bool read_err = se3_right.readStatusWord(st);
            if (read_err) read_err = se3_right.readStatusWord(st);   // retry once — see left-side comment
            if (read_err) {
                r_fail++;
            } else {
                r_ok++;
                last_r_status = st;
                const bool fault = (st & 0x0080) != 0;
                g_se3_right_fault.store(fault);
                if (fault) {
                    const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now_pt - last_clear_r).count();
                    if (since >= CLEAR_COOLDOWN_MS) {
                        // Re-added throttled fault-code read — see left-side comment.
                        std::string fc_str = "(fault_code throttled)";
                        const auto since_fc = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now_pt - last_fault_code_r).count();
                        if (since_fc >= FAULT_CODE_READ_INTERVAL_MS) {
                            fc_str = format_se3_fault_codes(se3_right);
                            last_fault_code_r = now_pt;
                        }
                        std::cout << "[SE3-keepalive] right FAULT (status=0x" << std::hex << st
                                  << std::dec << ") " << fc_str
                                  << " — auto-clear via H1101=0x9696\n";
                        se3_right.clearAlarm();
                        last_clear_r = now_pt;
                        r_clear_count++;
                    }
                }
            }
        }

        // First 10 ticks: log each immediately so bench can see early activity
        // before OPT alarm triggers. After that, batch every ~30s.
        ticks++;
        if (ticks <= 10) {
            const bool l_fault = (last_l_status & 0x0080) != 0;   // b7 = fault
            const bool r_fault = (last_r_status & 0x0080) != 0;
            std::cout << "[SE3-keepalive] tick " << ticks
                      << " — L: ok=" << l_ok << " fail=" << l_fail
                      << " status=0x" << std::hex << last_l_status << std::dec
                      << (l_fault ? " ⚠FAULT" : "")
                      << " | R: ok=" << r_ok << " fail=" << r_fail
                      << " status=0x" << std::hex << last_r_status << std::dec
                      << (r_fault ? " ⚠FAULT" : "")
                      << "\n";
            std::cout.flush();
        } else if (ticks >= 60) {   // every ~30s (60 ticks × 500ms)
            std::cout << "[SE3-keepalive] last 30s — L: ok=" << l_ok << " fail=" << l_fail
                      << " status=0x" << std::hex << last_l_status << std::dec
                      << " clears=" << l_clear_count
                      << " | R: ok=" << r_ok << " fail=" << r_fail
                      << " status=0x" << std::hex << last_r_status << std::dec
                      << " clears=" << r_clear_count << "\n";
            std::cout.flush();
            ticks = 10;   // reset to "first 10 done" state, keep batching
            l_ok = l_fail = r_ok = r_fail = 0;
            l_clear_count = r_clear_count = 0;
        }
    }
}

static void hold_loop() {
    int     prev_sync_dir       = 0;       // 0=none, +1=down (pay_out both), -1=up (retract both)
    int32_t balance_base_left   = 0;
    int32_t balance_base_right  = 0;
    bool    was_trimmed         = false;
    auto    last_balance_tick   = std::chrono::steady_clock::now();

    while (!hold_loop_stop.load()) {
        const bool active = any_hold_active();

        double l = 0.0, r = 0.0;
        if (!read_tensions(l, r)) {
            g_tension_left.store(l);
            g_tension_right.store(r);
            g_tension_valid.store(true);

            if (active) {
                // Total threshold (only when UP active — DOWN releases tension)
                const bool up_active = hold_up_left.load() || hold_up_right.load();
                if (up_active) {
                    const double total = l + r;
                    if (total > g_up_stop_total_kg.load()) {
                        hold_all_off();
                        std::ostringstream oss;
                        oss << "EVT tension_total_limit total=" << total
                            << " threshold=" << g_up_stop_total_kg.load() << "\n";
                        broadcast_evt(oss.str());
                        std::this_thread::sleep_for(std::chrono::milliseconds(HOLD_LOOP_ACTIVE_MS));
                        continue;
                    }
                }
                // Per-side safety (low/high/diff) — fires regardless of direction
                const std::string alarm = tension_safety_check_values(l, r);
                if (!alarm.empty()) {
                    hold_all_off();
                    broadcast_tension_alarm(alarm, l, r);
                    std::this_thread::sleep_for(std::chrono::milliseconds(HOLD_LOOP_ACTIVE_MS));
                    continue;
                }
            }
        } else {
            g_tension_valid.store(false);
            // Read fail tolerated as transient. If sustained while holding,
            // user's GUI will see tension_valid=0 and can choose to release.
        }

        // Dynamic balance for synchronized hold (both UP or both DOWN).
        const bool up_both   = hold_up_left.load()   && hold_up_right.load();
        const bool down_both = hold_down_left.load() && hold_down_right.load();
        const int  cur_sync_dir = up_both ? -1 : (down_both ? +1 : 0);

        if (cur_sync_dir != prev_sync_dir) {
            if (cur_sync_dir != 0
                && g_length_left_valid.load() && g_length_right_valid.load()) {
                // Sync just entered AND length cache valid → snapshot baseline.
                balance_base_left  = g_length_left .load();
                balance_base_right = g_length_right.load();
                was_trimmed        = false;
                last_balance_tick  = std::chrono::steady_clock::now();
                prev_sync_dir      = cur_sync_dir;
                std::cout << "[BAL] hold sync entered dir=" << cur_sync_dir
                          << " base L=" << balance_base_left
                          << " R=" << balance_base_right << "\n";
            } else if (cur_sync_dir == 0 && prev_sync_dir != 0) {
                // Sync just ended → if we'd been trimming, reset still-held
                // sides back to base_hz so they don't keep last-trim Hz after
                // their partner released. setFreqHz on stopped SE3 just queues
                // the next-start freq, harmless.
                if (was_trimmed) {
                    const double base = g_se3_hold_hz.load();
                    if (hold_up_left.load()  || hold_down_left.load())
                        se3_left .setFreqHz(base, SE3_MAX_HZ);
                    if (hold_up_right.load() || hold_down_right.load())
                        se3_right.setFreqHz(base, SE3_MAX_HZ);
                    was_trimmed = false;
                    std::cout << "[BAL] hold sync ended — reset to base " << base << " Hz\n";
                }
                prev_sync_dir = 0;
            }
            // else: cur != 0 but cache invalid — skip; retry next iter when cache is fresh.
        }

        if (cur_sync_dir != 0 && prev_sync_dir == cur_sync_dir) {
            const auto now_pt = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now_pt - last_balance_tick).count() >= BALANCE_TICK_MS) {
                last_balance_tick = now_pt;
                apply_balance_trim(g_se3_hold_hz.load(), cur_sync_dir,
                                   balance_base_left, balance_base_right, was_trimmed);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(
            active ? HOLD_LOOP_ACTIVE_MS : HOLD_LOOP_IDLE_MS));
    }
}

// ============ pay_out / retract (both ropes + middle pipeline) ============

// Post-motion fine-adjust: align L and R to the "leader" side, matching the
// direction of the main motion (no reversal).
//
//   main was pay_out  → target = max(L_cur, R_cur)
//                       (the side that went further down has the lead;
//                        the lagging side continues pay_out to match it)
//   main was retract  → target = min(L_cur, R_cur)
//                       (the side that retracted further has the lead;
//                        the lagging side continues retract to match it)
//
// Rationale: with PAY_OUT_INCREASES_DISPLAY=true the leader is the side whose
// display moved further in the main motion direction. Aligning to the leader
// instead of the midpoint avoids reversing motor direction, which would fight
// rope tension and risk drum back-spin under load.
//
// Motors are already stopped on entry (motion_rope does reliable_stop_one
// on target reach). Each side stays in the main motion direction; the side
// already at target stays stopped, the other gets kick-started (20Hz × 500ms)
// then drops to fine_adjust_hz precision until display reaches target ± tol.
//
// Returns:
//   "" on success
//   "aborted" / "fine_adjust_timeout" / "tension_<kind>" /
//   "fine_adjust_setfreq_<side>_fail" / "fine_adjust_run_<side>_fail"
static std::string motion_fine_adjust_sync(bool main_motion_pay_out)
{
    // Entry retry for cache validity
    for (int i = 0; i < 3; ++i) {
        if (g_length_left_valid.load() && g_length_right_valid.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!g_length_left_valid.load() || !g_length_right_valid.load()) {
        std::cout << "[fine_adjust] cache invalid after retry — skipping\n";
        return "";
    }

    // Settle wait for SE3 stop ramps initiated by motion_rope to complete
    // before we read cache + issue fresh RUN cmds. Without this, two problems:
    //   1. cache snapshot is taken mid-ramp → init L/R reading is 3-5cm short
    //      of where rope actually settles → may early-exit "within tol" when
    //      panel shows out-of-tol gap (bench: code saw L=127 R=129 diff=-2,
    //      panel ended 130/134 diff=-4)
    //   2. SE3 firmware rejects H1001 RUN writes during P.8 decel ramp / DC
    //      brake injection → reliable_run_one's 2x50ms retry exhausts and
    //      fine_adjust aborts before catching up the lagging side (bench:
    //      "[fine_adjust] R run cmd FAILED ... 2x retry exhausted" right
    //      after motion_rope stopped R)
    // 1500ms covers typical P.8 ramp + DC brake duration on heavy load.
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    const int curL_init = (int)g_length_left .load();
    const int curR_init = (int)g_length_right.load();
    const int diff_init = curL_init - curR_init;

    // Already within tolerance — no convergence needed, stop both
    if (std::abs(diff_init) <= FINE_ADJUST_TOLERANCE_CM) {
        std::cout << "[fine_adjust] L=" << curL_init << " R=" << curR_init
                  << " diff=" << diff_init << " — within ±" << FINE_ADJUST_TOLERANCE_CM
                  << ", stopping both\n";
        std::thread sL([]{ reliable_stop_one(se3_left);  });
        std::thread sR([]{ reliable_stop_one(se3_right); });
        sL.join();
        sR.join();
        return "";
    }

    // Align to leader (no direction reversal)
    const int target_cm = main_motion_pay_out
                          ? std::max(curL_init, curR_init)
                          : std::min(curL_init, curR_init);
    const int half_tol  = FINE_ADJUST_TOLERANCE_CM / 2;

    // Whether each side has further to go
    const bool left_needs  = (std::abs(curL_init - target_cm) > FINE_ADJUST_TOLERANCE_CM);
    const bool right_needs = (std::abs(curR_init - target_cm) > FINE_ADJUST_TOLERANCE_CM);

    // Stop threshold per side: at target ± half_tol on the approach side
    // (pay_out approaches from below since display ↑; retract from above)
    const int L_stop_at = main_motion_pay_out ? (target_cm - half_tol) : (target_cm + half_tol);
    const int R_stop_at = main_motion_pay_out ? (target_cm - half_tol) : (target_cm + half_tol);
    const bool L_approach_from_below = main_motion_pay_out;
    const bool R_approach_from_below = main_motion_pay_out;

    // Both sides move in main motion direction; no reversal
    const bool L_pay = main_motion_pay_out;
    const bool R_pay = main_motion_pay_out;
    const double hz = g_fine_adjust_hz.load();

    std::cout << "[fine_adjust] align-to-leader L=" << curL_init << " R=" << curR_init
              << " target=" << target_cm
              << " main=" << (main_motion_pay_out ? "pay_out" : "retract")
              << " L=" << (left_needs ? "→go" : "stop(at-target)") << " (stop at " << L_stop_at << ")"
              << " R=" << (right_needs ? "→go" : "stop(at-target)") << " (stop at " << R_stop_at << ")"
              << " at " << hz << "Hz\n";

    // Belt-and-suspenders: stop at-target side(s) again. motion_rope already
    // stopped both on target reach, but if this function is called from
    // cmd_align_lengths (independent of motion_rope), motor state may be
    // unknown — idempotent stop is cheap.
    if (!left_needs)  reliable_stop_one(se3_left);
    if (!right_needs) reliable_stop_one(se3_right);

    // Start only the lagging side(s). Distance-adaptive kick:
    //   distance >= KICK_DISTANCE_THRESHOLD_CM → kick_hz × KICK_DURATION_MS, then drop to precision hz
    //   distance <  KICK_DISTANCE_THRESHOLD_CM → start at precision hz directly (no kick)
    // Reason: kick is a blind sleep (no target check inside). At kick_hz=20Hz × 500ms
    // ≈ 10cm of rope movement, which overshoots short corrections before the
    // convergence loop even gets to poll the cache.
    const double kick_hz = g_kick_hz.load();
    const int L_distance = left_needs  ? std::abs(curL_init - target_cm) : 0;
    const int R_distance = right_needs ? std::abs(curR_init - target_cm) : 0;
    const bool L_use_kick = left_needs  && (L_distance >= KICK_DISTANCE_THRESHOLD_CM);
    const bool R_use_kick = right_needs && (R_distance >= KICK_DISTANCE_THRESHOLD_CM);
    const double L_start_hz = L_use_kick ? kick_hz : hz;
    const double R_start_hz = R_use_kick ? kick_hz : hz;

    if (left_needs) {
        if (reliable_setfreq_one(se3_left, L_start_hz)) {
            std::cout << "[fine_adjust] L setFreq FAILED (8x100ms retries exhausted)\n";
            return "fine_adjust_setfreq_l_fail";
        }
        // Aggressive 6x200ms retry (1200ms window): main motion just stopped
        // both sides via reliable_stop_one; SE3 firmware may still be in P.8
        // decel / DC brake injection phase and reject H1001 RUN. Default 2x50ms
        // (sync_start use) is too short to outwait the firmware transient.
        if (reliable_run_one(se3_left, L_pay, 6, 200)) {
            std::cout << "[fine_adjust] L run cmd FAILED (Modbus / SE3 firmware reject, 6x200ms exhausted)\n";
            return "fine_adjust_run_l_fail";
        }
        if (L_use_kick) {
            std::cout << "[fine_adjust] L kick start at " << kick_hz << "Hz for " << KICK_DURATION_MS
                      << "ms (distance=" << L_distance << "cm)\n";
        } else {
            std::cout << "[fine_adjust] L direct start at " << hz << "Hz (distance="
                      << L_distance << "cm < " << KICK_DISTANCE_THRESHOLD_CM << "cm, no kick)\n";
        }
    }
    if (right_needs) {
        if (reliable_setfreq_one(se3_right, R_start_hz)) {
            std::cout << "[fine_adjust] R setFreq FAILED (8x100ms retries exhausted)\n";
            if (left_needs) reliable_stop_one(se3_left);
            return "fine_adjust_setfreq_r_fail";
        }
        if (reliable_run_one(se3_right, R_pay, 6, 200)) {
            std::cout << "[fine_adjust] R run cmd FAILED (Modbus / SE3 firmware reject, 6x200ms exhausted)\n";
            if (left_needs) reliable_stop_one(se3_left);
            return "fine_adjust_run_r_fail";
        }
        if (R_use_kick) {
            std::cout << "[fine_adjust] R kick start at " << kick_hz << "Hz for " << KICK_DURATION_MS
                      << "ms (distance=" << R_distance << "cm)\n";
        } else {
            std::cout << "[fine_adjust] R direct start at " << hz << "Hz (distance="
                      << R_distance << "cm < " << KICK_DISTANCE_THRESHOLD_CM << "cm, no kick)\n";
        }
    }

    // Sleep kick duration only if any side actually used kick. Otherwise both
    // sides are already at precision hz, skip the wait so convergence loop
    // can start polling immediately.
    if (L_use_kick || R_use_kick) {
        std::this_thread::sleep_for(std::chrono::milliseconds(KICK_DURATION_MS));
    }

    // Drop kicking sides from kick_hz to precision hz; non-kicking sides are
    // already at hz (set at start). Diagnostic status read on every started side.
    if (left_needs) {
        if (L_use_kick) se3_left.setFreqHz(hz, SE3_MAX_HZ);   // drop to precision Hz
        uint16_t st = 0;
        if (!se3_left.readStatusWord(st)) {
            std::cout << "[fine_adjust] L post-start status=0x" << std::hex << st << std::dec
                      << " running=" << ((st&0x01)?1:0)
                      << " fwd=" << ((st&0x02)?1:0)
                      << " rev=" << ((st&0x04)?1:0)
                      << " fault=" << ((st&0x80)?1:0)
                      << " (target Hz=" << hz << ")\n";
        }
    }
    if (right_needs) {
        if (R_use_kick) se3_right.setFreqHz(hz, SE3_MAX_HZ);
        uint16_t st = 0;
        if (!se3_right.readStatusWord(st)) {
            std::cout << "[fine_adjust] R post-start status=0x" << std::hex << st << std::dec
                      << " running=" << ((st&0x01)?1:0)
                      << " fwd=" << ((st&0x02)?1:0)
                      << " rev=" << ((st&0x04)?1:0)
                      << " fault=" << ((st&0x80)?1:0)
                      << " (target Hz=" << hz << ")\n";
        }
    }

    // Sides already at target start "done"
    bool left_done  = !left_needs;
    bool right_done = !right_needs;
    const auto fine_start = std::chrono::steady_clock::now();
    auto last_evt_push    = fine_start;
    auto last_console_log = fine_start;
    std::string fa_abort;

    while (!left_done || !right_done) {
        if (abort_flag.load()) {
            std::cout << "[fine_adjust] aborted (abort_flag set)\n";
            fa_abort = "aborted"; break;
        }
        const auto el = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - fine_start).count();
        if (el > FINE_ADJUST_TIMEOUT_MS) {
            std::cout << "[fine_adjust] TIMEOUT after " << el << "ms — L_done=" << left_done
                      << " R_done=" << right_done << "\n";
            fa_abort = "fine_adjust_timeout"; break;
        }

        // Tension safety
        {
            double l_kg = 0, r_kg = 0;
            const std::string alarm = tension_safety_check(l_kg, r_kg);
            if (!alarm.empty()) {
                broadcast_tension_alarm(alarm, l_kg, r_kg);
                fa_abort = "tension_" + alarm;
                break;
            }
        }

        int curL = 0, curR = 0;
        bool L_valid = false, R_valid = false;
        if (g_length_left_valid.load())  { curL = (int)g_length_left .load(); L_valid = true; }
        if (g_length_right_valid.load()) { curR = (int)g_length_right.load(); R_valid = true; }

        // Per-side stop: crossed inner boundary of target zone
        if (!left_done && L_valid) {
            const bool reached = L_approach_from_below ? (curL >= L_stop_at) : (curL <= L_stop_at);
            if (reached) {
                reliable_stop_one(se3_left);
                left_done = true;
                std::cout << "[fine_adjust] left converged at " << curL
                          << " (target=" << target_cm << ")\n";
            }
        }
        if (!right_done && R_valid) {
            const bool reached = R_approach_from_below ? (curR >= R_stop_at) : (curR <= R_stop_at);
            if (reached) {
                reliable_stop_one(se3_right);
                right_done = true;
                std::cout << "[fine_adjust] right converged at " << curR
                          << " (target=" << target_cm << ")\n";
            }
        }

        // EVT motion_progress push (TCP broadcast)
        {
            const auto now_pt = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now_pt - last_evt_push).count() >= EVT_PROGRESS_INTERVAL_MS) {
                last_evt_push = now_pt;
                std::ostringstream evt;
                evt << "EVT motion_progress phase=fine_adjust_sync"
                    << " l_cm=" << (L_valid ? std::to_string(curL) : std::string("?"))
                    << " r_cm=" << (R_valid ? std::to_string(curR) : std::string("?"))
                    << " target_cm=" << target_cm
                    << " tolerance_cm=" << FINE_ADJUST_TOLERANCE_CM
                    << " elapsed_ms=" << el
                    << "\n";
                broadcast_evt(evt.str());
            }
        }
        // Console progress every 2s — TCP EVT may not reach crane console
        // operator; this gives bench visibility during convergence.
        {
            const auto now_pt = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now_pt - last_console_log).count() >= 2000) {
                last_console_log = now_pt;
                std::cout << "[fine_adjust] progress L=" << (L_valid ? std::to_string(curL) : std::string("?"))
                          << (left_done ? "(done)" : "")
                          << " R=" << (R_valid ? std::to_string(curR) : std::string("?"))
                          << (right_done ? "(done)" : "")
                          << " target=" << target_cm
                          << " elapsed=" << el << "ms\n";
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }

    // Safety: stop any still-running side (abort / timeout / tension paths)
    if (!left_done)  reliable_stop_one(se3_left);
    if (!right_done) reliable_stop_one(se3_right);

    // Wait for motor ramp + drum settling, then log final cache. Useful for
    // verifying convergence matched the panel reading. ~1s covers P.8 decel
    // ramp + meter_loop polling refresh.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    int32_t finalL = g_length_left .load();
    int32_t finalR = g_length_right.load();

    // Overrun correction passes:
    // kick (20Hz) + stopDecel (P.8 ramp) under heavy load can overrun the
    // target zone by 10+ cm. Bench 2026-05-13 observed L_init=-67 → converged
    // at -72 → final -85 (13cm overshoot past target=-72). The main converge
    // loop only checks "reached", not "stayed within tol after settle".
    //
    // Run up to MAX_CORRECTION_PASSES of slow reverse-direction approach to
    // bring each side back within FINE_ADJUST_TOLERANCE_CM. Skip if aborted /
    // timed out (those paths already stopped everything for safety).
    constexpr int MAX_CORRECTION_PASSES = 2;
    constexpr int CORRECTION_TIMEOUT_MS = 5000;
    const double correction_hz = std::min(g_fine_adjust_hz.load(), 8.0);
    if (fa_abort.empty()) {
        for (int pass = 1; pass <= MAX_CORRECTION_PASSES; ++pass) {
            const int errL = finalL - target_cm;
            const int errR = finalR - target_cm;
            const bool need_l = std::abs(errL) > FINE_ADJUST_TOLERANCE_CM;
            const bool need_r = std::abs(errR) > FINE_ADJUST_TOLERANCE_CM;
            if (!need_l && !need_r) break;

            std::cout << "[fine_adjust] overrun correction pass " << pass
                      << " L=" << finalL << " (err=" << errL << ")"
                      << " R=" << finalR << " (err=" << errR << ")"
                      << " target=" << target_cm
                      << " @ " << correction_hz << "Hz\n";

            // err > 0 (cur > target) → need cur ↓ → retract (display ↓)
            // err < 0 (cur < target) → need cur ↑ → pay_out (display ↑)
            // (PAY_OUT_INCREASES_DISPLAY = true convention)
            bool L_corr_done = !need_l, R_corr_done = !need_r;
            const bool L_dir_pay_out = (errL < 0);
            const bool R_dir_pay_out = (errR < 0);

            // SE3 reversal lockout: firmware rejects RUN cmd in opposite
            // direction for ~1-2s after stop (protects motor from back-EMF
            // shock). Bench 2026-05-13 saw "correction L start FAILED" when
            // main motion was retract (STF) and correction needed pay_out
            // (STR). Extra 2s settle on top of the 1s final-read sleep
            // gives ~3s total since stopDecel issued, enough for lockout.
            const bool L_reversing = need_l && (L_dir_pay_out != main_motion_pay_out);
            const bool R_reversing = need_r && (R_dir_pay_out != main_motion_pay_out);
            if (L_reversing || R_reversing) {
                std::cout << "[fine_adjust] correction needs direction reversal — "
                          << "extra 2s settle (L_rev=" << L_reversing
                          << " R_rev=" << R_reversing << ")\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            }

            if (need_l) {
                if (reliable_setfreq_one(se3_left, correction_hz)) {
                    std::cout << "[fine_adjust] correction L setFreq FAILED (8x100ms exhausted)\n";
                    reliable_stop_one(se3_left);
                    if (need_r) reliable_stop_one(se3_right);
                    break;
                }
                if (reliable_run_one(se3_left, L_dir_pay_out, 6, 200)) {
                    std::cout << "[fine_adjust] correction L run cmd FAILED (Modbus / SE3 reject, 6x200ms exhausted)"
                              << " — dir_pay_out=" << L_dir_pay_out
                              << " main_was_pay_out=" << main_motion_pay_out
                              << " (reversing=" << L_reversing << ")\n";
                    reliable_stop_one(se3_left);
                    if (need_r) reliable_stop_one(se3_right);
                    break;
                }
            }
            if (need_r) {
                if (reliable_setfreq_one(se3_right, correction_hz)) {
                    std::cout << "[fine_adjust] correction R setFreq FAILED (8x100ms exhausted)\n";
                    if (need_l) reliable_stop_one(se3_left);
                    reliable_stop_one(se3_right);
                    break;
                }
                if (reliable_run_one(se3_right, R_dir_pay_out, 6, 200)) {
                    std::cout << "[fine_adjust] correction R run cmd FAILED (Modbus / SE3 reject, 6x200ms exhausted)"
                              << " — dir_pay_out=" << R_dir_pay_out
                              << " main_was_pay_out=" << main_motion_pay_out
                              << " (reversing=" << R_reversing << ")\n";
                    if (need_l) reliable_stop_one(se3_left);
                    reliable_stop_one(se3_right);
                    break;
                }
            }

            // Poll until each side crosses target zone (sign flip or within tol)
            const auto corr_start = std::chrono::steady_clock::now();
            bool corr_aborted = false;
            while (!L_corr_done || !R_corr_done) {
                if (abort_flag.load()) { corr_aborted = true; break; }
                const auto el = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - corr_start).count();
                if (el > CORRECTION_TIMEOUT_MS) {
                    std::cout << "[fine_adjust] correction pass " << pass << " TIMEOUT\n";
                    break;
                }
                if (!L_corr_done) {
                    const int curL = (int)g_length_left.load();
                    const int new_err = curL - target_cm;
                    // Sign flip → overshot in opposite direction; within tol → done
                    if (std::abs(new_err) <= FINE_ADJUST_TOLERANCE_CM ||
                        (errL > 0 && new_err <= 0) ||
                        (errL < 0 && new_err >= 0)) {
                        reliable_stop_one(se3_left);
                        L_corr_done = true;
                        std::cout << "[fine_adjust] correction L stop at " << curL << "\n";
                    }
                }
                if (!R_corr_done) {
                    const int curR = (int)g_length_right.load();
                    const int new_err = curR - target_cm;
                    if (std::abs(new_err) <= FINE_ADJUST_TOLERANCE_CM ||
                        (errR > 0 && new_err <= 0) ||
                        (errR < 0 && new_err >= 0)) {
                        reliable_stop_one(se3_right);
                        R_corr_done = true;
                        std::cout << "[fine_adjust] correction R stop at " << curR << "\n";
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
            }
            // Safety stop on timeout / abort
            if (!L_corr_done) reliable_stop_one(se3_left);
            if (!R_corr_done) reliable_stop_one(se3_right);
            if (corr_aborted) { fa_abort = "aborted"; break; }

            // Settle, re-read for next pass evaluation / final log
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            finalL = g_length_left .load();
            finalR = g_length_right.load();
        }
    }

    std::cout << "[fine_adjust] final L=" << finalL << " R=" << finalR
              << " target=" << target_cm
              << " (L_init=" << curL_init << " R_init=" << curR_init << ")"
              << (fa_abort.empty() ? " — done" : (" — abort: " + fa_abort))
              << "\n";
    return fa_abort;
}

static std::string motion_rope(int cm, bool is_retract) {
    if (cm <= 0) return "ERR invalid_cm\n";

    // motion_rope needs both ropes (SE3 + SD76 each side) + BOTH tension
    // sensors for safety. Middle pipeline (SD76 middle + CLV900) is OPTIONAL —
    // bench usually has no middle conduit, so we skip middle motion if either
    // device is unavailable or its cache is invalid. Production deployment
    // requires middle; operator should verify all 4 light up at startup.
    if (!g_dev_se3_left.load())    return "ERR se3_left_unavailable\n";
    if (!g_dev_se3_right.load())   return "ERR se3_right_unavailable\n";
    if (!g_dev_meter_left.load())  return "ERR meter_left_unavailable\n";
    if (!g_dev_meter_right.load()) return "ERR meter_right_unavailable\n";
    if (!g_dev_dsz_left.load())    return "ERR dsz_left_unavailable\n";
    if (!g_dev_dsz_right.load())   return "ERR dsz_right_unavailable\n";

    // try_lock instead of blocking lock — reject duplicate / overlapping
    // motion commands instead of queueing them. Bench 2026-05-13: GUI sent
    // one pay_out but server received two, second auto-ran after first done.
    std::unique_lock<std::mutex> lock(motion_mtx, std::try_to_lock);
    if (!lock.owns_lock()) return "ERR motion_busy\n";

    MotionScope ms;
    MotionTimeoutScope mts(cm);
    abort_flag = false;

    // Read base from meter_loop's atomic cache (250ms refresh) instead of
    // direct Modbus to avoid bus contention with meter_loop on cli_M (both
    // SD76 share the sensing bus — physical-separation layout 2026-05-15).
    // Bench observed `ERR meter_middle_read_fail` consistently when this
    // function did its own readUpperInteger — likely stale-buffer / race
    // with concurrent meter_loop polling. Cache freshness (≤250 ms) is fine
    // for baseline + cm-level distance targeting.
    if (!g_length_left_valid.load())   return "ERR meter_left_read_fail\n";
    if (!g_length_right_valid.load())  return "ERR meter_right_read_fail\n";
    const int32_t base_left   = g_length_left  .load();
    const int32_t base_right  = g_length_right .load();

    // Middle pipeline (SD76 middle + CLV900) is currently DISABLED at init
    // (2026-05-14, hardware not installed). g_dev_meter_middle / g_dev_clv900
    // always false → use_middle = false → all middle branches skip silently.
    // When middle hardware is installed, restore init() calls in the USR_A
    // init block and this logic re-engages automatically.
    const bool use_middle = g_dev_meter_middle.load() && g_dev_clv900.load()
                          && g_length_middle_valid.load();
    const int32_t base_middle = use_middle ? g_length_middle.load() : 0;

    const bool pay_out = !is_retract;
    const int  middle_target_cm = (int)std::lround(cm * MIDDLE_WINCH_RATIO_K);

    // Two-phase sync start: setFreq with retry first (Phase A absorbs variable
    // latency), then run command on both sides at near-same wall time (Phase B,
    // no retry). Drift between sides ~10ms vs concurrent-with-bundled-retry's
    // 80-700ms when one side hits a transient. See dual_se3_sync_start doc.
    if (dual_se3_sync_start(g_se3_motion_hz.load(), pay_out, pay_out)) {
        return "ERR se3_start_fail\n";
    }
    if (use_middle && middleStart(pay_out)) {
        allMotionOff();
        return "ERR inverter_start_fail\n";
    }

    const auto start = std::chrono::steady_clock::now();
    // Each side does reliable_stop_one when its displayed delta reaches cm.
    //
    // Was previously setFreq(freeze_hz=4Hz) "freeze" to keep motor running and
    // avoid cold-start stall for fine_adjust's reverse direction. But that
    // design conflicted with driver-level watchdog:
    //   1. Once a side reached target it kept running at 4Hz, accumulating
    //      additional displacement until the other side caught up.
    //   2. When fine_adjust then issued a run cmd, if H1001 transient-failed
    //      twice (~6% bench fail rate on L), driver watchdog tried to re-claim
    //      CU mode via H1000=0 write — but SE3 firmware rejects H1000 writes
    //      while motor is running, leaving the watchdog in a dead loop.
    //
    // Now: stop on target reach. fine_adjust always starts from stationary +
    // already has kick start (20Hz × 500ms) to break cold-start static friction.
    bool left_frozen = false, right_frozen = false, middle_done = !use_middle;
    bool retract_tension_stopped = false;   // retract finished early on soft tension
    std::string abort_reason;

    auto last_balance_tick = start;
    auto last_evt_push     = start;
    bool was_trimmed = false;
    const int balance_dir = pay_out ? +1 : -1;
    // Meter-death tracking: elapsed_ms when each side first went invalid (0=ok).
    int64_t left_invalid_since   = 0;
    int64_t right_invalid_since  = 0;
    int64_t middle_invalid_since = 0;

    while (!(left_frozen && right_frozen && middle_done)) {
        if (abort_flag.load()) { abort_reason = "aborted"; break; }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > MOTION_TIMEOUT_MS) { abort_reason = "timeout"; break; }

        // SE3 fault detection: if either side is in fault (OPT alarm etc),
        // abort BOTH immediately. keepalive thread sets these atomics based
        // on status word b7. Safety principle: one rope stuck = abort both
        // (asymmetric load on crane structure is dangerous).
        if (g_se3_left_fault.load() || g_se3_right_fault.load()) {
            const bool lf = g_se3_left_fault.load();
            const bool rf = g_se3_right_fault.load();
            abort_reason = (lf && rf) ? "se3_both_fault"
                         : lf         ? "se3_left_fault"
                                      : "se3_right_fault";
            break;
        }

        // Meter-death detection: if any required meter stays invalid > grace,
        // abort with meter_<side>_lost. Without this, distance check never
        // fires for that side → motor runs uncontrolled until MOTION_TIMEOUT_MS.
        {
            auto check_meter = [&](bool valid, int64_t& since,
                                   const char* name) -> bool {
                if (valid) { since = 0; return false; }
                if (since == 0) since = elapsed;
                else if (elapsed - since > METER_LOST_GRACE_MS) {
                    abort_reason = std::string("meter_") + name + "_lost";
                    return true;
                }
                return false;
            };
            if (check_meter(g_length_left_valid .load(), left_invalid_since,  "left"))  break;
            if (check_meter(g_length_right_valid.load(), right_invalid_since, "right")) break;
            if (use_middle &&
                check_meter(g_length_middle_valid.load(), middle_invalid_since, "middle")) break;
        }

        // Tension safety: read DSZL-107 every iter.
        // Read failure is transient (e.g., one corrupt frame) — skip this iter.
        {
            double l_kg = 0, r_kg = 0;
            if (!read_tensions(l_kg, r_kg)) {
                // Soft retract stop: for a RETRACT, rising tension means the
                // rope has gone taut = all slack collected = goal reached.
                // Stop both sides and finish NORMALLY — a successful
                // completion, NOT the hard overload alarm below (no EVT
                // tension_alarm, no abort_reason). pay_out is exempt.
                if (is_retract &&
                    std::max(l_kg, r_kg) >= g_retract_tension_stop_kg.load()) {
                    dual_se3_sync_retry([](SE3_inverter& inv){ return inv.stopDecel(); },
                                        8, 100, "motion_rope.tension_soft_stop");
                    if (use_middle) inverter.stopDecel();
                    std::cout << "[motion_rope] soft tension stop — L=" << l_kg
                              << "kg R=" << r_kg << "kg >= "
                              << g_retract_tension_stop_kg.load()
                              << "kg (slack collected, retract done)\n";
                    retract_tension_stopped = true;
                    break;
                }
                // Hard overload / imbalance alarm → abort + EVT.
                const std::string alarm = tension_safety_check_values(l_kg, r_kg);
                if (!alarm.empty()) {
                    broadcast_tension_alarm(alarm, l_kg, r_kg);
                    abort_reason = "tension_" + alarm;
                    break;
                }
            }
        }

        // Sync stop on FIRST side to reach target (2026-05-15): matches hold
        // mode semantic — when user releases hold button, both sides stop
        // together via dual_se3_sync_retry. For motion_rope, "leader reaches
        // target" is the analog trigger. Follower stops short by however much
        // it was lagging; fine_adjust picks up the residual after this loop.
        // Pre-2026-05-15 had per-side independent stop → drift between L and R
        // was visible during stop ramp (one side decelerating while other
        // still at full speed). Sync stop eliminates this asymmetry window.
        if (!left_frozen && !right_frozen) {
            const bool l_valid = g_length_left_valid.load();
            const bool r_valid = g_length_right_valid.load();
            const bool l_reached = l_valid &&
                std::abs(g_length_left.load()  - base_left)  >= cm;
            const bool r_reached = r_valid &&
                std::abs(g_length_right.load() - base_right) >= cm;
            if (l_reached || r_reached) {
                const int32_t curL = g_length_left.load();
                const int32_t curR = g_length_right.load();
                const char* leader = l_reached ? (r_reached ? "BOTH" : "L") : "R";
                // Sync stop both (matches hold_off pattern). 8x retry covers
                // transient Modbus failures so neither side is left running.
                dual_se3_sync_retry([](SE3_inverter& inv){ return inv.stopDecel(); },
                                    8, 100, "motion_rope.sync_stop");
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                const int32_t afterL = g_length_left.load();
                const int32_t afterR = g_length_right.load();
                std::cout << "[motion_rope] sync stop (leader=" << leader << ") "
                          << "trigger L=" << curL << " R=" << curR
                          << " base_L=" << base_left << " base_R=" << base_right
                          << " delta_L=" << (curL - base_left)
                          << " delta_R=" << (curR - base_right)
                          << " target=" << (is_retract ? -cm : cm)
                          << " after_200ms L=" << afterL << " R=" << afterR << "\n";
                left_frozen  = true;
                right_frozen = true;
            }
        }
        // Middle still uses stopDecel (no cold-start concern for middle winch).
        if (use_middle && !middle_done && g_length_middle_valid.load() &&
            std::abs(g_length_middle.load() - base_middle) >= middle_target_cm) {
            inverter.stopDecel();
            middle_done = true;
        }

        // Balance trim: tick every BALANCE_TICK_MS while NEITHER side is frozen.
        // Once either freezes, balance trim would fight the freeze freq.
        if (!left_frozen && !right_frozen) {
            const auto now_pt = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now_pt - last_balance_tick).count() >= BALANCE_TICK_MS) {
                last_balance_tick = now_pt;
                apply_balance_trim(g_se3_motion_hz.load(), balance_dir,
                                   base_left, base_right, was_trimmed);
            }
        }

        // EVT motion_progress push every ~EVT_PROGRESS_INTERVAL_MS — lets
        // washrobot's watchdog refresh during long ops, GUI sees live progress.
        {
            const auto now_pt = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now_pt - last_evt_push).count() >= EVT_PROGRESS_INTERVAL_MS) {
                last_evt_push = now_pt;
                std::ostringstream evt;
                evt << "EVT motion_progress phase=main_loop"
                    << " l_cm=" << balance_dir * (g_length_left .load() - base_left)
                    << " r_cm=" << balance_dir * (g_length_right.load() - base_right);
                if (use_middle)
                    evt << " m_cm=" << balance_dir * (g_length_middle.load() - base_middle);
                evt << " target_cm=" << cm
                    << " elapsed_ms=" << elapsed
                    << "\n";
                broadcast_evt(evt.str());
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }

    // Soft tension stop (retract only): rope went taut = slack collected = goal
    // reached. No emergency stop, no fine_adjust (it would just re-build
    // tension by retracting toward the cm target) — return OK so the caller
    // treats it as a normal completion, not an error / PausedOnError.
    if (retract_tension_stopped) {
        std::cout << "[motion_rope] retract finished early on tension (soft stop) — OK\n";
        return "OK tension_reached\n";
    }

    // For tension / meter / se3 fault aborts: hard-stop everything via
    // allMotionOff (extra safety; main loop already stopped on target reach).
    // For success / timeout / user-abort: motors already stopped on target
    // reach; fine_adjust starts each side from stationary.
    const bool tension_aborted = (abort_reason.compare(0, 8, "tension_") == 0);
    const bool meter_aborted   = (abort_reason.compare(0, 6, "meter_") == 0);
    const bool se3_aborted     = (abort_reason.compare(0, 4, "se3_") == 0);

    if (tension_aborted || meter_aborted || se3_aborted) {
        // EMERGENCY hard stop — switched from allMotionOff() (decel ramp via
        // reliable_stop_one, ~800ms + P.8 ramp) to allMotionEmergencyStop()
        // for safety critical aborts (2026-05-14). When one side comm-dead
        // during pay_out, we can't reach it anyway (Modbus writes fail), so
        // the priority is to stop the LIVE side AS FAST AS POSSIBLE so the
        // robot doesn't tilt while the dead side waits for its own SE3 07-09
        // OPT timeout (panel-side, typically 2-4s). emergencyStop = MRS bit
        // = output cutoff in ~50ms. Then stopDecel to clear MRS for next cmd.
        allMotionEmergencyStop();
        dual_se3_concurrent([](SE3_inverter& inv){ return inv.stopDecel(); });   // clear MRS
    }

    // Broadcast EVT for hard-abort reasons so washrobot/GUI sees specific cause
    if (meter_aborted || se3_aborted) {
        std::ostringstream evt;
        evt << "EVT motion_abort reason=" << abort_reason << "\n";
        broadcast_evt(evt.str());
    }

    // Sync fine_adjust: motors are stopped on entry. Function kick-starts
    // the lagging side(s) to bring L and R into alignment-to-leader within
    // FINE_ADJUST_TOLERANCE_CM, then overrun correction (≤2 passes).
    //
    // Run on: success, timeout, user-abort, watchdog-abort.
    // SKIP on: tension safety breach, meter death, SE3 fault.
    //
    // For user-abort: reset abort_flag so fine_adjust loop's own abort check
    // doesn't immediately fire. User can press stop again to interrupt.
    if (!tension_aborted && !meter_aborted && !se3_aborted) {
        if (abort_reason == "aborted") {
            abort_flag.exchange(false);
            std::cout << "[fine_adjust] post-abort cleanup attempt (abort_flag reset; "
                      << "press stop again to interrupt)\n";
        }
        const std::string fa_reason = motion_fine_adjust_sync(pay_out);
        if (!fa_reason.empty()) {
            allMotionOff();   // safety: ensure both stopped on error path
            if (abort_reason.empty()) abort_reason = fa_reason;
        }
    } else {
        std::cout << "[fine_adjust] skipped (" << abort_reason << ")\n";
    }

    if (!abort_reason.empty()) return "ERR " + abort_reason + "\n";
    return "OK\n";
}

// ============ roll_correct: differential (middle winch idle) ============

static std::string cmd_roll_correct(int delta_cm) {
    // +delta = 左放右收 |delta| cm;  -delta = 左收右放
    if (delta_cm == 0) return "OK\n";

    // roll_correct needs both ropes + tension safety. Middle pipeline not used.
    if (!g_dev_se3_left.load())    return "ERR se3_left_unavailable\n";
    if (!g_dev_se3_right.load())   return "ERR se3_right_unavailable\n";
    if (!g_dev_meter_left.load())  return "ERR meter_left_unavailable\n";
    if (!g_dev_meter_right.load()) return "ERR meter_right_unavailable\n";
    if (!g_dev_dsz_left.load())    return "ERR dsz_left_unavailable\n";
    if (!g_dev_dsz_right.load())   return "ERR dsz_right_unavailable\n";

    std::unique_lock<std::mutex> lock(motion_mtx, std::try_to_lock);
    if (!lock.owns_lock()) return "ERR motion_busy\n";

    MotionScope ms;
    const int  abs_cm   = std::abs(delta_cm);
    MotionTimeoutScope mts(abs_cm);
    abort_flag = false;

    const bool left_pay = (delta_cm > 0);   // +delta = 左放右收

    // Read base from meter_loop's cache (see motion_rope for rationale)
    if (!g_length_left_valid.load())  return "ERR meter_left_read_fail\n";
    if (!g_length_right_valid.load()) return "ERR meter_right_read_fail\n";
    const int32_t base_left  = g_length_left .load();
    const int32_t base_right = g_length_right.load();

    // Two-phase sync start with opposing directions (left / right move
    // opposite ways for differential roll correction). Same rationale as
    // motion_rope — see dual_se3_sync_start doc.
    if (dual_se3_sync_start(g_se3_motion_hz.load(), left_pay, !left_pay)) {
        return "ERR se3_start_fail\n";
    }

    const auto start = std::chrono::steady_clock::now();
    bool left_done = false, right_done = false;
    std::string abort_reason;
    int64_t left_invalid_since = 0, right_invalid_since = 0;

    while (!(left_done && right_done)) {
        if (abort_flag.load()) { abort_reason = "aborted"; break; }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > MOTION_TIMEOUT_MS) { abort_reason = "timeout"; break; }

        // SE3 fault detection (same as motion_rope): if either side in fault,
        // abort both. Critical for roll_correct which moves L/R opposite —
        // one side stuck while other moves causes large tilt asymmetry.
        if (g_se3_left_fault.load() || g_se3_right_fault.load()) {
            const bool lf = g_se3_left_fault.load();
            const bool rf = g_se3_right_fault.load();
            abort_reason = (lf && rf) ? "se3_both_fault"
                         : lf         ? "se3_left_fault"
                                      : "se3_right_fault";
            break;
        }

        // Meter-death detection (same logic as motion_rope)
        {
            auto check_meter = [&](bool valid, int64_t& since,
                                   const char* name) -> bool {
                if (valid) { since = 0; return false; }
                if (since == 0) since = elapsed;
                else if (elapsed - since > METER_LOST_GRACE_MS) {
                    abort_reason = std::string("meter_") + name + "_lost";
                    return true;
                }
                return false;
            };
            if (check_meter(g_length_left_valid .load(), left_invalid_since,  "left"))  break;
            if (check_meter(g_length_right_valid.load(), right_invalid_since, "right")) break;
        }

        // Tension safety (same logic as motion_rope)
        {
            double l_kg = 0, r_kg = 0;
            const std::string alarm = tension_safety_check(l_kg, r_kg);
            if (!alarm.empty()) {
                broadcast_tension_alarm(alarm, l_kg, r_kg);
                abort_reason = "tension_" + alarm;
                break;
            }
        }

        // Each side stops when its displayed delta reaches abs_cm.
        if (!left_done && g_length_left_valid.load() &&
            std::abs(g_length_left.load() - base_left) >= abs_cm) {
            reliable_stop_one(se3_left);
            left_done = true;
        }
        if (!right_done && g_length_right_valid.load() &&
            std::abs(g_length_right.load() - base_right) >= abs_cm) {
            reliable_stop_one(se3_right);
            right_done = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }

    allRopeInvertersOff();

    // EVT broadcast for hard-abort reasons (meter / se3 fault) so GUI sees cause
    if (abort_reason.compare(0, 6, "meter_") == 0 ||
        abort_reason.compare(0, 4, "se3_")   == 0) {
        std::ostringstream evt;
        evt << "EVT motion_abort reason=" << abort_reason << "\n";
        broadcast_evt(evt.str());
    }

    if (!abort_reason.empty()) return "ERR " + abort_reason + "\n";
    return "OK\n";
}

// ============ align_lengths: align L and R to the LONGER side ============
//
// Reads current L, R from cache. The side with the LARGER display (paid out
// more rope, hangs lower) is the standard; the shorter side runs pay_out
// (display ↑ per PAY_OUT_INCREASES_DISPLAY=true) until reaching target ±tol.
//
// Triggered manually (e.g., GUI button). Acquires motion_mtx so it can't run
// concurrently with motion_rope / cmd_roll_correct. Cold-start risk: motors
// at rest at entry, lagging side needs to spin up — may stall on heavy load.
// If stall, returns "fine_adjust_timeout" after 30s.
//
// Standard safety checks: SE3 fault, meter availability, tension monitoring.
static std::string cmd_align_lengths() {
    if (!g_dev_se3_left.load())    return "ERR se3_left_unavailable\n";
    if (!g_dev_se3_right.load())   return "ERR se3_right_unavailable\n";
    if (!g_dev_meter_left.load())  return "ERR meter_left_unavailable\n";
    if (!g_dev_meter_right.load()) return "ERR meter_right_unavailable\n";
    if (!g_dev_dsz_left.load())    return "ERR dsz_left_unavailable\n";
    if (!g_dev_dsz_right.load())   return "ERR dsz_right_unavailable\n";

    // Refuse if either SE3 is in fault (would silently fail to start)
    if (g_se3_left_fault.load() || g_se3_right_fault.load()) {
        return "ERR se3_fault_pending\n";
    }

    std::unique_lock<std::mutex> lock(motion_mtx, std::try_to_lock);
    if (!lock.owns_lock()) return "ERR motion_busy\n";

    MotionScope ms;
    abort_flag = false;

    // Force CU mode re-claim on both SE3 — defensive against stale cached
    // cu_mode_set_ flag (bench observed 2-3 silent failures before working).
    // Next run cmd from fine_adjust will write H1000=0 + 150ms sleep before
    // the actual H1001 run write.
    se3_left.invalidateCuModeCache();
    se3_right.invalidateCuModeCache();

    // Snapshot for log
    const int32_t curL_init = g_length_left.load();
    const int32_t curR_init = g_length_right.load();
    std::cout << "[align_lengths] L=" << curL_init << " R=" << curR_init
              << " target=max=" << std::max(curL_init, curR_init)
              << " (shorter side pays out to match longer)\n";

    // Reuse motion_fine_adjust_sync with main_motion_pay_out=true:
    //   - target = max(L, R)
    //   - lagging side runs SE3_DIR_PAY_OUT (= STR per project bench)
    //   - converges to target ± FINE_ADJUST_TOLERANCE_CM
    const std::string fa_reason = motion_fine_adjust_sync(/*main_motion_pay_out=*/true);
    if (!fa_reason.empty()) {
        allRopeInvertersOff();
        return "ERR " + fa_reason + "\n";
    }
    return "OK\n";
}

// ============ Other handlers ============

// Raw debug-only manual control of one rope inverter. Bypasses tension safety
// monitoring — use hold mode (`up_left on` etc.) for normal operation.
//   pay_out_left/right on  → setFreqHz(SE3_HOLD_HZ) + runForward (or convention)
//   retract_left/right on  → setFreqHz(SE3_HOLD_HZ) + runReverse
//   <any> off              → stopDecel
static std::string cmd_manual(const std::string& dir, const std::string& onoff) {
    bool on;
    if      (onoff == "on")  on = true;
    else if (onoff == "off") on = false;
    else return "ERR expected_on_or_off\n";

    bool pay_out;
    SE3_inverter* inv = nullptr;
    bool side_left = false;
    if      (dir == "pay_out_left")  { inv = &se3_left;  pay_out = true;  side_left = true;  }
    else if (dir == "pay_out_right") { inv = &se3_right; pay_out = true;  side_left = false; }
    else if (dir == "retract_left")  { inv = &se3_left;  pay_out = false; side_left = true;  }
    else if (dir == "retract_right") { inv = &se3_right; pay_out = false; side_left = false; }
    else return "ERR unknown_channel\n";

    // Per-side SE3 availability check (manual = bypass tension safety, so DSZL
    // not required — caller accepted that trade-off by using cmd_manual).
    if ( side_left && !g_dev_se3_left.load())  return "ERR se3_left_unavailable\n";
    if (!side_left && !g_dev_se3_right.load()) return "ERR se3_right_unavailable\n";

    if (!on) {
        if (reliable_stop_one(*inv)) return "ERR se3_stop_fail\n";
        return "OK\n";
    }
    if (se3StartRopeHold(*inv, pay_out)) return "ERR se3_start_fail\n";
    return "OK\n";
}

static std::string cmd_middle_set(int rpm, const std::string& dir) {
    if (!g_dev_clv900.load()) return "ERR clv900_unavailable\n";

    if (dir == "stop") {
        if (inverter.stopDecel()) return "ERR inverter_stop_fail\n";
        return "OK\n";
    }
    if (rpm <= 0) return "ERR invalid_rpm\n";

    // TODO: proper rpm → Hz needs motor pole count. Interim: assume 4-pole
    // 50 Hz → 1500 rpm synchronous. Recalibrate on site.
    const double hz = rpm * 50.0 / 1500.0;

    if (inverter.setFreqHz(hz, CLV900_MAX_HZ)) return "ERR inverter_freq_fail\n";
    if (dir == "pay") {
        if (inverter.runForward()) return "ERR inverter_run_fail\n";
    } else if (dir == "retract") {
        if (inverter.runReverse()) return "ERR inverter_run_fail\n";
    } else {
        return "ERR expected_pay_retract_stop\n";
    }
    return "OK\n";
}

static std::string cmd_zero_meters(const std::string& mode) {
    if (mode != "ground" && mode != "top") return "ERR expected_ground_or_top\n";

    // Left + right are mandatory (they're the rope-length references and are
    // tied to home_ground_cm). Middle is OPTIONAL — gracefully skip if not
    // installed (bench config 2026-05-14e disables meter_middle when middle
    // pipeline hardware not present). Without this skip, both 三條歸零 buttons
    // return ERR meter_middle_unavailable on bench.
    if (!g_dev_meter_left.load())   return "ERR meter_left_unavailable\n";
    if (!g_dev_meter_right.load())  return "ERR meter_right_unavailable\n";
    const bool reset_middle = g_dev_meter_middle.load();

    if (mode == "top") {
        // Store |left SD76| before reset: rope length from top to ground.
        // Left used as reference (left/right symmetric; middle uses K ratio).
        int32_t left_cur = 0;
        if (meter_left.readUpperInteger(left_cur)) return "ERR meter_left_read_fail\n";
        home_ground_cm.store(std::abs(left_cur));
    }

    if (meter_left.resetAll())   return "ERR meter_left_reset_fail\n";
    if (meter_right.resetAll())  return "ERR meter_right_reset_fail\n";
    if (reset_middle && meter_middle.resetAll()) return "ERR meter_middle_reset_fail\n";

    // resetAll (write 0x0003) zeros the count but does NOT guarantee leaving
    // a paused state. Always follow up with resumeMeter (write 0x0008) so the
    // meter actively counts after zero. (best-effort; ignored if it fails)
    meter_left  .resumeMeter();
    meter_right .resumeMeter();
    if (reset_middle) meter_middle.resumeMeter();

    // Verify reset actually applied — bench 2026-05-14 observed cases where the
    // resetAll Modbus write returns success (no protocol error) but SD76
    // firmware silently ignored the command and counter stays at the old value
    // (same mode-latch pattern as project_sd76_panel_mode_latch.md for DP).
    // Refresh atomic cache too so cmd_status reflects post-reset state on the
    // next GUI status poll (meter_loop's 250ms cycle would otherwise show old
    // value for up to 250ms after this command returns OK).
    int32_t left_after = 0, right_after = 0, middle_after = 0;
    const bool left_rb_ok   = !meter_left .readUpperInteger(left_after);
    const bool right_rb_ok  = !meter_right.readUpperInteger(right_after);
    const bool middle_rb_ok = reset_middle ? !meter_middle.readUpperInteger(middle_after) : true;
    // On readback success: store new value + mark valid (GUI sees 0 on next
    // status poll without waiting for meter_loop's 250ms cycle).
    // On readback FAIL: SD76 was actually reset but our readback hit cli jitter.
    // Must INVALIDATE cache, not leave it at old pre-reset value — otherwise
    // sanity filter (jump > 30cm) will reject every legitimate post-reset
    // read from meter_loop, and GUI stays stuck on old number forever.
    if (left_rb_ok)  { g_length_left .store(left_after);  g_length_left_valid .store(true); }
    else             {                                     g_length_left_valid .store(false); }
    if (right_rb_ok) { g_length_right.store(right_after); g_length_right_valid.store(true); }
    else             {                                     g_length_right_valid.store(false); }
    if (reset_middle) {
        if (middle_rb_ok) { g_length_middle.store(middle_after); g_length_middle_valid.store(true); }
        else              {                                       g_length_middle_valid.store(false); }
    }

    // Invalidate cal baselines — they were captured against the old origin.
    // (scale itself is unaffected — wheel circumference doesn't change.)
    g_meter_left_cal_baseline  .store(METER_CAL_UNSET);
    g_meter_right_cal_baseline .store(METER_CAL_UNSET);
    if (reset_middle) g_meter_middle_cal_baseline.store(METER_CAL_UNSET);

    // Reply embeds the post-reset readback so user can diagnose "I pressed
    // zero but display didn't change" — if left_after≠0 here, the SD76 didn't
    // actually reset (firmware mode-latch or RS485 byte loss on the write).
    // ZERO_TOLERANCE_CM: allow small residual count from any motion during reset.
    constexpr int32_t ZERO_TOLERANCE_CM = 5;
    const bool left_zeroed   = left_rb_ok   && std::abs(left_after)   <= ZERO_TOLERANCE_CM;
    const bool right_zeroed  = right_rb_ok  && std::abs(right_after)  <= ZERO_TOLERANCE_CM;
    const bool middle_zeroed = (!reset_middle) || (middle_rb_ok && std::abs(middle_after) <= ZERO_TOLERANCE_CM);

    std::ostringstream oss;
    if (left_zeroed && right_zeroed && middle_zeroed) {
        oss << "OK";
    } else {
        oss << "ERR reset_didnt_apply";
    }
    if (mode == "top") oss << " home_ground_cm=" << home_ground_cm.load();
    oss << " left_after="  << (left_rb_ok  ? std::to_string(left_after)  : std::string("READ_FAIL"));
    oss << " right_after=" << (right_rb_ok ? std::to_string(right_after) : std::string("READ_FAIL"));
    if (reset_middle) oss << " middle_after=" << (middle_rb_ok ? std::to_string(middle_after) : std::string("READ_FAIL"));
    else              oss << " middle_skipped=1";
    oss << "\n";
    return oss.str();
}

// Per-meter zero: reset only one side. Use when one of the three meters is
// unavailable (e.g., one SD76 hot-swapped, or partial init failure) and
// operator wants to recalibrate the rest. Does NOT update home_ground_cm —
// only the bulk zero_meters top does that. Mixing per-side zero with
// motion_rope creates origin drift between left and right; only use during
// commission/debug.
static std::string cmd_zero_meter(const std::string& which) {
    SD76_length_meters*   meter = nullptr;
    std::atomic<bool>*    flag  = nullptr;
    const char*           name  = nullptr;
    if      (which == "left")   { meter = &meter_left;   flag = &g_dev_meter_left;   name = "meter_left";   }
    else if (which == "right")  { meter = &meter_right;  flag = &g_dev_meter_right;  name = "meter_right";  }
    else if (which == "middle") { meter = &meter_middle; flag = &g_dev_meter_middle; name = "meter_middle"; }
    else return "ERR expected_left_right_or_middle\n";

    if (!flag->load()) {
        std::ostringstream oss; oss << "ERR " << name << "_unavailable\n";
        return oss.str();
    }
    if (meter->resetAll()) {
        std::ostringstream oss; oss << "ERR " << name << "_reset_fail\n";
        return oss.str();
    }
    // Follow-up resume (see cmd_zero_meters comment for rationale).
    meter->resumeMeter();

    // Readback verification + atomic cache refresh — see cmd_zero_meters
    // comment. On readback fail (cli jitter), must INVALIDATE cache so the
    // sanity filter (jump > 30cm) in meter_loop doesn't reject every
    // legitimate post-reset read by comparing against stale old value.
    int32_t after = 0;
    const bool rb_ok = !meter->readUpperInteger(after);
    if (rb_ok) {
        if      (which == "left")   { g_length_left  .store(after); g_length_left_valid  .store(true); }
        else if (which == "right")  { g_length_right .store(after); g_length_right_valid .store(true); }
        else if (which == "middle") { g_length_middle.store(after); g_length_middle_valid.store(true); }
    } else {
        if      (which == "left")   g_length_left_valid  .store(false);
        else if (which == "right")  g_length_right_valid .store(false);
        else if (which == "middle") g_length_middle_valid.store(false);
    }

    // Invalidate this side's cal baseline (origin shifted by reset).
    if      (which == "left")   g_meter_left_cal_baseline  .store(METER_CAL_UNSET);
    else if (which == "right")  g_meter_right_cal_baseline .store(METER_CAL_UNSET);
    else if (which == "middle") g_meter_middle_cal_baseline.store(METER_CAL_UNSET);

    constexpr int32_t ZERO_TOLERANCE_CM = 5;
    const bool zeroed = rb_ok && std::abs(after) <= ZERO_TOLERANCE_CM;
    std::ostringstream oss;
    oss << (zeroed ? "OK" : "ERR reset_didnt_apply");
    oss << " " << which << "_after=" << (rb_ok ? std::to_string(after) : std::string("READ_FAIL"));
    oss << "\n";
    return oss.str();
}

static std::string cmd_home_status() {
    // Same cache as cmd_status — avoids Modbus per home_status request.
    const int32_t l = g_length_left.load();
    const int32_t r = g_length_right.load();
    const int32_t m = g_length_middle.load();
    const bool lok = g_length_left_valid.load();
    const bool rok = g_length_right_valid.load();
    const bool mok = g_length_middle_valid.load();
    const int32_t home = home_ground_cm.load();
    const int32_t remaining = home - (lok ? std::abs(l) : 0);

    std::ostringstream oss;
    oss << "OK home_ground_cm=" << home;
    oss << " left="      << (lok ? std::to_string(l) : std::string("ERR"));
    oss << " right="     << (rok ? std::to_string(r) : std::string("ERR"));
    oss << " middle="    << (mok ? std::to_string(m) : std::string("ERR"));
    oss << " remaining=" << remaining;
    oss << "\n";
    return oss.str();
}

static std::string cmd_status() {
    // Length: read from meter_loop's atomic cache — keeps cmd_status fast
    // (just atomic loads, no Modbus). Without this, cmd_status takes ~450ms
    // and GUI's 200ms poll backs up the dispatcher queue (observed 1+ min
    // lag for user button presses). Cache freshness is METER_POLL_MS_IDLE = 250ms
    // (150ms during motion — see meter_loop motion-aware comment).
    const int32_t l = g_length_left.load();
    const int32_t r = g_length_right.load();
    const int32_t m = g_length_middle.load();
    const bool lok = g_length_left_valid.load();
    const bool rok = g_length_right_valid.load();
    const bool mok = g_length_middle_valid.load();

    // Tension: prefer hold_loop's atomic cache (no extra Modbus traffic per
    // status call). Fall back to direct read only when cache invalid (e.g.
    // hold_loop not yet polled or tension read failures).
    double tl = g_tension_left.load();
    double tr = g_tension_right.load();
    bool   tvalid = g_tension_valid.load();
    if (!tvalid) {
        // Don't fall back to direct DSZL read — it adds ~150ms × 2 to every
        // status call and brings back the queue-backup problem. If cache says
        // invalid, just report invalid and let GUI show ERR.
    }

    std::ostringstream oss;
    oss << "OK";
    oss << " length_left="     << (lok ? std::to_string(l) : std::string("ERR"));
    oss << " length_right="    << (rok ? std::to_string(r) : std::string("ERR"));
    oss << " length_middle="   << (mok ? std::to_string(m) : std::string("ERR"));
    oss << " tension_left="    << (tvalid ? std::to_string(tl) : std::string("ERR"));
    oss << " tension_right="   << (tvalid ? std::to_string(tr) : std::string("ERR"));
    oss << " tension_valid="   << (tvalid ? 1 : 0);
    oss << " up_left="         << (hold_up_left.load()    ? 1 : 0);
    oss << " up_right="        << (hold_up_right.load()   ? 1 : 0);
    oss << " down_left="       << (hold_down_left.load()  ? 1 : 0);
    oss << " down_right="      << (hold_down_right.load() ? 1 : 0);
    oss << " up_stop_total_kg="<< g_up_stop_total_kg.load();
    oss << " tension_max_kg="  << g_tension_max_kg.load();
    oss << " tension_diff_max_kg=" << g_tension_diff_max_kg.load();
    oss << " retract_tension_stop_kg=" << g_retract_tension_stop_kg.load();
    oss << " dsz_left_scale="  << g_dsz_left_scale.load();
    oss << " dsz_right_scale=" << g_dsz_right_scale.load();
    // Device-side meter scale (cached from SD76 SCAL/DP EEPROM). length_* is
    // already SD76's calibrated cm; this field is for GUI display + diagnostics.
    oss << " meter_left_scale="   << (g_meter_left_scale_valid  .load() ? std::to_string(g_meter_left_device_scale  .load()) : std::string("ERR"));
    oss << " meter_right_scale="  << (g_meter_right_scale_valid .load() ? std::to_string(g_meter_right_device_scale .load()) : std::string("ERR"));
    oss << " meter_middle_scale=" << (g_meter_middle_scale_valid.load() ? std::to_string(g_meter_middle_device_scale.load()) : std::string("ERR"));
    oss << " home_ground_cm="  << home_ground_cm.load();
    oss << " hold_hz="         << g_se3_hold_hz.load();
    oss << " motion_hz="       << g_se3_motion_hz.load();
    oss << " middle_hz="       << g_middle_winch_hz.load();
    oss << " balance_enabled=" << (g_balance_enabled.load() ? 1 : 0);
    oss << " balance_kp="       << g_balance_kp.load();
    oss << " balance_cap_ratio=" << g_balance_trim_cap_ratio.load();
    oss << " balance_deadband=" << g_balance_deadband.load();
    oss << " balance_hz_min="          << g_balance_hz_min.load();
    oss << " balance_hz_max_offset="   << g_balance_hz_max_offset.load();
    oss << " fine_adjust_hz="   << g_fine_adjust_hz.load();
    oss << " freeze_hz="        << g_freeze_hz.load();
    oss << " kick_hz="          << g_kick_hz.load();
    // Device availability — GUI uses these to grey out unavailable controls.
    oss << " dev_se3_left="    << (g_dev_se3_left.load()    ? 1 : 0);
    oss << " dev_se3_right="   << (g_dev_se3_right.load()   ? 1 : 0);
    oss << " dev_meter_left="  << (g_dev_meter_left.load()  ? 1 : 0);
    oss << " dev_meter_right=" << (g_dev_meter_right.load() ? 1 : 0);
    oss << " dev_meter_middle="<< (g_dev_meter_middle.load()? 1 : 0);
    oss << " dev_clv900="      << (g_dev_clv900.load()      ? 1 : 0);
    oss << " dev_dsz_left="    << (g_dev_dsz_left.load()    ? 1 : 0);
    oss << " dev_dsz_right="   << (g_dev_dsz_right.load()   ? 1 : 0);
    oss << " dev_gw_a="        << (g_gw_a_ok.load()         ? 1 : 0);
    oss << " dev_gw_b="        << (g_gw_b_ok.load()         ? 1 : 0);
    oss << " dev_gw_m="        << (g_gw_m_ok.load()         ? 1 : 0);
    oss << " dev_gw_c="        << (g_gw_c_ok.load()         ? 1 : 0);
    oss << " dev_gw_d="        << (g_gw_d_ok.load()         ? 1 : 0);
    // GUI busy/init UI uses these two flags. motion_active reflects
    // pay_out / retract / align_lengths / zero_meters_with_motion AND any
    // active hold button (see line ~2402 motion_active.store(any_hold_active())).
    oss << " init_done="       << (g_init_done.load()       ? 1 : 0);
    oss << " motion_active="   << (motion_active.load()     ? 1 : 0);
    oss << "\n";
    return oss.str();
}

static std::string cmd_tension() {
    double tl = 0.0, tr = 0.0;
    const bool tlok = !dsz_left .get_tension_kg(tl);
    const bool trok = !dsz_right.get_tension_kg(tr);

    std::ostringstream oss;
    oss << "OK";
    oss << " left="  << (tlok ? std::to_string(tl) : std::string("ERR"));
    oss << " right=" << (trok ? std::to_string(tr) : std::string("ERR"));
    oss << "\n";
    return oss.str();
}

// Apply current hold flags to one SE3 inverter side. Up takes precedence over
// Down if both flags are set (shouldn't happen via cmd_hold but defensive).
//   up=true  → retract (motor reverse)
//   down=true → pay_out (motor forward)
//   neither  → stopDecel
// Returns true on inverter command failure.
static bool apply_hold_one_side(SE3_inverter& inv, bool up, bool down,
                                const char* side_tag = "?") {
    const auto t0 = std::chrono::steady_clock::now();
    bool err;
    const char* action;
    if (up)        { action = "retract"; err = se3StartRopeHold(inv, /*pay_out=*/false); }
    else if (down) { action = "pay_out"; err = se3StartRopeHold(inv, /*pay_out=*/true);  }
    else           { action = "stop";    err = reliable_stop_one(inv);                   }
    const auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    std::cout << "[apply_hold] " << side_tag << " action=" << action
              << " dispatch=" << dur << "ms err=" << err << "\n";
    return err;
}

// Press-and-hold rope control with tension safety.
//   dir   : "up" / "down" / "up_left" / "up_right" / "down_left" / "down_right"
//   onoff : "on" / "off"
// "up"/"down" are combined shortcuts (both ropes simultaneously).
// Setting "up_left on" while "down_left" was on auto-clears down_left first
// (motor can't go both directions).
// Menu-25-style atomic dual SE3 hold start. Sets freq on BOTH first (no
// motor motion yet), then runs in the requested direction on both. If any
// step fails, rolls back internally (stopDecel both) and returns true.
//
// Both setFreqHz pair AND run pair AND rollback stopDecel pair use
// dual_se3_concurrent so the two sides start / stop in lockstep even when
// driver retry latency lands asymmetrically. Sequential dispatch was visibly
// asymmetric (one side could lag 0.5-2s if it hit a watchdog reclaim).
//
// Why this pattern (vs sequential setFreqHz+run per side):
//   1. Tighter sync window — both motors start within ~10ms of each other
//      instead of staggered by setFreqHz round-trip latency
//   2. If any setFreqHz fails, no motor has started yet, no rollback needed
//   3. Mirrors the proven Linux_test menu 25 implementation that handled the
//      "left only retract / right only pay_out" intermittent observed when
//      SE3 Modbus comms are flaky (CU-mode write fail, stale-buffer reply)
static bool dual_se3_hold_start(bool pay_out) {
    const double hz = g_se3_hold_hz.load();
    HOLD_TRACE("dual_se3_hold_start ENTRY pay_out=" << pay_out << " hz=" << hz);

    // Switched to dual_se3_sync_start (2026-05-14): old reliable_start_one
    // bundled setFreqHz + run + retry, so a retry on one side could let the
    // other side run alone for 100-800ms before catching up — audibly /
    // visibly asymmetric. sync_start splits into Phase A (setFreq both, full
    // retry) then Phase B (run both, near-simultaneous, bounded retry) → drift
    // 10-100ms. Same direction on both sides for hold (pay_out, pay_out).
    HOLD_TRACE("dual_se3_hold_start -> dual_se3_sync_start");
    if (dual_se3_sync_start(hz, pay_out, pay_out)) {
        // sync_start already rolled back internally on Phase B failure
        HOLD_TRACE("dual_se3_hold_start EXIT (sync_start fail)");
        return true;
    }
    HOLD_TRACE("dual_se3_hold_start sync_start OK -> 200ms settle + verification");

    // Post-start verification (2026-05-14): SE3 firmware has a known silent-
    // reject pattern where H1001 (run cmd) is ACK'd at protocol level but
    // firmware ignores it (CU-mode latch / fault state) — driver returns
    // success but motor never engages. Without this check, one side runs solo
    // and server thinks both are happy → asymmetric rope = robot tilt.
    // Read status word b0 (running) on BOTH sides; if either is not running,
    // abort and stop both.
    //
    // Robust read: up to 4 attempts × 50ms backoff per side. bench observed
    // single retry (14p) wasn't enough — cli_A jitter bursts last 50-100ms and
    // sometimes both first attempts within 1ms apart fail. 4 attempts over
    // ~150ms covers the longest jitter pattern seen on bench. If after 4 reads
    // we still can't get status, the comm channel is genuinely down → abort
    // is correct.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto robust_read_status = [](SE3_inverter& inv, uint16_t& out, int& attempts_used) -> bool {
        constexpr int MAX_ATTEMPTS = 4;
        for (int i = 0; i < MAX_ATTEMPTS; ++i) {
            attempts_used = i + 1;
            if (!inv.readStatusWord(out)) return false;  // success
            if (i < MAX_ATTEMPTS - 1) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return true;  // all attempts failed
    };
    uint16_t stL = 0, stR = 0;
    int l_attempts = 0, r_attempts = 0;
    const bool l_read_err = robust_read_status(se3_left,  stL, l_attempts);
    const bool r_read_err = robust_read_status(se3_right, stR, r_attempts);
    const bool l_read_ok = !l_read_err;
    const bool r_read_ok = !r_read_err;
    if (l_attempts > 1 || r_attempts > 1) {
        std::cout << "[dual_se3_hold_start] verification needed retry L=" << l_attempts
                  << "/" << 4 << " R=" << r_attempts << "/" << 4 << " attempts\n";
    }
    const bool l_running = l_read_ok && ((stL & 0x0001) != 0);   // b0
    const bool r_running = r_read_ok && ((stR & 0x0001) != 0);
    if (!l_running || !r_running) {
        HOLD_TRACE("dual_se3_hold_start verification FAIL l_run=" << l_running
                   << " r_run=" << r_running << " -> EMERGENCY stop both");
        std::cout << "[dual_se3_hold_start] asymmetric run state — "
                  << "L: " << (l_read_ok ? "" : "READ_FAIL ") << "running=" << l_running
                  << " status=0x" << std::hex << stL << std::dec
                  << " | R: " << (r_read_ok ? "" : "READ_FAIL ") << "running=" << r_running
                  << " status=0x" << std::hex << stR << std::dec
                  << " — EMERGENCY stop both for safety\n";
        // Fast abort — see dual_se3_sync_start comment. emergencyStop (MRS
        // output cutoff) instead of decel ramp; clear MRS for next run cmd.
        dual_se3_concurrent([](SE3_inverter& inv){ return inv.emergencyStop(); });
        dual_se3_concurrent([](SE3_inverter& inv){ return inv.stopDecel();     });
        HOLD_TRACE("dual_se3_hold_start EXIT (verification fail, both stopped)");
        return true;
    }
    HOLD_TRACE("dual_se3_hold_start EXIT OK both running");
    return false;
}

static std::string cmd_hold(const std::string& dir, const std::string& onoff) {
    const auto t_entry = std::chrono::steady_clock::now();
    const uint64_t t_entry_ms = now_ms();
    std::cout << "[cmd_hold] ENTRY dir=" << dir << " onoff=" << onoff
              << " t=" << t_entry_ms << "\n";

    bool on;
    if      (onoff == "on")  on = true;
    else if (onoff == "off") on = false;
    else {
        std::cout << "[cmd_hold] EXIT dir=" << dir << " result=ERR_BAD_ARG total=0ms\n";
        return "ERR expected_on_or_off\n";
    }

    // Determine which sides this cmd affects (used for both device check and
    // post-failure rollback so the two stay consistent).
    const bool need_left  = (dir == "up_left"  || dir == "down_left"  || dir == "up" || dir == "down");
    const bool need_right = (dir == "up_right" || dir == "down_right" || dir == "up" || dir == "down");

    // Per-side SE3 availability for "on" requests. "off" is always allowed
    // (releasing flags / sending stopDecel is safe even if device is gone).
    if (on) {
        if (need_left  && !g_dev_se3_left.load()) {
            std::cout << "[cmd_hold] EXIT dir=" << dir << " result=ERR_L_UNAVAIL\n";
            return "ERR se3_left_unavailable\n";
        }
        if (need_right && !g_dev_se3_right.load()) {
            std::cout << "[cmd_hold] EXIT dir=" << dir << " result=ERR_R_UNAVAIL\n";
            return "ERR se3_right_unavailable\n";
        }
    }

    bool err = false;

    // ESCALATE on stop fail (2026-05-14): when up_state and down_state are
    // both false, this is a stop attempt. If apply_hold_one_side returns
    // error, the motor MIGHT still be running. Don't return ERR with motor
    // running — escalate to emergencyStop (MRS cutoff). Last line of defense
    // before SE3 firmware OPT self-trip.
    auto set_left = [&](bool up_state, bool down_state) {
        HOLD_TRACE("set_left up=" << up_state << " down=" << down_state
                   << " -> apply_hold_one_side");
        hold_up_left.store(up_state);
        hold_down_left.store(down_state);
        const bool side_err = apply_hold_one_side(se3_left, up_state, down_state, "L");
        HOLD_TRACE("set_left done err=" << side_err);
        if (side_err) {
            if (!up_state && !down_state) {
                HOLD_TRACE("set_left STOP failed -> ESCALATE emergencyStop(L)");
                std::cout << "[cmd_hold] L stop failed — ESCALATE to emergencyStop\n";
                se3_left.emergencyStop();
                se3_left.stopDecel();
                HOLD_TRACE("set_left ESCALATE done");
            }
            err = true;
        }
    };
    auto set_right = [&](bool up_state, bool down_state) {
        HOLD_TRACE("set_right up=" << up_state << " down=" << down_state
                   << " -> apply_hold_one_side");
        hold_up_right.store(up_state);
        hold_down_right.store(down_state);
        const bool side_err = apply_hold_one_side(se3_right, up_state, down_state, "R");
        HOLD_TRACE("set_right done err=" << side_err);
        if (side_err) {
            if (!up_state && !down_state) {
                HOLD_TRACE("set_right STOP failed -> ESCALATE emergencyStop(R)");
                std::cout << "[cmd_hold] R stop failed — ESCALATE to emergencyStop\n";
                se3_right.emergencyStop();
                se3_right.stopDecel();
                HOLD_TRACE("set_right ESCALATE done");
            }
            err = true;
        }
    };

    HOLD_TRACE("cmd_hold branch=" << dir << " on=" << on
               << " need_L=" << need_left << " need_R=" << need_right);

    if (dir == "up_left") {
        // ON: up_left=true, down_left auto-false (motor can't be both)
        // OFF: up_left=false, down_left preserved
        set_left(on, on ? false : hold_down_left.load());
    } else if (dir == "up_right") {
        set_right(on, on ? false : hold_down_right.load());
    } else if (dir == "down_left") {
        set_left(on ? false : hold_up_left.load(), on);
    } else if (dir == "down_right") {
        set_right(on ? false : hold_up_right.load(), on);
    } else if (dir == "up" || dir == "down") {
        const bool pay_out = (dir == "down");
        if (on) {
            // Menu-25 atomic: freq both → run both, internal rollback on any fail
            HOLD_TRACE("combined ON pay_out=" << pay_out << " -> dual_se3_hold_start");
            const bool dhs_err = dual_se3_hold_start(pay_out);
            HOLD_TRACE("combined ON dual_se3_hold_start return err=" << dhs_err);
            if (dhs_err) {
                err = true;
            } else {
                if (pay_out) {
                    hold_down_left .store(true);
                    hold_down_right.store(true);
                    hold_up_left   .store(false);
                    hold_up_right  .store(false);
                } else {
                    hold_up_left   .store(true);
                    hold_up_right  .store(true);
                    hold_down_left .store(false);
                    hold_down_right.store(false);
                }
            }
        } else {
            // OFF: both sides retry stopDecel in lockstep via attempt-synchronized
            // retry — succeeded side re-issues no-op stopDecel while other side
            // catches up, bounds drift to ~150ms (vs 300-700ms for the old
            // independent reliable_stop_one threads). Flags clear regardless of
            // stop result.
            HOLD_TRACE("combined OFF -> dual_se3_sync_retry(stopDecel)");
            const bool stop_err = dual_se3_sync_retry(
                [](SE3_inverter& inv){ return inv.stopDecel(); },
                8, 100, "hold_off");
            HOLD_TRACE("combined OFF sync_retry return err=" << stop_err);
            if (stop_err) {
                err = true;
                // ESCALATE (2026-05-14): sync_retry already did 8x retries on
                // both sides — if it still failed, cli is genuinely sick. User
                // already released the button so we MUST stop motor now,
                // otherwise rope keeps running until SE3 firmware OPT 07-10
                // self-trip (~2-4s, can release 60-120cm of rope at 30Hz).
                // emergencyStop (MRS output cutoff) bypasses decel ramp and
                // is faster to ACK than stopDecel because it's a single
                // priority write — better chance of getting through the
                // jitter that broke stopDecel.
                HOLD_TRACE("combined OFF ESCALATE -> emergencyStop both");
                std::cout << "[cmd_hold] OFF sync_retry stopDecel failed — "
                             "ESCALATE to emergencyStop\n";
                dual_se3_concurrent([](SE3_inverter& inv){ return inv.emergencyStop(); });
                dual_se3_concurrent([](SE3_inverter& inv){ return inv.stopDecel();     });   // clear MRS
                HOLD_TRACE("combined OFF ESCALATE done");
            }
            if (pay_out) {
                hold_down_left .store(false);
                hold_down_right.store(false);
            } else {
                hold_up_left .store(false);
                hold_up_right.store(false);
            }
        }
    } else {
        std::cout << "[cmd_hold] EXIT dir=" << dir << " result=ERR_UNKNOWN_DIR\n";
        return "ERR unknown_hold_dir\n";
    }

    motion_active.store(any_hold_active());

    auto exit_log = [&](const char* result) {
        const auto total = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t_entry).count();
        std::cout << "[cmd_hold] EXIT dir=" << dir << " onoff=" << onoff
                  << " result=" << result << " total=" << total << "ms"
                  << " hold_flags=[uL=" << hold_up_left.load() << " uR=" << hold_up_right.load()
                  << " dL=" << hold_down_left.load() << " dR=" << hold_down_right.load() << "]\n";
    };

    // Atomic rollback for ON failures (single-side or combined). Without this,
    // "up on" partial failure leaves one rope moving solo → robot tilts. The
    // dual_se3_hold_start helper already rolls back on combined-cmd failure;
    // this catches per-side (up_left etc.) failures too.
    if (err && on) {
        if (need_left) {
            reliable_stop_one(se3_left);
            hold_up_left  .store(false);
            hold_down_left.store(false);
        }
        if (need_right) {
            reliable_stop_one(se3_right);
            hold_up_right  .store(false);
            hold_down_right.store(false);
        }
        motion_active.store(any_hold_active());
        exit_log("ERR_FAIL_ROLLBACK");
        return "ERR se3_cmd_fail_rollback\n";
    }

    exit_log(err ? "ERR_CMD_FAIL" : "OK");
    return err ? "ERR se3_cmd_fail\n" : "OK\n";
}

static std::string cmd_set_up_stop_total_kg(double kg) {
    if (kg <= 0 || kg > 500) return "ERR threshold_out_of_range\n";
    g_up_stop_total_kg.store(kg);
    std::cout << "[crane] up_stop_total_kg = " << kg << "\n";
    return "OK\n";
}

static std::string cmd_set_tension_max_kg(double kg) {
    if (kg <= 0 || kg > 500) return "ERR threshold_out_of_range\n";
    g_tension_max_kg.store(kg);
    std::cout << "[crane] tension_max_kg = " << kg << "\n";
    return "OK\n";
}

static std::string cmd_set_tension_diff_max_kg(double kg) {
    if (kg <= 0 || kg > 500) return "ERR threshold_out_of_range\n";
    g_tension_diff_max_kg.store(kg);
    std::cout << "[crane] tension_diff_max_kg = " << kg << "\n";
    return "OK\n";
}

static std::string cmd_set_retract_tension_stop_kg(double kg) {
    if (kg <= 0 || kg > 500) return "ERR threshold_out_of_range\n";
    g_retract_tension_stop_kg.store(kg);
    std::cout << "[crane] retract_tension_stop_kg = " << kg << "\n";
    return "OK\n";
}

// Per-side DSZL raw → kg scale tuning. Negative values are valid (and expected
// per bench wiring observation). Zero is rejected (would always read 0 kg).
// Applies immediately to the live driver instance so next get_tension_kg uses
// the new scale; also stored in atomic so cmd_status reports current value
// (and survives crane process restart only if persisted somewhere — currently
// not persisted, GUI must re-send after restart unless default suffices).
static std::string cmd_set_dsz_scale(const std::string& side, double scale) {
    if (scale == 0.0) return "ERR scale_zero\n";
    if (side == "left") {
        g_dsz_left_scale.store(scale);
        if (g_dev_dsz_left.load()) dsz_left.setScale(scale);
        std::cout << "[crane] dsz_left_scale = " << scale << "\n";
        return "OK\n";
    }
    if (side == "right") {
        g_dsz_right_scale.store(scale);
        if (g_dev_dsz_right.load()) dsz_right.setScale(scale);
        std::cout << "[crane] dsz_right_scale = " << scale << "\n";
        return "OK\n";
    }
    return "ERR expected_left_or_right\n";
}

// ============ Meter calibration (device-side via SD76 SCAL/DP EEPROM) ============

// Resolve side string to (driver, displayed-cm cache, baseline atomic,
// device-scale cache, scale-valid flag, dev flag).
// Returns false (no error) on success; true on unknown side.
static bool resolve_meter_side(const std::string& side,
                                SD76_length_meters*&   meter_out,
                                std::atomic<int32_t>*& length_out,
                                std::atomic<bool>*&    length_valid_out,
                                std::atomic<int32_t>*& baseline_out,
                                std::atomic<double>*&  dev_scale_out,
                                std::atomic<bool>*&    dev_scale_valid_out,
                                std::atomic<bool>*&    dev_out) {
    if (side == "left") {
        meter_out           = &meter_left;
        length_out          = &g_length_left;
        length_valid_out    = &g_length_left_valid;
        baseline_out        = &g_meter_left_cal_baseline;
        dev_scale_out       = &g_meter_left_device_scale;
        dev_scale_valid_out = &g_meter_left_scale_valid;
        dev_out             = &g_dev_meter_left;
    } else if (side == "right") {
        meter_out           = &meter_right;
        length_out          = &g_length_right;
        length_valid_out    = &g_length_right_valid;
        baseline_out        = &g_meter_right_cal_baseline;
        dev_scale_out       = &g_meter_right_device_scale;
        dev_scale_valid_out = &g_meter_right_scale_valid;
        dev_out             = &g_dev_meter_right;
    } else if (side == "middle") {
        meter_out           = &meter_middle;
        length_out          = &g_length_middle;
        length_valid_out    = &g_length_middle_valid;
        baseline_out        = &g_meter_middle_cal_baseline;
        dev_scale_out       = &g_meter_middle_device_scale;
        dev_scale_valid_out = &g_meter_middle_scale_valid;
        dev_out             = &g_dev_meter_middle;
    } else {
        return true;
    }
    return false;
}

// cal_zero <side>: snapshot current SD76 displayed cm as calibration baseline.
// User then physically pulls the rope a known distance and calls cal_set.
static std::string cmd_cal_zero(const std::string& side) {
    SD76_length_meters* m; std::atomic<int32_t> *len, *base; std::atomic<bool> *lv, *sv, *dev;
    std::atomic<double>* sc;
    if (resolve_meter_side(side, m, len, lv, base, sc, sv, dev))
        return "ERR expected_left_right_or_middle\n";
    if (!dev->load())   return "ERR meter_unavailable\n";
    if (!lv->load())    return "ERR meter_read_invalid\n";

    const int32_t v = len->load();   // displayed cm (post-SCAL)
    base->store(v);
    std::cout << "[crane] cal_zero " << side << " baseline_cm = " << v << "\n";
    std::ostringstream oss;
    oss << "OK baseline_cm=" << v << "\n";
    return oss.str();
}

// cal_set <side> <actual_cm>: ratio = actual_cm / (now - baseline);
// driver: device SCAL ×= ratio (in EEPROM, persists across power-cycle).
// Refresh atomic cache so cmd_status reflects the new effective scale.
static std::string cmd_cal_set(const std::string& side, double actual_cm) {
    SD76_length_meters* m; std::atomic<int32_t> *len, *base; std::atomic<bool> *lv, *sv, *dev;
    std::atomic<double>* sc;
    if (resolve_meter_side(side, m, len, lv, base, sc, sv, dev))
        return "ERR expected_left_right_or_middle\n";
    if (!dev->load())              return "ERR meter_unavailable\n";
    if (!lv->load())               return "ERR meter_read_invalid\n";
    if (actual_cm == 0.0)          return "ERR actual_cm_zero\n";

    const int32_t baseline_cm = base->load();
    if (baseline_cm == METER_CAL_UNSET) return "ERR cal_zero_first\n";

    const int32_t now_cm = len->load();
    const int32_t observed_delta = now_cm - baseline_cm;
    if (observed_delta == 0)       return "ERR no_movement_detected\n";

    const double ratio = actual_cm / (double)observed_delta;
    // Reject sign flips: a negative ratio means user pulled in the opposite
    // direction or swapped rope/encoder phase. Recover via panel, not here.
    if (ratio <= 0.0) return "ERR negative_ratio_check_direction\n";

    if (m->scaleByRatio(ratio)) {
        return "ERR sd76_scale_write_failed\n";
    }
    // Refresh cache from device — also implicit verification that write stuck.
    if (refresh_meter_scale_cache(*m, *sc, *sv)) {
        std::cout << "[WARN] cal_set " << side
                  << ": scale written but readback failed\n";
    }
    // Invalidate baseline — forcing the user to call cal_zero again before the
    // next cal_set (avoids accidentally re-applying ratio against stale baseline).
    base->store(METER_CAL_UNSET);

    std::cout << "[crane] cal_set " << side
              << " actual=" << actual_cm << "cm observed=" << observed_delta
              << " → ratio=" << ratio << " new_device_scale=" << sc->load() << "\n";
    std::ostringstream oss;
    oss << "OK ratio=" << ratio
        << " observed_cm=" << observed_delta
        << " new_scale=" << sc->load() << "\n";
    return oss.str();
}

// set_meter_scale <side> <scale>: direct override (advanced / GUI manual entry).
// Driver writes effective multiplier directly into SD76 SCAL (uses device's
// current DP). Bypasses cal_zero/cal_set flow.
static std::string cmd_set_meter_scale(const std::string& side, double scale) {
    SD76_length_meters* m; std::atomic<int32_t> *len, *base; std::atomic<bool> *lv, *sv, *dev;
    std::atomic<double>* sc;
    if (resolve_meter_side(side, m, len, lv, base, sc, sv, dev))
        return "ERR expected_left_right_or_middle\n";
    if (!dev->load())   return "ERR meter_unavailable\n";
    if (scale <= 0.0)   return "ERR scale_must_be_positive\n";

    if (m->setEffectiveScale(scale))
        return "ERR sd76_scale_write_failed_check_dp_or_range\n";
    if (refresh_meter_scale_cache(*m, *sc, *sv)) {
        std::cout << "[WARN] set_meter_scale " << side
                  << ": scale written but readback failed\n";
    }
    std::cout << "[crane] set_meter_scale " << side << " = " << scale
              << " (device scale now " << sc->load() << ")\n";
    return "OK\n";
}

// read_meter_scale <side>: force re-read of SCAL/DP from SD76 EEPROM and
// refresh atomic cache. Returns effective M (application multiplier) plus raw
// SCAL + DP — the raw values are diagnostic for SD76 mode-latch issues
// (DP write rejected in 通訊模式; see project_sd76_panel_mode_latch.md).
static std::string cmd_read_meter_scale(const std::string& side) {
    SD76_length_meters* m; std::atomic<int32_t> *len, *base; std::atomic<bool> *lv, *sv, *dev;
    std::atomic<double>* sc;
    if (resolve_meter_side(side, m, len, lv, base, sc, sv, dev))
        return "ERR expected_left_right_or_middle\n";
    if (!dev->load()) return "ERR meter_unavailable\n";

    uint32_t raw_scal = 0;
    uint8_t  raw_dp = 0;
    if (m->readScale(raw_scal, raw_dp))
        return "ERR sd76_scale_read_failed\n";
    // Refresh cache too so cmd_status sees fresh value (uses readScale internally).
    if (refresh_meter_scale_cache(*m, *sc, *sv))
        return "ERR sd76_scale_read_failed\n";

    std::ostringstream oss;
    oss << "OK scale=" << sc->load()
        << " raw_scal=" << raw_scal
        << " raw_dp="   << (unsigned)raw_dp << "\n";
    return oss.str();
}

// Runtime frequency adjust. Range checked against driver upper bound; takes
// effect on NEXT motor-start command (not currently-running motors). Caller
// should send a fresh hold/motion cmd after change to apply at the inverter.
static std::string cmd_set_hold_hz(double hz) {
    if (hz <= 0 || hz > SE3_MAX_HZ) return "ERR hz_out_of_range\n";
    g_se3_hold_hz.store(hz);
    std::cout << "[crane] se3_hold_hz = " << hz << "\n";
    return "OK\n";
}
static std::string cmd_set_motion_hz(double hz) {
    if (hz <= 0 || hz > SE3_MAX_HZ) return "ERR hz_out_of_range\n";
    g_se3_motion_hz.store(hz);
    std::cout << "[crane] se3_motion_hz = " << hz << "\n";
    return "OK\n";
}
static std::string cmd_set_middle_hz(double hz) {
    if (hz <= 0 || hz > CLV900_MAX_HZ) return "ERR hz_out_of_range\n";
    g_middle_winch_hz.store(hz);
    std::cout << "[crane] middle_winch_hz = " << hz << "\n";
    return "OK\n";
}

// Runtime balance tuning. Effective on next BALANCE_TICK_MS (1000ms-ish later).
// No persistence — values reset to *_DEFAULT on restart. To make permanent, edit
// BALANCE_*_DEFAULT constants at top of file.
static std::string cmd_set_balance_enabled(const std::string& onoff) {
    if      (onoff == "on" || onoff == "1")  g_balance_enabled.store(true);
    else if (onoff == "off"|| onoff == "0")  g_balance_enabled.store(false);
    else return "ERR expected_on_or_off\n";
    std::cout << "[crane] balance_enabled = " << (g_balance_enabled.load() ? 1 : 0) << "\n";
    return "OK\n";
}
static std::string cmd_set_balance_kp(double kp) {
    if (kp < 0 || kp > 10.0) return "ERR kp_out_of_range (0..10 Hz/cm)\n";
    g_balance_kp.store(kp);
    std::cout << "[crane] balance_kp = " << kp << " Hz/cm\n";
    return "OK\n";
}
static std::string cmd_set_balance_cap(double ratio) {
    if (ratio < 0 || ratio > 2.0) return "ERR ratio_out_of_range (0..2.0)\n";
    g_balance_trim_cap_ratio.store(ratio);
    std::cout << "[crane] balance_trim_cap_ratio = " << ratio
              << " (effective at hold_hz=" << g_se3_hold_hz.load()
              << " → " << (g_se3_hold_hz.load() * ratio) << " Hz; "
              << "at motion_hz=" << g_se3_motion_hz.load()
              << " → " << (g_se3_motion_hz.load() * ratio) << " Hz)\n";
    return "OK\n";
}
static std::string cmd_set_balance_deadband(double cm) {
    if (cm < 0 || cm > 50.0) return "ERR deadband_out_of_range (0..50 cm)\n";
    g_balance_deadband.store(cm);
    std::cout << "[crane] balance_deadband = " << cm << " cm\n";
    return "OK\n";
}
static std::string cmd_set_balance_hz_min(double hz) {
    if (hz < 0 || hz > SE3_MAX_HZ) return "ERR hz_out_of_range\n";
    g_balance_hz_min.store(hz);
    std::cout << "[crane] balance_hz_min = " << hz << " Hz\n";
    return "OK\n";
}
// NOTE (2026-05-15): semantic changed — now sets OFFSET above base_hz, not
// absolute hz. effective hz_max = base_hz + offset. Default 5.
static std::string cmd_set_balance_hz_max(double offset) {
    if (offset < 0 || offset > SE3_MAX_HZ) return "ERR offset_out_of_range\n";
    g_balance_hz_max_offset.store(offset);
    std::cout << "[crane] balance_hz_max_offset = " << offset
              << " Hz (effective max = base_hz + " << offset
              << " → hold " << (g_se3_hold_hz.load() + offset)
              << "Hz / motion " << (g_se3_motion_hz.load() + offset) << "Hz)\n";
    return "OK\n";
}
static std::string cmd_set_fine_adjust_hz(double hz) {
    if (hz <= 0 || hz > SE3_MAX_HZ) return "ERR hz_out_of_range\n";
    g_fine_adjust_hz.store(hz);
    std::cout << "[crane] fine_adjust_hz = " << hz << " Hz\n";
    return "OK\n";
}
static std::string cmd_set_freeze_hz(double hz) {
    if (hz <= 0 || hz > SE3_MAX_HZ) return "ERR hz_out_of_range\n";
    g_freeze_hz.store(hz);
    std::cout << "[crane] freeze_hz = " << hz << " Hz\n";
    return "OK\n";
}
static std::string cmd_set_kick_hz(double hz) {
    if (hz <= 0 || hz > SE3_MAX_HZ) return "ERR hz_out_of_range\n";
    g_kick_hz.store(hz);
    std::cout << "[crane] kick_hz = " << hz << " Hz\n";
    return "OK\n";
}

static std::string cmd_zero_tension(const std::string& which) {
    // Zero writes only modify X518 RAM. Always follow with save_params()
    // so the zero offset persists across X518 power-cycle. Without this,
    // every fresh boot the operator would have to re-zero (and forget would
    // mean tension reads way off baseline).
    if (which == "left") {
        if (!g_dev_dsz_left.load())  return "ERR dsz_left_unavailable\n";
        if (dsz_left.do_zero_ch1())  return "ERR dsz_left_zero_fail\n";
        if (dsz_left.save_params()) {
            std::cout << "[WARN] dsz_left save_params after zero failed (zero in RAM only)\n";
        }
    } else if (which == "right") {
        if (!g_dev_dsz_right.load()) return "ERR dsz_right_unavailable\n";
        if (dsz_right.do_zero_ch1()) return "ERR dsz_right_zero_fail\n";
        if (dsz_right.save_params()) {
            std::cout << "[WARN] dsz_right save_params after zero failed (zero in RAM only)\n";
        }
    } else if (which == "all") {
        if (!g_dev_dsz_left.load())  return "ERR dsz_left_unavailable\n";
        if (!g_dev_dsz_right.load()) return "ERR dsz_right_unavailable\n";
        bool err = false;
        if (dsz_left .do_zero_ch1()) err = true;
        if (dsz_right.do_zero_ch1()) err = true;
        if (err) return "ERR dsz_zero_fail\n";
        // Save both (best-effort — if one save fails, the other's zero may
        // still persist; warn but report OK to keep operator flow simple).
        if (dsz_left .save_params())
            std::cout << "[WARN] dsz_left save_params after zero failed\n";
        if (dsz_right.save_params())
            std::cout << "[WARN] dsz_right save_params after zero failed\n";
    } else {
        return "ERR expected_left_right_or_all\n";
    }
    return "OK\n";
}

static std::string cmd_stop() {
    abort_flag = true;
    hold_all_off();   // clear any GUI hold state too
    allMotionOff();
    return "OK\n";
}

static std::string cmd_ping() {
    return "OK pong\n";
}

// On-demand SE3 fault code diagnostic. Reads H1007 + H1008 from the requested
// side and returns the 4 codes packed in the response. Kept OFF the keepalive
// hot path because per-fault automatic reads (a) doubled keepalive cycle time
// near the SE3 07-09=2s OPT threshold causing cascading faults, and (b)
// 0x1007/0x1008 register addresses are unverified — first bench run should
// confirm via this raw command before any reliance.
// Usage:  se3_fault left   or   se3_fault right
static std::string cmd_se3_fault(const std::string& side) {
    SE3_inverter* inv = nullptr;
    bool          available = false;
    if (side == "left")  { inv = &se3_left;  available = g_dev_se3_left .load(); }
    if (side == "right") { inv = &se3_right; available = g_dev_se3_right.load(); }
    if (!inv)       return "ERR usage:se3_fault_<left|right>\n";
    if (!available) return std::string("ERR se3_") + side + "_unavailable\n";

    uint8_t f1 = 0, f2 = 0, f3 = 0, f4 = 0;
    if (inv->readFaultCode(f1, f2, f3, f4)) {
        return std::string("ERR read_fail side=") + side + "\n";
    }
    char buf[160];
    std::snprintf(buf, sizeof(buf),
        "OK side=%s f1=%u/%s f2=%u/%s f3=%u/%s f4=%u/%s\n",
        side.c_str(),
        f1, se3_fault_name(f1),
        f2, se3_fault_name(f2),
        f3, se3_fault_name(f3),
        f4, se3_fault_name(f4));
    return std::string(buf);
}

// ============ Dispatcher ============

static std::string dispatch(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    // Log every cmd arrival except high-frequency polling (status/ping every 200ms+
    // would drown the log). Lets you trace "GUI clicked → cmd reached crane" timing.
    if (cmd != "status" && cmd != "ping" && cmd != "tension" && cmd != "home_status") {
        std::cout << "[dispatch] cmd='" << line << "' t=" << now_ms() << "\n";
    }

    if (cmd == "pay_out") {
        int cm = 0; iss >> cm;
        if (iss.fail()) return "ERR usage:pay_out_<cm>\n";
        return motion_rope(cm, false);
    }
    if (cmd == "retract") {
        int cm = 0; iss >> cm;
        if (iss.fail()) return "ERR usage:retract_<cm>\n";
        return motion_rope(cm, true);
    }
    if (cmd == "pay_out_left" || cmd == "pay_out_right" ||
        cmd == "retract_left" || cmd == "retract_right") {
        std::string onoff; iss >> onoff;
        if (iss.fail()) return "ERR usage:<cmd>_<on|off>\n";
        return cmd_manual(cmd, onoff);
    }
    if (cmd == "middle_set") {
        int rpm = 0; std::string dir;
        iss >> rpm >> dir;
        if (dir.empty()) return "ERR usage:middle_set_<rpm>_<pay|retract|stop>\n";
        return cmd_middle_set(rpm, dir);
    }
    if (cmd == "zero_meters") {
        std::string mode; iss >> mode;
        if (mode.empty()) return "ERR usage:zero_meters_<ground|top>\n";
        return cmd_zero_meters(mode);
    }
    if (cmd == "zero_meter") {
        std::string which; iss >> which;
        if (which.empty()) return "ERR usage:zero_meter_<left|right|middle>\n";
        return cmd_zero_meter(which);
    }
    if (cmd == "home_status") return cmd_home_status();
    if (cmd == "roll_correct") {
        int delta_cm = 0; iss >> delta_cm;
        if (iss.fail()) return "ERR usage:roll_correct_<delta_cm>\n";
        return cmd_roll_correct(delta_cm);
    }
    if (cmd == "align_lengths") return cmd_align_lengths();
    if (cmd == "tension") return cmd_tension();
    if (cmd == "zero_tension") {
        std::string which; iss >> which;
        if (which.empty()) return "ERR usage:zero_tension_<left|right|all>\n";
        return cmd_zero_tension(which);
    }
    if (cmd == "up" || cmd == "down" ||
        cmd == "up_left" || cmd == "up_right" ||
        cmd == "down_left" || cmd == "down_right") {
        std::string onoff; iss >> onoff;
        if (iss.fail()) return "ERR usage:<cmd>_<on|off>\n";
        HOLD_TRACE("dispatch -> cmd_hold dir=" << cmd << " onoff=" << onoff);
        return cmd_hold(cmd, onoff);
    }
    if (cmd == "set_up_stop_total_kg") {
        double kg = 0; iss >> kg;
        if (iss.fail()) return "ERR usage:set_up_stop_total_kg_<kg>\n";
        return cmd_set_up_stop_total_kg(kg);
    }
    if (cmd == "set_tension_max_kg") {
        double kg = 0; iss >> kg;
        if (iss.fail()) return "ERR usage:set_tension_max_kg_<kg>\n";
        return cmd_set_tension_max_kg(kg);
    }
    if (cmd == "set_tension_diff_max_kg") {
        double kg = 0; iss >> kg;
        if (iss.fail()) return "ERR usage:set_tension_diff_max_kg_<kg>\n";
        return cmd_set_tension_diff_max_kg(kg);
    }
    if (cmd == "set_retract_tension_stop_kg") {
        double kg = 0; iss >> kg;
        if (iss.fail()) return "ERR usage:set_retract_tension_stop_kg_<kg>\n";
        return cmd_set_retract_tension_stop_kg(kg);
    }
    if (cmd == "set_dsz_scale") {
        std::string side; double scale = 0;
        iss >> side >> scale;
        if (side.empty() || iss.fail())
            return "ERR usage:set_dsz_scale_<left|right>_<value>\n";
        return cmd_set_dsz_scale(side, scale);
    }
    if (cmd == "cal_zero") {
        std::string side; iss >> side;
        if (side.empty()) return "ERR usage:cal_zero_<left|right|middle>\n";
        return cmd_cal_zero(side);
    }
    if (cmd == "cal_set") {
        std::string side; double actual_cm = 0;
        iss >> side >> actual_cm;
        if (side.empty() || iss.fail())
            return "ERR usage:cal_set_<left|right|middle>_<actual_cm>\n";
        return cmd_cal_set(side, actual_cm);
    }
    if (cmd == "set_meter_scale") {
        std::string side; double scale = 0;
        iss >> side >> scale;
        if (side.empty() || iss.fail())
            return "ERR usage:set_meter_scale_<left|right|middle>_<value>\n";
        return cmd_set_meter_scale(side, scale);
    }
    if (cmd == "read_meter_scale") {
        std::string side; iss >> side;
        if (side.empty()) return "ERR usage:read_meter_scale_<left|right|middle>\n";
        return cmd_read_meter_scale(side);
    }
    if (cmd == "set_hold_hz") {
        double hz = 0; iss >> hz;
        if (iss.fail()) return "ERR usage:set_hold_hz_<hz>\n";
        return cmd_set_hold_hz(hz);
    }
    if (cmd == "set_motion_hz") {
        double hz = 0; iss >> hz;
        if (iss.fail()) return "ERR usage:set_motion_hz_<hz>\n";
        return cmd_set_motion_hz(hz);
    }
    if (cmd == "set_middle_hz") {
        double hz = 0; iss >> hz;
        if (iss.fail()) return "ERR usage:set_middle_hz_<hz>\n";
        return cmd_set_middle_hz(hz);
    }
    if (cmd == "set_balance_enabled") {
        std::string onoff; iss >> onoff;
        if (onoff.empty()) return "ERR usage:set_balance_enabled_<on|off>\n";
        return cmd_set_balance_enabled(onoff);
    }
    if (cmd == "set_balance_kp") {
        double v = 0; iss >> v;
        if (iss.fail()) return "ERR usage:set_balance_kp_<Hz/cm>\n";
        return cmd_set_balance_kp(v);
    }
    if (cmd == "set_balance_cap") {
        double v = 0; iss >> v;
        if (iss.fail()) return "ERR usage:set_balance_cap_<ratio_0..2.0>\n";
        return cmd_set_balance_cap(v);
    }
    if (cmd == "set_balance_deadband") {
        double v = 0; iss >> v;
        if (iss.fail()) return "ERR usage:set_balance_deadband_<cm>\n";
        return cmd_set_balance_deadband(v);
    }
    if (cmd == "set_balance_hz_min") {
        double v = 0; iss >> v;
        if (iss.fail()) return "ERR usage:set_balance_hz_min_<hz>\n";
        return cmd_set_balance_hz_min(v);
    }
    if (cmd == "set_balance_hz_max") {
        double v = 0; iss >> v;
        if (iss.fail()) return "ERR usage:set_balance_hz_max_<offset_hz_above_base>\n";
        return cmd_set_balance_hz_max(v);
    }
    if (cmd == "set_fine_adjust_hz") {
        double v = 0; iss >> v;
        if (iss.fail()) return "ERR usage:set_fine_adjust_hz_<hz>\n";
        return cmd_set_fine_adjust_hz(v);
    }
    if (cmd == "set_freeze_hz") {
        double v = 0; iss >> v;
        if (iss.fail()) return "ERR usage:set_freeze_hz_<hz>\n";
        return cmd_set_freeze_hz(v);
    }
    if (cmd == "set_kick_hz") {
        double v = 0; iss >> v;
        if (iss.fail()) return "ERR usage:set_kick_hz_<hz>\n";
        return cmd_set_kick_hz(v);
    }
    if (cmd == "status") return cmd_status();
    if (cmd == "stop")   return cmd_stop();
    if (cmd == "ping")   return cmd_ping();
    if (cmd == "se3_fault") {
        std::string side; iss >> side;
        if (iss.fail()) return "ERR usage:se3_fault_<left|right>\n";
        return cmd_se3_fault(side);
    }
    // [2026-06-05] Water inlet ball valve — moved from washrobot to crane side.
    // On PQW slave 12 CH4 (cli_M, .34 shared bus with SD76 meters).
    // Uses verify-retry pattern: PQW driver's controlRelay only checks TCP
    // send success, not actual relay state. On shared bus, occasional Modbus
    // frame collision with SD76 polling can drop the relay write silently.
    // Verify via FC01 readback + retry up to 3 times catches this.
    if (cmd == "water_inlet") {
        std::string s; iss >> s;
        if (iss.fail()) return "ERR usage:water_inlet_<on|off>\n";
        bool on;
        if      (s == "on")  on = true;
        else if (s == "off") on = false;
        else return "ERR expected_on_or_off\n";
        if (!g_dev_pqw_water.load()) return "ERR pqw_water_offline\n";

        // First write attempt
        if (pqw_water.controlRelay(CH_WATER_INLET, on))
            return "ERR water_inlet_relay_fail\n";

        // Verify-retry loop (mirrors washrobot pqw_set_relay_verified_)
        // [2026-06-06] sleep 50→200ms：cli_M 跟 SD76 共用 bus + Modbus TCP
        // gateway 偶有 stale-frame buffer 現象。50ms 不夠長讓殘留 frame 走完
        // → readAllStatus 拿到舊狀態 → verify 假 fail/pass。200ms 給 SD76 polling
        // 完整週期 + buffer drain。
        for (int vr = 0; vr < 3; ++vr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            auto st = pqw_water.readAllStatus();
            if (st.empty() || (int)st.size() <= CH_WATER_INLET - 1) {
                // Can't verify (FC01 fail) — accept as best-effort
                return "OK\n";
            }
            if (st[CH_WATER_INLET - 1] == on) {
                return "OK\n";   // confirmed
            }
            std::cout << "[pqw_water] ch=" << CH_WATER_INLET
                      << " set " << (on ? "ON" : "OFF")
                      << " verify fail vr=" << vr << ", retrying\n";
            if (pqw_water.controlRelay(CH_WATER_INLET, on))
                return "ERR water_inlet_relay_fail\n";
        }
        std::cout << "[pqw_water] ch=" << CH_WATER_INLET
                  << " set " << (on ? "ON" : "OFF")
                  << " gave up verify after 3 retries\n";
        // Return OK anyway (best-effort) — same policy as washrobot pqw_set_relay_verified_.
        // Caller (washrobot) sees OK; physical state unknown.
        return "OK\n";
    }
    return "ERR unknown_cmd\n";
}

// ============ TCP receive callback (line-buffered per client thread) ============

static void on_receive(socket_t sock, const char* data, int len) {
    touch_heartbeat();

    thread_local std::string rx_buf;
    rx_buf.append(data, len);

    size_t pos;
    while ((pos = rx_buf.find('\n')) != std::string::npos) {
        std::string line = rx_buf.substr(0, pos);
        rx_buf.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        const std::string reply = dispatch(line);
        cmd_server.sendToClient(sock, reply.c_str(), (int)reply.size());
    }
}

// ============ Main ============

int main() {
#ifndef _WIN32
    // Ignore SIGPIPE so send() on a dead peer returns -1/EPIPE instead of killing the process.
    signal(SIGPIPE, SIG_IGN);
#endif

    std::cout << "[Crane_control_PI] starting..." << std::endl;

    // ---- 5 gateway connections (graceful — failed ones get [WARN] and
    //      flag = false, devices on that gateway disabled accordingly) ----
    // 2026-05-15 physical-separation layout: each SE3 has its own USR (.30 / .31),
    // SD76 meters share dedicated sensing gateway (.34), DSZL each on .32 / .33.
    if (cli_A.connectToServer(USR_A_IP, USR_PORT)) {
        g_gw_a_ok = true;
        std::cout << "[OK]   USR_A (SE3 left)  @ " << USR_A_IP << ":" << USR_PORT << std::endl;
    } else {
        std::cerr << "[WARN] USR_A " << USR_A_IP << ":" << USR_PORT
                  << " connect failed — left rope SE3 disabled" << std::endl;
    }

    if (cli_B.connectToServer(USR_B_IP, USR_PORT)) {
        g_gw_b_ok = true;
        std::cout << "[OK]   USR_B (SE3 right) @ " << USR_B_IP << ":" << USR_PORT << std::endl;
    } else {
        std::cerr << "[WARN] USR_B " << USR_B_IP << ":" << USR_PORT
                  << " connect failed — right rope SE3 disabled" << std::endl;
    }

    if (cli_M.connectToServer(USR_M_IP, USR_PORT)) {
        g_gw_m_ok = true;
        std::cout << "[OK]   USR_M (SD76 meters) @ " << USR_M_IP << ":" << USR_PORT << std::endl;
    } else {
        std::cerr << "[WARN] USR_M " << USR_M_IP << ":" << USR_PORT
                  << " connect failed — all SD76 length feedback disabled" << std::endl;
    }

    if (cli_C.connectToServer(DSZL_LEFT_IP, DSZL_PORT)) {
        g_gw_c_ok = true;
        std::cout << "[OK]   DSZL left (X518)  @ " << DSZL_LEFT_IP << ":" << DSZL_PORT << std::endl;
    } else {
        std::cerr << "[WARN] X518 left tension " << DSZL_LEFT_IP << ":" << DSZL_PORT
                  << " connect failed — left tension monitoring disabled" << std::endl;
    }

    if (cli_D.connectToServer(DSZL_RIGHT_IP, DSZL_PORT)) {
        g_gw_d_ok = true;
        std::cout << "[OK]   DSZL right (X518) @ " << DSZL_RIGHT_IP << ":" << DSZL_PORT << std::endl;
    } else {
        std::cerr << "[WARN] X518 right tension " << DSZL_RIGHT_IP << ":" << DSZL_PORT
                  << " connect failed — right tension monitoring disabled" << std::endl;
    }

    // ---- USR_A (.30) — SE3 left (+ future CLV900 middle if installed) ----
    if (g_gw_a_ok.load()) {
        if (!se3_left.init(cli_A, SE3_LEFT_SLAVE, false)) {
            g_dev_se3_left = true;
            std::cout << "[OK]   SE3 left       USR_A slave " << SE3_LEFT_SLAVE << std::endl;
        } else {
            std::cerr << "[WARN] SE3 left init failed — left rope motion disabled" << std::endl;
        }
        // CLV900 middle winch intentionally NOT initialized (2026-05-14):
        // hardware not yet installed. When re-enabled it shares cli_A with
        // SE3 left (mutex-serialized; CLV900 write rate is low so impact is small).
        std::cout << "[SKIP] CLV900         — hardware not installed (init skipped)" << std::endl;
        // if (!inverter.init(cli_A, INVERTER_SLAVE, false)) {
        //     g_dev_clv900 = true;
        // }
    } else {
        std::cerr << "[WARN] USR_A down — skipping SE3 left + CLV900 init" << std::endl;
    }

    // ---- USR_B (.31) — SE3 right ----
    if (g_gw_b_ok.load()) {
        if (!se3_right.init(cli_B, SE3_RIGHT_SLAVE, false)) {
            g_dev_se3_right = true;
            std::cout << "[OK]   SE3 right      USR_B slave " << SE3_RIGHT_SLAVE << std::endl;
        } else {
            std::cerr << "[WARN] SE3 right init failed — right rope motion disabled" << std::endl;
        }
    } else {
        std::cerr << "[WARN] USR_B down — skipping SE3 right init" << std::endl;
    }

    // ---- USR_M (.34) — SD76 meters (sensing bus) ----
    if (g_gw_m_ok.load()) {
        if (!meter_left.init(cli_M, METER_LEFT_SLAVE, false)) {
            g_dev_meter_left = true;
            // SD76 may be in paused state from previous session (e.g. Linux_test
            // menu 9 'p' command) — paused state persists in flash, won't auto-
            // count even after reset. Always resume on init so meter_loop sees
            // live counts (otherwise length stays at last paused value, motion
            // appears to "only move 2cm" for 30cm of physical rope motion).
            meter_left.resumeMeter();
            std::cout << "[OK]   SD76 left      USR_M slave " << METER_LEFT_SLAVE << " (resumed)" << std::endl;
        } else {
            std::cerr << "[WARN] SD76 left init failed — left auto-distance disabled" << std::endl;
        }
        if (!meter_right.init(cli_M, METER_RIGHT_SLAVE, false)) {
            g_dev_meter_right = true;
            meter_right.resumeMeter();
            std::cout << "[OK]   SD76 right     USR_M slave " << METER_RIGHT_SLAVE << " (resumed)" << std::endl;
        } else {
            std::cerr << "[WARN] SD76 right init failed — right auto-distance disabled" << std::endl;
        }
        // SD76 middle conduit meter intentionally NOT initialized (2026-05-14):
        // hardware not yet installed. Skipping keeps g_dev_meter_middle = false.
        // Re-enable when middle conduit is wired up by restoring the init call.
        std::cout << "[SKIP] SD76 middle    — hardware not installed (init skipped)" << std::endl;
        // if (!meter_middle.init(cli_M, METER_MIDDLE_SLAVE, false)) {
        //     g_dev_meter_middle = true;
        //     meter_middle.resumeMeter();
        // }

        // [2026-06-05] PQW water-inlet relay on same cli_M bus, slave 12.
        // External-client init mode (shares cli_M with SD76 meters).
        // [2026-06-05] debug=true 暫時打開：bench 看 firmware 收到的 ON/OFF cmd 跟
        //               readAllStatus 回報內容，排查「first ON 沒物理動」問題。
        //               穩定後改回 false 避免 stdout 太吵。
        if (!pqw_water.init(cli_M, PQW_WATER_SLAVE, PQW_WATER_TOTAL_CH, true)) {
            g_dev_pqw_water = true;
            std::cout << "[OK]   PQW water      USR_M slave " << PQW_WATER_SLAVE
                      << " CH" << CH_WATER_INLET << " (water inlet ball valve)" << std::endl;
        } else {
            std::cerr << "[WARN] PQW water init failed — water_inlet cmd will fail" << std::endl;
        }
    } else {
        std::cerr << "[WARN] USR_M down — skipping both SD76 meters init + PQW water init" << std::endl;
    }

    // ---- USR_C (.32): DSZL left ----
    if (g_gw_c_ok.load()) {
        if (!dsz_left.init(cli_C, DSZL_LEFT_SLAVE, false)) {
            g_dev_dsz_left = true;
            std::cout << "[OK]   DSZL-107 left  USR_C slave " << DSZL_LEFT_SLAVE << std::endl;
        } else {
            std::cerr << "[WARN] DSZL left init failed — left tension monitoring disabled" << std::endl;
        }
    }

    // ---- USR_D (.33): DSZL right ----
    if (g_gw_d_ok.load()) {
        if (!dsz_right.init(cli_D, DSZL_RIGHT_SLAVE, false)) {
            g_dev_dsz_right = true;
            std::cout << "[OK]   DSZL-107 right USR_D slave " << DSZL_RIGHT_SLAVE << std::endl;
        } else {
            std::cerr << "[WARN] DSZL right init failed — right tension monitoring disabled" << std::endl;
        }
    }

    // Set DSZL unit register to kg in RAM. Idempotent — X518 should already
    // have unit=kg in flash from prior bench setup (Linux_test menu 24 'u'+'S').
    //
    // We deliberately do NOT call save_params() here. save_params commits the
    // ENTIRE current RAM state to flash, which would also persist whatever
    // zero offset is currently in RAM. If for any reason the boot-time RAM
    // doesn't match the previously-saved flash zero (e.g., transient firmware
    // glitch on TCP connect), an init-time save would overwrite a good flash
    // zero with a broken RAM zero. So save only happens on explicit user
    // action (cmd_zero_tension) where we know the operator intends a fresh
    // zero baseline.
    if (g_dev_dsz_left.load()  && dsz_left.set_unit_kg())
        std::cout << "[WARN] DSZL-107 left  set_unit_kg failed (continuing)\n";
    if (g_dev_dsz_right.load() && dsz_right.set_unit_kg())
        std::cout << "[WARN] DSZL-107 right set_unit_kg failed (continuing)\n";

    // Apply scale defaults (negative — see DSZL_SCALE_DEFAULT comment) so kg
    // values come out positive when force is applied. GUI overrides via
    // set_dsz_scale at runtime. (scale is driver-local, doesn't need save_params.)
    if (g_dev_dsz_left.load())  dsz_left .setScale(g_dsz_left_scale .load());
    if (g_dev_dsz_right.load()) dsz_right.setScale(g_dsz_right_scale.load());
    std::cout << "[INFO] DSZL scale: left=" << g_dsz_left_scale.load()
              << " right=" << g_dsz_right_scale.load()
              << " (negative = force↑→raw↓ wiring)\n";

    // Read meter scale from each SD76 EEPROM and cache. Calibration lives in
    // the device, not on the Pi. cmd_cal_set / cmd_set_meter_scale write to
    // EEPROM and refresh the cache; nothing to load from disk.
    if (g_dev_meter_left.load()) {
        if (refresh_meter_scale_cache(meter_left, g_meter_left_device_scale, g_meter_left_scale_valid))
            std::cout << "[WARN] meter_left scale read failed at init\n";
        else
            std::cout << "[INFO] meter_left  device scale = "
                      << g_meter_left_device_scale.load() << "\n";
    }
    if (g_dev_meter_right.load()) {
        if (refresh_meter_scale_cache(meter_right, g_meter_right_device_scale, g_meter_right_scale_valid))
            std::cout << "[WARN] meter_right scale read failed at init\n";
        else
            std::cout << "[INFO] meter_right device scale = "
                      << g_meter_right_device_scale.load() << "\n";
    }
    if (g_dev_meter_middle.load()) {
        if (refresh_meter_scale_cache(meter_middle, g_meter_middle_device_scale, g_meter_middle_scale_valid))
            std::cout << "[WARN] meter_middle scale read failed at init\n";
        else
            std::cout << "[INFO] meter_middle device scale = "
                      << g_meter_middle_device_scale.load() << "\n";
    }

    // Initial device state log (one consolidated line for log scanners)
    std::cout << "[INFO] " << make_device_state_line();

    // Safe startup state
    allMotionOff();

    cmd_server.setReceiveCallback(on_receive);
    if (!cmd_server.start(CMD_PORT, false)) {
        std::cerr << "[FATAL] TCP server start on port " << CMD_PORT << " failed" << std::endl;
        return 1;
    }
    std::cout << "[OK]   command server :" << CMD_PORT << " (type 'exit' to stop)" << std::endl;

    // Broadcast initial device_state so any client that connects later gets a
    // fresh snapshot via cmd_status; in-flight clients should also poll status
    // on (re)connect.
    broadcast_evt(make_device_state_line());

    watchdog_thread = std::thread(watchdog_loop);
    std::cout << "[OK] watchdog thread (timeout idle/hold " << WATCHDOG_TIMEOUT_MS_IDLE
              << " ms / motion min " << WATCHDOG_TIMEOUT_MS_MOTION_MIN
              << " ms, dynamic from motion_rope/cmd_roll_correct)" << std::endl;

    hold_thread = std::thread(hold_loop);
    std::cout << "[OK] hold safety thread (active="
              << HOLD_LOOP_ACTIVE_MS << "ms, idle=" << HOLD_LOOP_IDLE_MS << "ms)" << std::endl;

    g_meter_thread = std::thread(meter_loop);
    std::cout << "[OK] meter cache thread (poll "
              << METER_POLL_MS_IDLE << " ms idle / "
              << METER_POLL_MS_MOTION << " ms during motion)" << std::endl;

    g_se3_keepalive_thread = std::thread(se3_keepalive_loop);
    std::cout << "[OK] SE3 keepalive thread (" << SE3_KEEPALIVE_INTERVAL_MS
              << "ms always-on; readStatusWord on both SE3; first 10 ticks logged individually)"
              << std::endl;

    std::cout << "[INFO] balance: enabled=" << (g_balance_enabled.load() ? 1 : 0)
              << " kp=" << g_balance_kp.load() << "Hz/cm"
              << " cap_ratio=" << g_balance_trim_cap_ratio.load()
              << " (eff @hold " << (g_se3_hold_hz.load() * g_balance_trim_cap_ratio.load()) << "Hz"
              << " / @motion " << (g_se3_motion_hz.load() * g_balance_trim_cap_ratio.load()) << "Hz)"
              << " deadband=" << g_balance_deadband.load() << "cm"
              << " hz_min=" << g_balance_hz_min.load() << "Hz"
              << " hz_max_offset=" << g_balance_hz_max_offset.load() << "Hz (above base)"
              << " tick=" << BALANCE_TICK_MS << "ms" << std::endl;

    // Init complete — GUI uses this to enable action buttons. Broadcast EVT so
    // clients don't have to wait for their next status poll to find out.
    g_init_done.store(true);
    broadcast_evt("EVT init_done\n");
    std::cout << "[OK] init complete — accepting commands" << std::endl;

    // Local console
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "exit" || line == "quit") break;
        if (line == "status") std::cout << cmd_status();
    }

    std::cout << "[SHUTDOWN] stopping..." << std::endl;
    abort_flag = true;
    watchdog_stop = true;
    hold_loop_stop = true;
    g_meter_loop_stop = true;
    g_se3_keepalive_stop = true;
    if (watchdog_thread.joinable())       watchdog_thread.join();
    if (hold_thread.joinable())           hold_thread.join();
    if (g_meter_thread.joinable())        g_meter_thread.join();
    if (g_se3_keepalive_thread.joinable())g_se3_keepalive_thread.join();
    cmd_server.stop();
    allMotionOff();
    return 0;
}
