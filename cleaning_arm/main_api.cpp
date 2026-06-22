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
	m1_.hold_kp = 20.0f;
	m1_.hold_kd = 3.0f;
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
	m2_.hold_kp = 2.5f;
	m2_.hold_kd = 1.0f;
	m2_.hold_ki = 0.3f;   // ki*HOLD_I_MAX=1.0 Nm > ~0.8 Nm friction; eliminates ~0.1 rad offset
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

	move_to_slot(s, theta_target, speed_rad_s);

	std::cout << "[M1 touch_wall_slot] theta_target=" << theta_target << std::endl;
	return true;
}

// ============================================================
//  go_home_slot()  -- speed-step to 0, position-based arrival
// ============================================================
void DamiaoAPI::go_home_slot(MotorSlot& s)
{
	const float SPEED = 0.45f;           // halved: reduces ramp tracking lag (lag ≈ vel/kp)
	const float DT = 0.02f;
	const float ARRIVE_TOL = 0.05f;
	const float VEL_TOL = 0.02f;        // rad/s — treat motor as stopped below this threshold
	const int   ARRIVE_CNT = 3;
	const int   MAX_LOOPS = 150;        // 3s ramp budget (was 100)
	const int   MAX_SETTLE = 100;       // 2s extra settle after ramp reaches 0

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

	// M1: ramp gravity feedforward from hold value to 0 as setpoint approaches home,
	// preventing initial drop caused by removing gravity compensation at retract start.
	float init_tau_ff = 0.0f;
	if (s.id == MotorSlot::SlotId::M1) {
		std::lock_guard<std::mutex> lk(motor_mutex_);
		init_tau_ff = s.hold_tau_ff;
	}

	// Warmup: 3 frames at hold_pos before ramping. Prevents transient drop at control
	// handoff: the first ramp step would otherwise produce kp*(-step) + tau_ff which
	// briefly lets gravity win. s.id and start_pos are constant throughout go_home_slot.
	const float tau_warm = (s.id == MotorSlot::SlotId::M1
	                        && start_pos > VERTICAL_OFFSET_RAD + 0.01f)
	                       ? init_tau_ff : 0.0f;
	for (int w = 0; w < 3; ++w) {
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			dm_->control_mit(*s.motor, s.hold_kp, s.hold_kd, cur_cmd, 0.0f, tau_warm);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	int arrive_cnt = 0;

	for (int i = 0; i < MAX_LOOPS; ++i) {
		float pos = s.motor->Get_Position();
		float vel = s.motor->Get_Velocity();

		if (std::abs(pos) < ARRIVE_TOL) ++arrive_cnt; else arrive_cnt = 0;
		if (arrive_cnt >= ARRIVE_CNT) break;
		if (std::abs(pos) < ARRIVE_TOL * 4 && std::abs(vel) < VEL_TOL) break;

		float step = SPEED * DT;
		float diff = 0.0f - cur_cmd;
		if (std::abs(diff) <= step) cur_cmd = 0.0f;
		else cur_cmd += (diff > 0.0f ? step : -step);

		// Gravity feedforward: ramp from init_tau_ff (at start_pos) to 0 at VERTICAL_OFFSET_RAD.
		// Below vertical the arm is below neutral; PD handles the residual gravity.
		float tau_ff = 0.0f;
		if (s.id == MotorSlot::SlotId::M1
				&& cur_cmd > VERTICAL_OFFSET_RAD
				&& start_pos > VERTICAL_OFFSET_RAD + 0.01f) {
			tau_ff = init_tau_ff
				   * (cur_cmd - VERTICAL_OFFSET_RAD)
				   / (start_pos - VERTICAL_OFFSET_RAD);
		}

		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			dm_->control_mit(*s.motor,
				s.hold_kp, s.hold_kd,
				cur_cmd, 0.0f, tau_ff);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	// Ramp complete (cur_cmd==0). Wait for physical motor to converge before releasing.
	// Without this, callers see move done but motor still coasting toward target.
	if (arrive_cnt < ARRIVE_CNT) {
		arrive_cnt = 0;
		for (int j = 0; j < MAX_SETTLE; ++j) {
			float pos = s.motor->Get_Position();
			if (std::abs(pos) < ARRIVE_TOL) ++arrive_cnt; else arrive_cnt = 0;
			if (arrive_cnt >= ARRIVE_CNT || std::abs(s.motor->Get_Velocity()) < VEL_TOL) break;
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				dm_->control_mit(*s.motor, s.hold_kp, s.hold_kd, 0.0f, 0.0f, 0.0f);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
	}

	{
		std::lock_guard<std::mutex> lk(motor_mutex_);
		s.hold_pos = 0.0f;
		s.hold_tau_ff = 0.0f;   // clear stale gravity proxy; prevents lurch on next TOUCHWALL
		s.move_cur = 0.0f;
		s.move_target = 0.0f;
		s.hold_err_integral = 0.0f;
	}
	s.hold_en = true;    // hold at 0 after HOME, matching calibrate_arm_slot() pattern
	s.move_act = false;
	if (was_enabled) s.enabled = true;
}

// ============================================================
//  lr_calibrate_slot()  -- seek mechanical stop, back off ZERO_OFFSET, set zero
//  M2 only (called from dispatch_motor after id check)
// ============================================================
void DamiaoAPI::lr_calibrate_slot(MotorSlot& s, bool seek_left)
{
	const float dir = seek_left ? 1.0f : -1.0f;
	// [2026-06-09j] SEEK_KP 0.3 → 3.0: M2 calibration Phase 1 經常誤觸發
	// "Resistance at stop" (pos 幾乎沒動就判 stop)，原因是 kp=0.3 算出的 tau
	// (~0.6 Nm) 不夠克服 static friction → 馬達靜止 → 初始 PD transient tau
	// 被誤認為「碰到 stop」。拉高 kp 讓馬達真的能推到 mechanical stop。
	// [2026-06-09k] 3.0 太強撞 stop tau 飆 8 Nm，再降到 1.5 + kd 提高到 1.0
	// (對齊 M1 tuning)、撞擊較柔和。
	// [2026-06-09l] 1.5 仍有甩頭感、撞擊 ~4 Nm，再降到 1.0 完全對齊 M1。
	const float SEEK_KP = 1.0f;
	const float SEEK_KD = 1.0f;
	const float SEEK_RANGE = 2.8f;
	const float STOP_VEL = 0.04f;
	const float RESIST_TAU = 0.6f;
	const int   STOP_CNT = 3;
	const float MAX_TRAVEL = 1.5f;
	const int   MAX_LOOPS = 75;
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

	while (loop_count < MAX_LOOPS) {
		float cur_pos = s.motor->Get_Position();
		float cur_vel = s.motor->Get_Velocity();
		float cur_tau = s.motor->Get_tau();

		if (std::abs(cur_vel) > 0.10f) ever_moved = true;

		// stale-read detector: pos frozen → likely CAN receive() not updating state_q
		if (std::abs(cur_pos - prev_pos) < 0.002f) ++stale_cnt; else stale_cnt = 0;
		prev_pos = cur_pos;
		if (stale_cnt == 10)
			std::cerr << "[" << s.name << " calibrate] WARN: pos frozen at "
				<< std::fixed << std::setprecision(4) << cur_pos
				<< " for 10 loops  vel=" << cur_vel << "  tau=" << cur_tau
				<< "  (possible stale CAN reads or motor not responding)\n";

		if (dir * (cur_pos - p_start) >= MAX_TRAVEL) {
			vel_capture = 0.0f;
			p_stop = cur_pos;
			{
				std::lock_guard<std::mutex> lk(motor_mutex_);
				dm_->control_mit(*s.motor, HOLD_KP, HOLD_KD, p_stop, 0.0f, 0.0f);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			std::cerr << "[" << s.name << " calibrate] MAX_TRAVEL exceeded at pos="
				<< cur_pos << ", set_zero aborted\n";
			s.hold_pos = p_stop;
			s.hold_en = true;
			s.move_act = false;
			if (was_enabled) s.enabled = true;
			if (was_peer) peer.enabled = true;
			return;
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
			// the motor stops responding to frames.  Phase 2 starts immediately.
			std::cout << "[" << s.name << " calibrate] Resistance at pos=" << cur_pos
				<< "  tau=" << cur_tau << "\n";
			break;
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
			std::cout << "[" << s.name << " calibrate] seek"
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
	if (loop_count >= MAX_LOOPS) {
		p_stop = s.motor->Get_Position();
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);
			dm_->control_mit(*s.motor, HOLD_KP, HOLD_KD, p_stop, 0.0f, 0.0f);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		std::cerr << "[" << s.name << " calibrate] Phase 1 timeout"
			<< std::fixed << std::setprecision(4)
			<< "  final_pos=" << p_stop
			<< "  disp=" << (p_stop - p_start)
			<< "  vel=" << s.motor->Get_Velocity()
			<< "  tau=" << s.motor->Get_tau()
			<< "  stale_cnt=" << stale_cnt
			<< "  loops=" << loop_count
			<< "  dir=" << dir << "\n";
		s.hold_pos = p_stop;
		s.hold_en = true;
		s.move_act = false;
		if (was_enabled) s.enabled = true;
		if (was_peer) peer.enabled = true;
		return;
	}
	else {
		std::cout << "[" << s.name << " calibrate] Stop at pos=" << p_stop
			<< "  target zero=" << (p_stop - dir * ZERO_OFFSET) << "\n";
	}

	// ---- Phase 2: back off to zero point ------------------------------------
	std::cout << "[" << s.name << " calibrate] Phase 2: backing off...\n";
	float dyn_buffer = ZERO_OFFSET + LR_VEL_BUFFER_K * vel_capture;
	std::cout << "[" << s.name << " calibrate] vel_capture=" << vel_capture
		<< " dyn_buffer=" << dyn_buffer << "\n";
	float target = p_stop - dir * dyn_buffer;

	// Direct MIT with moving setpoint: s.enabled stays false (set in Phase 1) so
	// feedback_loop never interferes. P2_KP=8 matches hold_kp so the motor arrives
	// at target gently and is already settled when set_zero is called — prevents
	// residual velocity from creating hold-coordinate mismatch (hard rightward rush).
	{
		const float P2_KP    = 12.5f;
		const float P2_SPEED = 0.6f;
		const float CONV_TOL = 0.06f;
		const float DT       = 0.02f;
		// [2026-06-06] Fix 3: retry parameters when first attempt doesn't converge.
		const float P2_KP_RETRY    = 25.0f;   // stronger push (was 20 — still under-converged)
		const float CONV_TOL_RETRY = 0.15f;   // bench-realistic for M2 friction
		const float VEL_SETTLE_THRESHOLD = 0.05f;   // before set_zero
		const int   VEL_SETTLE_MAX_LOOPS = 50;      // = 500ms (was 200ms)
		const float VEL_SETTLE_KP = 30.0f;          // hold-hard during settle, no damping below

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
			if (settled_vel < VEL_SETTLE_THRESHOLD && settle_err < CONV_TOL_RETRY) break;
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

	std::cout << "[" << s.name << " calibrate] Done. Stop at ~"
		<< (seek_left ? "+" : "-") << ZERO_OFFSET << " rad.\n";
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
	if (slot < 0) target = -ZERO_OFFSET + 0.1f;   // LEFT  = -0.7
	else if (slot > 0) target = ZERO_OFFSET - 0.1f;   // RIGHT = +0.7
	else               target = 0.0f;           // CENTER

	// [2026-06-11e] 實驗：MIT_KP 10 → 20、加 FRICTION_TAU 1.5、測 motor 是否真飽和
	const float MIT_KP   = 20.0f;
	const float FRICTION_TAU = 1.5f;
	// [2026-06-09aa] reference tuning: CONV_TOL=0.1, MAX_LOOPS=100 (2s)
	// [2026-06-09bb] CONV_TOL 0.1 → 0.15：motor LEFT 方向實測能到 ±0.5、
	// target -0.6 用 0.1 容忍 (5.7°) 卡很可惜 (差 0.012 rad 就過關)。
	// 0.15 (8.6°) 給夠 margin、一次 converged。sweep 對角度精度要求不高。
	const float CONV_TOL = 0.15f;
	const float DT       = 0.02f;
	const int   MAX_LOOPS = 100;   // 2s timeout


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
		const float probe_setpt = cur_cmd + (target > cur_cmd ? 1.0f : -1.0f);
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
			// Refresh cur_cmd after warmup (motor may have moved during warmup)
			cur_cmd = s.motor->Get_Position();
		}
	}

	const float start_pos    = cur_cmd;

	// [2026-06-09s] Original simple loop: trajectory ramps cur_cmd, PD with
	// MIT_KP=12. No velocity FF, no stabilize frames, no distance-aware timeout.
	// hold_pos = target ALWAYS (let hold mode pull motor to target after move loop).
	bool converged = false;
	for (int j = 0; j < MAX_LOOPS; ++j) {
		float diff = target - cur_cmd;
		float step = std::max(speed_rad_s, 0.01f) * DT;
		if (std::abs(diff) <= step) cur_cmd = target;
		else cur_cmd += (diff > 0.0f ? step : -step);

		// [2026-06-11e] 加回 friction FF：用 motor pos vs target 判斷方向
		// 持續推到 motor 真的到 target ±CONV_TOL 內。實驗用、看硬體是否飽和。
		float motor_pos_now = s.motor->Get_Position();
		float pos_err_to_target = target - motor_pos_now;
		float tau_ff = 0.0f;
		if (std::abs(pos_err_to_target) > CONV_TOL) {
			tau_ff = (pos_err_to_target > 0.0f ? FRICTION_TAU : -FRICTION_TAU);
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
	while (running_) {
		{
			std::lock_guard<std::mutex> lk(motor_mutex_);

			auto  t_now = std::chrono::steady_clock::now();
			float dt = std::chrono::duration<float>(t_now - t_prev).count();
			dt = std::max(dt, 0.001f);
			t_prev = t_now;

			for (MotorSlot* s : { &m1_, &m2_ }) {
				if (!s->enabled) continue;

				if (s->move_act) {
					float diff = s->move_target - s->move_cur;
					float step = s->move_speed * MOVE_DT;
					if (std::abs(diff) <= step) {
						s->move_cur = s->move_target;
						s->move_act = false;
						s->hold_err_integral = 0.0f;
						if (s->id == MotorSlot::SlotId::M1) {
							// M1 has gravity: use actual position so hold engages with
							// zero PD error and avoids backward pull from hold_tau_ff
							// contamination while the motor is still moving.
							s->hold_pos = s->motor->Get_Position();
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
					// When ARM_MASS_KG is set, switch to model-based compensation.
					float tau_ff_move = (s->id == MotorSlot::SlotId::M1) ? s->move_tau_ff : 0.0f;
					if (s->id == MotorSlot::SlotId::M1 && ARM_MASS_KG > 0.0f) {
						float pos_now_move = s->motor->Get_Position();
						tau_ff_move = ARM_MASS_KG * 9.81f * (ARM_LENGTH_MM / 1000.0f)
							* std::sin(pos_now_move - VERTICAL_OFFSET_RAD);
					}
					dm_->control_mit(*s->motor,
						s->hold_kp, s->hold_kd,
						s->move_cur, 0.0f, tau_ff_move);

				}
				else if (s->hold_en) {
					float pos_now = s->motor->Get_Position();

					// M1 gravity proxy (tau at last HOLD or move→hold); overridden by ARM_MASS_KG model.
					float tau_ff = (s->id == MotorSlot::SlotId::M1) ? s->hold_tau_ff : 0.0f;
					if (s->id == MotorSlot::SlotId::M1 && ARM_MASS_KG > 0.0f) {
						tau_ff = ARM_MASS_KG * 9.81f * (ARM_LENGTH_MM / 1000.0f)
							* std::sin(pos_now - VERTICAL_OFFSET_RAD);
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

	go_home_slot(m1_);
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
	lr_calibrate_slot(m2_, /*seek_left=*/true);

	// ---- DIAG: 记录校准后 M2 状态 ----
	float m2_pos_after = m2_.motor->Get_Position();
	float m2_vel_after = m2_.motor->Get_Velocity();
	float m2_tau_after = m2_.motor->Get_tau();
	std::cout << "[INIT DIAG] M2 after calibrate: pos=" << std::fixed << std::setprecision(4)
		<< m2_pos_after << " vel=" << m2_vel_after << " tau=" << m2_tau_after
		<< " hold_pos=" << m2_.hold_pos << "\n";

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
	float wall_mm = 0.0f, clearance = 0.0f, spd = 0.35f;
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
	// go_home_slot reads hold_tau_ff as initial gravity feedforward and decays it to 0,
	// preventing the initial positive drop. It clears hold_tau_ff at completion.
	go_home_slot(m1_);
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
	wait_for_move(m1_);   // best-effort; timeout = arm clamped by SETWALL (warn below)

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
	if (m1_.enabled) go_home_slot(m1_);
	disable_slot(m1_);

	if (m2_.enabled) go_home_slot(m2_);
	disable_slot(m2_);

	return "OK";
}

std::string DamiaoAPI::cmd_status_sequence()
{
	float pos_1, vel_1, tau_1;
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
	}
	oss << std::fixed << std::setprecision(4)
		<< "[M1] pos=" << pos_1 << " vel=" << vel_1 << " tau=" << tau_1
		<< " hold=" << (m1_.hold_en.load() ? 1 : 0)
		<< " moving=" << (m1_.move_act.load() ? 1 : 0) << " | "
		<< "[M2] pos=" << pos_2 << " vel=" << vel_2 << " tau=" << tau_2
		<< " hold=" << (m2_.hold_en.load() ? 1 : 0)
		<< " moving=" << (m2_.move_act.load() ? 1 : 0);

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
