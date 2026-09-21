# systemd units(2026-09-16 起四支程式開機自動啟動;v2 GUI 的 `fcv-web` 同日退役)

這裡是**真機上那五個 unit 檔的副本**,收進 repo 的唯一理由:在此之前它們**只存在兩台 Pi 的檔案系統裡**
—— Pi 重灌、home 被清、或換一台機器,自動啟動的設定就沒了,而且沒有任何地方記得它長什麼樣。
🔴 **這裡是副本不是權威版**:真正在跑的是 Pi 上那份。改了這裡不會生效,改了 Pi 上那份請記得同步回來。

| unit | 主機 | 層級 | 程式 | 對外 |
|---|---|---|---|---|
| `fcv-crane.service`  | 吊機 `user@192.168.5.25`   | **system**(該機有 sudo) | `~/run/crane_control_PI.out` | TCP :5002 |
| `fcv-web-v3.service` | 吊機 .25 | system | `node server.js`(v3 主控台) | HTTP :8080(09-16 由 8081 改回,v1 退役後接手主埠) |
| `fcv-arm.service`    | 本體 `nexuni@192.168.5.26` | **user**(此機 sudo 密碼不在 Claude 手上)| `~/projects/…/cleaning_arm/motor_api` | TCP :9527 |
| `fcv-body.service`   | 本體 .26 | **user** | `~/run/facade_cleaning_v2.out` | TCP :5001 |

## 兩個非顯而易見的設計

### 1. `exec 3<> fifo` —— 讓 stdin 永遠開著但永遠沒資料

三支 C++ 程式都有 console,而 console 讀到 **EOF 會讓程式退出**。systemd 預設把 stdin 接到
`/dev/null` ⇒ 開機起來立刻 EOF ⇒ 程式當場結束。開一個 fifo 並且**自己同時持有讀端與寫端**
(`exec 3<> path`)就得到一個「開著、但不會 EOF」的 stdin。
副作用是好的:外部仍可 `echo <指令> > ~/run/body.in` 從 shell 餵指令進 console。

### 2. 本體是 user service + linger

本體 Pi 的 sudo 密碼不在 Claude 的密碼簿裡(在使用者那本),所以用 user unit:
```
systemctl --user enable --now fcv-arm fcv-body
sudo loginctl enable-linger nexuni     # ← 沒有這行,登出就被殺、開機也不會起
```
指令一律帶 `--user`(`systemctl --user restart fcv-body`),`scripts/deploy.sh` 已經處理好。

### 3. `fcv-crane` 的 `ExecStartPost` 是必要的,不是裝飾

吊機有三個執行期參數**不落地**,重啟一律回預設:`home_ground_cm`→0(座標慣例會被 cycle_test 誤判)、
`wall_height_cm`→0(`goto` 直接拒絕)、`motion_hz`→編譯預設。所以 ExecStartPost 等 20 s 後補送一次,
牆高從 web 存的 `~/run/wall_height.json` 讀。

### 4. official 用 drop-in 蓋值,unit 檔兩台共用(2026-09-19)

`official/` 是正式機三個 drop-in + `wait_devices.sh` 的副本。unit 檔本身在 official 只改了
`/home/user`→`/home/nexuni`、`User=nexuni`;所有位址/站號/校正值都在 drop-in
(`fcv-crane` 的 `endpoints.conf` 就是 `config/official-crane_485.md` 的機器可讀版)。

🔴 **`ExecStartPre=wait_devices.sh` 不是裝飾**:官方機 Pi 比 PoE 交換器/USR 網關早 ~40 s 起來,
沒有這道閘門 fcv-crane 開機就把所有裝置標成 skipped(旗標只在 init 設一次),要人手動 restart。
腳本**必須是 bash**(`/dev/tcp`),用 `#!/bin/sh` 會每次白等 180 s、service 卡在 activating。

本體那個 drop-in 是 **user** unit(`~/.config/systemd/user/fcv-body.service.d/`),
原因見第 2 節;沒有它本體會一直敲測試吊機 `.5.25` 的門(unit 檔寫死了 env,有 env 就不探測有線)。

## 安裝(重灌後)

```bash
# 吊機 .25
scp scripts/systemd/fcv-{crane,web-v3}.service user@192.168.5.25:/tmp/
ssh user@192.168.5.25 'sudo cp /tmp/fcv-*.service /etc/systemd/system/ && sudo systemctl daemon-reload && sudo systemctl enable --now fcv-crane fcv-web-v3'

# 本體 .26(user service)
scp scripts/systemd/fcv-{arm,body}.service nexuni@192.168.5.26:~/.config/systemd/user/
ssh nexuni@192.168.5.26 'systemctl --user daemon-reload && systemctl --user enable --now fcv-arm fcv-body'
# ↓ 這行要 sudo,請使用者自己執行
ssh nexuni@192.168.5.26 'sudo loginctl enable-linger nexuni'
```

```bash
# official 吊機 192.168.1.10(nexuni/123;先把 unit 檔的 /home/user→/home/nexuni、User=nexuni 改掉)
scp scripts/systemd/official/wait_devices.sh nexuni@192.168.1.10:~/run/
scp -r scripts/systemd/official/fcv-crane.service.d scripts/systemd/official/fcv-web-v3.service.d nexuni@192.168.1.10:/tmp/
ssh nexuni@192.168.1.10 'sudo cp -r /tmp/fcv-*.service.d /etc/systemd/system/ && sudo systemctl daemon-reload && sudo systemctl restart fcv-crane fcv-web-v3'
# official 本體 192.168.1.100(user service)
scp -r scripts/systemd/official/fcv-body.service.d nexuni@192.168.1.100:~/.config/systemd/user/
ssh nexuni@192.168.1.100 'systemctl --user daemon-reload && systemctl --user restart fcv-body'
```

日常更新程式用 `scripts/deploy.sh <body|arm|crane|server|script|web|status>`,不必碰這裡。
⚠️ `deploy.sh` 目前只認測試機位址(`.5.25`/`.5.26`);對 official 部署仍是手動 scp(🟡 待做成可選目標)。
🔴 覆蓋執行中的 binary 用 `cp` 會 **Text file busy** 而且錯誤很容易被吞掉——先 `cp` 到 `.new` 再 `mv -f`。
