#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

#include <string>
#include <vector>

enum SerialConfig {
	SERIAL_5N1, SERIAL_6N1, SERIAL_7N1, SERIAL_8N1,
	SERIAL_5N2, SERIAL_6N2, SERIAL_7N2, SERIAL_8N2,
	SERIAL_5E1, SERIAL_6E1, SERIAL_7E1, SERIAL_8E1,
	SERIAL_5E2, SERIAL_6E2, SERIAL_7E2, SERIAL_8E2,
	SERIAL_5O1, SERIAL_6O1, SERIAL_7O1, SERIAL_8O1, 
	SERIAL_5O2, SERIAL_6O2, SERIAL_7O2, SERIAL_8O2  
};

class Serial_port {
public:
	Serial_port();
	~Serial_port();

	bool init(const std::string& port_name, int baudrate = 115200, SerialConfig config = SERIAL_8N1, bool debug = false);
	bool connect();
	void disconnect();
	bool reconnect();
	bool is_connected() const;

	int send(const char* data, int length, int tx_multiplier = 10, int tx_constant = 50);
	int receive(char* buffer, int length, int rx_idle_ms = 1);

private:
#ifdef _WIN32
	void* hSerial;
#else
	int fd;
#endif
	std::string port_name;
	int baudrate;
	SerialConfig config;
	bool connected;
	bool debug_mode;
	std::string _log_tag;

	bool configure_port();
};

#endif