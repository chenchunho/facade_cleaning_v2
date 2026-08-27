#include "Serial_port.h"
#include "log_utils.h"
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#endif

//=========== init ===========

Serial_port::Serial_port()
	: baudrate(115200), config(SERIAL_8N1), connected(false), debug_mode(false)
{
	_log_tag = "SER";
#ifdef _WIN32
	hSerial = (void*)INVALID_HANDLE_VALUE;
#else
	fd = -1;
#endif
}

Serial_port::~Serial_port() {
	disconnect();
}

bool Serial_port::init(const std::string& port, int br, SerialConfig cfg, bool debug) {
	port_name = port;
	baudrate = br;
	config = cfg;
	debug_mode = debug;
	_log_tag = "SER:" + port;
	return connect();
}

bool Serial_port::is_connected() const {
	return connected;
}

//=========== utility: send/receive ===========

int Serial_port::send(const char* data, int length, int tx_multiplier, int tx_constant) {
	if (!connected) return -1;

#ifdef _WIN32
	COMMTIMEOUTS tmo;
	if (GetCommTimeouts((HANDLE)hSerial, &tmo)) {
		tmo.WriteTotalTimeoutMultiplier = tx_multiplier;
		tmo.WriteTotalTimeoutConstant = tx_constant;
		SetCommTimeouts((HANDLE)hSerial, &tmo);
	}
	DWORD dwWritten = 0;
	if (!WriteFile((HANDLE)hSerial, data, (DWORD)length, &dwWritten, nullptr)) {
		connected = false;
		return -1;
	}
	int written = (int)dwWritten;
#else
	int written = write(fd, data, length);
	if (written < 0) {
		connected = false;
		return -1;
	}
#endif

	if (written > 0) LOG_HEX(_log_tag, "TX", data, written);
	return written;
}

int Serial_port::receive(char* buffer, int length, int rx_idle_ms) {
	if (!connected) return -1;

#ifdef _WIN32
	COMMTIMEOUTS tmo;
	if (GetCommTimeouts((HANDLE)hSerial, &tmo)) {
		tmo.ReadIntervalTimeout = rx_idle_ms;
		tmo.ReadTotalTimeoutMultiplier = 0;
		tmo.ReadTotalTimeoutConstant = 0;
		SetCommTimeouts((HANDLE)hSerial, &tmo);
	}
	DWORD dwRead = 0;
	if (!ReadFile((HANDLE)hSerial, buffer, (DWORD)length, &dwRead, nullptr)) {
		connected = false;
		return -1;
	}
	int bytes_read = (int)dwRead;
#else
	struct termios tty;
	if (tcgetattr(fd, &tty) != 0) {
		connected = false;
		return -1;
	}
	// VTIME unit is 0.1s
	tty.c_cc[VTIME] = (rx_idle_ms <= 100) ? 1 : (rx_idle_ms / 100);
	tty.c_cc[VMIN] = 0;
	tcsetattr(fd, TCSANOW, &tty);
	int bytes_read = read(fd, buffer, length);
	if (bytes_read < 0) {
		connected = false;
		return -1;
	}
#endif

	if (bytes_read > 0) LOG_HEX(_log_tag, "RX", buffer, bytes_read);
	return bytes_read;
}

//=========== init: connect / disconnect ===========

bool Serial_port::connect() {
#ifdef _WIN32
	std::string full_path = port_name;
	if (port_name.find("\\\\.\\") == std::string::npos && port_name.length() >= 4) {
		full_path = "\\\\.\\" + port_name;
	}
	hSerial = (void*)CreateFileA(full_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if ((HANDLE)hSerial == INVALID_HANDLE_VALUE) {
		LOG_ERR(_log_tag, "Failed to open port %s (System Error Code: %lu)",
			port_name.c_str(), (unsigned long)GetLastError());
		return false;
	}
#else
	fd = open(port_name.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd < 0) {
		LOG_ERR(_log_tag, "Failed to open port %s (errno: %s)", port_name.c_str(), strerror(errno));
		return false;
	}
	fcntl(fd, F_SETFL, 0);
#endif

	if (!configure_port()) {
		LOG_ERR(_log_tag, "Configuration failed for %s", port_name.c_str());
		disconnect();
		return false;
	}

	connected = true;
	LOG_INF(_log_tag, "Connected successfully to: %s", port_name.c_str());
	return true;
}

void Serial_port::disconnect() {
#ifdef _WIN32
	if ((HANDLE)hSerial != INVALID_HANDLE_VALUE) {
		CloseHandle((HANDLE)hSerial);
		hSerial = (void*)INVALID_HANDLE_VALUE;
	}
#else
	if (fd >= 0) {
		close(fd);
		fd = -1;
	}
#endif
	connected = false;
}

bool Serial_port::reconnect() {
	this->disconnect();

	// wait for hardware to release resources
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	// port_name and baudrate already set in init(), just call connect()
	return this->connect();
}

//=========== utility: configure port ===========

bool Serial_port::configure_port() {
#ifdef _WIN32
	DCB dcb = { 0 };
	dcb.DCBlength = sizeof(dcb);
	if (!GetCommState((HANDLE)hSerial, &dcb)) return false;

	dcb.BaudRate = baudrate;
	dcb.fBinary = TRUE;
	dcb.fDtrControl = DTR_CONTROL_ENABLE;
	dcb.fRtsControl = RTS_CONTROL_ENABLE;
	dcb.ByteSize = (config % 4) + 5;

	if (config >= SERIAL_5E1 && config <= SERIAL_8E2) {
		dcb.Parity = EVENPARITY; dcb.fParity = TRUE;
	}
	else if (config >= SERIAL_5O1 && config <= SERIAL_8O2) {
		dcb.Parity = ODDPARITY; dcb.fParity = TRUE;
	}
	else {
		dcb.Parity = NOPARITY; dcb.fParity = FALSE;
	}

	if ((config >= 4 && config <= 7) || (config >= 12 && config <= 15) || (config >= 20 && config <= 23)) {
		dcb.StopBits = TWOSTOPBITS;
	}
	else {
		dcb.StopBits = ONESTOPBIT;
	}

	{
		const char* p_str = (dcb.Parity == EVENPARITY) ? "Even" : (dcb.Parity == ODDPARITY ? "Odd" : "None");
		LOG_DBG(_log_tag, "DCB: Port=%s BaudRate=%lu ByteSize=%d Parity=%s StopBits=%s",
			port_name.c_str(), (unsigned long)dcb.BaudRate, (int)dcb.ByteSize,
			p_str, (dcb.StopBits == TWOSTOPBITS ? "2" : "1"));
	}

	return SetCommState((HANDLE)hSerial, &dcb);
#else
	struct termios tty {};
	if (tcgetattr(fd, &tty) != 0) return false;

	// 1. set baud rate
	speed_t speed;
	switch (baudrate) {
	case 9600:   speed = B9600; break;
	case 19200:  speed = B19200; break;
	case 38400:  speed = B38400; break;
	case 57600:  speed = B57600; break;
	case 115200: speed = B115200; break;
	default:     speed = B115200;
	}
	cfsetispeed(&tty, speed);
	cfsetospeed(&tty, speed);

	// 2. clear basic bits (data bits, parity, stop bits, hw flow control)
	tty.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
	tty.c_cflag |= (CLOCAL | CREAD);

	// 3. set data bits (grouped 4 per set: 5, 6, 7, 8)
	int data_bits = (config % 4) + 5;
	if (data_bits == 5) tty.c_cflag |= CS5;
	else if (data_bits == 6) tty.c_cflag |= CS6;
	else if (data_bits == 7) tty.c_cflag |= CS7;
	else tty.c_cflag |= CS8;

	// 4. set parity
	if (config >= SERIAL_5E1 && config <= SERIAL_8E2) {
		tty.c_cflag |= PARENB;
		tty.c_iflag |= (INPCK | ISTRIP);
	}
	else if (config >= SERIAL_5O1 && config <= SERIAL_8O2) {
		tty.c_cflag |= (PARENB | PARODD);
		tty.c_iflag |= (INPCK | ISTRIP);
	}
	else {
		tty.c_iflag &= ~(INPCK | ISTRIP);
	}

	// 5. set stop bits (index 4-7 N2, 12-15 E2, 20-23 O2 need 2 stop bits)
	bool is_two_stop_bits = (config >= 4 && config <= 7) ||
		(config >= 12 && config <= 15) ||
		(config >= 20 && config <= 23);
	if (is_two_stop_bits) {
		tty.c_cflag |= CSTOPB;
	}

	// 6. raw mode
	tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	tty.c_iflag &= ~(IXON | IXOFF | IXANY);
	tty.c_oflag &= ~OPOST;

	// 7. write to hardware
	if (tcsetattr(fd, TCSANOW, &tty) != 0) return false;

	// 8. read back to verify
	struct termios v_tty {};
	if (tcgetattr(fd, &v_tty) == 0) {
		int r_bits = 0;
		if ((v_tty.c_cflag & CSIZE) == CS5) r_bits = 5;
		else if ((v_tty.c_cflag & CSIZE) == CS6) r_bits = 6;
		else if ((v_tty.c_cflag & CSIZE) == CS7) r_bits = 7;
		else r_bits = 8;

		const char* r_parity = (v_tty.c_cflag & PARENB) ?
			((v_tty.c_cflag & PARODD) ? "Odd" : "Even") : "None";
		int r_stop = (v_tty.c_cflag & CSTOPB) ? 2 : 1;

		LOG_DBG(_log_tag, "HW verify: Port=%s BaudRate=%d ByteSize=%d Parity=%s StopBits=%d",
			port_name.c_str(), baudrate, r_bits, r_parity, r_stop);
	}

	return true;
#endif
}
