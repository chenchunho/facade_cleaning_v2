#ifndef QX_DO24_H
#define QX_DO24_H

#include <vector>
#include <string>
#include <memory>
#include <chrono>

class TCP_client;

// ============================================================================
// QX_DO24 — 四川旗芯 QX-DO24 四路 PWM 信號輸出模組 (Modbus-RTU)
//
// ⚠⚠ RETURN VALUE CONVENTION IS INVERTED vs the rest of this project ⚠⚠
//   Every method here returns  true = SUCCESS / false = FAILURE.
//   CLAUDE.md mandates the opposite (false = success) and every other driver
//   in user_lib/ follows that. Calling this class with the project habit
//   `if (drv.foo()) { error }` reports success as failure and vice versa —
//   this exact mistake was made once in Linux_test menu 34 and made the whole
//   bench readout lie. Read the polarity here before writing any caller.
//
// Register map (manual QX-DO24_Product_manual V1.16, holding regs, FC 03/06/10):
//   0x00~0x03  ch1~4 duty      0~1000 = 0~100.0%   (16-bit)
//   0x04~0x0B  ch1~4 frequency 1~200000 Hz, 32-bit ABCD (hi word first)
//   0x0C~0x0F  ch1~4 control   0=off / 65535=continuous / 1~65534=pulse count
//   0x10       save-to-flash   ⚠ ~1-2k write-cycle life, once per power-up only
//   0x20 addr / 0x21 baud / 0x22 firmware version / 0x23 comm format
// ============================================================================
class QX_DO24 {
public:
	QX_DO24();
	~QX_DO24();

	// 初始化
	bool init(const std::string& ip, int port, int ID = 1, bool debug = false);
	bool init(TCP_client& extClient, int ID = 1, bool debug = false);

	// 綜合控制：依序執行 Duty -> Freq -> Control，全部成功才回傳 true
	// control 預設 65535 = 持續輸出（伺服/風扇/無刷馬達要求一直有訊號；
	// 帶 0 進來等於關掉輸出，撐著負載的裝置會失去保持力）
	bool setChannel(int channel, double duty, int freq, uint16_t control = 65535);

	// 獨立控制函式 (皆含 500ms 等待與正確性檢查)
	bool setPWM_Duty(int channel, double duty_percent);
	bool setPWM_Freq(int channel, int freq);
	bool setPWM_Control(int channel, uint16_t val);

	// 讀回 (FC 0x03)
	bool getPWM_Duty(int channel, double& duty_percent);
	bool getPWM_Freq(int channel, uint32_t& freq);
	bool getPWM_Control(int channel, uint16_t& val);
	bool getVersion(uint16_t& version);

private:
	TCP_client* client = nullptr;
	std::unique_ptr<TCP_client> owned_client;
	int deviceID = 6;
	bool debug_mode = false;
	std::string _log_tag;

	// 通訊核心
	bool sendAndReceive(const std::vector<uint8_t>& request, std::vector<uint8_t>& response);
	bool readRegs(uint16_t addr, uint16_t count, std::vector<uint16_t>& out);
	uint16_t modbusCRC(const uint8_t* data, int len);
};

#endif