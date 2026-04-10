# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a C++ robotic control system for a wash robot arm and crane lift, targeting Raspberry Pi (ARM) via cross-compilation from Windows using Visual Studio's "Visual C++ for Linux Development" workload.

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
```

## Architecture

### Communication

All hardware uses **Modbus-TCP** over TCP/IP (port 4001). The `TCP_client` class provides a cross-platform socket wrapper (WinSock2 on Windows, BSD sockets on Linux) with auto-reconnect via a monitor thread.

Every device driver sends/receives raw Modbus frames: function codes 0x01 (read coils), 0x03 (read registers), 0x05/0x06 (write single), 0x10 (write multiple). CRC16-CCITT is used for validation.

### Device Drivers (`user_lib/`)

| Class | Device | Interface |
|---|---|---|
| `TCP_client` | TCP socket abstraction | - |
| `Serial_port` | Serial port (Windows/Linux) | - |
| `DM2J_RS570` | Stepper motor controller | Modbus-TCP |
| `ZDT_motor_control` | Brushless servo motor | Modbus-TCP |
| `JC_100_METER` | Pressure/vacuum sensor | Modbus-TCP |
| `DY_500_weight_sensor` | Load cell / weight sensor | Modbus-TCP |
| `SD76_length_meters` | Length/dimension sensor | Modbus-TCP |
| `PQW_IO_16O_RLY` | 16-channel relay module | Modbus-TCP |
| `ZS_DIO_R_RLY` | Digital I/O relay module | Modbus-TCP |
| `WT901BC_TTL` | 9-axis IMU (accel/gyro/angle) | TTL Serial |

### Wash Robot Network Layout

```
PC/PI ──TCP─┬─→ 485 Controller @ 192.168.1.20:4001
            │   ├─ DM2J_RS570 Slave 1–4  (4 stepper drivers, linear axes)
            │   └─ PQW_IO_16O_RLY        (16 relays, solenoids)
            └─→ Motor Controller @ 192.168.1.21:4001
                ├─ ZDT_motor Slave 2–8   (7 brushless motors)
                ├─ JC_100_METER Slave 9–15 (7 pressure/vacuum sensors)
                └─ PQW_IO_16O_RLY        (relay bank)
```

### Crane Network Layout

```
PC/PI ──TCP─┬─→ Controller @ 192.168.1.21:4001
            │   ├─ SD76_length_meters Slave 2
            │   └─ ZS_DIO_R_RLY Slave 16
            └─→ Weight Controller @ 192.168.1.22:4001
                └─ DY_500_weight_sensor Slave 3
```

### Concurrency Model

All driver classes use `std::thread`, `std::atomic`, and `std::mutex` for thread safety. The `TCP_client` runs a background monitor thread for auto-reconnect. `WT901BC_TTL` runs a continuous read loop in a background thread. Status variables shared between threads use `std::atomic<>`.

## Testing

There is no automated test framework. Testing is done interactively via `Linux_test/main.cpp`, which provides a menu-driven console interface to exercise each device command (enable, disable, zero, position, speed, home, stop, etc.).

To test a device, deploy `Linux_test` to the target machine and run interactively. The test connection address defaults to `10.0.0.42:4001` in the test harness.

## Key Conventions

- **Language:** C++11/14/17, no external dependencies beyond POSIX/WinSock and the C++ standard library
- **Platform guards:** `#ifdef _WIN32` / `#else` used throughout `TCP_client` and `Serial_port` for platform-specific code
- **Modbus slave IDs:** Each physical device has a fixed slave ID baked into the driver call; check the `main.cpp` of each application for the mapping
- **Units:** Motor positions use pulse counts (`int32_t`) or cm (`double` for DM2J steppers); pressure in 0.1 kPa units; weight as `int32_t` and `float`; temperature in °C (`double`)
- **Code comments:** Often written in Traditional Chinese
