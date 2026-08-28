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
	int  _slaveID;
	bool debug_mode;
	TCP_client* client = nullptr;
	bool _isExternalClient;
	int  _last_pressure = 0;
	std::string _log_tag;

	// [2026-08-28] 連續失敗時縮短 recv timeout（fast-fail），避免一顆掛掉的表
	// 把整條 bus 的時間佔滿。
	//
	// 起因（bench log 2026-08-27）：.22 gateway 整條不通時，四顆 JC-100 各花
	// 滿 1000ms timeout，輪一圈就 4 秒，而每一次的整整 1 秒都握著 TCP_client 的
	// socket_mtx。同一條 bus 上的其他指令（PWM 寫入、PQW 繼電器）得排在後面等鎖，
	// 表現出來就是「按了按鈕過超久才有反應」。故障時整條 bus 等於自我癱瘓。
	//
	// ⚠ 刻意「縮短 timeout」而不是「跳過發包」：
	//   跳過的話，上層那些靠重試次數判斷吸附狀態的迴圈（smart_extend /
	//   disable_seal 的 read_err_cnt）會拿到一連串立即失敗，等於偷偷改掉了它們
	//   的語意。縮短只影響「失敗要等多久」，重試次數、error_flag 行為都不變。
	//
	// PROBE_EVERY：不能永遠停在短 timeout，否則 bus 復原後若回覆稍慢就再也回不
	// 來。每 N 次補一次完整 timeout 當探針。
	static constexpr int FAST_FAIL_AFTER  = 3;      // 連續失敗幾次後開始 fast-fail
	static constexpr int FAST_RECV_MS     = 100;    // fast-fail 時的 recv timeout（正常回覆 <20ms @115200）
	static constexpr int NORMAL_RECV_MS   = 1000;   // 原本的值，成功時沿用
	static constexpr int PROBE_EVERY      = 10;     // 每 N 次失敗補一次完整 timeout
	int  _consec_fail    = 0;
	bool _fast_fail_noted = false;   // 進入/離開 fast-fail 各只印一次，避免 log 洪水

	bool send_command(uint8_t func, uint16_t reg, uint16_t data, std::vector<uint8_t>& res);
};

#endif
