#include "DM2J_RS570.h"
#include "log_utils.h"
#include <cstring>
#include <thread>
#include <chrono>

//=========== init ===========

DM2J_RS570::DM2J_RS570()
{
	client = nullptr;
	useExternalClient = false;
	debug_mode = false;
	slaveID = 1;
	_log_tag = "DM2J:?";
}

DM2J_RS570::~DM2J_RS570()
{
	if (!useExternalClient && client)
	{
		client->close();
		delete client;
	}
}

//=========== utility: validate one RTU frame ===========
//
// [2026-08-28] Added by the driver audit. Every read site previously did
// `receiveData(rx, 32, 200)` followed by nothing but a length check — a
// bit-flipped frame was parsed as if it were valid, and the wrong position /
// status / error code went straight to the caller with no indication.
//
// Verifies the RTU CRC and the slave id. Safe for every call site that uses
// it — the one broadcast path (writeSingle_sync, slave 0x00) goes through
// sendRecv() instead, so it is unaffected by the address check.
int DM2J_RS570::validate_frame_(const uint8_t* rx, int len, int min_len)
{
	if (len < min_len) return -1;

	const uint16_t rx_crc = (uint16_t)rx[len - 2] | ((uint16_t)rx[len - 1] << 8);
	if (crc16(rx, len - 2) != rx_crc) {
		LOG_ERR(_log_tag, "reply CRC mismatch (%d bytes) — frame dropped", len);
		return -1;
	}

	// A reply addressed to a different slave carries a perfectly valid CRC, so
	// the check above cannot catch it. This matters here: the arm rail is
	// slave 14 on cli_20_ (moved back from cli_22_ on 2026-08-28), sharing the
	// bus with ZDT pushers 5-8 and PQW 12, and bus contention on that gateway
	// is a known, logged symptom. Safe to check at this level — the broadcast
	// path (writeSingle_sync, slave 0x00) goes through sendRecv(), not here.
	if (rx[0] != (uint8_t)slaveID) {
		LOG_ERR(_log_tag, "reply slave %d != %d — frame dropped",
		        (int)rx[0], (int)slaveID);
		return -1;
	}

	// All six read sites that use this helper issue FC 0x03; without this check
	// an exception reply (0x83) is parsed as if bytes 3-4 held register data,
	// so a refused read is reported to the caller as a value.
	if (rx[1] != 0x03) {
		LOG_ERR(_log_tag, "reply FC 0x%02X (expected 0x03) — frame dropped", (int)rx[1]);
		return -1;
	}
	return len;
}

//=========== utility: atomic transaction ===========
//
// 🔴 [2026-09-01] 見標頭的完整說明。取代原本的 drainRx + sendData + recv_frame_
// 三段式 —— 那個組合在 send 與 recv 之間會放開 socket_mtx，共用匯流排的別條
// 執行緒可以把自己的交易插進來造成回覆錯位，而且抓不到「在 recv 窗口內才抵達」
// 的遲到回覆（＝永久失步，只有重連救得回來）。
int DM2J_RS570::txn_frame_(const uint8_t* tx, int tx_len, uint8_t* rx, int rx_size, int min_len,
                           int recv_timeout_ms)
{
	// [2026-08-29] Null-client guard: the constructor leaves `client` as nullptr
	// and only init() sets it, so a call on an un-init'd (or failed-init)
	// instance dereferences nullptr and takes the whole process down.
	// Application layers already gate these calls, but that is the caller
	// remembering to be careful — the driver must not be a landmine.
	if (!client) return -1;

	const int len = client->sendAndReceive((const char*)tx, tx_len,
	                                       (char*)rx, rx_size, 200, recv_timeout_ms);
	if (len <= 0) return -1;
	return validate_frame_(rx, len, min_len);
}

bool DM2J_RS570::init(const std::string& ip, int port, int ID, bool debug)
{
	debug_mode = debug;
	slaveID = ID;
	_log_tag = "DM2J:" + std::to_string(ID);

	client = new TCP_client();
	if (!client->connectToServer(ip, port))
	{
		LOG_ERR(_log_tag, "connect failed %s:%d", ip.c_str(), port);
		return true;
	}

	useExternalClient = false;
	return false;
}

bool DM2J_RS570::init(TCP_client& extClient, int ID, bool debug)
{
	client = &extClient;
	useExternalClient = true;
	debug_mode = debug;
	slaveID = ID;
	_log_tag = "DM2J:" + std::to_string(ID);
	return false;
}

//=========== control: Speed Move ===========

bool DM2J_RS570::speed_move(int pr_num, int mode, int rpm, int pos)
{
	uint16_t pos_hi = (pos >> 16) & 0xFFFF;
	uint16_t pos_lo = pos & 0xFFFF;

	std::vector<uint16_t> block =
	{
		(uint16_t)mode,       // PRx.00
		pos_hi,               // PRx.01
		pos_lo,               // PRx.02
		(uint16_t)rpm,        // PRx.03
		(uint16_t)100,        // PRx.04 acc
		(uint16_t)100,        // PRx.05 dec
		(uint16_t)0,          // PRx.06 dwell (stop dwell time ms)
		(uint16_t)0           // PRx.07 special (path linking)
	};

	// [2026-08-28] 兩個寫入都要算進回傳值。只回最後一個的結果，比原本的 void
	// 更誤導 —— 呼叫端會以為 false 代表「整件事成功」，其實 block 寫入失敗被吞了。
	bool err = writeMulti(0x6200 + pr_num * 8, block);
	uint16_t trig = 0x10 | (pr_num & 0x0F);
	err |= writeSingle(0x6002, trig);
	return err;
}

bool DM2J_RS570::speed_move_stop()
{
	return writeSingle(0x6002, 0x0040);
}

//=========== control: PR Move ===========

bool DM2J_RS570::PR_move_set(int pr_num, int mode, int rpm, int pos, int acc, int dec)
{
	uint16_t base = 0x6200 + (pr_num * 8);

	uint16_t pos_hi = (uint16_t)(pos >> 16);
	uint16_t pos_lo = (uint16_t)(pos & 0xFFFF);

	std::vector<uint16_t> block =
	{
		(uint16_t)mode,   // PRx.00
		pos_hi,           // PRx.01
		pos_lo,           // PRx.02
		(uint16_t)rpm,    // PRx.03
		(uint16_t)acc,    // PRx.04
		(uint16_t)dec,    // PRx.05
		(uint16_t)0,      // PRx.06 dwell (stop dwell time ms)
		(uint16_t)0       // PRx.07 special (path linking)
	};

	// [2026-08-28] Was void: writeMulti's result died here, so no caller above
	// could ever tell a write apart from a no-response. See the header comment.
	return writeMulti(base, block);
}
bool DM2J_RS570::PR_trigger(int pr_num)
{
	uint16_t trig = 0x10 | (pr_num & 0x0F);
	return writeSingle(0x6002, trig);
}
bool DM2J_RS570::PR_trigger_sync(int pr_num)
{
	uint16_t trig = 0x10 | (pr_num & 0x0F);
	return writeSingle_sync(0x6002, trig);
}

//=========== control: PR Move (cm) ===========

// mode => 0 relative, 1 absolute
//=========== 機構標定：cm <-> pulse ===========
// [2026-08-28] 這一層以前不存在，cm↔pulse 直接寫死「1 圈 = 1 cm」散在 5 個地方。
// 上滑台實測 7.731 cm/圈（皮帶軸，三點量測 + 一次預測性驗證命中），
// 也就是每個 cm 指令都走了 7.7 倍 —— 而驅動器只數脈衝，回報的永遠是漂亮的
// 整數，log、狀態、位置回讀三邊都看不出來。

void DM2J_RS570::set_lead_cm_per_rev(double cm_per_rev)
{
	if (cm_per_rev <= 0.0) {
		LOG_ERR(_log_tag, "set_lead_cm_per_rev(%.4f) ignored — must be > 0", cm_per_rev);
		return;
	}
	lead_cm_per_rev_ = cm_per_rev;
	LOG_INF(_log_tag, "lead = %.4f cm/rev", cm_per_rev);
}

void DM2J_RS570::set_travel_limit_cm(double lo_cm, double hi_cm)
{
	travel_lo_cm_ = lo_cm;
	travel_hi_cm_ = hi_cm;
	if (lo_cm == hi_cm) LOG_INF(_log_tag, "travel limit disabled");
	else                LOG_INF(_log_tag, "travel limit = [%.2f, %.2f] cm", lo_cm, hi_cm);
}

int DM2J_RS570::cm_to_pulse_(double cm, uint16_t ppr) const
{
	return (int)(cm / lead_cm_per_rev_ * (double)ppr);
}

double DM2J_RS570::pulse_to_cm_(int32_t pulse, uint16_t ppr) const
{
	if (ppr == 0) return 0.0;
	return (double)pulse / (double)ppr * lead_cm_per_rev_;
}

// Reject instead of grinding. Deliberately LOUD: the whole point is that an
// out-of-range command previously produced no error anywhere — not from the
// drive (it just counts pulses), not from the app, not in the log.
bool DM2J_RS570::travel_reject_(double cm, const char* what)
{
	if (travel_lo_cm_ == travel_hi_cm_) return false;          // 停用
	if (cm >= travel_lo_cm_ && cm <= travel_hi_cm_) return false;
	LOG_ERR(_log_tag, "%s %.3f cm REJECTED — outside travel limit [%.2f, %.2f]",
	        what, cm, travel_lo_cm_, travel_hi_cm_);
	return true;
}

bool DM2J_RS570::PR_move_cm(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec)
{
	// Modbus read with up to 3 attempts — absorbs transient gateway / bus
	// hiccups instead of failing the whole move on one bad frame.
	auto read_status_retry = [this](uint32_t& s) -> bool {
		for (int a = 0; a < 3; ++a) {
			if (!read_status(s)) return false;          // ok
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
		}
		return true;                                    // failed after retries
	};

	// [2026-08-28] Range check BEFORE any bus traffic — cheapest place to stop a
	// command that would drive the mechanism into its end stop.
	if (travel_reject_(pos_cm, "PR_move_cm")) return true;

	// --- pulse-per-rev (with retry) ---
	uint16_t ppr = 0;
	bool ppr_ok = false;
	for (int a = 0; a < 3; ++a) {
		if (!read_pulse_per_rev(ppr) && ppr != 0) { ppr_ok = true; break; }
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}
	if (!ppr_ok)
	{
		LOG_ERR(_log_tag, "PPR read failed (3 attempts)");
		return true;
	}

	const int pos_pulse = cm_to_pulse_(pos_cm, ppr);
	LOG_DBG(_log_tag, "PR_move_cm %.3f cm -> %d pulses (PPR=%u, lead=%.4f)", pos_cm, pos_pulse, ppr, lead_cm_per_rev_);

	// Expected absolute encoder position after the move — used by the
	// !ever_busy check to tell a genuine no-op from a dropped trigger.
	//   mode 1 = absolute → target is pos_pulse directly.
	//   mode 0 = relative → target = start position + pos_pulse (need start).
	int32_t expected_pos = pos_pulse;
	if (mode == 0)
	{
		int32_t start_pos = 0;
		bool have_start = false;
		for (int a = 0; a < 3; ++a) {
			if (!read_motor_position(start_pos)) { have_start = true; break; }
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
		}
		if (!have_start)
		{
			LOG_ERR(_log_tag, "start position read failed — cannot run relative move safely");
			return true;
		}
		expected_pos = start_pos + pos_pulse;
	}
	const int32_t pos_tol = (int32_t)(0.3 / lead_cm_per_rev_ * ppr);   // ~0.3 cm — far below any real move delta

	// Trigger + wait, with re-trigger retry if the trigger has no effect.
	const int trigger_retry_max = 3;
	for (int attempt = 1; attempt <= trigger_retry_max; ++attempt)
	{
		PR_move_set(pr_num, mode, rpm, pos_pulse, acc, dec);
		PR_trigger(pr_num);

		uint32_t st = 0;

		// Phase 1: wait for path_done (0x0020) to CLEAR — drive accepted the
		// trigger and went busy. Edge-detect avoids a stale "done" bit making
		// Phase 2 return instantly.
		const int busy_wait_ms = 500;
		int busy_elapsed = 0;
		bool ever_busy = false;
		while (busy_elapsed < busy_wait_ms)
		{
			if (read_status_retry(st))
			{
				LOG_ERR(_log_tag, "read status failed during PR busy-wait");
				return true;
			}
			if (st & 0x0001)
			{
				LOG_ERR(_log_tag, "PR fault detected during busy-wait (status=0x%08X)", st);
				return true;
			}
			if (!(st & 0x0020)) { ever_busy = true; break; }
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			busy_elapsed += 20;
		}

		if (!ever_busy)
		{
			// path_done never cleared. The old code blindly assumed "no-op,
			// already at target" — which silently swallowed dropped triggers
			// (drive busy / Modbus hiccup) leaving the motor stuck at the
			// previous target. Disambiguate by ACTUAL position:
			//   at target     → genuine no-op (or move too fast to observe) → ok
			//   not at target → trigger was dropped → re-trigger
			int32_t cur = 0;
			if (read_motor_position(cur))
			{
				LOG_ERR(_log_tag, "PR !ever_busy and position read failed");
				return true;
			}
			int32_t diff = cur - expected_pos;
			if (diff < 0) diff = -diff;
			if (diff <= pos_tol)
			{
				LOG_DBG(_log_tag, "PR no-op — already at target (pos=%d target=%d)",
				        cur, expected_pos);
				return false;
			}
			LOG_WRN(_log_tag, "PR trigger had no effect (pos=%d target=%d) — re-trigger %d/%d",
			        cur, expected_pos, attempt, trigger_retry_max);
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;   // outer loop → re-trigger
		}

		// Phase 2: wait for cmd_done + path_done to SET (motion complete).
		const int timeout_ms = 20000;
		int elapsed = 0;
		while (elapsed < timeout_ms)
		{
			if (read_status_retry(st))
			{
				LOG_ERR(_log_tag, "read status failed during PR wait");
				return true;
			}
			if (st & 0x0001)
			{
				LOG_ERR(_log_tag, "PR fault detected (status=0x%08X)", st);
				return true;
			}
			// [2026-05-22] per-poll status DBG print disabled — too verbose
			// (spams every ~50ms during a motion wait). Uncomment to restore.
			//if (debug_mode) print_status(st);
			if ((st & 0x0010) && (st & 0x0020))
			{
				LOG_DBG(_log_tag, "PR motion completed");
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			elapsed += 50;
		}
		LOG_ERR(_log_tag, "PR timeout waiting motion done (%d ms)", timeout_ms);
		return true;
	}

	LOG_ERR(_log_tag, "PR trigger had no effect after %d attempts", trigger_retry_max);
	return true;
}

bool DM2J_RS570::PR_move_cm_nowait(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec)
{
	if (travel_reject_(pos_cm, "PR_move_cm_nowait")) return true;
	uint16_t ppr = 10000;
	// PPR skipped to save one round-trip; assumes default 10000
	int pos_pulse = cm_to_pulse_(pos_cm, ppr);
	LOG_DBG(_log_tag, "PR_move_cm_nowait %.3f cm -> %d pulses (PPR=%u, lead=%.4f)", pos_cm, pos_pulse, ppr, lead_cm_per_rev_);

	// [2026-08-28] Was an unconditional `return false` (= success). The caller
	// added on main (arm_sweep_fire_nowait_) tests this value to decide whether
	// the rail actually moved — against a constant, so "3 writes all failed"
	// could never be detected. Propagate for real.
	bool err = PR_move_set(pr_num, mode, rpm, pos_pulse, acc, dec);
	err |= PR_trigger(pr_num);
	return err;
}

bool DM2J_RS570::PR_move_cm_set(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec)
{
	if (travel_reject_(pos_cm, "PR_move_cm_set")) return true;
	uint16_t ppr = 10000;
	int pos_pulse = cm_to_pulse_(pos_cm, ppr);
	LOG_DBG(_log_tag, "PR_move_cm_set %.3f cm -> %d pulses (PPR=%u, lead=%.4f)", pos_cm, pos_pulse, ppr, lead_cm_per_rev_);

	return PR_move_set(pr_num, mode, rpm, pos_pulse, acc, dec);   // [2026-08-28] was unconditional false
}

bool DM2J_RS570::PR_move_cm_trigger_all(int pr_num)
{
	PR_trigger_sync(pr_num);

	const int timeout_ms = 20000;
	int elapsed = 0;

	while (elapsed < timeout_ms)
	{
		uint32_t st = 0;

		if (read_status(st))
		{
			LOG_ERR(_log_tag, "read status failed during trigger_all wait");
			return true;
		}

		// [2026-05-22] per-poll status DBG print disabled — too verbose.
		//if (debug_mode)
		//	print_status(st);

		bool cmd_done = st & 0x0010;
		bool path_done = st & 0x0020;
		bool fault = st & 0x0001;

		if (fault)
		{
			LOG_ERR(_log_tag, "PR fault detected (status=0x%08X)", st);
			return true;
		}

		if (cmd_done && path_done)
		{
			LOG_DBG(_log_tag, "PR motion completed (trigger_all)");
			return false;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		elapsed += 50;
	}

	LOG_ERR(_log_tag, "PR timeout waiting motion done (trigger_all, %d ms)", timeout_ms);
	return true;
}


//=========== control: JOG ===========

bool DM2J_RS570::jog_forward()
{
	return writeSingle(0x1801, 0x4001);
}

bool DM2J_RS570::jog_reverse()
{
	return writeSingle(0x1801, 0x4002);
}

bool DM2J_RS570::jog_stop()
{
	return writeSingle(0x6002, 0x0040);
}

bool DM2J_RS570::set_jog_speed(int rpm)
{
	return writeSingle(0x01E1, (uint16_t)rpm);
}

bool DM2J_RS570::set_jog_acc(int acc_ms)
{
	return writeSingle(0x01E7, (uint16_t)acc_ms);
}

bool DM2J_RS570::set_jog_dec(int dec_ms)
{
	return writeSingle(0x01E7, (uint16_t)dec_ms);   // RS485 JOG shares acc/dec register (Pr6.03, 0x01E7)
}

//=========== control: Homing ===========

bool DM2J_RS570::home_set_mode(uint16_t mode_bits)
{
	return writeSingle(0x600A, mode_bits);
}

bool DM2J_RS570::home_set_high_speed(uint16_t rpm)
{
	return writeSingle(0x600F, rpm);
}

bool DM2J_RS570::home_set_low_speed(uint16_t rpm)
{
	return writeSingle(0x6010, rpm);
}

bool DM2J_RS570::home_set_acc_time(uint16_t v)
{
	return writeSingle(0x6011, v);
}

bool DM2J_RS570::home_set_dec_time(uint16_t v)
{
	return writeSingle(0x6012, v);
}

bool DM2J_RS570::home_set_overrun(uint16_t v)
{
	return writeSingle(0x6015, v);
}

bool DM2J_RS570::home_start()
{
	return writeSingle(0x6002, 0x0020);
}

bool DM2J_RS570::home_set_current_pos_zero()
{
	return writeSingle(0x6002, 0x0021);
}

//=========== read ===========

bool DM2J_RS570::read_version(uint16_t& ver1, uint16_t& ver2)
{
	if (!client) return true;
	uint8_t tx[8] =
	{
		(uint8_t)slaveID,
		0x03,
		0x80, 0x00,     // start reg
		0x00, 0x02,     // read 2 registers
		0, 0
	};

	uint16_t crc = crc16(tx, 6);
	tx[6] = crc & 0xFF;
	tx[7] = crc >> 8;

	uint8_t rx[32];
	int len = txn_frame_(tx, 8, rx, sizeof(rx), 9);
	if (len < 0) return true;

	ver1 = (rx[3] << 8) | rx[4];
	ver2 = (rx[5] << 8) | rx[6];
	return false;
}

bool DM2J_RS570::read_status(uint32_t& status)
{
	if (!client) return true;
	// 0x1003 is a SINGLE 16-bit status register per DM2J-RS V1.0 manual §5.3.2:
	//   Bit0=FAULT  Bit1=ENABLE  Bit2=RUN  Bit4=CMD_DONE  Bit5=PATH_DONE  Bit6=HOME_DONE
	// Previous code read 2 registers and packed as (hi<<16)|lo, putting the real
	// flags into bits 16-22 of the returned uint32_t. Callers then masked with
	// 0x0010 / 0x0020 / 0x0001 (low 16 bits) and never saw any flag — every
	// PR_move_cm / dm2j_wait_done_ poll timed out.
	// Now: read only 0x1003, store 16-bit value in low 16 bits of uint32_t.
	// Legacy bit masks (0x0001 / 0x0002 / 0x0004 / 0x0010 / 0x0020) are now correct.
	// HOME_DONE mask changed from 0x10000 (never set) to 0x0040 — see print_status.
	uint8_t tx[8];

	tx[0] = slaveID;
	tx[1] = 0x03;
	tx[2] = 0x10;     // 0x1003
	tx[3] = 0x03;
	tx[4] = 0x00;
	tx[5] = 0x01;     // read 1 register (16-bit) — manual says status is single reg

	uint16_t crc = crc16(tx, 6);
	tx[6] = crc & 0xFF;
	tx[7] = crc >> 8;

	LOG_HEX(_log_tag, "TX read_status", tx, 8);

	uint8_t rx[32] = { 0 };
	int len = txn_frame_(tx, 8, rx, sizeof(rx), 7);   // slave+fn+bc+2data+2crc
	if (len < 0) return true;

	LOG_HEX(_log_tag, "RX read_status", rx, len);

	status = ((uint32_t)rx[3] << 8) | rx[4];
	return false;
}

void DM2J_RS570::print_status(uint32_t status)
{
	if (!debug_mode) return;

	char flags[128] = { 0 };
	if (status & 0x0001)  std::strncat(flags, "[FAULT] ",     sizeof(flags) - std::strlen(flags) - 1);
	if (status & 0x0002)  std::strncat(flags, "[ENABLE] ",    sizeof(flags) - std::strlen(flags) - 1);
	if (status & 0x0004)  std::strncat(flags, "[RUN] ",       sizeof(flags) - std::strlen(flags) - 1);
	if (status & 0x0010)  std::strncat(flags, "[CMD_DONE] ",  sizeof(flags) - std::strlen(flags) - 1);
	if (status & 0x0020)  std::strncat(flags, "[PATH_DONE] ", sizeof(flags) - std::strlen(flags) - 1);
	if (status & 0x0040)  std::strncat(flags, "[HOME_DONE] ", sizeof(flags) - std::strlen(flags) - 1);

	LOG_DBG(_log_tag, "status=0x%08X %s", status, flags);
}

bool DM2J_RS570::read_error_code(uint16_t& errCode)
{
	if (!client) return true;
	uint8_t tx[8];

	tx[0] = slaveID;
	tx[1] = 0x03;
	tx[2] = 0x22;     // 0x2203
	tx[3] = 0x03;
	tx[4] = 0x00;
	tx[5] = 0x01;

	uint16_t crc = crc16(tx, 6);
	tx[6] = crc & 0xFF;
	tx[7] = crc >> 8;

	uint8_t rx[32] = { 0 };
	int len = txn_frame_(tx, 8, rx, sizeof(rx), 7);
	if (len < 0) return true;

	errCode = (rx[3] << 8) | rx[4];
	return false;
}

bool DM2J_RS570::read_save_status(uint16_t& saveStatus)
{
	if (!client) return true;
	uint8_t tx[8];

	tx[0] = slaveID;
	tx[1] = 0x03;
	tx[2] = 0x19;     // 0x1901
	tx[3] = 0x01;
	tx[4] = 0x00;
	tx[5] = 0x01;

	uint16_t crc = crc16(tx, 6);
	tx[6] = crc & 0xFF;
	tx[7] = crc >> 8;

	uint8_t rx[32] = { 0 };
	int len = txn_frame_(tx, 8, rx, sizeof(rx), 7);
	if (len < 0) return true;

	saveStatus = (rx[3] << 8) | rx[4];
	return false;
}

//=========== control: enable / alarm / save ===========
//
// Per DM2J-RS V1.0 manual:
//   Pr0.07 (0x000F) software force-enable: 1 = ON (override DI1), 0 = OFF
//   0x1801 control word: 0x1111 = reset current alarm, 0x2211 = save params to EEPROM
// (Older header comments claiming 0x1801 = 0x1111/0x2233/0x2222 for enable/disable/save
//  were wrong — corrected here.)

bool DM2J_RS570::motor_enable()
{
	LOG_INF(_log_tag, "motor_enable (Pr0.07 = 1, force enable)");
	return writeSingle(0x000F, 0x0001);
}

bool DM2J_RS570::motor_disable()
{
	LOG_INF(_log_tag, "motor_disable (Pr0.07 = 0, release to DI1)");
	return writeSingle(0x000F, 0x0000);
}

bool DM2J_RS570::set_di1_function(uint16_t code)
{
	LOG_INF(_log_tag, "set_di1_function (Pr4.02 / 0x0145)");
	return writeSingle(0x0145, code);
}

bool DM2J_RS570::read_di1_function(uint16_t& code)
{
	if (!client) return true;
	uint8_t tx[8];
	tx[0] = slaveID;
	tx[1] = 0x03;
	tx[2] = 0x01;     // 0x0145
	tx[3] = 0x45;
	tx[4] = 0x00;
	tx[5] = 0x01;
	uint16_t crc = crc16(tx, 6);
	tx[6] = crc & 0xFF;
	tx[7] = crc >> 8;

	uint8_t rx[32] = { 0 };
	int len = txn_frame_(tx, 8, rx, sizeof(rx), 7);
	if (len < 0) return true;

	code = (rx[3] << 8) | rx[4];
	return false;
}

bool DM2J_RS570::save_params()
{
	LOG_INF(_log_tag, "save_params (0x1801 = 0x2211, save to EEPROM)");
	return writeSingle(0x1801, 0x2211);
}

bool DM2J_RS570::reset_alarm()
{
	LOG_INF(_log_tag, "reset_alarm (0x1801 = 0x1111, clear current fault)");
	return writeSingle(0x1801, 0x1111);
}

bool DM2J_RS570::read_motor_position(int32_t& pos)
{
	if (!client) return true;
	uint8_t tx[8];

	tx[0] = slaveID;
	tx[1] = 0x03;
	tx[2] = 0x60;     // PR8.44 high
	tx[3] = 0x2C;     //        low
	tx[4] = 0x00;
	tx[5] = 0x02;     // read 2 registers

	uint16_t crc = crc16(tx, 6);
	tx[6] = crc & 0xFF;
	tx[7] = crc >> 8;

	LOG_HEX(_log_tag, "TX read_pos", tx, 8);

	uint8_t rx[32];
	int len = txn_frame_(tx, 8, rx, sizeof(rx), 9);
	if (len < 0) return true;

	LOG_HEX(_log_tag, "RX read_pos", rx, len);

	uint16_t hi = (rx[3] << 8) | rx[4];
	uint16_t lo = (rx[5] << 8) | rx[6];

	pos = (int32_t)((hi << 16) | lo);
	return false;
}

bool DM2J_RS570::read_pulse_per_rev(uint16_t& ppr)
{
	if (!client) return true;
	uint8_t tx[8];

	tx[0] = slaveID;
	tx[1] = 0x03;
	tx[2] = 0x00;
	tx[3] = 0x01;   // PR0.00
	tx[4] = 0x00;
	tx[5] = 0x01;

	uint16_t crc = crc16(tx, 6);
	tx[6] = crc & 0xFF;
	tx[7] = crc >> 8;

	LOG_HEX(_log_tag, "TX read_ppr", tx, 8);

	uint8_t rx[32];
	// [2026-09-01] recv 逾時 400ms —— 原本是「send + sleep 200ms + recv(200ms)」，
	// 合成原子交易後 sleep 沒有容身之處（它夾在 send 與 recv 中間）。改用等值的
	// 總等待時間，不縮短這一站原有的寬限。
	int len = txn_frame_(tx, 8, rx, sizeof(rx), 7, 400);
	if (len < 0) return true;

	LOG_HEX(_log_tag, "RX read_ppr", rx, len);

	ppr = (rx[3] << 8) | rx[4];
	return false;
}

bool DM2J_RS570::read_position_cm(double& cm)
{
	int32_t pos = 0;
	uint16_t ppr = 0;

	if (read_motor_position(pos)) return true;
	if (read_pulse_per_rev(ppr) || ppr == 0) return true;

	cm = pulse_to_cm_(pos, ppr);
	return false;
}

//=========== utility: Modbus write ===========

bool DM2J_RS570::writeSingle(uint16_t reg, uint16_t value)
{
	uint8_t frame[8];

	frame[0] = slaveID;
	frame[1] = 0x06;           // Write Single Register
	frame[2] = reg >> 8;
	frame[3] = reg & 0xFF;
	frame[4] = value >> 8;
	frame[5] = value & 0xFF;

	uint16_t c = crc16(frame, 6);
	frame[6] = c & 0xFF;
	frame[7] = c >> 8;

	LOG_HEX(_log_tag, "TX writeSingle", frame, 8);

	std::vector<uint8_t> tx(frame, frame + 8);
	std::vector<uint8_t> rx;
	return sendRecv(tx, rx);
}
bool DM2J_RS570::writeSingle_sync(uint16_t reg, uint16_t value)
{
	uint8_t frame[8];

	frame[0] = 0x00;           // broadcast
	frame[1] = 0x06;
	frame[2] = reg >> 8;
	frame[3] = reg & 0xFF;
	frame[4] = value >> 8;
	frame[5] = value & 0xFF;

	uint16_t c = crc16(frame, 6);
	frame[6] = c & 0xFF;
	frame[7] = c >> 8;

	LOG_HEX(_log_tag, "TX writeSingle_sync", frame, 8);

	std::vector<uint8_t> tx(frame, frame + 8);
	std::vector<uint8_t> rx;
	return sendRecv(tx, rx);
}

bool DM2J_RS570::writeMulti(uint16_t startReg, const std::vector<uint16_t>& data)
{
	int count = data.size();
	std::vector<uint8_t> tx(9 + count * 2);

	tx[0] = slaveID;
	tx[1] = 0x10;                 // Write Multiple Registers
	tx[2] = startReg >> 8;
	tx[3] = startReg & 0xFF;
	tx[4] = 0x00;
	tx[5] = count;
	tx[6] = count * 2;

	int idx = 7;
	for (auto v : data)
	{
		tx[idx++] = v >> 8;
		tx[idx++] = v & 0xFF;
	}

	uint16_t c = crc16(tx.data(), idx);
	tx[idx++] = c & 0xFF;
	tx[idx++] = c >> 8;

	LOG_HEX(_log_tag, "TX writeMulti", tx.data(), (int)tx.size());

	std::vector<uint8_t> rx;
	bool err = sendRecv(tx, rx);

	if (err)
		LOG_ERR(_log_tag, "writeMulti no response");
	else
		LOG_HEX(_log_tag, "RX writeMulti", rx.data(), (int)rx.size());

	return err;
}

//=========== utility: send/recv ===========

bool DM2J_RS570::sendRecv(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx)
{
	if (!client) return true;

	// 🔴 [2026-09-01] 原子交易（見 txn_frame_ 的說明）。這條路徑同樣走共用匯流排，
	// 原本的 drainRx + sendData + receiveData 三段式在 send 與 recv 之間會放開
	// socket_mtx。逾時沿用原值（send 50 / recv 50）。
	uint8_t buf[256] = { 0 };
	const int r = client->sendAndReceive((const char*)tx.data(), (int)tx.size(),
	                                     (char*)buf, sizeof(buf), 50, 50);
	if (r <= 0) return true;

	// [2026-08-28] CRC check added by the driver audit. This path had no
	// validation at all — callers received whatever bytes turned up.
	// Slave id is deliberately NOT checked here: writeSingle_sync() broadcasts
	// to slave 0x00, so a reply (when one appears at all) will not carry our id.
	if (r >= 4) {
		const uint16_t rx_crc = (uint16_t)buf[r - 2] | ((uint16_t)buf[r - 1] << 8);
		if (crc16(buf, r - 2) != rx_crc) {
			LOG_ERR(_log_tag, "sendRecv CRC mismatch (%d bytes) — frame dropped", r);
			return true;
		}
	}

	rx.assign(buf, buf + r);
	return false;
}

//=========== utility: CRC16 (Modbus) ===========

uint16_t DM2J_RS570::crc16(const uint8_t* buf, int len)
{
	uint16_t crc = 0xFFFF;

	for (int i = 0; i < len; i++)
	{
		crc ^= buf[i];
		for (int j = 0; j < 8; j++)
		{
			if (crc & 1)
				crc = (crc >> 1) ^ 0xA001;
			else
				crc >>= 1;
		}
	}
	return crc;
}
