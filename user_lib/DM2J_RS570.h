#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "TCP_client.h"

/*
	DM2J_RS570 – Modbus/TCP 控制類別
	功能：
		- PR Move / Speed Move
		- JOG
		- Homing
		- 讀取狀態、位置、版本
		- PR move 支援 cm 轉 pulse
*/

class DM2J_RS570
{
public:
	DM2J_RS570();
	~DM2J_RS570();

	// 初始化 (內建 TCP client)
	bool init(const std::string& ip, int port, int ID = 1, bool debug = false);

	// 使用外部 TCP_client
	bool init(TCP_client& extClient, int ID = 1, bool debug = false);

	// Speed Move：寫入 PR block 並啟動
	void speed_move(int pr_num, int mode, int rpm, int pos);
	void speed_move_stop();

	// PR Move
	void PR_move_set(int pr_num, int mode, int rpm, int pos, int acc, int dec);
	void PR_trigger(int pr_num);
	void PR_trigger_sync(int pr_num);
	bool PR_move_cm(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec);
	bool PR_move_cm_nowait(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec);
	bool PR_move_cm_set(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec);
	bool PR_move_cm_trigger_all(int pr_num);

	// JOG
	void jog_forward();
	void jog_reverse();
	void jog_stop();
	void set_jog_speed(int rpm);     // Reg 0x01E1
	void set_jog_acc(int acc_ms);    // Reg 0x01E7
	void set_jog_dec(int dec_ms);    // Reg 0x01E8

	// Homing
	void home_set_mode(uint16_t mode_bits);
	void home_set_high_speed(uint16_t rpm);
	void home_set_low_speed(uint16_t rpm);
	void home_set_acc_time(uint16_t v);
	void home_set_dec_time(uint16_t v);
	void home_set_overrun(uint16_t v);
	void home_start();
	void home_set_current_pos_zero();

	// 讀取資訊
	bool read_version(uint16_t& ver1, uint16_t& ver2);
	bool read_status(uint16_t& status);
	void print_status(uint16_t status);

	// 位置相關
	bool read_motor_position(int32_t& pos);
	bool read_pulse_per_rev(uint16_t& ppr);
	bool read_position_cm(double& cm);

private:
	TCP_client* client;
	bool useExternalClient;
	bool debugEnabled;
	int slaveID;

	uint16_t crc16(const uint8_t* buf, int len);
	bool sendRecv(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx);

	bool writeSingle(uint16_t reg, uint16_t value);
	bool writeSingle_sync(uint16_t reg, uint16_t value);
	bool writeMulti(uint16_t startReg, const std::vector<uint16_t>& data);
};


/* =====================================================================
   Example Usage
   =====================================================================

#include "DM2J_RS570.h"

int main()
{
	DM2J_RS570 motor;

	// 初始化與連線
	if (!motor.init("192.168.1.50", 502, 1, true))
	{
		printf("Connect failed.\n");
		return 0;
	}

	printf("Connected.\n");

	//------------------------------------------------------------------
	// [1] 讀取版本
	//------------------------------------------------------------------
	uint16_t v1, v2;
	if (motor.read_version(v1, v2))
		printf("Version: %u.%u\n", v1, v2);

	//------------------------------------------------------------------
	// [2] Homing 範例
	//------------------------------------------------------------------
	motor.home_set_mode(0x0002);     // HOMING 模式設定
	motor.home_set_high_speed(200);  // 快速找原點
	motor.home_set_low_speed(50);    // 慢速靠近
	motor.home_set_acc_time(50);
	motor.home_set_dec_time(50);
	motor.home_start();

	Sleep(2000);                     // 等一下（也可以改成讀 status 完成判斷）

	//------------------------------------------------------------------
	// [3] PR Move (以 cm 移動)
	//------------------------------------------------------------------
	// mode=1 (絕對位置)
	// rpm=200
	// pos_cm=10.0 cm
	motor.PR_move_cm(0, 1, 200, 10.0, 100, 100);

	//------------------------------------------------------------------
	// [4] JOG 範例
	//------------------------------------------------------------------
	motor.set_jog_speed(150);
	motor.jog_forward();
	Sleep(1000);
	motor.jog_stop();

	//------------------------------------------------------------------
	// [5] 讀位置
	//------------------------------------------------------------------
	double cm = 0;
	if (motor.read_position_cm(cm))
		printf("Position: %.3f cm\n", cm);

	return 0;
}

   ===================================================================== */
