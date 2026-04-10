# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

C++ 機器人控制系統，包含洗窗機器手臂（wash robot arm）與吊車升降系統（crane lift），目標平台為 Raspberry Pi（ARM/ARM64），透過 Visual Studio 的 "Visual C++ for Linux Development" 從 Windows 交叉編譯部署。

## Build System

**IDE:** Visual Studio (solution: `washrobot_new_PI.sln`)  
**Build method:** MSBuild with remote SSH deployment to Linux/ARM targets  
**No CMake or Makefile** — all build configuration is in `.vcxproj` files  

Target platforms: ARM, ARM64, x86, x64 — all with Debug/Release configurations.  
Compiled binaries land in `bin/[arch]/[config]/` within each project directory.

To build from command line (if using MSBuild):
```
msbuild washrobot_new_PI.sln /p:Configuration=Debug /p:Platform=ARM
```

## Repository Structure

```
washrobot_new_PI/    # Main wash robot application (7 motors, 7 pressure sensors)
Crane_control_PI/    # Crane lift control (weight sensor, dimension measurement)
Linux_test/          # Interactive menu-driven test harness for all devices
windows_test/        # Minimal Windows build validation
user_lib/            # Shared device driver library (all hardware abstractions)
x64/                 # Output directory for built binaries
jc100_page*.png      # JC-100 pressure sensor datasheet images
```

## Architecture

### Communication

All hardware uses **Modbus-TCP** over TCP/IP (port 4001). The `TCP_client` class provides a cross-platform socket wrapper (WinSock2 on Windows, BSD sockets on Linux) with auto-reconnect via a monitor thread.

Every device driver sends/receives raw Modbus frames: function codes 0x01 (read coils), 0x03 (read registers), 0x05/0x06 (write single), 0x10 (write multiple). CRC16-CCITT (polynomial 0xA001) is used for validation.

Socket timeouts: 100-500ms per device. TCP monitor thread: 500ms reconnect polling interval.

### Device Drivers (`user_lib/`)

| Class | Device | Interface | Description |
|---|---|---|---|
| `TCP_client` | TCP socket abstraction | WinSock2/BSD | Cross-platform TCP with auto-reconnect & monitor thread |
| `Serial_port` | Serial port (Windows/Linux) | Native | TTL serial communication (8N1, multiple baud rates) |
| `DM2J_RS570` | Stepper motor controller | Modbus-TCP | Linear motion (cm-precision), PR/JOG/Home modes |
| `ZDT_motor_control` | Brushless servo motor | Modbus-TCP | Joint articulation, encoder feedback, stall protection |
| `JC_100_METER` | Pressure/vacuum sensor | Modbus-TCP | Read pressure (0.1 kPa), setpoint control, hysteresis |
| `DY_500_weight_sensor` | Load cell / weight sensor | Modbus-TCP | Read weight (int32/float), decimal point config, auto-zero |
| `SD76_length_meters` | Length/dimension sensor | Modbus-TCP | Read length (upper/lower displays), BCD parsing |
| `PQW_IO_16O_RLY` | 16-channel relay module | Modbus-TCP | ON/OFF with echo verification & retry (up to 5 times) |
| `ZS_DIO_R_RLY` | Digital I/O relay module | Modbus-TCP | Pre-generated command list, crane UP/DOWN |
| `WT901BC_TTL` | 9-axis IMU (accel/gyro/angle) | TTL Serial 115200 | Background thread continuous read, checksum validation |
| `DIHOOL_control` | Motor position controller | Modbus-TCP | Pre-set 4 positions (compiled but not actively used) |
| `QX_DO24` | Digital output module | - | Compiled but not integrated |
| `TCP_server` | TCP listener | - | Built but unused |

### Driver Initialization Pattern

All device drivers support two initialization modes:
```cpp
// Mode A: create internal TCP connection
bool init(const std::string& ip, int port, int ID, bool debug = false);

// Mode B: share external TCP connection (recommended for multiple devices on same controller)
bool init(TCP_client& extClient, int ID, bool debug = false);
```

### Wash Robot Network Layout

```
PC/PI ──TCP─┬─→ 485 Controller @ 192.168.1.20:4001
            │   ├─ DM2J_RS570 Slave 1–4  (4 stepper drivers, linear axes)
            │   └─ PQW_IO_16O_RLY        (16 relays, solenoids)
            └─→ Motor Controller @ 192.168.1.21:4001
                ├─ ZDT_motor Slave 2–8   (7 brushless motors, m1-m7)
                ├─ JC_100_METER Slave 9–15 (7 pressure/vacuum sensors)
                └─ PQW_IO_16O_RLY        (relay bank)
```

**Vacuum Control Pin Mapping:**
- Pin 11 = vacuum motor (main pump)
- Pin 12 = vacuum valve left
- Pin 13 = vacuum valve center
- Pin 14 = vacuum valve right

### Crane Network Layout

```
PC/PI ──TCP─┬─→ Controller @ 192.168.1.21:4001
            │   ├─ SD76_length_meters Slave 2
            │   └─ ZS_DIO_R_RLY Slave 16
            └─→ Weight Controller @ 192.168.1.22:4001
                └─ DY_500_weight_sensor Slave 3
```

**Crane Relay Pin Mapping:**
- Pin 7 = UP (crane rise)
- Pin 8 = DOWN (crane lower)

**Crane Auto Mode Algorithm:**
```
delta = current_weight - base_weight
if delta > 6.0 kg → relay UP on
if delta < -6.0 kg → relay DOWN on
else → both OFF
```

### Concurrency Model

- `TCP_client`: background `reconnectLoop()` monitor thread (500ms polling), `std::mutex socket_mtx` for thread-safe socket access
- `WT901BC_TTL`: dedicated `_worker_thread` for continuous serial read, `std::atomic<bool> read_error` flag
- `Crane_control_PI`: `control_loop()` thread samples weight every 1ms, averages 10 samples (~10ms), updates `std::atomic g_weight`
- All shared state uses `std::atomic<>` or mutex protection

### Wash Robot Commands (main.cpp)

| Command | Description |
|---|---|
| `init` | Initialize all connected devices |
| `vacuum enable/disable` | Activate/deactivate vacuum pump |
| `left/right/center vacuum enable/disable` | Zone-specific vacuum control |
| `move1-4 <rpm> <cm>` | Move linear axis by X cm at Y RPM |
| `move_sync` | Synchronized multi-axis movement |
| `shutdown` | Disable all motors and relays |

## Testing

There is no automated test framework. Testing is done interactively via `Linux_test/main.cpp`, which provides a menu-driven console interface to exercise each device command (enable, disable, zero, position, speed, home, stop, etc.).

To test a device, deploy `Linux_test` to the target machine and run interactively. The test connection address defaults to `10.0.0.42:4001` in the test harness.

`windows_test/` provides minimal Windows-only validation (TCP connection, motor position, pressure read loop, relay control).

## Key Conventions

- **Language:** C++11/14/17, no external dependencies beyond POSIX/WinSock and the C++ standard library
- **Platform guards:** `#ifdef _WIN32` / `#else` used throughout `TCP_client` and `Serial_port` for platform-specific code
- **Modbus slave IDs:** Each physical device has a fixed slave ID baked into the driver call; check the `main.cpp` of each application for the mapping
- **Units:** Motor positions use pulse counts (`int32_t`) or cm (`double` for DM2J steppers); pressure in 0.1 kPa units; weight as `int32_t` and `float`; temperature in °C (`double`); angles in degrees (`double`)
- **Code comments:** Often written in Traditional Chinese (校零=calibration, 觸發=trigger, 回零=home)
- **Error handling:** Drivers store last valid reading (e.g., `_last_pressure`, `lastValidWeight`) for graceful degradation on connection loss
- **Debug logging:** Conditional hex dump output via `printHex()` / `log_hex()`, enabled by `debug` flag in init
