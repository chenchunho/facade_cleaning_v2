#ifndef FCV_MECHANISM_ROPE_AXIS_H
#define FCV_MECHANISM_ROPE_AXIS_H

// 機構層 —— 虛擬軸。
//
// [2026-08-30] 重構階段 3（見 .claude/refactor_plan.md §3.1）。
//
// 🔴 為什麼參考架構的 `IAxis`（一個裝置 = 一個軸）在這台機器上不成立：
//    14 支 driver 裡**真正的位置軸只有 2 支**（ZDT 推桿、DM2J 上滑台）。
//    SE3／MH300／CLV900 是 VFD —— **只有速度輸出、沒有位置回授**。
//    吊機的一條繩是：
//        SE3 變頻器（USR_A/B，速度輸出）
//      + SD76 計米器（USR_M，位置回授）
//      + DSZL-107  （X518 獨佔，力回授）
//    = **三個裝置、三條匯流排**，而且是刻意拆開的（見 main.cpp 開頭的理由）。
//
// 🔴 這層不是新概念，是把已經存在的東西寫成型別。
//    重構前 `Crane_control_PI/main.cpp` 有**約 340 處**以 `_left`/`_right` 成對
//    出現的識別字（`vfd` 86／`g_length` 48／`meter` 29／`dsz` 28／
//    `hold_up` 28／`hold_down` 26／`g_dev_*` 74），還有 `resolve_meter_side()`
//    這種把 side 映射到裝置的臨時 helper。
//    **機構層早就存在了，只是拼寫成後綴而不是型別。**
//
// 🔴 不做這層的代價是可量測的：每一個新功能都得記得「左邊做一次、右邊做一次」。
//    2026-08-28 抓到的「吸盤左右歸屬錯了四個月、每個分側判準實際上都在看
//    『一邊各一顆』＝**等於沒有保護**」，就是同一個 class 的缺陷在本體那邊的實例。
//
// 目標是讓「側」變成**參數**而不是名字的一部分：
//      rope(side_left).vfd.setFreqHz(...)
//    取代
//      side_left ? vfd_left.setFreqHz(...) : vfd_right.setFreqHz(...)
//
// ⚠️ 這是階段 3 的**第一個增量**：先讓型別存在、把「選邊」的呼叫點收斂進來。
//    閉環（VFD 出力 → SD76 讀位置 → 張力守衛）目前仍散在 main.cpp，
//    那是下一個增量，**刻意不混在同一次改動裡** —— 這一次要能用等價比對證明
//    「行為完全沒變」，混進閉環搬遷就證不了了。

#include <atomic>

template <typename Vfd, typename Meter, typename Tension>
struct RopeAxisT {
    const char*         name;       // "left" / "right" —— 只用於訊息
    Vfd&                vfd;        // 速度輸出（無位置回授）
    Meter&              meter;      // 位置回授（另一條 bus）
    Tension&            tension;    // 力回授（第三條 bus）

    // 裝置可用旗標。**刻意保留三個分開的旗標而不是合成一個** ——
    // 現有程式碼的錯誤訊息會分別講「vfd_left_unavailable」「meter_left_unavailable」，
    // 合成一個就分不出是哪一個掛了，那是行為改變不是整理。
    std::atomic<bool>&  dev_vfd;
    std::atomic<bool>&  dev_meter;
    std::atomic<bool>&  dev_tension;
};

#endif  // FCV_MECHANISM_ROPE_AXIS_H
