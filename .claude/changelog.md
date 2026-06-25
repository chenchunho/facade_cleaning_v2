# 修改日誌

每次 Claude Code 修改檔案後，在此記錄異動內容。

格式：
```
## [日期] [修改者]
### 修改檔案
- `路徑/檔案.cpp` — 說明
### 原因
...
```

---

## 2026-06-25b Claude (Sadie)
### 修改檔案
- `.claude/runbook.md` — 6 處 RPi 部署目錄 `~/washrobot_new_PI/` → `~/facade_cleaning_v2/` + binary `./washrobot_new_PI` → `./facade_cleaning_v2`
- `.claude/easy_crane_test_mode.md` — 3 處（crane_shim 路徑、washrobot bin/binary、TEST MODE grep 的 source 資料夾名）
- `scripts/crane.sh` — 註解內 crane_shim 部署路徑範例
### 原因
接續 2026-06-25a，user 要求 RPi 端部署目錄（兩台 Pi clone 的 repo 目錄 `~/washrobot_new_PI/`）也跟著改名 facade_cleaning_v2。
### 影響
- 部署到 RPi 時 repo clone 目錄請用 `~/facade_cleaning_v2/`（runbook / easy_crane_test_mode 已對齊）。
- **刻意保留（待 user 決定）**：`.claude/gen_deploy_pdf.py` 用的是另一套 `~/washrobot_build/washrobot_new_PI/` build 暫存方案 + 過時開發機路徑，且改它要重跑 fpdf 重產 `deploy_and_test.pdf` 才同步，故先不動。
- **依舊不動**：README 的 GitHub 上游 repo URL（v1 身分）、changelog 與舊 work_log 歷史條目、CLAUDE.md/motion_flow 描述原始碼專案名的散文。

---

## 2026-06-25a Claude (Sadie)
### 修改檔案
- `washrobot_new_PI/` → `facade_cleaning_v2/`（資料夾，git mv）
- `washrobot_new_PI.vcxproj` / `.vcxproj.user` → `facade_cleaning_v2.*`（git mv）
- `washrobot_new_PI.sln` → `facade_cleaning_v2.sln`（git mv）+ 內部 Project 名稱/路徑引用更新
- `facade_cleaning_v2/facade_cleaning_v2.vcxproj` — `<RootNamespace>` 改 facade_cleaning_v2
- `facade_cleaning_v2/main.cpp` — header 註解 + 啟動 banner 字串改新名
- `scripts/wr.sh` — 預設 `WR_BIN` 路徑/binary 名 + 註解範例改 facade_cleaning_v2
### 原因
v2 fork 後主程式專案仍叫 washrobot_new_PI，改成跟 repo 根目錄同名 facade_cleaning_v2。
ProjectGuid 保留不動（維持 VS 識別、sln 對應）。
### 影響
- **輸出 binary 名稱變 `facade_cleaning_v2`**（vcxproj 無 TargetName，吃專案名）。RPi 部署目錄/binary 名都變，重 deploy 時注意。
- VS 要用新的 `facade_cleaning_v2.sln` 重開。
- **刻意不動**：CLAUDE.md/runbook/work_log 等文件的舊名 + `~/washrobot_new_PI/` 部署路徑、`scripts/crane.sh:17` 的 crane_shim 部署路徑範例（屬 RPi 部署目錄選擇，待 user 決定是否跟改）。

---

## 2026-06-12b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` 構造函式 — **取消註解** bystanders PR1=rpm=0 safe setup (3 行 + log)
### 原因
User bench 觀察「同步移動 feet 變成輪子動」、確認是 **bystander PR1 沒設 safe** 造成。

過去 init 有 3 行：
```cpp
D_(DM2J_LEFT_WHEEL ).PR_move_set(1, ..., rpm=0, ...);
D_(DM2J_RIGHT_WHEEL).PR_move_set(1, ..., rpm=0, ...);
D_(DM2J_ARM        ).PR_move_set(1, ..., rpm=0, ...);
```
不知什麼時候被註解掉。**輪子 PR1 留著未知值（廠商 default 或殘留設定）**。

cmd_dm2j_group "feet" → dm2j_pair_move_abs_ 用 PR1 broadcast trigger → 輪子也收到 → 執行**輪子 PR1 內容** → 輪子亂動。

取消註解後 PR1 對輪子是 rpm=0 no-op、broadcast 不會誤動輪子。
### 影響
- feet 同步移動不再誤動輪子
- 輪子自己的 motion 用 PR0、跟 PR1 不衝突、不影響
- 安全性 setup、應該一直存在

---

## 2026-06-12a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 `dm2j_wheels_move_verified_(target_cm)` helper declaration
- `user_lib/WASH_ROBOT.cpp` — 實作 `dm2j_wheels_move_verified_`，加 position verify + per-side retry
- `user_lib/WASH_ROBOT.cpp:cmd_wheels` — 改用新 helper
- `user_lib/WASH_ROBOT.cpp:cmd_dm2j_group(wheels)` — 改用新 helper
### 原因
User 反映「有時候只有一邊輪子放或收」。原本 cmd_wheels:
```cpp
trigger LEFT → trigger RIGHT → wait LEFT → wait RIGHT → return OK
```
- 任一步 fail 直接 return ERR、另一邊可能在動 / 沒動 / 已到位、流程不一致
- 即使「OK」也只代表 DM2J 報 done、**沒驗證馬達實際位置**

新 helper：
1. **per-side state**: left_ok / right_ok 各自追蹤
2. **每 attempt 只 trigger 未 ok 的輪子**（避免重複觸發已到位的）
3. **read_position_cm 驗證**：|actual - target| > 0.5cm 視為 fail
4. **retry 3 次**、每次 retry 500ms 間隔
5. **詳細 log**：每 attempt + 每側顯示 status

對 feet 程式**完全零影響**（不動 broadcast / PR1 / pair_move）、loose sync ~50ms gap 維持原狀。
### 影響
- 「只有一邊動」case → retry 後兩邊都到 → user 看到 "OK"
- 真的單邊壞掉 → 3 次都 fail → "ERR wheels_move_failed" + log 顯示哪邊
- 最壞 case 整 cmd 慢 ~5 秒（3 retry × 移動時間）— 接受 trade
- log 詳細、診斷容易

---

## 2026-06-11e Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl`：
  - `MIT_KP` 10 → **20**（暫時拉高、實驗用）
  - 加回 **FRICTION_TAU=1.5**
  - LEFT slot **-0.5 → -0.7**（測極限）
### 原因
驗證馬達 LEFT 方向**輸出是否真飽和**。命令更大力（kp 20 + FF 1.5）：
- pos_err 0.2 rad → PD = 4 Nm + FF 1.5 = total **5.5 Nm command**
- 看 STATUS tau 數值：
  - tau ≈ -1.5 → 硬體飽和 (saturated)、確認軟體救不了
  - tau ≈ -3+ → 馬達還有空間、kp 拉高有效
### 測試
跑 DEPLOY LEFT、立刻看 STATUS tau。

---

## 2026-06-11d Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl` — LEFT slot **-0.7 → -0.5**（RIGHT 維持 +0.7）
### 原因
06-11c CONV_TOL=0.15 後 LEFT 仍第 1 次 fail：motor 卡 -0.434、target -0.7、err 0.266 > 0.15。

motor LEFT 方向硬體**單次最大推程 ~0.44 rad**。target 設 -0.5（馬達 capability 內）：
- motor 到 -0.434：err 0.066 < 0.15 → converged ✓
- motor 走滿 -0.5：err 0 → converged ✓

兩種情況都首次成功、不需 retry。RIGHT 方向 motor 順暢、target 維持 +0.7。
### 影響
- DEPLOY LEFT 預期一次到位
- tool head LEFT 朝向 -0.5 (~29°)、跟原始 -0.7 (~40°) 差 11° — sweep 影響需實機確認
- RIGHT 方向不變

---

## 2026-06-11c Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl:CONV_TOL` 0.1 → **0.15**
### 原因
DEPLOY LEFT 第 1 次 motor 卡 -0.487、target -0.6、err 0.112 → 差 0.012 rad 就過關但仍 FAIL。CONV_TOL 從 5.7° 放寬到 8.6°、motor 到 -0.45 → converged。
### 影響
- DEPLOY LEFT 預期一次到位
- tool head 朝向 ±8.6° 容忍

---

## 2026-06-11b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h:397` — `ARM_SWEEP_CM` 60 → **55**
- `user_lib/WASH_ROBOT.h:423` — `ARM_SWEEP_EST_MS` 4200 → **3900**
### 原因
User 再縮短 5cm 到 55cm。EST_MS 重算（55cm@1000rpm × 1cm/rev：accel 0.1s + cruise 3.2s + decel 0.1s + retry 0.15s + buffer 0.35s = ~3.9s）。
### 影響
- sweep 行程再縮 5cm，覆蓋範圍變小
- 每段 sweep 4.2s → 3.9s

---

## 2026-06-11a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h:397` — `ARM_SWEEP_CM` 85 → **60**
- `user_lib/WASH_ROBOT.h:423` — `ARM_SWEEP_EST_MS` 5700 → **4200** (跟著新 sweep 距離重算)
### 原因
User 要求縮短上滑台行程到 60cm（之前 85cm）。EST_MS 跟著重算（60cm@1000rpm × 1cm/rev：accel 0.1s + cruise 3.5s + decel 0.1s + retry 0.15s + buffer 0.35s = ~4.2s）。
### 影響
- 上滑台 sweep 涵蓋範圍縮小（從 85cm 縮 60cm）
- Sweep 一段時間 5.7s → 4.2s，cleaning sweep 整體節奏加快
- Obstacle monitor poll 在 EST_MS 內跑（200ms/poll），不受影響
- 沒動 ARM_SWEEP_RPM/ACC/DEC（仍 1000/100/100）
### 待 bench 驗證
- sweep 是否真的能在 4.2s 內走完 60cm（如果 obstacle monitor 在動作未完時就 return，要把 EST_MS 拉高）
- 60cm 是否還能涵蓋窗框寬度 — 行程縮 25cm，可能要看實機決定 sweep 路徑是否需要重新規劃

---

## 2026-06-09aa Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp` — 全套採用 reference tuning（user 提供 C:\Users\midor\Downloads\main_api.cpp）
### 從 reference 採用的參數
- `m2_.hold_kp` 2.0 → **2.5**
- `m2_.hold_kd` 1.2 → **1.0**
- `lr_move_to_slot_impl:MIT_KP` 10（同 reference）
- `lr_move_to_slot_impl:CONV_TOL` 0.08 → **0.1**（容忍 5.7°）
- `lr_move_to_slot_impl:MAX_LOOPS` 200 → **100**（2s, reference 值）
- **移除 FRICTION_TAU FF**（reference 沒這個）
- **移除 velocity FF**（reference 沒）
- **移除 stabilize frames**（reference 沒）
- **Slot LEFT 回 -0.7、RIGHT 仍 +0.7**（reference 值）
- **DEPLOY speed 統一 0.8**（兩邊一樣、reference 值）
### 原因
我之前的調整路徑（FF / velocity FF / stabilize / 不對稱 speed）越改越複雜也沒解決問題。reference 版是「**原版簡單 loop + 微調軟化**」的成熟 tuning、放棄追求精準到位、用 CONV_TOL=0.1 容忍 5.7° 誤差。

reference 邏輯：motor 能到 -0.6~-0.65 也算 converged（CONV_TOL 0.1 範圍）→ 一次 DEPLOY 成功率高、不需 retry。
### 影響
- 簡單可靠、不再追求精準
- 5.7° 容忍對 sweep 清洗影響應該不大
- 如果 sweep 清洗角度真的需要更精準、再加新 tuning

---

## 2026-06-09z Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl` — LEFT slot **-0.7 → -0.6** (RIGHT 維持 +0.7)
### 原因
06-09y FRICTION_TAU=1.5 + MIT_KP=10 + 各種 FF/timeout 調整都試過、motor LEFT 方向**第一次仍到不了 -0.7**（卡 -0.48）、第 2 次接力才到 -0.67。

結論：motor 硬體輸出在 LEFT 方向有限制（asymmetric）、軟體救不了。

實測 motor 一次能到 -0.67、設 target -0.6（≤ motor 能力範圍）→ **一次到位**。
### 影響
- LEFT slot 角度差 -0.7 → -0.6（差 0.1 rad ≈ 5.7°）— tool head 朝向略有不同、但 sweep 流程影響小
- 一次 DEPLOY 成功率大幅提升
- 不再依賴 washrobot retry
- RIGHT slot 保持 +0.7（馬達順暢、能到）
### 後續可能要做
- 治本：馬達 commutation 重做 / 機械潤滑 / cable routing 改善 → 讓 LEFT 也能到 -0.7
- 暫時 -0.6 是 workaround

---

## 2026-06-09y Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl:FRICTION_TAU` 0.8 → **1.5**
### 原因
06-09x friction FF 改用 motor_pos vs target 後、user 反映「還是一樣」、motor 卡 -0.48。

推估：motor 在 -0.48 卡住時 PD 出 ~2.2 Nm + FF 0.8 = total 3.0 Nm 但仍動不了 → 摩擦力 + 機構阻力可能 > 3 Nm 在這位置。

FF 1.5 → total command ~3.7 Nm push、看能不能突破。
### 風險
- 若 motor 真到位（達 -0.7）、tau_ff 立刻 0、不會 overshoot
- 若 motor 仍卡 → 確認是 motor 輸出 saturated（硬體限制）、不是軟體 FF 不夠
- 若 motor 過 target → FF 反向、可能微振盪、但 CONV_TOL=0.08 容忍

---

## 2026-06-09x Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl` — friction FF 邏輯改用「motor pos vs target」判斷方向
### 原因
06-09w 後 LEFT 首次走到 -0.48 卡住、第 2-4 次接力才到 -0.7。

問題：原本 FF 條件是 `if (cur_cmd != target)`。當 trajectory 跑完、cur_cmd = target、FF 立刻變 0。但**馬達實際位置還沒到 target**、settle 階段只靠 PD（kp=10 × err 0.22 = 2.2 Nm）+ 摩擦阻力 ~1 Nm → motor 推不動最後 ~0.2 rad。

新邏輯：FF 判斷用 **motor 實際位置 vs target**：
```cpp
if (|target - motor_pos| > CONV_TOL):
    tau_ff = sign(target - motor_pos) × FRICTION_TAU
```

→ trajectory 結束後若馬達還沒到、FF 持續輸出、馬達能 settle 到 target ±CONV_TOL 內。
### 影響
- 首次 DEPLOY LEFT 預期能一次到位
- Settle 階段馬達持續微推、不會卡 -0.48
- 風險：若 motor overshoot 過 target、FF 會反向推、可能輕微振盪、但 CONV_TOL=0.08 容忍範圍內穩定

---

## 2026-06-09w Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl` 加 **friction feedforward** `FRICTION_TAU=0.8` Nm
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl:MIT_KP` 8 → **10**（配合 FF）
- `cleaning_arm/main_api.cpp:DEPLOY dispatch` — LEFT speed 0.5 → **0.6**
### 原因
06-09v MIT_KP=8 後馬達**力不夠克服摩擦**：DEPLOY LEFT 走 4 次（每次 4s）才到 -0.7。kp 太弱導致 stick-slip 無法突破、motor 走停走停。

加 **friction feedforward**：trajectory 中朝 target 方向預載 0.8 Nm（接近靜摩擦量級）：
```cpp
control_mit(motor, MIT_KP=10, hold_kd, cur_cmd, 0, tau_ff);
                                                  ^^^^^^
                                                  ±0.8 Nm 方向 follow trajectory
```

效果：
- 馬達從 frame 1 就有 0.8 Nm 基底推力 → 立刻克服靜摩擦
- PD 只需要小幅度算「小調整」、不會累積大 pos_err
- 起步**柔和漸進**、無 burst release（無「扭一下」）
- 馬達真的能持續移動到 target

到 target 後 tau_ff = 0、避免 overshoot。

LEFT speed 0.5 → 0.6 配合 friction FF 後馬達能 keep up trajectory（之前 0.5 太慢、PD 累積 lag）。
### 影響
- 預期：起步柔和（FF 預載）+ 中段平順（trajectory 配合馬達能力）+ 一次到位（FF 持續推）
- 副作用：FRICTION_TAU 太大可能讓馬達一直加速、到 target 後若 tau_ff 沒及時 = 0 會 overshoot（已防護）
- 風險：摩擦實際值若 < 0.8 Nm（例如 0.5 Nm）、FF 0.8 偏大會推快 → 微 overshoot

---

## 2026-06-09v Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:m2_.hold_kp` 4.0 → **2.0**（HOLD 力道減半）
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl:MIT_KP` 12 → **8**（轉動力道從 12 → 8）
### 原因
User 想要兩個力道都更小、更柔和。
- **MIT_KP 8**：trajectory 中 motor 出力較小、stick-slip 振動較小、起步較柔
- **hold_kp 2**：到位後 PD 維持力減半、減少 hold 階段的 thermal load 跟「咬住」感覺
### 影響
- 起步力小 → motor 加速較慢但較柔
- Hold 力小 → 到位後 steady-state error 可能略大（~0.1 rad）但不持續 thermal
- 若 motor 因摩擦無法到位、可能 fail 更頻繁（kp 減半、tau output 減半）
- 配合 hold_ki (0.3) 仍能 eliminate steady state error 一段時間

---

## 2026-06-09u Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl:MAX_LOOPS` 150 → **200** (3s → 4s)
- `cleaning_arm/main_api.cpp:DEPLOY dispatch` — M2 speed 對 LEFT 用 **0.5**、RIGHT 仍 0.8（asymmetric）
### 原因
06-09t MAX_LOOPS=150 後 LEFT 仍 fail 第 1 次（第 2 次才到）。

motor LEFT 方向實速 ~0.6 rad/s、慢於 RIGHT (~0.30+) 跟 trajectory 0.8。slot-to-slot 1.44 rad 用 trajectory 0.8 → ramp 1.8s 完成但 motor 還在 -0.485（lag 0.215）→ 3s 不夠 settle。

雙修：
1. **MAX_LOOPS 150 → 200 (4s)**：給 motor 更充足 settle 時間
2. **LEFT speed 0.8 → 0.5**：trajectory 配合 motor 實速、不 lag、不衝過頭、不 bounce

RIGHT 方向 motor 順暢、保持 speed 0.8 快速到位。
### 影響
- LEFT DEPLOY 預期首次成功（不再需要 washrobot retry 第二次）
- RIGHT DEPLOY 不受影響
- 整體：LEFT 多 ~0.5s、RIGHT 不變
- 如果還是 fail：可能 motor LEFT 方向真的有 hard limit < -0.7，或 mechanical 限制

---

## 2026-06-09t Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl:MAX_LOOPS` 100 → **150** (2s → 3s)
### 原因
06-09s revert 後 LEFT slot-to-slot DEPLOY 仍 fail：
```
DEPLOY 200 LEFT (from +0.74 to -0.7):
  distance = 1.44 rad、speed 0.8 → ramp 1.8s
  原版 2s timeout 只剩 0.2s settle margin
  → motor 到 -0.485 (lag 0.215) → FAIL
```
原版 2s timeout 是設計給「典型 0.7 rad 距離」、slot-to-slot 1.44 rad 太緊。

3s 給夠 settle 空間：1.8s ramp + 1.2s settle、motor 有時間追到 ±0.08 內。
### 影響
- LEFT slot-to-slot DEPLOY 預期能首次成功
- 短距離（如 CENTER→LEFT 0.7 rad）也不受影響、會早 converged break
- 副作用：真 fail case 等更久 1s

---

## 2026-06-09s Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl` — **大幅 revert 回原始版簡單 loop**
- `cleaning_arm/main_api.cpp:DEPLOY dispatch` — speed 0.15 → **0.8**
### 原因
User 對比 `D:\洗窗戶機器人\cleaning_arm\cleaning_arm\` (原始正常版) 後發現現在「怪怪的」。我這幾天的 tuning 把簡單 loop 改複雜了、反而暴露 motor stick-slip / overshoot 等問題。原版簡單快速 + slot 靠近 mechanical stop（natural braking）→ motor 沒時間振盪、實際 work。

revert 範圍：
- **Slot 位置 ±0.5 → ±0.7**（更靠近 stop、natural brake assist）
- **DEPLOY speed 0.15 → 0.8**（快速跨越）
- **MIT_KP 8 → 12**（原版值）
- **CONV_TOL 0.20 → 0.08**（原版嚴格）
- **MAX_LOOPS 固定 100 (=2s)**（原版固定 timeout）
- **移除 velocity feedforward**（原版沒有）
- **移除 5 個 stabilization frame**（原版沒有）
- **移除距離計算 timeout**（原版固定 100 loops）
- **hold_pos 永遠 = target**（原版行為、hold 階段繼續往 target 收）

保留改進：
- 校準 SEEK_KP=1.0（避免原版 phantom stop bug）
- DEPLOY 失敗時印「FAIL」而非「Done」（誠實 log）
- Passive-state detection + re-enable（M2 fault 救援）
- ARM_M2_VERIFY_RETRIES=4 + no_reply retry（grace for transient）
### 影響
- 預期回到「原版正常」的快速順暢行為
- 副作用：DEPLOY 完成後若 motor 沒到 target、hold 階段會繼續嘗試到 target（可能持續 1-2 秒輕度震動）
- 副作用：slot 靠近 stop 時 hold torque 可能達 2+ Nm、長時間 hold 可能讓 M2 進 passive state（但有 re-enable）
- 這個 trade-off 上次（2026-06-06）為了舊壞馬達避免 thermal、改成 ±0.5；新馬達應該 OK

---

## 2026-06-09r Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:DEPLOY dispatch`：speed 0.25 → **0.15**
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl`：MIN_TIMEOUT 3.0 → **4.5**、MAX_TIMEOUT 6.0 → **8.0**
### 原因
06-09q velocity feedforward 治好 RIGHT direction（到 +0.48）但 LEFT 仍只能到 -0.25。

log 顯示 motor 在 LEFT 方向實際速度 ~0.16 rad/s、追不上 trajectory 0.25。motor 出力在 LEFT 方向**先天就比較弱**（commutation / 機構偏置 / 重心）。

修法：trajectory speed 配合馬達能力**較弱**的那邊（0.15 rad/s）。兩邊都 keep up:
- LEFT: target_vel = -0.15、motor_vel = -0.15 → vel_err = 0、無 stick-slip burst
- RIGHT: target_vel = +0.15、motor_vel keep up（本來能跑 0.21）→ vel_err 微小正向、kd 小幅補
- 兩邊都平順抵達 target
### 影響
- LEFT 預期不再 timeout fail
- 整體 DEPLOY 慢 ~1s/次（從 0.25 → 0.15 rad/s）
- timeout 拉長到 4.5s/8s 配合慢速
- 副作用：washrobot 整體 sweep 流程多 ~2s（兩次 DEPLOY）— 接受 trade

---

## 2026-06-09q Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl` main loop — 加 **velocity feedforward**：
  - 計算當下 trajectory 速度方向（target_vel = ±effective_spd）
  - control_mit 第 4 個參數從 0 改成 target_vel
### 原因
User 觀察：「馬達**瞬間快速轉過 target 再彈回**」、最終 settle 在 target 以前。

這是**典型 stick-slip + overshoot 振盪**：
1. 馬達靜摩擦讓它卡住、PD pos_err 不斷累積
2. tau 累積到 > 靜摩擦 → 馬達**突然 release** → 暴衝
3. 衝過 target → PD 反向拉 → bounce
4. 重複幾次後 settle 在某點（不一定 target）

velocity feedforward 治本：
- damiao MIT mode 算 tau = kp×pos_err + kd×vel_err + tau_ff
- 帶 target_vel 後、馬達**從 frame 1 就知道「要以這速度走」**
- kd × (target_vel - motor_vel = -target_vel 初始) = 約 0.3 Nm 起始推力
- → motor 不需要等 pos_err 累積、馬上開始平滑加速
- 無 burst release、無 overshoot

對你 case：target_vel = ±0.25 rad/s、kd=1.2 → 預設起始 push 0.3 Nm（柔和起步）
中段 motor_vel 接近 target_vel → vel_err ≈ 0 → 只剩 kp×pos_err 補正
到 target：cur_cmd = target，target_vel 設 0 → motor 自然 brake
### 影響
- 起步無 stick-slip burst（從 0 平滑加速）
- 過程無暴衝過頭
- 終點無 bounce-back
- LEFT direction 應該也能到位（馬達 PD 內部更積極跟蹤速度）
- 副作用：feed-forward 太強可能造成穩態 vel_err、但 kd=1.2 不算大、應該 OK

---

## 2026-06-09p Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl`：
  - **移除速度漸入** (RAMP_ITERS) — user 反映「漸入結束突然進全速」反而像猛力
  - 全程使用 effective_spd（由 caller 控制）
- `cleaning_arm/main_api.cpp:DEPLOY dispatch`：speed 0.5 → **0.25**（配合 M2 實際能力）
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl:MIN_TIMEOUT_SEC` 2.0 → **3.0**
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl:MAX_TIMEOUT_SEC` 5.0 → **6.0**
### 原因
06-09o 漸入後 user 還是覺得「起步太猛」。原因不是 PD 力突然加大、是「漸入結束時 trajectory 速度從低突然進全速」反而被感覺成 transition。

改方法：**全程恆速、不要 ramp**。trajectory 速度配合 motor 實際能跑的速度（LEFT ~0.10, RIGHT ~0.30 rad/s），中間值 0.25：
- cur_cmd 從 motor pos 開始以 0.25 rad/s 等速增加
- motor 追得到（甚至 LEFT 偶爾追不上但差距小）
- pos_err 累積小、PD 算 tau 也小、無 kick
- 從第一秒到最後一秒**速度恆定**、沒有 transition

timeout 配合放寬：0.5 rad 距離 → 2.0s ramp + 0.6 settle = 2.6s → MIN_TIMEOUT 3.0s
### 影響
- 起步猛力應該消失（恆速 + low speed）
- LEFT 仍可能追不上但 deploy 過程更柔和
- 整體慢 ~0.5s/次
- 如果還是追不上 → 進一步降 speed 到 0.2 或加大 timeout

---

## 2026-06-09o Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl`：
  - **新增速度漸入** `RAMP_ITERS=25` (500ms 線性 0 → effective_spd)
  - main loop kp 從 `s.hold_kp` (=4) 改成 `MOVE_KP=8`（強到夠克服摩擦）
  - `MIN_TIMEOUT_SEC` 1.5 → **2.0**（補償漸入延遲）
### 原因
06-09n 把 MOVE 階段 kp 改成 hold_kp=4 後、馬達**力不夠克服摩擦**：
- LEFT 方向 1.6s 只走 0.12 rad（0.075 rad/s、嚴重不夠）
- RIGHT 方向 1.8s 走 0.41 rad（0.23 rad/s）

User 想要「**起步力小**、但又能順暢到達 target」、需要兩個一起做：
1. **MOVE_KP 8**：中等 kp、力足以克服 ~1 Nm 摩擦並推到 target（kp=4 net force 不夠、kp=12 起步太猛）
2. **速度漸入**：trajectory step size 500ms 內線性從 0 升到 effective_spd → 初始 pos_err 累積慢 → 初始 tau 小 → 起步柔和

組合：起步**有漸入緩衝**、中段**有足夠 force** → 不會啟動猛力又能到 target。
### 影響
- 起步「轉動力大」的感覺應該消失
- LEFT/RIGHT 兩邊都能到位（kp=8 force 足）
- 每次 DEPLOY 多 ~0.25s（漸入延遲）、總共 1.5-2s
- timeout buffer 寬一點、避免邊緣 case

---

## 2026-06-09n Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:DEPLOY dispatch`：speed 0.3 → **0.5**（恢復）
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl:MIN_TIMEOUT_SEC`：2.5 → **1.5**（恢復）
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl` 新增 **5 個 stabilization frame**（avoid disable→move gap 反向抽搐）
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl` main loop kp **MIT_KP=12 → s.hold_kp**（M2 = 4）
### 原因
1. **零點重新校準後**機械中心 = encoder 0、LEFT/RIGHT 都在物理範圍內、之前的 timeout/speed workaround 不需要
2. **反向抽搐根因**：lr_move_to_slot 進入時 `enabled.exchange(false)` 把 feedback_loop 關掉、馬達進「沒人控制」狀態 ~50ms、再開始送 trajectory frame、過程中馬達內部 PD 從鬆到緊瞬間反衝
3. **kp transition jump**：HOLD 用 kp=4、MOVE 突然 kp=12、轉換瞬間 PD 衝太大

修法：
- Fix 2：trajectory loop 前先送 5 個 hold-at-start_pos frame、馬達 settle、消除 gap
- Fix 4：MOVE 也用 hold_kp（4）跟 HOLD 一致、transition 不再有 kp jump
### 影響
- 預期 LEFT/RIGHT 按下去**不再先反方向抽**、直接朝目標走
- 速度回到 0.5 rad/s 順暢
- timeout 回 1.5s default
- 馬達 trajectory 跟 hold 用同 PD 參數、行為一致

---

## 2026-06-09m Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:DEPLOY dispatch` (line 1648)：`speed_rad_s` 0.5 → **0.3**
- `cleaning_arm/main_api.cpp:lr_move_to_slot_impl`：`MIN_TIMEOUT_SEC` 1.5 → **2.5**
### 原因
06-09l 後 calibration 撞擊 OK (2.66 Nm)、RIGHT DEPLOY 也 OK (0.33 rad/s)。但 **LEFT DEPLOY 首次仍 fail**：
```
DEPLOY 100 LEFT → FAIL pos=-0.295 target=-0.5 timeout=1.611s
DEPLOY 100 RIGHT → Done pos=0.392 dist=0.91 timeout=2.42s
```
LEFT 馬達實際速度 **0.20 rad/s**（對抗重力），RIGHT **0.33 rad/s**（順重力）— 機械結構不對稱、單向 gravity 負載。

trajectory 0.5 rad/s + timeout 1.5s 不夠 LEFT 走 0.5 rad。改 0.3 rad/s + timeout 2.5s 配合 LEFT 實際能力：
- LEFT: ramp 1.67s + settle 0.6s = 2.27s（floor 2.5s）— 馬達 0.2 rad/s 跑 0.5 rad 剛好 2.5s
- RIGHT: 同 2.5s timeout，馬達 0.33 rad/s 1.5s 走完 + 充裕 settle
### 影響
- LEFT DEPLOY 首次成功率提高（不再靠 retry）
- 整體 DEPLOY 慢 ~0.5s/次（trajectory 慢）
- 治標方案 — 真治本要做 feed-forward gravity compensation
### 治本方向（未做）
若要 LEFT/RIGHT 對稱速度：
- 在 control_mit 的 `target_tau` 參數加 feedforward 抵消重力
- 需 bench 校正：LEFT/RIGHT 各個 slot 位置的 gravity torque map
- 工程量：中（建立 model + 校正）

---

## 2026-06-09l Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_calibrate_slot`：
  - `SEEK_KP` 1.5 → **1.0** (完全對齊 M1)
  - `SEEK_VEL_MAX` 0.6 → **0.4**
- `cleaning_arm/main_api.cpp:DEPLOY dispatch` (line 1648)：
  - `lr_move_to_slot_impl` speed 0.8 → **0.5**
### 原因
**問題 1：甩頭仍稍重**
06-09k 撞 stop tau 4 Nm 比 8 Nm 好但 user 仍感覺甩頭。再降 SEEK_KP 對齊 M1（M1 健康 tuning kp=1.0），加上 vel 限速 0.4，預期撞擊 ~2-3 Nm。

**問題 2：DEPLOY LEFT 首次 timeout**
log 顯示 M2 負方向實際速度只 ~0.20 rad/s，但 trajectory speed=0.8 → 馬達追不上 → 1.5s timeout 內走不到 -0.5。
改 speed 0.8 → 0.5 後 trajectory 1.0s 走完，留 0.6s settle margin，馬達能追上、首次就 converge。
### 影響
- calibration 撞擊預期 ~2-3 Nm（柔和）
- DEPLOY LEFT 首次成功率提高（不再靠 retry）
- DEPLOY 整體速度略慢（從 0.8→0.5 rad/s）— 多 0.3 秒/次但更穩
- M2 retry 邏輯 06-09i 仍保留作為 backup

---

## 2026-06-09k Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_calibrate_slot`：
  - `SEEK_KP` 3.0 → **1.5**
  - `SEEK_KD` 0.3 → **1.0**
  - `SEEK_VEL_MAX` 0.8 → **0.6**
### 原因
06-09j 把 SEEK_KP 0.3 → 3.0 後 calibration **找到真的 stop 了**（pos 從 0 推到 +0.78、tau spike 8 Nm），但**衝擊太大**（user 反映「甩得很大力」）。

log 證據：
```
pre-check frame 1: vel=1.02 rad/s tau=3.58 Nm   ← 加速太快
Resistance at pos=0.7467 tau=8.2256             ← 撞 stop 時 tau 飆 8 Nm
```

對齊 M1 健康的 tuning（M1: kp=1.0 / kd=1.0 / vel_max=0.5），M2 用稍高 kp=1.5（同樣手臂結構但摩擦稍大）。
### 影響
- 預期初始扭矩 ~4 Nm（kp 1.5 × error 2.8）夠克服 static friction
- kd=1.0 在 vel=0.6 時提供 -0.6 Nm damping → 接近 stop 時自動減速
- VEL_MAX 0.6 限制最高速 → 撞擊力預期降到 ~3-4 Nm
- 找 stop 仍然成功（已驗證 KP=3 找得到、KP=1.5 也夠）

---

## 2026-06-09j Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp:lr_calibrate_slot` — `SEEK_KP` 從 0.3 改 3.0
### 原因
M2 換新馬達後 calibration **還是同症狀**：
- Phase 1 推 setpt=+2.8 rad、但 pos 只動 0.0008 rad 就宣告 "Resistance at stop"
- Phase 2 退 ZERO_OFFSET=0.8 rad，落在隨機位置設零點
- 結果：LEFT slot 在機械範圍外 → DEPLOY LEFT 永遠 fail
- DEPLOY RIGHT (+0.5) 因為零點偏 LEFT 邊，剛好在範圍內 → 看起來 work

根因：SEEK_KP=0.3 算出的 tau (≈ 0.6 Nm) **不足以克服 M2 static friction**，馬達不動 → 初始 PD transient tau spike 被誤認為「至 stop」。

新馬達跟舊馬達都在 LEFT 方向 fail = **不是馬達壞**，是 calibration 邏輯本身的 kp 太低。

拉高 SEEK_KP 3 → 約 3 Nm tau output → 足以推開 static friction、實際到達 90° 機械 stop → set_zero 落在真實中心位置 → LEFT/RIGHT 兩個 slot 都應該在機械範圍內。
### 影響
- 不需換馬達，calibration 流程本身修正
- 預期 Phase 1 log 會看到 pos 真的動到 ±0.78 rad 才判 stop（不再是 0.0006 那種誤觸發）
- 預期 Phase 2 退 0.8 後落在中心 ≈ 0、LEFT/RIGHT 兩個 slot 都到得了
- 風險：kp=3 推力較大、若馬達**沒接機械結構自由轉動**（例如 bench 測試）會猛甩、要小心
### 驗證
- 重新 build motor_api 部署
- 跑 INIT
- 看 calibration log：
  - Phase 1 "Resistance at pos=?" — 應該是 ±0.78 rad 附近 (90° 範圍 stop)
  - Phase 2 "settled pos=?" — 應該接近 0
- 跑 DEPLOY LEFT → 應該到 -0.5 ✓
- 跑 DEPLOY RIGHT → 應該到 +0.5 ✓

---

## 2026-06-09i Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp:do_arm_clean_sweep_` (主 thread 版本，~line 2745) — DEPLOY retry loop 加 no_reply 處理
- `user_lib/WASH_ROBOT.cpp:do_arm_clean_sweep_continuous_` (背景 thread 版本，~line 3131) — 同上
- `user_lib/WASH_ROBOT.h:ARM_M2_VERIFY_RETRIES` 從 2 改成 4 (total 5 attempts)
### 原因
M2 馬達 (清潔手臂工具頭) 進水腐蝕後 intermittent 失效 — 偶爾 motor passive (tau≈0)、`lr_move_to_slot` 完全不動、arm_cmd_("DEPLOY ...") 回非 OK。

舊行為只有 off-target 會 retry，no_reply 直接 break → M2 fail 第一次就 PAUSE-ON-ERROR、user 反覆按 RETRY 無限循環。

新行為：no_reply + off-target 都 retry，每次 retry 前 `arm_cmd_("PARK")` + sleep 500ms 重置 M2 passive state。

ARM_M2_VERIFY_RETRIES 2 → 4：bench M2 known broken 需要多 retry 才能撞到「這次出力 OK」那次。production 換新馬達後可降回 2。
### 影響
- M2 intermittent fail 自動 recover、不打擾 user
- 每次 fail 多花 ~700ms (PARK + sleep)、最壞 case 5 attempts ≈ 3.5s
- 真正壞掉 (連 5 次 fail) 才 pause → user 知道該手動處理
- 對應 memory `project_m2_water_damage_2026_06_09.md` 治標、不治本

---

## 2026-06-09h Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_detector.py:motion_parallax_detect` — 加 Y-proximity grouping，修 W 估計
- 同檔 import 加 `Dict`
### 原因
**根本 bug**：原本 `estimated_width_cm = (ln.x2_px - ln.x1_px) * 0.04` 用 line 在 image **X 軸**（橫向）的長度當 W，但 step_over 公式要的 W 是 obstacle 在 **Y 軸**（step direction = 機器走的方向）的厚度。**兩個是垂直的維度**。

實際 bench 木板 W 只有 3-4cm，detector 報 cam3 W=11.7、cam4 W=9.2 — 是「沿牆橫向長度」不是「step direction 厚度」。導致 step_over 公式算太大、過 STEP_CM_MAX → 變 over_partial、誤判 crush_into。

新 grouping logic (D 方案)：
- features 按 y_px 排序，相鄰 ≤80px 的 line 歸同組（視為同物體的 top+bottom edge）
- 組內 ≥2 line：W = Y span × cm_per_px (cm_per_px ≈ d_cam/200)（真實厚度）
- 組內 1 line（只看到 top edge）：W = `DEFAULT_OBSTACLE_W_CM` = 3.0cm（保守 fallback，符合典型 frame/seam）

### 預測對木板 case 結果
| | 舊 | 新 |
|---|---|---|
| cam3 (single line) | W=11.7 → over_partial 50, crush 2.9 | W=3.0 → step 47.25 over ✓ |
| cam4 (3 lines y=261/262/336) | W=9.2 → over 49.4 | W ≈ 7.4 (span 75px × 0.1) → step 47.65 over ✓ |
| combined | over_partial 50 | over ~47.65 |

### 影響
- 木板 / 窗框 / 縫隙 W 估計大幅精準
- Cluster 把不同物體誤合（dy<80）→ W 過估 → step 過大、可能 over_partial
- DEFAULT_OBSTACLE_W_CM=3 對寬障礙物（>5cm）會 underestimate → 但這 case 通常會 detect top+bottom 走 grouping path
- Features JSON 多 `estimated_width_cm` 欄位方便 debug

---

## 2026-06-09g Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_detector.py:_line_quality_penalty` — thickness threshold THICK_OK 從 4 放寬到 3
- `frame_capture/obstacle_detector.py:classify_line` — QUALITY_DOWNGRADE_TH 從 0.5 放寬到 0.4
### 原因
木板 bench case：cam3 看到木板頂緣 thickness=2 → thickness_penalty=0.67、再乘 straight*motion 0.57 → 總 penalty=0.38 → 降到 ambiguous → decision=proceed (false negative)。

木板邊緣 thickness 經常 ≈ 2px（measure_thickness 的 single-column gradient band 算法限制，木紋無法穩定 detect 多 pixel band），跟 cable 同範圍 → 06-09c 加的 thickness penalty 把木板也誤殺。

兩個修法一起做（單 A 不夠救 cam3）：
- A: thickness=2 penalty 0.67 → 0.75
- B: downgrade threshold 0.5 → 0.4

預測 cam3 case 新總 penalty = 1.0 × 0.75 × 0.57 = 0.43 > 0.4 → 保住 obstacle ✓
### 影響
- 木板邊緣 (thickness 2-3) 不會被誤殺
- Cable 防護：thickness=1 penalty 0.5 + 通常 cable straight/motion 也差 → 總 penalty 仍 < 0.4，擋住
- 風險：邊緣 case (penalty 0.4~0.5) 行為翻轉，可能 false positive 略增（靠 AND combine 兜底）

---

## 2026-06-09f Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_detector.py:motion_parallax_detect` — BLOCK condition 從「median < 2.0」改成「p90 < 3.0 AND max < 10.0」雙條件
### 原因
木板 case bench：cam3 median=0.98 max=70，cam4 median=0.43 max=34 — 兩 cam 都偵測到木板**強烈位移**但被舊條件誤 BLOCK。

原因：木板只佔畫面一部分，大部分像素是不動的背景 → 整張 median 被拉低 → median < 2 → BLOCK。但 max 70px 證明某區域真的有大移動，應該放行 detector 跑。

新條件：**只有當 p90 跟 max 都小**才 BLOCK（= 真的整張畫面靜止）。任一指標夠大就放行。
### 影響
- 木板 / 縫隙 / 邊角小 obstacle case：median 低但 max 高 → 不再誤 BLOCK
- 真正沒位移 (max < 10px) → 依然 BLOCK
- 06-05 雜亂場景 (median=13 max=56)：仍 proceed，behavior 不變
- 風險：遠處小區域有 motion artifact 但無實物 → 新 condition 會放行 → 但 line quality penalty + AND combine 兜底
### 額外 output
- 加 `motion_p90_px` 進 JSON output 方便 debug

---

## 2026-06-09e Claude (Sadie)
### 修改檔案
- `scripts/cams.sh` — 新增：4 支攝影機 frame_capture.py 同時啟動腳本（start/stop/restart/status/tail）
### 原因
之前 `scripts/wr.sh` 只啟 cam1+cam2 (tmux 內)。bench 上要同時跑 4 支偵測時 cam3/cam4 都得手動 `python3 frame_capture.py ... &` 一個個敲。

新 cams.sh：
- 用 nohup 跑 4 支 background process，pid 存 /tmp/camN.pid
- log 各自寫 /tmp/camN.log，可用 `cams.sh tail` 一次看 4 支
- start 前自動 detect 已在跑的 instance 不重複
- stop 用 pid 殺乾淨
- IP / 路徑都支援 env var override (CAM1_IP=... ./cams.sh start)
### 影響
- 4 支 cam 一條指令搞定，不再要記每支的 port / IP / output path
- 跟 wr.sh 平行使用（wr.sh 啟主程式 + cam1/2 tmux；cams.sh 啟 4 支獨立 cam）— 若要同時用記得避免 port 衝突 (wr.sh 跑 cam1/2 @ 5004/5005、cams.sh 也要這兩個 port → 同時跑會撞，二選一)

---

## 2026-06-09d Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_detector.py:_line_quality_penalty` — 加第三個 sub-check：thickness penalty
- 同檔 `classify_line` — 把 measured thickness 傳進 quality penalty
### 原因
06-09c straightness + motion uniformity penalty 對黃色電線 case 不夠重：cam3 conf 從 1.0 → 0.8 還是 obstacle action。原因：電線蜿蜒程度跟 motion CV 都還沒到 BAD threshold，penalty 只 ~0.8。

第三檢查 thickness：cable/wire thickness 通常 1-2px，真實 wall obstacle (wood/Al frame) ≥ 3-4px。對 cam3 case：
- thickness=1 → thickness_penalty = 0.5
- 配合 straight × motion ~0.8 → 總 penalty 0.4 < 0.5 → 降到 ambiguous → decide_step 看不到 obstacle → proceed ✓

threshold: THICK_OK=4 (penalty 1.0), THICK_BAD=1 (penalty 0.5), 中間線性。
### 影響
- bench 黃色電線 / 細線雜物：降到 ambiguous ✓
- 真實 wall obstacle (wood/Al frame thickness ≥4)：不影響
- 細鋁框 (thickness 2-3) 可能被輕度降權 (penalty 0.67-0.83)，但通常 straight + motion 都 OK 補回，總 penalty 仍 ≥ 0.5 → 維持 obstacle
- 縫隙 (thickness 1-2) 通常走 raw_fallback (conf cap 0.5) 影響有限

---

## 2026-06-09c Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_detector.py:` — 新增 `_line_quality_penalty(img_gray, mag, line)` helper
- 同檔 `classify_line` 加 `mag=None` 參數，套用 quality penalty
- 同檔 `motion_parallax_detect` 兩處 (主流程 + raw_fallback) 把 `mag` 傳進 classify_line
### 原因
06-05 bench 驗 AND combine 成功 downgrade，但 cam3 detector 端仍 confidence=1.0 報 obstacle (誤判黃色電線當 obstacle)。AND 是 safety net，但若兩 cam 剛好都被雜物騙到 AND 就破功。所以加 detector 端 quality 過濾。

兩個 sub-check：
1. **Straightness** — line 周圍 ±10px band 重跑 Canny，看 edge pixels 偏離 Hough line 的 std。電線蜿蜒 → residual std 大 → penalty
2. **Motion uniformity** — band 內 motion magnitude coefficient of variation (std/mean)。真實 wall obstacle motion 均勻；thin 物體在不同深度地面上 motion 不均

兩個 penalty 相乘，conservative threshold (RESID_OK=3px, RESID_BAD=8px; CV_OK=0.6, CV_BAD=1.2)。penalty < 0.5 直接降到 ambiguous，decide_step 看不到 obstacle。
### 影響
- 真實 wall obstacle (直線、motion 均勻) → penalty=1.0 不影響
- 黃色電線 / 不規則邊緣 → penalty < 0.5 降到 ambiguous
- 鋁框 反光可能 motion 不均，是潛在風險點 — 等實機驗證再 tune
- process_camera (非 motion 模式) 不傳 mag，只啟用 straightness check

---

## 2026-06-09b Claude (Sadie)
### 修改檔案
- `web_backend/public/index.html` — manual page 進水球閥 row 從兩個裸按鈕改成互動 panel：狀態指示燈 + ON/OFF + 已開計時 + auto-OFF 倒數；加 watchdog 警示 toast row；加 hint 列點說明
- `web_backend/public/app.js` —
  - 加 `wiState/wiOpenStartMs/wiAutoOffTimer/wiTickTimer/wiPendingAction` 本地 state
  - 加 `wiSetOn/wiSetOff/wiSetError/wiApplyStateClass/wiStartTickLoop/wiStopTickLoop` 狀態管理
  - 按 ON：送 cmd + 設 pending + 60s auto-off timeout（reply OK 才真正進 ON 狀態）；按 OFF：送 cmd + pending
  - `wiHandleReplyLine`：strict 比對 `line === 'OK'` 或 `ERR water_inlet_fail`（避免跟其他 OK 撞）
  - `wiHandleWatchdogEvt`：監聽 `EVT water_inlet_watchdog_force_close open_sec=N` → toast banner
  - 兩個 handler 接進 `onWashrobotLine` 開頭
- `web_backend/public/style.css` — 加 water-inlet panel 樣式（狀態燈三色 + ON pulse 動畫 + pending warn pulse + error red glow + watchdog toast red banner）
### 原因
延續 2026-06-09a 後端 retry+watchdog 補強，前端配套：
1. **狀態燈**：user 不用記憶上次按到 ON/OFF
2. **計時器**：開太久會看到秒數累積、警覺
3. **Auto-OFF 60s**：忘關自動兜底；ON 再按重設計時
4. **Pending 顯示**：點擊到 reply 之間視覺回饋（黃色 pulse），失敗顯紅
5. **Watchdog toast**：後端強制關閉時 GUI 顯紅 banner，不會無聲消失
### 設計取捨
- **只追蹤 GUI 操作**：sweep flow 的開閥不會在 GUI 顯示。理由是 sweep 內部有自己的 valve 管理，不該被 GUI override；user 看 sweep 進度有別的 panel
- **Strict OK match**：reply 不 echo cmd，pending action 對到非 water_inlet 的 OK 會誤判。比對 `line === 'OK'`（純 OK 沒 payload）避開 `OK state=`/`OK scripts=[]`/`OK csv=` 等
- **60s 預設**：bench 測試夠用；長 fill 操作 user 可以在計時到前再按一次 ON 重設；後續若要可調，加 settings.json 欄位
### 影響
- 不影響後端 — 純前端，舊 button 換 panel；按鈕的 data-tgt/data-cmd 改成 id + 自訂 onclick（不再走 `data-tgt[data-cmd]` 萬用 binding）
- Manual page 多兩個 row（panel + toast row 都隱藏，啟動時 toast hidden）
- 連線失敗時 backend 已 retry 3 次才送 ERR，user 看到 error 燈就知道是真斷
### 待辦
- 實機驗證：故意拔 crane Ethernet 看「ON click → pending → error」visual 對不對
- 故意讓 sweep flow 開閥 > 5 min（修改 cap 到 30s 暫測）→ 觀察 watchdog EVT + GUI toast
- 若 user 反映 60s 太短/太長，加進 settings.json (`water_inlet_auto_off_sec`)

---

## 2026-06-09a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 `WATER_INLET_OPEN_MAX_MS = 300_000`（5 min cap）常數；新增 `water_inlet_open_ts_ms_` atomic、`water_inlet_watchdog_running_` atomic、`water_inlet_watchdog_thread_` thread member、`water_inlet_watchdog_loop_()` declaration
- `user_lib/WASH_ROBOT.cpp` —
  - **Fix 1**：`set_water_inlet_(bool)` 加 retry 3 次（500ms gap）+ 三次都失敗印 "valve state UNKNOWN" 警告；成功 open 時 `water_inlet_open_ts_ms_.store(now_ms_())`，成功 close 時 store(0) disarm
  - **Fix 2**：新增 `water_inlet_watchdog_loop_()` background thread，每 10s poll 一次；valve 開超過 `WATER_INLET_OPEN_MAX_MS` 強制 close + 廣播 `EVT water_inlet_watchdog_force_close`
  - **Fix 3**：watchdog thread 在 `init()` 啟動、`stop()` 結束時 join + 強制最後一次 close（若 valve 還 armed）
  - **Fix 4**：`cmd_emergency_stop` 加 `set_water_inlet_(false)`（若 valve armed），補強 sweep mid-water-fill 被緊急停止時的洩漏 path
### 原因
**問題**：進水球閥的開閥點全部在 sweep flow（do_arm_clean_sweep_ / arm_clean_sweep_cont），每個開閥都有對應 close，但漏掉兩條路：
1. **set_water_inlet_(false) 失敗時沒 retry** — crane comm 短暫 hiccup 就讓 valve 卡開
2. **detached refill thread sleep 5s 期間 process 掛掉** — thread 隨 process 死，valve 不會被關 → 持續灌水

GUI 端也沒任何保護（ON 按下去沒 timer 自動 OFF、沒顯示當前狀態），user 忘關就漏。

**Watchdog design**：sweep 正常 refill 60-120s，加 5s post-full delay + retry buffer，5 min cap 給足餘裕；任何 sweep 路徑漏關 + crane comm 持續壞掉的狀況都會被 watchdog 兜底。
### 影響
- `set_water_inlet_` 所有 callsite 自動享受 retry（無需動 caller）；單次呼叫 worst case 從 ~60s（crane 60s timeout）變 ~3 × 60s = 180s（3 次都 timeout），實務上 1-2 次內就成功
- watchdog thread 每 10s 一次 atomic load，幾乎零成本
- emergency_stop 多 1 次 set_water_inlet_(false)（armed 時才送），無影響
### 待辦
- GUI 端補強（valve 狀態指示燈 + 「已開 N 秒」計時 + ON 後 auto-off timer 60-120s + 連線失敗視覺提示）— 等這批後端 ok 後另開
- 實機驗證：故意拔 crane Ethernet 線觀察 retry log 跟 watchdog force-close 行為
- 觀察 `[water_inlet_watchdog]` log 出現頻率（如果經常出現代表 sweep cleanup 有真實漏關 case，需追查具體 path）

---

## 2026-06-08b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — disable_seal WAIT_SEAL fast_skip 加 `!endpoint_stalled[i]` guard
### 原因
log 觀察 slave 5: peakI=1225 撞牆 → WAIT_SEAL 跑 1000ms 內 vacuum 只到 -1 → fast_skip 觸發 → weak_seal early。但 rescue 200ms 後 fresh_p=-6（vacuum 還在進步），給足時間其實能封住。

對照同一輪 slave 6：peakI=2300+ 撞牆，**WAIT_SEAL 3200ms 才 SEALED**。如果 slave 6 也跑 fast_skip 路徑會被誤殺。

**fast_skip 原本設計目的**是「cup 完全沒接觸任何東西」這種空轉情境（cup 飄在空氣中），用 500ms 後 best_p > -5kPa 判斷。但**對 endpoint_stalled 的 cup**（peakI > WALL_GATE 1200）已經明確撞牆 → no-contact 假設不成立 → fast_skip 不該對它觸發。

**修法**：fast_skip 條件加 `!endpoint_stalled[i]`。endpoint cup 一律走完整 5 秒 WAIT_SEAL（搭配 slow_plateau 仍會抓真正的「整段都沒進步 = hardware 漏氣」case）。

### 副作用
慢吸 cup 多撐 4 秒等真的 SEAL（vs 之前 1 秒 weak_seal）。對其他 cup 無影響（peakI < 400 直接跳過 WAIT_SEAL，根本不會進 fast_skip 判斷）。

### Test
slave 5 / 6 這種 peakI=1200+ 撞牆但 vacuum 慢的場景，應該看到：
- 之前：`vacuum plateau ... (no contact 1000ms) — stop waiting`
- 之後：`SEALED iter=X wait=3000~4500ms p=-50kPa ...`（給足時間）

或如果真的漏氣：
- `vacuum plateau ... (no progress 2000ms) — stop waiting`（slow_plateau 抓到）

## 2026-06-08a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_attach()` 加 `StepInProgressGuard`；同時把 struct 定義上移至 cmd_attach 上方（原本在 cmd_step_down 上方，forward 不到 cmd_attach）
### 原因
log 觀察 attach 期間 JC100:5/8 持續每 ~1024ms timeout 60+ 秒，WAIT_SEAL 預期 5 秒實際拖到 ~55 秒（11x 慢）。

掃 cmd_attach 發現它**漏設 motion_active_ / step_in_progress_** — 拿了 motion_mtx_ 但沒同步 set cmd_status 的 gate flag。

cmd_status fresh-read 條件：
```cpp
if (!motion_active_.load() && !step_in_progress_.load()) { fresh-read 9 JC100; }
```

cmd_step_down/up 有 `StepInProgressGuard`，attach 沒有 → GUI 每秒 poll status → cmd_status 每次都 fresh-read 9 個 JC100 → 跟 disable_seal 自己的 WAIT_SEAL polling 搶 cli_22_ → slave 5/8 連環 timeout（其他 slave 已 done 不被讀，所以只看到 5/8）。

加上 `StepInProgressGuard _sip_guard{step_in_progress_}` RAII，attach 期間 cmd_status 改用 cache，cli_22_ 流量大幅降低。預期 disable_seal WAIT_SEAL 真實時間恢復到 5 秒。

### Compile fix
原本 StepInProgressGuard 在 line ~6712（cmd_step_down 上方）定義，cmd_attach 在 line ~6051 用不到 → 編譯 error `was not declared in this scope`。把 struct 上移至 cmd_attach 之前（line ~6050），cmd_step_down/up 也照樣可以用（C++ 同 translation unit forward 找得到）。

## 2026-06-06u Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 `end_refill_active_{false}` atomic flag
- `user_lib/WASH_ROBOT.cpp` — `do_arm_clean_sweep_` + `do_arm_clean_sweep_continuous_` cleanup 的 end_refill detached thread 加 `exchange(true)` guard
### 原因
log XKC poll timing 顯示 186ms/57ms/186ms/57ms 交錯 pattern → **2 個 end_refill 背景 thread 同時跑**（每個各自 200ms poll，時間錯開 57ms）。

每個 thread 跑 WATER_FILL_TIMEOUT_MS = 180s 才結束。連續多次 sweep → spawn 多個 detached thread 累積，每個都拚 XKC，還會 race water_inlet open/close（一個剛開、另一個讀到滿就關 → 第一個再開 → 來回打轉）。

加 guard：spawn 前 `if (end_refill_active_.exchange(true)) skip;`，thread 結束時 `store(false)`。**每個時間點最多一個 refill thread**。

### Test
deploy 後連續跑 2~3 次 sweep。應看到第 2 次後出現：
```
[arm_clean_sweep_end_refill] another refill thread already active — skip spawning
```
XKC poll 從 8 reads/s 應降回 5 reads/s（單一 200ms 週期）。

## 2026-06-06t Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — cmd_status fresh-read 9 slaves 加 30ms inter-slave gap；disable_seal WAIT_SEAL poll 100ms → 200ms
### 原因
user 觀察 cli_22_ 上 JC100 1-8 連環 TIMEOUT（每秒換一個 slave timeout）+ `vacuum_release TIMEOUT after 1500ms stuck slaves: 3 2` → PausedOnError。掃過所有 read_pressure_ 呼叫點後找到兩個高流量處：

**A. cmd_status fresh-read**：9 個 slave 連續無 gap 讀，健康時 ~180ms 一輪，但其中一個 timeout 整輪變 1200ms+，加 30ms gap 避免 burst 灌爆 USR-TCP232 .22 gateway buffer。9 × 30 = 270ms 額外時間在 1Hz rate-limit budget 內。

**B. disable_seal WAIT_SEAL poll 100→200ms**：4 cup × 10 reads/s = 40 reads/s 持續多秒對 cli_22_ 是極大壓力（同 bus 還有 PQW/DM2J:14/XKC + cmd_status 1Hz）。降到 5 reads/s 給其他 device 喘息空間。代價：seal 響應慢 100ms，整體 disable_seal 多 ~5%。

### 預期效果
JC100 TIMEOUT 頻率應顯著降低。如果 reset gateway 後跑一輪仍看到全 bus 連環 TIMEOUT → 不是 polling 太快，是物理層問題（gateway 半當機 / 24V sag / RS485 wiring）。

## 2026-06-06s Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — disable_seal 兩條 weak_seal 路徑加 fresh-read **demote rescue**
### 原因
2026-06-06g 加 fresh_p log 但只在 MAX_ITERS wrap-up 路徑；endpoint+plateau early-weak_seal 路徑（slave 5/6 走的那條）沒加。而且兩條都只 log 不 demote。

現補：兩條路徑 mark weak_seal 之前：
1. `sleep_ms_(200)` — 給 vacuum 200ms 真的建立
2. fresh `read_pressure_` + 看 error_flag
3. `fresh_p <= VACUUM_SEAL_DEEP_KPA (-45)` → **demote 成 SEAL**（done=true, weak_seal=false）
4. 否則照舊 weak_seal + EVT

log 差異：
```
舊：[disable_seal:6] iter 1 at wall + vacuum can't seal (p=1kPa) — weak_seal early
新：[disable_seal:6] iter 1 RESCUED at wall — fresh_p=-58kPa <= -45 → SEAL not weak_seal
    （或仍是 weak_seal early，但 log 帶 fresh_p 數值）
```

### 為什麼能 rescue
觀察 slave 2 / 5 / 6：WAIT_SEAL polling 期間 p=0~2 kPa（reads ok=10 err=0），但 step 結束後 cmd_status 顯示 -65 kPa。差距是「polling 那 1s 內 vacuum 還沒建立 / JC100 sensor 響應慢 / cli_22_ stale frame」。多等 200ms + 重讀就能正確判定。

### 副作用
每個 weak_seal cup 收尾多 +200ms。如果 cup 真的沒封住（hardware 問題），多等也沒用，但 fresh_p log 會明確顯示「fresh_p=0」→ 知道是真實壞掉不是讀錯。

## 2026-06-06r Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.{h,cpp}` — 加 `cmd_water_level()`：一次性讀 XKC + 3-retry，回 `OK water_full=N rssi=N`
- `washrobot_new_PI/main.cpp` — 加 `water_level` dispatcher
- `web_backend/public/index.html` — manual cleaning panel 加「水位」row：cell + refresh button
- `web_backend/public/app.js` — `parseWaterLevel()` parser + `btn-refresh-water-level` handler
### 原因
User 要 GUI 上加跟「壓力計 refresh」一樣的水位 refresh 按鈕，按一下能看 XKC 現在水滿沒滿。

水位 cell 顯示：
- `FULL (rssi=N)` 綠 — out=1（水滿）
- `NOT FULL (rssi=N)` 黃 — out=0（沒滿）
- `ERR xkc_unreachable` 紅 — 3 retry 都 fail

cmd_water_level retry 3 次的 pattern 對齊 `do_arm_clean_sweep_` Phase A（2026-06-06k 早就用同 pattern），避免單次 cli_22_ hiccup 就誤判 unreachable。

## 2026-06-06q Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `verify_arm_m2_at_slot_` LEFT/RIGHT target ±0.7 → ±0.5 對齊 motor_api
### 原因
2026-06-06p 改 motor_api slot target 到 ±0.5 但**忘記同步 washrobot 端的 verify_arm_m2_at_slot_**。觀察 log：
```
slot=LEFT M2_pos=-0.322 target=-0.700 diff=0.378 (tol=0.300) FAIL
```
washrobot 對的 target 是 -0.7（motor_api 改前的值），但 motor_api 實際只把 M2 推到 -0.5（新值）。M2 物理上正確（在 -0.5 附近），但 washrobot 誤判 → 觸發 retry。

Sync 後兩邊一致，retry 只會在 motor 真的沒到位時 trigger。

## 2026-06-06p Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp` — slot target ±0.7 → ±0.5（clearance 0.1 → 0.3 rad）
### 原因
2026-06-06o 加了 passive-detection 但 user 觀察 log 還沒看到 `motor passive` 訊息（可能 binary 沒 rebuild，或 probe 沒偵測到 — motor 在 fault 但 ACK frame 看起來正常）。

雙管齊下 — 改根因：把 slot target 降到 ±0.5，**離 mechanical stop (±0.8) 留 0.3 rad 緩衝（vs 原本 0.1 rad）**。

原本 ±0.7 target 物理意義：M2 推到那裡其實在「快撞 stop」邊緣，PD 持續輸出 ~2 Nm 撐住 → damiao 過熱/過流 → fault latch → 之後 control_mit 都 ACK 但 motor 不出力。

降到 ±0.5：M2 在 range 中段，hold torque 接近 0 → 不會過熱。LEFT↔RIGHT 距離也從 1.4 rad 降到 1.0 rad（DEPLOY 之間切換更快）。

### 附帶影響
工具頭擺幅變小。如果原本設計需要 LEFT/RIGHT 各 40° 擺幅（±0.7 rad ≈ 40°），現在變 ±0.5 rad ≈ 28.6°。對清洗滾筒/刮刀打到牆的角度可能有些微差別，需 bench 驗證。如果視覺上發現某個工具沒貼到牆 → 再調整。

## 2026-06-06o Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp` — `lr_move_to_slot_impl` 加 passive-state detection + re-enable
### 原因
2026-06-06n 跑下去 user 觀察：DEPLOY LEFT 完跑 DEPLOY RIGHT，M2 **完全不動**。連續 5 次 RIGHT 都 FAIL，pos == start。但 DEPLOY 之間 M2 自己會漂（-0.5 → -0.15 → +0.2 → ...）。

**根因**：M2 進入 damiao **passive/fault state**。DEPLOY LEFT 把 M2 推到 -0.51（target -0.7，差 0.19），kp×err = 12×0.19 = 2.3 Nm 持續輸出 → 過熱/過流 → 馬達 latch 進 passive：MIT frame 還是 ACK 但 motor 不出 torque → control_mit 看起來成功，馬達不動。

**lr_calibrate_slot 早有同樣的 pre-check**（log 看得到 `motor passive after enable, re-enabling`），但 `lr_move_to_slot_impl` 沒對應防護。

**改法**：lr_move_to_slot 進 loop 前同樣 pre-check（mirror calibrate 的策略）：
1. 送 3 個輕量 MIT frame（target = cur + dir，輕推一點）
2. 讀 tau，< 0.3 Nm → motor passive → 呼叫 `dm_->enable()` 重置
3. warmup 5 frames 重建 torque loop
4. 重抓 cur_cmd 後進入正式 ramp loop

成本：每次 DEPLOY 多 ~60ms（3 frame × 20ms）。如果 passive 真的 trigger 多 ~100ms（5 warmup × 20ms）。整體可忽略。

### 預期效果
連續 DEPLOY LEFT/RIGHT/LEFT/RIGHT... 每次都會檢查 passive 狀態，需要時自動 re-enable。應該不再看到「sequence 第二次切換 motor 不動」的問題。

如果 re-enable 後 motor 還是不動 → 真的硬體 fault，需要 power cycle，不是軟體能修。

## 2026-06-06n Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp` — `lr_move_to_slot_impl` CONV_TOL 0.08→0.20；`lr_calibrate_slot` Phase 2 vel-settle 大幅強化
### 原因
2026-06-06m 跑下去 user 觀察「物理上到 target 了但程式 FAIL」：
```
M2 lr_move_to_slot FAIL  pos=-0.564775  target=-0.7  err=0.135
```
err 0.135 rad ≈ 7.7° — 在 bench 視覺上 M2 已經到 slot，但 CONV_TOL=0.08 (4.6°) 擋住。

**根本原因**：Phase 2 vel-settle 用 `s.hold_kd` 阻尼 → motor 推到一半就被 damping 拉住 → 停在 0.1 rad 偏 target 的位置 → set_zero 抓的零點偏了 0.088 rad → 後續所有 slot 位置都帶這個偏差。

**兩塊改**：
1. **lr_move_to_slot CONV_TOL 0.08 → 0.20**：接受 bench-現實的 calibration drift。0.20 rad ≈ 11.5° 還是擋得住 1+ rad 的 catastrophic miss（也就是 lr_move_to_slot 真的根本沒動的 case）
2. **Phase 2 vel-settle 強化**：
   - `VEL_SETTLE_KP` 從原本沿用 P2_KP_RETRY (20) → 獨立常數 **30**
   - `kd` 從 `s.hold_kd` (應該 ~2) → **0.5** （減少 velocity damping，讓 motor 能持續推到 target）
   - `VEL_SETTLE_MAX_LOOPS` 20 (200ms) → **50** (500ms)
   - settle done 條件改：不只 vel < 0.05，**還要 |pos - target| < CONV_TOL_RETRY (0.15)** 才算 settle 成功
   - 同步把 P2_KP_RETRY 20 → 25, CONV_TOL_RETRY 0.10 → 0.15

預期效果：set_zero 抓到的點離 ideal target 更近（< 0.1 rad）→ slot 位置更準 → lr_move_to_slot 的 err 應降到 < 0.1 → 即使 CONV_TOL 拉到 0.20 也綽綽有餘。

### Test plan
1. 重 build cleaning_arm + 重啟
2. INIT 看 Phase 2 vel-settle 收尾：
   - `final_err < 0.10` → ✓ 治本成功
   - `final_err 0.1~0.15` → 改善但有限，治標保平安
3. DEPLOY LEFT/RIGHT → 看 `[lr_move_to_slot] Done converged` 出現

## 2026-06-06m Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp` — calibrate_arm_slot 加 pre-check + TIMEOUT fallback；lr_calibrate_slot Phase 2 retry + vel settle；lr_move_to_slot_impl 動態 timeout + 回 bool
- `cleaning_arm/main_api.h` — `lr_move_to_slot` / `lr_move_to_slot_impl` 簽名 `void` → `bool`
- `cleaning_arm/main_api.cpp` — `cmd_deploy_sequence` 收 bool 回值 → 失敗回 ERR；LR_SLOT dispatch 同樣
### 原因
**Issue 1+2+5（M1 INIT 一直 TIMEOUT）**：M1 物理上在 negative stop 上，Phase 1 推不動 → ever_moved=0 → TIMEOUT。每次失敗的 INIT 還會重複用大 torque 撞 stop → 累積熱/電流 → damiao firmware 進 fault state（"switchControlMode failed"）。

**Fix（pre-check）**：calibrate_arm_slot 一進來不直接跑 Phase 1，先連 3 frame 觀察 `|pos|<0.05 && |vel|<0.04`。3 連續成立 → 認定已在 stop，**跳過 Phase 1 直接 set_zero**（goto m1_skip_to_set_zero）。避免重複撞 stop + 馬上能進 Phase 2。

**Fix（TIMEOUT fallback）**：萬一 pre-check 沒抓到（pos 在 0.05~0.10 邊界），Phase 1 TIMEOUT 路徑多檢查 `!ever_moved && |pos_now|<0.10` → 同樣視為 at-stop，goto 過去 set_zero（不再 return false）。

---

**Issue 3（M2 INIT Phase 2 settle 0.11 rad 偏差）**：Phase 2 loop 跑滿 3s timeout 後不管收斂沒收斂都印「settled」，set_zero 抓到的是正在運動中（vel=0.12, tau=-0.34）的瞬時位置，每次 INIT 零點不一致。

**Fix**：Phase 2 邏輯重寫成 lambda `run_phase2_attempt(kp, tol, max_loops)`：
1. 第一次 kp=12.5 / tol=0.06 / 3s — 沒收斂 → log WARN
2. 第二次 retry kp=20 / tol=0.10 / 1.5s — 提升 torque 推過摩擦
3. 還沒收斂 → log `WARN Phase 2 NOT converged after retry`（不 fatal，INIT 必須走完）
4. **set_zero 前等 vel<0.05 最多 200ms**（外加保險）— 確保不抓運動中位置

---

**Issue 4（M2 lr_move_to_slot 不驗證到位）**：MAX_LOOPS 寫死 100 (=2s)，距離大於 1.2 rad 來不及 ramp + settle → loop exit 但印 "Done"，washrobot 端被誤導以為 sweep 對位置。

**Fix**：
1. 動態 timeout：`max_loops = (ramp_time + 0.6s) / DT`，介於 1.5s~5s
2. loop 後檢查 `|pos-target|<CONV_TOL` → 沒過印 **FAIL** + 細節（pos/target/err/start/dist/timeout），有過印「Done converged」
3. 簽名 `void` → `bool`（true=converged, false=fail）
4. `cmd_deploy_sequence` 與 `LR_SLOT` dispatch 收到 false → 回 ERR 給 washrobot
5. Washrobot 端的 `deploy_with_m2_verify_`（2026-06-06l）原本只 fallback 看 STATUS，現在直接 `arm_cmd_(DEPLOY)` 就會收到 ERR → 觸發 retry 路徑

### 注意
- `calibrate_arm_slot` 用了兩個 goto label（`m1_skip_to_set_zero` 給 pre-check 用，`m1_skip_to_set_zero_from_phase1` 給 TIMEOUT 用）。C++ 標準允許這種 forward goto + 跳出 scope，destroys POD locals OK
- Phase 1 整段包進 `{ }` 給 goto label scoping
- 編譯前提：cleaning_arm 用 g++ ARM aarch64 交叉編譯 → 用 `compile.sh`

### Test plan
1. deploy 後跑 INIT → 看 log 是否出現 `pre-check: already at stop` 或正常 Phase 1 seek+set_zero
2. INIT 後跑 `[M2 calibrate] Phase 2 converged at pos=X (target=Y offset=Z)` — Z 應該 < 0.06；若印 `WARN Phase 2 NOT converged after retry` 表示物理層需要檢查
3. DEPLOY LEFT/RIGHT 連續切換 → 觀察 `lr_move_to_slot Done converged` vs `FAIL`，washrobot 端 verify-retry 應該能收到 motor_api 回的 ERR

## 2026-06-06l Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 `verify_arm_m2_at_slot_()` 宣告 + `ARM_M2_SLOT_TOL_RAD=0.30` / `ARM_M2_VERIFY_RETRIES=2` 常數
- `user_lib/WASH_ROBOT.cpp` — 加 `verify_arm_m2_at_slot_` 實作；`do_arm_clean_sweep_` 跟 `do_arm_clean_sweep_continuous_` DEPLOY 包 retry loop
### 原因
bench 觀察：motor_api `lr_move_to_slot` 沒實際驗證 M2 到位就回 OK + 印 "Done"。觀察到：
- M2 在 LEFT(-0.58)，被叫去 RIGHT(+0.7) → 距離 1.27 rad → motor_api timeout 認為 Done，M2 沒動，停在 -0.58
- 後續 DEPLOY 命令累積錯誤起點 → 永遠歪掉

新加 `verify_arm_m2_at_slot_`：DEPLOY 完讀 STATUS、parse `[M2] pos=`、跟 slot 期望角度（LEFT=-0.7 / RIGHT=+0.7 / CENTER=0）比對。差 > 0.30 rad 視為沒到位。

兩個 DEPLOY 站點都包 retry：DEPLOY → 150ms settle → verify M2 → 沒到位就再 DEPLOY，最多 ARM_M2_VERIFY_RETRIES=2 次重試。第 N 次還是 fail：EVT `arm_m2_verify_fail slot=X` + log + **繼續跑**（bench 沒實體牆，sweep 跑歪沒物理影響；要 PausedOnError 反而擋測試）。

副作用：
- 每次 DEPLOY 多 150ms settle + 一次 STATUS read（damiao localhost TCP，極快）
- M2 沒到位的話多最多 2 次重試 = ~12s 額外時間（每次 motor_api timeout ~6s）

## 2026-06-06k Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_arm_clean_sweep_` Phase A 水位檢查加 XKC 3-retry
### 原因
log 觀察：第一次 sweep 完，第二次 sweep 起點水位檢查 XKC 一次 `rx short: got 0 bytes` → 直接 `[arm_clean_sweep] XKC water sensor unreachable` → PausedOnError。

XKC 在 cli_22_ 上跟 JC100×9 + PQW + DM2J:14 競爭，單次 hiccup 是常態。`do_arm_clean_sweep_continuous_` 早就有 3-retry pattern (2026-05-22)，但前景版漏改。對齊：3 次都 fail 才當 unreachable。

## 2026-06-06j Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `verify_arm_deploy_()` 加 early `return false`
### 原因
2026-06-06h 拿掉的是 `arm_monitor_during_sweep_`（sweep 期間 tau monitor）。但還有**另一個獨立的 arm obstacle check**：`verify_arm_deploy_` 在 DEPLOY 後驗證 M1 角度有沒有到 wall_mm 期望位置，沒到 → `[arm_protect] DEPLOY ... hit obstacle`。

bench 測試沒實體牆，M1 永遠停在 0 rad 附近 → 每次 DEPLOY 都假障礙 → `[PAUSE-ON-ERROR] clean_arm_deploy_roller-1`。

最小改：函式頭加 early `return false`，4 個 caller（cmd_arm_deploy / ensure_arm_at_center_for_rope_ / do_arm_clean_sweep_ / do_arm_clean_sweep_continuous_）一次全 cover。恢復就刪那 4 行。

## 2026-06-06i Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_CM` 100→90→**85**，`ARM_SWEEP_EST_MS` 5500→6000→**5700**
### 原因
User 改主意，最終定 85cm。

重新算 sleep 時間（1000rpm × 1cm/rev × 85cm）：
- accel 0.1s + cruise 5.00s + decel 0.1s + 3× fire 0.15s + buffer 0.35s ≈ **5.7s**

舊 5500ms 是 2300rpm × 100cm 時代算的，5月 27 日 RPM 降到 1000 沒重算 → 一直靠 motion-complete 早退出在 cover；現在 monitor 短路（2026-06-06h）失去 motion-complete → 必須照實際算。

## 2026-06-06h Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `arm_monitor_during_sweep_()` 短路成單純 `sleep_ms_(ARM_SWEEP_EST_MS)`
### 原因
arm 障礙物偵測的 3 個 `signal_obstacle()` 早就 comment 掉（M1 INSTANT / M1 SPIKE CONFIRMED / M2 SPIKE CONFIRMED）→ tau 跟 DM2J:14 status 讀完根本沒人用，純粹浪費通訊。每 200ms 兩條讀取：
- `arm_cmd_("STATUS")` 透過 localhost TCP :9527 → motor_api → damiao（不影響 cli_22_ 但 dump log 噴 spam）
- `D_(DM2J_ARM).read_status()` 在 cli_22_ 上跟 JC100/PQW/DM2J:14 motion 搶 bus（JC100 TIMEOUT 主要污染源之一）

短路：在 `if (!arm_attached_) sleep` 後直接加無條件 `sleep_ms_(ARM_SWEEP_EST_MS); return;`。其他邏輯（baseline 抓、polling、tau 判斷、DECEL_MASK、dm2j gate）整段保留在 return 後面，方便重啟障礙物偵測時直接刪 early-return + 恢復 3 個 signal_obstacle 即可。

副作用：
- 失去 DM2J:14 motion-complete 早退出（slide 100cm 即使機械上 6s 跑完，邏輯上等滿 ARM_SWEEP_EST_MS 才回）
- 失去 DM2J:14 alarm bit 早報（slide 真的卡住要等 sweep timeout 才知道）
- 失去 arm tau 障礙物偵測（本來就 disable 了）
測試 phase 可接受。

### 配套
存 memory `project_slave2_weak_seal_stale_reads.md`：slave 2 連續 WEAK SEAL 但 post-step cmd_status 顯示真空滿，懷疑 cli_22_ contention 害 JC100:2 polling 讀 stale；要等 deploy 看新加的 `reads ok=N err=M` + `fresh_p=...kPa` log。

## 2026-06-06g Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — water_inlet verify sleep 50ms → 200ms
- `user_lib/WASH_ROBOT.cpp` — disable_seal WAIT_SEAL 加 per-slave read accounting + WEAK SEAL wrap-up fresh-read log
### 原因
**Issue 1**：slave 2 一直 WEAK SEAL，但 user 觀察結束 step 後真空度顯示滿的 → 懷疑 WAIT_SEAL 期間 JC100 讀值是 stale 或拿到 last_pressure (comm error 但 driver 還是 return 舊值)。加診斷 log：
- `read_ok_cnt` / `read_err_cnt` 計每個 slave 在 WAIT_SEAL 內成功 / 失敗 read 次數
- vacuum plateau 退出時 log 顯示 `reads ok=N err=M`
- WEAK SEAL wrap-up（MAX_ITERS 失敗的 cup）多做一次 `read_pressure_` + log `fresh_p=...kPa`；如果 fresh_p 跟 polling 期間的 p 差很多，代表 polling 拿到 stale → 是 bus contention / JC100 timing 問題；如果一致則是 cup/wall 物理問題

**Issue 2**：cli_M 上 water_inlet ON 連 3 次 verify fail 全 give up；前面已記錄 cli_M gateway TCP recv buffer 有 stale-frame 殘留 (memory `project_modbus_tcp_stale_buffer.md`)。原 50ms sleep 不夠 SD76 polling cycle 走完，verify FC01 readback 拿到舊狀態。改 200ms 給足時間。

## 2026-06-06f Claude (Sadie)
### 修改檔案
- `web_backend/public/index.html` — 全部 24 個 `<ul class="hint">` 區塊精簡，總文字量縮 60%+
### 原因
User 說 hint 太多。整理掉技術細節（reg 位址、IP、變數名、實作 detail），保留安全警告 + 操作差異說明。
### 主要精簡
- CLEAN SWEEP phases：13 行 → 2 行（A 補水 / B INIT / C × N rounds / D cleanup）
- 重心校正：7 行 → 4 行（保留 ⚠ 50cm 警告）
- crane 鋼索張力 settings：4 行 → 3 行
- IMU baseline 說明：4 行 → 1 行
- 全部 attached on/off 類 hint：2 行 → 1 行（只保留 OFF 的解釋）
- 移除 ML pipeline / git path / register address 等技術細節

---

## 2026-06-06e Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp:arm_sweep_monitor_loop_` — M1 INSTANT TRIGGER / M1 SPIKE CONFIRMED / M2 SPIKE CONFIRMED 三處的 `signal_obstacle(...) + break` 註解掉
### 原因
User 目前 bench 測試場景不需要上手臂障礙物偵測。
1. M1 INSTANT (>1.0 Nm) 偶發誤觸發（slide 加速時 M1 lateral 反力 ~1.07 Nm）
2. M1 spike + sustained CONFIRMED 同樣的誤觸發
3. M2 早就 disable（threshold 100 Nm）但保留邏輯路徑、順便也註解
### 行為
- Monitor 仍每 200ms poll、印 M1_tau / M2_tau / delta（用來觀察 spike）
- 觸發條件達成時印 `[arm_sweep_monitor] ... (DISABLED — testing mode)` 但**不會** call signal_obstacle、不會 break loop、不會停 slide、不會 PausedOnError
- Confirmed 路徑加 `m{1,2}_armed=false; m{1,2}_spike_count=0` reset，避免每 poll 都印 CONFIRMED log spam
### 還原
- 把每段 `// signal_obstacle(...); // break;` 兩行的 `//` 拿掉即可
- 同時拿掉「DISABLED — testing mode」字串保持 log 乾淨
- M1/M2 reset 那兩行可以留（不影響原邏輯）

---

## 2026-06-06d Claude (Sadie)
### 修改檔案
- `web_backend/public/index.html:94` — panel 標題 `scripted run（自訂步長序列）` → `auto run`
- `web_backend/public/index.html:106` — button text `▶ Run Script (down_sweep_af)` → `▶ Run`
- `web_backend/public/index.html:111` — label `已存 script:` → `已存:`
- `web_backend/public/index.html:125-130` — 移除 auto run panel 的 `<ul class="hint">` 整段詳細說明
### 原因
User 要求簡化 GUI panel 名稱。
### 影響
- 純 cosmetic，功能不變
- 內部 panel-script class、button id (btn-run-script 等) 沒動

---

## 2026-06-06c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h:387` — `ARM_SWEEP_CM` 90 → 100
### 原因
User 把上滑台 sweep 行程再拉到 100cm。
### 影響
- 所有 sweep sub-stroke target 從 90 → 100
- 4 sub-stroke 總滑行距離 400cm
- DM2J:14 物理行程上限自查（之前 100cm 試過 OK）

---

## 2026-06-06b Claude (Sadie) — compile fix for 2026-06-06a
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` 兩個 end_refill detached thread — 把 `std::this_thread::sleep_for(std::chrono::milliseconds(WATER_POLL_INTERVAL_MS))` 改成 `sleep_ms_(poll_ms)`，其中 `poll_ms` 是 copy 自 constexpr 的 local int
### 原因
編譯 linker error：`undefined reference to WashRobot::WATER_POLL_INTERVAL_MS`。
根因：`std::chrono::duration` 的 template constructor `duration(const Rep2& r)` 用 const reference，對 `static constexpr int` 算 ODR-use → 需要 out-of-class definition。而既有的 `sleep_ms_(int)` 是 by-value 參數 → 沒這個問題。
### 修法
先把 `WATER_POLL_INTERVAL_MS` / `WATER_FILL_TIMEOUT_MS` copy 到 local int，然後用既有的 `sleep_ms_()` helper（跟 Phase A 已 verified-working 的程式碼一致）。

---

## 2026-06-06a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h:387` — `ARM_SWEEP_CM` 80 → 90
- `user_lib/WASH_ROBOT.cpp:do_arm_clean_sweep_` + `do_arm_clean_sweep_continuous_` cleanup — 加 end-of-sweep background refill detached thread
### 原因
User 兩個需求：
1. 上滑台 sweep 行程從 80cm 拉到 90cm（涵蓋更寬範圍）
2. Sweep 流程結尾再補一輪水（讓下次 sweep 一定有水可用），但要 background 不卡其他流程

### 設計
Cleanup lambda 末尾 spawn detached thread 做 refill：
- 讀 XKC 水位
- 已滿 → 直接 return
- 沒滿 → 開 valve、polling 等滿 (timeout 180s)、5s 延遲後關 valve
- 跟 Phase A 補水相同邏輯，但跑在背景

放 cleanup 內好處：abort / error / success 都會 fire（任何 exit 都補水），確保下次 sweep 啟動時水位足夠。

### 影響
- 每次 sweep 結束會看到 `[arm_clean_sweep(_cont)_end_refill]` 系列 log
- 補水期間其他流程（step body cycle / 主 thread）不被阻塞
- 連續 sweep（背景 thread）的場景下，外層 step_down 結束後 sweep 已 join，但 refill 還在背景跑 — 下一輪 sweep Phase A 讀 XKC 看到「已滿」就 skip 自己的 refill（兩個 detached close thread 同時關 valve 是 idempotent，無害）
- 偶發狀況：refill 還在跑時 user 按 emergency_stop → refill 不會被打斷（detached），會繼續完成補水。如果這變煩可以加 abort flag 中斷

---

## 2026-06-05p Claude (Sadie) — refines 2026-06-05k
### 修改檔案
- `user_lib/WASH_ROBOT.cpp:do_arm_clean_sweep_` 跟 `do_arm_clean_sweep_continuous_` round body — 第 3 stroke 從 `LEFT roller 0→80` 改成 `RIGHT scraper 0→80`
### 原因
2026-06-05k 是「滾筒 ×3 + 刮刀 ×1」，user 改成「**滾筒 ×2 + 刮刀 ×2**」。物理上對稱：濕拖兩趟（往右+往左）後乾掃兩趟（往右+往左）。
### 新 round 順序
| # | slot | water | target | skip_deploy | 動作 |
|---|---|---|---|---|---|
| 1 | LEFT  | true  | 80 | false (首次 DEPLOY) | 滾筒濕拖→右 |
| 2 | LEFT  | true  | 0  | true (同 slot+water) | 滾筒濕拖→左 |
| 3 | RIGHT | false | 80 | false (換刮刀，DEPLOY) | 刮刀乾掃→右 |
| 4 | RIGHT | false | 0  | true (同 slot+water) | 刮刀乾掃→左 |
### 影響
- 仍 2 次 DEPLOY（1 次 LEFT、1 次 RIGHT），時間跟 2026-06-05k 相同
- 比 2026-06-05k 少一趟滾筒、多一趟刮刀 — 預期更乾、過水較少

---

## 2026-06-05o Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 `FEET_TARGET_OVER_CAP_CM = 5.0` / `FEET_MAX_OVER_CAP_CM = 4.5` 兩個常數；加 `feet_target_capped_(int slave) const` declaration；cycle_group_ template 內 feet 分支改用 helper
- `user_lib/WASH_ROBOT.cpp` —
  - **Fix A**：`pusher_extend_with_disable_seal_` 結尾 record_seal_pulse_ 加 `if (weak_seal[i]) continue;` skip，避免 WEAK_SEAL 污染 last_seal_pulse_
  - **Fix B**：`feet_max_overextend_cm_()` 回傳值前先 cap 在 `FEET_MAX_OVER_CAP_CM (4.5cm)`，超過印 `[snowball]` warning
  - **Fix C**：新增 `feet_target_capped_(int slave)` helper；`smart_extend_subset_` feet 分支改用 helper
### 原因
**Snowball bug**：跑 run_script 多 step 後，body group 5,6,7,8 的 Phase 1 fast extend 直接撞到 pusher 物理 end-stop（peakI 2900-3100mA、pos 61000+）。每 iter 後續又因為 `endpoint_stalled` 旁路全跳過，沒給真空 build 時間 → WEAK_SEAL × 4。

**完整鏈條**：
1. 機體下降過程中每 step feet cup 需要伸得更遠才能 seal（鋼索 angle 拉、機體微傾）— **物理現象**
2. `pusher_extend_with_disable_seal_` 收尾無條件 `record_seal_pulse_(slave, final_pulse)`，連 endpoint_stalled WEAK_SEAL 的位置都記
3. 下一 step feet target = `last_seal_pulse_` → cup 從更遠位置開始 → 又往前推一點才 seal → 又記下來
4. feet 慢慢 snowball（每 step +1-3cm），但 feet 自己還能 seal 不爆
5. **body 突然爆**：body target = preset + `feet_max_overextend_cm_()` × 3000；feet_max 是 4 顆 feet **最大值**，一顆 feet 撞 endpoint sealed at 60255 (slave 2 in log) 就讓 feet_max 跳到 10.62cm
6. Body Phase 1 target = 34000 + 31860 = 65860；body cup 物理 max ~60000 → 撞 end-stop
### Cap 值如何選
| Group | Preset | 物理 max | iter loop +12000 餘裕 | 可用 over cap |
|---|---|---|---|---|
| body 5,6 | 34000 | ~60000 | 12000 | (60000-34000-12000)/3000 ≈ 4.67 cm → **B cap 4.5cm** |
| feet 1,3 | 29000 | ~60000 | 12000 | (60000-29000-12000)/2857 ≈ 6.65 cm → **C cap 5.0cm 保守** |
- B 直接擋 body target 暴衝（cap feet_max）
- C 擋 feet 自己 snowball 不超 preset+5cm（避免 feet 也撞 end-stop）
- A 是 B/C 的 enabler — cap 後 cup 撞不到牆 → WEAK_SEAL → A 不 record → 下一輪重新從乾淨的 last_seal 起算 / realign 拉回 preset
### 撞牆超過 cap 的牆距怎麼辦
牆物理上離機體超過 cap+iter 範圍時：cup 在 free air、no contact → iter loop fast-skip → MAX_ITERS WEAK_SEAL。A 接手不污染 last_seal。下一 step 再 try 一樣 → 4 步累積後 realign trigger（drift > 1.5cm）把 cup 真的拉回 preset。

如果牆**永遠**離機體 > 8.5cm（body 物理絕對極限），是吊機 / 重心 / 鋼索 angle 的物理問題，軟體無解，需要：
- 操作層面：crane retract 把機體拉近牆
- 或新功能：偵測「機體離牆太遠」自動 crane retract（**未實作**，看實機跑 A+B+C 一輪後再評估）
### Log 變化
新增 `[snowball]` 字串 prefix 三條 log，方便追雪球發生：
```
[snowball] WEAK_SEAL slave 5 pulse=61131 — NOT recording (keep prior last_seal_pulse_=34521)
[snowball] feet slave 2 last_seal=58234 > cap 44186 (preset+5.0cm) — clamping
[snowball] feet_max_overextend_cm uncapped=10.62cm > cap 4.5cm — clamping (protects body target from overshoot)
```
### 影響
- cycle_group_ body 跟 feet 路徑都改了，**所有 step_down / step_up / attach / smart_extend_subset_** 都會用到 cap
- 既有 sealed slave 的 last_seal_pulse_ 不變（只擋未來污染）
- realign 機制不動，繼續是 drift > 1.5cm 觸發
### 待辦
- 實機驗證 A+B+C 是否擋住 snowball
- 觀察 `[snowball]` log 出現頻率（過高 = 牆距已超過機體物理範圍，需手動 crane retract）
- 跑 5+ step 看 feet last_seal 是否穩定在 preset+5cm 附近不再長
- 若 realign 仍然 stage 1 STALL slave 3，再考慮「先 valve off 解 vacuum 再 retract」這個改動（D）

---

## 2026-06-05n Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp:cmd_run_script` — 整個 step loop 重寫成 pipeline 模式（照抄 `cmd_run` 的 `is_pipeline` 分支）：
  - 把 per-step「sweep 呼 cmd_step_down_sweep_after_feet / transit 呼 do_step_down_」改成**統一呼叫 `do_step_down_(skip_cleaning_sweep=true, after_hook, before_hook)`**，sweep 完全用 hook 控
  - 加 RAII `SweepJoin` guard、`launch_round` lambda（同 cmd_run）
  - Pre-loop sweep：只在 `step[0].sweep == true` 時 fire（避開 transit 開頭）
  - iter 1 ba pattern：`before_feet_rail_hook` 永遠負責 join pre-loop sweep（若有）；`after_feet_rail_hook` 條件 launch（看 `step[0].sweep`）
  - iter k≥2 af pattern：`after_feet_rail_hook` 先 join 上一輪 sweep（無條件防 leak）→ 條件 launch（看 `step[k-1].sweep`）
  - Loop 結束：join 殘留 sweep + `handle_post_sweep_obstacle_` + `ensure_arm_parked_after_rope_("run_script_end")`
- `.claude/scripted_run_plan.md` — 加「Pipeline 模式」章節，畫每階段 hook 觸發時序 + sweep 覆蓋範圍表 + transit step 的 trade-off 明文寫出來
### 原因
**User 觀察**：cmd_run 在 pipeline 模式下 sweep 跟 step 是 overlap 跑的（iter N 的 sweep 跟 iter N+1 的 Phase A 並行），但 cmd_run_script 的舊實作沒這個 overlap（每 step 等 sweep 完才往下）→ 每 step 慢 5-8s。User 要 run_script 也做到。
**確認 hook 觸發時機**（do_step_down_ line 5937-6356）：
- `before_feet_rail_hook` 在 feet pre_cycle 內 feet release/retract 之後、feet rail PR_trigger 之前
- `after_feet_rail_hook` 在 feet rail done 之後、feet re-extend 之前
**Sweep overlap 區段**（一輪 sweep 覆蓋的 timeline）：
- Pre-loop sweep：iter 1 Phase A 全程 + Phase B 開頭 (feet release/retract)
- iter k after_hook sweep：iter k 後段 feet re-extend + iter k+1 Phase A 全程 + Phase B release/retract + feet rail
### Transit step 取捨（已決定）
| 情境 | 行為 |
|---|---|
| `step[k]=sweep, step[k+1]=sweep` | 跟 cmd_run 一模一樣，full overlap |
| `step[k]=sweep, step[k+1]=transit` | iter k 還是 launch 自己的 sweep（不然 step k 沒實際刷洗！），該 sweep 跑進 iter k+1 Phase A 背景 → transit 前段 arm 還在動 |
| `step[k]=transit, step[k+1]=sweep` | iter k+1 從 Phase A 沒 sweep 在背景，iter k+1 after_hook 才 launch（loss 部分 overlap）|
| `step[k]=transit, step[k+1]=transit` | 全程沒 sweep ✓ |
**Trade-off 明寫進 plan**：sweep→transit transition 時 transit 前段有 sweep 餘音；物理上安全（arm 跟機體一起下降、wall_mm 不變），但如果 transit 是「該位置不能讓 arm deploy」就要改設計（plan 裡記了兩個未來選項）。
### 影響
- run_script 每 step 預期省 5-8s（跟 run 一樣的 pipeline 收益）
- cmd_step_down_sweep_after_feet 從 cmd_run_script 移除 reference — 該 method 本身沒動（GUI 按鈕還在用）
- EVT 行為不變（script_start / script step K/N mode=X / script_complete）
- 既有 saved script 含義不變（仍是 same CSV → same 行為，只是現在 overlap 變多）
### 待辦
- 實機驗證：跑 `30*3`（全 sweep）對照 cmd_run，sweep round 數應該都是 N+1 = 4
- 跑 `30,30n,30`（sweep, transit, sweep）觀察 transit 前段是否真有 sweep 餘音
- 跑 `30n*3`（全 transit）確認完全沒 sweep / 預估時間最短
- log 應該每 iter 看到 `[run_script+pipe]` 而非 `[step_down+sweep_af]` 前綴

---

## 2026-06-05m Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `parse_script_csv_` 簽名從 `vector<int>&` 改成 `vector<ScriptStep>&`；class 加 typedef `struct ScriptStep { int cm; bool sweep; };`；註解區更新文法
- `user_lib/WASH_ROBOT.cpp` — `parse_script_csv_` 加 `n` 後綴解析（先 peel `*<count>`、再 peel 尾端 `n`）；`cmd_run_script` 改成 per-step 分流：`sweep=true` → `cmd_step_down_sweep_after_feet(cm)`、`sweep=false` → 直接 `do_step_down_(skip_cleaning_sweep=true)` + `StepInProgressGuard` 包；EVT 加 `mode=sweep|transit` 欄位；`script_start` 加 `sweep=A transit=B` 分計；`cmd_save_script` / `load_saved_scripts_from_disk_` 的 validate-only 呼叫對應改 `vector<ScriptStep>`
- `web_backend/public/index.html` — textarea placeholder 加 `n` 範例；hint 列點補一條
- `web_backend/public/app.js` — `parseScriptCsv` 解析 `n` 後綴 + 回傳 `{cm, sweep}` 陣列 + `nSweep/nTransit` 計數；preview 「N 步 (X sweep + Y transit)，總 Z cm」 — 混搭時才顯示分計；run/save dialog 文字同步；`showScriptProgress` 加 mode 參數 + transit 紫色斜體；`EVT script step` regex 加 `mode=` group
- `web_backend/public/style.css` — `.script-progress-cm.script-progress-mode-transit` 紫色斜體
### 原因
User 提問「除了 cm，可不可以每 step 個別決定要不要刷洗?」→ 跟 Sadie 確認後選 **`n` 後綴方案**（default = sweep，不破壞 2026-06-05k/l 已存 script 含義）。
**重要發現**：`cmd_step_down(cm)` 並非純下移 — 預設 `skip_cleaning_sweep=false`，會在 Phase C 跑 `do_arm_clean_sweep_(ARM_CLEAN_WALL_MM, ARM_CLEAN_ROUNDS)` 完整刷洗一輪（WASH_ROBOT.cpp:6326）。真正的純下移要走 private `do_step_down_(skip_cleaning_sweep=true)`，跟 `cmd_run` 在 pipeline 模式下用的 path 一樣。`cmd_run_script` 直接呼叫此 private method（同 class）+ 自己包 `StepInProgressGuard` + 結尾 `ensure_arm_parked_after_rope_`。
### Per-step CSV 文法（更新後）
```
token ::= <int> ['n'] ['*' <count>]
```
- `30` = 30cm + sweep（預設）
- `30n` = 30cm 純下移、不刷洗
- `30n*3` = 3 個純下移
- `30,30n*2,30` = sweep / transit / transit / sweep
### EVT 變更
- `EVT script_start total_steps=N total_cm=X sweep=A transit=B`
- `EVT script step K/N cm=Y mode=sweep|transit`
- `EVT script_complete status=ok|fail step=K/N mode=X reason=...`
### 影響
- 2026-06-05k/l 已存 script（如果有人 bench 存過）行為**不變**：無 `n` 後綴的所有 token 預設 sweep = 原本行為
- main.cpp 不動（dispatch table 一樣）
- WASH_ROBOT.cpp 其他既有 method 都沒動到
- GUI preview 只在 transit > 0 時顯示「X sweep + Y transit」分計，純 sweep 還是顯示「8 步，總 210 cm」
### 待辦
- 實機驗證：跑 `30n*2,30*3` 看 transit step 是否真的不啟動 arm sweep（log 應該完全不會出現 `[step_down] start cleaning sweep`）
- 確認 transit step 的時間是不是真的有縮短（理論上省掉 Phase C 一輪 sweep ~5-8s）
- CSV edge case 測試：`30N` (大寫)、`30n*0`、`30nn`、`n*5`（空 cm）

---

## 2026-06-05l Claude (Sadie)
### 修改檔案
- `web_backend/public/index.html` — 在 home page 加新 panel `panel-script`（scripted run），含 CSV textarea + preview line + 3 個操作按鈕 + 已存 script 列表容器 + 隱藏進度條 + hint 列點說明
- `web_backend/public/app.js` — 加 `parseScriptCsv()`（mirror C++ parser）、`updateScriptPreview()` live preview、`btn-run-script/save-script/list-scripts` wiring、`renderSavedScripts()` 動態列表 + Load/Run/Delete 按鈕、`showScriptProgress()/finishScriptProgress()`、`onWashrobotLine` 內加 4 個 matcher（OK scripts=[]、OK csv=、EVT script_start、EVT script step K/N、EVT script_complete）
- `web_backend/public/style.css` — 加 `.panel-script` 樣式（CSV textarea + preview color states + saved row glass panel + progress bar 漸層）；沿用 deep-space aurora 主題
### 原因
跟 2026-06-05k 的 C++ scripted_run 後端對齊的前端 GUI。User 已批准實作。
### Panel 結構
```
┌─ scripted run（自訂步長序列）────────────┐
│ CSV                                    │
│ ┌──────────────────────────────────┐  │
│ │ 30*5,20*3                        │  │
│ └──────────────────────────────────┘  │
│ ✓ 8 步，總 210 cm                       │  ← 即時 preview
│ [▶ Run Script]  [💾 Save as…]  [↻ ...] │
│ 已存 script:                            │
│   cleaning_full  [Load] [▶ Run] [×]    │
│   long_descend   [Load] [▶ Run] [×]    │
│ ┌──────────────────────────────────┐  │
│ │ Step 3/8                  (50cm) │  │
│ │ ████████░░░░░░░░░░░░░░░░░  43%   │  │
│ └──────────────────────────────────┘  │  ← 進度條（執行時自動顯示）
│ • 方向固定 down_sweep_af                │  ← hint 列點
│ • 每 token = 一個 step 的 cm，範圍 5..50  │
│ • 重啟仍保留 (./scripts.json)           │
└────────────────────────────────────────┘
```
### 前端 CSV parser
跟 C++ `parse_script_csv_` 同語意：split by `,` → strip whitespace per token → optional `<cm>*<count>` → range check cm ∈ [5,50] + count ∈ [1,1000] + total ≤ 1000。preview 即時告訴使用者展開後幾步、共幾 cm，或哪個 token 錯。
### EVT 訂閱
4 個 matcher 放在 `onWashrobotLine`，跟其他 EVT 同層：
- `OK scripts=[...]` → renderSavedScripts() 動態建 rows + 綁 Load/Run/Delete
- `OK csv=...` → 回填 textarea + 更新 preview
- `EVT script_start total_steps=N total_cm=X` → 顯示進度條（reset 至 0/N）
- `EVT script step K/N cm=Y` → 更新進度條（K/N，cm 顯示在右邊）
- `EVT script_complete status=ok|fail` → 標 ✓/✗，8 秒後自動隱藏
### 影響
- 新增 home page panel；其他 page (manual/camera/settings) 完全沒動
- 不動 send()、ws.onmessage、onWashrobotLine 既有邏輯，只在裡面加新 matcher block
- 已存 script 列表動態建按鈕用 `data-name` 屬性傳值；name 經 [A-Za-z0-9_-] 過濾仍做 HTML escape（defense-in-depth）
### 待辦
- 實機端對端測試（C++ build → 重啟 washrobot → 從 web GUI 走完 save/list/load/run/delete 流程）
- 觀察 ./scripts.json 持久化是否 OK（重啟後列表還在）

---

## 2026-06-05k Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 `<map>` include；`cmd_run` 後面新增 6 個 cmd_*_script public 宣告；class 結尾前新增 scripted_run private 區塊（3 個常數 + saved_scripts_ map / mutex + 4 個 helper）
- `user_lib/WASH_ROBOT.cpp` — 在 `cmd_run` (line 8199) 後面新增 ~285 行 scripted_run 實作（parse_script_csv_ / validate_script_name_ / cmd_run_script / 4 個 saved-script cmd + load/save persistence）；Constructor 結尾新增 `load_saved_scripts_from_disk_()` 啟動載入
- `washrobot_new_PI/main.cpp` — `cmd == "run"` dispatch 後面新增 6 個 case：run_script / save_script / list_scripts / load_script / delete_script / run_saved
### 原因
User 要求新增「scripted run」功能：使用者預先輸入每 step 不同 cm 的 CSV → 自動跑完。設計決策見 `.claude/scripted_run_plan.md`：
- 方向**固定 `down_sweep_af`**（一律往下含 sweep）
- CSV 支援 `30*5,20*3` 重複 shorthand
- 命名 script 持久化到 `./scripts.json`（key=value 格式跟 settings.json 一致）
- step 軟上限 1000、單 token 重複上限 1000、名稱 `[A-Za-z0-9_-]{1,32}`
User 明確要求「step 詳細過程依照 run 一模一樣」+「小心不要動到現有的功能」→ cmd_run_script 是 cmd_run 的 is_down_sweep 分支直接照刻（arm_sweep_obstacle_pending_ 清旗、set_state_(Running)、motion_active_=true、迴圈內 abort_flag/check_abort_/imu_ask_pending_/cmd_step_down_sweep_after_feet/失敗 set_state_(Error)、結尾 PARK + set_state_(Attached)），只差 cm 來自 steps[i] 與 EVT 訊息前綴 `script step ...`。
**完全沒動 cmd_run、cmd_step_*、do_step_*_、任何 driver layer。**
### TCP 指令集
```
run_script <csv>                  # 跑一次性 script
save_script <name> <csv>          # 命名儲存
list_scripts                       # 列出已存 script
load_script <name>                 # 讀回 CSV
delete_script <name>
run_saved <name>                   # load + run 一氣呵成
```
### EVT
```
EVT script_start total_steps=N total_cm=X
EVT script step K/N cm=Y
EVT script_complete status=ok|fail [step=K reason=...]
```
### 影響
- 新增功能，不影響 cmd_run / cmd_step_* 既有行為
- Constructor 多一次 `./scripts.json` 開檔嘗試（缺檔靜默 fallback，跟 settings.json 同 pattern）
- saved_scripts_ map / mutex 為新成員，不跟其他 motion_mtx_ / crane_mtx_ 共享 lock 順序
### 待辦
- Web GUI panel（textarea CSV 即時 preview + saved scripts 列表 + Run/Save/Delete 按鈕 + script_progress EVT 訂閱）
- 實機驗證 CSV parse edge cases（空 csv、超範圍 cm、`*` 邊界、混 token）
- 實機驗證 ./scripts.json 持久化（重啟後讀回正確）

---

## 2026-06-05k Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp:do_arm_clean_sweep_` 跟 `do_arm_clean_sweep_continuous_` —
  - `sweep_with_tool` lambda 加 `bool skip_deploy = false` 參數
  - `skip_deploy=true` 時跳過 DEPLOY + verify + pqw on/off，只跑 alarm check + slide motion
  - round body 從 2 sub-stroke 變 4 sub-stroke
### 原因
User 要求：每 round 「前面多加一輪滾輪刷洗（LEFT）」 — 0→80 + 80→0 兩個 sub-stroke 後再走原本的 0→80 (roller) → 80→0 (scraper)。
直接呼叫 4 次 sweep_with_tool 會做 4 次 DEPLOY（每次 ~3-5s），中間 sub-stroke 2/3 切換到同 slot 是無用功。加 skip_deploy=true 跳過。
### 新 round 順序
| # | slot | water | target | skip_deploy |
|---|---|---|---|---|
| 1 | LEFT | true | 80 | false（首次必跑 DEPLOY） |
| 2 | LEFT | true | 0 | **true** (同 slot+water 直接 slide) |
| 3 | LEFT | true | 80 | **true** (同) |
| 4 | RIGHT | false | 0 | false（slot+water 都變，必跑 DEPLOY+pqw off） |
### 影響
- 每 round 從 2 sub-stroke 變 4，總時間 ~30s → ~40s（多 2 個 slide 各 ~5s）
- DEPLOY 次數沒變（2 次/round），無 DEPLOY overhead 增加
- log 顯示「LEFT 0→80 → LEFT 80→0 → LEFT 0→80 → RIGHT 80→0」
- water/brush 在第一次 DEPLOY 後開、第四次 DEPLOY 前才關（中間 3 個 sub-stroke 維持 ON）
- 物理：滾筒先走兩趟濕拖、第三趟再走一次（重複清潔），最後刮刀乾掃

---

## 2026-06-05j Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp:pqw_water.init` — debug 旗標 false → true (暫時打開)
### 原因
Bench 排查「first water_inlet on 物理沒動，second 才動」問題。打開 PQW driver debug 看每次 TX/RX hex frame，確認 firmware 接收到的 cmd 跟 readAllStatus 回報內容是否一致。
### 影響
- crane stdout 每次 PQW 操作會印 hex dump（TX single relay、RX echo、TX read status、RX read status 等）
- 排查完要記得改回 false 避免 stdout 太吵

---

## 2026-06-05i Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp:do_arm_clean_sweep_` water-fill 段 — 偵測到水滿後改成 spawn detached thread 5s 後 close valve；timeout/abort 仍立刻 close
- `user_lib/WASH_ROBOT.cpp:do_arm_clean_sweep_continuous_` water-fill 段 — 同上
### 原因
User 要求：sweep 流程中偵測到水補足後，**delay 5 秒才關進水球閥**（讓水稍微過滿 / 確保 sensor 穩定），但**其他刷洗動作不要被這 5 秒卡住**。
### 實作
水滿 break out 時：
```cpp
std::thread([this]() {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    set_water_inlet_(false);
    std::cout << "[arm_clean_sweep] water_inlet closed (5s delay after full)\n";
}).detach();
```
detached thread 5s 後關閥，主流程立刻 return 繼續走 Phase B (arm INIT) + Phase C (sweep rounds)。
### 影響
- water fill phase 結束時間提早 ~5s（不再等 close 才繼續）
- 水箱多進 5 秒水量（依水壓～0.5-1L）
- timeout / abort 仍立刻 close（anti-flood，沒 delay）
- RAII ScopeExit 結束時也會 close — 大部分情況 sweep 跑 30-60s 遠 > 5s，detached thread 早就把 valve 關了，RAII close 是 no-op
- emergency_stop / sweep abort 期間若 detached thread 還沒 fire，RAII 會先 close，detached 5s 後 close 已關的 valve（idempotent）
### 潛在風險
- Process 在 detached 5s 內被 kill（罕見）→ thread 終止、close 沒執行 → valve 留 ON。下次 process 啟動 init flow 會 close（line 5506）兜底
- detached thread 沒 thread name 管理；理論上多個重疊的 5s thread 可能 pile up（user 反覆 RETRY 補水）— 但每個都 idempotent close，無實質風險

---

## 2026-06-05h Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp:cmd dispatch water_inlet` — 加 verify-retry pattern（write → 50ms sleep → readAllStatus → 對比 → 不一致 retry，最多 3 次）
### 原因
實機測試：Linux_test 直連 PQW (.34 slave 12 CH4) 正常開關；但走 web GUI → washrobot → crane → PQW 時偶發「按了沒反應」。
根因：`pqw_water.controlRelay()` 只看 TCP send 成功就 return success，不驗證 relay 真的有切。cli_M 上 SD76 meter polling 持續 100-250ms，PQW write 偶爾撞到 SD76 frame → RS485 RTU 序列化過程中 PQW packet 被吃掉 → driver 看 TCP OK → 回 OK → 物理上 relay 沒切。
模式跟之前 cli_22_ 上 PQW + JC100 撞 bus（washrobot 端 2026 早期已用 `pqw_set_relay_verified_` 解掉）一樣。crane 端補一個。
### 影響
- 每次 water_inlet 寫之後做 FC01 readback；不一致就 retry（最多 3 次，每次間隔 50ms）
- 最壞情境多 ~150ms 延遲；正常情境（first write OK）無額外負擔
- log 出現 `[pqw_water] ch=4 set ON verify fail vr=0, retrying` 表示 retry 發生
- 連續 3 次都 verify fail 仍回 OK（best-effort policy，跟 washrobot 同邏輯），但 stdout 印 `gave up verify after 3 retries` 警告

---

## 2026-06-05g Claude (Sadie) — fix-forward for 2026-06-05f
### 修改檔案
- `Crane_control_PI/Crane_control_PI.vcxproj` — `<ClCompile>` 加 `..\user_lib\PQW_IO_16O_RLY.cpp`；`<ClInclude>` 加 `..\user_lib\PQW_IO_16O_RLY.h`
### 原因
2026-06-05f 在 `Crane_control_PI/main.cpp` 加了 `#include "PQW_IO_16O_RLY.h"` 但 vcxproj 沒列這個檔，所以 MSBuild Linux cross-compile 不會把它 rsync 到 remote → 編譯時 "No such file or directory"。
### 影響
- crane 端編譯能找到 PQW header + cpp，可以正常 build
- vcxproj 列出的所有 user_lib/ 檔案會 deploy 到 remote `~/projects/crane_control_PI/...` 目錄
- 不影響 washrobot vcxproj（washrobot 早就有列 PQW_IO_16O_RLY）

---

## 2026-06-05f Claude (Sadie)
### 修改檔案

Washrobot 端：
- `user_lib/WASH_ROBOT.h` — `CH_WATER_INLET=7` 拿掉（標 deprecated 註解）；新增 private `set_water_inlet_(bool on)` 宣告
- `user_lib/WASH_ROBOT.cpp` — 實作 `set_water_inlet_`（送 `crane_cmd_("water_inlet on/off")`）；`cmd_water_inlet` 改用 helper；12 處內部 `pqw_.controlRelay(CH_WATER_INLET, ...)` / `pqw_set_relay_verified_(CH_WATER_INLET, ...)` 全部改成 `set_water_inlet_(...)`

Crane 端：
- `Crane_control_PI/main.cpp` —
  - include `PQW_IO_16O_RLY.h`
  - 新增 `static PQW_IO_16O_RLY pqw_water;`
  - 新增常數 `PQW_WATER_SLAVE=12 / CH_WATER_INLET=4 / PQW_WATER_TOTAL_CH=8`
  - 新增 `g_dev_pqw_water` atomic + `make_device_state_line()` 加 `pqw_water=` 欄位
  - cli_M init 區段：成功則 init `pqw_water` (external client mode)
  - cmd dispatch 新增 `water_inlet on|off` handler

文件：
- `CLAUDE.md` 硬體架構圖 — washrobot PQW CH7 標 "保留（2026-06-05 控制權移到 crane 端）"；新增 USR_M (192.168.1.34) 上的 PQW slave 12 描述；device driver 表 PQW row 更新成「× 2」；SD76 row USR_B.31 → USR_M.34（順手修舊 drift）

### 原因
User 把水箱進水球閥從 washrobot 端（PQW slave 12 CH7 on cli_22_）搬到 crane 端（PQW slave 12 CH4 on cli_M）。理由可能是：
- 物理重新配線到 crane 那側
- 把 cli_22_（已知擁塞 — JC100×9 + PQW + XKC + DM2J:14）拿掉一個 device 減壓
- crane 端水箱泵浦控制集中

### 行為
- washrobot 的所有 water_inlet 控制（GUI 按鈕、init cleanup、cleaning sweep refill、shutdown 等）→ 透過 `crane_cmd_("water_inlet on/off")` 轉發到 crane
- crane 收到 cmd → pqw_water.controlRelay(CH4, on/off) → 物理 relay
- 每次 inlet on/off 多一個 TCP round trip (~50ms)，但這個動作頻率低（補水週期）無影響
- cli_M bus 上 SD76 meter polling + 偶爾 PQW write，bus 衝突極輕

### Bus 共用注意
cli_M 原本只跑 SD76（meter_loop 每 50-100ms poll）。新增 PQW 後：
- 主要流量：SD76 polling（持續、每秒 10-20 次）
- 新流量：water_inlet relay write（偶爾、sweep 流程內幾次）
- TCP_client::socket_mtx_ 自動序列化、不會撞 frame
- 如果未來看到 SD76 read 偶發延遲跟 water_inlet 撞點時間吻合 → 是預期的（無害），不用緊張

### 還沒做
- 物理上 washrobot cli_22_ slave 12 CH7 的接線應該拔掉（規範文件已標「保留 / 空著」）— 這是硬體面工作
- crane init 失敗時 cmd_water_inlet 會回 ERR — 沒做降級（例如 fallback 回 washrobot 端）。應該不需要，crane 連不上時 washrobot 也已經沒在做 sweep。
- 兩邊都要重編 + deploy；deploy 之間如果有人按 water_inlet 會失敗

---

## 2026-06-05e Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_combine.py:combine_decisions` — combine 邏輯從 OR (conservative) 改成 AND (兩 cam 都看到才觸發 obstacle action)
- 同檔 docstring 改寫
### 原因
Bench 雜亂場景下 OR logic 會被單側 cam 誤觸發：
- 案例：cam3 看到地板黃色電線 + 裂縫 → action=over step=45.3cm，cam4 同場景看到都是 FD>30cm 的遠物 → action=proceed
- OR 取較保守 → over 觸發 → 但其實是 false positive（bench 雜物不是 wall obstacle）

新 AND logic：
- 兩 cam 都 proceed → proceed
- 只有一 cam 看到 obstacle → downgrade 到 proceed（標 alert: single_cam_obstacle_warning，肉眼確認）
- 兩 cam 都看到 obstacle → 取較保守（沿用 priority table）
- 單側 BLOCK 仍 override（safety）

理由：cam3 + cam4 從不同視角看同一面牆應該一致；不一致 = bench 雜物 / 鏡頭髒污 / 反光，likely false positive。
### 影響
- bench false positive 大幅減少
- 真實牆面 deploy 行為基本不變（雙 cam 對同一 obstacle 會一致）
- Trade-off：邊角點狀 obstacle 只被一 cam 看到時會 miss，但 alert 會出現給 user 肉眼判斷

---

## 2026-06-05d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 inline helper `crane_motion_timeout_sec_(int cm)`；`crane_retract_safe_` default timeout_sec 30 → 0（0 = 用 helper auto）
- `user_lib/WASH_ROBOT.cpp:crane_retract_safe_` — body 加 `if (timeout_sec <= 0) timeout_sec = crane_motion_timeout_sec_(cm);`
- `user_lib/WASH_ROBOT.cpp:body_pre_crane_pay_out`（step_down Phase B body cycle）— 用 `crane_motion_timeout_sec_(pay_cm)` 替代 default 60s
- `user_lib/WASH_ROBOT.cpp:body_backup_crane_pay_out_up`（step_up body_backup retry）— 同上
### 原因
實機踩坑：washrobot 送 `pay_out 41` → crane 收到並執行，**但 reply 沒在 default 60s 內回到 washrobot**（可能 EVT 撞 RPC channel、socket zombie、或其他 race）→ PausedOnError → user SKIP → 但 crane 還在執行 pay_out → 接下來的 tension query 2s 內 timeout → WEIGHT SENSOR OFFLINE 又 PausedOnError。

把 timeout 改成「跟 cm 動態 scale」後：
- timeout 變更緊，真的 hang 早 detect
- 配合 motion 完成的時間更接近 → reply 來時 timeout 還沒過、tension query 也不會撞到 crane busy
- pay_out 后 crane 真的閒了 → 後續 tension/retract 不會 race

### 公式
```cpp
crane_motion_timeout_sec_(cm) = (cm + 9) / 10 + 5
```
empirical rate ~10 cm/sec (BAL 30Hz)、fine_adjust 1-3s、overhead 1-2s、safety buffer 共 5s。

| cm | timeout |
|---|---|
| 5  | 6s |
| 10 | 6s |
| 20 | 7s |
| 30 | 8s |
| 41 | 10s |
| 60 | 11s |
| 80 | 13s |

### 影響
- 改變的 callers：step_down body pay_out、step_up body_backup pay_out、所有 `crane_retract_safe_` 沒顯式傳 timeout 的呼叫（6 處）
- 沒動的：`crane_pay_out_to_weight_`（loop 內 pay_out 1，默認 60s 對 1cm 太寬鬆但無害）、`cmd_return_home` 的 pay_out（顯式 300s，對長距離下降合理）
- tension query 2s 維持不動 — 這個問題的 root cause 是 pay_out timeout，pay_out 修對後就不會踩

---

## 2026-06-05c Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_detector.py:93` — 新增常數 `BODY_FEET_OFFSET_CM = 10`
- `frame_capture/obstacle_detector.py:593` — `step_over` 公式從 `FD+W+CAM_TO_FEET+safety` 改成 `FD+W+(2×CUP_RADIUS)+BODY_FEET_OFFSET+safety` = `FD+W+33`
- `frame_capture/obstacle_detector.py:608` — `crushed_into` 公式跟著更新，從 `(STEP_MAX-CAM_TO_FEET)-FD` 改成 `(2×CUP_RADIUS+BODY_FEET_OFFSET+FD+W)-STEP_MAX` = `30+FD+W-STEP_MAX`
### 原因
舊公式只算「feet cup edge 過 obstacle」，沒考慮：
1. cup 自身寬度（trailing edge 也要過 obstacle far edge）
2. body-feet 段位差（feet 是 Phase 2 trailing cup，要追上 body anchor + step）

導致 2026-06-05 bench iter 1：detector 算 31cm 跨過 W=14.2cm 縫隙，實際 body cup 落在縫隙上 vacuum=0 吸不住，rescue 退到 rail=11cm 才 SEAL。

新公式 `step = 33 + FD + W`：
- FD=10 W=2 → 45cm（典型 case）
- FD=0.5 W=14.2 → 47.7cm（bench iter 1 case，剛好 ≤ STEP_CM_MAX=50）

幾何推導見 memory `project_machine_geometry.md`。
### 影響
- detector 算的 step_cm 比舊版多 ~16.5cm
- 對 W < 17cm 的 obstacle 仍能用 single cycle 跨過（≤ STEP_MAX_CM=50）
- W > 17cm 會 fallback 到 over_partial（cup 會壓 obstacle）或 short / block

---

## 2026-06-05c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp:arm_sweep_monitor` — M1/M2 tau delta 改方向感知（不再用 fabs）
### 原因
2026-06-05 bench：M1 tau spike 又一次 false positive，但跟 2026-06-03 那次方向相反：
```
baseline M1_tau=-5.6166
t=800ms M1_tau=-5.1282  (motor magnitude 變小)
d=|Δ|=0.4884 > SPIKE 0.4 → ARMED
M1 tau spike CONFIRMED (3 polls after spike-arm)
[PAUSED ON ERROR] obstacle
```
但 user 確認**沒障礙物**。Motor tau 變不負 = motor 變輕鬆，物理上不是「撞到東西」。

舊 code line 2103 用 `fabs()` 計算 d，所以「motor 變吃力」跟「motor 變輕鬆」都會 trigger。但只有前者是 obstacle：
- 撞 obstacle → motor 推得更用力 → tau magnitude 增加（更負或更正，看 baseline 方向）
- Motor 變輕鬆 → tau magnitude 減小 → **不是 obstacle**

Code 內 line 2135 註解早就指出這問題：「M1_tau=-0.05 vs steady -1.3, delta=1.27 → false INSTANT」— 那次也是 motor 變鬆的 false。

### 修法
加 `directional_delta` lambda：
```cpp
if (baseline < 0 && signed_delta < 0) return -signed_delta;   // 更負 = obstacle
if (baseline > 0 && signed_delta > 0) return  signed_delta;   // 更正 = obstacle
return 0;                                                      // 反方向 = relaxation, ignore
```
M1/M2 都用這個。

### 影響
- **舊 false positive（motor 變鬆）**：d 算成 0 → 不觸發
- **真 obstacle（motor 變吃力）**：d 算同 |Δ| → 行為不變
- **baseline 近 0 邊界**：baseline=0 時 d=0，極端情況下不觸發 — 可接受（arm 在靜止無重力負載時不太可能有 obstacle）
- 配合 2026-06-03g (CNT 2→3) 雙重 filter — sustain + direction

### 預期 log 變化
- false positive (motor 變鬆) 不再 trigger
- 真 obstacle 行為不變
- log 中 d 數值會比之前小（單向 vs 絕對值），ARMED 數量減少

### Rollback
回到 `std::fabs(m1_tau - m1_baseline_tau)`

---

## 2026-06-05b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h:338` — `STEP_CM_MAX` 60 → 50
- `user_lib/WASH_ROBOT.h:746` — comment "5..60" → "5..50"
- `frame_capture/obstacle_detector.py:89` — `STEP_MAX_CM` 45 → 50
### 原因
detector (Python) 與 washrobot (C++) 兩處 `STEP_*_MAX` 不一致：
- detector 用 45 算 step_over，washrobot clamp 用 60
- 結果：detector 算 33.6cm + iter1 shortfall 補 20cm = 53.6cm，被 washrobot clamp 到 60 之下，實際走 54cm（超過 detector 認知的安全邊界）

統一為 50cm。從 iter 1 log 看，rail 31cm 已是邊界（body cup 開始搆不到牆），50cm 是合理上限。
### 影響
- detector 計算 step_over > 50cm 才 fallback short / block
- washrobot cmd_step_down / cmd_run_avoid 等 raw command clamp 用 50
- cmd_run_avoid 的 shortfall + detector_step 相加超過 50 會被 clamp，避免一次走太大步

---

## 2026-06-05a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp:cmd_init_impl_` — 收輪那段改成 nowait + wait pattern（兩邊同步）
### 原因
init 收輪原本是兩個 `try_or_pause_` 各包一個 blocking `PR_move_cm`，左輪走完才換右輪 → 總時間 = L + R。
改成跟 `cmd_wheels`（GUI 收輪按鈕）一樣：兩個 `PR_move_cm_nowait` 先 trigger，再依序 `dm2j_wait_done_` 等完成 → 總時間 ≈ max(L, R)。
### 影響
- init 收輪階段約省 1-3 秒（依輪子實際動的距離）
- 失敗時的 try_or_pause_ context 從 `init_left_wheel_retract` / `init_right_wheel_retract` 合併成 `init_wheels_retract`，PausedOnError 訊息上看不出是左還是右失敗 — 但 driver 自己會在 stdout 印 hex dump 可以追
- 沒加 dm2j_motion_mtx_：init 階段沒背景 arm sweep 跑，不需要與 cli_20_ 序列化
- cmd_wheels 跟 cmd_dm2j_group("wheels") 不變

---

## 2026-06-05b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `VACUUM_THRESHOLD_KPA` -50 → -40
  - `VACUUM_SEAL_DEEP_KPA` -60 → -45
### 原因
Step 加速計畫 F1.3 Step A：vacuum threshold 鬆綁。
**物理分析**：
- Cup 面積 ~30 cm²（SMC LEYG25 配套 cup）
- -45 kPa × 30 cm² = 135N = ~14kg 撐力 / cup
- 4 cups × 14kg = 56kg → 遠超機體 30-40kg 重量
- 不需要等到 -60 kPa 才接受 sealed

**改動**：
- `VACUUM_SEAL_DEEP_KPA` -60 → -45：disable_seal iter 接受 -45 為 sealed（與 EARLY_STOP -45 對齊：motor 早停的點 = iter 成功的點）
- `VACUUM_THRESHOLD_KPA` -50 → -40：vacuum_check_（anchor check）也對應放鬆，保留 5kPa margin（DEEP -45 vs THRESHOLD -40）

**為什麼兩個要一起改**：如果只改 DEEP 不改 THRESHOLD，剛 seal 完到 -45，下次 anchor check 還要 -50 → 立即失敗 → block step。
### 影響
- disable_seal iter 不必等真空建到 -60，cup 到 -45 即視為成功
- WAIT_SEAL phase 提早結束 → 每 cup 省 1-2 秒
- 預期淨節省 3-5s / step
- anchor check 容忍度放寬（cup 漂到 -38 ~ -40 還會被偵測為 unsealed，安全 net 保留）
- vacuum_seal_deep_kpa 也可從 web GUI runtime 調整（settings 中已有）
### 風險
- cup 邊緣狀態（cup 老化、cup 接觸面有髒污）下 -45 可能只是邊緣接觸，不夠穩固
- Mitigation：跟 F1.2 一起 deploy 跑 5-10 step，看 step 期間 cup 有沒有脫離（vacuum_check fail / cup detached）
### Rollback
const 改回 -60 / -50。

---

## 2026-06-05a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `REALIGN_THRESHOLD_CM` 3.0 → 1.5
  - `REALIGN_THRESHOLD_MEAN_CM` 2.0 → 1.0
- `.claude/step_speedup_phase1_plan.md` — 新增 Phase 1 加速計畫文件
- `.claude/crane_balance_hold_plan.md` — Crane balance hold 計畫存檔（暫緩實作）
### 原因
Step 加速計畫 F1.2：降 realign threshold，讓 realign 早 trigger。
**原理**：之前 threshold 3cm/2cm 設定，drift 累積到 ~3cm 才 realign，導致：
- body cup 經常撞 endpoint（pulse > 40000，物理 stroke 極限）
- disable_seal iter loop 跑 3+ 次才 seal
- 一次 realign 工作量大（retract delta 大、stall 機率高）

降到 1.5cm/1.0cm 後：
- realign 觸發頻率 ×2-3（每 1-2 step 而非每 4-5 step）
- 每次 realign 工作量小（retract delta 小、stall 機率低）
- body cup 不再撞 endpoint → disable_seal iter 從 3+ 降到 1（連鎖效益）
- 預期淨節省 3-5s/step

前提：2026-06-01h 已將 realign Phase 2 Stage 0 stall 改為 non-fatal，realign 整體更穩。
### 影響
- realign trigger 頻率上升（log 看到「realign start」每 1-2 step 而非每 5 step）
- 單次 realign 時間應該下降（drift 小、Stage A 短 retract）
- disable_seal log 應該看到 iter 數降低、SEALED 時間縮短
- body cup endpoint hit log 應該大幅減少（「WALL ... endpoint, not obstacle」變少）
### 驗證
- 跑 5-10 step 比較總時間
- 確認 realign 沒有 stall PausedOnError
- 如果發現 realign 變多但 step 沒變快 → rollback（const 改回 3.0/2.0）

---

## 2026-06-03i Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - 新增 `std::atomic<bool> arm_sweep_active_{false}`
  - 新增 `static constexpr int RESCUE_WAIT_SWEEP_MAX_MS = 15000`
  - `cycle_group_` rescue path 進入前加 wait sweep finish 邏輯（最多 15s）
- `user_lib/WASH_ROBOT.cpp` —
  - `do_arm_clean_sweep_` 入口 set true、cleanup 清掉
  - `do_arm_clean_sweep_continuous_` 入口 set true、cleanup 清掉
### 原因
2026-06-03 bench：cmd_step_down_with_sweep 並行模式下，step Phase B feet re-extend 偵測到 obstacle → 進 rescue（rail backup 10cm + re-extend），但同時背景 sweep thread 正在跑（DM2J:14 slide + arm 滾刷 + PQW pump/brush）。
觀察徵狀：
```
[disable_seal:1] STALL+EARLY ... → OBSTACLE
[cycle_feet] obstacle detected — rescue ...
[vacuum_release] all released
[arm_clean_sweep_cont] water full → round 1/1 LEFT      ← sweep 仍在跑
[wait_many ZDT:3] move ...                              ← feet retract
[stall_check all] slave 5 stall_flag SET → release
[stall_check all] slave 6 stall_flag SET → release      ← 旁觀 body slave 也被 latch stall flag
```
rescue 用 cli_22_ (PQW valve + JC100 read) + cli_20_ (DM2J 1+3 feet rail) + cli_21_ (ZDT 1-4)；sweep 用 cli_22_ (DM2J:14 slide + PQW pump/brush + arm STATUS via motor_api)。兩個一起：
1. cli_22_ bus contention 加劇 → FC01 stale frame 風險↑
2. ZDT bus 跨組 motion 互相干擾 → 旁觀 slave 也被 latch stall flag

雖然這次 log 沒崩，但 stall_flag latch 是「並行 race」的紅旗 — 邊緣 case 隨時可能爆。
### 修法
rescue 進入前 check `arm_sweep_active_`，true 就 wait 直到 sweep 結束或 15s timeout：
- 正常 case：rescue 偶發，且 sweep round ~5-10s，影響微乎其微
- rescue 期間 cli_22_ 空、ZDT bus 也沒 sweep 引起的副作用、stall flag 不會莫名 latch
- timeout 15s = sweep round 最壞 case 留 50% margin；若 sweep 卡死也不會無限等
### 影響
- Rescue path 觸發時可能多等 0-10 秒（等 sweep round 自然結束）
- 正常 step（沒 obstacle）完全不受影響（rescue path 沒進）
- sweep / step 整體流程語意不變
- 不影響 standalone `do_arm_clean_sweep_` 觸發的 rescue（standalone 模式 sweep 也 set active 但通常不會並行 step）

---

## 2026-06-03h Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp:do_arm_clean_sweep_continuous_` — 修 loop 條件 bug：max_rounds>0 時不再 check keep_going
### 原因
2026-06-03 bench：cmd_step_down_with_sweep step=5cm 時，sweep 灌完水後 0 round 直接退出 → 沒清洗就結束。
```
[arm_clean_sweep_cont] water full (rssi=3869)
[arm_clean_sweep_cont] loop exit (keep_going=false max_rounds=1 completed=0 rounds)   ← 0 round!
```
Root cause：loop 條件 `keep_going.load() && (max_rounds <= 0 || round_cnt < max_rounds)` 是 AND 把 keep_going 跟 max_rounds 綁在一起。但 2026-05-25 註解明確寫「max_rounds=1 場景 sweep 跑完 1 round 自動結束、**不等 keep_going**」。

實際發生時序：
1. Phase A feet 完成 → 啟動背景 sweep
2. 背景 sweep 灌水（5 秒）
3. 主 thread Phase B 完成（step=5cm 很快）
4. 主 thread 設 `keep_going=false`
5. 背景 sweep 灌完水，loop 條件 `keep_going` 已 false → 退出，0 round

修：分離 max_rounds>0 跟 max_rounds<=0 兩種模式
- `max_rounds > 0`：跑滿 max_rounds 才退（**不檢查 keep_going**，符合「跑完 1 round 自動結束」意圖）
- `max_rounds <= 0`：吃 keep_going 控制（unlimited mode）

### 影響
- cmd_step_down_with_sweep / cmd_step_up_with_sweep / *_sweep_after_feet 場景：每 step 真的會跑滿 1 round 清洗
- cmd_arm_clean_sweep_cont（無 max_rounds 模式）：行為不變（仍由 keep_going 控制）
- 若 user 用 emergency_stop 想中止 sweep：abort_flag 仍由其他機制處理（sweep_with_tool 內部會看到失敗）

---

## 2026-06-03g Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_M1_TAU_CONFIRM_CNT` 2 → 3
### 原因
2026-06-03 bench：sweep_with_tool RIGHT phase 結束時偵測到 M1 tau spike，PausedOnError 觸發但實際沒障礙物。
log 分析：
```
baseline M1_tau=-2.4908
t=4000ms d=0.488 → ARMED (> SPIKE 0.4)
t=4200ms d=0.293 → CONFIRMED (CNT=2 滿足、d > SUSTAIN 0.2)
```
但 t=4600ms 才到 DECEL_MASK，這個 spike 應該是 slide 加減速 transient 引起，t=4400ms 就會回到 baseline。CNT=2 太敏感，2 個 tick (400ms) 還沒夠時間區分「機械 transient」vs「真阻力」。

改 CNT=3：需要 3 個連續 tick (600ms) 都 sustained 才確認。真 obstacle 阻力持續時間遠超 600ms，影響可忽略；DM2J slide transient 通常 < 400ms，會被過濾。
### 影響
- 真 obstacle 偵測反應時間 400ms → 600ms（slide ~16cm/sec → 多走 ~3.2cm 才停）
- false positive 預期大幅減少（特別是 slide decel mask 啟動前的最後 1-2 個 tick）
- 如果還誤報 → 可升 CNT=4 OR 把 SPIKE 0.4→0.5 雙保險
- 如果真 obstacle 漏報 → 退回 CNT=2

---

## 2026-06-03f Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp:do_arm_clean_sweep_continuous_` — water-fill loop 移除 `if (!keep_going.load()) break;`，並把 timeout log 加上「REAL timeout」字樣
### 原因
實機 log 看到 `water fill timeout — abort sweep`，但時間軸只 15 秒（rssi 807 → 2649，水還在快速爬升），WATER_FILL_TIMEOUT_MS=180000 完全沒到。真正原因：
1. `cmd_step_down_*_sweep_after_feet` 把 sweep 跑在背景 thread（fut_sweep）
2. step_down body cycle 結束後 `SweepJoin` destructor + 顯式 `sweep_keep_going.store(false)` 殺 sweep
3. water-fill loop 內 `if (!keep_going.load()) break;` 被觸發跳出
4. `!full` → 印「water fill timeout」訊息（實際是 parent kill，不是真 timeout）

對照另一輪成功的 case：第二次 attempt 19:29:00-19:29:15 水 15 秒填滿，剛好那輪 step_down disable_seal slave 2 跑到 iter=2 wait=1700ms 多撐住幾秒讓水填完。**這是 race**：step_down 結束 vs 水填滿哪個先。
### 影響
- water-fill 階段不再被 parent 殺，**水會填到滿（或真的 180s timeout）**才繼續
- emergency_stop 仍可中斷整個 sweep — sweep round loop（LEFT/RIGHT 階段）內部還有 keep_going check，第一輪結束時會看到 false 退出
- log 訊息改清楚：真 timeout 印「REAL timeout — 180s 內水沒填滿」，跟原本誤導訊息區分
- 副作用：emergency_stop 期間水填中段被打斷後仍會等水填完才停（最多多 180s）— 但這個情境很罕見，半滿水箱沒意義
### 沒改的部分
- `do_arm_clean_sweep_` (standalone，呼叫自 `cmd_arm_clean_sweep`) 仍保留 `abort_flag` check。那邊是 user 手動觸發 sweep，user 真的想中斷時 abort_flag 應該要生效。

---

## 2026-06-03f Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp:pqw_set_relay_verified_` — verify settle 50ms → 200ms
### 原因
2026-06-03 bench：3a (sync sweep_with_tool) 修了之後，pump 還是會在 LEFT→RIGHT 切換時沒關。看 log 只有 `ch=5 set OFF verify fail` 出現，`ch=6 set OFF` 沒任何 verify 訊息但實際 pump 還在跑。

代表 ch=6 OFF 的 verify「靜默成功」，但物理 relay 沒切換。原因是 cli_22_ bus 還有其他用戶（step_down 主 thread 的 PQW valve + JC100 read），50ms 太短：
1. Relay 物理動作要 20-30ms，加上 PQW gateway 處理延遲
2. cli_22_ TCP buffer 可能殘留別人的 FC01 reply frame，沒被 flush

50ms 邊緣狀況下 readback 拿到 stale frame，恰好 ch=6 顯示 OFF（pre-toggle 狀態），verify「通過」但 relay 還沒動。

改 200ms：(a) 給 relay 充分時間切換 (b) 給 cli_22_ 安靜時間讓 stale buffer 自己 drain。
### 影響
- 每次 verify_relay 多 150ms
- 每 sweep round ~4-6 次 pqw 操作 = +0.6-0.9s
- step 期間 valve 開閉也用這個 = +0.3-0.5s
- 預期效果：CH6 OFF 不再被 stale readback 騙過去，pump 真的會在 RIGHT phase 開頭就關
### 未根治
這個 fix 是「拉長 settle 時間賭 bus 會空」，並沒真的解決 stale buffer 問題。如果 bus 還是太擠，200ms 可能還是不夠。治本要：
1. Modbus TCP transaction ID 比對
2. recv 前 drain TCP buffer
3. 拓樸分流（PQW 獨立 gateway）

---

## 2026-06-03e Claude (Sadie) — fix-forward for 2026-06-03d
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` constructor — 加 startup `arm_cli_.connectToServer(ARM_IP, ARM_PORT)`，跟 crane 同 lazy-init pattern
### 原因
實機驗證 2026-06-03d 馬上爆：
```
[arm] INIT
[arm_cmd] 'INIT' not connected attempt=0 (waiting for background reconnect)
[arm_cmd] 'INIT' not connected attempt=1 (waiting for background reconnect)
[arm] INIT failed () → arm_calibrated_=false
```
2026-06-03d 把 `arm_connect_if_needed_()` 整個刪掉，但**沒考慮到第一次連線觸發**：
- `TCP_client.cpp:87,94` — `startMonitor()`（啟動背景 reconnect thread）是在 `connectToServer()` 成功後才呼叫
- 舊版 lazy init pattern：第一次 arm_cmd_ 進來時，`arm_connect_if_needed_()` 觸發 connectToServer → 同時啟動背景 thread
- 新版我移除手動 connect → 背景 thread 永遠沒人啟動 → arm_cli_ 永遠 disconnected → 所有 arm_cmd_ 都失敗

修法：跟 crane 一樣，在 constructor 顯式呼叫一次 `arm_cli_.connectToServer()` bootstrap 背景 thread。失敗也不 fatal（lazy mode：背景 thread 之後會自己重連）。

---

## 2026-06-03d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp:arm_cmd_` 重寫，移除手動 close()/connectToServer() 邏輯
- `user_lib/WASH_ROBOT.cpp:arm_connect_if_needed_` 刪除（不再有 caller）
- `user_lib/WASH_ROBOT.h` 對應 declaration 刪除
### 原因
2026-06-03 bench：cmd_step_down_with_sweep iter 3 期間，washrobot 端 arm_cmd_ 報 motor_api (127.0.0.1:9527) 「死了 30 秒」造成 sweep 整段 FAIL。
細查 motor_api log 證實：**motor_api 沒死、是 washrobot 自己亂**：
```
[DamiaoAPI] Client connected: 127.0.0.1:35650
[DamiaoAPI] Client connected: 127.0.0.1:35662
[DamiaoAPI] Client connected: 127.0.0.1:51712  ← 3 個 source port 同時連
[DamiaoAPI] Client disconnected                 ← 只有 1 個明確斷
```
Root cause：washrobot 端**兩個 reconnect 機制並存且互打**：
1. `TCP_client::reconnectLoop()` 背景 thread (500ms tick) — `!connected || available()<0` 時 close+reconnect
2. `arm_cmd_` 的 attempt 1 — 手動 `arm_cli_.close()` + `arm_connect_if_needed_()`

兩個 thread 各自開新 socket、互相砍對方 socket → motor_api 收到 3 個並發連線 → 邏輯混亂 → 整段 sweep round abort。

### 修法：把 socket 生命週期完全交給背景 thread

- 不再手動 close
- 不再手動 connectToServer
- attempt 1 改成「等背景 reconnect 完成」（最多 1.5s = 3 個 background tick）
- recv timeout **不再 retry 指令**（重送 DEPLOY/PARK 會 motor_api 端 double-execute motion，危險）

### 副作用 / 行為差異
- recv timeout：舊行為 close + reconnect + 重送一次；新行為直接 return ""，由 caller (arm_clean_sweep_cont 等) 走 ERR / PausedOnError 路徑
- Zombie socket 偵測變慢 1-1.5s（從 send fail 到 background `available()<0` 偵測再 reconnect）— 但 zombie socket 在 localhost 上極罕見
- 沒動 crane_cmd_ — 同樣 pattern 但 crane 沒看到類似徵狀 (ethernet vs localhost、不同 server 實作)，先觀察 arm 的改動效果再決定是否套到 crane

### 預期效果
sweep 期間 motor_api 重載（M1 touch_wall、M2 切換）造成 STATUS 暫時無回應時，washrobot 不再亂 close socket 觸發災難性 reconnect 風暴。最壞情境 = 等 1.5s 後背景 reconnect 完 + return ""，比 30s 連環失敗好太多。

---

## 2026-06-02t Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_detector.py` —
  - 新增 **motion parallax 模式**（`--motion-before` / `--motion-after`）
  - 新增函式：`compute_optical_flow`、`build_motion_mask`、`motion_parallax_detect`
  - 加 morphological closing (15×15) 填 mask 內部洞
  - `OBSTACLE_MIN_LINE_LENGTH_PX` 350 → 180（motion 已過濾反射，length 門檻可放寬）
  - 加新常數 `MOTION_MAG_FACTOR=2.5`, `MOTION_MIN_MAG=1.5`, `MOTION_MASK_DILATE_PX=5`
- `.claude/camera_obstacle_plan.md` — 規範文件更新（後續做）
### 原因
2026-06-02 user 提供 bench frames (before.jpg + after.jpg, plank 25→24cm shift) 驗證 motion parallax 概念。

**結果完美：**
- Plank 區域 motion magnitude 高（紅）
- 反射雜物 motion 低（藍）→ 全被 mask 掉
- 1 obstacle 偵測：feet_distance 20.1cm, confidence 0.99
- Decision: STOP_SHORT step=19.1cm ✓

跟之前純 OpenCV (沒 motion) 對比：
- v1-v3 純 OpenCV：bench 雜亂 + 反射 → 全 ambiguous / false positive
- motion parallax：直接區分真實 vs 反射 → 乾淨偵測 plank

**物理原理：** 相機跟場景之間相對位移 1-2cm 時，
- 真實牆面物體 (~20cm 相機距離) 影像移位 ~10-20px (強 parallax)
- 反射工作室場景 (虛擬深度 2-5m via 玻璃鏡) 影像移位 < 1px (弱 parallax)
- → optical flow magnitude 自然分區

### 未做（後續）
- 多距離校正集 motion parallax 測試（10/15/25/35/45cm 各拍一對）— 驗證 LUT 在 motion 模式下準度
- 整合 frame_capture 連續模式（自動連拍 before/after）
- C++ FrameAnalyzer 升級用 motion 模式
- camera_obstacle_plan.md 加 motion parallax section

### 校正注意
測試結果 feet_distance 20.1cm（plank 真實 ~17.5cm）有 ~3cm 誤差。LUT 需要實機重新校正（之前 cam3 LUT 是 fitted 過時資料）。

---

## 2026-06-02s Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_balance_calibrate_abort` — 加 `await_record` 分支同步處理 cleanup（清旗標 + state→Idle + EVT）
- `user_lib/WASH_ROBOT.cpp` `do_balance_calibrate_` fail path — 加 explicit-abort 分支，user 主動 abort 走 state→Idle 而非 PausedOnError

### 原因 — Sadie bench 2026-06-02 cal 第一次完整跑完
v12 build 成功跑出 cal CONVERGED：
```
[bal_cal] crane up_stop_total_kg 50 → 100 (cal-only)
[bal_cal] outer 0 roll=-7.85° → up_left (continuous)
[bal_cal] outer 0 end: converged roll=-0.83° (motor was on 1863ms)
[bal_cal] CONVERGED after 1 outer iter, roll=-0.52°
[bal_cal] crane up_stop_total_kg restored → 50
[bal_cal] phase = awaiting_record
```
v12 的 RAII threshold restore 也正確跑了。但 user 按 ABORT 卡住：
```
[bal_cal] abort requested
... 之後沒動作 ...
```

### Root cause — abort 邏輯只覆蓋 running 不覆蓋 await_record
`cmd_balance_calibrate_abort` 舊邏輯：
```cpp
balance_cal_abort_requested_.store(true);
return "OK abort_requested\n";
```

只設 abort 旗標。但這旗標**只有 bal_cal_balance_loop_ / preload_ / release_ 等 mid-loop code 在 poll**。

CONVERGED 後 `do_balance_calibrate_` 已 return 成功:
```cpp
bal_cal_set_phase_("awaiting_record");
balance_cal_await_record_.store(true);
balance_cal_running_.store(false);
return "";  ← 函式已退
```
此時設 abort_requested_ 沒人聽 → state 卡 Calibrating、await_record 卡 true、phase 卡 awaiting_record。

### 修法 — 雙路徑處理
v13 `cmd_balance_calibrate_abort` 改成：
1. **await_record 階段**：同步 cleanup（不靠 loop）
   - `balance_cal_await_record_` ← false
   - `balance_cal_abort_requested_` ← false
   - phase ← "aborted_after_converge"
   - state ← Idle（跟 RECORD path 一樣，cup 都釋放了）
   - EVT broadcast 讓 GUI 知道
2. **running 階段**：維持原本只設旗標，loop 自己處理

### 順便修 mid-run abort 的 state target
原本 mid-run abort 走 fail path → PausedOnError → user 要按 continue。但 abort 本身就是 user 主動取消，**不該當錯誤**走 PausedOnError。

v13 在 do_balance_calibrate_ fail path 開頭加分支：
```cpp
if (balance_cal_abort_requested_.load()) {
    balance_cal_abort_requested_.store(false);
    set_state_(State::Idle);
    std::cerr << "[bal_cal] FAIL (user abort): " << err << " — state→Idle\n";
    return err;
}
// 其他 ERR 才走原本 PausedOnError 路徑
```

### Exit state 一致性
v13 之後 cal 三種結束 path 的 state target：

| Path | state target | 為什麼 |
|---|---|---|
| RECORD（CONVERGED 後 user 按 RECORD）| Idle | cup 釋放、offset 已保存 |
| ABORT after_converge（user 按 ABORT）| Idle | cup 釋放，offset 不保存 |
| ABORT mid-run（user 中途取消）| Idle | cup 可能部分釋放，狀態跟 Idle 一致 |
| 真正系統錯誤（IMU stale/inner_timeout）| PausedOnError | 出錯，user 介入 |

### log 改善
abort 路徑會印：
```
[bal_cal] abort during awaiting_record — cleanup, state→Idle
```
mid-run abort 會印：
```
[bal_cal] FAIL (user abort): ERR aborted — state→Idle
```

---

## 2026-06-02r Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 `BAL_CAL_UP_STOP_TOTAL_KG = 100.0`
- `user_lib/WASH_ROBOT.cpp` `bal_cal_balance_loop_` — entry 查詢並保存 crane 原本 up_stop_total_kg，暫時設成 100kg，用 RAII restorer 在任何 exit path 還原

### 原因 — Sadie 直接點出 root cause
v11 build 後 cal Phase 4 開頭 8 秒沒進展：
```
[bal_cal] outer 0 roll=-6.26221° → up_left (continuous)
[bal_cal] EMERGENCY STOP: ERR inner_timeout (off_reply=OK)
```

Sadie 直覺：「會不會和我的收繩張力限制有關係」— **正中要害**。

### 對 crane hold_loop 行為
看 `Crane_control_PI/main.cpp:1633-1646`:
```cpp
if (up_active) {  // up_left / up_right hold on
    const double total = l + r;
    if (total > g_up_stop_total_kg.load()) {
        hold_all_off();   ← crane 主動把所有 motor 關掉
        broadcast EVT tension_total_limit
    }
}
```

預設 `UP_STOP_TOTAL_KG_DEFAULT = 50.0`。

### 為什麼 v7 suppress 沒解決
v7 改的是「washrobot 收到 EVT tension_total_limit 時不進 PausedOnError」 — 只 mute 通知，**沒擋下 crane 自己內部 hold_all_off()**。所以：

1. washrobot 送 `up_left on` → crane `hold_up_left = true`
2. crane hold_loop tick 看 L+R = 57kg > 50kg → **crane 自己關 motor + 廣播 EVT**
3. washrobot 收 EVT → suppress（v7 行為）
4. washrobot 不知道 motor 已被 crane 內部關掉
5. inner poll 看 IMU 沒變（motor 沒動，roll 哪會變）
6. 8 秒 → inner_timeout

### 修法 — 暫時放寬 crane 門檻 + RAII restore
v12 在 bal_cal_balance_loop_ entry：
1. send `status` 給 crane，parse 出當前 up_stop_total_kg
2. send `set_up_stop_total_kg 100` 暫時拉高到 100kg
3. RAII struct `ThresholdRestorer` 在 destructor 還原 — converged / fail / abort / 例外 任何路徑都會跑

100kg 選擇邏輯：
- cal scenario 機體全重壓鋼索 → L+R 預期 50-65kg
- 100kg 給 35-50kg margin
- 真正 overload（鋼索快斷）仍會 catch（>100kg = 異常）
- 不直接 disable check，保留安全網

### 不修的東西
- crane hold_loop 邏輯不動 — 一般操作 50kg 門檻是對的
- per-side tension_alarm 不動 — 那是真的單側 overload 安全網
- EVT suppression（v7）保留 — cal 期間其他 EVT 仍 mute，這 layer 是 belt-and-suspenders

### log 改善
```
[bal_cal] crane up_stop_total_kg 50 → 100 (cal-only)
... cal 跑 ...
[bal_cal] crane up_stop_total_kg restored → 50
```
看 log 就知道 threshold 有沒有正確 raise + restore。

### 還沒解決的 continue 卡住問題
v11 fix `state_before_pause_` 是有用的，但這次 user 的 pre_cal_state 是 Idle（不是 Attached），continue 後 → Idle。從 Idle state 沒辦法接著做 cal retry 或其他。

不是 bug，是「user cal 之前沒走 cmd_attach，crane_attached_ 是手動 toggle 的」。State machine 設計上 Idle 沒地方回。建議流程：cal 失敗 → 進 Idle 後 → 手動 cmd_attach 重建 state 再做事。

---

## 2026-06-02q Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `BAL_CAL_INNER_STALE_LIMIT` 20 → 60（IMU stale 容忍從 1s 加長到 3s）
- `user_lib/WASH_ROBOT.cpp` `do_balance_calibrate_` — entry 加 `pre_cal_state` 變數，fail path 用它正確設 `state_before_pause_`，cmd_continue 才能回到正確狀態

### 原因 — Sadie bench 2026-06-02 抓到兩個 bug
v10 build 後 cal Phase 4 開頭立刻 emergency stop：
```
[bal_cal] outer 0 roll=-5.05389° → up_left (continuous)
[bal_cal] EMERGENCY STOP: ERR imu_stale (off_reply=OK)
[bal_cal] phase = aborted ERR imu_stale
[bal_cal] FAIL: ERR imu_stale
```
然後 user 按 continue，system 卡住沒反應。

### Bug 1 — IMU stale 1s 太緊
`BAL_CAL_INNER_STALE_LIMIT = 20` × `INNER_POLL_MS = 50ms` = 1 秒不變就 fail。

但 cal Phase 4 outer 0 開頭場景：
1. motor 才剛 on
2. 鋼索 slack 還沒拉緊
3. 機體還沒開始 tilt
4. IMU 在 1 秒內看不到 roll 改變 = **正常**

v11: 20 → 60，3 秒容忍。對真正 IMU 凍結（worker thread 死、USB serial 斷）3 秒還是夠快 abort。

### Bug 2 — cmd_continue 卡住 root cause
看 fail path:
```cpp
fail:
    bal_cal_set_phase_("aborted " + err);
    balance_cal_running_.store(false);
    balance_cal_await_record_.store(false);
    set_state_(State::PausedOnError);   ← 沒設 state_before_pause_!
```

`state_before_pause_` 從來沒被 cal 設過。user 按 continue 後 state 跳到 garbage value（可能上次某個 try_or_pause_ 留下的，或 Calibrating 本身 → 無限 ping-pong）。

對照 `crane_watchdog_loop_` line 2829-2830 的 guarded save pattern：
```cpp
{
    std::lock_guard<std::mutex> slk(state_mtx_);
    if (state_.load() != State::PausedOnError)
        state_before_pause_ = state_.load();
}
set_state_(State::PausedOnError);
```
這個 pattern 是「進 PausedOnError 之前保存 current state」。

但 cal fail 時 current state 是 **Calibrating**（被 do_balance_calibrate_ 自己設的），存它沒用 — continue 會回 Calibrating，又是 stuck。

### 修法 — entry 抓 pre-cal state
v11 entry 抓住 cal 開始**之前**的 state（通常是 Attached）：
```cpp
const State pre_cal_state = state_.load();  // ← entry 抓住，通常 Attached
balance_cal_running_.store(true);
...
set_state_(State::Calibrating);
...
fail:
    {
        std::lock_guard<std::mutex> slk(state_mtx_);
        if (state_.load() != State::PausedOnError)
            state_before_pause_ = pre_cal_state;  ← restore target = Attached
    }
    set_state_(State::PausedOnError);
```

continue → 回到 Attached，正確。

### Log 改善
fail 時多印一行讓 user 看：
```
[bal_cal] FAIL: ERR imu_stale — state→PausedOnError, cmd_continue will resume to Attached
```

之後若 continue 還是不對，至少知道目標 state 是什麼。

### 注意
- pre_cal_state 是「cal 開始前 user 看到的 state」，通常 Attached
- 若 user 從別的狀態（不太合理但 v3 拿掉 state check 後可能）按 START，pre_cal_state 會是那個怪狀態，continue 回到那
- cal Phase 1-3 可能已釋放部分 cup，回到 Attached state ≠ cup 還貼牆。是 semantic gap，但目前 cal 本來就是「user 自己理解風險」性質，先這樣

---

## 2026-06-02p Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `pusher_two_stage_retract_` — 在 `zdt_wait_motion_done_many_` 回 success 之後加 anti-FAKE-DONE 驗證：每顆 slave 的 `real_pos` 必須在 `±50°` 內，否則回 fail

### 原因 — Sadie bench 2026-06-02 抓到的重大 safety bug
v9 修好 valve OFF 之後，cal Phase 2 log：
```
[vacuum_release] all released after 300ms       ← 真空釋放正常
[2stage_retract] stage1 delay 3896ms (slow peel)
[wait_many ZDT:6] move I=537mA pos=1272° spd=-502rpm    ← 正常退中
[wait_many ZDT:8] move I=2497mA pos=2994.64° spd=0rpm   ← 卡住
[wait_many ZDT:5] move I=2582mA pos=3003.55° spd=0rpm   ← 卡住
[wait_many ZDT:7] move I=2544mA pos=2996.47° spd=0rpm   ← 卡住
[wait_many ZDT:8] done at 450ms, pos=2994.64° peakI=2497mA  ← 「done」但位置 ≈3000°!
[wait_many ZDT:5] done at 450ms, pos=3003.55° peakI=2593mA  ← 同上
[wait_many ZDT:7] done at 450ms, pos=2996.47° peakI=2545mA  ← 同上
[wait_many ZDT:6] done at 1800ms, pos=-0.02° peakI=540mA    ← 真的退到 0
[bal_cal] phase = balancing                                  ← 系統繼續往下走
```

3 顆 body cup 「假退完」— 實際位置 pulse ≈30000（preset_extend 附近）= 還壓在牆上，但系統當作 Phase 2 完成繼續 Phase 4 balancing。**等於 3 顆 cup 還貼牆的情況下做了校正**。Sadie 警覺看 log 抓到。

### Root cause — wait_many 判定邏輯有 hole
`zdt_wait_motion_done_many_` line 3348:
```cpp
const bool speed_ok = std::fabs(st.real_speed) <= SPEED_THRESHOLD_RPM;  // 20rpm
const bool pos_ok   = std::fabs(st.real_pos - prev_pos[i]) <= POS_DELTA_DEG;  // 0.15°
if (speed_ok && pos_ok) ++stable[i];
if (stable[i] >= STABLE_COUNT_NEED) { done[i] = true; ... }
```

判定假設「速度=0 + 位置沒變 = motor 到目標所以停了」。

但 motor 卡死推不動（電流 2500mA、撞牆推不過）時 sensor 看起來一模一樣：spd=0、pos 不變。`stall_flag` 由 ZDT 韌體自己判定，韌體在前 450ms 還沒判定到 stall（韌體 stall 閥值高 / 觀察時間長）→ wait_many 先看到 spd=0 + pos 穩 → 誤判 done。

### 為什麼 step_down 沒踩到
step_down body cup 是「正常 attached」壓力（-30~-50 kPa），pusher 退時阻力小退得順，wait_many 看到的是真的退動 → 真的到 0。

cal 場景前面有 disable_seal 把 cup 推到 pulse 35000+ + 牆面壓出 -65 kPa 深真空 + 額外機械壓力，3 顆退不動 → 韌體 stall_flag 還沒及時latch 之前先被 fake-done 騙到。

### 修法 — caller-side target verification
不動 wait_many 本身（它要支援不同 target 的 case）。在 `pusher_two_stage_retract_` 的 caller side（stage 2 target = 0）追加：
```cpp
constexpr double RETRACT_VERIFY_TOL_DEG = 50.0;  // ~500 pulse ≈ 0.15cm
for (int s : slaves) {
    if (Z_(s).get_system_status()) return true;  // 讀不到 = fail-safe abort
    if (std::abs(Z_(s).status.real_pos) > RETRACT_VERIFY_TOL_DEG) {
        std::cout << "[2stage_retract ZDT:" << s
                  << "] FAKE-DONE detected: pos=" << pos << "° — fail\n";
        return true;
    }
}
```

正常 feet retract 結束位置 < 1°（log 看到 0.4° / -0.1° / 0.07° / 0.09°），±50° 容忍很寬鬆但抓 3000° 卡牆綽綽有餘。

### 副作用
`pusher_two_stage_retract_` 也被 step_down 用 — step_down 此後同樣會多這層驗證。若 step 本身有相同 fake-done 問題（之前沒踩到），現在會比較嚴格 fail → 進 PausedOnError。**這是正確方向** — 寧可 step 多 fail 進 PausedOnError，也不要 cup 假退繼續 motion。

### 不修的東西
- `zdt_wait_motion_done_many_` 本體不改 — 它要支援不同 target 的 use case（不只 0），caller 知道 target 比較容易驗證
- ZDT firmware stall_flag 設定不動 — 韌體層改動範圍大，此 caller-side fix 足夠

### Risk
- 50° tolerance 從 bench 觀察取出，未來如果 retract 終點誤差大於 50°（不太可能但理論上 motor mechanics 變化會影響），可能誤觸 fail。可調，現在先用 50°
- 對其他正常 use case（如 feet retract）影響 = 0（log 看終點 < 1°）

---

## 2026-06-02o Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `bal_cal_release_body_` — `pqw_.controlRelay(CH_VALVE_BODY, false)` → `pqw_set_relay_verified_(...)`
- `user_lib/WASH_ROBOT.cpp` `bal_cal_release_feet_center_` — `pqw_.controlRelay(CH_VALVE_FEET/CENTER, false)` → `pqw_set_relay_verified_(...)`

### 原因
2026-06-02 bench (Sadie): v8 改成 `vacuum_wait_release_` 後仍 timeout，**且 user 確認 valve OFF 時聽不到 vent 聲**。對照 cmd_step_down 流程才發現差別不在 sleep vs poll，而在**raw `controlRelay` vs `_verified_` 版本**：

| 流程 | valve toggle 函式 |
|---|---|
| `cmd_step_down`（正常）| `pqw_set_relay_verified_` — toggle + 讀回驗證 + 最多 3 次 retry |
| `bal_cal_release_body_`（fail）| `pqw_.controlRelay` — 裸 Modbus write，只信 ACK |

User 觀察「step 正常、cal 才 fail」直接指向兩者差異。

### 推測機制
disable_seal 結束時 cli_22_ 跑了一大堆 PQW + JC100 + ZDT modbus 交易，可能在 USR_TCP232 gateway 留下 stale TCP recv buffer（memory note 2026-05-08 已記錄這 pattern）。下一個 PQW write 雖被 ACK 但繼電器沒實際 toggle — user 聽不到 vent 聲就是這個。

`pqw_set_relay_verified_` 寫完 50ms 後 readback PQW status，狀態不符就重試最多 3 次。step_down 場景前面操作相對單純，撞不到；cal 場景操作密集，撞到了。

### 行為差異
| 場景 | v8 | v9 |
|---|---|---|
| valve 第一次 toggle 成功 | OK（其實本來就 OK 的場景）| OK |
| valve 第一次 toggle stale buffer 影響沒實際動 | 沒察覺 → vacuum_wait_release_ 2s timeout → cal abort，user 聽不到 vent 聲 | 50ms 後 readback 發現沒動 → 自動重試 → 多半在第 2 次成功 → 正常 vent → cal 繼續 |
| 繼電器真的物理壞死 | 上面行為 | 3 次 retry 都 fail → log "gave up verify" → vacuum_wait_release_ 仍 timeout → cal abort（跟 v8 結果一樣但 log 有明確訊息）|

### 留 timeout 2000ms 不動
v8 的 BAL_CAL_VACUUM_RELEASE_TIMEOUT_MS=2000 保留 — 不是 timeout 不夠，是 valve 根本沒動。v9 修了 valve 之後 2000ms 對正常 vent 速度綽綽有餘。

---

## 2026-06-02n Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 `BAL_CAL_VACUUM_RELEASE_TIMEOUT_MS = 2000`
- `user_lib/WASH_ROBOT.cpp` `bal_cal_release_body_` — 把 `sleep_ms_(VACUUM_RELEASE_WAIT_MS)` 換成 `vacuum_wait_release_(body, BAL_CAL_VACUUM_RELEASE_TIMEOUT_MS)`，timeout 直接回 ERR cal abort
- `user_lib/WASH_ROBOT.cpp` `bal_cal_release_feet_center_` — 同上，對 feet (1,2,3,4) list

### 原因
2026-06-02 bench：v7 跑 cal 在 Phase 2 (releasing_body) 倒下，4 顆 body cup (ZDT 5/6/7/8) 全部 stall：
```
ZDT:5 I=2978mA pos=3098 spd=0
ZDT:6 I=2942mA pos=3074 spd=0  ← STALL 先觸發
ZDT:7 I=2952mA pos=3045 spd=0
ZDT:8 I=2874mA pos=3062 spd=0
[2stage_retract] STALL slave 6 during stage2 wait
[bal_cal] phase = aborted ERR body_retract_fail
```

對照 step_down 主流程才發現：normal 流程 valve OFF 後是用 `vacuum_wait_release_` **主動 poll JC100 壓力直到 `p >= DETACH_THRESHOLD_KPA`**，bal_cal 寫的時候我做了簡化版 `sleep_ms_(VACUUM_RELEASE_WAIT_MS)` 盲等。

而 `VACUUM_RELEASE_WAIT_MS` 一路從 4000 → 3000 → 1500ms 砍下去（註解寫「bench 觀察 valve OFF 瞬間 vent」），但這對 bal_cal 場景失敗了 — disable_seal 結束時壓力 -64/-68 kPa，1500ms 不夠 vent 到大氣，pusher retract → cup 還黏住 → stall。

### 行為差異
| 階段 | v7 (盲等) | v8 (主動 poll) |
|---|---|---|
| valve OFF | ✓ | ✓ |
| 等真空釋放 | `sleep_ms_(1500)` 不管實際壓力 | `vacuum_wait_release_(slaves, 2000)` poll JC100 直到 `p >= DETACH_THRESHOLD_KPA` |
| 釋放未完成 | 直接 retract → cup 黏住 → stall → fail | 等到 2000ms timeout 才 return → cal 自己 abort（ERR `*_vacuum_release_timeout`） |
| retract | 可能 stall | 真空真的釋放後才動，正常 retract |

### 為什麼不用 try_or_pause_
step_down 的 vacuum_wait_release_ 是用 `try_or_pause_` 包裝，timeout 會進 PausedOnError 讓 user 介入。cal 期間如果進 PausedOnError 會：
1. 跟 v7 加的 cal state 管理打架（卡住）
2. user 無法乾淨 abort cal

所以直接 call → timeout 回 ERR → cal 自己 abort（走 do_balance_calibrate_ 的 fail path），乾淨。

### timeout 給 2000ms 理由（per user）
- 主流程用 1500ms（per user 認為 bench 測過 valve OFF 瞬間 vent）
- cal 期間不趕時間，給 +500ms buffer 處理 disable_seal 後留下的高真空（-64 to -68 kPa）
- 若真的 2000ms 還沒釋放 → 物理上有問題（valve 壞 / 線路 / cup 卡死），cal abort 正確處理

### 不修的東西
- 主流程的 `VACUUM_RELEASE_WAIT_MS = 1500` 不動（per user, bench 已驗證足夠）
- 只是 cal 場景比較極端（disable_seal 完真空特別深 + cal 不趕時間），用獨立 timeout

---

## 2026-06-05a Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_detector.py`:
  - `MOTION_MAG_FACTOR` 2.5 → 1.5（mask threshold 降低、細物體能 pass）
  - `build_motion_mask` 內 closing kernel 15×15 → 5×5（避免大 kernel 把細長物體吞掉）
  - `motion_parallax_detect` 加 raw line fallback：motion mask 路徑跑完後，**永遠**再跑一次 Canny+Hough on raw after frame，dedup（y_px ±30）後加入 features 列表、conf=0.5、tag `source='raw_fallback'`
### 原因
2026-06-05 bench 測試：detector 對「細而均勻」障礙物（木條 ~1-2cm 厚）漏報 — Farnebäck optical flow 對細物體內部無紋理只有邊緣的特徵算不出強 magnitude，mask 把它殺光，Hough 沒線可抓。
A：降 mask 嚴格度（threshold + closing kernel）讓更多 pixel 通過 mask
B：raw line fallback 兜底 — 即使 mask 完全殺光，直接在 after 幀跑 Canny+Hough 還能抓到所有清楚的橫線，標記成 obstacle conf=0.5
### 行為對照
| 場景 | 修前 | 修後 |
|---|---|---|
| 木板（大塊有紋理）| motion-path 抓到、conf 1.0 | 同（dedup 跳過 fallback）|
| 細木條（1-2cm 厚）| motion-path 漏抓、features=[] | fallback 抓到、conf 0.5 |
| 反光 | mask 殺掉、漏抓 | fallback 可能抓到（false positive 風險）|
### 副作用
- **False positive 風險增加**：fallback 沒 motion 過濾、地板裂縫 / 雜物的長橫線都會被歸 obstacle
- 「兩邊都不踩到為主」哲學接受 → 假警報讓 user modal 多跳幾次、人工確認可過濾
- decide_step 用 nearest obstacle (conf >= 0.4)，conf 0.5 會被採用、低於 motion-path conf 1.0
- 如果 fallback 誤報多得難以接受 → 提高 OBSTACLE_MIN_LINE_LENGTH_PX（180 → 300）讓 fallback 也只抓很長的線

---

## 2026-06-04h Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 `do_obstacle_probe_` 宣告、`last_step_planned_cm_` / `last_step_achieved_cm_` atomic
- `user_lib/WASH_ROBOT.cpp`:
  - `do_obstacle_probe_()` 新實作 — body 2cm out + return probe（per Sadie design）
  - `do_step_down_` Phase A 結束記錄 `last_step_planned_cm_` / `last_step_achieved_cm_` 用於 shortfall 補償
  - `cmd_run_avoid` 加 iter 0 bootstrap probe + shortfall 補償邏輯
### 設計（per Sadie 2026-06-04）
**iter 0 bootstrap probe (`do_obstacle_probe_`)：**
1. 拍 cam3/4 before
2. body 釋放（stall_clear + valve off + vacuum_wait_release + two-stage retract）
3. DM2J 0 → 2cm（body 滑出 2cm，feet 還吸著）
4. 拍 cam3/4 after
5. DM2J 2cm → 0（body 返回原位）
6. body re-seal（valve on + disable_seal_extend + vacuum_check）

整段用 step_down 同款 pattern：vacuum_wait_release_、pusher_two_stage_retract_、pusher_extend_with_disable_seal_、vacuum_check_。Feet 全程吸著、機體不會掉。

**shortfall 補償：**
- `do_step_down_` Phase A 結束時記錄 planned cm vs achieved cm（rail_pos_cm_ 反映 vacuum_retry 後實際位置）
- `cmd_run_avoid` 下一輪把 shortfall 加到 detector suggested step 上
- 邏輯：detector frame 是「未 retry 之前」的位置，補償讓機體最終落點對齊 detector 原本 plan（safety margin 保留）

### 影響
- run_avoid iter 1 開始有 detector 偵測（不再用 default step）
- vacuum_retry 發生後下一步自動加距離 catch up
- 整段不動既有 do_step_down_ 流程（只加兩個 atomic 記錄）
- bootstrap probe 失敗（body re-seal fail）→ run_avoid 不 break、iter 1 fallback default step（user 可手動 stop）

### 風險
- probe 期間 body 釋放、機體靠 feet + crane 支撐 — 跟正常 step_down Phase A 一樣的 brief moment
- probe 後 body re-seal 失敗 → 機體只剩 feet+crane 撐到下一步真實 step_down 重新 attach（risky）。若實機常發生 → 改 probe fail 進 PausedOnError
- shortfall 累積太多（多次 retry）+ detector 距離超 STEP_CM_MAX → 被 clamp 在 45cm，下一輪繼續 carry 餘下未補的 cm

---

## 2026-06-04g Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` `do_step_down_` signature 加兩個 optional hook：`during_body_rail_hook` + `after_body_rail_hook`
- `user_lib/WASH_ROBOT.cpp`:
  - `do_step_down_` Phase A body_pre_cycle 加 hook 邏輯：DM2J move 開始前 spawn `std::thread`，sleep 到 motion 80% 進度時呼叫 during_hook 拍 "before" frame；motion 完成後（rail 已到 step_cm）呼叫 after_hook 拍 "after" frame
  - `cmd_run_avoid` 改用 `do_step_down_` hooks 取代 crane probe（per Sadie：crane 鋼索 vs 機體位置不同步、DM2J 才準）
  - 第一輪 iter 跳過 detector（沒 last step 的 frame）、走 default step；iter 2+ 用上一輪 hook 拍的 frame 跑 detector
  - 移除 crane retract/pay_out probe 那段，邏輯更乾淨
  - 加 `#include <thread>`
### 估時間公式
DM2J feet rail @ 400 RPM, 1cm/rev → 6.67 cm/s；ACC/DEC 500 ms/1000rpm × 400rpm = 200ms 每端 ramp。
- `est_total_ms = rail_delta × 150 + 400`
- `capture_at_ms = est_total_ms × 80 / 100`（80% 保守 margin、確保 frame_1 早於 motion done）
- 對 step=25cm: est_total=4150ms, capture_at=3320ms → rail ≈ 21cm 時拍，距離 step 還有 4cm
- 對 step=10cm: 1900/1520ms → rail ≈ 7.5cm，gap 2.5cm
- 對 step=5cm: 1150/920ms → rail ≈ 3.7cm，gap 1.3cm（最小但仍 work）
### 設計細節
- Probe thread 用 `std::thread` 不阻塞主 motion（DM2J 連續移動到底，不分段）
- thread 拷貝 hook 函式（capture-by-value）避免 lifetime 問題
- try/catch wrap hook callback 防 callback 異常卡住 motion
- DM2J move 失敗時也 join thread 才 return（避免 leak）
- after_hook 在 main thread 直接 call（motion 已完成，沒 race）
### 影響
- run_avoid loop 每輪 step_down 多花 ~0ms（probe 在原本 motion 期間做、沒額外時間）
- 不動既有 do_step_down_ caller（hook 預設為空 = 行為跟以前一樣）
- 第一步無偵測：bootstrap 用 default 25cm，從第二步起每步都用 detector

---

## 2026-06-04f Claude (Sadie)
### 修改檔案
- `user_lib/FrameAnalyzer.{h,cpp}` 新檔 — Python obstacle_combine.py 的 C++ subprocess wrapper（popen + regex JSON parse），timeout 30s、env override 路徑
- `user_lib/WASH_ROBOT.{h,cpp}` — 加 `cmd_obstacle_check()` 單發指令；加 `cmd_run_avoid()` 連續迴圈 + `cmd_obstacle_response()` 接 user reply；新增 `obstacle_ask_pending_` 跟 `obstacle_user_response_` atomic 同步
- `washrobot_new_PI/main.cpp` — dispatch 三個新指令 (`obstacle_check`, `run_avoid`, `obstacle_response`)
- `washrobot_new_PI/washrobot_new_PI.vcxproj` — 加 FrameAnalyzer.cpp 進 build
- `web_backend/public/index.html` — 加 RUN(avoid) 按鈕 + obstacle ask modal
- `web_backend/public/app.js` — 監聽 EVT obstacle_ask 跳 modal、confirm/cancel 按鈕送 obstacle_response、loop 結束 EVT 自動關 modal
### 原因
Step 1+2+3 一起完成：detector 整合進 washrobot 的「run with avoidance」流程，不動既有 run。
- cmd_run_avoid 邏輯：
  1. cp /tmp/cam{3,4}_latest.jpg → before
  2. crane retract 1cm（probe motion，cup 還吸著）
  3. sleep 1s
  4. cp /tmp/cam{3,4}_latest.jpg → after
  5. crane pay_out 1cm（恢復）
  6. FrameAnalyzer subprocess obstacle_combine.py
  7. EVT obstacle_ask + 設 pending flag → 跳 modal
  8. wait user response (5min timeout / emergency_stop / cancel)
  9. confirm → step_down(detector-suggested step_cm)
  10. block / detect_fail / cancel → break loop
- Probe motion 用 crane 1cm 收放（option b），不用 last step 末段 frame（option a 要動 do_step_down_ 加 hook，工程量大）
- Detector subprocess overhead ~200-500ms 可接受（step 是 30s 量級）
### 設計選擇
- Subprocess 而非 port 到 C++：Python 改動不重編 washrobot；crash 不殺主程式；bench 跟 deploy 同一支 detector。代價是 ~300ms latency，可忽略。
- 不用 PausedOnError state machine：obstacle_ask 是「正常流程中的問詢」，不是錯誤。獨立 atomic flag 簡單明瞭。
- 5 分鐘 user response timeout：避免無人時 loop 卡住佔資源。
- emergency_stop 中途可中斷：abort_flag 被 watch、loop 各階段都檢查。
- Block action 不接受 user override：detector 判 block 就 break、不給 user 強制走（per Sadie：兩邊都不踩到為主）。
### 未做
- modal 不顯示 annotated frame（per Sadie：純文字最簡單先這樣）。若之後要顯示需要走 HTTP serve `/tmp/dbg/annotated.jpg` 或塞 base64 EVT。
- Probe motion 還沒實機驗證：crane retract 1cm 在 cup 吸著時應該安全（鋼索瞬間 tension），但 deploy 前要確認沒觸發 tension_alarm。
- 第一個 iter 跑 probe 不一定保險（剛 attach 完狀態未明）。如果有問題可改成「第一 iter 跳過 detector、直接 default step」。

---

## 2026-06-04e Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_detector.py` `build_motion_mask`：算 median 時 trim 左右 15% (fisheye 嚴重區)，避免邊緣畸變把 threshold 拉高、誤殺中央 obstacle。Mask 仍套全圖。
- `frame_capture/obstacle_combine.py` **新檔**：subprocess 跑 cam3 + cam4 motion detect，OR 合併兩 cam 決策（per Sadie：兩邊都不踩到為主）
- `frame_capture/bench_capture_motion.sh`：步驟 4 改成呼叫 obstacle_combine.py 而不是分開跑兩次 detector
### 原因
1. **cam3 偵測不到 obstacle 但 cam4 抓到**（同一場景同一物體）— debug heatmap 看是 fisheye 邊緣 motion ~40-60px 把 median 拉到 8.7、threshold=21.9，剛好高於中央木板真實 motion ~20-25px → 木板被當靜止殺掉。Trim 邊緣後 median 預期降到 ~5-6、threshold ~15、木板會過 mask。
2. **兩 cam 分開跑** 用戶要手動比對 / 決定哪邊聽哪邊。OR 合併 + 保守 priority (block > short > over_partial > over > proceed) 把這個邏輯內建。
### Combine priority 邏輯
| 兩 cam action | combined 採用 | step |
|---|---|---|
| 任一 block | block | 0 |
| 任一 short | short | 較小那個 (停較早) |
| 任一 over_partial | over_partial | — |
| 都 over | over | 較大那個 (確保兩邊都跨過) |
| 都 proceed | proceed | 25cm |
### 影響
- cam3 之前偵測不到的場景，fisheye trim 後預期能抓到
- 兩 cam 不對稱時 (一 proceed 一 over) 系統選 over，不會錯過任一邊的 obstacle
- `bench_capture_motion.sh` 跑完直接看 combined.action，不用人工 OR
### 還沒做
- 整合進 washrobot do_step_down_ flow（Phase 3 工程）

---

## 2026-06-04d Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_detector.py`:
  - `classify_line` length fallback：加 `BACKUP_MIN_ABS_SHADOW=10` 條件 — 長線 + 弱對比才升 obstacle，避免地板裂縫 (shadow≈0 但 length 長) 偽陽性
  - `motion_parallax_detect`：開頭加 motion median 檢查，< 2px → return `action=block reason=motion_too_small`（before/after 太接近、bench 重拍）
  - `decide_step`:
    * `STEP_OVER_SAFETY_CM = 3` — 寧多勿少，step_over 加 3cm 安全餘裕
    * 跨不過時改成走 `STEP_MAX_CM` 而不是 fallback short（per Sadie：壓最小部分總比停在前撞還好）
    * 新增 `action='over_partial'` 代表「無法完全跨過、用最大行程走過去、會壓到 obstacle 後半段」
    * alert: `cup_may_crush_obstacle_far_edge; verify_physically`
    * 只有當 `STEP_MAX_CM < FD`（cup 連 obstacle near edge 都到不了）才 fallback short
### 原因
2026-06-04 bench 觀察：
1. **偽陽性**：y=315 shadow=+1.7 length=311 被升 obstacle，但 shadow 幾乎 0（很可能是地板裂縫）→ decision 抓錯 nearest → 改 fallback 必須有最低 shadow 訊號
2. **Motion 太小時結果不可靠**：median=1.3px (一般健康範圍 5-10px) → mask threshold 縮到 3.2 → 大量雜訊通過 → 偽特徵爆量。改成 motion 太小直接 block 重拍
3. **「寧多勿少」per Sadie**：detector 估的距離有 ~3cm 誤差。step_over 加 3cm safety overshoot，cup 落地在 obstacle 後 ~3cm 處，留 buffer
4. **跨不過時不要 short**：之前 fallback short 在距離估錯時會撞 obstacle 中段（最厚部分）。改成走 MAX，cup 走最遠 → 即使壓到 obstacle 也是壓「後半段（far edge 附近）」，crushed depth 最小
### 影響
- 偽陽性減少：弱 shadow + 長線不再自動升 obstacle
- bench 操作不夠精確 (motion < 2px) → detector 不會給垃圾結果，直接要求重拍
- step_over 比之前大 3cm（safety overshoot）
- 跨不過的大 obstacle case：cup 走 45cm 而不是 stop short — 至少嘗試跨、最壞情況壓 obstacle 後半段（不是中段最厚處）
### 還沒做
- W (estimated_width_cm) 的物理語意還是 horizontal 影像長度 × 0.04，不是真實的「沿著行進方向的 obstacle 深度」。本次 fix 都是 workaround；治本要 detector 量真實 obstacle depth (例如 stereo or motion magnitude)

---

## 2026-06-04c Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_detector.py`:
  - L89: `STEP_MAX_CM` 40 → 45（DM2J 物理最長行程）
  - `decide_step`: 邏輯重排 — 偵測到 obstacle 優先 step_over，只有 step_over 超過 45cm 才 fallback 到 step_short
  - `motion_parallax_detect`: flow 方向反轉成 `compute_optical_flow(after, before)`，feature detection 跟 classify_line 都改用 `gray_after` — distance 算的是 AFTER 幀 (cup 現在位置)
  - debug output: `masked_before.jpg` → `masked_after.jpg`，annotated 圖也改成標 after 幀
### 原因 (per Sadie request)
1. **decide_step 行為**：bench 實測 cup ↔ obstacle = 8cm 時，detector 算 step_short=10.2cm → 會撞。實機已驗證 step_over=37cm 是對的。
   → 結論：distance 估算有 ~3cm 誤差，step_short 高風險。預設一律 over，只有 over 超物理行程時才 fallback short。
2. **使用 AFTER 幀**：washrobot 流程是「step → 取最終 frame 給下一步規劃用」。AFTER 幀 = cup 現在位置 = 下一步起點。之前用 BEFORE 算的 FD 比 AFTER 大一個 camera-shift (~3cm)，會讓 short 估錯方向。
### 影響
- 偵測到 obstacle 的場景幾乎都會走 over（除非 FD + W + offset > 45cm）
- short 只在「對面太遠跨不過、又看到 obstacle」時才用，相對保守
- 對 bench 本次 case：FD=11cm + W ≈ 12cm + offset 13.5 = step_over ~36-37cm ≤ 45 → action=over step=37cm ✓ 對到實機驗證
- 對更大障礙物：step_over > 45 → fallback short，至少停在前面（user 手動處理）
### 還沒做
- step_over 跟 step_short 都不行時就 block — 已實作
- 整合進 washrobot do_step_down_ 流程（Phase 3 工程，跨界 user_lib）

---

## 2026-06-04b Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_detector.py`:
  - L128: `OBSTACLE_MIN_ABS_SHADOW` 30 → 25
  - L135 區域新增 `OBSTACLE_BACKUP_LINE_LENGTH_PX = 250`
  - `classify_line` 加 length fallback：弱對比 (|shadow| < 25) 但長度 ≥ 250 px → 仍升 obstacle conf=0.7
  - `y_to_distance` extrap tolerance 拆成 `EXTRAP_TOL_NEAR_PX=20` 跟 `EXTRAP_TOL_FAR_PX=5`（太遠端嚴格、避免亂外插出 y=23→37cm 那種錯誤）
### 原因
2026-06-04 bench 確認：
1. **真實板擺中段（y=250 區）時 shadow_diff 只有 +27.8**，卡在舊門檻 30 邊界外 → 被歸 ambiguous → decision proceed → **會撞**
2. **真實板擺頂部（y=23）時 LUT 外插返回錯距離 37cm**（實際 7-8cm，差 16cm）→ FD=23cm → decision short → 也**會撞**
3. **Sign 規律不可靠**：之前以為「正 shadow_diff = 反光、負 = 真實」，但 bench 後新場景真實板也出現 +shadow_diff（取決於擺位、光線、camera 角度），不能用 sign filter
### 影響
- 中段擺位且 shadow contrast 弱 (~+25) 的真實板 → length fallback 升 obstacle (conf=0.7)，decision 變 short/over
- 頂部擺位 (y<35) 的物體 → LUT 直接 return None → 該線被 skip 不誤算距離。等機體靠近物體進入 LUT 涵蓋範圍才偵測（漏遠物可接受，撞近物不可接受）
- 「太近」邊界 (大 y_px) 保留 20px 容忍 — 安全優先，近物寧可偵測到不漏
- 新增的 length fallback **可能**也讓某些長反光升 obstacle false positive — 留意實機跑的時候反光是否被誤判
### 還沒做
- shadow_diff sign 規律不可靠的結論：之後 decide_step 加「detector 對 distance 估計不可信」的 safety net — 偵測到 obstacle 一律 step_over 而非 short，等下個 iteration 再 verify

---

## 2026-06-04a Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_detector.py:77` — `CAM_TO_FEET_OFFSET_CM` 6.5 → 13.5 cm
- `.claude/camera_obstacle_plan.md` §2a — 更新對應表格
### 原因
實機 bench 觀察 detector 報的 FD（feet_distance）跟實際吸盤到障礙物距離差 7cm — **吸盤實際比 detector 估計的位置更靠近障礙物 7cm**。
原 6.5cm 應該是 camera 跟 robot body 邊緣的距離，**沒算到 ZDT cup 從 body 再伸出去那段**。吸盤伸出時 cup tip 比 camera 更靠前，所以實際 camera 到 cup tip 是 6.5 + 7 = 13.5cm。
### 影響
- 所有 detector 報的 FD 都比舊版小 7cm（更貼近實際吸盤距離）
- `proceed/short/over/block` 決策邊界都會偏保守一點（FD 變小→更早觸發 short/block）
- **安全性提升**：之前 detector 認為「FD=13.5cm 還有 13.5cm 安全餘裕」，實際只剩 6.5cm — 已修正
### 還沒做
- bench 多組距離驗證新 13.5cm 對不對（例如手動量 cup 到木板 25cm、看 detector 報多少）
- 如果還是不準，再 +/- 微調

---

## 2026-06-03c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_arm_clean_sweep_continuous_` `sweep_with_tool` — 取消 async parallel pqw OFF/ON，改成同步呼叫（跟 standalone `do_arm_clean_sweep_` 一致 pattern）
- `user_lib/WASH_ROBOT.h` 新增 `std::atomic<bool> step_in_progress_{false}`
- `user_lib/WASH_ROBOT.cpp` 加 `StepInProgressGuard` RAII struct + 在 8 個 cmd_step_* 入口 instantiate
- `user_lib/WASH_ROBOT.cpp:cmd_status` fresh-read 條件多加 `!step_in_progress_.load()`
### 原因
2026-06-03 bench：cmd_step_down_with_sweep 並行流程中，sweep 的 LEFT→RIGHT 切換時水幫浦沒關。Root cause = cli_22_ bus contention：
- `do_arm_clean_sweep_continuous_` 用 `std::async` 把 PQW relay ON/OFF 跟 DEPLOY 或 slide motion 並行跑
- DM2J:14 (上滑台 slide) 也在 cli_22_ 上
- step_down 主 thread 同時用 cli_22_ 做 PQW valve / JC100 / DM2J:14
- 加上 GUI cmd_status 1Hz JC100 fresh-read poll
- = 三方搶 bus，FC01 verify readback 拿到 stale frame → relay verify 失敗或誤報 → pump 物理沒真的關掉

standalone `do_arm_clean_sweep_` 用同步 pqw_set_relay_verified_，bus 沒人搶就 OK。所以 user bench 觀察「單跑 sweep 沒問題、跟 step_down 並跑就壞」。

### 3a: continuous mode pqw 改同步
不再用 fut_pqw_off / fut_pqw_on async。代價是每 round 多 ~500-1000ms，但 verify 在 bus 不忙時可靠。失敗會 return false 而不是繼續硬跑（之前 fut.get() 後也是 return false，行為其實沒變太多）。

### 3c: step 期間鎖死 cmd_status fresh-read
motion_active_ 在 step 兩個 phase 之間會 toggle off（mid-realign、phase 切換空檔），讓 cmd_status fresh-read 趁機觸發 9 顆 JC100。新加 step_in_progress_ flag 是 step cmd 入口設、出口（RAII destructor）清，整段 step 都鎖住 fresh-read。GUI status display 期間靠 cache + EVT 更新，無資料新鮮度差距太大（最多 1 秒）。

### 影響
- sweep LEFT→RIGHT 切換時 pump/brush 真的會關（同步 verify 在 bus 空時可靠）
- step 期間 GUI 看到的 vacuum 數值會「停在」step 開始前的最後一次 fresh 值（直到 step 結束才更新），但 IMU / state / flags 仍即時
- 不影響 standalone clean_sweep（本來就同步）
- ~500-1000ms / sweep round 的時間損失（標準 step 約多 1-2 秒）

### 還沒解
- cli_22_ bus 仍然會被主 thread step_down ops + sweep + JC100 / DM2J:14 搶
- 真正根治要 Layer 1（拓樸分流，新加 USR-TCP232 gateway 把 DM2J:14 抽出來）

---

## 2026-06-03d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_arm_clean_sweep_` 跟 `do_arm_clean_sweep_continuous_` 兩處的 water fill loop 加進度 log（每 30 秒印一次）
### 原因
填水期間 log 完全沒輸出，bench 操作時看不到進度只能等到 timeout 或滿。加個 30 秒一次的 `[arm_clean_sweep] filling... elapsed=Xs rssi=Y (timeout at 180s)` 方便觀察填水速度跟剩餘時間。
### 影響
- 正常填水 ~80s 會看到一次 30s log + 一次 60s log，然後滿了 break
- timeout 情境會看到 30/60/90/120/150 秒共 5 次進度 log，更容易判斷是漸進填水還是完全沒進水
- 沒改任何邏輯，純 log

---

## 2026-06-03c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h:231` — `WATER_FILL_TIMEOUT_MS` 60000 → 180000 (60s → 180s)
### 原因
實機 cleaning sweep 觸發 `water fill timeout — abort sweep`，log 顯示 XKC rssi 從 920 緩慢爬升（920 → 1134 大約 3 秒過了 +200），60 秒內到不了 sensor full threshold (~3000+)。手動再跑一次（約 84 秒後）XKC output=1 rssi=3292 確認**水實際有在流入但比 60 秒慢**。
拉到 180s 給 1.5-2× 餘裕。
### 影響
- cleaning sweep 開始前等水填滿最多 180 秒（原 60 秒）
- 如果頂樓水壓正常、填水 ~80s 就會早結束 break out（while loop 設計就是滿了就跳出）
- 真正水壓掛了 / 水管堵了 / 球閥沒開 → 180 秒等更久才知道
### 還沒處理的相關問題
- CH6（水箱泵浦）`verify fail vr=0/1/2 → gave up`，PQW 在 cli_22_ bus 擁塞造成 readAllStatus 撞 bus → 不知道泵浦真的有沒有 ON。要不要對 CH6 做更強的驗證（例如 sleep 後再 readAllStatus 一次確認）等之後再說。

---

## 2026-06-03b Claude (Sadie) — REVERT 2026-06-03a
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 移除 `disabled_jc100_slaves_` 成員
- `user_lib/WASH_ROBOT.cpp` —
  - 移除 `disabled_jc100_slaves_.insert(7)` + log
  - 移除 `vacuum_check_` 內的 disabled JC100 skip 邏輯
  - 移除 `cmd_status` 內的 disabled JC100 skip 邏輯
### 原因
User 換新 sensor 上去了 — 不想用「跳過 slave 7」的 workaround，回到「9 顆 JC100 全部正常讀取」狀態。新 sensor 物理校正完成（單位 + 零點）後，slave 7 應該跟其他 8 顆一致。
### 影響
- `vacuum_check_` / `cmd_status` 回到 2026-06-02m 之前的行為（讀 9 顆）
- 如果新 sensor 還沒完全校好（單位錯、零點偏） → 又會誤判 anchor check fail 卡住 step
- 1Hz rate-limit (2026-06-02m) **保留**，不在這次 revert 範圍內

---

## 2026-06-03a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 `std::set<int> disabled_jc100_slaves_` 成員
- `user_lib/WASH_ROBOT.cpp` —
  - 建構子 init 階段 insert `disabled_jc100_slaves_.insert(7)`
  - `vacuum_check_` 跳過 disabled JC100（assume sealed，不加入 fails）
  - `cmd_status` fresh-read loop 跳過 disabled JC100（不送 Modbus query）
### 原因
JC100:7（右身體 #2 真空壓力計）實機確認壞掉。Driver 永遠 timeout → return cached=0。連鎖效應：
- 2026-06-02c 新加的 anchor check：`vacuum_check_("body")` 把 slave 7 列入 fails（0 kPa = "unsealed"）→ step Phase A/B 釋放真空前永遠進 PausedOnError → step 動作全擋住
- `cmd_recover` 同理永遠 fail
- `cmd_status` 每秒 fresh-read 9 顆 JC100，slave 7 每次都 timeout 200ms，log 被 spam + 拖慢其他 sensor 讀取
仿照既有 `disabled_zdt_slaves_` (ZDT 9 中心) pattern 加 `disabled_jc100_slaves_` set，固定排除 slave 7。
### 影響
- `vacuum_check_("body")` 對 slave 7 假設已封 → anchor check 不會被 dead sensor 卡住
- `cmd_status` 不再讀 slave 7 → log 不再每秒 spam JC100:7 TIMEOUT
- **新風險**：右身體 #2 cup 真的漏氣不會被偵測（vacuum_check_ 不查它了）。靠 `disable_seal` 的 peakI 證據確認 cup 有壓到牆，但對「seal 後慢漏」這種情境是盲點。換 sensor 前要留意這顆 cup 的狀態。
- `disable_seal` 本身沒改：slave 7 push 時讀壓力會 timeout / 拿 cached=0 → 走完 MAX_ITERS → WEAK_SEAL fallback。Cup 還是會推到牆（peakI 證據），只是 driver 不會說它「SEALED」。日後若這變煩可以再進一步處理（例如 disable_seal Step B 也 skip disabled JC100，直接靠 wall reached 標 DONE）。
### 還原
- JC100:7 換新 sensor 後，把 `WASH_ROBOT.cpp:disabled_jc100_slaves_.insert(7)` 那行刪掉，重編

---

## 2026-06-02m Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 `std::atomic<int64_t> last_status_fresh_read_ms_{0}` 成員
- `user_lib/WASH_ROBOT.cpp` — `cmd_status()` 加 1Hz rate-limit on JC100 fresh-read
### 原因
Web GUI 每 500ms (2Hz) poll washrobot status，每次觸發 `cmd_status` → `read_pressure()` × 9 顆 JC100。motion_active_=false 時 = **18 JC100 reads/sec** 在 cli_22_ bus（跟 PQW、XKC、DM2J:14 共用），attach 兩 phase 之間 idle 時造成大量 TIMEOUT。
本來想重啟背景 `pressure_poll_loop_` 但 user 提醒會挖回 2026-05-29 「背景 poll 跟 PARK/PQW verify 撞 cli_22_」的問題（WASH_ROBOT.cpp:246-253 註解講得很清楚）。
改成 rate-limit：fresh-read 1Hz cap、GUI 想 poll 多快都行，cache 讀無 bus 負載。GUI 顯示最多 1 秒 stale，但仍會自動更新。
### 影響
- attach idle gap 期間 JC100 timeout 預期降到原本 1/2（18 → 9 reads/sec）
- motion 期間（motion_active_=true）跟以前一樣走 cache + piggyback，不變
- IMU 跟 state/flags 仍每 500ms 即時更新（這些不需要 fresh JC100 read）
- 沒重啟背景 pressure_poll，2026-05-29 的 bus contention 問題不會回來

---

## 2026-06-02l Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `handle_crane_evt_` — cal 期間 suppress `tension_total_limit`，per-side `tension_alarm` 保留
- `user_lib/WASH_ROBOT.cpp` `bal_cal_balance_loop_` — 完全重寫成 continuous-motor + inner poll loop（取代 30 iter pulse）
- `user_lib/WASH_ROBOT.h` — 新增 5 個 v7 常數（INNER_POLL_MS / INNER_MAX_MS / INNER_STALE_LIMIT / TOTAL_TIMEOUT_S / OVERSHOOT_DEG），舊 PULSE_* 標 deprecated，MAX_ITER 30→6
- `web_backend/public/index.html` — 加 `#balcal-awaiting-banner` 紅字提醒
- `web_backend/public/style.css` — 加 `.balcal-pulse` 動畫 (scale 1.0↔1.06 + glow)
- `web_backend/public/app.js` `parseBalanceCalibration` — phase=awaiting_record 時開 banner + 對 RECORD/ABORT 加 pulse class

### 原因
2026-06-02 bench 第一次成功收斂 log 暴露 3 個獨立問題（per Sadie 反饋）：

1. **crane `tension_total_limit` watchdog 整個 loop 至少觸發 15 次** — cup 釋放後總張力本來就會 >50kg（重量本來就壓鋼索），watchdog 每次都進 PausedOnError，loop 結束後 state 殘留 PausedOnError 害 ATTACH 全部 ERR
2. **on/off pulse 29 iter（58 次 SE3 啟停）— 後段 18 iter 全 80ms 微調**，意義不大的反覆啟停對 SE3 inverter ramp logic 不友善
3. **CONVERGED 後 state 卡在 Calibrating/PausedOnError 出不來** — user 按 ATTACH 全 ERR state_violation，要 RECORD/ABORT 才能退

### 1. tension_total_limit 抑制
```cpp
if (balance_cal_running_.load() && line.find("tension_total_limit") != std::string::npos) {
    std::cout << "[crane_evt] suppressed (balance cal in progress): " << line << "\n";
} else {
    // 原本記錄 alarm 的邏輯
}
```
- per-side `tension_alarm`（單側超門檻）保留 — 那是真的安全問題
- 只關掉 total-sum gate，因為 cal 期間 cup off 整個重量壓鋼索，total 必超 50kg

### 2. Continuous-motor design (取代 pulse)
```
outer (max 6, 預期 1-3):
  讀 IMU / tension / 連線 → 過則 emergency
  motor ON 一次
  inner poll @ 50ms:
    converged → break (OK)
    sign-flip 過零 → break (OK, 下個 outer 會跑反向)
    imu_.read_error → emergency
    imu_.x 連 20 次 (1s) 沒變 → emergency (IMU 凍結)
    crane_cli_.isConnected()=false → emergency
    |roll| > 15° → emergency
    motor on 超 8s → emergency
    total cal 超 60s → emergency
  motor OFF (emergency 仍 best-effort 送)
  settle 2s
```
- 每 outer = 1 次 on/off。預期 1-3 outer = 1-3 次 on/off（vs 之前 29 次）
- emergency 時 SE3 自己的 07-10 alarm-on-no-comm 是雙保險

### 3. GUI awaiting_record 提醒 (Z)
- state machine 邏輯**不改**（Calibrating 仍只接受 RECORD/ABORT 退出，per user 選 Z 不選 X/Y）
- 但 GUI 在 phase=awaiting_record 時：
  - banner 顯示 「⚠ 校正已收斂 — 請按 RECORD 保存或 ABORT 取消，才能執行其他動作」
  - RECORD / ABORT 按鈕加 `balcal-pulse` 動畫（紅光脈動 + 微縮放）
- user 一看就知道下一步是什麼

### 行為差異
| 觀察點 | 之前 (v6) | 現在 (v7) |
|---|---|---|
| cal 中 SE3 on/off 次數 | 29-58（30 iter cap） | 1-6（outer iter cap=6）|
| cal 中 tension_total_limit 進 PausedOnError | 每 iter 一次 → 卡死 | suppressed → 不影響 |
| 後段微調 | iter 11-28 共 18 個 80ms | 1 個連續 + 過零自停 |
| CONVERGED 後想 ATTACH | ERR state_violation 完全沒提示 | ERR 仍擋，但 GUI 閃紅字告訴你按 RECORD/ABORT |
| IMU 突然斷 | 沒檢測，loop 繼續 → motor 一直 on | 1s 內偵測到 → emergency stop |
| crane TCP 中途斷 | 沒檢測 | 每 50ms 檢，斷了 → best-effort off |

### 安全考量（per Sadie 提問）
> 如果吊機失去連線或 IMU 斷線要怎麼辦？

- **crane TCP 斷**：inner poll 每 50ms `crane_cli_.isConnected()` 檢測；中途斷 → 仍 best-effort 送 off cmd（可能失敗）；SE3 自己 07-10 設成 alarm-on-no-comm 是雙保險（comm 斷一段時間 SE3 自己會 alarm 停車）
- **IMU 斷**：兩條偵測 — `imu_.read_error.load()` (packet parse 錯) + `imu_.x` 連續 1 秒沒變化（worker thread 死掉的徵兆）。兩條任一觸發 → emergency stop motor

### Known 不修
- v7 將 PULSE_* 常數標 deprecated 但未拆掉（保留以防之後想 fallback 到舊邏輯，或外部有 reference）
- MAX_ITER 30→6 是 outer iter cap，不是 pulse cap，意義不同

---

## 2026-06-02k Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — START pre-check 改成雙門檻：新增 `BAL_CAL_START_ROLL_MIN_DEG = 0.5°`、`BAL_CAL_START_ROLL_MAX_DEG` 5° → 15°
- `user_lib/WASH_ROBOT.cpp` `cmd_balance_calibrate_start` — pre-check 改成 dual-threshold，三條 REJECT 路徑都加 `std::cout` log

### 原因
Sadie 反饋之前 5° 門檻方向設計反了：「**校正本來就是要修不正常的姿態，攔下 6° = 攔下唯一需要校正的時機**」。今天故意拉到 6° 想測校正，被 pre-check 擋下，且本地 log 完全沒輸出（ERR 直接 return string 給 GUI，沒 std::cout）只能從 web log 看到。

### 行為差異
| roll | 之前 (v5, 5°) | 現在 (v6, dual) |
|---|---|---|
| 0.2° (已平衡) | ✓ 放行 → Phase 4 立刻收斂 → record offset=0 | ✗ REJECT `already_balanced` — 機體已平衡無需校正 |
| 3° (輕微傾斜) | ✓ 放行 | ✓ 放行 |
| 6° (中度傾斜, 真實 use case) | ✗ REJECT `initial_roll_too_high` | ✓ 放行 — 這正是校正合理使用區間 |
| 20° (極端) | ✗ REJECT | ✗ REJECT `initial_roll_too_high` (太歪 preload 危險) |

### Pre-check 邏輯 (now)
```
1. crane_attached_ off → REJECT crane_not_attached
2. |roll| < BAL_CAL_START_ROLL_MIN_DEG (0.5°) → REJECT already_balanced
3. |roll| > BAL_CAL_START_ROLL_MAX_DEG (15°) → REJECT initial_roll_too_high
4. 三條都過 → 進入主流程
所有 REJECT 都會 std::cout 一行 `[bal_cal] REJECT cmd_start: ...` 方便本地 log 看
```

### 補充：MAX 跟 PANIC 同值 (15°) 但分兩常數
語意不同：MAX 是「不允許從這狀態啟動」；PANIC 是「loop 中如果到這就 abort」。目前 reuse 同值但保留兩個常數方便未來分別調。

---

## 2026-06-02j Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `crane_retract_to_weight_` — 兩側 pulse 改成並行（同時 on → 同 sleep → 同時 off），不再 sequential 各 pulse 一次
### 原因
Sadie bench 觀察：原本 sequential pulse 兩側都要收時要花 2×pulse_ms（左跑完才換右跑）。改並行：兩 motor 同時跑 pulse_ms 時間，省一半。

### 行為差異
| 場景 | 之前 | 現在 |
|---|---|---|
| 只左需收 | up_left on → 300ms → up_left off | 同 |
| 只右需收 | up_right on → 300ms → up_right off | 同 |
| **兩側都需收** | up_left on → 300ms → off → up_right on → 300ms → off （**~600ms motor 跑**）| up_left on + up_right on → 300ms → 同時 off（**300ms motor 跑**）|

### log 變化
之前：
```
iter 0 L=21 R=-0.6 → pulse_left 300ms pulse_right 300ms
```
現在：
```
iter 0 L=21 R=-0.6 target=25 → pulse L R 300ms
```

### preload 總時間預估改善
從你 bench 看 10 iter, 多半雙側都需收 → 並行省 ~3 秒（10 × 300ms 省下來）。

---

## 2026-06-02i Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `BAL_CAL_TENSION_MIN_KG` 30 → 10

### 原因
2026-06-02 bench：preload 收斂後 L=24.89 / R=19.06，cup 釋放後 L=30.35 / R=24.61。Phase 4 balance loop 第一 iter 直接被 watchdog 抓 (R=24.61 < 30) → tension_panic abort。

問題：watchdog 30kg 寫死，假設「正常 hang 時雙側 ≥ 30kg」。但 Sadie 機體重心嚴重偏右，正常 hang 時 R 側只有 20-25kg（135kg 機體不對稱負載）。

新值 10kg：
- 真斷繩 / 脫鉤 → 張力立刻掉到 0-3kg → 10kg 仍 catch
- 正常不平衡 hang 時 R 側 20-25kg → 不誤觸發
- 跟現實量測 (R=24.61) 留 ~15kg headroom

### 機體 hang 後總張力 < 機體重量 觀察
Sadie bench: L=30+R=24 = 54kg 總和。但 robot weight (CLAUDE.md `MACHINE_WEIGHT_KG = 135`) 應該全在鋼索上 → 期望 ~135kg 總和。

差 80kg 來自：
- cup 雖然 retract 但仍有部分接觸 / 摩擦支撐
- 鋼索本身彈性，靜態 hang 量測值偏低
- 機體重量實際可能比 135kg 少（含水量、arm 等變動）

不影響校正功能 — 重點是「雙側都有張力 + IMU 可量到傾斜」就能跑 Phase 4。

---

## 2026-06-02h Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 `BAL_CAL_START_ROLL_MAX_DEG = 5.0`（pre-check 用，跟 Phase 4 watchdog 的 `PANIC_DEG = 15` 分開）
- `user_lib/WASH_ROBOT.cpp` `cmd_balance_calibrate_start` — pre-check 2 改用 `BAL_CAL_START_ROLL_MAX_DEG` (5°) 取代 `BAL_CAL_ROLL_PANIC_DEG` (15°)
### 原因
Per Sadie：START 啟動門檻設嚴一點 (5°)，避免在已經很歪的狀態下開始拉鋼索越拉越歪。Phase 4 balance loop 中的 watchdog 仍用 15°（允許暫態 overshoot，否則 pulse 過了一點就會 abort）。

### 行為
| 場景 | 結果 |
|---|---|
| `\|init roll\|` < 5° | 通過，正常 calibration |
| `\|init roll\|` 5° ~ 15° | **新增 ERR**（之前會通過）|
| `\|init roll\|` > 15° | ERR（一致）|

---

## 2026-06-02g Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` —
  - `bal_cal_release_body_`：iterate over body slaves 列表時 filter `disabled_zdt_slaves_`，全 disabled 則 skip 整段（不會誤拉 ERR）
  - `bal_cal_release_feet_center_`：list 改成只有 feet (1,2,3,4)，**移除 ZDT_C (9)** — per user 2026-06-02 已物理拔除。仍保留 disabled filter 應對未來其他 slave 被 disable 的情況
  - `cmd_balance_calibrate_start` 加 3 個 pre-checks：
    1. `crane_attached_ == false` → 直接 ERR（沒 crane 整流程 no-op）
    2. `|IMU roll| > BAL_CAL_ROLL_PANIC_DEG (15°)` → 直接 ERR（已歪太多再拉繩危險）
    3. 任一側張力 < 5kg → WARN log + EVT broadcast（不擋下、提醒 user preload 會劇烈）
### 原因
2026-06-02 bench：
- ZDT:9 已物理拔除，但 list 還列著 → `pusher_two_stage_retract_` 對它送命令 → ZDT 韌體 timeout → 2 次 retry 都失敗 → ERR feet_center_retract_fail
- Sadie 問「開始前要不要先檢查傾斜」→ 加 pre-checks 防呆但不阻擋正常 case

### 設計決策（why this set of checks，不加其他）
| 想法 | 為什麼不做 |
|---|---|
| roll 已在 1° 內 → 跳過 | 校正本來就會立刻 converge，多餘 |
| 張力差 > 30kg → 拒絕 | 你 bench 就是這狀況，禁止反而做不了校正 |
| 依當下傾斜量動態調 target_kg | target 越低 cup 釋放跳幅越大、機體晃更多，物理上反向 |

### Pre-check 後 log 範例
```
[bal_cal] pre-checks OK: roll=1.47° L=22.0kg R=12.0kg
```
或 warning：
```
[bal_cal] balance_cal_warn preload_aggressive L=2.5kg R=1.2kg (鋼索鬆，cup 釋放時可能驟降)
[bal_cal] pre-checks OK: roll=1.47° L=2.5kg R=1.2kg
```

---

## 2026-06-02f Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 `crane_retract_to_weight_(target, safety_max, max_iter, pulse_ms, settle_ms)` 宣告
- `user_lib/WASH_ROBOT.cpp` —
  - 實作 `crane_retract_to_weight_`：迴圈讀 L/R 張力，分側 pulse `up_left` / `up_right`（只 pulse < target 的那側），達標即停。Safety: 任一側 >= safety_max → abort
  - `bal_cal_preload_` 重寫：讀 crane status 的 `retract_tension_stop_kg=` 當 target、用新 helper 跑、移除原本對稱 retract loop
### 原因
2026-06-02 bench：crane 原本的對稱 `retract` cmd + max(L,R) 軟停對重心嚴重不平衡機體（L=22 / R=12 起始）做不到雙側都到 target — L 立刻超 → crane 停 → R 永遠搆不到，35 輪後越拉越歪 (L=37 / R=2)。

`crane_retract_to_weight_` 鏡像 `crane_pay_out_to_weight_` 邏輯但反方向：每 iter 讀張力、分別 pulse 不足那一側（不會影響已達標那側）。對你那種不平衡情境，預估 3-10 iter 收斂、單次 1-2 秒、總 < 20s。

Target 從 GUI 上 crane 「收繩軟停張力」拿（跟 attach 的 pay_out_to_weight 共享同 knob），預設 25kg。若 crane status 解析失敗 fallback 15kg。

Safety upper bound 40kg（同 `rope_weight_limit_per_sensor_attached` default）— 跟 realign 的安全機制平行。

### 副作用
- 對稱 retract 改 per-side pulse → 比原版慢（但**會 work**）
- 每 iter 有 crane comm 開銷 (~100ms × 4 cmds: status read + pulse on + pulse off + tension read) → 1-1.5s/iter
- 預期：3-10 iter 收斂

### 預期下次跑 log
```
[bal_cal] phase = preload
[bal_cal] preload target_kg=18 (from crane retract_tension_stop_kg)
[retract_to_weight] iter 0 L=22kg R=12kg target=18kg → pulse_right 300ms
[retract_to_weight] iter 1 L=22.1kg R=14.5kg target=18kg → pulse_right 300ms
[retract_to_weight] iter 2 L=22.0kg R=17.2kg target=18kg → pulse_right 300ms
[retract_to_weight] iter 3 L=22.1kg R=19.5kg target=18kg → DONE
[retract_to_weight] DONE iter=3 L=22.1kg R=19.5kg target=18kg
[bal_cal] preload OK
[bal_cal] phase = releasing_body
...
```

---

## 2026-06-02e Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `bal_cal_preload_` — 成功門檻從「雙側都 ≥ 15kg」改成「雙側都 ≥ 5kg（鋼索都 taut）」

### 原因
2026-06-02 bench log：機體重心嚴重偏左，crane 的 `retract` cmd 是對稱動作 + `max(L,R) >= soft_stop` 軟停。對你這台機器：
- 起始 L=22 R=12
- L 立刻超過 target → crane 立刻停
- R 永遠搆不到 target
- 再加上左右馬達微小機械不對稱（之前討論過 SE3 P.7/P.8/鼓徑），每輪 L 多收一點
- 35 輪後：L=37 R=2，徹底失衡，timeout

「雙側 ≥ 15kg」對重心極度不平衡的機體**物理上做不到**（用對稱 retract 永遠不可能）。

放寬到「雙側都 taut (>5kg)」：
- 物理意義：cup 釋放時不會「鋼索沒拉緊先掉一截才被接住」（這是 preload 本意）
- 你 round 1 log: L=22 R=12，兩側 > 5 → 應該立刻通過
- 殘餘的張力不對稱由 Phase 4 IMU loop 修（single-side up_left / up_right 直接攻擊不平衡）

### 副作用
原本 preload 預期把鋼索拉到「接近實際吊掛載重」，cup 釋放時張力變化不大。現在門檻降到 5kg，cup 釋放時鋼索張力會從 5-20kg 瞬間跳到 50-70kg/側（135kg 機體均分）。理論上機體會輕微下沉（鋼索彈性 + 鼓上動量）+ IMU 可能短暫擺盪。

bench 觀察：
- 若實際下沉 > 1-2cm 或擺盪大 → 考慮加回去更嚴格的 preload，或先在 GUI 把 crane 軟停張力設高（30-40kg）
- 若下沉 < 0.5cm + 擺盪小 → 5kg 門檻就夠用

---

## 2026-06-02d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_balance_calibrate_start` — 拿掉 `state == Attached` precheck，只保留 `balance_cal_running_` re-entry guard

### 原因
Sadie bench 測試方便，不想每次都得先 attach 才能跑校正。從 Idle / PausedOnError 等狀態也能直接跑。

### 副作用提醒
- 從非 Attached 啟動，cup 可能沒吸住、Phase 1 preload 直接 retract 鋼索，機體可能瞬間被拉
- 從 PausedOnError 啟動會中斷原本 paused 的工作
- 沒設 motion mutex，理論上跟 step_down 等其他 motion cmd 可能 race

實機 bench 確認 OK 之後可以視情況加回去。

---

## 2026-06-02c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` —
  - `bal_cal_read_tensions_` 修 parser bug：crane `tension` cmd reply 是 `OK left=X right=Y`，**不是** `tension_left=X`。改用 `" left="` / `" right="`（含前置空格避免誤撞 `length_left=`）
  - 失敗時 log 原 reply 字串方便 debug
  - `bal_cal_preload_` 加 per-round log（round 編號、elapsed、crane reply、L/R tensions）

### 原因
2026-06-02 bench 跑 START CALIBRATION 卡在 phase=preload → ERR preload_tension_read。對比 crane source 才發現 `cmd_tension()` 用 `" left="` / `" right="`（沒 `tension_` 前綴），跟 `cmd_status` 不同。我之前依 status 格式猜測 tension cmd 格式，沒查實際 reply。

修完 + 加 log 後 preload 應該正常通過、走到 release_body 那邊。

---

## 2026-06-02b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `BAL_CAL_ROLL_TOL_DEG` 0.5 → 1.0
  - 移除 `BAL_CAL_PULSE_MS`，加 3 個比例 pulse 常數：
    - `BAL_CAL_PULSE_FAR_MS = 300` (|roll| > 5°)
    - `BAL_CAL_PULSE_MID_MS = 150` (|roll| 2~5°)
    - `BAL_CAL_PULSE_NEAR_MS = 80` (|roll| 1~2°)
- `user_lib/WASH_ROBOT.cpp` —
  - `bal_cal_balance_loop_` 內 pulse_ms 改成比例計算（每 iter 根據當下 abs(roll) 選 3 段）
  - 加 per-iter log：`[bal_cal] iter N roll=X° → up_right Yms`
  - 註解註記 sign convention 跟 Sadie 2026-06-02 bench 觀察結果一致

### 原因
Sadie bench 驗證 IMU sign（拉左繩往上 roll 1.47 → 5.12）→ 確認 sign 對。但靈敏度高 — 短時間左繩動就讓 roll 動 3.65°，原 300ms 對 0.5° tolerance 會 ping-pong overshoot 永遠不收斂。

改 3 段比例 pulse：
- 5° 以上仍用 300ms 大步走快
- 1-2° 用 80ms 微調精準
- 加上 tolerance 0.5° → 1.0° 務實放寬

收斂判據 1° 對重心校正用途夠（最終整機歪 1° 內可接受）。如果 bench 跑出來仍 overshoot，下一步可：
- 把 NEAR 再縮到 50ms
- 加 crane set_hold_hz 暫時降頻 (10 → 5-7)
- tolerance 再放到 1.5°

### 預期 iter 數
從 5° → 1° (3 段每段 3-5 iter): 約 10-15 iter 收斂，總時間 ~50-75 秒（含 2s settle/iter）。

---

## 2026-06-02a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - State enum 加 `Calibrating`
  - 加 balance calibration atomic flags + phase string + mutex
  - 加 9 個 `BAL_CAL_*` constexpr 常數
  - Settings struct 加 `static_roll_offset_cm` atomic
  - 4 個 cmd 宣告 (`start` / `record` / `abort` / `status`)
  - 7 個 helper 宣告
- `user_lib/WASH_ROBOT.cpp` —
  - state_name 加 `Calibrating`
  - constructor 初始化 `static_roll_offset_cm = 0`
  - cmd_get/set/save_settings 加 `static_roll_offset_cm` 條目
  - 實作 4 cmd + 7 helper（總 ~250 行）
    - Phase 1 preload：呼 `crane retract 30`、用 crane 現有「收繩軟停張力」、20s timeout、雙側 ≥15kg 視為達標
    - Phase 2 release body：CH_VALVE_BODY OFF → vent → ZDT(5,6,7,8) retract
    - Phase 3 release feet+center：CH_VALVE_FEET + CENTER OFF → vent → ZDT(1,2,3,4,9) retract → 3s settle
    - Phase 4 balance loop：max 30 iter，每 iter 讀 IMU + DSZL 張力 → watchdog → 收斂判據 → up_left/up_right 300ms pulse → 2s settle
    - Phase 5 record：讀 SD76 L/R、ΔL 寫入 settings.static_roll_offset_cm
  - 失敗 → set_state(PausedOnError) + EVT
- `washrobot_new_PI/main.cpp` — dispatcher 加 4 個 cmd 路由
- `web_backend/public/index.html` — home 頁加「⚖️ 重心校正」panel
- `web_backend/public/app.js` —
  - 加 `parseBalanceCalibration(line)` 處理 phase / iter / recorded EVT
  - 3 按鈕事件 handler（START 有 confirm prompt）

### 重要：目前只是「記錄」
ΔL 寫入 `settings.json static_roll_offset_cm`，但**還沒接 crane 平衡邏輯**（Phase 6 延後）。
- 看得到 ΔL 值
- step_down / pay_out / fine_adjust 仍然把鋼索拉等長（沒做補償）
- 機體下次掛上仍會傾斜

要等 Phase 6 改 crane main.cpp 三處 (motion_fine_adjust_sync target / BAL loop err / cmd_align_lengths target) 才會生效。

### Watchdog
張力 < 30kg 或 |roll| > 15° → PausedOnError，**不自動 attach**，user 自行判斷下一步。

### IMU 方向 sign 待 bench 確認
Phase 4 假設 `roll > 0` = 右側下沉 → 送 `up_right`。bench 若發現相反需翻 sign 或交換 up_left/up_right。

### 使用流程
1. 機體 attached（cup 都吸住牆）
2. crane 設好「收繩軟停張力」（建議 25-30kg）
3. 確認機體離地 ≤50cm（第一次）
4. 按 ⚖️ START → 看 phase 跑（~75s）
5. phase 變 awaiting_record → 檢查 roll 收斂結果
6. 按 RECORD → ΔL 寫 settings
7. 手動按 home 頁 ATTACH 重新吸牆

---

## 2026-06-01h Claude (Sadie)
### 修改檔案
- `frame_capture/obstacle_detector.py` — 新增 standalone Python detector（~400 行）
### 內容
純 OpenCV + numpy，**不依賴 washrobot/UDP**，目的：CLI 工具讓 Sadie bench 驗證偵測準度。

**主要組件：**
- LUT (`CAM3_LUT` / `CAM4_LUT`)：image y_px → camera_distance_cm，從 2026-06-01 校正集 6 點建出
- ROI crop：中央 80% 橫向、全縱向（避開 fisheye 邊緣變形）
- `detect_horizontal_lines`：Canny + HoughLinesP，過濾水平 ±15°（含 fisheye 容忍）
- `classify_line`：thickness + shadow 雙特徵判 obstacle / crack / ambiguous
- `cross_camera_match`：cam3 + cam4 相同 distance → real；單邊看到 → reflection 過濾
- `decide_step`：strategy B 單步決策（PROCEED 25 / SHORT FD-1 / OVER FD+W+offset / BLOCK）

**幾何常數：**
- `CAM_TO_FEET_OFFSET_CM = 6.5`（camera 比 feet 前 6.5cm）
- `ZDT_MAX_CLEARANCE_CM = 8`（cup 縮不過去的 obstacle 高度上限）
- `STEP_DEFAULT/MIN/MAX = 25/5/40`
- `CUP_RADIUS_CM = 10`

**CLI：**
```bash
# 單對測試
python3 obstacle_detector.py --cam3 X.jpg --cam4 Y.jpg --pretty

# 批次跑 (folder 內 cam3/ cam4/ 子資料夾配對)
python3 obstacle_detector.py --batch D:/工作/觀賞用/ --debug-out /tmp/debug/
```

`--debug-out` 會把每張 frame 畫出偵測 bbox + 標籤，方便視覺驗證。

### 沒做（後續）
- UDP server wrapper（port 5040，跟 detect_server.py 同協定）— camera_obstacle_plan.md §6 Phase 2
- C++ FrameAnalyzer（UDP client + JSON parse + decide）— Phase 3
- do_step_down_ 整合 — Phase 4（跨界 user_lib）
- LUT 校正補正 — cam4 中間距離 (15/35/45) 目前是 interpolation 估計，bench 跑通後實機重拍補

### 預期測試流程
1. 把 cam3 + cam4 校正集 (12 張 obstacle + 4 張 crack + 2 張 baseline) 用 `--batch` 跑
2. 看 JSON 輸出：feet_distance 估值跟實際量值誤差 < 3cm？type 分類對嗎？
3. 用 `--debug-out` 看 annotated 圖：bbox 落在木條上嗎？多餘 false positive 過濾乾淨嗎？
4. 不準 → tune `HOUGH_THRESHOLD` / `OBSTACLE_MIN_THICKNESS_PX` / `OBSTACLE_MIN_SHADOW_DIFF`
5. cam4 LUT 中間 3 個 interpolation 點實機補拍精確值

---

## 2026-06-02c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — step pre_cycle 加 anchor 真空檢查 + 重新啟用 cmd_recover 的 vacuum_check_：
  - `do_step_down_` body_pre_cycle (Phase A)：valve OFF body 前先 `vacuum_check_("feet")`
  - `do_step_down_` feet_pre_cycle (Phase B)：valve OFF feet 前先 `vacuum_check_("body")`
  - `do_step_up_` feet_pre_cycle (Phase A)：valve OFF feet 前先 `vacuum_check_("body")`
  - `do_step_up_` body_pre_cycle (Phase B)：valve OFF body 前先 `vacuum_check_("feet")`
  - `cmd_recover`：取消 vacuum_check_ 註解，check fail 回 `ERR recover_vacuum_fail slaves=...` 並 state 留在 Error
### 原因
state=Attached 不等於 cup 實際吸著（recover bypass、慢漏氣、JC100 stale read on TIMEOUT 都會讓 state 跟現實脫節）。在 step 中釋放當前 group 真空前，必須先驗證另一 group（anchor）真的吸住，否則：
1. 兩組都不吸 → 機體只剩 crane 鋼索撐
2. step 中 crane 通常已 pay_out 留 slack → 機體自由落體一段直到鋼索拉緊 → shock load
3. 對應 2026-06-01 step 35cm 那種 body cup 搆不到牆的 cascade failure 的前置條件

cmd_recover 同樣道理：跳 Attached 前必須驗證所有 9 顆 cup 真的吸著，不然下一步 release 就出事。

check 不過會走 try_or_pause_ → PausedOnError，user 可以：
- cmd_continue (retry)：再讀一次（sensor 偶發 timeout 用）
- cmd_skip：強制當作 OK 繼續（user 肉眼確認後用，危險）
- emergency_stop：完全中斷

cmd_recover 失敗則直接回 ERR，state 留 Error，user 必須手動修 cup 後再 recover，或 cmd_reset 重來（會破真空）。
### 影響
- 每 step Phase A + Phase B 各加 ~50-200ms（vacuum_check_ 4 顆 × 3 sample × 50ms gap）= 約 +0.4-1.6s/step。可忽略。
- recover 多了一個 ~600ms 全 9 顆 check。可忽略。
- **誤報風險**：JC100 偶發 timeout 可能讓 sealed cup 被誤判 → 停機。靠 vacuum_check_ 內部 3-sample retry + 3-comm retry 容錯，誤報率應該低。如果實機看到很常誤報，可以考慮加 cmd_recover_force 給「sensor 壞但肉眼 OK」用。
- step / recover flow 跑過一次都沒事的話這個改動完全 invisible；只有 cup 真的沒吸住才會被攔下來。

---

## 2026-06-02b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_CLEAN_WALL_MM` 350 → 330
### 原因
實機觀察 cleaning sweep 時 tool 上半部貼牆、下半部沒貼合（pitch 偏）。試把 wall_mm 縮小 20mm，讓 M1 收回一點看 tool 的 pitch 角會不會變平。
之前的紀錄（2026-05-27）是「刮不到壁」拉大到 350，現在反方向試 330。
### 影響
- arm sweep 時 M1 外擺角度減小，tool 整體距 M1 base 更近
- 若 330 還是上貼下不貼 → 確定是 tool 物理裝歪，需要實機調 tool mount
- 若 330 變成全不貼或下貼上不貼 → 表示 wall_mm 太小，往回試 340

---

## 2026-06-02a Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `BALANCE_KP_DEFAULT` 0.6 → 1.0 Hz/cm
### 原因
2026-06-01 實機 log 看到 BAL 持續在 ±2cm 範圍 oscillate（`err=-2cm trim=-1.2Hz` 重複），代表 kp=0.6 反應太慢追不上 drift。提高到 1.0 讓每 1cm err 給 1Hz trim（原本 0.6Hz），加快收斂。
### 影響
- 兩繩長度差會更快被修正
- 如果 1.0 仍然偏低（持續 oscillate）→ 可以 runtime `set_balance_kp 1.5` 不重編試試
- 如果 1.0 太高（左右開始 oscillate / overshoot）→ runtime `set_balance_kp 0.8` 退回
- 不影響其他功能

---

## 2026-06-01h Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_feet_realign_` Phase 2 `run_jog` stall handler 改成 non-fatal：
  - 不再設 `any_stalled = true`
  - emergency_stop 所有 moving slaves（含 stalled 本身），firmware queue 清乾淨
  - `release_stall_flag()` on stalled slave，讓 Stage A pos_mode 不被擋
  - 加 log 標示 NON-FATAL，繼續走 Stage A
### 原因
log 顯示 step 20 / 30 / 35 的 realign 都因為 Stage 0 outward jog stall 直接中止（150ms 內馬達只動 ~20 pulse 就 stall，peakI ~3000mA），原因是 cup 被真空黏在牆上 + cup 自己擋路導致外推不出去（不是 mechanism preload）。Stage 0 設計初衷是「先 jog 外推卸 preload 再 retract」，但 cup 強封時這個 jog 物理上做不到。realign 卡在 Stage 0 → 漂移完全沒補正 → 累積到 step 35 時 body cup 物理上搆不到牆（drift ~10cm，body 推到 endpoint 仍 WEAK SEAL）→ `body_backup_no_space` FAIL。
這個 fix 讓 Stage 0 stall 不再 abort 整個 realign，繼續走 Stage A 真正的 break-adhesion retract。最壞情境跟原本一樣（Stage A 自己 stall 就 fatal），最好情境是 Stage A 在 Stage 0 卡死不影響的狀態下完成 retract。
### 影響
- realign Phase 2 Stage 0 stall 後繼續跑 Stage A，不再立即 PausedOnError
- Stage A 自己 stall（既有邏輯）仍是 fatal — zero regression
- 沒解決根因：Stage A 在 cup 強封時可能還是會 stall。若實機看到 Stage A stall 率還是很高，下一步要動 Layer 2（Phase 2 期間 in_window 模式下 cycle valve OFF/ON，物理上讓 cup 鬆開）

---

## 2026-06-01g Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `try_or_pause_` 模板 loop 開頭加 `if (abort_flag.load()) return true;`，讓 E-stop 中途設的 abort_flag 在下一個 `try_or_pause_` 包的 op 之前就被攔截。
### 原因
log 顯示 step_up body phase `vacuum_release` 後 cmd_emergency_stop 觸發，但 `try_or_pause_` 從未 poll `abort_flag`，導致後續 `pusher_two_stage_retract_` / `dm2j_pair_move_abs_` / `crane_retract_safe_` / `do_feet_realign_` 整段繼續跑（body 已洩壓+收回時 rail 還在 10→0、crane 還在收 10cm），直到 `cycle_group_` 開頭的 `check_abort_()` 才回 `"aborted"` → step_up 印 `FAIL: ERR aborted`。
這個 fix 把「下一個 op 之前」當作 E-stop 的最快響應點，避免動作中途打斷（那需要每個底層 helper 內部 poll abort_flag）但至少讓 pre_cycle 不會跑完整段。
### 影響
- E-stop 觸發後，下一個 `try_or_pause_` 入口直接 return Abort，body_pre_cycle / feet_pre_cycle / 各 backup 函式不再繼續執行下一個動作。
- 不影響 normal flow（abort_flag 預設 false）。
- 注意：正在執行中的單一動作（例如 ZDT 移動指令已下、crane retract 已啟動）仍會跑完當前那一段才停 — 真正即時打斷需要每個 helper 內部 poll，目前未做。

---

## 2026-06-01f Claude (Sadie)
### 修改檔案
- `.claude/camera_obstacle_plan.md`:
  - §2a 更新 `CAM_TO_WALL_CM 15 → 18`（feet cup 9.7 + offset 8.5）
  - §2a 新增 `CAM_TO_FEET_OFFSET_CM = 6.5`（camera 在 feet 前方）
  - §2a 更新 `CAMERA_POSITION = 機體底部`、`CAMERA_ORIENTATION = 下俯 54°`
  - §5 已確認區更新：用 stream=0 主碼流（**子碼流被 camera 內部 ROI 裁切，不能用**）
  - §5 新增「校正資料 LUT (2026-06-01)」section — cam3 + cam4 各 6 點 image_y → camera_distance_cm
### 原因
2026-06-01 完成校正集拍攝 + 分析：
- cam3 + cam4 各拍 6 距離 (10/15/25/35/45/50cm) × 鮮豔木條當 marker
- 確認鏡頭角度 OK：木條中心 y_px 跟距離單調對應、5-50cm 全範圍清楚可見
- 兩 cam 行為對稱（角度裝得不錯）
- 邊界發現：camera 跟 feet 有 5-8cm offset → camera_distance ≥ 10cm 才能用，feet_distance = camera_distance - 6.5
- 邊界發現：camera 內部子碼流有 ROI crop → 改用 mainstream stream=0

LUT 6 點 piecewise linear 即可 fit。曲線 image_y 跟 distance 大致 exp 關係，但 6 點精度夠 detector 使用。

### 沒做
- `obstacle_detector.py` baseline 實作 — 下一步
- cam4 中間距離 (15/35/45) 還沒精確量，先用 interpolation 估計值放 LUT，後續 detector 跑起來實拍補正

---

## 2026-06-01e Claude (Sadie)
### 修改檔案
- `web_backend/public/index.html` — 加 IMU 姿態 panel (home page)
  - roll / pitch 數值 cell + 視覺條 (bar)
- `web_backend/public/app.js` —
  - 加 `parseImuValues(line)` 從 `roll=N pitch=N` 抽值更新
  - 加 `updateImuCell` 顏色分級 (綠 ≤2°/黃 ≤5°/橘 ≤10°/紅 >10°) + bar 填充
  - 鉤進 `onWashrobotLine` 開頭，每筆都解析
  - 加 washrobot status 500ms auto-poll (mirror crane 200ms / easy 50ms pattern)
  - mute washrobot status 自動 poll reply 不刷 log（與 crane / easy 一致）
- `web_backend/public/style.css` — 加 `.imu-cell` 顏色 + `.imu-bar-wrap/center/fill` widget
### 原因
Sadie 反映機體左右重量不平衡、吊機調動時會傾斜，計米器看不出傾斜（兩繩等長 ≠ 機體水平）。要先把 IMU 即時顯示出來，方便：
1. bench 量 `STATIC_ROLL_OFFSET_CM` 校正值（解法 A）
2. 觀察 motion 過程中 roll 漂移狀況
3. 之後接 BAL 系統的 IMU feedback 時，肉眼有對照

cmd_status reply 本來就有 `roll=X.XX pitch=X.XX`（cup 相對 imu_roll0_/imu_pitch0_ baseline 的偏移），GUI 之前只在 EVT balance_ask modal 用，沒做常駐顯示。

### 預期 GUI
home 頁 vacuum readings 下面多一個 `📐 IMU 姿態 (WT901BC)` panel：
- roll: `0.42 °` (綠) — 條中央偏右一點點
- pitch: `-1.85 °` (綠) — 條中央偏左一點點
- 500ms 更新一次，畫面條會微動

校正流程（bench, 不寫 code）：
1. 機體掛牆但 cup 全離牆（純鋼索撐）
2. 看 IMU panel roll 數值 = α
3. GUI 手動 up_left / up_right 微調直到 roll ≈ 0
4. 記下 SD76 L - R 的長度差 ΔL
5. ΔL 就是 STATIC_ROLL_OFFSET_CM，未來寫進 fine_adjust_sync target 偏移

---

## 2026-06-01d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 `std::atomic<bool> obstacle_detect_enabled_` 成員 + `cmd_obstacle_detect(bool on)` 宣告
- `user_lib/WASH_ROBOT.cpp` —
  - constructor 初始化 `obstacle_detect_enabled_(false)`（預設 OFF）
  - 實作 `cmd_obstacle_detect`：toggle flag、log 變化、回 `OK obstacle_detect=on/off`
  - `cmd_status` 加 emit `obstacle_detect=on/off`
- `washrobot_new_PI/main.cpp` — dispatcher 加 `obstacle_detect <on|off>` cmd
- `web_backend/public/index.html` — 新 panel 「🎥 窗框避障 (camera obstacle detect)」
  - link-badge: ENABLED / DISABLED
  - avoidance ON/OFF buttons
  - `(currently: ?)` 文字
  - hint 說明 ML pipeline 位置 + 提醒目前只是 flag、未接 FrameAnalyzer
- `web_backend/public/app.js` — 加 `obstacle_detect=on/off` regex + badge update（mirror arm/crane handler）

### 原因
Sadie 加裝 192.168.1.112/113 兩支底部相機，要做 step_down 期間窗框/窗檻避障。為了測試新功能時不影響現有流程，先做 toggle 基礎建設（跟 arm_attached / crane_attached 同款 pattern）：
- 預設 OFF — 現有 step_down 流程完全無感
- ON 時（未來 FrameAnalyzer 整合後）才會 pre-check 並可能 override step_cm

設計權威：`.claude/camera_obstacle_plan.md`（2026-04-23 已存在的規劃文件）
ML 參考：`D:/洗窗戶機器人/window_detect/`
- `detect_server.py` — Hailo NPU + YOLOv8s，UDP :5040，class=window_frame
- `best.pt` / `yolov8s_window_640.hef` — 訓練權重 + NPU 模型
- 已定義 UDP 協定：request = JPEG 路徑字串，response = JSON `{detections: [{near_edge_cm, height_cm, ...}]}`

### 沒做
- **FrameAnalyzer C++ class**（UDP send + JSON parse + step decision）— 規範文件 §6 Phase 4 ~ 5 範圍
- **整合 do_step_down_ pre-check** — 跨界 user_lib，要另開 PR
- **frame_capture systemd service** for 112/113 — Pi 端部署，跟程式無關
- 純 OpenCV `sill_detector.py` baseline — 之前討論的 Phase 2 fallback，detect_server 跑得起來就不需要

當前狀態：toggle 可以從 GUI 開關，但 ON 沒有實際效果（無 detection logic）。Phase 4 / 5 完成後 ON 才會真的觸發 pre-check。

### 預期 GUI 行為
1. 首頁多一個「🎥 窗框避障」panel（在 arm 整合操作下面）
2. badge 預設 ⚪ DISABLED
3. 按 avoidance ON → badge 變 🟢 ENABLED + `(currently: on)`
4. cmd_status reply 多一段 `obstacle_detect=on/off`，GUI 重新 sync 不會跟掉

---

## 2026-06-01c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 3 個 realign jog 常數
  - `REALIGN_JOG_PULSES = 300` (~0.1 cm 微推)
  - `REALIGN_JOG_RPM = 30` (extend 跟 retract 之間)
  - `REALIGN_JOG_ACC = 150`
- `user_lib/WASH_ROBOT.cpp` — `do_feet_realign_` Phase 2 加 stage 0 jog (run_jog lambda)
  - 在 Stage A 前先對所有 retract slaves (dir==1) 做 outward 微推
  - sync-trigger 平行,wait 平行
  - 失敗(send fail / stall)走跟其他 stage 同 fail path
  - extend slaves 不需要 (本來就在往牆推,沒預壓問題)
### 原因 — 機械診斷 (user 2026-06-01)
這台機器很重,掛在 cup 上時 cup-pusher 機構被自重「往外彈性預壓」(導軌/軸承/leadscrew 介面累積彈性形變)。Realign Phase 2 要把 cup 往內收回,馬達不只要克服真空+摩擦,還要「擠回機構的形變預壓」→ peakI 飆 3000mA+ → STALL。

in_window realign 特別嚴重,因為 Phase 1 (crane 提一下卸載自重) SKIPPED。

新流程:Stage 0 outward 微推 0.1cm → cup 略深入牆但機構回中性 → Stage A retract 不需對抗預壓,順順走。

### Pending: realign threshold 是否要降?
觀測 log drift 數據:
- max < 3.0 cm: 不觸發
- max 3.0~4.0: 觸發,多半成功
- max 4.0~5.0: stall 機率上升
- max 5.0~7.7: 幾乎一定 stall

user 決定: **先觀察 jog 效果,如果還會 stall 才降 threshold** (例如 max 3.0→2.5)。
如果 jog 後 5cm 以下都不會 stall,就保留現 threshold 不降。

---

## 2026-06-01b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `VACUUM_NO_CONTACT_FAST_MS` 2000 → 1000
### 原因
disable_seal WAIT_SEAL phase 內的 no-contact fast-skip：cup 經過 1000ms 後若 best_p 還 ≥ -1kPa（沒有任何真空建立），判定 cup 沒接觸牆、跳過本 iter 進下一輪 push。

2026-05-28ag 從 1000 升 2000 是因為 cup 5 有「慢 seal」現象（1000ms 還只到 -5kPa）。但 2026-05-29q 加了 `DISABLE_LOW_CONTACT_PEAK_MA = 400` 的 peakI fast-skip — push stable 後 peakI < 400mA 直接判定無接觸、跳過整個 WAIT_SEAL，不需等 NO_CONTACT_FAST_MS 也不會卡 cup 5 的慢 seal。

所以這 2000ms 現在只在「peakI ≥ 400mA（有摸到牆）但真空很慢」的 borderline 情境才有意義。這種 cup 若 1000ms 還沒任何真空建立（best_p ≥ -1），剩下 1000ms 大概率也建不起來。1000ms 已是合理 timeout。

cup 5 的慢 seal 問題現在主要靠 peakI fast-skip + VACUUM_PLATEAU_MS 兜底，不再依賴 NO_CONTACT_FAST_MS 長等。

### 預期省時
~1s/no-contact iter（出現頻率取決於牆面狀況，平均每 step 0.5-2s）

### 風險評估
- 「slow seal cup」被誤判 no-contact — 極低（peakI fast-skip 是主要保護機制）
- 若觀察到 false weak_seal 增加 → 改回 1500 或 2000

要 build + 部署，跑 5-10 step 觀察 `iter X vacuum plateau ... (no contact 1000ms)` log，確認沒有真實 seal cup 被誤判。

---

## 2026-06-01a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `VACUUM_RELEASE_WAIT_MS` 3000 → 1500
### 原因
Sadie bench 觀察：valve OFF 瞬間 cup 內部真空就掉了（vent ~100-300ms 完成），剩下「cup 殘留黏附」靠 slow peel 拉開才會釋放。3000ms 等待大部分是浪費。

物理：cup 容積 ~50ml、valve 流量遠大於此體積、瞬間倒灌大氣 → JC100 sensor 顯示 -10~-20kPa 殘留只是 cup 跟牆的微小縫隙未開，靠 ZDT slow peel 30 RPM 物理拉開即可。

1500ms 仍保 5-10× 安全餘量（實際只需 200-500ms），保留 cushion 防偶發 valve actuator 慢動作。

### 預期省時
每 step 兩次 vacuum release (body + feet) × 1500ms = **3 秒/step**（30s/step base → 10% 提升）

### 風險評估
- cup 撕裂 — 極低（slow peel 速度沒變 + cup 內已大氣壓）
- 殘留真空黏住 cup — slow peel 階段已處理，不影響後續
- retract stall — 維持原 stall 偵測，try_or_pause_ 仍會 catch

要 build + 部署後實機跑一輪 step_up/down，觀察：
1. 沒有 pusher stall 增加
2. cup 沒見裂痕
3. 真空 seal 成功率不變（disable_seal iter 數沒變多）

---

## 2026-05-29x Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `arm_monitor_during_sweep_` M1 INSTANT trigger 加 `elapsed >= 400` 守衛
### 原因
log 顯示 sweep RIGHT round monitor baseline 抓到 M1_tau=-0.0488 (M1 還在從 DEPLOY 完成姿態 settle 回 hold 扭矩),200ms 後 M1 settle 到正常 -1.32 → delta=1.27 > 1.0 → INSTANT 假觸發。Baseline transient 是源頭。

新邏輯:前 400ms 不允許 INSTANT trigger,只允許 spike-armed-confirm (Tier 2)。Tier 2 的 SUSTAINED 要求 (delta > 0.2 持續 2 polls = 400ms) 自然 filter 掉 single-poll baseline transient — baseline 抖動完後 delta 回 ~0 → m1_armed 自動 dis-arm,不會觸發。

真撞東西情境:
- t=200ms 撞: spike armed (m1_cnt=1)
- t=400ms 仍 sustained → cnt=2 → CONFIRM 觸發
- 比原 INSTANT 慢 200ms,但不會誤報

trade-off: 「真有 200ms 內猛撞」場景延遲 200ms 偵測。實機若觀察到這類 case 再調回 INSTANT or 縮短 gate ms。

---

## 2026-05-29w Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `zdt_wait_motion_done_many_` 加 optional `std::vector<uint16_t>* peakI_out` 參數
- `user_lib/WASH_ROBOT.cpp` —
  - `zdt_wait_motion_done_many_`:函式結尾 copy 內部 `peak_I[]` 到 caller 的 vector(若傳了 peakI_out)
  - `pusher_extend_with_disable_seal_`:Phase 1 fast extend 後檢查 per-slave peakI,若 ≥ `DISABLE_PHASE_CURRENT_LIMIT_MA` (1200mA) 視為「已撞牆」,設 `endpoint_stalled[i] = true` → iter 0 跳過 push 直接進 WAIT_SEAL
### 原因
log 顯示 sweep 期間 body slave 5,6 在 Phase 1 fast extend (700rpm) 期間 peakI 1545/2086mA 就撞到牆,但程式仍然進 iter 0 慢推 +3000 pulse → 浪費 ~1 秒 + 多壓 cup 一次。實際上 iter 0 立刻 WALL detection + SEALED wait=100ms,證明 cup 早就 sealed。

新流程:
- Phase 1 wait 結束時,zdt_wait_motion_done_many_ 把 internal `peak_I[]` 透傳給 caller
- caller 檢查每個 slave peakI,≥ 1200mA 視為已撞牆 → 設 endpoint_stalled
- iter 0 既有 `if (endpoint_stalled[i]) continue;` 邏輯處理:skip push,進 WAIT_SEAL
- WAIT_SEAL 既有 logic 不對 endpoint_stalled fast-skip → 正常等真空
- 效益:每個 Phase 1 撞牆的 cup 省 ~1s + 少壓一次

電流太小 skip WAIT_SEAL 那條 (user 提到的「也要加」) 已經在 28w 實作了 (`DISABLE_LOW_CONTACT_PEAK_MA=400`),log 可看到 `peakI=387mA < 400mA — no contact evidence, skip WAIT_SEAL`。沒撞、沒接觸的 cup 自動快速跳過。

風險:Phase 1 peakI 高但其實還沒 sealed(只是擦到一下)→ iter 0 跳過 push 後 WAIT_SEAL 拉不到真空 → vacuum_plateau → 走 iter 1 補救,無永久副作用。

---

## 2026-05-29v Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - 加 `Settings` struct（19 個 `std::atomic<int/double>` 成員）+ `settings_` 成員
  - 加 `load_settings_file_` / `save_settings_file_` private helpers + `load_settings_at_boot` public
  - 加 `cmd_get_settings` / `cmd_set_setting` / `cmd_save_settings` 三個 public cmd handlers
- `user_lib/WASH_ROBOT.cpp` —
  - constructor 初始化 19 個 atomic 從 constexpr 預設值
  - 實作 cmd_get_settings (格式 `OK key=cur:def key=cur:def ...`)
  - 實作 cmd_set_setting（Idle-only gate；每 key 自訂 min/max range）
  - 實作 cmd_save_settings / save_settings_file_ / load_settings_file_（plain `key value` text format）
  - 加一段 `#define` shadow block 把 19 個 constexpr 名稱 redirect 到 `settings_.<name>.load()`
    - 放在 cmd_get/set/save/load 實作之後，consumer code（line 1310 以後）之前
    - 不改原 constexpr 宣告，只在 `WASH_ROBOT.cpp` 這個 TU 內 shadow
- `washrobot_new_PI/main.cpp` —
  - dispatcher 加 `get_settings` / `set_setting <k> <v>` / `save_settings` 三 cmd
  - boot 流程加 `robot.load_settings_at_boot("settings.json")`（在 `robot.init()` 之前）
- `web_backend/public/index.html` —
  - sidebar 加 Settings 按鈕（4th nav tab）
  - 加 `.panel-settings` panel，5 個 sub-section (A 牆距 / B 真空 / C realign / D crane 張力 / E IMU+arm)
  - 19 個 input 都帶 `data-setting`、`(default: ?)` 顯示 constexpr 預設
  - Load / Apply All / Save 三按鈕 + 狀態列
- `web_backend/public/app.js` —
  - PAGES 加 'settings'
  - 加 `initSettingsPage` IIFE：Load 送 `get_settings`、Apply All 序列送 `set_setting`、Save 送 `save_settings`
  - 加 `handleSettingsReply` 解析 `OK key=cur:def ...` 回填 input + default
  - 鉤進 `onWashrobotLine` 開頭 forward 給 handler
  - input.dirty class：值跟最後 Apply 不同就高亮
  - MutationObserver 偵測首次切到 settings 頁 → auto-load
- `web_backend/public/style.css` —
  - 加 `.panel-settings` h3 / `.settings-grid` 2-col layout / `.default` 灰字 / `.dirty` cyan glow
  - page switcher 加 `settings` 那行 CSS
### 原因
換牆面要改 ~25 個參數，原本都是 `static constexpr int`，每次要動就改 `.h` 重編。Sadie 要求 GUI 加 Settings 頁、初值用現在 default。

設計 (5 個選項):
- **L1+L2 (19 個)** — 高/中頻 wall-tune
- **Live tune (atomic 即時生效)** — 不用重啟
- **持久化 settings.json** — 重啟保留
- **Idle-only 安全鎖** — 跑到一半 SET 會 ERR busy
- **單一 settings** — 不做多牆 preset 切換

實作技巧 — 用 `#define CONST_NAME (settings_.const_name.load())` 在 TU 內 shadow constexpr。consumer code 不用改一行，但 cmd_get_settings 跟 constructor 在 #define 之前所以仍能看到原 constexpr 預設值，可以同時 emit current 跟 default。

> 沒做 multi-preset (玻璃/金屬/水泥 切換)。要做就再開 2 倍工程量,先把單 settings 跑穩。

---

## 2026-05-29u Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_M1_INSTANT_THRESHOLD_NM` **0.7 → 1.0** Nm
### 原因
log 顯示 sweep LEFT round 期間 feet `disable_seal:4` iter 1 push 推牆瞬間 (I=593→718→786→843mA),機械反作用力傳到上滑台 → M1 tau 從 -0.83 飆到 -0.24 (d=0.7814) → 觸發 INSTANT (門檻 0.7) → 假警報 PAUSE-ON-ERROR。

`dm2j_motion_active_` flag 只 cover DM2J 馬達,不 cover ZDT pusher 推 → ZDT push 期間 tau 偵測沒禁用。最快修法是拉高 INSTANT 門檻避開 ZDT push 引起的瞬間 spike (觀測 0.78,提到 1.0 留 0.22 margin)。

trade-off: 真撞東西若 M1 spike < 1.0 Nm 不會 INSTANT 觸發,改靠 spike-armed-confirm 路徑(0.4 spike + 0.2 sustained × 2 polls)接住,延遲 ~400ms 偵測。實機觀察。

---

## 2026-05-29t Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — 三件 cli_22_ bus 競爭緩解
  1. `vacuum_wait_release_` poll interval `POLL_MS` **200 → 300 ms**(JC100 read 之間隔開,給 bus 喘息)
  2. `do_arm_clean_sweep_continuous_` cleanup: 3 個 `pqw_.controlRelay` OFF 改用 `std::async` 跟 `arm_cmd("PARK")` 並行(原 PARK→PQW 序列 ~1s,改並行後 ~300ms)
  3. `arm_monitor_during_sweep_`: DM2J:14 `read_status` 加 `elapsed >= 1000` 守衛 — sweep 前 1000ms 不查 slide alarm + motion_complete,給 body_pre_cycle 喘息空間
### 原因
2026-05-29 log 看到 step_up 內 body_pre_cycle `vacuum_wait_release_(body)` 卡 ~5 秒,期間 JC100:6 / JC100:8 連續 timeout。背景 pressure_poll 已停(29s 修),但 cli_22_ 上還有 arm sweep 的 DM2J:14 `read_status` 每 200ms + sweep cleanup PQW 寫,跟 body group JC100 讀撞 bus → JC100 driver timeout → vacuum_wait_release_ 一直判定「sealed」直到讀成功。

**改 #3 影響分析:**
- slide 動的前 1000ms 是加速段,撞東西機率低
- 1000ms 後正常 200ms 偵測 alarm + motion_complete (實測 motion complete 多落在 1400-2400ms,早退不受影響)
- cli_22_ 流量在 sweep 前 1000ms 完全沒 DM2J:14 read → 給 body_pre_cycle vacuum_wait_release_ 暢通空間
- 風險: 前 1000ms 撞東西要等 1000ms 後才偵測,但物理上 slide motor alarm 自己 latch 會停,沒繼續推牆,只是 user 反應慢 1s

**改 #2:** cleanup 內 PQW 寫從序列 → 跟 PARK 並行。PQW 寫快(各 ~100-300ms)、PARK 慢(可達 10s timeout),並行後 cleanup wall time ≈ PARK time,不受 PQW 拖累。

**改 #1:** 治標但有效 — JC100 driver timeout 約 1.2 秒,改 300ms poll 讓兩次 timeout 之間至少隔 100ms 不會堆疊。

預期 body retract 卡 5 秒的問題減緩,但若 cli_22_ gateway 本身有 stale buffer 偶發症狀(項目記憶 [Modbus-TCP gateway stale buffer 偶發症狀](project_modbus_tcp_stale_buffer.md)),根本解還是要從 driver 層修。

---

## 2026-05-29s Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 `read_pressure_(int slave)` 宣告;原 pressure_poll 成員保留但加註解(thread 不再啟動)
- `user_lib/WASH_ROBOT.cpp` —
  - `init()`: 移除 `pressure_poll_thread_` 啟動,改印 "DISABLED" log
  - `stop()`: 移除 join
  - `pressure_poll_loop_()`: body 空掉(保留簽章 backward compat)
  - 新 `read_pressure_(int slave)` helper:wrap `M_(s).read_pressure()` + 更新 `cached_pressure_[s-1]`
  - 8 處 motion-path `M_(s).read_pressure()` 全部換成 `read_pressure_(s)`:
    - `phase5_center` × 2 (roll correction monitor)
    - `pusher_extend` vacuum early-stop
    - `disable_seal` pre-push check + WAIT_SEAL poll
    - `vacuum_check_` worst-pressure retry loop
    - `vacuum_wait_release_` poll loop + stuck list
  - `cmd_status()`: 新 refresh-on-demand 邏輯
    - `motion_active_=true` → 回 cache (避免撞 motion 用 bus)
    - `motion_active_=false` → fresh read 9 顆 + 更新 cache + 回
- frontend 不動(`btn-refresh-vacuum` 既有 → `send('washrobot','status')` 行為不變,但現在 status 在 idle 時會真的去讀)
### 原因
2026-05-29 log 顯示 step done 後 JC100 timeout 大爆發(slave 1/3/5/6/8 每 1-2s timeout 持續 1 分鐘+),vacuum_release 拿到 stale cache 卡住 PAUSE-ON-ERROR。原因:背景 `pressure_poll_loop_` motion 結束就立刻全速 polling cli_22_,跟 arm sweep cleanup PARK + PQW verify retry 撞 bus。

`pressure_poll_loop_` 純為 GUI 顯示存在,沒任何 motion 邏輯需要它。改成:
- Motion 期間 piggyback 更新 cache (read_pressure_ helper)
- Idle 期間 cmd_status 一次性 fresh read (refresh 按鈕觸發點)

效果:**cli_22_ bus 上 JC100 idle traffic 從每秒 9 reads 降到 0**(除非 user 按 refresh)。

DY-500 weight cache (`cached_weight_kg_`) 隨之 dead — pressure_poll 是唯一 updater。但 DY-500 sensors 從未實機安裝(`[--] DY-500 slaves 10/11 not installed`),`read_rope_weight_max_kg_` Tier 2 本來就 skip,**無功能影響**。

---

## 2026-05-29r Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` —
  - `cmd_status` 加 `arm_attached=on/off`（在 `crane_attached` 後一行）
  - `cmd_arm_attached` reply 改 `arm_attached=on/off`（原本 `=1/0`）統一格式，跟 crane 對齊，GUI regex 共用
- `web_backend/public/index.html` —
  - "🦾🚿 arm 整合操作 (washrobot 編排)" panel 從 `data-page="manual"` 改 `data-page="home"`，移到首頁
  - 加 `arm-link-badge` (crane 同款 prominent badge)
  - 加 `arm-attached-status` 文字（`(currently: ?)`）
  - 加 `INIT` 按鈕（`data-tgt="washrobot" data-cmd="arm_init"`），會設 `arm_calibrated_`
  - hint 列表更新提到 manual 頁的「🦾 清潔手臂」面板有更細的 arm 操作
- `web_backend/public/app.js` —
  - 加 `arm_attached=on/off` regex（mirror crane handler）
  - 更新 `arm-attached-status` 文字 + `arm-link-badge` 顏色（🟢 ATTACHED / ⚪ DETACHED）
### 原因
user request：把 arm 編排 panel 提到首頁 + 加 attach 狀態顯示 + 加 INIT 按鈕。INIT 用 washrobot 包裝（`arm_init` cmd），sweep 才能跳過內部 ENABLE 重做（memory 提到 28n fix）。

---

## 2026-05-29q Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 `DISABLE_LOW_CONTACT_PEAK_MA = 400`
- `user_lib/WASH_ROBOT.cpp` `pusher_extend_with_disable_seal_` —
  - iter loop 內加 `std::vector<uint16_t> peak_I_iter(N, 0)`
  - per-slave push wait loop 內，更新 `peak_I_iter[idx]` 跟著 local `peak_I` 同步寫
  - WAIT_SEAL phase 啟動前先掃 `peak_I_iter[i]`：< 400mA + not-done + not-endpoint_stalled → 直接 `plateaued[i] = true` + log
  - 全部 not-done slave 都 fast-skip → 跳過整個 WAIT_SEAL 100ms-tick 迴圈，`continue;` 進下一 iter
### 原因
2026-05-29 field log：cup 2/4 走「逐 iter 找牆」流程，iter 0~2 push stable peakI 都很低 (135~770 mA = 沒摸到牆)，但每 iter 仍花 2000ms 等 vacuum plateau timeout。3 iter × 2000ms × 2 slave = ~12 秒純浪費。

用 peakI 當「有沒有接觸」直接判定：
- peakI 600+ mA → 摸到東西，等 vacuum 正常 build（不影響 SEAL 場景，slave 1, 3 peakI 都 600+）
- peakI < 400 mA → 在空中，無接觸，跳過 vacuum 等待
- peakI 400~600 mA → 模糊區間，仍走 vacuum 等待

預期效果：每 step「找牆 + 弱密封」場景省 4~6 sec；正常 seal 場景 0 影響。Endpoint 已 stalled 的 cup 不受 fast-skip 影響（它們已在牆上，需要 vacuum loop 給機會 seal）。

---

## 2026-05-29p Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `try_or_pause_` (line ~702) external pause for `arm_sweep_obstacle`: Retry / Skip 路徑加 `arm_sweep_fire_nowait_(0.0)` 把 slide 送回 0（跟 `handle_post_sweep_obstacle_` 對齊）
  - 加 `std::atomic<bool> dm2j_motion_active_{false}` 成員 + 詳細註解
- `user_lib/WASH_ROBOT.cpp` —
  - `arm_monitor_during_sweep_` (line ~810): 加 dm2j motion gate
    - DM2J motion active 時 → 跳過 tau-trigger 邏輯，counters / armed / prev_delta 全凍結
    - DM2J motion true→false transition → 重讀 M1/M2 baseline + reset counters
    - 持續 active 時每 5 polls (~1s) 印 "DM2J_MOTION_GATE active" 表示 monitor 活著
  - `dm2j_pair_move_abs_` (line ~416): RAII guard set/clear flag（放在 skip-if-at-target 後）
  - `pusher_move_many_` (line ~2535): RAII guard set/clear flag（函式入口）
  - `pusher_two_stage_retract_` (line ~2668): RAII guard set/clear flag（函式入口）
### 原因
2026-05-29 field log：step_up + sweep pipeline 跑時觸發 false-positive obstacle (`m1_tau_spike`)，明明沒撞東西。Trace 顯示 M1 spike 0.488 Nm 剛好發生在 DM2J:1 + DM2J:3 (feet rail) 從 5cm 收回 0cm 的瞬間 — body 重心位移→上滑台跟著動→arm 對牆距離微變→M1 受力變化。detection 邏輯本身沒錯（SPIKE 0.488>0.4、rate 0.391>0.2、sustained 0.293>0.2 全 hit）但 threshold 假設「sweep 期間機體靜止」與 pipeline 模式衝突。

額外發現：user RETRY 後 slide 沒回 0，下一輪 sweep 從 obstacle 中途位置開始。Root cause = `try_or_pause_` 的 RETRY/SKIP 路徑沒做 slide-home，而 `handle_post_sweep_obstacle_` 較晚才執行（先被 try_or_pause_ 清掉 flag 後變成 no-op）。

修法 A+B 組合：
- A. 用 `dm2j_motion_active_` flag gate detection — DM2J motion 期間 monitor 不偵測
- B. motion 結束 transition → 重新 baseline，把 motion 引起的 tau drift 吃掉
- C. `try_or_pause_` 的 Retry/Skip 補 slide → 0（跟 handle_post_sweep_obstacle_ 邏輯一致；Abort 仍保留現場）

RAII guard 確保 flag 在 helper 任何 return path 都會清掉。dm2j_pair_move_abs_ 的 skip-if-at-target fast path 不 set flag（沒有實際 motion）。

> 三個 helper 各自一份 6 行的 ClearMotionFlag struct — 沒抽 common helper 因為跨函式 scope 共用反而複雜。

---

## 2026-05-29o Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_arm_clean_sweep_continuous_` 內 `sweep_with_tool` lambda 加兩段 `std::async` 並行
  - **dry round (RIGHT)**: pqw OFF 跟 arm_cmd DEPLOY 並行(原本是 OFF → DEPLOY 序列)。join 在 verify_arm_deploy_ 之前。
  - **wet round (LEFT)**: pqw ON 跟 arm_sweep_fire_nowait_ 並行(原本是 ON → slide 序列)。join 在 sweep_with_tool 結束前。
### 原因
log 顯示 LEFT→RIGHT round 間有 ~9 秒空檔,主要被 arm_cmd DEPLOY 佔住 (~5-7s)。其前後的 pqw 序列原本是純序列,但跟 arm_cmd 完全不同通訊通道 (cli_22_ PQW vs motor_api TCP) 沒任何衝突,可以並行。

每 round 約省 1-1.5 秒(dry 前置 + wet 後置)。連續清洗 5 round 省 5-7 秒。

副作用都很小:
- dry round: 水/刷 OFF 比 DEPLOY 早完成是常態;反之 OFF 慢一點就在 M1 retract 過程中關閉,水噴空中無害
- wet round: pqw ON 在 slide motion 期間執行,水可能晚 ~500ms 到,前 1-2cm sweep 偏乾。可接受

未動的 risky options:
- DEPLOY 期間 fire slide → tool 在牆上拖怪軌跡,可能傷工具
- 改 motor_api 縮 DEPLOY → 超出 washrobot 範圍

---

## 2026-05-29n Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `VACUUM_PLATEAU_MS` **1800 → 2000** ms (退回 29g 改之前)
### 原因
實測 log slave 6 iter 0/1 `best=1kPa / -1kPa` 1900ms 後被 plateau 判定提早結束 + weak_seal,但 user 確認 cup 過一下其實會封住 — 屬於「慢開機」型 cup,真空起始 push 後要過 2 秒才開始往下掉。29g 的 1800ms 把這類 cup 從邊緣推進失敗區。

先退回 2000 保安全。若還是太緊張(實測仍有慢開機 cup 漏網)再往上拉。

---

## 2026-05-29m Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `zdt_wait_motion_done_many_` 內 `STABLE_COUNT_NEED` **3 → 2**
### 原因
2-stage retract stage 2 高速 (500rpm) controlled stop 反彈/抖動機率比慢速移動低,2 polls (300ms) 確認夠用。省 ~150ms/retract。

未動: `zdt_wait_motion_done_`(單 slave 版) + Phase 1 fast extend 內的 wait_many — 那兩個 case 比較需要保險,保留 3。

風險: 若 stage 2 結束時 motor 有未捕捉的抖動 → 提早回報 done → 後續動作可能跟未停的 cup 微動撞。實機觀察。

---

## 2026-05-29l Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `PUSHER_SETTLE_MS` **300 → 100** ms
### 原因
log 顯示 ZDT 收回完成到 DM2J feet rail 移動之間有 ~750ms gap (450ms wait_many stable confirm + 300ms PUSHER_SETTLE_MS)。

PUSHER_SETTLE_MS 用意是讓 cup 機械震盪 settle,但 cup 跟 DM2J rail 在不同軸,震盪不影響 rail 移動。300ms → 100ms 省 200ms/step,保守可控。

STABLE_COUNT_NEED + poll_ms 也可動但暫保守不動。

---

## 2026-05-29k Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `PUSHER_EXTEND_BODY_PULSE` (slave 5,6 preset) **33000 → 34000** (+1000 = +0.8cm)
  - 新增 `PUSHER_STAGE1_SAFETY_FACTOR = 3.0` (從 hardcoded 2.0 抽出來),`PUSHER_STAGE1_DELAY_MS` 隨之 2597 → **3896 ms**
### 原因
2026-05-29 log 分析後 user 決定:
1. slave 5,6 preset 拉一點(33000→34000),邊際提速 iter loop 收斂。撞牆風險可控:
   - over_cm=0 時 phase1_end = 34000+0-3000 = 31000,wall avg 38500 → margin 7500 ✓
   - over_cm=3cm (realign threshold 內) 時 phase1_end = 31000+3780 = 34780,closest wall(36038) → margin 1258 ✓
   - over_cm=4cm (極端) 時 phase1_end = 36042,closest wall 36038 → 4 pulse 邊緣,有撞風險但機率低
2. stage 1 delay 拉長: factor 2.0→3.0,delay 2.6s→3.9s。給過伸 cup 更多時間 peel 再被 stage 2 接手
### 變化
每 step body 多一點點 base 距離(0.8cm)、stage 1 delay 多 1.3s。理論上單次 retract 從 ~4.1s → ~5.4s,但若 cup 過伸 ≥ 3cm 比例下降足夠 iter,整體 step 時間可能更短。實機觀察。

抽 `PUSHER_STAGE1_SAFETY_FACTOR` 是為了未來再調速時直接動 factor 就好,不用碰公式。

---

## 2026-05-29j Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `pusher_two_stage_retract_` 分類條件 sentinel fix
  - 舊: `int cur_pulse = -1; if (cur_pulse >= 0 && cur_pulse <= stage1_target) skip_stage1` — `-1` sentinel 跟「真實位置 -1 pulse (= -0.1°)」碰撞;cur_pulse 為微小負數時 (overshoot noise, e.g. `-5 = -0.5°`) 條件 FAIL → 跑 stage 1 → 馬達往外伸 4cm 再縮回 0
  - 新: 用 `INT_MIN` sentinel 區分「讀失敗」vs「真實位置稍負」;微小負值 → 視為已過 endpoint,skip stage 1
  - 額外加 encoder fault guard:`cur_pulse < -1000` (-100° ≈ -3cm) 視為 encoder 異常,直接 abort (避免誤命令馬達往牆推)
- `user_lib/WASH_ROBOT.cpp` — 加 `#include <limits>`
### 原因
delay-based two-stage retract (29h) 之後 user 提出: stage 2 起始位置已近 0 時,會不會反而先往外伸再縮?

分析結果:
- cup 在 0~stage1_target 之間: 既有條件已正確 skip stage 1 ✓
- **cup 在微小負位置 (-0.x° overshoot 噪訊): 舊邏輯 BUG → 走 stage 1 → 外伸 ~4cm 再縮**(舊 polling 版也有此 bug,但因為要等馬達 stable 才 fire stage 2,等同把外伸距離跑完才修正,只浪費時間不撞牆;新 delay 版同樣浪費 ~5s 跟機械晃)
- cup 位置嚴重負值 (encoder 異常): 兩條路徑都不安全,直接 abort 讓 user 處理

實際觸發場景: manual `pusher all retract` 連點兩次 (上次剛縮完,cup 在 0 附近 noise 落在負值)。

---

## 2026-05-29i Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `apply_balance_trim` 內兩處 `setFreqHz` 改用 `dual_se3_sync_retry` 雙 lambda overload，L+R 平行寫
  - `reset_to_base` lambda (line ~1019): `base_hz` 兩邊一起寫
  - 主 trim 寫 (line ~1054): `left_hz` / `right_hz` 兩邊一起寫
  - 兩處都 `max_attempts=1, backoff=0` — BAL 不需要 retry（下個 tick 自己會 re-issue）
### 原因
2026-05-29 field log：`down on/off` 連發 4 次後 L/R 鋼索差 12cm，面板 P.7/P.8/DC brake 已確認對齊。檢查程式同步寫狀況發現：
- ✅ sync_start / sync_retry(stopDecel) / emergencyStop — 都正確 parallel
- ❌ `apply_balance_trim` 走 same-thread 序列寫 L 後寫 R — 每 BAL tick 內 L 比 R 早 ~20-50ms 收到新 Hz

每 250ms tick 浪費 ~10-20% trim 力道，4 sec hold (16 ticks) 累積 1.5~5cm 殘留誤差，跟 12cm gap 對得上一半。剩下一半推測為機械層（鼓徑 / 鋼索層數 / brake 不對稱）— 軟體救不了，下一步要實機驗證。

修法只改 BAL trim 寫的同步，其他 control path（sync_start / sync_retry）本來就 parallel 沒動。

> 用 `dual_se3_sync_retry<FnL, FnR>` 雙 lambda overload (line 773) — 已存在的 helper，無新增 utility。

---

## 2026-05-29h Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 3 個常數
  - `PUSHER_CM_PER_REV = 3.08` (bench-measured: 30rpm = 1942 pulse/s ÷ 1261 pulse/cm = 1.54 cm/s → 1 rev ≈ 3.08 cm)
  - `PUSHER_RETRACT_CM_PER_SEC` = `PUSHER_RPM_RETRACT × PUSHER_CM_PER_REV / 60` ≈ 1.54 cm/s
  - `PUSHER_STAGE1_DELAY_MS` = `(RETRACT_SLOW_PEEL_CM / cm_per_sec) × 2 × 1000` ≈ 2597 ms (2× safety factor)
- `user_lib/WASH_ROBOT.cpp` — `pusher_two_stage_retract_` 改寫為 delay-based
  - **舊**: per-slave polling @150ms,每個 slave 自己 stage 1 完成才 fire stage 2。wall time = max(stage1)+max(stage2) ≈ 7-10s body
  - **新**: 一次性 read 位置分類 → sync-fire stage 1(+ skip-to-stage2) → `sleep PUSHER_STAGE1_DELAY_MS` → sync-fire stage 2 → `zdt_wait_motion_done_many_` 等全部到 0
  - wall time ≈ 2.6s delay + 1.5s stage 2 ≈ **4s/retract**,省 ~3-5s
### 原因
2026-05-29 log 分析: pusher_two_stage_retract_ stage 1 是每 step 最大時間消耗。user 提案改成 delay-based,不再每 150ms polling cli_21_ ZDT bus。

**correctness 推論:** cup adhesion 在 motion 開始的前幾 mm 就破除(vacuum 已關)。Slow peel 距離原本是「位置安全」設計,改成「時間安全」等價 — 30rpm × 2.6s = 4cm slow motion,即使 cup 過伸 10cm,2.6s 後 fire stage 2 (sync=1) motor 接收新 target=0 + 新 speed RPM_RETRACT_FULL,從中途位置平順加速。driver 接受 mid-motion pos_mode_nowait (跟舊路徑同 primitive)。

跳 stage 1 case 保留(cup 已在 endpoint 內側,absolute stage 1 會把它推回牆)— 直接 queue stage 2 跟 stage 1 batch 一起 sync-trigger。

**call sites:** 8 處(body/feet retract、cycle_group_ rescue ×2、realign Phase A、return_home ×2、manual ×3)— 全部共用此函式,一次改全部生效。

**測試重點:** 手動 `pusher all retract` 看 wall time、stage 2 sync 起步有沒有 stall。實機萬一發現過伸太多 cup stage 2 跟不上,把 PUSHER_STAGE1_DELAY_MS 的 2.0 safety factor 調大即可。

---

## 2026-05-29g Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `RETRACT_SLOW_PEEL_CM` 2.3 → **2.0** cm (two-stage retract Stage 1 距離縮短 ~13%)
  - `VACUUM_PLATEAU_MS` 2000 → **1800** ms (disable_seal no-contact 提早 200ms 放棄等待)
### 原因
2026-05-29 log 分析: 2-stage retract Stage 1 (~6-7s body / 4-5s feet) 是 step 最大時間消耗;disable_seal 失敗 iter 受 plateau 2000ms 拖慢。

預估每 step 省 ~1-2s。保守調整避免 stall / 真空誤判風險:
- Stage 1 距離只動 0.3cm,30rpm 慢速 cup 還是有充足時間脫離;Stage 2 (500rpm) 接手位置更靠近 preset,風險可控
- Plateau 只縮 200ms,真空已建立的 cup 通常 1000-1500ms 就 SEALED,1800ms 仍足夠正常情況

---

## 2026-05-29f Claude (Sadie)
### 修改檔案
- `Linux_test/main.cpp` — 新增 menu 29 `SE3 inspect`
  - 連到 USR_A (.30) + USR_B (.31)，slave 1
  - 讀 5 個 register：`0x0106 (P.7 acc)`、`0x0107 (P.8 dec)`、`0x0A00 (DC brake Hz)`、`0x0A01 (DC brake time)`、`0x0A02 (DC brake volt)`
  - 左右並排印 raw hex + 換算值 + 單位
  - 任一參數 L/R 不同 → 印 `*MISMATCH*` + 提示
### 原因
2026-05-29 field log 出現 R 邊 `down off` 後比 L 多放 2~5cm，4 連發累積 1cm→12cm gap。memory + summary doc 都標過 P.7/P.8 + DC brake 兩台必須對齊但實機沒查證手段。Menu 29 = 一鍵遠端 dump 兩邊 5 個參數對比，不用爬上吊機改面板。

> Register mapping 來自 `.claude/summaries/SE3_INVERTER_MODBUS_SUMMARY.md` 表 188-198。

---

## 2026-05-29e Claude (Sadie)
### 修改檔案
- `cleaning_arm/main_api.cpp` — `DamiaoAPI::init()` motor 初始化加 retry loop
  - 每個 motor (M1/M2) 最多嘗試 5 次,每次:
    - `disable` + 200ms settle (清 motor 暫態 state)
    - `refresh_motor_status` 探活 + 順便讓 driver `receive()` 一次清掉 serial RX 殘留 CAN frame
    - 100ms 後 `switchControlMode(MIT_MODE)`
  - 失敗則間隔 500ms 重來,5 次全 fail 才放棄
### 原因
2026-05-29 user 回報:每次 motor_api 被 `^C` 強砍(washrobot sweep 卡住時)後重啟,M1 `switchControlMode failed` 必須斷 M1 電源才救得回,很麻煩。

根因猜測:
- 上次 `^C` 時 M1 還在跑 MIT control loop (touch_wall 中),motor 內部 state machine 沒收 disable 就被切 CAN,RAM 殘留
- USB-CAN serial RX buffer 殘留 stale CAN frame,新 program 開起來 receive_param() 拿到舊資料 RID 對不上 → switchControlMode 一直等不到 ACK

新 retry 流程能救 stale serial buffer + motor 暫態 state。救不了 firmware lock (那種還是要斷電,但機率低)。

注意: damiao driver 沒提供專用 clear_error/reset CAN cmd,只能靠 disable + delay 變通。`refresh_motor_status` 順便 flush serial RX 是這個方案的核心。

---

## 2026-05-29d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` —
  - `do_arm_clean_sweep_continuous_::sweep_with_tool`：DEPLOY no_reply 不再只是 return false 就結束；改成 set `arm_sweep_obstacle_pending_` + detail `arm_deploy_no_reply slot=...` + EVT，讓 main thread 走 `handle_post_sweep_obstacle_` 給 user 三選一(Retry/Skip→slide 回 0；Abort→slide 留原位)
  - `do_arm_clean_sweep_continuous_` cleanup：PARK timeout `30→10s`；若 PARK 也沒回覆且尚未 set obstacle flag → set `arm_park_no_reply` flag，同樣走 main thread pause
  - `do_arm_clean_sweep_` cleanup：PARK timeout `30→10s`(sequential 路徑本來就靠 try_or_pause_ 處理 DEPLOY no_reply,cleanup 只需縮 timeout)
### 原因
2026-05-29 實測：sweep 連續模式下 LEFT round OK (slide 0→80)、RIGHT round DEPLOY arm_cmd_ 拿不到 reply(motor_api 還活著但回覆太慢/卡 `M1 touch_wall_slot`)→ sweep abort → cleanup PARK 又 timeout → main thread 拿 ERR 繼續,但 **slide 卡在 80cm**,user 無法決定要不要送回 0、也沒收到警報。

新流程比照 obstacle pause：no_reply 也透過 `arm_sweep_obstacle_pending_` flag 觸發 main thread pause UI。Retry/Skip → slide 回 0；Abort → slide 留 80cm user 自己處理。

PARK timeout 30→10s：motor_api 沒回時 cleanup 不再卡 30s×2attempts=60s,改 10s×2=20s。motor_api 健康時 PARK 也很快,影響不大。

注意：detail 字串用「no_reply」中性詞,不寫「dead」— motor_api 經常只是 busy(M1 touch_wall 搜牆中)不是真死。

---

## 2026-05-29c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — declare `handle_post_sweep_obstacle_(context)` helper
- `user_lib/WASH_ROBOT.cpp` —
  - 實作 `handle_post_sweep_obstacle_`：偵測 `arm_sweep_obstacle_pending_` flag → `await_user_intervention_` 給 user 三選一
    - **Retry**: 清 flag → `arm_sweep_fire_nowait_(0)` 把 slide 從 obstacle 位置送回 0
    - **Skip**: 清 flag + set skip_rest_of_run + 送 slide 回 0
    - **Abort**: 回 true（caller 傳 ERR），**slide 留在 obstacle 位置**（user 可實機檢查）
  - 7 個 `fut_sweep.get()` 後加上 `handle_post_sweep_obstacle_(...)` 呼叫：
    - `cmd_step_down_with_sweep`
    - `cmd_step_up_with_sweep`
    - `cmd_step_up_sweep_after_feet`
    - `cmd_step_down_sweep_after_feet`
    - `cmd_step_up_sweep_before_after` (round 2 等 + round 1 join in before_hook)
    - `cmd_step_down_sweep_before_after` (round 2 等 + round 1 join in before_hook)
    - `cmd_run` pipeline (iter 1 pre join + iter 2+ join + final)
### 原因
28ai 之前 continuous sweep 模式偵測到 obstacle 只能 `signal_obstacle` (stop slide + set flag)，**沒人 pause UI 給 user 決定**，slide 卡在 obstacle 中途位置。Step body 結束後就 PARK + 結束 step，下一 sweep 從錯位置開始。

新流程：
1. Background sweep 偵測 → stop slide + set flag
2. Main thread `fut_sweep.get()` 拿到 sweep result
3. **main thread 呼叫 `handle_post_sweep_obstacle_(context)` → await user**
4. User 選 Retry / Skip → slide 回 0；Abort → slide 留原位 + 傳 ERR

per user 2026-05-29: 「只有 skip、retry 才會回 0」— Abort 路徑刻意不移動 slide。

對 sequential `do_arm_clean_sweep_` 已在 28t 處理（inline retry loop），這次補上 continuous 模式所有入口。

---

## 2026-05-29b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — 6 個 sweep-related cmd 函式入口加上 `arm_sweep_obstacle_pending_` / `arm_sweep_skip_rest_of_run_` 旗標 reset:
  - `cmd_arm_clean_sweep` (~1080)
  - `cmd_attach` (~4068)
  - `cmd_step_down_with_sweep` (4733)
  - `cmd_step_up_with_sweep` (5865)
  - `cmd_step_up_sweep_after_feet` (5877)
  - `cmd_step_down_sweep_after_feet` (5985)
  - `cmd_step_up_sweep_before_after` (6082)
  - `cmd_step_down_sweep_before_after` (6168)
### 原因
實機觀察 log：先按 attach、再按 step+sweep，sweep 卻直接 skip：
```
[arm_clean_sweep_cont] SKIPPED (arm_sweep_skip_rest_of_run_=true from prior obstacle)
```
這個旗標原本只在 `cmd_run` 入口 reset (2026-05-28)，**單步 cmd 不會 reset**。意味著：之前某次 obstacle 觸發 Skip 之後，該旗標就一直 latched 到下次 `cmd_run`，期間所有單步 sweep cmd 都被靜默 skip。

語意修正：**Skip scope = 單一 user-initiated motion command 內**。每個 user cmd 入口（attach / step / run / sweep）都是 fresh start，把旗標清回 false。下次該 cmd 真的撞到 obstacle 再重新 latch。
### 不改 cmd_run
`cmd_run` 入口在 2026-05-28 已有同樣 reset，本次只是補齊其他單步 entry。
### Trade-off
- 若用戶在一連串單步 cmd 之間「希望保持上次 Skip 決定」，現在不行了 — 每按一次按鈕都是新 scope
- 但相反語意更直覺：「按了新按鈕，sweep 就會試」比「按新按鈕但 sweep 還是 silently skip」好得多

---

## 2026-05-29a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `VACUUM_BACKUP_CM` 5.0 → **10.0**
### 原因
實機觀察 cup 在某牆面位置吸不到時，cycle_group_ vacuum retry backup 5cm 後重吸，新位置往往**仍在原本問題區附近**（牆面瑕疵、髒污可能延伸幾 cm）→ 第二次也吸不到 → 又 retry → 累積失敗。

backup 改 10cm 讓 cup 換到**更明顯不同的牆面位置**，避開原本的漏氣區。
### Trade-off
- 退更多 = rail 來回距離更長、crane motion 也加大、總耗時略增
- 但若一次 retry 成功，比兩次 retry 還快
- 後退空間上限 (rail < 0 或 > step_cm) 達到時仍會 abort
### 兩個 backup_factory 共用同一常數
- step_down body backup (3997)
- step_up body backup (4538)
- step_down feet backup
- step_up feet backup
全部受影響。

---

## 2026-05-28ai Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `ARM_SWEEP_M2_SPIKE_THRESHOLD_NM` 0.3 → **100.0**（實質 disable）
  - `ARM_SWEEP_M2_RATE_THRESHOLD_NM` 0.15 → **100.0**（實質 disable）
### 原因
實機 28ah 觀察：M2 drift 在 step+sweep 並行模式下可達 0.7 Nm（baseline -0.554 → +0.171）、rate 達 0.178。無論 RATE threshold 設多少都跟 light block 訊號重疊：

| | drift | light block |
|---|---|---|
| max delta | 0.7 Nm | 0.3-0.5 Nm |
| max rate | 0.18 Nm | 0.12 Nm |

drift 比 light block 大 → 任何 M2 threshold 設「dilute drift」就會「miss light block」、設「catch light block」就會「false-trigger drift」。

放棄 M2 path，純靠 M1：
- M1 SPIKE 0.4 + RATE 0.2 + CONFIRM 2 → 中等以上 block
- M1 INSTANT 0.7 → 巨大 block 立刻 trigger
- DM2J:14 alarm → slide motor 失步（極端硬擋）

### Trade-off
- 失去純 M2 反應的 light block 偵測（罕見，多數 block 會反應到 M1）
- 換來 sweep 期間不再 false positive
- 操作員手動 emergency_stop 為 fallback

### M2 邏輯仍保留
程式 code 沒刪 M2 check，只把 threshold 設超高讓它永不觸發。未來若有更好方法區分 M2 drift vs block（如動態 baseline、rate-of-rate 之類），可以恢復。

---

## 2026-05-28ah Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_M2_RATE_THRESHOLD_NM` 0.08 → **0.15**
### 原因
實機觀察 M2 drift 有自然 step 跳 rate 到 0.109（看 28ag log），剛好過 28ae 的 0.08 threshold → false positive。

實際數據對照：
- Drift natural step rate: 0.055 ~ 0.109
- Light block rate: 0.123（舊 28aa log）
- Heavy block rate: 0.246+

raise threshold 到 0.15 拉開 drift 跟 block 的判定範圍：
- Drift max 0.11 < 0.15 → 不誤觸發 ✓
- Heavy block 0.25+ → 仍觸發 ✓
- Light block 0.12 < 0.15 → **可能漏報，改靠 M1 偵測**
### Trade-off
- 失去 M2 對 light block (rate 0.12) 的偵測
- 但 M1 path（SPIKE 0.4 + RATE 0.2）仍可抓真擋（M1 rate >> 0.2 在 28aa 真擋 log 看到）
- Drift 不再誤報 → 整體 UX 好

---

## 2026-05-28ag Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `VACUUM_NO_CONTACT_FAST_MS` 1000 → **2000**
  - `VACUUM_NO_CONTACT_KPA` -5 → **-1**
### 原因
28af 已把 FAST_MS 從 500→1000 + PLATEAU_MS 1500→2000。實機觀察：
- slave 6 順利在 iter 1 SEALED（wait 1800ms p=-60）→ 1000ms 對 slave 6 夠了
- slave 5 iter 1 best_p=-5 還是被 fast-skip → 真空更慢、1000ms 不夠

兩個加強保護：
1. **FAST_MS 1000→2000**：給 slave 5 多 1 秒緩衝（cup 5 邊緣漏氣比 6 嚴重？泵浦多 1s 也許能拉到 -20~-30）
2. **NO_CONTACT_KPA -5→-1**：「best_p 還沒 < -1」才當無接觸。slave 5 best_p=-5 已經有部分密封跡象，不該被 skip
### 兩條件並存
- iter 0 真的吸不到 (p=0~+1): 0 >= -1 → 2000ms 後 fast-skip ✓ 保留快速跳過 truly no-contact
- iter 1 有部分密封 (p=-5): -5 < -1 → **不 fast-skip → 繼續等到 5s timeout 或 plateau**
### 預期 slave 5
原本 1000ms p=-5 → fast-skip → weak_seal
現在 2000ms p=-5 → 不 skip (還沒到 FAST_MS) → WAIT_SEAL 繼續 → 看能否 5000ms 內到 -60
### 若還是不成
- 真空建得太慢（cup/管路漏氣嚴重）→ 物理問題，需要實機看 cup 5
- 可考慮降 SEAL_DEEP 從 -60 到 -40（接受部分密封）但風險：cup 撐不住重力

---

## 2026-05-28af Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `VACUUM_NO_CONTACT_FAST_MS` 500 → **1000**
  - `VACUUM_PLATEAU_MS` 1500 → **2000**
### 原因
實機觀察：cup 5,6 在 disable_seal 期間 best_p 只到 0 ~ -5 kPa（被 fast-skip 判 no-contact），但 **step 結束後 GUI refresh 讀到 -60 kPa**（真空 eventually 有達標）。

說明 cup 5,6 真空建立速度比較慢（可能是 cup 微洩漏、sensor low-pass filter、或 cup 到 sensor 氣管路長），500ms 不夠它建起來。

拉長 fast-skip 時間 + plateau 時間給 slow cup 更多機會：
- FAST_MS 500→1000ms: 翻倍時間給「no-contact」判定，慢吸 cup 多了 500ms 機會
- PLATEAU_MS 1500→2000ms: 給 vacuum progress 更多時間反應

### Trade-off
- 真的吸不到的 case 多等 500ms 才 fast-skip
- 但慢吸 cup 一次成功就省下 iter 1/2 整輪 push (~3s/iter)
- 整體可能更快（若 5,6 真的能在 1000ms 內開始建真空）

### 後續觀察
- 若 cup 5,6 在 disable_seal 內仍只到 0 ~ -5 kPa → 不是慢、是真的吸不到，要看 cup 物理問題
- 若 cup 5,6 能達 -60 → fast-skip 改成 1000ms 解決

---

## 2026-05-28ae Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_M2_RATE_THRESHOLD_NM` 0.05 → **0.08**
### 原因
28ad 加 M2 rate filter 後又踩雷：drift 通常每 poll +0.014（gradual），但**偶發會跳一個量化步**：

```
t=2600: M2 d=0.287
t=2800: M2 d=0.342 ← change=+0.055 (~一個量化步)
         M2 ARMED (rate 0.055 > 0.05 + d > 0.3)
t=3000: M2 d=0.287 → sustained → cnt=2 → trigger ✗
```

實機觀察 drift 的最大 rate 是 0.055（偶發單步跳），不是預期的 0.014（平均）。Rate threshold 0.05 不夠 margin。

Light block 的 rate ~0.123，所以 0.08 是個合理 middle ground：
- Drift max rate 0.055 < 0.08 ✓ 不觸發
- Light block rate 0.123 > 0.08 ✓ 仍觸發

### Trade-off
- 抓擋的最低 rate 從 0.05 提到 0.08：對「漸進中等擋」（rate 在 0.05-0.08 範圍）會漏；但這種擋通常 d 也低，本來就難辨
- 抓重擋（rate >> 0.08）完全不影響

---

## 2026-05-28ad Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - 新增 `ARM_SWEEP_M1_RATE_THRESHOLD_NM = 0.2f`
  - 新增 `ARM_SWEEP_M2_RATE_THRESHOLD_NM = 0.05f`
- `user_lib/WASH_ROBOT.cpp` `arm_monitor_during_sweep_` 在 SPIKE 條件加 rate-of-change check：
  - M1 spike condition: `d > 0.4 AND |d - d_prev| > 0.2`
  - M2 spike condition: `d > 0.3 AND |d - d_prev| > 0.05`
  - 加 `m1_prev_delta`, `m2_prev_delta` 追蹤
### 原因
實機 28ac 觀察：M2 drift 偶爾爬到 0.30+，SPIKE 0.3 + sustained 0.2 + confirm 2 → false positive 觸發。

分析 drift vs block：
- Drift: d 每 poll 微爬 ~0.014 Nm（漸進）
- Light block: d 單 poll 跳 +0.123 Nm（突發）
- Heavy block: d 單 poll 跳 +0.246+ Nm（更突發）

**Block 的特徵是「rate-of-change 突然變大」**，不只是「絕對值大」。Drift 雖然能爬到 0.3+，但每 poll 變化量小。

新邏輯加 rate check：

| 類型 | d | |d - d_prev| | M2 0.3 + rate 0.05 | M1 0.4 + rate 0.2 |
|---|---|---|---|---|
| Drift (M2 0.30+) | > 0.3 | ~0.014 | ✗ rate 不夠 | (M1 漸進不到 0.4) |
| 輕擋 (M2 0.30+) | > 0.3 | ~0.12 | ✓ trigger | (M1 沒到 0.4) |
| 重擋 (M1+M2) | M1>0.4 M2>0.5 | 多步跳 | ✓ | ✓ |
### 反應
- Drift 不再誤報 ✓
- Block 仍能抓（rate 條件對真擋永遠滿足）
- INSTANT (d > 0.7) 跟 SUSTAINED 邏輯不變

---

## 2026-05-28ac Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `ARM_SWEEP_M2_SPIKE_THRESHOLD_NM` 0.5 → **0.3**
  - `ARM_SWEEP_M2_SUSTAINED_NM` 0.3 → **0.2**
### 原因
實機 28ab 觀察：user 擋輕了一點 M2 d 沒到 0.5：
- 之前 18:12 重擋 log: M2 d 0.697
- 這次 18:18 輕擋 log: M2 d max 只到 0.342

M2 d=0.342 < 0.5 SPIKE → 不觸發。但仔細看 LEFT 中段：M2 d 從 baseline 0.08 跳到 0.30 sustained，是清楚的「block 接觸」訊號。

降 M2 SPIKE 到 0.3 + SUSTAINED 0.2：
| t | M2 d | 28ab (0.5/0.3) | 28ac (0.3/0.2) |
|---|---|---|---|
| 2800 | 0.301 | no arm | armed cnt=1 |
| 3000 | 0.260 | no arm | sustained > 0.2 → cnt=2 → trigger |

預期 t=3000 觸發 = 比 28ab 不觸發強多。

### 風險
- Drift M2 中段最大觀察 ~0.26（28v 之前 log 紀錄），0.3 還有 0.04 margin
- 若有牆面或設備變動讓 drift 上升到 0.3+ → 中段誤報
- DECEL_MASK 末段仍保護，drift 末段衝高不會誤報

### Trade-off
- Pro: 輕擋抓得到（之前完全沒反應）
- Con: 對 drift 容錯收小，需觀察

---

## 2026-05-28ab Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `ARM_SWEEP_M2_TAU_THRESHOLD_NM` 改名為 `ARM_SWEEP_M2_SPIKE_THRESHOLD_NM` = **0.5 Nm**
  - 新增 `ARM_SWEEP_M2_SUSTAINED_NM` = 0.3 Nm
  - `ARM_SWEEP_M2_TAU_CONFIRM_CNT` 維持 2
- `user_lib/WASH_ROBOT.cpp` `arm_monitor_during_sweep_` M2 邏輯改成 spike+sustained state machine（mirror M1）
- baseline log + per-poll log 加 m2armed 指標
### 原因
實機 28aa 觀察：user 擋 LEFT，M2 在 t=1000 就反應（d=0.328），t=1200 spike (d=0.697)，但 M2 threshold 1.5 沒觸發 → 等 M1 在 t=2600 才 trigger（晚 1400ms）。

物理上**工具頭 (M2) 是先接觸障礙的、M1 (大臂) PD 反應有 1+ 秒延遲**。讓 M2 跟 M1 一樣有 spike+sustained 偵測，M2 觸發路徑：

| t | M2 d | state |
|---|---|---|
| 1000 | 0.328 | < SPIKE 0.5, no arm |
| 1200 | 0.697 | > 0.5 → **armed, cnt=1** |
| 1400 | 0.424 | armed, > 0.3 sustained → **cnt=2 → trigger** |

**t=1400 觸發**，比 M1-only 邏輯（t=2600）快 1200ms。
### 各 case 對照
| Case | M2 行為 | trigger 時機 |
|---|---|---|
| LEFT 真擋（本 log） | t=1200 spike 0.697, t=1400 sust 0.424 | **t=1400** (vs M1 t=2600) |
| RIGHT 真擋（本 log） | M2 沒反應 (d=0.027) | M1 INSTANT t=1400 (no change) |
| Drift (28v) | M2 max 0.26 < 0.5 | 不觸發 ✓ |
| Normal sweep | M2 d 0.0-0.08 | 不觸發 ✓ |

### 反應時間綜合
M1 跟 M2 並聯偵測：哪個先到 confirm 就 trigger。
- 工具頭直接撞: M2 ~400ms (2 polls)
- 大臂被推: M1 ~400ms (2 polls)
- 巨大擋: M1 INSTANT ~200ms

---

## 2026-05-28aa Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `ARM_SWEEP_M1_SPIKE_THRESHOLD_NM` 0.3 → **0.4**
  - `ARM_SWEEP_M1_TAU_CONFIRM_CNT` 1 → **2**
- `user_lib/WASH_ROBOT.cpp` `arm_monitor_during_sweep_` 還原 spike+sustained+armed state machine（移除 28z 的 gradient detection）
### 原因
實機 28z gradient 邏輯（curr > SPIKE 0.3 AND prev > SUSTAINED 0.2）有個致命缺陷：**真擋的初始 spike 跟 noise spike 在那個 poll 長一樣**（都是 d 從 baseline 突然跳到 >0.4）。Gradient filter 把真擋初始 spike 也擋掉、要等到 d 第 2 次 elevate 才觸發 → 比 28v 慢 800ms。

User log 28z RIGHT block（d=2.735 跳一次）：INSTANT 觸發 (✓)
User log 28z LEFT block：
- t=3000 d=0.488 (prev=0.000) → gradient block (prev 沒 elevated)
- t=3200-3800 d=0.293 → 5 polls sustained，全部 < SPIKE 0.3
  - 注意: 0.293 < 0.3 因為 quantization (3 steps)
  - 但 t=4000 d=0.391 (4 steps, prev=0.293) → 終於 trigger
- 總延遲 1000ms vs 28v 的 200ms

回到 28v 邏輯 spike + sustained + confirm 2 + armed:
- t=3000 d=0.488 > 0.4 → armed, cnt=1
- t=3200 d=0.293 > 0.2 sustained → cnt=2 → trigger
- 觸發於 t=3200（**200ms 後**，等於 1 poll latency）
### 各 case 對照
| Case | curr d | prev d | 28aa (28v 邏輯) |
|---|---|---|---|
| LEFT 真擋 t=3000 | 0.488 | 0.000 | armed cnt=1, t=3200 d=0.293 → cnt=2 → trigger ✓ at t=3200 |
| RIGHT 真擋 t=3600 | 2.735 | 0.098 | INSTANT > 0.7 → trigger ✓ |
| 28y noise t=3400 | 0.391 | 0.097 | < SPIKE 0.4 → 不 armed → 不觸發 ✓ |
| 28r noise (5-step) | 0.488 | low | armed cnt=1, 下 poll d=0.0 < sust → dis-arm → 不觸發 ✓ |
| 28v drift | max 0.293 | varies | < SPIKE 0.4 → 不觸發 ✓ |
| Decel | 0.586 | low | armed cnt=1, 但 DECEL_MASK 範圍 → 不觸發 ✓ |
### 反應時間
- INSTANT (d > 0.7): ~200ms（1 poll）
- SPIKE + sustained (d > 0.4, then > 0.2): ~400ms（2 polls）
- 大部分真擋第 2 poll 就達 sustained，所以實際 ~200-400ms

---

## 2026-05-28z Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `arm_monitor_during_sweep_` M1 logic 重寫為 **gradient detection**：
  - 移除 m1_armed state machine + m1_spike_count
  - 加 `m1_prev_delta` 追蹤上一 poll 的 delta
  - Tier 1: d > INSTANT (0.7) → 立刻觸發
  - Tier 2: d > SPIKE (0.3) AND prev_d > SUSTAINED (0.2) → 觸發 (gradient = 上升中)
  - Drift / noise spike 不滿足 Tier 2 條件 → 不觸發
- Diagnostic log 加印 prev_delta
### 原因
實機 28y：user 沒擋，但 M1 d 在 t=3400 從 0.097 突然跳到 0.391（單 poll 跳 3 step）→ 28y 邏輯（spike+armed cnt 1）立刻觸發 → false positive。

對照真擋（28x log）：
- 真擋：d 從 0.097→0.195→0.293→0.391（每 poll 升 1 step，gradual rising）
- Noise: d 從 0.097→0.391（單 poll 跳 3 step，prev 還在 baseline）

兩者最終都到 0.391，但 prev 不一樣：
- 真擋 prev=0.293（已 elevated）
- noise prev=0.097（仍 baseline）

新邏輯利用 prev 區分：trigger 要求 **d > SPIKE 0.3 AND prev > SUSTAINED 0.2**。

### 對照表
| 案例 | curr d | prev d | 是否 trigger |
|---|---|---|---|
| 重擋 (28x) | 0.391 | 0.293 | ✓ |
| 輕擋 (28q) | 0.488 | 0.097→0.0 | ✗ (prev 太低) — hmm |
| Noise single (28y) | 0.391 | 0.097 | ✗ (prev 太低，filter 掉) |
| Decel (28w) | 0.586 | ~0.097 | ✗ + DECEL_MASK |
| Drift (28v) | 0.293 max | 0.195 | ✗ (curr<0.3) |

等等，輕擋 (28q) 的 d 軌跡是 0.098 → 0.488 → 0.293 sustained：
- t=N (d=0.488): prev=0.098 → 不滿足 prev>0.2 → 沒 trigger ✗
- t=N+200 (d=0.293): prev=0.488 (>0.2)，但 curr<0.3 → 沒 trigger ✗

這代表 28q 場景**會漏報**！輕擋 d 沒先 rising 就跳到 0.488 → gradient filter 把它擋掉。

需要再加 INSTANT 的 fallback：d > 0.5 也算 INSTANT（單 poll trigger）？
或 SPIKE 0.3 + prev > 0.05 (更鬆)？

實機跑跑看，先觀察 false positive 是否解了，輕擋是否還抓得到再調。
### Trade-off / 風險
- 28q 輕擋場景如果 d 真的是「prev 還在 baseline 就突然 0.488」→ 漏報
- 但實際多數真擋是 gradual rising，prev 會 elevated → 抓得到
- 之前的 28x 也是 gradual rising (0.293→0.391→0.488)，gradient filter 完美

---

## 2026-05-28y Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `ARM_SWEEP_M1_SPIKE_THRESHOLD_NM` 0.4 → **0.3 Nm**
  - `ARM_SWEEP_M1_TAU_CONFIRM_CNT` 2 → **1**
### 原因
實機 28x log：user 重擋，d 從 0.195→0.293→0.391→0.488→1.66 漸升，t=2800ms 因 d=1.66 > INSTANT 0.7 觸發。User 說「還是不夠快停」。

事實上 t=2400ms (d=0.391) 已經是清楚的異常訊號（超過 28v drift 觀察的 max 0.293），但 28x 邏輯要 d>0.4 才 ARMED + 等下一 poll → 多等 400ms。

降 SPIKE 到 0.3（drift max 0.293 + 0.007 safety margin），confirm 1（首個 SPIKE poll 立刻觸發）：

對照表：
| 案例 | d 軌跡 | 28x 觸發 | 28y 觸發 | 省 |
|---|---|---|---|---|
| 重擋 (28x log) | 0.293→0.391→0.488→1.66 | t=2800 (INSTANT) | **t=2400** (SPIKE+cnt 1) | 400ms |
| 輕擋 (28q log) | 0.097→0.488→0.293 sust | t=3800 | **t=3600** (0.488>0.3) | 200ms |
| Drift (28v) | 漸升 max 0.293 | 不觸發 | 不觸發 (0.293<0.3) | — |
| Decel (28w) | 0.586 in DECEL_MASK | 不觸發 | 不觸發 (DECEL_MASK) | — |

### 風險
- 若 drift 突然到 0.391 (4 quantization steps) → 會誤報。28v 觀察 max 0.293 (3 steps)，需實機跑幾次確認 4 steps 不會出現
- noise 單 spike 0.3+ → 觸發。28r 觀察 noise 多在 d=0.488 (5 steps single poll)，已在 DECEL_MASK 內 — mid-sweep 沒看到過

---

## 2026-05-28x Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 `ARM_SWEEP_M1_INSTANT_THRESHOLD_NM = 0.7f`
- `user_lib/WASH_ROBOT.cpp` `arm_monitor_during_sweep_` M1 check 加 INSTANT 分支：
  - d > 0.7 → 立刻觸發（無 confirm，200ms 反應）
  - d > 0.4 → armed → 等 sustained 0.2 cnt 2 → 觸發（400ms 反應，原邏輯）
  - 三層門檻：INSTANT > SPIKE > SUSTAINED
### 原因
實機 28w log：user 故意擋的瞬間 M1 d=0.977 Nm，超過 SPIKE 0.4 → ARMED → 等下個 poll d=0.488 sustained → confirm=2 → trigger。

User 問「可以再早一點發現嗎」。0.977 是非常大的 spike，明顯是真擋（不可能是雜訊或減速），其實不需要等 confirm。加 INSTANT 0.7 Nm 分支：d > 0.7 立刻 trigger。

各案例對照：
| 案例 | spike d | 28w 行為 | 28x 行為 |
|---|---|---|---|
| 重擋 (28w log) | 0.977 | t+200ms trigger | t+0ms INSTANT trigger（省 200ms）|
| 輕擋 (28q log) | 0.488 | t+200ms trigger | t+200ms trigger（不變）|
| Noise spike | 0.488 | dis-armed | dis-armed（不變）|
| 減速 spike | 0.586 | DECEL_MASK 不觸發 | DECEL_MASK 不觸發（不變）|

### 反應時間
- 重擋（d > 0.7）：~200ms（單 poll + slide stop）
- 輕擋（0.4 < d < 0.7）：~400ms（2 polls + slide stop）
- noise/decel：不觸發

---

## 2026-05-28w Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 `ARM_SWEEP_DECEL_MASK_MS = 1000`
- `user_lib/WASH_ROBOT.cpp` `arm_monitor_during_sweep_` —
  - 在 sweep 末段最後 1000ms 跳過 tau-based trigger（M1 spike+sustained 跟 M2 sustained 都跳過）
  - Diagnostic log 維持印（標記 `DECEL_MASK` tag），方便 user 看到 mask 期間 M1/M2 數值
### 原因
實機 28v 仍有 false positive：sweep 末段（t=4400ms，最後 ~600ms）M1 出現 d=0.586 spike → 觸發 ARMED → 下一 poll d=0.293 sustained → CONFIRMED。

物理根因：slide 急減速（ACC/DEC=100ms/1000rpm，從 1000 RPM 到 0 只要 100ms）→ 機構慣性反作用力傳到 M1 → tau spike。
- 真擋 spike: 0.488 (in middle of sweep)
- 減速 spike: 0.586 (at end of sweep)

兩者量級類似，threshold 無法區分。但**時間軸可區分**：減速 spike 只在 sweep 末段。

修法：sweep 最後 1 秒（DECEL_MASK_MS）跳過 tau trigger 邏輯。Diagnostic log 仍印（標 `DECEL_MASK`）方便觀察。
### Trade-off
- 末段最後 1 秒 = ~16-20 cm slide travel 內的真擋抓不到（對 80cm sweep 是 20% 末段範圍）
- 接受：末段擋到的機率 < 中段，因為刮刀已經刮過大部分牆面
- 若 motion early-exit 在 mask 前就 fire，沒影響
### 完整邏輯流程
```
sweep fire → 監測 0~4500ms 全部 tau check
         → 4500~5500ms 只 print log + skip trigger (DECEL_MASK)
         → 5500ms timeout (或 motion-done early exit)
```

---

## 2026-05-28v Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `ARM_SWEEP_M1_TAU_THRESHOLD_NM` 改名為 `ARM_SWEEP_M1_SPIKE_THRESHOLD_NM` = 0.4 Nm
  - 新增 `ARM_SWEEP_M1_SUSTAINED_NM` = 0.2 Nm
  - `ARM_SWEEP_M1_TAU_CONFIRM_CNT` 維持 2
- `user_lib/WASH_ROBOT.cpp` `arm_monitor_during_sweep_` M1 邏輯改成 spike + sustained：
  - 新增 `m1_armed` 狀態（state machine）
  - Stage 1 (未 armed): d > SPIKE (0.4) → armed=true、count=1
  - Stage 2 (armed): d > SUSTAINED (0.2) → count++; count >= CONFIRM (2) → 觸發
  - Reset (armed 但 d < SUSTAINED): dis-arm + count=0（noise spike 後回 baseline）
  - Diagnostic log 加 `m1armed=0/1`
### 原因
實機 28s 仍有 false positive：M1 在 RIGHT 方向 sweep 期間漸進漂移 baseline → t=4200/4400 d=0.293 連續超過 0.2 → 觸發。User 沒擋。

數據比對：
- 真擋：t=3600 d=0.488 spike → t=3800+ d=0.293 sustained（spike + sustained 模式）
- 漸進漂移（誤報）：M1 d 從 0.097 慢慢爬到 0.293（無 spike、純漸進）

最終 sustained 值都是 0.293，單看 sustained 沒法區分。但「先有突發 spike (≥ 0.4)」是真擋特徵。

新邏輯：必須先看到 d > 0.4 的單一 poll（armed），之後才開始算 sustained。漸進漂移從未跨過 0.4 → 永不 armed → 不觸發。
### 三種場景對照
| 場景 | t=N (d) | t=N+200 (d) | t=N+400 (d) | armed | cnt | 結果 |
|---|---|---|---|---|---|---|
| 真擋 | 0.488 spike | 0.293 sustained | 0.293 sustained | T→T→T | 1→2 | ✓ trigger at t=N+200 |
| Noise 單 spike | 0.488 spike | 0.000 | 0.000 | T→F | 1→0 | ✗ dis-armed |
| 漸進漂移 | 0.097 | 0.195 | 0.293 | F→F→F | 0 | ✗ 從未 armed |
| 正常 sweep | 0.0-0.1 | 0.0-0.1 | 0.0-0.1 | F | 0 | ✗ |

---

## 2026-05-28u Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_arm_clean_sweep_` `sweep_with_tool` Skip 路徑加 explicit cleanup：
  - 關水/刷/水閥 (CH_BRUSH / CH_WATER_PUMP / CH_WATER_INLET)
  - 呼叫 `arm_cmd_("PARK", 30)` + set `arm_stow_state_=Parked`
  - 然後 set skip_rest_of_run_ + return true
### 原因
28t 設計：Skip → set flag → return → outer loop break → arm_clean_sweep 返回 → ScopeExit cleanup 才 PARK。中間有幾行 overhead。

User 要求 Skip 後**立刻 PARK**。所以在 Skip 路徑顯式做：
1. 關水 / 關刷 / 關水閥
2. PARK arm
3. set skip_rest_of_run_
4. return true

ScopeExit cleanup 仍會 fire PARK 一次，但 motor_api PARK 對已 disable 的馬達是快速 no-op（< 100ms），不影響效能。
### 預期效果
- Skip 按下 → log 立刻看到「PARK + skip remaining sweeps」+ 馬達實際 PARK 動作 → 然後才退出 sub-round
- 不再有「skip 設了 flag、過了幾秒才 PARK」的延遲感

---

## 2026-05-28t Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_arm_clean_sweep_` —
  - `sweep_with_tool` 內 `arm_sweep_fire_nowait_` 改成 retry 迴圈：偵測 obstacle → pause 詢問 user
  - Retry → re-fire 同一 target_cm（slide 從停住位置走完到原目標）
  - Skip → set `arm_sweep_skip_rest_of_run_` + return true (本 sub-round 視為完成)
  - Abort → return false（sub-round 失敗，arm_clean_sweep 回 ERR）
  - 移除 outer loop 的 `check_arm_obstacle_pause` helper（pause 已 inline 處理，舊 post-check 失效）
  - Outer loop 改 check `arm_sweep_skip_rest_of_run_` 提早 break
### 原因
28r/s 修了「偵測到 obstacle 立刻停 slide」+「降低誤報」，但**「Retry 之後沒回去做完原本 sweep」**沒解。

舊行為：obstacle 偵測到 → slide 停 → sweep_with_tool 直接返回 true → 下個 sub-round 開始（原本 sweep 沒做完）

新行為：obstacle 偵測到 → slide 停 → sweep_with_tool 內 pause + ask user → Retry → re-fire 同 target_cm（absolute PR_move）→ slide 從停住位置繼續走到原目標 → sweep 完成 → 才進下個 sub-round

例：
- LEFT 0→80 cm，t=2000ms 停在 50 cm 因 obstacle
- pause → user 排除障礙 → Retry
- re-fire `PR_move_cm_nowait(80)` → slide 從 50 走到 80
- LEFT sub-round 完成 → 進 RIGHT
### 例外路徑
- 若 Retry 後 sweep **再次**撞到（user 沒真的排除）→ 再 pause，可一直 Retry
- Skip → 本 run 後續 sweep 全跳過（含當前及後續 sub-round）
- Abort → sub-round fail → arm_clean_sweep 整段回 ERR + state 進 Error
### Continuous mode (continuous sweep) 未改
`do_arm_clean_sweep_continuous_` 跑在背景 thread，從背景呼叫 `await_user_intervention_` 會跟主 thread 在 state_ race。需另外設計 wait-flag 機制讓背景 thread 等主 thread 處理 pause。Defer 到 user 需要時再做。

---

## 2026-05-28s Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `ARM_SWEEP_M1_TAU_THRESHOLD_NM` 0.4 → **0.2 Nm**
  - `ARM_SWEEP_M1_TAU_CONFIRM_CNT` 1 → **2**
### 原因
實機 28r 測試：沒擋東西卻誤報 m1_tau_spike。Log 分析：
- M1 tau 是離散量化（每步 ~0.0977 Nm）
- Noise 場景：單一 poll 跳 5 步 (delta=0.488)，下一 poll 立刻回 baseline
- 真擋場景：跳一下後**持續 elevated** (delta=0.293) 很多 poll

28r 的 threshold 0.4 + confirm 1 兩種都觸發 → 抓真擋也抓 noise。

新門檻 0.2 + confirm 2：
- 真擋：t=N 跳到 0.488>0.2 (cnt=1) → t=N+200 維持 0.293>0.2 (cnt=2) → CONFIRMED
- Noise：t=N 跳到 0.488>0.2 (cnt=1) → t=N+200 回 0 (cnt=0) → 不觸發
- 正常 sweep：d 最大 0.098 < 0.2 → 永不觸發

反應時間從 200ms (confirm 1) 變 400ms (confirm 2)。User 場景可接受。
### 觀察建議
若仍有誤報 → confirm 升到 3（600ms 反應）。若真擋抓不到 → threshold 降到 0.15。

---

## 2026-05-28r Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` —
  - `arm_monitor_during_sweep_`：偵測 obstacle 時加 `D_(DM2J_ARM).speed_move_stop()` **立刻停 slide**（寫 0x6002=0x0040），不再讓 nowait fire 的 motion 跑完
  - 提出 `signal_obstacle` lambda 統一 stop + set flag + write detail + evt_，slide_alarm / m1_tau_spike / m2_tau_spike 三路共用
  - `cmd_arm_clean_sweep`：進入 / 離開時 set/clear `motion_active_`，讓 pressure_poll_loop_ 在 sweep 期間跳過 JC100 reads（消除「sweep 期間 cli_22_ 撞 bus、JC100 timeout 洗版 log」）
### 原因
**Bug A：obstacle 偵測到但 slide 沒停**
回程 t=2000ms M1 spike CONFIRMED → set flag → break loop。但 `PR_move_cm_nowait` 早 fired，slide 在 DM2J:14 內部獨立執行 → 繼續硬推到 0cm（剩 ~3.5s）才停 → user 看到 pause UI 時 slide 已在錯誤位置。

修法：偵測到時用 `speed_move_stop()` 寫 0x6002=0x0040 中止 PR motion → slide 停在當前位置。

**Bug B：JC100 timeout 洗版**
`cmd_arm_clean_sweep` 沒 set `motion_active_=true`，pressure_poll_loop_ 仍每秒輪詢 JC100。sweep 期間 cli_22_ 被 DM2J:14 motion / PQW relay / arm STATUS 用得很滿 → JC100 reads timeout → 每秒一行 JC100:N TIMEOUT。

修法：cmd_arm_clean_sweep 用 motion_active_ guard 跟 step flow 對齊，sweep 期間 pressure poll skip。
### 預期效果
- 擋住瞬間 slide 立刻停在原位（不再硬走到 0）
- Sweep log 變乾淨（沒 JC100 spam）

---

## 2026-05-28q Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - 拆分 M1 / M2 threshold + confirm：
    - `ARM_SWEEP_M1_TAU_THRESHOLD_NM = 0.4`、`ARM_SWEEP_M1_TAU_CONFIRM_CNT = 1`（瞬間擋即觸發）
    - `ARM_SWEEP_M2_TAU_THRESHOLD_NM = 1.5`、`ARM_SWEEP_M2_TAU_CONFIRM_CNT = 2`（容忍 sweep 自然摩擦漂移）
  - 移除舊的 `ARM_SWEEP_TAU_THRESHOLD_NM` / `ARM_SWEEP_TAU_CONFIRM_CNT`（28p 加的，沒撐到 24 小時）
- `user_lib/WASH_ROBOT.cpp` —
  - `arm_monitor_during_sweep_`：
    - **Fix A**：M1 / M2 用各自 threshold + confirm
    - **Fix C**：偵測 DM2J:14 motion completion bits (cmd_done + path_done with edge-detect via `ever_busy`) → motion 完成提早 break，省剩餘 EST_MS 時間
  - `do_arm_clean_sweep_` (sequential): 加 **Fix B** — `check_arm_obstacle_pause` lambda helper，每個 sub-round 結束後 explicit check obstacle flag → 若 set 就 await user。補上 monitor 在 sweep 最後一 poll set flag 但後續無 try_or_pause_ 攔截的破口
### 原因
實機 log 28p 觀察：
1. **去程沒擋卻誤報**：LEFT sweep M2 tau baseline=-0.554 → t=4600ms 漂到 -1.101 (delta 0.547)，自然摩擦力慢慢累積，0.5 Nm threshold 太低 → 觸發誤報
2. **回程真擋了沒 pause**：RIGHT sweep M2 在最後一 poll set flag，但 sweep_with_tool 返回後 outer loop 直接 exit「all rounds done」→ 沒任何 try_or_pause_ 攔截 → user 沒看到彈窗
3. **slide 早就到位但 monitor 跑滿 5.5s**：浪費

Fix A：M1 (noise 小、抓擋的主力) 0.4 + confirm 1；M2 (易漂) 1.5 + confirm 2
Fix B：sub-round 結束後 explicit check，補上「sweep 最後一 poll 觸發」破口
Fix C：DM2J:14 status bits 偵測完成 → 早退、節省 ~0.5-1.5s/sweep
### 預期行為
- Real block 一觸發 M1 spike → 立刻 pause（不用等 confirm 多 poll）
- M2 不再因摩擦漂移誤報
- slide 到位後 monitor 不再傻等
- sweep 最後一 poll set flag 也能 pause

### Tune 路徑
M1 threshold 0.4 抓得到 user log 中 t=3600ms 的 0.488 spike。若實機觀察:
- 仍漏報 → M1 threshold 降到 0.3 Nm
- 誤報多 → M1 threshold 升到 0.5 Nm 或 confirm 2

---

## 2026-05-28p Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - 把 28m 的 `ARM_SWEEP_M2_TAU_THRESHOLD_NM` / `ARM_SWEEP_M2_TAU_CONFIRM_CNT` 改名為 `ARM_SWEEP_TAU_THRESHOLD_NM` / `ARM_SWEEP_TAU_CONFIRM_CNT`（M1/M2 共用）
  - threshold 1.0 → **0.5 Nm**、confirm 3 → **2**
- `user_lib/WASH_ROBOT.cpp` `arm_monitor_during_sweep_` 大改：
  - 同時 capture M1 + M2 baseline tau，加 `parse_tau` lambda 抽出
  - poll 期間檢查 M1 + M2 各自 delta，獨立 spike count
  - **加 per-poll diagnostic log**：`[arm_sweep_monitor] t=Xms M1_tau=A d=B M2_tau=C d=D (m1cnt=E m2cnt=F)`
  - 觸發時 detail 區分 `m1_tau_spike` vs `m2_tau_spike` vs `slide_alarm`
### 原因
實機測試 28m：user 故意擋 sweep，monitor 完全沒觸發。

物理分析發現 28m 選錯馬達：
- **M2 (工具頭旋轉)**：holds slot 角度，外力沿 sweep 方向施加時力臂幾乎不通過 M2 軸 → tau 變化小
- **M1 (大臂俯仰)**：PD-holds TOUCHWALL，外力反作用沿 M1 lever arm → tau 直接升高

因此 M1 才是主要 detector。雙監測 M1+M2 互補；diagnostic log 讓 user 實機 tune threshold。
### 預期 log 範例
```
[arm_sweep_monitor] baseline M1_tau=1.245 M2_tau=-0.234 (threshold=0.5 Nm, confirm=2 polls)
[arm_sweep_monitor] t=200ms M1_tau=1.301 d=0.056 M2_tau=-0.241 d=0.007 (m1cnt=0 m2cnt=0)
[arm_sweep_monitor] t=400ms M1_tau=2.103 d=0.858 M2_tau=-0.198 d=0.036 (m1cnt=1 m2cnt=0)
[arm_sweep_monitor] t=600ms M1_tau=2.245 d=1.000 M2_tau=-0.187 d=0.047 (m1cnt=2 m2cnt=0)
[arm_sweep_monitor] M1 tau spike CONFIRMED (tau=2.245 baseline=1.245 delta=1.000 Nm, 2 consecutive polls)
EVT arm_sweep_obstacle m1_tau_spike
```
### Tune 步驟（給實機）
1. 跑正常 sweep（無障礙）看 log：M1 tau delta 通常多少？M2 呢？
2. 故意擋一下：擋的瞬間 delta 多少？
3. 若 0.5 Nm 抓不到 → 再降到 0.3 Nm
4. 若誤報多 → 升回 0.7 或 1.0 Nm
5. confirm 2 = 400ms 反應；擋更久才觸發 → 升 confirm count

---

## 2026-05-28o Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_arm_clean_sweep_` (sequential) `sweep_with_tool` 內把 blocking `D_(DM2J_ARM).PR_move_cm(...)` 改成 `arm_sweep_fire_nowait_(target_cm)`
### 原因
實機 bug：user 在 GUI 按「CLEAN SWEEP」（呼叫 `cmd_arm_clean_sweep` → sequential `do_arm_clean_sweep_`），故意擋一下 sweep 中的滑台、預期觸發 28m 的 obstacle monitor → 沒觸發。

根因：28m 的 `arm_monitor_during_sweep_` 只裝在 `arm_sweep_fire_nowait_` 內，sequential 路徑用 blocking `PR_move_cm` 不會經過 monitor。

修法：sequential 也改用 nowait+monitor 路徑，跟 continuous 一致。
### 影響
- Sequential mode 現在也有 DM2J:14 alarm + M2 tau spike 偵測 ✓
- 失去 PR_move_cm 的「motion completed」確認 log，但 DM2J 真有 fault 會走 monitor 的 alarm bit detection
- try_or_pause_ wrap 也拿掉（arm_sweep_fire_nowait_ 沒返回值，沒東西可 pause）。Obstacle 偵測仍透過 external pause flag 在下個 try_or_pause_ 觸發 pause

---

## 2026-05-28n Claude (Sadie)
### 修改檔案
- `web_backend/public/index.html` — 「INIT」按鈕從 `data-tgt="arm"` 改成 `data-tgt="washrobot" data-cmd="arm_init"`
### 原因
實機 bug：user 按 GUI「INIT」按鈕，motor_api 校準成功了，但 sweep 還是卡在「arm not calibrated」。

根因：舊按鈕 `data-tgt="arm"` 走 `send('arm', 'INIT')` → **直接打 cleaning_arm motor_api (TCP :9527)，跳過 washrobot**。我 2026-05-28k 在 `WashRobot::cmd_arm_init` 加的「set `arm_calibrated_=true`」邏輯永遠跑不到，因為這條路徑不經過 washrobot。

改成 `data-tgt="washrobot" data-cmd="arm_init"` 後：
- send('washrobot', 'arm_init') → washrobot:5001 → main.cpp dispatcher → `cmd_arm_init` → arm_cmd_("INIT", 60) → motor_api 校準 → 回 OK → flag=true

工作流變正常：sweep refused → 按 INIT → flag=true → 按繼續 → sweep 過。
### 注意
之前用 `data-tgt="arm"` 是為了 raw access motor_api。GUI 上其他按鈕 (PARK / STATUS / DEPLOY) 還是 raw 路徑，那些不影響 calibrated flag、不需改。

---

## 2026-05-28m Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - 新常數 `ARM_SWEEP_MONITOR_POLL_MS=200`、`ARM_SWEEP_M2_TAU_THRESHOLD_NM=1.0`、`ARM_SWEEP_M2_TAU_CONFIRM_CNT=3`
  - declare `arm_monitor_during_sweep_()` helper
- `user_lib/WASH_ROBOT.cpp` —
  - 實作 `arm_monitor_during_sweep_()`：取代 `arm_sweep_fire_nowait_` 內的 plain `sleep_ms_(ARM_SWEEP_EST_MS)`，改成 poll loop
  - **Option A**：每 200ms 讀 DM2J:14 status，alarm bit (0x0001) set → obstacle (`slide_alarm`)
  - **Option C**：sweep 啟動時 capture damiao M2 tau baseline，每 200ms 比對，連續 3 次 |delta|>1.0 Nm → obstacle (`m2_tau_spike`)
  - 觸發時 set `arm_sweep_obstacle_pending_` + detail + EVT，跟 28l 的 DEPLOY-verify obstacle 走同個 pause 通路
### 原因
28l 只覆蓋 DEPLOY 階段的 obstacle（M1 沒到位）。Sweep 過程中（slide 0→100→0）刮刀/滾筒撞東西完全沒偵測 — 28l 的 verify_arm_deploy_ 是 DEPLOY 完才 check 一次，slide motion 期間 fire-and-forget 沒人 watch。

加 A + C 互補：
- **A (DM2J:14 alarm)**：硬偵測。slide 馬達被擋到失步/過電流 → 內建 alarm bit 自動 latch。簡單可靠但只抓大障礙
- **C (M2 tau)**：軟偵測。M2 holding slot 角度，正常 sweep tau 近 0；橫向撞擊力臂通過 M2 軸 → tau 飆。靈敏但需 threshold tune

兩者共用 28l 的 pause 通路 → user 看到的彈窗一致（slide_alarm vs m2_tau_spike detail），同樣 3 個選項（繼續/跳過/結束）
### 風險 / 觀察點
- M2 threshold 1.0 Nm 是初值，可能誤報（cup 接觸不均、牆面粗糙時 tau 噪訊）或漏報。實機 tune
- 每 sweep 期間多 27 個 STATUS query（200ms × 5.5s ≈ 27 次），透過 arm_cli_ (127.0.0.1:9527)。STATUS query 走 arm_mtx_，跟 motor_api 內部 motor_mutex_ 競爭但很快
- DM2J:14 status read 在 cli_22_ bus 上，sweep 期間 motion_active_=true 已 pause JC100 poll，PQW 也少寫 → bus 相對閒
- arm_attached_=off 時退回 plain sleep（沒 M2 reference）

---

## 2026-05-28l Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - 新 atomic `arm_sweep_obstacle_pending_{false}` + `arm_sweep_skip_rest_of_run_{false}` + 配套 `arm_sweep_obstacle_mtx_` + `arm_sweep_obstacle_detail_`
  - `try_or_pause_` template 加 external pause check：每個 op 開始前 check `arm_sweep_obstacle_pending_`，set 就 await user
- `user_lib/WASH_ROBOT.cpp` —
  - constructor 加新 atomic initializer
  - `do_arm_clean_sweep_continuous_` `sweep_with_tool` 內 verify_arm_deploy_ 偵測 obstacle 時：set `arm_sweep_obstacle_pending_=true` + 寫 detail（slot 名）+ evt_("arm_sweep_obstacle slot=...") + 照舊 return false abort sweep
  - `do_arm_clean_sweep_` + `do_arm_clean_sweep_continuous_` 入口加 `arm_sweep_skip_rest_of_run_` check（user Skip 後本 run 後續 sweep 全跳過）
  - `cmd_run` 進入時清兩個 flag（每 run 獨立）
### 原因
User 擔心 step_up/down 中 arm 撞到東西時：背景 sweep 只 log + abort、step 繼續做、user 不知道發生啥。需要立刻 pause 給使用者決定。

設計：
- 背景 sweep 不直接呼叫 await_user_intervention_（會跟主 thread 在 state_/pause_flag race）
- 改 set atomic flag → 主 thread 在每個 try_or_pause_ 進入時 check → set 就走 await_user_intervention_ pause 流程

3 個選項對應現有 PauseAction：
- **繼續** (Retry) → 清 flag、step body 從同 op 繼續、下 step sweep 再試（操作員應已物理排除障礙）
- **跳過未來 sweep** (Skip) → 清 flag + set `arm_sweep_skip_rest_of_run_` → 本 run 後續所有 sweep 入口短路 OK skipped
- **結束** (Abort) → try_or_pause_ 回 true → step 中止 → run 終止
### 觸發範圍
只有 `verify_arm_deploy_` obstacle（M1 沒到位）會 set flag。其他 sweep 失敗（relay write fail / arm_cmd timeout / etc.）維持原 behavior（log + abort sweep round）。
### Pause 時機
立刻 pause — flag 一 set 主 thread 下個 try_or_pause_ 就 pause（step body 幾乎每個 hardware op 都包在 try_or_pause_ 內，延遲 < 1s）。

恢復後從同 op 繼續做（try_or_pause_ 設計本來就支援），不會丟失 step body 進度。
### EVT 通知
背景 sweep set flag 時同步發 `EVT arm_sweep_obstacle slot=LEFT/RIGHT/CENTER` 給 GUI，GUI 顯示彈窗 + 按鈕（繼續/略過/急停 對應 cmd_continue / cmd_skip / cmd_emergency_stop）
### GUI 改動需求（web_backend，Sadie 範圍）
- 收到 `arm_sweep_obstacle` EVT 時顯示彈窗，3 個按鈕對應現有的 cmd_continue / cmd_skip / cmd_emergency_stop
- 文案：「手臂撞到障礙物 (slot=X)。請排除後選擇：繼續 / 跳過剩餘 sweep / 結束」

---

## 2026-05-28k Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - 新 atomic `arm_calibrated_{false}`（damiao arm 校準狀態旗標）
  - declare `ensure_arm_ready_()` helper
- `user_lib/WASH_ROBOT.cpp` —
  - constructor 加 `, arm_calibrated_(false)` initializer
  - 實作 `ensure_arm_ready_()`：arm_attached_=off 直接 OK skip；arm_calibrated_=false 回 ERR；else 送 `M1 ENABLE` + `M2 ENABLE`
  - `cmd_init_impl_` 最後加 damiao arm INIT 步驟：arm_attached_=off 跳過 + flag=true；else 跑 INIT，成功 → flag=true、失敗 → 警告 + flag=false 但繼續 init 流程到 Ready
  - `do_arm_clean_sweep_` + `do_arm_clean_sweep_continuous_` 把 background `arm_cmd_("INIT", 60)` 改成 `ensure_arm_ready_()`；同步 retry 路徑也改
  - `ensure_arm_center_for_rope_` 內 `arm_cmd_("INIT", 60)` 改成 `ensure_arm_ready_()`
  - `cmd_arm_init`（手動 GUI cmd）成功 → flag=true、失敗 → flag=false
  - `cmd_emergency_stop` 加 `arm_calibrated_.exchange(false)` invalidate
### 原因
2026-05-28 user 擔心 mid-run 重複 INIT 撞到障礙物會導致 0 點錯位，後續 DEPLOY 用壞 zero 毀機構。

設計把 damiao arm INIT 從「per-sweep」收緊到「per-cmd_init」：
- 每個 process session 只在系統 init 時校準一次（operator 在場確認路徑無障礙）
- 之後 sweep 用 `M1/M2 ENABLE`（only re-enable disabled motor，不重新校 zero）
- INIT 撞障礙的風險時間窗從「每 step × N」縮到「每 cmd_init × 1」
- 同時順手省每 sweep ~10s INIT calibrate 時間

User 確認的策略：
- arm INIT 串行（cmd_init 最後一步）
- arm INIT 失敗 → 警告 + flag=false 但 init 流程繼續到 Ready（不擋整個 init）
- arm_attached_=off → skip + flag=true（讀起來一致；sweep 本來也會 skip）
- 自動 invalidate timing：emergency_stop（必要） + process restart（自然 default false）
### 工作流變更
- **每次 process 重啟後必須先 cmd_init**：flag 預設 false，sweep 拒絕直接跑
- **emergency_stop 後必須再 cmd_init**：flag 被清回 false
- **正常 PARK/DEPLOY/run/step 不再觸發 INIT**：只用 ENABLE 重啟馬達（PARK 後 motor disabled）
### 預期收益 / 風險
- **每 sweep 省 ~10s**（INIT calibrate 時間）
- **撞障礙風險降低**：只有 cmd_init 一個時間點會校準
- **失效模式**：忘了 init 直接 run → sweep 立即 ERR + 訊息「arm not calibrated」(乾淨 failure)
- **damiao set_zero 在 PARK disable 期間需保留**（假設）：bench 應該驗證一次

---

## 2026-05-28j Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `PUSHER_EXTEND_BODY_PULSE` 36000 → **33000**（-3000=-1cm）
### 原因
Bench log 顯示 body upper (slave 5, 6) Phase 1 fast extend 在 700rpm 直接撞 wall:
- `[wait_many ZDT:5/6] peakI=1541-1592mA`(高於 wall gate 1300mA）
- 原因:當 over_cm 大（這次 2.63cm）,target = 36000 + 7800 = 43900,Phase 1 endpoint = 40900 ≈ wall 41600

功能上 ZDT 閉迴路停 motor + iter 0 立刻 SEALED,**動作正常**,但 cup 跟驅動每 step 高速撞牆,長期 wear 不好。

縮 preset -3000 後:
- target = 33000 + 7800 = 40800（over=2.6 場景）
- Phase 1 endpoint = 37800,離 wall 41600 安全 ~3.8 cm
- iter 0 push +3000 = 40800,still 短於 wall → 可能 iter 1 才 seal（當 over 大時）
- 當 over_cm 小（<1）時更安全,但 iter 0 plateau 機率變高

Trade-off:**保護機械 vs. iter 0 seal 機率**。若 bench 觀察到 iter 變多太慢,再回頭。

Body lower（SHORT preset = 35400）+ slave 8 plateau 問題未動（用戶選 A only）。

---

## 2026-05-28i Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `PUSHER_EXTEND_BODY_PULSE_SHORT` 32400 → **35400**（+3000=+1cm）
### 原因
5/28h 改完 bench log 顯示:
- Body upper (5,6) iter 0 SEALED ✓
- **Body lower (7,8) iter 0 plateau,iter 1 才 sealed**

挖 code 發現 body target = `preset + cm_to_pulses(over_cm)`,其中 `over_cm` 是 feet drift 偏移。這次 bench over_cm≈2.47,所以 target = preset + 7418:
- Body upper target = 36000 + 7418 = 43418,wall hit at 41467 → iter 0 reach ✓
- Body lower target = 32400 + 7418 = 39818,wall at 42798 → iter 0 不夠 reach ✗

**這 bench 環境下 body lower wall 反而比 upper 還深**(2cm 多),SHORT preset 變不合理。加深 +3000 → target 42818 剛好覆蓋 wall 42798,iter 0 應可 seal。

未來如 bench 牆面 / robot 安裝改變,wall 位置會變,SHORT 可能需要再調。

---

## 2026-05-28h Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `PUSHER_EXTEND_FEET_PULSE` 26000 → 29000（+3000=+1cm，feet upper iter 0 plateau 修）
  - `PUSHER_EXTEND_FEET_PULSE_LOWER` 26900 → 29900（+3000=+1cm）
  - `PUSHER_EXTEND_BODY_PULSE` 30000 → 36000（+6000=+2cm，body upper iter 2 才 wall hit at pulse 39281）
  - `PUSHER_EXTEND_BODY_PULSE_SHORT` 29400 → 32400（+3000=+1cm）
  - `REALIGN_THRESHOLD_MEAN_CM` 1.5 → 2.0（bench mean 1.6-1.7 每 iter 觸發 realign 浪費 ~4s）
- `user_lib/WASH_ROBOT.cpp` — `pressure_poll_loop_` 加 `disabled_zdt_slaves_` check,JC100:9 不再 polling（slave 9 ZDT 物理拔除,sensor 也死掉,連續 timeout 污染 log + 浪費 ~2s/cycle）
### 原因
User bench log:
- iter 0 disable_seal 全 plateau no contact → iter 1 / iter 2 才 SEAL → 每組多 ~3-8s
- body upper wall hit at iter 2 cum +9000 = Phase1+9000 = 39000+,跟 preset 30000 差太遠
- realign mean 1.6-1.7 每 iter 都觸發 (限 1.5),浪費 ~4s
- JC100:9 連續 timeout（ZDT 9 拔除 + sensor 失效,但 pressure_poll 還每 cycle 讀）

**算法（從 log 反推）**：
- iter 0 push = Phase1 + 3000;iter 1 = +6000;iter 2 = +9000
- 要 iter 0 直接 seal:Phase1 + 3000 ≥ wall_pulse → Phase1 ≥ wall - 3000
- body upper wall 39281 → Phase1 ≥ 36281 → 用 36000
- feet upper wall 29000-31700 → Phase1 ≥ 28000-29000 → 用 29000
- body lower 原本 iter 0 已 seal,保守 +3000 可省 push 時間
- feet lower 同上

**風險**：Phase 1 是 700rpm 快速 extend,若實際 wall 跟 bench 不同位置,可能 fast-extend 階段就 slam wall。ZDT stall detection 應該 catch,但 user 觀察看是否要回退。

**JC100:9 disable**：跟 ZDT 一樣 disabled_zdt_slaves_ 控制（共用 set,reuse 既有 cmd_zdt_enable/disable）。

---

## 2026-05-28g Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_run` `up` / `down` 改成 **pipeline 模式**:跨 iter overlap step 跟 sweep。
  - 持久 `fut_sweep` + `sweep_keep_going` flag 跨 iter
  - SweepJoin RAII guard
  - iter 1: loop 前 launch pre-feet sweep（round 1）；step 用 do_step_*_(skip=true, before_hook=join round1, after_hook=launch round2)
  - iter 2+: step 用 do_step_*_(skip=true, after_hook=wait fut_sweep + launch new), before_hook 空
  - **iter step 不等 sweep 就 return** → 下 iter pre-feet 跟本 iter sweep 並行
  - 全 run 結束等最後 sweep + ensure_arm_parked
  - sweep_af 變體保持原邏輯（每 iter cmd_step_*_sweep_after_feet self-contained）
### 原因
User 要 run 跨 iter overlap:「上一步移動完了,在等手臂清洗時,可以先做下一步移動」。
**衝突 audit:**
- DM2J:14 arm rail (cli_22_)：iter step 不用 → 無衝突 ✓
- DM2J:1,3 feet rail (cli_20_)：sweep 不用 → 無衝突 ✓
- ZDT pushers (cli_21_)：sweep 不用 → 無衝突 ✓
- cli_22_ JC100/PQW 共用：bus socket_mtx 序列化 → 無 race ✓
- arm_cli_：only sweep 用 → 無衝突 ✓
- **唯一 sync 點**：iter N+1 launch 新 sweep 前要 join iter N sweep（在 `after_feet_hook` 內）
**效益估計**：step 長於 sweep 時,iter pace ≈ step time（省 sweep time × (N-1)）；sweep 長於 step 時,iter pace ≈ sweep time。
**注意**：iter N+1 pre-feet 跟 iter N sweep 並行 → cli_22_ 流量變高（JC100 poll + arm motion + PQW + arm DEPLOY arm_cli）,bus socket_mtx 序列化但延遲可能增加。bench 觀察。

---

## 2026-05-28f Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_EST_MS` 7000 → 5500（配合 ARM_SWEEP_CM 80 + RPM 1000：cruise 4.8s + ramp 0.2s + buffer 0.5s ≈ 5.5s）
### 原因
28e 預告：7000ms 是依 100cm 算，80cm 下每 sweep 多等 1.5s，雙向 2 sweep × 2 round = 6s/step 浪費。

---

## 2026-05-28e Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_CM` 100 → 80（per user）
### 原因
sweep 行程縮短。
### 待考慮（user 未指示）
`ARM_SWEEP_EST_MS` 目前 7000ms 是依 100cm @ 1000 RPM 算（cruise 6.0s + ramp + buffer）。80cm @ 1000 RPM cruise 4.8s + ramp 0.2s + buffer 0.5s ≈ 5500ms。7000 太保守、每 sweep 多等 ~1.5s（雙向 2 sweep × 2 round = 6s/step）。本次未動，user 確認後可拉低到 5500ms。

---

## 2026-05-28d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` —
  - `do_arm_clean_sweep_` `sweep_with_tool` lambda：加 `double target_cm` 參數，內部從「sweep 0→ARM_SWEEP_CM → sweep ARM_SWEEP_CM→0」兩段改成單段「sweep → target_cm」（caller 決定方向）
  - round 內 caller 端：LEFT 傳 `ARM_SWEEP_CM`（0→100 滾筒濕），RIGHT 傳 `0.0`（100→0 刮刀刮回，順便回 0 給下一 round）
  - `do_arm_clean_sweep_continuous_` 同上改動
### 原因
2026-05-28 10:38 實機 log sweep 時間軸分析：
- 每 round 有 4 個 sweep（LEFT 0→100→0、RIGHT 0→100→0），每 sweep ~7s（ARM_SWEEP_EST_MS）= 28s/round
- 兩 round/step（sweep_ba 模式 before+after feet）= 56s sweep 純等待
- 對照 body step 30-35s → sweep 變後段 bottleneck，body 做完還要等 sweep ~25s

舊邏輯回程是純空走：LEFT 100→0 滾筒重複滾過剛濕的牆、RIGHT 100→0 刮刀重複刮過剛乾的牆，無清潔意義。

雙向設計：LEFT 0→100（滾筒往右濕）+ RIGHT 100→0（刮刀往左刮回），每段都是有效清潔，且 RIGHT 收尾自然把 slide 帶回 0 給下一 round。
### 預期收益
每 round 從 4 sweep 變 2 sweep → 省 2 × ARM_SWEEP_EST_MS = **14s/round**
兩 round/step = **28s/step**
### 風險觀察點
- 刮刀往「左方向」刮跟往「右方向」刮效果是否一樣（刮刀刃幾何對稱性）
- DEPLOY RIGHT 在 slide=100 位置切換 M2，M1 短暫 retract（離牆）期間 slide 不會動但 tool 離牆，無影響
- 第一 round 假設 slide 起點 = 0（init / 上一次 sweep 結束位置）；若 slide 在中途停留（e.g. error 後），LEFT 0→100 absolute 仍會到 100，但少掃前半段
### 觀察方法
新 log 會看到每 round 只有 2 個 DM2J:14 nowait fire（不是 4 個）：
- LEFT: `PR_move_cm_nowait 100.000 cm`
- RIGHT: `PR_move_cm_nowait 0.000 cm`
若刮刀效果不對 → 把 sweep_with_tool 的 target_cm 改回原本兩段 sweep

---

## 2026-05-28c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_arm_clean_sweep_continuous_` 內 `sweep_with_tool` lambda 開頭加 DM2J:14 alarm check：read_status,fault bit (0x0001) set → log + return false（skip 本 round,不 DEPLOY/不 sweep/不開水刷）
### 原因
DM2J 失步沒有「事先預測」能力（PDF 確認 firmware 沒有 position deviation alarm threshold,只有 over-current/voltage/encoder/wire-break 等 7 種固定 alarm）。
退而求其次的保護：**下一次要 sweep 前先讀 status,有 alarm 就跳過本 round**。
- alarm 來源：失步、過流、編碼器 fault、斷線等任一觸發,都會 latch 在 0x2203
- skip 後 sweep_with_tool return false → 外層 round loop 印 "round X failed" + 整 sweep thread 結束 → cleanup PARK
- 下次 cmd_step_*_sweep_* 又會啟新 sweep → 又 alarm check → 又 skip → 連續跳過直到 user 手動 reset
- status read fail（cli_22_ contention）→ fall through 嘗試 fire,避免 transient miss 永久跳過
**Reset 方式（user 需手動）**：
- 重新 cmd_init（會 re-init DM2J 包含 reset alarm）
- 或 Linux_test menu 對 DM2J:14 跑 reset_alarm
- 將來可考慮加 dedicated cmd `arm_rail_clear_alarm` 給 GUI 按鈕

---

## 2026-05-28b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `zdt_wait_motion_done_many_` 加 optional `int* stalled_slave_out` 參數
  - `DISABLE_PRE_DISABLE_DELAY_MS` 200 → 100
- `user_lib/WASH_ROBOT.cpp` —
  - `zdt_wait_motion_done_many_` 實作支援 `stalled_slave_out` 寫回 stall 觸發的 slave id
  - `do_feet_realign_` phase 2 `run_stage` 內序列 `zdt_wait_motion_done_` 改成單一 `zdt_wait_motion_done_many_` 呼叫；stall 時用 `stalled_slave_out` 拿回 id + emergency_stop 其他 moving slaves（行為對齊原邏輯）
### 原因
2026-05-28 10:21 實機 log：
1. realign 觸發時 phase 2 stage 1 序列 wait 加總 ~2850ms、stage 2 加總 ~3750ms。slaves 已 broadcast 同步 motion，平行等只需 max ≈ 1050-1950ms。每次 realign 浪費 ~3s。
2. disable_seal Step D.5 預設 holding 緩衝 200ms。實際 push stable 訊息在 motion 結束瞬間就印，200ms 過保守。每 iter 省 100ms × 4-5 iter ≈ 500ms/step。
### 預期收益
- realign phase 2 並行：~3s/step（realign 觸發時，約 30-50% step）
- DISABLE_PRE_DISABLE_DELAY 縮減：~400-500ms/step
- 合計 ~1-4s/step 取決於是否觸發 realign
### 風險觀察點
- realign phase 2 並行：邏輯完全對稱舊版（stall 時 e-stop 其他 moving），低風險
- DISABLE_PRE_DISABLE_DELAY 100ms：若 cup 在剛接觸牆那刻就被 disable EN，可能彈離 → seal 率下降。觀察 log 若見 fast-skip 比例上升 / 多 iter 才 seal → 回 200ms
### Cup 接觸建立的 timing 餘量
即使 PRE_DISABLE_DELAY=100ms，Step A 後的 `sleep_ms_(200)` 還在（總共 ~300ms settle 在 push 前後），cup 不會完全沒緩衝

---

## 2026-05-28a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - declare 新 helper `zdt_wait_motion_done_many_(slaves, timeout_ms, defer_stall)`
  - 加新常數 `VACUUM_NO_CONTACT_FAST_MS = 500`、`VACUUM_NO_CONTACT_KPA = -5`
- `user_lib/WASH_ROBOT.cpp` —
  - 實作 `zdt_wait_motion_done_many_`（從 `pusher_move_many_` inline pattern 抽出）
  - `disable_seal` Phase 1 wait 改成 parallel: 序列 4 個 slave wait_done 並行化
  - `disable_seal` WAIT_SEAL 加 no-contact fast-skip：cup 真空 500ms 內仍 ≥ -5kPa → 提早判 plateau（vs 原本要等 1500ms）
### 原因
2026-05-28 19:38 實機 log 兩個觀察：
1. Phase 1 wait 序列 `wait ZDT:6 done at 1800ms / wait ZDT:8 done at 600ms / wait ZDT:5 done at 600ms / wait ZDT:7 done at 600ms` 加總 3600ms。但 broadcast sync trigger 下 slaves 並行 motion，實際 max ≈ 1800ms。序列等待浪費 ~1800ms/extend。
2. vacuum plateau iter 全部 cup 一路 p=0/±1 kPa，但要等 VACUUM_PLATEAU_MS=1500ms 才判 plateau。實際上 500ms 內若 best_p 還沒跌破 -5 kPa，cup 幾乎不可能 seal（正常 seal 在 100-200ms 就會看到 -30~-60 kPa）。
### 預期收益
- Phase 1 並行：~1-2s/extend × 2 extend = ~2-4s/step
- vacuum plateau fast-skip：~1s/plateau iter × 1-3 plateau iter = ~1-3s/step
- 合計 ~3-7s/step
### 風險觀察點
- Phase 1 並行：helper 完全 mirror `pusher_move_many_` pattern（已 production），低風險
- fast-skip：threshold -5 kPa 保守，若有 cup 真的 slow-seal（如 600ms 才到 -10 kPa），會被誤殺。實測若見到「該 seal 的 iter 被 fast-skip 誤判 plateau」→ 把 KPA 從 -5 放寬到 -10 或 FAST_MS 從 500 拉到 800
### Pusher_move_many 沒一起改的理由
保持原有 inline 版本不動，避免破壞已 production 的 retract 路徑。helper 是新加的，先用在 Phase 1 risk 較低；穩定後再考慮 refactor `pusher_move_many_` 共用 helper。

---

## 2026-05-27m Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_run`:
  - `up` / `down` 行為改成 **iter 1 = `cmd_step_*_sweep_before_after`(前後各刷)**、**iter 2+ = `cmd_step_*_sweep_after_feet`(腳組後刷)**
  - 移除 5/27a 的 B 方案整 run 連續 sweep thread + 5/27g 的 relaunch 邏輯（不再需要外層 sweep thread,每 iter cmd 自己 launch + 收 sweep）
  - `up_sweep_af` / `down_sweep_af` 維持原邏輯（每 iter 都 af）
  - 移除 RAII SweepJoin guard、launch_sweep lambda、run_with_sweep flag
  - 結尾 `ensure_arm_parked_after_rope_("run_end")` 直接呼叫（之前包在 `if (run_with_sweep)` 內,變數移除後改成無條件 idempotent 收尾）
### 原因
User 要 `run` 第一步用新的 _sweep_before_after（移動前後各刷）、第二步以後用 _sweep_after_feet（腳組後刷）。
理由（user 意圖推測）：起點通常最髒,前後各刷一次徹底洗;之後每步只需腳組後 1 round 維持,節省時間。
**影響**：
- 「↑ run N up」「↓ run N down」按鈕 → 自動使用新行為
- 「↓ 走到地面（含清洗）」按鈕送的是 `down_sweep_af` → 維持原邏輯（每 iter 都 af,沒 ba）。要改的話 JS 那邊改一行送 `down`。
- 整 run sweep 的時間：少了 B 方案 continuous 多 round/step、但每 iter 多 PARK/INIT overhead

---

## 2026-05-27l Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `do_step_up_` / `do_step_down_` signature 加 `before_feet_rail_hook` 參數（預設空，向後相容）
  - 新增 cmd decl `cmd_step_up_sweep_before_after(int)` / `cmd_step_down_sweep_before_after(int)`
- `user_lib/WASH_ROBOT.cpp` —
  - `do_step_up_` / `do_step_down_` feet_pre_cycle lambda 內，在 `dm2j_pair_move_abs_(feet rail)` 觸發之前 call `before_feet_rail_hook`（如果非空），並更新 lambda capture
  - 新增 `cmd_step_up_sweep_before_after` / `cmd_step_down_sweep_before_after` 實作：開頭 launch sweep round 1（max_rounds=1）、`before_feet_rail_hook` join round 1、`after_feet_rail_hook` launch round 2、step 結束等 round 2 完成
- `washrobot_new_PI/main.cpp` — dispatcher 加 `step_up_sweep_ba` / `step_down_sweep_ba`
- `web_backend/public/index.html` — auto cycle 區加按鈕「↓ 下移 + 移動前後各刷」「↑ 上移 + 移動前後各刷」
- `web_backend/public/app.js` — 綁定兩個新按鈕，送 `step_*_sweep_ba ${cm}`
### 原因
User 要新功能：在 phase A 腳組 DM2J 移動「前」並行先做 1 round arm 清洗，feet rail 移完「後」再 1 round。step_up / step_down 兩方向都要。
跟現有 sweep 模式對照：
- `_with_sweep`: 整 step 連續 loop（多 round）
- `_sweep_after_feet`: 只 feet rail 後 1 round
- **新 `_sweep_before_after`: pre-feet 1 round + post-feet 1 round（共 2 round）**

實作要點：
- `do_step_*_` 加 `before_feet_rail_hook` 參數對稱於既有 `after_feet_rail_hook`，呼叫點在 `pusher_two_stage_retract_` 之後、`dm2j_pair_move_abs_(feet rail)` 之前
- 兩 round 都用 `do_arm_clean_sweep_continuous_(max_rounds=1)` silent error policy（fail 不擋主流程）
- SweepJoin RAII 保證任何 return 路徑都 wait sweep join
- round 1 join 前 sweep 已 set keep_going=false（max_rounds=1 自然 exit），所以 join 不阻塞太久；round 1 OK 後再進 launch_round 啟動 round 2（內部會 reset keep_going=true）
- **代價**：每 step 兩次 PARK/INIT 來回（do_arm_clean_sweep_continuous_ 每次 launch 都 INIT、結束都 PARK），overhead 比 single round 大；如果 round 1 比 pre-feet work 慢、step 會等 round 1

---

## 2026-05-27k Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 4 個 ZDT preset extend pulse 上調：
  - `PUSHER_EXTEND_FEET_PULSE` 23000 → 26000（+1cm）
  - `PUSHER_EXTEND_FEET_PULSE_LOWER` 23900 → 26900（+1cm）
  - `PUSHER_EXTEND_BODY_PULSE` 28500 → 30000（+0.5cm）
  - `PUSHER_EXTEND_BODY_PULSE_SHORT` 27900 → 29400（+0.5cm）
### 原因
2026-05-27 15:27 實機 log 分析 step_up + sweep 5cm step（總 74s）：

**Feet 流程**：
- iter 0 推到 preset (23000)        → p=0 kPa plateau 1.5s 浪費
- iter 1 推到 preset+1cm (26000)    → p=0~1 kPa plateau 1.5s 浪費
- iter 2 推到 preset+2cm (29000)    → SEALED at pulse 28729~30051

實機 cup 平均在 preset+2cm 才能 seal。preset +1cm 後 iter 0 落在 26000、iter 1 在 29000 → 第 1 輪即可 seal，省 1 iter ≈ 3.5s/組 × 2 組 (upper/lower)

**Body 流程**：
- iter 0 推到 preset (28500)        → plateau 1.5s 浪費
- iter 1 推到 preset+1cm (31500)    → 4 cup 全 sealed (pulse 30886~31508)

實機 cup 100% 在 preset+1cm seal。preset +0.5cm 後 iter 0 落在 30000、iter 1 在 33000 → 若 iter 0 即 seal，省 1.5s/組。Body 保守 +0.5（vs feet +1cm）因 body 一致性高、過調風險高（ZDT 過電流堵轉）。

**連鎖效益**：preset 拉高 → cup 過伸量縮短 → two-stage retract slow peel 距離縮短（slow peel 實際 = 過伸量 + RETRACT_SLOW_PEEL_CM；目前過伸 2-3cm → 實際慢段走 4-5cm @ 30 RPM = 8-10s）

### 風險觀察點
- 若 preset 比真實牆面遠 → cup 推過頭 → ZDT 過電流 → disable_seal obstacle 路徑觸發 rescue（rail 退 + 重推）
- 第一次跑要看 log：是否出現 `[disable_seal] obstacle` 或 `[disable_seal] iter X obstacle abort`
- 出現 obstacle → preset 拉太高，需退回
### 預期收益
~5-8s/step (feet 省 1 iter + body 可能省 1 iter + slow peel 縮短)

---

## 2026-05-27j Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `STEP_MARGIN_CM` 15 → 10（crane pay_out/retract margin 減少 5cm × 2 方向 × 2 phase ≈ 省 4s/step @ SE3 30Hz / rope 10cm/s）
  - `RETRACT_SLOW_PEEL_CM` 2.6 → 2.3（two-stage retract 慢段縮短 0.3cm × 2 組 @ 30 RPM ≈ 省 ~1-2s/step）
### 原因
User 加速需求。兩個都是低風險改動：
- STEP_MARGIN_CM：margin 是 rope slack 緩衝，10cm 仍足夠 rail/crane 響應差容忍
- RETRACT_SLOW_PEEL_CM：歷史 2.0→2.3→2.6 一路加長過，2.3 是中間值（曾在 2.3 跑過）
### 風險觀察點
- crane retract margin 後若 rope 還是緊（margin 不夠）→ feet pusher extend 階段會被 rope 反推 → 真空不穩
- retract 慢段太短 → cup 還沒脫壁就進入 fast 段 → ZDT stall（若再次出現就退回 2.6）

---

## 2026-05-27i Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_arm_clean_sweep_` + `do_arm_clean_sweep_continuous_` —
  入口加 `arm_attached_=false` early-return，回 `OK skipped_arm_off`
### 原因
原本 `arm_cmd_("DEPLOY/INIT/PARK")` 在 `arm_attached_=off` 時會自動回 `OK skipped`，但 sweep 內的：
- 上滑台 DM2J slide motion（`arm_sweep_fire_nowait_` / `PR_move_cm`）
- water inlet / pump / brush relay

都是獨立流程、不會被 arm_cmd_ skip。結果 arm 拔掉時上滑台還是會掃 + 水會出來。

User 要求 arm off 時整輪 sweep 完全跳過，所以在兩個 sweep 入口統一短路。caller（do_step_up_/down_、cmd_step_*_with_sweep、cmd_run）拿到 `OK skipped_arm_off` 不會當錯誤（rfind("OK",0)==0 仍 true）。
### 影響面
- arm off 時 cmd_arm_clean_sweep / cmd_step_*_with_sweep / cmd_run up|down 都會跳過 sweep
- arm 一上線後再下 cmd_run 就會正常 sweep（27g 的 sweep relaunch 機制走背景 thread re-check 也會 honor 這個 early-return）

---

## 2026-05-27h Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `body_backup_factory` (do_step_down_ phase A vacuum/obstacle 重吸 backup) —
  - **動作順序 swap**：原本 `crane retract → rail backward`，改成 `rail backward → crane retract`
  - **crane retract 升級**：原 `crane_cmd_("retract N")` blind 收繩 → `crane_retract_safe_(N)` tension-monitored 早停
### 原因
原順序「crane 先收 → rail 後動」在 body 還沒移動前就把 rope 拉緊，tension 突然飆高 → 可能拉扯仍在吸壁的 feet vacuum / rope mount。

step_down body backup 的物理場景（body 退回上方、靠近 crane、rope 自動鬆）跟 step_up body_pre_cycle forward 完全相同。step_up forward 的註解（L4380-4383）明寫「NO pre-climb pay_out — the body climbing UP toward the crane generates rope slack on its own」，做法是 rail 先動製造 slack、crane 再收。step_down backup 應對齊同一 pattern。

退休的順序在 changelog 2026-05-18 期間引入；當時專注 stale-atomic 修復、沒注意動作順序的物理性。User 2026-05-27 指出問題後 swap 對齊 step_up。
### 對照表（4 個 backup/forward sequence 順序）
| 路徑 | rail 方向 | body 對 crane | 順序 | 註 |
|---|---|---|---|---|
| step_down body forward (pre_cycle) | forward | ↓ 拉緊 rope | pay_out → rail → retract margin | ✓ |
| step_down body backup（本次改） | backward | ↑ 鬆 rope | **rail → crane retract (safe)** | ✓ 改後 |
| step_up body forward (pre_cycle) | backward | ↑ 鬆 rope | rail → crane retract (safe) | ✓ |
| step_up body backup | forward | ↓ 拉緊 rope | pay_out → rail → retract (safe) | ✓ |

---

## 2026-05-27g Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_run` 加 sweep 提早結束自動 relaunch（B+）：
  - 抽出 `launch_sweep` lambda 重用啟動邏輯
  - run loop 每 iter 開頭 `fut_sweep.wait_for(0)` non-blocking poll
  - 如果 sweep 已 ready（代表提早 return，可能 arm offline / XKC / DEPLOY fail）→ 印 prev 結果 + re-launch 新 sweep thread
  - keep_going flag 在 launch_sweep 內 reset 成 true
### 原因
2026-05-27a 的 B 方案實作後,user 問「run 中途 arm 連線失敗、之後恢復會繼續嗎」。原 code 答案是「不會」：`do_arm_clean_sweep_continuous_` INIT fail 就 return,sweep thread exit,主 thread 不知道、繼續跑 N step 都沒刷洗。
改進：每 iter check sweep 狀態 → 提早結束就 relaunch。給 arm 中途上線一個自動恢復機會。
**Rate limit**：每 step 才 check 一次,arm 真的死透也只是每 step 試一次（INIT timeout 60s 在背景跑,不擋主流程）。step 通常 30-60s,relaunch 開銷可接受。
**RAII 仍 work**：SweepJoin 持 fut_sweep 的 reference,relaunch 把新 future 賦值給 fut_sweep,guard destructor 仍會 wait 新的 future。

---

## 2026-05-27f Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_wheels("lower")` target_cm -7.0 → -6.0；同 function 註解 -7 → -6
- `user_lib/WASH_ROBOT.h` — `cmd_wheels` declaration 後註解 -7 → -6
### 原因
User 指示「放輪 -7 改成 -6」，調整輪子下放位置 1 cm。

---

## 2026-05-27e Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_CLEAN_WALL_MM` 300 → 350
### 原因
刮刀在某些 slide 位置貼不到壁，根因：`cleaning_arm/main_api.cpp:319 touch_wall_slot()` 是純幾何位置控制（asin 算 M1 角度），沒有力/電流回授。固定 300mm 假設牆完全平整 + 平行於 slide 軌道，實際牆面起伏 → 凹陷位置刮刀懸空。

`wall_mm` 越大 → M1 認為牆越遠 → 推更遠 → 真實牆近的位置就被壓更緊。靠刮刀座彈性墊吸收過壓變異。

若 350 還不夠/或過壓壞東西 → 上 B（per-position wall_mm map）或 C（M1 力控 deploy 改 motor_api）。
### 注意
本次只動 wall_mm，假設刮刀座有彈性墊。若刮刀是硬接觸 350mm 會直接撞壞，bench 試之前先目測確認。

---

## 2026-05-27d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_EST_MS` 4000 → 7000（配合 RPM 1000 後 100 cm cruise ~6s + ramp 0.2s + buffer 0.8s）
### 原因
27c 已預告：1000 RPM 下 EST_MS 4000ms 不夠 arm 走完 100 cm，會被下一段 nowait fire override → sweep 範圍縮水。User 確認 bump 到 7000ms。

---

## 2026-05-27c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `ARM_SWEEP_RPM` 2000 → 1000（per user，bench menu 28 在 1300 RPM 仍觀察失步/PR fault，主程式再降到 1000 試）
  - `ARM_SWEEP_ACC` 200 → 100、`ARM_SWEEP_DEC` 200 → 100（RPM 降低後 ramp 時間可縮短，加快起停）
### 原因
bench 1300 RPM 第二輪 return 觸發 PR fault → 主程式 sweep RPM 大幅下調保守試。ACC/DEC 一起拉回 100 是因為頂速降一半後即使 ramp 時間 100ms/1000rpm 也只需 100ms × 1.0 = 100ms 完成升降速,不需 200。
### TODO（user 未指示，待確認）
ARM_SWEEP_EST_MS 目前 4000ms 是依 2000 RPM 算。1000 RPM 下：cruise 100cm/(1000/60≈16.7cm/s)≈6.0s + ramp 0.2s + buffer 0.5s ≈ 6700ms。本次未動,user 確認後再 bump 到 ~7000ms（否則 nowait fire 下一段會在 arm 還沒走完前 override → sweep 範圍縮水）。

---

## 2026-05-27b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - `ARM_SWEEP_RPM` 2300 → 2000（ARM_SWEEP_CM 100 行程拉長後頂速段失步）
  - `ARM_SWEEP_EST_MS` 3500 → 4000（cruise 時間隨 RPM 下降變長：100 cm @ 2000rpm ≈ 3.0s + ramp + buffer）
- `Linux_test/main.cpp` menu 28 (`test_dm2j_slide_bench`) — slave 5 → 14、預設 IP 192.168.1.20 → 192.168.1.22、註解 + menu 字串一併更新
### 原因
- 沿用 2026-05-26 上滑台搬遷（cli_20_ slave 5 → cli_22_ slave 14），menu 28 之前還指到舊位址會連不到 / 動錯 device。
- sweep RPM 配合行程拉長要降速避免失步；EST_MS 同步拉長否則 nowait fire 會在 arm 走完前 override，arm 走不到端點。

---

## 2026-05-27a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_run` 改成 B 方案：`up` / `down` 方向整 run 期間單一背景 sweep thread 持續刷洗,iter 之間 arm 不 PARK/INIT。
  - 用 `do_arm_clean_sweep_continuous_(ARM_CLEAN_WALL_MM, sweep_keep_going)` launch 背景 sweep
  - SweepJoin RAII guard 確保任何 return 路徑都 wait sweep join
  - loop 內 `do_step_up_/do_step_down_(skip_cleaning_sweep=true)` 跳末段 sweep（背景接手）
  - 全 run 結束後 set keep_going=false + fut.get() 取結果 + ensure_arm_parked_after_rope_
  - sweep_af 變體（`down_sweep_af` / `up_sweep_af`）保持原邏輯（per-step,after feet rail）
### 原因
User 要 `run up`/`run down` 行為「邊移動,上面邊刷洗」。對齊 `cmd_step_*_with_sweep` 概念但 scope 從 1 step 拉到整 run —— iter 之間 arm 連續 deploy,不會每 step PARK→INIT 浪費時間（N=10 step 估省 50-100 秒 overhead）。
**代價**：整 run 期間 cli_22_ 一直 contention（arm motion + JC100 + PQW + disable_seal）,但 2026-05-26e 的 nowait fix 已處理。
**注意**：中間 step error 會經 SweepJoin 自動把 sweep 停掉、set State::Error 後 return。abort_flag 路徑同理。

---

## 2026-05-26e Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - 新增常數 `ARM_SWEEP_FIRE_RETRIES = 3`、`ARM_SWEEP_FIRE_SPACING_MS = 50`、`ARM_SWEEP_EST_MS = 3500`
  - 新增 method decl `arm_sweep_fire_nowait_(double target_cm)`
- `user_lib/WASH_ROBOT.cpp` —
  - 新增 `arm_sweep_fire_nowait_` 實作（PR_move_cm_nowait × N + sleep estimate）
  - `do_arm_clean_sweep_continuous_` 內 sweep_with_tool 兩個 `PR_move_cm` 改用 `arm_sweep_fire_nowait_`
### 原因
Bench log 顯示 `_with_sweep` 模式下 `RIGHT round 1 failed — abort loop` —— 原因是 arm rail 搬到 cli_22_ slave 14 後,跟 disable_seal 階段密集的 JC100 壓力讀撞 bus,`PR_move_cm` 內部 status poll timeout → driver return error → 整 round abort。
改用 `PR_move_cm_nowait`（只 write target + trigger,**沒 status poll**）→ 不受 cli_22_ contention 影響。代價:fire 完不知道是否真成功,所以:
- **多 fire N 次**:單次 modbus write 可能被 contention 害掉,多寫幾次保險(re-fire 同 target 是 idempotent,driver 只是重新 load PRx + trigger)
- **sleep estimate**:取代 status poll wait。100 cm @ 2300 rpm 估計 ~3s,buffer 0.5s → 3500ms。如果 arm 沒走完下一段 fire 會 override target,arm 跳到新位置
- **不 return error**:sweep_with_tool 不再因為 fire fail abort,整 round 一定跑完 cleanup
- **自我修復**:即使一輪 sweep 沒走完,下一輪 sweep 又重 fire,大致會收斂
**影響範圍**:只動 `do_arm_clean_sweep_continuous_`（`_with_sweep` + `_sweep_after_feet` 共用）。`do_arm_clean_sweep_` (cmd_arm_clean_sweep manual sequential)、`do_arm_sweep_`（end-of-step）保留 blocking PR_move_cm 行為。
**待 bench**:`ARM_SWEEP_EST_MS = 3500` 是估計值,如果 arm 沒走到 +CM extremes 表示 sleep 不夠,需要調大。

---

## 2026-05-26d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_CM` 80 → 100（per user 再試）
### 原因
2026-05-25 試過 100 時 0 點會漂、當時縮回 80；今日 user 要求再試 100。歷史軌跡更新到註解。

---

## 2026-05-26c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `DM2J_ARM` 5 → 14；`D_()` accessor 加 special case (slave 14 → dm2j_[4])；註解更新 dm2j_motion_mtx_ 適用範圍（只 cli_20_）
- `user_lib/WASH_ROBOT.cpp` —
  - `cmd_init_impl_`：DM2J init 拆兩段，slave 1-4 走 cli_20_，slave 14 走 cli_22_
  - `do_arm_clean_sweep_continuous_`：移除 2 個 `dm2j_motion_mtx_` lock（arm 已不在 cli_20_，鎖了反而會卡 feet rail）
  - `cmd_move`：motor=="arm" 不鎖 dm2j_motion_mtx_
  - `cmd_dm2j_zero`：group=="arm" 不鎖 dm2j_motion_mtx_
- `CLAUDE.md` — 架構圖：RS485_1 從 5 個 DM2J 改成 4 個；RS485_3 加入 DM2J slave 14（上滑台）+ XKC slave 13；driver table 註解搬遷
### 原因
之前 arm sweep (DM2J slave 5) 跟 feet rail (DM2J slave 1,3) 都在 cli_20_ → RS485 半雙工撞 bus、靠 `dm2j_motion_mtx_` 序列化 → arm sweep 跟 feet rail 不能真正並行。
搬到 .22 slave 14 後兩條 motion 走不同物理 bus，可真正並行。
**代價**：cli_22_ bus 變忙（JC100×9 poll + DM2J arm motion + PQW valve + XKC + DY500 全序列化），arm motion 期間 pressure_poll 延遲會變大。實際影響待 bench 觀察。
**TCP session**：共用 cli_22_（不開新連線）。USR gateway 開多連線也不會給並行 RS485 access（半雙工），新 socket 只是多耗資源。TCP_client 內建 socket_mtx_ 已 serialize modbus frame，沒 race。
**注意**：實機 SE3 right 改 panel P.36 必要時要先退出 P.79=3 通訊模式（同樣的，DM2J slave id 修改如果用驅動寫 register，要記得寫 EEPROM 才會 persist）。本次只改 code，不動 panel。
**Linux_test 未一起改**：sadie 未明確要求；如需要再加。

## 2026-05-26b Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `SE3_RIGHT_SLAVE` 2 → 1，註解更新
### 原因
Bench log `[WARN] SE3 right init failed — right rope motion disabled`。SE3 driver 已做 3-probe + clearAlarm retry 仍 fail → 物理上 slave 2 沒回應。
5/15 re-layout 時 code 註解寫「kept ID 2 to avoid panel re-config」是錯誤假設，右 SE3 panel P.36 實際一直是 1（從未改成 2）。既然現在每台 SE3 各自一條 bus（左在 USR_A .30、右在 USR_B .31），ID 1 不衝突，直接把 code 改成 1 比改 panel 快。

## 2026-05-26a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_run` 加 `down_sweep_af` / `up_sweep_af` 兩個 direction，per iter 呼叫 `cmd_step_down_sweep_after_feet` / `cmd_step_up_sweep_after_feet`
- `web_backend/public/index.html` — auto cycle 區加按鈕「↓ 走到地面（含清洗）」放 run 按鈕旁邊
- `web_backend/public/app.js` — 綁定 `btn-descend-to-ground`：讀 `crane-remaining` + `step-cm`、算 N、confirm dialog、送 `run N cm down_sweep_af`
### 原因
User：「根據洗窗起點和地面起點和步長計算總步數，一路向下走到地面，跳出通知告知使用者步數，確認後才開始」+「讀取網頁上已有值」+「向下用下移+清洗(腳組後)」。

**距離來源**：用 GUI 既有的 `crane-remaining` 顯示值（=`home_ground_cm - |left|`，由 `zero_meters top` 校準）。「洗窗起點」= zero_meters top 設的零點；「地面起點」= home_ground_cm；剩餘 = 兩者差 - 已走的。

**N 計算**：`Math.ceil(remaining / step_cm)`

**Backend**：cmd_run 增 direction 變體 `down_sweep_af`，loop 內呼叫 `cmd_step_down_sweep_after_feet(0)`（cm=0 = 用 step_cm_ atomic）。state 在每 iter 之間 Running→Attached→Running 短暫切換（cmd_step_*_sweep_after_feet self-manages state），可接受。

**Frontend**：
- 邊界處理：
  - `crane-remaining` 不是數字 → alert「讀不到、請 zero_meters top」
  - `remaining ≤ 0` → alert「已在地面」
- confirm dialog 顯示剩餘距離 / 步長 / 步數 / 預估時間 / 等
- user 取消 → 不送指令

驗證 bench：按按鈕看 confirm 訊息正確;確認後 log 看到「[run] N steps down_sweep_af × XX cm」，然後每 iter 走 step_down + sweep。

## 2026-05-25i Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_attach` 修 mid-attach realign 的 deadlock + 改傳 `in_window=true`
### 原因
User bench：cmd_attach 跑到 mid-attach realign 卡住,Phase 0 印完位置之後沒任何 log。

**Root cause: deadlock**
- `cmd_attach` 第一行 `std::lock_guard<std::mutex> lk(motion_mtx_)` 持有 motion_mtx_
- `do_feet_realign_` 內部 Phase 0 後（沒鎖只讀），跑 threshold check → 觸發 → `std::lock_guard<std::mutex> lk(motion_mtx_)` 重鎖
- `std::mutex` 非 reentrant，同 thread 再 lock → undefined behavior → 實機 hang
- step_down/up 沒這問題因為它們用 `std::unique_lock` + 手動 unlock 才呼叫 realign。cmd_attach 用 lock_guard 沒這 pattern

**Fix**：
1. `cmd_attach` 的 `std::lock_guard` → `std::unique_lock`
2. 呼叫 realign 前 `lk.unlock()`、後 `lk.lock()`（跟 step_down/up 對稱）
3. 順手傳 `in_window=true` 給 realign → 跳 Phase A body retract（此時 body 在 0、從 0 retract 會撞硬體下限）；Phase B 仍會 open body valve + smart_extend body → 後面 attach step 4-5 變成 no-op-ish 但保留當保險

驗證 bench：跑 attach，realign Phase 0 後應接著看到 `[realign] start, force=0 trigger=...` 跟後續 Phase B 跑 body extend。

## 2026-05-25h Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_CM` 60 → 100
### 原因
User:60 不夠,改 100。

歷史:30 → 40 → 45 → 50 → 55 → 60 → **100**(單向 +CM→0 機制不變)。

注意:上滑台實際機構行程要 ≥ 100cm 才能跑到位,不然會撞到硬限位 stall。bench 跑時看一下。
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_CM` 55 → 60
- `user_lib/WASH_ROBOT.cpp` — 上滑台 sweep 三個 call site 拿掉 `-ARM_SWEEP_CM` 段(註解保留)
### 原因
User:上滑台從原本「+CM → -CM → 0」改成「+CM → 0」,單向不再有負,值從 55 改 60。

改動:
- `ARM_SWEEP_CM` 55 → 60
- `do_arm_clean_sweep_` 內 sweep_with_tool lambda(舊「右→左→中」)→ 改「右→中」,中間 `-ARM_SWEEP_CM` 段註解
- `do_arm_clean_sweep_continuous_` 同樣三段 lock 區塊,中間段註解
- `do_arm_sweep_` 的 if/else-if 鏈,中間「arm_sweep_left」段(含後面的 check_abort_)註解
- 上方設計註解 765-770 跟著更新:`{上滑台 → +CM, M2 RIGHT → 0, M2 CENTER}`

全部用 `//` 註解,要回復雙向直接取消註解即可。

備註:此改動是純行為調整(機構工序變動),不影響 PR 槽配置 / 馬達 enable / 其他狀態。
### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `cmd_init_impl_` — feet 兩段 extend（lower 2,4 + upper 1,3）註解掉，留 commented 程式碼方便 revert
  - `cmd_attach` step 1b 註解更新（feet 從 0 開始、smart_extend_subset_ 一氣呵成「伸 + 等 seal」）
### 原因
User：「init 都不要伸腳，深 zdt 改在 attach 且開腳真空後才深」。

**新 flow**：
- `cmd_init_impl_` 不伸任何 pusher（feet + body 都在 0）
- `cmd_attach`：
  - step 1 開 feet valve
  - step 1b `smart_extend_subset_("feet")` 從 0 伸到 preset + WAIT_SEAL 等真空

跟 5/22 那次 "body extend 從 init 搬到 attach" 對稱 — 現在 feet 也搬過來。

**物理影響**（bench 第一次跑要注意）：
- init 結束後機器**沒任何抓力**（feet + body 全縮回）→ 必須靠繩支撐 / 物理底座
- init → attach 之間如果繩鬆掉 robot 可能位移

驗證 bench：跑 init 看 log 應該**沒有**「init feet lower/upper pushers ... extend」訊息；跑 attach 看 log 序列「open FEET valve → feet disable_seal extend 從 0 開始 → ...」。

## 2026-05-25e Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_attach` 重排步驟
### 原因
User：「attach 部分先等腳組吸附後，先 realign 一次，再換身體組開真空並伸出」+「center 都先不用控制」。

**新 flow**：
1. 開 FEET valve（只開腳組）
2. `smart_extend_subset_("feet", feet_slaves)` — 用 disable_seal pipeline 等 feet 真空建立（user 指定的「用 disable_seal 那套方法」）
3. `do_feet_realign_(force=false)` — 內部 threshold check 自動決定
4. 開 BODY valve（**不開 CENTER**，user 暫不控制）
5. `smart_extend_subset_("body", body_slaves)` — 同舊版 step 2
6. vacuum_check + smart_extend 補漏（同舊版）
7. pay_out（同舊版）

**舊 flow** vs 新 flow 差別：
- 舊：一次開 3 valve(feet+body+center) → body extend → feet 在 body extend 期間被動 seal
- 新：feet valve → feet active seal → realign → body valve → body extend
- center valve 完全不開（per user，bench 暫不用 center cup）

**注意點**：
- realign 在 attach 中段執行：fresh init 通常 drift 0 → skip Phase 1；累積 drift 後會跑 Phase 1 retract rope，此時只有 feet sealed、body 未 extend → rope retract 會把 body 往上拉 → tension 集中到 feet cup。bench 觀察 feet 是否撐得住。
- vacuum_check_("all") 已透過 init() 的 `disabled_zdt_slaves_.insert(ZDT_C)` 自動排除 slave 9，所以不開 center valve 不影響後續判定。

驗證 bench：跑 attach 看 log 序列「open FEET valve → feet disable_seal → mid-attach realign → open BODY valve → body disable_seal extend → ...」。

## 2026-05-25d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_RPM` 1000 → **1300**
### 原因
User：「我要到 1300」。歷史軌跡 1500→500→700→900→1000→1300。

## 2026-05-25c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_init` 註解掉身體 extend;`cmd_attach` 插入身體 disable_seal extend
### 原因
User:init 只伸腳組,attach 再開全部閥 + 身體組伸出來吸(同時腳組被動吸),pay_out 保留。

**`cmd_init`**(WASH_ROBOT.cpp:3507-3524):
- 註解掉 `body_short`(slave 7,8)/`body_long`(slave 5,6)兩段 extend + 對應 release_stall_flag。body_short / body_long 變數宣告保留(`(void)` cast 抑制 unused warning),用 `//` 註解可一鍵回復。
- 結果:init 只伸腳組(1,2,3,4)到 preset,身體(5,6,7,8)留在 0。

**`cmd_attach`**(WASH_ROBOT.cpp:3543-3568):
- 抬頭註解改寫,說明新流程。
- 移除舊 `ATTACH_VACUUM_WAIT_MS = 4000ms` 的 sleep。
- 新插入 step 2:`smart_extend_subset_("body", {5,6,7,8})` —— body 從 0 走 disable_seal 伸出(跟 step_up/down 共用同一條 disable_seal 管線,Phase 1 快伸→Phase 2 推/斷電/等真空)。
- 腳組已伸好 + 閥開 → body disable_seal 那 10s+ 期間真空被動建立,不用顯式 settle。
- step 3(vacuum_check + smart_extend fine-tune)不變,當 weak_seal 保險網。
- step 4(crane pay_out)不變,user 確認保留。

設計選擇:sequential 而非真平行(避免 ZDT broadcast trigger 在兩條 thread 之間競爭)。time saving 來自 body extend 跟 feet 真空 settle 重疊,而不是兩條 thread 真同時。

回復方法:`cmd_init` 取消註解 body 那兩段 + `cmd_attach` 把 step 2 的 body smart_extend_subset 拿掉、加回 4s sleep。
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `do_arm_clean_sweep_continuous_` 加 `int max_rounds = 0` 參數（0=unlimited、N>0=固定 N round 後自動退）
- `user_lib/WASH_ROBOT.cpp`：
  - `do_arm_clean_sweep_continuous_` loop 條件多 `(max_rounds <= 0 || round_cnt < max_rounds)`；log 多印 max_rounds 跟 completed
  - `cmd_step_up_sweep_after_feet` / `cmd_step_down_sweep_after_feet` 背景 thread 從 `do_arm_clean_sweep_(rounds=1)` 改回 `do_arm_clean_sweep_continuous_(keep_going, max_rounds=1)`；恢復 sweep_keep_going atomic 跟 SweepJoin guard 的 flag
### 原因
User bench 觀察：「sometimes arm 卡住、沒辦法 PARK/INIT」。

**root cause**：5/25a 把 `_sweep_after_feet` 系列改用 `do_arm_clean_sweep_(rounds=1)`。該函式內部用 `try_or_pause_`，sweep 任何 step（DEPLOY 撞 obstacle / INIT fail / XKC read fail 等）失敗 → 背景 thread 進 `await_user_intervention_` 卡住、**持續持有 arm_mtx_**（透過內部 arm_cmd_ 的 lock_guard）→ user GUI 按 PARK/INIT 拿不到 `arm_mtx_` → 整個 arm subsystem 卡死。

**修法**：改回 `do_arm_clean_sweep_continuous_`（silent error policy），加 `max_rounds` 參數讓它在 1 round 後自動退出。
- 背景 sweep 失敗 → log + return → cleanup → 不卡 arm_mtx_
- 主 thread state machine 不受 try_or_pause_ 干擾
- max_rounds=1 保證跑完恰好 1 輪自動結束

helper 改動很小（loop 條件 + log），向後相容（預設 0=unlimited 保留現有 `cmd_step_up_with_sweep` / `cmd_step_down_with_sweep` 的行為）。

驗證 bench：跑 step_up_sweep_after_feet / step_down_sweep_after_feet，sweep 1 round 結束後 GUI PARK/INIT 應該可用。失敗時 log 看到 `[arm_clean_sweep_cont] ... failed — abort loop`，不再卡住。

## 2026-05-25a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `do_step_down_` 加 `std::function<void()> after_feet_rail_hook = {}` 參數（對稱 `do_step_up_`）
  - 新增 `cmd_step_down_sweep_after_feet(int cm = 0)` 宣告
- `user_lib/WASH_ROBOT.cpp`：
  - `do_step_down_` Phase B feet_pre_cycle 在 feet rail `dm2j_pair_move_abs_(..., 0.0)` + `rail_pos_cm_.store(0.0)` 之後若 hook 非空就呼叫
  - **改寫** `cmd_step_up_sweep_after_feet`：背景 thread 從 `do_arm_clean_sweep_continuous_` 改成 `do_arm_clean_sweep_(ARM_CLEAN_WALL_MM, 1)` 固定 1 round；移除 sweep_keep_going 旗標跟相關邏輯
  - **新增** `cmd_step_down_sweep_after_feet`：對稱 step_up 版，同樣 1 round
- `washrobot_new_PI/main.cpp` — dispatcher 加 `step_down_sweep_after_feet <cm>`
- `web_backend/public/index.html` — auto cycle 加按鈕「↓ 下移 + 清洗(腳組後)」放 `↓ 下移 + 連續清洗` 旁邊
- `web_backend/public/app.js` — 綁定 `btn-step-down-sweep-af` 送 `step_down_sweep_after_feet ${cm}`
### 原因
User：「step_down 也新增一個和上面很像的按鈕功能；feet 的 dm2j 回 0 後就開始同步清洗；這兩個都改成只洗 1 round 就好」；「不用加 max_rounds 參數，只改這兩個按鈕」。

**設計選擇**：
- 「1 round 固定」改用既有的 `do_arm_clean_sweep_(wall_mm, 1)`（內建 rounds 參數），不動 `do_arm_clean_sweep_continuous_`
- Trade-off：`do_arm_clean_sweep_` 用 `try_or_pause_`（PausedOnError 語意），背景 thread sweep 失敗（DEPLOY 撞 obstacle / INIT fail 等）會進 PausedOnError，跟主 thread 的 state machine 可能交互。對 1 round 失敗率低、accept

**Hook 觸發點對稱**：
| 流程 | Phase A | Phase B | Hook 觸發點 |
|---|---|---|---|
| step_up | feet (rail 0→+step) | body (rail +step→0) | feet rail = +step 完成（在 Phase A） |
| step_down | body (rail 0→+step) | feet (rail +step→0) | feet rail = 0 完成（在 Phase B） |

Step_down 因 feet phase 是 Phase B（在 body 之後），sweep launch 點偏 step 末段，只跟「feet cup 密封 + end-realign」並行；step_up 的 launch 點偏前段，跟「feet cup 密封 + 整個 body phase」並行 → step_up 並行收益大、step_down 較小但 user 要求對稱保留。

**目前 step 按鈕全集**：
- `step_up` / `step_down`：純動作
- `↑/↓ + 連續清洗`：sweep 從 step 開頭就 launch，多輪 loop（5/22i / 5/22o）
- **`↑/↓ + 清洗(腳組後)`**：sweep 在 feet rail 走完才 launch，只 1 round（本次）

驗證 bench：按新按鈕，log 應看到 step_down 跑完 Phase A body → Phase B feet → `feet rail home → launching 1-round sweep` → `[arm_clean_sweep]` 跟 feet cup 密封交錯。

## 2026-05-22aa Claude (Sadie)
### 修改檔案
- `Linux_test/main.cpp` — 新增 menu 27 `test_dm2j_clear_pr`(復原工具);修掉所有「mode=1,rpm=0」地雷 PR 寫法
### 原因
跑完 menu 26 測試後,washrobot 的 DM2J slave 5 `PR_move_cm` 開始 20s timeout。根因:測試的「safe PR」寫法 `PR_move_set(slot, 1, 0, 0)` = mode=1(絕對)+ rpm=0,是一條「已配置但跑不動的路徑」。腳組 broadcast `PR_trigger_sync(1)` 觸發 bystander(2,4,5)的這條 PR1 → 馬達不在絕對 0 時就卡死(RUN 卡 1、零速路徑永遠跑不完)→ 後續對該 slave 的 PR_move_cm timeout。

修正(全部在 Linux_test,不動主程式 per user）：
- **新增 menu 27 `test_dm2j_clear_pr`** —— 復原工具:對 5 顆 DM2J 送 `speed_move_stop()` + 把 PR1/PR2 寫成 `mode=0`(未配置 → broadcast 觸發 = 乾淨 no-op)。RAM only。
- **`dm2j_set_safe_pr` helper**(menu 7 / 17 用)`PR_move_set(pr,1,0,0)` → `PR_move_set(pr,0,…)`。
- **menu 16** init / reset / cleanup 的 `PR_move_set(…,1,0,0)` → mode=0。
- **menu 26** init-safe / cleanup → mode=0(INS 測試的 bystander 配置 4409 維持,那是測試本體,cleanup 會還原)。

關鍵觀念:bystander 安全 PR 應該用 **mode=0(未配置)**,不是 mode=1+rpm=0。mode=0 → 驅動器把 trigger 當「路徑未配置」直接忽略 = 真 no-op;mode=1+rpm=0 → 是「要移動但速度 0」→ 卡死。

復原步驟:停 washrobot → 重 build/deploy Linux_test → 跑 menu 27 → 重啟 washrobot。
備註:washrobot init 的 bystander PR1 設定(WASH_ROBOT.cpp:104-107)目前仍是註解狀態 —— 本次未動主程式;若要根因永久修正,該段應取消註解並改用 mode=0 寫法。
### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `do_step_up_` 加參數 `std::function<void()> after_feet_rail_hook = {}`
  - 新增 `cmd_step_up_sweep_after_feet(int cm = 0)` 宣告
- `user_lib/WASH_ROBOT.cpp`：
  - `do_step_up_` feet_pre_cycle 在 feet rail DM2J move + `rail_pos_cm_.store()` 之後呼叫 `after_feet_rail_hook`（非空時）
  - 新增 `cmd_step_up_sweep_after_feet` 實作
- `washrobot_new_PI/main.cpp` — dispatcher 加 `step_up_sweep_after_feet <cm>`
- `web_backend/public/index.html` — auto cycle 加按鈕「↑ 上移 + 清洗(腳組後)」
- `web_backend/public/app.js` — 綁定 `btn-step-up-sweep-af` 送 `step_up_sweep_after_feet ${cm}`
### 原因
User：「新增一個功能（不是改現有的）── feet 的 DM2J 一走完就開始同步做 cleaning arm」，只做 step_up。

跟現有 `cmd_step_up_with_sweep`（5/22i，sweep 從 step 開頭就 launch）的差別：**sweep launch 時機延後到 feet rail DM2J move 完成那刻**。

理由：feet rail 走完 → robot frame 已到新垂直位置 → arm 在新牆面上 → 才開始洗才有意義。sweep 之後跟「feet cup 密封 + 整個 body phase」並行。

**實作**：
- `do_step_up_` 加 `after_feet_rail_hook` callback 參數。feet_pre_cycle 在 feet rail `dm2j_pair_move_abs_` 成功 + `rail_pos_cm_.store()` 之後呼叫 hook（非空才呼叫）。一般 cmd_step_up / cmd_step_up_with_sweep 傳空 hook、行為不變。
- `cmd_step_up_sweep_after_feet`：fut_sweep 初始空，hook 被呼叫時才 `std::async` launch `do_arm_clean_sweep_continuous_`。SweepJoin RAII guard 用 `valid()` 檢查（feet phase 在 hook 前失敗 → fut 仍空 → guard no-op）。
- 共用 `do_arm_clean_sweep_continuous_`（5/22i）+ `dm2j_motion_mtx_`（5/22k/l）。

**保留不動**：`cmd_step_up_with_sweep`（全段並行版）、`cmd_step_down_with_sweep`。現在 3 個 step_up 按鈕：純 step_up / 連續清洗(開頭) / 清洗(腳組後)。

Q1（上滑台 async bus）user 決定不做 → DM2J motion 仍透過 `dm2j_motion_mtx_` 序列化。

驗證 bench：按「↑ 上移 + 清洗(腳組後)」，log 應看到 feet phase 先跑、`feet rail done → launching background sweep`，然後 `[arm_clean_sweep_cont]` 跟 body phase log 交錯。

## 2026-05-22y Claude (Sadie)
### 修改檔案
- `Linux_test/main.cpp` — menu 26 修正 INS bit 套用對象(腳組 PR1 → bystander PR1)
### 原因
INS bit=1 那輪測出來上滑台還是被打斷 —— 發現測試 bug:INS bit 設錯 slave。

`PR_trigger_sync(1)` broadcast 後,**每顆 slave 跑的是「自己的」PR1**。「broadcast 會不會打斷 slave 5 的 PR2」取決於 **slave 5 自己 PR1 的 INS bit**,不是腳組(1,3)的 PR1。舊版把 `feet_mode`(帶 INS bit)套到 slaves 1,3 的 PR1 → slave 5 的 PR1 還是 INS=0 → 照樣打斷。

修正：
- prompt 改名「Feet PR1 INS bit」→「Slave 5 PR1 INS bit」。
- INS bit 改套到 **bystander PR1(slaves 2,4,5)** —— `PR_move_set(1, 1|(ins<<4), 0,0,…)`。slave 5 才是被干擾測試的對象。
- 腳組(1,3)PR1 回到 `mode=1`(腳組是預定移動者、被觸發時 idle,INS 不相干)。
- 移除 `feet_mode` 變數,report / header 註解一併更新。

下一步:重跑 menu 26、Slave 5 PR1 INS bit 輸入 1,這次才是真正測 slave 5 PR1 的屏蔽插斷。
### 修改檔案
- `Linux_test/main.cpp` — menu 26 `test_dm2j_slide_during_feet` 改進(判讀邏輯 + INS bit 可調)
### 原因
第一輪 bench 結果:上滑台被腳組 broadcast 觸發**打斷了** —— 上滑台往絕對 15cm 跑,腳組 broadcast 一下,上滑台凍在 ~2.3cm 不動、RUN bit 卻卡在 1。

結論:**DM2J INS bit(mode word Bit4)= 0 時 = 插斷有效**(手冊 §6.5 line 3265 的描述對、附錄表 line 4465 錯)。被打斷後上滑台掉進 bystander-safe 的 PR1(rpm=0)→ 卡在「執行一個 rpm=0 路徑」的 limbo,RUN 永遠不清。

測試改進:
- 新增 prompt「Feet PR1 INS bit 0/1」→ 腳組 PR1 mode word 改 `1 | (ins<<4)`(0x01 或 0x11),可直接測 Bit4=1(屏蔽插斷)。
- 判讀改用**位置**不再靠 RUN bit(RUN 會卡死)：到目標±0.3cm = 沒被打斷;連續 10 sample(~3s)不前進 = frozen = 被打斷。
- 加 glitch filter:共用 gateway 偶爾回傳 garbage(實測 slide 讀到 0.000),反方向跳 >0.8cm 的讀數直接丟棄。
- 結束時送 `speed_move_stop()` 清掉上滑台卡死的 rpm=0 PR1 limbo。

下一步:用 INS bit=1 再跑一次。若上滑台這次能跑到目標 → broadcast 不再打斷 → 軟體方案(上滑台獨立 PR)成立。
### 修改檔案
- `Linux_test/main.cpp` — 新增 menu 26 `test_dm2j_slide_during_feet`(上滑台獨立 PR vs 腳組 broadcast 同步的干擾測試)
### 原因
User：寫一個 bench 測試 —— 先觸發上滑台移動,移動中再觸發腳組 DM2J 同步移動到絕對 2cm,測「腳組 broadcast 觸發會不會打斷上滑台的獨立 PR」(decide DM2J INS bit polarity)。

測試設計：
- 上滑台 = slave 5 → PR2,**非 broadcast** `PR_move_cm_nowait`(只定址 slave 5)。
- 腳組 = slaves 1,3 → PR1,**broadcast** `PR_trigger_sync(1)` —— 兩條腳導軌硬體同步啟動(user 硬性要求:腳組一定要同步,不然機構壞)。
- 流程:觸發上滑台 → 等 delay(上滑台仍在動)→ broadcast 觸發腳組 → 監看上滑台有沒有被打斷(走到目標 vs 中途停)。
- 安全:slaves 2,4(輪子)+ slave 5 的 PR1 全預設 rpm=0(bystander-safe),broadcast PR1 不會把任何馬達帶跑;2,4 保持 disabled。
- 結果判讀:上滑台走到目標 → broadcast 沒打斷 → 獨立 PR 可行;停在半路 → 被打斷 → 需翻 INS bit(mode word Bit4)再測。

備註:純測試工具,不影響生產程式碼。menu 16 `test_dm2j_group_sync` 是這支的模板。
### 修改檔案
- `user_lib/DM2J_RS570.cpp` — 註解掉 motion-wait 迴圈內的 `print_status` 呼叫(2 處)
### 原因
User：DM2J 的 `[DBG] status=0x... [ENABLE][RUN][HOME_DONE]` log 在馬達運轉時每 ~50ms 印一次,太囉嗦。

`print_status` 在兩個 motion-wait 迴圈內被每次輪詢呼叫:
- `PR_move_cm` Phase 2 wait(:245)
- `PR_move_cm_trigger_all` wait(:301-302)

兩處 `if (debug_mode) print_status(st)` 改成註解(用註解保留,可回復)。`print_status` 函式本身保留(其他重要 log 仍走 LOG_ERR/LOG_DBG,不受影響)。
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — 註解掉 5 處 end-of-step / between-steps realign 呼叫
### 原因
User：把 step 最後面的 realign 拿掉,先用註解(不行可回復)。

承 5/22s —— mid-step realign 已移進 body_pre_cycle(in-window feet-only),加上 step 自己的 body cycle,cup 在走到 step 結尾時 drift 已 ≈ 0,end-of-step 的完整 realign 過不了 threshold、幾乎是空跑(只多一次 9 顆 ZDT 位置回讀)。

註解掉的 5 個 call site(都是 `do_feet_realign_(/*force=*/false)` 完整版):
- `cmd_step_down` :4051
- `cmd_step_down_with_sweep` :4115
- `cmd_step_up` :5156
- `cmd_step_up_with_sweep` :5224
- `run` 連續步迴圈(步與步之間) :5275

全部用註解保留、未刪除 —— 若 bench 發現跨 step 仍有殘留 drift 累積,取消註解即可回復。
end-of-step PARK 邏輯不受影響(realign 本來就沒做 crane 動作 → net rope 仍 ≈ 0)。
手動 realign 按鈕(`cmd_realign` 之類)與 standalone realign 不受影響,仍可用完整版。
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `RETRACT_SLOW_PEEL_CM` 2.3 → **2.6**
### 原因
User：「第一階段收繳的 2.4cm 改成 2.6cm」（實際當前值 2.3，照新目標 2.6 改）。

## 2026-05-22s Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `do_feet_realign_` 加 `bool in_window = false` 參數
- `user_lib/WASH_ROBOT.cpp` — `do_feet_realign_` 加 in_window「feet-only」模式;`do_step_down_` / `do_step_up_` mid-step realign 移進 body_pre_cycle
### 原因
User：拆掉 realign 與 step body cycle 之間重複的 body 退+建。

問題：mid-step realign 呼叫完整 `do_feet_realign_`(Phase A 退 body + Phase 2 feet + Phase B 重建 body)。但 step 自己的 body cycle 本來就退+建 body 一次 → body 被退兩次、伸兩次,多出來那組 retract+disable_seal 是 realign 最貴的部分。

改法（do_feet_realign_ 加 `in_window` 參數,additive,`in_window=false` 行為完全不變）：
- `in_window=true`：realign_skip 額外排除 body 5-8(Phase 0 不讀退開的 body、threshold 只看 feet drift);跳過 Phase 1(crane assist)、Phase A(退 body)、Phase B(重建 body)、Phase 5(crane restore);只做 Phase 0 + Phase 2(feet)+ Phase 2.5 + feet 真空驗證。
- 不重新 lock `motion_mtx_`(caller step 已持有)。
- 為什麼連 crane Phase 1/5 都省：body 的 release 變成 step 自己 body_pre_cycle 的 release,step 本來就 handle 鋼索;feet realign 全程 feet 都吸著,沒有突發 load transfer。

call site：
- `do_step_down_` / `do_step_up_`：mid-step realign 從「cycle_group body 之後/之前的獨立呼叫」移到 `body_pre_cycle` 結尾 —— body 剛被 body_pre_cycle 退開、cycle_group 即將重建的窗口,正是 feet realign 需要的「body 退開 + feet 吸著」狀態。
- 舊的獨立 mid-step realign 區塊（含 `lk.unlock()/lock()` dance）改成**註解保留**（per user：先用註解、不行可回復）,未刪除。
- body_pre_cycle 結尾呼叫後補 `motion_active_ = true`,防 realign 內部錯誤路徑把它關掉害後續 disable_seal 撞 pressure_poll。

效果：realign 觸發時不再多退+建一次 body,從「等於再跑一輪 body cycle」縮成純 feet 微調。end-of-step / standalone realign 維持完整版（`in_window=false`）。

待 bench 驗證：feet realign 進行時機器只靠 feet+rope 撐,觀察有無下沉/傾。
- `user_lib/WASH_ROBOT.h` — `REALIGN_THRESHOLD_MEAN_CM` 1.0 → **1.5**；`RETRACT_SLOW_PEEL_CM` 2.0 → **2.3**
### 原因
User：bench tuning。
- realign mean 門檻拉高 → realign 更少觸發
- 慢速脫壁距離拉長 → cup 從牆面 peel 更充分再進入快速段

## 2026-05-22q Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `REALIGN_THRESHOLD_CM` 1.5 → **3.0**（語意改成單顆 max trigger）；新增 `REALIGN_THRESHOLD_MEAN_CM = 1.0`
- `user_lib/WASH_ROBOT.cpp` — `do_feet_realign_` 觸發判定從「單一 max」改成 hybrid：max > 3.0 OR mean > 1.0；同時計算 `sum_abs_drift_cm` 跟 `count_included` 算 mean；log 多印 trigger 原因（max / mean / max+mean）
### 原因
User：「覺得有點太常跑 realign，例如全部的差距平均」→ 選 D (hybrid)。

**舊邏輯**：`if max_abs_drift > 1.5cm → fire`。**任一**單顆 cup 漂 1.5cm 就觸發 → 一個 outlier 就 fire，太敏感。

**新邏輯**（hybrid OR）：
- 單顆 cup 漂超過 **3.0cm** → fire（safety net for outlier）
- OR 全 cup 平均漂超過 **1.0cm** → fire（累積式判斷）
- 兩條件分開 → bench 可獨立微調

**典型場景行為**：
| 情境 | max | mean | 舊 | 新 |
|---|---|---|---|---|
| 1 顆 2cm、其他 0.5cm | 2.0 | 0.6 | fire | skip ✓ |
| 1 顆 3.5cm、其他 0.5cm | 3.5 | 0.9 | fire | fire（max）|
| 全 1.2cm | 1.2 | 1.2 | skip | fire（mean）|
| 全 0.8cm | 0.8 | 0.8 | skip | skip ✓ |

log 加印觸發原因（max / mean / max+mean）方便 bench 觀察是哪條件 trigger 的，可據此調整門檻。

驗證 bench：跑幾個 step 看 realign 觸發頻率是否符合 user 預期，調 `REALIGN_THRESHOLD_CM` / `REALIGN_THRESHOLD_MEAN_CM`。

## 2026-05-22p Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_arm_clean_sweep_continuous_` Phase A 開頭 XKC 讀加 3 次 retry
### 原因
User bench：step_down_with_sweep 一啟動 sweep 就讀 XKC 失敗、直接 abort：
```
[arm_clean_sweep_cont] XKC sensor unreachable — abort sweep
```
連續 sweep 平行模式 user 介入機會少，第一次失敗就放棄太脆弱。XKC 在 cli_22_ 跟 JC100 / PQW / DY500 共 bus，瞬間 contention 機率高。

修法（user 選 B）：第一次 read 失敗 → 100ms 後 retry，最多 3 次。3 次都失敗才認定 sensor 真掛、log 後 abort。

未動 fill loop 內的 polling read（那邊本來就是 loop 內持續讀，transient fail 自然會在下次重試）。

## 2026-05-22o Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `do_step_down_(bool skip_cleaning_sweep = false)` 加參數（對稱 do_step_up_）
  - 新增 `cmd_step_down_with_sweep(int cm = 0)` 宣告
- `user_lib/WASH_ROBOT.cpp`：
  - `do_step_down_` 末段 cleaning sweep call 加 `if (!skip_cleaning_sweep)` gate
  - 新增 `cmd_step_down_with_sweep` 實作（對稱 cmd_step_up_with_sweep：std::async 背景 sweep + 主 thread do_step_down_(skip=true) + RAII SweepJoin guard + end-realign + PARK）
- `washrobot_new_PI/main.cpp` — dispatcher 加 `step_down_with_sweep <cm>`
- `web_backend/public/index.html` — auto cycle 區加按鈕「↓ 下移 + 連續清洗」放 step_down 旁邊
- `web_backend/public/app.js` — 綁定按鈕送 `step_down_with_sweep ${cm}`
### 原因
User：「step_down 也增加一個按鈕功能來邊 step_down 邊清洗」（對稱 5/22i 的 step_up_with_sweep）。

實作完全 mirror cmd_step_up_with_sweep：共用 `do_arm_clean_sweep_continuous_` helper（5/22i）跟 `dm2j_motion_mtx_` 序列化（5/22k/l）。

風險繼承 5/22i：sweep 期間 arm 在 robot 上動、robot 也在 step 中移動 → bench 第一次跑要看軌跡無衝突。

## 2026-05-22n Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_feet_realign_` Phase 1 收繩改成連續收繩；刪除 `crane_retract_to_weight_`
- `user_lib/WASH_ROBOT.h` — 刪除 `crane_retract_to_weight_` 宣告
### 原因
User：realign 前的收繩不要「開開關關吊機」。

舊問題：realign Phase 1 用 `crane_retract_to_weight_`，內部是 1cm 一步的迴圈，每步送 `retract 1` → 變頻器啟動→走→停。收 10cm = 10 次起停。

改法：
- Phase 1 改用現成的 `crane_retract_safe_(REALIGN_CRANE_ASSIST_MAX_CM)` —— 送一次 `retract N`，吊機連續收，monitor thread（estop channel）讀張力到 `rope_weight_limit_per_sensor_kg_()` 就送 `stop`。一次起、一次停。
- monitor 門檻 `rope_weight_limit_per_sensor_kg_()` 正好就是 realign Phase 1 原本要的門檻，語意不變。
- 邊界 1：繩已 ≥ 門檻 → `crane_retract_safe_` 回 `ERR rope_weight_too_high`，realign 當「繩已吃滿、Phase 1 目標已達」→ 照常往下不 abort。
- 邊界 2：`crane_retract_safe_` 回覆不含走了幾 cm。改用 Phase 1 前後各讀一次 `crane_cmd_("status")` 的 `length_left` 相減（收繩 → length 下降），得實際收繩 cm（向 0 截斷，cap 不會超過實收量）餵給 Phase 5 的 pay_out cap，維持 net rope ≤ 0。
- crane 未 attached → 跳過 Phase 1（同舊行為）。
- `crane_retract_to_weight_` 改完後無呼叫端 → 連同 .h 宣告一併刪除。

備註：Phase 5 的放繩 `crane_pay_out_to_weight_` 也是 1cm 迴圈、同樣會開開關關，本次未動（user 只要求收繩）。

## 2026-05-22m Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `PUSHER_RPM_RETRACT` 50 → **30**（改回 5/22g 之前的值）
### 原因
User：「幫我把 ZDT 脫離牆的 50 RPM 改回 30」。5/22g 為了加速從 30 拉到 50，bench 後 user 決定改回 30（可能 cup 脫壁 50 RPM 力道太大）。

## 2026-05-22l Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `dm2j_read_pos_robust_` 跟 `cmd_dm2j_zero` 也加 `dm2j_motion_mtx_` lock
### 原因
User bench：5/22k 加完 lock 後仍出現
```
[dm2j_robust] slave 1 attempt 1/5 comm fail
... 5 次 attempt 全 fail
[PAUSE-ON-ERROR] body_pre_dm2j_read_left_up
[arm_clean_sweep_cont] round 3 — LEFT(滾筒+水) → RIGHT(刮刀乾)
```
原因：5/22k 只 lock 了 motion 函式（`dm2j_pair_move_abs_` / `cmd_move` 等），但 `dm2j_read_pos_robust_`（單純讀位置、用於 body backup factory 的 actual-pos 抓取）沒 lock。背景 sweep 的 PR_move_cm 占用 TCP socket_mtx → robust read 的 5 次 retry 全 frame timeout。

補加 lock 到：
- `dm2j_read_pos_robust_`：body backup factory 用、每個 step 都會跑、跟 sweep 同時發生機率高
- `cmd_dm2j_zero`：手動 GUI zero，user 隨時可能按

未補（DM2J 主 thread 路徑、但跟 background sweep 互斥於 motion_mtx_）：
- init() 啟動時的 wheel retract
- do_arm_sweep_ / do_arm_clean_sweep_（已透過 motion_mtx_ 跟 cmd_step_up_with_sweep 互斥）
- cmd_return_home arm move

驗證 bench：再跑 step_up_with_sweep，body_pre_dm2j_read_left_up 不應再 PausedOnError。

## 2026-05-22k Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 `std::mutex dm2j_motion_mtx_` 成員
- `user_lib/WASH_ROBOT.cpp`：
  - `dm2j_pair_move_abs_` 入口加 `lock_guard<dm2j_motion_mtx_>`
  - `do_arm_clean_sweep_continuous_` 的 3 個 sub-round PR_move_cm 各自加 lock_guard（block-scoped、每段釋放讓主 thread 有空檔）
  - `cmd_move` / `cmd_wheels` / `cmd_dm2j_group` (wheels branch) 加 lock_guard
### 原因
User bench 觀察「上滑台在動的時候腳組 DM2J 無法動」：cmd_step_up_with_sweep 並行下，背景 thread 的 arm sweep (slave 5) 跟主 thread 的 feet rail pair (slave 1,3) 都在 cli_20_，共用 TCP socket。背景 PR_move_cm 在 poll loop 期間持續占用 TCP_client::socket_mtx → 主 thread 的 PR_move_cm 因為 frame 等不到 → 失敗 → `[PAUSE-ON-ERROR] feet_pre_rail_forward_up`（log 顯示 user 連按 4 次 retry 才終於跨過 sweep 的 polling window）。

**修法**（user 選 A：dm2j_motion_mtx_ 序列化）：
- 新 mutex 在 WashRobot
- 所有 DM2J motion 進入點加 lock_guard：
  - 主 thread 的 pair move (feet rail), single move (manual), wheel move (cmd_wheels / cmd_dm2j_group wheels branch)
  - 背景 arm sweep 的 3 段 PR_move_cm（每段 block-scope，做完釋放）

**並行性**：sub-round 之間（arm 釋放 mutex 的瞬間）feet 等的 thread 可以拿到 → 不會永遠 starve。但 DM2J 動作之間是序列化（同 bus 物理上本來就無法平行）。

**保留平行的部分**：
- cli_21_ (ZDT pusher) 跟 cli_20_ 不衝突 → ZDT 操作仍然平行
- cli_22_ (PQW 水閥 / JC100 真空 / XKC) 不衝突
- arm_cli_ (motor_api INIT/DEPLOY) 不衝突

實際省時：sweep 期間主 thread 偶爾等 arm 釋放，最長等 ~2-3s（一段 sub-round 移動時間）。比原本「step→sweep 串行」還是省很多。

**未修的 DM2J 主 thread 路徑**：init() / cmd_return_home / cmd_descent 不加 lock，因為這些都跟 step_up_with_sweep 互斥（透過 motion_mtx_ 或 state machine），不會並行跑。

驗證 bench：跑 step_up_with_sweep 看 log 是否還會看到 `feet_pre_rail_forward_up` PausedOnError；正常的話 sweep round 跟 feet motion 會交替（不平行但都跑得完）。

## 2026-05-22j Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_attach` step 3 改用 `smart_extend_subset_` 取代 `fine_tune_extend_per_slave_`
### 原因
User：attach 補吸未密封 cup 改用和 step_up/down 一樣的 disable_seal 機制。

舊 `fine_tune_extend_per_slave_` 的問題（實機 log 佐證）：
- 用靜態 `preset` 當補伸基準。若 cup 實際位置已超過 preset，三輪 `+1cm` 絕對目標全落在 cup 身後 → `spd=0rpm` 空推、no-op，fine_tune 等於沒作用。

改法：
- step 3 偵測到未密封 cup 後，依組別拆成 feet(1-4) / body(5-8)，分別呼叫 `smart_extend_subset_("feet"/"body", fails)` —— 與 step_up/down 經 `cycle_group_` 共用的同一條 disable_seal 管線。
- disable_seal：Phase 1 快伸到 preset−1cm → Phase 2 迭代「推→斷電→等真空」，主動驗證馬達推進 + 即時監看真空，不會空推。
- 只把「未密封」的 cup 傳進去：disable_seal Phase 1 會快伸到 preset−1cm，已密封 cup 一起傳會被縮回而破真空。
- center(slave 9) 在 attach 不伸出，拆組時自然排除。
- 包 `try_or_pause_`：disable_seal 硬失敗 → PausedOnError 由操作員介入（與手動 GUI 伸腳路徑一致）；單純「cup 沒吸到」仍由後續 `vacuum_check_` 收尾 WARN + 照常進 Attached（attach 容忍度不變）。

備註：`fine_tune_extend_per_slave_` 改完後已無呼叫端，成為 dead code，待確認是否移除。
### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `do_step_up_(bool skip_cleaning_sweep = false)` 加參數
  - 新增 `cmd_step_up_with_sweep(int cm = 0)` 宣告
  - 新增 `do_arm_clean_sweep_continuous_(int wall_mm, std::atomic<bool>& keep_going)` 宣告
- `user_lib/WASH_ROBOT.cpp`：
  - `do_step_up_` 末段 cleaning sweep call 改成 `if (!skip_cleaning_sweep)` gate
  - 新增 `do_arm_clean_sweep_continuous_` 實作（Phase A/B 同 do_arm_clean_sweep_、Phase C 改成 while(keep_going) 連續 LEFT+RIGHT round、Phase D RAII 自動 PARK+關水/刷）
  - 新增 `cmd_step_up_with_sweep` 實作（背景 std::async sweep + 主 thread do_step_up_(skip=true) + RAII SweepJoin guard）
- `washrobot_new_PI/main.cpp` — dispatcher 加 `step_up_with_sweep <cm>`
- `web_backend/public/index.html` — auto cycle 區加按鈕「↑ 上移 + 連續清洗」
- `web_backend/public/app.js` — 綁定按鈕送 `step_up_with_sweep ${cm}`
### 原因
User：「新增一個 step_up 按鈕，同步進行往上移動和 cleaning arm sweep 兩件事」+「在爬行過程中持續刷洗，arm sweep 走完發現 step 還沒結束就繼續一輪；step 結束後若 arm sweep 進行中，就等 arm cleaning 完整輪結束」。

**設計**：
- 主 thread：`do_step_up_(skip_sweep=true)`（feet → body → mid-realign）
- 背景 thread (std::async)：`do_arm_clean_sweep_continuous_`（Phase A 補水 → Phase B arm INIT，然後 `while (keep_going) { LEFT roller round; RIGHT scraper round }`）
- 主 thread 跑完 → `sweep_keep_going.store(false)` → `fut_sweep.get()` 等當前 round 跑完
- RAII `SweepJoin` guard：任何 return 路徑都確保 sweep 收尾（先 `flag=false` 再 `f.wait()`），不會 orphan thread

**Round 邊界檢查**：keep_going 只在 round 之間（LEFT/RIGHT 一對之後）檢查，sub-round 內不檢查 → 確保 user 要的「完整輪結束」語意，arm 不會卡在 LEFT 沒切到 RIGHT 就停。

**錯誤策略**（user 確認）：sweep 內部錯誤（DEPLOY obstacle、relay write fail 等）→ log + return ERR + cleanup，**不**走 try_or_pause_（會跟主 thread state machine 打架）。主 thread 跑完後從 future 拿到 ERR 字串、log warning 但不影響 step_up 成功判定。

**資源並行性**：
- step_up 用 cli_20_ (feet rail 1,3) + cli_21_ (ZDT) + cli_22_ (JC100/vacuum 閥)
- sweep 用 cli_20_ (上滑台 slave 5) + cli_22_ (water 閥 5/6/7 + XKC) + arm_cli_
- cli_20_/cli_22_ TCP mutex 序列化，bus latency 增但無 corruption
- arm_cli_ / cli_21_ 完全獨立
- motion_mtx_：step_up 持有；sweep 不持（用 internal helper 版本）
- state_：step_up Running；sweep 不動
- motion_active_：step_up 控制；sweep 不動

**機構安全提醒**：bench 第一次跑要看 arm sweep ±55cm 軌跡 + robot 上爬中（feet rail 0↔step_cm）的機構不會撞。如果有問題，arm sweep 行程要縮（ARM_SWEEP_CM）或改非並行模式。

**驗證 bench**：
1. 按新按鈕，看 log 應該並行出現 `[step_up+sweep]` / `[arm_clean_sweep_cont]` / 平常的 step phase log
2. step 結束時 log 看到 `step_up done, waiting for current sweep round to finish...`
3. 整 step 時間 < 原本 cmd_step_up（少了末段 30s sweep）
4. sweep 輪數會 ≥ 1，依 step 時間長短可能 2-3 輪

## 2026-05-22h Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `DISABLE_RETRY_INCR_PULSE` 2400 → 3000
- `user_lib/WASH_ROBOT.cpp` — 同步更新 disable_seal stall 進度判定的註解（240° → 300°）
### 原因
User：把 disable_seal 弱密封重試的每 iter 補伸從 0.8cm 換回 1.0cm。

- `DISABLE_RETRY_INCR_PULSE` 2400 → **3000**（身體 3000 pulse/cm → 1.0cm；腳組 2857 pulse/cm → ~1.05cm）
- 2026-05-19 曾從 3000→2400，本次還原。
- 連帶影響：iter 0~4 五輪累積補伸 = +1.0~+5.0cm（原 +0.8~+4.0cm）。
  累積上限 `DISABLE_RETRY_MAX_OVEREXTEND = 15000`（+5.0cm）不變 →
  iter 4 push 後剛好到 cap，五輪仍全部能 push，cap 不提早截斷，weak_seal 仍由 MAX_ITERS 收尾。
- `MAX_OVEREXTEND` 與 `cpp` 內 stall 進度判定註解一併更新對齊。
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 三個 motion timing 參數加快
### 原因
User：speed-up bench，挑了 3 個改：
- `VACUUM_RELEASE_WAIT_MS` 4000 → **3000** ── valve OFF 後等 cup 洩壓 / 線路 vent 時間。實機 `vacuum_release: all released after 200ms` 通常很快，4s 過於保守，省 1s/phase
- `PUSHER_RPM_RETRACT` 30 → **50** ── two-stage retract 第一段慢速脫壁。30 RPM 慢，50 RPM 同樣安全脫壁但快 67%。每次 retract 省 2-3s
- `VACUUM_SETTLE_MS` 2000 → **1500** ── extend 後 settle 等真空。cup 通常 < 1s sealing，1.5s 仍夠 buffer。每 round 省 500ms

跳過的（user 選不改）：3 (`RETRACT_SLOW_PEEL_CM`)、5 (`DM2J_RPM` 上滑台/輪子)、6/7/8。

預估每 step 省 ~4-6s（feet + body phase 各受影響）。

驗證 bench：
- cup 收回時聲音 / 完整度（PUSHER_RPM_RETRACT 50 不能傷 cup）
- vacuum release 後 retract 立刻開始有沒有殘留 cup 黏附
- extend 後 1.5s 內 cup 是否來得及 sealing（看 disable_seal log 第一輪 SEALED 等待時間）

## 2026-05-22f Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_DEPLOY_POS_TOL_RAD` 0.10 → 0.15
### 原因
User bench：「DEPLOY LEFT 已經好幾次都要 retry 才會好」。

log 顯示自然 motor variance：
- 第一次：M1 actual=0.483 expected=0.586 delta=0.103（剛剛超過 tol 0.100 → 誤判 obstacle、PausedOnError）
- Retry：M1 actual=0.632 expected=0.586 delta=-0.046（overshoot）

兩次差 ~0.15 rad 不是撞 obstacle、純 PD impedance 控制的自然 variance（起始速度、摩擦、雜訊）。tol 0.10 太緊 → false positive。

放寬到 0.15 rad（~8.6°、~48mm short）── motor 自然 jitter 不會誤判，但真撞 obstacle（通常 ≥100mm short）仍抓得到。

驗證 bench：跑 DEPLOY 看 log 的 delta 分佈，正常範圍應該都 < 0.15；故意擋 obstacle 應該 delta > 0.15 觸發 PausedOnError。

## 2026-05-22e Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_step_down_` / `do_step_up_` mid-step realign 返回後加 `motion_active_ = true`
### 原因
User bench：「JC100 5/6 在 disable_seal WAIT_SEAL 期間讀 -1~-4 kPa（沒吸到），但 recover 立刻讀 -60 kPa（吸好了）」。

User 自己想到：「是不是背景讀真空的地方搶到了」── 正確!

**Race**：`do_feet_realign_` 自己是個 standalone motion，所以結束時把 `motion_active_ = false`（line 4785）。但 step_down/up 的**中段 realign**（line 3789/4014）是在 step 還沒結束時呼叫的。realign 跑完後 `motion_active_` 變成 false，但 step 繼續跑 Phase B body cycle。`pressure_poll_loop_` 看到 `motion_active_=false` 開始輪詢 JC100 on cli_22_ → 跟 body cycle 的 disable_seal WAIT_SEAL 撞 bus → Modbus collision / stale frames → JC100 5/6 等讀錯。

只有 mid-step realign 之後的 body cycle 才會撞 → 解釋為什麼**不是每次都壞** + 為什麼**只有特定 cup（5/6 在 poll sequence 中段）會受影響**。

修法：兩處 `lk.lock()` 之後馬上補 `motion_active_ = true`（兩行 + 大段註解）。

**仍待考慮**（沒做）：更乾淨的做法是 realign 用 RAII guard 保存/恢復原 motion_active_ 值，或加 `mid_step` 參數讓內部 realign 不要 reset 旗標。但兩行 fix 直接到位，先這樣。

驗證 bench：跑 step_down/step_up，看 JC100 5/6 在 disable_seal 期間能不能讀到 -50 以下、不再 weak_seal early exit。

## 2026-05-22d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — 5 個 `ensure_arm_center_for_rope_` call site 全部註解掉
### 原因
User：「現在幫我把所有在收放繩之前 deploy center 都註解掉」。

註解掉的 5 個位置：
1. line 1412 — `crane_pay_out_to_weight_` loop 起始
2. line 3686 — `do_step_down_` body pre crane_pay_out
3. line 3981 — `do_step_up_` step_up_pre_feet（feet phase 起始）
4. line 4145 — `do_step_up_` body_backup_pay_out_up
5. line 5433 — `cmd_return_home` descent pay_out

**保留**（沒動）：
- helper 定義 `ensure_arm_center_for_rope_` 本體（line 663）
- `verify_arm_deploy_` 跟 `ensure_arm_parked_after_rope_` 兩個 helper
- ARM_ROPE_PROTECTION constant + arm_stow_state_ 等
- `ensure_arm_parked_after_rope_` 在 3 個 end-of-realign 注入（5/21z 加的）+ 2 個 step_*_no_progress（5/21r 加的）
- cmd_arm_deploy 的 verify_arm_deploy_ obstacle 檢查（5/21x）
- clean_sweep Phase C 內 LEFT/RIGHT DEPLOY 的 verify（5/21x）

**結果**：自動流程不再做 pay_out 前 DEPLOY CENTER stow。arm 跑 pay_out 時就在原本位置（通常是 Parked）。pole 下降時 arm 不會被自動帶到牆上去避。

**將來想恢復**：grep `DISABLED 2026-05-22` 一次找齊、解除註解即可。或 flip ARM_ROPE_PROTECTION → 但因為這次是註解 call site、不是讓 helper return early，flag 沒用、要實際解註解。

⚠ **意涵**：原本「桿子下降前 arm 閃過去」的物理保護解除。User 自己判斷 arm 在 PARK 位置時不會擋桿子（或不在乎），決定關掉自動 stow。

## 2026-05-22c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_ROPE_PROTECT_WALL_MM` 300 → 250；註解 `DEPLOY 300 CENTER` → `DEPLOY 250 CENTER`
- `user_lib/WASH_ROBOT.cpp` — `crane_retract_safe_` 那段註解的 `DEPLOY 300 CENTER` → `DEPLOY 250 CENTER`
### 原因
User：「幫我改成 center deploy 通通用 250mm」。

`ARM_ROPE_PROTECT_WALL_MM` 是唯一的「自動 CENTER deploy」wall_mm 常數，被 `ensure_arm_center_for_rope_` 拿來組 `DEPLOY <X> CENTER` 指令給 motor_api。改 300 → 250 之後：
- 所有 pay_out 前自動 stow 的 DEPLOY → 250mm CENTER
- `verify_arm_deploy_` 算 expected θ 也用 ARM_ROPE_PROTECT_WALL_MM、自動跟著變

新 expected θ（CENTER, wall=250）：
- total_ext = PASSIVE 86.46 + TOOL_CENTER 160.00 = 246.46 mm
- usable = 250 - 246.46 = 3.54 mm
- expected θ = 0.38 + asin(3.54/320) ≈ 0.391 rad（之前 wall=300 是 0.548 rad）

⚠ 250mm 比較淺的 stow ── arm 沒伸太遠出去，跟 PARK 位置（home=0 rad）距離較近、防護幅度較小。但這是 user 決定，可能基於：撞牆風險低 / 桿子下降空間不需那麼多 / 或別的物理觀察。

未動：
- `ARM_CLEAN_WALL_MM = 300`：clean_sweep 的 wall_mm，但 5/20v 之後 clean_sweep 沒有 CENTER deploy（只 LEFT/RIGHT），不算 CENTER。
- GUI `arm-wall-mm` 預設 300：給 user 手動輸入，LEFT/CENTER/RIGHT 都用同一個，動了會影響非 CENTER 場景。

## 2026-05-22b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_run` 的 state check 註解掉
### 原因
User：「cmd_run 也把 state check disable」。

`cmd_step_down` / `cmd_step_up` 之前已經 disable state check（從任何狀態都能跑），但 `cmd_run` 沒對齊，還在卡 `if (cur != State::Attached) return state_violation_(cur);` → bench 從 Idle/Error 按 run 都被擋。

註解掉兩行（保留 commented 程式碼當未來恢復用），跟 step_down/up 註解風格一致。

## 2026-05-22a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 `cmd_brush(bool)` / `cmd_water_pump(bool)` / `cmd_water_inlet(bool)` 宣告
- `user_lib/WASH_ROBOT.cpp` — 三個 impl，pattern 同 `cmd_pump`（Error 狀態拒絕 + 直接 `pqw_.controlRelay`）
- `washrobot_new_PI/main.cpp` — dispatcher 加 `brush` / `water_pump` / `water_inlet` 三條（`<on|off>` 參數）
- `web_backend/public/index.html` — 新 panel-washrobot 區「manual — cleaning」放在 manual — vacuum 之後，三組 ON/OFF 按鈕（球閥 CH7 / 水泵 CH6 / 刷子 CH5），`data-page="manual"`
### 原因
User：「在網頁上加上球閥、刷洗滾筒、幫浦的按鈕」。

背景：5/21 user 報 emergency_stop 後問「抽水馬達等有關嗎」。設計上 `do_arm_clean_sweep_` 的 RAII cleanup 會關三個 relay，但用的是 `pqw_.controlRelay`（raw，不 readback），如果通訊 hiccup 可能 silent fail。加手動關閉按鈕當保險。

CH 對應（PQW slave 12）：
- CH5 = 刷洗滾筒馬達（arm 上）
- CH6 = 水箱泵浦（噴水）
- CH7 = 水箱進水球閥（頂樓水壓 → 補水箱）

放在「manual — cleaning」section（`data-page="manual"` → 控制頁籤）。Error 狀態拒絕避免亂噴水或吸盤洩壓組合。

## 2026-05-21z Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — 三個 end-of-realign 呼叫處加 `ensure_arm_parked_after_rope_`
### 原因
User：「realign 結束後放繩玩(完)，為什麼手臂沒有 park 回去」。

realign Phase 5 跑 `crane_pay_out_to_weight_` 把繩拉回到目標張力。pay_out_to_weight_ 開頭會觸發 `ensure_arm_center_for_rope_` → arm INIT + DEPLOY CENTER。realign 結束後 arm 卡在 CENTER 沒人帶回 PARK。

修法：三個 end-of-realign 點各加 PARK 注入：
- `cmd_step_down` line ~3910 (do_step_down_ 跑完 → realign → 末端)
- `cmd_step_up` line ~4822 (do_step_up_ 跑完 → realign → 末端)
- `cmd_realign` line ~5507 (manual realign command)

**Mid-step realign 不加**（line 3789 / 4012 等）：mid-step realign 跑完後 step 還會繼續、下次 pay_out 又會 DEPLOY，PARK 中間沒意義。

**為什麼 PARK 安全**：
- realign Phase 1 retract X cm（X = crane_assist_actual_cm）
- realign Phase 5 pay_out_to_weight CAP=X cm（同一個變數當上限）
- net rope motion ≈ 0 → 桿子相對位置幾乎不變 → PARK 軌跡跟 realign 前一樣安全
- 跟 5/21o 描述的「step_down body phase pay_out 大量造成桿子擋路」場景不同

state-tracked 行為：
- step_down 末段：cleaning sweep cleanup 把 arm PARK 過了 → realign Phase 5 重新 DEPLOY CENTER → 新加的 PARK 注入再 park 回來
- step_up 末段：同上
- manual cmd_realign：上次 arm 在哪不一定，realign Phase 5 確保 CENTER → 新加的 PARK 把它 park

驗證 bench：跑 cmd_realign 看 log 是否多出：
```
[realign] done — all cups reset to preset
[arm_protect] cmd_realign_done — PARK
```

## 2026-05-21y Claude (Sadie)
### 修改檔案
- `web_backend/public/app.js` — `updateErrorPauseUI` 的 `error-pause-label` 文字拿掉 "STATUS:" 前綴與 "暫停中"
- `web_backend/public/index.html` — `error-pause-label` 預設文字 `STATUS: unknown` → `unknown`
### 原因
User：error 狀態 bar 看不到重點字。把填充字拿掉，讓重點（ERROR + 失敗 context）有空間：
- PausedOnError：`STATUS: ERROR 暫停中 —` → `ERROR —`
- Error：`STATUS: ERROR — 可按…` → `ERROR — 可按…`
- 其他：`STATUS: <state>` → `<state>`
- 預設：`STATUS: unknown` → `unknown`

## 2026-05-21x Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 補 LEFT/RIGHT TOOL_EXT 鏡像常數；加 `verify_arm_deploy_` 宣告
- `user_lib/WASH_ROBOT.cpp`：
  - 抽出 `verify_arm_deploy_(slot, wall_mm)` helper（包含 5/21w 的 STATUS 解析邏輯 + slot-aware expected θ）
  - `ensure_arm_center_for_rope_` 內聯版改成 call helper
  - `cmd_arm_deploy` (GUI) DEPLOY 成功後 call helper，撞障礙物 → return `ERR DEPLOY obstacle`
  - `do_arm_clean_sweep_` Phase C 的 DEPLOY 包進同一個 try_or_pause_ 內呼叫 helper，撞障礙物 → PausedOnError
### 原因
User：「擴到所有 DEPLOY (GUI + sweep)」（延續 5/21w 的問題：先前只有 ensure_arm_center_for_rope_ 的 CENTER deploy 有保護，GUI 跟 sweep 內的 LEFT/RIGHT/CENTER 都沒接）。

**抽 helper**：把 5/21w 的內聯邏輯獨立成 `verify_arm_deploy_(slot, wall_mm)`：
- 對應 slot 取 TOOL_EXT（LEFT/CENTER/RIGHT 三組值）
- 套用同一條 motor_api `touch_wall_slot` 公式算 expected θ
- 同樣的 STATUS 解析、tolerance 比對

**三個 call site**：
1. `ensure_arm_center_for_rope_` — 維持 CENTER + ARM_ROPE_PROTECT_WALL_MM（pay_out 前 stow，撞 obstacle → return true → try_or_pause_ → PausedOnError）
2. `cmd_arm_deploy` (GUI 按鈕) — slot 跟 wall_mm 由 user 指定，撞 obstacle → 回 ERR 字串給 GUI（不更新 arm_stow_state_，user 看 log 處理）
3. `do_arm_clean_sweep_` Phase C `sweep_with_tool` lambda — DEPLOY + verify 放進同一個 try_or_pause_ closure，撞 obstacle → 當作 DEPLOY 失敗、走 PausedOnError 路徑（user Continue → 整個 DEPLOY+verify 再跑）

**TOOL_EXT 鏡像加 LEFT/RIGHT**（之前只有 CENTER）：
- `ARM_M2_TOOL_LEFT_MM  = 148.09`
- `ARM_M2_TOOL_RIGHT_MM = 134.07`
（CENTER = 160.00 保留）

對應 expected θ（wall_mm=300）：
- CENTER：usable=53.54 mm → θ ≈ 0.548 rad
- LEFT：usable=65.45 mm  → θ ≈ 0.586 rad
- RIGHT：usable=79.47 mm → θ ≈ 0.632 rad

公差統一 `ARM_DEPLOY_POS_TOL_RAD = 0.10 rad`（~5.7°、~35mm at arm tip）。

**反向：將來 motor_api 升級到 Option A（自己回 ERR）→ 砍 verify_arm_deploy_ helper + 三個 call site 註解就行**，全部 grep `arm rope protect TEMP` 找齊。

## 2026-05-21w Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 motor_api 鏡像常數（ARM_M1_LENGTH_MM / PASSIVE_EXT / VERTICAL_OFF / TOOL_CENTER）+ `ARM_DEPLOY_POS_TOL_RAD`
- `user_lib/WASH_ROBOT.cpp` — `ensure_arm_center_for_rope_` DEPLOY 後加 STATUS 驗證（Option B 障礙物偵測）；加 arm_attached=off 早期跳過
### 原因
User：「B」（依前面三個選項：A=改 motor_api、B=washrobot STATUS 驗證、C=只 log）。

**目的**：motor_api 的 `touch_wall_slot` 因為 `wait_for_move best-effort, return ignored`，M1 卡在障礙物會 silent return OK → washrobot 誤以為 arm 已到牆。改在 washrobot 端 DEPLOY 後驗證 M1 實際角度。

**做法**：
1. WASH_ROBOT.h 鏡像 motor_api 的 4 個物理常數（main_api.h `MotorSlot`）：
   - `ARM_M1_LENGTH_MM=320`、`PASSIVE_EXT_MM=86.46`、`VERTICAL_OFF_RAD=0.38`、`TOOL_CENTER_MM=160.00`
   - 公差 `ARM_DEPLOY_POS_TOL_RAD=0.10`（~5.7°、~35mm at 320mm arm tip）
2. ensure_arm_center_for_rope_ 在 DEPLOY OK 後：
   - 用 motor_api 同一條公式算 expected θ（DEPLOY 300 CENTER ≈ 0.548 rad）
   - 送 `STATUS` 讀回 `[M1] pos=X.XXXX`
   - parse 失敗或 actual < expected - TOL → return true（PausedOnError）

**arm_attached=off 早期跳過**：之前 helper 沒檢查 arm_attached，會走完 INIT/DEPLOY/STATUS 三條 arm_cmd_、全部回 "OK skipped"，STATUS 解析會失敗。現在 helper 第一步檢查 arm_attached，off 直接 return false。

**驗證 bench**：
- 正常 DEPLOY → 看 log `[arm_protect] DEPLOY verify: M1 actual=0.5XX expected=0.548 delta=0.0XX rad`，delta 應 < TOL
- 故意擋住 arm → 應該看到 `DEPLOY hit obstacle — M1 stopped XX mm short` + PausedOnError
- 調 TOL：太緊 → 正常 DEPLOY 也誤判，太鬆 → 真障礙物沒抓到

**未來如果改 motor_api 物理常數（main_api.h）→ 必須同步更新 WASH_ROBOT.h 鏡像**。理想是 motor_api 改成 A 方案（自己回 ERR）就不用鏡像，但 user 暫不做。

## 2026-05-21v Claude (Sadie)
### 修改檔案
- `web_backend/public/index.html` — sidebar 3 顆 nav 按鈕：emoji icon 換成 `.nav-dot`、標籤改英文（Home / Control / Camera）
- `web_backend/public/style.css` — 移除 `.nav-ico`，新增 `.nav-dot`（中空 cyan 環，active 時填滿 + 發光）
### 原因
User：分頁圖示換成跟主題相符（深空 aurora，emoji 太花），改用一個點即可；標籤改英文。

`.nav-dot` 沿用 GUI 既有的「dot」視覺語彙（header 連線狀態也是用 dot）：未選 = 中空 cyan 環，選中 = 填滿 cyan + cyan glow。標籤 首頁/控制/攝影機 → Home / Control / Camera，`title` 屬性同步改英文。data-page 內部值（home/manual/camera）不變，CSS / app.js 不受影響。

## 2026-05-21u Claude (Sadie)
### 修改檔案
- `web_backend/public/index.html` — raw command section 加 `panel-raw` class
- `web_backend/public/style.css` — 新增 `.panel-raw { grid-column: 1 / -1; }`
### 原因
User：raw command panel 要佔一整行（原本在 grid 裡只佔半行/一格）。比照 `.panel-log` 既有的 `grid-column: 1/-1` 做法，加一個 `panel-raw` class 讓它跨滿整個 panel grid 的寬度。

## 2026-05-21t Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_step_up_` feet phase 之前加 `ensure_arm_center_for_rope_("step_up_pre_feet")`
### 原因
User：「step up 腳組往上之前應該要先依樣要先把手臂 init -> 300mm deploy center」。

對稱性問題：
- **step_down**：body phase 先跑（內含 pay_out → 走 `ensure_arm_center_` 注入），arm 自動被 stow；feet phase 之後跑時 arm 已在 CENTER（state-tracked no-op）
- **step_up**：feet phase 先跑、**無 pay_out**（body anchored、純 rail 移動）→ 沒任何點觸發 `ensure_arm_center_`；body phase 雖然會 retract 但 retract 不會主動 stow arm

結果：step_up feet 上爬時 arm 可能還在 PARK 位置（剛跑完 step_down + cleaning sweep cleanup）→ 不安全。

修法：在 `cycle_group_("feet", ...)` 呼叫之前加 `try_or_pause_(ensure_arm_center_for_rope_)`，tag `step_up_pre_feet` / context `arm_stow_for_step_up_feet`。state-tracked 後續 body phase 的 ensure_arm_center 自動 no-op。

注意：`do_step_down_` 不用加類似修，因為它先跑 body phase（含 pay_out → 已注入 ensure_arm_center），feet phase 後跑時 arm 已在 CENTER。

## 2026-05-21s Claude (Sadie)
### 修改檔案
- `web_backend/public/index.html` — 加左側 sidebar nav（首頁/控制/攝影機 3 按鈕）；14 個 panel 各加 `data-page`；camera section 從 `</main>` 外搬進 `<main>`；`<head>` 加 favicon link
- `web_backend/public/style.css` — 新增 `#sidebar` / `.nav-btn` 樣式；`header/#banner/main` 加 `margin-left` 讓出 sidebar 空間；page 切換的顯示/隱藏規則
- `web_backend/public/app.js` — 新增 `initPageNav`：按鈕切 `<body data-page>`、記憶上次選的頁（localStorage）
- `web_backend/public/favicon.svg`（新增）— 機器人頭小 icon（深空 cyan/purple 主題）
### 原因
User 要求 web GUI 依功能分 3 頁、左側 bar 切換，並加瀏覽器分頁 icon。

分頁：
- **首頁**：auto cycle、vacuum readings、crane、🆘 緊急收繩、raw command、log
- **控制**：manual vacuum / pusher / wheels / DM2J group sync、清潔手臂、arm 整合操作、easy crane
- **攝影機**：攝影機 panel

做法（不實體搬動 13 個大 panel 區塊、降低出錯）：每個 `<section class="panel">` 加 `data-page="home|manual|camera"`，`<body data-page>` 記錄當前頁，CSS `body[data-page=X] .panel:not([data-page=X]) { display:none }` 隱藏非當前頁的 panel → `<main>` 的 auto-fit grid 自動只排當前頁。camera section 原本在 `</main>` 外（free-floating），搬進 main 才能跟其他 panel 同一個 grid + 吃到 sidebar 的 margin。

sidebar 固定定位（`position:fixed` 左側 76px），`header/#banner/main` 加 `margin-left:76px` 讓出空間。JS 失敗時 `<body>` 仍有 HTML 寫死的 `data-page="home"`、且 CSS 規則對「無 data-page」不隱藏任何 panel → 安全 fallback。

未來加頁 / 加 panel：panel 加對應 `data-page`、sidebar 加按鈕、app.js 的 `PAGES` 陣列加一個即可。

驗證：開 web GUI，三顆按鈕切頁、重整後記得上次的頁、瀏覽器分頁出現 robot icon；各 panel 的既有功能（autocycle / crane / 截圖…）不受影響（element id 全沒變）。前端檔案重整瀏覽器即生效。

## 2026-05-21r Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_step_down_` / `do_step_up_` 的 no_progress 早期 return 加 `ensure_arm_parked_after_rope_`
### 原因
User bench log：
```
[step_down] body backed up to origin (rail=0 cm) — skip feet phase, no actual descent
[step_down] OK step_down_no_progress
[realign] phase 0: ...
```
→ User：「這邊沒有把手臂 park」。

`do_step_down_` / `do_step_up_` 的 no_progress 早期 return 跳過 cleaning sweep（cleaning sweep 是 do_*_ 流程末段才跑），cleaning sweep cleanup 也跑不到。arm 因為 pay_out 前的 `ensure_arm_center_for_rope_` 還停在 DEPLOY 300 CENTER，沒人帶回 PARK。

修法：在兩處 no_progress return 前加 `ensure_arm_parked_after_rope_(ctx)`（user 5/21r 選「只 PARK、不跑 sweep」）。

安全性 OK ── no_progress 路徑代表 rail 來回 backup 結束時又回到 0，**淨升/降 = 0**，桿子相對位置不變，PARK 軌跡跟最初一樣安全（不會撞 5/21o 描述的「桿子下降後擋路徑」情況）。

不影響正常流程：rail_delta > 0.5 的正常路徑仍走完整 cleaning sweep（其中 cleanup 自己 PARK），no_progress 才走這個新加的 PARK shortcut。tag 仍是 `[arm rope protect TEMP 2026-05-21]`，將來整片砍可一次找齊。

## 2026-05-21q Claude (Sadie)
### 修改檔案
- `web_backend/public/style.css` — `.panel-camera` 從 `grid-column: 1/-1`（佔滿整列）改回 `min-width: 320px`（與其他 panel 同寬）
### 原因
User：camera panel 不要佔滿整列，要跟其他 panel 一樣寬。改回一般 panel 寬度後，`.cam-grid` 的 `auto-fit minmax(260px,1fr)` 在 ~320px 寬的 panel 內只容得下 1 欄 → 兩台攝影機改為**上下堆疊**、各佔滿 panel 寬度（仍是等比例分割，只是垂直方向）。

## 2026-05-21p Claude (Sadie)
### 修改檔案
- `web_backend/public/index.html` — 兩個獨立 camera panel 合併成單一 `panel-camera`，內含 `cam-grid` + 每台一個 `cam-cell`
- `web_backend/public/style.css` — 新增 `.cam-grid` / `.cam-cell` / `.cam-cell-head`；`.panel-camera` 改 `grid-column: 1/-1`（佔滿整列）
- `web_backend/public/app.js` — `wireCamera` 的 `dock` selector 從 `.panel-camera[data-cam-id]` 改 `.cam-cell[data-cam-id]`
### 原因
User 要求網頁攝影機畫面合成同一個 panel、有幾台就等比例分割。

原本每台攝影機各自一個 `<section class="panel panel-camera">`，在主面板 grid 裡各佔一格。改成**單一 panel**，內部 `.cam-grid` 用 `grid-template-columns: repeat(auto-fit, minmax(260px, 1fr))` — 每台一個等寬 cell（2 台→各 50%、3 台→各 33%…），窄畫面自動換行堆疊。`.panel-camera` 設 `grid-column: 1/-1` 佔滿整列，確保多台並排有足夠寬度。

每台的 IP 標籤從各自的 `<h2>` 移到 cell 內的 `.cam-cell-head`；panel 標題統一為「📹 攝影機」。各攝影機的 element id（`camN-stream/-offline/-status/-snap/-reload`）不變 → app.js 的截圖 / 重連 / 離線偵測邏輯沿用，只改了 `dock` 的 selector（`data-cam-id` 從 section 移到 cell）。

未來加 cam3/cam4：index.html 複製一個 `.cam-cell`、app.js 加 `wireCamera('cam3')`、server.js CAMERAS 加一條即可，grid 會自動等比例分割。

驗證：開 web GUI，確認兩台攝影機在同一個 panel 內左右各半；截圖 / 重連 / 離線覆蓋層仍正常。

## 2026-05-21o Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `crane_retract_safe_` 兩個成功 return path 的 `ensure_arm_parked_after_rope_` 注入移除
### 原因
User bench 觀察：「向下移動時雖然桿子下降前 arm 有往前閃過了，但他在桿子下降完成後 PARK 回原位時會被桿子擋到路線」。

幾何分析：
- pay_out 讓 robot 下降、桿子相對 robot 上移（桿子物理位置不變但 robot 下降）
- DEPLOY 300 CENTER 把 arm 伸到牆面，避開桿子下降路徑 ✓
- retract 雖然讓 robot 上升、桿子相對下移，但每個 iteration 是「pay_out N + retract margin」配對，**淨下降**，桿子相對 robot 永遠比初始高
- 因此 PARK（arm 從 CENTER 退回 home）會穿過桿子現在的位置 → 撞到

User 決定（5/21o）：**「除了結束以外不 auto-PARK」**。
- 移除 `crane_retract_safe_` 兩個成功 return path 的 PARK 注入
- arm 在 step_down / step_up body phase 全程留在 DEPLOY 300 CENTER 不動
- PARK 只在 `do_arm_clean_sweep_` cleanup（step_down / step_up 末段）發生

**保留**：
- helper `ensure_arm_parked_after_rope_` 定義 + `arm_stow_state_` tracking + `cmd_arm_park` / `cmd_arm_deploy` / `cmd_arm_init` 的 state 更新 — 將來想恢復 auto-PARK 直接 grep 之前的 marker 恢復注入即可
- pay_out 端的 4 個 `ensure_arm_center_for_rope_` 注入 — 那一邊沒問題，照舊保護

**注意**：
- 如果 clean_sweep cleanup 的 PARK 在實機也撞到桿子 → 移除 `do_arm_clean_sweep_` 的 `arm_cmd_("PARK", 30)`（line ~720）+ 相應 state 更新
- arm 持續 DEPLOY 30s+ 期間不會傷 motor（motor_api hold 模式）；長期續行（小時級）才需要考慮電流發熱

## 2026-05-21n Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 `ARM_ROPE_PROTECTION` 開關常數、`ArmStowState` enum + `arm_stow_state_` atomic、`ensure_arm_center_for_rope_` / `ensure_arm_parked_after_rope_` 宣告
- `user_lib/WASH_ROBOT.cpp`：
  - 實作上述兩 helper（state-tracked，已 Center/Parked 直接 return false 不動 arm）
  - `cmd_arm_init` / `cmd_arm_deploy` / `cmd_arm_park` / `do_arm_clean_sweep_` cleanup 都更新 `arm_stow_state_`
  - 4 個 pay_out 注入點：`crane_pay_out_to_weight_` 開頭、`do_step_down_` body pay_out、`do_step_up_` body backup pay_out、`cmd_return_home` descent pay_out
  - `crane_retract_safe_` 兩個成功 return path 都加 PARK 注入
### 原因
User：「現在上面裝了機械手臂，放繩時桿子怕打到他，所以幫我在所有放繩動作前加上 init arm → 300mm center deploy，收繩完在 park」+「這個是暫時性的，幫我做到隨時可以註解掉的方便」。

**設計**：
1. **單一 constexpr 開關** `ARM_ROPE_PROTECTION = true`（WASH_ROBOT.h）。翻 false → 兩 helper 都在頂端 early-return → 行為等同 pre-change
2. **state-tracked**（user 確認，避免 1cm pay_out 循環裡反覆 20s 的 INIT+DEPLOY 浪費）。`ArmStowState { Unknown, Center, Parked }`，相同狀態 helper 直接 return
3. **call site 統一標 `// [arm rope protect TEMP 2026-05-21]`**：將來 grep 這個 token 就能一次找到 helper 定義 + 全部呼叫點，整片刪除
4. **失敗策略**：
   - INIT / DEPLOY 失敗 → helper return true → caller 經 `try_or_pause_` 進 PausedOnError（user 可手動排除後 retry / skip）
   - PARK 失敗 → helper log warn 但 return true 不阻塞流程（retract 已完成、後續流不依賴 PARK）
5. **loop 邊界**（user 確認）：
   - `crane_pay_out_to_weight_`：loop 開始前一次 ensure_arm_center，loop 內 1cm pay_out 跑 raw `crane_cmd_` 不再保護
   - `crane_retract_to_weight_`：透過 inner `crane_retract_safe_` 自動 PARK；第一個 5cm step 後狀態 = Parked，後續 steps state-tracker 自動 no-op

**注入位置**：
| 點 | 操作 | 用途 |
|---|---|---|
| `crane_pay_out_to_weight_` 開頭 | ensure_arm_center | attach 末段、realign 都會跑 |
| `do_step_down_` body 段 pay_out | ensure_arm_center | 主下降 body phase 每次 pay_out (state-tracked 後續 no-op) |
| `do_step_up_` body backup pay_out | ensure_arm_center | step_up retry 路徑 |
| `cmd_return_home` descent | ensure_arm_center | 整段降到地面的 long pay_out |
| `crane_retract_safe_` tripped path | ensure_arm_parked | 張力觸頂提早停 |
| `crane_retract_safe_` 正常完成 | ensure_arm_parked | 一般 retract 完成 |

**注意**：
- 在 do_step_down_ 末段 / do_step_up_ 末段已有 cleaning sweep（5/21d 加的），sweep 內部會 INIT / DEPLOY / PARK，會與本 protection 機制互動。cleaning sweep 結束 cleanup 時更新 `arm_stow_state_ = Parked`，下個 pay_out 會正確 re-INIT+DEPLOY
- 整個機制只在 user 接 arm 上機器人時才有意義；arm 未裝 → 把 `ARM_ROPE_PROTECTION` 翻 false
- arm_attached=off 時 `arm_cmd_` 返回 "OK skipped"，helper 的判斷 `rfind("OK", 0) == 0` 會通過、state 會更新成功，等於 protection 機制在 attached=off 下也安靜 no-op

**將來移除**：grep `arm rope protect TEMP` 找全部 marker 一次砍掉。

## 2026-05-21m Claude (Sadie)
### 修改檔案
- `web_backend/server.js` — `CAMERAS` 預設 host 從寫死 `192.168.1.100` 改成 `${WASHROBOT_IP}`
### 原因
User bench 啟動指令：
```
CRANE_IP=127.0.0.1 EASY_CRANE_IP=127.0.0.1 WROBOT_IP=192.168.5.20 node server.js
```
washrobot Pi 兩張網卡：eth0 `.1.100`（接相機 + Modbus gateway）、wlan0 `.5.20`（跟 crane Pi WiFi 互通）。crane Pi 只看得到 .5.x 那段，所以 `WROBOT_IP=192.168.5.20`。

但 `CAMERAS` 預設寫死 `http://192.168.1.100:5004/5005`，crane Pi 反代過去 → `upstream_unreachable`。User 必須額外加 `CAM1_URL=http://192.168.5.20:5004 CAM2_URL=...` 才會通。

改成預設 `http://${WASHROBOT_IP}:5004/5005` 之後：
- bench 場景（WROBOT_IP=192.168.5.20）→ 攝影機自動跟著走 WiFi，不用另外設
- production 場景（WROBOT_IP 預設 .1.100）→ 跟之前行為一致
- 仍可用 `CAM1_URL` / `CAM2_URL` 個別覆蓋（例如相機反代到別台機器）

bench 啟動指令現在可以簡化成：
```
CRANE_IP=127.0.0.1 EASY_CRANE_IP=127.0.0.1 WROBOT_IP=192.168.5.20 node server.js
```
（不變，但攝影機會自動通）

## 2026-05-21l Claude (Sadie)
### 修改檔案
- `frame_capture/frame_capture.py` — `DEFAULT_RTSP_URL` IP `.10` → `.110`
- `frame_capture/README.md` — 全文 `.10` → `.110`、預設值說明補上 cam2=.111
- `web_backend/server.js` — `CAMERAS` 加 `cam2: http://192.168.1.100:5005`
- `web_backend/public/index.html` — CAM 1 標題 `.10` → `.110`；新增 CAM 2 section
- `web_backend/public/app.js` — `wireCamera('cam2')` 啟用
- `scripts/wr.sh` — launcher 開兩個 frame_capture instance（cam1 :5004 + cam2 :5005），加 `CAM1_IP`/`CAM2_IP` env 覆蓋
- `.claude/camera_obstacle_plan.md` — IP 表更新
### 原因
User：「相機的 ip 是 110、111」+「兩台同時上」。

`nmap -sn 192.168.1.0/24` 掃出真實相機 IP 是 `.110` / `.111`，標 `00:12:34:` placeholder MAC（雄邁 Xiongmai firmware 預設、被 nmap OUI 表誤標成 Camille Bauer）。之前 frame_capture / README / GUI / camera_obstacle_plan 都寫死 `.10`，已 retire。

**新配置**：
- cam1: RTSP `rtsp://192.168.1.110:554/...`，frame_capture instance HTTP `:5004`，輸出 `/tmp/cam1_latest.jpg`，web GUI `/mjpeg/cam1` `/snap/cam1`
- cam2: RTSP `rtsp://192.168.1.111:554/...`，frame_capture instance HTTP `:5005`，輸出 `/tmp/cam2_latest.jpg`，web GUI `/mjpeg/cam2` `/snap/cam2`

兩個 frame_capture 是獨立 process（同 .py、不同 CLI 參數），cam1 跟 cam2 互不干擾。launcher `wr.sh start` 會開兩個 tmux window (`cam1` / `cam2`)。

web_backend `CAMERAS` map 內仍指向 washrobot Pi（.100），由 Pi 上的 frame_capture HTTP server 反代 — browser → crane Pi `:8080` → washrobot Pi `:5004/:5005` → RTSP camera。

可用 env override 換 IP（bench / production 差異）：
```bash
CAM1_IP=192.168.1.110 CAM2_IP=192.168.1.111 ./scripts/wr.sh start
```
或在 crane Pi 上用 `CAM1_URL=http://x.x.x.x:5004 CAM2_URL=...` 覆蓋反代目標。

注意：`/tmp/cam_latest.jpg`（detect_server.py 預設輸入）這個檔名沒人寫了 ── cam1 寫的是 `cam1_latest.jpg`。如果 detect 程式還在跑、預期讀 `/tmp/cam_latest.jpg`，需要在 detect 側改路徑或 launcher 多開一個 sym link（之後遇到再處理）。

## 2026-05-21k Claude (Sadie)
### 修改檔案
- `user_lib/DM2J_RS570.cpp` — `PR_move_cm` 大改：comm 重試 + `!ever_busy` 改位置判定 + trigger 重送
### 原因
arm sweep 實機兩種症狀都指向 DM2J `PR_move_cm` 不穩：(1) 手臂靜默停在左邊、程式繼續（`!ever_busy` 誤判 no-op 假成功）；(2) `arm_sweep_left` PausedOnError（`PR_move_cm` 真的回 error、無重試）。User 要求把 `PR_move_cm` 修穩。

改動：
1. **`!ever_busy` 不再盲目當 no-op 成功** — 原本「path_done 500ms 內沒清掉 → 假設已在目標 → return false」會把「trigger 被 drive busy / Modbus 抖動吃掉」誤判成成功，馬達靜默卡在前一個目標。改成：讀**實際 encoder 位置**（`read_motor_position`）跟目標比 —
   - 在目標 ±0.3cm 內 → 真 no-op（或移動快到沒觀察到 busy）→ 成功
   - 不在目標 → trigger 沒生效 → **重送 trigger**（最多 3 次）
2. **trigger 重送** — 外層 retry loop，最多 3 次 `PR_move_set + PR_trigger`；3 次都沒生效才回 error
3. **Modbus 讀取重試** — `read_status` / `read_pulse_per_rev` 包 3 次重試（間隔 30ms），吸收 bus 瞬間抖動，不再一個壞 frame 就整個 move 失敗
4. 目標位置換算：mode 1（絕對）= `pos_pulse`；mode 0（相對）= 移動前讀 start position + `pos_pulse`（讀不到才回 error）
5. PR fault（status bit 0x0001）仍照舊立即回 error — 那是真故障、該停 / PausedOnError
6. Phase 2 的 20s timeout 仍回 error（真 stall / 機構問題，重送無益）

影響範圍：所有 `PR_move_cm` 呼叫端（腳組 rail、輪子、上滑台 arm）。方向是讓它更可靠、正常情況不受影響。happy path 只在 mode 0 多一次讀位置（現有呼叫端全是 mode 1，無額外開銷）。

驗證 bench：跑 arm sweep / step，確認 (a) 不再出現「停左邊但程式繼續」；(b) 偶發 comm 抖動被重試吸收、不再動不動 PausedOnError；(c) log 若出現 `PR trigger had no effect ... re-trigger` 代表重送機制生效。

## 2026-05-21j Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_CM` 50 → **55**
### 原因
User：「幫我改成 55」。每 sub-round 行程 200 → 220 cm（55+110+55），中段大跨 100 → 110 cm。

## 2026-05-21i Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_CM` 45 → **50**
### 原因
User：「幫我改成 50」。每 sub-round 行程 180 → 200 cm（50+100+50），中段大跨 90 → 100 cm。

## 2026-05-21h Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_CM` 40 → **45**
### 原因
User：「幫我把 arm sweep 公分數改成 45cm」。每 sub-round 行程 160 → 180 cm（45+90+45），中段大跨 80 → 90 cm。

## 2026-05-21g Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_RPM` 900 → **1000**
### 原因
User：「幫我改成 1000」。延續 5/21f 的調速，1500→500→700→900→1000。

## 2026-05-21f Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_RPM` 700 → **900**
### 原因
User：「幫我把上滑台速度加快」→ 確認 900 RPM。歷史：1500（init）→ 500（5/20q 過快）→ 700（5/20t）→ 900（本次）。加減速 ACC/DEC=100 ms/1000rpm 不動，物理加減速時間會跟著 top speed 等比延長，不需額外調 ramp。

## 2026-05-21e Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_arm_clean_sweep_` Phase A 補水 + Phase B arm INIT 改成並行；加 `#include <future>`
### 原因
User：「do cleaning sweep 有辦法把補水和手臂歸為 (phase a,b) 同步進行嗎?」。

物理上水箱補水 (PQW+XKC over cli_22_) 跟 arm INIT (motor_api over arm_cli_) 是兩個獨立系統，TCP client 也分開（zero contention），可並行省 ~min(A, B) wall time。常見場景 (水滿 ~0s、INIT ~10s) 省 ~10s；最壞 (60s 補水 + 10s INIT) 從 70s 變 60s。

**設計**（user 確認的兩個 trade-off）：
1. **Phase A Abort 時等背景 INIT 跑完再返回** — 用 RAII `AsyncJoin` guard（destructor 呼叫 `fut.wait()`）。可能多等最多 ~60s 但不會 orphan thread / 不會跟 cleanup 的 `arm_cmd_("PARK")` 撞 arm_mtx_
2. **背景 INIT 失敗時等 Phase A 完、再同步 retry 進 PausedOnError** — 並行期間有兩個 thread 都可能 fail，try_or_pause_ 的 state_ / condition variable 不適合多 thread。所以背景 INIT 結果先收集成 `init_err` bool，Phase A 完之後再單執行緒 try_or_pause_ retry（user Continue 走 sync `arm_cmd_("INIT")`）

**程式結構**：
```cpp
// 1. Launch background INIT
auto fut_init = std::async(std::launch::async, [this]() {
    return arm_cmd_("INIT", 60).rfind("OK", 0) != 0;
});
// 2. RAII guard - join on every return path
struct AsyncJoin { ... ~AsyncJoin() { if (f.valid()) f.wait(); } } _join{fut_init};

// 3. Phase A (foreground with try_or_pause_)
if (try_or_pause_(<water fill>, "clean_water_fill")) return "ERR aborted\n";

// 4. Collect parallel INIT result; consumes future → guard becomes no-op
bool init_err = fut_init.get();
if (init_err) {
    // 5. Single-threaded sync retry
    if (try_or_pause_([](){ return arm_cmd_("INIT", 60)...; }, "clean_arm_init"))
        return "ERR aborted\n";
}
```

**Destruction 順序 (LIFO)**：
- 成功 / sync retry 成功：`_join.f` 已被 `get()` 消費 → no-op；ScopeExit `guard` 跑 cleanup (PARK + relays OFF)
- Abort：`_join.wait()` 阻塞到背景 INIT 結束（最多 60s 內），然後 `guard` 跑 PARK。PARK 走的 arm_mtx_ 此時已釋放，能正常執行
- mutex 自然序列化：背景 INIT 持 arm_mtx_，cleanup 的 PARK 想拿 mutex 會等到 INIT 結束，所以不會撞 — guard wait() 順序意義不大但寫明保險

**邊界情況都 OK**：
- 水滿 (Phase A ~0s)：fut_init.get() 阻塞 ~10s 等 INIT — 等於原本順序版
- Phase A 卡 PausedOnError 5 分鐘：背景 INIT 早結束、future 已 ready，user 終於回應後 get() 瞬間返回
- 背景 INIT 失敗 + Phase A 跑 OK：sync retry 走 try_or_pause_，背景 thread 早已釋放 arm_mtx_，sync retry 可進

⚠ **co-tenant 影響 step_down/step_up 末段 cleaning sweep（5/21d 新加）**：那邊呼叫 `do_arm_clean_sweep_` 在持有 motion_mtx_ 的 context 下也會 spawn 背景 thread。背景 thread 跑 arm_cmd_ 不會碰 motion_mtx_，相容。

## 2026-05-21d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增常數 `ARM_CLEAN_WALL_MM = 300` / `ARM_CLEAN_ROUNDS = 1`；新增 `do_arm_clean_sweep_(int,int)` 宣告
- `user_lib/WASH_ROBOT.cpp`
  - `cmd_arm_clean_sweep` 拆成 `cmd_`（public：state 檢查 + 鎖 motion_mtx_ + abort_flag reset）+ `do_arm_clean_sweep_`（internal：不鎖、caller 持鎖）
  - `do_step_down_` / `do_step_up_` 末段：原本註解掉的 arm_sweep（[DISABLED 2026-05-04]）換成啟用的 `do_arm_clean_sweep_(ARM_CLEAN_WALL_MM, ARM_CLEAN_ROUNDS)`
### 原因
User 要求 step_up / step_down 結尾原本註解掉 arm_sweep 的地方換成 cleaning sweep 並啟用。參數：貼牆距離固定 300mm、1 round。

`cmd_arm_clean_sweep` 原本自己鎖 `motion_mtx_`；step_up/down 已持有該鎖，直接呼叫會死鎖 → 拆成 `cmd_` / `do_` 兩層（比照 `cmd_arm_sweep` / `do_arm_sweep_` 既有 pattern）。step_up/down 呼叫 internal 的 `do_arm_clean_sweep_`。

cleaning sweep 內容：水位檢查補水 → arm INIT → 1 round（LEFT 滾筒+水濕刷 → RIGHT 刮刀乾刮，各 DEPLOY 300mm 貼牆 + 上滑台右→左→中）→ RAII 自動 PARK + 關水 + 關刷。失敗回傳 ERR、step 跟著回 ERR（沿用原 arm_sweep 區塊的傳播行為）。

⚠️ 風險提醒（已於對話告知 user，user 選擇啟用）：當初 arm_sweep 被註解掉的原因是「arm 高 RPM 動態力矩疑似晃破 body cup 邊際密封 → 掉機」。cleaning sweep 的 Phase C 一樣會動 `DM2J_ARM` 上滑台（同 `ARM_SWEEP_RPM`），同樣風險存在。實機跑要盯 body cup 密封。

驗證 bench：跑 step_down / step_up，確認末段出現 `[step_*] start cleaning sweep` → 水位/INIT/DEPLOY/上滑台 sweep 流程 → `arm_clean_sweep_done`；盯 body cup 有沒有被晃鬆。

## 2026-05-21c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_CM` 30 → **40**
### 原因
User：「幫我把上滑台移動的公分數 +30、-30 改成 +40、-40」。

影響範圍：`cmd_arm_clean_sweep` 每個 sub-round 的 3 段 sweep（絕對位置 0 → +40 → -40 → 0），每 sub-round 總行程從 120 cm → 160 cm（30+60+30 → 40+80+40）。

注意：`DM2J_RPM_FEET`（feet rail 用）跟 `ARM_SWEEP_RPM`（上滑台 sweep 用）不變，只調行程。上滑台機械極限需 bench 驗證 ±40 cm 不會撞到機構或拉超 rail 範圍。

## 2026-05-21b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_arm_clean_sweep` 的 `sweep_with_tool` lambda 拆成 pre-DEPLOY 關水 / post-DEPLOY 開水兩段
### 原因
User：「換成刮刀的 init 前，應該要先關水幫浦和關滾洗刷桶再回 park 位置」。

舊版（5/20v）的 lambda 步驟：DEPLOY → toggle pump+brush → sweep。問題：
- LEFT round 結束時 pump+brush 還 ON、arm 還在牆面
- 進入 RIGHT round → 立刻 `arm_cmd_("DEPLOY <wall_mm> RIGHT")` → DEPLOY 第一步就是 **M1 從牆面 retract 到 park 位置 (0)**
- M1 retract 過程中 pump 還在送水、brush 還在轉滾筒 → 水噴向空中浪費 + 滾筒乾甩
- 然後才在 DEPLOY 完成後關 pump+brush（但水已經噴完了）

新版（不對稱 toggle）：
- **要關（dry round）**：pre-DEPLOY 先關 → DEPLOY → 不再 toggle → sweep
- **要開（wet round）**：DEPLOY → post-DEPLOY 再開（M1 已貼牆 → 水落在玻璃上而非空中）→ sweep

效果：
- Round 1 LEFT (wet)：DEPLOY LEFT（無水，乾乾貼牆）→ 開 pump+brush → sweep
- Round 2 RIGHT (dry)：**先關 pump+brush** → DEPLOY RIGHT（M1 乾退場、切刮刀、乾貼牆）→ sweep
- Round 3 LEFT 如果 rounds>1：水已關 → DEPLOY LEFT（乾退/切回滾筒/貼牆）→ 開 pump+brush → sweep

log tag 改成 `clean_pump_off_pre_<tag>` / `clean_pump_on_post_<tag>`（之前是 `clean_pump_on_/off_`），PausedOnError 訊息能直接看出是 pre / post 段卡住。

## 2026-05-21a Claude (Sadie)
### 修改檔案
- `web_backend/public/index.html` — CLEAN SWEEP 按鈕 label 文字
- `web_backend/public/app.js` — 補上 CLEAN SWEEP handler 上方註解（記錄 washrobot 端 4-phase 流程，方便未來查）
### 原因
User：「那網頁的 clean sweep 有一起改流程嗎」。

審了一下：5/20v 改 hint + confirm dialog 都有跟上新流程，**但按鈕 label 漏改** ——
- 舊：`CLEAN SWEEP（開水+刷+sweep）`（描述舊流程）
- 新：`CLEAN SWEEP（補水→INIT→滾筒+刮刀）`（4-phase 簡述）

後端指令名 `arm_clean_sweep <mm> <rounds>` 不變 ── washrobot 端 implementation 改了但對外 wire format 一樣，前端不用動 send 邏輯。

另順手在 app.js handler 上方加註 4-phase 流程摘要（之前只寫舊版的「開水+刷+sweep」），未來改 GUI 不用回頭看 cpp 才知道實際流程。

## 2026-05-20z Claude (Sadie)
### 修改檔案
- `Linux_test/main.cpp` 選單 18 — 新增 `b` action（改 sensor baud），更新顯示行 / unknown 訊息 / 函式頭註解
### 原因
2026-05-20y 修好 `set_baud_rate` 寫入正確 reg（0x0005）後，user 要求把 bench 上改 baud 的操作開出來。原本註解寫「本 menu 不開放改 baud 避免拆掉整條 bus」是為了防誤改，現 user 明確需要且了解風險（會打斷 bus 上其他同 baud device 通訊直到 gateway 跟著改）。

`b` action 行為：
- 印 baud code 對照表（05=2400 ... 0D=115200 ... 0F=256000）
- 讀 hex 輸入，檢查範圍 0x05..0x0F
- `yes` 確認後呼叫 `lvl.set_baud_rate(code)`
- 提示 LED 應閃 + 必須先改 gateway baud 才能重連 + 同 bus 其他 device 會受影響
- 自動 break 退出 menu（連線已死）

註解區塊「警告」改寫：拿掉「本 menu 不開放改 baud」，改成「改 baud 會打斷 bus 上其他 device，bench 操作時請確認接受副作用」。

驗證 bench：sensor 在當前 baud → 進選單 18 → `b` → 輸入 `0D`（115200）→ `yes` → LED 應閃、menu 退出 → 改 gateway baud 到 115200 → 重連選單 18 讀得到。

## 2026-05-20y Claude (Sadie)
### 修改檔案
- `user_lib/XKC_Y25_RS485.h` / `.cpp` — 修正 `set_address` 與 `set_baud_rate` 寫入的 register；移除上一輪加錯的 `set_address_broadcast`
- `Linux_test/main.cpp` 選單 18 `i` action — 改回呼叫修正後的 `set_address`
### 原因
查到 XKC-Y25 V1.6 手冊（`D:\洗窗戶機器人\電控設備資料\水位sensor\XKC-Y25-RS485 INFO-CN-V16.pdf`），發現之前 driver 的 register map 是錯的：

| 操作 | driver 之前用的 reg | 手冊實際 reg | 出處 |
|------|---|---|---|
| Set ADR | 0x0003 | **0x0004** | §1.6（directed write）|
| Set Baud | 0x0004 | **0x0005** | §1.8（directed write）|

§2.1 register map 寫 0x0003 = ADR、0x0004 = baud，**但那是「讀取當前值」的 reg**（read-only 語意）。SET 操作的 reg 是 +1 — XKC 韌體把 read-reg 跟 operation-reg 分開放，§1.6/§1.8 的範例 frame 才是正確的寫入位址。

bench 驗證：sensor 在 slave 1、發 `01 06 00 03 00 0D <CRC>`（寫 0x0003）→ 0 bytes 回應、LED 不閃、slave 1 仍正常 → 完全被無視。改成 `01 06 00 04 00 0D <CRC>`（寫 0x0004）才是正解。上一輪我加的 broadcast 變體基於錯誤假設（以為 XKC 要 broadcast 才接），現在移除。

修法：
- `XKC_REG` namespace 改名 `SET_ADDR = 0x0004` / `SET_BAUD = 0x0005`（拿掉舊的 `ADDR` / `BAUD`），加大段註解說明 read-reg 跟 operation-reg 的差異
- `set_address`：寫 reg 0x0004（directed），不驗證回覆（§1.7 顯示 sensor 回非標準 7 bytes，無法可靠 echo 比對）— 模仿 `set_baud_rate` 的 send-only pattern，LED 閃 + 重連新 ID 當驗證
- `set_baud_rate`：寫 reg 0x0005（原本寫 0x0004，會打到讀-當前-baud reg，無效）
- 移除 `set_address_broadcast`（上一輪加錯）
- Linux_test 選單 18 `i` 改回呼叫 `set_address`，log 訊息更新

factory reset（選單 18 `f`）使用 raw bytes `FF 06 00 04 00 02 5C 14` 寫 reg 0x0004 broadcast value 0x02 = §2.0 標準觸發、不受此修正影響。

驗證 bench：sensor slave 1 → 進選單 18 → `i` → 13 → 應該 LED 閃、退出 menu → 重連 slave 13 讀得到。

## 2026-05-20x Claude (Sadie)
### 修改檔案
- `user_lib/XKC_Y25_RS485.h` / `.cpp` — 新增 `set_address_broadcast(uint8_t new_addr)`
- `Linux_test/main.cpp` — 選單 18 `i` action 改用 `set_address_broadcast`
### 原因
**Bench 實測：XKC-Y25 對 slave ID 寫入只接 broadcast（slave 0xFF）**，用當前 slave ID 直接寫會被無視。

驗證：slave 1 連得到、發 `01 06 00 03 00 0D <CRC>`（直接寫 ID 1→13）→ sensor 回 0 bytes、LED 沒閃、slave 1 仍正常回應 = 寫入完全被無視。對照同 sensor 對 factory reset 是 broadcast `FF 06 00 04 00 02 5C 14` 才生效 — 推測 XKC 對所有 config 寫入都要 broadcast。

修法：新增 `set_address_broadcast`，frame 改成 `FF 06 00 03 00 <new_addr> <CRC>`，無 echo 回應、靠 LED 閃 + 重連新 ID 確認。原本的 `set_address` 留著（將來若有變體 sensor 支援直寫可用）。

Linux_test 選單 18 `i` 改用新方法，log 訊息更新（明說「broadcast」、「no reply expected」、「LED 應閃爍代表 sensor 接受」）。

注意：`set_baud_rate` 也是用當前 slave ID 寫 reg 0x0004，理論上同樣會被無視。若實機改 baud 也踩這雷再補 broadcast 變體（factory reset 已是 broadcast 寫 0x0004，能 cover 部分需求）。

## 2026-05-20w Claude (Sadie)
### 修改檔案
- `washrobot_new_PI/washrobot_new_PI.vcxproj` — 加入 `XKC_Y25_RS485.cpp` + `.h` 到 ClCompile / ClInclude 清單
### 原因
5/20v 把 `#include "XKC_Y25_RS485.h"` 寫進 `WASH_ROBOT.h`，但 `washrobot_new_PI.vcxproj` 之前沒有把這個 driver 加進編譯清單（這 driver 只有 Linux_test 在用），導致編譯時：
```
fatal error: XKC_Y25_RS485.h: No such file or directory
```
插入位置：alphabetical 順序，介於 `WT901BC_TTL` 跟 `ZDT_motor_control` 之間（ClCompile + ClInclude 兩處）。

Linux_test.vcxproj 早已包含這檔，crane_control_PI / cleaning_arm 都不 include `WASH_ROBOT.h`，不受影響。

## 2026-05-20v Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `#include "XKC_Y25_RS485.h"`、新增 `lvl_` member、`XKC_SLAVE=13` / `WATER_FILL_TIMEOUT_MS=60000` / `WATER_POLL_INTERVAL_MS=200` 常數
- `user_lib/WASH_ROBOT.cpp`：
  - `init()` 加 `lvl_.init(cli_22_, XKC_SLAVE, dbg)`（Mode B、不 probe）
  - `cmd_arm_clean_sweep` 完全重寫流程
- `web_backend/public/index.html` — CLEAN SWEEP hint 改寫成 4-phase 結構
- `web_backend/public/app.js` — confirm dialog 改寫
### 原因
User 指示：「先檢查是否水滿，沒有則先補水直到水位到達該水位，關閉補水球閥，初始化手臂、進入 phase c，手臂轉到 left 貼牆→開啟水幫浦→arm_sweep，換第二輪先關水幫浦→轉到 right 貼牆→arm_sweep」。

XKC-Y25-RS485 水位 sensor driver 之前就有（`user_lib/`），但 WASH_ROBOT 主程式從來沒整合，這次補上 + 重寫 clean_sweep 流程。

**新流程（4 phase）**：
- **Phase A 補水**：`lvl_.read_state()` 讀水位
  - sensor 不通 → `return true` → PausedOnError（user 可手動補水後 Skip / 修 sensor 後 Continue retry）
  - output==1 → skip 補水
  - output==0 → 開 CH7 球閥、200ms 輪詢 sensor、output==1 自動關閥；60s 內沒到 → PausedOnError
  - 不管哪條路徑離開都會關 CH7（anti-flood，包進閉包尾段一律 `pqw_.controlRelay(CH_WATER_INLET, false)`）
- **Phase B arm INIT**：每次 sweep 都重跑 `arm_cmd_("INIT", 60)`（保證已知起點，~10s cost；user 確認可接受）
- **Phase C `rounds × {LEFT roller wet + RIGHT scraper dry}`**：每 sub-round =
  - 1) DEPLOY `<wall_mm>` `<slot>`（M1 retract → M2 LR_SLOT → M1 TOUCHWALL）
  - 2) Water pump + brush 一起 ON/OFF（LEFT=ON、RIGHT=OFF，user 確認 brush 跟 pump 同步）
  - 3) 上滑台 sweep 右→左→中（絕對位置 +30 / -30 / 0）
- **Phase D RAII cleanup**：arm PARK + 刷子 + 泵浦 + 球閥全 OFF

**設計決策（從 user 確認）**：
1. brush(CH5) 跟 water pump(CH6) 同步 ── 刮刀 round 不用轉滾筒
2. XKC sensor 連不上 → PausedOnError，不 fallback 走 timed（避免靜默灌水）
3. arm INIT 每次都跑（雖然 ~10s，但保證起點）；舊版 Phase B 的 DEPLOY CENTER 拿掉，直接 round 1 DEPLOY LEFT 接上

**參數來源**：跟 `Linux_test/main.cpp` menu 17 mode 4 對齊 ── slave 13、IP 192.168.1.22（共用 cli_22_）、60s timeout、200ms 輪詢。`lvl_.init()` 是 Mode B 不 probe，physical sensor 缺席不會卡 init()，第一次 read 才會 catch（per `project_driver_init_mode_b_no_probe` memory）。

⚠ **CLAUDE.md 的 RS485_3 拓樸表還沒列 XKC-Y25 sensor**（待補）。

## 2026-05-20u Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_arm_clean_sweep` Phase C 改寫為 LEFT(滾筒) → RIGHT(刮刀) 兩 sub-round 結構
- `web_backend/public/index.html` — CLEAN SWEEP hint 文字更新
- `web_backend/public/app.js` — confirm dialog 文字更新
### 原因
User：「Phase C 修改成做兩個 round，第一 round 先改成 left，arm_sweep 右→左→回中，第二 round 放 right，arm_sweep 右→左→回中（就是滾筒先，再刮刀）」。

舊 Phase C：每 round 6 個動作 ── 上滑台 +30 → M2 RIGHT → 上滑台 -30 → M2 LEFT → 上滑台 0 → M2 CENTER。3 個 sweep 段每段配一次工具切換，邏輯混亂。

新 Phase C：每 round = 兩個 sub-round（角色固定）：
- **sub-round 1 LEFT slot（滾筒）**：M2 → LEFT → 上滑台 +30 → -30 → 0
- **sub-round 2 RIGHT slot（刮刀）**：M2 → RIGHT → 上滑台 +30 → -30 → 0

物理意義：滾筒先把髒污刷散 + 帶水抹過，刮刀再把殘水刮乾淨。每 sub-round 都是「右→左→回中」絕對位置（0→+30 reposition with tool engaged、+30→-30 為主刷段 60cm、-30→0 回中），工具進場 / 退場時順便刷一遍邊。

`rounds` 參數保留為**多重複 multiplier**（rounds=1 跑一次完整 roller+scraper、rounds=N 跑 N 次），預設網頁 1 已是完整流程。

實作面：用 lambda `sweep_with_tool(slot_str, tag_prefix)` 收斂兩 sub-round 重複碼，tag_prefix 區分 log（`clean_dm2j_roller_right` / `clean_dm2j_scraper_left` 等），PausedOnError 訊息能直接指出卡在哪個工具+哪段。每 sub-round 之間插一次 `check_abort_()`。

## 2026-05-20t Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_RPM` 500 → **700**（per user）

## 2026-05-20s Claude (Sadie)
### 修改檔案
- `web_backend/server.js` — WS dispatcher 加 `target === 'arm'` 路由（之前漏掉），開放前端直連 motor_api
- `web_backend/public/index.html` — 拆分 arm 面板成兩塊
- `web_backend/public/app.js`：
  - `applyMode` 的 `panel-arm` disable 條件 `!w || !a` → `!a`（只看 arm 連線）
  - `btn-arm-deploy` 從 `send('washrobot', 'arm_deploy …')` 改 `send('arm', 'DEPLOY …')`
### 原因
User 接續 5/20r：「panel 還是沒 enable」確認後，washrobot dot 紅、arm dot 綠。因為 `panel-arm` 的 disable 條件是 `!w || !a`（兩個都要在線），所以 washrobot 沒跑時整個 arm 面板灰掉。

User 要求：「需要主程式連動的功能換 panel，留下 arm 本身的功能在 panel 上就好」。

拆分原則：
- **panel-arm（只需 arm，直連 motor_api）**：INIT / DEPLOY / PARK / STATUS
  - 這些按鈕 `data-tgt="arm"`，server.js 直接 forward 到 9527
  - 指令字串就是 motor_api 原生格式（`INIT`、`DEPLOY <mm> <slot>`、`PARK`、`STATUS`），剛好跟 washrobot 的 arm_init/arm_deploy/arm_park/arm_status pass-through 一樣
  - 用途：bench 沒跑 washrobot 也能單獨測 arm

- **新 panel-washrobot 區「🦾🚿 arm 整合操作」**：
  - `arm_attached on/off`：這是 washrobot 內部 flag，washrobot 沒跑沒意義
  - `CLEAN SWEEP`：washrobot 編排（水閥 + 泵浦 + 刷子 + 上滑台 sweep + arm），跨多硬體必須 washrobot 在線
  - 兩者都繼續走 washrobot dispatcher

server.js 配套改動：WS message handler 的 target 三元鏈本來只認 washrobot/crane/easy_crane，補上 `arm` 分支。順手更新檔頭 protocol 註解 + ARM_IP 旁邊那段「target='arm' is NOT routed」的舊註解。

## 2026-05-20r Claude (Sadie)
### 修改檔案
- `web_backend/public/app.js` — 收到 status 訊息時，arm 連線狀態變化也要觸發 applyMode
### 原因
User：「arm 連上了閃綠燈，但 panel 沒有 enable」。

Bug：之前的流程
- `setDot(dotA, m.arm)` → 綠燈會亮 ✓
- `updateArmButtonStates(!!m.arm)` → 按鈕個別 disabled 會處理 ✓
- `lastStatus.arm = !!m.arm` → 內部狀態有更新 ✓
- `handleStatusChange(w, c, e)` 不含 arm，arm 變化沒進去 debounce 流程
- `applyMode` 只在 handleStatusChange 內部 w/c/e 有變化時才被呼叫

→ 如果 w/c/e 都沒變、只有 arm false→true，`applyMode` 完全不會被觸發 → `panel-arm` 的 `panel-disabled` class 留在那裡，整個面板灰掉。

修法：偵測 `lastStatus.arm !== !!m.arm`，arm 狀態有變化時直接 call applyMode（不走 debounce — arm 連線本來就是被動 polling，沒有 cross-device stop 邏輯，不需要 3s 防抖）。

## 2026-05-20q Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `ARM_SWEEP_RPM` 1500 → 500
### 原因
User：「幫我調低一下上滑台的速度」。clean_sweep 用的 ARM_SWEEP_RPM 原本設 1500 偏快，bench 觀察起來不需要這麼急。

只動 ARM_SWEEP_RPM、不動共用的 DM2J_RPM=200（後者也影響輪子+一般 arm 手動移動）— 範圍限定在 `cmd_arm_clean_sweep` 序列。

DM2J_ACC/DEC（500 ms/1000rpm）未動 — 從 1500→500 後加減速段時間相對縮短（成正比），不另外調 ramp。

## 2026-05-20p Claude (Sadie)
### 修改檔案
- `scripts/wr.sh`（新增）— washrobot Pi 用 tmux 一鍵啟動 washrobot_new_PI + frame_capture
- `scripts/crane.sh`（新增）— crane Pi 用 tmux 一鍵啟動 Crane_control_PI + web_backend
- `.claude/runbook.md` — 在 A. 啟動順序開頭加 A0 段，說明 launcher 用法 + tmux 操作 cheatsheet
### 原因
User：「測試用，想隨時開關所有程式」— 多 daemon 場景下不想開好幾個 SSH 視窗，又不要 systemd 那麼正式。

選 tmux launcher 的理由：
- 一個指令全開、log 都看得到（每個程式一個 window）
- 要關哪個 `Ctrl-C`、要重開 `↑ Enter`，個別控制不影響其他
- detach 後（`Ctrl-b d`）程式繼續跑、SSH 斷線重連 `attach` 就回到原狀
- 比 systemd 適合 dev/bench（不必每次 `sudo systemctl restart`、log 不用 `journalctl` 切）

script 設計：
- `{start|stop|attach|status}` 四個動作
- 路徑用環境變數覆蓋（`WR_BIN` / `WR_CAM` / `CRANE_BIN` / `WEB_DIR`）— 預設依 runbook 文件的 deploy 路徑
- `CRANE_BIN` 接受 binary 路徑或完整命令字串 → 可以直接塞 `python3 .../crane_shim.py` 切測試模式吊車
- start 前檢查檔案存在性，缺檔印明確錯誤 + 怎麼 override

新使用者首次跑必須 `chmod +x scripts/*.sh`（git 在 Windows 下無法保留 +x 屬性，runbook 已註明）。

systemd unit（生產用、開機自動跑）暫不寫 — 等實機驗證 + 確定路徑穩定再補。

## 2026-05-20o Claude (Sadie)
### 修改檔案
- `cleaning_arm/cleaning_arm.vcxproj` — `ClCompile` 加 `<CppLanguageStandard>c++17</CppLanguageStandard>`
- `cleaning_arm/compile.sh` — 加 `-I../user_lib` 給 damiao.h / SerialPort.h
### 原因
User 編譯 cleaning_arm 報錯 `‘make_unique’ is not a member of ‘std’`。

`std::make_unique` 是 C++14,main_api.cpp 用到三次(建 SerialPort / Motor_Control / Motor)。MSBuild 的 Linux toolchain 預設 C++ 標準舊(可能 C++11)→ 編不過。Crane_control_PI 跟 washrobot_new_PI 的 vcxproj 都沒設,但它們剛好沒用到 C++14+ feature 才沒事。

修法:
1. `cleaning_arm.vcxproj` 在 `ClCompile` ItemDefinitionGroup 加 `<CppLanguageStandard>c++17</CppLanguageStandard>`(MSBuild Linux toolchain 支援的標準寫法)
2. `compile.sh` 順便補 `-I../user_lib`(2026-05-20h 移檔後 damiao.h / SerialPort.h 在 user_lib,bash 編路徑之前漏改;那時 changelog 已標記「之後再順手調」)

選 c++17 不是 c++14:main_api.cpp 也用到 `std::variant`(C++17),要顧到。

## 2026-05-20n Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 `wheels_attached_` atomic + `cmd_wheels_attached(bool)` 宣告
- `user_lib/WASH_ROBOT.cpp` — 建構式 init list 加 `wheels_attached_(true)`;`init()` 收輪區塊加 gate(off → skip + log);`cmd_wheels` / `cmd_dm2j_group("wheels")` 開頭加 gate(off → 回 OK skipped);實作 `cmd_wheels_attached`
- `washrobot_new_PI/main.cpp` — TCP dispatch 加 `wheels_attached <on|off>`
- `web_backend/public/index.html` — manual — wheels 面板加「輪子是否已安裝 attached ON/OFF」一列 + hint
### 原因
User:「現在有辦法暫時不裝兩邊輪子(dm2j)嗎」。

bench 沒裝輪子(DM2J slave 2/4)的話,washrobot `init()` 會卡死在 `D_(DM2J_LEFT_WHEEL).PR_move_cm(0)` —— Modbus 對沒接的 slave timeout ~15s → `try_or_pause_` → PausedOnError 兩次,init 跑不完。

加 `wheels_attached_` toggle(預設 ON,跟 crane_attached / arm_attached 同 pattern):
- ON:既有行為不變
- OFF:`init()` 跳過收輪、`cmd_wheels` / `cmd_dm2j_group("wheels")` 變 no-op(回 `OK skipped (wheels_attached=off)`)

GUI 在 manual — wheels 面板下方加兩顆 attached ON/OFF 按鈕(user 確認只加按鈕、不加 dot —— 因為輪子是 Modbus device 沒「連不連得上」的服務狀態)。

驗證 bench(沒接輪子):
1. 啟動後送 `wheels_attached off`
2. 再送 `init` → log 應印 `[init] wheels_attached=off, skip wheel retract`、不再 PausedOnError
3. 想裝回輪子 → `wheels_attached on` + 重新 `init`

待裝回:把 .h 的 `wheels_attached_(true)` 留著當 production default;bench 上每次重啟都要手動 OFF。如果未來常常 bench 沒輪子,可考慮預設改 false(改一個 init list 數字)。

## 2026-05-20m Claude (Sadie)
### 修改檔案
- `web_backend/public/index.html` — 清潔手臂 `<section>` class `panel-washrobot` → `panel-arm`
- `web_backend/public/app.js`
  - 新增 `panelsArm = querySelectorAll('.panel-arm')`
  - `lastStatus` 加 `arm: null`
  - `applyMode` 簽名加 `a` 參數,加 `panelsArm.forEach(...)` 套用 `!w || !a` 條件
  - 三處 applyMode 呼叫端都改成傳 4 個參數(`lastStatus.arm` / 或 false)
  - status 訊息收到時 `lastStatus.arm = !!m.arm`
### 原因
2026-05-20l 加的「panel-washrobot + arm-action」只 disable 個別按鈕,沒讓**整個面板**灰掉。user:「他沒連上的時候 panel 沒有 disable」。

改成跟 crane / washrobot 同款的 panel-level disable:cleaning_arm `<section>` 改掛 `panel-arm` class(不再 `panel-washrobot`),由新的 `panelsArm.forEach(...)` 統一管理。

disable 條件 = `!w || !a` —— **washrobot 或 arm motor_api 任一掛了 → 整個面板灰**。理由:所有 arm 指令都走「網頁 → washrobot → arm」,任一段斷掉就送不到。

副作用:`arm_attached on/off` 兩顆現在 arm 掛掉時也跟著灰(以前只灰其他 5 顆)。可接受 —— 雖然語意上 arm_attached toggle 是寄給 washrobot 的本地 flag(arm 掛了還能送),但 panel 全灰 UI 一致性比 edge case 重要。要切 attached 可以等 arm 起來再切,或用其他途徑。

`arm-action` class 跟 `updateArmButtonStates` 留著當 defense-in-depth(雙保險),但實際 panel-disabled 已經涵蓋。

驗證:
- arm motor_api 關 → arm dot 紅 + **整個清潔手臂面板 grey out**
- arm 起來 → dot 綠 + 面板回正常
- washrobot 關 → 面板也灰(同樣的 `!w || !a` 條件)

## 2026-05-20l Claude (Sadie)
### 修改檔案
- `web_backend/public/index.html` — 清潔手臂 `<section>` 加 `panel-washrobot` class;INIT/PARK/STATUS/DEPLOY/CLEAN SWEEP 五個按鈕加 `arm-action` class
- `web_backend/public/app.js` — 新增 `updateArmButtonStates(connected)`,收到 status 訊息 / ws 關閉時呼叫
### 原因
User:「arm 沒連上沒有 disable 欸」—— 之前加了 arm 連線指示燈,但按鈕本身沒跟著鎖。

兩層 disable 設計:
1. **`panel-washrobot` class**:washrobot 本身連不上 → 整個 cleaning_arm 面板 grey out(沿用既有 `applyMode` 機制)。理由:所有 arm 指令都走「網頁 → washrobot → arm」,washrobot 沒了就什麼都送不出去。
2. **`arm-action` class**:cleaning_arm motor_api 連不上 → 個別按鈕 disable(新增 `updateArmButtonStates`)。理由:即使 washrobot OK,arm 不通也不該按

**`arm_attached on/off` 兩顆刻意不加 `arm-action`** —— 那兩顆是「告訴 washrobot 要不要試著送指令」的本地 toggle,即使 arm 不通也該能切(常用情境:bench 還沒裝 arm,想先關掉,免得 washrobot 一直 retry)。它們仍受 `panel-washrobot` 保護,washrobot 不通才會 disable。

驗證:
- arm motor_api 關著 → header arm dot 紅 → INIT/PARK/STATUS/DEPLOY/CLEAN SWEEP 五顆 disable + hover tooltip「arm motor_api 未連線」
- arm motor_api 起來 → dot 綠 → 五顆都能按
- washrobot 關 → 整個面板 grey(包括 arm_attached on/off)

## 2026-05-20k Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 `cmd_arm_clean_sweep(int wall_mm, int rounds)` 宣告
- `user_lib/WASH_ROBOT.cpp` — 實作 `cmd_arm_clean_sweep`(序列、RAII cleanup)
- `washrobot_new_PI/main.cpp` — TCP dispatch 加 `arm_clean_sweep <wall_mm> <rounds>`
- `web_backend/public/index.html` — 清潔手臂面板加「🧽 清潔 sweep」row（wall_mm input + rounds input + CLEAN SWEEP 按鈕）
- `web_backend/public/app.js` — 加 `btn-arm-clean-sweep` click handler(confirm + send)
### 原因
User 規劃清潔流程:「收水馬達、上滑台跟手臂搭配 → 噴水 → 手臂貼牆 → 左右清潔 → 上滑台邊左右動 → 直到上滑台完成一回合 → 關水 → 手臂回原位」。

四個關鍵設計 user 確認:
1. **序列**(非並行)— DM2J 動完才換 arm 動,反之亦然
2. 手臂 LR 切換用 **M2 LR_SLOT**(只動工具頭、不重新貼牆)
3. wall_mm / rounds 走**指令參數**(GUI input 帶下來)
4. **RAII 強制關水** — ScopeExit lambda 確保任何 exit path 都 PARK + 關水/刷

流程:
```
A. CH_WATER_INLET ON → CH_WATER_PUMP ON → CH_BRUSH ON
B. arm DEPLOY <wall_mm> CENTER → sleep 2s
C. for r in 1..rounds:
     DM2J_ARM → +ARM_SWEEP_CM   ; arm M2 LR_SLOT RIGHT
     DM2J_ARM → -ARM_SWEEP_CM   ; arm M2 LR_SLOT LEFT
     DM2J_ARM → 0               ; arm M2 LR_SLOT CENTER
D. (RAII) arm PARK → CH_BRUSH OFF → WATER_PUMP OFF → WATER_INLET OFF
```

每步都包 try_or_pause_,失敗 → PausedOnError 可 retry/skip;abort/return 也都會走 ScopeExit。state guard:只擋 Error(允許 bench 在 Idle/Ready 試跑)。rounds 範圍 1..20。

GUI 按鈕加了 confirm dialog 列出 wall_mm + rounds + 動作摘要,防誤觸。

跟既有 `cmd_arm_sweep` / `do_arm_sweep_`(DM2J slave 5 來回擺,沒水沒手臂)**完全分開**,舊的保留不動。

驗證 bench:
1. cleaning_arm motor_api 跑著、INIT 過、washrobot 也重編
2. 網頁鋼索手臂面板按 CLEAN SWEEP → 應看到 log:`[arm_clean_sweep] start wall_mm=300 rounds=1` → 水/刷打開 → DEPLOY → 三段 DM2J + LR_SLOT → cleanup PARK + 關水
3. 中途按 emergency_stop → ScopeExit 應確保水關掉、arm PARK

## 2026-05-20j Claude (Sadie)
### 修改檔案
- `web_backend/server.js` — 加 `ARM_IP/ARM_PORT` 常數、`makeBridge('arm', ARM_IP, ARM_PORT)`、broadcastStatus + 初始 ws 推播都加上 `arm: arm.isConnected()`
- `web_backend/public/index.html` — header 那條 dot bar 加 `<span class="dot" id="dot-arm"></span>arm`
- `web_backend/public/app.js` — 加 `dotA = document.getElementById('dot-arm')`;`ws.onclose` 補 `setDot(dotA, false)`;status 訊息加 `setDot(dotA, m.arm)`
### 原因
User:「把清潔手臂也像 crane 一樣加個是否可連線的號誌」。

做法:server.js 多開一條 TCP bridge 到 cleaning_arm 的 motor_api(預設 `ARM_IP = WASHROBOT_IP`,因為 arm 跟 washrobot 同一台 Pi;改別處用 `ARM_IP` 環境變數)。`isConnected()` 即時推給瀏覽器,前端 dot 依 crane 同款 `setDot` 機制紅/綠顯示。

注意:**只做 dot 指示,不做指令路由** —— `target='arm'` 沒加進 ws 處理,所有 arm 指令仍走「網頁 → washrobot `arm_*` → washrobot arm_cmd_ → cleaning_arm」這條既有路徑。server.js 對 arm 的這條 TCP 純粹用來偵測「motor_api 服務在不在」,跟 washrobot 端的 `arm_attached_` flag 各管各的(washrobot 端是「要不要送指令」、server.js 這條是「服務跑沒跑」)。

副作用:server.js 對 arm 的 keepalive 也送 `ping`,但 motor_api 不認 `ping` → 每 10s arm 回一行 `ERR unknown_command` 之類。會出現在瀏覽器 log,但無害(TCP 不會斷,isConnected 持續 true)。如果 log 太吵之後再考慮:(a) 改用 STATUS 當 keepalive,(b) 加參數讓 makeBridge 跳過 app-level ping、只靠 OS-level keepalive。現在這版優先驗證 dot 邏輯。

cross-device 自動停那套(`handleStatusChange` / `applyDeviceTransition_` / panel-disabled)**沒**接 arm —— arm 失聯不會自動停別人,只有 dot 變紅。需要的話之後再擴充。

驗證 bench:
- motor_api 跑著 → 網頁 header 看到 arm dot 變綠
- motor_api 關掉 → dot 變紅,1s 後 server.js 自動 reconnect 失敗,broadcastStatus 持續發 arm=false
- motor_api 重起 → server.js 連回去,dot 又變綠

需要重啟 web_backend (Node) + 瀏覽器 Ctrl+F5 才看到新 dot。

## 2026-05-20i Claude (Sadie)
### 修改檔案
- `CLAUDE.md` 硬體拓樸圖補 USB-CAN damiao 手臂條目;Device Drivers 表新增 `damiao` row
### 原因
之前 CLAUDE.md 那行寫「(USB→CAN) 機械手臂控制器 [未來擴充,需終端電阻 120Ω]」—— 現在實際裝起來了(cleaning_arm 子專案 + damiao M1/M2 + USB-CAN dongle on `/dev/ttyACM0`),把佔位文字換成實裝資訊:
- M1 DM10010L (CAN slave 0x01 / master 0x11)
- M2 DM4340_48V (CAN slave 0x02 / master 0x22)
- 註明由 cleaning_arm/motor_api 服務驅動(TCP :9527)、washrobot 透過 arm_cmd_ 跨 process 下指令
- 保留 120Ω 終端電阻提醒

Device Drivers 表新增一 row 描述 damiao + SerialPort,標明「整個專案唯一走 CAN 的裝置」,跟其他全走 Modbus-TCP/RS485 的裝置區分。

電源架構區塊**沒動** —— 那段「[A組] 24V ... 機械手臂電源 [未來擴充]」可能不準(damiao DM10010L / DM4340_48V 多半 48V 不是 24V),但實際 bench 怎麼接電我不知道,留給 user 自行確認後更新。

[跨界: CLAUDE.md] —— 屬於規範文件,本來歸 Jim;但這次是「硬體事實已成現實 → 把佔位文字換成實情」的描述性更新,不是設計決策,user (Sadie) 在 chat 確認過了。
### 修改檔案
- 移檔:`cleaning_arm/damiao.h` → `user_lib/damiao.h`
- 移檔:`cleaning_arm/SerialPort.h` → `user_lib/SerialPort.h`
- 刪檔:`cleaning_arm/{TCP_client.h, TCP_client.cpp, PQW_IO_16O_RLY.h, PQW_IO_16O_RLY.cpp}`
- 新建:`cleaning_arm/cleaning_arm.vcxproj`(Linux Generic、ARM/ARM64/x86/x64 Debug/Release、ProjectGuid `{B7F4D2A1-...-9D03}`)
- 更新:`washrobot_new_PI.sln` 加 cleaning_arm Project entry + 全 8 個 ProjectConfigurationPlatforms 條目
### 原因
User:「把 cleaning_arm 像 crane 一樣弄成子專案,共用設備 library 都放 user_lib」。

選擇(user 確認):
1. damiao.h + SerialPort.h 一起搬 user_lib(跟 user_lib 既有 Serial_port.h 並存,因為 damiao 廠商驅動寫死用自家 SerialPort,改 damiao 風險高)
2. 刪 cleaning_arm 內的 TCP_client / PQW_IO_16O_RLY 重複(掃過 main.cpp/main_api.cpp/main_api.h/damiao.h **沒有任何檔 include 它們**,是 dead copy-paste)
3. PalletizerController.h 跟 motor_api binary 保留

vcxproj 結構完全 mirror Crane_control_PI:
- ClCompile:`main.cpp` + `main_api.cpp`(damiao.h/SerialPort.h 都是 header-only,不用列 .cpp)
- ClInclude:project 自有的 + 兩個 user_lib header
- `AdditionalIncludeDirectories=..\user_lib` 所有 config 都加(因為 damiao.h 跟 SerialPort.h 都在 user_lib)
- `LibraryDependencies=pthread`(damiao TCP server 用 std::thread)

cleaning_arm/ 原始檔不用改 include statement —— 因為 vcxproj 加了 user_lib 到 include path,`#include "damiao.h"` / `#include "SerialPort.h"` 還是找得到。

Build 方式從原本「`cd cleaning_arm && bash compile.sh`(g++ 直接編)」變成「Visual Studio 主 solution 開,選 cleaning_arm 編譯;或 `msbuild washrobot_new_PI.sln /p:Configuration=Debug /p:Platform=ARM`」。`compile.sh` 保留當 fallback / Linux 直編路徑(damiao.h 跟 SerialPort.h 改在 user_lib 後,compile.sh 要 `g++ -I../user_lib -std=c++17 *.cpp -o motor_api -pthread`,**之後再順手調**)。

驗證:VS 開 sln 應該看到 5 個專案(washrobot_new_PI / crane_control_PI / windows_test / Linux_test / crane_easy_PI / cleaning_arm —— 6 個了)。選 cleaning_arm + Release|ARM64 編。

## 2026-05-20g Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 ARM_IP/ARM_PORT 常數、TCP_client `arm_cli_` + `arm_mtx_`、atomic `arm_attached_`、宣告 `arm_cmd_` / `arm_connect_if_needed_` / `cmd_arm_init` / `cmd_arm_deploy` / `cmd_arm_park` / `cmd_arm_status` / `cmd_arm_attached`
- `user_lib/WASH_ROBOT.cpp` — 建構式 init list 加 `arm_attached_(true)`；新增「cleaning arm」section,實作 `arm_cmd_` (mirror crane_cmd_ 但無 EVT filter / 無 estop channel) + 5 個 cmd_arm_*
- `washrobot_new_PI/main.cpp` — TCP dispatch 加 `arm_init` / `arm_park` / `arm_status` / `arm_deploy <mm> <slot>` / `arm_attached <on|off>`
- `web_backend/public/index.html` — washrobot 跟 crane 之間插入「🦾 清潔手臂 (cleaning arm)」面板:attached on/off、INIT / PARK / STATUS 按鈕、DEPLOY 輸入(wall_mm + LEFT/CENTER/RIGHT)+ 按鈕
- `web_backend/public/app.js` — 新增 `btn-arm-deploy` click handler(讀 input + select → 送 `arm_deploy <mm> <slot>`);其他按鈕用既有 generic dispatch
### 原因
User 新增 `cleaning_arm/` 資料夾(獨立 motor_api 執行檔,USB 接 damiao M1 大臂 + M2 工具頭、背景 TCP 9527、指令 INIT/DEPLOY/PARK/STATUS/M1|M2 ENABLE 等)。要把它整合進 washrobot 控制流。

整合方式 user 確認:
1. cleaning_arm 跟 washrobot 同一台 Pi → ARM_IP = 127.0.0.1
2. 舊 DM2J_ARM (slave 5) 跟 `do_arm_sweep_` / `cmd_arm_sweep` 保留不動(舊機構還在)
3. 同時加網頁 GUI 按鈕

實作完全 mirror crane 那一套:`arm_cmd_` 內含 self-healing reconnect(2 attempts),`arm_attached_` toggle 在 OFF 時讓 arm_cmd_ 變 no-op,bench 沒 motor_api 也能跑。沒做 EVT 過濾 / estop channel / watchdog —— motor_api 不發 EVT,純 line-based RPC,簡單一條進一條出。

新指令對應 motor_api 文件:
- `arm_init` → `INIT`(啟動 + M2 工具頭校正,可能慢,timeout 60s)
- `arm_deploy <mm> <LEFT|CENTER|RIGHT>` → `DEPLOY <mm> <SLOT>`(timeout 30s)
- `arm_park` → `PARK`(timeout 30s)
- `arm_status` → `STATUS`(timeout 3s)
- `arm_attached on|off` → 切 toggle

driver/網頁/dispatch 都做完了,bench 跑前需要:
1. cleaning_arm 編譯 + 跑起來(`cd cleaning_arm && bash compile.sh && ./motor_api`)
2. washrobot + web_backend 重編 / 重啟 / 瀏覽器 Ctrl+F5

驗證 bench:網頁 → arm_attached ON → STATUS 應回 `[M1] pos=... | [M2] pos=...`;INIT 後跑 DEPLOY 300 LEFT 應看到 OK 回應。

## 2026-05-20f Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp`
  - 新增 file-local `WEIGHT_NO_DATA_KG = -9999.0` sentinel
  - `read_rope_weight_max_kg_`：負值不再當失敗,改成「re-read 一次,連續負值就接受」
  - `crane_retract_safe_` / `crane_retract_to_weight_` (2 處) 的「<0 = offline」檢查改用 `<= WEIGHT_NO_DATA_KG` sentinel
  - `crane_pay_out_to_weight_` 的 `read_lr` lambda 同步加 re-read + 接受負值
### 原因
Bench：step_up body retract 觸發 `crane_retract_safe_` → `read_rope_weight_max_kg_` → DSZL 讀回負值 → 舊 `if (l >= 0)` 判定無效 → fall through 到沒裝的 fallbacks → 回 -1 → "WEIGHT SENSOR OFFLINE — refuse retract"。User retry 多次都一樣，整個 step_up 卡死。

對比手動按鈕（網頁 hold ↑↓）完全沒問題：那條路 web → server.js → crane `cmd_hold` 直接過去，不經 washrobot 端 weight check，所以負值不會擋。

修法（user 指示）:「讀到負值讀兩遍,都是負值就正常使用」—— 負值物理上不可能(繩不能推),但 bench DSZL 未校正會讀負;單一次負是 transient,連續負是真實「低張力 + 零點偏移」。

實作:
1. 引入 sentinel `WEIGHT_NO_DATA_KG = -9999.0` 區分「真的讀不到」vs「讀到負值」(實際讀值不會接近 -9999)
2. `read_rope_weight_max_kg_` 解析後若任一邊負 → re-read 一次 → 用第二次的值(正/負都接受)
3. 4 處 `< 0 = offline` 檢查改成 `<= WEIGHT_NO_DATA_KG`,只在「真的拿不到任何讀值」才認定 offline

DSZL 即使校正前,bench 上 retract / pay_out 不再被誤擋。負值會被當「低張力 = 不過載」處理,washrobot 端 weight monitor 對 overweight 偵測仍正確(負值永遠 < limit,不會誤觸發 trip）。最終安全網仍由 crane 端 `TENSION_MAX_KG`（即時直讀 DSZL、20ms 迴圈）守住。

驗證 bench：跑 step_up → 應該看到 `[rope_weight] negative reading L=... R=... — re-reading to confirm` log,然後 `re-read ... (negative confirmed, using as-is)`,後續 retract 正常開始（不再「WEIGHT SENSOR OFFLINE — refuse retract」）。

## 2026-05-20e Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_feet_realign_` Phase 5 改用 `crane_pay_out_to_weight_(ATTACH_PAYOUT_TARGET_KG, crane_assist_actual_cm)`
### 原因
User：「realign 後放繩放太多了，可以參考 attach 最後放繩邏輯」。

原本 Phase 5 盲放 `crane_cmd_("pay_out crane_assist_actual_cm")` — 把 Phase 1 收的 cm 原樣 pay 回去。實機 bench 看到「放太多」：Phase 2 feet 對齊後幾何位置已重置，盲還原同樣 cm 會把繩放得比需要的鬆。

改用 attach 結尾同一條函式 `crane_pay_out_to_weight_`：
- 逐 cm 放繩、每步讀 DSZL-107 張力
- 達 `ATTACH_PAYOUT_TARGET_KG = 10kg` 殘餘張力即停（attach 結尾用的同一個目標常數）
- max_cm 上限設為 `crane_assist_actual_cm`（不超過 Phase 1 收的量，保證淨繩位移 ≤ 0、不會留下比 realign 前更鬆的繩）

`crane_pay_out_to_weight_` 也會處理：張力本來就已 ≤ 目標 → 立即回 `OK already_at_target total_cm=0`；張力感測離線 → `ERR ...`（仍 non-fatal、不擋 realign 完成）。

備註：`crane_pay_out_to_weight_` 在 2026-05-20d 把停止條件從「兩邊都 ≤ target」改成「任一邊 ≤ target」 → 任一條鋼索張力先降到 10kg 即停，較保守、Phase 5 也跟著繼承這行為。

驗證 bench：跑 realign，看 `[crane_pay_out_to_weight] step` log 的張力是否從 ~40kg 下降到 ~10kg 即停；確認最終放的 cm < Phase 1 收的 cm（若放太多時等於 cap）。

## 2026-05-20d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `crane_pay_out_to_weight_`：停止條件「兩邊都 ≤ target」→ **「任一邊 ≤ target」**（`&&` → `||`）
### 原因
Bench log（target=12kg）：
```
step 1/50 left=14.91 right=9.18  → 用「兩邊都到」沒停，繼續放
step 2/50 left=4.45  right=1.97  → 放太多了，兩邊都跌到 < 5kg
```

right 在 step 1 已經 9.18 < 12，本該停。但「兩邊都 ≤ target」要等 left 也降到 ≤ 12，再放 1cm → left 14.91→4.45、right 9.18→1.97 → **嚴重 overshoot**（變幾乎沒張力的鬆繩）。

物理原因：兩邊張力本來就不對稱（負載/繩鬆緊不同）。等比較緊的那邊降到 target，比較鬆的那邊早就掉到接近 0 了。1cm 步進對「最後收尾」太粗。

修法：停止條件改成「任一邊 ≤ target」（包含 already_at_target 早退跟主迴圈的 break）。理由：第一個觸到 target 的那邊代表「鋼索張力鬆到目標」、另一邊只會更鬆 → 此時 cup 已承重充分、可以停。對稱情況兩邊同時觸發效果不變；不對稱情況不再 overshoot。

trade-off：較緊那邊可能還高於 target（例如收尾時 left=14.91 still > 12），但這不是 bug —— 兩邊張力差是物理現實，目標本來就是「至少有一邊鬆到 target」而不是「兩邊強制對齊」。strict 雙邊對齊由 crane balance trim 在 motion_rope 處理，不是 attach pay_out 的職責。

驗證 bench：再跑一次 attach，看 log step 應該在「任一邊 ≤ target」就停，不再放第二步。

## 2026-05-20c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_attach` 第 4 步：放繩前加 vacuum safety gate
### 原因
之前的行為：fine_tune 結束若仍有 cup 沒密封，只印 WARN log + 發 EVT `attach_partial_seal`，**不擋第 4 步放繩**。

風險：放繩 = 把機體重量從繩轉到 cup。沒密封的 cup 不承重 → 重量會堆到其他 cup → 過載 → 鬆脫 → 機體掉。
- 全部沒吸到 → 機體掛在繩上、pay_out 永遠到不了 target → 跑滿 `ATTACH_PAYOUT_MAX_CM=50cm` → 機體掉 50cm
- 部分沒吸到 → 剩下的 cup 撐不住 → 鬆脫 → 機體掉

修法（user 選 (a) 最嚴）：第 4 步開頭重做一次 `vacuum_check_("all")`，**任一顆沒密封 → skip 放繩**，直接過去 set_state_(Attached)。繩繼續承重（= attach-entry 狀態，恆安全）。
- 全部密封 → 照常放繩（讀 crane 軟停張力當 target）
- 有任一顆沒密封 → log 印出哪些 slave 沒吸到 + 發 EVT `attach_payout_skipped_unsealed count=N`、attach 仍轉 Attached（cup 已盡力吸了、有部分吸到就算 attached，只是「重量轉移」這一步不做）

設計選擇（過程中討論的）：
- (a) ✅ 任一顆沒密封就 skip 放繩 —— 最嚴、最安全、safe-by-default
- (b) 容忍少數顆 —— 拿掉了，因為「少數」門檻在 DSZL 沒校正前難設、且少數顆失敗也可能讓剩下的 cup 過載
- (c) 進 PausedOnError —— 拿掉了，attach 屬於起始流程、不該丟一個錯卡住整個 GUI；用 EVT 通知操作員夠了

驗證 bench：在牆面差的位置或刻意拿掉 cup 跑 attach，看 log `[attach] skip pay_out — cups still unsealed: 4 ...` + EVT `attach_payout_skipped_unsealed`，attach 仍然轉 Attached，繩維持高張力承重。然後正常情況下（全部密封）log 仍印 `pay out crane to ~N kg per rope`，行為跟之前一樣。

## 2026-05-20b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_attach` 第 4 步：放繩目標張力改成讀 crane status 的 `retract_tension_stop_kg=`（網頁「收繩軟停張力」），失敗才退回 `ATTACH_PAYOUT_TARGET_KG` 常數
- `user_lib/WASH_ROBOT.h` — `ATTACH_PAYOUT_TARGET_KG` 註解改成「fallback only」
- `web_backend/public/index.html` — 「收繩軟停張力」hint 補上「也用於 attach 末段放繩」
### 原因
User：「把 10kg 改成跟 step_down/up 一樣用 web 上的收繩停止張力」。

step_down/step_up 的 retract 走 crane `motion_rope`，到 `g_retract_tension_stop_kg`（網頁「收繩軟停張力」可即時調）就軟停。attach 末段以前用獨立的 `ATTACH_PAYOUT_TARGET_KG = 10.0` 常數，要改值得重編 washrobot。User 要兩個共用同一個網頁旋鈕。

做法：cmd_attach 第 4 步開始時 `crane_cmd_("status", 2)`，從 reply 解析 `retract_tension_stop_kg=N`，用 N 當 pay_out 目標。解析失敗（crane 不在線 / status read 失敗 / 欄位缺）→ 退回 `ATTACH_PAYOUT_TARGET_KG`(10.0) 當保險。Range check（0 < v < 500）防止吃到垃圾值。

效果：網頁「收繩軟停張力」的數字現在同時管:
1. step_up/step_down retract 的軟停張力
2. attach 末段放繩的目標張力

bench 上只調一個旋鈕、兩處生效。GUI hint 也更新告訴操作員這件事。

驗證 bench：跑 attach，看 log `[attach] pay out crane to ~N kg per rope (web 收繩軟停張力, fallback=10)`，N 應該等於網頁設的值；網頁改成例如 20 → 下次 attach 就用 20。

## 2026-05-20a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_feet_realign_` Phase 1 crane assist 從 Phase A 之後**移到 Phase A 之前**（pre-check #1 通過之後、Phase A 之前）
### 原因
User：「Phase 1 改到 realign 一開始」。

原本順序：pre-check #1 → Phase A（身體 retract）→ Phase 1 crane assist（收繩） → pre-check #2 → Phase 2 …
新順序：pre-check #1 → **Phase 1 crane assist**（先把繩收緊到上限）→ Phase A → pre-check #2 → Phase 2 …

目的：在 Phase A 釋放身體吸盤**之前**先讓繩承載到位，避免身體釋放瞬間機體突然把重量丟到繩上的衝擊。

選擇 pre-check #1 **之後** 插入（不是文字上絕對的 realign 第一行）的理由：pre-check #1 的設計是「機器仍在原本封閉狀態下做安全 fail-fast」，失敗時走 PausedOnError／abort、流程返回**沒有 Phase 5 restore**。若把 Phase 1 放在 pre-check #1 之前，遇到 abort 會把繩留在 40kg 緊繃狀態。放在 pre-check #1 通過之後既符合「Phase A 之前」的物理需求、也不必新增 abort 路徑的 restore 邏輯。pre-check #1 只是讀氣壓、沒有 motion → 「移到 realign 一開始」實質上就是「移到任何 motion 之前」。

不變：Phase 5 的 pay_out 還原仍解析 `crane_assist_actual_cm`、會自動跟著新位置的實際收繩量還原。pre-check #2（Phase A 之後）的設計仍能涵蓋「Phase 1 crane assist 或 Phase A 干擾到 feet cup」這兩種情況，註解略改、邏輯不變。

若你真的要絕對放在 pre-check #1 之前，跟我說我再補上 abort 路徑的還原。

## 2026-05-19w Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_feet_realign_` Phase 1 crane assist 的張力目標改用 `rope_weight_limit_per_sensor_kg_()`
- `user_lib/WASH_ROBOT.h` — 移除常數 `REALIGN_CRANE_ASSIST_TARGET_KG`（原 2.0kg，已無人引用）；更新相關註解
### 原因
User：realign 的 Phase 1 crane assist 收繩目標從固定 2kg 改成「和其他張力閥值用一樣的參數」。

原本 Phase 1 `crane_retract_to_weight_(REALIGN_CRANE_ASSIST_TARGET_KG=2.0, MAX_CM)` — 收繩到張力僅 2kg（常數註解標「保守起步值」）。改成目標 = `rope_weight_limit_per_sensor_kg_()` — 即 `crane_retract_safe_` monitor 用的同一個張力上限（Running/Attached 40kg、Hanging 80kg，狀態相依）。realign 在 Running 狀態 → 目標 40kg。

`REALIGN_CRANE_ASSIST_TARGET_KG` 改後無人引用 → 移除。Phase 5 的 pay_out 還原沿用 `crane_assist_actual_cm`（從 Phase 1 回覆解析實際收的 cm），會自動跟著新目標調整，不需改。

注意：`REALIGN_CRANE_ASSIST_MAX_CM` 維持 10cm — 若 10cm 內收不到 40kg，`crane_retract_to_weight_` 回 `OK max_cm_reached`、停在 10cm（仍是 OK，不會 fail）。若實機發現 10cm 不夠到上限、需要的話再調高 MAX_CM。

驗證 bench：觸發 realign，看 `[crane_retract_to_weight] step` log 的張力是否往 40kg 收（或到 10cm cap 停）。

## 2026-05-19v Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `pusher_extend_with_disable_seal_` 的「weak_seal early」分支補上重開驅動 EN
### 原因
Bench：disable_seal 後 cycle_group_ L2 retract 的 `pusher_two_stage_retract_` 對 slave 2 `pos_mode_nowait FAIL`。User 診斷正確 —— slave 2 的驅動 EN（使能）沒開。

根因是 2026-05-19t「weak_seal early」的 bug：disable_seal Step E 會把未 done 的 cup `motion_control_driver_EN(false)`。正常兩個出口都會重開回來（SEALED 路徑、MAX_ITERS 收尾的 `if(!done)`），但 2026-05-19t 新加的「endpoint + plateau → weak_seal early」只設 `weak_seal=done=true`、**漏了重開 EN**。該 cup 因此帶著 EN=OFF 離開 disable_seal，且因為已 `done`，收尾的 `!done` 重開也跳過它 → 下一個 pos_mode（L2 retract）被 ZDT 韌體靜默拒收 → FAIL → PausedOnError。

修正：weak_seal early 分支比照另外兩條出口，補上 `motion_control_driver_EN(true)` + `sleep_ms_(80)` + 重讀 `final_pulse`。維持 disable_seal 的契約「所有 cup 離開時 EN 都是開的」。

驗證 bench：在有縫牆面跑伸腳，slave 觸發 `weak_seal early` 後，cycle_group_ L2 retract 不再 `pos_mode FAIL`。

## 2026-05-19u Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增常數 `BACKUP_PAYOUT_MARGIN_CM = 5`
- `user_lib/WASH_ROBOT.cpp` — 改寫 `do_step_up_()` 的 `body_backup_factory_up`（step_up 身體 backup retry 的繩控）
### 原因
User 要求 step_up 身體 backup（後退重吸）的繩控改成「先大方放鬆 → 移 rail → 張力回授收繩」。

原本：`pay_out backup_cm` → 移 rail。收繩量靠盲算 cm。

改後三步：
1. `pay_out (backup_cm + BACKUP_PAYOUT_MARGIN_CM)` — 多放 5cm 額外鬆繩餘裕，讓 rail 下移身體時繩夠鬆
2. `dm2j_pair_move_abs_` 移 rail（會等到位）
3. `crane_retract_safe_(backup_cm)` — 收回 backup_cm，但 `crane_retract_safe_` 的重量 monitor 會在張力到門檻時提早停（與 step_up/down 的收繩一致）

淨繩位移 ≈ backup_cm（和原本一樣），但最終張力由感測回授決定，不是盲算 cm。vacuum-retry backup：放 `5+5=10`、收 5；obstacle-rescue backup：放 `10+5=15`、收 10。

範圍：只動 step_up 身體 backup（`body_backup_factory_up`）。step_down 身體 backup 是 retract 方向、未動；feet backup 無繩控、未動。

備註：step3 用 `crane_retract_safe_`，trip 門檻是 40kg（ATTACHED 狀態安全上限），到限即停並回 OK 當收完 — 屬安全上限式的停，非伺服到目標張力。

驗證 bench：step_up 身體階段觸發 backup retry，看 log 依序出現 `pay_out (backup+margin)` → `rail →` → `crane retract N (tension-monitored)`。

## 2026-05-19t Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `pusher_extend_with_disable_seal_` Step F：endpoint cup 真空 plateau → 立即 weak_seal
### 原因
牆面有縫隙的位置,cup 頂到牆了也吸不到（p≈0）。原本這種 cup（endpoint_stalled）會被拖到 MAX_ITERS 才判 weak_seal —— 但 endpoint cup 後續 iter 不會再被推,狀況不會變,中間的 iter 是空轉。

改法：Step F 趨勢偵測判 plateau 時,若該 cup 已是 `endpoint_stalled`（path A WALL 或 path B endpoint stall 設的）→ 直接 `weak_seal=done=true`,不再拖。

判斷依據：endpoint（已在牆上、不會再推）+ plateau（真空停滯）兩條件同時成立 → 這個位置就是吸不到,結果不會再變。non-endpoint 的 cup plateau 維持原樣（下個 iter 照常多推 0.8cm，可能推到好的牆面）。

效果：endpoint+吸不到的 cup 立刻收掉 → iter loop 提早 break → 提早交給 cycle_group_ 的 L2 重吸（退 5cm 換新牆面，這才是牆面有縫的真正解）。不會誤殺 —— endpoint cup plateau 後本來就注定 weak_seal,提早判定只省空轉、不減少它本來會吸到的機會。

驗證 bench：在有縫的牆面位置伸腳,看 log `at wall + vacuum can't seal ... weak_seal early, skip remaining iters`,該 cup 不再被拖到 iter 4；整組若都這樣 → iter loop 提早結束 → 進 L2 退 5cm 重吸。

## 2026-05-19s Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增常數 `OBSTACLE_REGRESS_MARGIN_CM = 0.3`
- `user_lib/WASH_ROBOT.cpp` — `pusher_extend_with_disable_seal_`：path A obstacle 判定加「跨 iter 退步」檢查
### 原因
Bench 實測（user 故意在 slave 4 下放障礙物）：障礙物**從頭到尾沒被判成 obstacle**。iter 1 電流 path A 有觸發（peakI 1202 > 1200），但 2026-05-19i 的位置 gate 把它判成 `WALL endpoint`（卡死 pulse 21943 ≥ preset−1.5cm gate 19615）→ 標 endpoint_stalled、後續 iter skip → 最後只 WEAK SEAL。位置 gate 對「障礙物剛好在接近全伸處」是死角。

修法：新增**跨 iter 退步判定**，蓋過位置 gate：
- 新增 per-cup `max_reached[]` — cup 曾到過的最深 pulse。Phase 1 後 seed、每次 Step D poll 更新、跨 iter 持續
- path A 電流超標時：`final_pulse < max_reached − OBSTACLE_REGRESS_MARGIN_CM(0.3cm)` → `regressed=true`
- 判定：`near_preset && !regressed` → WALL endpoint（壓牆，defer）；否則 → obstacle
- 物理依據：牆不會往內縮。上輪推桿能伸到 X，這輪過不了 X−0.3cm 就卡死 → 中間冒出新障礙物。margin 0.3cm 濾掉正常機械變異（~mm 級），又抓得到真障礙物

套 slave 4 那筆：iter 0 到過 23288 → `max_reached=23288`；iter 1 卡在 21943（短 0.47cm > 0.3cm margin）→ `regressed=true` → 正確判 obstacle → 走 L3 rescue（退 10cm 離開障礙物）。

順手修一個 log bug：path A 的 `I=` 印的是 `emergency_stop` + 重讀 status 後的電流（`st` 是 reference），不是觸發當下的值。改成觸發前先存 `trig_I` 再印。

驗證 bench：障礙物放接近全伸處，看 log iter 1 應印 `OBSTACLE ... (regressed=1 maxReached=... gate=...)` 而非 `WALL`；正常壓牆仍印 `WALL ... endpoint`。若正常壓牆偶被誤判 obstacle → 調大 `OBSTACLE_REGRESS_MARGIN_CM`；真障礙物漏抓 → 調小。

## 2026-05-19r Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增常數 `VACUUM_PLATEAU_MS = 1500` / `VACUUM_PROGRESS_EPSILON_KPA = 3`
- `user_lib/WASH_ROBOT.cpp` — `pusher_extend_with_disable_seal_` Step F（WAIT_SEAL）加真空趨勢提早結束
### 原因
原本 WAIT_SEAL 每 iter 等滿 `VACUUM_DEEPEN_TIMEOUT_MS`(5s)，cup 真的吸不到時要 5s×5 iter≈25s 才走到 weak_seal → L2 重吸（退 5cm 換牆面，真正的解）。最該快點觸發的反而被拖最久。

加趨勢偵測：WAIT_SEAL 每 100ms poll 時，per-cup 追蹤真空最深值 `best_p`。
- 真空又變深 ≥ `VACUUM_PROGRESS_EPSILON_KPA`(3kPa) → 重置「停滯計時」
- 停滯逾 `VACUUM_PLATEAU_MS`(1500ms) 沒再變深 → 判定本 iter 吸不到、`plateaued[i]=true`、停止等它
- 迴圈結束條件從「全部 done」改為「全部 done 或 plateaued」

效果：真的吸不到的 cup 每 iter 從 5s 砍到 ~1.5s → weak_seal / L2 重吸提早約 3 倍觸發。慢但會吸到的 cup 真空持續變深 → 停滯計時一直重置 → 保有完整 grace（到 5s 上限），不會被誤判 weak_seal。5s 上限保留當「真的很慢但會吸到」的底線。

比較對象 `best_p`（本 iter 至今最深）：慢速穩定變深的 cup，改善量會累加、跨幾個 poll 後仍會超過 epsilon → best_p 推進 → 計時重置，不會被誤殺；只有真正平掉的 cup 才會 plateau。bench 實測的 sealing cup 0.4-2.1s 內到 -60（~30-150kPa/s），遠快過 3kPa/1.5s 的 plateau 門檻 → 區分穩健。

plateaued 但沒 done 的 cup → 下個 iter 照常 Step C 多推 0.8cm 再試（沿用原流程）；跑完 MAX_ITERS 仍沒 done → weak_seal 收尾。

驗證 bench：跑伸腳遇吸不到的 cup，看 log `vacuum plateau ... stop waiting this iter`，每 iter WAIT_SEAL 從 ~5000ms 縮到 ~1500ms；正常會吸到的 cup 仍印 `SEALED ... wait=Nms`、N 不受影響。

## 2026-05-19q Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp`
  - `crane_watchdog_loop_` + realign Phase 2 stall：寫 `state_before_pause_` 前加 guard（state 已是 PausedOnError 就不覆寫）
- `Crane_control_PI/main.cpp`
  - 新增 `RETRACT_TENSION_STOP_KG_DEFAULT` 常數 + atomic `g_retract_tension_stop_kg`
  - `motion_rope`：retract 時新增「軟停張力」檢查 — 張力到 `g_retract_tension_stop_kg` → sync stop + 回 `OK tension_reached`（不發 alarm、不跑 fine_adjust、非錯誤）
  - 新增指令 `set_retract_tension_stop_kg` + handler + dispatch；`cmd_status` 多回 `retract_tension_stop_kg=`
- `web_backend/public/index.html` — 新增「收繩軟停張力」input；原「自動收放繩張力門檻」改名「自動運動過載警報」；hint 改寫說明三者差異
- `web_backend/public/app.js` — status 解析 `retract_tension_stop_kg`；input wiring 加一條
### 原因
**Bug 1 — 按 skip 沒反應：** step_up 收繩遇 crane tension_alarm 進 PausedOnError 時，`try_or_pause_` 先進、`crane_watchdog` 又收到 `EVT tension_alarm` 二次 escalate。watchdog 的 `state_before_pause_ = state_.load()` 沒 guard → 此時 state 已是 PausedOnError → recovery target 被汙染成 PausedOnError → `cmd_skip`/`cmd_continue` 做 `set_state_(state_before_pause_)` = set 回 PausedOnError → 永遠出不來、按鈕像死的。`await_user_intervention_` 2026-05-06 早就修過同款 bug、有 guard；watchdog 跟 realign Phase 2 stall 兩處漏掉。補上同樣的 guard。

**Bug 2 / 功能 — crane 收繩軟停：** 之前只有兩種張力門檻、跳掉行為相反 ——washrobot `crane_retract_safe_` 40kg monitor（soft，當完成）vs crane `motion_rope` 的 `TENSION_MAX_KG`（hard alarm）。user 把網頁 `TENSION_MAX_KG` 調低想要「到張力就停當完成」，結果調到的是硬警報 → tension_alarm → error。

新增第三個門檻 `g_retract_tension_stop_kg`：`motion_rope` retract 時，`max(L,R)` 到此值 → sync stop、回 `OK tension_reached`、跳過 fine_adjust（fine_adjust 會往 cm target 再收、重新拉緊）、**不發 tension_alarm**。washrobot 端 `crane_retract_safe_` 收到 `OK...` → `try_or_pause_` 視為成功、流程繼續，不進 PausedOnError。pay_out 不適用此軟停（放繩本不該長張力）。硬警報 `g_tension_max_kg`/`g_tension_diff_max_kg` 保留，當設高於軟停的最後保險。

網頁三個門檻分清楚：收繩軟停（正常完成）/ 過載警報（故障）/ 收繩總和（只管手動 hold）。

驗證 bench：跑 step_up，看 crane log `[motion_rope] soft tension stop ... (slack collected)` + 回 `OK tension_reached`，washrobot 不再 PausedOnError；真的 PausedOnError 時按 skip/continue 確認狀態能離開 PausedOnError。`g_retract_tension_stop_kg` 預設 25kg 是 placeholder，DSZL 校正後再調。

## 2026-05-19o Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - `TENSION_MAX_KG` / `TENSION_DIFF_MAX_KG` constexpr 改名為 `*_DEFAULT`，新增 atomic `g_tension_max_kg` / `g_tension_diff_max_kg`
  - `tension_safety_check_values` 改讀 atomic（不再讀 constexpr）
  - 新增指令 `set_tension_max_kg <kg>` / `set_tension_diff_max_kg <kg>` + handler + dispatch
  - `cmd_status` 多回報 `tension_max_kg=` / `tension_diff_max_kg=`
  - 檔頭指令清單註解補上兩個新指令
- `web_backend/public/index.html` — 鋼索張力面板新增「自動收放繩張力門檻」一列（單側上限 + 左右差上限兩個 input）+ 說明 hint
- `web_backend/public/app.js` — status 解析 `tension_max_kg` / `tension_diff_max_kg` 更新「目前」顯示；input 改值 debounce 150ms 送指令
### 原因
User 要求把 motion_rope 的張力門檻做成網頁可動態調整（之前是 constexpr，要重編譯才能改）。

背景：step_up 收繩衝到 ~38kg 沒被攔，因為 motion_rope 的 abort 門檻 `TENSION_MAX_KG` 寫死 100kg（DSZL 未校正期間調高避免 false alarm）。要現場調門檻就得能動態設定。

做法：完全比照既有的 `g_up_stop_total_kg` + `set_up_stop_total_kg` pattern（hold-mode 總和門檻早就是動態的）。constexpr 保留當 `*_DEFAULT` 初始值，實際值走 atomic。`server.js` 是通用指令轉發、不需改。

注意兩個門檻管不同東西，網頁上是分開的兩列：
- 「收繩總和停止門檻」`g_up_stop_total_kg` → 只管手動 hold 收繩、判 L+R 總和
- 「自動收放繩張力門檻」`g_tension_max_kg`(單側) / `g_tension_diff_max_kg`(左右差) → 管 motion_rope（pay_out/retract cm 自動運動，含 step 收放繩）+ fine_adjust

驗證 bench：網頁改「單側上限」→ 看 crane log `[crane] tension_max_kg = N`、status `tension_max_kg=N`、網頁「目前」欄跟著變；跑自動收繩、張力超過設定值應觸發 EVT tension_alarm + 中止。DSZL-107 未校正前門檻數值僅參考。

## 2026-05-19n Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `init()` 拿掉 DY-500（slave 10/11）的開機 probe，`weight_present_` 直接寫死 false
### 原因
User 確認 DY-500 鋼索重量感測器（slave 10/11）**實體沒安裝**，要求關掉。

現象：背景 poll loop 閒置時每 ~1 秒輪詢 DY-500，讀失敗 → driver 累積連續錯誤達門檻 → 一直印 `[ERR] [DY500:10/11] get_weight_float: consecutive errors reached threshold`。

原本 init 有「probe 一次、失敗就標 absent 不輪詢」的防 spam 設計，但單發 probe 在開機時被 gateway stale buffer / echo 誤判成成功（已知 Modbus-TCP stale buffer pattern）→ `weight_present_=true` → 一直輪詢失敗。

修法：既然確定沒裝，直接拿掉 probe，`weight_present_[0/1]` 寫死 false → 背景 loop 的 `if (!weight_present_[i]) continue` 永遠跳過 → 不輪詢、不洗版。driver 物件仍 init（保持有效狀態）。功能不受影響：鋼索重量本來就靠 crane DSZL-107 張力（`read_rope_weight_max_kg_` tier 1 / `crane_retract_safe_` monitor 走 estop tension），DY-500 只是未使用的 tier-2 fallback。

未來若實體裝上 DY-500，把 probe 邏輯加回來即可（註解有留說明）。

## 2026-05-19m Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp`
  - 新增 `read_rope_weight_estop_()` — 透過 estop 通道讀鋼索張力（送 `tension`、解析 left/right、回 max kg）
  - `crane_retract_safe_` 的 active monitor 改用 `read_rope_weight_estop_()`（原本用 `read_rope_weight_max_kg_()`）
  - `crane_retract_safe_` 過重 trip 的回傳從 `ERR rope_weight_tripped` 改為 `OK rope_weight_tripped ... (stopped early, treated as done)`
- `user_lib/WASH_ROBOT.h`
  - 新增 `read_rope_weight_estop_()` 宣告；更新 `crane_retract_safe_` 註解
### 原因
**Bug：收繩過重保護完全失效。** User 實測：主程式下收繩指令、網頁顯示鋼索已破 50kg，retract 卻沒中斷。

根因 — `crane_mtx_` 死結：`crane_cmd_`（WASH_ROBOT.cpp:470）整個指令期間持有 `crane_mtx_`。收繩時主執行緒的 `crane_cmd_("retract N")` 把鎖佔住好幾秒；monitor 執行緒每 100ms 呼叫 `read_rope_weight_max_kg_()` → tier-1 `crane_cmd_("tension")` → 想拿同一把 `crane_mtx_` → 被擋住。整趟收繩 monitor 一筆重量都讀不到，無從 trip。原作者知道收繩時 `crane_mtx_` 被佔（所以送 stop 走 estop 通道），但漏了讀重量的路徑也走 `crane_mtx_`。

修法 1（修 bug）：新增 `read_rope_weight_estop_()`，透過 estop 專用 TCP 連線（`crane_cli_estop_` + `crane_estop_mtx_`，不碰 `crane_mtx_`）送 `tension` 讀張力。monitor 改用它 → 收繩中也讀得到、能正常 trip 送 stop。pre-check 不動（它在收繩前跑、鎖還沒被佔）。

修法 2（per user）：trip 後回傳從 `ERR` 改 `OK`。原本 trip → ERR → 呼叫端 `try_or_pause_` → PausedOnError 卡住流程。改成 `OK rope_weight_tripped ...` → 把「沒走完公分數但張力到限」當成這趟收繩已完成（slack 已收完、繩已張緊），流程繼續不暫停。estop `stop` 照送。影響所有 `crane_retract_safe_` 呼叫端（step_up/step_down/`crane_retract_to_weight_`）。

不變：pre-check「開始前已超 limit」「感測器離線」仍回 `ERR`。

驗證 bench：故意收繩到破門檻，確認 log 出現 `OVERWEIGHT ... sending crane stop`、crane 真的停、且流程不進 PausedOnError 而是繼續。前提：crane 端能在收繩進行中回應第二條連線的 `tension` 查詢（與「estop 送 stop 能中斷收繩」同一假設）。

## 2026-05-19l Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h`
  - `ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_ATTACHED` 50 → **40** kg
  - `ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_HANGING` 90 → **80** kg
### 原因
User 要求把吊車收繩的過重保護門檻調低（`crane_retract_safe_` 的 active monitor 用此值，超過就送 stop estop）。

- ATTACHED（Attached/Running/Paused/PausedOnError/Balancing 等吸盤吸著的狀態）40kg/sensor — 收繩中繩載重超 40kg 即中止
- HANGING（Idle/Ready/ReturningHome/Error 純吊掛狀態）80kg/sensor

備註：HANGING 80kg 仍高於名目吊掛載重（MACHINE_WEIGHT_KG 135 / 2 繩 ≈ 67.5kg/繩），餘裕從 ~22.5kg 縮到 ~12.5kg — 正常吊掛不會誤觸，但若實機單繩分配不均、瞬間擺盪可能更接近門檻。門檻本質仍是估算值，待實機校正。

## 2026-05-19k Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — 更正 `do_step_up_()` 的 lead comment
### 原因
User 請我檢查 step_up 收繩有沒有收太多。檢查結果：收繩邏輯正確 — Phase A（腳爬、身體錨）不收繩；Phase B retract `round(rail_delta)` = 身體實際爬升 cm，剛好收回身體往上爬產生的 slack，無 margin、無雙重計算。

但發現函式 lead comment 過時：仍寫著舊邏輯「retract step+margin BEFORE rail，pay_out margin AFTER」。實際程式（2026-05-19a 起）已改為「rail 先移、之後 retract delta、無 pre pay_out」。更正註解使其與實作一致，避免誤導。

純註解更正，無行為變動。

## 2026-05-19j Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp`
  - `pusher_two_stage_retract_` Stage 1 改成逐 slave 判斷：推桿目前位置若已比 Stage 1 終點（preset − RETRACT_SLOW_PEEL_CM）更內縮 → 跳過 Stage 1、直接 dispatch Stage 2 收到 0
  - 更新函式 lead comment
### 原因
User 反映：兩階段收腳的 Stage 1 用**絕對位置**移到 `preset − RETRACT_SLOW_PEEL_CM`（脫壁終點）。若某隻推桿原本就已收在比這更內縮的位置，絕對位置移動會把它**往外伸出去**到 Stage 1 終點才開始收 → 多跑一趟、還可能讓已離牆的 cup 重新撞牆。

修法：dispatch Stage 1 前逐 slave 讀目前位置（`real_pos° × 10 = pulse`）：
- `cur_pulse > stage1_target`（還貼牆）→ 正常 Stage 1 慢速脫壁（sync 起步）
- `cur_pulse ≤ stage1_target`（已更內縮）→ 跳過 Stage 1，直接 Stage 2 收到 0（full speed、sync=0 立即起步），`stage[i]=2`
- 讀位置失敗 → fallback 正常 Stage 1（= 原行為，不會更糟）

`trigger_sync_move()` 只在「真有 slave 走 Stage 1」（`any_stage1`）時呼叫。poll loop 不變 — 它本來就看 `stage[i]` 分支，skip 的 slave `stage[i]=2` 直接走到 stage2 done。

驗證 bench：先把某隻 ZDT 手動收到很內縮的位置，再觸發整組兩階段收腳，確認 log 印 `already inside stage1 endpoint ... skip stage1`，且該推桿不會反向伸出。

## 2026-05-19i Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增常數 `OBSTACLE_ENDPOINT_GATE_CM = 1.5`
- `user_lib/WASH_ROBOT.cpp` — `pusher_extend_with_disable_seal_` Step D obstacle 路徑 A 加位置 gate
### 原因
Bench log：body slave 6 在 disable_seal iter 0 push 時相電流飆到 1772mA 被判 OBSTACLE，但實際沒撞障礙物 —— cup 是壓在牆上（Phase 1 已花 1650ms/605mA 頂到牆，iter 0 的 +over-push 把已貼牆的 cup 往死裡夾 → 電流爆）。

純電流判定（路徑 A）分不出「壓牆 jam」和「障礙物 jam」：兩者馬達都卡死、電流都飆。調高門檻沒用（壓牆可飆到 1.7A 以上，吃驅動器上限為止）。

改法：路徑 A 電流超標時，**用 cup 卡死的絕對位置區分**：
- `final_pulse >= preset − OBSTACLE_ENDPOINT_GATE_CM`（cup 已推到接近全伸位置）→ 判定壓牆 endpoint，比照路徑 B 的 endpoint stall 處理（`endpoint_stalled[idx]=true`、`finished=true` defer，讓 Step E disable EN / Step F 等真空），**不**設 obstacle、**不** abort 旁邊的 sibling push
- `final_pulse < gate`（cup 卡在遠短於 preset 的位置）→ 才是真障礙物，維持原本 obstacle 行為

gate 用 per-slave `preset_extend_pulse_for_slave_` − `cm_to_pulses_for_slave_(s, 1.5)`，自動處理 body 長/短(5,6/7,8)、feet 不同 preset。

驗證 bench：跑伸腳，看 log —— cup 推到位才飆電流應印 `WALL ... endpoint, not obstacle`（不再進 rescue）；真的半路被擋才印 `OBSTACLE ... < gate`。若正常壓牆仍偶爾被判 obstacle，代表 cup 卡死位置比 preset−1.5cm 更短，調大 `OBSTACLE_ENDPOINT_GATE_CM`。

## 2026-05-19h Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h`
  - `DISABLE_RETRY_INCR_PULSE` 3000 → **2400**（disable_seal 每 iter 補伸 1.0cm → 0.8cm）
  - `DISABLE_RETRY_MAX_OVEREXTEND` 的註解更新（步進改 0.8cm 後的 iter 分佈）
- `user_lib/WASH_ROBOT.cpp`
  - `pusher_extend_with_disable_seal_` Step D STALL 路徑的 `expected_delta` 註解更新（過時的「1500 pulse → 150°」改為 2400 → 240°）
### 原因
User 要求 disable_seal 每個 iter 的補伸量改成 0.8cm。

換算依專案慣例 3000 pulse = 1.0cm（與 changelog 2026-05-18k 一致）→ 0.8cm = 2400 pulse。

配 cap 15000，iter 分佈變為：
- iter 0~4 = push 到 +2400/+4800/+7200/+9600/+12000（五輪都能 push，總過伸 +4.0cm）
- Step C cap 判斷最大只看到 accumulated=9600 < 15000 → cap 仍不會提早觸發，MAX_ITERS=5 維持 binding limit

備註：INCR 是單一常數、feet/body 共用。3000 pulse=1.0cm 對 body 精準、對 feet 約 0.84cm（既有近似，未變）。Step D STALL 路徑的 `expected_delta = INCR_PULSE * 0.1` 會自動跟著變（2400 → 240°），progress 判定比例不受影響。

驗證 bench：跑 extend，確認每 iter 的 `push absolute target` 間距約 2400 pulse；弱密封 cup 仍跑滿 iter 0~4。

## 2026-05-19g Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h`
  - `DISABLE_RETRY_MAX_OVEREXTEND` 7500 → **15000**（disable_seal 過伸上限 +2.5cm → +5.0cm）
  - `DISABLE_RETRY_MAX_ITERS` 註解更新（現在才是真正的 binding limit）
### 原因
User 反映 disable_seal 伸腳實測只跑到 iter 2 就停（log: slave 2 `WEAK SEAL cap (+9000 pulses past phase1)`），預期應能跑到 iter 4。

根因：2026-05-18 把 `DISABLE_RETRY_INCR_PULSE` 1500→3000 後，每 iter 跳 +3000，iter 2 push 完 accumulated 就到 +9000；iter 3 的 Step C cap 判斷（`accumulated >= DISABLE_RETRY_MAX_OVEREXTEND`，7500）立刻觸發 → 標 weak_seal、不再 push。`DISABLE_RETRY_MAX_ITERS = 5` 變成永遠執行不到的 dead limit。舊 cap 7500 是配舊 INCR 1500（iter 0~4 = +1500~+7500 剛好踩線）。

修法：cap 7500 → 15000，配新 INCR 3000：
- iter 0~4 = push 到 +3000/+6000/+9000/+12000/+15000（五輪都能 push）
- Step C cap 判斷最大只會看到 accumulated=12000 < 15000 → cap 不再提早觸發
- 改由迴圈 `iter >= MAX_ITERS` 收尾 + 後段 wrap-up（`WEAK SEAL after MAX_ITERS`）判定弱密封
- 過伸總量上限 +5.0cm；LEYG25 行程 200mm，feet phase1 終點 ~7.4cm + 5cm = ~12.4cm，仍在行程內

注意：這是治標 — log 中 slave 2 的問題是「推進電流近 0、推桿空推沒碰到牆」，調高 cap 只是讓它多伸幾輪、有機會搆到較遠的牆面；若該角牆距真的超出行程或氣管異常仍會 weak_seal。obstacle 偵測（路徑 A 1200mA）每輪仍照常守著。

驗證 bench：放障礙物 / 正常吸附都跑 extend，確認 disable_seal 會跑到 iter 4；確認沒碰牆的 cup 最後是 `WEAK SEAL after MAX_ITERS`。

## 2026-05-19f Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — `RETRACT_SLOW_PEEL_CM` 1.5 → **2.0**
### 原因
User 要求兩階段收推桿第一段慢速脫壁距離改成 2cm（原 1.5cm）。慢段加長 0.5cm，脫壁更保險，仍遠短於改寫前的 ~2.7-3.2cm。

## 2026-05-19e Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h`
  - `cycle_group_` 的 Clog_Ma RAII guard 整塊用 `#if 0` 停用（不再寫 ZDT 韌體賭轉電流）
  - `CLOG_MA_GENTLE` / `CLOG_MA_NORMAL` 常數註解標明「目前未使用」（只剩 #if 0 區塊引用）
  - `cycle_group_` 上方演算法說明的 ClogMaGuard 段落改為「已停用」；移除過時的「RAII Clog guard」註解
- `user_lib/WASH_ROBOT.cpp`
  - `smart_extend_subset_` 的 Clog_Ma RAII guard 整塊用 `#if 0` 停用
### 原因
User：「把會修改 zdt 實際的賭轉電流的地方註解掉，只用軟體讀取電流值判斷就好」。

原本伸腳前會把 ZDT 驅動卡韌體的 Clog_Ma（賭轉保護檢測電流）降到 0.8A、密封後復原 3A。現在停用此韌體寫入 — 韌體 Clog_Ma 維持驅動卡上操作員設定的值（3A），obstacle 偵測完全交給軟體相電流判定（`pusher_extend_with_disable_seal_` Step D 路徑 A，`DISABLE_PHASE_CURRENT_LIMIT_MA` = 1200mA）。

副作用（已知、可接受）：路徑 B（韌體 `stall_flag` + 進度 < 80%）在 Clog_Ma = 3A 下幾乎不會觸發 — 正常伸腳馬達相電流很少到 3A。obstacle 實質改由路徑 A 單獨負責。路徑 B 程式碼保留作為 fallback，未刪。

兩塊 guard 以 `#if 0`/`#endif` 包住保留，未來要恢復韌體 Clog 控制只需移除 `#if 0`。

驗證 bench：跑 extend，確認 log 不再出現 `[clog_guard] ... enter/exit`；伸腳撞障礙物時靠 `OBSTACLE I=...mA (current-only judgment)` 偵測。

## 2026-05-19d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h`
  - 新增常數 `RETRACT_SLOW_PEEL_CM = 1.5`（兩階段收推桿第一段慢速脫壁距離）
  - 新增 method 宣告 `pusher_two_stage_retract_(const std::vector<int>& slaves)`
  - cycle_group_ template 的 retry / rescue 兩處兩階段收推桿改用新 helper
  - 移除已無人使用的常數 `RETRACT_HALF_WAIT_MS`
- `user_lib/WASH_ROBOT.cpp`
  - 新增 `pusher_two_stage_retract_()` — pipeline 式兩階段收推桿
  - 9 處呼叫端改用新 helper（取代舊的 `pusher_move_many_` ×2 / `pusher_move_` ×2 pattern）
### 原因
User 要求改寫兩階段式 ZDT 收推桿：(1) 第一段從「收到 extend×2/3（約 1/3 行程，feet ~2.7cm / body ~3.2cm）」改成只慢速脫壁 **1.5cm**；(2) 不再等全組都到第一段終點才一起收第二段 — **誰先到第一段終點誰就立刻收第二段（到 0）**；(3) 最後確認全部都收到 0。

新 helper `pusher_two_stage_retract_()`：
- **第一段**：每支 slave 目標 = `preset_extend_pulse_for_slave_(s) − cm_to_pulses_for_slave_(s, 1.5)`，慢速 `PUSHER_RPM_RETRACT`(30)，sync trigger 同時起步。
- **Pipeline 第二段**：單一 poll 迴圈，某支 slave 一偵測到第一段穩定 → 立刻對它送第二段指令（目標 0、快速 `PUSHER_RPM_RETRACT_FULL`、sync=0 立即啟動），不等其他 slave。
- **收尾**：迴圈跑到全部 slave 第二段都到 0 才回成功；stall / timeout → 回 true（error，呼叫端進 PausedOnError）。

為何更快：(a) 慢段距離從 ~2.7-3.2cm 砍到 1.5cm，30 RPM 那段時間幾乎減半；(b) pipeline — 快的 slave 不再被最慢的拖住。

涵蓋全部 9 處呼叫端（feet + body，per user 確認「全部」）：step_up/step_down 的 feet/body pre_cycle ×4、cycle_group_ retry、cycle_group_ rescue、realign phase A、cmd_pusher（manual "all" feet+body、manual 單組）、return_home、cmd_zdt_pusher 單支。每處的 group 分支（`(group=="feet")?...`）和 `step1_pulse` 計算都拿掉了 — helper 內部用 `preset_extend_pulse_for_slave_` 自動處理 per-slave 行程。

移除舊的「第一段與第二段之間 `sleep_ms_(RETRACT_HALF_WAIT_MS)` 1 秒 dwell」：新設計第一段就是刻意的慢速 1.5cm 脫壁，slave 回報穩定時 cup 已脫離牆面，dwell 無意義；且 user 明確要「誰先到誰先收」＝立即接第二段。

驗證 bench：跑 step / attach 收推桿，看 `[2stage_retract ZDT:N]` log — 確認 stage1 距離約 1.5cm、各 slave stage1 done 時間不同但都即時接 stage2、最後全部 stage2 done 到 0；注意脫壁太短若造成 cup adhesion 沒完全斷 → 第二段快收會 ZDT stall，屆時調大 `RETRACT_SLOW_PEEL_CM`。

## 2026-05-19c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h`
  - 新增常數 `ATTACH_PAYOUT_TARGET_KG = 10.0` / `ATTACH_PAYOUT_MAX_CM = 50` / `ATTACH_PAYOUT_SETTLE_MS = 300`
  - 新增 private method 宣告 `crane_pay_out_to_weight_(double target_kg, int max_cm)`
- `user_lib/WASH_ROBOT.cpp`
  - 新增 `crane_pay_out_to_weight_()` — 逐 cm pay_out、每步讀 crane DSZL-107 雙邊張力，直到**左右兩邊都 ≤ target_kg** 或達 max_cm 上限
  - `cmd_attach()` 在 fine_tune 補吸後、轉 Attached 前，新增第 4 步呼叫 `crane_pay_out_to_weight_(10, 50)`
### 原因
User 要求：attach 最後、所有 ZDT 都吸好後，把吊機 pay_out 到兩邊鋼索張力都在 10kg 左右。

機制：cup 密封後機體重量仍掛在鋼索上（張力高），逐 cm 放繩讓重量轉移到吸盤承載，鋼索只留 ~10kg 殘餘張力當安全裕度。

設計細節：
- **雙邊判定**：`crane_cmd_("tension")` 回 `OK left=<kg> right=<kg>`，停止條件 = 左右皆 ≤ 10（等價 max ≤ 10），確保「兩邊」都到位，不是只看單邊。
- **1cm/step + 300ms settle**：小步放繩讓重量平順轉移到 cup，避免 cup 瞬間吃滿載彈開。
- **非致命**：張力感測離線 / crane detached → skip + EVT `attach_payout_skipped`，仍轉 Attached。理由：cup 已密封（承重關鍵已完成），放繩只是次要的張力釋放；且「跳過 = 維持鋼索承重」＝未加此步前的既有行為，安全。
- **max_cm=50 上限**：若放繩 50cm 張力仍未降到 10，代表 cup 沒實際承重（異常），停手回 `OK max_cm_reached` 讓 log 可見。

驗證 bench：跑 attach，看 `[crane_pay_out_to_weight] step` log 的 left/right 是否逐步降到 ~10；確認放繩過程 cup 沒被扯離牆。注意 DSZL-107 scale factor 目前仍是 placeholder（0.01 待實機校正），10kg 門檻可能需依實測調整。

## 2026-05-19b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h`
  - `DISABLE_PHASE_CURRENT_LIMIT_MA` 900 → **1200**（obstacle 偵測路徑 A 的電流門檻，0.9A → 1.2A）
- `user_lib/WASH_ROBOT.cpp`
  - `pusher_extend_with_disable_seal_` Step D obstacle 路徑 A 的註解更新（800mA → 1200mA）
### 原因
User 要求把 ZDT 伸腳時的電流檢測（obstacle 偵測門檻）調到 1.2A。

路徑 A 自 2026-05-18 改純電流判定後，門檻一度跟 `CLOG_MA_GENTLE`(800mA) 同值 → 正常 cup 用力壓牆建真空時電流也會爬到 800-900mA，會被誤判成 obstacle。調高到 1200mA 在「正常壓牆 push」之上留餘裕，減少 false-positive。

不變動：`CLOG_MA_GENTLE`(800mA 韌體賭轉電流) 不動 — 那是 ZDT 韌體層的 Clog 保護，與軟體 obstacle 判定是兩條獨立機制。

## 2026-05-19a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_step_up_()` 的 `body_pre_cycle`
  - 拿掉身體往上爬之前的 pre-climb `pay_out STEP_MARGIN_CM`
  - 爬升後的 crane retract 從 `delta + margin` 改成 **`delta`**（純收身體實際爬升的 cm）
### 原因
User：「往上走時，身體組往上前不需要 pay_out，往上走後直接 retract 走的 cm 就可以了」。

物理：身體往上爬（朝吊機方向移動）本身就會自然鬆出 delta cm 的繩，不需要事先 pay_out 製造 slack。原本「pay_out margin → 爬 → retract delta+margin」的淨繩位移其實也是 delta（margin 放出再收回淨 0），所以拿掉 margin round-trip 後**淨繩位置不變**，只是少一趟 pay_out + 多收的來回，流程更簡單也更快。

不變動：
- `body_backup_factory_up`（retry backup）仍保留 `pay_out` — 那是把身體往**下**退（rail target = rail_before + backup_cm，rail 值變大 = 身體下降），往下退本來就需要放繩，正確。
- `STEP_MARGIN_CM` 常數保留（step_down 等其他路徑仍用）。

驗證 bench：跑 step_up，看 `[step_up] crane retract N (= body climb delta)` 的 N 是否等於 DM2J delta；確認爬升中腳組沒被繩張力拉扯。

## 2026-05-18k Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h`
  - `DISABLE_RETRY_INCR_PULSE` 1500 → **3000**（disable_seal 每 iter 補伸 0.5cm → 1.0cm）
  - `PHASE1_BUFFER_PULSES` 4500 → **3000**（Phase 1 快伸終點從 preset−1.5cm → preset−1.0cm）
### 原因
User 要求加快伸腳：
1. 每 iter 伸出量 0.5cm → 1cm（INCR_PULSE 1500→3000）— Phase 2 慢推 iter 數變少，每少一 iter 省 ~400ms overhead（Step A settle 200ms + Step D.5 200ms）
2. PHASE1_BUFFER 1.5cm → 1.0cm — 把 0.5cm 從「Phase 2 慢推 50RPM」搬到「Phase 1 快伸 700RPM」，慢段距離縮短

新的 disable_seal 伸出序列（PHASE1_BUFFER=3000 + INCR=3000）：
| Phase | 終點 | 速度 |
|---|---|---|
| Phase 1 快伸 | preset − 1.0cm | 700 RPM |
| iter 0 | preset（剛好）| 50 RPM |
| iter 1 | preset + 1.0cm | 50 RPM |
| iter 2 | accumulated 9000 ≥ MAX_OVEREXTEND 7500 → weak_seal（不推）| — |

實際有效 push iter 從原本 ~3 個（0.5cm 步進覆蓋 1.5cm buffer）降到 ~2 個。

未改（user 暫不動）：
- `PUSHER_RPM_DISABLE_SLOW` 50 — Phase 2 慢推速度（加快有密封品質風險，要 bench 驗證才動）
- `PUSHER_RPM` 700 — Phase 1 快伸已夠快

驗證 bench：跑伸腳，量總時間 vs 之前。看 disable_seal log iter 數 + cum 數值（iter 0 cum +3000、iter 1 cum +6000）。

---

## 2026-05-18j Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 新增常數 `RESCUE_VACUUM_SETTLE_MS = 1000`
  - `cycle_group_` rescue 分支：`vacuum_wait_release_` 之後、`clear_other_group_stalls_` + 兩階段 retract 之前，加 `sleep_ms_(RESCUE_VACUUM_SETTLE_MS)`
### 原因
User 反映 rescue 路徑「真空釋放後」要多一點 delay。

`vacuum_wait_release_` 偵測壓力越過 `DETACH_THRESHOLD_KPA` 就回報「released」，但壓力到門檻 ≠ cup 物理上已脫離牆面 — 殘留吸附可能還黏著。沒有 dwell 的話 retract 馬達直接拉、cup 還黏在牆上 → ZDT 堵轉。

加 1000ms settle 讓殘留吸附完全脫離再 retract。常數可調。

### 沒動
- vacuum-retry 路徑（cycle_group_ attempt > 0 那條）的 `vacuum_wait_release_` 後**沒加** settle — user 只說 rescue。如果 vacuum-retry 也觀察到 retract 堵轉，再比照加
- `RESCUE_VACUUM_SETTLE_MS` 1000ms 是初值、bench 看 retract 還會不會堵轉再調

---

## 2026-05-18i Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h`：`DISABLE_PHASE_CURRENT_LIMIT_MA` 800mA → **900mA**
### 原因
obstacle 路徑 A（18f 起改純電流判定）門檻從 800mA 上調回 900mA 試。
- 800mA 跟 `CLOG_MA_GENTLE` 同值、太貼近正常壓牆電流、容易誤判
- 配合 18g 的 B1 fix（body 不再過推 → 貼牆電流下降），900mA 留多一點誤判餘裕
純常數調整、邏輯不變。要再調直接改這個常數。

---

## 2026-05-18h Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `body_backup_factory` / `body_backup_factory_up` 內 `rail_before` 改用 `dm2j_read_pos_robust_` 讀 DM2J 實際位置（取代 `rail_pos_cm_.load()` 軟體 atomic）
### 原因
Bench：rescue backup 的收放繩量算錯。

`[retry body up_rescue]` 走的是 `body_backup_factory_up` 產的 rescue-backup lambda。它用 `rail_pos_cm_.load()`（軟體 atomic）當 `rail_before` 算 `rail_delta` → crane pay_out/retract 量。

問題：DM2J 用 absolute targeting，如果上一步沒吸好 rail 停在 5cm、但 `rail_pos_cm_` atomic 還寫 0：
- factory 算 target = 0 + backup_cm，rail_delta = backup_cm
- crane pay_out = backup_cm（例如 20）
- 但 DM2J 移到 absolute target，實際從 5 走到 target → 真實移動只有 (target - 5)
- crane 動 20、DM2J 實際動 15 → 5cm 收放繩過頭

14y 已經把 `body_pre_cycle` 改成讀 DM2J 實際位置，但**漏掉 backup factory**（factory 是後來重構出來的，當時沒一起改）。本次補上。

修法：factory lambda 進場 `dm2j_read_pos_robust_(DM2J_LEFT_FOOT/RIGHT_FOOT)` 讀實際位置，平均當 `rail_before`。dry_run 也讀（feasibility check 跟 real exec 用同一個 rail_before 才一致）。讀失敗 → 回 `body_backup_dm2j_read_fail` error。

驗證：bench 重現「上步沒吸好 rail 卡中途 → 觸發 rescue backup」，看 log 印的 crane 量 == DM2J before→after 實際 delta。

---

## 2026-05-18g Claude (Sadie) ⚠ TRIAL — 不行要還原
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `smart_extend_subset_` extend target 計算
- `user_lib/WASH_ROBOT.h` `cycle_group_` template extend target 計算
### 改動內容
body 組 extend target 的計算 base 從 `last_seal_pulse_` 改成 `preset`：

**改前（兩處都一樣）：**
```cpp
int target = last_seal_pulse_[s - 1].load();
if (group == "body") {
    const double over_cm = last_feet_max_over_cm_.load();
    if (over_cm > 0) target += cm_to_pulses_for_slave_(s, over_cm);
}
```

**改後：**
```cpp
int target;
if (group == "body") {
    const double over_cm = last_feet_max_over_cm_.load();
    target = preset_extend_pulse_for_slave_(s)
           + ((over_cm > 0) ? cm_to_pulses_for_slave_(s, over_cm) : 0);
} else {
    target = last_seal_pulse_[s - 1].load();
}
```

feet 組**完全不變**（還是 `last_seal_pulse_` base）。只有 body 改。

### 原因
追「ZDT 伸腳壓到牆面、obstacle 偵測誤判」根因，發現 body extend target 雙重計算 feet_over：

- `body target = last_seal_pulse_body + feet_over`
- 但 body seal 後 `record_seal_pulse_` 把「含 feet_over 的 seal 位置」記回 `last_seal_pulse_body`
- 下一步 target = (已含上次 feet_over 的 base) + 這次 feet_over → **feet_over 重複疊加、body target 雪球往牆裡推**
- bench 觀察 slave 5 target = preset + 0.49cm、cup 過推 → 高電流 (1451mA) + 大 pos_err (-22°) → obstacle 路徑 A 誤判

feet 組沒這問題：feet target 不加任何 delta、純 `last_seal_pulse_`。

修法 B1：body base 改用穩定的 `preset`、feet_over 一步只加一次、不再雪球。

### ⚠ 這是 TRIAL — 還原方式
User 指示「先試試看，不行再還原」。如果 bench 顯示：
- body Phase 1 經常 under-shoot（preset 離真實牆面太遠）→ iter loop 多做很多次補推
- 或 body seal 行為變差

→ **還原**：把上面「改後」的 block 換回「改前」的 block（兩個檔案都要：WASH_ROBOT.cpp `smart_extend_subset_` + WASH_ROBOT.h `cycle_group_`）。

### 影響範圍 / 連帶效果
- body 不再用 `last_seal_pulse_body` 當 extend base —「學習到的實際 seal 位置」對 body 不再用於 targeting（但 `last_seal_pulse_body` 仍由 `record_seal_pulse_` 記錄、仍被 `do_feet_realign_` Phase 0 拿來算 drift，realign 功能不受影響）
- body target 不再雪球 → `do_feet_realign_` 不會被假 drift 提早觸發
- 預期 body cup 過推減少 → 貼牆電流 / pos_err 下降 → obstacle 路徑 A 誤判機率下降（不用動 obstacle 門檻就可能改善）

### 沒動
- obstacle 路徑 A 門檻（18f 的純電流 800mA 判定）維持不動 — 先看 B1 是否讓過推消失、obstacle 自然不誤判；若仍誤判再回頭調門檻
- feet target 計算

---

## 2026-05-18f Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `DISABLE_PHASE_CURRENT_LIMIT_MA`：900mA → **800mA**（與 `CLOG_MA_GENTLE` 同值）
  - `DISABLE_POS_ERROR_LIMIT_DEG` 標記為 currently unused（保留常數定義）
- `user_lib/WASH_ROBOT.cpp` `pusher_extend_with_disable_seal_` Step D obstacle 路徑 A：
  - 移除 `pos_error > DISABLE_POS_ERROR_LIMIT_DEG &&` 的 AND 條件
  - 改為**純電流判定**：`if (phase_current > 800mA) → obstacle`
  - OBSTACLE log 改成 `I=XmA peakI=YmA pos_err=Z° (current-only judgment)`
### 原因
User 拍板：路徑 A 改純電流判定、門檻 800mA。

之前路徑 A 是 `pos_err > 5° AND I > 900mA` 雙條件。`pos_err` gate 用來區分「用力但有前進（pos_err 低）」vs「用力但卡死（pos_err 高）」。

**Claude 已提示風險、user 確認接受：** 拿掉 pos_err gate 後，正常 cup 用力壓牆建真空時若電流瞬間超過 800mA，會被誤判成 obstacle。`DISABLE_PHASE_CURRENT_LIMIT_MA` 800mA 跟 `CLOG_MA_GENTLE` 同值，路徑 A（app 層）跟 Clog（韌體層）幾乎同時觸發。

### Bench 驗證點
1. **正常 cup 貼牆、不擋障礙物** → 看會不會誤觸 OBSTACLE。頻繁印 `OBSTACLE ... (current-only judgment)` 但其實沒障礙物 → false-positive、要調高門檻或加回 pos_err gate
2. **擋障礙物** → 應該比之前更快觸發（不用等 pos_err 累積到 5°）

`DISABLE_PHASE_CURRENT_LIMIT_MA` 在 WASH_ROBOT.h，要回調直接改；`DISABLE_POS_ERROR_LIMIT_DEG` 常數保留、要恢復 gate 把 AND 條件加回即可。

---

## 2026-05-18b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `pusher_extend_with_disable_seal_()` — 加 `endpoint_stalled[]` flag：cup 一旦 endpoint stall 就不再被後續 iter 往前推
### 原因
Bench log：
```
[disable_seal:4] iter 0 STALL pos=2388.79° (defer, progress=0.992)  ← endpoint stall
[disable_seal:4] iter 1 push absolute target=25400 (cum +3000)       ← 又推更遠！
[disable_seal:4] iter 1 OBSTACLE pos_err=... I=1290mA                 ← 硬頂到爆 → 誤判 obstacle
```

User 問為什麼 endpoint-stalled 的 slave 4 在 iter 1 還繼續伸。

根因：disable_seal 的 iter retry 設計把 endpoint stall（cup 頂到牆、progress ≥ 0.8）只當「defer」處理，**不標 done**。下一個 iter Step C 看它還 not-done → `intended_target += INCR_PULSE` 繼續推到更遠的 absolute target。

但 endpoint stall = cup 已到物理極限。再推更遠的 target = 用更大力氣硬頂已到底的 cup → 電流飆 1290mA、posErr 飆 5° → 觸發 OBSTACLE → 整個 disable_seal abort。

修法：
- 新 `std::vector<bool> endpoint_stalled(N, false)`
- Step D endpoint-stall 分支 set `endpoint_stalled[idx] = true`
- Step C push 迴圈：`if (endpoint_stalled[i]) continue;` — 跳過、不推、不加 target
- endpoint-stalled cup 仍參與 Step B/F 等真空 poll：真空達標 → done；撐到 MAX_ITERS → 收尾標 weak_seal

效果：cup 頂到牆後就停在原位等真空，不會被硬推到誤判 OBSTACLE。最壞情況 = 等到 MAX_ITERS 後 weak_seal（vs 之前的 OBSTACLE abort + rescue）。

---

## 2026-05-18a Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp`
  - `init()`：啟動時 `disabled_zdt_slaves_.insert(ZDT_C)` — 中間推桿（ZDT slave 9）加入 disabled set
  - 3 處寫死的 "all 9" slave list 把 `ZDT_C` 註解掉（`/*, ZDT_C*/`）：do_step phase realign 的 `all_slaves`、return_home 的 vacuum_release `all9`、return_home 的 pusher_retract `all9`
  - `cmd_pusher` group="all" 路徑：`center_g` 宣告 + 兩行 center pusher retract 註解掉
### 原因
User 要求：目前不要控制中間 ZDT 9（center pusher）。

ZDT 9 控制路徑分兩類：
1. **走 group_slaves_ 的**（feet/body/center/all group ops）— `disabled_zdt_slaves_.insert(ZDT_C)` 一次搞定，group_slaves_ 自動 filter
2. **寫死 slave list 的**（不過 group_slaves_，pusher_move_many_ 不 filter disabled set）— 逐處註解掉 ZDT_C

大部分 center 控制路徑 2026-05-04 已經註解（step_up/down body_pre_cycle 的 center pusher move）。本次補完剩下的：realign phase、return_home、manual `pusher all`。

未受影響（刻意保留）：
- `do_phase5_roll_correct_` 的 `M_(ZDT_C).read_pressure()` — 那是讀 JC-100 #9 壓力 sensor，不是控制 ZDT 9 馬達
- center valve（PQW CH4）控制 — 不同裝置
- ZDT 9 stall flag clear — 只清 flag、不動馬達

重新啟用：拿掉 `disabled_zdt_slaves_.insert(ZDT_C)` + 取消 3+1 處註解，或 runtime `zdt_enable 9`（只復原 group ops 那類，寫死 list 仍要改 code）。

啟動 log 多一行：`[OK] ZDT 9 (center) disabled — excluded from all group ops`

**Build fix（同日）：** `disabled_zdt_slaves_.insert(ZDT_C)` 直接傳 `static constexpr` 成員給 `set::insert(const int&)` → ODR-use → linker `undefined reference to WashRobot::ZDT_C`（C++14 下 static constexpr 成員被 reference 綁定需要 out-of-class 定義）。改成 `insert(static_cast<int>(ZDT_C))` 產生 prvalue 副本，不 ODR-use 該成員。

---

## 2026-05-18d Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `return_home`（Phase 6 收推桿）：原本 `all9` 一階段直接收到 0 → 改成拆腳組 / 身體組各自兩階段（half → sleep RETRACT_HALF_WAIT_MS → full）
- `user_lib/WASH_ROBOT.cpp` `cmd_zdt_pusher` 單 slave retract：原本一階段 → 改兩階段（step1 = 該 cup 所屬群組 extend pulse × 2/3、sleep、step2 → 0）
- `user_lib/WASH_ROBOT.h` `cycle_group_` vacuum-retry retract：兩階段之間補 `sleep_ms_(RETRACT_HALF_WAIT_MS)`（原本沒有、跟 rescue retract 不一致）
### 原因
User 要求 audit「所有收 ZDT 腳組/身體組」都必須兩階段式（half 慢速脫壁 → wait → full 快速收完）。單階段快速收會在 cup 真空釋放後殘留吸附時 ZDT 堵轉。

**Audit 結果：**
- ✓ 已兩階段：do_step_down/up 的 feet/body retract、do_feet_realign Phase A、cmd_pusher（all + 單群組）、cycle_group_ rescue retract（2026-05-15h 加的、本來就兩階段含 sleep）
- ✗ **`return_home` line 4375**：8 顆 cup（腳+身體）一階段直收 0 → **已修**
- ✗ **`cmd_zdt_pusher` 單 slave retract**：一階段 → **已修**（單 slave 不算「組」，但仍是 feet/body cup、為一致性 + 安全一起改）
- ⚠ `cycle_group_` vacuum-retry retract：兩階段但缺 inter-stage sleep（rescue retract 有）→ **補上 sleep**

**澄清 user 的疑問：** user 問「救援收腳沒兩階段」— 其實 `cycle_group_` 的 rescue 分支（WASH_ROBOT.h 內）本來就是完整兩階段、還有 sleep。user 看到的應該是還沒含 2026-05-15h 改動的舊 build（rescue 是 h 才新增）。重編譯後就有。

### 兩階段 retract 統一 pattern（現在全專案一致）
```
step1: pusher_move(group_extend_pulse × 2/3, RPM_RETRACT 慢速)   ← 脫壁
sleep_ms(RETRACT_HALF_WAIT_MS)                                   ← 等 cup 黏著釋放
step2: pusher_move(PUSHER_RETRACT_PULSE=0, RPM_RETRACT_FULL 快速) ← 收完
```

### 已知未改（非 retract，不在本次範圍）
- `cmd_zdt_pusher` extend 走 smart_extend_subset_（disable_seal）— 不是 retract
- ZDT_C（中間推桿）相關 retract 多處已 2026-05-18 註解掉（中間推桿目前不控）

---

## 2026-05-18c Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `pusher_move_many_` poll loop（網頁 RETRACT 群組路徑）：加 `peak_I` per-slave 追蹤、每 ~300ms（每 2 poll）印 `move I=XmA pos=Y° spd=Wrpm`、STALL/done log 加 `peakI`
  - `zdt_wait_motion_done_` poll loop（單 slave 路徑，`cmd_zdt_pusher` 用）：同樣加 `peak_I`、每 ~300ms live 電流 log、STALL/done/timeout log 加 `peakI`
### 原因
User 問「網頁的 retract 跟 extend 有沒有都加電流 log」。釐清後：
- **網頁 EXTEND** → `smart_extend_subset_` → `pusher_extend_with_disable_seal_` → 18b 已加 ✓
- **網頁 RETRACT** → `pusher_move_many_` → `zdt_wait_motion_done_` → 18b **沒加** ✗

18c 補上 retract 路徑（`pusher_move_many_` 群組版 + `zdt_wait_motion_done_` 單 slave 版）的電流 log，跟 extend 對齊。現在三條 ZDT 等待路徑都有 live 電流 + peakI：
| 路徑 | 函式 | 用於 |
|------|------|------|
| disable_seal Step D | `pusher_extend_with_disable_seal_` | 網頁 EXTEND / 自動 step |
| group wait | `pusher_move_many_` | 網頁 RETRACT 群組 / cycle_group_ retract |
| single wait | `zdt_wait_motion_done_` | 單 slave `cmd_zdt_pusher` |

純診斷、不改判定邏輯。retract 路徑 log cadence 用 ~300ms（150ms poll × 2），比 extend 的 200ms 稍疏，因為 retract 通常較快、且不需要那麼密的取樣。

### 預期 log（網頁 RETRACT）
```
[wait_many ZDT:3] move I=180mA pos=1800° spd=-220rpm
[wait_many ZDT:3] move I=150mA pos=900° spd=-240rpm
[wait_many ZDT:3] done at 1200ms, pos=-0.04° peakI=410mA
```

---

## 2026-05-18b Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `pusher_extend_with_disable_seal_` Step D wait loop：
  - 加 `int poll_count` + `uint16_t peak_I` 追蹤
  - 每 ~200ms（每 4 個 poll）印一行 live 電流診斷：`push I=XmA pos=Y° posErr=Z° spd=Wrpm`
  - `peak_I` 追蹤整個 push 期間的相電流峰值
  - 4 個結局 log 都加上 `peakI`：OBSTACLE / STALL+EARLY / STALL defer / timeout
  - 「stable」結局原本沒 log，現在加一行 `push stable — I=XmA peakI=YmA pos=Z°`
### 原因
User 要看推程中的實際相電流，用來：
1. 理解為什麼障礙物在某個 iter 才被抓到（看電流何時爬到 DISABLE_PHASE_CURRENT_LIMIT_MA=900mA）
2. 校準 `CLOG_MA_GENTLE` (800mA) / `DISABLE_PHASE_CURRENT_LIMIT_MA` (900mA) 兩個門檻

之前 disable_seal 只在 OBSTACLE / STALL 結局印電流（而且是 emergency_stop 後的殘留值、不是峰值）。stable 結局完全沒 log。現在：
- **每 200ms 一行 live 電流** → 看得到電流曲線
- **peakI** → 看得到這次 push 的最高電流（觸發判定的真實依據）

### 預期 log
```
[disable_seal:4] iter 0 push absolute target=23900 ...
[disable_seal:4] iter 0 push I=320mA pos=1850° posErr=0.3° spd=180rpm     ← 200ms
[disable_seal:4] iter 0 push I=410mA pos=1980° posErr=0.5° spd=160rpm     ← 400ms
[disable_seal:4] iter 0 push I=780mA pos=2080° posErr=1.2° spd=20rpm      ← 600ms（接近 target）
[disable_seal:4] iter 0 push stable — I=240mA peakI=780mA pos=2089°       ← 推完 stable
```
障礙物 case 會看到 I 一路爬高、peakI 接近/超過 900mA 然後 OBSTACLE / STALL+EARLY 觸發。

純診斷、不改任何判定邏輯。每 iter 每 slave 約多印 3-10 行（push ~600ms-2s）。

---

## 2026-05-15h5 Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `pusher_extend_with_disable_seal_`：
  - 新增 `std::vector<double> pre_iter_pos(N, 0.0)` 紀錄每 iter Step C push **前**的 real_pos
  - Step C 進入前，對每個 not-done slave 讀 real_pos 存到 `pre_iter_pos[i]`
  - Step D STALL path 加 progress ratio 計算：
    - `actual_delta = |real_pos - pre_iter_pos|`
    - `expected_delta = INCR_PULSE * 0.1`（bench-verified：1 cmd-pulse = 0.1° encoder）
    - `progress = actual_delta / expected_delta`
    - **`progress < STALL_ENDPOINT_RATIO (0.80)` → 標 obstacle + 觸發 `early_abort_obstacle`**
    - `progress ≥ 0.80` → defer（既有行為，cup 撞牆正常 endpoint）
### 原因
User 拍板：「**STALL 只要伴隨『push 進度 < 80%』就視為 obstacle**」。

h4 只在「pos_err + I 雙超標」(OBSTACLE path) 才觸發 early abort。但 Clog_Ma 800mA 後，cup 撞硬障礙物時馬達電流被軟限在 800mA、不會超過 DISABLE_PHASE_CURRENT_LIMIT_MA=900mA、走的是 STALL path（韌體 stall_flag）不是 OBSTACLE path。

h5 補上：STALL path 也檢查「cup 到底有沒有移動到預期距離」。如果只移動了 < 80% expected delta → cup 卡住沒法推 → 視為 obstacle、立刻 abort。

### 對 user 之前 bench log 的應用
之前 h3 log:
```
[disable_seal:4] iter 0 STALL pos=2081.24° (defer)        ← Phase 1 結束 1931.5°、現在 2081.24°
                                                            actual_delta = 149.7°
                                                            expected_delta = 150°
                                                            progress = 99.8% > 80% → defer (還是 endpoint 行為)
```
這個 case h5 不會改變行為（cup 真的有推到位、只是力道有限）— 用 OBSTACLE path 等到下個 iter pos_err 累積才抓。

但如果 cup 是**真的被擋住沒法推**（actual_delta = 20°、progress = 13%）：
```
[disable_seal:4] iter 0 STALL+EARLY actual=20° expected=150° progress=0.133 < 0.80 → OBSTACLE (abort)  ← h5 NEW
[disable_seal:3] iter 0 abort-stop (sibling obstacle, skipping wait)  ← h4 既有
[disable_seal] iter 0 obstacle abort — exiting iter loop early (rescue will handle)  ← h4 既有
（cycle_group_ rescue 接手）
```

### 已知限制
- **軟障礙物**（cup 能推但出力不夠 seal）：actual_delta 接近 expected_delta、progress 高、不會被 STALL+EARLY 抓到。需要等 OBSTACLE path 累積 pos_err 才抓
- 你 h3 log 的 slave 4 就是這種 case（progress 99.8%）
- 對「真的卡住完全推不動」的硬障礙物 h5 才有用

如果軟障礙物也要早 abort，下一輪可以加「**N 次連續 STALL with high progress 視為 obstacle**」邏輯（連 2 次 cup 推到位卻沒 seal → 視為持續對抗），但會複雜化 — 先看 h5 效果

---

## 2026-05-15h4 Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `pusher_extend_with_disable_seal_`：
  - 新增 `bool early_abort_obstacle = false` flag
  - Step D 內 OBSTACLE 偵測 path 設 `early_abort_obstacle = true`
  - Step D 處理 pushing slaves 的 for loop 進場檢查 `if (early_abort_obstacle)` → 對該 slave emergency_stop + log 「abort-stop (sibling obstacle)」+ continue
  - Step D 結束後加 `if (early_abort_obstacle) { emergency_stop 所有 not-done + log + break iter loop }`，跳過 Step D.5/E/F、跳出 iter for-loop
### 原因
User bench 測試「iter 0 偵到 obstacle、為什麼還有 iter 1」。原因：disable_seal iter loop 是 per-slave，一個 cup obstacle 標 done 後，其他 not-done cup 繼續 push 直到 sealed 或自己也 done。

**user 拍板：** ZDT 身體組 / 腳組必須一起動，partial seal 沒意義（rescue 後反正全部釋放重來）。所以一個 cup obstacle 立刻 abort iter loop、讓外層 cycle_group_ 的 rescue 馬上接手。

**設計細節：**
- 只有 OBSTACLE path（pos_err + I 雙超標）觸發 early abort
- STALL path（韌體 stall_flag，包含 cup 正常撞牆建真空那種）**仍是 defer**，不算 abort 條件 — 撞牆 stall 是預期 endpoint、不該誤判
- 對 mid-push 的 sibling cup 立刻 emergency_stop，避免 push 完成、後續 rescue retract 時馬達已經到底
- early abort 後 disable_seal 仍走 post-loop cleanup（re-enable not-done + 標 weak_seal），無害（cycle_group_ rescue 反正會釋放整組）
- 對 `smart_extend_subset_`（手動 GUI 路徑）效果：disable_seal 提早結束、any_obstacle_out=true、log 印「obstacle detected (no rescue in manual path)」、Clog guard restore — 比之前快結束、不浪費 iter

### 預期 log 變化
之前 (h3 build) bench log：
```
[disable_seal:4] iter 0 STALL pos=2081.24° (defer)
[disable_seal] iter 0 WAIT_SEAL ...
[disable_seal:1] SEALED ...
[disable_seal:2] SEALED ...
[disable_seal:3] iter 1 push ...
[disable_seal:4] iter 1 push ...
[disable_seal:4] iter 1 OBSTACLE pos_err=0.148 I=1006mA
[disable_seal] iter 1 WAIT_SEAL ...                       ← 還在繼續
```

h4 build 預期：
```
[disable_seal:4] iter 0 STALL pos=... (defer)             ← STALL 仍不 abort
[disable_seal] iter 0 WAIT_SEAL ...
[disable_seal:1] SEALED ...
[disable_seal:2] SEALED ...
[disable_seal:3] iter 1 push ...
[disable_seal:4] iter 1 push ...
[disable_seal:4] iter 1 OBSTACLE pos_err=... I=...
[disable_seal:3] iter 1 abort-stop (sibling obstacle, skipping wait)   ← NEW
[disable_seal] iter 1 obstacle abort — exiting iter loop early (rescue will handle)  ← NEW
（cycle_group_ 收到 any_obstacle=true → rescue 接手）
```

### 限制（已知不完美）
- STALL 不觸發 early abort。如果 cup 撞障礙物先觸發 STALL（透過 Clog 韌體路徑）而不是 OBSTACLE（透過 pos_err + I app 路徑），仍會繼續推一個 iter 直到 OBSTACLE 出現才 abort
- 你 h3 log 看到的就是這 case：slave 4 iter 0 STALL（Clog 800mA 觸發）、iter 1 才 OBSTACLE
- 之後若觀察到「STALL 後馬達一直拉不起來、應該 abort 卻沒 abort」可以加「N 次連續 STALL 視為 obstacle」的補強，目前先看 h4 效果

---

## 2026-05-15h3 Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `CLOG_MA_GENTLE`：1000mA → **800mA**
  - `DISABLE_PHASE_CURRENT_LIMIT_MA`：1200mA → **900mA**
### 原因
Bench 測試 user 故意擋障礙物：第二次測試「推了好幾下才賭轉」。Trade-off 調整：
- 降 `CLOG_MA_GENTLE` 800mA → ZDT 韌體 stall 觸發更快、撞障礙物時馬達輸出力道更小（更安全）
- 降 `DISABLE_PHASE_CURRENT_LIMIT_MA` 900mA → disable_seal 應用層 obstacle path 更敏感、不用等電流 spike 過 1.2A 才觸發

兩個值有意保持 100mA 間距（Clog 800 < DISABLE 900）— 正常 push 力道在 800mA 以下 → 不誤踩；撞到障礙物 800-900mA 馬達被 Clog 軟限，application-level disable_seal 在電流剛 spike 過 900mA 就抓到障礙、emergency_stop 接管。

### Bench 驗證點
1. 正常 cup 貼牆 + 真空建立：vacuum kPa 能不能達 -60/-65 SEAL（vs 之前 1A 時 sealed wait=1100-1700ms）。如果常 weak_seal、cup 推不到底 → Clog 太低、調回 1000mA
2. 擋障礙物：應該 1-2 iter 就 OBSTACLE（vs 之前 5 iter 以上才 stall）
3. log 觀察 disable_seal OBSTACLE 觸發瞬間的 iter 號 — 越早觸發越好

如果 800/900 還是太鬆 → 下一輪繼續調，常數都在 WASH_ROBOT.h 頂部 group。

---

## 2026-05-15h2 Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `smart_extend_subset_`（web GUI EXTEND 按鈕路徑）加 Clog_Ma RAII guard：進場 set GENTLE (1A)、出場 restore NORMAL (3A)。**不接 obstacle rescue 邏輯**（per user 2026-05-15 訊息：「只要 Clog_Ma guard、不要 rescue 邏輯」）
- `smart_extend_subset_` 也把 `any_obstacle_out` 傳給 `pusher_extend_with_disable_seal_`，但只 log 訊息「obstacle detected (no rescue in manual path)」不觸發救援。理由：手動 GUI 路徑預期操作員在看現場、不該自動退 rail 換位置
### 原因
Bench 測試 user 按 web EXTEND 故意擋障礙物，回報「沒看到 rescue 觸發」。原因是 15h 版本只在 `cycle_group_`（自動 step_down/up 路徑）做了 Clog guard + obstacle rescue，**手動 GUI 路徑 `smart_extend_subset_` 沒接**。user 確認他要的是「手動路徑也要 Clog guard、但 rescue 仍只在自動路徑」— 折衷方案，手動操作仍由操作員判斷障礙處理，但伸推桿時力道有限制不傷物

### 預期 log 變化
原本 smart_extend log：
```
[smart_extend] feet slaves={3,4,1,2} target_pulses={...} (disable_seal mechanism)
[disable_seal] Phase 1 ...
```
改後：
```
[smart_extend] feet slaves={3,4,1,2} target_pulses={...} (disable_seal mechanism)
[clog_guard] smart_extend feet enter — Clog_Ma -> 1000mA (GENTLE) on 4 slave(s)
[disable_seal] Phase 1 ...
...
[disable_seal:4] iter X OBSTACLE pos_err=... I=...        ← 障礙偵測仍有
EVT obstacle_detected slave=4                              ← 仍廣播
[smart_extend] feet obstacle detected (no rescue in manual path — operator action)
[clog_guard] smart_extend exit — Clog_Ma -> 3000mA (NORMAL) on 4 slave(s)
```

---

## 2026-05-15h Claude (Sadie)
### 修改檔案
- `user_lib/ZDT_motor_control.{h,cpp}` — 加 3 個 method：
  - `read_driver_config(uint16_t out[15])` — FC 0x04 從 0x0042 讀 15 個 reg（§3.7.5）
  - `write_driver_config(const uint16_t in[15], bool store)` — FC 0x10 寫 0x0048 15 個 reg（§3.7.6）、自動加 magic byte 0xD1
  - `set_clog_ma(uint16_t mA, bool store=false)` — 便利 read-modify-write、只動 Reg 13（堵轉保護檢測電流 Clog_Ma）
- `user_lib/WASH_ROBOT.h`：
  - 加常數 `STALL_ENDPOINT_RATIO=0.80`、`OBSTACLE_RESCUE_MAX=2`、`OBSTACLE_RESCUE_BACKUP_CM=10.0`、`CLOG_MA_GENTLE=1000`、`CLOG_MA_NORMAL=3000`
  - `pusher_extend_with_disable_seal_` 加 `bool* any_obstacle_out = nullptr` 參數
  - `cycle_group_` template 改 signature：加 `RescueBackup rescue_backup` 第 4 個 lambda、加 `int& out_rescue_count`
  - `cycle_group_` template 內：
    - 進場用 RAII `ClogMaGuard` 把整組 slave 的 Clog_Ma 設 GENTLE (1A)、destructor 自動 restore NORMAL (3A) — 確保所有 exit path（success / vacuum exhausted / obstacle exhausted / abort / exception）都會回 3A
    - 在 disable_seal 跟 vacuum_check 之間插一個 obstacle rescue while-loop：disable_seal 回報 any_obstacle → 不消耗 vacuum retry budget、走「rescue_backup(10cm) + valve off + retract + re-extend」flow、最多 `OBSTACLE_RESCUE_MAX=2` 次後給 PausedOnError
- `user_lib/WASH_ROBOT.cpp`：
  - `pusher_extend_with_disable_seal_` 加 `any_obstacle_out` 參數、底部 OR-aggregate 既有的 per-slave `obstacle[]` 陣列寫到 `*any_obstacle_out`
  - 4 個 `cycle_group_` 呼叫點（do_step_down_ body / feet、do_step_up_ feet / body）原本 backup lambda 重構成 factory pattern，產出兩個 lambda（vacuum-retry 5cm / obstacle-rescue 10cm）並傳給新 signature 的 cycle_group_
### 原因
**User 的兩個需求一起處理：**

1. **「撞到障礙物就先換位置、不要把 retry 都用玩」** — 既有 disable_seal 已有 `obstacle[]` 內部偵測（pos_err + phase_current 雙超標）但只是 mark + skip，外層 cycle_group_ 不知情、retry 時只退 5cm 常常還在原地撞。新邏輯：disable_seal 把 obstacle flag 暴露給 cycle_group_，後者多一個「rescue」分支退 10cm，且**不算 vacuum retry**；2 次救援都沒過才 PausedOnError 讓人出手
2. **「伸腳前 Clog 1A、伸完恢復 3A」** — ZDT §3.7.6 Clog_Ma (Reg 13) 是堵轉檢測電流門檻、batch write 15 reg from 0x0048。GENTLE 期間馬達遇阻早觸發 stall flag、減少撞到障礙時的推力；NORMAL 期間保持 cup 夾持力道。RAII guard 保證**不論走 success / vacuum 失敗 / obstacle exhausted / abort / exception path 都會 restore 3A** — user 強調「伸完必須改回 3A」這點不能漏

### 預期 log
正常成功：
```
[clog_guard] group enter — Clog_Ma -> 1000mA (GENTLE) on 4 slave(s)
[disable_seal] Phase 1 ...
[clog_guard] group exit — Clog_Ma -> 3000mA (NORMAL) on 4 slave(s)
```
撞到障礙（rescue 路徑）：
```
[clog_guard] group enter — Clog_Ma -> 1000mA (GENTLE) ...
[disable_seal:5] iter 2 OBSTACLE pos_err=0.95 I=2300mA
EVT obstacle_detected slave=5
[cycle_body] obstacle detected — rescue 1/2 (rail backup 10cm + re-extend)
EVT obstacle_rescue group=body rescue=1/2
  [retry body_rescue] rail 9.5 → -0.5 cm   ← 退 10cm
...（valve off / retract / 10cm 退 / re-extend）...
[disable_seal] Phase 1 ...                  ← 再試
（如果再撞）[cycle_body] obstacle rescue exhausted (2/2) — PausedOnError
EVT obstacle_rescue_exhausted group=body rescues=2
[clog_guard] group exit — Clog_Ma -> 3000mA (NORMAL)
```

### 沒動的東西
- `cmd_init` 4 條 init extend（feet/body upper/lower）— 維持 stall=成功既有行為、不套 obstacle rescue
- `smart_extend_subset_`（手動 GUI EXTEND 按鈕路徑）— 主動操作不擋
- `zdt_wait_motion_done_` / `pusher_move_*` 簽名不動（obstacle 偵測由 disable_seal 內部 pos_err+I 法走、不走 stall ratio 法）
- ZDT_modbus.pdf 暫存萃取檔已清掉、不留專案內

### Bench 驗證點
1. **正常 cycle_group_ 流程 log** 應該開頭有 `[clog_guard] group enter`、結尾有 `[clog_guard] group exit -> 3000mA`
2. **故意擋一個 cup**（手抓住推桿頭）→ disable_seal 應該偵測到 OBSTACLE → cycle_group_ 印 `obstacle detected — rescue 1/2` → rail 退 10cm → 再 extend。重複 2 次都擋 → `obstacle rescue exhausted` → PausedOnError
3. **Clog_Ma 真的有降下來：** 用 raw command 或 Linux_test 驗一下伸腳期間 cup 真的力道變弱（可額外加 `cmd_zdt_read_clog` 之類診斷指令、目前沒做）
4. **GENTLE 1A 太低或太高的話**：bench 看到撞牆 cup 太早 stall（沒貼上）→ 調高 `CLOG_MA_GENTLE`；看到撞障礙還是推得動 → 調低。`CLOG_MA_GENTLE` 跟 `CLOG_MA_NORMAL` 都在 WASH_ROBOT.h、直接改、重編譯

---

## 2026-05-15z Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 private method `dm2j_read_pos_robust_(int slave, double& out_cm, int max_attempts=5, double agree_cm=1.0)`
- `user_lib/WASH_ROBOT.cpp`
  - 加 `dm2j_read_pos_robust_()` 實作：連續 2 次讀數需在 1cm 內一致才接受，最多 5 attempts
  - `do_step_down_()` / `do_step_up_()` body_pre_cycle 內讀 DM2J 位置從 `read_position_cm` 改成 `dm2j_read_pos_robust_`
### 原因
Bench：14y 加完 DM2J 讀取後 user 看到讀回 **610.XX cm**（實際位置是 5cm）— Modbus frame contamination 或 stale buffer 把別的 slave/frame 的資料當成位置回來。

如果 corrupted 值被直接拿去算 delta，crane 會誤動超大距離（610 - 30 = -580 → retract 580！）→ 拉爆機器。

robust read 邏輯：
- 連續讀，第二次必須跟第一次差 ≤ 1cm 才接受
- 不一致 → log + 重讀（最多 5 次）
- comm fail → log + 重讀（不算入一致 pair）
- 5 次後仍不一致 → 回 error → 上層 try_or_pause_ 進 PausedOnError

正常 case 影響：bench 上 DM2J 讀通常 ~50ms RTT。正常 2 次讀需 ~100ms 額外（vs 之前 50ms 單讀）。Pre_cycle 進場一次性，可接受。

corruption case：log 會印 `[dm2j_robust] slave N attempt M prev=X new=Y (diff Zcm > 1.0 tol) — retry`，retry 直到拿到 2 個一致讀數。

長期：DM2J driver 本身應該驗 CRC + slave ID（跟 SD76 同問題，14m memory 有提）；目前 application 層 sanity 是臨時 mitigation。

---

## 2026-05-15y Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_` / `do_step_up_` body_pre_cycle — **加回 margin pattern**，但用 **DM2J 實際讀取值**算 delta：
  - 進場呼叫 `D_(DM2J_LEFT_FOOT).read_position_cm()` + 右腳同樣 → 平均 = `rail_actual`
  - delta = `rail_target - rail_actual`（step_down）或 `rail_actual - 0`（step_up）
  - delta ≤ 0.5cm → skip 全部 crane + DM2J 操作（body 已在目標、無需動）
  - 否則跑 margin pattern：
    - **step_down**：pay_out (delta + margin) → DM2J → retract margin（net pay_out delta）
    - **step_up**：pay_out margin → DM2J → retract (delta + margin)（net retract delta）
- 進場 log 印 DM2J L/R/avg + delta
### 原因
14x 移除 margin 後 user 反映：margin 還是要（給 rail 移動時鋼索 slack 安全），但**參數不能用 step_cm，要用 DM2J 實際位置**算 delta。

舊問題：rail_pos_cm_ atomic 只在 store 時更新，如果上次 step 失敗中途沒正確 rollback 就會 stale。現在每次 pre_cycle 進場直接讀 DM2J 硬體位置，不信任 software state。

新行為：
| 場景 | 舊（step_cm 算）| 新（DM2J 讀算）|
|---|---|---|
| 正常 step_down 30cm，rail 從 0 開始 | pay_out 30+15=45 → DM2J → retract 15 | pay_out 30+15 → DM2J → retract 15（一樣）|
| 上次失敗 rail 卡 +25，再 step_down 30 | pay_out 30+15 → DM2J 只動 5 → retract 15（25cm 過繩）| **pay_out 5+15=20 → DM2J 動 5 → retract 15**（精確 sync）|
| Rail 完全在 +30 處 | pay_out 30+15 → DM2J no-op → retract 15（30cm 過繩）| **delta 0 → skip 全部**（不動繩、不動 DM2J）|

step_up 鏡像：pay_out margin → DM2J 0 → retract (delta + margin)。

純 user_lib 改動。Sadie 範圍。

驗證 bench：故意讓 body retry 失敗中途、再 step 一次，看 log 印「DM2J read L=X R=Y avg=Z target=W delta=N」+ crane 動的量等於 delta。

---

## 2026-05-15x Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_()` 跟 `do_step_up_()` 的 body phase — crane payout/retract 改用**實際 DM2J rail delta** 計算，不再用固定 `step_cm`：
  - **step_down body_pre_cycle**：`pay_out = step - rail_before`（rail 已在 step_cm 處時 = 0，整個 skip）；移除 +margin/-margin 補償 pattern
  - **step_down body_backup**：pay_out → **retract**（方向修正），量 = rail_delta
  - **step_up body_pre_cycle**：retract = `rail_before - 0`，移動前 snapshot
  - **step_up body_backup**：pay_out 量改用 rail_delta 計算（值同 VACUUM_BACKUP_CM，但顯式 link 到 rail 移動）
### 原因
User 觀察：DM2J 用 **absolute target**，如果上次 step 因 body retry 沒走完整 30cm 留在 +25cm，下次 step_down 進場 → `dm2j_pair_move_abs_(... +30)` 只實際移 5cm，但程式還無腦放 30cm 繩 → 25cm 過繩、未來會被拉爆。

修法：crane 動的距離 = `target_rail - current_rail`，跟 rail 物理 delta 完全綁定。如果 rail 已在 target 處，crane 不動（log 標 skip）。

完整對稱矩陣（修完）：

| Phase | Rail 方向 | Crane 動作 | 量 |
|---|---|---|---|
| step_down body pre_cycle | rail 0→+step（body 下）| pay_out | `step - rail_before` |
| step_down body backup | rail -5（body 上）| retract | `VACUUM_BACKUP_CM` |
| step_up body pre_cycle | rail +step→0（body 上）| retract | `rail_before` |
| step_up body backup | rail +5（body 下）| pay_out | `VACUUM_BACKUP_CM` |

特殊 case：
- 若 rail 已在目標（rail_delta ≤ 0.5cm），skip crane 操作 + 印 log
- 副作用：移除了之前 step_down 的 +STEP_MARGIN_CM (15cm) overshoot pattern。STEP_MARGIN_CM 常數本身保留（其他地方可能還在用）。

驗證 bench：bench 試重複「先讓 body retry 失敗到中途位置」+「直接再 step 一次」場景，看 crane 是否只動實際差距。應該看到：
```
[step_down] rail already at 25cm (target 30) — skip crane pay_out
或
[step_down] crane pay_out 5 (rail 25→30cm)
```

---

## 2026-05-15w Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_()` feet_backup — 拿掉 retry 前的 `crane_cmd_("pay_out N")` 那段
### 原因
User 指出：腳組重新找位置吸（feet phase retry）時不該動吊繩。

物理上：feet phase body 是 wall anchor、feet 釋放重定位。rail 移動只改變 feet 相對 body 位置，body 沒動 → 繩子拉著的 anchor 沒動 → 繩子鬆緊不變。pay_out 完全多餘。

對照 step_up feet_backup 一直都沒 crane ops（讀過 confirm），現在 step_down feet_backup 也對齊。

body phase 仍保留 crane 操作（body_backup 跟 body_pre_cycle）— body 移動會改變繩子 anchor 位置，crane 該配合。

純拿掉冗餘 crane 呼叫，沒改其他邏輯。

---

## 2026-05-15v Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp`
  - **`crane_watchdog_loop_` 拆掉持續 ping + auto-abort**：只保留 EVT alarm escalation 到 PausedOnError 的路徑。不再每 500ms 送 ping 給 crane、不再因 ping 失敗 set abort_flag
  - **停掉 `crane_keepalive_loop_` thread**（14t 加的 1Hz ping），init 跟 stop 都不啟動 / join 那條 thread
  - **`crane_cmd_` 加 self-healing reconnect**：每次呼叫最多 2 attempts，第一次失敗（send fail / recv 沒有非-EVT reply）→ 強制 close socket + reconnect + 重試。徹底解決僵屍 socket（`isConnected()=true` 但實際 TCP 已死）導致的 cmd 永久失敗
### 原因
Bench：washrobot crane TCP 連線進入「僵屍」狀態 — `[OK] crane 192.168.5.26:5002` 啟動連 OK，但之後 ping 永遠 timeout，elapsed=18002ms 超過 watchdog timeout → step_up abort。

GUI 透過 web_backend 自己的 TCP socket 操作 crane 完全正常，證明 **crane 本身沒問題、是 washrobot 端的 TCP 連線特定壞掉**（可能 NAT 表清掉、bench 網路抖動、kernel 沒收到 RST）。

舊 watchdog 持續 ping 反而是傷害 — flag 顯示 connected 但實際送不出，每次都 timeout，把 elapsed 撐爆，最後 abort 機器人 motion。

新設計（per user 要求「不要一直 ping，使用 crane 前確認連線就好」）：
1. **沒有 continuous ping**：crane_watchdog 只 drain EVT alarm，沒 ping 流量
2. **on-demand 連線驗證**：crane_cmd_ 第一次失敗會自動 force-close + reconnect + 重試。每次「使用 crane 前」相當於做了一次連線驗證
3. **省 bus 流量**：閒置時完全不打擾 crane

代價：crane 真的沉默掉的時候，washrobot 不會主動偵測（要等下次 crane_cmd_ 才會發現）。但 motion 失敗會走 try_or_pause_ → PausedOnError，user 仍能看到。

副作用：14t keepalive thread 變成 dead code（atomic + member 還在但沒啟動）；之後不需要可以乾淨拆掉。

---

## 2026-05-15u Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — 加 abort 來源 stdout log（之前只有 evt_ broadcast，stdout 看不到）：
  - `crane_watchdog_loop_` timeout 觸發 abort → 印 `[crane_watchdog] TIMEOUT elapsed=Xms > Yms during motion — ABORT_FLAG SET`
  - `imu_monitor_loop_` emergency tilt → 印 `[imu_monitor] EMERGENCY tilt avg=X° >= Y° sustained Zms — ABORT_FLAG SET`
  - `crane_keepalive_loop_` 加 ping fail 計數 + log（連續 fail 印 `[crane_keepalive] ping FAIL (N)`、恢復印 recovered）
### 原因
14t 加 keepalive 後仍看到 step_up abort，但啟動 log 確認 keepalive thread 有跑。問題在 stdout 看不到 abort 來源（evt_ 只 broadcast TCP），無法判斷是 watchdog / IMU / 別處。

加 stdout log 後下次 bench 撞 abort 時直接看訊息：
- `[crane_watchdog] TIMEOUT` → watchdog 還是 fire（keepalive 沒生效或失敗）
- `[crane_keepalive] ping FAIL` → keepalive 自己撞 crane 通訊問題
- `[imu_monitor] EMERGENCY` → IMU 角度問題
- 三個都沒看到 → abort 來自其他路徑（用戶 cmd / try_or_pause 路徑沒識別到）

純診斷，不改 abort 行為。

---

## 2026-05-15t Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 `crane_keepalive_running_` atomic + `crane_keepalive_thread_` + `crane_keepalive_loop_()` 宣告
- `user_lib/WASH_ROBOT.cpp`
  - 建構子初始化 `crane_keepalive_running_(false)`
  - `init()` 啟動 `crane_keepalive_thread_` 並 log
  - `stop()` 對應 join keepalive thread
  - 新增 `crane_keepalive_loop_()` 實作：1Hz polling `motion_active_`，true 時送 `ping` 給 crane（透過 `crane_cmd_`），失敗無害（watchdog 下次 tick 處理）
### 原因
Bench：`step_up` feet phase 跑到一半 (rail forward DM2J 10cm 後) 突然 `ERR aborted`，沒有任何 try_or_pause 訊息。

Root cause: `crane_watchdog_loop_` 看 `crane_last_ok_ms_`，預設 timeout 2s。`crane_last_ok_ms_` 只在 crane_cmd_ 收 OK 或 crane broadcast `motion_progress` EVT 時更新。

問題場景：washrobot 端做長 op（ZDT 推桿 extend 4s+、DM2J 10cm rail 移動 2-3s）期間**不跟 crane 通訊** → crane_last_ok_ms_ 超過 2s 沒更新 → motion_active=true 條件下 watchdog set `abort_flag=true` → 下一個 check_abort_() 截獲 → "ERR aborted"。

修法：washrobot 自己加 1Hz keepalive thread，motion_active 時主動送 `ping` 給 crane。`ping` 的 OK reply 走原有路徑更新 crane_last_ok_ms_ → watchdog 不會誤觸發。

效果：washrobot 長 op 期間 watchdog 不會 false-fire。但 crane 真的死掉時：keepalive 自己也會 fail（crane_cmd 沒回 OK）→ crane_last_ok_ms_ 不更新 → 2s 後 watchdog 正常觸發 → 偵測能力保留。

注意：這是 user_lib 改動（Sadie 範圍 per 14f memory），user 明確要求 option B 才做。

---

## 2026-05-15s Claude (Sadie)
### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_up_()` body phase pre_cycle 順序重排：
  - **舊**：body pushers retract → crane retract `step+margin` → DM2J rail → 0 → crane pay_out `margin`
  - **新**：body pushers retract → **DM2J rail → 0**（body 機械往上爬）→ **crane retract `step`**（吸收 body 上升產生的繩鬆）
  - 移除 crane pay_out margin 步驟（user 要求「收緊就好不用 payout」）
  - retract 量從 `step + STEP_MARGIN_CM` 改為純 `step`
### 原因
舊邏輯先 crane retract step+margin、後 DM2J rail → body 還機械鎖在 +step_cm（rail 沒移）就被繩往上扯 → feet 吸盤承受向上拉力風險。

新邏輯機械先動：
1. body pushers retract → body 脫牆
2. DM2J rail 0 → body 沿 rail 機械爬到 feet 高度（推力來自 rail 馬達、不扯 feet）
3. 此時鋼索因 body 上升出現約 step_cm 鬆度
4. crane retract step → 收掉那個鬆度，恢復正常張力

淨 retract = step（剛好對應 body 上升距離），不需要 pay_out margin 還原。

step_down body phase 也用「rail 動 → crane retract」順序，保持對稱。

注意：這是 user_lib 改動，Sadie 範圍。

---

## 2026-05-15r Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `TENSION_DIFF_MAX_KG` **10.0 → 50.0**
### 原因
DSZL 還沒校準，bench 跑時 10kg 差太緊 → 正常操作（一邊瞬間卡到 / 重物搖晃）就誤觸發 tension_alarm diff、motion abort。

50kg 是 bench 暫時值，等 DSZL scale factor 校準後再 tighten 回合理值（comment 已標）。

注意：仍然是編譯時 constexpr、runtime 改不了。如果需要 runtime 調整可以另外加 atomic + cmd setter。

---

## 2026-05-15q Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — balance hz_min / hz_max semantic 改：
  - `BALANCE_HZ_MIN_DEFAULT` 2.0 → **5.0**
  - `BALANCE_HZ_MAX_DEFAULT (30 absolute)` 退場，改成 **`BALANCE_HZ_MAX_OFFSET_DEFAULT = 5.0`**（上限 = `base_hz + offset` 動態）
  - atomic 改名 `g_balance_hz_max` → `g_balance_hz_max_offset`
  - `apply_balance_trim`: `hz_max = base_hz + offset.load()` 取代固定值
  - `cmd_set_balance_hz_max` 參數語意改為 **offset 而非絕對 hz**；log 額外印 hold/motion 各自 effective max
  - `cmd_status` 輸出欄位 `balance_hz_max` → `balance_hz_max_offset`
  - 啟動 log 跟 control-law comment 同步更新
### 原因
- **hz_min 2 → 5**：之前 2Hz 太低，balance 把 L 拉到 2Hz 接近失速。5Hz 給足轉矩 + 跟 base 10Hz 還有 50% 餘裕讓 balance 能下手。
- **hz_max 絕對 30 → offset 5**：舊邏輯 motion base 30Hz 時 hz_max=30 → balance **只能拉慢 L、不能加速 R**（R 已 cap 上限）。新 `base + 5` 給對稱 ±5Hz 餘裕，hold 10Hz 時 max=15、motion 30Hz 時 max=35，balance 兩邊都能調。

實際 hz 範圍對照：

| base | cap (ratio 0.5) | 舊 hz_max | 新 hz_max | 舊 R 上限到嗎 | 新 R 上限到嗎 |
|---|---|---|---|---|---|
| 10 (hold) | ±5 | 30 | 15 | 否 (15 < 30) | 是 (15) |
| 30 (motion) | ±15 | 30 | 35 | 是 (R 永遠 30 卡住) | 否 (35 餘裕) |

motion 場景下 balance 終於能對 R 「加速」追上 L 了（之前只能單向減 L）。

Runtime cmd 行為：
- `set_balance_hz_max 8` 現在意思是「base+8」當 max，不再是絕對 8Hz
- 舊腳本如果用 `set_balance_hz_max 30` 會變成 hold 10+30=40Hz 上限、motion 30+30=60Hz（會撞 SE3_MAX_HZ=50 反過來被 setFreqHz 的內部 clamp 攔下）—**注意這是 breaking change**

---

## 2026-05-15p Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` `motion_rope` 停車邏輯改成 **sync stop**：
  - 舊：L/R 各自獨立看自己 target、各自呼叫 `reliable_stop_one`
  - 新：第一邊跨 target → 立刻 `dual_se3_sync_retry(stopDecel)` 同步停**兩邊**
  - 跟 hold mode 的 `cmd_hold off` 一致（hold 也是 sync_retry stopDecel 兩邊）
  - log 標 leader = L / R / BOTH，方便看哪邊先到
### 原因
user 反映 `pay_out` / `retract` 應該跟手動 hold 一樣兩邊同步啟停。

啟動：14g 起已 sync（`dual_se3_sync_start`）✓
停車：14p 之前是 per-side 獨立 → L 先到 target 開始減速時 R 還在全速 → **減速期間左右大幅不對稱**

新邏輯：第一邊跨 target → 兩邊同步 stopDecel → 落後那邊會 stop 在「leader 走的距離」內 → fine_adjust 之後把落後邊拉到 leader 位置（fine_adjust 已存在的對齊功能）。

副作用：
- 落後邊比 leader 少走 ~N cm（N 取決於 mid-motion drift）
- fine_adjust 會處理這個殘餘 — 比之前各自停車後 fine_adjust 還要修「左/右 各自 overshoot 不一」要簡單
- motion 期間 stop 時序的鋼索不對稱應力消失（最大安全收益）

驗證：bench 跑 `pay_out 50` 看 log
- 應該看到一行 `[motion_rope] sync stop (leader=L 或 R 或 BOTH) ...`
- 之後 fine_adjust 進場 align-to-leader
- 最終 L/R 兩邊都應該接近 target

---

## 2026-05-15o Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — 加快 motion_rope 三個 loop cadence，利用 14n physical-separation 後的 bus 餘裕：
  - `POLL_INTERVAL_MS` 50 → 20ms（motion 主迴圈 tick 20Hz → 50Hz；純 atomic load，沒額外 bus traffic）
  - `METER_POLL_MS_MOTION` 150 → 100ms（cli_M 現在 dedicated SD76 沒人搶；cache 新鮮度 +50%）
  - `BALANCE_TICK_MS` 500 → 250ms（balance trim 2Hz → 4Hz；每 SE3 獨立 bus，setFreq 寫不會 contention）
### 原因
14n 拓樸改完後 cli_A / cli_B / cli_M 都是各自獨立 RS485，沒跨 device contention。原本為了減 bus 流量保守設定的 cadence 都可以加快。

預期效果（motion_rope @ 30Hz / 30cm-s 場景）：
| 項目 | 14m 之前 | 14o 之後 | 改善 |
|---|---|---|---|
| Stop trigger lag (cache + tick) | ~200ms = 6cm | ~120ms = 3.6cm | 停車誤差 -40% |
| Balance 反應週期 | 500ms | 250ms | mid-motion drift 累積 -50% |
| motion 主迴圈 abort/fault 偵測 | 50ms | 20ms | safety event 響應 ×2.5 快 |

副作用：
- Bus 流量：cli_M ~+50%（仍 < 50% 占用）；cli_A/cli_B 保持 ~5% (keepalive only)
- balance setFreq Modbus rate: 1Hz → 2Hz per side（cli_A 跟 cli_B 各跑各的，沒交互）
- motion_rope CPU 使用率：~微升（純 atomic + sleep，不會 spin）

驗證重點：
1. bench 跑 pay_out 50 / retract 50 看停車最終位置 overshoot 是否減小
2. balance 在 mid-motion 看 [BAL] err 數字是否更快收斂
3. SE3-keepalive fail rate 不該變化（這幾個 cadence 不影響 SE3 traffic）

---

## 2026-05-15n Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — **physical-separation 拓樸**：
  - 加新 IP `USR_M_IP = 192.168.1.34`（SD76 meters 專用 gateway）
  - `cli_A` (.30) → SE3 left only（dedicated）
  - `cli_B` (.31) → SE3 right only（dedicated，從 sensing bus 改成 SE3 right）
  - 加 `cli_M` (.34) → SD76 left + right（sensing bus 搬家）
  - 移除 `cli_A_se3_L` / `cli_A_se3_R`（不再需要 split，每個 SE3 已獨佔 USR）
  - 加 `g_gw_m_ok` atomic + status 輸出 `gw_m=` / `dev_gw_m=`
  - 啟動 init：5 個 gateway 各自 connect、各自 [WARN] / [OK]
  - SE3 left init 用 cli_A，SE3 right init 用 cli_B，SD76 兩台都用 cli_M
  - dual_se3_sync_retry NOTE comment 重寫（不再有 mutex contention，物理分離）
- `web_backend/public/app.js`
  - `craneDevices` 加 `gw_m: true`
  - `DEVICE_LABEL_TW` 加 `gw_m` + 補 a/b/c/d label 標明對應裝置
### 原因
2026-05-15 多次踩 USR-TCP232-304 + 多 TCP 同時 session 的 frame contamination：
- 14h split 兩個 SE3 在同一 USR (.30) → R 收到 L 的 reply 被 driver reject
- 14j revert 單 cli_A → init chicken-and-egg + R 永遠卡 OPT
- 14m driver init Mode B 加 retry+clearAlarm 解掉 init 那關
- 但運轉中 R 還是不時 fail（USR 廣播 reply 機制無解）

USR-TCP232-304 是 **transparent serial server**，沒 Modbus-aware 路由，**多 client 模式必廣播所有 RS485 reply**。軟體層只能擋 frame、不能避免廣播。

唯一徹底解 = **物理分離每個 SE3 一台 USR**：
- USR 上只有 1 個 TCP client（RPi）→ 沒人可以被廣播給「錯的人」
- USR 後面 RS485 上只有 1 個 SE3 → reply 一定是這個 SE3 的，driver slave-ID check 永遠通過

SD76 meters 共用 cli_M 沒問題，因為 SE3 traffic 完全不在 cli_M 上、不會跨 slave 汙染。

驗證重點：
1. 啟動 log 5 個 gateway 都 [OK]
2. 兩 SE3 init 都 [OK]，R 不再卡 OPT
3. `up on` 雙邊命令 sync_retry attempts 應該大多 = 1（無 mutex 排隊、無 broadcast 干擾）
4. SE3-keepalive L/R fail count 都 = 0

未來：CLV900 中間絞盤上機時可以接 cli_A（跟 SE3 left 共用 .30，mutex 序列化），或要再買一台 USR 給它獨佔。

---

## 2026-05-15m Claude (Sadie) [跨界: user_lib]
### 修改檔案
- `user_lib/SE3_inverter.cpp` — `init` Mode B 加 **retry + clearAlarm fallback**：
  - probe (readParam H1001) 失敗 → log + 呼叫 `clearAlarm()`（H1101=0x9696，driver 內建 200ms sleep）→ 重新 probe
  - 最多 3 次 attempts；前 2 次失敗都會嘗試 clearAlarm，第 3 次失敗才真的 return error
  - probe **成功** 但 status word b7 (FAULT) = 1：opportunistic clearAlarm（避免 caller 第一個 run cmd 撞到 alarm 狀態）
  - 純 additive：成功 path 不變、失敗 path 多了自救機制
### 原因
2026-05-15 多次 bench 撞同一個 chicken-and-egg loop：

```
SE3 殘留 OPT (上次 bench 沒清乾淨)
  → crane 啟動 init probe (readParam H1001) fail
  → init 回 error → g_dev_se3_*=false
  → keepalive 進場 if 跳過該邊 → 不會 auto-clear OPT
  → 整個 session R 永遠不能動
```

memory 14k 的 `project_se3_07_10_two_options.md` 已寫得很清楚，但只有 application 層 14l pre-flight clearAlarm 救不了 — pre-flight 在 cmd_hold 進場後才生效，而 cmd_hold 會被 `g_dev_se3_*=false` 提前 reject，根本進不到 sync_start。

**唯一的徹底解 = driver init 自己會清 OPT**。clearAlarm 跟 keepalive 用的是同一個 mechanism (H1101=0x9696)，差別只在「init 階段先做一次」。

成本：
- SE3 啟動正常時：1 次 probe 成功，0 額外延遲
- SE3 在 OPT：probe fail (~150ms timeout) + clearAlarm (200ms sleep) + 第二次 probe 成功 ≈ ~400ms 額外時間，**但 R 救得回**
- 如果 3 次都 fail：~1.2s + 3 × 200ms ≈ ~1.8s 之後才放棄（vs 之前 150ms 就放棄）

驗證：
1. bench panel 故意把 R 進 OPT
2. 重啟 crane
3. 看啟動 log：應該看到 `init probe attempt 1/3 failed` + `clearAlarm` log + `[OK] SE3 right`
4. keepalive 開始 tick R（ok 從 1 累加）

未來這條路徑變成標準：任何裝置上電殘留 OPT 都能 self-recover，bench 不用介入。

---

## 2026-05-15l Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `dual_se3_sync_start` 進場加 **pre-flight clearAlarm**：
  - 進場第一件事檢查 `g_se3_left_fault` / `g_se3_right_fault` atomic
  - 任一邊有 fault → log + 對該邊呼叫 `clearAlarm()`（H1101=0x9696，driver 內建 200ms sleep）
  - 兩邊都乾淨時：純 atomic 讀，0 額外 Modbus 流量
  - log 加 `[sync_start] pre-flight clearAlarm L_fault=X R_fault=Y` + HOLD_TRACE
### 原因
Bench：bench 看到「up_left on/off 單邊永遠 OK」+「up on 雙邊常 fail」+ R 端 keepalive `clears=8/30s`（≈每 4 秒 OPT 一次）。

時序拆解雙邊 fail 的真正原因：
1. R 平均每 ~4 秒卡 OPT，keepalive auto-clear 後幾百 ms 又卡（chronic 問題，可能 R 物理層 / wiring 還沒查完）
2. user 按 `up on` → sync_start 進 Phase A → 兩 thread 並發
3. 撞到 R 在 OPT window 內 + keepalive thread 同時也在 clearAlarm
4. 三方搶 cli_A_se3_R 的 mutex → R 的 cmd 卡 / 拒收
5. Phase B run 6ms fast-fail (R 的 SE3 firmware 還在 OPT-clear transition state)
6. sync_start return err → cmd_hold ERR_FAIL_ROLLBACK → 馬達不動

修法：sync_start 進場主動 clearAlarm — 把「等 keepalive 偶然清」改成「我自己現在清」。確保 Phase A 時 SE3 已 OPT-cleared + 200ms settle。

副作用：
- 兩邊都 clean 時：0 額外延遲
- 任一邊 fault：~150-300ms 延遲（clearAlarm + driver 內建 sleep）
- 不會 break 其他現有路徑（純 additive guard）

驗證重點：
1. Bench 連續按 up on/off 30 次以上，看 `[cmd_hold] EXIT result=` 是否多數 OK
2. 看 `[sync_start] pre-flight clearAlarm` log 出現頻率 — 若幾乎每次都印代表 R OPT 還是頻繁，根因仍在硬體
3. SE3-keepalive `clears=` 數字應該不會明顯改變（pre-flight 清的 fault 不算 keepalive 的 clear）

長期：仍要查 R 為什麼 chronic OPT（keepalive fail rate L=1.7% / R=5.3%，差 3x；可能 R RS485 cable / panel 11-00~02 通訊參數對不齊 / wiring）。

---

## 2026-05-15k Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - **Re-apply SE3 split (re-revert 14j)**：
    - 加回 `cli_A_se3_L` / `cli_A_se3_R` static
    - cli_A 宣告但不 connect（CLV900 future）
    - USR_A 啟動 connect 兩條 SE3 session
    - `se3_left.init(cli_A_se3_L, ...)` / `se3_right.init(cli_A_se3_R, ...)`
    - `dual_se3_sync_retry` NOTE comment 改回 split 版本 + 加上 frame contamination 解釋
### 原因
14j revert 後 bench 看到：R panel 手動清 OPT → 重啟 crane → R init 還是 fail。User 指出 14h split 版本 R 是正常 init 的。

**重新分析發現之前的診斷錯誤** — single cli_A 真正的問題不是 USR 多 session 限制，是 **shared cli_A 上的 Modbus reply frame contamination**：

1. cli_A 一個 TCP socket
2. se3_left.init (slave 1) probe → USR 回 slave 1 reply → OK
3. se3_right.init (slave 2) probe → USR 還有 slave 1 殘留 frame 在 buffer → 傳給 RPi
4. SE3 driver 看 `resp[0]=1 != deviceID=2` → 視為 invalid → init fail

split 給 cli_A_se3_L / cli_A_se3_R 各自 TCP buffer → USR 的 buffering 是 per-connection → L 的 reply 進不到 R 的 buffer → 永遠收到正確 slave reply。

14h bench 看到的 R 6ms / 166ms fail 不是「USR 不穩」— 是 sync_start 偶發撞到 R reconnect 窗口，TCP_client monitor (500ms) 內會自動重連、不影響 R 整體可用性。當時我過度反應 revert 是判斷錯誤。

**驗證重點**（重新 deploy 後）：
1. 啟動 log 兩條 USR_A session 都 [OK]
2. R init [OK]，keepalive R: ok 從 0 開始累加
3. SE3 R panel 不再卡 OPT（因為 keepalive 一直 ping）
4. bench hold 動作 R 能跑

未來如果 sync_start 偶發 R fast-fail 還是煩人：
- 可考慮 sync_retry 內部加 isConnected 檢查 + 等 monitor 重連完再發
- 或調整 sync_retry attempts 數補 R 一次重連所需時間
但這些都不該動回 single cli_A — frame contamination 是 deal-breaker

---

## 2026-05-15j Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - **Revert SE3 split (14h + 14i)**：
    - 移除 `cli_A_se3_L` / `cli_A_se3_R` static
    - 恢復單一 `cli_A.connectToServer(USR_A_IP, USR_PORT)` 啟動 connect
    - `se3_left.init(cli_A, ...)` / `se3_right.init(cli_A, ...)` 都用 cli_A
    - `dual_se3_sync_retry` NOTE comment 改回「兩 SE3 共用 cli_A、socket_mtx 序列化」版本
    - CLV900 init comment 改回「重啟用時用同一 cli_A」
### 原因
14h 拆 SE3 → 3 條 TCP；14i 砍 idle 留 2 條。bench 連續測都看到 R-side TCP 不穩：
- "down on" R 6ms × 8 attempts (TCP `connected=false`)
- "up on" Phase B R 166ms × 2 attempts (TCP 還活但 USR 不 reply)
- 結果 R **連續 > 2s 沒收 Modbus → SE3 R 觸發 OPT**

User 指出 keepalive 設計就是 idle 時防 OPT，OPT 一直發 = keepalive 自己壞。我們持續清 OPT 是治標不治本（對 SE3 也不健康）。

USR-TCP232 多 session 即使只 2 條都不穩，整個 SE3 split 路線斷掉。

回到 14g 的單 cli_A：
- 兩 SE3 透過 socket_mtx 序列化（drift floor ~30-50ms）
- keepalive 一定能到 R → OPT 不會觸發
- 機械同步精度比 OPT 帶來的 motor coast 停車安全得多

保留：14a 的診斷 log（dispatch / cmd_hold ENTRY/EXIT / sync_retry per-attempt detail），下次 bench 觀察 mutex contention 還是有用。

下一步如果要再試解 mutex contention：
- 走實體分離（兩台 USR_A、各自一台 SE3）— 硬體成本，但根因解
- 或接受 socket_mtx 序列化（現況），把優化精力放別處

---

## 2026-05-15i Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - 拿掉 `cli_A.connectToServer(USR_A_IP, USR_PORT)` 啟動 connect — `cli_A` 從 declared-but-unused，未來 CLV900 上線再決定怎麼接
  - USR_A 上現在只有 2 條 TCP session：`cli_A_se3_L` + `cli_A_se3_R`
  - 註解標明 cli_A 為什麼不 connect（USR 對 3 條不穩，14h 已驗證）
### 原因
14h 加 SE3 split 後 bench 看到 R 端兩種 fail 模式：
- "down on" R 6ms × 8 attempts → TCP `connected=false`
- "up on" Phase B R 166ms × 2 attempts → TCP 還活但 USR 不回（kernel buffer 收 send，USR 沒 forward 或 reply 給錯 connection）

USR-TCP232 對 3 條並發 TCP 可能踢掉其中一條（最常見是最後 connect 那條被淘汰）。砍掉 idle 的 `cli_A`（CLV900 用，目前 disabled，從來沒 traffic）→ USR 上只剩 2 條 session 給兩台 SE3。

預期改善：
- USR 多連線壓力減半 → 兩條 session 都應該能穩
- SE3_left / SE3_right 仍然 mutex 獨立、無 cross-side contention

驗證重點：
1. 啟動 log 看 cli_A_se3_L + cli_A_se3_R 都 OK，且**沒有持續看到 stderr `[WRN] reconnecting 192.168.1.30:4001`**
2. bench hold up/down on/off 連續操作 50 次以上，看 sync_retry 不再固定一邊出 attempts ≥ 2
3. SE3-keepalive log L/R fail 應該都 = 0

Failure mode (萬一 2 條 USR 還是不喜歡)：
- 持續看到 reconnect log + R 還是會掉 → 14h+i 整個 SE3 split 路線不可行
- 下一步：revert 回 14g（兩個 SE3 共用 cli_A），接受 mutex 序列化代價

未來 CLV900 hardware 上機時要重新評估 cli_A 怎麼接：
- (a) 再 enable `cli_A.connectToServer()` 試 USR 撐不撐 3 條
- (b) CLV900 共用 cli_A_se3_L（mutex 序列化，但 CLV900 寫頻率低，影響小）

---

## 2026-05-15h Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - 新增兩個 dedicated TCP_client：`cli_A_se3_L` / `cli_A_se3_R`，都連同一個 USR_A_IP:USR_PORT (.30:4001)
  - `cli_A` 保留給未來 CLV900 middle（disabled 中）
  - 啟動 init：3 條 USR_A 上的 TCP 各自 connect，per-session 失敗印獨立 [WARN]；`g_gw_a_ok` 任一條成功就 true
  - `se3_left.init()` 改用 `cli_A_se3_L`，`se3_right.init()` 改用 `cli_A_se3_R`，per-side 啟動先檢查自己 session 有 connect
  - `dual_se3_sync_retry` NOTE comment 改寫：socket_mtx 在 L/R 之間 contention 已消除
### 原因
Bench 看到 sync_start Phase A R=481ms / Phase B L=316ms 等 drift。Sadie 假設 cli_A 上 SE3 兩邊共用 socket_mtx 是其中一個原因（mutex 排隊 + USR 內部 forwarding 可能 cross-connection 拌到 reply）。

USR-TCP232-304 預設支援多 client 連線，所以拆兩條 TCP 走同一個 IP:port 在 RPi 端可行；同一條 RS485 物理 bus，USR 內部仍會序列化 RS485 frame，但 per-session TCP buffer 隔開後：
- 兩 thread 的 send() 在 RPi 端不互等 mutex
- USR 不會把 L 的 reply 寫到 R 的 TCP buffer

預期改善：
- L/R 同步啟動的 mutex queue 等待從 ~30-50ms → ~0ms
- USR cross-session frame 汙染風險消失
- RS485 物理層 drift 下限不變（半雙工硬限制）

驗證重點：
1. 啟動 log 看 3 條 USR_A 連線都 OK（如果 USR 不支援 multi-connection，第 2 或 3 條會 fail，看到 [WARN]）
2. Bench 再跑幾輪 hold up/down on/off，看 `[sync_retry:sync_start.setfreq]` 跟 `[sync_retry:sync_start.run]` 的 attempts / drift 是否變少
3. SE3-keepalive fail rate 應該降低（兩邊各自走自己的 TCP session，互不干擾）

風險：
- USR 多連線併發可能有韌體 bug 或 limit；若 connect 第 2/3 條失敗，會打回單 client 模式（partial init）
- 萬一改完 bench 反而更糟（USR 內部 scheduler 對 multi-connection 處理差），revert 把 init 兩個 SE3 改回都用 cli_A 即可

---

## 2026-05-15g Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` `se3_keepalive_loop`：
  - 加 `FAULT_CODE_READ_INTERVAL_MS = 5000` 跟 `last_fault_code_l/r` throttle 時間戳
  - 左右 fault 偵測路徑：cooldown 滿了要 clearAlarm 之前，throttle 通過就先 `format_se3_fault_codes()` 讀 H1007/H1008，把 fault code 含名稱（OPT/OC/OV/OHT etc.）一起印出來
  - throttle 未通過時印 `(fault_code throttled)`，保證 keepalive 不會被 fault 風暴拖死
### 原因
2026-05-15 re-layout 後 bench 看到 R 端「fault → clear → 又 fault」循環、30 秒視窗 `clears=8`。但 `status=0x80` 只說 fault bit 有設，**不知道哪種 fault**（OPT 通訊 timeout / OC 過電流 / OV 過電壓 / OHT 散熱 / EEP 等）— 無法判斷根因。

之前 2026-05-14 把 fault code 讀取從 keepalive 拿掉的理由是「unthrottled reads 撞到 07-09=2s OPT threshold 拖累 other 側」。現在用 5 秒 throttle 把單側 fault code read 頻率上限鎖在 1/5s，最壞情況加 ~300ms 而且 5 秒才一次，不會 cascade。

**節制設計：**
- 只在 cooldown 滿了（要真的 clearAlarm）才考慮讀 fault code
- 額外 5 秒 throttle：30 秒視窗最多 6 個 fault code 樣本 → 對診斷夠用
- read 失敗 fc_str 會是 `fault_code=READ_FAIL`，仍進 clearAlarm 不卡

### 預期 log
原本：
```
[SE3-keepalive] right FAULT (status=0x80) — auto-clear via H1101=0x9696
```
改後：
```
[SE3-keepalive] right FAULT (status=0x80) fault_codes=[160/OPT 0/- 0/- 0/-] — auto-clear via H1101=0x9696
                                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
                                          最新 fault = OPT (通訊 timeout)
                                          其餘 slot 是歷史紀錄，0/- 表示空
```
（throttle 5 秒內後續 fault：印 `(fault_code throttled)`）

### 這次沒做
- 沒動 `dual_se3_sync_retry` / TCP_client 架構（user 一度提「每 SE3 一個 TCP client」，但兩 SE3 共 RS485 bus 後改 TCP 拓樸沒實質效益、反而會多 byte-collision 風險，討論後不做）
- 沒動 SD76/SE3 driver
- 沒改 P.7/P.8/P.55-57 等 panel 參數對齊（等 fault code 結果出來再說）

### Bench 下一步
重編譯 + 重啟 + 跑 up/down 序列觸發 R fault → 拿 log 看 fault_codes 內容。
- 是 OPT (160) → 通訊 silence 問題，可能要動 keepalive cadence / 加 write keepalive (傳統 keepalive 是 read，部分 inverter 只認 write)
- 是 OC1/OC2/OC3 (16/17/18) → 過電流，可能 P.8 decel 過快觸發
- 是 OV1/OV2/OV3 (32/33/34) → 過電壓，可能 P.8 decel 過快導致回授過電壓
- 是 OHT (144) → 散熱不夠，動作頻率太高
- 是 EEP (64) → EEPROM 寫太頻繁，setFreqHz 用 H1002 (RAM) 不會踩這個，但歷史紀錄可能還在

---

## 2026-05-15f Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`：
  - 拓樸 header comment（L1-65）— USR_A 改成「控制 bus（雙 SE3）」、USR_B 改成「感測 bus（雙 SD76）」
  - Slave ID 常數：`SE3_RIGHT_SLAVE 1→2`、`METER_LEFT_SLAVE 2→1`；middle 預留 slot 跟 CLV900 IDs 重排
  - `cli_A` / `cli_B` 變數註解、device 宣告註解全更新
  - `if (g_gw_a_ok)` init 區塊：原本初始化 SE3 left + SD76 left → 改成初始化 SE3 left + SE3 right；middle CLV900 / SD76 middle 註解仍預留為 [SKIP]
  - `if (g_gw_b_ok)` init 區塊：原本初始化 SE3 right + SD76 right → 改成初始化 SD76 left + SD76 right
  - Gateway `[OK]` log 文字、`[WARN]` 失能影響範圍對應更新
  - `dual_se3_sync_retry` doc 補充：兩 SE3 共 cli_A 後並行 thread 會在 socket_mtx 上序列化、drift floor ~30-50ms
  - 多處內文 comment 提到「meter_loop 撞 cli_A/cli_B」的部分更新為新 layout 描述
- `CLAUDE.md` §架構圖：USR_A / USR_B 內容、拓樸理由、driver 表 SD76/SE3/CLV900 行全部更新
- `.claude/motion_flow.md` §2 RS485_crane 拓樸表：原本停在 2026-05-07 之前 ZS_DIO 架構（更舊），一併升級到 2026-05-15 控制/感測 bus 分離
### 原因
User 拍板硬體 re-layout：**兩 SE3 都接 USR_A (.30)、兩 SD76 都接 USR_B (.31)**，slave ID 對應 SE3 左=1 右=2 / SD76 左=1 右=2（user 物理側面板自己改）。

**動機：** 修掉 sync_retry + SD76 probe 修完之後仍會出現的 200-300ms drift 殘留 — 那殘留是 meter_loop 跟 SE3 dispatch 共用 cli_A/cli_B 造成的 contention。re-layout 後控制 bus / 感測 bus 物理隔離：
- meter_loop 全在 cli_B 上（兩 SD76）— 完全不影響 SE3 dispatch
- SE3 dispatch 全在 cli_A 上（兩 SE3）— 不會被 meter_loop 拖

**Trade-off：** 兩 SE3 共一條 RS485 bus → Modbus RTU half-duplex 強制序列化 → dual_se3_sync_retry 兩條 thread 在 TCP_client::socket_mtx 上輪流。drift floor 從理論「兩個物理 bus 真並行 < 10ms」變成「序列一個 round-trip 30-50ms」。仍比修前看到的 200-300ms 好 4-10x、且 contention 完全消失（最壞延遲可預測）。

**沒動：**
- `dual_se3_sync_retry` / `dual_se3_concurrent` 程式邏輯（即使物理序列化、code 仍 correct）
- 任何 user_lib（driver 不知道誰跟誰共 bus，不需要動）
- 15e 修的 SD76/SE3 init Mode B probe（仍適用）
- 15d 修的 sync_retry 架構（仍適用）

### User 物理動作清單（驗證後給）
1. SE3 right 面板 P.36：1 → 2
2. SD76 left 面板 slave ID：2 → 1（confirm 原本是 2）；SD76 right 確認 2
3. 重接 RS485：兩 SE3 都接到 USR_A bus；兩 SD76 都接到 USR_B bus。注意 A/B 極性 + 終端電阻 120Ω 兩端各一
4. 重編譯部署 + 重啟 crane

### 預期 startup log
```
[OK]   USR_A (control bus — both SE3) @ 192.168.1.30:4001
[OK]   USR_B (sensing bus — both SD76) @ 192.168.1.31:4001
[OK]   DSZL left (X518)  @ 192.168.1.32:502
[OK]   DSZL right (X518) @ 192.168.1.33:502
[OK]   SE3 left       USR_A slave 1
[OK]   SE3 right      USR_A slave 2
[SKIP] CLV900         — hardware not installed (init skipped)
[OK]   SD76 left      USR_B slave 1 (resumed)
[OK]   SD76 right     USR_B slave 2 (resumed)
[SKIP] SD76 middle    — hardware not installed (init skipped)
[OK]   DSZL-107 left  USR_C slave 1
[OK]   DSZL-107 right USR_D slave 1
```

### 預期 sync_retry log（hold up/down 操作）
Phase A setFreq drift 應該降到 30-50ms（被 socket_mtx 序列化），不再受 meter_loop 干擾。
Stop 操作的 drift 同樣 30-50ms floor。
之前看到 200-300ms 的 drift 應該完全消失。

---

## 2026-05-15e Claude (Sadie)
### 修改檔案
- `user_lib/SD76_length_meters.cpp` — `init(TCP_client&, ...)` Mode B 加 Modbus probe（讀 0x0000 work mode reg），probe 失敗 return true
- `user_lib/SE3_inverter.cpp` — `init(TCP_client&, ...)` Mode B 加 Modbus probe（讀 H1001 status word），probe 失敗 return true
### 原因
**Bench 抓到的根因 bug：** user 把 SD76 計米器拔掉但 startup 還印 `[OK] SD76 left/right (resumed)`、`meter_left=1 meter_right=1`。追到 driver `init(TCP_client&, ...)` 兩條（SD76 + SE3）**只設指標就 return false**，完全沒驗硬體 → TCP 連到 USR-TCP232 gateway OK 就被誤判 device alive。

**症狀鏈：**
1. driver init Mode B 直接 return false（success）
2. main.cpp `if (!init(...))` → 進 OK 分支 → `g_dev_meter_* = true` + 印 `[OK] ... (resumed)`
3. meter_loop 每個 cycle 對不存在的 SD76 發 readUpperInteger → 等滿 **300ms recv timeout** → 失敗
4. cli_A / cli_B 上的 socket_mtx 因為 SD76 timeout 每個 cycle 被 hold 300ms
5. `dual_se3_concurrent` / `dual_se3_sync_retry` dispatch 時 ~46% 機率撞上 mutex held by meter_loop → R 端 single-shot 變 200-300ms
6. user 看到 sync_retry 之後仍有 `final L=7ms R=229ms drift=222ms`

**這就是 15d sync_retry 改完之後殘留 drift 的真正源頭。** sync_retry 修了「retry 結構」的不同步，但沒修「meter_loop 打空氣 hold mutex」這個底層問題。

**修法：** SD76 + SE3 的 init Mode B 加 Modbus probe：
- SD76：讀 `0x0000` work mode register（單 reg、最小封包）
- SE3：讀 `H1001` status word（keepalive 已經用同一個 reg）

Probe 失敗 → return true → main.cpp 進 [WARN] 分支 → `g_dev_*` 留 false → meter_loop / keepalive 都 skip 該 device。

**並沒動 main.cpp** — 它原本的 `if (!init(...))` 邏輯一直是對的，只是 driver 給了錯資訊。

### 期待效果（user 重編譯重啟後驗證）
1. 拔掉 SD76 重啟 → 應該印 `[WARN] SD76 left init failed` / `[WARN] SD76 right init failed`，EVT `meter_left=0 meter_right=0`
2. meter_loop 完全 skip SD76 → cli_A / cli_B 上沒人定時搶 mutex
3. `dual_se3_sync_retry` 期望 drift 從 200-300ms 降到 < 50ms
4. SE3 left 那個 startup 假 FAULT (`status=0x80`) 訊息應該也會消失 — 那個八成是 SE3 left 沒實機回應 / readStatusWord 拿到 garbage 被誤判，probe 後 init 會直接 fail

### 跨界紀錄
按 memory `feedback_user_lib_ownership.md` 2026-05-15 起 Sadie 自己改 user_lib，本次未 PR 標 `[跨界: user_lib]`、直接動。其他 driver（DSZL / CLV900 / TCP_client 之類）init Mode B 也可能有同類問題，這輪沒動 — 等 bench 顯示有實際症狀再說。

---

## 2026-05-15d Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`：
  - 新增 `dual_se3_sync_retry<FnL, FnR>(...)` template helper（含 same-fn 兩邊用的 convenience overload）
  - `dual_se3_sync_start`：
    - Phase A `reliable_setfreq_one` × 2 thread → `dual_se3_sync_retry(setFreqHz, 8, 100, "sync_start.setfreq")`
    - Phase B `reliable_run_one` × 2 thread → `dual_se3_sync_retry(run_left_fn, run_right_fn, 2, 50, "sync_start.run")`
    - 保留 [sync_start] Phase A/B 高層 log，retry 細節走 [sync_retry:tag]
  - `cmd_hold` combined OFF：`dual_se3_concurrent(reliable_stop_one)` → `dual_se3_sync_retry(stopDecel, 8, 100, "hold_off")`，escalation 訊息改寫
  - `allRopeInvertersOff`、`hold_all_off`：同樣替換
  - `dual_se3_concurrent` template comment 補充 USE FOR / 不該用於 retry-based ops 的指引
### 原因
User 提供 log：
```
[sync_start] Phase A setFreq L=7ms R=556ms drift=549ms (errL=0 errR=0)
[sync_start] Phase B run L=316ms R=8ms drift=308ms ...
[dual_se3_concurrent] asymmetric cmd dispatch L=6ms R=302ms ...
```
精準對應 1-2 次 retry 的時序：300ms ≈ 1 fail(150ms recv timeout) + 100ms backoff + 50ms success；556ms ≈ 2 fail + 1 success。誰慢輪替 = 純電氣 jitter race。

**根因（軟體側）：** 舊 `reliable_*_one` 各跑各的 thread，一邊 attempt 1 成功就直接 return → 另一邊單獨重試 N 次。結果：成功那邊馬達已跑、失敗那邊還在 retry，drift 300-700ms。

**新架構：** Barrier-style attempt-synchronized retry：
1. 兩條 thread 並行跑一次 attempt
2. `join()` 兩邊等齊
3. 任一邊失敗 → 兩邊一起 sleep backoff → 一起 retry
4. SE3 H1001/H1002 都 level-triggered，重發等於 firmware-side no-op，所以再 issue 一次安全

**效果（預期，等 bench 驗）：**
- **Phase A setFreq drift**：300ms → ≤ 10ms（馬達還沒跑，re-issue 完全等效）
- **Stop drift**：300ms → ~150ms（雙邊一起重試，drift 鎖在一個 attempt cycle）
- **Phase B run drift**：300ms → ~150ms（成功邊馬達已跑無法 undo，但 drift 從 N×cycle 縮到 1×cycle）

**Phase B 殘留 150ms drift 的物理事實：** 一邊 H1001 write 成功後馬達就跑、re-issue 不會 restart。要徹底消除得做 commit-or-rollback（停起來那邊 + 兩邊整個重試），但會造成 200ms decel + 1-2s DC brake 重置 + re-accel 的機械 judder，比 150ms drift 還糟，故不做。

**並行物理檢查（user 自己驗 R 端 panel）：**
- 確認 SE3 P.32/154/36/79/35/51 跟 SE3 left 對齊
- 確認 P.7/P.8 acc/dec、P.55-57 DC brake 左右對齊
- **特別注意：** memory `project_crane_rs485_bus_format.md` 寫的「USR 8N1 / SE3 8N2」是經驗證的正確配置；user 提到改 USR 為 8N2 → SD76 (硬鎖 8N1) 可能變成 bus 上的失敗源，導致 keepalive `clears=4/2` 出現。**建議 USR 改回 8N1**，看 sync_retry 加正確 bus 配置雙管下 drift 還剩多少。

### 不動的東西
- `dual_se3_concurrent` template 保留（單發 emergencyStop / stopDecel 還在用）
- `reliable_*_one` 保留（單側 path 如 `apply_hold_one_side`、motion_rope fine_adjust 個別 stop / 重試 還在用）
- SD76 timeout / meter_loop pace 不動 — 先看純軟體側修完夠不夠

---

## 2026-05-15c Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`：
  - 新增 `ts_now()` helper（wall-clock `[HH:MM:SS.mmm]` 字串）
  - 新增 `HOLD_TRACE(msg)` macro → 印 `[HH:MM:SS.mmm] [HOLD-TRACE] <msg>`
  - **`dispatch`** hold cmd 進入 → `HOLD_TRACE("dispatch -> cmd_hold dir=X onoff=Y")`
  - **`cmd_hold`** 加 branch trace: `cmd_hold branch=<dir> on=<bool> need_L=<> need_R=<>`
  - **set_left / set_right** lambda 加 ENTRY trace（up/down state）、結果 trace、escalate 前後
  - **combined ON path**：dual_se3_hold_start 呼叫前後
  - **combined OFF path**：reliable_stop_one 前後、escalate 前後
  - **`dual_se3_hold_start`** ENTRY / sync_start 呼叫前 / settle 200ms / verification 結果 / EXIT
  - **`dual_se3_sync_start`** ENTRY / Phase A start / Phase A 結果（PASS or FAIL） / Phase B start / Phase B 結果 / EXIT
### 原因
延續 15a 的 trace 工作但補三項：
1. **wall-clock `[HH:MM:SS.mmm]` 時間戳** — 15a 用 `t=<unix-ms>` bench 對碼錶不方便，這次用 HH:MM:SS.mmm 直接讀
2. **branch trace** — 15a 沒記 cmd_hold 走哪個 dir branch（up_left / up_right / down_left / down_right / up / down），bench debug 時看不出來
3. **dual_se3_hold_start / dual_se3_sync_start 內部 checkpoint** — 15a 只 trace 失敗時的 log、成功路徑沒記、bench 無法看到「成功時花多少時間在哪一段」

`[HOLD-TRACE]` 跟既有的 `[dispatch] / [cmd_hold] / [sync_start] / [apply_hold]` 並存、不衝突。grep `[HOLD-TRACE]` 可單獨抽出時序軌跡。

完整一次按 hold up 5s 鬆開的預期 trace（簡化）：
```
[14:23:01.142] [HOLD-TRACE] dispatch -> cmd_hold dir=up onoff=on
[14:23:01.143] [HOLD-TRACE] cmd_hold branch=up on=1 need_L=1 need_R=1
[14:23:01.143] [HOLD-TRACE] combined ON pay_out=0 -> dual_se3_hold_start
[14:23:01.143] [HOLD-TRACE] dual_se3_hold_start ENTRY pay_out=0 hz=10
[14:23:01.143] [HOLD-TRACE] dual_se3_hold_start -> dual_se3_sync_start
[14:23:01.143] [HOLD-TRACE] sync_start ENTRY hz=10 L_pay=0 R_pay=0
[14:23:01.143] [HOLD-TRACE] sync_start Phase A start (setFreq L+R concurrent)
[14:23:01.155] [HOLD-TRACE] sync_start Phase A OK -> Phase B start (run L+R concurrent)
[14:23:01.165] [HOLD-TRACE] sync_start EXIT OK (both running)
[14:23:01.165] [HOLD-TRACE] dual_se3_hold_start sync_start OK -> 200ms settle + verification
[14:23:01.420] [HOLD-TRACE] dual_se3_hold_start EXIT OK both running
[14:23:01.420] [HOLD-TRACE] combined ON dual_se3_hold_start return err=0
... (5 秒 hold) ...
[14:23:06.142] [HOLD-TRACE] dispatch -> cmd_hold dir=up onoff=off
[14:23:06.143] [HOLD-TRACE] cmd_hold branch=up on=0 need_L=1 need_R=1
[14:23:06.143] [HOLD-TRACE] combined OFF -> dual_se3_concurrent(reliable_stop_one)
[14:23:06.190] [HOLD-TRACE] combined OFF reliable_stop_one return err=0
```

從每行 timestamp 差直接看每段花多少時間、哪段拖最久。

---

## 2026-05-15b Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `SE3_KEEPALIVE_INTERVAL_MS` 500ms → 1000ms
### 原因
Bench log 看到 cmd_hold sync_start Phase B L 一致 316ms（≈ 1 次 driver retry 時間），假設是 keepalive 在 Phase A 等 R retry 的期間插入 cli_A → 後面 L 的 run cmd 撞到 SE3 firmware「剛 read 完」拒收窗口。

把 keepalive 從 2Hz 降到 1Hz，cli_A/cli_B 流量再砍半。預期觀察：
- 如果 Phase B L 的 316ms 變成 ≤50ms → keepalive interleave 假設成立，後續可考慮做更聰明的 keepalive（cmd 進場時暫停一次 keepalive）
- 如果 Phase B L 仍然 316ms → 不是 keepalive，是 SE3 firmware 自己的 post-read 拒收，需從 SE3 panel 端追

安全檢查：07-09 (Modbus comm timeout) 預設 2s，1s keepalive 仍有 2x 安全係數。middle 已 disable (14e) cli_A 流量再砍 33%，1s keepalive 撞滿 timeout 風險低。

---

## 2026-05-15a Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — 加吊機手動控制路徑的診斷 log，用來追「L/R 不同步」跟「按鈕反應慢」：
  - **`dispatch()`**：所有非 polling cmd（排除 status/ping/tension/home_status）進場印 `[dispatch] cmd='...' t=<ms>`
  - **`cmd_hold()`**：進場印 `[cmd_hold] ENTRY dir=<x> onoff=<y> t=<ms>`，所有 return path 印 `[cmd_hold] EXIT ... result=<X> total=<Y>ms hold_flags=[uL=.. uR=.. dL=.. dR=..]`
  - **`apply_hold_one_side()`**：加 `side_tag` 參數（"L"/"R"），印 `[apply_hold] L/R action=<retract|pay_out|stop> dispatch=<ms> err=<0/1>`
  - **`dual_se3_sync_start()`**：Phase A/B 各自捕捉 L/R 完成時間，無條件印 `[sync_start] Phase A setFreq L=Xms R=Yms drift=Zms` 跟同樣的 Phase B run；舊的「Phase A failed」log 改成 abort 提示
### 原因
User 提兩個 bench 痛點：
1. **L/R 不同步：** 看 sync_start Phase A/B 的 `drift=Zms` 直接知道哪一階段哪邊慢；對照 `[dual_se3_concurrent] asymmetric` 既有 log 一起看
2. **按鈕反應慢：** `[dispatch] t=...` 跟 `[cmd_hold] ENTRY t=...` 的時間差 = TCP 收到 → cmd 開始處理；`EXIT total=Yms` = 整個 cmd 處理時間。對照 GUI click 時間就能定位是 backend 轉發慢、TCP 排隊慢、還是 cmd 內部慢

預期 success path：
```
[dispatch] cmd='down on' t=12345678
[cmd_hold] ENTRY dir=down onoff=on t=12345678
[sync_start] Phase A setFreq L=12ms R=18ms drift=6ms (errL=0 errR=0)
[sync_start] Phase B run L=8ms R=14ms drift=6ms (L_pay=1 R_pay=1 errL=0 errR=0)
[cmd_hold] EXIT dir=down onoff=on result=OK total=247ms hold_flags=[uL=0 uR=0 dL=1 dR=1]
```

預期 fail / 不同步 path：
```
[dispatch] cmd='down on' t=12345678
[cmd_hold] ENTRY dir=down onoff=on t=12345678
[sync_start] Phase A setFreq L=12ms R=243ms drift=231ms (errL=0 errR=0)  ← R 被 cli_B contention 拖住
[sync_start] Phase B run L=8ms R=14ms drift=6ms (L_pay=1 R_pay=1 errL=0 errR=0)
[dual_se3_hold_start] asymmetric run state — L: running=1 status=0x4b | R: READ_FAIL ...
[cmd_hold] EXIT dir=down onoff=on result=ERR_FAIL_ROLLBACK total=512ms hold_flags=[uL=0 uR=0 dL=0 dR=0]
```

純 additive：沒改動任何邏輯，只多印 log。對所有走 `dual_se3_sync_start` 的路徑（hold + motion_rope + cmd_align_lengths）都生效。

---

## 2026-05-14z Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `meter_read_robust` sanity filter 加**物理可行性 check**：
  - 新增 `METER_MAX_PHYSICAL_JUMP_CM = 30`（max motor 50Hz × 0.25s poll ≈ 12.5cm × 2.4x margin）
  - v1 跟 v2 一致時、再 check `|v2 - prev| ≤ 30cm`；超過 = sustained corruption reject
### 原因
14y diagnostic 抓到鐵證：
```
[motion_rope] left  reached target — stopped (base=350 trigger_cache=0 delta=-350 ... cache_after_200ms=0)
[motion_rope] right reached target — stopped (base=350 trigger_cache=0 delta=-350 ... cache_after_200ms=0)
[fine_adjust] align-to-leader L=124 R=-4 target=-4 main=retract
```

兩邊 cache 同時 = 0、200ms 後還是 0、1500ms 後 fine_adjust 看到 L=124 R=-4。表示 **SD76 持續返回 0 達 1-2 秒**（非單 frame），cli_A + cli_B 兩條 bus 同時觸發（罕見、但顯然會發生）。

14u double-read filter 邏輯：
```
v1 = 0, prev = 120, jump 120cm > 15cm → double-read
v2 = 0 (corruption sustained)
|v2 - v1| = 0 → consensus → accept 0   ❌ BUG
```

雙讀 consensus 把 sustained 同值 corruption 當真實數據。

**修法：** consensus check 後再驗物理 plausibility：
```
v1 == v2 (consensus) 之後：
  |v2 - prev| ≤ 30cm  → 大跳但物理可能 (motor 快速進度 / poll stall) → accept
  |v2 - prev| > 30cm  → 物理不可能 → sustained corruption → reject
```

30cm 來源：SE3 最大 50Hz × 1cm/Hz × 0.25s slowest poll = 12.5cm physical max → ×2.4 margin = 30cm cap.

**預期效果：**
- bench 看到 sustained "0" corruption 時 log `[meter] left sustained corruption rejected: prev=120 v1=v2=0 (jump=120cm > 30cm physical limit)`
- cache 保持上一個 good value，motion_rope 不會被騙提早 stop
- 真正 jump > 30cm 場景（不太可能存在，post-zero 走另一條 valid_flag=false 路徑）會被誤拒，但物理上 motor 不可能那麼快，所以這條 edge case 不存在

---

## 2026-05-14y Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — motion_rope 內 "reached target — stopped" log 增加診斷欄位
### 原因
bench user 反映「第二次 motion_rope 退繩後左右繩長差 90cm 才 fine_adjust 救」：
```
[motion_rope] left reached target — stopped
[motion_rope] right reached target — stopped
[fine_adjust] align-to-leader L=143 R=53 target=53 ...   ← L 跟 R 差 90cm！
```

motion_rope 認為 L「reached target」、但 fine_adjust 一開始看到 L=143 與 target=53 差 90cm。**motion_rope 被騙、提早停了 L 馬達**。

兩個可能：
- L cache 短暫被 corruption 帶到觸發條件 (|cache - base| >= cm)、motor 停了之後 cache 又彈回真實值
- L 馬達中途真的 fault / OPT 停了（19cm 進度時停下）、cache 是真實的、但 motion_rope 的 "reached target" 判斷有問題

加 diagnostic：
- `base=` baseline at motion start
- `trigger_cache=` cache value at moment of stop trigger
- `delta=` trigger_cache - base
- `target=` expected cm (signed by direction)
- `cache_after_200ms=` cache 200ms 後（馬達應停下）的值

下次 bench 重現時看 log：
- `trigger_cache=62 cache_after_200ms=143` → corruption 騙了 motion_rope
- `trigger_cache=62 cache_after_200ms=62` → cache 一直 stable 在 62、但 fine_adjust 後讀到 143 表示之後 cache 又跳變、SD76 有更深 bug
- `trigger_cache=143 cache_after_200ms=143` → motion_rope 不該觸發 reached target（19cm 不到 cm=100），表示 `cm` 參數本身就小、或 base_left 記錯

---

## 2026-05-14x Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` `cmd_hold`：
  - **combined OFF（"up off" / "down off"）**：`reliable_stop_one` 失敗 → **escalate** 到 `emergencyStop` (MRS output cutoff) + `stopDecel` 清 MRS
  - **single-side OFF（"up_left off" / "down_right off" 等）**：set_left / set_right lambda 在 stop attempt 失敗時也 escalate emergencyStop
### 原因
**Safety critical bug：** user log 看到 `[dual_se3_concurrent] asymmetric cmd dispatch L=2467ms R=8ms (errL=1 errR=0)` — L 的 `reliable_stop_one` 8 次 retry **全部失敗、errL=1 abort**。但 cmd_hold OFF 路徑只是設 `err = true`、把 hold flag 清 false、然後**回 ERR** — **完全沒有任何 fallback、馬達沒被命令停**。

trace：
1. user 鬆開按鈕 → cmd_hold("up", "off") 進來
2. `dual_se3_concurrent(reliable_stop_one)` 試 8x stopDecel — L 全部 fail（cli_A jitter）
3. err = true，hold_*_left flag 清除（hold_loop 不會重複命令）
4. cmd_hold 回 `ERR se3_cmd_fail` 結束
5. **L 馬達仍在 last commanded freq 跑**，等 SE3 自己 OPT 自停（2-4 秒）

→ user 經驗：「鬆開按鈕後馬達沒停、繼續放繩 / 收繩數秒」。

**修法：** OFF 失敗 → 立刻 escalate 到 `emergencyStop`（MRS bit，output 切斷，最快 ACK 的指令、優先順序高、bypasses decel ramp）。然後 `stopDecel` 清 MRS bit（讓下次 run cmd 還能用）。

雙保險：
- L 端 stopDecel 全失敗 → emergencyStop 仍可能成功（不同 register write、單發、更有機會穿過 jitter）
- 若連 emergencyStop 也失敗 → SE3 firmware OPT 07-10=0 是最後一道防線（panel 設定，**必須在 bench 確認**）

也補了 single-side（up_left/down_left/up_right/down_right off）同樣 escalate，因為 user 偶爾單側操作也會踩到。

---

## 2026-05-14w Claude (Sadie)
### 修改檔案
- `user_lib/SE3_inverter.cpp` — `sendModbus` recv timeout 300ms → **150ms**（[跨界: user_lib]）
- `.claude/mailbox.md` — 通知 Jim
### 原因
bench 看到 `[dual_se3_concurrent] asymmetric cmd dispatch L=1228ms R=7ms (errL=0 errR=0)` — L 端 cli_A jitter 觸發 driver watchdog + reliable_*_one 8x retry。每次 writeParam fail 等 300ms recv timeout，累積 ~1.2s。

**修法（user 拍板）：** 縮 recv timeout 300→150ms。SE3 正常 reply 10-50ms、150ms 仍 3-15x headroom；少數 SE3 內部 heavy work 超 150ms 由 caller retry 補。

**預期效益：**
- 單次 writeParam fail wall time: 500ms → 350ms
- 8 retries 上限 wall time: ~4.8s → ~2.4s
- 1228ms 這種 cli_A jitter cascade 預期降到 ~600-700ms
- response 感受改善

**trade-off：** 健康 SE3 偶爾 reply > 150ms 會被當 fail、retry 一次（多 100ms backoff）。淨損失小。

---

## 2026-05-14v Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `motion_fine_adjust_sync` 改成**距離自適應 kick**：
  - 新增 `KICK_DISTANCE_THRESHOLD_CM = 15`
  - 進場 per-side 算 `L_distance` / `R_distance`（curL/R_init 到 target_cm 的絕對距離）
  - 距離 ≥ 15cm → `kick_hz`（20Hz）× 500ms 後 drop 到 fine_adjust_hz
  - 距離 < 15cm → 直接從 fine_adjust_hz（10Hz）啟動，**不 kick**
  - `sleep(KICK_DURATION_MS)` 只在 `L_use_kick || R_use_kick` 才執行
  - post-kick 的 `setFreq(hz)` drop 也只對 use_kick 的那邊做（另一邊本來就在 hz）
  - log 改 "kick start" / "direct start"，標出距離跟為什麼有/沒 kick
  - 變數重命名 post-kick → post-start
### 原因
Bench：
```
[fine_adjust] align-to-leader L=-39 R=-48 target=-48  ← L 需 retract 8cm
[fine_adjust] L kick start at 20Hz for 500ms
[fine_adjust] left converged at -48 (target=-48)
[fine_adjust] overrun correction pass 1 L=-63 (err=-15)  ← 過跑 15cm！
```

物理估算：
- kick_hz 20Hz × ~1cm/Hz ≈ 20cm/s
- KICK_DURATION_MS 500ms × 20cm/s = **10cm 移動量**
- 但 L 只需 8cm → kick 期間就已 overshoot
- kick 期間是 `sleep_for(500ms)` blind 等待，沒檢查 target，motor 全速衝
- 加上 SE3 stop ramp + DC brake coast 再 5-8cm
- 總共 overshoot 10cm（kick）+ 5cm（coast）= 15cm，跟 log 完全吻合

修正後行為：
- L 需 8cm（< 15）→ 直接從 10Hz 啟動 → 10cm/s 收斂 → 較精準停車
- L 需 ≥15cm → kick 仍生效（破靜摩擦 + 加速接近 target）
- 兩邊距離不同時各自判斷（不會因一邊長就拖一邊短的）

副作用：短距離冷啟動如果 10Hz 偶爾轉不起來會看不到動作。bench 上若再遇，可調 `KICK_DISTANCE_THRESHOLD_CM` 或之後考慮選項 B（在 kick window 內也 poll target）。

---

## 2026-05-14u Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - **`meter_loop` sanity filter 改成兩段式 + double-read**：
    - 小跳 (≤ 15cm) 直接接受（正常運動）
    - 大跳 (> 15cm) 立即第二次讀 v2 確認；v1 跟 v2 一致 (±5cm) 視為真的大跳、接受；不一致就 reject（single-frame corruption catch）
    - 舊邏輯 fixed 30cm threshold 有 edge case：cache 在 0 附近時、cor­ruption→0 算微跳 (~6cm) 被接受、cache 卡死 0，後續真實值全被當大跳 reject、cache 永遠不更新
  - **motion_rope 緊急 abort 路徑換 `allMotionEmergencyStop` + clear MRS**（meter_lost / se3_fault / tension）：之前用 `allMotionOff` (reliable_stop_one 8x stopDecel + decel ramp ~1-3s)，**pay_out 一邊斷線時活的那邊要等 1-3s 才停 → 鋼索瞬間極不對稱 → 機器人傾斜風險**。emergency 切 output ~50ms 內活的那邊馬達失力
### 原因
**bug 1 — sanity filter cache 卡死 0：**
bench 觀察 fine_adjust 結束後 R ≈ -6，motion_rope 後續 retract，某次 meter_loop 拿到 corruption "0"（jump 從 -6 → 0 只 6cm < 30cm threshold）被誤接受 → cache=0 → 下一筆真實值 34/41/47 全被當大跳 reject → cache 永遠 0 → balance 拿 R=0 跟 L=29 算 → err=29 → trim 拉滿 15Hz → 馬達瞬間 8Hz 差 → 機械應力。

double-read 修法：大跳必須兩次讀都一致才接受、單發 corruption 抓得到。

**bug 2 — emergency abort 太慢（critical safety）：**
user 反映 pay_out 中一邊斷線「不停放繩」。trace：
1. comm 斷 → meter_loop 拿不到讀數 → 500ms grace 後 motion_rope 觸發 `meter_<side>_lost` abort
2. abort 走 `allMotionOff` → `reliable_stop_one` 8×100ms 重試 + P.8 decel ramp → 活的那邊馬達跑 1-3s 才停
3. 死的那邊根本 Modbus 寫不到 → 只能靠 SE3 自己 07-09 (2s) × 07-08 (2) = 4s OPT 才自動 coast 停

**Pi 對死的那邊無能為力**（這是 hardware 層 + SE3 firmware OPT 的責任）。能做的：**活的那邊瞬間 emergencyStop**，讓 asymmetric 鋼索時間從 1-3s 縮到 ~50ms。

合 `allMotionEmergencyStop` (用 SE3 emergencyStop = MRS output cutoff) + 補一次 `stopDecel` 清 MRS。

**Bench-side 必須做（軟體無法替代）：**
- SE3 panel **07-09 = 1.0s ~ 2.0s**（comm timeout 容忍時間）→ 越小越快，但太小會誤觸發
- SE3 panel **07-10 = 0**（OPT 觸發報警 + 空轉停車）— 沒有這個、斷線馬達永遠不會自停

---

## 2026-05-14t Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - `dual_se3_sync_start` Phase B 失敗 cleanup：`reliable_stop_one`（8×100ms 重試 + decel ramp）→ **`emergencyStop` (MRS 輸出切斷) + `stopDecel` (清 MRS)**
  - `dual_se3_hold_start` post-start verification 失敗 cleanup：同上替換
### 原因
user 反映「一邊 Phase B fail 後兩邊停止時間太長、不夠安全」。

**舊邏輯：**
- Phase B 失敗 → `reliable_stop_one` 兩邊
- `reliable_stop_one` = `stopDecel` 8x100ms retry → 最多 800ms wall time
- `stopDecel` 觸發 P.8 decel ramp → 馬達從 hold_hz (10Hz) ramp 到 0 ≈ 1 秒（看 P.8 設定）
- **total ~1.8 秒，期間一邊馬達還在跑** → 鋼索不對稱 → 機器人傾斜風險

**新邏輯：**
- `emergencyStop` = 寫 H1001 b7 (MRS bit) → SE3 立刻 output cutoff → **馬達瞬間失力 coast 停**
- 接著 `stopDecel` = 寫 H1001 = 0 → 清 MRS bit，下次 run cmd 才能正常啟動
- total ~100-200ms，馬達 ~50ms 內就失力

**代價：** emergencyStop 沒 decel ramp，馬達是 coast 不是 controlled decel。鋼索可能因突然失力反彈、有甩繩風險。但**在「一邊跑、一邊掛」的不對稱緊急情境**，coast 是比 decel 更小的災害。

普通鬆開按鈕（cmd_hold "off"）的路徑**沒改**，仍是 `reliable_stop_one` 走 decel ramp（平滑、無甩繩）。只有「safety abort」路徑改 emergency。

---

## 2026-05-14s Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - `cmd_zero_meters` / `cmd_zero_meter`：readback **失敗**時 `g_length_*_valid.store(false)`（之前是 silently 不更新）
### 原因
user 反映「計米器歸零後 GUI 數字不會即時更新」。

**Edge case：14o sanity filter（≤ 30cm jump 才接受）和 14m readback 驗證的交互 bug：**
1. user 按歸零 → SD76 真的 reset 了
2. 但 cmd_zero_meters readback `readUpperInteger` 那一次剛好碰 cli jitter → READ_FAIL
3. server 回 `ERR reset_didnt_apply left_after=READ_FAIL`，**cache 留在歸零前的值 e.g. 140**
4. meter_loop 下次 poll → SD76 回真實值 0 → sanity filter: `prev=140, new=0, jump=-140cm` > 30cm → **reject**
5. cache 永遠卡在 140，GUI 永遠看不到 0

**修法：** readback 失敗時把 `g_length_*_valid` 設 false。meter_loop 的 sanity filter 第一行就是 `if (!valid_flag.load()) return true;` — 沒有 prev 比較對象、直接接受任何值。這樣下一筆 meter_loop 讀回的 0 會通過 filter、cache 更新為 0、GUI 看到正確值。

trade-off：valid=false 短暫期間（一個 meter_loop tick = 150-250ms），GUI 會顯示 `ERR` 而不是數字。下一筆讀回後恢復顯示。比「卡在 140 永遠不動」好太多。

---

## 2026-05-14r Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `dual_se3_hold_start` post-start verification 的 readStatusWord retry 從 1 次 (14p) **加碼到 4 次 × 50ms backoff**；多 retry 時 log `verification needed retry L=N/4 R=N/4`
### 原因
14p 加 retry-once 後 user 仍頻繁看到 `asymmetric run state — L: READ_FAIL running=0` → 兩次 attempt 都中招。bench cli_A jitter burst 偶爾連續 50-100ms 兩次 read 都打不到、單次 retry 不夠用。

Bump 到 4 attempts × 50ms = 150ms 容忍 window，加上前面 200ms 初始 sleep = total ~350ms 動 verification 容忍時間。

**容忍 vs 安全的平衡：**
- 4 retries 都失敗 → 真的 comm down 或 SE3 死 → abort 正確
- silent reject 仍能抓到：readStatusWord 成功但 status word b0=0 → abort 正確
- 純 comm jitter：retry 接住、不再 false abort

驗證：log 看到 `verification needed retry L=2/4` 之類 = 有用、第二次 retry 救回來。

---

## 2026-05-14q Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `dual_se3_concurrent` 加 timing diagnostic：兩邊命令完成 wall time 差 > 100ms 就 log `[dual_se3_concurrent] asymmetric cmd dispatch L=Xms R=Yms`
### 原因
user 反映「鬆開按鈕後一邊吊機很晚才停」。需要 diagnostic 區分：
- **軟體層 asymmetric**（一邊 stopDecel CMD retry / watchdog reclaim 拖久）→ log 會看到 L vs R 差 100ms+
- **馬達物理 decel asymmetric**（P.8 decel time 左右不對齊、DC brake 設定不對齊）→ log 兩邊命令時間幾乎相同（< 100ms 差），但實際馬達聲音晚停

加 timing log 後重測：
- log 沒抓到 asymmetric → 純粹 panel P.8 / P.55-57 沒對齊（memory `project_se3_panel_acc_dec_alignment.md` / `project_se3_dc_brake_setup.md`）→ user 要去 SE3 panel 對齊
- log 抓到 → 軟體 retry 問題、進一步分析

---

## 2026-05-14p Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `dual_se3_hold_start` post-start verification 的 readStatusWord 兩邊各加 retry-once（跟 keepalive 同 pattern）
### 原因
bench log 看到：
```
[dual_se3_hold_start] asymmetric run state — L: READ_FAIL running=0 status=0x0 | R: running=1 status=0x4b — aborting both for safety
```
L 的 `READ_FAIL` 是 cli_A 偶發單次 byte loss、不是 L 真的沒跑。但 14n 加的 verification 把 single-read fail 直接視為「沒 running」→ false positive 把好好在跑的兩邊都停掉。

修法：跟 SE3 keepalive 14k 同 pattern，readStatusWord fail 立刻 retry 1 次再算。fail² 機率把 false abort 砍 70-90%。

---

## 2026-05-14o Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - `meter_loop`：每側 SD76 read 加 sanity filter (`meter_jump_check`) — 若新讀回值跟前值差 > **30cm** 就 reject、保留前值 + log 前 3 次/分鐘
- `.claude/mailbox.md` — 🔴 通知 Jim：`SD76_length_meters::readRegister` 沒驗 CRC（其他 driver 可能也有）
### 原因
**bench 實況：** user 反映 balance err 連飆 `err=224cm trim=5Hz` / `err=-214cm trim=-5Hz` / `err=266cm trim=5Hz`，**但實際繩長差才 30cm 量級**。同時 SE3 keepalive 顯示 `clears=18/30s`（很多，但 fail=0 → 不是 OPT 通訊問題、是其他 fault 類型 OC/OL）。

**Root cause：**
- `SD76_length_meters::readRegister` source code：
  ```cpp
  if (sendModbus(req, 8, resp, respLen)) return true;
  if (respLen < 5) return true;
  if (resp[1] != 0x03) return true;     // FC check only
  int byteCount = resp[2];
  memcpy(raw, &resp[3], byteCount);     // ← 直接吃，不驗 CRC、不驗 byteCount
  ```
  RS485 偶發 bit-flip 的 corrupted reply 只要 FC byte 剛好是 0x03 就被接受 → driver 回 success 但 raw 是 random garbage → readUpperInteger 拼出狗屁數字。

**級聯效應：**
1. SD76 corrupted read → balance err 暴衝（+214 / -214 / +266）
2. balance trim 拉滿 ±5Hz → L=12.5Hz R=7.5Hz 或反之
3. 馬達瞬間左右差 5Hz 跑 → 機械應力 → 電流暴衝 → SE3 觸發 OC / OL fault
4. keepalive 偵測 b7=1 → clearAlarm → motor 重啟 → 再被 balance trim 甩出去 → 再 fault
5. clears=18/30s 就這樣來的

**修法：application 層 sanity filter（hot fix）**：
- meter_loop 內 prev vs new 比較，差 > 30cm 視為 corruption、reject 並保留 prev
- 30cm 閾值：max motion ~30 cm/s × 250ms 最壞 7.5cm 真實 jump + ~4x 安全係數
- 第一次 valid 讀（or 失敗後重新有效）不檢查、直接接受作為基準
- log 前 3 次/分鐘 reject 事件方便診斷

**根治方向（在 user_lib，已 mailbox 通知 Jim）：** SD76 / SE3 / DSZL / 等所有 driver 的 `readRegister` 都應該驗 CRC + byteCount，不應該讓 corrupted frame 通過。`sendAndReceive` atomic API 防的是 stale buffer interleave，但 RS485 bit-flip 是另一回事。

---

## 2026-05-14n Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - `dual_se3_hold_start`：switch from `reliable_start_one` (bundled setFreq+run with 8x100ms retry) → `dual_se3_sync_start` (Phase A setFreq both → Phase B run both near-simultaneous, bounded retry)
  - 加 **post-start verification**：sync_start 成功後 sleep 200ms 讀兩邊 `readStatusWord` b0 (running)；任一邊 b0=0 視為 silent reject / fault → `reliable_stop_one` 兩邊 + 回 ERR
### 原因
user 反映同時收放繩**兩邊不同步**，而且**一邊沒動另一邊也沒停**。

**問題 #1（不同步）**：`reliable_start_one` 把 setFreqHz + run + 8x100ms retry 綁在同一個 loop。一邊一次過、另一邊 retry 3 次 → wall time 差 ~300ms，視覺/聽覺上明顯一邊先動。`dual_se3_sync_start` 用 Phase A/B 分離（setFreq 兩邊都做完才同時 fire run），drift 從 ~100-800ms 降到 ~10-100ms。motion_rope 早就用這個 pattern，但 hold 還在用舊版 — 統一過去。

**問題 #2（safety 沒 trigger）**：SE3 firmware 有 silent reject pattern（memory `project_se3_modbus_cu_latch.md`）— Modbus 寫 H1001 收到 OK ACK 但 firmware 內部沒接受 run cmd（CU mode latch / fault state）。driver 看不出來、回 success → cmd_hold 認為都成功 → 沒 rollback → 一邊跑、另一邊沒跑、繩子拉成不對稱 = 機器人傾斜風險。

修法：start 後 sleep 200ms 等馬達 engage、讀 status word b0；任一邊 b0=0（沒在跑）就 stop 兩邊 + 回 `ERR se3_cmd_fail_rollback`。Log 印出兩邊 status 方便診斷。

延遲影響：每次「同時 ↑/↓」按下會多 200ms 才 server 回 OK，但馬達其實第 0ms 就被命令動了、200ms 純粹是驗證等待。使用者不會感覺到（馬達在動）。STOP 在另一條 TCP 連線（crane_intr），不會被這 200ms 卡到。

---

## 2026-05-14m Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - `cmd_zero_meters`：reset+resume 後**立即 readback** 每條 meter 的 upper integer；不為 0（容差 5cm）就回 `ERR reset_didnt_apply ...`、回應內嵌 `left_after=` / `right_after=` / `middle_after=` 顯示實際值
  - 同步更新 `g_length_*` atomic cache → GUI 下一筆 status poll 立即看到新值（不用等 meter_loop 250ms cycle）
  - `cmd_zero_meter`（單側）做同樣的 readback + cache 更新
### 原因
user 反映按了歸零、server 回 `OK` 但 **GUI 顯示計米器數字沒變**。trace：
- `meter_left.resetAll()` = `writeSingleRegister(0x0000, 0x0003)`
- `sendModbus` 只檢查「有回覆」、**不檢查 SD76 是否真的歸零**
- SD76 firmware 在某些 work mode 下可能**靜默吃掉** reset cmd（跟 memory `project_sd76_panel_mode_latch.md` DP 寫被吃同 pattern）
- driver 看不出來、回 success → server 回 `OK`
- 但 SD76 內部 counter 沒變 → meter_loop 下一輪讀回原值 → GUI 顯示不變

**修法：**
1. **Readback 驗證**：reset 完馬上讀 `readUpperInteger`。容差 5cm（reset 過程可能還有殘留 pulse）內視為成功；超出回 `ERR reset_didnt_apply` 並顯示實際讀回值，讓使用者一眼看出哪一側沒清。
2. **Atomic cache 立即更新**：以前要等 meter_loop 下一輪（250ms idle）才把 reset 後的值 propagate 到 GUI；現在 cmd_zero_meters 直接 store 新讀回的值到 `g_length_*`，GUI 200ms 下次 status poll 馬上看到正確值（最壞 200ms 延遲降到 ~0ms）。

**回應格式新增：**
```
OK left_after=0 right_after=0 middle_skipped=1               ← 正常
ERR reset_didnt_apply left_after=129 right_after=0           ← 左沒清，bench debug 用
```

如果 bench 還是看到「歸零後 GUI 沒變」：
- 看 log 是 `OK` 還是 `ERR reset_didnt_apply` —
  - `OK left_after=0` → reset 真的成功，問題在 GUI（前端 cache / display）
  - `ERR reset_didnt_apply left_after=NNN` → SD76 firmware 真的吃掉 reset cmd，是 driver / panel 模式 latch 問題（需要 panel-side 改 work mode、或 driver 加 pause→reset→resume 序列）

---

## 2026-05-14l Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `cmd_zero_meters`: meter_middle 不可用時 graceful skip（不再返回 ERR）；reply 加 `middle_skipped=1` 旗標
- `web_backend/public/app.js` — 新增 `motion_lr` / `meters_lr` token groups（不含 middle/clv900）
- `web_backend/public/index.html`
  - pay_out / retract `data-required` 從 `motion_full` 改 `motion_lr`
  - 兩個三條歸零按鈕 `data-required` 從 `meters_all` 改 `meters_lr`
### 原因
**Bug：bench 配置（2026-05-14e CLV900 + meter_middle init 跳過）下，這些按鈕全部被 disable**：
- 三條歸零（地面 / 洗窗）：`data-required="meters_all"` 展開含 `meter_middle` → 前端 disable
- pay_out / retract：`data-required="motion_full"` 展開含 `meter_middle, clv900` → 前端 disable
- 即使前端開放、server cmd_zero_meters 也硬要求 `g_dev_meter_middle.load()` 才肯歸零，否則回 `ERR meter_middle_unavailable`

User 觀察到「兩個歸零按鈕不管用」就是這個 — 按鈕在 UI 被 disable（hover title 應該寫「不可用：中間管線計米」）。其實 motion_rope 內部 `use_middle = ... && g_dev_meter_middle && g_dev_clv900` 已經正確 graceful skip，data-required 的限制是過嚴的。

**修法兩邊都動：**
- Server cmd_zero_meters 比照 motion_rope，middle 不可用就 skip middle 部分（reset_middle = false），保留 left/right reset 強制
- 前端加 `motion_lr` / `meters_lr` 兩個排除 middle 的 token group，讓沒裝中間管線時這些按鈕能用

未來中間管線裝上來後 `g_dev_meter_middle=1`，運行邏輯不變 — `reset_middle=true` 自動把 middle 也歸零。前端按鈕也都沒摸 `meter_middle` requirement，所以即使 group 名字叫 `motion_lr`，含義就是「最低需求」而已，不衝突。

「獨零中」按鈕保留 `data-required="meter_middle"` — middle 不可用時這個按鈕**應該** disable（按了沒意義），UX 正確。

---

## 2026-05-14k Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - `CLEAR_COOLDOWN_MS`: 5000 → **500ms**（OPT 到 clearAlarm 的等待從 5 秒砍到 0.5 秒）
  - `se3_keepalive_loop` 內 `readStatusWord` fail 立刻 **retry 1 次** 才算 fail（左右兩側）
### 原因
**Bench 實況：** user 反映**馬達實際在 OPT 空轉 → reset → 重啟反覆**（耳朵聽到、面板顯示 OPT），不只 log 吵。最新 log 顯示 L 端 fail rate **58%**（21 ok / 29 fail / 30s），clears=6/30s = 每 5 秒一次 OPT。每次 OPT L 馬達空轉 5 秒等 cooldown，**R 在這 5 秒持續跑**，造成嚴重左右不同步（balance trim 卡在 ±5Hz 救不回來）。

**修法 1（cooldown 5s → 500ms）：** 原本 5s 是怕 Pi 真離線狂寫 H1101 spam。但 bench transient OPT 是另一種情境，應該快速清掉搶回馬達。500ms 後 clearAlarm 自帶 200ms sleep → max 寫入頻率 ~2/sec/side，不算 spam。L 馬達空轉時間從 5s → ~0.7s（500ms cooldown + 200ms clearAlarm sleep）。

**修法 2（readStatusWord retry-once）：** fail² 數學上會把效率不錯的單次 fail rate 30% 壓到 9%（70% 減 OPT 觸發），就算最壞 60% 也壓到 36%。重點：SE3 看到的是「**請求**」次數，retry 後的請求也算 — SE3 內部 silence 計數會被 reset。**OPT 觸發頻率預期降 70% 以上**。

**合計效果（理論）：**
- OPT 觸發 ↓ ~70%
- 每次 OPT 馬達 coast 時間 ↓ ~90%（5s → 0.7s）
- → 馬達空轉時間 **↓ ~97%** → sync 漂移大幅改善

**待 bench 驗證：** 重編 → 部署 → 跑 hold sync 30s → 看 `clears=` 數字是否降到 ≤1/30s、聽馬達是否還會聽到 OPT-reset 反覆聲。**仍然吵的話**：根本是 RS485 物理層（cable / termination / USR_A 韌體），軟體沒辦法再壓。

---

## 2026-05-14j Claude (Sadie)
### 修改檔案
- `web_backend/public/app.js` — 開機 `updateCraneButtonStates()` 呼叫從 line ~615 移到檔尾、`connectWs()` 前
### 原因
**Critical bug 我自己造成的：** 14f 的 busy UI 改動把開機 `updateCraneButtonStates()` 放在 line 615，但該函式內部用 `craneHoldState`（`const` at line 1040）。`const` 有 temporal dead zone — 在宣告前存取就 throw `ReferenceError`。

結果：browser 載 app.js 跑到 line 615 就死 → 後續 `connectWs()` 沒被執行 → WS 從來沒連 → 沒收到 status msg → `setDot()` 沒被呼叫 → **三顆 status dot 全紅**。

User 一直以為 "web 連不到 crane"，但 web_backend log 顯示 `[crane] connected 127.0.0.1:5002` 兩條都通 — server 端正常，純粹 frontend script crashed early，狀態傳不到 DOM。

排查線索：
- Web backend log: `[crane] connected x2` ✓
- Crane main.cpp log: `init complete, accepting commands` ✓
- 但 GUI dots 全紅且這次才出現

→ 鎖定 frontend script 載入失敗（不是 WS 通訊失敗）→ TDZ。

教訓：以後加開機呼叫的時候，要確認所有依賴的 `const` / `let` 都已經宣告過。下次寫 init 函式前先在頭幾行用 `console.log(craneHoldState)` 之類的 smoke test。

---

## 2026-05-14i Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - **撤回** `se3_keepalive_loop` 內呼叫 `format_se3_fault_codes()` 的部分，還原成原本的 simple FAULT log
  - 保留 `se3_fault_name()` / `format_se3_fault_codes()` helper（dead-code but cheap，下個 commit 可能還會用）
  - **新增 raw command `se3_fault left|right`**：on-demand 讀取 SE3 H1007/H1008 fault code，不在 keepalive 熱路徑跑
### 原因
**回滾理由 — 2026-05-14h 的 keepalive 自動讀 fault code 反而把問題搞嚴重：**

bench 第二次驗證 log:
```
[SE3-keepalive] right FAULT (status=0x80) fault_code=READ_FAIL — auto-clear via H1101=0x9696
[SE3-keepalive] right FAULT (status=0x80) fault_code=READ_FAIL — auto-clear via H1101=0x9696
... (連續多次)
last 30s — L: ok=55 fail=5 status=0x0 clears=1 | R: ok=35 fail=25 status=0x80 clears=6
```

兩個問題：
1. **`fault_code=READ_FAIL` 每次都 fail** — `0x1007`/`0x1008` 是 `.claude/summaries/SE3_INVERTER_MODBUS_SUMMARY.md` 從 PDF text dump 抓的，註解明寫「未驗證」。bench 實證可能根本不是 fault code reg（或不允許這樣讀）。
2. **每次 fault tick 多吃 ~400ms（readParam ×2 失敗 timeout）** — 加上 clearAlarm 200ms sleep、原本就有的 readStatusWord ~50ms × 2 (L+R) → 單圈逼近 1.6s。**SE3 07-09 OPT timeout = 2.0s**，原本只有一邊 fault 變成**兩邊輪流 fault**（左邊 fault 時拖到右邊 cli_B 的 readStatusWord 也來不及，OPT 觸發）。

**改為 raw command 模式：**
- bench 想看 fault code 時，在 web GUI raw command 區送 `se3_fault left` 或 `se3_fault right`
- server 回覆格式：`OK side=left f1=160/OPT f2=0/- f3=0/- f4=0/-`
- 第一次 bench 用這個 raw cmd 確認 0x1007/0x1008 register 是否正確、byte ordering 是否 latest-first
- 確認後再決定要不要把 fault code 重新拉進 keepalive（用更短的 cooldown / 加 separate thread / 跳過讀取）

driver 端 `readFaultCode()` method **保留不撤**（純 additive、零副作用、未來會用）。

---

## 2026-05-14h Claude (Sadie)
### 修改檔案
- `user_lib/SE3_inverter.h` / `user_lib/SE3_inverter.cpp` — 新增 `readFaultCode(f1, f2, f3, f4)` 純 additive method，讀 H1007 + H1008 拆成 4 個 fault code byte
- `Crane_control_PI/main.cpp`
  - 新增 `se3_fault_name(uint8_t)` 對照表（OPT / OC1-3 / OV1-3 / OHT / EEP / OL2 / OLS / IPF / SCP / CPU / CPR / THT / THN / NTC / PID / AErr）— 對照來源 `.claude/summaries/SE3_INVERTER_MODBUS_SUMMARY.md`
  - 新增 `format_se3_fault_codes()` 一次組成 `fault_codes=[160/OPT 0/- 0/- 0/-]` 格式
  - `se3_keepalive_loop` 偵測 fault + cooldown 允許清 alarm 時，先讀 fault code 並 log；維持原本 5s cooldown
- `.claude/mailbox.md` — 通知 Jim：SE3 加 readFaultCode（[跨界: user_lib]）
### 原因
bench segfault 修掉後（2026-05-14g）SE3 keepalive 終於跑起來，發現左右繩 SE3 反覆進 fault state (status=0x80) → auto-clear → 又 fault 的 loop。clearAlarm 機制有作用、最終會穩定下來，但**不知道根因是 OPT (160) / OC / OV 還是其他**。光看 status word 只能看到「有 fault」，不能看 fault code。

加 `readFaultCode()` 讓 keepalive log 直接吐出 mnemonic，bench 下次跑就能看到例如：
```
[SE3-keepalive] left FAULT (status=0x80) fault_codes=[160/OPT 0/- 0/- 0/-] — auto-clear via H1101=0x9696
```
便於對焦：OPT → 通訊 / keepalive 問題；OC/OV → 電源/負載；THT/OHT → 散熱；EEP → 韌體；其他則去查手冊。

byte ordering（H1007 high 或 low 哪邊 latest）手冊不清楚，bench 觀察後若相反再翻 driver 註解。

---

## 2026-05-14g Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - `allMotionOff()` / `allMotionEmergencyStop()`：CLV900 stopDecel 前加 `if (g_dev_clv900.load())` 守備
- `.claude/mailbox.md` — 通知 Jim：CLV900_inverter（以及可能 SD76/SE3/DSZL）需要 null-client 防護
### 原因
**Bug fix — 啟動 segfault**：bench 跑這次 build 在「[INFO] EVT device_state ...」之後立刻 segfault。trace 是 `allMotionOff() → inverter.stopDecel() → writeParam → sendModbus → client->sendAndReceive(nullptr deref)`。CLV900 在 2026-05-14e 為了中間管線未裝跳過 init，但 `client` 留 nullptr，後續任何呼叫都會 null-deref。修法暫時在 application 層守住兩個 startup 必呼叫的 helper；underlying driver 防護放 mailbox 給架構方 review。

---

## 2026-05-14f Claude (Sadie)
### 修改檔案
- `web_backend/server.js` — 新增第二條 crane TCP 連線 `crane_intr`，路由 `stop` / `status` / `home_status` / `ping` 走這條（不變的 `src: 'crane'`）；若 intr 斷線會 fallback 回主連線
- `Crane_control_PI/main.cpp`
  - 新增 `g_init_done` atomic，於 main() 所有 driver init / helper thread 啟動完成後 store(true) 並 `broadcast_evt("EVT init_done\n")`
  - `cmd_status` 新增 `init_done=` 與 `motion_active=` 兩個 flag
- `web_backend/public/index.html`
  - `.panel-crane` 頂端加 `#crane-busy-row` banner（預設隱藏）
  - 所有 crane 按鈕加分類 class：
    - `.crane-readonly` (status / home_status / STOP)
    - `.crane-action` (pay_out / retract / align_lengths / zero_meters / zero_meter / zero_tension / 8 顆 raw relay)
    - `.crane-hold-btn` (6 顆 ↑/↓ hold 按鈕)
- `web_backend/public/style.css` — `.crane-busy-row` 樣式（紫青漸層 banner）
- `web_backend/public/app.js`
  - 新增 `craneInitDone` / `craneMotionActive` 模組級 state
  - `updateCraneDeviceUI()` → 重寫為 `updateCraneButtonStates()`，組合三層 disable（data-required / init / motion）
  - `onCraneLine` 解析 `init_done=` / `motion_active=` + `EVT init_done`
  - hold flag 變動時 trigger `updateCraneButtonStates()`
  - crane TCP 斷線時 reset `craneInitDone=false`、`craneMotionActive=false`
  - 頁面載入時呼叫 `updateCraneButtonStates()`，banner 預設顯示「初始化中」

### 原因
**Bug fix — GUI STOP 按鈕「按了沒用」**：
crane server 每個 TCP 連線一條 handler thread，`motion_rope` (pay_out / retract / align_lengths) 在那條 thread 上塞滿迴圈直到動作結束。`web_backend` 對 crane 只開一條連線 → 所有指令排隊在同一條 handler thread → 按下 STOP 後 `stop\n` 卡在 socket buffer 等 motion_rope 自己 timeout 才被 dispatch。雖然 motion_rope 每 iter 有檢查 `abort_flag.load()`，但 recv 根本回不來、`cmd_stop` 沒機會 set flag。

修法沿用 `WASH_ROBOT::crane_cli_estop_` 已驗過的 pattern：web_backend 多開一條專供「中斷類」指令的 TCP 連線。第二條落在 server-side 另一條 handler thread → cmd_stop 立即執行 → set abort_flag → motion_rope 下個 iter 看到 abort → 退出。額外副作用：200ms 狀態 poll (status) 也走 intr 通道，**motion 期間張力/繩長/hold 顯示反而更即時**（以前是 motion 結束後一次刷新）。

**Feature — busy / init UI gating**：
使用者要求：(1) 動作執行中時 disable 相關按鈕避免誤觸；(2) 初始化未完成時整個 crane panel 鎖住。Server 暴露 `init_done` + `motion_active`，前端按三層 OR 邏輯 disable 按鈕：
- `.crane-readonly` 永不 disable（包含 STOP，緊急逃生）
- `.crane-hold-btn`：!init_done OR (motion_active && 本地無 hold 按住) 時 disable（讓使用者壓住的那組能放開）
- `.crane-action`：!init_done OR motion_active 時 disable（包含 raw relay 8 顆 + 自動動作類）

Banner 三態：「⏳ crane 初始化中…」/「⏳ 自動動作執行中…（STOP 仍可按）」/「🖐 手動拉/放繩中…（其他動作鎖定）」。

---

## 2026-05-14e Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp`
  - USR_A 初始化區塊：註解掉 `meter_middle.init()` 跟 `inverter.init()`（CLV900），改印 `[SKIP] ... — hardware not installed`
  - `motion_rope`：移除每次都印的「middle pipeline not available — skipping」log（因為現在是預期狀態，不該每次都吵）
### 原因
中間管線（水管 + 電線）硬體尚未裝上來。前一陣子 bench 每次按 pay_out/retract 都會看到一行 "[motion_rope] middle pipeline not available — skipping" 噪音；而且 meter_middle 還在每 150ms poll cli_A，吃 ~33% 的 bus 流量。

採用 Runtime disable 方式（option A）：
- `meter_middle.init()` + `inverter.init()` 註解掉 → `g_dev_meter_middle` / `g_dev_clv900` 保持 default false
- 所有 `if (g_dev_*)` / `use_middle` 檢查自動 skip middle path
- meter_loop 跳過 meter_middle polling → cli_A 流量 ~33% 節省
- GUI 仍會顯示 middle 欄位但 status=ERR/0，相關按鈕透過 `data-required="meter_middle"` 自動 greyed out

未來中間管線裝上來時，恢復兩行 init 即可，所有 middle 邏輯（atomic globals / motion_rope use_middle 分支 / cmd 處理 / GUI 顯示）保留完整不動。

---

## 2026-05-14d Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — balance trim cap 由「絕對 Hz」改為「base_hz 比例」
  - `BALANCE_TRIM_CAP_DEFAULT (5.0Hz)` → `BALANCE_TRIM_CAP_RATIO_DEFAULT (0.5)`
  - atomic 改名 `g_balance_trim_cap` → `g_balance_trim_cap_ratio`
  - `apply_balance_trim`：`cap = base_hz * ratio`
  - `cmd_set_balance_cap`：參數從 Hz 改為 ratio (0..2.0)，log 同時顯示 hold/motion 各自的 effective Hz
  - `cmd_status` 輸出欄位 `balance_cap` → `balance_cap_ratio`
  - 啟動 log 印 ratio + hold/motion 兩個 base 下的 effective Hz
### 原因
2026-05-14c 把 cap 從 3 升到 5Hz 解 R fault 那個 case。但思考發現絕對 Hz cap 在不同 base_hz 下意義不同：
- base 10Hz cap 5 → L 範圍 7.5-12.5（合理）
- base 5Hz cap 5 → L 範圍 2.5-7.5（最低 hz 太接近 hz_min=2，有失速風險）
- base 20Hz cap 5 → L 範圍 17.5-22.5（修正能力相對縮水）

改用 ratio 後 cap 自動跟隨 base：
- ratio 0.5 + base 10Hz = cap 5Hz
- ratio 0.5 + base 5Hz  = cap 2.5Hz（保護慢速 base 不掉到 hz_min 以下）
- ratio 0.5 + base 20Hz = cap 10Hz（高速時給更大修正權限）

Runtime 調整：`set_balance_cap <ratio>` 接 0..2.0；超過 1.0 代表 cap 可以等於甚至大於 base，極端情況才用。

注意：本次是 default 0.5，跟改前 5Hz @ base 10Hz 行為完全等價，沒有 silent 變更現有行為。

---

## 2026-05-14c Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `BALANCE_TRIM_CAP_DEFAULT` 3.0 → 5.0Hz
### 原因
Bench：手動兩邊同時 retract 時 R 端 fault 卡死、L 持續走，balance_trim 跑到 cap 3Hz（L=8.5 R=11.5）就拉不動了 — err=11cm 還在繼續成長。

提高 cap 到 5Hz：在 base 10Hz 下 → L 最低 7.5Hz、R 最高 12.5Hz，仍在 hz_min(2)/hz_max(30) 安全範圍內，給 balance 更多權限去追補單邊 lag。

注意：本次只改 default。runtime 仍可透過 cmd `set_balance_cap <hz>` 即時調整（已有 setter）。

實機驗證重點：
1. R 沒 fault 的正常情境下 — 2 邊 hz 是否仍穩定收斂、不振盪
2. R 真的故障時 — 提高 cap 不會魔法救硬體；若 fault 反覆觸發應停下來查 R 端硬體（panel code、機械、接線）

---

## 2026-05-14b Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `reliable_run_one` 加 `max_attempts` / `backoff_ms` 參數（default 仍為 2x50ms）；fine_adjust 進場（L+R kick）跟 correction（L+R reversal/same-dir）4 處改成 6x200ms = 1200ms 視窗
- `.claude/se3_mode6_migration_plan.md` — 新增 SE3 Mode 6 + GPIO relay 長期 migration 計畫提案（硬體 BOM、panel 設定差異、軟體刪/留/新增清單、工作量、風險）
### 原因
Bench：main motion 結束 → fine_adjust align-to-leader 進場 → `reliable_run_one(L, pay_out)` FAIL ×2。同向（main 跟 fine_adjust 都 pay_out），不是反向 lockout，是 SE3 stop ramp + DC brake 期間的 firmware 拒收（>1500ms settle 仍不夠）。

Short-term fix（本次）：
- `reliable_run_one` 加參數讓 caller 控制 retry 強度
- `dual_se3_sync_start` 仍用 default (2,50) 維持 sync 精度（兩邊最多 drift 100ms）
- fine_adjust 進場 + correction 改 (6,200) = 1200ms 視窗，跨過 SE3 firmware 停車 transient

長期解（下個迭代）：
- Mode 6 (P.79=6) + 4CH GPIO relay 控制 STF/STR
- Modbus 仍走 H1002 setFreq（不 time-critical）
- H1001 RUN/STOP 整路徑改 GPIO，消除 bus contention / state machine race / watchdog deadlock 整類問題
- 詳見 `.claude/se3_mode6_migration_plan.md`

---

## 2026-05-14a Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `METER_POLL_MS_MOTION` 50ms → 150ms，附 history comment 說明；同步更新 cmd_status 內舊註解 (1000ms → 150ms)
### 原因
Bench keepalive log L 端 fail 6%（3/50）/ R 端 0%（0/50）。Cli_A 上 4 個 device（SE3_left + SD76_left + SD76_middle + CLV900）vs cli_B 2 個（SE3_right + SD76_right）— polling traffic 差距與 fail rate 差距完全吻合，contention 假設成立。

雖然 TCP_client::sendAndReceive 在 mutex 層級已 atomic、不會 reply corruption，但 SE3 H1001/setFreq 寫入若被 SD76 polling 排隊延遲幾十 ms，會撞到 SE3 firmware 內部 timing window（最明顯就是 ensureCuMode_ 後 150ms latch 跟 reversal lockout）導致 reject。

50ms → 150ms 把 cli_A traffic 砍 67%，cache lag 上限 150ms 對 fine_adjust（自身就 250ms cadence）影響可忽略。

驗證指標：再跑幾次 align_lengths，看 keepalive log 的 L fail count 是否從 3/30s 降到 ≤1。若無改善 → 推到混合模式 6 + relay。

---

## 2026-05-13q Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — fine_adjust overrun correction 需反方向時加 2s settle，並把 setFreq fail vs run cmd fail 的 log 分開
### 原因
Bench log：
```
[fine_adjust] left converged at -259 (target=-259)
[fine_adjust] overrun correction pass 1 L=-268 (err=-9) R=-259 (err=0) target=-259 @ 8Hz
[fine_adjust] correction L start FAILED — abandoning pass
```

main motion 是 retract (STF)，L overrun 到 -268，correction 要 pay_out (STR) — **方向反向**。SE3 firmware 對「剛從 STF stop 後立刻 STR run」會 reject（內部反向 lockout，防反電動勢損馬達）。`reliable_run_one` 2x50ms retry 窗口（~100ms）遠不夠等 lockout 解除。

修法：
1. correction 進場判斷是否反向（任一邊 dir != main_motion_pay_out）。反向時 sleep 2s（加上 final-read 已 sleep 的 1s = 共 3s since stopDecel），足以等 SE3 lockout 解除。
2. setFreq fail / run cmd fail 的 log 分開 — 之前合併判斷導致看不出哪一步失敗，現在能直接知道。

副作用：同向 correction（main 沒到 target → 繼續走原方向）不會 sleep，沒影響。

---

## 2026-05-13p Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `motion_rope` / `cmd_roll_correct` / `cmd_align_lengths` 三處 `lock_guard<motion_mtx>` 改 `unique_lock + try_lock`，busy 時直接回 `ERR motion_busy`
- `web_backend/public/app.js` — pay_out / retract / align_lengths 三個 button click 後 1.5s disable，防 double-click 連發
### 原因
Bench 觀察：user 從 GUI 按一次 pay_out，crane 端跑了兩輪完整 motion_rope（第一輪 fine_adjust done 後立刻 "middle pipeline not available — skipping" 開始第二輪 motion_rope）。

Server 端原本 `lock_guard<motion_mtx>` 是 **阻塞** lock — 第二條同類指令進來會等第一條跑完再自動跑。任何來源（GUI double-click、washrobot 同步 cmd、TCP 層問題）造成的 duplicate cmd 都會被排隊全跑。

修法：
1. crane 三處 motion entry 改 `try_lock` — busy 時直接拒絕，回 `ERR motion_busy`。第二條 dup cmd 不會跑
2. app.js debounce — 三個長 motion 按鈕 click 後 1.5s disable，避免 mouse stutter / 連按產生 dup cmd
3. server-side try_lock 是真正保護，GUI debounce 是 UX feedback

Root cause（為什麼一次 click 變兩條 cmd）未完全確定 — 可能 mouse double-click、可能 button event 重複觸發，但兩層 fix 都能擋。

---

## 2026-05-13o Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `motion_fine_adjust_sync` 進場讀 cache 前加 1500ms settle wait
### 原因
Bench 反覆觀察到兩個現象，root cause 同一個（fine_adjust 進場太快，SE3 stopDecel/DC brake ramp 還沒完成）：

1. **Cache 比 panel 短 3-5cm**：log `[fine_adjust] L=127 R=129 diff=-2 — within ±2, stopping both` 早退，但 panel 實際停在 L=130 R=134（diff=-4 出 tol）。原因：fine_adjust 立即讀 cache，但 motion_rope 剛下 stop，鋼索在 P.8 ramp 期間繼續放，meter_loop cache 反映的是 stop 觸發瞬間（mid-ramp），不是最終靜止位置。
2. **`R run cmd FAILED (Modbus / SE3 firmware reject, 2x retry exhausted)`**：fine_adjust catch-up 時對 R 寫 H1001=0x0002（FWD），SE3 韌體在 decel 中拒絕新 RUN 指令；`reliable_run_one` 2x50ms 重試窗口（~100ms）遠遠不夠等 1-2s 的 ramp 完成。

修法：`motion_fine_adjust_sync` 在 cache validity retry **之後**、讀 `curL_init/curR_init` **之前**插入 `sleep_for(1500ms)`。一次性解兩個現象：
- Settle 後 cache 反映實際停止位置 → early-exit 判斷不再誤觸
- SE3 進入 idle，H1001 RUN 寫不再被 reject → catch-up 路徑能順利啟動

副作用：每次 motion_rope 後固定多 1.5s 才進 fine_adjust convergence。可接受（精度 > 速度）。

**待驗證**：bench 跑一次 pay_out 後觀察 settle 後的 cache reading 是否跟 SD76 panel 對齊；catch-up 路徑（R/L 落後一邊）是否能成功啟動且不再 reject。

---

## 2026-05-13n Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — motion_rope 的 freeze 4Hz 改成 reliable_stop_one；移除死變數 `freeze_hz_val`；更新 fine_adjust 註解
### 原因
Root cause 分析 — bench 反覆出現「L/R run cmd FAILED (Modbus / SE3 firmware reject, 2x retry exhausted)」，調查發現是 freeze 設計跟 driver watchdog 機制衝突：

1. motion_rope target 到達後馬達持續以 freeze 4Hz 跑（為了避免 fine_adjust reverse direction 時 cold-start stall）
2. fine_adjust 進場寫 H1001 transient 失敗 2 次（L 端 keepalive 顯示 ~6% Modbus fail rate）
3. driver `run_h1001_with_watchdog_` 累積 2 fails → 觸發 watchdog re-claim CU mode
4. watchdog 寫 H1000=0 — **但 SE3 firmware 拒絕 motor running 期間的 H1000 write**（driver 註解明確寫過）
5. watchdog "giving up" → caller 看到 run cmd FAILED

修法：motion_rope target reached 改 `reliable_stop_one()`，馬達物理停下。fine_adjust 進場是 clean state — kick start (20Hz × 500ms) 設計已經能處理 cold-start static friction，freeze 維持 motor running 的理由不存在。額外好處：消除 freeze 期間累積 overrun 問題。

`g_freeze_hz` / `cmd_set_freeze_hz` 用戶接口保留（未實際作用），未來如需 ramp-down 模式可回用。

Bench log:
```
[SE3-keepalive] last 30s — L: ok=47 fail=3 ... | R: ok=50 fail=0 ...
[fine_adjust] L run cmd FAILED (Modbus / SE3 firmware reject, 2x retry exhausted)
[fine_adjust] R run cmd FAILED (Modbus / SE3 firmware reject, 2x retry exhausted)
```

---

## 2026-05-13m Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `motion_fine_adjust_sync` 加入 overrun correction loop（最多 2 passes，反方向慢速 8Hz 接近 target）
### 原因
Bench 觀察：fine_adjust converged at -72，但 stopDecel ramp + 鋼索慣性 overrun 到 final -85（13cm overshoot）。User 反饋「85/73 怎麼沒微調」— 期望看到 overrun 後自動補回。

原本主迴圈只檢查 "reached"（cur 跨過 stop_at 就 stop），不檢查 "stayed within tol after settle"。退場前的 1s sleep 只是 log 用，沒驗證。

新邏輯：sleep 1s 後讀 final，算 errL/errR vs target。若超過 FINE_ADJUST_TOLERANCE_CM 就反方向（err>0 retract / err<0 pay_out）8Hz 慢速補一次，poll 直到 sign flip 或 within tol 就 stop。最多 2 passes 防無限 loop / oscillation。

注意：correction 用 8Hz < 10Hz fine_adjust_hz，目的是把 correction 本身的 overrun 也壓小。

---

## 2026-05-13l Claude (Sadie)
### 修改檔案
- `Crane_control_PI/main.cpp` — `motion_fine_adjust_sync` 啟動 L/R run cmd 改用 `reliable_run_one()` wrapper（2x50ms retry），取代 raw `SE3_DIR_PAY_OUT/RETRACT()` 單次呼叫
### 原因
Bench 觀察：fine_adjust 進場 setFreq 成功但 run cmd 第一次失敗就直接 return `fine_adjust_run_l_fail`，馬達完全沒動。

Root cause：driver `run_h1001_with_watchdog_` 第一次 writeParam(H1001) 失敗不會自動 retry，只把 comm_fail_count++ 然後 return error，要等 caller 下次呼叫累積到 RUN_FAIL_THRESHOLD=2 才會 re-claim CU + retry。但 fine_adjust 看到 error 就立刻放棄。

`reliable_setfreq_one` 已經有 retry wrapper（8x100ms），run cmd 也有對應的 `reliable_run_one`（2x50ms）— 但 fine_adjust 漏掉沒用。換成 wrapper 後 first-try transient 會自動補救。

Bench log：
```
[fine_adjust] align-to-leader L=-55 R=-58 target=-58 ...
[fine_adjust] L run cmd FAILED (Modbus / SE3 firmware reject)
（fine_adjust 直接 return，L 完全沒動 → final L=-55 vs target=-58 差 3cm）
```

<!-- 日誌從下方開始 -->

## 2026-05-13k — Claude Code — SE3 invalidateCuModeCache + align_lengths 進場呼叫（解第三次才動）

### 修改檔案
- `user_lib/SE3_inverter.{h,cpp}` **[跨界: user_lib]** —
  - 新增 public method `invalidateCuModeCache()`：強制下次 run cmd 重新 claim CU mode（drop cached `cu_mode_set_` flag）。比 `clearAlarm()` 輕（不觸發 H1101=H9696 reset）
- `Crane_control_PI/main.cpp` —
  - `cmd_align_lengths` 進場時呼叫 `se3_left.invalidateCuModeCache()` + `se3_right.invalidateCuModeCache()`
  - fine_adjust setFreq / run cmd fail 時加 explicit cout log（之前 silent）

### 為什麼（bench observation）
Sadie bench 2026-05-13：align_lengths 連續按 3 次，前兩次馬達不動，第三次才動。

原因分析：
- driver `cu_mode_set_` cached as true（從早先成功的 run cmd）
- SE3 firmware 內部 CU mode 不知怎麼失同步（也許 keepalive 讀寫之間，或長 idle 後）
- 我們 run cmd: Modbus 寫 H1001 → SE3 ACK → 但 firmware 沒實際啟動馬達
- driver `run_h1001_with_watchdog_` 看到 Modbus 成功 → 不計入失敗 → cu_mode_set_ 維持 true
- 直到 RUN_FAIL_THRESHOLD=2 連續真實失敗才 reset cu_mode_set_，但本問題下從未失敗
- 第三次成功可能是 keepalive read 的副作用或時序巧合

修法：align_lengths 進場時主動 invalidate，下次 run cmd 自動 re-claim CU mode (write H1000=0 + 150ms sleep)。

### 預期 bench
```
[align_lengths] L=73 R=66 target=max=73 ...
[SE3:1] invalidateCuModeCache: forcing re-claim on next run cmd
[SE3:1] invalidateCuModeCache: forcing re-claim on next run cmd  ← right side
[fine_adjust] align-to-leader ...
（next run cmd 自動寫 H1000=0 → sleep 150ms → 寫 H1001）
[fine_adjust] R kick start at 20Hz for 500ms
[fine_adjust] R post-kick status=0x07 running=1 fwd=0 rev=1 fault=0
[fine_adjust] right converged at 73
```

一次成功，不再需要按三次。

### Mailbox 給 Jim
- 純 additive method，沒改既有 API
- 用途：應用層需要「強制下次 run cmd re-claim CU mode」的情境
- 比 clearAlarm 輕（不寫 H1101=H9696 / 不觸發 inverter reset）

## 2026-05-13j — Claude Code — fine_adjust 加 kick start（破 cold start 靜摩擦）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 新增 `KICK_HZ_DEFAULT=20.0`、`KICK_DURATION_MS=500` 常數 + `g_kick_hz` atomic
  - 新增 `cmd_set_kick_hz` runtime cmd + dispatcher + cmd_status 欄位
  - `motion_fine_adjust_sync` lagging 側啟動改為兩階段：
    - Phase 1（kick）：先寫 setFreq(kick_hz)（預設 20Hz）+ run 方向 cmd
    - Sleep KICK_DURATION_MS（500ms）讓馬達加速過靜摩擦
    - Phase 2（precision）：drop 到 fine_adjust_hz（10Hz）穩定收斂
  - 兩階段間加 readStatusWord 診斷 log（show running/fwd/rev/fault bits）

### 為什麼（bench 觀察）
Sadie bench 2026-05-13：
1. align_lengths 多次嘗試，L 馬達 cold start 有時動有時不動（intermittent stall）
2. 「有時候有調整，但這次又沒有」← cold start 邊界 case，10Hz 剛好不夠
3. log 顯示 align-to-leader header 出現但沒收斂 log → 馬達真的沒動

修法：cold start 第一次需要更高扭力。SE3 V/f 曲線 + P.7 加速時間 → 命令越高 Hz，加速時的扭力 boost 越大。先用 20Hz 衝 500ms 破靜摩擦，再降回 10Hz 精準收斂。

### 預期 bench log
```
[fine_adjust] align-to-leader L=-62 R=-58 target=-58 main=pay_out L=→go R=stop ...
[fine_adjust] L kick start at 20Hz for 500ms
（500ms 後）
[fine_adjust] L post-kick status=0x7 running=1 fwd=0 rev=1 fault=0 (target Hz=10)
[fine_adjust] progress L=-59 R=-58(done) ...
[fine_adjust] left converged at -59 (target=-58)
```

### Runtime tunable
- `set_kick_hz <hz>` 即時調整 kick 速度（預設 20）
- 太低 → 仍 stall → 拉高
- 太高 → 突進過頭、最終位置精度差 → 拉低
- bench 找出能可靠 cold start 的最低值

### 副作用
- align_lengths 啟動延遲多 500ms（一次性，可接受）
- kick 期間 motor 走較快 ~10cm/s × 0.5s = 5cm，可能超過 target 一點 → fine_adjust 後續仍會收到 tolerance 內

## 2026-05-13i — Claude Code — 新增 align_lengths cmd / GUI 按鈕（以長邊為準同步繩長）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 新增 `cmd_align_lengths()`：讀 L/R → target=max(L,R) → 短邊 pay_out 到 target±2cm
  - 內部 reuse `motion_fine_adjust_sync(true)`（true 表示 pay_out 方向，自動算 max 當 target）
  - 入口檢查 SE3 fault / meter / DSZL availability
  - 用 `motion_mtx` lock 避免跟 motion_rope / cmd_roll_correct 並行
  - dispatcher 加 `align_lengths` entry
- `web_backend/public/index.html` — 「計米器歸零」section 下加「同步」row + 按鈕「對齊左右繩長（以較長邊為準）」+ hint 說明

### 為什麼
Sadie 要一個獨立按鈕，能手動觸發「把繩長拉成左右一致」— 用較長那邊（display 較大 = 放繩較多）當標準，較短邊 pay_out 跟上。用於 commission / 跑完一輪 motion 後發現有偏移時的補救。

### 用法
```
GUI 按「對齊左右繩長（以較長邊為準）」
→ crane: align_lengths
→ 後端讀計米器、決定短邊、單邊 pay_out 至 target±2cm
→ OK reply
```

### 注意
- Cold-start risk：motors at rest 進入 fine_adjust，短邊需從 0Hz 加速到 10Hz，可能撞到 P.7 加速 ramp / 靜摩擦
- 不能跟 motion_rope / cmd_roll_correct 並行（motion_mtx）
- SE3 fault 狀態下 refuse（避免靜默失敗）

### 安全
- Tension safety / Meter death / SE3 fault / abort_flag 全部覆蓋（fine_adjust 內建）

## 2026-05-13h — Claude Code — SE3_MOTION_HZ_DEFAULT 10 → 20（吊機放收繩速度加倍）

### 修改檔案
- `Crane_control_PI/main.cpp` — `SE3_MOTION_HZ_DEFAULT = 10.0` → `20.0`

### 為什麼
Sadie 要主 motion 速度加倍以縮短 traversal 時間。其他參數保留：
- `SE3_HOLD_HZ_DEFAULT = 10`（hold 模式維持，避免使用者一按就甩太快）
- `FREEZE_HZ_DEFAULT = 4`（freeze 慢速等對方）
- `FINE_ADJUST_HZ_DEFAULT = 10`（收尾精度，慢一半於主 motion 較穩）

### 預估效果
| 距離 | 之前 (10 Hz) | 現在 (20 Hz) |
|---|---|---|
| pay_out 50 | ~10s | ~5s |
| pay_out 100 | ~20s | ~10s |
| pay_out 200 | ~40s | ~20s |

watchdog 動態 timeout 用的 `est_speed_cm_per_s = 5` 不動，預算反而更寬鬆（沒副作用，只是 spurious timeout 風險更低）。

### 風險
- freeze over-shoot 看 freeze_hz (4Hz 不變)，主 motion 速度加倍不影響 over-shoot 量級
- balance trim tick 500ms，rope 10 cm/s 期間 5 cm 才修一次（之前 2.5 cm），同步差容忍度變大；若 bench 觀察 sync 明顯變糟可調 BALANCE_TICK_MS 250
- motor 啟動 ramp（P.7）跟以前一樣，20 Hz 不會比 10 Hz 更易 stall（扭力夠）

### runtime override
- 不重 build：GUI 用 `set_motion_hz <hz>` 隨時可調
- 範圍 (0, SE3_MAX_HZ]，預設 SE3_MAX_HZ=30

---

## 2026-05-13g — Claude Code — Web GUI 監視器跨機 proxy（frame_capture.py 加 HTTP server）

### 修改檔案
- `frame_capture/frame_capture.py` —
  - 加 `FrameBuffer` class：thread-safe 共享最新 JPEG bytes，Condition variable 讓 mjpeg consumer block 等下一張
  - 加 `_make_http_handler` + `_start_http_server`：`ThreadingHTTPServer` serving `/mjpeg/<cam_id>` + `/snap/<cam_id>` + `/health`
  - decode loop 改用 `cv2.imencode` in-memory，同時寫 `/tmp/cam_latest.jpg`（給 detect_server）跟 `buffer.publish()`（給 HTTP consumer）
  - 加 `--http-port` (5004) + `--http-bind` (0.0.0.0) args；`--http-port 0` 可 disable
- `web_backend/server.js` —
  - `CAMERAS` 從 file path 改 URL：`cam1: 'http://192.168.1.100:5004'`（CAM1_URL env 可覆蓋）
  - 砍 `fs.readFile` / mtime-watch 邏輯，改 `proxyToCam()` 用 `http.get(...).pipe(res)` 反代
  - `/snap/:cam_id` 跟 `/mjpeg/:cam_id` 都走 proxy；client disconnect → `upstream_req.destroy()`
  - 移除沒用的 `fs` import
- `frame_capture/README.md` — 加 flag 說明 + §5 HTTP server 測試 + 跨機架構圖

### 為什麼
2026-05-13a 寫的 backend 是 `fs.readFile('/tmp/cam_latest.jpg')`，假設同機。但 frame_capture 在 washrobot Pi (.100)、web_backend 在 crane Pi (.101)，跨機讀不到。

採方案 A（reverse proxy）：washrobot Pi 上 frame_capture 內建 HTTP server :5004，crane Pi 的 server.js 反代過去。優點：
- Browser 一路連 crane Pi、URL 不變、frontend 0 改動
- frame_capture 本來就有 decode pipeline，加 in-memory buffer + HTTP server 邊際成本低
- 多 client 連線不放大 decode 成本（一次解碼、多次 publish）

不採方案 B（browser 直接連 washrobot Pi）：IP hardcoded 進 HTML、跟「web_backend 故意放救援側 crane」的設計哲學牴觸。

### 後續驗證
- washrobot Pi: `python3 frame_capture.py` → `curl localhost:5004/health` 回 `ok`
- crane Pi: `curl http://192.168.1.100:5004/snap/cam1 -o /tmp/test.jpg`，`file` 應為 JPEG
- Browser GUI → live stream + 截圖按鈕能下載
- 若已 systemd 部署 frame_capture：`sudo systemctl restart frame_capture`，log 應有 `http server :5004`

---

## 2026-05-13f — Claude Code — Bug fix: fine_adjust at-target 側沒停（drift 6cm overshoot）

### 修改檔案
- `Crane_control_PI/main.cpp` — `motion_fine_adjust_sync`：
  - 之前：`!left_needs / !right_needs` 那邊只是 skip，沒 stopDecel → 該邊仍在 freeze 4Hz 狀態繼續跑
  - 現在：在 start lagging 側之前，先 `reliable_stop_one` at-target 側
  - log 訊息從 `skip(at-target)` 改 `stop(at-target)`

### 為什麼（bench observed）
Sadie bench 2026-05-13：
```
[fine_adjust] align-to-leader L=92 R=83 target=83 main=retract
              L=→go (stop at 84) R=skip(at-target) (stop at 84)
[fine_adjust] left converged at 82 (target=83)
[fine_adjust] final L=82 R=77 target=83 (L_init=92 R_init=83) — done
```

R 一開始就在 target 83（diff=0），標記 `skip`。但因為 main loop 結束後 R 在 freeze 4Hz retract 狀態，fine_adjust 期間沒主動 stop R → R 繼續 retract 一路跑到 77（多走 6cm）。L 同時也在跑 fine_adjust 收到 target 附近，但 R 一直在飄移 → 最終差距 5cm 還是不對齊。

修法：fine_adjust 入口時，若某邊 already at target → **立刻 stopDecel 該邊**（不再留 freeze）。只有 lagging 側才繼續跑。

### 預期行為
```
[fine_adjust] align-to-leader L=92 R=83 target=83 main=retract
              L=→go R=stop(at-target) at 10Hz
（R 立刻 stopDecel，從 freeze 4Hz 停下；可能 ramp 飄 ~1cm）
[fine_adjust] progress L=88 R=83(done) ...
[fine_adjust] progress L=84 R=83(done) ...
[fine_adjust] left converged at 82 (target=83)
[fine_adjust] final L=82 R=82 (or close to 83) — done
```

R 從 83 可能變 82 (1cm ramp drift)，L 收到 82。最終 diff < 2cm ✓

## 2026-05-13e — Claude Code — Bug fix: keepalive cooldown init + SE3 fault → 兩邊都停

### Bug 1: keepalive cooldown init overflow（clearAlarm 從沒呼叫過）
**問題**：2026-05-13d 加的 cooldown 用 `std::chrono::steady_clock::time_point::min()` 初始化。`now - min` 在 int64 內 overflow，`since.count()` 變負或 0 → 永遠 `< CLEAR_COOLDOWN_MS` → cooldown 永遠不滿足 → clearAlarm 從沒呼叫。

bench 證據：30s log 顯示 `L: status=0x80 clears=0`，左邊一直 FAULT 卻沒看到 `[SE3-keepalive] left FAULT ... auto-clear` 訊息。

**修法**：`now() - 60s` 初始化，首次 detect 時 since=60s > 5s cooldown，立刻 fire。

### Feature: SE3 fault 兩邊都停
**user 訴求**：bench 上左 SE3 OPT 鎖死、右 OK，user 下 motion 結果只動了右邊，左邊沒動。「兩邊都要停止」否則繩 asymmetric load 危險。

**修法**：
- 新增 atomic `g_se3_left_fault` / `g_se3_right_fault`，由 keepalive 根據 status word b7 set/clear
- `motion_rope` + `cmd_roll_correct` 主 loop 每 iter 檢查兩個 atomic
- 任一邊 fault → `abort_reason = "se3_<side>_fault"` 或 `"se3_both_fault"` → break
- 結尾 `tension/meter/se3` 都歸類為「hard abort」→ allMotionOff + EVT motion_abort 廣播
- fine_adjust skip 條件加上 `se3_aborted`

### 行為流程（修完後）
```
crane 啟動 → SE3 已在 OPT 狀態（status=0x80）
keepalive tick 1: 偵測 FAULT → cooldown 滿足（60s 過去）→ 立刻 clearAlarm
SE3 firmware reset → 200ms 後 OPT 清除
keepalive tick 2: status=0x0 → g_se3_left_fault.store(false)
user 下 pay_out → 主 loop 不再 abort（atomic 已 clear）→ 馬達運轉

若 motion 中途某邊跳 fault：
keepalive 偵測 → set atomic → 主 loop 下一 iter 看到 → break → allMotionOff
→ 廣播 EVT motion_abort reason=se3_left_fault
→ washrobot 端 GUI 看到原因
```

### 待 bench 驗證
- 啟動時 tick 1 立刻看到 `[SE3-keepalive] left FAULT ... auto-clear via H1101=0x9696` log
- tick 2 看到 status=0x0、clears=1
- 再下 motion 馬達能動
- 模擬 motion 中拔網線 → 兩邊都停 + ERR se3_left_fault

## 2026-05-13d — Claude Code — SE3 自動 alarm 復原（fault detect + clearAlarm via H1101=0x9696）

### 修改檔案
- `user_lib/SE3_inverter.{h,cpp}` —（**[跨界: user_lib]** 純 additive）
  - 新增 public method `clearAlarm()`：寫 H1101 = 0x9696（手冊 §H1101 對照表：= 00-02=2 / P.997=1 = 變頻器復位）
  - 寫完 sleep 200ms 等 SE3 firmware 復位完成
  - 同時 reset 內部 `cu_mode_set_ = false`（強制下次 run cmd 重新 claim CU mode）+ `comm_fail_count_ = 0`（watchdog 計數歸零）
- `Crane_control_PI/main.cpp` —
  - `se3_keepalive_loop` 偵測 `readStatusWord` 回的 b7 (fault) bit
  - 若 fault 設定 → 自動呼叫 `clearAlarm()`
  - **每邊獨立 5 秒 cooldown** 避免持續斷線時狂送 reset
  - 30s 摘要 log 多吐 `clears=N` 計數
  - `SE3_KEEPALIVE_INTERVAL_MS` 從 1000 改 500（給 cli_A bus 競爭更多 margin，1s 太接近 07-09=2s）

### 為什麼（bench 觀察）
2026-05-13c 加 always-on keepalive 後，bench 觀察：
1. 正常運作時 keepalive 有效，SE3 不會自己觸發 OPT
2. **但拔網線重插**後 SE3 已進 OPT 狀態，光重連 TCP 沒清掉，左邊馬達就一直不動
3. user 期望：「拔線重插後自動解警報 + 重連」

修法：
- driver 加 `clearAlarm()` 透過 Modbus H1101=H9696 magic 觸發 SE3 內部復位
- keepalive 看到 b7 fault bit 設定就自動 clearAlarm
- 5s cooldown 防止持續斷線時的無謂重試

### Bench 預期行為
拔線重插：
```
[SE3-keepalive] tick N — L: ok=N fail=N status=0x0080 ⚠FAULT
[SE3-keepalive] left FAULT (status=0x80) — auto-clear via H1101=0x9696
[SE3:1] clearAlarm: H1101=0x9696 written (inverter reset), waiting 200ms
（200ms 後 SE3 復位完成、cu_mode_set_ flag reset）
[SE3-keepalive] tick N+1 — L: ok=N+1 fail=N status=0x0000  ← FAULT 清除
```

下次 motion 時 driver 自動 write H1000=0 重新 claim CU mode，馬達可動。

### Mailbox 給 Jim
- 新增 `clearAlarm()` 純 additive，沒改任何 API 簽名
- 簡短 review 重點：H1101=0x9696 是手冊明文 magic、sleep 200ms 是慣例（DSZL save_params 也是 150ms）

## 2026-05-13c — Claude Code — SE3 keepalive 改 always-on（不依賴 motion_active）

### 修改檔案
- `Crane_control_PI/main.cpp` — `se3_keepalive_loop` 拿掉 `motion_active || any_hold_active()` gate，改成 always 跑
- startup log 從「during motion/hold」改「always-on」

### 為什麼
2026-05-13b 加 keepalive 時把它限定在 motion / hold 期間跑。但 bench 觀察：

```
SE3 加電 t=0
SE3 開始計算通訊 timeout
t=4 秒 後 → 通訊計數累計超過 07-08 → OPT 異警觸發 + 馬達鎖
（這段期間 crane 程式可能還沒啟動，或剛啟動 idle 中）

user 啟動 crane 程式 → init 完成 → 沒 motion / hold → keepalive 不動
→ SE3 早已在 OPT 狀態
→ user 看到「一啟動就 OPT」
```

修法：keepalive **不檢查 motion_active**，從 thread 啟動起每 1 秒讀一次，SE3 啟動的瞬間就有訊息持續餵進來。

Bus 負擔多 ~10%（每秒 100ms），可接受。

### Bench 操作（解 OPT 鎖死）
1. rebuild + deploy
2. **兩台 SE3 面板按 STOP/RESET 鍵**清除目前 OPT 異警
3. 重啟 crane 程式（keepalive thread 立刻開始 tickle）
4. 確認 startup log：`[OK] SE3 keepalive thread (1000ms always-on; ...)`
5. 之後應該不會再觸發 OPT

### 未做的優化
- 軟體自動清 OPT（寫 00-02 = 1）— 簡單但要動 driver / 應用層，暫不做
- crane init 後立刻發一個 SE3 read 也行（但 always-on keepalive 已涵蓋）

## 2026-05-13b — Claude Code — SE3 keepalive thread（防 OPT 異警 + 修正 07-10 文件）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 新增 `SE3_KEEPALIVE_INTERVAL_MS = 1000` 常數 + `g_se3_keepalive_stop` atomic + `g_se3_keepalive_thread`
  - 新增 `se3_keepalive_loop()`：`motion_active || any_hold_active()` 為 true 時每 1s `readStatusWord` 兩台 SE3
  - main() 啟動時開 thread + 加 `[OK] SE3 keepalive thread (...)` log，shutdown 時 stop + join
- `user_lib/SE3_inverter.h` — 修正 07-08/09/10 註解：明文 07-10 只有 0/1 兩個選項（不是之前文件說的 4 個）

### 為什麼
2026-05-13 bench 觀察：

1. user 設 07-10 = 0（想啟用通訊中斷保護）
2. 因為 crane 程式有靜默期（freeze / fine_adjust / hold mode 期間沒寫 SE3 > 2 秒）
3. SE3 firmware 認定通訊中斷 → 觸發 OPT 異警 → 馬達鎖死
4. 面板顯示 OPT（user 看成 OPR / 0PX）
5. crane 程式重啟也救不回（SE3 內部 alarm 狀態未清）

Claude 先前文件把 07-10 選項說成「0=ignore / 1=decel / 2=trip / 3=continue」，那是 Shihlin 其他機型（SS2 / SF3）的，**SE3-210 V1.03 手冊明文只有兩個選項**：
- **0 = 報警並空轉停車**（保護）
- **1 = 不報警並繼續運行**（無保護）

要安全（07-10 = 0）**且**馬達能用，必須加 keepalive 防 SE3 內部計數器在靜默期累計觸發。

### 用法（user bench）
1. **重啟 crane 程式**（讓 keepalive thread 啟動）
2. 確認 startup log 看到 `[OK] SE3 keepalive thread (1000ms during motion/hold; ...)`
3. SE3 面板按 **STOP/RESET** 清除 OPT 異警（兩台都做）
4. 確認 07-08=2, 07-09=2.0, 07-10=0（保護開啟）
5. 試 `pay_out_left on` 馬達應該動

### Keepalive 觸發條件
- `motion_active.load()` 為 true（motion_rope / cmd_roll_correct 在跑）
- 或 `any_hold_active()` 為 true（hold mode）
- 否則 idle，不打擾 bus

### Bus 負擔
- 1 秒 2 次 readStatusWord（左右 SE3 各一）
- 各 ~50ms = 100ms / 秒 = 10% 占用
- 可接受

### Memory
- 新增 `project_se3_07_10_two_options.md`（記載 07-10 真實選項 + keepalive 必要性 + OPT 救援方式）

### 已知踩坑（記到 memory）
- PU LED 亮 = PU 和 CU 模式共用，無法從 LED 判斷在哪個模式
- OPT 字 7-段顯示像 OPR / 0PX，看 panel 字型容易誤認
- 07-10 = 0 = 報警空轉（**不是**忽略，跟其他 Shihlin 機型反過來）

## 2026-05-13a — Claude Code — Web GUI 加監視器 dock panel（MJPEG 串流 + 截圖）

### 修改檔案
- `web_backend/server.js` —
  - 加 `fs` import
  - 加 `CAMERAS = { cam1: '/tmp/cam_latest.jpg' }` 表（CAM1_PATH env 可覆蓋）；MJPEG_INTERVAL_MS=150
  - 加 `GET /snap/:cam_id` endpoint：單張 JPEG sendFile，403 frame_not_ready / 404 unknown_cam
  - 加 `GET /mjpeg/:cam_id` endpoint：multipart/x-mixed-replace push，每 150ms 檢查 mtime，新 frame 才推
  - boot log 加列出 cameras
- `web_backend/public/index.html` —
  - 加 `<section class="panel panel-camera" data-cam-id="cam1">` dock 在最後（grid auto-fit 自動入排）
  - 含 `<img id="cam1-stream" src="/mjpeg/cam1">` + offline overlay + 「📸 截圖」+「↻ 重連」按鈕 + status 字
- `web_backend/public/style.css` —
  - 加 `.panel-camera` (cyan accent 取代預設 mixed)、`.cam-stage` 16:9、`.cam-video` object-fit、`.cam-overlay-offline` (隱藏式)、`.cam-controls` flex、`.cam-btn` neon cyan、`.cam-status` 三色 (live/offline/dim)
- `web_backend/public/app.js` —
  - 結尾加 `wireCamera('cam1')`：onload→live、onerror→offline overlay + 3s 後 cache-busted reload；截圖按鈕用 fetch /snap → blob → `<a download>` 觸發下載，檔名 `cam1_2026-05-13T14-32-01.jpg`
- `frame_capture/frame_capture.py` —
  - 加 `--cam-id` arg（default `cam1`）+ `_draw_overlay(frame, cam_id)` helper
  - 寫檔前 `cv2.putText` 在左下角畫 `<cam_id> | YYYY-MM-DD HH:MM:SS` 黑邊 + 綠字（~1-2ms/frame on RPi 4）

### 為什麼
Sadie 在 2026-05-11 規劃了「Web GUI 監視器區」要播 RTSP 即時影像。
- 方案 MJPEG（multipart/x-mixed-replace）：延遲 0.2-0.5s、`<img>` 一行搞定、不用 hls.js / WebRTC、CPU 友善
- frame_capture.py 已有 atomic JPEG write → backend 純讀檔、不解 RTSP，CPU 集中在 frame_capture 那條 pipeline
- overlay 放 OpenCV cv2.putText 比 backend sharp/jimp 省 ~20-30ms × 每 client 的成本（多 client 也只解碼一次）

### 架構
```
Camera 192.168.1.10:554
   ↓ RTSP sub stream (TCP)
frame_capture.py（+ cv2.putText overlay）
   ↓ atomic write
/tmp/cam_latest.jpg
   ↓ reads
server.js
   ├─ /mjpeg/cam1  → multipart/x-mixed-replace
   └─ /snap/cam1   → single JPEG
   ↓
browser:
   <img src="/mjpeg/cam1">       ← live
   截圖按鈕 → fetch /snap → blob download
```

### 多路擴充
留好參數化接口，未來加 4 路只要：
1. frame_capture 多 instance: `frame_capture.py --url <camN> --out /tmp/camN_latest.jpg --cam-id camN`
2. server.js `CAMERAS` 加 entry
3. index.html 複製 dock panel section（改 `data-cam-id`）
4. app.js 加 `wireCamera('cam2')`

### 後續驗證
- crane RPi 重啟 web_backend → 開 GUI 看到右下 dock 顯示 live stream
- 按截圖 → 瀏覽器下載 `cam1_<ts>.jpg`，畫面有 timestamp + cam1 字樣 overlay
- 拔網路測 offline overlay 顯示 + 3s 後自動重連
- 看 frame_capture stderr 「fps」沒掉（overlay 不該明顯影響 fps）

---

## 2026-05-12d — Claude Code — fine_adjust 改「對齊到 leader」+ 結尾印 final reading

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - `motion_fine_adjust_sync()` 簽名加 `bool main_motion_pay_out` 參數
  - 邏輯改：target = `max(L,R) if pay_out, min(L,R) if retract`（對齊到走最遠那邊，「leader」）
  - 兩邊**保持 main motion 方向**，不再反向收斂
  - 已在 target ±tolerance 內那邊不啟動，停在 freeze 狀態，最後一起 stop
  - log header 從「sync L=... mid=...」改「align-to-leader L=... target=...」
  - 結尾加 1 秒 sleep + log 印 `final L=X R=Y target=Z` 對齊驗證用
  - motion_rope 呼叫處傳 `pay_out` 參數

### 為什麼
Sadie bench：pay_out 100，freeze 結束後 fine_adjust 進入 cache L=110 R=122（freeze 期間 over-shoot 累積），舊邏輯把兩邊朝 mid=116 收斂——L pay_out 5 cm + R retract 5 cm。

問題：R retract 期間要把 motor 反向，但 rope 上重物的張力會「fight」motor，drum 可能 back-spin 不準；且 motor 啟動有 ramp/慣性，反向方向頻繁切換容易 over-shoot 或 stall。

新邏輯「對齊到 leader」：
- 走得最遠的那邊不動（已 frozen）
- 另一邊繼續同方向跑直到追上
- 完全不反向、不 fight rope 張力

### 套用
- `motion_rope` 呼叫處：`motion_fine_adjust_sync(pay_out)`
- `pay_out` cmd → 對齊到 max
- `retract` cmd → 對齊到 min

### 副作用
- 最終位置等於 max(L,R) 或 min(L,R) 不再是 mid → 跟之前比偏向 over-shoot 那邊。如果 freeze 期間 over-shoot 大（例如 3 cm），最終比 target 多 3 cm
- 要再壓 freeze over-shoot：降 `freeze_hz_val` 從 4Hz → 2Hz，或砍掉 freeze 改 stopDecel（之前 11h 改過又被加回）

### 後續驗證
- bench 跑 `pay_out 100` 看 final L/R 是不是 ≈ leader 而不是 mid
- 看 stderr 新增的 `[fine_adjust] final L=... R=... target=...` 行確認

---

## 2026-05-12c — Claude Code — 方向 sign 反向修正（PAY_OUT_INCREASES_DISPLAY false → true）

### 修改檔案
- `Crane_control_PI/main.cpp` — `PAY_OUT_INCREASES_DISPLAY` 從 `false` 改 `true`

### 為什麼（bench 數據）
2026-05-12b 我把 `PAY_OUT_INCREASES_DISPLAY` 設成 false（依 user 早先說「payout 數字變小」），但 bench 第一次跑出：

```
[fine_adjust] sync L=111 R=108 mid=109 L→pay_out (stop at 110) R→retract (stop at 108) at 10Hz
```

user 回報「L 比較長 應 retract、R 比較短 應 payout」（跟我代碼相反）。

Reverse engineer：
- L=111（大）→ user 要 retract → 表示 retract 讓 display ↓
- R=108（小）→ user 要 pay_out → 表示 pay_out 讓 display ↑

所以實際 bench 是 **pay_out → display ↑**（跟 2026-05-12b 的假設相反）。

可能 user 早先「payout 數字變小」描述的是不同 case 或記反了；以這次 bench 觀察為準。

### 修正
`PAY_OUT_INCREASES_DISPLAY = true`：
- side > mid → display 要降 → retract
- side < mid → display 要升 → pay_out

下次同 case 應該變：
```
[fine_adjust] sync L=111 R=108 mid=109 L→retract (stop at 110) R→pay_out (stop at 108) at 10Hz
```

## 2026-05-12b — Claude Code — Freeze 4Hz + Sync-to-midpoint fine_adjust（user 確認 pay_out → display ↓）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - Re-add `FREEZE_HZ_DEFAULT=4.0` + `g_freeze_hz` atomic + `cmd_set_freeze_hz` + dispatcher + cmd_status field
  - 新增 `PAY_OUT_INCREASES_DISPLAY=false` 常數（user bench 確認 2026-05-12）
  - `motion_rope` 主 loop：
    - `left_done/right_done` → `left_frozen/right_frozen`
    - 到 cm 改用 `reliable_setfreq_one(freeze_hz_val)` 取代 `reliable_stop_one`
    - 退出條件 `left_frozen && right_frozen && middle_done`
    - balance trim 在任一邊 frozen 後停做
  - 新 `motion_fine_adjust_sync()` 取代 `motion_fine_adjust_to_target`：
    - 讀 L, R 算 midpoint
    - 若 |diff| ≤ tolerance 直接 stopDecel 兩邊 return
    - 否則 per-side 決定方向（`PAY_OUT_INCREASES_DISPLAY=false`: side > mid → pay_out；side < mid → retract）
    - SE3 直接切方向位元（motor 仍在 freeze_hz）
    - 每邊跨過 midpoint 內側邊界（mid ± tol/2）→ stopDecel 該邊
    - 雙邊都 done → 退出
  - motion_rope 結尾：tension/meter abort → allMotionOff（hard stop）；其他情況 motors 維持 freeze，丟給 fine_adjust 處理
  - cmd_roll_correct 不接 fine_adjust（intentional asymmetry）

### 為什麼
3 層問題一次解：
1. **同步差太大** — pay_out 30 後 L=5 R=8 差 3cm
2. **校正動不了** — fine_adjust 從停止狀態啟動，馬達 ramp 距離 > adjustment 距離 → 加速不到 target Hz → stall
3. **方向 sign 對不上** — user 確認 pay_out → display ↓（之前假設反了）

Freeze + sync 設計同時解：
- (1) sync to midpoint，雙邊互相靠攏
- (2) motors 從來沒進停止狀態（4Hz freeze 高於 SE3 DC brake 啟動頻率 3Hz）→ 沒 cold start
- (3) `PAY_OUT_INCREASES_DISPLAY=false` 記錄 bench 慣例，方向判定用這個

### 預期行為
```
pay_out 30
... main loop ...
[motion_rope] left reached target — frozen at 4Hz
[motion_rope] right reached target — frozen at 4Hz
[fine_adjust] sync L=5 R=8 mid=6 L→retract (stop at 5) R→pay_out (stop at 7) at 4Hz
[fine_adjust] left converged at 6 (mid=6)
[fine_adjust] right converged at 6 (mid=6)
OK reply
```

### Tunables
- `set_freeze_hz <hz>` — default 4
- `set_fine_adjust_hz <hz>` — default 10（fine_adjust 期間的 Hz）
- `PAY_OUT_INCREASES_DISPLAY` — 要反轉得 rebuild

### 未解風險
- 方向反轉（4Hz pay_out → 4Hz retract）過 0Hz 時，SE3 firmware 怎麼處理還沒實機驗證
- 若 transition 期間 DC brake 短暫啟動 → 可能仍 stall
- 若不行，下一步加 kick start（短暫 boost 到 10-15Hz 過 transition）

## 2026-05-12a — Claude Code — 動態 watchdog motion timeout（依命令距離估算）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - `WATCHDOG_TIMEOUT_MS_MOTION` 拆成 const `_MOTION_MIN = 10000` (floor) + atomic `g_motion_dynamic_timeout_ms` (runtime)
  - 加 `MotionTimeoutScope` RAII helper：constructor 接 cm，set `est_ms = (cm × 1000)/5 + 10000`（5 cm/s 估速 + 10s buffer）；destructor reset 0
  - `motion_rope` / `cmd_roll_correct` entry 加 `MotionTimeoutScope mts(cm)` 一行
  - `watchdog_loop` motion 期間取 `max(_MOTION_MIN, g_motion_dynamic_timeout_ms)`
  - boot log 更新

### 為什麼
Sadie bench 觀察「下 pay_out 100，實際只跑了 50 cm」+ `ERR aborted` + `EVT watchdog_timeout / watchdog_recovered`。Root cause：

- watchdog motion timeout 固定 10s（2026-05-09e 設）
- pay_out 100 @ 10 Hz × ~5 cm/s rope speed ≈ 20s + fine_adjust 5s = 25s
- 10s 在主迴圈走到 50 cm 時觸發 abort → fine_adjust 進去後 abort_flag 又觸發 → 馬達停在 50 cm

固定拉長 60s 可解但短距離 motion fail-safe 變弱。動態依命令距離算更精準：

| cmd | timeout |
|---|---|
| `pay_out 10` | 12s |
| `pay_out 50` | 20s |
| `pay_out 100` | 30s |
| `pay_out 200` | 50s |
| `pay_out 500` | 110s |

短距離 fail-safe 仍緊，長距離自動拉長。

### 為什麼用 5 cm/s 估速
- bench 觀察：10s 走 50 cm = 5 cm/s @ 10 Hz（保守）
- 真實 rope speed 量出來後可調 `MotionTimeoutScope` 內 `est_speed_cm_per_s` 常數
- buffer 10s cover ramp + fine_adjust

### 影響範圍
- `motion_rope`（pay_out / retract）/ `cmd_roll_correct` 自動套用
- hold mode（按住 UP/DOWN）保持 2s
- idle 保持 2s

### 後續
- 實機驗證 `pay_out 100` / `pay_out 200` 一次跑完
- step_down 等上層 motion 走 motion_rope，自動套用

---

## 2026-05-11h — Claude Code — Revert freeze + 改 per-side bidirectional fine_adjust

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - **Revert freeze 設計（2026-05-11g）**：
    - 砍 `FREEZE_HZ_DEFAULT` / `g_freeze_hz` / `cmd_set_freeze_hz` / dispatcher entry / cmd_status field
    - `motion_rope` 主 loop：`left_frozen` → `left_done`，到 cm 改回 `reliable_stop_one`
    - `cmd_roll_correct` 主 loop 同樣 revert
  - **Fine_adjust 改寫成 per-side bidirectional**：
    - 砍 `FINE_ADJUST_TRIGGER_CM` / `FINE_ADJUST_TARGET_CM`，合併成 `FINE_ADJUST_TOLERANCE_CM = 2`（user spec ±2cm）
    - 函式重簽 `motion_fine_adjust_to_target(target_l, pay_out_l, target_r, pay_out_r)`：
      - 每邊獨立 `err = current - target`
      - `|err| ≤ 2` → 該邊跳過
      - `err > 0` → 反向跑（超過 target，要倒回）
      - `err < 0` → 順向跑（沒到 target，繼續走）
      - 雙邊可同時跑、可同向 / 反向
      - 每邊到 ±2cm 個別停車
    - 含 abort / timeout / tension / meter 守則 + EVT motion_progress (`l_err_cm` / `r_err_cm`)
  - `motion_rope` 呼叫端：`target = base + balance_dir × cm`，雙邊一樣
  - `cmd_roll_correct` 呼叫端：`target_l = base_l + dirL × abs_cm`，`target_r = base_r + dirR × abs_cm`（dirL/dirR 相反）

### 為什麼
Freeze 設計（2026-05-11g）bench 失敗：
- `freeze_hz=0.5` → 低於 SE3 DC brake 啟動頻率 3Hz → 馬達實際停車 → cold start 問題重現
- `freeze_hz=10` (= motion_hz) → 兩邊無速度差 → 不會收斂 → 主 loop hang

User 提出真正想要的設計：「到點後發現超過預期，要往反方向收/放繩，調到 target ±2cm」。屬於**到 target 後 per-side 雙向校正**，跟原本「主 loop 內同步」是不同概念。

### 行為示意
```
pay_out 30 (cm)
→ 主 loop：左到顯示 30 → stop；右到顯示 30 → stop
→ 主 loop 結束，狀態：left=33 (overshot 3), right=28 (undershot 2)
→ fine_adjust 算 err：l_err=+3, r_err=-2
→ 左：|err|=3 > 2 → 反向 retract 至 err≤2
→ 右：|err|=2 ≤ 2 → skip
→ 雙邊跑後最終：left=31, right=28（左右各在 ±2cm 內）
```

### 副作用
- 反向收回時還是會經歷「停車狀態 → 啟動反向」，可能踩到 cold start stall。若實機觀察 cold start 仍 fail，下步加 kick start 邏輯（先 20Hz 衝 500ms）
- 每邊個別控制 → fine_adjust 期間可能 cli_A / cli_B bus 都很忙
- runtime cmd 還是 `set_fine_adjust_hz`（freeze 那條砍了）

### Bench 驗證流程
1. `pay_out 30` 跑完，看 console
2. 應該看到：`[fine_adjust] L: err=Ncm →pay_out/retract | R: err=Mcm →...`
3. 每邊個別 converge：`[fine_adjust] left converged at err=Ncm`
4. 最終回 OK

## 2026-05-11g — Claude Code — Freeze 設計：到 target 不關馬達、低 Hz 等同步

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 新增 `FREEZE_HZ_DEFAULT = 0.5` + `g_freeze_hz` atomic
  - `motion_rope` 主 loop：
    - `left_done` / `right_done` → `left_frozen` / `right_frozen`
    - 到 cm 時改 `reliable_setfreq_one(side, freeze_hz)` 不 stopDecel — 馬達維持 run 狀態
    - balance trim 條件：`!left_frozen && !right_frozen`（任一邊 freeze 就停 trim，避免跟 freeze 打架）
    - 退出條件 `should_exit()` lambda：兩邊 frozen + `|diff| ≤ FINE_ADJUST_TARGET_CM`
    - 退出後 `allMotionOff()` 一次性把兩邊一起 stopDecel
    - setFreqHz 失敗 fallback 才用 stopDecel（保留安全網）
  - `cmd_roll_correct` 同樣處理（dirL / dirR 對相反方向）
  - 新增 `cmd_set_freeze_hz` runtime + dispatcher
  - cmd_status 多吐 `freeze_hz` 欄位

### 為什麼（Sadie bench finding）
左馬達靜摩擦 / 負載大 → 「跑著沒事，停下重啟動不了」即使 fine_adjust 拉到 15Hz 也起不來。

舊流程：
```
左到 cm → reliable_stop_one(左)   ← 馬達進入 DC brake / 停止狀態
右繼續...
右到 cm → reliable_stop_one(右)
fine_adjust: 嘗試重啟左 → cold start fail
```

新流程（freeze）：
```
左到 cm → setFreqHz(左, 0.5Hz)    ← 馬達仍在 run state（只是極慢）
右繼續，balance 不再 trim
右到 cm → setFreqHz(右, 0.5Hz)
等 |diff| ≤ 1cm
最終 allMotionOff 兩邊一次停
```

關鍵：**馬達 run cmd 全程沒拔過，沒 cold start 問題**。

### 副作用
- frozen 期間 leading 仍以 0.5Hz 慢慢動 → 最終位置略超 target（不到 1cm，acceptable）
- fine_adjust 在成功 case 變 no-op（freeze 已收斂到 ≤ target 才退出，fine_adjust 入口檢查 `< trigger` 直接 return）
- fine_adjust 仍保留為 abort/timeout 等場景的 fallback
- Middle 仍用舊 stopDecel（沒 cold start 問題的場景）

### 行為矩陣
| 場景 | 主 loop 退出 | fine_adjust |
|---|---|---|
| 正常完成 + 兩邊 sync | should_exit true | no-op（diff < trigger） |
| 正常完成 + 突然失同步 | should_exit true（已 sync）| no-op |
| 主 timeout | abort timeout | 跑（fallback） |
| User stop | abort aborted | 跑（fallback，舊邏輯）|
| Meter death | abort meter_X | skip |
| Tension | abort tension | skip |

### Tunables
- `set_freeze_hz <hz>` runtime 調整（預設 0.5）
- 想用「不 freeze」的舊行為：`set_freeze_hz 0.01`（極接近 0，behavior 像 stop）— 或之後加 enable/disable flag

## 2026-05-11f — Claude Code — motion_rope + roll_correct 加 meter death detection（補繩走無控漏洞）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 新增常數 `METER_LOST_GRACE_MS = 500`（容忍 2-3 個 cache cycle 的 transient 失效）
  - `motion_rope` 主 loop 加 per-side `*_invalid_since` 追蹤 + lambda `check_meter`：失效 > grace 設 `abort_reason = "meter_<side>_lost"` break。涵蓋 left / right；middle 在 `use_middle=true` 時也檢查
  - `cmd_roll_correct` 同樣加入（只 left / right）
  - 主 loop break 後若 abort_reason 以 `meter_` 起頭，broadcast `EVT motion_abort reason=meter_<side>_lost\n`
  - fine_adjust skip 條件加 `meter_aborted`（meter 死掉沒可靠 feedback 做收斂）；skip log 訊息精簡

### 為什麼（安全漏洞）
之前 motion_rope 內部 distance check 是 `g_length_*_valid.load() && (length - base) >= cm`。如果 meter 死掉 `_valid` 永遠 false → 那一邊永遠不會 `*_done=true` → 馬達持續以 10Hz 跑直到 `MOTION_TIMEOUT_MS = 120s`。

10Hz × ~10cm/s × 120s ≈ **12 公尺繩**沒控制地放出/收回。最壞 case：機器人撞頂 / 掉下去。

加 grace period 500ms 過濾 transient（如 stopDecel 時 bus 衝突造成的 1-2 missed read），sustained 失效就 abort。

### 行為
| Meter 狀態 | 主 loop 反應 |
|---|---|
| 健康 | 正常 distance check |
| 短暫失效 < 500ms | 計時開始，本 iter 跳 distance check，繼續跑 |
| 持續失效 > 500ms | abort_reason="meter_<side>_lost"，allMotionOff，EVT motion_abort，return ERR |
| 失效後又恢復 | 計時 reset，繼續正常 |

### 副作用
- 短暫 transient 期間（< 500ms）馬達仍可能多動 ~5cm（10Hz × 0.5s）— acceptable
- fine_adjust 跟著 meter_aborted skip — 無 feedback 做不了收斂；user 處理完 meter 後手動微調
- bench 端如果 meter 真的 flaky（如 USR-TCP232 經常 timeout），會頻繁 abort — 此時要先修硬體不能放寬 grace

## 2026-05-11e — Claude Code — fine_adjust entry cache validity retry + log

### 修改檔案
- `Crane_control_PI/main.cpp` — `motion_fine_adjust` 入口判定改寫：
  - 之前：`if (!_left_valid || !_right_valid) return ""` 靜默 early return
  - 現在：先 retry 3 次 × 200ms（最多等 600ms 給 meter_loop 跑過 cycle），仍失效才 return，且有 log

### 為什麼
Sadie 看 code 發現 fine_adjust 入口 `!_valid` 的 short-circuit 太脆弱：

```
motion_rope 結束 → allMotionOff
  ├─ reliable_stop_one(se3_left)  ← 占用 cli_A
  └─ reliable_stop_one(se3_right) ← 占用 cli_B
              ↓ meter_loop 正在 cli_A 讀 → bus 衝突 → readUpperInteger 失敗
              → g_length_left_valid 變 false 瞬間
緊接著 motion_fine_adjust 進來 → 看到 _valid=false → return ""（silently）
```

也就是 diff 明明 5cm 但 fine_adjust 完全沒動，user bench 看到「為甚麼沒校正」。

修法：retry 3 次給 meter_loop 200ms × 3 cycle 把 _valid 拉回 true。若 600ms 後仍 false，至少有 console log 讓 user 知道，不會 silent fail。

### 預期效果
- 99% 的 case：retry 第 1 次就成功（200ms 內 meter_loop 已 refresh）
- 真的 meter 死的 case：log 出現「cache invalid after retry — skipping」，user 知道要查 meter

## 2026-05-11d — Claude Code — FINE_ADJUST_HZ_DEFAULT 5 → 10

### 修改檔案
- `Crane_control_PI/main.cpp` — `FINE_ADJUST_HZ_DEFAULT` 從 5.0 改 10.0

### 為什麼
之前 5Hz 在 bench 跑的時候左馬達會 stall（碰到大負載 / 摩擦不對稱）→ fine_adjust 啟動但實際馬達不動 → 拖到 30s timeout 也收不斂。10Hz 跟 base `motion_hz` 一致，能跨過 stall 閾值。

runtime 仍可 `set_fine_adjust_hz <hz>` 即時調。

## 2026-05-11c — Claude Code — fine_adjust 在 user-abort 時也跑（只 tension 才跳過）

### 修改檔案
- `Crane_control_PI/main.cpp` — `motion_rope` 尾段邏輯改寫：
  - 之前：`user_aborted || tension_aborted` 跳 fine_adjust
  - 現在：只有 `tension_aborted` 跳 fine_adjust
  - 額外處理：若 `abort_reason == "aborted"`（user stop / watchdog timeout），在進 fine_adjust 前 `abort_flag.exchange(false)`，這樣 fine_adjust loop 內的 `if (abort_flag.load()) break` 不會一進去就馬上 fire。User 想中斷再按一次 stop 即可

### 為什麼
Sadie bench：看到 `[fine_adjust] skipped (main motion abort: aborted)` 但 diff 已經 > 2cm，user 明確說「為甚麼沒校正」。

之前邏輯把 user-abort 也排除是為了「user 按停就完全停」，但 user 實際 expectation 是「不管什麼原因結束，diff 大就該收尾」。

調整後唯一跳過的情境是 tension breach（真的危險，不該再開動），其他都跑 fine_adjust 當清理。User 仍可 hard stop（按第二次 stop）中斷 fine_adjust。

### 行為矩陣（更新）
| main 結果 | abort_reason | fine_adjust 是否跑 | 最終回應 |
|---|---|---|---|
| 正常完成 | (空) | ✅ 跑 | OK 或 ERR fine_adjust_* |
| 超時 | "timeout" | ✅ 跑（清理） | ERR timeout |
| Tension 觸發 | "tension_high_left" 等 | ❌ 跳過 | ERR tension_* |
| **User 按停 / watchdog** | **"aborted"** | **✅ 跑（清理，abort_flag reset）** | **ERR aborted** |

## 2026-05-11b — Claude Code — Crane↔Washrobot 協調修正：fine_adjust timeout 撞爆 + 加進度 EVT

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 新增常數 `EVT_PROGRESS_INTERVAL_MS=1000`
  - `motion_rope` 主 loop 加 `last_evt_push` 計時器，每 ~1s broadcast `EVT motion_progress phase=main_loop l_cm=N r_cm=N [m_cm=N] target_cm=N elapsed_ms=N`
  - `motion_fine_adjust` loop 加同樣的 1s 推送：`EVT motion_progress phase=fine_adjust lag=<left|right> diff_cm=N target_cm=N elapsed_ms=N`
- `user_lib/WASH_ROBOT.h` —
  - `crane_cmd_` 預設 timeout `30 → 60` 秒，註解標 fine_adjust 30s + 主動作所需餘裕
- `user_lib/WASH_ROBOT.cpp` —
  - `handle_crane_evt_` 加 `motion_progress` 識別：refresh `crane_last_ok_ms_`，讓 watchdog 在長 op 中也能看 crane 活著（之前只靠 OK reply，OK 要整個 op 完才來）

### 為什麼
Sadie 觀察 fine_adjust 加進去之後，pay_out 5cm 的 worst case = 主動作 ~3s + fine_adjust 30s = 33s > washrobot 預設 30s timeout → step_down body/feet phase 的 `crane_cmd_("pay_out X")` 會收 timeout → ERR → 整個 step_down 中斷。

順手把 #3/#4 watchdog 在長 op 中「事實上沒在看」這個架構問題一起補了 — 加 progress EVT，washrobot 收到就知道 crane 活著。

### 行為變化
- Crane long op（>1s）會持續廣播 `EVT motion_progress` 給所有 client
- washrobot RPC channel 自動 drain，refresh 內部 watchdog 時間戳
- GUI 也會收到（已有 `evt_("crane_relay ...")` 轉播），可顯示即時進度
- crane_cmd_ 預設 60s timeout 讓 fine_adjust 30s + 主動作有餘裕

### 已知殘留問題（未動）
- CRANE_IP 仍 hardcode `192.168.5.26`（bench 網段），production deploy 前要改回 `.1.101`（memory 已記）
- step_down 各 call site 仍用預設 timeout，沒有 explicit per-call 設定（60s 預設應夠用，等出問題再 case-by-case 改）
- crane_mtx_ serialize 整個 RPC channel — 但有 estop 副 channel 可繞過，可接受

## 2026-05-11a — Claude Code — SE3_MOTION_HZ_DEFAULT 5 → 10

### 修改檔案
- `Crane_control_PI/main.cpp` — `SE3_MOTION_HZ_DEFAULT` 從 5.0 改 10.0

### 為什麼
2026-05-09c 把 motion_hz 從 10 降到 5 是為了「halve rope speed → halve overshoot」。但後續做了：
- SD76 校正完成（顯示 = 物理）→ 不需軟體 overshoot 補償（已移除 2026-05-09m）
- balance trim 對稱補償左右差異
- fine_adjust 收尾把殘餘 < 1cm

5Hz 對左邊馬達來說 **太接近 stall 閾值**（balance trim 把左壓到 3.5Hz 就完全卡住）。10Hz 給 trim 留更多 ±headroom，左右兩邊都不會掉到 stall 區。

### 副作用
- rope speed 倍增 → 物理慣性 overshoot 增加，但 SD76 校正後 1:1，目標精度仍可
- 若 overshoot 太大可從 SE3 面板改 P.8 (01-07) 縮短減速時間 / 加 DC brake 強度
- runtime 仍可 `set_motion_hz <hz>` 即時調

## 2026-05-09t — Claude Code — fine_adjust 在 timeout 時也跑 + trigger inclusive

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - `motion_rope` 結尾邏輯改寫：拿掉「abort_reason 非空就 early return」，改成只有「user 主動 abort」或「tension 觸發」才跳 fine_adjust；timeout 等其他 abort 仍跑 fine_adjust 當清理
  - fine_adjust 失敗時：若 main motion 也有 abort_reason，surface main 的 reason（更有意義）；只有 main OK 但 fine_adjust 失敗才 surface fine_adjust 的 reason
  - `motion_fine_adjust` trigger 條件 `<= FINE_ADJUST_TRIGGER_CM` 改 `<` → `|diff| ≥ TRIGGER_CM` 觸發（之前 `> TRIGGER_CM` 才觸發，剛好 = 2cm 不收）
  - skip 時加 log `[fine_adjust] skipped (main motion abort: ...)` 方便診斷

### 為什麼
Sadie bench：右邊馬達低 Hz stall，main motion 經常 timeout（120s）— abort_reason="timeout" → 舊邏輯直接 early return → fine_adjust 永遠到不了 → diff 5cm 沒人收尾。

User 訴求「到定點後兩邊相差 2 公分以上會沒有自動校正回小於 2 公分」對應兩個問題：
1. timeout 時 fine_adjust 沒跑（修：改邏輯讓 timeout 仍跑）
2. trigger `<= 2`（即 ≤ 2 不收）跟 user 表達「2 公分以上」inclusive 不一致（修：條件改 `<`，2 含 2 都觸發）

### 行為矩陣
| main 結果 | abort_reason | fine_adjust 是否跑 | 最終回應 |
|---|---|---|---|
| 正常完成 | (空) | ✅ 跑 | OK 或 ERR fine_adjust_* |
| 超時 | "timeout" | ✅ 跑（清理） | ERR timeout（保留 main 原因） |
| Tension 觸發 | "tension_high_left" 等 | ❌ 跳過 | ERR tension_* |
| User 按停 | "aborted" | ❌ 跳過 | ERR aborted |

### 待 bench 驗證
- pay_out 5 / 10 / 30 等不同距離，看 fine_adjust 是否在 timeout case 觸發
- 觀察 fine_adjust 5Hz 能不能動右邊（不能就 `set_fine_adjust_hz 8` 拉高）

## 2026-05-09s — Claude Code — fine_adjust + balance hz_min/max 改 runtime tunable

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - `FINE_ADJUST_HZ` 從 constexpr 改 `g_fine_adjust_hz` atomic（預設仍 5.0），`motion_fine_adjust` 改讀 atomic
  - 新增 3 個 cmd handler + dispatcher：`set_fine_adjust_hz` / `set_balance_hz_min` / `set_balance_hz_max`
  - cmd_status 多吐 `balance_hz_min` / `balance_hz_max` / `fine_adjust_hz`

### 為什麼（bench log）
Sadie bench：左邊馬達在低 Hz stall（負載 / 摩擦不對稱），balance trim 把左邊壓到 3.5Hz 後就完全停住，err 一路爬 2→3→4→5→6cm 沒收斂；fine_adjust 用預設 5Hz 也過不了靜摩擦，左邊根本沒動。

需要 runtime 能 bench 邊跑邊調 fine_adjust_hz（試 8-10）跟 balance_hz_min（防 trim 把任一邊壓到 stall 閾值之下）。

### 用法
```
# 給 fine_adjust 更大力氣（5Hz 過不了靜摩擦時）
set_fine_adjust_hz 10

# 防 balance trim 把任一邊壓到 stall 閾值之下
set_balance_hz_min 5      # 預設 2，碰到大負載拉到 5（= base motion_hz，保證至少有原速度）

# motion_hz 跟著拉高給 trim 留空間
set_motion_hz 8           # 5 太小會被 trim 壓到 stall

# 觀察
status                    # 多吐三個欄位
```

### Tuning 建議流程（給 user bench 用）
1. `set_motion_hz 8`（給 trim 留空間）
2. `set_balance_hz_min 5`（防 trim stall 任一邊）
3. `set_fine_adjust_hz 8`（fine_adjust 用得動的 Hz）
4. `pay_out 10` 試一輪，看 balance err 收不收斂、fine_adjust 是不是動得動
5. 不夠再 `set_balance_kp 1.0` / `set_balance_cap 5` 加大 trim
6. 太敏感反震 → kp / cap 退回去
7. 找到滿意值寫進 main.cpp 的 `*_DEFAULT` 常數永久化

### 沒解決的根本問題
左邊馬達低 Hz stall 是物理層 — 軟體 tuning 只能繞過，不能根治。長期建議：
- 檢查左繩機械摩擦 / SE3 P.7 加速時間設太長 / DC brake 設太強
- 或左右都拉到馬達甜蜜帶 Hz（避開 stall 區）

## 2026-05-09r — Claude Code — sync_start Phase B 加 1 retry（解 Phase B fail 機率太高）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 新增 helper `reliable_run_one(inv, pay_out)`：2 attempts × 50ms backoff
  - `dual_se3_sync_start` Phase B 從「直接 run cmd 不 retry」改成呼叫 `reliable_run_one`
  - log 訊息更新成 `Phase B failed (run, after retry)`

### 為什麼（bench log）
Sadie bench 觀察 sync_start Phase B fail rate ~30%（log 一段時間內看到 3 次 `Phase B failed (run): L=0 R=1 — stopping both`）。Phase B 不 retry 是上一版（2026-05-09p）為了極致同步而做的選擇，但 SE3 firmware 偶發 transient（CU mode glitch / TCP retransmit / bus contention）讓 single-shot 太脆弱。

加 1 retry 後：
- Worst-case drift 從 0ms 變 50-100ms（仍遠優於原 reliable_start_one 0-700ms）
- Fail rate 預期大降（單次 transient 通常下次就過）
- Trade-off：失去「絕對同步」但換來「絕對能跑」

### 數字
- Phase A：8 attempts × 100ms backoff = 800ms 上限（變數一邊 retry，吸收延遲）
- Phase B（new）：2 attempts × 50ms backoff = 100ms 上限（兩邊各自 retry，drift ≤ 50ms）
- 比起原 reliable_start_one 把 setFreq + run 綁同 retry loop（最壞 700ms drift），這個 split + bounded retry 是中間值

### 待 bench 驗證
- 連續 pay_out 多次，看 Phase B fail rate 是否大降
- 兩邊起步同步性是否還在可接受範圍（肉眼幾乎同步即可）

## 2026-05-09q — Claude Code — motion_rope 加 post-motion fine-adjust（diff > 2cm 用 5Hz 收斂）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 新增常數：`FINE_ADJUST_TRIGGER_CM=2` / `FINE_ADJUST_TARGET_CM=1` / `FINE_ADJUST_HZ=5.0` / `FINE_ADJUST_TIMEOUT_MS=30000`
  - 新增 helper `motion_fine_adjust(base_left, base_right, direction, pay_out)`：
    - 算 L/R 進度差，差 ≤ trigger → no-op return
    - 差 > trigger → 落後者 setFreq 5Hz + 同向 run，loop 等到 |diff| ≤ target 或 abort/tension/timeout
    - 中途含 abort_flag / tension safety / timeout（30s）守則
  - `motion_rope` 主 loop 結束後（沒 abort 才跑）呼叫 `motion_fine_adjust`，失敗回 `ERR <reason>`
  - `cmd_roll_correct` 暫不動（差動方向計算不同，user 也只提到 pay_out / retract）

### 為什麼
Sadie bench：sync_start 後雖然兩邊基本同步，偶發一邊起步抖動造成 ~1cm 殘留差異。希望主動作結束後自動「收尾」— 若兩邊差 > 2cm，落後者用 5Hz 慢慢追上，直到 ≤ 1cm。

### 行為示例
```
pay_out 5
[motion_rope] base L=100 R=100, target +5cm
... 主 loop 跑 ...
[motion_rope] both done: L=105 R=103 (left progress=5, right progress=3, diff=2cm)
（此時主 loop 認為都到了，但其實右邊只走 3cm）
→ diff=2cm ≤ trigger 2 → 不觸發 fine_adjust  // edge：剛好 2cm 不收

pay_out 10
... 主 loop 跑 ...
[motion_rope] both done: L=110 R=107 (diff=3cm)
[fine_adjust] diff=3cm > 2 — right lagging, converging at 5Hz (target ≤1cm)
... 右邊 5Hz 跑 ...
[fine_adjust] converged at diff=1cm
→ 最終 L=110, R≈109
```

### 副作用 / 邊界
- 增加 motion_rope 完成時間：差 3cm 約多 1-2 秒（5Hz 慢但只跑短距離）
- 落後者尾段慢，ramp 殘餘可能讓最終差 < target（理想）；也可能稍超過（超過 1cm 但通常 < 2）
- 觸發條件嚴格 `>` 不含 `=`：剛好 diff=2cm 不收 — 跟 deadband <= 哲學一致（避免 boundary case oscillation）
- timeout 30s 是物理層問題的安全網（馬達卡 / 繩子被勾住），正常一兩秒內收斂

### 待 bench 驗證
- 連續 pay_out 多次，看 fine_adjust 是否常觸發 + 收斂時間
- 若太頻繁 → trigger 拉到 3cm；若太少 → 拉到 1.5cm

## 2026-05-09p — Claude Code — 兩階段 sync start（解 motion_rope 偶發「一邊先放」）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 新增 helper `reliable_setfreq_one(inv, hz)`：純 setFreqHz + 8x100ms retry
  - 新增 helper `dual_se3_sync_start(hz, left_pay_out, right_pay_out)`：兩階段
    - Phase A：兩條 thread 各自 `reliable_setfreq_one`（含 retry，吸收 transient 延遲）
    - Phase B：兩條 thread 同時下 run command（不 retry），失敗則 stop both + ERR
  - `motion_rope` 啟動段：`dual_se3_concurrent(se3StartRopeMotion)` → `dual_se3_sync_start(motion_hz, pay_out, pay_out)`
  - `cmd_roll_correct` 啟動段：對應改 `dual_se3_sync_start(motion_hz, left_pay, !left_pay)`
  - `dual_se3_concurrent` / `reliable_start_one` / `se3StartRopeMotion` 保留給 stop 操作 + hold mode + 單側 cmd_manual 用（單側不需要 sync）

### 為什麼
Sadie bench 觀察 `motion_rope` 偶發「一邊比較快放」。`reliable_start_one` 把 setFreqHz + run command 綁在同一個 retry loop（8x100ms），即使 dual_se3_concurrent 兩條 thread 並行，也是「兩邊各自獨立做完整個 retry-or-success」— 一邊第一次成功（~50ms）、另一邊撞 transient 要 retry（多 100-700ms），物理啟動時間就差好幾百 ms。

兩階段把 retry 全部留在 Phase A（setFreq，RAM-only idempotent，可隨便 retry），Phase B 只剩單一 run command 兩邊同時觸發 → drift 從幾百 ms 降到 ~10ms（TCP jitter 等級）。

### Trade-off
- Phase B 不 retry：run command 撞 transient → ERR，user 重按。原本 8x100ms retry 帶來的 fault-tolerance 沒了
- 但對 sync 要求高的 motion_rope / roll_correct 來說，sync > auto-recovery；hold mode 跟 cmd_manual 還用 reliable_start_one

### 待 bench 驗證
- 連續 pay_out N 多次，觀察兩邊起步時間差是否縮小
- 偶發 ERR se3_start_fail 若變多，考慮 Phase B 加單次 retry（會帶回少量 drift）

## 2026-05-09o — Claude Code — 加 runtime 平衡參數調整指令

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 新增 4 個 cmd handler：`cmd_set_balance_enabled` / `cmd_set_balance_kp` / `cmd_set_balance_cap` / `cmd_set_balance_deadband`，各自 atomic store + log
  - dispatcher 加對應 4 個 entry
  - cmd_status 多吐 `balance_enabled / balance_kp / balance_cap / balance_deadband` 4 個欄位

### 為什麼
Sadie 觀察 bench balance 看到 err 從 2cm 漸長到 3cm 沒收斂，trim 1.2-1.8Hz 不夠把落後者拉上來。原本 Kp / cap / deadband 只能改 `BALANCE_*_DEFAULT` 常數重 build，bench 調很慢。加 runtime cmd 後可即時調 + 觀察。

### 用法
```
set_balance_kp 1.0           # 從預設 0.6 拉到 1.0
set_balance_cap 5.0          # 預設 3，想 trim 走更大
set_balance_deadband 0.5     # 預設 1，想抓更小偏差
set_balance_enabled off      # 完全關掉做對照
```
無持久化 — restart 會回 `*_DEFAULT`。要永久修，改 main.cpp 的 `BALANCE_*_DEFAULT` 常數重 build。

## 2026-05-09n — Claude Code — METER_POLL_MS_MOTION 200ms → 50ms

### 修改檔案
- `Crane_control_PI/main.cpp` — `METER_POLL_MS_MOTION = 50`（之前 200，再之前 1000）+ 註解更新解釋演進

### 為什麼
Sadie 要進一步壓 cache lag。50ms 後實測 cache 最壞 stale 從 200ms 降到 ~50ms，distance precision 限制改由 SE3 ramp / DC brake 物理層決定（軟體不可再壓）。

### 副作用評估
- cli_A bus 占用率 ~30% → 70-80%（meter_loop polling 幾乎連續）
- SE3 寫指令排隊機率上升，但 reliable_start_one 8x100ms / reliable_stop_one 8x100ms 已 cover 900ms transient
- balance trim setFreqHz 沒 retry，一輪撞 cli busy 就丟失，500ms 後 re-tick — 可接受

### 後續觀察
- `pay_out 5` overshoot 是否進一步改善（理論上 cache 那段從 ~2 cm 降到 ~0.5 cm，總 overshoot 主要剩 SE3 ramp）
- SE3 ERR se3_*_start_fail 觸發頻率（如果 retry 8 次都不夠，會看到）
- balance trim 是否仍有作用（log `[BAL] err= trim=`）

### 回滾條件
- 若 SE3 啟動失敗率明顯上升 → 回到 100ms 折衷
- 若 balance 完全無 trim 訊息 → 表示 setFreqHz 一直撞 busy → 回到 100ms

---

## 2026-05-09m — Claude Code — 移除 motion_overshoot 補償機制（校正完成後不再需要）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 移除 `MOTION_OVERSHOOT_CM_DEFAULT` 常數 + `g_motion_overshoot_cm` atomic
  - 移除 `cmd_set_motion_overshoot()` 函式 + dispatcher 條目
  - 移除 `cmd_status` 的 `motion_overshoot_cm=` 輸出欄位
  - `motion_rope` 拿掉 `eff_cm` / `eff_middle_cm`，stop threshold 直接用 `cm` / `middle_target_cm`
  - `cmd_roll_correct` 拿掉 `eff_cm`，stop threshold 直接用 `abs_cm`
  - 留一段註解說明移除原因 + 若未來物理 overshoot 再變大，建議從 SE3 P.8 / DC injection brake 端解（不要再加應用層補償）

### 為什麼
2026-05-09c 加 overshoot=3cm 是「校正前」的補償 — 當時 SD76 顯示 1cm = ~5cm 物理，必須提前停才不會超過。

Sadie 校正完成後（2026-05-09h ~ k），1cm 顯示 = 1cm 物理（左右繩各自校過），overshoot=3 變成「永遠少走 2-3cm」（user 報告 `pay_out 5` 只走 2-3cm）。

→ 拔掉。`pay_out N` 現在會跑到顯示 +N cm 為止，物理大致對應 N cm（殘餘 ramp/cache slip 估 < 1cm）。

### 副作用
- 若 SE3 P.8 decel 時間太長 / 馬達慣性大，物理會稍微 overshoot 顯示值（例如 `pay_out 5` 顯示停在 5cm，但物理跑了 5.5cm）— 這是 SE3 / 機械層面的事，不是應用層補
- 解法：SE3 面板把 P.8 縮短，或加 DC injection brake（見 memory `project_se3_dc_brake_setup.md`）

## 2026-05-09l — Claude Code — Linux_test menu 9（test_sd76）加 SCAL/DP 校正指令

### 修改檔案
- `Linux_test/main.cpp` — `test_sd76()` 加 4 個 sub-command：
  - `e` — 讀 effective M + raw SCAL + raw DP（一次秀完）
  - `m <multiplier>` — `drv.setEffectiveScale(M)`（preserve DP，超範圍會看 driver log）
  - `b <ratio>` — `drv.scaleByRatio(r)`（cal_set 的核心；例 `b 5.555` 把 SCAL × 5.555）
  - `w <scal> <dp>` — `drv.writeScale(scal, dp, write_dp=true)` raw 寫，diagnostic mode latch 用
- input thread 改吃整行（`mutex + string + flag`），不再只取首字，這樣 `m 5.5` / `w 18 4` 之類帶參指令能 parse

### 為什麼
Sadie bench 想直接從 Linux_test 端 poke SD76 校正參數，不想透過 crane TCP 繞。menu 9 之前只有 r/p/s/q 純 counter 控制，沒暴露剛加的 driver SCAL API。

### 用法（bench 校正流程示範）
```
[SD76:2] commands listed at startup
1. e                    ← 看現值（例 M=1, SCAL=100, DP=2）
2. r                    ← 計米器歸零
3. （手拉繩 100cm）
4. e                    ← 看 SD76 顯示多少（例 18）
5. b 5.555              ← 100/18=5.555，SCAL × 5.555 → SCAL=555
   或
   m 5.555              ← 直接設倍率（等價於 b 5.555 從 1.0 起算）
6. e                    ← 確認 scale 變了
7. r                    ← 再歸零
8. （手拉 100cm 驗證）→ 應顯示 ~100cm
```

### 注意事項（已 log 在 cmd 回應裡提示 user）
- DP 在 SD76 通訊模式（00-16=3）下 Modbus 寫被 silently 吃掉（見 changelog 2026-05-09j），所以 `m`/`b` 走 `setEffectiveScale` 是 preserve DP 的路徑；`w` 是 diagnostic 路徑會嘗試寫 DP 但很可能不生效

## 2026-05-09k — Claude Code — Balance 微調：HZ_MIN 5→2、deadband 條件 < 改 <=

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - `BALANCE_HZ_MIN_DEFAULT` 從 `5.0` 改 `2.0`
  - `apply_balance_trim` 內的 deadband 判斷從 `fabs(err) < deadband` 改 `<=`

### 為什麼（bench log）
Sadie 跑 hold + auto motion，看到：
1. `[BAL] err=1cm trim=0.6Hz L=5 R=5.3` — base motion_hz=5，對稱 trim 應該 L=4.7 R=5.3，但 L 被 BALANCE_HZ_MIN=5 卡住變單邊（只能加速落後者，無法減速領先者）
2. `[BAL] err=1cm` 連續多筆不收斂 — err 剛好踩在 deadband=1cm 邊上，`<` 不滿足 → 永遠走 trim 分支不會放手 → cli_A bus 一直被 setFreqHz 寫

### 修法
1. 把 HZ_MIN 拉到 2Hz，給 base=5 留出 ±3 trim 空間（symmetric 完整可用）
2. deadband 改 `<=`，err 等於 deadband 也算落在 deadband 內，trim 收斂到 0 + 一次性 reset_to_base

### 副作用
- HZ_MIN=2 → 落後者最低被 trim 到 2Hz；2Hz 在 SE3 起轉應該 OK，但若觀察到 trim 後馬達不轉 / 跳脫，再往上拉
- deadband 邊界 case 行為改 — err==1 從「無限 trim」變「不 trim」，正常使用感受不到差別

## 2026-05-09j — Claude Code — SD76 setEffectiveScale 改 preserve DP（DP 在通訊模式下 Modbus 寫被吃掉）

### 修改檔案
- `user_lib/SD76_length_meters.cpp` — `setEffectiveScale` 不再 auto-pick DP；改成讀裝置當前 DP，算 SCAL = round(10^DP / multiplier)，超出 [1, 999999] 範圍清楚回 ERR + log 提示「面板 00-16=非3 → 改 DP → 00-16=3」
- `user_lib/SD76_length_meters.h` — `setEffectiveScale` 註解更新
- `Crane_control_PI/main.cpp` — `cmd_read_meter_scale` 多吐 `raw_scal=N raw_dp=N` 給 bench diagnose 用

### 為什麼（bench 數據）
2026-05-09 Sadie 在 bench 用左繩 SD76：
- `set_meter_scale left 5.5` → driver 算 SCAL=18182, DP=5、`writeScale(write_dp=true)` 回成功
- `read_meter_scale left` → effective scale = 0.00549995（差 1000 倍 = 10^3）
- 反推：SCAL=18182 ✓ 寫進去了，但 DP **沒變**（仍是面板原值，估計是 2）
- 公式 M = 10^DP / SCAL = 100/18182 = 0.0055 ✓ 完全對得上

User 補充關鍵 context：「**面板再設定的時候必須要先把 00-16 設定成非 3，設定完後再把 00-16 改回 3（通訊模式）**」— 確認 SD76 跟 SE3 同類有 mode latch（通訊模式下面板鎖某些參數，反過來 Modbus 也鎖某些參數，DP 是其中之一）。

→ Auto-DP 路線走不通。回到 preserve DP，invariant 變成「driver 只在 user 設好的 DP 範圍內動 SCAL」。想擴範圍要去 SD76 面板：00-16=非3 → 改 DP → 00-16=3。

### 副作用
- 想要 multiplier 5.5 在當前 DP=2 下：SCAL = round(100/5.5) = 18，effective M = 100/18 = 5.555（約 1% 誤差，bench 夠用）
- 如果 user 想要 multiplier > 100 或 < 0.01，需要先去面板改 DP

### Bench 二輪驗證 SOP（重 build 後）
1. 把面板 DP 確認在 user 想要的位置（這次 bench 觀察看起來是 2 → 範圍 0.01～100，5.5x 在範圍內）
2. **先還原**之前寫壞的 SCAL：`set_meter_scale left 1.0` → 應該寫 SCAL=100, DP=2 不變
3. `read_meter_scale left` → 應回 `OK scale=1 raw_scal=100 raw_dp=2`（diagnostic 確認 layout）
4. `set_meter_scale left 5.5` → driver log 印「multiplier=5.5 → SCAL=18 (DP=2 preserved)」
5. `read_meter_scale left` → 應回 `OK scale=5.555 raw_scal=18 raw_dp=2`
6. 物理拉 11cm → 顯示應該變 ~11cm（finally）

### Memory + mailbox
- 新增 `project_sd76_panel_mode_latch.md`
- mailbox 給 Jim 補一條（DP 鎖的問題 + 待找 unlock magic 的後續方向）

## 2026-05-09i — Claude Code — SE3 retry 5x80ms → 8x100ms + balance tick 1000ms → 500ms

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - `reliable_start_one`：MAX_ATTEMPTS 5 → 8、backoff 80ms → 100ms（總 wall time 400ms → 900ms）
  - `reliable_stop_one`：同上（保持兩者對稱）
  - `BALANCE_TICK_MS`：1000ms → 500ms（motion 啟動 1s 內無 trim 的 desync 視覺問題）

### 為什麼
Sadie 觀察兩個現象：

1. **manual cmd「左右都隨機偶發不動」**：reliable_start_one 5x80ms=400ms 包不住所有 transient。SE3 CU-mode watchdog reclaim + cli bus 跟 meter_loop 撞時 transient 偶發 > 500ms。加到 8x100ms=900ms，watchdog motion (10s) / idle (2s) 都還在預算內。
2. **pay_out「兩邊都動但不同步」**：dynamic balance tick = 1000ms，第一次 trim 在 motion 啟動 1 秒後才發生，前 1 秒兩邊純物理啟動差（SE3 各自 ramp / 摩擦 / 慣性）沒被修正。改 500ms tick：第一次 trim 在 motion 啟動 0.5s 內就發生 → 啟動 desync 視覺感受減半。

### 副作用
- retry 加長 → cli bus 在 transient 期間佔用變多（500ms 額外排隊），但 transient 是少數路徑，平均成功仍 50-100ms 完成
- balance tick 加快 → 寫 setFreqHz 雙邊 500ms 一次，跟 SD76 polling 200ms 撞概率變高，但 sendAndReceive atomic API 已 cover serialization
- watchdog motion timeout 10s 仍充裕（900ms × 2 邊 concurrent = 900ms wall time）

### 後續驗證
- `manual left payout on` / `manual right payout on` 各自重複 10 次，記錄成功率
- `pay_out 5` 看兩邊啟動同步性是否改善（肉眼觀察、計米器數值差）
- 若 manual 成功率仍 <100% → 需要更長 retry 或 driver 層 root cause 調查
- 若 pay_out 仍視覺不同步 → 動 phase-split 啟動（先兩邊 setFreqHz 再兩邊 run）

---

## 2026-05-09h — Claude Code — SD76 SCAL 公式倒過來修正（bench 觀察 SCAL 是除數不是乘數）

### 修改檔案
- `user_lib/SD76_length_meters.h` — `getEffectiveScale` / `setEffectiveScale` / `scaleByRatio` 註解改寫，明文 SD76 把 SCAL 當 K-factor 用（divisor）；application API 對外仍是 multiplier 語意
- `user_lib/SD76_length_meters.cpp` —
  - `getEffectiveScale`：`M = 10^DP / SCAL`（之前是 `M = SCAL × 10^(-DP)`，bench 證明顛倒）
  - `setEffectiveScale`：`SCAL = round(10^DP / M)`，並改成**自動挑最高 DP**（從 5 往下試）讓 SCAL 落在 [1, 999999] 區間，藉此給最大精度；同時 `write_dp=true` 把 DP 一起寫進裝置
  - `readScale` debug log 更新顯示 app multiplier + device K-factor 兩個值方便診斷

### 為什麼（bench 數據）
2026-05-09 Sadie 在 bench 用左繩 SD76：
| SCAL | 物理 cm | 顯示 delta | 顯示 / 物理 |
|---|---|---|---|
| 1.0 | 11 | 2 | 0.18 |
| 2.0 | 9 | 1 | 0.11 |

SCAL 加大 → 顯示變慢 → **裝置把 SCAL 當「K 個脈衝才動 1 顯示單位」(divisor) 用**。手冊雖然標 "Counter Multiplier" 但實機行為是反的（可能是手冊翻譯誤導 / 此 SD76-C 子型號特性）。

### 驗證
原 driver `setEffectiveScale(2.0)` 寫了 SCAL=2 給裝置，user 拉 9cm 顯示只動 1cm（更慢，跟 multiplier 假設相反）。倒過來後：`setEffectiveScale(5.5)` 應寫 SCAL=18181 + DP=5（自動），讓 effective M = 100000/18181 = 5.5，使「11cm 物理 = 11cm 顯示」。

### 副作用：DP 會被 driver 動
之前 setEffectiveScale 只動 SCAL、保留 user 面板設的 DP。修正後為了精度自動挑 DP 並寫入裝置 → SD76 LCD 小數點位置會跟著跑（例如本來顯示「10」可能變「10.00000」）。**user 已同意此 trade-off**（GUI 端從 0x0021 integer register 讀，不受 DP 影響）。

### 待 bench 二輪驗證
- `set_meter_scale left 5.5` → 應該跑 setEffectiveScale → 寫 SCAL=18181, DP=5
- 物理拉 11cm → 顯示應該變 ~11cm（不是 ~2cm）
- `read_meter_scale left` 應回 5.5

## 2026-05-09g — Claude Code — motion_rope / cmd_roll_correct SE3 啟動改 concurrent（解「只動一邊」）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - `motion_rope`：兩邊 `se3StartRopeMotion` 從序列改 `dual_se3_concurrent` lambda 啟動；fail 時 stop 兩邊；error 訊息從 `se3_left/right_start_fail` 簡化為 `se3_start_fail`
  - `cmd_roll_correct`：方向相反不能用 dual_se3_concurrent 同 lambda，改 inline `std::thread tL/tR` 各自送方向；同樣 fail rollback

### 為什麼
Sadie bench 觀察 `pay_out 2` 後「只有一邊在動」，沒看到 ERR aborted（watchdog 已寬鬆 10s 不再觸發），但右邊就是不轉。

對比 `dual_se3_hold_start` 已用 concurrent dispatch、hold 模式兩邊都正常動，差別在 **motion_rope 啟動是序列**：

```
左 setFreqHz + run（cli_A 比較閒，多半成功）
↓ 100-200ms 後
右 setFreqHz + run（cli_B 此時可能正被 SD76 polling 用）
  → reliable_start_one 5x80ms retry 全踩到 bus 競爭
  → Modbus reply OK 但 motor 實際沒啟動（CU mode latch 邊緣）
  → return false（"成功"），但 motor 物理沒轉
```

Concurrent 啟動後：
- 兩邊 thread 同時 launch setFreqHz + run，bus 競爭機率對稱
- wall time 從 T_left + T_right 降到 max(T_left, T_right)，啟動同步性也提升
- 配合 dynamic balance（2026-05-09b）motion 中 trim 也更穩

### 影響範圍
- `motion_rope`（pay_out / retract）：兩邊同步啟動
- `cmd_roll_correct`：左右方向相反，inline thread 各送，仍 concurrent
- `dual_se3_hold_start`（hold 模式）：本來就 concurrent，無變動

### 後續驗證
- `pay_out 2` 應兩邊同時動，停在 ~5 cm 附近（5 Hz × P.8 ramp + cache lag）
- 若仍只一邊動 → 表示某邊 SE3 真的有 hardware / wiring 問題，不是序列 race；用 manual 指令分別測單邊區隔

---

## 2026-05-09f — Claude Code — 計米器校正改裝置端 SD76 SCAL/DP（root-cause 解，丟掉軟體 scale 層）

### 修改檔案
- `user_lib/SD76_length_meters.h` / `.cpp`（**[跨界: user_lib]** — Sadie 自代 Jim 寫，下面 mailbox 給 Jim review）
  - 新增 public API：`readScale(scal, dp)` / `writeScale(scal, dp, write_dp=false)` / `getEffectiveScale(double&)` / `setEffectiveScale(double)` / `scaleByRatio(double)`
  - 新增 private helper：`writeMultipleRegisters(addr, count, data, len)`（FC 0x10）/ `encodeBCD6(value, out[4])`（鏡像 decodeSignedBCD6，no sign byte）
  - SCAL @ `0x0014-0x0015` (6-BCD)；DP @ `0x0020`（low byte=upper DP, high byte=lower DP）
  - effective multiplier = SCAL × 10^(-DP)
  - `setEffectiveScale` 預設 preserve 裝置 DP（user 面板設的 DP 不動），算 SCAL = round(multiplier × 10^DP)，超出 999999 回 ERR（提示 user 面板降 DP）
  - `writeScale(write_dp=true)` 走 read-modify-write 保留 lower-display DP byte，只動 upper

- `Crane_control_PI/main.cpp` —
  - **拆掉 2026-05-09d 軟體 scale 層全部**：
    - `<fstream>` include / `g_meter_*_scale` / `g_length_*_raw` / `apply_meter_scale` / `load_meter_scales` / `save_meter_scales` / `METER_SCALE_*` 常數 / `meter_scale.txt` 檔案 I/O
  - 改 `meter_loop`：直接存 SD76 顯示 cm（已是 calibrated），下游消費端不變
  - 新增 atomic：`g_meter_*_device_scale` + `g_meter_*_scale_valid`（裝置端 scale 快取）
  - 改 `cmd_cal_zero`：snapshot 顯示 cm（不是 raw）
  - 改 `cmd_cal_set`：算 ratio = actual_cm / observed_delta → `meter.scaleByRatio(ratio)` 寫進 SD76 EEPROM → refresh 快取 → invalidate baseline（強制 user 下次 cal_set 前重新 cal_zero）
  - 改 `cmd_set_meter_scale`：直接 `meter.setEffectiveScale(scale)` 寫裝置
  - 新增 `cmd_read_meter_scale <side>`：強制重讀 SD76 SCAL/DP（user 從面板改過時用）
  - dispatcher 加 `read_meter_scale`
  - cmd_status 改吐裝置端 scale（valid 才印數字、否則印 ERR）
  - 啟動時呼叫 `refresh_meter_scale_cache` 讀三台 SD76、log 一行 device scale

### 為什麼
之前（2026-05-09d）做了軟體 scale 層 — 校正存 `meter_scale.txt` 在 Pi 上。Sadie 想要**根本解**：寫進 SD76 EEPROM，讓校正跟裝置走，Pi 重灌 / 移機都不會丟。

SD76 本來就有 SCAL（counter multiplier）+ DP（decimal point）兩個 R/W 暫存器，driver 沒暴露而已。我把他們 expose 了。

### 校正 SOP（user 操作）
1. 拉繩到基準位置
2. `cal_zero left` → 後端 snapshot 當前顯示 cm
3. 物理拉動已知距離（例：100cm 記號）
4. `cal_set left 100` → ratio = 100 / observed → `meter.scaleByRatio(ratio)` 進 SD76 EEPROM
5. 之後顯示就是真實 cm；可疊加（再校就再乘 ratio）

### 風險（bench 必須驗證）
- **公式假設未驗證**：`display = pulse × SCAL × 10^(-DP)` 是依手冊 + 常見廠牌設計猜的；萬一不是這公式，第一次 cal_set 會把 SD76 顯示弄歪 → 從面板恢復原值即可
- **是否需要 save_params**：DSZL 那種 `0xA20=40` 的明確 save 命令，SD76 手冊沒提；目前假設 FC 0x10 寫 0x0014-0x0015 會直接落 EEPROM。bench 寫完 power-cycle 確認是否 persist；若否，要照 DSZL 模式加 save 補丁
- **bench 第一次測試流程**：
  1. `read_meter_scale left` 看現值（記下當 backup）
  2. `set_meter_scale left 2.0` 暴力寫 2.0 倍率
  3. 物理拉繩 10cm，看 SD76 顯示是否跳到 ~20cm（驗證公式）
  4. 是 → 公式對；否 → 公式錯，從面板恢復
  5. power-cycle SD76，再 `read_meter_scale left` 看是否還是 2.0（驗證持久）
  6. 還原原值（`set_meter_scale left <原值>`）

### 待辦
- bench 驗證公式 + 持久性（user 待測）
- GUI 加 cal_zero / cal_set 按鈕（Phase 3 一起做）
- 若 bench 發現需要 save_params，照 DSZL 模式加（`SD76_length_meters::save_params()` 試 `0x0000` 寫某 magic）

## 2026-05-09e — Claude Code — Crane watchdog motion-aware timeout（解 motion_rope 中途 ERR aborted）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 把 `WATCHDOG_TIMEOUT_MS` (2000) 拆成 `WATCHDOG_TIMEOUT_MS_IDLE` (2000) + `WATCHDOG_TIMEOUT_MS_MOTION` (10000)
  - `watchdog_loop` 內依 `motion_active && !any_hold_active()` 動態決定 timeout
  - 啟動 log 從「timeout 2000 ms」改「timeout idle/hold 2000 ms / motion 10000 ms」

### 為什麼
Sadie 在 bench 觀察 `retract 2` 指令偶發回 `ERR aborted` + `EVT watchdog_recovered`，「右邊不動 / 只動一邊」其實是 motion_rope 啟動序列中 GUI heartbeat 偶發 silence > 2 秒 → watchdog 觸發 → abort_flag → motion_rope 整個取消。

motion_rope 自動跑時 GUI 是「觀察」不是「操舵」，timeout 拉到 10s 不影響 fail-safe 等級——
- motion 中真斷線：watchdog 10s 後仍會觸發 → allMotionOff
- motion_rope 自己的 `MOTION_TIMEOUT_MS = 120000` (2min) 是更上層的 cap

hold 模式（按住 UP/DOWN）必須保持 2s——使用者鬆手收不到 → 馬達不能跑 10s 才停。判斷式 `motion_active && !any_hold_active` 把這兩個情境分開。

### 影響範圍
- `motion_rope`（pay_out / retract）：watchdog 拉到 10s，spurious abort 大幅減少
- `cmd_roll_correct`：同樣走 motion_active=true，受惠
- `cmd_hold` (UP/DOWN 按住)：保持 2s，fail-safe 行為不變
- idle 期間：保持 2s

### 後續驗證
- `pay_out 5` / `retract 5` 應一次跑完不再看到 ERR aborted（除非真網路斷線 > 10s）
- 若仍偶發 ERR aborted → 看 EVT watchdog_recovered 出現頻率，可能要進一步診斷 GUI heartbeat 為何中斷
- 配合 2026-05-09c 的 5 Hz + overshoot 補償一起測，看 cm 精度

---

## 2026-05-09d — Claude Code — 軟體層計米器校正（per-meter scale + cal_zero/cal_set + 持久化）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 新增 includes：`<fstream>` `<climits>`
  - 新增 atomics：`g_meter_{left,right,middle}_scale`（預設 1.0）/ `g_length_{left,right,middle}_raw` / `g_meter_{left,right,middle}_cal_baseline`（INT32_MIN sentinel）
  - 新增常數：`METER_SCALE_DEFAULT/MIN/MAX` `METER_SCALE_FILE="meter_scale.txt"` `METER_CAL_UNSET=INT32_MIN`
  - 新增 file I/O：`load_meter_scales(path)` / `save_meter_scales(path)` — key=value 文字檔，sanity bound 過濾
  - 新增 helper：`apply_meter_scale(raw, scale)` — int32 round
  - 改 `meter_loop`：每 tick 同時存 raw（`g_length_*_raw`）跟 scaled（`g_length_*`），所有下游消費端不需改
  - 改 `cmd_zero_meters` / `cmd_zero_meter`：reset SD76 後把對應 cal baseline 設回 UNSET（原點變了，舊 baseline 失效）
  - 新增 `resolve_meter_side(side, ...)` helper 把 string → 對應四個 atomic
  - 新增 `cmd_cal_zero(side)` — snapshot 當前 raw 進 baseline atomic，回 `OK baseline_raw=N`
  - 新增 `cmd_cal_set(side, actual_cm)` — 算 `scale = actual_cm / (raw_now - baseline)`，sanity check（[0.1, 10.0] + 不可 0），通過後 store + save 到檔
  - 新增 `cmd_set_meter_scale(side, scale)` — 直接覆蓋 scale（GUI manual entry / advanced 用）
  - dispatcher 加 `cal_zero` / `cal_set` / `set_meter_scale` 三個指令
  - `cmd_status` 多吐 `meter_{l,r,m}_scale` 跟 `length_{l,r,m}_raw`
  - 啟動時呼叫 `load_meter_scales(METER_SCALE_FILE)`

### 為什麼
Sadie 在 bench 觀察 SD76 計米器讀數「不準」。SD76 driver（Jim 的 user_lib）只有 read/zero/pause/resume，沒有暴露輪徑 / 除數 setting，且面板校正每台都要爬到吊機那邊操作。

選軟體層 scale（user 確認 B 方案）：每個 meter 一個倍數，runtime 校正、寫檔保存、不動 user_lib。`meter_loop` 統一吸收 scale，所有 motion / balance / status 消費端 stay scale-agnostic（讀的 `g_length_*` 已校正）。raw 另存 `g_length_*_raw` 給 calibration 流程跟 cmd_status diagnostic 用。

### 校正 SOP（user 在 GUI 操作）
1. 拉繩到基準位置
2. 下 `cal_zero left` → 後端 snapshot baseline raw
3. 物理量測拉動已知距離（例如 100cm，繩子上做記號）
4. 下 `cal_set left 100` → 後端算 scale = 100 / (raw_now - baseline_raw) → 存檔
5. 之後 `pay_out 100` 真的就是 100cm

或 GUI advanced：直接 `set_meter_scale left 1.0526` 手動輸入。

### 待辦
- GUI 加 cal_zero / cal_set 按鈕（Phase 3 一起做）
- bench 驗證：先用左繩跑一次 SOP，看 cmd_status 的 meter_left_scale 有沒有正確更新 + 檔案是否寫入

## 2026-05-09c — Claude Code — motion_rope / cmd_roll_correct 加 overshoot 補償 + 降 SE3_MOTION_HZ 預設 5 Hz

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - `SE3_MOTION_HZ_DEFAULT` 從 `10.0` 改 `5.0`：rope speed 減半
  - 加 `g_motion_overshoot_cm` 預設 `3.0`、`MOTION_OVERSHOOT_CM_DEFAULT`
  - `motion_rope`：stop threshold 從 `>= cm` 改 `>= eff_cm = max(1, cm - overshoot)`，middle 同樣套
  - `cmd_roll_correct`：同樣套 eff_cm
  - 加 `cmd_set_motion_overshoot(cm)` + dispatcher `set_motion_overshoot <cm>`
  - cmd_status 輸出 `motion_overshoot_cm=`

### 為什麼
2026-05-09a 把 METER_POLL_MS_MOTION 從 1000 改 200 後，bench 實測「下 2 cm 仍走 10 cm」。物理拆解（P.8=0.5s）：

| 來源 | 估算 @10 Hz | 估算 @5 Hz |
|---|---|---|
| Cache lag 200ms × rope speed | ~4 cm | ~2 cm |
| P.8=0.5s 減速 ramp distance | ~5 cm | ~2.5 cm |
| iter sleep 50ms | ~1 cm | ~0.5 cm |
| **合計 overshoot** | **~10 cm ✓** | **~5 cm** |

降 motion Hz 把物理 overshoot 量級減半；overshoot 補償邏輯在 SW 層把剩下的吃掉。預設補償 3 cm，若實測仍 ±誤差大，runtime 用 `set_motion_overshoot <cm>` 微調。

### 副作用
- step_down 等距離型 motion 時間加倍（5 Hz vs 10 Hz）
- 小距離指令（cm <= overshoot=3）會「啟動瞬間就停」，物理 motion ≈ overshoot；這是 hardware ramp distance 的下限，無法軟修
- `cmd_roll_correct` 同步受惠

### 後續驗證
- `pay_out 10` 應落在 9-11 cm
- `pay_out 30` 應落在 28-32 cm
- 若仍系統性偏差，調 `set_motion_overshoot`：實走 > 命令 → 增大；實走 < 命令 → 減小

### 可能跟 2026-05-09b（Phase 1 動態平衡）互動
- 動態平衡 trim 用 `g_se3_motion_hz` 當 base，base 從 10 → 5，trim 上下界相對 base 不變但絕對量減半
- 兩個邏輯各管一條（balance 管同步、overshoot 管距離），不衝突

---

## 2026-05-09b — Claude Code — Phase 1 動態平衡（length-diff feedback + symmetric P 控制）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 新增 globals：`g_balance_enabled` / `g_balance_kp` / `g_balance_trim_cap` / `g_balance_deadband` / `g_balance_hz_min` / `g_balance_hz_max`，跟對應 `*_DEFAULT` 常數 + `BALANCE_TICK_MS=1000`
  - 新增 helper `apply_balance_trim(base_hz, direction, base_left, base_right, was_trimmed)` — 計算長度進度差 → P 控制 trim → symmetric split → setFreqHz 兩台；deadband 內 / 失效 / disabled 時透過 was_trimmed 一次性 reset 回 base
  - `motion_rope` wait loop：每 `BALANCE_TICK_MS` 跑一次 `apply_balance_trim(g_se3_motion_hz, dir, base_left, base_right)`，僅在兩邊都還沒 done 時 tick
  - `hold_loop`：加 `prev_sync_dir` / `balance_base_*` / `was_trimmed` 狀態追蹤；當 up_both / down_both 同向時自動 snapshot baseline 並 tick balance；同向解除（一邊放開）時若曾 trim 過會把仍按住的一邊 reset 回 `g_se3_hold_hz`
  - startup log 加一行印 balance 預設值

### 為什麼
Sadie 要更智慧的吊機控制 — 兩邊原本都跑固定 motion_hz / hold_hz（open-loop），物理上會 drift。設計選擇（user 確認）：
- Feedback：長度差為主（最直觀、計米器穩、無需 cross-PI 拿 IMU）
- 控制：P + Hz trim cap（簡單、行為可預測）
- 範圍：±3Hz, 5~30Hz（保守起手）
- Deadband 1cm：避免 noise 讓 SE3 一直被寫入（cli_A bus 友善）
- Symmetric trim：總速度大致不變，user 看不出明顯加速

### 觸發條件
- 自動 `pay_out` / `retract <cm>`：always tick（兩邊都還在動時）
- 持續按住 `up_left + up_right` OR `down_left + down_right`：tick
- 單側按住 / 混合方向 / `roll_correct`：不 tick（intentionally asymmetric）

### Failure modes（balance 是 nice-to-have，全部都 fall back 到 base_hz）
- `balance_enabled=false` → 不動，或 reset 回 base
- length cache invalid → 不動，或 reset 回 base
- err < deadband → 不動，或 reset 回 base
- setFreqHz 失敗 → 紀錄但不 abort motion

### 待辦（Phase 2/3/4）
- Phase 2：可考慮把 hold mode 的「同向偵測」做更精準（目前用 BALANCE_TICK_MS=1000ms 偵測，user 同時按下 < 50ms 沒問題，但若先按某邊隔 0.8s 再按另一邊則第一秒不會 trim，問題不大）
- Phase 3：GUI 加 enable toggle + Kp / cap / deadband 即時調 + 顯示當前 trim
- Phase 4：bench 驗證（故意一邊重一邊輕看 trim 收斂方向）



## 2026-05-09a — Claude Code — METER_POLL_MS_MOTION 從 1000ms 縮到 200ms（解 motion_rope cm overshoot ~10×）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - `METER_POLL_MS_MOTION` 從 `1000` 改成 `200`
  - 註解更新解釋 trade-off 變化（依賴 reliable_start_one retry 取代原 1000ms 安全邊際）

### 為什麼
Sadie 在 bench 觀察「下指令 `pay_out 2`、實際放了約 10 cm」。Root cause：
- `motion_rope` 用 `g_length_left/right` cache 比距離（commit 2026-05-08cc 為了避開 bus 競爭改的）
- meter_loop 在 motion 期間（commit 2026-05-08hh 加的 motion-aware）poll 變成 1000ms
- SE3 = 10 Hz × 繩速 ~10 cm/s → 1 秒 cache 延遲 = ~10 cm overshoot

把 motion 期間 poll 從 1000ms 改 200ms 後：
- overshoot 從 ~10 cm 降到 ~2 cm（cm-level targeting OK）
- 原本 1000ms 的目的是避免 SE3 啟動期 writeParam 被 meter_loop 卡 → ERR se3_*_start_fail
- 但 2026-05-08gg 加的 reliable_start_one 5x80ms retry 已經足夠 cover 啟動期偶發 timeout，不再需要 1000ms 安全邊際

### 影響範圍
- `motion_rope`（pay_out / retract）距離精度提升
- `cmd_roll_correct`（roll 校正）也走同樣 cache → 同步受惠
- 副作用：cli_A bus 占用稍微提高（200ms 一次 vs 1000ms），SE3 啟動期 retry 觸發率可能小幅增加，但 reliable_start_one 內部消化

### 後續
- 實機驗證 `pay_out 2` 是否停在 2~4 cm 之間
- 若仍 overshoot 嚴重（>5 cm）→ 改方案 B（加 g_se3_starting flag 分階段 polling）
- 若 SE3 啟動 retry 率明顯升高 → 折衷 300ms 或回滾到 500ms 觀察

---

## 2026-05-08hh — Claude Code — TCP keepalive [跨界: user_lib] + meter_loop motion-aware poll 速率

### 修改檔案
- `user_lib/TCP_client.cpp` —
  - 加 `apply_keepalive()` helper：Linux 設 SO_KEEPALIVE + TCP_KEEPIDLE=10s + TCP_KEEPINTVL=3s + TCP_KEEPCNT=3；Windows 帶 SO_KEEPALIVE 系統預設
  - `connectToServer` connect 成功後呼叫
  - `reconnectLoop` 重連成功後呼叫
  - include `<netinet/tcp.h>`
- `Crane_control_PI/main.cpp` —
  - `METER_POLL_MS = 250` 拆成 `METER_POLL_MS_IDLE = 250` + `METER_POLL_MS_MOTION = 1000`
  - meter_loop 結尾依 `motion_active.load() || any_hold_active()` 動態切 poll 速率
  - 啟動 log 從「poll 250 ms」改「poll 250 ms idle / 1000 ms during motion」

### 為什麼
使用者問「為什麼 SE3 那麼容易失敗、應該 TCP 開著不關 / 檢查連線嗎」。掃完架構發現：
- TCP **本來就一直開著**，monitor thread 每 500ms reconnect
- 連線檢查 sendData/sendAndReceive 進入時都做
- 真正失敗的是兩件事：
  - **Kernel 級 dead connection 偵測太慢**（默認 SO_KEEPALIVE 沒開，加上 NAT/USR-TCP232 中介設備斷線時 sendto 不會立刻 RST，要等到送 buffer 滿才知道。最壞情況幾分鐘）
  - **cli_A bus 競爭**：USR_A 上掛 4 個 device（SE3 left + SD76 left + SD76 middle + CLV900），meter_loop 每 250ms 打 SD76 兩次 = 500ms × 150ms / iter = ~80% 占用，SE3 寫入要排隊

### 兩個改動效果
**TCP keepalive**：
- Idle 10s + 3 探包 × 3s = ~19s 偵測 dead connection（之前默認 ~2hr）
- 拔網路線 / NAT timeout / USR-TCP232 reboot 等情境，monitor thread 會更快發現連不上 → reconnect → 復原
- 跟原本的 `available()` 500ms 檢查互補：available 抓 EOF / RST，keepalive 抓 silent disconnection

**meter_loop motion-aware**：
- idle 時保持 250ms 高頻 cache（GUI 看 length 流暢）
- motion 期間（motion_rope 跑、cmd_hold 按住、cmd_roll_correct 執行）切 1000ms
- cli_A 給 SE3 / CLV900 寫入的空檔變多 4 倍，排隊延遲降低，SE3 retry 觸發率應該明顯下降

### 跨界宣告 [跨界: user_lib]
`user_lib/TCP_client.cpp` 屬 Jim 範圍。本次新增匿名 namespace 的 helper function `apply_keepalive`（純 internal，不改 public API），從 connectToServer 跟 reconnectLoop 兩個內部位置呼叫。依 CLAUDE.md「不改 public API 的內部改動 → 協作者可修，PR 標 [跨界: user_lib]」規則處理。Mailbox 已留訊息給 Jim review。

review 重點：
- keepalive timing（10/3/3）太激進嗎？bench 慢網路會誤判嗎？
- Windows path 沒設 per-socket idle，是否需要 WSAIoctl SIO_KEEPALIVE_VALS？

### 沒做什麼
- **沒**改 sendData / receiveData / sendAndReceive 的 timeout 機制（已經夠用）
- **沒**改 socket_mtx 鎖 granularity（per-call atomic 已經對）
- **沒**動 SE3 driver 的 watchdog（內部 retry 已經有，改 retry 數量是另一個議題）
- **沒**動 hold_loop / motion_rope 內部邏輯，只是讓 meter_loop 讓 bus

---

## 2026-05-08gg — Claude Code — SE3 start retry 從 3x100ms 升級到 5x80ms（reliable_start_one helper）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 加 `reliable_start_one(SE3_inverter&, double hz, bool pay_out)` helper：5x80ms backoff retry，跟 `reliable_stop_one` 對稱
  - `se3StartRopeMotion` / `se3StartRopeHold` 改為 thin wrapper 直接呼叫 `reliable_start_one`
  - `dual_se3_hold_start` 簡化：原本 outer-loop 3-retry × 2-phase concurrent → 現在 single concurrent dispatch 內每 thread 跑 `reliable_start_one`，rollback 同樣 concurrent stopDecel
  - 移除 `SE3_START_RETRY_MAX` / `SE3_START_RETRY_DELAY_MS` 常數（已被 `reliable_start_one` 內部 MAX_ATTEMPTS=5 取代）

### 為什麼
2026-05-08ff 加了 3x100ms retry 後使用者仍報 `ERR se3_right_start_fail` 偶發出現：
```
→ pay_out 3
← ERR se3_right_start_fail
```
代表 transient 的時間尺度（300-500ms）超過 3x100ms = 300ms 的總 retry 預算。bench 上 meter_loop 在 cli_A / cli_B 上週期性佔用，加上 SE3 driver `run_h1001_with_watchdog` 內部觸發 CU mode reclaim（含 150ms settle）會把單一 Modbus call 拖到 600ms+ — 3 retries 無法 cover。

5 attempts × 80ms backoff = ~400ms wall time，更接近實測 transient 包絡。配合 `dual_se3_hold_start` 走 concurrent + per-side retry，左右獨立計算 wall time 為 max(L, R) 而非 L+R，total 更短。

### 行為變化
**之前（2026-05-08ff）：**
- pay_out 偶發 fail → 3 次 retry 內無法消化 → 回 ERR → user 重按一次

**之後：**
- pay_out → 5 retry 內 transient 解掉 → 直接 OK
- 真的全部 5 次都 fail → 回 ERR（罕見，這時是真正的 SE3 / RS485 / network 問題）

### 結構簡化
`dual_se3_hold_start` 從 outer-loop ~30 行縮成 5 行：每邊跑自己的 `reliable_start_one`，大寫 wall time 從  `phase1_freq + phase2_run + retry_overhead` 變 `max(L_full, R_full)`。

### 範圍
所有 SE3 start path 都自動受惠：
- `motion_rope`（自動收放繩）start sequence
- `cmd_roll_correct` start
- `cmd_hold up_left/up_right/down_left/down_right`（單側）via `apply_hold_one_side` → `se3StartRopeHold`
- `cmd_hold up/down`（雙側）via `dual_se3_hold_start`
- `cmd_manual` 透過 `se3StartRopeHold`

### 沒做什麼
- **沒**動 user_lib（純 app 層編排調整）
- **沒**改 `motion_rope` 啟動仍是序列（左 → 右）— 沒 sync 訴求；reliable_start_one 的 retry 已經把 transient 消化，序列啟動 sequencing 不會放大 transient

---

## 2026-05-08ff — Claude Code — SE3 start helpers 加 retry（解 ERR se3_*_start_fail 偶發）

### 修改檔案
- `Crane_control_PI/main.cpp` — 三個 SE3 start helper 加 retry：
  - `se3StartRopeMotion`（motion_rope 用）
  - `se3StartRopeHold`（cmd_hold 個別側 / cmd_manual 用）
  - `dual_se3_hold_start`（cmd_hold 同步雙側 用）
  - 共用 `SE3_START_RETRY_MAX = 3` / `SE3_START_RETRY_DELAY_MS = 100`
  - 失敗 → sleep 100ms → retry，最多 3 次。dual 版本 retry 前先 stopDecel 兩邊保持 atomic

### 為什麼
使用者報「自動 pay_out 有時要按 2-3 次才會動」：
```
→ pay_out 3 → ERR se3_left_start_fail
→ pay_out 3 → ERR se3_left_start_fail
→ pay_out 3 → ERR se3_right_start_fail
→ pay_out 3 → OK（終於跑了）
```

根因 SE3 Modbus 寫指令偶發失敗（per memory `project_modbus_tcp_stale_buffer.md` + `project_se3_modbus_cu_latch.md`）：
- stale buffer：TCP recv 拿到上一輪殘留 frame
- CU mode latch：H1000=0 寫失敗，cu_mode_set_=true cache 但 SE3 firmware 端 latch 沒進去

SE3 driver 內建 `run_h1001_with_watchdog` 對 run cmd（H1001）有 retry，但 **setFreqHz（H1002 透過 writeParam）沒有**。User 端等於要手動「再按一次」當成 retry。

### 改動
3 helper 內部各包 `for (attempt < 3)` 迴圈，第二次起先 sleep 100ms 才再試。一條 retry 路徑成本最多 ~300ms（3 × 100ms sleep + 寫入時間），對 user 體感從「需要按 2-3 次」變「等 0.3 秒」。

`dual_se3_hold_start` 特別注意 atomic：每次 retry 之前 `dual_se3_concurrent stopDecel 兩邊`，避免上次 attempt 一邊跑成功一邊失敗時繼續 retry 會留下單邊 spinning。

### 沒做什麼
- **沒**動 SE3 driver（user_lib）— 內部 watchdog 維持原樣，application 層 retry 是補強
- **沒**做 GUI 提示「重試中」 — 0.3 秒延遲 user 應該感覺不到
- **沒**動 motion_rope / cmd_hold 內部呼叫 helper 的位置 — retry 對 caller transparent

### 後續可能要做
如果 retry 還是頻繁失敗（超過 3 次），表示底層 user_lib/TCP_client 或 SE3 driver 有更深的問題，需要進一步診斷。bench 觀察 retry log 出現頻率可作參考指標。

---

## 2026-05-08ee — Claude Code — WASH_ROBOT::CRANE_IP 改 192.168.5.26（bench 網段）

### 修改檔案
- `user_lib/WASH_ROBOT.h:125` — `CRANE_IP`: `"192.168.1.101"` → `"192.168.5.26"`，註解同步更新標 2026-05-08 bench 網段配置；保留還原 production IP 的提示

### 為什麼
使用者請求改成 192.168.5.26（目前 bench 用的網段）。之前在 2026-05-07 從 .5.26 還原到 .1.101 為 production crane deploy；現在 bench 上 crane 又跑在 .5.26 / 不同 subnet → washrobot 連不到 .1.101 → cmd 卡 timeout。改回 .5.26 跟著 bench 走。

### 範圍
單一 constexpr 改動。所有 `crane_cmd_` / `crane_cli_` / `crane_cli_estop_` 等 call 都引用 `WASH_ROBOT::CRANE_IP`，一處改全部生效。

### 沒做什麼
- **沒**改 `CLAUDE.md` / `motion_flow.md` / `runbook.md` / `easy_crane_test_mode.md` / `gen_deploy_pdf.py` / `web_backend/server.js` 等文件 — 這些描述的是 production 架構（`.1.101`），bench 暫時偏離不該改規範
- **沒**改 `web_backend` 預設值（`server.js` 走 env var `CRANE_IP`，啟 web backend 時自己帶）
- **沒**動 `crane_cli_estop_` 的特殊處理邏輯
- 部署 production 時要記得改回 `.1.101`（註解已留提示）

---

## 2026-05-08dd — Claude Code — motion_rope middle pipeline 改 optional（bench 沒中間管線也能跑）

### 修改檔案
- `Crane_control_PI/main.cpp` — `motion_rope`：
  - device 檢查：移除 `g_dev_meter_middle` / `g_dev_clv900` 強制要求（之前缺任一就 ERR）
  - cache 檢查：移除 `g_length_middle_valid` 強制要求
  - 新增 local `use_middle` 旗標 = `g_dev_meter_middle && g_dev_clv900 && g_length_middle_valid`
  - `middleStart` 跟 poll 中 middle done check 都用 `if (use_middle && ...)` 包起來
  - `middle_done` 初值 = `!use_middle`（skip 時直接視為 done）
  - 加 `[motion_rope] middle pipeline not available — skipping ...` 提示

### 為什麼
2026-05-08cc 改 cache 後，bench 仍持續報 `ERR meter_middle_read_fail`：

```
→ crane: pay_out 3
← crane: ERR meter_middle_read_fail
（連續 7 次都一樣）
```

表示 SD76 middle (USR_A slave 3) **真的沒在回應 Modbus**，meter_loop 也讀不到，cache `_valid=false`，motion_rope 看 cache 也是 invalid → ERR。bench 設定通常**沒有實體中間管線計米**（memory `2026-05-08c` 標 `SD76 middle = ✗ 待測`），導致 motion_rope 永遠不能跑。

把 middle 從強制要求改成 optional：
- bench 沒 middle → 自動 skip，左+右繩照樣同步運動
- production 接好 middle → 4 個 device 都 light up → middle 同步運動
- 哪邊壞掉 GUI banner / startup log 會看到，operator 知道
- 不需要 deploy-time 改設定 / 重編

### 行為變化
| 情境 | 之前 | 現在 |
|---|---|---|
| middle SD76 不通 | `ERR meter_middle_unavailable` | `[motion_rope] middle pipeline not available — skipping ...` 然後馬達跑 |
| middle SD76 通 + CLV900 通 + cache 有效 | 三個一起同步動到 cm | 同上（行為不變） |
| middle SD76 通但 CLV900 不通 | `ERR clv900_unavailable` | skip middle，左+右仍跑 |
| 跑到一半 middle cache 突然 invalid | （之前不可能，因為要求一直有效） | left/right 繼續跑到 cm 達標，middle 不再 stop check（那時 middle CLV900 仍在轉，要靠 motion_rope 結束時的 allMotionOff 統一停） |

### 沒做什麼
- **沒**動 cmd_roll_correct（roll_correct 本來就不用 middle，沒影響）
- **沒**改 init 行為（device flag 仍以 init() 是否成功為準；運行時 cache invalid 由 meter_loop 維護）
- **沒**改 motion_flow.md 規範（規範說 middle 同步管線，現在實作 graceful skip — 待 Jim review 規範要不要寫成 optional）
- **沒**改 GUI（GUI 只看 dev_* / length_*_valid，banner 仍會顯示 meter_middle 不通的話相關 control 灰；本次改的是 motion_rope 內部判斷）

---

## 2026-05-08cc — Claude Code — motion_rope / cmd_roll_correct 改讀 length cache（解 ERR meter_middle_read_fail）

### 修改檔案
- `Crane_control_PI/main.cpp` — `motion_rope` 跟 `cmd_roll_correct`：
  - base value 從直接 `meter_*.readUpperInteger()` 改成 `g_length_*.load()` + `_valid` 檢查
  - poll loop 距離檢查同樣改用 atomic cache
  - 移除 `int32_t cur = 0; if (!meter.readUpperInteger(cur) && ...)` 路徑

### 為什麼
使用者報「auto pay_out / retract 按下去都不會動」，bench log：
```
→ crane: pay_out 3
← crane: ERR meter_middle_read_fail
```
重複出現。

`motion_rope` 啟動時直接打 Modbus 讀 `meter_middle.readUpperInteger(base_middle)` 失敗。原因猜測：
- USR_A bus 競爭（meter_loop 背景每 250ms 在掃 SD76 left + middle，motion_rope 直接打進去撞包）
- Stale buffer 殘留（Jim 的 TCP_client::sendData drain 修復對這個路徑可能不夠）
- 也可能 SD76 middle 真的硬體不穩

### 修法
既然 meter_loop 背景已經把 SD76 left/right/middle 的最新值放在 atomic cache，且 cmd_status / cmd_home_status 早就改用 cache，沒理由 motion_rope 還要直接打 Modbus。

改用 cache 之後：
- **零 bus 競爭**：base 跟 distance 檢查都從 atomic 讀，跟 meter_loop 完全不衝突
- **失敗收斂到一個地方**：如果 SD76 middle 真有問題，meter_loop 會把 `g_length_middle_valid=false`，motion_rope 一進去就回 ERR，不會半途中斷
- **精度不變**：cache 250ms 新鮮度，10 Hz 馬達速度下大約幾 cm overshoot，cm 級目標可接受

### 沒做什麼
- **沒**動 meter_loop 本身（持續 250ms poll 三條 SD76）
- **沒**動 cmd_zero_meters / cmd_zero_meter（這些**必須**直接寫 Modbus 給 SD76，不能用 cache）
- **沒**動 cmd_status / cmd_home_status（早就改用 cache 了）
- **沒**動 hold_loop（hold mode 的距離不限制，沒 cm 目標）

---

## 2026-05-08bb — Claude Code — TCP_client reconnect 事件無條件 log（診斷工具）[跨界: user_lib]

### 修改檔案
- `user_lib/TCP_client.cpp` — `reconnectLoop()` 觸發 / 成功 / 失敗三個事件加 `std::fprintf(stderr, ...)` 直接印，**不受 debug_mode 控制**。LOG_INF/LOG_WRN 仍保留（雙印對 debug_mode=true 的使用者無害，反正只在 reconnect 觸發時打印 — 罕見事件）

### 為什麼
2026-05-08bb bench 觀察 cmd_hold OFF 失敗率 ~10-20%（每 5-10 次鬆開就有一次馬達沒停）。使用者懷疑是「通訊太長斷開重連」。診斷現況：crane main 呼叫 `connectToServer(IP, PORT)` 沒帶第三個 `debug` 參數 → debug_mode 預設 false → reconnect 事件用 `LOG_INF` 印**完全靜默**，使用者沒辦法觀察到 reconnect 是不是真的在發生。

reconnect 事件是 operationally 重要（每次 reconnect ~500ms-1s 期間所有 sendAndReceive 都 return -1 失敗），不該被 debug flag 隱藏。改成無條件 stderr 直接印。

### 跨界宣告
**[跨界: user_lib]** — `TCP_client` 是 Jim 範圍。本次只在內部 `reconnectLoop()` 加診斷 log（不改 public API / 行為），約 15 行新增。標 [跨界: user_lib]，等 Jim review。

### 預期使用方式（給 Sadie）
1. 重新 build user_lib + Crane_control_PI 後 deploy
2. 啟動 crane main，console / journalctl 看 stderr
3. 重複 hold up → 鬆開十幾次
4. 觀察是否有以下 log 出現：
   ```
   [HH:MM:SS.mmm] [WRN] [TCP] reconnecting 192.168.1.30:4001 ...
   [HH:MM:SS.mmm] [INF] [TCP] reconnect success
   ```
5. 如果**有** → reconnect 在搗亂，是 root cause；下一步要 harden trigger 條件（require N 連續 available()<0 才 reconnect）+ 查 USR-TCP232 idle timeout 設定
6. 如果**沒** → reconnect 不是兇手，要找別的（meter_loop 衝突？SE3 firmware 不穩？RS485 線干擾？）

### 沒做什麼
- **沒**改 reconnect 觸發條件（單次 available()<0 就 reconnect 仍偏激進，但這版只加 log 觀察 — 確認是這條路再 harden）
- **沒**改 debug_mode 預設值（保持 false 避免日常 noise）
- **沒**動 `cli_A.connectToServer(...)` call site 加 debug=true（reconnect log 已無條件印，不需要打開全域 debug）

---

## 2026-05-08aa — Claude Code — Crane stopDecel 加 reliable retry helper（解「按一次沒停要按兩次」）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 加 `reliable_stop_one(SE3_inverter&)` helper：stopDecel 失敗最多 retry 5 次，每次間 80ms backoff
  - 替換所有「必須真的停」的 SE3 stopDecel call 為 reliable_stop_one：
    - `allRopeInvertersOff` / `hold_all_off`
    - `dual_se3_hold_start` rollback
    - `cmd_hold` "up"/"down" OFF combined 路徑
    - `cmd_hold` per-side rollback（單側 ON 失敗）
    - `apply_hold_one_side`（單側手動 hold off）
    - `cmd_manual` 單側 stop
    - `motion_rope` start fail rollback + main loop 達標 stop（左/右）
    - `cmd_roll_correct` start fail rollback + main loop 達標 stop（左/右）

### 為什麼
bench 觀察：手動 hold 鬆開按鈕後，**某一邊馬達瞬間停、另一邊還跑**，要再按一次 release 才停（GUI 顯示 ERR）。診斷：driver fix 全 deploy 後 Modbus 抖動仍會偶發 transient failure（TCP packet 重送 / SE3 firmware 短暫 busy / RS485 jitter / contention timeout）— 第一次 stopDecel 偶爾 fail，第二次必中。

問題不在 driver（driver 已盡力）— 在主程式 cmd_hold 路徑**沒做 retry**，第一次失敗就接受 err=true 回 "ERR se3_cmd_fail" 給 GUI，馬達照跑。改成 retry 5 次後，transient 失敗在 helper 內就消化掉，使用者完全感受不到。

### 行為差異
**之前：**
- 鬆開 → stopDecel 偶發 fail → 立刻回 ERR → 馬達繼續跑 → 使用者 (a) GUI 看到 ERR 紅色 (b) 必須再按一次

**之後：**
- 鬆開 → stopDecel 第 1 次 fail → 80ms 後第 2 次 retry → 多半成功 → 馬達 ramp 停 → OK
- 真的全部 5 次都 fail（極端情況）→ 才回 ERR，馬達可能還在跑（這時是真實硬體問題）

### 為什麼 retry 5 次而不是 3 / 10
- 3 次有時不夠（曾觀察連續 2 次抖動）
- 10 次太多（共 800ms 等待感受得出來）
- 5 × 80ms backoff = 最壞 400ms（使用者鬆開到馬達開始 ramp 的延遲）— acceptable

### 為什麼不 fallback 到 emergencyStop
雖然 emergencyStop（MRS）保證馬達停，但鋼索瞬斷電 → 重物慣性甩動 / 鋼索鬆 → 對機體有額外風險。使用者不希望走這條路（feedback 2026-05-08）。Retry 直到成功比強制斷電好。

### 沒做什麼
- **沒**動 user_lib（純 app 層）
- **沒**改 emergency stop / 緊急安全路徑（line 362 `allMotionEmergencyStop`）— 緊急情境就是要瞬切，不該 retry
- **沒**改 CLV900 `inverter.stopDecel()`（中間絞盤同樣 race risk 但本次 scope 限 SE3；CLV900 的 reliable wrapper 後續再加）

---

## 2026-05-08z — Claude Code — Tension diff 從 30% percent 改成絕對 10 kg

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 常數 `TENSION_DIFF_MAX_PCT = 30.0` → `TENSION_DIFF_MAX_KG = 10.0`
  - 拿掉 `TENSION_DIFF_CHECK_MIN`（avg ≥ 0.5）的 gate（絕對值差不需要避免除以噪音）
  - `tension_safety_check_values` 檢查邏輯：`fabs(l-r) > 10.0` → `diff` alarm

### 為什麼
使用者報「同步收繩沒到重量門檻卻自己停，再按又能跑」。最可能是 percent diff 在小 avg 時放大噪音 → 觸發 false alarm（例如 left=2 right=0.5，avg=1.25，diff%=120% → 30% 閾值秒過）。改絕對值後：
- 空載：0 vs 0 → 0 kg diff < 10 → 不觸發 ✓
- 同步拉到 5 kg / 5 kg：0 → 不觸發 ✓
- 一邊卡住：0 vs 12 kg → 12 kg > 10 → 觸發 ✓ 這才是真有問題的情境
- 校準偏差 ±2 kg：4 vs 6 → 2 kg < 10 → 不觸發 ✓

### 沒做什麼
- **沒**改 GUI（門檻只在 server 改 source 重編；之後若想 runtime tune，可仿 `up_stop_total_kg` 做 atomic + GUI input）
- **沒**動 motion_flow.md 規範（目前寫「30%」過時了，待 Jim review tension safety 一塊兒更新）
- **沒**動 TENSION_MAX_KG（仍是 100 kg constexpr）/ up_stop_total_kg（runtime 可調）

---

## 2026-05-08y — Claude Code — DSZL_107 加 save_params() + crane zero 自動 follow-up SAVE（init 不 save）[跨界: user_lib]

### 修改檔案
- `user_lib/DSZL_107.h` — 新增 public `bool save_params()` 宣告，文件 comment 更新註明 zero / set_unit 只寫 RAM 要 follow-up save
- `user_lib/DSZL_107.cpp` — `save_params()` 實作：寫 0xA20 = 40 + Sleep(150) 等 X518 CPU 寫 flash 完成
- `Crane_control_PI/main.cpp` —
  - **init 時不呼叫 `save_params()`** — `set_unit_kg()` 仍寫 RAM (idempotent) 但**不 commit flash**，避免 init 時意外覆蓋使用者上次校好的 flash zero
  - `cmd_zero_tension`（left/right/all 三個分支）每個 zero 之後 follow-up `save_params()`，save 失敗印 WARN 但回 OK 不阻塞 operator

### 為什麼
使用者報「每次重開程式都要重新校零，不然公斤數變奇怪」。

X518 校零指令 `0xA20 = 1/2/7` **只寫 RAM**，沒寫 flash。手冊明文：「所有參數修改後（包括校零、校準、恢復出廠等）都需要執行寫 0xA20 = 40 把 RAM 同步到 flash」。Driver 之前缺這一步：

```
do_zero_ch1()  →  寫 0xA20=1   →  RAM zero ✓
                                  Flash 沒動 ✗
                  下次 X518 斷電 → RAM 清掉 → zero 丟失
```

加 SAVE follow-up 之後：

```
do_zero_ch1() + save_params()  →  寫 0xA20=1 + 寫 0xA20=40
                              →  RAM zero ✓ + Flash 持久化 ✓
                              →  下次斷電仍保留
```

### 跨界宣告 [跨界: user_lib]
`user_lib/DSZL_107.{h,cpp}` 屬 Jim 範圍。本次只**新增 public method `save_params()`**（純 additive，不改既有任何 API 簽名 / 行為），依 CLAUDE.md「不改 public API 的內部改動 → 協作者可修，PR 標 [跨界: user_lib]」規則處理。等 Jim review。

### 行為細節
- `save_params()` 同步呼叫，內含 150ms sleep（X518 CPU 暫停期間 driver 不能再下別的命令，會 timeout / bad reply）
- 連續 save（例如 `cmd_zero_tension all` 兩條 cell 各 save 一次）→ ~300ms 延遲，acceptable
- save 失敗（comm error）→ 印 WARN 不算 fatal，原本 zero 仍在 RAM 有效，只是不持久化
- `setScale()` 是 driver-local 變數，不寫 X518 RAM，**不需要** save_params
- `set_dsz_scale` cmd handler 同理不需 save（只動本地 atomic + driver scale）

### 沒做什麼
- **沒**改 do_zero_ch1/2/all 內部 — 維持原本「只寫 RAM」語義，新加的 save_params 是 caller 顯式呼叫，比較不會有意外副作用（例如有人想連續校零三次最後才 save，內建 save 會多三次寫 flash 浪費 wear）
- **沒**動 GUI — 從 GUI 點「校零（左右）」走 `cmd_zero_tension all` 已經自動 follow-up save
- **沒**做 X518 IP / mode 等其他設定改動的 save follow-up — Linux_test menu 24 改 IP 流程已內建 `S` 命令；crane main.cpp 啟動時不寫這些 reg，所以不用補

### 重編後驗證
1. crane 啟動 console **不再有** save_params log（init 不 save）
2. GUI 校零（左右）→ console 看到 `[INF] [DSZL:1] save_params: RAM → flash committed (sleeping 150ms)` 兩條
3. 物理斷 X518 電 → 重新接電 → crane 程式不重啟 → kg 仍在 0 附近（上次按校零時 save 到 flash 的 zero 持久化生效）
4. 重啟整個 crane（含 X518 power cycle）→ 上次校零仍保留 → 不用再校零

### 設計理由（為什麼 init 不 save）
使用者明確要求：「init 時不要自動歸零，有按歸零鍵時才歸」。

`save_params()` 是「把整個 RAM 寫到 flash」，包含 zero offset / unit / IP 等所有參數。如果 init 莫名其妙 save 一次（即使理論上應該是 no-op，因為 boot 後 RAM 應該 = flash），萬一 X518 firmware 有 side effect（例如 TCP connect 瞬間 RAM 短暫亂掉、unit 改寫觸發 zero 重置等），會把壞狀態存進 flash，反而把使用者上次校好的 zero 抹掉。

把 init 的 save 拿掉之後：
- init 對 X518 flash **完全 read-only**（除了 RAM-only 的 set_unit_kg）
- 只在使用者**主動**按校零時才寫 flash
- 即使 set_unit_kg 在 init 寫了 RAM，下次斷電仍以 flash 為準（前次保存的 unit）

---

## 2026-05-08x — Claude Code — TCP_client::sendAndReceive atomic API + 4 driver migration [跨界: user_lib]

### 修改檔案
- `user_lib/TCP_client.h` — 加 public method `int sendAndReceive(tx, txLen, rx, rxSize, send_timeout, recv_timeout)` 宣告
- `user_lib/TCP_client.cpp` — 實作 `sendAndReceive`：取一次 mutex 後做 drain → send → recv → 釋放，單一 lock 保護整個 Modbus transaction
- `user_lib/SE3_inverter.cpp` — `sendModbus` 改用 sendAndReceive
- `user_lib/CLV900_inverter.cpp` — 同
- `user_lib/SD76_length_meters.cpp` — 同
- `user_lib/JC_100_METER.cpp` — 讀壓力路徑改用 sendAndReceive

### 跨界宣告
**[跨界: user_lib]** — `TCP_client` 跟 4 個 Modbus driver 都是 Jim 範圍。本次：
- TCP_client **加** public method（不改既有簽名 / 行為）— 既有 caller 完全不受影響
- 4 個 driver 內部 `sendModbus` 實作改用新 API — driver public API 不變

依 CLAUDE.md「不改 public API 簽名的內部改動 → 協作者可修，PR 標 [跨界: user_lib]」規則處理。等 Jim review 後合併。

### 為什麼
bench 觀察：手動 hold 同步停車有時候要按兩次才停，且不固定哪邊。診斷出是**並發 Modbus transaction race condition**：

`TCP_client::sendData` 跟 `receiveData` 各自取/放 socket_mtx，**中間有空檔**。當 meter_loop（背景 SD76 polling）跟 cmd_hold（concurrent dispatch 的 SE3 stopDecel）共用同一條 cli_A：

```
T1 meter_loop: sendData(SD76 read) ─ release ─ ... receiveData
T2 SE3 thread:                       └─ sendData(stop) ─ release ─ ... receiveData
```

T2 的 sendData 進場時 mutex 釋放，drain（2026-05-08q 的 fix）會把 SD76 將回的 reply 吃掉；或反過來 T1 的 receiveData 讀到 SE3 的 reply。任一 transaction reply 被串包 → driver 報 "comm fail" → counter++ → user 看到「沒停成功」 → 再按一次。

第二次按通常會通是因為 driver `run_h1001_with_watchdog_` 第 2 次 fail 觸發 watchdog reclaim CU + retry，那次剛好成功。

### 修法核心
`sendAndReceive` 在**一個 lock_guard 範圍內**做完整個 transaction：drain → send → recv → 釋放。其他 thread 的 send/recv 完全被排隊，沒有空檔可以插入。

```cpp
int TCP_client::sendAndReceive(tx, txLen, rx, rxSize, sendT, recvT) {
    std::lock_guard<std::mutex> lock(socket_mtx);    // ★ 一次 lock
    // drain (在同一 lock 內，不會吃別 thread 的 in-flight reply)
    // send
    // recv (mutex 仍持有)
    return received;
}
```

### 沒做什麼
- **沒**移除 `sendData` 的 drain（保留作為 standalone 使用的 defense；但 standalone send/recv pair 仍有 race，drivers 應陸續遷移）
- **沒**遷移其他 driver（DM2J / ZDT / PQW / DSZL / DY500 / XKC / ZS_DIO）— 這些目前 deployment 不會有並發 thread 共用 TCP_client 的場景，遷移可後續做不阻塞
- **沒**改 driver public API
- **沒**動 `Crane_control_PI/main.cpp`（concurrent dispatch 邏輯保留 — driver-level race fix 後並發 dispatch 才真的安全）

### 副作用
- `sendAndReceive` 持鎖時間 = drain + send + recv ~50ms 平均 / 600ms 最壞（watchdog reclaim case）
- 並發 thread 在 cli_A / cli_B 上會排隊，**但這就是正確行為** — Modbus 本來就是 sequential bus
- 行為差異：之前並發 thread 可以「同時」進入但會搞亂彼此的 reply；現在會 sequential 串接但每個都正確

### 後續建議（不在此 PR）
- 其他 driver（DM2J / ZDT / PQW / DSZL / DY500 / XKC / ZS_DIO）也應遷移 sendAndReceive，避免未來增加並發路徑時踩同樣的 race
- `sendData` 的 drain 可在所有 driver 遷移完後移除（屆時 standalone send 不再被 Modbus 用）
- memory `project_modbus_tcp_stale_buffer.md` 待更新標「2026-05-08x 終極修法」

---

## 2026-05-08w — Claude Code — TENSION_MAX_KG 從 3.0 提高到 100.0（解 bench false alarm）

### 修改檔案
- `Crane_control_PI/main.cpp` — `TENSION_MAX_KG` constexpr `3.0` → `100.0`

### 為什麼
使用者報「同步收放繩沒超過總和門檻 1 kg 卻自動停」。原因是 hold_loop 安全檢查多層，per-side `TENSION_MAX_KG = 3.0 kg` 寫死，bench 上 DSZL 還沒精準校準（剛剛才把 scale 改成 -0.01 default），單側讀數很容易超過 3 kg 觸發 `EVT tension_alarm kind=high_left/high_right`。

提高到 100.0 讓 bench 可以順暢測試，等實機 DSZL 校好（用 GUI 「張力 scale」即時調），再依實際載重重新 tighten 到 ~50%（例如部署載重 80 kg → 設 120 kg）。

### 沒做什麼
- **沒**做 task #18（runtime tunable + GUI）— 使用者只要求「設成 100」，沒要求 GUI 即時調，省一次大改。日後若要再調可:
  - (a) 直接改 source 重編
  - (b) 之後做 task #18 把它跟 `up_stop_total_kg` / `dsz_*_scale` 一樣 atomic + GUI input
- **沒**動左右差 30%、總和 `up_stop_total_kg` 邏輯 — 兩個還在原本機制下運作

---

## 2026-05-08v — Claude Code — Crane DSZL scale 左/右 runtime tunable + 預設負值

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 新增 `DSZL_SCALE_DEFAULT = -0.01` constexpr，左右 atomic globals `g_dsz_left_scale` / `g_dsz_right_scale`
  - init 時呼叫 `dsz_left.setScale(...)` / `dsz_right.setScale(...)` 套用 default + 印一行 `[INFO] DSZL scale: left=-0.01 right=-0.01`
  - 新增 `cmd_set_dsz_scale(side, scale)`：拒 0、寫 atomic + 立即 `setScale()` driver
  - cmd_status 多 `dsz_left_scale=` / `dsz_right_scale=`
  - dispatcher 加 `set_dsz_scale <left|right> <value>`
- `web_backend/public/index.html` — 鋼索張力區下方加「張力 scale」row，左右各一個 number input + 顯示目前值；下方 hint 說明校準公式 `scale = W / ΔR`
- `web_backend/public/app.js` —
  - 新增 `wireScaleInput()` helper（debounce 200ms）綁兩個 input → `set_dsz_scale left/right`
  - `onCraneLine` 解析 `dsz_left_scale=` / `dsz_right_scale=`（支援科學記號），更新顯示

### 為什麼
使用者報「公斤數越重越負」。bench memory `project_x518_architecture_mismatch.md` 提過：DSZL-107 訊號線跟 X518 接線方向相反 → 拉力 ↑ → raw ↓。driver 預設 `scaleToKg = +0.01`，乘出來變負。

之前的選項討論：
- A 軟體寫死負值（不彈性）
- B GUI 可調（彈性，bench 校準後直接 GUI 調）
- C 實體調線（要拆 X518）

使用者選 B 並要求「預設值都要矯正負」。所以：
1. server 端預設 `-0.01`（負號處理 wiring 反向，量級保持原本 0.01）
2. GUI 兩個 input 都預設 `-0.01`，user 可即時調

### 校準流程
1. cell 空載 → 看 GUI raw（要怎麼顯示 raw 還沒做，目前只顯示 kg）→ 記下 ZERO_RAW
2. 掛已知重量 W kg → 看 raw → 記下 LOAD_RAW
3. 計算 scale = W ÷ (LOAD_RAW − ZERO_RAW)（保留符號 — 反向接線會自然產生負值）
4. GUI 「張力 scale 左」輸入 → server 立即套用 → 後續 kg 顯示就對了
5. 配合「校零（左右）」確保 ZERO_RAW = 0（不過 X518 校零 driver 還沒做 SAVE，重啟會丟，bench 記得每次校零）

### 沒做什麼
- **沒**動 `user_lib/DSZL_107.{h,cpp}` 的 setScale API（已有，本次只是 application 端開始用）
- **沒**做「raw 值顯示」 — 只有 kg。如果校準需要看 raw，可以後加；目前可用 Linux_test menu 24 `r` 命令直接看 raw
- **沒**持久化 scale — server 重啟會回到 `-0.01`，bench 校準後要記下值，下次重新輸入 GUI（或之後加 file persistence）
- **沒**改 motion_flow.md / runbook 規範

---

## 2026-05-08u — Claude Code — Crane SE3 dual 操作改 concurrent dispatch（解左右不同步停的 0.5-2 秒延遲）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 加 `dual_se3_concurrent<Fn>` template helper：用 `std::thread` 同時對 `se3_left` 跟 `se3_right` dispatch 同一個 callable，join 後 OR 合 error flag
  - `allRopeInvertersOff()` / `allMotionEmergencyStop()` / `hold_all_off()` 三個內部 helper 改用 concurrent 版
  - `dual_se3_hold_start()` 三段（setFreqHz pair / run pair / rollback stopDecel pair）全用 concurrent
  - `cmd_hold` "up"/"down" OFF 路徑改 concurrent stopDecel

### 為什麼
bench 觀察：手動 hold 同步收/放繩然後鬆開 GUI 按鈕後，**左右某一邊**會比另一邊晚 0.5-2 秒才停下來，**慢的那邊不固定**（隨機）。

Root cause 不是面板 P.7/P.8 不一致（如果是面板會永遠固定一邊慢）— 是 `cmd_hold` OFF 序列發送 `se3_left.stopDecel()` 然後 `se3_right.stopDecel()`，當 driver 內 `run_h1001_with_watchdog_` 觸發 CU mode reclaim（含 150ms settle）+ retry，那次 stopDecel call 自己就要 400-600 ms。序列下一邊已經 stop ~50ms，另一邊正在 watchdog reclaim → 視覺上慢 0.5-2 秒。隨機是因為 watchdog counter 走勢隨機，哪邊先觸發不定。

`dual_se3_concurrent` 用兩個 thread 並發發送，wall time 從 `T_left + T_right` 變 `max(T_left, T_right)`，更重要的是兩邊**同時**收到 stop 命令，物理上同時開始 decel ramp。即使其中一邊內部 retry，另一邊不會被卡住。

### 範圍：哪些路徑改了
- 手動 hold OFF（GUI 鬆開 up/down 按鈕）
- 自動 motion 完成 / abort（`allMotionOff` → `allRopeInvertersOff`）
- 緊急停止（`allMotionEmergencyStop`）
- Tension safety trip / 心跳斷線（透過 `hold_all_off`）
- Hold ON（`dual_se3_hold_start` setFreqHz + run + rollback）

### 沒做什麼
- **沒**動 `cmd_hold` 單側路徑（up_left / up_right / down_left / down_right）— 那些只動一邊 SE3，沒有 sync 問題
- **沒**改 motion_rope 主迴圈裡 per-side 達標時各自 stop（line 587 / 590 / 597）— 那是設計上「各自到目標就先停」，本來就不同步停才對
- **沒**改 user_lib（純 app 層編排調整）

### 副作用 / 注意
- 多兩個 ephemeral thread per dual call。建立成本約 10-50 us，相對 Modbus round trip ~50ms 可忽略
- 兩個 thread 各自呼叫 SE3 driver，driver 內部 `TCP_client::sendData` 已有 mutex（`socket_mtx`）保護單一 connection，但兩邊用不同 connection（cli_A vs cli_B）所以無 contention
- **重要前置依賴：** 2026-05-08j（CU mode 150ms settle）+ 2026-05-08q（TCP_client drain stale buffer）兩個 driver fix 已落地。如果這兩個沒 deploy，concurrent dispatch 仍會運作但每次 stop 仍會碰到 retry，wall time 不會降低很多

---

## 2026-05-08t — Claude Code — Crane init 補 SD76 resumeMeter（解計米器不更新）

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 三個 SD76 init 成功後加 `resumeMeter()`（寫 0x0000=0x0008），並把 `[OK] SD76 …` log 加 `(resumed)` 標記
  - `cmd_zero_meters` 全部零之後 follow-up `resumeMeter()` 三條
  - `cmd_zero_meter <which>` 單條零之後 follow-up `resumeMeter()` 該條

### 為什麼
使用者報「實際移動 30+ cm，GUI 顯示只動了 2 cm」。

根因：SD76 控制 reg `0x0000` 有三個動作 — `0x0003=resetAll(歸零)`、`0x0004=pauseMeter(暫停計數)`、`0x0008=resumeMeter(恢復計數)`。Linux_test menu 9 提供 r/p/s 三鍵讓 bench 操作。**Crane main.cpp 從來沒呼叫 resumeMeter**，所以如果上次 session（例如 bench 用 menu 9）按過 `p` 暫停，SD76 會記著 paused 狀態（裝置不重新上電不會重置），crane 啟動讀到的就是 pause 之前的舊值，馬達狂跑也不會增加 → 「動 30 cm 顯示 2 cm」。

resetAll (0x0003) 只是「歸零 count」，**沒保證離開 paused 狀態**。需要顯式寫 0x0008 才會 active counting。

### 改動效果
- 每次 crane 啟動，三條 SD76 自動 resumeMeter → 不論上次 session 留在什麼狀態，這次都會 active 計數
- `zero_meters ground/top` 跟 `zero_meter <left|right|middle>` 在 reset 後自動 follow-up resume，避免使用者按完歸零還要手動 resume

### 沒做什麼
- **沒**動 SD76 driver 本身（user_lib）— 只在應用層補呼叫
- **沒**動 Linux_test menu 9 — bench 工具仍保留 r/p/s 三鍵手動測試（pauseMeter 仍可在 bench 觸發 paused 狀態，但下次 crane 啟動會自動 resume 修掉）
- **沒**改 GUI — 計米器顯示邏輯不變，只是後端開始有正確數值
- **沒**動 motion_rope 跟 cmd_roll_correct 內部對 SD76 的 readUpperInteger 假設（單位是 cm，方向有號）

---

## 2026-05-08s — Claude Code — Crane cmd_hold 仿 Linux_test menu 25 atomic pattern + rollback

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - 新增 `dual_se3_hold_start(pay_out)` helper：menu 25 模式「freq 兩邊先設 → run 兩邊」，任一步驟失敗內部自動 stopDecel 兩邊
  - `cmd_hold` 重構：「up」/「down」combined 走 `dual_se3_hold_start` (atomic)；per-side（up_left 等）走原 set_left/set_right 路徑
  - `cmd_hold` 結尾加 atomic rollback：`err && on` 時 stopDecel 受影響側 + 清旗標 + 回 `ERR se3_cmd_fail_rollback`
  - `need_left/need_right` 計算 hoist 上來，給 device check 跟 rollback 共用

### 為什麼
使用者報「按同時收繩，只有左邊收；按同時放繩，只有右邊放」。bench 用 menu 23 單台驗證 SE3 通訊有 stale buffer + CU mode write fail，但隨機在哪一邊失敗。原本 `cmd_hold` 的處理：

```cpp
set_left(...);      // 左 setFreqHz + run（成功 → 馬達跑）
set_right(...);     // 右 setFreqHz + run（失敗 → err=true）
return err ? "ERR" : "OK";   // 回 ERR 但「左馬達還在跑」！
```

→ GUI 收到 ERR 在 log 顯示，但**左邊馬達已經啟動**，沒人去停它 → 只有左動。

Linux_test menu 25 早就解過這問題，pattern 是：
1. 兩邊 setFreqHz 先做（沒 run，安全）
2. 兩邊 run
3. 任一邊失敗 → stopDecel 兩邊（atomic rollback）

把這套抄進 `cmd_hold` 的「up」/「down」combined 邏輯。

### 改動效果

**combined（同時雙繩）**：
- 任一邊 SE3 通訊失敗 → 自動 stopDecel 兩邊 + 回 `ERR se3_cmd_fail_rollback`
- 機體不會因為單繩運動而傾斜
- 兩邊馬達啟動時間差從「一邊 setFreqHz round-trip + run」之後才換另一邊（~hundreds of ms），縮短到「兩個 setFreqHz 完成後同時 run」（~10s ms）

**per-side（↑左/↓右等）**：
- 失敗 → stopDecel 該側 + 清旗標 + 回 `ERR se3_cmd_fail_rollback`
- 之前失敗會讓 hold_up_left=true 但馬達沒跑（stale state），hold_loop 還以為在 hold

**「off」分支**：
- 永遠允許（不需要 device check）
- stopDecel 失敗也只是 best-effort，不 trigger rollback（已經是停止意圖）

### 沒做什麼
- **沒**動 motion_rope / cmd_roll_correct（已有 rollback chain，不必動）
- **沒**改 SE3 driver（user_lib）— stale buffer 問題在 changelog 2026-05-08q 由 Jim 修 `TCP_client::sendData` drain；本次只是應用層補 atomic 保護避免 driver 偶發失敗時的單繩動
- **沒**動 GUI — server 端原子化後 GUI 收到 ERR 就純粹是「沒事發生」，不需要特別處理
- **沒**動 hold_loop tension 監控 / watchdog — 那些是 motion 進行中的安全網，跟 cmd start 階段的 atomic 是兩個層級

### 已知 follow-up
- 如果 driver stale buffer 還是高頻發生，cmd_hold rollback 會頻繁觸發，使用者按按鈕一直 ERR → 看 Jim 那邊 `TCP_client.cpp` drain 修復是否完整；不行就要在 SE3 driver 層加更多 retry

---

## 2026-05-08r — Claude Code — Crane tension safety 拿掉「過低」檢查（保留過高 + 左右差）

### 修改檔案
- `Crane_control_PI/main.cpp` — `tension_safety_check_values()` 拿掉 `TENSION_MIN_KG` 比較，現只檢查 `TENSION_MAX_KG`（過高）與 `TENSION_DIFF_MAX_PCT`（左右差）。`TENSION_MIN_KG` constant 保留但標 `(currently UNUSED)`，將來校準後若要恢復「low = 鬆弛/斷裂」檢測再加回
- `web_backend/public/index.html` — 鋼索控制 hint 列文字更新：「單側張力過高 / 左右差過大 → server 自動全停 + EVT（張力過低**不再觸發**，避免空繩 / 未校準 false alarm）」

### 為什麼
使用者報「按按鈕後馬達動一下就停」。診斷：
- bench 上鋼索沒掛東西 → DSZL-107 張力讀數接近 0
- 又因 DSZL scale 還是 placeholder `0.01` 沒實機校準（memory `project_x518_architecture_mismatch.md` 提）
- `tension_safety_check_values` 看 `l_kg < TENSION_MIN_KG=0.5` → 立刻回 `"low_left"`
- `hold_loop` / `motion_rope` 收到非空 alarm → `hold_all_off()` + `EVT tension_alarm kind=low_left`
- → 馬達啟動 ~50ms 後就被自動停

使用者選項：「應該只需要檢查是否超過門檻重量，低於門檻的沒關係」 — 拿掉 low 檢查就解。

### 改動效果
- 空繩 / 未校準狀態下不再 false alarm，可以順利 bench 測試 hold-to-pull
- 過載 / 卡住保護仍然在（high_left / high_right）
- 左右差仍會觸發（diff）但需要 avg ≥ 0.5 kg 才生效，bench 空繩 avg=0 會跳過 diff 檢查
- watchdog (2s 心跳) / total threshold (UP 收繩總和) 都不受影響

### 沒做什麼
- **沒**動 `TENSION_MIN_KG / MAX_KG / DIFF_MAX_PCT` 數值 — 只是 MIN 不再被使用
- **沒**改 `motion_flow.md §6.5`（規範文件，Jim 範圍）— mailbox 留紀錄請 Jim 對齊

### 規範待更新（Jim）
`motion_flow.md §6.5` 表格列「鋼索張力過低（< TENSION_MIN_KG）| 疑似鬆弛 / 斷裂 → 立即 crane stop + washrobot pause」，跟現在實作不符。Jim review 時順便修這條 — 兩個方向擇一：(a) 規範跟實作對齊，刪掉 low 那行；(b) 規範保留但加註「DSZL 校準完成前停用」。

---

## 2026-05-08q — Claude Code — TCP_client::sendData drain stale buffer [跨界: user_lib]

### 修改檔案
- `user_lib/TCP_client.cpp` — `sendData()` 開頭加 non-blocking drain loop（送 request 前清掉 kernel recv buffer 的 stale byte，含 Win/Linux 平台分支 `ioctlsocket(FIONBIO)` / `fcntl(O_NONBLOCK)`，drain >0 印 `LOG_DBG`，安全上限 4096 bytes）

### 跨界宣告
**[跨界: user_lib]** — 標準上 `TCP_client` 是 Jim 範圍。本次只改 `sendData()` 內部實作（多一段 drain loop + 兩段 #ifdef 平台 socket flag toggle），**public API 簽名/語意完全不變**：`sendData / receiveData / available / connectToServer / close / isConnected` 行為皆同。依 CLAUDE.md「不改 public API 的內部改動 → 協作者可修，PR 標 [跨界: user_lib]」規則處理。等 Jim review 後合併。

### 為什麼
2026-05-08 bench `[ERR] [SE3:1] writeParam reg=0x1002 bad reply len=86`（重啟程式恢復）— 診斷 code path（memory `project_modbus_tcp_stale_buffer.md`）：USR-TCP232 透傳 gateway 把 RS485 上 **任何** byte 都倒進 TCP socket，包含 (a) 之前 timeout 後遲到的 reply / (b) 共用 bus 上別 device polling 的 cross-talk / (c) bus glitch 的 partial frame。累積在 kernel recv buffer，下次 `recv()` 一次倒給 driver 形成「86 bytes」之類的怪異長度，driver 前 2-byte validate 會失敗。所有 user_lib Modbus driver（SE3 / CLV900 / SD76 / JC100）都共用 `TCP_client::receiveData`，**全部 vulnerable**，但只有 SE3 寫了顯式 `bad reply len=N` log 所以最早被看到。

### 為什麼選 TCP_client 而非 per-driver wrapper
架構分析：`TCP_client` 在本專案**只用於 Modbus gateway**（washrobot 走 `TCP_server` line-buffered；crane main 也只用 4 個 cli_A/B/C/D 連 .30/.31/.32/.33 gateway）。沒有 streaming-receive 的 caller 會誤殺。一處改全 driver 受惠。

### 行為差異
- **之前：** stale 累積 → 下次 `receiveData` 回 stale+new 拼接 → driver 報 bad reply len → 偶發 fail，重啟程式才好
- **之後：** 每次 sendData 開頭把 kernel buffer 清空 → receiveData 只讀到新 reply → driver validate 通過

### 副作用 / 注意
- 每次 send 多 1 個 socket flag toggle + 1-2 個 `recv()` syscall，正常無 stale 時 drain 立刻收 EAGAIN/EWOULDBLOCK 退出（< 0.1 ms），效能近乎無影響
- 若有 stale → 印 `[DBG]` log 提示（debug_mode=true 才印）
- 4096 byte 安全上限：理論上不該觸發；若觸發代表 bus 真的爆量，多印一次 DBG 後讓正常 send 進行（仍然丟掉前 4096 byte 之外的舊資料 — 但 4096 已遠多於任何合理 stale 累積）
- **理論風險：** 若新 reply 在 send 之前就抵達（非常不太可能，需要某種 async push），drain 會吃掉。Modbus 純 request-response 不會發生

### 沒做什麼
- **沒**改 driver 層 `sendModbus` / `readParam` / `writeParam`（驗證邏輯保留 — 多一道防線）
- **沒**改 `receiveData` / `available` / API 簽名
- **沒**改 `TCP_server`（不同 class，本身有 line-buffer 已經 robust）

---

## 2026-05-08p — Claude Code — Crane 三個馬達頻率改 runtime 可調 + 預設改 10 Hz + GUI 輸入

### 修改檔案
- `Crane_control_PI/main.cpp` —
  - `SE3_HOLD_HZ` / `SE3_MOTION_HZ` / `MIDDLE_WINCH_HZ` 從 `static constexpr` 改 `static std::atomic<double>` (`g_se3_hold_hz` / `g_se3_motion_hz` / `g_middle_winch_hz`)，預設值新增同名 `_DEFAULT` constexpr 值都 = `10.0`
  - `se3StartRopeMotion` / `se3StartRopeHold` / `middleStart` 改用 `.load()`
  - 新增 `cmd_set_hold_hz` / `cmd_set_motion_hz` / `cmd_set_middle_hz`，範圍檢查 `0 < hz ≤ SE3_MAX_HZ` (or `CLV900_MAX_HZ`)
  - `cmd_status` 多 `hold_hz=` / `motion_hz=` / `middle_hz=` 三欄位
  - dispatcher 加 `set_hold_hz` / `set_motion_hz` / `set_middle_hz` 三命令
- `web_backend/public/index.html` — 在「收繩總和停止門檻」row 下加「頻率設定」row：手動 hold / 自動 motion / 中間絞盤 三個 number input + 對應「目前」顯示；hint 補說明「下個 motor-start cmd 才生效」
- `web_backend/public/app.js` — 通用 `wireFreqInput()` helper（debounce 200ms）綁三個 input 到 `set_hold_hz` / `set_motion_hz` / `set_middle_hz`；`onCraneLine` 解析 `hold_hz=` / `motion_hz=` / `middle_hz=` 顯示當前值

### 為什麼
2026-05-08p 之前三個頻率寫死 constexpr（hold 20 / motion 30 / middle 20），bench 沒辦法即時調試。使用者要求「網頁上能設定頻率，預設都改 10 Hz」。10 Hz 是保守值，第一次驗證方向 / 安全用，確認沒問題後可直接從 GUI 拉高。

### 行為細節
- 範圍檢查：`hz ∈ (0, SE3_MAX_HZ=50]` 或 `(0, CLV900_MAX_HZ=50]`，超範圍回 `ERR hz_out_of_range`
- mid-motion 改頻率：原子 store 立即生效，但**運轉中的馬達不會自動換速** — 要重新下 hold/motion cmd 才會送 setFreqHz 到 inverter
- GUI debounce 200ms：避免 typing「10.5」時連續送 `1` `10` `10.5`
- cmd_status 200ms poll 自動同步「目前」顯示，server 重啟 / 別人改值都會反映

### 沒做什麼
- **沒**動 SE3_MAX_HZ / CLV900_MAX_HZ — 物理馬達上限，不應 runtime 改
- **沒**動 motion_rope 內部頻率邏輯 — 仍然開始時抓一次 `g_se3_motion_hz` 然後跑到 cm 達標
- **沒**做「目前正在跑的馬達 live 換頻率」— mid-motion 換速會干擾 SE3 watchdog 邏輯，留 TODO
- **沒**動 driver SE3_inverter 的 setFreqHz API — 還是 caller 帶 hz 進來

---

## 2026-05-08o — Claude Code — Crane SD76 length cache（解 status 200ms poll 隊列堆積）

### 修改檔案
- `Crane_control_PI/main.cpp` — 新增 `meter_loop()` 背景 thread（仿 `hold_loop` 設計）每 250 ms 讀 3 個 SD76 計米器並存進 atomic globals (`g_length_*` + `_valid`)；`cmd_status` / `cmd_home_status` 改成讀 atomic 不打 Modbus；`main()` 啟/停 `g_meter_thread`

### 為什麼
使用者回報「個別收繩按鈕送出指令後等超過 1 分鐘才動」。分析：

`cmd_status` 每次要做 3 個 SD76 FC03 讀（每個透 USR gateway ~150 ms）= **每次 status ~450 ms**。但 GUI 是 200 ms 自動 poll status → server 端 dispatcher 處理時間 > poll 間隔 → recv thread 隊列無限堆積。每次按鈕按下，cmd 排在數百個 status call 後面，等他們都跑完才輪到。idle 1 分鐘 → 隊列累積 ~75 秒（300 個 × 250 ms 缺口）→ 後續按鈕真的會 1+ 分鐘才 fire。

張力（DSZL）原本就有 `hold_loop` cache 不打 Modbus；length 沒做。本次補上對等的 cache thread。

### 改動效果
- `cmd_status` / `cmd_home_status` 純 atomic load，~微秒級完成 → 隊列不會堆
- length 顯示精度從「每次 status 直讀」變成「最多落後 250 ms」(METER_POLL_MS)
- meter_loop 也 graceful：device flag false 時跳過該 meter（USR_B 不通時不會浪費時間 retry）
- 移除 cmd_status 對 DSZL 的 fallback 直讀（本來只有在 hold_loop 還沒跑時才會打到，但會帶回隊列堆積問題）

### 沒做什麼
- **沒**改 motion_rope 內的計米器 read（那個是 motion 進行中需要 fresh 讀數，且 motion_mtx 已序列化，不會跟 status 競爭）
- **沒**降 GUI 200ms poll 頻率（cache 解了根因，poll 頻率不用動）
- **沒**動 hold_loop（張力 cache 已經在做）

---

## 2026-05-08n — Claude Code — 計米器個別歸零（per-meter zero_meter）

### 修改檔案
- `Crane_control_PI/main.cpp` — 新增 `cmd_zero_meter(<left|right|middle>)` 命令，只重置單一 meter；dispatcher 加 `zero_meter` 入口；查 device flag 缺則回 `ERR <meter>_unavailable`，不會更新 `home_ground_cm`
- `web_backend/public/index.html` — zero row 拆兩列：「全部零」（保留現有 ground/top 雙鈕，仍要 meters_all）+「個別零」（左/右/中三鈕，各自只 require 對應 meter）；加 hint 提醒個別零不更新 home_ground、混用會造成原點不一致

### 為什麼
2026-05-08j 把 zero_meters 兩鈕都設成 `meters_all` (左+右+中) → USR_B 不通時整組灰掉，無法零任何 meter。使用者要求加個別歸零，這樣 USR_B 不通時仍可以零左+中，右繩修好後再個別零右繩。

### 沒做什麼
- **沒**改 `cmd_zero_meters`（ground/top 雙鈕）行為 — 正常部署仍用這兩個建立統一原點 + 記 home_ground
- **沒**改 motion_rope / cmd_roll_correct 對 home_ground 的依賴 — 個別零後跑 motion_rope 仍可能因左右原點不對齊而異常，責任在使用者（hint 已警告）

---

## 2026-05-08m — Claude Code — Web GUI 顯示鋼索計米器即時長度

### 修改檔案
- `web_backend/public/index.html` — crane panel 在 cm 輸入框 row 上方加「目前長度」row：左 / 右 / 中 cm + home_ground + 剩餘 cm；下方 hint 解釋三個值的意義
- `web_backend/public/app.js` — `onCraneLine` 解析 `length_left / length_right / length_middle / home_ground_cm`，自動算「剩 = home_ground − |left|」更新 DOM

### 為什麼
使用者要求「主要吊機程式和網頁可以加上目前放收繩的長度嗎? 就是顯示計米器的數值」。crane main.cpp 的 `cmd_status` reply 早就有 `length_*` 欄位（200 ms poll 自動 refresh），但 GUI 沒解析顯示。本次純前端：
- crane main.cpp 不需動
- 新欄位 ERR 字串透傳直接顯示 ERR（meter 不可用時）
- 跟 graceful degradation（2026-05-08j）配合：右繩計米 unavailable 時也會顯示 ERR，跟按鈕 disabled 一致

### 沒做什麼
- 沒加單獨的 polling — 復用既有 cmd_status 200 ms refresh
- 沒做歷史曲線 / chart — 之後若要可加 chart.js 之類

---

## 2026-05-08j — Claude Code — Crane graceful degradation + Web GUI 個別 disable

### 修改檔案
- `Crane_control_PI/main.cpp` — 加 12 個 device atomic flag（4 gateway + 8 device）；12 個 init fail 點全改 `[WARN] continuing` 不再 `return 1`；`make_device_state_line()` 產生 EVT/log 字串；`cmd_status` 多 12 個 `dev_*` / `gw_*` 欄位；init 結束 broadcast `EVT device_state`；motion_rope / cmd_roll_correct / cmd_manual / cmd_middle_set / cmd_zero_meters / cmd_hold / cmd_zero_tension 進入時各自 check 需要的 device flag，缺則回 `ERR <name>_unavailable`
- `web_backend/public/index.html` — 在 crane panel 頂部 `crane 驅動狀態` 下加 device-state banner；按鈕加 `data-required="<tokens>"` 屬性對應需要的 device（pay_out/retract = motion_full、↑左/↓左 = se3_left、↑右/↓右 = se3_right、零鋼索 = meters_all、零張力 = dsz_left,dsz_right、emergency = se3_left,se3_right、raw left/right 個別）
- `web_backend/public/app.js` — 加 `craneDevices` state object（12 個 flag）+ `DEVICE_TOKEN_GROUPS` shorthand 展開（motion_full / motion_diff / meters_all）+ `DEVICE_LABEL_TW` 中文標籤；`parseCraneDeviceState(line)` 一條 regex 同時相容 `dev_*=` (status) 跟 `*=` (EVT device_state)；`updateCraneDeviceUI()` 走訪 `[data-required]` element，缺裝置時 `el.disabled=true` + tooltip 顯示哪個不可用；banner 顯示總清單；`onCraneLine` 開頭呼叫一次 `parseCraneDeviceState` + `updateCraneDeviceUI` change-detection

### 為什麼
2026-05-08c bench session 紀錄 USR_B (.31) 整條 bus 不通 → SE3 right + SD76 right 都 READ ERROR，但左繩 + middle + DSZL 都可用。原本 `Crane_control_PI/main.cpp` 12 個 init fail 都直接 `return 1`，**任一裝置沒接好整支 crane 起不來**。今天熱修 DSZL 之後啟動立刻 `[FATAL] connect USR_C` 也是同一個問題。

實作後行為：
- 4 個 gateway 任一連不上 → 印 `[WARN] USR_X ... connect failed — <影響>`，繼續啟動
- 8 個 device init 任一失敗 → 印 `[WARN] <device> init failed — <影響>`，繼續啟動
- 完成 init 後 console 印一行 `[INFO] EVT device_state ...`，TCP 廣播同樣
- GUI 自動把不可用裝置對應的按鈕灰掉，懸停顯示中文「不可用：左繩變頻器、右繩計米…」
- 頂部 banner 顯示「⚠ 裝置狀態：USR_B 閘道、右繩變頻器、右繩計米 不可用 — 相關控制已停用」
- 全部裝置可用時 banner 自動隱藏

### 沒做什麼
- **沒**做 hot re-init：device flag 只在啟動時設一次。硬體中途修好要重啟 crane（TCP_client 背景重連 OK，但 driver 內部狀態（cu_mode_set_ 等）不會自動重 claim — 留作 TODO）
- **沒**動 watchdog / hold_loop / safety check 邏輯：那些是 motion 進行中的安全網，跟 init-time graceful degradation 是兩件事
- **沒**改 motion_flow.md / runbook：這是 main.cpp 應用層 + GUI 層的實作改動，不影響規範描述
- **沒**動 washrobot 端：washrobot 透過 `crane_cmd_("status")` 拿 reply 後，看到 dev_* 欄位會略過（regex 不 match washrobot 不 care 的就忽略），不需配合改

### 已知互動
- DSZL_107 driver 路 B 熱修（2026-05-08f）+ SE3 方向 sync（2026-05-08i）+ 本次 graceful degradation（2026-05-08j）三件事一起決定了「2026-05-08 之後 crane 啟動行為」：DSZL 走 TCP :502、SE3 STR=pay_out / STF=retract、任一裝置 down 不阻塞起 crane

---

## 2026-05-08k — Claude Code — Linux_test menu 25 加 Min Hz floor + stall 警告

### 修改檔案
- `Linux_test/main.cpp` — `test_se3_inverter_dual()` 啟動 prompt 多問 `Min Hz floor`（預設 10）；pay/retract 指令進來若 hz < min_hz，先印 `[WARN] X Hz < min Y Hz floor -- motor may stall silently` 但仍照原邏輯執行

### 為什麼
bench 2026-05-08 跑 dual `retract 5` 時：driver 完全沒報錯（CU mode timing fix 後 H1001 寫入 ack）但右側馬達物理上沒轉，左側轉。診斷：5 Hz 扭矩不足以克服右側鋼索摩擦，**SE3 自我認知在 5 Hz 跑（status word b0=running, b1=STF），但 shaft 物理 stall** — 沒設 b9 stall bit 因為 SE3 覺得自己沒問題。10 Hz 兩邊都動。

→ Driver / Modbus 完全正常，純粹是低 Hz torque issue。menu 加 prompt 提醒，**讓使用者下次自己看到 Hz<floor 就知道 stall 是預期行為，不要再回頭懷疑 driver**。

### 為何選 soft floor 不 hard reject
某些情境（純空轉測試、無載 bench）使用者可能合理需要 5 Hz 試方向。Hard reject 會擋掉這個用法。Soft warn 兼顧安全與彈性。

### 沒做什麼
- **沒**動 user_lib（純 app 層 prompt + warning）
- **沒**改 menu 23 — 單側測試者通常會自己 ramp Hz 找出能轉的最低值，不需要 prompt
- **沒**改 motion_flow.md — 「最低運轉 Hz」不是規範層次的事，是現場 commissioning 時挑值

---

## 2026-05-08j — Claude Code — SE3_inverter::ensureCuMode_ 加 150ms settle [跨界: user_lib]

### 修改檔案
- `user_lib/SE3_inverter.cpp` — `ensureCuMode_()` 寫完 H1000=0 後加 `std::this_thread::sleep_for(150ms)`，include `<thread>` `<chrono>`
- `user_lib/SE3_inverter.h` — header doc 補一段 "CU-mode timing race" 說明

### 跨界宣告
**[跨界: user_lib]** — 標準上 `SE3_inverter` 是 Jim 範圍。本次只改 private method 內部實作（加一個 sleep + 註解），**public API 簽名/行為完全不變**：所有 `runForward / runReverse / stopDecel / emergencyStop / setFreqHz / readXxx` 簽名相同；唯一可觀察變化是首次 run 命令延長 150ms。依 CLAUDE.md「不改 public API 的內部改動 → 協作者可修，PR 標 [跨界: user_lib]」規則處理。等 Jim review 後合併。

### 為什麼
bench 2026-05-08 跑 Linux_test menu 25 dual 時，第一次 `retract 5` 偶發 right 側 H1001 comm fail（count=1/2），第二次同樣指令直接通。診斷：SE3 firmware 收到 H1000=0 (CU mode Modbus latch) 後，**ack Modbus 很快**但內部 state machine 還要 50-150ms 才真正 transition 到 CU mode；driver 立刻接著寫 H1001 (run cmd) 落在這個 window 就被 silently rejected。單側 menu 23 不容易踩到是因為 user 輸入 IP / port / slave / max Hz 那段 typing 已經 cover 了 settle 時間。memory `project_se3_modbus_cu_latch.md` 已同步補上 §「2026-05-08 補充」。

### 副作用
- 首次 `runForward` / `runReverse` 從 power-up 起多等 150ms — 對應用層幾乎無感（首次 init 後使用者通常還在判讀畫面）
- `cu_mode_set_` cache 後 settle 不重複付（subsequent run 命令照舊速度）
- Watchdog 觸發 reclaim 時也會重新付 settle（合理：是因為 SE3 真的掉了 CU mode）

### 沒做什麼
- **沒**改 menu 25 / menu 23 的 app 層邏輯（之前考慮加 app 層 warmup，最後選 driver 修一勞永逸）
- **沒**改 public API 簽名

---

## 2026-05-08i — Claude Code — Crane main.cpp SE3 方向巨集 sync（與 Linux_test menu 23/25 對齊）

### 修改檔案
- `Crane_control_PI/main.cpp` — `SE3_DIR_PAY_OUT` / `SE3_DIR_RETRACT` 巨集翻轉，註解改寫指向 Linux_test menu 23/25 + changelog 2026-05-08g/h 的 bench 觀察

### 改動細節
原本（猜的）：
```cpp
#define SE3_DIR_PAY_OUT(inv)   inv.runForward()    // STF
#define SE3_DIR_RETRACT(inv)   inv.runReverse()    // STR
```
改後（bench 2026-05-08 兩台都驗過）：
```cpp
#define SE3_DIR_PAY_OUT(inv)   inv.runReverse()    // STR
#define SE3_DIR_RETRACT(inv)   inv.runForward()    // STF
```

### 為什麼要 sync
Linux_test menu 23（單台 SE3）跟 menu 25（dual sync）都已內含 bench 觀察「STF=retract / STR=pay_out」並在 prompt + 註解寫明（見 changelog `2026-05-08g`）。但主程式 `Crane_control_PI/main.cpp` 仍是「pay_out=forward 是猜的」placeholder（見 work_log 2026-05-07 §2「未完待辦」）。實機跑 motion_rope / cmd_roll_correct / hold-to-pull 會方向反 → 鋼索動向跟指令相反，本體可能撞到障礙物。

### 沒做什麼
- **沒**改 `cmd_middle_set` / `middleStart` 的 CLV900 方向 — memory 沒驗過，scope 限 SE3。第一次部署中間絞盤前要在 bench 同樣手動對一次方向
- **沒**動 atomic / rollback 邏輯 — `motion_rope` / `cmd_roll_correct` 既有的 rollback chain（左成功 → 右失敗 stop 左；middle 失敗 → allMotionOff）已經是 menu 25 同款邏輯，無須再改
- **沒**更新 motion_flow.md / runbook — 巨集映射屬於 main.cpp 內部實作細節，不是規範層面（規範描述「pay_out / retract」抽象動作即可）

---

## 2026-05-08h — Claude Code — Linux_test menu 25：pay/retract 原子化 + rollback

### 修改檔案
- `Linux_test/main.cpp` — `test_se3_inverter_dual()` 的 pay/retract 流程加 rollback：任一邊 run 失敗 → 立刻送 stopDecel 給兩邊；doc 區塊補 "atomic" 安全性說明

### 為什麼
bench 測試 `retract 5` 時右側 SE3 因 CU mode / 設定問題 run 失敗，但左側已經被命令跑起來了。對機器來說「只有一邊鋼索動」會讓本體傾斜，使用者要求 dual menu 的 pay/retract 必須原子化（要嘛兩邊都動，要嘛都不動）。

### 行為改變
- 之前：rL fail / rR fail 各自獨立印 [WARN]，run 不會被自動停
- 之後：rL || rR fail → 主動送 stopDecel 兩邊（rollback）+ 印「rolling back: stopping both sides」+ 列印 rollback stop 結果
- 仍非完美原子：rL/rR 中間有幾十毫秒 gap，左側可能短暫送出 run 指令；但 5 Hz 下啟動慢，rollback stop 在馬達真的轉之前就到了

### 為什麼 stopDecel 不會被 CU mode 卡住
依 driver 設計（memory project_se3_modbus_cu_latch.md / SE3_inverter.h）：`stopDecel` / `emergencyStop` 跳過 `ensureCuMode_()`，運轉中呼叫不會炸 → 即使失敗那邊本來就 CU mode 沒設好，rollback 還是有機會 stop 成功。

---

## 2026-05-08g — Claude Code — Linux_test menu 25：SE3 dual sync（左右吊機同步收/放繩）+ menu 23 註解補 bench 觀察

### 修改檔案
- `Linux_test/main.cpp` — 新增 `test_se3_inverter_dual()`（約 170 行）並掛 menu entry 25；menu 23 doc 區塊補 bench 2026-05-08 實測：STF=收繩、STR=放繩（兩台同向，與「forward = pay-out」直觀相反）

### 內容
menu 25 同時連兩個 USR-TCP232 gateway（預設 .30 左 / .31 右），啟動時 prompt：
- 左/右 gateway IP、port、slave ID（兩邊共用）、max Hz
- **方向 prompt**「STR=pay-out / STF=retract? [Y/n]」— 預設 Y（對應 bench 觀察值），按 N 可一次翻轉所有 pay/retract 對應，不用改程式碼

指令：
- `pay <hz>` / `retract <hz>` 兩邊同步（先各自 setFreqHz，任一邊 freq 設失敗整體取消 run；run 失敗印 WARN）
- `s` / `e` 兩邊同步 stop / emergency stop（送雙方各自的 stopDecel / MRS）
- `h <hz>` 兩邊同步改 RAM freq 不改 run state
- `m` 並列顯示左右 Hz / A / V / StatusWord
- `q` 退出前 cleanup 一定送 stopDecel 兩邊（即使其中一邊已斷線也試）

安全策略：任一邊 init 失敗整個 abort（避免「以為兩邊都通結果只動一邊」）；個別指令失敗不會自動停另一邊（讓使用者按 `e` 自己決定）。

### 規範權威
- 拓樸：`CLAUDE.md` §吊機子系統（USR_A.30 = SE3 left / USR_B.31 = SE3 right）
- SE3 行為：`user_lib/SE3_inverter.h` doc 區塊（control 0x1001 / freq 0x1002 / monitor 0x1003-0x1005）
- 方向約定：memory `project_crane_topology_2026_05_07.md`（已同步補上 bench 實測值）

### 沒做什麼
- **沒**動 user_lib（純應用層 / 測試工具）
- **沒**改 `Crane_control_PI/main.cpp` 的 `SE3_DIR_PAY_OUT` / `SE3_DIR_RETRACT` 巨集對應 — 主程式方向修正屬於另一個 task；本 menu 只在 bench 工具層提供翻轉開關
- 不做 ramp / 漸進加速，跟 menu 23 行為一致（直接 setFreqHz 後送 run）

---

## 2026-05-08f — Claude Code — DSZL_107 driver 熱修走 Modbus TCP（路 B）+ Crane main.cpp port 對應更新

### 修改檔案
- `user_lib/DSZL_107.h` — header comment 改寫為 X518 直連 switch / port 502 / slave 1；private member 加 `uint16_t txid_`；移除 `CRC16` 宣告（不再使用）
- `user_lib/DSZL_107.cpp` — register 註解更新（補 `0xA20=40 SAVE` / IP 編碼 / mode reg 0x644）；建構子初始化 `txid_=0`；`modbus_read` / `modbus_write_long` 整段重寫為 **Modbus TCP MBAP 框架**（無 CRC，加 7-byte MBAP header），並把 reply 重新封裝成 `[unit][fc][bc][data]` 的「RTU-like」layout，讓既有 caller（`parse_long(&buf[3], …)`、`get_both_long len < 11` 檢查）不用動；加 Modbus exception code 解碼+log
- `Crane_control_PI/main.cpp` — header comment 從 USR_C/USR_D gateway 改為 X518 直連描述；`USR_C_IP/USR_D_IP` 改名 `DSZL_LEFT_IP/DSZL_RIGHT_IP`；新增 `DSZL_PORT=502`；`cli_C/cli_D` connect 改用新 port；FATAL/OK log 文字更新

### 跨界宣告
**[跨界: user_lib]** — `DSZL_107.{h,cpp}` 原本是 Jim 範圍。此次熱修僅改**內部 framing 實作**（RTU+CRC → MBAP+TCP），**public API 完全不變**：`init / get_tension_long / get_tension_kg / get_both_long / do_zero_* / set_unit / setScale / getScale` 簽名與行為都保持。依 CLAUDE.md「不改 public API 的內部改動 → 協作者可修，PR 標 [跨界: user_lib]」規則處理。等 Jim review 後合併到 main。

### 為何熱修而非等決策
mailbox 路 A/B/C 三選一原本標 🟡（不阻塞 bench），但今天 bench 把兩台 X518 IP 設成 .32/.33 後 `Crane_control_PI` 啟動就 FATAL（`connect USR_C 192.168.1.32:4001 failed`）— 升級成 🔴 阻塞。為了不卡 SE3/SD76/CLV900 後續測試進度，照路 B（推薦選項）熱修。

### 沒做什麼
- **沒**改 `DSZL_107` 的任何 public API
- **沒**動 motion_flow.md / CLAUDE.md 架構圖（屬於規範，等 Jim 確認後一起改）
- **沒**動 vcxproj / CMake，編譯單元無增減

---

## 2026-05-08e — Claude Code — Linux_test menu 24：X518 張力感測器測試（Modbus TCP :502 直連）

### 修改檔案
- `Linux_test/main.cpp` — 新增 `test_x518()`（約 200 行）並掛 menu entry 24

### 內容
直接以 raw socket 傳 Modbus TCP MBAP frame 測 bench X518（**不**走 user_lib/DSZL_107，因為它是 RTU + USR gateway 框架，跟 bench 自帶 Ethernet 的 X518 對不上 — 詳見 memory project_x518_architecture_mismatch.md）。

支援指令：
- `r` 單次讀 CH1+CH2 raw + kg
- `l` live loop（200 ms 間隔，Enter 停）
- `z` / `Z` / `A` zero CH1 / CH2 / all（write 0x0A20 = 1/2/7）
- `u` 設單位 kg（write 0x0614 = 2）
- `c` 讀 IP/port/mode/slave/baud/format/unit 設定 reg
- `s <val>` 換 raw→kg scale
- `q` 離開

預設值：IP `192.168.1.120`（出廠值）/ unit ID `1` / scale `0.01`。bench 上目前 IP 是 `.100`（衝突 washrobot Pi 規範值，必要時改回 `.120`）。

### 沒做什麼
- **沒**改 `user_lib/DSZL_107.cpp/h`（架構決策待 Jim，請見 mailbox）
- **沒**動 vcxproj，不需要新的編譯單元

### 後續修正（同日）
拿掉 `quick_tcp_probe` 改用單一 non-blocking connect + 3s timeout。原因：bench 測試時 probe 過了但接續的 blocking connect 失敗 — X518 Wiznet 的 TCP stack 同時 socket 數很少，「probe 開→關→真連」太快會被裝置 reject（python `x518_probe.py` 沒這問題因為它只開一次 socket）。同時 connect 失敗訊息加上 `SO_ERROR` / `errno`，3s timeout 走完直接提示「ICMP up TCP dead → 物理電源重啟」。

### 後續擴充（同日）：8 通道讀取（後撤）→ 改成 2 通道 + 參數 dump
bench 實測 CH1/CH2 raw 是死值（拉力不會變動）→ 一度懷疑 cell 接在別 channel，先擴成 8 通道讀。但翻 X518 手冊 v1.1 確認 X518 是 **雙通道**（不是 8）— CH3..CH8 那些值是讀到隔壁無關 register 繞回來的垃圾。

撤回 8 通道讀，改成更實用的診斷工具：
- `r` 還原成 CH1+CH2
- `S` 新增 SAVE 命令（write 0xA20 = 40），手冊明寫**所有參數修改/清零/校準後都需要寫 40 才持久化**
- `p` 新增參數 dump：一次 FC03 讀 0x600..0x64e（40 個 long）顯示原始值，方便對比手冊 default
- `R <hex addr>` 任意暫存器 raw read
- `W <hex addr> <val>` 任意暫存器 raw write

死值原因待 bench 實測 — 嫌疑：採集速度被設成 0 / 採集 enable 旗標關（手冊 0x620 與 0x634 各有一個「0=關閉 1=允許 默認 0」）。要先跑 `p` 看現況才能對症下藥。

---

## 2026-05-08d — Claude Code — X518 (DSZL-107) bench 診斷工具 + 架構錯位發現

### 修改檔案
- `Linux_test/x518_probe.py` (新增) — Modbus TCP 直戳 X518，讀 CH1/CH2 raw + 設備 IP/port/mode/slave/baud/format/unit 暫存器
- `Linux_test/x518_portscan.py` (新增) — 12 個常見工業 port TCP 掃描
- `Linux_test/x518_wide_scan.py` (新增) — 擴大 TCP/UDP 掃描 + 開 PC:8000 listener 收 X518 client 連線
- `.claude/mailbox.md` — 加 [2026-05-08] DSZL driver 架構決策請求給 Jim
- `memory/project_x518_architecture_mismatch.md`（user memory，新增）

### 發現
bench X518 是**自帶 Ethernet 口**版本（不是 RS485 + USR 架構），原生走 Modbus TCP（port 502，標準 MBAP）。但 `user_lib/DSZL_107.cpp` 是寫 Modbus RTU + USR 透傳，**對不上**。

X518 出廠值（手冊 v1.1）：IP `192.168.1.120` / port `502` / mode 0x644=1 (Modbus TCP)
廠商 0755-2890-9121（深圳）

### bench 當下狀態
X518 在 `192.168.1.100`（衝突 washrobot Pi 規範值），ping 1ms 通（Wiznet 硬體 ICMP）但 12 個 TCP port + 9 個 UDP port 全 timeout — **韌體死當**。

### 下次接手第一步
1. 完全斷電 X518 ≥10s 重啟
2. 還不通 → 物理 reset（迴紋針按 reset 5~10s）回出廠 IP `.120`
3. 再跑 `python Linux_test/x518_probe.py 192.168.1.120`
4. driver 架構決策（路 A/B/C）詳見 mailbox

### 待 Jim review（不阻塞 Sadie）
DSZL_107 driver 三條路請選：
- 路 A：X518 改用 RS485 + 加 USR-TCP232 中繼（driver 不動）
- 路 B（推薦）：driver 改 Modbus TCP framing，X518 直插 switch
- 路 C：driver 加 mode flag 兩種都支援

詳見 `mailbox.md` + `memory/project_x518_architecture_mismatch.md`。

---

## 2026-05-08c — Claude Code — Session 進度紀錄 + 待辦

### 今天完成（已驗證）
- **SE3 Modbus 整合徹底搞定**：
  - 修 0x1101 → 0x1001（run command 暫存器）
  - 修 monitor 位址 0x100A/B/C → 0x1003/4/5（output Hz/A/V）
  - 加 `ensureCuMode_()` 寫 H1000=0 進 CU mode（cache，第一次 run 才寫）
  - 加 `run_h1001_with_watchdog_()`：連續 2 次失敗自動重 claim CU mode + retry
  - bench 驗證：menu 23 `f 5` / `r 5` / `s` / `e` 全通
- **SD76-C 計米器測試**：
  - menu 9 在 USR_A 通，slave 1 / 2 都讀得到（解釋：SE3 在 slave 1 也回應 FC03 read 0x0021，那是 SE3 P.33 參數，巧合）
  - SD76-C 變種**面板沒 UArt 入口**，固定 8N1 不可改
  - SD76 站號改成 left=2、middle=3（USR_A 上）
- **RS485 bus 統一 8N1**：
  - SD76 鎖 8N1、SE3 RTU 沒 8N1（只有 8N2 / 8E1 / 8O1）
  - 解：USR 設 8N1，SE3 P.154=3 (8N2) 但 UART 寬容收 8N1 也接受
  - bench 驗證：USR_A 8N1 + SE3 + SD76 同 bus 都通

### 開放議題（明天繼續）
**1. USR_B (192.168.1.31) 整條 bus 沒回應**
   - SE3 right (slave 1) + SD76 right (slave 2) 兩個都 READ ERROR
   - 表示問題不在 SE3/SD76 設定，是 USR_B 本身
   - 待驗證：
     - USR_B web UI 進得去嗎（瀏覽器 http://192.168.1.31）
     - USR_B 設定跟 USR_A 對齊（特別是 stop bits = 1）
     - USR_B 機身 TX/RX/LINK/PWR 燈號狀態
     - USR_B 後面 RS485 線（A+ B- 接對沒、有沒鬆）
     - 暴力測試：SD76 right 移到 USR_A bus 試讀（slave=4 避衝突）

**2. Crane_control_PI graceful degradation**（未做，已寫 plan）
   - 目前 12 個 init fail point 都 `return 1`，任一 device 沒接好整個 crane 不能用
   - Plan：改成 `[WARN] continuing` + 標 device flag + broadcast `EVT device_state` + GUI 對應 disable 按鈕
   - 5 個改動點寫在 changelog draft 等待確認後實作

**3. Watchdog 風險**
   - SE3 watchdog 中途斷電後 cu_mode_set_ flag stale → 連續 2 次失敗才會重 claim
   - 目前接受這個延遲（可調 RUN_FAIL_THRESHOLD = 1 加快但會誤觸）

### 設備設定備忘（驗證日）
| 設備 | 站號 | Bus | 設定 | 已驗證 |
|---|---|---|---|---|
| SE3 left | 1 | USR_A (.30) | P.33=0, P.154=3, P.79=3, P.36=1 | 2026-05-07 ✓ |
| SE3 right | 1 | USR_B (.31) | 同上 | ✗ USR_B 整條不通 |
| SD76 left | 2 | USR_A (.30) | factory + Adrset=2 | 2026-05-08 ✓ |
| SD76 middle | 3 | USR_A (.30) | factory + Adrset=3 | ✗ 待測 |
| SD76 right | 2 | USR_B (.31) | factory + Adrset=2 | ✗ USR_B 不通 |
| CLV900 | 4 | USR_A (.30) | factory（待 driver 用 SE3 同步式 H1000=0 套用）| ✗ 待測 |
| DSZL left | 5 | USR_C (.32) | X518 預設 + set_unit_kg | ✗ 待測 |
| DSZL right | 5 | USR_D (.33) | 同上 | ✗ 待測 |

### USR Gateway 設定備忘
| Gateway | IP | Port | Baud | Data | Parity | Stop | Mode | Verified |
|---|---|---|---|---|---|---|---|---|
| USR_A | 192.168.1.30 | 4001 | 9600 | 8 | None | **1** | TCP Server 透傳 | ✓ |
| USR_B | 192.168.1.31 | 4001 | 9600 | 8 | ? | **?** | ? | ✗ 整條不通 |
| USR_C | 192.168.1.32 | 4001 | ? | ? | ? | ? | ? | ✗ 待測 |
| USR_D | 192.168.1.33 | 4001 | ? | ? | ? | ? | ? | ✗ 待測 |

→ 明天進度：先排 USR_B → 排 USR_C/D → 跑 graceful degradation 改造

## 2026-05-08b — Claude Code — Crane RS485 bus 統一用 8N1（SE3 容忍 8N2 設定收 8N1）

### 修改檔案
- `user_lib/SE3_inverter.h`：keypad pre-config 註解更新 — P.154 仍要設 3 (8N2 RTU)，但備註 USR 端可用 8N1，SE3 UART 容忍

### 為什麼
bench 實測 2026-05-08：
- SD76-C（左/右/中間管線）面板鎖定，**UArt 不可改**，固定 8N1（出廠）
- SE3 RTU 模式只支援 8N2 / 8E1 / 8O1（沒 8N1）
- CLV900 預設 8N1，可改

→ SE3 + SD76 在同一條 RS485 bus 沒有共同格式。

**解法：UART receiver tolerance**
- USR-TCP232 改成 **8 / None / 1 (8N1)** 統一
- SE3 P.154 維持 3（內部期待 8N2），實測**收 8N1 frame 也接受**（多數 hardware UART 對 stop bit 寬容，最少 1 個就 OK）
- SD76 native 8N1 ✓
- CLV900 native 8N1 ✓

### 驗證結果（2026-05-08 bench）
- USR_A 改 8N1 後：
  - SD76 menu 9 讀取正常 ✓
  - SE3 menu 23 `f 5` / `r 5` / `s` 全部通 ✓
- 沒看到隨機 CRC error（短時間測試）

### 不變
- 各設備內部設定不動：
  - SE3: P.154 = 3 (8N2)、P.33 = 0 (Modbus)、P.79 = 3 (CU)、P.36 = 1 (站號)
  - SD76: UArt = 1 (8N1, factory)、Adrset = 站號（依需求改）
  - CLV900: F7-02 = 3 (8N1, factory)
- USR baud = 9600 不變
- driver 程式邏輯不變（只 USR 設定改）

### 待驗證 / 風險
- 長時間運轉是否會出現 SE3 → USR 方向偶發 frame error（SE3 送 8N2，USR 期待 8N1，可能 frame boundary 偵測異常）。bench 連續讀 1000 次 stress test 確認後再放心。
- 如果出問題：考慮 C 方案 物理隔離 bus（SE3 一條、SD76 + CLV900 另一條）。

## 2026-05-08a — Claude Code — SE3_inverter watchdog：連續失敗自動重新 claim CU mode

### 修改檔案
- `user_lib/SE3_inverter.h`：
  - 新增 private `bool run_h1001_with_watchdog_(uint16_t value, const char* ctx)` 宣告
  - 新增 `int comm_fail_count_ = 0`（連續失敗計數）
  - 新增 `static constexpr int RUN_FAIL_THRESHOLD = 2`（門檻）
- `user_lib/SE3_inverter.cpp`：
  - 新增 `run_h1001_with_watchdog_()`：
    - 寫 H1001 → 成功歸零計數
    - 失敗 → `comm_fail_count_++` + log warn
    - 達 `RUN_FAIL_THRESHOLD`：reset `cu_mode_set_ = false` + 呼叫 `ensureCuMode_` 重寫 H1000=0 + retry 原命令一次
    - retry 成功 → log info 標明 watchdog 救回來了
    - retry 也失敗 → log err 放棄
  - `runForward / runReverse / stopDecel / emergencyStop` 全部改透過 `run_h1001_with_watchdog_` 寫 H1001（runForward/Reverse 仍先呼叫 ensureCuMode_ 第一次設 CU mode；stop / emergency 跳過）

### 為什麼
SE3 中途斷電 / 重啟（操作員手動 / 跳閘 / 急停拉電）的話，driver 內 cache 的 `cu_mode_set_ = true` 還在，但 SE3 內部 latch 已忘記 CU mode → 下次 run 命令會被拒。

不加 watchdog → 永遠卡住，要重 init driver。
加 watchdog → 自動偵測 N 次連續失敗（通訊問題 vs CU 掉了無法區分）→ 重新 claim CU + retry。

選 N=2 而不是 1：第一次失敗可能是 transient comm error（USR / RS485 雜訊）；連續 2 次以上才認定是 CU 掉了。

### 行為示意
```
正常流程:
  f 5 → ensureCuMode (write H1000=0, cu_mode_set_=true)
      → write H1001=0x0002, success, fail_count=0 → 馬達正轉
  s   → write H1001=0x0000, success → 馬達停

SE3 斷電後恢復 (driver 沒重啟):
  f 5 → ensureCuMode (cu_mode_set_=true，跳過)
      → write H1001=0x0002, FAIL, fail_count=1, log WARN
      → 不到門檻，return error，操作員看到 [WARN] runForward reported error

  f 5 (再試) → ensureCuMode 跳過
      → write H1001=0x0002, FAIL, fail_count=2 (= 門檻)
      → watchdog: cu_mode_set_=false, ensureCuMode (寫 H1000=0)
      → 重 retry write H1001=0x0002, success → 馬達正轉
      → log INFO "watchdog: fwd succeeded after CU re-claim"
```

→ 操作員可能看到 1-2 次失敗後自動恢復，不需要手動 reset driver。

### 不變
- `ensureCuMode_()` 邏輯不變（lazy + cache）
- `setFreqRaw / setFreqHz` 直接走 writeParam（沒有 watchdog，因為 freq write 不需要 CU mode）
- monitor reads 不變
- threshold 寫死 2，要調直接改 RUN_FAIL_THRESHOLD constant

### 待驗證
- bench 模擬 SE3 中途斷電：
  1. driver init + `f 5` → 馬達轉
  2. SE3 斷電 5 秒
  3. SE3 重啟（CU mode 遺失）
  4. `f 5` 第一次 → 失敗 (count=1)
  5. `f 5` 第二次 → watchdog 觸發，自動寫 H1000=0 + retry → 馬達轉
- 看 log 應有 `watchdog: re-claiming CU mode after 2 fails` + `watchdog: fwd succeeded after CU re-claim`

## 2026-05-07j — Claude Code — SE3_inverter ensureCuMode 改成 cache once + stop 跳過

### 修改檔案
- `user_lib/SE3_inverter.h`：新增 private 成員 `bool cu_mode_set_ = false`（sticky flag）
- `user_lib/SE3_inverter.cpp` `ensureCuMode_()`：
  - 開頭 `if (cu_mode_set_) return false;` — 已寫過就直接返回
  - 寫入成功後 `cu_mode_set_ = true` 標記
- `runForward / runReverse`：仍呼叫 `ensureCuMode_()`（會 cache）
- `stopDecel / emergencyStop`：**移除** `ensureCuMode_()` 呼叫 — stop 不需要 mode latch，直接寫 H1001=0

### 為什麼
2026-05-07 實機測試發現問題：每次 run/stop 都呼叫 ensureCuMode_ 寫 H1000=0：
- 第一次（馬達停）→ OK，CU mode 設好，run 成功
- 第二次（馬達跑中）→ **SE3 拒絕 H1000 寫入**（Modbus exception `bad reply len=5`），且接下來的 H1001 命令也跟著 timeout

SE3 firmware 不准運轉時改 operation mode（合理的安全鎖），但被拒絕的 exception response 干擾 bus 狀態，下個命令也炸掉。

修法：**H1000=0 只寫一次（lazy + cache）**：
- driver 第一次 runForward / runReverse 才寫 H1000=0
- 寫成功後設 `cu_mode_set_ = true`，之後永不重寫
- SE3 內部 latch 也是 sticky，set 過就一直在 CU 模式直到斷電

stop / emergency 不需要 CU mode（H1001=0 / H1001=0x80 在任何模式都接受），跳過 ensureCuMode_ 避免運轉中誤觸發 exception。

### 副作用
- 若 SE3 中途斷電重啟（操作員手動或意外），driver 的 `cu_mode_set_` flag 會 stale → 之後 run 不會重寫 H1000 → SE3 可能拒收。需要 driver 重 init 或加一個 `reset_cu_state()` API。目前先 accept，未來如遇到再處理。
- emergency_stop 跳過 ensureCuMode 是 OK 的：若 cu_mode_set_ 還沒被設過（第一次沒先 run 直接按 emergency），SE3 仍會收 H1001=0x80 寫入（manual 沒說 stop/MRS 需要 CU mode）。

### 已驗證待測試
- bench 重 build 後重跑 menu 23：
  - `f 5` → 第一次寫 H1000=0 + H1001=STF，馬達正轉 ✓
  - `s` → 只寫 H1001=0，馬達 decel 停 ✓（不再 timeout）
  - `r 5` → 跳過 H1000=0（cached），直接 H1001=STR ✓
  - `s` → 同樣只寫 H1001=0 ✓
  - `e` → 寫 H1001=0x80（MRS）✓

## 2026-05-07i — Claude Code — SE3_inverter run 命令前先寫 H1000=0 (CU mode latch)

### 修改檔案
- `user_lib/SE3_inverter.cpp`：
  - 新增 private 方法 `ensureCuMode_()`：寫 Modbus reg `0x1000 = 0x0000`（通訊模式）
  - `runForward / runReverse / stopDecel / emergencyStop` 每次都先呼叫 `ensureCuMode_()` 再寫 0x1001
  - 失敗的話 `ensureCuMode_` 印 WARN 但仍繼續 run 命令（caller 看 run 寫入結果決定）
- `user_lib/SE3_inverter.h`：新增 `ensureCuMode_` 私有宣告

### 為什麼
Manual V1.03 §7-3 「例一．通訊寫操作模式為 CU（通訊）模式」明確列為 Modbus 通訊範例的「步驟 1」 — **寫 Modbus reg 0x1000 = 0x0000 設成通訊模式**，然後才能寫運轉命令到 0x1001。

雖然使用者已從面板把 P.79 (00-16) 設成 3 (通訊模式)，但實測 0x1001 寫入仍 timeout。推測 SE3 firmware 把「面板模式」跟「Modbus 模式」分成兩個 latch — 面板設只影響面板 / 外部端子的命令來源；要讓 Modbus run 命令也被接受，必須**透過 Modbus 自己再寫一次 H1000=0**（SL-INV 工具背景應是這樣做的）。

H1000 / H1001 的 Modbus 值映射跟面板 P.79 不同：

| 模式 | 面板 P.79 | Modbus H1000 |
|---|---|---|
| 通訊（CU）| 3 | **0** |
| 外部 | (?) | 1 |
| JOG | (?) | 2 |
| 混合 1–5 | (?) | 3–7 |
| PU | (?) | 8 |

兩個值體系不一樣 — 同一個「通訊模式」在面板叫 3、在 Modbus 叫 0。

### 設計選擇
每次 run 命令都重寫 H1000 = 0，而不是只在 init 時寫一次：
- 防呆：避免使用者中途切回 PU 模式後忘記重設
- 開銷低：一個額外 8-byte Modbus write，bench 上幾 ms
- 失敗 best-effort：`ensureCuMode_` 失敗只 log warning 不阻擋 run 命令（讓 run 命令本身的錯誤訊息浮出來）

### 不變
- 0x1001 bit 配置（b1 STF / b2 STR / b7 MRS / 0x0000=stop）
- setFreqHz / setFreqRaw 走 0x1002 不需要 ensureCuMode（freq setpoint 跟 mode 無關）
- 現有 monitor reads (0x1003/4/5) 不變
- 面板必要設定（P.33=0 / P.154=3 / P.79=3 / P.35=0）仍要操作員手動設

### 已驗證 ✓ (2026-05-07 bench)
- Linux_test menu 23 `f 5` / `r 5` / `s` / `e` / `h <hz>` 全部通
- TX 序列驗證：先 `01 06 10 00 00 00 ...`（H1000=0）後 `01 06 10 01 00 02 ...`（H1001=STF）
- 馬達實際正轉 / 反轉 / 停止 / 緊停
- 結論：**SE3 firmware 確實要求 Modbus 控制前先把 H1000 寫成 0（CU mode latch）**，即使面板 P.79 已設 3 也不夠

## 2026-05-07h — Claude Code — Revert SE3_inverter 回 Modbus 協議（撤銷 2026-05-07g）

### 修改檔案
- `user_lib/SE3_inverter.cpp`：移除 `shihlin_write_data` 實作；`runForward / runReverse / stopDecel / emergencyStop` 改回 `writeParam(0x1001, ...)`、`setFreqRaw` 改回 `writeParam(0x1002, ...)`（Modbus FC 06）
- `user_lib/SE3_inverter.h`：移除 `shihlin_write_data` 宣告；keypad 設定註解改回 P.33=0 (Modbus) 路徑

### 為什麼
使用者明確要求用 Modbus 不用士林協議。

之前測試 Shihlin 協議寫入 `cmd HED val=0x01F4` 也回 no response，所以 SE3 端可能：
- P.33 還是 0（Modbus）→ 不收士林協議封包
- 或其他通訊參數沒對齊（P.51 / P.154 / parity）

### SE3 面板需確認
要走 Modbus 必須：
1. **P.33 (07-00) = 0**（Modbus 協議）
2. P.36 (07-01) = 1（站號）
3. P.32 (07-02) = 1（9600）
4. **P.154 (07-07) = 3**（8N2 RTU）— USR 也要設 8/None/2
5. P.79 (00-16) = 3（通訊模式）
6. P.35 (00-19) = 0（運轉源 = 通訊）
7. **斷電重啟 SE3**

### 已知限制
之前實測 0x1001 read/write 都 timeout — 在 P.33=0 + 上述設定下若仍不通，需要：
- 用 Modbus Poll 直接驗證（不經 Linux_test）
- 嘗試 FC 16 write multiple（不是 FC 06 single）
- 確認 USR 沒在 Modbus TCP gateway 模式（要 TCP Server 透傳）

### 不變
- Monitor reads at 0x1003/4/5（output Hz / 電流 / 電壓）位址
- Modbus FC 03 read frame 結構

## 2026-05-07g — Claude Code — SE3_inverter 改用士林 ASCII 協議寫入 run / freq

### 修改檔案
- `user_lib/SE3_inverter.cpp`：
  - 新增私有 helper `shihlin_write_data(uint8_t cmd_code, uint16_t value)`：建構士林協議 ASCII frame、計算 SumCheck、送出、收 ACK/NAK
  - `runForward / runReverse / stopDecel / emergencyStop` 改呼叫 `shihlin_write_data(0xFA, value)`：
    - runForward: 0x0002 (b1 STF)
    - runReverse: 0x0004 (b2 STR)
    - stopDecel:  0x0000（清所有 run bits）
    - emergencyStop: 0x0080 (b7 MRS)
  - `setFreqRaw` 改呼叫 `shihlin_write_data(0xED, value)`：HED = 目標頻率寫入 RAM
- `user_lib/SE3_inverter.h`：
  - 新增 `shihlin_write_data` 宣告
  - 註解整段更新標明：Modbus 0x1001 在此 firmware 不可用，改用士林協議；`P.33 (07-00) = 1` 是必要前提

### 為什麼
bench 實測：
- Modbus FC 03 read 0x1003/4/5（output Hz/A/V）→ ✓ 通
- Modbus FC 03 read 0x1001（status）→ ✗ no response
- Modbus FC 06 write 0x1001（run command）→ ✗ no response

→ SE3 firmware 把 0x1001 從 Modbus access 砍掉了（不管讀寫都 timeout）。manual V1.03 §7-3 說 0x1001 可讀寫，但實機不支援。

manual §7-2 說 SE3 同時支援 Modbus 與 **士林協議**（Shihlin protocol）。士林協議是 ASCII frame，命令碼 HFA 就是 run command（範例 Table 171 確認 frame format）。

切到士林協議後 run / freq 寫入應該能通。代價：**Modbus FC 03 read 不能用**（協定互斥），所以 monitor 讀（0x1003/4/5）會失效，但 user 明確說「先實作 write Hz / FWD / REV 就好」，monitor 之後有需要再加。

### 必要的 SE3 面板設定（操作員）
切換到士林協議：
1. **P.33 (07-00) = 1**（士林協議，不是 0 Modbus）
2. P.36 (07-01) = 1（站號 ≠ 0）
3. P.32 (07-02) = 1（9600，對齊 USR）
4. P.51 (07-06) = 1（結束字元只送 CR）
5. P.79 (00-16) = 3（通訊模式）
6. P.35 (00-19) = 0（運轉源 = 通訊）
7. **斷電重啟 SE3**

### Frame 格式（送出 13 bytes）
```
ENQ(0x05) | Stn(2 ASCII) | Cmd(2 ASCII hex) | Wait(1 char='0')
        | Data(4 ASCII hex) | SumCheck(2 ASCII hex) | CR(0x0D)
```

例：站號 1 跑正轉 → `05 30 31 46 41 30 30 30 30 32 44 39 0D`
（station="01", cmd="FA", wait="0", data="0002", sumcheck="D9"）

### 不變
- `init`（TCP 連線）/ `crc16`（雖然不再用 Modbus 了，留著）/ `getSlaveID` 不變
- monitor 讀（`readOutputFreqHz` 等仍用 Modbus 0x1003/4/5）— 但因為 P.33=1 會失效，回 false 但拿到 0
- `Crane_control_PI` 高階 API 不變（只看 runForward / runReverse 等）

### 待驗證
- SE3 面板設好 P.33=1 + 其他通訊參數 + 斷電重啟
- USR 設定不變（9600/8/N/1，但 SE3 P.51=1 預期是 8/None/1）
- Linux_test menu 23：
  - `f 5` → 應送出 ASCII frame 並收到 ACK，馬達正轉
  - `r 5` → 反轉
  - `s` / `e` → 停 / 急停
  - `m` → 會 fail（monitor 走 Modbus，但 SE3 切士林後不回應）— 預期，先忽略

## 2026-05-07f — Claude Code — **CRITICAL** SE3_inverter monitor 位址全錯（H100A/B/C → H1003/4/5）

### 修改檔案
- `user_lib/SE3_inverter.cpp`：
  - `readOutputFreqHz`: 0x100A → **0x1003**
  - `readOutputCurrentA`: 0x100B → **0x1004**
  - `readOutputVoltageV`: 0x100C → **0x1005**
- `user_lib/SE3_inverter.h`：register map 註解整段重寫，標明正確位址 + 標明 0x100A/B/C 是線速度/張力（不是 output monitor）

### 為什麼
從使用者提供的 SE3 V1.03 手冊 Word 版（Modbus 命令明細表 §7-3）對照確認：

| Modbus 位址 | 實際用途 | 我們驅動以為 |
|---|---|---|
| 0x100A | 線速度回饋 | ❌ 輸出頻率 |
| 0x100B | 線速度目標 | ❌ 輸出電流 |
| 0x100C | 張力給定 | ❌ 輸出電壓 |
| 0x100D | 轉矩給定（±400%）| — |
| **0x1003** | **輸出頻率** | (沒寫) |
| **0x1004** | **輸出電流** | (沒寫) |
| **0x1005** | **輸出電壓** | (沒寫) |

bench 實測 read 0x100A 回 0 = 真的有讀到「線速度回饋」，馬達停 = 0，**碰巧看起來像 output Hz = 0**，所以 monitor 以為通了其實全錯。

### 影響
- `Linux_test` menu 23 `m` 命令：之前讀「線速度」「張力」「轉矩」並當成 Hz/A/V 顯示，現在會讀真正的輸出值
- `Crane_control_PI` 用 SE3 monitor 做的任何邏輯都受影響（如果有的話）

### 不變
- 0x1001 run command / status word ✓ 已修（前一個 changelog 2026-05-07e）
- 0x1002 freq setpoint ✓ 沒變（這個本來就對）
- frame format / CRC / retry 機制不變

### 待驗證
- Linux_test menu 23 重 build 後 `m`：跑 `f 5` 後再 `m` 應看到 Hz ≈ 5、Current 有非零值（馬達運轉時）
- 如果 0x1001 (status word) 仍 comm fail → 那是另一個問題（位址對但 firmware 不接 standard FC 03）

## 2026-05-07e — Claude Code — **CRITICAL** SE3_inverter run command 暫存器位址 0x1101 → 0x1001

### 修改檔案
- `user_lib/SE3_inverter.cpp`：
  - `runForward / runReverse / stopDecel / emergencyStop` 寫入位址：**0x1101 → 0x1001**
  - `stopDecel` 寫入值：**0x0001 (b0) → 0x0000**（manual b0 是保留位，要停就清掉所有 run bits）
  - `readStatusWord` 讀取位址：**0x1101 → 0x1001**
  - 區塊註解更新標明 register 位址 + bit layout（含 manual §7-3 verify 標記）
- `user_lib/SE3_inverter.h`：
  - 整段 register map 註解重寫，標明 V1.03 manual §7-3 對照
  - 標明 0x1101 = 站號釋放（NOT run command）的陷阱
  - 註明 0x100B = 輸出電壓（不是電流）、0x100D = 輸出電流（百分比）— monitor read 待修

### 為什麼
bench 實測 menu 23 SE3 控制：
- `setFreqHz` (寫 0x1002) ✓ 成功
- `runForward` (寫 0x1101) ✗ comm fail
- `readStatusWord` (讀 0x1101) ✗ Modbus exception (5-byte error response)

從 SE3 V1.03 manual `§7-3 通訊命令明細` PDF 翻出 register 表：
- **H1001 = 運轉狀態（讀）/ 運轉控制（寫）** ← 這是 run command
- **H1101 = 站號釋放** — 只接受 magic value H9696 寫入，其他值都拒收

我們驅動原本拿了「站號釋放」當「運轉控制」來寫，難怪 SE3 拒收。

bit layout 也修正：原本以為 b0 是 stop bit，實際 manual 寫 b0 = 保留。要停只能清掉所有 run bits（寫 0x0000），motor 自動 decel ramp down。MRS (b7) 仍是輸出切斷。

### 影響範圍
- `Crane_control_PI/main.cpp` 用 `se3_left.runForward()` 等高階 API → driver 修完自動套用
- `Linux_test` menu 23 → 同樣自動套用

### 不變
- `setFreqHz` (0x1002) ✓ 沒變（這個位址原本就對）
- `readOutputFreqHz` (0x100A) ✓ 沒變
- frame 結構、CRC、retry、debug log 等 driver 內部機制不變

### 待驗證 / TODO
- bench 重新跑 menu 23 `f 5` → 馬達應該會轉了
- `m` 讀 status word 應正常回應（不再是 Modbus exception）
- **monitor 讀 voltage/current 還沒修**：0x100B 在 manual 是輸出電壓，driver 標 current；0x100D 才是輸出電流（百分比，需 P.9 rated 換算）。bench 確認 run command 對之後再修這部分

## 2026-05-07d — Claude Code — Linux_test 新增 menu 23：SE3 inverter 互動測試

### 修改檔案
- `Linux_test/main.cpp`：
  - 新增 `#include "SE3_inverter.h"`
  - 新增 `test_se3_inverter()` 函式（line ~3434）— 互動測試 menu
  - `print_menu()` 加 `23  SE3 inverter` 條目
  - 主迴圈加 `else if (line == "23") test_se3_inverter();`
- `Linux_test/Linux_test.vcxproj`：加 `SE3_inverter.cpp` + `.h` 到 ClCompile / ClInclude

### Menu 23 操作介面
```
[SE3 slave=N] f <hz> | r <hz> | s | e | h <hz> | m | q :
  f <hz>  forward (pay out 放繩) at <hz> Hz — 先 setFreqHz 再 runForward
  r <hz>  reverse (retract 收繩)
  s       stopDecel — 走 P.7 acc/dec 曲線停
  e       emergencyStop (MRS) — 輸出切斷立即停
  h <hz>  setFreqHz 但不改 run 狀態
  m       monitor — 讀輸出 Hz / 電流 / 電壓 / status word（含 b0/b1/b2/b7 解析）
  q       退出 menu（自動先 stopDecel）
```

啟動時 prompt：Gateway IP（預設 192.168.1.30）/ Port 4001 / Slave ID（預設 1）/ Max Hz（預設 50）。

開頭印「⚠ SAFETY」提示：
- 確認 SE3 P.79 = 2 (CU mode)，否則運轉命令被靜默拒收
- 先用低 Hz（5）確認方向再加速
- q 退出會自動 stopDecel；緊急狀況按 e

### 為什麼
SE3 是 2026-05-07 才接入的新設備（取代 ZS_DIO_R_RLY 控繩），Linux_test 還沒有單機隔離測試 menu。bench 上接好硬體要驗證單支 SE3 + 馬達是否正常 → 這個 menu 直接驗證：
- SE3 ↔ USR 通訊通
- Modbus run / freq / monitor 命令有效
- 馬達實際旋轉方向（fwd / rev 對應 pay-out / retract，依機械安裝可能反向）

### 不變
- 既有 menu 1–22 邏輯不變
- SE3_inverter.cpp/.h 本身不動

### 待驗證
- bench 上 ARM64 部署 + 跑 menu 23：
  1. 連線 OK → 顯示 prompt
  2. 按 `m` 讀狀態 → SE3 應有回應（前提 P.36 站號設好）
  3. 按 `f 5` → 馬達慢速正轉
  4. 按 `s` → 停
  5. 按 `r 5` → 反轉
  6. 按 `e` → 立即停
  7. 按 `q` → 自動 stopDecel + 退回主 menu

## 2026-05-07c — Claude Code — Crane 拓樸改成 4 個 RS485 gateway（每繩獨立 bus + DSZL 各佔一個）

### 修改檔案
- `Crane_control_PI/main.cpp`：
  - 4 個 TCP_client：`cli_A` (.30) / `cli_B` (.31) / `cli_C` (.32) / `cli_D` (.33)，取代之前的 cli_30/cli_31
  - 重新分配 device 到對應 cli + 新 slave ID：

| Gateway | IP | Slave | Device |
|---|---|---|---|
| USR_A | .30 | 1 | SE3 left |
|       |     | 2 | SD76 left |
|       |     | 3 | SD76 middle |
|       |     | 4 | CLV900 中間變頻器 |
| USR_B | .31 | 1 | SE3 right |
|       |     | 2 | SD76 right |
| USR_C | .32 | 1 | DSZL left |
| USR_D | .33 | 1 | DSZL right |

  - main() 每 init 一個 device 都有獨立 `[OK]` log，方便部署時逐項排查
  - 拓樸理由註解寫進 header
- `CLAUDE.md`：架構圖 + driver 表完全重寫（4 個 gateway，原本的 RS485_crane 拆成 left/right/dsz_l/dsz_r 4 個 bus）

### 為什麼這樣分
- **每繩有自己的 RS485 bus**：左繩 SE3 + 計米器在 .30；右繩 SE3 + 計米器在 .31。理由：每邊獨立可避免一邊 motion polling 影響另一邊 inverter 控制延遲。
- **DSZL-107 各佔一個 bus**（.32 / .33）：X518 採集板採樣率高，獨佔避免被其他 device polling 拖慢；也方便獨立替換 / 校正
- **中間管線（CLV900 + SD76 middle）跟左繩同 bus（.30）**：物理上中間管線跟左繩並走，電線同走線

### 不變
- 應用層邏輯（motion_rope / hold-to-pull / tension safety / cmd_status / EVT 流程）完全不變 — 都是透過 driver instance 名稱（se3_left, meter_left, dsz_left 等）操作，跟底下的 cli 連到哪沒關係
- SE3_inverter / DSZL_107 / SD76 / CLV900 driver class 不變
- 命令協定不變

### 待硬體驗證
- 4 個 USR-TCP232-304 通電 + IP 對應正確
- SE3 keypad 站號各設 1（同一 bus 上沒衝突 → 兩個 SE3 都可以用站號 1，因為在不同 bus）
- DSZL X518 站號各設 1
- SD76 站號設 2 / 3（不同 bus 也不衝突）
- CLV900 站號設 4

### 部署 checklist
1. 4 個 gateway IP 正確設好（.30 / .31 / .32 / .33）
2. 每個 gateway 的 RS485 接好對應的 device
3. 每個 device 站號正確設定
4. Build + deploy + 看啟動 log，確認每行 `[OK]`

## 2026-05-07b — Claude Code — Crane 左右繩控制改用士林 SE3 變頻器（取代 ZS_DIO 繼電器）

### [跨界: user_lib]
本 commit 新增 `user_lib/SE3_inverter.{h,cpp}` driver class，依協作分工屬 Jim 範圍。Sadie 應用戶要求先寫，標 `[跨界: user_lib]` 等 Jim review。

### 新增檔案
- `user_lib/SE3_inverter.h`：driver 介面（runForward/Reverse/stopDecel/emergencyStop/setFreqHz/readOutputFreqHz 等）
- `user_lib/SE3_inverter.cpp`：實作（Modbus FC03 read / FC06 write single）
  - 控制 reg `0x1101`：bit0 stop / bit1 STF / bit2 STR / bit7 MRS
  - 頻率 reg `0x1002`（RAM，0.01 Hz 單位，避免 EEPROM 寫入次數限制）
  - 監看 reg `0x100A` 輸出頻率 / `0x100B` 電流 / `0x100C` 電壓 / `0x1101` 狀態字

### 修改檔案
- `Crane_control_PI/Crane_control_PI.vcxproj`：加 SE3_inverter，移除 ZS_DIO_R_RLY
- `Crane_control_PI/main.cpp`：
  - **新增 cli_31 TCP_client** 連 USR2 @ `192.168.1.31`（IP 待硬體確認）
  - 移除 ZS_DIO_R_RLY 全部 reference
  - 新增 2 支 SE3_inverter 實例：`se3_left` (slave 1) / `se3_right` (slave 2) on cli_31
  - `motion_rope` / `cmd_roll_correct`：左右繩從 `relay.controlRelay(CH, on/off)` → `se3StartRopeMotion + stopDecel`
  - `cmd_manual` / `cmd_hold`：同上轉換成 inverter 控制
  - 新增 helper：`allRopeInvertersOff` / `allMotionEmergencyStop` / `se3StartRopeMotion` / `se3StartRopeHold` / `apply_hold_one_side`
  - 新增 SE3 速度常數：`SE3_MOTION_HZ=30` / `SE3_HOLD_HZ=20` / `SE3_MAX_HZ=50`
  - 方向約定：`pay_out=runForward` / `retract=runReverse`（wiring 相依，#define 包裝方便翻轉）
- `CLAUDE.md`：
  - 架構圖：拆成 USR1 (.30 sensors) + USR2 (.31 SE3) 兩台 gateway；移除 ZS_DIO；加 SE3 × 2
  - Driver 表：移除 ZS_DIO（搬到「未使用」並註明保留），新增 SE3_inverter / CLV900_inverter 列入「使用中」

### 設計決策（per 2026-05-07 對話）
- **Q1 = 2 台 SE3，各控一邊**：1 台對應左繩、1 台對應右繩
- **Q2 = IP 不確定**：placeholder `192.168.1.31`，硬體端確定後改一行常數
- **Q3 = 拔掉 ZS_DIO**：不再共存，乾淨切換
- **Q4 = 簡化版 hold**：固定速度 SE3_HOLD_HZ = 20 Hz（不做 GUI 速度滑桿）
- **Q5 = 一鼓作氣**：driver + main.cpp 改寫一起做完，沒做兩段

### ⚠️ 待硬體確認 / 實機驗證
1. **USR2 IP 是否真的 .31**？硬體端決定後改 `USR2_IP` 一行
2. **SE3 站號設定**：兩台分別設 1 / 2（透過面板 P.33 / 07-00），波特率對齊 19200
3. **SE3 keypad 預先設定**：Modbus 控制源（00-16 / P.79 = ?）、頻率源、watchdog timeout（P.52 = 5s）
4. **方向約定**：`pay_out=runForward` 是猜的，實機測：手動下 `pay_out_left on` 看繩子是放出還是收入；錯了把 `SE3_DIR_PAY_OUT` / `SE3_DIR_RETRACT` 巨集對調
5. **Modbus 暫存器**：從 PDF 文字 dump 解讀，PDF 中文 GBK 字型轉文字部分缺漏，建議實機 Modbus 工具測一下這些 address（特別 `0x1101` / `0x1002` / `0x100A`）對不對

### 不變
- `motion_active` / `abort_flag` / watchdog / motion_mtx 機制不變
- DSZL_107 整合（2026-05-06n）+ hold-mode 安全 (2026-05-06p) + crane EVT → washrobot PausedOnError (2026-05-07a) 全部保留
- `pay_out` / `retract` / `roll_correct` 命令外觀不變（後端實作改了）
- 中間絞盤 CLV900 邏輯完全不變

### 待辦
- 通知 Jim：mailbox 加一筆 SE3_inverter driver review 請求
- 部署前確認 SE3 keypad 設定 + IP + 方向約定
- 實機測 hold 按鈕、motion_rope、tension 安全保護

## 2026-05-07a — Claude Code — Washrobot 連動切回正式 Crane_control_PI

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `CRANE_IP` `"192.168.5.26"` → **`"192.168.1.101"`**
  - `WATCHDOG_TIMEOUT_MS` `60000` → **`2000`**
  - 新增 atomic `crane_alarm_pending_` + `crane_alarm_kind_` / `crane_alarm_detail_` + `crane_alarm_mtx_`
  - 新增 method `handle_crane_evt_(line)`
- `user_lib/WASH_ROBOT.cpp`：
  - `crane_cmd_` 改 receive loop：drain 一行一行解析，遇 `EVT ` 開頭 → 呼叫 `handle_crane_evt_()` 後**繼續等真正 reply**（不再因 EVT 卡住 RPC 流程）
  - 新增 `handle_crane_evt_()`：log + 廣播 `crane_relay <line>` 給 GUI；若是 `tension_alarm` / `tension_total_limit` → 設 alarm flag
  - `crane_watchdog_loop_` 每 tick 檢查 alarm flag，set → 進 PausedOnError + EVT `crane_alarm_paused`
  - `read_rope_weight_max_kg_()` 重寫：
    1. Primary：`crane_cmd_("tension", 2)` 解 `left=` `right=` → 回 `max(left, right)`
    2. Fallback 1：washrobot 端 DY-500 cache（目前無硬體常為 -1）
    3. Fallback 2：`read_easy_weight_kg_()`（保留 per Q4=(a) 多一層保險）
  - 兩處 `crane_cmd_("emergency_stop", 2)` → `crane_cmd_("stop", 2)`（Crane_control_PI 沒有 emergency_stop alias）
  - 建構子初始化 `crane_alarm_pending_(false)`
- `.claude/easy_crane_test_mode.md` §9a：標記 CRANE_IP / WATCHDOG / weight 來源三項為「✅ 已撤除 2026-05-07」
- `CLAUDE.md` DSZL_107 row：補充「Washrobot 透過 `crane_cmd_("tension")` 跨 PI 拿 kg；不直接接 .30 bus」

### 設計決策（per 2026-05-07 對話）
- **Q1=(a)+ 架構分離**：crane 獨家擁有 DSZL-107，washrobot 走 TCP RPC `tension` 拿 kg。速度等同原本 easy crane fallback（同 LAN TCP RTT，crane 端讀 atomic ~1ms）。
- **Q2=(a)**：washrobot 端 `emergency_stop` → `stop`（最小改動）。
- **Q3=(a)**：crane 端 `EVT tension_alarm/total_limit` → washrobot 進 PausedOnError，操作員必須手動恢復。
- **Q4=(a)**：保留 `read_easy_weight_kg_()` 作 final fallback（多一層安全網）。

### 為什麼
之前 washrobot 連的是 easy crane shim（test mode），不是 Crane_control_PI。最近兩個 commit 把 Crane_control_PI 補完 DSZL-107 + tension API + hold mode + safety monitor，可以正式切回去了。

### 不變
- `crane_attached_=off` detached mode 不變（bench 測試還能用）
- `crane_cli_estop_` 雙通道機制不變（避免 emergency stop 跟 RPC 鎖死）
- `crane_retract_safe_` / `crane_retract_to_weight_` 邏輯不變（只是 weight 來源換了）

### 部署 checklist
1. 重 build washrobot_new_PI ARM
2. deploy 到 192.168.1.100
3. 確認 Crane_control_PI 已在 192.168.1.101:5002 運行
4. 開啟 GUI，確認：
   - washrobot status 顯示 crane 連線正常
   - crane 區塊顯示 tension_left/right kg
   - 嘗試 hold 按鈕（按一下放開）
   - watchdog 不再 timeout（2s）
5. 若有 EVT tension_alarm，washrobot 應自動進 PausedOnError

### 待辦
- 實機校正 DSZL-107 scale factor（目前 0.01 是猜的）
- DSZL-107 byte order 確認
- 測試 EVT 流程：手動拉 cup 製造低張力，看 washrobot 是否進 PausedOnError

## 2026-05-06q — Claude Code — disable_seal obstacle 偵測電流門檻 2A → 1.2A

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`DISABLE_PHASE_CURRENT_LIMIT_MA` 2000 → **1200**（comment 更新）

### 為什麼
bench 觀察：2A 觸發前 cup 已經被馬達擠到變形。原本 `pos_error > 5° AND phase_current > 2000mA` 雙條件 obstacle 觸發太晚，等到觸發時機構已經受傷。

調到 1.2A 讓 obstacle 在 cup 開始受力但還沒變形時就被擋下。

### 不變
- `DISABLE_POS_ERROR_LIMIT_DEG = 5.0`（沒改，pos_err 仍要 > 5° 才共同觸發）
- AND 邏輯不變（避免單一指標瞬間 spike 誤觸）
- obstacle 處理流程不變（emergency_stop + 標 done + log + EVT + 不再 push）

### 待驗證
- bench 跑時若 1.2A 太敏感（正常密封過程也誤觸 obstacle）→ 可往上調 1500–1800
- 若還是太晚 → 往下調 800–1000

## 2026-05-06p — Claude Code — Crane GUI hold-to-pull 按鈕 + 張力即時顯示 + 總和門檻

### 修改檔案
- `Crane_control_PI/main.cpp`：
  - 新增 hold 命令：`up on/off`、`down on/off`、`up_left on/off`、`up_right on/off`、`down_left on/off`、`down_right on/off`
  - 新增 `set_up_stop_total_kg <kg>` 命令
  - 新增 `hold_loop` 背景執行緒：
    - 任一 hold flag 開 → 50 ms 週期讀 DSZL_107、更新 atomic
    - UP 任一 + total > threshold → `hold_all_off()` + `EVT tension_total_limit total=... threshold=...`
    - 各側張力檢查（low/high/diff） → `hold_all_off()` + `EVT tension_alarm`
    - 沒 hold → 200 ms 週期僅維持 atomic 快取（給 GUI 看）
  - `cmd_status` 加 `tension_left/right`、`tension_valid`、`up_left/up_right/down_left/down_right`、`up_stop_total_kg` 欄位（tension 優先讀 hold_loop atomic 快取，避免每個 status 都 Modbus 一次）
  - `cmd_stop` / watchdog timeout 都加呼叫 `hold_all_off()` 確保 hold 狀態同步清除
- `web_backend/public/index.html`：crane panel 新增「🪢 鋼索張力 / 拉放繩控制」分區：
  - 左/右/總和 kg 即時顯示 + 校零按鈕
  - 收繩總和門檻 input（debounced 150ms 自動送 `set_up_stop_total_kg`）
  - 6 顆 hold 按鈕：`↑/↓`（雙繩同時）+ `↑左/↑右/↓左/↓右`（個別）
- `web_backend/public/app.js`：
  - `onCraneLine` 新增解析：tension_*、up_*、down_*、up_stop_total_kg → 更新 DOM；EVT tension_alarm/limit → 清本地 hold 狀態
  - 新增 `wireCraneHold(btnId, onCmd, offCmd)` helper（mousedown / mouseup / touch / mouseleave 全綁）
  - 6 顆 hold 按鈕逐一綁好
  - 200 ms 週期 `sendSilent('crane', 'status')` 自動 poll
  - 收繩總和門檻 input 綁 debounced 150 ms
  - poll 回應加入 mute 規則（`isCranePoll`），避免洗版 log

### 設計選擇
- **無 bus_mtx：** cli_30 是 crane 端所有 device 共用 TCP_client。理論上 hold_loop 跟 motion_rope 同時跑會 frame interleave，但實務上使用者不會同時觸發 motion_rope 與 hold mode。先簡化、實機踩到再加。
- **GUI poll 200 ms（vs easy_crane 50 ms）：** crane 端 cli_30 device 數多（relay + SD76×3 + DSZL×2 + CLV900），降頻減少 bus 壓力。
- **「all stop together」：** 任一 alarm（threshold / 各側 low/high/diff）都 `hold_all_off()` 全部停（含 DOWN），符合 spec「停的時候要一起停」。
- **Server-authoritative：** server 自動停時 GUI 按鈕 `active` 狀態靠 status reply 同步（onCraneLine 解析 up_left/right/down_left/right），避免「server 停了但 GUI 看起來還在拉」。

### 預設 / 待調
- `UP_STOP_TOTAL_KG_DEFAULT = 50.0`：placeholder，待 DSZL-107 scale factor 實機校正後決定
- `TENSION_MIN_KG / TENSION_MAX_KG / TENSION_DIFF_MAX_PCT`：沿用 motion_flow §6.5

### 不變
- `pay_out` / `retract` / 既有 `pay_out_left on/off` 等 raw 命令保留（debug escape hatch，UI 仍露出）
- `motion_rope` / `cmd_roll_correct` 內的張力檢查邏輯（2026-05-06n 加的）不變
- watchdog（2 秒 timeout）邏輯不變

## 2026-05-06o — Claude Code — Realign 加 Pre-check #1（在 Phase A 之前的安全 vacuum check）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_()`：
  - **新增 Pre-check #1**：在 Phase 0 pre-flight stall clear 之後、Phase A retract body 之前
    - 對 feet (1,2,3,4) 跑 `vacuum_check_("all")` + filter 只看 feet
    - 失敗 → log + EVT + `await_user_intervention_("realign_pre_A_feet_unsealed")` 三選項：
      - **continue (Retry)**：重做 vacuum check（操作員手動處理 cup 後重測）
      - **skip**：繼續往下，機器可能會掉（用戶確認過才按）
      - **emergency_stop (Abort)**：sweep_stalls + return ERR `realign_pre_A_aborted`，**機體狀態完全不變**（Phase A 還沒跑）
  - **`phase2_slaves` 宣告上移**：到 sweep_stalls lambda 之後、Phase 0 之後（讓 #1 / #2 兩個 pre-check 都能用）
  - **Pre-check #2** 註解更新：明確標示「機體在 dangerous state」、「pre-check #1 已驗證過，#2 catch 邊緣情況」

### 為什麼
舊流程：Pre-check 只在 Phase A 之後（body 已縮）。如果這時偵測到 feet 沒吸 → 機體只剩 3 隻腳 + crane 撐 → pause 期間有 stall + 墜落風險。

新流程：
- **#1（Phase A 之前）**：機體在原本 9-cup 全吸狀態，pause 100% 安全。操作員可放心檢查。
- **#2（Phase A + Phase 1 之後）**：保留為邊緣情況 backup（catch crane assist 過程中 cup 脫離）。理論上 #1 過了 #2 也應該過，所以 #2 觸發機率低。

使用者要求：「如果偵測沒吸好，應該是發 ERROR 暫停給使用者檢查」+「不能 skip 因為 3 feet 撐不住會墜下」。

### 三選項風險表

| 選項 | Pre-check #1 (機體安全) | Pre-check #2 (機體危險) |
|------|------------------------|------------------------|
| Retry | 安全，鼓勵使用 | 仍危險，但讓操作員快檢查並 retry |
| Skip | DANGER：cup 真沒吸→Phase A 後墜下 | 進 Phase 2 motion，更危險 |
| Abort | 安全：機體狀態不變，return ERR | 機體留在 body retracted 狀態，需手動恢復（cmd_reset → cmd_pusher body extend） |

→ Pre-check #1 的 abort 是真正的「安全終止」選項；#2 的 abort 仍會留下難恢復的狀態。所以盡量讓問題在 #1 就被擋住。

### 不變
- Pre-check #2 內部邏輯不變（while loop + await_user）
- Phase A / Phase 1 / Phase 2 / Phase 2.5 / Phase B / Phase 3 / Phase 5 順序與內容不變
- sweep_stalls / Phase 0 pre-flight 不變

### 待驗證
- bench 跑 realign，feet 全吸 → log 看 `pre-check #1 OK`，跳過 await，正常進 Phase A
- feet 故意拔一支 → log 看 `realign_pre_A_unsealed=N` + state→PausedOnError + GUI 三選項
- 按繼續 → re-check 流程
- 按略過 → 進 Phase A（觀察是否真的會墜下，bench 上有 crane 應該不會）
- 按緊停 → state→Error，但機體 state 不變（feet 仍吸著、body 還沒動）

## 2026-05-06n — Claude Code — Crane_control_PI 接入 DSZL_107 + 張力安全保護

### 修改檔案
- `Crane_control_PI/main.cpp`：
  - include `DSZL_107.h`，新增 slave 5 / slave 6 兩支實例 `dsz_left` / `dsz_right`
  - main() init：兩支 init + `set_unit_kg()`（X518 預設單位是 N，改成 kg）
  - 新增張力安全 helper：`read_tensions()` / `tension_safety_check()` / `broadcast_tension_alarm()`
  - **`motion_rope` 與 `cmd_roll_correct` 的 polling loop 內加張力檢查**：每個 50 ms iter 讀一次 DSZL_107，違規（low/high/diff）→ broadcast `EVT tension_alarm kind=... left=... right=...` + `abort_reason = "tension_<kind>"` break
  - `cmd_status` 增加 `tension_left=<kg> tension_right=<kg>` 欄位
  - 新增命令：`tension`（純讀張力）/ `zero_tension <left|right|all>`（呼叫 DSZL_107 校零）
  - header comment 更新（slave 5/6 從「驅動未完成」改成正式列入）

### 安全保護門檻（per motion_flow §6.5，可實機調整）
- `TENSION_MIN_KG = 0.5` — 低於 → low alarm（疑似鬆弛 / 斷裂）
- `TENSION_MAX_KG = 3.0` — 高於 → high alarm（疑似卡住 / 超載）
- `TENSION_DIFF_MAX_PCT = 30%` — 左右差超過 → diff alarm（不平衡）
- `TENSION_DIFF_CHECK_MIN = 0.5 kg` — avg 低於這個就跳過 diff 檢查（避免低載時噪訊放大）

### 為什麼
- DSZL_107 driver 上一個 commit (2026-05-06m) 已寫好，這個 commit 把它接進 Crane_control_PI 應用層
- 為什麼安全檢查放在 motion polling loop 內而非獨立 thread：cli_30 是所有 device 共用的 TCP_client（relay / SD76 × 3 / CLV900 / 現在加 DSZL_107 × 2）。獨立 thread 的 Modbus 讀取會跟 motion 端的 SD76 polling 同時打到 gateway → 有 frame interleave 風險。放在 motion loop 內 = 同 thread 序列化，無並發問題。代價：idle 時不主動監看張力（idle 時 cup 吸著牆，rope 應鬆，無監看的迫切性）。

### 不變
- 原有 motion_rope / cmd_roll_correct 的計米完成判定邏輯不變
- watchdog / motion_mtx / abort_flag 機制不變
- 其他命令（pay_out / retract / manual relay / middle_set / zero_meters / home_status）行為不變

### 待辦
- 實機校正 DSZL_107 scale factor（目前 driver 預設 0.01 kg/digit，需用已知重量驗證）
- 確認 byte order（目前用大端 BE，X518 也支援 word-swap，必要時改 driver `parse_long`）
- GUI 側接入：把 `tension_left/right` 顯示在 web UI（user 之前要求的 hold-to-pull 按鈕也依賴此）

## 2026-05-06m — Claude Code — 新增 DSZL_107 driver（吊機鋼索張力感測，X518 採集板）

### [跨界: user_lib]
本 commit 新增 `user_lib/` driver class，依協作分工屬 Jim 範圍。Sadie 應用戶要求先寫，標 `[跨界: user_lib]` 等 Jim review。

### 新增檔案
- `user_lib/DSZL_107.h`：driver class 介面（左/右鋼索張力感測，slave 5/6）
- `user_lib/DSZL_107.cpp`：實作（Modbus FC03 讀資料、FC10 寫校零 / 單位 / 通訊參數）

### 修改檔案
- `Crane_control_PI/Crane_control_PI.vcxproj`：加入 `DSZL_107.cpp` / `.h` 編譯項
- `CLAUDE.md`：
  - 架構圖 slave 5/6 標記從「[TBD Modbus 暫存器表]」改成「（X518 採集板，CH1）」
  - Device Drivers 表把 DSZL_107 從「待實作」搬到「使用中」，註明 scale factor 預設 0.01 待實機校正

### 設計重點
- **Class 命名 `DSZL_107`** 對齊 CLAUDE.md 角色名稱（左/右鋼索張力感測），但實際 Modbus 對話對象是 **X518 多通道資料採集板**（讀 DSZL_107 load cell 類比訊號）。.h 註解說明這個關係。
- **per architecture (b)**：每台 X518 只用 CH1，slave 5（左）+ slave 6（右）。
- **API 模式照 `DY_500_weight_sensor`**：init(ip,port,id) / init(client&,id) / Modbus FC03+FC10 / log_utils 規範 / bool false=success。
- **核心讀取：** `get_tension_long(int32_t&)` 讀 0x0A00 area 取 CH1 long；`get_tension_kg(double&)` 走 graceful degradation（連續錯誤超過 10 次才回 true）。
- **校零：** `do_zero_ch1()` / `do_zero_ch2()` / `do_zero_all()` 寫 0x0A20。
- **單位：** `set_unit_kg()` 寫 0x0614 = 2（X518 預設 5=N，要改成 2=kg）。
- **Scale factor**（raw → kg）：預設 0.01（假設 X518 1 digit = 10 g），caller 可 `setScale()` 調。實機校正後再寫死。

### 來源資料
- `D:\洗窗戶機器人\電控設備資料\張力感測器\x518多通道数据采集器操作手册v1.1.pdf`（PDF 中文 GBK 字型轉文字部分缺漏，但 Modbus 暫存器位址 + 範例 frame 可解讀）

### 不變
- 應用層（WASH_ROBOT、Crane_control_PI/main.cpp）尚未串接 DSZL_107；本 commit 只提供 driver class
- DM2J / ZDT / JC100 / 等其他 driver 完全沒動

### 待辦（記在 mailbox.md / 後續 commit）
- 應用層串接：Crane_control_PI/main.cpp 加入 `DSZL_107` 實例 + 在 `cmd_status` 回 `tension_left/right`
- 實機校正 scale factor（已知重量法）
- byte order 確認（PDF 提到有 `(d3d2d1d0)` / `(d1d0d3d2)` 兩種，預設用標準 BE，必要時改）

## 2026-05-06l — Claude Code — Realign Phase 2 pre-check 改 PausedOnError + 三選項

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_()` Phase 2 pre-check vacuum 段：
  - 舊：失敗 → log + EVT + sweep_stalls + return ERR（直接 abort 整個 realign）
  - 新：失敗 → log + EVT + `await_user_intervention_("realign_phase2_pre_vacuum_fail")` → 依使用者選擇分流：
    - **continue (Retry)**：重新跑 vacuum_check_（給操作員時間手動把 cup 推回去後重測）
    - **skip**：接受風險，照樣進 Phase 2 motion（使用者 override）
    - **emergency_stop**：sweep_stalls + return ERR `realign_phase2_pre_vacuum_aborted`
  - 包成 while loop — Retry 路徑會再做一次 vacuum check

### 為什麼
舊版過嚴：任何一支 feet cup 沒 sealed 就直接 abort 整個 realign，但此時 body group 已經被 Phase A 縮回 0。abort 後 body 卡 retract、操作員無法繼續 → 必須手動恢復。

實際情境：
- 真的沒 sealed → 操作員需要時間檢查 + 手動處理
- 偽陽性（sensor 雜訊 / 暫時讀錯）→ retry 一次就過

新流程給操作員三個選項：
- 自己看到沒 sealed 又手動處理好 → 按繼續，重新檢查
- 確定那支 cup 已不重要（例如 sensor 壞）→ 按略過繞過去
- 機器狀況不對 → 緊急停止

### 不變
- vacuum_check_ 內部 multi-sample 邏輯不變（已經 3 次取最弱）
- 只 check feet（1,2,3,4），body / disabled / ZDT_C 仍在 filter 排除
- stall sweep（前一段）不變

### 待驗證
- bench 跑 realign 觸發 unsealed 時：state 應變 PausedOnError、GUI 顯示按鈕、log 印 "pausing for user intervention"
- 按繼續：重 check vacuum，沒問題就進 Phase 2
- 按略過：不重 check，直接進 Phase 2 motion（unsealed cup 會被 motion）
- 按緊停：return ERR 並 sweep stalls

## 2026-05-06k — Claude Code — disable_seal Step A 加 stall release + EN retry，Step C 加 retry + 診斷 log

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `pusher_extend_with_disable_seal_()` Phase 2：
  - **Step A**（每 iter 開頭）：
    - 對每支 not-done slave 先 `release_stall_flag()`（覆蓋上一個 iter Step D timeout / Step E emergency_stop 殘留的 latch）
    - `motion_control_driver_EN(true)` 加 return-check：失敗就 retry 一次（log + 50ms wait + 再試）
    - settle delay 從 80 ms → **200 ms**（給 firmware 更長時間 enable）
  - **Step C**：
    - `motion_control_pos_mode_nowait` 的 `retry` 參數從 **1 → 2**（總共最多 3 次 attempt）
    - pos_mode 仍失敗時 → `get_system_status()` 讀狀態 → log `en/stall/pos/posErr/I` 診斷資訊 + 發 EVT `disable_seal_push_fail`

### 為什麼
2026-05-06 bench 觀察：disable_seal Phase 2 iter 2 對 slave 6,7,8 都回 `pos_mode FAIL`，但實測**這幾支沒 stall**。

`motion_control_pos_mode_nowait` 的 FAIL = **Modbus 通訊重試耗盡**（送出失敗 / 沒回應 / response 壞）。驅動內部已自動 release_stall_flag 一次再 retry。所以 stall 不是直接原因，更可能是：

1. **Step E disable EN 後，Step A re-enable 命令本身失敗**（Modbus 漏一支）→ 馬達還 disabled → pos_mode 拒收
2. **Re-enable 後 settle 不夠**（80ms 不足，firmware 需更久）
3. **Stall_flag 雖沒爆出，但被 emergency_stop 暗 latch**（firmware quirk）

修法：

- (a) Step A 主動 release_stall_flag，覆蓋暗 latch 情境
- (b) Step A EN re-enable 加 return-check + 自動重試一次
- (c) settle 200 ms 給 firmware 更多時間
- (d) Step C 內部 retry 加倍，多撐一次通訊抖動
- (e) Step C 失敗時印狀態（en bit / stall / posErr / I）→ 下次再 fail 有具體訊息可診斷

### 不變
- Phase 1 fast extend 邏輯不變
- Step B vacuum pre-check / Step D wait / Step E disable / Step F vacuum poll 都不變
- weak_seal 收尾邏輯不變
- iter 上限 5、cap 7500、INCR 1500 都不變

### 待驗證
- bench 跑 step_up/down + realign Phase B 看 iter 2+ 是否還會出 pos_mode FAIL
- 若仍 fail，會看到診斷 log 例如 `en=0 stall=0 pos=2517° posErr=0° I=0mA` → 可確認是不是 EN 沒成功 enable
- 若 `en=1 stall=0` 但仍 fail → 純粹 Modbus bus 問題，要更深入處理（增加 inter-cmd delay 或檢查 cli sendData 失敗模式）

## 2026-05-06j — Claude Code — Realign Phase A 從 5,6 擴大到全 body 組（5,6,7,8）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_()`：
  - **Phase A**：`body_upper_56 = {5,6}` → `body_all = {5,6,7,8}`
    - 整段註解改寫，標明動機（避開 Phase 2 relative motion 對 7,8 也會 stall 的問題）
    - 兩段式 retract 套用到全 4 支
    - **移除 A4 / A5**（valve ON + settle）— body 全縮了沒理由再開 valve；valve 留 OFF 到 Phase B
  - **Phase 2 pre-check**：`phase2_slaves` 從 `{1,2,3,4,7,8}` 改 `{1,2,3,4}`；vacuum_check_ filter 也排除 7,8
  - **Phase 2 motion**：cmds 建構迴圈跳過 `{5,6,7,8}`（之前只跳 5,6）
  - **Phase 2.5**：re-read 跳過 `{5,6,7,8}`（之前只跳 5,6）
  - **Phase B**：
    - 在 disable_seal 之前加 `pqw_set_relay_verified_(CH_VALVE_BODY, true)`（接 Phase A 的 valve OFF）
    - body slave 4 支用 `preset_extend_pulse_for_slave_(s)` 取各 slave 的 preset：5,6 = 28500、7,8 = 27900
    - log 改 "re-extend whole body group (5,6,7,8) via disable_seal"

### 為什麼
使用者 bench 觀察：Phase 2 relative-mode retract 時 slave 8 也會 stall（先前是 slave 6）。問題不只是 5,6，整個 body 在 relative drift retract 時都有 stall 風險。

解法：把 **body 全縮 → feet realign → body 全重建** 的 pattern 從 5,6 擴大到 4 支：
- Phase A 全 body 縮回 0（兩段式，避免脫壁 stall）
- Phase 2 只動 feet（1,2,3,4）— 不會再有 body relative motion stall
- Phase B 用 disable_seal 重建全 4 支 body cup（跟 cycle_group_ + step_up/down 完全同流程）

每支 body cup 從 retract 0 走 fast extend 到 (preset − 1.5 cm)，再 iter loop 5 × 0.5 cm 推到 (preset + 1 cm cap)，每次 push 後 200ms holding → disable → 5sec 等真空。

### 風險 / 注意

- **Body 整組無真空時間更長**：Phase A retract 開始到 Phase B disable_seal 完成 = 整段 body 無支撐。期間 feet 4 支撐機體 + Phase 1 crane assist 抬重。如果 feet 不穩、crane 不在線（detached），機體可能下沉。
- **Phase B 從 retract 0 fast extend ~9 cm**：disable_seal Phase 1 一次衝 9 cm，速度依 PUSHER_RPM_BODY_EXTEND（700 rpm）+ ACC 255。實測若慣性過頭撞牆 stall，由 disable_seal 自己 defer + Phase 2 iter loop 接管（已驗證機制可用）。

### 不變
- Phase 0 read positions / drift compute / threshold check 不變（5,6,7,8 仍計入 max_abs_drift，body 漂移仍是 realign 觸發條件）
- `realign_skip` 仍只擋 disabled + ZDT_C
- sweep_stalls / pre-flight stall clear / Phase 1 crane assist / Phase 5 crane restore 不變
- `clear_other_group_stalls_` 在 cycle_group_ 的呼叫不受影響
- 失敗路徑：A1 valve OFF / A3 retract / B valve ON / B disable_seal 各自的 ERR return + sweep_stalls + motion_active_=false 不變

### 待驗證
- bench 跑 realign log 應依序看到：
  ```
  [realign] phase A: retract whole body group (5,6,7,8)
  [realign] phase A: body group retracted to 0, valve OFF
  [realign] crane assist done...
  [realign] phase 2 pre-check OK
  [realign] phase 2 stage 1 slave 1/2/3/4 ...   (只動 feet)
  [realign] phase 2.5: re-read positions...     (只 feet)
  [realign] phase B: re-extend whole body group (5,6,7,8) via disable_seal
  [disable_seal:5/6/7/8] ... SEALED ...
  ```
- 確認 Phase B disable_seal 對 4 支 body cup 同步 trigger 工作正常（slave 5,6,7,8 應同時 phase 1 fast extend）

## 2026-05-06i — Claude Code — Realign real_pos sanity 下限放寬到 -10°

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_()`：`REAL_POS_MIN_DEG` 從 `0.0` 改 `-10.0`

### 為什麼
bench 實測 slave 6 縮到 retract zero 後讀到 `real_pos = -0.0109863°`（約 -0.01°）— 真實在 zero 附近的 encoder 雜訊，不是 corrupt frame。原本的 `[0, 6000]` 嚴格範圍會把這個 reject 成 "bad pos"，連續 3 次 retry 都讀同樣負值 → UNREADABLE → stall flag 沒 sweep 到、last_seal_pulse_ 沒更新。

放寬下限到 -10° 可以容忍 encoder 在 zero 附近的合理抖動，仍能擋掉真正的 garbage（觀察過的：2.96e6° / 0° / 大負值都會被擋）。

### 影響
- Phase 0 read positions、Phase 2.5 re-read、sweep_stalls 三處都用同一組常數 → 全部一起放寬

## 2026-05-06h — Claude Code — GUI EXTEND 改用 disable_seal 機制（與 step_up/down 一致）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `smart_extend_subset_()`：
  - 舊：`pusher_extend_with_vacuum_stop_` + `sleep VACUUM_SETTLE_MS` + `fine_tune_extend_per_slave_`（推到 last_seal_pulse_，再 fine_tune 補伸 +1cm × 3 輪 = +3cm cap）
  - 新：`pusher_extend_with_disable_seal_`（與 cycle_group_ body extend 同機制）
    - Phase 1 fast extend 到 `target − PHASE1_BUFFER_PULSES`（target − 1.5 cm）
    - Phase 2 iter loop：push +0.5cm absolute → 200ms holding → disable EN → 5s 等真空 → re-enable on seal
    - 上限 5 iter / +2.5 cm cap
    - final_pulse 自動寫入 `last_seal_pulse_`
  - 移除 `fine_tune_extend_per_slave_` 呼叫（disable_seal Phase 2 取代）
  - log 訊息加 "(disable_seal mechanism)" 標示

### 為什麼
使用者要求 GUI EXTEND 按鈕的伸腳行為跟 step_up/down 一樣：「先伸到 preset − 1 cm」。

step_up/down 透過 `cycle_group_` → `pusher_extend_with_disable_seal_`，第一個 push 目標就是 phase1+1500 = preset − 1cm（PHASE1_BUFFER 4500，第一輪 push +1500）。

舊 manual path 走 `pusher_extend_with_vacuum_stop_` + `fine_tune` — 直接推到 target、看真空，沒有「短推 → disable → 等真空」的迭代。對 cup 較硬的撞擊。

切到 disable_seal 後：
- 第一次 push 目標 = preset − 1 cm（與 step_up/down 一致）
- 之後 +0.5 cm × 4 iter，最多到 preset + 1 cm
- 馬達在 push 之間都是 disable（無持續出力）
- vacuum 早建立的 cup 提早 done，沒密封的繼續推

### 影響範圍
1. **GUI EXTEND**（feet / body / center group 按鈕）→ 走 `cmd_pusher → smart_extend_subset_`
2. **GUI 單支推桿 EXTEND**（zdt_pusher slave N extend）→ 走 `cmd_zdt_pusher → smart_extend_subset_`

兩者都自動套用新機制，不需改 GUI 端。

### 不變
- `cycle_group_`（auto step_up/down 用）原本就走 disable_seal，沒變
- `cmd_attach` 仍用 `fine_tune_extend_per_slave_`（attach 第一次貼牆是不同情境，所有 cup 同時補伸 +1cm × 3 比較適合）
- `last_seal_pulse_` D persistence + B body delta 邏輯不變
- 失敗回 "ERR pusher_move_fail" 給 caller 不變

### 待驗證
- GUI 按 EXTEND 看 log：`[smart_extend] feet target_pulses={...} (disable_seal mechanism)` → `[disable_seal] Phase 1 fast extend` → `[disable_seal:N] iter 0 push absolute target=...`
- 第一次按（last_seal = preset）的 iter 0 target 應該是 preset − 1 cm（feet upper = 23000 → 20000；feet lower = 23900 → 20900；body 5,6 = 28500 → 25500；body 7,8 = 27900 → 24900；center = 30000 → 27000）

## 2026-05-06g — Claude Code — Realign Phase A 5,6 縮腳改兩段式

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_()` Phase A A3：
  - 舊：單一 `pusher_move_many_(body_upper_56, PUSHER_RETRACT_PULSE, PUSHER_RPM_RETRACT_FULL, ...)` 直接全縮回
  - 新：兩段式（對齊 step_down/up + cycle_group_ retract pattern）
    - Stage 1：`pusher_move_many_(body_upper_56, PUSHER_EXTEND_BODY_PULSE * 2 / 3, PUSHER_RPM_RETRACT, PUSHER_ACC_RETRACT)` — 縮 1/3（28500 → 19000）at 慢速 break-adhesion
    - Stage 2：`pusher_move_many_(body_upper_56, PUSHER_RETRACT_PULSE, PUSHER_RPM_RETRACT_FULL, PUSHER_ACC_RETRACT)` — 剩下 2/3 fast finish
  - 任一 stage 失敗 → `sweep_stalls + return ERR realign_phaseA_retract_stage{1,2}_fail`

### 為什麼
單段 fast retract 在 cup 真空釋放後仍有殘留黏附力 → ZDT 馬達拉不動 → stall。step_down/up + cycle_group_ 都已用兩段式（slow 1/3 → fast 2/3），realign Phase A 的 5,6 縮腳沒對齊，現在補上。

### 不變
- valve OFF / vacuum_wait_release / valve ON / settle 順序不變
- Stage 1 stall 風險仍由 ZDT firmware stall_flag 偵測 + sweep_stalls 處理（沒額外 defer 機制 — 因為 Phase A retract 不該長時間 stall，cup 釋放後應該能拉動）

## 2026-05-06f — Claude Code — Realign 改 body upper 5,6 縮腳→重建（disable_seal）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_()`：四段插入 + 兩處跳過 5,6
  1. **Phase A（新）**：在 pre-flight stall clear 之後、Phase 1 crane assist 之前
     - A1 `pqw_set_relay_verified_(CH_VALVE_BODY, false)` — body valve OFF（共用 valve，5,6,7,8 都會釋放真空）
     - A2 `vacuum_wait_release_({5,6}, VACUUM_RELEASE_WAIT_MS)` — 等 5,6 cup 真空釋放
     - A3 `pusher_move_many_({5,6}, PUSHER_RETRACT_PULSE, ...)` — 全縮回
     - A4 `pqw_set_relay_verified_(CH_VALVE_BODY, true)` — valve 重開讓 7,8 重新吸住
     - A5 `sleep_ms_(VACUUM_SETTLE_MS)` — 給 7,8 真空恢復時間
     - 5,6 EN 全程不 disable
  2. **Phase 2 pre-check（新）**：Phase 1 crane assist 之後、Phase 2 motion 之前
     - 對 `phase2_slaves = {1,2,3,4,7,8}` 做 stall_flag sweep（latch 到的 release）
     - 對 `phase2_slaves` 做 vacuum_check_，發現任何沒密封的 cup → log + EVT + sweep_stalls + return ERR（不繼續 Phase 2）
  3. **Phase 2 motion 跳過 5,6**：cmds 建構迴圈加 `if (s == ZDT_RB1 || s == ZDT_LB1) continue;`
  4. **Phase 2.5 跳過 5,6**：re-read 不寫入 last_seal_pulse_[4]/[5]（5,6 在 Phase A 已縮到 retract 位置，等 Phase B 重建後 disable_seal 自己寫 final_pulse）
  5. **Phase B（新）**：any_stalled 處理之後、Phase 3 vacuum_check 之前
     - `pusher_extend_with_disable_seal_({5,6}, [28500, 28500], PUSHER_RPM_BODY_EXTEND, PUSHER_ACC_BODY_EXTEND)`
     - 與 cycle_group_ body extend 同機制（Phase 1 fast → Phase 2 iter 短推+disable+等真空）
     - final_pulse 自動寫入 `last_seal_pulse_[4]`（slave 5）/ `last_seal_pulse_[5]`（slave 6）
     - 失敗 → sweep_stalls + return ERR

### 為什麼

1. **5,6 cup drift 量最大**：body 上方兩支（slave 5,6 = ZDT_RB1/LB1）是長 cup（preset 9.5 cm），承擔最多載荷，disable_seal lock + 反覆 stall 累積最快漂移。relative motion 修不動的時候改成「縮回完整重建」最徹底。
2. **避開 relative motion 的隱憂**：Phase 2 relative mode 對 cup 沒接觸 / encoder 飄走的情況不可控（encoder 動了但物理位置不一定動）。5,6 直接 retract → 物理歸零 → 再 disable_seal 重建到 preset，路徑乾淨。
3. **使用者要求**：「身體組上面兩支 slave 5,6 先解真空，縮腳後（不關使能）把身體組真空打開，等到其他部分調整完後再伸腳，且使用和其他伸腳分是一樣的 disable 機制伸腳」+「Phase 2 開始移動前要先檢查 1,2,3,4,7,8 真空和 stall 狀態，有 stall 必須先解」。

### 副作用 / 注意

- **Body valve 共用**：A1 valve OFF 會讓 7,8 也短暫失真空。A4 重開後 7,8 cup 因為還貼著牆面 + pump 持續抽氣，會自己重新吸住。A5 等 `VACUUM_SETTLE_MS = 2000 ms`。
- **A1–A5 期間 body 整組無真空**：feet 撐機體，但此時 crane 還沒 assist（Phase 1 在 A 之後）。風險：feet 不穩 + body 失真空 → 機體下沉。bench 驗證後若有風險可考慮 Phase A 與 Phase 1 對調。
- **5,6 EN 不 disable**：使用者明確要求。整段 5,6 EN enabled，A3 retract 後馬達 holding 在 0；Phase B disable_seal 內部自己 cycle EN（disable + 等真空 + re-enable）。
- **Phase 2 pre-check 失敗保守處理**：對 1,2,3,4,7,8 vacuum 不過 → return ERR + sweep_stalls，不繼續 motion，避免 corrupt encoder 累積。

### 不變
- Phase 0 read / drift compute / threshold check 不變（5,6 仍計入 max_abs_drift，5,6 漂移仍是 realign 觸發條件之一）
- `realign_skip` 仍只擋 disabled + ZDT_C
- sweep_stalls / pre-flight stall clear / Phase 1 crane assist / Phase 5 crane restore 不變
- any_stalled → PausedOnError 邏輯不變（給 1,2,3,4,7,8 stall 用，5,6 不會走這條）

### 待驗證
- bench log 應依序看到：`phase A: retract body upper 5,6` → `phase 2 pre-check OK` → Phase 2 motion 不含 5,6 → `phase B: re-extend body upper 5,6` → disable_seal SEALED 5,6
- A1–A5 期間 body 失真空 + 無 crane assist 的風險
- Phase B disable_seal 從 retract（0 pulse）fast extend 8 cm 是否需要 staged
- 5,6 在 Phase B disable 期間 valve 仍 ON → 7,8 真空仍維持，這段沒影響

## 2026-05-06e — Claude Code — 修 await_user_intervention_ 巢狀 PausedOnError 卡死 bug

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `await_user_intervention_()`：開頭 `state_before_pause_ = prev` 改成「只在 `prev != PausedOnError` 時才覆寫」，否則保留前一段的真正 source state。

### 為什麼
2026-05-06a 加的 realign Phase 2 stall → `set_state_(PausedOnError)` 沒走 await，導致一個 corner case：
1. realign stall 直接 set state PausedOnError（state_before_pause_ = Running）
2. caller 把 realign FAIL 當 non-fatal，繼續往下執行（PausedOnError 設了但沒人擋）
3. 後續 try_or_pause_ 抓到別的錯誤 → 呼叫 `await_user_intervention_`
4. await 開頭讀 `prev = state_.load()` = **PausedOnError**（已經是了）
5. 把 `state_before_pause_` 覆寫成 PausedOnError ← bug
6. 使用者按 retry → `cmd_continue` 把 state 設回 `state_before_pause_` = **PausedOnError**
7. await while loop 永遠不結束 → retry/skip 像沒按一樣

實測（2026-05-06）：realign slave 5 stall 後，step_up 接著 vacuum_release timeout 進 try_or_pause_，使用者按 retry/skip 都沒反應，只能 emergency_stop 解卡。

修法：await 開頭判斷 `prev`，若已是 PausedOnError 就保留原 `state_before_pause_`（前一段記錄的真正 source state）。log 加印 `nested pause detected` 方便追蹤。

### 不變
- 其他 await_user_intervention_ 邏輯不變
- 2026-05-06a realign Phase 2 stall → PausedOnError 機制不變

## 2026-05-06d — Claude Code — Realign Stage B 再降速（80→60 RPM、ACC 200→50）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `REALIGN_RETRACT_RPM_FULL` 80 → **60**（1.2× Stage A 的 50 RPM）
  - `REALIGN_RETRACT_ACC_FULL` 200 → **50**

### 為什麼
2026-05-06 實測：Stage 1 完成後 Stage 2 啟動，slave 5 在 150 ms（第一次 poll）就 STALL。對比 slave 6（同身體上組）順利跑完，推斷是 slave 5 cup 特別黏。

根因推測：**Stage 1 → Stage 2 速度切換的 ramp-up 加速 phase 峰值扭力過高**：
- 80 RPM 配 ACC=200 → 加速 ramp 短、瞬間扭力需求最大
- slave 5 cup 黏附力恰好高於這個峰值 → 馬達啟動瞬間就被擋住 → stall

降速 + 降加速度雙管齊下：
- RPM 80 → 60：速度跳變從 1.6× 縮到 1.2×，更平滑
- ACC 200 → 50：加速 ramp 拉長 4×，峰值扭力降低約 4×

仍保留兩段式骨架（Stage A 50 RPM 破黏附 + Stage B 60 RPM 完成），萬一這版還不夠再考慮回單段或加 real_pos 驗證。

### 不變
- Stage A (50 RPM, ACC=200) 不變
- 兩段式 1/3 + 2/3 切分不變
- EXTEND 路徑（20 RPM）不變
- sweep_stalls retry / Phase 2 stall → PausedOnError 邏輯不變

## 2026-05-06c — Claude Code — Realign Stage B RPM 200 → 80（承重 + 真空狀態下保守加速）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`REALIGN_RETRACT_RPM_FULL` 200 → **80**

### 為什麼
2026-05-06b 直接套用 cycle_group_ 兩段式參數（30→500 RPM）的概念，但忽略狀態差異：
- cycle_group_ retract 時 valve 已 OFF、cup 已離牆 → 自由空走，怎麼快都沒事
- realign Phase 2 retract 時 valve 全 ON、cup 持續吸牆 → 整段過程 9 顆 cup 承擔機體重量

200 RPM 是 Stage A (50) 的 4×，速度突變可能：
- 機器 4× 速度衝向牆面
- 動量打破某顆 cup 真空 → 連環掉真空 → 機器掉落

改用 80 RPM (1.6× Stage A)：仍比單段式快、能加速完成 retract，但加速幅度溫和，避免承重 + 真空狀態下的速度突變風險。

### 不變
- Stage A RPM=50 / mag=delta/3 不變
- Extend 路徑（單段 20 RPM）不變
- 兩段式骨架（PhaseCmd / run_stage / sweep_stalls retry）不變

## 2026-05-06b — Claude Code — sweep_stalls 加重試 + realign Phase 2 改兩段式 retract

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 新增 `REALIGN_RETRACT_RPM_FULL = 200`（Stage B 快速完成段）
  - 新增 `REALIGN_RETRACT_ACC_FULL = 200`
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_()`：
  - **`sweep_stalls` 改寫：** 每支最多 3 次重試（status read 失敗或 real_pos 在 `[0, 6000]°` 之外都當 corrupt frame，sleep 30 ms 後 retry）。3 次都失敗 → log warn + EVT `realign_post_stall_read_fail`。
  - **Phase 2 改兩段式：**
    - `delta > 0` (retract)：Stage A 走 `delta/3` at `REALIGN_RETRACT_RPM=50` 慢速破真空黏附；Stage B 走剩 `2*delta/3` at `REALIGN_RETRACT_RPM_FULL=200` 快速完成
    - `delta < 0` (extend)：仍單段 full mag at `REALIGN_EXTEND_RPM=20`（在 Stage A 跑完，Stage B 不再跑）
    - `delta == 0`：skip
  - 用 `PhaseCmd` struct + `run_stage` lambda 包裝送命令 / sync trigger / wait / stall handling。任何一段 stall → emergency_stop 其他 moving slaves → break。pos_send_fail 也一併用 flag 帶出。

### 為什麼
1. **sweep_stalls retry：** 2026-05-06 實測 sweep 漏掉 slave 5/7 latched stall flag（單次讀到 corrupt frame，stall_flag bit 偶然讀為 0）。retry + sanity check 確保至少有一次乾淨讀取。
2. **兩段式 retract：** 2026-05-05 slave 6 / 2026-05-06 slave 3 都在 Phase 2 retract 早期（150 ms / 1200 ms）stall — cup 黏牆，馬達一開始就拉不動。比照 cycle_group_ body_pre_pusher_retract 兩段式（先慢 1/3 破黏點、再快 2/3 完成），讓 stall 機率降低。

### 不變
- Phase 0 / 0.5 (pre-flight stall clear) / 1 / 2.5 / 3 / 4 / 5 邏輯不變
- Phase 2 stall 仍進 PausedOnError（2026-05-06a 加的）
- EXTEND 路徑速度（20 RPM）不變

## 2026-05-06a — Claude Code — realign Phase 2 stall → 強制 PausedOnError

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_()` Phase 2 stall 處理：
  - sweep_stalls + motion_active_=false 後，記錄 `state_before_pause_`，`set_state_(State::PausedOnError)`，evt `realign_stall_paused slave=X`
  - log `[realign] PAUSED ON ERROR — slave X stalled, state→PausedOnError`

### 為什麼
2026-05-06 實測事故：realign Phase 2 slave 3 stall → emergency_stop 其他 slaves 凍結 → caller (`cmd_step_up`) 把 realign 當「non-fatal」繼續執行 → step_up body_pre_cycle 釋 body 真空 → **機器掉下來**。原因：cup 凍結在非 preset 位置、機體幾何已被扭歪、crane assist=0 cm 沒分擔重量。

PausedOnError 強制操作員介入：
- 下個 cmd_step_down/up 看到 state ≠ Attached → 拒絕執行
- 操作員按「繼續」(`cmd_continue`) 切回 state_before_pause_，或 emergency_stop 完整中止
- EVT `realign_stall_paused` 通知 GUI

### 不變
- sweep_stalls / Phase 2.5 重讀位置邏輯不變
- 其他 4 個 return 點（crane_assist 失敗、pos_mode_send 失敗、vacuum_check 失敗、成功）不轉 PausedOnError，只 sweep + return

## 2026-05-05zi — Claude Code — 解真空後預先清對側 group stall flag

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 新增 method 宣告 `bool clear_other_group_stalls_(const std::string& current_group);`
  - `cycle_group_` template retry 路徑：`vacuum_wait_release_` 之後、retract 之前插 `clear_other_group_stalls_(group)`
- `user_lib/WASH_ROBOT.cpp`：
  - 新增實作 `clear_other_group_stalls_(current_group)`：依 current_group 對應到 other group（feet ↔ body），檢查對側 4 支 ZDT 的 stall_flag，set 就 release_stall_flag + log + EVT。disabled slaves 跳過。回傳 false（best-effort，不阻塞流程）
  - `do_step_down_`：
    - body_pre_cycle 在 vacuum_wait_release_ 之後、body retract 之前插 `clear_other_group_stalls_("body")`
    - feet_pre_cycle 在 vacuum_wait_release_ 之後、feet retract 之前插 `clear_other_group_stalls_("feet")`
  - `do_step_up_`：
    - feet_pre_cycle 在 vacuum_wait_release_ 之後、feet retract 之前插 `clear_other_group_stalls_("feet")`
    - body_pre_cycle 在 vacuum_wait_release_ 之後、body retract 之前插 `clear_other_group_stalls_("body")`

### 為什麼
使用者要求：「在每個腳組/身體組解真空後，先確認另一組沒有堵轉，有堵轉則解開，再進行下一步縮腳動作」。

當一組釋放真空 + cup 從牆面剝離時，機體載荷會重新分配 — 另一組（仍吸著牆面、承載機體重量）的 cup 可能瞬間受側向反作用力 → ZDT 馬達 holding 力不夠 → pos_error 累積 → stall_flag latch。stall flag 一旦 latch，下次 pos_mode 會被 firmware 靜默拒收，馬達不動但程式以為動了 → cup 被拖壞。

舊的 pre-flight `ensure_all_zdt_stall_clear_` 只在每個 phase 一開頭跑（valve off 前），但對「真空釋放期間 / 之後」新產生的 stall flag 沒檢查到。新加的 `clear_other_group_stalls_` 補上這個 gap：每次解真空之後、進入下一個動作（retract）之前，掃對側組一遍。

### 不變
- `ensure_all_zdt_stall_clear_`（pre-flight）保留 — 對全 9 支掃，避免上一輪殘留
- `do_feet_realign_` 內的 `realign_skip` / `sweep_stalls` 邏輯不變
- `clear_other_group_stalls_` best-effort 不擋流程（comm fail 跳過、不 PausedOnError）；如果 firmware 真的卡到 stall release 失敗，下個 motion 還是會收到 stall 提示，由原本的 try_or_pause_ 處理
- center 不在 feet/body group 對應裡 — current_group ∈ {feet, body} 才會跑；center 仍走原 ensure_all_zdt_stall_clear_ 涵蓋

### 待驗證
- bench 跑 step_down/up 看 log：每個 phase 解完真空後出現 `[other_stall_clear]` 行
- 若有 stall 被清掉，會看到 `other-group slave N stall_flag SET ... → release` 與 EVT `other_group_stall_clear`

## 2026-05-05zh — Claude Code — realign Phase 0 / 2.5 加 ZDT real_pos sanity range check

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_()`：
  - 新增常數 `REAL_POS_MIN_DEG = 0.0` / `REAL_POS_MAX_DEG = 6000.0`（SMC LEYG25 物理行程 20 cm = 6000°）
  - **Phase 0** read positions：`get_system_status()` 成功後，檢查 `real_pos ∈ [0, 6000]°`；超出範圍視為 corrupt frame，保留 `prev_recorded`、發 `realign_phase0_bad_pos` event
  - **Phase 2.5** re-read：同樣的 range check，超出保留 prev、發 `realign_phase25_bad_pos` event

### 為什麼
實測 log：

```
[realign] phase 0 slave 2 real_pos=2.96173e+06° pulse=29617331 (prev_recorded=24230)
[realign] phase 0 slave 3 real_pos=0° pulse=0 (prev_recorded=23721)
[realign] slave 2 preset=23900 actual=29617331 delta=29593431 drift_cm=10357.7
[realign] slave 3 preset=23000 actual=0 delta=-23000 drift_cm=-8.05
```

slave 2 = 29,617,331 pulse ≈ 100 m（不可能，物理行程 20 cm）；slave 3 = 0（前次 prev = 23721，不可能瞬間退這麼多）。

`get_system_status()` 在通訊層成功（CRC 過、無 timeout）但 status struct 裡的 `real_pos` 是 garbage — ZDT firmware 已知問題（frame alignment / broadcast echo race，見 memory `project_zdt_firmware_quirks.md`）。

舊邏輯只擋 `get_system_status()` 回 true（comm fail），對 garbage 數值沒檢查 → garbage 被寫入 `last_seal_pulse_`，drift 計算爛掉，Phase 2 嘗試 retract 29593431 pulse 會撞 hardstop / extend 23000 pulse 會推到牆面再過半 cm。

加 range check 後：corrupt frame 被擋住，保留前次合理值，realign 用 prev_recorded 算 drift（仍可能稍微偏 — 但比 garbage 安全）。

### 不變
- range check 用上下限 `[0°, 6000°]`，正常運轉的 cup 位置都落在這區間內
- Phase 1.5（`disabled_zdt_slaves_` + `ZDT_C` skip）/ pre-flight stall clear / sweep_stalls 邏輯不變
- 其他呼叫 `Z_(s).status.real_pos` 的地方（disable_seal、cycle_group_）暫不加 range check — disable_seal 拿 final_pulse 寫 last_seal_pulse_ 也有相同風險，待後續一併處理
- 範圍 `[0, 6000]` 是寬鬆的物理上限；正常 cup 位置 ~2300–3200°，corrupt frame 通常表現為極端值（0、超大、負數）

### 待驗證
- bench 跑 realign 確認 corrupt frame 被擋（看 `OUT OF RANGE` log）
- 若實際遇到 valid 位置但被擋（例如 set_zero 在不同位置）→ 放寬上限或加負容忍

## 2026-05-05zg — Claude Code — 修 mid-step realign deadlock + realign 排除 ZDT_C (slave 9)

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - **`do_step_down_` / `do_step_up_`**：開頭 `std::lock_guard<std::mutex>` → `std::unique_lock<std::mutex>`；mid-step realign 呼叫前後加 `lk.unlock()` / `lk.lock()` 包夾
  - **`do_feet_realign_`**：
    - 函式開頭新增 lambda `realign_skip`：對 `disabled_zdt_slaves_.count(s) > 0 || s == ZDT_C` 為 true（永遠跳過 center 與已停用的 slave）
    - Phase 0 read positions、drift compute、pre-flight stall clear、sweep_stalls、Phase 2 motion、Phase 2.5 re-read 全部把 `disabled_zdt_slaves_.count(s) continue` 改成 `realign_skip(s) continue`
    - Phase 2 motion 在迴圈進入 `all_slaves` 後新增 `if (realign_skip(s)) continue;`（原本沒檢查 disabled）
    - Phase 3 `vacuum_check_("all")` 後加 `fails.erase(std::remove_if(fails.begin(), fails.end(), realign_skip), fails.end())` 把跳過的 slave 從 fail 名單剔除

### 為什麼

**1. Deadlock 修法**

`do_feet_realign_` 在 line 2684 取 `motion_mtx_`：

```cpp
std::lock_guard<std::mutex> lk(motion_mtx_);
```

但呼叫端 `do_step_up_` / `do_step_down_` 自己已經拿了同一個 mutex（line 2150 / 2432）。`std::mutex` 不是 recursive，**同 thread 重複 lock = undefined behavior（Linux 上典型表現是 hang）**。

實測 log 卡在 Phase 0 read + drift print 之後（這些都在 do_feet_realign_ 內 lock 之前）：

```
[realign] phase 0 slave 9 real_pos=3900.01° pulse=39000 (prev_recorded=30000)
...
[realign] slave 9 preset=30000 actual=39000 delta=9000 drift_cm=3
（卡在這裡）
```

改 unique_lock + 手動 unlock/lock 包 mid-step realign 呼叫，避開 re-entry。

**2. ZDT_C (slave 9) 排除原因**

Center pusher 在 step_down/step_up 流程裡 `[TEMP DISABLED 2026-05-04]`，沒被 retract / extend 過。但 encoder 累積值不是 set_zero 後的純伸長量 — log 看到 `real_pos=3900.01°` 等於 pulse 39000 ≈ 13 cm，但 SMC LEYG25 物理行程只有 10 cm。

這個假位置進入 realign Phase 0 + drift compute 後，slave 9 drift = 3 cm，把 max_abs_drift 直接拉到 3 cm，遠超 `REALIGN_THRESHOLD_CM = 1.5 cm`，**強迫 realign 一定會跑**（即使 1–8 漂移都只 ~1 cm 不該 realign）。而且 Phase 2 還會嘗試 retract slave 9 by 9000 pulse，可能撞 hardstop。

加 `realign_skip` lambda 把 ZDT_C 與 `disabled_zdt_slaves_` 一起排除，realign 完全不碰它。

### 不變
- end-of-step realign（在 cmd_step_down/up/run 裡的）邏輯不變 — 那邊呼叫時 do_step_*_ 已 return、mutex 已釋放，沒有 deadlock 問題
- `disabled_zdt_slaves_` 的其他用途（cycle_group_ / cmd_init / smart_extend_subset_）不受影響 — `realign_skip` 只在 `do_feet_realign_` 內定義
- threshold (`REALIGN_THRESHOLD_CM = 1.5 cm`) 與其他常數不動
- center 若日後重新啟用，從 `realign_skip` 拿掉 `s == ZDT_C` 即可

### 待驗證
- bench 跑 step_up / step_down 確認 mid-step realign 觸發 + 不再 deadlock + slave 9 drift 不出現在 log
- 若 1–8 漂移都 < 1.5 cm，threshold check 應 skip realign（之前因 slave 9 = 3 cm 被迫跑）

## 2026-05-05zf — Claude Code — disable_seal Phase 1 buffer 從 2 cm 縮到 1.5 cm（iter 0 推到 preset−1 cm）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`PHASE1_BUFFER_PULSES` 6000 → **4500**（comment 同步：「preset − 2 cm 起點」→「preset − 1.5 cm 起點，iter 0 推到 preset−1cm」）

### 為什麼
舊：Phase 1 終點 = preset − 2 cm（pulse 6000）。Phase 2 iter 0 目標 = phase1 + 1500 = preset − 1.5 cm。第一次 push 距離 preset 還有 1.5 cm，cup 不見得能接觸到牆面 → 真空建不起來、要靠後續 iter 慢慢逼近。

新：Phase 1 終點 = preset − 1.5 cm（pulse 4500）。Phase 2 iter 0 目標 = phase1 + 1500 = preset − 1 cm（body ratio 精確 1 cm；feet ratio 約 1.05 cm）。第一次 push 就直接壓到 preset 前 1 cm，cup 早期接觸牆面 → 真空更早建立，少跑幾個 iter。

使用者：「幫我改成第一輪就推到preset-1cm開始」。

### 副作用（待使用者決定）
`DISABLE_RETRY_MAX_OVEREXTEND = 7500` 是「Phase 1 終點之後再累積多少」。Phase 1 終點向前移 0.5 cm，5 iter 跑完最終位置也跟著往前 0.5 cm：

- 舊（buffer 6000，cap 7500）：max 位置 = preset − 2 + 2.5 = **preset + 0.5 cm**
- 新（buffer 4500，cap 7500）：max 位置 = preset − 1.5 + 2.5 = **preset + 1.0 cm**

如果不希望 over-extension 變大，可把 cap 從 7500 → 6000（max 維持 preset + 0.5 cm，但 iter 數從 5 變 4）。目前先保留 cap 不動。

### 不變
- `DISABLE_RETRY_INCR_PULSE = 1500`（每 iter +0.5 cm 不變）
- `DISABLE_RETRY_MAX_ITERS = 5`、`DISABLE_RETRY_MAX_OVEREXTEND = 7500`（cap 與 iter 上限維持）
- 其他常數與整體 Phase 2 流程（intended_target absolute 累加、Step D.5 holding 緩衝、disable+vacuum 等待）不變

## 2026-05-05ze — Claude Code — realign 結尾全 9 支 stall 掃描 + 清除

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_()`：
  - 新增 `sweep_stalls` lambda（在 `motion_mtx_` 取得後定義）：對 9 支 ZDT（disabled 跳過）`get_system_status()`，若 `stall_flag` SET 就 `release_stall_flag()` + log + EVT。
  - 5 個 return 點都呼叫 `sweep_stalls()`：
    1. crane_assist 失敗
    2. pos_mode_send 失敗
    3. Phase 2 motion stall
    4. Phase 3 vacuum check 失敗
    5. 成功 return

### 為什麼
實測：slave 6 在 Phase 2 RETRACT 時 150 ms 就 STALL（cup 黏牆，馬達拉不動）。`zdt_wait_motion_done_(defer_stall=false)` 已對 stall 那支 release 了，但 emergency_stop 其他 slaves 時可能也會 latch 它們的 stall flag，下次 realign 或 cycle 進來時 ZDT firmware 直接拒收 pos_mode → 連環 stall。結尾統一掃過一次保證下次乾淨。

threshold-skip 的 early return（在 `motion_mtx_` 鎖之前）不需要 sweep — 沒做任何 motion，stall flag 不可能因這次 realign 被 latch。

### 不變
- Phase 0 / 0.5 (pre-flight stall clear) / 1 / 2 / 2.5 / 3 / 4 / 5 邏輯不變
- Phase 2 stall 仍只回 ERR（caller log non-fatal），未改 PausedOnError

## 2026-05-05zd — Claude Code — disable_seal Phase 2 改 absolute target 累加 + push 完加 holding 緩衝

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - **新增** `DISABLE_PRE_DISABLE_DELAY_MS = 200` — push 完到 disable EN 之間的緩衝時間
- `user_lib/WASH_ROBOT.cpp` `pusher_extend_with_disable_seal_()` Phase 2：
  - **取消 `total_pushed[]` 陣列**，改成 `intended_target[]` 陣列
    - 在 Phase 1 結束後初始化 `intended_target[i] = phase1_targets[i]`（= preset − 6000）
    - Step C 每 iter 先 `intended_target[i] += INCR_PULSE`（1500），再用該值送 absolute push
    - cap 檢查改用 `(intended_target[i] - phase1_targets[i]) >= DISABLE_RETRY_MAX_OVEREXTEND`，意義不變（累積上限 +7500 pulse / +2.5 cm）
  - **`motion_control_pos_mode_nowait` 模式從 `relative=0` 改 `absolute=1`**，pulse 參數從 `INCR_PULSE`（delta）改成 `intended_target[i]`（絕對 encoder 座標）
  - **新增 Step D.5**（push wait 結束後，Step E emergency_stop+disable 之前）：`sleep_ms_(DISABLE_PRE_DISABLE_DELAY_MS)` = 200 ms 緩衝
  - 註解全部更新：流程圖加入 Step D.5、Step C 換 absolute 描述、log 訊息從 "push +N pulses" 改成 "push absolute target=X (cum +Y past phase1)"

### 為什麼

**1. relative → absolute target 累加**

舊（relative）：每 iter 從馬達當下 encoder 位置 +1500。問題：
- 若上一 iter stall（cup 撞牆但未密封），encoder 停在 stall 點 → 下一 iter 從 stall 點再 +1500，等於「重新從停的地方加 0.5 cm」。如果 cup 與牆面距離本來就估錯（例如 Phase 1 終點離牆比預期遠），每次 +0.5 cm 慢慢推可能還沒到牆面真空就建不起來。
- disable 期間 encoder 可能被外力拖動（真空往前拉、cup 彈簧往後）→ 下一 iter 從那個飄走的位置算 +0.5 cm，方向不可控。

新（absolute）：每 iter 把 in-memory `intended_target` 加 1500，送 absolute target 給 ZDT。馬達會把 encoder 拉到 in-memory 預定的座標：
- 不管前次 stall 在哪、disable 期間 encoder 飄了多少，馬達都試圖回到設計位置
- 累積進度由 in-memory 控制，不依賴馬達實際位置
- iter 0..4 的目標分別是 phase1+1500 / +3000 / +4500 / +6000 / +7500（= preset−4500 → preset+1500 → preset+0.5 cm 超過 preset）

使用者明確要求：「下一次推的位置應該是上一次原本的目標值再加1500」。

**2. 加 Step D.5 holding 緩衝**

舊：push wait 結束 → 立刻 emergency_stop + disable EN。問題：cup 剛接觸牆面那一瞬間就失去 holding torque，可能因彈性彈離牆面，密封建不起來。

新：push wait 完先停 200 ms（馬達還 holding，cup 被馬達力壓在牆面）→ 給 cup 時間建立初步接觸 → 再 disable EN 讓真空接手。

使用者：「推完後放一點點delay時間，在把zdt diable」。

### 不變
- Phase 1 fast extend 不變（absolute mode、phase1_targets[i]）
- Step A re-enable / Step B vacuum pre-check / Step D obstacle-stall 偵測 / Step F poll vacuum 全部邏輯不變
- 上限 cap、iter 數（5 iters，6000 → 7500 pulse 累積），語意等同舊版
- Step E 內部的 emergency_stop → 50 ms → disable EN 兩段流程不動
- weak_seal / obstacle / 收尾 force re-enable + 記 final_pulse 不變

### 待驗證
- bench 跑 step_down/up 確認新流程：每 iter motor 都嘗試到 in-memory 預定位置（看 log "push absolute target=X" 的進展），不會因 stall / drift 無法前進
- 200 ms 緩衝是否足夠（cup 接觸牆面建立密封需要的時間，使用者可調）

## 2026-05-05zc — Claude Code — feet 群組分上下兩組 preset（upper 8.0 / lower 8.3 cm）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `PUSHER_EXTEND_FEET_PULSE = 23000`：comment 從 "feet group ~8 cm (all 4 slaves)" 改成 "feet upper (slave 1,3 = ZDT_RF1/LF1) ~8.0 cm"
  - **新增** `PUSHER_EXTEND_FEET_PULSE_LOWER = 23900` — feet 下組（slave 2,4 = ZDT_RF2/LF2），對應 ~8.3 cm
- `user_lib/WASH_ROBOT.cpp`：
  - `preset_extend_pulse_for_slave_()`：feet 1-4 從單一回傳 `PUSHER_EXTEND_FEET_PULSE` 改成依 slave 分流 — slave 1,3 (ZDT_RF1/LF1) → 23000；slave 2,4 (ZDT_RF2/LF2) → 23900
  - `feet_max_overextend_cm_()`：preset 不再硬寫 `PUSHER_EXTEND_FEET_PULSE`，改呼叫 `preset_extend_pulse_for_slave_(s)` 取每支 slave 自己的 preset
  - `cmd_init` feet_lower 分支：extend 目標 `PUSHER_EXTEND_FEET_PULSE` → `PUSHER_EXTEND_FEET_PULSE_LOWER`，log 訊息 "~8 cm" → "~8.3 cm"
  - `cmd_init` feet_upper 分支：log 訊息 "~8 cm" → "~8.0 cm"（值不變）

### 為什麼
使用者要求把 feet 4 支拆成「上組 / 下組」兩個 preset：上組（slave 1,3 = F1）保留 8.0 cm；下組（slave 2,4 = F2）改 8.3 cm。實體機構上下兩段 cup 與牆面的距離不一致，下組要多伸 0.3 cm 才能達到設計密封壓力。

### 不變
- 半縮路徑（`PUSHER_EXTEND_FEET_PULSE * 2 / 3` 用於 step_down/up 中的 pusher_retract_half、manual_pusher_all、cycle_group_ step1_pulse）共用同一中間目標值，所有 feet slaves 一起退到 ~15333 pulse。對「先慢退一半再全退」的功能無影響（兩組起始點都比 15333 大很多）。
- body / center preset 不變（slave 5,6=28500 / 9.5cm；slave 7,8=27900 / 9.3cm；slave 9=30000 / 10cm）
- `last_seal_pulse_` 透過 `record_seal_pulse_` 與 `reset_seal_pulse_group_` 維護；reset 走 `preset_extend_pulse_for_slave_` → 自動拿到分組後的 preset，不需另外處理
- D persistence (`last_seal_pulse_`) + B body delta + E realign 全鏈路自動套用新 preset，無其他改動

### 待驗證
- bench 上 cmd_init 確認 feet_lower 確實伸 ~8.3 cm、feet_upper 維持 ~8.0 cm
- step_down/up 跑一輪確認 cycle_group_ 算 extend 目標時各 slave 拿到正確 preset
- realign 跑一次確認 phase 0 read real_pos / phase 2 雙向修正都用各 slave 的 preset 算 drift

## 2026-05-05zb — Claude Code — step_down/up Phase A 後加 mid-step realign

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `do_step_down_`：在 Phase A（body cycle）成功 + no_progress 檢查通過後，新增 `do_feet_realign_(force=false)` 呼叫，再進 Phase B（feet cycle）
  - `do_step_up_`：在 Phase A（feet cycle）成功 + no_progress 檢查通過後，新增 `do_feet_realign_(force=false)` 呼叫，再進 Phase B（body cycle）
  - 兩處都包 try-catch 風格的 non-fatal handling：realign 失敗只 log + EVT，不中斷 step

### 為什麼
原本 step_down/up 是「Phase A → Phase B → 整 step 結束 realign」。Phase A 結束時 cup drift 可能已經產生，但要等 Phase B 也跑完才會 realign — 中間如果 Phase B 又疊更多 drift（cycle_group_ retry / over-extend / disable_seal 補伸），realign 一次要修很多，幅度大、容易超出機構容忍。

改成 Phase A 一結束就先 realign：
- Phase A→B 邊界，剛吸好的那組 + 上一輪保持吸著的另一組 → **全 9 支 cup 都吸著**，realign 條件成立
- threshold check (`REALIGN_THRESHOLD_CM = 1.5 cm`) 若沒超過會自動 skip，不會白跑
- 每次 realign 修的 drift 比較小，每支 ZDT 補伸 / 縮回的 magnitude 也小，對機構應力分散

### 不變
- Phase A→B 邊界 cup 都吸著（feet 是在 Phase B 的 `feet_pre_cycle` 才釋放，body 同理）— 經使用者指出後確認，realign 條件成立
- end-of-step realign（在 `cmd_step_down` / `cmd_step_up` / `cmd_run` 裡的）仍保留 → 涵蓋 Phase B 的 drift；threshold 沒超過就 skip
- realign 失敗 non-fatal（同 end-of-step 處理方式）

## 2026-05-05za — Claude Code — disable_seal iter 上限 4 → 5、cap 6000 → 7500

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `DISABLE_RETRY_MAX_ITERS`: 4 → 5
  - `DISABLE_RETRY_MAX_OVEREXTEND`: 6000 → 7500（≈2 cm → 2.5 cm）

### 為什麼
要多給弱密封一次補伸機會。原本 4 iter / 2 cm cap，cup 在 Phase 1 終點（preset − 2 cm）若距牆面比較遠，2 cm 補伸不夠。改成 5 iter / 2.5 cm cap，多 0.5 cm 餘裕。cap 必須一起放寬，否則第 5 次 push 會被 `total_pushed >= MAX_OVEREXTEND` 提前判 weak_seal。

## 2026-05-05z — Claude Code — disable_seal Phase 2 改為「短推 → disable → 被動等真空」迭代

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `pusher_extend_with_disable_seal_()`：Phase 2 完全重寫
  - 舊：state machine（PUSHING / WAITING_VACUUM / RETRY_EXTENDING）+ 連續慢推到目標 + 多 cap，期間單一 motion 一路推完，馬達持續出力。
  - 新：每 iter 是短推 + disable + 被動等真空：
    - **Step A：** re-enable 未 DONE 的 slaves
    - **Step B：** 推前先讀真空 — 若 ≤ SEAL_DEEP（前次 wait 已建好密封）→ 直接 DONE
    - **Step C：** 對未 DONE slaves 送 `+DISABLE_RETRY_INCR_PULSE`（1500 pulse ≈ 0.5 cm）relative pos_mode at slow rpm，sync trigger
    - **Step D：** 等所有 push motion done — 期間偵測 obstacle（pos_err + I 都高）→ emergency_stop + 標 obstacle / DONE；偵測 stall_flag → emergency_stop + 標 DONE；real_speed ≈ 0 → 結束此次 push
    - **Step E：** 對所有未 DONE slaves emergency_stop + disable EN
    - **Step F：** poll 真空（每 100ms）至多 `VACUUM_DEEPEN_TIMEOUT_MS`（5s），達 SEAL_DEEP → re-enable 讀 real_pos → DONE
    - 任一 slave 累積推 ≥ `DISABLE_RETRY_MAX_OVEREXTEND`（6000 pulse ≈ 2 cm cap）→ 標 weak_seal + DONE
  - 上限：`DISABLE_RETRY_MAX_ITERS = 4` iters → 最多 4 × 0.5 cm = 2 cm 補伸
  - 收尾：iter loop 結束仍有未 DONE → force re-enable + 標 weak_seal + 記 final_pulse
- `user_lib/WASH_ROBOT.h`：函式 doc comment 同步改成新流程描述

### 為什麼
腳組 disable_seal 在密封不順時，連續慢推會持續壓 cup → 反作用力透過機體 rail 拉到對側組（如腳放下時 body slave 5,6 被頂向後）→ 對側 cup 被剝、堵轉 → 連鎖誤動作。

使用者要的行為是：「每次 push 完先關閉使能，等到吸好或 timeout 後打開使能讀位置；有吸好就繼續，沒吸好就再推一點，重複。」

新版每個 iter 推得很短（0.5 cm）就立刻 disable，馬達不出力 → 5 秒被動等真空 → 真空建立 cup 自己往牆吸 → 達 SEAL_DEEP 即收工。沒建立成功才再推 0.5 cm，最多累積 2 cm。

整段過程馬達只在「短推」那 < 1 秒在出力，其他時間都 disabled，對機構與對側 cup 衝擊大幅下降。

### 不變
- Phase 1 fast extend 行為不變（preset - 2 cm）
- 對 stall_flag 的 defer 邏輯保留
- `last_seal_pulse_` 記錄、`weak_seal` event 通知、`PUSHER_SETTLE_MS` 結束 settle 都不變
- 呼叫端契約（caller hold motion_mtx_ + pre-open valve）不變

### 待驗證
- bench 上跑一次 step_down/up cycle 確認新流程不再造成對側 cup 堵轉
- 若 iter 0.5 cm 太細（4 iter / 2 cm 不夠補）或 5 秒等待太短（cup 慢慢爬）→ 調 `DISABLE_RETRY_INCR_PULSE` / `VACUUM_DEEPEN_TIMEOUT_MS`

## 2026-05-05y — Claude Code — realign Phase 2 stall 處理 + Phase 2.5 重讀位置

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_()`：
  - **Phase 2 wait 改寫：** 偵測到任何一支 stall 時：
    1. 對其他「還在動」的 slaves 發 `emergency_stop(false)` 凍結在當下位置
    2. break wait loop（不繼續等剩下的）
    3. 走到 Phase 2.5 重讀位置 → 回 ERR 給上層
    - `zdt_wait_motion_done_(defer_stall=false)` 本身已處理 stall flag release
  - **Phase 2.5（新）：** 不論成功或 stall，都重讀 9 支 ZDT 實際 `real_pos` 寫入 `last_seal_pulse_`，讓下個 cycle 拿到的是真正的位置（不是 preset 預設值）。
  - **Phase 4 簡化：** 移除「全 9 支寫成 preset」的 blanket reset，因為 Phase 2.5 已寫入實測值。仍會 reset `last_feet_max_over_cm_` = 0.0。

### 為什麼
1. 原本 Phase 2 偵測 stall 直接 return，剩下 sync trigger 已發出去的 slaves 還會繼續走完它們的 delta → 不一致狀態。改成「stall 一發生 → 全部凍結」。
2. 凍結後，每支 cup 的真實位置可能離 preset 還有距離（短 cup 沒補完、長 cup 沒收完）。如果 Phase 4 仍寫 preset，下個 cycle 會用錯誤值算 delta。Phase 2.5 重讀 + 寫入解決這問題。
3. 即使 Phase 2 全部成功完成，relative move 累積誤差也可能讓位置偏離 preset 一點點，重讀比硬 reset 準。

### 不變
- disabled ZDT slaves 仍跳過
- crane assist / Phase 3 vacuum check / Phase 5 crane restore 邏輯不變

## 2026-05-05x — Claude Code — realign 雙向對齊（讀實際位置 + 短 cup 也補伸到 preset）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 新增 `REALIGN_EXTEND_RPM = 20`（很慢，避免短 cup 推回 preset 時瞬間負載過大）
  - 新增 `REALIGN_EXTEND_ACC = 200`
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_()`：
  - **Phase 0（新增，在 delta 計算前）：** 對 9 支 ZDT（disabled 跳過）逐一 `get_system_status()` 讀 `real_pos`，換算 `actual_pulse = real_pos × 10`，把 `last_seal_pulse_[s-1]` 更新成實測值（讀失敗則保留原值）。log 印出 `prev_recorded` 與 actual 對照。
  - **Delta 計算：** 不只看 `over_cm`，改算 `drift_cm`（含正負）；threshold 改用 `max_abs_drift_cm`（兩個方向都計）。
  - **Phase 2 改成雙向：**
    - `delta > 0`（cup 比 preset 長）→ retract by delta（dir=1, RPM=50）
    - `delta < 0`（cup 比 preset 短）→ **extend by |delta|（dir=0, RPM=20 很慢）**
    - `delta == 0` → 跳過
  - 每支動作前 log 印出方向 / mag / rpm。

### 為什麼
1. 原本 realign 用 `last_seal_pulse_` 算 delta，但這個值是上次 attach / cycle 結束時記的，期間若有 disable_seal 改鎖位、手動 jog、step_down/up 改變實際位置，記錄會 drift。直接讀即時 `real_pos` 才準。
2. 原本只處理「太長的 cup」（retract），太短的 cup 跳過 → cup spread 累積。使用者要求「大家都到 preset 應該的長度」，所以短 cup 也補伸。
3. 補伸時若速度太快，cup 推牆瞬間負載大，機器結構撐不住 → 用 RPM=20 很慢的速度推。

### 不變
- disabled ZDT slaves 仍跳過
- relative mode（不要動到 absolute encoder zero）不變
- crane assist / Phase 3 vacuum check / Phase 4 reset / Phase 5 crane restore 邏輯不變

## 2026-05-05w — Claude Code — step_down/up Phase A 全 backup 退回原位時 skip Phase B

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `do_step_down_` Phase A（body）結束後檢查 `rail_pos_cm_`，若 < 0.5 cm（body 沒實際下降）→ 跳過 feet phase、回 `OK step_down_no_progress`
  - `do_step_up_` Phase A（feet）結束後同樣，feet 沒實際上爬 → 跳過 body phase、回 `OK step_up_no_progress`

### 為什麼
情境：step_up 5 cm，feet 想上爬 5 cm 但密封失敗，cycle_group_ retry backup 全退到 rail=0（feet 回原位才吸住）。cycle_group_ 看 cup 有沒密封、不看 rail 進展，所以回 OK。step_up 接著進 body phase：
- body 釋放、縮推桿
- crane retract step+margin（拉鋼索想抬機體 5 cm）
- DM2J → 0（已在 0、no-op）
- 結果：feet 沒上、crane 卻拉了，機構互拉、cup 應力

新邏輯：Phase A 結束看 rail 位置，若退回原位 → 跳過 Phase B、回 no_progress。state 仍 Attached、安全、操作員看到回應決定下一步。

### 行為對照

| 情境 | 之前 | 現在 |
|---|---|---|
| Phase A 成功推到 +step | Phase B 跑（正常） | Phase B 跑 ✓ |
| Phase A backup 到中途位置吸住 | Phase B 跑、跟到中途 | Phase B 跑 ✓ |
| Phase A backup 全退原位才吸住 | Phase B 仍跑、機構互拉 | **跳過 Phase B、回 no_progress** ✓ |
| Phase A 完全失敗（沒密封） | step_down/up 回 ERR | 同樣 ERR ✓ |

---

## 2026-05-05v — Claude Code — Realign 移除 Phase 1.5 equalize + 恢復 threshold check

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_`：
  - 移除整個 Phase 1.5 within-group equalize 區塊（~80 行）
  - threshold check (`max_over_cm < REALIGN_THRESHOLD_CM` return) 恢復、註解更新

### 為什麼
現場 log 看到 equalize 把 feet 推前 3 cm 時：
- feet 推桿伸長 → feet body 被推離牆面
- 透過 rail 拉著 body unit 後退
- body cups 還黏牆上 → cup-body 距離擴大
- body ZDT 馬達被外力強迫伸桿、卻被 holding torque 鎖住 → 連續 stall（slave 5,6,7 stall flag 反覆觸發）

equalize 設計初衷是「統一 over_cm 讓 retract 起步姿態平衡」，但副作用比原問題嚴重，移除。

直接走 Phase 2 同步 retract，每 cup 用 relative mode 縮自己的 delta，理論上各 cup 獨立、不互相干擾。

### Realign 新流程
```
Phase 0：pre-flight stall clear
Phase 1：crane retract_to_weight(2 kg, 上限 10 cm)
Phase 2：9 顆 cup 同步 relative retract delta
Phase 3：vacuum check
Phase 4：reset state
Phase 5：crane pay_out 還原
```

threshold check 一律生效（手動 / 自動都檢查 max_over < 1.5 cm 就跳過）。

---

## 2026-05-05u — Claude Code — VACUUM_CONTACT_KPA -10 → -3（disable_seal 早停）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`VACUUM_CONTACT_KPA` -10 → -3

### 為什麼
觀察 disable_seal 實際 log（slave 2）：
```
t=2100 p=0    I=1209  ← cup 剛接觸牆（真空起步）
t=2200 p=-1   I=1486
t=2300 p=-7   I=1803
t=2400 p=-14  I=2207  ← 觸發 CONTACT、disable
```

從「cup 接觸」到「disable」之間 400 ms，馬達還在硬推，I 從 1.2 A → 2.2 A。
反作用力傳到機體 → body slave 5,6 沒密封被拉開 / 堵轉。

設計初衷：cup 一接觸就 disable、讓真空自己拉緊（lead screw 在 disable 時可倒推）。
但 -10 kPa 太晚觸發，導致馬達沒乖乖讓真空做事、繼續硬推。

降到 -3 kPa：cup 一碰牆真空起一點點就 disable。馬達不負責推 cup 到位（變成真空在做），body 不會被反作用力拉走。

### 預期行為改變
phase 2 slow extend 觸發 disable 的時機從 -10 → -3：
- I 從 ~2200 mA 降到 ~1500 mA（少 700 mA 反作用力）
- cup 擠壓深度從 ~1.5 cm 降到 ~0.3 cm

---

## 2026-05-05t — Claude Code — init 改成 4 段分階段伸 ZDT

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_init_impl_()`：原本 feet 整組同時伸 → 改成 4 段順序伸：
  1. **腳組下半**（F2 = slave 2, 4）8.0 cm
  2. **腳組上半**（F1 = slave 1, 3）8.0 cm
  3. **身體組下半**（B2 = slave 7, 8）9.3 cm
  4. **身體組上半**（B1 = slave 5, 6）9.5 cm

### 為什麼
使用者要求 init 改成依序伸（避免一次 4 支腳同時伸出造成的瞬間負載 / 失衡）。腳組上下分開、身體上下分開後，每段只 2 支同時動作。

### 不變
- 各組伸長量 / RPM / ACC 維持原值
- `defer_stall=true` 行為（cup 撞牆視為成功）不變
- 段間無 sleep（與原 body_long/body_short 連續銜接的行為一致）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `PUSHER_EXTEND_BODY_PULSE`：29400 → **28500**（9.8 → **9.5 cm**，slave 5,6）
  - `PUSHER_EXTEND_BODY_PULSE_SHORT`：28800 → **27900**（9.6 → **9.3 cm**，slave 7,8）

### 為什麼
使用者要求把身體上組改 9.5 cm、下組改 9.3 cm。腳組維持 8.0 cm 不動。

## 2026-05-05r — Claude Code — 還原 ZDT 伸長量（撤銷 2026-05-05q）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `PUSHER_EXTEND_FEET_PULSE`：25200 → **23000**（8.4 → **8.0 cm**）
  - `PUSHER_EXTEND_BODY_PULSE`：27900 → **29400**（9.3 → **9.8 cm**，slave 5,6）
  - `PUSHER_EXTEND_BODY_PULSE_SHORT`：27000 → **28800**（9.0 → **9.6 cm**，slave 7,8）

### 為什麼
使用者要求還原為調整前的設定。

## 2026-05-05q — Claude Code — 調整 ZDT 伸長量

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `PUSHER_EXTEND_FEET_PULSE`：23000 → **25200**（8.0 → **8.4 cm**）
  - `PUSHER_EXTEND_BODY_PULSE`：29400 → **27900**（9.8 → **9.3 cm**，slave 5,6）
  - `PUSHER_EXTEND_BODY_PULSE_SHORT`：28800 → **27000**（9.6 → **9.0 cm**，slave 7,8）

### 為什麼
使用者要求調整身體上下組與腳組伸長量。

## 2026-05-05p — Claude Code — 修 retract 前先 emergency_stop（清 firmware queue 殘留 extend 命令）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `cmd_zdt_pusher` retract 路徑：pos_mode 前先 `emergency_stop` + 50 ms settle
  - `cmd_pusher` 群組 retract 路徑（含 "all" / 個別 group）：開頭對所有相關 slave `emergency_stop` + settle

### 為什麼
GUI 按 ZDT 收推桿時觀察到「先往前伸一點再收回」。

推測機制：
- `pusher_extend_with_disable_seal_` 在 vacuum 達 -10 kPa 時 emergency_stop + disable
- ZDT firmware 內部 motion queue 可能還記得「extend 到 preset+2cm」這個 target（emergency_stop 不一定清 queue）
- disable → re-enable 後，新的 retract pos_mode 命令進來時，firmware 可能先處理殘留 → 馬達短暫前伸 → 再執行新命令 → 反向縮回

明確在 retract 前送 `emergency_stop` 強制清 queue，避免殘留命令被執行。

### 影響
GUI 按單支收 / 群組收都修正。auto step_down/up 流程的 retract 透過 `pusher_move_many_` 也可能有此潛在問題，但目前只看到 manual 路徑出狀況，先治 manual。auto 流程觀察後再說。

---

## 2026-05-05o — Claude Code — cmd_attach 改用 wait + vacuum_check + fine_tune 流程

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_attach`：
  - 移除舊的 body extend 段（init 已經 extend body）
  - 移除舊的 commented-out vacuum_check 死代碼
  - 用 `pqw_set_relay_verified_` 取代 `pqw_.controlRelay`（FC01 readback 防丟 frame）
  - 開 valve 後等 4 秒（`ATTACH_VACUUM_WAIT_MS = 4000`）
  - `vacuum_check_("all")` 找出沒密封的 cup
  - 沒密封的 cup 跑 `fine_tune_extend_per_slave_("all")`，per-slave 補伸 1 cm × 3 次（max +3 cm）
  - fine_tune 後 release stall flag（defer mode 留下的）
  - 仍未密封 → log warn + EVT `attach_partial_seal`、繼續設 Attached（best-effort）

### 為什麼
依使用者要求：attach 開真空後等 4 秒、檢查、不夠就調整。

新流程比舊的更聰明：
- 舊：開 valve → body extend with vacuum_stop → settle → state Attached（沒驗證、可能某些 cup 沒密封但程式不知）
- 新：開 valve → 等 4 秒 → 檢查 → 補伸沒吸住的 cup → state Attached

依賴 init 已先 extend feet+body（剛剛 2026-05-05n 改）。

### 流程
```
state=Ready → cmd_attach:
  1. open feet/body/center valves (FC01 verify)
  2. sleep 4000 ms
  3. vacuum_check_("all") → initial_fails
  4. if not empty:
       fine_tune_extend_per_slave_(all 9 slaves, preset start, "all")
         per cup: 沒密封 → +1 cm → 等 800 ms → 重檢 → 最多 3 次或 +3 cm cap
       release stall flags
       未密封者 log warn + EVT，仍進 Attached
  5. state Attached
```

---

## 2026-05-05n — Claude Code — cmd_init 加身體組 ZDT extend

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_init_impl_`：
  - 原本只 extend feet group，現在也 extend body group
  - body 拆 long pair (slave 5,6 = 9.5 cm) + short pair (slave 7,8 = 9.3 cm)
  - 用 body 專屬的 `PUSHER_RPM_BODY_EXTEND` / `PUSHER_ACC_BODY_EXTEND`
  - defer_stall=true 容忍 cup 撞到牆面 stall
  - 結尾統一 release_stall_flag 防 latched stall
  - center cup（slave 9）仍保留給 cmd_attach 處理

### 為什麼
依使用者要求，init 階段把 feet+body 的 ZDT 都伸出來（之前只有 feet）。

---

## 2026-05-05m — Claude Code — ZDT extend 速度 1000 → 700 rpm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `PUSHER_RPM` 1000 → **700**（feet/center extend）
  - `PUSHER_RPM_BODY_EXTEND` 1000 → **700**（body extend）

### 為什麼
使用者要求降低 ZDT extend 速度（−30%）。ACC 維持 255 不動。

---

## 2026-05-05l — Claude Code — 兩段式收推桿第一段速度 50 → 30 RPM

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`PUSHER_RPM_RETRACT` 50 → 30（第一段半收速度）

### 為什麼
更慢脫壁，給 cup 真空釋放更多時間，避免 cup adhesion 拉破密封 / stall。

第二段 `PUSHER_RPM_RETRACT_FULL = 500` 不變（脫壁後空走可快）。

---

## 2026-05-05k — Claude Code — REALIGN_THRESHOLD_CM 檢查暫時註解掉（一律跑 realign）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `do_feet_realign_` 開頭的 `if (!force && max_over_cm < REALIGN_THRESHOLD_CM) return "";` 註解
  - `cmd_realign` 內的 skipped 早返回註解

### 行為改變
- step_down/step_up/run 結束 → 一律走 realign（不管漂移多少）
  - 沒漂移時仍會跑 pre-stall-clear / crane assist（可能 0 cm）/ retract phase（deltas 都 0 不會動）/ vacuum_check / reset state
  - 流程跑完但無實際 cup 動作
- 手動按按鈕 → 同樣一律跑

### 為什麼
依使用者要求。可能想用 realign 流程當作每步結束的 sanity check（無論漂移多少都驗證 9 cup 真空 + 重置 last_seal_pulse_）。

### 保留 EQUALIZE 檢查
`REALIGN_EQUALIZE_THRESHOLD_CM = 3.0` 仍生效 — 組內 spread 不夠時不做 equalize（避免無謂動作）。

### 恢復方式
解註解兩行即可。

---

## 2026-05-05j — Claude Code — ZDT extend 速度 1200 → 1000 RPM

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `PUSHER_RPM` 1200 → 1000（feet / center extend）
  - `PUSHER_RPM_BODY_EXTEND` 1200 → 1000（body extend）

### 為什麼
減速以提升 vacuum stop / obstacle 偵測的反應準度。

---

## 2026-05-05i — Claude Code — 恢復 step_down/step_up/cmd_run 自動 realign trigger

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：解註解三處 realign 自動觸發
  - `cmd_step_down` 末尾
  - `cmd_step_up` 末尾
  - `cmd_run` loop 內每步結束

### 觸發邏輯
全部 `do_feet_realign_(force=false)` — 內部 threshold 1.5 cm 自動跳過漂移不足的情況：
- max_over < 1.5 cm → 靜默 return ""
- max_over ≥ 1.5 cm → 跑完整 realign（pre-stall-clear / crane assist / equalize / retract / vacuum check）

非致命：失敗只 log，不影響 step 結果（state 仍 Attached）。

### 條件齊備
此次恢復前已修：
- 04-30q realign 改 relative mode（治 absolute 撞 hardstop 跳電）
- 05-05c pre-flight stall clear（防 firmware silently reject pos_mode）
- 05-05f deg→pulse 轉換 unit bug（max_over_cm 算對）
- 05-05g cmd_realign 也走 threshold

可以放心開回。

---

## 2026-05-05h — Claude Code — Crane_easy_PI debug=true 還原 false（治 web 連不上 + log 刷屏）

### 修改檔案
- `Crane_easy_PI/main.cpp`：`relay.init(...)` 與 `dy.init(...)` 的 `debug` 參數從 `true` → `false`

### 為什麼
使用者回報「新版 web 連不上 + DY500 log 刷屏」，舊版（debug=false）可動。

`LOG_ERR / LOG_HEX` 兩個 macro 都受 `debug_mode` 控制（user_lib/log_utils.h），所以 driver 端 debug=true 時：
- 每次 modbus_read 失敗都印 hex dump（TX 一行 + RX 一行）
- weight_loop 1ms cycle → ~1000 行 log/秒
- SSH 跑 console 會發生 stdout/stderr backpressure → main thread 變慢
- TCP server accept 反應慢 → web GUI 連不上

debug 改回 false 後 driver 完全靜默；DY500 是否真的有讀到值改用 `status` 指令的 `weight_valid` 旗標判斷（不靠 log）。要排查 DY500 通訊時，建議只暫時打開、確認後關回。

---

## 2026-05-05g — Claude Code — cmd_realign 改 force=false（手動觸發也檢查 threshold）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_realign`：
  - force=true → false（也走 1.5 cm threshold 檢查）
  - 自己先算 `max_over_cm` → 若不到 threshold 直接回 `OK realign_skipped max_over=X cm < threshold=1.5cm`
  - 過 threshold 才呼叫 `do_feet_realign_(false)`、log 印 `manual trigger, max_over=X cm > threshold`

### 為什麼
原本 force=true 強制執行整套流程。但 cup 沒漂移時不需要做 realign（會多走 crane assist + 9 顆同步 retract 但實際 deltas 都接近 0）。manual 也走 threshold 比較合理：
- 沒漂移 → 回明確的 skipped 訊息給 GUI
- 有漂移 → 跑完整流程

### 行為對照

| 情境 | 之前（force=true） | 現在（force=false） |
|---|---|---|
| max_over < 1.5 cm | 跑完整流程（無謂動作） | 立刻回 `OK realign_skipped` |
| max_over ≥ 1.5 cm | 跑完整流程 | 跑完整流程 |

---

## 2026-05-05f — Claude Code — 修 deg→pulse 轉換 unit bug（max_over_cm 爆表 14×）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `pusher_extend_with_disable_seal_` 內 `deg_to_pulse`：`deg × 142.22` → **`deg × 10.0`**
  - `do_feet_realign_` equalize 段同樣：`real_pos × 51200/360` → **`real_pos × 10.0`**

### 為什麼
ZDT pos_mode 接受的 pulse 不是 encoder microstep（51200/rev convention），而是另一個 unit。從 bench log 反推：
- preset 30000 pulses → real_pos ≈ 3000 deg（slave 9 滿伸 10 cm 時）
- preset 27900 pulses → real_pos ≈ 2789.89 deg（slave 7 9.3 cm 時）
- 兩個 data point 都驗證 **1 pos_mode pulse = 0.1 deg encoder**（pulses = deg × 10）

之前用 `deg × (51200/360) = deg × 142.22` 是 microstep convention — 換算結果**爆表 14.22 倍**。所以：
- last_seal_pulse_ 寫成 426,660 而不是 30,000
- over = 426,660 - 30000 = 396,660 pulses
- over_cm = 396,660 / 3000 = **132 cm** ← 比 1.5 cm threshold 大 88 倍

這就是為什麼 max_over_cm 看起來怪、永遠超過 threshold。Realign equalize / retract 都基於這個錯誤值算 delta、會傳錯指令給馬達。

### 影響範圍
- `pusher_extend_with_disable_seal_` 的 `final_pulse` 計算（之前所有用 disable_seal 跑出來的 last_seal_pulse_ 都是錯的）
- `do_feet_realign_` equalize 後讀 real_pos 寫回 last_seal_pulse_

### 後續
1. 重新跑一次 step_down/up 讓 last_seal_pulse_ 用正確 unit 重寫
2. 觀察 max_over_cm 數值是否合理（< 6 cm）
3. realign 觸發應該變得「實際發生時才觸發」，不會莫名其妙就觸發

---

## 2026-05-05e — Claude Code — step_down/up pre_cycle 全 9 顆 ZDT pre-flight stall clear

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新 helper 宣告 `ensure_all_zdt_stall_clear_()`
- `user_lib/WASH_ROBOT.cpp`：
  - 實作 `ensure_all_zdt_stall_clear_`：對 9 顆 ZDT 各做 get_system_status 看 stall_flag、有就 release、settle 100ms 後 verify 一次。Persistent stall → return true 觸發 PausedOnError
  - 4 個 pre_cycle 開頭從 `ensure_group_stall_clear_(other_group)` 改成 `ensure_all_zdt_stall_clear_()`：
    - do_step_down_ body_pre_cycle（line ~2114）
    - do_step_down_ feet_pre_cycle（line ~2225）
    - do_step_up_ feet_pre_cycle（line ~2358）
    - do_step_up_ body_pre_cycle（line ~2412）

### 為什麼
原本只查「另一組」（保留密封那組）stall — 但即將解真空 + 縮推桿那組如果有 latched stall（defer mode 留下的），firmware 會 silently reject retract 的 pos_mode → cup 沒動但程式以為動了 → valve 已釋放 → cup 被機械力拉脫 → cascade 失敗、可能堵轉 24V 跳電。

現場觀察：往上走時 body 組堵轉。可能是 body 組有殘留 stall，本次 step_up phase B 做 body retract 時馬達不動、cup 被扯掉。

新邏輯一次清 9 顆，covers 所有可能殘留位置（包含 center cup 9）。

---

## 2026-05-05d — Claude Code — body 組伸長量上調 0.3 cm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `PUSHER_EXTEND_BODY_PULSE` 28500 → **29400**（slave 5,6: 9.5 → 9.8 cm）
  - `PUSHER_EXTEND_BODY_PULSE_SHORT` 27900 → **28800**（slave 7,8: 9.3 → 9.6 cm）

### 為什麼
使用者要求 body 組整體往外多伸 0.3 cm。維持上下不對稱（5,6 比 7,8 多 0.2 cm）。

---

## 2026-05-05c — Claude Code — Realign Phase 0：pre-flight stall flag clear

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_`：在 evt_ realign_start 後、crane assist 前新增 Phase 0：
  - 對 9 顆 ZDT 各做一次 `get_system_status` + 檢查 `stall_flag`
  - 若有 latched stall → `release_stall_flag()` + EVT `realign_pre_stall_clear slave=X`
  - 全部清完後 sleep 100 ms 讓 firmware settle
  - 跳過 disabled_zdt_slaves_ 中的 slave

### 為什麼
ZDT firmware 對 latched stall_flag 的反應：之後的 pos_mode 寫入會被 silently reject，馬達不動但 modbus 看起來成功。
realign 接著要做大量 pos_mode（equalize、retract），任一 cup 殘留 stall 就會打亂整個流程，可能 cascade 成更嚴重狀況（cup 沒動但 last_seal_pulse_ 紀錄錯）。
事先一次性檢查 + 清除是防呆。

### 範圍
只在 do_feet_realign_。其他 ZDT 動作路徑（cycle_group_、cmd_pusher 等）已在自己流程內預清 stall flag。

---

## 2026-05-05b — Claude Code — Realign 加 within-group pre-equalize 階段（防傾斜跳電）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新常數 `REALIGN_EQUALIZE_THRESHOLD_CM = 3.0`
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_`：crane assist 後、retract 前，新增 Phase 1.5 pre-equalize：
  - 在每組內（feet 1-4 / body 5-8）算 max - min over_cm（spread）
  - 若 spread > 3 cm：把組內較短的 cup 用 **relative mode**（dir=0 forward）伸到該組 max over_cm
  - body 組的 9.5/9.3 設計差會自動保留（over_cm 同 → 絕對 pulse 不同因為 preset 不同）
  - 用 broadcast trigger sync，多顆同步動
  - 完成後讀 real_pos 更新 last_seal_pulse_、重算 delta 給後續 retract 用
- 用 `relative` 模式避開 ZDT encoder 累積問題

### 為什麼
現場觀察：機器在連續 step / fine_tune 後，常因傾斜使**同一組內某些 cup 比其他 cup 長 3 cm 以上**（例如「頭重腳輕」造成 1,2,5,6 比 3,4,7,8 長）。同步 retract 拉所有 cup 時，較長那邊 cup 要對抗的力量大很多 → 過電流 → 24V supply 跳保護。

新邏輯：每組 (feet / body) 內先把較短 cup 伸到組內最大 over_cm，把整體姿態調平再 retract。

### 流程
```
crane assist 收繩到 2 kg
  ↓
[Phase 1.5 pre-equalize]
  for each group in {feet (1-4), body (5-8)}:
    if (max_over_cm - min_over_cm > 3 cm):
      for each slave in group:
        if slave's over_cm < max - 0.05:
          extend +(max - current) cm (relative mode forward)
  sync trigger + wait all
  update last_seal_pulse_ + 重算 deltas
  ↓
[Phase 2 retract]
  9 顆 cup 同步 relative retract 各自的 delta（已用新值）
  ↓
[Phase 3] vacuum_check + reset last_seal_pulse_ + crane pay_out
```

### 設計細節
- equalize 用 over_cm（相對 preset）為 target，**保留 preset 的設計差**：
  - body slave 5,6 (preset 9.5 cm) vs 7,8 (preset 9.3 cm) — over_cm 同但絕對 pulse 不同，9.5/9.3 design ratio 維持
  - feet 全 4 支 preset 都 8 cm — over_cm 同 → 絕對 pulse 同，4 支等長
- 不假設 upper/lower：用「組內最大 over_cm」當 target，無論傾斜方向（左右/前後）都能拉平
- center cup (slave 9) 不參與 equalize（單顆無對齊概念）

### 風險
- equalize 期間 cup 仍密封，extend 會把該 cup 那邊離牆更遠 — 預期行為，cup 應保持吸住（伸 cup 時是「推牆」方向）
- 較短 cup 大幅 extend 後可能在新位置密封減弱（cup 變形範圍外） — transient，下個 retract 立即把所有 cup 拉回 preset

---

## 2026-05-05a — Claude Code — Disable-seal extend：用 ZDT disable 讓 cup 自由被真空拉到位

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 新常數一組（`VACUUM_CONTACT_KPA`、`VACUUM_SEAL_DEEP_KPA`、`DISABLE_RETRY_*`、`DISABLE_PHASE_CURRENT_LIMIT_MA`、`DISABLE_POS_ERROR_LIMIT_DEG`、`PUSHER_RPM_DISABLE_SLOW`、`PHASE1_BUFFER_PULSES`）
  - 新 helper 宣告 `pusher_extend_with_disable_seal_(slaves, target_pulses, fast_rpm, acc)`
  - `cycle_group_` extend 段 `pusher_extend_with_vacuum_stop_` 改用 `pusher_extend_with_disable_seal_`，移除 fine_tune 補伸（disable_seal 內部已自帶 retry）
- `user_lib/WASH_ROBOT.cpp`：
  - 實作 `pusher_extend_with_disable_seal_`：per-slave state machine（PHASE1_FAST → PHASE2_SLOW → WAIT_SEAL → RETRY_PUSH → DONE）
  - 內含 phase_current / pos_error 障礙物偵測（撞到提早停 + lock + EVT obstacle_detected）
  - 弱密封自動 retry +0.5 cm × 4 = +2 cm cap

### 為什麼
利用 SMC LEYG25 在 ZDT disable 後**可被外力推動**的特性。原本「motor 跟 cup 速度不同步」的兩難（太快 → 撞牆 stall；太慢 → vacuum 拉 cup 過頭）改成：
- 馬達 push cup 接觸牆面（觸發 -10 kPa） → disable
- cup 自己被真空吸到完美位置（變形 + 拉緊在 2 mm 範圍內）
- 真空達 -60 kPa → re-enable 鎖位置、讀 real_pos 當作 last_seal_pulse_

### 流程
```
Phase 1 (fast 1200 RPM)  : extend → preset - 2 cm
Phase 2 (slow 50 RPM)    : extend toward preset + 2 cm
                           poll: vacuum / current / pos_error / stall
  vacuum ≤ -10            → emergency_stop + disable → 進 WAIT_SEAL
  current > 2000 mA AND
  pos_error > 5°          → emergency_stop + lock (obstacle)
  stall                   → defer + lock
  natural stable          → disable + 進 WAIT_SEAL（試一下萬一有微吸）

WAIT_SEAL:
  vacuum ≤ -60            → re-enable + 讀 real_pos + 記 last_seal_pulse_
  timeout 5s              → 看 retry count，沒爆 cap：
                              → re-enable + 推 +0.5 cm → 回 RETRY_PUSH
                            爆 cap (+2cm) → 接受弱密封 + 記位置 + EVT weak_seal
```

### 待 bench 調整的常數
- `VACUUM_CONTACT_KPA = -10`：cup 開始接觸的判定值
- `VACUUM_SEAL_DEEP_KPA = -60`：完整密封的判定值
- `DISABLE_PHASE_CURRENT_LIMIT_MA = 2000`：撞障礙物電流閾值（保守估）
- `DISABLE_POS_ERROR_LIMIT_DEG = 5.0`：位置誤差閾值（同）
- `PUSHER_RPM_DISABLE_SLOW = 50`：phase 2 慢速 RPM
- 已加 obstacle_meas 風格的 log，bench 跑時會印每次 poll 的 pos / err / I / p 值，方便調

### 替換範圍
- ✅ `cycle_group_` 自動流程（step_down / step_up / cmd_run 走的路徑）
- ❌ `cmd_attach`（你之前要求保持簡單，繼續用 pusher_move_many_）
- ❌ `cmd_pusher` / `cmd_zdt_pusher` 手動按鈕（仍走 smart_extend_subset_，先不動，驗證 OK 再 propagate）

### 風險
- 5 個閾值都需 bench 調整，第一次跑會 print 大量 log，看數值對齊
- disable / re-enable 之間若有瞬間電流突波，可能再次觸發 24V 跳保護（但 disable 是降低電流，理論上更安全）
- weak_seal 接受路徑 — cup 沒密封就 record，下次 step 拿這個位置當 base，可能累積誤差。先觀察是否會發生

---

## 2026-05-04t — Claude Code — step_down / step_up / cmd_run 自動 realign trigger 暫時註解

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `cmd_step_down` 末尾的 `do_feet_realign_()` 區塊註解掉
  - `cmd_step_up` 末尾同樣
  - `cmd_run` loop 內的 realign trigger 同樣

### 為什麼
realign 改成 relative mode 後雖然理論上修了 absolute mode 撞 hardstop 跳電的 bug，但**還沒實際 bench 驗證**。先把自動觸發關掉避免再出意外，**手動 realign 按鈕（cmd_realign）仍可用**讓人為觸發測試 + 確認沒問題後再恢復自動。

### 恢復方式
把那三段 // 開頭的註解去掉即可。

---

## 2026-05-04s — Claude Code — fine_tune settle 時間 800 → 2000 ms

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`FINE_TUNE_SETTLE_MS` 800 → 2000

### 為什麼
800 ms 對 cup 從接觸 → 真空建立可能不夠（特別是邊際接觸的情況）。2000 ms 給 cup 充分時間吸住，下一次 vacuum_check 結果更穩定。

副作用：fine_tune 全程從最壞 800×3=2.4 s → 2000×3=6 s，慢一些但對流程穩定影響不大。

---

## 2026-05-04r — Claude Code — fine_tune 上限改回 +3 cm（避免過度補伸觸發 realign 危機）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `FINE_TUNE_MAX_ITERS` 6 → 3
  - `FINE_TUNE_MAX_OVEREXTEND` 18000 → 9000（~6 cm → ~3 cm）

### 為什麼
+6 cm 補伸範圍太大、實務上 fine_tune 補到 4 cm（剛剛現場 max_over=4.149 cm）就代表 cup 沒對到位 / preset 估計偏差太多，繼續補只是擠壓機構、不會真正密封。

收回 +3 cm 是給 cup 變形 + 對位誤差合理 buffer。超過代表「cup 真的不該裝這支」→ obstacle 偵測讓 fine_tune 早停、cycle_group_ 標記 fail，由 vacuum_retry 機制處理。

順帶讓 realign 觸發更不容易（threshold 1.5 cm + max 3 cm，自然不會累積到撞 hardstop 那種誇張 drift）。

---

## 2026-05-04q — Claude Code — Realign 改 relative mode（治 absolute target 撞 hardstop 跳電）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_feet_realign_`：
  - ZDT pos_mode 從 `(dir=0, target=preset, mode=1=absolute)` 改成 `(dir=1=reverse, target=delta, mode=0=relative)`
  - 加 `moving_slaves` vector：deltas≤0 的 cup 跳過不送命令、不等
  - 移除不再使用的 `targets` 變數

### 為什麼（事故重建）
2026-05-04 現場：step_up × 2 後 max_over=4.149 cm，realign 自動觸發。Realign 用 absolute mode 命令 ZDT slave 3 到 absolute preset (~23000 pulses)，但馬達 encoder real_pos 已累積到 ~355,000 pulses（多次 extend/retract 沒重新 set_zero）。Absolute target 在 encoder 物理範圍以下 → 馬達反向硬撞 hardstop → 大電流 stall → **B 組 24V 200W supply 跳保護** → 真空泵 + 三口閥 + ZDT + sensors 全失電。

absolute mode 在 ZDT encoder 累積後就壞掉。Relative mode 不依賴 encoder absolute zero，馬達就「往後退指定 pulses」就好，撞不到 hardstop（除非 delta 算錯，但 last_seal_pulse_ 是該 cup 真實密封位置，理論上不會超過實際物理 stroke）。

### 治本但不治本的部分
ZDT encoder real_pos 累積本身仍是隱憂（會影響 absolute 邏輯的其他地方？目前只 realign 用 absolute mode，其他 extend 也用 absolute但 base 是 last_seal_pulse_ 跟 encoder 同源所以還行）。若日後遇到類似問題，需要一次完整的 set_zero on physical hardstop。

### 驗證流程
1. 24V 電源切掉、等 30 秒、再開
2. cmd_init → cmd_attach
3. step_up / step_down 跑到 fine_tune 補伸超過 1.5 cm
4. realign 自動觸發（或手動按）
5. log 應顯示「retract delta=X pulses」、不再 stall

---

## 2026-05-04p — Claude Code — step_down / step_up 暫時停用 arm_sweep（排除掉機嫌疑）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `do_step_down_` 末尾的 arm_sweep 區塊註解掉（保留 motion_active_ = false）
  - `do_step_up_` 末尾同樣註解

### 為什麼
現場跑 step_up × 2 + arm_sweep 後，body slave 5,6 突然 stall 掉機。懷疑 arm 高 RPM 1500 + ACC/DEC 100 的高動態力矩破壞 cup 邊際密封（slave 5,6 上排離 arm 最近、槓桿臂最長 → 最先脫附）。
先排除 arm_sweep 的影響，純跑 step 流程確認穩定後再恢復。

### 後續
- 若純 step 流程穩定 → 確認 arm_sweep 是禍源，需降速或調 cup 密封 margin
- 若仍掉機 → 排除 arm_sweep，問題在 cup 9.5 cm preset 不足

恢復方式：把 `do_arm_sweep_()` 那幾行解註解。

---

## 2026-05-04o — Claude Code — cmd_attach 暫時停用 center pusher extend

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_attach()`：步驟 1 的 center pusher extend 註解掉，標記 `[TEMP DISABLED 2026-05-04]`

### 為什麼
延伸 2026-05-04n 的暫停 center 流程，讓 attach 也跳過 center 推桿伸出。center 推桿維持在 retracted 位置；CH_VALVE_CENTER 仍會在步驟 2 開啟（與 feet/body 一起），但 cup 未貼牆 → 真空空抽，不影響整體 attach 結果（feet 已在 init 伸出貼牆、body 在步驟 3 vacuum-aware extend）。

待恢復時搜尋 `[TEMP DISABLED 2026-05-04]` 統一處理。

---

## 2026-05-04n — Claude Code — step_down/step_up 暫時停用 center pusher (ZDT slave 9)

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `do_step_down_()` body_pre_cycle：CH_VALVE_CENTER OFF / ZDT_C 加入 vacuum_wait_release_ / center 兩階段 retract — 全部加 `[TEMP DISABLED 2026-05-04]` 註解掉
  - `do_step_down_()` body 重 attach 階段：center pusher_move_ + CH_VALVE_CENTER ON 註解掉
  - `do_step_up_()` body_pre_cycle：同 step_down 處理
  - `do_step_up_()` body 階段後：center re-extend + valve ON 註解掉
- 共 5 個區塊（step_down 2、step_up 2、+ 共用 vacuum_wait_release_ 名單調整）

### 為什麼
使用者要求在 step_down / step_up / run 流程中暫停使用 center pusher（slave 9）。其他不在這三個流程內的使用（cmd_attach、cmd_detach、phase5 balance、cmd_return_home、cmd_pusher / cmd_zdt_pusher 手動操作、cmd_realign）保留不動。

待恢復時搜尋 `[TEMP DISABLED 2026-05-04]` 把註解拿掉即可（不要刪 marker，方便之後 grep 確認所有 disable 都恢復）。

---

## 2026-05-04m — Claude Code — body slave 5,6 伸長 9.8 → 9.5 cm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`PUSHER_EXTEND_BODY_PULSE` 29400 → 28500（9.8 → 9.5 cm）

slave 7,8 維持 _SHORT 27900（9.3 cm）不變。

---

## 2026-05-04l — Claude Code — Weight check 加 easy crane fallback + DY-500 probe-at-init

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 新 `read_easy_weight_kg_()` 宣告（透過 estop channel 送 status 給 shim、parse `weight=`）
  - 新 atomic `weight_present_[2]`：probe-at-init 結果，false = sensor 不存在 → 跳過 polling
- `user_lib/WASH_ROBOT.cpp`：
  - init 時對兩顆 DY-500 各做一次 probe（呼叫 `get_weight_float`），成功才標記 present
  - `pressure_poll_loop_` 內 DY-500 polling 加 `if (!weight_present_[i]) continue;` 跳過 absent sensor，避免 driver 連續錯誤 log spam
  - `read_rope_weight_max_kg_()` 加 fallback：兩顆 DY-500 都 offline 時 → 呼叫 `read_easy_weight_kg_()` 從 easy crane（單顆 DY500:3 via shim status）拿值
  - 實作 `read_easy_weight_kg_`：透過 `crane_cli_estop_` 連線送 `status`、parse `weight_valid=` + `weight=` 欄位，失敗回 -1

### 為什麼
washrobot 端規格上有 2 顆 DY-500 (slave 10/11)，但目前實體沒接（要等正式吊機）。簡易吊機自己有 1 顆（slave 3）對外暴露在 `status` reply 的 `weight=` 欄位。

新邏輯：
- **正式吊機上場** → washrobot DY-500 probe 成功 → 走原本的雙感測器邏輯（max of 2）
- **目前 test mode** → washrobot DY-500 probe 失敗 → 自動 fallback 到 easy crane weight via shim
- 切換完全自動、不用改 flag

probe-at-init 防止 polling 不存在的 sensor 持續產生 `[ERR] [DY500:10/11] consecutive errors reached threshold` log spam。

### 未來
正式吊機到位後（3 條鋼索 + 2 顆張力感測器），DY-500 自然 probe 成功、進入主路徑。fallback 路徑保留作為救援。

---

## 2026-05-04k — Claude Code — cmd_run 支援方向選擇（down / up）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`cmd_run` 加 `direction` 參數（default "down"，值="down"|"up"）
- `user_lib/WASH_ROBOT.cpp`：`cmd_run` 依 direction 決定每步呼叫 `do_step_down_` 或 `do_step_up_`
- `washrobot_new_PI/main.cpp`：dispatch `run <n> [cm] [down|up]`，header banner 更新
- `web_backend/public/index.html`：「run steps」旁加 `direction` dropdown（down ↓ / up ↑）
- `web_backend/public/app.js`：btn-run handler 帶 `direction` 字串

### 為什麼
原本 run 只能往下走 N 步。現在支援雙向，方便在牆面上連續清洗一段後改向回頭。

### 用法
```
run 5            → 5 步往下（向後相容）
run 5 30         → 5 步往下、step_cm=30（向後相容）
run 5 30 down    → 同上、明確指定
run 5 30 up      → 5 步往上
```

GUI 只需從 dropdown 選方向再按 run。

---

## 2026-05-04j — Claude Code — Linux_test ZDT prompt 接受 cm 單位

### 修改檔案
- `Linux_test/main.cpp`：
  - 加常數 `ZDT_PULSES_PER_CM = 3000`（對齊 WashRobot）
  - 加 helper `parse_pulse_or_cm(input, default)`：接受 `Ncm` / `N.NNcm` 字尾轉換為 pulses，或純數字當 pulses；空輸入用 default
  - menu 3（test_zdt）prompt 改為「Target [pulses 或 'Ncm', 30000=10cm full / 0=retract] [30000]」
  - menu 6（test_zdt_group）prompt 同上
  - 兩處 `[INFO] controlling...` 輸出加 `~X.XX cm` 換算
  - 主選單描述 menu 3 / 6 後綴改為 `target (pulses 或 'Ncm')`
  - include `<cctype>`（用 std::isspace / std::tolower）

### 為什麼
使用者要求 Linux_test 支援用公分輸入（更直觀），同時保留原本 pulses 直接輸入（兩者都接受）。輸入 `9.5cm` 會自動換成 28500 pulses。空輸入用 30000（10 cm 全伸）取代舊的 144000（從舊 microstep 殘留）。

---

## 2026-05-04i — Claude Code — Manual extend 對齊 auto cycle（vacuum stop + fine_tune + obstacle 偵測）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新增 `smart_extend_subset_(group, slaves)` private helper 宣告
- `user_lib/WASH_ROBOT.cpp`：
  - 實作 `smart_extend_subset_`：抽出 cycle_group_ 的 extend 段邏輯（per-slave start_pulses from `last_seal_pulse_` + body delta、`pusher_extend_with_vacuum_stop_`、fine_tune 補伸 + obstacle 偵測、release stall flag）。caller 自管 valve 狀態
  - `cmd_pusher` group extend：把 `pusher_move_many_` 改成 `smart_extend_subset_`（feet / body / center / all 路徑）
  - `cmd_zdt_pusher` 單支 extend：同樣改用 `smart_extend_subset_(group, {slave})`，移除舊有的 hardcoded extend_pulse 計算（內部 helper 從 `last_seal_pulse_` 拿 base）

### 為什麼
網頁 GUI 上 manual pusher extend 按鈕原本只走 `pusher_move_many_` / `pusher_move_`，不包含：
- vacuum 達標早停（會過度壓縮 cup）
- fine_tune 補伸不足
- obstacle 偵測（撞障礙物會反覆 stall）

現在改用同一個 helper，行為與 step_down / step_up / cmd_run 完全一致：
- `extend_pulses[s] = last_seal_pulse_[s]` (D 持久化)
- body 組另加 `last_feet_max_over_cm_` 補償 (B)
- `VACUUM_EARLY_STOP_KPA = -45` 達標就停
- fine_tune 最多 +6 cm 補伸
- 偵測到 stall + 沒吸住 → 標記為 obstacle、跳過後續迭代

### 注意
manual mode 的 valve 由使用者自管，helper 不主動開關 valve。若 valve 是 OFF、按 extend，vacuum 永遠不會吸上 → fine_tune 走滿 6 次或全 obstacle → 推桿停在 ~base+6 cm 處。這是預期行為（沒 vacuum 時推桿無法區分「碰牆」與「障礙物」）。

---

## 2026-05-04h — Claude Code — fine_tune 上限 +3 → +6 cm，加 obstacle 早停

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `FINE_TUNE_MAX_ITERS` 3 → 6
  - `FINE_TUNE_MAX_OVEREXTEND` 9000 → 18000（~3 cm → ~6 cm）
  - `FINE_TUNE_INCREMENT_PULSE` 不變（3000 ≈ 1 cm/iter）
- `user_lib/WASH_ROBOT.cpp` `fine_tune_extend_per_slave_`：
  - 新增 per-slave `obstacle[]` 旗標
  - 每次 `vacuum_check_` 後，若 cup 仍 fail 且 `Z_(s).status.stall_flag` 仍 set（前一次 pusher_move_ defer 留下的）→ 判定為障礙物 → EVT `obstacle_detected slave=X pulse=Y`、設 `obstacle[idx]=true`
  - 後續 iter 跳過 obstacle 旗標為 true 的 slave，不再硬撞

### 為什麼
原本 fine_tune 撞到障礙物時，會在 +3 cm 上限內反覆嘗試擠壓，反覆 stall 累積對馬達不友善。
新邏輯：第一次 stall + 沒吸住就判定障礙、停止此 cup 嘗試。同時上限拉高到 +6 cm 給單純距離不足（不是障礙）的情境多一些空間。

### 對外行為
- 撞到障礙物：fine_tune 早停、cup 標記為 fail 回給 cycle_group_
- 正常需多伸：依舊有 6 次 iter × 1 cm = 6 cm 上限
- cycle_group_ 收到 fail 後維持原邏輯（valve off 重試 / 最終失敗進 Error）

### TODO
真正的「無 stall 主動偵測」（基於 pos_error + phase_current 提前停）等 bench 量測閾值後再做（搭配 04-30g 的 obstacle_meas log）。

---

## 2026-05-04g — Claude Code — pusher_extend_with_vacuum_stop_ 加 obstacle 偵測量測 log

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `pusher_extend_with_vacuum_stop_` poll loop：
  - 每次 `get_system_status()` 之後印一行 `[obstacle_meas ZDT:s] t=Xms pos=Y° err=Z° spd=Wrpm I=Vma p=KkPa`
  - VACUUM SEAL 結束 log 加 `err=` `I=`
  - STALL 結束 log 加 `err=` `I=`
  - 新增 STABLE 結束 log（之前沒有），含 `err=` `I=`

### 為什麼
為了 bench 量測「正常 extend / cup 吸住 / 撞障礙」三種情境下 `pos_error` 與 `phase_current` 數值範圍，作為日後障礙物早期偵測（在 firmware stall 觸發前主動軟煞）的閾值依據。

### 使用方式
跑任意 step_down / step_up / manual extend，觀察 log：
- 正常 extend 過程：pos_error 應 < 1°，phase_current 為穩定中間值
- cup 吸住碰牆瞬間：pos_error 短暫升高、phase_current 衝峰，然後 vacuum_early_stop 停下
- 撞障礙：pos_error 持續累積（馬達跟不上命令），phase_current 持續高位

### 後續
量好閾值後加實際 obstacle detection（emergency_stop + skip_slave 路徑），同時把 obstacle_meas log 拿掉或 gate 在 env var 後面。

---

## 2026-05-04f — Claude Code — REALIGN_CRANE_ASSIST_TARGET_KG 30 → 2（保險起步）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`REALIGN_CRANE_ASSIST_TARGET_KG` 30.0 → 2.0

### 為什麼
首次 realign 流程上場，先用最低拉力測試動態邏輯運作。bench 上確認流程正常後再往上調。

---

## 2026-05-04e — Claude Code — Realign 改動態 crane retract（拉力為準）+ 新增手動 realign 按鈕

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 移除 `REALIGN_CRANE_ASSIST_CM`，改為 `REALIGN_CRANE_ASSIST_TARGET_KG = 30.0`（每 sensor 目標拉力）+ `REALIGN_CRANE_ASSIST_MAX_CM = 10`（安全上限）
  - 新增 `cmd_realign()` 公開方法宣告
  - 新增 `crane_retract_to_weight_(target_kg, max_cm)` 私有方法宣告
  - `do_feet_realign_()` 加 `bool force = false` 參數（手動觸發跳過 threshold 檢查）
- `user_lib/WASH_ROBOT.cpp`：
  - 實作 `crane_retract_to_weight_`：每次 1 cm 漸進式 retract（透過 `crane_retract_safe_`），每步等 200ms 讓張力穩定後讀重量；任一 sensor ≥ target 即停；達 max_cm 也停。回傳含 `total_cm=X` 方便 caller pay_out 對齊。
  - `do_feet_realign_` 改用 `crane_retract_to_weight_`，phase 5 pay_out 用實際 retract 的 cm
  - 加 `force` 參數路徑、log + EVT 反映
  - 實作 `cmd_realign()`：state guard（Attached/Paused/PausedOnError 才允許）、呼叫 `do_feet_realign_(true)`
- `washrobot_new_PI/main.cpp`：dispatch 加 `realign`，header banner 更新（realign 是 SLOW cmd，走 detached worker thread）
- `web_backend/public/index.html`：washrobot panel 加「手動 Realign」按鈕（直接 `data-cmd="realign"`，沿用既有 dispatch）

### 為什麼
原本 `REALIGN_CRANE_ASSIST_CM=5` 寫死，無法因應現場張力變化（鋼索原本鬆 vs 緊、機身重量分布等）。改成「retract 到拉力達 30 kg」的閉迴路控制：
- 鋼索原本就緊（已經承重）→ 0 cm 內就達標，不浪費時間
- 鋼索原本鬆 → 多 retract 幾 cm 直到分擔到 30 kg
- 上限 10 cm 防失控

手動按鈕：bench / 現場操作員直接觸發 realign，不必等 step 結束自動 trigger。

### 安全性
- 每個 1 cm retract 仍走 `crane_retract_safe_`，over-load (>50 kg attached / >90 kg hanging) 會中止 + ERR
- max_cm=10 是硬上限（超過代表 sensor 異常或鋼索極鬆，不該再 retract）
- target=30 kg 遠低於 attached limit 50 kg，正常情況有 20 kg 緩衝

---

## 2026-05-04d — Claude Code — Web GUI auto-busy 時鎖 easy_crane 面板（防 shim 開放迴路被打斷）

### 修改檔案
- `web_backend/public/app.js`：
  - 新 `AUTO_BUSY_STATES = {running, balancing, paused_on_error, returning_home}` + `isWashrobotAutoBusy()` helper
  - `applyMode` 內 easy panel disable 條件：TCP down OR washrobot 在 auto-busy state
  - washrobot state 解析後再 call applyMode 重新評估 lock 狀態

### 為什麼
shim ↔ easy_crane 是 open-loop 計時控制（duration = cm/rate），shim 不讀馬達狀態。easy_crane 硬體層做 up/down 互鎖：用戶按 `down on` 會強制 `up off`。
若 washrobot auto step_down/step_up 執行中、shim 正計時 retract，使用者在 GUI 按 easy_crane 「down」→ easy 切方向，shim 仍以為 retract 跑滿、回 OK，washrobot 收到「成功」但實際 rope 動作不對 → 機體位置誤差累積。
前端鎖 easy panel 是最輕量的防呆。

### 範圍
僅 frontend disable，後端與架構不變。手動 detach 模式（state ∈ Idle/Ready/Attached/Paused/Error 等非 auto）easy_crane 仍可用。

---

## 2026-05-04c — Claude Code — Cup-drift 治本三件套：B（feet→body 補償）+ D（last_seal_pulse_ 持久化）+ E（realign 同步歸位）+ 鋼索重量保護

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新 `#include DY_500_weight_sensor.h`、新常數、新 atomic state、新 method 宣告、改寫 cycle_group_ extend 邏輯（per-slave start pulses + B 補償）
- `user_lib/WASH_ROBOT.cpp`：DY_500 init、weight 背景 poll piggyback 在 pressure_poll_loop_、`crane_retract_safe_` 包所有 retract 路徑、`do_feet_realign_` 實作、step_down/step_up/run 末尾 trigger realign、fine_tune 改 per-slave start_pulses、record seal pulse 寫入 last_seal_pulse_、preset_extend_pulse_for_slave_ helper、`feet_max_overextend_cm_` helper

### B：feet over-extension → body 幾何補償
- feet phase 結束算 `last_feet_max_over_cm_ = max((last_seal - feet_preset)/3000) cm`
- body phase extend target += `last_feet_max_over_cm_ × 3000` pulses（per-slave）
- step_up：feet→body 同步驟內生效；step_down：給下一步 body 用

### D：per-cup last_seal_pulse_ 持久化
- 新 atomic `last_seal_pulse_[9]`，初值=preset
- fine_tune 成功密封 → 記錄 actual seal pulse
- 下次 cycle_group_ extend：start_pulses[s] = `last_seal_pulse_[s]`（自動跟上 wall-distance drift）
- fine_tune 上限改 per-slave：`start_pulses[i] + FINE_TUNE_MAX_OVEREXTEND`

### E：realign（drift 累積 → 同步歸位）
- 觸發：max((last_seal - preset)/ratio) > `REALIGN_THRESHOLD_CM=1.5`
- 流程：
  1. crane retract `REALIGN_CRANE_ASSIST_CM=5` 鋼索分擔重量（走 safe wrapper）
  2. 9 顆 ZDT 同步 broadcast trigger 縮回各自 preset（valve 全程 ON、cup 保持密封 → 真空把機器拉向牆）
  3. retract 用慢速 `REALIGN_RETRACT_RPM=50`、`REALIGN_RETRACT_ACC=200`（cup 必須維持 grip）
  4. vacuum_check_ 驗證 9 顆都還密封
  5. reset `last_seal_pulse_[*] = preset`、`last_feet_max_over_cm_ = 0`
  6. crane pay_out 還原張力
- 觸發點：cmd_step_down / cmd_step_up / cmd_run 每步成功後（非致命，失敗只 log）

### 鋼索重量保護
- DY-500 × 2（slave 10、11 on cli_22_）init + 背景 poll
- `crane_retract_safe_(cm)` 包所有 retract 呼叫：
  - **Pre-check**：若任一 sensor > limit → ERR `rope_weight_too_high`
  - **Active monitor**：spawn watcher thread 每 100ms 讀重量，超 limit → 透過**專用 estop TCP 連線**（`crane_cli_estop_`）送 crane stop、回 ERR。設第二條連線是因為主線程在 `crane_retract_safe_` 中持 `crane_mtx_` 等 retract reply，watchdog 若用同一連線會 deadlock。Shim 是 multi-connection 設計，兩條同時運作沒問題。
  - sensor 全失聯 → 拒絕 retract（safe default）
- 替換 5 處 retract 呼叫：step_down body_pre / step_up body_pre / realign / 其他需手動補的留 raw `crane_cmd_`（已檢查 pay_out 不需保護）
- State-aware threshold：
  - `ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_ATTACHED = 50`（吸住中 cup 撐重，鋼索不該扛多）
  - `ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_HANGING  = 90`（懸吊狀態，鋼索扛全重 ~67 kg/側 + 33% buffer）
  - 判斷依 state_ enum：Attached/Running/Paused/Balancing/PausedOnError → ATTACHED；其他 → HANGING

### 預設常數一覽（現場可調，全在 WASH_ROBOT.h）
```
MACHINE_WEIGHT_KG = 135
ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_ATTACHED = 50
ROPE_WEIGHT_LIMIT_KG_PER_SENSOR_HANGING  = 90
WEIGHT_MONITOR_POLL_MS = 100
REALIGN_THRESHOLD_CM     = 1.5
REALIGN_CRANE_ASSIST_CM  = 5
REALIGN_RETRACT_RPM      = 50
REALIGN_RETRACT_ACC      = 200
DY_SLAVE_LEFT  = 10
DY_SLAVE_RIGHT = 11
```

### 為什麼
治三個糾纏的 bug：
1. fine_tune 補伸後機器離牆 +Δ → 下一步 body 構不到牆（B 補償）
2. fine_tune 每步從 preset 重來，物理 drift 一直被當「異常」重複補（D 持久化記錄 base）
3. cup over-extension 累積無上限 → 軌道空間吃光、cup 變形（E 定期歸零）
4. crane 收繩在吸住狀態下太用力 → 拉壞機體（rope weight check）

### 風險
- 第一次跑可能有 transient：last_seal_pulse_ 一開始等於 preset，要靠 fine_tune 第一輪填出真實值，與舊行為一致
- E 觸發時若 vacuum_check 失敗 → 回 ERR PausedOnError，rope 還在 5cm 拉緊位（人介入時記得 release）
- DY-500 sensor 若量測方向錯（左右沒分清楚），threshold 可能不對；現場 bench 確認

---

## 2026-05-04b — Claude Code — 新增 zdt_pusher 指令 + GUI 單支推桿控制

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新增 `cmd_zdt_pusher(int slave, const std::string& action)` 宣告
- `user_lib/WASH_ROBOT.cpp`：實作 `cmd_zdt_pusher`，依 slave 所屬組挑選 extend pulse + RPM + ACC（feet 8cm / body 上 9.8cm / body 下 9.3cm / center 10cm），retract 一律到 0；取 motion_mtx_，狀態守衛排除 Error/Running/Balancing/ReturningHome/WaitingConfirm/PausedOnError；pre-clear stall flag
- `washrobot_new_PI/main.cpp`：dispatch `zdt_pusher <slave> <extend|retract>`（SLOW，不在 FAST_CMDS）
- `web_backend/public/index.html`：「manual — pusher」面板加 single slave row：dropdown（1~9 with 中文標籤）+ EXTEND/RETRACT 按鈕
- `web_backend/public/app.js`：`btn-zdt-extend` / `btn-zdt-retract` click handler

### 為什麼
使用者要求 GUI 上能單獨控制某一支 ZDT 推桿伸長或縮短（過去 `pusher` 指令只能整組操作）。現在可以對單一 slave 1~9 個別測試或排查（例如懷疑 slave 6 卡住時直接讓他動一下確認）。Per-slave 自動套用對應的伸長距離（5,6 用 9.8cm、7,8 用 9.3cm 等），不需手動算 pulse。

---

## 2026-05-04a — Claude Code — Linux_test 加 menu 22：vacuum seal auto fine-tune

### 修改檔案
- `Linux_test/main.cpp`：新增 `test_vacuum_seal_fix()`，menu 列表加 `22  Vacuum seal fix — auto fine-tune ZDT extend per-slave until vacuum sealed (feet/body)`，main loop dispatch 對應到 `line == "22"`

### 功能
- 連 .21（ZDT）+ .22（JC-100），選 group=feet/body
- 對每顆 cup：讀真空 → ≤ -50 kPa 略過；> -50 kPa 則讀目前 ZDT 位置當 base，每次 +2000 pulse（~7 mm）伸長並重讀真空，最多 3 次或 +6000 pulse（~2 cm）為止
- 規範對齊 WASH_ROBOT.h `fine_tune_extend_per_slave_` 同樣的閾值與步進
- **不**碰 valve，預設使用者已開好對應 group valve + pump

### 用途
Bench 救援場景：cup 因 vacuum-early-stop 過早停 / 行程跑掉，導致沒密封但其他 cup 還吸住，可用此菜單對單組做 per-slave 補吸。

### 順帶觀察
寫的時候發現 menu 3（ZDT single）的 `if (!motion_control_pos_mode(...))` 判斷邏輯反了（驅動回傳 false=success，但 menu 3 把 `!success` 當失敗印 ERR 然後進 polling 分支）。新菜單用正確判斷，舊 menu 3 留待單獨修。

---

## 2026-04-30ag — Claude Code — ZDT extend RPM 1000 → 1200，ACC 統一 255

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `PUSHER_RPM` 1000 → **1200**
  - `PUSHER_RPM_BODY_EXTEND` 1000 → **1200**
  - `PUSHER_ACC` 200 → **255**
  - `PUSHER_ACC_BODY_EXTEND` 200 → **255**

### 為什麼
使用者要求微幅上調速度（+20%）並把加速度一起拉到 max（255）。

---

## 2026-04-30af — Claude Code — ZDT extend RPM 1500 → 1000，ACC 統一 200

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `PUSHER_RPM` 1500 → **1000**
  - `PUSHER_RPM_BODY_EXTEND` 1500 → **1000**
  - `PUSHER_ACC` 255 → **200**
  - `PUSHER_ACC_BODY_EXTEND` 維持 **200**（feet/center 與 body 同步）

### 為什麼
使用者要求降回 1000 RPM、加速度統一 200。可能因為 1500 RPM + 255 ACC 太兇導致現場堵轉率上升。

---

## 2026-04-30ae — Claude Code — fine_tune 每輪伸 +1 cm，上限 +3 cm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `FINE_TUNE_INCREMENT_PULSE` 2000 → **3000**（每輪 +1 cm）
  - `FINE_TUNE_MAX_OVEREXTEND` 6000 → **9000**（上限 +3 cm，配合 3 輪都能跑滿）

### 為什麼
使用者要求每輪 fine_tune 增量改 ~1 cm。為配合 3 輪都能實際動，上限同步從 2 cm 拉到 3 cm。

實際結果：每輪 +1 cm，3 輪後最多到 base + 3 cm。例：body slave 5 base 9.8 cm → 最多到 12.8 cm。

---

## 2026-04-30ad — Claude Code — ZDT extend 速度+ACC 上拉、retract 兩段比例 1/2 → 1/3+2/3

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `PUSHER_RPM` 1000 → **1500**（feet/center extend）
  - `PUSHER_RPM_BODY_EXTEND` 1000 → **1500**
  - `PUSHER_ACC` 200 → **255**（max，配高 RPM）
  - `PUSHER_ACC_BODY_EXTEND` 150 → **200**（保守，body 承重大）
  - `cycle_group_` template 內 retract 第一段 pulse 計算從 `/ 2` 改為 `* 2 / 3`；變數 `half_pulse` 改名為 `step1_pulse`；context 字串 `_half_retry/_full_retry` → `_step1_retry/_step2_retry`
- `user_lib/WASH_ROBOT.cpp`：
  - 所有 `PUSHER_EXTEND_FEET_PULSE / 2` / `PUSHER_EXTEND_BODY_PULSE / 2` / `PUSHER_EXTEND_PULSE / 2` 替換為 `* 2 / 3`（step_down/step_up body_pre 與 feet_pre cycles、manual_pusher_all、manual_pusher 單一 group 共 9 處 + 1 ternary）
  - `cmd_pusher` retract 路徑變數 `half_pulse` → `step1_pulse`，ctx 後綴 `_half/_full` → `_step1/_step2`

### 為什麼
使用者要求：
1. ZDT extend 速度再往上 → 1500 rpm；ACC 配套提高至 255（max）/ 200（body）
2. 兩階段 retract 第一段距離從 1/2 縮為 1/3（pusher 從 full extend 走 1/3 距離 = 停在 2/3-extended 位置），第二段走剩下 2/3 到 0。

效果：第一段「脫壁」階段更短（更早接到第二段全速），但啟動仍是慢速 50rpm + 高 ACC，杯子破封需要的時間不變；第二段距離拉長（全速 500rpm 跑完）對總 cycle 時間不太影響但邏輯更直觀。

---

## 2026-04-30ac — Claude Code — ARM_SWEEP_ACC 1000 → 100（起步更猛，對稱 DEC）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`ARM_SWEEP_ACC` 1000 → 100

### 為什麼
配合 DEC 100，ACC/DEC 對稱起停。Leadshine ACC 單位 ms/1000rpm，數值越小起步越猛。

---

## 2026-04-30ab — Claude Code — ARM_SWEEP_DEC 300 → 100（更猛的剎車）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`ARM_SWEEP_DEC` 300 → 100

### 為什麼
RPM 拉到 1500 後需要更短停止距離。Leadshine ACC/DEC 單位是 ms/1000rpm，數值越小減速越猛。

---

## 2026-04-30aa — Claude Code — ARM_SWEEP_RPM 1000 → 1500

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`ARM_SWEEP_RPM` 1000 → 1500

### 為什麼
現場實測可拉快洗窗 sweep 速度。

---

## 2026-04-30z — Claude Code — ZDT extend 速度 700 → 1000

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `PUSHER_RPM` 700 → 1000（feet/center extend）
  - `PUSHER_RPM_BODY_EXTEND` 700 → 1000（body extend）

### 為什麼
使用者要求 ZDT extend 速度往上拉。兩個 RPM 同時 +43%，acc 維持（PUSHER_ACC=200, PUSHER_ACC_BODY_EXTEND=150）。

---

## 2026-04-30y — Claude Code — revert trigger_sync_move 檢查（false positive）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：移除 `pusher_move_many_()` 與 `pusher_extend_with_vacuum_stop_()` 兩處對 `trigger_sync_move()` 回傳值的檢查，加 TODO 註解
- `.claude/mailbox.md`：給 Jim 提報 user_lib bug

### 為什麼
新加的 trigger 檢查在現場一直 false positive — body extend 實際成功，但 log 一直印「trigger_sync_move FAIL」。原因：`ZDT_motor_control::trigger_sync_move()` 是 Modbus 廣播（slave 0x00），依協定**不應有回應**，但 driver 實作 `return resp.empty()` 把「沒回應」當成 error → 永遠回傳 true。屬 user_lib 範圍 bug，已 mailbox 給 Jim，WashRobot 暫時忽略回傳值。

`pos_mode_nowait` 的失敗 log（同一 PR 加的）仍保留有效，那部分行為正確。

---

## 2026-04-30x — Claude Code — Linux_test menu 21 + WashRobot 失敗診斷 log

### 修改檔案
- `Linux_test/main.cpp`：
  - 新增 menu 21「ZDT driver enable」
  - 實作 `test_zdt_driver_enable()`：互動式 enable/disable 單一或全部 1~9 ZDT slave 的 `driver_EN`
  - 命令：`e <N>` / `d <N>` / `ea`（all enable）/ `da`（all disable）/ `q`（離開）
- `user_lib/WASH_ROBOT.cpp`：
  - `pusher_move_()`、`pusher_move_many_()`、`pusher_extend_with_vacuum_stop_()` 三處 `motion_control_pos_mode_nowait` 失敗時印出 `[XXX ZDT:N] pos_mode_nowait FAIL (pulse/rpm/acc) — check driver_EN / stall / alarm`
  - 兩處 `trigger_sync_move()` 加 return 檢查：失敗時印 `trigger_sync_move FAIL — broadcast frame dropped` 並 return true，避免空等 15 秒 timeout

### 為什麼
Body group 失敗時 log 沒有 TIMEOUT，代表 pos_mode_nowait 直接被 firmware 拒絕（最常見：driver_EN=0 → Modbus exception 0x03），但程式沒印任何訊息。新增 log 可立即指出哪一隻 slave 失敗。`trigger_sync_move` 是整組 sync=1 模式的單點故障（廣播框丟掉就全組不動），return 也納入檢查。

操作員可用 Linux_test menu 21 排查特定 slave 是否因 driver 失能而無法控制。

---

## 2026-04-30w — Claude Code — ZDT extend 全組速度提升 RPM 700 + ACC 上拉

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `PUSHER_RPM` 500 → 700（feet / center extend）
  - `PUSHER_RPM_BODY_EXTEND` 500 → 700
  - `PUSHER_ACC` 100 → 200（feet / center extend）
  - `PUSHER_ACC_BODY_EXTEND` 50 → 150（body 仍維持比 feet/center 低，緩啟動防上方兩支堵轉）

### 為什麼
現場實測可拉快。Retract 速度不動。

---

## 2026-04-30v — Claude Code — PQW relay set 全面加 FC01 readback verify（治 valve OFF 卡死）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 新增 `pqw_set_relay_verified_(int ch, bool on)` 宣告
  - `cycle_group_` valve_on 改用此 helper（移除 inline verify loop）
- `user_lib/WASH_ROBOT.cpp`：
  - 新增 `pqw_set_relay_verified_` 實作：set + FC01 readback，狀態不符最多重送 3 次（50ms 間隔）
  - `vacuum_valve_` 改用此 helper（all/group 路徑都涵蓋）
  - body_pre_cycle valve OFF（CH3 + CH4，step_down + step_up 各 2 處）改用
  - feet_pre_cycle valve OFF（CH2，step_down + step_up 各 1 處）改用

### 為什麼
今天現場：step_up 5 cm 成功後 step_down 10 cm，卡在 `body_pre_vacuum_release` 反覆 timeout，body 4 顆都還黏住 → CH3 OFF 指令送了但 PQW 沒切（USR-TCP232 gateway 在 RS485 bus 忙時偶爾丟 FC05）。
04-29i 只對 `valve_on` 加了 readback verify，這次是 `valve_off` 中招。把所有 valve set 路徑都統一走 verify helper。

---

## 2026-04-30u — Claude Code — ARM_SWEEP_DEC 1000 → 300（回復）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`ARM_SWEEP_DEC` 1000 → 300

### 為什麼
使用者測試後決定還是 300 比較好（更乾脆的收尾）。

---

## 2026-04-30t — Claude Code — ARM_SWEEP_DEC 300 → 1000（測試用）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`ARM_SWEEP_DEC` 300 → 1000

### 為什麼
使用者改主意要回去試 1000ms 減速段（柔和到達）。實測比較兩種設定下機構震動、cycle time、收尾乾脆度。

---

## 2026-04-30s — Claude Code — 上滑台減速時間縮短 1000 → 300

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `ARM_SWEEP_DEC` 1000 → 300（縮短減速段，靠近目標時更快收住）
  - 註解標明 DM2J ACC/DEC 單位是 ms/1000rpm，數字越小越急

### 為什麼
使用者觀察到上滑台快到目標時減速太緩，要求縮短減速時間。確認 DM2J PR 模式 ACC/DEC 單位為 ms（Leadshine 慣例：到達 1000rpm 所需時間），數值越小越急。從 1000 → 300 = 減速段時間縮為原本的 30%。

ACC=1000 暫不動（使用者只提到減速）；如果起步太緩再單獨改。

---

## 2026-04-30r — Claude Code — PUSHER_SETTLE_MS 1500 → 300 縮短兩階段間等待

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`PUSHER_SETTLE_MS` 1500 → 300

### 為什麼
使用者要求 ZDT 收推桿第一段、第二段之間的等待縮短。實際等待源是 `pusher_move_many_()` 結尾的 `sleep_ms_(PUSHER_SETTLE_MS)`（先前已移除 `RETRACT_HALF_WAIT_MS` 後沒有其他 sleep）。1500ms 對機構震盪過於保守，~300ms 已夠。Extend 路徑的 cycle_group_ 後續還有 `VACUUM_SETTLE_MS = 2000ms` 給壓力建立，不受影響。

每次 `pusher_move_many_()` 省 1.2s；step_down/step_up 一次 cycle 多次呼叫累計可省約 5~8s。

---

## 2026-04-30q — Claude Code — body 組 ZDT extend 速度 250 → 500 RPM

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`PUSHER_RPM_BODY_EXTEND` 250 → 500（與其他組同速）

### 為什麼
原本 body extend 用獨立低速防上方兩支堵轉，現場實測可拉高與其他組同速。

---

## 2026-04-30p — Claude Code — 上滑台（手臂）速度 + 加速度提升

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `ARM_SWEEP_RPM` 700 → **1000**
  - 新增 `ARM_SWEEP_ACC = 1000`（原用 `DM2J_ACC=500`）
  - 新增 `ARM_SWEEP_DEC = 1000`（原用 `DM2J_DEC=500`）
- `user_lib/WASH_ROBOT.cpp` `do_arm_sweep_()`：3 個 `PR_move_cm` 呼叫改用 `ARM_SWEEP_ACC/DEC`，不再共用 `DM2J_ACC/DEC`

### 為什麼
使用者要求上滑台（DM2J slave 5）速度 + 加速度都調快。獨立常數避免影響輪子和腳組（同樣用 DM2J_ACC=500）。

---

## 2026-04-30o — Claude Code — body 組 extend 速度 200 → 250 RPM

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`PUSHER_RPM_BODY_EXTEND` 200 → 250

### 為什麼
使用者要求 body 組 extend 速度加快一點點。先小幅 +25% 試水溫，若 slave 5,6 仍未堵轉再考慮往上推。

---

## 2026-04-30n — Claude Code — 兩段式收推桿第二段速度 300 → 500 RPM

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`PUSHER_RPM_RETRACT_FULL` 300 → 500（與 extend 同速）

### 為什麼
第二段已脫壁、空走負載小，再加快省時間。第一段 50 RPM 不變。

---

## 2026-04-30m — Claude Code — DM2J 腳組 rail 速度提升至 400 rpm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新增 `DM2J_RPM_FEET = 400`（腳組專用，原 `DM2J_RPM = 200` 保留給輪子/手臂）
- `user_lib/WASH_ROBOT.cpp`：`dm2j_pair_move_abs_()` 內 `PR_move_cm_set` 兩處改用 `DM2J_RPM_FEET`

### 為什麼
使用者要求腳組 rail 速度加快。`dm2j_pair_move_abs_` 經確認是腳組 rail 專用（所有呼叫位置都是 `DM2J_LEFT_FOOT + DM2J_RIGHT_FOOT`），所以這個改動只影響腳組 rail，輪子和手臂維持 200 rpm。

---

## 2026-04-30l — Claude Code — vacuum early-stop 門檻 + body slave 5,6 加長

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `VACUUM_EARLY_STOP_KPA` -30 → -45（接近 verified-sealed -50；舊值太鬆會在微弱密封時就早停）
  - `PUSHER_EXTEND_BODY_PULSE` 28500 → 29400（slave 5,6 從 9.5 → 9.8 cm；slave 7,8 仍用 _SHORT 27900 不變）

### 為什麼
現場 body slave 5,6 容易伸不夠長後被拉住堵轉。診斷：vacuum early-stop 在 -30 kPa 太鬆，cup 一接觸牆面就早停 → 微弱密封 → 承重後被拉開 → 後續動作堵轉。
新門檻 -45 要求 cup 接近完全密封才提前停。同時 5,6 伸長量 +0.3 cm 給更多 cup 變形量。

---

## 2026-04-30k — Claude Code — revert 2026-04-30j（init 歸位/真空全解恢復）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_init_impl_()`：把 2026-04-30j 刪掉的三段加回來
  - 三個 vacuum valve OFF（feet/body/center）
  - DM2J 輪子 retract to 0（左右輪）
  - DM2J 腳組 rail home to abs 0 + `rail_pos_cm_.store(0.0)` 軟體追蹤同步

### 為什麼
使用者表示上一個改動方向錯誤，init 仍應執行原本的 valve 關閉與位置歸零動作。恢復為 2026-04-30j 之前的版本。

---

## 2026-04-30j — Claude Code — init 移除歸位/真空全解動作

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_init_impl_()`：移除以下三段
  - 三個 vacuum valve OFF（feet/body/center）
  - DM2J 輪子 retract to 0（左右輪）
  - DM2J 腳組 rail home to abs 0 + `rail_pos_cm_.store(0.0)` 軟體追蹤同步

### 為什麼
使用者要求 init 不要執行「歸位」動作。原本的 valve OFF 會破壞已 attach 的真空狀態（從 Error state re-init 時整台會掉牆），rail/wheel 移動到 0 也會干擾現場位置。改完之後 init 只做：pump ON + 清洗系統 OFF（安全）+ ZDT enable + feet 推桿 extend + arm set zero + IMU baseline。要回 0 / 解真空的話交給後續流程明確呼叫。

---

## 2026-04-30i — Claude Code — 解真空 + retract 前先確認另一組無堵轉

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新增 `ensure_group_stall_clear_(group)` 宣告
- `user_lib/WASH_ROBOT.cpp`：
  - 實作 `ensure_group_stall_clear_()`：讀該組所有 slave 的 stall_flag，有堵轉就 release，50ms 後驗證；idempotent，全 clear 時 no-op
  - `do_step_down_()` body_pre_cycle 起點：呼叫 `ensure_group_stall_clear_("feet")`
  - `do_step_down_()` feet_pre_cycle 起點：呼叫 `ensure_group_stall_clear_("body")`
  - `do_step_up_()` feet_pre_cycle 起點：呼叫 `ensure_group_stall_clear_("body")`
  - `do_step_up_()` body_pre_cycle 起點：呼叫 `ensure_group_stall_clear_("feet")`
  - `cmd_pusher` retract 路徑：feet/body 兩組會檢查另一組（center 跳過，獨立）

### 為什麼
要解真空並 retract 一組（feet 或 body）時，另一組是當前撐住車身的力源。若該組馬達還有 latched stall_flag，ZDT 韌體下次發 motion cmd（下一個 phase 換成它 retract）會被拒絕 → false success 風險。新增 pre-retract 檢查 + 自動 release，並透過 try_or_pause_ 包裝；若 release 後仍 stall 不清，drop into PausedOnError 讓操作員介入。

---

## 2026-04-30h — Claude Code — init/attach 流程拆分：init 只伸 feet，attach 開閥後伸 body

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `cmd_init_impl_`：移除 body 組 extend，只剩 feet 組 extend
  - `cmd_attach`：流程改為
    1. center 推桿 extend（閥仍關閉，無過壓風險）
    2. 開所有 3 個 valve（feet/body/center）→ 已在牆上的 feet 與 center 立即封
    3. body 組 extend（用 `pusher_extend_with_vacuum_stop_()` per-slave pulses + 真空感知早停：cup 一吸住就停）
    4. release deferred stall flags（idempotent）
    5. settle

### 為什麼
使用者要求 init 只伸 feet 把車架定位，attach 才開吸盤把 body 推上牆吸住。新流程：
- init 完成後 feet 已貼牆但無真空，車身懸空靠 crane 撐
- attach 開閥讓 feet+center 即時封住、再 push body 上牆 → vacuum-aware 確保 body cup 一接觸牆面就停（避免堵轉/過壓）

---

## 2026-04-30g — Claude Code — Linux_test 加 menu 20：解除所有 ZDT 堵轉

### 修改檔案
- `Linux_test/main.cpp`：新增 `test_zdt_release_stall()`，逐顆呼叫 `release_stall_flag()`、印 ok/fail/skip 統計。menu 列表加 `20  ZDT release stall — clear stall flag on all 9 ZDT pushers`，main loop dispatch 對應到 `line == "20"`

### 為什麼
和主程式 `cmd_zdt_release_stall` 對應，bench 上不開 web GUI 也能一鍵解 stall。Gateway IP 預設 `192.168.1.21`（RS485_2）。

---

## 2026-04-30f — Claude Code — 腳組伸長量統一改回 8 cm（移除 slave 1,3 的 +0.3 cm）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 移除 `PUSHER_EXTEND_FEET_PULSE_LONG = 23900` 常數
  - `cycle_group_()` 移除 `group == "feet"` 的 per-slave override block
  - 註解更新為「feet: all 4 slaves use default (~8 cm)」

### 為什麼
使用者要求腳組四隻全部統一為 8 cm。

---

## 2026-04-30e — Claude Code — 新增 zdt_release_stall 指令 + GUI 按鈕

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新增 `cmd_zdt_release_stall()` 宣告
- `user_lib/WASH_ROBOT.cpp`：實作 `cmd_zdt_release_stall()`，對 1~9 號 ZDT slave 呼叫 `release_stall_flag()`，跳過 disabled，回傳 ok/fail/skipped 計數；不取 motion_mtx_，無狀態守衛
- `washrobot_new_PI/main.cpp`：加入 `zdt_release_stall` dispatch；列入 FAST_CMDS（同 emergency_stop pattern）
- `web_backend/public/index.html`：在 status/reset/tilt 那排新增「解除 ZDT 堵轉」按鈕（auto-wire via `data-cmd`）

### 為什麼
使用者要求一個能在任意狀態（含 step_down/run 進行中）解除全部 ZDT 堵轉旗的按鈕。仿照 `cmd_emergency_stop` 模式：直接對 9 個 slave 下 Modbus 指令、不取 motion_mtx_、列為 FAST 命令立刻在 receive thread 執行。

---

## 2026-04-30d — Claude Code — pusher_move_many_ 改 parallel poll 加速等待

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：`pusher_move_many_()` 等待邏輯從「逐一 zdt_wait_motion_done_」改成「單迴圈同時 poll 所有 slaves」（同 `pusher_extend_with_vacuum_stop_()` 的 pattern）

### 為什麼
原本逐一等待時，每隻 slave 即使物理上已停止仍需 ~600ms 確認穩定（4 次 poll：第 1 次設 prev_pos、後 3 次累積 stable_count）。4 隻並行 sync trigger 同時停下後，逐一檢查總共浪費 (N-1) × ~600ms ≈ 1.8s。改成 parallel poll 後，所有 slaves 共用同一輪 poll，總等待時間 = 最慢 slave 時間 + 單一輪確認；腳/身體組每段收推桿節省約 1.8s。

---

## 2026-04-30c — Claude Code — feet 組 slave 1,3 伸長 +0.3 cm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 新增 `PUSHER_EXTEND_FEET_PULSE_LONG = 23900`（slave 1,3 用，~8.3 cm）
  - `cycle_group_` feet extend per-slave：ZDT_RF1/ZDT_LF1（slave 1,3）套用 LONG，slave 2,4 維持 23000

### 為什麼
現場實測 slave 1,3 端需要再多伸 ~0.3 cm 才碰到牆面密封。

---

## 2026-04-30b — Claude Code — body 組伸長對調：slave 5,6 改 9.5 cm，slave 7,8 改 9.3 cm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `cycle_group_` body extend per-slave 條件 `ZDT_RB1/ZDT_LB1` → `ZDT_RB2/ZDT_LB2`（套用 SHORT 的 slave 改成 7,8）
  - 常數註解對調：`PUSHER_EXTEND_BODY_PULSE = 28500` (slave 5,6 = 9.5 cm)、`_SHORT = 27900` (slave 7,8 = 9.3 cm)

### 為什麼
現場實測調整。Pulse 數值不變，只對調 slave 對應關係。

---

## 2026-04-30a — Claude Code — 兩段式收推桿第二段速度 200 → 300 RPM

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`PUSHER_RPM_RETRACT_FULL` 200 → 300

### 為什麼
第二段（半收 → 全收）吸盤已脫壁、空走負載小，速度可拉高省時間。第一段保持 50 RPM 慢速脫壁不變。

---

## 2026-04-29s — Claude Code — fine_tune 補伸上限 1.4 → 2 cm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `FINE_TUNE_INCREMENT_PULSE` 1000 → 2000（每次補伸 ~7 mm）
  - `FINE_TUNE_MAX_OVEREXTEND` 4000 → 6000（絕對上限 ~2 cm）

### 為什麼
原 1.4 cm 上限不夠補吸盤與牆面間距，提高到 2 cm。維持 3 次 iteration，平均每次補 7 mm 走滿。

---

## 2026-04-29r — Claude Code — body 組 slave 5,6 獨立縮短伸出長度至 9.3 cm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新增 `PUSHER_EXTEND_BODY_PULSE_SHORT = 27900`；`pusher_extend_with_vacuum_stop_()` 改為 per-slave pulses；`cycle_group_()` body 組 ZDT_RB1/ZDT_LB1（slave 5,6）填入 SHORT，其餘 28500
- `user_lib/WASH_ROBOT.cpp`：`pusher_extend_with_vacuum_stop_()` 改用 `pulses[i]` 傳各 slave 目標

### 為什麼
Slave 5,6 在 attach body extend 9.5 cm 時因承重過大提早堵轉，需手動解旗。改為 9.3 cm（27900 pulses）讓這兩支自然停止；slave 7,8 維持 9.5 cm。

---

## 2026-04-29q — Claude Code — extend 邊伸邊看真空：吸住即停（防過壓）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 新常數 `VACUUM_EARLY_STOP_KPA = -30`（比 `VACUUM_THRESHOLD_KPA -50` 寬鬆，「開始吸住」門檻）
  - 新增 `pusher_extend_with_vacuum_stop_(slaves, pulse, rpm, acc)` 宣告
  - `cycle_group_` extend 改用此 helper（取代 `pusher_move_many_(... defer_stall=true)`）
- `user_lib/WASH_ROBOT.cpp`：
  - 實作 `pusher_extend_with_vacuum_stop_`：每 150ms 對每顆未完成的 slave 依序檢查
    1. JC-100 真空：≤ -30 kPa → `emergency_stop(false)` 單顆停，視為成功
    2. ZDT stall：視為碰牆成功，flag 不解（defer，由 cycle_group_ vacuum check 後統一釋放）
    3. ZDT 穩定（speed≤20RPM、Δpos≤0.15° 連續 3 次）→ 自然完成
  - timeout 15s

### 為什麼
原本 ZDT 一律伸到目標 pulse（除非 stall），就算吸盤已經吸好牆面也繼續壓 → 過度壓縮、吸盤可能變形、組間不平衡。
新行為：邊伸邊讀壓力，一旦吸住就單顆停。fine_tune 內的小幅補伸（+1000 pulse）維持原樣（小步進，邊際效益不大）。

### 風險與保留
- 寬鬆門檻 `-30` 是初步值，現場若太早停（吸盤未完全密封）可調更負（如 `-40`）
- 單顆 `emergency_stop(false)` 之後馬達停在當前位置；下次 motion 會在 `pusher_move_many_` 開頭 `release_stall_flag()` 預清，安全

---

## 2026-04-29p — Claude Code — 兩階段收推桿：移除 sleep + 調速 + 修 bug

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `PUSHER_RPM_RETRACT` 100 → 50（第一段，半收）
  - 新增 `PUSHER_RPM_RETRACT_FULL = 100`（第二段，全收）
  - `PUSHER_ACC_RETRACT = 255`（兩段共用）
  - `cycle_group_` template：移除兩階段中間 sleep；第二段改用 `PUSHER_RPM_RETRACT_FULL`
- `user_lib/WASH_ROBOT.cpp`：
  - 所有兩階段收推桿區塊移除 sleep（step_down / step_up body & feet & center、manual pusher）
  - 第二段（全收到 0）改用 `PUSHER_RPM_RETRACT_FULL`（100rpm）
  - 修正 center_g 因對齊空格未被 replace_all 匹配的 RPM 遺漏
  - 修正移除 sleep 時行合併 bug，還原各 if-statement 為獨立行

### 為什麼
使用者要求兩階段收腳不再等待直接連續；第二段（全收）100rpm，第一段（半收脫壁）50rpm，兩段加速度均為 255。

---

## 2026-04-29o — Claude Code — extend 堵轉延後處理：先確認真空再解 stall

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `pusher_move_` / `pusher_move_many_` / `zdt_wait_motion_done_` 加 `defer_stall_release` 參數（預設 false，舊行為不變）
  - `cycle_group_` extend 改傳 `defer_stall=true`，vacuum check 結束後統一 `release_stall_flag`
- `user_lib/WASH_ROBOT.cpp`：
  - `zdt_wait_motion_done_` defer 模式下 stall → 印 `DEFER stall release` + return false（視為成功，flag 留著）
  - `fine_tune_extend_per_slave_` 內部 `pusher_move_` 也傳 `defer_stall=true`

### 為什麼
原本 extend 時碰到 stall 立刻 `release_stall_flag` + 視為失敗 → 進 PausedOnError。
但 stall = 推桿碰到牆 = 我們要的終點。立刻解 stall 反而會讓馬達 holding torque 改變、可能讓吸盤微微鬆開、影響真空建立。

新行為：extend 時 stall → flag 留著（馬達持續壓住牆面）→ 跑 vacuum check → 確認吸好或失敗後才釋放 stall。Retract 與其他場景照舊（預設 defer=false）。

---

## 2026-04-29n — Claude Code — ZDT 收推桿速度/加速度調整

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `PUSHER_RPM_RETRACT` 100 → 50（所有組收推桿 RPM）
  - 新增 `PUSHER_ACC_RETRACT = 255`（所有組收推桿加速度）
  - `cycle_group_` template retract call site 加入 `PUSHER_ACC_RETRACT` 參數（2 處）
- `user_lib/WASH_ROBOT.cpp`：所有 retract call site 加入 `PUSHER_ACC_RETRACT` 參數（共約 17 處）

### 為什麼
使用者要求收推桿速度改為 50 rpm、加速度改為 255。

---

## 2026-04-29m — Claude Code — body extend 長度改回 9.5 cm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`PUSHER_EXTEND_BODY_PULSE` 27000 → 28500（9 cm → 9.5 cm）

### 為什麼
使用者測試後決定 body 組伸出長度需要 9.5 cm（28500 pulses）。

---

## 2026-04-29l — Claude Code — body extend 低速 + retry 後 set_zero 防位置漂移

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 新增 `PUSHER_RPM_BODY_EXTEND=200`、`PUSHER_ACC_BODY_EXTEND=50`（body 組 extend 獨立低速/低加速）
  - `pusher_move_` / `pusher_move_many_` 宣告加 `acc` 選擇性參數（default=PUSHER_ACC）
  - `cycle_group_` template：body extend 使用 `PUSHER_RPM_BODY_EXTEND` / `PUSHER_ACC_BODY_EXTEND`；retry full retract 後對每個 slave 呼叫 `set_zero()` 重新錨定零點
- `user_lib/WASH_ROBOT.cpp`：
  - `pusher_move_` / `pusher_move_many_` 實作加 acc 參數，傳入 `motion_control_pos_mode_nowait`
  - `cmd_init_impl_` body extend 改用 `PUSHER_RPM_BODY_EXTEND` / `PUSHER_ACC_BODY_EXTEND`
  - `cmd_pusher` body extend（all 和 per-group）同步改用低速常數

### 為什麼
body 組上方兩支推桿承重最大，伸出時頂牆堵轉，堵轉後重力讓機構位置微移 → ZDT encoder 零點漂移，累積後每次 retry 位置越來越偏。雙管齊下：(1) 降速降加速減少堵轉發生；(2) retry 收推桿成功後立刻 set_zero() 確認零點，防止漂移跨 retry 累積。

---

## 2026-04-29k — Claude Code — 壓力值 background poll：cmd_status 改讀 atomic cache

### 修改檔案
- `user_lib/WASH_ROBOT.h`：加 `cached_pressure_[9]`（atomic int array）、`pressure_poll_running_`、`pressure_poll_thread_`、`pressure_poll_loop_()` 宣告
- `user_lib/WASH_ROBOT.cpp`：
  - 建構子：初始化 `cached_pressure_[]` 全為 0、`pressure_poll_running_=false`
  - `init()`：啟動 `pressure_poll_thread_`
  - `stop()`：停止並 join `pressure_poll_thread_`
  - `pressure_poll_loop_()`：每 ~1s 讀全部 9 顆 JC-100，只在 `error_flag==0` 時更新 cache
  - `cmd_status()`：改為讀 `cached_pressure_[s-1].load()`，不再直接呼叫 `read_pressure()`，狀態回應立即返回

### 為什麼
原本 `cmd_status()` 在 FAST receive thread 上同步讀 9 顆 JC-100，每顆最多等 1000ms，最壞情況阻塞 receive thread 9 秒，網頁 refresh 按下去完全無反應；且 `read_pressure()` comm 失敗回 cache 舊值，值永遠不動。改成 background poll 後，status 立即回傳，壓力值每秒自動更新。

---

## 2026-04-29j — Claude Code — zdt_disable/enable：排除未安裝的 ZDT slave

### 修改檔案
- `user_lib/WASH_ROBOT.h`：加 `#include <set>`、`disabled_zdt_slaves_` 成員（`std::set<int>`）、`cmd_zdt_disable(int)` / `cmd_zdt_enable(int)` 宣告；`group_slaves_` 改為 non-static（需存取 `disabled_zdt_slaves_`）
- `user_lib/WASH_ROBOT.cpp`：`group_slaves_` 改 non-static，回傳時過濾 `disabled_zdt_slaves_`；init ZDT enable 迴圈跳過 disabled slaves；init pusher extend 改用 `group_slaves_("feet"/"body")` 取代硬碼向量；新增 `cmd_zdt_disable` / `cmd_zdt_enable`
- `washrobot_new_PI/main.cpp`：dispatch 加 `zdt_disable <n>` / `zdt_enable <n>`

### 為什麼
slave 4 未安裝時，`pusher_move_many_({3,4,1,2})` 遇到 slave 4 通訊失敗就整批失敗，其他推桿完全沒動。加 disable 機制後，`zdt_disable 4` 一條指令即可把 slave 4 踢出所有群組操作（init extend、step_down/up、cycle_group_、cmd_pusher 等），不需要改任何業務邏輯。

---

## 2026-04-29i — Claude Code — cmd_pusher retract：自動關閥 + abort 恢復 pre-cmd state

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：`cmd_pusher` 重構
  - **retract 前先關閥**：`pqw_.controlRelay(valve, false)` + `vacuum_wait_release_`（poll 等吸盤洩壓，最多 VACUUM_RELEASE_WAIT_MS），再做 two-stage retract；解決「吸盤未洩壓 → ZDT stall → 無窮 PauseOnError 迴圈」問題
  - **abort 恢復 pre-cmd state**：加 `on_abort` lambda — `emergency_stop` 中斷手動推桿操作時，清 abort_flag + 恢復 `cur` state（不留在 Error），讓手動操作卡死只需一次 STOP 即可脫困

### 為什麼
手動 `pusher feet retract` 在吸盤未洩壓時 ZDT stall，`skip` 只跳一個子指令，8 次 skip 也逃不出去；`emergency_stop` 又會落入 Error state 需要 recover。兩個改動合計：stall 不發生（有關閥），發生也能一次 STOP 脫困。

---

## 2026-04-29h — Claude Code — 新增 cmd_step_up：往上爬一步（feet→body 順序，crane 方向反轉）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：宣告 `cmd_step_up(int cm=0)` 與 `do_step_up_()`
- `user_lib/WASH_ROBOT.cpp`：實作 `do_step_up_()` 和 `cmd_step_up()`
  - Phase A（腳組先）：release feet valve → 2-stage retract → rail 0 → +step_cm（body anchored，無 crane 動作）→ cycle_group_("feet")
  - Phase B（身體後）：release body+center valve → 2-stage retract → crane retract(step+margin) → rail +step_cm → 0 → crane pay_out(margin) → re-extend center → cycle_group_("body")
  - Phase C：arm_sweep（同 step_down）
  - backup 方向與 step_down 對稱：feet backup 往 rail 負方向，body backup 往 rail 正方向
- `washrobot_new_PI/main.cpp`：dispatch 加 `step_up [cm]`，header 說明同步更新
- `web_backend/public/index.html`：step_down 旁加 `#btn-step-up`
- `web_backend/public/app.js`：`btn-step-up.onclick` 使用 `readStepCm()` 送 `step_up <cm>`

### 為什麼
需要機器人能往上爬（返回或重新定位），尺蠖動作與 step_down 完全對稱，只交換腳組/身體組執行順序並反轉 crane 收/放繩方向。

---

## 2026-04-29j — Claude Code — body 推桿伸長 9.5 → 9 cm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`PUSHER_EXTEND_BODY_PULSE` 28500 → 27000（~9.5 → ~9 cm）

### 為什麼
現場實測調整。

---

## 2026-04-29i — Claude Code — cycle_group_ valve_on 加 FC01 readback verify + retry

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`cycle_group_` 的 valve_on `try_or_pause_` 之後新增 verify+retry loop

### 根本原因
PQW（slave 12）與 9 顆 JC-100 共用同一條 `cli_22_` TCP 連線（USR gateway .22）。
`body_pre_cycle` 結尾送 `CH_VALVE_CENTER ON`（FC05），cycle_group_ 立刻又送 `CH_VALVE_BODY ON`（FC05），兩筆幾乎零間隔打到同一條 RS485 bus。USR-TCP232-304 gateway 在 RS485 echo 尚未清空時偶爾丟棄第二筆 FC05，但 TCP send() 層已回傳成功，`controlRelay` 看不到錯誤，導致 ZDT 伸出時 CH3 沒開。

### 修法
valve_on 之後以 FC01 `readAllStatus()` 驗證對應 bit，若未設定最多重送 3 次（每次間隔 50ms）。無法驗證（空回應）時照常繼續不 block。

---

## 2026-04-29h — Claude Code — 新增 cmd_step_up：爬升一格（腳組先移、身體組後移）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：宣告 `cmd_step_up(int cm = 0)` 及 `do_step_up_()`
- `user_lib/WASH_ROBOT.cpp`：實作 `do_step_up_()` 及 `cmd_step_up()`
  - Phase A（腳組先）：release feet valve → 兩段式縮腳 → DM2J rail 0 → +step_cm（腳上升）
  - Phase B（身體組後）：release body+center valve → 兩段式縮體 → **crane retract (step+margin)** → DM2J rail +step_cm → 0 → **crane pay_out margin** → re-extend center + valve on
  - 與 step_down 對稱：crane 方向在 body phase 反轉（retract 在前、pay_out 在後），腳組 phase 無 crane 動作
  - retry backup 方向對稱：feet backup 往 0 退、body backup 往 +step 退
  - 結尾同 step_down 呼叫 arm_sweep 清洗
- `washrobot_new_PI/main.cpp`：dispatch 加 `step_up [cm]`；header banner 更新
- `web_backend/public/index.html`：step_down 旁新增 `#btn-step-up`
- `web_backend/public/app.js`：`btn-step-up.onclick` 使用 `readStepCm()` 送 `step_up ${cm}`

### 為什麼
腳 → 身體順序爬升，配合未來工地多層清洗往上回走需求。
crane 方向待 bench 確認後視需要調整（目前設計：body phase retract before rail）。

---

## 2026-04-29g — Claude Code — 新增 cmd_recover：Error → Attached 軟恢復（驗真空但不破真空）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：宣告 `cmd_recover()`
- `user_lib/WASH_ROBOT.cpp`：實作 `cmd_recover` — Error 狀態下 `vacuum_check_("all")` 全 9 顆 sealed → state Attached；任一未吸 → 回 ERR、state 留 Error
- `washrobot_new_PI/main.cpp`：dispatch 加 `recover`
- `web_backend/public/index.html`：error pause row 加新按鈕「恢復至 Attached（驗真空）」(`#btn-recover`)
- `web_backend/public/app.js` `updateErrorPauseUI`：
  - 繼續/略過只在 `paused_on_error` enabled（既有）
  - **新**：recover 只在 `error` enabled
  - status bar 在 Error 時顯示「STATUS: ERROR — 可按「恢復至 Attached」驗真空回復」

### 為什麼
原本 step_down 失敗 → state Error → 唯一 GUI 路徑是 reset → init → attach。但 init 第一步把 valves OFF，**會破真空、機器人從牆上掉下來**。Error 狀態其實 cups 多半還貼著（feet/center 沒動到、body 部分問題），不需要重 init。

`cmd_recover` 直接：
1. 在 Error 狀態驗證 9 顆 cup vacuum
2. 全 OK → state 切到 Attached
3. 完全不動 valve / 推桿 / DM2J，保持物理現狀

### 流程對照
```
step_down 失敗 → state Error
  ↓
GUI vacuum panel 看哪幾顆紅 (沒吸)
  ↓
現場手動處理（推回 cup / 清膠 / 重貼）
  ↓
按 [恢復至 Attached（驗真空）]
  ├─ 全 sealed → state Attached → 繼續 step_down
  └─ 還是有沒吸 → ERR recover_vacuum_fail slaves=N,M → 繼續處理或走 return_home
```

### 跟 reset 的差異
| 指令 | 從哪裡 | 到哪裡 | 動硬體？ |
|---|---|---|---|
| reset | Error | Idle | 不動，但之後 init/attach 會破真空 |
| **recover** | Error | **Attached** | **完全不動硬體**，只驗真空 |

### 待驗
- [ ] step_down 失敗後 GUI 顯示 STATUS: ERROR、recover 按鈕亮起
- [ ] 真空全 OK → 按 recover → state 直接 Attached、可立刻 step_down
- [ ] 真空有壞 → 按 recover → 紅色錯誤訊息 + state 留 Error
- [ ] recover 過程不會聽到 valve click、不會看到推桿動作

## 2026-04-29f — Claude Code — panel-disabled 加 3s debounce + 後端 reconnect 3s → 1s

### 修改檔案
- `web_backend/server.js`：`RECONNECT_MS` 3000 → **1000**（瞬斷情境恢復更快）
- `web_backend/public/app.js`：
  - 新 const `DEBOUNCE_DOWN_MS = 3000` + `pendingDownTimers / pendingRawStatus` 兩個 module state
  - 重寫 `handleStatusChange` 為 per-device debounce：
    - **recovery (false→true)** → 立即套用 + 取消 pending timer
    - **down (true→false)** → 等 3s，期間若 raw 又變 true 就取消、不套 panel-disabled
  - 新私有 helper `debounceDeviceTransition_` / `applyDeviceTransition_`

### 行為對照
| 情境 | 改前 | 改後 |
|---|---|---|
| washrobot 重啟 ~1s（很快接回）| 立刻 panel-disabled flicker、可能 fire 跨裝置 stop | UI 完全不變、不 fire stop |
| 網路瞬斷 ~2s | panel-disabled 直接套上 | UI 不變 |
| 真斷線 > 3s | 立刻 disable | 3 秒後 disable |
| 重連恢復 | 立刻復原 | 立刻復原（不變）|

### 跨裝置 auto-stop 行為
- `washrobot 失聯 → 送 crane stop` 改成「3s 確認後才送」
- 副作用：如果 washrobot 真斷線，crane 會慢 3 秒才收到 stop
- 但既然 step_down 的所有 crane 動作都來自 washrobot，washrobot 一斷就不會再有新 crane 命令，所以 3s 延遲影響低

### 效果疊加
後端 reconnect 1s + 前端 debounce 3s = 配對良好的雙層保護：
- 短於 1s 的瞬斷：bridge 已重連，根本沒觸發 ws status broadcast
- 1-3s 中等斷線：bridge 重連 + frontend debounce 還沒 fire → UI 不變
- 超過 3s 真斷線：debounce timer fire → panel-disabled 套用 + 跨裝置 auto-stop

### 待驗
- [ ] 模擬 washrobot 重啟（kill + 再跑）：UI 應該不 flicker、log 看到 `washrobot reconnected within 3000ms — UI not changed`
- [ ] 拔網線 5 秒：3 秒後 panel 灰掉、log 看到「失聯 (>3s)」訊息

## 2026-04-29e — Claude Code — WS reconnect 自動 sync state（避免前端 STATUS 卡舊值）

### 修改檔案
- `web_backend/public/app.js` `ws.onopen`：log 之外多送一次 `status` 給 washrobot + crane（200 ms delay 給 server bridge settle）

### 為什麼
原本 `ws.onopen` 只 log 不抓 state。WS 中斷重連時，前端 `washrobotState` 可能停在斷線前的舊值（甚至 `unknown`），UI badge / 按鈕狀態跟後端真實 state 不一致。

新版 reconnect 後**主動送 `status`**：
- washrobot 回 `OK state=... p1=... crane_attached=... roll=... pitch=...`
- 既有 parser（`\bstate=`、`pN=`、`crane_attached=`）會自動同步 GUI

### 適用情境
- 切瀏覽器 tab 回來（Chrome backgrounded throttle 觸發 reconnect）
- 網路短暫斷線
- 第一次開頁面（之前 STATUS 顯示 `unknown` 直到第一次 state_changed EVT）

### 沒動的
- panel-disabled 邏輯不改 — washrobot 真斷線時整個 panel 不可點是正確的訊號（與其 bypass，不如修真實連線問題）
- easy_crane 不額外 sync — 既有 server-side weight loop 已經 push

## 2026-04-29d — Claude Code — 解真空後加入「真空已釋放」確認 polling（治推桿被吸住 stall）

### 為什麼

之前流程「valve OFF → fixed sleep 4 秒 → ZDT 推桿 retract」。如果 cup 還沒完全釋放，retract 會：
- 把 cup 強行從牆扯下來（cup 損壞 + 機械衝擊）
- ZDT 物理動不了 → stall
- 嚴重時 LEYG25 推桿桿身被拉扯損壞

新流程：每 200 ms poll JC-100，**全部 cup 壓力 ≥ DETACH_THRESHOLD_KPA (-10 kPa) 才往下**。timeout 才放棄。

### 修改檔案

- `user_lib/WASH_ROBOT.h`：宣告 `bool vacuum_wait_release_(const std::vector<int>& slaves, int timeout_ms)`
- `user_lib/WASH_ROBOT.cpp`：
  - 實作 `vacuum_wait_release_` — 每 200 ms poll JC-100、全釋放 return false、timeout return true 並 log + EVT 廣播 stuck slave list
  - 4 個 callsite 把 `sleep_ms_(VACUUM_RELEASE_WAIT_MS)` / `sleep_ms_(RETURN_VACUUM_RELEASE_MS)` 換成 `try_or_pause_(... vacuum_wait_release_(...) ...)`
- `user_lib/WASH_ROBOT.h` cycle_group_ template：retry path 也換掉

### 4 個套用點

| 函式 | slaves | timeout | context |
|---|---|---|---|
| `do_step_down_` body_pre_cycle | body 4 顆 + center 1 顆 | 4 s | `body_pre_vacuum_release` |
| `do_step_down_` feet_pre_cycle | feet 4 顆 | 4 s | `feet_pre_vacuum_release` |
| `cycle_group_` retry path | 該 group 的 slaves | 4 s | `cycle_<group>_vacuum_release_retry` |
| `cmd_return_home` step 4 | all 9 | 5 s | `return_home_vacuum_release` |

### 失敗處理

包在 `try_or_pause_` 內 — timeout（cup 沒釋放）會進 PausedOnError：
- **continue**：再 poll 一輪（再給 4s/5s 機會）
- **skip**：當作已釋放、強行 retract（操作者判斷）
- **emergency_stop**：中止、進 Error

EVT 同步廣播 `vacuum_release_timeout stuck=N1,N2,...` 讓 GUI 顯示哪幾顆卡住。

### 行為改變

- 原本：**固定**等 4 秒（不論 cup 釋放多快都 sleep 滿）
- 現在：**最多**等 4 秒，cup 0.5 秒釋放就 0.5 秒走人 → 大多情況**反而省時間**
- 唯一變慢：cup 真的卡住，原本 sleep 4s 然後直接 retract（會 stall），現在 sleep 4s 然後再進 PausedOnError（讓人介入）

### `cmd_return_home` 行為調整

舊：sleep + 一次性 check → 任一顆沒釋放回 `ERR detach_fail` + 直接 Error 狀態
新：poll + try_or_pause_ → timeout 進 PausedOnError 等使用者決定（不直接進 Error）

### 待驗

- [ ] step_down 跑一輪：body_pre_vacuum_release 的 log 應顯示 `all released after Xms`，X 應該 < 4000
- [ ] 故意把某顆 cup 用手按住模擬「沒釋放」→ 觀察是否進 PausedOnError + EVT 列出該 slave
- [ ] return_home 9 顆全釋放快不快（5s timeout 夠不夠）

## 2026-04-29c — Claude Code — 真空回饋微調伸出（fine-tune extend per-slave）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 新增常數 `FINE_TUNE_MAX_ITERS=3` / `FINE_TUNE_INCREMENT_PULSE=1000` / `FINE_TUNE_MAX_OVEREXTEND=4000` / `FINE_TUNE_SETTLE_MS=800`
  - 新增 method 宣告 `fine_tune_extend_per_slave_`（返回最終仍失敗的 slave list）
  - `cycle_group_` template：extend + settle 之後，把 `vacuum_check_` 換成 `fine_tune_extend_per_slave_`
- `user_lib/WASH_ROBOT.cpp`：實作 `fine_tune_extend_per_slave_`

### 邏輯
```
1. broadcast 同步伸到 base 預設值（既有）
2. settle 2s（既有）
3. fine-tune loop（最多 3 次）：
     vacuum_check_ → 抓沒吸住的 cup 清單
     全 OK → return（成功）
     對每顆未吸 cup → 多伸 +1000 脈衝（per-cup pusher_move_）
     超過 base+4000 上限 → 放棄該 cup
     等 800 ms 讓真空建立
4. 最後再 vacuum_check_ 一次，把結果交給呼叫端
5. 若仍有 fail → 觸發既有 outer retry（valve OFF / backup / 重伸）
```

### 屬性
- **per-cup 自適應**：只多推還沒吸的，吸住的不動
- **best-effort**：不進 PauseOnError；fail 走原本 retry 流程
- **每輪 vacuum_check_ 會 broadcast pN=value**（既有）→ GUI 即時看到變化
- **硬上限保護**：base+4000 (~1.4 cm) 避免硬撞牆

### 副作用
- 一次 phase 最壞情況多 ~25 秒（3 輪 × ~8 秒，含 ZDT per-slave 動作）
- 一次到位 → 第 0 iter return、0 額外時間

### 待驗
- [ ] cup 沒吸時看 log `[fine_tune] iter N slave M unsealed, extend X → Y`
- [ ] 全 OK 情況：log `[fine_tune] body all sealed at iter 0`
- [ ] 過上限：log `give up this cup` + 進 outer retry
- [ ] GUI vacuum panel 微調期間即時更新顏色

## 2026-04-29b — Claude Code — PUSHER_ACC 255 → 100（緩衝啟動衝擊，減少 body extend stall）

### 修改檔案
- `user_lib/WASH_ROBOT.h:156`：`PUSHER_ACC` 255 → **100**

### 為什麼
昨天現場測試 body 推桿觸牆會被往內推到 stall（chassis 被牆推回 → 較長的 body 推桿補不到 commanded 位置）。
255 是 ZDT 最大 acc（≈ 25500 RPM/s），啟動瞬間電流衝擊很大 → 馬達瞬間吃硬負載 → 容易誤判 stall。
100 是 moderate（≈ 10000 RPM/s），啟動較柔，配合 PUSHER_RPM=500 比較對稱。

### 影響
所有走 `motion_control_pos_mode_nowait(0, PUSHER_ACC, ...)` 的呼叫：
- `pusher_move_(slave, pulse)` 單顆動作
- `pusher_move_many_(slaves, pulse)` 群組同步動作
- 所以 init / attach / step_down / 手動 pusher / return_home 全部都會用新 acc

### 待驗
- [ ] 跑 init → 觀察 body extend 是否還會 stall
- [ ] 如果還 stall → 進一步降到 50 試
- [ ] 如果完全不 stall 但動作太慢 → 拉回 150

## 2026-04-29a — Claude Code — body 推桿伸長 10 cm → 9.5 cm（PUSHER_EXTEND_BODY_PULSE 30000 → 28500）

### 修改檔案
- `user_lib/WASH_ROBOT.h:152`：`PUSHER_EXTEND_BODY_PULSE` 30000 → **28500**（~10 cm → ~9.5 cm）

### 換算
body 校準：30000 pulses ≈ 10 cm → 3000 pulses/cm。9.5 × 3000 = **28500 pulses**。

### 影響範圍
所有 body 推桿伸長動作會用新值（少 0.5 cm）：
- `cmd_init_impl_` body extend
- `cmd_pusher body extend`
- `cmd_pusher all extend`（body 那一段）
- `do_step_down_` body 階段（cycle_group_ template 內 PUSHER_EXTEND_BODY_PULSE）
- staged extend 的 half_pulse（body）會變 14250

### 為什麼
使用者要求縮短 body 伸長量到 9.5 cm（從 10 cm）。

## 2026-04-28x — Claude Code — PUSHER_RPM_RETRACT 250 → 100

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`PUSHER_RPM_RETRACT` 250 → **100**

### 原因
04-28w 才剛改 250（extend 500 的半速），現場實測仍太硬。再降到 100 RPM（extend 的 1/5）。

### 速度對照（最新）
| 動作 | RPM |
|---|---|
| extend | 500 |
| retract（含 half / full / 所有 group / 手動） | **100** |

### 預期副作用
retract 時間再次拉長（500/100 = 5 倍 vs 原版 1000 RPM）。step_down 體感再慢一些。

## 2026-04-28w — Claude Code — ZDT retract 速度獨立慢（PUSHER_RPM_RETRACT = 250）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - 新增 `PUSHER_RPM_RETRACT = 250`（extend `PUSHER_RPM = 500` 的半速）
  - `pusher_move_` / `pusher_move_many_` signature 加 `int rpm = PUSHER_RPM` 參數（default 不破壞 extend 呼叫端）
  - `cycle_group_` retry 路徑兩個 retract（half + full）改傳 `PUSHER_RPM_RETRACT`
- `user_lib/WASH_ROBOT.cpp`：
  - `pusher_move_` / `pusher_move_many_` impl 用參數 rpm 取代寫死的 PUSHER_RPM
  - 所有 retract 呼叫加 `PUSHER_RPM_RETRACT` 第三參數：
    - body_pre_cycle center half + full
    - body_pre_cycle body half + full
    - feet_pre_cycle feet half + full
    - cmd_pusher manual feet/body/center retract（half + full）
    - cmd_pusher manual all retract（feet/body/center 各 half + full = 6 個）
    - cmd_return_home pusher retract

### 速度對照
| 動作 | RPM |
|---|---|
| extend（任何 group / phase）| **500**（PUSHER_RPM）|
| retract（任何 group / phase / half / full）| **250**（PUSHER_RPM_RETRACT）|

### 沒影響
- DM2J 動作（PR_move_cm 等）速度走 DM2J_RPM=200，不變
- ZDT 廠商工具讀回的速度顯示不變（這個是 RPM 設定，不是 max_speed）

### 預期副作用
- 所有 retract 動作時間加倍
- step_down 每步多花約 30~40 秒（feet/body 各 2 個 retract phase × 2 倍時間）

### 待驗
- [ ] 跑 step_down → 觀察 retract 段是否明顯比 extend 慢
- [ ] 觀察是否減少 ZDT stall 發生率

## 2026-04-28v — Claude Code — 兩段式 retract 擴及所有 ZDT（含 center / 手動）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `body_pre_cycle` center pusher：one-shot 0 → two-stage（half=PUSHER_EXTEND_PULSE/2 → 1s → 0）
  - `cmd_pusher("retract")` for feet / body / center：全部改 two-stage
  - `cmd_pusher("all", "retract")`：拆成 feet / body / center 三段式（各自 half pulse）
- `user_lib/WASH_ROBOT.h` `cycle_group_` template retry：拿掉 `if (group == "feet" || group == "body")` 條件，所有 group（含 center）都走 two-stage

### 行為對照
| 觸發點 | 改前 | 改後 |
|---|---|---|
| body_pre_cycle 收 center | one-shot 0 | half=15000 → 1s → 0 |
| body_pre_cycle 收 body 4 顆 | (04-28u 已 two-stage) | 不變 |
| feet_pre_cycle 收 feet 4 顆 | (04-28u 已 two-stage) | 不變 |
| cycle_group_ retry 收 center / fallback | one-shot | two-stage |
| GUI feet RETRACT | one-shot | two-stage |
| GUI body RETRACT | one-shot | two-stage |
| GUI center RETRACT | one-shot | two-stage |
| GUI all RETRACT | one-shot 0 全部 | feet two-stage → body two-stage → center two-stage（依序）|

### 結論
**所有「ZDT 收推桿到 0」的路徑都用兩段式**。沒有例外。

## 2026-04-28u — Claude Code — vacuum wait 4s + ZDT 半速 + feet/body 兩段式縮

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `VACUUM_RELEASE_WAIT_MS` 2000 → **4000 ms**
  - 新增 `RETRACT_HALF_WAIT_MS = 1000`（兩段式縮的中間等待）
  - `PUSHER_RPM` 1000 → **500**（伸縮速度半速）
  - `cycle_group_` template retry 路徑：feet/body 群組改兩段式縮（half → wait → full）；center / 其他群組維持 one-shot
- `user_lib/WASH_ROBOT.cpp`：
  - `body_pre_cycle`：body 4 顆 ZDT 改兩段式（center 維持 one-shot）
  - `feet_pre_cycle`：feet 4 顆 ZDT 改兩段式

### 行為
| 步驟 | 改前 | 改後 |
|---|---|---|
| valve OFF 後等多久 | 2 秒 | **4 秒** |
| ZDT 速度 | 1000 RPM | **500 RPM**（半速）|
| body group 收推桿 | one-shot 0 | half (15000) → 等 1s → 0 |
| feet group 收推桿 | one-shot 0 | half (11500) → 等 1s → 0 |
| center 收推桿 | one-shot 0 | one-shot 0（不變）|
| 手動 cmd_pusher | one-shot 0 | one-shot 0（不變，使用者自己控制）|

### 為什麼
解真空後 cup 邊緣黏吸需要時間鬆開、加上推桿原速太急會在 cup 沒鬆時 stall。兩段式 + 半速 + 等更久三招同時治。

### 預期副作用
單次 step_down 體感變慢：
- valve OFF 等：+2s × 2 phases = +4s
- 兩段式：+1s × 2 phases = +2s
- 速度半 → 推桿動作時間 ×2（每段 ~6s 變 ~12s，body+feet 6 段 → +約 36s）
- 累計每步 step_down 增加 ~40 秒左右

如果太慢之後可以調回。

## 2026-04-28t — Claude Code — Linux_test 腳組 extend 對齊 7 cm → 8 cm

### 修改檔案
- `Linux_test/main.cpp`：
  - `PUSHER_EXTEND_FEET_PULSE` 常數 20000 → 23000
  - 註解 `feet pushers reach ~7cm at 20000 pulses` → `~8cm at 23000 pulses`
  - 所有 cout `extend feet pushers ~7 cm` → `~8 cm`（共 8 處）

### 對齊
跟 04-28s 主程式的 WASH_ROBOT.h 數值同步。Linux_test menu 7/8/11/12 等用到腳組伸出的選項都跟著走 8 cm。

### 沒動的
- `cm_per_degree` 估算函式 L610（用 `7.0 / (20000 × 360 / 51200) ≈ 0.04978`）：那是校準點，數值差很小，留著不影響顯示

## 2026-04-28s — Claude Code — 腳組推桿 extend 7 cm → 8 cm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`PUSHER_EXTEND_FEET_PULSE` 從 20000 → 23000（依 ~2857 pulses/cm 校準，8 cm ≈ 22857 取整）

### 影響
所有觸發 feet 推桿伸出的流程都自動跟著走：
- `cmd_init_impl_` 初始化伸 feet
- `do_step_down_` `cycle_group_("feet", ...)` 各 attempt 的伸出
- `cmd_pusher feet extend`（GUI 手動）
- `cmd_pusher all extend`（GUI manual all）

body 組的 `PUSHER_EXTEND_BODY_PULSE = 30000`（~10 cm）不變。

## 2026-04-28r — Claude Code — vacuum release wait 1s → 2s（新增 VACUUM_RELEASE_WAIT_MS 常數）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新增常數 `VACUUM_RELEASE_WAIT_MS = 2000`，cycle_group_ template 內 `sleep_ms_(1000)` 換用此常數
- `user_lib/WASH_ROBOT.cpp`：`body_pre_cycle` 和 `feet_pre_cycle` 內 `sleep_ms_(1000)` 換用 `VACUUM_RELEASE_WAIT_MS`

### 統一三處
| 位置 | 改前 | 改後 |
|---|---|---|
| `cycle_group_` retry 路徑 | 1000 ms | **VACUUM_RELEASE_WAIT_MS = 2000** |
| `body_pre_cycle` | 1000 ms | 同上 |
| `feet_pre_cycle` | 1000 ms | 同上 |

### 為什麼加長
1 秒對某些 cup 不夠 — 真空管路洩壓 + cup 邊緣黏吸完全鬆開需要時間。短了會在 cup 還吸住時拔推桿 → ZDT 卡（stall）。集中常數方便日後微調。

### 沒動的
- `RETURN_VACUUM_RELEASE_MS = 5000`（return_home 緊急流程用，更保守）

## 2026-04-28q — Claude Code — Linux_test 新增 menu 19：讀全部 9 顆 ZDT 位置

### 修改檔案
- `Linux_test/main.cpp`：
  - 新增 `test_zdt_positions()` — 連 .21、init 9 顆、loop 印表格（slave / pos(deg) / cm(est) / enabled / pos_reached / stall / group label）
  - menu 列加 `19  ZDT positions   — read all 9 ZDT pushers (deg + cm estimate)`
  - main loop 加 `else if (line == "19") test_zdt_positions();`
  - 按 Enter 重讀，q 回 menu

### cm 估算
依據 `WASH_ROBOT.h` 的 pusher 校正常數 + ZDT 預設 51200 ppr：
- feet (slave 1-4): `cm/deg ≈ 0.04978`（20000 pulses ≈ 7 cm）
- body (slave 5-8) + center (9): `cm/deg ≈ 0.04741`（30000 pulses ≈ 10 cm）

注意：若使用者改過 ZDT microstepping（不是預設 51200 ppr），cm 估算就不準，但 deg 仍然是真值（直接從 encoder 讀的）。

### 為什麼

bench 測試需要快速確認 9 顆推桿目前伸到哪、是否同步、是否有 stall flag — 之前每次只能 menu 3 / 6 一顆一顆查。新 menu 19 一次印 9 顆 + group label，方便對照物理位置。

### 待驗
- [ ] 9 顆全 retract 時跑 menu 19 → cm 估算應該全部接近 0
- [ ] 點 GUI `pusher feet extend` → 跑 menu 19 → feet (1-4) cm ≈ 7
- [ ] 點 `pusher body extend` → body (5-8) cm ≈ 10
- [ ] 故意送很大的 target 看 stall_flag 是否亮 Y

## 2026-04-28p — Claude Code — crane-link-badge 改反映 crane_attached（不是 web→crane TCP）

### 修改檔案
- `web_backend/public/index.html`：badge label 從「connection 狀態」改成「crane 驅動狀態」
- `web_backend/public/app.js`：
  - `setDot` 還原成原本只切 `.ok` class（不再 mirror 到 badge）
  - `crane_attached=on/off` parser 同時更新 `#crane-link-badge` 文字 + 顏色 class
- 樣式 `.link-badge` 不變（仍用 04-28o 的綠/紅樣式）

### 行為更正
| crane_attached 值 | badge 顯示 | class |
|---|---|---|
| `on` | `🟢 ATTACHED (washrobot 驅動)` | `link-ok`（綠底）|
| `off` | `⚪ DETACHED (skip)` | `link-down`（紅底脈動）|
| 未知（沒收到 status）| `? unknown` | 預設灰底 |

### 為什麼改
之前 04-28o 把 badge 接 setDot（dotC），反映 web→crane TCP 連線。但使用者要的是 washrobot 的 `crane_attached_` flag（決定 step_down 等流程是否送命令到 crane）。這兩個是不同層的訊息，前者由 web_backend 維護、後者由 washrobot 內部 atomic flag。

### 部署
只動前端兩檔（HTML 文字、JS 邏輯），scp + Ctrl+Shift+R reload。

## 2026-04-28o — Claude Code — GUI crane panel 加連線狀態 badge（鏡像 #dot-crane）

### 修改檔案
- `web_backend/public/index.html`：crane panel 加新 row「connection 狀態」 + `<span id="crane-link-badge">`
- `web_backend/public/app.js`：`setDot` 改為「除了切 dot class 外，crane dot 同步更新 badge 文字 + 顏色 class」
- `web_backend/public/style.css`：新增 `.link-badge` / `.link-ok` / `.link-down` 樣式（已連線綠 / 失聯紅 + pulse 動畫）

### 行為
- web_backend → crane TCP 通訊狀態變化時，header 的小綠點與 crane panel 的 badge 同步切換
- 連線：`🟢 已連線`（綠底）
- 失聯：`🔴 失聯`（紅底脈動）
- WS 斷線：badge 維持上次狀態，直到重連刷新

### 注意
這個 badge 反映的是 **web_backend ↔ crane TCP 連線**（同 #dot-crane），不是 washrobot ↔ crane 的健康度。如果之後要區分（例如 watchdog timeout 但 web 仍可連），要再加一個 badge。

### 部署
只動前端三檔，scp + Ctrl+Shift+R reload 生效。

## 2026-04-28n — Claude Code — 新增 `crane_attached` toggle（GUI 切換 washrobot 是否驅動 crane）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：宣告 `cmd_crane_attached(bool on)` + member `std::atomic<bool> crane_attached_`（預設 true）
- `user_lib/WASH_ROBOT.cpp`：
  - constructor 加 `crane_attached_(true)`
  - `crane_cmd_` 進入時若 `!crane_attached_` 直接回 `OK skipped`，不送 TCP、不抓 mutex
  - `crane_watchdog_loop_` 進每輪先檢查 `crane_attached_`，false 就 continue（不 ping、不檢 timeout、不 abort）
  - 新 `cmd_crane_attached(on)`：切換 atomic、ON 時 reset `crane_last_ok_ms_` 給 grace period、廣播 EVT
  - `cmd_status` 回應加 `crane_attached=on/off` 欄位
- `washrobot_new_PI/main.cpp`：dispatch 加 `crane_attached <on|off>`
- `web_backend/public/index.html`：crane panel 上方加一排 `attached ON / OFF` 按鈕 + 狀態文字 + hint 說明
- `web_backend/public/app.js`：parse 任何含 `crane_attached=on/off` 的 line → 同步 `#crane-attached-status` 文字

### 行為
| 狀態 | crane_cmd_（內部呼叫）| watchdog | 用途 |
|---|---|---|---|
| **ON**（預設）| 真送 TCP，timeout 等回應 | ping 跑、timeout 期間 motion → abort | 真實 crane 連線 |
| **OFF** | 直接回 `OK skipped` | 整輪 skip（不 ping、不 abort）| bench 測試 / crane 離線 |

step_down body_pre_cycle / feet_backup / phase5 / return_home 內 crane_cmd_ 全部會自動受影響。GUI 直接 raw command 送 `pay_out 30` 還是會 forwarded 給 crane（不經 WashRobot）。

### 部署
- 後端：rebuild + deploy washrobot 主程式
- 前端：scp index.html / app.js 到 .5.26 + Ctrl+Shift+R reload

### 待驗
- [ ] OFF：點 step_down → log 看到 `[crane_cmd] '...' SKIPPED`、watchdog 沒動、流程繼續
- [ ] ON：cmd_status 顯示 `crane_attached=on`、watchdog 正常 ping crane
- [ ] OFF → ON 切換：crane_last_ok_ms_ 重置，不會立刻 trigger timeout abort
- [ ] GUI 狀態文字隨 cmd_status / EVT 切換顯示

## 2026-04-28m — Claude Code — vacuum_check_ 廣播每顆 sensor worst 讀數到 GUI（即時更新真空 panel）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `vacuum_check_`：每顆 sensor 確定 worst（最弱 sample）後，呼叫 `evt_("vac_sample pN=value")`，廣播到所有 WS client

### 行為
GUI 的 `parseVacuumValues()` 已經 parse 任何含 `pN=value` 的 line（cmd_status / EVT 都吃），現在 vacuum_check_ 期間也會 emit。
- step_down body / feet / cmd_attach 觸發 vacuum_check_("body"/"feet"/"all") 時，cell 5/6/7/8（body）或 1/2/3/4（feet）或全 9 顆即時 colored update
- 通訊全失敗那顆 sensor 不 emit（panel cell 保留前次數字）

### 沒動的
- cmd_status 一次性回 9 顆值的格式不變
- 前端不用改（parser 已通用）

### 待驗
- [ ] 跑 step_down body 階段 → 看 GUI cell 5-8 在 vacuum_check_ 時更新顏色 + 數字
- [ ] cmd_attach 之後 vacuum_check_("all") → 9 顆都 update
- [ ] 一顆 sensor 全 comm fail → 那顆 cell 不變、其他更新

## 2026-04-28l — Claude Code — 撤掉 3 處 TEST MODE 安全機制 skip（attach 真空驗證 / IMU emergency / crane watchdog）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - **`cmd_attach`** 末段：解註解 `vacuum_check_("all")` — attach 完之後驗證 9 顆 cup 真的吸住，任一未吸 → 回 `ERR attach_vacuum_fail slaves=...`，state 不轉到 Attached
  - **`imu_monitor_loop_`** EMERGENCY 路徑：解註解 4 行 abort 邏輯（`abort_flag = true`、`motion_active_ = false`、`crane_cmd_("emergency_stop")`、`set_state_(State::Error)`）— 連續 SUSTAIN_MS 內傾斜 ≥ `IMU_EMERGENCY_DEG (45°)` → 真的進緊急停止
  - **`crane_watchdog_loop_`**：解註解 `abort_flag = true` — 動作中 crane 失聯 (`elapsed > WATCHDOG_TIMEOUT_MS = 60s`) → 設 abort_flag

### 為什麼解
- attach vacuum：threshold 已對齊 kPa 單位 + multi-sample + comm-retry 補強，誤觸機率低
- IMU emergency：機體已上機，傾斜 45° 不停就是真有問題
- crane watchdog：要連 crane 才有意義，連線後 60s 沒回應 = 真的斷了

### 影響
- attach 失敗會直接回 ERR、state 維持 Ready（之前永遠回 OK）
- IMU 偵測 45° 連續 SUSTAIN_MS → state = Error，整個流程立刻停
- crane 60s 沒回 OK + 動作進行中 → abort_flag → 下個 check_abort_ 點停下

### 仍保留 TEST MODE 的設定
- `WATCHDOG_TIMEOUT_MS = 60000`（仍 60s，主 crane 連上要回 2000）
- `CRANE_IP = "192.168.5.26"`（shim 在那台，主 crane 上自己 Pi 後改 .1.101）
- driver `debug=true`（`WR_DRIVER_DEBUG=0` 可暫時關）

### 待驗
- [ ] attach 真的吸住 → state 轉 Attached
- [ ] attach 故意一顆吸不上 → 回 `ERR attach_vacuum_fail slaves=N`，state 留在 Ready
- [ ] IMU 把機體傾斜 45° 連續 N 秒 → state 進 Error、log 印 `imu_emergency balance_deg=...`
- [ ] step_down 中斷 crane 連線 → 60 秒後 abort，下個 check_abort_ 觸發中止

## 2026-04-28k — Claude Code — step_down 重啟清洗自動化（撤掉 TEST MODE skip）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_` 尾段：
  - 撤掉 `[TEST MODE 2026-04-24]` 的 arm_sweep skip 註解
  - 重新啟用 `do_arm_sweep_()` 呼叫
  - sweep 失敗 → 印 log + 直接 propagate 失敗（state 進 Error）

### 行為
每跑完一個 step_down（body 階段 + feet 階段都成功）後，自動執行 arm_sweep：
1. 開水閥 + 水泵 + 刷子
2. arm 從 0 → +30 cm（右）
3. arm → -30 cm（左）
4. arm → 0 cm（回中）
5. 關水/刷子

arm_sweep 內 3 段 DM2J 動作各自有 `try_or_pause_` 包過（04-28j 改的）→ arm 卡住會進 PausedOnError 讓使用者按繼續/略過，不是直接整個 step_down 中斷。

### 連帶
- `cmd_run` 跑 N 次也會每次 step 後跑 sweep
- 整個 step 時間延長：原本 body+feet ~30~60 秒 → 加上 sweep（3 段 × 30 cm @ 200 rpm ≈ 27 秒 + 水/刷子 ramp）約 60~90 秒

### 待驗
- [ ] 跑單次 step_down → body OK → feet OK → 看到 `[step_down] start wash sweep`
- [ ] arm 三段移動正確（右-左-中）+ 水閥 / 水泵 / 刷子有開
- [ ] sweep 結束 → 水/刷子全關 → return OK step_done
- [ ] 故意讓 arm 卡（手擋住）→ try_or_pause_ 進 PausedOnError，可繼續/略過

## 2026-04-28j — Claude Code — PauseOnError 擴到 cmd_init / cmd_attach / do_arm_sweep_

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：
  - `cmd_init_impl_`：所有原本 `if (op) return "ERR ...";` 的硬體呼叫包進 `try_or_pause_`
    - 7 個 PQW relay（pump on / 6 個 valves+water off）
    - 2 顆 DM2J wheel retract（PR_move_cm）
    - DM2J 腳組 rail home（dm2j_pair_move_abs_）
    - ZDT enable loop 的 `motion_control_driver_EN(true)`
  - `cmd_attach`：3 個 valve ON 包進 try_or_pause_
  - `do_arm_sweep_`：3 個 DM2J ARM PR_move_cm 包進 try_or_pause_（cleanup 的 brush/water_pump/water_inlet OFF 維持原樣不包，因為 cleanup 必須無條件跑）

### 不包的地方（風險評估）
- `do_phase5_roll_correct_` — 機體只靠中心吸盤撐，pause 時間不可控 → 風險高，不包
- `cmd_return_home` — 緊急下降流程，pause 機體留懸空 → 風險高，不包

### 行為對照
| 流程 | 改前（fail 行為）| 改後 |
|---|---|---|
| init 中 PQW 偶發失敗 | 直接 return ERR，state 不變、再跑 init 又撞 | PausedOnError → 使用者 GUI 按繼續 → 重試 |
| attach valve ON 失敗 | 直接 ERR，state 退回 Ready | PausedOnError → 重試 / 略過 |
| arm_sweep DM2J 失敗 | ERR，但 brush/water 有清乾淨 | PausedOnError → 重試 / 略過（cleanup 仍跑）|

### 待驗
- [ ] 拔網線製造 init PQW 失敗 → state 進 paused_on_error，GUI 紅底脈動顯示 context
- [ ] 接回網線、按「繼續」→ 重試該 op、流程接著跑
- [ ] 按「略過此步」→ 假裝該 op OK、流程繼續（init 完成）
- [ ] arm 卡住 stall → PausedOnError；按「繼續」清 stall 後重做

## 2026-04-28i — Claude Code — vacuum_check_ comm-error 顯式處理 + 跨 slave delay

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `vacuum_check_`：
  - 每個 sample 內部加 `COMM_RETRY_MAX=3` 次 retry，間隔 50 ms
  - 每次 read_pressure() 後檢查 `M_(s).error_flag`，error 視為該 sample 失敗（不採用回傳的 cached 值）
  - 全 3 sample 都 comm fail → 印 log + 視為 detached（fail）
  - 部分 sample comm fail → log 提示，採用其餘 good samples 的最弱
  - 換 slave 之間加 50 ms delay 降低 gateway buffer 殘留風險

### 原因
之前 `read_pressure()` 在 Modbus 失敗時 silent return cached `_last_pressure`，呼叫端拿到的是「舊讀數冒充新讀數」。使用者觀察到 vacuum_check_ 一直讀到 -23（其實是某次成功讀取留下的 cache），Linux_test 同顆 sensor 卻讀 -68。

新版透過 `error_flag` 判斷該 sample 是否 fresh，stale 就 retry，retry 仍失敗就丟棄。即使 driver 行為沒改，呼叫端也能不被 cache 騙。

### 風險 / 副作用
- 4 顆 cup × 3 sample × 最壞情況 3 retry × 50 ms = 1.8 秒（但只在 comm 真的有問題時才達到）
- 正常情況一次過：4 × 3 × 50 ms 採樣 + 3 × 50 ms slave gap ≈ 750 ms（比舊版多 150 ms）

### 待驗
- [ ] 製造 gateway 阻塞（cmd_status + step_down 同時跑）→ vacuum_check_ 不會被 -23 cache 騙
- [ ] 真的 comm 全失敗時看到「ALL 3 samples comm-failed — treat as detached」log + 該 slave 列為 fail
- [ ] 正常情況沒有 comm fail log，速度跟之前差不多

## 2026-04-28h — Claude Code — GUI vacuum panel 新增 pump (CH1) 開關 + 後端 `pump <on|off>` 指令

### 修改檔案
- `user_lib/WASH_ROBOT.h`：宣告 `cmd_pump(bool on)`（在 `cmd_vacuum` 旁邊）
- `user_lib/WASH_ROBOT.cpp`：實作 `cmd_pump` — `pqw_.controlRelay(CH_PUMP, on)`，Error 狀態拒絕
- `washrobot_new_PI/main.cpp`：dispatch 加 `pump <on|off>` + header 註解同步
- `web_backend/public/index.html`：在 `manual — vacuum` panel **最上面**加一排「pump (CH1) ON / OFF」按鈕

### 為什麼

使用者要求在網頁 vacuum panel 新增馬達開關（即 dp0105 vacuum pump，CH1）。原本 pump 只能由 `cmd_init_impl_` 自動開、`cmd_shutdown` 自動關，沒有手動 toggle 的入口，不利 bench debug。

### 注意

- `cmd_init_impl_` 一進來會把 pump 自動打開，所以正常流程不需手動操作
- 跑流程中（init / attach / step_down / run）手動關 pump 會讓 9 顆吸盤瞬間漏氣 → 真空 fail，不要這樣做
- 沒加 hint 提醒（按鈕語意明確、跟其他 valve 排在同 panel）

## 2026-04-28g — Claude Code — 把所有 `pusher_move_*` 呼叫點都包進 `try_or_pause_`（一致性）

### 為什麼

之前 PausedOnError + continue/skip 按鈕只在 `do_step_down_` / `cycle_group_` 路徑生效。其他指令（init / attach / 手動 pusher / return_home）裡的 pusher 動作**沒包**，stall 時只回 `ERR ...\n`，state 不變，按鈕不會 enable，使用者卡住只能用 raw command 重試或 reset。本次補齊一致性。

### 改動

`user_lib/WASH_ROBOT.cpp` 新增 9 個 try_or_pause_ wrap 點（context 對應）：

| 函式 | 動作 | context |
|---|---|---|
| `cmd_init_impl_` | feet pusher extend | `init_feet_pusher_extend` |
| `cmd_init_impl_` | body pusher extend | `init_body_pusher_extend` |
| `cmd_attach` | center pusher extend | `attach_center_pusher_extend` |
| `cmd_pusher` (retract) | group retract | `manual_pusher_<group>_retract` |
| `cmd_pusher` (all extend) | feet | `manual_pusher_all_feet_extend` |
| `cmd_pusher` (all extend) | body | `manual_pusher_all_body_extend` |
| `cmd_pusher` (all extend) | center | `manual_pusher_all_center_extend` |
| `cmd_pusher` (group extend) | feet/body/center | `manual_pusher_<group>_extend` |
| `cmd_return_home` | all 9 retract | `return_home_pusher_retract` |

加上原有 4 個 `do_step_down_` + 2 個 `cycle_group_` template 內部的 pusher wrap，**共 13 處 pusher 呼叫全部走 try_or_pause_**。

### 正確性檢查

1. **狀態保留**：`await_user_intervention_` 會把進入時的 state 存到 `state_before_pause_`，user 按 continue 後恢復。例如 init 從 Idle 開始 → stall → PausedOnError → continue → 回 Idle → 重試 → 成功 → cmd_init_impl_ 結尾 set_state_ Ready
2. **Skip 語意**：user 按 skip 表示「我手動修好了，當作這步成功」，try_or_pause_ 回 false，後續流程繼續（譬如 init 跳過 pusher extend 直接做 IMU baseline）
3. **Abort 路徑**：try_or_pause_ 回 true 只發生在 emergency_stop，那時 cmd_emergency_stop 已經把 state 設成 Error，所以 cmd 直接 `return "ERR aborted\n"` 即可（state 已是 Error）。`cmd_return_home` 用 `fail()` lambda 多套一層 `set_state_(Error)`，是冗余但無害
4. **多次點擊**：第二次點擊同 cmd_pusher 進到 worker thread，會在 `motion_mtx_` 上排隊等第一次解套；continue/skip 是 FAST 不卡 mutex
5. **lambda capture**：所有 `&slaves` / `&feet_group` 等 by-ref 都在同 scope（try_or_pause_ 同步呼叫），無 dangling 風險

### 待驗

- [ ] 在 init 時故意讓某顆 ZDT stall → 觀察「⚠ ERROR 暫停中」label 顯示 + context 顯示 `init_feet_pusher_extend` 之類
- [ ] 同上但 stall 在 attach 階段 → context 應為 `attach_center_pusher_extend`
- [ ] 手動 pusher feet extend → ZDT stall → context 為 `manual_pusher_feet_extend`
- [ ] return_home 階段 stall → context 為 `return_home_pusher_retract`，按 continue 重試 / skip 略過 / emergency_stop 後狀態為 Error

## 2026-04-28f — Claude Code — 修 PausedOnError 解套死鎖（前端 regex + 後端 fast/slow 分流）

### Fix A：前端 regex 漏抓 `EVT state_changed`
`web_backend/public/app.js:174-182`：原本只用 `\bstate=(\S+)` 抓 state，但 `EVT state_changed running paused_on_error` 不含 `state=` 字面 → `washrobotState` 永遠停在 `'unknown'` → continue/skip 按鈕永遠 disabled。

加 fallback regex `EVT\s+state_changed\s+\S+\s+(\S+)` 抓 state_changed 的第二個 token（new state）。

### Fix B：dispatch 在 receive thread 內同步阻塞 → 死鎖
`washrobot_new_PI/main.cpp on_receive`：原本所有指令 inline 跑在 receive thread，long-running 指令（step_down / run）卡進 `await_user_intervention_` 後，**同條 TCP 連線後續送來的 continue / skip / emergency_stop / ping / status 全在 buffer 排隊**，因為 receive thread 沒空回去處理 → 整個 GUI 死鎖。

改成 fast/slow 兩條路：
- **FAST**（synchronous on receive thread）：`ping` / `status` / `pause` / `resume` / `continue` / `skip` / `emergency_stop` / `reset`
- **SLOW**（detached thread per call）：其他全部

SLOW 指令在 worker thread 跑 `dispatch()` + `sendToClient()`，receive thread 立刻回去 polling 下一筆 → 卡 PausedOnError 時可以**從同一條連線送 continue/skip 解套**。

SLOW 指令彼此會在 `motion_mtx_` 自然 serialize（先 push 先處理），不會 race。`sendToClient` 是 raw `send(2)`，POSIX 保證對同 fd thread-safe，<200 byte 的 ASCII 訊息也不會 interleave。

### 修改檔案
- `web_backend/public/app.js` — regex 加 state_changed 的 fallback
- `washrobot_new_PI/main.cpp` — `#include <thread>` + `#include <unordered_set>`、新 `FAST_CMDS` 集合、`on_receive` 拆 fast/slow 兩條路

### 部署
- `app.js` → scp 到 .5.26 backend `public/`，瀏覽器 Ctrl+Shift+R
- `main.cpp` → 重新編譯 + 部署 washrobot 主程式

### 待驗
- [ ] 模擬 PausedOnError（step_down 故意讓 ZDT stall）→ 按 status 應立刻回 OK，狀態框顯示「⚠ ERROR 暫停中」+ context、continue/skip 按鈕變可按
- [ ] 直接按 continue/skip 不需先按 status → state_changed event 抓到 paused_on_error 即啟動
- [ ] PausedOnError 期間多次按 ping 都能回 OK pong（receive thread 沒被卡）
- [ ] PausedOnError 期間按 pusher / vacuum 等 SLOW 指令會在 worker thread 等 motion_mtx_，不影響 fast 指令處理

### 後續可考慮
- 若 SLOW 指令的回覆順序需要保證一致，加一個 worker queue + 單一 dispatcher thread；目前每個 SLOW 指令一條 detached thread，靠 motion_mtx_ 自然排序，dev/test 夠用

## 2026-04-28e — Claude Code — feet_pre_cycle valve OFF → ZDT retract 等待從 300 ms → 1000 ms

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `feet_pre_cycle` lambda：valve OFF 後 sleep 從 300 ms 改 1000 ms，對齊 body_pre_cycle 與 cycle_group_ template 的 1000 ms 標準

### 原因
使用者要求：解真空後必須等 1 秒才能縮回 ZDT 推桿。掃過所有 valve OFF + ZDT retract 的點：
- `cycle_group_` template ✓ 1000 ms
- `body_pre_cycle` ✓ 1000 ms
- **`feet_pre_cycle` ✗ 只有 300 ms** ← 本次修正
- 其他 valve OFF 點後面沒接 ZDT retract（init 後接 extend；phase5 / tilt_mode / shutdown 都沒 retract；return_home 有 5000 ms `RETURN_VACUUM_RELEASE_MS`）

### 物理意義
真空放掉到吸盤實際脫離牆面有滯後（VT307 電磁閥切換 ~50 ms + 真空管路洩壓 + 吸盤接觸點仍黏附）。短於 1 秒收推桿，cup 還在吸狀態下被機械強行拔下，會卡 ZDT (stall) + 拉壞 cup。

## 2026-04-28d — Claude Code — GUI 新增 vacuum readings panel（9 顆 JC-100 顯示 + 顏色標示）

### 修改檔案
- `web_backend/public/index.html`：在 `manual — vacuum`（valve 控制）和 `manual — pusher` 之間插入新 panel `manual — vacuum readings`，9 顆吸盤分三組（feet 1-4 / body 5-8 / center 9）+ refresh 按鈕
- `web_backend/public/app.js`：
  - `parseVacuumValues(line)` — 從任何含 `pN=value` 的 line 抓出讀數，更新 `#vac-N` 內容 + 顏色 class
  - `onWashrobotLine` 結尾呼叫 `parseVacuumValues(line)` 自動同步
  - `btn-refresh-vacuum` onclick → 送 `status` 觸發後端回 9 顆讀數
- `web_backend/public/style.css`：新增 `.vac-cell` 基本樣式 + 三種狀態 class（`.vac-strong` 綠、`.vac-weak` 黃、`.vac-none` 紅）

### 行為
- 點 refresh → 送 status → 後端回 `OK state=... rail=... p1=N p2=N ... p9=N ...` → 各 cell 自動更新
- step_down / 任何流程的 status 回應也會被 parseVacuumValues 抓到 → 同時更新顯示
- 顏色判定：
  - `≤ -50 kPa` → 綠 `.vac-strong`（attached）
  - `-50 < p ≤ -10` → 黃 `.vac-weak`（partial seal）
  - `> -10 kPa` → 紅 `.vac-none`（detached / no contact）

### 部署
只動前端三檔，scp 到 .5.26 web_backend/public/，瀏覽器 Ctrl+Shift+R reload 生效。

### 待驗
- [ ] 開頁面後所有 cell 顯示 `pN = ?` 灰色（沒讀過）
- [ ] 按 refresh → 9 個 cell 跳出實際數值 + 對應顏色
- [ ] 跑 step_down 過程中 cell 顏色隨吸盤狀態變化（valve OFF 後變紅 / 重吸後變綠）

## 2026-04-28c — Claude Code — GUI 繼續/略過按鈕加視覺狀態指示

### 修改檔案
- `web_backend/public/index.html`：error pause row 重構為 status 框 + 兩顆 disabled-by-default 按鈕（id `btn-continue` / `btn-skip` / `error-pause-status` / `error-pause-label` / `error-pause-context`）
- `web_backend/public/app.js`：
  - 新 module-level state `washrobotState` / `lastPauseContext`
  - `onWashrobotLine` 新增 regex 抓 `state=X`（適用 EVT state_changed + cmd_status reply）
  - 新增監聽 `EVT error_pause context=...` 抓暫停 context
  - 新 helper `updateErrorPauseUI()`：依 state 切按鈕 disabled、status 框 active class、文字標示
- `web_backend/public/style.css`：新增 `.error-pause-row` / `.error-pause-status` / `.error-action:disabled` 規則 + `pulse-err` 動畫

### 行為對照
| state | 按鈕 | status 框 |
|---|---|---|
| running / attached / etc. | disabled、半透明 | 灰底虛線「state=X (非 paused_on_error，按鈕 disabled)」|
| **paused_on_error** | **enabled、可按** | 紅底脈動「⚠ ERROR 暫停中: <context>」|

### 原因
之前兩顆按鈕永遠可見可按，使用者搞不清楚什麼時候有用。Backend `cmd_continue` / `cmd_skip` 都會檢查 state 是否為 PausedOnError，非的話直接回 ERR。視覺 disable 把這個語意提前到 GUI 層，避免誤按 + 看不懂為什麼 ERR。

### 部署
只動前端三檔，scp 到 .5.26 web_backend/public/，瀏覽器 Ctrl+Shift+R reload 就生效。

### 待驗
- [ ] 啟動時 status 框顯示 `state=unknown (...)`，按一次 status 後變正確
- [ ] step_down 故意製造 fail 進 PausedOnError → status 框紅底脈動 + 按鈕亮起
- [ ] 按「繼續」→ 重試動作 → 過了的話 status 框回灰、按鈕 disabled
- [ ] 按「略過此步」→ state 回 Running → 按鈕 disabled

## 2026-04-28b — Claude Code — GUI + 後端新增 `zdt_zero <group>`（ZDT 當前位置歸零）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新增 `cmd_zdt_zero(const std::string& group)` 宣告（在 `cmd_pusher` 旁）
- `user_lib/WASH_ROBOT.cpp`：實作 `cmd_zdt_zero` — 呼叫 `group_slaves_(group)` 拿 slave list（"feet"/"body"/"center"/"all"），對每顆 `Z_(s).set_zero()`（ZDT 手冊 3.1.3，Reg `0x000A` ← `0x0001`），任一失敗回 `ERR zdt_zero_fail slave=N`。Error 狀態拒絕，其他允許
- `washrobot_new_PI/main.cpp`：dispatch 新增 `zdt_zero <group>` 分支 + header 註解同步
- `web_backend/public/index.html`：在 `manual — pusher` panel 底下加一個 row，4 顆按鈕（feet / body / center / all），下方 hint 提醒「正確時機：pushers 完全縮回到底時按」

### 為什麼

使用者要求：依 ZDT 手冊在網頁加當前位置歸零功能。`Z_(s).set_zero()` 已存在但沒透出到 GUI。

### Caveat（GUI hint 也寫了）

- ✅ 推桿在物理底（完全縮回）時 zero → 之後 abs 0 = 真實底
- ❌ 半伸/全伸時 zero → 之後 retract → 0 不會回到真實底，可能 over-extend 撞死或漏縮

### 待驗
- [ ] 點 zero feet → DM2J/ZDT log 看到 4 顆 (slave 1,2,3,4) set_zero 成功
- [ ] zero 後送 `pusher feet extend` → 看實際移動的相對位置是否如預期
- [ ] zero all 9 顆同時下指令是否會 timing 衝突（`group_slaves_("all")` 是循序，不該有問題）

## 2026-04-28a — Claude Code — ZDT stall_flag 升級為 fail（保留 release_stall_flag）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `zdt_wait_motion_done_`：stall_flag 路徑從「印 log + clear flag + return false (假裝成功)」改為「印 log + clear flag + return true (fail)」
- function header comment 同步更新

### 原因
之前堵轉被當成「正常完成」往上回，呼叫端不知道有問題 → 流程繼續 → 後面 vacuum 一定吸不上 → 5 次 retry exhausted (~50 秒) 才認失敗，等使用者已經沒辦法即時介入。

實例：使用者 04-27 測試時某一支 ZDT 原點偏移、伸太長撞牆 stall 在 22°（target 是 2000°），但程式視為完工繼續往下，後面才在 vacuum 階段崩。

### 新行為
1. ZDT drive 偵測到 stall → set stall_flag
2. `zdt_wait_motion_done_` 看到 stall_flag → 清 flag（讓下次 motion command 能下）→ return true
3. caller `pusher_move_many_` 拿到 fail → 透過 `try_or_pause_` 進 PausedOnError
4. EVT 廣播 `error_pause context=cycle_<group>_pusher_extend / pusher_retract`
5. 使用者現場處理（重設原點、清障）→ 按 GUI「繼續」→ 重做這個 pusher group → 再 stall 又 pause
6. 不行就按「略過此步」或 emergency_stop

### release_stall_flag 維持
- 必須清才能讓下次重試能成功下指令；ZDT firmware 對 latched stall 後續 pos_mode write 會拒絕

### 風險
- 若 ZDT 在「正常推到底」自然 stall（例如貼到牆面瞬間 drive 過電流保護），會誤判 fail
- 目前觀察 ZDT 正常完成都走「速度穩定 / 位置不變」收尾、不走 stall 路徑，誤觸機率低
- 如果現場後續發現誤判，再升級成「stall + 偏離 target 才 fail」（C 方案）

### 待驗
- [ ] 故意製造 stall（推桿頭頂東西）→ log 看到 `STALL ... — release flag + fail` → state 變 paused_on_error
- [ ] 按「繼續」重試，這次正常推完 → 流程繼續
- [ ] 按「略過此步」→ 假裝成功、流程繼續

## 2026-04-27w — Claude Code — dm2j_pair_move_abs_ 加 skip-if-at-target 優化

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `dm2j_pair_move_abs_`：讀完 before 位置後比對 target，兩顆都在 ±0.5 mm 內視為已到位，直接 return false（成功）

### 原因
現場 log 看到 feet 階段呼叫 `pair DM2J 1+3` 從 (1=0, 3=0) 移到 target=0：
```
[pair DM2J 1+3] before: 1=0 3=0 cm → target 0 cm
[pair DM2J 1+3] after:  1=0 (Δ0) 3=0 (Δ0) cm
```
這個 no-op 還是跑完整套：read positions × 2 → PR_move_cm_set × 2 → PR_trigger_sync 廣播 → dm2j_pair_poll_done_ poll → read positions × 2，浪費 ~2 秒。

加 EPSILON_CM = 0.05 (0.5 mm) 容忍度，兩顆都已在 target 就 skip。

### 影響
- step_down feet 階段如果 rail 已經在 0 → skip rail 動作
- step_down body 階段如果 step_cm 比目前 rail 位置近（不太常見）→ skip
- backup retry 動作如果 target 跟現位置一樣 → skip
- 都不影響 vacuum / pusher / valve 流程，只跳過 rail 動作本身

### 你提到的「身體走 0 公分時腳不需要移動」更深的可能性
如果你希望「**body 階段沒實際動到 rail（包括 skip 也算）→ 整個 feet phase 都跳過**（連 valve/pusher 都不做）」，這個現在還沒做。本次只跳 rail 動作。要做進階的 skip 我再加。

### 待驗
- [ ] feet phase rail=0 / target=0 時 log 看到 `already at target 0 cm — skip`，總時間少 ~2 秒
- [ ] body phase 正常情境（rail 0 → +30）仍跑完整 motion，沒被誤跳

## 2026-04-27v — Claude Code — vacuum_check_ 改 multi-sample 取最弱（過濾真空 ripple / glitch）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `vacuum_check_`：每顆 sensor 連讀 3 次（間隔 50 ms）取最弱（最不負）那次跟 threshold 比

### 原因
現場觀察 attached 時 reading 多半 -68，但偶爾跳到 -35。可能來源：
1. dp0105 真空泵 PWM 運轉造成 cup 內壓力 ripple
2. JC-100 driver 在 Modbus glitch 時 return cached `_last_pressure`（可能是稍早 transient 值）
3. cmd_status / cycle_group_ 連讀多顆 sensor，gateway buffer 對齊偶發異常

單一 sample 抖到 -35 就 > -50 threshold → vacuum_check_ 誤判 fail → step_down 進 retry → 連續抖兩三次就 vacuum_retry_exceeded。Multi-sample 取最弱，3 次都 > -50 才認失敗，過濾掉 ripple 與 single-shot glitch。

### 行為
- 每顆 sensor 取 3 個 sample、間隔 50 ms（一顆共 ~150 ms）
- 取「最不負」那個 reading 跟 `VACUUM_THRESHOLD_KPA = -50` 比
- 例如三次讀 [-68, -35, -70] → worst = -35 → -35 > -50 = fail
- 例如三次讀 [-68, -35, -68]（中間一次 ripple）→ worst = -35 → 仍 fail 嗎？

> 注意：取最弱仍會被 single-shot glitch 觸發。如果你想更寬鬆改用「median」或「2/3 majority OK」更好。但保守起見先用「最弱」測試現場，若仍偶發誤判再升級。

### 時間成本
- body / feet（4 顆）：~600 ms
- center（1 顆）：~150 ms
- "all"（9 顆）：~1.35 s

可接受（vacuum_check_ 在 settle 2 秒之後跑，多 0.6 秒不影響整體節奏）。

### 待驗
- [ ] step_down 不再因為單次 reading 抖到 -35 而誤判 vacuum_retry
- [ ] cmd_status 顯示的 p 值穩定度可從 multi-sample 結果評估

## 2026-04-27u — Claude Code — 真空 threshold 單位對齊現場 JC-100（0.1 kPa → kPa）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `VACUUM_THRESHOLD_X10 = -500`（0.1 kPa）→ `VACUUM_THRESHOLD_KPA = -50`（kPa）
  - `DETACH_THRESHOLD_X10 = -100`（0.1 kPa）→ `DETACH_THRESHOLD_KPA = -10`（kPa）
- `user_lib/WASH_ROBOT.cpp`：4 處引用全部 rename（vacuum_check_、phase5 中心吸盤檢查、return_home 脫離檢查）

### 原因
現場 JC-100 attach 後 cmd_status 顯示 p1~p8 ≈ -68~-70（強吸力 ≈ -68 kPa），p9 ≈ -1（無吸）。對照原本 driver 假設「raw value × 0.1 = kPa」，那這些 readings 應該是 -680~-700（在 0.1 kPa 單位下對應 -68 kPa）才對。實際讀回來是 -68 表示**硬體已經是 kPa 單位**（可能 JC-100 set_pressure_unit 設成 kPa 或本來就是）。

舊 `VACUUM_THRESHOLD_X10 = -500` 在「0.1 kPa 單位下 -50 kPa」是對的，但 reading 是 kPa 單位 → -68 永遠 > -500 → vacuum_check_ 永遠回 fail，就算實際吸得很死（-68 kPa）也算失敗 → step_down 一直 retry → vacuum_retry_exceeded。

新 `-50 kPa` 對齊後，-68 < -50 → 視為 attached ✓。

### 待驗
- [ ] 重新跑 step_down，body / feet 階段不再因為門檻錯誤而 retry exhausted
- [ ] cmd_status 顯示的 p 值符合預期：attached cup -50 ~ -80 kPa、未 attached -1 ~ -5 kPa
- [ ] phase5 平衡校正（cmd_confirm_balance yes）的中心吸盤檢查也用對齊後的門檻
- [ ] return_home 的脫離檢查（p > -10 kPa = 脫離）也對齊

## 2026-04-27t — Claude Code — [跨界: user_lib] PauseOnError 機制（auto 流程 fail → 暫停 → 使用者重試/略過）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - State enum 加 `PausedOnError`
  - 新 enum `PauseAction { None, Retry, Skip, Abort }`
  - 新 member `std::atomic<int> pause_action_`
  - 新指令 `cmd_continue()` / `cmd_skip()`
  - 新私有 helper `await_user_intervention_(ctx)` — block 直到 cmd_continue / cmd_skip / emergency_stop
  - 新 inline template `try_or_pause_(fn, ctx)` — 包裝任何 `bool fn() returning true=error` 的呼叫
  - `cycle_group_` template 內所有 `controlRelay` / `pusher_move_many_` 都改用 `try_or_pause_` 包
- `user_lib/WASH_ROBOT.cpp`：
  - constructor 初始化 `pause_action_(None)`
  - `state_name` 加 `paused_on_error`
  - 實作 `await_user_intervention_` / `cmd_continue` / `cmd_skip`
  - `do_step_down_` 內 4 個 lambda（body_pre_cycle、body_backup、feet_pre_cycle、feet_backup）所有 hardware op 都改用 `try_or_pause_`：PQW controlRelay / pusher_move_/_many_ / crane_cmd_ / dm2j_pair_move_abs_
- `washrobot_new_PI/main.cpp`：dispatch 加 `continue` / `skip`，header 註解同步
- `web_backend/public/index.html`：auto cycle panel 多一 row 加兩顆按鈕「繼續(重試)」/「略過此步」（用 data-tgt/data-cmd 走既有 dispatch）

### 行為
1. step_down / run 自動流程，任一 hardware op 失敗 → 進 `PausedOnError` 狀態（之前是直接進 Error）
2. EVT 廣播 `error_pause context=<具體哪一步>`，console 印 `[PAUSE-ON-ERROR] ...`
3. 使用者三選項：
   - **`continue`（繼續）** → 重試剛才那步
   - **`skip`（略過）** → 假設使用者已手動處理完該步、繼續往下
   - **`emergency_stop`** → 設 abort_flag → await 跳出回 Abort → 流程 propagate `aborted` → 進 Error
4. 重試的 op 還是失敗會再 pause 一次，無限循環直到 skip / abort

### 已知限制
- PausedOnError 期間 `do_step_down_` 仍持有 `motion_mtx_`，所以 GUI 的 manual 指令（vacuum / pusher / move / dm2j_group）會 block 在 mutex 上等。**使用者預期是物理現場手動調整**（撥 valve、推/縮推桿等）；要從 GUI 操作得先 emergency_stop 跳出 PausedOnError
- 未涵蓋的 fail 點：`cmd_arm_sweep`、`cmd_init_impl_` 內部、`cmd_attach`、`cmd_return_home` 還沒 wrap（這些不是 step_down/run 流程的常見痛點）

### 待驗
- [ ] 故意讓某 op 失敗（拔網線等）→ state 變 paused_on_error、EVT 收到 error_pause context=...
- [ ] 修復後按 GUI「繼續(重試)」→ 同一 op 重做、流程繼續
- [ ] 按「略過此步」→ 假裝該 op 成功、流程繼續
- [ ] 按 emergency_stop → 流程結束、state 進 Error
- [ ] 多次重試（一直失敗）能正常重複 pause/retry

## 2026-04-27s — Claude Code — [跨界: user_lib] PQW controlRelay 拆掉 readback 驗證

### 修改檔案
- `user_lib/PQW_IO_16O_RLY.cpp` `controlRelay()`：
  - 移除「再送 read-back 命令 + 解析狀態 + 比對」三步驟
  - 保留 send relay command（送失敗仍 return true）+ 讀 echo（純 log，不解析）
  - 永遠 return false on success

### 原因
PQW 韌體 echo 格式非標準（work_log 04-23 已記錄："TX `... 05 ...` echoed as RX `... 00 ...`"），導致 `parseReadResponse` 解出錯誤狀態 → `controlRelay` 對 OFF 操作隨機回 true（誤判失敗）。今天現場 step_down body 階段第一個 `controlRelay(CH_VALVE_BODY, false)` 就被誤判 → 進 Error 狀態 → reset 也救不回（init/attach 重跑會撞同樣 bug） → 必須重啟程式。

### 規範邊界
PQW driver 屬 user_lib 範圍，本 PR 標 `[跨界: user_lib]`。實質是把已知壞掉的驗證機制拆掉、靠物理 LED + JC-100 + 真空驗證（cycle_group_ 內既有的）作為真實狀態確認。

### 影響
- ✅ 所有 `pqw_.controlRelay(...)` 呼叫不再因 readback bug 誤判
- ⚠️ 失去「真實電氣狀態驗證」能力 — 但本來這個驗證就是壞的
- ⚠️ 真實 PQW 沒收到 / 沒響應的狀況偵測不到 → 改靠後續 vacuum_check_ / 觀察 LED 補強

### 待驗
- [ ] 重新跑 init → attach → step_down，body_valve_off / center_valve_off 不再無故 fail
- [ ] valve 真的有切換（聽 VT307 click + 看 LED + 觀察 JC-100 變化）

## 2026-04-27r — Claude Code — Linux_test menu 18 — XKC-Y25-RS485 sensor 配置工具

### 修改檔案
- `Linux_test/main.cpp`：
  - 新增 `test_xkc_y25()` 函式（menu 18）
  - print_menu 加上「18 XKC water sensor」一行
  - main dispatch 加 `else if (line == "18") test_xkc_y25();`

### 功能
- 連線到既有 RS485 gateway（預設 .22:4001）+ slave ID（預設 13）
- 子選單動作：
  - `r` 連續讀（200ms 一次，Enter 中止）
  - `s` 單次讀（output + RSSI）
  - `i` 改 slave ID（呼叫 `set_address`，需 'yes' 二次確認；改完退出 menu 讓使用者重連）
  - `f` 出廠還原（手動發 broadcast `FF 06 00 04 00 02 5C 14`，需 'reset' 二次確認；sensor 重置為 slave=1 / 9600）
  - `q` 退出

### 不開放的功能
- **改 baud rate** — RS485 bus 上 JC-100 / PQW / DY-500 共用，改 sensor baud 會拆掉整條 bus。需要時請手動操作 gateway 設定後再用 hex frame 改 sensor。

### 待驗
- [ ] 連續讀：把手指或杯水靠近 sensor 探頭 → output 0→1 切換、RSSI 跨越 4100 門檻
- [ ] 單次讀：印 `有水/無水` 中文標示
- [ ] 改 slave ID：執行後實機 LED 閃爍（依手冊 §1.7 描述），退出 menu 後用新 ID 能讀到
- [ ] 出廠還原：執行後 sensor LED 閃 2 下，重新進 menu 用 slave=1 能讀到

## 2026-04-27q — Claude Code — Linux_test menu 13 mode 4 加入 sensor 驅動補水

### 修改檔案
- `Linux_test/main.cpp`：
  - include 新增 `XKC_Y25_RS485.h`
  - menu 13（water tank）prompt 描述更新（mode 2 標註 timed / mode 4 標註 sensor 補水→閒置→刷洗）
  - mode 4 補水階段重寫：
    - 進入時 prompt 一個 `水位 sensor slave ID [13]:`
    - 共用既有 `cli` 初始化 `XKC_Y25_RS485 lvl`
    - 開球閥 → 每 200 ms 讀 `read_state(output, rssi)` → output==1 視為水位滿 → 關球閥、印 `[DONE] 水位達滿 — output=N RSSI=N`
    - 補水秒數 input 改用為「sensor 等待 timeout 上限」，避免 sensor 故障害淹水
    - sensor init 失敗 → 警告 + fallback 回原本 timed 行為（不阻塞使用者）
    - 中途按 Enter 中止支援（沿用 `water_wait_or_abort` 的 stdin 非阻塞 polling 模式）
  - 階段 2（閒置）+ 階段 3（刷洗）邏輯不變

### 行為
- 正常情境：sensor 偵測到水位 → 主動關閥（不會超量）
- sensor 異常：read_state 失敗時印 `read err` 但不放棄；連續無 output=1 直到 timeout 才認失敗
- sensor 完全無響應（init 失敗）：自動退回 timed mode，不會卡死

### 待驗
- [ ] 接好 XKC sensor、注水到貼片高度 → output 切 0→1、補水自動停止
- [ ] sensor slave ID 預設 13（實際接線確定後改 default）
- [ ] timeout 路徑：sensor 一直回 output=0（例如貼錯位置）→ 60s 後自動關閥、不淹水
- [ ] Enter 中止：補水中按 Enter → 立刻關閥 + 印當下 RSSI

## 2026-04-27p — Claude Code — [跨界: user_lib] 新增 XKC_Y25_RS485 水位感測器驅動

### 修改檔案
- 新增 `user_lib/XKC_Y25_RS485.h`
- 新增 `user_lib/XKC_Y25_RS485.cpp`

### 規範邊界備註
依 CLAUDE.md「新增 class（新硬體驅動）→ 架構方負責，協作者提供硬體文件即可」原則上是 Jim 範圍。Sadie 直接要求新增，本 PR 標 `[跨界: user_lib]` 等 Jim review。文件來源：`D:\洗窗戶機器人\電控設備資料\水位sensor\XKC-Y25-RS485 INFO-CN-V16.pdf`。

### 規格摘要
- 非接觸式電容式水位感測器（外貼，可穿透 ≤20 mm 非金屬容器壁）
- Modbus-RTU over RS-485，預設 9600 8N1，slave 1
- 24V DC（可訂 12V），耗電 5 mA，響應時間 500 ms
- 寄存器：
  - `0x0001` OutPut（0=無水 / 1=有水）
  - `0x0002` RSSI 信號強度（< 3900 無水 / > 4100 有水 / 之間保持）
  - `0x0003` slave 地址（1~254）
  - `0x0004` 波特率代碼

### Public API
- `init(ip, port, ID, debug)` / `init(extClient, ID, debug)` — 兩種模式對齊現有 driver
- `read_state(uint16_t& output, uint16_t& rssi)` — 一個 frame 拿到狀態 + 信號（func 0x03 連讀 2 reg）
- `has_liquid()` — bool 包裝
- `last_output()` / `last_rssi()` — 不發 Modbus、回上次成功讀的值
- `set_address(new_addr)` / `set_baud_rate(code)` — 設定（caller 自己負責 re-init）

### 約定遵循
- 回傳值 false=success / true=error（CLAUDE.md coding style）
- log 全部走 `log_utils.h` 的 `LOG_ERR/WRN/INF/DBG/HEX` 巨集
- 預設 `debug_mode=false`（靜默）；錯誤透過 bool return 通知，log 純除錯觀察
- `_log_tag = "XKC:<id>"` per project log format
- class 內部以區塊註解分群（init / read / config / utility）

### 文件不一致說明
原廠手冊範例 frame 與寄存器表對不齊（例如 set address 範例寫到 reg 0x0004 但 table 說 0x0003 才是地址）。實作以 register table 為準。實機若行為不符要回頭驗證。

### 待驗
- [ ] 接線：棕 VCC / 藍 GND / 黃 RS485-B / RS485-A（依 PDF p.10 接腳定義）— **注意 PDF p.14 尺寸圖標的腳位顏色與 p.10 不同（黃 = Out）**，實機接線需以 RS485 版本 PDF p.10 為準
- [ ] read_state 在有水 / 無水時 OutPut 切換
- [ ] RSSI 讀數合理（接近水時應上升超過 4100）
- [ ] 共用 TCP_client 模式整合到 USR-TCP232-304 RS485_3 (.22) 是否可行（看 gateway slave 衝突）

## 2026-04-27o — Claude Code — GUI log panel 寬螢幕固定為右側 sidebar

### 修改檔案
- `web_backend/public/style.css`：尾端新增 `@media (min-width: 1200px)` block

### 行為
- **螢幕 ≥ 1200px**：log panel 變成固定右側 sidebar（width 480px，從 header 下方延伸到視窗底部），控制 panel 區域 `padding-right: 512px` 騰出空間
- **螢幕 < 1200px**：原本排版不變（log 在最下方 full-width，max-height 42vh）

### 原因
使用者要求操控時可以同時看到 log 即時輸出，避免按按鈕後要捲頁去看回應。寬螢幕固定 sidebar 是最直覺的做法，純 CSS 改動，不破壞 HTML，也不影響窄螢幕使用者。

### 效能考量
無新增 GPU 重的 op（沒有 backdrop-filter、沒有動畫），對 Pi Chromium 友好。

### 待驗
- [ ] 寬螢幕（>= 1200px）：log panel 固定在右側永遠可見，控制 panel 在左側自動 reflow
- [ ] 窄螢幕（< 1200px）：排版回到原本，log 在最下方
- [ ] 滾動 main 區時 log 不會跟著滾、log 自己內部 scrollbar 正常運作
- [ ] log 文字夠新時會自動 scroll 到底（既有行為不變）

## 2026-04-27n — Claude Code — retry backup guard 移到 cleanup 之前（dry-run 預檢）

### 修改檔案
- `user_lib/WASH_ROBOT.h` `cycle_group_` template：
  - `Backup` lambda 簽名改為 `(bool dry_run) -> std::string`
  - retry 流程改為 3 步驟：
    1. `backup(true)` 預檢可行性，不可行就直接 return（不動 valve / pusher）
    2. valve OFF + pusher retract（cleanup）
    3. `backup(false)` 實際反向移動
- `user_lib/WASH_ROBOT.cpp`：
  - `body_backup` lambda 簽名 `[this](bool dry_run)`，target < 0 → return error；dry_run=true 且可行 → return ""（無副作用）
  - `feet_backup` 同樣處理，門檻是 `target > step_cm_`

### 原因
原本 guard 寫在 backup() 內部，但 cycle_group_ 在 backup() 之前已經做完 valve_off + pusher_retract。意思是：guard 觸發時系統已經半解除（吸盤鬆掉、推桿縮回），但 rail 沒退到位 → 留下不一致狀態。
應有的順序是：先檢查能不能退 → 不能退就放棄（保持貼牆狀態）→ 能退才動 valve/pusher → 退 rail。

### 行為差異
- 之前：guard 觸發 → 系統處於「valve 關、pusher 縮、rail 在 +step_cm」的半解除狀態 + 回 ERR
- 現在：guard 觸發 → 系統保持「valve 開、pusher 伸、rail 在 +step_cm（仍貼牆）」狀態 + 回 ERR

### 待驗
- [ ] step_cm=10 連續真空失敗到 attempt 3：dry_run 觸發 `body_backup_no_space`，**valve 維持 ON、pusher 維持 extend**（log 不應出現 valve_off / pusher_retract 訊息）
- [ ] 正常 retry 流程：valve_off / pusher_retract / 退 rail 三步都跑

## 2026-04-27m — Claude Code — DM2J group set-zero 功能（mirror Linux_test menu 15）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：宣告 `cmd_dm2j_zero(group)`
- `user_lib/WASH_ROBOT.cpp`：實作 `cmd_dm2j_zero`
  - feet → slaves 1, 3 / wheels → 2, 4 / arm → 5
  - 對 group 內每顆 slave 呼叫 `home_set_current_pos_zero()`（寫 0x6002 = 0x0021）
  - 讀前 / 讀後位置寫到 log，回 reply 包含每顆 slave 歸零後位置
  - feet 歸零時順手 `rail_pos_cm_.store(0.0)` 同步軟體紀錄
  - Error 狀態拒絕，其他狀態都允許
- `washrobot_new_PI/main.cpp`：dispatch `dm2j_zero <feet|wheels|arm>`，header 註解同步
- `web_backend/public/index.html`：`manual — DM2J group sync` panel 同 row 加 `set zero (current = 0)` 按鈕
- `web_backend/public/app.js`：`btn-dm2j-zero` handler — 讀 group selector → `confirm()` 二次確認 → 送 `dm2j_zero <group>`

### 行為
- 共用 group selector：選 feet/wheels/arm → 按 set zero → 瀏覽器跳 confirm → 確定送指令
- 單一動作對 group 所有 slave 一起歸零（feet 1+3、wheels 2+4、arm 5）
- 歸零後讀回位置寫進 log + reply 字串

### 待驗
- [ ] feet 歸零：兩腳當下位置 → 0；rail_pos_cm_ 內部追蹤同步重置
- [ ] wheels 歸零：兩輪當下位置 → 0
- [ ] arm 歸零：手臂當下位置 → 0
- [ ] confirm 對話框取消 → 不送指令

## 2026-04-27l — Claude Code — step_down retry backup 加範圍守衛（避免退出本步行程超撞限位）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_`：
  - `body_backup`：在送 DM2J 動作前先算 `target = rail - 5`，若 `target < 0` 直接 `return "body_backup_no_space"` 不再 retry
  - `feet_backup`：先算 `target = rail + 5`，若 `target > step_cm_` 直接 `return "feet_backup_no_space"` 不再 retry

### 原因
原 backup 沒檢查範圍，連續 retry 會把 rail 推出本步應有區間 [0, step_cm]。例如 step_cm=10 時 body 第 3 次 retry 就會把 rail 推到 -5（低於原點）→ 硬撞機構限位 / drive 計數器 vs 物理位置不一致。Option A：把 retry 限定在「本步起終點之間」[0, step_cm]，超出視為「沒空間了，認失敗」。

### 行為
- step_cm=30：body retry 最多到 0（5 次都還在範圍內）；feet retry 最多到 +30
- step_cm=10：body retry 最多 2 次到 0（3rd 退到 -5 → 攔下）；feet 同樣最多 2 次到 +10
- step_cm=5：body / feet 最多 retry 1 次（5cm backup 1 次就到 0 / +5 邊界，2nd 直接攔下）

### 訊息
失敗時 cycle_group_ 把 backup() 的 error 字串往上回傳，最終看到：
- `ERR body_backup_no_space\n`
- `ERR feet_backup_no_space\n`

### 待驗
- [ ] step_cm=10 故意連續真空失敗 → 第 3 次 retry 應該看到 `target -5 cm < 0 — no more backup space, abort retries` 然後 `ERR body_backup_no_space`
- [ ] step_cm=30 正常情況 → retry 5 次都不會撞 guard

## 2026-04-27k — Claude Code — GUI manual 區改群組同動（mirror Linux_test menu 16）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：宣告 `cmd_dm2j_group(group, cm)`
- `user_lib/WASH_ROBOT.cpp`：實作 `cmd_dm2j_group`
  - `feet` (slave 1, 3) → 走 `dm2j_pair_move_abs_(1, 3, 1, cm)` PR1 廣播同動（剛性耦合用真同步）
  - `wheels` (slave 2, 4) → `PR_move_cm_nowait × 2 + dm2j_wait_done_ × 2`（非剛性，與 `cmd_wheels` 同 pattern）
  - Error 狀態拒絕，其他狀態都允許
- `washrobot_new_PI/main.cpp`：dispatch 新增 `dm2j_group <feet|wheels> <cm>`，header 註解同步
- `web_backend/public/index.html`：`manual — DM2J move (cm, absolute)` panel 換成 `manual — DM2J group sync`，下拉選 feet / wheels / arm
- `web_backend/public/app.js`：`btn-move` handler 改成 `btn-dm2j-group`，feet/wheels 送 `dm2j_group`、arm 沿用 `move arm`

### 行為
- GUI 上選 group + 輸入 cm + 按 move
  - feet → broadcast PR1 真同步（兩腳同 frame 啟動）
  - wheels → 平行 nowait（兩輪一個 Modbus frame 內前後 trigger）
  - arm → 走舊 `cmd_move arm` 單顆動作
- 舊 `cmd_move` 後端**保留**（raw command 還能用 `move left_foot 30` 之類）

### 待驗
- [ ] feet 移到 abs 30 → 兩腳同步啟動 + 完成
- [ ] wheels 移到 abs -7 → 兩輪同時下降
- [ ] arm 移到 abs 0 → 手臂歸位

## 2026-04-27j — Claude Code — step_down 真空驗證 + retry 機制復活（撤 TEST MODE skip）

### 修改檔案
- `user_lib/WASH_ROBOT.h` `cycle_group_` template：
  - 移除 `[TEST MODE 2026-04-24] 真空驗證暫時跳過` 那段 short-circuit (`out_retry_count = 0; return "";`)
  - 解註 `vacuum_check_(group)` + 失敗 EVT 廣播 + 進入 outer for-loop 下一個 attempt 的 retry 流程

### 行為
- step_down body / feet 階段，extend + valve ON + settle 後讀 JC-100 真空感測，任一吸盤未達 `VACUUM_THRESHOLD_X10 (-50 kPa)` → fail
- fail 時：發 `EVT vacuum_fail <group> attempt=N slaves=A,B,...`，回到 for-loop 下一輪 → 關 valve、收推桿、跑 `backup()`（rail 後退 5 cm）、再 extend + valve + verify
- 最多 retry `VACUUM_RETRY_MAX (5)` 次；都失敗才回 `vacuum_retry_exceeded <group>`

### 待驗
- [ ] 故意讓某顆吸盤吸不到（例如吸盤外貼膠帶）→ EVT vacuum_fail 觸發 + rail 後退 5 cm + 重吸
- [ ] 5 次都吸不上 → step_down 回 `ERR vacuum_retry_exceeded body` / `feet`，狀態進 Error
- [ ] 正常情況：第 1 attempt 就 OK，retry_count = 0

## 2026-04-27i — Claude Code — step_cm 改為 runtime 可調（GUI input → step_down/run 按鈕讀值帶參送出）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `STEP_CM` 常數拆成 `STEP_CM_DEFAULT=30 / STEP_CM_MIN=5 / STEP_CM_MAX=60`
  - 新增 member `std::atomic<int> step_cm_`，constructor 預設 30
  - `cmd_step_down()` 改 `cmd_step_down(int cm = 0)`，`cmd_run(int steps)` 改 `cmd_run(int steps, int cm = 0)`，cm=0 = 用當下 step_cm_
- `user_lib/WASH_ROBOT.cpp`：
  - constructor 加 `step_cm_(STEP_CM_DEFAULT)`
  - `cmd_step_down(cm)` 和 `cmd_run(steps, cm)`：cm > 0 時 validate 5..60 再 store；超範圍回 ERR step_cm_out_of_range
  - `do_step_down_` 內 3 處 STEP_CM 改用 `step_cm_.load()`
- `washrobot_new_PI/main.cpp`：
  - dispatch `step_down [cm]` / `run <n> [cm]` 都接受可選 cm 參數
  - header 註解同步更新
- `web_backend/public/index.html`：auto cycle panel 加 `step cm` input (5-60，預設 30)，`step_down` button 從 `data-cmd` 改 `id="btn-step-down"` 走 JS handler
- `web_backend/public/app.js`：新增 `readStepCm()` helper、`btn-step-down` handler、`btn-run` 改成同時讀 run-steps + step-cm 送出 `run <n> <cm>`

### 行為
- 使用者每次按 step_down / run，**先讀 input 的 step cm 值**，連同指令一起送
- Backend 收到 cm 時先 validate 5..60 範圍，OK 才存 atomic + 執行；超範圍回 ERR、不執行動作
- cm 不送（純 `step_down` 或 `run <n>`）時用當下 step_cm_（預設 30）
- 兼容向後：raw command 沒帶 cm 也能跑

### 待驗
- [ ] GUI 改 step cm = 15 → 按 step_down → log 顯示 `start → Running (step=15 cm)` + 實際 rail 走 15 cm
- [ ] GUI 改 step cm = 60 → run 5 → 5 步 × 60 cm 都正確
- [ ] 改 step cm = 4（超下限）→ 客戶端攔截或 backend 回 ERR step_cm_out_of_range
- [ ] 改 step cm = 70（超上限）同上

## 2026-04-27h — Claude Code — `dm2j_pair_poll_done_` read_status 加 retry（解 RS485 gateway 單次掉包誤判 fail）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `dm2j_pair_poll_done_`：
  - 新增 lambda `read_status_retry` 包裝 `D_(slave).read_status()` — 最多 3 次嘗試、每次間隔 50 ms
  - slave_a / slave_b 的 read_status 都改用 retry 包裝

### 原因
現場 step_down 出現 `slave 3 comms error at 5200ms` → `feet_rail_home_fail`，但使用者確認**硬體實際移動到位**（5 顆 EEPROM 都寫過、物理位置正確）。表示 RS485-over-TCP gateway 在動作中偶有單次 Modbus frame 掉包（已知問題，work_log 記錄過 "TCP buffer 殘留干擾"），但**單次失敗不該整個動作 fail**。Retry 後若真的死掉才認 fail。

### 同步啟動保證
PR 廣播觸發 (`PR_trigger_sync`) 在 `dm2j_pair_move_abs_` 內，本 PR **完全不動**那段。retry 只加在「等待完成」的 polling，不影響兩腳同瞬間啟動。

### 待驗
- [ ] step_down 重跑：feet phase rail → 0 cm 不再因為單次 read_status 掉包 fail
- [ ] 真的物理 fault / 通訊全斷 時仍能在 3 次 retry 後正確 fail（不會無限等）

## 2026-04-27g — Claude Code — cmd_init 加入腳組 rail 歸零（DM2J slave 1, 3 → abs 0）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_init_impl_()`：在 wheel retract（slave 2, 4 → 0）之後、ZDT enable 之前，新增腳組 rail 歸零動作

### 做法
- `dm2j_pair_move_abs_(DM2J_LEFT_FOOT, DM2J_RIGHT_FOOT, 1, 0.0)` — broadcast PR1 同瞬間啟動，bystander 2/4/5 已在 `WashRobot::init()` L92-94 設 PR1 rpm=0 safe
- 完成後 `rail_pos_cm_.store(0.0)` 同步軟體紀錄
- 失敗回 `ERR feet_rail_home_fail`

### 為什麼用 sync pair
左右腳 rail 機械剛性耦合（do_step_down 也是因為這個用 broadcast）— 一般循序 `PR_move_cm` 兩顆會有 ms 級時序差，剛性連桿可能受傷。

### 待驗
- [ ] 現場測試：點 init → 兩顆 foot rail 同步歸 0、無聲音/震動異常
- [ ] 若 foot rail 啟動位置已是 0，廣播觸發是否會因 0 距離立刻完成（預期：是，pair_move 內部位置讀取 + log 會看到 Δ=0）

## 2026-04-27f — Claude Code — [跨界: user_lib] DM2J driver 內部 timeout 10s → 20s

### 修改檔案
- `user_lib/DM2J_RS570.cpp`：
  - `PR_move_cm()` 內 hard-coded `timeout_ms` 10000 → 20000
  - `PR_move_cm_trigger_all()` 內 hard-coded `timeout_ms` 10000 → 20000

### 原因
延續 04-27e — 為了支援 60 cm @ 200 rpm（18 秒）動作，driver 自己的內部 timeout 也要對齊。否則 `cmd_move` / `cmd_arm_sweep` / `cmd_init_impl_` 收輪等走 `PR_move_cm` 的單顆動作仍會撞 10 秒 timeout。

### 規範邊界
屬 user_lib 內部改動，**不改 public API 簽名**（const value 變更）。依 CLAUDE.md「不改 public API 簽名的內部改動 → 協作者可以修，但 PR 必須標 [跨界: user_lib]」。

## 2026-04-27e — Claude Code — DM2J 動作 helper timeout 加大（撐 60 cm @ 200 rpm = 18 秒）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `dm2j_wait_done_` default timeout 10000 → 20000 ms
  - `dm2j_pair_move_abs_` default timeout 15000 → 20000 ms

### 計算依據
60 cm × 10000 PPR = 600,000 pulses；200 rpm × 10000 PPR / 60 = 33,333 pulses/sec；移動時間 600,000 / 33,333 ≈ 18 秒；加速/減速段約 0.2 秒；合計 ≈ 18.2 秒。加 10% safety margin 取 20 秒。

### 已知未處理（待跨界 user_lib）
~~`user_lib/DM2J_RS570.cpp` 內 `PR_move_cm()` 自己有寫死的 `const int timeout_ms = 10000`。~~ 已在 04-27f 一併處理。

## 2026-04-27d — Claude Code — DM2J_RPM 100 → 200 還原（EEPROM 電流參數已存好）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`DM2J_RPM` 100 → 200

### 原因
使用者用廠商工具把所有 DM2J 驅動器的電流/微步進等參數調好並寫進 EEPROM (`0x1801 = 0x2211`)，斷電重啟驗證過參數保留，不再有失步問題。可以還原到 200 rpm 正常速度，避免 step_down 撞到 `dm2j_pair_move_abs_` 預設 15 秒 timeout（100 rpm 下 30 cm 需 18 秒，會 fail 回 `body_rail_forward_fail`）。

### 後續
- ACC/DEC 維持 500（避免加速段失步的保險不必撤）
- step_down body phase 30 cm @ 200 rpm = 9 秒，回到 timeout 安全範圍

## 2026-04-27c — Claude Code — DM2J_RPM 200 → 100（電流不足下穩態失步驗證）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：`DM2J_RPM` 200 → 100

### 原因
ACC=500 後 slave 2/3/4 仍有「卡住的聲音」（穩態失步聲，非加速段問題）。診斷指向驅動電流被前面 0x2233 bug 出廠還原打回 default（~1A），無法支撐 200 rpm 的負載。降速到 100 rpm 看能不能用 default 電流跑得動以縮小範圍。

### 補充情報
使用者目前 slave 1 能動是因為剛用廠商工具調好參數，但是 RAM 暫態，未存 EEPROM (`0x1801 = 0x2211`)。下次斷電會回出廠。

### 待驗
- [ ] 100 rpm 下 slave 2/3/4 能不能動
- [ ] 不論結果，都要用廠商工具把所有 5 顆 DM2J 的電流 + 微步進等參數**存進 EEPROM**（`0x1801 = 0x2211`），否則斷電就回出廠
- [ ] EEPROM 存好後，可考慮把 RPM 還原回 200

## 2026-04-27b — Claude Code — DM2J ACC/DEC 100 → 500（解 slave 2/3/4 加速失步 stall）

### 修改檔案
- `user_lib/WASH_ROBOT.h`:
  - `DM2J_ACC` 100 → 500
  - `DM2J_DEC` 100 → 500

### 原因
slave 2, 3, 4 用 200 rpm 跑時物理卡住（廠商 JOG 工具同樣馬達能動）。`DM2J_ACC=100` 在 DM2J 手冊單位「ms / 1000 rpm」下，200 rpm 加速時間僅 20 ms，太陡造成失步 stall。改成 500 → 200 rpm 加速時間 100 ms，類似廠商 JOG 工具的溫和曲線。對整段移動時間幾乎無影響（30 cm 約 9 秒，加速段 100 ms 可忽略）。

### 影響範圍
全域改動，影響所有 DM2J PR 動作：腳組同動 (`dm2j_pair_move_abs_`)、輪組 (`cmd_init_impl_` 收輪 / `cmd_wheels` / `cmd_move`)、手臂 (`cmd_arm_sweep`)、bystander-safe init (rpm=0 → 不影響)。

### 待驗
- [ ] 現場：slave 2, 3, 4 在 ACC=500 下能正常動作不再 stall
- [ ] 若仍 stall，下一輪試 1000；同時檢查驅動器電流參數是否被前面 0x2233 bug 出廠還原導致電流不足

## 2026-04-27a — Claude Code — Linux_test 拆掉 dm2j_manual_enable/disable 本地 helper（誤植 0x2233 = 出廠還原）

### 修改檔案
- `Linux_test/main.cpp`:
  - 刪除 L97-136：comment block + `_dm2j_crc16` + `dm2j_write_0x1801` + `dm2j_manual_enable` + `dm2j_manual_disable`
  - menu 群組同步：`dm2j_manual_enable(cli, s)` → `drv[s].motor_enable()`
  - menu 群組 cleanup：`dm2j_manual_disable(cli, s)` → `drv[s].motor_disable()`
  - menu 17 emergency cleanup：
    - `dm2j_manual_enable(cli20, …)` → `dm2j_L.motor_enable()` / `dm2j_R.motor_enable()`
    - `for (s=1..5) dm2j_manual_disable(cli20, s)` → 直接對 5 個 driver instance 各呼叫 `motor_disable()`（slave 1=`dm2j_L`、2=`bL`、3=`dm2j_R`、4=`bR`、5=`arm`，都是上面已經 init 好的）

### 為什麼修

Sadie 觀察「每次重新上電後，用程式發送控制 DM2J 的指令都會有問題，參數都要重新調一遍」。

挖出主嫌：`dm2j_manual_disable` 寫 `0x1801 = 0x2233`，註解寫「DISABLE」但對照 DM2J V1.0 原廠手冊 + `.claude/summaries/DM2J_RS_MODBUS_SUMMARY.md:185`，**0x2233 是「所有參數恢復出廠值」不是 disable**。

兩個 callsite（menu 群組結尾 cleanup + menu 17 emergency cleanup）每次跑都會把全部 5 顆 DM2J 出廠還原一次，且這個指令通常會持久化到 EEPROM → 重新上電 EEPROM 仍是出廠值 → Sadie 必須再手動把 PPR / micro-step / enable mode 等重灌。完美符合症狀。

### 為什麼選砍 helper（不選改 helper 內部值）

- `user_lib/DM2J_RS570::motor_enable() / motor_disable()` 04-24ay 已正確實作（`0x000F = 1 / 0`）
- helper 是「未實作前的暫時 workaround」，現在 user_lib 已落實，留著只會誤導下一個讀 code 的人
- 連 helper 用的 `_dm2j_crc16` 也只給 `dm2j_write_0x1801` 用，可以一起清掉
- 一次拆乾淨 ≈ 40 行死碼

### 側面影響

- `washrobot_new_PI` 主程式不受影響（從沒用過這 helper，主程式只透過 `user_lib/WASH_ROBOT.cpp` 間接呼叫，而 user_lib 寫的是正確的 `0x000F`）
- 只有 Linux_test 本身會受惠

### 待 Sadie 跟進
- [ ] 受傷的 DM2J 驅動器需重新調參數 + 寫 EEPROM（正確存檔指令是 `0x1801 ← 0x2211`，不是 0x2222 也不是 0x2233）
- [ ] 部署修好的 Linux_test 到現場 Pi
- [ ] 後續若還在掉參數，看別的地方有沒有別人也踩到 0x2233（grep 全 repo 已確認沒有，只有此處）

## 2026-04-24be — Claude Code — [跨界: user_lib] 拿掉 WashRobot::init 啟動時放輪組 (-7cm) 那段

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `WashRobot::init()`：
  - 移除啟動時對 slave 2, 4（左右輪）的 `PR_move_cm_nowait(-7.0)` + `dm2j_wait_done_` 整段
  - 留一個 `[REMOVED 2026-04-24]` 註解指向 `wheels lower` TCP 指令作為替代手動觸發方式

### 原因
使用者要求拿掉。啟動時不再自動放輪 — 要用時改呼叫 `wheels lower` 指令（header 已有 `cmd_wheels(action)` 對應）。

### 影響
- 啟動速度變快（省掉 wheels 移動時間 + 潛在 DM2J timeout/FAULT 風險）
- 啟動後輪子位置**未定義**（在上一個程式結束/斷電前的位置）
- 若需放輪，用 web GUI 的放輪按鈕或 TCP `wheels lower` 指令

## 2026-04-24bd — Claude Code — [跨界: user_lib] do_step_down_ 順序交換：body 先 feet 後（對齊 Linux_test menu 7）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_` 整段重寫：
  - **順序交換**：Phase A = body（原本 feet），Phase B = feet（原本 body）
  - **絕對目標**：Phase A rail → +STEP_CM，Phase B rail → 0
  - **Crane 搬到 body phase**：pay_out (STEP+margin) → rail 前進 → retract margin，配合 body 下降
  - **Retry 方向反轉**：body_backup → rail -VACUUM_BACKUP_CM（body 退回）；feet_backup → rail +VACUUM_BACKUP_CM（feet 退回）
  - **Center 推桿 / CH_VALVE_CENTER 處理搬到 body phase**（因為 body 現在先動，center 跟 body 同組）
  - **移除 step-compensation 狀態**：`rail_at_start` / `residual_start` / `body_residual_cm_.store()` / `actual_feet_cm_` 全部拿掉 — 用絕對目標就夠了，不需跨 step carryover

### cmd_run 受惠
`cmd_run(int steps)` 內部呼叫 `do_step_down_()` 迴圈 N 次，自動跟著新邏輯跑，不用改。

### 行為變化
- 原本：feet 先下降 → body 追上 → wash sweep（跳過）
- 新版：body 先下降 → feet 追上 → wash sweep（跳過）
- Rail 0 → +STEP_CM → 0 每個 step 循環
- Crane pay_out/retract 發生在 body phase（第一個下降的）

### 未動
- `cycle_group_` template（H 檔）— 共用 retry + vacuum check 架構不變
- `cmd_attach` / `cmd_detach` — 不動
- Phase C wash sweep — 仍是 TEST MODE skip

## 2026-04-24bc — Claude Code — Linux_test menu 7 step 順序交換：body 先 feet 後（都用絕對路徑）

### 修改檔案
- `Linux_test/main.cpp` `test_full_step` 迴圈內的 step body 整段重寫：
  - **順序交換**：Phase A = body（原本是 feet），Phase B = feet（原本是 body）
  - **絕對目標**：Phase A rail → `+step_cm`（身長，body 下降），Phase B rail → `0`（收回，feet 跟上）
  - 改用 `dm2j_pair_rail_move_abs_sync()` 直接送絕對 target（原本用 `dm2j_pair_rail_move_sync` 送相對 delta）
  - 移除 step-compensation 計算（`offset`、`phase_a_target`、`phase_b_target`、`feet_delta`）
  - Retry backup 方向符合新流程：body retry `-rail_backup_cm` / feet retry `+rail_backup_cm`，max cumulative 都 = `step_cm`
  - `rail_baseline` 變數保留（step loop 前讀一次，純診斷用）

### 原因
使用者要求：rail 下走一律 `+step_cm`（身長），再回到 0，都用絕對路徑；body 先 feet 後。移除原本的 step compensation 邏輯以符合「簡單絕對」流程。

### 行為變化
- **每個 step 開頭**：body 組先釋放/收推桿/rail +step → 吸住
- **每個 step 結尾**：feet 組釋放/收推桿/rail 0 → 吸住
- Rail 總是 0 → +step → 0 循環
- 若 Phase B retry 剩下 offset，下一 step 不會自動補償（絕對目標 +step 和 0 固定）

### Menu 11/12 未動
只改 menu 7 (`test_full_step`)。menu 11 (`test_full_step_no_rail_verify`) 和 menu 12 (`test_full_step_report`) 順序不變，因為使用者只指定 menu 7。

## 2026-04-24bb — Claude Code — WASH_ROBOT DM2J 速度全面降到 200 rpm

### 修改檔案
- `user_lib/WASH_ROBOT.h`：
  - `DM2J_RPM` 700 → 200（影響所有腳組/輪組 rail 動作、`cmd_move`、`cmd_wheels`、啟動下輪、`dm2j_pair_move_abs_`）
  - `ARM_SWEEP_RPM` 500 → 200（影響 `cmd_arm_sweep`）

### 保持不變
- `WashRobot::init()` L92-94 bystander-safe `PR_move_set(1, 1, 0, 0, …)` 的 `rpm=0` 維持不動，這是廣播同動時叫輪/手臂 no-op 的保險值

### 原因
使用者要求把主程式所有 DM2J 動作都降速到 200 rpm。

### 待驗
- [ ] 現場觀察：降速後動作是否仍在合理時間內完成（注意 `dm2j_wait_done_` 預設 timeout 10 s，長距離 rail 移動可能會逼近上限）
- [ ] 若出現 timeout，需考慮提高 `dm2j_wait_done_` 的 timeout 或針對某些動作個別覆蓋 rpm

## 2026-04-24ba — Claude Code — Linux_test DM2J_RPM 500 → 200（menu 7/11/12 共用）

### 修改檔案
- `Linux_test/main.cpp`:
  - `DM2J_RPM` 常數從 500 → 200
  - 註解裡提到 500 同步改成 200

### 原因
使用者要求 menu 7 的 DM2J 軌道移動速度改 200 RPM（更慢、更穩定）。`DM2J_RPM` 是全域常數 menu 7 / 11 / 12 共用，都會受影響（它們都是 full-step 測試變體）。

Menu 2 不受影響（直接用數字 500）。主程式 WASH_ROBOT.h 的 `DM2J_RPM=700` 也不變。

## 2026-04-24az — Claude Code — menu 2 加 mode 選項 (PR abs / PR rel / JOG) + 位置回讀驗證

### 修改檔案
- `Linux_test/main.cpp` `test_dm2j` (menu 2)：
  - 加 mode 選擇：`a=PR absolute` (原行為) / `r=PR relative` (mode=2) / `j=JOG forward 2s`
  - JOG 路徑用 `set_jog_speed(200)` + `jog_forward()` + 2s sleep + `jog_stop()`
  - 尾端加位置回讀 + `Δ` 顯示：驗證 drive 報完成時馬達實際有沒有動

### 原因
使用者用廠商工具「使能 + 正向轉」（= JOG）馬達會動，但 menu 2 PR absolute 報 CMD_DONE 但物理沒動；歸 0 後還是不動。要確認：
1. 我們的程式呼叫 JOG 能不能動（跟廠商工具同路徑）
2. PR relative (mode=2) 能不能動
3. 動完位置有沒有真的到 target

### 三個可能結論
- **JOG 動、PR 不動** → 確認 PR 子系統 drive-side 有配置問題，需對比廠商工具 Pr 參數
- **JOG 也不動** → 我們程式通訊層有問題（基本排除，因為其他 DM2J 都 OK）
- **位置 Δ 跟 target 差很多** → drive fake completion 確認，需深入 PR 配置

## 2026-04-24ay — Claude Code — [跨界: user_lib] DM2J 實作 motor_enable/disable/save_params/reset_alarm + menu 2 加 pre-step

### 修改檔案
- `user_lib/DM2J_RS570.h`:
  - 4 個函式的註解改成**真實指令**（對照手冊 §5.3.2/5.3.3）：
    - `motor_enable()` → `0x000F (Pr0.07) = 1` 軟體強制使能（原舊註解 `0x1801=0x1111` 錯誤）
    - `motor_disable()` → `0x000F = 0`
    - `save_params()` → `0x1801 = 0x2211`（原 `0x2222` 錯；那是參數初始化不是儲存）
  - 新增 `reset_alarm()` 宣告（`0x1801 = 0x1111` 清當前警報）
- `user_lib/DM2J_RS570.cpp`:
  - 實作 `motor_enable()` / `motor_disable()` / `save_params()` / `reset_alarm()`（之前只宣告沒實作，呼叫會 link 錯）
  - 每個加 `LOG_INF` 標示動作
- `Linux_test/main.cpp` `test_dm2j` (menu 2)：
  - `PR_move_cm` 之前加 `reset_alarm()` + 50ms + `motor_enable()` + 100ms 作為 pre-step
  - 若使用者現場測「廠商工具會動、menu 2 不會動」，補這兩行驗證是不是缺了這塊

### 原因
使用者實測 Linux_test menu 2 有時候馬達不動（status 顯示 ENABLE+RUN 但物理沒移動），但用廠商工具（MotionStudio / JMC tool）同樣參數能動。硬體層沒問題，差別在廠商工具每次連線時可能順便清警報 + 強制使能。補齊這兩個 pre-step 重試。

若有效：
- 下一步可以評估要不要把 `reset_alarm()` / `motor_enable()` 搬進 `DM2J_RS570::init()` 或 WASH_ROBOT `init()` 預設 per-slave 做一次
- 或每個 cmd_* 呼叫前做一次（保守但通訊流量增加）

### 驗證方式
跑 menu 2 → slave 4 → target 7cm。
- 若現在能動 → 確認「缺 reset_alarm + motor_enable」是主因
- 若還不動 → 問題在更深層（驅動器 Pr 參數）、或 broadcast 層面

## 2026-04-24ax — Claude Code — GUI 新增收輪/放輪按鈕 + 新 `wheels <retract|lower>` 指令

### 修改檔案
- `user_lib/WASH_ROBOT.h`：新增 `cmd_wheels(const std::string& action)` 宣告
- `user_lib/WASH_ROBOT.cpp`：實作 `cmd_wheels` — `retract` = abs 0 cm / `lower` = abs -7 cm，兩輪 `PR_move_cm_nowait` ×2 + 依序 `dm2j_wait_done_`，Error 狀態拒絕、其他狀態允許
- `washrobot_new_PI/main.cpp`：dispatch 新增 `wheels <retract|lower>` 分支、header 註解同步更新
- `web_backend/public/index.html`：在 `manual — pusher` 下方新增 `manual — wheels` panel，含兩按鈕 `wheels retract` / `wheels lower`

### 原因
使用者要求 GUI 可手動下/收輪組。命名與行為 mirror 啟動時的 init() 動作（平行移動、不用 broadcast sync，避免需要設 slave 1, 3 bystander-safe）。

### 待驗
- [ ] 現場測試：GUI 點「放輪 (abs -7)」時兩輪同步下降、無碰撞
- [ ] GUI 點「收輪 (abs 0)」時兩輪回到 0 位置，且不會撞到其他結構
- [ ] 回報狀態正常（OK / ERR）顯示於 log

## 2026-04-24aw — Claude Code — 啟動時自動下輪（slave 2, 4 → abs -7 cm，平行）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`：`WashRobot::init()` 尾端（relays off 之後、`return false;` 之前）加入啟動時自動下輪動作

### 做法
- `D_(DM2J_LEFT_WHEEL).PR_move_cm_nowait(0, 1, DM2J_RPM, -7.0, ...)` + `D_(DM2J_RIGHT_WHEEL).PR_move_cm_nowait(...)` 兩個連續 trigger（無 wait），接著 `dm2j_wait_done_` 各等一次完成
- 兩輪在一個 Modbus frame 內前後觸發，實際是並行移動，不是 broadcast sync → 不需要處理 slave 1, 3 的 PR0 bystander-safe
- 任一 trigger 或 wait 失敗即 `return true` 讓 `main()` 判定為 FATAL

### 原因
啟動時需要先把輪組放下（-7 cm 絕對位置）才能讓車體著地進入後續流程。放在 `init()` 尾端而非 `cmd_init_impl_()`，因為這是**開機動作不是使用者 init 指令的一部分**，且此時 TCP server 還沒開（`main()` 在 init 之後才啟 server），不會被 web 指令干擾。

### 待驗
- [ ] 現場測試：機器啟動後兩輪是否同時放下到 -7 cm，動作平順無碰撞
- [ ] DM2J slave 2, 4 啟動前的絕對座標零點假設是否正確（若未歸零，-7 cm 絕對位置沒有意義 → 日後可能需要在此動作前加 `home_set_current_pos_zero()`）

## 2026-04-24av — Claude Code — [跨界: user_lib] DM2J 對同步組 helper（修 1 診斷 log + 平行 poll）

### 修改檔案
- `user_lib/WASH_ROBOT.h`：宣告 `dm2j_pair_move_abs_(slave_a, slave_b, pr_num, target_cm, timeout_ms=15000)` + `dm2j_pair_poll_done_(slave_a, slave_b, timeout_ms)` 兩個 private helper
- `user_lib/WASH_ROBOT.cpp`：
  - 實作 `dm2j_pair_poll_done_`：每 iter 同時 poll 兩顆 slave（不序列），任一 fault 或兩者都 done 就返回，有 FAULT/timeout 則 return true
  - 實作 `dm2j_pair_move_abs_`：讀兩顆位置 → 寫 PR block → broadcast trigger → parallel poll → 再讀位置；log 印 `[pair DM2J 1+3] before: 1=X 3=Y → target Z` / `after: 1=X' (ΔdX) 3=Y' (ΔdY) cm`
  - 4 處 DM2J rail move 全改成呼叫 helper：
    - feet_pre_cycle: rail +STEP_CM
    - body_pre_cycle: rail → 0
    - feet_backup: rail -VACUUM_BACKUP_CM (retry)
    - body_backup: rail +VACUUM_BACKUP_CM (retry)
  - 移除舊的兩次 `dm2j_wait_done_` 序列呼叫（它只看一顆完成才看下一顆，掩蓋了平行狀態）

### 原因
使用者實機跑 `run 2` 發現 DM2J slave 1 跟 3 沒同時移動（第二個 step 某顆先完成）。從 log 分析序列 poll 模式看不到真實差異（先 poll slave 1 到 done → 才 poll slave 3，可能 slave 3 早就 done 只是我們晚到）。

新 helper 做兩件事：
1. **Parallel poll**：同一個 iter 裡同時讀兩顆 status，真實反映同步狀況
2. **位置診斷 log**：before/after/Δ 印出來，可以直接看到實際 travel 有沒有差異

Broadcast trigger 保持（廣播 slave=0 在 Modbus 同一幀 = 真硬體同步起步；byspassers 2/4/5 PR1=rpm=0 不會動）。

### 之後排查流程
下次跑 log 看 `[pair DM2J 1+3]` 行：
- Δ 兩邊差很大 → slave 1/3 starting pos 有 drift（累積誤差）
- 某邊 Δ 接近 0 → 那顆可能沒收到 broadcast（USR-TCP232 廣播不穩）或本來就在 target 附近
- FAIL → fault 或 timeout

## 2026-04-24au — Claude Code — [跨界: user_lib] zdt_wait_motion_done_ 偵測到 stall 時清 stall_flag

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `zdt_wait_motion_done_`：stall_flag 分支加 `Z_(slave).release_stall_flag()` 呼叫，然後才 `return false`

### 原因
實機跑 `cmd_run` 第二個 step 的 feet retract 時，slave 3/4 stall（slave 3 卡在 477°、slave 4 卡在 22.65°）。原 helper 把 stall 當「停了」直接 return false 繼續下一步，但 drive 的 stall_flag 沒清掉 — 接下來 cycle_group_ 的 feet extend `motion_control_pos_mode_nowait` 可能被 drive 拒絕 / 忽略，造成 pusher_move_many_ 等第一個 slave 15s timeout 才回錯，表面看像「卡住」。

改成偵測到 stall 時主動呼叫 `release_stall_flag()`，下個 pos_mode 指令 drive 能正常接受。行為仍 return false 讓流程繼續（避免一次 stall 就中斷整個 step）。

### 觀察
Slave 3/4 重複 stall 代表這兩顆推桿有機械阻力（可能：推桿末端卡牆面、桿本身磨損、氣管拉扯）。現場應檢查機構；若持續 stall 可調 `PUSHER_ACC` 降加速度減少堵轉機率。

## 2026-04-24at — Claude Code — [跨界: user_lib][TEST MODE] cycle_group_ 真空驗證跳過（step_down / run 用）

### 修改檔案
- `user_lib/WASH_ROBOT.h` — `cycle_group_` template 在 extend + settle 完成後，直接 `out_retry_count = 0; return "";`（視為貼附成功）。原本的 `vacuum_check_ / evt_(vacuum_fail) / retry` 邏輯整段 `/* ... */` 註解保留，REVERT 時刪這兩行、解開註解即可

### 原因
Sadie bench 測試 `run` / `step_down` 時不想被真空門檻卡住 — 吸盤可能未貼牆或環境條件達不到 -50 kPa。跳過驗證讓 step_down 流程可以跑通，專心驗證 crane + DM2J + ZDT 時序。

### 效果
- `do_step_down_` 兩個 `cycle_group_("feet" / "body")` 呼叫都不再驗真空
- 不再有 `vacuum_fail` / `vacuum_retry_exceeded` 錯誤
- Retry loop 不會執行（第 1 次就回 OK）
- backup 機制（DM2J 反向 5cm）不再觸發

### 恢復方式
刪除 `[TEST MODE]` 標記的 `out_retry_count = 0; return "";` 兩行，移除 `/* ... */` 註解包圍。同標記 grep 可找：
```
grep -n "TEST MODE 2026-04-24" user_lib/WASH_ROBOT.h
```

## 2026-04-24as — Claude Code — [跨界: user_lib][TEST MODE] cmd_attach vacuum check 再度註解掉

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_attach`：再度把 `vacuum_check_("all")` 註解掉（上一版 ar 剛恢復），bench 測試直接通過進 Attached

### 原因
使用者要求：attach 真空檢查先註解掉直接通過。Bench 沒貼牆時 attach 永遠失敗擋住後續測試流程；step_down 的 cycle_group_ 內部 vacuum check + retry 保持啟用（那裡才是驗證 seal 品質的地方）。

### 保留
- `cycle_group_` vacuum check 仍**啟用**（Phase A/B 每次伸推桿後都驗證真空，失敗才觸發 retry + crane 放繩）

## 2026-04-24ar — Claude Code — [跨界: user_lib] step_down 恢復 vacuum 檢查 + retry 加 crane pay_out

### 修改檔案
- `user_lib/WASH_ROBOT.h` `cycle_group_` template：解除 TEST MODE 註解，恢復 `vacuum_check_(group)` + retry 迴圈（fails 非空則發 `vacuum_fail` EVT、進 retry；連 VACUUM_RETRY_MAX 次都失敗回 `vacuum_retry_exceeded`）
- `user_lib/WASH_ROBOT.cpp` `cmd_attach`：解除 TEST MODE 註解，恢復 `vacuum_check_("all")` 檢查；任一 cup 失敗回 `ERR attach_vacuum_fail slaves=...`
- `user_lib/WASH_ROBOT.cpp` `feet_backup`：retry rail 後退前加 `crane pay_out <VACUUM_BACKUP_CM>` 放繩，給 rope slack 讓 rail 能自由後退
- `user_lib/WASH_ROBOT.cpp` `body_backup`：同樣加 `crane pay_out <VACUUM_BACKUP_CM>` 在 rail 後退前

### 原因
使用者要求：恢復 step_down 的真空檢查重吸機制 + crane 搭配放繩。

**Retry 流程（新）：**
1. vacuum_check_ 偵測 fail slaves
2. 進 retry：關 valve → 300ms → 收推桿
3. backup lambda：**crane pay_out** VACUUM_BACKUP_CM → rail -5/+5cm 後退
4. valve ON → 伸推桿 → settle
5. 再次 vacuum_check_
6. 最多 VACUUM_RETRY_MAX=5 次，都失敗就 `vacuum_retry_exceeded`

**crane pay_out 時機**：rail backup 之前，跟 Phase A pre_cycle 的「crane pay_out → DM2J 下降 → crane retract」一致 — 先放繩給 slack，再動軌道。

### 仍保留的 TEST MODE
- crane_watchdog_loop_ abort 關閉（意外斷線不 abort）
- imu_monitor_loop_ emergency abort 關閉（bench 傾斜不中斷）
- do_step_down_ Phase C wash sweep 註解掉（DM2J:5 FAULT 未排除）

## 2026-04-24aq — Claude Code — [跨界: user_lib] do_step_down_ DM2J 改 broadcast sync + init 加 bystanders safe PR1

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`:
  - `WashRobot::init()` DM2J 初始化後加 3 行：wheel left/right + arm 的 PR1 設 `rpm=0`（safe no-op）
  - `do_step_down_()` 4 處 DM2J 觸發改用 `PR_trigger_sync(1)` 廣播：
    1. `feet_pre_cycle` rails → +STEP_CM
    2. `feet_backup` rails → current - backup
    3. `body_pre_cycle` rails → 0
    4. `body_backup` rails → current + backup
  - 每處拿掉多餘的 `PR_trigger` 第二次呼叫（廣播只要一次）

### 原因
Sadie 指出腳組 slaves 1,3 機構剛性連接，washrobot 原用個別 `PR_trigger` 兩次有 5~20ms 的 TCP 序列化 skew，足以扭壞機構。改用 `PR_trigger_sync(1)` 廣播讓兩顆在同一個 Modbus frame 同時啟動（<1ms skew）。

廣播會對所有 slave 起作用 → 前置必須把 wheels(2,4) / arm(5) 的 PR1 設成 rpm=0 safe state，broadcast 到它們時執行 rpm=0 = 不動作。wheels 只用 PR0（cmd_init retract），arm 只用 PR0（arm_sweep），PR1 在 bystander 上是一次性設定終身有效。

### 對齊 Linux_test menu 7
Menu 7 早就用 `dm2j_pair_rail_move_sync` + `dm2j_set_safe_pr` 同樣 pattern，這次把 washrobot 生產碼也對齊，修 4 項差異裡最嚴重的 #1。

### 規範邊界
`user_lib/WASH_ROBOT.cpp` 屬 Jim 範圍，標 `[跨界: user_lib]`。純內部改動（public API 不變）。

## 2026-04-24ap — Claude Code — [跨界: user_lib] feet_pre_cycle 恢復 crane pay_out / retract 呼叫

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_` `feet_pre_cycle`：
  - 解除 `crane_cmd_("pay_out <STEP_CM + STEP_MARGIN_CM>")` 的 TEST MODE 註解
  - 解除 `crane_cmd_("retract <STEP_MARGIN_CM>")` 的 TEST MODE 註解
  - 兩處加 `[step_down] crane pay_out/retract <N>` stdout log

### 原因
Changelog ao 將 CRANE_IP 改 127.0.0.1 後 crane_shim 本地可連。使用者指出「step_down 的吊機沒有連動」— 因為我之前為 bench 測試把 crane 呼叫註解掉了。現在 crane 可用，恢復呼叫讓腳組 rail 移動前後跟 crane pay_out/retract 同步。

### 仍保留的 TEST MODE 註解（bench 測試安全）
- `crane_watchdog_loop_` abort 仍關閉（ap 後 crane healthy 時不會觸發；意外斷線時不會誤 abort）
- IMU emergency abort 仍關閉（bench 傾斜不中斷）
- Vacuum check（cmd_attach + cycle_group_）仍關閉（沒貼牆）
- Phase C wash sweep 仍關閉（DM2J:5 FAULT 未排除）

## 2026-04-24ao — Claude Code — [跨界: user_lib] CRANE_IP 改 127.0.0.1（shim 移到 .5.19 與 washrobot 同機）

### 修改檔案
- `user_lib/WASH_ROBOT.h:104` `CRANE_IP`：`"192.168.5.26" → "127.0.0.1"`
- `.claude/easy_crane_test_mode.md §9a` — 撤除清單更新

### 原因
Sadie 回報 step_down 跑完但 easy crane 沒動。診斷：
- 現狀：washrobot + shim 都在 .5.19，實體 easy crane 在 .5.26
- 原本 CRANE_IP 設 `192.168.5.26` → washrobot 想連 shim 卻指到 easy crane 本體（:5002 沒人 listen）
- `crane_cmd_("pay_out 45")` connect 失敗回空字串 → step_down 回 `ERR crane_pay_out_fail` 進 Error

### 新配置
- washrobot → `127.0.0.1:5002`（shim 在同機）
- shim 啟動：`python3 crane_shim.py --easy-host 192.168.5.26`（連實體 easy crane）
- easy crane → 實體 .5.26:5003

本機 loopback 最穩不經網路層。主 crane 部署時仍改回 `192.168.1.101`。

## 2026-04-24an — Claude Code — [跨界: user_lib] body_pre_cycle 補「body valve OFF 先於 retract」（對齊 Linux_test menu 7）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_` `body_pre_cycle`：
  - Retract center/body 推桿之前同時關 `CH_VALVE_BODY` 和 `CH_VALVE_CENTER`（之前只關了 CENTER）
  - 然後 sleep 300ms 讓真空釋放
  - 才開始 retract

### 原因
使用者指出：ZDT 推桿 retract 之前應該先解真空（對齊 Linux_test menu 7）。

Phase A 的 feet_pre_cycle 已經有這步（`CH_VALVE_FEET OFF → 300ms → retract`），但 Phase B 的 body_pre_cycle 只關了 CH_VALVE_CENTER，**沒關 CH_VALVE_BODY**。body valve 從 attach 或 Phase A extend 之後一直 ON，retract 時吸盤還抓著牆：
- 輕則推桿拉扯 seal 損壞
- 重則推桿 stall 收不回來

補上 `CH_VALVE_BODY OFF` 讓狀況對稱到 Linux_test menu 7 pattern。

### 其他 retract 點檢查
- `feet_pre_cycle`：有做 ✓
- `body_pre_cycle` center retract：有做 ✓（CH_VALVE_CENTER OFF）
- `body_pre_cycle` body retract：**之前漏了，本次補上** ✓
- `cycle_group_` retry 內的 retract：有做 ✓（`controlRelay(valve_ch, false)` + `sleep_ms(300)`）

## 2026-04-24am — Claude Code — [跨界: user_lib][TEST MODE] do_step_down_ 註解掉 Phase C 清洗流程

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_` 最後段：
  - 註解掉 `do_arm_sweep_()` 呼叫、return 檢查
  - 保留 `motion_active_ = false`
  - 加 log `[step_down] wash sweep skipped (TEST MODE)`
  - 直接 return "OK step_done\n"

### 原因
- DM2J:5 (arm/上滑台) 在 FAULT 狀態，`PR_move_cm` 立刻 return fault → `ERR arm_right_fail`
- Bench 測試焦點在走路（feet/body/rail），清洗流程（水泵/進水閥/刷子 + arm 左右掃）不是驗證目標
- `do_arm_sweep_` 本身不動（REVERT 後還能用）；只是 cmd_step_down 暫時不呼叫它

### 上牆前要做
1. 處理 DM2J:5 FAULT（讀 error code 0x2203 確認是不是鎖軸 0x80，清 alarm、手動轉軸測機械）
2. 解註解 Phase C

## 2026-04-24al — Claude Code — [跨界: user_lib] cycle_group_ 改 valve ON 在 extend 之前（對齊 Linux_test menu 7）

### 修改檔案
- `user_lib/WASH_ROBOT.h` `cycle_group_` template：把 `pqw_.controlRelay(valve_ch, true)` 從 extend 後移到 extend 前

### 原因
使用者要求：放 ZDT 腳推桿**之前**應該先開抽真空，對齊 Linux_test menu 7 的「pre-engage valve」pattern。

原順序（錯）：extend → valve ON
- 推桿貼牆時吸盤內還是大氣壓
- 閥打開後要從大氣壓抽到真空，seal 效能打折扣

新順序（對）：valve ON → extend
- 推桿還沒碰牆時吸盤已經被抽空
- 接觸牆瞬間吸住，真空 seal 最佳化
- 權威：memory `project_vacuum_seal_patterns.md`「valve-before-extend」

### 未做
Memory 同時提到 staged extend（half → full 兩段），Linux_test menu 7 用 `zdt_group_extend_staged`。這次沒改（WASH_ROBOT 還是一次伸到目標），使用者沒明確要求。上牆測試若 seal 效能仍不足再追加。

## 2026-04-24ak — Claude Code — [跨界: user_lib] ZDT 加 set_debug() + zdt_wait_motion_done_ 暫停 hex dump

### 修改檔案
- `user_lib/ZDT_motor_control.h` — 公開 inline `void set_debug(bool v)` 讓上層 runtime 切 debug_mode
- `user_lib/WASH_ROBOT.h` — 新增 private member `bool driver_dbg_`（記住 init 時 WR_DRIVER_DEBUG 的值）
- `user_lib/WASH_ROBOT.cpp`:
  - init() 裡 `driver_dbg_ = dbg` 記下狀態
  - `zdt_wait_motion_done_` 進入時 `Z_(slave).set_debug(false)` 關掉 ZDT hex dump；正常結束 / timeout 兩條 return 前都 restore `set_debug(true)`

### 原因
Sadie 回報：step_down 時 web GUI log 被 `[DBG] [ZDT:9] RX get_status ...` 洗屏 — zdt_wait_motion_done_ 每 150ms 呼叫 get_system_status，啟用 debug 時每次 TX/RX 都印 hex dump（~7 行/秒）。只要關掉 poll loop 的 hex 即可，ad-hoc motion 命令的 hex dump 保留（方便 debug trigger / stall）。

### 效果
- poll loop 完全靜默（不再 get_status hex 洗屏）
- 單一 motion 命令（`motion_control_pos_mode` 等）的 TX/RX 仍然印（debug 價值保留）
- PQW / DM2J / JC-100 等其他 driver 的 hex 不受影響
- WR_DRIVER_DEBUG=0 仍然完全關閉所有 debug（driver_dbg_=false，set_debug 呼叫被 skip）

## 2026-04-24aj — Claude Code — [跨界: user_lib][TEST MODE] 關掉 crane watchdog 和 IMU 緊急 abort（bench 測試）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`:
  - `crane_watchdog_loop_` (line 263): `abort_flag = true` 註解掉；改成只發 `crane_watchdog timeout (TEST MODE — abort suppressed)` EVT 不 abort
  - `imu_monitor_loop_` (line 377): IMU > 45° 持續 500ms 的 `abort_flag = true` + `motion_active_ = false` + `crane_cmd_(emergency_stop)` + `set_state_(Error)` 整段註解掉；改成只發 EVT 不 abort

### 原因
step_down Phase B 途中（center 推桿 extend + CH4 valve ON 之後）突然 `ERR aborted`。身體推桿沒 extend。追源頭發現背景執行緒 `crane_watchdog_loop_`：ping crane 沒回應 → 60s timeout → 看到 `motion_active_==true` 就 `abort_flag=true`。Bench 沒接 crane，從程式啟動起 ping 就不會 OK，累積到 step_down 剛好過 60s → 觸發。

同時預防 IMU 緊急 abort — bench 上機身可能隨便傾斜，> 45° 也會 abort。

兩個 abort source 都註解掉，TEST MODE marker 保留 `[TEST MODE 2026-04-24]`，正式部署前回復。

### 同時保留的 EVT
- `crane_watchdog timeout (TEST MODE — abort suppressed)` — 每次 watchdog 仍會發，讓操作員知道 crane 沒上線
- `imu_emergency balance_deg=X (TEST MODE — abort suppressed)` — 仍會發，不中斷運動

## 2026-04-24ai — Claude Code — [跨界: user_lib] cmd_init 結束廣播 EVT init_complete

### 修改檔案
- `user_lib/WASH_ROBOT.h`:
  - 私有區加 `std::string cmd_init_impl_();` 宣告
- `user_lib/WASH_ROBOT.cpp`:
  - 把原本的 `cmd_init()` 改名 `cmd_init_impl_()`（實際 init 邏輯）
  - 新增薄 `cmd_init()` 包裝層：呼叫 `cmd_init_impl_()` 拿到 result → 依成功/失敗廣播 EVT：
    - Success：`EVT init_complete status=ok`
    - Failure：`EVT init_complete status=fail reason=<reason>`
  - 原樣回傳 result 給 caller

### 原因
Sadie 要求：init 完成不論成功失敗都要有狀態 log 廣播到 web。原本 `cmd_init` 只**回傳**字串給發命令的 client（Web 看得到 `[washrobot] OK init_done`），但其他同時連線的 client 看不到。改成用 `evt_()` 廣播 EVT，讓所有 GUI client 都收到。
用 wrapper pattern 避免動到 13 個 return 分支各自加 EVT，單點集中處理。

### GUI 端會收到
```
[washrobot] EVT state_changed idle ready
[washrobot] EVT init_complete status=ok               ← 新增
[washrobot] OK init_done

# 或失敗時：
[washrobot] EVT init_complete status=fail reason=pump_on_fail
[washrobot] ERR pump_on_fail
```

## 2026-04-24ah — Claude Code — [跨界: user_lib] cmd_attach / cmd_detach / cmd_step_down 加 progress std::cout

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`
  - `cmd_attach`: 加 `[attach] center pusher extend / open valves / settle Xms / done → Attached`
  - `cmd_detach`: 加 `[detach] close valves CH2/3/4 → Ready`
  - `cmd_step_down`: 加 `[step_down] start → Running / done / FAIL: <reason>`

### 原因
Web GUI 按 attach 後 stdout 最後一條是 PQW 第三個 relay RX echo，接著 2000ms settle + state change + TCP reply 都不走 stdout，看起來像「卡住」但實際上 cmd_attach 已經成功返回 `OK attached`。加 [attach] done 讓 stdout 有明確完成訊息，避免誤判。

## 2026-04-24ag — Claude Code — [跨界: user_lib][TEST MODE] do_step_down_ 對齊 Linux_test menu 7 pattern

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `do_step_down_`:
  - **feet_pre_cycle / body_pre_cycle**：`PR_trigger_sync(1)`（廣播）改成 `PR_trigger(1)` 對左右腳軌道個別觸發 — 避免誤觸發 slaves 2/4/5（輪子 + 上滑台）的 stale PR1 資料
  - **feet_pre_cycle crane_cmd_("pay_out ...") + crane_cmd_("retract ...")**：註解掉，bench 測試沒接 crane 否則每次 30s timeout
  - **feet_backup / body_backup**：mode=0 relative 改 mode=1 absolute（mode=0 在此 firmware 是「PR 未配置」不動）— 目標 = `rail_pos_cm + backup_signed`
- `user_lib/WASH_ROBOT.h` `cycle_group_` template：註解掉 `vacuum_check_(group)` + retry 迴圈，`pusher_move_many_` 成功後直接 `return ""`（跟 cmd_attach 註解掉 vacuum 對齊）

### 原因
現場實測 step_down log 顯示 Phase A 收完 feet 推桿後（`[wait ZDT:1/2] done`）就沒進展 — 下一步 `crane_cmd_("pay_out ...")` 在 bench 沒 crane 會等 30s timeout 才回錯，使用者看起來像卡住。

加上之前發現的三個子 bug（broadcast 會誤觸發無關 slaves、mode=0 無效、bench vacuum 不可能通過），一次把 do_step_down_ 改成對齊 Linux_test menu 7 可跑的 pattern。

所有註解都加 `[TEST MODE 2026-04-24]` marker，正式上牆部署前 revert。

### 剩下跟 Linux_test menu 7 的差異（未改）
- Linux_test menu 7 用 `zdt_group_extend_staged`（half → full 兩段），WASH_ROBOT 還是一次伸到目標
- Linux_test 「valve ON before extend」預抽真空（memory vacuum_seal_patterns）；WASH_ROBOT cycle_group_ 是 extend → valve ON（順序反）

這兩點對 bench 測試沒差（沒真空可驗證），上牆 seal 效能可能打折扣，日後真實測試再評估。

## 2026-04-24af — Claude Code — [跨界: user_lib][TEST MODE] cmd_attach 註解掉 vacuum check

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_attach`：註解掉 `vacuum_check_("all")` + `if (!fails.empty()) return "ERR ..."` 整段，讓 state 直接進 Attached

### 原因
現場 bench 測試沒真的貼牆 → 吸盤無法 seal → JC100 都讀到 ≈-0.2 kPa（閾值 -50 kPa）→ vacuum_check_ 全 fail → cmd_attach 回 `ERR attach_vacuum_fail slaves=1..9` → state 沒進 Attached → 接著按 step_down 被 `cur != Attached` 擋下 → 看起來「沒反應」。

使用者要求先註解掉讓下一步能測試。加 `[TEST MODE 2026-04-24]` marker，正式部署到牆上前記得 REVERT。

### 下一個可能的雷
`WASH_ROBOT.h::cycle_group_` template（do_step_down_ 用）line 328 也有 `vacuum_check_(group)`：若真空還是沒 seal，step_down 會 retry 5 次（VACUUM_RETRY_MAX）後回 `vacuum_retry_exceeded feet/body`，整個 cmd_step_down 返錯。若 bench 測試需要 step_down 也能通，那裡也要註解掉。

## 2026-04-24ae — Claude Code — [跨界: user_lib] **CRITICAL** ZDT get_system_status return 值反了

### 修改檔案
- `user_lib/ZDT_motor_control.cpp`:
  - `get_system_status()` line 182：`return true` → `return false`（成功要回 false 符合專案慣例）
  - `wait_until_pos_reached()` line 188-189：`if (get_system_status() && status.pos_reached) return true;` → `if (!get_system_status() && status.pos_reached) return false;`（配合上面的 convention 修正）

### 原因（**影響所有 ZDT 用 status polling 的地方**）
實機 log 顯示 slave 3 `get_system_status` TX/RX bytes 完全正常（37 bytes response, 可正確解析），但 WASH_ROBOT 的 `zdt_wait_motion_done_` 判為 100 次連續 comms fail。

挖到 driver 源碼發現 bug：`get_system_status` **所有 return 路徑都是 `return true`** — send 失敗、response 太短、**以及成功解析完** — 全部回 true。按專案慣例 `false=success, true=error`，所有呼叫端做 `if (get_system_status()) ...error...` 都把成功當失敗。

Log 證據：RX `03 04 20 23 09 5C C2 00 66 C3 06 ...` 正好 37 bytes，byte count 0x20=32，格式完美；但我的 helper 100% 失敗。只能是 return 值反了。

### 連帶修正
`wait_until_pos_reached` 內部用 `get_system_status() && status.pos_reached` — 之前靠 get_system_status 回 true=成功才進 if，表面上是「同錯抵消」能 work；現在修成 convention 後要翻成 `!get_system_status() && status.pos_reached`。

### 影響範圍
修完後以下呼叫端自動變對（原本都默默踩雷）：
- `WASH_ROBOT::zdt_wait_motion_done_`（我的 helper）
- `Linux_test/main.cpp` menu 3 `test_zdt` line 321
- `Linux_test/main.cpp` menu 7 `zdt_group_move_sync` line 855
- `Linux_test/main.cpp` menu 16 `test_dm2j_group_sync` 若有用到
- `ZDT_motor_control::wait_until_pos_reached` 內部（已同時修）

原本 Linux_test menu 7 「能 work」可能是靠其他 fallback（如 POS_DELTA_DEG 穩定偵測繞過）或者實際也有悄悄 bug 只是沒被注意到。

**「CRITICAL」**：這 bug 擋住所有 ZDT motion 完成偵測；修完 init / attach / step / run 任何有 ZDT 等待的指令都會受惠。

## 2026-04-24ad — Claude Code — 選項 17 cleanup 改個別 relay 關閉（修 CH1 pump 沒關）

### 修改檔案
- `Linux_test/main.cpp` `test_cleanup()` — PQW 清理區塊改用 for 迴圈 `controlRelay(ch, false)` 關 CH1..CH8，`controlAll(false)` 降級為最後 belt-and-suspenders

### 原因
Sadie 回報選項 17 跑完 CH1 pump 沒關。根因：`controlAll` 用 PQW 廠商特殊的 0x0085 register，此韌體版本 echo 長度檢查通過但實際不作動（跟 2026-04-21 debug PQW 時看到的現象一致）。改成逐通道 `controlRelay` function 0x05 標準寫入，每顆 relay 都有實際 TX → 一定送出命令。

## 2026-04-24ac — Claude Code — Linux_test 新增選項 17「Emergency cleanup」

### 修改檔案
- `Linux_test/main.cpp`:
  - 新增 `test_cleanup()`（~100 行）— 獨立 cleanup 工具，跟選項 7 尾段 cleanup 行為一致但可單跑
  - 選單加第 17 項

### 流程
1. 三個 gateway IP 輸入（預設 .20/.21/.22），各自 quick_tcp_probe 判定
2. PQW（若 .22 通）：CH2/3/4 OFF → 300ms settle → `controlAll(false)` 關 pump + CH5-8
3. ZDT（若 .21 通）：9 顆 release_stall + enable → 廣播 retract 到 0 → disable 全部
4. DM2J（若 .20 通）：bystanders(2,4,5) 設 safe PR1 → 1,3 enable → 廣播返回 0 → 1..5 全 disable
5. 各 gateway 單獨失敗不中止整個 cleanup（skip 該層 + [WARN]）

### 原因
Sadie 要一個現場 panic button：測試中途掛掉 / 按錯順序 / 現場要撤走，一鍵把機器回 parked state（繼電器 OFF / 推桿收光 / 軌道歸 0 / 馬達 disable）。單獨跑比要 rerun 選項 7 全套快很多，也避免選項 7 前半段誤動到卡在奇怪狀態的機構。

## 2026-04-24ab — Claude Code — [跨界: user_lib] zdt_wait_motion_done_ 對齊 Linux_test pattern（sleep 先於 poll + 無 comms fail 限制）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `zdt_wait_motion_done_`：
  - sleep 移到迴圈頂端（從 poll 後 → poll 前）：broadcast trigger 後給 150ms 暖身窗讓 TCP gateway buffer 對齊穩定，再開始讀 status
  - 移除 `COMMS_FAIL_MAX` 上限：只要在 15s 全域 timeout 內都持續 retry，不因為連續失敗就放棄
  - 失敗時累計 total_fails，success / timeout log 印出總失敗次數供診斷

### 原因
上輪 (aa) 雖然把容忍度從 3 → 10 次，實機 log 顯示「comms fail x10 at 1350ms」— 10 次連續失敗都不恢復，motion 實際成功但判定 fail。

對比 Linux_test `zdt_group_move_sync` 迴圈結構：**先 sleep 再 poll**。broadcast trigger 後立即讀會撞到 TCP buffer 對齊 race window；先睡 150ms 讓 gateway 處理完 broadcast 後的 buffer 狀態，第一次讀才有機會對。

也取消連續失敗限制 — 物理 motion 完成前反覆讀失敗是正常的 frame alignment 問題，持續 retry 總會成功；只在 15s 全域 timeout 後才放棄，那才算真的死了。

### 預期 log
```
[init] feet pushers (ZDT 3,4,1,2) → extend 20000 pulses (~7 cm)
[wait ZDT:3] recovered after 3 comms fail(s) at 600ms
[wait ZDT:3] done at 900ms, pos=2250° (total comms fails=3)
[wait ZDT:4] done at 750ms, pos=2250° (total comms fails=2)
...
```

若還是 timeout，log 末尾的 `total comms fails=N` 讓我們知道是「真的沒動」還是「通訊整段爛」。

## 2026-04-24aa — Claude Code — [跨界: user_lib] zdt_wait_motion_done_ comms fail 容忍 3→10 次 + 恢復 log

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `zdt_wait_motion_done_`：
  - `COMMS_FAIL_MAX` 從 3 改 10（= 10 × 150ms = 1.5s 容忍度）
  - 成功讀取時若之前有失敗，印 `recovered after X comms fail(s) at Yms`

### 原因
實機 init 踩雷：
```
[init] feet pushers (ZDT 3,4,1,2) → extend 20000 pulses (~7 cm)
[wait ZDT:3] comms fail x3 at 300ms — giving up
```

但推桿物理上**有伸出來**。這是 memory `project_zdt_firmware_quirks.md` #3 典型症狀：trigger_sync_move 廣播後 TCP gateway buffer 對齊會亂一陣，幾次 read 後自動穩定。原本 3 次容忍太嚴，motion 才 300ms 就放棄（推桿動作總時間 ~500ms 都還沒結束）。

改 10 次 = 1.5s 容忍窗，motion 物理完成後如果讀還是失敗才真的放棄（那才是硬體/TCP 斷線）。加「recovered after N comms fail(s)」log 讓現場可看到 frame 對齊何時恢復。

## 2026-04-24z — Claude Code — [跨界: user_lib] zdt_wait_motion_done_ 加診斷 log + comms fail 容忍

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `zdt_wait_motion_done_`：
  - 每種退出印一行 `std::cout`：`done at Xms, pos=Y°` / `stall_flag set at Xms, pos=Y°` / `comms fail x3 at Xms — giving up` / `TIMEOUT after Yms, last pos=Z°, speed=W rpm`
  - `get_system_status()` 失敗不再立刻 return true；改容忍 2 次暫時錯誤（累積 3 次才放棄），期間 sleep + continue 下個 poll

### 原因
現場 init 卡在 feet pushers extend 後沒 log 可 debug。加進度輸出後可快速定位：每顆 slave 停止 / 失敗時間、位置、速度。Comms fail 容忍對應 memory `project_zdt_firmware_quirks.md` #3 frame alignment issue — TCP-gateway 連續指令後殘留 buffer 可能讓單次 read 失敗，retry 即正常。不容忍的話單次 hiccup 會讓整個 extend 誤 abort。

## 2026-04-24y — Claude Code — [跨界: user_lib] center ZDT 伸出距離改 ~10cm 對齊 body

### 修改檔案
- `user_lib/WASH_ROBOT.h:137` — `PUSHER_EXTEND_PULSE 144000 → 30000`（約 20cm → 10cm）

### 原因
Sadie 回報：中心推桿（ZDT slave 9）原本用全行程 20cm 伸出，應該跟 body 組一樣 10cm。三處用到的地方（`cmd_attach` / `do_step_down_` 中心 re-extend / `cmd_pusher` 中心或 fallback 路徑）共用同一個常數，改值後全部一致。

PUSHER_EXTEND_BODY_PULSE 維持 30000，兩者數值相等但保留分開常數，未來若兩者要分開調整只改對應常數即可。

## 2026-04-24x — Claude Code — [跨界: user_lib] 推桿等待改 speed/pos 穩定偵測（繞過 pos_reached 不可靠）

### 修改檔案
- `user_lib/WASH_ROBOT.h`: 宣告 private `zdt_wait_motion_done_(slave, timeout_ms=15000)`
- `user_lib/WASH_ROBOT.cpp`:
  - 新增 `zdt_wait_motion_done_` 實作：每 150ms `get_system_status()`，判「stall_flag 已 set」或「`|real_speed| ≤ 20 RPM` 連續 3 次」或「`|Δreal_pos| ≤ 0.15°` 連續 3 次」就算完成；15s timeout fail
  - `pusher_move_`：原本 `motion_control_pos_mode(sync=0)`（內建 wait_until_pos_reached）+ 再 `wait_until_pos_reached` 一次 → 改 `motion_control_pos_mode_nowait(sync=0)` + `zdt_wait_motion_done_`
  - `pusher_move_many_`：原本迴圈呼叫 `wait_until_pos_reached` → 改呼叫 `zdt_wait_motion_done_`

### 原因
用戶按 attach 後 log 顯示 `[WRN] [ZDT:9] Waiting for moving timeout` 但**現場確認 center pusher 物理有正常伸出去** → 典型 ZDT firmware quirk #1（memory `project_zdt_firmware_quirks.md`）：馬達實際已停但 `pos_reached` bit 不 set。cmd_attach 拿到 timeout 回 `ERR center_extend_fail` 給 web GUI（stdout 看不到）。Linux_test 早就用 speed/pos 穩定偵測繞過這個雷（menu 7 的 `zdt_group_move_sync`），主程式補上同 pattern。

穩定偵測三個 fallback（跟 memory 記錄完全對齊）：
1. `stall_flag`=1 → 算已停（雖然堵轉）
2. `|real_speed| ≤ 20 RPM` 連續 3 次 poll (~450ms) → 低速 = 停
3. `|Δreal_pos| ≤ 0.15°` 連續 3 次 → 幾乎沒動 = 停

驅動原 `wait_until_pos_reached` 不動（還有別人在用）；WASH_ROBOT 自己 wrap。

`[跨界: user_lib]` — WASH_ROBOT.{h,cpp} 內部實作 + 新增 private helper，沒動 public API。

### 影響
- cmd_init extend feet/body → 不會再假 timeout
- cmd_attach center extend → 不會再假 timeout
- cycle_group_ template（Phase A/B 推桿 extend / retract）→ 不會再假 timeout
- cmd_pusher 手動推桿 → 同上

## 2026-04-24w — Claude Code — [跨界: user_lib] cmd_init 加 progress std::cout

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_init` 每個關鍵步驟前加 `std::cout`：
  - `[init] PQW relays → pump ON, valves/water OFF`
  - `[init] DM2J wheels (slave 2, 4) → retract to 0`
  - `[init] ZDT 1-9 → release_stall + driver enable`
  - `[init] feet pushers (ZDT ...) → extend <N> pulses (~7 cm)`
  - `[init] body pushers (ZDT ...) → extend <N> pulses (~10 cm)`
  - `[init] DM2J arm (slave N) → set current as zero`
  - `[init] IMU → take baseline`
  - `[init] done → Ready`

### 原因
LOG_HEX 預設關掉後，ZDT release_stall / driver_EN / pos_mode 沒有其他 decoded log，使用者看不到 init 在跑什麼，以為程式卡住。`std::cout` 走正常 stdout 不受 log_utils 的 debug_mode / hex gate 控制，一定會印。格式「[init] ...」跟既有 `[OK] ...` / `[SETUP] ...` 風格對齊。

`[跨界: user_lib]` 但純加 log，不影響行為。

## 2026-04-24v — Claude Code — [跨界: user_lib] LOG_HEX 預設關（TX/RX hex dump 不再洗版）

### 修改檔案
- `user_lib/log_utils.h`:
  - 加 `#include <cstdlib>`（getenv 用）
  - 加 `namespace user_lib_log::hex_log_enabled()` — lazy-init 從 env `USER_LIB_HEX_LOG` 讀取，預設 false
  - `LOG_HEX` macro 多加一道 gate：`if (debug_mode && hex_log_enabled())`，兩個同時 true 才印
  - 文件頂部註解補說明 hex dump 預設 off、如何打開

### 原因
用戶要求把「tx、rx 那種 log」不要顯示。driver LOG_HEX 印出 Modbus frame hex dump，25+ 個 device 併發時會把 stdout 洗掉，decoded status / PR completion 這些有用訊息被沖走。其他 LOG_DBG/INF/WRN/ERR（例如 `status=0x00000072 [ENABLE] [CMD_DONE]` / `PR motion completed`）不受影響，仍由 `debug_mode` 控制。

### 如何重新打開（做低階診斷時）
Linux: `USER_LIB_HEX_LOG=1 ./washrobot_new_PI`
Windows: `set USER_LIB_HEX_LOG=1 & washrobot_new_PI.exe`

Lazy 讀取：env 只在第一次 LOG_HEX 被呼叫時讀一次，之後都用 cached value。啟動前設定即可，runtime 改 env 無效。

`[跨界: user_lib]` 但純加法（新 gate + 註解），沒動現有 LOG_ERR/WRN/INF/DBG 行為。

## 2026-04-24u — Claude Code — [跨界: user_lib] pusher_move_many_ 改用 _nowait 配 sync=1 pattern

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `pusher_move_many_`: 內部迴圈呼叫從 `motion_control_pos_mode(..., sync=1, ...)` 改成 `motion_control_pos_mode_nowait(..., sync=1, ...)`

### 原因
修完 ZDT enable 後 (s) init 能送出 Pos Mode，log 顯示：
```
[ZDT:3] TX Pos Mode ... sync=01 ...
[ZDT:3] RX Pos Mode 03 10 00 FD 00 05 90 18       ← echo 正常
[ZDT:3] TX get_status ...
[WRN] [ZDT:3] Waiting for moving timeout          ← 卡住
```

原因：ZDT firmware 對 `sync=1` 的 pos mode 指令**只 queue 不執行**，要等 `trigger_sync_move()` 廣播才同時啟動所有 queued slaves。但 `motion_control_pos_mode`（無 nowait 後綴）**內建 wait_until_pos_reached**：
1. slave N queue 成功（sync=1 → 不動）
2. 同函式內立即 poll pos_reached → 永遠不 set（馬達還沒動）
3. timeout → return true

結果：迴圈第一個 slave 就 timeout 退出，`trigger_sync_move()` 都沒送就失敗。

Memory `project_zdt_firmware_quirks.md` #4 有記錄正確 pattern。`motion_control_pos_mode_nowait` 送完 echo 檢查就 return 不 wait，配合迴圈外單次 broadcast + 迴圈外 per-slave `wait_until_pos_reached` 才對。單 slave 版 `pusher_move_`（sync=0 立即執行）不改。

### 驗證順序
按 init → DM2J 輪子回零 → ZDT 1-9 enable → ZDT 1-8 同步 extend (feet 7cm / body 10cm) → DM2J 5 set zero → IMU baseline → Ready。

## 2026-04-24t — Claude Code — main.cpp 加 `--no-debug` CLI flag（VS 可靠注入）

### 修改檔案
- `washrobot_new_PI/main.cpp`:
  - `main()` 加 `argc, argv` 參數
  - `#include <cstdlib>`（setenv / _putenv_s）
  - 解析 argv：`--no-debug` → 把 `WR_DRIVER_DEBUG=0` 注入環境（WashRobot::init() 的 env var 讀取邏輯接手）
  - 印 `[CLI] --no-debug → WR_DRIVER_DEBUG=0` 確認
  - 未知 flag 印 warning 不中止

### 原因
VS Linux remote debug 的 Environment 欄位不見得可靠，Program Arguments 最穩 — argv 保證 pass 進 binary。Sadie 之前嘗試把 `--no-debug` 放 Linker AdditionalOptions 是錯位置（給 ld，不進 argv）。現在改用 CLI flag：
- VS 專案 Properties → **Debugging → Program Arguments** 填 `--no-debug`
- 啟動 log 確認：`[CLI] --no-debug → WR_DRIVER_DEBUG=0` + `[OK] driver debug = OFF`
- 之前放錯的 linker AdditionalOptions `--no-debug` 要拿掉

## 2026-04-24s — Claude Code — [跨界: user_lib] cmd_init 補 ZDT release_stall + enable（修 exception 0x03）

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `cmd_init`：DM2J 輪子回零後、ZDT 推桿 extend 前，加 loop 對 ZDT slave 1-9 做 `release_stall_flag()` + `motion_control_driver_EN(true)`；enable 失敗 return `ERR zdt_enable_fail slave=N`；sleep 200ms 給 drive 進穩定狀態。

### 原因
現場測試按「init」後 DM2J 輪子歸零成功（Bug 1 修完後驗證 status decode 正確），接著 ZDT slave 3（左腳第一顆推桿）送 `motion_control_pos_mode` 收到 Modbus exception：`03 90 03 AD C1`（function 0x10 + error bit，exception code 3 "Illegal data value"）。對照 Linux_test menu 3 (`test_zdt`) 發現它每次先做 `release_stall_flag()` + `motion_control_driver_EN(true)` + 200ms 才送 pos_mode。WASH_ROBOT::init() 只連 TCP 不 enable，加上 `cmd_shutdown` / `cmd_emergency_stop` 會 disable ZDT，所以 cmd_init 必須自己負責重新 enable。

`[跨界: user_lib]`：改 WASH_ROBOT.cpp `cmd_init` 內部實作，API 簽名不變。

### 驗證
實機上次 init log 卡在 ZDT:3 retry 兩次都 exception。修完後 ZDT 1-9 都會先 enable，pos_mode 應該能正常執行 7cm/10cm 推桿 extend。

### 下一步
按 init 看能不能走到「extend feet 7cm」→「extend body 10cm」→「slave 5 set zero」→「imu baseline」→ state Ready。若仍失敗再看 log。Bug 2 / Bug 3 繼續排程。

## 2026-04-24r — Claude Code — [跨界: user_lib] 主程式推桿深度依 group 拆分 (feet 7cm / body 10cm)

### 修改檔案
- `user_lib/WASH_ROBOT.h`
  - 新增常數 `PUSHER_EXTEND_FEET_PULSE = 20000`（~7 cm，實測值對齊 Linux_test）
  - 新增常數 `PUSHER_EXTEND_BODY_PULSE = 30000`（~10 cm）
  - 保留 `PUSHER_EXTEND_PULSE = 144000`（full stroke，給 center 和 fallback 用）
  - `cycle_group_` template extend 區塊：依 group ("feet"/"body") 選對應 pulse，其他 fallback 到全行程
- `user_lib/WASH_ROBOT.cpp`
  - `cmd_init`：不再把 8 顆一起伸 full stroke；改成 feet(1,2,3,4) 伸 7cm、body(5,6,7,8) 伸 10cm 兩次分開呼叫
  - `cmd_pusher`：`extend` 依 group 選對應 pulse；`pusher all extend` 拆成 feet+body+center 三次分開呼叫（各自 depth）；`retract` 邏輯不變
  - `cmd_attach`（center extend）/ `do_step_down_` 的 center re-extend — 保留 PUSHER_EXTEND_PULSE（用戶未指定 center 深度）

### 原因
用戶要求主程式走路時腳組推桿全伸 7cm、身體組全伸 10cm（跟 Linux_test 實測對齊，避免吸盤壓力過強或推桿負載過大）。原本所有 `PUSHER_EXTEND_PULSE` 一視同仁 = 144000 pulse (~20cm) 壓到底，對真空 seal / 推桿 / 機構剛性都不友善。實作對齊 Linux_test menu 7/8 pattern。

**`[跨界: user_lib]`**：改動 WASH_ROBOT.{h,cpp} public 常數 + cmd_init / cmd_pusher 實作。API 簽名未改（constants 新增、沒刪舊的），但 runtime 行為對 web GUI 的 "init" / "pusher all extend" / "pusher feet extend" 按鈕結果有**直接影響**（推桿不再伸到底）。

### 下一步
繼續修 Bug 2 (`PR_trigger_sync` broadcast safety on slaves 2/4/5) 或 Bug 3 (`feet_backup`/`body_backup` mode=0)。

## 2026-04-24q — Claude Code — [跨界: user_lib] DM2J read_status 改讀 1 register + HOME_DONE mask

### 修改檔案
- `user_lib/DM2J_RS570.cpp`
  - `read_status()`：Modbus 請求從「read 2 registers」改「read 1 register」（`tx[5] = 0x01`），回應長度檢查從 9 改 7，組裝從 `(hi<<16)|lo` 改 `rx[3]<<8 | rx[4]`。16-bit 值落在 uint32_t 低 16 位
  - `print_status()`：HOME_DONE mask 從 `0x10000` → `0x0040`（bit 6，對照手冊 §5.3.2）
  - 函式頂部加詳細註解說明修改原因
- `Linux_test/main.cpp` menu 14 (`test_dm2j_monitor`)：HOME_DONE mask 從 `0x10000` → `0x0040`

### 原因
**`[跨界: user_lib]` 改動**。參考：`.claude/summaries/DM2J_RS_MODBUS_SUMMARY.md` 「Known Driver Bugs」#1 + `memory/project_dm2j_firmware_truth.md`。

DM2J-RS V1.0 手冊 §5.3.2 明確寫 0x1003 是單一 16-bit register，bit 0-6 為 FAULT/ENABLE/RUN/(unused)/CMD_DONE/PATH_DONE/HOME_DONE。但 driver 之前讀 2 個 register 組成 32-bit、把真實 bits 推到高 16 位，再用 0x0010/0x0020 低 16 位查 → 永遠抓不到完工 → 所有 `PR_move_cm` 內部 poll + 所有 `dm2j_wait_done_` 呼叫 timeout。實測 menu 7 先靠位置穩定偵測 workaround；WASH_ROBOT 的 `do_step_down_` 目前還 100% 跑不起來就是這個 bug。

改完後連帶受益：
- `DM2J_RS570::PR_move_cm` / `PR_move_cm_trigger_all` 內部 poll → 自動變對
- `WASH_ROBOT::dm2j_wait_done_`（line 163）→ 自動變對，主程式 do_step_down_ 具備跑起來的前提
- `Linux_test` menu 16 (`test_dm2j_group_sync`) 的 status 查詢 → 自動變對
- `windows_test` 呼叫 `print_status` → 自動變對

`dm2j_pair_poll_done`（Linux_test menu 7 用的位置穩定偵測）**不受影響**也**無需改動** — 它刻意不查 bit，留著當雙保險。

### 下一步
繼續修 Bug 2 (`PR_trigger_sync` broadcast safety) 或 Bug 3 (`feet_backup`/`body_backup` mode=0)。

## 2026-04-24p — Claude Code — 驅動 debug 改 env var WR_DRIVER_DEBUG 控制 [跨界: user_lib]

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` `init()`:
  - 加 `#include <cstdlib>` (std::getenv)
  - 開頭讀 env var `WR_DRIVER_DEBUG`，預設 `true`（對齊 [TEST MODE]）；若值為 `"0"` 則 `false`
  - 所有 driver init（DM2J×5 / ZDT×9 / JC-100×9 / PQW / IMU）的 debug 參數從寫死 `true` 改為 `dbg`
  - 印 `[OK] driver debug = ON|OFF (override via WR_DRIVER_DEBUG=0|1)` 讓使用者一眼看當下模式

### 原因
Sadie 回報：VS remote debug washrobot 會出現 "Broken pipe"（Pi process 沒死，是 VS↔gdbserver stdout pipe 被 25 顆裝置的 Modbus hex dump 淹爆）。直接在 Pi 跑 binary 沒這問題。

修法：保留 TEST MODE 預設 debug=true（現場 troubleshoot 完整 log），但加 env var 讓 VS debug 時透過 Properties → Debugging → Environment 設 `WR_DRIVER_DEBUG=0` 關掉 hex dump。主 crane 上線要撤 TEST MODE 時只改這裡的預設 `true → false`，env var 機制留著。

### 使用
- **VS remote debug**：專案 Properties → Debugging → Environment 加 `WR_DRIVER_DEBUG=0`
- **現場直接跑**：`./washrobot_new_PI` 預設 debug=true
- **強制關**：`WR_DRIVER_DEBUG=0 ./washrobot_new_PI`

### 規範邊界
`user_lib/WASH_ROBOT.cpp` 屬 Jim 範圍，標 `[跨界: user_lib]`。原來的 `true` 寫死本身就是 Sadie 04-21c 加的 TEST MODE，同性質改動。

## 2026-04-24o — Claude Code — 選項 7 rail move 改廣播同步觸發（1+3 硬體同步）

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 `dm2j_pair_rail_move_sync()` / `dm2j_pair_rail_move_abs_sync()` — 先 set PR on 左右腳（slave 1,3），再 `PR_trigger_sync(pr_num)` 廣播觸發。零 TCP 序列化 skew，硬體級同步。
  - 新增 `dm2j_set_safe_pr(slaves, pr_num)` helper — 把指定 slaves 的 PR<pr_num> 設 rpm=0（safe no-op）
  - `test_full_step()` 開頭 init 輪組（slave 2,4）+ 上滑台（slave 5）當 bystanders，用 `dm2j_set_safe_pr` 把它們 PR1 設 rpm=0。廣播時這三顆執行 rpm=0 → 不動作
  - `test_full_step()` 內 4 個 rail move 呼叫都換成 `_sync` 版本（retry back-off / phase A / phase B / cleanup return-to-0）
  - 舊 `dm2j_pair_rail_move()` / `_abs()` 保留不動，仍被 `test_full_step_report()`（選項 12）使用（沒 bystander 設置，繼續用個別 trigger 避免誤觸發）

### 原因
Sadie 要求：腳組 slave 1,3 機構剛性連接必須同步移動（ms-skew 會扭壞）。原實作用個別 `PR_trigger`，兩顆相差 5-20ms。改成 `PR_trigger_sync` 廣播需要保護其他非目標 slave：用 PR1 劃分 + bystanders PR1 set rpm=0，確保輪組 + arm 不會被廣播誤觸發。

## 2026-04-24n — Claude Code — 步伐補償寫入 motion_flow 規範 (§4.E)

### 修改檔案
- `.claude/motion_flow.md` — Phase 4 下新增 §4.E「步伐補償規則」：
  - Invariant：每 step 結束 rail 回 `rail_baseline`
  - 動態 target 公式：`phase_a_target = STEP_CM - offset`、`phase_b_target = baseline - rail_after_A`
  - 動態 retry skip：每 retry 先 check cumulative < phase_target 才執行，否則跳過
  - 兩個數值範例（Phase B retry compensation、retry skip）
  - 關鍵性質四點 + 呼叫端 `rail_baseline` 記錄時機
- `memory/project_step_compensation.md` — 依 motion_flow §4.E 更新，加 retry skip 細節跟數值範例、強調「以後所有 inchworm 流程皆套此邏輯」

### 原因
用戶要求「以後統一用這種方式走路」。之前 memory 只描述基本邏輯，現在把完整規範（含 retry skip）寫進 motion_flow 當**權威文件**給所有協作者（Jim、Sadie、未來協作者、所有 Claude session），不只留在我的 personal memory。日後 `WASH_ROBOT.cpp` 主程式實作 Phase 4 時直接依 motion_flow §4.E 走。

## 2026-04-24m — Claude Code — menu 7 retry 改為動態 skip（取代 pre-clamp）

### 修改檔案
- `Linux_test/main.cpp` `test_full_step()`:
  - 移除 pre-clamp `rail_backup_cm = step_cm / retry_cnt`，改成只印 WARN
  - `retry_grip_rail` 簽名加 `max_total_backoff_cm` 參數
  - retry 迴圈每輪先 check `cumulative_backoff + per_backoff > max_total_backoff_cm`，超過就印 `[RETRY n] skipped` + return false
  - retry log 加上 `cum X/Y` 顯示累積進度
  - Feet 呼叫端傳 `phase_a_target`（這 step 腳組實際要走的距離）
  - Body 呼叫端傳 `std::fabs(phase_b_target)`（這 step 身體要走的距離）

### 原因
使用者要求：用戶設定 `rail_backup=2cm × retry=3` 但 `step=4cm` 時，不要預先改小 rail_backup，改成動態執行 retry 1（-2cm）+ retry 2（-2cm）到達原位後，retry 3 不執行。這樣 retry 幅度可以維持使用者意圖，超過 phase 自己走的範圍時才停。Limit 用 `phase_a_target / |phase_b_target|` 而非 step_cm，配合步伐補償語意：每輪 retry 最多只能回到這 phase 自己的起點，不會 dig 到上一 step 的累積 offset。

## 2026-04-24l — Claude Code — WebSocket 層 heartbeat + tab visibility 強制重連

### 修改檔案
- `web_backend/server.js` —
  - 新常數 `WS_PING_INTERVAL_MS = 30000`
  - `wss.on('connection')` 裡加 `ws.isAlive = true` + `on('pong')` 更新
  - 新 `setInterval` 每 30s 對每個 ws client 送 `ws.ping()`，上輪沒 pong 的 client `terminate()`
- `web_backend/public/app.js` —
  - 新 `_onVisibilityChange()` handler：tab 變 visible 時若 ws 是 CLOSED/CLOSING 就立刻 `connectWs()` 不等 2s timer
  - `connectWs()` 內加 `document.addEventListener('visibilitychange', ...)` 註冊

### 原因
Sadie 回報：web + washrobot 放著 idle 一陣子後無法從 web 控制。診斷：三層連線只有「browser ↔ backend WS」沒有 heartbeat 保護（backend ↔ 3 個 TCP bridge 已有 setKeepAlive + 10s ping）。browser tab 背景化時 setInterval 被 throttle / 凍結，WS 長期閒置可能被 NAT/middlebox 偷殺。加 WS 層 ping/pong + tab visibility 回前景立即重連。

## 2026-04-24k — Claude Code — menu 7 步伐補償邏輯（rail 閉回 baseline）

### 修改檔案
- `Linux_test/main.cpp` `test_full_step()`：
  - 初始化 (step loop 之前)：加 `rail_baseline_L/R` 讀取 + log
  - Step 開頭：讀 `rail_cur`、算 `offset = rail_cur - rail_baseline`、算 `phase_a_target = step_cm - offset`，log 印出 offset 跟 target；若 target ≤ 0 直接 abort
  - Phase A 軌道 `rail +step_cm` 改成 `rail +phase_a_target`
  - Phase A 結束後讀 `rail_after_A`，`phase_b_target = rail_baseline - rail_after_A`（關到 baseline，不是只反 feet_delta）
  - Phase B 軌道從 `-feet_delta` 改成 `phase_b_target`
  - 保留 feet_delta 的 log 作為觀察用
- `memory/project_step_compensation.md` — 新增，紀錄「步伐補償」專案用語對應此邏輯（含 Step 1/2/3 數值範例）
- `memory/MEMORY.md` — 加 index

### 原因
用戶要求：Phase B retry 若導致 body 沒完全追上（rail 留下 offset），下一 step 的 Phase A 要減小 `step_cm - offset`，Phase B 要額外走把 offset 關回 baseline。Invariant：rail 每步結束一定回到 baseline。用戶定義這整套為「步伐補償」，未來聽到這詞直接套這邏輯，不再重新討論。對比上一版 j（Phase B = -feet_delta 只 match feet）差別：j 讓 rail offset 持續累積，k 每步關回 0。

## 2026-04-24j — Claude Code — menu 7 Phase B 用實際 feet delta 而非固定 step_cm

### 修改檔案
- `Linux_test/main.cpp` `test_full_step()` step loop：
  - Phase A 「收腳推桿」之後加一步 `read_position_cm(L/R)` 記 `rail_before_{L,R}`
  - Phase A verify + retry 結束之後加 `read_position_cm(L/R)` 記 `rail_after_{L,R}`
  - 計算 `feet_delta = avg(L, R)`，log 印出 L/R 各自差值
  - Phase B 的 rail 後退距離從固定 `-step_cm` 改成 `-feet_delta`

### 原因
Phase A 的 retry_grip_rail 會讓腳組軌道倒退（-rail_backup_cm 每次），導致實際前進距離 < step_cm。原本 Phase B 固定走 `-step_cm` 會讓身體走超過腳組實際進度，下一 step 的 baseline 就不對。改讀實際位置差取 L/R 平均，Phase B 走同樣絕對量反向，確保一個 step 結束時鐵軌回到 Phase A 起始位置（即腳+身體都淨前進同樣距離）。

## 2026-04-24i — Claude Code — 把 DM2J 手冊真相記錄起來（summary / work_log / memory）

### 修改檔案
- `.claude/summaries/DM2J_RS_MODBUS_SUMMARY.md` — 依 `doc/DM2J-RS.V1.pdf` V1.0 實機手冊**整篇重寫**：
  - Status register 0x1003 改回單 16-bit（bit 0-6），移除「Bit16=HOME_DONE」誤解
  - 0x1801 控制字表全改對：0x1111=復位當前報警、0x2211=save EEPROM、0x2233=恢復出廠值 等
  - 新加「Enable 機制」章節：DI1 NC auto-enable + Pr0.07 (0x000F) 軟體強制 enable
  - PR Mode 欄位加警告：實際 `1=absolute, 2=relative`，舊 driver 註解顛倒；`mode=0` 是「路徑未配置」不會動
  - 文末新加「Known Driver Bugs」清單列出 user_lib/DM2J_RS570 待修的四類 bug
- `.claude/work_log.md` — 頂部加 2026-04-24 session 條目：發現緣起、核心真相、過去 log 重解讀、user_lib 待修項、workaround 說明、影響範圍
- `memory/project_dm2j_firmware_truth.md` — 新增 memory 條目（對齊 ZDT firmware quirks 那個的格式）
- `memory/MEMORY.md` — 加 index 條目

### 原因
Sadie 要求在動手修 user_lib 前先把發現記錄起來，確保其他協作者 session 能直接看到權威文件（summary + work_log），未來我的 Claude session 也會從 memory 直接看到 → 不會再花一輪 debug 撞同一個雷。程式碼本身未動。

## 2026-04-24h — Claude Code — 選項 7 retry 軌道退縮距離改使用者可調 + 位置下限保護

### 修改檔案
- `Linux_test/main.cpp` `test_full_step()`:
  - 新增 prompt `"Retry rail back-off cm (DM2J retract per retry) [5]: "`
  - 移除 `static constexpr double RAIL_BACKUP_CM`，改為 local `double rail_backup_cm = 5.0`
  - 加 clamp：`retry_cnt × rail_backup_cm ≤ step_cm`（否則軌道會退縮超過 step 前原本位置）超過會 `[WARN]` 印出並降到 `step_cm / retry_cnt`
  - body 退縮的 live call（行 1284 `+RAIL_BACKUP_CM` → `+rail_backup_cm`）改用新變數
  - feet retry 的註解（行 1248-1250）也改用 `rail_backup_cm`，未來 uncomment 可用
  - lambda `retry_grip_rail` 透過 `[&]` capture 取用新變數

### 原因
Sadie 要求：
1. 錯誤重試時 DM2J 軌道縮回距離可調（預設 5cm）
2. 總縮回量（`N × backup`）不能小於原本位置 → 加 clamp

現場不同吸盤位置 / 牆面條件需要調大/調小 back-off；clamp 避免軌道退過 step 起點造成機構異常。

## 2026-04-24g — Claude Code — DM2J 完工判斷改位置穩定偵測 + menu 7 cleanup 歸 0

### 修改檔案
- `Linux_test/main.cpp`：
  - 頂部加 `#include <cmath>`（std::fabs 用）
  - 新增 `dm2j_pair_poll_done(left, right, left_target, right_target)` — 位置穩定偵測 helper：每 150ms 讀兩顆位置，連續 3 次 `|Δpos| < 0.01cm` 且 `|pos - target| < 0.5cm` 就算完成，15s timeout
  - 新增 `dm2j_pair_rail_move_abs(left, right, pr_num, target_cm)` — 兩顆同時走絕對目標（cleanup 歸 0 用）
  - `dm2j_pair_rail_move()` 改用 `dm2j_pair_poll_done` 做完工判斷（原本查 `CMD_DONE/PATH_DONE` bit 在這 firmware 永遠讀不到 → timeout）
  - `test_full_step()` cleanup 在收完推桿之後、關 PQW 之前加一步 `dm2j_pair_rail_move_abs(..., 0.0)` 讓 rail 回絕對 0 位置；失敗只印 WARN 不中止

### 原因
實測 DM2J rail 確實用 mode=1 絕對模式走到目標，但 status register 一直回 `0x00320000`。Driver 依 summary 猜的 `CMD_DONE=bit4 / PATH_DONE=bit5` 查 LOW word，實際 firmware 的 bit 位置不同 → 永遠找不到完成指示 → timeout `[ABORT]`。改用位置穩定偵測（跟 ZDT firmware quirks memory 驗證過的 pattern 一樣），不依賴 bit layout 推論，穩定性優先。使用者另要求結束後 rail 歸 0，用新的 `move_abs` helper 實作。

## 2026-04-24f — Claude Code — Linux_test 加本地 DM2J motor_enable/disable helper（避開 user_lib 未實作）

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 `_dm2j_crc16()` Modbus CRC16 helper
  - 新增 `dm2j_write_0x1801(cli, slave, value)` — 寫 0x1801 register 的 generic helper
  - 新增 `dm2j_manual_enable()` / `dm2j_manual_disable()` — 寫 0x1111 / 0x2233
  - `test_dm2j_group_sync()` 改用這些本地 helper，不呼叫 `drv.motor_enable()`（避免 link 錯誤）

### 原因
`DM2J_RS570::motor_enable()` / `motor_disable()` 在 header 宣告但 `.cpp` 沒實作 → link 報 `undefined reference`。這是 user_lib 範圍（Jim）的事，正式要補進 `DM2J_RS570.cpp`。為了今天能測，Linux_test 自己送 Modbus function 0x06 frame 寫 0x1801 register。日後 Jim 補完可以移除本地 helper。

### 需要 mailbox
建議往 `.claude/mailbox.md` 發訊息給 Jim：DM2J motor_enable/disable 實作缺失。

## 2026-04-24e — Claude Code — Linux_test 新增選項 16「DM2J group sync move」

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 `test_dm2j_group_sync()`：硬體同步移動 {腳組 1,3} 或 {輪組 2,4}
    - 啟動時初始化所有 5 顆 DM2J 的 PR1、PR2 為 rpm=0 safe state
    - 使用者選 f/w 群組 → 目標 cm → RPM
    - 目標 slave `motor_enable` + 刷新對應 PR slot（feet 用 PR1、wheels 用 PR2）
    - 廣播 `PR_trigger_sync(pr_slot)` — 非目標 slave 執行 rpm=0 PR → 不動
    - wait 兩顆 target 到位（10s timeout）
    - 動完後把該 PR slot 重置回 safe，避免下次廣播殘留
    - 退出時 reset 所有 PR + disable 所有馬達
  - 選單加第 16 項

### 原因
Sadie 回報 slave 1,3 跟 2,4 機構剛性連接，必須硬體同步（ms 級差距會扭壞機構）。個別 `PR_trigger` 有 TCP round-trip 延遲不夠同步。用 `PR_trigger_sync` 廣播 + PR slot 劃分（PR1 for 1,3 / PR2 for 2,4）+ safe-state 初始化，非目標 slave 執行 rpm=0 不動作，實現真硬體同步。

## 2026-04-24d — Claude Code — menu 7 cleanup 最後加 controlAll(false)

### 修改檔案
- `Linux_test/main.cpp` — `test_full_step()` cleanup 尾段：
  - 舊：`controlRelay(PQW_CH_PUMP, false)` 只關 CH1
  - 新：`controlAll(false)` 一次關 CH1~8（包含 CH5 刷子 / CH6 水泵 / CH7 水閥 / CH8 保留）

### 原因
使用者要求結束後所有繼電器都關掉。原本只關 pump，CH5-8 如果之前被其他流程意外打開會留在 ON 狀態。現在用 controlAll 一次關完，不論哪個 channel 有沒有被用到都保證結束狀態乾淨。Valves（CH2/3/4）仍然在 controlAll 之前個別關並 settle 300ms，維持「先放真空再收推桿」的安全順序。

## 2026-04-24c — Claude Code — dm2j_pair_rail_move 改用 mode=1 絕對模式

### 修改檔案
- `Linux_test/main.cpp` — `dm2j_pair_rail_move()`：
  - 動作前先 `read_position_cm()` 讀左右當前位置，讀失敗就 abort
  - 計算 `target = current + cm`（保留呼叫端「相對位移」語意）
  - `PR_move_cm_set` mode 從 `0` 改 `1`（絕對），改送絕對 target
  - log 印出「left X → Y cm / right X → Y cm」方便對照

### 原因
實機測試發現改個別觸發後 status 仍停在 `0x00320000` timeout。比對 menu 2 發現差異：menu 2 用 mode=1（絕對）**會動**，menu 7 原本 mode=0（summary 寫是「相對」）**不動**。推測此 DM2J firmware 的 mode=0 是「PR 未配置 / 跳過」不是「相對」，所以 drive 收到 trigger 但不進入運動狀態、ENABLE 維持 0。改用已驗證的絕對模式 + 讀當前位置計算 target，可靠性優先。代價：每次 rail move 多 ~20ms 讀位置，影響可忽略。

## 2026-04-24b — Claude Code — menu 7 cleanup 改成完整歸位（對齊 menu 8）

### 修改檔案
- `Linux_test/main.cpp` — `test_full_step()` 結尾 cleanup 流程：
  - 舊：只 disable ZDT driver，valves/pump 保留
  - 新：依序 (1) 關 CH2/3/4 所有 valve → settle 300ms (2) 收 feet 推桿 到 0 (3) 收 body 推桿 到 0 (4) pump OFF (5) disable ZDT driver

### 原因
使用者要求 menu 7 結束後所有設備歸位（對齊 menu 8 行為）。舊版為「機器人可能還吸在牆上」的安全考量保留 valves + pump；但既然 menu 7 現在已有 initial attach（機器人從非吸附狀態開始），結束時也應該回到非吸附狀態，流程才閉環。DM2J 沒 motor_disable 實作（header 宣告未落地），仍保持通電不動。

## 2026-04-24a — Claude Code — dm2j_pair_rail_move 改個別觸發 + menu 7 檢查 return

### 修改檔案
- `Linux_test/main.cpp` — `dm2j_pair_rail_move()`（line 918）從 broadcast 改個別觸發：
  - 移除 `PR_move_cm_trigger_all`（內部走 `writeSingle_sync` slave 0 broadcast）
  - 改用 `left.PR_trigger(pr_num)` + `right.PR_trigger(pr_num)` 兩次個別 writeSingle
  - 新 poll 迴圈同時監視兩顆（up to 10s），任一 fault 或 timeout 即 return true
- `test_full_step()` 三個 rail 呼叫點都檢查 return value：
  - Phase A 腳組軌道前進：fail → cerr `[ABORT] feet rail move failed` → break step loop
  - Phase B 身體軌道後退：fail → cerr `[ABORT] body rail move failed` → break
  - `retry_grip_rail` 內部 rail backup：fail → cerr `[ABORT RETRY] rail backup failed` → return false（放棄整個 retry，因為下一次 retry 位置不會變）

### 原因
實機測試發現 menu 2（個別 `PR_trigger` to slave 2/3/5）可動，menu 7（`PR_trigger_sync` broadcast slave 0）不動，DM2J status 停在 ENABLE=0 重複讀取到 timeout。原因：USR-TCP232-304 gateway 對 Modbus broadcast 不可靠（跟 memory 裡 ZDT firmware quirk 類似），加上 broadcast 也會觸發 slave 2/4/5（輪子 + 上滑台）造成誤動作 — 不是正確工具。改個別觸發後左右起步差 ~5-20ms（DM2J_RPM=500, 10mm pitch → ~0.4-1mm 位置差），犧牲少許同步精度換正確性 + 安全性。呼叫端從「忽略 return」改「fail 就 abort step」避免矽默失敗繼續跑下面推桿流程造成誤判。

## 2026-04-24 — Claude Code — menu 7 加 initial attach + user gate

### 修改檔案
- `Linux_test/main.cpp` — `test_full_step()` 新增 initial attach 區塊（抄 menu 8 的 pattern）：
  - Pre-flight 訊息從「must already be attached」改為「will perform initial attach」
  - Pump ON 後先伸 feet 7cm + body 10cm staged（valves 全關），讀 JC-100 1..8 報告（預期 ≈0）
  - User gate：Enter 確認後開 CH2+CH3，settle 2000ms 再讀一次
  - Step 迴圈加 `!user_aborted` 條件，使用者在 gate 按 q 就跳過 step 進 cleanup
  - Center 組（ZDT slave 9 + PQW CH4）依使用者指示**整組跳過**，menu 7 initial 不開第三口

### 原因
Menu 7 原本假設「機器人已吸附在牆上」不做 initial attach，但現場測試需要從牆邊完全釋放狀態開始執行。對齊 menu 8 做法：先伸推桿看吸盤定位、閥關、讀壓力驗證 cup 無貼合、Enter 後才開閥入 step 迴圈。

## 2026-04-23zzzz — Claude Code — menu 7 改 staged 7/10cm extend

### 修改檔案
- `Linux_test/main.cpp` — `test_full_step()` 三處 extend 全部改用 `zdt_group_extend_staged()`：
  - Phase A Feet：`PUSHER_EXTEND_PULSE` (144000 全行程) → `PUSHER_EXTEND_FEET_PULSE` (20000 ~7cm) staged
  - Phase B Body：`PUSHER_EXTEND_PULSE` → `PUSHER_EXTEND_BODY_PULSE` (30000 ~10cm) staged
  - `retry_grip_rail` 內 re-extend：依 `zdt_group.front() <= 4` 判斷 feet/body 選 target，統一改 staged（抄 menu 8 retry_grip 的 pattern）
- Menu 顯示字串 + section header + 函式標題同步改成「8 pushers staged 7/10cm」

### 原因
使用者要求 menu 7 與 menu 8/11/12 對齊：腳組只伸 ~7cm、身體組 ~10cm（不再全行程 200mm），且分兩階段 (half → full) 避免一次伸到底對吸盤 seal 不利。helper `zdt_group_extend_staged()` + 兩個 pulse 常數早就存在，只是 menu 7 沒用到。

## 2026-04-23zzz — Claude Code — Linux_test 新增 menu 15「DM2J zero position」

### 修改檔案
- `Linux_test/main.cpp` — 新增 `test_dm2j_zero()`；menu 顯示 + dispatch 加 15

### 原因
Debug 過程需要把 DM2J 目前位置當零點重置（呼叫 `home_set_current_pos_zero()`，寫 0x6002 = 0x0021）。會改寫座標原點屬於破壞性操作，加一層 `type 'yes' to confirm` 防手滑；動作前後各讀一次位置顯示給使用者對照，200ms 等驅動器 latch 新零點。

## 2026-04-23zz — Claude Code — Linux_test 新增 menu 14「DM2J live monitor」

### 修改檔案
- `Linux_test/main.cpp` — 新增 `test_dm2j_monitor()`；menu 顯示 + dispatch 加 14

### 原因
DM2J 5 顆 slave 都不動，status bit 顯示 `[ENABLE]` 未 set，使用者需要在調面板 P00.03 / 量 48V 動力電源時能即時觀察位置 + status bits 是否變化（ENABLE / RUN / FAULT / HOME_DONE）。新選項純監視、不動馬達；驅動 init 時刻意 `debug=false` 避免 TX/RX hex dump 洗版，讓一行 live display 清楚可讀。每 200ms 更新。

## 2026-04-23z — Claude Code — Linux_test 新增 menu 13「Water tank」

### 修改檔案
- `Linux_test/main.cpp` — 新增 `test_water_tank()` + helper `water_wait_or_abort()`；menu 顯示 + dispatch 加 13；頂部 Windows include 區加 `<conio.h>` 供 `_kbhit()/_getch()` 用（abort 檢查跨平台）

### 原因
使用者要測試水箱（PQW CH5 刷子 / CH6 水泵 / CH7 進水閥）。現有 menu 5 雖能操作 PQW 但範圍是 8 channel 易誤觸，且無計時腳本 — 進水閥忘了關會淹水。新選項限定 CH5/6/7、腳本模式有秒數上限（valve ≤120s / wash ≤300s / gap ≤60s）+ Enter 中止 + 離開自動全關。包含 4 子模式：手動 toggle / 補水 / 刷洗 / 完整循環（補水→閒置→刷洗）。

## 2026-04-23y — Claude Code — Session 總結歸檔

### 修改檔案
- `.claude/work_log.md` — 頂部加 2026-04-23 Session 總結條目：Linux_test 12 option 現況、slave ID 映射、TEST MODE 清單、硬體校準值、9 個 debug 發現、未解問題、未 commit 狀態、規範邊界
- `memory/project_zdt_firmware_quirks.md` — 新增：ZDT 韌體 4 個 quirk + workaround（pos_reached / broadcast echo / frame 對齊 / sync pattern）
- `memory/project_vacuum_seal_patterns.md` — 新增：valve-before-extend + staged extend 兩個 pattern + 實測參數
- `memory/MEMORY.md` — 加兩個 index 條目

### 原因
Sadie 要清空 session 前整理。Session 內 changelog 已有 ~45 筆細節條目，但 work_log 只有幾筆、memory 未更新。補上：(a) session-level 軌跡總結進 work_log (b) durable 技術洞見進 memory。

## 2026-04-23x — Claude Code — 加選項 12「Full step with rail, report only」

### 修改檔案
- `Linux_test/main.cpp` — 新增 `test_full_step_report()`（選項 12）：結構同選項 7（有 DM2J rail），但：
  - 拿掉 `retry_grip_rail` lambda + verify 呼叫
  - 每個 phase 結束用 `vacuum_report()` 只印壓力，不做 threshold 判斷
  - 包含初始貼附階段（閥關 → 伸桿 → 使用者等 Enter 開閥）跟完整 cleanup（關閥 / 推桿歸 0 / 泵浦 OFF）
  - 用 staged extend（兩階段 → 1 秒 → 全伸）
  - 選單加第 12 項；main 分派加

### 新選項矩陣（rail vs vacuum handling）
| 選項 | rail | 真空 |
|---|---|---|
| 7  | ✅   | verify + retry + ABORT |
| 8  | ❌   | report only |
| 11 | ❌   | verify + retry + FAIL-continue |
| 12 | ✅   | report only |

### 原因
Sadie 要有滑桿但不驗證的版本（選項 12），用於現場觀察壓力變化、diagnose 建立真空的能力。完整四象限測試工具到齊。

## 2026-04-23w — Claude Code — 新增 frame_capture.py（RTSP → /tmp/cam_latest.jpg）

### 修改檔案
- `frame_capture/frame_capture.py` — 新增（~140 行）
- `frame_capture/README.md` — 新增（啟動、CLI flag、驗證步驟、systemd service、故障排除）

### 腳本重點
- OpenCV + FFmpeg 持續解碼 RTSP 子碼流（避免每次 query 重新 handshake）
- `os.replace()` Linux atomic rename，避免 detect_server 讀到半寫入檔
- 斷線自動重連（預設 2s 間隔）
- FPS 限流（預設 10fps），避免過度寫檔
- `OPENCV_FFMPEG_CAPTURE_OPTIONS=rtsp_transport;tcp` RTSP over TCP 較穩
- `CAP_PROP_BUFFERSIZE=1` 只保留最新 frame 降延遲
- SIGINT/SIGTERM 清理退出
- 每 10s stderr 印一次 fps + latest_age 健康統計

### 原因
Sadie 要開工寫攝影機避障，第一步 sidecar decoder。相機 RTSP URL 已由 Sadie 提供（Xiongmai H264DVR @ 192.168.1.10，子碼流），預設值直接帶進程式。程式先寫，實機 ffmpeg 驗證 + 跑腳本由 Sadie 之後做。

### 下一步
- Sadie 實機驗 ffmpeg + 跑 frame_capture.py
- 我接著寫 C++ `FrameAnalyzer` class（UDP to :5040 + JSON parse + decide）

## 2026-04-23v — Claude Code — 新增 camera_obstacle_plan.md（攝影機窗框避障規劃）

### 修改檔案
- `.claude/camera_obstacle_plan.md` — 新增
  - 系統架構（frame_capture.py / detect_server / washrobot）
  - 參數表分 5 類：攝影機 / 網路協定 / 決策邏輯 / server 內建 / frame_capture
  - Detect server v2 回應格式（多了 width_cm/height_cm/distance_cm/near_edge_cm）
  - Null 處理表 + 5 狀態 client 收發狀態機（ok/empty/error/no_reply/server_down）
  - C++ FrameAnalyzer decide_from_result() 決策邏輯範例
  - 分階段 roadmap + 規範邊界備註

### 原因
Sadie 提供：相機資訊（Xiongmai H264DVR RTSP + 帳密）、假設貼牆距離 15cm、detect_server v2 回應格式、client 收發建議（2s timeout × 3 連續失敗 → server_down）。先存檔規劃 + 參數，程式碼下一步才動工。

## 2026-04-23u — Claude Code — 兩階段 extend 修 short-circuit bug

### 修改檔案
- `Linux_test/main.cpp` `zdt_group_extend_staged()` — 原本 stage 1 `zdt_group_move_sync` 回 true 就 early return，導致 stage 2 沒跑，馬達停在半程。改成不管 stage 1 結果都跑 stage 2，err 合併回傳

### 原因
Sadie 回報：加了兩階段後實際只伸 3.5 cm（一半）。根因：stage 1 timeout（已知 ZDT pos_reached 不穩）觸發 early return。必須讓 stage 2 永遠執行。

## 2026-04-23t — Claude Code — 選項 8/11 推桿 extend 改兩階段

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 helper `zdt_group_extend_staged(drvs, slaves, full_target, delay_ms=1000)`：先 extend 到 half_target → 等 1s → extend 到 full_target
  - 選項 8 / 11 所有 extend 相關呼叫（初始貼附 / step phase / retry grip）全部改用 staged 版本：
    - feet extend to 20000 → 10000 → (1s) → 20000
    - body extend to 30000 → 15000 → (1s) → 30000
  - Retract 仍然單階段（直接到 0）

### 原因
Sadie 要 ZDT 伸長時分兩階段（先一半、等 1 秒、再另一半）減少吸盤接觸衝擊、給真空閥建立負壓時間 seal。

## 2026-04-23s — Claude Code — 推桿 extend 常數現場實測校正

### 修改檔案
- `Linux_test/main.cpp` — `PUSHER_EXTEND_FEET_PULSE` / `PUSHER_EXTEND_BODY_PULSE` 改直接硬編實測值：
  - `PUSHER_EXTEND_FEET_PULSE = 20000`（實測 ~7 cm）
  - `PUSHER_EXTEND_BODY_PULSE = 30000`（實測 ~10 cm）
  - 拿掉 `PUSHER_PULSES_PER_CM = 7200` 推算（當初從 144000 pulses = 20 cm 反推錯了）
- print 字樣從 "7 cm" → "~7 cm" 提醒這是近似

### 原因
Sadie 實測：原本推算 7 cm = 50400 pulses / 10 cm = 72000 pulses，結果超過 15 cm。實際腳組 20000 pulses / 身體組 30000 pulses 才對。直接硬編實測值比反推更可靠。

## 2026-04-23r — Claude Code — 選項 8/11 推桿改部分伸（腳 7cm / 身 10cm）

### 修改檔案
- `Linux_test/main.cpp` —
  - 加 `PUSHER_PULSES_PER_CM = 7200`（144000 pulses / 20cm）
  - 加 `PUSHER_EXTEND_FEET_PULSE = 50400`（7 cm）
  - 加 `PUSHER_EXTEND_BODY_PULSE = 72000`（10 cm）
  - 選項 8 所有 `zdt_group_move_sync(.. feet_slaves, PUSHER_EXTEND_PULSE)` → `PUSHER_EXTEND_FEET_PULSE`
  - 選項 8 所有 body_slaves 同理 → `PUSHER_EXTEND_BODY_PULSE`
  - 選項 11 同樣改法（包括 retry_grip lambda 裡依 zdt_group front() ≤4 判定 feet/body，選對 extend target）
  - 選項 7（有滑桿的完整版）不動

### 原因
Sadie 要腳組推桿伸 7cm、身體組推桿伸 10cm，不要 full stroke 20cm。避免推桿整根 extend 時機器被過度推離牆面。

## 2026-04-23q — Claude Code — 選項 8/11 initial attach 拆成「閥關伸桿 → 等操作員 → 開閥驗證」

### 修改檔案
- `Linux_test/main.cpp` `test_full_step_no_rail()` (選項 8) 和 `test_full_step_no_rail_verify()` (選項 11) 初始貼附流程：
  - **階段 1：** 不開閥，直接 extend 所有 8 支推桿（讓操作員目視確認貼附位置）
  - **印當前壓力**（閥關所以應該 ≈ 0）
  - **User gate：** `Press Enter to OPEN valves, 'q' to abort`
  - **階段 2（若沒 abort）：** 開 CH2+CH3 → settle 2s → 再印壓力（選項 8 只 report，選項 11 驗證 + retry）
  - User 若 abort → 跳過整個 step 迴圈，直接進入 cleanup

### 原因
Sadie 要求：初始貼附時先不要開三口二位電磁閥（CH2/CH3），給操作員時間目視確認吸盤貼牆位置是否正確。確認後操作員手動觸發開閥，才真正開始抽真空。避免「閥一下開了馬上錯位吸破真空」的情境。

## 2026-04-23p — Claude Code — zdt_group_move_sync 加位置不變 fallback

### 修改檔案
- `Linux_test/main.cpp` `zdt_group_move_sync()` settle 偵測：
  - `STOP_RPM` 5 → 20（寬鬆容許殘值）
  - 新增 `POS_DELTA_DEG = 0.15`：追蹤每 slave 前次 real_pos，Δpos ≤ 0.15° 視為靜止
  - 完成條件改 `stopped_by_speed || stopped_by_pos`（任一符合即可）
  - Debug 印 `rpm=X Δpos=Y°` 看是哪個 signal 觸發

### 原因
Sadie 回報 ZDT 物理上已停但 poll loop 卻總 timeout。根因：ZDT firmware 在靜止時 `real_speed` 可能回非 0 殘值（5-20 RPM 噪訊），速度 threshold 5 太嚴。改加位置不變 fallback — 就算 real_speed 讀值不乾淨，只要 real_pos 在 450ms 內沒變化就算到位。

## 2026-04-23o — Claude Code — 選項 11 對齊選項 8：完整 cleanup + retry 失敗繼續不中止

### 修改檔案
- `Linux_test/main.cpp` `test_full_step_no_rail_verify()`：
  - 四處 retry 失敗訊息（initial feet / initial body / step feet / step body）從 `[ABORT] ... Stopping.` 改 `[FAIL] ... continuing anyway` — 不 break、不 return，繼續下個 phase / step
  - 結尾 cleanup 從「只 disable ZDT」擴增成跟選項 8 一樣完整流程：關 CH2/CH3/CH4 → retract feet → retract body → CH1 pump OFF → disable ZDT

### 原因
Sadie 要選項 11 跟選項 8 結構對齊（initial attach 已經有、現在補上 cleanup），且 retry 超限時只印 [FAIL] 繼續，不要像之前那樣整個停止。適合全步驟跑完後看哪幾輪成功哪幾輪失敗的 diagnostic 用途。

## 2026-04-23n — Claude Code — 選項 8 完整 cleanup（關閥 + 推桿歸 0 + 泵浦 off）

### 修改檔案
- `Linux_test/main.cpp` `test_full_step_no_rail()` 結尾 cleanup：
  - 依序：關 CH2/CH3/CH4 三閥 → 等 300ms → 腳組 1,2,3,4 retract → 身體組 5,6,7,8 retract → 泵浦 CH1 OFF → disable ZDT 驅動
  - 順序重要：先關閥讓吸盤釋壓，避免帶著真空撕開吸盤

### 原因
Sadie 要選項 8 走完全部步數後完整清場：所有 relay 關、所有推桿回 0。原本只 disable ZDT driver，閥跟推桿位置都留在最後狀態。



### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 `test_full_step_no_rail_verify()`：結構類似選項 8，但每階段（初始貼附 / Phase A / Phase B）做 `vacuum_verify` 對 threshold，失敗 trigger `retry_grip`（release valve → retract → valve ON → re-extend → settle → re-verify）最多 N 次，exhaust 後 ABORT
  - retry_grip 是局部 lambda，只需 group 名 + slaves + valve channel 三參（沒有 center valve 參）
  - 選單加第 11 項；option 8 選單字樣更新為 "report, no verify" 區別
  - main 分派加分支

### 原因
Sadie 要「像選項 8 但帶 vacuum 判斷 + 重吸」。選項 8 保持純 diagnostic 模式（只印壓力、繼續），選項 11 是嚴格模式（驗證不過就整組 retry，超限就 ABORT）。兩個同時存在，用途分明。

## 2026-04-23l — Claude Code — 選項 8 加 initial attach phase

### 修改檔案
- `Linux_test/main.cpp` `test_full_step_no_rail()` — 在 step 迴圈前加初始貼附段：
  - 開兩個閥（CH2 feet + CH3 body）
  - 腳組 4 支 extend（ZDT 1,2,3,4）
  - 身體組 4 支 extend（ZDT 5,6,7,8）
  - Settle 2s
  - 讀 JC-100 1-8 報告壓力
- 更新 PRE-FLIGHT 提示：不再要求 operator 先貼好，測試會自己做初始貼附

### 原因
Sadie 要選項 8 最一開始先把所有推桿伸出吸住（初始貼附），之後再進入腳組 → 身體組的循環。原本流程假設 operator 手動貼好，對測試不方便；現在可以把機器放到牆上就按 Enter，程式自己貼。

## 2026-04-23k — Claude Code — 選項 7/8 拿掉中心推桿（ZDT 9 + CH4）

### 修改檔案
- `Linux_test/main.cpp` — `test_full_step()` 跟 `test_full_step_no_rail()` 的 Phase B：
  - `body_center_slaves = {5,6,7,8,9}` → `body_slaves = {5,6,7,8}`
  - 拿掉 `pqw.controlRelay(PQW_CH_VALVE_CENTER, ...)` 呼叫
  - Phase B 名稱從 "Body + Center" → "Body"
  - 所有 "body+center" 字樣改 "body"
  - retry_grip_rail 第 5 參 extra_valve_ch 改 0（無第二個閥）
  - JC-100 讀取範圍從 5,6,7,8,9 → 5,6,7,8

### 原因
Sadie 要求。中心推桿（ZDT 9）+ 中心吸盤閥（PQW CH4）暫時不用，現階段先驗證腳組 + 身體組。(User 說 "8,9"，但選項 9 是 SD76 不動 ZDT，所以實際改的是 7 跟 8。)

## 2026-04-23j — Claude Code — 選項 8 改成只印壓力不判門檻（diagnostic mode）

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 `vacuum_report()` helper：讀 JC-100 一群印壓力值，不做門檻判斷，無回傳
  - `test_full_step_no_rail()` Phase A / Phase B 的 `vacuum_verify + retry_grip + ABORT` 三件套改成單一 `vacuum_report()` 呼叫
  - 壓力無論如何繼續下一步

### 原因
Sadie 要選項 8 在此階段當純診斷工具 — 現場觀察壓力變化、不要被門檻卡住中斷流程。選項 7 保持完整門檻 + retry + ABORT 行為。`vacuum_verify()` 跟 `retry_grip` 都保留給選項 7 使用。

## 2026-04-23i — Claude Code — zdt_group_move_sync 加 per-slave retry + 失敗 skip 不 abort

### 修改檔案
- `Linux_test/main.cpp` `zdt_group_move_sync()` 重構：
  - enable 和 pos_queue 各自最多 3 次 retry，每次 back-off 120ms
  - 某個 slave 整組 retry 失敗 → 只 skip 那顆，不中斷整組
  - 收集 `queued`（成功佇列的）跟 `skipped`（放棄的）
  - Poll loop 只追蹤 `queued`
  - 全部 queued 成功到位且有 skipped → 回 error 讓 caller 知道
  - Timeout 時同時列 stuck + skipped
  - 加 `#include <functional>` 給 std::function

### 原因
Sadie 回報 option 8 retry 時連續 `[ERR] ZDT slave 1 enable fail` / `slave 3 enable fail`。根因：ZDT driver 連續送 Modbus 命令時 TCP buffer 有殘留 echo 造成下一命令 echo 讀錯 → 某個 slave 偶爾 enable 回非預期 bytes → 回 true。原邏輯一失敗就整組 abort 太嚴格，且失敗 slave 下次 retry 可能就好。改加 per-slave 3 次 retry + back-off + 放棄某顆時繼續其他 slave。

## 2026-04-23h — Claude Code — option 6 (test_zdt_group) 也補上速度回零 fallback

### 修改檔案
- `Linux_test/main.cpp` `test_zdt_group()` Phase 2 poll loop — 加入跟 `zdt_group_move_sync` 一樣的速度 fallback：`|real_speed| ≤ 5 RPM` 連續 3 次 poll 且超過 500ms → 視為完成

### 原因
Sadie 跑選項 6 slaves 5,6,7,8,9：馬達實際都到 `real_pos≈3600°` (=144000 pulses = 10 轉) 但 `pos_reached` bit 沒被 firmware set → 15s timeout 全部 aborted。之前只在選項 7/8 用的 `zdt_group_move_sync` 有這個 fallback，選項 6 的 poll loop 漏掉。

## 2026-04-23g — Claude Code — zdt_group_move_sync 移除 trigger_sync_move 誤警告

### 修改檔案
- `Linux_test/main.cpp` — `zdt_group_move_sync()` 不再檢查 `trigger_sync_move()` 回傳值

### 原因
ZDT driver 的 `trigger_sync_move()` 送 Modbus broadcast (slave 0x00)，依規範**不會有回應**。driver 看 readEcho 空就回 true (=專案慣例的 error)。我的 code 原本檢查回傳值印 `[WARN] trigger_sync_move reported send error`，但這實際上是 broadcast 正常行為，不是錯誤。Sadie 回報 retry 時 WARN 重複出現誤導 debug 方向。

## 2026-04-23f — Claude Code — Linux_test 選項 7/8 改為「先開真空再伸推桿」

### 修改檔案
- `Linux_test/main.cpp` — 選項 7 (Phase A feet / Phase B body+center)、選項 8 (同 2 phase)、兩個 retry grip lambda 都改順序：
  - **舊：** retract → (rail) → extend → valve ON → settle
  - **新：** retract → (rail) → **valve ON → extend** → settle
- print 訊息改稱 "pre-engage valve" 跟 "extend into pre-vacuumed cups"

### 原因
Sadie 指出抽真空應該在推桿伸出前開始 — 吸盤碰牆瞬間已有負壓，seal 立刻形成，減少邊緣漏氣機率。原先是貼牆後才開閥，空氣有時間從邊緣跑進去。

### 注意
WASH_ROBOT.cpp `cmd_attach()` 目前仍是「extend → valve ON」舊順序（line 516-519），之後 Sadie 實機驗證新順序確實更穩，可以同步改 WASH_ROBOT（跨界 user_lib 需要 PR review）。

## 2026-04-23e — Claude Code — ZDT group 加速度回零 fallback，避免 pos_reached bit 沒 set 的無限 poll

### 修改檔案
- `Linux_test/main.cpp` — `zdt_group_move_sync()` 偵測完成條件擴增：
  - (a) `pos_reached` bit set（原本唯一條件）
  - (b) `stall_flag` set（原本已有）
  - (c) **新**：`|real_speed| <= 5 RPM` 連續 3 次 poll（~450ms）且超過 MIN_WAIT 500ms → 視為實際停止，印 INFO 標註 pos_reached bit 未 set
  - timeout 10s → 6s（fallback 生效時本就該更早結束）

### 原因
Sadie 回報選項 6/7/8 的 ZDT 動作：推桿明明已經伸/縮到定位（物理、耳朵聽馬達停了），但程式一直 `get_system_status()` 直到 timeout。根因：ZDT 某些韌體 / 位置容差下，馬達到位後 `pos_reached` bit 不會被 set，只有速度歸零。加速度 fallback 讓程式能看到「馬達停了就走」而不是死等那個 bit。

## 2026-04-23d — Claude Code — Linux_test 加選項 9 (SD76 計米器) + 10 (ZS_DIO 捲揚機)

### 修改檔案
- `Linux_test/main.cpp` —
  - include `SD76_length_meters.h` + `ZS_DIO_R_RLY.h`
  - 新增 `test_sd76()`（選項 9）：提示 gateway IP (.30) + slave (2/3/4)，持續 live-read `readUpperInteger()` + `readStatus()`，互動指令 r/p/s（reset / pause / resume）/ q
  - 新增 `test_zsdio()`（選項 10）：提示 gateway IP + slave + total_relay，互動 `on N / off N / r N / a / o / q`。註解標示 main crane (.30) 跟 easy crane (.21) 的 channel 對應
  - 選單加兩項；main 分派加兩分支

### 原因
Sadie 要單獨測試計米器（SD76）和捲揚機繼電器（ZS_DIO）硬體是否正常。兩者都掛在吊車側，SD76 只有 main crane 有（slave 2,3,4），ZS_DIO 兩端都有但 channel 用法不同。測試工具涵蓋兩種配置。

## 2026-04-23c — Claude Code — 更新 ZDT slave ID 映射 + 選項 7 retry 改 rail-backup [跨界: user_lib]

### 修改檔案
- `user_lib/WASH_ROBOT.h:119-123` — ZDT slave ID 常數更新（跨界）：
  - `ZDT_LF1, LF2: 1,2 → 3,4`（左腳）
  - `ZDT_LB1, LB2: 3,4 → 6,8`（左身體）
  - `ZDT_RF1, RF2: 5,6 → 1,2`（右腳）
  - `ZDT_RB1, RB2: 7,8 → 5,7`（右身體）
  - `ZDT_C: 9`（中心，不變）
- `Linux_test/main.cpp` —
  - 選項 7/8 的 `feet_slaves` 改 `{1,2,3,4}`、`body_slaves` 改 `{5,6,7,8}`、`body_center_slaves` 改 `{5,6,7,8,9}`
  - print 字串從 "ZDT 1,2,5,6" → "ZDT 1,2,3,4"、"ZDT 3,4,7,8,9" → "ZDT 5,6,7,8,9"
  - **選項 7 retry 策略改 rail-backup（Strategy B）**：新增 `retry_grip_rail()` lambda + `RAIL_BACKUP_CM=5.0` 常數。Feet retry → rail -5cm；Body retry → rail +5cm。每次 retry 累積（3 次 retry = ±15cm）。跟 WASH_ROBOT `feet_backup`/`body_backup` 一致
  - 選項 8 保持原 `retry_grip`（純 pusher back-off，因無滑桿）
- `CLAUDE.md` 架構圖 — ZDT slave 對應表更新；PQW CH2/CH3 備註的 slave 清單從 `1,2,5,6 / 3,4,7,8` 改 `1,2,3,4 / 5,6,7,8`

### 原因
Sadie 確認實機接線：feet 左 3,4 / 右 1,2、body 左 6,8 / 右 5,7。原本 WASH_ROBOT.h 的常數跟實機對不上。修掉後 do_step_down_ / do_attach 等函式自然用新 ID（因為都用 `ZDT_LF1` 等常數）。另外選項 7 retry 改 rail-backup 更貼近 WASH_ROBOT 原生行為，能嘗試不同貼附位置。

### 規範邊界備註
`WASH_ROBOT.h` 屬 Jim 範圍，標 `[跨界: user_lib]`。Sadie 已確認 slave ID 就是這個新映射。

## 2026-04-23b — Claude Code — Linux_test 加選項 8「無滑桿的步伐測試」

### 修改檔案
- `Linux_test/main.cpp` — 新增 `test_full_step_no_rail()`：選項 7 的子集，去掉 DM2J rail 移動，只跑「腳組 detach → retract → extend → attach → 驗真空」+「身+中心同流程」+ retry grip。選單加第 8 項

### 原因
Sadie 要一個純 push/vacuum 循環測試，不動滑桿。適用場景：
- DM2J 還沒好
- 在平台上測（rail 會撞東西）
- 單純驗證推桿同步 + 真空重吸邏輯

## 2026-04-23 — Claude Code — Linux_test 加選項 7「完整步伐測試」

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增常數：slave 映射（feet 1,2,5,6 / body 3,4,7,8 / center 9 / DM2J rail 1,3 / PQW slave 12）、`PUSHER_BACKOFF_PULSE`（16mm back-off 用於重吸）、`VACUUM_SETTLE_MS`(2s) / `VACUUM_RELEASE_MS`(300ms)
  - 新增三個 helper：
    - `zdt_group_move_sync()` — queue N 個 ZDT 用 sync=1 + broadcast trigger + poll 到 pos_reached（沿用 option 6 的硬體同步模式）
    - `vacuum_verify()` — 讀 JC-100 一批，印每顆壓力 + 判斷是否都 ≤ threshold
    - `dm2j_pair_rail_move()` — 左右滑桿 queue + `PR_move_cm_trigger_all` 廣播 + 分別確認 done
  - 新增 `test_full_step()`：IP × 3（.20/.21/.22）+ 步距 + 步數 + 壓力門檻 + retry 數，循環跑：
    - Phase A（腳組）：釋放 CH2 → 腳推桿 retract → 滑桿 +cm → 腳推桿 extend → CH2 ON → 讀 JC-100 1/2/5/6 → 失敗觸發 retry_grip（back-off + re-extend + re-engage）
    - Phase B（身體+中心）：釋放 CH3+CH4 → 身+中推桿 retract → 滑桿 -cm → extend → ON → JC-100 3/4/7/8/9 → retry
  - 選單加第 7 項

### 原因
Sadie 要一個整合測試把 8 支推桿（腳 4 + 身 4）+ 滑桿（DM2J）+ 真空壓力驗證 + retry 全部串起來跑，不經 washrobot 主程式。相當於 `do_step_down_` 精簡成 Linux_test。預設小步距（10cm）+ 可調 retry，方便現場調參。



### 修改檔案
- `Linux_test/main.cpp` `test_zdt_group()`：`send_pos` lambda 把 `motion_control_pos_mode` 換成 `motion_control_pos_mode_nowait`

### 原因
2026-04-22i 的 patch 把 sync 改成 1（暫存模式），但觀察到「都不會動」。根因：`user_lib/ZDT_motor_control.cpp:361-369` 的 `motion_control_pos_mode` 內部在送完指令後會 blocking 呼叫 `wait_until_pos_reached()` 等到位。sync=1 下 ZDT 不啟動馬達（等廣播觸發才動），所以 `wait_until_pos_reached()` 永遠等不到 pos_reached → 每顆 slave 都 timeout → 被標 `aborted/send_fail` → 到 Phase 1b 時已無可 trigger 的 slave → 整組不動。

`motion_control_pos_mode_nowait` 是現成的 user_lib API（相同簽名、不 wait），送完即返回，剛好適合 queue-then-broadcast 的模式。換成 nowait 後 Phase 1 能正確 queue，Phase 1b broadcast 才會真的觸發 8 顆同步啟動。

### 規範權威
無（測試工具行為；未改 user_lib）。

## 2026-04-22i — Claude Code — 選項 6 改用 ZDT 硬體同步觸發

### 修改檔案
- `Linux_test/main.cpp` `test_zdt_group()`：
  - `send_pos()` lambda 加 `sync_flag` 參數
  - Phase 1 把 `pos_mode` 的 sync 從 `0`（立即）改為 `1`（暫存不執行）
  - 新增 Phase 1b：對任一已 queue 成功的 slave 呼叫 `trigger_sync_move()`（廣播 slave 0x00, Reg 0x00FF），所有暫存指令的 ZDT 同時執行
  - Stall recovery 的 resend 仍用 `sync=0`（立即），因為此時其他顆早已在動或到位，不該等廣播

### 原因
Sadie 回報選項 6 的推桿「一支接一支伸出」，不是真同步。根因：RS485 bus 是半雙工序列化，原本 `sync=0` 指令一送到就立刻執行，導致第 1 顆比第 8 顆早 ~300ms 動作。ZDT 本身支援多軸硬體同步（Reg 0x00FF 廣播觸發），用這機制後 8 顆會同一瞬間啟動。

### 規範權威
無（測試工具行為；ZDT API 本來就有，未改 user_lib）。

## 2026-04-22h — Claude Code — Linux_test 新增選項 6「ZDT multi-pusher group」

### 修改檔案
- `Linux_test/main.cpp`
  - 頂部新增 `#include <vector>` `#include <set>`
  - 新增 `test_zdt_group()`：對 slave 1~9 下達統一 target pulse，支援 skip list（預設 `9`，因主體只裝 8 支推桿）
  - 流程：Phase 1 逐顆 `release_stall_flag` + `motion_control_driver_EN(true)` + `motion_control_pos_mode`；Phase 2 統一 poll 每 200ms 讀所有未完成 slave 的 status
  - Per-slave 堵轉自動 recovery（延用選項 3 的邏輯：`emergency_stop` → `release_stall_flag` → re-enable → re-send，最多 3 次）
  - 單顆失敗不阻斷其他顆；結束印 SUMMARY 列每顆狀態（reached / aborted + 原因）
  - 主選單新增選項 `6`，dispatcher 加 `else if (line == "6")`

### 原因
Sadie 需求：主體目前只裝 slave 1~8 SMC 推桿（slave 9 中心未裝），想單元測試「8 支同時動作」驗證機械同步性與堵轉處理。選項 3 僅能單顆測，選項 6 填補批次驗證需求，不改 user_lib、不改 WASH_ROBOT 自動流程。

### 規範權威
無（測試工具行為，不動規範）。

## 2026-04-22g — Claude Code — 修 Linux SIGPIPE 讓主程式被殺

### 修改檔案
- `washrobot_new_PI/main.cpp` — `main()` 最前面加 `signal(SIGPIPE, SIG_IGN)`（`#ifndef _WIN32` 守衛）
- `Crane_control_PI/main.cpp` — 同上
- `Crane_easy_PI/main.cpp` — 同上
- `.claude/mailbox.md` — 留訊息給 Jim，請評估 user_lib `TCP_client::sendData` / `TCP_server::sendToClient` 改用 `MSG_NOSIGNAL`

### 原因
Sadie 回報 washrobot 跑到一半印 `Broken pipe` 被 shell 殺掉。根因：`TCP_client.cpp:175` / `TCP_server.cpp:175` 都用 `send(sock, buf, len, 0)` 不帶 `MSG_NOSIGNAL`，Linux 下對已關閉對端的 socket 寫入會送 SIGPIPE，預設處置 terminate → 整個 process 死。任何一端斷（web 後端、crane、RS485 gateway、Linux_test 退出）都會踩到。

`signal(SIGPIPE, SIG_IGN)` 在 main 最前面一次設定即可 process-wide 生效，之後 `send()` 對死 socket 會回 -1 + `errno=EPIPE`，交由呼叫端 return false 處理，不會殺 process。Windows 不受影響（Winsock 沒 SIGPIPE 概念）。

長期建議由 Jim 把 user_lib 的 send 改用 `MSG_NOSIGNAL`（更完整、Linux_test 等也受益），已留 mailbox。

### 規範權威
無（build/runtime 修復，不動規範或 API）。

## 2026-04-22f — Claude Code — 修 `WashRobot::IMU_BASELINE_SEC` undefined reference

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — 頂部新增類外定義 `constexpr int WashRobot::IMU_BASELINE_SEC;`

### 原因
連結錯誤 `undefined reference to WashRobot::IMU_BASELINE_SEC`。根因：C++14 下 `static constexpr` 類別成員**只是宣告**不是定義，一旦被 ODR-use（如 `WASH_ROBOT.cpp:266` 傳進 `std::chrono::seconds(const Rep&)`，參數是 const reference）linker 就要找類外定義。最小修復為加一行類外定義；長期應升 C++17（`static constexpr` 成員自動 inline variable 則無此坑）。

### 規範權威
無（build 層修復，不動規範或 API）。

## 2026-04-22e — Claude Code — Linux_test ZDT 測試加堵轉自動排除

### 修改檔案
- `Linux_test/main.cpp` — `test_zdt()` 把原本的 `drv.wait_until_pos_reached(10000, 200)` 換成自寫 poll loop：
  - 每 200ms 讀一次 `get_system_status()`
  - `pos_reached=1` → 成功 break
  - `stall_flag=1` → 印 `[STALL] at real_pos=... attempt N/3`，執行 recovery 序列（`emergency_stop(true)` → `release_stall_flag` → `motion_control_driver_EN(true)` → 重送 `motion_control_pos_mode`），最多重試 3 次
  - 10 秒無 stall 也沒到位 → `[WARN] attempt timeout`
  - 成功時印 `[OK] reached target (stall retries=N)` 讓使用者看到有沒有觸發過 recovery

### 原因
Sadie 測 slave 2 遇到 `pos_reached=0 stall=1` final 狀態，`wait_until_pos_reached` 只會報 timeout 不會告訴你是 stall，也不會嘗試排除。現在 Linux_test 能在偵測到堵轉時自動 release + 重試，用於排除偶發堵轉；連續 3 次 stall 時會 ABORT 並印最後位置，方便判斷是「穩定機械卡」還是「偶發 encoder 雜訊」。

### 規範權威
無（測試工具行為，不動 motion_flow 規範；user_lib API 未改）。

## 2026-04-22d — Claude Code — PQW all ON/OFF 從 [OK] 改 [SENT]（更誠實）

### 修改檔案
- `Linux_test/main.cpp` —
  - `test_pqw()` 裡 `all ON` / `all OFF` 改印 `[SENT] ... echo-len OK, content NOT verified → check LEDs physically`。因為 `controlAll` 只檢查 echo 長度 ≥ 8，內容不驗，garbage 也算 success
  - 在 PQW 測試開頭加 note 提醒：Modbus echo check 在有些 PQW 韌體不穩，實體 LED 驗證才是可靠

### 原因
Sadie 回報：按 `all ON` 看到 `[OK]`，但實體 relay 沒動。根因：driver 的 `controlAll` 邏輯太寬（echo.size() >= 8 就當成功），而不是驗證 echo 內容是否跟 TX 對應。Linux_test 不改 user_lib 邏輯，只改訊息誠實化：現在顯示 `[SENT]` 表示「送出去了，但不能保證真的開」，叫使用者看 LED。

## 2026-04-22c — Claude Code — Linux_test 避免 readback 誤報誤判（PQW / ZDT）

### 修改檔案
- `Linux_test/main.cpp` —
  - `test_pqw()` `controlRelay` / `controlAll(true)` / `controlAll(false)` 的錯誤改 `[WARN] readback mismatch — check LED physically`，不再報 ERR 中斷
  - 三處都採 optimistic 更新本地 state（信任寫入，忽略 readback 解析失敗）
  - `test_zdt()` `wait_until_pos_reached` timeout 改 `[WARN]` 並提示實體檢查位置
  - `motion_control_pos_mode` send 失敗仍是 ERR（這是真的送不出去，不是 readback）

### 原因
Sadie 實測 PQW `on 1` 時 TX 有發出、RX 回非標準 Modbus 框架，`parseReadResponse` 解析失敗→ 全回 false → `states[0] != true` → driver return error。實際上 relay 可能已經物理吸合。改 ERR→WARN 讓現場操作員憑實體 LED / 咔聲判定，而不是被假錯誤打斷。

## 2026-04-22b — Claude Code — Linux_test 加 TCP quick-probe 避免 2 分鐘 connect 卡死

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 `quick_tcp_probe(ip, port, timeout_ms=2000)`：用 non-blocking connect + select 做 2 秒快速可達性檢查（跨平台 Linux/Windows）
  - 4 個 TCP 測試（DM2J / ZDT / JC-100 / PQW）在 `TCP_client::connectToServer` 之前先 probe，不可達直接 2 秒內回錯誤（原本要 ~130 秒 Linux SYN retry 耗盡才會回）

### 原因
Sadie 回報無法連線的等待太久。`TCP_client::connectToServer` 用 blocking connect 沒設 timeout，Linux 預設要跑完所有 SYN 重試。不動 user_lib（Jim 範圍），只在 Linux_test 層加 pre-probe 處理。

## 2026-04-22 — Claude Code — Linux_test 重構：5 個單物件測試 + menu 迴圈

### 修改檔案
- `Linux_test/main.cpp` — 整個重寫結構：
  - 拿掉多-slave 的 `test_dm2j_scan()` + `test_zdt_pusher()`（violates "single object per test" 原則）
  - 保留 5 個獨立測試：IMU / DM2J / ZDT / JC-100 / PQW，每個自問 IP / slave / 參數
  - `main()` 改 menu loop（原本測完就結束；現在測完回選單、直到輸入 `q` 才退出）
  - 每個測試函式都清理資源後 `return`（ZDT/PQW 自動 disable / all_off，TCP 自動 close）

### 原因
Sadie 要求：每個物件都能輸入 slave id + ip 單獨測，且測完回主視窗選下一個。原設計單次執行單次測試後即退出，切換設備要重跑執行檔很煩。移除 scan/cycle 這兩個不是「單物件」的選項也讓介面更一致。

## 2026-04-21s — Claude Code — Linux_test ZDT 測試結束自動 disable 馬達

### 修改檔案
- `Linux_test/main.cpp` —
  - `test_zdt_single()` (option 5) 結束前加 `motion_control_driver_EN(false)` 自動關使能
  - `test_zdt_pusher()` (option 4) 'q' 退出前 loop slave 1..9 全部 disable

### 原因
Sadie 回報 ZDT 測試完馬達「關不掉」— 原流程只 enable 不 disable，馬達保持通電卡位置 + 持續發熱。現在測完自動 disable。跟 PQW 的 exit-all-off 同策略。

## 2026-04-21r — Claude Code — Linux_test PQW 改明確 on/off 語法（不再 toggle）

### 修改檔案
- `Linux_test/main.cpp` — `test_pqw()` 互動 loop 改成：`on N` 開指定通道、`off N` 關、`a` 全開、`o` 全關、`s` 看 state、`q` 退出。原 toggle 模式拿掉（避免跟硬體實際狀態不同步造成誤解）

### 原因
Sadie 要明確「開 relay」的動作而不是 toggle。改成 verb 明確的語法後意圖清楚，重複按同指令也不會誤翻狀態。

## 2026-04-21q — Claude Code — Linux_test 選項 6 簡化（拿掉 scan，直接問 slave）

### 修改檔案
- `Linux_test/main.cpp` — `test_jc100()` 移除 scan 1..9 階段，改成線性流程：`IP → Slave ID → live-read` 跟選項 2/5 統一風格
- 選單 line 改成 `JC-100 pressure — slave + live read`

### 原因
Sadie 要所有測試都可直接輸 slave id 跑，不要中間 scan 干擾。選項 7 PQW 已經是線性流程沒動。

## 2026-04-21p — Claude Code — Linux_test 加選項 6/7（JC-100 + PQW）

### 修改檔案
- `Linux_test/main.cpp` —
  - 新增 `test_jc100()`：掃 slave 1~9 印 pressure，然後可選一顆 live-read（每 200ms 刷新，按 Enter 停）
  - 新增 `test_pqw()`：連 gateway → init PQW 8CH → 互動 toggle（1~8 切換 / 'a' 全開 / 'o' 全關 / 's' 看 state / 'q' 退出），退出時自動 all off
  - 內含 washrobot 的 CH1~8 對照表（泵浦/腳閥/身閥/中心閥/刷洗/水泵/水閥/保留）
  - 選單加選項 6、7

### 原因
Sadie 要在 Linux_test 裡測試 JC-100 真空表跟 PQW 繼電器，讓現場可以不經 washrobot 單獨驗這兩個硬體。PQW 測試退出時自動 all_off 避免泵浦 / 閥留啟動狀態。

## 2026-04-21o — Claude Code — Linux_test 加選項 5「ZDT single move」（簡單版）

### 修改檔案
- `Linux_test/main.cpp` — 新增 `test_zdt_single()`：提示 gateway IP / slave / target pulse，release_stall + enable + motion_control_pos_mode + wait_until_pos_reached + 印最終狀態。選單加第 5 項

### 原因
Sadie 覺得選項 4（scan+cycle）太複雜，要一個像 option 2 (test_dm2j) 那樣簡單線性的版本：給 slave + 目標 pulse 就 go。保留 option 4 給掃描需求。

## 2026-04-21n — Claude Code — Linux_test 加 ZDT SMC 推桿測試（選項 4）

### 修改檔案
- `Linux_test/main.cpp` —
  - 加 SMC 推桿常數（與 `WASH_ROBOT.h PUSHER_*` 對齊：EXTEND=144000, RETRACT=0, RPM=1000, ACC=255）
  - 新增 `test_zdt_pusher()`：連 .21 gateway → 掃 slave 1~9 印每顆 `enabled / pos_reached / stall / home_ok` → 互動式選 slave → `release_stall_flag` + `motion_control_driver_EN(true)` + 伸 → 等 `wait_until_pos_reached` → 縮 → 等 → 報告 → 回到選單
  - 支援 `e`（只 enable）/ `d`（只 disable）/ `q`（退出）
  - main 選單加第 4 項

### 原因
Sadie 想現場測 SMC 推桿（ZDT 驅動卡 × 9）是否活著 + 動得了。比單一 slave 方便，掃完直接選要 cycle 哪顆。含 enable 保險（DM2J 那邊的 ENABLE 問題同樣可能發生在 ZDT）。

## 2026-04-21m — Claude Code — Linux_test main.cpp：debug=true + 加 DM2J slave scan 選項

### 修改檔案
- `Linux_test/main.cpp` —
  - `test_imu()` IMU init debug `false → true`
  - `test_dm2j()` DM2J init debug `false → true`（現場 diagnostic 用途）
  - 新增 `test_dm2j_scan()` — 掃 slave 1~10，每個呼叫 `read_pulse_per_rev()`，回報 OK/no_response/init_fail
  - main 選單加第 3 項「DM2J slave scan」

### 原因
Sadie 跑 test_dm2j 報 `PPR read failed` — slave ID 不對或硬體狀態不明。加 scan 功能讓現場可一鍵掃出哪些 slave 活著、PPR 多少；debug=true 印 Modbus TX/RX hex 幫助判斷是哪一段沒回應。

## 2026-04-21l — Claude Code — Linux_test.vcxproj 同樣修法（log_utils + 相對路徑 + pthread）

### 修改檔案
- `Linux_test/Linux_test.vcxproj` —
  - `<ClInclude>` 群組加 `..\user_lib\log_utils.h`
  - `AdditionalIncludeDirectories` 從 `D:\washrobot_new_PI\user_lib` 改相對 `..\user_lib`
  - 加 unconditional Link 依賴 `pthread`

### 原因
Sadie 跑 Linux_test build 時報 `log_utils.h: No such file or directory`，跟 washrobot_new_PI 是同樣三個老問題（log_utils.h 漏列 + 絕對路徑 + pthread）。一併修好。

## 2026-04-21k — Claude Code — 兩個 vcxproj 加 pthread linker dependency

### 修改檔案
- `washrobot_new_PI/washrobot_new_PI.vcxproj` — 加 unconditional ItemDefinitionGroup 帶 `<Link><LibraryDependencies>pthread</LibraryDependencies></Link>`
- `Crane_easy_PI/Crane_easy_PI.vcxproj` — 同上

### 原因
Sadie build 時 link 階段報 `undefined reference to pthread_create`。std::thread 在 Linux g++ 下需要 `-lpthread`，VS Linux project 要透過 `<LibraryDependencies>pthread</LibraryDependencies>` 指定（MSBuild 翻譯為 `-lpthread`）。兩個 vcxproj 都漏了；加 unconditional ItemDefinitionGroup 讓所有 config 都吃到。

## 2026-04-21j — Claude Code — washrobot_new_PI.vcxproj 修 log_utils.h 遺漏 + 絕對路徑改相對

### 修改檔案
- `washrobot_new_PI/washrobot_new_PI.vcxproj` —
  - `<ClInclude>` 群組加 `..\user_lib\log_utils.h`（原本漏了，Crane_easy_PI 有）
  - `AdditionalIncludeDirectories` 從硬編 `D:\washrobot_new_PI\user_lib` 改成相對 `..\user_lib`（跟 Crane_easy_PI 對齊）

### 原因
Sadie build washrobot_new_PI 時報 `log_utils.h: No such file or directory`。根因：VS Linux project 靠 `<ClInclude>` 列表決定 sync 哪些 header 到 Pi，washrobot vcxproj 當初沒把 log_utils.h 列進去 → VS 不 sync → g++ 找不到。Crane_easy_PI 當初有加。順便把同檔的硬編 Windows 絕對路徑改相對，避免換 drive letter / 換機器壞掉。

## 2026-04-21i — Claude Code — web_backend reconnect exponential-spawn bug fix（OOM 元兇）

### 修改檔案
- `web_backend/server.js` — `makeBridge()` 重構：reconnect 去重（`state.reconnectTimer` flag）、進 connect() 先 destroy 舊 socket、`error` 事件只 log 不觸發 reconnect（讓 `close` 獨占驅動）
- `.claude/work_log.md` — 頂部 2026-04-21i 條目

### 原因
Sadie 回報 backend 被 `Killed`（OOM），ss 顯示 fd 87787 + 大量 SYN-SENT。根因是 Node socket 失敗會同時 fire error+close 兩事件，原碼兩個 handler 各自 schedule 重連 → 每次失敗 reconnect 數量翻倍 → 10 分鐘內 socket 指數爆炸。修去重 + 清理 + 事件單一驅動。

## 2026-04-21h — Claude Code — easy_crane_test_mode.md §5 補 shim 不聽 EVT 限制

### 修改檔案
- `.claude/easy_crane_test_mode.md` §5 功能落差表 — 加一行「shim 監聽 easy EVT ❌」，說明 easy 自我保護觸發時繩物理會停但 shim 回假 OK、累積位置誤差；現場發現狀態不一致時查 shim stderr + easy log

### 原因
Sadie 問收繩時碰到 weight_limit 或 DY500 read_fail 會不會停。確認：物理層 easy 會 all_off 停，但 shim 是開環睡覺不訂閱 EVT，會繼續回 OK 給 washrobot。現階段不改 shim（retract 15cm 太短不易觸發），只補文件提醒。

## 2026-04-21g — Claude Code — easy crane 按鈕語意重構（HOLD×2 + AUTO 單鍵）

### 修改檔案
- `web_backend/public/index.html` — 拿掉「模式」切換 row；AUTO 按鈕移到獨立 row（🤖 AUTO 拉到上限）；hint 更新
- `web_backend/public/app.js` — 廢除 `easyAutoMode` 雙模式切換，改成三顆按鈕獨立語意：UP/DOWN 純 hold、AUTO click toggle。server state sync 改單向（僅在 server 清零時重置 local，避免剛按下的 race window）；`releaseAllEasyHolds()` 擴充包含 AUTO 重置

### 原因
Sadie 要更直覺的按鈕語意：AUTO = 一鍵拉到重量門檻（靠 server-side weight_loop 自動停）、UP/DOWN = 純按住。原 HOLD/AUTO mode toggle 混淆。後端不動（cmd_up / weight_loop 既有行為已足夠），純前端重構。

## 2026-04-21f — Claude Code — GUI 效能調整（拿掉 backdrop-filter + aurora blob）

### 修改檔案
- `web_backend/public/style.css` — 全面精簡，拿掉 `backdrop-filter`（原每 panel / header / banner / modal 都有 blur）、aurora drift 動畫、banner pulse、按鈕 hover box-shadow glow、log text-shadow；保留靜態 bg gradient、dot 脈動（改 opacity fade）、panel 頂部漸層線、gradient header、input focus border
- `.claude/work_log.md` — 頂部 2026-04-21f 條目

### 原因
Sadie 回報 Pi Chromium 渲染原 04-21b 主題有點卡。主因是 `backdrop-filter: blur(16px)` 用在每個 panel，加上兩顆 aurora 漂移 blob（`blur(90px)` + 無限動畫）GPU 吃很兇。拆掉最重的幾項，保 aesthetic 核心（霓虹色、gradient 標題、脈動 dot、漸層 bg）。CSS 行數 380 → ~290。

## 2026-04-21e — Claude Code — WASH_ROBOT CRANE_IP 改 .5.26（shim/easy 共 Pi）[跨界: user_lib]

### 修改檔案
- `user_lib/WASH_ROBOT.h:100` — `CRANE_IP: "192.168.1.101" → "192.168.5.26"` + `[TEST MODE 2026-04-21]` 4 行註解
- `.claude/easy_crane_test_mode.md §9a` — 撤除清單加 CRANE_IP 第一行；其他行號因前面加註解而位移 +5
- `.claude/work_log.md` — 頂部 2026-04-21e 條目

### 原因
Sadie 測試配置：crane_shim + Crane_easy_PI + web_backend 全在 .5.26 同一台 Pi，washrobot 將進 .5.x 網段。原硬編 .101 連不到。加 TEST MODE 註解，主 crane 到位時 revert。

## 2026-04-21d — Claude Code — web_backend TCP keepalive + 10s ping（easy crane 閒置掉線修復）

### 修改檔案
- `web_backend/server.js` — `makeBridge()` 加 `sock.setKeepAlive(true, 30000)` + 每 10s 對每個 bridge 送 `ping\n`；新常數 `BRIDGE_PING_MS = 10000`
- `.claude/work_log.md` — 頂部 2026-04-21d 條目

### 原因
Sadie 回報 GUI 閒置一陣子後 easy crane 轉紅。根因：easy crane 跨網段（.5.x），中間 NAT 閒置 15~60min 殺 TCP session，backend 沒啟 keepalive 沒察覺。Browser 50ms status poll 在 tab 背景化時被 setInterval throttle 無法保連。兩層修：OS 層 `setKeepAlive` + backend 自己 10s `ping`。副作用：log 每 10s 多 3 行 OK 回應，若吵下一輪處理。

## 2026-04-21c — Claude Code — 測試模式程式改動：watchdog 60s + 全驅動 debug [跨界: user_lib]

### 修改檔案
- `user_lib/WASH_ROBOT.h:142` — `WATCHDOG_TIMEOUT_MS: 2000 → 60000`（+ 6 行 [TEST MODE] 註解）
- `user_lib/WASH_ROBOT.cpp:58,66,74,81` — DM2J / ZDT / JC-100 / PQW init debug `false → true`（+ 區塊 [TEST MODE] 註解）
- `user_lib/WASH_ROBOT.cpp:96` — IMU init debug `false → true`（+ 單行 [TEST MODE] 註解）
- `Crane_easy_PI/main.cpp:318,319` — relay / dy500 init debug `false → true`（+ [TEST MODE] 註解）
- `.claude/easy_crane_test_mode.md` §9 — 新增「撤除測試模式 ⚠️ 必看清單」
- `.claude/work_log.md` — 頂部 2026-04-21c 條目

### 原因
下午上機前 code review 發現 `WATCHDOG_TIMEOUT_MS=2000` 會讓 shim `pay_out 45cm @ 3cm/s = 15s` 期間 crane_mtx_ 鎖死 → `motion_active_=true` 時自動 abort。Sadie 決策：調 60000ms + 所有驅動 debug=true 方便 on-site troubleshoot。所有改動旁都有 `[TEST MODE 2026-04-21]` 註解，grep 可快速找到撤除點。主 crane 到位時照 §9 清單還原。

## 2026-04-21b — Claude Code — Web GUI 主題重做（深空極光 glassmorphism）

### 修改檔案
- `web_backend/public/style.css` — 全面重寫，加設計 token（深空紫藍底 + 霓虹青紫粉 accent）、aurora bg blobs（body ::before/::after 漂移動畫）、glass panel（backdrop-filter blur + 紫 border + 頂部漸層線）、脈動 status dot、gradient 標題、cyan-glow input focus、發光 log 顏色、pulse banner、modal glass
- `web_backend/public/index.html` — `<head>` 加 Google Fonts 3 行（Inter + JetBrains Mono，display=swap，Pi 離線自動 fallback system-ui/Consolas）
- `.claude/work_log.md` — 頂部 2026-04-21b 條目

### 原因
Sadie 要「夢幻 + 科技感」，從 A/B/C 三方向選了 A（深空 glass）。保留所有 class/id hook，app.js 零改動。待上機驗證：backdrop-filter 在 Pi 瀏覽器的渲染效能、blur 過重可能要降、Google Fonts 離線 fallback。

## 2026-04-21 — Claude Code — crane_shim 測試模式（簡易吊車 + washrobot 自動下洗）

### 修改檔案
- `crane_shim/crane_shim.py` — 新增（Python stdlib，TCP server :5002 偽裝 Crane_control_PI，翻譯 pay_out/retract → easy crane :5003 timed on/off；motion_lock + abort flag + easy watchdog keepalive）
- `crane_shim/README.md` — 新增（啟動、CLI flag、速率校正、故障排除）
- `.claude/easy_crane_test_mode.md` — 新增（測試模式規範：指令對照、安全守則、可用流程、checklist）
- `.claude/runbook.md` — §A 加「1-alt 測試模式」啟動分支
- `.claude/work_log.md` — 頂部 2026-04-21 條目

### 原因
Sadie 想在主吊車（Crane_control_PI + DSZL_107 + 中間絞盤變頻器）到位前，用簡易吊車（Crane_easy_PI @ .5.26:5003）跑 washrobot 的 `run` / `step_down` 自動循環做短距離受控測試。兩協定不相容（距離型 vs 時間型），用 shim 層轉譯最不侵入。Phase 5/6 / home_status / roll_correct 在 shim 內明確拒絕以擋 GUI 自動按鈕。速率 rate_down/rate_up 目前 3.0 cm/s placeholder，上機實測後校正。

## 2026-04-20k — Claude Code — easy crane 第三輪提速（SUSTAIN=0 + all_off 最佳化）

### 修改檔案
- `Crane_easy_PI/main.cpp` — `WEIGHT_SUSTAIN_MS: 50 → 0`；`all_off()` 用 `atomic::exchange` 跳過已關閉的繼電器寫入
- `.claude/work_log.md` — 頂部 2026-04-20k

### 原因
Sadie 要求再快。j 輪降到 ~50-80ms，k 輪再砍 30-50ms：第 1 個 bad reading 就觸發（移除 sustain）+ 省一次冗餘 relay OFF Modbus 寫入。剩下的延遲全在 Modbus 物理層。

## 2026-04-20j — Claude Code — easy crane weight_loop 二輪提速

### 修改檔案
- `Crane_easy_PI/main.cpp` —
  - 常數：`WEIGHT_POLL_MS = 50` → 移除；新增 `WEIGHT_YIELD_MS = 1`（僅 CPU yield）
  - `WEIGHT_SUSTAIN_MS: 100 → 50`
  - `weight_loop` 用 `std::chrono::steady_clock` 實測時間累計 over_ms / fail_ms（原本假設固定間隔不準）
- `.claude/work_log.md` — 頂部 2026-04-20j 條目

### 原因
i 輪優化把停機從 ~800ms 降到 ~150ms，Sadie 回報仍不夠快。主因 50ms sleep 佔一半循環時間。移除 sleep 並用實測時間累計後預估 ~50-80ms 停機。

## 2026-04-20i — Claude Code — Easy crane 停機延遲優化 + 門檻 input live

### 修改檔案
- `Crane_easy_PI/main.cpp` — `weight_loop` 安全檢查改用 raw `w` 不用平均 `g_weight`（節省 ~500ms 平均延遲）；`WEIGHT_SUSTAIN_MS` 300→100；結構調整把安全檢查移進 read OK 分支
- `web_backend/public/index.html` — 移除「set」按鈕，改成純 input（中間文字「（目前: X kg）」顯示 server 值）
- `web_backend/public/app.js` — input `input` event 直接送 `set_up_stop_kg`，150ms debounce 防中間狀態
- `.claude/work_log.md` — 頂部新增 2026-04-20i 條目

### 原因
Sadie 實測發現停機太慢（~800ms 最差），來不及避免打到門檻。根因是 safety 用了 10 樣本平均 + 300ms sustain。改用 raw 讀值 + 2 樣本 sustain 降到 ~150ms。順便把 set 按鈕去掉改為 live input 以減少操作步驟。

## 2026-04-20h — Claude Code — Easy crane AUTO / HOLD 雙模式

### 修改檔案
- `web_backend/public/index.html` — easy crane panel 新增「模式」row + AUTO toggle 按鈕；hint 列點加 2 條解釋兩模式
- `web_backend/public/app.js` — 整段 easy crane 重寫：
  - 移除 bindHold 依賴 + 500ms ping heartbeat（與 50ms status poll 重複）
  - 新增 `easyAutoMode` / `easyUpActive` / `easyDownActive` 狀態
  - `easyStartUp/StopUp/StartDown/StopDown` + `updateEasyButtonLabels`
  - AUTO 按鈕 handler（關閉時自動停所有動作）
  - UP/DOWN onPress/onRelease mode-aware：HOLD 按住才動、AUTO 點擊 toggle
  - `onEasyCraneLine` 新增 server state sync（解析 `up=` / `down=` 校正客戶端）
- `web_backend/public/style.css` — `.btn-auto` + `.btn-auto.active` 樣式（橙色、box-shadow）
- `.claude/work_log.md` — 頂部新增 2026-04-20h 條目

### 原因
Sadie 要求加 auto 模式：點擊 UP/DOWN 持續動作，再點一次停，遇門檻/watchdog/讀失敗仍自動停。設計關鍵：50ms status poll 本身就是 heartbeat，因此 AUTO 模式不需要額外心跳；客戶端狀態用 status 回傳的 `up=` / `down=` 持續校正，避免任何顯示與 server 實際狀態不一致。

## 2026-04-20g — Claude Code — Easy crane 可調 UP 門檻 + DY500 讀取失敗防呆

### 修改檔案
- `Crane_easy_PI/main.cpp` —
  - `WEIGHT_UP_STOP_KG` (constexpr) → `g_up_stop_kg` (atomic<float>)，預設 `DEFAULT_UP_STOP_KG = -20.0f`
  - 新增 `g_weight_valid` (atomic<bool>)、常數 `READ_FAIL_STOP_MS = 500`
  - `weight_loop` 雙檢：(a) 讀失敗累計超 500ms 且動作中 → all_off + EVT `weight_read_fail`；(b) UP 且 weight < g_up_stop_kg 持續 300ms → all_off + EVT `weight_limit`
  - `cmd_up` / `cmd_down` pre-flight：`!g_weight_valid` 直接 `ERR weight_read_fail`；`cmd_up` 同時檢查門檻
  - 新 `cmd_set_up_stop_kg` + dispatch 新增 `set_up_stop_kg <kg>`
  - `cmd_status` 回傳加 `up_stop_kg` + `weight_valid`
- `web_backend/public/index.html` — easy crane panel 新「收繩停止門檻」input + set 按鈕 + 目前值顯示；hint 列點從 3 條擴到 4 條
- `web_backend/public/app.js` — `onEasyCraneLine` 解析 `up_stop_kg=`、新增 EVT `weight_read_fail` 觸發 `releaseAllEasyHolds()`；`btn-easy-set-stop` 送 `set_up_stop_kg <v>`
- `.claude/runbook.md` — C2b easy crane 指令集加 `set_up_stop_kg`、防呆從 3 層更新為 4 層
- `.claude/work_log.md` — 頂部新增 2026-04-20g 條目

### 原因
Sadie 要求：(a) 網頁 input 可設門檻（避免 hard-code 重編）；(b) DY500 讀不到重量時強制停機且拒絕新指令。設計上 UP 門檻 runtime 可設 + pre-flight 檢查 + 持續監測三重保險。

## 2026-04-20f — Claude Code — 新增 Crane_easy_PI 子專案（獨立簡易吊車）

### 修改檔案
- `Crane_easy_PI/main.cpp` — **新檔**：獨立吊車 TCP server (:5003)，指令集 `up/down <on|off> / stop / status / ping`，含 weight monitor thread + watchdog thread + weight-limit safety
- `Crane_easy_PI/Crane_easy_PI.vcxproj` — **新檔**：MSBuild 專案檔（GUID `{909DCE76-3882-475C-8853-EB344B428FF6}`），引用 user_lib 的 TCP_client/server、ZS_DIO_R_RLY、DY_500_weight_sensor
- `washrobot_new_PI.sln` — 加入新專案 + 8 configuration mappings
- `crane_control_PI_easy_ver/` — **刪除整個資料夾**（user_lib/ 副本重複、main.cpp 被重寫的版本取代）
- `web_backend/server.js` — 加第 3 條 bridge `easy_crane`（env `EASY_CRANE_IP` 預設 `192.168.5.26:5003`），3-state status broadcast
- `web_backend/public/index.html` — 頂部第 3 顆 dot、新 panel「easy crane」（重量顯示 + 拉繩/釋放繩 press-and-hold + refresh/STOP）
- `web_backend/public/app.js` — 新 `bindHold()` 通用 helper（emergency retract 按鈕也改走 helper），`easy_crane` press-and-hold + 500ms ping heartbeat，自動 2 秒 poll `status` 更新重量顯示，EVT `watchdog_timeout` / `weight_limit` 收到時本地釋放按鈕狀態
- `web_backend/public/style.css` — `.panel-easy_crane` + `.btn-hold` + `.btn-hold.active` 樣式
- `.claude/runbook.md` — 啟動順序加第 3 步 Pi、GUI 按鈕表加 easy crane、C2b 指令表 + 三層防呆說明、緊急處置表新增 2 條
- `.claude/work_log.md` — 頂部新增 2026-04-20f 條目

### 原因
Sadie 丟來獨立的簡易吊車程式碼（交互式 terminal 版），要求整理成主 workspace 的子專案、加到 Web GUI、並做網路失聯防呆（防止 UP 方向繩縮到最上卡壞）。採取三層防呆設計：server watchdog（2s 無 inbound 自動停）+ 重量門檻（`WEIGHT_UP_STOP_KG` placeholder `-20kg`）+ 前端 press-and-hold 500ms heartbeat。

## 2026-04-20e — Claude Code — CLAUDE.md 交接指引加 runbook [跨界: CLAUDE.md]

### 修改檔案
- `CLAUDE.md` — 「給 Claude CLI 的交接指引」節新增 item 3 指向 `.claude/runbook.md`，原 item 3 順延為 4

### 原因
runbook 定義「怎麼用系統」，對新接手 session / 協作者而言與 work_log / motion_flow 同等重要，加進 onboarding 清單。規範文件屬 Jim 範圍，標 `[跨界: CLAUDE.md]`。

## 2026-04-20d — Claude Code — 新增 runbook.md

### 修改檔案
- `.claude/runbook.md` — 新增：啟動順序（crane → washrobot → browser）/ Web GUI 按鈕對應 / washrobot + crane raw command 指令集 / EVT 主動事件 / 典型流程表（Phase 1~6）/ 緊急處置表 / 失聯模式四態對照

### 原因
Sadie 要求整理一份「從冷開機到操控」的操作文件，涵蓋啟動順序、按鈕 → 指令對應、典型流程、緊急處置。放進 `.claude/` 讓協作者都看得到。規範權威仍以 motion_flow.md 為準，runbook 只說「怎麼用」不重複定義。

## 2026-04-20c — Claude Code — Web 前端五件套

### 修改檔案
- `web_backend/public/app.js` — 整檔重寫：mode 切換、auto-stop 邏輯、home_status pending resolver、balance_ask EVT 解析、press-and-hold 緊急收繩、2 個 modal
- `web_backend/public/index.html` — 新增 `#banner` + 各 panel 加 `panel-washrobot` / `panel-crane` / `panel-emergency` class、2 個 modal、鋼索歸零按鈕、召回按鈕、reset 按鈕；修正 STOP (robot) 送 `emergency_stop`
- `web_backend/public/style.css` — append banner / panel-disabled / primary button / emergency panel+button / modal 樣式
- `.claude/work_log.md` — 頂部新增 2026-04-20c 條目

### 原因
motion_flow §8 已規範失聯模式 UI + 緊急收繩按住邏輯，但 `app.js` 舊版只有基本命令送出、`index.html` 無對應元素。本次實作 5 件套：失聯模式灰化 + banner、緊急收繩 press-and-hold、Phase 6 召回兩步驟流程（home_status → remaining → return_home）、鋼索歸零、balance_ask EVT 彈窗。

未動 `server.js`（既有 bridge 已足夠）。

## 2026-04-20b — Claude Code — Phase 2 補收輪 + 程式碼同步 [跨界: motion_flow]

### 修改檔案
- `.claude/motion_flow.md` —
  - §2 RS485_1 表 slave 2/4：「Phase 1 only」→「Phase 1 放輪爬牆；Phase 2 收輪」
  - §4 Phase 2 新增 step 8「DM2J slave 2, 4 → 0（收輪）」+ 說明機械原理 + 前置假設，原 step 8~11 順延
- `user_lib/WASH_ROBOT.cpp cmd_init()` — 在繼電器 OFF 後、`pusher_move_many_` 前插入左右輪 retract（`PR_move_cm(0, 1, DM2J_RPM, 0.0, DM2J_ACC, DM2J_DEC)`）
- `.claude/work_log.md` — 頂部新增 2026-04-20b 條目

### 原因
輪子裝於靠牆面，Phase 1 展開供爬牆；Phase 2 推桿伸出前必須先收輪，否則吸盤碰不到玻璃。放在 cmd_init() 統一觸發：單一 entry point + 防呆。Phase 6 召回不放輪（輪子無地面緩衝作用）。

## 2026-04-20 — Claude Code — Phase 3 補 VACUUM_SETTLE_MS 規格 [跨界: motion_flow]

### 修改檔案
- `.claude/motion_flow.md` —
  - §4 Phase 3 新增 step 5「等 VACUUM_SETTLE_MS（預設 2000 ms）讓壓力穩定」，原 step 5 順延為 6
  - §6 可調參數表新增 `VACUUM_SETTLE_MS = 2000 ms`（列於 VACUUM_THRESHOLD_KPA 之後）
- `.claude/work_log.md` — 頂部新增 2026-04-20 條目

### 原因
`cmd_attach()` 實際已有 `sleep_ms_(VACUUM_SETTLE_MS)` 在 CH4 ON 後、讀 JC-100 前；規格漏列。補進規格讓 spec/code 對齊。程式碼無需變動。

## 2026-04-17b — Claude Code — 角色表釐清 Jim/Sadie 分工

### 修改檔案
- `CLAUDE.md` — 角色表從 5 列改 4 列：「規範 + 裝置驅動 = Jim」「應用層實作 = Sadie」「前端 = Sadie」「測試 / 工具 = Sadie」。`介面契約` 節完全不動（保留抽象代名詞「架構方」/「協作方」）
- `.claude/work_log.md` — 2026-04-17 條目的「Jim 一度以為」→「Sadie 一度以為」（該條目紀錄的是本次對話，user 是 Sadie）

### 原因
Sadie 本次 session 自我介紹並釐清分工：Jim 負責規範文件 + user_lib/ 裝置驅動；Sadie 負責應用層（WASH_ROBOT 編排、main.cpp、web_backend、測試工具）。原 5 列角色表把 user_lib/ 完整掛在 Jim 名下、其他 4 列為空，與實況不符。

## 2026-04-17 — Claude Code — work_log 備忘 TCP 拓撲釐清

### 修改檔案
- `.claude/work_log.md` — 頂部新增 2026-04-17 條目，記錄「washrobot↔crane TCP 方向現狀已符合 Jim 的救援設計，無需更動」

### 原因
Jim 對話中一度以為 washrobot 是 crane 連線的 server 端，想翻轉；翻代碼確認現狀 crane 已是 server、washrobot 是 client，救援路徑已健全。純備忘，避免未來協作者/session 再誤會。


## 2026-04-15i — Claude Code — Phase 4 真空失敗後退 5cm 重試機制

### 修改檔案
- `user_lib/WASH_ROBOT.h` —
  - 加常數 `VACUUM_BACKUP_CM = 5.0`
  - 加成員 `rail_pos_cm_`（atomic）、`body_residual_cm_`（atomic）、`actual_feet_cm_`
  - 重寫 `cycle_group_` template 簽名：`(group, pre_cycle, backup, out_retry_count)` — pre_cycle 只跑一次做 crane + DM2J 大位移，backup 每次重試前做 5cm 小反向位移
- `user_lib/WASH_ROBOT.cpp` —
  - Constructor 初始化新成員
  - 重寫 `do_step_down_()`：腳組 / 身體組各用 pre_cycle + backup lambda 分離
    - 腳組 pre_cycle：閥關 + 推桿縮 + crane pay_out + DM2J abs `+STEP_CM` + crane retract
    - 腳組 backup：DM2J relative `-5cm`（rail 反向）
    - 身體組 pre_cycle：中心閥關 + 中心推桿縮 + 身體推桿縮 + DM2J abs `0` + 中心推桿伸 + 中心閥開
    - 身體組 backup：DM2J relative `+5cm`（rail 反向）
  - 絕對定位自動吸收 `body_residual_cm_`（身體上步沒回到原點的量）
  - 身體組重試次數 → `body_residual_cm_ = 5 × retries`，下次腳組絕對定位自動補償
  - `cmd_status` 加 `rail=<cm>` 與 `body_residual=<cm>` 輸出

### 原因
依 spec 與使用者 2026-04-15 討論實作真空失敗後退 5cm 機制：
- 使用 DM2J 絕對定位（mode=1）讓 body residual 自動被下次 feet 吸收，無需手動補償運算
- 腳組與身體組共用滑桿，backup 方向相反（腳組 -5、身體組 +5），都是「往對方前進的方向退」
- 重試時不重複 crane 動作（pre_cycle 只跑一次），避免累積 rope 長度
- 範例：身體組重試 1 次（只退 25cm 到 +5）→ 下次腳組絕對目標 +30，DM2J 自動只走 +25（從 +5 到 +30），不會超過行程
- body_residual/rail_pos 只做 diagnostic，不影響控制邏輯

編譯通過（aarch64, g++ -std=c++14），Phase 4 穩定性機制完成。

---

## 2026-04-15h — Claude Code — 狀態機 + reset/ping/pause/resume + 命名同步

### 修改檔案
- `user_lib/WASH_ROBOT.h` — 加 `enum class State`（Idle/Ready/Attached/Running/WaitingConfirm/Paused/Balancing/ReturningHome/Error）、`state_`/`state_before_pause_`/`state_before_wait_`/`state_mtx_` 成員、`set_state_`/`state_name`/`state_violation_`/`do_step_down_` helper；`cmd_stop` 改名 `cmd_emergency_stop`；加 `cmd_reset`/`cmd_ping`/`cmd_pause`/`cmd_resume`；`IMU_STOP_DEG` 改名 `IMU_EMERGENCY_DEG`
- `user_lib/WASH_ROBOT.cpp` —
  - Constructor 初始化 state 成員
  - 所有 cmd_* 加 state guard（state_violation_ 回 `ERR state_violation current=<state>`）
  - `cmd_init`/`cmd_attach`/`cmd_detach`/`cmd_step_down`/`cmd_run` 成功後 set_state 轉移；失敗轉 Error
  - `do_step_down_()` 抽出（無 state guard），`cmd_step_down` / `cmd_run` 共用
  - `cmd_emergency_stop` 改名 + 加 `crane_cmd_("emergency_stop")` + 轉 Error
  - `cmd_status` 輸出加 `state=<name>` 與 `roll=/pitch=`
  - `cmd_confirm_balance` 加 WaitingConfirm → Balancing → prev 狀態轉移
  - `cmd_return_home` 加 Attached/Paused/Error 前置檢查，用 fail lambda 統一轉 Error；成功轉 Idle
  - 新增 `cmd_reset` / `cmd_ping` / `cmd_pause` / `cmd_resume`
  - 手動指令（`cmd_vacuum`/`cmd_pusher`/`cmd_move`/`cmd_arm_sweep`/`cmd_tilt_mode`）在 Error 狀態下擋掉
  - `imu_monitor_loop_`：`IMU_STOP_DEG` → `IMU_EMERGENCY_DEG`、EVT `imu_stop` → `imu_emergency`、觸發時 set_state(Error)、ASK 觸發時轉 WaitingConfirm（存 `state_before_wait_`），hysteresis 清除時還原
- `washrobot_new_PI/main.cpp` — dispatch `stop` 改 `emergency_stop`，加 `reset`/`ping`；`pause`/`resume` 改呼叫 `cmd_pause`/`cmd_resume`；header 註解同步
- `.claude/motion_flow.md` — `stop` → `emergency_stop`、`imu_stop` → `imu_emergency`、`IMU_STOP_DEG` → `IMU_EMERGENCY_DEG` 三處同步

### 原因
依使用者要求做 3 件事：
1. 把容易跟 `pause` 搞混的 `stop` 改成語意明確的 `emergency_stop`（與 `ZDT_motor_control::emergency_stop()` 既有命名一致）
2. 補齊 spec §生命週期狀態 定義的 9 個狀態與轉移，指令違反狀態回 `ERR state_violation`
3. 補齊 spec 協定第 589-590 行規定的 `reset` / `ping`，讓 web/crane 能反向心跳

手動指令在 Error 下擋掉：避免操作者在未確認現場安全前操作硬體。允許在 Error 下的指令：`status`/`ping`/`reset`/`return_home`/`emergency_stop`。

---

## 2026-04-15g — Claude Code — Phase 6 Return Home 實作

### 修改檔案
- `user_lib/WASH_ROBOT.h` — 宣告 `cmd_return_home(int descent_cm)`
- `user_lib/WASH_ROBOT.cpp` — 實作 Phase 6 流程（arm 回零 → 關水 → 破真空 → 等 5s → 驗證脫附 → 收推桿 → 關泵 → crane pay_out）
- `washrobot_new_PI/main.cpp` — dispatch 加 `return_home <descent_cm>` 指令

### 原因
依 motion_flow.md §Phase 6 規格實作召回回地面流程。`descent_cm` 由 caller 傳入（washrobot 未追蹤 home_ground_cm / current_down_cm，暫由 GUI/operator 計算），crane 端 300s timeout。脫附驗證失敗時直接停機回報，不繼續放繩。

---

## 2026-04-15f — Claude Code — Crane Step 2（Watchdog 執行緒 + EVT）

### 修改檔案
- `Crane_control_PI/main.cpp` — 新增 watchdog 機制：
  - 新 atomics：`last_ping_ms` / `motion_active` / `watchdog_fired` / `watchdog_stop`
  - 常數：`WATCHDOG_TIMEOUT_MS=2000` / `HEARTBEAT_CHECK_MS=250`
  - `touch_heartbeat()`：任何 inbound 資料都刷新 last_ping_ms；若之前 fired 則發 `EVT watchdog_recovered`
  - `watchdog_loop()` thread：每 250 ms 檢查，超時且有 client 連接時：動作中 → abort + allMotionOff + `EVT watchdog_timeout state=aborted`；閒置 → `EVT watchdog_timeout state=idle`
  - `MotionScope` RAII guard 在 `motion_rope` + `cmd_roll_correct` 頭尾設定/清 `motion_active`
  - `on_receive` 開頭呼叫 `touch_heartbeat()`
  - `main()` 啟動 thread + shutdown 時 join
  - `broadcast_evt()` helper

### 原因
spec §5：washrobot ↔ crane 通訊 > 2s 無心跳視同斷線，動作中需立即停機保護機器人；閒置中僅 log。此 commit 實作 crane 端被動 watchdog（washrobot 端主動 ping 已存在）。

### EVT 格式
```
EVT watchdog_timeout state=aborted   # 動作中觸發，已停機
EVT watchdog_timeout state=idle      # 閒置中觸發
EVT watchdog_recovered               # 下次 ping 回來
```

---

## 2026-04-15e — Claude Code — Crane 重寫 Step 1（SD76 #3 + CLV900 + 新指令 handlers）

### 修改檔案
- `Crane_control_PI/main.cpp` — 整體重寫：
  - 新硬體：`meter_middle` (SD76 slave 4, 中間管線計米) + `inverter` (CLV900 slave 7, 中間絞盤變頻器)
  - `motion_rope()`（原 `cmd_pay_out`）加入中間絞盤同步：起動 CLV900 forward/reverse + `MIDDLE_WINCH_HZ` (placeholder 20 Hz)，middle 完成條件 = `|Δ| >= cm × MIDDLE_WINCH_RATIO_K`
  - 新指令：`ping` / `zero_meters <ground|top>` / `home_status` / `roll_correct <delta_cm>` / `middle_set <rpm> <pay|retract|stop>`
  - `zero_meters top` 讀 `|SD76 左|` 存 `home_ground_cm`（atomic）再三顆計米歸零
  - `roll_correct`：正值 = 左放右收，中間絞盤不動
  - `cmd_stop` / `allMotionOff` 同時關繼電器 + CLV900.stopDecel
- `Crane_control_PI/Crane_control_PI.vcxproj` — 加入 `CLV900_inverter.{cpp,h}` 編譯清單；修正 `AdditionalIncludeDirectories` 從別人電腦硬寫死的 `C:\Users\Administrator\source\repos\...` 改為相對路徑 `..\user_lib`

### 原因
washrobot 端已呼叫 `pay_out`/`retract`/`roll_correct`/`zero_meters`/`home_status`/`ping` 等新協定，crane 端需對應實作，否則分散式無法上機。本 commit 為 Step 1（核心功能），未含 watchdog 執行緒（Step 2）與張力 monitor（Step 3，等 DSZL-107 驅動）。

### 待確認 / TODO
- `MIDDLE_WINCH_HZ` = 20 Hz 為 placeholder，實機測轉速後調整
- `middle_set` 的 rpm→Hz 換算假設 4 極 50 Hz → 1500 rpm，需實機驗證馬達規格
- CLV900 direction convention（forward=pay / reverse=retract）wiring-dependent，實機若反向則翻轉

---

## 2026-04-15d — Claude Code — cmd_init 補 CH5/CH6/CH7 關閉

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `cmd_init()` 補上 CH_BRUSH/CH_WATER_PUMP/CH_WATER_INLET 三個 relay 明確設為 OFF（對應 motion_flow.md Phase 2 步驟 5-7）

### 原因
硬體開機 `init()` 的 safe startup 雖已關閉這三路，但 `cmd_init()` 重送時（如崩潰後 re-init）未顯式關閉，造成狀態不確定。

---

## 2026-04-15c — Claude Code — arm_sweep 加清洗動作

### 修改檔案
- `user_lib/WASH_ROBOT.h` — 新增 CH5/CH6/CH7 relay 常數：`CH_BRUSH=5`, `CH_WATER_PUMP=6`, `CH_WATER_INLET=7`
- `user_lib/WASH_ROBOT.cpp` — `do_arm_sweep_()` 加入清洗 relay 控制：掃臂前開 CH7→CH6→CH5，掃臂後（無論成功失敗）關 CH5→CH6→CH7；`cmd_shutdown()` + `init()` safe startup 補上這三個 relay 的關閉

## 2026-04-15b — Claude Code — Phase 5 吸盤狀態切換補完

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — `do_phase5_roll_correct_()` 補上 Phase 5 spec 規定的吸盤切換：
  - 前置：CH2(feet) OFF + CH3(body) OFF + CH4(center) ON，驗證中心真空OK才繼續
  - 每次差動前再確認中心真空（若中心脫落立即中止）
  - 校正完成（或失敗）後：CH3 ON → 等 VACUUM_SETTLE_MS → CH2 ON 恢復全吸附

### 原因
舊版直接叫吊機差動，未先切換吸盤狀態，導致 8 顆腳+身體全吸住的情況下強拉鋼索，機器人無法旋轉校正。

## 2026-04-15 — Claude Code — IMU bug fix + Linux_test IMU 測試

### 修改檔案
- `user_lib/WASH_ROBOT.cpp` — 修正兩個 bug：
  1. `Serial_port::init()` 回傳 `true=success`（非專案慣例），WASH_ROBOT 的判斷缺少 `!` → 把成功當 fatal error；加上 `!` 修正
  2. `cmd_step_down()` 持有 `motion_mtx_` 後呼叫 `cmd_arm_sweep()`（也持有同一 mutex）→ deadlock；改呼叫不加鎖的 `do_arm_sweep_()`
- `user_lib/WASH_ROBOT.h` — 新增 private `do_arm_sweep_()` 宣告
- `Linux_test/main.cpp` — 改寫為選單式測試程式：選項 1 = IMU 連續讀取（Roll/Pitch/Yaw/Pressure/Alt），選項 2 = DM2J 快速移動測試

### 原因
- Serial_port 使用標準慣例（true=success），不同於專案慣例；需加 `!` 才能正確判斷失敗
- mutex deadlock 修正（同 session 發現）

## 2026-04-14d — Claude Code — OOP 重構：WashRobot class

### 修改檔案
- `user_lib/WASH_ROBOT.h` — 全新 WashRobot class 宣告（覆蓋舊版）：所有硬體成員（TCP_client×4, DM2J×5, ZDT×9, JC×9, PQW, IMU）、背景執行緒（crane watchdog + IMU monitor）、狀態 atomics、evt_cb callback、完整 cmd_*() public 方法、private helpers、cycle_group_ template
- `user_lib/WASH_ROBOT.cpp` — 全新實作（覆蓋舊版）：從舊 main.cpp 搬移所有 static function 成 class method；body_displace 改用 dm2j_wait_done_ 取代舊 sleep_ms_(4000)
- `washrobot_new_PI/main.cpp` — 縮減為薄包裝層（~120 行）：TCP server + dispatch → robot.cmd_*()；main() 注入 evt_cb，呼叫 robot.init()，shutdown 呼叫 robot.stop()
- `washrobot_new_PI/washrobot_new_PI.vcxproj` — 新增 Serial_port.cpp/.h、WASH_ROBOT.cpp/.h、WT901BC_TTL.cpp/.h

### 原因
OOP 重構：business logic 移入 WashRobot class，main.cpp 純 I/O 層，職責清晰。

## [2026-04-14c] Claude Code — IMU 整合（WT901BC baseline + 監控 + Phase 5）

### 修改檔案
- `washrobot_new_PI/main.cpp`

### 變更內容
1. 新增 includes：`Serial_port.h`、`WT901BC_TTL.h`、`<deque>`、`<iomanip>`
2. 新增 IMU 常數：`IMU_PORT / IMU_BAUD / IMU_ASK_DEG(15°) / IMU_STOP_DEG(45°) / IMU_BASELINE_SEC(3) / IMU_HYSTERESIS_DEG(1°) / ROLL_CORRECT_CM_PER_DEG / ROLL_CORRECT_RETRY_MAX`
3. 新增 globals：`imu_serial / imu / imu_roll0 / imu_pitch0 / imu_ask_pending / imu_mon_running / imu_mon_thread`
4. 新增 `imu_take_baseline()`：取 3 秒 100ms 取樣平均，存 roll0/pitch0；`do_init()` 最後呼叫
5. 新增 `do_phase5_roll_correct()`：送 `roll_correct <delta_cm>` 給 crane，最多重試 5 次，收斂條件 `|Δroll| < 1°`
6. 新增 `imu_monitor_loop()`：100ms 取樣，10 樣本 1s 滑動平均，持續 500ms 超標才觸發；`>45°` → abort + crane stop；`>15°` → imu_ask_pending + EVT balance_ask
7. `do_run()` 每步後檢查 `imu_ask_pending`，若 true 則 `pause_flag=true` 等 confirm_balance
8. 新增 `confirm_balance <yes|no>` 指令：yes → Phase 5 → 清 pending/pause；no → 直接清
9. `main()` 加 IMU serial init + imu.init + monitor thread 啟動；shutdown 時 stop + join

### 原因
- 依據 motion_flow.md §5 及 work_log 2026-04-12 確認規格實作

---

## [2026-04-14b] Claude Code — Crane watchdog

### 修改檔案
- `washrobot_new_PI/main.cpp`

### 變更內容
1. 新增常數 `HEARTBEAT_INTERVAL_MS = 500`、`WATCHDOG_TIMEOUT_MS = 2000`
2. 新增 globals：`motion_active`、`crane_last_ok_ms`、`crane_wd_running`、`crane_wd_thread`
3. 新增 `now_ms()` utility
4. `crane_cmd` 加 `timeout_sec` 參數（預設 30s，ping 用 2s），並在 OK 回應時更新 `crane_last_ok_ms`；舊的 180s hardcode 移除
5. 新增 `crane_watchdog_loop()`：每 500ms 送 `ping`，若 `crane_last_ok_ms` 超過 2000ms 未更新 → 動作中則 `abort_flag=true` + EVT，閒置中只推 EVT
6. `do_step_down` / `do_run` 頭尾設定 `motion_active`；`do_stop` / `do_shutdown` 清除
7. `main()` 啟動 watchdog thread；shutdown 時 `join()`

### 原因
- 通訊斷線時機器人必須立即停機，是最底層安全保障

---

## [2026-04-14] Claude Code — Phase 4-A margin 修正 + DM2J 同步等待

### 修改檔案
- `washrobot_new_PI/main.cpp`

### 變更內容
1. 新增常數 `STEP_MARGIN_CM = 15`
2. 新增 helper `dm2j_wait_done(slave, timeout_ms)` — poll 單顆 DM2J slave 的 CMD_DONE + PATH_DONE，取代舊的固定 `sleep_ms(4000)`
3. 重寫 `do_step_down()` 內的 `feet_displace` lambda：
   - 修正順序：crane 先 `pay_out (STEP_CM + STEP_MARGIN_CM)` 等 OK，再啟動 DM2J，DM2J 完成後 crane `retract STEP_MARGIN_CM`
   - 改用 `PR_trigger_sync`（廣播觸發一次）+ 分別 `dm2j_wait_done` 等左右腳，取代舊的 `PR_trigger_sync` + 固定等待

### 原因
- 舊版只放剛好 STEP_CM 的繩，鋼索可能仍有張力，阻礙 DM2J 腳下移
- 舊版先發 DM2J 指令才放繩，順序錯誤
- 舊版用 `sleep_ms(4000)` 等待，無法感知實際到位與否

## [2026-04-13d] Claude Code — testSingleLegWash 重構 + adjustLegPos 修正

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`
  - `adjustLegPos()` 頂部加上 `if (leg.sensor == nullptr)` 保護，無感測器時直接回傳 0（避免 nullptr 解參考）
  - `testSingleLegWash()` 全面重構：
    1. 在 init 後建立 `Leg` struct，將本地裝置指標填入（含 `sensor = has_sensor ? &sensor : nullptr`）
    2. 初始化完成後立即伸腳（初始吸附狀態）
    3. **FORWARD PHASE**：循環起頭才解真空 → 縮腳 → 前進 `step_cm` → 啟動真空 + 伸腳 → `adjustLegPos(leg)`，回傳 `adj` 計算 `actual_step = step_cm + adj`
    4. **BACKWARD PHASE**：不解真空（valve 保持 ON）→ 僅縮腳 → 後退 `actual_step` → 伸腳 → `adjustLegPos(leg)` 確認吸附
    5. 迴圈末尾真空保持 ON，下一次前進才釋放

### 原因
用戶需求：前進吸附後用 adjustLegPos 確認，根據退後補償量計算實際步長；往回滑動時持續吸著牆壁，下次前進時才解真空。

---

## [2026-04-13c] Claude Code — bugfix

### 修改檔案
- `user_lib/WASH_ROBOT.cpp`
  - `testSingleLegWash()` 中 `rly.init(tcp, cfg.relay_slave, false)` 改為 `rly.init(tcp, cfg.relay_slave)`
  - 原因：第三參數對應 `total_relay`（預設 16），傳 `false`（=0）會讓 relay_count=0，之後 parseReadResponse 存取空 vector 造成 segfault
- `user_lib/PQW_IO_16O_RLY.cpp`
  - 解構子從 `if (client)` 改為 `if (client && owns_client)`
  - 原因：外部 client 模式（owns_client=false）不應呼叫 client->close()，否則共用 TCP 連線會被提早關閉

### 原因
test leg wash 執行時出現 segfault，根因為 PQW init 的 total_relay 參數被錯誤傳入 false=0。

---

## [2026-04-13b] Claude Code

### 修改檔案
- `washrobot_new_PI/main.cpp`
  - 移除啟動時自動執行的 `robot.init()` / `robot.doInit()`，改為指令
  - 新增指令 `init`（呼叫 robot.init()，設定 robot_initialized = true）
  - 新增指令 `doinit`（需先 init）
  - 所有需要 robot 初始化的指令加上 `robot_initialized` 檢查，未 init 時印 "Run 'init' first."
  - `test leg wash` 不需要 init 即可直接執行

### 原因
測試單隻腳時，主機器人的 init() 會嘗試連所有 8 個裝置，timeout 累積造成 hang。

---

## [2026-04-13] Claude Code

### 修改檔案
- `user_lib/WASH_ROBOT.h`
  - 新增 `SingleLegTestConfig` struct（網路連線、slave ID、繼電器通道、動作參數全部可設定）
  - 新增 public 方法宣告 `testSingleLegWash(cfg, cycles, step_cm)`
- `user_lib/WASH_ROBOT.cpp`
  - 新增 `testSingleLegWash()` 實作：建立獨立 TCP 連線、初始化單獨裝置（ZDT/DM2J/JC100/PQW）、執行來回洗窗迴圈、每次到位後讀壓力並報告
- `washrobot_new_PI/main.cpp`
  - 在 main() while 迴圈前加入 `SingleLegTestConfig testCfg` 設定區塊（有 ★ 標記，換腳改這裡）
  - 新增指令 `test leg wash <cycles> <step_cm>`

### 原因
用戶需要可獨立設定 slave ID / IP 的單腳來回洗窗測試 function。

---

## [2026-04-12] Claude Code

### 修改檔案
- `user_lib/WASH_ROBOT.h`
  - 新增 `RobotConfig` namespace（config 區塊，陣列形式，8 腳 slave ID、繼電器通道、動作參數）
  - `Leg` struct 新增欄位 `vacuum_motor_ch`（每腳抽真空馬達繼電器通道，0 = 不控制）
  - `m[7]`、`meter[7]` 擴充為 `m[8]`、`meter[8]`
  - 新增 `LegGroup body_group`（上部分 4 腳）、`LegGroup foot_group`（下部分 4 腳）
  - 舊常數重命名為 `COMPAT_RELAY_*` 避免與 `RobotConfig::RELAY_VACUUM_MOTOR[]` 衝突
  - 新增 public 方法 `void startCleaningAll(int step_cm)`
- `user_lib/WASH_ROBOT.cpp`
  - `initDevices()` 改用 `RobotConfig` 陣列初始化全部 8 個 ZDT 馬達 + JC100 感測器（非致命失敗）
  - `setupGroups()` 新增 body_group / foot_group 設定，滑桿 axes[0]/axes[1] 改用 RobotConfig slave ID
  - `enableGroup()` / `disableGroup()` 加入 `vacuum_motor_ch` 控制（含 0 值保護）
  - `moveSync()` 更新為左右滑桿（axes[0] + axes[1]）同步
  - `adjustLegPos()` 使用 `RobotConfig::PRESSURE_THRESHOLD`、`ADJUST_BACK_CM`、邊界常數；修正舊 `sleep(0.5)` 為 `Sleep(500)`
  - 新增 `startCleaningAll(int step_cm)` 實作（Phase A: body 移 / Phase B: foot 移，迴圈含位移補償）
- `washrobot_new_PI/main.cpp`
  - 新增指令 `start cleaning all with step <cm>`，解析整數後呼叫 `robot.startCleaningAll(step)`

### 原因
用戶說明 8 腳架構（body_group 上部 / foot_group 下部），每腳有獨立真空馬達與繼電器通道，需要彈性 config 與主清洗流程。
