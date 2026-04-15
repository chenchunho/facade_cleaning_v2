#include "WT901BC_TTL.h"
#include "log_utils.h"
#include <cmath>

//=========== init ===========

WT901BC_TTL::WT901BC_TTL()
	: ax(0), ay(0), az(0), gx(0), gy(0), gz(0),
	x(0), y(0), z(0), pressure(0), altitude(0),
	read_error(false), error_count(0),
	_serial(nullptr), debug_mode(false), _buf_count(0), _running(false) {
	_log_tag = "WT901";
}

WT901BC_TTL::~WT901BC_TTL() {
	stop();
}

void WT901BC_TTL::init(Serial_port* com, bool debug) {
	_serial = com;
	debug_mode = debug;

	if (_serial && _serial->is_connected()) {
		_running = true;
		_worker_thread = std::thread(&WT901BC_TTL::update, this);
		LOG_INF(_log_tag, "Thread started");
	}
}

void WT901BC_TTL::stop() {
	_running = false;
	if (_worker_thread.joinable()) {
		_worker_thread.join();
	}
}

//=========== worker thread ===========

void WT901BC_TTL::update() {
	// initialize timestamp so startup doesn't immediately trigger timeout
	_last_update_time = std::chrono::steady_clock::now();
	while (_running) {
		// 1. Check if the serial object exists or is disconnected
		if (!_serial || !_serial->is_connected()) {
			LOG_ERR(_log_tag, "Connection lost. Attempting to reconnect...");

			read_error = true;

			// Try to reconnect through the serial port's own reconnect method
			if (_serial && _serial->reconnect()) {
				LOG_INF(_log_tag, "Reconnected successfully");
				read_error = false;
				_buf_count = 0;
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
		// timeout check: no valid packet for > 500ms
		auto now = std::chrono::steady_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - _last_update_time).count();

		if (duration > 500) {
			if (!read_error) {
				read_error = true;
				if (error_count < 9999) error_count++;
			}
		}
		// Small sleep to prevent 100% CPU usage if no data is coming in
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

//=========== utility: validation ===========

bool WT901BC_TTL::validateChecksum(uint8_t* buf) {
	uint8_t sum = 0;
	for (int i = 0; i < 10; i++) {
		sum += buf[i];
	}
	return (sum == buf[10]);
}

//=========== utility: debug ===========

void WT901BC_TTL::printDebugInfo(uint8_t* buf) {
	LOG_HEX(_log_tag, "RX", buf, 11);
}

//=========== read: parse packet ===========

void WT901BC_TTL::parsePacket(uint8_t* buf) {
	_last_update_time = std::chrono::steady_clock::now();
	uint8_t type = buf[1];
	short d1 = (short)((buf[3] << 8) | buf[2]);
	short d2 = (short)((buf[5] << 8) | buf[4]);
	short d3 = (short)((buf[7] << 8) | buf[6]);

	std::lock_guard<std::mutex> lock(_data_mutex);
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
	case 0x56: // pressure packet
	{
		// combine 4 bytes to get raw pressure (Pa), use uint32_t to avoid shift overflow
		uint32_t p_raw = ((uint32_t)buf[5] << 24) | ((uint32_t)buf[4] << 16) | ((uint32_t)buf[3] << 8) | buf[2];

		// convert to hPa
		pressure = (double)(int32_t)p_raw / 100.0;

		calculateAltitude();
		break;
	}
	}
}

//=========== utility: altitude calculation ===========

void WT901BC_TTL::calculateAltitude() {
	if (pressure <= 0) return;

	// sea-level standard atmospheric pressure (hPa)
	const double P0 = 1013.25;

	// international barometric formula
	// Altitude = 44330 * (1 - (P/P0)^(1/5.255))
	altitude = 44330.0 * (1.0 - std::pow(pressure / P0, 1.0 / 5.255));
}
