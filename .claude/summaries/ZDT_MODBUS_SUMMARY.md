# ZDT Closed-Loop Stepper Motor MODBUS Protocol Summary

Source: `ZDT閉環步進馬達MODBUS協議.pdf` V1.0.0 (2025.12.4)
Source: `ZDT_X42S第二代閉迴路步進馬達使用手冊V1.0.3_251224.pdf` V1.0.3 (2025.12.24)

## Firmware Types

| Firmware | Description |
|---|---|
| X (X42_V1.3) | Legacy, position in 0.1° units, function code 0x04 for reads |
| Emm (Emm5.0) | Current default for X42S, position in pulse units (×360/65536 → degrees), FOC support |
| Y42 (Y42_V2.0) | Variant with battery monitoring, supports both X and Emm |

**This project uses Emm firmware.** Commands marked with (Emm) apply; (X) variants differ in register addresses and data formats.

## Communication Settings

- Default baud: 115200, 8N1
- Checksum modes: 0x6B (default), XOR, CRC-8, **Modbus-RTU (CRC16)** ← used in this project
- Modbus function codes: 0x03/0x04 (read), 0x06 (write single reg), 0x10 (write multiple regs)
- Slave address: 1-255, 0 = broadcast

## 3.1 Trigger Commands (Function 0x06, Write Single Register)

| Section | Command | Register | Data | Description |
|---|---|---|---|---|
| 3.1.1 | Calibrate encoder | 0x0006 | 0x0001 | Takes 0.75~1.5s, motor rotates slightly |
| 3.1.2 | Reset motor (X42S/Y42) | 0x0008 | 0x0001 | Software restart |
| 3.1.3 | Set zero position | 0x000A | 0x0001 | Current angle → 0°, not stored by default |
| 3.1.4 | Release stall/OTP/OCP | 0x000E | 0x0001 | Clear protection flags |
| 3.1.5 | Factory reset | 0x000F | 0x0001 | Restore all defaults |

Response: echo of sent command.

## 3.2 Motion Control Commands

### 3.2.1 Driver Enable (Emm, Function 0x10)

```
TX: Addr 10 00 F3 00 02 04 AB [enable] 00 00 CRC16
RX: Addr 10 00 F3 00 02 CRC16
```

- enable: 0x00=disable, 0x01=enable
- 0xAB = fixed marker
- Last two bytes: reserved (0x00)

### 3.2.6 Speed Mode Emm (Function 0x10, Reg 0x00F6)

```
TX: Addr 10 00 F6 00 03 06 [dir] [acc] [speed_H] [speed_L] [sync] 00 CRC16
RX: Addr 10 00 F6 00 03 CRC16
```

| Field | Range | Description |
|---|---|---|
| dir | 0x00/0x01 | CW / CCW |
| acc | 0x00-0xFF | Acceleration gear (0-255). Time per 1RPM step = (256-acc)×50μs |
| speed | 0x0000-0x0BB8 | 0-3000 RPM |
| sync | 0x00/0x01 | 0=execute now, 1=buffer for sync trigger |

### 3.2.11 Position Mode Emm (Function 0x10, Reg 0x00FD)

```
TX: Addr 10 00 FD 00 05 0A [dir] [acc] [speed_H] [speed_L] [pulse_3] [pulse_2] [pulse_1] [pulse_0] [mode] [sync] CRC16
RX: Addr 10 00 FD 00 05 CRC16
```

| Field | Range | Description |
|---|---|---|
| dir | 0x00/0x01 | CW / CCW |
| acc | 0x00-0xFF | Acceleration gear (same formula as speed mode) |
| speed | 0x0000-0x0BB8 | 0-3000 RPM |
| pulse | 0x00000000-0xFFFFFFFF | Pulse count (microstep × step_angle/360 per revolution) |
| mode | 0x00/0x01 | 0=relative, 1=absolute |
| sync | 0x00/0x01 | 0=execute now, 1=buffer for sync trigger |

**Pulse conversion (Emm):** With 1.8° step and 16 microsteps: 3200 pulses = 360°.
Example: 32000 pulses = 10 revolutions.

### 3.2.12 Emergency Stop (Function 0x06, Reg 0x00FE)

```
TX: Addr 06 00 FE 98 [sync] CRC16
RX: Addr 06 00 FE 98 [sync] CRC16
```

- sync: 0x00=stop immediately, 0x01=buffer for sync trigger
- 0x98 = fixed marker

### 3.2.13 Trigger Sync Move (Function 0x06, Reg 0x00FF)

```
TX: 00 06 00 FF 66 00 CRC16    (broadcast address 0x00)
RX: (no response for broadcast)
```

- 0x66 = fixed marker
- Used after buffering commands with sync=0x01

## 3.3 Homing Commands

### 3.3.1 Set Home Zero Position (Function 0x06, Reg 0x0093)

```
TX: Addr 06 00 93 88 [store] CRC16
```

- 0x88 = fixed marker
- store: 0x00=temporary, 0x01=save to flash

### 3.3.2 Trigger Home (Function 0x06, Reg 0x009A)

```
TX: Addr 06 00 9A [mode] [sync] CRC16
```

| Mode | Description |
|---|---|
| 0x00 | Single-turn nearest |
| 0x01 | Single-turn directional |
| 0x02 | Sensorless collision |
| 0x03 | Limit switch |
| 0x04 | Absolute zero |
| 0x05 | Power-loss position |

### 3.3.3 Abort Home (Function 0x06, Reg 0x009C)

```
TX: Addr 06 00 9C 48 00 CRC16
```

- 0x48 = fixed marker

### 3.3.4 Home Status Flags (Read Reg 0x003B, Emm uses 0x0052)

Bit layout of status byte:

| Bit | Flag | Description |
|---|---|---|
| 0 | Enc_Rdy | Encoder ready (0=error, 1=normal) |
| 1 | Cal_Rdy | Calibration ready (0=not calibrated, 1=calibrated) |
| 2 | Org_SF | Homing in progress (0=no, 1=yes) |
| 3 | Org_CF | Homing failed (0=no, 1=yes) |
| 4 | Otp_TF | Over-temperature protection (0=no, 1=triggered) |
| 5 | Ocp_TF | Over-current protection (0=no, 1=triggered) |

Homing result interpretation:
- Success: Org_CF=0, Org_SF=0 (after was 1)
- In progress: Org_CF=0, Org_SF=1
- Failed: Org_CF=1, Org_SF=0

## 3.4 Status Reading

### 3.4.14 Motor Status Flags (Read Reg 0x003A, Emm uses 0x0050)

| Bit | Flag | Description |
|---|---|---|
| 0 | Ens_TF | Enabled (0=disabled, 1=enabled) |
| 1 | Prf_TF | Position reached (0=not reached, 1=reached) |
| 2 | Cgi_TF | Stall flag (conditions 1,2 met) |
| 3 | Cgp_TF | Stall protection (conditions 1,2,3 met) |
| 4 | Esi_LF | Left limit switch (0=low, 1=high) |
| 5 | Esi_RF | Right limit switch (0=low, 1=high) |
| 7 | Oac_TF | Power loss flag (default=1, set to 1 if power loss occurred) |

Stall detection conditions:
1. Real speed < Clog_Rpm threshold
2. Phase current > Clog_Ma threshold
3. Duration > Clog_Ms threshold

## 3.7.2 Batch Read System Status Emm (Function 0x04, Reg 0x0043, Qty 0x0010)

```
TX: Addr 04 00 43 00 10 CRC16
RX: Addr 04 20 [32 bytes data] CRC16    (total 37 bytes)
```

### Response Data Layout (16 registers = 32 bytes)

| Reg | Bytes | Field | Unit / Conversion |
|---|---|---|---|
| 1 | 2 | [total_bytes=0x1F, param_count=0x09] | Skip |
| 2 | 2 | Bus voltage | mV |
| 3 | 2 | Phase current | mA |
| 4 | 2 | Encoder linear value | 0-65535 → 0-360° |
| 5 | 2 | Target position sign | 0x00=positive, 0x01=negative |
| 6-7 | 4 | Target position value (u32) | degrees = value × 360 / 65536 |
| 8 | 2 | Speed sign | 0x00=positive, 0x01=negative |
| 9 | 2 | Speed value (u16) | RPM (direct) |
| 10 | 2 | Real position sign | 0x00=positive, 0x01=negative |
| 11-12 | 4 | Real position value (u32) | degrees = value × 360 / 65536 |
| 13 | 2 | Position error sign | 0x00=positive, 0x01=negative |
| 14-15 | 4 | Position error value (u32) | degrees = value × 360 / 65536 |
| 16 | 2 | [home_flags(H), motor_flags(L)] | See 3.3.4 & 3.4.14 |

**Fields NOT included in Emm batch read** (vs individual reads):
- Bus current (always 0)
- Encoder raw value (always 0)
- Temperature (always 0)

### Degree conversion formula

```
degrees = (value * 360.0) / 65536.0
```

Example: value=131072 → 131072 × 360 / 65536 = 720° (2 full rotations)

## 3.5 Configuration Commands (selected)

### 3.5.1 Set Slave ID (Function 0x10, Reg 0x00AE)

```
TX: Addr 10 00 AE 00 02 04 4B [store] [new_ID] CRC16
```

- store: 0x00/0x01
- new_ID: 0x01-0xFF (0x00 = broadcast, not allowed as ID)

### 3.5.2 Set Microstep (Function 0x10, Reg 0x00B4)

```
TX: Addr 10 00 B4 00 02 04 8A [store] [microstep] CRC16
```

- microstep: subdivision value (e.g., 16 for 1.8° motor → 3200 pulses/rev)

### 3.5.3 Power-Loss Flag (Function 0x10, Reg 0x0050/0x00DF)

Modify power-loss record flag setting.
- Reg 0x0050 (Emm) / 0x00DF (X firmware)

### 3.5.11 Speed 10x Resolution Emm (Function 0x10, Reg 0x004F)

When enabled, speed reporting switches from 1 RPM to 0.1 RPM resolution.

## 3.3.6 Modify Homing Parameters

Adjustable homing parameters (via dedicated registers):
- Homing speed
- Homing timeout
- Collision detection: RPM threshold, current threshold, duration threshold

## Acceleration Formula (Emm)

```
Time per 1 RPM increment = (256 - acc) × 50 μs
```

- acc=0: **direct start** (no acceleration curve, immediate speed)
- acc=1: slowest acceleration, (256-1)×50μs = 12.75ms per RPM step
- acc=100: (256-100)×50μs = 7.8ms per RPM step
- acc=255: fastest acceleration, (256-255)×50μs = 50μs per RPM step

## CRC16 (Modbus-RTU)

Standard Modbus CRC16 with polynomial 0xA001:

```cpp
uint16_t modbusCRC(const uint8_t* data, int len) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}
```

CRC is appended LSB first: `[CRC_L] [CRC_H]`
