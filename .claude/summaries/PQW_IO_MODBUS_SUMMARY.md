# PQW_IO_16O_RLY Modbus Summary

Source: `doc/数字量输出系列(RS485-RS232-TTL通信接口)等4个文件/数字量输出系列(RS485-RS232-TTL通信接口)/数字量输出系列使用手册(RS485-RS232-TTL通信接口)-V3.0.pdf`

## Overview

PQW series digital output (relay) module with RS485 Modbus RTU interface.
- Up to 64 output channels
- Relay / NPN / PNP output options
- Modes: Normal, Delay, Jog, Cycle, Fixed-duration

## Communication Settings

- RS485 Modbus RTU, CRC-16 (0xA001, init 0xFFFF, LSB first)
- Default address: 1, configurable 1-255
- Default baud: 9600, options: 4800/9600/14400/19200/38400/56000/57600/115200
- Data frame: 8N1

## Function Codes

| Code | Name | Scope |
|---|---|---|
| 0x01 | Read Coil Status | Bit: 00001-09999 |
| 0x02 | Read Discrete Input | Bit: 10001-19999 |
| 0x03 | Read Holding Registers | Word: 40001-49999 |
| 0x04 | Read Input Registers | Word: 30001-39999 |
| 0x05 | Write Single Coil | Bit: single |
| 0x06 | Write Single Register | Word: single |
| 0x0F | Write Multiple Coils | Bit: multiple |
| 0x10 | Write Multiple Registers | Word: multiple |

## Coil Addresses (0x01 / 0x05)

| Hex Addr | Description |
|---|---|
| 0x0000 | Channel 1 (0=OFF, 1=ON) |
| 0x0001 | Channel 2 |
| ... | ... |
| 0x000F | Channel 16 |
| ... | ... |
| 0x003F | Channel 64 |

## Holding Register Map (0x03 / 0x06 / 0x10)

| Hex Addr | PLC Addr | Description |
|---|---|---|
| 0x0000-0x003F | 40001-40064 | Channel 1-64 mode setting (0=Normal, 1=Delay, 3=Jog, 4=Cycle, 5=Fixed) |
| 0x0040 | 40065 | Parity setting (0=None, 1=Odd, 2=Even) |
| 0x0041 | 40066 | Communication watchdog timer |
| 0x0042 | 40067 | Stop bits (0=1bit, 1=2bits) |
| 0x0043 | 40068 | Station address (1-255) |
| 0x0044 | 40069 | Baud rate (0-7: 4800-115200) |
| 0x0045-0x0084 | 40070-40133 | Channel 1-64 control value |
| **0x0085** | **40134** | **All channels control: write 0=all OFF, >0=all ON** |
| 0x0086 | 40135 | CH1-16 status bitmap (bit0=CH1, bit15=CH16) |
| 0x0087 | 40136 | CH17-32 status bitmap |
| 0x0088 | 40137 | CH33-48 status bitmap |
| 0x0089 | 40138 | CH49-64 status bitmap |
| 0x008A | 40139 | Factory reset (write >=1 to reset) |

## Code Implementation Notes

- Driver uses: 0x05 (single coil), 0x06 (all on/off via 0x0085), 0x01 (read coil status)
- Single relay: FC 0x05, addr = relay_num - 1, ON=0xFF00, OFF=0x0000 (correct)
- All relay: FC 0x06, addr 0x0085, ON=0xFF00, OFF=0x0000 (correct)
- Read status: FC 0x01, start 0x0000, count = relay_count (correct)
- Coil response parsing: byte_index = i/8 + 3, bit = i%8, LSB first (correct)
