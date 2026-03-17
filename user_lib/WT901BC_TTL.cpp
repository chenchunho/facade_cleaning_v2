#include "WT901BC_TTL.h"
#include <iomanip>
#include <cmath>

WT901BC_TTL::WT901BC_TTL()
	: ax(0), ay(0), az(0), gx(0), gy(0), gz(0),
	x(0), y(0), z(0), pressure(0), altitude(0),
	read_error(false), error_count(0),
	_serial(nullptr), _debug(false), _buf_count(0), _running(false) {
}

WT901BC_TTL::~WT901BC_TTL() {
	stop();
}

void WT901BC_TTL::init(Serial_port* com, bool debug) {
	_serial = com;
	_debug = debug;

	if (_serial && _serial->is_connected()) {
		_running = true;
		_worker_thread = std::thread(&WT901BC_TTL::update, this);
		if (_debug) std::cout << "[WT901BC] Thread started." << std::endl;
	}
}

void WT901BC_TTL::stop() {
	_running = false;
	if (_worker_thread.joinable()) {
		_worker_thread.join();
	}
}

void WT901BC_TTL::update() {
	// 初始化時間，避免啟動瞬間就判定超時
	_last_update_time = std::chrono::steady_clock::now();
	while (_running) {
		// 1. Check if the serial object exists or is disconnected
		if (!_serial || !_serial->is_connected()) {
			if (_debug) std::cerr << "[WT901BC] Connection lost. Attempting to reconnect..." << std::endl;

			read_error = true;

			// Try to reconnect through the serial port's own reconnect method
			// Note: You need to implement 'reconnect()' in your Serial_port class
			if (_serial && _serial->reconnect()) {
				if (_debug) std::cout << "[WT901BC] Reconnected successfully!" << std::endl;
				read_error = false;
			}
			else {
				// Wait longer between reconnection attempts to avoid pegging the CPU
				std::this_thread::sleep_for(std::chrono::seconds(1));
				continue;
			}
		}

		// 2. Normal data reading logic
		char rx_byte;
		// Added a check to ensure we don't hang here if _running becomes false
		while (_running && _serial->receive(&rx_byte, 1, 0) > 0) {
			uint8_t data = (uint8_t)rx_byte;

			// Sync: Look for header 0x55
			if (_buf_count == 0 && data != 0x55) continue;

			_msg_buf[_buf_count++] = data;

			if (_buf_count == 11) {
				if (validateChecksum(_msg_buf)) {
					read_error = false;
					parsePacket(_msg_buf);
				}
				else {
					read_error = true;
					if (error_count < 9999) error_count++;

					// Buffer recovery: if checksum fails, reset to find next 0x55
					_buf_count = 0;
					continue;
				}
				_buf_count = 0;
			}
		}
		// --- 新增：1 秒超時檢查邏輯 ---
		auto now = std::chrono::steady_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - _last_update_time).count();

		if (duration > 500) { // 超過 1000ms (1秒)
			if (!read_error) { // 避免重複累加 error_count，僅在狀態切換時處理
				read_error = true;
				if (error_count < 9999) error_count++;
			}
		}
		else if (duration < 1000 && !_serial->is_connected() == false) {
			// 如果在 1 秒內且連線正常，且沒有校驗錯誤，read_error 會在 parsePacket 或此處被維護
		}
		// Small sleep to prevent 100% CPU usage if no data is coming in
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

bool WT901BC_TTL::validateChecksum(uint8_t* buf) {
	uint8_t sum = 0;
	for (int i = 0; i < 10; i++) {
		sum += buf[i];
	}
	return (sum == buf[10]);
}

void WT901BC_TTL::printDebugInfo(uint8_t* buf) {
	std::cout << "[WT901BC RX] ";
	for (int i = 0; i < 11; ++i) {
		std::cout << std::hex << std::setw(2) << std::setfill('0') << std::uppercase
			<< (int)buf[i] << " ";
	}
	std::cout << std::dec << std::endl;
}

void WT901BC_TTL::parsePacket(uint8_t* buf) {
	_last_update_time = std::chrono::steady_clock::now();
	uint8_t type = buf[1];
	short d1 = (short)((buf[3] << 8) | buf[2]);
	short d2 = (short)((buf[5] << 8) | buf[4]);
	short d3 = (short)((buf[7] << 8) | buf[6]);

	switch (type) {
	case 0x51:
		ax = (double)d1 / 32768.0 * 16.0;
		ay = (double)d2 / 32768.0 * 16.0;
		az = (double)d3 / 32768.0 * 16.0;
		break;
	case 0x52:
		gx = (double)d1 / 32768.0 * 2000.0;
		gy = (double)d2 / 32768.0 * 2000.0;
		gz = (double)d3 / 32768.0 * 2000.0;
		break;
	case 0x53:
		x = (double)d1 / 32768.0 * 180.0;
		y = (double)d2 / 32768.0 * 180.0;
		z = (double)d3 / 32768.0 * 180.0;
		break;
	case 0x56: // 氣壓封包
	{
		// 1. 合併 4 bytes 取得原始氣壓值 (Pa)
		long p_raw = (long)((buf[5] << 24) | (buf[4] << 16) | (buf[3] << 8) | buf[2]);

		// 2. 換算成 hPa (除以 100.0)
		pressure = (double)p_raw / 100.0;

		calculateAltitude();
		break;
	}
	}
}

void WT901BC_TTL::calculateAltitude() {
	if (pressure <= 0) return;

	// 海平面標準大氣壓 (hPa)
	const double P0 = 1013.25;

	// 國際壓高公式
	// Altitude = 44330 * (1 - (P/P0)^(1/5.255))
	altitude = 44330.0 * (1.0 - std::pow(pressure / P0, 1.0 / 5.255));
}