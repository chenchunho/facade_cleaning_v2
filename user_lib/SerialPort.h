#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

// =====================================================================
// Cross-platform SerialPort.
//   Windows : WinAPI (HANDLE / DCB / COMMTIMEOUTS)
//   Linux   : POSIX termios + poll()
//
//   Public API is identical on both platforms:
//       SerialPort(std::string port, baudrate, int timeout_ms = 2)
//       ssize_t send(const uint8_t* data, size_t len)
//       ssize_t recv(uint8_t* data, size_t len)
//       void    recv(uint8_t* data, uint8_t head, ssize_t len)
//       void    set_timeout(int timeout_ms)
//       using   SharedPtr = std::shared_ptr<SerialPort>
//
//   Baudrate type differs:
//     Windows → uint32_t  (e.g. 921600u)
//     Linux   → speed_t   (e.g. B921600, from <termios.h>)
//
//   Typical usage:
//     Windows: std::make_shared<SerialPort>("\\\\.\\COM10", 921600u)
//     Linux:   std::make_shared<SerialPort>("/dev/ttyACM0", B921600)
// =====================================================================

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <queue>
#include <string>

inline void print_data(const uint8_t* data, uint8_t len)
{
    for (int i = 0; i < len; i++)
        printf("%02x ", data[i]);
    printf("\n");
}

// ==========================================================================
#ifdef _WIN32
// ==========================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#if !defined(_SSIZE_T_DEFINED) && !defined(ssize_t)
typedef long long ssize_t;
#define _SSIZE_T_DEFINED
#endif

class SerialPort
{
public:
    using SharedPtr = std::shared_ptr<SerialPort>;

    SerialPort(std::string port, uint32_t baudrate, int timeout_ms = 2)
        : handle_(INVALID_HANDLE_VALUE), timeout_ms_(timeout_ms)
    {
        Init(port, baudrate);
        set_timeout(timeout_ms);
    }

    ~SerialPort()
    {
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
    }

    ssize_t send(const uint8_t* data, size_t len)
    {
        if (handle_ == INVALID_HANDLE_VALUE) return -1;
        DWORD written = 0;
        if (!WriteFile(handle_, data, static_cast<DWORD>(len), &written, NULL))
            return -1;
        return static_cast<ssize_t>(written);
    }

    ssize_t recv(uint8_t* data, size_t len)
    {
        if (handle_ == INVALID_HANDLE_VALUE) return 0;
        DWORD read_bytes = 0;
        if (!ReadFile(handle_, data, static_cast<DWORD>(len), &read_bytes, NULL))
            return 0;
        return static_cast<ssize_t>(read_bytes);
    }

    void recv(uint8_t* data, uint8_t head, ssize_t len)
    {
        ssize_t recv_len = this->recv(recv_buf.data(), len);
        for (ssize_t i = 0; i < recv_len; i++)
            recv_queue.push(recv_buf[i]);

        while (recv_queue.size() >= static_cast<size_t>(len))
        {
            if (recv_queue.front() != head) { recv_queue.pop(); continue; }
            break;
        }

        if (recv_queue.size() < static_cast<size_t>(len)) return;

        for (ssize_t i = 0; i < len; i++)
        {
            data[i] = recv_queue.front();
            recv_queue.pop();
        }
    }

    void set_timeout(int timeout_ms)
    {
        timeout_ms_ = timeout_ms;
        if (handle_ == INVALID_HANDLE_VALUE) return;

        COMMTIMEOUTS to = { 0 };
        to.ReadIntervalTimeout         = MAXDWORD;
        to.ReadTotalTimeoutMultiplier  = 0;
        to.ReadTotalTimeoutConstant    = static_cast<DWORD>(timeout_ms);
        to.WriteTotalTimeoutMultiplier = 0;
        to.WriteTotalTimeoutConstant   = 50;
        SetCommTimeouts(handle_, &to);
    }

private:
    void Init(std::string port, uint32_t baudrate)
    {
        std::string full = port;
        if (full.rfind("\\\\.\\", 0) != 0 && full.rfind("\\\\?\\", 0) != 0)
        {
            if (full.size() >= 3 && (full[0] == 'C' || full[0] == 'c'))
                full = std::string("\\\\.\\") + full;
        }

        handle_ = CreateFileA(full.c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              0, NULL, OPEN_EXISTING, 0, NULL);

        if (handle_ == INVALID_HANDLE_VALUE)
        {
            printf("Open serial port %s failed (err=%lu)\n",
                   port.c_str(), static_cast<unsigned long>(GetLastError()));
            return;
        }

        DCB dcb = { 0 };
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(handle_, &dcb))
        {
            printf("GetCommState failed\n");
            CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; return;
        }

        dcb.BaudRate        = baudrate;
        dcb.ByteSize        = 8;
        dcb.Parity          = NOPARITY;
        dcb.StopBits        = ONESTOPBIT;
        dcb.fBinary         = TRUE;
        dcb.fParity         = FALSE;
        dcb.fOutxCtsFlow    = FALSE;
        dcb.fOutxDsrFlow    = FALSE;
        dcb.fDtrControl     = DTR_CONTROL_DISABLE;
        dcb.fRtsControl     = RTS_CONTROL_DISABLE;
        dcb.fOutX = FALSE; dcb.fInX = FALSE;

        if (!SetCommState(handle_, &dcb))
        {
            printf("SetCommState failed (err=%lu)\n",
                   static_cast<unsigned long>(GetLastError()));
            CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; return;
        }

        PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);

        COMMTIMEOUTS to = { 0 };
        to.ReadIntervalTimeout         = MAXDWORD;
        to.ReadTotalTimeoutMultiplier  = 0;
        to.ReadTotalTimeoutConstant    = static_cast<DWORD>(timeout_ms_);
        to.WriteTotalTimeoutMultiplier = 0;
        to.WriteTotalTimeoutConstant   = 50;
        SetCommTimeouts(handle_, &to);
    }

    HANDLE               handle_;
    int                  timeout_ms_;
    std::queue<uint8_t>          recv_queue;
    std::array<uint8_t, 1024>    recv_buf;
};

// ==========================================================================
#else  // Linux
// ==========================================================================

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

class SerialPort
{
public:
    using SharedPtr = std::shared_ptr<SerialPort>;

    // baudrate: B921600, B460800, B115200 … (speed_t from <termios.h>)
    SerialPort(std::string port, speed_t baudrate, int timeout_ms = 2)
        : fd_(-1), timeout_ms_(timeout_ms)
    {
        Init(port, baudrate);
    }

    ~SerialPort()
    {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

    ssize_t send(const uint8_t* data, size_t len)
    {
        if (fd_ < 0) return -1;
        return ::write(fd_, data, len);
    }

    // Returns bytes actually read (0 on timeout or error).
    // Uses poll() for millisecond-accurate timeout, matching Windows behaviour.
    ssize_t recv(uint8_t* data, size_t len)
    {
        if (fd_ < 0) return 0;
        struct pollfd pfd{ fd_, POLLIN, 0 };
        if (::poll(&pfd, 1, timeout_ms_) <= 0) return 0;
        ssize_t n = ::read(fd_, data, len);
        return (n < 0) ? 0 : n;
    }

    // Frame-header search overload — mirrors Windows behaviour exactly.
    void recv(uint8_t* data, uint8_t head, ssize_t len)
    {
        ssize_t recv_len = this->recv(recv_buf.data(), static_cast<size_t>(len));
        for (ssize_t i = 0; i < recv_len; i++)
            recv_queue.push(recv_buf[i]);

        while (recv_queue.size() >= static_cast<size_t>(len))
        {
            if (recv_queue.front() != head) { recv_queue.pop(); continue; }
            break;
        }

        if (recv_queue.size() < static_cast<size_t>(len)) return;

        for (ssize_t i = 0; i < len; i++)
        {
            data[i] = recv_queue.front();
            recv_queue.pop();
        }
    }

    void set_timeout(int timeout_ms)
    {
        timeout_ms_ = timeout_ms;
        // Timeout is implemented via poll(); no termios change needed.
    }

private:
    void Init(const std::string& port, speed_t baudrate)
    {
        // O_NOCTTY : don't make this the controlling terminal
        // O_NDELAY : skip carrier-detect on open; cleared right after
        fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd_ < 0)
        {
            printf("Open serial port %s failed (errno=%d)\n", port.c_str(), errno);
            return;
        }

        // Switch back to blocking (O_NDELAY was only for open())
        ::fcntl(fd_, F_SETFL, 0);

        struct termios tty{};
        if (::tcgetattr(fd_, &tty) != 0)
        {
            printf("tcgetattr failed (errno=%d)\n", errno);
            ::close(fd_); fd_ = -1; return;
        }

        ::cfsetispeed(&tty, baudrate);
        ::cfsetospeed(&tty, baudrate);

        // 8N1, no flow control
        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
        tty.c_cflag |=  (CLOCAL | CREAD);
        tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);

        // Raw input: no XON/XOFF, no CR translation, no parity strip
        tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
                         INLCR  | IGNCR  | ICRNL  |
                         IXON   | IXOFF  | IXANY);

        tty.c_oflag &= ~OPOST;   // raw output

        // No canonical mode, no echo, no signals
        tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

        // Non-blocking at termios level; poll() provides the actual timeout
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 0;

        if (::tcsetattr(fd_, TCSANOW, &tty) != 0)
        {
            printf("tcsetattr failed (errno=%d)\n", errno);
            ::close(fd_); fd_ = -1; return;
        }

        ::tcflush(fd_, TCIOFLUSH);
    }

    int                          fd_;
    int                          timeout_ms_;
    std::queue<uint8_t>          recv_queue;
    std::array<uint8_t, 1024>    recv_buf;
};

#endif  // _WIN32
#endif  // SERIAL_PORT_H
