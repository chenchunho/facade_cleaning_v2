// ==========================================================
//  DamiaoAPI -- implementation
// ==========================================================
#include "main_api.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#ifndef _WIN32
#include <cerrno>
#endif

// ---- string helper ----------------------------------------------------------
static void ltrim(std::string& s) {
	s.erase(s.begin(),
		std::find_if(s.begin(), s.end(),
			[](unsigned char c) { return !std::isspace(c); }));
}

// ============================================================
//  init()
// ============================================================
bool DamiaoAPI::init(const char* port,
#ifdef _WIN32
	uint32_t      baud,
#else
	speed_t       baud,
#endif
	MotorConfig   cfg1,
	MotorConfig   cfg2,
	int           tcp_port)
{
	tcp_port_ = tcp_port;

#ifdef _WIN32
	WSADATA wsa{};
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		std::cerr << "[DamiaoAPI] WSAStartup failed\n";
		return false;
	}
	wsa_ok_ = true;
#endif

	try {
		serial_ = std::make_shared<SerialPort>(port, baud);
	}
	catch (const std::exception& e) {
		std::cerr << "[DamiaoAPI] SerialPort open failed: " << e.what() << "\n";
		return false;
	}
	catch (...) {
		std::cerr << "[DamiaoAPI] SerialPort open failed: " << port << "\n";
		return false;
	}

	dm_ = std::make_unique<damiao::Motor_Control>(serial_);

	// ---- M1 slot ----
	m1_.id = MotorSlot::SlotId::M1;
	m1_.name = "M1";
	m1_.lower_bound = 0.0f;         // stop = 0 after CALIBRATE; don't go negative
	m1_.upper_bound = 1.5f;         // hard upper limit: 1.5 rad
	// [2026-08-14 per user] 40.0/6.0 這組在手臂經過 VERTICAL_OFFSET_RAD(0.38，弧形
	// 最高點) 附近時出現失控震盪（HOLD 剛切換就衝到 vel=1.5 rad/s、tau 打到
	// -15Nm）——最高點附近重力力矩趨近零、過了頂點方向反轉，純 PD 反應式控制在
	// 這裡天生不穩，kp 太高只會讓過衝更猛。先降 kp、kd 相對少降一點（拉高阻尼
	// 比例、抑制震盪），治本作法是另外做重力前饋補償（ARM_MASS_KG），這只是先
	// 降低震盪風險的應急措施。
	// [2026-08-17] ⚠ kd 的協定上限是 5.0，不是「想調多大就多大」。
	// damiao.h control_mit 把 kd 編碼成 12-bit、範圍固定 [0,5]，而編碼函式原本
	// 沒有做 clamp，超過 5 的值會在打包進 uint8_t 時高位被砍掉、繞回成一個更小
	// 的數：kd=5.5 馬達實收 0.50、kd=6.0 實收 1.00。也就是說這行原本寫 5.5，
	// M1 實際上幾乎是「無阻尼」在跑——DEPLOY 停停走走、撐不住掉下去、以及歷史上
	// 每次「加大 kd 反而更糟」全部源自這裡。damiao.h 已補上 clamp（2026-08-17），
	// 這裡同步改成協定內的最大值 5.0（相對於先前實際生效的 0.50 是 10 倍阻尼，
	// 首次上機請慢速 + 有人在旁）。若 5.0 仍不夠穩，只能往 kp（範圍 0~500，
	// 現在才用 34，空間還很大）或 tau_ff 重力前饋去要，不要再動 kd。
	// 🔴 [2026-09-02 per user] 34 → 60，直接回應「是不是馬達出力不夠」。
	// **不是馬達**：DM10010L 的 TAU_MAX = 200 Nm（damiao.h limit_param，並由當日
	// 所有 tau 讀值量化在 400/4096 = 0.0977 Nm 的階梯上反證），而實測峰值僅約 7 Nm
	// ⇒ **只用到 3.5% 的能力**。手臂停在「kp*err 平衡（被高估的）重力前饋」的位置，
	// 而 kp=34 時 kp*err 只有 2.4 Nm —— 它不是推不動，是沒被要求去推。
	// 本檔談 kd 上限的註解早已指出這個槓桿：「只能往 kp（範圍 0~500，現在才用 34，
	// 空間還很大）或 tau_ff 去要」。
	// ⚠️ **2026-08-14 曾以 40.0/6.0 發生失控震盪**（過 0.38 附近 vel 衝到 1.5 rad/s、
	//    tau −15Nm）。但當時的 kd=6.0 因 MIT 編碼溢位**實際只有 1.00 Nm**（08-17 才修），
	//    現在 kd=5.0 是真值 ⇒ 阻尼是當時的 5 倍，這是敢往上調的依據。
	// 🔴 仍未解：重力前饋在工作區高估約 25%。提高 kp 只是用更大的位置誤差力去抵銷
	//    一個錯的前饋，殘差會變小但不會歸零。**這不是重力模型的替代品。**
	m1_.hold_kp = 90.0f;   // 2026-09-02: 34→60→90（per user：+40mm 壓入仍嫌不足；手壓基準本身偏淺）
	m1_.hold_kd = 5.0f;   // was 5.5 (= 0.50 after wrap-around); 5.0 is the protocol ceiling
	// [2026-07-24 per user] PARK 一路調過 12→8→14→11→9→10→16→14，速度也調過
	// 0.45→0.30→0.22→0.16→0.10——最後 user 決定直接跟 DEPLOY 用同一組（kp/kd/speed
	// 全部相等），不再分開調。MAX_LOOPS 拉長到 650 這件事保留（現在 speed 變快，
	// 650 loops 只是上限更寬鬆，不影響正常提前 arrive 結束）。
	// [2026-07-24 per user] 晃動根因是 PARK 目標(0 rad)是機械硬停點，撞到底會反彈，
	// DEPLOY 目標(貼牆角度)不是硬停點所以感覺不出來。方案 3：kd 加大抑制反彈震盪 +
	// go_home_slot 加到位前減速 creep（見該函式）。kd 3.0→6.0，只提高阻尼，kp 不動。
	// [2026-07-24 per user] 再小力一點 → park_kp 拆回獨立值 18.0→15.0；速度放慢
	// 一點點 → park_speed 0.35→0.28→0.20→0.15。
	// [2026-07-24 per user] 仍反應「速度太快 力道太大」→ 再降：kp 15.0→12.0、
	// speed 0.15→0.10（同時已加 PARK_STOP_MARGIN 讓 target 不再是硬停點本身，
	// 見 go_home_slot）。
	// [2026-07-24 per user] 速度再放慢、扭力再減小 → kp 12.0→9.0、speed 0.10→0.07。
	// [2026-07-24 per user] 扭力改回來 → kp 9.0→12.0（speed 維持 0.07 沒動）。
	// [2026-07-27 per user] 覺得撐不住、會往後躺 → kp/kd 都稍微加大，速度（park_speed）維持慢不變。
	// [2026-08-14 per user] 換了一顆更大的變壓器 → 恢復退回前驗證過的 park_kp/kd
	// （解決「DEPLOY 換手/PARK 收回原點無力」的症狀，當時退回純粹是懷疑電源容量）。
	// [2026-08-14 per user] touch_wall 現在會衝過目標一截才停（本體問題還沒解），
	// 代表換手收回時起點比預期更遠、更靠外，同一組 park_kp/kd 對這段多出來的
	// 距離不夠力 → 再加大應急。注意：這只是治標，起點會衝多遠不固定，之後這個
	// 值可能還要再調，真正要解的是 touch_wall 衝過頭這件事本身。
	// [2026-08-17] park_kd 同樣受上面那個 kd≤5.0 的協定上限限制，而且這組值
	// 從 2026-07-24 引入的第一天（初始值 7.0）就一直越線，從來沒有正確過。
	// 歷史值代進編碼算出來的「馬達實收」：7.0→2.00、8.5→3.50、10.0→5.00、
	// 12.0→2.00。注意最後那次「10→12 再加大應急」實際上是把阻尼從 5.0 砍到
	// 2.0，PARK 因此變得更無力——那筆調整的方向是反的，不是力道不夠。
	m1_.park_kp = 26.0f;
	m1_.park_kd = 5.0f;   // was 12.0 (= 2.00 after wrap-around); 5.0 is the protocol ceiling
	// [2026-08-18 per user] 0.07 → 0.15. The whole 0.45→0.07 descent was
	// compensation for faults now fixed at the root: kd was silently wrapping to
	// ~2.0 (damiao MIT encoding, 2026-08-17c), and the ramp's gravity feedforward
	// was computed from cur_cmd instead of pos (2026-08-18e). PARK also runs
	// AGAINST gravity (tau_ff is negative i.e. toward home, so gravity pulls the
	// arm outward) — it cannot run away downhill the way DEPLOY can, which is why
	// it gets the larger increase of the two. CREEP_ZONE/CREEP_SPEED still slow
	// the last 0.15 rad to 0.05 rad/s, so the approach to the stop is unchanged.
	// [2026-08-18 per user] 0.15 → 0.25（第二次提速）。摩擦補償上機後 bench
	// trace 顯示中段 Δpos 穩定、末段不再衝過頭、tau 也不再翻正，還有餘裕。
	// [2026-08-18 per user] 0.25 → 0.35（第三次提速）。CREEP_ZONE 由 0.15 縮到
	// 0.10 之後，時間分配整個翻轉：巡航段 0.72 rad 佔約 3.1s（79%）、creep 只剩
	// 約 1.0s，瓶頸已從 creep 移回主速度，所以這次動這裡才有效。
	// 可用 `M1 SET_PARK_SPEED <v>` 在 bench 現場掃最佳值，不必重編。
	m1_.park_speed = 0.35f;
	// 🔴 [2026-09-02 per user] M1 積分項由 0（預設＝關閉）改為 0.6 —— 與 M2 同值。
	//
	// 為什麼需要：DEPLOY 的落點**系統性短於目標**，且短少量隨目標角度擴大
	// （當日實測 wall=300 短 0.070 rad、wall=320 短 0.121 rad）。代入控制律：
	//     θ=0.4885 時 tau_ff = 20.87*sin(0.4885-3.317) = -6.43 Nm
	//                 kp*err = 34 * 0.1234            = +4.20 Nm
	//                 合計 -2.23 Nm，實測 tau = -2.39 Nm（吻合）
	// 手臂靜止代表那 +4.20 Nm 被靜摩擦吃掉 —— 與 2026-08-18 註解記載的
	// 「static friction at that angle is ≥4.6 Nm」對得上。
	//
	// 🔴 **HOLD 分支缺兩個能突破靜摩擦的機制，而 M1 兩個都沒有**：
	//   ① 摩擦前饋（M1_FRICTION_TAU，隨 |vel|→0 淡入）**只存在於 MOVE 分支**
	//   ② 積分項 —— `hold_ki` 預設 0，而 **M2 早就設了 0.6**，其註解正是
	//      「eliminates ~0.1 rad offset」，與 M1 今天的下垂同一量級。
	//   ⇒ ramp 結束交給 HOLD 之後，助力全失，kp*err 打不過靜摩擦就永遠停在那。
	//
	// 取 0.6 而非更大：`tau_i = hold_ki * hold_err_integral`，而 `HOLD_I_MAX = 2.0`
	// 把積分箝在 ±2.0 ⇒ **額外扭力上限 0.6×2.0 = 1.2 Nm**。突破靜摩擦只需約 0.4 Nm
	// 餘裕（4.6 − 4.2），1.2 有備而不過量。**DEPLOY 是順重力方向，積分過大會把手臂
	// 推進玻璃**，所以刻意不調快。
	// ⚠️ 代價是**慢**：err≈0.07 rad 時要約 10 秒才累積到 0.4 Nm。
	//    觀察時要等移動結束後再看 15~20 秒，不能只讀 DEPLOY 的當下回傳值。
	// 🔴 [2026-09-02] 移除（當日稍早才加，同日移除）。
	// 加它的理由是 DEPLOY 落點系統性短於目標，積分確實把殘差由 0.093 壓到 0.069。
	// **但它在「頂住玻璃」時是有害的**：位置誤差永遠不會消失（命令角遠在玻璃後方），
	// 積分因此持續累積，實測貼合後 10 秒內 tau 由 +0.24 爬到 +1.51 且未停，
	// 壓入量跟著一路增加 —— **貼合狀態不是穩定狀態，校 wall_mm 等於對著移動目標校**。
	// 🔧 要重新啟用必須先做 anti-windup on contact（誤差長時間不下降就停止累積）；
	//    在那之前寧可保留可預測的靜態誤差，也不要不可預測的持續加壓。
	// 📌 M2 的 hold_ki=0.6 不受影響 —— 它是水平軸、不會頂在牆上累積。
	// m1_.hold_ki 維持預設 0（見 main_api.h MotorSlot::hold_ki）

	// DEPLOY 起步值比 PARK 保守（順重力、失控會被重力持續加速），可用
	// `M1 SET_DEPLOY_SPEED <v>` 在執行期微調，不必重編。
	m1_.deploy_speed = 0.15f;
	m1_.motor = std::make_unique<damiao::Motor>(cfg1.type, cfg1.slave_id, cfg1.master_id);
	dm_->addMotor(m1_.motor.get());

	// ---- M2 slot ----
	m2_.id = MotorSlot::SlotId::M2;
	m2_.name = "M2";
	m2_.lower_bound = -ZERO_OFFSET; // allow RIGHT slot at -ZERO_OFFSET
	// upper_bound keeps default 1e9f (unconstrained)
	//old : 8, 3, 0.5
	// [2026-06-09v] hold_kp 4 → 2
	// [2026-06-09aa] 參考 reference tuning: hold_kp=2.5, hold_kd=1.0
	// [2026-08-13 per user] 刷頭端變重、撐不住 → hold_kp 2.5→4.0，hold_kd 同步 1.0→1.5
	// [2026-08-13d per user] 頭重很多，全面加大 → hold_kp 4.0→7.0，hold_kd 1.5→3.0，
	// hold_ki 0.3→0.6（跟 M1 之前撐不住時大幅拉高的模式一致）
	// [2026-08-18 per user] hold_kd 3.0 → 5.0（協定上限）。M2 過衝／甩頭的直接
	// 成因是阻尼不足：INIT 短距離回中心衝過 target 兩倍距離。M1 那邊已經證實
	// kd 對抑制過衝最有效，而 M2 的 3.0 距離上限還有空間沒用（M1 是撞到 5.0 才
	// 沒得加）。這也順便讓 HOLD 更穩。注意 5.0 是 damiao MIT 編碼的硬上限，
	// 再往上寫會被 clamp（2026-08-17c 之前甚至會繞回成更小的值）。
	m2_.hold_kp = 7.0f;
	m2_.hold_kd = 5.0f;
	m2_.park_kp = m2_.hold_kp;   // M2 PARK/DEPLOY unchanged — user only asked to split M1
	m2_.park_kd = m2_.hold_kd;
	m2_.hold_ki = 0.6f;   // ki*HOLD_I_MAX=0.6*2.0=1.2 Nm > ~0.8 Nm friction; eliminates ~0.1 rad offset
	m2_.motor = std::make_unique<damiao::Motor>(cfg2.type, cfg2.slave_id, cfg2.master_id);
	dm_->addMotor(m2_.motor.get());

	// ---- init both motors ---------------------------------------------------
	// [2026-05-29] switchControlMode retry loop.
	// 之前每次前次 motor_api 被 ^C 強砍時若 M1 還在執行 MIT control
	// (touch_wall 中), 重啟新 motor_api 第一次 switchControlMode 拿不到 ACK ->
	// init failed -> 只能斷 M1 電源才能救. 現在改成:
	//   - disable + 200ms settle (清 motor 暫態)
	//   - refresh_motor_status 探活 + 順便 receive() 一次清掉 serial RX 殘留 frame
	//   - 100ms 再 switchControlMode
	//   - 失敗則整個流程重來, 最多 5 次, 每次間隔 500ms
	// 救得了 stale serial buffer + motor 暫態; 救不了 firmware lock (那種還是要斷電).
	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		for (MotorSlot* s : { &m1_, &m2_ }) {
			bool ok = false;
			for (int attempt = 0; attempt < 5; ++attempt) {
				dm_->disable(*s->motor);
				usleep(200000);                      // 200ms settle
				dm_->refresh_motor_status(*s->motor); // probe + drain stale CAN frames
				usleep(100000);                      // 100ms

				if (dm_->switchControlMode(*s->motor, damiao::MIT_MODE)) {
					ok = true;
					break;
				}
				std::cerr << "[DamiaoAPI] switchControlMode attempt " << (attempt + 1)
					<< "/5 failed for " << s->name
					<< ", retrying in 500ms\n";
				usleep(500000);
			}
			if (!ok) {
				std::cerr << "[DamiaoAPI] switchControlMode FINAL failed for "
					<< s->name << " (5 attempts) — power cycle may be needed\n";
				return false;
			}
			std::cout << "[DamiaoAPI] " << s->name << " init OK"
				<< "  (slave=0x" << std::hex << s->motor->GetSlaveId()
				<< " master=0x" << s->motor->GetMasterId() << std::dec << ")\n";
		}
	}

	// ---- TCP listen socket --------------------------------------------------
	listen_sock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_sock_ == INVALID_SOCKET) {
		std::cerr << "[DamiaoAPI] socket() failed\n";
		return false;
	}

	int opt = 1;
#ifdef _WIN32
	::setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR,
		reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
	::setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(static_cast<uint16_t>(tcp_port_));

	if (::bind(listen_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
		std::cerr << "[DamiaoAPI] bind() failed on port " << tcp_port_ << "\n";
		closesocket(listen_sock_);
		listen_sock_ = INVALID_SOCKET;
		return false;
	}
	if (::listen(listen_sock_, 5) == SOCKET_ERROR) {
		std::cerr << "[DamiaoAPI] listen() failed\n";
		closesocket(listen_sock_);
		listen_sock_ = INVALID_SOCKET;
		return false;
	}

	std::cout << "[DamiaoAPI] TCP server ready on port " << tcp_port_ << "\n";
	return true;
}

// ============================================================
//  start() / stop()
// ============================================================
void DamiaoAPI::start()
{
	if (running_) return;
	running_ = true;
	server_thread_ = std::thread(&DamiaoAPI::server_loop, this);
	feedback_thread_ = std::thread(&DamiaoAPI::feedback_loop, this);
	std::cout << "[DamiaoAPI] Background TCP server started\n";
}

void DamiaoAPI::stop()
{
	if (!running_ && listen_sock_ == INVALID_SOCKET) return;
	running_ = false;

	if (listen_sock_ != INVALID_SOCKET) {
		closesocket(listen_sock_);
		listen_sock_ = INVALID_SOCKET;
	}
	if (server_thread_.joinable())   server_thread_.join();
	if (feedback_thread_.joinable()) feedback_thread_.join();

	disable_slot(m1_);
	disable_slot(m2_);

#ifdef _WIN32
	if (wsa_ok_) { WSACleanup(); wsa_ok_ = false; }
#endif
	std::cout << "[DamiaoAPI] Stopped\n";
}

// ============================================================
//  registerCommand
// ============================================================
void DamiaoAPI::registerCommand(const std::string& key, CommandHandler handler)
{
	std::string upper = key;
	std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
	cmd_map_[upper] = std::move(handler);
}

// ============================================================
//  Slot operations
// ============================================================
void DamiaoAPI::enable_slot(MotorSlot& s)
{
	s.hold_en = false;
	s.move_act = false;
	s.enabled = false;

	// Pause the peer slot for the duration of enable() + state-refresh frames.
	// enable() calls serial_->send() outside motor_mutex_, so a concurrent
	// feedback_loop write for the peer motor corrupts both CAN frames on the shared
	// serial bus — occasionally leaving this motor hardware-disabled yet MIT-responsive
	// (state_q updates, tau≈0 despite kp*error > stiction). Pausing the peer
	// ensures enable() is the sole serial writer during its 100 ms window.
	MotorSlot& peer = (&s == &m1_) ? m2_ : m1_;
	bool was_peer = peer.enabled.exchange(false);

	dm_->enable(*s.motor);   // 100 ms internal sleep; no concurrent serial writes now

	// Refresh state_q: enable() response is CMD=0x12 (not parsed by receive()); MIT
	// frames force CMD=0x11 responses that update Get_Position() to actual position.
	for (int i = 0; i < 5; ++i) {
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			dm_->control_mit(*s.motor, 0.0f, s.hold_kd, 0.0f, 0.0f, 0.0f);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}



	if (was_peer) peer.enabled = true;   // restore peer before hold setup

	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		s.hold_pos = s.motor->Get_Position();
		s.hold_tau_ff = 0.0f;   // clear stale feedforward; updated by HOLD cmd or move completion
		s.hold_err_integral = 0.0f;
	}
	s.hold_en = true;   // arm before enabled so feedback_loop never sees a passive gap
	s.enabled = true;
}

void DamiaoAPI::disable_slot(MotorSlot& s)
{
	s.enabled = false;
	s.hold_en = false;
	s.move_act = false;
	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		dm_->disable(*s.motor);
	}
}

void DamiaoAPI::set_zero_slot(MotorSlot& s)
{
	s.hold_en = false;
	s.move_act = false;
	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		dm_->set_zero_position(*s.motor);
		s.hold_pos = 0.0f;
		s.move_cur = 0.0f;
		s.move_target = 0.0f;
		s.hold_err_integral = 0.0f;
	}
}

void DamiaoAPI::hold_slot(MotorSlot& s)
{
	if (!s.enabled) return;
	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		s.hold_pos = s.motor->Get_Position();
		s.hold_err_integral = 0.0f;
		if (s.id == MotorSlot::SlotId::M1)
			s.hold_tau_ff = s.motor->Get_tau();   // gravity proxy at HOLD time
	}
	s.hold_en = true;
}

void DamiaoAPI::release_hold_slot(MotorSlot& s)
{
	s.hold_en = false;
}

void DamiaoAPI::move_to_slot(MotorSlot& s, float target_rad, float speed_rad_s)
{
	if (!s.enabled) return;

	float clamped = target_rad;
	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		// upper clamp 1: hard slot limit (M1=1.2 rad, M2=unconstrained)
		clamped = std::min(clamped, s.upper_bound);
		// upper clamp 2: SETWALL geometry safety (M1 only; M2 wall_dist is always 0)
		if (s.wall_dist > 0.0f) {
			float usable = s.wall_dist - PASSIVE_EXT_MM;
			float theta_max = (usable <= 0.0f)
				? VERTICAL_OFFSET_RAD
				: VERTICAL_OFFSET_RAD + std::asin(std::min(usable / ARM_LENGTH_MM, 1.0f));
			clamped = std::min(clamped, theta_max);
		}
		// lower clamp: M1=0.0f (stop=0 after calibrate), M2=-ZERO_OFFSET (RIGHT slot)
		clamped = std::max(clamped, s.lower_bound);

		s.hold_en = false;
		s.move_cur = s.motor->Get_Position();
		s.move_target = clamped;
		s.move_speed = std::max(speed_rad_s, 0.01f);
		// Use stored gravity proxy (hold_tau_ff) instead of live Get_tau(), which would
		// include PD corrective torque from a non-equilibrium hold and cause overshoot.
		s.move_tau_ff = (s.id == MotorSlot::SlotId::M1) ? s.hold_tau_ff : 0.0f;
		s.move_act = true;   // no gap: hold→move transition atomic under mutex
	}
}

bool DamiaoAPI::approach_wall_slot(MotorSlot& s, float clearance_mm, float speed_rad_s)
{
	if (!s.enabled) return false;
	float wall_dist;
	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		wall_dist = s.wall_dist;
	}
	if (wall_dist <= 0.0f) return false;

	float usable = wall_dist - clearance_mm - PASSIVE_EXT_MM;
	float theta_target = (usable <= 0.0f)
		? VERTICAL_OFFSET_RAD
		: VERTICAL_OFFSET_RAD + std::asin(std::min(usable / ARM_LENGTH_MM, 1.0f));

	move_to_slot(s, theta_target, speed_rad_s);
	return true;
}

// ============================================================
//  touch_wall_slot()  -- M1: move to slot-tool-tip just at wall
// ============================================================
// 🔴 [2026-09-02] M1 速度安全閥的常數，由 feedback_loop() 內部的區域常數抽到檔案範圍。
//
// 抽出來的理由不是整潔，是**同一個保護必須覆蓋所有驅動路徑**：原本它只存在於
// feedback_loop() 的 HOLD/MOVE 分支，而 `go_home_slot()` 進入時會把 s.enabled 設為 false，
// feedback_loop 因此 `continue` 跳過 M1 —— **整段 ramp 完全沒有速度保護**。
// 2026-08-18 的註解就記過這個缺口（「實際速度衝到 -0.4335…而 go_home_slot 這條路徑
// 沒有速度安全閥」），但只是記下來、沒有補。
// 🔴 **2026-09-02 實測顯示嚴重度被低估了一個數量級**：長行程收回時
//    |vel| 達 **1.5069 / 1.5690 / 2.2283 rad/s**（三次獨立量測），是限制值的 3.8~5.6 倍，
//    全程沒有任何 [M1 SAFETY] 觸發。08-18 記的只有 0.4335（超出 8%）。
static const float M1_VEL_SAFETY_LIMIT   = 0.4f;   // rad/s；超過即緊急煞車
static const float M1_EMERGENCY_BRAKE_KD = 5.0f;   // 協定上限（20.0 會被 clamp 成這個值）

bool DamiaoAPI::touch_wall_slot(MotorSlot& s, float wall_dist_mm,
	int m2_slot, float clearance_mm,
	float speed_rad_s)
{
	if (!s.enabled) return false;

	float tool_ext;
	if (m2_slot < 0) tool_ext = TOOL_EXT_LEFT_MM;
	else if (m2_slot > 0) tool_ext = TOOL_EXT_RIGHT_MM;
	else                   tool_ext = TOOL_EXT_CENTER_MM;

	float total_ext = PASSIVE_EXT_MM + tool_ext;
	float usable = wall_dist_mm - clearance_mm - total_ext;
	float theta_target = (usable <= 0.0f)
		? VERTICAL_OFFSET_RAD
		: VERTICAL_OFFSET_RAD + std::asin(std::min(usable / ARM_LENGTH_MM, 1.0f));

	// [2026-08-14 per user] Passive-state detection + re-enable, mirroring the
	// same check already proven for M2 in lr_move_to_slot_impl (2026-06-06).
	// Bench just showed the identical fingerprint on M1: tau stuck near 0 Nm
	// for the entire move (should have been ~kp*0.63 rad error, tens of Nm)
	// while pos never left its starting point — MIT frames ACKing but no real
	// torque output. Probe with a few frames toward theta_target and sample
	// tau; if it stays near zero, re-enable before starting the real move.
	{
		const float TAU_LIVE_THRESHOLD = 0.3f;
		float cur_pos;
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			cur_pos = s.motor->Get_Position();
		}
		float probe_setpt = cur_pos + (theta_target > cur_pos ? 0.3f : -0.3f);
		float last_tau = 0.0f;
		for (int k = 0; k < 3; ++k) {
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				dm_->control_mit(*s.motor, s.hold_kp, s.hold_kd, probe_setpt, 0.0f, 0.0f);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			last_tau = s.motor->Get_tau();
		}
		if (std::abs(last_tau) < TAU_LIVE_THRESHOLD) {
			std::cerr << "[M1 touch_wall_slot] motor passive (tau=" << last_tau
				<< " Nm < " << TAU_LIVE_THRESHOLD << "), re-enabling\n";
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				dm_->enable(*s.motor);
			}
			for (int k = 0; k < 5; ++k) {
				{
					std::lock_guard<std::mutex> lk(motor_mutex_);
					dm_->control_mit(*s.motor, s.hold_kp, s.hold_kd, probe_setpt, 0.0f, 0.0f);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		}
	}

	move_to_slot(s, theta_target, speed_rad_s);

	std::cout << "[M1 touch_wall_slot] theta_target=" << theta_target << std::endl;
	return true;
}

// ============================================================
//  go_home_slot()  -- speed-step to 0, position-based arrival
//  Returns true = reached target within ARRIVE_TOL; false = gave up short.
//  [2026-08-18] Was void — bench PARK log showed it reporting "ramp ARRIVED"
//  at pos=0.2180 against target=0.0500 (0.168 rad short), after which
//  cmd_park_sequence dropped a disable_slot() and the arm fell. See the
//  arrival-criteria and passive-watchdog comments inside for the full story.
// ============================================================
bool DamiaoAPI::go_home_slot(MotorSlot& s, bool use_park_profile)
{
	// [2026-07-24 per user] cmd_deploy_sequence's Step 1 (retract before
	// re-extending to a new slot) calls this same function — it must NOT
	// inherit PARK's slow/gentle tuning (that made switching LEFT/RIGHT slots
	// feel much slower than before, which was never the intent). use_park_profile
	// selects which gain/speed/target set applies; false = original fast
	// DEPLOY-matching behavior (hold_kp/hold_kd, 0.45 rad/s, target=0, no
	// stop-short margin) exactly as this function always behaved before today's
	// PARK-specific tuning began.
	const float kp    = use_park_profile ? s.park_kp   : s.hold_kp;
	const float kd    = use_park_profile ? s.park_kd   : s.hold_kd;
	// [2026-07-27 per user] 0.45→0.35 — DEPLOY 內部 Step 1（LEFT→RIGHT 切換前的
	// M1 retract）稍微放慢，不是恢復成 PARK 的慢速 profile（那組 kp/kd/speed 都
	// 沒動），只是這個非 park 分支自己的速度稍微降一點。
	const float SPEED = use_park_profile ? s.park_speed : 0.35f;
	const float DT = 0.02f;
	const float ARRIVE_TOL = 0.05f;
	const float VEL_TOL = 0.02f;        // rad/s — treat motor as stopped below this threshold
	const int   ARRIVE_CNT = 3;
	// [2026-07-24 per user] park_speed 一路降到 0.10 之後，3s (150 loops) 的 ramp
	// 額度 (0.10×3=0.30 rad) 遠小於 M1 收回的典型距離 (~0.8-1.1 rad) —— ramp 沒走完
	// 就被砍掉，掉進下面 settle 迴圈直接下「目標=0」的瞬間階躍指令，反而製造一次
	// 猛然的力道，抵銷慢速 ramp 原本想要的效果。拉長到 650 loops (13s)。
	// [2026-07-24 per user] speed 再降到 0.07 之後，650 loops(13s) 只夠走 0.91 rad，
	// 仍小於最壞情況 ~1.1 rad — 再拉長到 900 loops (18s) 確保 ramp 走得完全程。
	const int   MAX_LOOPS = 900;
	const int   MAX_SETTLE = 100;       // 2s extra settle after ramp reaches 0
	// [2026-07-24 per user] Creep-to-target: PARK's target (0 rad) is M1's actual
	// mechanical hard stop (calibrate_arm_slot sets zero AT the stop, unlike M2
	// whose zero sits ZERO_OFFSET away from its stop) — arriving with residual
	// ramp velocity bounces off that stop (the "晃動" symptom). Regardless of the
	// cruise SPEED, force the ramp step down to CREEP_SPEED once within
	// CREEP_ZONE rad of target, so it always settles into the stop gently.
	// [2026-08-18 per user] CREEP_ZONE 0.15→0.10、CREEP_SPEED 0.05→0.08.
	// Bench 量到的事實：park_speed 從 0.15 提到 0.25（+67%）只讓 PARK 從 5.54s
	// 縮到 5.32s（4%），因為時間根本不花在巡航段——cmd 斜率顯示 0.6204→0.2004
	// 走 0.25 rad/s，但 0.2004→0.0644 這 0.15 rad 全在 creep，耗掉約 3 秒，
	// 佔總時間 56%。瓶頸在 creep，不在 SPEED。
	// 敢動它的理由：上面那段 2026-07-24 的原始註解寫的是「target(0 rad) 就是機械
	// 硬停點，帶著殘速撞上去會反彈」——但後來加入的 PARK_STOP_MARGIN 已經讓
	// target 停在硬停點前 0.05 rad，主動控制下根本不接觸硬停點，creep 原本要防的
	// 那個碰撞已經被 margin 擋掉了，不需要再用這麼慢的速度買第二層保險。
	// 預估 PARK 由 5.3s 縮到 3.5s 上下。
	// [2026-08-18 per user] CREEP_SPEED 0.08 → 0.12（CREEP_ZONE 維持 0.10）。
	// 上一輪把 zone 縮小後 creep 只剩約 1.0s，但配合主速度提到 0.35 之後它又會
	// 變回相對大的一塊，所以一併提。zone 不再縮：0.10 rad 已經是主速度 0.35
	// 降到 creep 速度所需的減速距離下限，再縮會讓慣性直接帶著手臂衝過 target。
	// [2026-08-18 per user] CREEP_SPEED 0.12 → 0.10（退半格）。0.12 實測開始過衝：
	// ramp ARRIVED 落在 pos=0.0414，已經低於 target=0.0500，且實際速度衝到
	// -0.4335（超過 M1_VEL_SAFETY_LIMIT=0.4 的設計值，而 go_home_slot 這條路徑
	// 沒有速度安全閥），tau 反向煞車峰值也從 +0.6349 漲到 +1.1233。
	// 距硬停點只剩 0.041 rad，安全邊際太薄，用約 0.16s 換回落點在 target 之上。
	const float CREEP_ZONE  = 0.10f;
	const float CREEP_SPEED = 0.10f;
	// [2026-07-24 per user] "往回甩" persisted even after removing the tau_ff
	// contamination and adding creep — the remaining suspect is commanding M1
	// all the way to 0, which IS the real mechanical hard stop (calibrate_arm_slot
	// sets zero AT the stop). Any active PD holding exactly at a rigid stop is
	// prone to a physical bounce-back off that stop regardless of how gently it
	// arrives. Stop short of the stop by PARK_STOP_MARGIN for M1 so it never
	// actually contacts the hard limit under active control. M2's home isn't a
	// hard stop (zero sits ZERO_OFFSET away from M2's stop), so it keeps target=0.
	const float PARK_STOP_MARGIN = 0.05f;
	const float target = (use_park_profile && s.id == MotorSlot::SlotId::M1) ? PARK_STOP_MARGIN : 0.0f;

	bool was_enabled = s.enabled.exchange(false);

	// Use hold_pos (the target feedback_loop was maintaining) rather than Get_Position()
	// (actual pos). If gravity slightly exceeded hold_tau_ff, the motor may have drifted
	// positive; Get_Position() would set cur_cmd above hold_pos, dropping PD to near-zero
	// and letting gravity win at handoff. hold_pos keeps PD continuous with feedback_loop.
	// Safe without mutex: s.enabled=false, so feedback_loop no longer writes hold_pos.
	float cur_cmd = s.hold_pos;

	// Guard: M1 hold_pos may be stale after a crash (encoder offset survives power cycle).
	// Clamp to physical range before starting the ramp to prevent dangerous large motion.
	if (s.id == MotorSlot::SlotId::M1
			&& (cur_cmd < s.lower_bound || cur_cmd > s.upper_bound)) {
		std::cerr << "[" << s.name << " go_home] hold_pos=" << cur_cmd
			<< " out of range [" << s.lower_bound << ", " << s.upper_bound
			<< "], clamping\n";
		cur_cmd = std::max(s.lower_bound, std::min(s.upper_bound, cur_cmd));
		s.hold_pos = cur_cmd;
	}

	float start_pos = cur_cmd;

	// [2026-07-27 per user — REVERTED] Had ported a passive-state probe here
	// (3 frames commanding a full ±1.0 rad offset setpoint to sample tau,
	// mirroring lr_move_to_slot_impl's 2026-06-06 passive-recovery check).
	// Turned out to be wrong on two counts: (1) it never actually fired on
	// bench — M2's PARK timeout wasn't CAN-passive, it was just under-torqued
	// (see cmd_park_sequence's fix, now routes M2 PARK through
	// lr_move_to_slot_impl(CENTER) instead of go_home_slot at all); (2) it ran
	// unconditionally on EVERY go_home_slot call, including M1's non-PARK
	// retract-before-redeploy step in cmd_deploy_sequence — sending a real
	// ±1.0 rad probe setpoint via control_mit for 60ms produced a visible
	// jerk right before every move, and left cur_cmd/start_pos out of sync
	// with where the probe had already nudged the arm, causing the reported
	// "doesn't reach the configured touch-wall distance, moves erratically".
	// Removed entirely — do not re-add without a way to scope it to just the
	// PARK path that actually needs it (which no longer goes through here for
	// M2 anyway).

	// [2026-07-24 per user] Old ad-hoc gravity feedforward (based on
	// s.hold_tau_ff, the torque measured while M1 was pressed against the
	// wall) was removed here — that value was contaminated by wall contact
	// reaction force, not pure gravity. [2026-08-14 per user] Replaced below
	// with the empirically-fit M1_GRAVITY_K/M1_GRAVITY_PHASE_RAD model
	// (see main_api.h) instead of leaving PARK with no feedforward at all.

	// [2026-08-14 per user] Passive-state probe, re-added after M1 showed the
	// identical fingerprint touch_wall_slot just got fixed for (tau stuck near
	// 0 the whole move, no real torque despite a big position error) — this
	// time on PARK ("軟掉" while user caught the falling arm by hand). The
	// 2026-07-27 revert of an earlier attempt here flagged two real problems,
	// both addressed this time:
	//   1. was unconditional on every go_home_slot call, including DEPLOY's
	//      internal M1 retract-before-redeploy step, causing a visible jerk
	//      on every single move — this time scoped to use_park_profile only
	//      (real PARK calls), leaving the DEPLOY retract path untouched.
	//   2. left cur_cmd/start_pos stale after the probe moved the motor,
	//      desyncing the ramp start from the real position — this time
	//      cur_cmd/start_pos are refreshed from Get_Position() right after.
	if (use_park_profile && s.id == MotorSlot::SlotId::M1) {
		const float TAU_LIVE_THRESHOLD = 0.3f;
		// [2026-08-14 per user] 0.3 rad 偏移量在 target(0.05) 已經很靠近 cur_cmd
		// 的情況下，方向判斷可能反過來（往「前」偏），造成 PARK 一開始出現不必要
		// 的往前小衝——縮小成 0.05 rad，配合 park_kp/kd 仍足夠產生超過
		// TAU_LIVE_THRESHOLD 的扭力差異來判斷是否 passive，但實際造成的移動量
		// 小很多，降低不必要動作的風險。
		float probe_setpt = cur_cmd + (target > cur_cmd ? 0.05f : -0.05f);
		float last_tau = 0.0f;
		for (int k = 0; k < 3; ++k) {
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				dm_->control_mit(*s.motor, kp, kd, probe_setpt, 0.0f, 0.0f);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			last_tau = s.motor->Get_tau();
		}
		if (std::abs(last_tau) < TAU_LIVE_THRESHOLD) {
			std::cerr << "[" << s.name << " go_home] motor passive (tau=" << last_tau
				<< " Nm < " << TAU_LIVE_THRESHOLD << "), re-enabling\n";
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				dm_->enable(*s.motor);
			}
			for (int k = 0; k < 5; ++k) {
				{
					std::lock_guard<std::mutex> lk(motor_mutex_);
					dm_->control_mit(*s.motor, kp, kd, probe_setpt, 0.0f, 0.0f);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		}
		// Refresh cur_cmd/start_pos from real position — the probe (whether or
		// not it triggered re-enable) may have moved the motor; starting the
		// ramp from a stale reference is exactly what caused the erratic-move
		// bug in the 2026-07-27 revert.
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			cur_cmd = s.motor->Get_Position();
		}
		start_pos = cur_cmd;
	}

	// Warmup: 3 frames at hold_pos before ramping. Prevents transient drop at control
	// handoff: the first ramp step would otherwise produce kp*(-step) + tau_ff which
	// briefly lets gravity win. s.id and start_pos are constant throughout go_home_slot.
	// [2026-08-14 per user] Same M1_GRAVITY_K/PHASE model as the main ramp loop
	// below, evaluated once at start_pos (this warmup doesn't move cur_cmd).
	const float tau_warm = (s.id == MotorSlot::SlotId::M1 && start_pos > M1_GRAVITY_MIN_VALID_RAD)
	                       ? M1_GRAVITY_K * std::sin(start_pos - M1_GRAVITY_PHASE_RAD) : 0.0f;
	for (int w = 0; w < 3; ++w) {
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			dm_->control_mit(*s.motor, kp, kd, cur_cmd, 0.0f, tau_warm);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	int arrive_cnt = 0;

	// [2026-07-27 diagnostic, per user] Log why the ramp loop exited — ARRIVED
	// (position/velocity criteria satisfied) vs TIMEOUT (ran the full MAX_LOOPS
	// with neither criterion ever met, e.g. motor not responding to control_mit
	// at all) — to disambiguate "PARK is just slow" from "PARK is actually stuck".
	int ramp_loops_used = 0;
	bool ramp_arrived = false;

	// [2026-08-18] Mid-ramp passive-state watchdog state. Same thresholds as
	// feedback_loop()'s HOLD/MOVE watchdog so both paths judge "passive" the
	// same way. Cooldown is counted in ramp iterations (20ms each) because
	// dm_->enable() blocks ~100ms and must not be re-issued every tick.
	const float PASSIVE_ERR_TOL = 0.1f;   // rad — position error that should produce torque
	const float PASSIVE_TAU_TOL = 0.3f;   // Nm  — below this the motor isn't really pushing
	int passive_cooldown   = 0;
	int passive_recoveries = 0;

	for (int i = 0; i < MAX_LOOPS; ++i) {
		ramp_loops_used = i + 1;
		float pos = s.motor->Get_Position();
		float vel = s.motor->Get_Velocity();

		// [2026-08-14 per user] BUG: this used to only check position, so if the
		// arm swept THROUGH the target zone at high speed (e.g. still carrying
		// vel=-0.75 rad/s from an earlier fast phase), 3 consecutive 20ms
		// samples of "position happens to be close" could satisfy this and
		// declare ARRIVED — skipping the settle-loop below entirely (it only
		// runs when arrive_cnt < ARRIVE_CNT) and handing a still-fast-moving
		// arm straight to HOLD. Now requires velocity to actually be low too,
		// not just position.
		if (std::abs(pos - target) < ARRIVE_TOL && std::abs(vel) < VEL_TOL) ++arrive_cnt; else arrive_cnt = 0;
		if (arrive_cnt >= ARRIVE_CNT) { ramp_arrived = true; break; }
		// [2026-08-18] ARRIVE_TOL*4 → *1.5. The x4 form made this a 0.20 rad
		// window, and paired with "velocity is low" it accepted a STALLED arm as
		// ARRIVED: bench PARK logged ramp ARRIVED at pos=0.2180 / target=0.0500
		// (0.168 rad short, vel 0.0061) because 0.168 < 0.20. A motor that has
		// stopped producing torque has a low velocity too, so this pair of
		// conditions cannot tell "settled onto target" from "stuck". Keeping a
		// small tolerance band (the intent — accept arriving slightly short
		// rather than grinding at the creep speed forever) but tightening it to
		// 0.075 rad so a real stall no longer fits inside it.
		if (std::abs(pos - target) < ARRIVE_TOL * 1.5f && std::abs(vel) < VEL_TOL) { ramp_arrived = true; break; }

		float diff = target - cur_cmd;
		const float eff_speed = (std::abs(diff) < CREEP_ZONE) ? CREEP_SPEED : SPEED;
		float step = eff_speed * DT;
		if (std::abs(diff) <= step) cur_cmd = target;
		else cur_cmd += (diff > 0.0f ? step : -step);

		// [2026-08-14 per user] 舊的 init_tau_ff 前饋一直是 0.0f（2026-07-24 就
		// 停用了，見上面 init_tau_ff 宣告處的說明），PARK 全程完全沒有重力補償，
		// 純靠 park_kp/kd 硬頂——但我們現在已經用實測反推出 M1 真正的重力模型
		// （M1_GRAVITY_K/M1_GRAVITY_PHASE_RAD，真正的零點在硬體範圍外，代表
		// PARK 收回全程都在對抗重力）。改用這組經過驗證的模型即時算出當下角度
		// 需要的補償扭力，取代舊的、範圍限定在 VERTICAL_OFFSET_RAD 以上、且
		// 因 init_tau_ff=0 而形同虛設的邏輯。
		// [2026-08-18] cur_cmd → pos. Gravity torque depends on where the arm
		// ACTUALLY is, not where we are commanding it to go. Using the commanded
		// position meant that whenever the arm lagged behind the ramp — exactly
		// when it needs the most help — the feedforward was computed for a point
		// the arm had not reached, so it under-compensated, and once cur_cmd fell
		// below M1_GRAVITY_MIN_VALID_RAD it switched off entirely while the arm
		// was still up high.
		// Bench trace of the resulting two-stage PARK (arm stuck at pos≈0.66):
		//     cmd=0.6202 pos=0.6357  tau_ff=-8.98  (should be -9.26)
		//     cmd=0.5026 pos=0.6678  tau_ff=-6.71  (should be -9.79)
		//     cmd=0.1878 pos=0.6601  tau_ff= 0.00  (should be -9.66)
		// This is self-reinforcing: under-compensate → can't move → lag grows →
		// cur_cmd drops further → even less feedforward. The arm only broke free
		// once kp*error alone grew past static friction, producing the visible
		// stall-then-jump. Proof the fix is right: the settle loop below already
		// computes its feedforward from pos, and on the same bench run it moved
		// the arm from 0.2535 to 0.0959 using the same kp target the stalled ramp
		// had been holding — the only difference was the correct tau_ff.
		// feedback_loop()'s MOVE and HOLD branches have always used Get_Position()
		// here; this brings the ramp in line with them.
		float tau_ff = 0.0f;
		if (s.id == MotorSlot::SlotId::M1 && pos > M1_GRAVITY_MIN_VALID_RAD) {
			tau_ff = M1_GRAVITY_K * std::sin(pos - M1_GRAVITY_PHASE_RAD);
		}

		// [2026-08-18] Coulomb friction breakaway assist — see main_api.h for the
		// bench measurements this is sized from. Direction comes from the overall
		// error to target (stable for the whole move) rather than cur_cmd-pos
		// (small and noisy when tracking well, so its sign flickers).
		if (s.id == MotorSlot::SlotId::M1) {
			float err_to_target = target - pos;
			if (std::abs(err_to_target) > M1_FRICTION_DEADBAND_RAD) {
				float fade = 1.0f - std::min(std::abs(vel) / M1_FRICTION_FADE_VEL, 1.0f);
				tau_ff += M1_FRICTION_TAU * fade * (err_to_target > 0.0f ? 1.0f : -1.0f);
			}
		}

		// 🔴 [2026-09-02] 速度安全閥 —— 補上 go_home 這條路徑長期缺少的保護。
		// 與 feedback_loop() 用同一組常數、同一種煞車手法（kp=0、只留 kd 阻尼，
		// 並保留重力前饋 —— 08-17 已證實煞車時把 tau_ff 歸零等於放掉重力補償，
		// 數學上撐不住手臂）。
		// 只對 M1 生效：M2 是水平軸，沒有重力失控的路徑。
		if (s.id == MotorSlot::SlotId::M1 && std::abs(vel) > M1_VEL_SAFETY_LIMIT) {
			float brake_tau_ff = 0.0f;
			if (pos > M1_GRAVITY_MIN_VALID_RAD)
				brake_tau_ff = M1_GRAVITY_K * std::sin(pos - M1_GRAVITY_PHASE_RAD);
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				dm_->control_mit(*s.motor, 0.0f, M1_EMERGENCY_BRAKE_KD, 0.0f, 0.0f, brake_tau_ff);
			}
			// 把 ramp 參考點重新錨定到真實位置，否則煞車結束後命令會突然「追進度」。
			// 與 feedback_loop 的 `s->move_cur = real_pos_now` 同樣理由。
			cur_cmd = pos;
			std::cerr << "[" << s.name << " go_home SAFETY] vel=" << vel
				<< " rad/s exceeds " << M1_VEL_SAFETY_LIMIT
				<< " — emergency brake (kd=" << M1_EMERGENCY_BRAKE_KD
				<< ", tau_ff=" << brake_tau_ff << ") engaged, pos=" << pos << "\n";
		} else {
			std::lock_guard<std::mutex> lk(motor_mutex_);
			dm_->control_mit(*s.motor,
				kp, kd,
				cur_cmd, 0.0f, tau_ff);
		}

		// [2026-08-18] Continuous passive watchdog for the ramp — this was a
		// blind spot. feedback_loop() grew a mid-motion passive watchdog on
		// 2026-08-17b, but go_home_slot() sets s.enabled=false at entry, so
		// feedback_loop `continue`s past M1 and that watchdog is inactive for
		// this entire ramp (473 loops / 9.5s on the bench PARK run). The only
		// passive check here was the one-shot probe before the ramp starts, so a
		// motor that went passive MID-ramp was never noticed.
		// Bench evidence it actually happens: PARK stalled at pos=0.2180 while
		// commanding 5.26 Nm toward home (kp 26 x 0.168 err, plus -0.889 tau_ff).
		// Solving 20.87*sin(pos-3.317) = friction puts a 1 Nm static friction
		// equilibrium at pos=0.223 — i.e. the arm was resting where only ~1 Nm
		// holds it (matches the user's "slight resistance" by hand), which is
		// only possible if the motor's real output was ~0.
		if (s.id == MotorSlot::SlotId::M1) {
			if (passive_cooldown > 0) --passive_cooldown;
			float live_pos = s.motor->Get_Position();
			float live_tau = s.motor->Get_tau();
			if (passive_cooldown == 0
					&& std::abs(cur_cmd - live_pos) > PASSIVE_ERR_TOL
					&& std::abs(live_tau) < PASSIVE_TAU_TOL) {
				std::cerr << "[" << s.name << " go_home] passive suspected MID-RAMP (err="
					<< (cur_cmd - live_pos) << " rad, tau=" << live_tau
					<< " Nm < " << PASSIVE_TAU_TOL << ") — re-enabling\n";
				{
					std::lock_guard<std::mutex> lk(motor_mutex_);
					dm_->enable(*s.motor);
				}
				passive_cooldown = 15;   // 15 x 20ms = 300ms; dm_->enable() blocks ~100ms
				++passive_recoveries;
			}
		}

		// [2026-08-18] Throttled tau trace (~4 Hz, mirrors feedback_loop's).
		// PARK previously produced ZERO tau output of any kind: the ramp loop
		// never logged it, and feedback_loop's [M1 tau] line is suppressed here
		// because s.enabled is false. That is why a motor silently producing no
		// torque for 9.5 seconds was invisible in the logs.
		if (s.id == MotorSlot::SlotId::M1 && (i % 12 == 0)) {
			std::cout << "[" << s.name << " go_home] cmd=" << cur_cmd
				<< " pos=" << pos << " vel=" << vel
				<< " tau=" << s.motor->Get_tau()
				<< " tau_ff=" << tau_ff << "\n";
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	std::cout << "[" << s.name << " go_home] ramp " << (ramp_arrived ? "ARRIVED" : "TIMEOUT")
		<< " after " << ramp_loops_used << "/" << MAX_LOOPS << " loops (" << (ramp_loops_used * 20) << "ms)"
		<< " pos=" << s.motor->Get_Position() << " vel=" << s.motor->Get_Velocity()
		<< " target=" << target << std::endl;

	// Ramp complete (cur_cmd==target). Wait for physical motor to converge before releasing.
	// Without this, callers see move done but motor still coasting toward target.
	if (arrive_cnt < ARRIVE_CNT) {
		arrive_cnt = 0;
		// [2026-07-27 diagnostic, per user] same ARRIVED-vs-TIMEOUT log for the
		// settle loop — if the ramp above already TIMEOUT'd without moving, this
		// settle loop will almost certainly also TIMEOUT for the same reason.
		int settle_loops_used = 0;
		bool settle_arrived = false;
		for (int j = 0; j < MAX_SETTLE; ++j) {
			settle_loops_used = j + 1;
			float pos = s.motor->Get_Position();
			if (std::abs(pos - target) < ARRIVE_TOL) ++arrive_cnt; else arrive_cnt = 0;
			// [2026-08-18] Was `arrive_cnt >= ARRIVE_CNT || |vel| < VEL_TOL`.
			// The velocity half stood alone, and a stalled motor's velocity is
			// low too — so this loop exited on its very first iteration whenever
			// the arm was stuck (bench: "settle DONE after 1/100 loops (20ms)"
			// at pos=0.2180 vs target=0.0500). Its whole purpose is to wait for
			// the motor to actually converge, so convergence must be judged on
			// POSITION. arrive_cnt already encodes "position within ARRIVE_TOL
			// for N consecutive samples", which also implies it has settled.
			if (arrive_cnt >= ARRIVE_CNT) { settle_arrived = true; break; }
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				// [2026-08-17] settle 階段原本 tau_ff 硬傳 0，等於 PARK 收尾這段
				// 完全裸奔——而這正好是「主 ramp 已經跑完、手臂還沒真的到底」的
				// 最後一哩，重力力臂仍在，卻只剩 kp 硬頂。跟主 ramp 迴圈補回同一
				// 組模型（門檻 M1_GRAVITY_MIN_VALID_RAD 已於 2026-08-17 從 0.55
				// 放寬到 0.20，這段低角度區間現在才真的拿得到補償）。
				float settle_tau_ff = 0.0f;
				if (s.id == MotorSlot::SlotId::M1 && pos > M1_GRAVITY_MIN_VALID_RAD)
					settle_tau_ff = M1_GRAVITY_K * std::sin(pos - M1_GRAVITY_PHASE_RAD);

				// [2026-08-18 per user] Friction compensation — settle was the one
				// place that never got it (ramp and feedback_loop's MOVE both do), so
				// the moment the ramp ended that push simply vanished. Bench: two
				// consecutive PARKs stalled here at pos=0.1040 / 0.1165 and ran the
				// full 100-loop settle without moving, ending in NOT HOME. At those
				// angles kp alone only produces 26 x 0.054 = 1.40 Nm, well under the
				// ~2.3 Nm static friction measured in the low-angle region. Adding
				// the same faded term brings it to ~3.9 Nm, which clears it.
				// The DEADBAND is what stops this from oscillating around target:
				// inside 0.02 rad the term switches off, so the arm settles instead
				// of being pushed back and forth across the setpoint.
				if (s.id == MotorSlot::SlotId::M1) {
					float err_settle = target - pos;
					if (std::abs(err_settle) > M1_FRICTION_DEADBAND_RAD) {
						float fade_s = 1.0f - std::min(
							std::abs(s.motor->Get_Velocity()) / M1_FRICTION_FADE_VEL, 1.0f);
						settle_tau_ff += M1_FRICTION_TAU * fade_s
							* (err_settle > 0.0f ? 1.0f : -1.0f);
					}
				}
				dm_->control_mit(*s.motor, kp, kd, target, 0.0f, settle_tau_ff);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		std::cout << "[" << s.name << " go_home] settle " << (settle_arrived ? "DONE" : "TIMEOUT")
			<< " after " << settle_loops_used << "/" << MAX_SETTLE << " loops (" << (settle_loops_used * 20) << "ms)"
			<< " pos=" << s.motor->Get_Position() << " vel=" << s.motor->Get_Velocity() << std::endl;
	}

	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		s.hold_pos = target;
		s.hold_tau_ff = 0.0f;   // clear stale gravity proxy; prevents lurch on next TOUCHWALL
		s.move_cur = target;
		s.move_target = target;
		s.hold_err_integral = 0.0f;
	}
	s.hold_en = true;    // hold at 0 after HOME, matching calibrate_arm_slot() pattern
	s.move_act = false;
	if (was_enabled) s.enabled = true;

	// [2026-08-18] Report whether we genuinely got there. Note this runs AFTER
	// s.enabled is restored, so from here on feedback_loop() resumes servicing
	// this slot — including its own continuous passive watchdog. That matters
	// for the failure path: as long as the caller does NOT disable the motor,
	// a passive M1 still has a chance of being recovered by feedback_loop.
	const float final_pos = s.motor->Get_Position();
	const bool  reached   = std::abs(final_pos - target) < ARRIVE_TOL;
	if (!reached) {
		std::cerr << "[" << s.name << " go_home] NOT HOME: pos=" << final_pos
			<< " target=" << target << " (short by " << std::abs(final_pos - target)
			<< " rad)";
		if (passive_recoveries > 0)
			std::cerr << "; passive re-enable fired " << passive_recoveries << "x during ramp";
		std::cerr << "\n";
	}
	return reached;
}

// ============================================================
//  lr_calibrate_slot()  -- seek mechanical stop, back off ZERO_OFFSET, set zero
//  M2 only (called from dispatch_motor after id check)
//  Returns true = genuinely calibrated (stop found + Phase 2 converged);
//  false = MAX_TRAVEL exceeded / Phase 1 timeout (stop never found) / Phase 2
//  never converged even after retry (zero point is shifted from the real stop).
//  [2026-07-27 per user] Was void — cmd_init_sequence() blindly returned "OK"
//  even when M2 completely failed to calibrate (bench: Phase 1 timeout,
//  position frozen for the full 75-loop seek, twice in a row).
// ============================================================
bool DamiaoAPI::lr_calibrate_slot(MotorSlot& s, bool seek_left)
{
	// [2026-08-13 per user] dir/max_travel/max_loops 不再是 const：Phase 1B
	// 要反向重跑同一段 seek 邏輯（往另一邊找第二個機械停點），故改成可變。
	float dir = seek_left ? 1.0f : -1.0f;
	// [2026-06-09j] SEEK_KP 0.3 → 3.0: M2 calibration Phase 1 經常誤觸發
	// "Resistance at stop" (pos 幾乎沒動就判 stop)，原因是 kp=0.3 算出的 tau
	// (~0.6 Nm) 不夠克服 static friction → 馬達靜止 → 初始 PD transient tau
	// 被誤認為「碰到 stop」。拉高 kp 讓馬達真的能推到 mechanical stop。
	// [2026-06-09k] 3.0 太強撞 stop tau 飆 8 Nm，再降到 1.5 + kd 提高到 1.0
	// (對齊 M1 tuning)、撞擊較柔和。
	// [2026-06-09l] 1.5 仍有甩頭感、撞擊 ~4 Nm，再降到 1.0 完全對齊 M1。
	// [2026-08-13 per user] 刷頭變重後某個方向常常一開始就被判定「撞牆」（位移
	// 只有 0.001-0.04 rad），user 已目視/手揸確認該方向沒有實體卡住。查證後發現：
	// setpt 是每圈用目前位置重算的移動式追蹤點（cur_pos+dir*SEEK_RANGE），所以
	// commanded 的位置誤差幾乎恆等於 SEEK_RANGE=2.8，tau≈kp*2.8 基本是常數、跟
	// 馬達是否真的卡住幾乎無關；真正的判斷依據幾乎只剩速度那一項（STOP_CNT 連
	// 續 60ms 低速）。單純加大 SEEK_KP 有歷史風險（3.0 撞擊 tau 飆 8Nm、1.5 仍有
	// 甩頭感），故只小步加到 1.3；同時拉長 STOP_CNT（60ms→200ms）讓瞬間的
	// stick-slip 不會被誤判成撞牆——這個不影響撞擊力道，風險比單純加扭力低。
	const float SEEK_KP = 1.3f;
	const float SEEK_KD = 1.0f;
	const float SEEK_RANGE = 2.8f;
	const float STOP_VEL = 0.04f;
	const float RESIST_TAU = 0.6f;
	const int   STOP_CNT = 10;
	// [2026-08-14 per user] Phase 1 used to get a tighter budget (1.5 rad / 75
	// loops = 1.5s) than Phase 1B (2.0 rad / 200 loops = 4s), on the assumption
	// Phase 1 only travels from wherever M2 happens to start to the NEAREST
	// stop. But the starting position is itself unreliable (zero-reference
	// drift across attempts), so Phase 1 sometimes needs just as much room as
	// Phase 1B — bench saw Phase 1 timeout after traveling 1.17 rad while still
	// moving at 0.36 rad/s, simply out of time, not because it hit anything.
	// Give both seeks the same generous budget from the start.
	float max_travel = 2.0f * ZERO_OFFSET + 0.4f;
	int   seek_max_loops = 200;   // renamed to avoid shadowing run_phase2_attempt's own `max_loops` param
	const float HOLD_KP = 2.0f;
	const float HOLD_KD = 1.0f;
	const float HOME_KP = 1.2f;
	const float HOME_KD = 0.3f;
	const float HOME_TOL = 0.1f;
	const int   HOME_CNT = 10;
	const int   MAX_HOME_LOOPS = 100;

	// Clear hold/move before disabling so feedback_loop won't fire a hold frame
	// in the window between here and enabled.exchange(false).
	s.hold_en = false;
	s.move_act = false;
	bool was_enabled = s.enabled.exchange(false);

	// Pause the OTHER slot's feedback_loop during seek to eliminate the serial-port
	// receive race: when both motors are active on the same CAN bus, control_mit()
	// receive() for M2 can land on a queued M1 response (2ms timeout too short),
	// leaving state_q stale for the entire seek. Pausing M1 is safe here because
	// M1 is at VERTICAL_OFFSET_RAD where gravity torque ≈ 0 (ARM_MASS_KG = 0).
	MotorSlot& peer = (&s == &m1_) ? m2_ : m1_;
	bool was_peer = peer.enabled.exchange(false);

	// ---- Phase 1: seek stop -------------------------------------------------
	std::cout << "[" << s.name << " calibrate] Phase 1: seeking "
		<< (seek_left ? "left" : "right")
		<< " stop...  cur_pos=" << s.motor->Get_Position() << "\n";

	float p_start = s.motor->Get_Position();
	float p_initial = p_start;   // saved to detect if pre-check moved the motor at all
	int   stop_count = 0, loop_count = 0;
	float p_stop = p_start;
	float vel_capture = 0.0f;
	float prev_pos = p_start;
	int   stale_cnt = 0;   // consecutive loops where pos change < 0.002 rad
	bool  ever_moved = false;   // guard: stop detection only after motor actually moved

	// Pre-check: DM4340_48V sometimes acknowledges MIT frames (state_q updates,
	// CMD=0x11 returns) but applies zero torque after DISABLE→ENABLE — the motor
	// hardware needs a few real-error frames to fully activate its torque loop.
	// Detect by sending 3 seek-force frames and sampling tau. If tau < 0.3 Nm
	// (working state: ~1.95 Nm commanded; bad state: < 0.08 Nm noise), re-enable.
	// peer.enabled is already false here, so dm_->enable() is serial-race-free.
	{
		const float TAU_LIVE_THRESHOLD = 0.3f;
		float setpt = p_start + dir * SEEK_RANGE;

		// ---- DIAG: pre-check 开始 ----
		float pre_p_start = p_start;
		std::cout << "[" << s.name << " calibrate DIAG] pre-check start"
			<< " p_start=" << std::fixed << std::setprecision(4) << p_start
			<< " dir=" << dir << " setpt=" << setpt << "\n";

		for (int k = 0; k < 3; ++k) {
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				dm_->control_mit(*s.motor, SEEK_KP, SEEK_KD, setpt, 0.0f, 0.0f);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));

			// ---- DIAG: 每帧记录 ----
			float pre_pos = s.motor->Get_Position();
			float pre_vel = s.motor->Get_Velocity();
			float pre_tau = s.motor->Get_tau();
			std::cout << "[" << s.name << " calibrate DIAG] pre-check frame " << k
				<< " pos=" << std::fixed << std::setprecision(4) << pre_pos
				<< " vel=" << pre_vel << " tau=" << pre_tau;
			if (std::abs(pre_tau) >= TAU_LIVE_THRESHOLD)
				std::cout << " (LIVE)";
			else
				std::cout << " (DEAD)";
			std::cout << "\n";
		}

		float tau_after_3 = s.motor->Get_tau();
		std::cout << "[" << s.name << " calibrate DIAG] pre-check 3-frame tau="
			<< std::fixed << std::setprecision(4) << tau_after_3
			<< " threshold=" << TAU_LIVE_THRESHOLD
			<< " -> " << (std::abs(tau_after_3) < TAU_LIVE_THRESHOLD ? "RE-ENABLE" : "OK")
			<< "\n";

		if (std::abs(s.motor->Get_tau()) < TAU_LIVE_THRESHOLD) {
			std::cerr << "[" << s.name << " calibrate] motor passive after enable"
				" (tau=" << s.motor->Get_tau() << " Nm), re-enabling\n";
			dm_->enable(*s.motor);

			// ---- DIAG: re-enable 后的 10 帧 ----
			std::cout << "[" << s.name << " calibrate DIAG] re-enable start, 10 warmup frames\n";

			for (int k = 0; k < 10; ++k) {
				{
					std::lock_guard<std::mutex> lk(motor_mutex_);
					dm_->control_mit(*s.motor, SEEK_KP, SEEK_KD, setpt, 0.0f, 0.0f);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(20));

				float re_pos = s.motor->Get_Position();
				float re_vel = s.motor->Get_Velocity();
				float re_tau = s.motor->Get_tau();
				std::cout << "[" << s.name << " calibrate DIAG] re-enable frame " << k
					<< " pos=" << std::fixed << std::setprecision(4) << re_pos
					<< " vel=" << re_vel << " tau=" << re_tau << "\n";
			}
		}
		// Update starting references: pre-check frames may have moved the motor.
		p_start = s.motor->Get_Position();

		std::cout << "[" << s.name << " calibrate DIAG] pre-check done"
			<< " p_start was=" << std::fixed << std::setprecision(4) << pre_p_start
			<< " p_start now=" << p_start
			<< " delta=" << (p_start - pre_p_start) << "\n";

		// Post-check: if the motor barely moved during pre-check yet shows high resistance,
		// it likely drifted near the stop while disabled and the pre-check drove it there
		// faster than CAN could update. Send settle frames so Get_Position() reflects the
		// actual stop location before Phase 1 loop uses p_start.
		if ((std::abs(p_start - p_initial) < 0.05f)
				&& (std::abs(s.motor->Get_Velocity()) < STOP_VEL)
				&& (std::abs(s.motor->Get_tau())      > RESIST_TAU)) {
			std::cout << "[" << s.name << " calibrate] pre-check: already at stop"
				<< "  tau=" << s.motor->Get_tau()
				<< "  stale_pos=" << p_start << "  refreshing...\n";
			float settle_setpt = p_start + dir * SEEK_RANGE;
			for (int j = 0; j < 5; ++j) {
				{
					std::lock_guard<std::mutex> lk(motor_mutex_);
					dm_->control_mit(*s.motor, SEEK_KP, SEEK_KD, settle_setpt, 0.0f, 0.0f);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			p_start = s.motor->Get_Position();
			std::cout << "[" << s.name << " calibrate] p_start refreshed: " << p_start << "\n";
		}

		p_stop   = p_start;
		prev_pos = p_start;
		stale_cnt = 0;
	}

	// [2026-08-13 per user] 原本只往一邊找一次機械停點，另一邊靠 ZERO_OFFSET
	// 對稱假設推算 zero——但實機重複出現 Phase 2 收斂失敗、offset 卡在
	// ~0.3-0.4 rad（跟假設值差很多），懷疑刷頭變重後真實可動範圍已經跟假設
	// 的 0.8+0.8=1.6 rad 不一樣。改成兩邊都真的撞一次機械停點，用實測範圍
	// 取代假設值。把整段 seek 邏輯包成 lambda，Phase 1 / Phase 1B 各呼叫一次
	// （方向相反、Phase 1B 的 max_travel/max_loops 加大以橫跨整個機構）。
	auto run_seek = [&](const char* label) -> bool {
		p_start   = s.motor->Get_Position();
		stop_count = 0; loop_count = 0;
		p_stop    = p_start;
		vel_capture = 0.0f;
		prev_pos  = p_start;
		stale_cnt = 0;
		ever_moved = false;

		while (loop_count < seek_max_loops) {
			float cur_pos = s.motor->Get_Position();
			float cur_vel = s.motor->Get_Velocity();
			float cur_tau = s.motor->Get_tau();

			if (std::abs(cur_vel) > 0.10f) ever_moved = true;

			// stale-read detector: pos frozen → likely CAN receive() not updating state_q
			if (std::abs(cur_pos - prev_pos) < 0.002f) ++stale_cnt; else stale_cnt = 0;
			prev_pos = cur_pos;
			if (stale_cnt == 10)
				std::cerr << "[" << s.name << " calibrate] " << label << " WARN: pos frozen at "
					<< std::fixed << std::setprecision(4) << cur_pos
					<< " for 10 loops  vel=" << cur_vel << "  tau=" << cur_tau
					<< "  (possible stale CAN reads or motor not responding)\n";

			if (dir * (cur_pos - p_start) >= max_travel) {
				vel_capture = 0.0f;
				p_stop = cur_pos;
				{
					std::lock_guard<std::mutex> lk(motor_mutex_);
					dm_->control_mit(*s.motor, HOLD_KP, HOLD_KD, p_stop, 0.0f, 0.0f);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				std::cerr << "[" << s.name << " calibrate] " << label << " max_travel exceeded at pos="
					<< cur_pos << ", aborted\n";
				return false;
			}

			bool resisting = (ever_moved || loop_count >= 5) /* ≈100ms without movement */
				&& (std::abs(cur_vel) < STOP_VEL)
				&& (std::abs(cur_tau) > RESIST_TAU);
			if (resisting) {
				if (stop_count == 0) vel_capture = std::abs(cur_vel);
				++stop_count;
			}
			else {
				if (stop_count > 0) {   // was detecting resistance but lost it — print breakdown
					std::cerr << "[" << s.name << " calibrate] resist lost"
						<< std::fixed << std::setprecision(4)
						<< "  vel=" << cur_vel
						<< (std::abs(cur_vel) < STOP_VEL ? "(OK)" : "(FAIL-too-fast)")
						<< "  tau=" << cur_tau
						<< (std::abs(cur_tau) > RESIST_TAU ? "(OK)" : "(FAIL-too-low)")
						<< "  prev_cnt=" << stop_count << "\n";
				}
				stop_count = 0;
				vel_capture = 0.0f;
			}

			if (stop_count >= STOP_CNT) {
				p_stop = cur_pos;
				{
					std::lock_guard<std::mutex> lk(motor_mutex_);
					dm_->control_mit(*s.motor, HOLD_KP, HOLD_KD, p_stop, 0.0f, 0.0f);
				}
				// No sleep here — any gap ≥ ~100 ms triggers Damiao MIT watchdog and
				// the motor stops responding to frames.
				std::cout << "[" << s.name << " calibrate] " << label << " Resistance at pos=" << cur_pos
					<< "  tau=" << cur_tau << "\n";
				return true;
			}

			const float SEEK_VEL_MAX = 0.4f;   // [2026-06-09l] 0.6→0.4 進一步減撞擊力
			float eff_kp = (std::abs(cur_vel) > SEEK_VEL_MAX)
				? (SEEK_KP * SEEK_VEL_MAX / std::abs(cur_vel)) : SEEK_KP;
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				dm_->control_mit(*s.motor, eff_kp, SEEK_KD,
					cur_pos + dir * SEEK_RANGE, 0.0f, 0.0f);
			}

			if (loop_count % 10 == 0)
				std::cout << "[" << s.name << " calibrate] " << label << " seek"
					<< "  lp=" << (loop_count + 1)
					<< "  pos=" << std::fixed << std::setprecision(4) << cur_pos
					<< "  vel=" << cur_vel
					<< "  tau=" << cur_tau
					<< "  stop_cnt=" << stop_count
					<< "  eff_kp=" << eff_kp
					<< "  setpt=" << (cur_pos + dir * SEEK_RANGE) << "\n";

			++loop_count;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}

		// timeout
		p_stop = s.motor->Get_Position();
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			dm_->control_mit(*s.motor, HOLD_KP, HOLD_KD, p_stop, 0.0f, 0.0f);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		std::cerr << "[" << s.name << " calibrate] " << label << " timeout"
			<< std::fixed << std::setprecision(4)
			<< "  final_pos=" << p_stop
			<< "  disp=" << (p_stop - p_start)
			<< "  vel=" << s.motor->Get_Velocity()
			<< "  tau=" << s.motor->Get_tau()
			<< "  stale_cnt=" << stale_cnt
			<< "  loops=" << loop_count
			<< "  dir=" << dir << "\n";
		return false;
	};

	if (!run_seek("Phase 1")) {
		s.hold_pos = p_stop;
		s.hold_en = true;
		s.move_act = false;
		if (was_enabled) s.enabled = true;
		if (was_peer) peer.enabled = true;
		return false;
	}
	float p_stop_1 = p_stop;
	std::cout << "[" << s.name << " calibrate] Phase 1 stop at pos=" << p_stop_1
		<< "  legacy target zero (assumed sym.)=" << (p_stop_1 - dir * ZERO_OFFSET) << "\n";

	// ---- Phase 1B: seek the OPPOSITE mechanical stop ------------------------
	// max_travel/seek_max_loops already at their wide values from the top of
	// the function (both seeks now share the same generous budget) — only the
	// direction needs flipping here.
	dir = -dir;
	if (!run_seek("Phase 1B")) {
		s.hold_pos = p_stop;
		s.hold_en = true;
		s.move_act = false;
		if (was_enabled) s.enabled = true;
		if (was_peer) peer.enabled = true;
		return false;
	}
	float p_stop_2 = p_stop;
	std::cout << "[" << s.name << " calibrate] Phase 1B stop at pos=" << p_stop_2 << "\n";

	float half_range = std::abs(p_stop_1 - p_stop_2) / 2.0f;
	float midpoint    = (p_stop_1 + p_stop_2) / 2.0f;
	std::cout << "[" << s.name << " calibrate] measured half_range=" << half_range
		<< " rad (assumed ZERO_OFFSET=" << ZERO_OFFSET << ")  midpoint=" << midpoint << "\n";

	// [2026-08-13 per user] Plausibility guard. Bench found both seeks
	// occasionally false-triggering "Resistance" after only ~0.02 rad of
	// travel (transient torque right after the pre-check hand-off, not a real
	// mechanical stop) — confirmed impossible because M2 had already reached
	// real slot positions ~±0.5 rad away in a prior successful DEPLOY. A tiny
	// half_range doesn't just mean "range shrank" — it can flip LEFT/RIGHT
	// target signs entirely in lr_move_to_slot_impl (half_range smaller than
	// the slot margin), which is worse than the old wrong-but-stable
	// ZERO_OFFSET assumption. Reject outright rather than trusting it.
	const float MIN_PLAUSIBLE_HALF_RANGE = 0.3f;
	if (half_range < MIN_PLAUSIBLE_HALF_RANGE) {
		std::cerr << "[" << s.name << " calibrate] FAIL implausible half_range="
			<< half_range << " rad (< " << MIN_PLAUSIBLE_HALF_RANGE << " rad floor)"
			" — Phase 1/1B almost certainly false-triggered on a transient, not a"
			" real mechanical stop. NOT updating lr_half_range (keeping previous"
			" value); LEFT/RIGHT slot targets unaffected by this run. Retry"
			" LR_CALIBRATE, or investigate the resistance-detection false positive"
			" (STOP_CNT/RESIST_TAU/STOP_VEL) if it keeps happening.\n";
		s.hold_pos = p_stop_2;
		s.hold_en = true;
		s.move_act = false;
		if (was_enabled) s.enabled = true;
		if (was_peer) peer.enabled = true;
		return false;
	}

	if (std::abs(half_range - ZERO_OFFSET) > 0.15f) {
		std::cerr << "[" << s.name << " calibrate] WARN measured half_range differs from"
			" assumed ZERO_OFFSET by " << std::abs(half_range - ZERO_OFFSET)
			<< " rad — mechanism range changed from assumption; LEFT/RIGHT slot"
			" targets will use the measured value, not ZERO_OFFSET\n";
	}

	// ---- Phase 2: move to the measured midpoint -----------------------------
	// [2026-07-27 per user] Tracked outside the block below so the function's
	// return value can reflect whether Phase 2 actually converged — previously
	// this fell through to Phase 3/set_zero and printed "Done." regardless,
	// silently accepting a shifted zero point (see the WARN below) as success.
	bool phase2_ok = true;
	std::cout << "[" << s.name << " calibrate] Phase 2: moving to measured midpoint...\n";
	float target = midpoint;

	// Direct MIT with moving setpoint: s.enabled stays false (set in Phase 1) so
	// feedback_loop never interferes. P2_KP=8 matches hold_kp so the motor arrives
	// at target gently and is already settled when set_zero is called — prevents
	// residual velocity from creating hold-coordinate mismatch (hard rightward rush).
	{
		// [2026-07-24 per user] 21.0/36.0 那次實機出現 Phase 1 timeout + M2 暴衝，
		// user 要求整個退回上一版 → 18.0/33.0。
		const float P2_KP    = 18.0f;
		const float P2_SPEED = 0.6f;
		const float CONV_TOL = 0.06f;
		const float DT       = 0.02f;
		// [2026-06-06] Fix 3: retry parameters when first attempt doesn't converge.
		const float P2_KP_RETRY    = 33.0f;   // stronger push (was 20 — still under-converged)
		const float CONV_TOL_RETRY = 0.15f;   // bench-realistic for M2 friction
		const float VEL_SETTLE_THRESHOLD = 0.05f;   // before set_zero
		const int   VEL_SETTLE_MAX_LOOPS = 50;      // = 500ms (was 200ms)
		const float VEL_SETTLE_KP = 30.0f;          // hold-hard during settle, no damping below
		// [2026-08-13 per user] settle loop 之前沿用 CONV_TOL_RETRY(0.15) 當作
		// 「夠接近 target」的門檻，太寬鬆——兩次實機校正都固定少走 ~0.07 rad 就
		// 提早收工（vel 已經夠低，但位置還沒真的到），造成 set_zero 抓到的零點
		// 固定偏移。收緊成專用的 0.03 rad，逼 settle 迴圈真的推到位。
		const float VEL_SETTLE_POS_TOL = 0.03f;

		auto run_phase2_attempt = [&](float kp, float tol, int max_loops) -> bool {
			float cur_cmd_local;
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				cur_cmd_local = s.motor->Get_Position();
			}
			for (int j = 0; j < max_loops; ++j) {
				float diff = target - cur_cmd_local;
				float step = P2_SPEED * DT;
				if (std::abs(diff) <= step) cur_cmd_local = target;
				else cur_cmd_local += (diff > 0.0f ? step : -step);

				{
					std::lock_guard<std::mutex> lk(motor_mutex_);
					dm_->control_mit(*s.motor, kp, s.hold_kd, cur_cmd_local, 0.0f, 0.0f);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(20));

				if (cur_cmd_local == target &&
					std::abs(s.motor->Get_Position() - target) < tol) return true;
			}
			return false;
		};

		// First attempt (3s @ kp=12.5)
		bool converged = run_phase2_attempt(P2_KP, CONV_TOL, 150);
		float final_pos = s.motor->Get_Position();
		float offset    = std::abs(final_pos - target);

		if (converged) {
			std::cout << "[" << s.name << " calibrate] Phase 2 converged at pos="
				<< final_pos << " (target=" << target << " offset="
				<< offset << " rad)\n";
		} else {
			// [Fix 3] Retry once with stronger kp + relaxed tol (still 1.5s).
			std::cerr << "[" << s.name << " calibrate] Phase 2 first attempt"
				" did NOT converge, pos=" << final_pos << " target=" << target
				<< " offset=" << offset << " rad — retry with kp=" << P2_KP_RETRY << "\n";
			converged = run_phase2_attempt(P2_KP_RETRY, CONV_TOL_RETRY, 75);
			final_pos = s.motor->Get_Position();
			offset    = std::abs(final_pos - target);
			if (converged) {
				std::cout << "[" << s.name << " calibrate] Phase 2 retry converged at pos="
					<< final_pos << " (target=" << target << " offset="
					<< offset << " rad)\n";
			} else {
				std::cerr << "[" << s.name << " calibrate] WARN Phase 2 NOT converged"
					" after retry, pos=" << final_pos << " target=" << target
					<< " offset=" << offset << " rad — set_zero will use current"
					" stable pos (zero will be shifted; LEFT/RIGHT slots will be"
					" off by ~" << offset << " rad — physical inspection of M2"
					" mechanism advised if offset > 0.10)\n";
				phase2_ok = false;
			}
		}

		// [Fix 3] Wait for velocity to settle before falling through to set_zero.
		// 2026-06-06 v2: VEL_SETTLE_KP=30 (no damping below — high kp pushes
		// motor right to target during settle, breaks through stiction. Original
		// vel-settle used s.hold_kd which dampened vel before reaching target →
		// motor stopped 0.1 rad short → set_zero captured the wrong position.
		// Now: high kp + small kd until vel really drops near 0 AT target.
		float settled_vel = 0.0f;
		int settle_loop = 0;
		float settle_pos_final = 0.0f;
		for (; settle_loop < VEL_SETTLE_MAX_LOOPS; ++settle_loop) {
			settled_vel = std::abs(s.motor->Get_Velocity());
			settle_pos_final = s.motor->Get_Position();
			const float settle_err = std::abs(settle_pos_final - target);
			// Done if vel low AND near target (don't accept "vel=0 but stuck halfway")
			if (settled_vel < VEL_SETTLE_THRESHOLD && settle_err < VEL_SETTLE_POS_TOL) break;
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				// Reduced kd (0.5) → less velocity damping → motor stays pushing toward target
				dm_->control_mit(*s.motor, VEL_SETTLE_KP, 0.5f, target, 0.0f, 0.0f);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		std::cout << "[" << s.name << " calibrate] Phase 2 vel-settle: vel="
			<< settled_vel << " loops=" << settle_loop
			<< " final_pos=" << settle_pos_final
			<< " final_err=" << std::abs(settle_pos_final - target) << "\n";
		std::cout << "[" << s.name << " calibrate] Phase 2 settled: pos="
			<< s.motor->Get_Position() << "  target=" << target << "\n";
	}

	// ---- Phase 3: set zero --------------------------------------------------
	// peer.enabled=false since Phase 1; feedback_loop sends no frames to M1 here.
	// M1 hits MIT watchdog (~100ms) and enters passive (zero torque), which is safe
	// because M1 is at VERTICAL_OFFSET_RAD where gravity torque ~= 0.
	// M2 is kept alive by the explicit 5-frame flush loop below.
	s.enabled = false;   // stop feedback_loop from sending stale-coord hold frames during set_zero
	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		dm_->set_zero_position(*s.motor);
		s.hold_pos = 0.0f;
		s.move_cur = 0.0f;
		s.move_target = 0.0f;
		s.hold_err_integral = 0.0f;
	}

	// set_zero response is CMD=0x12 (adapter confirm), which does NOT update state_q.
	// Send 10 MIT frames (200 ms) to get CMD=0x11 responses that refresh Get_Position()
	// and fully absorb any residual velocity before hold engages.
	// kp=0: no positional error force; kd=hold_kd: velocity damping only.
	for (int i = 0; i < 10; ++i) {
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			dm_->control_mit(*s.motor, 0.0f, s.hold_kd, 0.0f, 0.0f, 0.0f);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	// Capture actual settled position as hold target; avoids hold-force mismatch if
	// the motor drifted slightly from the set_zero point during damping frames.
	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		s.hold_pos = s.motor->Get_Position();
	}

	s.hold_en = true;    // hold at settled position to prevent residual drift
	s.move_act = false;
	if (was_enabled) s.enabled = true;
	if (was_peer) peer.enabled = true;

	// [2026-08-13 per user] 儲存實測 half_range，取代 lr_move_to_slot_impl 原本
	// 寫死的 ZERO_OFFSET 假設；即使 Phase 2 沒完全收斂，兩個機械停點本身是真的
	// 撞出來的量測值，仍然比假設值準，所以不 gate 在 phase2_ok 之後才存。
	s.lr_half_range = half_range;
	// [2026-08-14 per user] 只有整個流程真的收斂（phase2_ok）才標記「已校正」，
	// 讓 cmd_init_sequence() 之後可以跳過這段自動尋找；沒收斂就不標記，保留
	// 讓下次 INIT 還是會嘗試重新自動校正（也可能仍然不可靠，但至少不會誤以為
	// 目前這組沒收斂好的值是可信的）。
	s.lr_calibrated = phase2_ok;

	std::cout << "[" << s.name << " calibrate] Done. Measured half_range=~" << half_range
		<< " rad (was assumed ZERO_OFFSET=" << ZERO_OFFSET << ")."
		<< (phase2_ok ? "" : " (WARN: zero shifted, see Phase 2 offset above)") << "\n";
	return phase2_ok;
}

// ============================================================
//  calibrate_arm_slot()  -- M1: push negative to stop, set zero (no back-off)
// ============================================================
bool DamiaoAPI::calibrate_arm_slot(MotorSlot& s)
{
	const float dir = -1.0f;   // M1 calibrates toward negative stop
	const float SEEK_KP = 1.0f;
	const float SEEK_KD = 1.0f;
	const float SEEK_RANGE = 3.0f;
	const float STOP_VEL = 0.04f;
	const float RESIST_TAU = 2.5f;
	const int   STOP_CNT = 3;
	const float MAX_TRAVEL = 3.0f;   // M1 has larger travel range
	const int   MAX_LOOPS = 100;    // 2s timeout
	const float HOLD_KP = 5.0f;
	const float HOLD_KD = 1.0f;

	bool was_enabled = s.enabled.exchange(false);

	// Pause peer: same serial-bus race as lr_calibrate_slot. M2 is at a horizontal
	// slot position (gravity ~0), safe to pause for calibrate duration (~400 ms).
	MotorSlot& peer = (&s == &m1_) ? m2_ : m1_;
	bool was_peer = peer.enabled.exchange(false);

	// [2026-06-06] Fix 1+2+5: pre-check for "already at stop" condition.
	//   If pos is already near 0 (≤ 0.05 rad) AND velocity is steady (≤ STOP_VEL)
	//   for 3 consecutive samples, M1 is physically at the negative stop already
	//   — running Phase 1 would just slam the motor uselessly + repeatedly trigger
	//   damiao thermal/over-current latch (the "switchControlMode failed" we saw).
	//   Detection logic must NOT require tau threshold because the motor often
	//   refuses to apply torque cleanly to a stalled position (the failed-INIT
	//   case had `ever_moved=0 last_vel=-0.006` with low tau — clearly stuck but
	//   not detected by the resistance check).
	//   Skip path: jump straight to Phase 2 (set_zero at current pos).
	{
		const float AT_STOP_POS_THRESHOLD = 0.05f;
		const float AT_STOP_VEL_THRESHOLD = 0.04f;   // = STOP_VEL
		int stable_cnt = 0;
		float pos_sample = 0.0f, vel_sample = 0.0f;
		for (int k = 0; k < 3; ++k) {
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				// Light hold frame to keep CAN comm alive but no aggressive push.
				dm_->control_mit(*s.motor, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(40));
			pos_sample = s.motor->Get_Position();
			vel_sample = s.motor->Get_Velocity();
			if (std::abs(pos_sample) < AT_STOP_POS_THRESHOLD &&
			    std::abs(vel_sample) < AT_STOP_VEL_THRESHOLD) {
				++stable_cnt;
			} else {
				stable_cnt = 0;
			}
		}
		if (stable_cnt >= 3) {
			std::cout << "[" << s.name << " calibrate] pre-check: already at stop"
				<< "  pos=" << std::fixed << std::setprecision(4) << pos_sample
				<< "  vel=" << vel_sample
				<< " — skipping Phase 1 seek, going straight to set_zero\n";
			// Goto Phase 2 (set_zero) — wrap with a label to avoid restructuring.
			goto m1_skip_to_set_zero;
		}
	}

	// ---- Phase 1: seek negative stop ----------------------------------------
	{
	std::cout << "[" << s.name << " calibrate] Phase 1: seeking negative stop"
		<< "  cur_pos=" << s.motor->Get_Position() << "\n";

	float p_start = s.motor->Get_Position();
	int   stop_count = 0, loop_count = 0;
	float p_stop = p_start;
	bool  ever_moved = false;   // guard: stop detection only after motor actually moved
	float last_vel = 0.0f;

	while (loop_count < MAX_LOOPS) {
		float cur_pos = s.motor->Get_Position();   // safe: s.enabled=false, no feedback_loop writes
		float cur_vel = s.motor->Get_Velocity();
		float cur_tau = s.motor->Get_tau();
		last_vel = cur_vel;

		if (std::abs(cur_vel) > 0.15f) ever_moved = true;

		if (dir * (cur_pos - p_start) >= MAX_TRAVEL) {
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				dm_->control_mit(*s.motor, HOLD_KP, HOLD_KD, cur_pos, 0.0f, 0.0f);
				s.hold_pos = cur_pos;
				s.hold_err_integral = 0.0f;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			std::cerr << "[" << s.name << " calibrate] MAX_TRAVEL exceeded at pos="
				<< cur_pos << ", set_zero aborted\n";
			s.hold_en = true;
			s.move_act = false;
			if (was_enabled) s.enabled = true;
			if (was_peer) peer.enabled = true;
			return false;
		}

		bool resisting = (ever_moved || loop_count >= 5) /* ≈120ms without movement */
			&& (std::abs(cur_vel) < STOP_VEL)
			&& (std::abs(cur_tau) > RESIST_TAU);
		if (resisting) ++stop_count; else stop_count = 0;

		if (stop_count >= STOP_CNT) {
			p_stop = cur_pos;
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				dm_->control_mit(*s.motor, HOLD_KP, HOLD_KD, p_stop, 0.0f, 0.0f);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			std::cout << "[" << s.name << " calibrate] Resistance at pos=" << cur_pos
				<< "  tau=" << cur_tau << "\n";
			break;
		}

		const float SEEK_VEL_MAX = 0.5f;
		float eff_kp = (std::abs(cur_vel) > SEEK_VEL_MAX)
			? (SEEK_KP * SEEK_VEL_MAX / std::abs(cur_vel)) : SEEK_KP;
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			dm_->control_mit(*s.motor, eff_kp, SEEK_KD,
				cur_pos + dir * SEEK_RANGE, 0.0f, 0.0f);
		}
		++loop_count;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	if (loop_count >= MAX_LOOPS) {
		float pos_now = s.motor->Get_Position();
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			dm_->control_mit(*s.motor, HOLD_KP, HOLD_KD, pos_now, 0.0f, 0.0f);
			s.hold_pos = pos_now;
			s.hold_err_integral = 0.0f;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		// [2026-06-06] Fix 5: if M1 didn't move at all AND ended near 0, treat as
		// "already at stop" (the pre-check missed this case because pos drifted
		// just past 0.05 threshold) instead of aborting. ever_moved=0 +
		// pos≈0 = clearly stuck on stop, not a true seek failure.
		if (!ever_moved && std::abs(pos_now) < 0.10f) {
			std::cout << "[" << s.name << " calibrate] TIMEOUT but pos="
				<< pos_now << " ever_moved=0 → treating as already-at-stop,"
				" proceeding to set_zero (Fix 5 fallback)\n";
			goto m1_skip_to_set_zero_from_phase1;
		}
		std::cerr << "[" << s.name << " calibrate] TIMEOUT — stop not found at pos="
			<< pos_now << " ever_moved=" << ever_moved
			<< " last_vel=" << last_vel << ", set_zero aborted\n";
		s.hold_en = true;
		s.move_act = false;
		if (was_enabled) s.enabled = true;
		if (was_peer) peer.enabled = true;
		return false;
	}
	}   // close Phase 1 block (opened 2026-06-06 for goto label scoping)

m1_skip_to_set_zero:
m1_skip_to_set_zero_from_phase1:
	// ---- Phase 2: set zero at stop (no back-off for M1) ---------------------
	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		dm_->set_zero_position(*s.motor);
		s.hold_pos = 0.0f;
		s.move_cur = 0.0f;
		s.move_target = 0.0f;
		s.hold_err_integral = 0.0f;
	}

	// set_zero response is CMD=0x12 (does not update state_q). Send 5 MIT frames so
	// CMD=0x11 responses refresh Get_Position() to ~0.0 and prevent the MIT watchdog
	// gap that would release elastic stop energy before feedback_loop engages.
	// kp=0: no position force; kd=hold_kd: velocity damping only.
	for (int i = 0; i < 5; ++i) {
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			dm_->control_mit(*s.motor, 0.0f, s.hold_kd, 0.0f, 0.0f, 0.0f);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	s.hold_en = true;
	s.move_act = false;
	if (was_enabled) s.enabled = true;
	if (was_peer) peer.enabled = true;

	std::cout << "[" << s.name << " calibrate] Done. Mechanical stop = 0 rad. Holding at 0.\n";
	return true;
}

// ============================================================
//  lr_move_to_slot_impl()  -- M2: move to LEFT/CENTER/RIGHT slot
//  Blocking direct-MIT: same moving-setpoint pattern as lr_calibrate Phase 2.
//  kp=15 overcomes stiction; convergence check guarantees arrival before return.
// ============================================================
bool DamiaoAPI::lr_move_to_slot_impl(MotorSlot& s, int slot, float speed_rad_s)
{
	float target;
	// [2026-06-11e] 實驗：LEFT 改回 -0.7 + 拉高 MIT_KP 20 + 加 FRICTION_TAU 1.5
	// 看 motor 能否真的推到 -0.7（測試硬體是否真飽和）
	// [2026-07-24 per user] LEFT 實測不夠過去 → margin 0.1→0.05，target -0.7→-0.75
	// (更靠近 ZERO_OFFSET=0.8 那個 calibrate 量到的 stop，還沒頂到)
	// [2026-08-13 per user] 改用 lr_calibrate_slot 兩邊實測的 s.lr_half_range
	// 取代寫死的 ZERO_OFFSET——如果實際可動範圍比假設的 0.8 小，target 會自動
	// 跟著縮小，不會再往一個量測不到的位置硬推。margin 維持原本 LEFT/RIGHT
	// 不對稱的調法（0.05 / 0.1）。
	if (slot < 0) target = -s.lr_half_range + 0.05f;   // LEFT
	else if (slot > 0) target = s.lr_half_range - 0.1f;   // RIGHT
	else               target = 0.0f;           // CENTER

	// [2026-06-11e] 實驗：MIT_KP 10 → 20、加 FRICTION_TAU 1.5、測 motor 是否真飽和
	// [2026-07-24 per user] 32.0/2.6 那次實機出現 Phase 1 timeout + M2 衝到 2.27 rad
	// 遠超正常範圍、tau 飆到 -21 Nm 的異常暴衝，user 要求整個退回上一版 → 28.0/2.3。
	// [2026-07-27 per user] 要求扭力再稍微加大 — 保守只加到 30.0（沒有直接跳回
	// 32.0，避免重演上面那次暴衝），FRICTION_TAU 沒動。如果還是不夠，下一步再
	// 小步往上調，不要一次跳太多。
	// [2026-08-13b per user] FRICTION_TAU 2.8 實測幾乎沒推動（2s 內只移動
	// ~0.08 rad，比校正 Phase 2 用 kp=33 還小很多），懷疑馬達進入 passive/fault
	// 狀態而非單純扭力不足；user 知悉風險後仍要求再加，這次 MIT_KP/FRICTION_TAU
	// 兩邊都小步加：30.0→31.0 / 2.8→3.0（刻意不跳回 32.0/2.6 那組暴衝配置）
	// [2026-08-13d per user] 頭重很多、要求全面加大。2026-07-24 暴衝（衝到
	// 2.27 rad、tau -21Nm）發生在 MIT_KP=32.0（FRICTION_TAU 當時只有 2.6，
	// 比現在的 3.0 還低）——問題比較像是出在 MIT_KP 逼近/超過 32 這個區間本身，
	// 故 MIT_KP 固定在 31.0 不再往上，只加大 FRICTION_TAU（固定值 feedforward、
	// 不隨 error 放大，風險較低）3.0→4.0
	const float MIT_KP   = 31.0f;
	const float FRICTION_TAU = 4.0f;
	// [2026-08-18 per user] M2 轉動時有「硬轉動的聲音」，但到位正常、目視無異狀。
	// 下面原本是「只要還沒到位就固定補滿 ±FRICTION_TAU」，全程無衰減。馬達動起來
	// 之後動摩擦遠小於靜摩擦，那 4.0 Nm 就變成純過剩推力，機構一路被硬頂——聲音
	// 由此而來（跟隨本身沒問題，因為 MIT_KP=31 夠高，所以目視看不出來）。
	// 這個 4.0 的來歷跟 M1 那批過度調校同源：它是 1.5→2.3→2.8→3.0→4.0 為了
	// 「M2 轉不過去」一路加上來的，但 2026-08-13g 已查明那些轉不過去有一部分
	// 其實是 half_range=0.013 假觸發害 LEFT/RIGHT target 正負互換算錯，不是扭力
	// 不足；target 用手量的 lr_half_range 修對後，這些扭力就過剩了。
	// 改用與 M1 相同的速度衰減（見 main_api.h 的 M1_FRICTION_* 說明）：
	//   fade = 1 - min(|vel| / FRICTION_FADE_VEL, 1)
	// 起動時（vel≈0）仍補滿 4.0，保住原本的破靜摩擦能力；動起來後逐步退場。
	// 0.35 是照 M2 的速度尺度取的——M2 slot 移動命令速度 0.6~0.8 rad/s，比例上
	// 對應 M1 的 FADE_VEL 0.10 / 命令速度 0.15~0.25。
	// 這個機制自帶負回饋，很安全：若中途減速將卡住，vel 下降會讓 fade 回升、
	// 補償自動加回來。
	const float FRICTION_FADE_VEL = 0.35f;
	// [2026-06-09aa] reference tuning: CONV_TOL=0.1, MAX_LOOPS=100 (2s)
	// [2026-06-09bb] CONV_TOL 0.1 → 0.15：motor LEFT 方向實測能到 ±0.5、
	// target -0.6 用 0.1 容忍 (5.7°) 卡很可惜 (差 0.012 rad 就過關)。
	// 0.15 (8.6°) 給夠 margin、一次 converged。sweep 對角度精度要求不高。
	const float CONV_TOL = 0.15f;
	const float DT       = 0.02f;
	// [2026-08-28 per user] MAX_LOOPS 100 → 150（2s → 3s）。
	//
	// bench 症狀：DEPLOY LEFT 反覆失敗、M1 因此完全沒伸出（cmd_deploy_sequence
	// 的 Step 2 一 return，Step 3 的 touch_wall 就不會執行）。兩次 log 的數字幾乎
	// 完全相同：
	//     pos=-0.547227 target=-0.6775 err=0.130273 start=0.508316
	//     pos=-0.541505 target=-0.6775 err=0.135995 start=0.50679
	// 這種重現性不是機械卡住或扭力不足，而是 loop 預算差臨門一腳。實際算一遍
	// （start≈0.507 → target=-0.6775，距離 1.1843 rad）：
	//     巡航段 (1.1843 − M2_CREEP_ZONE) / (0.8 * DT) = 1.0343/0.016 = 64.6 loops
	//     creep 段        M2_CREEP_ZONE / (0.20 * DT) =    0.15/0.004 = 37.5 loops
	//     合計 102.1 loops  >  100
	// 差 2.1 個 loop。注意 err(0.136) 已經 < CONV_TOL(0.15) —— **馬達其實轉到位了**，
	// 只是收斂條件同時要求 `cur_cmd == target`（命令軌跡走完），而命令還差最後
	// 幾個 tick。
	//
	// 根因是 2026-08-18 為了修「M2 甩頭」加入的 creep：它把最後 0.15 rad 的速度
	// 砍到 1/4，光那段就吃掉 37.5 個 loop（預算的三分之一以上），當時沒有同步
	// 放大 MAX_LOOPS。
	//
	// 150 的依據：最壞情況是從 RIGHT 端（+lr_half_range−0.1）走到 LEFT 端
	// （−lr_half_range+0.05），half_range=0.7275 時距離 1.305 rad →
	//     (1.305−0.15)/0.016 + 37.5 = 72.2 + 37.5 = 109.7 loops
	// 取 150 留約 35% 餘裕。真的卡死時代價只是多等 1 秒。
	// ⚠ 日後再改 M2_CREEP_ZONE / M2_CREEP_SPEED / speed_rad_s / lr_half_range，
	//   都要用上面這條算式重算這個值。
	const int   MAX_LOOPS = 150;   // 3s timeout (2026-08-28: 100→150，creep 段吃掉的 loop 沒被算進原本的預算)


	bool was_enabled = s.enabled.exchange(false);

	float cur_cmd;
	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		cur_cmd = s.motor->Get_Position();
	}

	// [2026-06-06] Passive-state detection + re-enable.
	// damiao M2 sometimes latches into passive state after being held against a
	// stop with high torque (observed: DEPLOY LEFT pushes M2 to slot near
	// mechanical stop, holds with kp*err=2.3Nm, M2 over-current/thermal → fault
	// → MIT frames "ACK" but no torque applied → subsequent DEPLOY motor doesn't
	// move at all). Mirrors the pre-check in lr_calibrate_slot.
	// Strategy: send 3 light frames with target far from current pos, sample tau.
	// If tau stays below threshold → motor passive → re-enable.
	{
		const float TAU_LIVE_THRESHOLD = 0.3f;
		// 🔴 [2026-09-02] 偏移量 1.0 → 0.05 rad，並在區塊末尾無條件刷新 cur_cmd。
		//
		// **這兩道 go_home_slot 早在 2026-08-14 就做了，只是沒有擴散到這裡。**
		// 見該函式同名探針的註解：「縮小成 0.05 rad…實際造成的移動量小很多」與
		// 「the probe (whether or not it triggered re-enable) may have moved the
		//   motor; starting the ramp from a stale reference is exactly what
		//   caused the erratic-move bug」。
		//
		// 2026-09-02 現場證據（四次 M2 slot 移動，兩個方向都對得上）：
		//   start=+0.0090 target=0 → 探針設在 -0.991（負向）→ 實際落點 **-0.1444**
		//   start=-0.0013 target=0 → 探針設在 +0.999（正向）→ 實際落點 **+0.1421**
		//   start=-0.1692 target=0 → 探針朝目標 → 落點 -0.0261（正常）
		//   start=+0.1329 target=0 → 探針朝目標 → 落點 +0.0090（正常）
		// **過衝方向與探針方向完全一致**，且只在「幾乎不用動」的短距離發作：
		// 長距離時探針正好朝目標、那 60ms 會被後續軌跡吸收；短距離時目標就在腳邊，
		// 探針把馬達踹到目標另一側，而 creep 段只有 0.20 rad/s（每步 0.004 rad）拉不回來。
		//
		// 🔴 更糟的是它**回報成功**：兩次落點 0.142/0.144 都剛好卡在 CONV_TOL=0.15 內，
		// 所以印的是 `(converged)`。DEPLOY 之後 M2 停在 -0.119、tau≈2.0 由 hold 迴圈硬拉，
		// 根因就在這裡。
		//
		// 為什麼 0.05 仍足以判定 passive：MIT_KP=31 × 0.05 = 1.55 Nm，
		// 遠高於 TAU_LIVE_THRESHOLD(0.3)；活著的馬達必然超過，passive 的仍然是 ~0。
		const float probe_setpt = cur_cmd + (target > cur_cmd ? 0.05f : -0.05f);
		float last_tau = 0.0f;
		for (int k = 0; k < 3; ++k) {
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				dm_->control_mit(*s.motor, MIT_KP, s.hold_kd, probe_setpt, 0.0f, 0.0f);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			last_tau = s.motor->Get_tau();
		}
		if (std::abs(last_tau) < TAU_LIVE_THRESHOLD) {
			std::cerr << "[" << s.name << " lr_move_to_slot] motor passive"
				" (tau=" << last_tau << " Nm < " << TAU_LIVE_THRESHOLD
				<< "), re-enabling\n";
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				dm_->enable(*s.motor);
			}
			// Warmup frames (re-establish torque loop)
			for (int k = 0; k < 5; ++k) {
				{
					std::lock_guard<std::mutex> lk(motor_mutex_);
					dm_->control_mit(*s.motor, MIT_KP, s.hold_kd, probe_setpt, 0.0f, 0.0f);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		}
		// 🔴 [2026-09-02] 刷新移到 if 之外 —— 原本只在 passive 分支內刷新，
		// 而**馬達活著的正常情況同樣被探針推走了**，卻拿探針前的舊值當 ramp 起點。
		// 與 go_home_slot 的作法一致（"whether or not it triggered re-enable"）。
		// ⚠️ Get_Position() 是快取值，但探針的 control_mit 剛交換過幀，所以是新的。
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			cur_cmd = s.motor->Get_Position();
		}
	}

	// start_pos 宣告在探針區塊之後，因此會自動取到上面刷新過的值（僅供 log 用）。
	const float start_pos    = cur_cmd;

	// [2026-06-09s] Original simple loop: trajectory ramps cur_cmd, PD with
	// MIT_KP=12. No velocity FF, no stabilize frames, no distance-aware timeout.
	// hold_pos = target ALWAYS (let hold mode pull motor to target after move loop).
	bool converged = false;
	for (int j = 0; j < MAX_LOOPS; ++j) {
		float diff = target - cur_cmd;
		// [2026-08-18 per user] M2「甩頭」——加上與 M1 同款的 creep-to-target。
		// 之前這裡是全程等速直衝 target，短距離移動特別明顯：INIT 從 pos=0.0750
		// 回 CENTER(0)，距離只有 0.075 rad，但 speed=0.6 讓命令 6 個 tick 就到位，
		// 手臂靠慣性直接衝過去，實測落在 pos=-0.1429（過衝量是移動距離的兩倍），
		// 且 tau=+4.4650 是馬達在反向硬拉回來——那個回拉就是甩頭的手感。
		// creep 對長距離影響有限（1.3 rad 的換槽只有最後 0.15 rad 減速），但短距離
		// 會整段落在 zone 內而全程慢速，正好是最需要溫和的那種情況。
		// 取 min 是為了不讓 creep 反而比呼叫端指定的速度還快（例如 LR_SLOT 傳很
		// 慢的速度做微調時）。
		const float M2_CREEP_ZONE  = 0.15f;
		const float M2_CREEP_SPEED = 0.20f;
		float eff_speed = (std::abs(diff) < M2_CREEP_ZONE)
			? std::min(M2_CREEP_SPEED, speed_rad_s) : speed_rad_s;
		float step = std::max(eff_speed, 0.01f) * DT;
		if (std::abs(diff) <= step) cur_cmd = target;
		else cur_cmd += (diff > 0.0f ? step : -step);

		// [2026-06-11e] 加回 friction FF：用 motor pos vs target 判斷方向
		// 持續推到 motor 真的到 target ±CONV_TOL 內。實驗用、看硬體是否飽和。
		float motor_pos_now = s.motor->Get_Position();
		float motor_vel_now = s.motor->Get_Velocity();
		float pos_err_to_target = target - motor_pos_now;
		float tau_ff = 0.0f;
		if (std::abs(pos_err_to_target) > CONV_TOL) {
			// [2026-08-18] Was a flat ±FRICTION_TAU with no fade — see the
			// FRICTION_FADE_VEL comment above for why that caused the audible
			// grinding once the motor was already moving.
			float fade = 1.0f - std::min(std::abs(motor_vel_now) / FRICTION_FADE_VEL, 1.0f);
			tau_ff = (pos_err_to_target > 0.0f ? FRICTION_TAU : -FRICTION_TAU) * fade;
		}

		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			dm_->control_mit(*s.motor, MIT_KP, s.hold_kd, cur_cmd, 0.0f, tau_ff);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));

		if (cur_cmd == target &&
			std::abs(s.motor->Get_Position() - target) < CONV_TOL) {
			converged = true;
			break;
		}
	}

	const float final_pos = s.motor->Get_Position();
	const float final_err = std::abs(final_pos - target);

	// Log honestly (kept from 2026-06-06 Fix 4)
	if (converged) {
		std::cout << "[" << s.name << " lr_move_to_slot] Done."
			<< "  pos=" << final_pos << "  target=" << target
			<< "  start=" << start_pos << " (converged)\n";
	} else {
		std::cerr << "[" << s.name << " lr_move_to_slot] FAIL — did not reach"
			" target."
			<< "  pos=" << final_pos << "  target=" << target
			<< "  err=" << final_err
			<< "  start=" << start_pos
			<< "  (motor may be jammed / PD insufficient / encoder drift)\n";
	}

	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		s.hold_pos = target;   // [2026-06-09s] always target (original behavior)
		s.hold_err_integral = 0.0f;
	}
	s.hold_en  = true;
	s.move_act = false;
	if (was_enabled) s.enabled = true;
	return converged;
}

// ============================================================
//  Public API -- forwarding to default slots
// ============================================================
void  DamiaoAPI::enable() { enable_slot(m2_); }
void  DamiaoAPI::disable() { disable_slot(m2_); }
void  DamiaoAPI::set_zero() { set_zero_slot(m2_); }
void  DamiaoAPI::go_home() { go_home_slot(m2_); }
void  DamiaoAPI::hold_position() { hold_slot(m2_); }
void  DamiaoAPI::release_hold() { release_hold_slot(m2_); }
bool  DamiaoAPI::is_holding() const { return m2_.hold_en.load(std::memory_order_relaxed); }
void  DamiaoAPI::move_to(float r, float s) { move_to_slot(m2_, r, s); }
bool  DamiaoAPI::is_moving() const { return m2_.move_act.load(std::memory_order_relaxed); }
float DamiaoAPI::get_position() const { return m2_.motor->Get_Position(); }
float DamiaoAPI::get_velocity() const { return m2_.motor->Get_Velocity(); }
float DamiaoAPI::get_torque()   const { return m2_.motor->Get_tau(); }

void  DamiaoAPI::lr_calibrate(bool seek_left) { lr_calibrate_slot(m2_, seek_left); }
bool  DamiaoAPI::lr_move_to_slot(int slot, float speed) { return lr_move_to_slot_impl(m2_, slot, speed); }

bool  DamiaoAPI::calibrate_arm() { return calibrate_arm_slot(m1_); }
void  DamiaoAPI::set_wall_distance(float mm) {
	std::lock_guard<std::mutex> lk(motor_mutex_);
	m1_.wall_dist = (mm > 0.0f) ? mm : 0.0f;
}
bool  DamiaoAPI::approach_wall(float clearance_mm, float speed_rad_s) {
	return approach_wall_slot(m1_, clearance_mm, speed_rad_s);
}
bool  DamiaoAPI::touch_wall(float wall_dist_mm, int m2_slot,
	float clearance_mm, float speed_rad_s) {
	return touch_wall_slot(m1_, wall_dist_mm, m2_slot, clearance_mm, speed_rad_s);
}

bool DamiaoAPI::switch_mode(damiao::Control_Mode mode) {
	std::lock_guard<std::mutex> lk(motor_mutex_);
	return dm_->switchControlMode(*m2_.motor, mode);
}
void DamiaoAPI::control_mit(float kp, float kd, float q, float dq, float tau) {
	std::lock_guard<std::mutex> lk(motor_mutex_);
	dm_->control_mit(*m2_.motor, kp, kd, q, dq, tau);
}

// ============================================================
//  feedback_loop()  -- 50 Hz, handles both slots under one lock
// ============================================================
void DamiaoAPI::feedback_loop()
{
	const float MOVE_DT = 0.02f;   // fixed step size for move interpolation
	auto t_prev = std::chrono::steady_clock::now();
	// [2026-08-14 per user] M1 電流量測請求 — driver 沒有回傳原始電流（damiao.h
	// 只有 Get_Position/Get_Velocity/Get_tau，tau 是馬達內部算好的扭力估計 Nm，
	// 不是電流安培值，換算常數也沒有），只能拿 tau 當替代訊號連續印出來，趨勢上
	// 會跟電流大致同步，但不是真的電流數字；真的要量電流還是得用鉗表夾實體線路。
	// 節流到 4Hz（每 250ms 一次），只印 M1、只在 move/hold 真的有輸出時印，避免
	// feedback_loop 本身 50Hz 洗版。
	auto t_last_tau_log = std::chrono::steady_clock::now();
	while (running_) {
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);

			auto  t_now = std::chrono::steady_clock::now();
			float dt = std::chrono::duration<float>(t_now - t_prev).count();
			dt = std::max(dt, 0.001f);
			t_prev = t_now;

			bool log_tau_this_tick = std::chrono::duration<float>(t_now - t_last_tau_log).count() >= 0.25f;
			if (log_tau_this_tick) t_last_tau_log = t_now;

			for (MotorSlot* s : { &m1_, &m2_ }) {
				if (!s->enabled) continue;

				if (s->move_act) {
					// [2026-08-14 per user] M1 has run away past dangerous velocity
					// on every attempt toward this DEPLOY target — tried ramp speed
					// 0.55 and 0.1 (both failed), hold_kp/kd from 26 to 40 (all
					// failed), gravity feedforward on and off (both failed). The one
					// constant every time: real velocity wildly exceeds anything
					// commanded (hit 2.2 rad/s against a 0.55 rad/s ramp). Rather
					// than keep guessing at the root cause live and risking another
					// runaway, add a hard velocity governor: once M1's real velocity
					// exceeds a safe bound, abandon the position-tracking ramp
					// immediately and apply a pure high-damping brake (kp=0 — not
					// fighting whatever the ramp reference has diverged to) until
					// velocity is back under control, instead of continuing to
					// command a PD response that has already lost the race.
					// [2026-08-17 per user] Bench repeatedly showed the runaway building
					// past this threshold before the brake could fully arrest it
					// (multiple re-trigger cycles, vel swinging up to ±2 rad/s even
					// with the brake engaged) — the true unstable-equilibrium angle
					// is still unconfirmed (geometric estimate ~0.75 rad disagrees
					// with the curve-fit's implied ~0.18 rad), so rather than guess
					// the physics more precisely, intervene earlier and harder.
					// [2026-09-02] 改用檔案範圍的共用常數（見其宣告處），原本是這裡的區域常數
					// [2026-08-17] 20.0 → 5.0：這不是把煞車調弱，而是讓數字誠實。
					// kd 的協定編碼上限就是 5.0（詳見 init() 裡 m1_.hold_kd 的說明），
					// 送 20.0 進去實際上也只會被截成 ~5.0——這個 20 從來沒有生效過，
					// 留著只會讓人誤以為煞車比實際強 4 倍。行為不變，僅修正表述。
					// M1_EMERGENCY_BRAKE_KD 同上，已抽為共用常數

					float real_vel_check = s->motor->Get_Velocity();
					bool  emergency_brake = (s->id == MotorSlot::SlotId::M1)
						&& (std::abs(real_vel_check) > M1_VEL_SAFETY_LIMIT);

					if (emergency_brake) {
						float real_pos_now = s->motor->Get_Position();
						// Re-anchor the ramp reference to the real position so that
						// once braked, the ramp doesn't lurch trying to catch up to
						// wherever it had diverged to during the runaway.
						s->move_cur = real_pos_now;
						// [2026-08-17] 煞車時補回重力前饋（原本這裡 tau_ff 硬傳 0）。
						// 舊寫法等於在最危險的時刻把重力補償整個放掉，只剩 kd*vel 的
						// 阻尼，而阻尼是速度的函數、提供不了任何靜態支撐力。代入實際
						// 數字看就很清楚：觸發門檻 0.4 rad/s × kd 5.0（真實上限）= 2 Nm，
						// 而 pos≈0.8 處的重力矩約 12 Nm——淨力仍有 10 Nm 往下，手臂會
						// 一路加速到 12/5 ≈ 2.4 rad/s 才勉強平衡。bench 觀察到的「煞車
						// 已介入卻還是衝到 ±2 rad/s」正好落在這個數量級，也就是說舊的
						// 煞車在數學上根本不可能撐住手臂，只能讓它以較慢的等速往下滑，
						// 這正是「無法撐住自己整個掉下去」的直接來源。
						// 補回 tau_ff 之後，重力由前饋承擔，kd 只需要吸收「超出平衡點
						// 的那部分」動能，才是真正意義上的煞車。
						float brake_tau_ff = 0.0f;
						if (real_pos_now > M1_GRAVITY_MIN_VALID_RAD)
							brake_tau_ff = M1_GRAVITY_K * std::sin(real_pos_now - M1_GRAVITY_PHASE_RAD);
						dm_->control_mit(*s->motor, 0.0f, M1_EMERGENCY_BRAKE_KD, 0.0f, 0.0f, brake_tau_ff);
						if (log_tau_this_tick) {
							std::cerr << "[M1 SAFETY] vel=" << real_vel_check
								<< " rad/s exceeds " << M1_VEL_SAFETY_LIMIT
								<< " limit — emergency brake (kd=" << M1_EMERGENCY_BRAKE_KD
								<< ", tau_ff=" << brake_tau_ff
								<< ") engaged, pos=" << real_pos_now << "\n";
						}
					}
					else {
					float diff = s->move_target - s->move_cur;
					float step = s->move_speed * MOVE_DT;
					if (std::abs(diff) <= step) {
						s->move_cur = s->move_target;
						s->move_act = false;
						s->hold_err_integral = 0.0f;
						if (s->id == MotorSlot::SlotId::M1) {
							// [2026-08-18 per user] Was s->hold_pos = Get_Position(), i.e.
							// "wherever the arm happened to stop". That froze DEPLOY short
							// of its target: touch_wall would end with the arm still ~0.08 rad
							// out (static friction at that angle is ≥4.6 Nm), and HOLD then
							// latched onto the stalled position instead of continuing to
							// push, so it could never recover the last bit — bench saw
							// err=0.0818 and 0.0822 on two consecutive DEPLOY 400 LEFT runs,
							// i.e. a systematic stop, not noise.
							// Holding move_target instead means the arm keeps being pushed
							// after the ramp ends, and crucially it can now use help that is
							// only available once it stops: the friction feedforward fades
							// back in as |vel|→0 (it was 0 during the stall because
							// vel=0.1526 > M1_FRICTION_FADE_VEL). Net force at the stall
							// point goes 2.26 → ~5.5 Nm, past the ~4.6 Nm static friction.
							// The original rationale ("avoid backward pull from hold_tau_ff
							// contamination") no longer applies: hold_tau_ff is only consulted
							// below M1_GRAVITY_MIN_VALID_RAD (0.20), and every DEPLOY target
							// sits far above that, where the fitted gravity model overrides it.
							// This also makes M1 consistent with M2, which has always used
							// move_target here.
							s->hold_pos = s->move_target;
							// Still refreshed for the low-angle case (pos <= 0.20) where the
							// gravity model is not applied and this proxy is what HOLD uses.
							// Remove kd*(-vel) braking component from Get_tau() so the
							// stored gravity proxy is not contaminated.
							float vel_now = s->motor->Get_Velocity();
							s->hold_tau_ff = s->motor->Get_tau() + s->hold_kd * vel_now;
						} else {
							// M2 is horizontal: use commanded target so PD pulls to the
							// exact slot position. hold_pos=Get_Position() would lock at
							// the tracking-lag offset (~0.3 rad at 1 rad/s, kp=8, kd=3).
							s->hold_pos = s->move_target;
						}
						s->hold_en = true;
					}
					else {
						s->move_cur += (diff > 0.0f ? step : -step);
					}
					// Gravity proxy: M1 only. M2 is horizontal, no gravity compensation.
					// [2026-08-14 per user] Model-based compensation now uses the
					// empirically-fit M1_GRAVITY_K/M1_GRAVITY_PHASE_RAD (see header
					// comment) instead of the old ARM_MASS_KG*g*L*sin(pos-VERTICAL_OFFSET_RAD)
					// guess, which had the wrong phase and made overshoot worse.
					float tau_ff_move = (s->id == MotorSlot::SlotId::M1) ? s->move_tau_ff : 0.0f;
					if (s->id == MotorSlot::SlotId::M1) {
						float pos_now_move = s->motor->Get_Position();
						if (pos_now_move > M1_GRAVITY_MIN_VALID_RAD)
						tau_ff_move = M1_GRAVITY_K * std::sin(pos_now_move - M1_GRAVITY_PHASE_RAD);

						// [2026-08-18] Same Coulomb friction breakaway assist as the
						// PARK ramp in go_home_slot() — DEPLOY showed the same
						// stall-then-jump segmentation. Sized/justified in main_api.h.
						// Safe against the emergency brake above: that fires at
						// |vel| > 0.4 rad/s, where this term has already faded to 0
						// (M1_FRICTION_FADE_VEL = 0.10), so it can never fight the brake.
						float err_move = s->move_target - pos_now_move;
						if (std::abs(err_move) > M1_FRICTION_DEADBAND_RAD) {
							float fade_move = 1.0f - std::min(
								std::abs(s->motor->Get_Velocity()) / M1_FRICTION_FADE_VEL, 1.0f);
							tau_ff_move += M1_FRICTION_TAU * fade_move
								* (err_move > 0.0f ? 1.0f : -1.0f);
						}
					}
					dm_->control_mit(*s->motor,
						s->hold_kp, s->hold_kd,
						s->move_cur, 0.0f, tau_ff_move);

					// [2026-08-17 per user] Same continuous passive watchdog as the
					// HOLD branch below — a mid-MOVE passive event is just as
					// dangerous (arm still has a target to chase, gravity wins
					// silently if the motor stops actually outputting torque).
					if (s->id == MotorSlot::SlotId::M1) {
						if (s->passive_recover_cooldown_ticks > 0) --s->passive_recover_cooldown_ticks;
						float live_pos_chk = s->motor->Get_Position();
						float live_tau_chk = s->motor->Get_tau();
						if (s->passive_recover_cooldown_ticks == 0
								&& std::abs(s->move_cur - live_pos_chk) > 0.1f
								&& std::abs(live_tau_chk) < 0.3f) {
							std::cerr << "[M1 MOVE] passive suspected (err=" << (s->move_cur - live_pos_chk)
								<< " rad, tau=" << live_tau_chk << " Nm < 0.3), re-enabling\n";
							dm_->enable(*s->motor);
							s->passive_recover_cooldown_ticks = 15;
						}
					}
					}

					if (s->id == MotorSlot::SlotId::M1 && log_tau_this_tick) {
						std::cout << "[M1 tau] mode=" << (emergency_brake ? "BRAKE" : "MOVE")
							<< " pos=" << s->motor->Get_Position()
							<< " vel=" << s->motor->Get_Velocity()
							<< " tau=" << s->motor->Get_tau() << " Nm\n";
					}
				}
				else if (s->hold_en) {
					float pos_now = s->motor->Get_Position();

					// M1 gravity proxy (tau at last HOLD or move→hold); overridden by the
					// empirically-fit M1_GRAVITY_K/M1_GRAVITY_PHASE_RAD model below.
					float tau_ff = (s->id == MotorSlot::SlotId::M1) ? s->hold_tau_ff : 0.0f;
					if (s->id == MotorSlot::SlotId::M1 && pos_now > M1_GRAVITY_MIN_VALID_RAD) {
						tau_ff = M1_GRAVITY_K * std::sin(pos_now - M1_GRAVITY_PHASE_RAD);
					}

					// software integral (disabled when hold_ki == 0)
					float tau_i = 0.0f;
					if (s->hold_ki > 0.0f) {
						s->hold_err_integral += (s->hold_pos - pos_now) * dt;
						s->hold_err_integral = std::max(-MotorSlot::HOLD_I_MAX,
							std::min(s->hold_err_integral,
								MotorSlot::HOLD_I_MAX));
						tau_i = s->hold_ki * s->hold_err_integral;
					}

					dm_->control_mit(*s->motor,
						s->hold_kp, s->hold_kd,
						s->hold_pos, 0.0f,
						tau_ff + tau_i);

					// [2026-08-17 per user] Continuous passive-state watchdog — bench
					// video showed M1 going limp mid-HOLD (not at a fresh command's
					// start, where touch_wall_slot/go_home_slot's own pre-checks
					// already cover it) and free-falling uncaught by software.
					// If there's a real position error (>0.1 rad) but measured tau
					// stays near zero, the motor almost certainly isn't outputting
					// real torque — force a re-enable. Cooldown avoids re-triggering
					// dm_->enable() (blocks ~100ms) every single 20ms tick.
					if (s->id == MotorSlot::SlotId::M1) {
						if (s->passive_recover_cooldown_ticks > 0) --s->passive_recover_cooldown_ticks;
						float live_pos = s->motor->Get_Position();
						float live_tau = s->motor->Get_tau();
						if (s->passive_recover_cooldown_ticks == 0
								&& std::abs(s->hold_pos - live_pos) > 0.1f
								&& std::abs(live_tau) < 0.3f) {
							std::cerr << "[M1 HOLD] passive suspected (err=" << (s->hold_pos - live_pos)
								<< " rad, tau=" << live_tau << " Nm < 0.3), re-enabling\n";
							dm_->enable(*s->motor);
							s->passive_recover_cooldown_ticks = 15;   // 15*20ms = 300ms before retrying
						}
					}

					if (s->id == MotorSlot::SlotId::M1 && log_tau_this_tick) {
						std::cout << "[M1 tau] mode=HOLD pos=" << s->motor->Get_Position()
							<< " vel=" << s->motor->Get_Velocity()
							<< " tau=" << s->motor->Get_tau() << " Nm\n";
					}
				}
				else {
					dm_->control_mit(*s->motor, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
				}
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

// ============================================================
//  TCP server internals
// ============================================================
void DamiaoAPI::server_loop()
{
	while (running_) {
		sockaddr_in client_addr{};
#ifdef _WIN32
		int addr_len = sizeof(client_addr);
#else
		socklen_t addr_len = sizeof(client_addr);
#endif
		socket_t client = ::accept(listen_sock_,
			reinterpret_cast<sockaddr*>(&client_addr),
			&addr_len);
		if (client == INVALID_SOCKET) {
			if (!running_) break;
			continue;
		}
		char ip_str[INET_ADDRSTRLEN]{};
		inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
		std::cout << "[DamiaoAPI] Client connected: " << ip_str
			<< ":" << ntohs(client_addr.sin_port) << "\n";
		std::thread(&DamiaoAPI::client_thread, this, client).detach();
	}
}

void DamiaoAPI::client_thread(socket_t sock)
{
	char buf[512];
	std::string leftover;

	while (running_) {
		int n = ::recv(sock, buf, static_cast<int>(sizeof(buf)) - 1, 0);
		if (n <= 0) break;
		buf[n] = '\0';
		leftover += buf;

		for (;;) {
			auto nl = leftover.find('\n');
			if (nl == std::string::npos) break;
			std::string line = leftover.substr(0, nl);
			leftover.erase(0, nl + 1);
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty()) continue;

			std::cout << "[DamiaoAPI] recv: " << line << "\n";
			std::string reply = dispatch(line) + "\n";
			::send(sock, reply.c_str(), static_cast<int>(reply.size()), 0);
		}
	}
	closesocket(sock);
	std::cout << "[DamiaoAPI] Client disconnected\n";
}

// ============================================================
//  dispatch()  -- parse M1/M2 prefix, route to dispatch_motor
// ============================================================
std::string DamiaoAPI::dispatch(const std::string& line)
{
	std::istringstream iss(line);
	std::string prefix;
	iss >> prefix;
	if (prefix.empty()) return "ERR empty command";
	for (auto& c : prefix) c = static_cast<char>(::toupper(c));

	// ---- SYS compound commands (no M1/M2 prefix) ----------------------------
	if (prefix == "INIT") {
		return cmd_init_sequence();
	}
	if (prefix == "DEPLOY") {
		std::string rest;
		std::getline(iss, rest);
		ltrim(rest);
		return cmd_deploy_sequence(rest);
	}
	if (prefix == "PARK") {
		return cmd_park_sequence();
	}
	if (prefix == "STATUS") {
		return cmd_status_sequence();
	}

	MotorSlot* slot = nullptr;
	if (prefix == "M1") slot = &m1_;
	else if (prefix == "M2") slot = &m2_;
	else return "ERR usage: M1 <cmd> or M2 <cmd>";

	std::string rest;
	std::getline(iss, rest);
	ltrim(rest);

	return dispatch_motor(*slot, rest);
}

// ============================================================
//  wait_for_move()  -- poll move_act; true=done, false=timeout
// ============================================================
bool DamiaoAPI::wait_for_move(MotorSlot& s, int timeout_ms)
{
	int elapsed = 0;
	while (s.move_act.load(std::memory_order_relaxed) && elapsed < timeout_ms) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		elapsed += 50;
	}
	return !s.move_act.load(std::memory_order_relaxed);
}

// ============================================================
//  cmd_init_sequence()  -- INIT
//  M1: ENABLE → HOME → CALIBRATE
//  M2: ENABLE → HOME → LR_CALIBRATE RIGHT
// ============================================================
std::string DamiaoAPI::cmd_init_sequence()
{
	if (!m1_.enabled) enable_slot(m1_);

	// Safety guard: M1 encoder offset may survive a crash and come back out of physical range.
	// pos <= 0: arm is at/past mechanical stop — set_zero restores calibrated zero.
	// pos > upper_bound: stale large offset — force set_zero so go_home starts from sane baseline.
	{
		bool did_reset = false;
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			float pos = m1_.motor->Get_Position();
			if (pos <= 0.0f || pos > m1_.upper_bound) {
				std::cerr << "[DamiaoAPI] INIT: M1 pos=" << pos
					<< " rad — out of range [0, " << m1_.upper_bound << "], forcing set_zero\n";
				dm_->set_zero_position(*m1_.motor);
				m1_.hold_pos = 0.0f;
				m1_.hold_err_integral = 0.0f;
				did_reset = true;
			}
		}
		if (did_reset) {
			for (int i = 0; i < 3; ++i) {
				{
					std::lock_guard<std::mutex> lk(motor_mutex_);
					dm_->control_mit(*m1_.motor, 0.0f, m1_.hold_kd, 0.0f, 0.0f, 0.0f);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		}
	}

	// [2026-07-24 per user] use_park_profile=false — INIT's pre-calibrate homing,
	// not the user-facing PARK command; keep original fast behavior.
	go_home_slot(m1_, /*use_park_profile=*/false);
	if (!calibrate_arm_slot(m1_))
		return "ERR INIT: M1 calibrate failed (stop not found)";

	enable_slot(m2_);

	// Safety guard: M2 encoder offset can survive a crash and come back wildly wrong
	// (e.g. -12 rad). Physical travel is ~±0.76 rad; beyond 3 rad is stale — force
	// set_zero before calibration so lr_calibrate_slot() starts from a sane baseline.
	{
		bool did_reset = false;
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			float pos = m2_.motor->Get_Position();
			if (std::abs(pos) > 1.5f) {
				std::cerr << "[DamiaoAPI] INIT: M2 pos=" << pos
					<< " rad — out of range, forcing set_zero\n";
				dm_->set_zero_position(*m2_.motor);
				m2_.hold_pos = 0.0f;
				m2_.hold_err_integral = 0.0f;
				did_reset = true;
				// [2026-08-14 per user] This just clobbered whatever zero reference
				// lr_half_range/lr_calibrated were trusting — force a real
				// LR_CALIBRATE run below instead of confidently moving to a
				// "CENTER" that's actually centered on nothing.
				m2_.lr_calibrated = false;
			}
		}
		if (did_reset) {
			for (int i = 0; i < 3; ++i) {
				{
					std::lock_guard<std::mutex> lk(motor_mutex_);
					dm_->control_mit(*m2_.motor, 0.0f, m2_.hold_kd, 0.0f, 0.0f, 0.0f);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		}
	}

	// ---- DIAG: 记录校准前 M2 状态 ----
	float m2_pos_before = m2_.motor->Get_Position();
	float m2_vel_before = m2_.motor->Get_Velocity();
	float m2_tau_before = m2_.motor->Get_tau();
	std::cout << "[INIT DIAG] M2 before calibrate: pos=" << std::fixed << std::setprecision(4)
		<< m2_pos_before << " vel=" << m2_vel_before << " tau=" << m2_tau_before << "\n";

	/*
	go_home_slot(m2_);

	// ---- DIAG: 记录校准前 M2 状态 ----
	float m2_pos_home = m2_.motor->Get_Position();
	float m2_vel_home = m2_.motor->Get_Velocity();
	float m2_tau_home = m2_.motor->Get_tau();
	std::cout << "[INIT DIAG] M2 go home: pos=" << std::fixed << std::setprecision(4)
		<< m2_pos_home << " vel=" << m2_vel_home << " tau=" << m2_tau_home << "\n";

	*/
	// [2026-08-14 per user] LR_CALIBRATE's auto-seek is still unreliable (false-
	// early stops, or seeks that travel huge distances finding no resistance at
	// all), and re-running it on every single INIT was stomping a good, already-
	// trusted lr_half_range/zero — that's why 3 consecutive INITs each landed
	// somewhere different (left/right/center) instead of centering reliably.
	// Once M2 is trusted (a converged auto-calibrate, or a manual SET_HALF_RANGE),
	// skip the auto-seek entirely and just move to CENTER using the existing
	// zero/half_range. Re-running LR_CALIBRATE is still available as an explicit
	// standalone command for whenever it's actually needed (e.g. after a crash).
	bool m2_calib_ok;
	if (m2_.lr_calibrated) {
		std::cout << "[INIT] M2 already calibrated (lr_half_range=" << m2_.lr_half_range
			<< ") — skipping auto-seek, moving to CENTER\n";
		m2_calib_ok = lr_move_to_slot_impl(m2_, 0 /*CENTER*/, 0.6f);
	} else {
		m2_calib_ok = lr_calibrate_slot(m2_, /*seek_left=*/true);
	}

	// ---- DIAG: 记录校准后 M2 状态 ----
	float m2_pos_after = m2_.motor->Get_Position();
	float m2_vel_after = m2_.motor->Get_Velocity();
	float m2_tau_after = m2_.motor->Get_tau();
	std::cout << "[INIT DIAG] M2 after calibrate: pos=" << std::fixed << std::setprecision(4)
		<< m2_pos_after << " vel=" << m2_vel_after << " tau=" << m2_tau_after
		<< " hold_pos=" << m2_.hold_pos << "\n";

	// [2026-07-27 per user] lr_calibrate_slot now reports whether it actually
	// found the stop / converged — previously this was ignored and INIT always
	// returned OK even when M2 completely failed to calibrate (bench-confirmed:
	// Phase 1 timeout, position frozen for the full seek, twice in a row).
	if (!m2_calib_ok)
		return "ERR INIT: M2 calibrate failed (stop not found / did not converge — see log)";

	return "OK";
}

// ============================================================
//  cmd_deploy_sequence()  -- DEPLOY
//  Step 1: M1 retract to vertical (VERTICAL_OFFSET_RAD)
//  Step 2: M2 move to target slot @ 1.0 rad/s
//  Step 3: M1 TOUCHWALL
//  [speed] param applies to M1 only; M2 always uses 1.0 rad/s.
// ============================================================
std::string DamiaoAPI::cmd_deploy_sequence(const std::string& params)
{
	// [2026-08-14 per user] DEPLOY touch_wall 這段反覆在通過某個角度時失控暴衝，
	// 已知在 0.55、0.1、0.08 都發生過（0.08 這次還是來回甩了 4-5 次才穩下來，只是
	// 沒有再撞上限/凍結）。[2026-08-17 per user] 真正的不穩定平衡角度還沒確認
	// （幾何估計 ~0.75 vs 擬合結果換算 ~0.18，兩者對不上），在查清楚之前先繼續
	// 降速，配合同時調緊的 M1_VEL_SAFETY_LIMIT/M1_EMERGENCY_BRAKE_KD 一起降低風險。
	// [2026-08-18 per user] spd 0.05 → 0.10. 原本 0.55→0.35→0.10→0.08→0.05 一路
	// 降速，是為了壓制當時查不出根因的暴衝；那些根因後來查明並修掉了（kd 編碼
	// 溢位讓阻尼實際只有 0.5 而非設定值、ramp 重力前饋用 cur_cmd 而非 pos）。
	// 提升幅度刻意比 PARK（0.07→0.15）保守一半：DEPLOY 是**順著重力**伸出
	// （tau_ff 為負代表重力往伸出方向拉），一旦失控會被重力持續加速，正是當年
	// 暴衝的那條路徑；PARK 逆重力則不會。速度安全閥 M1_VEL_SAFETY_LIMIT=0.4
	// 維持不動，仍是命令速度的 4 倍餘裕，真失控時照樣攔得住。
	float wall_mm = 0.0f, clearance = 0.0f, spd = m1_.deploy_speed;   // 0.08→0.05→0.10→0.15 (runtime-tunable)
	std::string slot_str;
	std::istringstream ps(params);
	if (!(ps >> wall_mm >> slot_str))
		return "ERR usage: DEPLOY <wall_mm> <LEFT|CENTER|RIGHT> [clearance_mm] [speed_rad_s]";
	for (auto& c : slot_str) c = static_cast<char>(::toupper(c));
	ps >> clearance >> spd;
	if (clearance < 0.0f) clearance = 0.0f;

	int m2_slot_idx;
	if (slot_str == "LEFT")   m2_slot_idx = -1;
	else if (slot_str == "CENTER") m2_slot_idx = 0;
	else if (slot_str == "RIGHT")  m2_slot_idx = 1;
	else return "ERR usage: DEPLOY <wall_mm> <LEFT|CENTER|RIGHT> [clearance_mm] [speed_rad_s]";

	if (!m1_.enabled) return "ERR DEPLOY: M1 not enabled";
	if (!m2_.enabled) return "ERR DEPLOY: M2 not enabled";

	// Step 1: M1 retract to home (0 rad).
	// [2026-07-27 per user] use_park_profile=true — reverses the 2026-07-24
	// decision below: user now explicitly wants this LEFT/RIGHT-switch retract
	// to use the SAME profile as PARK (park_kp/park_kd/park_speed), slower than
	// before. If slot-switching feels too slow again, this is the line to flip
	// back to false (see the retained rationale comment for why it was false).
	// [2026-07-24 per user] use_park_profile=false — this is an internal
	// retract-before-re-extend, NOT the user-facing PARK command. Must keep the
	// original fast DEPLOY-matching speed/torque; it must not inherit PARK's
	// slow/gentle tuning (that regression made switching LEFT/RIGHT slots feel
	// much slower, which was never the intent).
	go_home_slot(m1_, /*use_park_profile=*/true);
	// Wait for physical convergence (now effective since hold bias is zeroed)
	for (int i = 0; i < 40; ++i) {
		float actual;
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			actual = m1_.motor->Get_Position();
		}
		if (actual < 0.05f) break;   // go_home_slot targets 0 rad, not VERTICAL_OFFSET_RAD
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	// Step 2: M2 move to target slot
	// [2026-06-06] Fix 4: lr_move_to_slot_impl now returns bool — false = timed
	// out before reaching slot. Fail DEPLOY explicitly so washrobot's
	// deploy_with_m2_verify_ retry pattern (2026-06-06l) sees ERR and retries.
	// [2026-06-09s] Revert to original speed 0.8 rad/s. With slot ±0.7 (close to
	// mechanical stop) + simple control loop + fast trajectory, motor reaches
	// slot quickly without time to oscillate. Hold mode then pulls to target.
	// [2026-06-09aa] reference tuning: 兩邊都用 0.8
	if (!lr_move_to_slot_impl(m2_, m2_slot_idx, 0.8f))
		return "ERR DEPLOY: M2 slot move did not converge";
	if (!wait_for_move(m2_))
		return "ERR DEPLOY: M2 slot timeout";

	// Step 3: M1 touch wall
	if (!touch_wall_slot(m1_, wall_mm, m2_slot_idx, clearance, spd))
		return "ERR DEPLOY: M1 touch_wall failed";
	// [2026-07-27 per user] Was "best-effort" — wait_for_move's return value was
	// discarded, so DEPLOY always reported OK even when M1 never actually
	// reached theta_target (touch_wall_slot only SETS the target and returns
	// immediately; a separate background loop drives the real motion via
	// move_act/move_target). Bench symptom: repeated DEPLOYs of the same slot
	// drifted progressively farther from the wall with no error ever surfacing,
	// because each cycle's Step 1 retract started from wherever M1 actually
	// stopped last time, not from the intended theta_target. Mirror Step 2's
	// M2 pattern: fail the DEPLOY explicitly on timeout instead of silently
	// continuing.
	if (!wait_for_move(m1_))
		return "ERR DEPLOY: M1 touch_wall timeout";

	// [2026-08-13 per user] wait_for_move() above only confirms the SOFTWARE
	// ramp finished (move_cur reached move_target inside feedback_loop) — it
	// does NOT confirm M1's real encoder actually got there. Bench found M1's
	// real position frozen near 0 rad across three consecutive DEPLOYs while
	// touch_wall_slot kept commanding new theta_targets each time (0.586,
	// 0.631, 0.586 rad) and no error ever surfaced — mirrors the same gap M2's
	// lr_move_to_slot_impl already closed with an explicit position check.
	// m1_.move_target still holds touch_wall_slot's clamped theta_target here
	// (feedback_loop's move-complete branch only touches move_cur/hold_pos/
	// hold_en/move_act, never move_target), so read it back for comparison
	// instead of threading a new out-param through touch_wall_slot.
	{
		float actual_pos, expected_target;
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			actual_pos      = m1_.motor->Get_Position();
			expected_target = m1_.move_target;
		}
		// [2026-08-18 per user] 0.05 → 0.08. A bench DEPLOY failed with
		// err=0.0501738 against the 0.05 limit — over by 0.0002 rad, which is
		// 0.06 mm at the arm tip, i.e. measurement noise rather than a real
		// miss. 0.05 rad is only 16 mm at the tip, and washrobot's own mirror of
		// this check (ARM_DEPLOY_POS_TOL_RAD in WASH_ROBOT.h) has always used
		// 0.15 — this side was the stricter of the two for no stated reason.
		// 0.08 (≈26 mm at the tip) still catches a genuinely jammed or
		// unresponsive motor, which is what this check exists for.
		const float M1_TOUCH_WALL_TOL = 0.08f;   // was 0.05 (= go_home_slot's ARRIVE_TOL)
		float err = std::abs(actual_pos - expected_target);
		if (err > M1_TOUCH_WALL_TOL) {
			std::cerr << "[DamiaoAPI] DEPLOY: M1 touch_wall FAIL — did not reach real"
				" target.  pos=" << actual_pos << "  target=" << expected_target
				<< "  err=" << err
				<< "  (motor may be jammed / not responding / disabled — see M1"
				" go_home logs for its last known real position)\n";
			return "ERR DEPLOY: M1 touch_wall did not converge";
		}
	}

	bool may_limit = false;
	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		if (m1_.wall_dist > 0.0f) {
			float usable_sw = m1_.wall_dist - PASSIVE_EXT_MM;
			float theta_sw = (usable_sw <= 0.0f)
				? VERTICAL_OFFSET_RAD
				: VERTICAL_OFFSET_RAD + std::asin(std::min(usable_sw / ARM_LENGTH_MM, 1.0f));
			float tool_ext = (m2_slot_idx < 0) ? TOOL_EXT_LEFT_MM
				: (m2_slot_idx > 0) ? TOOL_EXT_RIGHT_MM
				: TOOL_EXT_CENTER_MM;
			float usable = wall_mm - clearance - (PASSIVE_EXT_MM + tool_ext);
			float theta_tgt = (usable <= 0.0f)
				? VERTICAL_OFFSET_RAD
				: VERTICAL_OFFSET_RAD + std::asin(std::min(usable / ARM_LENGTH_MM, 1.0f));
			may_limit = (theta_tgt > theta_sw);
		}
	}

	std::ostringstream oss;
	oss << "OK";
	/*
	oss << std::fixed << std::setprecision(4)
		<< "OK wall=" << wall_mm << " slot=" << slot_str
		<< " clearance=" << clearance << " speed=" << spd;
	if (may_limit) oss << " warn=SETWALL_MAY_LIMIT";
	*/
	return oss.str();
}

// ============================================================
//  cmd_park_sequence()  -- PARK: home + disable both motors
// ============================================================
std::string DamiaoAPI::cmd_park_sequence()
{
	// [2026-08-18] Only release M1 if it actually made it home.
	// Bench failure this fixes: go_home_slot() falsely reported ARRIVED at
	// pos=0.2180 (target 0.0500), then disable_slot() cut all torque with the
	// arm still 0.168 rad up. It did not drop immediately — ~1 Nm of static
	// friction held it, which is exactly why PARK_STOP_MARGIN's "release just
	// short of the stop and let it settle" normally looks fine. It dropped a
	// moment later when M2 started moving and the vibration broke that friction
	// balance, which is what made it look like M2 caused the fall. M2 was the
	// trigger; the cause was releasing a motor that was never home.
	// On failure: leave M1 enabled and holding. go_home_slot() restored
	// s.enabled/hold_en on exit, so feedback_loop() is now servicing the slot
	// and its continuous passive watchdog can still try to recover it — which
	// is strictly better than handing the arm to gravity.
	// [2026-08-26 per user: 「park沒反應」] PARK used to be gated on
	// `if (m1_.enabled)`. Since a successful PARK ends with disable_slot(),
	// EVERY SUBSEQUENT PARK silently did nothing and still returned "OK" — no
	// go_home_slot() call, so not even a [M1 go_home] line in the log. That is
	// exactly the reported symptom.
	//
	// Doing nothing is only correct if the arm is actually home. A disabled M1
	// has NO holding torque, so it can drift or be pushed away from home while
	// parked — and that is precisely the case where the operator presses PARK
	// again and most needs it to work. So: if the arm is disabled but not near
	// home, re-enable it, home it properly, then release it again.
	bool m1_home_ok = true;
	if (!m1_.enabled) {
		float pos_now = 0.0f;
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			pos_now = m1_.motor->Get_Position();
		}
		// PARK_STOP_MARGIN-sized band around home; outside it the arm is
		// meaningfully off-target and worth re-homing.
		if (std::abs(pos_now) > 0.15f) {
			std::cerr << "[PARK] M1 disabled but pos=" << pos_now
			          << " is away from home — re-enabling to home it\n";
			enable_slot(m1_);
			m1_home_ok = go_home_slot(m1_);
		} else {
			std::cerr << "[PARK] M1 already disabled and near home (pos="
			          << pos_now << ") — nothing to do\n";
		}
	} else {
		m1_home_ok = go_home_slot(m1_);
	}
	if (m1_home_ok) {
		disable_slot(m1_);
	} else {
		std::cerr << "[PARK] M1 did not reach home — keeping it ENABLED and holding "
			"instead of releasing it mid-air\n";
	}

	// [2026-07-27 per user] M2 home no longer goes through go_home_slot()'s own
	// ramp — bench confirmed M2 isn't CAN-passive (the go_home_slot passive
	// probe added earlier this session never fires), it's just that go_home_slot
	// uses park_kp==hold_kp==2.5 Nm-ish gain for M2, nowhere near enough to
	// overcome M2's static friction (same reason lr_move_to_slot_impl needs
	// MIT_KP=28 + FRICTION_TAU=2.3 to reliably move M2 to a slot at all — see
	// its 2026-06-06+ tuning history). CENTER (slot=0) target is target=0.0,
	// identical to go_home_slot's M2 target, so reuse the already-proven
	// lr_move_to_slot_impl path (which also carries its own passive-state
	// detection + re-enable) instead of inventing new go_home_slot tuning.
	if (m2_.enabled) lr_move_to_slot_impl(m2_, 0 /*CENTER*/, 0.6f);
	disable_slot(m2_);

	// M2 still runs even when M1 failed: M1 is holding under power at this
	// point, so M2's motion can no longer shake it loose, and leaving the tool
	// head off-center helps nobody.
	if (!m1_home_ok)
		return "ERR PARK: M1 did not reach home — still enabled and holding, NOT released";

	return "OK";
}

// 🔴 [2026-09-02] 把 damiao 的錯誤碼翻成看得懂的字串。
//
// 錯誤碼定義在 user_lib/damiao.h 的 CAN_Receive_Frame 註解裡，而 MIT 回授幀的
// data[0] = (ERR<<4)|ID —— 那半個位元組在此之前**整個專案都沒有讀過**，所以
// 「M1 突然 passive」自 2026-08-17 起只能靠 tau<0.3 間接推斷，根因一直未明。
//
// ⚠️ **`0` 只代表「這一幀沒有報錯」，不等於馬達健康。** 若 passive 的成因不是
//    馬達自報的保護動作（例如 CAN 幀遺失、韌體層靜默失效），這裡會恆為 0 ——
//    那仍是有價值的排除，但不會直接給答案。不要把 err=0 當成「一切正常」的證據。
static std::string err_name(uint8_t e)
{
	switch (e) {
	case 0x0: return "0";
	case 0x8: return "8:OVER_VOLT";
	case 0x9: return "9:UNDER_VOLT";
	case 0xA: return "A:OVER_CURRENT";
	case 0xB: return "B:MOS_OVERTEMP";
	case 0xC: return "C:COIL_OVERTEMP";
	case 0xD: return "D:CAN_LOST";
	case 0xE: return "E:OVERLOAD";
	default: {
		std::ostringstream o; o << "0x" << std::hex << unsigned(e); return o.str();
	}
	}
}

std::string DamiaoAPI::cmd_status_sequence()
{
	float pos_1, vel_1, tau_1;
	uint8_t err_1 = 0, err_2 = 0;   // [2026-09-02] 馬達自報錯誤碼
	float pos_2, vel_2, tau_2;
	std::ostringstream oss;
	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		pos_1 = m1_.motor->Get_Position();
		vel_1 = m1_.motor->Get_Velocity();
		tau_1 = m1_.motor->Get_tau();
		pos_2 = m2_.motor->Get_Position();
		vel_2 = m2_.motor->Get_Velocity();
		tau_2 = m2_.motor->Get_tau();
		// 🔴 [2026-09-02] 馬達自報的錯誤碼。先前整個專案都沒有讀過這半個位元組，
		// 導致「M1 突然 passive」只能靠 tau<0.3 間接猜（2026-08-17 起原因未明）。
		err_1 = m1_.motor->Get_err();
		err_2 = m2_.motor->Get_err();
	}
	oss << std::fixed << std::setprecision(4)
		<< "[M1] pos=" << pos_1 << " vel=" << vel_1 << " tau=" << tau_1
		<< " hold=" << (m1_.hold_en.load() ? 1 : 0)
		<< " moving=" << (m1_.move_act.load() ? 1 : 0)
		<< " err=" << err_name(err_1) << " | "
		<< "[M2] pos=" << pos_2 << " vel=" << vel_2 << " tau=" << tau_2
		<< " hold=" << (m2_.hold_en.load() ? 1 : 0)
		<< " moving=" << (m2_.move_act.load() ? 1 : 0)
		<< " err=" << err_name(err_2);

	return oss.str();
}

// ============================================================
//  dispatch_motor()  -- per-motor command handler
// ============================================================
std::string DamiaoAPI::dispatch_motor(MotorSlot& s, const std::string& line)
{
	std::istringstream iss(line);
	std::string kw;
	iss >> kw;
	if (kw.empty()) return "ERR empty command";
	for (auto& c : kw) c = static_cast<char>(::toupper(c));

	std::string params;
	std::getline(iss, params);
	ltrim(params);

	// ---- shared commands (M1 + M2) ------------------------------------------

	if (kw == "ENABLE") { enable_slot(s);  return "OK"; }
	if (kw == "DISABLE") { disable_slot(s); return "OK"; }
	if (kw == "ZERO") { set_zero_slot(s); return "OK"; }

	if (kw == "HOME") {
		if (!s.enabled) return "ERR motor not enabled; send ENABLE first";
		go_home_slot(s);
		return "OK";
	}

	// [2026-08-14 per user] M1 real gravity-balance-point measurement. Repeated
	// bench failures near VERTICAL_OFFSET_RAD(0.38) suggest that constant does
	// NOT match M1's real mechanical balance point (slow-speed test showed it
	// still accelerating well past 0.38, all the way to ~0.9-1.2 before gravity
	// visibly reversed). Rather than guess a new constant, let gravity settle
	// M1 on its own: kp=0 (no spring pulling toward any target), kd-only
	// (viscous damping so it doesn't swing wildly), and log where it actually
	// comes to rest. Aborts if position leaves [lower_bound, upper_bound] (that
	// would mean something is wrong — pure gravity+damping shouldn't reach the
	// hard limits) instead of continuing to push against a real mechanical stop.
	if (kw == "FREE_HANG") {
		if (s.id != MotorSlot::SlotId::M1) return "ERR FREE_HANG is M1-only";
		if (!s.enabled) return "ERR motor not enabled; send ENABLE first";

		const float DAMPING_KD = 4.0f;
		const float VEL_SETTLE_THRESHOLD = 0.05f;
		const int   SETTLE_CNT_NEEDED = 20;   // 20*20ms = 400ms sustained low velocity
		const int   MAX_LOOPS = 1000;         // 1000*20ms = 20s safety cap

		s.hold_en = false;
		s.move_act = false;

		std::cout << "[M1 FREE_HANG] starting — kp=0, kd=" << DAMPING_KD
			<< ", letting gravity settle it, logging every 200ms\n";

		int   settle_cnt = 0;
		float final_pos = 0.0f, final_vel = 0.0f;
		bool  settled = false;
		bool  aborted = false;

		for (int i = 0; i < MAX_LOOPS; ++i) {
			float pos, vel;
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				pos = s.motor->Get_Position();
				vel = s.motor->Get_Velocity();
			}
			final_pos = pos;
			final_vel = vel;

			if (pos < s.lower_bound - 0.05f || pos > s.upper_bound + 0.05f) {
				std::cerr << "[M1 FREE_HANG] ABORT: pos=" << pos << " left safe range ["
					<< s.lower_bound << ", " << s.upper_bound << "]\n";
				aborted = true;
				break;
			}

			if (std::abs(vel) < VEL_SETTLE_THRESHOLD) ++settle_cnt; else settle_cnt = 0;

			if (i % 10 == 0)
				std::cout << "[M1 FREE_HANG] t=" << (i * 20) << "ms pos=" << pos
					<< " vel=" << vel << " settle_cnt=" << settle_cnt << "\n";

			if (settle_cnt >= SETTLE_CNT_NEEDED) { settled = true; break; }

			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				dm_->control_mit(*s.motor, 0.0f, DAMPING_KD, 0.0f, 0.0f, 0.0f);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}

		// Snap back to a normal safe hold at wherever it ended up — never leave
		// M1 in the zero-kp damping-only state once this command returns.
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			s.hold_pos = final_pos;
			s.hold_tau_ff = 0.0f;
			s.hold_err_integral = 0.0f;
		}
		s.hold_en = true;

		std::ostringstream oss;
		oss << std::fixed << std::setprecision(4);
		if (aborted)
			oss << "ERR FREE_HANG aborted out of range pos=" << final_pos << " vel=" << final_vel;
		else if (settled)
			oss << "OK FREE_HANG settled_pos=" << final_pos << " vel=" << final_vel;
		else
			oss << "OK FREE_HANG timeout (20s, did not fully settle) pos=" << final_pos << " vel=" << final_vel;
		std::cout << "[M1 FREE_HANG] " << oss.str() << " -- now back in normal HOLD at this position\n";
		return oss.str();
	}

	if (kw == "STATUS") {
		float pos, vel, tau;
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			pos = s.motor->Get_Position();
			vel = s.motor->Get_Velocity();
			tau = s.motor->Get_tau();
		}
		std::ostringstream oss;
		oss << std::fixed << std::setprecision(4)
			<< "pos=" << pos << " vel=" << vel << " tau=" << tau
			<< " hold=" << (s.hold_en.load() ? 1 : 0)
			<< " moving=" << (s.move_act.load() ? 1 : 0);
		return oss.str();
	}

	if (kw == "HOLD") {
		if (!s.enabled) return "ERR motor not enabled; send ENABLE first";
		hold_slot(s);
		return "OK";
	}

	if (kw == "UNHOLD") { release_hold_slot(s); return "OK"; }

	if (kw == "MOVING") { return s.move_act ? "1" : "0"; }

	if (kw == "MOVETO") {
		if (!s.enabled) return "ERR motor not enabled; send ENABLE first";
		float rad = 0.0f, spd = 0.3f;
		std::istringstream ps(params);
		if (!(ps >> rad)) return "ERR usage: MOVETO <rad> [speed_rad_s]";
		ps >> spd;
		move_to_slot(s, rad, spd);
		float actual_target;
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			actual_target = s.move_target;
		}
		std::ostringstream oss;
		oss << std::fixed << std::setprecision(4)
			<< "OK target=" << actual_target << " speed=" << spd;
		if (std::abs(actual_target - rad) > 1e-4f)
			oss << " warn=CLAMPED";
		return oss.str();
	}

	if (kw == "MIT") {
		float kp, kd, q, dq, tau;
		std::istringstream ps(params);
		if (!(ps >> kp >> kd >> q >> dq >> tau))
			return "ERR usage: MIT <kp> <kd> <q> <dq> <tau>";
		std::lock_guard<std::mutex> lk(motor_mutex_);
		dm_->control_mit(*s.motor, kp, kd, q, dq, tau);
		return "OK";
	}

	if (kw == "MODE") {
		int m = 0;
		std::istringstream ps(params);
		if (!(ps >> m) || m < 1 || m > 7)
			return "ERR usage: MODE <1-7>  (1=MIT 2=POS_VEL 3=VEL ...)";
		std::lock_guard<std::mutex> lk(motor_mutex_);
		bool ok = dm_->switchControlMode(*s.motor, static_cast<damiao::Control_Mode>(m));
		return ok ? "OK" : "FAIL";
	}

	if (kw == "PARAM") {
		int rid = 0;
		std::istringstream ps(params);
		if (!(ps >> rid)) return "ERR usage: PARAM <reg_id>";
		float val;
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			val = dm_->read_motor_param(*s.motor, static_cast<uint8_t>(rid));
		}
		std::ostringstream oss;
		oss << std::fixed << std::setprecision(4) << val;
		return oss.str();
	}

	// ---- M1-only commands ---------------------------------------------------

	if (kw == "SETWALL") {
		if (s.id != MotorSlot::SlotId::M1) return "ERR SETWALL is M1-only";
		float mm = 0.0f;
		std::istringstream ps(params);
		if (!(ps >> mm) || mm < 0.0f) return "ERR usage: SETWALL <mm>  (0 = no limit)";
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			s.wall_dist = mm;
		}
		return "OK";
	}

	if (kw == "APPROACH") {
		if (s.id != MotorSlot::SlotId::M1) return "ERR APPROACH is M1-only";
		if (!s.enabled) return "ERR motor not enabled; send ENABLE first";
		float clearance = 0.0f, spd = 0.3f;
		std::istringstream ps(params);
		if (!(ps >> clearance)) return "ERR usage: APPROACH <clearance_mm> [speed_rad_s]";
		ps >> spd;
		if (!approach_wall_slot(s, clearance, spd)) return "ERR SETWALL not configured";
		std::ostringstream oss;
		oss << std::fixed << std::setprecision(4)
			<< "OK clearance=" << clearance << " speed=" << spd;
		return oss.str();
	}

	if (kw == "TOUCHWALL") {
		if (s.id != MotorSlot::SlotId::M1) return "ERR TOUCHWALL is M1-only";
		if (!s.enabled) return "ERR motor not enabled; send ENABLE first";

		float wall_mm = 0.0f, clearance = 0.0f, spd = 0.3f;
		std::string slot_str;
		std::istringstream ps(params);
		if (!(ps >> wall_mm >> slot_str))
			return "ERR usage: TOUCHWALL <wall_dist_mm> <LEFT|CENTER|RIGHT> [clearance_mm] [speed_rad_s]";
		for (auto& c : slot_str) c = static_cast<char>(::toupper(c));
		ps >> clearance >> spd;

		if (clearance < 0.0f) clearance = 0.0f;

		int m2_slot_idx;
		if (slot_str == "LEFT")   m2_slot_idx = -1;
		else if (slot_str == "CENTER") m2_slot_idx = 0;
		else if (slot_str == "RIGHT")  m2_slot_idx = 1;
		else return "ERR usage: TOUCHWALL <wall_dist_mm> <LEFT|CENTER|RIGHT> [clearance_mm] [speed_rad_s]";

		float tool_ext = (m2_slot_idx < 0) ? TOOL_EXT_LEFT_MM
			: (m2_slot_idx > 0) ? TOOL_EXT_RIGHT_MM
			: TOOL_EXT_CENTER_MM;
		float usable_check = wall_mm - clearance - (PASSIVE_EXT_MM + tool_ext);
		float theta_target = (usable_check <= 0.0f)
			? VERTICAL_OFFSET_RAD
			: VERTICAL_OFFSET_RAD + std::asin(std::min(usable_check / ARM_LENGTH_MM, 1.0f));

		touch_wall_slot(s, wall_mm, m2_slot_idx, clearance, spd);

		bool may_limit = false;
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			if (s.wall_dist > 0.0f) {
				float usable_sw = s.wall_dist - PASSIVE_EXT_MM;
				float theta_max_sw = (usable_sw <= 0.0f)
					? VERTICAL_OFFSET_RAD
					: VERTICAL_OFFSET_RAD + std::asin(std::min(usable_sw / ARM_LENGTH_MM, 1.0f));
				may_limit = (theta_target > theta_max_sw);
			}
		}

		std::ostringstream oss;
		oss << std::fixed << std::setprecision(4)
			<< "OK wall=" << wall_mm
			<< " slot=" << slot_str
			<< " clearance=" << clearance
			<< " speed=" << spd;
		if (may_limit) oss << " warn=SETWALL_MAY_LIMIT";
		return oss.str();
	}

	if (kw == "CALIBRATE") {
		if (s.id != MotorSlot::SlotId::M1) return "ERR CALIBRATE is M1-only; M2 uses LR_CALIBRATE";
		if (!s.enabled) return "ERR motor not enabled; send ENABLE first";
		if (!calibrate_arm_slot(s))
			return "ERR calibrate failed: stop not found, holding current position";
		return "OK";
	}

	// ---- M2-only commands ---------------------------------------------------

	if (kw == "LR_CALIBRATE") {
		if (s.id != MotorSlot::SlotId::M2) return "ERR LR_CALIBRATE is M2-only";
		if (!s.enabled) return "ERR motor not enabled; send ENABLE first";
		std::string dir_str;
		std::istringstream ps(params);
		ps >> dir_str;
		for (auto& c : dir_str) c = static_cast<char>(::toupper(c));
		if (!dir_str.empty() && dir_str != "LEFT" && dir_str != "RIGHT")
			return "ERR usage: LR_CALIBRATE [LEFT|RIGHT]";
		lr_calibrate_slot(s, dir_str != "RIGHT");
		return "OK";
	}

	// [2026-08-14 per user] Manual override — LR_CALIBRATE's auto-seek has been
	// unreliable (false-early stops, or seeks that travel huge distances without
	// ever finding resistance). This lets a hand-measured half_range (M2 DISABLE
	// -> move by hand -> M2 MIT 0 0 0 0 0 to refresh Get_Position() -> M2 STATUS
	// at LEFT/CENTER/RIGHT) be applied directly, bypassing the auto-seek entirely.
	// Does NOT touch set_zero — call `M2 ZERO` at the physical center first so
	// this half_range is measured from the right origin.
	if (kw == "SET_HALF_RANGE") {
		if (s.id != MotorSlot::SlotId::M2) return "ERR SET_HALF_RANGE is M2-only";
		float val;
		std::istringstream ps(params);
		if (!(ps >> val)) return "ERR usage: SET_HALF_RANGE <rad>";
		if (val <= 0.0f) return "ERR SET_HALF_RANGE must be > 0";
		s.lr_half_range = val;
		s.lr_calibrated = true;   // trust this manual measurement; INIT will skip auto-seek
		std::ostringstream oss;
		oss << "OK lr_half_range=" << val;
		return oss.str();
	}

	// [2026-08-18 per user] Runtime speed tuning for M1. The right value is still
	// being felt out on the bench, and a rebuild+redeploy per tweak is a slow way
	// to find it. PARK has no command-line speed argument at all, and the GUI's
	// DEPLOY button sends no speed either — so these two setters are the only way
	// to move those without recompiling. Not persisted: a motor_api restart
	// returns to the init() defaults, which is deliberate (a value that felt good
	// once should be promoted into init() rather than living only in RAM).
	if (kw == "SET_PARK_SPEED" || kw == "SET_DEPLOY_SPEED") {
		if (s.id != MotorSlot::SlotId::M1) return "ERR " + kw + " is M1-only";
		float val;
		std::istringstream ps(params);
		if (!(ps >> val)) return "ERR usage: " + kw + " <rad_per_s>";
		if (val <= 0.0f)  return "ERR speed must be > 0";
		if (val > 0.50f)  return "ERR speed too high (max 0.50 rad/s)";
		std::ostringstream oss;
		if (kw == "SET_PARK_SPEED") { s.park_speed   = val; oss << "OK park_speed=" << val; }
		else                        { s.deploy_speed = val; oss << "OK deploy_speed=" << val; }
		// Only DEPLOY runs with gravity (tau_ff is negative, i.e. gravity pulls the
		// arm outward), so it is the direction that can actually run away.
		if (val > 0.20f)
			oss << "  (WARNING: nearing M1_VEL_SAFETY_LIMIT=0.4 — watch for [M1 SAFETY] brake)";
		return oss.str();
	}

	if (kw == "LR_SLOT") {
		if (s.id != MotorSlot::SlotId::M2) return "ERR LR_SLOT is M2-only";
		if (!s.enabled) return "ERR motor not enabled; send ENABLE first";
		std::string slot_str;
		float spd = 1.0f;
		std::istringstream ps(params);
		if (!(ps >> slot_str)) return "ERR usage: LR_SLOT <LEFT|CENTER|RIGHT> [speed_rad_s]";
		for (auto& c : slot_str) c = static_cast<char>(::toupper(c));
		ps >> spd;
		int slot_idx;
		if (slot_str == "LEFT")   slot_idx = -1;
		else if (slot_str == "CENTER") slot_idx = 0;
		else if (slot_str == "RIGHT")  slot_idx = 1;
		else return "ERR usage: LR_SLOT <LEFT|CENTER|RIGHT> [speed_rad_s]";
		// [2026-06-06] Fix 4: return ERR if M2 didn't actually reach the slot.
		// Old code unconditionally printed OK even when M2 was stuck — masked
		// the silent-miss bug observed in bench testing.
		bool ok = lr_move_to_slot_impl(s, slot_idx, spd);
		std::ostringstream oss;
		if (!ok) {
			oss << "ERR LR_SLOT: did not reach " << slot_str
				<< " (final pos=" << std::fixed << std::setprecision(4)
				<< s.motor->Get_Position() << ")";
			return oss.str();
		}
		oss << "OK slot=" << slot_str
			<< " speed=" << std::fixed << std::setprecision(4) << spd;
		return oss.str();
	}

	// ---- user-registered commands -------------------------------------------
	auto it = cmd_map_.find(kw);
	if (it != cmd_map_.end()) return it->second(line);

	return "ERR unknown command: " + kw;
}
