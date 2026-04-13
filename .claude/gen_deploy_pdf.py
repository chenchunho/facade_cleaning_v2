# -*- coding: utf-8 -*-
"""Generate the deploy-and-test PDF for washrobot_new_PI.
Run:  python .claude/gen_deploy_pdf.py
Output: deploy_and_test.pdf (project root)
"""
import os
from fpdf import FPDF

FONT_TTC = r"C:\Windows\Fonts\msjh.ttc"
FONT_BOLD_TTC = r"C:\Windows\Fonts\msjhbd.ttc"
OUT = os.path.join(os.path.dirname(__file__), "..", "deploy_and_test.pdf")


class PDF(FPDF):
    def header(self):
        self.set_font("CJK", "B", 10)
        self.set_text_color(120, 120, 120)
        self.cell(0, 6, "washrobot_new_PI — 正式環境部署 + 測試步驟", 0, 0, "L")
        self.cell(0, 6, "2026-04-12", 0, 1, "R")
        self.set_draw_color(200, 200, 200)
        self.line(15, 22, 195, 22)
        self.ln(4)
        self.set_text_color(0, 0, 0)

    def footer(self):
        self.set_y(-12)
        self.set_font("CJK", "", 8)
        self.set_text_color(120, 120, 120)
        self.cell(0, 6, f"第 {self.page_no()} 頁", 0, 0, "C")
        self.set_text_color(0, 0, 0)

    def h1(self, text):
        self.ln(2)
        self.set_font("CJK", "B", 14)
        self.set_fill_color(35, 60, 110)
        self.set_text_color(255, 255, 255)
        self.cell(0, 8, " " + text, 0, 1, "L", fill=True)
        self.set_text_color(0, 0, 0)
        self.ln(2)

    def h2(self, text):
        self.ln(1)
        self.set_font("CJK", "B", 12)
        self.set_text_color(35, 60, 110)
        self.cell(0, 7, text, 0, 1, "L")
        self.set_text_color(0, 0, 0)

    def h3(self, text, color=(60, 60, 60)):
        self.set_font("CJK", "B", 10.5)
        self.set_text_color(*color)
        self.cell(0, 6, text, 0, 1, "L")
        self.set_text_color(0, 0, 0)

    def para(self, text):
        self.set_font("CJK", "", 10)
        self.multi_cell(0, 5.5, text)
        self.ln(1)

    def code(self, text):
        self.set_font("CJK", "", 8.5)
        self.set_fill_color(245, 245, 248)
        self.set_text_color(20, 20, 20)
        for line in text.splitlines():
            self.cell(0, 4.7, "  " + line, 0, 1, "L", fill=True)
        self.set_text_color(0, 0, 0)
        self.ln(1)

    def checklist(self, items):
        self.set_font("CJK", "", 10)
        for it in items:
            x0 = self.get_x()
            y0 = self.get_y()
            self.set_draw_color(80, 80, 80)
            self.rect(x0, y0 + 1.3, 3.2, 3.2)
            self.set_x(x0 + 5)
            self.multi_cell(0, 5.2, it)
            self.ln(0.5)
        self.ln(1)

    def kv_table(self, rows, widths=(70, 110)):
        self.set_font("CJK", "", 9.5)
        self.set_draw_color(200, 200, 200)
        for k, v in rows:
            y = self.get_y()
            self.set_fill_color(240, 240, 245)
            self.cell(widths[0], 6, k, 1, 0, "L", fill=True)
            self.multi_cell(widths[1], 6, v, 1, "L")
        self.ln(1)


def build():
    pdf = PDF(orientation="P", unit="mm", format="A4")
    pdf.set_margins(15, 16, 15)
    pdf.set_auto_page_break(True, 14)
    pdf.add_font("CJK", "",  FONT_TTC)
    pdf.add_font("CJK", "B", FONT_BOLD_TTC)
    pdf.add_page()

    # Title block
    pdf.set_font("CJK", "B", 18)
    pdf.cell(0, 10, "washrobot_new_PI", 0, 1, "L")
    pdf.set_font("CJK", "B", 13)
    pdf.set_text_color(80, 80, 80)
    pdf.cell(0, 7, "正式環境部署 + 測試步驟", 0, 1, "L")
    pdf.set_text_color(0, 0, 0)
    pdf.ln(2)

    # Overview
    pdf.h2("系統拓撲")
    pdf.kv_table([
        ("Crane RPi",     "192.168.1.101  →  TCP server :5002"),
        ("Washrobot RPi", "192.168.1.100  →  TCP server :5001"),
        ("Web Backend",   "Node.js @ washrobot RPi :8080 (橋接 Browser <-> 兩支 TCP)"),
        ("編譯驗證",       "RPi 5 / aarch64 / g++ 14.2.0  → 雙 binary 均 0 warning / 0 error"),
    ], widths=(40, 140))

    # -------------------------------------------------------
    pdf.h1("A. 一次性環境準備（每台 RPi）")

    pdf.h2("Crane RPi @ 192.168.1.101")
    pdf.code(
        "sudo apt update && sudo apt install -y build-essential git\n"
        "mkdir -p ~/washrobot_build/{user_lib,Crane_control_PI}"
    )

    pdf.h2("Washrobot RPi @ 192.168.1.100")
    pdf.code(
        "sudo apt update && sudo apt install -y build-essential git curl\n"
        "\n"
        "# Node.js 20.x (給 web_backend 用)\n"
        "curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -\n"
        "sudo apt install -y nodejs\n"
        "\n"
        "mkdir -p ~/washrobot_build/{user_lib,washrobot_new_PI,web_backend}"
    )

    # -------------------------------------------------------
    pdf.h1("B. 從開發機 scp 原始碼")
    pdf.para("從 D:/Desktop/agent_ai/projects/washrobot_new_PI/ 執行：")
    pdf.code(
        "# Crane\n"
        "scp -r user_lib                user@192.168.1.101:~/washrobot_build/\n"
        "scp Crane_control_PI/main.cpp  user@192.168.1.101:~/washrobot_build/Crane_control_PI/\n"
        "\n"
        "# Washrobot\n"
        "scp -r user_lib                user@192.168.1.100:~/washrobot_build/\n"
        "scp washrobot_new_PI/main.cpp  user@192.168.1.100:~/washrobot_build/washrobot_new_PI/\n"
        "scp -r web_backend             user@192.168.1.100:~/washrobot_build/"
    )

    # -------------------------------------------------------
    pdf.h1("C. 編譯（兩台各自執行）")

    pdf.h2("Crane RPi")
    pdf.code(
        "cd ~/washrobot_build\n"
        "g++ -std=c++17 -O2 -Wall -Iuser_lib -pthread \\\n"
        "    Crane_control_PI/main.cpp \\\n"
        "    user_lib/{TCP_client,TCP_server,ZS_DIO_R_RLY,SD76_length_meters}.cpp \\\n"
        "    -o crane"
    )

    pdf.h2("Washrobot RPi")
    pdf.code(
        "cd ~/washrobot_build\n"
        "g++ -std=c++17 -O2 -Wall -Iuser_lib -pthread \\\n"
        "    washrobot_new_PI/main.cpp \\\n"
        "    user_lib/{TCP_client,TCP_server,DM2J_RS570,ZDT_motor_control,JC_100_METER,PQW_IO_16O_RLY}.cpp \\\n"
        "    -o washrobot"
    )

    pdf.h2("web_backend (washrobot RPi)")
    pdf.code("cd ~/washrobot_build/web_backend && npm install")

    # -------------------------------------------------------
    pdf.h1("D. 啟動（tmux 或三個 terminal）")
    pdf.code(
        "# Terminal 1 - crane RPi\n"
        'ssh user@192.168.1.101 "cd ~/washrobot_build && ./crane"\n'
        "\n"
        "# Terminal 2 - washrobot RPi\n"
        'ssh user@192.168.1.100 "cd ~/washrobot_build && ./washrobot"\n'
        "\n"
        "# Terminal 3 - web backend (washrobot RPi)\n"
        'ssh user@192.168.1.100 "cd ~/washrobot_build/web_backend && node server.js"'
    )
    pdf.para("瀏覽器開 http://192.168.1.100:8080 ，兩顆連線燈應轉綠。")

    # -------------------------------------------------------
    pdf.add_page()
    pdf.h1("上機前冒煙測試（按順序，每關 pass 才進下一關）")

    pdf.h3("Gate 0 — 裸啟動（硬體全部斷電）", (180, 0, 0))
    pdf.checklist([
        "./crane 印 [FATAL] connect USR 192.168.1.30:4001 failed 後退出 (TCP 連不到 USR 的錯誤處理正常)",
        "./washrobot 同樣在第一個 connect 失敗時退出",
    ])

    pdf.h3("Gate 1 — USR 轉換器通電，下位機斷電", (180, 0, 0))
    pdf.para("USR-TCP232-304（.20 / .21 / .22 / .30）全部通電並接網路，Modbus 下位機不通電。")
    pdf.checklist([
        "./crane 通過 TCP connect，但在 ZS_DIO_R_RLY init 失敗時印 FATAL 退出（slave 無回應）",
        "./washrobot 同上，在 DM2J/ZDT/JC-100/PQW 任一 init 處退出",
    ])

    pdf.h3("Gate 2 — 單裝置通電逐顆驗", (200, 130, 0))
    pdf.para("每次只通電一顆下位機，用 nc <ip> <port> 或 Web GUI 的 raw command 下單指令：")
    pdf.checklist([
        "Crane ZS_DIO slave 1:  pay_out_left on / pay_out_left off  → 聽繼電器 click",
        "Crane SD76 slave 2/3:  status → OK length_left=<num> length_right=<num>",
        "Washrobot PQW slave 12: 逐一 vacuum feet|body|center on/off",
        "Washrobot JC-100 slave 1~9:  status → 9 個 pN=xxx（大氣壓附近值）",
        "Washrobot ZDT slave 1~9:  pusher feet extend → 兩顆推桿同步走位",
        "Washrobot DM2J slave 1/3/5:  move left_foot 5 / move right_foot 5 / move arm 10",
    ])

    pdf.h3("Gate 3 — 兩支 C++ 全通電、不接 Web", (200, 130, 0))
    pdf.para("用 nc 192.168.1.100 5001 / nc 192.168.1.101 5002 直接對話：")
    pdf.checklist([
        "Crane 六個手動 channel（pay_out_left/right、retract_left/right × on/off）逐一驗",
        "Crane pay_out 10 雙繩同步，SD76 各自停",
        "Crane 長指令執行中另一 client 下 stop 能中斷",
        "Washrobot init / attach（先人工貼牆）/ detach 三招通過",
        "Washrobot attach 故意堵一顆吸盤 → 回 ERR attach_vacuum_fail slaves=X",
    ])

    pdf.h3("Gate 4 — Web GUI 整合", (0, 130, 60))
    pdf.checklist([
        "瀏覽器開 http://192.168.1.100:8080，兩顆燈轉綠",
        "點 init / attach / vacuum feet on — log 顯示 TX/RX 對應行",
        "kill ./crane → crane 燈變紅；重啟 crane 三秒內自動轉綠（reconnect）",
        "同時開兩個瀏覽器 tab — 兩邊都能送指令、都能看到對方的回應",
        "紅色 STOP (robot) / STOP (crane) 按鈕能分別緊急停",
    ])

    pdf.h3("Gate 5 — 不上牆 dry-run 整套流程", (0, 130, 60))
    pdf.para("4 腳不貼牆，人工撐著機器人：")
    pdf.checklist([
        "init → 泵浦啟、腳+身體推桿伸、中心縮（手扶住機器人確認推桿動作）",
        "人工貼牆 → attach → 9 顆真空都過 -50 kPa",
        "step_down（單步）觀察時序：\n"
        "   A. 腳組:  valve off → 推桿縮 → 腳軌移 30cm + crane 放 30cm → 推桿伸 → valve on → 真空驗\n"
        "   B. 身體+中心:  valve off → 推桿縮 → 腳軌歸零 → 推桿伸 → valve on → 真空驗\n"
        "   C. arm sweep 右 → 左 → 回零",
        "run 3 連跑三步，log 出現 EVT step 1/3 … 2/3 … 3/3",
        "run 3 中途按 STOP — 當前動作完成後停",
    ])

    pdf.h3("Gate 6 — 上牆全流程", (0, 130, 60))
    pdf.para("頂樓吊機吊上、4 輪貼玻璃：")
    pdf.checklist([
        "人工 attach 驗 9 顆吸盤都到位",
        "run 1 先走一步看實際下移 + crane 放繩同步",
        "tilt_mode on → 關 8 顆只留中心 → 手動 crane 左右收放 → 看 WT901BC 姿態值",
        "回填實測參數到 main.cpp：\n"
        "   PUSHER_EXTEND_PULSE (對應 10cm 貼牆的 pulse 數)\n"
        "   STEP_CM / TOTAL_DISTANCE_CM\n"
        "   ARM_SWEEP_CM / ARM_SWEEP_RPM\n"
        "   VACUUM_THRESHOLD_X10（依實測最差值）",
        "重新編譯部署後  run N  （N = 樓高 / STEP_CM） 跑到底",
    ])

    # -------------------------------------------------------
    pdf.h1("緊急操作備忘")
    pdf.kv_table([
        ("任一環節異常",  "Web GUI 紅色 STOP (robot) + STOP (crane)"),
        ("機器人卡住",    "tilt_mode on  +  手動 crane 收/放繩微調"),
        ("Web 失聯",      "SSH 到任一 RPi  →  nc localhost 5001 或 5002  →  stop"),
        ("最後手段",      "切斷 PQW CH1（泵浦）+ 吊機電源"),
    ], widths=(45, 135))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    pdf.output(OUT)
    print("Wrote", os.path.abspath(OUT))


if __name__ == "__main__":
    build()
