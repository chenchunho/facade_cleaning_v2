# SD76-C Length Meter Modbus Summary

Source: `doc/計米器/SD76-C/1.SD76-C仪表 MODBUS通信协议.docx`, `2.SD76-C系列仪表寄存器功能说明.docx`

> 📌 **2026-08-31 擴充**：由使用者提供的原始 `.docx` 重新抽取，補上「面板設定方式 / `UArt` 代碼表 /
> 鮑率只有 9600·4800 / 100Ω 終端電阻」四項——這些在原摘要裡沒有，而**當天的故障正好卡在這裡**。
> ⚠️ 原始檔當時放在 `tmp/SD76-C/`，而 **`tmp/` 已被排除在 gdrive 鏡像之外**（2026-08-31 起），
> 要長期保存需移到會被鏡像的位置。

## Overview

SD76-C series length/counter meter with RS485 Modbus RTU interface.
- Work modes: Meter, Timer, Timer+Counter, Total Meter, Batch Meter
- Upper + lower dual display
- Up to 32 devices on same RS485 bus

## Communication Settings

- RS485 Modbus RTU, CRC-16 (0xA001, init 0xFFFF, LSB first)
- Default address: 0x01, configurable 1-32
- **Baud: 手冊寫「默认 9600，范围 9600-4800」，但實機不是這樣 —— 見下方紅字**
- Data frame: 8N1 (8 data, no parity, 1 stop)
- Function codes: 0x03 (read), 0x06 (write single), 0x10 (write multiple)

### 🔴🔴 手冊的鮑率範圍與實機不符（2026-08-31 實戰更正）

| 來源 | 說法 |
|---|---|
| 手冊原文 | 「`BAUSET` 通讯速率:默认为9600.（**范围 9600-4800**）」 |
| **實機（per user 2026-08-31）** | **本專案這批 SD76-C 實際跑在 115200，可用。** |

🔴 **以實機為準，不要照抄手冊這一行。** `USR_M`（`192.168.1.34`）長期就是設 **115200**，
SD76 與同匯流排的 PQW 都正常運作過。

⚠️ **這條是我（Claude）2026-08-31 踩過的坑，寫下來免得重犯**：當天計米器失聯，
我讀到手冊這句、又看到閘道是 115200，就**斷定「鮑率超規格」是根因**，
實際把閘道改成 9600 —— **改完仍然不通，證明推論是錯的**，隨後改回 115200。
📌 **教訓：手冊的參數範圍是「這型號的通用說明」，不等於「現場這批的實際能力」。
把規格書當成推翻現場既有事實的依據之前，先問「那它之前是怎麼正常運作的？」**
——本例中「同一支 binary 幾小時前還讀得到值」這件事，本身就已經否證了鮑率假說。

**症狀對照（實測，供日後判讀用）**：

| 閘道鮑率 | RX 內容 | 當時的（錯誤）判讀 |
|---|---|---|
| 115200（正確值） | 滿版 `FE`（250 個） | 誤判為「鮑率不符的框架錯誤」 |
| 9600（改錯） | **完全空白** | 誤判為「鮑率對了、線路安靜」 |

→ **兩種現象都不是鮑率造成的**；真正的故障在裝置端（未解，見 `work_log.md` 同日）。

### 面板設定方式（二级参数设定模式，連按 `SET` 循環）

| 選項 | 內容 | 範圍 / 預設 |
|---|---|---|
| `Adrset` | 儀表位址 | 01–32，預設 **01** |
| `BAUSET` | 通訊速率 | 手冊寫 9600 / 4800，預設 9600 —— **但實機可設 115200，見上方紅字** |
| `UArt` | 資料結構（見下表代碼） | 出廠預設 **1** |

`UArt` 代碼對照（資料位一律 8、停止位一律 1，只有校驗位不同）：

| 代碼 | 資料位 | 停止位 | 校驗位 |
|---|---|---|---|
| 0 | 8 | 1 | （手冊此列表格錯位，未採信） |
| **1** | 8 | 1 | **無**（出廠預設 = 8N1） |
| 2 | 8 | 1 | 奇 |
| 3 | 8 | 1 | 偶 |

⚠️ **停止位只有 1**：本錶沒有 8N2 選項。同一條 RS485 上若有裝置需要 2 停止位，
閘道設 2 會讓本錶收不到（RS485 一條匯流排只能一組參數 —— 見 `per_program_cautions.md` §0）。

### 🔴 電氣：終端電阻

手冊明載「**必要时在仪表后部端子并接一只 100Ω 左右的电阻**」。
📌 匯流排浮接（無終端 / 無偏壓）的典型症狀就是**近 `0xFF` 的雜訊**（`0xFE` / `0xFC` 交替），
與鮑率不符的症狀**很像但不同**：鮑率不符通常是**穩定的同一個位元組整片刷**，
浮接則會**跳動**。兩者都要看原始 hex 才分得出來（`USER_LIB_HEX_LOG=1`）。

## Register Map

| Hex Addr | Name | R/W | Data Format | Description |
|---|---|---|---|---|
| 0x0000 | Control / Status | R/W | uint16 | Read: high byte=work mode, low byte=alarm status; Write: control codes |
| 0x0001-0x0002 | Upper Display Value | R/W | BCD (3 bytes) | 6-digit BCD, low 3 bytes valid |
| 0x0003-0x0004 | Lower Display Value | R/W | BCD (3 bytes) | 6-digit BCD, low 3 bytes valid |
| 0x0006-0x0007 | TIA1 Relay 1 Time | R | BCD | Relay 1 closure duration (read only) |
| 0x0008-0x0009 | TIA2 Relay 2 Time | R | BCD | Relay 2 closure duration (read only) |
| 0x000F-0x0010 | AL1 Alarm 1 | R/W | BCD | Must write with AL2 together via FC 0x10 (4 regs 0x000F~0x0012) |
| 0x0011-0x0012 | AL2 Alarm 2 | R/W | BCD | Must write with AL1 together via FC 0x10 (4 regs 0x000F~0x0012) |
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

## Work Mode (Read 0x0000 high byte)

| Value | Mode |
|---|---|
| 0x00 | Counter / Length meter |
| 0x01 | Timer |
| 0x02 | Timer + Counter |
| 0x03 | Total length meter |
| 0x04 | Batch length meter |

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
- For signed values, prefer integer registers (0x0021-0x0024) over BCD registers

## Error Codes

| Code | Description |
|---|---|
| E-3 | Sensor abnormal |
| E-10 | EEPROM abnormal |
