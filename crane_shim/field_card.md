# 現場操作卡（shim 測試模式）

> 印一份帶到現場。每項打勾，fail 就停。

---

## 啟動（照順序）

```
□ 1. easy .5.26:  ./Crane_easy_PI                         → [OK] easy crane :5003
□ 2. crane .101:  pgrep Crane_control_PI (要空)
                   cd ~/washrobot_new_PI/crane_shim
                   python3 crane_shim.py --rate-down R --rate-up R
                                                          → [shim] ready :5002
□ 3. crane .101:  node ~/washrobot_web_backend/server.js
□ 4. robot .100:  ./washrobot_new_PI                      → [OK] crane OK / :5001
□ 5. 瀏覽器:      http://192.168.1.101:8080
                   → 3 顆 dot 全綠、banner 隱藏
```

---

## S1. shim 層（機器不上牆）

```
□ raw: ping                → OK shim_pong
□ raw: status              → OK weight=... shim_mode=1
□ raw: pay_out 10          → OK shim down=10cm duration=...  (CH7 on 3s)
□ raw: retract 10          → OK shim up=10cm ...              (CH8 on 3s)
□ raw: pay_out 20，立刻 stop → pay_out=ERR aborted / stop=OK
□ raw: home_status         → ERR shim_no_home_...
□ raw: roll_correct 5      → ERR shim_no_roll_correct
```

---

## S2. 校 rate（機器掛繩、離地 2m、繩鬆 30cm）

```
□ GUI easy ↓ 按住 10 秒 → 量下降 ___ cm → rate_down = ___ / 10
□ GUI easy ↑ 按住 10 秒 → 量上升 ___ cm → rate_up   = ___ / 10
□ Ctrl-C shim → 重啟帶新參數
□ 驗證: raw pay_out 30 → 實測下降 27~33 cm
```

---

## S3. 硬體 manual（地面或低架台）

```
真空:
  □ all OFF → all ~0 kPa
  □ feet ON → 腳 4 顆負壓、身體+中心 0
  □ body ON → 身體 4 顆負壓
  □ center ON → 中心負壓
  □ all OFF → 全歸 0

推桿:
  □ feet EXTEND / RETRACT
  □ body EXTEND / RETRACT
  □ center EXTEND / RETRACT

DM2J:
  □ left_foot 5 / right_foot 5 / arm 10
  □ 全送 cm=0 回零
```

---

## S4. init + attach + detach（低處玻璃貼合）

```
□ auto: init               → Idle → Ready、泵浦聲音、推桿伸出
□ 人工: 調位置讓上下身體推桿都觸玻璃
□ auto: attach             → Ready → Attached、9 顆 JC-100 全負壓
    失敗 → 調位置重按 attach
□ auto: detach             → Attached → Ready → Idle
```

---

## S5. 單步 step_down（離地 ≥ 1m、水管預放 ≥ 2m、2 人現場）

```
預置: Attached、🆘 鈕手邊

□ auto: step_down          → Attached → Stepping → Attached（1~2 分鐘）

全程盯:
  □ shim stderr 每個 pay_out/retract duration
  □ 9 顆 JC-100（attach 時重建）
  □ 實際下降 ~30 cm

通過: 狀態回 Attached、無 vacuum_fail / imu_emergency
穩定連 2 次 → 進 S6
```

---

## S6. 連續 run 3（下降 ≥ 1.5m、水管 ≥ 3m）

```
□ auto: run 3              → 自動跑 3 步
□ 盯累積誤差 + 水管鬆度
□ 跑完 Attached
通過: 總降 80~100 cm、水管未繃緊
```

---

## S7. 收工（手動召回 — 不按 GUI 召回按鈕）

```
□ auto: detach             → Ready
□ easy 重量變大 ✓
□ easy ↓ 按住 → 機器降下，快到地前 ~20 cm 短按放慢
□ 到地: 放開 → easy STOP
□ robot 終端: shutdown
```

---

## 緊急處置（隨時可用）

```
狀況                        動作
─────────────────────────────────────────────
washrobot 異常            → GUI: STOP (robot)
crane 異常                → GUI: STOP (crane)
機器懸空失控（robot 掛）   → GUI: 🆘 按住收繩（橘 banner 模式）
shim 死掉                 → pay_out 會 ERR → easy panel 手動接管
重量超標（繩快拉到底）     → easy 自動停 + EVT weight_limit（按鈕解除）
全機撤離                  → 頂樓 E-stop + 電箱切總電
```

---

## 數字備忘

```
rate_down (cm/s):  _______    (量於 ____/__/__)
rate_up   (cm/s):  _______
up_stop_kg (kg):   _______
機器總重 (kg):     _______
預估下降距離 (m):  _______
```

---

**發現任何異常 → 回報給 Sadie + 拍照/錄影 + 記錄 shim stderr**
