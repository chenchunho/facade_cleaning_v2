# crane_shim

**測試模式專用。** 讓 washrobot 跑自動 `run` / `step_down` 時，底下由
Crane_easy_PI（簡易吊車，:5003）代替主吊車（:5002）。

完整規範見 `.claude/easy_crane_test_mode.md`。

---

## 運作原理

washrobot 把 `pay_out 45` 送到 :5002。正常情況 `Crane_control_PI` 會處理；
測試模式下改由 `crane_shim.py` 接管，翻譯成：

```
easy  down on
(sleep  45 / rate_down  秒，期間每 500ms 送 ping 維持 easy watchdog)
easy  down off
easy  stop
reply OK
```

`retract` 對稱（`up on/off`）。其他主吊車指令多半 no-op 或回 ERR（見下表）。

---

## 啟動

**前置：**
1. Easy crane Pi (.5.26) 跑 `./Crane_easy_PI` — 監聽 :5003
2. **關掉** crane Pi (.101) 上的 `Crane_control_PI`（會搶 :5002）

**啟動 shim（在 crane Pi .101）：**

```bash
cd ~/washrobot_new_PI/crane_shim
python3 crane_shim.py --rate-down 3.0 --rate-up 3.0
```

可選參數：

| Flag | 預設 | 說明 |
|---|---|---|
| `--listen-host` | `0.0.0.0` | 監聽介面 |
| `--listen-port` | `5002` | 假裝主 crane |
| `--easy-host` | `192.168.5.26` | Crane_easy_PI IP |
| `--easy-port` | `5003` | Crane_easy_PI port |
| `--rate-down` | `3.0` | **放繩速率 cm/s — 實測後要改** |
| `--rate-up` | `3.0` | **收繩速率 cm/s — 實測後要改** |

Ctrl-C 正常關閉（會自動送 easy `stop`）。

---

## 校正放繩速率

**做一次就好，之後寫進啟動腳本。**

1. 機器掛在簡易吊車繩上、離地面一點距離
2. Web GUI easy crane panel 按住 **↓ 釋放繩** 10 秒
3. 量繩實際放了多少 cm（或目測下降距離）
4. `rate_down = 放出長度 / 10` cm/s
5. 對 **↑ 拉繩** 同樣量一次，得 `rate_up`

**備註：**
- 收 / 放速率可能不同（繩重 + 機器重量）— 分別量
- 誤差 ±20% 以內 shim 夠用，因為 `STEP_MARGIN_CM=15` 有餘裕
- 跑過一兩步之後可以量實際下降 vs washrobot 指令總和，微調 `rate_down`

---

## 指令對照

| 主吊車指令 | shim 行為 |
|---|---|
| `pay_out <cm>` | easy `down on` → 等 `cm/rate_down` 秒 → `down off` + `stop` → `OK` |
| `retract <cm>` | easy `up on` → 等 `cm/rate_up` 秒 → `up off` + `stop` → `OK` |
| `stop` / `emergency_stop` | 中止進行中的 motion + 轉發 easy `stop` |
| `ping` | `OK shim_pong`（不經 easy，shim 存活確認）|
| `status` | 轉發 easy `status`，回傳加 `shim_mode=1` |
| `pay_out_left`/`_right <on\|off>` | easy `down on/off`（易吊無左右分）|
| `retract_left`/`_right <on\|off>` | easy `up on/off` |
| `zero_meters ground\|top` | `OK shim_noop`（不計米，接受不處理）|
| `middle_set *` | `OK shim_noop` |
| `home_status` | **`ERR shim_no_home_use_manual_easy_crane`** — 擋召回按鈕 |
| `roll_correct <delta>` | **`ERR shim_no_roll_correct`** — Phase 5 平衡校正跳過 |
| 其他 | `ERR unknown_cmd` |

---

## 限制

- **精度開環估算**：距離換時間靠 `rate_*` 常數，無 encoder 回饋。每步誤差 ±10~20%。
  `STEP_CM=30` + `STEP_MARGIN_CM=15` 容忍範圍內，但連續多步會累積。
- **Phase 5 平衡校正跳過**：easy 吊車只有一條繩，無法做左右差動。`roll_correct` 直接 ERR。
- **Phase 6 自動召回不支援**：`home_status` 回 ERR；Web GUI 「↩ 召回」按鈕會失敗。
  召回要人工按 easy crane ↓ 按鈕下放到地面。
- **左右獨立手動鈕失真**：`pay_out_left` / `pay_out_right` 都 map 到同一條 easy 繩；
  左右差異無意義。
- **中間管線沒放線**：水管電線要**事先預放**足夠長度攤頂樓。超過預放長度會扯斷。

詳細安全清單見 `.claude/easy_crane_test_mode.md`。

---

## 故障排除

| 現象 | 原因 | 解法 |
|---|---|---|
| `FATAL bind :5002` | `Crane_control_PI` 還在跑 | `pkill Crane_control_PI` 後再啟 shim |
| `ERR easy_link_down ...` | Easy Pi (.5.26) 沒開 / 網段不通 | 確認 easy 程式啟動、ping `192.168.5.26` |
| motion 中 `ERR aborted` | 另一個 client 送了 `stop` | 查誰按下 STOP 按鈕 |
| 下降明顯不到 `pay_out` cm | `rate_down` 估太高 | 實測重新調；也可能繩卡住 |
| 下降比指令多 | `rate_down` 估太低 | 實測重新調 |

---

## 不要用 shim 的時機

- 正式 deploy（主 crane 硬體全上線）
- 超過 **1~3 公尺**距離的測試（中間管線拉扯風險高）
- 需要 Phase 5 / Phase 6 自動流程
- 無人監控
