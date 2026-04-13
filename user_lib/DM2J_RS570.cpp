#include "DM2J_RS570.h"
#include <iostream>
#include <cstdio>
#include <cstring>
#include <thread> // 新增：用於跨平台延遲
#include <chrono> // 新增：用於時間單位

// ======================================================================
// Constructor / Destructor
// ======================================================================

DM2J_RS570::DM2J_RS570()
{
	client = nullptr;
	useExternalClient = false;
	debugEnabled = false;
	slaveID = 1;
}

DM2J_RS570::~DM2J_RS570()
{
	if (!useExternalClient && client)
	{
		client->close();
		delete client;
	}
}

// ======================================================================
// Init
// ======================================================================

bool DM2J_RS570::init(const std::string& ip, int port, int ID, bool debug)
{
	debugEnabled = debug;
	slaveID = ID;

	client = new TCP_client();
	if (!client->connectToServer(ip, port))
		return true;

	useExternalClient = false;
	return false;
}

bool DM2J_RS570::init(TCP_client& extClient, int ID, bool debug)
{
	client = &extClient;
	useExternalClient = true;
	debugEnabled = debug;
	slaveID = ID;
	return false;
}

// ======================================================================
// Speed Move
// ======================================================================

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

// ======================================================================
// PR Move
// ======================================================================

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

// ======================================================================
// PR Move (cm)
// ======================================================================

// mode => 0相對位置 1絕對位置
bool DM2J_RS570::PR_move_cm(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec)
{
	uint16_t ppr = 10000;
	if (read_pulse_per_rev(ppr) || ppr == 0)
	{
		printf("Error: PPR read failed.\n");
		return true;
	}

	int pos_pulse = (int)(pos_cm * ppr);

	if (debugEnabled)
		printf("[PR_move_cm] %.3f cm → %d pulses (PPR=%u)\n", pos_cm, pos_pulse, ppr);

	PR_move_set(pr_num, mode, rpm, pos_pulse, acc, dec);
	PR_trigger(pr_num);

	// ---------------------------------------------------------
	// 等待 CMD_DONE + PATH_DONE
	// ---------------------------------------------------------
	const int timeout_ms = 10000;
	int elapsed = 0;

	uint32_t st = 0;

	while (elapsed < timeout_ms)
	{
		if (read_status(st))
		{
			printf("Read status failed!\n");
			return true;
		}

		bool cmd_done  = st & 0x0010;
		bool path_done = st & 0x0020;
		bool fault     = st & 0x0001;

		if (debugEnabled)
		{
			printf("[WAIT] Status = 0x%08X => ", st);
			print_status(st);
		}

		if (fault)
		{
			printf("[PR] Fault detected.\n");
			return true;
		}

		if (cmd_done && path_done)
		{
			if (debugEnabled)
				printf("[PR] Motion Completed\n");
			return false;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		elapsed += 50;

	}

	printf("[PR] Timeout waiting motion done.\n");
	return true;
}

bool DM2J_RS570::PR_move_cm_nowait(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec)
{
	uint16_t ppr= 10000;
	//if (!read_pulse_per_rev(ppr) || ppr == 0)
	//{
	//	printf("Error: PPR read failed.\n");
	//	return false;
	//}

	int pos_pulse = (int)(pos_cm * ppr);

	if (debugEnabled)
		printf("[PR_move_cm_nowait] %.3f cm → %d pulses (PPR=%u)\n",
			pos_cm, pos_pulse, ppr);

	PR_move_set(pr_num, mode, rpm, pos_pulse, acc, dec);
	PR_trigger(pr_num);
	return false;
}

bool DM2J_RS570::PR_move_cm_set(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec)
{
	uint16_t ppr = 10000;
	//if (!read_pulse_per_rev(ppr) || ppr == 0)
	//{
	//	printf("Error: PPR read failed.\n");
	//	return false;
	//}

	int pos_pulse = (int)(pos_cm * ppr);

	if (debugEnabled)
		printf("[PR_move_cm_nowait] %.3f cm → %d pulses (PPR=%u)\n",
			pos_cm, pos_pulse, ppr);

	PR_move_set(pr_num, mode, rpm, pos_pulse, acc, dec);
	return false;
}

bool DM2J_RS570::PR_move_cm_trigger_all(int pr_num)
{
	PR_trigger_sync(pr_num);

	// ---------------------------------------------------------
	// 等待 CMD_DONE + PATH_DONE
	// ---------------------------------------------------------
	const int timeout_ms = 10000;
	int elapsed = 0;

	while (elapsed < timeout_ms)
	{
		uint32_t st = 0;

		if (read_status(st))
		{
			printf("Read status failed!\n");
			return true;
		}

		if (debugEnabled)
		{
			printf("[WAIT] Status = 0x%08X => ", st);
			print_status(st);
		}

		bool cmd_done = st & 0x0010;
		bool path_done = st & 0x0020;
		bool fault = st & 0x0001;

		if (fault)
		{
			printf("[PR] Fault detected.\n");
			return true;
		}

		if (cmd_done && path_done)
		{
			if (debugEnabled)
				printf("[PR] Motion Completed\n");
			return false;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		elapsed += 50;

	}

	printf("[PR] Timeout waiting motion done.\n");
	return true;
}


// ======================================================================
// JOG
// ======================================================================

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
	writeSingle(0x01E7, (uint16_t)dec_ms);   // RS485 JOG 加減速共用 Pr6.03 (0x01E7)
}

// ======================================================================
// Homing
// ======================================================================

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

// ======================================================================
// Read Version
// ======================================================================

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

// ======================================================================
// Read Status
// ======================================================================

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

	if (debugEnabled)
	{
		printf("[TX] ");
		for (int i = 0; i < 8; i++) printf("%02X ", tx[i]);
		printf("\n");
	}

	client->sendData((char*)tx, 8, 200);

	uint8_t rx[32] = { 0 };
	int len = client->receiveData((char*)rx, 32, 200);

	if (len < 9) return true;  // need 9 bytes for 2 registers

	if (debugEnabled)
	{
		printf("[RX] ");
		for (int i = 0; i < len; i++) printf("%02X ", rx[i]);
		printf("\n");
	}

	uint16_t hi = (rx[3] << 8) | rx[4];
	uint16_t lo = (rx[5] << 8) | rx[6];
	status = ((uint32_t)hi << 16) | lo;
	return false;
}

void DM2J_RS570::print_status(uint32_t status)
{
	printf("Status 0x%08X => ", status);

	if (status & 0x0001)  printf("[FAULT] ");
	if (status & 0x0002)  printf("[ENABLE] ");
	if (status & 0x0004)  printf("[RUN] ");
	if (status & 0x0010)  printf("[CMD_DONE] ");
	if (status & 0x0020)  printf("[PATH_DONE] ");
	if (status & 0x10000) printf("[HOME_DONE] ");  // Bit16

	printf("\n");
}

// ======================================================================
// Motor Enable / Disable / Save Params
// ======================================================================

bool DM2J_RS570::motor_enable()
{
	return writeSingle(0x1801, 0x1111);
}

bool DM2J_RS570::motor_disable()
{
	return writeSingle(0x1801, 0x2233);
}

bool DM2J_RS570::save_params()
{
	return writeSingle(0x1801, 0x2222);
}

// ======================================================================
// Read Error Code (0x2203)
// ======================================================================

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

// ======================================================================
// Read Save Status (0x1901)
// ======================================================================

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

// ======================================================================
// Read Motor Position (PR8.44 / 8.45)
// ======================================================================

bool DM2J_RS570::read_motor_position(int32_t& pos)
{
	uint8_t tx[8];

	tx[0] = slaveID;
	tx[1] = 0x03;
	tx[2] = 0x60;     // PR8.44 高位
	tx[3] = 0x2C;     //     低位
	tx[4] = 0x00;
	tx[5] = 0x02;     // read 2 register

	uint16_t crc = crc16(tx, 6);
	tx[6] = crc & 0xFF;
	tx[7] = crc >> 8;

	if (debugEnabled)
	{
		printf("[TX] ");
		for (int i = 0; i < 8; i++) printf("%02X ", tx[i]);
		printf("\n");
	}

	client->sendData((char*)tx, 8, 200);

	uint8_t rx[32];
	int len = client->receiveData((char*)rx, 32, 200);
	if (len < 9) return true;

	if (debugEnabled)
	{
		printf("[RX] ");
		for (int i = 0; i < len; i++) printf("%02X ", rx[i]);
		printf("\n");
	}

	uint16_t hi = (rx[3] << 8) | rx[4];
	uint16_t lo = (rx[5] << 8) | rx[6];

	pos = (int32_t)((hi << 16) | lo);
	return false;
}

// ======================================================================
// Read PPR (PR0.00)
// ======================================================================

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

	if (debugEnabled)
	{
		printf("[TX] ");
		for (int i = 0; i < 8; i++) printf("%02X ", tx[i]);
		printf("\n");
	}

	client->sendData((char*)tx, 8, 200);
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	uint8_t rx[32];
	int len = client->receiveData((char*)rx, 32, 200);
	if (len < 7) return true;

	if (debugEnabled)
	{
		printf("[RX] ");
		for (int i = 0; i < len; i++) printf("%02X ", rx[i]);
		printf("\n");
	}

	ppr = (rx[3] << 8) | rx[4];
	return false;
}

// ======================================================================
// Convert motor position to cm
// ======================================================================

bool DM2J_RS570::read_position_cm(double& cm)
{
	int32_t pos = 0;
	uint16_t ppr = 0;

	if (read_motor_position(pos)) return true;
	if (read_pulse_per_rev(ppr) || ppr == 0) return true;

	cm = (double)pos / (double)ppr;
	return false;
}

// ======================================================================
// Write Single Holding Register
// ======================================================================

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

	if (debugEnabled)
	{
		printf("[TX] ");
		for (int i = 0; i < 8; i++) printf("%02X ", frame[i]);
		printf("\n");
	}

	std::vector<uint8_t> tx(frame, frame + 8);
	std::vector<uint8_t> rx;
	return sendRecv(tx, rx);
}
bool DM2J_RS570::writeSingle_sync(uint16_t reg, uint16_t value)
{
	uint8_t frame[8];

	frame[0] = 0x00;
	frame[1] = 0x06;           // Write Single Register
	frame[2] = reg >> 8;
	frame[3] = reg & 0xFF;
	frame[4] = value >> 8;
	frame[5] = value & 0xFF;

	uint16_t c = crc16(frame, 6);
	frame[6] = c & 0xFF;
	frame[7] = c >> 8;

	if (debugEnabled)
	{
		printf("[TX] ");
		for (int i = 0; i < 8; i++) printf("%02X ", frame[i]);
		printf("\n");
	}

	std::vector<uint8_t> tx(frame, frame + 8);
	std::vector<uint8_t> rx;
	return sendRecv(tx, rx);
}

// ======================================================================
// Write Multiple Registers
// ======================================================================

bool DM2J_RS570::writeMulti(uint16_t startReg, const std::vector<uint16_t>& data)
{
	int count = data.size();
	std::vector<uint8_t> tx(9 + count * 2);

	tx[0] = slaveID;
	tx[1] = 0x10;                 // Write Multiple Register
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

	// --- 新增 TX Debug Log ---
	if (debugEnabled)
	{
		printf("[TX Multi] ");
		for (size_t i = 0; i < tx.size(); i++) printf("%02X ", tx[i]);
		printf("\n");
	}

	std::vector<uint8_t> rx;
	bool err = sendRecv(tx, rx);

	// --- 新增 RX Debug Log ---
	if (debugEnabled && !err)
	{
		printf("[RX Multi] ");
		for (size_t i = 0; i < rx.size(); i++) printf("%02X ", rx[i]);
		printf("\n");
	}
	else if (debugEnabled && err)
	{
		printf("[RX Multi] Failed to receive response.\n");
	}
	return err;
}

// ======================================================================
// Send / Receive
// ======================================================================

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

// ======================================================================
// CRC16 (Modbus)
// ======================================================================

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