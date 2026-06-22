# Shihlin SE3-210 Inverter Modbus Summary

Source: `D:/洗窗戶機器人/電控設備資料/變頻器SE3系列操作手冊 V1.03.pdf` (also `.docx`)

## Overview

Shihlin SE3-210 series variable-frequency drive (VFD) for AC induction motors. Project uses 2 units to control left / right rope winches (replaces ZS_DIO_R_RLY relays since 2026-05-07).

- 3-phase 220V / 380V input variants
- Operation modes: PU (panel) / EXT (external terminals) / CU (network / communication / Modbus) / JOG / 5 hybrid modes
- Modbus RTU over RS485, accessed via USR-TCP232 transparent gateway
- Built-in PLC, PG card support, PID, tension control, line-speed control

## Communication Settings

| Item | Default | Notes |
|---|---|---|
| Protocol | Modbus RTU | 07-00 (P.33): 0=Modbus, 1=士林協議, 2=PLC |
| Station ID | 0 | 07-01 (P.36): 0-254. **Project uses 1.** |
| Baud rate | 9600 | 07-02 (P.32): 0=4800, 1=9600, 2=19200, 3=38400, 4=57600, 5=115200 |
| Data bits | 8 | 07-03 (P.48): 0=8, 1=7 |
| Stop bits | 1 | 07-04 (P.49): 0=1, 1=2 |
| Parity | None | 07-05 (P.50): 0=N, 1=Odd, 2=Even |
| CR/LF | CR only | 07-06 (P.51): 1=CR, 2=CR+LF |
| Format | 1,8,E,1 (RTU) | 07-07 (P.154): 0..5; **3=1,8,N,2 RTU used by project** |
| Function codes | 0x03, 0x06, 0x10, 0x08 | Read, write single, write multiple, diagnostic |

⚠ Project bench note: USR-TCP232 set to 8N1 — SE3 UART tolerates 1 stop bit when configured for 2. Required to share bus with SD76 (locked at 8N1).

## Communication Timeout (Critical for Safety)

| Param | Default | Recommended | Description |
|---|---|---|---|
| **07-08** P.52 | 1 | 2 | 通訊異常容許次數 (consecutive comm errors before action) |
| **07-09** P.53 | 99999 | 2.0 sec | 通訊間隔容許時間 (max gap between Modbus messages; 99999=disabled) |
| **07-10** P.153 | 1 | 0 (with keepalive) | 通訊錯誤處理: **0=報警 OPT + 空轉停車**, **1=不報警繼續運行**. Only 2 values. |

⚠ **07-10 has only 0 or 1** — older Shihlin VFDs (SS2/SF3) have 4 options (ignore/decel/trip/continue); SE3-210 does NOT.

⚠ With 07-10=0, SE3 triggers OPT alarm if comm timeout. **Requires periodic Modbus traffic** (keepalive). Project Crane_control_PI has `se3_keepalive_loop` 500ms always-on readStatusWord.

⚠ Effective timeout = 07-08 × 07-09 (seconds of silence before alarm).

## Modbus Register Map — Control / Monitor

These are special "operating" registers separate from parameter access.

### Operation Mode (H1000)

| Reg | R/W | Description |
|---|---|---|
| H1000 | R/W (H06/H10) | Set operation mode. Values: H0000=通訊CU, H0001=外部, H0002=JOG, H0003-H0007=混合1-5, H0008=PU |

⚠ **CRITICAL**: Even with panel 00-16(P.79)=3 set to 通訊 mode, SE3 firmware still rejects Modbus run-command writes to H1001 unless `H1000 = 0` has been explicitly written from Modbus side. Panel "3" and Modbus "0" both refer to 通訊 mode but use different number codings (manual §7-3 example #1).

⚠ Driver auto-writes H1000=0 via `ensureCuMode_()`. After write, waits ~150ms for SE3 internal state machine to transition; H1001 writes within this window are silently rejected.

### Run Command (H1001 write)

| Bit | Function |
|---|---|
| b0 | Reserved (DO NOT set) |
| b1 | STF — run forward |
| b2 | STR — run reverse |
| b3 | RL — multi-speed low |
| b4 | RM — multi-speed mid |
| b5 | RH — multi-speed high |
| b6 | RT — 2nd parameter set |
| b7 | MRS — output cutoff / emergency stop |

- **Stop = write 0x0000** (clear all bits) → motor decel per P.7
- ⚠ **NOT 0x1101** — different register (see below)

### Status Word (H1001 read)

Read same address as control; FC distinguishes (FC03=read, FC06/10=write).

| Bit | Meaning |
|---|---|
| b0 | 運轉中 (running) |
| b1 | 正轉中 (fwd) |
| b2 | 反轉中 (rev) |
| b3 | 頻率到達 (freq reached) |
| b4 | 過負載 (overload) |
| b5 | 保留 |
| b6 | 頻率檢出 (freq detect) |
| **b7** | **異常發生 (fault)** — set on OPT, OC, OV, etc. |
| b8 | 電壓失速 (undervolt) |
| b9 | 變頻器欠壓 |
| b10 | PLC 運行 |
| b11 | 變頻器 EO 狀態 |
| b14 | 變頻器復位中 (resetting) |
| b15 | tuning 進行中 |

### Frequency Setpoint

| Reg | R/W | Description |
|---|---|---|
| H1002 | R/W (H06/H10) | Frequency setpoint, RAM (no EEPROM wear). Unit: 0.01 Hz (5000 = 50.00 Hz). **Use this for runtime control.** |
| H1009 | R/W (H06/H10) | Frequency setpoint, EEPROM (persisted). **Avoid for runtime.** |

### Monitor Reads (FC 0x03)

| Reg | Description | Unit |
|---|---|---|
| H1003 | 輸出頻率 (output freq) | 0.01 Hz |
| H1004 | 輸出電流 (output current) | 0.01 A |
| H1005 | 輸出電壓 (output voltage) | 0.01 V |
| H1007 | 異常代碼 1, 2 (latest 2 error codes packed) | b15-b8 / b7-b0 |
| H1008 | 異常代碼 3, 4 (error history) | b15-b8 / b7-b0 |
| H100A | 線速度回饋 (line speed feedback) | (config-dependent) |
| H100B | 線速度目標 (line speed target) | (config-dependent) |
| H100C | 張力給定 (tension setpoint) | 0-30000 |
| H100D | 轉矩給定 (torque setpoint) | 0-FA0 (0-400%), F060-FFFF (-400% to 0) |
| H1012 | 監視外部運轉狀態 (external run status) | b0-b15 |
| H1013 | 特殊監視選擇碼 (special monitor code) | H0000-H0010 |
| H1014 | 數位輸入端子狀態 | bit-per-terminal |
| H1015 | 數位輸出端子狀態 | bit-per-terminal |
| H1016 | 2-5 端子輸入電壓 | |
| H1017 | 4-5 端子輸入電流/電壓 | |
| H1018 | AM-5 端子輸出電壓/電流 | |
| H1019 | 直流 PN 端電壓 | |
| H101A | 變頻器電子積熱率 | |
| H101B | 變頻器輸出功率 | |
| H101C | 變頻器溫升累積率 | |
| H101D | 變頻器 NTC 溫度累積 | |
| H101E | 馬達電子積熱率 | |
| H101F | PID 控制目標壓力 | |

⚠ Earlier driver used H100A/B/C for output monitor — those are actually line-speed / tension regs, NOT motor output monitor. Caused 0 reads to look correct when motor stopped. Fixed 2026-05-07.

## Modbus Magic Commands (H1101-H1106)

For inverter reset / parameter clear functions. Write specific magic values.

| Reg | Magic | Effect |
|---|---|---|
| **H1101** | **H9696** | **變頻器復位 (inverter reset)** — equivalent to 00-02=2 / P.997=1. Clears alarms, resets internal state. ⚠ Driver expects no reply (resetting interrupts response). |
| H1102 | HA5A5 | 參數恢復 (see parameter restore table) |
| H1103 | H9966 | (see parameter restore table) |
| H1104 | H5A5A / H5566 / H5959 | (see parameter restore table) |
| H1105 | H55AA / H99AA / H9A9A | (see parameter restore table) |
| H1106 | H9696 | (see parameter restore table) |

⚠ Project uses `clearAlarm()` driver method = write H1101 = H9696 to clear OPT alarm without power-cycle. Added 2026-05-13.

## Function Code Reference

| FC | Hex | Function | Notes |
|---|---|---|---|
| 03 | H03 | Read holding register | Single or multiple |
| 06 | H06 | Write single register | |
| 08 | H08 | Diagnostic (loopback test) | Subcode H0000 = loopback |
| 10 | H10 | Write multiple registers | |

When using FC H03 or H10 on multiple registers, if ≥1 register is operable the operation succeeds (not all-or-nothing).

## Operation Mode Parameter (00-16 / P.79)

| Value | Mode | Frequency source | Run/Stop source |
|---|---|---|---|
| 0 | PU/外部 混合 | (power-up default to external, switchable) | |
| 1 | PU 永遠 | 操作鍵盤 | 操作鍵盤 |
| 2 | 外部 永遠 | 外部類比 2-5/4-5 or 多段速 | STF/STR 端子 |
| **3** | **通訊 (CU)** | **通訊 (H1002)** | **通訊 (H1001)** ← project value |
| 4 | 混合 1 | 操作鍵盤 | STF/STR 端子 |
| 5 | 混合 2 | 外部 | 操作鍵盤 |
| **6** | **混合 3** | **通訊 (H1002)** | **STF/STR 端子** |
| 7 | 混合 4 | 外部 | 通訊 |
| 8 | 混合 5 | 操作鍵盤 | 通訊 |
| 99999 | 第二操作模式 | | |

⚠ Mode 6 (混合 3) is a candidate split-control architecture: relay drives STF/STR
for hard-synchronized start/stop (electrical sync < 1ms vs Modbus 10-100ms), while
Modbus retains setFreqHz dynamic control for kick/fine_adjust/balance trim. Removes
the H1001 / ensureCuMode / watchdog complexity entirely. Bench-validate before
committing to the architecture change (see §4.2.2 of V1.03 manual for full step-by-step).

⚠ In mode 2/5/7 ("外部模式" or 混合 2/4): multi-speed terminals (RH/RM/RL/REX, M0/M1/M2)
override Modbus frequency when ANY is on. In mode 6 (混合 3) same rule applies: ensure
these terminals are tied low / disconnected to avoid unintended speed selection.

## Command Source Parameter (00-19 / P.35)

In 通訊 mode (00-16=3), `00-19` specifies command source. Typically default 0 = communication.

## Other Critical Panel Parameters

| Param | P. | Description | Project use |
|---|---|---|---|
| **00-02** | P.996/997/998/999 | 異警清除 / 變頻器復位 | 1=clear all alarms; 2=reset inverter |
| **00-16** | P.79 | 操作模式 (operation mode) | Project: 3 (CU) |
| **00-19** | P.35 | 運轉指令來源 | Project: 0 (communication) |
| **01-06** | P.7 | 加速時間 1 | Tune for smooth rope start |
| **01-07** | P.8 | 減速時間 1 | Tune for smooth rope stop |
| **10-00** | P.10 | 直流制動動作頻率 | Default 3.00 Hz. ⚠ freeze_hz must stay above this. |
| **10-01** | P.11 | 直流制動動作時間 | Default 0.5s |
| **10-02** | P.12 | 直流制動動作電壓 | Default 4% (7.5K↓) / 2% (11K↑) |

⚠ Per project memory `project_se3_panel_acc_dec_alignment.md`: P.7/P.8 must match on left + right SE3 for synchronized motion.

⚠ Per project memory `project_se3_dc_brake_setup.md`: 10-00/01/02 must match on both sides for synchronized brake.

## Panel LED Indicators

⚠ **PU and CU mode SHARE the same indicator**. Manual: "PU: PU 和 CU 模式運行時燈亮。H1~H5 運行模式時燈閃爍".

| LED | Meaning |
|---|---|
| PU (steady) | PU mode OR CU mode (cannot distinguish from LED alone) |
| PU (blinking) | Hybrid mode 1-5 OR multi-speed H1-H5 |
| MON / Hz / A / V | Monitor display value (current frequency / current / voltage) |
| RUN (blinking) | Motor running |
| FWD / REV | Motor direction |
| ALR | Alarm |

## Error Code Reference (H1007 / H1008)

| Code | Mnemonic | Description |
|---|---|---|
| 16 | OC1 | 過電流 (accel) |
| 17 | OC2 | 過電流 (constant speed) |
| 18 | OC3 | 過電流 (decel) |
| 19 | OC0 | 過電流 (other) |
| 32 | OV1 | 過電壓 (accel) |
| 33 | OV2 | 過電壓 (constant) |
| 34 | OV3 | 過電壓 (decel) |
| 35 | OV0 | 過電壓 (other) |
| 48 | THT | 變頻器過熱 |
| 49 | THN | 馬達過熱 |
| 50 | NTC | NTC 溫度感測 |
| 64 | EEP | EEPROM 異常 |
| 66 | PID | PID 異常 |
| 82 | IPF | 瞬間停電 |
| 97 | OLS | 過載失速 |
| 98 | OL2 | 馬達過載 |
| 129 | AErr | Auto-tune 失敗 |
| **144** | **OHT** | **散熱片過熱** |
| **160** | **OPT** | **通訊逾時異警** — fires when 07-08 errors exceed in 07-09 window, and 07-10=0 |
| 179 | SCP | 短路保護 |
| 192 | CPU | CPU 錯誤 |
| 193 | CPR | CPU 通訊錯誤 |
| 209 | PG1 | PG 卡異常 1 |
| 210 | PG2 | PG 卡異常 2 |
| 211 | PG3 | PG 卡異常 3 |
| 212 | bEb | 內部煞車異常 |
| 213 | PTC | PTC 異常 |
| 215 | dv3 | (PG 相關) |
| 216 | dv1 | (PG 相關) |
| 217 | dv2 | (PG 相關) |

History: parameters 06-40 ~ 06-48 contain past alarm data (most-recent first).

## Reset / Clear Alarm Procedures

1. **Panel STOP/RESET key** (hold ~1.0s) — clears alarm, motor remains stopped
2. **Power cycle** (turn off + on) — full reset
3. **Modbus H1101 = H9696** — programmatic equivalent to 00-02=2 / P.997=1
4. **Modbus H06 to param addr for 00-02 = 1** — clear alarms only (less drastic)

Driver provides `clearAlarm()` using method 3 (see `SE3_inverter.h`).

## Direction Convention (Project-Specific)

⚠ **Wiring-dependent**. Project bench-verified 2026-05-07 / 08:
- **STF (runForward) = retract** 收繩 (cup goes up, display ↓)
- **STR (runReverse) = pay_out** 放繩 (cup goes down, display ↑)

Counter-intuitive vs the term "forward"; caused by motor wiring / P.17 setup. Re-verify at low Hz before production sequences.

Project macros (`Crane_control_PI/main.cpp`):
```cpp
#define SE3_DIR_PAY_OUT(inv)   inv.runReverse()    // STR
#define SE3_DIR_RETRACT(inv)   inv.runForward()    // STF
```

`PAY_OUT_INCREASES_DISPLAY = true` (meter counts up when paying rope out).

## Code Implementation Notes

- `user_lib/SE3_inverter.{h,cpp}` — driver class
- TCP_client wraps Modbus-TCP via USR-TCP232 transparent gateway (port 4001)
- CRC16 standard Modbus (init 0xFFFF, poly 0xA001, LSB-first byte order)
- `ensureCuMode_()` writes H1000=0 once per power-up + 150ms sleep — required before any H1001 run command
- `run_h1001_with_watchdog_()` — wraps run cmd with retry; after 2 consecutive failures, resets `cu_mode_set_` flag to re-claim CU mode on next call
- `clearAlarm()` — writes H1101=H9696, sleeps 200ms, resets `cu_mode_set_=false` + `comm_fail_count_=0`
- `setFreqHz()` writes H1002 (RAM, no EEPROM wear); range [0, max_hz]
- `readStatusWord()` reads H1001 → 16-bit status word, b7 = fault

## Bench-Verified Caveats (Project-Specific)

1. **CU mode latch race** (2026-05-08): SE3 ACKs H1000=0 quickly but internal state takes 50-150ms. H1001 write in this window silently rejected. → driver sleeps 150ms after H1000 write.

2. **Stale buffer pattern** (2026-05-08): USR transparent gateway sometimes returns "bad reply len=N" (N>>8) due to stale frame in TCP buffer. Driver retries on this pattern.

3. **P.7/P.8 must align L/R** (2026-05-08): Asymmetric accel/decel times cause one rope to lag 0.5-2s on stops. Hardware-side fix only.

4. **DC brake setup for heavy loads** (2026-05-09): 10-00=3Hz, 10-01=0.5-2s, 10-02=10-15% for cranes carrying heavy rope+robot. Default values too weak.

5. **OPT alarm + keepalive requirement** (2026-05-13): With 07-10=0 (safety), need application-side 1Hz keepalive via readStatusWord to prevent false OPT during legitimate silent periods (freeze / fine_adjust / hold idle).

6. **Auto-recovery from OPT via clearAlarm()** (2026-05-13): Keepalive detects b7 fault, calls clearAlarm (H1101=H9696). 5s cooldown prevents spamming. After clear, driver re-claims CU mode on next run cmd.

7. **PU LED dual-meaning** (2026-05-13): Manual confirmed PU LED is lit during BOTH PU and CU mode — cannot distinguish operation mode from LED alone. Must check 00-16 parameter value directly.

## Manual Location

- PDF: `D:/洗窗戶機器人/電控設備資料/變頻器SE3系列操作手冊 V1.03.pdf`
- DOCX: same path with `.docx` extension (cleaner text extraction)
- SL-INV software: `D:/洗窗戶機器人/電控設備資料/se3變頻器通訊軟體V0.8.18/` (PC tool for SE3)
