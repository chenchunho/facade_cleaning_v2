# JC-100-RS485 Pressure Sensor Modbus Summary

Source: `doc/真空氣壓表/JC-100-RS485产品说明书.pdf`

## Overview

JC-100-RS485 digital pressure/vacuum sensor with RS485 Modbus RTU interface.
- Low pressure model (JC-101): -100 ~ +100 kPa
- High pressure model (JC-102): -0.1 ~ +1.0 MPa
- Pressure value unit: 0.1 kPa (int16 signed)

## Communication Settings

- RS485 Modbus RTU, CRC-16 (0xA001)
- Default address: 0x12 (18 decimal), configurable 1-247
- Default baud: 19200, options: 9600/19200/38400/115200
- Function codes: 0x03 (read), 0x06 (write single)

## Register Map

| Address | Name | R/W | Description |
|---|---|---|---|
| 0x0001 | Pressure | R | Current pressure value (int16, unit: 0.1 kPa) |
| 0x0010 | Setpoint | R/W | OUT1 target value |
| 0x0011 | Upper Limit | R/W | OUT1 upper limit |
| 0x0012 | Lower Limit | R/W | OUT1 lower limit |
| 0x0013 | Output Mode | R/W | 0=EASY, 1=HYS, 2=WCMP |
| 0x0014 | Display Color | R/W | 0=R_ON, 1=G_ON, 2=RED, 3=GREEN |
| 0x0015 | Pressure Unit | R/W | 0=MPa, 1=kPa, 2=kgf/cm², 3=bar, 4=psi, 5=mmHg |
| 0x0016 | NO/NC | R/W | 0=NO (normally open), 1=NC (normally closed) |
| 0x0017 | Response Time | R/W | 0~A → 2.5ms, 5ms, 10ms, 25ms, 50ms, 100ms, 250ms, 500ms, 1000ms, 2500ms, 5000ms |
| 0x0018 | Hysteresis | R/W | 1~8 levels |
| 0x0019 | ECO Mode | R/W | 0=OFF, 1=Std, 2=FULL |
| 0x001A | Switch Status | R | 0=OFF, 1=ON |
| 0x0020 | Zero Cal | W | Write 0x0001 to trigger zero calibration |

## Output Modes

- **EASY**: Single setpoint with hysteresis
- **HYS**: Setpoint + upper/lower hysteresis bands
- **WCMP**: Window comparator (between upper and lower limits)

## Wiring (5-pin connector)

| Pin | Color | Function |
|---|---|---|
| 1 | Brown | +V (12-24VDC) |
| 2 | Black | Compare output (NPN/PNP) |
| 3 | White | RS485 A |
| 4 | Orange | RS485 B |
| 5 | Blue | 0V (GND) |
