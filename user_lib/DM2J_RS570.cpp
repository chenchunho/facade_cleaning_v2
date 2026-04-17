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

void DM2J_RS570::speed_move(int pr_num, int mode, int rpm, int pos)
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

	writeMulti(0x6200 + pr_num * 8, block);
	uint16_t trig = 0x10 | (pr_num & 0x0F);
	writeSingle(0x6002, trig);
}

void DM2J_RS570::speed_move_stop()
{
	writeSingle(0x6002, 0x0040);
}

//=========== control: PR Move ===========

void DM2J_RS570::PR_move_set(int pr_num, int mode, int rpm, int pos, int acc, int dec)
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

	writeMulti(base, block);
}
void DM2J_RS570::PR_trigger(int pr_num)
{
	uint16_t trig = 0x10 | (pr_num & 0x0F);
	writeSingle(0x6002, trig);
}
void DM2J_RS570::PR_trigger_sync(int pr_num)
{
	uint16_t trig = 0x10 | (pr_num & 0x0F);
	writeSingle_sync(0x6002, trig);
}

//=========== control: PR Move (cm) ===========

// mode => 0 relative, 1 absolute
bool DM2J_RS570::PR_move_cm(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec)
{
	uint16_t ppr = 10000;
	if (read_pulse_per_rev(ppr) || ppr == 0)
	{
		LOG_ERR(_log_tag, "PPR read failed");
		return true;
	}

	int pos_pulse = (int)(pos_cm * ppr);
	LOG_DBG(_log_tag, "PR_move_cm %.3f cm -> %d pulses (PPR=%u)", pos_cm, pos_pulse, ppr);

	PR_move_set(pr_num, mode, rpm, pos_pulse, acc, dec);
	PR_trigger(pr_num);

	const int timeout_ms = 10000;
	int elapsed = 0;

	uint32_t st = 0;

	while (elapsed < timeout_ms)
	{
		if (read_status(st))
		{
			LOG_ERR(_log_tag, "read status failed during PR wait");
			return true;
		}

		bool cmd_done  = st & 0x0010;
		bool path_done = st & 0x0020;
		bool fault     = st & 0x0001;

		if (debug_mode)
			print_status(st);

		if (fault)
		{
			LOG_ERR(_log_tag, "PR fault detected (status=0x%08X)", st);
			return true;
		}

		if (cmd_done && path_done)
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

bool DM2J_RS570::PR_move_cm_nowait(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec)
{
	uint16_t ppr = 10000;
	// PPR skipped to save one round-trip; assumes default 10000
	int pos_pulse = (int)(pos_cm * ppr);
	LOG_DBG(_log_tag, "PR_move_cm_nowait %.3f cm -> %d pulses (PPR=%u)", pos_cm, pos_pulse, ppr);

	PR_move_set(pr_num, mode, rpm, pos_pulse, acc, dec);
	PR_trigger(pr_num);
	return false;
}

bool DM2J_RS570::PR_move_cm_set(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec)
{
	uint16_t ppr = 10000;
	int pos_pulse = (int)(pos_cm * ppr);
	LOG_DBG(_log_tag, "PR_move_cm_set %.3f cm -> %d pulses (PPR=%u)", pos_cm, pos_pulse, ppr);

	PR_move_set(pr_num, mode, rpm, pos_pulse, acc, dec);
	return false;
}

bool DM2J_RS570::PR_move_cm_trigger_all(int pr_num)
{
	PR_trigger_sync(pr_num);

	const int timeout_ms = 10000;
	int elapsed = 0;

	while (elapsed < timeout_ms)
	{
		uint32_t st = 0;

		if (read_status(st))
		{
			LOG_ERR(_log_tag, "read status failed during trigger_all wait");
			return true;
		}

		if (debug_mode)
			print_status(st);

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

void DM2J_RS570::jog_forward()
{
	writeSingle(0x1801, 0x4001);
}

void DM2J_RS570::jog_reverse()
{
	writeSingle(0x1801, 0x4002);
}

void DM2J_RS570::jog_stop()
{
	writeSingle(0x6002, 0x0040);
}

void DM2J_RS570::set_jog_speed(int rpm)
{
	writeSingle(0x01E1, (uint16_t)rpm);
}

void DM2J_RS570::set_jog_acc(int acc_ms)
{
	writeSingle(0x01E7, (uint16_t)acc_ms);
}

void DM2J_RS570::set_jog_dec(int dec_ms)
{
	writeSingle(0x01E7, (uint16_t)dec_ms);   // RS485 JOG shares acc/dec register (Pr6.03, 0x01E7)
}

//=========== control: Homing ===========

void DM2J_RS570::home_set_mode(uint16_t mode_bits)
{
	writeSingle(0x600A, mode_bits);
}

void DM2J_RS570::home_set_high_speed(uint16_t rpm)
{
	writeSingle(0x600F, rpm);
}

void DM2J_RS570::home_set_low_speed(uint16_t rpm)
{
	writeSingle(0x6010, rpm);
}

void DM2J_RS570::home_set_acc_time(uint16_t v)
{
	writeSingle(0x6011, v);
}

void DM2J_RS570::home_set_dec_time(uint16_t v)
{
	writeSingle(0x6012, v);
}

void DM2J_RS570::home_set_overrun(uint16_t v)
{
	writeSingle(0x6015, v);
}

void DM2J_RS570::home_start()
{
	writeSingle(0x6002, 0x0020);
}

void DM2J_RS570::home_set_current_pos_zero()
{
	writeSingle(0x6002, 0x0021);
}

//=========== read ===========

bool DM2J_RS570::read_version(uint16_t& ver1, uint16_t& ver2)
{
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

	client->sendData((char*)tx, 8, 200);

	uint8_t rx[32];
	int len = client->receiveData((char*)rx, 32, 200);
	if (len < 9) return true;

	ver1 = (rx[3] << 8) | rx[4];
	ver2 = (rx[5] << 8) | rx[6];
	return false;
}

bool DM2J_RS570::read_status(uint32_t& status)
{
	uint8_t tx[8];

	tx[0] = slaveID;
	tx[1] = 0x03;
	tx[2] = 0x10;     // 0x1003
	tx[3] = 0x03;
	tx[4] = 0x00;
	tx[5] = 0x02;     // read 2 registers (32-bit for Bit16 HOME_DONE)

	uint16_t crc = crc16(tx, 6);
	tx[6] = crc & 0xFF;
	tx[7] = crc >> 8;

	LOG_HEX(_log_tag, "TX read_status", tx, 8);

	client->sendData((char*)tx, 8, 200);

	uint8_t rx[32] = { 0 };
	int len = client->receiveData((char*)rx, 32, 200);

	if (len < 9) return true;  // need 9 bytes for 2 registers

	LOG_HEX(_log_tag, "RX read_status", rx, len);

	uint16_t hi = (rx[3] << 8) | rx[4];
	uint16_t lo = (rx[5] << 8) | rx[6];
	status = ((uint32_t)hi << 16) | lo;
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
	if (status & 0x10000) std::strncat(flags, "[HOME_DONE] ", sizeof(flags) - std::strlen(flags) - 1);

	LOG_DBG(_log_tag, "status=0x%08X %s", status, flags);
}

bool DM2J_RS570::read_error_code(uint16_t& errCode)
{
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

	client->sendData((char*)tx, 8, 200);

	uint8_t rx[32] = { 0 };
	int len = client->receiveData((char*)rx, 32, 200);
	if (len < 7) return true;

	errCode = (rx[3] << 8) | rx[4];
	return false;
}

bool DM2J_RS570::read_save_status(uint16_t& saveStatus)
{
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

	client->sendData((char*)tx, 8, 200);

	uint8_t rx[32] = { 0 };
	int len = client->receiveData((char*)rx, 32, 200);
	if (len < 7) return true;

	saveStatus = (rx[3] << 8) | rx[4];
	return false;
}

bool DM2J_RS570::read_motor_position(int32_t& pos)
{
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

	client->sendData((char*)tx, 8, 200);

	uint8_t rx[32];
	int len = client->receiveData((char*)rx, 32, 200);
	if (len < 9) return true;

	LOG_HEX(_log_tag, "RX read_pos", rx, len);

	uint16_t hi = (rx[3] << 8) | rx[4];
	uint16_t lo = (rx[5] << 8) | rx[6];

	pos = (int32_t)((hi << 16) | lo);
	return false;
}

bool DM2J_RS570::read_pulse_per_rev(uint16_t& ppr)
{
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

	client->sendData((char*)tx, 8, 200);
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	uint8_t rx[32];
	int len = client->receiveData((char*)rx, 32, 200);
	if (len < 7) return true;

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

	cm = (double)pos / (double)ppr;
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

	if (!client->sendData((const char*)tx.data(), tx.size(), 50))
		return true;

	uint8_t buf[256] = { 0 };
	int r = client->receiveData((char*)buf, sizeof(buf), 50);
	if (r <= 0) return true;

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
