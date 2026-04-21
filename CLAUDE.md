# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 給 Claude CLI 的交接指引（接手必讀）

新 session 接手此專案時，**務必先做以下動作再開始工作**：

1. 讀 **`.claude/work_log.md`** — 最新進度 + 待完成項目（最新紀錄在最上方）；每個決策條目上方會標示「**規範權威：** xxx」告訴你該決策落在哪份規範文件
2. 讀 **`.claude/motion_flow.md`** — 完整運動流程規格（硬體表、phase、狀態機、指令協定、參數常數）
3. 讀 **`.claude/runbook.md`** — 啟動順序、Web GUI 按鈕對應、raw command 指令集、典型流程、緊急處置（知道「怎麼用」系統）
4. 本 CLAUDE.md 的硬體架構圖可能**落後於 motion_flow.md**，以 motion_flow.md §2 為準

待完成工作、已討論但尚未實作的設計決策，都在上述 `.claude/` 文件中。

## 多人協作紀律（重要）

本專案多人協作，每位協作者用自己的 Claude Code。協作者之間**只能透過 git 裡的文件同步資訊** — 任何 Claude 的本機 auto memory 都不會共享。因此：

- **決策 / 規範 / 架構變動一律寫進 git 追蹤的 .md 檔**（CLAUDE.md / motion_flow.md / work_log.md），不要只留在對話或個人 Claude memory
- **規範文件與程式改動放在同一個 PR**（不要只 commit 程式、.md 留到下次）；reviewer 會在 PR 看到規範 diff，討論後才合併
- **work_log 每筆重要決策要標「規範權威」指標**，指向該決策落在哪個權威文件（CLAUDE.md 某節 / motion_flow.md §X），方便協作者跳去確認完整規範

### 開 session 須知（所有協作者必看）

1. **告訴 Claude 你的角色**：「我是 Jim」/「我是協作者A」等，Claude 會根據角色自動遵守分工邊界
2. **若角色表中你的欄位還是 `_（待填）_`**，請填上姓名後 commit + push，讓其他協作者也看到
3. **掃一眼 `.claude/mailbox.md`** — 看有沒有給你的訊息或待處理需求
4. **讀 `.claude/work_log.md`** — 了解最新進度

### 協作者角色定義

| 角色 | 負責人 | 負責範圍 | 不碰的範圍 |
|------|--------|---------|-----------|
| **規範 + 裝置驅動** | Jim | `CLAUDE.md` / `motion_flow.md` / `.claude/` 文件 + `user_lib/` 硬體驅動 class（DM2J / ZDT / JC100 / WT901 / TCP_client / TCP_server / CLV900 / ZS_DIO / SD76 / PQW / DY500 等）| 應用層 `main.cpp` 實作、`WASH_ROBOT.{h,cpp}` |
| **應用層實作** | Sadie | `user_lib/WASH_ROBOT.{h,cpp}`（機器人編排層）、`washrobot_new_PI/main.cpp`、`Crane_control_PI/main.cpp` | `user_lib/` 裝置驅動、規範文件 |
| **前端** | Sadie | `web_backend/`、GUI 頁面 | `user_lib/`、`main.cpp` |
| **測試 / 工具** | Sadie | `Linux_test/`、`windows_test/` | 生產程式碼 |

> **備註：** 一人可兼多個角色。新協作者加入時自行填上姓名與負責範圍，commit 進 git。
> 動工前請確認要改的檔屬於誰的範圍，**跨界改動要先開 PR 討論**。

### 介面契約（user_lib 邊界）

**原則：** `user_lib/*.h` 的 public API 簽名是跨模組契約。

- **誰能改 user_lib/？** 原則上只有架構方（Jim）
- **協作者發現 bug？** 在 `.claude/mailbox.md` 提報，描述重現步驟。
  嚴重到阻塞工作的 bug → 可以先在自己的 branch 熱修，
  但必須開 PR 標 `[跨界: user_lib]`，等架構方 review 後才合併
- **協作者需要新功能？** 在 mailbox 提需求，描述「我需要什麼行為」，
  不要描述「幫我加什麼 method」— 讓架構方決定 API 設計
- **新增 class（新硬體驅動）？** 架構方負責，協作者提供硬體文件即可
- **不改 public API 簽名的內部改動**（private method、實作邏輯 bug fix）
  → 協作者可以修，但 PR 必須標 `[跨界: user_lib]`

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
washrobot_new_PI/    # 洗窗機器人主程式
Crane_control_PI/    # 吊車升降控制
Linux_test/          # Linux 環境下測試硬體工具
windows_test/        # Windows 環境下測試硬體工具
user_lib/            # 共用裝置驅動庫（所有硬體抽象層）
doc/                 # 各裝置技術文件（PDF、規格書）
x64/                 # 編譯輸出目錄
```

## Architecture

### 系統主體架構

```
[遠端/外部網路訊號]
  │
  ▼ (2-wire Tether 雙絞線)
[長距離通訊橋接] Fathom-X Tether Interface Board
  │
  ▼ (Ethernet)
[網路核心] 8 Port PoE Switch
  │
  ├─▶ (Ethernet) Raspberry Pi 5 (eth0) ─── 系統主控 ─── 192.168.1.100
  │     ├─▶ (USB→TTL) 姿態儀 WT901BC
  │     └─▶ (USB→CAN) 機械手臂控制器 [未來擴充，需終端電阻 120Ω]
  │
  ├─▶ (PoE) 防水型 2MP 攝影機 × 4
  │     ├─ 左上
  │     ├─ 左下
  │     ├─ 右上
  │     └─ 右下
  │
  ├─▶ (Ethernet) USR-TCP232-304 #1 ─── RS485_1 ─── 192.168.1.20
  │     └─▶ DM2J_RS570 × 5 (Slave 1~5)
  │           ├─ Slave 1: 左腳
  │           ├─ Slave 2: 左輪
  │           ├─ Slave 3: 右腳
  │           ├─ Slave 4: 右輪
  │           └─ Slave 5: 上滑台（乘載機械手臂）
  │
  ├─▶ (Ethernet) USR-TCP232-304 #2 ─── RS485_2 ─── 192.168.1.21
  │     └─▶ ZDT_motor_control × 9 (Slave 1~9) ─── 驅動 SMC LEYG25 200mm 推桿
  │           ├─ Slave 1~2: 左腳 × 2
  │           ├─ Slave 3~4: 左身體 × 2
  │           ├─ Slave 5~6: 右腳 × 2
  │           ├─ Slave 7~8: 右身體 × 2
  │           └─ Slave 9:   中心 × 1
  │
  ├─▶ (Ethernet) USR-TCP232-304 #3 ─── RS485_3 ─── 192.168.1.22
  │     ├─▶ JC_100_METER × 9 (Slave 1~9) ─── 真空氣壓感測器，各裝於推桿末端吸盤
  │     ├─▶ DY_500_weight_sensor × 2 (Slave 10~11) ─── 鋼索重量感測器
  │     └─▶ PQW_IO_16O_RLY × 1 (Slave 12, 8CH) ─── 吸盤真空 + 清洗系統控制
  │           ├─ CH1: dp0105 真空泵浦 × 9（共用，給電/斷電）
  │           ├─ CH2: VT307 電磁閥 ─── 腳組吸盤（左腳 + 右腳，共 4 顆）
  │           ├─ CH3: VT307 電磁閥 ─── 身體組吸盤（左右身體，共 4 顆）
  │           ├─ CH4: VT307 電磁閥 ─── 中心吸盤（獨立 1 顆，姿態校正用）
  │           ├─ CH5: 手臂刷洗滾筒馬達（裝於上滑台機械臂，清洗時旋轉）
  │           ├─ CH6: 水箱泵浦
  │           ├─ CH7: 水箱進水球閥（頂樓水壓 → 水箱補水）
  │           └─ CH8: 保留
  │
  │ ─────────── 吊機 (Crane) 子系統 ───────────
  │
  ├─▶ (Ethernet) Raspberry Pi ─── 吊機主控 ─── 192.168.1.101
  │
  └─▶ (Ethernet) USR-TCP232-304 #4 ─── RS485_crane ─── 192.168.1.30
        ├─▶ ZS_DIO_R_RLY × 1 (Slave 1, 8CH) ─── 左右鋼索絞盤繼電器（不經變頻器）
        │     ├─ CH1: 左收繩
        │     ├─ CH2: 右收繩
        │     ├─ CH3: 左放繩
        │     └─ CH4: 右放繩
        ├─▶ SD76_length_meters (Slave 2) ─── 左鋼索計米
        ├─▶ SD76_length_meters (Slave 3) ─── 右鋼索計米
        ├─▶ SD76_length_meters (Slave 4) ─── 中間管線計米（水管 + 電線）
        ├─▶ DSZL_107 (Slave 5) ─── 左鋼索張力感測 [TBD Modbus 暫存器表]
        ├─▶ DSZL_107 (Slave 6) ─── 右鋼索張力感測 [TBD Modbus 暫存器表]
        └─▶ 變頻器 (Slave 7) ─── 中間絞盤馬達控制 [TBD 型號 + 暫存器表]
```

### 分散式系統通訊

- **washrobot RPi (192.168.1.100)** 跑 `washrobot_new_PI/main.cpp`，TCP server :5001
- **crane RPi (192.168.1.101)** 跑 `Crane_control_PI/main.cpp`，TCP server :5002
- **Web GUI backend (Node.js)** 跑在 **crane RPi (.101) :8080**，橋接 Browser WebSocket ↔ 兩裝置的 TCP
  - 刻意放在救援側：washrobot 在半空中掛掉時，GUI 仍可透過 crane 手動收繩救援
  - 失聯模式 + 緊急收繩（按住持續收）行為詳見 `.claude/motion_flow.md` §8
- 自動下移時 washrobot 當 TCP client 連 crane :5002 下 `pay_out <cm>` 指令同步放繩
- 指令協定：簡單文字，`\n` 結尾；回應 `OK\n` / `OK <data>\n` / `ERR <msg>\n` / `EVT <type> <data>\n`
- 詳見 `.claude/motion_flow.md`

### 吸盤控制邏輯

每組推桿末端配有吸盤，真空吸附由以下元件組成：
- **dp0105** (24V 真空產生器) — 9 顆共用繼電器 CH1 統一供電
- **VT307** (24V 負壓電磁閥) — 3 分區控制，支援尺蠖式交替吸附：
  - **CH2: 腳組** — 左腳 × 2 + 右腳 × 2 = 4 顆吸盤（ZDT 推桿 slave 1,2,5,6 末端）
  - **CH3: 身體組** — 左身體 × 2 + 右身體 × 2 = 4 顆吸盤（ZDT 推桿 slave 3,4,7,8 末端）
  - **CH4: 中心** — 1 顆吸盤（ZDT 推桿 slave 9 末端），獨立控制，姿態校正時可單獨吸附

### 電源架構（參考）

```
[總電源輸入] AC 220V
  │
  ├─▶ [A組] EPP-200-24 (DC 24V) ─────────▶ 馬達剎車與機械手臂
  │     ├─ 上滑台、左腳、右腳、左輪、右輪（步進剎車 + 24V）
  │     └─ 機械手臂電源 [未來擴充]
  │
  ├─▶ [B組] EPP-200-24 (DC 24V) ─────────▶ 氣動、感測 I/O 與通訊介面
  │     ├─ Fathom-X Tether Interface Board
  │     ├─ SMC 推桿 ZDT 步進
  │     ├─ JC-100 氣壓表
  │     ├─ DY-500 重量感測器控制器 (100KG)
  │     └─ PQW 繼電器 (8CH)
  │          ├─ (Relay) ▶ dp0105 真空產生器
  │          └─ (Relay) ▶ VT307 電磁閥
  │
  ├─▶ [C組] EPP-200-48 (DC 48V) ─────────▶ 主動力與網路
  │     ├─ 8 Port PoE Switch
  │     └─ DM2J_RS570 步進控制器 × 5
  │
  └─▶ [變壓器] 插座式變壓器 (DC 5V) ──────▶ 主控與通訊模組
        ├─ Raspberry Pi 5
        └─ USR-TCP232-304 × 3
```

### Communication

All hardware uses **Modbus-TCP** over TCP/IP (port 4001). The `TCP_client` class provides a cross-platform socket wrapper (WinSock2 on Windows, BSD sockets on Linux) with auto-reconnect via a monitor thread.

Every device driver sends/receives raw Modbus frames: function codes 0x01 (read coils), 0x03 (read registers), 0x05/0x06 (write single), 0x10 (write multiple). CRC16-CCITT (polynomial 0xA001) is used for validation.

Socket timeouts: 100-500ms per device. TCP monitor thread: 500ms reconnect polling interval.

### Device Drivers (`user_lib/`)

**使用中：**

| Class | Device | Interface | Description |
|---|---|---|---|
| `TCP_client` | TCP socket abstraction | WinSock2/BSD | Cross-platform TCP with auto-reconnect & monitor thread |
| `TCP_server` | TCP listener | WinSock2/BSD | washrobot :5001 / crane :5002，多 client、line-buffered |
| `Serial_port` | Serial port (Windows/Linux) | Native | TTL serial communication (8N1, multiple baud rates) |
| `DM2J_RS570` | 步進馬達驅動器 × 5 | Modbus-TCP (RS485_1) | 左腳/左輪/右腳/右輪/上滑台，cm 精度，PR/JOG/Home 模式 |
| `ZDT_motor_control` | 閉環步進驅動卡 × 9 | Modbus-TCP (RS485_2) | 驅動 SMC LEYG25 推桿，encoder 回饋，堵轉保護 |
| `JC_100_METER` | 真空氣壓感測器 × 9 | Modbus-TCP (RS485_3) | 讀取壓力 (0.1 kPa)，裝於各推桿末端吸盤 |
| `DY_500_weight_sensor` | 鋼索重量感測器 × 2 | Modbus-TCP (RS485_3) | 讀取重量 (int32/float)，裝於機體與鋼索連接處 |
| `PQW_IO_16O_RLY` | 8CH 繼電器模組 × 1 | Modbus-TCP (RS485_3) | 控制 dp0105 泵浦 + VT307 電磁閥 + 刷洗/水泵/水閥 |
| `WT901BC_TTL` | 九軸姿態儀 | USB→TTL Serial 115200 | 背景執行緒連續讀取，checksum 驗證；Roll+Pitch 平衡監控 |
| `ZS_DIO_R_RLY` | 8CH 繼電器模組 × 1 | Modbus-TCP (RS485_crane) | 吊機左右鋼索絞盤收/放繩控制 |
| `SD76_length_meters` | 計米器 × 3 | Modbus-TCP (RS485_crane) | 左/右鋼索 + 中間管線計米，int32 讀取，支援 pause/resume/zero |

**待實作（等硬體文件到位）：**

| Class | Device | Description |
|---|---|---|
| `DSZL_107` | 張力感測器 × 2 | 左/右鋼索張力 (kg)，裝於吊機端；等使用者補 Modbus 暫存器表 |
| 中間絞盤變頻器 | 馬達控制器 × 1 | 中間放繩同步 (RPM + 方向)，等使用者補型號與暫存器表 |

**未使用：**

| Class | Description |
|---|---|
| `DIHOOL_control` | 馬達定位控制器，已編譯未整合 |
| `QX_DO24` | 數位輸出模組，已編譯未整合 |

### Driver Initialization Pattern

All device drivers support two initialization modes:
```cpp
// Mode A: create internal TCP connection
bool init(const std::string& ip, int port, int ID, bool debug = false);

// Mode B: share external TCP connection (recommended for multiple devices on same controller)
bool init(TCP_client& extClient, int ID, bool debug = false);
```

### Concurrency Model

- `TCP_client`: background `reconnectLoop()` monitor thread (500ms polling), `std::mutex socket_mtx` for thread-safe socket access
- `WT901BC_TTL`: dedicated `_worker_thread` for continuous serial read, `std::atomic<bool> read_error` flag
- All shared state uses `std::atomic<>` or mutex protection

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

## Coding Style

### 函式回傳值規範（Return Value Convention）

所有 bool 函式統一遵循以下規範：

- 函式執行**成功（無異常）**時，回傳 `false`
- 函式執行**失敗（有錯誤）**時，回傳 `true` 或具體的 error code

```cpp
// ✓ 正確寫法
bool init(const std::string& ip, int port) {
    if (!client->connectToServer(ip, port))
        return true;   // error: connect failed
    return false;       // success
}

// 呼叫端
if (drv.init("192.168.1.20", 4001)) {
    std::cerr << "init failed" << std::endl;
    return 1;
}
```

> **注意：** 此規範與常見的 `true=success` 相反，新增或修改函式時務必遵守。

### 溝通語言與註解語言

- 溝通使用**繁體中文**
- 程式碼註解使用**英文**

### 物件內 function 區塊分隔註解

在 class 中，不同性質的 function 群組之間須加上區塊分隔註解：

```cpp
//=========== init ===========

//=========== control ===========

//=========== read ===========

//=========== utility ===========
```

### Log 格式規範（user_lib 統一）

`user_lib/` 所有驅動的 log **必須**透過 `user_lib/log_utils.h` 的巨集輸出，格式固定：

```
[HH:MM:SS.mmm] [LEVEL] [DEVICE:ID] <message>
```

**Levels：**

| Macro | Level | 用途 |
|---|---|---|
| `LOG_ERR(tag, fmt, ...)` | ERR | 嚴重錯誤（連線斷、CRC 錯、timeout 超限）|
| `LOG_WRN(tag, fmt, ...)` | WRN | 警告（重試、異常值但可繼續）|
| `LOG_INF(tag, fmt, ...)` | INF | 重要流程訊息（動作完成、狀態轉換）|
| `LOG_DBG(tag, fmt, ...)` | DBG | 除錯訊息 |
| `LOG_HEX(tag, note, data, len)` | DBG | Hex dump |

**所有 level 都由 `debug_mode` 成員統一控制：**

- `debug_mode == false`（預設）→ 完全靜默，一行都不印
- `debug_mode == true` → ERR/WRN/INF/DBG/HEX 全部輸出
- 理由：錯誤本來就透過 bool return（true=error）通知呼叫端，log 純為除錯觀察用；驅動庫預設不吵，由使用者決定何時打開

**呼叫端要求：**

- 每個驅動 class 必須有成員 `std::string _log_tag`，在 `init()` 裡設為 `"DEVICE:ID"`（例：`"DM2J:3"`、`"ZDT:5"`、`"TCP"`）
- 每個驅動 class 必須有成員 `bool debug_mode`（所有 LOG_* 巨集都依賴它）
- **禁止**在驅動內直接用 `printf` / `std::cout` / `std::cerr`（regression 守則）

**範例：**

```cpp
LOG_ERR(_log_tag, "PPR read failed");
LOG_INF(_log_tag, "target %.3f cm -> %d pulses", pos_cm, pulses);
LOG_DBG(_log_tag, "status=0x%08X", st);
LOG_HEX(_log_tag, "TX", buf, len);
```

輸出範例：

```
[14:32:01.123] [INF] [DM2J:3] target 15.500 cm -> 6200 pulses
[14:32:01.145] [DBG] [DM2J:3] TX 03 03 00 5F 00 02 35 86
[14:32:01.178] [ERR] [DM2J:3] Motion timeout
```

規範範圍：`user_lib/` 資料夾內。`main.cpp` / 應用層的 log 不受此約束（但建議對齊）。
