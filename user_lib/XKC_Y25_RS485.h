#ifndef XKC_Y25_RS485_H
#define XKC_Y25_RS485_H

#include "TCP_client.h"
#include <string>
#include <vector>
#include <cstdint>

/******************************************************
 *  XKC_Y25_RS485 — Non-contact water level sensor (capacitive)
 *  Mfr: Shenzhen XingKeChuang  Model: XKC-Y25-RS485 (V1.6 manual)
 *
 *  - Senses through non-metal container wall (≤ 20 mm)
 *  - Modbus-RTU over RS-485, default 9600 8N1, slave addr 0x01
 *  - Output is binary: liquid present / not present (capacitive
 *    threshold compared against RSSI; sensor itself debounces)
 *
 *  Register map (per V1.6 manual §2.1):
 *    0x0000  reserved
 *    0x0001  OutPut       (0 = no liquid, 1 = liquid detected)  R
 *    0x0002  RSSI         (< 3900 = none, > 4100 = liquid, between = hold)  R
 *    0x0003  slave addr   (1..254)  R/W   default 0x0001
 *    0x0004  baud code    (0x05..0x0F)  R/W  default 0x0007 (9600)
 *
 *  --- Init (one of) ---
 *      XKC_Y25_RS485 lvl;
 *      lvl.init("192.168.1.22", 4001, 1, true);    // own TCP client
 *
 *      TCP_client cli; cli.connectToServer("192.168.1.22", 4001);
 *      lvl.init(cli, 1, true);                     // share external client
 *
 *  --- Read ---
 *      uint16_t output = 0, rssi = 0;
 *      if (!lvl.read_state(output, rssi))
 *          // output = 1 means liquid detected, rssi is signal strength
 *
 *      bool wet = lvl.has_liquid();   // convenience
 ******************************************************/

class XKC_Y25_RS485 {
public:
    XKC_Y25_RS485();
    ~XKC_Y25_RS485();

    //=========== init ===========

    bool init(const std::string& ip, int port, int ID = 1, bool debug = false);
    bool init(TCP_client& extClient, int ID = 1, bool debug = false);

    //=========== read ===========

    // Read OutPut (0x0001) + RSSI (0x0002) in one Modbus frame.
    // Returns false on success (output / rssi populated), true on comms / CRC error.
    // On error, output / rssi keep their last successful values (cached).
    bool read_state(uint16_t& output, uint16_t& rssi);

    // Convenience: returns true if liquid detected.
    // On comms error, returns last cached value (default false at boot).
    bool has_liquid();

    // Last successful read — useful for status queries that should not trigger Modbus traffic.
    uint16_t last_output() const { return _last_output; }
    uint16_t last_rssi()   const { return _last_rssi;   }

    //=========== config ===========

    // Set new slave address (1..254). Writes register 0x0003.
    // CAUTION: after this call the sensor responds at the new address — current
    // _slaveID member is NOT updated automatically; caller should re-init() if it
    // wants to keep talking to the same physical sensor.
    bool set_address(uint8_t new_addr);

    // Set baud rate by code (per manual table §1.9): 5=2400, 6=4800, 7=9600,
    // 8=14400, 9=19200, A=28800, C=57600, D=115200, E=128000, F=256000.
    // Writes register 0x0004. Sensor restarts UART; LED flashes on success.
    bool set_baud_rate(uint8_t code);

    //=========== utility ===========

    int error_flag;   // 0 = ok on last call, 1 = error on last call

private:
    int          _slaveID;
    bool         debug_mode;
    TCP_client*  client;
    bool         _isExternalClient;
    std::string  _log_tag;

    // Cached last successful read (returned on subsequent comms error).
    uint16_t     _last_output;
    uint16_t     _last_rssi;

    uint16_t modbusCRC(const uint8_t* data, int len);
    bool sendRecv(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx, int expected_rx_len);
};

#endif
