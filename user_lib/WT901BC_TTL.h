#ifndef WT901BC_TTL_H
#define WT901BC_TTL_H

#include "Serial_port.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

/**
 * @brief WT901BC (TTL 版本) 感測器驅動類別
 * * 使用範例 (包含錯誤處理顯示):
 * @code
 * Serial_port com;
 * if (com.connect("COM3", 115200)) {
 * WT901BC_TTL imu;
 * imu.init(&com, true); // 開啟 debug 模式
 * * while(true) {
 * if (imu.read_error) {
 * // 顯示錯誤狀態：可能是校驗失敗、斷線或超過 500ms 未收到資料
 * std::cerr << "--- [IMU ERROR] ---" << std::endl;
 * std::cerr << "累計錯誤次數: " << imu.error_count << std::endl;
 * } else {
 * // 正常讀取數據
 * std::cout << "角度 (x,y,z): " << imu.x << ", " << imu.y << ", " << imu.z << std::endl;
 * std::cout << "高度: " << imu.altitude << " m" << std::endl;
 * }
 * std::this_thread::sleep_for(std::chrono::milliseconds(100));
 * }
 * imu.stop();
 * }
 * @endcode
 */

class WT901BC_TTL {
public:
	double ax, ay, az;

	// [2026-08-27] 收到的封包類型計數。bench 上遇到 ax/ay/az 恆為 0 而角度有值，
	// 需要區分「模組沒送加速度封包(0x51)」與「送了但解析錯」——這兩個計數可以
	// 直接證實，不必開 debug_mode 看整片 hex dump。
	std::atomic<uint32_t> n_accel_pkt{0};   // 0x51
	std::atomic<uint32_t> n_angle_pkt{0};   // 0x53
	double gx, gy, gz;
	double x, y, z;
	double pressure;
	double altitude;

	// 錯誤追蹤變數 (atomic，執行緒安全)
	std::atomic<bool> read_error;      // 封包解析是否有誤
	std::atomic<int> error_count;      // 錯誤累計次數

	WT901BC_TTL();
	~WT901BC_TTL();

	void init(Serial_port* com, bool debug = false);
	void stop();

private:
	Serial_port* _serial;
	bool debug_mode;
	uint8_t _msg_buf[11];
	int _buf_count;
	std::string _log_tag;

	std::thread _worker_thread;
	std::atomic<bool> _running;
	std::mutex _data_mutex;
	std::chrono::steady_clock::time_point _last_update_time;


	void update();
	bool validateChecksum(uint8_t* buf);
	void parsePacket(uint8_t* buf);
	void printDebugInfo(uint8_t* buf);
	void calculateAltitude();
};

#endif

