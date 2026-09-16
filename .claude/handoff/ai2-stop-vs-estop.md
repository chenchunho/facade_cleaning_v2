# AI-2:v3 GUI —— 「中止」與「急停」分家(用詞 + 收尾提示)

> 2026-09-16 per user:「**中止跟急停容易被誤解,而且中止還吸附在牆上容易被誤操作**」。
> 本體側的硬守衛與後端的收腳步驟**已由 agent-ai-db 改好並部署到真機**(body + server.js)。
> 你只做 v3 GUI 的用詞與收尾提示。

## 1. 已經改掉的(契約,不要動)

| 位置 | 變更 |
|---|---|
| 本體 `crane_goto` | **吸附中或壓力讀不到 ⇒ 拒絕**:`ERR cups_attached sealed=N unreadable=M — 吸附中不可移動吊機,請先 \`pusher all retract\`(強制:crane_goto <cm> force)`,同時廣播 `EVT crane_goto_blocked cups_attached sealed=N unreadable=M` |
| 本體 `crane_goto <cm> force` | 明知還吸著也移動(救援用),放行但廣播 `EVT crane_goto_forced cups_attached …` |
| 本體 `crane_goto` 壞參數 | `ERR usage:crane_goto_<height_cm>_[force]` |
| `server.js` missionStop | 由四步變**五步**:④ 之後、**python 行程真的結束時**再送 `pusher all retract` + `pump off`(log 行 `[web] STOP ⑤：…`)。所以**中止完成後機器是純吊在繩上,不再吸附**。 |
| `harness/fake_robot.py` | 同步鏡像以上三種回覆與兩個 EVT(已驗:吸附中拒絕 / force 放行 / 收腳後放行) |

⚠️ 沒有改的:Manual 吊機卡的 **▲▼ 拉繩/放繩、緊急收繩**是直連吊機 :5002 的,**吊機不知道吸盤狀態,擋不到**。見第 2 節第 3 點。

## 2. 要做的

1. **用詞分家**——兩顆按鈕的名字要自己說出後果,不要再叫「中止 / 急停」:
   - 中止 → **「停止作業」**(一般色;語意是正常結束)
   - 急停 → **「緊急脫離」**(紅色、與前者**視覺上隔開**、需二次確認或長按)
   兩顆的 tooltip 各寫一行差異:停止作業=停腳本、收臂、**收腳**、關幫浦,機器留在原高度吊著;
   緊急脫離=立刻九步全關全收並進 Error。
2. **停止後的狀態提示**:收到 `[web] STOP ⑤` 這行之後,壓力還沒回到大氣前(`p5..p8` 任一 ≤ −40)
   顯示常駐橫幅:「**停止中:機器仍吸附在牆上、腳伸出** —— 吊機移動已被本體鎖住」,
   四顆都 > −40 之後改成「已脫離牆面,純吊在繩上」並在數秒後自動收起。
3. **吸附中把會動吊機的按鈕變灰**(Manual 三顆「拉到…」、Mission 控制列 ⤒、前置 ⑦,**以及 ▲▼ 拉繩/放繩**):
   判據用 status 的 `p5..p8` 任一 ≤ −40 或該顆出現在 `p_err=`。
   🔴 ▲▼ 那組**後端擋不到**(直連吊機),所以 GUI 這一層是它唯一的防線 —— 灰掉之外還要給一行說明。
   旁邊直接放一顆**「收腳」**(送本體 `pusher all retract`),讓操作員一步就能解鎖。
4. 收到 `EVT crane_goto_blocked` 就用醒目色印出來,不要讓它跟一般 log 混在一起。
   `EVT crane_goto_forced` 更要醒目(那是有人硬幹)。

## 3. 驗收(fake_robot 已支援,用 `./harness/gui_offline.sh`)

```
pusher all extend_raw 5 ; vacuum on      # 讓四顆吸住(p→ −62)
crane_goto 200        → ERR cups_attached sealed=4 unreadable=0 … + EVT crane_goto_blocked
crane_goto 200 force  → 放行 + EVT crane_goto_forced
pusher all retract ; vacuum off
crane_goto 200        → 正常往下走
```
`./harness/gui_offline.sh report` 不得出現 `?? not modelled`。

## 4. 回報

改完 `.claude/changelog.md` 記一條,版號照 `scripts/deploy_web.sh` 流程更新,SendMessage 給 `agent-ai-db`。
**不要 commit**(全 repo 目前都未 commit,等 user 說)。
