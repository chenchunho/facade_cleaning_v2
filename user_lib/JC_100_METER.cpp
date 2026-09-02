#include "JC_100_METER.h"
#include "log_utils.h"

// MODBUS register map (per JC-100-RS485 manual)
namespace JC100_REG {
	constexpr uint16_t PRESSURE      = 0x0001;  // current pressure (R)
	constexpr uint16_t SETPOINT      = 0x0010;  // OUT1 setpoint (R/W)
	constexpr uint16_t UPPER_LIMIT   = 0x0011;  // OUT1 upper limit (R/W)
	constexpr uint16_t LOWER_LIMIT   = 0x0012;  // OUT1 lower limit (R/W)
	constexpr uint16_t OUTPUT_MODE   = 0x0013;  // output mode EASY/HYS/WCMP (R/W)
	constexpr uint16_t DISPLAY_COLOR = 0x0014;  // display color (R/W)
	constexpr uint16_t PRESSURE_UNIT = 0x0015;  // pressure unit (R/W)
	constexpr uint16_t NO_NC         = 0x0016;  // NO/NC (R/W)
	constexpr uint16_t RESPONSE_TIME = 0x0017;  // response time (R/W)
	constexpr uint16_t HYSTERESIS    = 0x0018;  // hysteresis 1~8 (R/W)
	constexpr uint16_t ECO_MODE      = 0x0019;  // eco OFF/Std/FULL (R/W)
	constexpr uint16_t SWITCH_STATUS = 0x001A;  // switch output status (R)
	constexpr uint16_t ZERO_CAL      = 0x0020;  // zero calibration (W)
}

//=========== init ===========

JC_100_METER::JC_100_METER() : error_flag(0), _slaveID(1), debug_mode(false), client(nullptr), _isExternalClient(false)
{
	_log_tag = "JC100:?";
}

JC_100_METER::~JC_100_METER() {
	if (!_isExternalClient && client != nullptr) {
		delete client;
		client = nullptr;
	}
}

bool JC_100_METER::init(TCP_client& extClient, int ID, bool debug) {
	_slaveID = ID;
	debug_mode = debug;
	client = &extClient;
	_isExternalClient = true;
	_log_tag = "JC100:" + std::to_string(ID);
	return false;
}

bool JC_100_METER::init(const std::string& ip, int port, int ID, bool debug) {
	_slaveID = ID;
	debug_mode = debug;
	_isExternalClient = false;
	_log_tag = "JC100:" + std::to_string(ID);
	if (client) delete client;
	client = new TCP_client();
	return !client->connectToServer(ip, port, debug);
}

//=========== utility: Modbus send ===========

bool JC_100_METER::send_command(uint8_t func, uint16_t reg, uint16_t data, std::vector<uint8_t>& res) {
	if (!client || !client->isConnected()) {
		error_flag = 1;
		return true;
	}

	uint8_t frame[8] = {
		(uint8_t)_slaveID, func,
		(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
		(uint8_t)(data >> 8), (uint8_t)(data & 0xFF),
		0, 0
	};
	uint16_t crc = modbusCRC(frame, 6);
	frame[6] = crc & 0xFF;
	frame[7] = crc >> 8;

	LOG_HEX(_log_tag, "TX", frame, 8);

	// [2026-08-28] fast-fail：連續失敗夠多次後改用短 timeout，讓這顆表不再霸佔
	// socket_mtx（完整說明見 JC_100_METER.h 的 FAST_FAIL_AFTER 區塊）。
	// 每 PROBE_EVERY 次補一次完整 timeout，確保 bus 復原時回得來。
	const bool probe = (_consec_fail % PROBE_EVERY) == 0;
	const bool fast  = (_consec_fail >= FAST_FAIL_AFTER) && !probe;
	const int  recv_to = fast ? FAST_RECV_MS : NORMAL_RECV_MS;

	// Atomic transaction — see TCP_client::sendAndReceive doc. JC100 shares
	// the RS485_3 gateway with DY500 weight + PQW relay; multiple readers
	// must not interleave Modbus on the same bus.
	char rxBuf[256];
	int len = client->sendAndReceive((const char*)frame, 8,
	                                 rxBuf, 256,
	                                 500, recv_to);
	if (len < 5) {
		error_flag = 1;
		++_consec_fail;
		// Log 節流：連續失敗時只在「剛進入 fast-fail」和「每次探針」印，否則
		// 四顆表 × 每秒重試會把 console 灌爆（bench 上就是這樣淹掉真正的線索）。
		if (!_fast_fail_noted && _consec_fail >= FAST_FAIL_AFTER) {
			_fast_fail_noted = true;
			LOG_ERR(_log_tag, "TIMEOUT ×%d — 進入 fast-fail (recv %dms)，log 節流至每 %d 次",
			        _consec_fail, FAST_RECV_MS, PROBE_EVERY);
		} else if (!_fast_fail_noted || probe) {
			LOG_ERR(_log_tag, "TIMEOUT (連續 %d 次)", _consec_fail);
		}
		return true;
	}

	LOG_HEX(_log_tag, "RX", rxBuf, len);

	// [2026-09-02] 回覆的結構檢查 —— 原本這裡只算 CRC，造成兩個問題：
	//
	// ① **一句「CRC error」蓋住三種病因**（真雜訊／回覆被切成兩段只收到前半／
	//    前一筆交易的遲到回覆）。三者處置完全不同：雜訊要查接地與終端、
	//    分片要改 USR 網關的 `_pt`（見 CLAUDE.md 網關設定表，那條待辦至今
	//    卡在「沒有證據指向分片」）、遲到回覆要查交易同步。
	//    分不出來就只能猜，而 hex dump 要開 driver debug 才有 —— 那是啟動時
	//    才讀的環境變數，等於「下次再發生時要有證據，必須事先就開著」。
	//
	// ② 🔴 **Modbus 例外回覆會被當成成功**：`[id][func|0x80][code][crc][crc]`
	//    正好 5 bytes（通過上面的 `len < 5`）且 CRC 正確（通過下面的比對）
	//    → `read_pressure()` 取 `r[3]`/`r[4]`，而那是 **CRC 的兩個位元組**，
	//    被當成壓力值往上傳。**裝置明確回報錯誤，上層收到一個看起來正常的數字。**
	//    與 CLAUDE.md 記載的 QX-DO24 `err 0x7C` 同一類（撞號時 JC100 的回覆
	//    被 PWM driver 撿走）。
	//
	// 檢查順序是「結構優先於 CRC」：截斷的幀 CRC 一定也對不上，但報
	// SHORT_FRAME(len=5,expect=7) 比報 CRC 有用得多。代價是若剛好是位址那個
	// 位元組被雜訊打壞，會報成 ADDR_MISMATCH —— 所以訊息一律附上前 4 個位元組，
	// 不必開 debug 就判得出來。
	const int expect_len = (func == 0x03) ? (5 + 2 * (int)data)   // [id][fc][bc][data..][crc][crc]
	                     : (func == 0x06) ? 8                      // echo of the request
	                     : 0;                                      // 0 = 不檢查長度
	const uint8_t r0 = (uint8_t)rxBuf[0];
	const uint8_t r1 = (uint8_t)rxBuf[1];

	// 共用的失敗收尾：計數 + 節流後輸出。訊息由呼叫處組好。
	auto fail_with = [&](const char* kind, int a, int b) -> bool {
		error_flag = 1;
		++_consec_fail;   // 任何一種通訊失敗都要計數 — 同樣不該霸佔 bus
		if (!_fast_fail_noted || probe)
			LOG_ERR(_log_tag, "%s (%d/%d) len=%d head=%02X %02X %02X %02X (連續失敗 %d 次)",
			        kind, a, b, len,
			        r0, r1,
			        len > 2 ? (uint8_t)rxBuf[2] : 0,
			        len > 3 ? (uint8_t)rxBuf[3] : 0,
			        _consec_fail);
		return true;
	};

	if (r1 == (uint8_t)(func | 0x80))            // 裝置回報例外 — 不是通訊失敗，是它拒絕了
		return fail_with("MODBUS_EXCEPTION", (int)r1, len > 2 ? (uint8_t)rxBuf[2] : -1);
	if (r0 != (uint8_t)_slaveID)                 // 別人的回覆（撞號／遲到）
		return fail_with("ADDR_MISMATCH", (int)r0, _slaveID);
	if (r1 != func)                              // 功能碼不符 — 同樣是接錯了回覆
		return fail_with("FUNC_MISMATCH", (int)r1, (int)func);
	if (expect_len && len != expect_len)         // 截斷或多餘 — 分片的直接證據
		return fail_with("SHORT_FRAME", len, expect_len);

	uint16_t cCrc = modbusCRC((uint8_t*)rxBuf, len - 2);
	uint16_t rCrc = (uint8_t)rxBuf[len - 2] | ((uint8_t)rxBuf[len - 1] << 8);
	if (cCrc != rCrc)                            // 結構對但位元被打壞 = 真的 CRC 錯
		return fail_with("CRC", (int)cCrc, (int)rCrc);

	if (_fast_fail_noted) {
		LOG_INF(_log_tag, "通訊恢復（先前連續失敗 %d 次）— 回到正常 timeout %dms",
		        _consec_fail, NORMAL_RECV_MS);
	}
	_consec_fail = 0;
	_fast_fail_noted = false;
	error_flag = 0;
	res.assign((uint8_t*)rxBuf, (uint8_t*)rxBuf + len);
	return false;
}

//=========== read: pressure (0x0001) ===========

int JC_100_METER::read_pressure() {
	std::vector<uint8_t> r;
	if (!send_command(0x03, JC100_REG::PRESSURE, 0x0001, r)) {
		_last_pressure = (int16_t)(r[3] << 8 | r[4]);
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		return _last_pressure;
	}
	else {
		LOG_WRN(_log_tag, "comm error, return last pressure: %d", _last_pressure);
		return _last_pressure;
	}
}

//=========== read/write: OUT1 setpoint (0x0010~0x0012) — signed int16 ===========

int  JC_100_METER::get_setpoint()    { std::vector<uint8_t> r; return !send_command(0x03, JC100_REG::SETPOINT,    0x0001, r) ? (int16_t)(r[3] << 8 | r[4]) : -9999; }
bool JC_100_METER::set_setpoint(int v)    { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::SETPOINT,    (uint16_t)v, r); }

int  JC_100_METER::get_upper_limit() { std::vector<uint8_t> r; return !send_command(0x03, JC100_REG::UPPER_LIMIT, 0x0001, r) ? (int16_t)(r[3] << 8 | r[4]) : -9999; }
bool JC_100_METER::set_upper_limit(int v) { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::UPPER_LIMIT, (uint16_t)v, r); }

int  JC_100_METER::get_lower_limit() { std::vector<uint8_t> r; return !send_command(0x03, JC100_REG::LOWER_LIMIT, 0x0001, r) ? (int16_t)(r[3] << 8 | r[4]) : -9999; }
bool JC_100_METER::set_lower_limit(int v) { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::LOWER_LIMIT, (uint16_t)v, r); }

//=========== read/write: output config (0x0013, 0x0016) ===========

int  JC_100_METER::get_output_mode() { std::vector<uint8_t> r; return !send_command(0x03, JC100_REG::OUTPUT_MODE, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
bool JC_100_METER::set_output_mode(int v) { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::OUTPUT_MODE, (uint16_t)v, r); }

int  JC_100_METER::get_no_nc()       { std::vector<uint8_t> r; return !send_command(0x03, JC100_REG::NO_NC,       0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
bool JC_100_METER::set_no_nc(int v)       { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::NO_NC,       (uint16_t)v, r); }

//=========== read/write: display (0x0014, 0x0015) ===========

int  JC_100_METER::get_display_color()  { std::vector<uint8_t> r; return !send_command(0x03, JC100_REG::DISPLAY_COLOR, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
bool JC_100_METER::set_display_color(int v)  { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::DISPLAY_COLOR, (uint16_t)v, r); }

int  JC_100_METER::get_pressure_unit()  { std::vector<uint8_t> r; return !send_command(0x03, JC100_REG::PRESSURE_UNIT, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
bool JC_100_METER::set_pressure_unit(int v)  { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::PRESSURE_UNIT, (uint16_t)v, r); }

//=========== read/write: control params (0x0017~0x0019) ===========

int  JC_100_METER::get_response_time() { std::vector<uint8_t> r; return !send_command(0x03, JC100_REG::RESPONSE_TIME, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
bool JC_100_METER::set_response_time(int v) { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::RESPONSE_TIME, (uint16_t)v, r); }

int  JC_100_METER::get_hysteresis()    { std::vector<uint8_t> r; return !send_command(0x03, JC100_REG::HYSTERESIS,    0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
bool JC_100_METER::set_hysteresis(int v)    { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::HYSTERESIS,    (uint16_t)v, r); }

int  JC_100_METER::get_eco_mode()      { std::vector<uint8_t> r; return !send_command(0x03, JC100_REG::ECO_MODE,      0x0001, r) ? (r[3] << 8 | r[4]) : -1; }
bool JC_100_METER::set_eco_mode(int v)      { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::ECO_MODE,      (uint16_t)v, r); }

//=========== read: status (0x001A) — read only ===========

int JC_100_METER::get_switch_output_status() { std::vector<uint8_t> r; return !send_command(0x03, JC100_REG::SWITCH_STATUS, 0x0001, r) ? (r[3] << 8 | r[4]) : -1; }

//=========== control: command (0x0020) ===========

bool JC_100_METER::zero_calibration() { std::vector<uint8_t> r; return send_command(0x06, JC100_REG::ZERO_CAL, 0x0001, r); }

//=========== utility: CRC16 (Modbus RTU) ===========

uint16_t JC_100_METER::modbusCRC(const uint8_t* data, int len) {
	uint16_t crc = 0xFFFF;
	for (int i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i];
		for (int j = 8; j != 0; j--) {
			if ((crc & 0x0001) != 0) { crc >>= 1; crc ^= 0xA001; }
			else crc >>= 1;
		}
	}
	return crc;
}
