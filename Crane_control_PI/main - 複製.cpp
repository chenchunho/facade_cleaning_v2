#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <cmath>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

#include "TCP_client.h"
#include "ZS_DIO_R_RLY.h"
#include "DY_500_weight_sensor.h"
#include "SD76_length_meters.h"

#define CRANE_DOWN_PIN   7
#define CRANE_UP_PIN     8

TCP_client cli_21;
TCP_client cli_22;
ZS_DIO_R_RLY relay;
DY_500_weight_sensor dy;
SD76_length_meters meter;

std::atomic<bool> program_running(true);
std::atomic<float> g_weight(0.0f);  // 全域重量變數

void control_loop()
{
	// 用於計算平均的變數
	double weight_sum = 0.0;
	int weight_count = 0;
	float current_avg = 0.0f;

	while (program_running)
	{
		float w = 0.0f;
		if (!dy.get_weight_float(w)) {
			// 累加數值
			weight_sum += w;
			weight_count++;

			// 每 500 次計算一次平均並更新全域變數
			if (weight_count >= 10) {
				current_avg = static_cast<float>(weight_sum / 10.0);
				g_weight.store(current_avg, std::memory_order_relaxed);

				// 重置計數與累加器
				weight_sum = 0.0;
				weight_count = 0;
			}
		}
		else {
			std::cout << "[ERROR] weight read fail\n";
		}

		// 畫面更新邏輯 (建議顯示平均值 current_avg)
		std::cout << "\033[s";     // save cursor
		std::cout << "\033[H";     // go top

		std::cout << "\033[2K";
		// 這裡顯示目前平均值，若還沒滿500次會顯示上一次的結果
		std::cout << "[WEIGHT AVG] " << current_avg << " kg (sampling: " << weight_count << "/10)\n";

		std::cout << "\033[2K";
		std::cout << "-------------------------------------\n";

		std::cout << "\033[2K";
		std::cout << "Command: manual / auto / hold / release / up / down / exit\n";

		std::cout << "\033[u" << std::flush;

		// 原本 sleep 1ms，500次大約 0.5 秒更新一次平均值
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}
int main()
{
	std::cout << "\033[2J\033[H";

	cli_21.connectToServer("192.168.1.21", 4001);
	cli_22.connectToServer("192.168.1.22", 4001);

	relay.init(cli_21, 16, false);
	meter.init(cli_21, 2, true);
	dy.init(cli_22, 3, false);

	std::cout << "\n\n\n";

	std::thread t(control_loop);

	while (true)
	{
		std::string cmd;

		std::cout << "> ";
		std::cin >> cmd;

		std::cout << "> " << cmd << "\n";

		if (cmd == "manual") {
			relay.controlRelay(CRANE_UP_PIN, false);
			relay.controlRelay(CRANE_DOWN_PIN, false);
			std::cout << "[MODE] MANUAL\n";
		}
		else if (cmd == "auto") {
			termios oldt, newt;
			tcgetattr(STDIN_FILENO, &oldt);
			newt = oldt;
			newt.c_lflag &= ~(ICANON | ECHO);
			tcsetattr(STDIN_FILENO, TCSANOW, &newt);

			std::cout << "[MODE] AUTO (press 'x' to exit)\n";

			float base_weight = g_weight.load(std::memory_order_relaxed);
			std::cout << "[AUTO] base weight = " << base_weight << " kg\n";

			bool up_on = false;
			bool down_on = false;

			while (true) {
				float w = g_weight.load(std::memory_order_relaxed);
				float delta = w - base_weight;

				if (delta > 6.0f) {
					if (!up_on) {
						relay.controlRelay(CRANE_UP_PIN, true);
						relay.controlRelay(CRANE_DOWN_PIN, false);
						up_on = true;
						down_on = false;
						std::cout << "[AUTO] w=" << w << " → UP\n";
					}
				}
				else if (delta < -6.0f) {
					if (!down_on) {
						relay.controlRelay(CRANE_UP_PIN, false);
						relay.controlRelay(CRANE_DOWN_PIN, true);
						down_on = true;
						up_on = false;
						std::cout << "[AUTO] w=" << w << " → DOWN\n";
					}
				}
				else {
					if (up_on || down_on) {
						relay.controlRelay(CRANE_UP_PIN, false);
						relay.controlRelay(CRANE_DOWN_PIN, false);
						std::cout << "[AUTO] w=" << w << " → STOP\n";
						up_on = false;
						down_on = false;
					}
				}

				// check key press 'x' to exit
				fd_set set;
				struct timeval timeout;
				FD_ZERO(&set);
				FD_SET(STDIN_FILENO, &set);
				timeout.tv_sec = 0;
				timeout.tv_usec = 0;

				int rv = select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout);
				if (rv > 0) {
					char c;
					read(STDIN_FILENO, &c, 1);
					if (c == 'x' || c == 'X') {
						std::cout << "[AUTO] Exit by key\n";
						break;
					}
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}

			// 安全關閉
			relay.controlRelay(CRANE_UP_PIN, false);
			relay.controlRelay(CRANE_DOWN_PIN, false);
			tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
		}
		else if (cmd == "hold") {
			const float HOLD_THRESHOLD = -0.4f;
			std::cout << "[MODE] HOLD (↑ when weight > " << HOLD_THRESHOLD << ")\n";

			while (true)
			{
				// 讀取的是 500 次採樣的平均值
				float w = g_weight.load(std::memory_order_relaxed);

				if (w > HOLD_THRESHOLD) {
					relay.controlRelay(CRANE_UP_PIN, true);
					relay.controlRelay(CRANE_DOWN_PIN, false);
					std::cout << "[HOLD] w=" << w << " → UP\r" << std::flush;
				}
				else {
					relay.controlRelay(CRANE_UP_PIN, false);
					relay.controlRelay(CRANE_DOWN_PIN, false);
					std::cout << "\n[HOLD] Target reached. STOP.\n";
					break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			// --- 修正後的結束判斷邏輯 ---
			// 停止後稍微等一下，確保讀到的是停止後的穩定平均值
			std::this_thread::sleep_for(std::chrono::milliseconds(600));
			float final_w = g_weight.load(std::memory_order_relaxed);

			std::cout << "[HOLD] Final stable weight = " << final_w << " kg\n";

			if (final_w < -2.0f) {
				std::cout << "[ADJUST] Weight < -2kg, releasing (DOWN) for 500ms...\n";
				relay.controlRelay(CRANE_DOWN_PIN, true);
				std::this_thread::sleep_for(std::chrono::milliseconds(300));
				relay.controlRelay(CRANE_DOWN_PIN, false);
				std::cout << "[ADJUST] Done.\n";
			}
			// -------------------------
		}
		else if (cmd == "release") {
			const float RELEASE_THRESHOLD = -1.0f;
			std::cout << "[MODE] RELEASE (↓ while weight < " << RELEASE_THRESHOLD << ", STOP when ≥ " << RELEASE_THRESHOLD << ")\n";

			while (true)
			{
				float w = g_weight.load(std::memory_order_relaxed);

				if (w < RELEASE_THRESHOLD) {
					relay.controlRelay(CRANE_DOWN_PIN, true);
					relay.controlRelay(CRANE_UP_PIN, false);
					std::cout << "[RELEASE] w=" << w << " → DOWN\n";
				}
				else {
					relay.controlRelay(CRANE_DOWN_PIN, false);
					relay.controlRelay(CRANE_UP_PIN, false);
					std::cout << "[RELEASE] w=" << w << " → STOP & EXIT RELEASE\n";
					break;
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}

			// 安全關閉
			relay.controlRelay(CRANE_UP_PIN, false);
			relay.controlRelay(CRANE_DOWN_PIN, false);

			float final_w = g_weight.load(std::memory_order_relaxed);
			std::cout << "[RELEASE] done, current weight = " << final_w << " kg\n";
		}
		else if (cmd == "up") {
			int ms = 0;
			std::cin >> ms;
			std::cout << "> up " << ms << "\n";

			// 限制範圍在 100ms - 3000ms 之間
			if (ms < 100 || ms > 3000) {
				std::cout << "[ERR] ms must be between 100 and 3000\n";
				continue;
			}

			relay.controlRelay(CRANE_UP_PIN, true);
			relay.controlRelay(CRANE_DOWN_PIN, false);

			std::this_thread::sleep_for(std::chrono::milliseconds(ms));

			relay.controlRelay(CRANE_UP_PIN, false);
			std::cout << "[DONE] up\n";
		}
		else if (cmd == "down") {
			int ms = 0;
			std::cin >> ms;
			std::cout << "> down " << ms << "\n";

			// 限制範圍在 100ms - 3000ms 之間
			if (ms < 100 || ms > 3000) {
				std::cout << "[ERR] ms must be between 100 and 3000\n";
				continue;
			}

			relay.controlRelay(CRANE_DOWN_PIN, true);
			relay.controlRelay(CRANE_UP_PIN, false);

			std::this_thread::sleep_for(std::chrono::milliseconds(ms));

			relay.controlRelay(CRANE_DOWN_PIN, false);
			std::cout << "[DONE] down\n";
		}
		else if (cmd == "exit") {
			program_running = false;
			break;
		}
		else {
			std::cout << "[ERR] Unknown command\n";
		}
	}

	t.join();
	relay.controlRelay(CRANE_UP_PIN, false);
	relay.controlRelay(CRANE_DOWN_PIN, false);

	return 0;
}
