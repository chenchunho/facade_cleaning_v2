#ifndef QX_DO24_H
#define QX_DO24_H

#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <cstdint>   // uint16_t / uint32_t used below — don't rely on transitive includes

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

	// 綜合控制：依序執行 **Freq -> Duty -> Control**，全部成功才回傳 true。
	// 順序是刻意的（理由見 .cpp）：頻率先設好，占空比才有意義；control 最後
	// 才開，負載不會看到中間狀態。中途失敗會保留前面已寫入的部分。
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

	// ---- 頻率安全上下限（跟占空比限制是連動的，別分開看）----------------
	// setPWM_Freq 會拒絕落在 [freq_min_hz, freq_max_hz] 之外的值，預設鎖死
	// 50Hz（min=max=50）。
	//
	// ⚠ 為什麼頻率也要鎖：占空比的 5%=停止 / 10%=全速 **只有在 50Hz 才成立**
	//   （50Hz → 週期20ms → 5%=1.0ms、10%=2.0ms）。換成模組的上電預設 1000Hz，
	//   同樣的 10% 只有 0.1ms，調速器收到的是無效訊號。占空比限制只看百分比、
	//   看不到頻率，擋不住這種情況，所以兩個限制必須一起存在才有意義。
	//
	// ⚠ 模組上電預設頻率是 **1000Hz 不是 50Hz**，而且 0x00~0x0F 都是「臨時
	//   生效、斷電恢復」——每次重新上電都會跳回 1000Hz，必須重下一次 50Hz。
	// [2026-08-26 per user] 四個通道一律鎖 50Hz。
	void setFreqLimits(int min_hz, int max_hz);
	int freqMinHz() const { return freq_min_hz; }
	int freqMaxHz() const { return freq_max_hz; }

	// ---- 保存為開機預設（⚠ 破壞性、有壽命上限，非必要不要用）--------------
	// 對暫存器 0x10 寫非 0 值，把 0x00~0x0F 目前的值（4 通道的占空比/頻率/
	// 控制）存成模組的上電預設。
	//
	// ⚠ 手冊明列的限制與風險：
	//   1. 內部儲存體擦寫壽命只有約 **1~2 千次**，寫壞不保固。
	//   2. 模組**每次上電只接受一次**這個寫入（廠商為防呆刻意加的限制），
	//      第二次會被拒絕 —— 這是裝置行為，不是本 driver 的 bug。
	//   3. **最危險的一點**：如果保存時 control=65535（持續輸出），那模組
	//      之後**一通電就會立刻開始輸出 PWM**，變成「插電馬達就轉」。要存
	//      之前先想清楚當下的 control 值是什麼。
	//
	// 0x00~0x0F 本身是「臨時生效、斷電恢復」，可無限次修改、不耗擦寫壽命；
	// 只有這個函式會動到 flash。
	bool saveOutputAsDefault();

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
	// see setFreqLimits() — locked to 50Hz; the duty window above is only
	// physically meaningful at this frequency
	int freq_min_hz = 50;
	int freq_max_hz = 50;

	// 通訊核心
	// [2026-08-28] 交易層重試 + 失敗原因外露。
	// 實測（bench，本模組 slave 9 @ 115200）：寫入 10 次有 2 次 `no reply (timeout)`，
	// 讀取則 10/10 成功 —— 而且**失敗剛好發生在「停止螺旋槳」那一發**。
	// 🔴 關鍵性質：寫入失敗不會讓輸出歸零，模組會**保持前一個值繼續輸出**。
	//    也就是通訊斷掉時螺旋槳不會停，會維持轉速。因此重試必須放在這一層，
	//    而不是指望每個呼叫端都記得重試（左右螺旋槳共用 CH1）。
	// ⚠ 只重試「傳輸層」失敗（無回應／回覆過短／CRC 錯）。
	//   `device rejected` 是模組明確拒絕，重試沒有意義，也可能是真的參數不對。
	bool sendAndReceive(const std::vector<uint8_t>& request, std::vector<uint8_t>& response);
	bool sendAndReceiveOnce_(const std::vector<uint8_t>& request, std::vector<uint8_t>& response);

public:
	// 最近一次失敗的原因。讓呼叫端能把「通訊失敗」和「參數超出安全範圍」分開講 ——
	// 2026-08-28 實測送合法的 hz=50 卻收到「頻率被鎖」的錯誤訊息，就是因為
	// 呼叫端只拿得到一個 bool。（同型問題 2026-08-28b 在 Linux_test 修過，漏了主程式）
	enum class Fail { None, NoReply, TooShort, CrcMismatch, DeviceRejected, OutOfRange, NotReady };
	Fail        last_fail() const { return last_fail_; }
	const char* last_fail_str() const;
private:
	Fail last_fail_ = Fail::None;
	bool readRegs(uint16_t addr, uint16_t count, std::vector<uint16_t>& out);
	uint16_t modbusCRC(const uint8_t* data, int len);
};

#endif