# DM2J-RS570 Modbus RTU Protocol Summary

Source: `doc/DM2J-RS.V1.pdf` V1.0

## Overview

DM2J-RS570 is an RS485 stepper motor driver supporting Modbus RTU protocol with 16 PR (Position Register) blocks and digital I/O.

## Communication Settings

- RS485 Modbus RTU, CRC-16
- Baud rate: 9600/19200/38400/115200 (set via DIP switch SW6-SW7)
- Slave ID: 1-31 (set via DIP switch SW1-SW5, or Pr5.23)
- Function codes: 0x03 (read N regs), 0x06 (write single), 0x10 (write multiple)

## Key Registers

### Status & Control

| Register | Name | R/W | Description |
|---|---|---|---|
| 0x1003 | Status | R | Bit0=FAULT, Bit1=ENABLE, Bit2=RUN, Bit4=CMD_DONE, Bit5=PATH_DONE, Bit6=HOME_DONE |
| 0x1801 | Command | W | 0x1111=enable, 0x2233=disable, 0x2211=enable+EEPROM, 0x2244=disable+EEPROM, 0x4001=JOG+, 0x4002=JOG- |
| 0x6002 | Trigger | W | 0x001P=trigger PR P, 0x010P=write+trigger PR P, 0x0020=home start, 0x0021=set zero, 0x0040=stop |
| 0x2203 | Error code | R | 0x01-0x06=fault codes, 0x09=stall |

### PR Block (Position Register)

Each PR block = 8 registers, base = 0x6200 + (PR_num × 8)

| Offset | Name | Description |
|---|---|---|
| +0 | PRx.00 Mode | Bit0-3: 0=relative, 1=absolute, 2=speed, 3=speed; Bit4=INS; Bit5=OVLP |
| +1 | PRx.01 Position H | Upper 16 bits of position (pulse) |
| +2 | PRx.02 Position L | Lower 16 bits of position (pulse) |
| +3 | PRx.03 Speed | RPM |
| +4 | PRx.04 Acc | ms/1000rpm |
| +5 | PRx.05 Dec | ms/1000rpm |

16 PR blocks: PR0 (0x6200) through PR15 (0x6278).

### Homing

| Register | Name | Description |
|---|---|---|
| 0x600A | Pr8.10 Home mode | Bit0=direction, Bit1=sensor type, Bit2=speed source |
| 0x600F | Pr8.15 High speed | RPM (fast approach) |
| 0x6010 | Pr8.16 Low speed | RPM (slow approach) |
| 0x6011 | Pr8.17 Acc time | ms/1000rpm |
| 0x6012 | Pr8.18 Dec time | ms/1000rpm |
| 0x6015 | Pr8.19 Overrun | 0.1 revolutions |

Trigger: write 0x0020 to 0x6002. Set current as zero: write 0x0021 to 0x6002.

### JOG (RS485)

| Register | Name | Description |
|---|---|---|
| 0x01E1 | Pr6.00 JOG speed | RPM |
| 0x01E7 | Pr6.03 JOG acc/dec | ms/1000rpm (shared for both acc and dec) |

Forward: write 0x4001 to 0x1801. Reverse: write 0x4002 to 0x1801. Stop: write 0x0040 to 0x6002.
JOG commands auto-repeat every 50ms until stop.

### Position & PPR

| Register | Name | Description |
|---|---|---|
| 0x0001 | Pr0.00 PPR | Pulses per revolution (default 10000) |
| 0x602C-602D | Pr8.44-8.45 | Current motor position (int32, high/low) |

### Version

| Register | Name | Description |
|---|---|---|
| 0x8000 | Version | Read 2 regs → ver1.ver2 |

## Trigger Examples (from PDF)

PR0 absolute move to 200000 pulses at 600rpm:
```
01 06 62 00 00 01  → PR0 mode = absolute
01 06 62 01 00 03  → position high
01 06 62 02 0D 40  → position low (0x00030D40 = 200000)
01 06 62 03 02 58  → speed = 600 rpm
01 06 62 04 00 32  → acc = 50
01 06 62 05 00 32  → dec = 50
01 06 60 02 00 10  → trigger PR0
```

Stop:
```
01 06 60 02 00 40  → stop
```

## CRC-16

Standard Modbus CRC16, polynomial 0xA001, init 0xFFFF, appended LSB first.
