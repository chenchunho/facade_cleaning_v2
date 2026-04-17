#pragma once
#include <string>
#include "TCP_client.h"

/******************************************************
 *  DY_500_weight_sensor 使用說明
 *
 *  一、初始化方式（任选一种）
 *  --------------------------------------------------
 *  方式 1：由本類別內部建立 TCP 連線
 *
 *      DY_500_weight_sensor dy;
 *      dy.init("192.168.1.100", 502, 1, true);
 *
 *  方式 2：外部已有 TCP_client（已 connect）
 *
 *      TCP_client cli;
 *      cli.connectToServer("192.168.1.100", 502);
 *
 *      DY_500_weight_sensor dy;
 *      dy.init(cli, 1, true);  // 不重新連線
 *
 *
 *  二、基本使用方法
 *  --------------------------------------------------
 *      int32_t w_long;
 *      dy.get_weight_long(w_long);
 *
 *      float w_float;
 *      dy.get_weight_float(w_float);
 *
 *      int dp;
 *      dy.get_decimal_point(dp);
 *
 *
 *  三、讀取全參數
 *  --------------------------------------------------
 *
 *      if (!dy.read_all_parm())
 *          dy.print_parm();
 *
 *
 ******************************************************/

class DY_500_weight_sensor
{
public:

	DY_500_weight_sensor();

	// 初始化（本類建立 TCP）
	bool init(const std::string& ip, int port, int ID, bool debug = false);

	// 初始化（外部傳入 TCP_client）
	bool init(TCP_client& extClient, int ID, bool debug = false);

	~DY_500_weight_sensor();

	// 設定通訊參數（ID / Baud / Format）
	void set_communication_parm(int ID, int baud, int format);

	// 讀全部寄存器
	bool read_all_parm();

	// 印全部寄存器
	void print_parm();

	// 重量讀取
	bool get_weight_long(int32_t& outValue);
	bool get_weight_float(float& outValue);

	// 小數點讀取
	bool get_decimal_point(int& dp);

	// 清零
	bool do_clear();

private:

	/***********************
	 * Modbus + Tools
	 ***********************/
	bool modbus_read(uint16_t addr, uint16_t quantity, uint8_t* rx, int& rxLen);
	bool modbus_write_single(uint16_t addr, uint16_t value);

	int32_t parse_long(uint8_t* buf, int index);

	// 寫入 LONG 值到寄存器 (FC10, 2 registers, 4 bytes)
	bool modbus_write_long(uint16_t addr, int32_t value);

	// 單點寄存器讀取（含延遲 + 重試）
	bool read_reg_long(uint16_t addr, int32_t& out);

	uint16_t CRC16(const uint8_t* data, int len);

	/***********************
	 * 通訊物件
	 ***********************/
	TCP_client  ownedClient;  // 由本類建立連線時使用
	TCP_client* client;       // 指向 ownedClient 或外部傳入的物件
	bool debug_mode;
	uint8_t slaveID;
	std::string _log_tag;

	/***********************
	 * get_weight_float 狀態
	 ***********************/
	float lastValidWeight;
	int   weightErrorCount;

public:

	/***********************
	 * 寄存器結構體（全部 Long）
	 ***********************/
	struct RegBlock
	{
		int32_t weight;
		int32_t adc_value;
		int32_t da_value;
		int32_t system_status;
		int32_t multi_function;
		int32_t calibrate_weight;

		int32_t power_on_zero_range;
		int32_t stable_range;
		int32_t stable_time;
		int32_t zero_track;
		int32_t digital_filter;
		int32_t auto_zero_cfg;
		int32_t auto_zero_trigger;
		int32_t auto_zero_delay;
		int32_t decimal_point;

		int32_t rated_output;
		int32_t sample_speed;
		int32_t protocol_mode;
		int32_t data_format;
		int32_t baudrate;
		int32_t station_id;

		int32_t auto_send_interval;
		int32_t system_zero;
		int32_t span_factor;
		int32_t sensor_sensitivity;
		int32_t AD0;
		int32_t AD1;
		int32_t sensor_range;
		int32_t auto_calibration;
		int32_t trans_zero;
		int32_t trans_full;
		int32_t trans_start;
	} reg;

};
