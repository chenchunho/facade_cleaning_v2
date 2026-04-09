#ifndef JC_100_METER_H
#define JC_100_METER_H

#include "TCP_client.h"
#include <string>
#include <vector>
#include <cstdint>
#include <thread>
#include <chrono>

/******************************************************
 *  JC_100_METER 使用說明 (JC-100-RS485 壓力傳感器)
 *
 *  一、初始化方式（任選一種）
 *  --------------------------------------------------
 *  方式 1：由本類別內部建立 TCP 連線
 *
 *      JC_100_METER meter;
 *      meter.init("192.168.1.21", 4001, 1, true);
 *
 *  方式 2：外部已有 TCP_client（已 connect）
 *
 *      TCP_client cli;
 *      cli.connectToServer("192.168.1.21", 4001);
 *
 *      JC_100_METER meter;
 *      meter.init(cli, 1, true);
 *
 *
 *  二、基本使用方法
 *  --------------------------------------------------
 *      int val = meter.read_pressure();
 *      double kPa = val / 10.0;
 *
 *      meter.get_output_mode();
 *      meter.set_setpoint(500);
 *      meter.zero_calibration();
 *
 ******************************************************/

class JC_100_METER {
public:
	JC_100_METER();
	~JC_100_METER();

	// --- 初始化 ---
	bool init(const std::string& ip, int port, int ID, bool debug = false);
	bool init(TCP_client& extClient, int ID, bool debug = false);

	// === 即時數據 (0x0001) ===
	int  read_pressure();              // 當前氣壓值 (R)

	// === OUT1 設定 (0x0010~0x0012) ===
	int  get_setpoint();               // OUT1 目標值 (R/W)
	bool set_setpoint(int value);
	int  get_upper_limit();            // OUT1 目標上限值 (R/W)
	bool set_upper_limit(int value);
	int  get_lower_limit();            // OUT1 目標下限值 (R/W)
	bool set_lower_limit(int value);

	// === 輸出設定 (0x0013, 0x0016) ===
	int  get_output_mode();            // 0:EASY, 1:HYS, 2:WCMP (R/W)
	bool set_output_mode(int value);
	int  get_no_nc();                  // 0:NO, 1:NC (R/W)
	bool set_no_nc(int value);

	// === 顯示設定 (0x0014, 0x0015) ===
	int  get_display_color();          // 0:R_ON, 1:G_ON, 2:RED, 3:GREEN (R/W)
	bool set_display_color(int value);
	int  get_pressure_unit();          // 0:MPa, 1:kPa, 2:kgf/cm², 3:bar, 4:psi, 5:mmHg (R/W)
	bool set_pressure_unit(int value);

	// === 控制參數 (0x0017~0x0019) ===
	int  get_response_time();          // 0~A 對應 2.5ms~5000ms (R/W)
	bool set_response_time(int value);
	int  get_hysteresis();             // 1~8 級 (R/W)
	bool set_hysteresis(int value);
	int  get_eco_mode();               // 0:OFF, 1:Std, 2:FULL (R/W)
	bool set_eco_mode(int value);

	// === 狀態讀取 (0x001A) ===
	int  get_switch_output_status();   // 開關量輸出狀態 0:OFF, 1:ON (R only)

	// === 命令 (0x0020) ===
	bool zero_calibration();           // 校零 (W only)

	// --- 工具 ---
	uint16_t modbusCRC(const uint8_t* data, int len);

	int error_flag;  // 0: 正常, 1: 異常

private:
	TCP_client* client = nullptr;
	int  _slaveID;
	bool _debug;
	bool _isExternalClient;
	int  _last_pressure = 0;

	bool send_command(uint8_t func, uint16_t reg, uint16_t data, std::vector<uint8_t>& res);
	void log_hex(const std::string& prefix, const uint8_t* data, int len);
};

#endif
