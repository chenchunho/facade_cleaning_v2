# SD76-C Length Meter Modbus Summary

Source: `doc/計米器/SD76-C/1.SD76-C仪表 MODBUS通信协议.docx`, `2.SD76-C系列仪表寄存器功能说明.docx`

## Overview

SD76-C series length/counter meter with RS485 Modbus RTU interface.
- Work modes: Meter, Timer, Timer+Counter, Total Meter, Batch Meter
- Upper + lower dual display
- Up to 32 devices on same RS485 bus

## Communication Settings

- RS485 Modbus RTU, CRC-16 (0xA001, init 0xFFFF, LSB first)
- Default address: 0x01, configurable 1-32
- Default baud: 9600, option: 4800
- Data frame: 8N1 (8 data, no parity, 1 stop)
- Function codes: 0x03 (read), 0x06 (write single), 0x10 (write multiple)

## Register Map

| Hex Addr | Name | R/W | Data Format | Description |
|---|---|---|---|---|
| 0x0000 | Control / Status | R/W | uint16 | Read: alarm status bits; Write: control codes |
| 0x0001-0x0002 | Upper Display Value | R/W | BCD (3 bytes) | 6-digit BCD, low 3 bytes valid |
| 0x0003-0x0004 | Lower Display Value | R/W | BCD (3 bytes) | 6-digit BCD, low 3 bytes valid |
| 0x0006-0x0007 | TIA1 Relay 1 Time | R/W | BCD | Relay 1 closure duration |
| 0x0008-0x0009 | TIA2 Relay 2 Time | R/W | BCD | Relay 2 closure duration |
| 0x000F-0x0010 | AL1 Alarm 1 | R/W | BCD | Must write with AL2 together |
| 0x0011-0x0012 | AL2 Alarm 2 | R/W | BCD | Must write with AL1 together |
| 0x0014-0x0015 | SCAL Counter Multiplier | R/W | BCD | Scaling factor |
| 0x001A-0x001B | PRE1 Upper Initial | R/W | BCD | Preset for upper display |
| 0x001C-0x001D | PRE2 Lower Initial | R/W | BCD | Preset for lower display |
| 0x0020 | Decimal Point | R/W | uint8+uint8 | Low byte=upper DP, High byte=lower DP |
| 0x0021-0x0022 | Upper Integer Value | R | int32 signed | Upper display as integer |
| 0x0023-0x0024 | Lower Integer Value | R | int32 signed | Lower display as integer |
| 0x0025-0x0026 | Upper Float Value | R | IEEE 754 float | Upper display as float |
| 0x0027-0x0028 | Lower Float Value | R | IEEE 754 float | Lower display as float |

## Control Codes (Write to 0x0000 via FC 0x06)

| Value | Function |
|---|---|
| 0x0001 | Reset lower display |
| 0x0002 | Reset upper display |
| 0x0003 | Reset both displays |
| 0x0004 | Pause meter |
| 0x0008 | Resume meter |

## Alarm Status (Read 0x0000 low byte)

| Bit | Meaning |
|---|---|
| 0x01 | AL1 alarm triggered |
| 0x02 | AL2 alarm triggered |
| 0x04 | AL1 relay closed |
| 0x08 | AL2 relay closed |
| 0x10 | Meter paused |

## Data Encoding

- **BCD**: 3 valid bytes (6 digits), highest byte discarded. Each nibble = 1 decimal digit.
- **Sign**: Documentation says BCD values are unsigned. Code extracts sign from bit7 of byte[0] — this may work by vendor convention but is not documented.
- **Integer (0x0021-0x0024)**: Standard 32-bit signed two's complement.
- **Float (0x0025-0x0028)**: IEEE 754 single precision.

## Code Implementation Notes

- Driver reads BCD registers 0x0001-0x0002 (upper) and 0x0001-0x0004 (upper+lower)
- `resetAll()` writes 0x0003 to register 0x0000 = reset both displays (correct)
- `decodeSignedBCD6()` extracts sign from bit7 — undocumented but may work in practice
