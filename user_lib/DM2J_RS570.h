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
	// 🔴 [2026-08-28] 這一族原本全是 void，把 writeSingle/writeMulti 的結果整個丟掉。
	//    也就是**連「停止」都不知道有沒有送成功** —— speed_move_stop() 有 4 個呼叫點，
	//    其中 app 的 signal_obstacle() 註解自己寫著 "Critical to stop the slide
	//    IMMEDIATELY"，而它的回傳值被丟在地上。
	//    與同日修的 PR_move_set / PR_trigger 完全同型（見 changelog -drv3）：
	//    **通訊失敗時馬達不會停，會保持前一個動作。**
	//    改成 bool（false = OK，依 CLAUDE.md 慣例）。忽略回傳值的既有呼叫端不受影響。
	bool speed_move(int pr_num, int mode, int rpm, int pos);
	bool speed_move_stop();

	// PR Move 兩步驟: 1.設定移動長度cm 2.觸發移動
	// ---- 機構標定（2026-08-28 實機量測後加入）----------------------------
	// 這顆驅動器只認脈衝；「一個脈衝走多遠」完全取決於它後面接什麼機構。
	// 本檔所有 *_cm 介面原本寫死「1 圈 = 1 cm」，那是把某一台機器的機構參數
	// 埋進通用驅動層 —— 上滑台實測是 7.731 cm/圈（皮帶軸），於是每一個 cm
	// 指令都走了 7.7 倍，而驅動器照樣回報漂亮的整數，從 log 完全看不出來。
	//
	// 預設 1.0 = 維持舊行為，未呼叫 set_lead_cm_per_rev() 的既有使用者不受影響。
	void set_lead_cm_per_rev(double cm_per_rev);

	// 軟性行程上限（預設關閉）。超出範圍的 cm 指令會被「明確拒絕並記錄」，
	// 而不是安靜地把機構推到底 —— 2026-08-28 實測：下一個超出行程兩倍的
	// 指令，驅動器、應用層、log 三邊都沒有任何抗議。
	// lo == hi 表示停用。
	void set_travel_limit_cm(double lo_cm, double hi_cm);

	// [2026-08-28] void → bool (false = OK, per CLAUDE.md). These three swallowed
	// the writeMulti/writeSingle result, which made every layer above them blind:
	// PR_move_cm_nowait returned a hardcoded "success", and the rail-sweep
	// failure detection added on main tested that constant. Existing callers that
	// ignore the value keep compiling unchanged.
	bool PR_move_set(int pr_num, int mode, int rpm, int pos, int acc, int dec); // 只設定，要自己算移動一公分要轉起圈
	bool PR_trigger(int pr_num);
	bool PR_trigger_sync(int pr_num);
	bool PR_move_cm(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec);        // 設定+觸發，單位公分，會等待執行完畢
	bool PR_move_cm_nowait(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec); // 同上但不等待
	bool PR_move_cm_set(int pr_num, int mode, int rpm, double pos_cm, int acc, int dec);    // 只設定，單位公分
	bool PR_move_cm_trigger_all(int pr_num); 

	// JOG 目前不需要用
	bool jog_forward();
	bool jog_reverse();
	bool jog_stop();
	bool set_jog_speed(int rpm);     // Reg 0x01E1
	bool set_jog_acc(int acc_ms);    // Reg 0x01E7
	bool set_jog_dec(int dec_ms);    // Reg 0x01E7 (shared with acc, Pr6.03)

	// Homing
	bool home_set_mode(uint16_t mode_bits);
	bool home_set_high_speed(uint16_t rpm);
	bool home_set_low_speed(uint16_t rpm);
	bool home_set_acc_time(uint16_t v);
	bool home_set_dec_time(uint16_t v);
	bool home_set_overrun(uint16_t v);
	bool home_start();
	bool home_set_current_pos_zero();

	// 使能 / 儲存 / 清警報（真實指令以手冊 §5.3.2/5.3.3 為準，舊版註解有誤）
	bool motor_enable();                   // 0x000F (Pr0.07) = 1: 軟體強制使能
	bool motor_disable();                  // 0x000F (Pr0.07) = 0: 解除強制 (交回 DI1)
	bool save_params();                    // 0x1801 = 0x2211: 儲存所有參數到 EEPROM
	bool reset_alarm();                    // 0x1801 = 0x1111: 復位當前報警

	// 讀取資訊
	bool read_version(uint16_t& ver1, uint16_t& ver2);
	bool read_status(uint32_t& status);
	void print_status(uint32_t status);
	bool read_error_code(uint16_t& errCode);  // 0x2203
	bool read_save_status(uint16_t& saveStatus); // 0x1901

	// 位置相關
	bool read_motor_position(int32_t& pos);
	bool read_pulse_per_rev(uint16_t& ppr);
	bool read_position_cm(double& cm);

private:
	TCP_client* client;
	bool useExternalClient;
	bool debug_mode;
	int slaveID;
	std::string _log_tag;

	uint16_t crc16(const uint8_t* buf, int len);

	// 機構標定（見 set_lead_cm_per_rev）。所有 cm↔pulse 換算一律走這兩個 helper，
	// 不要在各函式裡各自乘除 —— 這個檔案已經因為「同一個換算散在多處」出過事。
	double  lead_cm_per_rev_ = 1.0;
	double  travel_lo_cm_    = 0.0;
	double  travel_hi_cm_    = 0.0;   // lo == hi → 停用

	int     cm_to_pulse_(double cm, uint16_t ppr) const;
	double  pulse_to_cm_(int32_t pulse, uint16_t ppr) const;
	bool    travel_reject_(double cm, const char* what);   // true = 超出範圍（已記錄）

	// [2026-08-28] Single receive path for all six read sites: reads into a
	// 32-byte frame, enforces a minimum length and verifies the RTU CRC.
	// Returns the frame length, or -1 if nothing usable arrived. Private —
	// no public API change (see CLAUDE.md "模組邊界").
	int recv_frame_(uint8_t* rx, int min_len);
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
