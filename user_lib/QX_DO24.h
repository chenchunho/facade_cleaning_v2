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

	// ---- 占空比安全上下限（重要，出廠預設就是保守值）--------------------
	// setPWM_Duty 會拒絕落在 [duty_min_pct, duty_max_pct] 之外的值。
	//
	// 預設 5.0~10.0 對應目前實際接的那顆馬達規格：**5% = 停止、10% = 全速**。
	// 低於 5% 是規格外，高於 10% 等於下超過全速的指令 —— 兩者都不該送出去。
	// [2026-08-26 per user] 四個通道一律套用同一組限制。
	//
	// 這是 fail-safe 設計：預設就是最嚴格的範圍，接了規格不同的裝置（風扇
	// 0~100%、其他伺服 2.5~12.5% 等）必須「主動」呼叫 setDutyLimits 放寬，
	// 忘記設定時得到的是安全的窄範圍，而不是危險的全範圍。
	void setDutyLimits(double min_pct, double max_pct);
	double dutyMinPct() const { return duty_min_pct; }
	double dutyMaxPct() const { return duty_max_pct; }

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

	// see setDutyLimits() — fail-safe defaults matching the wired motor
	// (5% = stop, 10% = full speed)
	double duty_min_pct = 5.0;
	double duty_max_pct = 10.0;

	// 通訊核心
	bool sendAndReceive(const std::vector<uint8_t>& request, std::vector<uint8_t>& response);
	bool readRegs(uint16_t addr, uint16_t count, std::vector<uint16_t>& out);
	uint16_t modbusCRC(const uint8_t* data, int len);
};

#endif