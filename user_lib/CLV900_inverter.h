#pragma once
#include <stdint.h>
#include <string>
#include "TCP_client.h"

// ============================================================================
// CLV900_inverter — clvdrives 900 series VFD driver (model 900-0007M1)
//
// Reference doc:
//   doc/900系列通用变频器说明书-V900.24.01.16.C版本（6按键）.pdf
//   Chapter 6 "通讯" / 6.2 Modbus 寄存器定义
//
// Protocol: Modbus-RTU over RS485, accessed via USR-TCP232-304 (transparent).
//   Function codes supported by the inverter: 0x03 (read), 0x06 (write single).
//   No 0x10 multi-write, no bit-level read/write.
//   Max 12 consecutive registers per read.
//   Frame: standard Modbus-RTU with CRC16 (init 0xFFFF, poly 0xA001, LSB first).
//
// Key register map (subset used by this driver):
//
//   Control / setpoint (write 0x06):
//     0x0001  通讯设定频率   -10000..+10000  (10000 = 100% of F8-03 max freq;
//                                              negative = reverse direction)
//     0x0002  控制命令       1=run fwd / 2=run rev / 3=jog fwd / 4=jog rev
//                            5=free stop (coast) / 6=decel stop / 7=fault reset
//     0x0003  Relay/DO       BIT0..3 (only if F1-08=7 / F1-28=6 enable Modbus IO)
//     0x0004  AO1 output     0..0x7FFF (0..100%)
//     0x0005  AO2 output     0..0x7FFF (0..100%)
//
//   Function parameters Fx-yy (read 0x03 or write 0x06):
//     reg = 0xF000 + group*0x100 + index    (F0-00 = 0xF000, F9-40 = 0xF928)
//     F0-00  Run command source     2 = Modbus
//     F0-01  Freq command source    8 = Modbus
//     F7-00  Modbus slave address (this device)
//     F7-01  Baudrate               0=9600 1=19200 2=38400 3=57600 4=115200
//     F7-02  Data format            0=8N2 1=8E1 2=8O1 3=8N1
//     F7-03  Comm timeout (sec)     0.0 = disabled
//
//   Monitor parameters U0-yy (read 0x03 only):
//     reg = 0x1000 + group*0x100 + index    (U0-00 = 0x1000, U0-71 = 0x1047)
//     U0-00 (0x1000) Run state         1=fwd 2=rev 3=stop
//     U0-01 (0x1001) Fault code        0 = no fault
//     U0-02 (0x1002) Setpoint freq     0.1 Hz
//     U0-03 (0x1003) Running freq      0.1 Hz
//     U0-04 (0x1004) Speed             rpm
//     U0-05 (0x1005) Output voltage    V
//     U0-06 (0x1006) Output current    0.1 A
//     U0-07 (0x1007) Output power      0.1 kW
//     U0-08 (0x1008) Bus voltage       V
//     U0-09 (0x1009) Output torque     0.1 % (signed)
//     U0-26 (0x101A) IGBT temperature  °C
//
// Required keypad pre-configuration (one-time, before Modbus control works):
//   F0-00 = 2     run command via Modbus
//   F0-01 = 8     frequency setpoint via Modbus
//   F7-00 = <id>  this device's slave id
//   F7-01..03     match the RS485 line settings on the USR transparent bridge
//   F7-19 = 0     standard Modbus (driver assumes this; 1 = non-standard variant)
//   F7-20 = 0     parameter-compat flag off (default; legacy compatibility only)
// configureModbusControl() can write F0-00 / F0-01 once over the wire if the
// other side is already addressable (e.g. configured by keypad to factory).
// ============================================================================

class CLV900_inverter {
public:
	CLV900_inverter();
	~CLV900_inverter();

	//=========== init ===========

	bool init(const std::string& ip, int port, int id, bool debug = false);
	bool init(TCP_client& extClient, int id, bool debug = false);

	//=========== control (write reg 0x0002) ===========

	bool runForward();   // value 1
	bool runReverse();   // value 2
	bool stopDecel();    // value 6 — normal stop (ramps down per F3 settings)
	bool stopFree();     // value 5 — coast to stop (no ramp)
	bool resetFault();   // value 7
	bool jogForward();   // value 3
	bool jogReverse();   // value 4

	//=========== frequency setpoint (write reg 0x0001) ===========

	// Raw signed setpoint, range -10000..+10000.
	// 10000 = 100% of F8-03 motor max freq; sign = direction.
	bool setFreqRaw(int16_t value);

	// Convenience: percent of max freq, -100.00..+100.00.
	bool setFreqPercent(double pct);

	// Convenience: target Hz; max_hz is the motor's F8-03 (default 50.0).
	// Direction follows sign of hz; out-of-range values are clamped.
	bool setFreqHz(double hz, double max_hz = 50.0);

	//=========== monitor reads (U0-XX, reg 0x1000+) ===========

	bool readRunStatus(uint16_t& status);     // 1=fwd 2=rev 3=stop
	bool readFaultCode(uint16_t& code);       // 0 = no fault
	bool readSetFreq(double& hz);             // 0x1002 / 10
	bool readRunFreq(double& hz);             // 0x1003 / 10
	bool readSpeedRpm(uint16_t& rpm);
	bool readOutputVoltage(uint16_t& volts);
	bool readOutputCurrent(double& amps);     // 0x1006 / 10
	bool readBusVoltage(uint16_t& volts);
	bool readOutputTorque(double& pct);       // signed, 0x1009 / 10
	bool readIgbtTemp(uint16_t& celsius);

	//=========== generic param read/write ===========

	bool writeParam(uint16_t reg, uint16_t value);  // FC 0x06
	bool readParam(uint16_t reg, uint16_t& value);  // FC 0x03, single register

	// Address helpers for Fx-yy and U0-yy parameters.
	static uint16_t fxAddr(uint8_t group, uint8_t index) {
		return (uint16_t)(0xF000 + (group << 8) + index);
	}
	static uint16_t uxAddr(uint8_t group, uint8_t index) {
		return (uint16_t)(0x1000 + (group << 8) + index);
	}

	//=========== convenience ===========

	// Set F0-00 = 2 (run via Modbus) and F0-01 = 8 (freq via Modbus).
	// Saves a trip to the keypad if the unit is already reachable.
	bool configureModbusControl();

private:
	bool sendModbus(const uint8_t* req, int reqLen, uint8_t* resp, int& respLen);
	uint16_t crc16(const uint8_t* buf, int len);

	TCP_client* client;
	bool owns;
	int deviceID;
	bool debug_mode;
	std::string _log_tag;
};
