#include "Serial_port.h"
#include <iostream>
#include <iomanip>
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

/* ========== 初始化與工具函數 ========== */

Serial_port::Serial_port()
	: baudrate(115200), config(SERIAL_8N1), connected(false), debug_mode(false)
{
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
	return connect();
}

bool Serial_port::is_connected() const {
	return connected;
}

void Serial_port::print_hex(const std::string& label, const char* data, int length) {
	if (length <= 0) return;
	std::cout << "[Serial_port](" << port_name << ") " << label << " : ";
	for (int i = 0; i < length; ++i) {
		std::cout << std::hex << std::setw(2) << std::setfill('0') << std::uppercase
			<< (int)(unsigned char)data[i] << (i == length - 1 ? "" : " ");
	}
	std::cout << std::dec << std::endl;
}

/* ========== 資料收發 (被動觸發斷線狀態) ========== */

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

	if (debug_mode && written > 0) print_hex("TX", data, written);
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
	// VTIME 單位是 0.1s
	tty.c_cc[VTIME] = (rx_idle_ms <= 100) ? 1 : (rx_idle_ms / 100);
	tty.c_cc[VMIN] = 0;
	tcsetattr(fd, TCSANOW, &tty);
	int bytes_read = read(fd, buffer, length);
	if (bytes_read < 0) {
		connected = false;
		return -1;
	}
#endif

	if (debug_mode && bytes_read > 0) print_hex("RX", buffer, bytes_read);
	return bytes_read;
}

/* ========== 連線與斷線實作 ========== */

bool Serial_port::connect() {
#ifdef _WIN32
	std::string full_path = port_name;
	if (port_name.find("\\\\.\\") == std::string::npos && port_name.length() >= 4) {
		full_path = "\\\\.\\" + port_name;
	}
	hSerial = (void*)CreateFileA(full_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if ((HANDLE)hSerial == INVALID_HANDLE_VALUE) {
		if (debug_mode) {
			std::cerr << "[Serial_port] Error: Failed to open port " << port_name
				<< " (System Error Code: " << GetLastError() << ")" << std::endl;
		}
		return false;
	}
#else
	fd = open(port_name.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd < 0) {
		if (debug_mode) {
			std::cerr << "[Serial_port] Error: Failed to open port " << port_name
				<< " (errno: " << strerror(errno) << ")" << std::endl;
		}
		return false;
	}
	fcntl(fd, F_SETFL, 0);
#endif

	if (!configure_port()) {
		if (debug_mode) {
			std::cerr << "[Serial_port] Error: Configuration failed for " << port_name << std::endl;
		}
		disconnect();
		return false;
	}

	connected = true;
	if (debug_mode) {
		std::cout << "[Serial_port] Connected successfully to: " << port_name << std::endl;
	}
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
	// 1. 關閉目前的連線 (使用你類別中正確的函式名 disconnect)
	this->disconnect();

	// 2. 等待一下讓硬體釋放資源
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	// 3. 嘗試重新連線 (使用你類別中正確的函式名 connect，以及正確的變數名)
	// 因為 init 時已經把 port_name 和 baudrate 存下來了，直接呼叫 connect() 即可
	return this->connect();
}

/* ========== 串口參數配置 (與原本邏輯一致) ========== */

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

	if (debug_mode) {
		std::string p_str = (dcb.Parity == EVENPARITY) ? "Even" : (dcb.Parity == ODDPARITY ? "Odd" : "None");
		std::cout << "\n--- [Serial_port Debug] Windows DCB Settings ---" << "\n"
			<< " Port      : " << port_name << "\n"
			<< " BaudRate  : " << dcb.BaudRate << "\n"
			<< " ByteSize  : " << (int)dcb.ByteSize << "\n"
			<< " Parity    : " << p_str << "\n"
			<< " StopBits  : " << (dcb.StopBits == TWOSTOPBITS ? "2" : "1") << "\n"
			<< "------------------------------------------------" << std::endl;
	}

	return SetCommState((HANDLE)hSerial, &dcb);
#else
	struct termios tty {};
	if (tcgetattr(fd, &tty) != 0) return false;

	// 1. 設定波特率
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

	// 2. 清除基本設定位元 (資料位、同位位、停止位、硬體流控)
	tty.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
	tty.c_cflag |= (CLOCAL | CREAD);

	// 3. 運算邏輯：設定資料位元 (Data Bits)
	// 根據列舉順序，每 4 個一組循環 (5, 6, 7, 8)
	int data_bits = (config % 4) + 5;
	if (data_bits == 5) tty.c_cflag |= CS5;
	else if (data_bits == 6) tty.c_cflag |= CS6;
	else if (data_bits == 7) tty.c_cflag |= CS7;
	else tty.c_cflag |= CS8;

	// 4. 運算邏輯：設定同位檢查 (Parity)
	if (config >= SERIAL_5E1 && config <= SERIAL_8E2) {
		tty.c_cflag |= PARENB;        // 啟用 Even
		tty.c_iflag |= (INPCK | ISTRIP);
	}
	else if (config >= SERIAL_5O1 && config <= SERIAL_8O2) {
		tty.c_cflag |= (PARENB | PARODD); // 啟用 Odd
		tty.c_iflag |= (INPCK | ISTRIP);
	}
	else {
		tty.c_iflag &= ~(INPCK | ISTRIP); // None
	}

	// 5. 運算邏輯：設定停止位 (Stop Bits)
	// 索引 4-7(N2), 12-15(E2), 20-23(O2) 需要 2 個停止位
	bool is_two_stop_bits = (config >= 4 && config <= 7) ||
		(config >= 12 && config <= 15) ||
		(config >= 20 && config <= 23);
	if (is_two_stop_bits) {
		tty.c_cflag |= CSTOPB;
	}

	// 6. 設定原始模式 (Raw Mode)
	tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	tty.c_iflag &= ~(IXON | IXOFF | IXANY);
	tty.c_oflag &= ~OPOST;

	// 7. 寫入硬體
	if (tcsetattr(fd, TCSANOW, &tty) != 0) return false;

	// 8. 讀出檢查 (Verify)
	struct termios v_tty {};
	if (tcgetattr(fd, &v_tty) == 0 && debug_mode) {
		int r_bits = 0;
		if ((v_tty.c_cflag & CSIZE) == CS5) r_bits = 5;
		else if ((v_tty.c_cflag & CSIZE) == CS6) r_bits = 6;
		else if ((v_tty.c_cflag & CSIZE) == CS7) r_bits = 7;
		else r_bits = 8;

		std::string r_parity = (v_tty.c_cflag & PARENB) ?
			((v_tty.c_cflag & PARODD) ? "Odd" : "Even") : "None";
		int r_stop = (v_tty.c_cflag & CSTOPB) ? 2 : 1;

		std::cout << "\n--- [Serial_port Debug] Hardware Verification ---" << "\n"
			<< " Port      : " << port_name << "\n"
			<< " BaudRate  : " << baudrate << "\n"
			<< " ByteSize  : " << r_bits << "\n"
			<< " Parity    : " << r_parity << "\n"
			<< " StopBits  : " << r_stop << "\n"
			<< "--------------------------------------------------" << std::endl;
	}

	return true;
#endif
}