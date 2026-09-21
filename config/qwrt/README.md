# QWRT WiFi 橋(本體 ↔ 吊機)— 設定副本(2026-09-19)

🔴 **這裡是副本,權威版在 AP 上**(`192.168.1.250`,root/password)。AP 重灌時照這份重建。
機型 Q-WRT 25.06 / MT7628 / kernel 4.4;`ra0`=自己的 AP(`QWRT-2.4G`)、`apcli0`=client 端。

## 拓樸
```
本體 washrobot 192.168.1.100 ─(有線)─ QWRT br-lan ─ apcli0 ~~2.4G~~ facade_cleaning_2.4G(主路由 192.168.1.1)─ 吊機 192.168.1.10
```
L2 橋接:`apcli0` 進 `br-lan`,MediaTek 驅動內建 **MAT(MAC 轉譯)** 讓有線端裝置穿過 3-address client 連線
(筆電插 LAN 口可直接向主路由租到 `192.168.1.x`,實測 OK)。

## uci(一次性)
```sh
uci set network.lan.ipaddr='192.168.1.250'; uci set network.lan.gateway='192.168.1.1'; uci set network.lan.dns='192.168.1.1'
uci delete network.lan.ip6assign
uci set network.lanfb=interface; uci set network.lanfb.ifname='@lan'; uci set network.lanfb.proto='static'
uci set network.lanfb.ipaddr='192.168.100.1'; uci set network.lanfb.netmask='255.255.255.0'      # 備援管理位址
uci set network.@switch_vlan[0].ports='0 1 2 3 4 6t'; uci delete network.@switch_vlan[1]            # 5 口全 LAN,WAN VLAN 刪除
uci set network.wan.proto='none'; uci delete network.wan6; uci delete network.wan_dev
uci set dhcp.lan.ignore='1'; uci set dhcp.lan.dhcpv6='disabled'; uci set dhcp.lan.ra='disabled'    # DHCP 關,由主路由發
uci set wireless.ra.htmode='HT20'                                                                    # 2.4G 太擠,HT40 只會更糟
# wireless.sta 已存在:device='ra' mode='sta' ssid='facade_cleaning_2.4G' network='lan' encryption='none'
uci commit network; uci commit dhcp; uci commit wireless
```
再放 `50-apcli-bridge` 到 `/etc/hotplug.d/net/`(chmod +x)、`rc.local` 到 `/etc/rc.local`,重開機驗證
`logread | grep apcli-bridge` 兩條都有、`brctl show` 含 `apcli0`。

## 踩坑
- `wireless.sta.network='lan'` **不會**把 apcli0 加進 br-lan(`brctl show` 沒它)→ hotplug + rc.local 兩層保險。
- 「3-address client 帶不動後面裝置」是 09-18 **沒實測**就下的結論,MTK 驅動有 MAT 就能過;別再繞去 WDS/relayd。
- `swconfig dev switch0 show` 的 `link:` **不可信**(本體 0.5 ms ping 得到、它顯示 link down)。
- 改 `network` 後 `/etc/init.d/network restart` 會斷 SSH 十幾秒,正常。
- RF 品質是真正的瓶頸:09-19 量到 RSSI −70、Tx PER 83%、本體→吊機 30–40% 掉包、一次斷 285 s。
  HT20 + 關 Block-Ack 後 3%,但**主路由改 20 MHz、拉近距離**才是解;長期換 5 GHz 或有線。
