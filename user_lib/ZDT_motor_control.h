#ifndef ZDT_MOTOR_CONTROL_H
#define ZDT_MOTOR_CONTROL_H

#include "TCP_client.h"
#include <string>
#include <vector>
#include <cstdint>

struct MotorSystemStatus {
	// --- 數值參數 (來自 3.7.2 Emm 批量讀取) ---
	uint16_t bus_voltage;       // 總線電壓 (mV)
	uint16_t bus_current;       // 總線電流 (mA) — Emm 批量讀取不包含，固定為 0
	uint16_t phase_current;     // 電機相電流 (mA)
	uint16_t encoder_raw;       // 編碼器原始值 — Emm 批量讀取不包含，固定為 0
	uint16_t encoder_linear;    // 線性化編碼器值 (0-65535 = 0-360°)
	double target_pos;          // 電機目標位置 (deg)  Emm: pos * 360 / 65536
	double real_speed;          // 電機實時轉速 (RPM)  Emm: 直接為 RPM
	double real_pos;            // 電機實時位置 (deg)  Emm: pos * 360 / 65536
	double pos_error;           // 電機位置誤差 (deg)  Emm: err * 360 / 65536
	double temperature;         // 電機實時溫度 (℃) — Emm 批量讀取不包含，固定為 0

	// --- 回零狀態標誌 (3.3.4) ---
	bool encoder_ready;      // Bit 0: Enc_Rdy 編碼器就緒 (0:異常, 1:正常)
	bool calibration_ready;  // Bit 1: Cal_Rdy 校準表就緒 (0:未校準, 1:已校準)
	bool is_homing;          // Bit 2: Org_SF 正在回零 (0:否, 1:是)
	bool home_failed;        // Bit 3: Org_CF 回零失敗 (0:否, 1:是)
	bool over_temp_flag;     // Bit 4: Otp_TF 過熱保護 (0:未觸發, 1:觸發)
	bool over_current_flag;  // Bit 5: Ocp_TF 過流保護 (0:未觸發, 1:觸發)

	// --- 電機狀態標誌 (3.4.14) ---
	bool is_enabled;        // Bit 0: Ens_TF 使能狀態 (0:未使能, 1:已使能)
	bool pos_reached;       // Bit 1: Prf_TF 位置到達 (0:未到達, 1:已到達)
	bool stall_flag;        // Bit 2: Cgi_TF 堵轉標誌 (滿足條件1,2)
	bool stall_protection;  // Bit 3: Cgp_TF 堵轉保護 (滿足條件1,2,3)
	bool left_limit;        // Bit 4: Esi_LF 左限位開關狀態 (0:低電平, 1:高電平)
	bool right_limit;       // Bit 5: Esi_RF 右限位開關狀態 (0:低電平, 1:高電平)
	bool power_loss_flag;   // Bit 7: Oac_TF 掉電標誌 (預設1, 發生過掉電為1)

	bool is_home_success() const { return !is_homing && !home_failed; }
};

class ZDT_motor_control {
public:
	ZDT_motor_control();
	~ZDT_motor_control();

	bool init(const std::string& ip, int port, int ID, bool debug = false);
	bool init(TCP_client& extClient, int ID, bool debug = false);

	// --- 觸發動作命令 (3.1) ---
	bool set_zero();                    // 3.1.3 當前位置角度清零 (Reg 0x000A)
	bool calibrate_encoder();           // 3.1.1 觸發編碼器校準 (Reg 0x0006)
	bool reset_motor();                 // 3.1.2 重啟電機 (Reg 0x0008)
	bool release_stall_flag();          // 3.1.4 解除堵轉/過熱/過流保護 (Reg 0x000E)
	bool factory_reset();               // 3.1.5 恢復出廠設置 (Reg 0x000F)

	// --- 運動控制命令 (3.2) ---
	bool motion_control_driver_EN(bool status);  // 3.2.1 使能控制 (Reg 0x00F3)
	bool motion_control_speed_mode(int dir, int acc_rpm, int rpm, int sync, int retry);  // 3.2.6 速度模式 Emm (Reg 0x00F6)
	bool motion_control_pos_mode(int dir, int acc_rpm, int rpm, int pulse, int mode, int sync, int retry);  // 3.2.11 位置模式 Emm (Reg 0x00FD)
	bool motion_control_pos_mode_nowait(int dir, int acc_rpm, int rpm, int pulse, int mode, int sync, int retry);
	bool emergency_stop(bool sync);              // 3.2.12 立即停止 (Reg 0x00FE)
	bool trigger_sync_move();                    // 3.2.13 觸發多機同步運動 (Reg 0x00FF, 廣播)

	// --- 原點回零命令 (3.3) ---
	bool set_home_zero_position(bool store);     // 3.3.1 設置單圈回零零點位置 (Reg 0x0093)
	bool trigger_home(int mode, bool sync);      // 3.3.2 觸發回零 (Reg 0x009A)
	bool abort_home();                           // 3.3.3 強制中斷回零 (Reg 0x009C)

	// --- 讀取系統參數 (3.7.2) ---
	bool get_system_status();            // 批量讀取 Emm 系統狀態 (Reg 0x0043, 16 regs)
	bool wait_until_pos_reached(int timeout_ms = 10000, int poll_interval_ms = 500);  // 輪詢直到位置到達或超時
	MotorSystemStatus status;

private:
	TCP_client* client = nullptr;
	bool is_external_client = false;
	uint8_t slave_id = 1;
	bool debug_mode = false;
	std::string _log_tag;

	uint16_t modbusCRC(const uint8_t* data, int len);
	std::vector<uint8_t> build_write_single_register(uint16_t reg_addr, uint16_t value);
	void printHex(const std::vector<uint8_t>& data, const std::string& prefix);
	std::vector<uint8_t> readEcho(int timeout_ms);
};

#endif