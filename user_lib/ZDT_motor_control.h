#ifndef ZDT_MOTOR_CONTROL_H
#define ZDT_MOTOR_CONTROL_H

#include "TCP_client.h"
#include <string>
#include <vector>
#include <cstdint>

struct MotorSystemStatus {
	// --- 數值參數 ---
	uint16_t bus_voltage;       // 總線電壓 (mV)
	uint16_t bus_current;       // 總線電流 (mA)
	uint16_t phase_current;     // 電機相電流 (mA)
	uint16_t encoder_raw;       // 編碼器原始值
	uint16_t encoder_linear;    // 線性化編碼器值
	double target_pos;          // 電機目標位置 (deg)
	double real_speed;          // 電機實時轉速 (RPM)
	double real_pos;            // 電機實時位置 (deg)
	double pos_error;           // 電機位置誤差 (deg)
	double temperature;         // 電機實時溫度 (℃)

	// --- 回零狀態標誌 (3.3.4 Byte) ---
	bool encoder_ready;      // Bit 0: Enc_Rdy 编码器就绪 (0:异常, 1:正常)
	bool calibration_ready;  // Bit 1: Cal_Rdy 校准表就绪 (0:未校准, 1:已校准)
	bool is_homing;          // Bit 2: Org_SF 正在回零 (0:否, 1:是)
	bool home_failed;        // Bit 3: Org_CF 回零失败 (0:否, 1:是)
	bool over_temp_flag;     // Bit 4: Otp_TF 过热保护 (0:未触发, 1:触发)
	bool over_current_flag;  // Bit 5: Ocp_TF 过流保护 (0:未触发, 1:触发)

	// --- 電機狀態標誌 (3.4.14 Byte) ---
	bool is_enabled;        // Bit 0: Ens_TF 使能狀態 (0:未使能, 1:已使能)
	bool pos_reached;       // Bit 1: Prf_TF 位置到達 (0:未到達, 1:已到達)
	bool stall_flag;        // Bit 2: Cgi_TF 堵轉標誌 (滿足條件1,2)
	bool stall_protection;  // Bit 3: Cgp_TF 堵轉保護 (滿足條件1,2,3)
	bool left_limit;        // Bit 4: Esi_LF 左限位開關狀態 (0:低電平, 1:高電平)
	bool right_limit;       // Bit 5: Esi_RF 右限位開關狀態 (0:低電平, 1:高電平)
	bool reserved_bit6;     // Bit 6: 保留
	bool power_loss_flag;   // Bit 7: Oac_TF 掉電標誌 (預設1, 發生過掉電為1)

	bool is_home_success() const { return !is_homing && !home_failed; }
};

class ZDT_motor_control {
public:
	ZDT_motor_control();
	~ZDT_motor_control();

	bool init(const std::string& ip, int port, int ID, bool debug = false);
	bool init(TCP_client& extClient, int ID, bool debug = false);

	bool set_zero();
	bool calibrate_encoder();
	bool reset_motor();
	bool motion_control_driver_EN(bool status);
	bool motion_control_speed_mode(int dir, int acc_rpm, int rpm, int sync, int retry);
	bool motion_control_pos_mode(int dir, int acc_rpm, int rpm, int pulse, int mode, int sync, int retry);
	bool get_system_status();
	bool release_stall_flag();
	bool emergency_stop(bool sync);
	MotorSystemStatus status;

private:
	TCP_client* client = nullptr;
	bool is_external_client = false;
	uint8_t slave_id = 1;
	bool debug_mode = false;

	uint16_t modbusCRC(const uint8_t* data, int len);
	std::vector<uint8_t> build_write_single_register(uint16_t reg_addr, uint16_t value);
	void printHex(const std::vector<uint8_t>& data, const std::string& prefix);
	std::vector<uint8_t> readEcho(int timeout_ms);
};

#endif