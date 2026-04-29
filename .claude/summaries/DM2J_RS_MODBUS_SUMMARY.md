# DM2J-RS570 Modbus RTU Protocol Summary

Source: `doc/DM2J-RS.V1.pdf` V1.0
Last verified: 2026-04-24 (vs `D:\洗窗戶機器人\電控設備資料\DM2J-RS\DM2J-RS.V1.pdf`)

> **⚠ 歷史版本（2026-04-24 之前）此文件多處錯誤**：把 status 當 32-bit 橫跨 0x1003+0x1004、Bit16=HOME_DONE、0x1801=0x1111 當 enable — 全部錯。對應的 `user_lib/DM2J_RS570.cpp` 的 `read_status` / `print_status` / `PR_move_cm` 完工檢查也都踩了同一個雷。本版以實機手冊為準。

## Overview

DM2J-RS570 是 RS485 閉環步進驅動器，支援 Modbus RTU、16 個 PR (Position Register) 路徑、digital I/O。

## Communication Settings

- RS485 Modbus RTU, CRC-16
- Baud rate: 9600/19200/38400/115200 (set via DIP switch SW6-SW7)
- Slave ID: 1-31 (set via DIP switch SW1-SW5, or Pr5.23)
- Function codes: 0x03 (read N regs), 0x06 (write single), 0x10 (write multiple)

## Enable 機制（重要，有兩種方式）

### (1) 硬體 enable — 透過 DI1（出廠預設）

- DI1 端子出廠預設功能 = **SRV-ON（使能輸入）**，信號類型 = **常閉 (NC)**
- 意思：DI1 **懸空（斷開）→ 使能** / 接通 GND → 解除使能
- **驅動器上電後立刻進入使能狀態**（軸鎖住）

### (2) 軟體強制 enable — 透過 Pr0.07 (0x000F)

| 值 | 行為 |
|---|---|
| `0x000F = 0` | 使能狀態交給 DI1（預設）|
| `0x000F = 1` | 強制使能，**不管 DI1 狀態** |

Pr0.07 優先順序 **高於** DI1 硬體輸入。

## Key Registers

### Status Register

| Register | Name | R/W | Bits |
|---|---|---|---|
| 0x1003 | 運行狀態 | R | **單一 16-bit register**，bits 0-6 如下：|

| Bit | 含義 | EN 縮寫 |
|---|---|---|
| 0 | 故障 | FAULT |
| 1 | 使能 | ENABLE |
| 2 | 運行 | RUN |
| 3 | 無效 | (reserved) |
| 4 | 指令完成 | CMD_DONE |
| 5 | 路徑完成 | PATH_DONE |
| 6 | 回零完成 | HOME_DONE |

**重要注釋（手冊原文）：**
> 上電默認路徑完成和指令完成，故障和未使能狀態下，路徑和指令顯示未完成。

意即：
- 上電時 `0x1003` 初始值 = `0x0030`（bit 4 + bit 5 預設 set）
- 任何故障或未使能狀態會把 bit 4/5 清掉
- 所以「判斷 PR 動作完成」要同時檢查 bit 1 (ENABLE) + bit 4 (CMD_DONE) + bit 5 (PATH_DONE) 且 bit 0 (FAULT) = 0

### Control Word (0x1801) — 輔助功能

| Value | 真實含義 |
|---|---|
| 0x1111 | **復位當前報警**（清故障，不是 enable！） |
| 0x1122 | 復位歷史報警 |
| 0x2211 | 儲存所有參數到 EEPROM |
| 0x2222 | 參數初始化（不含電機參數） |
| 0x2233 | 所有參數恢復出廠值（不是 disable！） |
| 0x2244 | 映射參數存 EEPROM |
| 0x4001 | JOG 左（50ms 發一次） |
| 0x4002 | JOG 右（50ms 發一次） |

讀取後狀態字會自動回到初態。

### Save Status (0x1901) — 讀寫驗證儲存結果

| Register | Name | R/W |
|---|---|---|
| 0x1901 | 儲存狀態字 | R |

| Value | 含義 |
|---|---|
| 0x5555 | 儲存成功 |
| 0xAAAA | 儲存失敗 |
| 0x1111 | 首次上電從未存過的狀態 |

> 執行 0x2211 存檔指令後，首次讀取 0x1901 會看到 0x5555，之後又變回 0x1111。

### PR Trigger (0x6002)

| Value | 含義 |
|---|---|
| 0x0010 \| P | Trigger PR path P (P=0~15) |
| 0x0100 \| P | Write + trigger PR path P |
| 0x0020 | Home start |
| 0x0021 | Set current position as zero |
| 0x0040 | Stop |

### PR Block (Position Register)

每個 PR block = 8 個 register，base = `0x6200 + (PR_num × 8)`

| Offset | Name | 說明 |
|---|---|---|
| +0 | PRx.00 Mode | Bit0-3: **1=絕對, 2=相對, 3=速度** (⚠ **舊版本檔曾寫 0=relative 1=absolute 是錯的**)；Bit4=INS；Bit5=OVLP；Bit6=連續；Bit8-11=跳轉路徑 0-15；bit14=JUMP |
| +1 | PRx.01 Position H | 位置上 16 位（pulse） |
| +2 | PRx.02 Position L | 位置下 16 位 |
| +3 | PRx.03 Speed | RPM |
| +4 | PRx.04 Acc | ms/1000rpm |
| +5 | PRx.05 Dec | ms/1000rpm |
| +6 | PRx.06 Dwell | 停留時間 (ms) |
| +7 | PRx.07 Special | 路徑連結 / 特殊參數 |

16 個 PR blocks: PR0 (0x6200) 到 PR15 (0x6278)。

> **⚠ Mode 驗證（2026-04-24 實機觀察）**：`mode=0` 送下去會被驅動器當作「路徑未配置」忽略（trigger 被 ACK 但馬達完全不動、ENABLE 維持 0）。`mode=1` 確認會動（對應絕對位置）。**舊版 driver 註解 `0=relative / 1=absolute` 有誤，實際依手冊 bit 0-3 是 `1=absolute / 2=relative`**。

### Homing

| Register | Name | 說明 |
|---|---|---|
| 0x600A | Pr8.10 Home mode | Bit0=direction, Bit1=sensor type, Bit2=speed source |
| 0x600F | Pr8.15 High speed | RPM (fast approach) |
| 0x6010 | Pr8.16 Low speed | RPM (slow approach) |
| 0x6011 | Pr8.17 Acc time | ms/1000rpm |
| 0x6012 | Pr8.18 Dec time | ms/1000rpm |
| 0x6015 | Pr8.19 Overrun | 0.1 revolutions |

Trigger: 寫 0x0020 到 0x6002。設當前為零點: 寫 0x0021 到 0x6002。

### JOG (RS485)

| Register | Name | 說明 |
|---|---|---|
| 0x01E1 | Pr6.00 JOG speed | RPM |
| 0x01E7 | Pr6.03 JOG acc/dec | ms/1000rpm (shared) |

正轉: 0x4001 到 0x1801。反轉: 0x4002 到 0x1801。停止: 0x0040 到 0x6002。
JOG 指令需每 50ms 重發才會持續運動，否則當點動。

### Position & PPR

| Register | Name | 說明 |
|---|---|---|
| 0x0001 | Pr0.00 PPR | Pulses per revolution (default 10000) |
| 0x602C-602D | Pr8.44-8.45 | 馬達當前位置 (int32, high/low) |

### Version

| Register | Name | 說明 |
|---|---|---|
| 0x8000 | Version | Read 2 regs → ver1.ver2 |

## Trigger Examples (from PDF)

PR0 absolute move to 200000 pulses at 600rpm:
```
01 06 62 00 00 01  → PR0 mode = absolute (mode bit 0 = 1)
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

## Known Driver Bugs（user_lib/DM2J_RS570.cpp，2026-04-24 發現，待修）

1. **`read_status()` 多讀 1 個 register** — 手冊只需要讀 0x1003 (1 register / 16-bit)，driver 讀 0x1003+0x1004 組成 32-bit，把真實 bits 放到高 16 位，再用錯誤的 mask (`& 0x0010`、`& 0x0020`) 查低 16 位永遠為 0 → 完工判斷永遠 timeout
2. **`print_status()` HOME_DONE mask 錯** — 用 `status & 0x10000`，應該 `status & 0x0040`（bit 6 of 0x1003）
3. **`PR_move_cm()` / `PR_move_cm_trigger_all()`** — 內部用同樣錯誤 mask 判斷 CMD_DONE/PATH_DONE，造成馬達實際有動但 return true (timeout)
4. **`motor_enable()` / `motor_disable()` / `save_params()` 只有 header 宣告、`.cpp` 沒實作**；且 header 註解給的指令也錯：
   - `motor_enable()` 註解寫 `0x1801 = 0x1111` → 實際 0x1111 是「復位當前報警」不是 enable。正確應寫 `0x000F = 1` (Pr0.07 強制 enable)
   - `motor_disable()` 註解寫 `0x1801 = 0x2233` → 實際 0x2233 是「參數恢復出廠值」。正確應寫 `0x000F = 0`（交回 DI1 控制）
   - `save_params()` 註解寫 `0x1801 = 0x2222` → 實際 0x2222 是「參數初始化」。正確應寫 `0x1801 = 0x2211`（存 EEPROM）
