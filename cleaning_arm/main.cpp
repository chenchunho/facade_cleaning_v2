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

	std::cout << "Ready. TCP commands on port " << cfg.tcp_port << "\n";
	std::cout << "Press Ctrl+C to quit.\n\n";

	// ---- 主執行緒：等待 TCP 指令，不干預馬達狀態 ----------------
	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
}
