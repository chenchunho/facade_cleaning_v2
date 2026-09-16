// ==========================================================
//  跨平台入口（Windows / Linux）
//  ---------------------------------------------------------
//  設定檔搜尋順序：
//    1. argv[1]（命令列指定路徑）
//    2. 執行檔目錄下的 damiao.cfg
//    3. 找不到 → 使用平台內建預設值（不需設定檔也可執行）
//
//  TCP 指令前綴：M1 大馬達 / M2 小馬達（左右軸）
//    例：M1 ENABLE、M2 LR_SLOT LEFT、M1 CALIBRATE
// ==========================================================

#include "damiao_config.h"
#include "main_api.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main(int argc, char* argv[])
{
	// ---- 載入設定檔 -----------------------------------------------
	DamiaoConfig cfg;
	std::string  cfg_err;
	bool         cfg_loaded = false;

	const char* candidates[] = {
		(argc >= 2) ? argv[1] : nullptr,
		"damiao.cfg",
		nullptr
	};
	for (int i = 0; i < 2; ++i) {
		if (!candidates[i]) continue;
		if (load_config(candidates[i], cfg, cfg_err)) {
			std::cout << "[config] Loaded: " << candidates[i] << "\n";
			cfg_loaded = true;
			break;
		}
	}
	if (!cfg_loaded) {
		std::cerr << "[config] Not found (" << cfg_err << "); using built-in defaults.\n";
#ifdef _WIN32
		cfg.port = "\\\\.\\COM10";
#else
		cfg.port = "/dev/ttyACM0";
#endif
		cfg.baud     = 921600u;
		cfg.tcp_port = 9527;
		cfg.m1 = {damiao::DM10010L,   0x01, 0x11};
		cfg.m2 = {damiao::DM4340_48V, 0x02, 0x22};
	}

	// ---- Linux: uint32_t baud → speed_t ---------------------------
#ifndef _WIN32
	speed_t linux_baud;
	if (!baud_to_speed_t(cfg.baud, linux_baud)) {
		std::cerr << "[config] Unsupported baud rate: " << cfg.baud << "\n";
		return 1;
	}
#endif

	// ---- 初始化：序列埠 / M1 大馬達 / M2 小馬達 / TCP 監聽埠 --------
	DamiaoAPI api;
	if (!api.init(
			cfg.port.c_str(),
#ifdef _WIN32
			cfg.baud,
#else
			linux_baud,
#endif
			{cfg.m1.type, cfg.m1.slave_id, cfg.m1.master_id},
			{cfg.m2.type, cfg.m2.slave_id, cfg.m2.master_id},
			cfg.tcp_port))
	{
		std::cerr << "DamiaoAPI init failed\n";
		return 1;
	}

	// ---- 啟動背景 TCP 伺服器（非阻塞）---------------------
	api.start();

	// [2026-09-04 per user] 開機自動就緒：上電 → M1 回機械零點 → M2 滾筒(RIGHT)。
	// 🔴 [2026-09-16 per user] **再往前走一步：STARTUP 之後直接跑 INIT，開機即待命。**
	//    理由：INIT（M1 撞停點校零 + M2 左右校正）本來就是每次開工必做的第一件事，
	//    而它只動手臂自己、不碰吸盤/吊機 ⇒ 沒有理由讓人在 GUI 上多按一次。
	//    做完之後 `init_done=1`，本體的 `arm_ready` 直接就是 1，GUI 的前置「手臂」那項可以拿掉。
	//    ⚠️ 代價：服務啟動會多花約 10~20 秒，而且手臂會多動一輪（M1 撞停點、M2 找左右極限）。
	//       壓牆時重啟服務仍然要先確認沒壓著 —— 這點與 STARTUP 相同，deploy.sh 會問。
	//    ARM_NO_AUTOSTART=1 兩段都跳過（維持失能，需自行 ENABLE/INIT）。
	{
		const char* off = std::getenv("ARM_NO_AUTOSTART");
		if (off && off[0] == '1') {
			std::cout << "[STARTUP] ARM_NO_AUTOSTART=1 — 跳過自動就緒，"
			             "兩顆維持失能（需自行送 ENABLE 或 INIT）\n";
		} else {
			std::cout << api.startup_then_init() << "\n";
		}
	}

	std::cout << "Ready. TCP commands on port " << cfg.tcp_port << "\n";
	std::cout << "Press Ctrl+C to quit.\n\n";

	// ---- 主執行緒：等待 TCP 指令，不干預馬達狀態 ----------------
	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
}
