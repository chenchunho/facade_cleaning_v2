#ifndef FCV_COMMAND_DISPATCHER_H
#define FCV_COMMAND_DISPATCHER_H

// 指令層 —— 文字協定的解析、分派、參數驗證。
//
// [2026-08-30] 重構階段 2（見 .claude/refactor_plan.md §3.2）。
// 從 facade_cleaning_v2/main.cpp 抽出，那個檔案原本 521 行裡有 373 行是分派器。
//
// 🔴 為什麼這層要獨立存在（不是為了美觀）：
//    2026-08-27 把吸盤編號 1-4 改成 5-8 之後，`zdt_pusher`/`zdt_disable`/`zdt_enable`
//    **變成不可能成功** —— 分派器收 1-4、應用層收 5-8，**兩個範圍沒有交集**。
//    範圍驗證與被驗證的常數分居兩處就會分岔。這層明確化之後，
//    「參數驗證」有了唯一的家，而它必須吃與應用層同一組常數。
//
// 🔴 FAST / SLOW 雙路徑必須保留：`stop`/`estop`/`status`/`pause` 走同步路徑。
//    理由是修過的 bug —— 長時間運動指令佔住收包執行緒時 `stop` 送不進去 ＝ GUI 死鎖。
//    **急停排在一個 15 秒的 move 後面，等於沒有急停。**

#include <string>

class WashRobot;

namespace command {

// 解析一行文字指令並執行。回傳要送回客戶端的字串（含換行）。
// 🔴 `robot` 由呼叫端傳入而不是用全域 —— 這層不該知道「只有一台機器」。
std::string dispatch(WashRobot& robot, const std::string& line);

// 這個動詞走不走 FAST 路徑（同步、在收包執行緒上執行）。
bool is_fast(const std::string& verb);

}  // namespace command

#endif  // FCV_COMMAND_DISPATCHER_H
