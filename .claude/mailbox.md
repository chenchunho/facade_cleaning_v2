# 協作信箱 (Mailbox)

> 跨分工邊界的需求、通知、問題。
> 每位協作者的 Claude 開 session 時應掃一眼此檔。
> 處理完的條目移到「已處理」段，保留作為決策紀錄。
>
> **阻塞程度標記：** 🔴 急迫（阻塞工作）/ 🟡 非急迫（可等）/ 🟢 參考通知
>
> **格式：** `- **[日期] 來源角色:** 描述（含用途/上下文 + 阻塞程度）`

## 待處理

### → 架構（Jim）

- **[2026-04-22] Sadie:** 🟡 `user_lib/TCP_client.cpp:175` 和 `TCP_server.cpp:175` 的 `send(sock, buf, len, 0)` 沒帶 `MSG_NOSIGNAL`，Linux 下對已關閉對端寫入會送 SIGPIPE 把 process 殺掉。現場曾踩到：washrobot 跑到一半對端斷，shell 顯示 `Broken pipe` 後 process 死。已在三個 main.cpp（washrobot / Crane_control_PI / Crane_easy_PI）加 `signal(SIGPIPE, SIG_IGN)` 先擋住，但長期建議 user_lib 的 send 統一改用 `MSG_NOSIGNAL`（Linux），Windows 用 `#ifdef` 守衛。這樣 Linux_test 或未來新 main.cpp 即使忘記加 signal ignore 也不會中招。跨界改動標 `[跨界: user_lib]`，不阻塞我目前工作。

### → washrobot 實機

_（目前無）_

### → crane 實機

_（目前無）_

### → 前端

_（目前無）_

## 已處理

_（目前無）_
