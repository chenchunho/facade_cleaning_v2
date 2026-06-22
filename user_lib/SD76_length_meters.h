#pragma once
#include <stdint.h>
#include <string>
#include "TCP_client.h"

class SD76_length_meters {
public:
	SD76_length_meters();
	~SD76_length_meters();

	bool init(const std::string& ip, int port, int id, bool debug = false);
	bool init(TCP_client& extClient, int id, bool debug = false);

	// 單讀上排（0x0001~0x0002）
	bool readUpperDisplayValue(int& value);

	// 讀取上下兩排（0x0001~0x0004）
	bool readUpperLowerDisplayValue(int& upper, int& lower);

	// Read status register (0x0000): high byte=work mode, low byte=alarm status
	bool readStatus(uint8_t& workMode, uint8_t& alarmStatus);

	// Read integer display values (signed int32, alternative to BCD)
	bool readUpperInteger(int32_t& value);   // 0x0021~0x0022
	bool readLowerInteger(int32_t& value);   // 0x0023~0x0024

	// Control
	bool resetAll();         // Write 0x0003 to reg 0x0000
	bool pauseMeter();       // Write 0x0004 to reg 0x0000
	bool resumeMeter();      // Write 0x0008 to reg 0x0000

	//=========== calibration (SD76 EEPROM-side scale) ===========
	//
	// SD76 stores its display scaling in two registers:
	//   0x0014-0x0015  SCAL (R/W, 6-digit BCD, low 3 bytes valid)  — K factor
	//   0x0020         DP   (R/W, low byte=upper DP, high byte=lower DP) — decimal point
	//
	// CRITICAL — bench finding 2026-05-09 (this SD76-C unit):
	//   The device treats SCAL as a DIVISOR, not a multiplier.
	//     display_units = pulses ÷ (SCAL × 10^(-DP))
	//   Bigger SCAL → fewer display units per pulse.
	//   Manual labels it "Counter Multiplier"; bench observation says otherwise.
	//   (Confirmed by varying SCAL and observing display delta per fixed pull.)
	//
	// To shield application code from this gotcha, the get/setEffectiveScale
	// API exposes a MULTIPLIER semantic — bigger M = more display per pulse:
	//   effective M (multiplier) = 10^DP / SCAL
	//   SCAL on write              = round(10^DP / M)
	// scaleByRatio(r) multiplies the application-facing M by r, which means
	// internally divides SCAL by r. So cal_set passing ratio=actual/observed
	// (the natural sense) does the right thing.
	//
	// Writes go to SD76 EEPROM. Bench-verify persistence on first deploy; if
	// the value doesn't stick across power cycle, add save_params() per
	// DSZL_107 pattern (try writing magic value to control register 0x0000).

	// Read raw SCAL (0..999999) and upper-display DP (0..5 typical).
	// "raw" — not the application-facing multiplier; use getEffectiveScale for that.
	bool readScale(uint32_t& scal_value, uint8_t& dp_upper);

	// Write raw SCAL. By default preserves the device's current DP (read-modify-
	// write to keep lower-display DP byte intact); pass write_dp=true to also
	// change DP. SCAL must be in [1, 999999]; DP in [0, 5].
	bool writeScale(uint32_t scal_value, uint8_t dp_upper, bool write_dp = false);

	// Read effective multiplier as application sees it (M = display / pulses,
	// inverted from device's K-factor convention — see comment above).
	bool getEffectiveScale(double& multiplier);

	// Set effective multiplier (application-facing). Driver internally inverts
	// to SD76's K-factor convention. PRESERVES device DP — SD76 firmware
	// rejects Modbus writes to DP while in 通訊模式 (00-16=3), same class of
	// mode-latch as SE3 H1000. To increase precision range, operator must set
	// DP from SD76 panel: 00-16=非3 → 改 DP → 00-16=3.
	// Returns true if multiplier doesn't fit current DP — error log hints at
	// the panel-side fix.
	bool setEffectiveScale(double multiplier);

	// Read current effective multiplier, multiply by ratio, write back.
	// Used by application calibration flow:
	//   ratio = actual_cm / observed_displayed_cm
	// Iterative — call multiple times if first pass leaves residual error.
	bool scaleByRatio(double ratio);

private:
	bool readRegister(uint16_t addr, uint16_t count, uint8_t* raw);
	bool sendModbus(const uint8_t* req, int reqLen, uint8_t* resp, int& respLen);
	bool writeSingleRegister(uint16_t addr, uint16_t value);
	bool writeMultipleRegisters(uint16_t addr, uint16_t count, const uint8_t* data, int dataLen);
	uint16_t crc16(const uint8_t* buf, int len);

	int  decodeSignedBCD6(const uint8_t raw[4]);    // signed 6-BCD + sign bit
	void encodeBCD6(uint32_t value, uint8_t out[4]); // unsigned 6-BCD (no sign byte)

private:
	TCP_client* client;
	bool owns;
	int deviceID;
	bool debug_mode;
	std::string _log_tag;
};
